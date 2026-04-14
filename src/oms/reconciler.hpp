#pragma once
#include "oms/oms_manager.hpp"
#include <chrono>

template<typename T>
concept IsTCPClient = std::derived_from<T, TcpClient>;

template<IsTCPClient RestClient>
class Reconciler {

    explicit Reconciler(const char* host, const char* api_key, const char* api_secret, const ProductTable& products, SpscRing<OMSEvent, 256>*  reconcile_ring): 
    host_(host), api_key_(api_key), api_secret_(api_secret), products_(products), reconcile_ring_(reconcile_ring),client_(host, api_key, api_secret, products) {}

    void run(std::atomic<bool>& running_) {
        while(running_.load(std::memory_order_relaxed)) {
            reconcile();
            std::this_thread::sleep_for(std::chrono::seconds(10));
        }
    }

    void reconcile() {
        orders_state_valid = (order_state_manager_ -> get_orders_state) == ChannelState::Valid;
        positions_state_valid = (order_state_manager_ -> get_positions_state) == ChannelState::Valid;

        if(orders_state_valid) reconcile_orders(OMSEventSource::Reconcile, reconcile_ring_);
        if(positions_state_valid) reconcile_positions(OMSEventSource::Reconcile, reconcile_ring_);
        if(orders_state_valid && positions_state_valid) reconcile_wallet(OMSEventSource::Reconcile, reconcile_ring_);
    }

    void reconcile_orders() {
        auto& oms_events = client_.get_open_orders();

    }


    private:
        const OrderStateManager* order_state_manager_;
        const RestClient client_;
        const ProductTable& products_;
        SpscRing<OMSEvent, 256>* reconcile_ring_;
}