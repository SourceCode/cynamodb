#include <cynamodb/engine/lsm/lsm_engine.hpp>
#include <cynamodb/engine/lsm/compactor.hpp>
#include <cynamodb/engine/lsm/compaction.hpp>
#include <cynamodb/engine/lsm/manifest.hpp>
#include <cynamodb/engine/lsm/record_codec.hpp>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#ifdef __GLIBC__
#include <malloc.h>
#endif
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace cynamodb::engine::lsm {

namespace {

// LSM levels share a single physical keyspace, so the table name must be folded
// into the stored key to keep tables isolated. Table names are restricted to
// [A-Za-z0-9_.-] by DynamoDB, so a 0x00 separator is unambiguous and makes every
// key of a table a contiguous lexicographic range [table"\0", table"\1").
std::string internal_key(const std::string& table, const std::string& key) {
    std::string out;
    out.reserve(table.size() + 1 + key.size());
    out.append(table);
    out.push_back('\0');
    out.append(key);
    return out;
}

std::string table_prefix(const std::string& table) {
    std::string p = table;
    p.push_back('\0');
    return p;
}

// True if the key is present in this SSTable's sorted index.
bool sstable_contains(SSTable& sstable, const std::string& key) {
    return sstable.contains(key);
}

bool attribute_equals(const core::AttributeValue& a, const core::AttributeValue& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case core::AttributeType::S:
        case core::AttributeType::N:
            return std::get<core::String>(a.value) == std::get<core::String>(b.value);
        case core::AttributeType::BOOL:
            return std::get<bool>(a.value) == std::get<bool>(b.value);
        case core::AttributeType::NUL:
            return true;
        default:
            return false;
    }
}

bool item_matches(const StorageEngine::AttributeMap& item,
                  const StorageEngine::AttributeMap& conditions) {
    for (const auto& [name, expected] : conditions) {
        if (!expected) return false;
        auto it = item.find(name);
        if (it == item.end() || !it->second || !attribute_equals(*it->second, *expected)) {
            return false;
        }
    }
    return true;
}

bool auto_compaction_enabled() {
    const char* enabled = std::getenv("CYNAMODB_ENABLE_AUTO_COMPACTION");
    if (!enabled) return true;
    const std::string_view flag(enabled);
    return flag != "0" && flag != "false" && flag != "off";
}

void trim_free_heap_after_compaction() {
#ifdef __GLIBC__
    malloc_trim(0);
#endif
}

using SnapshotMap = std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess>;

// K-way merge over mutable snapshots and immutable SSTable indexes. Only one key per
// source is retained in the heap, so memory is O(number of files + active memtables),
// independent of the number or size of records in the requested range.
class MergedReader {
public:
    struct Row {
        std::string key;
        StorageEngine::AttributeMap attributes;
    };

    MergedReader(std::vector<SnapshotMap> memory_snapshots,
                 const std::deque<std::shared_ptr<SSTable>>& sstables,
                 std::string prefix,
                 std::optional<std::string> exclusive_start,
                 bool scan_forward = true)
        : prefix_(std::move(prefix)), scan_forward_(scan_forward),
          heap_(NodeOrder{scan_forward}) {
        const std::optional<std::string> upper = prefix_successor(prefix_);
        sources_.reserve(memory_snapshots.size() + sstables.size());
        size_t rank = 0;
        for (auto& snapshot : memory_snapshots) {
            Source source;
            source.rank = rank++;
            source.memory = std::make_shared<SnapshotMap>(std::move(snapshot));
            if (scan_forward_) {
                source.memory_it = exclusive_start ? source.memory->upper_bound(*exclusive_start)
                                                   : source.memory->lower_bound(prefix_);
                source.memory_valid = source.memory_it != source.memory->end();
            } else {
                auto after = exclusive_start ? source.memory->lower_bound(*exclusive_start)
                                             : (upper ? source.memory->lower_bound(*upper)
                                                      : source.memory->end());
                if (after != source.memory->begin()) {
                    source.memory_it = std::prev(after);
                    source.memory_valid = true;
                }
            }
            sources_.push_back(std::move(source));
            enqueue(sources_.size() - 1);
        }
        for (const auto& sstable : sstables) {
            Source source;
            source.rank = rank++;
            source.sstable = sstable;
            if (scan_forward_) {
                source.sstable_position = exclusive_start
                    ? sstable->upper_bound_index(*exclusive_start)
                    : sstable->lower_bound_index(prefix_);
                source.sstable_valid = source.sstable_position < sstable->entry_count();
            } else {
                const size_t after = exclusive_start
                    ? sstable->lower_bound_index(*exclusive_start)
                    : (upper ? sstable->lower_bound_index(*upper) : sstable->entry_count());
                if (after > 0) {
                    source.sstable_position = after - 1;
                    source.sstable_valid = true;
                }
            }
            sources_.push_back(std::move(source));
            enqueue(sources_.size() - 1);
        }
    }

    std::optional<Row> next() {
        while (!heap_.empty()) {
            const std::string key = heap_.top().key;
            std::vector<Node> same_key;
            do {
                same_key.push_back(heap_.top());
                heap_.pop();
            } while (!heap_.empty() && heap_.top().key == key);

            auto winner = std::min_element(same_key.begin(), same_key.end(),
                [](const Node& a, const Node& b) { return a.rank < b.rank; });
            auto entry = read_current(sources_[winner->source]);
            for (const Node& node : same_key) {
                advance(node.source);
            }
            if (!entry) {
                throw std::runtime_error("failed to decode indexed SSTable record for merged read");
            }
            if (entry->is_deleted) continue;
            return Row{key, std::move(entry->attributes)};
        }
        return std::nullopt;
    }

private:
    struct Source {
        size_t rank = 0;
        std::shared_ptr<SnapshotMap> memory;
        SnapshotMap::const_iterator memory_it;
        bool memory_valid = false;
        std::shared_ptr<SSTable> sstable;
        size_t sstable_position = 0;
        bool sstable_valid = false;
    };

    struct Node {
        std::string key;
        size_t source = 0;
        size_t rank = 0;
    };

    struct NodeOrder {
        bool scan_forward = true;
        bool operator()(const Node& left, const Node& right) const {
            if (left.key != right.key) {
                return scan_forward ? left.key > right.key : left.key < right.key;
            }
            return left.rank > right.rank;
        }
    };

    static std::optional<std::string> prefix_successor(std::string prefix) {
        for (size_t i = prefix.size(); i > 0; --i) {
            const auto byte = static_cast<unsigned char>(prefix[i - 1]);
            if (byte == 0xFF) continue;
            prefix[i - 1] = static_cast<char>(byte + 1);
            prefix.resize(i);
            return prefix;
        }
        return std::nullopt;
    }

    bool in_range(std::string_view key) const {
        return key.size() >= prefix_.size() && key.substr(0, prefix_.size()) == prefix_;
    }

    void enqueue(size_t source_index) {
        Source& source = sources_[source_index];
        std::string_view key;
        if (source.memory) {
            if (!source.memory_valid) return;
            key = source.memory_it->first;
        } else {
            if (!source.sstable_valid) return;
            key = source.sstable->index_entry(source.sstable_position).key;
        }
        if (!in_range(key)) return;
        heap_.push(Node{std::string(key), source_index, source.rank});
    }

    std::optional<Skiplist::SnapshotEntry> read_current(const Source& source) const {
        if (source.memory) return source.memory_it->second;
        return source.sstable->read_entry(source.sstable_position);
    }

    void advance(size_t source_index) {
        Source& source = sources_[source_index];
        if (source.memory) {
            if (scan_forward_) {
                ++source.memory_it;
                source.memory_valid = source.memory_it != source.memory->end();
            } else if (source.memory_it == source.memory->begin()) {
                source.memory_valid = false;
            } else {
                --source.memory_it;
            }
        } else {
            if (scan_forward_) {
                ++source.sstable_position;
                source.sstable_valid = source.sstable_position < source.sstable->entry_count();
            } else if (source.sstable_position == 0) {
                source.sstable_valid = false;
            } else {
                --source.sstable_position;
            }
        }
        enqueue(source_index);
    }

    std::string prefix_;
    bool scan_forward_ = true;
    std::vector<Source> sources_;
    std::priority_queue<Node, std::vector<Node>, NodeOrder> heap_;
};

unsigned size_tier(uint64_t bytes) {
    constexpr uint64_t kBaseSize = 1024ULL * 1024ULL;
    const uint64_t units = std::max<uint64_t>(1, (bytes + kBaseSize - 1) / kBaseSize);
    return static_cast<unsigned>((std::bit_width(units) - 1) / 2); // powers of four
}

}  // namespace

