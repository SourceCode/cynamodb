#pragma once

#include <string>
#include <vector>
#include <memory>
#include <map>
#include <optional>
#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/core/schema.hpp>
#include <cynamodb/engine/lsm/lsm_engine.hpp>

namespace cynamodb::engine::lsm {

struct LSIUpdate {
    std::string table_name;
    std::string index_name;
    StorageEngine::AttributeMap old_image;
    StorageEngine::AttributeMap new_image;
};

class LsiManager {
public:
    explicit LsiManager(std::shared_ptr<LsmEngine> storage_engine);

    void update_indexes(
        const core::TableDefinition& table_def,
        const std::string& base_key,
        const std::optional<StorageEngine::AttributeMap>& old_item,
        const std::optional<StorageEngine::AttributeMap>& new_item);

    std::optional<StorageEngine::AttributeMap> project_lsi_item(
        const StorageEngine::AttributeMap& item,
        const core::LocalSecondaryIndex& lsi_def,
        const std::vector<core::KeySchemaElement>& base_key_schema);

    static std::string make_lsi_key(
        uint8_t index_id,
        const std::string& partition_key,
        const std::string& lsi_sort_key,
        const std::string& base_sort_key);

private:
    std::shared_ptr<LsmEngine> storage_engine_;
};

} // namespace cynamodb::engine::lsm
