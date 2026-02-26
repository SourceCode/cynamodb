# Security, Resilience, And Performance Improvements (Round 23)

The following 64 additional improvements were implemented in this round.

1. Added shared WAL key-length bound constant (`WriteAheadLog::kMaxKeyLen`).
2. Added shared WAL value-length bound constant (`WriteAheadLog::kMaxValueLen`).
3. Added shared WAL combined payload bound constant (`WriteAheadLog::kMaxRecordPayloadLen`).
4. Added persisted in-memory WAL file-size tracker (`file_size_bytes_`) to avoid repeated stream-size probing.
5. Added `auto_sync_every_locked()` hook for runtime WAL sync tuning.
6. Added `refresh_file_size_locked()` helper for explicit on-disk size validation.
7. Hardened WAL constructor to initialize file-size state from filesystem metadata.
8. Hardened WAL constructor to abort initialization when file-size refresh fails.
9. Added bounded `parse_env_uint32()` helper for WAL runtime settings.
10. Added `CYNAMODB_WAL_AUTO_SYNC_EVERY` runtime configuration.
11. Clamped WAL auto-sync env configuration to sane bounds (`1..4096`).
12. Hardened WAL append path to reject NUL bytes in value payloads.
13. Hardened WAL append path to reject keys larger than `kMaxKeyLen`.
14. Hardened WAL append path to reject values larger than `kMaxValueLen`.
15. Hardened WAL append path to reject records exceeding `kMaxRecordPayloadLen`.
16. Replaced WAL append file-capacity checks to use cached file-size state.
17. Added WAL append accounting to advance cached file-size state only after successful writes.
18. Added stream-state recovery (`file_.clear()`) on WAL auto-flush failures.
19. Added WAL reset path update to reinitialize cached file-size state to `0`.
20. Hardened `ensure_open_locked()` to re-validate file size even when stream is already open.
21. Hardened `ensure_open_locked()` reopen path to re-validate file size after opening.
22. Hardened WAL file-size refresh to fail closed on filesystem metadata errors.
23. Hardened WAL file-size refresh to fail closed when on-disk bytes exceed max WAL size.
24. Removed duplicated local replay record-length limit and standardized on shared WAL constants.
25. Hardened LSM WAL replay parser to enforce `WriteAheadLog::kMaxKeyLen` for key allocation safety.
26. Hardened LSM WAL replay parser to enforce `WriteAheadLog::kMaxValueLen` for value allocation safety.
27. Hardened LSM WAL replay parser to enforce `WriteAheadLog::kMaxRecordPayloadLen` for combined payload safety.
28. Hardened LSM WAL replay parser to reject NUL bytes in value payloads.
29. Hardened LSM replay path to compute CRC only after new payload-structure validation gates.
30. Standardized LSM replay record validation with WAL writer invariants.
31. Reduced malformed-record memory amplification risk during replay by bounding key/value allocation sizes.
32. Improved replay truncation correctness by routing new malformed payload classes through record-parse failure handling.
33. Hardened `RecoveryManager` WAL validator to enforce `WriteAheadLog::kMaxKeyLen`.
34. Hardened `RecoveryManager` WAL validator to enforce `WriteAheadLog::kMaxValueLen`.
35. Hardened `RecoveryManager` WAL validator to enforce `WriteAheadLog::kMaxRecordPayloadLen`.
36. Hardened `RecoveryManager` WAL validator to reject NUL bytes in value payloads.
37. Added explicit truncation trigger for NUL-containing WAL values.
38. Preserved strict `wal.log` filename guard while adding stronger per-record validation.
39. Preserved CRC validation flow with stricter pre-CRC payload gating.
40. Extended deterministic `truncate_to_good_end` behavior to newly detected malformed payload classes.
41. Reduced recovery-time oversized-allocation risk via shared WAL payload bounds.
42. Added SigV4 canonical-request URI byte cap (`kMaxCanonicalUriBytes`).
43. Added SigV4 canonical-request query-string byte cap (`kMaxCanonicalQueryBytes`).
44. Added SigV4 per-signed-header value byte cap (`kMaxSignedHeaderValueBytes`).
45. Added SigV4 aggregate canonical-header byte budget (`kMaxCanonicalHeadersBytes`).
46. Added SigV4 payload-size cap in verifier canonicalization (`kMaxPayloadBytes`).
47. Hardened SigV4 canonicalization to reject empty canonicalized signed-header values.
48. Hardened SigV4 canonicalization to reject non-printable bytes in canonicalized signed-header values.
49. Added per-header budget accounting during SigV4 canonical-header assembly.
50. Added fail-closed SigV4 canonical-request rejection on canonical-header budget exhaustion.
51. Hardened SigV4 parser to reject credential-scope segments with leading/trailing whitespace.
52. Hardened SigV4 parser to reject credential-scope segments containing spaces/tabs.
53. Hardened SigV4 parser to reject empty credential-scope segments at split time.
54. Hardened SigV4 parser to reject empty signed-header tokens (`;;` / trailing `;`).
55. Hardened SigV4 parser to reject whitespace-padded signed-header tokens.
56. Kept strict signed-header lexical ordering with the new whitespace-strict token parsing.
57. Kept signed-header count enforcement with stricter token normalization rules.
58. Hardened memory engine key-schema validation to require scalar key attribute types (`S`/`N`/`B`).
59. Added explicit memory-engine bounds for attribute names, string/binary sizes, container sizes, and validation depth.
60. Added memory-engine numeric-string validator for `N` attributes (`+` rejected, digit-only bounds enforced).
61. Added recursive memory-engine type-safe attribute validator across all DynamoDB attribute types.
62. Added nested map attribute-name validation in memory engine (`[A-Za-z0-9_.-]` style identifiers).
63. Added set-value validation in memory engine for emptiness, size bounds, and duplicate elimination.
64. Hardened memory-engine `put_item` to gate writes on full attribute-structure validation before size accounting.

## Files

- `include/cynamodb/engine/lsm/wal.hpp`
- `src/engine/lsm/wal.cpp`
- `src/engine/lsm/lsm_engine.cpp`
- `src/engine/recovery/recovery_manager.cpp`
- `src/auth/sigv4.cpp`
- `src/engine/memory_engine.cpp`
- `tests/test_auth.cpp`
- `tests/test_recovery.cpp`
- `tests/test_memory_engine.cpp`
