#include "transport/wsclient.hpp"
#include <openssl/ssl.h>
#include <cassert>
#include <cerrno>
#include <iostream>
#include <memory>
#include <string>
#include <system_error>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include "simdjson.h"
#include "delta_exchange/models/product.hpp"
#include "delta_exchange/sessions/types.hpp"
#include "core/spsc_ring.hpp"
#include "src/oms/oms_manager.hpp"

class OrderSession;
class PositionFillSession;
class WalletSession;

class DeltaOMSWebsocketClient : public WebSocketClient<DeltaOMSWebsocketClient> {
public:
    DeltaOMSWebsocketClient(const char* host, int port, const char* path, const char* api_key, const char* api_secret,
                         const ProductTable& products, SpscRing<OMSEvent,4096>* const ring);

    ~DeltaOMSWebsocketClient();

    static constexpr uint64_t HEARTBEAT_TIMEOUT_MS = 35000;

    void enable_heartbeat(SSL* ssl) {
        ws_send(ssl, R"({"type":"enable_heartbeat"})");
    }

    void start();

    bool epoll_add(int fd, epoll_event& ev);

    bool epoll_delete(int fd);

    bool onsessionAdd(SessionCtx& ctx, EpollSlot& socketSlot, EpollSlot& timerSlot);

    void onsessionDelete(SessionCtx& ctx);

    bool shutdown();

    void commit_to_ring() {
        ring_->push_commit();
    }

    OMSEvent* get_ring_slot() {
        return ring_->push_begin();
    }

    bool isShutdown() {
        return shutdown_;
    }

    void shutdownReactor();

    simdjson::ondemand::parser& get_parser() { return oms_parser_; }

    const ProductTable& products_;
    std::string api_key_;
    std::string api_secret_;

protected:
    simdjson::ondemand::parser oms_parser_;

private:
    std::unique_ptr<OrderSession>                   orderSession_;
    std::unique_ptr<PositionFillSession>            positionFillSession_;
    std::unique_ptr<WalletSession>                  walletSession_;
    SpscRing<OMSEvent, 4096>* const              ring_;

    bool shutdown_ = false;
    EpollSlot eventFDSlot;
};

#include "delta_exchange/sessions/order.hpp"
#include "delta_exchange/sessions/position.hpp"
#include "delta_exchange/sessions/wallet.hpp"
