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

// ── CPU pinning layout ───────────────────────────────────────────────────────
// Logical CPU IDs as reported by /proc/cpuinfo / taskset.
//
// Target: c7gn.2xlarge (8 physical Graviton cores, no SMT) in AWS Tokyo.
// Cores 0-1 are NOT isolated — they run the OS, IRQs, and the background
// InfluxWriter push threads. Hot threads go on isolated cores 2-7.
//
// Boot the host with:
//   isolcpus=2,3,4,5,6,7 nohz_full=2,3,4,5,6,7 rcu_nocbs=2,3,4,5,6,7
// and move IRQs to cores 0-1 via /proc/irq/*/smp_affinity.
//
// On c7i.2xlarge (4 physical Intel + HT), cores 6,7 don't exist — adjust
// down to a 4-physical layout if you fall back to Intel.
namespace cfg::cpu {
    inline constexpr int FEED_REACTOR_CORE     = 2;   // WS read + frame decode + parse
    inline constexpr int FEED_MARKET_CORE      = 3;   // MarketState::run busy-spin
    inline constexpr int STRATEGY_CORE         = 4;   // Strategy::run busy-spin
    inline constexpr int OMS_EXEC_CORE         = 5;   // ExecutionManager::run busy-spin
    // 6, 7 reserved for Bybit feed (reactor + market_state) once that lands.

    // Cores that the OS, IRQs, and background threads (e.g. InfluxWriter push)
    // run on. NOT in the isolcpus set — kernel CFS schedules them normally.
    inline constexpr int HOUSEKEEPING_CORE_LO  = 0;
    inline constexpr int HOUSEKEEPING_CORE_HI  = 1;

    // SCHED_FIFO priority for hot threads. Requires CAP_SYS_NICE (root on EC2).
    inline constexpr int RT_PRIORITY           = 50;
}