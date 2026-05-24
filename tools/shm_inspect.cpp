// shm_inspect — attaches to /trading_bot_state and prints live snapshots.
// Build: cmake --build build --target shm_inspect
// Run:   ./build/shm_inspect      (while the trading bot is running)
//
// This is a *separate process*, not forked from main. It uses ShmOwner::attach()
// to map the existing SHM region into its own address space.
//
// NOTE: the trade ring is intentionally NOT observed here. SpmcRing now uses a
// shared-tail CAS design — any reader (including this tool) would advance the
// tail and steal trades from the real consumer (MarketState). Snapshots
// (market_state / mark_prices / spot_prices) use a separate SeqLock-style read
// that's side-effect-free, so polling them from this tool is safe.

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

    // Per-instrument last-seen state so we only print on change.
    uint64_t last_market_seq [SharedState::MAX_INSTRUMENTS]{};
    uint64_t last_mark_ts    [SharedState::MAX_INSTRUMENTS]{};
    int32_t  last_spot_tick  [SharedState::MAX_INSTRUMENTS]{};

    auto next_log = std::chrono::steady_clock::now();

    while (running.load(std::memory_order_relaxed)) {
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
