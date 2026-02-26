# Phase 01: Zero-Copy JSON & Serialization Optimization

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
In a high-performance database like CynamoDB, the overhead of JSON parsing and serialization can account for up to 30-40% of total request latency. This phase transitions the engine from standard JSON processing to a zero-copy, SIMD-accelerated architecture. We will leverage `simdjson` for parsing and implement a custom, allocation-free serialization engine using C++23 features like `std::span` and `std::string_view`.

## Technical Definition
*   **Zero-Copy Parsing:** Use `simdjson::ondemand` to access fields directly from the input buffer without intermediate object creation.
*   **Allocation-Free Serialization:** Implement a buffer-pooling mechanism and `std::format`-based serialization that writes directly into network-ready buffers.
*   **C++23 Integration:** Utilize `std::expected` for error handling in the serialization path and `std::span` for managing contiguous memory segments.

## Reference Files
*   `include/cynamodb/json/serializer.hpp`
*   `src/json/serializer.cpp`
*   `include/cynamodb/http/server.hpp`
*   `src/http/server.cpp`

## Expanded Tasks
1.  **SIMD Integration:** Upgrade `simdjson` to v3.10.0+ in `external/`. Modify `CMakeLists.txt` to enable `SIMDJSON_IMPLEMENTATION_HASWELL` and `SIMDJSON_IMPLEMENTATION_ARM64`. Verify via `simdjson::get_active_implementation()->name()` during engine startup.
2.  **On-Demand Parser Setup:** In `src/json/serializer.cpp`, instantiate a persistent `simdjson::ondemand::parser` as a `thread_local` object to reuse its internal 1MB+ buffers. Ensure it handles UTF-8 validation as per DynamoDB specs.
3.  **Buffer Padding:** Modify the HTTP server's read buffer allocation in `src/http/server.cpp` to include `simdjson::SIMDJSON_PADDING` extra bytes. Use `std::make_unique_for_overwrite<char[]>` to avoid zero-init costs for these large buffers.
4.  **StringView Mapping:** Create a utility `to_view(simdjson::ondemand::value)` that returns a `std::string_view`. Ensure this view points directly into the raw request buffer. Handle escape sequences by optionally using a small thread-local "unscape" buffer only when necessary.
5.  **Schema-Aware Parsing:** Implement `parse_put_item(std::string_view json)` using a `simdjson::ondemand::object`. Use a fixed-order field access pattern if possible, but fallback to `find_field()` for strict DynamoDB compliance where field order is non-deterministic.
6.  **Avoid Intermediate Maps:** Replace `std::map<std::string, AttributeValue>` with `std::vector<std::pair<std::string_view, AttributeValue>>` for request parsing. Use `std::sort` and `std::lower_bound` for lookups, ensuring O(log N) access with zero heap allocations for keys.
7.  **Custom Serializer Interface:** Define `class JsonWriter` in `serializer.hpp`. It must wrap a `std::span<char>` and maintain a `size_t offset`. All methods must be `noexcept` and return `std::expected<void, SerializationError>` if the span is exceeded.
8.  **Integer-to-String Optimization:** Use `std::to_chars` from `<charconv>` within `JsonWriter::write_number()`. Avoid `std::to_string` and `sprintf` entirely. Target < 20ns for 64-bit integer conversion.
9.  **Date/Time Serialization:** Use `std::chrono::format` with `"{:%FT%T%z}"` for ISO 8601 strings. If C++23 `<chrono>` support is incomplete in the compiler, implement a specialized `write_iso8601()` that writes directly to the `JsonWriter` buffer.
10. **Escaping Logic:** Implement `write_escaped_string()` using a lookup table for the first 128 ASCII characters. For multi-byte UTF-8, pass through raw bytes unless they contain characters required to be escaped by JSON spec (e.g., control chars).
11. **Buffer Pooling:** Implement `BufferPool` in `src/utils/buffer_pool.cpp` using a `std::vector<std::span<char>>`. Each thread should have a local pool of 64KB, 256KB, and 1MB buffers to serve as the `JsonWriter` backings.
12. **Vectorized Boolean Parsing:** For `BatchGetItem` results, use `simdjson`'s `get_bool()` which internally uses SIMD bitmasks. Ensure 'NULL' values are detected using the same high-speed path.
13. **AttributeValue Serialization:** Overload `JsonWriter::write(const AttributeValue&)` to use a visitor pattern (`std::visit`). Ensure that for 'S' (String) types, we write the opening quote, the escaped content, and the closing quote in a single contiguous operation if possible.
14. **Error Path Optimization:** Define `enum class SerializationError { BufferFull, InvalidType, DepthLimitExceeded }`. Use `[[nodiscard]]` on all serialization functions to ensure error checking doesn't impact the happy path.
15. **Compile-Time Key Hashing:** Use a `constexpr` FNV-1a hash function for JSON keys like `"Item"`, `"TableName"`, `"Key"`. Use these hashes in a `switch` statement inside the dispatcher to eliminate `std::string` comparisons.
16. **SIMD-Accelerated List Parsing:** When parsing `L` (List) types, use `simdjson::ondemand::array::count_elements()` which uses SIMD to find commas and brackets. Pre-reserve the target `std::vector` size based on this count.
17. **No-Copy Binary Data:** Implement a Base64 decoder that takes a `std::string_view` and writes directly into a `std::span<uint8_t>`. Use SIMD-accelerated Base64 (e.g., from `fast_base64` or equivalent) to target > 1GB/sec decoding.
18. **Memory Alignment:** Use `std::aligned_alloc` for request and response buffers, ensuring 64-byte alignment. This prevents cache line splits when SIMD registers cross 64-byte boundaries.
19. **Response Header Pre-Serialization:** Pre-serialize common headers like `HTTP/1.1 200 OK\r\nContent-Type: application/x-amz-json-1.0\r\n` into a `static constexpr std::string_view`. Use `writev` to send this followed by the body.
20. **Lazy Field Access:** In `GetItem` requests, only extract the `Key` object initially. Only parse `ProjectionExpression` and other fields if the initial key lookup succeeds in the storage engine.
21. **Benchmark Baseline:** Use `google/benchmark`. Measure `JsonParser::ParsePutItem` with a 10-attribute item. Target: 2.0 microseconds. Compare against `nlohmann::json`.
22. **Validation - Type Safety:** Ensure that if a field is tagged 'N' but contains non-numeric characters (excluding decimal point and scientific notation), a `SerializationError::InvalidType` is returned.
23. **Validation - Nesting Depth:** Add an `int depth` counter to `JsonWriter`. If `depth > 32`, return an error. This prevents stack exhaustion during recursive serialization of nested Maps and Lists.
24. **Validation - Required Fields:** Implement a bitmask (e.g., `uint32_t fields_present`) during parsing. At the end of parsing, check if `(fields_present & REQUIRED_MASK) == REQUIRED_MASK`.
25. **Integration Test:** In `tests/test_json.cpp`, add a "Round-trip" test: Parse JSON -> Internal Object -> Serialize -> Compare with Original (ignoring whitespace).

## Validation Criteria
*   **Latency:** Parsing and serialization of a 1KB PutItem request must be under 5 microseconds on a modern CPU.
*   **Memory:** Zero heap allocations during the parsing/serialization of a standard GetItem request (excluding the final response buffer).
*   **Correctness:** Full compliance with `api-operations-compliance.md` for JSON response structure.
