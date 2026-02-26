#pragma once

#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <optional>
#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/core/schema.hpp>
#include <cynamodb/core/lock_free_queue.hpp>
#include <cynamodb/engine/lsm/lsm_engine.hpp>

namespace cynamodb::engine::lsm {

struct GSIUpdate {
    std::string table_name;
    std::string index_name;
    bool is_delete;
    StorageEngine::AttributeMap old_image;
    StorageEngine::AttributeMap new_image;
};

class GsiManager {
public:
    explicit GsiManager(std::shared_ptr<LsmEngine> base_engine);
    ~GsiManager();

    void create_gsi_storage(const std::string& table_name, const core::GlobalSecondaryIndex& gsi_def);
    void remove_gsi_storage(const std::string& table_name, const std::string& index_name);

    void queue_update(GSIUpdate update);

    std::optional<StorageEngine::AttributeMap> project_item(
        const StorageEngine::AttributeMap& item,
        const core::GlobalSecondaryIndex& gsi_def,
        const std::vector<core::KeySchemaElement>& base_key_schema);

private:
    void propagation_worker();

    std::shared_ptr<LsmEngine> base_engine_;
    std::map<std::string, std::unique_ptr<LsmEngine>, core::StringViewLess> gsi_engines_;
    
    core::LockFreeQueue<GSIUpdate, 4096> queue_;
    std::thread worker_thread_;
    std::atomic<bool> running_{true};
};

} // namespace cynamodb::engine::lsm
