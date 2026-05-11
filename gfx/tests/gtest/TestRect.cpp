/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include <limits>

#include "gtest/gtest.h"

#include "gfxTypes.h"
#include "nsRect.h"
#include "nsRectAbsolute.h"
#include "gfxRect.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/gfx/RectAbsolute.h"
#include "mozilla/WritingModes.h"
#ifdef XP_WIN
#  include <windows.h>
#endif

using mozilla::CSSCoord;
using mozilla::CSSIntCoord;
using mozilla::CSSIntSize;
using mozilla::ScreenIntCoord;
using mozilla::gfx::IntPoint;
using mozilla::gfx::IntRect;
using mozilla::gfx::IntRectAbsolute;
using mozilla::gfx::Rect;

static_assert(std::is_constructible_v<CSSIntSize, CSSIntCoord, CSSIntCoord>);
static_assert(
    !std::is_constructible_v<CSSIntSize, ScreenIntCoord, ScreenIntCoord>);
static_assert(std::is_constructible_v<CSSIntSize, int, int>);
static_assert(!std::is_constructible_v<CSSIntSize, float, float>);

static_assert(std::is_same_v<CSSIntCoord, decltype(CSSIntCoord() * 42)>);
static_assert(std::is_same_v<CSSCoord, decltype(CSSCoord() * 42)>);
static_assert(std::is_same_v<CSSCoord, decltype(CSSIntCoord() * 42.f)>);
static_assert(std::is_same_v<CSSCoord, decltype(CSSCoord() * 42.f)>);

template <class RectType>
static bool TestRectConstructors() {
  // Create a rectangle
  RectType rect1(10, 20, 30, 40);

  // Make sure the rectangle was properly initialized
  EXPECT_TRUE(rect1.IsEqualRect(10, 20, 30, 40) && rect1.IsEqualXY(10, 20) &&
              rect1.IsEqualSize(30, 40))
      << "[1] Make sure the rectangle was properly initialized with "
         "constructor";

  // Create a second rect using the copy constructor
  RectType rect2(rect1);

  // Make sure the rectangle was properly initialized
  EXPECT_TRUE(rect2.IsEqualEdges(rect1) &&
              rect2.IsEqualXY(rect1.X(), rect1.Y()) &&
              rect2.IsEqualSize(rect1.Width(), rect1.Height()))
      << "[2] Make sure the rectangle was properly initialized with copy "
         "constructor";

  EXPECT_TRUE(!rect1.IsEmpty() && !rect1.IsZeroArea() && rect1.IsFinite() &&
              !rect2.IsEmpty() && !rect2.IsZeroArea() && rect2.IsFinite())
      << "[3] These rectangles are not empty and are finite";

  rect1.SetRect(1, 2, 30, 40);
  EXPECT_TRUE(rect1.X() == 1 && rect1.Y() == 2 && rect1.Width() == 30 &&
              rect1.Height() == 40 && rect1.XMost() == 31 &&
              rect1.YMost() == 42);

  rect1.SetRectX(11, 50);
  EXPECT_TRUE(rect1.X() == 11 && rect1.Y() == 2 && rect1.Width() == 50 &&
              rect1.Height() == 40 && rect1.XMost() == 61 &&
              rect1.YMost() == 42);

  rect1.SetRectY(22, 60);
  EXPECT_TRUE(rect1.X() == 11 && rect1.Y() == 22 && rect1.Width() == 50 &&
              rect1.Height() == 60 && rect1.XMost() == 61 &&
              rect1.YMost() == 82);

  rect1.SetBox(1, 2, 31, 42);
  EXPECT_TRUE(rect1.X() == 1 && rect1.Y() == 2 && rect1.Width() == 30 &&
              rect1.Height() == 40 && rect1.XMost() == 31 &&
              rect1.YMost() == 42);

  rect1.SetBoxX(11, 61);
  EXPECT_TRUE(rect1.X() == 11 && rect1.Y() == 2 && rect1.Width() == 50 &&
              rect1.Height() == 40 && rect1.XMost() == 61 &&
              rect1.YMost() == 42);

  rect1.SetBoxY(22, 82);
  EXPECT_TRUE(rect1.X() == 11 && rect1.Y() == 22 && rect1.Width() == 50 &&
              rect1.Height() == 60 && rect1.XMost() == 61 &&
              rect1.YMost() == 82);

  rect1.SetRect(1, 2, 30, 40);
  EXPECT_TRUE(rect1.X() == 1 && rect1.Y() == 2 && rect1.Width() == 30 &&
              rect1.Height() == 40 && rect1.XMost() == 31 &&
              rect1.YMost() == 42);

  rect1.MoveByX(10);
  EXPECT_TRUE(rect1.X() == 11 && rect1.Y() == 2 && rect1.Width() == 30 &&
              rect1.Height() == 40 && rect1.XMost() == 41 &&
              rect1.YMost() == 42);

  rect1.MoveByY(20);
  EXPECT_TRUE(rect1.X() == 11 && rect1.Y() == 22 && rect1.Width() == 30 &&
              rect1.Height() == 40 && rect1.XMost() == 41 &&
              rect1.YMost() == 62);

  return true;
}

