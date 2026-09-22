# PROGRESS

The build state. The `/hft-build` skill reads this, works the first unchecked step, and ticks the box only after the "done when" gate passes. One step per session.

Mark a box `[x]` only when its gate is genuinely met. Do not tick ahead.

---

## Phase 0: Foundations

- [x] **0.1 Toolchain** — Goal: a reproducible C++23 build.
  Build: CMakeLists with C++23, an `app/hello` target, release flags (`-O3 -march=native -flto`), and a separate sanitizer build (`-fsanitize=address,undefined`).
  Done when: both build configs compile and `hello` runs from each.

- [x] **0.2 TSC clock** — Goal: measure in nanoseconds, and understand why `steady_clock` isn't enough on the hot path.
  Build: `core/tsc_clock` with a calibration step that maps TSC ticks to ns.
  Done when: a test prints a measured 1ms sleep as ~1,000,000 ns within tolerance, and a short note in the commit explains TSC vs `steady_clock`.

- [x] **0.3 Latency histogram** — Goal: think in tails, not averages.
  Build: `telemetry/latency_hist` recording samples and reporting p50, p99, p99.9, and max.
  Done when: a test feeds a known distribution and the percentiles come back correct.

---

## Phase 1: Core primitives (the meat)

- [x] **1.1 Fixed-point types** — Goal: understand why price is an integer, never a float.
  Build: `core/types` with `FixedPrice` and `Qty` as scaled int64, plus add/compare.
  Done when: tests cover tick rounding and there is zero floating point in the type.

- [x] **1.2 Naive mutex queue (the baseline to beat)** — Goal: build the obvious thing first.
  Build: a `std::mutex` + `std::deque` SPSC queue.
  Done when: a single-producer single-consumer test passes, and a benchmark records its throughput.

- [x] **1.3 SPSC lock-free ring** — Goal: lock-free single-producer single-consumer, and the memory-order reasoning behind it.
  Build: `core/spsc_ring`, power-of-two capacity, acquire/release on head and tail.
  Done when: correctness test passes under TSan, and a commit note explains the memory orders chosen.

- [x] **1.4 Ring vs mutex benchmark** — Goal: feel the gap with your own eyes.
  Build: a Google Benchmark comparing 1.2 and 1.3 at the same message rate.
  Done when: both numbers are recorded in the commit, and you can state the ratio and why.
  Result (`benchmarks/bench_ring_vs_mutex.cpp`, Release `-O3 -march=native -flto`, 1.05M items/run):
  mutex ~4.4 M items/s (median), ring ~19.5 M items/s (median) → **~4.4× throughput**.
  Consistency gap is larger: mutex cv ~29% (ramps 114→257 ms across reps, P/E-core migration),
  ring cv ~0.65%. Why: the mutex pays an unconditional lock/unlock (atomic RMW ×2) per op and
  its lock line ping-pongs between cores; the ring is a couple of acquire/release atomics on
  single-writer counters + a masked array write, so no lock and far less coherence traffic.
  (No git repo, so numbers recorded here in PROGRESS instead of a commit.)

- [ ] **1.5 False-sharing experiment** — Goal: prove cache lines are real.
  Build: two ring variants, one with head/tail on the same cache line, one padded to `hardware_destructive_interference_size`.
  Done when: the benchmark shows the throughput collapse and recovery, both numbers recorded.

- [ ] **1.6 Object pool** — Goal: a hot path that never calls `new`.
  Build: `core/object_pool`, fixed capacity, O(1) acquire/release.
  Done when: a test exhausts and recycles the pool with zero heap allocation after construction (verify with a counting allocator or ASan).

---

## Phase 2: Synthetic feed + event model

- [ ] **2.1 Normalized event** — Goal: one internal shape for all market data.
  Build: `feed/market_event` (Trade, Quote, BookDelta, Status) as a tagged union.
  Done when: it compiles, is trivially copyable, and fits a ring slot.

- [ ] **2.2 Synthetic generator** — Goal: understand the shape of order flow.
  Build: `feed/adapters/synthetic_feed`, random-walk mid, Poisson arrivals, occasional size spikes, seeded for determinism.
  Done when: a fixed seed produces a byte-identical event stream across two runs.

- [ ] **2.3 First loop, single thread** — Goal: see the whole pipe move.
  Build: generator into SPSC ring into a consumer that just counts and timestamps.
  Done when: it runs at a target rate (start with 1M events/sec) and the histogram shows the per-event cost.

