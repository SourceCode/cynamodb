# Setup and Configuration

This document describes how to configure cynamoDB for different environments and use cases.

## Configuration Methods

cynamoDB can be configured via command-line arguments and environment variables. Command-line arguments take precedence.

### Command-Line Arguments

| Argument | Description | Default |
|----------|-------------|---------|
| `--port` | The port the HTTP server listens on. | `8000` |
| `--host` | The IP address to bind to. | `127.0.0.1` |
| `--data-dir` | Directory for persistent storage (LSM tree). | `./data` |
| `--in-memory` | Run with an ephemeral in-memory storage engine. | `false` |
| `--log-level` | Logging verbosity (`trace`, `debug`, `info`, `warn`, `error`). | `info` |
| `--no-auth` | Disable SigV4 authentication (development only). | `false` |
| `--max-threads` | Number of worker threads for the dispatcher. | CPU core count |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `CYNAMODB_PORT` | Equivalent to `--port`. |
| `CYNAMODB_DATA_DIR` | Equivalent to `--data-dir`. |
| `LOG_LEVEL` | Equivalent to `--log-level`. |
| `AWS_ACCESS_KEY_ID` | Used locally for SigV4 validation (stub mode). |
| `AWS_SECRET_ACCESS_KEY` | Used locally for SigV4 validation (stub mode). |

## Persistence Setup

cynamoDB uses a custom Log-Structured Merge-tree (LSM) for persistence.

1. **Default Mode**: Creates an `./data` directory relative to the binary.
2. **Custom Directory**: Use `--data-dir /path/to/storage`.
3. **In-Memory Mode**: If persistence is not required (e.g., CI tests), use `--in-memory`. All data is lost when the process terminates.

## Local Credentials (SigV4)

By default, cynamoDB validates AWS Signature Version 4. In a local development environment, you can use any dummy credentials as long as your client (SDK/CLI) and server agree on them.

**Example for local development:**
```bash
export AWS_ACCESS_KEY_ID=localkey
export AWS_SECRET_ACCESS_KEY=localsecret
./cynamodb
```

Then configure your AWS CLI:
```bash
aws configure set aws_access_key_id localkey
aws configure set aws_secret_access_key localsecret
aws configure set region us-east-1
```

## Resilience & Performance

- **SSTable Compaction**: cynamoDB performs background compaction to optimize read performance. You can monitor this via the `debug` logs.
- **Write-Ahead Log (WAL)**: Every write is recorded in the WAL before being committed to the memtable, ensuring crash consistency.
