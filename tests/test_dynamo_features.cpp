// Tests for the DynamoDB features added per CYNAMODB_FINDINGS.md:
//   #2 ConditionExpression, #3 UpdateItem, #4 Query expressions,
//   #5 batch/transactions, #6 DeleteTable/UpdateTable, #7 501 for known ops,
//   #8 empty-key rejection, #9 GetItem miss returns {}.
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

std::string unique_metadata_path() {
    static std::atomic<uint64_t> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cynamodb_feat_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}

struct Harness {
    engine::TableManager tables{unique_metadata_path()};
    engine::MemoryEngine storage;

    api::ApiResult call(api::Operation op, const std::string& body) {
        return api::handle_operation(tables, storage, op, body);
    }
    void create_hash_table(const std::string& name) {
        REQUIRE(call(api::Operation::CreateTable,
                     R"({"TableName":")" + name +
                         R"(","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                         R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})")
                    .status == 200);
    }
    void create_range_table(const std::string& name, const char* sk_type = "N") {
        REQUIRE(call(api::Operation::CreateTable,
                     std::string(R"({"TableName":")") + name +
                         R"(","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"},{"AttributeName":"sk","KeyType":"RANGE"}],)"
                         R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"},{"AttributeName":"sk","AttributeType":")" +
                         sk_type + R"("}]})")
                    .status == 200);
    }
};

}  // namespace

TEST_CASE("ConditionExpression guards PutItem", "[features][condition]") {
    Harness h;
    h.create_hash_table("T");
    REQUIRE(h.call(api::Operation::PutItem,
                   R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"1"}}})").status == 200);

    SECTION("attribute_not_exists fails on an existing item") {
        auto r = h.call(api::Operation::PutItem,
                        R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"2"}},)"
                        R"J("ConditionExpression":"attribute_not_exists(pk)"})J");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ConditionalCheckFailedException");
        // The original value must be untouched.
        auto g = h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})");
        REQUIRE_THAT(g.body, ContainsSubstring("\"v\":{\"N\":\"1\"}"));
    }

    SECTION("attribute_not_exists succeeds for a new item") {
        auto r = h.call(api::Operation::PutItem,
                        R"({"TableName":"T","Item":{"pk":{"S":"b"},"v":{"N":"9"}},)"
                        R"J("ConditionExpression":"attribute_not_exists(pk)"})J");
        REQUIRE(r.status == 200);
    }

    SECTION("value comparison condition with ExpressionAttributeValues") {
        auto r = h.call(api::Operation::PutItem,
                        R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"5"}},)"
                        R"("ConditionExpression":"v = :exp","ExpressionAttributeValues":{":exp":{"N":"1"}}})");
        REQUIRE(r.status == 200);
        auto bad = h.call(api::Operation::PutItem,
                          R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"7"}},)"
                          R"("ConditionExpression":"v = :exp","ExpressionAttributeValues":{":exp":{"N":"1"}}})");
        REQUIRE(bad.error_type == "ConditionalCheckFailedException");
    }
}

TEST_CASE("ConditionExpression guards DeleteItem", "[features][condition]") {
    Harness h;
    h.create_hash_table("T");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"1"}}})");

    auto fail = h.call(api::Operation::DeleteItem,
                       R"({"TableName":"T","Key":{"pk":{"S":"a"}},)"
                       R"("ConditionExpression":"v = :v","ExpressionAttributeValues":{":v":{"N":"2"}}})");
    REQUIRE(fail.error_type == "ConditionalCheckFailedException");

    auto good = h.call(api::Operation::DeleteItem,
                       R"({"TableName":"T","Key":{"pk":{"S":"a"}},)"
                       R"("ConditionExpression":"v = :v","ExpressionAttributeValues":{":v":{"N":"1"}},"ReturnValues":"ALL_OLD"})");
    REQUIRE(good.status == 200);
    REQUIRE_THAT(good.body, ContainsSubstring("\"Attributes\""));
    REQUIRE(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})").body == "{}");
}

