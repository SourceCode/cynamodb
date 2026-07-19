#include <cynamodb/engine/lsm/sstable.hpp>

#include <cynamodb/engine/lsm/record_codec.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cynamodb::engine::lsm {

namespace {

template <typename T>
bool read_scalar(const uint8_t* data, size_t size, size_t& position, T& value) {
    if (position > size || sizeof(T) > size - position) return false;
    std::memcpy(&value, data + position, sizeof(T));
    position += sizeof(T);
    return true;
}

bool write_bytes(std::ofstream& out, const void* data, size_t size) {
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return out.good();
}

bool write_u32(std::ofstream& out, uint32_t value) {
    return write_bytes(out, &value, sizeof(value));
}

bool write_u64(std::ofstream& out, uint64_t value) {
    return write_bytes(out, &value, sizeof(value));
}

std::string temp_suffix() {
    return ".tmp." + std::to_string(static_cast<unsigned long long>(::getpid()));
}

} // namespace

SSTableWriter::SSTableWriter(std::string path)
    : path_(std::move(path)),
      data_tmp_path_(path_ + temp_suffix()),
      index_tmp_path_(path_ + ".index" + temp_suffix()),
      data_(data_tmp_path_, std::ios::binary | std::ios::trunc),
      index_(index_tmp_path_, std::ios::binary | std::ios::trunc) {}

SSTableWriter::~SSTableWriter() {
    cleanup();
}

