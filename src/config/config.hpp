#pragma once
#include <cstddef>

// ── Ring + storage sizing ────────────────────────────────────────────────────
namespace cfg {
    inline constexpr size_t MAX_FEED_LEVELS      = 16;
    inline constexpr size_t SNAPSHOT_DEPTH       = 10;
    inline constexpr size_t TRADE_RING_SIZE      = 64;
    inline constexpr size_t INTENT_RING_SIZE     = 64;
    inline constexpr size_t FEED_RING_SIZE       = 64;
    inline constexpr size_t MAX_INSTRUMENTS      = 64;
    inline constexpr size_t OMS_RING_SIZE        = 64;
}

namespace cfg::execution_manager {
    inline constexpr size_t CONN_POOL_SIZE       = 5;
}

// ── Delta Exchange hosts ─────────────────────────────────────────────────────
// Public info — defaults only. Real values come from .env via env::get_or:
//   DELTA_REST_HOST, DELTA_WS_PUBLIC_HOST, DELTA_WS_PRIVATE_HOST
// Credentials (API_KEY / API_SECRET) do NOT live here — they're env-only.
namespace cfg::delta_exchange::testnet {
    inline constexpr const char* REST_HOST       = "cdn-ind.testnet.deltaex.org";
    inline constexpr const char* WS_HOST_PUBLIC  = "socket-ind-pub.testnet.deltaex.org";
    inline constexpr const char* WS_HOST_PRIVATE = "socket-ind.testnet.deltaex.org";
}

namespace cfg::delta_exchange::prod {
    inline constexpr const char* REST_HOST       = "api.india.delta.exchange";
    inline constexpr const char* WS_HOST_PUBLIC  = "public-socket.india.delta.exchange";
    inline constexpr const char* WS_HOST_PRIVATE = "socket.india.delta.exchange";
}

// ── Latency module ───────────────────────────────────────────────────────────
namespace latency {
    inline constexpr int PUSH_INTERVAL_SECONDS = 10;
}