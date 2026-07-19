#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace cynamodb::engine;
using namespace cynamodb::core;

namespace {

std::shared_ptr<AttributeValue> S(std::string_view s) {
    auto av = std::make_shared<AttributeValue>();
    av->type = AttributeType::S;
    av->value = String(s.data(), s.size());
    return av;
}

std::shared_ptr<AttributeValue> N(std::string_view n) {
    auto av = std::make_shared<AttributeValue>();
    av->type = AttributeType::N;
    av->value = String(n.data(), n.size());
    return av;
}

std::string get_s(const StorageEngine::AttributeMap& item, const std::string& attr) {
    auto it = item.find(attr);
    REQUIRE(it != item.end());
    REQUIRE(it->second);
    return std::string(std::get<String>(it->second->value));
}

StorageEngine::AttributeMap item_with(std::string_view pk_attr, std::string_view pk_val,
                                      std::string_view extra_attr = {}, std::string_view extra_val = {}) {
    StorageEngine::AttributeMap m;
    m[std::string(pk_attr)] = S(pk_val);
    if (!extra_attr.empty()) {
        m[std::string(extra_attr)] = S(extra_val);
    }
    return m;
}

}  // namespace

TEST_CASE("MemoryEngine put/get round-trip", "[engine][memory]") {
    MemoryEngine engine;
    const std::string table = "T";

    SECTION("get on empty table returns nullopt") {
        REQUIRE(!engine.get(table, "missing").has_value());
    }

    SECTION("put then get returns the stored item") {
        auto attrs = item_with("pk", "v1", "data", "hello");
        engine.put(table, "k1", attrs);

        auto got = engine.get(table, "k1");
        REQUIRE(got.has_value());
        REQUIRE(get_s(*got, "pk") == "v1");
        REQUIRE(get_s(*got, "data") == "hello");
    }

    SECTION("get of a different key returns nullopt") {
        engine.put(table, "k1", item_with("pk", "v1"));
        REQUIRE(!engine.get(table, "k2").has_value());
        REQUIRE(!engine.get("OtherTable", "k1").has_value());
    }

    SECTION("overwrite keeps the latest value") {
        engine.put(table, "k1", item_with("pk", "old"));
        engine.put(table, "k1", item_with("pk", "new"));
        auto got = engine.get(table, "k1");
        REQUIRE(got.has_value());
        REQUIRE(get_s(*got, "pk") == "new");
    }
}

TEST_CASE("MemoryEngine remove", "[engine][memory]") {
    MemoryEngine engine;
    const std::string table = "T";

    engine.put(table, "k1", item_with("pk", "v1"));
    REQUIRE(engine.get(table, "k1").has_value());

    SECTION("remove deletes the item") {
        engine.remove(table, "k1");
        REQUIRE(!engine.get(table, "k1").has_value());
    }

    SECTION("removing a missing key is a no-op") {
        engine.remove(table, "nope");
        engine.remove("NoTable", "k1");
        REQUIRE(engine.get(table, "k1").has_value());  // untouched
    }
}

TEST_CASE("MemoryEngine handles tens of thousands of inserts and lookups", "[engine][memory][scale]") {
    MemoryEngine engine;
    const std::string table = "Bulk";
    const int kCount = 20000;

    for (int i = 0; i < kCount; ++i) {
        std::string key = "key-" + std::to_string(i);
        engine.put(table, key, item_with("id", std::to_string(i)));
    }

    // Every single key must be found and carry the right payload.
    for (int i = 0; i < kCount; ++i) {
        std::string key = "key-" + std::to_string(i);
        auto got = engine.get(table, key);
        REQUIRE(got.has_value());
        REQUIRE(get_s(*got, "id") == std::to_string(i));
    }

    // A key that was never inserted must not be found.
    REQUIRE(!engine.get(table, "key-" + std::to_string(kCount)).has_value());
}

TEST_CASE("MemoryEngine scan returns all items in key order", "[engine][memory][scan]") {
    MemoryEngine engine;
    const std::string table = "T";

    // Insert in deliberately scrambled order.
    std::vector<std::string> keys = {"d", "a", "c", "b", "e"};
    for (const auto& k : keys) {
        engine.put(table, k, item_with("pk", k));
    }

    SECTION("unbounded scan is sorted and complete") {
        auto result = engine.scan(table, std::nullopt, 0);
        REQUIRE(result.items.size() == 5);
        REQUIRE(!result.last_evaluated_key.has_value());
        std::vector<std::string> seen;
        for (const auto& it : result.items) {
            seen.push_back(get_s(it, "pk"));
        }
        REQUIRE(seen == std::vector<std::string>{"a", "b", "c", "d", "e"});
    }

    SECTION("scan of an unknown table is empty") {
        auto result = engine.scan("Nope", std::nullopt, 0);
        REQUIRE(result.items.empty());
        REQUIRE(!result.last_evaluated_key.has_value());
    }
}

