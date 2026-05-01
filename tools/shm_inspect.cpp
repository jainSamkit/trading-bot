// shm_inspect — attaches to /trading_bot_state and prints live snapshots + trades.
// Build: cmake --build build --target shm_inspect
// Run:   ./build/shm_inspect      (while the trading bot is running)
//
// This is a *separate process*, not forked from main. It uses ShmOwner::attach()
// to map the existing SHM region into its own address space.

#include "ipc/shm.hpp"
#include "ipc/shared_state.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

static std::atomic<bool> running{true};
static void on_sig(int) { running.store(false, std::memory_order_relaxed); }

static const char* SHM_NAME = "/trading_bot_state";

int main(int argc, char** argv) {
    const char* name = argc > 1 ? argv[1] : SHM_NAME;
    int poll_ms = argc > 2 ? std::atoi(argv[2]) : 200;

    std::signal(SIGINT,  on_sig);
    std::signal(SIGTERM, on_sig);

    auto shm   = ShmOwner<SharedState>::attach(name);
    SharedState* state = shm.get();
    if (!state) {
        std::cerr << "[shm_inspect] failed to attach to " << name << "\n";
        return 1;
    }

    std::cout << "[shm_inspect] attached " << name
              << " ptr=" << state
              << " size=" << sizeof(SharedState) << " bytes"
              << " poll=" << poll_ms << "ms\n"
              << "----------------------------------------\n";

    // Independent producer-relative cursor for the trade ring.
    // Start from "current head" so we don't dump historical trades on startup.
    uint64_t trade_cursor = state->trade_ring.get_producer_seq() + 1;

    // Per-instrument last-seen state so we only print on change.
    uint64_t last_market_seq [SharedState::MAX_INSTRUMENTS]{};
    uint64_t last_mark_ts    [SharedState::MAX_INSTRUMENTS]{};
    int32_t  last_spot_tick  [SharedState::MAX_INSTRUMENTS]{};

    auto next_log = std::chrono::steady_clock::now();

    while (running.load(std::memory_order_relaxed)) {
        // ── Trade ring: drain everything new ─────────────────────────────────
        TradeEntry te;
        size_t drained = 0;
        while (true) {
            auto v = state->trade_ring.try_read(trade_cursor);
            if (!v) break;
            te = *v;
            ++drained;
            std::cout << "[trade] ts=" << te.timestamp
                      << " inst=" << static_cast<int>(te.instrument_id)
                      << " tick=" << to_int(te.tick)
                      << " size=" << to_int(te.size)
                      << "\n";
        }
        if (state->trade_ring.is_lapped(trade_cursor)) {
            const uint64_t head = state->trade_ring.get_producer_seq();
            std::cout << "[trade] LAPPED — fast-forwarding cursor "
                      << trade_cursor << " -> " << head + 1 << "\n";
            trade_cursor = head + 1;
        }

        // ── Snapshots: print only on seq/ts change ───────────────────────────
        const auto now = std::chrono::steady_clock::now();
        const bool periodic_log = (now >= next_log);

        for (uint8_t id = 0; id < SharedState::MAX_INSTRUMENTS; ++id) {
            const MarketSnapshot     ms = state->market_state[id].read();
            const MarkPriceSnapshot  mk = state->mark_prices [id].read();
            const SpotPriceSnapshot  sp = state->spot_prices [id].read();

            const bool market_changed = ms.last_seq          != last_market_seq[id] && ms.last_seq != 0;
            const bool mark_changed   = mk.last_update_ts_ns != last_mark_ts   [id] && mk.last_update_ts_ns != 0;
            const bool spot_changed   = to_int(sp.spot_price_tick) != last_spot_tick[id] && to_int(sp.spot_price_tick) != 0;

            if (!market_changed && !mark_changed && !spot_changed && !periodic_log) continue;
            if (ms.last_seq == 0 && mk.last_update_ts_ns == 0 && to_int(sp.spot_price_tick) == 0) continue;

            std::cout << "[id=" << static_cast<int>(id) << "] "
                      << "best_bid="  << to_int(ms.best_bid)
                      << " best_ask=" << to_int(ms.best_ask)
                      << " spread="   << to_int(ms.spread)
                      << " seq="      << ms.last_seq
                      << " mark_tk="  << to_int(mk.mark_price_tick)
                      << " spot_tk="  << to_int(sp.spot_price_tick)
                      << "\n";

            last_market_seq[id] = ms.last_seq;
            last_mark_ts   [id] = mk.last_update_ts_ns;
            last_spot_tick [id] = to_int(sp.spot_price_tick);
        }

        if (periodic_log) {
            next_log = now + std::chrono::seconds(2);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }

    std::cout << "[shm_inspect] exiting\n";
    return 0;
}
