# Resilience & Performance Improvements (Round 3, Implemented)

1. Upgraded table metadata format to explicit magic/versioned binary layout (`v3`).
2. Added CRC32C checksum coverage for `v3` metadata payloads.
3. Added bounded string-length validation during metadata load.
4. Added bounded table-count validation during metadata load.
5. Added bounded key-schema-entry validation during metadata load.
6. Added bounded attribute-definition-count validation during metadata load.
7. Added strict key-type byte validation during metadata decode.
8. Added strict attribute-type byte validation during metadata decode.
9. Added strict table-name identifier validation for metadata load/create.
10. Added strict attribute-name identifier validation for key schema.
11. Added strict TTL attribute-name identifier validation.
12. Added key-schema duplicate-attribute rejection in table manager.
13. Added strict key-shape validation (must have HASH, at most one RANGE).
14. Added key-schema-to-attribute-definition cross-validation.
15. Added backup metadata (`.bak`) fallback if primary metadata is corrupt.
16. Added automatic primary metadata rewrite from valid backup fallback.
17. Added atomic metadata persistence via temp-file write + rename.
18. Added pre-rename metadata backup creation of previous primary file.
19. Added metadata parent-directory auto-creation in `TableManager` ctor.
20. Added table-create validation: TTL attribute must exist in schema.
21. Added TTL-update validation: attribute must exist in table schema.
22. Added `Skiplist::SnapshotEntry` API carrying tombstone state.
23. Added `Skiplist::get_all_entries()` to snapshot deleted/non-deleted rows.
24. Added `MemTable::get_all_entries()` passthrough snapshot API.
25. Added SSTable index validation for strictly increasing keys.
26. Added SSTable index validation for strictly increasing record offsets.
27. Added explicit tombstone decode in SSTable (`attr_count == 0`).
28. Added SSTable bulk sequential read API (`read_all_records()`).
29. Refactored SSTable single-record reads through shared checked reader helper.
30. Switched compaction merge input from per-key random reads to bulk sequential reads.
31. Switched LSM merged-view construction to SSTable bulk sequential reads.
32. Converted LSM memtable flush to return success/failure status.
33. Gated startup WAL-reset checkpoint on successful replay flush only.
34. Preserved recovered startup memtable when checkpoint flush fails.
35. Added flush-thread requeue of immutable memtables on flush failure.
36. Added flush-thread retry backoff after flush failure to avoid tight spin.
37. Added shutdown flush failure preservation (do not silently drop immutable state).
38. Added WAL append failure propagation in `put_item` path.
39. Added WAL append failure propagation in `delete_item` path.
40. Changed WAL append API to return write success/failure.
41. Added `BatchWriteItem` failure propagation and per-item size validation on server write path.
42. Added `KeyConditionExpression` conjunction flattening (`AND`) for multi-key equality extraction.
43. Added key-encoding guard: reject hash keys larger than `uint16_t` max.
44. Added equivalent oversized-hash-key guard for GSI key encoding.
45. Added query exact-primary-key fast path to avoid full merged scans.
46. Fixed scan exclusive-start behavior to use strict `>` progression.
47. Fixed scan pagination marker semantics to emit last returned item key.
48. Added hash+range table support for hash-only query in `MemoryStorageEngine`.
49. Fixed `MemoryStorageEngine` scan exclusive-start semantics (`>` behavior).
50. Fixed `MemoryStorageEngine` pagination marker semantics (last returned key).
51. Added strict trailing-token rejection in expression parser.
52. Hardened lexer character classification with unsigned-char safe casts.
53. Added lexer support for `ATTRIBUTE_NOT_EXISTS` keyword tokenization.
54. Added case-insensitive function-name handling in expression evaluator.
55. Extended JSON numeric validation to support scientific notation.
56. Tightened JSON numeric validation to reject malformed trailing-dot literals.
57. Added JSON table-definition rejection of duplicate key-schema attributes.
58. Added JSON table-definition rejection of duplicate attribute definitions.
59. Added JSON table-definition rejection of empty key-schema attribute names.
60. Added JSON table-definition rejection of empty attribute-definition names.
61. Added JSON table-definition rejection of multiple HASH keys.
62. Added JSON table-definition rejection of multiple RANGE keys.
63. Hardened runtime config parsing in `main` using strict `std::from_chars` validation for port/thread env vars.
64. Added SigV4/HTTP strictness: enforce DynamoDB service token, signed-header token validation, and required `Host` header presence at request handling time.
