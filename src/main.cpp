#include "delta_exchange/client.hpp"

#include <cstdlib>
#include <csignal>
#include <iostream>
#include <string>

// Delta Exchange WebSocket — see https://docs.delta.exchange/#subscribing-to-channels
//
// URLs in the docs are wss:// (TLS). The port is not spelled out because wss uses the
// same default as HTTPS: 443. Pass that explicitly here (our client uses host + port, not a full URL).
//
// Official endpoints (from docs):
//   Production (India): wss://socket.india.delta.exchange
//   Testnet (demo):     wss://socket-ind.testnet.deltaex.org
//
// Usage:
//   ./trading_bot [host] [port] [path] [channel] [symbol]
// Example (testnet L2):
//   ./trading_bot socket-ind.testnet.deltaex.org 443 / l2_orderbook BTCUSD

static DeltaWebsocketClient* g_client = nullptr;

static void on_signal(int)
{
    if (g_client)
        g_client->shutdown();
}

int main(int argc, char** argv)
{
    // Hostname only — no "wss://" (that is implied by TLS + port 443 in our stack).
    std::string host    = "socket.india.delta.exchange";
    int         port    = 443;
    std::string path    = "/";
    std::string channel = "l2_updates";
    std::string symbol  = "ADAUSD";

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);
    if (argc >= 4) path = argv[3];
    if (argc >= 5) channel = argv[4];
    if (argc >= 6) symbol = argv[5];

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    DeltaWebsocketClient client(host.c_str(), port, path.c_str());
    g_client = &client;

    client.setL2Subscription(channel, symbol);

    std::cerr << "[delta] " << host << ":" << port << path
              << "  subscribe " << channel << " " << symbol << "\n";

    client.start();

    std::cerr << "[delta] exit\n";
    return 0;
}
