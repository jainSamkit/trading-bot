#include "oms/oms_manager.hpp"
#include "oms/oms_audit.hpp"
#include <cassert>
#include <cstdarg>
#include <chrono>
#include <ctime>
#include <iostream>
#include <thread>
// ─── lifecycle ────────────────────────────────────────────────────────────────

OrderStateManager::OrderStateManager(
    SpscRing<OMSEvent, 256>* const oms_ws_ring,
    SpscRing<OMSEvent, 256>* const oms_rest_ring,
    SpscRing<OMSEvent, 256>* const oms_reconcile_ring,
    const ProductTable& products,
    const char* audit_path)
    : products_(products)
    , oms_ws_ring_(oms_ws_ring)
    , oms_rest_ring_(oms_rest_ring)
    , oms_reconcile_ring_(oms_reconcile_ring)
{
    if (audit_path) {
        audit_ = fopen(audit_path, "w");   // "w" truncates each run; change to "a" to accumulate
        if (!audit_)
            fprintf(stderr, "[oms] WARNING: could not open audit log '%s'\n", audit_path);
    }
}

OrderStateManager::~OrderStateManager() {
    if (audit_) { fflush(audit_); fclose(audit_); }
}

// ─── audit helper ─────────────────────────────────────────────────────────────

void OrderStateManager::audit(const char* fmt, ...) {
    if (!audit_) return;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm t;
    localtime_r(&ts.tv_sec, &t);
    fprintf(audit_, "[%02d:%02d:%02d.%03ld] [oms] ",
            t.tm_hour, t.tm_min, t.tm_sec, ts.tv_nsec / 1'000'000L);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(audit_, fmt, ap);
    va_end(ap);
    fputc('\n', audit_);
    fflush(audit_);
}

// ─── ring drains ──────────────────────────────────────────────────────────────

// Previous implementation (replaced by dispatch_event):

void OrderStateManager::drain_ws_ring() {

    while (auto* oms_event = oms_ws_ring_->pop_begin()) {
        // fprintf(stderr, "drain_ws_ring: ev=%p type=%u\n", (void*)oms_event, (unsigned)(uint8_t)oms_event->type);
        const auto type_index = static_cast<size_t>(oms_event->type);
        (this->*kHandlers[type_index])(*oms_event);

        oms_ws_ring_->pop_commit();
    }
}

void OrderStateManager::drain_rest_ring() {
    while (auto* oms_event = oms_rest_ring_->pop_begin()) {
        const auto type_index = static_cast<size_t>(oms_event->type);
        (this->*kHandlers[type_index])(*oms_event);
        oms_rest_ring_->pop_commit();
    }
}

void OrderStateManager::drain_reconcile_ring() {
    while (auto* oms_event = oms_reconcile_ring_->pop_begin()) {

        const auto type_index = static_cast<size_t>(oms_event->type);
        (this->*kHandlers[type_index])(*oms_event);
        oms_reconcile_ring_->pop_commit();
    }
}

void OrderStateManager::run(std::atomic<bool>& running) {
    while (running.load(std::memory_order_relaxed)) {
        drain_ws_ring();
        drain_rest_ring();
        drain_reconcile_ring();
    }
}

// ─── insert helpers (unchanged) ───────────────────────────────────────────────

static Order* alloc_order_slot(OMSState& state, const OMSEvent& event) {
    return (event.order.side == OrderSide::Buy)
        ? state.bids_[event.order.instrument_id].allocate()
        : state.asks_[event.order.instrument_id].allocate();
}

static void insert_order(OMSState& state, const OMSEvent& event) {
    Order* slot = alloc_order_slot(state, event);
    *slot = event.order;
    state.exchange_id_order_map[event.order.id]            = slot;
    state.client_id_order_map[event.order.client_order_id] = slot;
}

static void insert_stop_order(OMSState& state, const OMSEvent& event) {
    Order* slot = state.stop_orders_[event.order.instrument_id].allocate();
    *slot = event.order;
    state.exchange_id_stop_order_map[event.order.id]            = slot;
    state.client_id_stop_order_map[event.order.client_order_id] = slot;
}

// ─── delete helpers ───────────────────────────────────────────────────────────
// Both use exchange_id as the primary lookup key — always unique, unlike
// client_order_id which is 0 for system-generated orders (e.g. close_all).
// The client map erase is guarded with a pointer check so a later order that
// overwrote client_id_order_map[0] is not accidentally removed.

