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

// Named MarkPriceData to avoid shadowing the FeedMessage::Type::MarkPrice enumerator
// (unscoped enums inject their values into the enclosing class scope).
struct MarkPriceData {
    struct PriceBand {
        double lower_limit = 0.0;
        double upper_limit = 0.0;
    };
    uint8_t   instrument_id = UINT8_MAX;
    PriceBand price_band{};
    double    price     = 0.0;
    uint64_t  timestamp = 0;
};


struct SpotPriceData {
    uint8_t   instrument_id = UINT8_MAX;
    double    price     = 0.0;
};

static constexpr std::array<std::string_view, 12>ohlc_resolutions {"1m", "3m", "5m", "15m", "30m", "1h", "2h", "4h", "6h", "12h", "1d", "1w"};

struct OHLCData {
    uint64_t    start_time = 0;
    double      close = 0.0;
    double      low = 0.0;
    double      open = 0.0;
    double      high = 0.0;
    uint64_t    timestamp = 0;
    double      volume = 0.0;
    uint8_t     res_idx = UINT8_MAX;
    bool        is_mark = false;
};


struct FeedMessage {
    enum Type : uint8_t {L2Feed, MarkPrice, Ticker, OHLC, SpotPrice} type;
    uint8_t  instrument_id;
    int64_t  t_kernel = 0;   // ns: after SSL_read
    int64_t  t_frame  = 0;   // ns: after WS frame assembled
    int64_t  t_parse  = 0;   // ns: after JSON parse + tick convert
    union {
        L2Update       l2;
        MarkPriceData  mark_price;
        SpotPriceData  spot_price;
        OHLCData       ohlc;
        // Ticker ticker;
    };
};
