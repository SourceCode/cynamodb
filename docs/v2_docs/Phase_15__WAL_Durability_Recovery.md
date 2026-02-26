# Phase 15: Durability: WAL Checksumming & Rapid Recovery

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Data durability is paramount. A crash should never result in silent data corruption or long recovery times. This phase hardens the Write-Ahead Log (WAL) with per-record checksums and implements a parallel recovery engine that can replay the log at near-SSD-speed.

## Technical Definition
*   **WAL Checksumming:** Add a CRC32C or HighwayHash to every log record and block.
*   **Parallel Recovery:** Replay WAL segments from multiple tables simultaneously using the `Scheduler`.
*   **Atomic Sync (fdatasync):** Optimize the disk sync path for low-latency writes while ensuring durability.

## Reference Files
*   `include/cynamodb/engine/lsm/wal.hpp`
*   `src/engine/lsm/wal_manager.cpp`
*   `src/engine/recovery/manager.cpp`

## Expanded Tasks
1.  **WAL Format Versioning:** Define `WALHeader`. It must contain a 4-byte `MagicNumber` (e.g., `0xCYWAL`), a 2-byte `Version` (e.g., `2`), and the `TableId`. Reject any WAL with a mismatch.
2.  **Record Checksumming:** Every WAL record must be followed by a `uint32_t checksum`. The checksum covers the record header and the body.
3.  **SSE4.2/NEON CRC32:** Use `_mm_crc32_u32` on x86 or `__crc32cw` on ARM. This allows checksumming 1GB of data in < 100ms.
4.  **WAL Block Alignment:** Define `WAL_BLOCK_SIZE = 4096`. If a record spans across a block boundary, ensure the remaining bytes in the current block are padded with zeros to keep the next record aligned.
5.  **Batched fsync:** Implement `GroupCommit`. When a thread writes to the WAL, it waits on a `condition_variable`. A dedicated `WALSyncThread` calls `fdatasync()` every 5ms (or when 64KB is pending) and notifies all waiting threads.
6.  **WAL File Rotation:** When a WAL file reaches 64MB, create a new one: `table_001_seq_002.wal`. Keep the old one until the `Manifest` confirms its data is in an SSTable.
7.  **WAL Parallelism:** Every `TableManager` instance gets its own `WALWriter`. This eliminates global lock contention on the WAL during multi-table `BatchWriteItem` calls.
8.  **Recovery Manager Setup:** Implement `RecoveryManager::scan_directory()`. It builds a map of `TableId -> List<WALFile>`, sorted by the sequence number in the filename.
9.  **Concurrent Log Replay:** For each table, submit a `RecoveryTask` to the `Scheduler`. This allows an 8-core machine to replay 8 tables simultaneously, saturating the NVMe bandwidth.
10. **Partial Write Detection:** During recovery, if a checksum mismatch is found at the *end* of a WAL file, assume it's a partial write from a crash and truncate the file at the last good record.
11. **Idempotent Replay:** In `MemTable::apply_wal_record()`, check the record's `SequenceNumber`. If `seq <= memtable.last_seq`, skip the record to avoid double-application.
12. **WAL Compression:** (Optional) Support `LZ4_compress_fast`. If enabled, the WAL record header must include a `compressed_size` field. This is beneficial for high-throughput numeric data.
13. **Checksum Verification on Read:** In `WALReader`, verify the checksum for every record. If a mismatch is found in the *middle* of a file, stop and return a `DataCorruptionError`.
14. **Direct I/O for WAL:** Implement `O_DIRECT` for the `WALWriter`. This requires the write buffer to be 4096-byte aligned and multiple of 4096 bytes. This avoids the OS page cache latency.
15. **WAL Indexing:** (Advanced) Maintain an in-memory `WALIndex` that stores `{SequenceNumber, FileOffset}` for every 1000th record. This allows "Skipping" ahead during recovery if only a partial range is needed.
16. **WAL Truncation:** Implement `WALManager::purge_old_logs(upto_seq)`. This is called by the `Compactor` after an L0 SSTable is successfully committed to the `Manifest`.
17. **Recovery Progress Metric:** Log: `[Recovery] Table 'User' (45%): Replayed 10,000/22,000 records. 2s remaining.`
18. **Corruption Recovery:** Implement a `--danger-skip-corruption` flag for the binary. If set, the engine will skip bad records instead of stopping, allowing for manual data rescue.
19. **Disk Full Safety:** Use `fstatvfs()` to check available space before every WAL write. If < 100MB, return `InternalServerError: Disk Full` and stop all write operations.
20. **WAL Pre-allocation:** When starting a new WAL file, call `posix_fallocate(fd, 0, 64MB)`. This ensures the space is reserved and prevents disk fragmentation.
21. **Recovery Order:** The `RecoveryManager` must first replay the `Manifest` file to restore the `VersionSet`. Then it starts the parallel replay of the WAL files for all active tables.
22. **Parallel Manifest Update:** If 5 compactions finish at the same time, the `ManifestWriter` should batch all 5 `VersionEdit` records into a single `fdatasync` call.
23. **Test Coverage - Crash Recovery:** Use `scripts/simulate_crash.sh`. It runs a loop: `Insert 1000 items; kill -9; restart; verify 1000 items exist.`
24. **Test Coverage - Checksum Failure:** Use a script to write random bytes into the middle of a `.wal` file. Verify the engine refuses to start and logs a `ChecksumMismatch` error.
25. **Validation:** Use `dd` to measure the raw sequential write speed of the disk. Verify the `WALWriter` achieves at least 80% of that speed for large writes.

## Validation Criteria
*   **Durability:** No data is lost after a `SIGKILL` if `fdatasync` was called.
*   **Performance:** WAL write overhead (including checksum and sync) is < 100 microseconds for a 1KB write.
*   **Integrity:** 100% detection of bit-flips in the WAL using CRC32C.
