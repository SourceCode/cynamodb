# Resilience and Performance Improvements (Round 10)

Implemented improvements in this round:

1. Added `BlockCache::kMaxKeyBytes` hard bound for cache-key inputs.
2. Added `BlockCache::kMaxValueBytes` hard bound for cache-value payloads.
3. `BlockCache::put` now rejects empty keys.
4. `BlockCache::put` now rejects oversized keys.
5. `BlockCache::put` now rejects oversized values.
6. `BlockCache` now reserves hash-bucket capacity at construction for lower rehash churn.
7. Added insert counter telemetry (`inserts_`).
8. Added update counter telemetry (`updates_`).
9. Added eviction counter telemetry (`evictions_`).
10. Added `hit_rate()` metric for cache efficiency tracking.
11. Added `capacity()` accessor.
12. Added `set_capacity()` for runtime cache tuning.
13. `set_capacity()` now eagerly evicts entries when shrinking capacity.
14. Converted `contains()` to shared-lock path for better concurrent read scaling.
15. Converted `size()` to shared-lock path for better concurrent read scaling.
16. `get()` now fast-rejects invalid keys before lock acquisition.
17. Replaced single mutex with `std::shared_mutex` to reduce read-side contention.
18. Added Bloom filter `kMaxBits` cap to prevent pathological memory growth.
19. Bloom filter now accepts `std::string_view` for `add()`.
20. Bloom filter now accepts `std::string_view` for `might_contain()`.
21. Bloom filter now exposes `bits()` for runtime sizing introspection.
22. Bloom filter now exposes `hash_count()` for runtime introspection.
23. Added `clear()` to reset filter state without reallocating.
24. Bloom filter now short-circuits on empty bitset for safer behavior.
25. Replaced simplistic Bloom hash with FNV-1a + splitmix64 finalization.
26. Bloom filter sizing now clamps to both minimum and maximum bit counts.
27. Bloom filter implementation dropped unused heavy includes.
28. Replaced global skiplist tuning constants with explicit hardened constexpr values.
29. Added max skiplist key-size bound (`kMaxSkiplistKeyBytes`).
30. Added max skiplist entry bound (`kMaxSkiplistEntries`).
31. `Skiplist::get` now rejects invalid keys early.
32. `Skiplist::is_tombstoned` now rejects invalid keys early.
33. `Skiplist::insert` now rejects invalid keys early.
34. `Skiplist::insert` now refuses new inserts after configured max-entry bound.
35. Skiplist traversal now clamps loaded level to valid range before iterating.
36. Skiplist random-level generation now uses hardened constant bounds.
37. Added memtable key bound (`kMaxMemtableKeyBytes`).
38. `MemTable::put` now ignores invalid keys.
39. `MemTable::remove` now ignores invalid keys.
40. `MemTable::get` now ignores invalid keys.
41. `MemTable::is_tombstoned` now ignores invalid keys.
42. Added memory-engine encoded-key max bound (`kMaxEncodedMemoryKeyBytes`).
43. `MemoryStorageEngine::make_key` now rejects oversized final encoded keys.
44. Memory-engine query now rejects over-specified key-condition maps.
45. Memory-engine query now rejects empty hash-key fragments.
46. Memory-engine query now enforces max result count bound.
47. Memory-engine scan now fails-safe when `ExclusiveStartKey` cannot be encoded.
48. LSM `build_merged_view` now enforces `kMaxMergedEntries` to cap peak memory.
49. LSM WAL replay now enforces `kMaxWalReplayRecords` to cap startup replay cost.
50. Table metadata save now ensures parent directories exist before writing temp files.
51. Table metadata save now retries rename via remove+rename fallback on target conflicts.
52. HTTP session now closes socket on read errors instead of silently returning.
53. HTTP session now closes socket on write errors instead of silently returning.
54. HTTP close path now uses `shutdown_both` for cleaner connection teardown.
55. Context now validates `CYNAMODB_DATA_DIR` for empty/oversized input.
56. Context now normalizes data directory paths before engine initialization.
57. Context now surfaces data-directory creation failures as explicit exceptions.
58. Main process now bounds `CYNAMODB_BIND_ADDR` length before parsing.
59. Main process now validates bind address via non-throwing `error_code` path.
60. Main worker-thread reservation now uses safe bounded conversion.
61. API target parser now trims all whitespace characters (not just space/tab).
62. API target parser now enforces max target-header size bound.
63. API target parser now rejects malformed operation names (empty, non-alnum, numeric-leading).
64. SigV4 parser now enforces strictly ordered unique signed headers and requires `x-amz-date` to be signed.
