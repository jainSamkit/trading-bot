//  OMS end-to-end test — Delta Exchange testnet.
//
//  Tests (in order):
//    1. Channel snapshot       — all channels reach Valid within 30 s.
//    2. CRUD + WS echo         — REST create/edit/cancel, verify WS echo in OMS.
//    3. Forced reconnect       — order session disconnected; verify recovery.
//    4. Reconcile              — REST open-orders/positions/wallet drift check.
//    5. Stop orders            — create SL + TP stop orders, verify in OMS
//                                stop_order_map, then cancel both.
//    6. Market order + pos     — market buy 1 contract, wait for position in OMS,
//                                verify side/size/instrument_id.
//    7. Cancel all orders      — create 2 limit orders, cancel_all (limit only),
//                                verify orders cleared, position unchanged.
//    8. Close all positions    — create a limit order, close_all_positions, verify
//                                position cleared and orders cancelled in OMS.
//
//  Build:   cmake --build build --target oms_test
//  Run:     ./build/oms_test

#include "core/logger.hpp"
#include "core/spsc_ring.hpp"
#include "config/config.hpp"
#include "config/env.hpp"
#include "delta_exchange/oms_ws_client.hpp"
#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/api/product/product.hpp"
#include "delta_exchange/api/order/order.hpp"
#include "oms/execution_manager.hpp"
#include "oms/oms_manager.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

// ─── compile-time constants ───────────────────────────────────────────────────

static constexpr const char* TEST_SYMBOL = "SOLUSD";
static constexpr size_t OMS_RING_SIZE    = cfg::OMS_RING_SIZE;

// Test against testnet by default. .env can flip this by changing
// DELTA_REST_HOST / DELTA_WS_PRIVATE_HOST.

// ─── globals ──────────────────────────────────────────────────────────────────

static std::atomic<bool>        g_running{true};
static DeltaOMSWebsocketClient* g_ws = nullptr;

static void on_signal(int) {
    g_running.store(false, std::memory_order_relaxed);
    if (g_ws) g_ws->shutdown();
}

// ─── test result ──────────────────────────────────────────────────────────────

struct TestResult {
    bool        passed;
    const char* name;
    const char* detail;  // nullptr = none
};

static void print_result(const TestResult& r) {
    if (r.passed)
        LOG_INFO("RESULT", "✓  PASS  %s", r.name);
    else
        LOG_ERROR("RESULT", "✗  FAIL  %s  — %s", r.name, r.detail ? r.detail : "");
}

// ─── wait helpers ─────────────────────────────────────────────────────────────

// Returns true if predicate becomes true within timeout_ms; polls every 200 ms.
template<typename Pred>
static bool wait_until(Pred pred, int timeout_ms) {
    for (int elapsed = 0; elapsed < timeout_ms && g_running.load(); elapsed += 200) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return pred();
}

static bool channels_valid(const OrderStateManager& mgr) {
    return mgr.get_orders_state()    == ChannelState::Valid &&
           mgr.get_positions_state() == ChannelState::Valid;
}

// ─── state dump ───────────────────────────────────────────────────────────────

static void dump_orders(const OrderStateManager& mgr, const ProductTable& products) {
    const OMSState& s = mgr.debug_state();
    size_t total = s.exchange_id_order_map.size();
    LOG_INFO("state", "open orders: %zu", total);
    for (auto& [id, o] : s.exchange_id_order_map) {
        const char* sym = (o->instrument_id < products.count)
                        ? products[o->instrument_id].symbol : "?";
        LOG_INFO("state", "  id=%-12lu  sym=%-8s  side=%s  size=%-4u  px=%.4f  state=%d  ts=%lu",
                 o->id, sym,
                 o->side == OrderSide::Buy ? "buy" : "sell",
                 o->size, o->limit_price,
                 (int)o->state,
                 o->timestamp);
    }

    for (auto& [id, o] : s.exchange_id_stop_order_map) {
        const char* sym = (o->instrument_id < products.count)
                        ? products[o->instrument_id].symbol : "?";
        LOG_INFO("state", "  id=%-12lu  sym=%-8s  side=%s  size=%-4u  px=%.4f  state=%d  ts=%lu",
                 o->id, sym,
                 o->side == OrderSide::Buy ? "buy" : "sell",
                 o->size, o->limit_price,
                 (int)o->state,
                 o->timestamp);
    }

    for (uint8_t i = 0; i < products.count; ++i) {
        const Position& p = s.positions_[i];
        if (p.size == 0) continue;
        LOG_INFO("state", "  pos sym=%-8s  size=%-4d  entry=%.4f  margin=%.4f  rpnl=%.4f",
                 p.symbol, p.size, p.entry_price, p.margin, p.realized_pnl);
    }
    LOG_INFO("state", "  wallet  balance=%.4f  available=%.4f  pos_margin=%.4f  ord_margin=%.4f",
             s.wallet_.balance, s.wallet_.available_balance,
             s.wallet_.position_margin, s.wallet_.order_margin);
}