template <class RectType>
static bool TestEqualityOperator() {
  RectType rect1(10, 20, 30, 40);
  RectType rect2(rect1);

  // Test the equality operator
  EXPECT_TRUE(rect1 == rect2) << "[1] Test the equality operator";

  EXPECT_FALSE(!rect1.IsEqualInterior(rect2))
      << "[2] Test the inequality operator";

  // Make sure that two empty rects are equal
  rect1.SetEmpty();
  rect2.SetEmpty();
  EXPECT_TRUE(rect1 == rect2) << "[3] Make sure that two empty rects are equal";

  return true;
}

template <class RectType, class UnitType>
static bool TestContainment() {
  RectType rect1(10, 10, 50, 50);

  // Test the point containment methods
  //

  // Basic test of a point in the middle of the rect
  EXPECT_TRUE(rect1.Contains(rect1.Center()) &&
              rect1.ContainsX(rect1.Center().x) &&
              rect1.ContainsY(rect1.Center().y))
      << "[1] Basic test of a point in the middle of the rect";

  // Test against a point at the left/top edges
  EXPECT_TRUE(rect1.Contains(rect1.X(), rect1.Y()) &&
              rect1.ContainsX(rect1.X()) && rect1.ContainsY(rect1.Y()))
      << "[2] Test against a point at the left/top edges";

  // Test against a point at the right/bottom extents
  EXPECT_FALSE(rect1.Contains(rect1.XMost(), rect1.YMost()) ||
               rect1.ContainsX(rect1.XMost()) || rect1.ContainsY(rect1.YMost()))
      << "[3] Test against a point at the right/bottom extents";

  // Test the rect containment methods
  //
  RectType rect2(rect1);

  // Test against a rect that's the same as rect1
  EXPECT_FALSE(!rect1.Contains(rect2))
      << "[4] Test against a rect that's the same as rect1";

  // Test against a rect whose left edge (only) is outside of rect1
  rect2.MoveByX(-1);
  EXPECT_FALSE(rect1.Contains(rect2))
      << "[5] Test against a rect whose left edge (only) is outside of rect1";
  rect2.MoveByX(1);

  // Test against a rect whose top edge (only) is outside of rect1
  rect2.MoveByY(-1);
  EXPECT_FALSE(rect1.Contains(rect2))
      << "[6] Test against a rect whose top edge (only) is outside of rect1";
  rect2.MoveByY(1);

  // Test against a rect whose right edge (only) is outside of rect1
  rect2.MoveByX(1);
  EXPECT_FALSE(rect1.Contains(rect2))
      << "[7] Test against a rect whose right edge (only) is outside of rect1";
  rect2.MoveByX(-1);

  // Test against a rect whose bottom edge (only) is outside of rect1
  rect2.MoveByY(1);
  EXPECT_FALSE(rect1.Contains(rect2))
      << "[8] Test against a rect whose bottom edge (only) is outside of rect1";
  rect2.MoveByY(-1);

  // Test rects approaching numeric limits can contain rects
  RectType rectLarge(10, 10, std::numeric_limits<UnitType>::max(),
                     std::numeric_limits<UnitType>::max());
  EXPECT_TRUE(rectLarge.Contains(rect2))
      << "[9] Test rect at numeric limits against a smaller rect";

  return true;
}

