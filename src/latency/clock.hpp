#pragma once
// File header comment:
// Provides CPU timestamp primitives for latency measurement.
// On Linux x86_64: uses __rdtscp (cycle counter, ~1ns resolution, ~20ns cost).
// On other platforms: falls back to clock_gettime(CLOCK_MONOTONIC_RAW).
// Calibration at startup measures TSC frequency; ratio cached for cycles→ns conversion.
//
// Backend selection is COMPILE-TIME only. If LATENCY_HAS_RDTSCP=1 at build time but
// the CPU lacks constant_tsc/nonstop_tsc flags, calibrate() aborts with a clear
// error. Rebuild with -DLATENCY_FORCE_CLOCK_GETTIME=1 for environments where TSC
// is unreliable (e.g., Docker-on-Mac).

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>      // std::abort
#include <cmath>        // std::abs (double)
#include <array>
#include <algorithm>    // std::sort
#include <thread>       // std::this_thread::sleep_for
#include <cassert>
#include <fstream>
#include <string>
#include <ctime>        // struct timespec, clock_gettime, CLOCK_MONOTONIC_RAW

#if defined(__linux__) && defined(__x86_64__) && !defined(LATENCY_FORCE_CLOCK_GETTIME)
    #define LATENCY_HAS_RDTSCP 1
    #include <x86intrin.h>
#else
    #define LATENCY_HAS_RDTSCP 0
#endif


namespace latency {
    enum class Backend : uint8_t { Rdtscp, ClockGettime };
}

namespace latency::detail {
    inline Backend backend       = Backend::Rdtscp;
    inline double  ns_per_cycle  = 0.0;
    inline bool    calibrated    = false;
}

namespace latency {

inline constexpr int    kCalibrationSamples = 10;
inline constexpr int    kDefaultSampleMs    = 100;
inline constexpr double kFallbackNsPerCycle = 1.0;
inline constexpr double kMaxDriftFraction   = 0.001;


// ── Forward declarations (calibrate calls is_tsc_reliable below) ────────────
inline bool is_tsc_reliable() noexcept;


// ── Hot-path primitives ─────────────────────────────────────────────────────

inline uint64_t now_cycles() noexcept {
#if LATENCY_HAS_RDTSCP
    uint32_t aux;
    return __rdtscp(&aux);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ull
         + static_cast<uint64_t>(ts.tv_nsec);
#endif
}

inline uint64_t cycles_to_ns(uint64_t cycles) noexcept {
    return static_cast<uint64_t>(static_cast<double>(cycles) * detail::ns_per_cycle);
}

inline uint64_t now_ns() noexcept {
    return cycles_to_ns(now_cycles());
}


// ── Cold-path API ───────────────────────────────────────────────────────────

inline void calibrate(int sample_ms = kDefaultSampleMs) {
    if (detail::calibrated) return;
    assert(sample_ms > 0 && "calibrate: sample_ms must be positive");

#if LATENCY_HAS_RDTSCP
    if (!is_tsc_reliable()) {
        std::fprintf(stderr,
            "[FATAL] latency: TSC missing constant_tsc/nonstop_tsc flags.\n"
            "        This binary was compiled with rdtscp support but the CPU\n"
            "        doesn't have reliable TSC (typical on Docker-on-Mac).\n"
            "        Rebuild with: -DLATENCY_FORCE_CLOCK_GETTIME=1\n");
        std::abort();
    }

    // Sample N times: ratio = wall_ns / tsc_cycles
    std::array<double, kCalibrationSamples> ratios{};
    for (int i = 0; i < kCalibrationSamples; ++i) {
        auto     w0 = std::chrono::steady_clock::now();
        uint64_t c0 = now_cycles();
        std::this_thread::sleep_for(std::chrono::milliseconds(sample_ms));
        uint64_t c1 = now_cycles();
        auto     w1 = std::chrono::steady_clock::now();

        const uint64_t wall_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(w1 - w0).count();
        const uint64_t tsc_cycles = c1 - c0;
        ratios[i] = static_cast<double>(wall_ns) / static_cast<double>(tsc_cycles);
    }

    // Median (robust to one preempted sleep)
    std::sort(ratios.begin(), ratios.end());
    detail::ns_per_cycle = ratios[kCalibrationSamples / 2];
    detail::backend      = Backend::Rdtscp;

    // 1-second drift self-check
    {
        auto     w0 = std::chrono::steady_clock::now();
        uint64_t c0 = now_cycles();
        std::this_thread::sleep_for(std::chrono::seconds(1));
        uint64_t c1 = now_cycles();
        auto     w1 = std::chrono::steady_clock::now();

        const uint64_t actual_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(w1 - w0).count();
        const uint64_t predicted_ns =
            static_cast<uint64_t>((c1 - c0) * detail::ns_per_cycle);

        // Cast each to double BEFORE subtracting — uint64_t would underflow
        const double drift =
            std::abs(static_cast<double>(actual_ns) - static_cast<double>(predicted_ns))
            / static_cast<double>(actual_ns);

        if (drift > kMaxDriftFraction) {
            std::fprintf(stderr,
                "[FATAL] latency: TSC drift %.5f exceeds threshold %.5f.\n"
                "        Rebuild with -DLATENCY_FORCE_CLOCK_GETTIME=1\n",
                drift, kMaxDriftFraction);
            std::abort();
        }

        detail::calibrated = true;
        std::fprintf(stderr,
            "[latency] calibrate: rdtscp, ns_per_cycle=%.4f, drift=%.5f\n",
            detail::ns_per_cycle, drift);
    }
#else
    // Compile-time fallback: cycles ARE nanoseconds (ratio = 1.0)
    detail::backend      = Backend::ClockGettime;
    detail::ns_per_cycle = kFallbackNsPerCycle;
    detail::calibrated   = true;
    std::fprintf(stderr,
        "[latency] calibrate: clock_gettime backend, ns_per_cycle=1.0\n");
#endif
}

inline double tsc_ns_per_cycle() noexcept {
    return detail::ns_per_cycle;
}

inline Backend active_backend() noexcept {
    return detail::backend;
}

inline const char* backend_name() noexcept {
#if LATENCY_HAS_RDTSCP
    return "rdtscp";
#else
    return "clock_gettime";
#endif
}

inline bool is_tsc_reliable() noexcept {
#if LATENCY_HAS_RDTSCP
    std::ifstream f("/proc/cpuinfo");
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("flags", 0) == 0) {
            const bool has_constant = line.find(" constant_tsc") != std::string::npos
                                   || line.find("\tconstant_tsc") != std::string::npos;
            const bool has_nonstop  = line.find(" nonstop_tsc")  != std::string::npos
                                   || line.find("\tnonstop_tsc") != std::string::npos;
            return has_constant && has_nonstop;
        }
    }
    return false;
#else
    return false;
#endif
}

}  // namespace latency
