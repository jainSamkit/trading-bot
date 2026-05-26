#pragma once
#include <cstdint>
#include "core/snapshots.hpp"
#include "core/tick.hpp"
#include "strategy/strategy_config.hpp"

namespace strategy {

    struct RiskOverlay {

        enum class RiskState : uint8_t { OK, SkipStale, SkipWideSpread, SkipUninit};

        static RiskState check(const MarketSnapshot& snapshot, uint64_t now_ns) {
            //skip no book
            if(snapshot.best_bid == Tick{0} || snapshot.best_ask == Tick{0}) return RiskState::SkipUninit;
            //check stale book 
            if(now_ns - snapshot.t_origin_ns  > strategy::Config::stale_ns) return RiskState::SkipStale;
            //check wide spread 
            if(snapshot.spread > strategy::Config::max_spread_ticks) return RiskState::SkipWideSpread;

            return RiskState::OK;
        }
    };
}