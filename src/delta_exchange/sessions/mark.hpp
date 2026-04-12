#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include <charconv>

class MarkSession : public Session<MarkSession, DeltaWebsocketClient> {
public:
    static constexpr SessionType session_type = SessionType::Public;

    explicit MarkSession(DeltaWebsocketClient& client, SessionID sessionID)
        : Session<MarkSession, DeltaWebsocketClient>(client, sessionID) {}

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

    void onMessage(std::string_view msg) {  // was: unnamed parameter
        // std::cout<<"[raw msg]: "<<msg<<'\n';
        FeedMessage* slot = client_.get_ring_slot();
        slot->type = FeedMessage::Type::MarkPrice;

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        for (auto field : doc.get_object()) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;
            if (key == "p") {
                std::string_view price_str;
                if (field.value().get_string().get(price_str)) continue;
                (*slot).mark_price.price = toDouble(price_str);
            // } else if (key == "sy") {
            //     parsePriceBand(field.value(), (*slot).mark_price.price_band);  // was: parseLevels
            } else if (key == "ts") {
                if (field.value().get_uint64().get((*slot).mark_price.timestamp)) {}  // was: (*slot).l2.timestamp
            } else if (key == "sy") {
                std::string_view symbol;
                if (field.value().get_string().get(symbol)) return;
                if (symbol.starts_with("MARK:")) symbol.remove_prefix(5);
                (*slot).mark_price.instrument_id = client_.products_.idfromSymbol(symbol);
            }
        }

        if ((*slot).mark_price.instrument_id == UINT8_MAX) return;

        slot->t_kernel = parser_.t_kernel;
        slot->t_frame  = parser_.t_frame;
        slot->t_parse  = now_ns();
        (*slot).instrument_id = (*slot).mark_price.instrument_id;
        client_.commit_to_ring();
    }

    void onAuth() {}

    void onSubscribe() {
        const auto& prods = client_.products_;
        const auto& instrument_ids = client_.product_groups_.instrument_ids;

        std::string symbols_str;
        for (uint8_t i = 0; i < instrument_ids.size(); ++i) {
            uint8_t instrument_id = instrument_ids[i];
            if (i > 0) symbols_str += ',';
            symbols_str += R"("MARK:)";
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
    std::string channel_{"mark_price"};
};
