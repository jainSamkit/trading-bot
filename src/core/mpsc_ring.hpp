#pragma once
#include <atomic>
#include <cstddef>
#include <array>
#include <cstring>
#include <cstdint>
#include <optional>

template<typename T, size_t N>
class MpscRing {
    static_assert( (N & (N-1) ) == 0, "Ring size should be power of 2");
    public:

    struct alignas(64) Slot {
        T data;
        std::atomic<uint64_t> seq_no;
    };

    MpscRing() {
        head = tail = 0;
        for(int i=0;i<N;i++) {
            buffer_[i].seq_no = i;
        }
    }

    // diff < 0 (another producer has filled the buffer), diff = 0(reader has read), diff > 0 (reader has not arrived yet return)

    bool push(const T& data) {
        uint64_t slot = head.load(std::memory_order_relaxed);
        while(true) {
            uint64_t seq_no_ = buffer_[slot & (N-1)].seq_no.load(std::memory_order_acquire);
            int64_t diff = static_cast<int64_t>(slot) - static_cast<int64_t>(seq_no_);
            if(diff == 0) {
                if(head.compare_exchange_weak(slot, slot + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
                    std::memcpy(&buffer_[slot & (N-1)].data, &data, sizeof(T));
                    buffer_[slot & (N-1)].seq_no.store(slot+1, std::memory_order_release);
                    return true;
                } 
            } else if(diff < 0) {
                //another producer has written on the slot 
                slot = head.load(std::memory_order_relaxed);
            } else {
                //the reader is behind
                return false;
            }
        }
    }

    std::optional<T> pop() {
        uint64_t slot = tail.load(std::memory_order_relaxed);
        Slot& s = buffer_[slot& (N-1)];
        if(s.seq_no.load(std::memory_order_acquire) != slot + 1) return std::nullopt;
        T data;
        std::memcpy(&data, &buffer_[slot & (N-1)].data, sizeof(T));
        buffer_[slot & (N-1)].seq_no.store(slot + N, std::memory_order_release);
        tail_.store(slot + 1, std::memory_order_relaxed);
        return data; 
    }


    private:
        alignas(64) std::atomic<uint64_t> head;
        alignas(64) std::atomic<uint64_t> tail;
        alignas(64) std::array<Slot, N> buffer_{};
}