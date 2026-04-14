#include "delta_exchange/api/position.hpp"
#include <simdjson.h>
#include <stdexcept>
#include <string>

Position parse_position(simdjson::ondemand::object& obj) {
    // TODO: implement
    (void)obj;
    return Position{};
}

std::vector<Position> fetch_positions(DeltaRestClient& rest, const std::string& query) {
    // TODO: implement
    (void)rest;
    (void)query;
    return {};
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
