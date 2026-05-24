#include "latency/span.hpp"
#include "latency/histogram.hpp"

int main() {
    latency::calibrate();
    latency::Histogram h;

    {
        latency::Span s(&h);
        for (int i = 0; i < 10000; ++i) asm volatile("" ::: "memory");
    }   // dtor fires here, records into h

    uint64_t p50, p90, p99, total;
    h.snapshot_percentile(p50, p90, p99, total);
    std::printf("after one Span: count=%lu p50=%lu ns\n", total, p50);
    return 0;
}
