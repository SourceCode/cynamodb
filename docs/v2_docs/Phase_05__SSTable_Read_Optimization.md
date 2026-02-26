# Phase 05: SSTable: SIMD-Accelerated Filtering & Block Cache Tuning

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Retrieving data from disk is the slowest part of any database. This phase focuses on minimizing disk reads through "Negative Lookups" (knowing data isn't there without looking) and maximizing the efficiency of the in-memory "Block Cache." We will use SIMD instructions to speed up Bloom filter checks and implement a highly-concurrent cache.

## Technical Definition
*   **Vectorized Bloom Filters:** Use AVX2/NEON instructions to check multiple bits in a Bloom filter simultaneously.
*   **Compressed Block Cache:** Store blocks in compressed form in memory to increase the effective cache size.
*   **Wait-Free Cache:** Use a sharded LRU or Clock-Pro algorithm to minimize lock contention.

## Reference Files
*   `include/cynamodb/engine/lsm/bloom_filter.hpp`
*   `src/engine/lsm/block_cache.cpp`
*   `include/cynamodb/engine/lsm/sstable.hpp`

## Expanded Tasks
1.  **SIMD Bloom Filter:** Implement `BlockedBloomFilter`. Divide the filter into 32-byte blocks. For a given key, map it to one 32-byte block. Use `_mm256_set_epi32` and `_mm256_testz_si256` (AVX2) to check 8 bits simultaneously within that block.
2.  **Bitset Optimization:** Implement `AlignedBitset` using `std::aligned_alloc(64, size)`. Ensure that the bitset size is always a multiple of 256 bits to avoid bounds checking in the SIMD hot path.
3.  **Hash Function Vectorization:** Integrate `XXH3_64bits`. For the Bloom filter, use the "Enhanced Double Hashing" technique: `hash1 + i * hash2 + i^2`. Vectorize this to generate 8 hash values in a single SIMD pass.
4.  **Bloom Filter False Positive Tuning:** Add `fpp_rate` to `TableMetadata`. Use 10 bits per key (1% FPP) for small tables and 14 bits (0.1% FPP) for tables with large items, optimizing for disk I/O reduction.
5.  **Block Cache Sharding:** Implement `ShardedBlockCache`. Use a `std::vector<std::unique_ptr<CacheShard>>`. Determine the shard index via `hash(block_id) % num_shards`. Target 64 shards to practically eliminate mutex contention.
6.  **LRU-K Implementation:** In `CacheShard`, implement `LRU-K` (K=2). Track the last two access timestamps for each block. Only promote a block to the "Hot" list if it is accessed twice, protecting the cache from "Scan Pollution."
7.  **Compressed Block Storage:** When a block is read from disk (compressed with LZ4), store it *as is* in the `BlockCache`. Decompress only when a `BlockIterator` is created. This effectively increases cache capacity by 2x-4x.
8.  **Zero-Copy Cache Access:** `BlockCache::get()` must return a `CacheHandle` (RAII wrapper for a `shared_ptr`). This handle increments the block's "pinned" count, ensuring it isn't evicted while the iterator is using it.
9.  **Prefetching Engine:** Implement `ReadAheadManager`. If a sequence of 3 consecutive block reads is detected, trigger an asynchronous `Scheduler` task to load the next 8 blocks into the cache.
10. **Index Block Caching:** Add a `Priority` enum to `BlockCache::insert()`. Index and Bloom blocks are inserted with `Priority::High`, making them 10x less likely to be evicted than standard data blocks.
11. **SSTable Footer Optimization:** Redesign the `SSTableFooter`. Use fixed-width 64-bit integers for the index offsets. This allows `SSTableIndex::find_block()` to use a vectorized binary search or a simple `std::lower_bound` on a contiguous array.
12. **Block Checksum Validation:** Use the `crc32c` library (SSE4.2/NEON) to verify the 4-byte block checksum. Perform this check in the background thread that loads the block from disk, before inserting it into the cache.
13. **Cache Hit/Miss Metrics:** Implement `Metrics::BLOCK_CACHE_HIT_RATE`. Track hits/misses separately for `DataBlocks` and `IndexBlocks` to identify if the cache size is sufficient for the metadata.
14. **Direct I/O Integration:** On Linux, use `O_DIRECT` for reading SSTables. This requires aligning all read buffers and offsets to 4096 bytes. Bypassing the OS page cache prevents "Double Buffering" and reduces memory pressure.
15. **Asynchronous Block Loading:** Use `std::promise/future` for block requests. The `LsmIterator` should call `request_block()`, which returns immediately. The iterator only blocks when `future.get()` is called, allowing for pre-fetching other levels.
16. **SIMD Key Comparison:** For SSTable blocks with fixed-length keys, use `_mm256_cmpeq_epi8` followed by `_mm256_movemask_epi8` to find the first non-matching byte across 32 bytes in a single instruction.
17. **Data Alignment:** Ensure `BlockBuilder` uses `RequestContext`'s aligned arena. All `std::pmr::vector` instances used for buffering must use the `AlignedMemoryResource` from Phase 02.
18. **Block Iterator Recycling:** Implement `ObjectPool<BlockIterator>`. Since iterators are created for every `GetItem` and `Scan`, reusing them saves thousands of allocations per second.
19. **Memory-Mapped Index:** For tables with `MMAP_INDEX=true`, use `mmap()` to map the entire Index section of the SSTable into the process's virtual address space. This is ideal for SSD-based systems with high RAM.
20. **Cache Warm-up:** Implement `warm_up_cache(TableName)`. This reads the `Manifest`, finds the most recent SSTables, and loads their Index and Bloom blocks into the cache during engine startup.
21. **TinyLFU Integration:** (Advanced) Implement a "Probabilistic Filter" (like Count-Min Sketch) at the cache entry point. Only admit a block if its estimated frequency is higher than the victim's frequency.
22. **SSTable Health Monitoring:** If an SSTable's Bloom filter reports a "Hit" but the key is not found in the block (False Positive), increment a counter. If the rate exceeds 5%, log a warning to re-evaluate the FPP settings.
23. **Bloom Filter Serialization:** Ensure the Bloom filter is written as a contiguous block at the end of the SSTable. Use a 4-byte "Magic Number" at the start of the Bloom block for versioning.
24. **Performance Test:** In `tests/bench_lsm.cpp`, measure the time to perform 100k lookups for keys that don't exist. With SIMD Bloom filters, this should be > 5x faster than without.
25. **Validation:** Use `top` or `htop` during a heavy read load. Mutex contention in the `BlockCache` should be near 0%, and CPU usage should be dominated by SIMD-heavy parsing, not locking.

## Validation Criteria
*   **Latency:** Point lookup for a non-existent key is resolved in < 2 microseconds (if Bloom filter is in cache).
*   **Throughput:** Block Cache throughput scales linearly with the number of CPU cores up to 32 cores.
*   **Efficiency:** Compressed cache can store 2x more data compared to raw storage for typical JSON payloads.
