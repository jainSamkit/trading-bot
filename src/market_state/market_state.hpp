#pragma once
#include "core/spsc_ring.hpp"
#include "core/orderbook/orderbook.hpp"
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "market_state/latency_stats.hpp"
#include "market_state/ohlc_ring.hpp"
#include <atomic>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <time.h>

static inline int64_t ms_now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
}

class MarketState {
    static constexpr uint8_t MAX_INSTRUMENTS = ProductTable::MAX_INSTRUMENTS;
    static constexpr size_t BOOK_DEPTH = 10;

public:
    explicit MarketState(SpscRing<FeedMessage, 4096>* const ring, const ProductTable& products, const ProductGroup& product_group);

    void run(std::atomic<bool>& running);

private:
    template<uint8_t Depth>
    static void printBook(const OrderBook<Depth>& book, const char* symbol,
                          double tick_size, const MarkPriceData& mark,
                          const SpotPriceData& spot) {
        static constexpr int W_PRICE = 12;
        static constexpr int W_SIZE  = 14;
        static constexpr int W_TOTAL = W_PRICE + W_SIZE + 2;
        const int prec = tick_size < 1.0
            ? static_cast<int>(std::ceil(-std::log10(tick_size)))
            : 0;

        std::cout << "\n\033[1m" << std::setw(W_PRICE + W_SIZE)
                  << symbol << "\033[0m\n"
                  << std::string(W_TOTAL, '=') << "\n"
                  << std::right
                  << std::setw(W_PRICE) << "Ask Price"
                  << std::setw(W_SIZE)  << "Size"     << "\n"
                  << std::string(W_TOTAL, '-')         << "\n";

        // asks: print worst → best so best ask is closest to the mid line
        int top_ask = -1;
        for (int n = Depth - 1; n >= 0; --n)
            if (book.ask(static_cast<uint8_t>(n)).size > 0) { top_ask = n; break; }

        for (int n = top_ask; n >= 0; --n) {
            const auto& lvl = book.ask(static_cast<uint8_t>(n));
            if (lvl.size == 0) continue;
            std::cout << std::fixed << std::setprecision(prec)
                      << std::setw(W_PRICE) << book.priceFromTick(lvl.tick)
                      << std::setw(W_SIZE)  << lvl.size << "\n";
        }

        std::cout << std::string(W_TOTAL, '-') << "\n";
        const double mid    = book.mid();
        const double spread = book.spread();
        if (mid > 0.0)
            std::cout << "  mid " << std::fixed << std::setprecision(prec) << mid
                      << "   spread " << spread << "\n";
        if (mark.price > 0.0) {
            std::cout << "  mark " << std::fixed << std::setprecision(prec) << mark.price;
            if (mark.price_band.lower_limit > 0.0)
                std::cout << "   band [" << mark.price_band.lower_limit
                          << ", " << mark.price_band.upper_limit << "]";
            std::cout << "\n";
        }
        if (spot.price > 0.0)
            std::cout << "  spot " << std::fixed << std::setprecision(prec) << spot.price << "\n";
        std::cout << std::string(W_TOTAL, '-') << "\n"
                  << std::right
                  << std::setw(W_PRICE) << "Bid Price"
                  << std::setw(W_SIZE)  << "Size"     << "\n";

        // bids: print best → worst so best bid is closest to the mid line
        for (uint8_t n = 0; n < Depth; ++n) {
            const auto& lvl = book.bid(n);
            if (lvl.size == 0) break;
            std::cout << std::fixed << std::setprecision(prec)
                      << std::setw(W_PRICE) << book.priceFromTick(lvl.tick)
                      << std::setw(W_SIZE)  << lvl.size << "\n";
        }

        std::cout << std::string(W_TOTAL, '=') << "\n";
        std::cout.flush();
    }

    static void printOHLC(const char* symbol, const char* res_name,
                          const OHLCRing<256>& trade_ring,
                          const OHLCRing<256>& mark_ring);

    SpscRing<FeedMessage, 4096>* const      ring_;
    const ProductTable&                     products_;
    const ProductGroup&                     product_group_;
    OrderBook<BOOK_DEPTH>                   orderbooks_[MAX_INSTRUMENTS]{};
    bool                                    orderbook_init_[MAX_INSTRUMENTS]{};
    MarkPriceData                           mark_prices_[MAX_INSTRUMENTS]{};
    LatencyStats                            stats_;
    SpotPriceData                           spot_prices_[MAX_INSTRUMENTS]{};
    bool                                    instrument_valid_[MAX_INSTRUMENTS]{};

    using ResolutionRings =     std::array<OHLCRing<256>, ohlc_resolutions.size()>;
    using InstrumentCandles =   std::array<ResolutionRings, MAX_INSTRUMENTS>;
    using OHLCStore =           std::array<InstrumentCandles, 2>;

    OHLCStore candle_store_{};

};
