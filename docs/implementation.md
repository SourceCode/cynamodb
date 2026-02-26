# Implementation Details

This document provides a deep dive into the engineering choices and internal architecture of cynamoDB.

## Core Philosophical Principles

1. **High-Integrity Type Safety**: Built in C++23, leveraging modern features like `std::expected`, `std::jthread`, and concepts to ensure robust memory and error management.
2. **Deterministic Behavior**: Designed to provide bit-for-bit response parity with Amazon DynamoDB, including error codes and JSON shapes.
3. **Lock-Free Concurrency**: Optimized for multi-core systems using lock-free data structures in the read path.

## Repository Layout

```text
/
├── include/           # Public headers (Core abstractions)
├── src/
│   ├── api/           # Operation dispatchers and handlers
│   ├── auth/          # SigV4 parsing and validation
│   ├── core/          # Memory management, schedulers
│   ├── engine/        # Storage engines (LSM, In-Memory)
│   ├── expressions/   # Lexer, Parser, Evaluator for DynamoDB expressions
│   ├── http/          # Boost.Beast HTTP server implementation
│   ├── json/          # Custom serializers and simdjson integration
│   └── main.cpp       # Application entry point
├── tests/             # Unit, integration, and fuzz tests
└── docs/              # Technical documentation
```

## Memory Management

cynamoDB uses a tiered memory management strategy:
- **Arena Allocators**: Used for per-request allocations that can be bulk-deleted once the response is sent.
- **Slab Pools**: Used for fixed-size objects like internal key buffers and item metadata.
- **Smart Pointers**: `std::shared_ptr` and `std::unique_ptr` are used for long-lived components like storage engines and dispatchers.

## Expression Engine

A critical component is the expression engine, which allows for `ConditionExpression`, `UpdateExpression`, and `FilterExpression`.
1. **Lexer**: Tokenizes the expression string.
2. **Parser**: Builds an Abstract Syntax Tree (AST) representing the logic.
3. **Evaluator**: A high-performance visitor that applies the AST to an item's data.
4. **Optimization**: Commonly used expressions are cached in a pre-compiled bytecode format.

## Concurrency Model

cynamoDB utilizes an **Asynchronous I/O** model via Boost.Asio and a shared thread pool.
- **I/O Threads**: Manage socket connections and JSON parsing.
- **Worker Threads**: Execute the API logic, expression evaluation, and storage engine interactions.
- **Background Threads**: Handle LSM tree maintenance tasks like flushing and compaction.

## Networking

The networking layer is built on **Boost.Beast**, providing a robust, standards-compliant HTTP implementation.
- Support for chunked transfer encoding.
- Efficient buffer management to minimize copies during high-volume reads/writes.
- Transport-layer hardening (TLS ready, though often run behind a proxy).
