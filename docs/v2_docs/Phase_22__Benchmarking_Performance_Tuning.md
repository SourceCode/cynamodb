# Phase 22: Benchmarking: Tail Latency (P99) & Throughput Tuning

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Performance is not just about average latency; for a database, "Tail Latency" (P99, P99.9) is what determines user experience at scale. This phase uses industry-standard benchmarks (YCSB, TPC-C) and custom tools to identify and eliminate latency spikes. We will tune the system to achieve "The DynamoDB Profile": ultra-low, predictable latency regardless of data size.

## Technical Definition
*   **P99 Latency:** The latency experienced by the 99th percentile of requests.
*   **YCSB (Yahoo Cloud Serving Benchmark):** The standard benchmark for NoSQL databases.
*   **Flamegraphs:** Use `perf` and `flamegraph.pl` to visualize CPU bottlenecks.

## Reference Files
*   `tests/bench_performance.cpp`
*   `scripts/run_ycsb.sh`
*   `docs/v2_docs/performance_profile.md`

## Expanded Tasks
1.  **Benchmarking Framework:** Implement `BENCHMARK_TEMPLATE(operation)` in `bench_performance.cpp`. Measure latency with a nanosecond timer. Use `DoNotOptimize()` and `ClobberMemory()` to prevent compiler optimizations from skewing results.
2.  **YCSB Port:** Implement the `com.yahoo.ycsb.DB` interface in Java (or use a C++ implementation) that calls the CynamoDB HTTP API. Support all 6 YCSB core workloads.
3.  **Baseline Profile:** Run 1 million operations (Workload A: 50/50 R/W). Record: `P50: 150us, P99: 1.2ms, P99.9: 8ms`. This is the baseline for all future optimizations.
4.  **CPU Flamegraph Analysis:** Use `perf record -F 99 -a -g -- sleep 60`. Convert the output to a `.svg` flamegraph. Look for "Flat" areas in the graph that indicate expensive function calls.
5.  **I/O Wait Analysis:** Use `perf stat -e block:block_rq_issue,block:block_rq_complete`. Correlate I/O completion times with P99.9 spikes to identify "Long Tail" disk I/O.
6.  **Context-Switch Reduction:** Monitor `cswch/s` in `pidstat`. If context switches > 50k/sec, reduce the number of worker threads or increase the task quantum in the `Scheduler`.
7.  **Lock Contention Profiling:** Use `perf lock record`. Identify mutexes with high "Wait Time". Replace these with `std::atomic` or the sharded `StripedLock` from Phase 07.
8.  **False Sharing Detection:** Use `perf c2c`. Look for "HITM" (Hit In Transfer Modified) events. Ensure that all frequently written counters are in their own cache line via `alignas(64)`.
9.  **LSM Write Stall Tuning:** Monitor `MemTable` flush speed. If `flushing_memtables > 2`, the engine must start throttling incoming writes to avoid a "Write Stall" where the system stops for seconds.
10. **Point Lookup Optimization:** Target `P99 < 500us` for cached reads. This requires that the HTTP parsing, SigV4 validation, and Block Cache lookup all happen within the same worker thread to avoid context switching.
11. **Scan Throughput Optimization:** Measure `Scan` speed on a 10GB table. Target `500MB/sec` per core. Optimize the `MergingIterator` to use a heap for more than 4 SSTables.
12. **BatchWriteItem Throughput:** Benchmark `BatchWriteItem` with 25 items of 1KB each. Target `200 batches/sec` per core (5000 items/sec).
13. **Huge Page Optimization:** Enable `THP` (Transparent Huge Pages) or `Explicit Huge Pages` for the `GlobalMemoryResource`. Measure the reduction in `dTLB-load-misses` via `perf stat`.
14. **Prefetching Calibration:** Run a "Range Scan" benchmark. Vary the `prefetch_blocks` parameter from 1 to 64. Identify the "Sweet Spot" where throughput is maximized without wasting cache space.
15. **Memory Bandwidth Analysis:** Use `intel-pcm` to monitor `L3 Misses` and `Memory BW`. If BW > 80% of peak, the system is memory-bound; optimize data structures for size.
16. **Wait-Free Queue Tuning:** Adjust the `MPMC_QUEUE_SIZE`. If it's too small, producers block; if too large, it wastes memory and increases cache latency.
17. **SSTable Block Size Tuning:** Benchmark 4KB blocks (better for random GetItem) vs 64KB blocks (better for large Scans). Set the default to 8KB or 16KB based on the results.
18. **Bloom Filter Tuning:** Measure the "False Positive" overhead. If 10 bits/key is too small for a 100GB table, increase it to 12 or 14 to save disk reads.
19. **Network Stack Tuning:** Tune the `tcp_mem` and `tcp_rmem` kernel parameters to ensure the OS doesn't throttle high-speed local TCP traffic.
20. **SigV4 Overhead Reduction:** Use `perf` to verify that `sha256` is indeed using the `sha-ni` instructions. If not, debug the feature detection logic.
21. **NUMA Locality Tuning:** Use `numactl --cpunodebind=0 --membind=0 ./cynamodb` and compare performance vs a "Cross-Node" configuration. Use this data to tune the `Scheduler` node affinity.
22. **Cold Start Optimization:** Measure time to achieve 90% of peak RPS after a restart. Implement `warm_up_cache` to reduce this time from 5 minutes to < 10 seconds.
23. **Tail Latency "Killer" Hunt:** Look for P99.9 spikes that happen every 60s. These are often caused by the OS's `dirty_expire_centisecs` or internal cleanup tasks. Move these tasks to a "Low Priority" worker.
24. **Multi-Tenant Stress Test:** Launch 10 threads, each targeting a different table. Verify that a heavy `Scan` on Table A does not increase the `GetItem` latency on Table B by more than 20%.
25. **Validation:** Produce a `PerformanceReport.pdf` that summarizes all findings and the final "Tuned" configuration for production.

## Validation Criteria
*   **Performance:** Achieves > 50,000 TPS (Transactions Per Second) on a single 16-core machine for YCSB Workload A (50/50 read/write).
*   **Predictability:** P99.9 latency is less than 5x the P50 latency.
*   **Scalability:** Performance scales > 0.9x linearly with the number of CPU cores.
