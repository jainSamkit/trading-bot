#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include <charconv>
#include<iostream>
using namespace std;


class SpotSession : public Session<SpotSession, DeltaWebsocketClient> {
public:
    static constexpr latency::TagSet::MsgType MSG_TYPE = latency::TagSet::MsgType::Spot;
    static constexpr SessionType session_type = SessionType::Public;

    using Span = latency::Span;
    explicit SpotSession(DeltaWebsocketClient& client, SessionID sessionID)
        : Session<SpotSession, DeltaWebsocketClient>(client, sessionID) {}

    static double toDouble(std::string_view sv) {
        double val = 0.0;
        ::std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    void onMessage(std::string_view msg) {  // was: unnamed parameter
        // std::cout<<"[raw msg]: "<<msg<<'\n';
        FeedMessage* slot;
        {
            Span s(ringwait_hist_);
            slot = client_.get_ring_slot();
        }

        slot->type = FeedMessage::Type::SpotPrice;
        auto& spot_price_slot = slot->spot_price_data;
        spot_price_slot = SpotPriceData{};

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        for (auto field : doc.get_object()) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;
            if (key == "p") {
                double price = 0.0;
                if (field.value().get_double().get(price)) continue;
                spot_price_slot.price = price;
            } else if (key == "sy") {
                std::string_view index_symbol;
                if (field.value().get_string().get(index_symbol)) return;
                spot_price_slot.instrument_id = client_.products_.idfromIndexSymbol(index_symbol);
            }
        }

        if (spot_price_slot.instrument_id == UINT8_MAX) return;

        slot->t_recv_userspace = parser_.t_recv_userspace;
        slot->t_frame  = parser_.t_frame;
        slot->t_parse  = latency::now_ns();
        slot->instrument_id = spot_price_slot.instrument_id;

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
            symbols_str += prods[instrument_id].index_symbol;
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
    std::string channel_{"spot_price"};
};
