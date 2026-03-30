#pragma once
#include "feed/sessions/types.hpp"
#include "feed/models/product.hpp"
#include <charconv>
#include <cstdlib>
#include <cstring>
// #include <iostream>
// std::ostream& operator<<(std::ostream& os, const L2Level& l) {
//     return os << "[" << l.price << ", " << l.size << "]";
// }
// std::ostream& operator<<(std::ostream& os, const L2Update& u) {
//     os << "L2Update{"
//        << "id=" << (int)u.instrument_id
//        << " seq=" << u.sequence_no
//        << " ts=" << u.timestamp
//        << " snap=" << u.isSnapshot
//        << "\n  asks(" << (int)u.ask_count << "): ";
//     for (uint8_t i = 0; i < u.ask_count; ++i)
//         os << u.asks[i] << " ";
//     os << "\n  bids(" << (int)u.bid_count << "): ";
//     for (uint8_t i = 0; i < u.bid_count; ++i)
//         os << u.bids[i] << " ";
//     return os << "\n}";
// }


class L2UpdateSession : public Session<L2UpdateSession, DeltaWebsocketClient> {

    static constexpr uint8_t MAX_LEVELS = MAX_FEED_LEVELS;

public:
    explicit L2UpdateSession(DeltaWebsocketClient& client, SessionID sessionID)
        : Session<L2UpdateSession, DeltaWebsocketClient>(client, sessionID) {}

    static double toDouble(std::string_view sv) {
        double val = 0.0;
        ::std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    static void convertToTick(L2Update& l2, const Product& p) {
        for (uint8_t i = 0; i < l2.ask_count; ++i) {
            l2.asks[i].price *= p.inv_tick_size;
        }
        for (uint8_t i = 0; i < l2.bid_count; ++i) {
            l2.bids[i].price *= p.inv_tick_size;
        }
    }

    static uint8_t parseLevels(simdjson::ondemand::value arr_val, L2Level* levels) {
        simdjson::ondemand::array arr;
        if (arr_val.get_array().get(arr)) return 0;
        uint8_t count = 0;

        for (auto level : arr) {
            if (count >= MAX_LEVELS) break;

            simdjson::ondemand::array pair;
            if (level.get_array().get(pair)) continue;

            auto it = pair.begin();
            if (it == pair.end()) continue;

            std::string_view price_str;
            if ((*it).get_string().get(price_str)) continue;

            ++it;

            if (it == pair.end()) continue;
            std::string_view size_str;
            if ((*it).get_string().get(size_str)) continue;


            levels[count].price = toDouble(price_str);
            levels[count].size  = toDouble(size_str);

            count++;
        }
        return count;
    }

    void onMessage(std::string_view msg) {
        FeedMessage* slot = client_.get_ring_slot();
        slot->type = FeedMessage::Type::L2Feed;

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        for (auto field : doc.get_object()) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;

            if (key == "action") {
                std::string_view action;
                if (field.value().get_string().get(action)) continue;
                (*slot).l2.isSnapshot = (action == "snapshot");
            } else if (key == "asks") {
                (*slot).l2.ask_count = parseLevels(field.value(), (*slot).l2.asks);
            } else if (key == "bids") {
                (*slot).l2.bid_count = parseLevels(field.value(), (*slot).l2.bids);
            } else if (key == "sequence_no") {
                if (field.value().get_uint64().get((*slot).l2.sequence_no)) return;
            } else if (key == "symbol") {
                std::string_view symbol;
                if (field.value().get_string().get(symbol)) return;
                (*slot).l2.instrument_id = client_.products_.idfromSymbol(symbol);
            } else if (key == "timestamp") {
                if(field.value().get_uint64().get((*slot).l2.timestamp)) {};
            }
        }

        if ((*slot).l2.instrument_id == UINT8_MAX)
            return;

        if ((*slot).l2.isSnapshot) {
            seq_no_[(*slot).l2.instrument_id] = (*slot).l2.sequence_no;
            book_valid_[(*slot).l2.instrument_id] = true;
        } else {
            if (seq_no_[(*slot).l2.instrument_id] + 1 != (*slot).l2.sequence_no) {
                book_valid_[(*slot).l2.instrument_id] = false;
                seq_no_[(*slot).l2.instrument_id] = 0;
                return;
            }
            seq_no_[(*slot).l2.instrument_id] = (*slot).l2.sequence_no;
        }

        const Product& product = client_.products_[(*slot).l2.instrument_id];
        convertToTick(slot->l2, product);
        
        (*slot).instrument_id = (*slot).l2.instrument_id;
        client_.commit_to_ring();
        // const int64_t t_parse = now_ns();
        // std::cout << (*slot).l2
        //           << " recv_to_frame_ns="  << (parser_.t_frame - parser_.t_kernel)
        //           << " frame_to_parse_ns=" << (t_parse         - parser_.t_frame)
        //           << " total_ns="          << (t_parse         - parser_.t_kernel)
        //           << "\n\n";
    }

    void onSubscribe() {
        const auto& prods = client_.products_;
        std::string symbols_str;
        for (uint8_t i = 0; i < prods.count; ++i) {
            if (i > 0) symbols_str += ',';
            symbols_str += '"';
            symbols_str += prods[i].symbol;
            symbols_str += '"';
        }

        std::string msg =
            R"({"type":"subscribe","payload":{"channels":[{"name":")"
            + channel_
            + R"(","symbols":[)"
            + symbols_str
            + R"(]}]}})";

        client_.ws_send(ctx_.ssl_, msg);
        client_.enable_heartbeat(ctx_.ssl_);
        arm_timer_ms(DeltaWebsocketClient::HEARTBEAT_TIMEOUT_MS);
    }

    void onAuth() {}

    bool bookValid(uint8_t id) const { return book_valid_[id]; }
    uint64_t seqNo(uint8_t id) const { return seq_no_[id]; }

private:
    std::string channel_{"l2_updates"};
    uint64_t    seq_no_[ProductTable::MAX_INSTRUMENTS]{};
    bool        book_valid_[ProductTable::MAX_INSTRUMENTS]{};
};
