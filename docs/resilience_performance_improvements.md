# Resilience & Performance Improvements (Implemented)

1. Added minimum arena block-size clamping to avoid invalid tiny blocks.
2. Added alignment-aware arena allocation.
3. Added power-of-two alignment validation in arena allocator.
4. Added zero-size allocation handling in arena allocator.
5. Added oversized-allocation guard in arena allocator.
6. Added overflow-safe alignment arithmetic in arena allocator.
7. Added allocated-bytes accounting in arena allocator.
8. Added arena `bytes_allocated()` accessor.
9. Added arena `block_count()` accessor.
10. Made arena reset resilient when blocks are missing.
11. Made arena mutex mutable for thread-safe const accessors.
12. Added robust trim helpers to SigV4 parsing path.
13. Added SigV4 parameter extraction helper for safer parsing.
14. Enforced exact 5-segment credential scope in SigV4 parsing.
15. Enforced non-empty credential scope segments in SigV4 parsing.
16. Enforced SigV4 request type `aws4_request`.
17. Enforced 8-digit SigV4 date format.
18. Normalized signed headers to lowercase.
19. Ignored empty signed-header entries safely.
20. Rejected requests with no valid signed headers.
21. Enforced 64-char signature length.
22. Enforced hexadecimal signature characters.
23. Added target whitespace trimming in API dispatcher.
24. Added prefix validation via `starts_with` in API dispatcher.
25. Replaced long if-chain with static lookup table in dispatcher.
26. Added missing/unknown operation fast-fail handling support.
27. Hardened numeric key encoding in `KeyManager` for non-digit values.
28. Prevented numeric padding underflow in `KeyManager`.
29. Added deterministic key derivation in memory storage engine.
30. Removed dummy key usage in memory storage engine put path.
31. Removed dummy key usage in memory storage engine get path.
32. Removed dummy key usage in memory storage engine delete path.
33. Added read/write lock strategy to memory storage engine.
34. Added concrete memory-engine `query` implementation.
35. Added concrete memory-engine `scan` implementation.
36. Added `scan` limit behavior in memory engine.
37. Added `scan` exclusive-start-key behavior in memory engine.
38. Added metadata parent directory creation in table manager.
39. Added dirty-bit tracking in table manager.
40. Avoided unnecessary metadata writes when not dirty.
41. Added atomic metadata save using temp-file rename.
42. Added metadata save failure cleanup for temp files.
43. Added bounded table-count validation on metadata load.
44. Added bounded key-schema-count validation on metadata load.
45. Added bounded string-length validation on metadata load.
46. Added key-type validation when loading metadata.
47. Added schema validation for empty table names.
48. Added schema validation for duplicate key attributes.
49. Added schema validation for missing key attribute definitions.
50. Added schema validation for non-empty key attributes.
51. Added list-tables capacity reservation for performance.
52. Added deterministic skiplist RNG via `mt19937`.
53. Removed global `rand()` dependency in skiplist.
54. Added shared lock for skiplist reads.
55. Added exclusive lock for skiplist writes.
56. Added exclusive lock in skiplist destructor.
57. Added WAL parent directory creation.
58. Added WAL append guards for unopened files.
59. Added WAL record size bounds checks.
60. Added periodic WAL auto-sync to bound buffered risk.
61. Added WAL sync state reset on explicit sync.
62. Added SSTable file-size and index-offset validation.
63. Added SSTable bounded key/attribute read validation.
64. Added malformed SSTable fail-safe behavior (returns not-found instead of unsafe read/crash).

