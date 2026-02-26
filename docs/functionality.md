# Functionality Overview

cynamoDB provides a rich set of features that mirror the capabilities of Amazon DynamoDB. This document provides a high-level overview of the major functional modules.

## Core Data Operations

### Single-Item Operations
- **`PutItem`**: Create or replace an item. Supports `ConditionExpression`.
- **`GetItem`**: Retrieve a single item by primary key. Supports `ProjectionExpression`.
- **`UpdateItem`**: Edit attributes of an existing item. Supports `UpdateExpression`.
- **`DeleteItem`**: Remove an item by primary key.

### Batch Operations
- **`BatchGetItem`**: Retrieve up to 100 items from one or more tables.
- **`BatchWriteItem`**: Put or delete up to 25 items in a single request.

### Scan and Query
- **`Query`**: Retrieve items that match a specific partition key and optional sort key criteria.
- **`Scan`**: Iterate over all items in a table. Supports parallel scans for high-volume data.

## Advanced Features

### Transactions
cynamoDB supports fully ACID-compliant transactions via:
- **`TransactWriteItems`**: Up to 100 write operations (Put, Update, Delete, ConditionCheck) performed atomically.
- **`TransactGetItems`**: Up to 100 read operations performed atomically.

### Secondary Indexes
- **Global Secondary Indexes (GSI)**: Indexes with a partition key and an optional sort key that can be different from those on the table. GSIs are eventually consistent.
- **Local Secondary Indexes (LSI)**: Indexes that have the same partition key as the table, but a different sort key. LSIs are strongly consistent.

### DynamoDB Streams
Capture item-level changes in your tables.
- **Stream Shards**: Change records are organized into shards.
- **Record Images**: Supports `NEW_IMAGE`, `OLD_IMAGE`, `NEW_AND_OLD_IMAGES`, and `KEYS_ONLY`.

### Time to Live (TTL)
Automatically delete items after a specified expiration time.
- Managed by a background process.
- No additional cost or capacity consumption for deletions.
- Emits events to DynamoDB Streams if enabled.

### Capability Matrix
For a detailed mapping of supported vs. recognized API operations, see [API & Protocol](api.md).
