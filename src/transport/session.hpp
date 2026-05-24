#pragma once
#include <chrono>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include "latency/registry.hpp"

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
        : client_(client), ctx_{.id = sessionID} {
            jsonparse_hist_ = latency::Registry::get_or_create({.event_type = latency::TagSet::EventType::JsonParse, .msg_type = DerivedSession::MSG_TYPE});
            ringwait_hist_ = latency::Registry::get_or_create({.event_type = latency::TagSet::EventType::RingWait, .msg_type = DerivedSession::MSG_TYPE});
            ringpush_hist_ = latency::Registry::get_or_create({.event_type = latency::TagSet::EventType::RingPush, .msg_type = DerivedSession::MSG_TYPE});
        }

    bool isHeartbeat(std::string_view msg) {
        if(msg.size() > 64 + simdjson::SIMDJSON_PADDING) return false;
        return msg.find(R"("heartbeat")") != std::string_view::npos;
    }

    bool isAuthResponse(std::string_view msg) {
        return msg.find(R"("type":"success")") != std::string_view::npos
            || msg.find(R"("type":"error")")   != std::string_view::npos;
    }

    bool isAuthSuccess(std::string_view msg) {
        return msg.find(R"("type":"success")") != std::string_view::npos;
    }

    void handleAuthResponse(std::string_view msg) {
        if (isAuthSuccess(msg)) {
            std::cerr << "[session " << (int)ctx_.id << "] auth success\n";
            authenticated_ = true;
            subscribe();
            return;
        }

        // Auth failed — format: {"message":"...","type":"error"}
        simdjson::ondemand::parser& parser = client_.get_parser();
        auto result = parser.iterate(msg.data(), msg.size(),
                                     msg.size() + simdjson::SIMDJSON_PADDING);
        if (result.error()) {
            std::cerr << "[session " << (int)ctx_.id << "] auth failed (unparseable): " << msg << "\n";
            return;
        }
        auto doc = std::move(result.value());

        std::string_view message;
        for (auto field : doc.get_object()) {
            std::string_view key;
            if (field.unescaped_key().get(key)) continue;
            if (key == "message") { if(!field.value().get_string().get(message)) return; }
        }
        
        std::cerr << "[session " << (int)ctx_.id << "] auth failed: " << message << "\n";
    }

    void forward_message(std::string_view m) {
        if (isHeartbeat(m)) {
            arm_timer_ms(ClientDerived::HEARTBEAT_TIMEOUT_MS);
            return;
        }
        if constexpr (DerivedSession::session_type == SessionType::Private) {
            if (!authenticated_) {
                std::cerr << "[auth raw] " << m << "\n";
                if (isAuthResponse(m)) {
                    handleAuthResponse(m);
                    return;
                }
            }
        }
        
        {                                                            
            latency::Span s(jsonparse_hist_);     // ← wraps only the parse, not heartbeats/auth                                                                                                                                                                                                   
            derivedSession().onMessage(m);                                            
        } 
    }


    void start() {
        if constexpr (DerivedSession::session_type == SessionType::Private) {sendAuth();}
        else {subscribe();}
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
        authenticated_ = false;
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
                        if constexpr (DerivedSession::session_type == SessionType::Private) {
                            std::cerr << "[session " << (int)ctx_.id << "] reconnected — authenticating\n";
                            sendAuth();
                        } else {
                            std::cerr << "[session " << (int)ctx_.id << "] reconnected — resubscribing\n";
                            subscribe();
                        }
                        return;
                    }
                    else {
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
        if constexpr (DerivedSession::session_type == SessionType::Private) {
            using namespace std::chrono;
            uint64_t ts = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
            std::string ts_str = std::to_string(ts);

            std::string payload = "GET" + ts_str + "/live";

            unsigned char digest[EVP_MAX_MD_SIZE];
            unsigned int  digest_len = 0;
            HMAC(EVP_sha256(),
                 client_.api_secret_.data(), static_cast<int>(client_.api_secret_.size()),
                 reinterpret_cast<const unsigned char*>(payload.data()), payload.size(),
                 digest, &digest_len);

            char hex[65];
            for (unsigned int i = 0; i < digest_len; ++i)
                snprintf(hex + i * 2, 3, "%02x", digest[i]);

            std::string msg =
                R"({"type":"auth","payload":{"api-key":")" + client_.api_key_ +
                R"(","signature":")" + std::string(hex, digest_len * 2) +
                R"(","timestamp":")" + ts_str + R"("}})";

            client_.ws_send(ctx_.ssl_, msg);
        }
    }

    ~Session() {
        destroy();
    }

protected:
    latency::Histogram* jsonparse_hist_ = nullptr;
    latency::Histogram* ringwait_hist_ = nullptr;
    latency::Histogram* ringpush_hist_ = nullptr;
    ClientDerived& client_;
    Ctx ctx_;

private:

    DerivedSession& derivedSession() {
        return *static_cast<DerivedSession*>(this);
    }

    uint8_t reconnects = 0;
    bool authenticated_ = false;
    EpollSlot socketSlot;
    EpollSlot timerSlot;
};
