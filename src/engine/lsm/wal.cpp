#include <cynamodb/engine/lsm/wal.hpp>
#include <cynamodb/utils/crc32.hpp>
#include <algorithm>
#include <filesystem>
#include <iostream>

namespace cynamodb::engine::lsm {

WriteAheadLog::WriteAheadLog(const std::string& path) : path_(path) {
    bool exists = std::filesystem::exists(path_);
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary);
    
    if (!exists) {
        file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
        write_header();
    } else {
        if (!verify_header()) {
            std::cerr << "WAL Header verification failed: " << path_ << std::endl;
            file_.close();
        }
    }
}

WriteAheadLog::~WriteAheadLog() {
    sync();
    if (file_.is_open()) file_.close();
}

bool WriteAheadLog::write_header() {
    WALHeader header;
    file_.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file_.flush();
    file_size_bytes_ = sizeof(header);
    return file_.good();
}

bool WriteAheadLog::verify_header() {
    file_.seekg(0, std::ios::beg);
    WALHeader header;
    file_.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (header.magic != WALHeader::kMagicNumber || header.version != WALHeader::kVersion) {
        return false;
    }
    file_.seekg(0, std::ios::end);
    file_size_bytes_ = static_cast<uint64_t>(file_.tellg());
    return true;
}

bool WriteAheadLog::append(uint64_t seq, const std::string& key, const std::string& value) {
    std::lock_guard lock(mutex_);
    if (!file_.is_open()) return false;

    WALRecord record;
    record.sequence_number = seq;
    record.key_size = static_cast<uint32_t>(key.size());
    record.val_size = static_cast<uint32_t>(value.size());
    
    uint32_t crc = utils::crc32c(std::string_view(reinterpret_cast<const char*>(&record), sizeof(uint64_t) + 2 * sizeof(uint32_t)));
    crc = utils::crc32c_extend(crc, reinterpret_cast<const uint8_t*>(key.data()), key.size());
    crc = utils::crc32c_extend(crc, reinterpret_cast<const uint8_t*>(value.data()), value.size());
    record.checksum = crc;

    file_.seekg(0, std::ios::end);
    file_.write(reinterpret_cast<const char*>(&record), sizeof(record));
    file_.write(key.data(), key.size());
    file_.write(value.data(), value.size());

    if (!file_.good()) return false;

    unsynced_writes_++;
    if (unsynced_writes_ >= kAutoSyncEvery) {
        sync();
    }
    return true;
}

bool WriteAheadLog::sync() {
    if (file_.is_open()) {
        file_.flush();
        unsynced_writes_ = 0;
        return true;
    }
    return false;
}

bool WriteAheadLog::reset() {
    std::lock_guard lock(mutex_);
    file_.close();
    std::filesystem::remove(path_);
    file_.open(path_, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc);
    return write_header();
}

std::vector<WriteAheadLog::ReplayRecord> WriteAheadLog::replay() {
    std::lock_guard lock(mutex_);
    std::vector<ReplayRecord> records;
    if (!file_.is_open()) return records;

    file_.seekg(sizeof(WALHeader), std::ios::beg);
    while (true) {
        WALRecord record;
        file_.read(reinterpret_cast<char*>(&record), sizeof(record));
        if (file_.gcount() < static_cast<std::streamsize>(sizeof(record))) break;

        std::string key(record.key_size, '\0');
        file_.read(key.data(), record.key_size);
        
        std::string val(record.val_size, '\0');
        file_.read(val.data(), record.val_size);

        if (!file_.good()) break;

        uint32_t crc = utils::crc32c(std::string_view(reinterpret_cast<const char*>(&record), sizeof(uint64_t) + 2 * sizeof(uint32_t)));
        crc = utils::crc32c_extend(crc, reinterpret_cast<const uint8_t*>(key.data()), key.size());
        crc = utils::crc32c_extend(crc, reinterpret_cast<const uint8_t*>(val.data()), val.size());
        
        if (crc != record.checksum) {
            std::cerr << "WAL Checksum mismatch at seq " << record.sequence_number << std::endl;
            break;
        }

        records.push_back({record.sequence_number, std::move(key), std::move(val)});
    }
    file_.clear();
    file_.seekg(0, std::ios::end);
    return records;
}

} // namespace cynamodb::engine::lsm
