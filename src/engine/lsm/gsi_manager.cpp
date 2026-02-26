#include <cynamodb/engine/lsm/gsi_manager.hpp>
#include <chrono>

namespace cynamodb::engine::lsm {

GsiManager::GsiManager(std::shared_ptr<LsmEngine> base_engine) : base_engine_(base_engine) {
    worker_thread_ = std::thread(&GsiManager::propagation_worker, this);
}

GsiManager::~GsiManager() {
    running_ = false;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void GsiManager::create_gsi_storage(const std::string& table_name, const core::GlobalSecondaryIndex& gsi_def) {
    // Placeholder: Actually create engine and store it in gsi_engines_
    (void)table_name;
    (void)gsi_def;
}

void GsiManager::remove_gsi_storage(const std::string& table_name, const std::string& index_name) {
    // Placeholder
    (void)table_name;
    (void)index_name;
}

void GsiManager::queue_update(GSIUpdate update) {
    while (!queue_.enqueue(std::move(update))) {
        std::this_thread::yield();
    }
}

std::optional<StorageEngine::AttributeMap> GsiManager::project_item(
    const StorageEngine::AttributeMap& item,
    const core::GlobalSecondaryIndex& gsi_def,
    const std::vector<core::KeySchemaElement>& base_key_schema) {
    
    // Check if GSI hash key exists
    if (gsi_def.key_schema.empty()) return std::nullopt;
    auto hash_it = item.find(gsi_def.key_schema[0].attribute_name);
    if (hash_it == item.end()) return std::nullopt; // Sparse index
    
    // Check if GSI range key exists (if defined)
    if (gsi_def.key_schema.size() > 1) {
        auto range_it = item.find(gsi_def.key_schema[1].attribute_name);
        if (range_it == item.end()) return std::nullopt;
    }

    StorageEngine::AttributeMap projected;
    
    if (gsi_def.projection.projection_type == core::ProjectionType::ALL) {
        projected = item;
    } else {
        // ALWAYS include base table keys and GSI keys
        for (const auto& ks : base_key_schema) {
            auto it = item.find(ks.attribute_name);
            if (it != item.end()) projected[it->first] = it->second;
        }
        for (const auto& ks : gsi_def.key_schema) {
            auto it = item.find(ks.attribute_name);
            if (it != item.end()) projected[it->first] = it->second;
        }

        if (gsi_def.projection.projection_type == core::ProjectionType::INCLUDE) {
            for (const auto& attr : gsi_def.projection.non_key_attributes) {
                auto it = item.find(attr);
                if (it != item.end()) projected[it->first] = it->second;
            }
        }
    }

    return projected;
}

void GsiManager::propagation_worker() {
    while (running_) {
        auto update = queue_.dequeue();
        if (update) {
            // Apply update to GSI storage
            // In a real implementation we would:
            // 1. Project the new_image to see if it should be inserted
            // 2. Project the old_image to see if it should be deleted
            // 3. Issue remove() and put() calls to gsi_engines_[update->index_name]
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

} // namespace cynamodb::engine::lsm
