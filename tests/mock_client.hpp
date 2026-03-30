#pragma once
#include "transport/wsclient.hpp"
#include "delta_exchange/models/product.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <vector>
#include <string>

struct MockClient : WebSocketClient<MockClient> {
    static constexpr uint64_t HEARTBEAT_TIMEOUT_MS = 35000;
    bool tcp_succeed = true;
    bool tls_succeed = true;
    bool ws_succeed  = true;

    int  connect_attempts  = 0;
    int  session_adds      = 0;
    int  session_deletes   = 0;
    bool shutdown_         = false;

    ProductTable products_;

    std::optional<int> tcp_connect_impl() {
        ++connect_attempts;
        if (!tcp_succeed) return std::nullopt;
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0)
            return std::nullopt;
        ::close(fds[1]);
        return fds[0];
    }

    std::optional<SSL*> tls_connect_impl(int /*fd*/) {
        if (!tls_succeed) return std::nullopt;
        return std::optional<SSL*>{nullptr};
    }

    bool ws_upgrade_impl(SSL* /*ssl*/) {
        return ws_succeed;
    }

    bool onsessionAdd(SessionCtx& /*ctx*/, EpollSlot& /*s*/, EpollSlot& /*t*/) {
        ++session_adds;
        return true;
    }

    void onsessionDelete(SessionCtx& /*ctx*/) {
        ++session_deletes;
    }

    bool isShutdown() const { return shutdown_; }
    void shutdownReactor()  { shutdown_ = true; }
};

struct MockSession : Session<MockSession, MockClient> {
    std::vector<std::string> messages;
    int subscribe_count = 0;
    int auth_count      = 0;

    explicit MockSession(MockClient& client, SessionID id)
        : Session<MockSession, MockClient>(client, id) {}

    void onMessage(std::string_view msg) { messages.emplace_back(msg); }
    void onSubscribe()                   { ++subscribe_count; }
    void onAuth()                        { ++auth_count; }
};