// Test the method that returns a boolean result but doesn't return a
// a rectangle
template <class RectType>
static bool TestIntersects() {
  RectType rect1(10, 10, 50, 50);
  RectType rect2(rect1);

  // Test against a rect that's the same as rect1
  EXPECT_FALSE(!rect1.Intersects(rect2))
      << "[1] Test against a rect that's the same as rect1";

  // Test against a rect that's enclosed by rect1
  rect2.Inflate(-1, -1);
  EXPECT_FALSE(!rect1.Contains(rect2) || !rect1.Intersects(rect2))
      << "[2] Test against a rect that's enclosed by rect1";
  rect2.Inflate(1, 1);

  // Make sure inflate and deflate worked correctly
  EXPECT_TRUE(rect1.IsEqualInterior(rect2))
      << "[3] Make sure inflate and deflate worked correctly";

  // Test against a rect that overlaps the left edge of rect1
  rect2.MoveByX(-1);
  EXPECT_FALSE(!rect1.Intersects(rect2))
      << "[4] Test against a rect that overlaps the left edge of rect1";
  rect2.MoveByX(1);

  // Test against a rect that's outside of rect1 on the left
  rect2.MoveByX(-rect2.Width());
  EXPECT_FALSE(rect1.Intersects(rect2))
      << "[5] Test against a rect that's outside of rect1 on the left";
  rect2.MoveByX(rect2.Width());

  // Test against a rect that overlaps the top edge of rect1
  rect2.MoveByY(-1);
  EXPECT_FALSE(!rect1.Intersects(rect2))
      << "[6] Test against a rect that overlaps the top edge of rect1";
  rect2.MoveByY(1);

  // Test against a rect that's outside of rect1 on the top
  rect2.MoveByY(-rect2.Height());
  EXPECT_FALSE(rect1.Intersects(rect2))
      << "[7] Test against a rect that's outside of rect1 on the top";
  rect2.MoveByY(rect2.Height());

  // Test against a rect that overlaps the right edge of rect1
  rect2.MoveByX(1);
  EXPECT_FALSE(!rect1.Intersects(rect2))
      << "[8] Test against a rect that overlaps the right edge of rect1";
  rect2.MoveByX(-1);

  // Test against a rect that's outside of rect1 on the right
  rect2.MoveByX(rect2.Width());
  EXPECT_FALSE(rect1.Intersects(rect2))
      << "[9] Test against a rect that's outside of rect1 on the right";
  rect2.MoveByX(-rect2.Width());

  // Test against a rect that overlaps the bottom edge of rect1
  rect2.MoveByY(1);
  EXPECT_FALSE(!rect1.Intersects(rect2))
      << "[10] Test against a rect that overlaps the bottom edge of rect1";
  rect2.MoveByY(-1);

  // Test against a rect that's outside of rect1 on the bottom
  rect2.MoveByY(rect2.Height());
  EXPECT_FALSE(rect1.Intersects(rect2))
      << "[11] Test against a rect that's outside of rect1 on the bottom";
  rect2.MoveByY(-rect2.Height());

  return true;
}

