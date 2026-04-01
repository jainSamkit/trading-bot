#pragma once
#include "feed/sessions/types.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>

template<uint32_t N>
class OHLCRing {
    static_assert(N > 0, "N should be greater than 0");
public:

    void push(const OHLCData& data) {
        bool should_update_prev = (count > 0 && back().start_time == data.start_time);
        if(should_update_prev) {
            buffer_[(head - 1 + N) % N] = data;
        } else {
            buffer_[head % N] = data;
            ++head;
            count = std::min(count + 1, N);
        }
    }

    const OHLCData& operator[](uint32_t i) const {
        assert(i < size() && "operator[] out of range");
        return buffer_[(head - count + i + N) % N];
    }

    const OHLCData& back() const {
        assert(size() > 0 && "back() called on empty ring");
        return buffer_[(head - 1 + N) % N];
    }

    uint32_t size() const { return count; }

private:
    uint64_t head = 0;
    uint32_t count = 0;
    std::array<OHLCData, N> buffer_;
};
