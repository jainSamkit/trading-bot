#pragma once
#include "feed/sessions/types.hpp"
#include "feed/models/product.hpp"
#include <charconv>

class SpotSession : public Session<SpotSession, DeltaWebsocketClient> {
public:
    explicit SpotSession(DeltaWebsocketClient& client, SessionID sessionID)
        : Session<SpotSession, DeltaWebsocketClient>(client, sessionID) {}

    static double toDouble(std::string_view sv) {
        double val = 0.0;
        ::std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    void onMessage(std::string_view msg) {  // was: unnamed parameter
        // std::cout<<"[raw msg]: "<<msg<<'\n';
        FeedMessage* slot = client_.get_ring_slot();
        slot->type = FeedMessage::Type::SpotPrice;

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
                (*slot).spot_price.price = price;
            } else if (key == "timestamp") {
                if (field.value().get_uint64().get((*slot).mark_price.timestamp)) {}  // was: (*slot).l2.timestamp
            } else if (key == "s") {
                std::string_view index_symbol;
                if (field.value().get_string().get(index_symbol)) return;
                (*slot).spot_price.instrument_id = client_.products_.idfromIndexSymbol(index_symbol);
            }
        }

        if ((*slot).spot_price.instrument_id == UINT8_MAX) return;

        slot->t_kernel = parser_.t_kernel;
        slot->t_frame  = parser_.t_frame;
        slot->t_parse  = now_ns();
        (*slot).instrument_id = (*slot).spot_price.instrument_id;
        client_.commit_to_ring();
    }

    void onAuth() {}

    void onSubscribe() {
        const auto& prods = client_.products_;
        std::string symbols_str;
        for (uint8_t i = 0; i < prods.count; ++i) {
            if (i > 0) symbols_str += ',';
            symbols_str += '"';
            symbols_str += prods[i].index_symbol;
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
    std::string channel_{"v2/spot_price"};
};