LsmEngine::LsmEngine(const std::string& db_path, std::shared_ptr<core::Arena> arena, bool allow_gsi_replication)
    : db_path_(db_path), arena_(arena), allow_gsi_replication_(allow_gsi_replication) {
    std::filesystem::create_directories(db_path_);
    
    manifest_ = std::make_shared<Manifest>(db_path_);
    if (!manifest_->load()) {
        throw std::runtime_error("failed to load a complete, valid manifest from " + db_path_);
    }
    
    compaction_manager_ = std::make_unique<CompactionManager>(db_path_, manifest_);

    memtable_ = std::make_unique<MemTable>();

    // Recover durable state before accepting writes: first reopen the SSTables the
    // manifest knows about, then replay any write-ahead-log segments left behind by
    // a previous run (unflushed memtable data).
    load_sstables_from_manifest();
    recover_from_wal();

    flush_thread_ = std::thread(&LsmEngine::flush_memtable, this);
    compaction_thread_ = std::thread(&LsmEngine::background_compaction, this);
}

std::string LsmEngine::wal_path(uint64_t generation) const {
    return db_path_ + "/wal_" + std::to_string(generation) + ".log";
}

void LsmEngine::load_sstables_from_manifest() {
    // Gather every SSTable the manifest references across all levels and order them
    // newest-first (highest sequence number) to preserve read precedence.
    std::vector<SSTableMetadata> all;
    for (uint32_t level = 0; level < 16; ++level) {
        for (const auto& meta : manifest_->get_level_files(level)) {
            all.push_back(meta);
        }
    }
    std::sort(all.begin(), all.end(), [](const SSTableMetadata& a, const SSTableMetadata& b) {
        return a.sequence_number > b.sequence_number;
    });
    std::set<std::string> referenced_paths;
    for (const auto& meta : all) {
        const std::string normalized = std::filesystem::path(meta.path).lexically_normal().string();
        referenced_paths.insert(normalized);
        if (!std::filesystem::exists(meta.path)) {
            throw std::runtime_error("manifest references missing SSTable: " + meta.path);
        }
        sstables_.push_back(std::make_shared<SSTable>(meta.path));
    }

    // A crash can leave a completed output between atomic SST publication and manifest
    // publication. Once every manifest reference has opened successfully, unreferenced
    // .sst files are unreachable by definition and can be removed without touching data.
    std::error_code iteration_error;
    for (const auto& entry : std::filesystem::directory_iterator(db_path_, iteration_error)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sst") continue;
        const std::string normalized = entry.path().lexically_normal().string();
        if (referenced_paths.contains(normalized)) continue;
        std::error_code remove_error;
        if (!std::filesystem::remove(entry.path(), remove_error) && remove_error) {
            std::cerr << "Failed to remove orphan SSTable " << entry.path() << ": "
                      << remove_error.message() << std::endl;
        }
    }
    if (iteration_error) {
        throw std::runtime_error("failed to inspect SSTable directory: " + iteration_error.message());
    }
}

