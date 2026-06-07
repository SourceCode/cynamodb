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
to opt into **full AWS Signature Version 4 verification**:

- A request with no `Authorization` header → `MissingAuthenticationTokenException`.
- A malformed `AWS4-HMAC-SHA256` header → `IncompleteSignatureException`.
- An access key not in the credential store → `UnrecognizedClientException`.
- A signature that does not match the recomputed one → `InvalidSignatureException`.

Verification reconstructs the canonical request, derives the SigV4 signing key, and
compares signatures (validated against the AWS published test vectors). The credential
store is seeded from `CYNAMODB_ACCESS_KEY_ID` / `CYNAMODB_SECRET_ACCESS_KEY`
(defaulting to `cynamodb` / `cynamodb-secret`).

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
| `Scan` | `FilterExpression`; `ProjectionExpression`; `Limit`/`ExclusiveStartKey`; parallel scan via `Segment`/`TotalSegments`. |
| `BatchGetItem` / `BatchWriteItem` | Fan-out across tables; validated before any write. |
| `TransactWriteItems` | `Put`/`Delete`/`Update`/`ConditionCheck`, all-or-nothing (conditions + updates validated before any write). |
| `TransactGetItems` | — |
| `UpdateTimeToLive` / `DescribeTimeToLive` | TTL spec persisted; expired items filtered from reads (lazy reaping). |
| Secondary indexes | GSI/LSI parsed, maintained on every write, and queryable via `Query`/`Scan` with `IndexName` (projection types honored, sparse indexes). |
| Streams | `ListStreams`/`DescribeStream`/`GetShardIterator`/`GetRecords`; INSERT/MODIFY/REMOVE records with NEW/OLD images per the stream view type. |
| PartiQL | `ExecuteStatement`/`BatchExecuteStatement` for SELECT/INSERT/UPDATE/DELETE with `?` parameters. |
| Backups | `CreateBackup`/`ListBackups`/`DescribeBackup`/`DeleteBackup`/`RestoreTableFromBackup` (durable JSON snapshots). |
| PITR / continuous backups | `RestoreTableToPointInTime`, `UpdateContinuousBackups`/`DescribeContinuousBackups`. |
| Global tables | `CreateGlobalTable`/`DescribeGlobalTable`/`UpdateGlobalTable`/`ListGlobalTables` (single-region replica). |
| Capacity | Provisioned tables throttle with `ProvisionedThroughputExceededException`. |
| Document paths | `a.b.c` / `a[0]` in Condition/Filter/Projection/Update expressions. |
| SigV4 | Full cryptographic verification when `CYNAMODB_REQUIRE_AUTH` is set. |

### 🚧 Recognized but not implemented → `501 NotImplementedException`

The remaining recognized targets return `501 NotImplementedException`, so SDK
feature-detection can tell "not built yet" apart from a typo'd target:

- `ExecuteTransaction` (PartiQL transactions)
- Contributor Insights, imports/exports, Kinesis streaming destination
- Resource policies, tagging, table replica auto-scaling, `DescribeLimits`/`DescribeEndpoints`

A genuinely unknown `X-Amz-Target` (not in the dispatcher) returns
`400 UnknownOperationException`.

### Behavioral notes (intentional local-engine simplifications)

- **PITR** has no continuous change log: `RestoreTableToPointInTime` restores the
  source table's **current** state.
- **Global tables** model the single node as a one-replica (`ddblocal`) global table.
- **Numeric sort keys** use a `double` ordering codec (exact ordering to 2⁵³); the stored
  `N` attribute value itself round-trips exactly at any precision and is canonicalized
  on write (`1.0`→`1`, `007`→`7`).
- **Batch/transaction caps** match AWS: `BatchWriteItem` ≤25, `BatchGetItem` ≤100,
  `TransactWriteItems`/`TransactGetItems` ≤100 items.

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
