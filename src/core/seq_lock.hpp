#pragma once
#include<atomic>

template<typename T>
class SeqLock {
    static_assert(std::is_trivially_copyable_v<T>, "SeqLock requires trivially copyable type"); 

    public: 
        void write(const T& in) {
            counter_.fetch_add(1, std::memory_order_acq_rel);
            data_ = in;
            counter_.fetch_add(1, std::memory_order_release);
        }

        T read() const {
            uint64_t s1, s2;
            T out;
            do {
                s1 = counter_.load(std::memory_order_acquire);
                out = data_;
                s2 = counter_.load(std::memory_order_acquire);
            } while(s1!=s2 || s1 & 1);

            return out;
        }

    private:
        alignas(64) std::atomic<uint64_t>counter_{0};
        alignas(64) T data_{};
};