#pragma once
#include <cstdint>
#include "core/tick.hpp"

namespace strategy {

    struct Config {
        inline static constexpr Tick max_spread_ticks               = Tick{10};
        inline static constexpr Tick half_spread_ticks              = Tick{10};
        inline static constexpr Tick requote_threshold_ticks        = Tick{10};
        inline static constexpr uint64_t stale_ns                   = 1ULL * 1000'000'000;
        inline static constexpr uint64_t max_contract_size          = 1;
    };
}