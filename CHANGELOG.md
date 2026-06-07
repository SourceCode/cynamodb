# Changelog

All notable changes to cynamoDB are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/) and this project adheres to
[Semantic Versioning](https://semver.org/).

## [2.4.3] - 2026-06-07

HTTP-layer correctness, addressing the QA battery's three observational (`info`) probes.

### Changed
- **Method handling.** Non-`POST` requests to the data-plane endpoint now return
  `405 Method Not Allowed` with an `Allow: POST` header instead of being routed through
  the operation dispatcher (`GET /` previously returned a generic 400). `GET`/`HEAD`
  `/health` still return 200.
- **Content-Type negotiation.** The response `Content-Type` now echoes the DynamoDB JSON
  protocol version the client used (`application/x-amz-json-1.0` or `-1.1`). Request
  Content-Type remains lenient, matching AWS.

### Notes
- The QA battery's lost-update `info` probe measures client-side read-modify-write via
  `PutItem` (inherently last-writer-wins); the engine's atomic alternative,
  `UpdateItem ... ADD`, reaches the exact count under concurrency (verified by the
  battery's `update` suite). No engine change is appropriate there.

### Tests
- `features_live_test.py` extended with HTTP-layer checks (405 + `Allow`, `/health`,
  Content-Type echo). External QA battery: 108 pass / 0 fail / 3 info (the 3 info lines
  are observational `reporter.info` records, not assertions).

## [2.4.2] - 2026-06-07

Closes the remaining open items from `CYNAMODB_IMPROVEMENT_PLAN.md` (round-4 deep
sweep): the full external QA battery (111 checks, 11 suites) now reports **0 failures**.

### Added / Fixed
- **CS-15 — parallel scan.** `Scan` now honors `Segment`/`TotalSegments`: each segment
  returns a deterministic, non-overlapping partition of the table (keys hashed into
  `TotalSegments` buckets), so the union of all segments is exactly one full scan with no
  overlap. Honors `Limit` with a resumable per-segment cursor; mismatched or out-of-range
  `Segment`/`TotalSegments` are rejected with `ValidationException`. (Previously every
  segment silently returned the whole table.)
- **CS-17 — request-size limits.** `BatchWriteItem` (≤25), `BatchGetItem` (≤100),
  `TransactWriteItems`/`TransactGetItems` (≤100) now reject oversized requests with
  `ValidationException` instead of accepting them.
- **CS-14 — number normalization.** `N`/`NS` values are canonicalized on the write path
  (`1.0`→`1`, `+5`→`5`, `-0`→`0`, `1.50`→`1.5`, `007`→`7`, `1e2`→`100`), so numeric values
  are representation-independent, while 38-digit precision is preserved.

### Tests
- New `test_qa_round4.cpp` (normalization, parallel-scan tiling + pagination, batch/txn
  caps). 148 unit cases + 5 integration groups, green under ASan + UBSan; external QA
  battery passes 0-fail.

## [2.4.1] - 2026-06-06

Hardening pass driven by `CYNAMODB_HARDCORE_QA.md` and `CYNAMODB_IMPROVEMENT_PLAN.md`.
The external QA battery (`task-tester/server/qa`, 75 checks) now reports **0 failures**
against this build (previously 9), with all 11 attribute types `ok` across
memtable→flush→restart.

### Added / Fixed
- **CS-3 — numeric & set validation.** `N`/`NS` values are validated against the
  DynamoDB number grammar (≤38 significant digits) and rejected with
  `ValidationException` otherwise — recursively inside `M`/`L` too. Sets (`SS`/`NS`/`BS`)
  must be non-empty and free of duplicate members. Previously garbage like
  `{"N":"not-a-number"}` was stored verbatim.
- **CS-10 — one sizing implementation.** The item validator and JSON serializer now
  share `core::attribute_size` (`core/sizing.hpp`); the serializer previously returned
  `0` for every non-scalar type, diverging from the validator.
- **CS-11 — observable corruption.** A record that fails to decode is now logged with
  its offset and returns a hard failure instead of silently dropping the row.
- **CS-1b.** `JsonWriter::write` emits base64 for `B`/`BS`, matching the serializer.

### Tests
- New `test_qa_hardening.cpp` (numeric/set validation, sizing agreement, truncated-record
  decode). 142 unit cases + 5 integration groups, green under ASan + UBSan; the external
  QA battery passes with 0 failures.

## [2.4.0] - 2026-06-06

This release closes the remaining limitations from the v2.3.0 compliance matrix:
cynamoDB now implements the large majority of the DynamoDB API surface.

### Added
- **Full SigV4 verification.** Self-contained SHA-256 + HMAC-SHA256 (validated against
  FIPS/RFC 4231 vectors and the AWS signing-key example); `CYNAMODB_REQUIRE_AUTH` now
  reconstructs the canonical request, derives the signing key, and compares signatures.
  A credential store is seeded from `CYNAMODB_ACCESS_KEY_ID`/`CYNAMODB_SECRET_ACCESS_KEY`.
- **Document-path expressions** (`a.b.c`, `a[0]`) across Condition/Filter/Projection/
  Update expressions, with copy-on-write so nested mutations never alias shared values.
- **TTL**: `UpdateTimeToLive`/`DescribeTimeToLive`, persisted; expired items filtered
  from GetItem (lazy reaping), Query, Scan, Batch/TransactGet.
- **Capacity throttling**: `ProvisionedThroughput` is parsed and enforced; provisioned
  tables return `ProvisionedThroughputExceededException` when their bucket is exhausted.
- **Secondary indexes (GSI/LSI)**: parsed at CreateTable, persisted, maintained on every
  write (sparse + projection-aware), and queryable via `Query`/`Scan` with `IndexName`.
- **DynamoDB Streams**: INSERT/MODIFY/REMOVE records (NEW/OLD images per view type) and
  the data plane (`ListStreams`/`DescribeStream`/`GetShardIterator`/`GetRecords`).
- **PartiQL**: `ExecuteStatement`/`BatchExecuteStatement` for SELECT/INSERT/UPDATE/DELETE
  with positional `?` parameters.
- **Backups / PITR / global tables**: durable JSON backup snapshots
  (`CreateBackup`/`ListBackups`/`DescribeBackup`/`DeleteBackup`/`RestoreTableFromBackup`),
  `RestoreTableToPointInTime`, continuous-backups status, and single-region global tables.
- Table-catalog metadata format extended (v2 TTL, v3 indexes, v4 stream spec); older
  files still load.

### Fixed
- Stream manager: the 24-hour retention check compared nanoseconds against epoch-seconds
  (purging every record immediately), and `append_record` used `map::operator[]` under a
  shared lock — both fixed.
- `N`/`NS` attribute values are now JSON-escaped on output.

### Tests
- New suites: SigV4 crypto + verifier, document paths, TTL, capacity throttling,
  secondary indexes, streams e2e, PartiQL, backups (incl. cross-restart persistence),
  and boundary/encoding (large numbers, unicode keys, attribute-name limits). 137 unit
  cases + 5 integration groups, all passing under AddressSanitizer + UndefinedBehaviorSanitizer.

## [2.3.0] - 2026-06-06

This release closes the data-integrity gaps and feature holes surfaced by an
external audit (`CYNAMODB_FINDINGS.md`). The headline fix: writes are no longer
silently lost or corrupted. It also fills in the most-used DynamoDB operations
(`UpdateItem`, conditional writes, batch/transactions, modern query expressions,
table lifecycle) so much more of a real workload runs unchanged.

### Fixed (data integrity — "fail loud, never silent")
- **Complex types are no longer silently lost.** The SSTable on-disk codec only
  persisted `S`/`N`/`BOOL`, so a `Map` vanished entirely after a memtable flush and
  `L`/`SS`/`NS`/`BS`/`B` were dropped. The SSTable codec now uses the same
  full-fidelity binary format as the WAL, and every attribute type round-trips and
  survives a flush. (Finding #1b)
- **`L`/`SS`/`NS`/`BS`/`B` are no longer coerced to `NULL` on the wire.** The JSON
  parser now decodes `B`/`BS` from base64 and populates `SS`/`NS`/`BS`, and the
  serializer emits `L`/`SS`/`NS`/`BS`/`B` instead of degrading them to `{"NULL":true}`.
  Invalid base64 in a `B`/`BS` value is rejected with `ValidationException`. (Finding #1a)
- **`ConditionExpression` is enforced.** Previously it was silently ignored, so a
  guarded `PutItem`/`DeleteItem` would clobber data. Conditions are now evaluated
  atomically (read-modify-write under the engine lock) and a failed condition returns
  `ConditionalCheckFailedException`. (Finding #2)
- **Empty-string key attributes are rejected** with `ValidationException`, matching AWS. (Finding #8)
- **A `GetItem` miss always returns the canonical `{}`** (it could previously emit an
  empty body when entangled with the codec bug). (Finding #9)

### Added (operations)
- **`UpdateItem`** with `UpdateExpression` (`SET`/`REMOVE`/`ADD`/`DELETE`, `+`/`-`,
  `if_not_exists`, `list_append`), `ConditionExpression`, and `ReturnValues`. (Finding #3)
- **`Query` `KeyConditionExpression`** (alongside legacy `KeyConditions`) with sort-key
  operators (`<`, `<=`, `>`, `>=`, `BETWEEN`, `begins_with`), plus `FilterExpression`,
  `ProjectionExpression`, and `ScanIndexForward`. `Scan` gains `FilterExpression` and
  `ProjectionExpression`. (Finding #4)
- **Batch & transactions**: `BatchWriteItem`, `BatchGetItem`, `TransactWriteItems`
  (all-or-nothing, conditions checked before any write), `TransactGetItems`. (Finding #5)
- **`DeleteTable`** (drops the catalog entry and purges the table's items) and a minimal
  **`UpdateTable`**. (Finding #6)
- **Expression engine** extended with `begins_with`, `contains`, `attribute_type`,
  `size`, `BETWEEN`, `IN`, and numeric ordering comparisons.
- **Opt-in SigV4 enforcement** via `CYNAMODB_REQUIRE_AUTH`: unsigned requests return
  `MissingAuthenticationTokenException`, malformed ones `IncompleteSignatureException`.
  (Finding #10)
- An atomic `mutate` read-modify-write primitive and a `drop_table` purge on the
  storage-engine interface (implemented for both the LSM and in-memory engines).

### Changed
- **Recognized-but-unimplemented operations now return `501 NotImplementedException`**
  instead of `400 UnknownOperationException`, so SDK feature-detection can tell "not
  built yet" from a typo'd target. Genuinely unknown targets still return
  `UnknownOperationException`. (Finding #7)
- `docs/api.md` and `AGENTS.md` were rewritten to match the implemented surface, with a
  contract-tested **Implemented vs. Planned** matrix; a unit test asserts every
  documented-Implemented op stays reachable. (Finding #11)

### Tests
- New unit suites: complex-type codec/flush round-trips, base64, the UpdateExpression
  applier, and a broad feature suite (conditions, updates, query expressions,
  batch/transactions, table lifecycle, error shapes). New live integration test
  (`features_live_test.py`) exercises all of the above over real HTTP, including a
  forced flush and auth enforcement. The full suite passes under
  AddressSanitizer + UndefinedBehaviorSanitizer.

## [2.2.0] - 2026-06-06

This release makes cynamoDB a functional local DynamoDB replacement. The previous
release built but the data path was non-operational (stubbed storage engine, an HTTP
layer that never routed requests, and a server that was never started). Inserts and
lookups now work end-to-end over HTTP and persist correctly across restarts and
crashes. See `ITEMS_TO_FIX.md` for the full investigation and `tests/TEST_COVERAGE.md`
for the coverage map and remaining gaps.

### Added
- End-to-end HTTP/JSON data plane: `CreateTable`, `DescribeTable`, `ListTables`,
  `PutItem`, `GetItem`, `DeleteItem`, `Scan` (with `Limit`/`ExclusiveStartKey`
  pagination), and `Query` (partition-key equality, sort-key ordered) — dispatched
  from `X-Amz-Target` with DynamoDB-style error types (`x-amzn-ErrorType`).
- The server now actually starts in `main` and serves requests until `SIGINT`/`SIGTERM`.
- Order-preserving primary-key codec so scans/queries return items correctly sorted by
  partition then sort key (numeric sort keys sort numerically).
- Persistence across restarts: WAL replay, SSTable reload from the manifest, and
  atomic table-catalog persistence.
- Real LSM compaction: SSTables are merged (newest-wins, tombstones purged), bounding
  on-disk file count; data survives compaction and restart.
- WAL durability: every acknowledged write is `fdatasync`'d to disk (survives process
  and power/OS crashes); tunable via `CYNAMODB_WAL_FSYNC=0`.
- Full-fidelity WAL record codec for all attribute types.
- Test suite overhaul: rigorous engine unit tests, live HTTP integration tests
  (CRUD across restarts, crash recovery via `SIGKILL`, concurrent multi-client load),
  a heavy multi-flush/compaction stress test, and HTTP robustness/negative tests — all
  passing under AddressSanitizer + UndefinedBehaviorSanitizer.

### Fixed
- `MemoryEngine` was fully stubbed (`get`/`scan`/`query`/`remove`/`put_item`/`get_item`/
  `delete_item`/`make_key`) — every lookup failed. Now fully implemented.
- `LsmEngine` `scan`/`query` returned nothing; `get` ignored memtable tombstones over
  SSTables (stale reads); and `table_name` was ignored so all tables shared one
  keyspace. All fixed.
- Use-after-free crash (`SIGSEGV`) in the thread-local metrics registry.
- Lost-wakeup deadlock on `LsmEngine` shutdown.
- Compaction thread busy-spin (100% CPU) once ≥4 L0 SSTables existed.
- `JsonParser::parse_table_definition` and `TableManager` metadata persistence were
  stubs (catalog was lost on restart).
- Build no longer applies `-Werror` to third-party dependencies; fixed missing
  `<mutex>` include and `printf` format mismatches.

### Changed
- `Context::storage_engine` is now the `StorageEngine` interface type (decoupled from
  the concrete LSM engine).
- HTTP `Server` header reports `cynamoDB/2.2.0`.

## [2.1.2] - prior release

- Initial scaffolding: build system, HTTP server skeleton, LSM primitives, expression
  and SigV4 parsers, and component stubs. The data path was not yet functional.
