#pragma once
#include "core/spsc_ring.hpp"
#include "config/config.hpp"
#include "delta_exchange/oms_ws_client.hpp"
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "delta_exchange/rest_client.hpp"
#include "latency/registry.hpp"
#include "latency/influx_writer.hpp"
#include "oms/oms_manager.hpp"
#include "oms/execution_manager.hpp"
#include "ipc/shared_state.hpp"
#include <atomic>
#include <csignal>
#include <concepts>
#include <memory>
#include <string>
#include <thread>


struct OMSConfig {
    std::string     ws_host;
    std::string     rest_host;
    int             port;
    std::string     path;
    std::string     api_key;
    std::string     api_secret;
};

template<typename T>
concept IsOMSWebSocketClient = std::derived_from<T, WebSocketClient<T>>;

template<typename U>
concept IsRestClient = std::derived_from<U, TcpClient>;

template<IsOMSWebSocketClient WSClient, IsRestClient RestClient = DeltaRestClient>
class OmsProcess {
    static constexpr size_t OMS_RING_SIZE = cfg::OMS_RING_SIZE;

public:
OmsProcess(const ProductTable& products, const ProductGroup& product_group, latency::InfluxWriter::Config influx_cfg, const OMSConfig& cfg, SharedState* const shared_state, ExecMode exec_mode, core::Venue venue)
        : products_(products), product_group_(product_group), influx_cfg_(influx_cfg), cfg_(cfg), shared_state_(shared_state), exec_mode_(exec_mode), venue_(venue)
    {
        oms_rest_ring_         = std::make_unique<SpscRing<OMSEvent, OMS_RING_SIZE>>();
        oms_ws_ring_           = std::make_unique<SpscRing<OMSEvent, OMS_RING_SIZE>>();
        oms_reconcile_ring_    = std::make_unique<SpscRing<OMSEvent, OMS_RING_SIZE>>();
        ws_client_             = std::make_unique<WSClient>(
            cfg_.ws_host.c_str(), cfg_.port, cfg_.path.c_str(),
            cfg_.api_key.c_str(), cfg_.api_secret.c_str(),
            products_, oms_ws_ring_.get());

        oms_manager_  = std::make_unique<OrderStateManager>(oms_ws_ring_.get(), oms_rest_ring_.get(), oms_reconcile_ring_.get(), products_);
        execution_manager_ = std::make_unique<ExecutionManager<RestClient>>(cfg_.rest_host.c_str(), cfg_.api_key.c_str(),cfg_.api_secret.c_str(), products_, shared_state_, oms_rest_ring_.get(), exec_mode_, venue_);
    }

    void start() {
        instance_for_shutdown_signal_ = this;
        struct sigaction sa{};
        sa.sa_handler = sig_interrupt;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        // ORDER MATTERS: spawn the InfluxWriter push thread BEFORE pinning +
        // elevating this thread to SCHED_FIFO. See feed.hpp for the full
        // rationale — short version: ExecutionManager::run never blocks, so a
        // push thread that inherits this thread's eventual SCHED_FIFO + core
        // affinity is starved at birth.
        influx_writer_ = std::make_unique<latency::InfluxWriter>(influx_cfg_);

        latency::Registry::init(venue_);
        latency::Registry::start_periodic_push(influx_writer_.get(), latency::PUSH_INTERVAL_SECONDS);

        // Now safe to pin + elevate. This thread runs ExecutionManager::run
        // busy-spin until shutdown.
        core::cpu::pin_thread(cfg::cpu::OMS_EXEC_CORE, "oms/execution_manager");
        core::cpu::set_realtime(cfg::cpu::RT_PRIORITY, "oms/execution_manager");

        running_.store(true, std::memory_order_relaxed);

        // oms_manager_thread_   = std::thread(&OrderStateManager::run, oms_manager_.get(), std::ref(running_));
        // oms_ws_client_thread_ = std::thread(&WSClient::start, ws_client_.get());

        // Runs on this thread until the signal handler flips running_ to false.
        execution_manager_->run(running_);

        // Back on a normal (non-signal) thread: safe to do the heavy teardown here.
        stop();
    }

    void stop() {
        if (stopped_.exchange(true)) return;
        running_.store(false, std::memory_order_relaxed);
        ws_client_->shutdown();
        if (oms_manager_thread_.joinable())   oms_manager_thread_.join();
        if (oms_ws_client_thread_.joinable()) oms_ws_client_thread_.join();
    }

    OrderStateManager& oms_manager() { return *oms_manager_; }

    ~OmsProcess() { stop(); }

private:
    // Async-signal-safe: only flip the running_ flag. The real teardown (ws shutdown,
    // thread joins) happens in stop(), called from start() once run() returns.
    static void sig_interrupt(int sig) {
        (void)sig;
        if (shutdown_signal_seen_ != 0) return;
        shutdown_signal_seen_ = 1;
        if (instance_for_shutdown_signal_)
            instance_for_shutdown_signal_->running_.store(false, std::memory_order_relaxed);
    }

    inline static volatile sig_atomic_t                 shutdown_signal_seen_ = 0;
    inline static OmsProcess*                           instance_for_shutdown_signal_ = nullptr;

    const ProductTable&                                 products_;
    const ProductGroup&                                 product_group_;
    latency::InfluxWriter::Config                       influx_cfg_;
    const OMSConfig                                     cfg_;
    SharedState* const                                  shared_state_;
    ExecMode                                            exec_mode_;
    core::Venue                                         venue_;
    std::unique_ptr<latency::InfluxWriter>              influx_writer_;
    std::atomic<bool>                                   running_{false};
    std::atomic<bool>                                   stopped_{false};

    std::unique_ptr<SpscRing<OMSEvent, OMS_RING_SIZE>>  oms_ws_ring_;
    std::unique_ptr<SpscRing<OMSEvent, OMS_RING_SIZE>>  oms_rest_ring_;
    std::unique_ptr<SpscRing<OMSEvent, OMS_RING_SIZE>>  oms_reconcile_ring_;
    std::unique_ptr<WSClient>                           ws_client_;
    std::unique_ptr<OrderStateManager>                  oms_manager_;
    std::unique_ptr<ExecutionManager<RestClient>>       execution_manager_;
    std::thread                                         oms_manager_thread_;
    std::thread                                         oms_ws_client_thread_;
};
