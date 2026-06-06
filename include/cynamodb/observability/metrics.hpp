#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <vector>
#include <mutex>
#include <memory>

namespace cynamodb::observability {

enum class MetricId {
    RequestCount,
    ErrorCount,
    SuccessCount,
    ThrottledCount,
    BytesReceived,
    BytesSent,
    Count // Must be last
};

class Metrics {
public:
    static void increment(MetricId id, uint64_t delta = 1);
    static void record_latency(MetricId id, std::chrono::nanoseconds duration);
    
    static uint64_t get_total(MetricId id);
    static void reset_all();

private:
    struct alignas(64) ThreadLocalData {
        uint64_t counters[static_cast<size_t>(MetricId::Count)] = {0};
    };

    static ThreadLocalData& get_thread_local();
    static std::vector<ThreadLocalData*>& get_all_threads();
    static std::mutex& get_registry_mutex();
    // Accumulates counters from threads that have exited so their contribution
    // is not lost (and so the registry never dereferences freed thread_local
    // storage). Guarded by get_registry_mutex().
    static uint64_t* get_dead_totals();
};

class ScopedTimer {
public:
    explicit ScopedTimer(MetricId id) : id_(id), start_(std::chrono::high_resolution_clock::now()) {}
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        Metrics::record_latency(id_, end - start_);
    }
private:
    MetricId id_;
    std::chrono::high_resolution_clock::time_point start_;
};

#define SCOPED_METRIC_TIMER(id) cynamodb::observability::ScopedTimer timer_##__LINE__(id)

} // namespace cynamodb::observability
