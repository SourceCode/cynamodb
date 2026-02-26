# API and Protocol Reference

cynamoDB implements the DynamoDB wire protocol over HTTP, ensuring 1:1 compatibility with official AWS SDKs.

## HTTP Protocol

- **Port**: Default 8000.
- **Protocol**: HTTP/1.1.
- **Content-Type**: `application/x-amz-json-1.0` or `application/x-amz-json-1.1`.
- **Targeting**: The operation is determined by the `X-Amz-Target` header.
  - Format: `DynamoDB_20120810.<OperationName>`

## Authentication (SigV4)

cynamoDB supports **AWS Signature Version 4**. 
- In development mode (using dummy keys), any calculated signature against the provided keys is accepted.
- Requests without headers or with malformed signatures will return `MissingAuthenticationTokenException` or `IncompleteSignatureException`.

## API Compliance Status

### Supported Core APIs
These operations are fully implemented and behave identically to AWS DynamoDB:
- `CreateTable`, `UpdateTable`, `DeleteTable`, `DescribeTable`, `ListTables`
- `PutItem`, `GetItem`, `UpdateItem`, `DeleteItem`
- `Query`, `Scan`
- `BatchGetItem`, `BatchWriteItem`
- `TransactWriteItems`, `TransactGetItems`
- `UpdateTimeToLive`, `DescribeTimeToLive`

### Supported Stream APIs
- `ListStreams`, `DescribeStream`, `GetShardIterator`, `GetRecords`

### Recognized But Not Implemented
The following operations are recognized by the dispatcher but will return a `501 NotImplementedException`. This allows clients to discover the operations but signals that the feature is not yet available:
- `CreateGlobalTable`, `UpdateGlobalTable`
- `ImportTable`, `ExportTableToPointInTime`
- `DescribeContributorInsights`

For a full list of all 70+ recognized operations, see [api-operations-compliance.md](api-operations-compliance.md).

## Error Handling

cynamoDB returns errors in the exact JSON format expected by AWS SDKs:

```json
{
  "__type": "com.amazonaws.dynamodb.v20120810#ConditionalCheckFailedException",
  "message": "The conditional request failed"
}
```

### Common Error Codes
- `ValidationException`: Input parameters do not meet constraints.
- `ResourceNotFoundException`: The requested table or index does not exist.
- `TransactionCanceledException`: A transaction failed due to conflict or condition check.
- `ProvisionedThroughputExceededException`: Simulated throttling (if enabled).
