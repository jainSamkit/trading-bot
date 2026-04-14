#pragma once
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/models/product.hpp"
#include <string>

// Fetches and parses a single product from GET /v2/products/{symbol}.
// Public endpoint — no auth required.
// Throws on HTTP error or parse failure.
Product fetch_product(DeltaRestClient& rest, const std::string& symbol);
