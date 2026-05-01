#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include <charconv>

class OHLCSession : public Session<OHLCSession, DeltaWebsocketClient> {

public:
    static constexpr SessionType session_type = SessionType::Public;
    explicit OHLCSession(DeltaWebsocketClient& client, SessionID sessionID)
        : Session<OHLCSession, DeltaWebsocketClient>(client, sessionID) {}


    void onMessage(std::string_view msg) {
        // std::cout<<"[raw msg]: "<<msg<<'\n';
        FeedMessage* slot = client_.get_ring_slot();
        slot->type = FeedMessage::Type::OHLC;

        auto& ohlc_slot = slot->ohlc;
        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        std::string_view msg_type;

        for (auto field : doc.get_object()) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;
            if (key == "candle_start_time") {
                if (field.value().get_uint64().get(ohlc_slot.start_time)) return;
            } else if (key == "c") {
                if (field.value().get_double().get(ohlc_slot.close)) return;
            } else if(key == "h") {
                if (field.value().get_double().get(ohlc_slot.high)) return;
            } else if(key == "l") {
                if (field.value().get_double().get(ohlc_slot.low)) return;
        } else if(key == "o") {
                if (field.value().get_double().get(ohlc_slot.open)) return;
            } else if(key == "res") {
                std::string_view res;
                if (field.value().get_string().get(res)) return;
                auto it = std::find(ohlc_resolutions.begin(), ohlc_resolutions.end(), res);
                if (it == ohlc_resolutions.end()) return;
                ohlc_slot.res_idx = static_cast<uint8_t>(it - ohlc_resolutions.begin());
            } else if (key == "sy") {
                std::string_view symbol;
                if (field.value().get_string().get(symbol)) return;
                if (symbol.starts_with("MARK:")) {
                    ohlc_slot.is_mark = true;
                    symbol.remove_prefix(5);
                } else {
                    ohlc_slot.is_mark = false;
                }

                slot->instrument_id = client_.products_.idfromSymbol(symbol);
                if(slot->instrument_id == UINT8_MAX) return;
            } else if (key =="ts") {
                if (field.value().get_uint64().get(ohlc_slot.timestamp)) {};
            } else if(key == "v") {
                if (field.value().get_double().get(ohlc_slot.volume)) {};
            } else if(key == "type") {
                if (field.value().get_string().get(msg_type)) {};
                if(msg_type == "subscriptions") return;
            }
        }

        slot->t_kernel = parser_.t_kernel;
        slot->t_frame  = parser_.t_frame;
        slot->t_parse  = now_ns();
        client_.commit_to_ring();
    }

    void onAuth() {}

    void onSubscribe() {
        const auto& prods = client_.products_;
        const auto& instrument_ids = client_.product_groups_.instrument_ids;

        std::string symbols_str;
        std::string mark_symbols_str;

        for (uint8_t i = 0; i < instrument_ids.size(); ++i) {
            uint8_t instrument_id = instrument_ids[i];

            if (i > 0) symbols_str += ',';
            symbols_str += '"';
            symbols_str += prods[instrument_id].symbol;
            symbols_str += '"';
        }

        for (uint8_t i = 0; i < instrument_ids.size(); ++i) {
            uint8_t instrument_id = instrument_ids[i];
            if (i > 0) mark_symbols_str += ',';
            mark_symbols_str += '"';
            mark_symbols_str += "MARK:";
            mark_symbols_str += prods[instrument_id].symbol;
            mark_symbols_str += '"';
        }

        std::string msg = R"({"type":"subscribe","payload":{"channels":[)";

        for (size_t i = 0; i < trade_price_resolutions.size(); ++i) {
            if (i > 0) msg += ',';
            msg += R"({"name":"candlestick_)";
            msg += trade_price_resolutions[i];
            msg += R"(","symbols":[)";
            msg += symbols_str;
            if (std::find(mark_price_resolutions.begin(), mark_price_resolutions.end(), trade_price_resolutions[i]) != mark_price_resolutions.end()) {
                msg += ',';
                msg += mark_symbols_str;
            }
            msg += "]}";
        }

        msg += R"(]}})";

        std::cout<<msg<<'\n';
        client_.ws_send(ctx_.ssl_, msg);
        client_.enable_heartbeat(ctx_.ssl_);
        arm_timer_ms(DeltaWebsocketClient::HEARTBEAT_TIMEOUT_MS);
    }

private:
    // static constexpr std::array<std::string_view, 3>trade_price_resolutions {"1m", "5m", "30m"};
    static constexpr std::array<std::string_view, 3>trade_price_resolutions {"1m", "5m", "30m"};
    static constexpr std::array<std::string_view, 1>mark_price_resolutions{"1m"};
    // static constexpr std::array<std::string_view, 1>mark_price_resolutions{};
};
