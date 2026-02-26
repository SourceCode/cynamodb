# Phase 10: Secondary Indexes: GSI Propagation & Consistency

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Global Secondary Indexes (GSIs) are independent tables that store a projected subset of data from the base table, partitioned by a different key. GSIs are eventually consistent. This phase implements the background propagation engine, projection logic, and backfilling mechanism for GSIs.

## Technical Definition
*   **Propagation Engine:** An asynchronous worker that reads from the base table's WAL or Stream and applies updates to the GSI.
*   **Projection Logic:** Filters attributes from the base item according to the GSI's `ProjectionType` (KEYS_ONLY, INCLUDE, ALL).
*   **Backfilling:** A background process that populates a new GSI from existing data in the base table without blocking writes.

## Reference Files
*   `include/cynamodb/engine/lsm/gsi_manager.hpp`
*   `src/engine/lsm/gsi_manager.cpp`
*   `include/cynamodb/engine/table_manager.hpp`

## Expanded Tasks
1.  **GSI Metadata Schema:** Define `GlobalSecondaryIndexMetadata` struct. Include `IndexName`, `KeySchema`, `Projection` (Type and NonKeyAttributes), and `ProvisionedThroughput`.
2.  **GSI Table Creation:** Implement `create_gsi_storage(tableName, indexName)`. This creates a standard `TableManager` instance but marks it as an internal GSI table in the metadata.
3.  **Propagation Worker:** Implement `GSIPropagationWorker`. It must subscribe to the `WALManager`'s "Commit" events and receive a batch of `WALRecord`s.
4.  **Projection Filtering:** Implement `project_item(baseItem, projection)`. If `KEYS_ONLY`, only include PK/SK of base and PK/SK of GSI. If `INCLUDE`, add specified attributes. If `ALL`, copy everything.
5.  **GSI Upsert Logic:** If a `PutItem` happens on the base table: 1. Project the new item. 2. Fetch the *old* item (if any) from the base table. 3. If keys changed, `Delete` the old GSI item and `Put` the new one.
6.  **GSI Delete Logic:** For `DeleteItem` on the base table, find the GSI keys in the deleted item and issue a `DeleteItem` to the GSI storage.
7.  **Async Queue for GSI:** Use a `lock_free_queue<GSIUpdate>` between the write path and the worker. If the queue is full, the worker must signal the `CapacityManager` to throttle base table writes.
8.  **Batch GSI Writes:** Group multiple GSI updates (e.g., from a base table `BatchWriteItem`) and use the GSI's own `internal_batch_write` to apply them efficiently.
9.  **Backfilling Manager:** Implement `GSIBackfillManager`. Use a `Scan` with a large buffer size (e.g., 10MB) to read base items and project them into the GSI in 25-item batches.
10. **Index Status Management:** Transition the index through `CREATING` (backfilling) -> `ACTIVE`. Only allow queries when the status is `ACTIVE`.
11. **GSI Query Dispatcher:** In `dispatcher.cpp`, if `IndexName` is present in a `Query` request, find the corresponding GSI `TableManager` and redirect the call.
12. **GSI Throughput (WCU):** Implement `check_gsi_capacity(indexMetadata)`. GSI writes must consume the GSI's own provisioned WCU. If exhausted, the propagation worker retries with backoff.
13. **Eventually Consistent Reads:** Force `ConsistentRead=false` for all GSI queries. If a user provides `true`, return `ValidationException: Consistent reads are not supported on GSIs`.
14. **GSI Sequence Numbers:** Include the base table's `SequenceNumber` in every GSI record. The GSI engine must ignore an update if the GSI already contains a record with a higher sequence number for that key.
15. **Storage Overhead Monitoring:** Add `Metrics::GSI_STORAGE_BYTES`. Sum the sizes of all internal GSI SSTables.
16. **Constraint: Key Size:** Validate GSI keys during base table writes. If an attribute used as a GSI key exceeds 2KB, do NOT propagate it to the GSI (Sparse Index behavior).
17. **Constraint: Projection Size:** If an item's projected size for a GSI exceeds 400KB, propagate as much as possible but log a warning (matching DDB's overflow behavior).
18. **GSI Deletion:** When a GSI is deleted, the `GSIPropagationWorker` for that index is stopped, and the directory containing the GSI's SSTables is deleted recursively.
19. **Propagation Delay Metric:** Implement `Metrics::GSI_PROPAGATION_LAG_MS`. Calculate as `now() - walRecord.timestamp`.
20. **GSI Recovery:** Store the `LastProcessedWALSequenceNumber` in the GSI's own `Manifest`. On restart, the worker resumes from this point.
21. **Sparse Index Support:** If a base item is missing either the Partition Key or Sort Key of the GSI, the `project_item` function must return `std::nullopt`, and no GSI write occurs.
22. **Backfill Throttling:** The `GSIBackfillManager` should use a `TokenBucket` to limit its own WCU consumption to 50% of the GSI's capacity to leave room for live updates.
23. **Test Coverage - Multi-GSI:** Create a table with 20 GSIs (the AWS limit). Verify that a single `PutItem` on the base table correctly propagates to all 20 GSIs within 1 second.
24. **Test Coverage - Key Change:** Test: `PutItem(PK=1, AttrA=10)`. GSI uses `AttrA` as its PK. Then `UpdateItem(PK=1, SET AttrA=20)`. Verify GSI now has `PK=20` and `PK=10` is gone.
25. **Validation:** Use `wrk` to saturate the base table writes and verify that GSI lag remains stable and doesn't grow indefinitely.

## Validation Criteria
*   **Correctness:** GSI data eventually matches the base table (with projections).
*   **Performance:** Base table write latency is not significantly affected by GSI propagation (since it's async).
*   **Compliance:** Passes all GSI-related tests in `tests/test_schema.cpp`.
