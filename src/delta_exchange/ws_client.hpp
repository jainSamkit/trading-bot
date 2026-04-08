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

class L2UpdateSession;
class TickerSession;
class MarkSession;
class SpotSession;
class OHLCSession;

class DeltaWebsocketClient : public WebSocketClient<DeltaWebsocketClient> {
public:
    DeltaWebsocketClient(const char* host, int port, const char* path,
                         const ProductTable& products, SpscRing<FeedMessage,4096>* const ring, const ProductGroup& product_groups);

    ~DeltaWebsocketClient();

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
        ring_-> push_commit();
    }

    FeedMessage* get_ring_slot() {
        return ring_ -> push_begin();
    }

    bool isShutdown() {
        return shutdown_;
    }

    void shutdownReactor();

    simdjson::ondemand::parser& get_parser() { return feed_parser_; }

    const ProductTable& products_;
    const ProductGroup& product_groups_;

protected:
    simdjson::ondemand::parser feed_parser_;

private:
    std::unique_ptr<L2UpdateSession>        l2UpdateSession_;
    std::unique_ptr<TickerSession>          tickerSession_;
    std::unique_ptr<MarkSession>            markSession_;
    std::unique_ptr<SpotSession>            spotSession_;
    std::unique_ptr<OHLCSession>            ohlcSession_;
    SpscRing<FeedMessage, 4096>* const      ring_;

    bool shutdown_ = false;
    EpollSlot eventFDSlot;
};

#include "delta_exchange/sessions/l2.hpp"
#include "delta_exchange/sessions/mark.hpp"
#include "delta_exchange/sessions/ticker.hpp"
#include "delta_exchange/sessions/spot.hpp"
#include "delta_exchange/sessions/ohlc.hpp"