TEST_CASE("MemoryEngine scan pagination visits every item exactly once", "[engine][memory][scan][pagination]") {
    MemoryEngine engine;
    const std::string table = "Page";
    const int kCount = 1000;

    std::set<std::string> expected;
    for (int i = 0; i < kCount; ++i) {
        // zero-pad so lexical order is well defined
        char buf[16];
        std::snprintf(buf, sizeof(buf), "k%05d", i);
        engine.put(table, buf, item_with("id", buf));
        expected.insert(buf);
    }

    std::set<std::string> collected;
    std::optional<std::string> cursor;
    const size_t page = 37;  // deliberately not a divisor of kCount
    size_t pages = 0;
    do {
        auto result = engine.scan(table, cursor, page);
        REQUIRE(result.items.size() <= page);
        for (const auto& it : result.items) {
            auto [_, inserted] = collected.insert(get_s(it, "id"));
            REQUIRE(inserted);  // no item returned twice
        }
        cursor = result.last_evaluated_key;
        ++pages;
        REQUIRE(pages < 1000);  // guard against an infinite loop
    } while (cursor.has_value());

    REQUIRE(collected == expected);
}

TEST_CASE("MemoryEngine read pages are bounded by data size", "[engine][memory][pagination][memory]") {
    MemoryEngine engine;
    const std::string table = "LargePage";
    const std::string payload(300 * 1024, 'x');
    for (int i = 0; i < 4; ++i) {
        const std::string key = "k" + std::to_string(i);
        engine.put(table, key, item_with("pk", key, "payload", payload));
    }

    auto first_scan = engine.scan(table, std::nullopt, 0);
    REQUIRE(first_scan.items.size() == 3);
    REQUIRE(first_scan.last_evaluated_key.has_value());
    auto final_scan = engine.scan(table, first_scan.last_evaluated_key, 0);
    REQUIRE(final_scan.items.size() == 1);
    REQUIRE_FALSE(final_scan.last_evaluated_key.has_value());

    auto first_query = engine.query(table, {}, std::nullopt, 0);
    REQUIRE(first_query.items.size() == 3);
    REQUIRE(first_query.last_evaluated_key.has_value());
    auto final_query = engine.query(table, {}, first_query.last_evaluated_key, 0);
    REQUIRE(final_query.items.size() == 1);
    REQUIRE_FALSE(final_query.last_evaluated_key.has_value());
}

TEST_CASE("MemoryEngine query filters by equality conditions", "[engine][memory][query]") {
    MemoryEngine engine;
    const std::string table = "Q";

    engine.put(table, "u1#a", item_with("user", "u1", "kind", "a"));
    engine.put(table, "u1#b", item_with("user", "u1", "kind", "b"));
    engine.put(table, "u2#a", item_with("user", "u2", "kind", "a"));

    SECTION("single-attribute partition query") {
        StorageEngine::AttributeMap cond;
        cond["user"] = S("u1");
        auto result = engine.query(table, cond, std::nullopt, 0);
        REQUIRE(result.items.size() == 2);
        // returned in key order: u1#a then u1#b
        REQUIRE(get_s(result.items[0], "kind") == "a");
        REQUIRE(get_s(result.items[1], "kind") == "b");
    }

    SECTION("multi-attribute (composite) query") {
        StorageEngine::AttributeMap cond;
        cond["user"] = S("u1");
        cond["kind"] = S("b");
        auto result = engine.query(table, cond, std::nullopt, 0);
        REQUIRE(result.items.size() == 1);
        REQUIRE(get_s(result.items[0], "kind") == "b");
    }

    SECTION("non-matching query is empty") {
        StorageEngine::AttributeMap cond;
        cond["user"] = S("nobody");
        auto result = engine.query(table, cond, std::nullopt, 0);
        REQUIRE(result.items.empty());
    }

    SECTION("query pagination resumes after last_evaluated_key") {
        StorageEngine::AttributeMap cond;
        cond["user"] = S("u1");
        auto first = engine.query(table, cond, std::nullopt, 1);
        REQUIRE(first.items.size() == 1);
        REQUIRE(first.last_evaluated_key.has_value());
        REQUIRE(get_s(first.items[0], "kind") == "a");

        auto second = engine.query(table, cond, first.last_evaluated_key, 1);
        REQUIRE(second.items.size() == 1);
        REQUIRE(get_s(second.items[0], "kind") == "b");
        REQUIRE(!second.last_evaluated_key.has_value());
    }
}

