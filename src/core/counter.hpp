#pragma once
#include <atomic>
#include <cstdint>

namespace core {
    inline std::atomic<uint64_t> counter_{0};
    inline uint64_t counter() {
        return counter_.fetch_add(1, std::memory_order_relaxed);
    }
};