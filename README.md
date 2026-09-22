# HFT Engine (from scratch, for learning)

A high-frequency-trading engine built in C++23, by hand, one piece at a time. The goal is not a product. The goal is to understand why HFT systems are shaped the way they are, by building each part myself and measuring it.

This repo is wired so that **Claude Code implements it step by step, never all at once.** See "How to drive this" below.

---

## The one rule

Build it slowly. Each step gets implemented, tested, benchmarked, and reviewed before the next one starts. One-shotting the whole thing teaches nothing. The bugs and the lessons live in the friction.

---

## Why the build order is flipped

The instinct is to start with a real exchange API. That's the wrong end. Free websocket feeds trickle a few messages a second, they're millisecond-bound, and wiring one up teaches generic backend plumbing, not what makes trading systems fast.

The real lessons live in the offline core: the lock-free ring, the single-threaded hot path, zero-allocation discipline, and measuring in nanoseconds. All of that gets built and stressed with a synthetic feed, no API key required. The real feed goes in last, as a reality check that the adapters survive messy live data.

So the order is: make it correct, make it measured, make it fast, then connect it to the world.

---

## Build philosophy (the habits this project is teaching)

**Synthetic feed first, not a real one.** A generator producing order flow (random-walk mid-price, Poisson arrivals, occasional size spikes) gives infinite deterministic data and can be cranked to millions of events a second to actually stress the hot path. A live free feed at a few ticks a second can't expose a single false-sharing bug.

**Benchmark every core piece against the naive version.** Write the lock-free ring, then also write a dumb mutex-locked queue, and benchmark both. Feel the gap yourself. Then put two atomics on the same cache line, watch throughput collapse, pad them apart, watch it recover. Reading about cache lines does nothing. Watching a number drop 4x sticks forever.

**Single-threaded first.** Get tick-to-order working in one thread before splitting feed and trading across cores. Once it's correct and measured, split it and watch what the ring-buffer hop actually costs. Then the threading model is understood, not copied.

**Measure the tail from commit one.** Stamp a TSC counter at every hop and print p50, p99, p99.9, and max. HFT is a tail-latency discipline. The median is comforting and useless. The p99.9 is the real worst case, usually 10x the median.

**Build a tiny matching engine.** Price-time priority, partial fills, what an order does when it lands. It teaches the exchange side, it's deterministic, and it closes the loop with zero network.

---

## Phase map

Each phase is shippable and testable on its own. The granular, checkable steps live in `PROGRESS.md`. This is the paragraph-level overview.

**Phase 0: Foundations.** CMake + C++23 toolchain, sanitizer build, a TSC nanosecond clock, and a latency histogram. Before building anything fast, build the thing that measures fast.

**Phase 1: Core primitives.** Fixed-point price/qty types, a naive mutex queue as a baseline, the SPSC lock-free ring, a benchmark pitting them against each other, the false-sharing experiment, and an object pool. This is where most of the learning is.

**Phase 2: Synthetic feed.** The normalized `MarketEvent`, a generator that produces realistic order flow, and the first single-threaded loop: generator into ring into consumer.

**Phase 3: Order book.** A flat-level book, apply logic for trades and quotes and deltas, correctness tests for crossed and out-of-order books, and a benchmark of `book.apply` against the histogram.

**Phase 4: Close the loop offline.** Strategy interface, pre-trade risk checks, the OMS order state machine, a tiny matching engine, and a sim gateway that feeds fills back. Ends with the keystone test: replay the same feed twice, get an identical order stream.

**Phase 5: Threading.** Split feed and trading onto two pinned threads through the ring, set CPU affinity, measure the cross-thread hop, and add a tail-latency regression guard.

**Phase 6: Real feed (last).** A websocket client, an Alpaca adapter (US, free real-time IEX), a `record_feed` tool to capture live sessions to disk, and replay of a real captured session through the offline loop. India (Angel One SmartAPI or ICICI Breeze, both free, L1 only) comes after if wanted.

---

## Data sources (free / mock)

**US (NYSE / NASDAQ):** Alpaca Basic tier is free, gives real-time IEX equity quotes and trades over websocket, plus free paper-trade execution. One vendor covers feed and fills. Free is IEX-only and one websocket connection; full SIP coverage is the paid Algo Trader Plus.

**India (NSE / BSE):** No free real-time tick feed exists. Free means broker L1 over websocket: Angel One SmartAPI (free, needs account + TOTP) or ICICI Breeze (free, streaming OHLC, capped at 100 requests/min). True tick-by-tick depth means a licensed vendor (TrueData, GlobalDataFeeds) plus NSE/BSE colocation, all paid. Note: SEBI now mandates a static IP for API trading (effective April 2026), and exchange data licenses generally restrict use for simulation, so backtest on recorded or synthetic data.

