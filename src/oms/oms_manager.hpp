#include "delta_exchange/models/product.hpp"
#include "core/memory_pool.hpp"
#include "delta_exchange/models/order.hpp"
#include "delta_exchange/models/position.hpp"
#include "delta_exchange/models/fill.hpp"
#include "core/spsc_ring.hpp"
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
    std::array<Position, ProductTable::MAX_INSTRUMENTS>                 positions_;

    std::unordered_map<uint64_t, Order*>                                exchange_id_order_map;
    std::unordered_map<uint64_t, Order*>                                client_id_order_map;

    std::array<uint32_t, ProductTable::MAX_INSTRUMENTS>                 open_buy_contracts_{};
    std::array<uint32_t, ProductTable::MAX_INSTRUMENTS>                 open_sell_contracts_{};                                                                                                                                             
    std::array<double,   ProductTable::MAX_INSTRUMENTS>                 open_buy_notional_{};                                                                                                                                                                  
    std::array<double,   ProductTable::MAX_INSTRUMENTS>                 open_sell_notional_{};

    ChannelState orders_state_ =                                         ChannelState::Invalid;
    ChannelState positions_state_ =                                      ChannelState::Invalid;
    ChannelState wallet_state_ =                                         ChannelState::Invalid;
};


class OrderStateManager {
public:

    explicit OrderStateManager(
        SpscRing<OMSEvent, 256>* const oms_ws_ring, 
        SpscRing<OMSEvent, 256>* const oms_rest_ring, 
        SpscRing<OMSEvent, 256>* const oms_reconcile_ring,
        const ProductTable& products);

    void run(std::atomic<bool>& running);

    void onOrderUpdate(Order& order);
    void onOrderCreate(Order& order);
    void onOrderDelete(uint64_t order_id);

    void onPositionCreate(Position& position);
    void onPositionUpdate(Position& position);
    void onPositionDelete(uint8_t instrument_id);
    void onFill(Fill& fill);

    void drain_ws_ring();
    void drain_rest_ring();
    void drain_reconcile_ring();

    void mark_orders_invalid();
    void mark_positions_invalid();
    void mark_wallet_invalid();


private:
    const ProductTable&                                     products_;
    OMSState                                                state_;
    SpscRing<OMSEvent, 256>*                                oms_ws_ring_;
    SpscRing<OMSEvent, 256>*                                oms_rest_ring_;
    SpscRing<OMSEvent, 256>*                                oms_reconcile_ring_;
};