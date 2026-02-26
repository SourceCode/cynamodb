#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <filesystem>
#include <string>

using namespace cynamodb::engine;
using namespace cynamodb::core;

TEST_CASE("TableManager basic operations", "[engine]") {
    std::string metadata_path = "./test_metadata.bin";
    std::filesystem::remove(metadata_path);

    TableManager manager(metadata_path);

    SECTION("Create and describe table") {
        TableDefinition table;
        table.table_name = "TestTable";
        table.key_schema = {{"pk", KeyType::HASH}};
        table.attribute_definitions = {{"pk", AttributeType::S}};

        auto res = manager.create_table(table);
        REQUIRE(res.has_value());
        REQUIRE(res->table_name == "TestTable");

        auto desc = manager.describe_table("TestTable");
        REQUIRE(desc.has_value());
        REQUIRE(desc->table_name == "TestTable");
    }

    std::filesystem::remove(metadata_path);
}
