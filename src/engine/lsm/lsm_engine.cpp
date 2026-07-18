#include <cynamodb/engine/lsm/lsm_engine.hpp>
#include <cynamodb/engine/lsm/compactor.hpp>
#include <cynamodb/engine/lsm/compaction.hpp>
#include <cynamodb/engine/lsm/manifest.hpp>
#include <cynamodb/engine/lsm/record_codec.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string_view>
#include <variant>
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
    const auto& idx = sstable.index();
    auto it = std::lower_bound(idx.begin(), idx.end(), key,
        [](const SSTable::IndexEntry& a, const std::string& b) { return a.key < b; });
    return it != idx.end() && it->key == key;
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
    if (!enabled) return false;
    const std::string_view flag(enabled);
    return flag == "1" || flag == "true" || flag == "on";
}

}  // namespace

LsmEngine::LsmEngine(const std::string& db_path, std::shared_ptr<core::Arena> arena, bool allow_gsi_replication)
    : db_path_(db_path), arena_(arena), allow_gsi_replication_(allow_gsi_replication) {
    std::filesystem::create_directories(db_path_);
    
    manifest_ = std::make_shared<Manifest>(db_path_);
    manifest_->load();
    
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
    for (const auto& meta : all) {
        if (std::filesystem::exists(meta.path)) {
            sstables_.push_back(std::make_shared<SSTable>(meta.path));
        }
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
    if (memtable_->size() >= memtable_flush_threshold_) {
        // Freeze the memtable together with its WAL segment, then start a fresh
        // memtable and WAL generation. sync() here makes the frozen segment (including
        // the record just appended) durable before it is abandoned for a new WAL, so
        // the caller's later commit(target_wal) is a cheap, already-synced no-op.
        wal_->sync();
        immutable_memtables_.push_back({std::move(memtable_), wal_path(wal_generation_)});
        memtable_ = std::make_unique<MemTable>();
        ++wal_generation_;
        wal_ = std::make_shared<WriteAheadLog>(wal_path(wal_generation_));
        flush_cv_.notify_one();
    }
    return target_wal;
}

std::shared_ptr<WriteAheadLog> LsmEngine::remove_locked(const std::string& ik) {
    auto target_wal = wal_;
    append_to_wal(ik, true, {});
    memtable_->remove(ik);
    return target_wal;
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
    std::shared_ptr<WriteAheadLog> committed_wal;
    {
        std::unique_lock lock(mutex_);
        // The LSM keeps all tables in one keyspace, so dropping a table means
        // tombstoning every key under its prefix. Compaction later reclaims the space.
        const std::string prefix = table_prefix(table_name);
        auto live = materialize(prefix);
        for (auto it = live.lower_bound(prefix); it != live.end(); ++it) {
            if (it->first.compare(0, prefix.size(), prefix) != 0) break;
            committed_wal = remove_locked(it->first);  // all removes share one WAL
        }
    }
    if (committed_wal) committed_wal->commit();  // one group-commit for the whole drop
}

// Builds a merged, tombstone-resolved, sorted view by reading every level newest-first
// and keeping the first decision seen for each key. When `prefix` is set the work is
// bounded to that key range: memtable entries outside it are skipped, and — critically
// — only in-range SSTable keys are DECODED (sstable->get is the expensive step). This
// keeps a Scan/Query proportional to the queried table, not the whole store.
std::map<std::string, StorageEngine::AttributeMap, core::StringViewLess> LsmEngine::materialize(std::string_view prefix) const {
    std::map<std::string, AttributeMap, core::StringViewLess> live;
    std::set<std::string> decided;

    auto in_prefix = [&](const std::string& k) {
        return prefix.empty() ||
               (k.size() >= prefix.size() && k.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0);
    };

    auto consider = [&](const std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess>& snapshot) {
        for (const auto& [k, e] : snapshot) {
            if (!in_prefix(k)) continue;              // outside the requested table
            if (!decided.insert(k).second) continue;  // newer level already decided
            if (!e.is_deleted) live.emplace(k, e.attributes);
        }
    };

    consider(memtable_->get_all_entries());
    for (auto it = immutable_memtables_.rbegin(); it != immutable_memtables_.rend(); ++it) {
        consider(it->table->get_all_entries());
    }
    for (const auto& sstable : sstables_) {  // newest-first
        const auto& idx = sstable->index();
        // index() is key-sorted; jump to the prefix range so out-of-table keys are
        // never decoded (they used to be — that was the O(store)-per-query blowup).
        auto begin = prefix.empty()
            ? idx.begin()
            : std::lower_bound(idx.begin(), idx.end(), prefix,
                  [](const SSTable::IndexEntry& e, std::string_view p) { return std::string_view(e.key) < p; });
        for (auto e = begin; e != idx.end(); ++e) {
            if (!in_prefix(e->key)) break;             // sorted: past the prefix range
            if (!decided.insert(e->key).second) continue;
            if (auto v = sstable->get(e->key)) {
                live.emplace(e->key, *v);
            }
            // else: tombstone; shadow older levels by leaving it out.
        }
    }
    return live;
}

LsmEngine::ScanResult LsmEngine::scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) {
    ScanResult result;
    std::shared_lock lock(mutex_);
    const std::string prefix = table_prefix(table_name);
    auto live = materialize(prefix);

    auto it = exclusive_start_key ? live.upper_bound(prefix + *exclusive_start_key)
                                   : live.lower_bound(prefix);
    for (; it != live.end(); ++it) {
        if (it->first.compare(0, prefix.size(), prefix) != 0) break;  // left this table
        if (limit != 0 && result.items.size() == limit) {
            // Report the user-facing (table-stripped) key as the cursor.
            result.last_evaluated_key = std::prev(it)->first.substr(prefix.size());
            return result;
        }
        result.items.push_back(it->second);
    }
    return result;
}

