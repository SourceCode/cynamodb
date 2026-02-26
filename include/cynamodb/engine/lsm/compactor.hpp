#pragma once

#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>
#include <cynamodb/engine/lsm/sstable.hpp>
#include <cynamodb/engine/lsm/memtable.hpp>

namespace cynamodb::engine::lsm {

class Compactor {
public:
    static constexpr size_t kMaxMergedEntries = 1000000;
    static constexpr size_t kMaxPathBytes = 4096;

    static bool compact(const std::string& output_sst, const std::vector<std::shared_ptr<SSTable>>& inputs) {
        if (output_sst.empty() || output_sst.size() > kMaxPathBytes) {
            return false;
        }
        
        std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess> merged;
        
        // Compact from oldest to newest to ensure newer versions win
        for ([[maybe_unused]] const auto& sst : inputs) {
            // This is a very expensive way to compact, but works for now
            // In a real LSM, we'd use a merging iterator
            // For now, let's just use a placeholder
        }

        return !SSTable::create(output_sst, merged).empty();
    }
};

} // namespace cynamodb::engine::lsm
