#pragma once

#include "core/venue.hpp"
#include <cstdint>
#include <tuple>

namespace latency {

struct TagSet {

    using Venue = core::Venue;

    enum class EventType : uint8_t {
        WsRead, WsFrame, JsonParse, RingPush, RingWait,
        Handler, ShmWrite, BookUpdate, TradeRingPush, WireOut,
        TickToTrade, QueueTime
    };

    enum class MsgType : uint8_t {
        None,        // for stages that don't have a per-msg breakdown
        L2, Mark, Spot, OHLC, Trade,
        OB200, Wallet, Position, Order
    };

    enum class Target : uint8_t {
        None,        // for stages without a target
        MarketState, Mark, Spot, ExecutionManager, Strategy
    };

    EventType event_type;
    MsgType   msg_type = MsgType::None;
    Target    target   = Target::None;

    bool operator<(const TagSet& o) const noexcept {
        return std::tie(event_type, msg_type, target)
             < std::tie(o.event_type, o.msg_type, o.target);
    }

    bool operator==(const TagSet& o) const noexcept {
        return event_type == o.event_type
            && msg_type   == o.msg_type
            && target     == o.target;
    }

    // ── Static stringifiers — NOT `const` (static functions cannot be cv-qualified) ──
    static const char* to_string(EventType e) noexcept {
        switch (e) {
            case EventType::WsRead:         return "ws_read";
            case EventType::WsFrame:        return "ws_frame";
            case EventType::JsonParse:      return "json_parse";
            case EventType::RingPush:       return "ring_push";
            case EventType::RingWait:       return "ring_wait";
            case EventType::QueueTime:      return "queue_time";
            case EventType::Handler:        return "handler";
            case EventType::ShmWrite:       return "shm_write";
            case EventType::BookUpdate:     return "book_update";
            case EventType::TradeRingPush:  return "trade_ring_push";
            case EventType::WireOut:        return "wire_out";
            case EventType::TickToTrade:    return "tick_to_trade";
        }
        return "unknown";
    }

    static const char* to_string(MsgType m) noexcept {
        switch (m) {
            case MsgType::None:  return nullptr;   // emit no tag
            case MsgType::L2:    return "l2";
            case MsgType::Mark:  return "mark";
            case MsgType::Spot:  return "spot";
            case MsgType::OHLC:  return "ohlc";
            case MsgType::Trade: return "trade";
            case MsgType::OB200: return "ob200";
            case MsgType::Order: return "order";
            case MsgType::Position: return "position";
            case MsgType::Wallet: return "wallet";
        }
        return "unknown";
    }

    static const char* to_string(Target t) noexcept {
        switch (t) {
            case Target::None:        return nullptr;
            case Target::MarketState: return "market_state";
            case Target::ExecutionManager:        return "execution_manager";
            case Target::Strategy:        return "strategy";
            case Target::Mark:        return "mark";
            case Target::Spot:        return "spot";
        }
        return "unknown";
    }

    static const char* to_string(Venue v) noexcept {
        switch(v) {
            case Venue::Delta: return "delta";
            case Venue::Bybit: return "bybit";
            case Venue::Count: return "unknown";
        }
        return "unknown";
    }
};

}  // namespace latency
