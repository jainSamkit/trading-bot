#include "delta_exchange/product_fetcher.hpp"
#include "market_state/market_state.hpp"
#include "models/product.hpp"
#include "processes/feed.hpp"
#include "ipc/shm.hpp"
#include <sys/wait.h>
#include <signal.h>
#include <atomic>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>


static std::vector<FeedProcess<DeltaWebsocketClient>*> g_feeds;

static void on_signal(int)
{
    for(auto* f : g_feeds) f->stop();
    // g_running.store(false, std::memory_order_relaxed);
    // if (g_client)
        // g_client->shutdown();
}

// Signal handlers only receive int sig — store PIDs in process-global state.
static std::vector<pid_t> g_child_pids;

static void on_shutdown_signal(int sig)
{
    (void)sig;
    for (pid_t pid : g_child_pids) {
        if (pid > 0)
            kill(pid, SIGTERM);
    }
}


static void print_product(const Product& p) {
    std::cout << "┌─ Product ─────────────────────────\n"
              << "│ symbol:           " << p.symbol            << "\n"
              << "│ index_symbol:     " << p.index_symbol      << "\n"
              << "│ exchange_id:      " << p.exchange_id       << "\n"
              << "│ contract_type:    " << (p.contract_type == Product::ContractType::Perpetual ? "perpetual" :
                                           p.contract_type == Product::ContractType::Futures   ? "futures"   : "options") << "\n"
              << "│ tick_size:        " << p.tick_size         << "\n"
              << "│ contract_value:   " << p.contract_value    << "\n"
              << "│ impact_size:      " << p.impact_size       << "\n"
              << "│ pos_size_limit:   " << p.position_size_limit << "\n"
              << "│ taker_fee:        " << p.taker_commission_rate << "\n"
              << "│ maker_fee:        " << p.maker_commission_rate << "\n"
              << "│ initial_margin:   " << p.initial_margin    << "\n"
              << "│ maint_margin:     " << p.maintenance_margin << "\n"
              << "│ default_leverage: " << p.default_leverage  << "\n"
              << "│ price_band:       " << p.price_band        << "%\n"
              << "└───────────────────────────────────\n";
}

int main(int argc, char** argv)
{
    std::vector<std::string> allowed_products {"BTCUSD", "ETHUSD", "SOLUSD"};
    DeltaRestClient rest("api.india.delta.exchange", "", "");
    rest.connect();

    ProductTable products;

    for (const std::string& sym : allowed_products) {
        try {
            products.add(fetch_product(rest, sym));
            print_product(products[products.count - 1]);
        } catch (const std::exception& e) {
            std::cerr << "[init] failed to fetch " << sym << ": " << e.what()
                      << "\n";
            return 1;
        }
    }

    ProductGroup btc_group {"BTC", {products.idfromSymbol("BTCUSD")}};
    ProductGroup sol_group {"SOL", {products.idfromSymbol("SOLUSD")}};
    ProductGroup eth_group {"ETH", {products.idfromSymbol("ETHUSD")}};

    auto shm_trading_bot = ShmOwner<SharedState>::create("/trading_bot_state");
    SharedState* state = shm_trading_bot.get();

    
    FeedConfig cfg {.host = "socket.india.delta.exchange", .port = 443, .path = "/"};
    FeedProcess<DeltaWebsocketClient> btc_feed {products, btc_group, state, cfg};
    FeedProcess<DeltaWebsocketClient> eth_feed {products, eth_group, state, cfg};

    // OmsProcess<DeltaExchange>
    

    std::vector<FeedProcess<DeltaWebsocketClient>*> feeds;

    feeds.push_back(&btc_feed);
    feeds.push_back(&eth_feed);
    // FeedProcess<DeltaWebsocketClient> sol_feed {products, sol_group, state, cfg};

    for(auto* feed : feeds) {
        pid_t pid = fork();
        if(pid == 0) {
            feed->start();
            return 0;
        }
        g_child_pids.push_back(pid);
    }

    std::signal(SIGINT, on_shutdown_signal);
    std::signal(SIGTERM, on_shutdown_signal);

    for (size_t i = 0; i < g_child_pids.size(); ++i) {
        int status;
        (void)waitpid(-1, &status, 0);
    }

    std::cerr << "[delta] exit\n";
    return 0;
}
