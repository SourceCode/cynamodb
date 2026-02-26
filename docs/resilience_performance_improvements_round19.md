# Security and Standardization Improvements (Round 19)

Implemented improvements in this round:

1. Reduced JSON table-definition `KeySchema` maximum from 16 entries to 2 entries for strict DynamoDB-style key-shape standardization.
2. Added explicit `kMaxNumberLiteralLen` bound for JSON numeric literals.
3. Added JSON-side control-character detector for string-like attribute values.
4. Added JSON string-attribute validator that enforces both length and control-character safety.
5. JSON `AttributeValue` objects now require exact object size of 1 field.
6. JSON `AttributeValue` parser now rejects payloads that mix one valid type key with extra unknown keys.
7. JSON `S` attribute parsing now rejects control-character-bearing values.
8. JSON `S` attribute parsing continues to enforce scalar size limits through centralized validation.
9. JSON `N` attribute parsing now applies dedicated numeric-literal length caps.
10. JSON `NS` member parsing now applies dedicated numeric-literal length caps.
11. JSON `SS` member parsing now rejects control-character-bearing set members.
12. JSON map (`M`) attribute parsing now rejects control-character-bearing map keys.
13. JSON map (`M`) attribute parsing now keeps strict identifier validation for map keys.
14. JSON table-definition parsing now enforces `HASH` key placement strictly at index 0.
15. JSON table-definition parsing now rejects `HASH` key entries appearing after index 0.
16. JSON table-definition parsing now enforces `RANGE` key placement strictly at index 1.
17. JSON table-definition parsing now rejects `RANGE` keys when `HASH` key is absent.
18. JSON table-definition parsing now rejects malformed two-key expectations (RANGE present without exactly 2 key elements).
19. JSON table-definition parser key-order validation now executes during schema ingestion rather than deferring to engine errors.
20. JSON table-definition parser now keeps explicit duplicate-key-name rejection while enforcing strict ordering.
21. JSON table-definition parser now preserves strict top-level field allowlisting alongside tighter key-shape constraints.
22. HTTP key-condition placeholder-name resolution now requires explicit `ExpressionAttributeNames` mappings for `#...` placeholders.
23. HTTP key-condition parser no longer falls back to implicit `#name -> name` resolution.
24. HTTP key extraction now includes scalar key-type allowlisting helper (`S`, `N`, `B`).
25. HTTP key validation now rejects null key attribute pointers before storage-engine calls.
26. HTTP key validation now rejects key attributes missing from table attribute definitions.
27. HTTP key validation now enforces request key attribute type equality against table schema types.
28. HTTP key validation now rejects table schemas that define non-scalar key attribute types at request-validation time.
29. HTTP `has_complete_key_attributes` now validates both key presence and type correctness.
30. HTTP `ExclusiveStartKey` validation now benefits from stricter key type checks via shared key-validation path.
31. HTTP `GetItem` request path now rejects key type mismatches earlier with consistent validation behavior.
32. HTTP `DeleteItem` request path now rejects key type mismatches earlier with consistent validation behavior.
33. HTTP `UpdateItem` request path now rejects key type mismatches earlier with consistent validation behavior.
34. HTTP `PutItem` key extraction path now rejects key type mismatches before write execution.
35. HTTP key-condition validation now enforces key-value type correctness for each referenced key attribute.
36. HTTP `Query` handler now rejects key-condition entries whose value types do not match schema-defined key types.
37. HTTP `Query` handler key-condition validation remains constrained to key-schema attributes while adding type enforcement.
38. LSM module now has centralized control-character detection utility for path/metadata safety checks.
39. LSM WAL replay JSON attribute-name parsing now rejects control-character-bearing attribute names.
40. LSM WAL replay JSON attribute-name parsing now enforces strict identifier token rules (`[A-Za-z0-9_.-]`).
41. LSM constructor now rejects control-character-bearing database paths.
42. LSM constructor normalized-path validation now rejects control-character-bearing normalized paths.
43. LSM constructor now rejects initialization when database path already exists as a non-directory filesystem object.
44. LSM constructor now fails early on invalid filesystem type before directory creation attempts.
45. LSM SSTable discovery now normalizes discovered `.sst` paths before caching.
46. LSM SSTable discovery now deduplicates normalized path entries after sort.
47. LSM SSTable cache lookup now normalizes incoming path keys before cache probing.
48. LSM SSTable cache lookup now rejects normalized paths with control characters.
49. LSM SSTable cache storage now uses normalized path keys for canonical cache identity.
50. LSM SSTable creation path now captures and uses canonicalized path returned by `SSTable::create`.
51. LSM in-memory SSTable index (`sstables_`) now stores canonical created paths.
52. LSM SSTable cache insertion after flush now stores canonical created paths.
53. LSM compaction output path now applies lexical normalization before compaction execution.
54. LSM compaction cleanup/removal now operates on normalized output path values.
55. LSM merged-view SSTable reads now benefit from canonicalized cache keys due normalized store/load paths.
56. Added JSON test coverage for rejecting extra fields in single-typed attribute payloads.
57. Added JSON test coverage for rejecting control characters in `S` attribute values.
58. Added JSON test coverage for rejecting control characters in `SS` set members.
59. Added JSON test coverage for rejecting oversized numeric literals via new numeric-length bounds.
60. Added JSON table-definition test coverage for rejecting RANGE-before-HASH key ordering.
61. Added JSON table-definition test coverage for rejecting key schemas with more than two key entries.
62. Added LSM test coverage for rejecting control-character database paths.
63. Added LSM test coverage for rejecting existing-file database path targets.
64. Added LSM test coverage for rejecting key type mismatches in `get_item` requests.

Additional consistency hardening delivered in this pass:

- HTTP request-layer key validation now aligns more closely with storage-engine key-type strictness.
- JSON schema ingestion now matches table-manager/LSM two-key primary-key model more consistently.
- LSM path handling now reduces cache/path aliasing risks via canonicalized path identity.
