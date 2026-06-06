#include <cynamodb/backups/manager.hpp>
#include <cynamodb/json/serializer.hpp>

#include <simdjson.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>

namespace cynamodb::backups {

namespace {

std::string attribute_type_str(core::AttributeType t) {
    switch (t) {
        case core::AttributeType::N: return "N";
        case core::AttributeType::B: return "B";
        default: return "S";
    }
}

// Serializes a backup snapshot as a JSON document whose "table" block matches what
// JsonParser::parse_table_definition expects, so restore can reuse the parser.
std::string serialize_snapshot(const BackupDescription& desc,
                               const std::vector<engine::StorageEngine::AttributeMap>& items) {
    const auto& def = desc.table_metadata;
    std::string out = "{\"summary\":{\"BackupArn\":\"" + desc.backup_summary.backup_arn +
                      "\",\"BackupName\":\"" + desc.backup_summary.backup_name +
                      "\",\"BackupCreationDateTime\":" + std::to_string(desc.backup_summary.backup_creation_datetime) +
                      ",\"TableName\":\"" + desc.backup_summary.table_name +
                      "\",\"BackupSizeBytes\":" + std::to_string(desc.backup_summary.backup_size_bytes) + "},";
    out += "\"table\":{\"TableName\":\"" + def.table_name + "\",\"KeySchema\":[";
    for (size_t i = 0; i < def.key_schema.size(); ++i) {
        if (i) out += ",";
        out += "{\"AttributeName\":\"" + def.key_schema[i].attribute_name + "\",\"KeyType\":\"" +
               (def.key_schema[i].key_type == core::KeyType::HASH ? "HASH" : "RANGE") + "\"}";
    }
    out += "],\"AttributeDefinitions\":[";
    bool first = true;
    for (const auto& [name, type] : def.attribute_definitions) {
        if (!first) out += ",";
        out += "{\"AttributeName\":\"" + name + "\",\"AttributeType\":\"" + attribute_type_str(type) + "\"}";
        first = false;
    }
    out += "]},\"items\":[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += json::JsonSerializer::serialize_item(items[i]);
    }
    out += "]}";
    return out;
}

}  // namespace

BackupManager::BackupManager(const std::string& backups_dir) : backups_dir_(backups_dir) {
    std::filesystem::create_directories(backups_dir_);
    metadata_path_ = backups_dir_ + "/metadata.bin";
    load_metadata();
}

std::string BackupManager::generate_backup_arn(const std::string& table_name, uint64_t timestamp, uint64_t sequence) {
    std::stringstream ss;
    ss << "arn:aws:dynamodb:ddblocal:000000000000:table/" << table_name << "/backup/";
    ss << timestamp << "-" << std::setfill('0') << std::setw(8) << sequence;
    return ss.str();
}

std::string BackupManager::snapshot_path(const std::string& arn) const {
    std::string safe = arn;
    std::replace(safe.begin(), safe.end(), '/', '_');
    std::replace(safe.begin(), safe.end(), ':', '_');
    return backups_dir_ + "/" + safe + ".backup.json";
}

void BackupManager::write_snapshot(const std::string& arn, const BackupDescription& desc,
                                   const std::vector<engine::StorageEngine::AttributeMap>& items) {
    const std::string tmp = snapshot_path(arn) + ".tmp";
    {
        std::ofstream os(tmp, std::ios::binary | std::ios::trunc);
        if (!os) return;
        os << serialize_snapshot(desc, items);
    }
    std::error_code ec;
    std::filesystem::rename(tmp, snapshot_path(arn), ec);
    if (ec) std::filesystem::remove(tmp, ec);
}

std::optional<BackupDescription> BackupManager::create_backup(
    const std::string& table_name,
    const std::string& backup_name,
    const core::TableDefinition& table_def,
    const std::vector<engine::StorageEngine::AttributeMap>& items) {

    std::unique_lock lock(mutex_);
    uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();

    std::string arn = generate_backup_arn(table_name, now, next_sequence_++);

    BackupDescription desc;
    desc.backup_summary.backup_arn = arn;
    desc.backup_summary.backup_name = backup_name;
    desc.backup_summary.backup_creation_datetime = now;
    desc.backup_summary.backup_status = "AVAILABLE";
    desc.backup_summary.backup_type = "USER";
    desc.backup_summary.table_name = table_name;
    desc.backup_summary.backup_size_bytes = items.size() * 1024;  // approximate
    desc.table_metadata = table_def;

    backups_[arn] = desc;
    items_[arn] = items;
    write_snapshot(arn, desc, items);
    save_metadata();
    return desc;
}

