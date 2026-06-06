#include <cynamodb/engine/table_manager.hpp>
#include <cynamodb/utils/crc32.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace cynamodb::engine {

TableManager::TableManager(const std::string& metadata_path) : metadata_path_(metadata_path) {
    load_metadata();
}

std::expected<core::TableDefinition, TableError> TableManager::create_table(const core::TableDefinition& table_def) {
    std::unique_lock lock(mutex_);
    if (tables_.contains(table_def.table_name)) {
        return std::unexpected(TableError::TableAlreadyExists);
    }
    tables_[table_def.table_name] = table_def;
    dirty_ = true;
    save_metadata();
    return table_def;
}

std::expected<core::TableDefinition, TableError> TableManager::describe_table(std::string_view table_name) {
    std::shared_lock lock(mutex_);
    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        return std::unexpected(TableError::TableNotFound);
    }
    return it->second;
}

std::expected<core::TableDefinition, TableError> TableManager::delete_table(std::string_view table_name) {
    std::unique_lock lock(mutex_);
    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        return std::unexpected(TableError::TableNotFound);
    }
    core::TableDefinition removed = it->second;
    tables_.erase(it);
    collection_sizes_.erase(std::string(table_name));
    dirty_ = true;
    save_metadata();
    return removed;
}

std::expected<core::TableDefinition, TableError> TableManager::set_ttl(
    std::string_view table_name, const core::TimeToLiveSpecification& spec) {
    std::unique_lock lock(mutex_);
    auto it = tables_.find(table_name);
    if (it == tables_.end()) {
        return std::unexpected(TableError::TableNotFound);
    }
    it->second.ttl_specification = spec;
    dirty_ = true;
    save_metadata();
    return it->second;
}

std::vector<std::string> TableManager::list_tables() {
    std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : tables_) {
        names.push_back(name);
    }
    return names;
}

void TableManager::update_collection_size(const std::string& table_name, const std::string& partition_key, int64_t size_delta) {
    std::unique_lock lock(mutex_);
    auto& table_collections = collection_sizes_[table_name];
    if (size_delta < 0 && static_cast<uint64_t>(-size_delta) > table_collections[partition_key]) {
        table_collections[partition_key] = 0;
    } else {
        table_collections[partition_key] += size_delta;
    }
}

std::expected<void, TableError> TableManager::check_collection_limit(const std::string& table_name, const std::string& partition_key, size_t new_item_size) {
    std::shared_lock lock(mutex_);
    auto table_it = tables_.find(table_name);
    if (table_it == tables_.end()) return std::unexpected(TableError::TableNotFound);
    
    // Limits only apply if LSI exists
    if (table_it->second.local_secondary_indexes.empty()) return {};

    auto it = collection_sizes_.find(table_name);
    if (it != collection_sizes_.end()) {
        auto coll_it = it->second.find(partition_key);
        if (coll_it != it->second.end()) {
            constexpr uint64_t kMaxCollectionSize = 10ULL * 1024 * 1024 * 1024; // 10GB
            if (coll_it->second + new_item_size > kMaxCollectionSize) {
                return std::unexpected(TableError::ItemCollectionSizeLimitExceeded);
            }
        }
    }
    return {};
}

