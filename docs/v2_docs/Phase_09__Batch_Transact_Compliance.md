# Phase 09: Full API Compliance: Batch & Transact Operations

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Batch and transaction APIs are the most complex in DynamoDB. This phase ensures that `BatchGetItem`, `BatchWriteItem`, `TransactGetItems`, and `TransactWriteItems` are fully compatible, including partial failure handling (UnprocessedKeys/UnprocessedItems), item limits, and transaction atomicity across multiple tables.

## Technical Definition
*   **Partial Failure Handling:** Implementation of the "Unprocessed" system for batch APIs under throttling or transient errors.
*   **Transaction Coordinator:** Refine the transaction manager to handle multi-table coordination and 2PC.
*   **Request Limits:** Enforce limits (100 items per batch, 25-100 items per transaction).

## Reference Files
*   `src/api/dispatcher.cpp`
*   `src/engine/transactions/manager.cpp`
*   `tests/test_transactions_http.cpp`

## Expanded Tasks
1.  **BatchGetItem Implementation:** In `dispatcher.cpp`, group the input `RequestItems` by `TableName`. Use the `Scheduler` to launch one task per table to fetch the items in parallel.
2.  **BatchGetItem Throttling:** If a `TableManager` reports it has 0 RCU remaining for a table, stop fetching items for that table and move the remaining keys to the `UnprocessedKeys` section of the response.
3.  **BatchGetItem Order Preservation:** For each table, return items in the same order they appeared in the request. If an item is not found, omit it from the `Responses` list for that table (standard DDB behavior).
4.  **BatchWriteItem Implementation:** Group `PutRequest` and `DeleteRequest` by table. Each table group must be processed independently.
5.  **BatchWriteItem Atomic (Internal):** Ensure that each individual write is atomic and independent. If 24 items succeed and the 25th fails due to a disk error, the 24 successful items must remain in the database.
6.  **BatchWriteItem 'UnprocessedItems':** If a write fails due to a *transient* error (Throttling, InternalError), return that specific request in the `UnprocessedItems` field. Do NOT return it if the error is a `ValidationException`.
7.  **Batch Validation:** Check `BatchWriteItem` for `requests.size() > 25` and `BatchGetItem` for `requests.size() > 100`. Reject if exceeded. Also check the total payload size (max 16MB).
8.  **TransactGetItems Execution:** Assign a single `Snapshot` to the entire request. All reads across all tables must use this same snapshot to ensure cross-table consistency.
9.  **TransactWriteItems Atomicity:** Use the `TransactionManager` from Phase 07. If any write in the transaction fails (e.g., a `ConditionCheck` fails), no changes from the transaction should be applied to any table.
10. **Transaction Idempotency:** Check the `ClientRequestToken` at the very start. If it exists and matches a recently completed transaction, return that result immediately without re-evaluating conditions.
11. **Transaction Condition Checks:** Implement `ConditionCheck` as a first-class operation in the transaction. It must be evaluated against the snapshot and its failure must abort the entire transaction.
12. **TransactWriteItems Limit:** Enforce the 25-item limit for `TransactWriteItems`. If more are provided, return `ValidationException`.
13. **Transaction Error Codes:** If aborted, return a 400 error with `CancellationReasons`. Each reason must correspond to one item in the request (e.g., `["ConditionalCheckFailed", "None", "None"]`).
14. **Transaction Conflict Detection:** Optimize the OCC logic. If two transactions only overlap on *reads*, they should both be allowed to commit (Standard Snapshot Isolation).
15. **Cross-Table Consistency:** When committing a transaction across Table A and Table B, ensure the `Manifest` updates for both tables are committed as a single atomic step in the `TransactionManager`.
16. **Duplicate Item Check:** Before processing, scan the request for duplicate keys (same Partition Key and Sort Key) within the same transaction or batch. If found, return `ValidationException`.
17. **Resource Calculation (Batch):** Sum the RCU/WCU for all individual items in the batch and return the total in `ConsumedCapacity`.
18. **Parallel Read Integration:** In `BatchGetItem`, use `std::vector<std::future<TableResult>>` to wait for all parallel table-fetch tasks to complete.
19. **Large Transaction WAL:** If a transaction has many items, write all write-intents as a single contiguous block in the `IntentLog` to minimize disk seek overhead.
20. **Transaction Snapshot Timeout:** If a transaction isn't committed or aborted within 60 seconds, the `TransactionManager` must automatically abort it and release all sharded locks.
21. **Recovery Integration:** Verify that if the process dies during a `TransactWriteItems` commit, the recovery engine either completes the whole transaction or rolls it back entirely across all tables.
22. **BatchWriteItem Condition Restriction:** Ensure the parser rejects any `ConditionExpression` inside a `BatchWriteItem`. This is a common point of confusion for new DynamoDB users.
23. **Test Coverage - Mix Batch:** Test `BatchWriteItem` where Table A is successful, Table B is throttled (returning Unprocessed), and Table C has a validation error (failing the entire request).
24. **Test Coverage - Transaction Conflict:** Simulate two transactions trying to increment the same counter. Verify that exactly one succeeds and one is canceled with `TransactionConflict`.
25. **Validation:** Use the `test_transactions_http.cpp` to verify that a transaction on 25 items across 25 tables is truly atomic.

## Validation Criteria
*   **Atomicity:** TransactWriteItems is 100% atomic or 100% failed.
*   **Correctness:** Batch operations correctly report `Unprocessed` items instead of failing the entire request.
*   **Performance:** A transaction with 25 items takes < 10ms p99 on high-end NVMe storage.
