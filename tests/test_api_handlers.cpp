#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {

std::string unique_metadata_path() {
    static std::atomic<uint64_t> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cynamodb_handlers_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}

// A self-contained API harness: table catalog + storage engine + dispatch helper.
struct Harness {
    engine::TableManager tables{unique_metadata_path()};
    engine::MemoryEngine storage;

    api::ApiResult call(api::Operation op, std::string_view body) {
        return api::handle_operation(tables, storage, op, body);
    }

    void create_simple_table(const std::string& name) {
        auto r = call(api::Operation::CreateTable,
                      R"({"TableName":")" + name +
                          R"(","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                          R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})");
        REQUIRE(r.status == 200);
    }
};

}  // namespace

TEST_CASE("CreateTable / DescribeTable / ListTables", "[api][handlers][tables]") {
    Harness h;

    auto created = h.call(api::Operation::CreateTable,
                          R"({"TableName":"Users","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                          R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})");
    REQUIRE(created.status == 200);
    REQUIRE_THAT(created.body, ContainsSubstring("\"TableName\":\"Users\""));
    REQUIRE_THAT(created.body, ContainsSubstring("\"TableStatus\":\"ACTIVE\""));

    SECTION("describe an existing table") {
        auto desc = h.call(api::Operation::DescribeTable, R"({"TableName":"Users"})");
        REQUIRE(desc.status == 200);
        REQUIRE_THAT(desc.body, ContainsSubstring("\"KeyType\":\"HASH\""));
    }

    SECTION("describe a missing table is ResourceNotFoundException") {
        auto desc = h.call(api::Operation::DescribeTable, R"({"TableName":"Ghost"})");
        REQUIRE(desc.status == 400);
        REQUIRE(desc.error_type == "ResourceNotFoundException");
    }

    SECTION("list tables") {
        h.create_simple_table("Second");
        auto list = h.call(api::Operation::ListTables, "{}");
        REQUIRE(list.status == 200);
        REQUIRE_THAT(list.body, ContainsSubstring("\"Users\""));
        REQUIRE_THAT(list.body, ContainsSubstring("\"Second\""));
    }

    SECTION("creating a duplicate table is ResourceInUseException") {
        auto dup = h.call(api::Operation::CreateTable,
                          R"({"TableName":"Users","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                          R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})");
        REQUIRE(dup.status == 400);
        REQUIRE(dup.error_type == "ResourceInUseException");
    }
}

TEST_CASE("PutItem / GetItem / DeleteItem round-trip", "[api][handlers][items]") {
    Harness h;
    h.create_simple_table("Users");

    auto put = h.call(api::Operation::PutItem,
                      R"({"TableName":"Users","Item":{"pk":{"S":"alice"},"email":{"S":"a@b.com"},"age":{"N":"30"}}})");
    REQUIRE(put.status == 200);

    SECTION("get returns the stored item") {
        auto get = h.call(api::Operation::GetItem, R"({"TableName":"Users","Key":{"pk":{"S":"alice"}}})");
        REQUIRE(get.status == 200);
        REQUIRE_THAT(get.body, ContainsSubstring("\"Item\""));
        REQUIRE_THAT(get.body, ContainsSubstring("\"email\":{\"S\":\"a@b.com\"}"));
        REQUIRE_THAT(get.body, ContainsSubstring("\"age\":{\"N\":\"30\"}"));
    }

    SECTION("get of a missing key returns no Item") {
        auto get = h.call(api::Operation::GetItem, R"({"TableName":"Users","Key":{"pk":{"S":"bob"}}})");
        REQUIRE(get.status == 200);
        REQUIRE(get.body == "{}");
    }

    SECTION("delete removes the item") {
        auto del = h.call(api::Operation::DeleteItem, R"({"TableName":"Users","Key":{"pk":{"S":"alice"}}})");
        REQUIRE(del.status == 200);
        auto get = h.call(api::Operation::GetItem, R"({"TableName":"Users","Key":{"pk":{"S":"alice"}}})");
        REQUIRE(get.body == "{}");
    }
}

