#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/wallet.hpp"
#include "oms/oms_manager.hpp"
#include <charconv>
#include <cstring>
#include <iomanip>
#include <iostream>

class WalletSession : public Session<WalletSession, DeltaOMSWebsocketClient> {
public:
    static constexpr SessionType session_type = SessionType::Private;

    explicit WalletSession(DeltaOMSWebsocketClient& client, SessionID sessionID)
        : Session<WalletSession, DeltaOMSWebsocketClient>(client, sessionID) {}

    static double svToDouble(std::string_view sv) {
        double val = 0.0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    void push_signal(OMSSignal sig) {
        OMSEvent* slot = client_.get_ring_slot();
        if (!slot) return;
        slot->type   = OMSEventType::OMSSignal;
        slot->source = OMSEventSource::Websocket;
        slot->signal = sig;
        client_.commit_to_ring();
    }

    void parseWallet(simdjson::ondemand::object& obj, OMSEvent& event) {
        event.wallet = {};
        Wallet& w = event.wallet;
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
                std::string_view v; if (!field.value().get_string().get(v)) w.balance = svToDouble(v);
            } else if (key == "available_balance") {
                std::string_view v; if (!field.value().get_string().get(v)) w.available_balance = svToDouble(v);
            } else if (key == "blocked_margin") {
                std::string_view v; if (!field.value().get_string().get(v)) w.blocked_margin = svToDouble(v);
            } else if (key == "order_margin") {
                std::string_view v; if (!field.value().get_string().get(v)) w.order_margin = svToDouble(v);
            } else if (key == "position_margin") {
                std::string_view v; if (!field.value().get_string().get(v)) w.position_margin = svToDouble(v);
            } else if (key == "cross_position_margin") {
                std::string_view v; if (!field.value().get_string().get(v)) w.cross_position_margin = svToDouble(v);
            } else if (key == "cross_order_margin") {
                std::string_view v; if (!field.value().get_string().get(v)) w.cross_order_margin = svToDouble(v);
            } else if (key == "cross_locked_collateral") {
                std::string_view v; if (!field.value().get_string().get(v)) w.cross_locked_collateral = svToDouble(v);
            } else if (key == "cross_commission") {
                std::string_view v; if (!field.value().get_string().get(v)) w.cross_commission = svToDouble(v);
            } else if (key == "cross_asset_liability") {
                std::string_view v; if (!field.value().get_string().get(v)) w.cross_asset_liability = svToDouble(v);
            } else if (key == "commission") {
                std::string_view v; if (!field.value().get_string().get(v)) w.commission = svToDouble(v);
            } else if (key == "portfolio_margin") {
                std::string_view v; if (!field.value().get_string().get(v)) w.portfolio_margin = svToDouble(v);
            } else if (key == "trading_fee_credit") {
                std::string_view v; if (!field.value().get_string().get(v)) w.trading_fee_credit = svToDouble(v);
            } else if (key == "pending_trading_fee_credit") {
                std::string_view v; if (!field.value().get_string().get(v)) w.pending_trading_fee_credit = svToDouble(v);
            } else if (key == "referral_bonus") {
                std::string_view v; if (!field.value().get_string().get(v)) w.referral_bonus = svToDouble(v);
            } else if (key == "pending_referral_bonus") {
                std::string_view v; if (!field.value().get_string().get(v)) w.pending_referral_bonus = svToDouble(v);
            } else if (key == "timestamp") {
                uint64_t v; if (!field.value().get_uint64().get(v)) w.timestamp = v;
            }
            // "action", "type", "asset_id", "user_id", "balance_inr", etc. — ignored
        }
    }

    void printWallet(const OMSEvent& event) {
        const Wallet& w = event.wallet;
        std::cout << std::fixed << std::setprecision(8)
                  << "[wallet]"
                  << " sym="          << w.asset_symbol
                  << " balance="      << w.balance
                  << " available="    << w.available_balance
                  << " blocked="      << w.blocked_margin
                  << " pos_margin="   << w.position_margin
                  << " order_margin=" << w.order_margin
                  << " commission="   << w.commission
                  << " fee_credit="   << w.trading_fee_credit
                  << "\n";
    }

    void onMessage(std::string_view msg) {
        if (msg.find(R"("subscriptions")") != std::string_view::npos) return;

        // First message after subscribe — channel is now live, signal valid
        if (waiting_for_first_msg_) {
            waiting_for_first_msg_ = false;
            push_signal(OMSSignal::WalletSnapshotComplete);
        }

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        OMSEvent* slot = client_.get_ring_slot();
        if (!slot) return;
        slot->type   = OMSEventType::Wallet;
        slot->source = OMSEventSource::Websocket;
        slot->action = OMSAction::Update;

        simdjson::ondemand::object obj;
        if (doc.get_object().get(obj)) return;
        parseWallet(obj, *slot);
        printWallet(*slot);
        client_.commit_to_ring();
    }

    void onSubscribe() {
        waiting_for_first_msg_ = true;
        push_signal(OMSSignal::WalletInvalid);

        std::string msg =
            R"({"type":"subscribe","payload":{"channels":[{"name":")"
            + channel_
            + R"(","symbols":["all"]}]}})";

        std::cout << msg << "\n";
        client_.ws_send(ctx_.ssl_, msg);
        client_.enable_heartbeat(ctx_.ssl_);
        arm_timer_ms(DeltaOMSWebsocketClient::HEARTBEAT_TIMEOUT_MS);
    }

private:
    bool        waiting_for_first_msg_ = true;
    std::string channel_{"margins"};
};
