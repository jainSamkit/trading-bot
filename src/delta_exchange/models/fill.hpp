#pragma once
#include <cstdint>

enum class FillRole   : uint8_t { Taker, Maker };
enum class FillReason : uint8_t { Normal, Adl, Liquidation };
enum class FillSide   : uint8_t { Buy, Sell };

struct Fill {
    uint64_t    order_id        = 0;
    uint32_t    product_id      = 0;
    uint8_t     instrument_id   = UINT8_MAX;
    char        symbol[24]      = {};
    char        fill_id[33]     = {};           // 32-char hex UUID + null

    double      price           = 0.0;
    uint32_t    size            = 0;            // contracts executed
    int32_t     position_after  = 0;            // net position after this fill

    FillRole    role            = FillRole::Taker;
    FillReason  reason          = FillReason::Normal;
    FillSide    side            = FillSide::Buy;

    uint64_t    client_order_id  = 0;           // numeric; convert to string only at REST send

    uint64_t    timestamp       = 0;
};
