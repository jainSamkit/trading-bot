#pragma once
#include "delta_exchange/models/product.hpp"
#include "core/memory_pool.hpp"
#include "delta_exchange/models/order.hpp"
#include "delta_exchange/models/position.hpp"
#include "delta_exchange/models/fill.hpp"
#include "delta_exchange/models/wallet.hpp"
#include "core/spsc_ring.hpp"
#include "config/config.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>


enum class OMSSignal : uint8_t {
    OrdersInvalid,
    PositionsInvalid,
    WalletInvalid,
    OrdersSnapshotComplete,
    PositionsSnapshotComplete,
    WalletSnapshotComplete,
};

enum class OMSEventType : uint8_t {
    Order,
    StopOrder,
    Position,
    Fill,
    Wallet,
    OMSSignal
};

enum class OMSEventSource : uint8_t {
    Rest,
    Websocket,
    Reconcile
};

enum class ChannelState : uint8_t {
    Invalid,
    Rebuilding,
    Valid
};

enum class OMSAction : uint8_t {
    Create, 
    Update, 
    Delete
};

struct OMSEvent {
    OMSEvent() : type{}, source{}, action{}, order{} {}
    OMSEventType   type;
    OMSEventSource source;
    OMSAction      action;
    union {
        Order       order;
        Position    position;
        Wallet      wallet;
        Fill        fill;
        OMSSignal   signal;
    };
};



struct OMSState {
    Wallet                                                              wallet_;
    std::array<MemoryPool<Order, 64>, cfg::MAX_INSTRUMENTS>    bids_;
    std::array<MemoryPool<Order, 64>, cfg::MAX_INSTRUMENTS>    asks_;
    std::array<MemoryPool<Order, 64>, cfg::MAX_INSTRUMENTS>    stop_orders_;

    std::array<Position, cfg::MAX_INSTRUMENTS>                 positions_;

    std::unordered_map<uint64_t, Order*>                                exchange_id_order_map;
    std::unordered_map<uint64_t, Order*>                                client_id_order_map;

    std::unordered_map<uint64_t, Order*>                                exchange_id_stop_order_map;
    std::unordered_map<uint64_t, Order*>                                client_id_stop_order_map;

    std::array<uint32_t, cfg::MAX_INSTRUMENTS>                 open_buy_contracts_{};
    std::array<uint32_t, cfg::MAX_INSTRUMENTS>                 open_sell_contracts_{};                                                                                                                                             
    std::array<double,   cfg::MAX_INSTRUMENTS>                 open_buy_notional_{};                                                                                                                                                                  
    std::array<double,   cfg::MAX_INSTRUMENTS>                 open_sell_notional_{};

    std::atomic<ChannelState> orders_state_    {ChannelState::Invalid};
    std::atomic<ChannelState> positions_state_ {ChannelState::Invalid};
    std::atomic<ChannelState> wallet_state_    {ChannelState::Invalid};
};


class OrderStateManager {

    static constexpr size_t OMS_RING_SIZE = cfg::OMS_RING_SIZE;
public:

    explicit OrderStateManager(
        SpscRing<OMSEvent, OMS_RING_SIZE>* const oms_ws_ring,
        SpscRing<OMSEvent, OMS_RING_SIZE>* const oms_rest_ring,
        SpscRing<OMSEvent, OMS_RING_SIZE>* const oms_reconcile_ring,
        const ProductTable& products,
        const char* audit_path = "oms_audit.log");

    ~OrderStateManager();

    void run(std::atomic<bool>& running);

    ChannelState get_orders_state()    const;
    ChannelState get_positions_state() const;

    // Test/debug only — read state without synchronisation (accepted race for logging).
    const OMSState& debug_state() const { return state_; }

private:
    void drain_ws_ring();
    void drain_rest_ring();
    void drain_reconcile_ring();

    void handle_order(const OMSEvent& event);
    void handle_stop_order(const OMSEvent& event);
    void handle_position(const OMSEvent& event);
    void handle_fill(const OMSEvent& event);
    void handle_wallet(const OMSEvent& event);
    void handle_signal(const OMSEvent& event);
    // void hello_there(const OMSEvent& event);

    // void dispatch_event(const OMSEvent& event);

    // Previous dispatch (member-pointer table; replaced by dispatch_event + switch — avoids UBSan on bad ev->type):
    using Handler = void (OrderStateManager::*)(const OMSEvent&);
    static constexpr Handler kHandlers[] = {
        &OrderStateManager::handle_order,
        &OrderStateManager::handle_stop_order,
        &OrderStateManager::handle_position,
        &OrderStateManager::handle_fill,
        &OrderStateManager::handle_wallet,
        &OrderStateManager::handle_signal
    };

    void mark_orders_invalid();
    void mark_positions_invalid();
    void mark_wallet_invalid();

    void audit(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    const ProductTable&      products_;
    OMSState                 state_;
    SpscRing<OMSEvent, OMS_RING_SIZE>* oms_ws_ring_;
    SpscRing<OMSEvent, OMS_RING_SIZE>* oms_rest_ring_;
    SpscRing<OMSEvent, OMS_RING_SIZE>* oms_reconcile_ring_;
    FILE*                    audit_ = nullptr;
};