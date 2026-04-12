#include "oms/oms_manager.hpp"

OrderStateManager::OrderStateManager(
    SpscRing<OMSEvent, 256>* const oms_ws_ring,
    SpscRing<OMSEvent, 256>* const oms_rest_ring,
    SpscRing<OMSEvent, 256>* const oms_reconcile_ring,
    const ProductTable& products)
    : oms_ws_ring_(oms_ws_ring)
    , oms_rest_ring_(oms_rest_ring)
    , oms_reconcile_ring_(oms_reconcile_ring)
    , products_(products) {}

void OrderStateManager::run(std::atomic<bool>& running) {
    while (running.load(std::memory_order_relaxed)) {
        drain_ws_ring();
        drain_rest_ring();
        drain_reconcile_ring();
    }
}

void OrderStateManager::drain_ws_ring() {
    while (auto oms_event = oms_ws_ring_->pop()) {
        if (oms_event->type == OMSEventType::Order)     handle_order(*oms_event);
        if (oms_event->type == OMSEventType::Position)  handle_position(*oms_event);
        if (oms_event->type == OMSEventType::Fill)      handle_fill(*oms_event);
        if (oms_event->type == OMSEventType::Wallet)    handle_wallet(*oms_event);
        if (oms_event->type == OMSEventType::OMSSignal) handle_signal(*oms_event);
    }
}

void OrderStateManager::drain_rest_ring() {
    while (auto oms_event = oms_rest_ring_->pop()) {
        if (oms_event->type == OMSEventType::Order)    handle_order(*oms_event);
        if (oms_event->type == OMSEventType::Position) handle_position(*oms_event);
        if (oms_event->type == OMSEventType::Fill)     handle_fill(*oms_event);
        if (oms_event->type == OMSEventType::Wallet)   handle_wallet(*oms_event);
    }
}

void OrderStateManager::drain_reconcile_ring() {
    while (auto oms_event = oms_reconcile_ring_->pop()) {
        if (oms_event->type == OMSEventType::Order)    handle_order(*oms_event);
        if (oms_event->type == OMSEventType::Position) handle_position(*oms_event);
        if (oms_event->type == OMSEventType::Fill)     handle_fill(*oms_event);
        if (oms_event->type == OMSEventType::Wallet)   handle_wallet(*oms_event);
    }
}

void OrderStateManager::handle_signal(OMSEvent& event) {
    assert(event.source == OMSEventSource::Websocket);

    switch (event.signal) {
        case OMSSignal::OrdersInvalid:
            mark_orders_invalid();
            break;

        case OMSSignal::OrdersSnapshotComplete:
            state_.orders_state_ = ChannelState::Valid;
            break;

        case OMSSignal::PositionsInvalid:
            mark_positions_invalid();
            break;

        case OMSSignal::PositionsSnapshotComplete:
            state_.positions_state_ = ChannelState::Valid;
            break;

        case OMSSignal::WalletInvalid:
            mark_wallet_invalid();
            break;

        case OMSSignal::WalletSnapshotComplete:
            state_.wallet_state_ = ChannelState::Valid;
            break;
    }
}

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

void OrderStateManager::handle_order(OMSEvent& event) {
    switch (event.source) {

        case OMSEventSource::Websocket: {
            if (state_.orders_state_ == ChannelState::Invalid)
                state_.orders_state_ = ChannelState::Rebuilding;

            if (state_.orders_state_ == ChannelState::Rebuilding) {
                // snapshot in progress — insert all incoming orders
                insert_order(state_, event);
                break;
            }

            // Valid path
            switch (event.action) {
                case OMSAction::Create: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        // order exists — REST arrived first, update only if WS is newer
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                    } else {
                        insert_order(state_, event);
                    }
                    break;
                }

                case OMSAction::Update: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                    }
                    // else: log discrepancy — order to update not found
                    break;
                }

                case OMSAction::Delete: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        Order* slot = it->second;
                        if (event.order.side == OrderSide::Buy)
                            state_.bids_[event.order.instrument_id].deallocate(slot);
                        else
                            state_.asks_[event.order.instrument_id].deallocate(slot);
                        state_.client_id_order_map.erase(it);
                        state_.exchange_id_order_map.erase(event.order.id);
                    }
                    // else: log order to delete not found
                    break;
                }
            }
            break;
        }

        case OMSEventSource::Rest: {
            // always apply regardless of channel state
            switch (event.action) {
                case OMSAction::Create: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        // WS arrived first — update only if REST is newer
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                    } else {
                        insert_order(state_, event);
                    }
                    break;
                }

                case OMSAction::Update: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        Order* slot = it->second;
                        if (slot->timestamp >= event.order.timestamp) break;
                        *slot = event.order;
                    }
                    break;
                }

                case OMSAction::Delete: {
                    auto it = state_.client_id_order_map.find(event.order.client_order_id);
                    if (it != state_.client_id_order_map.end()) {
                        Order* slot = it->second;
                        if (event.order.side == OrderSide::Buy)
                            state_.bids_[event.order.instrument_id].deallocate(slot);
                        else
                            state_.asks_[event.order.instrument_id].deallocate(slot);
                        state_.client_id_order_map.erase(it);
                        state_.exchange_id_order_map.erase(event.order.id);
                    }
                    break;
                }
            }
            break;
        }

        case OMSEventSource::Reconcile: {
            if (state_.orders_state_ != ChannelState::Valid) return;

            auto it = state_.client_id_order_map.find(event.order.client_order_id);
            if (it == state_.client_id_order_map.end()) return;

            Order* slot = it->second;
            if (event.order.timestamp <= slot->timestamp) return;

            // reconcile is ahead of local state — log field diffs
            if (slot->side          != event.order.side          ||
                slot->size          != event.order.size          ||
                slot->state         != event.order.state         ||
                slot->filled_size   != event.order.filled_size   ||
                slot->unfilled_size != event.order.unfilled_size ||
                slot->instrument_id != event.order.instrument_id) {
                std::cerr << "[reconcile diff] id=" << event.order.id
                          << " client_id=" << event.order.client_order_id
                          << " side="      << (slot->side != event.order.side ? "DIFF" : "ok")
                          << " size="      << slot->size          << "->" << event.order.size
                          << " state="     << static_cast<int>(slot->state) << "->" << static_cast<int>(event.order.state)
                          << " filled="    << slot->filled_size   << "->" << event.order.filled_size
                          << " unfilled="  << slot->unfilled_size << "->" << event.order.unfilled_size
                          << "\n";
            }
            break;
        }
    }
}

