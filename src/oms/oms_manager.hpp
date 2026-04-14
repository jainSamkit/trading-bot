#include "delta_exchange/models/product.hpp"
#include "core/memory_pool.hpp"
#include "delta_exchange/models/order.hpp"
#include "delta_exchange/models/position.hpp"
#include "delta_exchange/models/fill.hpp"
#include "core/spsc_ring.hpp"
#include <atomic>
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
    OMSSignal,
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
    std::array<MemoryPool<Order, 64>, ProductTable::MAX_INSTRUMENTS>    bids_;
    std::array<MemoryPool<Order, 64>, ProductTable::MAX_INSTRUMENTS>    asks_;
    std::array<MemoryPool<Order, 64>, ProductTable::MAX_INSTRUMENTS>    stop_orders_;

    std::array<Position, ProductTable::MAX_INSTRUMENTS>                 positions_;

    std::unordered_map<uint64_t, Order*>                                exchange_id_order_map;
    std::unordered_map<uint64_t, Order*>                                client_id_order_map;

    std::unordered_map<uint64_t, Order*>                                exchange_id_stop_order_map;
    std::unordered_map<uint64_t, Order*>                                client_id_stop_order_map;

    std::array<uint32_t, ProductTable::MAX_INSTRUMENTS>                 open_buy_contracts_{};
    std::array<uint32_t, ProductTable::MAX_INSTRUMENTS>                 open_sell_contracts_{};                                                                                                                                             
    std::array<double,   ProductTable::MAX_INSTRUMENTS>                 open_buy_notional_{};                                                                                                                                                                  
    std::array<double,   ProductTable::MAX_INSTRUMENTS>                 open_sell_notional_{};

    std::atomic<ChannelState> orders_state_    {ChannelState::Invalid};
    std::atomic<ChannelState> positions_state_ {ChannelState::Invalid};
    std::atomic<ChannelState> wallet_state_    {ChannelState::Invalid};
};


class OrderStateManager {
public:

    explicit OrderStateManager(
        SpscRing<OMSEvent, 256>* const oms_ws_ring, 
        SpscRing<OMSEvent, 256>* const oms_rest_ring, 
        SpscRing<OMSEvent, 256>* const oms_reconcile_ring,
        const ProductTable& products);

    void run(std::atomic<bool>& running);

    ChannelState get_orders_state() const;
    ChannelState get_positions_state() const;

private:
    void drain_ws_ring();
    void drain_rest_ring();
    void drain_reconcile_ring();

    using Handler = void (OrderStateManager::*)(const OMSEvent&);
    static constexpr Handler kHandlers[] = {
        &OrderStateManager::handle_order,
        &OrderStateManager::handle_stop_order,
        &OrderStateManager::handle_position,
        &OrderStateManager::handle_fill,
        &OrderStateManager::handle_wallet,
        &OrderStateManager::handle_signal,
    };

    void handle_order(const OMSEvent& event);
    void handle_stop_order(const OMSEvent& event);
    void handle_position(const OMSEvent& event);
    void handle_fill(const OMSEvent& event);
    void handle_wallet(const OMSEvent& event);
    void handle_signal(const OMSEvent& event);

    void mark_orders_invalid();
    void mark_positions_invalid();
    void mark_wallet_invalid();

    const ProductTable&      products_;
    OMSState                 state_;
    SpscRing<OMSEvent, 256>* oms_ws_ring_;
    SpscRing<OMSEvent, 256>* oms_rest_ring_;
    SpscRing<OMSEvent, 256>* oms_reconcile_ring_;
};