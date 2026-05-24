#pragma once
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "delta_exchange/models/order.hpp"
#include "oms/oms_manager.hpp"
#include <charconv>
#include <cstring>
#include <iostream>
#include "core/logger.hpp"

class OrderSession : public Session<OrderSession, DeltaOMSWebsocketClient> {
public:
    static constexpr latency::TagSet::MsgType MSG_TYPE = latency::TagSet::MsgType::Order;
    static constexpr SessionType session_type = SessionType::Private;
    using Span = latency::Span;

    explicit OrderSession(DeltaOMSWebsocketClient& client, SessionID sessionID)
        : Session<OrderSession, DeltaOMSWebsocketClient>(client, sessionID) {}

    static double svToDouble(std::string_view sv) {
        double val = 0.0;
        std::from_chars(sv.data(), sv.data() + sv.size(), val);
        return val;
    }

    // Parses order fields directly into event.order; sets event.action and seq_no_out from JSON fields.
    void parseOrder(simdjson::ondemand::object& obj, OMSEvent& event, uint64_t& seq_no_out) {
        event.order = {};
        Order& order = event.order;
        for (auto field : obj) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;

            if (key == "id") {
                uint64_t v; if (!field.value().get_uint64().get(v)) order.id = v;
            } else if (key == "product_id") {
                int64_t v; if (!field.value().get_int64().get(v)) order.product_id = static_cast<uint32_t>(v);
            } else if (key == "product_symbol") {
                std::string_view v;
                if (!field.value().get_string().get(v)) {
                    order.instrument_id = client_.products_.idfromSymbol(v);
                    size_t n = std::min(v.size(), sizeof(order.symbol) - 1);
                    std::memcpy(order.symbol, v.data(), n);
                    order.symbol[n] = '\0';
                }
            } else if (key == "side") {
                std::string_view v; if (!field.value().get_string().get(v))
                    order.side = (v == "buy") ? OrderSide::Buy : OrderSide::Sell;
            } else if (key == "size") {
                uint64_t v; if (!field.value().get_uint64().get(v)) order.size = static_cast<uint32_t>(v);
            } else if (key == "filled_size") {
                std::string_view v; if (!field.value().get_string().get(v)) order.filled_size = static_cast<uint32_t>(svToDouble(v));
            } else if (key == "unfilled_size") {
                int64_t v; if (!field.value().get_int64().get(v)) order.unfilled_size = static_cast<uint32_t>(v);
            } else if (key == "limit_price") {
                std::string_view v; if (!field.value().get_string().get(v)) order.limit_price = svToDouble(v);
            } else if (key == "stop_price") {
                    std::string_view v; if (!field.value().get_string().get(v)) order.stop_price = svToDouble(v);
            } else if (key == "average_fill_price") {
                std::string_view v; if (!field.value().get_string().get(v)) order.average_fill_price = svToDouble(v);
            } else if (key == "paid_commission") {
                std::string_view v; if (!field.value().get_string().get(v)) order.paid_commission = svToDouble(v);
            } else if (key == "state") {
                std::string_view v; if (!field.value().get_string().get(v)) {
                    if      (v == "open")      order.state = OrderState::Open;
                    else if (v == "closed")    order.state = OrderState::Closed;
                    else if (v == "cancelled") order.state = OrderState::Cancelled;
                    else if (v == "pending")   order.state = OrderState::Pending;
                }
            } else if (key == "stop_order_type") {
                std::string_view v; if (!field.value().get_string().get(v)) {
                    if      (v == "take_profit_order")      order.stop_order_type = StopOrderType::TP;
                    else if (v == "stop_loss_order")        order.stop_order_type = StopOrderType::SL;
                }
            } else if (key == "stop_trigger_method") {
                std::string_view v; if (!field.value().get_string().get(v)) {
                    if      (v == "mark_price")             order.stop_trigger_method = StopTriggerMethod::Mark;
                    else if (v == "last_traded_price")        order.stop_trigger_method = StopTriggerMethod::LTP;
                    else if (v == "spot_price")        order.stop_trigger_method = StopTriggerMethod::Spot;
                }
            } else if (key == "order_type") {
                std::string_view v; if (!field.value().get_string().get(v))
                    order.order_type = (v == "market_order") ? OrderType::Market : OrderType::Limit;
            } else if (key == "time_in_force") {
                std::string_view v; if (!field.value().get_string().get(v))
                    order.time_in_force = (v == "gtc") ? TimeInForce::GTC : TimeInForce::IOC;
            } else if (key == "client_order_id") {
                std::string_view v;
                if (!field.value().get_string().get(v)) {
                    uint64_t cid = 0;
                    std::from_chars(v.data(), v.data() + v.size(), cid);
                    order.client_order_id = cid;
                }
            } else if (key == "fill_id") {
                std::string_view v;
                if (!field.value().get_string().get(v)) {
                    size_t n = std::min(v.size(), sizeof(order.fill_id) - 1);
                    std::memcpy(order.fill_id, v.data(), n);
                    order.fill_id[n] = '\0';
                }
            } else if (key == "reduce_only") {
                bool v; if (!field.value().get_bool().get(v)) order.reduce_only = v;
            } else if (key == "seq_no") {
                uint64_t v; if (!field.value().get_uint64().get(v)) seq_no_out = v;
            } else if (key == "timestamp") {
                uint64_t v; if (!field.value().get_uint64().get(v)) order.timestamp = v;
            } else if (key == "action") {
                std::string_view v; if (!field.value().get_string().get(v)) {
                    if      (v == "create") event.action = OMSAction::Create;
                    else if (v == "update") event.action = OMSAction::Update;
                    else if (v == "delete") event.action = OMSAction::Delete;
                }
            } else if (key == "reason") {
                std::string_view v; if (!field.value().get_string().get(v)) {
                    if      (v == "fill")         order.reason = OrderReason::Fill;
                    else if (v == "stop_create")  order.reason = OrderReason::StopCreate;
                    else if (v == "stop_update")  order.reason = OrderReason::StopUpdate;
                    else if (v == "stop_trigger") order.reason = OrderReason::StopTrigger;
                    else if (v == "stop_cancel")  order.reason = OrderReason::StopCancel;
                    else if (v == "liquidation")  order.reason = OrderReason::Liquidation;
                    else if (v == "self_trade")   order.reason = OrderReason::SelfTrade;
                }
            }
        }
    }

    void printOrder(const OMSEvent& event) {
        const Order& order = event.order;
        const char* action_str = event.action == OMSAction::Create ? "create"
                               : event.action == OMSAction::Update ? "update" : "delete";
        const char* state_str  = order.state == OrderState::Open      ? "open"
                               : order.state == OrderState::Closed    ? "closed"
                               : order.state == OrderState::Cancelled    ? "cancelled"
                               : order.state == OrderState::Pending    ? "pending" : "unknown";

        const char* side_str   = order.side == OrderSide::Buy ? "buy" : "sell";
        const char* reason_str = order.reason == OrderReason::Fill         ? "fill"
                               : order.reason == OrderReason::StopUpdate  ? "stop_update"
                               : order.reason == OrderReason::StopTrigger ? "stop_trigger"
                               : order.reason == OrderReason::StopCancel  ? "stop_cancel"
                               : order.reason == OrderReason::Liquidation  ? "liquidation"
                               : order.reason == OrderReason::SelfTrade   ? "self_trade" : "unknown";
        std::cout << "[order]"
                  << " action="    << action_str
                  << " id="        << order.id
                  << " pid="       << order.product_id
                  << " sym="       << order.symbol
                  << " side="      << side_str
                  << " size="      << order.size
                  << " filled="    << order.filled_size
                  << " unfilled="  << order.unfilled_size
                  << " limit_px="  << order.limit_price
                  << " avg_px="    << order.average_fill_price
                  << " state="     << state_str
                  << " reason="    << reason_str
                  << " commission="<< order.paid_commission
                  << "\n";
    }

    void push_signal(OMSSignal sig) {

        OMSEvent* slot;
        {
            Span s(ringwait_hist_);
            slot = client_.get_ring_slot();
        }
        
        if (!slot) return;

        *slot = {};
        slot->type   = OMSEventType::OMSSignal;
        slot->source = OMSEventSource::Websocket;
        slot->signal = sig;

        {
            Span s(ringpush_hist_);
            client_.commit_to_ring();
        }
    }

    void onMessage(std::string_view msg) {
        // std::cout<<"[order raw msg]: "<<msg<<'\n';
        if (msg.find(R"("subscriptions")") != std::string_view::npos) return;

        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) return;
        auto doc = std::move(result.value());

        // Snapshot: per-symbol message; seq_no is in meta, not in result entries.
        if (msg.find(R"("snapshot")") != std::string_view::npos) {
            uint64_t snap_seq = 0;
            uint64_t snap_ts  = 0;
            uint8_t  snap_instrument_id = UINT8_MAX;

            for (auto field : doc.get_object()) {
                std::string_view key;
                if (field.unescaped_key().get(key)) continue;

                if (key == "meta") {
                    simdjson::ondemand::object meta;
                    if (!field.value().get_object().get(meta)) {
                        for (auto mf : meta) {
                            std::string_view mk;
                            if (mf.unescaped_key().get(mk)) continue;
                            if (mk == "seq_no")    { uint64_t v; if (!mf.value().get_uint64().get(v)) snap_seq = v; }
                            if (mk == "timestamp") { uint64_t v; if (!mf.value().get_uint64().get(v)) snap_ts  = v; }
                        }
                    }
                } else if (key == "result") {
                    simdjson::ondemand::array arr;
                    if (field.value().get_array().get(arr)) return;
                    for (auto elem : arr) {
                        simdjson::ondemand::object obj;
                        if (elem.get_object().get(obj)) continue;

                        OMSEvent* slot;
                        {
                            Span s(ringwait_hist_);
                            slot = client_.get_ring_slot();
                        }

                        if (!slot) continue;  // ring full — drop
                        *slot = {};
                        slot->type   = OMSEventType::Order;
                        slot->source = OMSEventSource::Websocket;
                        slot->action = OMSAction::Create;  // forced: snapshot = existing open orders
                        uint64_t unused_seq = 0;
                        parseOrder(obj, *slot, unused_seq);
                        if(slot->order.client_order_id == 0) slot->order.client_order_id = slot->order.id;
                        slot->order.timestamp = snap_ts;
                        snap_instrument_id = slot->order.instrument_id;  // all entries same symbol

                        if(slot -> order.state  == OrderState::Pending) {
                            slot->type = OMSEventType::StopOrder;
                        }
                        
                        printOrder(*slot);
                        {
                            Span s(ringpush_hist_);
                            client_.commit_to_ring();
                        }
                    }
                }
            }

            // Set seq baseline for this symbol from meta.seq_no
            if (snap_instrument_id != UINT8_MAX)
                orderSeq_[snap_instrument_id] = snap_seq;

            if (in_snapshot_) {
                in_snapshot_ = false;
                push_signal(OMSSignal::OrdersSnapshotComplete);
            }

            return;
        }

        if(in_snapshot_) return;
        // CRUD update — single order, all fields at top level including seq_no.

        OMSEvent* slot;
        {
            Span s(ringwait_hist_);
            slot = client_.get_ring_slot();
        }
        
        if (!slot) return;  // ring full — drop
        *slot = {};
        slot->type   = OMSEventType::Order;
        slot->source = OMSEventSource::Websocket;
        slot->order  = {};

        simdjson::ondemand::object obj;
        if (doc.get_object().get(obj)) return;
        uint64_t seq_no = 0;
        parseOrder(obj, *slot, seq_no);

        if (orderSeq_[slot->order.instrument_id] !=0 && seq_no != orderSeq_[slot->order.instrument_id] + 1) {
            orderSeq_.fill(0);
            push_signal(OMSSignal::OrdersInvalid);
            in_snapshot_ = true;
            reconnect();
            return;  // no commit — slot abandoned
        }

        orderSeq_[slot->order.instrument_id] = seq_no;
        if(slot -> order.state  == OrderState::Pending || (slot -> action == OMSAction::Delete && (slot -> order.reason == OrderReason::StopCancel || slot -> order.reason == OrderReason::StopTrigger))) {
            slot->type = OMSEventType::StopOrder;
        }

        if(slot->order.client_order_id == 0) slot->order.client_order_id = slot->order.id;

        printOrder(*slot);
        {
            Span s(ringpush_hist_);
            client_.commit_to_ring();
        }
    }

    void onSubscribe() {

        orderSeq_.fill(0);
        push_signal(OMSSignal::OrdersInvalid);
        in_snapshot_ = true;

        std::string msg =
            R"({"type":"subscribe","payload":{"channels":[{"name":")"
            + channel_
            + R"(","symbols":["all"]}]}})";

        // std::cout << msg << "\n";
        client_.ws_send(ctx_.ssl_, msg);
        client_.enable_heartbeat(ctx_.ssl_);
        arm_timer_ms(DeltaOMSWebsocketClient::HEARTBEAT_TIMEOUT_MS);
    }

private:
    std::array<uint64_t, ProductTable::MAX_INSTRUMENTS> orderSeq_{};
    bool        in_snapshot_ = true;
    std::string channel_{"orders"};
};
