# Resilience & Performance Improvements (Round 7, Implemented)

1. Added table-creation guard rejecting key schemas above the configured maximum entry count.
2. Added table-creation guard validating all attribute-definition names with identifier constraints.
3. Added table-creation guard rejecting new tables once metadata table-count cap is reached.
4. Added table-creation guard requiring TTL target attribute type to be numeric (`N`).
5. Added TTL update guard requiring target TTL attribute type to be numeric (`N`).
6. Added parser token-budget cap to prevent pathological expression token explosions.
7. Added parser recursion-depth cap to prevent deep-expression stack blowups.
8. Added parser fail-fast path returning `InvalidExpression` when token budget is exceeded.
9. Added parser fail-fast path returning `InvalidExpression` when depth limit is exceeded.
10. Converted parser recursive descent to explicit depth-threaded calls for bounded recursion control.
11. Added lexer token-budget cap to prevent unbounded tokenization on hostile expressions.
12. Added lexer over-budget behavior that emits `INVALID` + `END_OF_FILE` sentinel tokens.
13. Added SigV4 leap-year utility for accurate calendar date validation.
14. Added SigV4 strict calendar validation for `YYYYMMDD` credential dates.
15. Added SigV4 guard rejecting impossible dates (e.g., invalid month/day combinations).
16. Added SigV4 parameter occurrence counter utility for duplicate-key detection.
17. Added SigV4 guard requiring exactly one `Credential` parameter.
18. Added SigV4 guard requiring exactly one `SignedHeaders` parameter.
19. Added SigV4 guard requiring exactly one `Signature` parameter.
20. Added SSTable serializer dependency on JSON serialization for resilient fallback encoding of complex attrs.
21. Added SSTable write-path validation rejecting empty attribute names.
22. Added SSTable write-path validation rejecting oversized attribute names.
23. Added SSTable write-path validation rejecting oversized scalar payloads before write.
24. Changed SSTable unsupported-complex-attribute persistence to deterministic serialized JSON string payloads.
25. Added SSTable deserialize guard rejecting empty attribute names from disk.
26. Changed SSTable deserialize behavior to hard-fail on unknown attribute types instead of silent coercion.
27. Added SSTable read-path duplicate-attribute-name detection within a record.
28. Added SSTable create-path guard rejecting oversized total record counts before serialization.
29. Added SSTable create-path parent-directory auto-creation.
30. Added SSTable create-path parent-directory failure handling.
31. Added SSTable create-path failure handling for attribute-serialization errors with temp-file cleanup.
32. Added HTTP-level limit for max expression-attribute entry count.
33. Added HTTP-level limit for max parsed item attribute count.
34. Added HTTP parser guard rejecting empty attribute names in request payload maps.
35. Added HTTP parser guard rejecting duplicate attribute names during request map parsing.
36. Added HTTP expression-context guard rejecting oversized `ExpressionAttributeNames` maps.
37. Added HTTP expression-context guard rejecting oversized `ExpressionAttributeValues` maps.
38. Added HTTP expression-context guard rejecting duplicate `ExpressionAttributeNames` entries.
39. Added HTTP expression-context guard rejecting duplicate `ExpressionAttributeValues` entries.
40. Added HTTP expression-context guard requiring `ExpressionAttributeNames` keys to start with `#`.
41. Added HTTP expression-context guard requiring `ExpressionAttributeValues` keys to start with `:`.
42. Added HTTP key-completeness helper hardening for empty key maps.
43. Added HTTP key-completeness helper hardening for oversized key maps.
44. Changed UpdateItem semantics to require explicit `UpdateExpression` (rejects missing expression).
45. Removed ambiguous no-op update path by always validating and applying `UpdateExpression` before write.
46. Added Query guard rejecting key-condition sets larger than key schema.
47. Hardened auth/validation precedence so auth checks run only when earlier request validation passed.
48. Hardened signed-header verification to reject signed headers that are present but empty.
49. Added JSON size-accounting recursion-depth cap for nested attribute-size calculation.
50. Added saturating-add helper to prevent size-accounting integer overflow.
51. Added recursive JSON size-accounting helper with bounded depth propagation.
52. Added null-safe size accounting for map attribute members.
53. Added null-safe size accounting for list attribute members.
54. Added overflow-safe accumulation for string-set size accounting.
55. Added overflow-safe accumulation for number-set size accounting.
56. Added overflow-safe accumulation for binary-set size accounting.
57. Added overflow-safe accumulation for map/list nested size accounting.
58. Added null-safe item-size accounting for top-level null attribute pointers.
59. Hardened JSON serialization of map members to emit `{"NULL":true}` when nested pointers are null.
60. Hardened JSON serialization of list members to emit `{"NULL":true}` when nested pointers are null.
61. Hardened JSON serialization of top-level item attributes to emit `{"NULL":true}` for null pointers.
62. Added regression test coverage for parser depth/token limits to lock in anti-DoS behavior.
63. Added regression test coverage for SigV4 invalid calendar dates and duplicate parameter rejection.
64. Added regression test coverage for TTL numeric-type enforcement and null-pointer-safe JSON serialization.
