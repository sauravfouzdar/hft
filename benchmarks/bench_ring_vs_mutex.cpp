#include "core/mutex_queue.hpp"
#include "core/spsc_ring.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <thread>

// benchmarks/bench_ring_vs_mutex -- the head-to-head for PROGRESS 1.4.
//
// The point of this step is to FEEL the gap between the two handoffs with our
// own eyes, under identical conditions. Both queues run through the exact same
// cross-thread driver below: one producer thread pushes [0, kN), the
// benchmarked thread pops all kN. Same item count, same capacity, same machine,
// same run -- so the only thing that differs between the two rows of output is
// the queue itself, and the ratio of their items/sec is meaningful.
//
// Expected shape of the result: the mutex queue pays an unconditional
// lock/unlock on every single push and pop, and the mutex's own cache line
// ping-pongs between the two cores. The lock-free ring replaces all of that
// with a couple of atomic loads/stores and a masked array write. The ring
// should win by a wide margin; recording BOTH numbers and stating the ratio is
// the gate for this step.

using hft::core::MutexQueue;
using hft::core::SpscRing;

namespace {

// Same capacity for both, so neither gets an unfair "full less often" edge.
constexpr std::size_t kCapacity = 1024;

// The shared driver. Templated on the queue type so mutex and ring run byte-for
// -byte the same producer/consumer logic. Both expose try_push/try_pop with the
// identical "returns false when full/empty, never blocks" contract, so the
// producer spins while full and the consumer spins while empty in both cases.
template <class Q>
void drive(Q& q, std::uint64_t kN) {
  std::thread producer([&] {
    for (std::uint64_t i = 0; i < kN;) {
      if (q.try_push(i)) ++i;  // spin while full
    }
  });

  std::uint64_t got = 0;
  std::uint64_t value = 0;
  while (got < kN) {
    if (q.try_pop(value)) {
      benchmark::DoNotOptimize(value);
      ++got;
    }
  }
  producer.join();
}

void BM_MutexQueue_SPSC(benchmark::State& state) {
  const auto kN = static_cast<std::uint64_t>(state.range(0));
  for (auto _ : state) {
    MutexQueue<std::uint64_t> q(kCapacity);
    drive(q, kN);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(kN));
}

void BM_SpscRing_SPSC(benchmark::State& state) {
  const auto kN = static_cast<std::uint64_t>(state.range(0));
  for (auto _ : state) {
    // 8 KB of buffer + two atomics; fine on the stack, and a fresh queue per
    // iteration matches how the mutex row is measured.
    SpscRing<std::uint64_t, kCapacity> q;
    drive(q, kN);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(kN));
}

}  // namespace

// Same workload for both: 2^20 = 1,048,576 items handed across the thread.
BENCHMARK(BM_MutexQueue_SPSC)->Arg(1 << 20)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_SpscRing_SPSC)->Arg(1 << 20)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
