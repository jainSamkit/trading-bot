#include "delta_exchange/api/wallet/wallet.hpp"
#include <simdjson.h>
#include <charconv>
#include <cstring>

// ─── helpers ─────────────────────────────────────────────────────────────────

static double sv_to_double(std::string_view sv) {
    double val = 0.0;
    std::from_chars(sv.data(), sv.data() + sv.size(), val);
    return val;
}

static void parse_wallet_fields(simdjson::ondemand::object& obj, Wallet& w) {
    for (auto field : obj) {
        std::string_view key;
        if (field.unescaped_key().get(key)) continue;

        if (key == "id") {
            uint64_t v; if (!field.value().get_uint64().get(v)) w.id = v;

        } else if (key == "asset_symbol") {
            std::string_view v;
            if (!field.value().get_string().get(v)) {
                size_t n = std::min(v.size(), sizeof(w.asset_symbol) - 1);
                std::memcpy(w.asset_symbol, v.data(), n);
                w.asset_symbol[n] = '\0';
            }

        } else if (key == "balance") {
            std::string_view v; if (!field.value().get_string().get(v)) w.balance = sv_to_double(v);

        } else if (key == "available_balance") {
            std::string_view v; if (!field.value().get_string().get(v)) w.available_balance = sv_to_double(v);

        } else if (key == "blocked_margin") {
            std::string_view v; if (!field.value().get_string().get(v)) w.blocked_margin = sv_to_double(v);

        } else if (key == "order_margin") {
            std::string_view v; if (!field.value().get_string().get(v)) w.order_margin = sv_to_double(v);

        } else if (key == "position_margin") {
            std::string_view v; if (!field.value().get_string().get(v)) w.position_margin = sv_to_double(v);

        } else if (key == "cross_position_margin") {
            std::string_view v; if (!field.value().get_string().get(v)) w.cross_position_margin = sv_to_double(v);

        } else if (key == "cross_order_margin") {
            std::string_view v; if (!field.value().get_string().get(v)) w.cross_order_margin = sv_to_double(v);

        } else if (key == "cross_locked_collateral") {
            std::string_view v; if (!field.value().get_string().get(v)) w.cross_locked_collateral = sv_to_double(v);

        } else if (key == "cross_commission") {
            std::string_view v; if (!field.value().get_string().get(v)) w.cross_commission = sv_to_double(v);

        } else if (key == "cross_asset_liability") {
            std::string_view v; if (!field.value().get_string().get(v)) w.cross_asset_liability = sv_to_double(v);

        } else if (key == "commission") {
            std::string_view v; if (!field.value().get_string().get(v)) w.commission = sv_to_double(v);

        } else if (key == "portfolio_margin") {
            std::string_view v; if (!field.value().get_string().get(v)) w.portfolio_margin = sv_to_double(v);

        } else if (key == "trading_fee_credit") {
            std::string_view v; if (!field.value().get_string().get(v)) w.trading_fee_credit = sv_to_double(v);

        } else if (key == "pending_trading_fee_credit") {
            std::string_view v; if (!field.value().get_string().get(v)) w.pending_trading_fee_credit = sv_to_double(v);

        } else if (key == "referral_bonus") {
            std::string_view v; if (!field.value().get_string().get(v)) w.referral_bonus = sv_to_double(v);

        } else if (key == "pending_referral_bonus") {
            std::string_view v; if (!field.value().get_string().get(v)) w.pending_referral_bonus = sv_to_double(v);
        }
    }
}

// ─── GetWalletRequest ─────────────────────────────────────────────────────────

bool GetWalletRequest::deserialize(std::string_view json, OMSEvent& out) {
    simdjson::ondemand::parser parser;
    auto result = parser.iterate(json.data(), json.size(), json.size() + simdjson::SIMDJSON_PADDING);
    if (result.error()) return false;
    auto doc = std::move(result.value());

    bool success = false;
    if (doc["success"].get_bool().get(success) || !success) return false;

    simdjson::ondemand::array arr;
    if (doc["result"].get_array().get(arr)) return false;

    for (auto element : arr) {
        simdjson::ondemand::object obj;
        if (element.get_object().get(obj)) continue;

        // Peek at asset_symbol before committing to a full parse.
        // simdjson ondemand is forward-only, so we parse the whole object
        // and discard if it's not the tracked asset.
        Wallet w{};
        parse_wallet_fields(obj, w);

        if (std::string_view(w.asset_symbol) != kTrackedAsset) continue;

        out        = {};
        out.type   = OMSEventType::Wallet;
        out.source = OMSEventSource::Reconcile;
        out.action = OMSAction::Create;
        out.wallet = w;
        return true;
    }

    return false;
}
