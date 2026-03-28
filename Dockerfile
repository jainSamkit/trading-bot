# ── Stage 1: Build ─────────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Boost headers required for Beast/WebSocket; libssl for TLS.
RUN apt-get update && apt-get install -y \
    cmake \
    make \
    g++ \
    libssl-dev \
    libboost-dev \
    liburing-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /trading-bot

COPY . .

# PORTABLE_RELEASE=ON avoids -march=native and -flto (common Docker build failures).
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPORTABLE_RELEASE=ON \
    && cmake --build build --parallel $(nproc)

# ── Stage 2: Runtime ───────────────────────────────────────────────────────────
# Optional slim image (binary only). For day-to-day dev use docker-compose.yml
# (builder target + host mount at /trading-bot).
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y \
    libssl3 \
    liburing2 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /trading-bot

COPY --from=builder /trading-bot/build/trading_bot .

CMD ["./trading_bot"]
