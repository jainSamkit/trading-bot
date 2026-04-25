#include "delta_exchange/api/position/position.hpp"
#include <simdjson.h>
#include <charconv>
#include <cstring>
#include <string>

// ─── helpers ─────────────────────────────────────────────────────────────────

static double sv_to_double(std::string_view sv) {
    double val = 0.0;
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return val;
}

static void parse_position_fields(simdjson::ondemand::object& obj, Position& p,
                                  const ProductTable& products) {
    for (auto field : obj) {
        std::string_view key;
        if (field.unescaped_key().get(key)) continue;

        if (key == "product_id") {
            int64_t v; if (!field.value().get_int64().get(v)) p.product_id = static_cast<uint32_t>(v);

        } else if (key == "product_symbol") {
            std::string_view v;
            if (!field.value().get_string().get(v)) {
                p.instrument_id = products.idfromSymbol(v);
                size_t n = std::min(v.size(), sizeof(p.symbol) - 1);
                std::memcpy(p.symbol, v.data(), n);
                p.symbol[n] = '\0';
            }

        } else if (key == "size") {
            int64_t v; if (!field.value().get_int64().get(v)) p.size = static_cast<int32_t>(v);

        } else if (key == "entry_price") {
            std::string_view v; if (!field.value().get_string().get(v)) p.entry_price = sv_to_double(v);

        } else if (key == "margin") {
            std::string_view v; if (!field.value().get_string().get(v)) p.margin = sv_to_double(v);

        } else if (key == "liquidation_price") {
            std::string_view v; if (!field.value().get_string().get(v)) p.liquidation_price = sv_to_double(v);

        } else if (key == "bankruptcy_price") {
            std::string_view v; if (!field.value().get_string().get(v)) p.bankruptcy_price = sv_to_double(v);

        } else if (key == "commission") {
            std::string_view v; if (!field.value().get_string().get(v)) p.commission = sv_to_double(v);

        } else if (key == "realized_pnl") {
            std::string_view v; if (!field.value().get_string().get(v)) p.realized_pnl = sv_to_double(v);

        } else if (key == "realized_cashflow") {
            std::string_view v; if (!field.value().get_string().get(v)) p.realized_cashflow = sv_to_double(v);

        } else if (key == "realized_funding") {
            std::string_view v; if (!field.value().get_string().get(v)) p.realized_funding = sv_to_double(v);

        } else if (key == "margin_mode") {
            std::string_view v;
            if (!field.value().get_string().get(v))
                p.margin_mode = (v == "isolated") ? MarginMode::Isolated : MarginMode::Cross;

        } else if (key == "auto_topup") {
            bool v; if (!field.value().get_bool().get(v)) p.auto_topup = v;
        }
    }
}

// ─── GetOpenPositionsRequest ──────────────────────────────────────────────────

int GetOpenPositionsRequest::deserialize_all(std::string_view json,
                                             OMSEvent* out_events,
                                             int max_events,
                                             const ProductTable& products) {
    simdjson::ondemand::parser parser;
    auto result = parser.iterate(json.data(), json.size(), json.size() + simdjson::SIMDJSON_PADDING);
    if (result.error()) return 0;
    auto doc = std::move(result.value());

    bool success = false;
    if (doc["success"].get_bool().get(success) || !success) return 0;

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
        ev.type   = OMSEventType::Position;

        parse_position_fields(obj, ev.position, products);
    }

    return count;
}

// ─── CloseAllPositionsRequest ─────────────────────────────────────────────────

CloseAllPositionsRequest CloseAllPositionsRequest::from_intent(const CloseAllPositions& intent) {
    CloseAllPositionsRequest req;
    req.close_all_portfolio = intent.close_all_portfolio;
    req.close_all_isolated  = intent.close_all_isolated;
    req.close_all_cross     = intent.close_all_cross;
    return req;
}

std::string CloseAllPositionsRequest::serialize() const {
    std::string body;
    body.reserve(96);
    body += "{\"close_all_portfolio\":"; body += close_all_portfolio ? "true" : "false";
    body += ",\"close_all_isolated\":";  body += close_all_isolated  ? "true" : "false";
    body += ",\"close_all_cross\":";     body += close_all_cross     ? "true" : "false";
    body += "}";
    return body;
}

bool CloseAllPositionsRequest::parse_success(std::string_view json) {
    simdjson::ondemand::parser parser;
    auto result = parser.iterate(json.data(), json.size(), json.size() + simdjson::SIMDJSON_PADDING);
    if (result.error()) return false;
    auto doc = std::move(result.value());
    bool success = false;
    if (doc["success"].get_bool().get(success)) return false;
    return success;
}
