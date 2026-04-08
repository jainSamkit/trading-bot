#include "delta_exchange/api/order.hpp"
#include <simdjson.h>
#include <stdexcept>

std::vector<Order> fetch_orders(DeltaRestClient& rest, const std::string& query) {
    // TODO: implement
    (void)rest;
    (void)query;
    return {};
}

Order fetch_order(DeltaRestClient& rest, int64_t order_id) {
    // TODO: implement
    (void)rest;
    (void)order_id;
    return Order{};
}
