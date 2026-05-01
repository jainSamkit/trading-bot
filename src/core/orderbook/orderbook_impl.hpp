#pragma once
#include <algorithm>
#include <cmath>

#include "orderbook.hpp"

// ── Deferred initialisation ──────────────────────────────────────────────────
inline void OrderBook::init(const uint8_t product_id, Tick min_tick, Tick max_tick, const Product&  product)
{   
    assert(max_tick > min_tick && "need max_tick > min_tick");
    product_id_ = product_id;
    product_ = &product;
    num_ticks_ = to_int(max_tick-min_tick+Tick{1});
    min_tick_ = min_tick;
    max_tick_ = max_tick;
    best_bid_tick_ = Tick{-1};
    best_ask_tick_ = Tick{-1};
    bid_ladder_tail_tick_ = Tick{-1};
    ask_ladder_tail_tick_ = Tick{-1};
    bid_ladder_filled_ = 0;
    ask_ladder_filled_ = 0;
    bid_qty_per_tick_ = std::make_unique<Contracts[]>(num_ticks_);
    ask_qty_per_tick_ = std::make_unique<Contracts[]>(num_ticks_);
    market_snapshot_ = {};
}

// ── Incremental side updates ─────────────────────────────────────────────────
// Only rescan when the deleted level was the current best.

inline void OrderBook::applyBidTick(Tick tick, Contracts size)
{
    assert(inRangeTick(tick));
    bid_qty_per_tick_[to_int(tick)] = size;

    if (size == Contracts{0}) {
        if (best_bid_tick_ == tick) {
            int32_t i = to_int(tick) - 1;
            while (i >= 0 && bid_qty_per_tick_[i] == Contracts{0})
                --i;
            best_bid_tick_ = Tick{i};
        }
    } else {
        if (best_bid_tick_ < Tick{0} || tick > best_bid_tick_) best_bid_tick_ = tick;
    }
}

inline void OrderBook::applyAskTick(Tick tick, Contracts size)
{
    assert(inRangeTick(tick));
    ask_qty_per_tick_[to_int(tick)] = size;

    if (size == Contracts{0}) {
        if (best_ask_tick_ == tick) {
            int32_t i = to_int(tick) + 1;
            while (i <= to_int(max_tick_) && ask_qty_per_tick_[i] == Contracts{0})
                ++i;
            if (i > to_int(max_tick_))
                best_ask_tick_ = Tick{-1};
            else
                best_ask_tick_ = Tick{i};
        }
    } else {
        if (best_ask_tick_ < Tick{0} || tick < best_ask_tick_) best_ask_tick_ = tick;
    }
}

// ── Top-of-book ladders ─────────────────────────────────────────────────────

inline void OrderBook::refillBidLadder()
{
    uint8_t n = 0;
    Tick tail_tick {-1};
    
    for (int32_t i = to_int(best_bid_tick_); i >= 0 && n < Depth; --i) {
        const Tick t = Tick{i};
        const Contracts q  = bid_qty_per_tick_[i];
        if (q != Contracts{0}) {
            market_snapshot_.bids[n].tick = t;
            market_snapshot_.bids[n].size = q;
            tail_tick = t;
            ++n;
        }
    }

    uint8_t const filled = n;
    for (; n < Depth; ++n) {
        market_snapshot_.bids[n].tick = {};
        market_snapshot_.bids[n].size = {};
    }

    bid_ladder_filled_    = filled;
    bid_ladder_tail_tick_ = tail_tick;
}


inline void OrderBook::refillAskLadder()
{
    uint8_t n      = 0;
    Tick tail_tick {-1};

    for (int32_t i = to_int(best_ask_tick_);
         i >= 0 && i <= to_int(max_tick_) && n < Depth;
         ++i) {
        const Tick t = Tick{i};
        const Contracts q  = ask_qty_per_tick_[i];
        if (q != Contracts{0}) {
            market_snapshot_.asks[n].tick = t;
            market_snapshot_.asks[n].size = q;
            tail_tick = t;
            ++n;
        }
    }

    uint8_t const filled = n;
    for (; n < Depth; ++n) {
        market_snapshot_.asks[n].tick = {};
        market_snapshot_.asks[n].size = {};
    }

    ask_ladder_filled_    = filled;
    ask_ladder_tail_tick_ = tail_tick;
}

// ── Snapshot / update ───────────────────────────────────────────────────────


