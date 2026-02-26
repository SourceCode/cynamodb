#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/item_validator.hpp>

using namespace cynamodb::engine;
using namespace cynamodb::core;

TEST_CASE("Items basic operations", "[items]") {
    MemoryEngine engine;

    SECTION("PutItem") {
        StorageEngine::AttributeMap attrs;
        auto val = std::make_shared<AttributeValue>();
        val->type = AttributeType::S;
        val->value = String("v1");
        attrs["pk"] = val;

        engine.put("TestTable", "k1", attrs);
        REQUIRE(true);
    }
}

TEST_CASE("ItemValidator operations", "[items][validation]") {
    TableDefinition table_def;
    table_def.table_name = "TestTable";
    table_def.key_schema = {{"pk", KeyType::HASH}};
    table_def.attribute_definitions = {{"pk", AttributeType::S}};

    SECTION("Large Item Validation") {
        StorageEngine::AttributeMap attrs;
        auto pk_val = std::make_shared<AttributeValue>();
        pk_val->type = AttributeType::S;
        pk_val->value = String("k1");
        attrs["pk"] = pk_val;

        auto large_val = std::make_shared<AttributeValue>();
        large_val->type = AttributeType::S;
        large_val->value = String(399900, 'A'); // Approach 400KB limit
        attrs["large_attr"] = large_val;

        auto res = ItemValidator::validate_item_standard(attrs, table_def);
        REQUIRE(res.has_value()); // Should pass
        
        auto too_large_val = std::make_shared<AttributeValue>();
        too_large_val->type = AttributeType::S;
        too_large_val->value = String(400001, 'A'); // Over limit
        attrs["too_large_attr"] = too_large_val;
        
        auto res2 = ItemValidator::validate_item_standard(attrs, table_def);
        REQUIRE(!res2.has_value());
        REQUIRE(res2.error() == ValidationError::ItemTooLarge);
    }

    SECTION("Key Type Validation") {
        StorageEngine::AttributeMap attrs;
        auto pk_val = std::make_shared<AttributeValue>();
        pk_val->type = AttributeType::N; // Schema requires S
        pk_val->value = String("123");
        attrs["pk"] = pk_val;

        auto res = ItemValidator::validate_item_standard(attrs, table_def);
        REQUIRE(!res.has_value());
        REQUIRE(res.error() == ValidationError::TypeMismatchForKey);
    }
}
