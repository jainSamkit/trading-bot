#pragma once
#include "delta_exchange/models/order.hpp"
#include "delta_exchange/models/position.hpp"
#include "delta_exchange/models/wallet.hpp"
#include "oms/oms_manager.hpp"
#include <cstdio>

// Formatting helpers for the OMS audit log.
// All functions write into caller-supplied buffers; no heap allocation.
// Included only from oms_manager.cpp.

namespace oms_audit {

// ── enum → string ─────────────────────────────────────────────────────────────

static inline const char* src_str(OMSEventSource s) {
    switch (s) {
        case OMSEventSource::Websocket: return "WS";
        case OMSEventSource::Rest:      return "REST";
        case OMSEventSource::Reconcile: return "REC";
    }
    return "?";
}

static inline const char* side_str(OrderSide s) {
    return s == OrderSide::Buy ? "buy" : "sell";
}

static inline const char* order_type_str(OrderType t) {
    return t == OrderType::Market ? "market" : "limit";
}

static inline const char* tif_str(TimeInForce t) {
    return t == TimeInForce::GTC ? "gtc" : "ioc";
}

static inline const char* state_str(OrderState s) {
    switch (s) {
        case OrderState::Open:      return "open";
        case OrderState::Pending:   return "pending";
        case OrderState::Closed:    return "closed";
        case OrderState::Cancelled: return "cancelled";
    }
    return "?";
}

static inline const char* reason_str(OrderReason r) {
    switch (r) {
        case OrderReason::None:        return "none";
        case OrderReason::Fill:        return "fill";
        case OrderReason::StopCreate:  return "stop_create";
        case OrderReason::StopUpdate:  return "stop_update";
        case OrderReason::StopTrigger: return "stop_trigger";
        case OrderReason::StopCancel:  return "stop_cancel";
        case OrderReason::Liquidation: return "liquidation";
        case OrderReason::SelfTrade:   return "self_trade";
    }
    return "?";
}

static inline const char* stop_type_str(StopOrderType t) {
    switch (t) {
        case StopOrderType::None: return "none";
        case StopOrderType::TP:   return "tp";
        case StopOrderType::SL:   return "sl";
    }
    return "?";
}

static inline const char* trigger_str(StopTriggerMethod m) {
    switch (m) {
        case StopTriggerMethod::None: return "none";
        case StopTriggerMethod::Mark: return "mark";
        case StopTriggerMethod::LTP:  return "ltp";
        case StopTriggerMethod::Spot: return "spot";
    }
    return "?";
}

static inline const char* margin_mode_str(MarginMode m) {
    return m == MarginMode::Cross ? "cross" : "isolated";
}

static inline const char* signal_str(OMSSignal s) {
    switch (s) {
        case OMSSignal::OrdersInvalid:            return "OrdersInvalid";
        case OMSSignal::PositionsInvalid:          return "PositionsInvalid";
        case OMSSignal::WalletInvalid:             return "WalletInvalid";
        case OMSSignal::OrdersSnapshotComplete:    return "OrdersSnapshotComplete";
        case OMSSignal::PositionsSnapshotComplete: return "PositionsSnapshotComplete";
        case OMSSignal::WalletSnapshotComplete:    return "WalletSnapshotComplete";
    }
    return "?";
}

// ── buffer append helper ──────────────────────────────────────────────────────

// Advances `w` and decrements `rem` after each snprintf so callers can build
// a log line incrementally without recomputing the offset each time.
// When the buffer is full, subsequent calls are no-ops (rem stays <= 0).
#define OA_APP(out, w, rem, ...) do {                                   \
    if ((rem) > 0) {                                                    \
        int _r = snprintf((out)+(w), (size_t)(rem), __VA_ARGS__);      \
        if (_r > 0) { int _s = _r < (rem) ? _r : (rem)-1;             \
                      (w) += _s; (rem) -= _s; }                        \
    }                                                                   \
} while(0)

// ── order formatters ──────────────────────────────────────────────────────────

// Full order CREATE/DELETE line — all fields except fill_id.
static inline void fmt_order(char* out, size_t cap,
                              const char* src, const char* action, const char* kind,
                              const Order& o) {
    snprintf(out, cap,
        "src=%-4s type=%-5s action=%-6s  "
        "id=%-12lu cid=%-18lu sym=%-8s  "
        "side=%s ord_type=%s tif=%s  "
        "size=%-4u filled=%-4u unfilled=%-4u  "
        "limit_px=%.6f stop_px=%.6f avg_px=%.6f  "
        "stop_type=%s trigger=%s  "
        "state=%s reason=%s  "
        "reduce_only=%d commission=%.6f  ts=%lu",
        src, kind, action,
        o.id, o.client_order_id, o.symbol,
        side_str(o.side), order_type_str(o.order_type), tif_str(o.time_in_force),
        o.size, o.filled_size, o.unfilled_size,
        o.limit_price, o.stop_price, o.average_fill_price,
        stop_type_str(o.stop_order_type), trigger_str(o.stop_trigger_method),
        state_str(o.state), reason_str(o.reason),
        (int)o.reduce_only, o.paid_commission, o.timestamp);
}

