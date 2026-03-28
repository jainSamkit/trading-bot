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

class L2UpdateSession;
class TickerSession;
class MarkSession;

class DeltaWebsocketClient : public WebSocketClient<DeltaWebsocketClient> {
public:
    DeltaWebsocketClient(const char* host, int port, const char* path = "/");

    ~DeltaWebsocketClient();

    static constexpr uint64_t HEARTBEAT_TIMEOUT_MS = 35000;

    /** Default: Delta testnet public L2 book (override before start()). */
    void setL2Subscription(std::string channel, std::string symbol) {
        l2_channel_ = std::move(channel);
        l2_symbol_  = std::move(symbol);
    }

    void enable_heartbeat(SSL* ssl) {
        ws_send(ssl, R"({"type":"enable_heartbeat"})");
    }

    void subscribe(SSL* ssl, const std::string& channel,
                   const std::string& symbol) {
        std::string msg =
            R"({"type":"subscribe","payload":{"channels":[{"name":")"
            + channel
            + R"(","symbols":[")"
            + symbol
            + R"("]}]}})";
        ws_send(ssl, msg);
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
        // sessionHash.erase(ctx.id);
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

    bool isShutdown() {
        return shutdown_;
    }

    /** Called from I/O thread after draining shutdown eventfd — closes sessions and epoll. */
    void shutdownReactor();

protected:
    simdjson::ondemand::parser feed_parser_;
    
private:
    std::unique_ptr<L2UpdateSession>   l2UpdateSession_;
    std::unique_ptr<TickerSession>    tickerSession_;
    std::unique_ptr<MarkSession>     markSession_;

    std::string l2_channel_{"l2_orderbook"};
    std::string l2_symbol_{"BTCUSD_TestNet"};

    bool shutdown_ = false;
    EpollSlot eventFDSlot;
};

// Session classes need DeltaWebsocketClient complete (above) and are needed
// complete by the out-of-line methods (below). This is the only valid spot.
#include "delta_exchange/sessions/l2.hpp"
#include "delta_exchange/sessions/mark.hpp"
#include "delta_exchange/sessions/ticker.hpp"

inline DeltaWebsocketClient::~DeltaWebsocketClient() {
    if (!shutdown_)
        shutdownReactor();
}

inline void DeltaWebsocketClient::start() {
    if (!l2UpdateSession_->init())
        return;
    l2UpdateSession_->subscribe();

    // if (SSL* ssl = l2UpdateSession_->get_ssl()) {
    //     if (!l2_channel_.empty() && !l2_symbol_.empty())
    //         subscribe(ssl, l2_channel_, l2_symbol_);
    //     enable_heartbeat(ssl);
    //     std::cout<<"Heartbeat enabled"<<"\n\n";
    //     l2UpdateSession_->arm_timer_ms(DeltaWebsocketClient::HEARTBEAT_TIMEOUT_MS);
    //     std::cout<<"Timer armed"<<"\n\n";
    // }
    run_loop(static_cast<DeltaWebsocketClient*>(this));
}

inline void DeltaWebsocketClient::shutdownReactor() {
    if (shutdown_)
        return;
    shutdown_ = true;

    l2UpdateSession_->destroy();
    tickerSession_->destroy();
    markSession_->destroy();

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

inline DeltaWebsocketClient::DeltaWebsocketClient(const char* host, int port, const char* path)
    : l2UpdateSession_(std::make_unique<L2UpdateSession>(*this, SessionID::L2Update))
    , tickerSession_(std::make_unique<TickerSession>(*this, SessionID::Ticker))
    , markSession_(std::make_unique<MarkSession>(*this, SessionID::Mark)) {

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
