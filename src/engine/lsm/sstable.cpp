#include <cynamodb/engine/lsm/sstable.hpp>
#include <cynamodb/utils/crc32.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>

namespace cynamodb::engine::lsm {

namespace {

[[maybe_unused]] bool is_valid_identifier(std::string_view value, size_t max_len) {
    if (value.empty() || value.size() > max_len) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
    });
}

std::string encode_attribute_value(const core::AttributeValue& val) {
    std::string out;
    out.push_back(static_cast<char>(val.type));
    if (val.type == core::AttributeType::S || val.type == core::AttributeType::N) {
        const auto& s = std::get<core::String>(val.value);
        uint32_t len = static_cast<uint32_t>(s.size());
        out.append(reinterpret_cast<const char*>(&len), sizeof(len));
        out.append(s.data(), s.size());
    } else if (val.type == core::AttributeType::BOOL) {
        out.push_back(std::get<bool>(val.value) ? 1 : 0);
    }
    return out;
}

std::shared_ptr<core::AttributeValue> decode_attribute_value(std::string_view& data) {
    if (data.empty()) return nullptr;
    auto attr = std::make_shared<core::AttributeValue>();
    attr->type = static_cast<core::AttributeType>(data[0]);
    data.remove_prefix(1);
    if (attr->type == core::AttributeType::S || attr->type == core::AttributeType::N) {
        if (data.size() < sizeof(uint32_t)) return nullptr;
        uint32_t len;
        std::copy(data.data(), data.data() + sizeof(uint32_t), reinterpret_cast<char*>(&len));
        data.remove_prefix(sizeof(uint32_t));
        if (data.size() < len) return nullptr;
        attr->value = core::String(data.substr(0, len));
        data.remove_prefix(len);
    } else if (attr->type == core::AttributeType::BOOL) {
        if (data.empty()) return nullptr;
        attr->value = (data[0] != 0);
        data.remove_prefix(1);
    }
    return attr;
}

} // namespace

std::string SSTable::create(const std::string& path, const std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess>& entries) {
    std::ofstream file(path, std::ios::binary);
    if (!file) return "";

    std::vector<IndexEntry> index;
    for (const auto& [key, entry] : entries) {
        uint64_t offset = static_cast<uint64_t>(file.tellp());
        index.push_back({key, offset});

        uint32_t key_len = static_cast<uint32_t>(key.size());
        file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        file.write(key.data(), key_len);
        
        file.write(reinterpret_cast<const char*>(&entry.is_deleted), sizeof(entry.is_deleted));
        
        uint32_t attr_count = static_cast<uint32_t>(entry.attributes.size());
        file.write(reinterpret_cast<const char*>(&attr_count), sizeof(attr_count));
        for (const auto& [name, val] : entry.attributes) {
            uint32_t name_len = static_cast<uint32_t>(name.size());
            file.write(reinterpret_cast<const char*>(&name_len), sizeof(name_len));
            file.write(name.data(), name_len);
            
            std::string encoded = encode_attribute_value(*val);
            uint32_t val_len = static_cast<uint32_t>(encoded.size());
            file.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
            file.write(encoded.data(), val_len);
        }
    }

    uint64_t index_offset = static_cast<uint64_t>(file.tellp());
    uint32_t index_size = static_cast<uint32_t>(index.size());
    file.write(reinterpret_cast<const char*>(&index_size), sizeof(index_size));
    for (const auto& entry : index) {
        uint32_t key_len = static_cast<uint32_t>(entry.key.size());
        file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
        file.write(entry.key.data(), key_len);
        file.write(reinterpret_cast<const char*>(&entry.offset), sizeof(entry.offset));
    }
    file.write(reinterpret_cast<const char*>(&index_offset), sizeof(index_offset));
    
    file.close();
    return path;
}

SSTable::SSTable(const std::string& path) : path_(path) {
    load_index();
}

std::optional<std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>> SSTable::get(const std::string& key) {
    auto it = std::lower_bound(index_.begin(), index_.end(), key, [](const IndexEntry& a, const std::string& b) {
        return a.key < b;
    });
    if (it == index_.end() || it->key != key) return std::nullopt;

    std::ifstream file(path_, std::ios::binary);
    if (!file) return std::nullopt;
    file.seekg(static_cast<std::streamoff>(it->offset));

    uint32_t key_len;
    file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
    file.seekg(key_len, std::ios::cur);

    bool is_deleted;
    file.read(reinterpret_cast<char*>(&is_deleted), sizeof(is_deleted));
    if (is_deleted) return std::nullopt;

    uint32_t attr_count;
    file.read(reinterpret_cast<char*>(&attr_count), sizeof(attr_count));
    std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess> attrs;
    for (uint32_t i = 0; i < attr_count; ++i) {
        uint32_t name_len;
        file.read(reinterpret_cast<char*>(&name_len), sizeof(name_len));
        std::string name(name_len, '\0');
        file.read(name.data(), name_len);

        uint32_t val_len;
        file.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
        std::string val_data(val_len, '\0');
        file.read(val_data.data(), val_len);
        
        std::string_view sv = val_data;
        attrs[name] = decode_attribute_value(sv);
    }
    return attrs;
}

void SSTable::load_index() {
    std::ifstream file(path_, std::ios::binary | std::ios::ate);
    if (!file) return;
    
    std::streamoff size = file.tellg();
    if (size < static_cast<std::streamoff>(sizeof(uint64_t))) return;
    
    file.seekg(-static_cast<std::streamoff>(sizeof(uint64_t)), std::ios::end);
    uint64_t index_offset;
    file.read(reinterpret_cast<char*>(&index_offset), sizeof(index_offset));
    
    file.seekg(static_cast<std::streamoff>(index_offset));
    uint32_t index_size;
    file.read(reinterpret_cast<char*>(&index_size), sizeof(index_size));
    for (uint32_t i = 0; i < index_size; ++i) {
        uint32_t key_len;
        file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        std::string key(key_len, '\0');
        file.read(key.data(), key_len);
        uint64_t offset;
        file.read(reinterpret_cast<char*>(&offset), sizeof(offset));
        index_.push_back({key, offset});
    }
}

} // namespace cynamodb::engine::lsm
