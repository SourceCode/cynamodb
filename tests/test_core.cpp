#include <catch2/catch_test_macros.hpp>
#include <cynamodb/core/types.hpp>
#include <cynamodb/core/memory.hpp>
#include <cynamodb/core/memory_resource.hpp>
#include <cynamodb/engine/lsm/key_manager.hpp>

using namespace cynamodb::core;

TEST_CASE("AttributeValue variant works", "[core]") {
    AttributeValue val;
    val.type = AttributeType::S;
    val.value = String("test");
    REQUIRE(std::holds_alternative<String>(val.value));
    REQUIRE(std::get<String>(val.value) == "test");
}

TEST_CASE("Arena basic allocation", "[core]") {
    Arena arena(1024);
    void* p1 = arena.allocate(100);
    REQUIRE(p1 != nullptr);
    REQUIRE(arena.bytes_allocated() == 100);
}

TEST_CASE("PMR Arena allocator restricts memory to buffer", "[core][pmr]") {
    std::byte stack_buf[1024];
    std::pmr::monotonic_buffer_resource arena(stack_buf, sizeof(stack_buf), std::pmr::null_memory_resource());
    
    std::pmr::vector<int> vec(&arena);
    vec.reserve(10);
    
    const std::byte* buf_start = stack_buf;
    const std::byte* buf_end = stack_buf + sizeof(stack_buf);
    
    vec.push_back(42);
    const std::byte* elem_addr = reinterpret_cast<const std::byte*>(&vec[0]);
    
    REQUIRE(elem_addr >= buf_start);
    REQUIRE(elem_addr < buf_end);
}

TEST_CASE("TrackingMemoryResource tracks allocations", "[core][pmr]") {
    TrackingMemoryResource tracker;
    {
        std::pmr::vector<int> vec(&tracker);
        vec.reserve(100);
        REQUIRE(tracker.current_usage() >= 100 * sizeof(int));
        REQUIRE(tracker.peak_usage() >= 100 * sizeof(int));
    }
    REQUIRE(tracker.current_usage() == 0);
}
