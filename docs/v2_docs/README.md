# v2 Plan Index (Agent-Ready)

This directory contains the v2 implementation phases.
Each phase now has a strict completion contract to ensure work is fully implemented and verified.

## Start Here
1. Read [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md).
2. Read [`PHASE_CODEBASE_MAP.md`](PHASE_CODEBASE_MAP.md).
3. Execute one phase at a time in order.
4. Create a report from [`reports/REPORT_TEMPLATE.md`](reports/REPORT_TEMPLATE.md) after each phase.

## Why This Matters
The original phase docs contain ambitious targets, but several file references and infrastructure assumptions do not match the current repository state.
The contract + map documents close that gap and define what "complete" means.

## Canonical Validation Commands
Run these at minimum for every phase:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Phase Files
- `Phase_01__Zero_Copy_JSON_Optimization.md`
- `Phase_02__Advanced_Memory_Management.md`
- `Phase_03__High_Performance_Concurrency.md`
- `Phase_04__LSM_Compaction_Optimization.md`
- `Phase_05__SSTable_Read_Optimization.md`
- `Phase_06__Expression_Engine_Optimization.md`
- `Phase_07__Transaction_Layer_MVCC.md`
- `Phase_08__Item_Operations_Hardening.md`
- `Phase_09__Batch_Transact_Compliance.md`
- `Phase_10__GSI_Propagation_Consistency.md`
- `Phase_11__LSI_Projection_Optimization.md`
- `Phase_12__SigV4_Security_Optimization.md`
- `Phase_13__High_Throughput_Networking.md`
- `Phase_14__Zero_Overhead_Observability.md`
- `Phase_15__WAL_Durability_Recovery.md`
- `Phase_16__Capacity_Management.md`
- `Phase_17__Streams_Engine_Implementation.md`
- `Phase_18__Backup_Restore_Engine.md`
- `Phase_19__PartiQL_Query_Engine.md`
- `Phase_20__Chaos_Testing_Resilience.md`
- `Phase_21__API_Storage_Fuzzing.md`
- `Phase_22__Benchmarking_Performance_Tuning.md`
- `Phase_23__AWS_SDK_Compatibility.md`
- `Phase_24__Deployment_Infrastructure.md`
- `Phase_25__Final_Production_Audit.md`
