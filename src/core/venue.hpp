#pragma once
#include <cstdint>
#include <cstddef>

namespace core {

    enum class Venue: uint8_t {Delta = 0, Bybit = 1, Count};
    inline constexpr size_t N_VENUES = static_cast<size_t>(Venue::Count);
    inline const char* to_string(Venue v) {
        switch(v) {
            case Venue::Delta: return "delta";
            case Venue::Bybit: return "bybit";
            case Venue::Count: return "unknown";
        }
        return "unknown";
    }
}