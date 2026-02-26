# Storage Schema and Data Model

cynamoDB is designed for high-performance persistence using a custom Log-Structured Merge-tree (LSM) architecture.

## Internal Data Layout

### The LSM Tree
Every table in cynamoDB is represented by a set of LSM Tree structures:
1. **Primary Index**: Stores the full item data indexed by the primary key (Hash + Range).
2. **GSI Indexes**: Separate LSM trees containing projected attributes, indexed by the GSI key.
3. **LSI Indexes**: LSM trees sharing the same partition but indexed by the LSI sort key.

### Key Structure
Internal keys are structured to ensure efficient sorting and prefix scanning:
- `PartitionKey`: Serialized and prefixed.
- `SortKey`: Suffix to the Partition Key.
- `Internal Metadata`: Versioning and deletion markers (tombstones).

## Data Types

cynamoDB supports all core DynamoDB attribute types:

| Type Code | Description | Example |
|-----------|-------------|---------|
| `S` | String | `"Hello"` |
| `N` | Number | `"123.45"` (Sent as String) |
| `B` | Binary | Base64 encoded blob |
| `BOOL` | Boolean | `true` |
| `NULL` | Null | `true` |
| `M` | Map | `{"key": {"S": "value"}}` |
| `L` | List | `[{"N": "1"}, {"S": "two"}]` |
| `SS` | String Set | `["a", "b", "c"]` |
| `NS` | Number Set | `["1", "2.2"]` |
| `BS` | Binary Set | `["YmluYXJ5", "ZGF0YQ=="]` |

## Persistence Components

### MemTable
An in-memory, ordered structure (SkipList or B-Tree) that receives all incoming writes. When the MemTable reaches a certain size, it is flushed to disk as an SSTable.

### Write-Ahead Log (WAL)
Every write operation is first appended to the WAL. This ensures that in the event of a crash, the process can re-instantiate the MemTable by replaying the log.

### SSTables (Sorted String Tables)
Immutable disk-based files containing sorted key-value pairs. 
- **Levels**: SSTables are organized into levels (L0, L1, etc.).
- **Compaction**: A background process merges smaller SSTables into larger ones and removes obsolete versions or deleted items.

## Table Metadata
The `metadata.json` or `manifest` file in each table directory tracks:
- Tables names and ARNs.
- Attribute definitions and Key schemas.
- Index configurations.
- Current WAL sequence number.
- Active SSTable list.