// Test the method that returns a boolean result and an intersection rect
template <class RectType>
static bool TestIntersection() {
  RectType rect1(10, 10, 50, 50);
  RectType rect2(rect1);
  RectType dest;

  // Test against a rect that's the same as rect1
  EXPECT_FALSE(!dest.IntersectRect(rect1, rect2) ||
               !(dest.IsEqualInterior(rect1)))
      << "[1] Test against a rect that's the same as rect1";

  // Test against a rect that's enclosed by rect1
  rect2.Inflate(-1, -1);
  EXPECT_FALSE(!dest.IntersectRect(rect1, rect2) ||
               !(dest.IsEqualInterior(rect2)))
      << "[2] Test against a rect that's enclosed by rect1";
  rect2.Inflate(1, 1);

  // Test against a rect that overlaps the left edge of rect1
  rect2.MoveByX(-1);
  EXPECT_FALSE(!dest.IntersectRect(rect1, rect2) ||
               !(dest.IsEqualInterior(RectType(
                   rect1.X(), rect1.Y(), rect1.Width() - 1, rect1.Height()))))
      << "[3] Test against a rect that overlaps the left edge of rect1";
  rect2.MoveByX(1);

  // Test against a rect that's outside of rect1 on the left
  rect2.MoveByX(-rect2.Width());
  EXPECT_FALSE(dest.IntersectRect(rect1, rect2))
      << "[4] Test against a rect that's outside of rect1 on the left";
  // Make sure an empty rect is returned
  EXPECT_TRUE(dest.IsEmpty() && dest.IsZeroArea())
      << "[4] Make sure an empty rect is returned";
  EXPECT_TRUE(dest.IsFinite()) << "[4b] Should be finite";
  rect2.MoveByX(rect2.Width());

  // Test against a rect that overlaps the top edge of rect1
  rect2.MoveByY(-1);
  EXPECT_FALSE(!dest.IntersectRect(rect1, rect2) ||
               !(dest.IsEqualInterior(RectType(
                   rect1.X(), rect1.Y(), rect1.Width(), rect1.Height() - 1))))
      << "[5] Test against a rect that overlaps the top edge of rect1";
  EXPECT_TRUE(dest.IsFinite()) << "[5b] Should be finite";
  rect2.MoveByY(1);

  // Test against a rect that's outside of rect1 on the top
  rect2.MoveByY(-rect2.Height());
  EXPECT_FALSE(dest.IntersectRect(rect1, rect2))
      << "[6] Test against a rect that's outside of rect1 on the top";
  // Make sure an empty rect is returned
  EXPECT_TRUE(dest.IsEmpty() && dest.IsZeroArea())
      << "[6] Make sure an empty rect is returned";
  EXPECT_TRUE(dest.IsFinite()) << "[6b] Should be finite";
  rect2.MoveByY(rect2.Height());

  // Test against a rect that overlaps the right edge of rect1
  rect2.MoveByX(1);
  EXPECT_FALSE(
      !dest.IntersectRect(rect1, rect2) ||
      !(dest.IsEqualInterior(RectType(rect1.X() + 1, rect1.Y(),
                                      rect1.Width() - 1, rect1.Height()))))
      << "[7] Test against a rect that overlaps the right edge of rect1";
  rect2.MoveByX(-1);

  // Test against a rect that's outside of rect1 on the right
  rect2.MoveByX(rect2.Width());
  EXPECT_FALSE(dest.IntersectRect(rect1, rect2))
      << "[8] Test against a rect that's outside of rect1 on the right";
  // Make sure an empty rect is returned
  EXPECT_TRUE(dest.IsEmpty() && dest.IsZeroArea())
      << "[8] Make sure an empty rect is returned";
  EXPECT_TRUE(dest.IsFinite()) << "[8b] Should be finite";
  rect2.MoveByX(-rect2.Width());

  // Test against a rect that overlaps the bottom edge of rect1
  rect2.MoveByY(1);
  EXPECT_FALSE(
      !dest.IntersectRect(rect1, rect2) ||
      !(dest.IsEqualInterior(RectType(rect1.X(), rect1.Y() + 1, rect1.Width(),
                                      rect1.Height() - 1))))
      << "[9] Test against a rect that overlaps the bottom edge of rect1";
  EXPECT_TRUE(dest.IsFinite()) << "[9b] Should be finite";
  rect2.MoveByY(-1);

  // Test against a rect that's outside of rect1 on the bottom
  rect2.MoveByY(rect2.Height());
  EXPECT_FALSE(dest.IntersectRect(rect1, rect2))
      << "[10] Test against a rect that's outside of rect1 on the bottom";
  // Make sure an empty rect is returned
  EXPECT_TRUE(dest.IsEmpty() && dest.IsZeroArea())
      << "[10] Make sure an empty rect is returned";
  EXPECT_TRUE(dest.IsFinite()) << "[10b] Should be finite";
  rect2.MoveByY(-rect2.Height());

  // Test against a rect with zero width or height
  rect1.SetRect(100, 100, 100, 100);
  rect2.SetRect(150, 100, 0, 100);
  EXPECT_TRUE(!dest.IntersectRect(rect1, rect2) && dest.IsEmpty() &&
              dest.IsZeroArea())
      << "[11] Intersection of rects with zero width or height should be empty";
  EXPECT_TRUE(dest.IsFinite()) << "[11b] Should be finite";

  // Tests against a rect with negative width or height
  //

  // Test against a rect with negative width
  rect1.SetRect(100, 100, 100, 100);
  rect2.SetRect(100, 100, -100, 100);
  EXPECT_TRUE(!dest.IntersectRect(rect1, rect2) && dest.IsEmpty() &&
              dest.IsZeroArea())
      << "[12] Intersection of rects with negative width or height should be "
         "empty";
  EXPECT_TRUE(dest.IsFinite()) << "[12b] Should be finite";

  // Those two rects exactly overlap in some way...
  // but we still want to return an empty rect
  rect1.SetRect(100, 100, 100, 100);
  rect2.SetRect(200, 200, -100, -100);
  EXPECT_TRUE(!dest.IntersectRect(rect1, rect2) && dest.IsEmpty() &&
              dest.IsZeroArea())
      << "[13] Intersection of rects with negative width or height should be "
         "empty";
  EXPECT_TRUE(dest.IsFinite()) << "[13b] Should be finite";

  // Test against two identical rects with negative height
  rect1.SetRect(100, 100, 100, -100);
  rect2.SetRect(100, 100, 100, -100);
  EXPECT_TRUE(!dest.IntersectRect(rect1, rect2) && dest.IsEmpty() &&
              dest.IsZeroArea())
      << "[14] Intersection of rects with negative width or height should be "
         "empty";
  EXPECT_TRUE(dest.IsFinite()) << "[14b] Should be finite";

  return true;
}

