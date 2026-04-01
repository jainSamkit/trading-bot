#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

struct Product {
    uint32_t exchange_id            = 0;
    char     symbol[24]             = {};
    char     index_symbol[24]       = {};
    double   tick_size              = 0;
    double   contract_value         = 0;
    double   max_leverage           = 0;
    double   initial_margin         = 0;
    double   maintenance_margin     = 0;
    double   taker_commission_rate  = 0;
    double   maker_commission_rate  = 0;
    uint8_t  internal_id            = 0;
    double   inv_tick_size          = 0;
    double   inv_contract_value     = 0;
    double   lower_bound_price      = 0;
    double   upper_bound_price      = 0;
};

struct ProductTable {
    static constexpr uint8_t MAX_INSTRUMENTS = 64;

    std::array<Product, MAX_INSTRUMENTS> products{};
    uint8_t count = 0;

    uint8_t add(Product product) {
        uint8_t id = count++;
        product.internal_id        = id;
        product.inv_tick_size      = 1.0 / product.tick_size;
        product.inv_contract_value = 1.0 / product.contract_value;
        products[id] = product;
        return id;
    }

    const Product& operator[](uint8_t id) const { return products[id]; }

    uint8_t idfromSymbol(std::string_view sym) const {
        for (uint8_t i = 0; i < count; ++i)
            if (std::string_view(products[i].symbol) == sym)
                return i;
        return UINT8_MAX;
    }

    uint8_t idfromIndexSymbol(std::string_view sym) const {
        for (uint8_t i = 0; i < count; ++i)
            if (std::string_view(products[i].index_symbol) == sym)
                return i;
        return UINT8_MAX;
    }

    uint8_t idfromExchangeID(uint32_t eid) const {
        for (uint8_t i = 0; i < count; ++i)
            if (products[i].exchange_id == eid)
                return i;
        return UINT8_MAX;
    }


};