TEST_CASE("UpdateItem SET / ADD / REMOVE with ReturnValues", "[features][update]") {
    Harness h;
    h.create_hash_table("T");

    SECTION("SET creates and updates attributes") {
        auto r = h.call(api::Operation::UpdateItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"a"}},)"
                        R"("UpdateExpression":"SET title = :t","ExpressionAttributeValues":{":t":{"S":"hello"}},)"
                        R"("ReturnValues":"ALL_NEW"})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(r.body, ContainsSubstring("\"title\":{\"S\":\"hello\"}"));
        REQUIRE_THAT(r.body, ContainsSubstring("\"pk\":{\"S\":\"a\"}"));
    }

    SECTION("ADD increments a counter atomically from absent") {
        REQUIRE(h.call(api::Operation::UpdateItem,
                       R"({"TableName":"T","Key":{"pk":{"S":"c"}},)"
                       R"("UpdateExpression":"ADD n :one","ExpressionAttributeValues":{":one":{"N":"1"}}})").status == 200);
        auto r = h.call(api::Operation::UpdateItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"c"}},)"
                        R"("UpdateExpression":"ADD n :one","ExpressionAttributeValues":{":one":{"N":"1"}},"ReturnValues":"ALL_NEW"})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"n\":{\"N\":\"2\"}"));
    }

    SECTION("SET arithmetic and REMOVE") {
        h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"x"},"n":{"N":"10"},"junk":{"S":"y"}}})");
        auto r = h.call(api::Operation::UpdateItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"x"}},)"
                        R"("UpdateExpression":"SET n = n + :d REMOVE junk","ExpressionAttributeValues":{":d":{"N":"5"}},)"
                        R"("ReturnValues":"ALL_NEW"})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"n\":{\"N\":\"15\"}"));
        REQUIRE(r.body.find("junk") == std::string::npos);
    }

    SECTION("UpdateItem honors ConditionExpression") {
        h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"g"},"v":{"N":"1"}}})");
        auto r = h.call(api::Operation::UpdateItem,
                        R"({"TableName":"T","Key":{"pk":{"S":"g"}},)"
                        R"("UpdateExpression":"SET v = :v","ExpressionAttributeValues":{":v":{"N":"2"},":exp":{"N":"99"}},)"
                        R"("ConditionExpression":"v = :exp"})");
        REQUIRE(r.error_type == "ConditionalCheckFailedException");
    }
}

