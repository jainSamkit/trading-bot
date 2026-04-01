#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>

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
    void record(int64_t t_kernel, int64_t t_frame, int64_t t_parse, int64_t t_consume) {
        if (t_kernel == 0 || t_parse == 0) return;
        buf_[count_ % N] = {
            static_cast<int32_t>(t_parse   - t_kernel),
            static_cast<int32_t>(t_consume - t_parse),
            static_cast<int32_t>(t_consume - t_kernel)
        };
        ++count_;
        if (count_ % REPORT_EVERY == 0) print();
    }

    void print() const {
        const int n = std::min(count_, N);
        if (n == 0) return;

        // sort copies for each metric
        std::array<int32_t, N> stp{}, ptc{}, tot{};
        for (int i = 0; i < n; ++i) {
            stp[i] = buf_[i].socket_to_parse;
            ptc[i] = buf_[i].parse_to_consume;
            tot[i] = buf_[i].total;
        }
        std::sort(stp.begin(), stp.begin() + n);
        std::sort(ptc.begin(), ptc.begin() + n);
        std::sort(tot.begin(), tot.begin() + n);

        auto us = [](int32_t ns) { return ns / 1000.0; };
        auto p  = [&](const std::array<int32_t, N>& a, double pct) {
            return us(a[static_cast<int>(pct * (n - 1))]);
        };

        auto row = [&](const char* label, const std::array<int32_t, N>& a) {
            std::cerr << std::left  << std::setw(20) << label
                      << std::right << std::fixed << std::setprecision(1)
                      << std::setw(8) << p(a, 0.50) << "µs"
                      << std::setw(8) << p(a, 0.95) << "µs"
                      << std::setw(8) << p(a, 0.99) << "µs"
                      << std::setw(8) << p(a, 0.999) << "µs"
                      << "\n";
        };

        std::cerr << "\n─── Latency (" << count_ << " msgs) ────────────────────────────────\n"
                  << std::left  << std::setw(20) << ""
                  << std::right << std::setw(10) << "p50"
                  << std::setw(10) << "p95"
                  << std::setw(10) << "p99"
                  << std::setw(10) << "p99.9" << "\n";
        row("socket→parse",  stp);
        row("parse→consume", ptc);
        row("total",         tot);

        // ASCII histogram of total latency
        constexpr int NBINS = 8;
        const int64_t edges[NBINS + 1] = {0, 5'000, 10'000, 25'000, 50'000,
                                           100'000, 250'000, 500'000,
                                           INT32_MAX};
        const char* labels[NBINS] = {"<5µs","5-10µs","10-25µs","25-50µs",
                                      "50-100µs","100-250µs","250-500µs",">500µs"};
        int bins[NBINS]{};
        for (int i = 0; i < n; ++i)
            for (int b = 0; b < NBINS; ++b)
                if (tot[i] < edges[b + 1]) { ++bins[b]; break; }

        const int bar_max = 40;
        const int peak    = *std::max_element(bins, bins + NBINS);
        std::cerr << "\n";
        for (int b = 0; b < NBINS; ++b) {
            int bar = peak > 0 ? (bins[b] * bar_max + peak / 2) / peak : 0;
            std::cerr << std::left << std::setw(10) << labels[b]
                      << " |" << std::string(bar, '#')
                      << " " << bins[b] << "\n";
        }
        std::cerr << "─────────────────────────────────────────────────────\n\n";
    }
};
