#pragma once

#include <cynamodb/engine/storage_engine.hpp>
#include <cynamodb/engine/lsm/memtable.hpp>
#include <cynamodb/engine/lsm/wal.hpp>
#include <cynamodb/engine/lsm/sstable.hpp>
#include <cynamodb/engine/lsm/manifest.hpp>
#include <cynamodb/engine/lsm/compaction.hpp>
#include <cynamodb/core/memory.hpp>
#include <thread>
#include <queue>
#include <deque>
#include <condition_variable>
#include <shared_mutex>
#include <atomic>

namespace cynamodb::engine::lsm {

class LsmEngine : public StorageEngine {
public:
    LsmEngine(const std::string& db_path, std::shared_ptr<core::Arena> arena, bool allow_gsi_replication = true);
    ~LsmEngine() override;

    void put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) override;
    void remove(const std::string& table_name, const std::string& key) override;
    std::optional<AttributeMap> get(const std::string& table_name, const std::string& key) override;
    MutationOutcome mutate(const std::string& table_name, const std::string& key, const Mutator& mutator) override;
    void drop_table(const std::string& table_name) override;

    ScanResult scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) override;
    QueryResult query(
        const std::string& table_name,
        const AttributeMap& key_conditions,
        const std::optional<std::string>& exclusive_start_key,
        size_t limit,
        const std::optional<std::string>& key_prefix = std::nullopt,
        bool scan_forward = true) override;

private:
    // Lock-free building blocks; the caller must already hold mutex_ exclusively.
    // Used by the public put/remove and by mutate() so a read-modify-write can
    // happen atomically under a single lock acquisition.
    std::optional<AttributeMap> get_locked(const std::string& internal_key) const;
    // put_locked/remove_locked append the record to the WAL (without fsync) and update
    // the memtable under mutex_, then return the WAL the record went to. The caller
    // commits that WAL *after releasing mutex_*, so the fsync no longer blocks readers.
    std::shared_ptr<WriteAheadLog> put_locked(const std::string& internal_key, const AttributeMap& attributes);
    std::shared_ptr<WriteAheadLog> remove_locked(const std::string& internal_key);
    void rotate_memtable_if_needed();

    void flush_memtable();
    void background_compaction();
    // Merges every on-disk SSTable into a single one (newest-wins, tombstones
    // purged), bounding read amplification and disk usage. Does its own locking in
    // phases: it snapshots the SSTable set under the lock, performs the merge + file
    // write UNLOCKED (like flush_memtable), then re-locks only to swap the manifest,
    // preserving any SSTables flushed concurrently. Must NOT be called with mutex_ held.
    bool compact();
    std::string get_table_path(const std::string& table_name) const;

    // --- durability / recovery ---
    std::string wal_path(uint64_t generation) const;
    void load_sstables_from_manifest();
    void recover_from_wal();
    void append_to_wal(const std::string& internal_key, bool is_delete, const AttributeMap& attributes);

    // An immutable (frozen) memtable awaiting flush, paired with the WAL segment
    // that durably holds its writes until the SSTable is written.
    struct ImmutableMemtable {
        std::shared_ptr<MemTable> table;
        std::string wal_path;
    };

    std::string db_path_;
    std::shared_ptr<core::Arena> arena_;
    std::unique_ptr<MemTable> memtable_;
    std::vector<ImmutableMemtable> immutable_memtables_;
    std::deque<std::shared_ptr<SSTable>> sstables_;
    uint64_t wal_generation_ = 0;
    uint64_t wal_seq_ = 0;
    
    std::shared_mutex mutex_;
    std::condition_variable_any flush_cv_;
    std::condition_variable_any compaction_cv_;
    std::atomic<bool> shutting_down_{false};
    // Edge-triggered signal for the compaction thread. Set by the flush thread
    // when an L0 compaction becomes due, cleared once handled, so the compaction
    // thread runs once per event instead of spinning on a level-triggered
    // predicate (compaction does not yet reduce the L0 file count).
    bool compaction_pending_ = false;
    std::thread flush_thread_;
    std::thread compaction_thread_;

    // shared_ptr so an in-flight writer can commit (fsync) the exact WAL its record
    // went to after releasing mutex_, even if the engine rotates to a new WAL meanwhile.
    std::shared_ptr<WriteAheadLog> wal_;
    std::shared_ptr<Manifest> manifest_;
    std::unique_ptr<CompactionManager> compaction_manager_;
    
    std::map<std::string, std::unique_ptr<LsmEngine>, core::StringViewLess> gsi_engines_;
    [[maybe_unused]] bool allow_gsi_replication_;

    std::map<std::string, std::shared_ptr<SSTable>, core::StringViewLess> sstable_cache_;
    size_t memtable_flush_threshold_ = 1000;
};

} // namespace cynamodb::engine::lsm