template <class RectType>
static bool TestUnion() {
  RectType rect1;
  RectType rect2(10, 10, 50, 50);
  RectType dest;

  // Check the case where the receiver is an empty rect
  rect1.SetEmpty();
  dest.UnionRect(rect1, rect2);
  EXPECT_TRUE(!dest.IsEmpty() && !dest.IsZeroArea() &&
              dest.IsEqualInterior(rect2))
      << "[1] Check the case where the receiver is an empty rect";
  EXPECT_TRUE(dest.IsFinite()) << "[1b] Should be finite";

  // Check the case where the source rect is an empty rect
  rect1 = rect2;
  rect2.SetEmpty();
  dest.UnionRect(rect1, rect2);
  EXPECT_TRUE(!dest.IsEmpty() && !dest.IsZeroArea() &&
              dest.IsEqualInterior(rect1))
      << "[2] Check the case where the source rect is an empty rect";
  EXPECT_TRUE(dest.IsFinite()) << "[2b] Should be finite";

  // Test the case where both rects are empty
  rect1.SetEmpty();
  rect2.SetEmpty();
  dest.UnionRect(rect1, rect2);
  EXPECT_TRUE(dest.IsEmpty() && dest.IsZeroArea())
      << "[3] Test the case where both rects are empty";
  EXPECT_TRUE(dest.IsFinite()) << "[3b] Should be finite";

  // Test union case where the two rects don't overlap at all
  rect1.SetRect(10, 10, 50, 50);
  rect2.SetRect(100, 100, 50, 50);
  dest.UnionRect(rect1, rect2);
  EXPECT_TRUE(!dest.IsEmpty() && !dest.IsZeroArea() &&
              (dest.IsEqualInterior(RectType(rect1.X(), rect1.Y(),
                                             rect2.XMost() - rect1.X(),
                                             rect2.YMost() - rect1.Y()))))
      << "[4] Test union case where the two rects don't overlap at all";
  EXPECT_TRUE(dest.IsFinite()) << "[4b] Should be finite";

  // Test union case where the two rects overlap
  rect1.SetRect(30, 30, 50, 50);
  rect2.SetRect(10, 10, 50, 50);
  dest.UnionRect(rect1, rect2);
  EXPECT_TRUE(!dest.IsEmpty() && !dest.IsZeroArea() &&
              (dest.IsEqualInterior(RectType(rect2.X(), rect2.Y(),
                                             rect1.XMost() - rect2.X(),
                                             rect1.YMost() - rect2.Y()))))
      << "[5] Test union case where the two rects overlap";
  EXPECT_TRUE(dest.IsFinite()) << "[5b] Should be finite";

  return true;
}

template <class RectType>
static void TestUnionEmptyRects() {
  RectType rect1(10, 10, 0, 50);
  RectType rect2(5, 5, 40, 0);
  EXPECT_TRUE(rect1.IsEmpty() && rect2.IsEmpty());

  RectType dest = rect1.Union(rect2);
  EXPECT_TRUE(dest.IsEmpty() && dest.IsEqualEdges(rect2))
      << "Test the case where both rects are empty, and the result is the "
         "same value passing into Union()";
}