LsmEngine::QueryResult LsmEngine::query(
    const std::string& table_name,
    const AttributeMap& key_conditions,
    const std::optional<std::string>& exclusive_start_key,
    size_t limit,
    const std::optional<std::string>& key_prefix) {
    QueryResult result;
    std::shared_lock lock(mutex_);
    const std::string prefix = table_prefix(table_name);
    const std::string query_prefix = key_prefix ? prefix + *key_prefix : prefix;
    auto live = materialize(query_prefix);

    auto it = exclusive_start_key ? live.upper_bound(prefix + *exclusive_start_key)
                                   : live.lower_bound(query_prefix);
    std::string last_user_key;
    for (; it != live.end(); ++it) {
        if (it->first.compare(0, query_prefix.size(), query_prefix) != 0) break;  // left this query range
        if (!item_matches(it->second, key_conditions)) continue;
        if (limit != 0 && result.items.size() == limit) {
            result.last_evaluated_key = last_user_key;
            return result;
        }
        result.items.push_back(it->second);
        last_user_key = it->first.substr(prefix.size());
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
        SSTable::create(sst_path, entries);  // slow file I/O, done without the lock

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
            manifest_->save();
            sstables_.push_front(std::make_shared<SSTable>(sst_path));
            immutable_memtables_.erase(immutable_memtables_.begin());
            if (auto_compaction_enabled() && compaction_manager_->should_compact_L0()) {
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
        compact();
    }
}

void LsmEngine::compact() {
    // --- Phase 1: snapshot the SSTable set + manifest refs under the lock. ---
    std::deque<std::shared_ptr<SSTable>> to_merge;
    std::map<std::string, std::pair<uint32_t, uint64_t>> manifest_files;
    {
        std::unique_lock lock(mutex_);
        if (sstables_.size() < 2) return;  // nothing to gain from merging one file
        to_merge = sstables_;              // copy of shared_ptrs (newest-first)
        for (uint32_t level = 0; level < 16; ++level) {
            for (const auto& m : manifest_->get_level_files(level)) {
                manifest_files[m.path] = {level, m.sequence_number};
            }
        }
    }

    // --- Phase 2: merge + write the result, UNLOCKED (the expensive part). ---
    // Merge newest-first: the first version seen of a key wins, and a tombstone (key
    // in the index but get() returns nothing) drops the key entirely. Safe because we
    // merge the complete snapshot, so no older version of a dropped key survives below.
    std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess> merged;
    std::set<std::string> seen;
    uint64_t merged_seq = 0;
    std::vector<std::string> old_paths;
    for (const auto& sstable : to_merge) {  // newest-first
        old_paths.push_back(sstable->path());
        auto mit = manifest_files.find(sstable->path());
        if (mit != manifest_files.end()) merged_seq = std::max(merged_seq, mit->second.second);
        for (const auto& idx : sstable->index()) {
            if (!seen.insert(idx.key).second) continue;
            if (auto v = sstable->get(idx.key)) {
                merged.emplace(idx.key, Skiplist::SnapshotEntry{*v, false});
            }
        }
    }

    // The merged file inherits the highest sequence number it replaces, so any SSTable
    // flushed after this compaction still sorts as newer on restart.
    const std::string merged_path =
        db_path_ + "/table_" + std::to_string(merged_seq) + "_c.sst";
    SSTable::create(merged_path, merged);  // slow file I/O, done without the lock

    // --- Phase 3: publish under the lock, preserving concurrently-flushed SSTables. ---
    {
        std::unique_lock lock(mutex_);
        // Drop only the files we actually merged; flush only ever ADDS files, so any
        // entry not in our snapshot was flushed during the merge and must survive.
        for (const auto& [path, ref] : manifest_files) {
            manifest_->remove_file(ref.first, path);
        }
        SSTableMetadata meta;
        meta.path = merged_path;
        meta.level = 1;
        meta.sequence_number = merged_seq;
        if (!merged.empty()) {
            meta.min_key = merged.begin()->first;
            meta.max_key = merged.rbegin()->first;
        }
        manifest_->add_file(1, meta);
        manifest_->save();

        // Rebuild the in-memory list: keep SSTables flushed during the unlocked merge
        // (newer than merged_seq, so they stay at the front, newest-first), then append
        // the merged result as the oldest level.
        const std::set<std::string> merged_set(old_paths.begin(), old_paths.end());
        std::deque<std::shared_ptr<SSTable>> kept;
        for (const auto& s : sstables_) {
            if (!merged_set.count(s->path())) kept.push_back(s);
        }
        kept.push_back(std::make_shared<SSTable>(merged_path));
        sstables_ = std::move(kept);
    }

    // Delete the now-obsolete files from disk (outside the lock).
    for (const auto& p : old_paths) {
        std::error_code ec;
        std::filesystem::remove(p, ec);
    }
}

std::string LsmEngine::get_table_path(const std::string& table_name) const {
    return db_path_ + "/" + table_name;
}

} // namespace cynamodb::engine::lsm
