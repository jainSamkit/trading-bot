#pragma once
#include "core/models/asset.hpp"
#include "core/models/index.hpp"
#include <cstdint>
#include <string>

// REST model for /products — richer than the feed-side Product in feed/models/product.hpp
struct ProductInfo {
    enum class TradingStatus { Operational, DisruptedCancelOnly, DisruptedPostOnly };
    enum class State         { Live, Upcoming, Expired };

    int64_t       id                              = 0;
    std::string   symbol;                                 // e.g. "BTCUSD"
    std::string   description;
    std::string   product_type;                           // "future" | "inverse_future"
    std::string   contract_type;                          // "perpetual_futures" | "futures" | "call_options" | "put_options"
    std::string   settlement_time;                        // ISO timestamp; empty for perps
    std::string   pricing_source;

    // margin / leverage
    std::string   initial_margin;                         // BigDecimal string (%)
    std::string   maintenance_margin;                     // BigDecimal string (%)
    std::string   initial_margin_scaling_factor;
    std::string   maintenance_margin_scaling_factor;
    std::string   default_leverage;
    std::string   max_leverage_notional;

    // contract specs
    std::string   contract_value;                         // spot price × asset qty per contract
    std::string   contract_unit_currency;                 // underlying for vanilla, settling for inverse
    std::string   tick_size;                              // minimum price increment
    int32_t       impact_size                  = 0;       // typical size used in mark price calc
    int32_t       position_size_limit          = 0;       // max contracts per order
    bool          is_quanto                    = false;

    // fees
    std::string   commission_rate;                        // taker fee
    std::string   maker_commission_rate;                  // maker rebate
    std::string   liquidation_penalty_factor;

    // funding (perps)
    std::string   funding_method;                         // "mark_price" | "fixed"
    std::string   annualized_funding;

    // price limits
    std::string   price_band;                             // % range around mark price
    std::string   basis_factor_max_limit;

    TradingStatus trading_status               = TradingStatus::Operational;
    State         state                        = State::Live;

    std::string   created_at;
    std::string   updated_at;

    // nested objects
    Asset         underlying_asset{};
    Asset         quoting_asset{};
    Asset         settling_asset{};
    SpotIndex     spot_index{};
};
