#include <gtest/gtest.h>
#include "transport/wsclient.hpp"
#include <vector>
#include <string>
#include <cstring>
#include <utility>

namespace {

std::vector<uint8_t> make_frame(uint8_t opcode, const std::string& payload,
                                bool fin = true, bool masked = false,
                                const uint8_t mask_key[4] = nullptr) {
    std::vector<uint8_t> frame;
    uint8_t b0 = opcode;
    if (fin) b0 |= 0x80;
    frame.push_back(b0);

    size_t len = payload.size();
    uint8_t b1 = masked ? 0x80 : 0x00;
    if (len < 126) {
        frame.push_back(b1 | static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(b1 | 126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(b1 | 127);
        for (int i = 7; i >= 0; --i)
            frame.push_back((len >> (i * 8)) & 0xFF);
    }

    if (masked && mask_key) {
        frame.insert(frame.end(), mask_key, mask_key + 4);
        for (size_t i = 0; i < len; ++i)
            frame.push_back(static_cast<uint8_t>(payload[i]) ^ mask_key[i % 4]);
    } else {
        frame.insert(frame.end(), payload.begin(), payload.end());
    }
    return frame;
}

using Frame = std::pair<uint8_t, std::string>;

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  Basic frame types
// ═══════════════════════════════════════════════════════════════════════════════

TEST(WSParser, TextFrame) {
    WSParser p;
    std::vector<Frame> out;
    auto f = make_frame(0x1, "hello");
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].first, 0x1);
    EXPECT_EQ(out[0].second, "hello");
}

TEST(WSParser, BinaryFrame) {
    WSParser p;
    std::vector<Frame> out;
    std::string data = std::string("\x00\x01\x02", 3);
    auto f = make_frame(0x2, data);
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].first, 0x2);
    EXPECT_EQ(out[0].second, data);
}

TEST(WSParser, PingFrame) {
    WSParser p;
    std::vector<Frame> out;
    auto f = make_frame(0x9, "ping-payload");
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].first, 0x9);
    EXPECT_EQ(out[0].second, "ping-payload");
}

TEST(WSParser, PongFrame) {
    WSParser p;
    std::vector<Frame> out;
    auto f = make_frame(0xA, "");
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].first, 0xA);
}

TEST(WSParser, CloseFrame) {
    WSParser p;
    std::vector<Frame> out;
    auto f = make_frame(0x8, "");
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].first, 0x8);
}

TEST(WSParser, EmptyPayload) {
    WSParser p;
    std::vector<Frame> out;
    auto f = make_frame(0x1, "");
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].second, "");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Extended payload lengths
// ═══════════════════════════════════════════════════════════════════════════════

TEST(WSParser, ExtLen16_200Bytes) {
    WSParser p;
    std::string payload(200, 'A');
    std::vector<Frame> out;
    auto f = make_frame(0x1, payload);
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].second, payload);
}

TEST(WSParser, ExtLen64_70KBytes) {
    WSParser p;
    std::string payload(70000, 'B');
    std::vector<Frame> out;
    auto f = make_frame(0x1, payload);
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].second.size(), 70000u);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Masking
// ═══════════════════════════════════════════════════════════════════════════════

TEST(WSParser, MaskedFrame) {
    WSParser p;
    uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
    std::vector<Frame> out;
    auto f = make_frame(0x1, "hello", true, true, mask);
    p.feed(f.data(), f.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].second, "hello");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Fragmentation
// ═══════════════════════════════════════════════════════════════════════════════

TEST(WSParser, ThreeFragments) {
    WSParser p;
    std::vector<Frame> out;
    auto cb = [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    };

    auto f1 = make_frame(0x1, "hel",   /*fin=*/false);   // first text fragment
    auto f2 = make_frame(0x0, "lo ",   /*fin=*/false);   // continuation
    auto f3 = make_frame(0x0, "world", /*fin=*/true);    // final continuation

    p.feed(f1.data(), f1.size(), cb);
    EXPECT_EQ(out.size(), 0u);

    p.feed(f2.data(), f2.size(), cb);
    EXPECT_EQ(out.size(), 0u);

    p.feed(f3.data(), f3.size(), cb);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].first, 0x1);
    EXPECT_EQ(out[0].second, "hello world");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Streaming / partial feeds
// ═══════════════════════════════════════════════════════════════════════════════

TEST(WSParser, MultipleFramesInOneFeed) {
    WSParser p;
    auto f1 = make_frame(0x1, "first");
    auto f2 = make_frame(0x1, "second");

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), f1.begin(), f1.end());
    combined.insert(combined.end(), f2.begin(), f2.end());

    std::vector<Frame> out;
    p.feed(combined.data(), combined.size(), [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    });
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].second, "first");
    EXPECT_EQ(out[1].second, "second");
}

TEST(WSParser, SplitAcrossTwoFeeds) {
    WSParser p;
    auto frame = make_frame(0x1, "split-me");

    std::vector<Frame> out;
    auto cb = [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    };

    p.feed(frame.data(), 2, cb);
    EXPECT_EQ(out.size(), 0u);

    p.feed(frame.data() + 2, frame.size() - 2, cb);
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].second, "split-me");
}

TEST(WSParser, ByteByByteFeed) {
    WSParser p;
    auto frame = make_frame(0x1, "byte");

    std::vector<Frame> out;
    auto cb = [&](uint8_t op, std::string_view m) {
        out.emplace_back(op, std::string(m));
    };

    for (size_t i = 0; i < frame.size(); ++i)
        p.feed(frame.data() + i, 1, cb);

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].second, "byte");
}

// ═══════════════════════════════════════════════════════════════════════════════
//  base64 helper
// ═══════════════════════════════════════════════════════════════════════════════

TEST(Base64, RoundTrip) {
    const uint8_t data[] = {0x00, 0x01, 0x02, 0xFF, 0xFE, 0xFD};
    std::string encoded = base64_encode(data, sizeof(data));
    EXPECT_FALSE(encoded.empty());
    EXPECT_EQ(encoded.size() % 4, 0u);
}

TEST(Base64, EmptyInput) {
    std::string encoded = base64_encode(nullptr, 0);
    EXPECT_TRUE(encoded.empty());
}

TEST(Base64, KnownVector) {
    const uint8_t data[] = "Man";
    std::string encoded = base64_encode(data, 3);
    EXPECT_EQ(encoded, "TWFu");
}
