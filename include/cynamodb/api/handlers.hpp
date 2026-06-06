#pragma once

#include <string>
#include <string_view>

#include <cynamodb/api/dispatcher.hpp>
#include <cynamodb/engine/table_manager.hpp>
#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/engine/capacity/manager.hpp>
#include <cynamodb/streams/manager.hpp>
#include <cynamodb/backups/manager.hpp>

namespace cynamodb::api {

// Result of an API operation, ready to be turned into an HTTP response.
struct ApiResult {
    unsigned status = 200;     // HTTP status code
    std::string error_type;    // empty on success; e.g. "ResourceNotFoundException"
    std::string body;          // JSON response body
};

// Executes a single DynamoDB operation against the given table catalog and
// storage engine. `body` is the raw JSON request body. This is intentionally
// transport-agnostic so it can be unit-tested directly without a socket.
// `capacity` is optional: when non-null, single-table data operations consume
// read/write capacity and are throttled with ProvisionedThroughputExceededException
// once a provisioned table's token bucket is exhausted; table lifecycle ops keep the
// capacity manager's table registry in sync.
ApiResult handle_operation(engine::TableManager& tables, engine::StorageEngine& storage,
                           Operation op, std::string_view body,
                           engine::capacity::CapacityManager* capacity = nullptr,
                           streams::StreamManager* streams = nullptr,
                           backups::BackupManager* backups = nullptr);

}  // namespace cynamodb::api
