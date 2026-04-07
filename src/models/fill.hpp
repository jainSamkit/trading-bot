#pragma once
#include <cstdint>
#include <string>

enum class FillRole { Taker, Maker };

// REST model for /fills (individual execution reports)
struct Fill {
    int64_t     id              = 0;
    int64_t     order_id        = 0;
    int64_t     user_id         = 0;
    uint32_t    product_id      = 0;
    std::string product_symbol;

    int32_t     size            = 0;        // executed quantity (contracts)
    std::string price;                      // BigDecimal string; execution price
    std::string side;                       // "buy" | "sell"

    FillRole    role            = FillRole::Taker;
    std::string commission;                 // BigDecimal string; negative = maker rebate

    int64_t     created_at      = 0;        // microseconds since epoch
};
