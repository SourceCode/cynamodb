# Phase 24: Deployment: Containerization, Orchestration, and CI/CD

> Execution Contract: Follow [`AGENT_EXECUTION_CONTRACT.md`](AGENT_EXECUTION_CONTRACT.md). A phase is complete only when all contract gates pass and a report is added under `docs/v2_docs/reports/`.


## Context & Objectives
To be "Production Ready," CynamoDB must be easy to deploy, monitor, and scale. This phase focuses on building a high-performance Docker image, implementing Kubernetes orchestration (Helm charts), and setting up a robust CI/CD pipeline that enforces performance and security standards.

## Technical Definition
*   **Distroless Docker:** Use a minimal base image to reduce the attack surface and image size.
*   **Kubernetes Operator/Helm:** Provide standard deployment patterns for clustering and high availability.
*   **CI/CD Pipeline:** Automate builds, unit tests, integration tests, and performance benchmarks.

## Reference Files
*   `Dockerfile`
*   `docker-compose.yml`
*   `charts/cynamodb/`
*   `.github/workflows/ci.yml`

## Expanded Tasks
1.  **Multi-Stage Dockerfile:** Implement `Dockerfile` with 2 stages: 1. `builder` (using `gcc:13` or `clang:17`). 2. `runtime` (using `gcr.io/distroless/cc-debian12`). This keeps the final image size minimal and secure.
2.  **Optimized Compilation:** In the `builder` stage, use `cmake -DCMAKE_BUILD_TYPE=Release -DCYNAMODB_LTO=ON -DCYNAMODB_ARCH=x86-64-v3`. Ensure the binary is statically linked against `libgcc` and `libstdc++`.
3.  **Docker-Compose Environment:** Implement `docker-compose.yml`. Include `cynamodb`, `prometheus`, `grafana`, and `otel-collector`. Use a shared volume for the `/data` directory.
4.  **Kubernetes Helm Chart:** Implement `templates/deployment.yaml`. Use a `StatefulSet` instead of a `Deployment` to ensure stable network IDs and persistent volumes. Support `affinity` and `tolerations` for cloud deployments.
5.  **Health/Liveness Probes:** In the Helm chart, configure `livenessProbe` and `readinessProbe` to call the `/health` endpoint. Use `initialDelaySeconds: 5` and `periodSeconds: 10`.
6.  **Configuration Management:** Implement `ConfigLoader`. It must look for `CYNAMO_` environment variables first, then a YAML file at `/etc/cynamodb/config.yaml`. Use `schema-validation` for the YAML config.
7.  **Secrets Management:** Support `CYNAMO_SSL_CERT_PATH` and `CYNAMO_SSL_KEY_PATH`. In Kubernetes, mount these from a `Secret` volume.
8.  **Automated CI Build:** Implement `.github/workflows/ci.yml`. It must run on every push to `main`. Include steps for `vcpkg install`, `cmake build`, and `ctest`.
9.  **Static Analysis (SAST):** Add `clang-tidy` to the CI. Enforce rules for "Modern C++" and "Performance". Fail the build if any `performance-` or `bugprone-` warnings are found.
10. **Security Scanning:** Add `trivy image cynamodb:latest` to the CD pipeline. Reject images with "Critical" CVEs in the base libraries.
11. **Performance Regression Gate:** Implement `bench_gate.py`. It runs `bench_performance`, compares the results to `baseline.json` in the repo, and fails if any P99 latency increases by > 5%.
12. **Automated Documentation:** Use `Doxygen` and `Mkdoxy`. Deploy the generated documentation to `GitHub Pages` automatically after every successful build on `main`.
13. **Release Tagging:** Use `semantic-release`. It automatically calculates the next version (e.g., `2.1.0`) based on commit messages (using Conventional Commits).
14. **Monitoring Dashboard:** Export the Grafana dashboard JSON from the project. Include panels for "RPS", "P99 Latency", "MemTable Count", and "WAL Disk Throughput".
15. **Prometheus Alert Rules:** Create `alerts.yaml`. Include: `CynamoDBHighLatency` (P99 > 50ms for 5m) and `CynamoDBDiskFull` (Usage > 85%).
16. **Log Rotation & Forwarding:** Configure the engine to log to `stdout` in JSON format: `{"level": "info", "msg": "Compaction finished", "table": "User"}`. This is easily parsed by `FluentBit`.
17. **Resource Limits Tuning:** In the Helm chart, set `requests: {cpu: 2, memory: 4Gi}` and `limits: {cpu: 4, memory: 8Gi}` as the recommended baseline for a medium-scale deployment.
18. **Network Policy:** Implement `network-policy.yaml`. Only allow traffic to port 8000 from the `api-gateway` namespace and to port 9090 from the `monitoring` namespace.
19. **Snapshot/Backup Automation:** Provide a `CronJob` that calls `curl -X POST /admin/backup` every night at 2 AM.
20. **Multi-Arch Builds:** Use `docker buildx`. Build for `linux/amd64` and `linux/arm64`. This allows running CynamoDB on AWS Graviton instances for 40% better price-performance.
21. **Binary Distribution:** Implement `scripts/package_release.sh`. It should bundle the binary, the default config, and a `systemd` unit file into a `.tar.gz` for non-Docker deployments.
22. **Upgrade Path Testing:** Create an integration test: 1. Deploy v1.0 image. 2. Write data. 3. Upgrade to v2.0 image. 4. Verify data is still correct and GSIs are active.
23. **Graceful Shutdown:** Implement `SignalHandler`. On `SIGTERM`, it calls `Engine::shutdown()`, which flushes all MemTables to SSTables and closes all file handles before the process exits.
24. **Cloud-Init Scripts:** Provide a Terraform module that launches an EC2 instance, formats an NVMe disk for XFS, and starts CynamoDB as a `systemd` service.
25. **Validation:** Run a "Full Lifecycle" test: Build -> Deploy to K8s -> Run YCSB -> Check Grafana -> Tear Down.

## Validation Criteria
*   **Size:** Docker image size is < 100MB.
*   **Security:** Zero "High" or "Critical" vulnerabilities detected by image scanners.
*   **Automation:** Full build-test-deploy cycle is completed in < 15 minutes.
