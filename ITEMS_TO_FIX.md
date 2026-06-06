# ITEMS_TO_FIX

Catalog of broken / stubbed / buggy items discovered while reviewing, building,
and testing cynamoDB. Each item has a stable ID, severity, evidence (file:line),
and a status that is updated as fixes land. Regression tests are noted per item.

Legend: ✅ fixed + covered by test · 🟡 fixed, test pending · 🔴 open

---

## A. Build-blocking defects

### F1 — `-Werror` applied to third-party dependencies ✅
- **Evidence:** `CMakeLists.txt:13` used a global `add_compile_options(... -Werror)`,
  which propagated to FetchContent deps. Under GCC 16, Catch2 and simdjson headers
  emit `-Wdeprecated-literal-operator`, breaking the build before our code compiled.
- **Fix:** Scoped strict warning flags to our own targets only (`CYNAMODB_WARNING_FLAGS`),
  and downgraded the single dep-only deprecation to a warning. `CMakeLists.txt`.

### F2 — `std::unique_lock` used without `<mutex>` ✅
- **Evidence:** `src/backups/manager.cpp:30,56` — `'unique_lock' is not a member of 'std'`.
- **Fix:** Added `#include <mutex>`.

### F3 — `printf("%llx")` format mismatch with `uint64_t` ✅
- **Evidence:** `src/streams/manager.cpp:16`, `src/http/server.cpp:99` —
  `%llx` expects `unsigned long long` but got `uint64_t` (`unsigned long`) → `-Werror=format`.
- **Fix:** Cast arguments to `unsigned long long`.

## B. Crashes

### F4 — Metrics use-after-free → SIGSEGV ✅
- **Evidence:** `tests/test_metrics.cpp:8` crashed with `SIGSEGV`. Root cause in
  `src/observability/metrics.cpp`: each thread pushed a pointer to its `thread_local`
  data into a global registry but never removed it. After a worker thread exited, its
  storage was destroyed while the registry still held the dangling pointer;
  `get_total()`/`reset_all()` then dereferenced freed memory. Counts of exited
  threads were also lost.
- **Fix:** RAII registration that, on thread exit, folds the thread's counters into a
  global "dead totals" accumulator and removes the pointer under the registry mutex.

## C. Stubbed engine functionality (root cause of "failed inserts and lookups")

### S1 — `MemoryEngine::get` always returns `nullopt` ✅
- **Evidence:** `src/engine/memory_engine.cpp:16` ignored its args and returned
  `std::nullopt`. Every point lookup (incl. `TransactGetItems`) failed.

### S2 — `MemoryEngine::remove` is a no-op ✅
- **Evidence:** `src/engine/memory_engine.cpp:12`. Deletes silently did nothing.

### S3 — `MemoryEngine::scan` returns empty ✅
- **Evidence:** `src/engine/memory_engine.cpp:21`. Scan API returned no items.

### S4 — `MemoryEngine::query` returns empty ✅
- **Evidence:** `src/engine/memory_engine.cpp:26`. Query API returned no items.

### S5 — `put_item` / `get_item` / `delete_item` / `make_key` stubbed ✅
- **Evidence:** `src/engine/memory_engine.cpp:31-49`. `put_item` discarded the item,
  `get_item` always returned `ItemNotFound`, `make_key` returned `""` (so all items
  would collide on one key). Schema-aware item ops and key ordering were non-functional.

## D. LSM engine defects (secondary storage path)

### S6 — `LsmEngine::scan` and `LsmEngine::query` return empty ✅
- **Evidence:** `src/engine/lsm/lsm_engine.cpp:72-80`.
- **Fix:** Implemented a merged, tombstone-resolved, sorted view (`materialize()`)
  across memtable + immutable memtables + SSTables (newest-wins), with ordered
  pagination. Covered by `tests/test_lsm_engine.cpp`.

### S7 — `LsmEngine::get` ignores memtable tombstones over SSTables ✅
- **Evidence:** `src/engine/lsm/lsm_engine.cpp:53-70`. A key deleted in the memtable
  but still present in a flushed SSTable returned stale data.
- **Fix:** `get` now treats the newest level that knows the key as authoritative —
  a tombstone stops the search. SSTable tombstones are detected via index presence
  with a `nullopt` value read. Covered by the "flushed SSTable" test.

### S10 — `LsmEngine` ignored `table_name`: all tables shared one keyspace ✅
- **Evidence:** `src/engine/lsm/lsm_engine.cpp` `put/get/remove` did `(void)table_name`,
  so two tables with the same key collided and scans returned items from all tables.
