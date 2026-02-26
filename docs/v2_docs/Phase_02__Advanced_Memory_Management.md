# Phase 02: Advanced Memory Management & PMR Integration

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Standard `std::allocator` is general-purpose and often leads to memory fragmentation and high contention in multi-threaded environments. For a high-performance database, we need deterministic memory behavior. This phase introduces `std::pmr` (Polymorphic Memory Resources) across the codebase, allowing us to use Arena/Stack-based allocators for short-lived requests and specialized pool allocators for long-lived metadata.

## Technical Definition
*   **PMR Integration:** Transition core containers (`std::pmr::vector`, `std::pmr::string`, `std::pmr::unordered_map`) to use specialized memory resources.
*   **Arena Allocators:** Use `std::pmr::monotonic_buffer_resource` for request-scoped data to achieve O(1) allocation and deallocation.
*   **Cache Alignment:** Implement cache-line aware allocators to prevent false sharing in multi-threaded contexts.

## Reference Files
*   `include/cynamodb/core/memory_resource.hpp`
*   `include/cynamodb/context.hpp`
*   `src/core/memory_manager.cpp`

## Expanded Tasks
1.  **Global Memory Resource:** Implement `TrackingMemoryResource` in `src/core/memory_manager.cpp` that inherits from `std::pmr::memory_resource`. It must use `std::atomic<size_t>` to track `current_usage` and `peak_usage`. Set it as the default via `std::pmr::set_default_resource()`.
2.  **Request Arena Setup:** In `include/cynamodb/context.hpp`, add a `std::pmr::monotonic_buffer_resource` member to `RequestContext`. Initialize it with a 16KB stack-allocated buffer (`std::byte stack_buf[16384]`) to handle 95% of requests without any heap access.
3.  **Core Container Refactor:** Transition the LSM MemTable's internal storage from `std::map` to a `std::pmr::vector<Entry>` combined with a custom `std::pmr::monotonic_buffer_resource`. This ensures that all entries for a single MemTable are contiguous in memory.
4.  **String Refactor:** Update the `AttributeValue` union/variant to use `std::pmr::string`. Ensure that all string operations within the `ExpressionEvaluator` use the `RequestContext` arena to avoid millions of small string allocations.
5.  **Pool Allocator for Metadata:** Implement `MetadataPoolResource` using `std::pmr::unsynchronized_pool_resource`. Use this specifically for `TableMetadata` and `IndexMetadata` in the `TableManager`. Set `pool_options` to have a max block size of 4KB.
6.  **Cache Line Alignment:** Create `class AlignedMemoryResource` in `memory_resource.hpp`. Overload `do_allocate` to use `posix_memalign` or `aligned_alloc` with a 64-byte alignment requirement. Use this for all synchronization primitives (mutexes, atomics).
7.  **SSTable Block Cache PMR:** Update `BlockCache` to use a `std::pmr::fixed_size_buffer_resource` pre-allocated at startup (e.g., 1GB). This prevents the Block Cache from competing with the OS for memory during high-load scenarios.
8.  **PMR-Aware Map Integration:** Replace `std::unordered_map` in the `TableManager` with `std::pmr::unordered_map`. Use a pool resource to manage the buckets and nodes, reducing the overhead of map rehashes.
9.  **No-Lock Allocator for Thread-Local Data:** Create a `thread_local std::pmr::monotonic_buffer_resource` for high-frequency, short-lived strings (like GSI key construction). Reset this resource at the start of every request processed by that thread.
10. **Memory Pinning for WAL:** Implement `PinnedMemoryResource`. Use `mmap` with `MAP_LOCKED` or call `mlock()` on the allocated range. Use this specifically for the WAL's circular write-buffers to ensure the kernel never swaps them to disk.
11. **Huge Page Support:** Update the `GlobalMemoryResource` to use `mmap` with `MAP_HUGETLB` (Linux) for allocations larger than 2MB. This reduces TLB pressure during full-table scans.
12. **Fragmentation Monitoring:** In `src/core/memory_manager.cpp`, implement a `get_fragmentation_ratio()` function that calculates the gap between `total_allocated` and `total_requested` bytes across all pool resources.
13. **Zero-Initialization Control:** Define a `UninitializedMemoryResource` that bypasses `memset(0)` during allocation. Use this for temporary SSTable merge buffers where every byte is guaranteed to be overwritten immediately.
14. **PMR Transition in Expressions:** Refactor `ASTNode` to inherit from a base class that accepts a `std::pmr::memory_resource*`. Use the `RequestContext` arena to allocate all nodes in a single expression tree.
15. **Allocator-Aware Iterators:** Ensure the `LsmIterator` and its children accept and store a pointer to the memory resource. Use this resource for any internal buffering required during multi-level merging.
16. **Stack-Allocated Arenas:** Implement a `TrivialArena<Size>` template that provides a `std::pmr::memory_resource` interface over a fixed-size `std::array`. Use this for small API responses like `DescribeTable`.
17. **Deadlock-Free Allocators:** Audit all `std::pmr` usage to ensure that no `memory_resource` calls itself recursively or acquires a global lock that is already held by the calling thread.
18. **PMR-Aware Dispatcher:** Modify `src/api/dispatcher.cpp` to pass the `RequestContext`'s memory resource as the first argument to every API implementation function (e.g., `handle_put_item(res, req)`).
19. **Memory Limits:** Add a `hard_limit` to the `RequestContext` arena. If an allocation exceeds this limit (e.g., a malicious 100MB JSON), throw a `std::bad_alloc` or return a `CapacityExceeded` error.
20. **RAII Arena Cleanup:** Ensure the `RequestContext` destructor calls `release()` on the `monotonic_buffer_resource`. Verify that no pointers to the arena-allocated data leak outside the request's lifetime.
21. **Custom Delete for PMR:** Implement `pmr_delete<T>(res, ptr)` utility to correctly call the destructor and deallocate memory through the resource, mimicking `std::delete`.
22. **PMR Validation Test:** In `tests/test_core.cpp`, write a test that fills an arena, records the memory address, and verifies that subsequent `std::pmr::vector::push_back` calls use addresses within that same contiguous block.
23. **Pressure Signaling:** Implement `on_memory_pressure()` in the `MemoryManager`. When usage hits 90%, it should trigger `CompactionManager::trigger_emergency_compaction()` to free up MemTable space.
24. **NUMA-Aware Allocation:** On Linux, use `move_pages` or `set_mempolicy` within a custom memory resource to ensure that a thread running on Core 0 allocates memory from NUMA Node 0.
25. **Documentation:** Update `v1_PRD.md` to include a "Memory Strategy" section documenting the different resources (Arena, Pool, Global) and when to use each.

## Validation Criteria
*   **Performance:** A 20% reduction in `malloc/free` calls during a standard YCSB benchmark run.
*   **Stability:** Zero memory leaks detected by Valgrind/ASAN during a 1-hour stress test.
*   **Observability:** Memory usage per-table and per-request is accurately reported via metrics.