// ─── test 1: channel snapshot ─────────────────────────────────────────────────

static TestResult test_channel_snapshot(const OrderStateManager& mgr) {
    LOG_INFO("test1", "waiting for all channels to reach Valid (timeout 30 s)...");
    bool ok = wait_until([&]{ return channels_valid(mgr); }, 30000);
    if (!ok) return {false, "channel_snapshot", "channels did not reach Valid within 30 s"};
    LOG_INFO("test1", "orders=Valid  positions=Valid  wallet=%s",
             mgr.debug_state().wallet_state_.load() == ChannelState::Valid ? "Valid" : "not Valid");
    return {true, "channel_snapshot", nullptr};
}

// ─── test 2: CRUD + WS echo ───────────────────────────────────────────────────

static TestResult test_crud_ws_echo(DeltaRestClient& rest,
                                    const OrderStateManager& mgr,
                                    const ProductTable& products) {
    if (!channels_valid(mgr))
        return {false, "crud_ws_echo", "channels not Valid at test start"};

    const Product& p  = products[products.idfromSymbol(TEST_SYMBOL)];
    SpscRing<OMSEvent, OMS_RING_SIZE> rest_ring;

    // ── CREATE ────────────────────────────────────────────────────────────────
    LOG_INFO("test2", "--- CREATE ---");
    ExecutionIntent ci;
    ci.action = ExecutionAction::CreateOrder;
    const uint64_t test_client_id = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    ci.create_order_intent = {
        .side                = ExecutionOrderSide::Buy,
        .type                = ExecutionOrderType::Limit,
        .price               = 50.0,             // far OTM for SOL (~$85 mark) — won't fill
        .tick_size           = p.tick_size,
        .exchange_product_id = p.exchange_id,
        .size                = 1,
        .client_order_id     = test_client_id,
        .reduce_only         = false,
        .post_only           = true,
    };
    rest.create_order(ci, &rest_ring);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    uint64_t order_id  = 0;
    uint64_t rest_ts_create = 0;
    if (auto* ev = rest_ring.pop_begin()) {
        order_id       = ev->order.id;
        rest_ts_create = ev->order.timestamp;
        LOG_INFO("test2", "REST create  id=%lu  px=%.4f  rest_ts=%lu",
                 order_id, ev->order.limit_price, rest_ts_create);
        rest_ring.pop_commit();
    } else {
        return {false, "crud_ws_echo", "create_order: no REST response"};
    }

    // wait for WS echo to arrive in OMS state
    bool ws_arrived = wait_until([&]{
        const OMSState& s = mgr.debug_state();
        return s.exchange_id_order_map.count(order_id) > 0;
    }, 3000);

    if (!ws_arrived) {
        return {false, "crud_ws_echo", "WS create echo did not arrive within 5 s"};
    }
    {
        const OMSState& s = mgr.debug_state();
        auto it = s.exchange_id_order_map.find(order_id);
        if (it != s.exchange_id_order_map.end()) {
            uint64_t ws_ts = it->second->timestamp;
            LOG_INFO("test2", "WS  create   id=%lu  px=%.4f  ws_ts=%lu  applied=%s",
                     order_id, it->second->limit_price, ws_ts,
                     ws_ts >= rest_ts_create ? "WS (newer)" : "REST (newer)");
        }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // ── EDIT ─────────────────────────────────────────────────────────────────
    LOG_INFO("test2", "--- EDIT ---");
    ExecutionIntent ei;
    ei.action = ExecutionAction::EditOrder;
    ei.edit_order_intent = {
        .exchange_product_id = p.exchange_id,
        .order_id            = order_id,
        .size                = 1,
        .price               = 45.0,
        .tick_size           = p.tick_size,
        .type                = ExecutionOrderType::Limit,
        .post_only           = true,
    };
    rest.edit_order(ei, &rest_ring);

    uint64_t rest_ts_edit = 0;
    if (auto* ev = rest_ring.pop_begin()) {
        rest_ts_edit = ev->order.timestamp;
        LOG_INFO("test2", "REST edit    id=%lu  new_px=%.4f  rest_ts=%lu",
                 ev->order.id, ev->order.limit_price, rest_ts_edit);
        rest_ring.pop_commit();
    } else {
        return {false, "crud_ws_echo", "edit_order: no REST response"};
    }

    // wait for OMS state to reflect price change
    bool edit_applied = wait_until([&]{
        const OMSState& s = mgr.debug_state();
        auto it = s.exchange_id_order_map.find(order_id);
        return it != s.exchange_id_order_map.end() && it->second->limit_price <= 46.0;
    }, 3000);

    {
        const OMSState& s = mgr.debug_state();
        auto it = s.exchange_id_order_map.find(order_id);
        if (it != s.exchange_id_order_map.end()) {
            LOG_INFO("test2", "OMS  post-edit  px=%.4f  ts=%lu  edit_reflected=%s",
                     it->second->limit_price, it->second->timestamp,
                     edit_applied ? "yes" : "no");
        }
    }
    if (!edit_applied)
        return {false, "crud_ws_echo", "price edit not reflected in OMS state within 5 s"};

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // ── CANCEL ────────────────────────────────────────────────────────────────
    LOG_INFO("test2", "--- CANCEL ---");
    ExecutionIntent xi;
    xi.action = ExecutionAction::CancelOrder;
    xi.cancel_order_intent = {
        .order_id            = order_id,
        .exchange_product_id = p.exchange_id,
    };
    rest.cancel_order(xi, &rest_ring);

    if (auto* ev = rest_ring.pop_begin()) {
        LOG_INFO("test2", "REST cancel  id=%lu  state=%d  rest_ts=%lu",
                 ev->order.id, (int)ev->order.state, ev->order.timestamp);
        rest_ring.pop_commit();
    } else {
        return {false, "crud_ws_echo", "cancel_order: no REST response"};
    }

    // wait for WS delete echo — order removed from OMS map
    bool cancelled = wait_until([&]{
        const OMSState& s = mgr.debug_state();
        return s.exchange_id_order_map.count(order_id) == 0;
    }, 3000);

    if (!cancelled) {
        LOG_WARN("test2", "order %lu still in OMS map after cancel — WS delete echo late?", order_id);
        return {false, "crud_ws_echo", "WS delete echo did not remove order within 5 s"};
    }
    LOG_INFO("test2", "WS cancel echo received — order %lu removed from OMS state", order_id);
    return {true, "crud_ws_echo", nullptr};
}

// ─── test 3: forced reconnect ─────────────────────────────────────────────────

static TestResult test_reconnect(DeltaOMSWebsocketClient& ws,
                                 const OrderStateManager& mgr) {
    if (!channels_valid(mgr))
        return {false, "reconnect", "channels not Valid before forcing disconnect"};

    LOG_INFO("test3", "forcing TCP disconnect on order session...");
    ws.force_disconnect_for_test();

    // step 1 — orders channel should go Invalid within 3 s (OrdersInvalid signal pushed on reconnect)
    bool went_invalid = wait_until([&]{
        return mgr.get_orders_state() != ChannelState::Valid;
    }, 3000);
    LOG_INFO("test3", "after force-disconnect: orders_state=%s",
             mgr.get_orders_state() == ChannelState::Invalid    ? "Invalid"    :
             mgr.get_orders_state() == ChannelState::Rebuilding ? "Rebuilding" : "Valid");
    if (!went_invalid)
        return {false, "reconnect", "orders channel did not leave Valid after force-disconnect"};

    // step 2 — Rebuilding phase: first snapshot order arrives
    bool rebuilding = wait_until([&]{
        return mgr.get_orders_state() == ChannelState::Rebuilding;
    }, 8000);
    LOG_INFO("test3", "rebuilding reached: %s", rebuilding ? "yes" : "no (still Invalid — no open orders?)");
    // Note: if there are no open orders, the snapshot is empty and we skip straight to Valid.

    // step 3 — back to Valid after snapshot complete signal
    bool recovered = wait_until([&]{
        return channels_valid(mgr);
    }, 20000);
    LOG_INFO("test3", "final state: orders=%s  positions=%s",
             mgr.get_orders_state()    == ChannelState::Valid ? "Valid" : "not Valid",
             mgr.get_positions_state() == ChannelState::Valid ? "Valid" : "not Valid");
    if (!recovered)
        return {false, "reconnect", "channels did not recover to Valid within 60 s after reconnect"};

    return {true, "reconnect", nullptr};
}

// ─── test 4: reconcile ────────────────────────────────────────────────────────

static TestResult test_reconcile(DeltaRestClient& rest,
                                 const OrderStateManager& mgr,
                                 SpscRing<OMSEvent, OMS_RING_SIZE>& reconcile_ring,
                                 const ProductTable& products) {
    if (!channels_valid(mgr))
        return {false, "reconcile", "channels not Valid at reconcile test start"};

    LOG_INFO("test4", "running reconcile (open orders + positions + wallet)...");

    // reconcile only runs when state is Valid
    if (mgr.get_orders_state() == ChannelState::Valid) {
        rest.reconcile_open_orders(&reconcile_ring);
        LOG_INFO("test4", "reconcile_open_orders complete");
    }

    if (mgr.get_positions_state() == ChannelState::Valid) {
        rest.reconcile_open_positions(&reconcile_ring);
        LOG_INFO("test4", "reconcile_open_positions complete");
    }

    if (mgr.get_orders_state()    == ChannelState::Valid &&
        mgr.get_positions_state() == ChannelState::Valid) {
        rest.reconcile_wallet(&reconcile_ring);
        LOG_INFO("test4", "reconcile_wallet complete");
    }

    // give OMS manager a moment to drain the reconcile ring
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // any diffs will have been logged to stderr by handle_order/handle_position/handle_wallet
    LOG_INFO("test4", "post-reconcile state dump:");
    dump_orders(mgr, products);

    return {true, "reconcile", nullptr};
}

// ─── client id helper ─────────────────────────────────────────────────────────

static uint64_t next_client_id() {
    static uint64_t counter = 0;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count())
        + counter++;
}

