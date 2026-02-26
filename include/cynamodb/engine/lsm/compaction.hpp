#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cynamodb/engine/lsm/sstable.hpp>
#include <cynamodb/engine/lsm/manifest.hpp>

namespace cynamodb::engine::lsm {

class CompactionManager {
public:
    explicit CompactionManager(const std::string& db_path, std::shared_ptr<Manifest> manifest);

    bool should_compact_L0() const;
    void trigger_compaction();

    std::vector<SSTableMetadata> get_overlapping_files(uint32_t level, const std::string& min_key, const std::string& max_key) const;

private:
    void run_leveled_compaction(uint32_t source_level);
    
    std::string db_path_;
    std::shared_ptr<Manifest> manifest_;
};

} // namespace cynamodb::engine::lsm
