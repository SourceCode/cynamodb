# Security And Standardization Improvements (Round 20)

The following 64 improvements were implemented in this round.

1. Enforced `AKIA`/`ASIA` access-key prefix validation in SigV4 parser.
2. Added explicit SigV4 algorithm invariant check in verifier (`AWS4-HMAC-SHA256`).
3. Added signed-header count bound checks in verifier.
4. Added secret-key ASCII and max-length validation in verifier.
5. Enforced exact signed-header count match between parsed authorization and request map.
6. Enforced uppercase HTTP method invariant in canonical-request verification.
7. Enforced canonical URI invariant: must start with `/`.
8. Enforced canonical URI invariant: no query/fragment characters.
9. Enforced canonical URI/query printable-ASCII checks.
10. Added signed-header map non-empty and max-size validation in canonicalization path.
11. Enforced strict `x-amz-date` structural format validation in verifier.
12. Exposed reusable `sha256_hex_digest` helper for payload-integrity checks.
13. Added unit test for `AKIA`/`ASIA` access-key enforcement.
14. Added unit test for uppercase-method verifier enforcement.
15. Added unit test for deterministic SHA256 helper behavior.
16. Added `CYNAMODB_POLICY_DEFAULT_DENY` configuration toggle.
17. Added `CYNAMODB_DISABLE_DEFAULT_CREDENTIALS` configuration toggle.
18. Added `CYNAMODB_STRICT_HTTP11` configuration toggle.
19. Added `CYNAMODB_REQUIRE_CONTENT_LENGTH` configuration toggle.
20. Added `CYNAMODB_REJECT_CHUNKED_TRANSFER` configuration toggle.
21. Added `CYNAMODB_REJECT_EXPECT_HEADER` configuration toggle.
22. Added `CYNAMODB_REQUIRE_ACCEPT_HEADER` configuration toggle.
23. Added `CYNAMODB_REQUIRE_USER_AGENT` configuration toggle.
24. Added `CYNAMODB_REQUIRE_AMZ_CONTENT_SHA256` configuration toggle.
25. Added `CYNAMODB_MAX_REQUEST_BODY_BYTES` configuration toggle with strict bounds parsing.
26. Added bounded numeric environment parser (`from_chars`) for size config.
27. Added bounded key/value limits for semicolon-delimited env list parsing.
28. Tightened server-side access-key validation to `AKIA`/`ASIA` prefixes.
29. Added minimum secret-key length enforcement (16 bytes).
30. Added session-token validator with ASCII and max-length constraints.
31. Applied session-token validation to `CYNAMODB_SESSION_TOKEN`.
32. Applied session-token validation to `CYNAMODB_SIGV4_SESSION_TOKENS`.
33. Added support for full target-form actions in policy parsing (e.g., `DynamoDB_20120810.GetItem`).
34. Added default-deny policy fallback when policy-default-deny mode is enabled.
35. Extended operation authorization checks to accept both action name and full target forms.
36. Centralized JSON response construction for consistent protocol behavior.
37. Standardized response header `cache-control: no-store`.
38. Standardized response header `pragma: no-cache`.
39. Standardized response header `x-content-type-options: nosniff`.
40. Standardized response header `x-frame-options: DENY`.
41. Standardized response header `referrer-policy: no-referrer`.
42. Standardized response header `content-security-policy: default-src 'none'; frame-ancestors 'none'`.
43. Added conditional `strict-transport-security` when TLS requirement is enabled.
44. Added strict HTTP/1.1 request-version enforcement in request pipeline.
45. Switched request-body max enforcement to config-driven value.
46. Expanded duplicate-sensitive-header detection to include `x-amz-content-sha256`.
47. Expanded duplicate-sensitive-header detection to include `content-length`.
48. Expanded duplicate-sensitive-header detection to include `accept`.
49. Expanded duplicate-sensitive-header detection to include `expect`.
50. Expanded duplicate-sensitive-header detection to include `transfer-encoding`.
51. Added required-content-length enforcement path (configurable).
52. Added strict `Content-Length` syntax validation and integer parsing.
53. Added `Content-Length` to body-size consistency enforcement.
54. Added `Content-Length` upper-bound enforcement against configured request-body cap.
55. Added `Transfer-Encoding` value validation with bounded length checks.
56. Added chunked transfer-encoding rejection path (configurable).
57. Added `Expect` header validation with explicit rejection response (`417` when enabled).
58. Added `Accept` header syntax/length validation.
59. Added configurable required `Accept` header enforcement.
60. Added `User-Agent` header syntax/length validation.
61. Added configurable required `User-Agent` header enforcement.
62. Added strict `x-forwarded-proto` validation to only allow `http`/`https`.
63. Added `x-amz-content-sha256` format validation (64-char lowercase hex).
64. Added `x-amz-content-sha256` integrity checks:
   - Must be signed when present and SigV4 verification is enabled.
   - Must match computed SHA256 hash of the request payload.

## Files

- `src/http/server.cpp`
- `src/auth/sigv4.cpp`
- `include/cynamodb/auth/sigv4.hpp`
- `tests/test_http_security_standardization.cpp`
- `tests/test_auth.cpp`