- **Fix:** Keys are now namespaced as `table"\0"key`; scan/query restrict to the
  table's contiguous key range and report table-stripped pagination cursors.
  Covered by the "isolates tables sharing the same key" test.

## E. HTTP / server wiring (drop-in-replacement gap)

### S8 — HTTP layer never routes requests to handlers ✅
- **Evidence:** `src/http/server.cpp` `handle_request` returned `{}` for every request
  except `/health`. `src/api/dispatcher.cpp` only parsed the target; no handlers existed.
- **Fix:** Added a transport-agnostic handler layer
  (`include/cynamodb/api/handlers.hpp`, `src/api/handlers.cpp`):
  `handle_operation(tables, storage, op, body)` implementing CreateTable, DescribeTable,
  ListTables, PutItem, GetItem, DeleteItem, Scan, and Query (KeyConditions EQ).
  `HttpSession::handle_request` now parses `X-Amz-Target`, dispatches, and returns
  proper status + `x-amzn-ErrorType`. Covered by `tests/test_api_handlers.cpp` and a
  live `curl` smoke test.

### S9 — `main.cpp` never starts the HTTP server ✅
- **Evidence:** `src/main.cpp` constructed an empty `io_context` and ran it.
- **Fix:** `main` now builds a `Context` and starts `HttpServer`, blocking on
  SIGINT/SIGTERM for graceful shutdown. Verified by starting the binary and driving
  it with `curl` (CreateTable/PutItem/GetItem/Scan/ListTables all return correct JSON).

### S11 — `JsonParser::parse_table_definition` was a stub ✅
- **Evidence:** `src/json/serializer.cpp` returned `{}`, so CreateTable could not read
  a schema. **Fix:** Implemented parsing of TableName/KeySchema/AttributeDefinitions/BillingMode.

> Remaining HTTP scope (documented, not yet implemented): Query via
> `KeyConditionExpression` (only legacy `KeyConditions` EQ is supported), range/`begins_with`
> sort-key conditions, UpdateItem, Batch/Transact operations over HTTP, DeleteTable,
> SigV4 auth enforcement, and WAL replay on restart for durability of unflushed writes.

## F. Test-quality issues

### T1 — Existing tests are placeholders that assert stub behavior ✅
- **Evidence:** `tests/test_memory_engine.cpp` (never calls `get`),
  `tests/test_items_http.cpp:19` (`REQUIRE(true)`). They passed against stubs and gave
  false confidence. Replaced/augmented with rigorous coverage (see `test_memory_engine.cpp`).

## H. Persistence across restarts ✅

The system previously lost all data on restart. Three independent gaps were fixed so
that a fresh process over the same data directory recovers the full state:

### S12 — Write-ahead log was never written or replayed ✅
- **Evidence:** `LsmEngine::put/remove` never called `wal_->append`, and the constructor
  never replayed the WAL, so unflushed memtable writes vanished on restart.
- **Fix:** `put`/`remove` now append to a per-memtable WAL segment (`wal_<gen>.log`)
  using a new full-fidelity record codec (`engine/lsm/record_codec`). On startup the
  engine replays all segments into the memtable, checkpoints into a fresh segment, and
  deletes the old ones; segments are dropped once their memtable is flushed to an SSTable.

### S13 — SSTables were not reloaded from the manifest on startup ✅
- **Evidence:** the constructor called `manifest_->load()` but never reconstructed the
  in-memory `sstables_` list, so even *flushed* data was invisible after restart.
- **Fix:** `load_sstables_from_manifest()` rebuilds `sstables_` newest-first from the
  manifest's per-level file list.

### S14 — `TableManager` metadata persistence was a stub ✅
- **Evidence:** `TableManager::load_metadata()`/`save_metadata()` were empty, so the
  table catalog was lost on restart and every operation then failed with
  ResourceNotFoundException.
- **Fix:** Implemented a versioned, atomically-replaced (`.tmp` + rename) binary catalog
  storing each table's name, key schema, attribute definitions, billing mode, and
  creation time. Malformed/missing files are tolerated.

- Covered by `tests/test_lsm_engine.cpp` (WAL replay + SSTable reload across a restart),
  `tests/test_table_manager.cpp` (catalog round-trip + corrupt-file tolerance), and a
  live two-process `curl` restart test.

## I. Concurrency & durability hardening (production-readiness review)

### S16 — Compaction thread busy-spin at ≥4 L0 SSTables ✅
- **Evidence:** `should_compact_L0()` returns true at ≥4 L0 files, but
  `run_leveled_compaction` only logs (it never merges/removes files). The compaction
  thread waited on a level-triggered predicate (`should_compact_L0()`) that therefore
  stayed permanently true, so once ~4000+ records produced ≥4 SSTables the thread spun
  at 100% CPU and contended the engine mutex.
