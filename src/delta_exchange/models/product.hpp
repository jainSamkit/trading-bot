#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>
#include <vector>


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
};

template<uint8_t N>
struct ProductTableImpl {
    static constexpr uint8_t MAX_INSTRUMENTS = N;

    std::array<Product, N> products{};
    uint8_t count = 0;

    uint8_t add(Product product) {
        uint8_t id = count++;
        if (id >= N) throw std::runtime_error("ProductTable: max products exceeded");
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
using ProductTable = ProductTableImpl<64>;
