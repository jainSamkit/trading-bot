#pragma once

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/sha.h>
#include <sys/timerfd.h>
#include <cstdint>
#include "transport/types.hpp"

#ifndef be64toh
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be64toh(x) OSSwapBigToHostInt64(x)
#else
#include <endian.h>
#endif
#endif

template<typename DerivedClient>
struct WebSocketClient;

template<typename DerivedSession, typename ClientDerived>
class Session;

template<typename DS, typename DC>
void socket_epoll_stub(void* client, void* base, EpollSlot* slot);

template<typename DS, typename DC>
void timer_epoll_stub(void* client, void* base, EpollSlot* slot);

static constexpr int BUFSIZE           = 4096*4;
static constexpr int MAX_EPOLL_EVENTS = 64;

// ── base64 ────────────────────────────────────────────────────
inline std::string base64_encode(const uint8_t* data, size_t len) {
    static const char t[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = ((uint32_t)data[i] << 16)
                   | (i+1 < len ? (uint32_t)data[i+1] << 8 : 0)
                   | (i+2 < len ? (uint32_t)data[i+2]      : 0);
        out += t[(b>>18)&0x3F];
        out += t[(b>>12)&0x3F];
        out += i+1 < len ? t[(b>>6)&0x3F] : '=';
        out += i+2 < len ? t[(b>>0)&0x3F] : '=';
    }
    return out;
}

// ── fast PRNG (xoshiro256**) — seeded once from /dev/urandom ──
struct FastPRNG {
    uint64_t s[4];

    FastPRNG() {
        FILE* f = fopen("/dev/urandom", "rb");
        if (f) {
            if (fread(s, sizeof(s), 1, f) != 1) {
                s[0] = 0x12345678; s[1] = 0x9abcdef0;
                s[2] = 0xdeadbeef; s[3] = 0xcafebabe;
            }
            fclose(f);
        } else {
            s[0] = 0x12345678; s[1] = 0x9abcdef0;
            s[2] = 0xdeadbeef; s[3] = 0xcafebabe;
        }
    }

    static uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

    uint64_t next() {
        const uint64_t result = rotl(s[1] * 5, 7) * 9;
        const uint64_t t = s[1] << 17;
        s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
        s[2] ^= t;
        s[3] = rotl(s[3], 45);
        return result;
    }

    void fill(uint8_t* buf, size_t len) {
        while (len >= 8) {
            uint64_t v = next();
            memcpy(buf, &v, 8);
            buf += 8; len -= 8;
        }
        if (len > 0) {
            uint64_t v = next();
            memcpy(buf, &v, len);
        }
    }
};

inline FastPRNG& prng() {
    static FastPRNG instance;
    return instance;
}

// ── generate random WebSocket key ─────────────────────────────
inline std::string make_ws_key() {
    uint8_t rand_bytes[16];
    prng().fill(rand_bytes, 16);
    return base64_encode(rand_bytes, 16);
}

// ── WebSocket frame parser ────────────────────────────────────
struct WSParser {
    enum class State {
        Header, ExtLen16, ExtLen64, MaskKey, Payload
    };

    static constexpr size_t DEFAULT_RESERVE = 65536; // 64KB covers most L2 snapshots

    uint8_t  header_[2];
    State    state_      = State::Header;
    uint8_t  hfilled_    = 0;
    bool     fin_        = false;
    uint8_t  opcode_     = 0;
    bool     masked_     = false;
    uint64_t payload_len_= 0;
    uint8_t  mask_key_[4];
    uint8_t  ext_buf_[8];
    uint8_t  ext_filled_ = 0;
    uint64_t pfilled_    = 0;
    std::vector<uint8_t> payload_;

    // fragmentation state
    std::string fragment_buf_;

    WSParser() {
        payload_.reserve(DEFAULT_RESERVE);
        fragment_buf_.reserve(DEFAULT_RESERVE);
    }
    
    uint8_t     fragment_opcode_ = 0;
    bool        in_fragment_     = false;

