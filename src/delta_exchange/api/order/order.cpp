#include "delta_exchange/api/order/order.hpp"
#include <simdjson.h>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string>

// ─── helpers ─────────────────────────────────────────────────────────────────

static const char* side_str(OrderSide s) {
    return s == OrderSide::Buy ? "buy" : "sell";
}

static const char* order_type_str(OrderType t) {
    return t == OrderType::Limit ? "limit_order" : "market_order";
}

static const char* tif_str(TimeInForce t) {
    return t == TimeInForce::GTC ? "gtc" : "ioc";
}

static const char* stop_trigger_str(StopTriggerMethod m) {
    switch (m) {
        case StopTriggerMethod::Mark: return "mark_price";
        case StopTriggerMethod::LTP:  return "last_traded_price";
        case StopTriggerMethod::Spot: return "spot_price";
        default:                      return "";
    }
}

static double sv_to_double(std::string_view sv) {
    double val = 0.0;
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return val;
}

static int tick_decimals(double tick_size) {
    if (tick_size >= 1.0) return 0;
    return static_cast<int>(std::ceil(-std::log10(tick_size)));
}

static std::string double_to_str(double v, int prec) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed, prec);
    return std::string(buf, ptr);
}

// ─── from_intent ─────────────────────────────────────────────────────────────

CreateOrderRequest CreateOrderRequest::from_intent(const CreateOrderIntent& intent) {
    CreateOrderRequest req;
    req.product_id    = static_cast<uint32_t>(intent.exchange_product_id);
    req.size          = static_cast<uint32_t>(intent.size);
    req.side          = (intent.side == ExecutionOrderSide::Buy) ? OrderSide::Buy : OrderSide::Sell;
    req.order_type    = (intent.type == ExecutionOrderType::Limit) ? OrderType::Limit : OrderType::Market;
    req.post_only     = intent.post_only;
    req.reduce_only   = intent.reduce_only;
    req.time_in_force   = TimeInForce::GTC;
    req.limit_price     = intent.price;
    req.tick_size       = intent.tick_size;
    req.client_order_id = intent.client_order_id;

    if (intent.is_stop_order) {
        req.stop_order_type = (intent.stop_order_type == ExecutionStopOrderType::TP)
                              ? StopOrderType::TP : StopOrderType::SL;
        req.stop_price = intent.stop_price;

        switch (intent.stop_order_trigger_method) {
            case ExecutionStopOrderTriggerMethod::Mark: req.stop_trigger_method = StopTriggerMethod::Mark; break;
            case ExecutionStopOrderTriggerMethod::LTP:  req.stop_trigger_method = StopTriggerMethod::LTP;  break;
            case ExecutionStopOrderTriggerMethod::Spot: req.stop_trigger_method = StopTriggerMethod::Spot; break;
        }
    }

    return req;
}

// ─── serialize ───────────────────────────────────────────────────────────────

std::string CreateOrderRequest::serialize() const {
    std::string body;
    body.reserve(256);

    body += "{\"order_type\":\"";    body += order_type_str(order_type);
    body += "\",\"side\":\"";        body += side_str(side);
    body += "\",\"product_id\":";    body += std::to_string(product_id);
    body += ",\"size\":";            body += std::to_string(size);
    body += ",\"time_in_force\":\""; body += tif_str(time_in_force);
    body += "\",\"post_only\":";     body += post_only   ? "true" : "false";
    body += ",\"reduce_only\":";     body += reduce_only ? "true" : "false";

    if (order_type == OrderType::Limit) {
        body += ",\"limit_price\":\""; body += double_to_str(limit_price, tick_decimals(tick_size)); body += "\"";
    }

    if (client_order_id != 0) {
        body += ",\"client_order_id\":\""; body += std::to_string(client_order_id); body += "\"";
    }

    if (stop_order_type != StopOrderType::None) {
        body += ",\"stop_order_type\":\"";
        body += (stop_order_type == StopOrderType::TP) ? "take_profit_order" : "stop_loss_order";
        body += "\"";
        if (stop_price != 0.0) {
            body += ",\"stop_price\":\""; body += double_to_str(stop_price, tick_decimals(tick_size)); body += "\"";
        }
        if (stop_trigger_method != StopTriggerMethod::None) {
            body += ",\"stop_trigger_method\":\""; body += stop_trigger_str(stop_trigger_method); body += "\"";
        }
    }

    body += ",\"order_source\":\"place_order\",\"source\":\"api\"}";
    return body;
}

// ─── EditOrderRequest ─────────────────────────────────────────────────────────

EditOrderRequest EditOrderRequest::from_intent(const EditOrderIntent& intent) {
    EditOrderRequest req;
    req.order_id      = intent.order_id;
    req.product_id    = static_cast<uint32_t>(intent.exchange_product_id);
    req.size          = static_cast<uint32_t>(intent.size);
    req.limit_price   = intent.price;
    req.stop_price    = intent.stop_price;
    req.tick_size     = intent.tick_size;
    req.order_type    = (intent.type == ExecutionOrderType::Limit) ? OrderType::Limit : OrderType::Market;
    req.is_stop_order = intent.is_stop_order;
    return req;
}

