# CLAUDE.md

Project memory for the from-scratch HFT learning engine. These rules are always in context, including when the `/hft-build` skill is not active.

## What this project is

A high-frequency-trading engine built by hand in C++23, to learn how HFT systems work. The deliverable is understanding, not a shipped product. Expect this to take months. That is the point.

## Non-negotiable working rules

1. **Never one-shot.** Implement one step from PROGRESS.md at a time. Do not implement, scaffold, or pre-wire later steps. After each step, stop and wait for review.
2. **Test or benchmark first** on core and hot-path code. Show it fail before making it pass.
3. **Offline first.** No real exchange API before Phase 6. Use the synthetic feed and replay. If asked to jump ahead, confirm explicitly first.
4. **Tick a box only when its gate truly passes.** Never fake green to move on.
5. **Explain the why.** When writing engine code, say what is being built and why, in plain terms. This is a teaching repo.

## Hot-path engineering constraints

- No heap allocation after startup on the hot path. Preallocate pools.
- No exceptions on the hot path. Use `std::expected` and error enums. Exceptions only at startup/teardown.
- Price and quantity are scaled integers, never floats.
- Cross-thread handoff is lock-free SPSC rings only. No mutexes on the hot path.
- Pad shared atomics to `hardware_destructive_interference_size`.
- Instrument hops with TSC timestamps; report p50, p99, p99.9, max.

## Toolchain

- C++23, GCC 14+ or Clang 18+, CMake + Ninja.
- Sanitizer build (ASan/UBSan/TSan) for tests.
- GoogleTest, Google Benchmark. simdjson and Boost.Beast/Asio only at Phase 6, off the hot path.

## How to start a work session

Run `/hft-build` (or just say you want to work on the project). It reads PROGRESS.md, works the next single step, and stops for review.