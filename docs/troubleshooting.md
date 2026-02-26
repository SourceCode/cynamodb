# Troubleshooting Guide

This guide addresses common issues encountered when running or developing with cynamoDB.

## Common Startup Issues

### "Address already in use"
- **Symptom**: The server fails to start with an error indicating port 8000 is taken.
- **Resolution**: Another process (possibly an old cynamoDB instance or official DynamoDB Local) is using the port.
  - Find the process: `lsof -i :8000`
  - Kill the process: `kill -9 <PID>`
  - Or run cynamoDB on a different port: `./cynamodb --port 8001`

### "Failed to initialize LSM engine"
- **Symptom**: Error on startup regarding data directory permissions or corruption.
- **Resolution**: 
  - Ensure the user running the binary has write permissions to the `--data-dir`.
  - If the directory is corrupted, you can reset it: `rm -rf ./data`.

## Connection & API Issues

### "SignatureDoesNotMatchException"
- **Symptom**: AWS SDKs fail to communicate with the message "The request signature we calculated does not match...".
- **Resolution**:
  - Ensure your system clock is synchronized (SigV4 is time-sensitive).
  - Check that the `AWS_ACCESS_KEY_ID` and `AWS_SECRET_ACCESS_KEY` on the client match what you've configured (or the defaults) on the server.
  - If using dummy keys, ensure the region is set consistently (e.g., `us-east-1`).

### "HTTP 501 Not Implemented"
- **Symptom**: A perfectly valid DynamoDB request returns a 501 error.
- **Resolution**: This operation is recognized by cynamoDB but not yet implemented. Refer to [API & Protocol](api.md) for the current compliance status.

## Debugging Tips

### Enable Detailed Logging
Set the log level to `trace` to see the full request/response cycle and internal engine state:
```bash
./cynamodb --log-level trace
```

### Examine the WAL
The Write-Ahead Log is stored in the data directory. While binary, many parts are human-readable. You can use `hexdump` or `strings` to see the most recent operations if the engine fails to recover.

### Core Dumps
If the binary crashes, ensure core dumps are enabled on your system to help with debugging the C++ backtrace.
```bash
ulimit -c unlimited
```
