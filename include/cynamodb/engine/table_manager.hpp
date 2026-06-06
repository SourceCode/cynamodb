#pragma once

#include <string>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <cynamodb/core/schema.hpp>
#include <expected>
#include <vector>

namespace cynamodb::engine {

enum class TableError {
    TableAlreadyExists,
    TableNotFound,
    ItemCollectionSizeLimitExceeded,
    InternalError
};

class TableManager {
public:
    explicit TableManager(const std::string& metadata_path);

    std::expected<core::TableDefinition, TableError> create_table(const core::TableDefinition& table_def);
    std::expected<core::TableDefinition, TableError> describe_table(std::string_view table_name);
    std::expected<core::TableDefinition, TableError> delete_table(std::string_view table_name);
    std::vector<std::string> list_tables();

    // Collection size tracking
    void update_collection_size(const std::string& table_name, const std::string& partition_key, int64_t size_delta);
    std::expected<void, TableError> check_collection_limit(const std::string& table_name, const std::string& partition_key, size_t new_item_size);

private:
    void save_metadata();
    void load_metadata();

    std::map<std::string, core::TableDefinition, core::StringViewLess> tables_;
    
    // table_name -> PartitionKey -> total_size
    std::map<std::string, std::map<std::string, uint64_t, core::StringViewLess>, core::StringViewLess> collection_sizes_;
    
    bool dirty_ = false;
    std::shared_mutex mutex_;
    std::string metadata_path_;
};

} // namespace cynamodb::engine
