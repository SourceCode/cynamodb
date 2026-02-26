#include <catch2/catch_test_macros.hpp>
#include <cynamodb/core/schema.hpp>
#include <cynamodb/engine/lsm/gsi_manager.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <filesystem>

using namespace cynamodb::core;
using namespace cynamodb::engine::lsm;
using namespace cynamodb::engine;

TEST_CASE("GSI Projection", "[schema][gsi]") {
    GlobalSecondaryIndex gsi;
    gsi.index_name = "GSI1";
    gsi.key_schema = {{"gsi_pk", KeyType::HASH}};
    
    std::vector<KeySchemaElement> base_schema = {{"pk", KeyType::HASH}};

    StorageEngine::AttributeMap item;
    
    auto pk_val = std::make_shared<AttributeValue>();
    pk_val->type = AttributeType::S;
    pk_val->value = String("base_key");
    item["pk"] = pk_val;

    auto gsi_pk_val = std::make_shared<AttributeValue>();
    gsi_pk_val->type = AttributeType::S;
    gsi_pk_val->value = String("gsi_key");
    item["gsi_pk"] = gsi_pk_val;

    auto other_val = std::make_shared<AttributeValue>();
    other_val->type = AttributeType::S;
    other_val->value = String("other");
    item["other"] = other_val;

    SECTION("Sparse Index: Missing GSI key") {
        StorageEngine::AttributeMap sparse_item;
        sparse_item["pk"] = pk_val;
        sparse_item["other"] = other_val;
        
        GsiManager manager(nullptr);
        auto proj = manager.project_item(sparse_item, gsi, base_schema);
        REQUIRE_FALSE(proj.has_value());
    }

    SECTION("KEYS_ONLY projection") {
        gsi.projection.projection_type = ProjectionType::KEYS_ONLY;
        
        GsiManager manager(nullptr);
        auto proj = manager.project_item(item, gsi, base_schema);
        REQUIRE(proj.has_value());
        REQUIRE(proj->size() == 2); // pk and gsi_pk
        REQUIRE(proj->contains("pk"));
        REQUIRE(proj->contains("gsi_pk"));
    }
}

TEST_CASE("LSI Collection Limits", "[schema][lsi]") {
    std::string metadata_path = "./test_metadata_lsi.bin";
    std::filesystem::remove(metadata_path);
    TableManager manager(metadata_path);

    TableDefinition table;
    table.table_name = "LSITable";
    table.key_schema = {{"pk", KeyType::HASH}, {"sk", KeyType::RANGE}};
    
    LocalSecondaryIndex lsi;
    lsi.index_name = "LSI1";
    lsi.key_schema = {{"pk", KeyType::HASH}, {"lsi_sk", KeyType::RANGE}};
    table.local_secondary_indexes.push_back(lsi);

    manager.create_table(table);

    SECTION("Enforce 10GB limit") {
        std::string pk = "partition1";
        // Simulate near 10GB
        manager.update_collection_size("LSITable", pk, 10ULL * 1024 * 1024 * 1024 - 100);
        
        auto res = manager.check_collection_limit("LSITable", pk, 200);
        REQUIRE(!res.has_value());
        REQUIRE(res.error() == TableError::ItemCollectionSizeLimitExceeded);
        
        auto res2 = manager.check_collection_limit("LSITable", pk, 50);
        REQUIRE(res2.has_value());
    }

    std::filesystem::remove(metadata_path);
}
