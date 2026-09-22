#include "core/mutex_queue.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

using hft::core::MutexQueue;

// Single-threaded behaviour: order (FIFO), and full/empty signalling. This is
// the test that goes red against the stub (no threads, so no hang).
TEST(MutexQueue, FifoOrderAndFullEmpty) {
  MutexQueue<int> q(3);

  int out = -1;
  EXPECT_FALSE(q.try_pop(out)) << "empty queue must report empty";

  EXPECT_TRUE(q.try_push(1));
  EXPECT_TRUE(q.try_push(2));
  EXPECT_TRUE(q.try_push(3));
  EXPECT_FALSE(q.try_push(4)) << "queue is full (capacity 3)";

  EXPECT_TRUE(q.try_pop(out));
  EXPECT_EQ(out, 1);  // FIFO
  EXPECT_TRUE(q.try_pop(out));
  EXPECT_EQ(out, 2);
  EXPECT_TRUE(q.try_push(5));  // room again after popping
  EXPECT_TRUE(q.try_pop(out));
  EXPECT_EQ(out, 3);
  EXPECT_TRUE(q.try_pop(out));
  EXPECT_EQ(out, 5);
  EXPECT_FALSE(q.try_pop(out));  // empty again
}

// The gate: one producer, one consumer, every item transferred exactly once
// and in order. A spin cap turns a broken queue into a clean failure instead
// of an infinite hang.
TEST(MutexQueue, SpscTransfersAllInOrder) {
  constexpr std::uint64_t kN = 1'000'000;
  MutexQueue<std::uint64_t> q(1024);

  // Safety net: a wall-clock deadline, not a spin count. The number of empty
  // spins depends on relative thread speed (huge under a sanitizer), so a spin
  // cap is fragile; a deadline turns a genuinely broken/hung queue into a
  // clean failure while never tripping on a working one. Checked only every
  // so often so the clock read does not dominate the loop.
  using clock = std::chrono::steady_clock;
  const auto deadline = clock::now() + std::chrono::seconds(30);
  constexpr std::uint64_t kCheckMask = (1u << 20) - 1;  // ~every 1M spins

  std::thread producer([&] {
    std::uint64_t spins = 0;
    for (std::uint64_t i = 0; i < kN;) {
      if (q.try_push(i)) ++i;  // spin while full
      else if ((++spins & kCheckMask) == 0 && clock::now() > deadline) break;
    }
  });

  bool ok = true;
  std::uint64_t got = 0;
  std::uint64_t spins = 0;
  std::uint64_t value = 0;
  while (got < kN) {
    if (q.try_pop(value)) {
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
  EXPECT_EQ(got, kN) << "did not receive all items (queue broken?)";
}
