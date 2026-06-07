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
- **Auth** — DONE (presence/shape only at the time; superseded by v2.4.0 full SigV4).
- **501 vs UnknownOperation** and **empty-key rejection** — DONE, with tests.

## Addressed in v2.4.1 (from CYNAMODB_HARDCORE_QA.md / CYNAMODB_IMPROVEMENT_PLAN.md)

- **CS-3 numeric & set validation** (`test_qa_hardening.cpp`): `N`/`NS` values must
  match the DynamoDB number grammar (≤38 significant digits), validated recursively
  inside `M`/`L`; sets must be non-empty and free of duplicate members.
- **CS-10 single sizing source** (`core/sizing.hpp`): the item validator and JSON
  serializer now share one `attribute_size`; the serializer no longer returns 0 for
  non-scalar types (asserted equal in `test_qa_hardening.cpp`).
- **CS-11 observable corruption**: a record that fails to decode is logged with its
  offset and returns `nullopt` (never a silently-shortened item); regression test
  truncates an encoded record and asserts a hard failure.
- **CS-1b**: `JsonWriter::write` emits base64 for `B`/`BS`, matching the serializer.
- **End-to-end QA battery**: the external `task-tester` battery (`server/qa/`, 75
  checks across durability/concurrency/compaction/types/limits/pagination/protocol)
  reports **0 failures** against the v2.4.1 binary — all 11 attribute types are
  `ok` across memtable→flush→restart, and non-numeric `N`/empty keys are rejected.

## Addressed in v2.4.0

- **Full SigV4 verification** (`test_sigv4_crypto.cpp`): SHA-256/HMAC KATs, the AWS
  signing-key vector, canonical-request verification, and credential store.
- **Document-path expressions** (`test_document_paths.cpp`): nested map/list paths in
  Condition/Filter/Projection/Update, with copy-on-write.
- **TTL** (`test_ttl.cpp`), **capacity throttling** (`test_capacity_throttle.cpp`),
  **GSI/LSI** (`test_secondary_indexes.cpp`), **Streams** (`test_streams_e2e.cpp`),
  **PartiQL** (`test_partiql_exec.cpp`), **Backups/PITR/global tables**
  (`test_backups_e2e.cpp`, incl. cross-restart persistence).
- **Boundary/encoding** (`test_boundary.cpp`): large/high-precision numbers, unicode
  keys, attribute-name limits, control-character escaping, N sort keys beyond 2⁵³.

## Remaining gaps to close before production (recommended, prioritized)

1. **PartiQL transactions** (`ExecuteTransaction`), Contributor Insights, imports/exports,
   Kinesis streaming destination, resource policies, tagging, and auto-scaling still
   return `501 NotImplementedException`.
2. **PITR** restores the source table's current state (no continuous change log), and
   **global tables** are single-region — both intentional local-engine simplifications.
3. **Transaction isolation under contention.** `TransactWriteItems` validates all
   conditions/updates before applying, but is not serializable against a concurrent
   writer to the same keys mid-transaction.
4. **Soak / leak test.** A long-running mixed-workload test (hours) watching RSS to
   catch slow leaks and fragmentation under sustained flush/compaction.
5. **Fuzzing in CI.** Fuzz targets exist (`fuzz_json`, `fuzz_expressions`, `fuzz_sigv4`)
   but are not run regularly; wire them into a scheduled fuzzing job.
6. **Numeric sort-key precision.** The sort-key ordering codec uses `double` (exact to
   2⁵³); the stored `N` value round-trips exactly, but ordering of `N` sort keys beyond
   2⁵³ may collide.
