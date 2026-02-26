# AGENTS.md - Technical Specification for AI Agents (v2.1.2)

## Overview
cynamoDB is a high-performance, local DynamoDB-compatible database engine written in C++23. It provides 1:1 API compatibility with Amazon DynamoDB, enabling local development, testing, and edge computing without relying on the AWS cloud.

## Capabilities Mapping

| Feature | Description | Purpose |
|---------|-------------|---------|
| **DynamoDB API Parity** | Supports core DynamoDB operations (CRUD, Query, Scan, Batch). | Drop-in replacement for AWS DynamoDB in local/CI environments. |
| **LSM Tree Storage** | High-throughput persistence layer using Log-Structured Merge-trees. | Efficient writes and optimized range scans (Sort Keys). |
| **Expression Engine** | Full AST-based parser for Condition, Update, and Filter expressions. | Implements complex DynamoDB logic identically to AWS. |
| **SigV4 Authentication** | Supports AWS Signature Version 4. | Compatible with standard AWS SDKs and CLI. |
| **DynamoDB Streams** | Capture item-level changes in chronological order. | Enables event-driven architectures and triggers locally. |
| **ACID Transactions** | `TransactWriteItems` and `TransactGetItems` support. | Ensures data integrity across multiple items and tables. |
| **TTL Engine** | Automatic expiration of items based on a timestamp attribute. | Managed data retention and cleanup. |
| **Secondary Indexes** | GSI (Global) and LSI (Local) support. | Flexible data access patterns beyond the primary key. |

## Operational Guidance

### Command-Line Interface (CLI)
The primary binary is `cynamodb`.

**Basic Usage:**
```bash
./cynamodb [options]
```

**Common Options:**
- `--port <number>`: Port to listen on (default: 8000).
- `--data-dir <path>`: Directory for LSM tree persistence (default: `./data`).
- `--in-memory`: Enable in-memory mode (ephemeral).
- `--log-level <level>`: Set logging verbosity (trace, debug, info, warn, error).

### API Endpoints
cynamoDB listens on HTTP and responds to the standard DynamoDB JSON protocols (`application/x-amz-json-1.0`).

- **Base URL**: `http://localhost:8000`
- **Headers Required**:
  - `X-Amz-Target`: `DynamoDB_20120810.<OperationName>`
  - `Content-Type`: `application/x-amz-json-1.0`
  - `Authorization`: SigV4 signature (or stub in dev mode).

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
- **Data Model**: Partition Key is required; Sort Key is optional.
- **SigV4**: Requests must be signed unless `--no-auth` or dev-mode override is used.

### Expected Output Formats
- **Success**: HTTP 200 OK with the standard DynamoDB response JSON.
- **Error**: HTTP 400/500 with `__type` and `message` in the body, matching AWS error shapes.

### Known Limitations
- **IAM Policies**: Simplified local stub; full IAM resource-based policies are not enforced.
- **PartiQL**: Basic support for `ExecuteStatement`; advanced ANSI-SQL features may be limited.
- **Cluster Mode**: Currently optimized for single-node local performance.

## Agent-Optimized Reference
- **Engine**: C++23, Boost.Beast, simdjson.
- **Persistence**: LSM-tree with WAL (Write-Ahead Log) for crash consistency.
- **Schema**: Enforced at the table level; attributes are schema-less within items.

## Workflows Reference
- `/build-agents-md`: Updates this specification.
- `/gen-documentation`: Refreshes the full docs suite.
- `/cpp-git-commit-version-push`: Handles versioning and releases.