void OrderStateManager::handle_position(OMSEvent& event) {

    switch(event.source) {
        case OMSEventSource::Websocket: {
            if (state_.positions_state_ == ChannelState::Invalid)
                state_.positions_state_ = ChannelState::Rebuilding;

            if (state_.positions_state_ == ChannelState::Rebuilding) {
                // snapshot in progress — insert all incoming positions
                state_.positions_[event.position.instrument_id] = event.position;
                break;
            }

            switch(event.action) {
                case OMSAction::Create: {
                    state_.positions_[event.position.instrument_id] = event.position;
                    break;
                }

                case OMSAction::Update: {
                    if(state_.positions_[event.position.instrument_id].timestamp > event.position.timestamp) {
                        //log, position is newer than the update, the update is stale
                        break;
                    }

                    //also assert that the positions exists 
                    assert state_.positions_[event.position.instrument_id].instrument_id == event.position.instrument_id;
                    state_.positions_[event.position.instrument_id] = event.position;
                    break;
                }

                case OMSAction::Delete: {
                    state_.positions_[event.position.instrument_id]= {};
                    break;
                }
            }

            break;
        }
        case OMSEventSource::Reconcile:
            if(state_.positions_state_ != ChannelState::Valid) return;
            if(state_.positions_[event.position.instrument_id].timestamp > event.position.timestamp) return;

            Position slot = state_.positions_[event.position.instrument_id];

            if (slot->size          != event.position.size          ||
                slot->instrument_id != event.position.instrument_id) {
                std::cerr << "[reconcile diff] id=" << event..id
                          << " side="      << (slot->side != event.position.side ? "DIFF" : "ok")
                          << " size="      << slot->size          << "->" << event.position.size
                          << "\n";
            }
            break;
    }
}

void OrderStateManager::handle_fill(OMSEvent& event) {}

void OrderStateManager::handle_wallet(OMSEvent& event) {
    switch (event.source) {

        case OMSEventSource::Websocket: {
            if (state_.wallet_state_ == ChannelState::Invalid)
                state_.wallet_state_ = ChannelState::Rebuilding;

            if (state_.wallet_state_ == ChannelState::Rebuilding) {
                state_.wallet_ = event.wallet;
                break;
            }

            // Valid path — timestamp guard
            if (event.wallet.timestamp <= state_.wallet_.timestamp) break;
            state_.wallet_ = event.wallet;
            break;
        }

        case OMSEventSource::Reconcile: {
            if (state_.wallet_state_ != ChannelState::Valid) return;
            if (event.wallet.timestamp <= state_.wallet_.timestamp) return;

            // reconcile is ahead — log diffs
            if (state_.wallet_.balance           != event.wallet.balance           ||
                state_.wallet_.available_balance  != event.wallet.available_balance  ||
                state_.wallet_.position_margin    != event.wallet.position_margin    ||
                state_.wallet_.order_margin       != event.wallet.order_margin) {
                std::cerr << "[reconcile wallet diff]"
                          << " balance="    << state_.wallet_.balance           << "->" << event.wallet.balance
                          << " available="  << state_.wallet_.available_balance  << "->" << event.wallet.available_balance
                          << " pos_margin=" << state_.wallet_.position_margin    << "->" << event.wallet.position_margin
                          << " ord_margin=" << state_.wallet_.order_margin       << "->" << event.wallet.order_margin
                          << "\n";
            }

            state_.wallet_  = event.wallet;
            break;
        }

        case OMSEventSource::Rest:
            break;  // no REST source for wallet
    }
}

void OrderStateManager::mark_orders_invalid() {
    state_.orders_state_ = ChannelState::Invalid;
    state_.open_buy_contracts_.fill(0);
    state_.open_sell_contracts_.fill(0);
    state_.open_buy_notional_.fill(0.0);
    state_.open_sell_notional_.fill(0.0);
    state_.client_id_order_map.clear();
    state_.exchange_id_order_map.clear();
    for (auto& pool : state_.bids_) pool.reset();
    for (auto& pool : state_.asks_) pool.reset();
}

void OrderStateManager::mark_positions_invalid() {
    state_.positions_state_ = ChannelState::Invalid;
    state_.positions_ = {};
}

void OrderStateManager::mark_wallet_invalid() {
    state_.wallet_state_ = ChannelState::Invalid;
    state_.wallet_ = {};
}