inline void OrderBook::onSnapshot(L2Update const& msg)
{
    assert(msg.isSnapshot == true);

    std::fill_n(bid_qty_per_tick_.get(), num_ticks_, Contracts{0});
    std::fill_n(ask_qty_per_tick_.get(), num_ticks_, Contracts{0});
    best_bid_tick_        = Tick{-1};
    best_ask_tick_        = Tick{-1};
    bid_ladder_tail_tick_ = Tick{-1};
    ask_ladder_tail_tick_ = Tick{-1};
    bid_ladder_filled_    = 0;
    ask_ladder_filled_    = 0;

    for (uint8_t i = 0; i < msg.bid_count; ++i) {
        const Tick tick = Tick{product_->price_to_tick(msg.bids[i].price)};
        if (!inRangeTick(tick))
            continue;
        const Contracts q = product_->to_contracts(msg.bids[i].size);
        if (to_int(q) == 0)
            continue;
        bid_qty_per_tick_[to_int(tick)] = q;
        if (best_bid_tick_ < Tick{0} || tick > best_bid_tick_) best_bid_tick_ = tick;
    }

    for (uint8_t i = 0; i < msg.ask_count; ++i) {
        const Tick tick = Tick{product_->price_to_tick(msg.asks[i].price)};
        if (!inRangeTick(tick))
            continue;
        const Contracts q = product_->to_contracts(msg.asks[i].size);
        if (to_int(q) == 0)
            continue;
        ask_qty_per_tick_[to_int(tick)] = q;
        if (best_ask_tick_ < Tick{0} || tick < best_ask_tick_) best_ask_tick_ = tick;
    }

    refillBidLadder();
    refillAskLadder();
}


inline void OrderBook::onUpdate(L2Update const& msg)
{
    assert(msg.isSnapshot == false);

    bool refill_bid = false;
    bool refill_ask = false;

    for (uint8_t i = 0; i < msg.bid_count; ++i) {
        const Tick tick = Tick{product_->price_to_tick(msg.bids[i].price)};
        if (!inRangeTick(tick))
            continue;

        const Tick best_before = best_bid_tick_;
        const Contracts num_contracts = product_->to_contracts(msg.bids[i].size);
        applyBidTick(tick, num_contracts);
        const bool best_changed = (best_bid_tick_ != best_before);
        const bool may_affect_ladder =
            (bid_ladder_tail_tick_ < Tick{0}) || (bid_ladder_filled_ < Depth) ||
            (tick >= bid_ladder_tail_tick_);
        refill_bid |= (best_changed || may_affect_ladder);
    }

    for (uint8_t i = 0; i < msg.ask_count; ++i) {
        const Tick tick = Tick{product_->price_to_tick(msg.asks[i].price)};
        if (!inRangeTick(tick))
            continue;

        const Tick best_before = best_ask_tick_;
        const Contracts num_contracts = product_->to_contracts(msg.asks[i].size);
        applyAskTick(tick, num_contracts);
        const bool best_changed = (best_ask_tick_ != best_before);
        const bool may_affect_ladder =
            (ask_ladder_tail_tick_ < Tick{0}) || (ask_ladder_filled_ < Depth) ||
            (tick <= ask_ladder_tail_tick_);
        refill_ask |= (best_changed || may_affect_ladder);
    }

    if (refill_bid)
        refillBidLadder();
    if (refill_ask)
        refillAskLadder();
}


inline void OrderBook::update(L2Update const& msg) {

    market_snapshot_.last_seq = msg.sequence_no;
    market_snapshot_.last_update_ts_ns = msg.timestamp;

    if (msg.isSnapshot) {
        onSnapshot(msg);
    } else {
        onUpdate(msg);
    }
}

// ── Display accessors ─────────────────────────────────────────────────────────


inline double OrderBook::mid() const
{
    if (market_snapshot_.bids[0].size == Contracts{0} || market_snapshot_.asks[0].size == Contracts{0})
        return 0.0;
    const double bid_price = product_->tick_to_price(market_snapshot_.bids[0].tick);
    const double ask_price = product_->tick_to_price(market_snapshot_.asks[0].tick);
    return 0.5 * (bid_price + ask_price);
}


inline Tick OrderBook::bestBidPlusAsk() const
{
    if (market_snapshot_.bids[0].size == Contracts{0} || market_snapshot_.asks[0].size == Contracts{0})
        return Tick{0};
    return market_snapshot_.bids[0].tick + market_snapshot_.asks[0].tick;
}


inline Tick OrderBook::spread() const
{
    if (to_int(market_snapshot_.bids[0].size) <= 0 || to_int(market_snapshot_.asks[0].size) <= 0) return Tick{-1};
    return market_snapshot_.asks[0].tick - market_snapshot_.bids[0].tick;
}


inline Tick OrderBook::bestBidTick() const
{
    if (to_int(best_bid_tick_) < 0) return Tick{-1};
    return best_bid_tick_;
}


inline Tick OrderBook::bestAskTick() const
{
    if (to_int(best_ask_tick_) < 0) return Tick{-1};
    return best_ask_tick_;
}


inline const MarketSnapshot& OrderBook::snapshot()
{
    market_snapshot_.best_bid = bestBidTick();
    market_snapshot_.best_ask = bestAskTick();
    market_snapshot_.bestBidPlusAsk = bestBidPlusAsk();
    market_snapshot_.spread = spread();

    return market_snapshot_;
}