#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

template<typename T, size_t N>
class SpmcRing {

    static_assert( (N & (N-1)) == 0, "size must be power of 2");
    static constexpr size_t MASK = N-1;

    struct alignas(64) Slot {
        T data;
        std::atomic<uint64_t> seq{0};
    };

    public:
        std::optional<T> try_read(uint64_t& cursor) const {
            uint64_t head_cache = head_.load(std::memory_order_acquire);
            if(cursor > head_cache) return std::nullopt;
            const Slot& slot = slots[cursor & MASK];
            const uint64_t seq1 = slot.seq.load(std::memory_order_acquire);
            if(seq1 ==0 || seq1 != cursor) return std::nullopt;

            T copy = slot.data;
            const uint64_t seq2 = slot.seq.load(std::memory_order_acquire);
            if(seq2 ==0 || seq2 != cursor) return std::nullopt;
            ++cursor;
            return copy;
        }

        void write(const T& data) {
            const uint64_t s = head_.load(std::memory_order_relaxed) + 1;
            Slot& slot = slots[s & MASK];
            slot.seq.store(0, std::memory_order_release);
            slot.data = data;
            slot.seq.store(s, std::memory_order_release);
            head_.store(s, std::memory_order_release);
        }

        uint64_t get_producer_seq() const {return head_.load(std::memory_order_acquire);};

        bool is_lapped(const uint64_t cursor) const {
            return head_.load(std::memory_order_acquire) >= cursor  + N;
        }


    private:
    std::atomic<uint64_t> head_{0};
        Slot slots[N];
};