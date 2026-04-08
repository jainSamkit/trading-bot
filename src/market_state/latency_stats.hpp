#pragma once
#include <array>
#include <cstdint>

// Accumulates socket→parse, parse→consume, and total latency samples.
// Prints a percentile table + ASCII histogram every REPORT_EVERY samples.
class LatencyStats {
    static constexpr int N            = 500;
    static constexpr int REPORT_EVERY = 500;

    struct Sample {
        int32_t socket_to_parse;   // ns: t_parse  - t_kernel
        int32_t parse_to_consume;  // ns: t_consume - t_parse
        int32_t total;             // ns: t_consume - t_kernel
    };

    std::array<Sample, N> buf_{};
    int count_ = 0;

public:
    void record(int64_t t_kernel, int64_t t_frame, int64_t t_parse, int64_t t_consume);
    void print() const;
};
