#pragma once
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/models/wallet.hpp"
#include <vector>

// Fetches all wallet balances from GET /v2/wallet/balances.
std::vector<Wallet> fetch_wallet(DeltaRestClient& rest);