void LsmEngine::recover_from_wal() {
    // Collect WAL segments (wal_<gen>.log) in generation order.
    std::vector<std::pair<uint64_t, std::string>> segments;
    if (std::filesystem::exists(db_path_)) {
        for (const auto& entry : std::filesystem::directory_iterator(db_path_)) {
            const std::string name = entry.path().filename().string();
            if (name.rfind("wal_", 0) == 0 && entry.path().extension() == ".log") {
                const std::string num = name.substr(4, name.size() - 4 - 4);  // strip "wal_" and ".log"
                try {
                    segments.emplace_back(std::stoull(num), entry.path().string());
                } catch (const std::exception&) {
                    // Not a recognizable segment name; ignore.
                }
            }
        }
    }
    std::sort(segments.begin(), segments.end());

    // Replay oldest-to-newest so later writes overwrite earlier ones in the memtable.
    uint64_t max_gen = 0;
    bool any = false;
    for (const auto& [gen, path] : segments) {
        any = true;
        max_gen = std::max(max_gen, gen);
        WriteAheadLog segment(path);
        for (const auto& rec : segment.replay()) {
            if (rec.value.empty()) continue;
            const char op = rec.value.front();
            if (op == 'D') {
                memtable_->remove(rec.key);
            } else if (op == 'P') {
                auto attrs = decode_attributes(std::string_view(rec.value).substr(1));
                if (attrs) memtable_->put(rec.key, *attrs);
            }
        }
    }

    // Open a fresh active WAL one generation past anything seen, then checkpoint the
    // recovered memtable into it so the old segments can be safely discarded. After
    // this, exactly one WAL segment exists and fully describes the live memtable.
    wal_generation_ = any ? max_gen + 1 : 0;
    wal_ = std::make_shared<WriteAheadLog>(wal_path(wal_generation_));
    for (const auto& [k, entry] : memtable_->get_all_entries()) {
        std::string value;
        if (entry.is_deleted) {
            value.push_back('D');
        } else {
            value.push_back('P');
            value += encode_attributes(entry.attributes);
        }
        wal_->append(wal_seq_++, k, value);
    }
    wal_->sync();
    for (const auto& [gen, path] : segments) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}

