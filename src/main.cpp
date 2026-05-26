#include "delta_exchange/api/product/product.hpp"
#include "market_state/market_state.hpp"
#include "processes/feed.hpp"
#include "processes/oms.hpp"
#include "processes/strategy.hpp"
#include "ipc/shm.hpp"
#include "config/env.hpp"
#include "latency/clock.hpp"
#include <sys/wait.h>
#include <signal.h>
#include <atomic>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>


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
    // Load .env from cwd (no error if missing — real shell env still works).
    // Shell env wins over .env values (overwrite=0 inside the loader).
    env::load_file(".env");

    // Calibrate the latency clock ONCE in the parent, BEFORE fork(). Each
    // child inherits the calibrated ns_per_cycle via COW; otherwise every
    // Span records 0 ns and percentiles flatline at 1. (See clock.hpp.)
    latency::calibrate();

    const std::string delta_rest_host       = env::get_or("DELTA_REST_HOST",       "api.india.delta.exchange");
    const std::string delta_ws_public_host  = env::get_or("DELTA_WS_PUBLIC_HOST",  "public-socket.india.delta.exchange");
    const std::string delta_ws_private_host = env::get_or("DELTA_WS_PRIVATE_HOST", "socket.india.delta.exchange");

    ProductTable products;
    std::vector<std::string> allowed_products {"BTCUSD", "ETHUSD", "SOLUSD"};
    DeltaRestClient rest(delta_rest_host.c_str(), "", "", products);
    rest.connect();

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
    // ProductGroup sol_group {"SOL", {products.idfromSymbol("SOLUSD")}};
    // ProductGroup eth_group {"ETH", {products.idfromSymbol("ETHUSD")}};

    auto shm_trading_bot = ShmOwner<SharedState>::create("/trading_bot_state");
    SharedState* state = shm_trading_bot.get();

    FeedConfig feed_cfg_ {

        .host = delta_ws_public_host,
        .port = 443,
        .path = "/",
    };

    latency::InfluxWriter::Config influx_config {
        .host   = env::get_or    ("INFLUX_HOST",   "127.0.0.1"),
        .port   = env::get_int_or("INFLUX_PORT",   8086),
        .org    = env::require   ("INFLUX_ORG"),
        .bucket = env::require   ("INFLUX_BUCKET"),
        .token  = env::require   ("INFLUX_TOKEN"),
    };

    FeedProcess<DeltaWebsocketClient> btc_feed {
        products, btc_group, state, feed_cfg_,
        influx_config, core::Venue::Delta,
    };
    // FeedProcess<DeltaWebsocketClient> eth_feed {products, eth_group, state, cfg};


    // ── OMS (private session) ─────────────────────────────────────────────
    // Credentials come from .env via DELTA_API_KEY / DELTA_API_SECRET.
    // Host is testnet or prod depending on DELTA_WS_PRIVATE_HOST.
    
    OMSConfig oms_cfg_ {
        .ws_host       = delta_ws_private_host,
        .rest_host     = delta_rest_host,
        .port       = 443,
        .path       = "/",
        .api_key    = env::require("DELTA_API_KEY"),
        .api_secret = env::require("DELTA_API_SECRET"),
    };

    OmsProcess<DeltaOMSWebsocketClient, DeltaRestClient> oms_delta {products, btc_group, influx_config, oms_cfg_, state, ExecMode::Shadow, core::Venue::Delta};

    // ── Strategy (reads market_state SHM, emits ExecutionIntents) ─────────────
    Strategy strategy_delta {products, btc_group, state, core::Venue::Delta};

    std::vector<FeedProcess<DeltaWebsocketClient>*> feeds;

    feeds.push_back(&btc_feed);
    // feeds.push_back(&eth_feed);
    // feeds.push_back(&sol_feed);
    // FeedProcess<DeltaWebsocketClient> sol_feed {products, sol_group, state, cfg};

    for(auto* feed : feeds) {
        pid_t pid = fork();
        if(pid == 0) {
            feed->start();
            return 0;
        }
        g_child_pids.push_back(pid);
    }

    pid_t pid = fork();
    if(pid == 0) {
        oms_delta.start();
        return 0;
    }

    g_child_pids.push_back(pid);

    pid_t strat_pid = fork();
    if(strat_pid == 0) {
        strategy_delta.start();
        return 0;
    }

    g_child_pids.push_back(strat_pid);

    std::signal(SIGINT, on_shutdown_signal);
    std::signal(SIGTERM, on_shutdown_signal);

    for (size_t i = 0; i < g_child_pids.size(); ++i) {
        int status;
        (void)waitpid(-1, &status, 0);
    }

    std::cerr << "[delta] exit\n";
    return 0;
}
