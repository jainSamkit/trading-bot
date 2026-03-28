#pragma once
#include <cstddef>
#include "feed/feed_types.hpp"

// ── FeedParser ────────────────────────────────────────────────────────────────
// Responsibility: parse raw bytes into FeedMessage (BookLevel: tick index + qty_lots).
//
// Usage:
//   FeedParser parser;
//   FeedMessage msg;
//   if (parser.parse(raw_bytes, len, msg)) {
//       // msg is ready — dispatch to OrderBook
//   }

class FeedParser {
public:
    FeedParser()  = default;
    ~FeedParser() = default;

    FeedParser(FeedParser const&)            = delete;
    FeedParser& operator=(FeedParser const&) = delete;

    // Parse raw JSON bytes into msg.
    // Returns true on success, false if the message is malformed or
    // the sequence number is out of order.
    bool parse(const char* data, std::size_t len, FeedMessage& msg);

private:
    uint64_t last_sequence_no_ = 0;

    bool   validateChecksum(FeedMessage const& msg) const;
    Action parseAction(const char* str) const;
    bool   checkSequence(uint64_t seq_no);
};
