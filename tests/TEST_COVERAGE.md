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

**API / HTTP (unit `test_api_handlers.cpp`, `test_dynamo_features.cpp` + live)**
- CreateTable/DescribeTable/ListTables/DeleteTable/UpdateTable end-to-end.
- PutItem/GetItem/UpdateItem/DeleteItem/Scan/Query end-to-end, incl.
  `ConditionExpression`, `UpdateExpression`, `ReturnValues`.
- Query `KeyConditionExpression` + sort-key operators (`<`/`<=`/`>`/`>=`/`BETWEEN`/
  `begins_with`), `FilterExpression`, `ProjectionExpression`, `ScanIndexForward`.
- Batch & transactions: `BatchWriteItem`/`BatchGetItem`/`TransactWriteItems`
  (all-or-nothing)/`TransactGetItems`.
- Error mapping (ResourceNotFound/ResourceInUse/Validation/Serialization/
  ConditionalCheckFailed/TransactionCanceled/NotImplemented(501)/UnknownOperation).
- **Docs contract** (`[contract]`): every operation documented as Implemented in
  `docs/api.md` is asserted reachable (never 501/UnknownOperation).
- **Robustness** (`[robustness]`): missing fields, wrong key types, oversized items
  (>400KB), unrecognized attribute types, malformed/empty JSON, missing TableName,
  empty-string key attributes.
- **Live CRUD over HTTP across 3 restarts** (`crud_live_test.py`, 1586 assertions).
- **Live feature suite** (`features_live_test.py`): complex types surviving a real
  memtable→SSTable flush, conditional writes, UpdateItem, query expressions,
  batch/transactions, table lifecycle, error shapes, and SigV4 enforcement.
- **Concurrent load** (`concurrent_load_test.py`): 4000 records via 8 parallel clients,
  consistency verified (no lost/duplicated/corrupted items).

**Complex types & codecs (`test_complex_types.cpp`, `test_update_expression.cpp`)**
- SSTable codec round-trips M/L/SS/NS/BS/B (the whole row survives a flush).
- LsmEngine end-to-end: a map survives writing >1000 rows (forces a flush).
- JSON wire codec round-trips L/SS/NS/BS/B without degrading to `{"NULL":true}`;
  base64 encode/decode (incl. malformed rejection); UpdateExpression applier.

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

## Addressed in v2.3.0 (from CYNAMODB_FINDINGS.md)

- **Full attribute-type fidelity** — DONE. `B`/`BS` base64 decode/encode; `L`/`SS`/`NS`/
  `BS`/`B` serialize correctly; the SSTable codec shares the WAL's full-fidelity format
  so every type survives a flush. Round-trip + write→flush→read tests added.
- **Query expressiveness** — DONE. `KeyConditionExpression`, sort-key range conditions,
  `FilterExpression`, `ProjectionExpression`, `ScanIndexForward`, with tests.
- **Conditional writes & UpdateItem** — DONE. `ConditionExpression` (atomic via the
  engine `mutate` primitive), `UpdateItem` with update expressions, and `ReturnValues`.
- **Transactions** — DONE (best-effort). `TransactWriteItems` verifies all conditions
  before applying any write (all-or-nothing for the no-concurrent-writer case);
  `TransactGetItems` reads. Strict cross-key isolation under contention is future work.
- **Auth** — DONE. `CYNAMODB_REQUIRE_AUTH` enforces SigV4 header presence/shape
  (cryptographic signature verification remains future work).
- **501 vs UnknownOperation** and **empty-key rejection** — DONE, with tests.

## Remaining gaps to close before production (recommended, prioritized)

These are **not yet built**; they reflect known partial implementations and deeper
validation that a production deployment warrants.

1. **Secondary indexes (GSI/LSI).** Definitions are accepted but `IndexName` queries are
   not honored; add replication/projection and end-to-end index query tests.
2. **Document-path expressions.** `ProjectionExpression`/`UpdateExpression` operate on
   top-level attributes only; add nested-path (`a.b`, `a[0]`) support.
3. **Streams / TTL / backups / PITR / global tables / PartiQL.** Recognized but return
   `501 NotImplementedException`; implement and test as needed.
4. **SigV4 signature verification.** Enforcement checks presence/shape only; add a
   credential store and full canonical-request signature comparison.
5. **Capacity throttling** is implemented but not wired into the request path; wire it
   and test 4xx throttling responses.
6. **Soak / leak test.** A long-running mixed-workload test (hours) watching RSS to
   catch slow leaks and fragmentation under sustained flush/compaction.
7. **Fuzzing in CI.** Fuzz targets exist (`fuzz_json`, `fuzz_expressions`, `fuzz_sigv4`)
   but are not run regularly; wire them into a scheduled fuzzing job.
8. **Boundary/encoding tests.** Very large numbers (beyond 2^53 — the key codec uses
   double, see ITEMS_TO_FIX §G), unicode keys, max attribute-name length, and
   duplicate-attribute handling.
