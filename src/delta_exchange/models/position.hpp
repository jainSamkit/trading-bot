#pragma once
#include <cstdint>
#include <cstring>

enum class MarginMode     : uint8_t { Isolated, Cross };

struct Position {
    uint32_t        product_id          = 0;
    uint8_t         instrument_id       = UINT8_MAX;
    char            symbol[24]          = {};

    int32_t         size                = 0;     // positive = long, negative = short, 0 = flat

    double          entry_price         = 0.0;
    double          margin              = 0.0;
    double          liquidation_price   = 0.0;
    double          bankruptcy_price    = 0.0;
    double          commission          = 0.0;
    double          realized_pnl        = 0.0;
    double          realized_cashflow   = 0.0;
    double          realized_funding    = 0.0;

    MarginMode      margin_mode         = MarginMode::Isolated;

    bool            auto_topup          = false;
    bool            under_liquidation   = false;

    uint64_t        timestamp           = 0;
};
