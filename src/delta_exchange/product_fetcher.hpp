#pragma once
#include "delta_exchange/rest_client.hpp"
#include "models/product.hpp"
#include <simdjson.h>
#include <cstring>
#include <stdexcept>
#include <string>

// Fetches a single product from GET /v2/products/{symbol}.
// Public endpoint — no auth required.
// Throws on HTTP error or parse failure.
inline Product fetch_product(DeltaRestClient& rest, const std::string& symbol) {
    auto res = rest.get_product(symbol);
    if (!res)
        throw std::runtime_error("fetch_product: no response for " + symbol);
    if (res->status != 200)
        throw std::runtime_error("fetch_product: HTTP " + std::to_string(res->status)
                                 + " for " + symbol);

    simdjson::ondemand::parser parser;
    simdjson::padded_string    body(res->body);
    auto doc    = parser.iterate(body);
    auto result = doc["result"];

    Product p{};

    p.exchange_id = static_cast<uint32_t>(int64_t(result["id"]));

    auto copy_str = [](std::string_view sv, char* dst, size_t n) {
        size_t len = std::min(sv.size(), n - 1);
        std::memcpy(dst, sv.data(), len);
        dst[len] = '\0';
    };

    copy_str(std::string_view(result["symbol"]),               p.symbol,       sizeof(p.symbol));
    copy_str(std::string_view(result["spot_index"]["symbol"]), p.index_symbol, sizeof(p.index_symbol));

    // numeric fields arrive as decimal strings e.g. "0.5"
    auto parse_double = [](simdjson::ondemand::value v) {
        return std::stod(std::string(std::string_view(v)));
    };

    p.tick_size                          = parse_double(result["tick_size"]);
    p.contract_value                     = parse_double(result["contract_value"]);
    p.initial_margin                     = parse_double(result["initial_margin"]);
    p.maintenance_margin                 = parse_double(result["maintenance_margin"]);
    p.initial_margin_scaling_factor      = parse_double(result["initial_margin_scaling_factor"]);
    p.maintenance_margin_scaling_factor  = parse_double(result["maintenance_margin_scaling_factor"]);
    p.taker_commission_rate              = parse_double(result["taker_commission_rate"]);
    p.maker_commission_rate              = parse_double(result["maker_commission_rate"]);
    p.default_leverage                   = parse_double(result["default_leverage"]);
    p.price_band                         = parse_double(result["price_band"]);

    // integer fields
    p.impact_size         = static_cast<uint32_t>(int64_t(result["impact_size"]));
    p.position_size_limit = static_cast<uint32_t>(int64_t(result["position_size_limit"]));
    p.max_leverage_notional = parse_double(result["max_leverage_notional"]);

    // contract type
    std::string_view ct = result["contract_type"];
    if      (ct == "perpetual_futures") p.contract_type = Product::ContractType::Perpetual;
    else if (ct == "futures")           p.contract_type = Product::ContractType::Futures;
    else                                p.contract_type = Product::ContractType::Options;

    // no absolute price bounds in response — price_band is a % around mark price
    p.lower_bound_price = 0.0;
    p.upper_bound_price = 0.0;

    return p;
}
