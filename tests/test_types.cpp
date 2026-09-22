#include "core/types.hpp"

#include <gtest/gtest.h>

#include <type_traits>

using hft::core::FixedPrice;
using hft::core::Qty;

// --- compile-time guarantees ----------------------------------------------
// Trivially copyable and 8 bytes: fits a ring slot, memcpy-able, no surprises.
static_assert(std::is_trivially_copyable_v<FixedPrice>);
static_assert(std::is_trivially_copyable_v<Qty>);
static_assert(sizeof(FixedPrice) == 8);
static_assert(sizeof(Qty) == 8);

// Arithmetic and comparison are usable in constant expressions (proves the
// math is integer/constexpr, no runtime float creeping in).
static_assert(FixedPrice(100) + FixedPrice(50) == FixedPrice(150));
static_assert(FixedPrice(100) < FixedPrice(200));
static_assert(Qty(3) + Qty(4) == Qty(7));

// Strong typing: same-type arithmetic works, mixing price and qty does not.
// (Expressed as a concept so the ill-formed case resolves to false under
// substitution rather than producing a hard error.)
template <class A, class B>
concept Addable = requires(A a, B b) { a + b; };
static_assert(Addable<FixedPrice, FixedPrice>);
static_assert(Addable<Qty, Qty>);
static_assert(!Addable<FixedPrice, Qty>);

// --- arithmetic ------------------------------------------------------------
TEST(FixedPrice, AddSubtract) {
  FixedPrice a(11000), b(500);
  EXPECT_EQ((a + b).raw(), 11500);
  EXPECT_EQ((a - b).raw(), 10500);
  a += b;
  EXPECT_EQ(a.raw(), 11500);
  a -= FixedPrice(1500);
  EXPECT_EQ(a.raw(), 10000);
}

TEST(FixedPrice, Comparisons) {
  EXPECT_TRUE(FixedPrice(100) < FixedPrice(200));
  EXPECT_TRUE(FixedPrice(200) > FixedPrice(100));
  EXPECT_TRUE(FixedPrice(150) == FixedPrice(150));
  EXPECT_TRUE(FixedPrice(150) <= FixedPrice(150));
  EXPECT_TRUE(FixedPrice(150) >= FixedPrice(150));
  EXPECT_NE(FixedPrice(150), FixedPrice(151));
}

TEST(Qty, AddSubtractCompare) {
  EXPECT_EQ((Qty(10) + Qty(5)).raw(), 15);
  EXPECT_EQ((Qty(10) - Qty(5)).raw(), 5);
  EXPECT_TRUE(Qty(5) < Qty(10));
}

// --- tick rounding (the gate) ---------------------------------------------
// tick = 0.0100 -> 100 scaled units. Grid: ..., 12300, 12400, ...
TEST(FixedPriceTick, RoundDownUpNearest) {
  const FixedPrice tick(100);

  // 12345: 45 above 12300, 55 below 12400 -> nearest is down.
  EXPECT_EQ(FixedPrice(12345).round_down_to_tick(tick).raw(), 12300);
  EXPECT_EQ(FixedPrice(12345).round_up_to_tick(tick).raw(), 12400);
  EXPECT_EQ(FixedPrice(12345).round_to_nearest_tick(tick).raw(), 12300);

  // 12399: nearest is up.
  EXPECT_EQ(FixedPrice(12399).round_to_nearest_tick(tick).raw(), 12400);
}

TEST(FixedPriceTick, ExactMultipleUnchanged) {
  const FixedPrice tick(100);
  EXPECT_EQ(FixedPrice(12300).round_down_to_tick(tick).raw(), 12300);
  EXPECT_EQ(FixedPrice(12300).round_up_to_tick(tick).raw(), 12300);
  EXPECT_EQ(FixedPrice(12300).round_to_nearest_tick(tick).raw(), 12300);
}

TEST(FixedPriceTick, HalfwayRoundsUp) {
  const FixedPrice tick(100);
  // 12350 is exactly between 12300 and 12400 -> tie rounds up.
  EXPECT_EQ(FixedPrice(12350).round_to_nearest_tick(tick).raw(), 12400);
}

TEST(FixedPriceTick, NegativeUsesFloorCeil) {
  const FixedPrice tick(100);
  // Grid around -150: -200 (down/floor), -100 (up/ceil), tie -> up.
  EXPECT_EQ(FixedPrice(-150).round_down_to_tick(tick).raw(), -200);
  EXPECT_EQ(FixedPrice(-150).round_up_to_tick(tick).raw(), -100);
  EXPECT_EQ(FixedPrice(-150).round_to_nearest_tick(tick).raw(), -100);
}
