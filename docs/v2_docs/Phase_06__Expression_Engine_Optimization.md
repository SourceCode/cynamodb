# Phase 06: Expression Engine: AST Optimization & Vectorized Evaluation

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
DynamoDB expressions (FilterExpressions, ConditionExpressions, UpdateExpressions) are often complex. A naive tree-walk interpreter is slow and causes frequent cache misses. This phase optimizes the expression engine by introducing an optimized AST (Abstract Syntax Tree), constant folding, and vectorized evaluation for bulk operations like `Scan`.

## Technical Definition
*   **Constant Folding:** Evaluate expressions like `5 + 2` at parse time.
*   **Vectorized Evaluation:** Evaluate the same expression against multiple items in a single pass to improve cache locality.
*   **Bytecode Interpreter:** (Optional/Advanced) Compile the AST into a flat array of instructions to minimize pointer chasing.

## Reference Files
*   `include/cynamodb/expressions/evaluator.hpp`
*   `src/expressions/evaluator.cpp`
*   `src/expressions/parser.cpp`

## Expanded Tasks
1.  **AST Node Refactoring:** Define `struct ASTNode` as a `std::variant` of fixed-size POD (Plain Old Data) structs. Store all nodes in a `std::pmr::vector<ASTNode>` within the `Expression` object. This eliminates pointer chasing by making the tree contiguous.
2.  **Constant Folding:** In `parser.cpp`, implement `fold_constants(node)`. If a node is an operator (e.g., `+`) and both children are `Literal` nodes, replace the operator node with a new `Literal` containing the result.
3.  **Type Inference:** Implement `infer_type(node)`. Assign a `ResultType` (String, Number, Bool, Unknown) to every node. If a `Comparison` node has children with incompatible types, throw a `ValidationException` during parsing.
4.  **Short-Circuit Evaluation:** Refactor `evaluate_logical_and(node)`. If the left child returns `false`, skip the right child. For `OR`, if the left child returns `true`, skip the right child. Use `[[likely]]`/`[[unlikely]]` attributes.
5.  **Vectorized Scan Filter:** Implement `evaluate_filter_batch(items, bitset)`. Instead of `for (item : items) eval(expr, item)`, use `expr.eval_vectorized(items, bitset)`. This allows the CPU to keep the AST in the L1 cache while processing thousands of items.
6.  **SIMD Comparison:** For numeric comparisons (e.g., `Price > 100`), if the `items` are stored in a columnar format (or a contiguous array of numbers), use `_mm256_cmp_ps` (AVX) to compare 8 prices at once.
7.  **Attribute Access Optimization:** Pre-parse the expression to find all used attribute names. Map these names to internal `AttributeID` (integers) at the start of evaluation. This replaces expensive `std::string` lookups with simple array indexing.
8.  **String Search Acceleration:** For the `contains(attr, :substring)` function, use the `std::string_view::find` method. For very long strings, implement a SIMD-accelerated `memmem` to find the substring in a single pass.
9.  **Function Dispatch Table:** Replace the large `switch-case` or `if-else` in `evaluator.cpp` with a `std::array<EvalFunc, MAX_FUNC_ID>` dispatch table. This reduces branch mispredictions during expression execution.
10. **Pre-Compiled Condition Check:** For high-frequency `PutItem` operations, cache the `ConditionExpression` AST. Use a `std::hash` of the expression string as the cache key.
11. **UpdateExpression Optimizer:** Combine multiple `SET` and `REMOVE` actions into a single pass. For example, if updating 5 fields in a Map, traverse the Map once and apply all 5 changes instead of 5 separate traversals.
12. **Projection Expression Pruning:** Update the `SSTableReader` to accept a `std::vector<AttributeID>`. The reader will only decompress and deserialize the blocks containing these specific attributes, saving CPU and I/O.
13. **Expression Cache:** Implement `thread_local LRUCache<string, CompiledExpression>`. Use a capacity of 1024 expressions. This eliminates the 100-500 microsecond parsing cost for repeated queries.
14. **Deep-Nesting Protection:** In the parser, increment a `depth` counter for every nested expression. If `depth > 50`, reject the request with a `ValidationException`. This prevents stack overflow during both parsing and evaluation.
15. **Error Reporting:** When a `ParserError` occurs, return a JSON body: `{"message": "Syntax error at line 1, col 15: expected ')'", "code": "InvalidParameterValue"}` to match DynamoDB behavior.
16. **Null/Missing Value Optimization:** Implement a "Presence Bitmask" for every item. The evaluator can then check if an attribute exists by a single bitwise `AND` before attempting to access the data.
17. **Expression Engine Fuzzing:** In `tests/fuzz_expressions.cpp`, use `libFuzzer` to generate random strings. Feed them to the parser and evaluator. Target 100% code coverage and zero crashes over 1 million iterations.
18. **PMR Integration:** Use `std::pmr::monotonic_buffer_resource` for all temporary values generated during evaluation (e.g., results of string concatenations). Release the memory only once at the end of the entire batch scan.
19. **Boolean Simplification:** Implement a basic optimizer that converts `NOT (A > B)` to `A <= B`. This simplifies the AST and reduces the number of nodes the evaluator must visit.
20. **Map/List Indexing Performance:** For paths like `Map.List[5]`, pre-calculate the list index as an integer during parsing. Don't re-parse the string `"5"` into an integer during every evaluation.
21. **Benchmark Suite:** Create `bench_expressions.cpp`. Benchmark a filter with 5 conditions against 100,000 items. Compare the "Tree-Walk" approach vs the "Vectorized" approach. Target a 5x improvement.
22. **Compliance - Function Semantics:** Ensure `size()` correctly handles UTF-8 characters for Strings (returning number of bytes, not chars, as per DDB) and correctly counts elements for Sets and Maps.
23. **Compliance - In-place updates:** For `UpdateItem`, ensure that if the target attribute is missing, it is created, unless the operation is a `REMOVE` or a `DELETE` on a missing attribute (which is a no-op).
24. **Trace Logging:** Use `CYNAMO_LOG_TRACE` to log the AST structure and the intermediate result of every node evaluation. This should be disabled in release builds via `#ifdef`.
25. **Validation:** Ensure that the entire expression engine is `constexpr`-friendly where possible, allowing for compile-time validation of internal logic.

## Validation Criteria
*   **Correctness:** Matches AWS DynamoDB behavior for all edge cases defined in `v2_docs/expression_test_cases.md`.
*   **Performance:** Evaluating a 5-node filter expression on 10,000 items takes less than 1ms.
*   **Memory:** No heap allocations during the evaluation of a cached expression.
