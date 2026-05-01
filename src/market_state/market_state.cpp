#include "market_state/market_state.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>

MarketState::MarketState(SpscRing<FeedMessage, FEED_RING_SIZE>* const ring, 
    SharedState* const shared_state, const ProductTable& products, const ProductGroup& product_group)
    : feed_ring_(ring), shared_state_(shared_state), products_(products), product_group_(product_group) {
        for (uint8_t instrument_id : product_group_.instrument_ids) instrument_valid_[instrument_id] = true;
}

void MarketState::handle_l2_update(const FeedMessage& msg) {
    uint8_t id = msg.instrument_id;
    if (!instrument_valid_[id]) return;
    const Product& prod = products_[id];

    if (!orderbook_init_[id]) {
        if (!msg.l2.isSnapshot) return;  // wait for snapshot to seed bounds
        Tick max_tick{0};

        for (uint8_t i = 0; i < msg.l2.ask_count; ++i) {
            const double price = msg.l2.asks[i].price;
            Tick t = prod.price_to_tick(price);
            if (t > max_tick) max_tick = t;
        }

        for (uint8_t i = 0; i < msg.l2.bid_count; ++i) {
            const double price = msg.l2.bids[i].price;
            Tick t = prod.price_to_tick(price);
            if (t > max_tick) max_tick = t;
        }

        if (max_tick == Tick{0}) return;
        orderbooks_[id].init(id, Tick{0}, max_tick * 3, prod);
        orderbook_init_[id] = true;
    }

    orderbooks_[id].update(msg.l2);
    shared_state_->market_state[msg.instrument_id].write(orderbooks_[id].snapshot());
}

void MarketState::handle_mark_price_data(const FeedMessage& msg) {
    if (instrument_valid_[msg.instrument_id])  {
        uint8_t instrument_id = msg.instrument_id;
        mark_prices_[instrument_id] = msg.mark_price_data;
        MarkPriceSnapshot mark_price_snapshot {
            .last_update_ts_ns = msg.mark_price_data.timestamp,
            .mark_price_tick   = products_[instrument_id].price_to_tick(msg.mark_price_data.price),
        };
        shared_state_->mark_prices[instrument_id].write(mark_price_snapshot);
    }
}

void MarketState::handle_spot_price_data(const FeedMessage& msg) {

    if (instrument_valid_[msg.instrument_id])  {
        uint8_t instrument_id = msg.instrument_id;
        spot_prices_[instrument_id] = msg.spot_price_data;
        SpotPriceSnapshot spot_price_snapshot {
            .spot_price_tick   = products_[instrument_id].price_to_tick(msg.spot_price_data.price),
        };
        shared_state_->spot_prices[instrument_id].write(spot_price_snapshot);
    }
}

void MarketState::handle_ohlc_data(const FeedMessage& msg) {
    if (instrument_valid_[msg.instrument_id])
        candle_store_[msg.ohlc.is_mark][msg.instrument_id][msg.ohlc.res_idx].push(msg.ohlc);
}

void MarketState::handle_trade_data(const FeedMessage& msg) {


    const uint8_t id = msg.instrument_id;
    if (!instrument_valid_[id]) return;

    const Product& prod = products_[id];
    const TradeData& tr = msg.trade_data;

    const bool   is_taker_buy = (tr.buyer_role == TradeData::BuyerRole::Taker);
    const double signed_size  = is_taker_buy ? +tr.size : -tr.size;

    const TradeEntry trade_entry {
        .timestamp     = tr.trade_time,
        .tick          = prod.price_to_tick(tr.price),
        .size          = prod.to_contracts(signed_size),
        .instrument_id = id,
    };
    shared_state_->trade_ring.write(trade_entry);
    printTFI(msg);
}

void MarketState::printTFI(const FeedMessage& msg) {

    const uint8_t id = msg.instrument_id;
    if (!instrument_valid_[id]) return;

    const TradeData& tr = msg.trade_data;

    const bool   is_taker_buy = (tr.buyer_role == TradeData::BuyerRole::Taker);
    const double signed_size  = is_taker_buy ? +tr.size : -tr.size;

    tfi_windows_[id].push(tr.trade_time, signed_size);
    trade_data[id] = tr;

    const char* col   = is_taker_buy ? "\033[32m" : "\033[31m";
    const char* arrow = is_taker_buy ? "▲" : "▼";
    const char* side  = is_taker_buy ? "BUY " : "SELL";
    const int   prec  = products_[id].tick_size < 1.0
        ? static_cast<int>(std::ceil(-std::log10(products_[id].tick_size)))
        : 0;

    std::cout << "[" << format_us_local(tr.trade_time) << "] "
              << std::left << std::setw(8) << products_[id].symbol
              << " " << col << arrow << " TAKER:" << side << "\033[0m"
              << "  " << std::right << std::fixed
              << std::setprecision(2) << std::setw(8) << tr.size
              << " @ " << std::setprecision(prec) << tr.price
              << "   tfi60s=" << col << std::showpos
              << std::setprecision(2) << tfi_windows_[id].net()
              << std::noshowpos << "\033[0m";
    if (mark_prices_[id].price > 0.0) {
        std::cout << "   mark=" << std::setprecision(prec) << mark_prices_[id].price;
    }
    std::cout << "\n";
    std::cout.flush();
}

