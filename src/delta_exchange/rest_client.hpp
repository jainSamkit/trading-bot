#pragma once
#include "transport/http_client.hpp"
#include "models/order.hpp"
#include "models/position.hpp"
#include "models/product.hpp"
#include <chrono>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <string>

class DeltaRestClient : public TcpClient {
public:
    DeltaRestClient(const char* host, const char* api_key, const char* api_secret)
        : TcpClient(host),
          api_key_(api_key),
          api_secret_(api_secret) {}

    
    // products
    httplib::Result get_products(const std::string& query = "") {
        return get("/v2/products", query, auth_headers("GET", "/v2/products", query, ""));
    }

    // public endpoint — no auth needed
    httplib::Result get_product(const std::string& symbol) {
        std::string path = "/v2/products/" + symbol;
        return get(path, "", {{"User-Agent", "trading-bot/1.0"}});
    }

    // orders
    httplib::Result place_order(const std::string& body) {
        return post("/v2/orders", body, auth_headers("POST", "/v2/orders", "", body));
    }

    httplib::Result cancel_order(int64_t order_id) {
        std::string path = "/v2/orders/" + std::to_string(order_id);
        return del(path, "", auth_headers("DELETE", path, "", ""));
    }

    httplib::Result get_order(int64_t order_id) {
        std::string path = "/v2/orders/" + std::to_string(order_id);
        return get(path, "", auth_headers("GET", path, "", ""));
    }

    httplib::Result get_orders(const std::string& query = "") {
        return get("/v2/orders", query, auth_headers("GET", "/v2/orders", query, ""));
    }

    // positions
    httplib::Result get_positions(const std::string& query = "") {
        return get("/v2/positions", query, auth_headers("GET", "/v2/positions", query, ""));
    }

    // wallet
    httplib::Result get_wallet(const std::string& query = "") {
        return get("/v2/wallet/balances", query, auth_headers("GET", "/v2/wallet/balances", query, ""));
    }

private:
    // prehash: method + timestamp + requestPath + query_params + body
    httplib::Headers auth_headers(const std::string& method,
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

    std::string sign(const std::string& method,
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

    static std::string to_hex(const unsigned char* data, size_t len) {
        static constexpr char hex[] = "0123456789abcdef";
        std::string out;
        out.reserve(len * 2);
        for (size_t i = 0; i < len; ++i) {
            out += hex[data[i] >> 4];
            out += hex[data[i] & 0xf];
        }
        return out;
    }

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    std::string api_key_;
    std::string api_secret_;
};
