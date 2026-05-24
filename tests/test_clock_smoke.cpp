#include "latency/clock.hpp"
#include <cstdio>
#include <cstdint>

int main() {
    latency::calibrate();
    std::printf("backend:      %s\n", latency::backend_name());
    std::printf("ns_per_cycle: %.6f\n", latency::tsc_ns_per_cycle());

    // Spin loop. asm("" ::: "memory") prevents the optimizer from eliminating
    // the loop body without needing 'volatile' (deprecated in C++20).
    const uint64_t t0 = latency::now_cycles();
    for (int i = 0; i < 10000; ++i) {
        asm volatile("" ::: "memory");
    }
    const uint64_t t1 = latency::now_cycles();

    std::printf("10000-iter loop: %lu cycles = %lu ns\n",
                (unsigned long)(t1 - t0),
                (unsigned long)latency::cycles_to_ns(t1 - t0));

    // Cost of now_cycles() itself, averaged over 1M calls
    constexpr int N = 1'000'000;
    uint64_t a = latency::now_cycles();
    for (int i = 0; i < N; ++i) {
        uint64_t x = latency::now_cycles();
        asm volatile("" : "+r"(x));
    }
    uint64_t b = latency::now_cycles();
    double per_call_ns = static_cast<double>(latency::cycles_to_ns(b - a)) / N;
    std::printf("now_cycles() cost: %.2f ns/call over %d iterations\n",
                per_call_ns, N);

    return 0;
}