#pragma once
#include "delta_exchange/models/product.hpp"
#include "core/seq_lock.hpp"
#include "core/spmc_ring.hpp"
#include "core/snapshots.hpp"
#include "config/config.hpp"
#include <cstdint>


struct SharedState {
    static constexpr size_t MAX_INSTRUMENTS = cfg::MAX_INSTRUMENTS;
    SeqLock<MarketSnapshot>                                         market_state[MAX_INSTRUMENTS]{};
    SeqLock<MarkPriceSnapshot>                                      mark_prices[MAX_INSTRUMENTS]{};
    SeqLock<SpotPriceSnapshot>                                      spot_prices[MAX_INSTRUMENTS]{};
    SpmcRing<TradeEntry, cfg::TRADE_RING_SIZE>                      trade_ring{};
};