#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <cynamodb/core/schema.hpp>
#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/streams/shard.hpp>

namespace cynamodb::streams {

enum class StreamError {
    ResourceNotFound,
    ExpiredIterator,
    LimitExceeded,
    InternalError
};

struct StreamRecord {
    std::string event_id;
    std::string event_name;
    uint64_t approximate_creation_date_time;
    engine::StorageEngine::AttributeMap keys;
    std::optional<engine::StorageEngine::AttributeMap> old_image;
    std::optional<engine::StorageEngine::AttributeMap> new_image;
    std::string sequence_number;
    uint64_t size_bytes = 0;
};

struct StreamDescription {
    std::string stream_arn;
    std::string stream_label;
    std::string stream_status;
    core::StreamViewType stream_view_type;
    uint64_t creation_request_date_time;
    std::string table_name;
    std::vector<Shard> shards;
    std::optional<std::string> last_evaluated_shard_id;
};

struct ListStreamsResult {
    std::vector<StreamDescription> streams;
    std::optional<std::string> last_evaluated_stream_arn;
};

struct GetRecordsResult {
    std::vector<StreamRecord> records;
    std::optional<std::string> next_shard_iterator;
};

class StreamManager {
public:
    StreamManager();

    void sync_table(const core::TableDefinition& table_def);
    void remove_table(std::string_view table_name);

    void append_record(
        const core::TableDefinition& table_def,
        std::string_view event_name,
        const engine::StorageEngine::AttributeMap& keys,
        const std::optional<engine::StorageEngine::AttributeMap>& old_image,
        const std::optional<engine::StorageEngine::AttributeMap>& new_image);

    std::expected<ListStreamsResult, StreamError> list_streams(
        const std::optional<std::string>& table_name,
        const std::optional<std::string>& exclusive_start_stream_arn,
        size_t limit) const;

    std::expected<StreamDescription, StreamError> describe_stream(
        std::string_view stream_arn,
        const std::optional<std::string>& exclusive_start_shard_id,
        size_t limit) const;

    std::expected<std::string, StreamError> create_shard_iterator(
        std::string_view stream_arn,
        std::string_view shard_id,
        std::string_view iterator_type,
        const std::optional<std::string>& sequence_number);

    std::expected<GetRecordsResult, StreamError> get_records(
        std::string_view shard_iterator,
        size_t limit);

    bool is_stream_enabled(const core::TableDefinition& table_def) const;

private:
    struct StreamState {
        StreamDescription description;
        std::deque<StreamRecord> records;
        std::unique_ptr<std::mutex> records_mutex;

        StreamState() : records_mutex(std::make_unique<std::mutex>()) {}
        StreamState(StreamState&&) noexcept = default;
        StreamState& operator=(StreamState&&) noexcept = default;
    };

    struct IteratorState {
        std::string stream_arn;
        std::string shard_id;
        size_t record_index;
        std::chrono::steady_clock::time_point expires_at;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, StreamState> streams_by_arn_;
    std::unordered_map<std::string, std::string> active_stream_arn_by_table_;
    std::unordered_map<std::string, IteratorState> iterators_;
};

} // namespace cynamodb::streams
