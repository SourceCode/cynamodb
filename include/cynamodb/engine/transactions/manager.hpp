#pragma once

#include <cynamodb/engine/transactions/context.hpp>
#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <atomic>
#include <chrono>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <expected>
#include <optional>
#include <stdexcept>

namespace cynamodb::engine::transactions {

class HybridLogicalClock {
public:
    uint64_t get_time();
    void update(uint64_t remote_time);
private:
    std::atomic<uint64_t> current_time_{0};
    std::mutex mutex_;
};

class TIDGenerator {
public:
    explicit TIDGenerator(HybridLogicalClock& hlc) : hlc_(hlc) {}
    uint64_t next();
private:
    HybridLogicalClock& hlc_;
    std::atomic<uint16_t> sequence_{0};
};

enum class TransactionError {
    ValidationFailed,
    ConditionalCheckFailed,
    TransactionConflict,
    ProvisionedThroughputExceeded,
    ResourceNotFound,
    InternalError
};

struct TransactWriteItem {
    std::string table_name;
    std::string key;
    std::optional<StorageEngine::AttributeMap> put_attributes; // null if Delete/Update/ConditionCheck
    bool is_delete = false;
    // Condition check, update expressions, etc. are simplified for now
};

struct TransactGetItem {
    std::string table_name;
    std::string key;
};

class TransactionManager {
public:
    TransactionManager(std::shared_ptr<TableManager> table_manager, std::shared_ptr<StorageEngine> storage_engine);

    std::expected<void, std::vector<TransactionError>> execute_transact_write_items(
        const std::vector<TransactWriteItem>& items,
        const std::string& client_request_token = "");

    std::expected<std::vector<std::optional<StorageEngine::AttributeMap>>, TransactionError> execute_transact_get_items(
        const std::vector<TransactGetItem>& items);

private:
    std::shared_ptr<TableManager> table_manager_;
    std::shared_ptr<StorageEngine> storage_engine_;
    HybridLogicalClock hlc_;
    TIDGenerator tid_gen_;

    // Simplistic striped lock for 2PL
    static constexpr size_t kNumStripes = 64;
    std::mutex stripes_[kNumStripes];

    size_t get_stripe_index(const std::string& table, const std::string& key) const;
};

} // namespace cynamodb::engine::transactions