TEST_CASE("Query with KeyConditionExpression and sort-key operators", "[features][query]") {
    Harness h;
    h.create_range_table("E", "N");
    auto put = [&](const char* pk, const char* sk) {
        REQUIRE(h.call(api::Operation::PutItem,
                       std::string(R"({"TableName":"E","Item":{"pk":{"S":")") + pk +
                           R"("},"sk":{"N":")" + sk + R"("}}})").status == 200);
    };
    put("p", "1"); put("p", "5"); put("p", "10"); put("q", "1");

    SECTION("partition equality") {
        auto r = h.call(api::Operation::Query,
                        R"({"TableName":"E","KeyConditionExpression":"pk = :p",)"
                        R"("ExpressionAttributeValues":{":p":{"S":"p"}}})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":3"));
        REQUIRE(r.body.find("\"q\"") == std::string::npos);
    }

    SECTION("sort key range >") {
        auto r = h.call(api::Operation::Query,
                        R"({"TableName":"E","KeyConditionExpression":"pk = :p AND sk > :s",)"
                        R"("ExpressionAttributeValues":{":p":{"S":"p"},":s":{"N":"1"}}})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":2"));
    }

    SECTION("sort key BETWEEN") {
        auto r = h.call(api::Operation::Query,
                        R"({"TableName":"E","KeyConditionExpression":"pk = :p AND sk BETWEEN :lo AND :hi",)"
                        R"("ExpressionAttributeValues":{":p":{"S":"p"},":lo":{"N":"2"},":hi":{"N":"10"}}})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":2"));
    }

    SECTION("ScanIndexForward=false reverses order") {
        auto r = h.call(api::Operation::Query,
                        R"({"TableName":"E","KeyConditionExpression":"pk = :p",)"
                        R"("ExpressionAttributeValues":{":p":{"S":"p"}},"ScanIndexForward":false})");
        auto p10 = r.body.find("\"N\":\"10\"");
        auto p1 = r.body.find("\"N\":\"1\"");
        REQUIRE(p10 < p1);
    }

    SECTION("begins_with on a string sort key") {
        Harness h2;
        h2.create_range_table("S", "S");
        h2.call(api::Operation::PutItem, R"({"TableName":"S","Item":{"pk":{"S":"p"},"sk":{"S":"order#1"}}})");
        h2.call(api::Operation::PutItem, R"({"TableName":"S","Item":{"pk":{"S":"p"},"sk":{"S":"order#2"}}})");
        h2.call(api::Operation::PutItem, R"({"TableName":"S","Item":{"pk":{"S":"p"},"sk":{"S":"user#1"}}})");
        auto r = h2.call(api::Operation::Query,
                         R"J({"TableName":"S","KeyConditionExpression":"pk = :p AND begins_with(sk, :pre)","ExpressionAttributeValues":{":p":{"S":"p"},":pre":{"S":"order#"}}})J");
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":2"));
    }
}

TEST_CASE("FilterExpression and ProjectionExpression", "[features][query][filter]") {
    Harness h;
    h.create_range_table("E", "N");
    h.call(api::Operation::PutItem, R"({"TableName":"E","Item":{"pk":{"S":"p"},"sk":{"N":"1"},"status":{"S":"open"},"secret":{"S":"x"}}})");
    h.call(api::Operation::PutItem, R"({"TableName":"E","Item":{"pk":{"S":"p"},"sk":{"N":"2"},"status":{"S":"closed"}}})");

    SECTION("filter on a non-key attribute") {
        auto r = h.call(api::Operation::Query,
                        R"({"TableName":"E","KeyConditionExpression":"pk = :p",)"
                        R"("FilterExpression":"status = :s",)"
                        R"("ExpressionAttributeValues":{":p":{"S":"p"},":s":{"S":"open"}}})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":1"));
        REQUIRE_THAT(r.body, ContainsSubstring("\"ScannedCount\":2"));
    }

    SECTION("projection limits returned attributes") {
        auto r = h.call(api::Operation::Query,
                        R"({"TableName":"E","KeyConditionExpression":"pk = :p AND sk = :s",)"
                        R"("ProjectionExpression":"status",)"
                        R"("ExpressionAttributeValues":{":p":{"S":"p"},":s":{"N":"1"}}})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"status\":{\"S\":\"open\"}"));
        REQUIRE(r.body.find("secret") == std::string::npos);
    }

    SECTION("scan with FilterExpression") {
        auto r = h.call(api::Operation::Scan,
                        R"({"TableName":"E","FilterExpression":"status = :s",)"
                        R"("ExpressionAttributeValues":{":s":{"S":"closed"}}})");
        REQUIRE_THAT(r.body, ContainsSubstring("\"Count\":1"));
    }
}

TEST_CASE("BatchWriteItem and BatchGetItem", "[features][batch]") {
    Harness h;
    h.create_hash_table("T");
    auto bw = h.call(api::Operation::BatchWriteItem,
                     R"({"RequestItems":{"T":[{"PutRequest":{"Item":{"pk":{"S":"a"}}}},)"
                     R"({"PutRequest":{"Item":{"pk":{"S":"b"}}}}]}})");
    REQUIRE(bw.status == 200);
    REQUIRE_THAT(bw.body, ContainsSubstring("UnprocessedItems"));

    auto bg = h.call(api::Operation::BatchGetItem,
                     R"({"RequestItems":{"T":{"Keys":[{"pk":{"S":"a"}},{"pk":{"S":"b"}},{"pk":{"S":"missing"}}]}}})");
    REQUIRE(bg.status == 200);
    REQUIRE_THAT(bg.body, ContainsSubstring("\"a\""));
    REQUIRE_THAT(bg.body, ContainsSubstring("\"b\""));

    auto del = h.call(api::Operation::BatchWriteItem,
                      R"({"RequestItems":{"T":[{"DeleteRequest":{"Key":{"pk":{"S":"a"}}}}]}})");
    REQUIRE(del.status == 200);
    REQUIRE(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})").body == "{}");
}

TEST_CASE("TransactWriteItems is all-or-nothing", "[features][transactions]") {
    Harness h;
    h.create_hash_table("T");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"1"}}})");

    SECTION("a failed condition cancels the whole transaction") {
        auto r = h.call(api::Operation::TransactWriteItems,
                        R"({"TransactItems":[)"
                        R"({"Put":{"TableName":"T","Item":{"pk":{"S":"b"}}}},)"
                        R"({"ConditionCheck":{"TableName":"T","Key":{"pk":{"S":"a"}},"ConditionExpression":"v = :x","ExpressionAttributeValues":{":x":{"N":"99"}}}})"
                        R"(]})");
        REQUIRE(r.error_type == "TransactionCanceledException");
        // The Put must have been rolled back (never applied).
        REQUIRE(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"b"}}})").body == "{}");
    }

    SECTION("all actions apply when conditions pass") {
        auto r = h.call(api::Operation::TransactWriteItems,
                        R"({"TransactItems":[)"
                        R"({"Put":{"TableName":"T","Item":{"pk":{"S":"b"},"v":{"N":"2"}}}},)"
                        R"({"Update":{"TableName":"T","Key":{"pk":{"S":"a"}},"UpdateExpression":"SET v = :v","ExpressionAttributeValues":{":v":{"N":"5"}}}})"
                        R"(]})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"b"}}})").body,
                     ContainsSubstring("\"v\":{\"N\":\"2\"}"));
        REQUIRE_THAT(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})").body,
                     ContainsSubstring("\"v\":{\"N\":\"5\"}"));
    }

    SECTION("a malformed Update aborts the whole transaction before any write") {
        auto r = h.call(api::Operation::TransactWriteItems,
                        R"({"TransactItems":[)"
                        R"({"Put":{"TableName":"T","Item":{"pk":{"S":"new"}}}},)"
                        R"({"Update":{"TableName":"T","Key":{"pk":{"S":"a"}},"UpdateExpression":"SET v = :missing"}})"
                        R"(]})");
        REQUIRE(r.status == 400);
        // The earlier Put must NOT have been committed (all-or-nothing).
        REQUIRE(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"new"}}})").body == "{}");
    }

    SECTION("TransactGetItems reads multiple items") {
        auto r = h.call(api::Operation::TransactGetItems,
                        R"({"TransactItems":[{"Get":{"TableName":"T","Key":{"pk":{"S":"a"}}}},)"
                        R"({"Get":{"TableName":"T","Key":{"pk":{"S":"zzz"}}}}]})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(r.body, ContainsSubstring("\"Responses\""));
    }
}