    template<typename Callback>
    void feed(const uint8_t* data, size_t len, Callback&& on_frame)
    {
        size_t i = 0;
        while (i < len) {

            if (state_ == State::Header) {
                while (hfilled_ < 2 && i < len)
                    header_[hfilled_++] = data[i++];

                if (hfilled_ == 2) {
                    fin_      = (header_[0] & 0x80) != 0;
                    opcode_   =  header_[0] & 0x0F;
                    masked_   = (header_[1] & 0x80) != 0;
                    uint64_t plen = header_[1] & 0x7F;

                    hfilled_   = 0;
                    ext_filled_= 0;
                    pfilled_   = 0;

                    if (plen == 126) {
                        state_ = State::ExtLen16;
                    } else if (plen == 127) {
                        state_ = State::ExtLen64;
                    } else {
                        payload_len_ = plen;
                        payload_.resize(payload_len_);
                        if (plen == 0 && !masked_) {
                            dispatch(on_frame);
                            state_   = State::Header;
                            hfilled_ = 0;
                        } else {
                            state_ = masked_ ? State::MaskKey
                                             : State::Payload;
                        }
                    }
                }

            } else if (state_ == State::ExtLen16) {
                while (ext_filled_ < 2 && i < len)
                    ext_buf_[ext_filled_++] = data[i++];
                if (ext_filled_ == 2) {
                    uint16_t n;
                    memcpy(&n, ext_buf_, 2);
                    payload_len_ = ntohs(n);
                    payload_.resize(payload_len_);
                    state_ = masked_ ? State::MaskKey : State::Payload;
                }

            } else if (state_ == State::ExtLen64) {
                while (ext_filled_ < 8 && i < len)
                    ext_buf_[ext_filled_++] = data[i++];
                if (ext_filled_ == 8) {
                    uint64_t n;
                    memcpy(&n, ext_buf_, 8);
                    payload_len_ = be64toh(n);
                    payload_.resize(payload_len_);
                    state_ = masked_ ? State::MaskKey : State::Payload;
                }

            } else if (state_ == State::MaskKey) {
                while (ext_filled_ < 4 && i < len)
                    mask_key_[ext_filled_++] = data[i++];
                if (ext_filled_ == 4)
                    state_ = State::Payload;

            } else if (state_ == State::Payload) {
                uint64_t take = std::min(
                    (uint64_t)(len - i),
                    payload_len_ - pfilled_
                );
                memcpy(payload_.data() + pfilled_, data + i, take);
                pfilled_ += take;
                i        += take;

                if (pfilled_ == payload_len_) {
                    // unmask if needed
                    if (masked_) {
                        for (uint64_t j = 0; j < payload_len_; j++)
                            payload_[j] ^= mask_key_[j % 4];
                    }

                    dispatch(on_frame);
                    state_   = State::Header;
                    hfilled_ = 0;
                }
            }
        }
    }

    template<typename Callback>
    void dispatch(Callback&& on_frame) {
        std::string_view payload((char*)payload_.data(), payload_len_);

        // control frames — never fragmented, deliver immediately
        if (opcode_ >= 0x8) {
            on_frame(opcode_, payload);
            return;
        }

        if (!fin_) {
            // fragment — accumulate
            if (opcode_ != 0x0) {
                // first fragment
                fragment_opcode_ = opcode_;
                fragment_buf_.assign(payload.begin(), payload.end());
            } else {
                // TODO: what if we get a continuation frame
                //       but in_fragment_ is false?
                //       handle protocol error here
                fragment_buf_.append(payload.begin(), payload.end());
            }
            in_fragment_ = true;
            return;
        }

        // FIN=1
        if (opcode_ == 0x0) {
            // final continuation frame
            fragment_buf_.append(payload.begin(), payload.end());
            on_frame(fragment_opcode_,
                     std::string_view(fragment_buf_));
            fragment_buf_.clear();
            fragment_opcode_ = 0;
            in_fragment_     = false;
        } else {
            // unfragmented — deliver directly
            on_frame(opcode_, payload);
        }
    }
};

// ── WebSocket client (CRTP: Derived implements sessionAdd / sessionDelete) ──
template<typename DerivedClient>
struct WebSocketClient {
    int      efd_  = -1; //eventfd
    int      epfd_ = -1; //epoll fd

    SSL_CTX* ctx  = nullptr;
    std::string ws_key_;
    std::string host;
    int port = 0;
    std::string path;

    // ── init OpenSSL ──────────────────────────────────────────
    bool init() {
        SSL_library_init();
        ctx = SSL_CTX_new(TLS_client_method());
        if (!ctx) {
            ERR_print_errors_fp(stderr);
            return false;
        }

        // TODO: enable certificate verification for production
        // right now accepting any certificate — fine for testnet
        // for production:
        // SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, nullptr);
        // SSL_CTX_set_default_verify_paths(ctx);
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, nullptr);

