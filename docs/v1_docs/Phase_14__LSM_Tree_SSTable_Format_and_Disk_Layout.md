# Phase 14: LSM Tree SSTable Format and Disk Layout

**Target Goal from PRD:** Implement SSTables format, prefix compression while maintaining 1:1 API compatibility with AWS DynamoDB, focusing on high-performance C++23 standards, zero-cost abstractions, and strict typing.

**Instructions for AI Coding Agent:**
- Reference PRD Section 1 & 2 for strict API compatibility guarantees.
- Ensure all variables and functions are precisely typed (e.g., utilize `std::variant`, concepts, and robust constraints) instead of loose generic pointers (e.g., no `void*` or untyped `std::any` without strict type checks), reflecting our codebase's strict stance on types (similar to our avoidance of `any`/`unknown` in TypeScript).
- Produce idempotent build targets, complete memory management models using Arena allocators where necessary, and high-performance determinism.

## Phase Tasks (Detailed Step-by-Step)

1. **Analyze PRD Requirements**: Review PRD Section 'Feature Scope' and 'System Requirements' relevant to SSTables format, prefix compression. Ensure the architectural blueprint exactly aligns with '1:1 API compatibility'. Document the precise JSON schema AWS expects for this feature.

2. **Strict Typing Definition**: Define the precise C++23 structs/classes for SSTables format, prefix compression. Enforce exact types using `std::variant` or `std::expected` for error handling, avoiding ambiguous typeless structures entirely as mandated by global context.

3. **Memory Allocation Strategy**: Design the memory management blueprint using Arena allocators or slab pools for SSTables format, prefix compression to satisfy the PRD goal of 'Memory Efficiency: Lock-free reads'.

4. **Header Scaffolding**: Create the `.hpp` files for SSTables format, prefix compression, embedding detailed doxygen comments that map directly to the DynamoDB AWS SDK behavior.

5. **Interface Definition**: Define abstract interfaces (where appropriate) ensuring pluggability (PRD Section 6.1) for SSTables format, prefix compression.

6. **Dependency Integration**: Configure CMake targets to explicitly link required libraries (e.g., simdjson, Boost.Beast) required for SSTables format, prefix compression.

7. **Mock Implementation**: Create a stubbed version of the API/engine component returning dummy data that matches the DynamoDB deterministic error codes/success shapes.

8. **SDK Compatibility Test Setup**: Write a test harness script that points the official AWS C++ or Python SDK to `localhost:8000` to verify the stub for SSTables format, prefix compression.

9. **Data Model Mapping**: Implement the internal translation layer converting AWS JSON representations of SSTables format, prefix compression into the native C++23 strict types.

10. **Core Logic Implementation (Step 1)**: Begin implementation of SSTables format, prefix compression. Focus on zero-copy concepts and memory locality.

11. **Core Logic Implementation (Step 2)**: Integrate SSTables format, prefix compression with the internal Transaction Layer and API Dispatcher.

12. **Concurrency Validation**: Ensure the implementation is thread-safe using C++20/23 concurrency primitives (`std::jthread`, `std::atomic`). Ensure 'lock-free reads' are preserved per PRD Section 6.3.

13. **Error Handling Alignment**: Map every internal C++ exception in SSTables format, prefix compression to exact DynamoDB errors (e.g., `ValidationException`, `ProvisionedThroughputExceededException`).

14. **Metrics Instrumentation**: Add OpenTelemetry/Prometheus counters for latency and throughput tracking inside the SSTables format, prefix compression code path (PRD Section 6.11).

15. **Logging Integration**: Inject structured JSON logging into the SSTables format, prefix compression modules to enable deterministic trace debugging.

16. **Edge Case Implementation (Missing Fields)**: Ensure that null or omitted fields in the request payload for SSTables format, prefix compression are handled exactly as DynamoDB handles them.

17. **Edge Case Implementation (Type Mismatches)**: Validate that passing a Number where a String is expected gracefully returns the identical AWS `ValidationException` format.

18. **Throughput Profiling**: Run micro-benchmarks (Google Benchmark) on SSTables format, prefix compression to ensure it adheres to the '< 1ms read, < 2ms write' PRD targets.

19. **Memory Leak Analysis**: Run Valgrind or AddressSanitizer on the SSTables format, prefix compression test suite to guarantee zero leaks.

20. **SIMD Optimization Analysis**: Review the SSTables format, prefix compression parsing/processing loops to see if SIMD intrinsics can be applied for higher ops/sec.

21. **Unit Test Suite Generation**: Write comprehensive Catch2/GTest unit tests for SSTables format, prefix compression, covering 100% of branch logic.

22. **Integration Test Suite**: Add a Docker-compose based integration test verifying SSTables format, prefix compression interacts correctly with the rest of cynamoDB.

23. **Fuzz Testing**: Write a LibFuzzer target for SSTables format, prefix compression feeding it malformed AWS SigV4/JSON requests to ensure it never segfaults (Crash Recovery PRD requirement).

24. **Local Development Documentation**: Update the developer `README.md` and module-level docs explaining how to build and test SSTables format, prefix compression.

25. **Final Review against PRD**: Conduct a final checklist review of the completed SSTables format, prefix compression module against PRD sections 3 (API), 4 (Requirements), and 6 (Spec). Ensure zero deviation.

26. **Code Formatting & Linting**: Run `clang-format` and `clang-tidy` to ensure strict C++23 style adherence across all SSTables format, prefix compression source files.

27. **Merge Preparation**: Squash commits, verify all CI checks pass, and prepare the patch for SSTables format, prefix compression deployment to the main branch.