void LsmEngine::append_to_wal(const std::string& internal_key, bool is_delete, const AttributeMap& attributes) {
    if (!wal_) return;
    std::string value;
    if (is_delete) {
        value.push_back('D');
    } else {
        value.push_back('P');
        value += encode_attributes(attributes);
    }
    // Write the record but do NOT fsync here — the caller commits the WAL after
    // releasing the engine lock so the fsync doesn't stall concurrent reads/writes.
    wal_->append_only(wal_seq_++, internal_key, value);
}

LsmEngine::~LsmEngine() {
    // Set the shutdown flag while holding the mutex. The flush/compaction threads
    // evaluate their condition-variable predicates under this same mutex, so doing
    // the store unlocked here races: a worker can read shutting_down_ == false,
    // decide to block, and only then have us flip the flag and notify -- a lost
    // wakeup that hangs the join forever (readily reproduced under ASan's slower
    // timing). Holding the lock around the store closes that window.
    {
        std::unique_lock lock(mutex_);
        shutting_down_ = true;
    }
    flush_cv_.notify_all();
    compaction_cv_.notify_all();
    if (flush_thread_.joinable()) flush_thread_.join();
    if (compaction_thread_.joinable()) compaction_thread_.join();
    manifest_->save();
}

std::shared_ptr<WriteAheadLog> LsmEngine::put_locked(const std::string& ik, const AttributeMap& attributes) {
    auto target_wal = wal_;  // the WAL this record is appended to
    append_to_wal(ik, false, attributes);
    memtable_->put(ik, attributes);
    rotate_memtable_if_needed();
    return target_wal;
}

std::shared_ptr<WriteAheadLog> LsmEngine::remove_locked(const std::string& ik) {
    auto target_wal = wal_;
    append_to_wal(ik, true, {});
    memtable_->remove(ik);
    rotate_memtable_if_needed();
    return target_wal;
}

void LsmEngine::rotate_memtable_if_needed() {
    if (memtable_->size() < memtable_flush_threshold_) return;
    // Freeze the memtable together with its WAL segment, then start a fresh
    // generation. Deletes must take this path too; otherwise a delete-heavy workload
    // leaves an unbounded tombstone memtable that is never flushed.
    wal_->sync();
    immutable_memtables_.push_back({std::move(memtable_), wal_path(wal_generation_)});
    memtable_ = std::make_unique<MemTable>();
    ++wal_generation_;
    wal_ = std::make_shared<WriteAheadLog>(wal_path(wal_generation_));
    flush_cv_.notify_one();
}

