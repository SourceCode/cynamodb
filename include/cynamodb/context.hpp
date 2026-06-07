/**
 * @file context.hpp
 * @brief Core execution context for cynamoDB.
 * @version 2.4.1
 */

#pragma once
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <string>
#include <string_view>
#include <system_error>
#include <cynamodb/engine/table_manager.hpp>
#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/engine/lsm/lsm_engine.hpp>
#include <cynamodb/engine/capacity/manager.hpp>
#include <cynamodb/streams/manager.hpp>
#include <cynamodb/backups/manager.hpp>
#include <cynamodb/auth/credential_store.hpp>
#include <memory_resource>
namespace cynamodb {

/**
 * @class RequestContext
 * @brief Manages per-request memory via a monotonic buffer arena.
 */
struct RequestContext {
    std::byte stack_buf[16384];
    std::pmr::monotonic_buffer_resource arena;
    RequestContext() : arena(stack_buf, sizeof(stack_buf), std::pmr::null_memory_resource()) {}
    ~RequestContext() { arena.release(); }
    RequestContext(const RequestContext&) = delete;
    RequestContext& operator=(const RequestContext&) = delete;
};
struct Context {
    std::shared_ptr<engine::TableManager> table_manager;
    std::shared_ptr<engine::StorageEngine> storage_engine;
    std::shared_ptr<streams::StreamManager> stream_manager;
    std::shared_ptr<backups::BackupManager> backup_manager;
    std::shared_ptr<auth::CredentialStore> credential_store;
    std::shared_ptr<engine::capacity::CapacityManager> capacity_manager;
    std::mutex transaction_mutex;
    // Opt-in SigV4 enforcement. When true, requests without a parseable
    // AWS4-HMAC-SHA256 Authorization header are rejected. Enabled by setting
    // CYNAMODB_REQUIRE_AUTH to a value other than "0"/"false"/"off"/"" so auth-
    // required code paths can be exercised locally.
    bool require_auth = false;
    Context() {
        if (const char* env = std::getenv("CYNAMODB_REQUIRE_AUTH"); env != nullptr) {
            std::string_view v(env);
            require_auth = !(v.empty() || v == "0" || v == "false" || v == "off");
        }
        std::string data_dir = "./data";
        if (const char* env = std::getenv("CYNAMODB_DATA_DIR"); env != nullptr && env[0] != '\0') data_dir = env;
        std::filesystem::path data_path = std::filesystem::path(data_dir).lexically_normal();
        std::error_code ec;
        std::filesystem::create_directories(data_path, ec);
        data_dir = data_path.string();
        table_manager = std::make_shared<engine::TableManager>(data_dir + "/metadata.bin");
        auto arena = std::make_shared<core::Arena>();
        storage_engine = std::make_shared<engine::lsm::LsmEngine>(data_dir, arena);
        stream_manager = std::make_shared<streams::StreamManager>();
        backup_manager = std::make_shared<backups::BackupManager>(data_dir + "/backups");
        credential_store = std::make_shared<auth::CredentialStore>();
        capacity_manager = std::make_shared<engine::capacity::CapacityManager>();
        for (const auto& table_name : table_manager->list_tables()) {
            auto table_def = table_manager->describe_table(table_name);
            if (table_def) {
                stream_manager->sync_table(*table_def);
                capacity_manager->register_table(*table_def);
            }
        }
    }
};
}
