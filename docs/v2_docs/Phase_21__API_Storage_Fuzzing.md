# Phase 21: Fuzzing: API & Storage Engine Hardening

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
Traditional tests often miss edge cases in complex logic like expression parsing, JSON deserialization, and LSM-Tree merging. Fuzzing (randomized input testing) is the most effective way to find crashes, memory leaks, and logic errors. This phase implements a comprehensive fuzzing suite for all critical components using `libFuzzer` or `AFL++`.

## Technical Definition
*   **Differential Fuzzing:** Compare the output of CynamoDB against a "Ground Truth" (e.g., a simple in-memory map or a real DynamoDB Local instance).
*   **AFL++ / libFuzzer Integration:** Use coverage-guided fuzzing to explore all code paths.
*   **Invariant Checking:** Define properties (e.g., "A successful PutItem must result in a successful GetItem") that are checked during every fuzzer iteration.

## Reference Files
*   `tests/fuzz_api.cpp`
*   `tests/fuzz_expressions.cpp`
*   `tests/fuzz_lsm.cpp`

## Expanded Tasks
1.  **Fuzzing Framework Setup:** Add `fuzzing.cmake`. Define `add_fuzz_target(name source)`. Ensure it links with `-fsanitize=fuzzer,address,undefined` for maximum error detection.
2.  **JSON Parser Fuzzer:** Implement `fuzz_json_parser`. It takes a `uint8_t* data, size_t size` from the fuzzer and passes it to `simdjson::ondemand::parser`. Ensure no crashes on deeply nested or invalid UTF-8 strings.
3.  **Expression Parser Fuzzer:** Implement `fuzz_expression_parser`. The fuzzer generates random strings of DynamoDB expression syntax. Verify that the parser either returns a valid AST or a graceful error, but never crashes.
4.  **Expression Evaluator Fuzzer:** Use a fixed schema and random item data. The fuzzer generates a valid expression and evaluates it. Verify the result is always a valid `AttributeValue` or a boolean.
5.  **SigV4 Header Fuzzer:** Implement `fuzz_sigv4_parser`. Pass random bytes as the `Authorization` header. Ensure the parser correctly rejects malformed formats without buffer overreads.
6.  **LSM-Tree Key Fuzzer:** Implement `fuzz_key_comparison`. Generate two random DynamoDB Keys (Partition + Sort). Compare them using the internal logic and verify that the comparison is "Transitive" (if A < B and B < C, then A < C).
7.  **SSTable Format Fuzzer:** Create a fuzzer that takes a random buffer and attempts to initialize a `SSTableReader` from it. This ensures that corrupted or malicious SSTable files don't crash the engine.
8.  **Differential Fuzzer - Item Ops:** Implement `FuzzState`. For every fuzzer step: 1. Pick a random operation (Put, Get, Update, Delete). 2. Apply to CynamoDB. 3. Apply to `std::map<Key, Value>`. 4. Verify results are identical.
9.  **Differential Fuzzer - Query/Scan:** Generate a random set of 100 items. Generate a random `FilterExpression`. Compare the list of items returned by CynamoDB vs a simple `std::copy_if` over the 100 items.
10. **Transaction Fuzzer:** The fuzzer generates a sequence of `TransactWriteItems`. Verify that after every transaction, the state is consistent (e.g., if a transaction was to increment 5 counters, either all are incremented or none).
11. **GSI Propagation Fuzzer:** Perform 1000 random writes to the base table. Wait for propagation. Verify that a `Scan` of the GSI returns the same number of items and the same data as a `Scan` of the base table.
12. **Streams Fuzzer:** Perform a random sequence of operations. Verify that the `StreamRecords` generated have monotonically increasing `SequenceNumbers` and correctly reflect the change.
13. **PartiQL Grammar Fuzzer:** Feed random strings to the `PartiQLParser`. Use a "Dictionary" of SQL keywords to help the fuzzer generate semi-valid SQL more frequently.
14. **PartiQL Execution Fuzzer:** Compare `ExecuteStatement("SELECT * FROM ...")` against a direct `Scan` with the same filter. Results must be bit-for-bit identical.
15. **Memory Manager Fuzzer:** Generate a random sequence of `allocate(size)` and `deallocate(ptr)` calls. Verify that the `GlobalMemoryResource` correctly tracks usage and detects leaks at the end of the fuzzer run.
16. **Scheduler Fuzzer:** Generate random `Task` graphs with random dependencies. Verify the scheduler completes all tasks without deadlock and respects the dependency order.
17. **Buffer Pool Fuzzer:** Randomly `acquire()` and `release()` buffers of random sizes. Verify that the same buffer is never issued to two threads simultaneously.
18. **Address Sanitizer (ASAN) Integration:** Build the fuzzers with `-fsanitize-address-use-after-scope` to catch subtle stack-use-after-scope bugs in the C++23 code.
19. **Thread Sanitizer (TSAN) Integration:** Run a specialized fuzzer that launches 10 threads, all performing random operations on the same table, to find rare race conditions.
20. **Fuzzing "Seed" Generation:** Create a `corpus/` directory with 100+ valid DynamoDB JSON requests collected from the unit tests.
21. **Hang/Timeout Detection:** Set `LIB_FUZZER_OPTIONS="-timeout=5"`. Any input that takes more than 5 seconds to process is treated as a bug (likely an infinite loop or ReDoS).
22. **Crash Minimization:** Implement a script `minimize_crash.sh` that uses `AFL_TMIN` to reduce a crashing input to the minimum set of bytes required to trigger the bug.
23. **Continuous Fuzzing (CI):** Add a GitHub Action that runs `libFuzzer` for 10 minutes on every pull request, focusing on the files modified in that PR.
24. **Long-Running Fuzzing (Nightly):** Set up a dedicated machine (or a cloud instance) to run the `lsm_fuzzer` and `transaction_fuzzer` for 24 hours every weekend.
25. **Validation:** Review the "Code Coverage" report generated by `llvm-cov`. Ensure that the fuzzers hit at least 80% of the lines in `src/engine/` and `src/expressions/`.

## Validation Criteria
*   **Security:** No buffer overflows or memory safety violations detected.
*   **Correctness:** Differential fuzzing shows 100% agreement with the reference implementation for 10M operations.
*   **Stability:** Zero crashes detected by the nightly long-running fuzzing suite.
