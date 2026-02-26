#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <cynamodb/engine/lsm/sstable.hpp>

namespace cynamodb::engine::lsm {

enum class ManifestRecordType {
    AddFile,
    RemoveFile,
    NextSequence,
    LogNumber
};

struct ManifestRecord {
    ManifestRecordType type;
    uint32_t level;
    uint64_t sequence_number;
    std::string path;
    std::string min_key;
    std::string max_key;
};

class Manifest {
public:
    explicit Manifest(const std::string& db_path);
    
    void add_file(uint32_t level, const SSTableMetadata& meta);
    void remove_file(uint32_t level, const std::string& path);
    
    std::vector<SSTableMetadata> get_level_files(uint32_t level) const;
    uint64_t get_next_sequence() const { return next_sequence_; }
    void set_next_sequence(uint64_t seq) { next_sequence_ = seq; }

    bool save();
    bool load();

private:
    std::string db_path_;
    std::string manifest_path_;
    std::map<uint32_t, std::vector<SSTableMetadata>> levels_;
    uint64_t next_sequence_ = 0;
    mutable std::mutex mutex_;
};

} // namespace cynamodb::engine::lsm
