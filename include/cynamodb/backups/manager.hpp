#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <cynamodb/core/schema.hpp>
#include <cynamodb/engine/storage_engine.hpp>

namespace cynamodb::backups {

struct BackupSummary {
    std::string backup_arn;
    std::string backup_name;
    uint64_t backup_creation_datetime;
    uint64_t backup_expiry_datetime;
    uint64_t backup_size_bytes;
    std::string backup_status; // CREATING, AVAILABLE, DELETED
    std::string backup_type;   // USER, SYSTEM
    std::string table_name;
    std::string table_id;
    std::string table_arn;
};

struct BackupDescription {
    BackupSummary backup_summary;
    core::TableDefinition table_metadata;
};

struct BackupSnapshot {
    BackupDescription description;
    std::vector<engine::StorageEngine::AttributeMap> items;
};

class BackupManager {
public:
    explicit BackupManager(const std::string& backups_dir);

    std::optional<BackupDescription> create_backup(
        const std::string& table_name,
        const std::string& backup_name,
        const core::TableDefinition& table_def,
        const std::vector<engine::StorageEngine::AttributeMap>& items);

    bool delete_backup(const std::string& backup_arn);
    std::optional<BackupDescription> describe_backup(const std::string& backup_arn);
    std::vector<BackupSummary> list_backups(const std::string& table_name = "");
    std::optional<BackupSnapshot> restore_backup(const std::string& backup_arn);

private:
    std::string backups_dir_;
    std::string metadata_path_;
    std::map<std::string, BackupDescription> backups_;
    std::map<std::string, std::vector<engine::StorageEngine::AttributeMap>> items_;
    uint64_t next_sequence_ = 1;
    std::shared_mutex mutex_;

    void load_metadata();
    void save_metadata();
    std::string snapshot_path(const std::string& arn) const;
    void write_snapshot(const std::string& arn, const BackupDescription& desc,
                        const std::vector<engine::StorageEngine::AttributeMap>& items);
    std::string generate_backup_arn(const std::string& table_name, uint64_t timestamp, uint64_t sequence);
};

} // namespace cynamodb::backups