static bool TestFiniteGfx() {
  float posInf = std::numeric_limits<float>::infinity();
  float negInf = -std::numeric_limits<float>::infinity();
  float justNaN = std::numeric_limits<float>::quiet_NaN();

  gfxFloat values[4] = {5.0, 10.0, 15.0, 20.0};

  // Try the "non-finite" values for x, y, width, height, one at a time
  for (int i = 0; i < 4; i += 1) {
    values[i] = posInf;
    gfxRect rectPosInf(values[0], values[1], values[2], values[3]);
    EXPECT_FALSE(rectPosInf.IsFinite())
        << "For +inf (" << values[0] << "," << values[1] << "," << values[2]
        << "," << values[3] << ")";

    values[i] = negInf;
    gfxRect rectNegInf(values[0], values[1], values[2], values[3]);
    EXPECT_FALSE(rectNegInf.IsFinite())
        << "For -inf (" << values[0] << "," << values[1] << "," << values[2]
        << "," << values[3] << ")";

    values[i] = justNaN;
    gfxRect rectNaN(values[0], values[1], values[2], values[3]);
    EXPECT_FALSE(rectNaN.IsFinite())
        << "For NaN (" << values[0] << "," << values[1] << "," << values[2]
        << "," << values[3] << ")";

    // Reset to a finite value...
    values[i] = 5.0 * i;
  }

  return true;
}

// We want to test nsRect values that are still in range but where
// the implementation is at risk of overflowing
template <class RectType>
static bool TestBug1135677() {
  RectType rect1(1073741344, 1073741344, 1073756696, 1073819936);
  RectType rect2(1073741820, 1073741820, 14400, 77640);
  RectType dest;

  dest = rect1.Intersect(rect2);

  EXPECT_TRUE(dest.IsEqualRect(1073741820, 1073741820, 14400, 77640))
      << "[1] Operation should not overflow internally.";

  return true;
}

template <class RectType>
static bool TestSetWH() {
  RectType rect(1, 2, 3, 4);
  EXPECT_TRUE(rect.IsEqualRect(1, 2, 3, 4));
  rect.SetWidth(13);
  EXPECT_TRUE(rect.IsEqualRect(1, 2, 13, 4));
  rect.SetHeight(14);
  EXPECT_TRUE(rect.IsEqualRect(1, 2, 13, 14));
  rect.SizeTo(23, 24);
  EXPECT_TRUE(rect.IsEqualRect(1, 2, 23, 24));
  return true;
}

template <class RectType>
static bool TestSwap() {
  RectType rect(1, 2, 3, 4);
  EXPECT_TRUE(rect.IsEqualRect(1, 2, 3, 4));
  rect.Swap();
  EXPECT_TRUE(rect.IsEqualRect(2, 1, 4, 3));
  return true;
}

static void TestIntersectionLogicalHelper(nscoord x1, nscoord y1, nscoord w1,
                                          nscoord h1, nscoord x2, nscoord y2,
                                          nscoord w2, nscoord h2, nscoord xR,
                                          nscoord yR, nscoord wR, nscoord hR,
                                          bool isNonEmpty) {
  nsRect rect1(x1, y1, w1, h1);
  nsRect rect2(x2, y2, w2, h2);
  nsRect rectDebug;
  EXPECT_TRUE(isNonEmpty == rectDebug.IntersectRect(rect1, rect2));
  EXPECT_TRUE(rectDebug.IsEqualEdges(nsRect(xR, yR, wR, hR)));

  mozilla::LogicalRect r1(mozilla::WritingMode(), rect1.X(), rect1.Y(),
                          rect1.Width(), rect1.Height());
  mozilla::LogicalRect r2(mozilla::WritingMode(), rect2.X(), rect2.Y(),
                          rect2.Width(), rect2.Height());
  EXPECT_TRUE(isNonEmpty == r1.IntersectRect(r1, r2));
  EXPECT_TRUE(rectDebug.IsEqualEdges(nsRect(
      r1.IStart(mozilla::WritingMode()), r1.BStart(mozilla::WritingMode()),
      r1.ISize(mozilla::WritingMode()), r1.BSize(mozilla::WritingMode()))));

  mozilla::LogicalRect r3(mozilla::WritingMode(), rect1.X(), rect1.Y(),
                          rect1.Width(), rect1.Height());
  mozilla::LogicalRect r4(mozilla::WritingMode(), rect2.X(), rect2.Y(),
                          rect2.Width(), rect2.Height());
  EXPECT_TRUE(isNonEmpty == r4.IntersectRect(r3, r4));
  EXPECT_TRUE(rectDebug.IsEqualEdges(nsRect(
      r4.IStart(mozilla::WritingMode()), r4.BStart(mozilla::WritingMode()),
      r4.ISize(mozilla::WritingMode()), r4.BSize(mozilla::WritingMode()))));

  mozilla::LogicalRect r5(mozilla::WritingMode(), rect1.X(), rect1.Y(),
                          rect1.Width(), rect1.Height());
  mozilla::LogicalRect r6(mozilla::WritingMode(), rect2.X(), rect2.Y(),
                          rect2.Width(), rect2.Height());
  mozilla::LogicalRect r7(mozilla::WritingMode(), 0, 0, 1, 1);
  EXPECT_TRUE(isNonEmpty == r7.IntersectRect(r5, r6));
  EXPECT_TRUE(rectDebug.IsEqualEdges(nsRect(
      r7.IStart(mozilla::WritingMode()), r7.BStart(mozilla::WritingMode()),
      r7.ISize(mozilla::WritingMode()), r7.BSize(mozilla::WritingMode()))));
}

