# Security, Resilience, And Performance Improvements (Round 21)

The following 64 additional improvements were implemented in this round.

1. Added explicit `Connection` header max-size guard constant.
2. Added explicit `Upgrade` header max-size guard constant.
3. Increased hard upper bound for request header count from 128 to 256 (for controlled flexibility).
4. Added default total-header-bytes budget constant (`64 KiB`).
5. Added minimum total-header-bytes config bound (`4 KiB`).
6. Added maximum total-header-bytes config bound (`256 KiB`).
7. Added minimum configurable headers-per-request bound.
8. Added configurable read-timeout default constant.
9. Added minimum configurable read-timeout bound.
10. Added maximum configurable read-timeout bound.
11. Added transaction idempotency window min bound.
12. Added transaction idempotency window max bound.
13. Added transaction idempotency cache-size min bound.
14. Added transaction idempotency cache-size max bound.
15. Added upper bound for configurable client-request-token length.
16. Replaced transaction idempotency `size_t` payload hash with SHA256 payload digest storage.
17. Added `require_accept_json` control to auth/server runtime config.
18. Added `reject_upgrade_headers` control to auth/server runtime config.
19. Added `enforce_sigv4_auth_scheme` control to auth/server runtime config.
20. Added configurable max total header bytes to runtime config.
21. Added configurable max headers per request to runtime config.
22. Added configurable read-timeout seconds to runtime config.
23. Added transaction idempotency runtime config object.
24. Added typed environment parser for bounded `uint32` values.
25. Added `Accept` media-range parser that validates JSON allowance.
26. Added env toggle `CYNAMODB_REQUIRE_ACCEPT_JSON`.
27. Added env toggle `CYNAMODB_REJECT_UPGRADE_HEADERS`.
28. Added env toggle `CYNAMODB_ENFORCE_SIGV4_AUTH_SCHEME`.
29. Added env toggle `CYNAMODB_MAX_TOTAL_HEADER_BYTES`.
30. Added env toggle `CYNAMODB_MAX_HEADERS_PER_REQUEST`.
31. Added env toggle `CYNAMODB_READ_TIMEOUT_SECONDS`.
32. Added env toggle `CYNAMODB_TX_IDEMPOTENCY_WINDOW_SECONDS`.
33. Added env toggle `CYNAMODB_TX_IDEMPOTENCY_CACHE_ENTRIES`.
34. Added env toggle `CYNAMODB_MAX_CLIENT_REQUEST_TOKEN_BYTES`.
35. Added env toggle `CYNAMODB_TX_IDEMPOTENCY_CACHE_ERRORS`.
36. Added transaction idempotency config loader with strict bounds.
37. Added static transaction idempotency config accessor for stable runtime behavior.
38. Switched tx idempotency cache pruning from arbitrary erase to deterministic oldest-expiration eviction.
39. Added tx idempotency pruning to use configurable max-entries threshold.
40. Switched tx idempotency payload comparison to SHA256 digest comparison.
41. Kept prefix/suffix/size matching alongside digest for stronger mismatch detection.
42. Switched tx idempotency expiry window from fixed constant to configurable runtime window.
43. Made client request token length validation configurable at runtime.
44. Added optional tx idempotency caching of non-200 responses (feature-toggled).
45. Applied configurable read-timeout in `Session::do_read`.
46. Added per-request total-header-bytes accounting.
47. Enforced configurable header-count limit in request gate.
48. Enforced configurable total-header-bytes limit in request gate.
49. Standardized rejection message for header-budget violations.
50. Added explicit rejection for requests carrying both `Content-Length` and `Transfer-Encoding`.
51. Added optional strict JSON-acceptable `Accept` header enforcement.
52. Added fail-fast path for missing `Accept` when JSON-accept is required.
53. Added stricter `Content-Type` handling requiring canonical whitespace (no leading/trailing spaces).
54. Added default rejection of connection-upgrade attempts via `Connection` token.
55. Added default rejection of explicit `Upgrade` headers.
56. Added distinct validation path for malformed `Upgrade` header values.
57. Added authorization-scheme precheck before parser invocation for clearer failures.
58. Added explicit unauthorized error message for non-SigV4 auth schemes.
59. Added regression test: rejects upgrade via `Connection` header.
60. Added regression test: rejects explicit `Upgrade` header.
61. Added regression test: rejects unsupported authorization scheme.
62. Preserved and validated existing security-header standardization tests after request-gate changes.
63. Preserved and validated existing `x-amz-content-sha256` signature-integrity tests after idempotency and gate updates.
64. Re-ran full build/test suite after all hardening changes to guarantee behavioral stability.

## Files

- `src/http/server.cpp`
- `tests/test_http_security_standardization.cpp`
