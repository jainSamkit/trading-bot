#pragma once
#include "models/product.hpp"
#include "core/seq_lock.hpp"
#include "ipc/snapshots.hpp"
#include <cstdint>

static constexpr uint8_t MAX_INSTRUMENTS = ProductTable::MAX_INSTRUMENTS;
struct SharedState {
    SeqLock<MarketSnapshot>             book[MAX_INSTRUMENTS];
    SeqLock<VolSnapshot>                 vol[MAX_INSTRUMENTS];
    SeqLock<MarkSnapshot>               mark[MAX_INSTRUMENTS];
    SeqLock<SpotSnapshot>               spot[MAX_INSTRUMENTS];
    SeqLock<PositionSnapshot>            pos[MAX_INSTRUMENTS];
};