std::optional<StorageEngine::AttributeMap> LsmEngine::get_locked(const std::string& ik) const {
    // Read newest level to oldest. The first level that knows about the key is
    // authoritative: a live value is returned, a tombstone stops the search and
    // returns "not found" rather than falling through to an older, stale value.
    if (auto val = memtable_->get(ik)) return val;
    if (memtable_->is_tombstoned(ik)) return std::nullopt;

    for (auto it = immutable_memtables_.rbegin(); it != immutable_memtables_.rend(); ++it) {
        if (auto val = it->table->get(ik)) return val;
        if (it->table->is_tombstoned(ik)) return std::nullopt;
    }

    for (const auto& sstable : sstables_) {  // sstables_ is ordered newest-first
        if (auto val = sstable->get(ik)) return val;
        // get() returns nullopt for tombstones too; if the key is in this
        // table's index it is a tombstone and shadows older tables.
        if (sstable_contains(*sstable, ik)) return std::nullopt;
    }

    return std::nullopt;
}

void LsmEngine::put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) {
    std::shared_ptr<WriteAheadLog> committed_wal;
    {
        std::unique_lock lock(mutex_);
        committed_wal = put_locked(internal_key(table_name, key), attributes);
    }
    // fsync outside the engine lock (group-committed). The client is not acknowledged
    // until this returns, so an acknowledged write is still durable.
    if (committed_wal) committed_wal->commit();
}

void LsmEngine::remove(const std::string& table_name, const std::string& key) {
    std::shared_ptr<WriteAheadLog> committed_wal;
    {
        std::unique_lock lock(mutex_);
        committed_wal = remove_locked(internal_key(table_name, key));
    }
    if (committed_wal) committed_wal->commit();
}

std::optional<StorageEngine::AttributeMap> LsmEngine::get(const std::string& table_name, const std::string& key) {
    std::shared_lock lock(mutex_);
    return get_locked(internal_key(table_name, key));
}

LsmEngine::MutationOutcome LsmEngine::mutate(const std::string& table_name, const std::string& key, const Mutator& mutator) {
    MutationOutcome outcome;
    std::shared_ptr<WriteAheadLog> committed_wal;
    {
        std::unique_lock lock(mutex_);
        const std::string ik = internal_key(table_name, key);
        auto current = get_locked(ik);
        if (current) outcome.previous = current;

        Mutation m = mutator(current ? &*current : nullptr);
        switch (m.kind) {
            case MutationKind::Put:
                committed_wal = put_locked(ik, m.attributes);
                outcome.applied = true;
                break;
            case MutationKind::Delete:
                committed_wal = remove_locked(ik);
                outcome.applied = true;
                break;
            case MutationKind::None:
                break;
        }
    }
    if (committed_wal) committed_wal->commit();  // durable before returning to the caller
    return outcome;
}

void LsmEngine::drop_table(const std::string& table_name) {
    const std::string prefix = table_prefix(table_name);
    std::optional<std::string> cursor;
    while (true) {
        std::vector<std::string> keys;
        {
            std::shared_lock lock(mutex_);
            std::vector<SnapshotMap> snapshots;
            snapshots.push_back(memtable_->get_all_entries());
            for (auto it = immutable_memtables_.rbegin(); it != immutable_memtables_.rend(); ++it) {
                snapshots.push_back(it->table->get_all_entries());
            }
            MergedReader reader(std::move(snapshots), sstables_, prefix, cursor);
            while (keys.size() < memtable_flush_threshold_) {
                auto row = reader.next();
                if (!row) break;
                keys.push_back(std::move(row->key));
            }
        }
        if (keys.empty()) break;
        cursor = keys.back();

        std::shared_ptr<WriteAheadLog> committed_wal;
        {
            std::unique_lock lock(mutex_);
            for (const auto& key : keys) committed_wal = remove_locked(key);
        }
        if (committed_wal) committed_wal->commit();
    }
}

