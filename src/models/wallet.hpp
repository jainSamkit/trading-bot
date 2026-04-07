#pragma once
#include <cstdint>
#include <string>

enum class TransactionType {
    PnL,
    Deposit,
    Withdrawal,
    Commission,
    Conversion,
    PerpetualFuturesFunding,
    WithdrawalCancellation,
    ReferralBonus,
    CommissionRebate,
    PromoCredit,
};

// REST model for /wallet/balances
struct Wallet {
    int64_t     asset_id            = 0;
    std::string asset_symbol;               // e.g. "BTC", "USD"

    std::string balance;                    // BigDecimal string; total wallet balance
    std::string order_margin;               // BigDecimal string; blocked in open orders
    std::string position_margin;           // BigDecimal string; blocked in open positions
    std::string commission;                 // BigDecimal string; blocked for pending fees
    std::string available_balance;          // BigDecimal string; free to withdraw or trade
};

// REST model for /wallet/transactions
struct Transaction {
    int64_t         id              = 0;
    int64_t         user_id         = 0;

    std::string     amount;                 // BigDecimal string; positive = credit, negative = debit
    std::string     balance;                // BigDecimal string; net balance after this transaction

    TransactionType transaction_type = TransactionType::Deposit;

    // optional — set when transaction relates to a contract (e.g. PnL, funding)
    uint32_t        product_id      = 0;
    std::string     product_symbol;

    std::string     meta_data;              // raw JSON string for extra context

    int64_t         created_at      = 0;    // microseconds since epoch
};
