#pragma once
#include <cstdint>
#include <cstdlib>
#include "core/tick.hpp"
#include "core/counter.hpp"
#include "core/snapshots.hpp"
#include "delta_exchange/models/product.hpp"
#include "oms/execution_intent.hpp"
#include "strategy/strategy_config.hpp"

struct FixedSpreadQuoter {

    bool ask_quote(const MarketSnapshot& snapshot, const Product& product, CreateOrderIntent& intent) {
        Tick desired_ask_tick = Tick{static_cast<int32_t>(mid_tick_)} + strategy::Config::half_spread_ticks;

        if((abs(desired_ask_tick - last_ask_tick) >= strategy::Config::requote_threshold_ticks) || !has_ask_quote) {
            last_ask_tick = desired_ask_tick;
            has_ask_quote = true;
            intent = CreateOrderIntent{
                .side = ExecutionOrderSide::Sell, .type = ExecutionOrderType::Limit,
                .price = product.tick_to_price(desired_ask_tick),
                .tick_size = product.tick_size, .exchange_product_id = product.exchange_id,
                .size = strategy::Config::max_contract_size, .client_order_id = core::counter(),
                .reduce_only = false, .post_only = true, .is_stop_order = false
            };
            return true;
        }

        return false;
    }


    bool bid_quote(const MarketSnapshot& snapshot, const Product& product, CreateOrderIntent& intent) {
        Tick desired_bid_tick = Tick{static_cast<int32_t>(mid_tick_)} - strategy::Config::half_spread_ticks;

        if((abs(desired_bid_tick - last_bid_tick) >= strategy::Config::requote_threshold_ticks) || !has_bid_quote) {
            last_bid_tick = desired_bid_tick;
            has_bid_quote = true;
            intent = CreateOrderIntent{
                .side = ExecutionOrderSide::Buy, .type = ExecutionOrderType::Limit,
                .price = product.tick_to_price(desired_bid_tick),
                .tick_size = product.tick_size, .exchange_product_id = product.exchange_id,
                .size = strategy::Config::max_contract_size, .client_order_id = core::counter(),
                .reduce_only = false, .post_only = true, .is_stop_order = false
            };
            return true;
        }

        return false;
    }

    void set_mid_tick(const MarketSnapshot& snapshot) {
        mid_tick_ = Tick{(to_int(snapshot.best_bid) + to_int(snapshot.best_ask)) / 2};
    }

    private:
        bool has_ask_quote{false};
        bool has_bid_quote{false};
        Tick last_ask_tick{};
        Tick last_bid_tick{};
        Tick mid_tick_{};
};