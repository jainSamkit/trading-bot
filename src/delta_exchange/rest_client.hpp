#pragma once
#include "transport/http_client.hpp"
#include "delta_exchange/models/order.hpp"
#include "delta_exchange/models/position.hpp"
#include "delta_exchange/models/product.hpp"
#include <string>

class DeltaRestClient : public TcpClient {
public:
    DeltaRestClient(const char* host, const char* api_key, const char* api_secret);

    // products
    httplib::Result get_products(const std::string& query = "");

    // public endpoint — no auth needed
    httplib::Result get_product(const std::string& symbol);

    // orders
    httplib::Result place_order(const std::string& body);
    httplib::Result cancel_order(int64_t order_id);
    httplib::Result get_order(int64_t order_id);
    httplib::Result get_orders(const std::string& query = "");

    // positions
    httplib::Result get_positions(const std::string& query = "");

    // wallet
    httplib::Result get_wallet(const std::string& query = "");

private:
    // prehash: method + timestamp + requestPath + query_params + body
    httplib::Headers auth_headers(const std::string& method,
                                  const std::string& path,
                                  const std::string& query,
                                  const std::string& body);

    std::string sign(const std::string& method,
                     const std::string& ts,
                     const std::string& path,
                     const std::string& query,
                     const std::string& body);

    static std::string to_hex(const unsigned char* data, size_t len);
    static int64_t now_ms();

    std::string api_key_;
    std::string api_secret_;
};
