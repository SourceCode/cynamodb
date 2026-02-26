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
    
    ScanResult scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) override;
    QueryResult query(const std::string& table_name, const AttributeMap& key_conditions, const std::optional<std::string>& exclusive_start_key, size_t limit) override;

private:
    void flush_memtable();
    void background_compaction();
    std::string get_table_path(const std::string& table_name) const;

    std::string db_path_;
    std::shared_ptr<core::Arena> arena_;
    std::unique_ptr<MemTable> memtable_;
    std::vector<std::shared_ptr<MemTable>> immutable_memtables_;
    std::deque<std::shared_ptr<SSTable>> sstables_;
    
    std::shared_mutex mutex_;
    std::condition_variable_any flush_cv_;
    std::condition_variable_any compaction_cv_;
    std::atomic<bool> shutting_down_{false};
    std::thread flush_thread_;
    std::thread compaction_thread_;

    std::unique_ptr<WriteAheadLog> wal_;
    std::shared_ptr<Manifest> manifest_;
    std::unique_ptr<CompactionManager> compaction_manager_;
    
    std::map<std::string, std::unique_ptr<LsmEngine>, core::StringViewLess> gsi_engines_;
    [[maybe_unused]] bool allow_gsi_replication_;

    std::map<std::string, std::shared_ptr<SSTable>, core::StringViewLess> sstable_cache_;
    size_t memtable_flush_threshold_ = 1000;
};

} // namespace cynamodb::engine::lsm