// ─── test 5: stop orders (SL + TP create, WS echo, cancel) ───────────────────

static TestResult test_stop_orders(DeltaRestClient& rest,
                                   const OrderStateManager& mgr,
                                   const ProductTable& products) {
    if (!channels_valid(mgr))
        return {false, "stop_orders", "channels not Valid at test start"};

    const Product& p = products[products.idfromSymbol(TEST_SYMBOL)];
    SpscRing<OMSEvent, OMS_RING_SIZE> rest_ring;

    // ── CREATE SL sell — triggers if mark drops to 70 (far below ~85, safe) ──
    LOG_INFO("test5", "--- CREATE SL stop order ---");
    ExecutionIntent sl_ci;
    sl_ci.action = ExecutionAction::CreateOrder;
    sl_ci.create_order_intent = {
        .side                      = ExecutionOrderSide::Sell,
        .type                      = ExecutionOrderType::Limit,
        .price                     = 68.0,
        .tick_size                 = p.tick_size,
        .exchange_product_id       = p.exchange_id,
        .size                      = 1,
        .client_order_id           = next_client_id(),
        .reduce_only               = false,
        .post_only                 = false,
        .is_stop_order             = true,
        .stop_order_type           = ExecutionStopOrderType::SL,
        .stop_order_trigger_method = ExecutionStopOrderTriggerMethod::Mark,
        .stop_price                = 70.0,
    };
    rest.create_order(sl_ci, &rest_ring);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    uint64_t sl_id = 0;
    if (auto* ev = rest_ring.pop_begin()) {
        sl_id = ev->order.id;
        LOG_INFO("test5", "REST SL  id=%lu  stop_px=%.4f  limit_px=%.4f",
                 sl_id, ev->order.stop_price, ev->order.limit_price);
        rest_ring.pop_commit();
    } else {
        return {false, "stop_orders", "create SL: no REST response"};
    }

    bool sl_in_oms = wait_until([&]{
        return mgr.debug_state().exchange_id_stop_order_map.count(sl_id) > 0;
    }, 3000);
    if (!sl_in_oms)
        return {false, "stop_orders", "SL not in OMS stop_order_map within 5 s"};
    LOG_INFO("test5", "SL in OMS stop_order_map ✓");

    // ── CREATE TP sell — triggers if mark rises to 110 (far above ~85, safe) ──
    LOG_INFO("test5", "--- CREATE TP stop order ---");
    ExecutionIntent tp_ci;
    tp_ci.action = ExecutionAction::CreateOrder;
    tp_ci.create_order_intent = {
        .side                      = ExecutionOrderSide::Sell,
        .type                      = ExecutionOrderType::Limit,
        .price                     = 112.0,
        .tick_size                 = p.tick_size,
        .exchange_product_id       = p.exchange_id,
        .size                      = 1,
        .client_order_id           = next_client_id(),
        .reduce_only               = false,
        .post_only                 = false,
        .is_stop_order             = true,
        .stop_order_type           = ExecutionStopOrderType::TP,
        .stop_order_trigger_method = ExecutionStopOrderTriggerMethod::Mark,
        .stop_price                = 110.0,
    };
    rest.create_order(tp_ci, &rest_ring);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    uint64_t tp_id = 0;
    if (auto* ev = rest_ring.pop_begin()) {
        tp_id = ev->order.id;
        LOG_INFO("test5", "REST TP  id=%lu  stop_px=%.4f  limit_px=%.4f",
                 tp_id, ev->order.stop_price, ev->order.limit_price);
        rest_ring.pop_commit();
    } else {
        return {false, "stop_orders", "create TP: no REST response"};
    }

    bool tp_in_oms = wait_until([&]{
        return mgr.debug_state().exchange_id_stop_order_map.count(tp_id) > 0;
    }, 3000);
    if (!tp_in_oms)
        return {false, "stop_orders", "TP not in OMS stop_order_map within 5 s"};
    LOG_INFO("test5", "TP in OMS stop_order_map ✓");

    // ── CANCEL both ───────────────────────────────────────────────────────────
    for (auto [label, oid] : {std::pair{"SL", sl_id}, std::pair{"TP", tp_id}}) {
        LOG_INFO("test5", "--- CANCEL %s id=%lu ---", label, oid);
        ExecutionIntent xi;
        xi.action = ExecutionAction::CancelOrder;
        xi.cancel_order_intent = {.order_id = oid, .exchange_product_id = p.exchange_id};
        rest.cancel_order(xi, &rest_ring);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (auto* ev = rest_ring.pop_begin()) {
            LOG_INFO("test5", "REST cancel %s  id=%lu  state=%d", label, ev->order.id, (int)ev->order.state);
            rest_ring.pop_commit();
        }
    }

    bool both_gone = wait_until([&]{
        const OMSState& s = mgr.debug_state();
        return s.exchange_id_stop_order_map.count(sl_id) == 0 &&
               s.exchange_id_stop_order_map.count(tp_id) == 0;
    }, 3000);
    if (!both_gone)
        return {false, "stop_orders", "stop orders not removed from OMS after cancel"};

    LOG_INFO("test5", "SL + TP removed from OMS stop_order_map ✓");
    return {true, "stop_orders", nullptr};
}

