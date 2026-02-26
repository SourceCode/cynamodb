#include <catch2/catch_test_macros.hpp>
#include <cynamodb/utils/chaos_engine.hpp>
#include <chrono>

using namespace cynamodb::utils;

TEST_CASE("ChaosEngine basic functionality", "[chaos]") {
    ChaosEngine::set_enabled(true);
    ChaosEngine::set_probability(FaultType::Latency, 1.0f);

    SECTION("Inject latency") {
        auto start = std::chrono::steady_clock::now();
        ChaosEngine::inject_latency(10, 10);
        auto end = std::chrono::steady_clock::now();
        
        REQUIRE(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() >= 10);
    }

    SECTION("Probability check") {
        ChaosEngine::set_probability(FaultType::IOError, 0.0f);
        REQUIRE_FALSE(ChaosEngine::should_inject(FaultType::IOError));

        ChaosEngine::set_probability(FaultType::IOError, 1.0f);
        REQUIRE(ChaosEngine::should_inject(FaultType::IOError));
    }

    ChaosEngine::set_enabled(false);
}
