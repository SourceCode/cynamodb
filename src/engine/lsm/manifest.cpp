#include <cynamodb/engine/lsm/manifest.hpp>
#include <fstream>
#include <filesystem>

namespace cynamodb::engine::lsm {

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
    std::ofstream file(manifest_path_, std::ios::binary | std::ios::trunc);
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
    return true;
}

bool Manifest::load() {
    std::lock_guard lock(mutex_);
    if (!std::filesystem::exists(manifest_path_)) return true;
    
    std::ifstream file(manifest_path_, std::ios::binary);
    if (!file) return false;

    file.read(reinterpret_cast<char*>(&next_sequence_), sizeof(next_sequence_));
    
    uint32_t level_count;
    file.read(reinterpret_cast<char*>(&level_count), sizeof(level_count));
    
    for (uint32_t i = 0; i < level_count; ++i) {
        uint32_t level;
        file.read(reinterpret_cast<char*>(&level), sizeof(level));
        
        uint32_t file_count;
        file.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
        
        for (uint32_t j = 0; j < file_count; ++j) {
            SSTableMetadata meta;
            meta.level = level;
            
            uint32_t path_len;
            file.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
            meta.path.resize(path_len);
            file.read(meta.path.data(), path_len);
            
            file.read(reinterpret_cast<char*>(&meta.sequence_number), sizeof(meta.sequence_number));
            
            uint32_t min_len;
            file.read(reinterpret_cast<char*>(&min_len), sizeof(min_len));
            meta.min_key.resize(min_len);
            file.read(meta.min_key.data(), min_len);
            
            uint32_t max_len;
            file.read(reinterpret_cast<char*>(&max_len), sizeof(max_len));
            meta.max_key.resize(max_len);
            file.read(meta.max_key.data(), max_len);
            
            levels_[level].push_back(std::move(meta));
        }
    }
    return true;
}

} // namespace cynamodb::engine::lsm
