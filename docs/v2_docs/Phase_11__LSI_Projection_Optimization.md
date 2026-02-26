# Phase 11: Secondary Indexes: LSI & Projection Optimization

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Local Secondary Indexes (LSIs) share the same partition key as the base table but have a different sort key. Unlike GSIs, LSIs support strong consistency and share the base table's throughput. This phase implements synchronous LSI updates, optimizes item collection management, and ensures projection efficiency.

## Technical Definition
*   **Synchronous Updates:** LSI updates must happen within the same transaction/write operation as the base item.
*   **Item Collections:** Groups of items with the same partition key across the base table and all LSIs.
*   **Collection Limit:** Enforce the 10GB per-partition limit for tables with LSIs.

## Reference Files
*   `include/cynamodb/engine/lsm/lsi_manager.hpp`
*   `src/engine/lsm/lsi_manager.cpp`
*   `src/engine/table_manager.cpp`

## Expanded Tasks
1.  **LSI Metadata Integration:** Define `LocalSecondaryIndexMetadata`. Ensure it's stored in the same `TableMetadata` block. Check for `LSI.size() <= 5` (AWS limit).
2.  **LSI Synchronous Write Path:** In `TableManager::put_item`, after writing the base item to the MemTable, immediately call `lsi_manager->update_indexes()`. Both must succeed for the call to return 200 OK.
3.  **Key Prefixing for LSIs:** Use a 1-byte prefix: `0x00` for Base items, `0x01` for LSI 1, `0x02` for LSI 2, etc. This keeps all LSI data for the same Partition Key in the same SSTable block for cache locality.
4.  **Projection Logic (LSI):** Implement `project_lsi_item()`. Same logic as GSI but must be optimized for in-line execution during the write path.
5.  **LSI Delete Path:** When a base item is deleted or updated (changing the LSI sort key attribute), the LSI must synchronously remove the old index entry and add the new one.
6.  **Strongly Consistent Query:** In `Query`, if `IndexName` refers to an LSI and `ConsistentRead=true`, the engine performs a standard consistent read using the LSI prefix.
7.  **Item Collection Size Tracking:** In `TableManager`, maintain a `CollectionSizeCache` (a map of PartitionKey -> size). This cache is updated on every write.
8.  **Collection Limit Enforcement:** Before any write to a table with LSIs, check if `CollectionSizeCache[PK] + newItemSize > 10GB`. If so, throw `ItemCollectionSizeLimitExceededException`.
9.  **LSI Update Optimization:** If an `UpdateItem` only modifies attributes that are NOT part of the LSI sort key or its `INCLUDE` projection, skip the LSI update entirely.
10. **Atomic Write Integration:** Combine the base item and all LSI items into a single `BatchWALRecord`. This ensures that even if the process crashes mid-update, the recovery engine applies all or none.
11. **LSI Query Dispatcher:** Modify the `LsmIterator` to accept a `key_prefix`. When querying an LSI, the iterator only returns keys matching `prefix + PK`.
12. **Projection Fallback:** If a query on a `KEYS_ONLY` LSI requests an attribute not in the index, the `TableManager` must perform a "fetch" from the base table for each result (Standard DDB behavior).
13. **Throughput Calculation:** LSI writes consume the base table's WCU. A `PutItem` that updates 5 LSIs counts as 6 writes for capacity billing.
14. **LSI Sort Key Validation:** Ensure the attribute used as an LSI sort key matches the type defined in the schema (S, N, or B).
15. **LSI Creation Restriction:** In `CreateTable`, reject the request if LSIs are provided but the table already exists. LSIs cannot be added to an existing table.
16. **Scan Isolation:** Ensure `TableManager::scan()` only returns items with the `0x00` (Base) prefix. LSIs are ignored during full-table scans.
17. **LSI-Specific Metrics:** Add `Metrics::LSI_WRITE_LATENCY` to measure the overhead added by synchronous LSI updates.
18. **Index Key Conflict Resolution:** Since LSIs share a Partition Key, two different base items (different SKs) could have the same LSI Sort Key. Handle this by appending the base SK to the LSI entry for uniqueness.
19. **Bloom Filter for LSIs:** Include the LSI prefix in the Bloom filter hash. This allows `GetItem` to skip LSI entries entirely without reading SSTable blocks.
20. **LSI Iterator Optimization:** Use `MergingIterator::seek_to_prefix(prefix)`. This uses the SSTable index to jump directly to the LSI entries, bypassing all base items.
21. **Projection Size Calculator:** Create `estimate_lsi_entry_size()`. Include the PK, LSI-SK, Base-SK (for uniqueness), and all projected attributes.
22. **Concurrent LSI Updates:** Use `std::for_each(std::execution::par)` if a table has 5 LSIs. This parallelizes the projection and MemTable insertion for each index.
23. **Test Coverage - Strongly Consistent LSI Read:** Test: `PutItem` then immediately `Query(IndexName, ConsistentRead=true)`. Verify 100% success rate under high load.
24. **Test Coverage - 10GB Limit:** Add a test that writes 1MB items to the same PK until the 10,241st item is rejected.
25. **Validation:** Verify that `DescribeTable` correctly reports the `ItemCollectionSize` for a table with LSIs.

## Validation Criteria
*   **Consistency:** LSI reads with `ConsistentRead=true` always reflect the latest base table write.
*   **Reliability:** The 10GB item collection limit is strictly enforced.
*   **Performance:** LSI write overhead is < 0.5ms per index on average.
