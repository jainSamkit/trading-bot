#include "delta_exchange/oms_ws_client.hpp"

DeltaOMSWebsocketClient::DeltaOMSWebsocketClient(
    const char* host, int port, const char* path, const char* api_key, const char* api_secret,
    const ProductTable& products, SpscRing<OMSEvent, OMS_RING_SIZE>* const ring)
    : products_(products)
    , api_key_(api_key ? api_key : "")
    , api_secret_(api_secret ? api_secret : "")
    , orderSession_(std::make_unique<OrderSession>(*this, SessionID::Order))
    , positionFillSession_(std::make_unique<PositionFillSession>(*this, SessionID::Position))
    , walletSession_(std::make_unique<WalletSession>(*this, SessionID::Wallet))
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

    WebSocketClient<DeltaOMSWebsocketClient>::efd_ = efd;
}

DeltaOMSWebsocketClient::~DeltaOMSWebsocketClient() {
    if (!shutdown_)
        shutdownReactor();
}

void DeltaOMSWebsocketClient::start() {
    if (!orderSession_->init()) return;
    if (!positionFillSession_->init()) return;
    if (!walletSession_->init()) return;

    orderSession_->start();
    positionFillSession_->start();
    walletSession_->start();

    run_loop(static_cast<DeltaOMSWebsocketClient*>(this));
}

void DeltaOMSWebsocketClient::shutdownReactor() {
    if (shutdown_)
        return;
    shutdown_ = true;

    orderSession_->destroy();
    positionFillSession_->destroy();
    walletSession_->destroy();

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

bool DeltaOMSWebsocketClient::epoll_add(int fd, epoll_event& ev) {
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

bool DeltaOMSWebsocketClient::epoll_delete(int fd) {
    epoll_ctl(epfd_, EPOLL_CTL_DEL, fd, NULL);
    return true;
}

bool DeltaOMSWebsocketClient::onsessionAdd(SessionCtx& ctx, EpollSlot& socketSlot, EpollSlot& timerSlot) {
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

void DeltaOMSWebsocketClient::onsessionDelete(SessionCtx& ctx) {
    if (ctx.fd_ >= 0)
        epoll_delete(ctx.fd_);
    if (ctx.tfd_ >= 0)
        epoll_delete(ctx.tfd_);
}

bool DeltaOMSWebsocketClient::shutdown() {
    if(shutdown_) return true;
    
    uint64_t one = 1;
    if (write(efd_, &one, sizeof(one)) != sizeof(one)) {
        perror("eventfd write oms");
        return false;
    }
    return true;
}
