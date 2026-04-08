#include "delta_exchange/api/position.hpp"
#include <simdjson.h>
#include <stdexcept>

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
