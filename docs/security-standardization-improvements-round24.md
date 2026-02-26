# Security, Resilience, And Performance Improvements (Round 24)

The following 64 additional improvements were implemented in this round.

1. Reduced SSTable attribute-name max length from 64KiB to 255 bytes for tighter metadata bounds.
2. Added SSTable helper to detect control characters in serialized identifiers.
3. Added SSTable helper to enforce identifier shape (`[A-Za-z0-9_.-]`).
4. Added SSTable helper to validate numeric-string encoding (`N`) with no `+` prefix.
5. Added SSTable numeric digit bound (`<= 38`) in serializer validation.
6. Hardened SSTable serializer to reject invalid attribute identifiers before writing.
7. Hardened SSTable serializer to reject control characters in `S` attribute payloads.
8. Hardened SSTable serializer to reject invalid `N` attribute payloads.
9. Hardened SSTable deserializer to reject invalid attribute identifiers.
10. Hardened SSTable deserializer to reject control characters in `S` payloads.
11. Hardened SSTable deserializer to reject invalid numeric `N` payloads.
12. Standardized SSTable on-disk attribute-name validation between write and read paths.
13. Reduced acceptance of malformed on-disk attribute payloads by validating type-specific encodings.
14. Prevented permissive rehydration of malformed numeric strings from SST files.
15. Added strict per-attribute semantic gating in SSTable record parsing.
16. Aligned SSTable name validation policy with memory-engine/table-manager identifier rules.
17. Strengthened SSTable corruption detection for identifier and numeric payload classes.
18. Hardened SSTable parser to fail-closed on control-byte string payloads.
19. Added Compactor includes/utilities for normalized path and character validation.
20. Added Compactor max-path bound (`4096`) for input/output SST paths.
21. Hardened Compactor to reject output paths containing control bytes.
22. Added output-path normalization before compaction work.
23. Hardened Compactor to require normalized output path to end with `.sst`.
24. Hardened Compactor to reject malformed/empty normalized output paths.
25. Hardened Compactor to reject input paths with control bytes.
26. Added input-path normalization before SST loading.
27. Hardened Compactor to require normalized input paths to end with `.sst`.
28. Hardened Compactor to reject malformed/empty normalized input paths.
29. Hardened Compactor to reject output path colliding with normalized input path.
30. Replaced duplicate-input silent skip behavior with fail-closed duplicate rejection.
31. Added input SST file-size guard (`kMaxInputSstableBytes`) before opening SSTs.
32. Hardened Compactor to fail when a normalized input path cannot produce a valid SSTable.
33. Removed permissive “skip empty input SST” behavior in Compactor for stronger corruption signaling.
34. Hardened Compactor to pass normalized output path to SSTable create.
35. Standardized Compactor path handling to reduce path-alias ambiguity during merge.
36. Added deterministic duplicate-input rejection to prevent hidden merge-order ambiguity.
37. Hardened legacy metadata loader to reject duplicate table names.
38. Hardened v2 metadata loader to reject duplicate table names.
39. Hardened v3 metadata loader to reject duplicate table names.
40. Hardened v4 metadata loader to reject duplicate table names.
41. Hardened v5 metadata loader to reject duplicate table names.
42. Hardened v6 metadata loader to reject duplicate table names.
43. Hardened v7 metadata loader to reject duplicate table names.
44. Converted all metadata loaders from overwrite-on-duplicate to fail-on-duplicate semantics.
45. Hardened metadata parser to fail when filesystem size stat returns error.
46. Hardened metadata parser to reject undersized files (`< sizeof(uint32_t)`).
47. Preserved upper metadata file-size bound while adding lower-bound sanity checks.
48. Reduced risk of malicious duplicate-table overwrite during metadata recovery.
49. Improved metadata corruption detection sensitivity for truncated files.
50. Kept metadata parsing fail-closed for unknown/invalid version and duplicate-entry states.
51. Hardened memory-engine key encoding to require runtime key attribute type == schema type.
52. Added memory-engine fail-closed path for missing schema key definition during key encoding.
53. Prevented mismatched runtime key-type writes from generating inconsistent encoded keys.
54. Tightened key-consistency guarantees for put/get/delete by enforcing schema type equality in `make_key`.
55. Added regression test: SSTable create rejects invalid attribute identifiers.
56. Added regression test: SSTable create rejects invalid numeric encodings.
57. Added regression test: Compactor rejects duplicate input paths.
58. Added regression test: Compactor rejects missing input SSTables.
59. Added regression test: TableManager rejects duplicate legacy metadata table definitions.
60. Added regression test helper to write legacy metadata strings deterministically.
61. Added regression test: memory engine rejects runtime key-type mismatches.
62. Rebuilt and reran full unit test suite after hardening changes.
63. Verified all tests pass with new stricter SSTable/Compactor/TableManager behaviors.
64. Documented this full Round 24 implementation set for auditability and repeatability.

## Files

- `src/engine/lsm/sstable.cpp`
- `include/cynamodb/engine/lsm/compactor.hpp`
- `src/engine/table_manager.cpp`
- `src/engine/memory_engine.cpp`
- `tests/test_recovery.cpp`
- `tests/test_memory_engine.cpp`
