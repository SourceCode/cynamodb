PRODUCT REQUIREMENTS DOCUMENT (PRD)

Project Name: cynamoDB (Local DynamoDB-Compatible Engine in C++23)

Version: 1.0

Author: Principal Architect (AWS-Compatible Specification)

Status: Foundation Specification

⸻

1. Executive Summary

cynamoDB is a high-performance, fully local, API-compatible implementation of Amazon DynamoDB built in C++23. Its purpose is to:
	•	Provide 1:1 API compatibility with DynamoDB (HTTP + JSON protocol)
	•	Support drop-in SDK compatibility (AWS SDKs, CLI, IaC tooling)
	•	Enable local development, testing, CI, and edge deployment
	•	Provide high resilience and performance
	•	Preserve DynamoDB’s data model, semantics, and edge-case behavior

cynamoDB must behave identically to DynamoDB from the perspective of:
	•	AWS SDKs
	•	AWS CLI
	•	Infrastructure tooling
	•	Error codes
	•	Throughput semantics
	•	Conditional expressions
	•	Transactions
	•	Streams
	•	Global tables (simulated)

This document defines both:
	1.	Product Requirements (PRD)
	2.	Technical Architecture & Engineering Specification

⸻

2. Goals and Non-Goals

2.1 Goals
	•	Full DynamoDB API surface parity
	•	Strict behavioral compatibility
	•	Deterministic test behavior
	•	High-throughput local performance
	•	Crash-safe persistence
	•	Horizontal scale capability (optional cluster mode)
	•	Pluggable storage engines
	•	Local Streams and TTL engine
	•	Observability built-in

2.2 Non-Goals (Phase 1)
	•	Full AWS IAM federation (stubbed IAM mode instead)
	•	Full AWS Cloud integration
	•	True multi-region global replication (simulated)
	•	Managed service features (autoscaling billing logic)

⸻

3. Feature Scope (API Compatibility Matrix)

The system must support all DynamoDB APIs:

3.1 Table Operations
	•	CreateTable
	•	DeleteTable
	•	DescribeTable
	•	ListTables
	•	UpdateTable
	•	DescribeLimits
	•	UpdateTimeToLive
	•	DescribeTimeToLive

3.2 Item Operations
	•	PutItem
	•	GetItem
	•	DeleteItem
	•	UpdateItem
	•	BatchGetItem
	•	BatchWriteItem

3.3 Query Operations
	•	Query
	•	Scan

3.4 Index Operations
	•	Local Secondary Index (LSI)
	•	Global Secondary Index (GSI)

3.5 Transactions
	•	TransactWriteItems
	•	TransactGetItems

3.6 Streams
	•	Enable/Disable Streams
	•	GetRecords
	•	GetShardIterator
	•	DescribeStream
	•	ListStreams

3.7 Backup/Restore
	•	CreateBackup
	•	DeleteBackup
	•	DescribeBackup
	•	ListBackups
	•	RestoreTableFromBackup

3.8 PartiQL Support
	•	ExecuteStatement
	•	BatchExecuteStatement
	•	ExecuteTransaction

3.9 Capacity + Metrics
	•	ReturnConsumedCapacity
	•	ReturnItemCollectionMetrics

⸻

4. System Requirements

4.1 Functional Requirements
	•	Full DynamoDB data model:
	•	Partition Key (HASH)
	•	Sort Key (RANGE)
	•	Attribute types:
	•	S (String)
	•	N (Number)
	•	B (Binary)
	•	BOOL
	•	NULL
	•	M (Map)
	•	L (List)
	•	SS
	•	NS
	•	BS
	•	Expression engine:
	•	ConditionExpression
	•	FilterExpression
	•	UpdateExpression
	•	ProjectionExpression
	•	Conditional write behavior identical to DynamoDB
	•	Transaction atomicity across multiple items/tables
	•	TTL expiration background process
	•	Streams event log replication
	•	Pagination behavior identical
	•	ConsistentRead flag behavior
	•	Eventually consistent default mode

⸻

4.2 Non-Functional Requirements

Requirement	Target
Read Latency	< 1ms (local)
Write Latency	< 2ms (local)
Crash Recovery	No data loss beyond last fsync
Concurrency	100k+ ops/sec per node
Startup Time	< 2 seconds
Memory Efficiency	Lock-free reads
Determinism	Identical error codes to AWS


⸻

5. High-Level Architecture

                +--------------------+
                | HTTP Server Layer  |
                | AWS SigV4 Parser   |
                +---------+----------+
                          |
                +---------v----------+
                | API Dispatcher     |
                +---------+----------+
                          |
                +---------v----------+
                | Expression Engine  |
                +---------+----------+
                          |
                +---------v----------+
                | Transaction Layer  |
                +---------+----------+
                          |
                +---------v----------+
                | Storage Engine     |
                | (Pluggable)        |
                +---------+----------+
                          |
                +---------v----------+
                | WAL + Persistence  |
                +--------------------+


⸻

6. Technical Specification

⸻

6.1 Language & Core Stack
	•	Language: C++23
	•	Build: CMake
	•	Networking: Boost.Beast or custom epoll/kqueue layer
	•	JSON: simdjson + custom serialization
	•	Threading: std::jthread + thread pools
	•	Memory: Arena allocators + slab pools
	•	Storage: Pluggable (default: LSM Tree engine)

⸻

6.2 Storage Engine Design

6.2.1 Data Model Layout

Primary Key Structure:

CompositeKey = hash(partition_key) + sort_key

Internal layout:

Table
 ├── Primary Index (LSM Tree)
 ├── GSI Indexes
 ├── LSI Indexes
 ├── Stream Log
 └── TTL Queue

