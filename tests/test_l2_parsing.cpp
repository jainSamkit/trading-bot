#include <gtest/gtest.h>
#include "delta_exchange/client.hpp"
#include "simdjson.h"
#include <cstring>
#include <string>

// ═══════════════════════════════════════════════════════════════════════════════
//  toDouble
// ═══════════════════════════════════════════════════════════════════════════════

TEST(L2Parsing, ToDouble_Price) {
    EXPECT_DOUBLE_EQ(L2UpdateSession::toDouble("68004.5"), 68004.5);
}

TEST(L2Parsing, ToDouble_SmallDecimal) {
    EXPECT_DOUBLE_EQ(L2UpdateSession::toDouble("0.001"), 0.001);
}

TEST(L2Parsing, ToDouble_Size) {
    EXPECT_DOUBLE_EQ(L2UpdateSession::toDouble("5.685"), 5.685);
}

TEST(L2Parsing, ToDouble_Zero) {
    EXPECT_DOUBLE_EQ(L2UpdateSession::toDouble("0"), 0.0);
}

TEST(L2Parsing, ToDouble_Integer) {
    EXPECT_DOUBLE_EQ(L2UpdateSession::toDouble("42000"), 42000.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  convertToTick
// ═══════════════════════════════════════════════════════════════════════════════

TEST(L2Conversion, AskPriceToTick) {
    L2Update l2{};
    l2.ask_count = 1;
    l2.asks[0] = {68004.5, 5.685};

    Product p{};
    p.inv_tick_size      = 1.0 / 0.5;   // tick=0.5
    p.inv_contract_value = 1.0 / 0.001;  // contract=0.001

    L2UpdateSession::convertToTick(l2, p);

    EXPECT_DOUBLE_EQ(l2.asks[0].price, 136009.0);
    EXPECT_DOUBLE_EQ(l2.asks[0].size,  5685.0);
}

TEST(L2Conversion, BidPriceToTick) {
    L2Update l2{};
    l2.bid_count = 1;
    l2.bids[0] = {68004.0, 2.0};

    Product p{};
    p.inv_tick_size      = 1.0 / 0.5;
    p.inv_contract_value = 1.0 / 0.001;

    L2UpdateSession::convertToTick(l2, p);

    EXPECT_DOUBLE_EQ(l2.bids[0].price, 136008.0);
    EXPECT_DOUBLE_EQ(l2.bids[0].size,  2000.0);
}

TEST(L2Conversion, MultipleLevels) {
    L2Update l2{};
    l2.ask_count = 2;
    l2.asks[0] = {100.0, 10.0};
    l2.asks[1] = {100.5, 20.0};
    l2.bid_count = 2;
    l2.bids[0] = {99.5, 15.0};
    l2.bids[1] = {99.0, 25.0};

    Product p{};
    p.inv_tick_size      = 1.0 / 0.5;
    p.inv_contract_value = 1.0;

    L2UpdateSession::convertToTick(l2, p);

    EXPECT_DOUBLE_EQ(l2.asks[0].price, 200.0);
    EXPECT_DOUBLE_EQ(l2.asks[1].price, 201.0);
    EXPECT_DOUBLE_EQ(l2.bids[0].price, 199.0);
    EXPECT_DOUBLE_EQ(l2.bids[1].price, 198.0);
}

TEST(L2Conversion, ZeroCountSkipsConversion) {
    L2Update l2{};
    l2.ask_count = 0;
    l2.bid_count = 0;
    l2.asks[0] = {999.0, 999.0};

    Product p{};
    p.inv_tick_size      = 2.0;
    p.inv_contract_value = 1000.0;

    L2UpdateSession::convertToTick(l2, p);

    EXPECT_DOUBLE_EQ(l2.asks[0].price, 999.0);
    EXPECT_DOUBLE_EQ(l2.asks[0].size,  999.0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  parseLevels (via simdjson)
// ═══════════════════════════════════════════════════════════════════════════════

static uint8_t parse_asks_from_json(const std::string& json_src, L2Level* levels) {
    std::string padded = json_src;
    padded.resize(padded.size() + simdjson::SIMDJSON_PADDING, '\0');

    simdjson::ondemand::parser parser;
    size_t real_len = json_src.size();
    auto doc = parser.iterate(padded.data(), real_len, padded.size());
    if (doc.error()) return 0;

    for (auto field : doc.get_object()) {
        std::string_view key;
        if (field.unescaped_key().get(key)) continue;
        if (key == "asks")
            return L2UpdateSession::parseLevels(field.value(), levels);
    }
    return 0;
}

TEST(L2ParseLevels, TwoLevels) {
    L2Level levels[32]{};
    uint8_t count = parse_asks_from_json(
        R"({"asks":[["68004.5","5.685"],["68005.0","1.0"]]})", levels);

    ASSERT_EQ(count, 2);
    EXPECT_DOUBLE_EQ(levels[0].price, 68004.5);
    EXPECT_DOUBLE_EQ(levels[0].size,  5.685);
    EXPECT_DOUBLE_EQ(levels[1].price, 68005.0);
    EXPECT_DOUBLE_EQ(levels[1].size,  1.0);
}

TEST(L2ParseLevels, EmptyArray) {
    L2Level levels[32]{};
    uint8_t count = parse_asks_from_json(R"({"asks":[]})", levels);
    EXPECT_EQ(count, 0);
}

TEST(L2ParseLevels, SingleLevel) {
    L2Level levels[32]{};
    uint8_t count = parse_asks_from_json(
        R"({"asks":[["42000.0","0.5"]]})", levels);

    ASSERT_EQ(count, 1);
    EXPECT_DOUBLE_EQ(levels[0].price, 42000.0);
    EXPECT_DOUBLE_EQ(levels[0].size,  0.5);
}

TEST(L2ParseLevels, ManyLevelsClampedAt32) {
    std::string json = R"({"asks":[)";
    for (int i = 0; i < 40; ++i) {
        if (i > 0) json += ',';
        json += R"([")" + std::to_string(100 + i) + R"(.0","1.0"])";
    }
    json += "]}";

    L2Level levels[32]{};
    uint8_t count = parse_asks_from_json(json, levels);
    EXPECT_EQ(count, 32);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Full L2 JSON → L2Update (field extraction)
// ═══════════════════════════════════════════════════════════════════════════════

TEST(L2FullParse, SnapshotMessage) {
    ProductTable products;
    products.add({.exchange_id = 27, .symbol = "BTCUSD",
                  .tick_size = 0.5, .contract_value = 0.001});

    std::string json =
        R"({"action":"snapshot","symbol":"BTCUSD","sequence_no":100,)"
        R"("timestamp":1700000000,"asks":[["68004.5","5.685"]],)"
        R"("bids":[["68004.0","2.0"]]})";

    std::string padded = json;
    padded.resize(padded.size() + simdjson::SIMDJSON_PADDING, '\0');

    simdjson::ondemand::parser parser;
    auto doc = parser.iterate(padded.data(), json.size(), padded.size());
    ASSERT_FALSE(doc.error());

    L2Update l2{};
    for (auto field : doc.get_object()) {
        std::string_view key;
        if (field.unescaped_key().get(key)) continue;

        if (key == "action") {
            std::string_view action;
            if (field.value().get_string().get(action)) continue;
            l2.isSnapshot = (action == "snapshot");
        } else if (key == "asks") {
            l2.ask_count = L2UpdateSession::parseLevels(field.value(), l2.asks);
        } else if (key == "bids") {
            l2.bid_count = L2UpdateSession::parseLevels(field.value(), l2.bids);
        } else if (key == "sequence_no") {
            if (field.value().get_uint64().get(l2.sequence_no)) continue;
        } else if (key == "symbol") {
            std::string_view sym;
            if (field.value().get_string().get(sym)) continue;
            l2.instrument_id = products.idfromSymbol(sym);
        } else if (key == "timestamp") {
            if (field.value().get_uint64().get(l2.timestamp)) continue;
        }
    }

    EXPECT_TRUE(l2.isSnapshot);
    EXPECT_EQ(l2.instrument_id, 0);
    EXPECT_EQ(l2.sequence_no, 100u);
    EXPECT_EQ(l2.timestamp, 1700000000u);

    ASSERT_EQ(l2.ask_count, 1);
    EXPECT_DOUBLE_EQ(l2.asks[0].price, 68004.5);
    EXPECT_DOUBLE_EQ(l2.asks[0].size,  5.685);

    ASSERT_EQ(l2.bid_count, 1);
    EXPECT_DOUBLE_EQ(l2.bids[0].price, 68004.0);
    EXPECT_DOUBLE_EQ(l2.bids[0].size,  2.0);

    const Product& p = products[l2.instrument_id];
    L2UpdateSession::convertToTick(l2, p);

    EXPECT_DOUBLE_EQ(l2.asks[0].price, 136009.0);
    EXPECT_DOUBLE_EQ(l2.asks[0].size,  5685.0);
    EXPECT_DOUBLE_EQ(l2.bids[0].price, 136008.0);
    EXPECT_DOUBLE_EQ(l2.bids[0].size,  2000.0);
}

TEST(L2FullParse, UpdateMessage) {
    ProductTable products;
    products.add({.exchange_id = 27, .symbol = "BTCUSD",
                  .tick_size = 0.5, .contract_value = 0.001});

    std::string json =
        R"({"action":"update","symbol":"BTCUSD","sequence_no":101,)"
        R"("timestamp":1700000001,"asks":[["68005.0","3.0"]],)"
        R"("bids":[]})";

    std::string padded = json;
    padded.resize(padded.size() + simdjson::SIMDJSON_PADDING, '\0');

    simdjson::ondemand::parser parser;
    auto doc = parser.iterate(padded.data(), json.size(), padded.size());
    ASSERT_FALSE(doc.error());

    L2Update l2{};
    for (auto field : doc.get_object()) {
        std::string_view key;
        if (field.unescaped_key().get(key)) continue;

        if (key == "action") {
            std::string_view action;
            if (field.value().get_string().get(action)) continue;
            l2.isSnapshot = (action == "snapshot");
        } else if (key == "asks") {
            l2.ask_count = L2UpdateSession::parseLevels(field.value(), l2.asks);
        } else if (key == "bids") {
            l2.bid_count = L2UpdateSession::parseLevels(field.value(), l2.bids);
        } else if (key == "sequence_no") {
            if (field.value().get_uint64().get(l2.sequence_no)) continue;
        } else if (key == "symbol") {
            std::string_view sym;
            if (field.value().get_string().get(sym)) continue;
            l2.instrument_id = products.idfromSymbol(sym);
        } else if (key == "timestamp") {
            if (field.value().get_uint64().get(l2.timestamp)) continue;
        }
    }

    EXPECT_FALSE(l2.isSnapshot);
    EXPECT_EQ(l2.instrument_id, 0);
    EXPECT_EQ(l2.sequence_no, 101u);
    ASSERT_EQ(l2.ask_count, 1);
    EXPECT_EQ(l2.bid_count, 0);
}

TEST(L2FullParse, UnknownSymbolReturnsInvalid) {
    ProductTable products;
    products.add({.exchange_id = 27, .symbol = "BTCUSD",
                  .tick_size = 0.5, .contract_value = 0.001});

    std::string json =
        R"({"action":"snapshot","symbol":"XYZUSD","sequence_no":1,)"
        R"("timestamp":0,"asks":[],"bids":[]})";

    std::string padded = json;
    padded.resize(padded.size() + simdjson::SIMDJSON_PADDING, '\0');

    simdjson::ondemand::parser parser;
    auto doc = parser.iterate(padded.data(), json.size(), padded.size());
    ASSERT_FALSE(doc.error());

    L2Update l2{};
    for (auto field : doc.get_object()) {
        std::string_view key;
        if (field.unescaped_key().get(key)) continue;
        if (key == "symbol") {
            std::string_view sym;
            if (field.value().get_string().get(sym)) continue;
            l2.instrument_id = products.idfromSymbol(sym);
        }
    }

    EXPECT_EQ(l2.instrument_id, UINT8_MAX);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  ProductTable
// ═══════════════════════════════════════════════════════════════════════════════

TEST(ProductTable, AddAndLookup) {
    ProductTable products;
    uint8_t id = products.add({
        .exchange_id    = 27,
        .symbol         = "BTCUSD",
        .tick_size      = 0.5,
        .contract_value = 0.001,
    });

    EXPECT_EQ(id, 0);
    EXPECT_EQ(products.count, 1);
    EXPECT_EQ(products[0].exchange_id, 27u);
    EXPECT_STREQ(products[0].symbol, "BTCUSD");
    EXPECT_DOUBLE_EQ(products[0].tick_size, 0.5);
    EXPECT_DOUBLE_EQ(products[0].inv_tick_size, 2.0);
    EXPECT_DOUBLE_EQ(products[0].inv_contract_value, 1000.0);
    EXPECT_EQ(products[0].internal_id, 0);
}

TEST(ProductTable, LookupBySymbol) {
    ProductTable products;
    products.add({.exchange_id = 27, .symbol = "BTCUSD",
                  .tick_size = 0.5, .contract_value = 0.001});
    products.add({.exchange_id = 139, .symbol = "SOLUSD",
                  .tick_size = 0.01, .contract_value = 1.0});

    EXPECT_EQ(products.idfromSymbol("BTCUSD"), 0);
    EXPECT_EQ(products.idfromSymbol("SOLUSD"), 1);
    EXPECT_EQ(products.idfromSymbol("ETHUSD"), UINT8_MAX);
}

TEST(ProductTable, LookupByExchangeID) {
    ProductTable products;
    products.add({.exchange_id = 27, .symbol = "BTCUSD",
                  .tick_size = 0.5, .contract_value = 0.001});
    products.add({.exchange_id = 139, .symbol = "SOLUSD",
                  .tick_size = 0.01, .contract_value = 1.0});

    EXPECT_EQ(products.idfromExchangeID(27), 0);
    EXPECT_EQ(products.idfromExchangeID(139), 1);
    EXPECT_EQ(products.idfromExchangeID(999), UINT8_MAX);
}
