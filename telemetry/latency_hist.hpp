#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// telemetry/latency_hist -- a hot-path-friendly latency recorder that reports
// TAILS, not averages.
//
// Why tails: in trading the mean latency is a lie. What hurts is the p99.9
// spike -- the one-in-a-thousand event where you were slow and got picked off.
// So this tool's headline numbers are p50, p99, p99.9, and max. There is no
// mean() on purpose.
//
// Why buckets (not "store every sample and sort"): recording happens on the
// hot path, so it must be O(1), allocation-free, and tiny. record() is a
// single array increment: buckets[value / width]++. The bucket array is sized
// once at construction (startup allocation, allowed) and never grows.
//
// Accuracy: bucketing quantizes to the bucket width, so a reported percentile
// is the lower edge of the bucket the true value falls in (resolution =
// width). min and max are tracked EXACTLY on the side, so the extreme tail is
// never lost to quantization. With width == 1 the histogram is exact for
// integer samples -- which is how the test pins the percentiles down.

namespace hft::telemetry {

class LatencyHist {
public:
  // Linear buckets: bucket i covers [i*width, (i+1)*width) ns. The final
  // bucket is an overflow catch-all for anything at/over the range; max() is
  // still exact in that case. width must be >= 1, num_buckets >= 1.
  LatencyHist(std::uint64_t bucket_width_ns, std::size_t num_buckets);

  // Hot path: classify into a bucket and bump counters. No allocation, no
  // syscalls, O(1).
  void record(std::uint64_t value_ns) noexcept {
    const std::uint64_t idx = value_ns / width_;
    buckets_[idx < buckets_.size() ? idx : buckets_.size() - 1]++;
    if (value_ns < min_) min_ = value_ns;
    if (value_ns > max_) max_ = value_ns;
    ++count_;
  }

  // Reporting path (NOT hot): nearest-rank percentile. p in [0, 1]. Returns
  // the lower edge of the bucket containing the p-th sample (resolution =
  // width). Returns 0 if no samples recorded.
  [[nodiscard]] std::uint64_t percentile(double p) const noexcept;

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }
  [[nodiscard]] std::uint64_t min() const noexcept { return count_ ? min_ : 0; }
  [[nodiscard]] std::uint64_t max() const noexcept { return max_; }

  // Convenience: print p50/p99/p99.9/max with a label. Reporting path only.
  void report(const char* label) const;

private:
  std::uint64_t width_;
  std::vector<std::uint64_t> buckets_;  // sized at construction; never regrows
  std::uint64_t count_{0};
  std::uint64_t min_{UINT64_MAX};
  std::uint64_t max_{0};
};

}  // namespace hft::telemetry
