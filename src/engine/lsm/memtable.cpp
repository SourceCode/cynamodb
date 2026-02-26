#include <cynamodb/engine/lsm/memtable.hpp>
#include <algorithm>
#include <cctype>

namespace cynamodb::engine::lsm {

MemTable::MemTable() : skiplist_(std::make_unique<Skiplist>()) {}

void MemTable::put(const std::string& key, const Attributes& attributes) {
    skiplist_->insert(key, attributes, false);
}

void MemTable::remove(const std::string& key) {
    skiplist_->insert(key, {}, true);
}

std::optional<MemTable::Attributes> MemTable::get(const std::string& key) const {
    return skiplist_->get(key);
}

bool MemTable::is_tombstoned(const std::string& key) const {
    return skiplist_->is_tombstoned(key);
}

std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess> MemTable::get_all_entries() const {
    return skiplist_->get_all_entries();
}

size_t MemTable::size() const {
    return skiplist_->size();
}

} // namespace cynamodb::engine::lsm
