# Phase 13: Networking: High-Throughput HTTP Stack & Pipelining

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
The HTTP layer is the entry point for all requests. A standard web server is not optimized for the high-frequency, low-latency traffic pattern of DynamoDB. This phase implements a custom, high-performance HTTP stack that supports keep-alive, pipelining, and (optionally) HTTP/2 to minimize connection overhead.

## Technical Definition
*   **Asynchronous I/O:** Use `epoll` (Linux) or `kqueue` (macOS/BSD) via an event loop (e.g., `libuv` or `asio`).
*   **Keep-Alive & Pipelining:** Support multiple requests per connection without waiting for individual responses.
*   **Zero-Copy Response:** Use `writev` or `sendfile` to send response headers and body without merging them into a single buffer.

## Reference Files
*   `include/cynamodb/http/server.hpp`
*   `src/http/server.cpp`
*   `src/main.cpp`

## Expanded Tasks
1.  **Event Loop Integration:** Implement `EventLoop` using `epoll_wait`. Each worker thread should run its own `EventLoop` and accept connections using `accept4(SOCK_NONBLOCK)`.
2.  **Socket Option Optimization:** Call `setsockopt` with `TCP_NODELAY=1` (disable Nagle's algorithm) and `TCP_QUICKACK=1`. Set `SO_SNDBUF` and `SO_RCVBUF` to 256KB to optimize for high-concurrency 4KB-64KB payloads.
3.  **HTTP/1.1 Parser Optimization:** Use `llhttp`. Configure it to use the `on_header_value` and `on_body` callbacks to extract `std::string_view`s directly from the socket's read buffer.
4.  **Keep-Alive Management:** Track `last_activity_time` for every connection. If idle for > 60s, close the socket. Allow up to 10,000 requests per connection before forcing a close to prevent memory fragmentation.
5.  **HTTP Pipelining Support:** The parser should not stop after one request. If the buffer has more data, continue parsing and push multiple `RequestContext` objects into the `Scheduler` queue for the same connection.
6.  **Response Buffer Pooling:** Use the `BufferPool` from Phase 01. When a response is generated, it's written into a pooled 64KB buffer. If the response is larger, use a chain of pooled buffers.
7.  **Header Compression (HPack):** (If HTTP/2) Implement a static HPack table for common DynamoDB headers like `application/x-amz-json-1.0`. This can reduce header size by 80%.
8.  **HTTP/2 Stream Management:** Implement `Http2Session`. It must handle `SETTINGS`, `HEADERS`, `DATA`, and `WINDOW_UPDATE` frames. Use a `std::map<uint32_t, Stream>` to track concurrent streams.
9.  **Write Batching (writev):** Implement `send_response()`. Use `struct iovec iov[2]`. `iov[0]` = headers, `iov[1]` = body. Call `writev()` once. This reduces the number of system calls by 50%.
10. **Zero-Copy Body Forwarding:** For `Scan` operations, if the data is already in a contiguous SSTable block in the `BlockCache`, pass that block's `span` directly to `writev` without copying it to a response buffer.
11. **Connection Limit Throttling:** Define `MAX_CONNECTIONS = 50000`. If `active_connections > MAX_CONNECTIONS`, immediately `close()` new connections without parsing any data.
12. **Slowloris Protection:** Set a `HEADER_TIMEOUT = 5s`. If a client doesn't send the full HTTP header (ending in `\r\n\r\n`) within this time, disconnect them.
13. **Dynamic Response Compression:** If the `Accept-Encoding: gzip` header is present and the body is > 1KB, use the `zlib` or `libvdeflate` (faster) library to compress the response in a background thread.
14. **Custom HTTP Errors:** If a request is too large (> 16MB), return `413 Payload Too Large` with a JSON body: `{"__type": "com.amazonaws.dynamodb.v20120810#RequestEntityTooLargeException"}`.
15. **Request ID Tracking:** Implement `generate_request_id()`. Use a UUID v4 or a high-speed custom generator (Counter + Random + Timestamp). Return it in the `x-amzn-RequestId` header.
16. **CORS Support:** If an `Origin` header is present, return `Access-Control-Allow-Origin: *` and handle `OPTIONS` pre-flight requests by returning a 200 OK with allowed methods/headers.
17. **Health Check Endpoint:** In the HTTP parser, if the path is `/health`, return a hard-coded `200 OK` response immediately, bypassing the dispatcher and auth logic.
18. **Socket Buffer Tuning:** Use `getsockopt(SO_ERROR)` to detect dropped packets. If errors occur, slightly reduce the `RCVBUF` size to throttle the incoming rate.
19. **Context-Aware Timeouts:** If a request has a `X-Amz-Target-Timeout`, set an internal timer. If the storage engine doesn't return in time, close the connection and return `504 Gateway Timeout`.
20. **Network Metrics:** Track `Metrics::BYTES_RECEIVED`, `Metrics::BYTES_SENT`, and `Metrics::ACTIVE_CONNECTIONS`. Log these every 1 second.
21. **Load Balancing Affinity:** Use `SO_REUSEPORT` on the listen socket. This allows each worker thread to have its own `accept` queue, eliminating the "Thundering Herd" problem.
22. **Graceful Connection Draining:** During shutdown, stop the `accept` loop. Send `Connection: close` on all new responses. Wait for `active_connections` to hit 0 (or a 10s timeout) before exiting.
23. **Test Coverage - Pipelining:** In `tests/test_http.cpp`, use a raw TCP socket to send 100 `GetItem` requests in a single `send()` call. Verify all 100 responses are received correctly.
24. **Test Coverage - Large Payload:** Send a 20MB `BatchWriteItem` request and verify the server correctly returns a `413` error without crashing or running out of memory.
25. **Validation:** Use `wrk -t12 -c400 -d30s`. Verify that the server maintains > 100k requests/sec for simple `GetItem` calls.

## Validation Criteria
*   **Throughput:** HTTP server saturates a 10GbE link on a single modern server.
*   **Latency:** HTTP parsing and framing overhead is < 10 microseconds.
*   **Robustness:** Passes the "Slowloris" and "GoldenEye" HTTP stress tests.