std::string EditOrderRequest::serialize() const {
    std::string body;
    body.reserve(128);

    body += "{\"id\":";         body += std::to_string(order_id);
    body += ",\"product_id\":"; body += std::to_string(product_id);

    if (is_stop_order) {
        if (stop_price != 0.0) {
            body += ",\"stop_price\":\""; body += double_to_str(stop_price, tick_decimals(tick_size)); body += "\"";
        }
        if (order_type == OrderType::Limit && limit_price != 0.0) {
            body += ",\"limit_price\":\""; body += double_to_str(limit_price, tick_decimals(tick_size)); body += "\"";
        }
    } else {
        body += ",\"limit_price\":\""; body += double_to_str(limit_price, tick_decimals(tick_size)); body += "\"";
    }

    if (size != 0) {
        body += ",\"size\":"; body += std::to_string(size);
    }

    body += ",\"order_source\":\"open_order_edit\"}";
    return body;
}

bool EditOrderRequest::deserialize(std::string_view json, OMSEvent& out, const ProductTable& products) {
    if (!CreateOrderRequest::deserialize(json, out, products)) return false;
    out.action = OMSAction::Update;
    return true;
}

// ─── CancelOrderRequest ───────────────────────────────────────────────────────

CancelOrderRequest CancelOrderRequest::from_intent(const CancelOrderIntent& intent) {
    CancelOrderRequest req;
    req.order_id   = intent.order_id;
    req.product_id = static_cast<uint32_t>(intent.exchange_product_id);
    return req;
}

std::string CancelOrderRequest::serialize() const {
    std::string body;
    body.reserve(64);
    body += "{\"id\":";         body += std::to_string(order_id);
    body += ",\"product_id\":"; body += std::to_string(product_id);
    body += "}";
    return body;
}

bool CancelOrderRequest::deserialize(std::string_view json, OMSEvent& out, const ProductTable& products) {
    if (!CreateOrderRequest::deserialize(json, out, products)) return false;
    out.action = OMSAction::Delete;
    return true;
}

// ─── CancelAllOrderRequest ────────────────────────────────────────────────────

CancelAllOrderRequest CancelAllOrderRequest::from_intent(const CancelAllOrderIntent& intent) {
    CancelAllOrderRequest req;
    req.cancel_limit_orders = intent.cancel_limit_orders;
    req.cancel_stop_orders  = intent.cancel_stop_orders;
    req.product_id          = (intent.exchange_product_id == UINT64_MAX)
                              ? 0 : static_cast<uint32_t>(intent.exchange_product_id);
    return req;
}

std::string CancelAllOrderRequest::serialize() const {
    std::string body;
    body.reserve(96);
    body += "{\"cancel_limit_orders\":\""; body += cancel_limit_orders ? "true" : "false";
    body += "\",\"cancel_stop_orders\":\""; body += cancel_stop_orders  ? "true" : "false";
    body += "\"";
    if (product_id != 0) {
        body += ",\"product_id\":"; body += std::to_string(product_id);
    }
    body += "}";
    return body;
}

bool CancelAllOrderRequest::parse_success(std::string_view json) {
    simdjson::ondemand::parser parser;
    auto result = parser.iterate(json.data(), json.size(), json.size() + simdjson::SIMDJSON_PADDING);
    if (result.error()) return false;
    auto doc = std::move(result.value());
    bool success = false;
    if (doc["success"].get_bool().get(success)) return false;
    return success;
}

// ─── shared field parser ──────────────────────────────────────────────────────

