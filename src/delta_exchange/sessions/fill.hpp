#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "delta_exchange/models/fill.hpp"
#include <charconv>
#include <cstring>
#include <iostream>

class FillSession : public Session<FillSession, DeltaOMSWebsocketClient> {
public:
    static constexpr SessionType session_type = SessionType::Private;

    explicit FillSession(DeltaOMSWebsocketClient& client, SessionID sessionID)
        : Session<FillSession, DeltaOMSWebsocketClient>(client, sessionID) {}

    static double svToDouble(std::string_view sv) {
        double val = 0.0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    Fill parseFill(simdjson::ondemand::object& obj) {
        Fill fill{};
        for (auto field : obj) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;

            if (key == "o") {
                uint64_t v; if (!field.value().get_uint64().get(v)) fill.order_id = v;
            } else if (key == "i") {
                int64_t v;
                if (!field.value().get_int64().get(v)) {
                    fill.product_id = static_cast<uint32_t>(v);
                    fill.instrument_id = client_.products_.idfromExchangeID(static_cast<uint32_t>(v));
                }
            } else if (key == "sy") {
                std::string_view v;
                if (!field.value().get_string().get(v)) {
                    size_t n = std::min(v.size(), sizeof(fill.symbol) - 1);
                    std::memcpy(fill.symbol, v.data(), n);
                    fill.symbol[n] = '\0';
                }
            } else if (key == "f") {
                std::string_view v;
                if (!field.value().get_string().get(v)) {
                    size_t n = std::min(v.size(), sizeof(fill.fill_id) - 1);
                    std::memcpy(fill.fill_id, v.data(), n);
                    fill.fill_id[n] = '\0';
                }
            } else if (key == "p") {
                std::string_view v; if (!field.value().get_string().get(v)) fill.price = svToDouble(v);
            } else if (key == "s") {
                std::string_view v; if (!field.value().get_string().get(v)) fill.size = static_cast<uint32_t>(svToDouble(v));
            } else if (key == "po") {
                int64_t v; if (!field.value().get_int64().get(v)) fill.position_after = static_cast<int32_t>(v);
            } else if (key == "S") {
                std::string_view v; if (!field.value().get_string().get(v))
                    fill.side = (v == "buy") ? FillSide::Buy : FillSide::Sell;
            } else if (key == "r") {
                std::string_view v; if (!field.value().get_string().get(v))
                    fill.role = (v == "maker") ? FillRole::Maker : FillRole::Taker;
            } else if (key == "R") {
                std::string_view v; if (!field.value().get_string().get(v)) {
                    if      (v == "adl")         fill.reason = FillReason::Adl;
                    else if (v == "liquidation") fill.reason = FillReason::Liquidation;
                    else                         fill.reason = FillReason::Normal;
                }
            } else if (key == "c") {
                std::string_view v;
                if (!field.value().get_string().get(v)) {
                    uint64_t cid = 0;
                    std::from_chars(v.data(), v.data() + v.size(), cid);
                    fill.client_order_id = cid;
                }
            } else if (key == "se") {
                uint64_t v; if (!field.value().get_uint64().get(v)) fill.seq_no = v;
            } else if (key == "t") {
                uint64_t v; if (!field.value().get_uint64().get(v)) fill.timestamp = v;
            }
        }
        return fill;
    }

    void printFill(const Fill& fill) {
        const char* side_str   = fill.side   == FillSide::Buy        ? "buy"         : "sell";
        const char* role_str   = fill.role   == FillRole::Maker       ? "maker"       : "taker";
        const char* reason_str = fill.reason == FillReason::Adl        ? "adl"
                               : fill.reason == FillReason::Liquidation ? "liquidation" : "normal";
        std::cout << "[fill]"
                  << " pid="      << fill.product_id
                  << " sym="      << fill.symbol
                  << " order_id=" << fill.order_id
                  << " side="     << side_str
                  << " size="     << fill.size
                  << " price="    << fill.price
                  << " role="     << role_str
                  << " reason="   << reason_str
                  << " pos_after="<< fill.position_after
                  << " seq="      << fill.seq_no
                  << "\n\n";
    }

    void onMessage(std::string_view msg) {
        if (msg.find(R"("subscriptions")") != std::string_view::npos) return;

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        simdjson::ondemand::object obj;
        if (doc.get_object().get(obj)) return;
        Fill fill = parseFill(obj);
        printFill(fill);
    }

    void onSubscribe() {
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
    std::string channel_{"v2/user_trades"};
};
