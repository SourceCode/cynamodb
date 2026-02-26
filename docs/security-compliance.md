# DynamoDB Security Compliance Mapping

Reference: <https://docs.aws.amazon.com/amazondynamodb/latest/developerguide/security.html>

## Implemented

- SigV4 request authentication with canonical-request signature verification.
- Clock-skew validation for `x-amz-date`.
- Optional payload-hash verification via `x-amz-content-sha256`.
- Optional session-token validation for temporary credentials.
- Per-access-key action allowlists (least-privilege style policy control).
- Optional TLS enforcement signal (`x-forwarded-proto: https`) for reverse-proxy deployments.
- Strict duplicate-header and signed-header validation.
- Standardized defensive response headers (`nosniff`, `DENY`, strict CSP, no-store caching).
- Upgrade/header-budget hardening (`Connection`/`Upgrade` rejection, header count/bytes limits).
- Transaction idempotency hardening with SHA256 payload digest matching and bounded cache controls.
- Streams iterator hardening with bounded token/state controls and stricter ARN/input validation.
- Backup snapshot/metadata hardening with strict type/status validation and snapshot-size guardrails.
- WAL/replay hardening with bounded key/value record parsing, NUL-byte payload rejection, and deterministic truncate-to-good-end recovery behavior.
- SSTable/compaction and table-metadata hardening with strict identifier validation, duplicate-entry rejection, and fail-closed path normalization checks.
- Streams and backup subsystems now enforce strict schema/field allowlists, scoped ARN consistency checks, and iterator/snapshot fail-closed validation.
- SigV4 datetime validation now enforces full calendar/time component ranges, and cache internals are hardened for safer bounded behavior under churn.

## Partially Implemented (Local Equivalent)

- IAM policy model:
  - AWS IAM is not available in local standalone mode.
  - Local replacement is environment-configured access-key allowlists.

## Not 1:1 in Local Runtime

- AWS KMS integration and managed key lifecycle.
- Native AWS CloudTrail integration.
- VPC endpoint policies / PrivateLink controls.
- AWS Organizations / SCP integration.

These items require AWS control-plane infrastructure and cannot be fully replicated in a standalone local process.