6.2.2 Default Engine: LSM Tree

Inspired by:
	•	RocksDB
	•	DynamoDB internal architecture

Components:
	•	MemTable (Skiplist or B-Tree)
	•	WAL (append-only log)
	•	Immutable MemTable
	•	SSTables
	•	Bloom Filters
	•	Compaction Threads

Optimizations:
	•	Prefix compression
	•	Block cache
	•	Zero-copy reads
	•	Lock-free readers
	•	Snapshot isolation

⸻

6.3 Concurrency Model

6.3.1 Read Path
	•	Lock-free reads
	•	Snapshot isolation
	•	Atomic pointer swaps for memtable

6.3.2 Write Path
	•	Partition-level mutex
	•	WAL append (sync optional)
	•	Memtable insert
	•	Background compaction

6.3.3 Transactions

Use:
	•	MVCC
	•	Write intents
	•	Conflict detection
	•	2-phase commit (internal)

Guarantees:
	•	Serializable isolation (matching DynamoDB)

⸻

6.4 API Compatibility Layer

6.4.1 HTTP Interface
	•	JSON over HTTP
	•	SigV4 header parsing
	•	Identical request/response payloads

Headers:

X-Amz-Target: DynamoDB_20120810.PutItem

Response shape must match AWS exactly.

⸻

6.4.2 Error Compatibility

Must reproduce:
	•	ConditionalCheckFailedException
	•	ProvisionedThroughputExceededException
	•	ResourceNotFoundException
	•	TransactionCanceledException
	•	ValidationException

Error payloads must match exact JSON structure.

⸻

6.5 Expression Engine

Full parser for:
	•	ConditionExpression grammar
	•	UpdateExpression grammar
	•	Attribute name substitution (#)
	•	Attribute value substitution (:)

Implementation:
	•	Lexer → AST → Execution Plan
	•	Bytecode interpreter for performance
	•	Precompiled expression cache

⸻

6.6 Secondary Index Implementation

6.6.1 GSI
	•	Separate LSM tree per index
	•	Async replication from primary write
	•	Eventually consistent

6.6.2 LSI
	•	Shares partition key
	•	Same partition placement
	•	Strongly consistent

⸻

6.7 Streams Engine
	•	Append-only log per table
	•	Shard simulation
	•	Sequence numbers
	•	Trim horizon

Stream record includes:
	•	NewImage
	•	OldImage
	•	Keys
	•	ApproximateCreationDateTime

⸻

6.8 TTL Engine
	•	Background priority queue
	•	Time-indexed scan
	•	Async deletion
	•	Emits stream event

⸻

6.9 Capacity Simulation

Simulated:
	•	RCUs
	•	WCUs
	•	Burst capacity
	•	Throttling

Token bucket algorithm per partition.

⸻

6.10 Backup and Restore

Backup:
	•	Snapshot SSTables
	•	Copy WAL
	•	Metadata export

Restore:
	•	Load snapshot
	•	Replay WAL

Atomic restore.

⸻

6.11 Observability
	•	Prometheus metrics
	•	OpenTelemetry traces
	•	Structured JSON logs
	•	Slow query logging

⸻

6.12 Configuration Modes

6.12.1 Local Development Mode
	•	No auth required
	•	In-memory mode
	•	Persistent mode

6.12.2 Cluster Mode (Optional Phase 2)
	•	Raft replication
	•	Leader/follower
	•	Partition sharding

⸻

7. Performance Optimizations
	•	SIMD JSON parsing
	•	Memory pools
	•	Lock striping
	•	Partition-level parallelism
	•	Read-optimized bloom filters
	•	Async compaction
	•	Write coalescing
	•	Zero-copy network writes

⸻

8. Testing Requirements

8.1 Compatibility Testing
	•	Run AWS SDK integration tests
	•	Replay DynamoDB API recordings
	•	Validate byte-for-byte response matching

8.2 Fuzz Testing
	•	Expression fuzzing
	•	Transaction fuzzing
	•	Concurrency fuzzing

8.3 Crash Recovery Testing
	•	Kill during WAL write
	•	Kill during compaction
	•	Validate recovery integrity

⸻

9. Security Model
	•	SigV4 verification
	•	IAM stub mode
	•	TLS support
	•	Local token-based override

⸻

10. Deployment Targets
	•	macOS
	•	Linux
	•	Windows
	•	Docker
	•	Embedded (edge devices)

⸻

11. Phased Roadmap

Phase 1
	•	Core CRUD
	•	Query/Scan
	•	GSI/LSI
	•	Expressions
	•	Transactions
	•	WAL + Persistence

Phase 2
	•	Streams
	•	TTL
	•	Backup/Restore
	•	Capacity simulation

Phase 3
	•	Cluster mode
	•	Global tables simulation
	•	Advanced observability

⸻

12. Acceptance Criteria
	•	AWS SDKs work without modification
	•	CLI works identically
	•	Error codes match exactly
	•	Expression semantics identical
	•	Transaction semantics identical
	•	No data corruption under crash testing
	•	100k ops/sec sustained locally

⸻

13. Deliverables
	•	C++23 core engine
	•	REST API server
	•	DynamoDB-compatible SDK test harness
	•	Docker image
	•	Benchmark suite
	•	Documentation

⸻

14. Summary

cynamoDB establishes a high-performance, fully compatible DynamoDB implementation in C++23 optimized for:
	•	Deterministic behavior
	•	Resilience
	•	Performance
	•	Extensibility
	•	SDK compatibility

This PRD and Technical Specification define the architectural foundation required to build a production-grade, DynamoDB-compatible local engine with strict API and semantic parity.