// ─── test 6: market order + position ─────────────────────────────────────────

static TestResult test_market_order_position(DeltaRestClient& rest,
                                              const OrderStateManager& mgr,
                                              const ProductTable& products) {
    if (!channels_valid(mgr))
        return {false, "market_order_position", "channels not Valid at test start"};

    const Product& p           = products[products.idfromSymbol(TEST_SYMBOL)];
    const uint8_t  instr_id    = products.idfromSymbol(TEST_SYMBOL);
    SpscRing<OMSEvent, OMS_RING_SIZE> rest_ring;

    const int32_t size_before = mgr.debug_state().positions_[instr_id].size;
    ExecutionOrderSide order_side = size_before < 0 ?  ExecutionOrderSide::Sell : ExecutionOrderSide::Buy;

    LOG_INFO("test6", "position size before market order: %d", size_before);

    LOG_INFO("test6", "--- MARKET BUY 1 %s ---", TEST_SYMBOL);
    ExecutionIntent mi;
    mi.action = ExecutionAction::CreateOrder;
    mi.create_order_intent = {
        .side                = order_side,
        .type                = ExecutionOrderType::Market,
        .price               = 0.0,
        .tick_size           = p.tick_size,
        .exchange_product_id = p.exchange_id,
        .size                = 1,
        .client_order_id     = next_client_id(),
        .reduce_only         = false,
        .post_only           = false,
    };
    rest.create_order(mi, &rest_ring);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    if (auto* ev = rest_ring.pop_begin()) {
        LOG_INFO("test6", "REST market  id=%lu  state=%d  avg_px=%.4f  filled=%u",
                 ev->order.id, (int)ev->order.state,
                 ev->order.average_fill_price, ev->order.filled_size);
        rest_ring.pop_commit();
    } else {
        return {false, "market_order_position", "market buy: no REST response"};
    }

    const int32_t expected_size = size_before < 0 ? size_before - 1 : size_before + 1;

    // Wait for OMS position to reflect the fill (+-1 from before)
    bool pos_updated = wait_until([&]{
        return mgr.debug_state().positions_[instr_id].size == expected_size;
    }, 5000);
    if (!pos_updated) {
        const int32_t actual = mgr.debug_state().positions_[instr_id].size;
        LOG_ERROR("test6", "position size: expected %d (was %d + 1), got %d",
                  expected_size, size_before, actual);
        return {false, "market_order_position", "position did not increase by 1 in OMS within 8 s"};
    }

    const Position& pos = mgr.debug_state().positions_[instr_id];
    LOG_INFO("test6", "OMS position  sym=%s  size=%d (was %d)  instrument_id=%u  entry=%.4f",
             pos.symbol, pos.size, size_before, pos.instrument_id, pos.entry_price);

    if (pos.instrument_id != instr_id)
        return {false, "market_order_position", "position instrument_id mismatch"};

    LOG_INFO("test6", "position verified: size %d → %d (+1)  instrument_id=%u ✓",
             size_before, pos.size, pos.instrument_id);
    return {true, "market_order_position", nullptr};
}