---

## Phase 3: Order book

- [ ] **3.1 Book structure** — Goal: a cache-friendly book.
  Build: `book/order_book`, flat price levels, fast top-of-book.
  Done when: it builds and holds a bounded depth without allocation after construction.

- [ ] **3.2 Apply logic** — Goal: turn events into book state.
  Build: apply for trades, quotes, and deltas; maintain best bid/ask.
  Done when: a scripted event sequence produces the expected book.

- [ ] **3.3 Correctness tests** — Goal: handle the ugly cases.
  Build: tests for crossed books, out-of-order updates, and depth eviction.
  Done when: all pass, including the adversarial ones.

- [ ] **3.4 Book benchmark** — Goal: know what apply costs.
  Build: benchmark `book.apply` over the synthetic stream, feed the histogram.
  Done when: p50/p99/p99.9 for apply are recorded.

---

## Phase 4: Close the loop offline (the keystone)

- [ ] **4.1 Strategy interface + trivial strategy** — Goal: signal to intent.
  Build: `strategy/i_strategy` and one strategy that emits an order on a simple condition.
  Done when: it produces `OrderIntent`s from the synthetic stream.

- [ ] **4.2 Risk pre-trade checks** — Goal: nothing reaches the wire unchecked.
  Build: `risk/pretrade_checks`, position limit, max size, max notional, rate limit, price band, kill switch.
  Done when: tests prove each rule rejects, and the path is allocation-free.

- [ ] **4.3 OMS state machine** — Goal: track an order's life.
  Build: `oms/order` (New, PendingNew, Acked, PartiallyFilled, Filled, Canceled, Rejected) and a preallocated `order_store`.
  Done when: state transition tests pass, including illegal-transition rejection.

- [ ] **4.4 Tiny matching engine** — Goal: understand the exchange side.
  Build: `sim/matching_engine`, price-time priority, partial fills.
  Done when: scripted orders produce the correct fills and resting book.

- [ ] **4.5 Sim gateway** — Goal: close the loop with no network.
  Build: `gateway/adapters/sim_match_gw` routing intents into the matcher and fills back to the OMS.
  Done when: a strategy trades end to end against the matcher.

- [ ] **4.6 Determinism test (KEYSTONE)** — Goal: the whole point.
  Build: replay one recorded synthetic session through the full loop twice.
  Done when: the two order streams are byte-identical.

---

## Phase 5: Threading

- [ ] **5.1 Split feed and trading** — Goal: two threads, one ring.
  Build: feed thread produces, trading thread consumes; fills return on their own ring.
  Done when: behavior matches the single-threaded loop and the determinism test still passes.

- [ ] **5.2 Affinity + cross-thread cost** — Goal: measure what the split costs.
  Build: pin both threads with `core/cpu`, benchmark the ring hop across cores.
  Done when: the cross-thread hop latency is recorded.

- [ ] **5.3 Tail regression guard** — Goal: catch slowdowns automatically.
  Build: a test that fails if p99.9 of the hot path regresses past a threshold.
  Done when: it passes now and fails when you deliberately add a slow line.

---

## Phase 6: Real feed (last)

- [ ] **6.1 Websocket client** — Goal: control-plane IO, off the hot path.
  Build: `net/ws_client` (Boost.Beast/Asio), connect, subscribe, reconnect.
  Done when: it connects to the Alpaca test stream and logs messages.

- [ ] **6.2 Alpaca adapter** — Goal: normalize a real feed.
  Build: `feed/adapters/alpaca_feed`, decode with simdjson, emit `MarketEvent`s.
  Done when: live IEX quotes flow through the same ring the synthetic feed uses.

- [ ] **6.3 Recorder** — Goal: capture real sessions for deterministic replay.
  Build: `tools/record_feed` writing normalized events to disk.
  Done when: a captured file replays through `replay_feed` identically.

- [ ] **6.4 Replay a real session** — Goal: stress the loop with messy live data.
  Build: run a captured real session through the full offline loop.
  Done when: the loop handles real gaps and bursts without crashing or allocating.

- [ ] **6.5 (Optional) Paper execution + India** — Goal: a live fill path.
  Build: Alpaca paper gateway; then Angel One SmartAPI or ICICI Breeze adapter for NSE/BSE L1.
  Done when: a paper order fills, and (if pursued) an India L1 feed flows through the ring.