bool BackupManager::delete_backup(const std::string& backup_arn) {
    std::unique_lock lock(mutex_);
    if (backups_.erase(backup_arn)) {
        items_.erase(backup_arn);
        std::error_code ec;
        std::filesystem::remove(snapshot_path(backup_arn), ec);
        save_metadata();
        return true;
    }
    return false;
}

std::optional<BackupDescription> BackupManager::describe_backup(const std::string& backup_arn) {
    std::shared_lock lock(mutex_);
    auto it = backups_.find(backup_arn);
    if (it == backups_.end()) return std::nullopt;
    return it->second;
}

std::vector<BackupSummary> BackupManager::list_backups(const std::string& table_name) {
    std::shared_lock lock(mutex_);
    std::vector<BackupSummary> result;
    for (const auto& [arn, desc] : backups_) {
        if (table_name.empty() || desc.backup_summary.table_name == table_name) {
            result.push_back(desc.backup_summary);
        }
    }
    return result;
}

std::optional<BackupSnapshot> BackupManager::restore_backup(const std::string& backup_arn) {
    std::shared_lock lock(mutex_);
    auto it = backups_.find(backup_arn);
    if (it == backups_.end()) return std::nullopt;

    BackupSnapshot snapshot;
    snapshot.description = it->second;
    auto iit = items_.find(backup_arn);
    if (iit != items_.end()) snapshot.items = iit->second;
    return snapshot;
}

// Backups are persisted as one JSON snapshot file each; the catalog is rebuilt by
// scanning them on startup, so backups survive a restart.
void BackupManager::load_metadata() {
    if (!std::filesystem::exists(backups_dir_)) return;
    simdjson::dom::parser parser;
    uint64_t max_seq = 0;
    for (const auto& entry : std::filesystem::directory_iterator(backups_dir_)) {
        if (entry.path().extension() != ".json") continue;
        const std::string name = entry.path().filename().string();
        if (name.find(".backup.json") == std::string::npos) continue;

        simdjson::dom::element doc;
        auto json = simdjson::padded_string::load(entry.path().string());
        if (json.error() != simdjson::SUCCESS) continue;
        if (parser.parse(json.value()).get(doc) != simdjson::SUCCESS) continue;

        try {
            BackupDescription desc;
            std::string_view arn;
            if (doc["summary"]["BackupArn"].get_string().get(arn) != simdjson::SUCCESS) continue;
            desc.backup_summary.backup_arn = std::string(arn);
            std::string_view bname;
            if (doc["summary"]["BackupName"].get_string().get(bname) == simdjson::SUCCESS)
                desc.backup_summary.backup_name = std::string(bname);
            int64_t created = 0;
            if (doc["summary"]["BackupCreationDateTime"].get_int64().get(created) == simdjson::SUCCESS)
                desc.backup_summary.backup_creation_datetime = static_cast<uint64_t>(created);
            std::string_view tname;
            if (doc["summary"]["TableName"].get_string().get(tname) == simdjson::SUCCESS)
                desc.backup_summary.table_name = std::string(tname);
            desc.backup_summary.backup_status = "AVAILABLE";
            desc.backup_summary.backup_type = "USER";

            simdjson::dom::element table_el;
            if (doc["table"].get(table_el) == simdjson::SUCCESS) {
                desc.table_metadata = json::JsonParser::parse_table_definition(table_el);
            }

            std::vector<engine::StorageEngine::AttributeMap> items;
            simdjson::dom::array arr;
            if (doc["items"].get_array().get(arr) == simdjson::SUCCESS) {
                for (auto item_el : arr) {
                    engine::StorageEngine::AttributeMap item;
                    for (auto field : item_el.get_object()) {
                        item[std::string(field.key)] = std::make_shared<core::AttributeValue>(
                            json::JsonParser::parse_attribute_value(field.value));
                    }
                    items.push_back(std::move(item));
                }
            }

            // Track the highest sequence so new ARNs don't collide after reload.
            auto dash = desc.backup_summary.backup_arn.find_last_of('-');
            if (dash != std::string::npos) {
                try { max_seq = std::max<uint64_t>(max_seq, std::stoull(desc.backup_summary.backup_arn.substr(dash + 1))); }
                catch (const std::exception&) {}
            }
            backups_[desc.backup_summary.backup_arn] = std::move(desc);
            items_[std::string(arn)] = std::move(items);
        } catch (const std::exception&) {
            // Skip an unreadable snapshot rather than failing startup.
        }
    }
    next_sequence_ = max_seq + 1;
}

void BackupManager::save_metadata() {
    // Snapshots are self-describing; no separate index file is needed.
}

} // namespace cynamodb::backups
