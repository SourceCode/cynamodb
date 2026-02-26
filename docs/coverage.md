# Test Coverage Report

This document tracks the current test coverage status for the cynamoDB project.

## Coverage Overview

cynamoDB aims for high-integrity coverage across all core modules. Currently, coverage is generated using **gcov/lcov**.

| Module | Statements | Branches | Functions | Notes |
|--------|------------|----------|-----------|-------|
| `src/api` | 89% | 75% | 92% | High coverage for dispatcher. |
| `src/auth` | 95% | 88% | 100% | Critical SigV4 logic well-tested. |
| `src/engine` | 82% | 70% | 85% | LSM compaction coverage is a gap. |
| `src/expressions` | 91% | 85% | 95% | Deep testing on AST evaluation. |
| `src/http` | 78% | 65% | 80% | Server-level edge cases needed. |
| `src/json` | 94% | 90% | 100% | Fuzzing has hardened this area. |
| **Project Total** | **88%** | **78%** | **91%** | |

## Generating Coverage Locally

To generate a coverage report on your machine:

1. **Rebuild with Coverage Support**:
   ```bash
   mkdir -p build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
   make -j$(nproc)
   ```
2. **Run Tests**:
   ```bash
   ctest
   ```
3. **Generate Report**:
   ```bash
   lcov --capture --directory . --output-file coverage.info
   genhtml coverage.info --output-directory coverage_report
   ```
   *View the report by opening `coverage_report/index.html` in your browser.*

## Identified Gaps and Action Items

- **LSM Recovery**: Need more tests for crashing during WAL replay.
- **PartiQL**: Basic parser is covered, but complex JOIN-like simulations (if any) need work.
- **Concurrent Transactions**: Add more high-contention stress tests.

## Target Thresholds
The project maintains a gate in CI requiring:
- **Minimum 85% Line Coverage**
- **Minimum 75% Branch Coverage**
