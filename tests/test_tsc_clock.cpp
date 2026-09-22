#include "core/tsc_clock.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using hft::core::now_ticks;
using hft::core::TscClock;

// The gate test for 0.2: time a 1ms sleep through the calibrated clock and
// confirm it reads back as ~1,000,000 ns. sleep_for never undershoots and
// tends to overshoot, so the band is asymmetric and generous on the high
// side. A broken calibration (e.g. the 0.0 stub) reads 0 ns and fails here.
TEST(TscClock, MeasuresOneMillisecondSleep) {
  const TscClock clock = TscClock::calibrate();
  ASSERT_GT(clock.ns_per_tick(), 0.0) << "calibration produced a zero factor";

  const std::uint64_t c0 = now_ticks();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const std::uint64_t c1 = now_ticks();

  const std::uint64_t ns = clock.ticks_to_ns(c1 - c0);
  std::printf("[tsc] 1ms sleep measured as %llu ns (ns/tick=%.6f)\n",
              static_cast<unsigned long long>(ns), clock.ns_per_tick());

  EXPECT_GE(ns, 900'000u);    // at least ~1 ms
  EXPECT_LE(ns, 5'000'000u);  // not wildly off (catches a bad factor)
}

// Tighter check that calibration is actually accurate, independent of how
// imprecise sleep is: measure the SAME interval with both clocks and require
// them to agree. This is what really validates the ticks->ns factor.
TEST(TscClock, AgreesWithSteadyClockWithinOnePercent) {
  const TscClock clock = TscClock::calibrate();

  const auto s0 = std::chrono::steady_clock::now();
  const std::uint64_t c0 = now_ticks();
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const std::uint64_t c1 = now_ticks();
  const auto s1 = std::chrono::steady_clock::now();

  const auto ref_ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(s1 - s0).count();
  const std::uint64_t tsc_ns = clock.ticks_to_ns(c1 - c0);

  std::printf("[tsc] over %lld ns ref, tsc read %llu ns (%.3f%% off)\n",
              static_cast<long long>(ref_ns),
              static_cast<unsigned long long>(tsc_ns),
              100.0 * (static_cast<double>(tsc_ns) - static_cast<double>(ref_ns)) /
                  static_cast<double>(ref_ns));

  EXPECT_NEAR(static_cast<double>(tsc_ns), static_cast<double>(ref_ns),
              static_cast<double>(ref_ns) * 0.01);  // within 1%
}
