#pragma once
#include <atomic>
#include <cstdio>
#include "core/spsc_ring.hpp"
#include "core/venue.hpp"
#include "core/cpu_relax.hpp"
#include "config/config.hpp"
#include "latency/clock.hpp"
#include "ipc/shared_state.hpp"
#include "latency/span.hpp"
#include "latency/registry.hpp"
#include "oms/execution_intent.hpp"
#include "oms/oms_manager.hpp"
#include "transport/connection_pool.hpp"

enum class ExecMode : uint8_t { Live, Shadow };

// ─── ExecutionManager (TODO: wire up when ConnectionPool is implemented) ──────

template<typename T>
concept IsTCPClient = std::derived_from<T, TcpClient>;

template<IsTCPClient RestClient>
class ExecutionManager {
    using Span = latency::Span;
    using Histogram = latency::Histogram;
    using Registry = latency::Registry;

    using Handler = void (RestClient::*)(const ExecutionIntent&, SpscRing<OMSEvent, cfg::OMS_RING_SIZE>*);

    static constexpr Handler kHandlers[] = {
        &RestClient::create_order,
        &RestClient::edit_order,
        &RestClient::cancel_order,
        &RestClient::cancel_all_orders,
        &RestClient::close_all_positions,
    };

    //these is rest host

    void drain_intake_ring() {

        bool any_work = false;
        while (auto* intent = shared_state_->execution_intent_ring[static_cast<uint64_t>(venue_)].pop_begin()) {
            any_work = true;
            {
                Span s(wireout_hist_);
                RestClient& client = conn_pool_.get_next();
                auto handler = kHandlers[static_cast<uint8_t>(intent->action)];
                if(exec_mode_ == ExecMode::Live) {
                    (client.*handler)(*intent, oms_rest_ring_);
                } else {
                    log_shadow_intent(*intent);
                }
            }

            uint64_t tick_to_trade_ns = latency::now_ns() - intent->t_origin_ns;
            tick_to_trade_hist_->record_ns(tick_to_trade_ns);
            shared_state_->execution_intent_ring[static_cast<uint64_t>(venue_)].pop_commit();
        }

        if(!any_work) core::cpu_relax();
    }

    // Shadow mode: don't hit the wire, just record what we *would* have sent.
    void log_shadow_intent(const ExecutionIntent& intent) {
        static constexpr const char* kActionName[] = {
            "CreateOrder", "EditOrder", "CancelOrder", "CancelAllOrders", "CloseAllPositions"
        };
        std::fprintf(stderr, "[shadow] action=%s\n",
                     kActionName[static_cast<uint8_t>(intent.action)]);
    }

public:

    explicit ExecutionManager(const char* host, const char* api_key, const char* api_secret,
        const ProductTable& products, SharedState* const shared_state, SpscRing<OMSEvent, cfg::OMS_RING_SIZE>* oms_rest_ring, ExecMode exec_mode, core::Venue venue)
        : host_(host), api_key_(api_key), api_secret_(api_secret), products_(products),
        conn_pool_(host, api_key, api_secret, products), shared_state_(shared_state), oms_rest_ring_(oms_rest_ring), exec_mode_(exec_mode), venue_(venue)
    {
        conn_pool_.connect_all();
        wireout_hist_ = Registry::get_or_create({.event_type = latency::TagSet::EventType::WireOut});
        tick_to_trade_hist_ = Registry::get_or_create({.event_type = latency::TagSet::EventType::TickToTrade});
    }

    void run(std::atomic<bool>& running_) {
        while (running_.load(std::memory_order_relaxed)) {
            drain_intake_ring();
        }
    }

private:
    const char*                                                                     host_;
    const char*                                                                     api_key_;
    const char*                                                                     api_secret_;
    const ProductTable&                                                             products_;
    ConnectionPool<RestClient, cfg::execution_manager::CONN_POOL_SIZE>              conn_pool_;
    SharedState* const                                                              shared_state_;
    SpscRing<OMSEvent, cfg::OMS_RING_SIZE>*                                         oms_rest_ring_;
    ExecMode                                                                        exec_mode_;
    core::Venue                                                                     venue_;
    Histogram*                                                                      wireout_hist_ = nullptr;
    Histogram*                                                                      tick_to_trade_hist_ = nullptr;
};