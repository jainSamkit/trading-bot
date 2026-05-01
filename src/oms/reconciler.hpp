#pragma once
#include "oms/oms_manager.hpp"
#include <chrono>

template<typename T>
concept IsTCPClient = std::derived_from<T, TcpClient>;

template<IsTCPClient RestClient>
class Reconciler {
    static constexpr size_t OMS_RING_SIZE = cfg::OMS_RING_SIZE;

    explicit Reconciler(const char* host, const char* api_key, const char* api_secret, const ProductTable& products, SpscRing<OMSEvent, OMS_RING_SIZE>*  reconcile_ring): 
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
        
        if(orders_state_valid) rest_client_.reconcile_open_orders(reconcile_ring_);
        if(positions_state_valid) rest_client_.reconcile_positions(reconcile_ring_);
        if(orders_state_valid && positions_state_valid) rest_client_.reconcile_wallet(reconcile_ring_);
    }


    private:
        const OrderStateManager* order_state_manager_;
        const RestClient rest_client_;
        const ProductTable& products_;
        SpscRing<OMSEvent, OMS_RING_SIZE>* reconcile_ring_;
}