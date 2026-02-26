# v2 Agent Execution Contract

## Purpose
This contract defines the minimum completion bar for every phase in `docs/v2_docs/`.
Agents must use this contract to prevent partial implementations, unverifiable performance claims, and "done" states without test evidence.

## Repository Reality Snapshot
Use this snapshot as the source of truth before implementing a phase:

- Build system: `CMakeLists.txt` (single `cynamodb_core` library and `unit_tests` target)
- Core runtime entry points:
  - `src/main.cpp`
  - `src/http/server.cpp`
  - `src/api/dispatcher.cpp`
  - `src/engine/table_manager.cpp`
- Storage engines:
  - `src/engine/memory_engine.cpp`
  - `src/engine/lsm/*.cpp`
  - `src/engine/recovery/recovery_manager.cpp`
- Security/auth:
  - `src/auth/sigv4.cpp`
- JSON/expressions:
  - `src/json/serializer.cpp`
  - `src/expressions/*.cpp`
- Streams/backups:
  - `src/streams/manager.cpp`
  - `src/backups/manager.cpp`
- Tests currently wired into CI-like flow:
  - `tests/CMakeLists.txt` -> `unit_tests`

## Required Workflow Per Phase
1. Read the phase doc and this contract.
2. Reconcile phase references with real files in the repo.
3. Create or update implementation code.
4. Add or update tests in `tests/`.
5. Run required validation commands.
6. Update phase documentation if scope changes.
7. Write a phase report under `docs/v2_docs/reports/`.

## Non-Negotiable Completion Gates
A phase is complete only if all gates pass.

### Gate 1: Reference Reconciliation
- Every file listed in "Reference Files" must be either:
  - Existing and used, or
  - Created in this phase, or
  - Replaced in the phase doc with a real path and rationale.
- Never leave stale/nonexistent references unresolved.

### Gate 2: Implementation Completeness
- Core code path is integrated end-to-end, not isolated helper-only work.
- Request path changes must include dispatcher/server wiring when applicable.
- Storage path changes must include read + write + error path behavior.

### Gate 3: Test Coverage
- Add/extend unit or integration-style tests in `tests/` for:
  - Happy path
  - Validation/error path
  - Regression for the bug/risk addressed
- Tests must prove the implemented behavior, not only compilation.

### Gate 4: Build and Test Verification
Run all commands below and capture outcomes in the phase report:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Gate 5: Security and Compliance Alignment
- If auth/request validation changed: update and/or verify behavior against `docs/security.md` and `docs/security-compliance.md`.
- If API behavior changed: verify and update `docs/api-operations-compliance.md` as needed.
- Error behavior must remain deterministic and fail closed.

### Gate 6: Performance Claim Discipline
- Do not state throughput/latency improvements without measured evidence.
- If benchmark infra does not exist yet, add it or mark the phase as "Functionally complete, perf evidence pending" with explicit follow-up task IDs.

### Gate 7: Documentation Delta
- Update the phase doc when implementation differs from original assumptions.
- Add concise notes on tradeoffs and constraints discovered in code.

### Gate 8: Evidence Report
- Add `docs/v2_docs/reports/phase_##_report.md`.
- Report must include:
  - Files changed
  - Tests added/updated
  - Command outputs summary
  - Remaining risks
  - Explicit "Complete" or "Partial" status

## Standard Phase Report Template
Create one file per phase using this structure:

```md
# Phase ## Report

## Status
Complete | Partial

## Scope Delivered
- ...

## Files Changed
- ...

## Tests Added/Updated
- ...

## Validation Run
- `cmake -S . -B build` -> PASS/FAIL
- `cmake --build build -j` -> PASS/FAIL
- `ctest --test-dir build --output-on-failure` -> PASS/FAIL

## Compliance Impact
- `docs/api-operations-compliance.md`: updated/not needed
- `docs/security-compliance.md`: updated/not needed

## Performance Evidence
- benchmark file/results link or "pending"

## Residual Risks
- ...
```

## Phase Dependency Rules
- Do not start phases 08+ before phase 01-07 core infrastructure is at least partial and testable.
- Do not start phase 23 (SDK compatibility) before the target APIs are implemented and validated in `tests/test_*_http.cpp`.
- Do not mark phase 25 complete until every prior phase has a report file.

## Missing Reference Policy (Important for Current v2 Docs)
Current phase docs include references to files that are not in the repository yet.
Agents must do one of the following in each affected phase:

1. Create the referenced files and wire them into `CMakeLists.txt`, or
2. Replace references with existing equivalents and document why.

Never silently skip these gaps.

## Minimum Targeted Test Mapping
Use at least one relevant test file from this list when touching these areas:

- API/dispatcher: `tests/test_api.cpp`, `tests/test_items_http.cpp`, `tests/test_tables_http.cpp`, `tests/test_transactions_http.cpp`
- Auth/security/http validation: `tests/test_auth.cpp`, `tests/test_http_security_standardization.cpp`
- JSON: `tests/test_json.cpp`
- Expressions: `tests/test_expressions.cpp`
- LSM/storage primitives: `tests/test_lsm_primitives.cpp`, `tests/test_engine.cpp`, `tests/test_recovery.cpp`
- Streams/backups: `tests/test_streams_http.cpp`, `tests/test_streams_manager.cpp`, `tests/test_backups_manager.cpp`

## Definition of Done (Final)
A phase is "Done" only when:
- All 8 gates pass
- A phase report exists
- No unresolved stale references remain in that phase doc
- The global build/test commands pass
