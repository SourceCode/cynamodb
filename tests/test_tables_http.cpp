#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <filesystem>

using namespace cynamodb::engine;
using namespace cynamodb::core;

TEST_CASE("Tables basic operations", "[tables]") {
    std::string metadata_path = "./test_metadata_tables.bin";
    std::filesystem::remove(metadata_path);
    
    TableManager manager(metadata_path);

    SECTION("CreateTable and ListTables") {
        TableDefinition table;
        table.table_name = "Table1";
        auto res = manager.create_table(table);
        REQUIRE(res.has_value());

        auto list = manager.list_tables();
        REQUIRE(list.size() == 1);
        REQUIRE(list[0] == "Table1");
    }

    std::filesystem::remove(metadata_path);
}
