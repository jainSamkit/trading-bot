#include "delta_exchange/ws_client.hpp"

DeltaWebsocketClient::DeltaWebsocketClient(
    const char* host, int port, const char* path,
    const ProductTable& products, SpscRing<FeedMessage, FEED_RING_SIZE>* const ring, const ProductGroup& product_groups)
    : products_(products)
    , product_groups_(product_groups)
    , l2UpdateSession_(std::make_unique<L2UpdateSession>(*this, SessionID::L2Update))
    , tradeSession_(std::make_unique<TradeSession>(*this, SessionID::Trade))
    , markSession_(std::make_unique<MarkSession>(*this, SessionID::Mark))
    , spotSession_(std::make_unique<SpotSession>(*this, SessionID::Spot))
    // , ohlcSession_(std::make_unique<OHLCSession>(*this, SessionID::OHLC))
    , ring_(ring)
{
    WebSocketClient::host = host ? host : "";
    WebSocketClient::port = port;
    WebSocketClient::path = (path && path[0]) ? path : "/";

    WebSocketClient::init();
    WebSocketClient::init_transport_histograms();
    
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

DeltaWebsocketClient::~DeltaWebsocketClient() {
    if (!shutdown_)
        shutdownReactor();
}

void DeltaWebsocketClient::start() {
    if (!l2UpdateSession_->init())return;
    if (!markSession_->init())return;
    if (!spotSession_->init()) return;
    // if (!ohlcSession_->init()) return;
    if (!tradeSession_->init()) return;

    l2UpdateSession_->start();
    markSession_->start();
    spotSession_->start();
    // ohlcSession_->start();
    tradeSession_->start();


    run_loop(static_cast<DeltaWebsocketClient*>(this));
}

void DeltaWebsocketClient::shutdownReactor() {
    if (shutdown_)
        return;
    shutdown_ = true;

    l2UpdateSession_->destroy();
    tradeSession_->destroy();
    markSession_->destroy();
    spotSession_->destroy();
    // ohlcSession_->destroy();

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

bool DeltaWebsocketClient::epoll_add(int fd, epoll_event& ev) {
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

bool DeltaWebsocketClient::epoll_delete(int fd) {
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, NULL);
    return true;
}

bool DeltaWebsocketClient::onsessionAdd(SessionCtx& ctx, EpollSlot& socketSlot, EpollSlot& timerSlot) {
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

void DeltaWebsocketClient::onsessionDelete(SessionCtx& ctx) {
    if (ctx.fd_ >= 0)
        epoll_delete(ctx.fd_);
    if (ctx.tfd_ >= 0)
        epoll_delete(ctx.tfd_);
}

bool DeltaWebsocketClient::shutdown() {
    uint64_t one = 1;
    if (write(efd_, &one, sizeof(one)) != sizeof(one)) {
        perror("eventfd write");
        return false;
    }
    return true;
}
