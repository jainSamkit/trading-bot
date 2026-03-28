#pragma once
#include <cstddef>
#include <cstdint>

enum class Action : uint8_t {
    Snapshot,
    Update,
};

struct BookLevel {
    uint32_t tick     = 0;
    uint64_t qty_lots = 0;
};

/// Parsed feed frame (bounded sides for stack storage).
struct FeedMessage {
    Action   action      = Action::Update;
    uint64_t sequence_no = 0;
    uint8_t  bid_count   = 0;
    uint8_t  ask_count   = 0;
    BookLevel bids[16]{};
    BookLevel asks[16]{};
};
