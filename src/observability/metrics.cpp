#include <cynamodb/observability/metrics.hpp>
#include <algorithm>
#include <numeric>

namespace cynamodb::observability {

Metrics::ThreadLocalData& Metrics::get_thread_local() {
    thread_local ThreadLocalData data;
    static thread_local bool registered = []() {
        std::lock_guard lock(get_registry_mutex());
        get_all_threads().push_back(&data);
        return true;
    }();
    (void)registered;
    return data;
}

std::vector<Metrics::ThreadLocalData*>& Metrics::get_all_threads() {
    static std::vector<ThreadLocalData*> threads;
    return threads;
}

std::mutex& Metrics::get_registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

void Metrics::increment(MetricId id, uint64_t delta) {
    auto& data = get_thread_local();
    data.counters[static_cast<size_t>(id)] += delta;
}

void Metrics::record_latency(MetricId id, std::chrono::nanoseconds duration) {
    // Latency is recorded as total nanoseconds in a counter for now.
    // In a real implementation we'd use histograms.
    increment(id, static_cast<uint64_t>(duration.count()));
}

uint64_t Metrics::get_total(MetricId id) {
    std::lock_guard lock(get_registry_mutex());
    uint64_t total = 0;
    for (auto* thread_data : get_all_threads()) {
        total += thread_data->counters[static_cast<size_t>(id)];
    }
    return total;
}

void Metrics::reset_all() {
    std::lock_guard lock(get_registry_mutex());
    for (auto* thread_data : get_all_threads()) {
        std::fill(std::begin(thread_data->counters), std::end(thread_data->counters), 0);
    }
}

} // namespace cynamodb::observability
