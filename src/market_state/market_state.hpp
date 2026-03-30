#pragma once
#include "core/spsc_ring.hpp"
#include "core/orderbook/orderbook.hpp"
#include "feed/sessions/types.hpp"
#include "feed/models/product.hpp"
#include "market_state/types.hpp"
#include <atomic>
#include <iomanip>
#include <iostream>

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
        while (running.load(std::memory_order_relaxed)) {
            auto msg = ring_.pop();
            if (!msg) continue;

            switch (msg->type) {
                case FeedMessage::Type::L2Feed:
                    if (msg->instrument_id < products_.count) {
                        orderbooks_[msg->instrument_id].update(msg->l2);
                        printBook(orderbooks_[msg->instrument_id],
                                  products_[msg->instrument_id].symbol);
                    }
                    break;

                case FeedMessage::Type::MarkPrice:
                    mark_prices_[msg->instrument_id] = msg->mark_price;
                    break;

                case FeedMessage::Type::SpotPrice:
                    spot_prices_[msg->instrument_id] = msg->spot_price;
                    break;

                default:
                    break;
            }
        }
    }

private:
    template<uint8_t Depth>
    static void printBook(const OrderBook<Depth>& book, const char* symbol) {
        static constexpr int W_PRICE = 12;
        static constexpr int W_SIZE  = 14;
        static constexpr int W_TOTAL = W_PRICE + W_SIZE + 2;

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
            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(W_PRICE) << book.priceFromTick(lvl.tick)
                      << std::setw(W_SIZE)  << lvl.size << "\n";
        }

        std::cout << std::string(W_TOTAL, '-') << "\n";
        const double mid    = book.mid();
        const double spread = book.spread();
        if (mid > 0.0)
            std::cout << "  mid " << std::fixed << std::setprecision(2) << mid
                      << "   spread " << spread << "\n";
        std::cout << std::string(W_TOTAL, '-') << "\n"
                  << std::right
                  << std::setw(W_PRICE) << "Bid Price"
                  << std::setw(W_SIZE)  << "Size"     << "\n";

        // bids: print best → worst so best bid is closest to the mid line
        for (uint8_t n = 0; n < Depth; ++n) {
            const auto& lvl = book.bid(n);
            if (lvl.size == 0) break;
            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(W_PRICE) << book.priceFromTick(lvl.tick)
                      << std::setw(W_SIZE)  << lvl.size << "\n";
        }

        std::cout << std::string(W_TOTAL, '=') << "\n";
        std::cout.flush();
    }

    SpscRing<FeedMessage, 4096>& ring_;
    const ProductTable&           products_;
    OrderBook<BOOK_DEPTH>         orderbooks_[MAX_INSTRUMENTS];
    double                        mark_prices_[MAX_INSTRUMENTS]{};
    double                        spot_prices_[MAX_INSTRUMENTS]{};
};
