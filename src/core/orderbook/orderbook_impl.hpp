#pragma once
#include <algorithm>
#include <cmath>

#include "orderbook.hpp"

// ── Deferred initialisation ──────────────────────────────────────────────────

template <uint8_t Depth>
void OrderBook<Depth>::init(uint8_t const product_id,
                            double        lower_bound_price,
                            double        upper_bound_price,
                            double        tick_width)
{
    assert(upper_bound_price > lower_bound_price && "need upper > lower");
    assert(tick_width > 0.0 && "tick_width must be positive");

    product_id_           = product_id;
    lower_bound_          = lower_bound_price;
    upper_bound_          = upper_bound_price;
    tick_width_           = tick_width;
    num_levels_           = static_cast<uint32_t>(
        std::llround((upper_bound_price - lower_bound_price) / tick_width) + 1);
    best_bid_tick_        = -1;
    best_ask_tick_        = -1;
    bid_ladder_tail_tick_ = -1;
    ask_ladder_tail_tick_ = -1;
    bid_ladder_filled_    = 0;
    ask_ladder_filled_    = 0;

    bid_qty_.assign(num_levels_, 0);
    ask_qty_.assign(num_levels_, 0);

    assert(num_levels_ >= 2);
}

template <uint8_t Depth>
double OrderBook<Depth>::priceFromTick(uint32_t tick) const
{
    assert(inRangeTick(tick));
    return lower_bound_ + static_cast<double>(tick) * tick_width_;
}

// ── Incremental side updates ─────────────────────────────────────────────────
// Only rescan when the deleted level was the current best.

template <uint8_t Depth>
void OrderBook<Depth>::applyBidTick(uint32_t tick, uint64_t size)
{
    assert(inRangeTick(tick));
    bid_qty_[tick] = size;

    if (size == 0) {
        if (best_bid_tick_ == static_cast<int32_t>(tick)) {
            int32_t i = static_cast<int32_t>(tick) - 1;
            while (i >= 0 && bid_qty_[static_cast<uint32_t>(i)] == 0)
                --i;
            best_bid_tick_ = i;
        }
    } else {
        if (best_bid_tick_ < 0 || static_cast<int32_t>(tick) > best_bid_tick_)
            best_bid_tick_ = static_cast<int32_t>(tick);
    }
}

template <uint8_t Depth>
void OrderBook<Depth>::applyAskTick(uint32_t tick, uint64_t size)
{
    assert(inRangeTick(tick));
    ask_qty_[tick] = size;

    if (size == 0) {
        if (best_ask_tick_ == static_cast<int32_t>(tick)) {
            int32_t i = static_cast<int32_t>(tick) + 1;
            while (static_cast<uint32_t>(i) < num_levels_ && ask_qty_[static_cast<uint32_t>(i)] == 0)
                ++i;
            if (static_cast<uint32_t>(i) >= num_levels_)
                best_ask_tick_ = -1;
            else
                best_ask_tick_ = i;
        }
    } else {
        if (best_ask_tick_ < 0 || static_cast<int32_t>(tick) < best_ask_tick_)
            best_ask_tick_ = static_cast<int32_t>(tick);
    }
}

// ── Top-of-book ladders ─────────────────────────────────────────────────────

template <uint8_t Depth>
void OrderBook<Depth>::refillBidLadder()
{
    uint8_t n         = 0;
    int32_t tail_tick = -1;

    for (int32_t i = best_bid_tick_; i >= 0 && n < Depth; --i) {
        uint32_t const ui = static_cast<uint32_t>(i);
        uint64_t const q  = bid_qty_[ui];
        if (q != 0) {
            bid_[n].tick     = ui;
            bid_[n].size = q;
            tail_tick = static_cast<int32_t>(ui);
            ++n;
        }
    }

    uint8_t const filled = n;
    for (; n < Depth; ++n) {
        bid_[n].tick     = 0;
        bid_[n].size = 0;
    }

    bid_ladder_filled_    = filled;
    bid_ladder_tail_tick_ = tail_tick;
}

template <uint8_t Depth>
void OrderBook<Depth>::refillAskLadder()
{
    uint8_t n         = 0;
    int32_t tail_tick = -1;

    for (int32_t i = best_ask_tick_;
         i >= 0 && static_cast<uint32_t>(i) < num_levels_ && n < Depth;
         ++i) {
        uint32_t const ui = static_cast<uint32_t>(i);
        uint64_t const q  = ask_qty_[ui];
        if (q != 0) {
            ask_[n].tick     = ui;
            ask_[n].size = q;
            tail_tick = static_cast<int32_t>(ui);
            ++n;
        }
    }

    uint8_t const filled = n;
    for (; n < Depth; ++n) {
        ask_[n].tick     = 0;
        ask_[n].size = 0;
    }

    ask_ladder_filled_    = filled;
    ask_ladder_tail_tick_ = tail_tick;
}

