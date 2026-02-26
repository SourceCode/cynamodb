#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/transactions/manager.hpp>
#include <cynamodb/engine/memory_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <memory>

using namespace cynamodb::engine;
using namespace cynamodb::engine::transactions;

TEST_CASE("TransactionManager basic operations", "[tx]") {
    auto storage = std::make_shared<MemoryEngine>();
    auto table_manager = std::make_shared<TableManager>("./test_metadata_tx.bin");

    TransactionManager tx_manager(table_manager, storage);

    SECTION("Execute TransactWriteItems") {
        TransactWriteItem item;
        item.table_name = "TxTable";
        item.key = "k1";
        
        StorageEngine::AttributeMap attrs;
        auto val = std::make_shared<cynamodb::core::AttributeValue>();
        val->type = cynamodb::core::AttributeType::S;
        val->value = cynamodb::core::String("v1");
        attrs["v"] = val;
        
        item.put_attributes = attrs;
        
        auto res = tx_manager.execute_transact_write_items({item});
        REQUIRE(res.has_value());
    }

    SECTION("Duplicate items rejection") {
        TransactWriteItem item1;
        item1.table_name = "TxTable";
        item1.key = "k1";

        TransactWriteItem item2;
        item2.table_name = "TxTable";
        item2.key = "k1";

        auto res = tx_manager.execute_transact_write_items({item1, item2});
        REQUIRE(!res.has_value());
        REQUIRE(res.error()[0] == TransactionError::ValidationFailed);
    }

    SECTION("Exceed 25 items limit") {
        std::vector<TransactWriteItem> items;
        for (int i = 0; i < 26; ++i) {
            TransactWriteItem item;
            item.table_name = "TxTable";
            item.key = "k" + std::to_string(i);
            items.push_back(item);
        }

        auto res = tx_manager.execute_transact_write_items(items);
        REQUIRE(!res.has_value());
        REQUIRE(res.error()[0] == TransactionError::ValidationFailed);
    }
}
