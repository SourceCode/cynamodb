# Phase 07: Transaction Layer: MVCC & Optimistic Concurrency Control

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
DynamoDB's `TransactWriteItems` and `TransactGetItems` require atomicity and isolation across multiple items, potentially across different tables. This phase implements a Multi-Version Concurrency Control (MVCC) layer on top of the LSM-Tree and an Optimistic Concurrency Control (OCC) mechanism for conflict detection.

## Technical Definition
*   **Timestamp-based Isolation:** Assign a global `CommitTimestamp` to every transaction.
*   **Write Intents:** Write items to a "Pending" state (Intents) and commit them atomically by updating a global transaction log.
*   **Conflict Detection:** Verify that none of the items involved in the transaction have been modified since the transaction started.

## Reference Files
*   `include/cynamodb/engine/transactions/manager.hpp`
*   `src/engine/transactions/manager.cpp`
*   `include/cynamodb/engine/lsm/version_set.hpp`

## Expanded Tasks
1.  **Global Transaction ID (TID):** Implement `TIDGenerator` using `std::atomic<uint64_t>`. Combine a 48-bit microsecond timestamp with a 16-bit sequence number to ensure uniqueness even if the system clock drifts slightly.
2.  **Item Versioning:** Add a `_ts` (timestamp) and `_tid` (transaction ID) system attribute to every item in the LSM engine. Use these to determine which version of an item is visible to a given `ReadTimestamp`.
3.  **Snapshot Read:** In `TableManager::get_item()`, accept an optional `Snapshot`. If present, only return items where `item._ts <= snapshot.timestamp`. This ensures a "Point-in-Time" view of the data.
4.  **Transaction Context:** Define `TransactionContext` in `include/cynamodb/engine/transactions/context.hpp`. It must store a `read_set` (keys and versions read) and a `write_set` (new item data).
5.  **Write Intent Log:** Create a dedicated `IntentLog` (a specialized WAL). Every `TransactWriteItems` must first write all its intents to this log with a `State::PENDING` flag before any other work.
6.  **2-Phase Locking (Internal):** For the *Commit Phase* only, use `StripedLock` (a sharded mutex) to lock the keys involved. This prevents two transactions from committing changes to the same key at the exact same nanosecond.
7.  **Conflict Check (OCC):** Before moving from `PENDING` to `COMMITTED`, check if any key in the `read_set` has a newer `_ts` in the storage engine than what was originally read. If so, abort with `TransactionCanceledException`.
8.  **Atomic Commit:** To commit, write a single `CommitRecord` to the `Manifest`. This record contains the TID and the final `CommitTimestamp`. Once this record is `fsync`'d, the transaction is officially committed.
9.  **Transaction Cleanup:** Implement a background `MvccCleaner`. It finds versions where `_ts < (current_time - retention_period)` and marks them for deletion in the next compaction job.
10. **Transaction Isolation Levels:** Implement `Serializable` isolation by ensuring that any `ConditionCheck` or `Get` within the transaction also adds the key to the `read_set` for conflict detection.
11. **Idempotency Token Support:** Create a `std::pmr::unordered_map<string, TransactionResult>` for `ClientRequestToken`. Store results for 10 minutes (AWS spec) and return the cached result for duplicate tokens.
12. **Read-Only Transaction Optimization:** For `TransactGetItems`, simply take a global `ReadTimestamp` and perform standard reads. No intents, no locks, and no conflict detection are needed, maximizing throughput.
13. **Write Intent Storage:** Store pending write intents in a special `IntentMemTable`. During a normal `GetItem`, the engine must check both the main MemTable and the `IntentMemTable` (ignoring uncommitted intents).
14. **Deadlock Prevention:** Use the "Wait-Die" scheme: if T1 requests a lock held by T2, T1 only waits if it is older (smaller TID) than T2. Otherwise, T1 aborts (Dies). This prevents circular wait.
15. **Conditional Check Integration:** When evaluating a `ConditionExpression` in a transaction, use the transaction's own `Snapshot`. This ensures the condition is checked against a consistent view of the database.
16. **Constraint Validation:** In `TransactionManager`, check if `items.size() > 25`. If so, immediately return `ValidationException` to avoid wasting resources on an invalid transaction.
17. **Partial Commit Prevention:** Use the `IntentLog` sequence number. During recovery, if a `CommitRecord` is missing for a TID found in the `IntentLog`, the recovery engine must "Roll Back" those items.
18. **Metrics:** Add `Metrics::TRANSACTION_ABORT_COUNT` and `Metrics::TRANSACTION_LATENCY`. Track "Conflict Hotspots" (keys that cause frequent aborts) to help with debugging.
19. **Snapshot Isolation Test:** In `tests/test_transactions_http.cpp`, launch 10 threads. Each thread: Read X, Y; Write X+1, Y-1 (Atomic Swap). Verify that the total `X + Y` remains constant across all iterations.
20. **Clock Synchronization:** Use `HybridLogicalClock (HLC)` to generate timestamps. HLC combines physical time with a logical counter, ensuring that if Event A happens before Event B on one thread, its timestamp is smaller.
21. **Transaction Log Compaction:** Once a `CommitRecord` and its associated items are flushed to an L1 SSTable, the `IntentLog` entries for that TID can be safely truncated.
22. **Error Mapping:** Map `TransactionCanceledException` to include a `CancellationReasons` array: `["None", "ConditionalCheckFailed", "TransactionConflict", "None"]`.
23. **Recovery - Pending Transactions:** During startup, scan the `IntentLog`. If a transaction is `PENDING` and its `CommitRecord` exists, apply it. If it's `PENDING` and no `CommitRecord` exists, ignore/delete it.
24. **Multi-Table Coordination:** The `TransactionManager` must be a singleton that can access all `TableManager` instances. It must coordinate the atomic update of the `Manifest` files for all involved tables.
25. **Validation:** Ensure that a transaction with 25 items across 5 tables results in exactly ONE `fdatasync` call to the `Manifest` log for maximum performance.

## Validation Criteria
*   **Atomicity:** A crash during `TransactWriteItems` leaves the database in either the "Before" or "After" state, never partial.
*   **Isolation:** Concurrent transactions see a consistent snapshot and do not observe "Dirty Reads."
*   **Performance:** Transaction overhead is < 20% compared to equivalent individual `PutItem` calls.