static void erase_order(OMSState& state, uint64_t exchange_id) {
    auto it = state.exchange_id_order_map.find(exchange_id);
    if (it == state.exchange_id_order_map.end()) return;
    Order* slot = it->second;
    if (slot->side == OrderSide::Buy)
        state.bids_[slot->instrument_id].deallocate(slot);
    else
        state.asks_[slot->instrument_id].deallocate(slot);
    state.exchange_id_order_map.erase(it);
    auto cit = state.client_id_order_map.find(slot->client_order_id);
    if (cit != state.client_id_order_map.end()) state.client_id_order_map.erase(cit);
}

static void erase_stop_order(OMSState& state, uint64_t exchange_id) {
    auto it = state.exchange_id_stop_order_map.find(exchange_id);
    if (it == state.exchange_id_stop_order_map.end()) return;
    Order* slot = it->second;
    state.stop_orders_[slot->instrument_id].deallocate(slot);
    state.exchange_id_stop_order_map.erase(it);
    auto cit = state.client_id_stop_order_map.find(slot->client_order_id);
    if (cit != state.client_id_stop_order_map.end()) state.client_id_stop_order_map.erase(cit);
}

// ─── handle_signal ────────────────────────────────────────────────────────────


void OrderStateManager::handle_signal(const OMSEvent& event) {
    assert(event.source == OMSEventSource::Websocket);

    switch (event.signal) {
        case OMSSignal::OrdersInvalid:
            mark_orders_invalid();
            audit("src=WS type=SIGNAL  sig=%-30s -> maps_cleared pools_reset state=Invalid",
                  oms_audit::signal_str(event.signal));
            break;

        case OMSSignal::OrdersSnapshotComplete:
            state_.orders_state_.store(ChannelState::Valid, std::memory_order_release);
            audit("src=WS type=SIGNAL  sig=%-30s -> state=Valid  open_orders=%zu stop_orders=%zu",
                  oms_audit::signal_str(event.signal),
                  state_.exchange_id_order_map.size(),
                  state_.exchange_id_stop_order_map.size());
            break;

        case OMSSignal::PositionsInvalid:
            mark_positions_invalid();
            audit("src=WS type=SIGNAL  sig=%-30s -> positions_cleared state=Invalid",
                  oms_audit::signal_str(event.signal));
            break;

        case OMSSignal::PositionsSnapshotComplete:
            state_.positions_state_.store(ChannelState::Valid, std::memory_order_release);
            audit("src=WS type=SIGNAL  sig=%-30s -> state=Valid",
                  oms_audit::signal_str(event.signal));
            break;

        case OMSSignal::WalletInvalid:
            mark_wallet_invalid();
            audit("src=WS type=SIGNAL  sig=%-30s -> wallet_cleared state=Invalid",
                  oms_audit::signal_str(event.signal));
            break;

        case OMSSignal::WalletSnapshotComplete:
            state_.wallet_state_.store(ChannelState::Valid, std::memory_order_release);
            audit("src=WS type=SIGNAL  sig=%-30s -> state=Valid",
                  oms_audit::signal_str(event.signal));
            break;
    }
}

// ─── handle_order ─────────────────────────────────────────────────────────────

