#pragma once
#include "core/spsc_ring.hpp"
#include "core/spmc_ring.hpp"
#include "core/snapshots.hpp"
#include "config/config.hpp"
#include "core/orderbook/orderbook.hpp"
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "ipc/shared_state.hpp"
#include "market_state/latency_stats.hpp"
#include "market_state/ohlc_ring.hpp"
#include "latency/registry.hpp"      // brings histogram + tag_set transitively
#include "latency/span.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <time.h>

static inline int64_t ms_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

// Format a microsecond timestamp as local time HH:MM:SS.mmm
static inline std::string format_us_local(uint64_t ts_us) {
    const time_t sec = static_cast<time_t>(ts_us / 1'000'000ULL);
    const int    ms  = static_cast<int>((ts_us % 1'000'000ULL) / 1000);
    struct tm tm_local{};
    localtime_r(&sec, &tm_local);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tm_local.tm_hour, tm_local.tm_min, tm_local.tm_sec, ms);
    return buf;
}

// Rolling 60s window of signed trade size (taker buy = +size, taker sell = −size).
// O(1) amortized push: drop expired entries off the front before appending.
struct TFIWindow {
    static constexpr size_t   CAPACITY  = 4096;
    static constexpr uint64_t WINDOW_US = 60'000'000ULL;  // 60s in microseconds

    struct Entry { uint64_t ts_us; double signed_size; };
    Entry    entries[CAPACITY]{};
    size_t   head = 0;            // oldest
    size_t   tail = 0;            // next-write
    double   sum  = 0.0;

    void push(uint64_t ts_us, double signed_size) {
        while (head != tail && entries[head].ts_us + WINDOW_US < ts_us) {
            sum -= entries[head].signed_size;
            head = (head + 1) % CAPACITY;
        }
        entries[tail] = { ts_us, signed_size };
        sum += signed_size;
        tail = (tail + 1) % CAPACITY;
        if (tail == head) {                       // overflow: drop oldest
            sum -= entries[head].signed_size;
            head = (head + 1) % CAPACITY;
        }
    }
    double net() const      { return sum; }
    bool   has_data() const { return head != tail; }
};

class MarketState {
    static constexpr size_t MAX_INSTRUMENTS = cfg::MAX_INSTRUMENTS;
    static constexpr size_t Depth = cfg::SNAPSHOT_DEPTH;
    static constexpr size_t FEED_RING_SIZE = cfg::FEED_RING_SIZE;

    using Span = latency::Span;
    using Histogram = latency::Histogram;
public:
    explicit MarketState(SpscRing<FeedMessage, FEED_RING_SIZE>* const feed_ring, 
        SharedState* const shared_state, const ProductTable& products, const ProductGroup& product_group);

    void run(std::atomic<bool>& running);

private:
    static void printBook(OrderBook& book, const Product& product,
                          const MarkPriceData& mark, const SpotPriceData& spot,
                          const TFIWindow& tfi) {
        static constexpr int W_PRICE = 12;
        static constexpr int W_SIZE  = 14;
        static constexpr int W_TOTAL = W_PRICE + W_SIZE + 2;
        const double tick_size = product.tick_size;
        const int prec = tick_size < 1.0
            ? static_cast<int>(std::ceil(-std::log10(tick_size)))
            : 0;
        const MarketSnapshot& snap = book.snapshot();

        std::cout << "\n\033[1m" << std::setw(W_PRICE + W_SIZE)
                  << product.symbol << "\033[0m\n"
                  << std::string(W_TOTAL, '=') << "\n"
                  << std::right
                  << std::setw(W_PRICE) << "Ask Price"
                  << std::setw(W_SIZE)  << "Size"     << "\n"
                  << std::string(W_TOTAL, '-')         << "\n";

        int top_ask = -1;
        for (int n = Depth - 1; n >= 0; --n)
            if (snap.asks[static_cast<size_t>(n)].size != Contracts{0}) { top_ask = n; break; }

        for (int n = top_ask; n >= 0; --n) {
            const auto& lvl = snap.asks[static_cast<size_t>(n)];
            if (lvl.size == Contracts{0}) continue;
            std::cout << std::fixed << std::setprecision(prec)
                      << std::setw(W_PRICE) << product.tick_to_price(lvl.tick)
                      << std::setw(W_SIZE)  << to_int(lvl.size) << "\n";
        }

        std::cout << std::string(W_TOTAL, '-') << "\n";
        const double mid        = book.mid();
        const Tick   spread_t   = book.spread();
        const double spread_usd = to_int(spread_t) > 0 ? to_int(spread_t) * tick_size : 0.0;
        if (mid > 0.0)
            std::cout << "  mid " << std::fixed << std::setprecision(prec) << mid
                      << "   spread " << std::setprecision(prec) << spread_usd << "\n";
        if (mark.price > 0.0)
            std::cout << "  mark " << std::fixed << std::setprecision(prec) << mark.price << "\n";
        if (spot.price > 0.0)
            std::cout << "  spot " << std::fixed << std::setprecision(prec) << spot.price << "\n";
        if (tfi.has_data()) {
            const double net = tfi.net();
            const char*  col = net > 0 ? "\033[32m" : (net < 0 ? "\033[31m" : "");
            std::cout << "  tfi(60s) " << col << std::fixed << std::setprecision(2)
                      << std::showpos << net << std::noshowpos << "\033[0m\n";
        }
        std::cout << std::string(W_TOTAL, '-') << "\n"
                  << std::right
                  << std::setw(W_PRICE) << "Bid Price"
                  << std::setw(W_SIZE)  << "Size"     << "\n";

        for (size_t n = 0; n < Depth; ++n) {
            const auto& lvl = snap.bids[n];
            if (lvl.size == Contracts{0}) break;
            std::cout << std::fixed << std::setprecision(prec)
                      << std::setw(W_PRICE) << product.tick_to_price(lvl.tick)
                      << std::setw(W_SIZE)  << to_int(lvl.size) << "\n";
        }

        std::cout << std::string(W_TOTAL, '=') << "\n";
        std::cout.flush();
    }

