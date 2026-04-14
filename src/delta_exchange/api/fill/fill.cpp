#include "delta_exchange/api/fill.hpp"
#include <simdjson.h>
#include <stdexcept>

Fill parse_fill(simdjson::ondemand::object& obj) {
    // TODO: implement
    (void)obj;
    return Fill{};
}

std::vector<Fill> fetch_fills(DeltaRestClient& rest, const std::string& query) {
    // TODO: implement
    (void)rest;
    (void)query;
    return {};
}
