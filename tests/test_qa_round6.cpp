// Regression tests for the round-6 improvement-plan items (AWS-compatibility parity):
//   CS-18 Select=COUNT, CS-19 ConsistentRead on a GSI, CS-20 duplicate txn item,
//   CS-21 transaction idempotency (ClientRequestToken), CS-22 UPDATED_NEW/OLD.
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
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_r6_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}
struct Harness {
    engine::TableManager tables{meta_path()};
    engine::MemoryEngine storage;
    api::ApiResult call(api::Operation op, const std::string& b) { return api::handle_operation(tables, storage, op, b); }
};
const char* kCreateRange =
    R"({"TableName":"E","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"},{"AttributeName":"sk","KeyType":"RANGE"}],)"
    R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"},{"AttributeName":"sk","AttributeType":"N"}]})";
const char* kCreateGsi =
    R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
    R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"},{"AttributeName":"gpk","AttributeType":"S"}],)"
    R"("GlobalSecondaryIndexes":[{"IndexName":"G","KeySchema":[{"AttributeName":"gpk","KeyType":"HASH"}],)"
    R"("Projection":{"ProjectionType":"ALL"}}]})";
}  // namespace

TEST_CASE("CS-18: Select=COUNT returns the count without Items", "[r6][select]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable, kCreateRange).status == 200);
    for (int i = 0; i < 5; ++i)
        h.call(api::Operation::PutItem,
               R"({"TableName":"E","Item":{"pk":{"S":"p"},"sk":{"N":")" + std::to_string(i) + R"("}}})");

    SECTION("Query Select=COUNT") {
        auto r = h.call(api::Operation::Query,
                        R"({"TableName":"E","Select":"COUNT","KeyConditionExpression":"pk = :p",)"
                        R"("ExpressionAttributeValues":{":p":{"S":"p"}}})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":5"));
        REQUIRE(r.body.find("\"Items\"") == std::string::npos);
    }
    SECTION("Scan Select=COUNT") {
        auto r = h.call(api::Operation::Scan, R"({"TableName":"E","Select":"COUNT"})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":5"));
        REQUIRE(r.body.find("\"Items\"") == std::string::npos);
    }
    SECTION("Select=COUNT with ProjectionExpression is rejected") {
        auto r = h.call(api::Operation::Scan,
                        R"({"TableName":"E","Select":"COUNT","ProjectionExpression":"pk"})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }
}

TEST_CASE("CS-19: ConsistentRead on a GSI is rejected", "[r6][consistent]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable, kCreateGsi).status == 200);
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"gpk":{"S":"G"}}})");

    auto bad = h.call(api::Operation::Query,
                      R"({"TableName":"T","IndexName":"G","ConsistentRead":true,)"
                      R"("KeyConditionExpression":"gpk = :g","ExpressionAttributeValues":{":g":{"S":"G"}}})");
    REQUIRE(bad.status == 400);
    REQUIRE(bad.error_type == "ValidationException");

    // ConsistentRead on the base table is fine.
    auto ok = h.call(api::Operation::Query,
                     R"({"TableName":"T","ConsistentRead":true,"KeyConditionExpression":"pk = :p",)"
                     R"("ExpressionAttributeValues":{":p":{"S":"a"}}})");
    REQUIRE(ok.status == 200);
}

TEST_CASE("CS-20: a transaction cannot act on the same item twice", "[r6][txn]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable,
                   R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                   R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    auto r = h.call(api::Operation::TransactWriteItems,
                    R"({"TransactItems":[)"
                    R"({"Put":{"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"1"}}}},)"
                    R"({"Update":{"TableName":"T","Key":{"pk":{"S":"a"}},"UpdateExpression":"SET v = :v","ExpressionAttributeValues":{":v":{"N":"2"}}}})"
                    R"(]})");
    REQUIRE(r.status == 400);
    REQUIRE(r.error_type == "ValidationException");
}

TEST_CASE("CS-21: a transaction with a repeated ClientRequestToken applies once", "[r6][idempotent]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable,
                   R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                   R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);

    const std::string txn =
        R"({"ClientRequestToken":"tok-1","TransactItems":[)"
        R"({"Update":{"TableName":"T","Key":{"pk":{"S":"c"}},"UpdateExpression":"ADD n :one","ExpressionAttributeValues":{":one":{"N":"1"}}}})"
        R"(]})";
    REQUIRE(h.call(api::Operation::TransactWriteItems, txn).status == 200);
    REQUIRE(h.call(api::Operation::TransactWriteItems, txn).status == 200);  // retry, same token

    auto g = h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"c"}}})");
    REQUIRE_THAT(g.body, ContainsSubstring("\"n\":{\"N\":\"1\"}"));  // applied exactly once

    SECTION("same token with a different body is an error") {
        const std::string other =
            R"({"ClientRequestToken":"tok-1","TransactItems":[)"
            R"({"Update":{"TableName":"T","Key":{"pk":{"S":"c"}},"UpdateExpression":"ADD n :two","ExpressionAttributeValues":{":two":{"N":"2"}}}})"
            R"(]})";
        auto r = h.call(api::Operation::TransactWriteItems, other);
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "IdempotentParameterMismatchException");
    }
}

TEST_CASE("CS-22: UPDATED_NEW/UPDATED_OLD return only the changed attributes", "[r6][returnvalues]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable,
                   R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                   R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"a":{"N":"1"},"b":{"N":"9"}}})");

    SECTION("UPDATED_NEW returns only the modified attribute") {
        auto r = h.call(api::Operation::UpdateItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"a"}},"UpdateExpression":"SET a = :n",)"
                        R"("ExpressionAttributeValues":{":n":{"N":"5"}},"ReturnValues":"UPDATED_NEW"})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"a\":{\"N\":\"5\"}"));
        REQUIRE(r.body.find("\"b\"") == std::string::npos);   // untouched attr not returned
        REQUIRE(r.body.find("\"pk\"") == std::string::npos);  // key not returned
    }
    SECTION("UPDATED_OLD returns the prior value of the changed attribute") {
        auto r = h.call(api::Operation::UpdateItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"a"}},"UpdateExpression":"SET a = :n",)"
                        R"("ExpressionAttributeValues":{":n":{"N":"7"}},"ReturnValues":"UPDATED_OLD"})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"a\":{\"N\":\"1\"}"));
        REQUIRE(r.body.find("\"b\"") == std::string::npos);
    }
}
