#pragma once
#include <cstdint>

static constexpr uint8_t MAX_FEED_LEVELS = 32;  // max levels per feed message

struct L2Level {
    double price = 0;
    double size  = 0;
};

struct L2Update {
    uint8_t  instrument_id = UINT8_MAX;
    L2Level  asks[MAX_FEED_LEVELS]{};
    L2Level  bids[MAX_FEED_LEVELS]{};
    uint8_t  bid_count   = 0;
    uint8_t  ask_count   = 0;
    uint64_t sequence_no = 0;
    uint64_t timestamp   = 0;
    bool     isSnapshot  = true;
};

struct FeedMessage {
    enum Type : uint8_t {L2Feed, MarkPrice, Ticker, OHLC, SpotPrice} type;
    uint8_t instrument_id;
    union {
        L2Update l2;
        double   mark_price;
        double   spot_price;
        // OHLC   ohlc;
        // Ticker ticker;
    };
};
