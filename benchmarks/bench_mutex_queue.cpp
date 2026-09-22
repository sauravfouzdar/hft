#include "core/mutex_queue.hpp"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <thread>

using hft::core::MutexQueue;

// Cross-thread SPSC throughput of the mutex queue: one producer thread pushes
// kN items, the benchmarked thread pops them all. SetItemsProcessed lets
// Google Benchmark report items/sec -- the number we will compare against the
// lock-free ring in 1.4. This is the baseline, expected to be slow: every
// push and pop pays a lock/unlock and bounces the mutex line between cores.
static void BM_MutexQueue_SPSC(benchmark::State& state) {
  const std::uint64_t kN = static_cast<std::uint64_t>(state.range(0));
  constexpr std::size_t kCapacity = 1024;

  for (auto _ : state) {
    MutexQueue<std::uint64_t> q(kCapacity);

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

  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(kN));
}
BENCHMARK(BM_MutexQueue_SPSC)->Arg(1 << 20)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
