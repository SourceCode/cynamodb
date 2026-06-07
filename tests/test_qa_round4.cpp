// Regression tests for the round-4 improvement-plan items:
//   CS-14 number normalization, CS-15 parallel-scan segments, CS-17 batch/txn limits.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <filesystem>
#include <regex>
#include <set>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_r4_" + std::to_string(c.fetch_add(1)));
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
};
// Collects the pk values present in a Scan response body.
std::set<std::string> pk_values(const std::string& body) {
    std::set<std::string> out;
    std::regex re("\"pk\":\\{\"S\":\"([^\"]*)\"\\}");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), re); it != std::sregex_iterator(); ++it) {
        out.insert((*it)[1].str());
    }
    return out;
}
}  // namespace

TEST_CASE("CS-14: number values are normalized to canonical form", "[r4][normalize]") {
    Harness h;
    h.create();
    struct Case { const char* in; const char* out; };
    const Case cases[] = {
        {"1.0", "1"}, {"+5", "5"}, {"-0", "0"}, {"1.50", "1.5"}, {"007", "7"},
        {"0", "0"}, {"-3.14", "-3.14"}, {"100", "100"}, {"0.5", "0.5"},
        {"1e2", "100"}, {"1.5e-3", "0.0015"}, {"9007199254740993", "9007199254740993"},
    };
    int i = 0;
    for (const auto& c : cases) {
        std::string key = "k" + std::to_string(i++);
        REQUIRE(h.call(api::Operation::PutItem,
                       std::string(R"({"TableName":"T","Item":{"pk":{"S":")") + key +
                           R"("},"n":{"N":")" + c.in + R"("}}})").status == 200);
        auto g = h.call(api::Operation::GetItem,
                        std::string(R"({"TableName":"T","Key":{"pk":{"S":")") + key + R"("}}})");
        REQUIRE_THAT(g.body, ContainsSubstring(std::string("\"n\":{\"N\":\"") + c.out + "\"}"));
    }
}

TEST_CASE("CS-14: NS members are normalized", "[r4][normalize]") {
    Harness h;
    h.create();
    REQUIRE(h.call(api::Operation::PutItem,
                   R"({"TableName":"T","Item":{"pk":{"S":"a"},"ns":{"NS":["1.0","2.50"]}}})").status == 200);
    auto g = h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})");
    REQUIRE_THAT(g.body, ContainsSubstring("\"1\""));
    REQUIRE_THAT(g.body, ContainsSubstring("\"2.5\""));
}

TEST_CASE("CS-15: parallel-scan segments tile the table exactly (no overlap, full union)", "[r4][scan]") {
    Harness h;
    h.create();
    std::set<std::string> all;
    for (int i = 0; i < 40; ++i) {
        std::string k = "item" + std::to_string(i);
        h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":")" + k + R"("}}})");
        all.insert(k);
    }

    for (int total : {2, 3, 5}) {
        std::set<std::string> seen;
        size_t sum = 0;
        for (int seg = 0; seg < total; ++seg) {
            auto r = h.call(api::Operation::Scan,
                            R"({"TableName":"T","Segment":)" + std::to_string(seg) +
                                R"(,"TotalSegments":)" + std::to_string(total) + "}");
            REQUIRE(r.status == 200);
            auto pks = pk_values(r.body);
            sum += pks.size();
            for (const auto& p : pks) {
                REQUIRE(seen.insert(p).second);  // no key appears in two segments
            }
        }
        REQUIRE(sum == all.size());   // union covers everything
        REQUIRE(seen == all);
    }
}

TEST_CASE("CS-15: invalid Segment/TotalSegments are rejected", "[r4][scan]") {
    Harness h;
    h.create();
    REQUIRE(h.call(api::Operation::Scan, R"({"TableName":"T","Segment":0})").error_type == "ValidationException");
    REQUIRE(h.call(api::Operation::Scan, R"({"TableName":"T","TotalSegments":2})").error_type == "ValidationException");
    REQUIRE(h.call(api::Operation::Scan, R"({"TableName":"T","Segment":2,"TotalSegments":2})").error_type == "ValidationException");
    REQUIRE(h.call(api::Operation::Scan, R"({"TableName":"T","Segment":0,"TotalSegments":0})").error_type == "ValidationException");
}

TEST_CASE("CS-15: segmented scan honors Limit with a resumable cursor", "[r4][scan]") {
    Harness h;
    h.create();
    for (int i = 0; i < 30; ++i)
        h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"k)" + std::to_string(i) + R"("}}})");

    // Page through segment 0 of 2 with a small limit; the union of pages must be the
    // whole segment with no repeats.
    std::set<std::string> seen;
    std::string start_key;
    for (int guard = 0; guard < 100; ++guard) {
        std::string body = R"({"TableName":"T","Segment":0,"TotalSegments":2,"Limit":3)";
        if (!start_key.empty()) body += R"(,"ExclusiveStartKey":)" + start_key;
        body += "}";
        auto r = h.call(api::Operation::Scan, body);
        REQUIRE(r.status == 200);
        for (const auto& p : pk_values(r.body)) REQUIRE(seen.insert(p).second);
        std::smatch m;
        std::regex lek("\"LastEvaluatedKey\":(\\{[^}]*\\}[^}]*\\})");
        if (std::regex_search(r.body, m, lek)) start_key = m[1].str();
        else break;
    }
    REQUIRE(!seen.empty());
}

TEST_CASE("CS-17: batch/transaction request-size limits are enforced", "[r4][limits]") {
    Harness h;
    h.create();

    auto build_writes = [](int n) {
        std::string s = R"({"RequestItems":{"T":[)";
        for (int i = 0; i < n; ++i) {
            if (i) s += ",";
            s += R"({"PutRequest":{"Item":{"pk":{"S":"k)" + std::to_string(i) + R"("}}}})";
        }
        return s + "]}}";
    };
    REQUIRE(h.call(api::Operation::BatchWriteItem, build_writes(25)).status == 200);
    auto over = h.call(api::Operation::BatchWriteItem, build_writes(26));
    REQUIRE(over.status == 400);
    REQUIRE(over.error_type == "ValidationException");

    auto build_gets = [](int n) {
        std::string s = R"({"RequestItems":{"T":{"Keys":[)";
        for (int i = 0; i < n; ++i) {
            if (i) s += ",";
            s += R"({"pk":{"S":"k)" + std::to_string(i) + R"("}})";
        }
        return s + "]}}}";
    };
    REQUIRE(h.call(api::Operation::BatchGetItem, build_gets(100)).status == 200);
    REQUIRE(h.call(api::Operation::BatchGetItem, build_gets(101)).error_type == "ValidationException");

    auto build_txn = [](int n) {
        std::string s = R"({"TransactItems":[)";
        for (int i = 0; i < n; ++i) {
            if (i) s += ",";
            s += R"({"Put":{"TableName":"T","Item":{"pk":{"S":"t)" + std::to_string(i) + R"("}}}})";
        }
        return s + "]}";
    };
    REQUIRE(h.call(api::Operation::TransactWriteItems, build_txn(100)).status == 200);
    REQUIRE(h.call(api::Operation::TransactWriteItems, build_txn(101)).error_type == "ValidationException");
}
