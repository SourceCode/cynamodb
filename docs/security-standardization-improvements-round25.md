# Security / Resilience / Performance Improvements Round 25

This round implements 64 additional improvements focused on fail-closed validation, runtime resilience, and hot-path efficiency.

1. Added explicit stream-label length bound constant in `StreamManager`.
2. Added explicit max sequence-number digit bound constant in `StreamManager`.
3. Added explicit max key-schema-entry bound constant in `StreamManager`.
4. Added explicit max key-attribute bound constant in `StreamManager`.
5. Added explicit max image-attribute bound constant in `StreamManager`.
6. Added `is_valid_stream_label` validation helper for stream labels.
7. Added strict `is_valid_event_name` whitelist (`INSERT`/`MODIFY`/`REMOVE`).
8. Added strict `is_valid_iterator_type` validator for iterator type tokens.
9. Added `is_scalar_key_value` guard for key attribute type safety.
10. Added `is_valid_key_schema` guard for HASH/RANGE ordering and uniqueness.
11. Added `has_valid_key_attributes` guard to enforce key-schema/key-map parity.
12. Added `has_valid_image` guard for optional stream image payloads.
13. `sync_table` now fail-closes when table names are invalid.
14. `sync_table` now fail-closes when key schema is malformed.
15. `sync_table` now fail-closes when stream ARN/label is malformed.
16. `sync_table` now rejects enabled stream specs without a view type.
17. `sync_table` now prevents cross-table overwrite when a stream ARN collides.
18. `remove_table` now no-ops on invalid table names.
19. `remove_table` now disables all in-memory stream states for the removed table.
20. `remove_table` now invalidates outstanding iterators for removed-table streams.
21. `append_record` now rejects invalid table names before locking.
22. `append_record` now rejects unknown event names.
23. `append_record` now validates stream-state ARN/label/schema before write.
24. `append_record` now validates key payload shape against key schema.
25. `append_record` now validates optional image maps for malformed entries.
26. `append_record` now clamps negative epoch time to `0`.
27. `list_streams` now skips corrupted stream states instead of exposing them.
28. `describe_stream` now fails closed for corrupted stream state.
29. `create_shard_iterator` now validates iterator-type token format.
30. `create_shard_iterator` now rejects sequence numbers for `LATEST`/`TRIM_HORIZON`.
31. `create_shard_iterator` now re-validates stream state before issuing iterators.
32. `create_shard_iterator` now rejects zero/invalid `AT_`/`AFTER_` sequence numbers.
33. `create_shard_iterator` now collision-checks generated iterator tokens.
34. `get_records` now validates iterator token charset (`[A-Za-z0-9-]`).
35. `get_records` now pre-reserves output record capacity for lower allocation churn.
36. `parse_sequence_number` now enforces 21-40 digit bounds.
37. `parse_sequence_number` now rejects zero.
38. `prune_records` now advances stale iterator positions after trim.
39. Added explicit backup metadata/snapshot schema bound constants.
40. Added reusable `has_only_fields` object-schema validator in backup manager.
41. Added `is_valid_table_arn` validator scoped to the owning table name.
42. Added `is_valid_backup_arn_for_table` validator scoped to the owning table name.
43. Hardened snapshot filename validation (`..`, dotfile, and charset rejection).
44. `parse_backup_record` now enforces exact required field count.
45. `parse_backup_record` now rejects unknown fields.
46. Metadata top-level parsing now enforces exact expected field count.
47. Metadata top-level parsing now rejects unknown fields.
48. Metadata load now rejects backup/table ARN scope mismatches.
49. Metadata load now rejects duplicate snapshot filenames.
50. Metadata load now rejects out-of-bounds `ItemCount`/`BackupSizeBytes`.
51. Metadata save now re-validates scoped ARNs and size constraints.
52. Metadata save now enforces unique snapshot filenames.
53. Metadata save now enforces maximum serialized metadata JSON size.
54. `create_backup` now validates key-schema presence and key-definition references.
55. `create_backup` now validates generated backup ARN/table ARN/snapshot invariants.
56. `create_backup` now bounds embedded source-table metadata JSON size.
57. `create_backup` now rejects empty or oversized scanned items.
58. `delete_backup` now validates snapshot filename before filesystem delete.
59. `load_backup_snapshot` now validates snapshot filename before read.
60. `load_backup_snapshot` now enforces exact top-level snapshot schema.
61. `load_backup_snapshot` now validates metadata/PITR object shapes and optional stream fields.
62. `load_backup_snapshot` now cross-checks loaded item count and backup-size metadata integrity.
63. SigV4 date parsing now uses no-throw fixed-digit parsing and validates full `x-amz-date` calendar/time ranges.
64. Block cache hot-paths now use splice-based LRU updates, explicit zero-capacity disable semantics, and tighter capacity/erase guards.
