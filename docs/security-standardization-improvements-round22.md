# Security, Resilience, And Performance Improvements (Round 22)

The following 64 additional improvements were implemented in this round.

1. Added max backups-directory path length guard (`kMaxBackupsDirPathBytes`).
2. Added max embedded table-definition payload guard for backup snapshots.
3. Added explicit backup-status validator (`AVAILABLE`/`CREATING`/`DELETED`).
4. Added explicit backup-type validator (`USER`/`SYSTEM`/`AWS_BACKUP`).
5. Added backups-directory path validator to reject control characters.
6. Added constructor validation that backups directory path is syntactically safe.
7. Added constructor check to reject non-directory backups paths.
8. Added `read_file_limited` guard against `uintmax_t` to `size_t` overflow.
9. Added strict `read_file_limited` byte-count verification via `gcount`.
10. Hardened metadata loading to reject invalid backup statuses.
11. Hardened metadata loading to reject invalid backup types.
12. Hardened metadata saving to reject invalid backup statuses.
13. Hardened metadata saving to reject invalid backup types.
14. Added backup creation guard for oversized serialized source table definitions.
15. Added running snapshot-byte accounting during backup creation.
16. Added pre-write snapshot size bound checks before each item append.
17. Added final suffix write bound check before snapshot close.
18. Added list-backups validation to reject unknown backup types.
19. Added restore/snapshot-load guard for oversized embedded table definitions.
20. Added restore/snapshot-load guard to verify source table name validity.
21. Added restore/snapshot-load guard to verify snapshot table name matches backup record.
22. Added stream-manager max iterator default (`32768`) for bounded state growth.
23. Added stream-manager max iterator lower bound (`1024`) for config sanity.
24. Added stream-manager max iterator upper bound (`131072`) for config sanity.
25. Added stream-iterator default TTL seconds (`900`) as explicit runtime constant.
26. Added stream-iterator TTL min bound (`60s`) for config safety.
27. Added stream-iterator TTL max bound (`3600s`) for config safety.
28. Added stream ARN max-length guard constant.
29. Added stream iterator-token max-length guard constant.
30. Added table-name max-length guard constant in streams manager.
31. Added shared helper to detect control characters in streams manager inputs.
32. Added strict identifier validator for stream/table identifiers.
33. Added strict stream-ARN validator requiring DynamoDB ARN + `/stream/` segment.
34. Added strict table-name validator for list-stream filtering inputs.
35. Added bounded env size parser for stream runtime tuning.
36. Added configurable iterator TTL loader (`CYNAMODB_STREAM_ITERATOR_TTL_SECONDS`).
37. Added configurable max iterators loader (`CYNAMODB_STREAM_MAX_ITERATORS`).
38. Added fixed-width hex encoder utility for opaque token construction.
39. Added splitmix64 mixer utility for token entropy hardening.
40. Hardened `list_streams` to validate optional `table_name`.
41. Hardened `list_streams` to validate optional `exclusive_start_stream_arn`.
42. Hardened `describe_stream` to validate `stream_arn`.
43. Hardened `describe_stream` to validate `exclusive_start_shard_id`.
44. Hardened `create_shard_iterator` to validate `stream_arn`.
45. Added configurable iterator TTL usage in shard-iterator creation.
46. Added max-iterator eviction loop before inserting new iterator state.
47. Hardened `get_records` to reject malformed iterator tokens (format/length/control chars).
48. Added configurable iterator TTL refresh in `get_records`.
49. Replaced predictable `iter-<counter>` tokens with mixed-entropy opaque tokens.
50. Hardened shard-id validator to reject control characters.
51. Hardened sequence-number parser to return invalid on numeric overflow.
52. Added bounded-iterator enforcement in periodic iterator pruning.
53. Added deterministic oldest-expiry eviction policy when iterators exceed configured cap.
54. Added regression test: list-streams rejects invalid table names.
55. Added regression test: describe-stream rejects invalid stream ARNs.
56. Added regression test: create-shard-iterator rejects invalid stream ARNs.
57. Added regression test: create-shard-iterator rejects overflowed sequence numbers.
58. Added regression test: iterator tokens are opaque with bounded format.
59. Added regression test: malformed iterator-token format is rejected.
60. Added regression test: backup manager rejects file-path-as-directory misuse.
61. Added regression test: list-backups rejects unknown backup types.
62. Added regression test: list-backups rejects control-character backup types.
63. Extended unit-test target with dedicated stream-manager and backup-manager coverage.
64. Rebuilt and re-ran full test suite to verify no regressions after hardening.

## Files

- `src/backups/manager.cpp`
- `src/streams/manager.cpp`
- `tests/test_backups_manager.cpp`
- `tests/test_streams_manager.cpp`
- `tests/CMakeLists.txt`
