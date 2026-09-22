#include "core/tsc_clock.hpp"

#include <chrono>
#include <thread>

namespace hft::core {

TscClock TscClock::calibrate() {
  // Measure how many nanoseconds one counter tick represents by timing the
  // SAME interval with both clocks. steady_clock is the reference ("true" ns);
  // now_ticks() gives the raw counter delta. Their ratio is ns-per-tick.
  //
  // Why this is robust: both clocks advance in real wall-time. If the sleep
  // overshoots (it always does a little), both intervals grow together and the
  // ratio is unchanged. The only error is the sub-microsecond gap between the
  // two reads at each boundary -- negligible across a 10 ms window. We bracket
  // the counter interval *inside* the reference interval (read steady, then
  // counter at the start; counter, then steady at the end) so any bias is
  // tiny and one-signed rather than random.
  using std::chrono::nanoseconds;
  using std::chrono::steady_clock;

  const auto ref_start = steady_clock::now();
  const std::uint64_t tick_start = now_ticks();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  const std::uint64_t tick_end = now_ticks();
  const auto ref_end = steady_clock::now();

  const auto ref_ns =
      std::chrono::duration_cast<nanoseconds>(ref_end - ref_start).count();
  const std::uint64_t ticks = tick_end - tick_start;

  return TscClock(static_cast<double>(ref_ns) / static_cast<double>(ticks));
}

}  // namespace hft::core
