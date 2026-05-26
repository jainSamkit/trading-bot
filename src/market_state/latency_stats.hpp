#pragma once
#include <array>
#include <cstdint>

// Accumulates socket→parse, parse→consume, and total latency samples.
// Prints a percentile table + ASCII histogram every REPORT_EVERY samples.
class LatencyStats {
    static constexpr int N            = 500;
    static constexpr int REPORT_EVERY = 500;

    struct Sample {
        int32_t socket_to_parse;   // ns: t_parse  - t_recv_userspace
        int32_t parse_to_consume;  // ns: t_consume - t_parse
        int32_t total;             // ns: t_consume - t_recv_userspace
    };

    std::array<Sample, N> buf_{};
    int count_ = 0;

public:
    void record(uint64_t t_recv_userspace, uint64_t t_frame, uint64_t t_parse, uint64_t t_consume);
    void print() const;
};
