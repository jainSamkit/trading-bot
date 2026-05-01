#pragma once
#include "core/spsc_ring.hpp"
#include "config/config.hpp"
#include "delta_exchange/oms_ws_client.hpp"
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/sessions/types.hpp"
#include "delta_exchange/models/product.hpp"
#include "oms/oms_manager.hpp"
#include "ipc/shared_state.hpp"
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

template<typename U>
concept IsRestClient = std::derived_from<U, TcpClient>;

template<IsOMSWebSocketClient WSClient, IsRestClient RestClient = DeltaRestClient>
class OmsProcess {
    static constexpr size_t OMS_RING_SIZE = cfg::OMS_RING_SIZE;
    
public:
OmsProcess(const ProductTable& products, const OMSConfig& cfg)
        : products_(products), cfg_(cfg)
    {
        oms_rest_ring_         = std::make_unique<SpscRing<OMSEvent, OMS_RING_SIZE>>();
        oms_ws_ring_           = std::make_unique<SpscRing<OMSEvent, OMS_RING_SIZE>>();
        oms_reconcile_ring_    = std::make_unique<SpscRing<OMSEvent, OMS_RING_SIZE>>();
        ws_client_             = std::make_unique<WSClient>(
            cfg_.host.c_str(), cfg_.port, cfg_.path.c_str(),
            cfg_.api_key.c_str(), cfg_.api_secret.c_str(),
            products_, oms_ws_ring_.get());

            oms_manager_  = std::make_unique<OrderStateManager>(oms_ws_ring_.get(), oms_rest_ring_.get(), oms_reconcile_ring_.get(), products_);
    }

    void start() {
        instance_for_shutdown_signal_ = this;
        struct sigaction sa{};
        sa.sa_handler = sig_interrupt;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGTERM, &sa, nullptr);

        running_.store(true, std::memory_order_relaxed);

        oms_manager_thread_   = std::thread(&OrderStateManager::run, oms_manager_.get(), std::ref(running_));
        oms_ws_client_thread_ = std::thread(&WSClient::start, ws_client_.get());

        oms_manager_thread_.join();
        oms_ws_client_thread_.join();
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
    static void sig_interrupt(int sig) {
        (void)sig;
        if (shutdown_signal_seen_ != 0) return;
        shutdown_signal_seen_ = 1;
        if (instance_for_shutdown_signal_)
            instance_for_shutdown_signal_->stop();
    }

    inline static volatile sig_atomic_t shutdown_signal_seen_ = 0;
    inline static OmsProcess*           instance_for_shutdown_signal_ = nullptr;

    const ProductTable& products_;
    const OMSConfig     cfg_;
    std::atomic<bool>   running_{false};
    std::atomic<bool>   stopped_{false};

    std::unique_ptr<SpscRing<OMSEvent, OMS_RING_SIZE>>  oms_ws_ring_;
    std::unique_ptr<SpscRing<OMSEvent, OMS_RING_SIZE>>  oms_rest_ring_;
    std::unique_ptr<SpscRing<OMSEvent, OMS_RING_SIZE>>  oms_reconcile_ring_;
    std::unique_ptr<WSClient>                           ws_client_;
    std::unique_ptr<OrderStateManager>                  oms_manager_;
    std::thread                                         oms_manager_thread_;
    std::thread                                         oms_ws_client_thread_;
};
