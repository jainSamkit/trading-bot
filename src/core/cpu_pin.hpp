#pragma once
// CPU affinity + real-time priority helpers.
//
// Linux only — on other platforms (macOS for local dev), all calls are no-ops
// so the bot still builds and runs (without the latency guarantees).
//
// Conventions
// ───────────
// • Cores are passed as plain int CPU IDs matching /proc/cpuinfo / taskset.
// • pin_thread() returns false on failure but does NOT throw — pinning a
//   thread is a "should succeed; if not, run anyway" operation. We log
//   via stderr so an EC2 run produces an audit trail at startup.
// • set_realtime() requests SCHED_FIFO at priority 50. Requires CAP_SYS_NICE
//   (root on EC2). Skipped silently if not root — again, "best effort."
// • affinity_to_string() / current_cpu() are diagnostic helpers used at
//   startup to log "[pin] feed/market_state thread → core 3" lines.

#include <cstdio>
#include <cstring>
#include <pthread.h>

#if defined(__linux__)
    #include <sched.h>
    #include <sys/syscall.h>
    #include <unistd.h>
#endif

namespace core::cpu {

// Pin the calling thread to a single logical CPU. Returns true on success.
inline bool pin_thread(int cpu, const char* label) noexcept {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    const int rc = pthread_setaffinity_np(pthread_self(), sizeof(set), &set);
    if (rc != 0) {
        std::fprintf(stderr, "[pin] WARN %s → core %d failed: %s\n",
                     label, cpu, std::strerror(rc));
        return false;
    }
    std::fprintf(stderr, "[pin] %s → core %d\n", label, cpu);
    return true;
#else
    (void)cpu;
    std::fprintf(stderr, "[pin] (no-op on non-Linux) %s → core %d\n", label, cpu);
    return true;
#endif
}

// Request SCHED_FIFO real-time scheduling for the calling thread.
// Default priority 50 — enough to preempt CFS without fighting kernel threads.
// Best effort; if EPERM (no CAP_SYS_NICE), logs and continues.
inline bool set_realtime(int priority = 50, const char* label = "thread") noexcept {
#if defined(__linux__)
    sched_param sp{};
    sp.sched_priority = priority;
    const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    if (rc != 0) {
        std::fprintf(stderr, "[pin] WARN %s SCHED_FIFO(%d) failed: %s "
                             "(need CAP_SYS_NICE / root)\n",
                     label, priority, std::strerror(rc));
        return false;
    }
    std::fprintf(stderr, "[pin] %s → SCHED_FIFO prio %d\n", label, priority);
    return true;
#else
    (void)priority; (void)label;
    return true;
#endif
}

// Diagnostic: log the CPU the caller is currently running on. Useful right
// after pin_thread() to confirm the affinity took effect immediately.
inline int current_cpu() noexcept {
#if defined(__linux__)
    return sched_getcpu();
#else
    return -1;
#endif
}

}  // namespace core::cpu
