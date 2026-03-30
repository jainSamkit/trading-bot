#pragma once
#include<atomic>
#include<optional>

struct Ring {
    alignas(64) std::atomic<uint64_t>head_;
    char pad0[56];
    alignas(64) std::atomic<uint64_t>tail_;
    char pad1[56];
};

template<typename T, size_t N>
class SpscRing {
    static_assert ((N & (N-1)) == 0, "N must be power of 2");

    public: 
        std::optional<T> pop() {
            uint64_t tail = ring_.tail_.load(std::memory_order_relaxed);
            uint64_t head = ring_.head_.load(std::memory_order_acquire);

            if(head == tail) return std::nullopt;
            T val = buffer_[tail & (N-1)];
            ring_.tail_.fetch_add(1, std::memory_order_release);
            return val;
        }

        T* push_begin() {
            uint64_t head_cache = ring_.head_.load(std::memory_order_relaxed);
            while(head_cache -  ring_.tail_.load(std::memory_order_acquire) == N) {};
            return static_cast<T*>(&buffer_[head_cache & (N-1)]);
        }

        void push_commit() {
            ring_.head_.fetch_add(1, std::memory_order_release);
        }

        void push(const T& val) {
            uint64_t head = ring_.head_.load(std::memory_order_relaxed);
            uint64_t tail = ring_.tail_.load(std::memory_order_acquire);
            while(head - tail == N) {
                tail = ring_.tail_.load(std::memory_order_acquire);
            }

            buffer_[head & (N-1)] = val;
            ring_.head_.fetch_add(1, std::memory_order_release);
            return;
        }
    private:
        T buffer_[N]= {};
        Ring ring_ {};
};