TEST_CASE("PutItem error handling", "[api][handlers][items][errors]") {
    Harness h;
    h.create_simple_table("Users");

    SECTION("put to a missing table is ResourceNotFoundException") {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"Ghost","Item":{"pk":{"S":"x"}}})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ResourceNotFoundException");
    }

    SECTION("put without the key attribute is ValidationException") {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"Users","Item":{"other":{"S":"x"}}})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("put with a key of the wrong type is ValidationException") {
        // pk is declared S, but an N value is supplied.
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"Users","Item":{"pk":{"N":"5"}}})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("malformed JSON body is a SerializationException") {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"Users","Item":{)");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "SerializationException");
    }
}

TEST_CASE("Scan returns items with count and pagination", "[api][handlers][scan]") {
    Harness h;
    h.create_simple_table("Users");

    for (int i = 0; i < 5; ++i) {
        std::string pk = "u" + std::to_string(i);
        auto r = h.call(api::Operation::PutItem,
                        R"({"TableName":"Users","Item":{"pk":{"S":")" + pk + R"("}}})");
        REQUIRE(r.status == 200);
    }

    SECTION("unbounded scan returns all items") {
        auto scan = h.call(api::Operation::Scan, R"({"TableName":"Users"})");
        REQUIRE(scan.status == 200);
        REQUIRE_THAT(scan.body, ContainsSubstring("\"Count\":5"));
        REQUIRE_THAT(scan.body, ContainsSubstring("\"ScannedCount\":5"));
    }

    SECTION("limited scan exposes a LastEvaluatedKey that resumes correctly") {
        auto page1 = h.call(api::Operation::Scan, R"({"TableName":"Users","Limit":2})");
        REQUIRE(page1.status == 200);
        REQUIRE_THAT(page1.body, ContainsSubstring("\"Count\":2"));
        REQUIRE_THAT(page1.body, ContainsSubstring("\"LastEvaluatedKey\""));
        // First page (sorted) holds u0 and u1; the cursor must be u1.
        REQUIRE_THAT(page1.body, ContainsSubstring("\"LastEvaluatedKey\":{\"pk\":{\"S\":\"u1\"}}"));

        auto page2 = h.call(api::Operation::Scan,
                            R"({"TableName":"Users","Limit":2,"ExclusiveStartKey":{"pk":{"S":"u1"}}})");
        REQUIRE(page2.status == 200);
        REQUIRE_THAT(page2.body, ContainsSubstring("\"Count\":2"));
        // Resumed page must start after u1, i.e. contain u2/u3 but not u0/u1.
        REQUIRE_THAT(page2.body, ContainsSubstring("\"u2\""));
        REQUIRE_THAT(page2.body, ContainsSubstring("\"u3\""));
    }
}

TEST_CASE("Query by partition key with a sort key", "[api][handlers][query]") {
    Harness h;
    auto created = h.call(api::Operation::CreateTable,
                          R"({"TableName":"Events",)"
                          R"("KeySchema":[{"AttributeName":"pk","KeyType":"HASH"},{"AttributeName":"sk","KeyType":"RANGE"}],)"
                          R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"},{"AttributeName":"sk","AttributeType":"N"}]})");
    REQUIRE(created.status == 200);

    // Two partitions; the sort keys are inserted out of numeric order.
    auto put = [&](const char* pk, const char* sk) {
        auto r = h.call(api::Operation::PutItem,
                        std::string(R"({"TableName":"Events","Item":{"pk":{"S":")") + pk +
                            R"("},"sk":{"N":")" + sk + R"("}}})");
        REQUIRE(r.status == 200);
    };
    put("p1", "10");
    put("p1", "2");
    put("p2", "1");

    SECTION("query returns only the matching partition, sorted by range key") {
        auto q = h.call(api::Operation::Query,
                        R"({"TableName":"Events","KeyConditions":{"pk":{"ComparisonOperator":"EQ","AttributeValueList":[{"S":"p1"}]}}})");
        REQUIRE(q.status == 200);
        REQUIRE_THAT(q.body, ContainsSubstring("\"Count\":2"));
        // sk=2 must appear before sk=10 (numeric ordering via the key codec).
        auto pos2 = q.body.find("\"N\":\"2\"");
        auto pos10 = q.body.find("\"N\":\"10\"");
        REQUIRE(pos2 != std::string::npos);
        REQUIRE(pos10 != std::string::npos);
        REQUIRE(pos2 < pos10);
        // The other partition's item must not appear.
        REQUIRE(q.body.find("\"N\":\"1\"") == std::string::npos);
    }

    SECTION("non-EQ comparison operators are rejected") {
        auto q = h.call(api::Operation::Query,
                        R"({"TableName":"Events","KeyConditions":{"pk":{"ComparisonOperator":"LE","AttributeValueList":[{"S":"p1"}]}}})");
        REQUIRE(q.status == 400);
        REQUIRE(q.error_type == "ValidationException");
    }
}

