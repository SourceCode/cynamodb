# Security and Standardization Improvements (Round 18)

Implemented improvements in this round:

1. SigV4 parser now rejects authorization headers with leading/trailing whitespace.
2. SigV4 parser now rejects non-ASCII bytes in the full authorization header.
3. SigV4 parser now rejects control characters anywhere in the authorization header.
4. SigV4 parser now enforces exact `AWS4-HMAC-SHA256 ` prefix format (algorithm + single space).
5. SigV4 parser now rejects tab characters in authorization headers.
6. SigV4 parser now uses strict comma-delimited parameter tokenization.
7. SigV4 parser now rejects empty parameter segments.
8. SigV4 parser now rejects malformed parameter pairs that do not match `name=value`.
9. SigV4 parser now validates parameter names as alnum/hyphen tokens starting with a letter.
10. SigV4 parser now trims parameter name/value whitespace before validation.
11. SigV4 parser now unquotes quoted parameter values consistently.
12. SigV4 parser now rejects empty parameter values after trimming/unquoting.
13. SigV4 parser now rejects duplicate authorization parameter names.
14. SigV4 parser now rejects unknown authorization parameters.
15. SigV4 parser now enforces exact required parameter cardinality (`Credential`, `SignedHeaders`, `Signature`).
16. SigV4 parser now enforces max authorization parameter count through structured parsing.
17. SigV4 parser now enforces max credential scope byte length before splitting.
18. SigV4 parser now requires credential scope to contain exactly 5 slash-delimited components.
19. SigV4 parser now rejects credential scopes containing empty components.
20. SigV4 parser now enforces stricter access-key format (uppercase alnum only).
21. SigV4 parser now enforces stricter region format (lowercase alnum/hyphen only).
22. SigV4 parser now rejects region values that start/end with hyphen.
23. SigV4 parser now rejects region values containing consecutive hyphens.
24. SigV4 parser now enforces region minimum length alongside max length.
25. SigV4 parser now requires canonical service value `dynamodb`.
26. SigV4 parser now requires canonical request type `aws4_request`.
27. SigV4 parser now enforces signed-headers payload byte cap before token expansion.
28. SigV4 parser now validates signed-header tokens as lowercase alnum/hyphen and disallows edge hyphens.
29. SigV4 parser now enforces strict lexical ordering for signed headers (sorted + unique).
30. SigV4 parser now enforces signed-header token count cap under strict parsing.
31. SigV4 parser now enforces signed-header token byte-length cap per header token.
32. SigV4 parser now requires signed headers to include `host`.
33. SigV4 parser now requires signed headers to include `x-amz-date`.
34. SigV4 parser now requires signed headers to include `x-amz-target`.
35. SigV4 parser now requires signed headers to include `content-type`.
36. SigV4 parser now enforces minimum signed-header count for canonical required set.
37. SigV4 parser now enforces 64-byte signature length via structured parameter extraction.
38. SigV4 parser now enforces lowercase hex signature canonicalization.
39. API dispatcher now centralizes the DynamoDB target prefix in a single constant.
40. API dispatcher now uses a shared operation-name validator to standardize checks.
41. API dispatcher operation-name validation now enforces alnum-only payloads.
42. API dispatcher operation-name validation now requires at least one lowercase character.
43. API dispatcher now explicitly rejects operation names containing additional dot separators.
44. API dispatcher now has a compile-time sortedness assertion for operation lookup table integrity.
45. TableManager default metadata filename standard is now `metadata.bin`.
46. Runtime `Context` now uses `metadata.bin` for metadata path standardization.
47. Metadata path validation now enforces non-empty filename and filename-size cap.
48. Metadata path validation now enforces a strict allowed-extension set (`.bin`, `.json`, `.bak`, `.corrupt`).
49. TableManager constructor now rejects existing metadata paths that are not regular files.
50. Table primary-key schema max entry count is now strictly bounded to 2.
51. Table primary-key schema now requires `HASH` key to appear first.
52. Table primary-key schema now allows `RANGE` only as second key component.
53. Table primary-key schema now rejects non-scalar key attribute types for key fields.
54. Secondary-index key schema max entry count is now strictly bounded to 2.
55. Secondary-index key schema now requires `HASH` key to appear first.
56. Secondary-index key schema now allows `RANGE` only as second key component.
57. Secondary-index key schema now rejects non-scalar key attribute types for index key fields.
58. LSI validation now requires a `RANGE` key in the index key schema.
59. Projection validation now enforces explicit projection-type allowlist.
60. Projection validation now requires non-empty `non_key_attributes` only for `INCLUDE`.
61. Projection validation now rejects non-empty `non_key_attributes` for `ALL` and `KEYS_ONLY`.
62. Projection validation now requires projected attributes to exist in attribute definitions.
63. Projection validation now rejects projection attributes that overlap with table/index key attributes.
64. Metadata deserialization paths (`legacy`/`v2`/`v3`) and save/create paths now revalidate key-shape and TTL numeric-reference invariants before accepting or persisting schema state.

Additional runtime parity hardening implemented in this pass:

- LSM engine key-schema validation now mirrors strict table-manager ordering/type/cardinality rules.
- LSM engine now validates secondary-index schema shape/type consistency before write/read/query/scan paths.
- LSM get/delete/query key maps now enforce stronger key-attribute type matching against schema definitions.
- MemTable write path now rejects malformed item payloads via attribute-map, identifier, numeric-format, and recursive variant/type validation.