// Order UPDATE diff line.
// Always includes: side, state, size/filled/unfilled, all three prices, ts.
// Shows old->new for each field that changed.
static inline void fmt_order_diff(char* out, size_t cap,
                                   const char* src, const char* kind,
                                   const Order& o, const Order& n) {
    int w = 0, rem = (int)cap;

    OA_APP(out, w, rem,
        "src=%-4s type=%-5s action=UPDATE   "
        "id=%-12lu cid=%-18lu sym=%-8s  side=%s  ",
        src, kind, n.id, n.client_order_id, n.symbol, side_str(n.side));

#define DIFU(lbl, fld) \
    if (o.fld != n.fld) OA_APP(out, w, rem, lbl "=%u->%u  ", o.fld, n.fld); \
    else                OA_APP(out, w, rem, lbl "=%u  ",      n.fld)
#define DIFD(lbl, fld) \
    if (o.fld != n.fld) OA_APP(out, w, rem, lbl "=%.6f->%.6f  ", o.fld, n.fld); \
    else                OA_APP(out, w, rem, lbl "=%.6f  ",         n.fld)
#define DIFUL(lbl, fld) \
    if (o.fld != n.fld) OA_APP(out, w, rem, lbl "=%lu->%lu", o.fld, n.fld); \
    else                OA_APP(out, w, rem, lbl "=%lu",        n.fld)

    DIFU("size",     size);
    DIFU("filled",   filled_size);
    DIFU("unfilled", unfilled_size);
    DIFD("limit_px", limit_price);
    DIFD("stop_px",  stop_price);
    DIFD("avg_px",   average_fill_price);

    if (o.state != n.state)
        OA_APP(out, w, rem, "state=%s->%s  ", state_str(o.state), state_str(n.state));
    else
        OA_APP(out, w, rem, "state=%s  ", state_str(n.state));

    DIFUL("ts", timestamp);

#undef DIFU
#undef DIFD
#undef DIFUL
}

// ── position formatters ───────────────────────────────────────────────────────

// Full position line (new position or snapshot entry).
static inline void fmt_position(char* out, size_t cap,
                                 const char* src, const char* action,
                                 const Position& p) {
    snprintf(out, cap,
        "src=%-4s type=POS    action=%-6s  "
        "sym=%-8s  size=%d  "
        "entry_px=%.6f margin=%.6f liq_px=%.6f bankrupt_px=%.6f  "
        "commission=%.6f rpnl=%.6f rcashflow=%.6f rfunding=%.6f  "
        "mode=%s auto_topup=%d under_liq=%d  ts=%lu",
        src, action,
        p.symbol, p.size,
        p.entry_price, p.margin, p.liquidation_price, p.bankruptcy_price,
        p.commission, p.realized_pnl, p.realized_cashflow, p.realized_funding,
        margin_mode_str(p.margin_mode), (int)p.auto_topup, (int)p.under_liquidation,
        p.timestamp);
}

// Position UPDATE diff line.
// Always includes: sym, mode, size, entry_px, margin, liq_px, rpnl, ts.
static inline void fmt_position_diff(char* out, size_t cap,
                                      const char* src,
                                      const Position& o, const Position& n) {
    int w = 0, rem = (int)cap;

    OA_APP(out, w, rem,
        "src=%-4s type=POS    action=UPDATE   sym=%-8s mode=%s  ",
        src, n.symbol, margin_mode_str(n.margin_mode));

    if (o.size != n.size) OA_APP(out, w, rem, "size=%d->%d  ", o.size, n.size);
    else                   OA_APP(out, w, rem, "size=%d  ", n.size);

#define DIFD(lbl, fld) \
    if (o.fld != n.fld) OA_APP(out, w, rem, lbl "=%.6f->%.6f  ", o.fld, n.fld); \
    else                OA_APP(out, w, rem, lbl "=%.6f  ",         n.fld)
#define DIFUL(lbl, fld) \
    if (o.fld != n.fld) OA_APP(out, w, rem, lbl "=%lu->%lu", o.fld, n.fld); \
    else                OA_APP(out, w, rem, lbl "=%lu",        n.fld)

    DIFD("entry_px", entry_price);
    DIFD("margin",   margin);
    DIFD("liq_px",   liquidation_price);
    DIFD("rpnl",     realized_pnl);
    DIFUL("ts",      timestamp);

#undef DIFD
#undef DIFUL
}

// ── wallet formatter ──────────────────────────────────────────────────────────

static inline void fmt_wallet(char* out, size_t cap,
                               const char* src, const Wallet& w) {
    snprintf(out, cap,
        "src=%-4s type=WALLET action=UPDATE   "
        "sym=%-6s  balance=%.6f available=%.6f blocked=%.6f  "
        "order_margin=%.6f pos_margin=%.6f commission=%.6f  ts=%lu",
        src,
        w.asset_symbol, w.balance, w.available_balance, w.blocked_margin,
        w.order_margin, w.position_margin, w.commission,
        w.timestamp);
}

#undef OA_APP

} // namespace oms_audit
