#include <benchmark/benchmark.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <thread>

// benchmarks/bench_false_sharing -- the experiment for PROGRESS 1.5.
//
// Goal: PROVE cache lines are physically real by making TWO rings whose ONLY
// difference is a few bytes of padding, and watching the throughput collapse
// and recover.
//
// The CPU moves memory in fixed-size cache lines (64 bytes here). A line can be
// owned for writing by only one core at a time. In the ring, the producer
// writes head_ and the consumer writes tail_ -- different variables, no real
// conflict. But if head_ and tail_ sit in the SAME cache line, the hardware
// tracks the LINE, not the variable, so the two cores tug the one line back and
// forth on every op. That's FALSE SHARING: a fake conflict with a real cost.
//
// Fix: force head_ and tail_ onto SEPARATE cache lines with alignas. The
// experiment below instantiates the identical ring twice -- once packed (shared
// line), once padded (separate lines) -- so the delta is purely false sharing.

namespace {

// The cache-line size to pad to. std::hardware_destructive_interference_size is
// the standard name for "how far apart two variables must be to stop hurting
// each other," but some libc++ versions don't expose it, so fall back to 64
// (the line size on both Apple Silicon and x86) when the feature isn't present.
#if defined(__cpp_lib_hardware_interference_size)
inline constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

// One ring, parameterized by the alignment applied to its two counters. This is
// the SpscRing logic from core/spsc_ring.hpp verbatim (same memory orders); the
// only knob is CounterAlign. With CounterAlign == alignof(atomic) the counters
// pack adjacently into one line (false sharing); with CounterAlign ==
// kCacheLine each counter starts its own line (no false sharing).
template <class T, std::size_t Capacity, std::size_t CounterAlign>
class RingExp {
  static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be power of two");

 public:
  RingExp() = default;
  RingExp(const RingExp&) = delete;
  RingExp& operator=(const RingExp&) = delete;

  bool try_push(const T& value) noexcept {
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    const std::uint64_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= Capacity) return false;
    buffer_[head & kMask] = value;
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  bool try_pop(T& out) noexcept {
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    if (head == tail) return false;
    out = buffer_[tail & kMask];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

 private:
  static constexpr std::uint64_t kMask = Capacity - 1;

  std::array<T, Capacity> buffer_{};
  alignas(CounterAlign) std::atomic<std::uint64_t> head_{0};
  alignas(CounterAlign) std::atomic<std::uint64_t> tail_{0};
};

constexpr std::size_t kCapacity = 1024;

// Packed: counters get only their natural alignment, so they land adjacent in
// the same cache line -> false sharing.
using SharedLineRing = RingExp<std::uint64_t, kCapacity,
                               alignof(std::atomic<std::uint64_t>)>;
// Padded: each counter is aligned to a full cache line -> separate lines.
using PaddedRing = RingExp<std::uint64_t, kCapacity, kCacheLine>;

// Padding must actually make the object bigger, else the two rows below would
// be measuring the same layout. This is the compile-time proof the experiment
// is honest.
static_assert(sizeof(PaddedRing) > sizeof(SharedLineRing),
              "padding did not separate the counters");

// Same cross-thread driver as bench_ring_vs_mutex: producer pushes [0,kN), the
// benchmarked thread pops all kN. Templated so both rings run identical logic.
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

template <class Ring>
void run(benchmark::State& state) {
  const auto kN = static_cast<std::uint64_t>(state.range(0));
  for (auto _ : state) {
    Ring q;
    drive(q, kN);
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(kN));
}

void BM_Ring_SharedLine(benchmark::State& state) { run<SharedLineRing>(state); }
void BM_Ring_Padded(benchmark::State& state) { run<PaddedRing>(state); }

// --- Isolated false sharing ------------------------------------------------
// The ring only shows ~6% because its head/tail contention is buried under the
// buffer handoff traffic. To see false sharing in its PURE form, strip away
// everything else: two counters, two threads, each thread doing nothing but
// hammering ITS OWN counter in a tight loop. Now the only cross-core traffic is
// the (false) contention on the shared line, so the collapse is dramatic.
template <std::size_t Align>
struct TwoCounters {
  alignas(Align) std::atomic<std::uint64_t> a{0};
  alignas(Align) std::atomic<std::uint64_t> b{0};
};

// Same honesty check as the ring: padding must actually change the layout.
static_assert(sizeof(TwoCounters<kCacheLine>) > sizeof(TwoCounters<alignof(std::atomic<std::uint64_t>)>),
              "padding did not separate the two counters");

template <std::size_t Align>
void run_counters(benchmark::State& state) {
  const auto kN = static_cast<std::uint64_t>(state.range(0));
  for (auto _ : state) {
    TwoCounters<Align> c;
    // Thread A pounds a; the benchmarked thread pounds b. fetch_add is a
    // read-modify-write: it can't retire into the store buffer like a plain
    // store, so each increment must actually OWN the cache line before it can
    // complete -- which is exactly where false sharing serializes the two cores.
    std::thread ta([&] {
      for (std::uint64_t i = 0; i < kN; ++i)
        c.a.fetch_add(1, std::memory_order_relaxed);
    });
    for (std::uint64_t i = 0; i < kN; ++i)
      c.b.fetch_add(1, std::memory_order_relaxed);
    ta.join();
    benchmark::DoNotOptimize(c.a.load(std::memory_order_relaxed));
    benchmark::DoNotOptimize(c.b.load(std::memory_order_relaxed));
  }
  state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) *
                          static_cast<std::int64_t>(kN) * 2);  // both threads
}

void BM_Counters_SharedLine(benchmark::State& state) {
  run_counters<alignof(std::atomic<std::uint64_t>)>(state);
}
void BM_Counters_Padded(benchmark::State& state) { run_counters<kCacheLine>(state); }

}  // namespace

BENCHMARK(BM_Ring_SharedLine)->Arg(1 << 20)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Ring_Padded)->Arg(1 << 20)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Counters_SharedLine)->Arg(1 << 24)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Counters_Padded)->Arg(1 << 24)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
