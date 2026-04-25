#pragma once
#include <atomic>
#include "core/spsc_ring.hpp"

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
    ExecutionStopOrderType                      stop_order_type;
    ExecutionStopOrderTriggerMethod             stop_order_trigger_method;
    double                                      stop_price;
};

struct EditOrderIntent {
    uint64_t                        exchange_product_id;
    uint64_t                        order_id;
    uint64_t                        size;
    double                          price;
    double                          stop_price;
    double                          tick_size;
    ExecutionOrderType              type        = ExecutionOrderType::Limit;
    bool                            post_only   = true;
    bool                            is_stop_order = false;
};

struct CancelOrderIntent {
    uint64_t                        order_id;
    uint64_t                        exchange_product_id;
};

struct CancelAllOrderIntent {
    bool cancel_limit_orders        = false;
    bool cancel_stop_orders        = false;
    uint64_t                        exchange_product_id;
};

struct CloseAllPositions {
    bool                            close_all_isolated = true;
    bool                            close_all_cross = true;
    bool                            close_all_portfolio = true;
};


struct ExecutionIntent {
    ExecutionIntent() {}    // union members have non-trivial ctors; caller sets action before use
    ExecutionAction   action;
    union {
        CreateOrderIntent       create_order_intent; //place order
        EditOrderIntent         edit_order_intent;
        CancelOrderIntent       cancel_order_intent;
        CancelAllOrderIntent    cancel_all_order_intent;
        CloseAllPositions       close_all_positions_intent;
    };
};

// ─── ExecutionManager (TODO: wire up when ConnectionPool is implemented) ──────
//
// template<typename T>
// concept IsTCPClient = std::derived_from<T, TcpClient>;
//
// template<IsTCPClient RestClient, size_t PoolSize>
// class ExecutionManager {
//
//     using Handler = void (RestClient::*)(const ExecutionIntent&, SpscRing<OMSEvent, 256>*);
//     static constexpr Handler kHandlers[] = {
//         &RestClient::create_order,
//         &RestClient::edit_order,
//         &RestClient::cancel_order,
//         &RestClient::cancel_all_orders,
//         &RestClient::close_all_positions,
//     };
//
//     explicit ExecutionManager(const char* host, const char* api_key, const char* api_secret,
//                               const ProductTable& products)
//         : host_(host), api_key_(api_key), api_secret_(api_secret), products_(products),
//           conn_pool_(host, api_key, api_secret, products)
//     {
//         conn_pool_.connect_all();
//     }
//
//     void run(std::atomic<bool>& running_) {
//         while (running_.load(std::memory_order_relaxed))
//             drain_intake_ring();
//     }
//
//     void drain_intake_ring() {
//         while (auto* intent = intent_ring_->pop_begin()) {
//             RestClient& client = conn_pool_.get_next();
//             auto handler = kHandlers[static_cast<uint8_t>(intent->action)];
//             (client.*handler)(*intent, rest_ring_);
//             intent_ring_->pop_commit();
//         }
//     }
//
// private:
//     const char*                     host_;
//     const char*                     api_key_;
//     const char*                     api_secret_;
//     SpscRing<ExecutionIntent, 256>* intent_ring_;
//     SpscRing<OMSEvent, 256>*        rest_ring_;
//     ConnectionPool<RestClient, PoolSize> conn_pool_;
//     ProductTable&                   products_;
// };