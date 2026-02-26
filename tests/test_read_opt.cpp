#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/lsm/block_cache.hpp>
#include <cynamodb/engine/lsm/bloom_filter.hpp>
#include <string>

using namespace cynamodb::engine::lsm;

TEST_CASE("ShardedBlockCache basic operations", "[lsm][cache]") {
    ShardedBlockCache cache(1024 * 1024, 4); // 1MB, 4 shards

    SECTION("Insert and Get") {
        auto block = std::make_shared<Block>("data1");
        cache.insert("key1", block);
        
        auto handle = cache.get("key1");
        REQUIRE(handle.has_value());
        REQUIRE(handle->block->data() == "data1");
    }

    SECTION("Shard distribution") {
        // Just verify it doesn't crash with multiple keys
        for (int i = 0; i < 100; ++i) {
            cache.insert("key" + std::to_string(i), std::make_shared<Block>("data"));
        }
        REQUIRE(cache.get("key50").has_value());
    }
}

TEST_CASE("BlockedBloomFilter basic operations", "[lsm][bloom]") {
    BlockedBloomFilter filter(100, 0.01);

    SECTION("Add and Contains") {
        filter.add("apple");
        filter.add("banana");
        
        REQUIRE(filter.contains("apple"));
        REQUIRE(filter.contains("banana"));
        REQUIRE_FALSE(filter.contains("cherry")); // Highly likely to be false
    }
}
