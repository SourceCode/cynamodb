# Test coverage & production-readiness review

How to run everything:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j
ctest --test-dir build --output-on-failure
```

`ctest` runs four suites: `unit_tests` (Catch2), plus three live integration tests
that drive the real server binary over HTTP (`crud_live_integration`,
`crash_recovery_integration`, `concurrent_load_integration`).

## What is covered now

**Engine (unit, `tests/test_memory_engine.cpp`, `test_lsm_engine.cpp`)**
- CRUD round-trips, overwrites, missing-key reads, tens of thousands of inserts.
- Scan ordering + pagination (every item once, no dupes); Query equality filtering.
- Composite keys: partition grouping + numeric/lexicographic sort-key ordering.
- Tombstones across memtable / immutable / SSTable levels; multi-table isolation.
- Concurrent writers (engine-level).
- **Heavy load** (`[stress]`): 5000 ops forcing ≥4 flushes + compaction; every read,
  update, delete, and the full scan validated.

**Persistence / durability**
- WAL replay + SSTable reload across a clean restart; table-catalog round-trip;
  corrupt/missing metadata tolerated (unit).
- **Crash recovery** (`crash_recovery_test.py`): 1500 writes + 300 deletes survive two
  `SIGKILL`s — validates WAL/SSTable/manifest durability with no graceful shutdown.

**API / HTTP (unit `test_api_handlers.cpp` + live)**
- CreateTable/DescribeTable/ListTables/PutItem/GetItem/DeleteItem/Scan/Query end-to-end.
- Error mapping (ResourceNotFound/ResourceInUse/Validation/Serialization/UnknownOperation).
- **Robustness** (`[robustness]`): missing fields, wrong key types, oversized items
  (>400KB), unrecognized attribute types, malformed/empty JSON, missing TableName.
- **Live CRUD over HTTP across 3 restarts** (`crud_live_test.py`, 1586 assertions).
- **Concurrent load** (`concurrent_load_test.py`): 4000 records via 8 parallel clients,
  consistency verified (no lost/duplicated/corrupted items).

**Sanitizers**: full unit suite + live tests pass under AddressSanitizer +
UndefinedBehaviorSanitizer (`-fno-sanitize-recover=all`).

## Recently addressed (top production recommendations)

- **Power-loss durability (fsync)** — DONE. Every committed WAL record is now
  `fdatasync`'d to the device; acknowledged writes survive process *and* power/OS
  crash. Tunable via `CYNAMODB_WAL_FSYNC=0`. (Validated by `crash_recovery_test.py`.)
- **Real compaction** — DONE. SSTables are merged into one (newest-wins, tombstones
  purged); file count stays bounded and data survives restart. (Validated by the
  `[compaction]` test.) Remaining perf follow-ups: compaction reads SSTables per-key
  (add a sequential `SSTable::load_all`), it holds the engine lock for the whole merge
  (acceptable at current scale), and the strategy is full-merge rather than leveled.

## Remaining gaps to close before production (recommended, prioritized)

These are **not yet built**; they reflect known partial implementations (see
ITEMS_TO_FIX.md §G) and deeper validation that a production deployment warrants.

1. **Full attribute-type fidelity.** Binary (`B`) is not base64-decoded; `L`/`SS`/`NS`/
   `BS`/`B` are not serialized in responses; the SSTable codec only persists S/N/BOOL
   (complex types are lost on flush). Finish the codecs and add round-trip tests
   (including a value that is written, flushed, and read back).
2. **Query expressiveness.** Only legacy `KeyConditions` EQ is supported. Add
   `KeyConditionExpression`, sort-key range conditions (`<`, `between`, `begins_with`),
   `FilterExpression`, projection, and `ScanIndexForward`, with tests.
3. **Conditional writes & UpdateItem.** `ConditionExpression`, `UpdateItem` with
   update expressions, and `ReturnValues` are absent; add them with conflict tests.
4. **Transactions.** `TransactWriteItems` OCC is mocked (no real isolation/atomicity);
   add atomicity-under-conflict and all-or-nothing rollback tests.
5. **Secondary indexes (GSI/LSI).** Replication/projection are placeholders; add
   end-to-end index query tests once implemented.
6. **Auth.** SigV4 is parsed but not enforced; add an auth-required mode + tests.
7. **Capacity throttling** is implemented but not wired into the request path; wire it
   and test 4xx throttling responses.
8. **Soak / leak test.** A long-running mixed-workload test (hours) watching RSS to
   catch slow leaks and fragmentation under sustained flush/compaction.
9. **Fuzzing in CI.** Fuzz targets exist (`fuzz_json`, `fuzz_expressions`, `fuzz_sigv4`)
   but are not run regularly; wire them into a scheduled fuzzing job.
10. **Boundary/encoding tests.** Empty strings, very large numbers (beyond 2^53 — the
    key codec uses double, see ITEMS_TO_FIX §G), unicode keys, max attribute-name
    length, and duplicate-attribute handling.
