#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

template<typename T>
class SeqLock {
    static_assert(std::is_trivially_copyable_v<T>, "SeqLock requires trivially copyable type");

    public:
        void write(const T& in) {
            uint64_t seq = counter_.load(std::memory_order_relaxed);
            counter_.store(seq + 1, std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_release);
            std::memcpy(&data_, &in, sizeof(T));
            std::atomic_thread_fence(std::memory_order_release);
            counter_.store(seq + 2, std::memory_order_relaxed);
        }

        T read() const {
            T out;
            while (true) {
                const uint64_t s1 = counter_.load(std::memory_order_acquire);
                if (s1 & 1) continue;                       // writer in progress; retry
                std::memcpy(&out, &data_, sizeof(T));
                std::atomic_thread_fence(std::memory_order_acquire);
                const uint64_t s2 = counter_.load(std::memory_order_relaxed);
                if (s1 == s2) return out;                   // consistent snapshot
            }
        }

    private:
        alignas(64) std::atomic<uint64_t> counter_{0};
        alignas(64) T                     data_{};
};
