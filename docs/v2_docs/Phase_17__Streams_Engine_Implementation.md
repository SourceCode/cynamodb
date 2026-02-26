# Phase 17: Streams Engine: Sharding, Sequencing, and Retention

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
DynamoDB Streams provide a time-ordered sequence of all item-level changes in a table. This is critical for replication, triggers, and change data capture (CDC). This phase implements the Streams engine, including shard management, log-structured storage, and a 24-hour retention policy.

## Technical Definition
*   **Shard Management:** Divide the stream into shards based on throughput, each with a parent-child relationship for lineage.
*   **Ordered Sequence:** Ensure that updates to the same item appear in the stream in the exact order they were applied to the table.
*   **Log-Structured Storage:** Use a dedicated LSM-like engine to store stream records efficiently.

## Reference Files
*   `include/cynamodb/streams/manager.hpp`
*   `src/streams/manager.cpp`
*   `include/cynamodb/streams/shard.hpp`

## Expanded Tasks
1.  **Stream Metadata Schema:** Define `StreamSpecification`. Include `StreamEnabled` (bool) and `StreamViewType` (KEYS_ONLY, NEW_IMAGE, OLD_IMAGE, NEW_AND_OLD_IMAGES).
2.  **Stream Record Format:** Implement `StreamRecord`. It must include `eventID` (unique string), `eventName` (INSERT, MODIFY, REMOVE), `eventSource` (aws:dynamodb), and the `dynamodb` object containing the item images.
3.  **Stream Storage Engine:** Implement `StreamStorage`. It should be a simplified LSM-tree where the key is `ShardID + SequenceNumber`. This allows for high-speed sequential writes and range scans for `GetRecords`.
4.  **Sequence Number Generation:** Implement `generate_stream_seq()`. It should be `TID + SubStep`. This ensures that even for a single `UpdateItem` that produces multiple stream records (e.g., LSI updates), the sequence is strictly ordered.
5.  **Shard Division Logic:** In `StreamManager`, monitor `shard_throughput`. If a shard exceeds 2MB/sec or 1000 records/sec, close it and create two new child shards with non-overlapping key ranges.
6.  **Shard Lineage Tracking:** Update `Shard` struct to include `SequenceNumberRange` and `ParentShardId`. This is returned in `DescribeStream` to help the client understand the shard hierarchy.
7.  **Stream View Types:** Implement `filter_images()`. If `KEYS_ONLY`, strip everything except PK/SK. If `NEW_IMAGE`, only include the item as it exists *after* the operation.
8.  **GetShardIterator API:** Implement `get_iterator()`. `TRIM_HORIZON` points to the oldest available record. `LATEST` points to the newest. `AT_SEQUENCE_NUMBER` uses binary search on the `StreamStorage` index.
9.  **GetRecords API:** Implement `fetch_records(iterator)`. Read up to 1000 records or 10MB from the current `ShardID`. Return a `NextShardIterator`. If the shard is closed and all records read, return `null`.
10. **Retention Engine:** Create `StreamCleanerTask`. It runs every hour and identifies all `StreamStorage` blocks where `max_timestamp < (now - 24 hours)`. Delete these blocks from disk.
11. **Stream ARN Generation:** Implement `build_stream_arn()`. Format: `arn:aws:dynamodb:region:account:table/TableName/stream/2023-10-27T12:00:00.000`.
12. **DescribeStream API:** Implement the handler. It returns the `StreamDescription` including all shards that haven't expired yet. Support pagination if a stream has thousands of shards.
13. **Stream Write Path:** In `TableManager::commit_write`, if streams are enabled, generate the `StreamRecord` and call `stream_manager->append(record)`. This must be part of the same `BatchWALRecord` for atomicity.
14. **Stream Write Batching:** The `StreamManager` should use a 4MB write buffer. Flush to disk only when full or when the `WALSyncThread` triggers a sync.
15. **Checkpointing Support:** Add `internal_checkpoint(client_id, shard_id, seq)`. This allows internal replication workers to resume from the last known good record.
16. **Stream Compression:** Use `LZ4` for stream record bodies. Since records often have high redundancy (e.g., repeating attribute names), LZ4 can achieve > 5x compression.
17. **Empty Stream Handling:** If `GetRecords` finds no new data, return an empty list and the *same* iterator (or one with an updated internal timestamp) so the client can poll again.
18. **Stream Health Metrics:** Track `Metrics::STREAM_RECORDS_WRITTEN`, `Metrics::STREAM_STORAGE_SIZE`, and `Metrics::GET_RECORDS_LATENCY`.
19. **Stream Shard Aging:** Even if a shard isn't full, close it after 4 hours of inactivity to ensure that shards don't stay open indefinitely.
20. **Transaction Integration:** In `TransactWriteItems`, all stream records for all items in the transaction must have the same `approximateCreationDateTime` and be adjacent in the stream.
21. **Sequence Number Uniqueness:** Include the `ShardID` as a prefix to the `SequenceNumber` returned to the user to ensure it's globally unique across the entire stream.
22. **Parallel Record Retrieval:** In `GetRecords`, use `Scheduler` to perform the read and the LZ4 decompression in parallel if multiple shards are being polled by the same client.
23. **Test Coverage - Shard Lineage:** Test: Write 10k items to Shard A. Force a split. Verify `DescribeStream` shows Shard A as closed and Shard B/C as its children.
24. **Test Coverage - 24h Retention:** Set retention to 10 seconds. Write a record. Wait 15 seconds. Verify `GetRecords` returns `ExpiredIteratorException`.
25. **Validation:** Verify that the stream storage engine correctly handles "Tombstones" for the stream records themselves when they expire.

## Validation Criteria
*   **Correctness:** Stream records exactly match the updates applied to the base table in the correct order.
*   **Reliability:** Stream data survives a crash and is fully recoverable from the stream log.
*   **Compliance:** Passes all tests in `tests/test_streams_http.cpp`.
