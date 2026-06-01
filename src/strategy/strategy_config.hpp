#pragma once
#include <cstdint>
#include "core/tick.hpp"

namespace strategy {

    struct Config {
        // Note (2026-05-28): widened for the AWS Tokyo latency-measurement
        // runs. Original value (Tick{10}) was too tight for Delta BTCUSD —
        // typical $5 spread × $0.5 tick = 10 ticks → SkipWideSpread on every
        // snapshot → strategy never recorded queue_time / pushed intents →
        // tick_to_trade / wire_out / queue_time(exec) histograms stayed empty.
        // For the latency measurement we don't care about realistic spread
        // filtering; we just want every snapshot to reach the quoter so the
        // full pipeline is exercised end-to-end. Restore to a realistic value
        // when wiring the actual microprice/OFI quoter.
        inline static constexpr Tick max_spread_ticks               = Tick{10000};
        inline static constexpr Tick half_spread_ticks              = Tick{10};
        // Lowered from 10 → 1 for the latency-measurement run: every tick of
        // mid movement triggers a requote intent, so we generate ~50× more
        // tick_to_trade samples per minute. With n=1000+ the p99 histogram
        // becomes statistically meaningful (was n=20 → single-bucket artifact).
        // Restore to 10 when wiring the real quoter — production cares about
        // not spamming the venue, but measurement doesn't.
        inline static constexpr Tick requote_threshold_ticks        = Tick{1};
        // 60s stale window — defensive against any cross-process clock jitter
        // on first runs; tighten back to 1ms once we've confirmed the pipeline
        // works under tight thresholds too.
        inline static constexpr uint64_t stale_ns                   = 60ULL * 1000'000'000;
        inline static constexpr uint64_t max_contract_size          = 1;
    };
}