// ─── test 7: cancel_all_orders ────────────────────────────────────────────────

static TestResult test_cancel_all_orders(DeltaRestClient& rest,
                                          const OrderStateManager& mgr,
                                          const ProductTable& products) {
    if (!channels_valid(mgr))
        return {false, "cancel_all_orders", "channels not Valid at test start"};

    const Product& p        = products[products.idfromSymbol(TEST_SYMBOL)];
    const uint8_t  instr_id = products.idfromSymbol(TEST_SYMBOL);
    SpscRing<OMSEvent, OMS_RING_SIZE> rest_ring;

    // ── create 2 limit buy orders far OTM ────────────────────────────────────
    LOG_INFO("test7", "creating 2 limit orders...");
    uint64_t order_ids[2] = {};
    for (int i = 0; i < 2; ++i) {
        ExecutionIntent ci;
        ci.action = ExecutionAction::CreateOrder;
        ci.create_order_intent = {
            .side                = ExecutionOrderSide::Buy,
            .type                = ExecutionOrderType::Limit,
            .price               = 40.0 + i * 5.0,
            .tick_size           = p.tick_size,
            .exchange_product_id = p.exchange_id,
            .size                = 1,
            .client_order_id     = next_client_id(),
            .reduce_only         = false,
            .post_only           = true,
        };
        rest.create_order(ci, &rest_ring);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        if (auto* ev = rest_ring.pop_begin()) {
            order_ids[i] = ev->order.id;
            LOG_INFO("test7", "  created id=%lu  px=%.2f", order_ids[i], ev->order.limit_price);
            rest_ring.pop_commit();
        } else {
            return {false, "cancel_all_orders", "failed to create limit order for test"};
        }
    }

    bool both_visible = wait_until([&]{
        const OMSState& s = mgr.debug_state();
        return s.exchange_id_order_map.count(order_ids[0]) > 0 &&
               s.exchange_id_order_map.count(order_ids[1]) > 0;
    }, 3000);
    if (!both_visible)
        return {false, "cancel_all_orders", "limit orders did not appear in OMS before cancel_all"};

    const int32_t pos_before = mgr.debug_state().positions_[instr_id].size;
    LOG_INFO("test7", "position size before cancel_all: %d", pos_before);

    // ── cancel all limit orders (not stop orders) ─────────────────────────────
    LOG_INFO("test7", "calling cancel_all_orders (limit only)...");
    ExecutionIntent xa;
    xa.action = ExecutionAction::CancelAllOrders;
    xa.cancel_all_order_intent = {
        .cancel_limit_orders = true,
        .cancel_stop_orders  = false,
        .exchange_product_id = p.exchange_id,
    };
    rest.cancel_all_orders(xa, &rest_ring);

    bool orders_gone = wait_until([&]{
        const OMSState& s = mgr.debug_state();
        return s.exchange_id_order_map.count(order_ids[0]) == 0 &&
               s.exchange_id_order_map.count(order_ids[1]) == 0;
    }, 8000);
    if (!orders_gone)
        return {false, "cancel_all_orders", "orders not cleared from OMS after cancel_all"};
    LOG_INFO("test7", "orders cleared from OMS ✓");

    const int32_t pos_after = mgr.debug_state().positions_[instr_id].size;
    if (pos_after != pos_before) {
        LOG_ERROR("test7", "position changed %d -> %d — cancel_all_orders must NOT touch positions",
                  pos_before, pos_after);
        return {false, "cancel_all_orders", "cancel_all_orders changed position size"};
    }
    LOG_INFO("test7", "position unchanged (size=%d) ✓", pos_after);
    return {true, "cancel_all_orders", nullptr};
}