- **Fix:** made the signal edge-triggered (`compaction_pending_`, set by the flush
  thread, cleared once handled). Covered by the new `[stress]` test (5000-op run now
  completes in ~0.1s instead of pegging a core).

### S17 — WAL not durable against process crash, then not against power loss ✅
- **Evidence:** `WriteAheadLog::append` only flushed every 128 writes, so a `kill -9`
  lost up to 127 acknowledged writes (only graceful shutdown was durable).
- **Fix:** every committed record is now flushed to the OS *and* `fdatasync`'d to the
  device (via a companion descriptor), so an acknowledged `PutItem` survives both a
  process crash (`kill -9`) and a power/OS crash. Tunable with `CYNAMODB_WAL_FSYNC=0`
  for throughput. Covered by the crash-recovery integration test.

### S18 — Compaction was a no-op; L0 SSTables grew unbounded ✅
- **Evidence:** `run_leveled_compaction` only logged, so flushed SSTables accumulated
  forever — unbounded disk use and O(files) read amplification.
- **Fix:** implemented real compaction in `LsmEngine::compact_locked()`: it merges all
  on-disk SSTables into one (newest-wins, tombstones purged), rewrites the manifest and
  in-memory list, and deletes the obsolete files — all under the engine lock so it can
  never race the flush thread's manifest edits. The merged file inherits the highest
  sequence number it replaces, preserving read precedence across restarts. Covered by
  the new `[compaction]` test (8 flushes stay ≤4 files, reads/deletes correct, survives
  restart). (Per-key SSTable reads during merge and a leveled strategy are noted perf
  follow-ups in TEST_COVERAGE.md.)

### S15 — `LsmEngine` shutdown lost-wakeup deadlock ✅
- **Evidence:** the destructor set `shutting_down_` and called `notify_all()` *without*
  holding `mutex_`, while the flush/compaction threads evaluate their CV predicates
  *under* `mutex_`. A worker could read `shutting_down_ == false`, commit to blocking,
  and only then have the destructor flip the flag and notify — a lost wakeup that hangs
  the join forever. Surfaced as a hung test process under AddressSanitizer (zero CPU,
  both threads parked in `futex_do_wait`); the wider timing made the rare race reliable.
- **Fix:** set `shutting_down_` while holding `mutex_` in the destructor, then notify.
  Confirmed by the full suite completing cleanly under ASan/UBSan.

## G. Known limitations (documented, lower priority, outside the scalar data path)

These are pre-existing partial implementations in secondary subsystems. They do not
affect the scalar (S/N) insert/lookup/scan/query/sort path that was the reported
failure, and are recorded here so nothing is hidden:

- **Binary (`B`) attribute values are not base64-decoded** on parse
  (`src/json/serializer.cpp` — stored as an empty byte vector). S/N/BOOL/NULL/M/L parse.
- **Response serialization omits `L`/`SS`/`NS`/`BS`/`B`** (`JsonSerializer::serialize_attribute_value`
  emits `{"NULL":true}` for unhandled types). Scalars and maps round-trip correctly.
- **SSTable on-disk codec is limited to S/N/BOOL** (`src/engine/lsm/sstable.cpp`); the
  WAL codec is full-fidelity, so complex types survive until flushed but degrade after a
  flush to an SSTable.
- **`TableManager` persistence does not round-trip secondary-index/stream/TTL config**
  (only the fields the data plane needs).
- **GSI/LSI replication, PartiQL execution, SigV4 enforcement, and transaction OCC**
  are placeholder/partial (`gsi_manager.cpp`, `lsi_manager.cpp`, `partiql/parser.cpp`,
  `auth/sigv4.cpp`, `transactions/manager.cpp`).

## Verification

- Build: clean under GCC 16 in both Debug and Release (`-Werror`, IPO/LTO on our code).
- Unit tests: 65 cases / 243k assertions pass, including a multi-threaded engine
  stress test and restart-persistence tests.
- **Live CRUD integration test** (`tests/integration/crud_live_test.py`, run via
  `ctest` as `crud_live_integration`): starts the real server binary over HTTP and
  validates the full lifecycle across four sessions / three process restarts —
  insert 500 records, 50 exact-accuracy point lookups, paginated Scan, Query (EQ)
  with composite-key numeric sort, 25 updates, then deletes — confirming that
  inserts, updates, and deletes each persist correctly across restarts (1586
  assertions). Also passes against the ASan/UBSan-instrumented server.
- Sanitizers: suite passes under AddressSanitizer + UndefinedBehaviorSanitizer
  (`-fno-sanitize-recover=all`) across multiple random orderings — no leaks, no UB.
