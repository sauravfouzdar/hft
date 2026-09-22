#include "telemetry/latency_hist.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace hft::telemetry {

LatencyHist::LatencyHist(std::uint64_t bucket_width_ns, std::size_t num_buckets)
    : width_(bucket_width_ns ? bucket_width_ns : 1),
      buckets_(num_buckets ? num_buckets : 1, 0) {}

std::uint64_t LatencyHist::percentile(double p) const noexcept {
  if (count_ == 0) return 0;
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;

  // Nearest-rank method: the p-th percentile is the value of the sample at
  // rank ceil(p * N) in sorted order (ranks 1..N). We don't have sorted
  // samples, but the buckets ARE the sorted order -- walk them low to high,
  // accumulating counts, and stop at the bucket where the running total first
  // reaches the target rank. Report that bucket's lower edge.
  const std::uint64_t rank =
      std::max<std::uint64_t>(1, static_cast<std::uint64_t>(
                                     std::ceil(p * static_cast<double>(count_))));

  std::uint64_t cumulative = 0;
  for (std::size_t i = 0; i < buckets_.size(); ++i) {
    cumulative += buckets_[i];
    if (cumulative >= rank) return static_cast<std::uint64_t>(i) * width_;
  }
  return max_;  // unreachable when count_ > 0, but keeps the contract total
}

void LatencyHist::report(const char* label) const {
  std::printf("%s: p50=%llu p99=%llu p99.9=%llu max=%llu (n=%llu)\n", label,
              (unsigned long long)percentile(0.50),
              (unsigned long long)percentile(0.99),
              (unsigned long long)percentile(0.999),
              (unsigned long long)max(), (unsigned long long)count());
}

}  // namespace hft::telemetry
