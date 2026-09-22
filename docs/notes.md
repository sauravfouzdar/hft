# Build notes

A running log of the *why* behind each step. One short entry per PROGRESS item.

---

## 0.1 Toolchain

Build with **Homebrew LLVM clang** (`/opt/homebrew/opt/llvm/bin/clang++`), not
Apple clang and not GCC. On this Apple Silicon Mac it was the only choice that
gives both working sanitizers (ASan/UBSan/TSan) and a C++23 standard library
new enough for `std::print`. GCC 15 has the hot-path features but ships no
sanitizer runtime on Darwin; Apple clang has sanitizers but an old libc++.
LLVM is keg-only, so CMake adds `-L/-rpath` to its `lib/c++` to use its libc++
at link and run time. Two build dirs: `Release` (`-O3 -march=native -flto`) and
`Asan` (`-fsanitize=address,undefined`). Tests run under Asan.

LTO gotcha: `-flto` puts LLVM bitcode in `.o`/`.a`; Apple's `/usr/bin/ar` can't
index a bitcode archive ("archive member '/' not a mach-o file"). CMake hard-
bakes the archive command at language-init, so we override
`CMAKE_CXX_ARCHIVE_CREATE/APPEND/FINISH` to use `llvm-ar`/`llvm-ranlib`.

## 0.2 TSC clock — why not steady_clock on the hot path?

We need to timestamp hot-path hops to measure latency tails. The instinct is
`std::chrono::steady_clock::now()`. The problem is *what that call costs and how
predictably*:

- **steady_clock::now() is an opaque library call.** On macOS it routes through
  `mach_absolute_time()` — a non-inlined function-call boundary plus a timebase
  multiply. On Linux it may hit the VDSO or, in bad cases, a real syscall. The
  cost is real (tens of ns), variable, and invisible at the call site. When the
  thing you are measuring is itself sub-microsecond, the *measurement* overhead
  pollutes the number you are trying to read, and its jitter shows up in your
  tails — the exact metric you care about.

- **A raw counter read is one instruction.** On AArch64 that is `mrs cntvct_el0`
  (the architected generic-timer virtual count; free-running, constant-rate,
  monotonic, EL0-readable, no syscall). On x86 it is `rdtsc`. It is inlinable
  and has deterministic cost — so it barely perturbs what it measures.

- **Keep conversion off the hot path.** The counter yields raw *ticks*. We
  convert ticks→ns exactly once, later, in the reporting path, using a factor
  measured at startup (`TscClock::calibrate`). No division/float ever touches
  the hot loop; the hot loop only does the single-instruction read and stores a
  `uint64_t`.

Calibration measures the *same* wall interval with both clocks and takes the
ratio ns/tick. It is robust to sleep overshoot because both clocks advance in
real time, so a longer interval scales both numerator and denominator equally.
Measured here: ns/tick ≈ 1.0000 — i.e. `CNTVCT_EL0` runs at ~1 GHz on this
machine (newer Apple Silicon; older parts report 24 MHz). The empirical
calibration, not the `CNTFRQ_EL0` register, is the source of truth.

## 0.3 Latency histogram — think in tails, not averages

The mean latency is a lie in trading: it hides the p99.9 spike that gets you
picked off. So `telemetry/latency_hist` reports p50/p99/p99.9/max and
deliberately has no mean().

Design for the hot path: a fixed array of bucket counters sized once at
construction. `record(v)` is one increment, `buckets[v/width]++`, plus exact
min/max tracking — O(1), allocation-free. Percentiles are computed later, off
the hot path, by the **nearest-rank** method: the buckets already are the
sorted order, so walk them low→high accumulating counts and stop at the bucket
where the running total first reaches `ceil(p*N)`; report that bucket's lower
edge. Resolution is the bucket width; with width=1 it is exact, which is how
the test pins percentiles to a known 1..1000 distribution. min/max are tracked
exactly on the side so the extreme tail survives bucket quantization and
overflow.

Subtlety worth remembering: nearest-rank p50 of values 0..999 is the rank-500
value = 499 (ranks are 1-based), not 500. Off-by-one here is easy; the test
encodes it explicitly.

## 1.1 Fixed-point types — why price is an integer, never a float

