#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/api/order/order.hpp"
#include "delta_exchange/api/position/position.hpp"
#include "delta_exchange/api/wallet/wallet.hpp"
#include "core/logger.hpp"
#include <chrono>
#include <openssl/hmac.h>
#include <openssl/evp.h>

DeltaRestClient::DeltaRestClient(const char* host, const char* api_key, const char* api_secret,
                                 const ProductTable& products)
    : TcpClient(host)
    , api_key_(api_key)
    , api_secret_(api_secret)
    , products_(products) {}

// ─── execution methods ────────────────────────────────────────────────────────

static uint64_t now_us() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

void DeltaRestClient::create_order(const ExecutionIntent& intent, SpscRing<OMSEvent, 256>* ring) {
    auto req      = CreateOrderRequest::from_intent(intent.create_order_intent);
    auto body     = req.serialize();
    LOG_INFO("rest", "create_order body: %s", body.c_str());
    uint64_t sent = now_us();
    auto result   = place_order_http(body);
    if (!result) { LOG_ERROR("rest", "create_order: HTTP failed"); return; }
    LOG_INFO("rest", "create_order response [%d]: %s", result->status, result->body.c_str());

    OMSEvent* slot = ring->push_begin();
    if (!slot) { LOG_ERROR("rest", "create_order: ring full"); return; }
    if (!CreateOrderRequest::deserialize(result->body, *slot, products_)) {
        LOG_ERROR("rest", "create_order: deserialize failed");
        return;
    }
    slot->order.timestamp = sent;
    ring->push_commit();
}

void DeltaRestClient::edit_order(const ExecutionIntent& intent, SpscRing<OMSEvent, 256>* ring) {
    auto req      = EditOrderRequest::from_intent(intent.edit_order_intent);
    auto body     = req.serialize();
    LOG_INFO("rest", "edit_order body: %s", body.c_str());
    uint64_t sent = now_us();
    auto result   = edit_order_http(body);
    if (!result) { LOG_ERROR("rest", "edit_order: HTTP failed"); return; }
    LOG_INFO("rest", "edit_order response [%d]: %s", result->status, result->body.c_str());

    OMSEvent* slot = ring->push_begin();
    if (!slot) { LOG_ERROR("rest", "edit_order: ring full"); return; }
    if (!EditOrderRequest::deserialize(result->body, *slot, products_)) {
        LOG_ERROR("rest", "edit_order: deserialize failed");
        return;
    }
    slot->order.timestamp = sent;
    ring->push_commit();
}

void DeltaRestClient::cancel_order(const ExecutionIntent& intent, SpscRing<OMSEvent, 256>* ring) {
    auto req      = CancelOrderRequest::from_intent(intent.cancel_order_intent);
    auto body     = req.serialize();
    LOG_INFO("rest", "cancel_order body: %s", body.c_str());
    uint64_t sent = now_us();
    auto result   = cancel_order_http(body);
    if (!result) { LOG_ERROR("rest", "cancel_order: HTTP failed"); return; }
    LOG_INFO("rest", "cancel_order response [%d]: %s", result->status, result->body.c_str());

    OMSEvent* slot = ring->push_begin();
    if (!slot) { LOG_ERROR("rest", "cancel_order: ring full"); return; }
    if (!CancelOrderRequest::deserialize(result->body, *slot, products_)) {
        LOG_ERROR("rest", "cancel_order: deserialize failed");
        return;
    }
    slot->order.timestamp = sent;
    ring->push_commit();
}

void DeltaRestClient::cancel_all_orders(const ExecutionIntent& intent, SpscRing<OMSEvent, 256>* ring) {
    auto req    = CancelAllOrderRequest::from_intent(intent.cancel_all_order_intent);
    auto body   = req.serialize();
    auto result = cancel_all_orders_http(body);
    if (!result) return;
    // response has no order data — WS will deliver individual delete events for each cancelled order
    (void)CancelAllOrderRequest::parse_success(result->body);
}

void DeltaRestClient::close_all_positions(const ExecutionIntent& intent, SpscRing<OMSEvent, 256>* ring) {
    auto req    = CloseAllPositionsRequest::from_intent(intent.close_all_positions_intent);
    auto body   = req.serialize();
    LOG_INFO("rest", "close_all_positions body: %s", body.c_str());
    auto result = close_all_positions_http(body);
    if (!result) { LOG_ERROR("rest", "close_all_positions: HTTP failed"); return; }
    LOG_INFO("rest", "close_all_positions response [%d]: %s", result->status, result->body.c_str());
    (void)CloseAllPositionsRequest::parse_success(result->body);
}

