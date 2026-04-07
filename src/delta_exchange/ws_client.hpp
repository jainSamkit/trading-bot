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
#include "feed/models/product.hpp"
#include "feed/sessions/types.hpp"
#include "core/spsc_ring.hpp"

class L2UpdateSession;
class TickerSession;
class MarkSession;
class SpotSession;
class OHLCSession;

class DeltaWebsocketClient : public WebSocketClient<DeltaWebsocketClient> {
public:
    DeltaWebsocketClient(const char* host, int port, const char* path,
                         const ProductTable& products, SpscRing<FeedMessage,4096>* ring);

    ~DeltaWebsocketClient();

    static constexpr uint64_t HEARTBEAT_TIMEOUT_MS = 35000;

    void enable_heartbeat(SSL* ssl) {
        ws_send(ssl, R"({"type":"enable_heartbeat"})");
    }

    void start();

    bool epoll_add(int fd, epoll_event& ev) {
        if (fd < 0)
            return false;
        if (epoll_ctl(epfd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
            if (errno == EEXIST) {
                if (epoll_ctl(epfd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
                    perror("epoll_ctl ADD fd");
                    return false;
                }
                return true;
            }
            return false;
        }
        return true;
    }

    bool epoll_delete(int fd) {
        epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, NULL);
        return true;
    }

    bool onsessionAdd(SessionCtx& ctx, EpollSlot& socketSlot, EpollSlot& timerSlot) {
        if (epfd_ < 0) {
            perror("epoll_create1");
            return false;
        }
        if (ctx.fd_ < 0)
            return false;
        assert(ctx.status == SessionStatus::CONNECTED);

        int flags = fcntl(ctx.fd_, F_GETFL, 0);
        fcntl(ctx.fd_, F_SETFL, flags | O_NONBLOCK);

        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.ptr = &socketSlot;

        if (!epoll_add(ctx.fd_, ev))
            return false;

        if (ctx.tfd_ >= 0) {
            ev.events = EPOLLIN | EPOLLET;
            ev.data.ptr = &timerSlot;

            if (!epoll_add(ctx.tfd_, ev))
                return false;
        }

        return true;
    }

    void onsessionDelete(SessionCtx& ctx) {
        if (ctx.fd_ >= 0)
            epoll_delete(ctx.fd_);
        if (ctx.tfd_ >= 0)
            epoll_delete(ctx.tfd_);
    }

    bool shutdown() {
        uint64_t one = 1;
        if (write(efd_, &one, sizeof(one)) != sizeof(one)) {
            perror("eventfd write");
            return false;
        }
        return true;
    }

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

    const ProductTable products_;

protected:
    simdjson::ondemand::parser feed_parser_;

private:
    std::unique_ptr<L2UpdateSession> l2UpdateSession_;
    std::unique_ptr<TickerSession>   tickerSession_;
    std::unique_ptr<MarkSession>     markSession_;
    std::unique_ptr<SpotSession>     spotSession_;
    std::unique_ptr<OHLCSession>     ohlcSession_;
    SpscRing<FeedMessage, 4096>* ring_;

    bool shutdown_ = false;
    EpollSlot eventFDSlot;
};

#include "feed/sessions/l2.hpp"
#include "feed/sessions/mark.hpp"
#include "feed/sessions/ticker.hpp"
#include "feed/sessions/spot.hpp"
#include "feed/sessions/ohlc.hpp"

inline DeltaWebsocketClient::~DeltaWebsocketClient() {
    if (!shutdown_)
        shutdownReactor();
}

inline void DeltaWebsocketClient::start() {
    if (!l2UpdateSession_->init())return;
    if (!markSession_->init())return;
    if (!spotSession_->init()) return;
    if (!ohlcSession_->init()) return;

    l2UpdateSession_->subscribe();
    markSession_->subscribe();
    spotSession_->subscribe();
    ohlcSession_->subscribe();

    run_loop(static_cast<DeltaWebsocketClient*>(this));
}

inline void DeltaWebsocketClient::shutdownReactor() {
    if (shutdown_)
        return;
    shutdown_ = true;

    l2UpdateSession_->destroy();
    tickerSession_->destroy();
    markSession_->destroy();
    spotSession_->destroy();
    ohlcSession_->destroy();

    if (efd_ >= 0) {
        epoll_ctl(epfd_, EPOLL_CTL_DEL, efd_, nullptr);
        close(efd_);
        efd_ = -1;
    }
    if (epfd_ >= 0) {
        close(epfd_);
        epfd_ = -1;
    }
}

inline DeltaWebsocketClient::DeltaWebsocketClient(
    const char* host, int port, const char* path,
    const ProductTable& products, SpscRing<FeedMessage, 4096>* ring)
    : products_(products)
    , l2UpdateSession_(std::make_unique<L2UpdateSession>(*this, SessionID::L2Update))
    , tickerSession_(std::make_unique<TickerSession>(*this, SessionID::Ticker))
    , markSession_(std::make_unique<MarkSession>(*this, SessionID::Mark))
    , spotSession_(std::make_unique<SpotSession>(*this, SessionID::Spot))
    , ohlcSession_(std::make_unique<OHLCSession>(*this, SessionID::OHLC))
    , ring_(ring)
{
    WebSocketClient::host = host ? host : "";
    WebSocketClient::port = port;
    WebSocketClient::path = (path && path[0]) ? path : "/";

    WebSocketClient::init();
    epfd_ = epoll_create1(0);
    if (epfd_ < 0)
        throw std::system_error(errno, std::generic_category(), "epoll_create1");

    int efd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (efd < 0) {
        close(epfd_);
        throw std::system_error(errno, std::generic_category(), "eventfd_create");
    }

    eventFDSlot.ctx            = nullptr;
    eventFDSlot.kind           = Kind::Eventfd;
    eventFDSlot.shutdownEfd    = efd;
    eventFDSlot.session_opaque = nullptr;
    eventFDSlot.socket_ready   = nullptr;

    epoll_event ev{};
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = &eventFDSlot;

    if (epoll_ctl(epfd_, EPOLL_CTL_ADD, efd, &ev) < 0) {
        close(epfd_);
        close(efd);
        throw std::system_error(errno, std::generic_category(), "epoll_ctl_efd_add");
    }

    WebSocketClient<DeltaWebsocketClient>::efd_ = efd;
}
