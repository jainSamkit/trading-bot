#include "latency/clock.hpp"
#include "latency/histogram.hpp"

int main() {

    latency::calibrate();

    latency::Histogram h;

    for(int i=0;i<1000;i++) {
        h.record_ns(1000);
    }

    for(int i=0;i<100;i++) {
        h.record_ns(10000);
    }

    uint64_t p50,p90,p99, total;

    h.snapshot_percentile(p50, p90,p99,total);

    std::printf("total = %lu, p50 = %lu, p90=%lu, p99=%lu \n", total, p50, p90, p99);
}
