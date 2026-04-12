#pragma once
#include <cstdint>
#include <cstring>

struct Wallet {
    uint64_t    id                          = 0;
    char        asset_symbol[16]            = {};   // e.g. "USD", "BTC"

    double      balance                     = 0.0;  // total wallet balance
    double      available_balance           = 0.0;  // free to trade/withdraw
    double      blocked_margin              = 0.0;  // total blocked
    double      order_margin                = 0.0;  // blocked in open orders
    double      position_margin             = 0.0;  // blocked in open positions
    double      cross_position_margin       = 0.0;
    double      cross_order_margin          = 0.0;
    double      cross_locked_collateral     = 0.0;
    double      cross_commission            = 0.0;
    double      cross_asset_liability       = 0.0;
    double      commission                  = 0.0;  // pending fees
    double      portfolio_margin            = 0.0;
    double      trading_fee_credit          = 0.0;
    double      pending_trading_fee_credit  = 0.0;
    double      referral_bonus              = 0.0;
    double      pending_referral_bonus      = 0.0;

    uint64_t    timestamp                   = 0;
};
