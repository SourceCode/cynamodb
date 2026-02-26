#include <cynamodb/engine/table_manager.hpp>
#include <cynamodb/utils/crc32.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace cynamodb::engine {

TableManager::TableManager(const std::string& metadata_path) : metadata_path_(metadata_path) {
    load_metadata();
}

std::expected<core::TableDefinition, TableError> TableManager::create_table(const core::TableDefinition& table_def) {
    std::unique_lock lock(mutex_);
    if (tables_.contains(table_def.table_name)) {
        return std::unexpected(TableError::TableAlreadyExists);
    }
    tables_[table_def.table_name] = table_def;
    dirty_ = true;
    save_metadata();
    return table_def;
}

std::expected<core::TableDefinition, TableError> TableManager::describe_table(std::string_view table_name) {
    std::shared_lock lock(mutex_);
    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        return std::unexpected(TableError::TableNotFound);
    }
    return it->second;
}

std::vector<std::string> TableManager::list_tables() {
    std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : tables_) {
        names.push_back(name);
    }
    return names;
}

void TableManager::update_collection_size(const std::string& table_name, const std::string& partition_key, int64_t size_delta) {
    std::unique_lock lock(mutex_);
    auto& table_collections = collection_sizes_[table_name];
    if (size_delta < 0 && static_cast<uint64_t>(-size_delta) > table_collections[partition_key]) {
        table_collections[partition_key] = 0;
    } else {
        table_collections[partition_key] += size_delta;
    }
}

std::expected<void, TableError> TableManager::check_collection_limit(const std::string& table_name, const std::string& partition_key, size_t new_item_size) {
    std::shared_lock lock(mutex_);
    auto table_it = tables_.find(table_name);
    if (table_it == tables_.end()) return std::unexpected(TableError::TableNotFound);
    
    // Limits only apply if LSI exists
    if (table_it->second.local_secondary_indexes.empty()) return {};

    auto it = collection_sizes_.find(table_name);
    if (it != collection_sizes_.end()) {
        auto coll_it = it->second.find(partition_key);
        if (coll_it != it->second.end()) {
            constexpr uint64_t kMaxCollectionSize = 10ULL * 1024 * 1024 * 1024; // 10GB
            if (coll_it->second + new_item_size > kMaxCollectionSize) {
                return std::unexpected(TableError::ItemCollectionSizeLimitExceeded);
            }
        }
    }
    return {};
}

void TableManager::load_metadata() {}
void TableManager::save_metadata() {}

} // namespace cynamodb::engine
