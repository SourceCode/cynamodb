#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <atomic>
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

    // Durable append: writes the record and fsyncs before returning (equivalent to
    // append_only + commit). Used by the recovery/rebuild path.
    bool append(uint64_t seq, const std::string& key, const std::string& value);
    // Non-durable append: writes the record but does NOT fsync. The caller makes it
    // durable with commit() — typically AFTER releasing the engine lock, so the fsync
    // no longer serializes readers/writers. Returns false only on a write failure.
    bool append_only(uint64_t seq, const std::string& key, const std::string& value);
    // Group commit: ensures every record appended before this call is on stable
    // storage, coalescing concurrent callers into a single fdatasync (one fsync can
    // durably commit many pending records). Safe to call without any engine lock held.
    bool commit();
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
    // Appends one record to the file. Caller must hold mutex_.
    bool write_record(uint64_t seq, const std::string& key, const std::string& value);

    std::string path_;
    std::fstream file_;
    std::mutex mutex_;
    // Group-commit bookkeeping: appended_ counts records written (bumped under mutex_);
    // synced_ is the count known durable (guarded by sync_mutex_). commit() serializes
    // fsyncs on sync_mutex_ so concurrent writers share one fdatasync.
    std::mutex sync_mutex_;
    std::atomic<uint64_t> appended_{0};
    uint64_t synced_ = 0;
    uint32_t unsynced_writes_ = 0;
    uint64_t file_size_bytes_ = 0;
    bool fsync_each_ = true;
    // A second descriptor on the same file used purely to fdatasync the data that
    // file_ has flushed to the OS down to the device. -1 if unavailable.
    int sync_fd_ = -1;
};

} // namespace cynamodb::engine::lsm