TEST_CASE("MemoryEngine put_item/get_item/delete_item round-trip", "[engine][memory][item]") {
    MemoryEngine engine;
    TableDefinition def;
    def.table_name = "Users";
    def.key_schema = {{"pk", KeyType::HASH}};
    def.attribute_definitions = {{"pk", AttributeType::S}};

    StorageEngine::AttributeMap item;
    item["pk"] = S("alice");
    item["email"] = S("alice@example.com");
    REQUIRE(engine.put_item(def, item).has_value());

    StorageEngine::AttributeMap key;
    key["pk"] = S("alice");
    auto got = engine.get_item(def, key);
    REQUIRE(got.has_value());
    REQUIRE(get_s(*got, "email") == "alice@example.com");

    SECTION("get_item of a missing key returns ItemNotFound") {
        StorageEngine::AttributeMap missing;
        missing["pk"] = S("bob");
        auto res = engine.get_item(def, missing);
        REQUIRE(!res.has_value());
        REQUIRE(res.error() == StorageError::ItemNotFound);
    }

    SECTION("delete_item removes the item") {
        REQUIRE(engine.delete_item(def, key).has_value());
        auto res = engine.get_item(def, key);
        REQUIRE(!res.has_value());
        REQUIRE(res.error() == StorageError::ItemNotFound);
    }
}

TEST_CASE("MemoryEngine composite keys group by partition and sort by range", "[engine][memory][item][sort]") {
    MemoryEngine engine;
    TableDefinition def;
    def.table_name = "Events";
    def.key_schema = {{"pk", KeyType::HASH}, {"sk", KeyType::RANGE}};
    def.attribute_definitions = {{"pk", AttributeType::S}, {"sk", AttributeType::N}};

    auto put = [&](std::string_view pk, std::string_view sk) {
        StorageEngine::AttributeMap item;
        item["pk"] = S(pk);
        item["sk"] = N(sk);
        REQUIRE(engine.put_item(def, item).has_value());
    };

    SECTION("numeric range keys sort numerically, not lexically") {
        // Inserted out of order. Note 10 > 9 numerically but "10" < "9" lexically.
        for (auto sk : {"100", "9", "10", "-5", "0", "3"}) {
            put("p", sk);
        }
        auto result = engine.scan(def.table_name, std::nullopt, 0);
        REQUIRE(result.items.size() == 6);
        std::vector<std::string> order;
        for (const auto& it : result.items) {
            order.push_back(get_s(it, "sk"));
        }
        REQUIRE(order == std::vector<std::string>{"-5", "0", "3", "9", "10", "100"});
    }

    SECTION("items are grouped by partition then sorted by range") {
        put("b", "2");
        put("a", "10");
        put("a", "2");
        put("b", "1");

        auto result = engine.scan(def.table_name, std::nullopt, 0);
        REQUIRE(result.items.size() == 4);
        std::vector<std::pair<std::string, std::string>> order;
        for (const auto& it : result.items) {
            order.push_back({get_s(it, "pk"), get_s(it, "sk")});
        }
        std::vector<std::pair<std::string, std::string>> expected = {
            {"a", "2"}, {"a", "10"}, {"b", "1"}, {"b", "2"}};
        REQUIRE(order == expected);
    }

    SECTION("distinct range keys under one partition do not collide") {
        put("p", "1");
        put("p", "2");
        put("p", "3");
        auto result = engine.scan(def.table_name, std::nullopt, 0);
        REQUIRE(result.items.size() == 3);
    }
}

TEST_CASE("MemoryEngine string range keys sort lexicographically", "[engine][memory][item][sort]") {
    MemoryEngine engine;
    TableDefinition def;
    def.table_name = "Docs";
    def.key_schema = {{"pk", KeyType::HASH}, {"sk", KeyType::RANGE}};
    def.attribute_definitions = {{"pk", AttributeType::S}, {"sk", AttributeType::S}};

    for (auto sk : {"banana", "apple", "cherry", "apricot"}) {
        StorageEngine::AttributeMap item;
        item["pk"] = S("p");
        item["sk"] = S(sk);
        REQUIRE(engine.put_item(def, item).has_value());
    }

    auto result = engine.scan(def.table_name, std::nullopt, 0);
    std::vector<std::string> order;
    for (const auto& it : result.items) {
        order.push_back(get_s(it, "sk"));
    }
    REQUIRE(order == std::vector<std::string>{"apple", "apricot", "banana", "cherry"});
}

TEST_CASE("MemoryEngine is safe under concurrent writers", "[engine][memory][concurrency]") {
    MemoryEngine engine;
    const std::string table = "Conc";
    const int kThreads = 8;
    const int kPerThread = 5000;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&engine, &table, t, kPerThread] {
            for (int i = 0; i < kPerThread; ++i) {
                std::string key = std::to_string(t) + "-" + std::to_string(i);
                engine.put(table, key, item_with("tid", std::to_string(t)));
            }
        });
    }
    for (auto& th : threads) th.join();

    // All keys from all threads must be present and correct.
    for (int t = 0; t < kThreads; ++t) {
        for (int i = 0; i < kPerThread; ++i) {
            std::string key = std::to_string(t) + "-" + std::to_string(i);
            auto got = engine.get(table, key);
            REQUIRE(got.has_value());
            REQUIRE(get_s(*got, "tid") == std::to_string(t));
        }
    }

    auto result = engine.scan(table, std::nullopt, 0);
    REQUIRE(result.items.size() == static_cast<size_t>(kThreads * kPerThread));
}
