#pragma once
#include "core/spsc_ring.hpp"
#include "core/orderbook/orderbook.hpp"
#include "feed/sessions/types.hpp"
#include "feed/models/product.hpp"
#include "market_state/types.hpp"
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

public:
    explicit MarketState(SpscRing<FeedMessage, 4096>& ring, const ProductTable& products)
        : ring_(ring), products_(products) {
        for (uint8_t i = 0; i < products_.count; ++i) {
            const Product& p = products_[i];
            orderbooks_[i].init(p.internal_id,
                                p.lower_bound_price,
                                p.upper_bound_price,
                                p.tick_size);
        }
    }

    void run(std::atomic<bool>& running) {
        int64_t last_print_ns = ms_now_ns();

        while (running.load(std::memory_order_relaxed)) {
            auto msg = ring_.pop();
            if (!msg) {
                const int64_t now = ms_now_ns();
                if (now - last_print_ns >= 1'000'000'000LL) {
                    // {resolution name, index in ohlc_resolutions}
                    static constexpr std::pair<const char*, uint8_t> RES[] = {
                        {"1m",  0},
                        {"5m",  2},
                        {"30m", 4},
                    };
                    for (uint8_t i = 0; i < products_.count; ++i) {
                        printBook(orderbooks_[i], products_[i].symbol,
                                  products_[i].tick_size,
                                  mark_prices_[i], spot_prices_[i]);
                        for (const auto& [name, idx] : RES)
                            printOHLC(products_[i].symbol, name,
                                      candle_store_[0][i][idx],   // trade
                                      candle_store_[1][i][idx]);  // mark
                    }
                    last_print_ns = now;
                }
                continue;
            }

            const int64_t t_consume = ms_now_ns();
            stats_.record(msg->t_kernel, msg->t_frame, msg->t_parse, t_consume);

            switch (msg->type) {
                case FeedMessage::Type::L2Feed:
                    if (msg->instrument_id < products_.count)
                        orderbooks_[msg->instrument_id].update(msg->l2);
                    break;

                case FeedMessage::Type::MarkPrice:
                    if (msg->instrument_id < products_.count)
                        mark_prices_[msg->instrument_id] = msg->mark_price;
                    break;

                case FeedMessage::Type::SpotPrice:
                    if (msg->instrument_id < products_.count)
                        spot_prices_[msg->instrument_id] = msg->spot_price;
                    break;

                case FeedMessage::Type::OHLC:
                    if (msg->instrument_id < products_.count)
                        candle_store_[(msg->ohlc).is_mark][msg->instrument_id][(msg->ohlc).res_idx].push(msg->ohlc);
                    break;

                default:
                    break;
            }
        }
    }

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
                          const OHLCRing<256>& mark_ring) {
        static constexpr int W_TAG   = 4;   // "cur" / "prv"
        static constexpr int W_LABEL = 7;   // "Open   "
        static constexpr int W_COL   = 14;
        static constexpr int W_TOTAL = W_TAG + W_LABEL + W_COL * 2;
        static constexpr int PRICE_PREC = 1;
        static constexpr int VOL_PREC   = 2;

        const bool have_trade = trade_ring.size() > 0;
        const bool have_mark  = mark_ring.size()  > 0;
        if (!have_trade && !have_mark) return;

        auto pf = [](const OHLCData* d, double OHLCData::* field, int prec) -> std::string {
            if (!d) return "—";
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(prec) << (d->*field);
            return ss.str();
        };
        auto arrow = [](const OHLCData* d) -> const char* {
            if (!d) return "";
            return d->close >= d->open ? "\033[32m▲\033[0m" : "\033[31m▼\033[0m";
        };

        auto print_section = [&](const char* tag, const OHLCData* t, const OHLCData* m) {
            auto pc = [&](const OHLCData* d, double OHLCData::* f, int p) -> std::string {
                return pf(d, f, p);
            };
            std::cout
                << std::left  << std::setw(W_TAG)   << tag
                << std::left  << std::setw(W_LABEL)  << "Open"
                << std::right << std::setw(W_COL)    << pc(t, &OHLCData::open,   PRICE_PREC)
                << std::right << std::setw(W_COL)    << pc(m, &OHLCData::open,   PRICE_PREC) << "\n"
                << std::left  << std::setw(W_TAG)    << ""
                << std::left  << std::setw(W_LABEL)  << "High"
                << std::right << std::setw(W_COL)    << pc(t, &OHLCData::high,   PRICE_PREC)
                << std::right << std::setw(W_COL)    << pc(m, &OHLCData::high,   PRICE_PREC) << "\n"
                << std::left  << std::setw(W_TAG)    << ""
                << std::left  << std::setw(W_LABEL)  << "Low"
                << std::right << std::setw(W_COL)    << pc(t, &OHLCData::low,    PRICE_PREC)
                << std::right << std::setw(W_COL)    << pc(m, &OHLCData::low,    PRICE_PREC) << "\n"
                << std::left  << std::setw(W_TAG)    << ""
                << std::left  << std::setw(W_LABEL)  << "Close"
                << std::right << std::setw(W_COL-1)  << pc(t, &OHLCData::close, PRICE_PREC) << arrow(t)
                << std::right << std::setw(W_COL-1)  << pc(m, &OHLCData::close, PRICE_PREC) << arrow(m) << "\n"
                << std::left  << std::setw(W_TAG)    << ""
                << std::left  << std::setw(W_LABEL)  << "Volume"
                << std::right << std::setw(W_COL)    << pc(t, &OHLCData::volume, VOL_PREC)
                << std::right << std::setw(W_COL)    << "—" << "\n";
        };

        const OHLCData* cur_t  = have_trade             ? &trade_ring.back()                 : nullptr;
        const OHLCData* cur_m  = have_mark               ? &mark_ring.back()                  : nullptr;
        const OHLCData* prev_t = trade_ring.size() >= 2  ? &trade_ring[trade_ring.size() - 2] : nullptr;
        const OHLCData* prev_m = mark_ring.size()  >= 2  ? &mark_ring[mark_ring.size()   - 2] : nullptr;

        std::cout << "\n\033[1m" << symbol << " " << res_name << "\033[0m\n"
                  << std::string(W_TOTAL, '-') << "\n"
                  << std::left  << std::setw(W_TAG + W_LABEL) << ""
                  << std::right << std::setw(W_COL) << "TRADE"
                  << std::right << std::setw(W_COL) << "MARK" << "\n"
                  << std::string(W_TOTAL, '-') << "\n";

        print_section("cur", cur_t, cur_m);
        if (prev_t || prev_m) {
            std::cout << std::string(W_TOTAL, '.') << "\n";
            print_section("prv", prev_t, prev_m);
        }
        std::cout << std::string(W_TOTAL, '-') << "\n";
        std::cout.flush();
    }

    SpscRing<FeedMessage, 4096>& ring_;
    const ProductTable&           products_;
    OrderBook<BOOK_DEPTH>         orderbooks_[MAX_INSTRUMENTS];
    MarkPriceData                 mark_prices_[MAX_INSTRUMENTS]{};
    LatencyStats                  stats_;
    SpotPriceData                 spot_prices_[MAX_INSTRUMENTS]{};

    using ResolutionRings =     std::array<OHLCRing<256>, ohlc_resolutions.size()>;
    using InstrumentCandles =   std::array<ResolutionRings, MAX_INSTRUMENTS>;
    using OHLCStore =           std::array<InstrumentCandles, 2>;

    OHLCStore candle_store_{};

};