void DeltaRestClient::reconcile_open_orders(SpscRing<OMSEvent, 256>* ring) {
    static constexpr int PAGE_SIZE = 30;
    OMSEvent page_buf[PAGE_SIZE];

    for (const char* states : {"open", "pending"}) {
        for (int page = 1; ; ++page) {
            GetOpenOrdersRequest req{states, page, PAGE_SIZE};
            auto result = get_orders(req.query());
            if (!result || result->status != 200) break;

            bool has_more = false;
            int count = GetOpenOrdersRequest::deserialize_page(
                result->body, page_buf, PAGE_SIZE, has_more, page, PAGE_SIZE, products_);

            uint64_t ts = now_us();
            for (int i = 0; i < count; ++i) {
                OMSEvent* slot = ring->push_begin();
                if (!slot) break;
                *slot = page_buf[i];
                slot->order.timestamp = ts;
                ring->push_commit();
            }

            if (!has_more) break;
        }
    }
}

void DeltaRestClient::reconcile_open_positions(SpscRing<OMSEvent, 256>* ring) {
    static constexpr int MAX_POSITIONS = ProductTable::MAX_INSTRUMENTS;
    OMSEvent buf[MAX_POSITIONS];

    auto result = get_positions();
    if (!result || result->status != 200) return;

    int count = GetOpenPositionsRequest::deserialize_all(result->body, buf, MAX_POSITIONS, products_);

    uint64_t ts = now_us();
    for (int i = 0; i < count; ++i) {
        OMSEvent* slot = ring->push_begin();
        if (!slot) break;
        *slot = buf[i];
        slot->position.timestamp = ts;
        ring->push_commit();
    }
}

void DeltaRestClient::reconcile_wallet(SpscRing<OMSEvent, 256>* ring) {
    auto result = get_wallet();
    if (!result || result->status != 200) return;

    OMSEvent* slot = ring->push_begin();
    if (!slot) return;
    if (!GetWalletRequest::deserialize(result->body, *slot)) return;
    slot->wallet.timestamp = now_us();
    ring->push_commit();
}

// ─── raw HTTP (private) ───────────────────────────────────────────────────────

httplib::Result DeltaRestClient::place_order_http(const std::string& body) {
    return post("/v2/orders", body, auth_headers("POST", "/v2/orders", "", body));
}

httplib::Result DeltaRestClient::cancel_order_http(const std::string& body) {
    return del("/v2/orders", body, auth_headers("DELETE", "/v2/orders", "", body));
}

httplib::Result DeltaRestClient::cancel_all_orders_http(const std::string& body) {
    return del("/v2/orders/all", body, auth_headers("DELETE", "/v2/orders/all", "", body));
}

httplib::Result DeltaRestClient::close_all_positions_http(const std::string& body) {
    return post("/v2/positions/close_all", body, auth_headers("POST", "/v2/positions/close_all", "", body));
}

httplib::Result DeltaRestClient::edit_order_http(const std::string& body) {
    return put("/v2/orders", body, auth_headers("PUT", "/v2/orders", "", body));
}

// ─── reconcile / init fetches ─────────────────────────────────────────────────

httplib::Result DeltaRestClient::get_products(const std::string& query) {
    return get("/v2/products", query, auth_headers("GET", "/v2/products", query, ""));
}

httplib::Result DeltaRestClient::get_product(const std::string& symbol) {
    std::string path = "/v2/products/" + symbol;
    return get(path, "", {{"User-Agent", "trading-bot/1.0"}});
}

httplib::Result DeltaRestClient::get_orders(const std::string& query) {
    return get("/v2/orders", query, auth_headers("GET", "/v2/orders", query, ""));
}

httplib::Result DeltaRestClient::get_positions(const std::string& query) {
    return get("/v2/positions/margined", query, auth_headers("GET", "/v2/positions/margined", query, ""));
}

httplib::Result DeltaRestClient::get_wallet(const std::string& query) {
    return get("/v2/wallet/balances", query, auth_headers("GET", "/v2/wallet/balances", query, ""));
}

// ─── auth ─────────────────────────────────────────────────────────────────────

httplib::Headers DeltaRestClient::auth_headers(const std::string& method,
                                               const std::string& path,
                                               const std::string& query,
                                               const std::string& body) {
    std::string ts        = std::to_string(now_s());
    std::string signature = sign(method, ts, path, query, body);
    return {
        {"api-key",    api_key_},
        {"signature",  signature},
        {"timestamp",  ts},
        {"User-Agent", "trading-bot/1.0"},
    };
}

std::string DeltaRestClient::sign(const std::string& method,
                                  const std::string& ts,
                                  const std::string& path,
                                  const std::string& query,
                                  const std::string& body) {
    std::string msg = method + ts + path + query + body;
    unsigned char digest[32];
    unsigned int  len = 32;
    HMAC(EVP_sha256(),
         api_secret_.data(), api_secret_.size(),
         reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
         digest, &len);
    return to_hex(digest, len);
}

std::string DeltaRestClient::to_hex(const unsigned char* data, size_t len) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        out += hex[data[i] >> 4];
        out += hex[data[i] & 0xf];
    }
    return out;
}

int64_t DeltaRestClient::now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int64_t DeltaRestClient::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
