# Resilience & Performance Improvements (Round 4, Implemented)

1. Added strict trailing-byte rejection for metadata `v2` files after successful decode.
2. Added strict trailing-byte rejection for metadata `v3` files after successful decode.
3. Added strict trailing-byte rejection for legacy metadata files after successful decode.
4. Ensured metadata load starts from a clean in-memory state (`tables_` cleared before parse attempts).
5. Ensured metadata load resets dirty-state before parse attempts.
6. Preserved backup-fallback flow while enforcing strict EOF validation on parsed metadata.
7. Changed SSTable bulk-read API to explicit failure-aware return type (`optional<vector<...>>`).
8. Added SSTable create-path temp-file strategy (`.tmp`) to avoid partial final-file writes.
9. Added SSTable create-path cleanup if output file cannot be opened.
10. Added SSTable create-time key-length guard for oversized on-disk keys.
11. Added SSTable create-time key non-empty guard.
12. Added SSTable create-time attribute-count upper-bound guard.
13. Added SSTable create-path temp cleanup on validation failure before write completion.
14. Added SSTable create-path temp cleanup on stream flush/write failure.
15. Added SSTable atomic rename from temp path to final path.
16. Added SSTable temp cleanup on rename failure.
17. Added SSTable bulk-read `nullopt` return on file-open failure.
18. Added SSTable bulk-read `nullopt` return on record parse mismatch/corruption.
19. Updated compactor to fail fast if an input SSTable cannot be bulk-read safely.
20. Updated compactor to consume validated bulk records instead of assuming reads always succeed.
21. Updated LSM merged-view builder to consume failure-aware SSTable bulk reads.
22. Updated LSM merged-view builder to skip unreadable/corrupt SSTables instead of dereferencing invalid reads.
23. Reduced flush-thread lock contention by releasing `data_mutex_` before retry backoff sleep.
24. Added compaction-output cleanup when compaction merge fails.
25. Added compaction-output cleanup on replacement-race failure path (`sstables_` changed before apply).
26. Added WAL replay tracking of last-known-good byte offset.
27. Added WAL replay truncation to last-good offset when a corrupt/partial record is encountered.
28. Ensured WAL replay truncation uses filesystem-level resize for deterministic recovery behavior.
29. Added encoded-key global size ceiling constant (`KeyManager::kMaxEncodedKeyBytes`).
30. Added composite primary-key output size guard against oversized encoded keys.
31. Added GSI composite-key output size guard against oversized encoded keys.
32. Added batch-table upper bound constant for request fan-out control.
33. Added request-target header size upper bound constant for header abuse resistance.
34. Added explicit key-schema attribute membership checker for query validation.
35. Added scan `Limit` validation rejecting zero-valued limits.
36. Added scan `Limit` validation rejecting overflow beyond `uint32_t` range.
37. Added query validation rejecting `KeyConditionExpression` attributes not in key schema.
38. Added BatchGet validation for empty `RequestItems` maps.
39. Added BatchGet validation for excessive `RequestItems` table counts.
40. Added BatchGet validation requiring `Keys` member per table entry.
41. Added BatchGet validation rejecting empty `Keys` arrays.
42. Added BatchWrite validation for empty `RequestItems` maps.
43. Added BatchWrite validation for excessive `RequestItems` table counts.
44. Added BatchWrite validation rejecting empty per-table request arrays.
45. Added BatchWrite validation requiring exactly one action (`PutRequest` xor `DeleteRequest`).
46. Added BatchWrite PutRequest validation requiring full key coverage.
47. Added BatchWrite DeleteRequest validation requiring full key coverage.
48. Added TransactWrite validation rejecting empty `TransactItems` arrays.
49. Added TransactGet validation rejecting empty `TransactItems` arrays.
50. Added request-level validation rejecting oversized `X-Amz-Target` headers.
51. Added auth consistency check requiring all signed headers to be present on the HTTP request.
52. Added SigV4 parser support for quoted authorization parameter values.
53. Added SigV4 access-key token validation (alphanumeric credential id requirement).
54. Replaced sorted-set header dedupe with order-preserving dedupe for signed headers.
55. Added JSON attribute nesting depth ceiling to prevent pathological deep-recursion payloads.
56. Refactored JSON AttributeValue parsing into depth-aware recursive helper for centralized guard enforcement.
57. Added JSON `SS` duplicate-member rejection.
58. Added JSON `NS` duplicate-member rejection.
59. Added JSON `BS` duplicate-member rejection.
60. Added JSON table-definition guard for maximum table-name length.
61. Added JSON table-definition guard for key-schema size limits.
62. Added JSON table-definition guard for attribute-definition size limits.
63. Added runtime thread-count hard ceiling (`kMaxServerThreads`) to prevent overcommit from env misconfiguration.
64. Added strict final thread-count clamp after env parsing in `main`.
