# ==================== Build Stage ====================
FROM gcc:13 AS builder

WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    build-essential \
    libboost-all-dev \
    zlib1g-dev \
    ca-certificates \
    git \
    python3 \
    && rm -rf /var/lib/apt/lists/*

COPY . .

WORKDIR /app/build
RUN cmake ../backend -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DUSE_BOOST_LOCKFREE=ON \
    -DUSE_SPDLOG=ON \
    -DUSE_PROMETHEUS=ON \
    -DBUILD_FRONTEND=ON

RUN cmake --build . -j$(nproc)

# ==================== Runtime Stage ====================
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libatomic1 \
    libstdc++6 \
    ca-certificates \
    curl \
    tini \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/trebuchet_backend /app/trebuchet_backend
COPY --from=builder /app/frontend /app/frontend
COPY --from=builder /app/config /app/config
COPY --from=builder /app/backend/cmake/gzip_frontend.cmake /app/cmake/

RUN mkdir -p /app/logs /app/data

EXPOSE 8080 8081 9000/udp

HEALTHCHECK --interval=30s --timeout=5s --start-period=10s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/app/trebuchet_backend"]
