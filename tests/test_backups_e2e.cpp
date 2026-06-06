// Tests for Backups / PITR / global tables / continuous backups via the API.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cynamodb/api/handlers.hpp>
#include <cynamodb/backups/manager.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <filesystem>
#include <regex>
#include <string>

using namespace cynamodb;
using Catch::Matchers::ContainsSubstring;

namespace {
std::string tmp_dir() {
    static std::atomic<uint64_t> c{0};
    auto dir = std::filesystem::temp_directory_path() / ("cynamodb_bk_" + std::to_string(c.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir.string();
}
struct Harness {
    std::string dir = tmp_dir();
    engine::TableManager tables{dir + "/metadata.bin"};
    engine::MemoryEngine storage;
    backups::BackupManager backups{dir + "/backups"};
    api::ApiResult call(api::Operation op, const std::string& b) {
        return api::handle_operation(tables, storage, op, b, nullptr, nullptr, &backups);
    }
    void create(const std::string& name) {
        REQUIRE(call(api::Operation::CreateTable,
                     R"({"TableName":")" + name +
                         R"(","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                         R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})").status == 200);
    }
};
std::string extract(const std::string& body, const std::string& key) {
    std::regex re("\"" + key + "\":\"([^\"]*)\"");
    std::smatch m;
    if (std::regex_search(body, m, re)) return m[1].str();
    return "";
}
}  // namespace

TEST_CASE("CreateBackup / List / Describe / RestoreFromBackup / Delete", "[backups]") {
    Harness h;
    h.create("T");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"1"}}})");
    h.call(api::Operation::PutItem, R"({"TableName":"T","Item":{"pk":{"S":"b"},"v":{"N":"2"}}})");

    auto cb = h.call(api::Operation::CreateBackup, R"({"TableName":"T","BackupName":"bk1"})");
    REQUIRE(cb.status == 200);
    std::string arn = extract(cb.body, "BackupArn");
    REQUIRE_FALSE(arn.empty());

    auto lb = h.call(api::Operation::ListBackups, R"({"TableName":"T"})");
    REQUIRE_THAT(lb.body, ContainsSubstring("bk1"));

    auto db = h.call(api::Operation::DescribeBackup, R"({"BackupArn":")" + arn + R"("})");
    REQUIRE(db.status == 200);

    // Mutate the live table after the backup; restore must reflect the backup, not now.
    h.call(api::Operation::DeleteItem, R"({"TableName":"T","Key":{"pk":{"S":"a"}}})");

    auto rs = h.call(api::Operation::RestoreTableFromBackup,
                     R"({"BackupArn":")" + arn + R"(","TargetTableName":"T_restored"})");
    REQUIRE(rs.status == 200);
    auto got_a = h.call(api::Operation::GetItem, R"({"TableName":"T_restored","Key":{"pk":{"S":"a"}}})");
    REQUIRE_THAT(got_a.body, ContainsSubstring("\"v\":{\"N\":\"1\"}"));  // 'a' survived in the backup
    auto got_b = h.call(api::Operation::GetItem, R"({"TableName":"T_restored","Key":{"pk":{"S":"b"}}})");
    REQUIRE_THAT(got_b.body, ContainsSubstring("\"v\":{\"N\":\"2\"}"));

    auto del = h.call(api::Operation::DeleteBackup, R"({"BackupArn":")" + arn + R"("})");
    REQUIRE(del.status == 200);
    REQUIRE_THAT(h.call(api::Operation::ListBackups, R"({"TableName":"T"})").body, ContainsSubstring("[]"));
}

TEST_CASE("backups persist across a reload", "[backups][persist]") {
    std::string dir = tmp_dir();
    std::string arn;
    {
        engine::TableManager tables{dir + "/metadata.bin"};
        engine::MemoryEngine storage;
        backups::BackupManager backups{dir + "/backups"};
        api::handle_operation(tables, storage, api::Operation::CreateTable,
                              R"({"TableName":"T","KeySchema":[{"AttributeName":"pk","KeyType":"HASH"}],)"
                              R"("AttributeDefinitions":[{"AttributeName":"pk","AttributeType":"S"}]})",
                              nullptr, nullptr, &backups);
        api::handle_operation(tables, storage, api::Operation::PutItem,
                              R"({"TableName":"T","Item":{"pk":{"S":"a"},"v":{"N":"7"}}})", nullptr, nullptr, &backups);
        auto cb = api::handle_operation(tables, storage, api::Operation::CreateBackup,
                                        R"({"TableName":"T","BackupName":"bk"})", nullptr, nullptr, &backups);
        arn = extract(cb.body, "BackupArn");
    }
    // Fresh manager reloads the snapshot from disk.
    engine::TableManager tables{dir + "/metadata.bin"};
    engine::MemoryEngine storage;
    backups::BackupManager backups{dir + "/backups"};
    auto rs = api::handle_operation(tables, storage, api::Operation::RestoreTableFromBackup,
                                    R"({"BackupArn":")" + arn + R"(","TargetTableName":"R"})",
                                    nullptr, nullptr, &backups);
    REQUIRE(rs.status == 200);
    auto got = api::handle_operation(tables, storage, api::Operation::GetItem,
                                     R"({"TableName":"R","Key":{"pk":{"S":"a"}}})", nullptr, nullptr, &backups);
    REQUIRE_THAT(got.body, ContainsSubstring("\"v\":{\"N\":\"7\"}"));
}

TEST_CASE("RestoreTableToPointInTime copies current state", "[backups][pitr]") {
    Harness h;
    h.create("Src");
    h.call(api::Operation::PutItem, R"({"TableName":"Src","Item":{"pk":{"S":"a"},"v":{"N":"5"}}})");
    auto rs = h.call(api::Operation::RestoreTableToPointInTime,
                     R"({"SourceTableName":"Src","TargetTableName":"Dst"})");
    REQUIRE(rs.status == 200);
    REQUIRE_THAT(h.call(api::Operation::GetItem, R"({"TableName":"Dst","Key":{"pk":{"S":"a"}}})").body,
                 ContainsSubstring("\"v\":{\"N\":\"5\"}"));
}

TEST_CASE("global tables and continuous backups", "[backups][global]") {
    Harness h;
    h.create("T");
    auto cg = h.call(api::Operation::CreateGlobalTable,
                     R"({"GlobalTableName":"T","ReplicationGroup":[{"RegionName":"ddblocal"}]})");
    REQUIRE(cg.status == 200);
    REQUIRE_THAT(cg.body, ContainsSubstring("\"GlobalTableStatus\":\"ACTIVE\""));

    auto lg = h.call(api::Operation::ListGlobalTables, "{}");
    REQUIRE_THAT(lg.body, ContainsSubstring("\"GlobalTableName\":\"T\""));

    auto ucb = h.call(api::Operation::UpdateContinuousBackups,
                      R"({"TableName":"T","PointInTimeRecoverySpecification":{"PointInTimeRecoveryEnabled":true}})");
    REQUIRE(ucb.status == 200);
    REQUIRE_THAT(ucb.body, ContainsSubstring("\"PointInTimeRecoveryStatus\":\"ENABLED\""));
}