LsmEngine::ScanResult LsmEngine::scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) {
    ScanResult result;
    std::shared_lock lock(mutex_);
    const std::string prefix = table_prefix(table_name);
    std::vector<SnapshotMap> snapshots;
    snapshots.push_back(memtable_->get_all_entries());
    for (auto it = immutable_memtables_.rbegin(); it != immutable_memtables_.rend(); ++it) {
        snapshots.push_back(it->table->get_all_entries());
    }
    const std::optional<std::string> start = exclusive_start_key
        ? std::optional<std::string>(prefix + *exclusive_start_key)
        : std::nullopt;
    MergedReader reader(std::move(snapshots), sstables_, prefix, start);
    std::string last_user_key;
    size_t page_bytes = 0;
    while (auto row = reader.next()) {
        const size_t item_bytes = item_size_bytes(row->attributes);
        if (!result.items.empty() &&
            ((limit != 0 && result.items.size() == limit) ||
             item_bytes > kMaxReadPageBytes - std::min(page_bytes, kMaxReadPageBytes))) {
            result.last_evaluated_key = last_user_key;
            break;
        }
        last_user_key = row->key.substr(prefix.size());
        page_bytes = core::size_saturating_add(page_bytes, item_bytes);
        result.items.push_back(std::move(row->attributes));
    }
    return result;
}

LsmEngine::QueryResult LsmEngine::query(
    const std::string& table_name,
    const AttributeMap& key_conditions,
    const std::optional<std::string>& exclusive_start_key,
    size_t limit,
    const std::optional<std::string>& key_prefix,
    bool scan_forward) {
    QueryResult result;
    std::shared_lock lock(mutex_);
    const std::string prefix = table_prefix(table_name);
    const std::string query_prefix = key_prefix ? prefix + *key_prefix : prefix;
    std::vector<SnapshotMap> snapshots;
    snapshots.push_back(memtable_->get_all_entries());
    for (auto it = immutable_memtables_.rbegin(); it != immutable_memtables_.rend(); ++it) {
        snapshots.push_back(it->table->get_all_entries());
    }
    const std::optional<std::string> start = exclusive_start_key
        ? std::optional<std::string>(prefix + *exclusive_start_key)
        : std::nullopt;
    MergedReader reader(std::move(snapshots), sstables_, query_prefix, start, scan_forward);
    std::string last_user_key;
    size_t page_bytes = 0;
    while (auto row = reader.next()) {
        if (!item_matches(row->attributes, key_conditions)) continue;
        const size_t item_bytes = item_size_bytes(row->attributes);
        if (!result.items.empty() &&
            ((limit != 0 && result.items.size() == limit) ||
             item_bytes > kMaxReadPageBytes - std::min(page_bytes, kMaxReadPageBytes))) {
            result.last_evaluated_key = last_user_key;
            break;
        }
        result.items.push_back(std::move(row->attributes));
        page_bytes = core::size_saturating_add(page_bytes, item_bytes);
        last_user_key = row->key.substr(prefix.size());
    }
    return result;
}

