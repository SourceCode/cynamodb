#include <cynamodb/engine/lsm/lsm_engine.hpp>
#include <cynamodb/engine/lsm/compactor.hpp>
#include <cynamodb/engine/lsm/compaction.hpp>
#include <cynamodb/engine/lsm/manifest.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace cynamodb::engine::lsm {

LsmEngine::LsmEngine(const std::string& db_path, std::shared_ptr<core::Arena> arena, bool allow_gsi_replication)
    : db_path_(db_path), arena_(arena), allow_gsi_replication_(allow_gsi_replication) {
    std::filesystem::create_directories(db_path_);
    
    manifest_ = std::make_shared<Manifest>(db_path_);
    manifest_->load();
    
    compaction_manager_ = std::make_unique<CompactionManager>(db_path_, manifest_);
    
    memtable_ = std::make_unique<MemTable>();
    wal_ = std::make_unique<WriteAheadLog>(db_path_ + "/wal.log");
    
    flush_thread_ = std::thread(&LsmEngine::flush_memtable, this);
    compaction_thread_ = std::thread(&LsmEngine::background_compaction, this);
}

LsmEngine::~LsmEngine() {
    shutting_down_ = true;
    flush_cv_.notify_all();
    compaction_cv_.notify_all();
    if (flush_thread_.joinable()) flush_thread_.join();
    if (compaction_thread_.joinable()) compaction_thread_.join();
    manifest_->save();
}

void LsmEngine::put(const std::string& table_name, const std::string& key, const AttributeMap& attributes) {
    (void)table_name;
    std::unique_lock lock(mutex_);
    memtable_->put(key, attributes);
    if (memtable_->size() >= memtable_flush_threshold_) {
        immutable_memtables_.push_back(std::move(memtable_));
        memtable_ = std::make_unique<MemTable>();
        flush_cv_.notify_one();
    }
}

void LsmEngine::remove(const std::string& table_name, const std::string& key) {
    (void)table_name;
    std::unique_lock lock(mutex_);
    memtable_->remove(key);
}

std::optional<StorageEngine::AttributeMap> LsmEngine::get(const std::string& table_name, const std::string& key) {
    (void)table_name;
    std::shared_lock lock(mutex_);
    auto val = memtable_->get(key);
    if (val) return val;
    
    for (auto it = immutable_memtables_.rbegin(); it != immutable_memtables_.rend(); ++it) {
        val = (*it)->get(key);
        if (val) return val;
    }
    
    for (const auto& sstable : sstables_) {
        val = sstable->get(key);
        if (val) return val;
    }
    
    return std::nullopt;
}

LsmEngine::ScanResult LsmEngine::scan(const std::string& table_name, const std::optional<std::string>& exclusive_start_key, size_t limit) {
    (void)table_name; (void)exclusive_start_key; (void)limit;
    return {};
}

LsmEngine::QueryResult LsmEngine::query(const std::string& table_name, const AttributeMap& key_conditions, const std::optional<std::string>& exclusive_start_key, size_t limit) {
    (void)table_name; (void)key_conditions; (void)exclusive_start_key; (void)limit;
    return {};
}

void LsmEngine::flush_memtable() {
    while (!shutting_down_) {
        std::shared_ptr<MemTable> to_flush;
        {
            std::unique_lock lock(mutex_);
            flush_cv_.wait(lock, [this] { return shutting_down_ || !immutable_memtables_.empty(); });
            if (shutting_down_ && immutable_memtables_.empty()) break;
            to_flush = immutable_memtables_.front();
        }
        
        uint64_t seq = manifest_->get_next_sequence();
        manifest_->set_next_sequence(seq + 1);
        
        std::string sst_path = db_path_ + "/table_" + std::to_string(seq) + ".sst";
        auto entries = to_flush->get_all_entries();
        SSTable::create(sst_path, entries);
        
        SSTableMetadata meta;
        meta.path = sst_path;
        meta.level = 0;
        meta.sequence_number = seq;
        if (!entries.empty()) {
            meta.min_key = entries.begin()->first;
            meta.max_key = entries.rbegin()->first;
        }
        manifest_->add_file(0, meta);
        manifest_->save();
        
        {
            std::unique_lock lock(mutex_);
            sstables_.push_front(std::make_shared<SSTable>(sst_path));
            immutable_memtables_.erase(immutable_memtables_.begin());
            if (compaction_manager_->should_compact_L0()) compaction_cv_.notify_one();
        }
    }
}

void LsmEngine::background_compaction() {
    while (!shutting_down_) {
        std::unique_lock lock(mutex_);
        compaction_cv_.wait(lock, [this] { return shutting_down_ || compaction_manager_->should_compact_L0(); });
        if (shutting_down_) break;
        
        compaction_manager_->trigger_compaction();
    }
}

std::string LsmEngine::get_table_path(const std::string& table_name) const {
    return db_path_ + "/" + table_name;
}

} // namespace cynamodb::engine::lsm
