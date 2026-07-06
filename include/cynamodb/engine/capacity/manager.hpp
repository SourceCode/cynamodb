#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <expected>
#include <cynamodb/core/schema.hpp>

namespace cynamodb::engine::capacity {

enum class CapacityError {
    ProvisionedThroughputExceeded,
    TableNotFound,
    InternalError
};

class TokenBucket {
public:
    TokenBucket(double rate_per_sec, double burst_seconds);
    
    bool consume(double units);
    void refill();
    void update_rate(double new_rate_per_sec);

private:
    std::atomic<int64_t> tokens_{0}; // Fixed-point: units * 1000
    int64_t max_tokens_;
    int64_t refill_rate_per_ms_; // Fixed-point: units * 1000 / ms
    std::chrono::steady_clock::time_point last_refill_;
    std::mutex refill_mutex_;
};

class CapacityManager {
public:
    void register_table(const core::TableDefinition& table_def);
    void unregister_table(const std::string& table_name);
    void update_table(const core::TableDefinition& table_def);

    std::expected<void, CapacityError> consume_rcu(const std::string& table_name, double units);
    std::expected<void, CapacityError> consume_wcu(const std::string& table_name, double units);

    static double calculate_rcu(size_t item_size_bytes, bool consistent, bool transactional = false);
    static double calculate_wcu(size_t item_size_bytes, bool transactional = false);

private:
    struct TableBuckets {
        std::unique_ptr<TokenBucket> read_bucket;
        std::unique_ptr<TokenBucket> write_bucket;
        core::BillingMode billing_mode;
    };

    std::map<std::string, std::shared_ptr<TableBuckets>, core::StringViewLess> tables_;
    // Shared: consume_rcu/wcu only look up + copy a shared_ptr on the per-request hot
    // path (parallel-safe); register/unregister take it exclusively.
    mutable std::shared_mutex mutex_;
};

} // namespace cynamodb::engine::capacity
