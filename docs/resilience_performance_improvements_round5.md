# Resilience & Performance Improvements (Round 5, Implemented)

1. Changed WAL `sync()` to return success/failure for explicit durability signaling.
2. Changed WAL `reset()` to return success/failure for explicit recovery-path signaling.
3. Added WAL `sync()` flush-state validation (`file_` stream health check).
4. Added WAL `reset()` pre-close flush-state validation.
5. Added WAL `reset()` truncate-open failure detection.
6. Added WAL `reset()` truncate-flush failure detection.
7. Added WAL `reset()` reopen failure detection.
8. Added runtime-configurable memtable flush threshold via `CYNAMODB_MEMTABLE_FLUSH_THRESHOLD`.
9. Added runtime-configurable compaction trigger threshold via `CYNAMODB_COMPACTION_TRIGGER_SSTABLES`.
10. Added runtime-configurable compaction input count via `CYNAMODB_COMPACTION_INPUT_SSTABLES`.
11. Added strict numeric env parsing for LSM thresholds using `std::from_chars`.
12. Added lower-bound clamps for env-driven memtable flush threshold.
13. Added upper-bound clamps for env-driven memtable flush threshold.
14. Added lower-bound clamps for env-driven compaction trigger threshold.
15. Added upper-bound clamps for env-driven compaction trigger threshold.
16. Added lower-bound clamps for env-driven compaction input size.
17. Added cap of compaction input size to compaction trigger threshold.
18. Replaced static memtable flush threshold usage with runtime-configurable value.
19. Replaced static compaction trigger usage with runtime-configurable value.
20. Replaced static compaction input count usage with runtime-configurable value.
21. Hardened LSM query fast-path to require exact key-condition cardinality (no extra attributes).
22. Added LSM `scan(limit=0)` short-circuit to return empty result immediately.
23. Hardened `MemoryStorageEngine` query fast-path to require exact key-condition cardinality.
24. Added `MemoryStorageEngine` `scan(limit=0)` short-circuit.
25. Added explicit `INVALID` token type to expression lexer.
26. Added lexer rejection for empty name placeholders (`#` without identifier).
27. Added lexer rejection for empty value placeholders (`:` without identifier).
28. Added lexer invalid-token emission for unknown characters instead of silent EOF truncation.
29. Added lexer support for explicit `DOT` token emission.
30. Added HTTP helper to validate full/exact primary-key attribute sets.
31. Added HTTP helper to validate `IndexName` against known GSIs/LSIs.
32. Added `PutItem` request validation requiring full primary key in `Item`.
33. Added `GetItem` request validation requiring exact primary-key attributes in `Key`.
34. Added `DeleteItem` request validation requiring exact primary-key attributes in `Key`.
35. Added `UpdateItem` request validation requiring exact primary-key attributes in `Key`.
36. Added post-update `UpdateItem` size enforcement for 400KB max item size.
37. Added `Scan` `ExclusiveStartKey` validation requiring exact primary-key attributes.
38. Added `Scan` `IndexName` existence validation.
39. Passed validated `Scan` `IndexName` through to storage engine instead of dropping it.
40. Added `Query` `IndexName` existence validation.
41. Added per-key primary-key completeness validation in `BatchGetItem`.
42. Added `TransactWriteItems` per-item operation-count validation (exactly one operation).
43. Added transaction `Put` key-completeness validation in `TransactWriteItems`.
44. Added transaction `Put` 400KB item-size enforcement.
45. Added transaction `Delete` key-completeness validation in `TransactWriteItems`.
46. Added transaction `Update` key-completeness validation in `TransactWriteItems`.
47. Added transaction `Update` 400KB item-size enforcement.
48. Added transaction `ConditionCheck` key-completeness validation.
49. Added `TransactGetItems` per-item operation-count validation (exactly `Get`).
50. Added `TransactGetItems` key-completeness validation for each `Get` request.
51. Prevented auth checks from overriding earlier request-validation errors by gating auth on `status == ok`.
52. Tightened signed-header verification to reject missing **or empty** required signed headers.
53. Added JSON parser scalar size limit for `S` attributes.
54. Added JSON parser scalar size limit for `N` attributes.
55. Added JSON parser scalar size limit for `B` attributes.
56. Added JSON parser entry-count limit for `SS` collections.
57. Added JSON parser entry-count limit for `NS` collections.
58. Added JSON parser entry-count limit for `BS` collections.
59. Added JSON parser entry-count limit for `L` collections.
60. Added JSON parser entry-count limit for `M` maps.
61. Added JSON parser per-element size checks for set members (`SS`/`NS`/`BS`).
62. Added JSON parser map-key validation (non-empty and bounded attribute-name length).
63. Added JSON table-definition validation for provisioned throughput bounds (non-zero and capped).
64. Hardened SigV4 parsing with bounded auth-header size, bounded region/header lengths, signed-header-count cap, and stricter access-key length constraints.