    // static void printOHLC(const char* symbol, const char* res_name,
    //                       const OHLCRing<256>& ohlc_trade_ring,
    //                       const OHLCRing<256>& ohlc_mark_ring);

    void handle_l2_update      (const FeedMessage& msg);
    void handle_mark_price_data(const FeedMessage& msg);
    void handle_spot_price_data(const FeedMessage& msg);
    void handle_ohlc_data      (const FeedMessage& msg);
    void handle_trade_data     (const FeedMessage& msg);
    void printTFI              (const FeedMessage& msg);

    using Handler = void (MarketState::*)(const FeedMessage&);

    // Order MUST match FeedMessage::Type enum:
    //   L2Feed=0, MarkPrice=1, Trade=2, OHLC=3, SpotPrice=4
    static constexpr Handler kHandlers[] = {
        &MarketState::handle_l2_update,          // [0] L2Feed
        &MarketState::handle_mark_price_data,    // [1] MarkPrice
        &MarketState::handle_trade_data,         // [2] Trade
        &MarketState::handle_ohlc_data,          // [3] OHLC
        &MarketState::handle_spot_price_data,    // [4] SpotPrice
    };


    
    SpscRing<FeedMessage, FEED_RING_SIZE>* const                feed_ring_;
    SharedState* const                                          shared_state_;
    const ProductTable&                                         products_;
    const ProductGroup&                                         product_group_;
    OrderBook                                                   orderbooks_[MAX_INSTRUMENTS]{};
    bool                                                        orderbook_init_[MAX_INSTRUMENTS]{};
    MarkPriceData                                               mark_prices_[MAX_INSTRUMENTS]{};
    LatencyStats                                                stats_;
    SpotPriceData                                               spot_prices_[MAX_INSTRUMENTS]{};
    TradeData                                                   trade_data[MAX_INSTRUMENTS]{};
    TFIWindow                                                   tfi_windows_[MAX_INSTRUMENTS]{};
    bool                                                        instrument_valid_[MAX_INSTRUMENTS]{};

    Histogram*                                                  l2_handler_hist_ = nullptr;
    Histogram*                                                  mark_handler_hist_ = nullptr;
    Histogram*                                                  trade_handler_hist_ = nullptr;
    Histogram*                                                  ohlc_handler_hist_ = nullptr;
    Histogram*                                                  spot_handler_hist_ = nullptr;
    Histogram*                                                  ringpush_hist_ = nullptr;
    Histogram*                                                  ringwait_hist_ = nullptr;

    Histogram*                                                  l2_shm_hist_ = nullptr;
    Histogram*                                                  mark_shm_hist_ = nullptr;
    Histogram*                                                  spot_shm_hist_ = nullptr;
    Histogram*                                                  trade_shm_hist_ = nullptr;
    Histogram*                                                  ohlc_shm_hist_ = nullptr;

    using ResolutionRings =     std::array<OHLCRing<256>, ohlc_resolutions.size()>;
    using InstrumentCandles =   std::array<ResolutionRings, MAX_INSTRUMENTS>;
    using OHLCStore =           std::array<InstrumentCandles, 2>;

    OHLCStore candle_store_{};

};
