#include "market_state/market_state.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

MarketState::MarketState(SpscRing<FeedMessage, 4096>* const ring, const ProductTable& products, const ProductGroup& product_group)
    : ring_(ring), products_(products), product_group_(product_group) {

    for (uint8_t instrument_id : product_group_.instrument_ids)
        instrument_valid_[instrument_id] = true;
    // Orderbooks are initialized lazily on the first L2 snapshot for each instrument.
}

void MarketState::run(std::atomic<bool>& running) {
    int64_t last_print_ns = ms_now_ns();

    while (running.load(std::memory_order_relaxed)) {
        auto msg = ring_ -> pop();
        // if (!msg) {
        //     const int64_t now = ms_now_ns();
        //     if (now - last_print_ns >= 1'000'000'000LL) {
        //         // {resolution name, index in ohlc_resolutions}
        //         static constexpr std::pair<const char*, uint8_t> RES[] = {
        //             {"1m",  0},
        //             {"5m",  2},
        //             {"30m", 4},
        //         };
        //         for (uint8_t i = 0; i < product_group_.instrument_ids.size(); ++i) {
        //             uint8_t instrument_id = product_group_.instrument_ids[i];
        //             printBook(orderbooks_[instrument_id], products_[instrument_id].symbol,
        //                       products_[instrument_id].tick_size,
        //                       mark_prices_[instrument_id], spot_prices_[instrument_id]);
        //             for (const auto& [name, idx] : RES)
        //                 printOHLC(products_[instrument_id].symbol, name,
        //                           candle_store_[0][instrument_id][idx],   // trade
        //                           candle_store_[1][instrument_id][idx]);  // mark
        //         }
        //         last_print_ns = now;
        //     }
        //     continue;
        // }

        // const int64_t t_consume = ms_now_ns();
        // stats_.record(msg->t_kernel, msg->t_frame, msg->t_parse, t_consume);

        switch (msg->type) {
            case FeedMessage::Type::L2Feed: {
                uint8_t id = msg->instrument_id;
                if (!instrument_valid_[id]) break;
                if (!orderbook_init_[id]) {
                    if (!msg->l2.isSnapshot) break;  // wait for snapshot to seed bounds
                    double max_price = 0.0;
                    for (uint8_t i = 0; i < msg->l2.ask_count; ++i) {
                        double p = msg->l2.asks[i].price * products_[id].tick_size;
                        if (p > max_price) max_price = p;
                    }
                    for (uint8_t i = 0; i < msg->l2.bid_count; ++i) {
                        double p = msg->l2.bids[i].price * products_[id].tick_size;
                        if (p > max_price) max_price = p;
                    }
                    if (max_price == 0.0) break;
                    orderbooks_[id].init(id, 0.0, max_price * 3.0, products_[id].tick_size);
                    orderbook_init_[id] = true;
                }
                orderbooks_[id].update(msg->l2);
                break;
            }

            case FeedMessage::Type::MarkPrice:
                if (instrument_valid_[msg->instrument_id]) mark_prices_[msg->instrument_id] = msg->mark_price;
                break;

            case FeedMessage::Type::SpotPrice:
                if (instrument_valid_[msg->instrument_id]) spot_prices_[msg->instrument_id] = msg->spot_price;
                break;

            case FeedMessage::Type::OHLC:
                if (instrument_valid_[msg->instrument_id]) candle_store_[(msg->ohlc).is_mark][msg->instrument_id][(msg->ohlc).res_idx].push(msg->ohlc);
                break;

            default:
                break;
        }
    }
}

void MarketState::printOHLC(const char* symbol, const char* res_name,
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