TEST_CASE("DeleteTable and UpdateTable", "[features][tables]") {
    Harness h;
    h.create_hash_table("T");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"}}})");

    SECTION("UpdateTable keeps the table ACTIVE") {
        auto r = h.call(api::Operation::UpdateTable, R"({"TableName":"T","BillingMode":"PROVISIONED"})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(r.body, ContainsSubstring("\"TableStatus\":\"ACTIVE\""));
    }

    SECTION("DeleteTable removes the table and its data") {
        auto r = h.call(api::Operation::DeleteTable, R"({"TableName":"T"})");
        REQUIRE(r.status == 200);
        REQUIRE_THAT(r.body, ContainsSubstring("\"TableName\":\"T\""));
        // Table is gone.
        REQUIRE(h.call(api::Operation::DescribeTable, R"({"TableName":"T"})").error_type == "ResourceNotFoundException");
        // Its data is purged.
        REQUIRE(h.storage.get("T", "a") == std::nullopt);
    }

    SECTION("DeleteTable on a missing table is ResourceNotFoundException") {
        REQUIRE(h.call(api::Operation::DeleteTable, R"({"TableName":"Ghost"})").error_type ==
                "ResourceNotFoundException");
    }
}

TEST_CASE("known-but-unimplemented ops return 501 NotImplementedException", "[features][501]") {
    Harness h;
    for (auto op : {api::Operation::DescribeContributorInsights, api::Operation::ImportTable,
                    api::Operation::DescribeEndpoints, api::Operation::ListExports}) {
        auto r = h.call(op, "{}");
        REQUIRE(r.status == 501);
        REQUIRE(r.error_type == "NotImplementedException");
    }
}

TEST_CASE("genuinely unknown targets remain UnknownOperationException", "[features][501]") {
    Harness h;
    auto r = h.call(api::Operation::Unknown, "{}");
    REQUIRE(r.status == 400);
    REQUIRE(r.error_type == "UnknownOperationException");
}

TEST_CASE("empty-string key attributes are rejected", "[features][validation]") {
    Harness h;
    h.create_hash_table("T");
    SECTION("PutItem with empty key value") {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":""}}})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }
    SECTION("GetItem with empty key value") {
        auto r = h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":""}}})");
        REQUIRE(r.status == 400);
        REQUIRE(r.error_type == "ValidationException");
    }
}

TEST_CASE("contract: every documented-Implemented op is reachable", "[features][contract]") {
    // Guards docs/api.md from drifting: each operation listed as Implemented must
    // never answer with 501 NotImplementedException or UnknownOperationException
    // (a minimal/empty body may yield a 400 ValidationException, which is fine).
    Harness h;
    h.create_hash_table("T");
    const std::vector<api::Operation> implemented = {
        api::Operation::CreateTable, api::Operation::DescribeTable, api::Operation::ListTables,
        api::Operation::DeleteTable, api::Operation::UpdateTable,
        api::Operation::PutItem, api::Operation::GetItem, api::Operation::UpdateItem,
        api::Operation::DeleteItem, api::Operation::Query, api::Operation::Scan,
        api::Operation::BatchWriteItem, api::Operation::BatchGetItem,
        api::Operation::TransactWriteItems, api::Operation::TransactGetItems,
    };
    for (auto op : implemented) {
        auto r = h.call(op, R"({"TableName":"T"})");
        REQUIRE(r.status != 501);
        REQUIRE(r.error_type != "NotImplementedException");
        REQUIRE(r.error_type != "UnknownOperationException");
    }
}

TEST_CASE("GetItem miss always returns the canonical {}", "[features][getitem]") {
    Harness h;
    h.create_hash_table("T");
    auto r = h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"nope"}}})");
    REQUIRE(r.status == 200);
    REQUIRE(r.body == "{}");
}
