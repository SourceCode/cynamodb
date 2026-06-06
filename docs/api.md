# API and Protocol Reference

cynamoDB implements the DynamoDB wire protocol over HTTP, so official AWS SDKs and
the AWS CLI can talk to it as a drop-in local endpoint.

## HTTP Protocol

- **Port**: Default 8000.
- **Protocol**: HTTP/1.1.
- **Content-Type**: `application/x-amz-json-1.0`.
- **Targeting**: The operation is determined by the `X-Amz-Target` header.
  - Format: `DynamoDB_20120810.<OperationName>`

## Authentication (SigV4)

By default cynamoDB accepts unauthenticated requests, which is convenient for local
development. Set `CYNAMODB_REQUIRE_AUTH=1` (any value other than `0`/`false`/`off`/empty)
to opt into enforcement:

- A request with no `Authorization` header is rejected with `MissingAuthenticationTokenException`.
- A request whose `Authorization` header is not a parseable `AWS4-HMAC-SHA256` credential
  is rejected with `IncompleteSignatureException`.

> Enforcement validates the **presence and shape** of the SigV4 header so auth-required
> client code paths can be exercised; it does not perform full cryptographic signature
> verification (there is no credential/secret store). This is a deliberate scope choice
> for a local testing engine.

## API Compliance Status

The matrix below is the source of truth for what the engine actually does. A
contract test (`tests/test_dynamo_features.cpp`) asserts that every "Implemented"
operation is reachable (never returns `501`/`UnknownOperationException`) and that
recognized-but-unimplemented operations return `501 NotImplementedException`.

### ✅ Implemented (real behavior, end-to-end and persisted)

| Operation | Notes |
|-----------|-------|
| `CreateTable` | Hash and hash+range key schemas; catalog persisted atomically. |
| `DescribeTable` / `ListTables` | — |
| `DeleteTable` | Drops the catalog entry and purges the table's stored items. |
| `UpdateTable` | Minimal: table stays `ACTIVE`; billing/throughput changes are accepted as no-ops. |
| `PutItem` | All attribute types (S/N/B/BOOL/NULL/M/L/SS/NS/BS); `ConditionExpression`; `ReturnValues=ALL_OLD`. |
| `GetItem` | `ProjectionExpression` (top-level attributes); a miss returns `{}`. |
| `UpdateItem` | `UpdateExpression` (`SET`/`REMOVE`/`ADD`/`DELETE`, `+`/`-`, `if_not_exists`, `list_append`); `ConditionExpression`; `ReturnValues`. |
| `DeleteItem` | `ConditionExpression`; `ReturnValues=ALL_OLD`. |
| `Query` | `KeyConditionExpression` (and legacy `KeyConditions`); sort-key `=,<,<=,>,>=,BETWEEN,begins_with`; `FilterExpression`; `ProjectionExpression`; `ScanIndexForward`; `Limit`/`ExclusiveStartKey`. |
| `Scan` | `FilterExpression`; `ProjectionExpression`; `Limit`/`ExclusiveStartKey`. |
| `BatchGetItem` / `BatchWriteItem` | Fan-out across tables; validated before any write. |
| `TransactWriteItems` | `Put`/`Delete`/`Update`/`ConditionCheck`, all-or-nothing (conditions verified before any write). |
| `TransactGetItems` | — |

### 🚧 Recognized but not implemented → `501 NotImplementedException`

Every other DynamoDB / DynamoDB Streams target the dispatcher recognizes returns
`501 NotImplementedException`, so SDK feature-detection can tell "not built yet"
apart from a typo'd target. This includes:

- Streams: `ListStreams`, `DescribeStream`, `GetShardIterator`, `GetRecords`
- TTL: `UpdateTimeToLive`, `DescribeTimeToLive`
- Backups / PITR: `CreateBackup`, `RestoreTableFromBackup`, `RestoreTableToPointInTime`, …
- Global tables, imports/exports, contributor insights, Kinesis streaming, resource policies, tags
- PartiQL: `ExecuteStatement`, `BatchExecuteStatement`, `ExecuteTransaction`

A genuinely unknown `X-Amz-Target` (not in the dispatcher) returns
`400 UnknownOperationException`.

### Other current limitations

- **Secondary indexes**: GSI/LSI definitions on `CreateTable` are accepted but queries
  cannot target an index (`IndexName` is not honored); only the base table is queried.
- **TTL**: not auto-expired.
- **Document-path projections/updates**: `ProjectionExpression`/`UpdateExpression` operate on
  top-level attributes; nested paths (`a.b`, `a[0]`) are rejected with `ValidationException`.

## Error Handling

cynamoDB returns errors in the JSON format AWS SDKs expect, with the type also echoed
in the `x-amzn-ErrorType` response header:

```json
{
  "__type": "com.amazonaws.dynamodb.v20120810#ConditionalCheckFailedException",
  "message": "The conditional request failed"
}
```

### Common error codes
- `ValidationException`: Input parameters do not meet constraints (incl. empty key attributes, malformed expressions).
- `ResourceNotFoundException`: The requested table does not exist.
- `ResourceInUseException`: Table already exists.
- `ConditionalCheckFailedException`: A `ConditionExpression` evaluated to false.
- `TransactionCanceledException`: A transaction was cancelled (e.g. a condition check failed).
- `NotImplementedException` (501): A recognized operation that is not yet implemented.
- `UnknownOperationException`: The `X-Amz-Target` is not a recognized operation.
- `SerializationException`: The request body was not valid JSON.