void LsmEngine::flush_memtable() {
    while (!shutting_down_) {
        std::shared_ptr<MemTable> to_flush;
        std::string wal_to_delete;
        {
            std::unique_lock lock(mutex_);
            flush_cv_.wait(lock, [this] { return shutting_down_ || !immutable_memtables_.empty(); });
            if (shutting_down_ && immutable_memtables_.empty()) break;
            to_flush = immutable_memtables_.front().table;
            wal_to_delete = immutable_memtables_.front().wal_path;
        }

        uint64_t seq = 0;
        {
            // Allocate the sequence number under the lock so it can never collide
            // with one the compaction thread allocates concurrently.
            std::unique_lock lock(mutex_);
            seq = manifest_->get_next_sequence();
            manifest_->set_next_sequence(seq + 1);
        }

        std::string sst_path = db_path_ + "/table_" + std::to_string(seq) + ".sst";
        auto entries = to_flush->get_all_entries();
        if (SSTable::create(sst_path, entries).empty()) {
            std::cerr << "Flush failed while writing " << sst_path << "; retaining WAL for retry"
                      << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        auto flushed_sstable = std::make_shared<SSTable>(sst_path);

        SSTableMetadata meta;
        meta.path = sst_path;
        meta.level = 0;
        meta.sequence_number = seq;
        if (!entries.empty()) {
            meta.min_key = entries.begin()->first;
            meta.max_key = entries.rbegin()->first;
        }

        {
            // Publish the new SSTable to the manifest and the in-memory list under
            // the lock, so flush and compaction never interleave their manifest edits.
            std::unique_lock lock(mutex_);
            manifest_->add_file(0, meta);
            if (!manifest_->save()) {
                manifest_->remove_file(0, sst_path);
                std::error_code remove_error;
                std::filesystem::remove(sst_path, remove_error);
                std::cerr << "Flush failed while publishing manifest; retaining WAL for retry"
                          << std::endl;
                continue;
            }
            sstables_.push_front(std::move(flushed_sstable));
            immutable_memtables_.erase(immutable_memtables_.begin());
            if (auto_compaction_enabled() && sstables_.size() >= 4) {
                compaction_pending_ = true;
                compaction_cv_.notify_one();
            }
        }

        // The data is now durable in the SSTable; its WAL segment can be removed.
        if (!wal_to_delete.empty()) {
            std::error_code ec;
            std::filesystem::remove(wal_to_delete, ec);
        }
    }
}

void LsmEngine::background_compaction() {
    while (!shutting_down_) {
        {
            std::unique_lock lock(mutex_);
            // Edge-triggered: only run when the flush thread flags work, so a no-op
            // pass can't spin the predicate true forever.
            compaction_cv_.wait(lock, [this] { return shutting_down_ || compaction_pending_; });
            if (shutting_down_) break;
            compaction_pending_ = false;
        }
        // Compact WITHOUT holding the lock: compact() re-acquires it only to snapshot
        // and to publish. The heavy merge + SSTable write runs unlocked so readers and
        // writers are not frozen for the duration (the old path held mutex_ across the
        // whole merge + disk write, stalling everything at scale).
        try {
            bool compacted = false;
            while (!shutting_down_ && compact()) compacted = true;
            if (compacted) trim_free_heap_after_compaction();
        } catch (const std::exception& error) {
            std::cerr << "Compaction failed: " << error.what() << std::endl;
        }
    }
}

bool LsmEngine::compact() {
    // Select four similarly-sized files. Size-tiered batches prevent repeatedly
    // rewriting a multi-gigabyte bottom table whenever a few 1 MB memtables flush.
    constexpr size_t kCompactionFanout = 4;
    std::deque<std::shared_ptr<SSTable>> to_merge;
    std::map<std::string, std::pair<uint32_t, uint64_t>> manifest_files;
    std::map<std::string, SSTableMetadata> manifest_metadata;
    uint64_t physical_sequence = 0;
    bool complete_snapshot = false;
    {
        std::unique_lock lock(mutex_);
        if (sstables_.size() < kCompactionFanout) return false;
        for (uint32_t level = 0; level < 16; ++level) {
            for (const auto& m : manifest_->get_level_files(level)) {
                manifest_files[m.path] = {level, m.sequence_number};
                manifest_metadata[m.path] = m;
            }
        }

        // Only merge a contiguous sequence run. Combining non-adjacent files and
        // assigning the output their highest sequence would incorrectly promote an
        // old value above an unselected newer file that sat between the inputs.
        std::vector<std::shared_ptr<SSTable>> run;
        std::optional<unsigned> run_tier;
        for (auto it = sstables_.rbegin(); it != sstables_.rend(); ++it) {
            const unsigned tier = size_tier((*it)->file_size());
            if (!run_tier || *run_tier != tier) {
                run.clear();
                run_tier = tier;
            }
            run.push_back(*it); // oldest to newest
            if (run.size() == kCompactionFanout) {
                for (auto candidate = run.rbegin(); candidate != run.rend(); ++candidate) {
                    to_merge.push_back(*candidate); // newest to oldest
                }
                break;
            }
        }
        if (to_merge.empty()) return false;
        complete_snapshot = to_merge.size() == sstables_.size();
        physical_sequence = manifest_->get_next_sequence();
        manifest_->set_next_sequence(physical_sequence + 1);
    }

    uint64_t logical_sequence = 0;
    std::vector<std::string> old_paths;
    old_paths.reserve(to_merge.size());
    for (const auto& sstable : to_merge) {
        old_paths.push_back(sstable->path());
        logical_sequence = std::max(logical_sequence, manifest_files.at(sstable->path()).second);
    }

    const std::string merged_path =
        db_path_ + "/table_" + std::to_string(physical_sequence) + "_c.sst";
    SSTableWriter writer(merged_path);

    struct Cursor {
        std::string_view key;
        size_t source = 0;
        size_t position = 0;
    };
    struct CursorOrder {
        bool operator()(const Cursor& left, const Cursor& right) const {
            if (left.key != right.key) return left.key > right.key;
            return left.source > right.source;
        }
    };
    std::priority_queue<Cursor, std::vector<Cursor>, CursorOrder> heap;
    for (size_t source = 0; source < to_merge.size(); ++source) {
        if (to_merge[source]->entry_count() > 0) {
            heap.push(Cursor{to_merge[source]->index_entry(0).key, source, 0});
        }
    }

    while (!heap.empty()) {
        const std::string key(heap.top().key);
        std::vector<Cursor> same_key;
        do {
            same_key.push_back(heap.top());
            heap.pop();
        } while (!heap.empty() && heap.top().key == key);

        const auto winner = std::min_element(same_key.begin(), same_key.end(),
            [](const Cursor& left, const Cursor& right) { return left.source < right.source; });
        auto entry = to_merge[winner->source]->read_entry(winner->position);
        if (!entry) throw std::runtime_error("corrupt SSTable record during compaction");
        if (!entry->is_deleted || !complete_snapshot) {
            if (!writer.append(key, *entry)) {
                throw std::runtime_error("failed to stream compacted SSTable entry");
            }
        }

        for (const Cursor& cursor : same_key) {
            const size_t next = cursor.position + 1;
            if (next < to_merge[cursor.source]->entry_count()) {
                heap.push(Cursor{to_merge[cursor.source]->index_entry(next).key,
                                 cursor.source, next});
            }
        }
    }
    if (!writer.finish()) throw std::runtime_error("failed to finalize compacted SSTable");
    auto merged_sstable = std::make_shared<SSTable>(merged_path);

    {
        std::unique_lock lock(mutex_);
        for (const auto& path : old_paths) {
            const auto& ref = manifest_files.at(path);
            manifest_->remove_file(ref.first, path);
        }
        SSTableMetadata meta;
        meta.path = merged_path;
        meta.level = 1;
        meta.sequence_number = logical_sequence;
        meta.min_key = writer.min_key();
        meta.max_key = writer.max_key();
        meta.file_size = std::filesystem::file_size(merged_path);
        meta.entry_count = writer.entry_count();
        manifest_->add_file(1, meta);
        if (!manifest_->save()) {
            manifest_->remove_file(1, merged_path);
            for (const auto& path : old_paths) {
                const auto& original = manifest_metadata.at(path);
                manifest_->add_file(original.level, original);
            }
            (void)manifest_->save();
            std::error_code remove_error;
            std::filesystem::remove(merged_path, remove_error);
            throw std::runtime_error("failed to atomically publish compaction manifest");
        }

        const std::set<std::string> merged_set(old_paths.begin(), old_paths.end());
        std::deque<std::shared_ptr<SSTable>> kept;
        bool inserted = false;
        for (const auto& current : sstables_) {
            if (merged_set.count(current->path())) {
                if (!inserted) {
                    kept.push_back(merged_sstable);
                    inserted = true;
                }
                continue;
            }
            kept.push_back(current);
        }
        if (!inserted) kept.push_back(std::move(merged_sstable));
        sstables_ = std::move(kept);
    }

    // Delete the now-obsolete files from disk (outside the lock).
    for (const auto& p : old_paths) {
        std::error_code ec;
        if (!std::filesystem::remove(p, ec) && ec) {
            std::cerr << "Failed to remove compacted SSTable " << p << ": "
                      << ec.message() << std::endl;
        }
    }
    return true;
}

std::string LsmEngine::get_table_path(const std::string& table_name) const {
    return db_path_ + "/" + table_name;
}

} // namespace cynamodb::engine::lsm
