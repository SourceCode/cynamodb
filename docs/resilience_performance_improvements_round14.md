# Resilience and Performance Improvements (Round 14)

Implemented improvements in this round:

1. Switched immutable memtable backlog storage from `std::vector` to `std::deque` to make front dequeue/requeue operations O(1).
2. Added `kMaxDbPathBytes` to hard-bound LSM database path size.
3. Added `kMaxEnvValueBytes` to hard-bound LSM env-var parse input size.
4. Added `kMaxImmutableMemtables` to cap backlog growth under sustained write pressure.
5. Added `kMaxDiscoveredSstables` to cap startup SSTable discovery work.
6. Added `kMaxGsiEngines` to cap auto-spawned GSI engine count.
7. Added `kMaxTableNameBytes` to hard-bound table name size in the LSM path.
8. Hardened `parse_env_size` to ignore oversized env values instead of parsing them.
9. Added centralized LSM table-name validation helper (`is_valid_table_name`).
10. Added centralized key-condition validation helper (`is_key_condition_subset`).
11. LSM constructor now rejects empty/oversized database paths.
12. LSM constructor now normalizes DB paths with `lexically_normal()`.
13. LSM constructor now re-validates normalized DB paths for size/emptiness.
14. LSM constructor now allocates a fallback arena when `nullptr` is provided.
15. LSM constructor now creates directories through `error_code` flow and throws on failure.
16. LSM constructor now pre-reserves SSTable path vector capacity.
17. LSM constructor now pre-reserves SSTable cache buckets.
18. Flush worker now dequeues immutable memtables via `pop_front()`.
19. Flush retry path now requeues failed immutable memtables via `push_front()`.
20. Destructor drain path now dequeues immutable memtables via `pop_front()`.
21. Destructor retry path now requeues via `push_front()` on flush failure.
22. SSTable discovery now pre-reserves temporary discovery vector capacity.
23. SSTable discovery now enforces a hard upper bound on discovered files.
24. SSTable discovery now skips empty/oversized candidate SSTable paths.
25. SSTable cache lookup now rejects empty/oversized cache keys.
26. SSTable cache insertion now skips caching empty/invalid SSTable instances.
27. SSTable cache overflow now evicts one entry instead of clearing the entire cache.
28. Memtable flush now snapshots entries once before serialization.
29. Memtable flush now rejects oversized snapshots above merged-entry cap.
30. Memtable flush now rejects empty/oversized encoded keys.
31. Memtable flush now enforces serialized-entry count cap while materializing output.
32. Compaction scheduler now exits early if selected input set is empty.
33. Compaction input list is now deduplicated before invoking compactor.
34. WAL replay now pre-checks WAL file size before opening replay stream.
35. WAL replay now truncates oversized WAL files to zero for fail-safe startup.
36. WAL replay now truncates to last good byte offset when replay-record cap is hit.
37. `put_item` now rejects invalid table names before write-path work.
38. `put_item` now rejects empty items and oversized attribute-count inputs.
39. `put_item` now rejects empty/placeholder (`"{}"`) and oversized serialized payloads.
40. `put_item` now enforces immutable backlog cap before memtable rotation.
41. `put_item` now enforces GSI engine-count cap before creating child engines.
42. `delete_item` now rejects invalid table names before write-path work.
43. `delete_item` now enforces immutable backlog cap before memtable rotation.
44. `query` now pre-reserves result vector capacity to reduce reallocation churn.
45. `query` now validates table name plus key-condition size/subset/null invariants before scan work.
46. `scan` now applies bounded limits, pre-reserves output capacity, and starts from `upper_bound(ExclusiveStartKey)`.
47. Table-manager identifier validation now requires non-empty names and `[A-Za-z0-9_.-]` characters.
48. Added metadata-path size bound (`kMaxMetadataPathBytes`).
49. Added metadata-file size bound (`kMaxMetadataFileBytes`).
50. Added centralized metadata-path validity helper (`is_valid_metadata_path`).
51. Metadata parser now rejects invalid metadata paths before opening files.
52. Metadata parser now rejects oversized metadata files before decode.
53. `TableManager` constructor now throws `invalid_argument` for invalid metadata paths.
54. `TableManager` constructor now uses non-throwing directory creation and throws on failure.
55. `load_metadata` now short-circuits invalid manager paths and quarantines unreadable primary metadata to `.corrupt` when backup recovery fails.
56. `save_metadata` now pre-validates path/table-count/schema invariants and removes stale `.tmp` before rewrite.
57. `Context` now validates `CYNAMODB_DATA_DIR` length before filesystem operations.
58. `Context` now rejects empty normalized data-directory paths.
59. `Context` now verifies created data path is a directory before startup continues.
60. `Context` now enforces metadata path length bounds before constructing `TableManager`.
61. Added strict bounded `parse_uint_env` helper in `main` for numeric env parsing.
62. `main` now uses strict bounded parsing for `CYNAMODB_PORT` and `CYNAMODB_THREADS`.
63. `main` now uses a safe default thread count when hardware concurrency reports zero.
64. SigV4 authorization parsing now rejects control characters and enforces a cumulative signed-header byte cap (`kMaxSignedHeadersBytes`).
