# Phase 19: PartiQL: Full Grammar Support & Query Planning

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
PartiQL is a SQL-compatible query language that allows for more complex queries, including joins (limited in DynamoDB) and nested attribute manipulation. This phase implements the PartiQL parser, query planner, and execution engine, providing 1:1 compatibility with AWS DynamoDB's PartiQL implementation.

## Technical Definition
*   **PartiQL Parser:** A recursive-descent or PEG parser for the PartiQL grammar.
*   **Query Planner:** A component that selects the most efficient access path (Table Scan vs Index Query) for a given SQL-like statement.
*   **ExecuteStatement API:** The primary entry point for PartiQL queries.

## Reference Files
*   `include/cynamodb/expressions/partiql_parser.hpp`
*   `src/expressions/partiql_evaluator.cpp`
*   `src/api/dispatcher.cpp`

## Expanded Tasks
1.  **PartiQL Grammar Definition:** Implement the grammar using a parser generator or a hand-written recursive descent parser in `partiql_parser.cpp`. Support `SELECT`, `FROM`, `WHERE`, `ORDER BY`, `INSERT INTO`, `UPDATE`, `DELETE FROM`.
2.  **Lexer & Tokenizer:** Implement `PartiQLLexer`. It must handle SQL keywords (case-insensitive), double-quoted identifiers (for table names with spaces), and JSON-like literals.
3.  **Recursive-Descent Parser:** The parser must produce a `PartiQLAST`. Use the `ASTNode` structure from Phase 06 but extend it to support SQL-specific constructs like `ProjectionItem` and `FromSource`.
4.  **SELECT Statement Execution:** Map `SELECT` to the `Query` or `Scan` internal API. If the `WHERE` clause specifies the Partition Key, it's a `Query`; otherwise, it's a `Scan`.
5.  **INSERT Statement Execution:** Map `INSERT INTO "Table" VALUE {'PK': '123'}` to the `PutItem` internal API. Ensure it respects the `ConditionExpression` if the statement includes a `WHERE NOT EXISTS` clause.
6.  **UPDATE Statement Execution:** Map `UPDATE` to the `UpdateItem` internal API. Support the `SET`, `REMOVE`, `ADD`, `DELETE` actions within the SQL `SET` clause.
7.  **DELETE Statement Execution:** Map `DELETE FROM "Table" WHERE "PK" = '123'` to the `DeleteItem` internal API. The `WHERE` clause must uniquely identify the item.
8.  **Query Planner - Index Selection:** Implement `choose_best_index(ast)`. If the `WHERE` clause uses an attribute that is the PK of a GSI, the planner should rewrite the query to use that GSI automatically.
9.  **ExecuteStatement API:** Implement the handler. It must accept the `Statement` (string) and `Parameters` (list of AttributeValues). Return a `Result` object with `Items` and `NextToken`.
10. **BatchExecuteStatement API:** Implement the batch handler. It must take a list of statements and execute them sequentially. If one fails, it should report the error in the `Results` array without stopping.
11. **Parameter Binding:** Implement `bind_parameters(ast, params)`. Replace all `?` tokens in the AST with the corresponding values from the `Parameters` list before execution.
12. **Nested Attribute Access:** Support SQL paths like `SELECT Data.Address.City FROM "Users"`. The evaluator must use the same path-traversal logic as the `ExpressionEngine`.
13. **Function Support:** Implement `size()`, `attribute_type()`, `exists()`, `missing()`. These must be available in the `WHERE` clause of PartiQL statements.
14. **Pagination (NextToken):** Implement `PartiQLPagination`. The `NextToken` returned by `ExecuteStatement` must be a base64-encoded string containing the last evaluated key and a hash of the statement to prevent token reuse across different queries.
15. **Type Casting:** Implement `CAST(x AS Type)`. Support casting between `String` and `Number` as per DDB rules.
16. **Aggregation (Optional/Limited):** Support `SELECT COUNT(*) FROM "Table"`. This must be implemented by running a full scan and returning only the count, consuming RCU accordingly.
17. **DDB-Specific Syntax:** Support `RETURNING ALL NEW *`, `RETURNING ALL OLD *`, and `RETURNING UPDATED NEW *` in `UPDATE` and `DELETE` statements.
18. **Planner - Filter Pushdown:** If a `SELECT` has a `WHERE` clause with multiple conditions, the planner should push as many as possible into the `FilterExpression` of the underlying `Query/Scan`.
19. **ExecuteTransaction API (PartiQL):** Implement `ExecuteTransaction`. It takes a list of PartiQL statements and executes them within a single atomic `TransactWriteItems` operation.
20. **PartiQL Error Mapping:** Map a `ParserError` to `400 BadRequest` with `__type: com.amazon.awsexception#InvalidStatementException`.
21. **Execution Stats:** Correctly calculate the RCU/WCU cost of the PartiQL statement. A `SELECT` that scans 100 items but returns 0 still costs the same as a `Scan` of 100 items.
22. **AST Caching:** Implement `PartiQLCache`. Use the raw statement string as the key. Store the compiled and optimized `PartiQLAST` to avoid re-parsing overhead.
23. **Test Coverage - Complex SELECT:** Test: `SELECT * FROM "Table" WHERE "Age" > 18 AND "City" = 'NY'`. Verify it correctly identifies the base table and applies the filters.
24. **Test Coverage - Batch UPDATE:** Test `BatchExecuteStatement` with 25 `UPDATE` calls. Verify that each one is executed and its result is correctly placed in the response array.
25. **Validation:** Verify that the PartiQL engine is protected against SQL-injection-style attacks by ensuring that identifiers and literals are strictly validated.

## Validation Criteria
*   **Compatibility:** Passes the AWS SDK PartiQL compatibility suite.
*   **Correctness:** Queries on GSIs/LSIs via PartiQL return the same data as the direct API equivalents.
*   **Performance:** The query planner correctly identifies and uses the most efficient index 100% of the time for supported patterns.
