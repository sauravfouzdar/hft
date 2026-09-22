#include "core/spsc_ring.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

using hft::core::SpscRing;

// Single-threaded behaviour: FIFO order, full/empty signalling.
TEST(SpscRing, FifoOrderAndFullEmpty) {
  SpscRing<int, 4> r;  // capacity 4 (power of two)

  int out = -1;
  EXPECT_FALSE(r.try_pop(out)) << "empty ring must report empty";

  EXPECT_TRUE(r.try_push(1));
  EXPECT_TRUE(r.try_push(2));
  EXPECT_TRUE(r.try_push(3));
  EXPECT_TRUE(r.try_push(4));
  EXPECT_FALSE(r.try_push(5)) << "ring is full (capacity 4)";

  EXPECT_TRUE(r.try_pop(out));
  EXPECT_EQ(out, 1);  // FIFO
  EXPECT_TRUE(r.try_pop(out));
  EXPECT_EQ(out, 2);
}

// Push/pop far more than Capacity items so head_/tail_ advance past the array
// bounds and the index mask wraps repeatedly. Exercises the power-of-two mask.
TEST(SpscRing, WrapsAroundCorrectly) {
  SpscRing<std::uint64_t, 4> r;
  std::uint64_t out = 0;
  for (std::uint64_t i = 0; i < 1000; ++i) {
    ASSERT_TRUE(r.try_push(i));   // capacity 4, but we drain each iteration
    ASSERT_TRUE(r.try_pop(out));
    ASSERT_EQ(out, i);
  }
}

// The gate: one producer, one consumer, every item transferred exactly once
// and in order. Run under TSan, this also proves the memory ordering is race-
// free. Wall-clock deadline (not a spin count) is the anti-hang net.
TEST(SpscRing, SpscTransfersAllInOrder) {
  constexpr std::uint64_t kN = 1'000'000;
  SpscRing<std::uint64_t, 1024> r;

  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::seconds(30);
  constexpr std::uint64_t kCheckMask = (1u << 20) - 1;

  std::thread producer([&] {
    std::uint64_t spins = 0;
    for (std::uint64_t i = 0; i < kN;) {
      if (r.try_push(i)) ++i;  // spin while full
      else if ((++spins & kCheckMask) == 0 && clock::now() > deadline) break;
    }
  });

  bool ok = true;
  std::uint64_t got = 0;
  std::uint64_t spins = 0;
  std::uint64_t value = 0;
  while (got < kN) {
    if (r.try_pop(value)) {
      if (value != got) {  // SPSC must preserve order: expect 0,1,2,...
        ok = false;
        break;
      }
      ++got;
    } else if ((++spins & kCheckMask) == 0 && clock::now() > deadline) {
      break;
    }
  }
  producer.join();

  EXPECT_TRUE(ok) << "out-of-order or wrong value";
  EXPECT_EQ(got, kN) << "did not receive all items (ring broken?)";
}
