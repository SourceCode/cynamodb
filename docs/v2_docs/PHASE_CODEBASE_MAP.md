# v2 Phase-to-Codebase Map

This file upgrades the v2 plan with concrete codebase anchors so agents can finish each phase completely.
Use with `AGENT_EXECUTION_CONTRACT.md`.

## Global Observations
- `docs/v2_docs` contains 25 phases.
- 40 "Reference Files" entries currently point to paths that do not exist.
- Most phase goals are valid, but several assume modules (scheduler, transaction manager, capacity manager, fuzz/bench infra, deployment assets) that are not yet present.

## Missing Reference Paths (Must Be Resolved During Relevant Phases)
- `include/cynamodb/core/memory_resource.hpp`
- `src/core/memory_manager.cpp`
- `include/cynamodb/core/scheduler.hpp`
- `src/core/scheduler.cpp`
- `include/cynamodb/core/lock_free_queue.hpp`
- `include/cynamodb/engine/lsm/compaction.hpp`
- `src/engine/lsm/compaction_manager.cpp`
- `src/engine/lsm/block_cache.cpp`
- `include/cynamodb/engine/transactions/manager.hpp`
- `src/engine/transactions/manager.cpp`
- `include/cynamodb/engine/lsm/version_set.hpp`
- `include/cynamodb/engine/lsm/gsi_manager.hpp`
- `src/engine/lsm/gsi_manager.cpp`
- `include/cynamodb/engine/lsm/lsi_manager.hpp`
- `src/engine/lsm/lsi_manager.cpp`
- `src/observability/metrics.cpp`
- `src/engine/lsm/wal_manager.cpp`
- `src/engine/recovery/manager.cpp`
- `include/cynamodb/engine/capacity/manager.hpp`
- `src/engine/capacity/manager.cpp`
- `include/cynamodb/streams/shard.hpp`
- `src/engine/backup/pitr.cpp`
- `include/cynamodb/expressions/partiql_parser.hpp`
- `src/expressions/partiql_evaluator.cpp`
- `include/cynamodb/utils/chaos_engine.hpp`
- `src/utils/chaos_engine.cpp`
- `tests/test_resilience.cpp`
- `tests/fuzz_api.cpp`
- `tests/fuzz_expressions.cpp`
- `tests/fuzz_lsm.cpp`
- `tests/bench_performance.cpp`
- `scripts/run_ycsb.sh`
- `docs/v2_docs/performance_profile.md`
- `tests/sdk_compatibility/java/`
- `tests/sdk_compatibility/python/`
- `tests/sdk_compatibility/go/`
- `tests/sdk_compatibility/js/`
- `charts/cynamodb/`
- `.github/workflows/ci.yml`

## Phase Execution Anchors

### Phase 01 - Zero-Copy JSON
- Existing anchors: `include/cynamodb/json/serializer.hpp`, `src/json/serializer.cpp`, `src/http/server.cpp`.
- Key integration points: request parsing helpers in HTTP handlers, serializer error responses.
- Required tests: `tests/test_json.cpp`, `tests/test_items_http.cpp`.

### Phase 02 - Memory Management
- Existing anchors: `include/cynamodb/core/memory.hpp`, `include/cynamodb/context.hpp`.
- Missing refs to create or replace: `include/cynamodb/core/memory_resource.hpp`, `src/core/memory_manager.cpp`.
- Required tests: `tests/test_core.cpp`, `tests/test_memory_engine.cpp`.

### Phase 03 - Concurrency
- Existing anchors: `src/http/server.cpp`, `src/engine/table_manager.cpp`, `include/cynamodb/context.hpp`.
- Missing refs to create or replace: scheduler and lock-free queue headers/sources.
- Required tests: `tests/test_engine.cpp`, `tests/test_transactions_http.cpp`.

### Phase 04 - LSM Compaction
- Existing anchors: `include/cynamodb/engine/lsm/compactor.hpp`, `src/engine/lsm/lsm_engine.cpp`, `src/engine/lsm/sstable.cpp`.
- Missing refs to create or replace: `compaction.hpp`, `compaction_manager.cpp`.
- Required tests: `tests/test_lsm_primitives.cpp`, `tests/test_engine.cpp`.

### Phase 05 - SSTable Read Path
- Existing anchors: `include/cynamodb/engine/lsm/bloom_filter.hpp`, `include/cynamodb/engine/lsm/block_cache.hpp`, `src/engine/lsm/sstable.cpp`.
- Missing refs to create or replace: `src/engine/lsm/block_cache.cpp` (or keep header-only and update phase doc).
- Required tests: `tests/test_lsm_primitives.cpp`.

### Phase 06 - Expression Engine
- Existing anchors: `include/cynamodb/expressions/*.hpp`, `src/expressions/*.cpp`.
- Required tests: `tests/test_expressions.cpp`, `tests/test_items_http.cpp`.

### Phase 07 - Transactions / MVCC
- Existing anchors: `src/api/dispatcher.cpp`, `src/engine/table_manager.cpp`.
- Missing refs to create or replace: transaction manager and version-set modules.
- Required tests: `tests/test_transactions_http.cpp`, `tests/test_engine.cpp`.

