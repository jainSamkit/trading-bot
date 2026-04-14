#include "delta_exchange/api/wallet.hpp"
#include <simdjson.h>
#include <stdexcept>

Wallet parse_wallet(simdjson::ondemand::object& obj) {
    // TODO: implement
    (void)obj;
    return Wallet{};
}

std::vector<Wallet> fetch_wallet(DeltaRestClient& rest) {
    // TODO: implement
    (void)rest;
    return {};
}
