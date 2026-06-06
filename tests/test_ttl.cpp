// Tests for Time-To-Live: UpdateTimeToLive/DescribeTimeToLive and read-time expiry.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string meta_path() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_ttl_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}
long long now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}
struct Harness {
    std::string mp = meta_path();
    engine::TableManager tables{mp};
    engine::MemoryEngine storage;
    api::ApiResult call(api::Operation op, const std::string& b) { return api::handle_operation(tables, storage, op, b); }
    void create() {
        REQUIRE(call(api::Operation::CreateTable,
                     R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                     R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    }
};
}  // namespace

TEST_CASE("UpdateTimeToLive then DescribeTimeToLive", "[ttl]") {
    Harness h;
    h.create();
    auto u = h.call(api::Operation::UpdateTimeToLive,
                    R"({"TableName":"T","TimeToLiveSpecification":{"AttributeName":"expireAt","Enabled":true}})");
    REQUIRE(u.status == 200);
    REQUIRE_THAT(u.body, ContainsSubstring("\"Enabled\":true"));

    auto d = h.call(api::Operation::DescribeTimeToLive, R"({"TableName":"T"})");
    REQUIRE(d.status == 200);
    REQUIRE_THAT(d.body, ContainsSubstring("\"TimeToLiveStatus\":\"ENABLED\""));
    REQUIRE_THAT(d.body, ContainsSubstring("\"AttributeName\":\"expireAt\""));
}

TEST_CASE("DescribeTimeToLive defaults to DISABLED", "[ttl]") {
    Harness h;
    h.create();
    auto d = h.call(api::Operation::DescribeTimeToLive, R"({"TableName":"T"})");
    REQUIRE_THAT(d.body, ContainsSubstring("\"TimeToLiveStatus\":\"DISABLED\""));
}

TEST_CASE("expired items are filtered from reads", "[ttl]") {
    Harness h;
    h.create();
    h.call(api::Operation::UpdateTimeToLive,
           R"({"TableName":"T","TimeToLiveSpecification":{"AttributeName":"exp","Enabled":true}})");

    std::string past = std::to_string(now() - 100);
    std::string future = std::to_string(now() + 100000);
    h.call(api::Operation::PutItem,
           R"({"TableName":"T","Item":{"pk":{"S":"dead"},"exp":{"N":")" + past + R"("}}})");
    h.call(api::Operation::PutItem,
           R"({"TableName":"T","Item":{"pk":{"S":"alive"},"exp":{"N":")" + future + R"("}}})");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"nottl"}}})");

    SECTION("GetItem hides an expired item") {
        REQUIRE(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"dead"}}})").body == "{}");
        REQUIRE_THAT(h.call(api::Operation::GetItem, R"({"TableName":"T","Key":{"pk":{"S":"alive"}}})").body,
                     ContainsSubstring("\"alive\""));
    }

    SECTION("Scan excludes expired items but keeps live and TTL-less ones") {
        auto s = h.call(api::Operation::Scan, R"({"TableName":"T"})");
        REQUIRE(s.body.find("\"dead\"") == std::string::npos);
        REQUIRE_THAT(s.body, ContainsSubstring("\"alive\""));
        REQUIRE_THAT(s.body, ContainsSubstring("\"nottl\""));
    }
}

TEST_CASE("TTL specification persists across reload", "[ttl]") {
    std::string mp = meta_path();
    {
        engine::TableManager tables{mp};
        engine::MemoryEngine storage;
        api::handle_operation(tables, storage, api::Operation::CreateTable,
                              R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                              R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})");
        api::handle_operation(tables, storage, api::Operation::UpdateTimeToLive,
                              R"({"TableName":"T","TimeToLiveSpecification":{"AttributeName":"ttl","Enabled":true}})");
    }
    engine::TableManager reloaded{mp};
    auto def = reloaded.describe_table("T");
    REQUIRE(def.has_value());
    REQUIRE(def->ttl_specification.has_value());
    REQUIRE(def->ttl_specification->enabled);
    REQUIRE(def->ttl_specification->attribute_name == "ttl");
}
