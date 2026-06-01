#pragma once
// Plain intent POD types shared between the strategy (producer), SharedState
// (the intent ring), and ExecutionManager (consumer). Kept dependency-free so
// shared_state.hpp can include it without pulling in ExecutionManager — which
// would create a shared_state <-> execution_manager include cycle.
#include <cstdint>

enum class ExecutionAction : uint8_t {
    CreateOrder,
    EditOrder,
    CancelOrder,
    CancelAllOrders,
    CloseAllPositions,
};

enum class ExecutionOrderSide: uint8_t {Buy, Sell};
enum class ExecutionOrderType: uint8_t {Market, Limit};
enum class ExecutionStopOrderType : uint8_t {TP, SL};
enum class ExecutionStopOrderTriggerMethod: uint8_t {Mark, LTP, Spot};


struct CreateOrderIntent {
    ExecutionOrderSide                          side;
    ExecutionOrderType                          type;
    double                                      price;
    double                                      tick_size;
    uint64_t                                    exchange_product_id;
    uint64_t                                    size;
    uint64_t                                    client_order_id = 0;  // set by caller; 0 = omit from request
    bool                                        reduce_only=false;
    bool                                        post_only = true;
    bool                                        is_stop_order = false;
    ExecutionStopOrderType                      stop_order_type = ExecutionStopOrderType::TP;
    ExecutionStopOrderTriggerMethod             stop_order_trigger_method = ExecutionStopOrderTriggerMethod::Mark;
    double                                      stop_price = 0.0;
};

struct EditOrderIntent {
    uint64_t                                    exchange_product_id;
    uint64_t                                    order_id;
    uint64_t                                    size;
    double                                      price;
    double                                      stop_price;
    double                                      tick_size;
    ExecutionOrderType                          type        = ExecutionOrderType::Limit;
    bool                                        post_only   = true;
    bool                                        is_stop_order = false;
};

struct CancelOrderIntent {
    uint64_t                                    order_id;
    uint64_t                                    exchange_product_id;
};

struct CancelAllOrderIntent {
    bool cancel_limit_orders                    = false;
    bool cancel_stop_orders                     = false;
    uint64_t                                    exchange_product_id;
};

struct CloseAllPositions {
    bool                                        close_all_isolated = true;
    bool                                        close_all_cross = true;
    bool                                        close_all_portfolio = true;
};

struct ExecutionIntent {
    ExecutionIntent() {}    // union members have non-trivial ctors; caller sets action before use
    ExecutionAction                             action;
    uint64_t                                    t_origin_ns = 0;
    uint64_t                                    t_intent = 0; //time at which intent was pushed to intent ring
    union {
        CreateOrderIntent                       create_order_intent; //place order
        EditOrderIntent                         edit_order_intent;
        CancelOrderIntent                       cancel_order_intent;
        CancelAllOrderIntent                    cancel_all_order_intent;
        CloseAllPositions                       close_all_positions_intent;
    };
};