bool SSTableWriter::append(std::string_view key, const Skiplist::SnapshotEntry& entry) {
    if (finished_ || !data_ || !index_ || key.empty() ||
        key.size() > std::numeric_limits<uint32_t>::max() ||
        entry.attributes.size() > std::numeric_limits<uint32_t>::max() ||
        entry_count_ >= std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    if (!previous_key_.empty() && std::string_view(previous_key_) >= key) return false;

    const auto raw_offset = data_.tellp();
    if (raw_offset < 0) return false;
    const uint64_t record_offset = static_cast<uint64_t>(raw_offset);
    const uint32_t key_len = static_cast<uint32_t>(key.size());
    const uint32_t attr_count = static_cast<uint32_t>(entry.attributes.size());

    if (!write_u32(data_, key_len) || !write_bytes(data_, key.data(), key.size()) ||
        !write_bytes(data_, &entry.is_deleted, sizeof(entry.is_deleted)) ||
        !write_u32(data_, attr_count)) {
        return false;
    }

    for (const auto& [name, value] : entry.attributes) {
        if (!value || name.size() > std::numeric_limits<uint32_t>::max()) return false;
        const std::string encoded = encode_attribute_value(*value);
        if (encoded.size() > std::numeric_limits<uint32_t>::max()) return false;
        const uint32_t name_len = static_cast<uint32_t>(name.size());
        const uint32_t value_len = static_cast<uint32_t>(encoded.size());
        if (!write_u32(data_, name_len) || !write_bytes(data_, name.data(), name.size()) ||
            !write_u32(data_, value_len) || !write_bytes(data_, encoded.data(), encoded.size())) {
            return false;
        }
    }

    if (!write_u32(index_, key_len) || !write_bytes(index_, key.data(), key.size()) ||
        !write_u64(index_, record_offset)) {
        return false;
    }

    if (entry_count_ == 0) min_key_.assign(key);
    max_key_.assign(key);
    previous_key_.assign(key);
    ++entry_count_;
    return true;
}

bool SSTableWriter::finish() {
    if (finished_) return true;
    if (!data_ || !index_ || entry_count_ > std::numeric_limits<uint32_t>::max()) return false;

    index_.flush();
    index_.close();
    if (!index_) return false;

    const auto raw_index_offset = data_.tellp();
    if (raw_index_offset < 0) return false;
    const uint64_t index_offset = static_cast<uint64_t>(raw_index_offset);
    const uint32_t index_count = static_cast<uint32_t>(entry_count_);
    if (!write_u32(data_, index_count)) return false;

    std::ifstream index_input(index_tmp_path_, std::ios::binary);
    if (!index_input) return false;
    std::array<char, 1024 * 1024> buffer{};
    while (index_input) {
        index_input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = index_input.gcount();
        if (count > 0) data_.write(buffer.data(), count);
    }
    if (!index_input.eof() || !data_ || !write_u64(data_, index_offset)) return false;

    data_.flush();
    data_.close();
    if (!data_) return false;

    const int data_fd = ::open(data_tmp_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (data_fd < 0) return false;
    const bool synced = ::fdatasync(data_fd) == 0;
    ::close(data_fd);
    if (!synced) return false;

    std::error_code ec;
    std::filesystem::rename(data_tmp_path_, path_, ec);
    if (ec) return false;
    const std::filesystem::path parent = std::filesystem::path(path_).parent_path();
    const int directory_fd = ::open(parent.empty() ? "." : parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd >= 0) {
        (void)::fsync(directory_fd);
        ::close(directory_fd);
    }
    std::filesystem::remove(index_tmp_path_, ec);
    finished_ = true;
    return true;
}

void SSTableWriter::cleanup() noexcept {
    if (data_.is_open()) data_.close();
    if (index_.is_open()) index_.close();
    if (finished_) return;
    std::error_code ec;
    std::filesystem::remove(data_tmp_path_, ec);
    std::filesystem::remove(index_tmp_path_, ec);
}

std::string SSTable::create(
    const std::string& path,
    const std::map<std::string, Skiplist::SnapshotEntry, core::StringViewLess>& entries) {
    SSTableWriter writer(path);
    for (const auto& [key, entry] : entries) {
        if (!writer.append(key, entry)) return {};
    }
    return writer.finish() ? path : std::string{};
}

SSTable::SSTable(const std::string& path) : path_(path) {
    load_index();
}

SSTable::~SSTable() {
    unmap();
}

void SSTable::load_index() {
    const int fd = ::open(path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        throw std::system_error(errno, std::generic_category(), "open SSTable " + path_);
    }

    struct stat st {};
    if (::fstat(fd, &st) != 0 || st.st_size < static_cast<off_t>(sizeof(uint64_t) + sizeof(uint32_t))) {
        const int saved_errno = errno;
        ::close(fd);
        throw std::runtime_error("invalid or truncated SSTable: " + path_ +
                                 (saved_errno ? " (" + std::string(std::strerror(saved_errno)) + ")" : ""));
    }

    mapping_size_ = static_cast<size_t>(st.st_size);
    void* mapped = ::mmap(nullptr, mapping_size_, PROT_READ, MAP_SHARED, fd, 0);
    const int map_errno = errno;
    ::close(fd);
    if (mapped == MAP_FAILED) {
        mapping_size_ = 0;
        throw std::system_error(map_errno, std::generic_category(), "mmap SSTable " + path_);
    }
    mapping_ = static_cast<const uint8_t*>(mapped);
    (void)::madvise(const_cast<uint8_t*>(mapping_), mapping_size_, MADV_SEQUENTIAL);

    size_t footer_position = mapping_size_ - sizeof(uint64_t);
    if (!read_scalar(mapping_, mapping_size_, footer_position, data_end_offset_) ||
        data_end_offset_ > mapping_size_ - sizeof(uint64_t) - sizeof(uint32_t)) {
        unmap();
        throw std::runtime_error("invalid SSTable index offset: " + path_);
    }

    size_t position = static_cast<size_t>(data_end_offset_);
    uint32_t index_size = 0;
    if (!read_scalar(mapping_, mapping_size_ - sizeof(uint64_t), position, index_size)) {
        unmap();
        throw std::runtime_error("truncated SSTable index header: " + path_);
    }
    const size_t remaining_index_bytes = mapping_size_ - sizeof(uint64_t) - position;
    constexpr size_t kMinimumIndexEntryBytes = sizeof(uint32_t) + sizeof(uint64_t);
    if (index_size > remaining_index_bytes / kMinimumIndexEntryBytes) {
        unmap();
        throw std::runtime_error("impossible SSTable index count: " + path_);
    }
    index_entry_offsets_.reserve(index_size);
    for (uint32_t i = 0; i < index_size; ++i) {
        const size_t entry_position = position;
        uint32_t key_len = 0;
        if (!read_scalar(mapping_, mapping_size_ - sizeof(uint64_t), position, key_len) ||
            key_len > mapping_size_ - sizeof(uint64_t) - position ||
            sizeof(uint64_t) > mapping_size_ - sizeof(uint64_t) - position - key_len) {
            unmap();
            throw std::runtime_error("truncated SSTable index entry: " + path_);
        }
        position += key_len;
        uint64_t record_offset = 0;
        if (!read_scalar(mapping_, mapping_size_ - sizeof(uint64_t), position, record_offset) ||
            record_offset >= data_end_offset_) {
            unmap();
            throw std::runtime_error("invalid SSTable record offset: " + path_);
        }
        index_entry_offsets_.push_back(static_cast<uint64_t>(entry_position));
    }
    if (position != mapping_size_ - sizeof(uint64_t)) {
        unmap();
        throw std::runtime_error("SSTable index size mismatch: " + path_);
    }
    // Index construction touched every index page. The compact offset vector is now
    // sufficient; evict those file-backed pages so startup does not leave gigabytes in
    // the container's RSS. Point reads will fault only the pages they actually need.
    (void)::madvise(const_cast<uint8_t*>(mapping_), mapping_size_, MADV_DONTNEED);
    (void)::madvise(const_cast<uint8_t*>(mapping_), mapping_size_, MADV_RANDOM);
}

void SSTable::unmap() noexcept {
    if (mapping_) {
        ::munmap(const_cast<uint8_t*>(mapping_), mapping_size_);
        mapping_ = nullptr;
        mapping_size_ = 0;
        data_end_offset_ = 0;
    }
}

SSTable::IndexEntry SSTable::index_entry(size_t position) const {
    if (position >= index_entry_offsets_.size()) throw std::out_of_range("SSTable index position");
    size_t cursor = static_cast<size_t>(index_entry_offsets_[position]);
    uint32_t key_len = 0;
    uint64_t record_offset = 0;
    if (!read_scalar(mapping_, mapping_size_, cursor, key_len) || key_len > mapping_size_ - cursor) {
        throw std::runtime_error("corrupt SSTable index key: " + path_);
    }
    const std::string_view key(reinterpret_cast<const char*>(mapping_ + cursor), key_len);
    cursor += key_len;
    if (!read_scalar(mapping_, mapping_size_, cursor, record_offset)) {
        throw std::runtime_error("corrupt SSTable index offset: " + path_);
    }
    return {key, record_offset};
}

size_t SSTable::lower_bound_index(std::string_view key) const {
    size_t first = 0;
    size_t count = index_entry_offsets_.size();
    while (count > 0) {
        const size_t step = count / 2;
        const size_t middle = first + step;
        if (index_entry(middle).key < key) {
            first = middle + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    return first;
}

size_t SSTable::upper_bound_index(std::string_view key) const {
    size_t first = 0;
    size_t count = index_entry_offsets_.size();
    while (count > 0) {
        const size_t step = count / 2;
        const size_t middle = first + step;
        if (!(key < index_entry(middle).key)) {
            first = middle + 1;
            count -= step + 1;
        } else {
            count = step;
        }
    }
    return first;
}

bool SSTable::contains(std::string_view key) const {
    const size_t position = lower_bound_index(key);
    return position < entry_count() && index_entry(position).key == key;
}

std::optional<Skiplist::SnapshotEntry> SSTable::read_entry(size_t position) const {
    if (position >= entry_count()) return std::nullopt;
    const IndexEntry indexed = index_entry(position);
    size_t cursor = static_cast<size_t>(indexed.offset);
    uint32_t key_len = 0;
    if (!read_scalar(mapping_, static_cast<size_t>(data_end_offset_), cursor, key_len) ||
        key_len > data_end_offset_ - cursor) {
        return std::nullopt;
    }
    if (key_len != indexed.key.size() ||
        std::memcmp(mapping_ + cursor, indexed.key.data(), key_len) != 0) {
        return std::nullopt;
    }
    cursor += key_len;

    bool is_deleted = false;
    uint32_t attr_count = 0;
    if (!read_scalar(mapping_, static_cast<size_t>(data_end_offset_), cursor, is_deleted) ||
        !read_scalar(mapping_, static_cast<size_t>(data_end_offset_), cursor, attr_count)) {
        return std::nullopt;
    }

    Skiplist::SnapshotEntry result;
    result.is_deleted = is_deleted;
    for (uint32_t i = 0; i < attr_count; ++i) {
        uint32_t name_len = 0;
        if (!read_scalar(mapping_, static_cast<size_t>(data_end_offset_), cursor, name_len) ||
            name_len > data_end_offset_ - cursor) {
            return std::nullopt;
        }
        const std::string name(reinterpret_cast<const char*>(mapping_ + cursor), name_len);
        cursor += name_len;

        uint32_t value_len = 0;
        if (!read_scalar(mapping_, static_cast<size_t>(data_end_offset_), cursor, value_len) ||
            value_len > data_end_offset_ - cursor) {
            return std::nullopt;
        }
        const std::string_view encoded(reinterpret_cast<const char*>(mapping_ + cursor), value_len);
        cursor += value_len;
        auto value = decode_attribute_value(encoded);
        if (!value) return std::nullopt;
        result.attributes.emplace(name, std::move(value));
    }
    return result;
}

std::optional<std::map<std::string, std::shared_ptr<core::AttributeValue>, core::StringViewLess>>
SSTable::get(const std::string& key) const {
    const size_t position = lower_bound_index(key);
    if (position >= entry_count() || index_entry(position).key != key) return std::nullopt;
    auto entry = read_entry(position);
    if (!entry || entry->is_deleted) return std::nullopt;
    return std::move(entry->attributes);
}

} // namespace cynamodb::engine::lsm