static void TestIntersectionLogical(nscoord x1, nscoord y1, nscoord w1,
                                    nscoord h1, nscoord x2, nscoord y2,
                                    nscoord w2, nscoord h2, nscoord xR,
                                    nscoord yR, nscoord wR, nscoord hR,
                                    bool isNonEmpty) {
  TestIntersectionLogicalHelper(x1, y1, w1, h1, x2, y2, w2, h2, xR, yR, wR, hR,
                                isNonEmpty);
  TestIntersectionLogicalHelper(x2, y2, w2, h2, x1, y1, w1, h1, xR, yR, wR, hR,
                                isNonEmpty);
}

TEST(Gfx, Logical)
{
  TestIntersectionLogical(578, 0, 2650, 1152, 1036, 0, 2312, 1, 1036, 0, 2192,
                          1, true);
  TestIntersectionLogical(0, 0, 1000, 1000, 500, 500, 1000, 1000, 500, 500, 500,
                          500, true);
  TestIntersectionLogical(100, 200, 300, 400, 50, 250, 100, 100, 100, 250, 50,
                          100, true);
  TestIntersectionLogical(0, 100, 200, 300, 300, 100, 100, 300, 300, 100, 0, 0,
                          false);
}

TEST(Gfx, nsRect)
{
  TestRectConstructors<nsRect>();
  TestEqualityOperator<nsRect>();
  TestContainment<nsRect, nscoord>();
  TestIntersects<nsRect>();
  TestIntersection<nsRect>();
  TestUnion<nsRect>();
  TestUnionEmptyRects<nsRect>();
  TestBug1135677<nsRect>();
  TestSetWH<nsRect>();
  TestSwap<nsRect>();
}

TEST(Gfx, nsIntRect)
{
  TestRectConstructors<nsIntRect>();
  TestEqualityOperator<nsIntRect>();
  TestContainment<nsIntRect, int32_t>();
  TestIntersects<nsIntRect>();
  TestIntersection<nsIntRect>();
  TestUnion<nsIntRect>();
  TestUnionEmptyRects<nsIntRect>();
  TestBug1135677<nsIntRect>();
  TestSetWH<nsIntRect>();
  TestSwap<nsIntRect>();
}

TEST(Gfx, gfxRect)
{
  TestRectConstructors<gfxRect>();
  // Skip TestEqualityOperator<gfxRect>(); as gfxRect::operator== is private
  TestContainment<gfxRect, double>();
  TestIntersects<gfxRect>();
  TestIntersection<gfxRect>();
  TestUnion<gfxRect>();
  TestUnionEmptyRects<gfxRect>();
  TestBug1135677<gfxRect>();
  TestFiniteGfx();
  TestSetWH<gfxRect>();
  TestSwap<gfxRect>();
}

TEST(Gfx, nsRectAbsolute)
{
  TestUnionEmptyRects<nsRectAbsolute>();
}

TEST(Gfx, IntRectAbsolute)
{
  TestUnionEmptyRects<IntRectAbsolute>();
}

