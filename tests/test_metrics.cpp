#include <catch2/catch_test_macros.hpp>
#include <cynamodb/observability/metrics.hpp>
#include <thread>
#include <vector>

using namespace cynamodb::observability;

TEST_CASE("Thread-local metrics accuracy", "[observability][metrics]") {
    Metrics::reset_all();
    
    const int num_threads = 10;
    const int increments_per_thread = 100000;
    
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([=]() {
            for (int j = 0; j < increments_per_thread; ++j) {
                Metrics::increment(MetricId::RequestCount);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    REQUIRE(Metrics::get_total(MetricId::RequestCount) == static_cast<uint64_t>(num_threads) * increments_per_thread);
}
