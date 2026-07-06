#include <cynamodb/engine/capacity/manager.hpp>
#include <algorithm>
#include <cmath>

namespace cynamodb::engine::capacity {

TokenBucket::TokenBucket(double rate_per_sec, double burst_seconds) {
    max_tokens_ = static_cast<int64_t>(rate_per_sec * burst_seconds * 1000.0);
    refill_rate_per_ms_ = static_cast<int64_t>(rate_per_sec); // (units/sec * 1000) / 1000 ms/sec = units/ms
    tokens_.store(max_tokens_, std::memory_order_relaxed);
    last_refill_ = std::chrono::steady_clock::now();
}

void TokenBucket::refill() {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(refill_mutex_);
    
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_refill_);
    if (duration.count() <= 0) return;

    int64_t to_add = duration.count() * refill_rate_per_ms_;
    if (to_add > 0) {
        int64_t current = tokens_.load(std::memory_order_relaxed);
        int64_t next = std::min(max_tokens_, current + to_add);
        tokens_.store(next, std::memory_order_relaxed);
        last_refill_ = now;
    }
}

bool TokenBucket::consume(double units) {
    refill();
    int64_t requested = static_cast<int64_t>(units * 1000.0);
    
    int64_t current = tokens_.load(std::memory_order_relaxed);
    while (true) {
        if (current < requested) {
            // Check if we can still proceed but go negative (Task 15)
            // For simplicity, let's just fail if current < requested for now, 
            // but the prompt says "allow it to proceed but set the bucket to -4"
            // Let's implement that.
        }
        
        int64_t next = current - requested;
        if (tokens_.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
            return current >= 0; // If it was already negative, we throttle
        }
    }
}

void TokenBucket::update_rate(double new_rate_per_sec) {
    std::lock_guard lock(refill_mutex_);
    refill_rate_per_ms_ = static_cast<int64_t>(new_rate_per_sec);
    max_tokens_ = static_cast<int64_t>(new_rate_per_sec * 300.0); // 5 minutes burst
}

void CapacityManager::register_table(const core::TableDefinition& table_def) {
    std::unique_lock lock(mutex_);
    auto buckets = std::make_shared<TableBuckets>();
    buckets->billing_mode = table_def.billing_mode;
    
    double rcu = static_cast<double>(table_def.provisioned_throughput.read_capacity_units);
    double wcu = static_cast<double>(table_def.provisioned_throughput.write_capacity_units);
    
    if (buckets->billing_mode == core::BillingMode::PAY_PER_REQUEST) {
        rcu = 40000.0; // High default for on-demand
        wcu = 40000.0;
    }

    buckets->read_bucket = std::make_unique<TokenBucket>(rcu, 300.0);
    buckets->write_bucket = std::make_unique<TokenBucket>(wcu, 300.0);
    
    tables_[table_def.table_name] = std::move(buckets);
}

void CapacityManager::unregister_table(const std::string& table_name) {
    std::unique_lock lock(mutex_);
    tables_.erase(table_name);
}

std::expected<void, CapacityError> CapacityManager::consume_rcu(const std::string& table_name, double units) {
    std::shared_ptr<TableBuckets> buckets;
    {
        std::shared_lock lock(mutex_);
        auto it = tables_.find(table_name);
        if (it == tables_.end()) return std::unexpected(CapacityError::TableNotFound);
        buckets = it->second;
    }

    if (buckets->read_bucket->consume(units)) {
        return {};
    }
    return std::unexpected(CapacityError::ProvisionedThroughputExceeded);
}

std::expected<void, CapacityError> CapacityManager::consume_wcu(const std::string& table_name, double units) {
    std::shared_ptr<TableBuckets> buckets;
    {
        std::shared_lock lock(mutex_);
        auto it = tables_.find(table_name);
        if (it == tables_.end()) return std::unexpected(CapacityError::TableNotFound);
        buckets = it->second;
    }

    if (buckets->write_bucket->consume(units)) {
        return {};
    }
    return std::unexpected(CapacityError::ProvisionedThroughputExceeded);
}

double CapacityManager::calculate_rcu(size_t item_size_bytes, bool consistent, bool transactional) {
    // 1 RCU = 4KB for strong, 0.5 RCU for eventual (4KB)
    // Actually DDB is: up to 4KB = 1 RCU (strong) or 0.5 RCU (eventual)
    double base_units = std::ceil(static_cast<double>(item_size_bytes) / 4096.0);
    if (!consistent) base_units *= 0.5;
    if (transactional) base_units *= 2.0;
    return std::max(0.5, base_units);
}

double CapacityManager::calculate_wcu(size_t item_size_bytes, bool transactional) {
    // 1 WCU = 1KB
    double base_units = std::ceil(static_cast<double>(item_size_bytes) / 1024.0);
    if (transactional) base_units *= 2.0;
    return std::max(1.0, base_units);
}

} // namespace cynamodb::engine::capacity
