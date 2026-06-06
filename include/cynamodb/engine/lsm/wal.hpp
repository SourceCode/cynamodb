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

    // fsync_each: when true (the default) every committed record is flushed to
    // stable storage with fdatasync, so an acknowledged write survives a power /
    // OS crash, not merely a process crash. Set the env var CYNAMODB_WAL_FSYNC=0
    // to trade this durability for throughput.
    explicit WriteAheadLog(const std::string& path);
    WriteAheadLog(const std::string& path, bool fsync_each);
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
    bool fsync_each_ = true;
    // A second descriptor on the same file used purely to fdatasync the data that
    // file_ has flushed to the OS down to the device. -1 if unavailable.
    int sync_fd_ = -1;
};

} // namespace cynamodb::engine::lsm
