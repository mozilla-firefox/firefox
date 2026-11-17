/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/Vector.h"

#include <utility>

using mozilla::Vector;

#if defined(DEBUG) || defined(MOZ_ASAN) || defined(FUZZING)
static constexpr const char* kEmptyRangeMessage = "Assertion failure";
#else
static constexpr const char* kEmptyRangeMessage = "";
#endif
#if defined(DEBUG) || defined(MOZ_ASAN) || defined(FUZZING)
static constexpr const char* kInvalidIndexMessage = "ElementAt";
#else
static constexpr const char* kInvalidIndexMessage = "";
#endif

TEST(VectorRangesDeathTest, Empty)
{
  Vector<int> vec;
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  ASSERT_DEATH_IF_SUPPORTED(range.front(), kEmptyRangeMessage);
  ASSERT_DEATH_IF_SUPPORTED(range.popFront(), kEmptyRangeMessage);
  ASSERT_DEATH_IF_SUPPORTED(range.popCopyFront(), kEmptyRangeMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.front(), kEmptyRangeMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.popFront(), kEmptyRangeMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.popCopyFront(), kEmptyRangeMessage);
}

TEST(VectorRangesDeathTest, InitiallyEmptyThenGrown)
{
  Vector<int> vec;
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  ASSERT_TRUE(vec.append(1));
  ASSERT_TRUE(range.empty());
  ASSERT_TRUE(constRange.empty());
  ASSERT_DEATH_IF_SUPPORTED(range.front(), "");
  ASSERT_DEATH_IF_SUPPORTED(range.popFront(), "");
  ASSERT_DEATH_IF_SUPPORTED(range.popCopyFront(), "");
  ASSERT_DEATH_IF_SUPPORTED(constRange.front(), "");
  ASSERT_DEATH_IF_SUPPORTED(constRange.popFront(), "");
  ASSERT_DEATH_IF_SUPPORTED(constRange.popCopyFront(), "");
}

TEST(VectorRangesDeathTest, ExhaustedThenGrown)
{
  Vector<int> vec;
  ASSERT_TRUE(vec.append(1));
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  range.popFront();
  constRange.popFront();
  ASSERT_TRUE(range.empty());
  ASSERT_TRUE(constRange.empty());
  ASSERT_EQ(range.remain(), 0U);
  ASSERT_EQ(constRange.remain(), 0U);
  ASSERT_DEATH_IF_SUPPORTED(range.popFront(), kEmptyRangeMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.popFront(), kEmptyRangeMessage);
  ASSERT_TRUE(vec.append(2));
  ASSERT_EQ(range.remain(), 0U);
  ASSERT_EQ(constRange.remain(), 0U);
  ASSERT_DEATH_IF_SUPPORTED(range.front(), "");
  ASSERT_DEATH_IF_SUPPORTED(range.popFront(), "");
  ASSERT_DEATH_IF_SUPPORTED(range.popCopyFront(), "");
  ASSERT_DEATH_IF_SUPPORTED(constRange.front(), "");
  ASSERT_DEATH_IF_SUPPORTED(constRange.popFront(), "");
  ASSERT_DEATH_IF_SUPPORTED(constRange.popCopyFront(), "");
}

TEST(VectorRangesDeathTest, HeapOwnerMoved)
{
  Vector<int, 0> vec;
  ASSERT_TRUE(vec.append(1));
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  Vector<int, 0> moved(std::move(vec));
  ASSERT_EQ(moved.length(), 1U);
  ASSERT_DEATH_IF_SUPPORTED(range.front(), "");
  ASSERT_DEATH_IF_SUPPORTED(constRange.front(), "");
}

TEST(VectorRangesTest, InlineOwnerMoved)
{
  Vector<int, 4> vec;
  ASSERT_TRUE(vec.append(1));
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  Vector<int, 4> moved(std::move(vec));
  ASSERT_EQ(moved.length(), 1U);
  ASSERT_EQ(range.front(), 1);
  ASSERT_EQ(constRange.front(), 1);
}

TEST(VectorRangesDeathTest, Shrunk)
{
  Vector<int, 4> vec;
  ASSERT_TRUE(vec.append(1));
  ASSERT_TRUE(vec.append(2));
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  range.popFront();
  constRange.popFront();
  vec.shrinkTo(1);
  ASSERT_DEATH_IF_SUPPORTED(range.front(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(range.popCopyFront(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.front(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.popCopyFront(), kInvalidIndexMessage);
  range.popFront();
  constRange.popFront();
  ASSERT_TRUE(range.empty());
  ASSERT_TRUE(constRange.empty());
}

TEST(VectorRangesDeathTest, Cleared)
{
  Vector<int, 4> vec;
  ASSERT_TRUE(vec.append(1));
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  vec.clear();
  ASSERT_DEATH_IF_SUPPORTED(range.front(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(range.popCopyFront(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.front(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.popCopyFront(), kInvalidIndexMessage);
}

TEST(VectorRangesDeathTest, ClearedAndFreed)
{
  Vector<int> vec;
  ASSERT_TRUE(vec.append(1));
  auto range = vec.all();
  auto constRange = std::as_const(vec).all();
  vec.clearAndFree();
  ASSERT_DEATH_IF_SUPPORTED(range.front(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(range.popCopyFront(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.front(), kInvalidIndexMessage);
  ASSERT_DEATH_IF_SUPPORTED(constRange.popCopyFront(), kInvalidIndexMessage);
}
