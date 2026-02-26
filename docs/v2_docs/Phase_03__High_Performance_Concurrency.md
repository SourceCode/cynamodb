# Phase 03: High-Performance Concurrency & Work-Stealing Scheduler

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Standard thread pools often suffer from task imbalance and cache-locality issues. For CynamoDB to handle high throughput, we need a scheduler that understands the difference between I/O-bound tasks (WAL syncing) and CPU-bound tasks (JSON parsing, Expression evaluation). This phase implements a custom work-stealing scheduler using C++23's `std::jthread` and lock-free queues.

## Technical Definition
*   **Work-Stealing:** Each worker thread has its own local queue; idle threads steal from busy ones to balance load.
*   **Task Prioritization:** Separate queues for critical "Writer" tasks and "Reader" tasks to prevent read-starvation during heavy compaction.
*   **NUMA-Awareness:** Pin worker threads to physical cores and prefer local memory allocations.

## Reference Files
*   `include/cynamodb/core/scheduler.hpp`
*   `src/core/scheduler.cpp`
*   `include/cynamodb/core/lock_free_queue.hpp`

## Expanded Tasks
1.  **Lock-Free MPMC Queue:** Implement a `RingBuffer` based Multiple-Producer Multiple-Consumer queue. Use `std::atomic<size_t>` for head/tail pointers and `std::atomic_flag` for per-slot status. Target < 100ns for push/pop operations under contention.
2.  **Worker Thread Class:** Define `class Worker` in `scheduler.cpp`. Each worker must own a `std::deque` (protected by a `std::mutex` only for stealing) and a `std::jthread`. Use `std::stop_token` to handle graceful termination.
3.  **Task Affinity:** Implement `TaskHint` enum { LowLatency, HighThroughput, TableAffinity }. If `TableAffinity` is set, the scheduler should hash the `TableName` to assign the task to the same worker consistently, improving L3 cache hits.
4.  **Stealing Algorithm:** Use a "Randomized Stealing" strategy. When a worker's queue is empty, it picks a random peer and attempts to steal half of its tasks using `std::unique_lock<std::mutex> try_lock()`.
5.  **Scheduler Integration:** In `src/main.cpp`, replace the current pool with `WorkStealingScheduler scheduler(std::thread::hardware_concurrency())`. Ensure the scheduler is initialized before the HTTP server starts.
6.  **I/O Isolation:** Create a separate `DedicatedThreadPool` for WAL disk operations. This pool should have exactly one thread per physical disk to avoid seeking contention and should use `io_uring` on supported Linux kernels.
7.  **Adaptive Sleeping:** Implement a 3-stage idle loop: 1. Spin for 1000 iterations. 2. `std::this_thread::yield()` for 10 iterations. 3. `std::condition_variable::wait_for(1ms)` to avoid burning CPU cycles when the system is idle.
8.  **C++23 Stop Tokens:** Use `std::stop_callback` to trigger immediate task cancellation for long-running background tasks (like compaction) during system shutdown.
9.  **Priority Queuing:** Implement `Scheduler::submit(Task, Priority)`. High-priority tasks are pushed to the *front* of the worker's local deque to be executed immediately after the current task.
10. **Compaction Throttling:** Add a `TaskRateLimiter` to the compactor. If the scheduler's global queue depth exceeds 1000 tasks, reduce the compaction task submission rate by 50%.
11. **Wait-Free Task Submission:** For `Priority::Normal` tasks, implement a `try_submit()` that returns `false` if the target queue is full, allowing the caller to try another worker or shed load.
12. **Thread Pinning:** Use `pthread_setaffinity_np` on Linux. Map Worker 0 to CPU 0, Worker 1 to CPU 1, etc. On Apple Silicon, use `pthread_set_qos_class_self_np` to prefer performance cores.
13. **Cache-Line Padding:** Use `struct alignas(64) WorkerState` to group all frequently modified worker-local variables. This prevents "False Sharing" where multiple cores fight over the same cache line.
14. **Task Batching:** Add `submit_batch(std::vector<Task>)`. This allows a `BatchWriteItem` request to push all 25 items to a worker in a single operation, reducing atomic overhead.
15. **Context Switching Reduction:** Avoid `std::future::get()` in the hot path. Use a continuation-based approach: `submit(Task).then(NextTask)`. This keeps the worker busy without blocking.
16. **Wait/Notify Mechanism:** Use `std::atomic<bool> has_work` with `has_work.wait(false)` and `has_work.notify_one()`. This is more efficient than `std::condition_variable` in many modern C++ runtimes.
17. **Scheduler Metrics:** Implement `SchedulerStats` tracking: `tasks_completed`, `tasks_stolen`, `avg_queue_depth`, `max_latency_ns`. Export these to the observability layer.
18. **Deadlock Detection:** Implement a "Heartbeat" for each worker. If a worker hasn't finished a task in 30 seconds, trigger a `SIGABRT` and print a stack trace of all workers to help debug infinite loops.
19. **Load Shedding:** If the global task queue is full and P99 latency is > 200ms, start returning `503 Service Unavailable` for new requests before they even hit the scheduler.
20. **Coroutine Support:** (Advanced) Experiment with `std::coroutine` for the HTTP request handler. Use `co_await scheduler.schedule()` to yield the thread during I/O or long-running computations.
21. **NUMA Discovery:** Parse `/sys/devices/system/node/node*/cpulist` to identify which cores belong to which NUMA node. Ensure workers only steal from peers on the same NUMA node.
22. **Tail Latency Optimization:** Implement "Task Duplication" for idempotent reads. If a read takes > 50ms, submit it to another worker. The first one to finish wins.
23. **Stress Test:** In `tests/test_scheduler.cpp`, launch 1 million "No-Op" tasks and measure the total overhead. It should be less than 50ns per task.
24. **Fiber Local Storage:** Implement a `Map<TaskID, Data>` that follows the task as it moves between workers, providing a way to store request-scoped metadata without `thread_local`.
25. **Validation:** Use `wrk` to benchmark the HTTP server. Verify that increasing the number of worker threads from 4 to 8 results in a > 1.8x increase in requests per second.

## Validation Criteria
*   **Throughput:** At least a 15% increase in total requests per second compared to a standard `std::thread` pool.
*   **CPU Efficiency:** Minimal context switching overhead (< 1% of total CPU time).
*   **Safety:** No race conditions or memory leaks detected under TSAN.
