# Phase 16: Capacity Management: Distributed Token Bucket & Throttling

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
DynamoDB's RCU (Read Capacity Unit) and WCU (Write Capacity Unit) model requires a highly precise throttling engine. A naive global lock on capacity counters would become a massive bottleneck. This phase implements a distributed token bucket algorithm using atomic operations and thread-local "shards" to achieve high-performance throttling.

## Technical Definition
*   **Token Bucket Algorithm:** A classic algorithm for rate limiting that allows for bursts but maintains an average rate.
*   **Atomic Capacity Shards:** Use multiple atomic counters (one per core/NUMA node) to avoid cache contention during capacity checks.
*   **Adaptive Bursting:** Allow a table to burst up to 5 minutes of its provisioned capacity as per AWS spec.

## Reference Files
*   `include/cynamodb/engine/capacity/manager.hpp`
*   `src/engine/capacity/manager.cpp`
*   `src/api/dispatcher.cpp`

## Expanded Tasks
1.  **Table Capacity Schema:** Add `read_units` and `write_units` to `TableMetadata`. Add a `billing_mode` flag (PROVISIONED or ON_DEMAND).
2.  **Atomic Token Bucket:** Implement `TokenBucket`. It uses an `std::atomic<int64_t> tokens` representing the number of available units multiplied by 1000 (fixed-point arithmetic to avoid `double` in the hot path).
3.  **Capacity Sharding:** Create `class CapacityShard`. Every worker thread in the `Scheduler` has its own `CapacityShard`. When a check happens, it first checks its local shard. If empty, it "Refills" from the global pool in a batch.
4.  **Refill Engine:** Implement `RefillTask`. It runs every 100ms. It calculates `tokens_to_add = (current_time - last_refill) * provisioned_rate`. It adds tokens to the global pool up to the `burst_limit`.
5.  **Burst Management:** Set the `burst_limit` to `300 * provisioned_rate` (5 minutes of tokens). This is the maximum capacity the global pool can hold.
6.  **RCU/WCU Calculation:** Implement `calculate_rcu(operation, item_size, consistent)`. 1 RCU = 4KB for strong, 0.5 RCU for eventual. 1 WCU = 1KB for standard writes. Transactional writes = 2x RCU/WCU.
7.  **WCU Cost for GSI/LSI:** In `update_indexes()`, calculate the WCU for each index entry and subtract it from the index's (or base table's) capacity bucket.
8.  **Adaptive Throttling:** (Advanced) If a table is under its RCU limit, its tokens can be "Borrowed" by its GSIs if they are being throttled, mimicking DynamoDB's "Adaptive Capacity" (if enabled).
9.  **On-Demand Mode Support:** In `ON_DEMAND` mode, the bucket has no refill rate but has a high `burst_limit` (e.g., 40,000 RCU/sec). If exceeded, return `ProvisionedThroughputExceeded`.
10. **ProvisionedThroughputExceededException:** Return a 400 error with the header `x-amzn-ErrorType: ProvisionedThroughputExceededException`.
11. **Retry-After Header:** Calculate `wait_ms = required_tokens / refill_rate`. Return `Retry-After: 0.1` (in seconds) to tell the client when tokens will likely be available.
12. **Global Admissions Control:** Implement `GlobalThrottler`. If the system-wide CPU > 95% or Memory > 90%, it forces all `consume_units()` calls to fail, protecting the server from OOM.
13. **Capacity Dashboard Metrics:** Track `Metrics::RCU_CONSUMED` and `Metrics::RCU_THROTTLED`. Calculate the "Throttling Percentage" per table.
14. **Precision Refill Logic:** Use `std::chrono::steady_clock`. Refill logic must be monotonic to avoid issues if the system clock is adjusted via NTP.
15. **Negative Token Support:** If a bucket has 1 token and a request needs 5, allow it to proceed but set the bucket to -4. Subsequent requests will be blocked until the bucket refills to > 0.
16. **Consumed Capacity Response:** Implement `populate_consumed_capacity()`. If `ReturnConsumedCapacity=TOTAL`, add a JSON field: `"ConsumedCapacity": {"CapacityUnits": 5.0, "TableName": "User"}`.
17. **Transaction Capacity Billing:** For `TransactWriteItems`, calculate the WCU for every item and then multiply the total by 2. This accounts for the 2-phase commit overhead.
18. **Batch Capacity Billing:** In `BatchGetItem`, sum the sizes of all *returned* items to calculate the final RCU. If a key is not found, it still costs 0.5 or 1 RCU depending on consistency.
19. **Capacity-Aware Scheduler:** Add a `Priority::Throttled` level. If a request is near its capacity limit, the scheduler can move its tasks to this lower priority queue.
20. **RCU/WCU Update API:** When `UpdateTable` changes the throughput, the `CapacityManager` must immediately reset the `refill_rate` and the `burst_limit` without losing the currently accumulated tokens.
21. **Throughput Scaling Metric:** Log: `Table 'User' throttled 500 times in the last 60s. Suggest increasing RCU by 20%.`
22. **Sharding Rebalancing:** If one worker thread is handling 90% of a table's traffic, it should be allowed to take a larger "Batch" of tokens from the global pool to reduce atomic contention.
23. **Test Coverage - Precise Throttling:** In `test_api.cpp`, set RCU to 10. Start 10 threads doing 100 `GetItem` calls each. Verify that the total time taken is approximately 10 seconds.
24. **Test Coverage - Bursting:** Verify that if a table with 100 RCU is idle for 10 minutes, it can handle a sudden burst of 30,000 RCU (300 * 100) in 1 second.
25. **Validation:** Use `google-benchmark` to measure `CapacityManager::consume()`. Target: < 100ns under zero contention, < 500ns under high contention (32 threads).

## Validation Criteria
*   **Accuracy:** Throttling is accurate to within 1% of the provisioned rate over a 1-minute window.
*   **Overhead:** Capacity management logic is lock-free and scales linearly with the number of CPU cores.
*   **Compliance:** Passes all capacity-related tests in `tests/test_api.cpp`.