void OrderStateManager::handle_order(const OMSEvent& event) {
    char buf[768];
    const char* src = oms_audit::src_str(event.source);

    switch (event.source) {

        case OMSEventSource::Websocket: {
            if (state_.orders_state_.load(std::memory_order_relaxed) == ChannelState::Invalid)
                state_.orders_state_.store(ChannelState::Rebuilding, std::memory_order_release);

            if (state_.orders_state_.load(std::memory_order_relaxed) == ChannelState::Rebuilding) {
                insert_order(state_, event);
                oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "ORDER", event.order);
                audit("%s  [snapshot]", buf);
                break;
            }

            switch (event.action) {
                case OMSAction::Create: {
                    auto it = state_.exchange_id_order_map.find(event.order.id);
                    if (it != state_.exchange_id_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "ORDER", event.order);
                        audit("%s  [REST arrived first, slot updated]", buf);
                    } else {
                        insert_order(state_, event);
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "ORDER", event.order);
                        audit("%s  [inserted  exchange_map=%zu client_map=%zu]", buf,
                              state_.exchange_id_order_map.size(),
                              state_.client_id_order_map.size());
                    }
                    break;
                }

                case OMSAction::Update: {
                    auto it = state_.exchange_id_order_map.find(event.order.id);
                    if (it != state_.exchange_id_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        oms_audit::fmt_order_diff(buf, sizeof(buf), src, "ORDER", *slot, event.order);
                        *slot = event.order;
                        audit("%s", buf);
                    }
                    break;
                }

                case OMSAction::Delete: {
                    auto it = state_.exchange_id_order_map.find(event.order.id);
                    if (it != state_.exchange_id_order_map.end()) {
                        Order* slot = it->second;
                        oms_audit::fmt_order(buf, sizeof(buf), src, "DELETE", "ORDER", *slot);
                        erase_order(state_, event.order.id);
                        audit("%s  [removed  exchange_map=%zu client_map=%zu]", buf,
                              state_.exchange_id_order_map.size(),
                              state_.client_id_order_map.size());
                    }
                    break;
                }
            }
            break;
        }

        case OMSEventSource::Rest: {
            switch (event.action) {
                case OMSAction::Create: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "ORDER", event.order);
                        audit("%s  [WS arrived first, slot updated]", buf);
                    } else {
                        insert_order(state_, event);
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "ORDER", event.order);
                        audit("%s  [inserted  exchange_map=%zu client_map=%zu]", buf,
                              state_.exchange_id_order_map.size(),
                              state_.client_id_order_map.size());
                    }
                    break;
                }

                case OMSAction::Update: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        oms_audit::fmt_order_diff(buf, sizeof(buf), src, "ORDER", *slot, event.order);
                        *slot = event.order;
                        audit("%s", buf);
                    }
                    break;
                }

                case OMSAction::Delete: {
                    auto it = state_.exchange_id_order_map.find(event.order.id);
                    if (it != state_.exchange_id_order_map.end()) {
                        Order* slot = it->second;
                        oms_audit::fmt_order(buf, sizeof(buf), src, "DELETE", "ORDER", *slot);
                        erase_order(state_, event.order.id);
                        audit("%s  [removed  exchange_map=%zu client_map=%zu]", buf,
                              state_.exchange_id_order_map.size(),
                              state_.client_id_order_map.size());
                    }
                    break;
                }
            }
            break;
        }

        case OMSEventSource::Reconcile: {
            if (state_.orders_state_.load(std::memory_order_relaxed) != ChannelState::Valid) return;
            auto it = state_.exchange_id_order_map.find(event.order.id);
            if (it == state_.exchange_id_order_map.end()) return;
            Order* slot = it->second;
            if (event.order.timestamp <= slot->timestamp) return;

            if (slot->side != event.order.side || slot->size != event.order.size ||
                slot->state != event.order.state || slot->filled_size != event.order.filled_size ||
                slot->unfilled_size != event.order.unfilled_size) {
                oms_audit::fmt_order_diff(buf, sizeof(buf), src, "ORDER", *slot, event.order);
                audit("%s  [RECONCILE DIFF]", buf);
            }
            break;
        }
    }
}

// ─── handle_stop_order ────────────────────────────────────────────────────────

