#pragma once

#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/core/schema.hpp>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#include <expected>

namespace cynamodb::engine {

enum class StorageError {
    TableNotFound,
    ConditionalCheckFailed,
    ItemNotFound,
    InternalError
};

class MemoryEngine : public StorageEngine {
public:
    void put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) override;
    void remove(const std::string& table_name, const std::string& key) override;
    std::optional<AttributeMap> get(const std::string& table_name, const std::string& key) override;
    
    ScanResult scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) override;
    QueryResult query(const std::string& table_name, const AttributeMap& key_conditions, const std::optional<std::string>& exclusive_start_key, size_t limit) override;

    // Legacy methods
    std::expected<void, StorageError> put_item(const core::TableDefinition& table_def, const Item& item);
    std::expected<Item, StorageError> get_item(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key);
    std::expected<void, StorageError> delete_item(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key);

private:
    std::string make_key(const core::TableDefinition& table_def, const std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>& key);
    
    std::map<std::string, std::map<std::string, Item, core::StringViewLess>> data_;
    mutable std::shared_mutex mutex_;
};

} // namespace cynamodb::engine