// ─── test 8: close_all_positions ─────────────────────────────────────────────

static TestResult test_close_all_positions(DeltaRestClient& rest,
                                            const OrderStateManager& mgr,
                                            const ProductTable& products) {
    if (!channels_valid(mgr))
        return {false, "close_all_positions", "channels not Valid at test start"};

    const Product& p        = products[products.idfromSymbol(TEST_SYMBOL)];
    const uint8_t  instr_id = products.idfromSymbol(TEST_SYMBOL);
    SpscRing<OMSEvent, OMS_RING_SIZE> rest_ring;

    if (mgr.debug_state().positions_[instr_id].size == 0)
        return {false, "close_all_positions", "no open position — test 6 must pass first"};

    // ── create a limit order that close_all should also cancel ────────────────
    LOG_INFO("test8", "creating limit order before close_all_positions...");
    ExecutionIntent ci;
    ci.action = ExecutionAction::CreateOrder;
    ci.create_order_intent = {
        .side                = ExecutionOrderSide::Buy,
        .type                = ExecutionOrderType::Limit,
        .price               = 40.0,
        .tick_size           = p.tick_size,
        .exchange_product_id = p.exchange_id,
        .size                = 1,
        .client_order_id     = next_client_id(),
        .reduce_only         = false,
        .post_only           = true,
    };
    rest.create_order(ci, &rest_ring);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    uint64_t limit_id = 0;
    if (auto* ev = rest_ring.pop_begin()) {
        limit_id = ev->order.id;
        LOG_INFO("test8", "limit order created  id=%lu", limit_id);
        rest_ring.pop_commit();
    } else {
        return {false, "close_all_positions", "failed to create limit order before close_all"};
    }

    wait_until([&]{
        return mgr.debug_state().exchange_id_order_map.count(limit_id) > 0;
    }, 3000);

    // ── close all positions ───────────────────────────────────────────────────
    LOG_INFO("test8", "calling close_all_positions...");
    ExecutionIntent cai;
    cai.action = ExecutionAction::CloseAllPositions;
    cai.close_all_positions_intent = {
        .close_all_isolated  = true,
        .close_all_cross     = true,
        .close_all_portfolio = true,
    };
    rest.close_all_positions(cai, &rest_ring);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // ── verify position cleared ───────────────────────────────────────────────
    bool pos_cleared = wait_until([&]{
        return mgr.debug_state().positions_[instr_id].size == 0;
    }, 6000);
    if (!pos_cleared)
        return {false, "close_all_positions", "position not cleared from OMS within 10 s"};
    LOG_INFO("test8", "position cleared from OMS ✓");

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // ── verify open orders also cleared (close_all cancels orders too) ────────
    bool orders_cleared = wait_until([&]{
        return mgr.debug_state().exchange_id_order_map.empty() && mgr.debug_state().exchange_id_stop_order_map.empty();
    }, 3000);
    if (!orders_cleared) {
        const OMSState& s = mgr.debug_state();
        LOG_WARN("test8", "%zu order(s) still in OMS after close_all_positions:", s.exchange_id_order_map.size());
        LOG_WARN("test8", "%zu stop order(s) still in OMS after close_all_positions:", s.exchange_id_stop_order_map.size());
        for (auto& [id, o] : s.exchange_id_order_map) {
            const char* state_str  = o->state == OrderState::Open      ? "open"
                                   : o->state == OrderState::Closed    ? "closed"
                                   : o->state == OrderState::Cancelled ? "cancelled" : "pending";
            const char* side_str   = o->side  == OrderSide::Buy        ? "buy"   : "sell";
            const char* type_str   = o->order_type == OrderType::Market ? "market" : "limit";
            LOG_WARN("test8", "  id=%-12lu  cid=%-18lu  sym=%-8s  side=%s  type=%s  size=%-4u  "
                              "limit_px=%.4f  stop_px=%.4f  avg_px=%.4f  state=%s  "
                              "filled=%-3u  unfilled=%-3u",
                     o->id, o->client_order_id, o->symbol, side_str, type_str, o->size,
                     o->limit_price, o->stop_price, o->average_fill_price, state_str,
                     o->filled_size, o->unfilled_size);
        }
        return {false, "close_all_positions", "open orders not cleared after close_all_positions"};
    }
    LOG_INFO("test8", "all orders cleared from OMS ✓");
    return {true, "close_all_positions", nullptr};
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // ── 0. load .env + resolve config ─────────────────────────────────────────
    env::load_file(".env");
    const std::string rest_host    = env::get_or("DELTA_REST_HOST",       cfg::delta_exchange::testnet::REST_HOST);
    const std::string ws_priv_host = env::get_or("DELTA_WS_PRIVATE_HOST", cfg::delta_exchange::testnet::WS_HOST_PRIVATE);
    const std::string api_key      = env::require("DELTA_API_KEY");
    const std::string api_secret   = env::require("DELTA_API_SECRET");

    // ── 1. fetch products ─────────────────────────────────────────────────────
    LOG_INFO("init", "fetching %s from %s", TEST_SYMBOL, rest_host.c_str());
    ProductTable products;
    {
        DeltaRestClient prod_rest(rest_host.c_str(), "", "", ProductTable{});
        prod_rest.connect();
        try {
            products.add(fetch_product(prod_rest, TEST_SYMBOL));
            const Product& p = products[0];
            LOG_INFO("init", "  %s  exchange_id=%-6u  tick=%.6f  contract=%.2f",
                     p.symbol, p.exchange_id, p.tick_size, p.contract_value);
        } catch (const std::exception& e) {
            LOG_ERROR("init", "failed: %s", e.what());
            return 1;
        }
    }

    // ── 2. rings (heap — SpscRing<OMSEvent,256> is large) ────────────────────
    auto oms_ws_ring        = std::make_unique<SpscRing<OMSEvent, cfg::OMS_RING_SIZE>>();
    auto oms_rest_ring      = std::make_unique<SpscRing<OMSEvent, cfg::OMS_RING_SIZE>>();
    auto oms_reconcile_ring = std::make_unique<SpscRing<OMSEvent, cfg::OMS_RING_SIZE>>();

    // ── 3. WS client + OMS manager (heap — OrderStateManager is ~2 MB) ───────
    LOG_INFO("init", "connecting OMS WS to %s", ws_priv_host.c_str());
    auto ws = std::make_unique<DeltaOMSWebsocketClient>(
        ws_priv_host.c_str(), 443, "/",
        api_key.c_str(), api_secret.c_str(),
        products, oms_ws_ring.get());
    g_ws = ws.get();

    auto mgr = std::make_unique<OrderStateManager>(
        oms_ws_ring.get(), oms_rest_ring.get(), oms_reconcile_ring.get(), products);

    fprintf(stderr, "sizeof OrderStateManager = %zu\n", sizeof(*mgr));

    // ── 4. threads ────────────────────────────────────────────────────────────
    std::thread ws_thread([&]{ ws->start(); });
    std::thread oms_thread([&]{ mgr->run(g_running); });

    // ── 5. REST client for CRUD + reconcile ───────────────────────────────────
    DeltaRestClient test_rest(rest_host.c_str(), api_key.c_str(), api_secret.c_str(), products);
    test_rest.connect();

    // ── 6. run tests ──────────────────────────────────────────────────────────
    TestResult results[8];
    
    results[0] = test_channel_snapshot(*mgr);
    print_result(results[0]);

    if (results[0].passed && g_running.load()) {
        dump_orders(*mgr, products);
        results[1] = test_crud_ws_echo(test_rest, *mgr, products);
        print_result(results[1]);
    } else {
        results[1] = {false, "crud_ws_echo", "skipped — channel_snapshot failed"};
    }

    // brief pause before reconnect test so WS events settle
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (g_running.load()) {
        results[2] = test_reconnect(*ws, *mgr);
        print_result(results[2]);
    } else {
        results[2] = {false, "reconnect", "skipped — interrupted"};
    }

    if (results[2].passed && g_running.load()) {
        results[3] = test_reconcile(test_rest, *mgr, *oms_reconcile_ring, products);
        print_result(results[3]);
    } else {
        results[3] = {false, "reconcile", "skipped — reconnect test failed or interrupted"};
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (results[0].passed && g_running.load()) {
        results[4] = test_stop_orders(test_rest, *mgr, products);
        print_result(results[4]);
    } else {
        results[4] = {false, "stop_orders", "skipped — channel_snapshot failed"};
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (results[4].passed && g_running.load()) {
        results[5] = test_market_order_position(test_rest, *mgr, products);
        print_result(results[5]);
    } else {
        results[5] = {false, "market_order_position", "skipped — stop_orders failed"};
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (results[5].passed && g_running.load()) {
        results[6] = test_cancel_all_orders(test_rest, *mgr, products);
        print_result(results[6]);
    } else {
        results[6] = {false, "cancel_all_orders", "skipped — market_order_position failed"};
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (results[5].passed && g_running.load()) {
        results[7] = test_close_all_positions(test_rest, *mgr, products);
        print_result(results[7]);
    } else {
        results[7] = {false, "close_all_positions", "skipped — market_order_position failed"};
    }

    // ── 7. summary ────────────────────────────────────────────────────────────
    LOG_INFO("summary", "─────────────────────────────────────────");
    int passed = 0;
    for (const auto& r : results) {
        print_result(r);
        if (r.passed) ++passed;
    }
    LOG_INFO("summary", "%d / 8 tests passed", passed);

    // ── 8. shutdown ───────────────────────────────────────────────────────────
    g_running.store(false, std::memory_order_relaxed);
    ws->shutdown();
    if (ws_thread.joinable())  ws_thread.join();
    if (oms_thread.joinable()) oms_thread.join();
    LOG_INFO("shutdown", "done");
    return (passed == 8) ? 0 : 1;
};
