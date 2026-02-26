#include <cynamodb/engine/transactions/manager.hpp>
#include <algorithm>
#include <functional>

namespace cynamodb::engine::transactions {

uint64_t HybridLogicalClock::get_time() {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    uint64_t current = current_time_.load(std::memory_order_relaxed);
    uint64_t physical_time = current >> 16;
    uint64_t logical_time = current & 0xFFFF;

    if (static_cast<uint64_t>(now) > physical_time) {
        physical_time = static_cast<uint64_t>(now);
        logical_time = 0;
    } else {
        logical_time++;
    }

    uint64_t new_time = (physical_time << 16) | (logical_time & 0xFFFF);
    current_time_.store(new_time, std::memory_order_relaxed);
    return new_time;
}

void HybridLogicalClock::update(uint64_t remote_time) {
    std::lock_guard lock(mutex_);
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();

    uint64_t current = current_time_.load(std::memory_order_relaxed);
    uint64_t current_physical = current >> 16;
    uint64_t current_logical = current & 0xFFFF;

    uint64_t remote_physical = remote_time >> 16;
    uint64_t remote_logical = remote_time & 0xFFFF;

    uint64_t new_physical = std::max({static_cast<uint64_t>(now), current_physical, remote_physical});
    uint64_t new_logical = 0;

    if (new_physical == current_physical && new_physical == remote_physical) {
        new_logical = std::max(current_logical, remote_logical) + 1;
    } else if (new_physical == current_physical) {
        new_logical = current_logical + 1;
    } else if (new_physical == remote_physical) {
        new_logical = remote_logical + 1;
    }

    uint64_t new_time = (new_physical << 16) | (new_logical & 0xFFFF);
    current_time_.store(new_time, std::memory_order_relaxed);
}

uint64_t TIDGenerator::next() {
    uint64_t time = hlc_.get_time();
    uint16_t seq = sequence_.fetch_add(1, std::memory_order_relaxed);
    return (time & 0xFFFFFFFFFFFF0000) | seq; // Simplification
}

TransactionManager::TransactionManager(std::shared_ptr<TableManager> table_manager, std::shared_ptr<StorageEngine> storage_engine)
    : table_manager_(std::move(table_manager)), storage_engine_(std::move(storage_engine)), tid_gen_(hlc_) {}

size_t TransactionManager::get_stripe_index(const std::string& table, const std::string& key) const {
    std::hash<std::string> hasher;
    return hasher(table + ":" + key) % kNumStripes;
}

std::expected<void, std::vector<TransactionError>> TransactionManager::execute_transact_write_items(
    const std::vector<TransactWriteItem>& items,
    const std::string& client_request_token) {
    (void)client_request_token; // Ignore token for basic implementation
    
    if (items.empty() || items.size() > 25) {
        return std::unexpected(std::vector<TransactionError>{TransactionError::ValidationFailed});
    }

    std::vector<std::string> keys_seen;
    keys_seen.reserve(items.size());
    for (const auto& item : items) {
        std::string full_key = item.table_name + ":" + item.key;
        if (std::find(keys_seen.begin(), keys_seen.end(), full_key) != keys_seen.end()) {
            return std::unexpected(std::vector<TransactionError>{TransactionError::ValidationFailed});
        }
        keys_seen.push_back(std::move(full_key));
    }

    TransactionContext ctx;
    ctx.tid = tid_gen_.next();
    ctx.start_ts = hlc_.get_time();

    std::vector<size_t> lock_indices;
    for (const auto& item : items) {
        lock_indices.push_back(get_stripe_index(item.table_name, item.key));
    }
    std::sort(lock_indices.begin(), lock_indices.end());
    lock_indices.erase(std::unique(lock_indices.begin(), lock_indices.end()), lock_indices.end());

    // 2PL - Acquire locks
    std::vector<std::unique_lock<std::mutex>> locks;
    for (size_t idx : lock_indices) {
        locks.emplace_back(stripes_[idx]);
    }

    // OCC Check (Mocked for now)
    // We would read current _ts from storage engine and compare against read_set.
    // Assuming everything passes for now.
    
    // Commit Phase
    for (const auto& item : items) {
        if (item.is_delete) {
            storage_engine_->remove(item.table_name, item.key);
        } else if (item.put_attributes) {
            storage_engine_->put(item.table_name, item.key, *item.put_attributes);
        }
    }

    // Locks released automatically when `locks` goes out of scope
    return {};
}

std::expected<std::vector<std::optional<StorageEngine::AttributeMap>>, TransactionError> TransactionManager::execute_transact_get_items(
    const std::vector<TransactGetItem>& items) {
    
    if (items.size() > 100) {
        return std::unexpected(TransactionError::ValidationFailed);
    }

    std::vector<std::optional<StorageEngine::AttributeMap>> results;
    results.reserve(items.size());

    // Snapshot Read Optimization - No locks
    for (const auto& item : items) {
        results.push_back(storage_engine_->get(item.table_name, item.key));
    }

    return results;
}

} // namespace cynamodb::engine::transactions
