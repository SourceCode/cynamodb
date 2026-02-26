# Phase 23: Client Compatibility: AWS SDK Integration Testing

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
CynamoDB must be a drop-in replacement for AWS DynamoDB. The ultimate test is its ability to work perfectly with all official AWS SDKs without any client-side modifications. This phase sets up an automated testing matrix using the official SDKs for Java, Go, Python (Boto3), and JavaScript (Node.js).

## Technical Definition
*   **SDK Matrix:** A set of test suites, one per official AWS SDK, that execute the same logical tests.
*   **DDB-Local Parity:** Use the "DynamoDB Local" test suites as a baseline for compliance.
*   **API Coverage:** Verify all 45+ DynamoDB APIs (including the "v2" APIs like `ExecuteStatement`).

## Reference Files
*   `tests/sdk_compatibility/java/`
*   `tests/sdk_compatibility/python/`
*   `tests/sdk_compatibility/go/`
*   `tests/sdk_compatibility/js/`

## Expanded Tasks
1.  **SDK Test Runner:** Implement `scripts/run_all_sdk_tests.sh`. It must start CynamoDB, wait for `/health` to return 200, then run all 4 SDK test suites and finally generate a consolidated report.
2.  **Java SDK (V2) Integration:** Use `Maven`. Add the `software.amazon.awssdk:dynamodb` dependency. Implement a test class `CynamoDbJavaTest` that uses `DynamoDbClient.builder().endpointOverride(URI.create("http://localhost:8000")).build()`.
3.  **Python Boto3 Integration:** Use `pytest`. Use `boto3.resource('dynamodb', endpoint_url='http://localhost:8000')`. Verify all common resource-level and client-level methods.
4.  **Go SDK (V2) Integration:** Use `go test`. Use `dynamodb.NewFromConfig(cfg, func(o *dynamodb.Options) { o.BaseEndpoint = aws.String("http://localhost:8000") })`.
5.  **Node.js SDK Integration:** Use `Jest`. Use `new DynamoDBClient({ endpoint: "http://localhost:8000" })`. Test both the standard and the `DynamoDBDocumentClient` wrappers.
6.  **SigV4 Validation (Multi-SDK):** Use the "Static Credentials" provider in each SDK with `AccessKey=TEST` and `SecretKey=TEST`. Verify that CynamoDB validates all 4 different SDK signature formats correctly.
7.  **Error Code Parity:** Implement `test_exception_mapping`. Call `GetItem` on a non-existent table. Verify that the SDK throws a native `ResourceNotFoundException` (or equivalent) for each language.
8.  **Paginator Support:** Test `QueryPaginator`. Feed it a result set that requires 3 pages (using `Limit=10`). Verify that the SDK successfully follows the `LastEvaluatedKey` for all 3 pages.
9.  **Waiter Support:** Test `TableExists` waiter. Call `CreateTable`, then immediately start the waiter. Verify it succeeds when CynamoDB transitions the table status from `CREATING` to `ACTIVE`.
10. **Binary Data Handling:** Test: `PutItem` with a 1MB `B` (Binary) attribute using the Java SDK. Read it back using the Python SDK. Verify the byte content is identical.
11. **Null/Empty Value Policy:** Test: `PutItem` with an empty string and a null value. Verify both are correctly persisted and retrieved as defined by the latest DDB 2.0 specs.
12. **High-Precision Number Test:** Test: `PutItem` with `N="1.23456789012345678901234567890123456789"`. Read back. Verify the SDK receives the *exact* string without numeric rounding.
13. **Batch/Transaction Item Limits:** Test: `BatchWriteItem` with 26 items. Verify that the SDK itself throws a client-side exception *before* sending the request (if the SDK has built-in validation).
14. **GSI/LSI Metadata Retrieval:** Test `DescribeTable`. Verify the SDK's `TableDescription` object contains all GSI and LSI metadata exactly as expected.
15. **Streaming Support:** Use the `DynamoDBStreamsClient`. Test: `ListStreams`, `DescribeStream`, `GetShardIterator`, `GetRecords`. Verify the Java SDK's `KinesisClientLibrary` (KCL) can connect to the stream.
16. **PartiQL Compatibility:** Use `ExecuteStatement`. Test: `SELECT * FROM Table WHERE PK=?`. Verify that the SDK's `AttributeValue` parameter binding works correctly for all basic types.
17. **Retries and Backoffs:** In the `server.cpp`, inject a 503 error for 3 consecutive calls. Verify that the SDK's default retry policy (3 retries with exponential backoff) results in a successful 4th call.
18. **Custom Endpoint Support:** Verify that if the SDK uses a `us-east-1` region but a `localhost` endpoint, CynamoDB correctly handles the SigV4 region check.
19. **Header Case Sensitivity:** Send a request with `x-amz-target` (lowercase) using a raw `curl` or a custom HTTP client. Verify CynamoDB handles it identically to uppercase.
20. **Response Header Compliance:** Verify that `x-amzn-RequestId` is present in every SDK's response metadata. This is used for debugging in production AWS environments.
21. **Large Request Handling:** Test: `BatchWriteItem` with 25 items of 400KB each (total ~10MB). Verify all SDKs handle this large payload without timeout or memory issues.
22. **Client-Side Throttling:** Intentionally throttle a table. Verify the SDK correctly identifies the `ProvisionedThroughputExceededException` and does NOT retry unless configured otherwise.
23. **AWS CLI Compatibility:** Test: `aws dynamodb put-item --table-name T --item '{"PK": {"S": "V"}}' --endpoint-url http://localhost:8000`. This is the gold standard for compatibility.
24. **No-SQL Workbench Support:** (Optional) Use the "Operation Builder" in NoSQL Workbench. Verify that it can connect, create tables, and execute queries against CynamoDB.
25. **Validation:** Produce a `CompatibilityScoreboard.md`. Mark every API as "Verified (Java)", "Verified (Python)", etc. Target 100% across the board.

## Validation Criteria
*   **Interoperability:** Any standard AWS SDK can connect to CynamoDB without any code changes besides the endpoint URL.
*   **Behavioral Parity:** Identical input from two different SDKs (e.g., Java and Python) results in identical state in the database.
*   **Stability:** No client-side hangs or "Malformed Response" errors during SDK communication.