static void parse_order_fields(simdjson::ondemand::object& obj, Order& o, const ProductTable& products) {
    for (auto field : obj) {
        std::string_view key;
        if (field.unescaped_key().get(key)) continue;

        if (key == "id") {
            uint64_t v; if (!field.value().get_uint64().get(v)) o.id = v;

        } else if (key == "product_id") {
            int64_t v; if (!field.value().get_int64().get(v)) o.product_id = static_cast<uint32_t>(v);

        } else if (key == "product_symbol") {
            std::string_view v;
            if (!field.value().get_string().get(v)) {
                o.instrument_id = products.idfromSymbol(v);
                size_t n = std::min(v.size(), sizeof(o.symbol) - 1);
                std::memcpy(o.symbol, v.data(), n);
                o.symbol[n] = '\0';
            }

        } else if (key == "side") {
            std::string_view v;
            if (!field.value().get_string().get(v))
                o.side = (v == "buy") ? OrderSide::Buy : OrderSide::Sell;

        } else if (key == "order_type") {
            std::string_view v;
            if (!field.value().get_string().get(v))
                o.order_type = (v == "limit_order") ? OrderType::Limit : OrderType::Market;

        } else if (key == "state") {
            std::string_view v;
            if (!field.value().get_string().get(v)) {
                if      (v == "open")      o.state = OrderState::Open;
                else if (v == "pending")   o.state = OrderState::Pending;
                else if (v == "closed")    o.state = OrderState::Closed;
                else if (v == "cancelled") o.state = OrderState::Cancelled;
            }

        } else if (key == "size") {
            uint64_t v; if (!field.value().get_uint64().get(v)) o.size = static_cast<uint32_t>(v);

        } else if (key == "unfilled_size") {
            uint64_t v; if (!field.value().get_uint64().get(v)) o.unfilled_size = static_cast<uint32_t>(v);

        } else if (key == "limit_price") {
            std::string_view v; if (!field.value().get_string().get(v)) o.limit_price = sv_to_double(v);

        } else if (key == "average_fill_price") {
            std::string_view v; if (!field.value().get_string().get(v)) o.average_fill_price = sv_to_double(v);

        } else if (key == "paid_commission") {
            std::string_view v; if (!field.value().get_string().get(v)) o.paid_commission = sv_to_double(v);

        } else if (key == "stop_price") {
            std::string_view v; if (!field.value().get_string().get(v)) o.stop_price = sv_to_double(v);

        } else if (key == "reduce_only") {
            bool v; if (!field.value().get_bool().get(v)) o.reduce_only = v;

        } else if (key == "time_in_force") {
            std::string_view v;
            if (!field.value().get_string().get(v))
                o.time_in_force = (v == "gtc") ? TimeInForce::GTC : TimeInForce::IOC;

        } else if (key == "stop_order_type") {
            std::string_view v;
            if (!field.value().get_string().get(v)) {
                if      (v == "take_profit_order") o.stop_order_type = StopOrderType::TP;
                else if (v == "stop_loss_order")   o.stop_order_type = StopOrderType::SL;
            }

        } else if (key == "stop_trigger_method") {
            std::string_view v;
            if (!field.value().get_string().get(v)) {
                if      (v == "mark_price")        o.stop_trigger_method = StopTriggerMethod::Mark;
                else if (v == "last_traded_price") o.stop_trigger_method = StopTriggerMethod::LTP;
                else if (v == "spot_price")        o.stop_trigger_method = StopTriggerMethod::Spot;
            }

        } else if (key == "client_order_id") {
            std::string_view v;
            if (!field.value().get_string().get(v))
                std::from_chars(v.data(), v.data() + v.size(), o.client_order_id);
        }
    }

    o.filled_size = o.size - o.unfilled_size;
}

static OMSEventType order_event_type(const Order& o) {
    return (o.state == OrderState::Pending || o.stop_order_type != StopOrderType::None)
           ? OMSEventType::StopOrder : OMSEventType::Order;
}

// ─── deserialize ─────────────────────────────────────────────────────────────

bool CreateOrderRequest::deserialize(std::string_view json, OMSEvent& out, const ProductTable& products) {
    simdjson::ondemand::parser parser;
    auto result = parser.iterate(json.data(), json.size(), json.size() + simdjson::SIMDJSON_PADDING);
    if (result.error()) return false;
    auto doc = std::move(result.value());

    bool success = false;
    if (doc["success"].get_bool().get(success) || !success) return false;

    simdjson::ondemand::object res_obj;
    if (doc["result"].get_object().get(res_obj)) return false;

    out        = {};
    out.source = OMSEventSource::Rest;
    out.action = OMSAction::Create;

    parse_order_fields(res_obj, out.order, products);
    out.type = order_event_type(out.order);

    return true;
}

// ─── GetOpenOrdersRequest ─────────────────────────────────────────────────────

std::string GetOpenOrdersRequest::query() const {
    std::string q;
    q.reserve(64);
    q += "states=";    q += states;
    q += "&page_num="; q += std::to_string(page_num);
    q += "&page_size=";q += std::to_string(page_size);
    return q;
}

int GetOpenOrdersRequest::deserialize_page(std::string_view json,
                                           OMSEvent* out_events,
                                           int max_events,
                                           bool& has_more,
                                           int page_num,
                                           int page_size,
                                           const ProductTable& products) {
    simdjson::ondemand::parser parser;
    auto result = parser.iterate(json.data(), json.size(), json.size() + simdjson::SIMDJSON_PADDING);
    if (result.error()) return 0;
    auto doc = std::move(result.value());

    bool success = false;
    if (doc["success"].get_bool().get(success) || !success) return 0;

    int64_t total_count = 0;
    simdjson::ondemand::object meta;
    if (!doc["meta"].get_object().get(meta)) {
        for (auto field : meta) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;
            if (key == "total_count") { int64_t v; if (!field.value().get_int64().get(v)) total_count = v; }
        }
    }
    has_more = (total_count > static_cast<int64_t>(page_num) * page_size);

    simdjson::ondemand::array arr;
    if (doc["result"].get_array().get(arr)) return 0;

    int count = 0;
    for (auto element : arr) {
        if (count >= max_events) break;
        simdjson::ondemand::object obj;
        if (element.get_object().get(obj)) continue;

        OMSEvent& ev = out_events[count++];
        ev        = {};
        ev.source = OMSEventSource::Reconcile;
        ev.action = OMSAction::Create;

        parse_order_fields(obj, ev.order, products);
        ev.type = order_event_type(ev.order);
    }

    return count;
}
