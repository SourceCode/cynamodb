# AGENTS.md - Technical Specification for AI Agents (v2.4.0)

## Overview
cynamoDB is a high-performance, local DynamoDB-compatible database engine written in C++23. It speaks the DynamoDB HTTP/JSON wire protocol so AWS SDKs and the CLI work against it for local development, testing, and CI.

> **Read this before architecting against cynamoDB.** The table below is the source of
> truth for what is implemented. The authoritative, contract-tested matrix lives in
> [docs/api.md](docs/api.md#api-compliance-status); do not assume a feature exists because
> DynamoDB has it.

## Capabilities Mapping

| Feature | Status | Notes |
|---------|--------|-------|
| **Item CRUD** | ✅ Implemented | `PutItem`/`GetItem`/`UpdateItem`/`DeleteItem`, all attribute types (S/N/B/BOOL/NULL/M/L/SS/NS/BS), persisted across restarts and SSTable flushes. |
| **Conditional writes** | ✅ Implemented | `ConditionExpression` on Put/Delete/Update; `ReturnValues`. |
| **Query & Scan** | ✅ Implemented | `KeyConditionExpression` (+ legacy `KeyConditions`), sort-key range operators, `FilterExpression`, `ProjectionExpression` (top-level), `ScanIndexForward`, pagination. |
| **Batch & Transactions** | ✅ Implemented | `BatchGetItem`/`BatchWriteItem`; `TransactWriteItems` (all-or-nothing) / `TransactGetItems`. |
| **Table lifecycle** | ✅ Implemented | `CreateTable`/`DescribeTable`/`ListTables`/`DeleteTable`/`UpdateTable` (minimal). |
| **LSM Tree Storage** | ✅ Implemented | WAL + memtable + SSTables + compaction; every type survives a flush. |
| **Expression Engine** | ✅ Implemented | Condition/Filter/Update/KeyCondition, incl. document paths (`a.b`, `a[0]`). |
| **SigV4 Authentication** | ✅ Implemented | `CYNAMODB_REQUIRE_AUTH=1` enforces full SigV4 verification against a credential store. |
| **DynamoDB Streams** | ✅ Implemented | INSERT/MODIFY/REMOVE records; ListStreams/DescribeStream/GetShardIterator/GetRecords. |
| **TTL Engine** | ✅ Implemented | UpdateTimeToLive/DescribeTimeToLive; expired items filtered from reads. |
| **Secondary Indexes** | ✅ Implemented | GSI/LSI parsed, maintained on writes, queryable via `IndexName`. |
| **Capacity throttling** | ✅ Implemented | Provisioned tables throttle with ProvisionedThroughputExceededException. |
| **Backups / PITR** | ✅ Implemented | CreateBackup/Restore (durable snapshots); PITR restores current state. |
| **Global tables / PartiQL** | ✅ Implemented | Single-region global tables; ExecuteStatement/BatchExecuteStatement. |

## Operational Guidance

### Command-Line Interface (CLI)
The primary binary is `cynamodb`. It is configured entirely through environment
variables (there are no command-line flags):

```bash
CYNAMODB_DATA_DIR=./data CYNAMODB_PORT=8000 ./cynamodb
```

**Environment variables:**
- `CYNAMODB_BIND_ADDR`: Bind address (default `0.0.0.0`).
- `CYNAMODB_PORT`: Port to listen on (default `8000`).
- `CYNAMODB_THREADS`: HTTP worker threads (default: hardware concurrency).
- `CYNAMODB_DATA_DIR`: Directory for LSM persistence (default `./data`).
- `CYNAMODB_WAL_FSYNC`: Set to `0`/`off` to disable per-write `fdatasync` for throughput.
- `CYNAMODB_REQUIRE_AUTH`: Set to a truthy value to enforce full SigV4 verification.
- `CYNAMODB_ACCESS_KEY_ID` / `CYNAMODB_SECRET_ACCESS_KEY`: the credential used for SigV4
  verification (default `cynamodb` / `cynamodb-secret`).

### API Endpoints
cynamoDB listens on HTTP and responds to the standard DynamoDB JSON protocols (`application/x-amz-json-1.0`).

- **Base URL**: `http://localhost:8000`
- **Headers Required**:
  - `X-Amz-Target`: `DynamoDB_20120810.<OperationName>`
  - `Content-Type`: `application/x-amz-json-1.0`
  - `Authorization`: SigV4 signature — only required when `CYNAMODB_REQUIRE_AUTH` is set; ignored otherwise.

### Example: Create Table
```bash
aws dynamodb create-table \
    --table-name TestTable \
    --attribute-definitions AttributeName=pk,AttributeType=S \
    --key-schema AttributeName=pk,KeyType=HASH \
    --provisioned-throughput ReadCapacityUnits=5,WriteCapacityUnits=5 \
    --endpoint-url http://localhost:8000
```

## Context & Constraints

### Input Requirements
- **JSON Format**: Valid DynamoDB JSON payloads.
- **Data Model**: Partition Key is required; Sort Key is optional. Key attribute values must be non-empty.
- **SigV4**: Requests are unauthenticated by default; set `CYNAMODB_REQUIRE_AUTH` to enforce.

### Expected Output Formats
- **Success**: HTTP 200 OK with the standard DynamoDB response JSON.
- **Error**: HTTP 400/500 with `__type` and `message` in the body, matching AWS error shapes.

### Known Limitations
- **PITR**: no continuous change log — RestoreTableToPointInTime restores the source's current state.
- **Global tables**: modeled as a single-region (`ddblocal`) replica.
- **`ExecuteTransaction`** (PartiQL transactions), Contributor Insights, imports/exports,
  Kinesis streaming destination, resource policies, tagging, and auto-scaling return
  `501 NotImplementedException`.
- **Numeric sort-key ordering** uses a `double` codec (exact to 2⁵³); stored `N` values round-trip exactly.
- **Cluster Mode**: single-node local engine.

## Agent-Optimized Reference
- **Engine**: C++23, Boost.Beast, simdjson.
- **Persistence**: LSM-tree with WAL (Write-Ahead Log) for crash consistency.
- **Schema**: Enforced at the table level; attributes are schema-less within items.

## Workflows Reference
- `/build-agents-md`: Updates this specification.
- `/gen-documentation`: Refreshes the full docs suite.
- `/cpp-git-commit-version-push`: Handles versioning and releases.
