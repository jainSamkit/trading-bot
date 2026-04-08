#pragma once
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/models/position.hpp"
#include <vector>

// Fetches all open positions from GET /v2/positions.
std::vector<Position> fetch_positions(DeltaRestClient& rest, const std::string& query = "");
