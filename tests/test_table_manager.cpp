#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/table_manager.hpp>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

using namespace cynamodb;
using namespace cynamodb::engine;

namespace {

std::string unique_metadata_path() {
    static std::atomic<uint64_t> counter{0};
    auto dir = std::filesystem::temp_directory_path() /
               ("cynamodb_tm_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return (dir / "metadata.bin").string();
}

core::TableDefinition make_def(const std::string& name) {
    core::TableDefinition def;
    def.table_name = name;
    def.key_schema = {{"pk", core::KeyType::HASH}, {"sk", core::KeyType::RANGE}};
    def.attribute_definitions = {{"pk", core::AttributeType::S}, {"sk", core::AttributeType::N}};
    def.billing_mode = core::BillingMode::PROVISIONED;
    def.creation_epoch_seconds = 12345;
    return def;
}

}  // namespace

TEST_CASE("TableManager persists the catalog across instances", "[tables][persistence]") {
    const std::string path = unique_metadata_path();

    {
        TableManager tm(path);
        REQUIRE(tm.create_table(make_def("Alpha")).has_value());
        REQUIRE(tm.create_table(make_def("Beta")).has_value());
    }

    // A fresh manager over the same metadata file must see both tables with their
    // schema intact.
    {
        TableManager tm(path);
        auto names = tm.list_tables();
        REQUIRE(names.size() == 2);

        auto alpha = tm.describe_table("Alpha");
        REQUIRE(alpha.has_value());
        REQUIRE(alpha->key_schema.size() == 2);
        REQUIRE(alpha->key_schema[0].attribute_name == "pk");
        REQUIRE(alpha->key_schema[0].key_type == core::KeyType::HASH);
        REQUIRE(alpha->key_schema[1].key_type == core::KeyType::RANGE);
        REQUIRE(alpha->attribute_definitions.at("sk") == core::AttributeType::N);
        REQUIRE(alpha->billing_mode == core::BillingMode::PROVISIONED);
        REQUIRE(alpha->creation_epoch_seconds == 12345);

        // Creating a table that was loaded from disk must still be rejected.
        REQUIRE(!tm.create_table(make_def("Alpha")).has_value());
    }
}

TEST_CASE("TableManager tolerates a missing or unrecognized metadata file", "[tables][persistence]") {
    SECTION("missing file yields an empty catalog") {
        const std::string path = unique_metadata_path();
        std::filesystem::remove(path);
        TableManager tm(path);
        REQUIRE(tm.list_tables().empty());
    }

    SECTION("garbage file is ignored rather than crashing") {
        const std::string path = unique_metadata_path();
        {
            std::ofstream os(path, std::ios::binary);
            os << "not a valid metadata file";
        }
        TableManager tm(path);
        REQUIRE(tm.list_tables().empty());
    }
}
