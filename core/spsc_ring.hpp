#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// core/spsc_ring -- a lock-free single-producer / single-consumer ring buffer.
// This is the cross-thread handoff the whole engine is built around, and the
// point of this step is the MEMORY ORDERING, not the data structure.
//
// Layout: a fixed power-of-two array plus two monotonically increasing 64-bit
// counters. `head_` is the next slot the producer will write; `tail_` is the
// next slot the consumer will read. Because they only ever grow, `head_ -
// tail_` is exactly the number of queued items (no empty/full ambiguity), and
// the slot index is `counter & (Capacity - 1)` -- a mask, not a modulo, which
// is why Capacity must be a power of two.
//
// The ordering (the part that matters):
//   * Producer writes the slot, THEN stores head_ with release. Consumer loads
//     head_ with acquire. That release->acquire edge makes the slot write
//     happen-before the consumer's read: no torn or stale data.
//   * Consumer advances tail_ with release after reading; producer loads tail_
//     with acquire before overwriting, so it never clobbers an unread slot.
//   * Each counter has exactly one writer, so that thread reads ITS OWN
//     counter relaxed (it can't race with itself) and the OTHER counter
//     acquire.
//
// Cache-line padding of head_/tail_ is deliberately left for step 1.5 (the
// false-sharing experiment); here they may share a line.

namespace hft::core {

template <class T, std::size_t Capacity>
class SpscRing {
  static_assert(Capacity >= 2, "capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be power of two");

 public:
  SpscRing() = default;
  SpscRing(const SpscRing&) = delete;
  SpscRing& operator=(const SpscRing&) = delete;

  // Producer side. Returns false if the ring is full (never blocks).
  bool try_push(const T& value) noexcept {
    // head_ is ours -> relaxed. tail_ is the consumer's -> acquire, so we see
    // its progress and never overwrite a slot it has not yet drained.
    const std::uint64_t head = head_.load(std::memory_order_relaxed);
    const std::uint64_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= Capacity) return false;  // full

    buffer_[head & kMask] = value;
    // Release publishes BOTH the slot write above and the new head to the
    // consumer's acquire-load of head_.
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  // Consumer side. Returns false if the ring is empty (never blocks).
  bool try_pop(T& out) noexcept {
    // tail_ is ours -> relaxed. head_ is the producer's -> acquire, which pairs
    // with its release store so the slot write happens-before this read.
    const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint64_t head = head_.load(std::memory_order_acquire);
    if (head == tail) return false;  // empty

    out = buffer_[tail & kMask];
    // Release tells the producer (via its acquire-load of tail_) that this slot
    // is now free to reuse.
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

 private:
  static constexpr std::uint64_t kMask = Capacity - 1;

  std::array<T, Capacity> buffer_{};  // fix memory block, pre-allocated
  std::atomic<std::uint64_t> head_{0};  // producer writes, consumer reads
  std::atomic<std::uint64_t> tail_{0};  // consumer writes, producer reads
};

}  // namespace hft::core