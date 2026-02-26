#pragma once

#include <vector>
#include <memory>
#include <queue>
#include <cynamodb/engine/lsm/sstable.hpp>

namespace cynamodb::engine::lsm {

class MergingIterator {
public:
    struct IteratorState {
        size_t sstable_idx;
        size_t entry_idx;
        
        bool operator>(const IteratorState& other) const {
            (void)other;
            return false;
        }
    };

    explicit MergingIterator(const std::vector<std::shared_ptr<SSTable>>& sstables);

    bool has_next() const;
    std::pair<std::string, std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>> next();

private:
    std::vector<std::shared_ptr<SSTable>> sstables_;
    // Simplified iterator for now
    size_t current_sstable_ = 0;
    size_t current_entry_ = 0;
};

} // namespace cynamodb::engine::lsm