void OrderStateManager::handle_stop_order(const OMSEvent& event) {
    char buf[768];
    const char* src = oms_audit::src_str(event.source);

    switch (event.source) {

        case OMSEventSource::Websocket: {
            if (state_.orders_state_.load(std::memory_order_relaxed) == ChannelState::Invalid)
                state_.orders_state_.store(ChannelState::Rebuilding, std::memory_order_release);

            if (state_.orders_state_.load(std::memory_order_relaxed) == ChannelState::Rebuilding) {
                insert_stop_order(state_, event);
                oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "STOP", event.order);
                audit("%s  [snapshot]", buf);
                break;
            }

            switch (event.action) {
                case OMSAction::Create: {
                    auto it = state_.exchange_id_stop_order_map.find(event.order.id);
                    if (it != state_.exchange_id_stop_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "STOP", event.order);
                        audit("%s  [REST arrived first, slot updated]", buf);
                    } else {
                        insert_stop_order(state_, event);
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "STOP", event.order);
                        audit("%s  [inserted  stop_map=%zu]", buf,
                              state_.exchange_id_stop_order_map.size());
                    }
                    break;
                }

                case OMSAction::Update: {
                    auto it = state_.exchange_id_stop_order_map.find(event.order.id);
                    if (it != state_.exchange_id_stop_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        oms_audit::fmt_order_diff(buf, sizeof(buf), src, "STOP", *slot, event.order);
                        *slot = event.order;
                        audit("%s", buf);
                    }
                    break;
                }

                case OMSAction::Delete: {
                    auto it = state_.exchange_id_stop_order_map.find(event.order.id);
                    if (it != state_.exchange_id_stop_order_map.end()) {
                        Order* slot = it->second;
                        // Log stop order removal before freeing the slot.
                        oms_audit::fmt_order(buf, sizeof(buf), src, "DELETE", "STOP", *slot);
                        erase_stop_order(state_, event.order.id);
                        audit("%s  [removed  stop_map=%zu]", buf,
                              state_.exchange_id_stop_order_map.size());

                        if (event.order.reason == OrderReason::StopTrigger) {
                            // No WS create will arrive — insert a synthetic open order so
                            // the OMS reflects the triggered state immediately.
                            insert_order(state_, event);
                            oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "ORDER", event.order);
                            audit("%s  [synthetic: stop_trigger, no WS create received]", buf);
                        }
                    }
                    break;
                }
            }
            break;
        }

        case OMSEventSource::Rest: {
            switch (event.action) {
                case OMSAction::Create: {
                    auto it = state_.exchange_id_stop_order_map.find(event.order.id);
                    if (it != state_.exchange_id_stop_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "STOP", event.order);
                        audit("%s  [WS arrived first, slot updated]", buf);
                    } else {
                        insert_stop_order(state_, event);
                        oms_audit::fmt_order(buf, sizeof(buf), src, "CREATE", "STOP", event.order);
                        audit("%s  [inserted  stop_map=%zu]", buf,
                              state_.exchange_id_stop_order_map.size());
                    }
                    break;
                }

                case OMSAction::Update: {
                    auto it = state_.exchange_id_stop_order_map.find(event.order.id);
                    if (it != state_.exchange_id_stop_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        oms_audit::fmt_order_diff(buf, sizeof(buf), src, "STOP", *slot, event.order);
                        *slot = event.order;
                        audit("%s", buf);
                    }
                    break;
                }

                case OMSAction::Delete: {
                    auto it = state_.exchange_id_stop_order_map.find(event.order.id);
                    if (it != state_.exchange_id_stop_order_map.end()) {
                        Order* slot = it->second;
                        oms_audit::fmt_order(buf, sizeof(buf), src, "DELETE", "STOP", *slot);
                        erase_stop_order(state_, event.order.id);
                        audit("%s  [removed  stop_map=%zu]", buf,
                              state_.exchange_id_stop_order_map.size());
                    }
                    break;
                }
            }
            break;
        }

        case OMSEventSource::Reconcile: {
            if (state_.orders_state_.load(std::memory_order_relaxed) != ChannelState::Valid) return;
            auto it = state_.exchange_id_stop_order_map.find(event.order.id);
            if (it == state_.exchange_id_stop_order_map.end()) return;
            Order* slot = it->second;
            if (event.order.timestamp <= slot->timestamp) return;

            if (slot->side != event.order.side || slot->size != event.order.size ||
                slot->state != event.order.state || slot->filled_size != event.order.filled_size ||
                slot->unfilled_size != event.order.unfilled_size) {
                oms_audit::fmt_order_diff(buf, sizeof(buf), src, "STOP", *slot, event.order);
                audit("%s  [RECONCILE DIFF]", buf);
            }
            break;
        }
    }
}

// ─── handle_position ─────────────────────────────────────────────────────────

void OrderStateManager::handle_position(const OMSEvent& event) {
    char buf[768];
    const char* src = oms_audit::src_str(event.source);

    switch (event.source) {
        case OMSEventSource::Websocket: {
            if (state_.positions_state_.load(std::memory_order_relaxed) == ChannelState::Invalid)
                state_.positions_state_.store(ChannelState::Rebuilding, std::memory_order_release);

            if (state_.positions_state_.load(std::memory_order_relaxed) == ChannelState::Rebuilding) {
                state_.positions_[event.position.instrument_id] = event.position;
                oms_audit::fmt_position(buf, sizeof(buf), src, "CREATE", event.position);
                audit("%s  [snapshot]", buf);
                break;
            }

            switch (event.action) {
                case OMSAction::Create: {
                    state_.positions_[event.position.instrument_id] = event.position;
                    oms_audit::fmt_position(buf, sizeof(buf), src, "CREATE", event.position);
                    audit("%s", buf);
                    break;
                }

                case OMSAction::Update: {
                    Position& slot = state_.positions_[event.position.instrument_id];
                    if (slot.timestamp > event.position.timestamp) {
                        audit("src=%s type=POS  action=UPDATE  sym=%s  [stale update dropped ts=%lu slot_ts=%lu]",
                              src, event.position.symbol,
                              event.position.timestamp, slot.timestamp);
                        break;
                    }
                    oms_audit::fmt_position_diff(buf, sizeof(buf), src, slot, event.position);
                    slot = event.position;
                    audit("%s", buf);
                    break;
                }

                case OMSAction::Delete: {
                    oms_audit::fmt_position(buf, sizeof(buf), src, "DELETE",
                                            state_.positions_[event.position.instrument_id]);
                    state_.positions_[event.position.instrument_id] = {};
                    audit("%s  [cleared]", buf);
                    break;
                }
            }
            break;
        }

        case OMSEventSource::Reconcile: {
            if (state_.positions_state_.load(std::memory_order_relaxed) != ChannelState::Valid) return;
            const Position& slot = state_.positions_[event.position.instrument_id];
            if (slot.timestamp > event.position.timestamp) return;

            if (slot.size != event.position.size || slot.instrument_id != event.position.instrument_id) {
                oms_audit::fmt_position_diff(buf, sizeof(buf), src, slot, event.position);
                audit("%s  [RECONCILE DIFF]", buf);
            }
            break;
        }

        case OMSEventSource::Rest:
            break;
    }
}

