#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include <charconv>
#include<iostream>

using namespace std;

class TradeSession : public Session<TradeSession, DeltaWebsocketClient> {
public:
    static constexpr latency::TagSet::MsgType MSG_TYPE = latency::TagSet::MsgType::Trade;
    static constexpr SessionType session_type = SessionType::Public;

    using Span = latency::Span;

    explicit TradeSession(DeltaWebsocketClient& client, SessionID sessionID)
        : Session<TradeSession, DeltaWebsocketClient>(client, sessionID) {}

    static double toDouble(std::string_view sv) {
        double val = 0.0;
        ::std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    // void parsePriceBand(simdjson::ondemand::value val, MarkPriceData::PriceBand& price_band) {
    //     simdjson::ondemand::object band;
    //     if (val.get_object().get(band)) return;  // was: continue (invalid outside loop)
    //     for (auto f : band) {
    //         std::string_view key;
    //         if (f.unescaped_key().get(key)) continue;
    //         std::string_view value;
    //         if (f.value().get_string().get(value)) continue;
    //         double price = toDouble(value);
    //         if (key == "lower_limit") price_band.lower_limit = price;
    //         if (key == "upper_limit") price_band.upper_limit = price;
    //     }
    // }

    void onMessage(std::string_view msg) {

        // std::cout<<"[trade raw msg]: "<<msg<<'\n';
        FeedMessage* slot;
        {
            Span s(ringwait_hist_);
            slot = client_.get_ring_slot();
        }

        slot->type = FeedMessage::Type::Trade;

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        TradeData& trade_data = slot->trade_data;
        trade_data = TradeData{};

        for (auto field : doc.get_object()) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;

            if (key == "p") {
                std::string_view price_str;
                if (field.value().get_string().get(price_str)) continue;
                trade_data.price = toDouble(price_str);
            } else if (key == "r") {
                std::string_view role_str;
                if (field.value().get_string().get(role_str)) continue;
                trade_data.buyer_role = (role_str.size() == 1 && role_str[0] == 't')
                                   ? TradeData::BuyerRole::Taker
                                   : TradeData::BuyerRole::Maker;
            } else if (key == "s") {
                double size = 0.0;
                if (field.value().get_double().get(size)) continue;
                trade_data.size = size;
            } else if (key == "sy") {
                std::string_view symbol;
                if (field.value().get_string().get(symbol)) return;
                trade_data.instrument_id = client_.products_.idfromSymbol(symbol);
            } else if (key == "t") {
                if (field.value().get_uint64().get(trade_data.trade_time)) {}
            } else if (key == "ts") {
                if (field.value().get_uint64().get(trade_data.publish_time)) {}
            }
        }

        if (trade_data.instrument_id == UINT8_MAX) return;

        slot->t_recv_userspace = parser_.t_recv_userspace;
        slot->t_frame  = parser_.t_frame;
        slot->t_parse  = now_ns();
        slot->instrument_id = trade_data.instrument_id;

        {
            Span s(ringpush_hist_);
            client_.commit_to_ring();
        }

    }

    void onAuth() {}

    void onSubscribe() {
        const auto& prods = client_.products_;
        const auto& instrument_ids = client_.product_groups_.instrument_ids;

        std::string symbols_str;
        for (uint8_t i = 0; i < instrument_ids.size(); ++i) {
            uint8_t instrument_id = instrument_ids[i];
            
            if (i > 0) symbols_str += ',';
            symbols_str += '"';
            symbols_str += prods[instrument_id].symbol;
            symbols_str += '"';
        }
        
        std::string msg =
            R"({"type":"subscribe","payload":{"channels":[{"name":")"
            + channel_
            + R"(","symbols":[)"
            + symbols_str
            + R"(]}]}})";

        std::cout<<msg<<'\n';
        client_.ws_send(ctx_.ssl_, msg);
        client_.enable_heartbeat(ctx_.ssl_);
        arm_timer_ms(DeltaWebsocketClient::HEARTBEAT_TIMEOUT_MS);
    }

private:
    std::string channel_{"trades"};
};
