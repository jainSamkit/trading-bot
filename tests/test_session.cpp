#include <gtest/gtest.h>
#include "mock_client.hpp"
#include <sys/timerfd.h>

// ═══════════════════════════════════════════════════════════════════════════════
//  Session::init
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SessionInit, SuccessWithTimer) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);

    EXPECT_TRUE(session.init(true));
    EXPECT_EQ(session.status(), SessionStatus::CONNECTED);
    EXPECT_GE(session.get_fd(), 0);
    EXPECT_GE(session.get_tfd(), 0);
    EXPECT_EQ(client.session_adds, 1);
    EXPECT_EQ(client.connect_attempts, 1);
}

TEST(SessionInit, SuccessWithoutTimer) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);

    EXPECT_TRUE(session.init(false));
    EXPECT_EQ(session.status(), SessionStatus::CONNECTED);
    EXPECT_GE(session.get_fd(), 0);
    EXPECT_LT(session.get_tfd(), 0);
}

TEST(SessionInit, FailsAtTCP) {
    MockClient client;
    client.tcp_succeed = false;
    MockSession session(client, SessionID::L2Update);

    EXPECT_FALSE(session.init());
    EXPECT_NE(session.status(), SessionStatus::CONNECTED);
    EXPECT_EQ(client.connect_attempts, 1);
    EXPECT_LT(session.get_fd(), 0);
}

TEST(SessionInit, FailsAtTLS) {
    MockClient client;
    client.tls_succeed = false;
    MockSession session(client, SessionID::L2Update);

    EXPECT_FALSE(session.init());
    EXPECT_NE(session.status(), SessionStatus::CONNECTED);
    EXPECT_LT(session.get_fd(), 0);
}

TEST(SessionInit, FailsAtWSUpgrade) {
    MockClient client;
    client.ws_succeed = false;
    MockSession session(client, SessionID::L2Update);

    EXPECT_FALSE(session.init());
    EXPECT_NE(session.status(), SessionStatus::CONNECTED);
    EXPECT_LT(session.get_fd(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Session::destroy / disconnect
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SessionDestroy, CleansUpFds) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    EXPECT_GE(session.get_fd(), 0);
    EXPECT_GE(session.get_tfd(), 0);

    session.destroy();
    EXPECT_EQ(session.status(), SessionStatus::DISCONNECTED);
    EXPECT_LT(session.get_fd(), 0);
    EXPECT_LT(session.get_tfd(), 0);
    EXPECT_EQ(client.session_deletes, 1);
}

TEST(SessionDestroy, DoubleDestroyIsSafe) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    session.destroy();
    session.destroy();
    EXPECT_EQ(session.status(), SessionStatus::DISCONNECTED);
}

TEST(SessionDisconnect, KeepsTimerFd) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    int tfd = session.get_tfd();
    session.disconnect();
    EXPECT_EQ(session.status(), SessionStatus::DISCONNECTED);
    EXPECT_LT(session.get_fd(), 0);
    EXPECT_EQ(session.get_tfd(), tfd);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Session::reconnect — state transitions
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Reconnect, FromConnected_DisconnectsAndReconnects) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(false));
    EXPECT_EQ(session.status(), SessionStatus::CONNECTED);

    int old_attempts = client.connect_attempts;
    session.reconnect();

    EXPECT_EQ(session.status(), SessionStatus::CONNECTED);
    EXPECT_EQ(client.connect_attempts, old_attempts + 1);
    EXPECT_EQ(session.reconnect_count(), 0u);
}

TEST(Reconnect, FromDisconnected_Reconnects) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    EXPECT_EQ(session.status(), SessionStatus::DISCONNECTED);

    session.reconnect();
    EXPECT_EQ(session.status(), SessionStatus::CONNECTED);
    EXPECT_EQ(session.reconnect_count(), 0u);
}

