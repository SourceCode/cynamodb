#include <cynamodb/streams/manager.hpp>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <random>

namespace cynamodb::streams {

namespace {

std::string generate_event_id() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    char buf[33];
    snprintf(buf, sizeof(buf), "%016llx%016llx", dist(rng), dist(rng));
    return std::string(buf);
}

std::string build_stream_arn(const std::string& table_name, uint64_t timestamp) {
    // arn:aws:dynamodb:region:account:table/TableName/stream/2023-10-27T12:00:00.000
    std::time_t t = static_cast<std::time_t>(timestamp);
    std::tm* tm = std::gmtime(&t);
    std::stringstream ss;
    ss << "arn:aws:dynamodb:ddblocal:000000000000:table/" << table_name << "/stream/";
    ss << std::put_time(tm, "%Y-%m-%dT%H:%M:%S.000");
    return ss.str();
}

} // namespace

StreamManager::StreamManager() {}

void StreamManager::sync_table(const core::TableDefinition& table_def) {
    std::unique_lock lock(mutex_);
    if (table_def.stream_specification && table_def.stream_specification->stream_enabled) {
        if (active_stream_arn_by_table_.find(table_def.table_name) == active_stream_arn_by_table_.end()) {
            uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
            std::string arn = build_stream_arn(table_def.table_name, now);
            
            StreamState state;
            state.description.stream_arn = arn;
            state.description.table_name = table_def.table_name;
            state.description.stream_status = "ACTIVE";
            state.description.stream_view_type = *table_def.stream_specification->stream_view_type;
            state.description.creation_request_date_time = now;
            
            Shard root_shard;
            root_shard.shard_id = "shardId-0000000000000001";
            root_shard.sequence_number_range.starting_sequence_number = "1";
            state.description.shards.push_back(root_shard);

            streams_by_arn_[arn] = std::move(state);
            active_stream_arn_by_table_[table_def.table_name] = arn;
        }
    } else {
        active_stream_arn_by_table_.erase(table_def.table_name);
    }
}

void StreamManager::remove_table(std::string_view table_name) {
    std::unique_lock lock(mutex_);
    active_stream_arn_by_table_.erase(std::string(table_name));
}

bool StreamManager::is_stream_enabled(const core::TableDefinition& table_def) const {
    return table_def.stream_specification && table_def.stream_specification->stream_enabled;
}

void StreamManager::append_record(
    const core::TableDefinition& table_def,
    std::string_view event_name,
    const engine::StorageEngine::AttributeMap& keys,
    const std::optional<engine::StorageEngine::AttributeMap>& old_image,
    const std::optional<engine::StorageEngine::AttributeMap>& new_image) {
    
    std::string arn;
    {
        std::shared_lock lock(mutex_);
        auto it = active_stream_arn_by_table_.find(table_def.table_name);
        if (it == active_stream_arn_by_table_.end()) return;
        arn = it->second;
    }

    StreamRecord record;
    record.event_id = generate_event_id();
    record.event_name = std::string(event_name);
    record.approximate_creation_date_time = std::chrono::duration_cast<std::chrono::seconds>(
                                               std::chrono::system_clock::now().time_since_epoch())
                                               .count();
    record.keys = keys;
    
    auto view_type = *table_def.stream_specification->stream_view_type;
    if (view_type == core::StreamViewType::NEW_IMAGE || view_type == core::StreamViewType::NEW_AND_OLD_IMAGES) {
        record.new_image = new_image;
    }
    if (view_type == core::StreamViewType::OLD_IMAGE || view_type == core::StreamViewType::NEW_AND_OLD_IMAGES) {
        record.old_image = old_image;
    }

    {
        std::shared_lock lock(mutex_);
        auto& state = streams_by_arn_[arn];
        std::lock_guard r_lock(*state.records_mutex);
        record.sequence_number = std::to_string(state.records.size() + 1);
        state.records.push_back(std::move(record));
        
        // 24 hour retention (Task 10) - simplified cleanup here
        while (!state.records.empty() && 
               (std::chrono::system_clock::now().time_since_epoch().count() - state.records.front().approximate_creation_date_time > 86400)) {
            state.records.pop_front();
        }
    }
}

