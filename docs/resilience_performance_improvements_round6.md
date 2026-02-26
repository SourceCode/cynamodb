# Resilience & Performance Improvements (Round 6, Implemented)

1. Changed WAL `sync()` to return a success/failure status for explicit durability signaling.
2. Changed WAL `reset()` to return a success/failure status for explicit checkpoint-reset signaling.
3. Added WAL flush-state validation in `sync()` (`file_` stream health check).
4. Added WAL pre-close flush-state validation in `reset()`.
5. Added WAL truncate-open failure detection in `reset()`.
6. Added WAL truncate-flush failure detection in `reset()`.
7. Added WAL reopen failure detection after reset/truncate.
8. Added runtime-configurable memtable flush threshold via `CYNAMODB_MEMTABLE_FLUSH_THRESHOLD`.
9. Added runtime-configurable compaction trigger threshold via `CYNAMODB_COMPACTION_TRIGGER_SSTABLES`.
10. Added runtime-configurable compaction input file count via `CYNAMODB_COMPACTION_INPUT_SSTABLES`.
11. Added strict numeric env parsing for LSM thresholds using `std::from_chars`.
12. Added minimum clamp for memtable flush threshold.
13. Added maximum clamp for memtable flush threshold.
14. Added minimum clamp for compaction trigger threshold.
15. Added maximum clamp for compaction trigger threshold.
16. Added minimum clamp for compaction input count.
17. Added cap forcing compaction input count not to exceed compaction trigger threshold.
18. Replaced hardcoded flush threshold usage in write path with runtime-configured value.
19. Replaced hardcoded compaction trigger usage with runtime-configured value.
20. Replaced hardcoded compaction input count usage with runtime-configured value.
21. Added per-instance LSM threshold fields to store runtime-tuned values.
22. Hardened LSM query fast path to require exact key-schema cardinality (no extra key conditions).
23. Added LSM `scan(limit=0)` fast return to avoid unnecessary merged-view work.
24. Hardened `MemoryStorageEngine` query fast path to require exact key-schema cardinality.
25. Added `MemoryStorageEngine` `scan(limit=0)` fast return.
26. Replaced API target linear operation lookup with hash-map lookup in `ApiDispatcher`.
27. Added operation-name length guard in API target parsing.
28. Introduced explicit `INVALID` token type in expression lexer.
29. Added lexer rejection for empty name placeholders (`#` without identifier).
30. Added lexer rejection for empty value placeholders (`:` without identifier).
31. Changed lexer unknown-character handling from silent EOF to explicit INVALID token emission.
32. Added explicit `DOT` tokenization support in lexer.
33. Added HTTP parser ceiling for expression attribute entries.
34. Added HTTP parser ceiling for item attribute entries.
35. Hardened JSON item-size accounting with saturating arithmetic, recursion-depth limits, and null-safe nested traversal.
36. Added `parse_attribute_map` entry-limit enforcement.
37. Added `parse_attribute_map` empty-attribute-name rejection.
38. Added `parse_attribute_map` duplicate-attribute-name rejection.
39. Added ExpressionAttributeNames size-limit enforcement.
40. Added ExpressionAttributeValues size-limit enforcement.
41. Added ExpressionAttributeNames duplicate-entry rejection.
42. Added ExpressionAttributeValues duplicate-entry rejection.
43. Added empty alias/key/value rejection in expression context parsing.
44. Hardened primary-key completeness helper to enforce non-empty, bounded, exact key-shape matches.
45. Added explicit table index existence validation helper for GSI/LSI names.
46. Added `PutItem` validation that `Item` contains complete primary key.
47. Added `GetItem` validation requiring exact key-schema attributes.
48. Added `DeleteItem` validation requiring exact key-schema attributes.
49. Added `UpdateItem` validation requiring exact key-schema attributes.
50. Added post-expression `UpdateItem` 400KB size enforcement.
51. Added `Scan` validation requiring exact-key `ExclusiveStartKey` shape.
52. Added `Scan` validation for `IndexName` existence.
53. Changed `Scan` execution to pass validated `IndexName` through to storage engine.
54. Added `Query` validation for `IndexName` existence.
55. Added per-key primary-key shape validation inside `BatchGetItem` processing.
56. Added `TransactWriteItems` validation requiring exactly one operation per item.
57. Added transaction `Put` key-completeness validation.
58. Added transaction `Put` 400KB item-size enforcement.
59. Added transaction `Delete` key-completeness validation.
60. Added transaction `Update` key-completeness validation.
61. Added transaction `Update` 400KB item-size enforcement.
62. Added transaction `ConditionCheck` key-completeness validation.
63. Hardened SigV4 parsing with bounded auth-header size, bounded region/header-token lengths, signed-header-count cap, and stricter access-key length constraints.
64. Hardened request error precedence/auth verification by running auth checks only when prior validation passed and requiring signed headers to be present and non-empty.