**Mock / replay (works for any exchange):** the synthetic generator and the captured-session replay file. This is what 90% of the project runs on.

---

## How to drive this with Claude Code

The step-by-step discipline is enforced by three files:

- `.claude/skills/hft-build/SKILL.md` is the build coach. It loads automatically when you talk about the project, or you invoke it with `/hft-build`. It reads `PROGRESS.md`, works exactly one step, writes the test or benchmark first, runs it, then stops and waits for your review.
- `PROGRESS.md` is the living state. It holds every step as a checkbox with a "done when" gate. The skill ticks a box only after the gate passes.
- `CLAUDE.md` holds the always-on guardrails (never implement more than one step, keep the hot path allocation-free, stop and wait). It's loaded into every session, so the no-one-shot rule holds even when the skill isn't active.

**Typical loop:**

```
You:    /hft-build
Claude: [reads PROGRESS.md, finds the next unchecked step]
        [states the learning goal and what it's about to build]
        [writes the test/benchmark, then the minimum code to pass]
        [runs it, shows the numbers]
        [stops, suggests a commit message, ticks the box]
You:    [review the code, ask questions, commit]
You:    continue          # or /hft-build again for the next step
```

If you want to understand a step before Claude writes anything, ask it to explain the step first. The skill is built to teach, not just to type.

---

## Build & run

### Prerequisites

Install via [Homebrew](https://brew.sh):

```bash
brew install cmake ninja llvm googletest google-benchmark
```

The project builds with **Homebrew LLVM clang** (not Apple clang, not GCC): on
macOS it is the only compiler that gives both working sanitizers *and* a C++23
standard library new enough for `std::print`. It is keg-only, so we point CMake
at it explicitly below; `CMakeLists.txt` handles the rest (libc++ rpath, and
`llvm-ar` for the LTO archives).

### Configure (once)

Two out-of-source build directories:

- **Release** — `-O3 -march=native -flto`; the config the hot path runs under,
  and the only one where benchmark numbers mean anything.
- **Asan** — `-fsanitize=address,undefined`; the config the tests run under.

```bash
CLANG=/opt/homebrew/opt/llvm/bin/clang++

cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$CLANG
cmake -S . -B build-asan    -G Ninja -DCMAKE_BUILD_TYPE=Asan    -DCMAKE_CXX_COMPILER=$CLANG
```

You only re-run this configure step after editing `CMakeLists.txt`. For ordinary
source edits, just rebuild — Ninja recompiles only what changed.

### Build

```bash
cmake --build build-asan      # tests (sanitized)
cmake --build build-release   # optimized binaries + benchmarks
```

### Run

```bash
# All tests, failures shown verbosely (use the Asan build)
ctest --test-dir build-asan --output-on-failure

# A single test binary directly
./build-asan/test_types

# The app
./build-release/hello

# Benchmarks — Release ONLY (sanitizer instrumentation makes them meaningless)
./build-release/bench_mutex_queue
./build-release/bench_mutex_queue --benchmark_repetitions=3 --benchmark_report_aggregates_only=true
```

### Layout

```
core/         engine primitives (tsc_clock, types, mutex_queue, ...)
telemetry/    measurement (latency_hist)
app/          runnable entry points (hello)
tests/        GoogleTest unit tests  -> run under Asan
benchmarks/   Google Benchmark       -> run under Release
```

---

## 2. Tick-to-trade data flow
 
```
[ exchange / free API ]
        |  websocket or UDP/multicast (later)
        v
  ( Feed thread, pinned )
    socket read -> decode (simdjson / binary) -> normalize
        |
        v   SPSC ring buffer (lock-free, cache-aligned)
        |
  ( Trading thread, pinned, busy-poll )     <-- HOT PATH starts
    drain ring -> OrderBook.apply()
                -> Strategy.on_event() -> OrderIntent
                -> Risk.check(intent)   -> pass/reject
                -> OMS.register(intent) -> OrderRequest
        |
        v   SPSC ring buffer
        |                                     <-- HOT PATH ends
  ( Gateway thread, pinned )
    encode -> socket write -> exchange
 
  ( Telemetry thread, low priority )
    drains log + metric rings, writes to disk / stdout
```
 
The hot path is the middle block. It touches only preallocated memory and never blocks.
 
---

## Toolchain

- C++23, GCC 14+ or Clang 18+
- CMake + Ninja
- Debug builds with ASan/UBSan/TSan on
- GoogleTest for tests, Google Benchmark for microbenchmarks
- simdjson and Boost.Beast/Asio only at Phase 6, and only off the hot path