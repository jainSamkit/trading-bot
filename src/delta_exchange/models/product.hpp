#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>
#include "core/tick.hpp"
#include "core/contracts.hpp"
#include "config/config.hpp"

struct ProductGroup {
    const char* name_;
    std::vector<uint8_t> instrument_ids; 
};

struct Product {
    enum class ContractType : uint8_t { Futures, Options, Perpetual };
    enum class NotionalType : uint8_t { Vanilla, Inverse };

    uint32_t        exchange_id                         = 0;
    uint32_t        internal_id                         = 0;

    char            symbol[24]       = {};
    char            index_symbol[24] = {};
    ContractType    contract_type    = ContractType::Futures;

    double          initial_margin                      = 0.0;
    double          maintenance_margin                  = 0.0;
    double          initial_margin_scaling_factor       = 0.0;
    double          maintenance_margin_scaling_factor   = 0.0;
    double          default_leverage                    = 0.0;
    double          max_leverage_notional               = 0.0;

    // contract specs
    double          contract_value                      = 0.0;
    double          inv_contract_value                  = 0.0;
    double          tick_size                           = 0.0;
    double          inv_tick_size                       = 0.0;
    uint32_t        impact_size                         = 0;
    uint32_t        position_size_limit                 = 0;

    // fees
    double          maker_commission_rate               = 0.0;
    double          taker_commission_rate               = 0.0;

    // price limits
    double          price_band                          = 0.0;   // % range around mark price
    double          lower_bound_price                   = 0.0;
    double          upper_bound_price                   = 0.0;

    // conversions — live on Product since they need this product's tick_size / contract_value
    Tick price_to_tick(double price) const noexcept {
        return Tick{static_cast<int32_t>(std::lround(price * inv_tick_size))};
    }
    double tick_to_price(Tick t) const noexcept {
        return to_int(t) * tick_size;
    }
    Contracts to_contracts(double size) const noexcept {
        return Contracts{static_cast<int32_t>(std::lround(size))};
    }
    int32_t contracts_to_int(Contracts c) const noexcept {
        return to_int(c);
    }
};


struct ProductTableImpl {
    static constexpr uint8_t MAX_INSTRUMENTS = cfg::MAX_INSTRUMENTS;

    std::array<Product, MAX_INSTRUMENTS> products{};
    uint8_t count = 0;

    uint8_t add(Product product) {
        uint8_t id = count++;
        if (id >= MAX_INSTRUMENTS) throw std::runtime_error("ProductTable: max products exceeded");
        product.internal_id        = id;
        product.inv_tick_size      = 1.0 / product.tick_size;
        product.inv_contract_value = 1.0 / product.contract_value;
        products[id] = product;
        return id;
    }

    const Product& operator[](uint8_t id) const { return products[id]; }

    uint8_t idfromSymbol(std::string_view sym) const {
        for (uint8_t i = 0; i < count; ++i)
            if (std::string_view(products[i].symbol) == sym) return i;
        return UINT8_MAX;
    }

    uint8_t idfromIndexSymbol(std::string_view sym) const {
        for (uint8_t i = 0; i < count; ++i)
            if (std::string_view(products[i].index_symbol) == sym) return i;
        return UINT8_MAX;
    }

    uint8_t idfromExchangeID(uint32_t eid) const {
        for (uint8_t i = 0; i < count; ++i)
            if (products[i].exchange_id == eid) return i;
        return UINT8_MAX;
    }

};

/// Fixed capacity (64) table used by feed, WS client, and market state.
using ProductTable = ProductTableImpl;
