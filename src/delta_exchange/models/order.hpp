#pragma once
#include <cstdint>
#include <cstring>

enum class OrderSide                : uint8_t { Buy, Sell };
enum class OrderType                : uint8_t { Limit, Market };
enum class TimeInForce              : uint8_t { GTC, IOC };
enum class StopOrderType            : uint8_t { None, TP, SL };
enum class StopTriggerMethod        : uint8_t { None, Mark, LTP, Spot};
enum class OrderState               : uint8_t { Open, Pending, Closed, Cancelled };
enum class OrderReason              : uint8_t { None, Fill, StopCreate, StopUpdate, StopTrigger, StopCancel, Liquidation, SelfTrade };

// WS order channel update — no heap strings on hot fields
struct Order {
    uint64_t      id                = 0;
    uint32_t      product_id        = 0;
    uint8_t       instrument_id     = UINT8_MAX; // index into ProductTable
    char          symbol[24]        = {};         // e.g. "BTCUSD"

    uint32_t      size              = 0;
    uint32_t      filled_size       = 0;         // always >= 0
    uint32_t      unfilled_size     = 0;         // always >= 0

    OrderSide     side              = OrderSide::Buy;
    OrderType     order_type        = OrderType::Limit;
    TimeInForce   time_in_force     = TimeInForce::GTC;
    OrderState    state             = OrderState::Open;
    OrderReason   reason            = OrderReason::None;
    StopOrderType stop_order_type   = StopOrderType::None;
    StopTriggerMethod stop_trigger_method = StopTriggerMethod::None;

    double        limit_price       = 0.0;
    double        average_fill_price= 0.0;
    double        paid_commission   = 0.0;
    double        stop_price        = 0.0;

    bool          reduce_only       = false;
    bool          mmp               = false;

    char          fill_id[33]       = {};        // 32-char hex UUID + null; no heap
    uint64_t      client_order_id   = 0;         // numeric; convert to string only at REST send

    uint64_t      timestamp         = 0;         // Unix microseconds; WS: event timestamp from Delta; REST: local system_clock just before request was sent
};
