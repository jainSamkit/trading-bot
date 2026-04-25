#pragma once
#include "delta_exchange/models/order.hpp"
#include "delta_exchange/models/product.hpp"
#include "execution/execution_manager.hpp"
#include "oms/oms_manager.hpp"
#include <string>
#include <string_view>
#include <vector>

class DeltaRestClient;

struct CreateOrderRequest {
    uint32_t          product_id      = 0;
    uint32_t          size            = 0;
    OrderSide         side            = OrderSide::Buy;
    OrderType         order_type      = OrderType::Limit;
    TimeInForce       time_in_force   = TimeInForce::GTC;
    bool              post_only       = false;
    bool              reduce_only     = false;
    double            limit_price     = 0.0;
    double            tick_size       = 0.01;
    uint64_t          client_order_id = 0;
    StopOrderType     stop_order_type        = StopOrderType::None;
    double            stop_price             = 0.0;
    StopTriggerMethod stop_trigger_method    = StopTriggerMethod::None;

    // Build from the union member of ExecutionIntent
    static CreateOrderRequest from_intent(const CreateOrderIntent& intent);

    // Serialize to JSON body for POST /v2/orders
    std::string serialize() const;

    // Parse POST /v2/orders response into an OMSEvent (type=Order, source=Rest, action=Create)
    // Returns false if success=false or JSON is malformed
    static bool deserialize(std::string_view json, OMSEvent& out, const ProductTable& products);
};

struct EditOrderRequest {
    uint64_t  order_id    = 0;
    uint32_t  product_id  = 0;
    uint32_t  size        = 0;    // 0 = don't change size
    double    limit_price = 0.0;
    double    stop_price  = 0.0;
    double    tick_size   = 0.01;
    OrderType order_type  = OrderType::Limit;
    bool      is_stop_order = false;

    // Build from the union member of ExecutionIntent
    static EditOrderRequest from_intent(const EditOrderIntent& intent);

    // Serialize to JSON body for PUT /v2/orders/{id}
    std::string serialize() const;

    // Reuses same response format as CreateOrderRequest — action=Update
    static bool deserialize(std::string_view json, OMSEvent& out, const ProductTable& products);
};

struct CancelOrderRequest {
    uint64_t order_id   = 0;
    uint32_t product_id = 0;

    // Build from the union member of ExecutionIntent
    static CancelOrderRequest from_intent(const CancelOrderIntent& intent);

    // Serialize to JSON body for DELETE /v2/orders/{id}
    std::string serialize() const;

    // Reuses same response format as CreateOrderRequest — action=Delete
    static bool deserialize(std::string_view json, OMSEvent& out, const ProductTable& products);
};

struct CancelAllOrderRequest {
    bool     cancel_limit_orders = true;
    bool     cancel_stop_orders  = true;
    uint32_t product_id          = 0;    // 0 = all products

    static CancelAllOrderRequest from_intent(const CancelAllOrderIntent& intent);

    // Serialize to JSON body for DELETE /v2/orders/all
    std::string serialize() const;

    // Response is {"result":{}, "success":true} — no order data
    // Returns true if success=true, false otherwise
    static bool parse_success(std::string_view json);
};

struct GetOpenOrdersRequest {
    std::string states    = "open";   // "open" for regular orders, "pending" for stop orders
    int         page_num  = 1;
    int         page_size = 30;

    // Builds query string: states=open&page_num=1&page_size=30
    std::string query() const;

    // Parses one page of GET /v2/orders response into out_events[0..max_events].
    // Returns number of events parsed, sets has_more based on meta.total_count.
    static int deserialize_page(std::string_view json,
                                OMSEvent* out_events,
                                int max_events,
                                bool& has_more,
                                int page_num,
                                int page_size,
                                const ProductTable& products);
};

