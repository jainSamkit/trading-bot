#pragma once
#include "transport/http_client.hpp"
#include "delta_exchange/models/order.hpp"
#include "delta_exchange/models/position.hpp"
#include "delta_exchange/models/product.hpp"
#include "execution/execution_manager.hpp"
#include "oms/oms_manager.hpp"
#include "core/spsc_ring.hpp"
#include "config/config.hpp"
#include <string>

class DeltaRestClient : public TcpClient {
    static constexpr size_t OMS_RING_SIZE = cfg::OMS_RING_SIZE;
public:
    DeltaRestClient(const char* host, const char* api_key, const char* api_secret,
                    const ProductTable& products);

    // execution — called by ExecutionManager via jump table
    void create_order      (const ExecutionIntent& intent, SpscRing<OMSEvent, OMS_RING_SIZE>* ring);
    void edit_order        (const ExecutionIntent& intent, SpscRing<OMSEvent, OMS_RING_SIZE>* ring);
    void cancel_order      (const ExecutionIntent& intent, SpscRing<OMSEvent, OMS_RING_SIZE>* ring);
    void cancel_all_orders (const ExecutionIntent& intent, SpscRing<OMSEvent, OMS_RING_SIZE>* ring);
    void close_all_positions(const ExecutionIntent& intent, SpscRing<OMSEvent, OMS_RING_SIZE>* ring);
    void reconcile_open_orders    (SpscRing<OMSEvent, OMS_RING_SIZE>* ring);
    void reconcile_open_positions (SpscRing<OMSEvent, OMS_RING_SIZE>* ring);
    void reconcile_wallet         (SpscRing<OMSEvent, OMS_RING_SIZE>* ring);

    // reconcile / init fetches
    httplib::Result get_products (const std::string& query = "");
    httplib::Result get_product  (const std::string& symbol);
    httplib::Result get_orders   (const std::string& query = "");
    httplib::Result get_positions(const std::string& query = "");
    httplib::Result get_wallet   (const std::string& query = "");

private:
    httplib::Result place_order_http         (const std::string& body);
    httplib::Result cancel_order_http        (const std::string& body);
    httplib::Result cancel_all_orders_http   (const std::string& body);
    httplib::Result edit_order_http          (const std::string& body);
    httplib::Result close_all_positions_http (const std::string& body);

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
    static int64_t now_s();
    static int64_t now_ms();

    std::string          api_key_;
    std::string          api_secret_;
    const ProductTable&  products_;
};
