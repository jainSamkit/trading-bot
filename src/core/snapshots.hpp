#pragma once
#include <cstdint>
#include <array>                                                                                                                                                                                                                                                                                                                                         
#include "core/tick.hpp"
#include "core/contracts.hpp"
#include "config/config.hpp"

struct TickLevel {
    Tick        tick{};
    Contracts   size{};
};

struct MarketSnapshot {
    uint64_t                                                t_origin_ns = 0;
    std::array<TickLevel, cfg::SNAPSHOT_DEPTH>              asks = {};
    std::array<TickLevel, cfg::SNAPSHOT_DEPTH>              bids = {};
    uint64_t                                                last_seq = 0;
    uint64_t                                                last_update_ts_ns = 0;
    Tick                                                    best_bid{};
    Tick                                                    best_ask{};
    Tick                                                    bestBidPlusAsk{};
    Tick                                                    spread{};

};

// struct VolSnapshot {
//     double   sigma_sq;
//     double   close;
//     double   volume;
//     uint64_t sequence;
//     int64_t  t_ns;
// };

struct MarkPriceSnapshot {
    uint64_t        t_origin_ns = 0;
    uint64_t        last_update_ts_ns = 0;
    Tick            mark_price_tick{};
};

struct SpotPriceSnapshot {
    uint64_t        t_origin_ns = 0;
    Tick            spot_price_tick{};
};

struct TradeEntry {
    uint64_t        t_origin_ns = 0;
    uint64_t        timestamp   = 0;
    Tick            tick{};
    Contracts       size{};
    uint8_t         instrument_id;
};