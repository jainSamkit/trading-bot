#pragma once
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/models/position.hpp"
#include "execution/execution_manager.hpp"
#include <string>
#include <string_view>
#include <vector>

// Fetches all open positions from GET /v2/positions.
std::vector<Position> fetch_positions(DeltaRestClient& rest, const std::string& query = "");

struct CloseAllPositionsRequest {
    bool close_all_portfolio = false;
    bool close_all_isolated  = true;
    bool close_all_cross     = false;

    static CloseAllPositionsRequest from_intent(const CloseAllPositions& intent);

    // Serialize to JSON body for DELETE /v2/positions/close_all
    std::string serialize() const;

    // Response is {"result":{}, "success":true}
    static bool parse_success(std::string_view json);
};
