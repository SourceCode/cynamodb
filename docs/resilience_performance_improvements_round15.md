# Security and Standardization Improvements (Round 15)

Implemented improvements in this round:

1. Added `has_invalid_target_chars(...)` to centralize request-target character sanitization in API dispatch.
2. `ApiDispatcher::parse_target` now rejects non-graphic/control characters after trimming.
3. Dispatcher target parsing now explicitly rejects non-ASCII operation targets.
4. Dispatcher target validation now hard-fails malformed tokens before operation lookup.
5. JSON numeric parsing now rejects plus-prefixed numbers (`+42`) to enforce canonical number formatting.
6. Added `is_likely_base64_token(...)` to standardize binary-attribute token validation.
7. JSON parser now rejects `B` attributes containing non-base64-like characters.
8. JSON parser now rejects `BS` members containing non-base64-like characters.
9. JSON map (`M`) parsing now enforces standardized identifier charset on map keys.
10. Added `kMaxTableDefinitionFields` to cap top-level table-definition field fanout.
11. Table-definition parsing now rejects oversized top-level field counts.
12. Added `kMaxProvisionedFields` to standardize throughput-object shape limits.
13. `ProvisionedThroughput` parsing now rejects extra/unexpected fields.
14. `is_valid_identifier(...)` in JSON layer now enforces `[A-Za-z0-9_.-]`.
15. Table-name parsing now uses the stricter standardized identifier validator.
16. Key-schema attribute-name parsing now uses stricter identifier validation.
17. Attribute-definition name parsing now uses stricter identifier validation.
18. Item serialization now rejects non-standard attribute names using stricter identifier validation.
19. Added `sanitize_error_fragment(...)` to normalize error field content.
20. Error-type serialization now strips control/non-printable bytes before JSON encoding.
21. Error-message serialization now strips control/non-printable bytes before JSON encoding.
22. Added `kMaxKeyNameBytes` in key encoding to standardize key-schema identifier limits.
23. Added `kMaxInputKeyAttributes` to cap key-encoding input maps.
24. Added `kMaxNumericKeyDigits` to standardize numeric key canonicalization width.
25. Replaced platform-specific `htons` dependency with explicit big-endian length-byte emission.
26. Added key-component control-character detection helper for encoded key safety.
27. Added key identifier validator (`[A-Za-z0-9_.-]`) inside `KeyManager`.
28. Key-schema name resolution now rejects invalid/non-standard key attribute names.
29. Composite-key encoding now rejects empty PK names and oversized key-attribute maps.
30. Composite-key encoding now includes explicit overflow checks before length-prefix assembly.
31. String key conversion now rejects values containing control characters.
32. Numeric key conversion now rejects plus-prefixed numeric strings.
33. Numeric key conversion now rejects non-digit numeric tokens.
34. Numeric key conversion now rejects values exceeding configured digit width.
35. Numeric key conversion now canonicalizes leading-zero numbers before padding.
36. Numeric key conversion now canonicalizes negative zero to normalized zero representation.
37. Added metadata-path control-character detection in table metadata manager.
38. Metadata-path validation now rejects control-character-bearing paths.
39. Metadata parse now requires a regular file before attempting decode.
40. TableManager constructor now rejects empty original metadata-path input before normalization.
41. TableManager constructor now normalizes metadata paths (`lexically_normal`) for canonical handling.
42. TableManager constructor now rejects metadata paths missing a filename component.
43. Corrupt-metadata quarantine now only renames regular files.
44. Metadata backup copy now only executes when the primary metadata path is a regular file.
45. Metadata path handling now consistently operates on normalized path state.
46. Added `kMaxContentTypeHeaderBytes` to bound Content-Type header size.
47. Added `kMaxAmzDateHeaderBytes` to bound `x-amz-date` header size.
48. Added `kMaxRequestTargetBytes` to bound HTTP request-target size.
49. Added `kMaxHeaderNameBytes` and `kMaxHeaderValueBytes` to standardize header bounds.
50. Added `kMaxHeadersPerRequest` to cap per-request header count.
51. Added `kMaxExpressionBytes` to bound expression string parsing cost.
52. Added `kMaxSigV4ClockSkew` for bounded request-time skew validation.
53. Added strict request-target validator and enforced exact canonical target (`/`).
54. Added full per-header validation pass (token charset + non-control bounded values).
55. Added duplicate-sensitive-header rejection for auth-critical headers.
56. Added normalized header-count map builder for standardized duplicate checks.
57. Added case-insensitive helper for signed-header presence with non-empty values.
58. Added case-insensitive helper for trimmed header value retrieval.
59. Host header validation now enforces strict allowed characters and bounds.
60. Content-Type validation now parses media type canonically and matches exact allowlist values.
61. SigV4 parser now enforces a hard max authorization-parameter count (`kMaxAuthorizationParams`).
62. SigV4 parser now rejects non-ASCII bytes in Authorization headers.
63. SigV4 parser now rejects excessively parameterized Authorization headers before deep parsing.
64. `main` env ingestion now rejects control characters in numeric env values and bind-address env input.
