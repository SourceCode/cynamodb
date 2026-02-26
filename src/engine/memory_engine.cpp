#include <cynamodb/engine/memory_engine.hpp>
#include <algorithm>
#include <cctype>

namespace cynamodb::engine {

void MemoryEngine::put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) {
    std::unique_lock lock(mutex_);
    data_[table_name][key] = attributes;
}

void MemoryEngine::remove(const std::string& table_name, const std::string& key) {
    (void)table_name; (void)key;
}

std::optional<StorageEngine::AttributeMap> MemoryEngine::get(const std::string& table_name, const std::string& key) {
    (void)table_name; (void)key;
    return std::nullopt;
}

MemoryEngine::ScanResult MemoryEngine::scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) {
    (void)table_name; (void)exclusive_start_key; (void)limit;
    return {};
}

MemoryEngine::QueryResult MemoryEngine::query(const std::string& table_name, const AttributeMap& key_conditions, const std::optional<std::string>& exclusive_start_key, size_t limit) {
    (void)table_name; (void)key_conditions; (void)exclusive_start_key; (void)limit;
    return {};
}

std::expected<void, StorageError> MemoryEngine::put_item(const core::TableDefinition& table_def, const Item& item) {
    (void)table_def; (void)item;
    return {};
}

std::expected<MemoryEngine::Item, StorageError> MemoryEngine::get_item(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key) {
    (void)table_def; (void)key;
    return std::unexpected(StorageError::ItemNotFound);
}

std::expected<void, StorageError> MemoryEngine::delete_item(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key) {
    (void)table_def; (void)key;
    return {};
}

std::string MemoryEngine::make_key(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key) {
    (void)table_def; (void)key;
    return "";
}

} // namespace cynamodb::engine
