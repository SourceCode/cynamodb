#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <random>
#include <string>

namespace cynamodb::utils {

enum class FaultType {
    IOError,
    Latency,
    BitFlip,
    NetworkDrop,
    MemoryPressure
};

class ChaosEngine {
public:
    static bool should_inject(FaultType type);
    static void set_probability(FaultType type, float probability);
    static void set_enabled(bool enabled);
    
    static void inject_latency(uint32_t min_ms, uint32_t max_ms);

private:
    static inline std::atomic<bool> enabled_{false};
    static inline std::mutex mutex_;
    static inline std::map<FaultType, float> probabilities_;
    static inline thread_local std::mt19937 rng_{std::random_device{}()};
};

} // namespace cynamodb::utils
