# Security Model and Compliance

cynamoDB is designed for local development and testing, and its security model reflects this intended use case.

## Authentication & Authorization

### Signature Version 4 (SigV4)
cynamoDB supports official AWS SigV4 authentication. This allows it to work with standard AWS SDKs without modification.
- **Enforcement**: By default, SigV4 is enabled.
- **Bypass**: You can run the server with `--no-auth` to disable signature verification for local testing.
- **Stub IAM**: There is no actual IAM service connected. The engine validates that the signature is correct for the provided access key but does not evaluate complex resource-based policies.

### Credential Configuration
Credentials can be provided via environment variables (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`) or via a local configuration file if supported by your deployment method.

## Data Protection

### Transport Security (TLS)
cynamoDB serves content over HTTP by default. For production-like environments or when exposing the service over a network, it is highly recommended to use a reverse proxy (like Nginx, HAProxy, or Caddy) to terminate TLS.

### Encryption at Rest
cynamoDB does not natively encrypt SSTables on disk. If encryption at rest is required for the host environment, use filesystem-level encryption (e.g., LUKS on Linux or FileVault on macOS).

## Security Hardenings (Recent Rounds)

The project underwent several rounds of security and resilience hardening:
- **Round 20-25**: Implemented 300+ hardening items including buffer overflow protections, input validation sanitization, and transport hardening.
- **Round 26**: Focused on Stream-ARN and SigV4 scope-hardening to prevent unauthorized cross-table access.

## Compliance Mapping

cynamoDB maps to the standard DynamoDB security features as follows:

| Feature | DynamoDB (AWS) | cynamoDB (Local) |
|---------|----------------|------------------|
| Auth | IAM Users/Roles | SigV4 Stub / No-Auth |
| Encryption | AWS KMS (Managed) | Host Filesystem Encryption |
| Audit | CloudTrail | Structured JSON Logs |
| VPC | VPC Endpoints | Local Network Only |

## Responsibility
As a local-first engine, the security of the data handled by cynamoDB is primarily the responsibility of the environment host. Do not store sensitive production data in a local cynamoDB instance without appropriate system-level protections.
