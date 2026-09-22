#pragma once

#include <compare>
#include <cstdint>

// core/types -- money types as scaled integers. NEVER floats.
//
// Why not double for price?
//   * Exactness: 0.10 has no exact binary representation. Sum fills or PnL in
//     double and the error accumulates; compare two "equal" prices and they
//     can differ in the last bits. The integer 11000 (scale 1e4) IS exactly
//     1.1000, and == is a plain, reliable integer compare.
//   * Reality: exchanges quote on a discrete tick grid -- a price is already
//     an integer number of ticks. Float pretends otherwise.
//   * Determinism: integer math is bit-identical across machines/compilers,
//     which the Phase 4.6 determinism keystone depends on. Float reordering /
//     FMA / fast-math do not promise that.
//
// A FixedPrice stores units of (1 / kPriceScale). With kPriceScale = 10000,
// the stored value 11000 means the price 1.1000. FixedPrice and Qty are
// DISTINCT types on purpose, so the compiler rejects price + qty mistakes.

namespace hft::core {

inline constexpr std::int64_t kPriceScale = 10'000;  // 1e-4 price resolution
inline constexpr std::int64_t kQtyScale = 1'000'000;  // 1e-6 qty resolution

namespace detail {
// Largest multiple of t that is <= v (true floor, correct for negatives).
// t must be > 0. All integer math.
[[nodiscard]] constexpr std::int64_t floor_to_multiple(std::int64_t v,
                                                       std::int64_t t) noexcept {
  std::int64_t q = v / t;
  std::int64_t r = v % t;
  if (r != 0 && (r < 0))  // truncation rounded toward zero; step down for v<0
    --q;
  return q * t;
}
}  // namespace detail

class FixedPrice {
public:
  constexpr FixedPrice() noexcept = default;
  explicit constexpr FixedPrice(std::int64_t scaled) noexcept : scaled_(scaled) {}

  [[nodiscard]] constexpr std::int64_t raw() const noexcept { return scaled_; }

  // Arithmetic (price +/- price is a price). All integer, noexcept.
  friend constexpr FixedPrice operator+(FixedPrice a, FixedPrice b) noexcept {
    return FixedPrice(a.scaled_ + b.scaled_);
  }
  friend constexpr FixedPrice operator-(FixedPrice a, FixedPrice b) noexcept {
    return FixedPrice(a.scaled_ - b.scaled_);
  }
  constexpr FixedPrice& operator+=(FixedPrice o) noexcept {
    scaled_ += o.scaled_;
    return *this;
  }
  constexpr FixedPrice& operator-=(FixedPrice o) noexcept {
    scaled_ -= o.scaled_;
    return *this;
  }

  // Comparisons: defaulted, so == and <,<=,>,>= all come from the single
  // int64 member. Reliable integer ordering, no epsilon games.
  friend constexpr bool operator==(FixedPrice, FixedPrice) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(FixedPrice,
                                                    FixedPrice) noexcept = default;

  // Tick rounding: snap this price to the exchange's tick grid. `tick` is the
  // tick size expressed as a FixedPrice (e.g. FixedPrice(100) == 0.0100).
  //   * down  -> for resting a passive BID (never cross above your limit)
  //   * up    -> for resting a passive ASK
  //   * nearest-> generic snap, ties rounded up
  [[nodiscard]] constexpr FixedPrice round_down_to_tick(FixedPrice tick) const noexcept;
  [[nodiscard]] constexpr FixedPrice round_up_to_tick(FixedPrice tick) const noexcept;
  [[nodiscard]] constexpr FixedPrice round_to_nearest_tick(FixedPrice tick) const noexcept;

private:
  std::int64_t scaled_{0};
};

class Qty {
public:
  constexpr Qty() noexcept = default;
  explicit constexpr Qty(std::int64_t scaled) noexcept : scaled_(scaled) {}

  [[nodiscard]] constexpr std::int64_t raw() const noexcept { return scaled_; }

  friend constexpr Qty operator+(Qty a, Qty b) noexcept {
    return Qty(a.scaled_ + b.scaled_);
  }
  friend constexpr Qty operator-(Qty a, Qty b) noexcept {
    return Qty(a.scaled_ - b.scaled_);
  }
  constexpr Qty& operator+=(Qty o) noexcept {
    scaled_ += o.scaled_;
    return *this;
  }
  constexpr Qty& operator-=(Qty o) noexcept {
    scaled_ -= o.scaled_;
    return *this;
  }

  friend constexpr bool operator==(Qty, Qty) noexcept = default;
  friend constexpr std::strong_ordering operator<=>(Qty, Qty) noexcept = default;

private:
  std::int64_t scaled_{0};
};

// --- tick rounding --------------------------------------------------------
constexpr FixedPrice FixedPrice::round_down_to_tick(FixedPrice tick) const noexcept {
  // Floor to the grid: the highest tick at or below this price.
  return FixedPrice(detail::floor_to_multiple(scaled_, tick.raw()));
}

constexpr FixedPrice FixedPrice::round_up_to_tick(FixedPrice tick) const noexcept {
  // Ceil to the grid = -floor(-v): the lowest tick at or above this price.
  return FixedPrice(-detail::floor_to_multiple(-scaled_, tick.raw()));
}

constexpr FixedPrice FixedPrice::round_to_nearest_tick(FixedPrice tick) const noexcept {
  const std::int64_t t = tick.raw();
  const std::int64_t down = detail::floor_to_multiple(scaled_, t);
  const std::int64_t r = scaled_ - down;  // distance above the lower tick, 0..t-1
  if (r == 0) return FixedPrice(down);     // already on the grid
  // Closer to lower tick if 2r < t; ties (2r == t) round up.
  return FixedPrice(2 * r < t ? down : down + t);
}

}  // namespace hft::core