void MarketState::run(std::atomic<bool>& running) {
    int64_t last_print_ns = ms_now_ns();

    while (running.load(std::memory_order_relaxed)) {
        auto* msg = feed_ring_->pop_begin();
        if (!msg) {
            const int64_t now = ms_now_ns();
            if (now - last_print_ns >= 1'000'000'000LL) {
                // {resolution name, index in ohlc_resolutions}
                // static constexpr std::pair<const char*, uint8_t> RES[] = {
                //     {"1m",  0},
                //     {"5m",  2},
                //     {"30m", 4},
                // };
                for (uint8_t i = 0; i < product_group_.instrument_ids.size(); ++i) {
                    uint8_t instrument_id = product_group_.instrument_ids[i];
                    printBook(orderbooks_[instrument_id], products_[instrument_id],
                              mark_prices_[instrument_id], spot_prices_[instrument_id],
                              tfi_windows_[instrument_id]);
                    // for (const auto& [name, idx] : RES)
                    //     printOHLC(products_[instrument_id].symbol, name,
                    //               candle_store_[0][instrument_id][idx],   // trade
                    //               candle_store_[1][instrument_id][idx]);  // mark
                }
                last_print_ns = now;
            }
            continue;
        }

        // const int64_t t_consume = ms_now_ns();
        // stats_.record(msg->t_kernel, msg->t_frame, msg->t_parse, t_consume);

        if(!msg) continue;

        const auto type_index = static_cast<uint8_t>(msg->type);
        (this->*kHandlers[type_index])(*msg);
        feed_ring_->pop_commit();
    }
}



// void MarketState::printOHLC(const char* symbol, const char* res_name,
//                              const OHLCRing<256>& ohlc_trade_ring,
//                              const OHLCRing<256>& ohlc_mark_ring) {
//     static constexpr int W_TAG   = 4;   // "cur" / "prv"
//     static constexpr int W_LABEL = 7;   // "Open   "
//     static constexpr int W_COL   = 14;
//     static constexpr int W_TOTAL = W_TAG + W_LABEL + W_COL * 2;
//     static constexpr int PRICE_PREC = 1;
//     static constexpr int VOL_PREC   = 2;

//     const bool have_trade = ohlc_trade_ring.size() > 0;
//     const bool have_mark  = ohlc_mark_ring.size()  > 0;
//     if (!have_trade && !have_mark) return;

//     auto pf = [](const OHLCData* d, double OHLCData::* field, int prec) -> std::string {
//         if (!d) return "—";
//         std::ostringstream ss;
//         ss << std::fixed << std::setprecision(prec) << (d->*field);
//         return ss.str();
//     };
//     auto arrow = [](const OHLCData* d) -> const char* {
//         if (!d) return "";
//         return d->close >= d->open ? "\033[32m▲\033[0m" : "\033[31m▼\033[0m";
//     };

//     auto print_section = [&](const char* tag, const OHLCData* t, const OHLCData* m) {
//         auto pc = [&](const OHLCData* d, double OHLCData::* f, int p) -> std::string {
//             return pf(d, f, p);
//         };
//         std::cout
//             << std::left  << std::setw(W_TAG)   << tag
//             << std::left  << std::setw(W_LABEL)  << "Open"
//             << std::right << std::setw(W_COL)    << pc(t, &OHLCData::open,   PRICE_PREC)
//             << std::right << std::setw(W_COL)    << pc(m, &OHLCData::open,   PRICE_PREC) << "\n"
//             << std::left  << std::setw(W_TAG)    << ""
//             << std::left  << std::setw(W_LABEL)  << "High"
//             << std::right << std::setw(W_COL)    << pc(t, &OHLCData::high,   PRICE_PREC)
//             << std::right << std::setw(W_COL)    << pc(m, &OHLCData::high,   PRICE_PREC) << "\n"
//             << std::left  << std::setw(W_TAG)    << ""
//             << std::left  << std::setw(W_LABEL)  << "Low"
//             << std::right << std::setw(W_COL)    << pc(t, &OHLCData::low,    PRICE_PREC)
//             << std::right << std::setw(W_COL)    << pc(m, &OHLCData::low,    PRICE_PREC) << "\n"
//             << std::left  << std::setw(W_TAG)    << ""
//             << std::left  << std::setw(W_LABEL)  << "Close"
//             << std::right << std::setw(W_COL-1)  << pc(t, &OHLCData::close, PRICE_PREC) << arrow(t)
//             << std::right << std::setw(W_COL-1)  << pc(m, &OHLCData::close, PRICE_PREC) << arrow(m) << "\n"
//             << std::left  << std::setw(W_TAG)    << ""
//             << std::left  << std::setw(W_LABEL)  << "Volume"
//             << std::right << std::setw(W_COL)    << pc(t, &OHLCData::volume, VOL_PREC)
//             << std::right << std::setw(W_COL)    << "—" << "\n";
//     };

//     const OHLCData* cur_t  = have_trade             ? &ohlc_trade_ring.back()                 : nullptr;
//     const OHLCData* cur_m  = have_mark               ? &ohlc_mark_ring.back()                  : nullptr;
//     const OHLCData* prev_t = ohlc_trade_ring.size() >= 2  ? &ohlc_trade_ring[ohlc_trade_ring.size() - 2] : nullptr;
//     const OHLCData* prev_m = ohlc_mark_ring.size()  >= 2  ? &ohlc_mark_ring[ohlc_mark_ring.size()   - 2] : nullptr;

//     std::cout << "\n\033[1m" << symbol << " " << res_name << "\033[0m\n"
//               << std::string(W_TOTAL, '-') << "\n"
//               << std::left  << std::setw(W_TAG + W_LABEL) << ""
//               << std::right << std::setw(W_COL) << "TRADE"
//               << std::right << std::setw(W_COL) << "MARK" << "\n"
//               << std::string(W_TOTAL, '-') << "\n";

//     print_section("cur", cur_t, cur_m);
//     if (prev_t || prev_m) {
//         std::cout << std::string(W_TOTAL, '.') << "\n";
//         print_section("prv", prev_t, prev_m);
//     }
//     std::cout << std::string(W_TOTAL, '-') << "\n";
//     std::cout.flush();
// }
