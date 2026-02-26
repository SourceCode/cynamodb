# Security and Standardization Improvements (Round 17)

Implemented improvements in this round:

1. Added `kMaxKeySchemaEntries` bound in memory engine key-schema validation.
2. Added `kMaxKeyNameBytes` bound in memory engine key-name validation.
3. Added `kMaxNumericKeyDigits` for canonical numeric key encoding in memory engine.
4. Added explicit memory-key delimiter constants (`kKeySeparator`, `kComponentSeparator`) to standardize key format.
5. Added memory-engine control-character detector for input sanitization.
6. Added standardized identifier validator (`[A-Za-z0-9_.-]`) in memory engine.
7. Added numeric-key canonicalizer for memory-engine key fragments.
8. Numeric-key canonicalizer now rejects plus-prefixed numeric strings.
9. Numeric-key canonicalizer now rejects non-digit numeric payloads.
10. Numeric-key canonicalizer now trims leading zeros to canonical representation.
11. Numeric-key canonicalizer now normalizes negative zero to zero representation.
12. Added centralized key-fragment validator in memory engine.
13. Key-fragment validator now rejects embedded key-separator bytes.
14. Key-fragment validator now rejects embedded component-separator bytes.
15. Key-fragment validator now rejects control characters.
16. Added memory-engine key-schema shape validator requiring exactly one HASH key and at most one RANGE key.
17. Memory key-schema validator now rejects duplicate key attribute names.
18. Memory key-schema validator now requires key attributes to exist in attribute definitions.
19. Memory string key-fragment encoding now enforces key-fragment validator.
20. Memory numeric key-fragment encoding now uses canonicalized numeric output.
21. Memory boolean key-fragment encoding now standardizes to `true`/`false` text.
22. Memory binary key-fragment encoding now uses deterministic lowercase hex.
23. Memory binary key-fragment encoding now rejects empty binary keys.
24. Memory binary key-fragment encoding now enforces bounded binary expansion.
25. `MemoryStorageEngine::make_key` now requires valid key schema before encoding.
26. `MemoryStorageEngine::put_item` now rejects invalid table key schemas.
27. `MemoryStorageEngine::get_item` now rejects invalid table key schemas.
28. `MemoryStorageEngine::delete_item` now rejects invalid table key schemas.
29. `MemoryStorageEngine::query` now rejects invalid table key schemas.
30. `MemoryStorageEngine::scan` now rejects invalid table key schemas.
31. Memory query path now rejects non-empty `index_name` values (unsupported path).
32. Memory scan path now rejects non-empty `index_name` values (unsupported path).
33. Memory query prefix construction now uses standardized component separator constant.
34. Memory query prefix validation now checks encoded hash fragment safety before scan.
35. WAL constructor now canonicalizes path with `lexically_normal`.
36. WAL constructor now rejects paths with empty filename components.
37. WAL constructor now rejects existing non-regular-file targets.
38. WAL append path now rejects value payloads containing NUL bytes.
39. WAL path validator now requires `.log` extension.
40. WAL reopen (`ensure_open_locked`) now rejects non-regular-file paths.
41. WAL reset now rejects non-regular-file targets.
42. Recovery manager now canonicalizes WAL path before validation.
43. Recovery manager now rejects WAL paths with empty filename components.
44. Recovery manager now requires WAL filename to be exactly `wal.log`.
45. Recovery manager now enforces regular-file WAL requirement before replay validation.
46. Recovery manager now enforces WAL max-file-size precheck and truncates oversized files to zero.
47. Recovery manager now truncates WAL to last-good offset when replay-record limit is reached.
48. SSTable path validator now rejects control-character-bearing paths.
49. SSTable path validator now requires non-empty filename components.
50. SSTable path validator now requires `.sst` extension.
51. SSTable constructor now canonicalizes paths via `lexically_normal`.
52. SSTable create path now canonicalizes output paths before filesystem operations.
53. SSTable temp-file path is now derived from canonicalized output path.
54. SSTable rename/install path now writes to canonicalized output path.
55. SSTable create now returns canonicalized output path.
56. Expression lexer now enforces alpha/underscore start for `#name` placeholders.
57. Expression lexer now enforces alpha/underscore start for `:value` placeholders.
58. Expression lexer now uses shared identifier classification helpers for token consistency.
59. Expression lexer now supports underscore-leading identifiers consistently.
60. Expression parser now enforces a strict comparison-operator allowlist.
61. Expression parser now rejects unsupported keyword nodes unless mapped to allowed function names.
62. Expression parser now validates placeholder token minimum structure (`prefix + name`).
63. Expression evaluator now requires explicit `ExpressionAttributeNames` mappings for `#placeholder` resolution (no implicit fallback).
64. Expression evaluator now validates resolved identifier names before read/update/delete operations.

Additional request-path and schema hardening implemented in this pass:

- HTTP request auth flow now requires signed headers to include `host`, `x-amz-date`, `x-amz-target`, and `content-type`.
- HTTP operation dispatch now parses `X-Amz-Target` using the already-trimmed canonical header value.
- HTTP expression context parsing now enforces `kMaxExpressionContextBytes` aggregate size.
- HTTP expression context now validates placeholder format via a shared `is_valid_expression_placeholder` helper.
- LSM engine now validates key-schema shape centrally before write/read/query/scan operations.
- LSM `get_item` / `delete_item` now require exact key-schema attribute sets.
- LSM `query` / `scan` now reject unknown `index_name` values.
- LSM `scan` now validates `ExclusiveStartKey` shape strictly before encoding.
- JSON table-definition parser now rejects unsupported top-level fields.
- JSON table-definition parser now enforces strict shapes/allowed fields for `KeySchema`, `AttributeDefinitions`, and `ProvisionedThroughput` objects.
- JSON table-definition parser now parses and validates `TimeToLiveSpecification`, including numeric-attribute requirement.
