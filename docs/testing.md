# Testing Strategy

cynamoDB employs a multi-layered testing strategy to ensure high reliability and DynamoDB compatibility.

## Test Types

### 1. Unit Tests
Located in the `tests/` directory and written using **Catch2**. These tests validate individual components like the expression parser, JSON serializer, and LSM tree primitives.
- **Run Commands**:
  ```bash
  cd build && ctest -R "^test_"
  ```

### 2. Integration Tests
These tests simulate end-to-end API calls (HTTP requests to the server) and verify the responses against expected DynamoDB behavior.
- **Run Commands**:
  ```bash
  cd build && ./test_api
  ```

### 3. Fuzz Testing
cynamoDB includes fuzzing targets for critical components like the JSON parser and expression evaluator to identify edge cases and memory safety issues.
- **Run Commands** (requires `-DENABLE_FUZZING=ON`):
  ```bash
  ./build/fuzz_json
  ./build/fuzz_expressions
  ```

### 4. SDK Compatibility Tests
Under `tests/sdk_compatibility`, we maintain scripts that run standard AWS SDK test suites against a local cynamoDB instance.

## Performance Benchmarking
A dedicated benchmark tool `bench_performance` measures throughput (items/sec) and latency for various operations.
- **Run Command**:
  ```bash
  ./build/bench_performance --threads 8 --duration 60
  ```

## Adding New Tests
When contributing new features, please add corresponding tests in the `tests/` directory. Use the existing `test_*.cpp` files as templates.

**Test Policy**:
- All new API operations must have at least one positive and one negative integration test.
- Any change to the expression engine requires updated unit tests in `test_expressions.cpp`.