// ─── handle_fill ──────────────────────────────────────────────────────────────

void OrderStateManager::handle_fill(const OMSEvent& event) {
    (void)event;
}

// ─── handle_wallet ────────────────────────────────────────────────────────────

void OrderStateManager::handle_wallet(const OMSEvent& event) {
    char buf[512];
    const char* src = oms_audit::src_str(event.source);

    switch (event.source) {
        case OMSEventSource::Websocket: {
            if (state_.wallet_state_.load(std::memory_order_relaxed) == ChannelState::Invalid)
                state_.wallet_state_.store(ChannelState::Rebuilding, std::memory_order_release);

            if (state_.wallet_state_.load(std::memory_order_relaxed) == ChannelState::Rebuilding) {
                state_.wallet_ = event.wallet;
                oms_audit::fmt_wallet(buf, sizeof(buf), src, event.wallet);
                audit("%s  [snapshot]", buf);
                break;
            }

            if (event.wallet.timestamp <= state_.wallet_.timestamp) break;
            state_.wallet_ = event.wallet;
            oms_audit::fmt_wallet(buf, sizeof(buf), src, event.wallet);
            audit("%s", buf);
            break;
        }

        case OMSEventSource::Reconcile: {
            if (state_.wallet_state_.load(std::memory_order_relaxed) != ChannelState::Valid) return;
            if (event.wallet.timestamp <= state_.wallet_.timestamp) return;

            if (state_.wallet_.balance          != event.wallet.balance          ||
                state_.wallet_.available_balance != event.wallet.available_balance ||
                state_.wallet_.position_margin   != event.wallet.position_margin   ||
                state_.wallet_.order_margin      != event.wallet.order_margin) {
                oms_audit::fmt_wallet(buf, sizeof(buf), src, event.wallet);
                audit("%s  [RECONCILE DIFF  "
                      "balance=%.6f->%.6f available=%.6f->%.6f "
                      "pos_margin=%.6f->%.6f ord_margin=%.6f->%.6f]",
                      buf,
                      state_.wallet_.balance,           event.wallet.balance,
                      state_.wallet_.available_balance,  event.wallet.available_balance,
                      state_.wallet_.position_margin,    event.wallet.position_margin,
                      state_.wallet_.order_margin,       event.wallet.order_margin);
            }
            state_.wallet_ = event.wallet;
            break;
        }

        case OMSEventSource::Rest:
            break;
    }
}

// ─── invalidation helpers ────────────────────────────────────────────────────

void OrderStateManager::mark_orders_invalid() {
    state_.orders_state_.store(ChannelState::Invalid, std::memory_order_release);
    state_.open_buy_contracts_.fill(0);
    state_.open_sell_contracts_.fill(0);
    state_.open_buy_notional_.fill(0.0);
    state_.open_sell_notional_.fill(0.0);
    state_.client_id_order_map.clear();
    state_.exchange_id_order_map.clear();
    state_.client_id_stop_order_map.clear();
    state_.exchange_id_stop_order_map.clear();
    for (auto& pool : state_.bids_)        pool.reset();
    for (auto& pool : state_.asks_)        pool.reset();
    for (auto& pool : state_.stop_orders_) pool.reset();
}

void OrderStateManager::mark_positions_invalid() {
    state_.positions_state_.store(ChannelState::Invalid, std::memory_order_release);
    state_.positions_ = {};
}

void OrderStateManager::mark_wallet_invalid() {
    state_.wallet_state_.store(ChannelState::Invalid, std::memory_order_release);
    state_.wallet_ = {};
}

// ─── state accessors ─────────────────────────────────────────────────────────

ChannelState OrderStateManager::get_orders_state() const {
    return state_.orders_state_.load(std::memory_order_acquire);
}

ChannelState OrderStateManager::get_positions_state() const {
    return state_.positions_state_.load(std::memory_order_acquire);
}