// ── Snapshot / update ───────────────────────────────────────────────────────

template <uint8_t Depth>
void OrderBook<Depth>::onSnapshot(L2Update const& msg)
{
    assert(msg.isSnapshot == true);

    std::fill(bid_qty_.begin(), bid_qty_.end(), 0);
    std::fill(ask_qty_.begin(), ask_qty_.end(), 0);
    best_bid_tick_        = -1;
    best_ask_tick_        = -1;
    bid_ladder_tail_tick_ = -1;
    ask_ladder_tail_tick_ = -1;
    bid_ladder_filled_    = 0;
    ask_ladder_filled_    = 0;

    for (uint8_t i = 0; i < msg.bid_count; ++i) {
        uint32_t const tick = static_cast<uint32_t>(msg.bids[i].price);
        if (!inRangeTick(tick))
            continue;
        uint64_t const q = static_cast<uint64_t>(msg.bids[i].size);
        if (q == 0)
            continue;
        bid_qty_[tick] = q;
        if (best_bid_tick_ < 0 || static_cast<int32_t>(tick) > best_bid_tick_)
            best_bid_tick_ = static_cast<int32_t>(tick);
    }

    for (uint8_t i = 0; i < msg.ask_count; ++i) {
        uint32_t const tick = static_cast<uint32_t>(msg.asks[i].price);
        if (!inRangeTick(tick))
            continue;
        uint64_t const q = static_cast<uint64_t>(msg.asks[i].size);
        if (q == 0)
            continue;
        ask_qty_[tick] = q;
        if (best_ask_tick_ < 0 || static_cast<int32_t>(tick) < best_ask_tick_)
            best_ask_tick_ = static_cast<int32_t>(tick);
    }

    refillBidLadder();
    refillAskLadder();
}

template <uint8_t Depth>
void OrderBook<Depth>::onUpdate(L2Update const& msg)
{
    assert(msg.isSnapshot == false);

    bool refill_bid = false;
    bool refill_ask = false;

    for (uint8_t i = 0; i < msg.bid_count; ++i) {
        uint32_t const tick = static_cast<uint32_t>(msg.bids[i].price);
        if (!inRangeTick(tick))
            continue;

        int32_t const ti          = static_cast<int32_t>(tick);
        int32_t const best_before = best_bid_tick_;
        applyBidTick(tick, static_cast<uint64_t>(msg.bids[i].size));
        bool const best_changed = (best_bid_tick_ != best_before);
        bool const may_affect_ladder =
            (bid_ladder_tail_tick_ < 0) || (bid_ladder_filled_ < Depth) ||
            (ti >= bid_ladder_tail_tick_);
        refill_bid |= (best_changed || may_affect_ladder);
    }

    for (uint8_t i = 0; i < msg.ask_count; ++i) {
        uint32_t const tick = static_cast<uint32_t>(msg.asks[i].price);
        if (!inRangeTick(tick))
            continue;

        int32_t const ti          = static_cast<int32_t>(tick);
        int32_t const best_before = best_ask_tick_;
        applyAskTick(tick, static_cast<uint64_t>(msg.asks[i].size));
        bool const best_changed = (best_ask_tick_ != best_before);
        bool const may_affect_ladder =
            (ask_ladder_tail_tick_ < 0) || (ask_ladder_filled_ < Depth) ||
            (ti <= ask_ladder_tail_tick_);
        refill_ask |= (best_changed || may_affect_ladder);
    }

    if (refill_bid)
        refillBidLadder();
    if (refill_ask)
        refillAskLadder();
}

template <uint8_t Depth>
void OrderBook<Depth>::update(L2Update const& msg) {
    if (msg.isSnapshot) {
        onSnapshot(msg);
    } else {
        onUpdate(msg);
    }
}

// ── Display accessors ─────────────────────────────────────────────────────────

template <uint8_t Depth>
double OrderBook<Depth>::mid() const
{
    if (bid_[0].size == 0 || ask_[0].size == 0)
        return 0.0;
    return 0.5 * (priceFromTick(bid_[0].tick) + priceFromTick(ask_[0].tick));
}

template <uint8_t Depth>
double OrderBook<Depth>::spread() const
{
    if (bid_[0].size == 0 || ask_[0].size == 0)
        return 0.0;
    return priceFromTick(ask_[0].tick) - priceFromTick(bid_[0].tick);
}

template <uint8_t Depth>
double OrderBook<Depth>::bestBidPrice() const
{
    if (best_bid_tick_ < 0)
        return 0.0;
    return priceFromTick(static_cast<uint32_t>(best_bid_tick_));
}

template <uint8_t Depth>
double OrderBook<Depth>::bestAskPrice() const
{
    if (best_ask_tick_ < 0)
        return 0.0;
    return priceFromTick(static_cast<uint32_t>(best_ask_tick_));
}
