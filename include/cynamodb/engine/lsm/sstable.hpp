#pragma once

#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

// Writes the existing SSTable format without retaining keys or values in memory.
// Records are written to the data file while index entries are spooled to a temporary
// file, then the index is appended and the completed file is atomically renamed.
class SSTableWriter {
public:
    explicit SSTableWriter(std::string path);
    ~SSTableWriter();

    SSTableWriter(const SSTableWriter&) = delete;
    SSTableWriter& operator=(const SSTableWriter&) = delete;

    bool append(std::string_view key, const Skiplist::SnapshotEntry& entry);
    bool finish();

    uint64_t entry_count() const { return entry_count_; }
    const std::string& min_key() const { return min_key_; }
    const std::string& max_key() const { return max_key_; }

private:
    void cleanup() noexcept;

    std::string path_;
    std::string data_tmp_path_;
    std::string index_tmp_path_;
    std::ofstream data_;
    std::ofstream index_;
    uint64_t entry_count_ = 0;
    std::string min_key_;
    std::string max_key_;
    std::string previous_key_;
    bool finished_ = false;
};

class SSTable {
public:
    struct IndexEntry {
        std::string_view key;
        uint64_t offset;
    };

    static std::string create(
        const std::string& path,
        const std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess>& entries);

    explicit SSTable(const std::string& path);
    ~SSTable();

    SSTable(const SSTable&) = delete;
    SSTable& operator=(const SSTable&) = delete;
    SSTable(SSTable&&) = delete;
    SSTable& operator=(SSTable&&) = delete;

    std::optional<std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>>
    get(const std::string& key) const;

    // Reads the indexed record, including tombstone state. A missing result means the
    // mapped file failed validation while decoding and must be treated as corruption.
    std::optional<Skiplist::SnapshotEntry> read_entry(size_t position) const;

    size_t lower_bound_index(std::string_view key) const;
    size_t upper_bound_index(std::string_view key) const;
    bool contains(std::string_view key) const;
    IndexEntry index_entry(size_t position) const;

    size_t entry_count() const { return index_entry_offsets_.size(); }
    uint64_t file_size() const { return static_cast<uint64_t>(mapping_size_); }
    const std::string& path() const { return path_; }

private:
    void load_index();
    void unmap() noexcept;

    std::string path_;
    const uint8_t* mapping_ = nullptr;
    size_t mapping_size_ = 0;
    uint64_t data_end_offset_ = 0;

    // One 64-bit offset per entry points into the mapped on-disk index. Key bytes stay
    // file-backed and reclaimable instead of expanding into millions of heap strings.
    std::vector<uint64_t> index_entry_offsets_;
};

} // namespace cynamodb::engine::lsm
