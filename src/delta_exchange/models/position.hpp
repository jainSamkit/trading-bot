#pragma once
#include <cstdint>
#include <string>

// REST model for /positions
struct Position {
    int64_t     user_id             = 0;
    uint32_t    product_id          = 0;
    std::string product_symbol;

    int32_t     size                = 0;    // positive = long, negative = short, 0 = flat

    std::string entry_price;                // BigDecimal string; average entry price
    std::string margin;                     // BigDecimal string; allocated margin
    std::string liquidation_price;          // BigDecimal string
    std::string bankruptcy_price;           // BigDecimal string; price of total loss

    int32_t     adl_level           = 0;    // auto-deleveraging rank 1 (low) – 5 (high)

    std::string commission;                 // BigDecimal string; accumulated commissions
    std::string realized_pnl;              // BigDecimal string; closed P&L
    std::string realized_funding;          // BigDecimal string; net funding received/paid
};
