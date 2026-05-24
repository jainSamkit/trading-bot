#pragma once

// cpu_relax — hint to the CPU that we're in a spin-wait loop.
//
// Emits:
//   x86_64 / x86       →  PAUSE          (~140 cycles on Skylake+, ~10 on older Intel)
//   aarch64 / ARM64    →  YIELD          (~few cycles, hyperthread hint)
//   anything else      →  asm compiler barrier (no-op stall, prevents reordering)
//
// Use it in busy-poll loops over lock-free queues, spinlocks, atomic flags.
// It does NOT yield the OS thread — for that, use std::this_thread::yield().
// It does NOT order memory — for that, use std::atomic_thread_fence.
//
// Reference: Linux kernel uses cpu_relax() in the same role; HFT codebases
// typically wrap it in a similarly-named primitive.

namespace core {

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    asm volatile("pause" ::: "memory");
#elif defined(__aarch64__) || defined(__arm__)
    asm volatile("yield" ::: "memory");
#else
    // Unknown arch — emit a compiler barrier so the loop body isn't elided.
    asm volatile("" ::: "memory");
#endif
}

}  // namespace core
