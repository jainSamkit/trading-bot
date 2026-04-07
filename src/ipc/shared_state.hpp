#pragma once
#include "core/seq_lock.hpp"
#include "ipc/snapshots.hpp"
#include <cstdint>

static constexpr uint8_t     MAX_INSTRUMENTS = 64;
static constexpr const char* SHM_NAME        = "/trading_bot_state";

struct SharedState {
    SeqLock<MarketSnapshot>   book[MAX_INSTRUMENTS];
    SeqLock<VolSnapshot>      vol[MAX_INSTRUMENTS];
    SeqLock<MarkSnapshot>     mark[MAX_INSTRUMENTS];
    SeqLock<SpotSnapshot>     spot[MAX_INSTRUMENTS];
    SeqLock<PositionSnapshot> pos[MAX_INSTRUMENTS];
};
