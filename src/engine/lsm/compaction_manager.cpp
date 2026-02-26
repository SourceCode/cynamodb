#include <cynamodb/engine/lsm/compaction.hpp>
#include <algorithm>
#include <iostream>

namespace cynamodb::engine::lsm {

CompactionManager::CompactionManager(const std::string& db_path, std::shared_ptr<Manifest> manifest)
    : db_path_(db_path), manifest_(manifest) {}

bool CompactionManager::should_compact_L0() const {
    return manifest_->get_level_files(0).size() >= 4;
}

void CompactionManager::trigger_compaction() {
    if (should_compact_L0()) {
        run_leveled_compaction(0);
    }
}

std::vector<SSTableMetadata> CompactionManager::get_overlapping_files(uint32_t level, const std::string& min_key, const std::string& max_key) const {
    std::vector<SSTableMetadata> files = manifest_->get_level_files(level);
    std::vector<SSTableMetadata> overlapping;
    
    for (const auto& meta : files) {
        if (!(meta.max_key < min_key || meta.min_key > max_key)) {
            overlapping.push_back(meta);
        }
    }
    return overlapping;
}

void CompactionManager::run_leveled_compaction(uint32_t source_level) {
    uint32_t target_level = source_level + 1;
    auto source_files = manifest_->get_level_files(source_level);
    if (source_files.empty()) return;

    std::string min_key = source_files[0].min_key;
    std::string max_key = source_files[0].max_key;
    
    for (const auto& meta : source_files) {
        if (meta.min_key < min_key) min_key = meta.min_key;
        if (meta.max_key > max_key) max_key = meta.max_key;
    }

    auto overlapping_target = get_overlapping_files(target_level, min_key, max_key);
    
    std::cout << "[Compaction] Compacting L" << source_level << " to L" << target_level << std::endl;
    
    // In a real implementation, we would merge these files here.
    // For now, we'll just log it.
}

} // namespace cynamodb::engine::lsm
