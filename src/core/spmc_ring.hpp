#pragma once
#include <atomic>
#include <cstddef>
#include <array>
#include <cstring>
#include <cstdint>
#include <optional>

template<typename T, size_t N>
class SpmcRing {

    static_assert( (N & (N-1)) == 0, "size must be power of 2");

    struct alignas(64) Slot {
        T data;
        std::atomic<uint64_t> seq_no;
    };

    public:
        SpmcRing() {
            head_ = tail_ = 0;
            for(size_t i =0;i<N;i++) buffer_[i].seq_no = i;
        }

        std::optional<T> try_read() {
            uint64_t slot = tail_.load(std::memory_order_relaxed);

            while(true) {
                uint64_t seq_no = buffer_[slot & (N-1)].seq_no.load(std::memory_order_acquire);
                int64_t diff = static_cast<int64_t>(seq_no) - static_cast<int64_t>(slot+1);
                if(diff == 0) {
                    if(tail_.compare_exchange_weak(slot, slot+1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                        T data;
                        std::memcpy(&data, &buffer_[slot & (N-1)].data, sizeof(T));
                        buffer_[slot & (N-1)].seq_no.store(slot+N, std::memory_order_release);
                        return data;
                    }
                } else if(diff < 0) {
                    return std::nullopt;
                } else {
                    slot = tail_.load(std::memory_order_relaxed);
                }
            }
        }

        bool write(const T& data) {
            uint64_t h = head_.load(std::memory_order_relaxed);
            Slot& s = buffer_[h & (N-1)];
            if(s.seq_no.load(std::memory_order_acquire) != h) return false;
            std::memcpy(&s.data, &data, sizeof(T));
            s.seq_no.store(h+1, std::memory_order_release);
            head_.store(h + 1, std::memory_order_relaxed);
            return true;
        }

    private:
        alignas(64) std::atomic<uint64_t> head_;
        alignas(64) std::atomic<uint64_t> tail_;
        std::array<Slot, N> buffer_;
};