#pragma once

#include <array>
#include <atomic>
#include <stdexcept>
#include <transport/http_client.hpp"

template<typename ClientT, size_t N = 3>
requires std::derived_from(ClientT, TcpClient) 

class ConnectionPool {

    template<typename... Args>
    explicit ConnectionPool(Args&& args) {
        for (auto& slot: slots_) {
            slot.client_ = std::make_unique<ClientT>(std::forward<Args>(args)...);
        }
    }

    void connect_all() {
        for(auto& slot: slots_) {
            slot.healthy = slot.client_->connect();
        }
    }

    ClientT& get_next() {
        const size_t start = counter_.fetch_add(1, std::memory_order_relaxed) % N;
        for(size_t i = 0; i<N ; ++i) {
            auto& slot = slots_[(start + i) % N ];
            if(slot.healthy) return *(slot.client);
        }

        slots_[0].healthy = slots_[0].client_->connect();
        if(slots_[0].healthy) return *slots_[0].client_;

        throw std::runtime_error("connection pool exhausted");
    }

    void mark_unhealthy(ClientT& client) {
        for (auto& slot : slots_)
            if (slot.client.get() == &client)
                slot.healthy = false;
    }

    private:
        struct Slot {
            std::unique_ptr<ClientT>client_;
            bool healthy = false;
        };

        std::array<Slot, N> slots_;
        std::atomic<size_t> counter_{0};
}
