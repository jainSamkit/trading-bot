#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <iostream>
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
public:
    static constexpr SessionType session_type = SessionType::Public;
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
            if (count >= MAX_FEED_LEVELS) break;
            
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

            levels[count].price  = toDouble(price_str);
            levels[count].size   = toDouble(size_str);

            count++;
        }
        return count;
    }

    void onMessage(std::string_view msg) {
        FeedMessage* slot = client_.get_ring_slot();
        slot->type = FeedMessage::Type::L2Feed;
        auto& l2_slot = slot->l2;
        l2_slot   = L2Update{};   // reset stale ring slot state

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        std::string_view raw_action;  // capture what the exchange actually sent

        for (auto field : doc.get_object()) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;

            if (key == "action") {
                std::string_view action;
                if (field.value().get_string().get(action)) continue;
                raw_action = action;
                l2_slot.isSnapshot = (action != "update");
            } else if (key == "a") {
                l2_slot.ask_count = parseLevels(field.value(), l2_slot.asks);
            } else if (key == "b") {
                l2_slot.bid_count = parseLevels(field.value(), l2_slot.bids);
            } else if (key == "seq") {
                if (field.value().get_uint64().get(l2_slot.sequence_no)) return;
            } else if (key == "sy") {
                std::string_view symbol;
                if (field.value().get_string().get(symbol)) return;
                l2_slot.instrument_id = client_.products_.idfromSymbol(symbol);
            } else if (key == "ts") {
                if(field.value().get_uint64().get(l2_slot.timestamp)) {};
            }
        }

        if (l2_slot.instrument_id == UINT8_MAX) return;

    // const Product& product = client_.products_[l2_slot.instrument_id];
        // convertToTick(slot->l2, product);

        // const L2Update& u = slot->l2;
        // std::cout << "L2Update{"
        //           << " sym=" << product.symbol
        //           << " seq=" << u.sequence_no
        //           << " snap=" << u.isSnapshot
        //           << " asks(" << (int)u.ask_count << "):";
        // for (uint8_t i = 0; i < u.ask_count; ++i)
        //     std::cout << " [" << u.asks[i].price << "," << u.asks[i].size << "]";
        // std::cout << " bids(" << (int)u.bid_count << "):";
        // for (uint8_t i = 0; i < u.bid_count; ++i)
        //     std::cout << " [" << u.bids[i].price << "," << u.bids[i].size << "]";
        // std::cout << " }\n";

        uint8_t id = l2_slot.instrument_id;
        if (!book_valid_[id]) {
            seq_no_[id]    = l2_slot.sequence_no;
            book_valid_[id] = true;
            l2_slot.isSnapshot = true;
        } else {
            if (seq_no_[id] + 1 != l2_slot.sequence_no) {
                std::cerr << "[l2] seq gap: expected " << seq_no_[id] + 1
                          << " got " << l2_slot.sequence_no << " — dropping\n";
                book_valid_[id] = false;
                seq_no_[id]    = 0;
                return;
            }
            seq_no_[id] = l2_slot.sequence_no;
            l2_slot.isSnapshot = false;
        }

        slot->t_kernel = parser_.t_kernel;
        slot->t_frame  = parser_.t_frame;
        slot->t_parse  = now_ns();
        slot->instrument_id = l2_slot.instrument_id;
        client_.commit_to_ring();
        // const int64_t t_parse = now_ns();
        // std::cout << l2_slot
        //           << " recv_to_frame_ns="  << (parser_.t_frame - parser_.t_kernel)
        //           << " frame_to_parse_ns=" << (t_parse         - parser_.t_frame)
        //           << " total_ns="          << (t_parse         - parser_.t_kernel)
        //           << "\n\n";
    }

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

    void onAuth() {return;}

    bool bookValid(uint8_t id) const { return book_valid_[id]; }
    uint64_t seqNo(uint8_t id) const { return seq_no_[id]; }

private:
    std::string channel_{"ob_updates"};
    uint64_t    seq_no_[MAX_INSTRUMENTS]{};
    bool        book_valid_[MAX_INSTRUMENTS]{};
};
