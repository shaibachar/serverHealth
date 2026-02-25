# ── Stage 1: build ──────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy source tree
COPY CMakeLists.txt .
COPY src/ src/

# Configure and build (FetchContent will pull cpp-httplib from GitHub)
RUN cmake -B _build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build _build --parallel

# ── Stage 2: runtime ────────────────────────────────────────────────────────
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        docker.io \
    && rm -rf /var/lib/apt/lists/*

# Copy binary
COPY --from=builder /build/_build/serverhealth /usr/bin/serverhealth

# Copy web assets
COPY web/ /usr/share/serverhealth/

ENV WEB_ROOT=/usr/share/serverhealth
ENV PORT=9091

EXPOSE 9091

CMD ["/usr/bin/serverhealth"]
