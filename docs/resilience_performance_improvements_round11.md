# Resilience and Performance Improvements (Round 11)

Implemented improvements in this round:

1. Added `saturating_fetch_add` helper for atomic counters in metrics.
2. `Metrics::increment_request()` now uses saturating atomic increment.
3. `Metrics::increment_error()` now uses saturating atomic increment.
4. `Metrics::increment_success()` now uses saturating atomic increment.
5. `Metrics::add_request_bytes()` now uses saturating atomic addition.
6. `Metrics::add_response_bytes()` now uses saturating atomic addition.
7. `Metrics::record_latency()` now saturates `total_latency_us_` updates.
8. `Metrics::record_latency()` now ignores negative durations.
9. Min latency updates now use CAS loops for thread-safe extrema tracking.
10. Max latency updates now use CAS loops for thread-safe extrema tracking.
11. Added configurable slow-query logging enable/disable control.
12. Added configurable slow-query logging sample interval (`every_n`).
13. Slow-query interval now guards `every_n == 0` by normalizing to `1`.
14. Added `Metrics::get_error_rate()`.
15. Added `Metrics::get_success_rate()`.
16. Added `Metrics::reset()` to clear counters and latency state.
17. Added `Arena::kDefaultBlockSize` constant.
18. Added `Arena::kMaxBlockSize` hard limit constant.
19. Added `Arena::kMaxAllocationBytes` hard limit constant.
20. Added `Arena::kMaxBlocks` hard limit constant.
21. `Arena` constructor now clamps requested block size into a safe range.
22. `Arena::allocate()` now rejects alignments greater than `alignof(std::max_align_t)`.
23. `Arena::allocate()` now rejects requests above `kMaxAllocationBytes`.
24. `Arena::allocate()` now supports oversized allocations via dedicated large-block path.
25. `Arena` byte accounting now uses saturating addition.
26. `Arena::allocate_large_block_locked()` now enforces `kMaxBlocks`.
27. `Arena::allocate_block()` now enforces `kMaxBlocks`.
28. Compactor now caps input SSTable count (`kMaxCompactionInputs`).
29. Compactor now caps merged output entries (`kMaxMergedEntries`).
30. Compactor now caps compaction key size (`kMaxKeyBytes`).
31. Compactor now rejects empty output SST path.
32. Compactor now rejects output path that equals any input path.
33. Compactor now de-duplicates duplicate input SST paths safely.
34. Compactor now rejects empty individual input paths.
35. Compactor now validates key bounds during merge iteration.
36. Compactor now fails fast when merged map exceeds configured cap.
37. Recovery manager now validates WAL path length before opening.
38. Recovery manager now caps diagnostic replay records (`kMaxReplayRecords`).
39. Recovery manager now tracks the last verified good WAL offset.
40. Recovery manager now truncates WAL tail on CRC mismatch.
41. Recovery manager now truncates WAL tail on partial CRC/key/value header reads.
42. Recovery manager now truncates WAL tail on invalid key/value length fields.
43. Recovery manager now truncates WAL tail on short key/value payload reads.
44. Recovery manager now advances `good_end` only after CRC-verified records.
45. Recovery manager now performs non-throwing truncate via `resize_file(..., error_code)`.
46. Added `kMaxBatchGetTotalKeys` limit in HTTP server.
47. Added `kMaxBatchWriteTotalRequests` limit in HTTP server.
48. Added `kMaxResponseBodyBytes` limit in HTTP server.
49. Added `append_with_limit(...)` helper for bounded response construction.
50. Added `escape_json_string(...)` helper and applied it to dynamic JSON fields.
51. HTTP session now enables `tcp::no_delay(true)`.
52. HTTP read loop now consumes buffered data before issuing new reads.
53. HTTP session buffer now enforces `kMaxReadBufferBytes` upper bound.
54. HTTP request handler now rejects oversized response bodies before send.
55. `handle_list_tables` now enforces response size cap while building JSON.
56. `handle_scan` response assembly now enforces size cap.
57. `handle_query` response assembly now enforces size cap.
58. `handle_batch_get_item` now validates table names.
59. `handle_batch_get_item` now enforces total key count cap.
60. `handle_batch_get_item` now de-duplicates repeated keys.
61. `handle_batch_get_item` now parses keys through `parse_attribute_map` and validates key completeness.
62. `handle_batch_write_item` now validates table names and total request count cap.
63. `handle_batch_write_item` now parses Put/Delete keys via `parse_attribute_map` and enforces key completeness.
64. Added regression coverage for new resilience/performance behavior across metrics, arena, API/auth, table management, expressions, compaction, and recovery.