TEST(Reconnect, SuccessResetsCounter) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    client.tcp_succeed = false;
    session.reconnect();
    EXPECT_EQ(session.reconnect_count(), 1u);

    client.tcp_succeed = true;
    session.reconnect();
    EXPECT_EQ(session.status(), SessionStatus::CONNECTED);
    EXPECT_EQ(session.reconnect_count(), 0u);
}

TEST(Reconnect, FailArmsTimerWithCorrectDelay) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    client.tcp_succeed = false;
    session.reconnect();
    EXPECT_EQ(session.reconnect_count(), 1u);

    int tfd = session.get_tfd();
    ASSERT_GE(tfd, 0);
    itimerspec ts{};
    timerfd_gettime(tfd, &ts);
    long armed_ms = ts.it_value.tv_sec * 1000 + ts.it_value.tv_nsec / 1000000;
    EXPECT_GT(armed_ms, 0);
    EXPECT_LE(armed_ms, 2000);  // base_wait(1000) + (1<<0)*1000 = 2000
}

TEST(Reconnect, ExponentialBackoffDelays) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    client.tcp_succeed = false;

    for (int i = 0; i < 4; ++i) {
        session.reconnect();
        EXPECT_EQ(session.reconnect_count(), static_cast<uint8_t>(i + 1))
            << "at iteration " << i;

        int tfd = session.get_tfd();
        ASSERT_GE(tfd, 0);
        itimerspec ts{};
        timerfd_gettime(tfd, &ts);
        long armed_ms = ts.it_value.tv_sec * 1000 + ts.it_value.tv_nsec / 1000000;
        EXPECT_GT(armed_ms, 0) << "timer not armed at iteration " << i;
    }
}

TEST(Reconnect, MaxReconnectsExceeded_Destroys) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    client.tcp_succeed = false;

    session.reconnect();
    for (int i = 1; i < 10; ++i)
        session.reconnect();
    EXPECT_EQ(session.reconnect_count(), 10u);

    session.reconnect();
    EXPECT_EQ(session.status(), SessionStatus::DISCONNECTED);
    EXPECT_LT(session.get_fd(), 0);
    EXPECT_LT(session.get_tfd(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  arm_timer_ms
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ArmTimer, SetsCorrectDelay) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    int tfd = session.get_tfd();
    ASSERT_GE(tfd, 0);

    EXPECT_EQ(session.arm_timer_ms(1500), 0);
    itimerspec ts{};
    timerfd_gettime(tfd, &ts);
    long armed_ms = ts.it_value.tv_sec * 1000 + ts.it_value.tv_nsec / 1000000;
    EXPECT_GT(armed_ms, 0);
    EXPECT_LE(armed_ms, 1500);
}

TEST(ArmTimer, InvalidTfdFails) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    EXPECT_LT(session.arm_timer_ms(1000), 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Session callbacks
// ═══════════════════════════════════════════════════════════════════════════════

TEST(SessionCallbacks, ForwardMessage) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);

    session.forward_message("order-book-update");
    session.forward_message("trade");

    ASSERT_EQ(session.messages.size(), 2u);
    EXPECT_EQ(session.messages[0], "order-book-update");
    EXPECT_EQ(session.messages[1], "trade");
}

TEST(SessionCallbacks, HeartbeatIsFiltered) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    ASSERT_TRUE(session.init(true));

    session.forward_message(R"({"type":"heartbeat"})");

    EXPECT_EQ(session.messages.size(), 0u);
}

TEST(SessionCallbacks, Subscribe) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    session.subscribe();
    EXPECT_EQ(session.subscribe_count, 1);
}

TEST(SessionCallbacks, Auth) {
    MockClient client;
    MockSession session(client, SessionID::L2Update);
    session.sendAuth();
    EXPECT_EQ(session.auth_count, 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Shutdown, MockClientShutdownFlag) {
    MockClient client;
    EXPECT_FALSE(client.isShutdown());
    client.shutdownReactor();
    EXPECT_TRUE(client.isShutdown());
}
