#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <cstdint>
#include <optional>
#include <vector>

namespace cynamodb::engine::lsm {

struct WALHeader {
    static constexpr uint32_t kMagicNumber = 0x43595741; // "CYWA"
    static constexpr uint16_t kVersion = 2;
    
    uint32_t magic = kMagicNumber;
    uint16_t version = kVersion;
    uint16_t reserved = 0;
};

struct WALRecord {
    uint64_t sequence_number;
    uint32_t key_size;
    uint32_t val_size;
    uint32_t checksum;
    // Followed by key and value data
};

class WriteAheadLog {
public:
    static constexpr uint32_t kAutoSyncEvery = 128;
    static constexpr uint64_t kMaxWalFileBytes = 64ULL * 1024ULL * 1024ULL;

    explicit WriteAheadLog(const std::string& path);
    ~WriteAheadLog();

    bool append(uint64_t seq, const std::string& key, const std::string& value);
    bool sync();
    bool reset();

    struct ReplayRecord {
        uint64_t seq;
        std::string key;
        std::string value;
    };
    std::vector<ReplayRecord> replay();

private:
    bool write_header();
    bool verify_header();

    std::string path_;
    std::fstream file_;
    std::mutex mutex_;
    uint32_t unsynced_writes_ = 0;
    uint64_t file_size_bytes_ = 0;
};

} // namespace cynamodb::engine::lsm