std::expected<ListStreamsResult, StreamError> StreamManager::list_streams(
    const std::optional<std::string>& table_name,
    const std::optional<std::string>& exclusive_start_stream_arn,
    size_t limit) const {
    
    std::shared_lock lock(mutex_);
    ListStreamsResult result;
    
    for (const auto& [arn, state] : streams_by_arn_) {
        if (table_name && state.description.table_name != *table_name) continue;
        if (exclusive_start_stream_arn && arn <= *exclusive_start_stream_arn) continue;
        
        result.streams.push_back(state.description);
        if (result.streams.size() >= limit) {
            result.last_evaluated_stream_arn = arn;
            break;
        }
    }
    
    return result;
}

std::expected<StreamDescription, StreamError> StreamManager::describe_stream(
    std::string_view stream_arn,
    const std::optional<std::string>& exclusive_start_shard_id,
    size_t limit) const {
    
    std::shared_lock lock(mutex_);
    auto it = streams_by_arn_.find(std::string(stream_arn));
    if (it == streams_by_arn_.end()) return std::unexpected(StreamError::ResourceNotFound);
    
    StreamDescription desc = it->second.description;
    // Handle shard pagination if needed
    (void)exclusive_start_shard_id;
    (void)limit;
    
    return desc;
}

std::expected<std::string, StreamError> StreamManager::create_shard_iterator(
    std::string_view stream_arn,
    std::string_view shard_id,
    std::string_view iterator_type,
    const std::optional<std::string>& sequence_number) {
    
    std::unique_lock lock(mutex_);
    auto it = streams_by_arn_.find(std::string(stream_arn));
    if (it == streams_by_arn_.end()) return std::unexpected(StreamError::ResourceNotFound);

    std::string iterator_id = generate_event_id();
    IteratorState state;
    state.stream_arn = std::string(stream_arn);
    state.shard_id = std::string(shard_id);
    state.expires_at = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    
    if (iterator_type == "TRIM_HORIZON") {
        state.record_index = 0;
    } else if (iterator_type == "LATEST") {
        std::lock_guard r_lock(*it->second.records_mutex);
        state.record_index = it->second.records.size();
    } else if (iterator_type == "AT_SEQUENCE_NUMBER" && sequence_number) {
        // Linear search for now
        std::lock_guard r_lock(*it->second.records_mutex);
        size_t idx = 0;
        for (; idx < it->second.records.size(); ++idx) {
            if (it->second.records[idx].sequence_number == *sequence_number) break;
        }
        state.record_index = idx;
    } else {
        state.record_index = 0;
    }

    iterators_[iterator_id] = std::move(state);
    return iterator_id;
}

std::expected<GetRecordsResult, StreamError> StreamManager::get_records(
    std::string_view shard_iterator,
    size_t limit) {
    
    std::unique_lock lock(mutex_);
    auto it = iterators_.find(std::string(shard_iterator));
    if (it == iterators_.end()) return std::unexpected(StreamError::ExpiredIterator);
    
    if (std::chrono::steady_clock::now() > it->second.expires_at) {
        iterators_.erase(it);
        return std::unexpected(StreamError::ExpiredIterator);
    }

    auto stream_it = streams_by_arn_.find(it->second.stream_arn);
    if (stream_it == streams_by_arn_.end()) return std::unexpected(StreamError::InternalError);

    GetRecordsResult result;
    std::lock_guard r_lock(*stream_it->second.records_mutex);
    
    size_t start = it->second.record_index;
    size_t count = std::min(limit, stream_it->second.records.size() - start);
    
    for (size_t i = 0; i < count; ++i) {
        result.records.push_back(stream_it->second.records[start + i]);
    }
    
    it->second.record_index += count;
    // Next iterator is the same for simple implementation
    result.next_shard_iterator = std::string(shard_iterator);
    
    return result;
}

} // namespace cynamodb::streams
