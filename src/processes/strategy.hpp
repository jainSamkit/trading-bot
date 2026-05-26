#pragma once
#include <atomic>
#include <csignal>
#include <cstddef>
#include <memory>
#include "core/venue.hpp"
#include "core/cpu_relax.hpp"
#include "latency/clock.hpp"
#include "ipc/shared_state.hpp"
#include "strategy/quoter.hpp"
#include "strategy/risk_overlay.hpp"
#include "strategy/strategy_config.hpp"

class Strategy {
    public:
        explicit Strategy(const ProductTable& products, const ProductGroup& product_group, SharedState* const shared_state, core::Venue venue): 
            products_(products), product_group_(product_group), shared_state_(shared_state), venue_(venue) {}

        void run() {
            while(running_.load(std::memory_order_relaxed)) {
                bool no_quote = true;

                for(size_t i = 0; i < product_group_.instrument_ids.size();i++) {

                    uint8_t instrument_id = product_group_.instrument_ids[i];
                    const Product& product = products_[instrument_id];
                    uint64_t last_seq = shared_state_->market_state[static_cast<size_t>(venue_)][instrument_id].peek_seq_no();

                    if(last_seq == last_seen_seq_[instrument_id]) continue;
                    last_seen_seq_[instrument_id] = last_seq;
                    
                    const MarketSnapshot& snapshot = shared_state_->market_state[static_cast<size_t>(venue_)][instrument_id].read();
                    if(strategy::RiskOverlay::check(snapshot, latency::now_ns()) != strategy::RiskOverlay::RiskState::OK) continue;

                    no_quote = false;
                    quoter->set_mid_tick(snapshot);
                    
                    ExecutionIntent* intent_slot = shared_state_->execution_intent_ring[static_cast<size_t>(venue_)].push_begin();
                    intent_slot->t_origin_ns = snapshot.t_origin_ns;
                    intent_slot->action = ExecutionAction::CreateOrder;

                    CreateOrderIntent& execution_intent = intent_slot->create_order_intent;
                    bool has_ask_intent = quoter->ask_quote(snapshot, product, execution_intent);
                    if(has_ask_intent) {
                        shared_state_->execution_intent_ring[static_cast<size_t>(venue_)].push_commit();
                    }

                    intent_slot = shared_state_->execution_intent_ring[static_cast<size_t>(venue_)].push_begin();
                    intent_slot->t_origin_ns = snapshot.t_origin_ns;
                    intent_slot->action = ExecutionAction::CreateOrder;
                    CreateOrderIntent& bid_execution_intent = intent_slot->create_order_intent;

                    bool has_bid_intent = quoter->bid_quote(snapshot, products_[instrument_id], bid_execution_intent);
                    if(has_bid_intent) {
                        shared_state_->execution_intent_ring[static_cast<size_t>(venue_)].push_commit();
                    }
                }

                if(no_quote) core::cpu_relax();
            }
        }

        void start() {
            instance_for_shutdown_signal_ = this;
            struct sigaction sa{};
            sa.sa_handler = sig_interrupt;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGINT, &sa, nullptr);
            sigaction(SIGTERM, &sa, nullptr);

            quoter = std::make_unique<FixedSpreadQuoter>();
            running_.store(true, std::memory_order_relaxed);
            // Runs on this thread until the signal handler flips running_ to false.
            run();
        }
    
    private:

        static void sig_interrupt(int sig) {
            (void)sig;
            if (shutdown_signal_seen_ != 0) return;
            shutdown_signal_seen_ = 1;
            if (instance_for_shutdown_signal_)
                instance_for_shutdown_signal_->running_.store(false, std::memory_order_relaxed);
        }

        inline static volatile sig_atomic_t                 shutdown_signal_seen_ = 0;
        inline static Strategy*                             instance_for_shutdown_signal_ = nullptr;
        const ProductTable&                                 products_;
        const ProductGroup&                                 product_group_;
        SharedState* const                                  shared_state_;
        const core::Venue                                   venue_;
        uint64_t                                            last_seen_seq_[cfg::MAX_INSTRUMENTS]{};
        std::unique_ptr<FixedSpreadQuoter>                  quoter;
        std::atomic<bool>                                   running_{false};
};