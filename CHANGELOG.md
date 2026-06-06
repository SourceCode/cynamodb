# Changelog

All notable changes to cynamoDB are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/) and this project adheres to
[Semantic Versioning](https://semver.org/).

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