TEST_CASE("Unknown operation is rejected", "[api][handlers][errors]") {
    Harness h;
    auto r = h.call(api::Operation::Unknown, "{}");
    REQUIRE(r.status == 400);
    REQUIRE(r.error_type == "UnknownOperationException");
}

TEST_CASE("API handler robustness against malformed/abusive input", "[api][handlers][robustness]") {
    Harness h;
    h.create_simple_table("Users");

    SECTION("missing TableName is rejected, not crashed") {
        for (auto op : {api::Operation::PutItem, api::Operation::GetItem,
                        api::Operation::DeleteItem, api::Operation::Scan,
                        api::Operation::Query, api::Operation::DescribeTable}) {
            auto r = h.call(op, R"({"Item":{"pk":{"S":"x"}}})");
            REQUIRE(r.status == 400);
            REQUIRE(r.error_type == "ValidationException");
        }
    }

    SECTION("operations on a non-existent table are ResourceNotFoundException") {
        REQUIRE(h.call(api::Operation::GetItem, R"({"TableName":"Ghost","Key":{"pk":{"S":"x"}}})").error_type
                == "ResourceNotFoundException");
        REQUIRE(h.call(api::Operation::Scan, R"({"TableName":"Ghost"})").error_type
                == "ResourceNotFoundException");
        REQUIRE(h.call(api::Operation::DeleteItem, R"({"TableName":"Ghost","Key":{"pk":{"S":"x"}}})").error_type
                == "ResourceNotFoundException");
        REQUIRE(h.call(api::Operation::Query,
                       R"({"TableName":"Ghost","KeyConditions":{"pk":{"ComparisonOperator":"EQ","AttributeValueList":[{"S":"x"}]}}})").error_type
                == "ResourceNotFoundException");
    }

    SECTION("PutItem missing the Item member") {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"Users"})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("GetItem missing the Key member") {
        auto r = h.call(api::Operation::GetItem, R"({"TableName":"Users"})");
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("an oversized item (>400KB) is rejected") {
        std::string big(400001, 'A');
        std::string body = R"({"TableName":"Users","Item":{"pk":{"S":"k"},"blob":{"S":")" + big + R"("}}})";
        auto r = h.call(api::Operation::PutItem, body);
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("an attribute value with no recognized type is rejected") {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"Users","Item":{"pk":{"WAT":"x"}}})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("CreateTable without a key schema is rejected") {
        auto r = h.call(api::Operation::CreateTable, R"({"TableName":"NoKeys"})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("Query without KeyConditions is rejected") {
        auto r = h.call(api::Operation::Query, R"({"TableName":"Users"})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }

    SECTION("a variety of malformed JSON bodies are rejected, never crash") {
        for (const char* bad : {"", "not json", "{", "{\"TableName\":}", "[]", "12345", "{\"TableName\":\"Users\",\"Item\":"}) {
            auto r = h.call(api::Operation::PutItem, bad);
            REQUIRE(r.status == 400);
            REQUIRE((r.error_type == "ValidationException" || r.error_type == "SerializationException"));
        }
    }
}
