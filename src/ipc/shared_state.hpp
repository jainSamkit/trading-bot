#pragma once
#include "delta_exchange/models/product.hpp"
#include "core/seq_lock.hpp"
#include "core/venue.hpp"
#include "core/spmc_ring.hpp"
#include "core/spsc_ring.hpp"
#include "core/snapshots.hpp"
#include "config/config.hpp"
#include "oms/execution_intent.hpp"
#include <cstdint>


struct SharedState {
    SeqLock<MarketSnapshot>                                         market_state[core::N_VENUES][cfg::MAX_INSTRUMENTS]{};
    SeqLock<MarkPriceSnapshot>                                      mark_prices[core::N_VENUES][cfg::MAX_INSTRUMENTS]{};
    SeqLock<SpotPriceSnapshot>                                      spot_prices[core::N_VENUES][cfg::MAX_INSTRUMENTS]{};
    SpmcRing<TradeEntry, cfg::TRADE_RING_SIZE>                      trade_ring[core::N_VENUES]{};
    SpscRing<ExecutionIntent, cfg::INTENT_RING_SIZE>                execution_intent_ring[core::N_VENUES]{};
};