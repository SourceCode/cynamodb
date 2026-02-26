# Resilience and Performance Improvements (Round 13)

Implemented improvements in this round:

1. Switched immutable memtable backlog storage from `std::vector` to `std::deque` for O(1) front operations.
2. Added LSM max DB path bound constant (`kMaxDbPathBytes`).
3. Added LSM max env-value parse length constant (`kMaxEnvValueBytes`).
4. Added LSM max immutable memtable backlog bound (`kMaxImmutableMemtables`).
5. Added LSM max discovered SSTable count bound (`kMaxDiscoveredSstables`).
6. Added LSM max GSI engine fanout bound (`kMaxGsiEngines`).
7. Added LSM table-name length bound constant (`kMaxTableNameBytes`).
8. `parse_env_size` now rejects oversized env values before parsing.
9. Added centralized LSM table-name format validation helper.
10. Added centralized helper validating key-condition maps are key-schema subsets.
11. LSM constructor now rejects empty/oversized DB paths.
12. LSM constructor now lexically normalizes DB paths.
13. LSM constructor now re-validates normalized DB path bounds.
14. LSM constructor now creates a fallback arena when passed `nullptr`.
15. LSM constructor now creates directories through `error_code` path and throws on failure.
16. LSM constructor now pre-reserves SSTable path vector capacity.
17. LSM constructor now pre-reserves SSTable cache buckets.
18. Flush worker now dequeues immutable memtables via `pop_front()`.
19. Flush retry path now requeues failed memtables via `push_front()`.
20. Destructor drain now dequeues immutable memtables via `pop_front()`.
21. Destructor retry path now requeues via `push_front()`.
22. SSTable discovery now pre-reserves discovery vector capacity.
23. SSTable discovery now enforces a hard max discovered-file count.
24. SSTable discovery now skips empty/oversized candidate SSTable paths.
25. SSTable cache lookup now rejects empty/oversized cache keys early.
26. SSTable cache population now skips caching empty/invalid SSTables.
27. SSTable cache eviction now drops one entry instead of clearing the entire cache.
28. Memtable flush path now snapshots entries once before serialization.
29. Memtable flush path now rejects oversized snapshots above merged-entry cap.
30. Memtable flush path now rejects empty/oversized encoded keys.
31. Memtable flush path now enforces serialized-entry growth cap while building output.
32. Compaction trigger now exits early when no input SSTables are selected.
33. Compaction input set is now deduplicated before compaction runs.
34. WAL replay now checks WAL file size before opening replay stream.
35. WAL replay now truncates oversized WAL files to zero for fail-safe startup.
36. WAL replay now truncates to last good offset when replay-record cap is reached.
37. `put_item` now rejects invalid table names before write path work.
38. `put_item` now rejects empty items and oversized attribute-count inputs.
39. `put_item` now rejects empty/placeholder serialized JSON payloads.
40. `put_item` now enforces immutable backlog cap before rotating active memtable.
41. `put_item` now enforces max GSI engine instance count before creating new child engines.
42. `delete_item` now rejects invalid table names before write path work.
43. `delete_item` now enforces immutable backlog cap before rotating active memtable.
44. `query` now pre-reserves result capacity to reduce realloc churn.
45. `query` now rejects invalid table names before query planning.
46. `query` now rejects key-condition maps containing non-key attributes.
47. `query` now rejects key-condition maps containing null attribute pointers.
48. `query` now rejects missing/null hash-condition values early.
49. `scan` now rejects invalid table names before merged-view work.
50. `scan` now clamps explicit limits to max supported query result size.
51. `scan` now applies a bounded default limit when no limit is provided.
52. `scan` now pre-reserves output vector based on effective limit.
53. `scan` iteration now starts from `upper_bound(ExclusiveStartKey)` for faster pagination.
54. Table-manager identifier validation now enforces allowed characters (`[A-Za-z0-9_.-]`).
55. Added metadata-path length guard (`kMaxMetadataPathBytes`).
56. Added metadata-file size guard (`kMaxMetadataFileBytes`).
57. Added centralized metadata-path validity helper.
58. Metadata parse now rejects invalid metadata paths before open.
59. Metadata parse now rejects oversized metadata files before decode.
60. TableManager constructor now throws on invalid metadata path input.
61. TableManager constructor now uses non-throwing directory creation and throws on failure.
62. Metadata load now short-circuits when manager path is invalid.
63. Metadata load now quarantines unreadable primary metadata files to `.corrupt`.
64. Metadata save now validates path, table count, and full table schema invariants before writing.
