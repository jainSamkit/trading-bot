#pragma once
#include <cstdint>
#include <string>

enum class OrderSide        { Buy, Sell };
enum class OrderType        { Limit, Market };
enum class TimeInForce      { GTC, IOC };       // good-til-cancelled, immediate-or-cancel
enum class StopOrderType    { None, StopLoss };
enum class OrderState       { Open, Pending, Closed, Cancelled };

// REST request to POST /orders
struct OrderRequest {
    uint32_t    product_id      = 0;
    std::string limit_price;                    // BigDecimal string; omit for market orders
    int32_t     size            = 0;            // number of contracts
    OrderSide   side            = OrderSide::Buy;
    OrderType   order_type      = OrderType::Limit;
    TimeInForce time_in_force   = TimeInForce::GTC;
    bool        post_only       = false;
    bool        reduce_only     = false;

    // stop orders
    StopOrderType stop_order_type       = StopOrderType::None;
    std::string   stop_price;                   // BigDecimal string
    std::string   trail_amount;                 // BigDecimal string; trailing stop distance
    std::string   stop_trigger_method;          // e.g. "last_traded_price"

    // market maker protection
    bool        mmp             = false;

    // user-defined tag, max 32 chars
    std::string client_order_id;
};

// REST response from GET/POST /orders
struct Order {
    int64_t       id                = 0;
    int64_t       user_id           = 0;
    uint32_t      product_id        = 0;
    std::string   product_symbol;

    int32_t       size              = 0;        // original quantity
    int32_t       unfilled_size     = 0;        // remaining open quantity

    OrderSide     side              = OrderSide::Buy;
    OrderType     order_type        = OrderType::Limit;
    TimeInForce   time_in_force     = TimeInForce::GTC;
    std::string   limit_price;                  // BigDecimal string
    bool          post_only         = false;
    bool          reduce_only       = false;
    bool          close_on_trigger  = false;

    StopOrderType stop_order_type   = StopOrderType::None;
    std::string   stop_price;
    std::string   trail_amount;

    OrderState    state             = OrderState::Open;

    std::string   paid_commission;              // BigDecimal string
    std::string   commission;                   // BigDecimal string

    bool          mmp               = false;
    std::string   client_order_id;

    int64_t       created_at        = 0;        // microseconds since epoch
};