namespace {

constexpr uint32_t kMetadataMagic = 0x4359544D;  // "CYTM"
// v2 appends a per-table TTL block; v3 appends GSI/LSI definitions. Older files
// (without those trailing blocks) still load.
constexpr uint32_t kMetadataVersion = 3;

void write_u32(std::ostream& os, uint32_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void write_u64(std::ostream& os, uint64_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void write_u8(std::ostream& os, uint8_t v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void write_str(std::ostream& os, const std::string& s) {
    write_u32(os, static_cast<uint32_t>(s.size()));
    os.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool read_u32(std::istream& is, uint32_t& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    return static_cast<size_t>(is.gcount()) == sizeof(v);
}
bool read_u64(std::istream& is, uint64_t& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    return static_cast<size_t>(is.gcount()) == sizeof(v);
}
bool read_u8(std::istream& is, uint8_t& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(v));
    return static_cast<size_t>(is.gcount()) == sizeof(v);
}
bool read_str(std::istream& is, std::string& s) {
    uint32_t len = 0;
    if (!read_u32(is, len)) return false;
    if (len > 64 * 1024) return false;  // sanity bound for a table-metadata string
    s.resize(len);
    is.read(s.data(), static_cast<std::streamsize>(len));
    return static_cast<size_t>(is.gcount()) == len;
}

}  // namespace

// Persists the table catalog. Only the fields required by the data plane are
// stored (name, key schema, attribute definitions, billing mode, creation time);
// secondary-index/stream configuration is not yet round-tripped. The caller must
// hold the write lock.
void TableManager::save_metadata() {
    const std::string tmp_path = metadata_path_ + ".tmp";
    {
        std::ofstream os(tmp_path, std::ios::binary | std::ios::trunc);
        if (!os) return;

        write_u32(os, kMetadataMagic);
        write_u32(os, kMetadataVersion);
        write_u32(os, static_cast<uint32_t>(tables_.size()));

        for (const auto& [name, def] : tables_) {
            write_str(os, name);
            write_u8(os, static_cast<uint8_t>(def.billing_mode));
            write_u64(os, def.creation_epoch_seconds);

            write_u32(os, static_cast<uint32_t>(def.key_schema.size()));
            for (const auto& ks : def.key_schema) {
                write_str(os, ks.attribute_name);
                write_u8(os, static_cast<uint8_t>(ks.key_type));
            }

            write_u32(os, static_cast<uint32_t>(def.attribute_definitions.size()));
            for (const auto& [attr_name, attr_type] : def.attribute_definitions) {
                write_str(os, attr_name);
                write_u8(os, static_cast<uint8_t>(attr_type));
            }

            // v2: TTL block — has_ttl flag, then (enabled, attribute_name).
            if (def.ttl_specification) {
                write_u8(os, 1);
                write_u8(os, def.ttl_specification->enabled ? 1 : 0);
                write_str(os, def.ttl_specification->attribute_name);
            } else {
                write_u8(os, 0);
            }

            // v3: secondary indexes. A key schema + projection block per index.
            auto write_key_schema = [&](const std::vector<core::KeySchemaElement>& schema) {
                write_u32(os, static_cast<uint32_t>(schema.size()));
                for (const auto& ks : schema) {
                    write_str(os, ks.attribute_name);
                    write_u8(os, static_cast<uint8_t>(ks.key_type));
                }
            };
            auto write_projection = [&](const core::Projection& p) {
                write_u8(os, static_cast<uint8_t>(p.projection_type));
                write_u32(os, static_cast<uint32_t>(p.non_key_attributes.size()));
                for (const auto& a : p.non_key_attributes) write_str(os, a);
            };
            write_u32(os, static_cast<uint32_t>(def.global_secondary_indexes.size()));
            for (const auto& gsi : def.global_secondary_indexes) {
                write_str(os, gsi.index_name);
                write_key_schema(gsi.key_schema);
                write_projection(gsi.projection);
            }
            write_u32(os, static_cast<uint32_t>(def.local_secondary_indexes.size()));
            for (const auto& lsi : def.local_secondary_indexes) {
                write_str(os, lsi.index_name);
                write_key_schema(lsi.key_schema);
                write_projection(lsi.projection);
            }
        }
    }
    // Atomically replace so a crash mid-write never leaves a corrupt catalog.
    std::error_code ec;
    std::filesystem::rename(tmp_path, metadata_path_, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
    }
}

void TableManager::load_metadata() {
    if (!std::filesystem::exists(metadata_path_)) return;
    std::ifstream is(metadata_path_, std::ios::binary);
    if (!is) return;

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t table_count = 0;
    if (!read_u32(is, magic) || !read_u32(is, version) || !read_u32(is, table_count)) return;
    if (magic != kMetadataMagic || version < 1 || version > kMetadataVersion) {
        std::cerr << "TableManager: unrecognized metadata file, ignoring: " << metadata_path_ << std::endl;
        return;
    }

    std::map<std::string, core::TableDefinition, core::StringViewLess> loaded;
    for (uint32_t i = 0; i < table_count; ++i) {
        core::TableDefinition def;
        uint8_t billing = 0;
        if (!read_str(is, def.table_name) || !read_u8(is, billing) ||
            !read_u64(is, def.creation_epoch_seconds)) {
            return;  // truncated; keep whatever the constructor started with (empty)
        }
        def.billing_mode = static_cast<core::BillingMode>(billing);

        uint32_t ks_count = 0;
        if (!read_u32(is, ks_count)) return;
        for (uint32_t j = 0; j < ks_count; ++j) {
            core::KeySchemaElement ks;
            uint8_t kt = 0;
            if (!read_str(is, ks.attribute_name) || !read_u8(is, kt)) return;
            ks.key_type = static_cast<core::KeyType>(kt);
            def.key_schema.push_back(std::move(ks));
        }

        uint32_t ad_count = 0;
        if (!read_u32(is, ad_count)) return;
        for (uint32_t j = 0; j < ad_count; ++j) {
            std::string attr_name;
            uint8_t at = 0;
            if (!read_str(is, attr_name) || !read_u8(is, at)) return;
            def.attribute_definitions[attr_name] = static_cast<core::AttributeType>(at);
        }

        if (version >= 2) {
            uint8_t has_ttl = 0;
            if (!read_u8(is, has_ttl)) return;
            if (has_ttl != 0) {
                core::TimeToLiveSpecification spec;
                uint8_t enabled = 0;
                if (!read_u8(is, enabled) || !read_str(is, spec.attribute_name)) return;
                spec.enabled = enabled != 0;
                def.ttl_specification = spec;
            }
        }

        if (version >= 3) {
            auto read_key_schema = [&](std::vector<core::KeySchemaElement>& schema) -> bool {
                uint32_t n = 0;
                if (!read_u32(is, n)) return false;
                for (uint32_t k = 0; k < n; ++k) {
                    core::KeySchemaElement ks;
                    uint8_t kt = 0;
                    if (!read_str(is, ks.attribute_name) || !read_u8(is, kt)) return false;
                    ks.key_type = static_cast<core::KeyType>(kt);
                    schema.push_back(std::move(ks));
                }
                return true;
            };
            auto read_projection = [&](core::Projection& p) -> bool {
                uint8_t pt = 0;
                uint32_t n = 0;
                if (!read_u8(is, pt) || !read_u32(is, n)) return false;
                p.projection_type = static_cast<core::ProjectionType>(pt);
                for (uint32_t k = 0; k < n; ++k) {
                    std::string a;
                    if (!read_str(is, a)) return false;
                    p.non_key_attributes.push_back(std::move(a));
                }
                return true;
            };
            uint32_t gsi_count = 0;
            if (!read_u32(is, gsi_count)) return;
            for (uint32_t g = 0; g < gsi_count; ++g) {
                core::GlobalSecondaryIndex gsi;
                if (!read_str(is, gsi.index_name) || !read_key_schema(gsi.key_schema) ||
                    !read_projection(gsi.projection)) return;
                def.global_secondary_indexes.push_back(std::move(gsi));
            }
            uint32_t lsi_count = 0;
            if (!read_u32(is, lsi_count)) return;
            for (uint32_t li = 0; li < lsi_count; ++li) {
                core::LocalSecondaryIndex lsi;
                if (!read_str(is, lsi.index_name) || !read_key_schema(lsi.key_schema) ||
                    !read_projection(lsi.projection)) return;
                def.local_secondary_indexes.push_back(std::move(lsi));
            }
        }

        loaded[def.table_name] = std::move(def);
    }
    tables_ = std::move(loaded);
}

} // namespace cynamodb::engine
