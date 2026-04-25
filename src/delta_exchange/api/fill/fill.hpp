#pragma once
#include "delta_exchange/models/fill.hpp"
#include <simdjson.h>
#include <string>
#include <vector>

class DeltaRestClient;

Fill               parse_fill  (simdjson::ondemand::object& obj);
std::vector<Fill>  fetch_fills (DeltaRestClient& rest, const std::string& query);
