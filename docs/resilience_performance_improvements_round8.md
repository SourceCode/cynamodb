# Resilience & Performance Improvements (Round 8, Implemented)

1. Added table-creation guard rejecting key schemas larger than configured maximum.
2. Added table-creation guard validating every attribute-definition name against identifier constraints.
3. Added table-creation guard rejecting creation once metadata table-count ceiling is reached.
4. Added table-creation guard requiring TTL target attribute type to be numeric (`N`) when TTL spec is provided.
5. Added TTL-update guard requiring existing TTL target attribute type to be numeric (`N`).
6. Added expression lexer maximum input-size guard (`64 KiB`) to prevent oversized expression parsing workloads.
7. Added expression parser maximum token budget guard to cap parse complexity.
8. Added expression parser maximum recursion-depth guard to prevent stack exhaustion.
9. Added expression parser maximum function-argument count guard.
10. Added parser fail-fast behavior returning `InvalidExpression` on token-budget overflow.
11. Added parser fail-fast behavior returning `InvalidExpression` on depth overflow.
12. Added parser fail-fast behavior returning `InvalidExpression` when function arg limit is exceeded.
13. Converted parser descent functions to depth-threaded signatures for deterministic recursion bounding.
14. Added lexer over-budget sentinel sequence (`INVALID` + EOF) for controlled rejection.
15. Added parser-path hardening so oversized/invalid lexer output is consistently rejected.
16. Added SigV4 calendar-date validator with leap-year handling.
17. Added SigV4 rejection for impossible credential-scope dates (e.g., invalid day-of-month).
18. Added SigV4 rejection for out-of-range years in credential-scope dates.
19. Added SigV4 helper to count authorization parameter occurrences.
20. Added SigV4 requirement of exactly one `Credential` parameter.
21. Added SigV4 requirement of exactly one `SignedHeaders` parameter.
22. Added SigV4 requirement of exactly one `Signature` parameter.
23. Strengthened SigV4 parse determinism by rejecting duplicated auth params early.
24. Added request-level auth check for oversized `Authorization` headers.
25. Added request-level auth check requiring valid `x-amz-date` header format.
26. Added request-level auth check enforcing `x-amz-date` date prefix matches credential-scope date.
27. Added `x-amz-date` structural validator (`YYYYMMDDThhmmssZ`) in HTTP layer.
28. Added SSTable write-path validation rejecting empty attribute names.
29. Added SSTable write-path validation rejecting oversized attribute names.
30. Added SSTable write-path validation rejecting oversized scalar attribute payloads.
31. Changed SSTable attribute serialization to return success/failure for robust error propagation.
32. Added SSTable write abort with temp cleanup when any attribute serialization fails.
33. Added SSTable create-path top-level record-count bound check before serialization.
34. Added SSTable create-path parent-directory auto-creation.
35. Added SSTable create-path parent-directory error handling.
36. Replaced SSTable unsupported-type placeholder persistence with deterministic JSON-string fallback encoding.
37. Added SSTable deserialize guard rejecting empty decoded attribute names.
38. Changed SSTable deserialize behavior to hard-fail on unknown on-disk attribute types.
39. Added SSTable record decode duplicate-attribute-name detection.
40. Added SSTable point-lookup fast rejection for empty lookup keys.
41. Added SSTable point-lookup fast rejection for oversized lookup keys.
42. Added SSTable hot-record cache for repeated point reads.
43. Added shared-lock cache lookup path for concurrent SSTable read performance.
44. Added bounded SSTable cache capacity with clear-on-cap to prevent unbounded growth.
45. Added cache population on successful SSTable point reads to reduce repeated disk deserialization.
46. Added LSM query early-return when HASH key condition is missing.
47. Added LSM query hash-prefix range pruning to avoid full merged-view scans.
48. Added LSM query hash-prefix encode failure guard for invalid key-condition shapes.
49. Added LSM query iteration break when encoded keys leave hash-prefix range.
50. Added HTTP UpdateItem validation requiring explicit `UpdateExpression` (rejects silent no-op updates).
51. Simplified UpdateItem write path to perform a single validated update write.
52. Added Query validation rejecting key-condition sets larger than table key schema.
53. Added auth/validation precedence hardening to avoid parsing signatures after earlier auth-header size rejection.
54. Added expression-context validation requiring `ExpressionAttributeNames` keys to start with `#`.
55. Added expression-context validation requiring `ExpressionAttributeValues` keys to start with `:`.
56. Added JSON serializer null-safety for map members (null emits `{"NULL":true}`).
57. Added JSON serializer null-safety for list members (null emits `{"NULL":true}`).
58. Added JSON serializer null-safety for top-level item attributes.
59. Added overflow-safe saturating add utility for item-size accounting.
60. Added recursion-bounded nested attribute-size calculator.
61. Added null-safe map/list traversal in item-size computation.
62. Added overflow-safe accumulation for all set/map/list size-accounting branches.
63. Added regression tests covering parser size/depth/argument-limit protections.
64. Added regression tests covering SigV4 date/duplicate-parameter hardening, TTL numeric-type enforcement, and null-safe JSON serialization behavior.
