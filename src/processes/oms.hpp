#pragma once
#include "core/spsc_ring.hpp"
#include "delta_exchange/oms_ws_client.hpp"
#include "delta_exchange/sessions/types.hpp"
#include "ipc/shared_state.hpp"
#include "delta_exchange/models/product.hpp"
#include <atomic>
#include <csignal>
#include <concepts>
#include <memory>
#include <string>
#include <thread>


struct OMSConfig {
    std::string host;
    int         port;
    std::string path;
    std::string api_key;
    std::string api_secret;
};

template<typename T>
concept IsOMSWebSocketClient = std::derived_from<T, WebSocketClient<T>>;

template<IsOMSWebSocketClient Client>
class OmsProcess {

public:
    OmsProcess(const ProductTable& products, const OMSConfig& cfg): products_(products), cfg_(cfg) {}

    void start() {
        instance_for_shutdown_signal_ = this;
        struct sigaction sa{};
        sa.sa_handler = sig_interrupt;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        feedRing_ = std::make_unique<SpscRing<FeedMessage, 4096>>();
        client_   = std::make_unique<Client>(cfg_.host.c_str(), cfg_.port,
                                                cfg_.path.c_str(), cfg_.api_key.c_str(), 
                                                cfg_.api_secret.c_str(), products_,
                                                feedRing_.get());

        // market_   = std::make_unique<MarketState>(feedRing_.get(), products_, product_group_);
        // market_thread_ = std::thread(&MarketState::run, market_.get(), std::ref(running_));
        client_->start();
    }

    void stop() {
        if (stopped_.exchange(true)) return;
        // running_.store(false, std::memory_order_relaxed);
        if (client_) client_->shutdown();
        // if (market_thread_.joinable()) market_thread_.join();
    }

    ~OmsProcess() { stop(); }

private:

    static void sig_interrupt(int sig) {
        (void)sig;
        // Terminal Ctrl+C often delivers SIGINT to every process in the foreground
        // group, and the parent may also send SIGTERM — handler can run more than once.
        if (shutdown_signal_seen_ != 0)
            return;
        shutdown_signal_seen_ = 1;
        if (instance_for_shutdown_signal_)
            instance_for_shutdown_signal_->stop();
    }

    inline static volatile sig_atomic_t             shutdown_signal_seen_ = 0;
    inline static OmsProcess*                       instance_for_shutdown_signal_ = nullptr;
    const ProductTable&                             products_;
    const OMSConfig&                                cfg_;
    std::atomic<bool>                               running_{true};
    std::atomic<bool>                               stopped_{false};

    std::unique_ptr<SpscRing<FeedMessage, 4096>>    feedRing_;
    std::unique_ptr<Client>                         client_;
};
