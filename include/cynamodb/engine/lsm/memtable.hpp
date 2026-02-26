#pragma once

#include <string>
#include <map>
#include <memory>
#include <optional>
#include <cynamodb/core/types.hpp>
#include <cynamodb/engine/lsm/skiplist.hpp>

namespace cynamodb::engine::lsm {

class MemTable {
public:
    using Attributes = std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>;

    MemTable();

    void put(const std::string& key, const Attributes& attributes);
    void remove(const std::string& key);
    std::optional<Attributes> get(const std::string& key) const;
    bool is_tombstoned(const std::string& key) const;
    
    std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess> get_all_entries() const;
    size_t size() const;

private:
    std::unique_ptr<Skiplist> skiplist_;
};

} // namespace cynamodb::engine::lsm
