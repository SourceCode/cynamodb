# Phase 12: Security: SigV4 Acceleration & TLS Optimization

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
AWS SigV4 authentication involves multiple rounds of HMAC-SHA256 hashing. In a high-throughput scenario, this can become a significant CPU bottleneck. This phase optimizes the SigV4 implementation using SIMD-accelerated hashing, zero-copy header parsing, and TLS session resumption to reduce the "time to first byte."

## Technical Definition
*   **SIMD Hashing:** Use SHA-NI (SHA Extensions) or AVX2 for HMAC-SHA256 calculations.
*   **Zero-Copy Auth:** Extract signature components directly from the HTTP headers as `std::string_view`.
*   **TLS Optimization:** Optimize OpenSSL/BoringSSL for fast handshakes and minimal overhead.

## Reference Files
*   `src/auth/sigv4.cpp`
*   `include/cynamodb/auth/sigv4.hpp`
*   `src/http/server.cpp`

## Expanded Tasks
1.  **SHA-NI Integration:** Implement `sha256_shani()`. Use `_mm_sha256msg1_epu32` and related intrinsic instructions. Verify support at runtime via `cpuid`. This can speed up hashing by 3x-5x compared to software implementations.
2.  **AVX2 Fallback:** Implement `sha256_avx2()`. Use 256-bit registers to process 8 hashing rounds in parallel (interleaving). This is ideal for machines without SHA-NI (e.g., older Xeon/Epyc).
3.  **HMAC-SHA256 Parallelization:** Refactor the SigV4 pipeline. While the payload hash is being calculated on one core, the credential string and signing key can be calculated on another core via the `Scheduler`.
4.  **Zero-Copy Header Extraction:** Update the `SigV4Parser`. Instead of `std::string`, use `std::string_view` for `AccessKey`, `CredentialScope`, `SignedHeaders`, and `Signature`. Ensure the lifetime of these views matches the request buffer.
5.  **Canonical Request Optimizer:** Implement `build_canonical_request(ctx)`. Use a `std::pmr::string` from the `RequestContext` arena. Reserve 1KB of space upfront to avoid reallocations during string construction.
6.  **HTTP Header Normalization:** Implement `fast_normalize_header(name)`. Convert to lowercase and strip whitespace using SIMD `_mm256_sub_epi8` (to shift 'A'-'Z' to 'a'-'z' if within range).
7.  **Credential Cache:** Implement `SigningKeyCache`. Key: `hash(AccessKey + Date + Region + Service)`. Value: The 32-byte derived signing key. Use a `std::shared_mutex` for thread-safe access.
8.  **Query Parameter Sorting:** Use `std::sort` with a custom comparator that uses `std::string_view::compare` for query parameters. Perform this in-place within the canonical request buffer if possible.
9.  **Payload Hash Skipping:** Check for `x-amz-content-sha256: UNSIGNED-PAYLOAD`. If present, skip the `SHA256(Body)` step entirely, which is a major win for large `PutItem` requests.
10. **Pre-calculated Region/Service Hash:** At engine startup, if the `Region` and `Service` (dynamodb) are fixed, pre-calculate the first few rounds of the HMAC chain and store them as constants.
11. **TLS 1.3 Prioritization:** In `server.cpp`, use `SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION)`. TLS 1.3 reduces the handshake to a single round-trip.
12. **TLS Session Resumption:** Configure `SSL_CTX_set_session_cache_mode` to use `SSL_SESS_CACHE_SERVER`. Use `SSL_CTX_set_tlsext_ticket_key_cb` for stateless session tickets.
13. **Cipher Suite Hardening:** Use `SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256")`. These are highly optimized for modern CPUs.
14. **BoringSSL Integration:** If performance is the priority, update `CMakeLists.txt` to link against `libcrypto.a` from BoringSSL, which includes better SIMD optimizations for SHA and AES than standard OpenSSL.
15. **Signature V4-Streaming:** Implement `parse_chunk_signature()`. This is required for clients that use chunked encoding to send large bodies with per-chunk signatures.
16. **Time-Drift Tolerance:** In `validate_timestamp()`, compare `x-amz-date` against `now()`. If the delta > 300s, return `AccessDeniedException: Request has expired`.
17. **Access Key Validation Cache:** Use an internal `UserStore` (a sharded hash map) to quickly verify that an `AccessKeyId` is active and retrieve its `SecretAccessKey`.
18. **Identity-Aware Throttling:** Before parsing the body, check if the authenticated `Identity` has exceeded its global request limit (e.g., 10k req/sec) and return 429 early.
19. **Secure Memory Zeroing:** Implement `secure_zero(span)`. Use `volatile` pointers or `std::memset` wrapped in a function the compiler cannot elide, ensuring keys are wiped after use.
20. **Audit Logging:** Implement `AsyncAuditLogger`. It writes `{timestamp, access_key, ip, status, reason}` to a ring-buffer. A background thread flushes this to a file or a remote logging service.
21. **SigV4a (Asymmetric SigV4):** (Advanced) Implement the ECDSA-based signature validation if multi-region support is required for the project.
22. **Auth Performance Test:** Measure `SigV4::validate()` using `google-benchmark`. Target: < 10ms for a 64KB payload.
23. **Security Fuzzing:** Use `tests/fuzz_auth.cpp`. Generate random `Authorization` headers. Ensure no crashes even with extremely long or malformed header values.
24. **Identity Propagation:** Add an `Identity` object to the `RequestContext`. This object stores the user's ARN, AccountID, and permissions for the storage engine to check.
25. **Validation:** Run the "AWS SigV4 Test Suite" (a set of standard requests and their expected canonical strings and signatures) to ensure 100% compliance.

## Validation Criteria
*   **Latency:** SigV4 validation (excluding TLS) takes < 25 microseconds for a 1KB request.
*   **Scalability:** The authentication layer can handle 50k+ requests/sec on a 16-core machine.
*   **Security:** Successfully rejects malformed signatures, replayed requests, and expired credentials.
