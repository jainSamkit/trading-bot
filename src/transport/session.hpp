#pragma once

// Included from wsclient.hpp after WebSocketClient is fully defined.
// Do NOT include this file directly — include wsclient.hpp instead.

template<typename DerivedSession, typename ClientDerived>
class Session {
public:
    static constexpr uint8_t MAX_RECONNECTS = 10; //max retries per session 
    static constexpr uint32_t base_wait = 1000; //base milliseconds to wait before retrying

    using Status = SessionStatus;
    using Ctx = SessionCtx;

    explicit Session(ClientDerived& client, SessionID sessionID)
        : client_(client), ctx_{.id = sessionID} {}

    bool isHeartbeat(std::string_view msg) {
        if(msg.size() > 64 + simdjson::SIMDJSON_PADDING) return false;
        return msg.find(R"("heartbeat")") != std::string_view::npos;
    }

    void forward_message(std::string_view m) { 
        if(isHeartbeat(m)) {
            arm_timer_ms(ClientDerived::HEARTBEAT_TIMEOUT_MS);
            return;
        }

        derivedSession().onMessage(m); 
    }

    WSParser parser_;

    SSL* get_ssl() const { return ctx_.ssl_; }
    int get_fd() const { return ctx_.fd_; }
    int get_tfd() const { return ctx_.tfd_; }
    SessionStatus status() const { return ctx_.status; }
    uint8_t reconnect_count() const { return reconnects; }

    bool init(bool with_timer = true) {
        bool socket_connected = tcp_connect() && tls_connect() && client_.ws_upgrade(ctx_.ssl_);
        if(!socket_connected) {
            if(ctx_.ssl_) { SSL_free(ctx_.ssl_); ctx_.ssl_ = nullptr; }
            if(ctx_.fd_ >= 0) { close(ctx_.fd_); ctx_.fd_ = -1; }
            return false;
        }
        if(with_timer) {
            if(!setup_timer()) { return false;}
        }

        ctx_.status = Status::CONNECTED;

        socketSlot.ctx            = &ctx_;
        socketSlot.kind           = Kind::Socket;
        socketSlot.session_opaque = static_cast<void*>(static_cast<DerivedSession*>(this));
        socketSlot.socket_ready   = &socket_epoll_stub<DerivedSession, ClientDerived>;

        timerSlot.ctx             = &ctx_;
        timerSlot.kind            = Kind::Timer;
        timerSlot.session_opaque  = static_cast<void*>(static_cast<DerivedSession*>(this));
        timerSlot.socket_ready    = &timer_epoll_stub<DerivedSession, ClientDerived>;

        return client_.onsessionAdd(ctx_, socketSlot, timerSlot);
    }

    void subscribe() {
        derivedSession().onSubscribe();
    }

    bool tcp_connect() {
        auto fd = client_.tcp_connect();
        if(!fd) return false;
        ctx_.fd_ = *fd;

        return true;
    }

    bool tls_connect() {
        auto ssl = client_.tls_connect(ctx_.fd_);
        if (!ssl)
            return false;
        ctx_.ssl_ = *ssl;

        return true;
    }

    bool setup_timer() {
        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if(tfd<0) return false;
        ctx_.tfd_ = tfd;
        return true;
    }

    void destroy() {
        ctx_.status = Status::DISCONNECTED;
        client_.onsessionDelete(ctx_);

        if(ctx_.ssl_) { SSL_shutdown(ctx_.ssl_); SSL_free(ctx_.ssl_); ctx_.ssl_ = nullptr;}
        if(ctx_.fd_ >= 0) { close(ctx_.fd_); ctx_.fd_ = -1;}
        if(ctx_.tfd_ >= 0) { close(ctx_.tfd_); ctx_.tfd_ = -1;}
    }

    void disconnect() {
        std::cerr << "[session " << (int)ctx_.id << "] disconnected\n";
        ctx_.status = Status::DISCONNECTED;
        client_.onsessionDelete(ctx_);

        if(ctx_.ssl_) { SSL_shutdown(ctx_.ssl_); SSL_free(ctx_.ssl_); ctx_.ssl_ = nullptr;}
        if(ctx_.fd_ >= 0) { close(ctx_.fd_); ctx_.fd_ = -1;}
    }

    int arm_timer_ms(int delay_ms) {
        itimerspec ts{};
        ts.it_value.tv_sec  = delay_ms / 1000;
        ts.it_value.tv_nsec = (delay_ms % 1000) * 1000000L;
        ts.it_interval.tv_sec  = 0;   // no repeat
        ts.it_interval.tv_nsec = 0;
    
        if (timerfd_settime(ctx_.tfd_, 0, &ts, nullptr) < 0)
            {perror("timerfd_settime"); return -1;}
        else {
            return 0;
        }
    }

    void reconnect() {
        for (;;) {
            switch (ctx_.status) {
                case Status::DISCONNECTED:
                    ctx_.status = Status::RECONNECTING;
                    continue;

                case Status::RECONNECTING:
                    if (reconnects >= MAX_RECONNECTS) {
                        std::cerr << "[session " << (int)ctx_.id << "] max reconnects reached\n";
                        destroy();
                        return;
                    }
                    std::cerr << "[session " << (int)ctx_.id << "] reconnect attempt " << (int)(reconnects + 1) << "\n";
                    if (init(false)) {
                        reconnects = 0;
                        std::cerr << "[session " << (int)ctx_.id << "] reconnected — resubscribing\n";
                        subscribe();
                        return;
                    }
                    {
                        uint32_t delay_ms = base_wait + (1u << reconnects) * 1000;
                        std::cerr << "[session " << (int)ctx_.id << "] init failed — retrying in " << delay_ms << "ms\n";
                        reconnects += 1;
                        if (arm_timer_ms(delay_ms) < 0)
                            destroy();
                        return;
                    }

                case Status::CONNECTED:
                    disconnect();
                    continue;
            }
        }
    }

    void sendAuth() {
        derivedSession().onAuth();
    }

    ~Session() {
        destroy();
    }

protected:
    ClientDerived& client_;
    Ctx ctx_;

private:

    DerivedSession& derivedSession() {
        return *static_cast<DerivedSession*>(this);
    }

    uint8_t reconnects = 0;
    EpollSlot socketSlot;
    EpollSlot timerSlot;
};