        return true;
    }

    // ── TCP connect (returns fd; owned by Session, not stored here) ──
    std::optional<int> tcp_connect() {
        return static_cast<DerivedClient*>(this)->tcp_connect_impl();
    }
    std::optional<int> tcp_connect_impl() {
        if (host.empty())
            return std::nullopt;

        addrinfo hints{};
        hints.ai_family   = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        const std::string port_str = std::to_string(port);

        addrinfo* res = nullptr;
        const int gai_err = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &res);
        if (gai_err != 0) {
            std::cerr << "getaddrinfo: " << gai_strerror(gai_err) << "\n";
            return std::nullopt;
        }

        int sockfd = -1;
        for (addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
            // sockfd = ::socket(rp->ai_family, rp->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, rp->ai_protocol);

            sockfd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (sockfd < 0) {
                perror("socket");
                continue;
            }

            int opt = 1;
            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
            setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

            if (::connect(sockfd, rp->ai_addr, static_cast<socklen_t>(rp->ai_addrlen)) == 0)
                break;
            perror("connect");
            ::close(sockfd);
            sockfd = -1;
        }

        freeaddrinfo(res);

        if (sockfd < 0)
            return std::nullopt;

        // TODO: configure socket options — TCP_NODELAY, SO_RCVBUF, SO_SNDBUF

        std::cout << "TCP connected to " << host << ":" << port << "\n";
        return sockfd;
    }

    // ── TLS handshake (SSL* owned by Session) ─────────────────
    std::optional<SSL*> tls_connect(int fd) {
        return static_cast<DerivedClient*>(this)->tls_connect_impl(fd);
    }
    std::optional<SSL*> tls_connect_impl(int fd) {
        SSL* ssl = SSL_new(ctx);
        if (!ssl)
            return std::nullopt;
        SSL_set_fd(ssl, fd);
        SSL_set_tlsext_host_name(ssl, host.c_str());   // SNI

        if (SSL_connect(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
            SSL_free(ssl);
            return std::nullopt;
        }

        std::cout << "TLS connected, cipher: "
                  << SSL_get_cipher(ssl) << "\n";

        // TODO: verify server certificate matches host
        // X509* cert = SSL_get_peer_certificate(ssl);
        // check cert CN or SAN against host

        return ssl;
    }

    // ── WebSocket HTTP upgrade ────────────────────────────────
    bool ws_upgrade(SSL* ssl) {
        return static_cast<DerivedClient*>(this)->ws_upgrade_impl(ssl);
    }
    bool ws_upgrade_impl(SSL* ssl) {
        if (!ssl)
            return false;

        ws_key_ = make_ws_key();

        std::string req =
            std::string("GET ") + path + " HTTP/1.1\r\n"
            + "Host: "                    + host          + "\r\n"
            + "Upgrade: websocket\r\n"
            + "Connection: Upgrade\r\n"
            + "Sec-WebSocket-Key: "       + ws_key_       + "\r\n"
            + "Sec-WebSocket-Version: 13\r\n"
            + "\r\n";

        if (!tls_write(ssl, req.c_str(), req.size())) return false;

        // read HTTP response until \r\n\r\n
        std::string response;
        char buf[BUFSIZE];

        while (response.find("\r\n\r\n") == std::string::npos) {
            int n = SSL_read(ssl, buf, sizeof(buf));
            if (n <= 0) {
                ERR_print_errors_fp(stderr);
                return false;
            }
            response.append(buf, n);

            // TODO: add max size guard — same as server side
            // if response.size() > 8192 → abort
        }
 
        if (response.find("101 Switching Protocols") == std::string::npos) {
            std::cerr << "WS upgrade failed:\n" << response << "\n";
            return false;
        }

        // TODO: verify Sec-WebSocket-Accept header
        // extract accept key from response
        // compute expected = base64(SHA1(ws_key_ + MAGIC))
        // compare — if mismatch server doesn't speak WebSocket
        std::cout << "WebSocket upgrade complete\n";
        return true;
    }

    // ── send WebSocket frame (client→server, must be masked) ──
    bool ws_send(SSL* ssl, std::string_view payload, uint8_t opcode = 0x1) {
        if (!ssl)
            return false;
        std::vector<uint8_t> frame;

        frame.push_back(0x80 | opcode);   // FIN=1 + opcode

        uint8_t mask[4];
        prng().fill(mask, 4);

        size_t len = payload.size();
        if (len < 126) {
            frame.push_back(0x80 | (uint8_t)len);
        } else if (len < 65536) {
            frame.push_back(0x80 | 126);
            frame.push_back((len >> 8) & 0xFF);
            frame.push_back( len       & 0xFF);
        } else {
            frame.push_back(0x80 | 127);
            for (int i = 7; i >= 0; i--)
                frame.push_back((len >> (i*8)) & 0xFF);
        }

        frame.push_back(mask[0]);
        frame.push_back(mask[1]);
        frame.push_back(mask[2]);
        frame.push_back(mask[3]);

        for (size_t i = 0; i < len; i++)
            frame.push_back(payload[i] ^ mask[i % 4]);

        return tls_write(ssl, (char*)frame.data(), frame.size());
    }

    // ── TLS write ─────────────────────────────────────────────
    bool tls_write(SSL* ssl, const char* data, size_t len) {
        if (!ssl)
            return false;
        size_t sent = 0;
        while (sent < len) {
            int w = SSL_write(ssl, data + sent, len - sent);
            if (w <= 0) {
                ERR_print_errors_fp(stderr);
                return false;
            }
            sent += w;
        }
        return true;
    }

    // ── DRAIN one epoll batch for a connection (call from epoll loop; buf is scratch) ──
    template<typename DS>
    void socketfd(DerivedClient* client, Session<DS, DerivedClient>& session, EpollSlot* slot) {
        SSL* ssl = session.get_ssl();
        if (!ssl)
            return;

        uint8_t buf[BUFSIZE];
        for (;;) {
            int r = SSL_read(ssl, buf, BUFSIZE);
            if (r > 0) {
                session.parser_.feed(buf, static_cast<size_t>(r),
                    [&](uint8_t opcode, std::string_view msg) {
                        switch (opcode) {
                            case 0x1: // text
                                session.forward_message(msg);
                                break;
                            case 0x2: // binary
                                session.forward_message(msg);
                                break;

                            case 0x9: // ping
                                ws_send(ssl, msg, 0xA);
                                break;

                            case 0xA: // pong
                                break;

                            case 0x8: // close
                                std::cout << "server sent close\n";
                                ws_send(ssl, "", 0x8);
                                return;

                            default:
                                std::cerr << "unknown opcode: " << (int)opcode << "\n";
                        }
                    });
                continue;
            }

            int err = SSL_get_error(ssl, r);
            if (err == SSL_ERROR_WANT_READ)
                break;

            if (err == SSL_ERROR_ZERO_RETURN) {
                std::cout << "server closed TLS connection\n";
                session.reconnect();
                return;
            }

            if (err == SSL_ERROR_SYSCALL) {
                perror("SSL_read syscall error");
                session.reconnect();
                return;
            }

            ERR_print_errors_fp(stderr);
            return;
        }
    }

    template<typename DS>
    void timerfd(Session<DS, DerivedClient>& session,EpollSlot* slot) {
        if (!slot->ctx)
            return;
        int fd = slot->ctx->tfd_;
        if (fd < 0)
            return;

        uint64_t count = 0;
        bool     drained = false;
        for (;;) {
            ssize_t n = read(fd, &count, sizeof(count));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK)
                    break;
                perror("timerfd read");
                break;
            }
            if (n != sizeof(count))
                break;
            drained = true;
        }
        if (drained)
            session.reconnect();
    }

    void run_loop(DerivedClient* client) {

        epoll_event events[MAX_EPOLL_EVENTS];

        while (!client->isShutdown()) {
            int n = epoll_wait(epfd_, events, MAX_EPOLL_EVENTS, -1);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                perror("epoll_wait");
                break;
            }

            for (int i = 0; i < n; ++i) {
                EpollSlot* slot = static_cast<EpollSlot*>(events[i].data.ptr);
                if (!slot)
                    continue;

                
                if (slot->kind == Kind::Eventfd) {
                    // Someone called write(efd_) to request shutdown; drain before teardown.
                    bool drained = false;
                    for (;;) {
                        uint64_t count = 0;
                        ssize_t r = read(slot->shutdownEfd, &count, sizeof(count));
                        if (r < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK)
                                break;
                            perror("eventfd read");
                            break;
                        }
                        if (r != sizeof(count))
                            break;
                        drained = true;
                    }
                    if (!drained)
                        continue; // spurious wake: nothing to drain
                    static_cast<DerivedClient*>(client)->shutdownReactor();
                    return;
                }

                slot->socket_ready(client, this, slot);
            }
        }
    }

    // ── send heartbeat ping ───────────────────────────────────
    void ping(SSL* ssl) {
        // TODO: call this on a timer — exchanges disconnect
        //       idle connections after 30-60 seconds
        //       set up a periodic ping every 20 seconds
        ws_send(ssl, "ping", 0x9);
    }

    ~WebSocketClient() {
        if (ctx) {
            SSL_CTX_free(ctx);
            ctx = nullptr;
        }
        if (epfd_ >= 0) {
            close(epfd_);
            epfd_ = -1;
        }
        if (efd_ >= 0) {
            close(efd_);
            efd_ = -1;
        }
    }
};

// Session needs WebSocketClient complete (above) and is needed
// complete by the epoll stubs (below). This is the only valid spot.
#include "transport/session.hpp"

template<typename DS, typename DC>
void socket_epoll_stub(void* client, void* base, EpollSlot* slot) {
    auto* c = static_cast<DC*>(client);
    auto* b = static_cast<WebSocketClient<DC>*>(base);
    auto* s = static_cast<Session<DS, DC>*>(slot->session_opaque);
    b->socketfd(c, *s, slot);
}

template<typename DS, typename DC>
void timer_epoll_stub(void* client, void* base, EpollSlot* slot) {
    auto* s = static_cast<Session<DS, DC>*>(slot->session_opaque);
    auto* b = static_cast<WebSocketClient<DC>*>(base);

    b->timerfd(*s, slot);
}