#include "delta_exchange/rest_client.hpp"
#include "delta_exchange/api/order/order.hpp"
#include "delta_exchange/api/position/position.hpp"
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
    uint64_t sent = now_us();
    auto result   = place_order_http(body);
    if (!result) return;

    OMSEvent* slot = ring->push_begin();
    if (!slot) return;
    if (!CreateOrderRequest::deserialize(result->body, *slot, products_)) return;
    slot->order.timestamp = sent;
    ring->push_commit();
}

void DeltaRestClient::edit_order(const ExecutionIntent& intent, SpscRing<OMSEvent, 256>* ring) {
    auto req      = EditOrderRequest::from_intent(intent.edit_order_intent);
    auto body     = req.serialize();
    uint64_t sent = now_us();
    auto result   = edit_order_http(static_cast<int64_t>(req.order_id), body);
    if (!result) return;

    OMSEvent* slot = ring->push_begin();
    if (!slot) return;
    if (!EditOrderRequest::deserialize(result->body, *slot, products_)) return;
    slot->order.timestamp = sent;
    ring->push_commit();
}

void DeltaRestClient::cancel_order(const ExecutionIntent& intent, SpscRing<OMSEvent, 256>* ring) {
    auto req      = CancelOrderRequest::from_intent(intent.cancel_order_intent);
    auto body     = req.serialize();
    uint64_t sent = now_us();
    auto result   = cancel_order_http(static_cast<int64_t>(req.order_id), body);
    if (!result) return;

    OMSEvent* slot = ring->push_begin();
    if (!slot) return;
    if (!CancelOrderRequest::deserialize(result->body, *slot, products_)) return;
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
    auto result = close_all_positions_http(body);
    if (!result) return;
    // response has no position data — WS will deliver individual position updates
    (void)CloseAllPositionsRequest::parse_success(result->body);
}

// ─── raw HTTP (private) ───────────────────────────────────────────────────────

httplib::Result DeltaRestClient::place_order_http(const std::string& body) {
    return post("/v2/orders", body, auth_headers("POST", "/v2/orders", "", body));
}

httplib::Result DeltaRestClient::cancel_order_http(int64_t order_id, const std::string& body) {
    std::string path = "/v2/orders/" + std::to_string(order_id);
    return del(path, body, auth_headers("DELETE", path, "", body));
}

httplib::Result DeltaRestClient::cancel_all_orders_http(const std::string& body) {
    return del("/v2/orders/all", body, auth_headers("DELETE", "/v2/orders/all", "", body));
}

httplib::Result DeltaRestClient::close_all_positions_http(const std::string& body) {
    return del("/v2/positions/close_all", body, auth_headers("DELETE", "/v2/positions/close_all", "", body));
}

httplib::Result DeltaRestClient::edit_order_http(int64_t order_id, const std::string& body) {
    std::string path = "/v2/orders/" + std::to_string(order_id);
    return put(path, body, auth_headers("PUT", path, "", body));
}

// ─── reconcile / init fetches ─────────────────────────────────────────────────

httplib::Result DeltaRestClient::get_products(const std::string& query) {
    return get("/v2/products", query, auth_headers("GET", "/v2/products", query, ""));
}

httplib::Result DeltaRestClient::get_product(const std::string& symbol) {
    std::string path = "/v2/products/" + symbol;
    return get(path, "", {{"User-Agent", "trading-bot/1.0"}});
}

httplib::Result DeltaRestClient::get_order(int64_t order_id) {
    std::string path = "/v2/orders/" + std::to_string(order_id);
    return get(path, "", auth_headers("GET", path, "", ""));
}

httplib::Result DeltaRestClient::get_open_orders_http(const std::string& query) {
    return get("/v2/orders", query, auth_headers("GET", "/v2/orders", query, ""));
}

httplib::Result DeltaRestClient::get_open_positions_http(const std::string& query) {
    return get("/v2/positions", query, auth_headers("GET", "/v2/positions", query, ""));
}

httplib::Result DeltaRestClient::get_wallet_http(const std::string& query) {
    return get("/v2/wallet/balances", query, auth_headers("GET", "/v2/wallet/balances", query, ""));
}

// ─── auth ─────────────────────────────────────────────────────────────────────

httplib::Headers DeltaRestClient::auth_headers(const std::string& method,
                                               const std::string& path,
                                               const std::string& query,
                                               const std::string& body) {
    std::string ts        = std::to_string(now_ms());
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

int64_t DeltaRestClient::now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