static void TestMoveInsideAndClamp(IntRect aSrc, IntRect aTarget,
                                   IntRect aExpected) {
  // Test the implementation in BaseRect (x/y/width/height representation)
  IntRect result = aSrc.MoveInsideAndClamp(aTarget);
  EXPECT_TRUE(result.IsEqualEdges(aExpected))
      << "Source " << aSrc << " Target " << aTarget << " Expected " << aExpected
      << " Actual " << result;

  // Also test the implementation in RectAbsolute (left/top/right/bottom
  // representation)
  IntRectAbsolute absSrc = IntRectAbsolute::FromRect(aSrc);
  IntRectAbsolute absTarget = IntRectAbsolute::FromRect(aTarget);
  IntRectAbsolute absExpected = IntRectAbsolute::FromRect(aExpected);

  IntRectAbsolute absResult = absSrc.MoveInsideAndClamp(absTarget);
  EXPECT_TRUE(absResult.IsEqualEdges(absExpected))
      << "AbsSource " << absSrc << " AbsTarget " << absTarget << " AbsExpected "
      << absExpected << " AbsActual " << absResult;
}

TEST(Gfx, MoveInsideAndClamp)
{
  TestMoveInsideAndClamp(IntRect(0, 0, 10, 10), IntRect(1, -1, 10, 10),
                         IntRect(1, -1, 10, 10));
  TestMoveInsideAndClamp(IntRect(0, 0, 10, 10), IntRect(-1, -1, 12, 5),
                         IntRect(0, -1, 10, 5));
  TestMoveInsideAndClamp(IntRect(0, 0, 10, 10), IntRect(10, 11, 10, 0),
                         IntRect(10, 11, 10, 0));
  TestMoveInsideAndClamp(IntRect(0, 0, 10, 10), IntRect(-10, -1, 10, 0),
                         IntRect(-10, -1, 10, 0));
  TestMoveInsideAndClamp(IntRect(0, 0, 0, 0), IntRect(10, -10, 10, 10),
                         IntRect(10, 0, 0, 0));
}

TEST(Gfx, ClampPoint)
{
  /* Various bounds */
  IntRect Square(0, 0, 10, 10);
  EXPECT_EQ(Square.ClampPoint(IntPoint(-5, -5)), IntPoint(0, 0));
  EXPECT_EQ(Square.ClampPoint(IntPoint(-5, 5)), IntPoint(0, 5));
  EXPECT_EQ(Square.ClampPoint(IntPoint(-5, 15)), IntPoint(0, 10));

  EXPECT_EQ(Square.ClampPoint(IntPoint(5, -5)), IntPoint(5, 0));
  EXPECT_EQ(Square.ClampPoint(IntPoint(5, 5)), IntPoint(5, 5));
  EXPECT_EQ(Square.ClampPoint(IntPoint(5, 15)), IntPoint(5, 10));

  EXPECT_EQ(Square.ClampPoint(IntPoint(15, -5)), IntPoint(10, 0));
  EXPECT_EQ(Square.ClampPoint(IntPoint(15, 5)), IntPoint(10, 5));
  EXPECT_EQ(Square.ClampPoint(IntPoint(15, 15)), IntPoint(10, 10));

  /* Special case of empty rect */
  IntRect Empty(0, 0, -1, -1);
  EXPECT_TRUE(Empty.IsEmpty());
  EXPECT_EQ(Empty.ClampPoint(IntPoint(-1, -1)), IntPoint(0, 0));
  EXPECT_EQ(Empty.ClampPoint(IntPoint(-1, 1)), IntPoint(0, 0));
  EXPECT_EQ(Empty.ClampPoint(IntPoint(1, -1)), IntPoint(0, 0));
  EXPECT_EQ(Empty.ClampPoint(IntPoint(1, 1)), IntPoint(0, 0));
}

TEST(Gfx, SafeMoveBy)
{
  IntRect intRect(0, 0, 10, 10);
  intRect.SafeMoveByX(10);
  intRect.SafeMoveByY(10);
  EXPECT_EQ(intRect, IntRect(10, 10, 10, 10));

  intRect = IntRect(0, 0, 10, 10);
  intRect.SafeMoveByX(-10);
  intRect.SafeMoveByY(-10);
  EXPECT_EQ(intRect, IntRect(-10, -10, 10, 10));

  Rect rect(0, 0, 10, 10);
  rect.SafeMoveByX(10);
  rect.SafeMoveByY(10);
  EXPECT_EQ(rect, Rect(10, 10, 10, 10));

  rect = Rect(0, 0, 10, 10);
  rect.SafeMoveByX(-10);
  rect.SafeMoveByY(-10);
  EXPECT_EQ(rect, Rect(-10, -10, 10, 10));
}
