# Phase 20: Resilience: Chaos Testing & Fault Injection

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
A production database must be resilient to hardware failures, network issues, and software bugs. This phase introduces a "Chaos Engine" that injects failures into the system during stress tests to verify that the database remains stable, consistent, and eventually recovers.

## Technical Definition
*   **Fault Injection:** Intentionally introducing errors (disk full, latency, bit-flips) into the storage and networking layers.
*   **Chaos Monkey:** A background worker that randomly kills threads or components during high-load tests.
*   **Jepsen-style Testing:** Verifying linearizability and consistency under partition and crash scenarios.

## Reference Files
*   `include/cynamodb/utils/chaos_engine.hpp`
*   `src/utils/chaos_engine.cpp`
*   `tests/test_resilience.cpp`

## Expanded Tasks
1.  **Chaos Engine Setup:** Implement `ChaosEngine::inject(fault_type)`. Use an internal `std::map<FaultType, float>` to store the probability of each fault occurring.
2.  **I/O Error Injection:** In the `FileSystem` wrapper, add `if (ChaosEngine::should_fail(IO_WRITE)) return -EIO;`. This must be able to target specific files like the `Manifest` or the `WAL`.
3.  **Latency Injection:** In the `Scheduler`, add `if (ChaosEngine::should_delay(TASK_EXEC)) std::this_thread::sleep_for(rand_ms);`. Use this to simulate slow disks or overloaded CPUs.
4.  **Network Split Simulation:** In `server.cpp`, implement `ChaosEngine::should_drop_connection()`. If true, the server `close()`es the socket immediately after accepting it, without sending any response.
5.  **Memory Pressure Injection:** Implement `MemoryGuzzler`. It allocates 1GB blocks of memory and `mlock`s them to force the OS to reclaim memory from the CynamoDB process, triggering page faults.
6.  **Bit-Flip Simulator:** Implement `corrupt_buffer(span)`. It flips a random bit in the buffer before it's passed to `write()`. This tests the checksum logic in the WAL and SSTable readers.
7.  **Clock Skew Simulation:** Implement `get_skewed_time()`. Every call returns `real_time + ChaosEngine::current_skew`. Randomly adjust the skew by +/- 10 seconds during the test.
8.  **Thread Killer:** Create a `ThreadMonkey`. It periodically calls `pthread_cancel` or sends a signal to random background threads (Compactor, GSI worker) to ensure they are restarted by their managers.
9.  **SIGKILL Resilience Test:** Create a script `chaos_kill_restart.sh`. It runs `wrk` in the background and sends `kill -9` to the `cynamodb` process at random intervals between 1s and 300s.
10. **Partial Flush Simulation:** In `SSTableWriter`, if chaos is enabled, stop writing in the middle of a block and close the file. Verify that the `Manifest` rejects this SSTable during the next startup.
11. **Compaction Interference:** Inject a "Disk Full" error exactly when the compactor is attempting to commit its result to the `Manifest`. Verify that no SSTables are leaked.
12. **GSI Lag Injection:** Artificially block the `GSIPropagationWorker` for 30 seconds. Verify that GSI queries return 400 or stale data, and then eventually return correct data once the worker is unblocked.
13. **Transaction Conflict Injection:** Create a `ConflictMonkey`. It identifies which items a transaction is about to write and spawns a "Racer" thread that tries to update those same items simultaneously.
14. **Corrupt Manifest Test:** Create a test that truncates the `Manifest` file to a random size. Verify the engine detects the corruption via the manifest checksum and attempts to recover from `manifest.bak`.
15. **Resource Exhaustion Test:** Use `cgroups` (Linux) to limit the process to 10% CPU and 128MB RAM. Run a heavy `Scan` and verify the process doesn't OOM.
16. **Slow Disk Simulation:** Use `pv -L 1M` in a pipe for WAL writes (or an equivalent software throttle) to simulate an EBS volume that has run out of IOPS burst credits.
17. **Jepsen-style Consistency Checker:** Implement `LinearizabilityChecker`. It records the start/end time and result of every operation and then verifies if there exists a valid sequential history that explains the results.
18. **Deadlock Detector Validation:** Intentionally create a circular dependency between two `StripedLock` mutexes. Verify that the `Scheduler` logs a "Possible Deadlock Detected" message within 30s.
19. **Cleanup Verification:** After a chaos run, use `find . -name "*.tmp"` and `find . -name "*.wal"`. Verify that all temporary files were cleaned up and all WALs belong to active table versions.
20. **Retry Logic Validation:** If the `WALWriter` gets an `EAGAIN` or `EINTR`, verify it correctly retries the operation without failing the request.
21. **Recovery Time Objective (RTO) Test:** Record the time from process start to "HTTP Server Ready". For a 100GB database with 1GB of WAL, this must be < 5 seconds.
22. **Recovery Point Objective (RPO) Test:** After a `SIGKILL`, verify that every `PutItem` that received a `200 OK` response is actually present in the database.
23. **Stress-Chaos-Mix:** Run the "YCSB-A" workload for 4 hours with 10% fault injection probability. The "Success Rate" must be > 95% and the "Data Integrity" must be 100%.
24. **Chaos Metrics:** Track `Metrics::CHAOS_FAULTS_INJECTED` and `Metrics::UNEXPECTED_PROCESS_EXITS`.
25. **Validation:** Verify that the `ChaosEngine` can produce a "Repeatable Seed" so that a failed chaos run can be reproduced exactly for debugging.

## Validation Criteria
*   **Stability:** The process never crashes due to an injected I/O or memory error (it should return a 5xx error instead).
*   **Consistency:** Data integrity is maintained 100% of the time despite bit-flips and partial writes (verified via checksums).
*   **Recovery:** The database always returns to a clean state after a restart, regardless of how it was terminated.
