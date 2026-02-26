# Resilience and Performance Improvements (Round 12)

Implemented improvements in this round:

1. Promoted WAL auto-sync cadence to an explicit public constant (`kAutoSyncEvery`).
2. Promoted WAL max record size to a public hard limit (`kMaxRecordLen`).
3. Added WAL max file-size hard limit (`kMaxWalFileBytes`).
4. Added WAL max path-length hard limit (`kMaxWalPathBytes`).
5. WAL constructor now rejects invalid/oversized/empty paths up front.
6. WAL constructor now creates parent directories via `error_code` path (no exception dependency).
7. WAL destructor now conditionally flushes only when unsynced writes exist.
8. WAL append path now auto-recovers closed streams via `ensure_open_locked()`.
9. WAL append now performs overflow-safe record-size accounting before writing.
10. WAL append now preflights current file size before each write.
11. WAL append now enforces global WAL file-size cap before accepting a record.
12. WAL append now clears stream error state after write failures for cleaner recovery.
13. WAL sync now requires/open-checks the file handle before attempting flush.
14. WAL sync now clears stream error state on failure.
15. WAL reset now validates path constraints before mutating on-disk state.
16. WAL reset now removes prior file path before truncate/reopen to avoid stale state.
17. Added private WAL path validator helper (`has_valid_path`).
18. Added private WAL reopen helper (`ensure_open_locked`).
19. Added private WAL size probe helper (`current_size_locked`).
20. WAL reset/sync flows now reliably reset unsynced-write counters only on success paths.
21. Added explicit SSTable max path-length guard (`kMaxSstablePathBytes`).
22. Added explicit SSTable cache-capacity constant (`kMaxRecordCacheEntries`).
23. Added a FIFO eviction queue for SSTable record cache (`cache_fifo_`).
24. Added SSTable path-validation helper (`is_valid_sstable_path`).
25. Added centralized SSTable temp-file cleanup helper (`remove_if_exists`).
26. Added per-record attribute-count hard cap (`kMaxAttrsPerRecord`).
27. SSTable record reads now reject empty/oversized expected keys early.
28. SSTable record reads now enforce per-record attribute-count limits.
29. SSTable constructor now rejects invalid/oversized/empty input paths.
30. SSTable constructor now validates path existence before opening.
31. SSTable constructor now rejects non-regular-file inputs.
32. SSTable constructor now rejects files exceeding max SSTable file-size bound.
33. SSTable constructor now validates index-footer bounds more strictly.
34. SSTable constructor now validates minimum index-section size before parsing.
35. SSTable constructor now bounds entry count by physically possible index bytes.
36. SSTable constructor now pre-reserves cache buckets based on entry count.
37. SSTable constructor now rejects empty index keys.
38. SSTable creation now rejects invalid/oversized/empty destination paths.
39. SSTable creation now removes stale `.tmp` files before writing.
40. SSTable creation now validates attribute names before serialization.
41. SSTable creation now enforces per-record attribute-count caps.
42. SSTable creation now verifies stream integrity after each record write.
43. SSTable creation now enforces file-size cap during record emission.
44. SSTable creation now validates index keys before footer/index write.
45. SSTable creation rename now retries with remove-and-rename fallback on conflicts.
46. SSTable failure paths now consistently use centralized temp-file cleanup.
47. SSTable `get_record` now fast-returns when table index is empty.
48. SSTable cache policy moved from full-cache clears to bounded FIFO single-entry eviction.
49. SSTable cache insertion now avoids duplicate FIFO growth for existing keys.
50. SSTable `read_all_records` now rejects pathological oversized index counts defensively.
51. Memory engine key builder now supports strict key-schema mode (`require_exact_key_schema`).
52. Memory engine key builder now rejects empty key schemas.
53. Strict key mode now rejects unexpected key names in key maps.
54. Memory engine key builder now rejects null key-attribute pointers.
55. Memory engine `put_item` now validates table-name format/length.
56. Memory engine `put_item` now enforces non-empty item and max attribute-count limit.
57. Memory engine `put_item` now rejects oversized serialized items via size estimator.
58. Memory engine now caps number of in-memory tables.
59. Memory engine now caps items per table.
60. Memory engine `get_item` now requires exact key schema encoding.
61. Memory engine `delete_item` now requires exact key schema encoding.
62. Memory engine `delete_item` now returns `NotFound` for missing keys.
63. Memory engine scan pagination now starts via ordered `upper_bound` for faster skip-to-start behavior.
64. API dispatcher now uses bounded op-name validation plus sorted-array `lower_bound` lookup for deterministic, allocation-free dispatch matching.
