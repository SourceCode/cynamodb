# Phase 18: Backup & Restore: Incremental Snapshots & PITR

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Data safety requires backups that can be taken without impacting live performance. This phase implements "On-Demand Backup" (full snapshots) and "Point-In-Time Recovery" (PITR) using incremental WAL-based backups. We will also integrate checksum validation to ensure backup integrity.

## Technical Definition
*   **On-Demand Backup:** A consistent, point-in-time snapshot of the entire table, including all LSIs.
*   **Point-In-Time Recovery (PITR):** A continuous backup of all table changes (from the WAL) to allow recovery to any microsecond within the last 35 days.
*   **Zero-Impact Snapshots:** Use "Hard Link" or "COW" (Copy-on-Write) file system features for near-instant SSTable snapshots.

## Reference Files
*   `include/cynamodb/backups/manager.hpp`
*   `src/backups/manager.cpp`
*   `src/engine/backup/pitr.cpp`

## Expanded Tasks
1.  **Backup Metadata Schema:** Define `BackupSummary`. Include `BackupArn`, `BackupName`, `BackupSizeBytes`, `BackupStatus` (CREATING, AVAILABLE, DELETED), and `BackupType` (USER, SYSTEM).
2.  **On-Demand Backup Logic:** Implement `create_backup(tableName, backupName)`. It must first "Freeze" the table's `VersionSet`, record the current `SequenceNumber`, and then release the lock immediately.
3.  **Hard-Link Snapshot Engine:** Implement `snapshot_sstables()`. For every SSTable in the current `VersionSet`, create a hard link in the `backups/<backupID>/` directory. This is near-instant and consumes 0 extra disk space initially.
4.  **PITR Worker:** Create `PITRArchiver`. It tails the table's WAL. Every time a WAL file is rotated, the archiver copies the *completed* WAL file to a `pitr/<tableName>/` directory.
5.  **Backup Compression:** Implement `export_backup()`. This asynchronously reads the hard-linked SSTables, compresses them with `Zstd`, and bundles them into a single `.tar.zst` or `.cynamobkp` file.
6.  **Backup Checksumming:** For every backup, generate a `SHA256` manifest that lists all files and their individual checksums. This allows the `Restore` operation to detect corruption before starting.
7.  **RestoreTableFromBackup API:** Implement `restore_from_backup()`. It creates a new table, extracts the backup files into the new table's data directory, and generates a fresh `Manifest` starting from the backup's `SequenceNumber`.
8.  **PITR Recovery Logic:** Implement `restore_to_time(tableName, targetTime)`. 1. Find the latest full backup *before* `targetTime`. 2. Restore that backup. 3. Replay all archived WAL segments from `backupTime` to `targetTime`.
9.  **Backup Lifecycle Manager:** Implement `RetentionPolicy`. Automatically delete on-demand backups older than `N` days and PITR logs older than 35 days (AWS standard).
10. **Backup Status Monitoring:** Add `Metrics::BACKUP_IN_PROGRESS_COUNT`. Use `DescribeBackup` to return the percentage of the export process that is complete.
11. **GSI Backup Support:** When a backup is taken, the `BackupManager` must also snapshot the `VersionSet` of all associated GSI tables to ensure they are restorable as a consistent unit.
12. **S3 Integration (Optional):** Implement `S3StorageProvider`. Use the `aws-sdk-cpp` (or raw HTTP) to upload backup chunks using `MultipartUpload`. This is critical for off-site durability.
13. **Restore Throttling:** Add `restore_limit_mbps`. Use a `TokenBucket` in the `RestoreTask` to ensure that a 1TB restore doesn't saturate the disk and starve production traffic.
14. **Snapshot Consistency:** Use the `Manifest`'s `atomic_snapshot()` feature. This ensures that the snapshot contains a consistent set of SSTables that represent a valid state of the database.
15. **Backup Encryption:** Implement `EncryptedBackupStream`. Use `AES-256-GCM` to encrypt the SSTable data as it is being read for the backup export.
16. **DescribeContinuousBackups API:** Implement the handler. Return whether PITR is enabled and the `EarliestRestorableDateTime` (which is the time the first WAL was archived after PITR was enabled).
17. **Incremental SSTable Backup:** Implement `backup_diff()`. Only copy SSTables that were created *after* the previous backup's `SequenceNumber`. This significantly reduces backup time and storage for large tables.
18. **Restore to New Table:** Ensure that `RestoreTableFromBackup` rejects the request if the `TargetTableName` already exists, preventing accidental data overwrites.
19. **Backup Verification:** Implement `verify_backup(backupID)`. It reads the backup's manifest and verifies every file's `SHA256`. It also checks that all required SSTables for a full level-set are present.
20. **PITR Lag Metric:** Track `Metrics::PITR_ARCHIVE_LAG_SEC`. If the WAL archiver falls behind by > 1 hour, trigger a critical alert.
21. **Backup Failure Recovery:** If a `CreateBackup` fails (e.g., Disk Full), the `BackupManager` must automatically delete the partial `backups/<backupID>/` directory.
22. **Parallel Backup Export:** In `export_backup`, use `std::for_each(std::execution::par)` to compress and upload different SSTables in parallel, saturating the CPU and network.
23. **Test Coverage - PITR Restore:** Test: Write 100 items every second for 60s. Restore to `T+30s`. Verify exactly 3000 items exist and no items from `T+31s` onwards are present.
24. **Test Coverage - Large Backup:** Generate 10GB of random data. Measure the time to create a hard-link backup (target: < 500ms) and the time to export it to a compressed file.
25. **Validation:** Use `cynamodb-admin verify --backup-id <id>` to perform a trial restore into a temporary directory and verify the checksums of the restored items.

## Validation Criteria
*   **Correctness:** Restored tables are bit-for-bit identical to the source table at the snapshot time.
*   **Performance:** PITR replay speed exceeds 100MB/sec.
*   **Safety:** Deleting the original table does not affect the integrity of its existing backups (due to hard link behavior or full copies).
