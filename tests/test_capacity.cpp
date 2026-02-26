#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/capacity/manager.hpp>
#include <thread>

using namespace cynamodb::engine::capacity;
using namespace cynamodb::core;

TEST_CASE("CapacityManager basic throttling", "[capacity]") {
    CapacityManager manager;
    TableDefinition table;
    table.table_name = "ThrottledTable";
    table.provisioned_throughput.read_capacity_units = 10;
    table.provisioned_throughput.write_capacity_units = 10;
    table.billing_mode = BillingMode::PROVISIONED;

    manager.register_table(table);

    SECTION("Consume RCU within limits") {
        auto res = manager.consume_rcu("ThrottledTable", 5.0);
        REQUIRE(res.has_value());
    }

    SECTION("Throttle when limits exceeded") {
        // Initial burst is 3000 units (10 RCU * 300s)
        auto res1 = manager.consume_rcu("ThrottledTable", 3000.0);
        REQUIRE(res1.has_value());
        
        // This one allows us to go negative but still returns success (Task 15)
        auto res2 = manager.consume_rcu("ThrottledTable", 1.0);
        REQUIRE(res2.has_value());
        
        // This one should fail because we are already negative
        auto res3 = manager.consume_rcu("ThrottledTable", 1.0);
        REQUIRE(!res3.has_value());
        REQUIRE(res3.error() == CapacityError::ProvisionedThroughputExceeded);
    }
}

TEST_CASE("RCU/WCU Calculation", "[capacity]") {
    SECTION("RCU Calculation") {
        // 4KB strong = 1 RCU
        REQUIRE(CapacityManager::calculate_rcu(4096, true) == 1.0);
        // 4KB eventual = 0.5 RCU
        REQUIRE(CapacityManager::calculate_rcu(4096, false) == 0.5);
        // 8KB strong = 2 RCU
        REQUIRE(CapacityManager::calculate_rcu(8192, true) == 2.0);
        // 4KB transactional = 2 RCU
        REQUIRE(CapacityManager::calculate_rcu(4096, true, true) == 2.0);
    }

    SECTION("WCU Calculation") {
        // 1KB = 1 WCU
        REQUIRE(CapacityManager::calculate_wcu(1024) == 1.0);
        // 2KB = 2 WCU
        REQUIRE(CapacityManager::calculate_wcu(2048) == 2.0);
        // 1KB transactional = 2 WCU
        REQUIRE(CapacityManager::calculate_wcu(1024, true) == 2.0);
    }
}
