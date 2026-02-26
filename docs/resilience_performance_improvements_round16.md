# Security and Standardization Improvements (Round 16)

Implemented improvements in this round:

1. Added `kMaxKeySchemaEntries` guard in memory engine key construction path.
2. Added `kMaxKeyNameBytes` guard for memory-engine key schema attribute names.
3. Added `kMaxNumericKeyDigits` to standardize numeric key normalization width.
4. Added explicit key separator constant (`kKeySeparator`) to centralize composite-key encoding format.
5. Added explicit component separator constant (`kComponentSeparator`) to centralize key encoding format.
6. Added control-character detection helper for memory-engine key fragments.
7. Added standardized identifier validator (`[A-Za-z0-9_.-]`) in memory engine.
8. Added numeric-key canonicalization helper to normalize numeric key tokens.
9. Memory numeric canonicalization now rejects plus-prefixed numbers.
10. Memory numeric canonicalization now rejects non-digit numeric payloads.
11. Memory numeric canonicalization now trims leading zeros to canonical values.
12. Memory numeric canonicalization now normalizes negative zero to canonical zero form.
13. Added key-fragment validation helper that rejects separators and control bytes.
14. Added key-schema shape validator enforcing exactly one HASH key and at most one RANGE key.
15. Key-schema validator now requires key attributes to exist in `attribute_definitions`.
16. Key-schema validator now rejects duplicate key attribute names.
17. String key-fragment encoding now enforces strict key-fragment validation.
18. Numeric key-fragment encoding now uses canonicalized numeric representation.
19. Boolean key-fragment encoding now uses canonical text (`true`/`false`) for consistency.
20. Binary key-fragment encoding now converts bytes to lowercase hex for deterministic safe encoding.
21. Binary key-fragment encoding now rejects empty binary keys.
22. Binary key-fragment encoding now enforces bounded binary-to-hex expansion.
23. `make_key` now requires valid key schema before encoding.
24. `make_key` now emits centralized separator constants instead of inline literals.
25. `make_key` now validates every encoded fragment via centralized fragment checker.
26. `put_item` now rejects writes when table key schema is invalid.
27. `get_item` now rejects lookups when table key schema is invalid.
28. `delete_item` now rejects deletes when table key schema is invalid.
29. `query` now rejects requests when a non-empty `index_name` is provided to memory engine.
30. `scan` now rejects requests when a non-empty `index_name` is provided to memory engine.
31. Query hash-prefix construction now uses standardized component separator constant.
32. Query hash-prefix path now validates encoded hash fragment before iteration.
33. WAL constructor now normalizes filesystem paths with `lexically_normal`.
34. WAL constructor now rejects normalized paths with empty filename components.
35. WAL constructor now rejects existing non-regular-file targets.
36. WAL append path now rejects embedded NUL bytes in payload values.
37. WAL path validation now rejects control-character-bearing paths.
38. WAL path validation now enforces non-empty filename component.
39. WAL reopen path (`ensure_open_locked`) now rejects existing non-regular-file targets.
40. WAL reset path now rejects existing non-regular-file targets.
41. Recovery manager now normalizes WAL paths before validation.
42. Recovery manager now rejects control-character-bearing WAL paths.
43. Recovery manager now rejects normalized WAL paths with empty filename components.
44. Recovery manager now requires WAL path existence prior to replay validation.
45. Recovery manager now requires WAL path to be a regular file prior to replay validation.
46. Recovery manager now enforces maximum WAL file-size pre-check before replay.
47. Recovery manager now truncates oversized WAL files to zero bytes as fail-safe.
48. Recovery manager no longer emits WAL-corruption details to stderr during validation.
49. Recovery manager now truncates WAL to last good offset when replay-record cap is reached.
50. SSTable path validator now rejects control-character-bearing paths.
51. SSTable path validator now requires non-empty filename components.
52. SSTable path validator now requires `.sst` extension for standardized file identity.
53. SSTable constructor now normalizes input paths before validation/loading.
54. SSTable creation path now normalizes output path before any filesystem writes.
55. SSTable creation now builds `.tmp` path from normalized canonical output path.
56. SSTable create-rename path now renames to normalized output path.
57. SSTable create retry-rename path now targets normalized output path after cleanup.
58. SSTable create now returns normalized output path on success.
59. Expression lexer now requires placeholder names (`#...`) to start with alpha/underscore after prefix.
60. Expression lexer now requires placeholder values (`:...`) to start with alpha/underscore after prefix.
61. Expression lexer now uses shared identifier-start/identifier-char helpers for consistent token classification.
62. Expression lexer now permits underscore-leading identifiers for standardized token handling.
63. Expression parser now enforces explicit comparison-operator allowlist (`=`, `<>`, `<`, `<=`, `>`, `>=`).
64. Expression evaluator now requires explicit `ExpressionAttributeNames` mapping for `#placeholder` resolution (no implicit fallback).
