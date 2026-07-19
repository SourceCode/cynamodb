#include <cynamodb/engine/lsm/manifest.hpp>
#include <fstream>
#include <filesystem>
#include <limits>
#include <fcntl.h>
#include <unistd.h>

namespace cynamodb::engine::lsm {

namespace {

constexpr uint64_t kMaxManifestBytes = 256ULL * 1024ULL * 1024ULL;
constexpr uint32_t kMaxManifestLevels = 64;
constexpr uint32_t kMaxManifestStringBytes = 1024U * 1024U;

template <typename T>
bool read_bounded(std::ifstream& file, uint64_t& remaining, T& value) {
    if (remaining < sizeof(T)) return false;
    file.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!file) return false;
    remaining -= sizeof(T);
    return true;
}

bool read_bounded_string(std::ifstream& file, uint64_t& remaining, std::string& value) {
    uint32_t length = 0;
    if (!read_bounded(file, remaining, length) || length > kMaxManifestStringBytes ||
        length > remaining) {
        return false;
    }
    value.resize(length);
    if (length != 0) file.read(value.data(), static_cast<std::streamsize>(length));
    if (!file) return false;
    remaining -= length;
    return true;
}

} // namespace

Manifest::Manifest(const std::string& db_path) 
    : db_path_(db_path), manifest_path_(db_path + "/MANIFEST") {}

void Manifest::add_file(uint32_t level, const SSTableMetadata& meta) {
    std::lock_guard lock(mutex_);
    levels_[level].push_back(meta);
}

void Manifest::remove_file(uint32_t level, const std::string& path) {
    std::lock_guard lock(mutex_);
    auto& files = levels_[level];
    auto it = std::remove_if(files.begin(), files.end(), [&](const auto& meta) {
        return meta.path == path;
    });
    files.erase(it, files.end());
}

std::vector<SSTableMetadata> Manifest::get_level_files(uint32_t level) const {
    std::lock_guard lock(mutex_);
    auto it = levels_.find(level);
    if (it == levels_.end()) return {};
    return it->second;
}

bool Manifest::save() {
    std::lock_guard lock(mutex_);
    if (levels_.size() > kMaxManifestLevels) return false;
    for (const auto& [level, files] : levels_) {
        if (level >= kMaxManifestLevels || files.size() > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
        for (const auto& meta : files) {
            if (meta.path.empty() || meta.path.size() > kMaxManifestStringBytes ||
                meta.min_key.size() > kMaxManifestStringBytes ||
                meta.max_key.size() > kMaxManifestStringBytes) {
                return false;
            }
        }
    }
    const std::string temp_path = manifest_path_ + ".tmp." + std::to_string(static_cast<unsigned long long>(::getpid()));
    const auto remove_temp = [&] {
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
    };
    std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    // Very simple binary format for now
    file.write(reinterpret_cast<const char*>(&next_sequence_), sizeof(next_sequence_));
    
    uint32_t level_count = static_cast<uint32_t>(levels_.size());
    file.write(reinterpret_cast<const char*>(&level_count), sizeof(level_count));
    
    for (const auto& [level, files] : levels_) {
        file.write(reinterpret_cast<const char*>(&level), sizeof(level));
        uint32_t file_count = static_cast<uint32_t>(files.size());
        file.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));
        
        for (const auto& meta : files) {
            uint32_t path_len = static_cast<uint32_t>(meta.path.size());
            file.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
            file.write(meta.path.data(), path_len);
            
            file.write(reinterpret_cast<const char*>(&meta.sequence_number), sizeof(meta.sequence_number));
            
            uint32_t min_len = static_cast<uint32_t>(meta.min_key.size());
            file.write(reinterpret_cast<const char*>(&min_len), sizeof(min_len));
            file.write(meta.min_key.data(), min_len);
            
            uint32_t max_len = static_cast<uint32_t>(meta.max_key.size());
            file.write(reinterpret_cast<const char*>(&max_len), sizeof(max_len));
            file.write(meta.max_key.data(), max_len);
        }
    }
    file.flush();
    file.close();
    if (!file) {
        remove_temp();
        return false;
    }

    const int fd = ::open(temp_path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        remove_temp();
        return false;
    }
    const bool synced = ::fdatasync(fd) == 0;
    ::close(fd);
    if (!synced) {
        remove_temp();
        return false;
    }

    std::error_code ec;
    std::filesystem::rename(temp_path, manifest_path_, ec);
    if (ec) {
        remove_temp();
        return false;
    }
    const int directory_fd = ::open(db_path_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd >= 0) {
        (void)::fsync(directory_fd);
        ::close(directory_fd);
    }
    return true;
}

bool Manifest::load() {
    std::lock_guard lock(mutex_);
    if (!std::filesystem::exists(manifest_path_)) return true;

    std::error_code size_error;
    const uint64_t file_size = std::filesystem::file_size(manifest_path_, size_error);
    if (size_error || file_size > kMaxManifestBytes ||
        file_size < sizeof(uint64_t) + sizeof(uint32_t)) {
        return false;
    }
    std::ifstream file(manifest_path_, std::ios::binary);
    if (!file) return false;

    uint64_t remaining = file_size;
    uint64_t loaded_next_sequence = 0;
    uint32_t level_count = 0;
    if (!read_bounded(file, remaining, loaded_next_sequence) ||
        !read_bounded(file, remaining, level_count) || level_count > kMaxManifestLevels) {
        return false;
    }

    std::map<uint32_t, std::vector<SSTableMetadata>> loaded_levels;
    for (uint32_t i = 0; i < level_count; ++i) {
        uint32_t level = 0;
        uint32_t file_count = 0;
        if (!read_bounded(file, remaining, level) || level >= kMaxManifestLevels ||
            !read_bounded(file, remaining, file_count)) {
            return false;
        }
        // Every entry has three length fields plus a sequence even when all strings
        // are empty. This rejects impossible counts before reserving or looping.
        constexpr uint64_t kMinimumEntryBytes = sizeof(uint32_t) * 3 + sizeof(uint64_t);
        if (file_count > remaining / kMinimumEntryBytes) return false;
        auto& files = loaded_levels[level];
        files.reserve(file_count);
        for (uint32_t j = 0; j < file_count; ++j) {
            SSTableMetadata meta;
            meta.level = level;
            if (!read_bounded_string(file, remaining, meta.path) || meta.path.empty() ||
                !read_bounded(file, remaining, meta.sequence_number) ||
                !read_bounded_string(file, remaining, meta.min_key) ||
                !read_bounded_string(file, remaining, meta.max_key)) {
                return false;
            }
            files.push_back(std::move(meta));
        }
    }
    if (remaining != 0) return false;

    next_sequence_ = loaded_next_sequence;
    levels_ = std::move(loaded_levels);
    return true;
}

} // namespace cynamodb::engine::lsm
