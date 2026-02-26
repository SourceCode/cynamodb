#include <catch2/catch_test_macros.hpp>
#include <cynamodb/core/scheduler.hpp>
#include <atomic>
#include <chrono>
#include <thread>

using namespace cynamodb::core;

TEST_CASE("WorkStealingScheduler basic execution", "[core][scheduler]") {
    WorkStealingScheduler scheduler(1);
    std::atomic<int> counter{0};
    
    scheduler.submit([&counter]() {
        counter.fetch_add(1, std::memory_order_relaxed);
    });
    
    auto start = std::chrono::steady_clock::now();
    while (counter.load() < 1 && (std::chrono::steady_clock::now() - start) < std::chrono::seconds(1)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    REQUIRE(counter.load() == 1);
}
