#pragma once
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "core/snapshots.hpp"
#include "config/config.hpp"

// ── OrderBook ────────────────────────────────────────────────────────────────

class OrderBook {
    static constexpr size_t Depth = cfg::SNAPSHOT_DEPTH;

public:
    OrderBook() = default;

    OrderBook(OrderBook const&)            = delete;
    OrderBook& operator=(OrderBook const&) = delete;
    OrderBook(OrderBook&&)                 = delete;
    OrderBook& operator=(OrderBook&&)      = delete;

    void init(uint8_t product_id, Tick min_tick,Tick max_tick, const Product& product);

    void onSnapshot(L2Update const& msg);
    void onUpdate(L2Update const& msg);
    void update(L2Update const& msg);

    // ── Display (double) — computed from tick + contrats; never stored in the book ─

    double mid() const;
    Tick bestBidPlusAsk() const;
    Tick spread() const;
    Tick bestBidTick() const;
    Tick bestAskTick() const;
    const MarketSnapshot& snapshot();

private:
    void applyBidTick(Tick tick, Contracts size);
    void applyAskTick(Tick tick, Contracts size);

    void refillBidLadder();
    void refillAskLadder();

    bool inRangeTick(Tick tick) const { return tick <= max_tick_ && tick >= min_tick_; }
    
    uint8_t                                     product_id_ = 0;
    const Product*                              product_ = nullptr;

    size_t                                      num_ticks_;
    Tick                                        min_tick_;
    Tick                                        max_tick_;

    Tick                                        best_bid_tick_;
    Tick                                        best_ask_tick_;

    std::unique_ptr<Contracts[]>                bid_qty_per_tick_;
    std::unique_ptr<Contracts[]>                ask_qty_per_tick_;

    MarketSnapshot                              market_snapshot_{};

    Tick                                        bid_ladder_tail_tick_;
    Tick                                        ask_ladder_tail_tick_;

    uint8_t                                     bid_ladder_filled_;
    uint8_t                                     ask_ladder_filled_;
};

#include "orderbook_impl.hpp"
