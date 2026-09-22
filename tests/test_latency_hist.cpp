#include "telemetry/latency_hist.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using hft::telemetry::LatencyHist;

// Known distribution: the integers 1..1000, each exactly once. With width=1
// every value lands in its own bucket, so the nearest-rank percentile is
// exact and hand-checkable. Nearest-rank p-th percentile = sample at rank
// ceil(p*N); with the k-th smallest sample equal to k, that rank's value is
// just ceil(p*1000).
TEST(LatencyHist, ExactPercentilesOnKnownDistribution) {
  LatencyHist h(/*bucket_width_ns=*/1, /*num_buckets=*/1024);
  for (std::uint64_t v = 1; v <= 1000; ++v) h.record(v);

  EXPECT_EQ(h.count(), 1000u);
  EXPECT_EQ(h.min(), 1u);
  EXPECT_EQ(h.max(), 1000u);          // exact, tracked on the side
  EXPECT_EQ(h.percentile(0.50), 500u);
  EXPECT_EQ(h.percentile(0.99), 990u);
  EXPECT_EQ(h.percentile(0.999), 999u);
  EXPECT_EQ(h.percentile(1.0), 1000u);
}

// A skewed distribution: 990 fast samples at 10 ns, 10 slow ones at 5000 ns.
// The mean (~60 ns) hides the tail entirely -- the whole point of this tool.
// p50/p99 sit in the fast cluster; p99.9 and max expose the slow tail.
TEST(LatencyHist, TailExposesWhatTheMeanHides) {
  LatencyHist h(/*width=*/1, /*num_buckets=*/8192);
  for (int i = 0; i < 990; ++i) h.record(10);
  for (int i = 0; i < 10; ++i) h.record(5000);

  EXPECT_EQ(h.percentile(0.50), 10u);    // half are fast
  EXPECT_EQ(h.percentile(0.99), 10u);    // rank 990 still in the fast cluster
  EXPECT_EQ(h.percentile(0.999), 5000u); // rank 1000 -> into the slow tail
  EXPECT_EQ(h.max(), 5000u);
}

// Values beyond the histogram range fall in the overflow bucket, but max()
// must stay exact so the extreme tail is never silently lost to bucketing.
TEST(LatencyHist, OverflowKeepsExactMax) {
  LatencyHist h(/*width=*/1, /*num_buckets=*/100);  // range [0,100)
  h.record(5);
  h.record(50);
  h.record(999999);  // overflow bucket

  EXPECT_EQ(h.count(), 3u);
  EXPECT_EQ(h.max(), 999999u);
}

// Quantization with a wider bucket: width=100 means the reported percentile is
// the bucket's lower edge, within `width` of the true value.
TEST(LatencyHist, WidthQuantizesToLowerEdge) {
  LatencyHist h(/*width=*/100, /*num_buckets=*/1024);
  for (std::uint64_t v = 0; v < 1000; ++v) h.record(v);  // 0..999

  // Nearest-rank p50 of 0..999 is the rank-500 value = 499, which sits in
  // bucket 4 ([400,500)). The reported value is that bucket's lower edge,
  // i.e. 400 -- within `width` (100) of the true 499.
  EXPECT_EQ(h.percentile(0.50), 400u);
}

TEST(LatencyHist, EmptyIsZero) {
  LatencyHist h(1, 16);
  EXPECT_EQ(h.count(), 0u);
  EXPECT_EQ(h.percentile(0.5), 0u);
  EXPECT_EQ(h.min(), 0u);
  EXPECT_EQ(h.max(), 0u);
}
