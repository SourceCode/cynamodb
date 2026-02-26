#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <cstdint>
#include <cynamodb/core/types.hpp>
#include <cynamodb/engine/lsm/skiplist.hpp>

namespace cynamodb::engine::lsm {

struct SSTableMetadata {
    std::string path;
    uint32_t level = 0;
    uint64_t sequence_number = 0;
    std::string min_key;
    std::string max_key;
    uint64_t file_size = 0;
    uint64_t entry_count = 0;
};

class SSTable {
public:
    struct IndexEntry {
        std::string key;
        uint64_t offset;
    };

    static std::string create(const std::string& path, const std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess>& entries);

    explicit SSTable(const std::string& path);

    std::optional<std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>> get(const std::string& key);
    
    const std::string& path() const { return path_; }
    const std::vector<IndexEntry>& index() const { return index_; }

private:
    void load_index();

    std::string path_;
    std::vector<IndexEntry> index_;
};

} // namespace cynamodb::engine::lsm
