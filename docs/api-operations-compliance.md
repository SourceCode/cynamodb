# DynamoDB API Operations Compliance

Reference: <https://docs.aws.amazon.com/amazondynamodb/latest/APIReference/API_Operations_Amazon_DynamoDB.html>

## Coverage Strategy

- All API operation names listed in the DynamoDB API reference are now recognized by the request dispatcher.
- Operations implemented in cynamoDB execute their handler logic.
- Operations recognized but not implemented return:
  - HTTP `501 Not Implemented`
  - Error type: `com.amazonaws.dynamodb.v20120810#NotImplementedException`

This standardizes behavior for clients that depend on deterministic operation discovery/handling.

## Implemented Operations

- `BatchGetItem`
- `BatchWriteItem`
- `CreateBackup`
- `CreateTable`
- `DeleteBackup`
- `DeleteItem`
- `DeleteTable`
- `DescribeBackup`
- `DescribeContinuousBackups`
- `DescribeEndpoints`
- `DescribeLimits`
- `DescribeTable`
- `DescribeTimeToLive`
- `GetItem`
- `ListBackups`
- `ListTables`
- `ListTagsOfResource`
- `PutItem`
- `Query`
- `RestoreTableFromBackup`
- `Scan`
- `TagResource`
- `TransactGetItems`
- `TransactWriteItems`
- `UntagResource`
- `UpdateContinuousBackups`
- `UpdateItem`
- `UpdateTable`
- `UpdateTimeToLive`

## Recognized But Not Implemented

- `BatchExecuteStatement`
- `CreateGlobalTable`
- `DeleteResourcePolicy`
- `DescribeContributorInsights`
- `DescribeExport`
- `DescribeGlobalTable`
- `DescribeGlobalTableSettings`
- `DescribeImport`
- `DescribeKinesisStreamingDestination`
- `DescribeTableReplicaAutoScaling`
- `DisableKinesisStreamingDestination`
- `EnableKinesisStreamingDestination`
- `ExecuteStatement`
- `ExecuteTransaction`
- `ExportTableToPointInTime`
- `GetResourcePolicy`
- `ImportTable`
- `ListContributorInsights`
- `ListExports`
- `ListGlobalTables`
- `ListImports`
- `PutResourcePolicy`
- `RestoreTableToPointInTime`
- `UpdateContributorInsights`
- `UpdateGlobalTable`
- `UpdateGlobalTableSettings`
- `UpdateKinesisStreamingDestination`
- `UpdateTableReplicaAutoScaling`

## DynamoDB Streams Operations (Separate Target Prefix)

- `ListStreams`
- `DescribeStream`
- `GetShardIterator`
- `GetRecords`
