# Build stage
FROM debian:12 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCYNAMODB_LTO=ON
RUN cmake --build build -j$(nproc) --target cynamodb

# Runtime stage.
# Use the ":debug" distroless variant: it adds a busybox shell + wget on top of the same
# minimal cc base, which is what lets orchestrators health-check the container. Plain
# distroless has no shell/curl/wget, so a `curl /health` healthcheck can never run.
FROM gcr.io/distroless/cc-debian12:debug

WORKDIR /app
COPY --from=builder /app/build/cynamodb /app/cynamodb

EXPOSE 8000

# Container-native readiness probe against the built-in /health endpoint (busybox wget,
# exec form — no shell needed). Compose / Kubernetes can rely on this or override it.
HEALTHCHECK --interval=15s --timeout=5s --start-period=15s --retries=5 \
    CMD ["wget", "-q", "-O", "/dev/null", "http://localhost:8000/health"]

ENTRYPOINT ["/app/cynamodb"]
