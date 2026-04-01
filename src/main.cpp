#include "feed/client.hpp"
#include "market_state/market_state.hpp"
#include <atomic>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>

static DeltaWebsocketClient* g_client  = nullptr;
static std::atomic<bool>     g_running{true};

static void on_signal(int)
{
    g_running.store(false, std::memory_order_relaxed);
    if (g_client)
        g_client->shutdown();
}

int main(int argc, char** argv)
{
    ProductTable products;
    products.add({
        .exchange_id       = 27,
        .symbol            = "BTCUSD",
        .index_symbol      = ".DEXBTUSD",
        .tick_size         = 0.5,
        .contract_value    = 1.0,
        .lower_bound_price = 0.0,
        .upper_bound_price = 200000.0,
    });
    // products.add({
    //     .exchange_id       = 139,
    //     .symbol            = "SOLUSD",
    //     .tick_size         = 0.001,
    //     .contract_value    = 1.0,
    //     .lower_bound_price = 0.0,
    //     .upper_bound_price = 100.0,
    // });

    auto feedRing    = std::make_unique<SpscRing<FeedMessage, 4096>>();
    auto marketState = std::make_unique<MarketState>(*feedRing, products);

    std::string host = "socket.india.delta.exchange";
    int         port = 443;
    std::string path = "/";

    if (argc >= 2) host = argv[1];
    if (argc >= 3) port = std::atoi(argv[2]);
    if (argc >= 4) path = argv[3];

    std::signal(SIGINT,  on_signal);
    std::signal(SIGTERM, on_signal);

    // market state spins on its own thread
    std::thread market_thread([&]() {
        marketState->run(g_running);
    });

    DeltaWebsocketClient client(host.c_str(), port, path.c_str(), products, feedRing.get());
    g_client = &client;

    std::cerr << "[delta] " << host << ":" << port << path << "\n";

    client.start();   // blocks until shutdown signal

    g_running.store(false, std::memory_order_relaxed);
    market_thread.join();

    std::cerr << "[delta] exit\n";
    return 0;
}
