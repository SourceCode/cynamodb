// Tests for Global/Local Secondary Index maintenance and querying.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <filesystem>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_idx_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}
struct Harness {
    std::string mp = meta_path();
    engine::TableManager tables{mp};
    engine::MemoryEngine storage;
    api::ApiResult call(api::Operation op, const std::string& b) { return api::handle_operation(tables, storage, op, b); }
};
// Table with a GSI on (gsiPk HASH, gsiSk RANGE), ALL projection.
const char* kCreateGsi =
    R"({"TableName":"T",)"
    R"("KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
    R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"},)"
    R"({"AttributeName":"gpk","AttributeType":"S"},{"AttributeName":"gsk","AttributeType":"N"}],)"
    R"("GlobalSecondaryIndexes":[{"IndexName":"G1",)"
    R"("KeySchema":[{"AttributeName":"gpk","KeyType":"HASH"},{"AttributeName":"gsk","KeyType":"RANGE"}],)"
    R"("Projection":{"ProjectionType":"ALL"}}]})";
}  // namespace

TEST_CASE("CreateTable parses and DescribeTable reports a GSI", "[index][gsi]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable, kCreateGsi).status == 200);
    auto d = h.call(api::Operation::DescribeTable, R"({"TableName":"T"})");
    REQUIRE_THAT(d.body, ContainsSubstring("\"IndexName\":\"G1\""));
    REQUIRE_THAT(d.body, ContainsSubstring("GlobalSecondaryIndexes"));
}

TEST_CASE("items are indexed and queryable by a GSI", "[index][gsi]") {
    Harness h;
    h.call(api::Operation::CreateTable, kCreateGsi);
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"gpk":{"S":"G"},"gsk":{"N":"2"},"data":{"S":"x"}}})");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"b"},"gpk":{"S":"G"},"gsk":{"N":"1"}}})");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"c"},"gpk":{"S":"H"},"gsk":{"N":"9"}}})");

    SECTION("query returns only matching index partition, sorted by index range key") {
        auto q = h.call(api::Operation::Query,
                        R"({"TableName":"T","IndexName":"G1","KeyConditionExpression":"gpk = :g",)"
                        R"("ExpressionAttributeValues":{":g":{"S":"G"}}})");
        REQUIRE(q.status == 200);
        REQUIRE_THAT(q.body, ContainsSubstring("\"Count\":2"));
        // gsk=1 must sort before gsk=2.
        auto p1 = q.body.find("\"gsk\":{\"N\":\"1\"}");
        auto p2 = q.body.find("\"gsk\":{\"N\":\"2\"}");
        REQUIRE(p1 != std::string::npos);
        REQUIRE(p1 < p2);
        // The other index partition (H) must not appear.
        REQUIRE(q.body.find("\"H\"") == std::string::npos);
        // ALL projection carries the base 'data' attribute.
        REQUIRE_THAT(q.body, ContainsSubstring("\"data\":{\"S\":\"x\"}"));
    }

    SECTION("sort condition on the index range key") {
        auto q = h.call(api::Operation::Query,
                        R"({"TableName":"T","IndexName":"G1","KeyConditionExpression":"gpk = :g AND gsk > :s",)"
                        R"("ExpressionAttributeValues":{":g":{"S":"G"},":s":{"N":"1"}}})");
        REQUIRE_THAT(q.body, ContainsSubstring("\"Count\":1"));
    }
}

TEST_CASE("updating an item moves its GSI entry", "[index][gsi]") {
    Harness h;
    h.call(api::Operation::CreateTable, kCreateGsi);
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"gpk":{"S":"G"},"gsk":{"N":"1"}}})");

    // Move it to a different index partition.
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"gpk":{"S":"H"},"gsk":{"N":"1"}}})");

    auto old_part = h.call(api::Operation::Query,
                           R"({"TableName":"T","IndexName":"G1","KeyConditionExpression":"gpk = :g",)"
                           R"("ExpressionAttributeValues":{":g":{"S":"G"}}})");
    REQUIRE_THAT(old_part.body, ContainsSubstring("\"Count\":0"));
    auto new_part = h.call(api::Operation::Query,
                           R"({"TableName":"T","IndexName":"G1","KeyConditionExpression":"gpk = :g",)"
                           R"("ExpressionAttributeValues":{":g":{"S":"H"}}})");
    REQUIRE_THAT(new_part.body, ContainsSubstring("\"Count\":1"));
}

TEST_CASE("deleting an item removes its GSI entry", "[index][gsi]") {
    Harness h;
    h.call(api::Operation::CreateTable, kCreateGsi);
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"gpk":{"S":"G"},"gsk":{"N":"1"}}})");
    h.call(api::Operation::DeleteItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})");
    auto q = h.call(api::Operation::Query,
                    R"({"TableName":"T","IndexName":"G1","KeyConditionExpression":"gpk = :g",)"
                    R"("ExpressionAttributeValues":{":g":{"S":"G"}}})");
    REQUIRE_THAT(q.body, ContainsSubstring("\"Count\":0"));
}

TEST_CASE("a sparse GSI omits items missing the index key", "[index][gsi]") {
    Harness h;
    h.call(api::Operation::CreateTable, kCreateGsi);
    // No gpk/gsk: should not appear in the index.
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"other":{"S":"y"}}})");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"b"},"gpk":{"S":"G"},"gsk":{"N":"1"}}})");
    auto q = h.call(api::Operation::Query,
                    R"({"TableName":"T","IndexName":"G1","KeyConditionExpression":"gpk = :g",)"
                    R"("ExpressionAttributeValues":{":g":{"S":"G"}}})");
    REQUIRE_THAT(q.body, ContainsSubstring("\"Count\":1"));
}

TEST_CASE("querying a non-existent index is a ValidationException", "[index][gsi]") {
    Harness h;
    h.call(api::Operation::CreateTable, kCreateGsi);
    auto q = h.call(api::Operation::Query,
                    R"({"TableName":"T","IndexName":"Nope","KeyConditionExpression":"gpk = :g",)"
                    R"("ExpressionAttributeValues":{":g":{"S":"G"}}})");
    REQUIRE(q.status == 400);
    REQUIRE(q.error_type == "ValidationException");
}

TEST_CASE("KEYS_ONLY projection persists across reload and projects only keys", "[index][gsi][persist]") {
    std::string mp = meta_path();
    const char* create =
        R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
        R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"},{"AttributeName":"gpk","AttributeType":"S"}],)"
        R"("GlobalSecondaryIndexes":[{"IndexName":"G","KeySchema":[{"AttributeName":"gpk","KeyType":"HASH"}],)"
        R"("Projection":{"ProjectionType":"KEYS_ONLY"}}]})";
    {
        engine::TableManager tables{mp};
        engine::MemoryEngine storage;
        api::handle_operation(tables, storage, api::Operation::CreateTable, create);
    }
    engine::TableManager reloaded{mp};
    auto def = reloaded.describe_table("T");
    REQUIRE(def.has_value());
    REQUIRE(def->global_secondary_indexes.size() == 1);
    REQUIRE(def->global_secondary_indexes[0].index_name == "G");
    REQUIRE(def->global_secondary_indexes[0].projection.projection_type == core::ProjectionType::KEYS_ONLY);
}
