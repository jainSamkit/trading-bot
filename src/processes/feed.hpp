#pragma once
#include "core/spsc_ring.hpp"
#include "core/spmc_ring.hpp"
#include "config/config.hpp"
#include "core/snapshots.hpp"
#include "delta_exchange/ws_client.hpp"
#include "delta_exchange/sessions/types.hpp"
#include "ipc/shared_state.hpp"
#include "market_state/market_state.hpp"
#include "delta_exchange/models/product.hpp"
#include "latency/registry.hpp"
#include "latency/influx_writer.hpp"
#include <atomic>
#include <csignal>
#include <concepts>
#include <memory>
#include <string>
#include <thread>


struct FeedConfig {
    std::string host;
    int         port;
    std::string path;
};


template<typename T>
concept IsWebSocketClient = std::derived_from<T, WebSocketClient<T>>;

template<IsWebSocketClient Client>
class FeedProcess {
    static constexpr size_t FEED_RING_SIZE = cfg::FEED_RING_SIZE;

public:
    FeedProcess(const ProductTable& products,
                const ProductGroup& product_group,
                SharedState* const  shared_state,
                const FeedConfig&   cfg, latency::InfluxWriter::Config influx_cfg, core::Venue venue)
        : shared_state_(shared_state), products_(products),
          product_group_(product_group), cfg_(cfg), influx_cfg_(influx_cfg), venue_(venue) {}

    void start() {

        instance_for_shutdown_signal_ = this;
        struct sigaction sa{};
        sa.sa_handler = sig_interrupt;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        influx_writer_ = std::make_unique<latency::InfluxWriter>(influx_cfg_);

        latency::Registry::init(venue_);
        latency::Registry::start_periodic_push(influx_writer_.get(), latency::PUSH_INTERVAL_SECONDS);

        feedRing_ = std::make_unique<SpscRing<FeedMessage, FEED_RING_SIZE>>();

        client_   = std::make_unique<Client>(cfg_.host.c_str(), cfg_.port,
                                             cfg_.path.c_str(), products_,
                                             feedRing_.get(), product_group_);
        market_   = std::make_unique<MarketState>(feedRing_.get(), shared_state_, products_, product_group_, venue_);
        market_thread_ = std::thread(&MarketState::run, market_.get(), std::ref(running_));

        // Blocks in the epoll reactor until the signal handler calls shutdown() (eventfd wake).
        client_->start();

        // Back on a normal (non-signal) thread: safe to do the heavy teardown here.
        stop();
    }

    void stop() {
        if (stopped_.exchange(true)) return;
        running_.store(false, std::memory_order_relaxed);
        if (client_) client_->shutdown();
        if (market_thread_.joinable()) market_thread_.join();
        latency::Registry::stop_periodic_push();
    }

    ~FeedProcess() { stop(); }

private:

    // Async-signal-safe only: flip running_ (stops the MarketState thread) and wake the
    // epoll reactor via shutdown() — which is just write() to an eventfd, on the AS-safe
    // list. The heavy teardown (join, stop_periodic_push) happens in stop(), called from
    // start() once client_->start() returns.
    static void sig_interrupt(int sig) {
        (void)sig;
        // Terminal Ctrl+C often delivers SIGINT to every process in the foreground
        // group, and the parent may also send SIGTERM — handler can run more than once.
        if (shutdown_signal_seen_ != 0)
            return;
        shutdown_signal_seen_ = 1;
        if (instance_for_shutdown_signal_) {
            instance_for_shutdown_signal_->running_.store(false, std::memory_order_relaxed);
            if (instance_for_shutdown_signal_->client_)
                instance_for_shutdown_signal_->client_->shutdown();
        }
    }

    inline static volatile sig_atomic_t                         shutdown_signal_seen_ = 0;
    inline static FeedProcess*                                  instance_for_shutdown_signal_ = nullptr;
    SharedState* const                                          shared_state_;
    const ProductTable&                                         products_;
    const ProductGroup&                                         product_group_;
    const FeedConfig&                                           cfg_;
    latency::InfluxWriter::Config                               influx_cfg_;
    core::Venue                                                 venue_;
    std::unique_ptr<latency::InfluxWriter>                      influx_writer_;

    std::atomic<bool>                                           running_{true};
    std::atomic<bool>                                           stopped_{false};

    std::unique_ptr<SpscRing<FeedMessage, FEED_RING_SIZE>>      feedRing_;
    std::unique_ptr<Client>                                     client_;
    std::unique_ptr<MarketState>                                market_;
    std::thread                                                 market_thread_;
};
