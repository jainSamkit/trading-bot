#pragma once
#include "delta_exchange/models/wallet.hpp"
#include "oms/oms_manager.hpp"
#include <string_view>

// Only this asset symbol is tracked; all other entries are skipped.
static constexpr const char* kTrackedAsset = "USD";

struct GetWalletRequest {
    // GET /v2/wallet/balances — no query params.
    // Finds the kTrackedAsset entry and pushes one OMSEvent into out.
    // Returns true if the tracked asset was found and parsed.
    static bool deserialize(std::string_view json, OMSEvent& out);
};