`core/types` defines `FixedPrice` and `Qty` as scaled `int64` (stored value =
units of 1/scale; kPriceScale=1e4, kQtyScale=1e6). No float anywhere in the
type. Reasons: (1) exactness — 0.10 has no exact binary form, so float prices
drift and `==` is unreliable; the integer 11000 IS exactly 1.1000. (2) reality
— exchanges quote on a discrete tick grid, so price is already integer ticks.
(3) determinism — integer math is bit-identical across machines, which the
Phase 4.6 keystone needs. (4) speed/safety — integer compare is cheap, and the
two are DISTINCT types so `price + qty` won't compile (verified with a concept
static_assert).

Tick rounding snaps a price to the grid: `round_down` (passive bid),
`round_up` (passive ask), `round_to_nearest` (ties up). All built on one
helper `floor_to_multiple(v,t)` that does a true floor (correct for negatives,
since C++ integer division truncates toward zero). Ceil = -floor(-v).

Gotcha hit: an inline `static_assert(!requires(FixedPrice p, Qty q){p+q;})`
with concrete types is a HARD compile error in clang, not a false requirement.
Wrap the check in a `concept` over template params so substitution makes the
ill-formed case resolve to false.

## 1.2 Naive mutex queue — the baseline to beat

`core/mutex_queue` is `std::deque` behind a `std::mutex`, bounded, with a
non-blocking `try_push`/`try_pop` API (matches the lock-free ring's API so 1.4
is apples-to-apples). Every op takes the lock even with one producer + one
consumer and no real contention — that lock/unlock pair plus the mutex
cache-line bouncing between cores is exactly the cost we will beat in 1.3/1.4.

**Baseline throughput (Release, this machine):** ~22.5M items/sec to move 1M
`uint64` across two threads (~51 ms). Record this; the ring should crush it.

Google Benchmark entered here (brew, `find_package(benchmark CONFIG)`).
Benchmarks build under `HFT_BUILD_BENCHMARKS` and are only meaningful from the
Release dir — never run them under Asan. macOS notes: can't set thread
affinity (revisit Phase 5.2) and the "24 MHz CPU" line is bogus metadata, not a
measurement problem.

Test gotcha worth remembering: a busy-wait SPSC test must NOT use a spin-count
cap as its anti-hang net — under a sanitizer the fast side racks up hundreds of
millions of empty polls waiting on the slow side and trips a count cap
falsely (saw got=396612/1e6). Use a wall-clock DEADLINE (checked every ~1M
spins) instead; robust regardless of spin speed.

## 1.3 SPSC lock-free ring — the memory orders chosen

`core/spsc_ring` is a fixed power-of-two array with two monotonically growing
64-bit counters: `head_` (producer's next write) and `tail_` (consumer's next
read). `head_ - tail_` is the exact queue depth (no empty/full ambiguity) and
the slot index is `counter & (Capacity-1)` — a mask, not a modulo, which is why
capacity must be a power of two.

The ordering, and why each choice:

- **Producer `try_push`:** load `head_` **relaxed** (we are the only writer of
  head_, can't race with ourselves), load `tail_` **acquire** (pairs with the
  consumer's release store of tail_, so we see slots it has freed and never
  overwrite an unread one). Write the slot, then store `head_` **release** —
  that release publishes BOTH the slot write and the new head to the consumer.
- **Consumer `try_pop`:** load `tail_` **relaxed** (our own), load `head_`
  **acquire** (pairs with the producer's release store of head_, making the
  slot write happen-before our read — no torn/stale data). Read the slot, then
  store `tail_` **release** to tell the producer the slot is reusable.

Rule of thumb that falls out: each counter has exactly one writer → that thread
reads its OWN counter relaxed and the OTHER counter acquire; the single store
that publishes shared data is release.

Verified, not assumed: the correctness test passes under **TSan** clean. To
prove TSan is real (not a false green) and that the ordering is load-bearing, I
temporarily downgraded the head_ release/acquire to relaxed — TSan immediately
reported a data race on the buffer read, even though the functional result
still happened to print OK. That last part is the lesson: for lock-free code,
"the test passed" is not evidence of correctness; the race check is.

Throughput preview (Asan, ~same machine state): ring SPSC transfer of 1M items
~59 ms vs the mutex queue ~86 ms. The real apples-to-apples Release comparison
is step 1.4.

New build config: **Tsan** (`-fsanitize=thread`, its own `build-tsan` dir;
cannot combine with ASan). Concurrency tests run there.
