#pragma once
#include <cstdint>
#include <string>

// REST model for /assets
struct Asset {
    int64_t     id                    = 0;
    std::string symbol;                       // e.g. "BTC", "USD"
    int32_t     precision             = 0;    // decimal places
    std::string deposit_status;               // "enabled" | "disabled"
    std::string withdrawal_status;            // "enabled" | "disabled"
    std::string base_withdrawal_fee;          // BigDecimal string
    std::string min_withdrawal_amount;        // BigDecimal string
};
