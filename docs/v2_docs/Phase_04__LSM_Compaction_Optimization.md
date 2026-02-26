# Phase 04: LSM-Tree: Advanced Compaction & Write Amplification Reduction

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
The LSM-Tree is the heart of CynamoDB's storage. Without advanced compaction, the system will eventually suffer from "Compaction Debt," leading to high write amplification and unpredictable read latency. This phase implements Leveled Compaction (similar to RocksDB) and introduces optimizations to minimize disk I/O.

## Technical Definition
*   **Leveled Compaction:** Organizes SSTables into levels (L0, L1, ... Ln). L0 has overlapping ranges; L1+ are non-overlapping.
*   **Write Amplification:** The ratio of bytes written to disk vs bytes written by the user. Goal is < 15.
*   **Parallel Compaction:** Compacting multiple non-overlapping ranges simultaneously.

## Reference Files
*   `include/cynamodb/engine/lsm/compaction.hpp`
*   `src/engine/lsm/compaction_manager.cpp`
*   `include/cynamodb/engine/lsm/sstable.hpp`

## Expanded Tasks
1.  **Level Metadata Management:** Add `uint32_t level` and `uint64_t sequence_number` to `SSTableMetadata`. Update the `Manifest` file format to store which SSTables belong to which level.
2.  **L0 to L1 Strategy:** Implement `should_compact_L0()`. Trigger when L0 has > 4 SSTables. Since L0 files overlap, ALL L0 files plus all overlapping L1 files must be included in a single compaction job.
3.  **Non-Overlapping Range Check:** In `compaction_manager.cpp`, implement `get_overlapping_files(level, min_key, max_key)`. For L1+, use binary search on the sorted list of SSTables to find overlaps in O(log N).
4.  **Multi-Threaded Compactor:** Implement `CompactionJob` as a task for the `Scheduler`. Ensure that multiple jobs can run on different levels (e.g., L1->L2 and L3->L4) as long as their key ranges do not overlap.
5.  **Compaction Picker Optimization:** Calculate a `compaction_score` for each level: `current_size / max_level_size`. Pick the level with the highest score > 1.0. For L0, the score is `file_count / 4`.
6.  **Tombstone Sweeping:** During the merge process in `MergingIterator`, if an item is a tombstone and its sequence number is less than the `EarliestSnapshotSequenceNumber`, drop it entirely to reclaim space.
7.  **SSTable Iteration Optimization:** Implement `MergingIterator` using `std::priority_queue` with a custom comparator that handles sequence numbers (higher sequence number wins for the same key).
8.  **Block-Size Awareness:** When writing new SSTables during compaction, ensure the `BlockBuilder` flushes at exactly 4096 bytes (or a multiple) to align with NVMe physical sectors.
9.  **Write Buffer Management:** Use a 64MB `std::pmr::vector<std::byte>` as a write-back buffer for compaction. This prevents frequent small `write()` calls and allows the OS to optimize disk throughput.
10. **Bloom Filter Rebuilding:** After a compaction job finishes, generate a new Bloom filter for the resulting SSTable. Use 10 bits per key to target a < 1% false positive rate.
11. **Compaction Throttling:** Implement `set_compaction_speed(mb_per_sec)`. Use a simple sleep-based rate limiter in the `SSTableWriter` to prevent compaction from saturating disk I/O during peak hours.
12. **Background Flush Optimization:** Ensure `flush_memtable()` creates an L0 SSTable. Use `std::async` to perform the I/O, allowing the main thread to immediately start a new MemTable.
13. **Incremental Compaction:** Break large compaction jobs (e.g., L4->L5) into smaller "Sub-compactions" based on key ranges to avoid blocking a single thread for minutes.
14. **Space Amplification Monitoring:** Add `Metrics::SPACE_AMPLIFICATION`. Calculate as `total_disk_size / total_live_data_size`. Alert if it exceeds 2.0.
15. **Size-Tiered Fallback:** If a table is configured for "Heavy Writes," allow L0 to grow up to 20 files using Size-Tiered logic before forcing a move to L1.
16. **Cold Data Separation:** Implement a "Cold Level" (e.g., L6). Compaction into L6 should use Zstd level 9 compression, whereas L1 uses LZ4, optimizing for space on rarely-accessed data.
17. **Manual Compaction API:** Add `admin_compact_table(TableName, TargetLevel)`. This allows users to manually trigger space reclamation after a large batch deletion.
18. **SSTable Checksumming:** Use `crc32c_append` on every 4KB block. Store the checksum in a 4-byte trailer at the end of each block. Verify on every read during compaction.
19. **Compaction Statistics:** Log JSON-formatted stats: `{"job_id": 123, "input_bytes": 1048576, "output_bytes": 800000, "duration_ms": 450}`.
20. **Atomic Level Switch:** Update the `VersionSet` in memory and then sync the `Manifest` file. Only after `fdatasync(manifest_fd)` succeeds should the old SSTables be scheduled for deletion.
21. **Manifest File Hardening:** Implement a `ManifestWriter` that uses a rolling log format. Periodically "Compact" the manifest by writing a snapshot of the current state to a new file.
22. **Overlapping Key Optimization:** If `get_overlapping_files(L_next, min, max)` returns zero files, perform a "Trivial Move" by simply updating the level in the `Manifest` without reading or writing data.
23. **Data Compression Selection:** Implement `CompressionPicker`. Use `CompressionType::None` for L0/L1 to minimize CPU during burst writes, and `CompressionType::Zstd` for L2+.
24. **Compaction Stress Test:** In `tests/test_engine.cpp`, write 1GB of data with many overlapping keys. Verify that the total number of SSTables settles to a predictable number based on the leveled strategy.
25. **Validation:** Use `ls -lh` on the data directory. Verify that for a table with 100k items, the total disk usage is within 1.5x of the raw data size (due to compaction efficiency).

## Validation Criteria
*   **Write Amplification:** Average WA remains below 20 for a sustained 1-hour write load.
*   **Read Performance:** Point lookups in L1+ levels involve at most one SSTable per level.
*   **Recovery:** The `Manifest` system can recover the exact database state after a crash during compaction.
