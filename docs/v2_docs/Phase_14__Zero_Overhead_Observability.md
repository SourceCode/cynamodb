# Phase 14: Observability: Zero-Overhead Metrics & Distributed Tracing

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
In a high-performance system, the act of measuring performance can itself degrade performance. This phase implements a "Zero-Overhead" metrics system using thread-local counters and atomic snapshots. We will also integrate distributed tracing (OpenTelemetry) to track request flow across the networking, authentication, and storage layers.

## Technical Definition
*   **Lock-Free Metrics:** Use `thread_local` counters to avoid atomic contention during the hot path.
*   **Adaptive Sampling:** Only trace a small percentage of requests (e.g., 0.1%) by default, increasing it for slow requests.
*   **OpenTelemetry Integration:** Export metrics and traces to Prometheus/Grafana or AWS CloudWatch.

## Reference Files
*   `include/cynamodb/observability/metrics.hpp`
*   `src/observability/metrics.cpp`
*   `include/cynamodb/context.hpp`

## Expanded Tasks
1.  **Thread-Local Counter Pool:** Implement `ThreadLocalCounter`. It should be an array of `uint64_t` padded to 64 bytes. A global `MetricsRegistry` keeps a list of pointers to these thread-local arrays to aggregate them during scrapes.
2.  **High-Resolution Clock:** Implement `get_nanoseconds()`. On x86_64, use `__rdtsc()` and calibrate it against `std::chrono::system_clock` at startup to convert cycles to nanoseconds efficiently.
3.  **Metrics Macros:** Implement `SCOPED_TIMER(metric_name)`. It records the start time in the constructor and adds the delta to the thread-local counter in the destructor.
4.  **Prometheus Exporter:** Create `PrometheusExporter`. It listens on a separate port (e.g., 9090) and when scraped, it sums all thread-local counters and formats them: `cynamodb_request_count{table="User"} 1234`.
5.  **Histograms & Percentiles:** Integrate `HdrHistogram`. It uses a logarithmic bucket approach to store latencies from 1ns to 1s with fixed relative error, requiring very little memory.
6.  **Context-Based Tracing:** Add `TraceID` (16 bytes) and `SpanID` (8 bytes) to `RequestContext`. If the incoming request has a `x-amzn-trace-id` header, parse it; otherwise, generate a new one.
7.  **OpenTelemetry Span Support:** Implement `SpanBuilder`. A span should record its name, start/end time, and attributes like `db.table`, `http.method`, and `error.type`.
8.  **Span Propagation:** When the `Scheduler` moves a task to another thread, ensure the `TraceContext` is copied into the new thread's local storage so child spans are correctly linked.
9.  **Storage Engine Metrics:** Add `Metrics::SSTABLE_READ_BYTES`, `Metrics::BLOOM_FILTER_FALSE_POSITIVES`, and `Metrics::COMPACTION_JOBS_TOTAL`. These help identify if storage is the bottleneck.
10. **Memory Metrics:** In `MemoryManager`, report `current_rss`, `pmr_arena_used_bytes`, and `block_cache_usage_percent` every 5 seconds.
11. **Concurrency Metrics:** Track `Metrics::SCHEDULER_QUEUE_DEPTH` and `Metrics::WORKER_STOLEN_TASKS`. High stealing counts indicate poor task affinity.
12. **HTTP Metrics:** Track `Metrics::HTTP_2XX_COUNT`, `Metrics::HTTP_4XX_COUNT`, and `Metrics::HTTP_5XX_COUNT` per API operation (e.g., `PutItem`, `Query`).
13. **Adaptive Sampling Engine:** Implement `Sampler`. Use a "Probability Sampler" for the first 1000 requests, then adjust the rate based on the target spans-per-second to avoid overwhelming the trace collector.
14. **Asynchronous Metrics Exporter:** Use a `std::jthread` that wakes up every 10 seconds, aggregates the counters, and sends them via UDP (StatsD format) or HTTP (OpenTelemetry OTLP).
15. **Metrics Snapshotting:** Implement `take_snapshot()`. This copies all counters into a `std::map<string, uint64_t>` and resets the "per-interval" counters (like `requests_last_minute`).
16. **Log Correlation:** Update the `Logger` to check if a `RequestContext` is active in the current thread and if so, prepend `[trace_id=...]` to the log message.
17. **Resource Usage Metrics:** Use `getrusage()` to track CPU user/system time and `procfs` (on Linux) to track disk I/O stats for the process.
18. **Custom DynamoDB Metrics:** Calculate `ConsumedReadCapacityUnits` for every successful request and report it as a counter: `cynamodb_consumed_rcu_total`.
19. **Throttling Metrics:** Track `Metrics::THROTTLED_REQUESTS_TOTAL`. High values indicate that users need to increase their provisioned capacity.
20. **Error Rate Monitoring:** Calculate the "Error Ratio" (`5xx / Total`). If it exceeds 1% over 5 minutes, trigger an internal "Critical Alert" log.
21. **Lock Contention Metrics:** In `StripedLock` and `BlockCache`, increment `Metrics::LOCK_CONTENTION_COUNT` every time a thread fails `try_lock()` and has to block.
22. **Introspection API:** Implement `/debug/vars` (inspired by Go's expvar). It returns a large JSON object with all internal counters, configuration, and build info.
23. **Test Coverage - Metrics Accuracy:** In `tests/test_metrics.cpp`, launch 100 threads, each incrementing a counter 1 million times. Verify the final sum is exactly 100 million.
24. **Test Coverage - Overhead:** Use `wrk` to measure RPS with and without `SCOPED_TIMER` on the main request path. The difference must be within the noise floor (< 1%).
25. **Validation:** Verify that the Prometheus exporter output is valid by running `promtool check metrics` against the output.

## Validation Criteria
*   **Overhead:** Enabling all metrics adds less than 1 microsecond to the request critical path.
*   **Accuracy:** Latency percentiles are accurate to within 1% of the true value.
*   **Visibility:** All 45+ DynamoDB APIs have dedicated latency and count metrics.
