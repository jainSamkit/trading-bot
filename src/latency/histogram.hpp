#pragma once

// Lock-free histogram with HdrHistogram-style sub-bucketing.
//
// Each binary decade [2^k, 2^(k+1)) is divided into SUB_BUCKETS equal slices,
// giving ~1/SUB_BUCKETS relative precision (~6% with SUB_BITS=4).
// Below SUB_BUCKETS, buckets are linear (each value gets its own bucket).
//
// Bucket layout:
//   indices [0, SUB_BUCKETS):  linear, bucket i == value i           (covers 0..15)
//   indices [SUB_BUCKETS, ∞):  decade-divided
//     bucket = ((magnitude - SUB_BITS + 1) << SUB_BITS) | sub_index
//     where magnitude = bit position of the highest set bit of v
//           sub_index = top SUB_BITS bits of the in-decade offset
//
// Total slots: SUB_BUCKETS + (MAX_MAGNITUDE - SUB_BITS + 1) * SUB_BUCKETS
//   With SUB_BITS=4, MAX_MAGNITUDE=63: 16 + 60*16 = 976 slots × 8 B = ~7.6 KB
//
// Write side: relaxed atomic fetch_add (single instruction, multi-writer safe).
// Read side: relaxed loads from the push thread. Reader sees a "consistent enough"
// snapshot — minor races are acceptable for percentile reporting at 10s intervals.

#include <cstdint>
#include <atomic>
#include <array>

#include "latency/clock.hpp"   // cycles_to_ns

namespace latency {

class Histogram {
public:
    static constexpr int SUB_BITS      = 4;
    static constexpr int SUB_BUCKETS   = 1 << SUB_BITS;      // 16
    static constexpr int SUB_MASK      = SUB_BUCKETS - 1;    // 0x0F
    static constexpr int MAX_MAGNITUDE = 63;                 // covers values up to 2^63
    static constexpr int N_BUCKETS     = SUB_BUCKETS + (MAX_MAGNITUDE - SUB_BITS + 1) * SUB_BUCKETS;  // 976

    Histogram() noexcept = default;

    // ── write side (hot path) ────────────────────────────────────────────────
    inline void record_ns(uint64_t v) noexcept {
        if (v == 0) v = 1;
        const int b = bucket_of(v);
        buckets_[b].fetch_add(1, std::memory_order_relaxed);
    }

    inline void record_cycles(uint64_t cycles) noexcept {
        record_ns(cycles_to_ns(cycles));
    }

    // ── read side (push thread, every 10s) ───────────────────────────────────
    uint64_t count() const noexcept {
        uint64_t total = 0;
        for (int i = 0; i < N_BUCKETS; ++i)
            total += buckets_[i].load(std::memory_order_relaxed);
        return total;
    }

    void snapshot_percentile(uint64_t& p50, uint64_t& p90, uint64_t& p99,
                             uint64_t& total) const noexcept {
        total = 0;
        for (int i = 0; i < N_BUCKETS; ++i)
            total += buckets_[i].load(std::memory_order_relaxed);

        if (total == 0) { p50 = p90 = p99 = 0; return; }

        // ceil thresholds — exact percentile semantics rather than floor.
        const uint64_t t50 = (total + 1) / 2;
        const uint64_t t90 = (total * 9 + 9) / 10;
        const uint64_t t99 = (total * 99 + 99) / 100;

        uint64_t cum = 0;
        bool got50 = false, got90 = false, got99 = false;

        for (int i = 0; i < N_BUCKETS; ++i) {
            cum += buckets_[i].load(std::memory_order_relaxed);
            if (!got50 && cum >= t50) { p50 = upper_bound_of(i); got50 = true; }
            if (!got90 && cum >= t90) { p90 = upper_bound_of(i); got90 = true; }
            if (!got99 && cum >= t99) { p99 = upper_bound_of(i); got99 = true; }
            if (got50 && got90 && got99) return;
        }
    }

    uint64_t bucket_count(int i) const noexcept {
        return buckets_[i].load(std::memory_order_relaxed);
    }

    void reset() noexcept {
        for (int i = 0; i < N_BUCKETS; ++i)
            buckets_[i].store(0, std::memory_order_relaxed);
    }

private:
    // value → bucket index
    static inline int bucket_of(uint64_t v) noexcept {
        if (v < static_cast<uint64_t>(SUB_BUCKETS)) {
            return static_cast<int>(v);
        }
        const int m     = 63 - __builtin_clzll(v);    // 2^m <= v < 2^(m+1)
        const int shift = m - SUB_BITS;               // m >= 4 here, so shift >= 0
        const int sub   = static_cast<int>((v >> shift) & SUB_MASK);
        return ((m - SUB_BITS + 1) << SUB_BITS) | sub;
    }

    // bucket index → upper bound (exclusive) of the value range it covers.
    // Reporting upper bound matches HdrHistogram convention: "this percentile is
    // at most this value" — pessimistic but unambiguous.
    static inline uint64_t upper_bound_of(int b) noexcept {
        if (b < SUB_BUCKETS) {
            return static_cast<uint64_t>(b) + 1;      // linear: bucket i covers [i, i+1)
        }
        const int m   = (b >> SUB_BITS) + SUB_BITS - 1;
        const int sub = b & SUB_MASK;
        const uint64_t lower = (1ULL << m) + static_cast<uint64_t>(sub) * (1ULL << (m - SUB_BITS));
        return lower + (1ULL << (m - SUB_BITS));
    }

    alignas(64) std::array<std::atomic<uint64_t>, N_BUCKETS> buckets_{};
};

}  // namespace latency
