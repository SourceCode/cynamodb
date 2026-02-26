#include <cynamodb/utils/chaos_engine.hpp>
#include <thread>
#include <algorithm>

namespace cynamodb::utils {

bool ChaosEngine::should_inject(FaultType type) {
    if (!enabled_.load(std::memory_order_relaxed)) return false;

    std::lock_guard lock(mutex_);
    auto it = probabilities_.find(type);
    if (it == probabilities_.end()) return false;

    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(rng_) < it->second;
}

void ChaosEngine::set_probability(FaultType type, float probability) {
    std::lock_guard lock(mutex_);
    probabilities_[type] = std::clamp(probability, 0.0f, 1.0f);
}

void ChaosEngine::set_enabled(bool enabled) {
    enabled_.store(enabled, std::memory_order_relaxed);
}

void ChaosEngine::inject_latency(uint32_t min_ms, uint32_t max_ms) {
    if (!enabled_.load(std::memory_order_relaxed)) return;
    
    std::uniform_int_distribution<uint32_t> dist(min_ms, max_ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng_)));
}

} // namespace cynamodb::utils