### Phase 08 - Item Operations Hardening
- Existing anchors: `src/api/dispatcher.cpp`, `src/http/server.cpp`, `src/engine/table_manager.cpp`.
- Required tests: `tests/test_items_http.cpp`, `tests/test_api.cpp`.

### Phase 09 - Batch/Transact Compliance
- Existing anchors: `src/api/dispatcher.cpp`, `src/http/server.cpp`.
- Missing refs to create or replace: `src/engine/transactions/manager.cpp` if still not present.
- Required tests: `tests/test_transactions_http.cpp`, `tests/test_api.cpp`.

### Phase 10 - GSI
- Existing anchors: `include/cynamodb/core/schema.hpp`, `src/engine/table_manager.cpp`, `src/engine/lsm/lsm_engine.cpp`.
- Missing refs to create or replace: GSI manager header/source.
- Required tests: `tests/test_tables_http.cpp`, `tests/test_engine.cpp`.

### Phase 11 - LSI
- Existing anchors: `include/cynamodb/core/schema.hpp`, `src/engine/table_manager.cpp`.
- Missing refs to create or replace: LSI manager header/source.
- Required tests: `tests/test_tables_http.cpp`, `tests/test_engine.cpp`.

### Phase 12 - SigV4 Security
- Existing anchors: `include/cynamodb/auth/sigv4.hpp`, `src/auth/sigv4.cpp`, `src/http/server.cpp`.
- Required tests: `tests/test_auth.cpp`, `tests/test_http_security_standardization.cpp`.

### Phase 13 - Networking
- Existing anchors: `include/cynamodb/http/server.hpp`, `src/http/server.cpp`, `src/main.cpp`.
- Required tests: `tests/test_http_security_standardization.cpp`, HTTP-focused tests under `tests/test_*_http.cpp`.

### Phase 14 - Observability
- Existing anchors: `include/cynamodb/observability/metrics.hpp`, `include/cynamodb/context.hpp`.
- Missing refs to create or replace: `src/observability/metrics.cpp`.
- Required tests: `tests/test_metrics.cpp`.

### Phase 15 - WAL / Recovery
- Existing anchors: `include/cynamodb/engine/lsm/wal.hpp`, `src/engine/lsm/wal.cpp`, `src/engine/recovery/recovery_manager.cpp`.
- Missing refs to create or replace: `wal_manager.cpp`, `recovery/manager.cpp` (or update doc to current names).
- Required tests: `tests/test_recovery.cpp`, `tests/test_lsm_primitives.cpp`.

### Phase 16 - Capacity Management
- Existing anchors: `src/api/dispatcher.cpp`, `src/http/server.cpp`, `include/cynamodb/context.hpp`.
- Missing refs to create or replace: capacity manager header/source.
- Required tests: `tests/test_api.cpp`, `tests/test_transactions_http.cpp`.

### Phase 17 - Streams
- Existing anchors: `include/cynamodb/streams/manager.hpp`, `src/streams/manager.cpp`.
- Missing refs to create or replace: `include/cynamodb/streams/shard.hpp`.
- Required tests: `tests/test_streams_manager.cpp`, `tests/test_streams_http.cpp`.

### Phase 18 - Backup / Restore
- Existing anchors: `include/cynamodb/backups/manager.hpp`, `src/backups/manager.cpp`.
- Missing refs to create or replace: `src/engine/backup/pitr.cpp`.
- Required tests: `tests/test_backups_manager.cpp`.

### Phase 19 - PartiQL
- Existing anchors: `src/api/dispatcher.cpp`, `src/expressions/*`.
- Missing refs to create or replace: PartiQL parser/evaluator files.
- Required tests: add dedicated PartiQL tests (new) plus API coverage.

### Phase 20 - Chaos / Resilience
- Existing anchors: recovery + WAL + server paths.
- Missing refs to create or replace: chaos engine modules and resilience test file.
- Required tests: add resilience tests and include in `tests/CMakeLists.txt`.

### Phase 21 - Fuzzing
- Existing anchors: API/auth/expression/LSM code.
- Missing refs to create or replace: fuzz test files and build wiring.
- Required tests: fuzz targets + smoke corpus in CI/local script.

### Phase 22 - Benchmarking
- Existing anchors: server, dispatcher, engine modules.
- Missing refs to create or replace: benchmark files/scripts/profile doc.
- Required tests: keep `ctest` green and add benchmark command docs.

### Phase 23 - AWS SDK Compatibility
- Existing anchors: HTTP API handlers and SigV4.
- Missing refs to create or replace: SDK compatibility harness directories.
- Required tests: scripted SDK smoke runs with reproducible commands.

### Phase 24 - Deployment/CI
- Existing anchors: `CMakeLists.txt`, runtime executable `cynamodb`.
- Missing refs to create or replace: Docker/K8s/CI workflow assets.
- Required tests: fresh-build reproducibility script.

### Phase 25 - Final Audit
- Existing anchors: `README.md`, `docs/api-operations-compliance.md`, `docs/security-compliance.md`.
- Required checks: all prior phase reports exist, all contract gates pass, and unresolved missing references are zero.

## Required Tracker Updates
For each completed phase, update:
- the phase file status section (add if missing), and
- `docs/v2_docs/reports/phase_##_report.md`.

If a phase remains partial, mark exactly which gate(s) failed and why.
