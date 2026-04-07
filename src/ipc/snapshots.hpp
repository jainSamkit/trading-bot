#pragma once
#include <cstdint>

struct MarketSnapshot {
    double   best_bid;
    double   best_ask;
    double   mid;
    double   spread;
    uint64_t sequence;
    int64_t  t_ns;
};

struct VolSnapshot {
    double   sigma_sq;
    double   close;
    double   volume;
    uint64_t sequence;
    int64_t  t_ns;
};

struct MarkSnapshot {
    double   price;
    double   upper_limit;
    double   lower_limit;
    uint64_t sequence;
    int64_t  t_ns;
};

struct SpotSnapshot {
    double   price;
    uint64_t sequence;
    int64_t  t_ns;
};

struct PositionSnapshot {
    double   size;           // positive = long, negative = short
    double   entry_price;
    double   unrealised_pnl;
    uint64_t sequence;
    int64_t  t_ns;
};
