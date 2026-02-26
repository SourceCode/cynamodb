# Security / Resilience / Performance Improvements Round 26

This round implements additional fail-closed validation in stream-ARN handling and SigV4 scope verification paths.

1. Added explicit region-token bounds/constants for stream-ARN parsing.
2. Added explicit AWS account-id length constant for stream-ARN parsing.
3. Added `is_valid_region_token` helper for stream ARN region segments.
4. Added `is_valid_account_id` helper for strict 12-digit account-id validation.
5. Added `ParsedStreamArn` typed structure to avoid ad-hoc ARN string handling.
6. Added `parse_stream_arn` helper for strict DynamoDB stream-ARN structure validation.
7. `is_valid_stream_arn` now uses structured parsing instead of loose prefix matching.
8. Added `stream_arn_matches_table_and_label` helper to enforce ARN/table/label consistency.
9. `sync_table` now rejects stream ARNs whose embedded table name does not match the table definition.
10. `sync_table` now rejects stream ARNs whose embedded stream label does not match `latest_stream_label`.
11. `append_record` now fail-closes when stream state ARN/table/label invariants are inconsistent.
12. `list_streams` now skips stream states with ARN/table/label mismatches.
13. `describe_stream` now fails closed when stored stream state contains ARN/table/label mismatches.
14. `create_shard_iterator` now fails closed when stored stream state has invalid or mismatched ARN/table/label values.
15. SigV4 parser now rejects `authorization` being included in `SignedHeaders`.
16. SigV4 verifier now re-validates parsed scope invariants (`access_key`, date, region, service, request type, signature format).
17. SigV4 verifier now enforces sorted/valid signed-header tokens even when parameters are caller-supplied.
18. SigV4 verifier now enforces required signed headers (`host`, `x-amz-date`, `x-amz-target`, `content-type`) and rejects `authorization` in `SignedHeaders`.
19. SigV4 verifier now enforces `Credential` scope date equality with `x-amz-date`.
20. SigV4 verifier now validates signed `x-amz-content-sha256` value format and payload-hash equality when present.
21. Added regression test: stream sync rejects ARNs with mismatched table name.
22. Added regression test: stream sync rejects ARNs with mismatched stream label.
23. Added regression test: SigV4 parser rejects `authorization` in `SignedHeaders`.
24. Added regression test: SigV4 verifier rejects mismatched credential-scope date.
25. Added regression test: SigV4 verifier rejects mutated/non-dynamodb service scope.
