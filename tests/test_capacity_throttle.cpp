// Tests that capacity throttling is wired into the request path: a provisioned
// table exhausts its token bucket and returns ProvisionedThroughputExceededException,
// while a PAY_PER_REQUEST table does not throttle under the same load.
#include <catch2/catch_test_macros.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/capacity/manager.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <filesystem>
#include <string>

using namespace cynamodb;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_cap_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}
struct Harness {
    engine::TableManager tables{meta_path()};
    engine::MemoryEngine storage;
    engine::capacity::CapacityManager capacity;
    api::ApiResult call(api::Operation op, const std::string& b) {
        return api::handle_operation(tables, storage, op, b, &capacity);
    }
};
}  // namespace

TEST_CASE("a provisioned table throttles once its write capacity is exhausted", "[capacity][throttle]") {
    Harness h;
    auto created = h.call(api::Operation::CreateTable,
                          R"({"TableName":"P","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                          R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}],)"
                          R"("ProvisionedThroughput":{"ReadCapacityUnits":1,"WriteCapacityUnits":1}})");
    REQUIRE(created.status == 200);

    int throttled = 0;
    int ok = 0;
    for (int i = 0; i < 500; ++i) {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"P","Item":{"pk":{"S":"k"}}})");
        if (r.status == 200) ++ok;
        else if (r.error_type == "ProvisionedThroughputExceededException") ++throttled;
    }
    // The 300s burst lets a few hundred writes through, then it must throttle.
    REQUIRE(ok > 0);
    REQUIRE(throttled > 0);
}

TEST_CASE("a PAY_PER_REQUEST table does not throttle under the same load", "[capacity][throttle]") {
    Harness h;
    REQUIRE(h.call(api::Operation::CreateTable,
                   R"({"TableName":"D","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                   R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    for (int i = 0; i < 500; ++i) {
        auto r = h.call(api::Operation::PutItem, R"({"TableName":"D","Item":{"pk":{"S":"k"}}})");
        REQUIRE(r.status == 200);
    }
}

TEST_CASE("deleting a table unregisters its capacity", "[capacity][throttle]") {
    Harness h;
    h.call(api::Operation::CreateTable,
           R"({"TableName":"X","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
           R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}],)"
           R"("ProvisionedThroughput":{"ReadCapacityUnits":5,"WriteCapacityUnits":5}})");
    auto del = h.call(api::Operation::DeleteTable, R"({"TableName":"X"})");
    REQUIRE(del.status == 200);
    // After unregister, a recreate with on-demand billing starts fresh (no throttle).
    REQUIRE(h.call(api::Operation::CreateTable,
                   R"({"TableName":"X","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                   R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    REQUIRE(h.call(api::Operation::PutItem, R"({"TableName":"X","Item":{"pk":{"S":"k"}}})").status == 200);
}
