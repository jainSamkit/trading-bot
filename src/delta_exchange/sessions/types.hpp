#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include "core/tick.hpp"
#include "core/contracts.hpp"
#include "config/config.hpp" // max levels per feed message

static constexpr size_t MAX_FEED_LEVELS = cfg::MAX_FEED_LEVELS;
static constexpr size_t MAX_INSTRUMENTS = cfg::MAX_INSTRUMENTS;

struct L2Level {
    double      price = 0.0;
    double      size = 0.0;
};

struct L2Update {
    L2Level  asks[MAX_FEED_LEVELS]{};
    L2Level  bids[MAX_FEED_LEVELS]{};
    uint64_t sequence_no = 0;
    uint64_t timestamp   = 0;
    uint8_t  bid_count   = 0;
    uint8_t  instrument_id = UINT8_MAX;
    uint8_t  ask_count   = 0;
    bool     isSnapshot  = true;
};

// Named TradeData to avoid shadowing the FeedMessage::Type::Trade enumerator
// (unscoped enums inject their values into the enclosing class scope).
struct TradeData {
    enum class BuyerRole : uint8_t { Maker, Taker };

    uint64_t        trade_time    = 0;                     // exchange ts (μs) from "t"
    uint64_t        publish_time  = 0;                     // server publish ts (μs) from "ts"
    double          price = 0.0;
    double          size  = 0.0;                   // contracts (payload may be fractional, e.g. "s": 1.0)
    BuyerRole       buyer_role    = BuyerRole::Maker;      // r="m" → buyer maker (sell-aggressed); r="t" → buyer taker (buy-aggressed)
    uint8_t         instrument_id = UINT8_MAX;
};

// Named MarkPriceData to avoid shadowing the FeedMessage::Type::MarkPrice enumerator
// (unscoped enums inject their values into the enclosing class scope).
struct MarkPriceData {
    uint64_t    timestamp = 0;
    double      price = 0.0;
    uint8_t     instrument_id = UINT8_MAX;
};


struct SpotPriceData {
    double      price = 0.0;
    uint8_t     instrument_id = UINT8_MAX;
};

static constexpr std::array<std::string_view, 12>ohlc_resolutions {"1m", "3m", "5m", "15m", "30m", "1h", "2h", "4h", "6h", "12h", "1d", "1w"};

struct OHLCData {
    uint64_t        start_time = 0;
    uint64_t        timestamp = 0;
    double          volume = 0.0;
    double          close = 0.0;
    double          low = 0.0;
    double          open{0};
    double          high{0};
    uint8_t         res_idx = UINT8_MAX;
    bool            is_mark = false;
};


struct FeedMessage {
    enum Type : uint8_t {L2Feed, MarkPrice, Trade, OHLC, SpotPrice} type;
    uint8_t  instrument_id;
    uint64_t  t_recv_userspace = 0;   // ns: after SSL_read
    uint64_t  t_frame  = 0;   // ns: after WS frame assembled
    uint64_t  t_parse  = 0;   // ns: after JSON parse + tick convert
    union {
        L2Update       l2;
        MarkPriceData  mark_price_data;
        SpotPriceData  spot_price_data;
        OHLCData       ohlc;
        TradeData      trade_data;
    };
};
