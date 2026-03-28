#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>
#include "feed/feed_types.hpp"

// ── OrderBook<Depth> ──────────────────────────────────────────────────────────
// Dense book in integer tick space + integer lot counts. No floating point on
// the hot path; tick_width / lower_bound / lot_size are only for API doubles.

template <uint8_t Depth>
class OrderBook {
public:
    struct TOBEntry {
        uint32_t tick     = 0;   // meaningless if qty_lots == 0
        uint64_t qty_lots = 0;   // 0 → empty ladder slot
    };

    /// lot_size: multiply qty_lots for display (e.g. 0.001 if lots are milli-BTC).
    explicit OrderBook(uint16_t const& symbol,
                       double             lower_bound_price,
                       double             upper_bound_price,
                       double             tick_width,
                       double             lot_size = 1.0);

    OrderBook(OrderBook const&)            = delete;
    OrderBook& operator=(OrderBook const&) = delete;
    OrderBook(OrderBook&&)                 = delete;
    OrderBook& operator=(OrderBook&&)      = delete;

    void onSnapshot(FeedMessage const& msg);
    void onUpdate(FeedMessage const& msg);

    // ── Display (double) — computed from tick + lots; never stored in the book ─

    double mid() const;
    double spread() const;
    double bestBidPrice() const;
    double bestAskPrice() const;

    /// Convert book tick index → price (same grid as constructor).
    double priceFromTick(uint32_t tick) const;

    /// Display quantity: lots × lot_size (constructor).
    double qtyFromLots(uint64_t qty_lots) const;

    uint16_t const& symbol() const { return symbol_; }

    TOBEntry const& bid(uint8_t n) const
    {
        assert(n < Depth);
        return bid_[n];
    }
    TOBEntry const& ask(uint8_t n) const
    {
        assert(n < Depth);
        return ask_[n];
    }

private:
    void applyBidTick(uint32_t tick, uint64_t qty_lots);
    void applyAskTick(uint32_t tick, uint64_t qty_lots);

    void refillBidLadder();
    void refillAskLadder();

    bool inRangeTick(uint32_t tick) const { return tick < num_levels_; }

    uint16_t symbol_;

    uint32_t num_levels_;
    double   lower_bound_;
    double   upper_bound_;
    double   tick_width_;
    double   lot_size_;

    int32_t best_bid_tick_;
    int32_t best_ask_tick_;

    std::vector<uint64_t> bid_qty_;
    std::vector<uint64_t> ask_qty_;

    std::array<TOBEntry, Depth> bid_{};
    std::array<TOBEntry, Depth> ask_{};

    int32_t bid_ladder_tail_tick_;
    int32_t ask_ladder_tail_tick_;

    uint8_t bid_ladder_filled_;
    uint8_t ask_ladder_filled_;
};

#include "orderbook_impl.hpp"
