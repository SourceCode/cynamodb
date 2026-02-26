#include <cynamodb/engine/lsm/lsi_manager.hpp>
#include <algorithm>

namespace cynamodb::engine::lsm {

LsiManager::LsiManager(std::shared_ptr<LsmEngine> storage_engine)
    : storage_engine_(std::move(storage_engine)) {}

void LsiManager::update_indexes(
    const core::TableDefinition& table_def,
    [[maybe_unused]] const std::string& base_key,
    const std::optional<StorageEngine::AttributeMap>& old_item,
    const std::optional<StorageEngine::AttributeMap>& new_item) {
    
    for (size_t i = 0; i < table_def.local_secondary_indexes.size(); ++i) {
        const auto& lsi = table_def.local_secondary_indexes[i];
        [[maybe_unused]] uint8_t index_id = static_cast<uint8_t>(i + 1);

        // If old item exists, remove it from LSI
        if (old_item) {
            auto projected_old = project_lsi_item(*old_item, lsi, table_def.key_schema);
            if (projected_old) {
                // Placeholder
            }
        }

        // If new item exists, add it to LSI
        if (new_item) {
            auto projected_new = project_lsi_item(*new_item, lsi, table_def.key_schema);
            if (projected_new) {
                // Placeholder
            }
        }
    }
}

std::optional<StorageEngine::AttributeMap> LsiManager::project_lsi_item(
    const StorageEngine::AttributeMap& item,
    const core::LocalSecondaryIndex& lsi_def,
    const std::vector<core::KeySchemaElement>& base_key_schema) {
    
    if (lsi_def.key_schema.size() < 2) return std::nullopt;
    auto range_it = item.find(lsi_def.key_schema[1].attribute_name);
    if (range_it == item.end()) return std::nullopt;

    StorageEngine::AttributeMap projected;
    
    if (lsi_def.projection.projection_type == core::ProjectionType::ALL) {
        projected = item;
    } else {
        for (const auto& ks : base_key_schema) {
            auto it = item.find(ks.attribute_name);
            if (it != item.end()) projected[it->first] = it->second;
        }
        for (const auto& ks : lsi_def.key_schema) {
            auto it = item.find(ks.attribute_name);
            if (it != item.end()) projected[it->first] = it->second;
        }

        if (lsi_def.projection.projection_type == core::ProjectionType::INCLUDE) {
            for (const auto& attr : lsi_def.projection.non_key_attributes) {
                auto it = item.find(attr);
                if (it != item.end()) projected[it->first] = it->second;
            }
        }
    }

    return projected;
}

std::string LsiManager::make_lsi_key(
    uint8_t index_id,
    const std::string& partition_key,
    const std::string& lsi_sort_key,
    const std::string& base_sort_key) {
    
    std::string out;
    out.push_back(static_cast<char>(index_id));
    
    uint16_t pk_len = static_cast<uint16_t>(partition_key.size());
    out.append(reinterpret_cast<const char*>(&pk_len), sizeof(pk_len));
    out.append(partition_key);
    
    uint16_t lsi_sk_len = static_cast<uint16_t>(lsi_sort_key.size());
    out.append(reinterpret_cast<const char*>(&lsi_sk_len), sizeof(lsi_sk_len));
    out.append(lsi_sort_key);
    
    out.append(base_sort_key);
    return out;
}

} // namespace cynamodb::engine::lsm
