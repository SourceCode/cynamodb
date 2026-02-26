# Resilience & Performance Improvements (Round 2, Implemented)

1. Added `SSTable` forward declaration in LSM header to prevent incomplete-type build hazards.
2. Added WAL `reset()` API for explicit log checkpoint truncation.
3. WAL destructor now flushes before close to reduce tail-loss risk.
4. LSM now checkpoints recovered WAL data into SSTable at startup.
5. LSM now truncates WAL after startup replay checkpoint.
6. LSM replay now returns replayed-record count for operational decisions.
7. LSM now persists tombstones into SSTables (empty-record tombstones).
8. SSTable now exposes `get_record()` to distinguish tombstone vs not-found.
9. SSTable `get()` now consistently suppresses tombstoned records.
10. SSTable now exposes `keys()` for deterministic iteration and compaction.
11. Added Bloom filter member to SSTable for read-path key rejection.
12. SSTable constructor now builds Bloom filter from index keys.
13. SSTable lookups now short-circuit via Bloom filter negative checks.
14. Replaced compactor stub with real merge-compaction logic.
15. Compactor now preserves newest-write-wins ordering across input SSTables.
16. Compactor now preserves tombstone semantics through compaction.
17. LSM now triggers compaction when SSTable count threshold is reached.
18. LSM compaction now removes old compacted files from disk.
19. LSM compaction now updates in-memory SSTable list atomically.
20. LSM now caches SSTable reader objects by path.
21. LSM cache now avoids repeated SSTable index reloads on hot reads.
22. LSM cache now evicts compacted-away SSTable readers.
23. Added merged-view builder across SSTables + immutable memtables + memtable.
24. `scan()` now reads merged view instead of memtable-only data.
25. `scan()` now honors tombstones across all levels.
26. `scan()` pagination now uses merged key order for deterministic results.
27. `query()` now evaluates over merged visible data, not point-lookup only.
28. `query()` now supports multi-attribute equality condition matching.
29. Added robust attribute equality helper for string/number/bool/binary matching.
30. LSM flush now serializes full memtable snapshot including delete state.
31. LSM startup now discovers SSTables then rehydrates cache lazily.
32. Added `MemTable::get_all_entries()` for snapshotting deleted/non-deleted rows.
33. Added typed `MemTable` aliases (`Attributes`, `SnapshotEntry`) for safety.
34. Added `Skiplist::SnapshotEntry` type for explicit tombstone state transport.
35. Added `Skiplist::get_all_entries()` for consistent flush/recovery behavior.
36. Upgraded CRC32C implementation to constexpr table-driven algorithm.
37. Added `crc32c_extend` for incremental checksum composition.
38. Updated WAL checksum path to incremental CRC composition.
39. Updated recovery checksum path to incremental CRC composition.
40. Updated LSM WAL replay checksum path to incremental CRC composition.
41. Added strict HTTP method gate (`POST` only) in request handler.
42. Added explicit `Allow: POST` response on method violations.
43. Added strict Content-Type validation for Dynamo JSON payloads.
44. Added generated `x-amzn-requestid` header to all responses.
45. Added request-byte metric accounting.
46. Added response-byte metric accounting.
47. Added success counter metric (in addition to request/error counters).
48. Added min-latency tracking metric.
49. Added max-latency tracking metric.
50. Added latency extreme CAS update path for lock-free metric updates.
51. Added BatchGet per-table key-count enforcement.
52. Added BatchWrite per-table request-count enforcement.
53. Added TransactWrite maximum item-count enforcement.
54. Added TransactGet maximum item-count enforcement.
55. Added transaction-level mutex in shared Context for serialized transaction execution.
56. Added validation for `PutItem` `ReturnValues` enum values.
57. Added validation for `UpdateItem` `ReturnValues` enum values.
58. Added HASH-key presence validation for `Query` key conditions.
59. Lexer now classifies expression keywords case-insensitively.
60. Parser now supports boolean expression precedence (`OR`/`AND`/`NOT`).
61. Parser now supports parenthesized expressions for grouping.
62. Evaluator now supports boolean composition (`AND`, `OR`, `NOT`).
63. Evaluator now supports comparison operators (`=`, `<>`, `<`, `<=`, `>`, `>=`) for string/number/bool.
64. Added environment-configurable server/runtime settings (`CYNAMODB_DATA_DIR`, `CYNAMODB_BIND_ADDR`, `CYNAMODB_PORT`, `CYNAMODB_THREADS`).

