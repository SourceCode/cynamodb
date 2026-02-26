#include <catch2/catch_test_macros.hpp>
#include <cynamodb/engine/lsm/block_cache.hpp>
#include <cynamodb/engine/lsm/bloom_filter.hpp>
#include <string>

using namespace cynamodb::engine::lsm;

TEST_CASE("ShardedBlockCache basic operations (primitive)", "[lsm][cache]") {
    ShardedBlockCache cache(1024);

    SECTION("Put and Get") {
        cache.insert("key1", std::make_shared<Block>("data1"));
        auto handle = cache.get("key1");
        REQUIRE(handle.has_value());
        REQUIRE(handle->block->data() == "data1");
    }
}

TEST_CASE("BlockedBloomFilter basic operations (primitive)", "[lsm][bloom]") {
    BlockedBloomFilter filter(100, 0.01);

    SECTION("Add and Contains") {
        filter.add("alpha");
        filter.add("beta");
        
        REQUIRE(filter.contains("alpha"));
        REQUIRE(filter.contains("beta"));
    }
}
