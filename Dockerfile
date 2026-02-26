# Build stage
FROM debian:12 AS builder

RUN apt-get update && apt-get install -y 
    build-essential 
    cmake 
    git 
    libboost-all-dev 
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCYNAMODB_LTO=ON
RUN cmake --build build -j$(nproc) --target cynamodb

# Runtime stage
FROM gcr.io/distroless/cc-debian12

WORKDIR /app
COPY --from=builder /app/build/cynamodb /app/cynamodb

EXPOSE 8000
ENTRYPOINT ["/app/cynamodb"]
