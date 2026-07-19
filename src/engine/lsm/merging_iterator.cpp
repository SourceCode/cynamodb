#include <cynamodb/engine/lsm/merging_iterator.hpp>

namespace cynamodb::engine::lsm {

MergingIterator::MergingIterator(const std::vector<std::shared_ptr<SSTable>>& sstables)
    : sstables_(sstables) {}

bool MergingIterator::has_next() const {
    if (sstables_.empty()) return false;
    return current_sstable_ < sstables_.size() && 
           current_entry_ < sstables_[current_sstable_]->entry_count();
}

std::pair<std::string, std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>> MergingIterator::next() {
    auto& sst = sstables_[current_sstable_];
    std::string key(sst->index_entry(current_entry_).key);
    auto attrs = sst->get(key).value_or(std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>{});
    
    current_entry_++;
    if (current_entry_ >= sst->entry_count()) {
        current_entry_ = 0;
        current_sstable_++;
    }
    
    return {std::move(key), std::move(attrs)};
}

} // namespace cynamodb::engine::lsm
