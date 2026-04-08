#pragma once
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/models/order.hpp"
#include <vector>

// Fetches all open orders from GET /v2/orders.
std::vector<Order> fetch_orders(DeltaRestClient& rest, const std::string& query = "");

// Fetches a single order by exchange order id from GET /v2/orders/{id}.
Order fetch_order(DeltaRestClient& rest, int64_t order_id);
