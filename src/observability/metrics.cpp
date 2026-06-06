#include <cynamodb/observability/metrics.hpp>
#include <algorithm>
#include <numeric>

namespace cynamodb::observability {

Metrics::ThreadLocalData& Metrics::get_thread_local() {
    thread_local ThreadLocalData data;
    // RAII registration. The previous implementation registered a pointer to
    // the thread_local `data` but never removed it, so once a worker thread
    // exited its storage was destroyed while the registry still held a dangling
    // pointer -- get_total()/reset_all() then dereferenced freed memory
    // (SIGSEGV). This guard removes the pointer on thread exit and first folds
    // the thread's counters into the global totals so exited threads still
    // count toward get_total(). `data` is declared before `registration`, so it
    // is destroyed after the guard runs.
    struct Registration {
        ThreadLocalData* data;
        explicit Registration(ThreadLocalData* d) : data(d) {
            std::lock_guard lock(get_registry_mutex());
            get_all_threads().push_back(data);
        }
        ~Registration() {
            std::lock_guard lock(get_registry_mutex());
            uint64_t* dead = get_dead_totals();
            for (size_t i = 0; i < static_cast<size_t>(MetricId::Count); ++i) {
                dead[i] += data->counters[i];
            }
            auto& threads = get_all_threads();
            threads.erase(std::remove(threads.begin(), threads.end(), data), threads.end());
        }
    };
    thread_local Registration registration(&data);
    (void)registration;
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

uint64_t* Metrics::get_dead_totals() {
    static uint64_t totals[static_cast<size_t>(MetricId::Count)] = {0};
    return totals;
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
    uint64_t total = get_dead_totals()[static_cast<size_t>(id)];
    for (auto* thread_data : get_all_threads()) {
        total += thread_data->counters[static_cast<size_t>(id)];
    }
    return total;
}

void Metrics::reset_all() {
    std::lock_guard lock(get_registry_mutex());
    uint64_t* dead = get_dead_totals();
    std::fill(dead, dead + static_cast<size_t>(MetricId::Count), 0);
    for (auto* thread_data : get_all_threads()) {
        std::fill(std::begin(thread_data->counters), std::end(thread_data->counters), 0);
    }
}

} // namespace cynamodb::observability
