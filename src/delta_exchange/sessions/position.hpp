#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "delta_exchange/models/position.hpp"
#include "delta_exchange/models/fill.hpp"
#include "oms/oms_manager.hpp"
#include <charconv>
#include <cstring>
#include <iostream>

class PositionFillSession : public Session<PositionFillSession, DeltaOMSWebsocketClient> {
public:
    static constexpr SessionType session_type = SessionType::Private;

    explicit PositionFillSession(DeltaOMSWebsocketClient& client, SessionID sessionID)
        : Session<PositionFillSession, DeltaOMSWebsocketClient>(client, sessionID) {}

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

    // Parses position fields into event.position. Works for both snapshot entries and live updates.
    void parsePosition(simdjson::ondemand::object& obj, OMSEvent& event) {
        event.position = {};
        Position& pos = event.position;
        for (auto field : obj) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;

            if (key == "product_id") {
                int64_t v; if (!field.value().get_int64().get(v)) pos.product_id = static_cast<uint32_t>(v);
            } else if (key == "product_symbol") {
                std::string_view v;
                if (!field.value().get_string().get(v)) {
                    pos.instrument_id = client_.products_.idfromSymbol(v);
                    size_t n = std::min(v.size(), sizeof(pos.symbol) - 1);
                    std::memcpy(pos.symbol, v.data(), n);
                    pos.symbol[n] = '\0';
                }
            } else if (key == "size") {
                int64_t v; if (!field.value().get_int64().get(v)) pos.size = static_cast<int32_t>(v);
            } else if (key == "entry_price") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.entry_price = svToDouble(v);
            } else if (key == "margin") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.margin = svToDouble(v);
            } else if (key == "liquidation_price") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.liquidation_price = svToDouble(v);
            } else if (key == "bankruptcy_price") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.bankruptcy_price = svToDouble(v);
            } else if (key == "commission") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.commission = svToDouble(v);
            } else if (key == "realized_pnl") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.realized_pnl = svToDouble(v);
            } else if (key == "realized_cashflow") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.realized_cashflow = svToDouble(v);
            } else if (key == "realized_funding") {
                std::string_view v; if (!field.value().get_string().get(v)) pos.realized_funding = svToDouble(v);
            } else if (key == "margin_mode") {
                std::string_view v; if (!field.value().get_string().get(v))
                    pos.margin_mode = (v == "cross") ? MarginMode::Cross : MarginMode::Isolated;
            } else if (key == "auto_topup") {
                bool v; if (!field.value().get_bool().get(v)) pos.auto_topup = v;
            } else if (key == "under_liquidation") {
                bool v; if (!field.value().get_bool().get(v)) pos.under_liquidation = v;
            } else if (key == "timestamp") {
                uint64_t v; if (!field.value().get_uint64().get(v)) pos.timestamp = v;
            }
            // "product", "action", "symbol", "updated_at", "created_at", etc. — ignored
        }
    }

    void parseFill(simdjson::ondemand::object& obj, OMSEvent& event, uint64_t& seq_no_out) {
        event.fill = {};
        Fill& fill = event.fill;
        for (auto field : obj) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;

            if (key == "o") {
                uint64_t v; if (!field.value().get_uint64().get(v)) fill.order_id = v;
            } else if (key == "i") {
                int64_t v;
                if (!field.value().get_int64().get(v)) {
                    fill.product_id   = static_cast<uint32_t>(v);
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
                uint64_t v; if (!field.value().get_uint64().get(v)) seq_no_out = v;
            } else if (key == "t") {
                uint64_t v; if (!field.value().get_uint64().get(v)) fill.timestamp = v;
            }
        }
    }

    void printPosition(const OMSEvent& event) {
        const Position& pos = event.position;
        const char* action_str = event.action == OMSAction::Create ? "create" : "update";
        const char* mode_str   = pos.margin_mode == MarginMode::Isolated ? "isolated" : "cross";
        std::cout << "[position]"
                  << " action="    << action_str
                  << " pid="       << pos.product_id
                  << " sym="       << pos.symbol
                  << " size="      << pos.size
                  << " entry_px="  << pos.entry_price
                  << " liq_px="    << pos.liquidation_price
                  << " margin="    << pos.margin
                  << " mode="      << mode_str
                  << " rpnl="      << pos.realized_pnl
                  << " under_liq=" << pos.under_liquidation
                  << "\n";
    }

    void printFill(const OMSEvent& event) {
        const Fill& fill = event.fill;
        const char* side_str   = fill.side   == FillSide::Buy         ? "buy"         : "sell";
        const char* role_str   = fill.role   == FillRole::Maker        ? "maker"       : "taker";
        const char* reason_str = fill.reason == FillReason::Adl        ? "adl"
                               : fill.reason == FillReason::Liquidation ? "liquidation" : "normal";
        std::cout << "[fill]"
                  << " pid="       << fill.product_id
                  << " sym="       << fill.symbol
                  << " order_id="  << fill.order_id
                  << " side="      << side_str
                  << " size="      << fill.size
                  << " price="     << fill.price
                  << " role="      << role_str
                  << " reason="    << reason_str
                  << " pos_after=" << fill.position_after
                  << "\n";
    }

    void onMessage(std::string_view msg) {
        if (msg.find(R"("subscriptions")") != std::string_view::npos) return;

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        // Fill update — routed by type field
        if (msg.find(R"("v2/user_trades")") != std::string_view::npos) {
            OMSEvent* slot = client_.get_ring_slot();
            if (!slot) return;
            slot->type   = OMSEventType::Fill;
            slot->source = OMSEventSource::Websocket;

            simdjson::ondemand::object obj;
            if (doc.get_object().get(obj)) return;
            uint64_t seq_no = 0;
            parseFill(obj, *slot, seq_no);

            if (seq_no != fillSeq_[slot->fill.instrument_id] + 1) {
                fillSeq_.fill(0);
                push_signal(OMSSignal::PositionsInvalid);
                reconnect();
                return;  // no commit — slot abandoned
            }
            fillSeq_[slot->fill.instrument_id] = seq_no;
            printFill(*slot);
            client_.commit_to_ring();
            return;
        }

        // Position snapshot — single message, all symbols in result[]
        if (msg.find(R"("snapshot")") != std::string_view::npos) {
            push_signal(OMSSignal::PositionsInvalid);

            for (auto field : doc.get_object()) {
                std::string_view key;
                if (field.unescaped_key().get(key)) continue;
                if (key != "result") continue;

                simdjson::ondemand::array arr;
                if (field.value().get_array().get(arr)) return;
                for (auto elem : arr) {
                    simdjson::ondemand::object obj;
                    if (elem.get_object().get(obj)) continue;
                    OMSEvent* slot = client_.get_ring_slot();
                    if (!slot) continue;  // ring full — drop
                    slot->type   = OMSEventType::Position;
                    slot->source = OMSEventSource::Websocket;
                    slot->action = OMSAction::Create;
                    parsePosition(obj, *slot);
                    printPosition(*slot);
                    client_.commit_to_ring();
                }
            }

            push_signal(OMSSignal::PositionsSnapshotComplete);
            return;
        }

        // Position live update — fields at top level
        OMSEvent* slot = client_.get_ring_slot();
        if (!slot) return;
        slot->type   = OMSEventType::Position;
        slot->source = OMSEventSource::Websocket;
        slot->action = OMSAction::Update;

        simdjson::ondemand::object obj;
        if (doc.get_object().get(obj)) return;
        parsePosition(obj, *slot);
        printPosition(*slot);
        client_.commit_to_ring();
    }

    void onSubscribe() {
        fillSeq_.fill(0);
        push_signal(OMSSignal::PositionsInvalid);

        std::string msg =
            R"({"type":"subscribe","payload":{"channels":[{"name":"positions","symbols":["all"]},{"name":"v2/user_trades","symbols":["all"]}]}})";

        std::cout << msg << "\n";
        client_.ws_send(ctx_.ssl_, msg);
        client_.enable_heartbeat(ctx_.ssl_);
        arm_timer_ms(DeltaOMSWebsocketClient::HEARTBEAT_TIMEOUT_MS);
    }

private:
    std::array<uint64_t, ProductTable::MAX_INSTRUMENTS> fillSeq_{};
};
