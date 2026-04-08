#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct IndexConstituent {
    std::string exchange;   // e.g. "Coinbase", "Binance"
    double      weight = 0.0;
};

// REST model for /indices and nested in Product::spot_index
struct SpotIndex {
    int64_t     id                   = 0;
    std::string symbol;                       // e.g. ".DEXBTUSD"
    std::string index_type;                   // "spot_pair" | "fixed_interest_rate" | "floating_interest_rate"
    int64_t     underlying_asset_id  = 0;
    int64_t     quoting_asset_id     = 0;
    std::string tick_size;                    // BigDecimal string
    std::vector<IndexConstituent> constituent_exchanges;
};
