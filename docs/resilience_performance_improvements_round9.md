# Resilience and Performance Improvements (Round 9)

Implemented improvements in this round:

1. Added `KeyManager::kMaxKeyComponentBytes` to bound individual key parts.
2. Added shared key-schema resolution helper for table and GSI encoders.
3. Key-schema resolution now rejects empty key attribute names.
4. Key-schema resolution now rejects missing HASH keys.
5. Key-schema resolution now rejects duplicate HASH keys.
6. Key-schema resolution now rejects duplicate RANGE keys.
7. Key-schema resolution now rejects unknown key-type values.
8. Consolidated table and GSI encoding through one hardened encode path.
9. Composite-key encoding now rejects missing HASH attributes.
10. Composite-key encoding now rejects null HASH attribute pointers.
11. Composite-key encoding now rejects null RANGE attribute pointers when present.
12. Composite-key encoding now rejects empty encoded HASH fragments.
13. Composite-key encoding now enforces uint16 HASH-length framing bounds.
14. Composite-key encoding now reserves output capacity up front.
15. Binary key encoding now avoids extra vector copy in `attribute_to_string`.
16. Expression parser now fails fast on any `INVALID` token.
17. Expression evaluator now enforces a max recursion depth.
18. `evaluate_condition` now routes through depth-limited implementation.
19. `evaluate_update` now routes through depth-limited implementation.
20. Numeric parsing switched from exception-based `std::stod` to `std::strtod` fast path.
21. Numeric parsing now rejects empty numeric strings.
22. Numeric parsing now rejects ERANGE overflow/underflow values.
23. Numeric parsing now rejects trailing non-numeric characters.
24. Numeric parsing now rejects non-finite values.
25. Identifier resolution now uses single `find` lookups (no `contains` + `at`).
26. Name placeholder resolution now uses single `find` lookups.
27. Value placeholder resolution now uses single `find` lookups.
28. `get_attribute_name` now resolves placeholders with one map lookup.
29. `ATTRIBUTE_EXISTS` checks now use `find` directly.
30. SigV4 `extract_param` now requires a valid token boundary before parameter names.
31. SigV4 duplicate-parameter counting now ignores lookalike-prefixed names.
32. SigV4 parser now enforces max credential-parameter size.
33. SigV4 parser now enforces access-key segment length bounds.
34. SigV4 parser now enforces credential date-segment length bounds.
35. SigV4 parser now enforces region/service/request-type segment length bounds.
36. SigV4 parser is now resilient to `XCredential`-style prefix spoofing.
37. WAL record size constants were centralized for safer checks.
38. WAL append now enforces a combined key+value record-size bound.
39. WAL sync now skips flush when there are no unsynced writes.
40. WAL reset now ensures parent directories exist before truncation/reopen.
41. WAL replay JSON parsing now rejects oversized payloads.
42. WAL replay JSON parsing now enforces max per-item attribute count.
43. WAL replay JSON parsing now enforces attribute-name length limits.
44. WAL replay JSON parsing now rejects duplicate attribute keys.
45. SSTable cache population now double-checks existing entries under write lock.
46. SSTable cache now has a hard cap and bounded clear behavior.
47. LSM `put_item` now serializes before acquiring the write lock.
48. LSM `put_item` now rejects oversized serialized-item payloads.
49. LSM `put_item` now rolls memtable at `>=` threshold for deterministic flushing.
50. LSM `delete_item` now participates in memtable rollover/flush triggering.
51. LSM query now rejects key-condition maps larger than key schema.
52. LSM query now caps result growth to protect memory.
53. LSM scan now returns early when `ExclusiveStartKey` cannot be encoded.
54. LSM SSTable discovery now uses non-throwing directory iteration.
55. LSM flush path now no-ops empty immutable memtables.
56. SSTable create now validates record-offset `tellp()` before use.
57. SSTable create now validates index-offset `tellp()` before use.
58. SSTable create now enforces a max file-size ceiling during write.
59. SSTable constructor now validates end-position `tellg()` before size casting.
60. HTTP expression placeholders now enforce max key/value placeholder lengths.
61. HTTP key-condition parsing now rejects expressions that resolve to zero conditions.
62. HTTP now validates `TableName` length/emptiness across all TableName handlers.
63. HTTP auth now validates and bounds the Host header before SigV4 verification.
64. `ProvisionedThroughput` fields now default-initialize to zero, removing uninitialized-schema risk.
