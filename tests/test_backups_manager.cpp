#include <catch2/catch_test_macros.hpp>
#include <cynamodb/backups/manager.hpp>
#include <filesystem>
#include <string>

using namespace cynamodb::backups;
using namespace cynamodb::core;

TEST_CASE("BackupManager basic operations", "[backups]") {
    std::string backups_dir = "./test_backups";
    std::filesystem::remove_all(backups_dir);

    BackupManager manager(backups_dir);

    SECTION("List backups empty") {
        auto backups = manager.list_backups();
        REQUIRE(backups.empty());
    }

    std::filesystem::remove_all(backups_dir);
}
