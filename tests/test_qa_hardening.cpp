// Regression tests for the hardcore-QA / improvement-plan items:
//   CS-3  numeric (N/NS) validation + set constraints
//   CS-10 single shared attribute-size implementation
//   CS-11 corrupt-record decode is observable (returns nullopt, never partial)
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/core/sizing.hpp>
#include <cynamodb/engine/item_validator.hpp>
#include <cynamodb/engine/lsm/record_codec.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <cynamodb/json/serializer.hpp>

#include <atomic>
#include <filesystem>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_qa_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}
struct Harness {
    engine::TableManager tables{meta_path()};
    engine::MemoryEngine storage;
    api::ApiResult call(api::Operation op, const std::string& b) { return api::handle_operation(tables, storage, op, b); }
    void create() {
        REQUIRE(call(api::Operation::CreateTable,
                     R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                     R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    }
    api::ApiResult put(const std::string& item) {
        return call(api::Operation::PutItem, R"({"TableName":"T","Item":)" + item + "}");
    }
};
}  // namespace

TEST_CASE("CS-3: non-numeric N values are rejected", "[qa][validation]") {
    Harness h;
    h.create();
    auto bad = h.put(R"({"pk":{"S":"a"},"n":{"N":"not-a-number"}})");
    REQUIRE(bad.status == 400);
    REQUIRE(bad.error_type == "ValidationException");

    for (const char* good : {"0", "-5", "3.14", "1e10", "1.5E-3", "9007199254740993",
                             "12345678901234567890123456789012345678"}) {  // 38 digits
        auto r = h.put(std::string(R"({"pk":{"S":"a"},"n":{"N":")") + good + R"("}})");
        REQUIRE(r.status == 200);
    }
    // 39 significant digits exceeds the DynamoDB limit.
    auto over = h.put(R"({"pk":{"S":"a"},"n":{"N":"123456789012345678901234567890123456789"}})");
    REQUIRE(over.status == 400);
}

TEST_CASE("CS-3: nested non-numeric N (in M and L) is rejected", "[qa][validation]") {
    Harness h;
    h.create();
    REQUIRE(h.put(R"({"pk":{"S":"a"},"m":{"M":{"x":{"N":"bad"}}}})").status == 400);
    REQUIRE(h.put(R"({"pk":{"S":"a"},"l":{"L":[{"N":"bad"}]}})").status == 400);
}

TEST_CASE("CS-3: set constraints (empty, duplicate, non-numeric NS)", "[qa][validation]") {
    Harness h;
    h.create();
    REQUIRE(h.put(R"({"pk":{"S":"a"},"ss":{"SS":[]}})").status == 400);                 // empty set
    REQUIRE(h.put(R"({"pk":{"S":"a"},"ss":{"SS":["x","x"]}})").status == 400);          // duplicate
    REQUIRE(h.put(R"({"pk":{"S":"a"},"ns":{"NS":["1","nope"]}})").status == 400);       // non-numeric
    REQUIRE(h.put(R"({"pk":{"S":"a"},"ns":{"NS":["1","1"]}})").status == 400);          // duplicate
    REQUIRE(h.put(R"({"pk":{"S":"a"},"ss":{"SS":["x","y"]},"ns":{"NS":["1","2"]}})").status == 200);  // valid
}

TEST_CASE("CS-10: validator and serializer agree on attribute size (non-scalar > 0)", "[qa][sizing]") {
    auto list = std::make_shared<core::AttributeValue>();
    list->type = core::AttributeType::L;
    core::ListValue lv;
    {
        auto s = std::make_shared<core::AttributeValue>(); s->type = core::AttributeType::S; s->value = core::String("hello");
        lv.push_back(s);
    }
    list->value = std::move(lv);

    std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess> item;
    item["data"] = list;

    size_t via_serializer = json::JsonSerializer::calculate_item_size(item);
    size_t via_validator = engine::ItemValidator::calculate_item_size(item);
    REQUIRE(via_serializer == via_validator);
    REQUIRE(via_serializer > 0);  // previously the serializer returned 0 for lists
    REQUIRE(core::attribute_size(*list) > 0);
}

TEST_CASE("CS-11: a truncated record decodes to nullopt, never a partial item", "[qa][codec]") {
    // Encode a valid two-attribute record, then truncate it mid-value.
    engine::lsm::RecordAttributes attrs;
    {
        auto a = std::make_shared<core::AttributeValue>(); a->type = core::AttributeType::S; a->value = core::String("alpha");
        auto b = std::make_shared<core::AttributeValue>(); b->type = core::AttributeType::S; b->value = core::String("bravo");
        attrs["a"] = a;
        attrs["b"] = b;
    }
    std::string encoded = engine::lsm::encode_attributes(attrs);
    REQUIRE(engine::lsm::decode_attributes(encoded).has_value());

    std::string truncated = encoded.substr(0, encoded.size() - 3);
    auto decoded = engine::lsm::decode_attributes(truncated);
    REQUIRE_FALSE(decoded.has_value());  // hard failure, not a silently shortened item
}
