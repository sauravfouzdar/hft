#pragma once

#include <cstdint>

// core/tsc_clock -- a cheap hardware timestamp for instrumenting hot-path hops.
//
// Why not std::chrono::steady_clock on the hot path?
//   steady_clock::now() is an opaque, non-inlined library call. On macOS it
//   routes through mach_absolute_time() (a call boundary plus a timebase
//   multiply); on Linux it may hit the VDSO or, worse, a syscall. The cost is
//   real, variable, and invisible. When you are measuring sub-microsecond
//   hops, that overhead pollutes the very number you are trying to read.
//
//   A raw counter read is a SINGLE instruction, inlinable, with deterministic
//   cost. So on the hot path we read raw *ticks* (now_ticks) and convert to
//   nanoseconds once, later, off the hot path (TscClock::ticks_to_ns) using a
//   factor measured at startup. No division ever touches the hot loop.

namespace hft::core {

// Read the free-running timestamp counter as raw ticks. One instruction,
// inlinable, no syscall. This is the call that lands on the hot path.
[[nodiscard]] inline std::uint64_t now_ticks() noexcept {
#if defined(__aarch64__)
  // CNTVCT_EL0: the architected generic-timer virtual count register.
  // Free-running, constant-rate, monotonic, and readable from EL0 (user
  // space). This is the AArch64 analogue of x86's rdtsc.
  std::uint64_t t;
  asm volatile("mrs %0, cntvct_el0" : "=r"(t));
  return t;
#elif defined(__x86_64__)
  // rdtsc returns the 64-bit TSC split across edx:eax.
  std::uint32_t lo, hi;
  asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<std::uint64_t>(hi) << 32) | lo;
#else
#error "core/tsc_clock: no timestamp counter known for this architecture"
#endif
}

// Maps counter ticks to nanoseconds using a factor measured at startup.
// Construct it once via calibrate(); copy it freely (it is just a double).
class TscClock {
public:
  // Measure ticks->ns against a reference clock. Startup only -- never call
  // this on the hot path. Returns a ready-to-use clock.
  [[nodiscard]] static TscClock calibrate();

  [[nodiscard]] double ns_per_tick() const noexcept { return ns_per_tick_; }

  // Convert a tick delta to nanoseconds. Reporting path only (it multiplies
  // by a double); keep it off the hot loop.
  [[nodiscard]] std::uint64_t ticks_to_ns(std::uint64_t ticks) const noexcept {
    return static_cast<std::uint64_t>(static_cast<double>(ticks) * ns_per_tick_);
  }

private:
  explicit TscClock(double ns_per_tick) noexcept : ns_per_tick_(ns_per_tick) {}
  double ns_per_tick_;
};

}  // namespace hft::core
