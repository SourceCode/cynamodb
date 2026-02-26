# CynamoDB Operations Manual

## Cluster Topology
CynamoDB currently operates as a high-performance single-node database. Multi-node clustering support is planned for v3.

## Backup and Restore
### Creating a Backup
```bash
# via HTTP API
X-Amz-Target: DynamoDB_20120810.CreateBackup
{"TableName": "MyTable", "BackupName": "Snapshot1"}
```

### Restoring from Backup
```bash
X-Amz-Target: DynamoDB_20120810.RestoreTableFromBackup
{"TargetTableName": "NewTable", "BackupArn": "arn:aws:..."}
```

## Scaling Provisioned Capacity
Capacity is managed via the `UpdateTable` API. Throttling is enforced using a token bucket algorithm with a 5-minute burst window.
