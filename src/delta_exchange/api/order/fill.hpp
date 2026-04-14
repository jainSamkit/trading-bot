#pragma once
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/models/fill.hpp"
#include <vector>

// Fetches recent fills from GET /v2/fills.
std::vector<Fill> fetch_fills(DeltaRestClient& rest, const std::string& query = "");
