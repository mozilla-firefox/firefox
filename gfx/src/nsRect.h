/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef NSRECT_H
#define NSRECT_H

#include <stdint.h>          // for int32_t, int64_t
#include <algorithm>         // for min/max
#include "mozilla/Likely.h"  // for MOZ_UNLIKELY
#include "mozilla/gfx/BaseRect.h"
#include "mozilla/gfx/Rect.h"
#include "nsCoord.h"      // for nscoord, etc
#include "nsISupports.h"  // for MOZ_COUNT_CTOR, etc
#include "nsPoint.h"      // for nsIntPoint, nsPoint
#include "nsSize.h"       // for IntSize, nsSize

#if !defined(ANDROID) && (defined(__SSE2__) || defined(_M_X64) || \
                          (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#  include "mozilla/SSE.h"
#  if defined(_MSC_VER) && !defined(__clang__)
#    include "smmintrin.h"
#  else
#    include "emmintrin.h"
#  endif

#endif

struct nsMargin;

typedef mozilla::gfx::IntRect nsIntRect;

struct nsRect : public mozilla::gfx::BaseRect<nscoord, nsRect, nsPoint, nsSize,
                                              nsMargin> {
  typedef mozilla::gfx::BaseRect<nscoord, nsRect, nsPoint, nsSize, nsMargin>
      Super;

  // Constructors
  nsRect() { MOZ_COUNT_CTOR(nsRect); }
  nsRect(const nsRect& aRect) : Super(aRect) { MOZ_COUNT_CTOR(nsRect); }
  nsRect(const nsPoint& aOrigin, const nsSize& aSize) : Super(aOrigin, aSize) {
    MOZ_COUNT_CTOR(nsRect);
  }
  nsRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight)
      : Super(aX, aY, aWidth, aHeight) {
    MOZ_COUNT_CTOR(nsRect);
  }
  nsRect& operator=(const nsRect&) = default;

  MOZ_COUNTED_DTOR(nsRect)

  // We have saturating versions of all the Union methods. These avoid
  // overflowing nscoord values in the 'width' and 'height' fields by
  // clamping the width and height values to nscoord_MAX if necessary.

  // Returns the smallest rectangle that contains both the area of both
  // this and aRect. Thus, empty input rectangles are ignored.
  // Note: if both rectangles are empty, returns aRect.
  [[nodiscard]] nsRect SaturatingUnion(const nsRect& aRect) const {
    if (IsEmpty()) {
      return aRect;
    } else if (aRect.IsEmpty()) {
      return *static_cast<const nsRect*>(this);
    } else {
      return SaturatingUnionEdges(aRect);
    }
  }

  [[nodiscard]] nsRect SaturatingUnionEdges(const nsRect& aRect) const {
    nscoord resultX = std::min(aRect.X(), x);
    int64_t w =
        std::max(int64_t(aRect.X()) + aRect.Width(), int64_t(x) + width) -
        resultX;
    if (MOZ_UNLIKELY(w > nscoord_MAX)) {
      // Clamp huge negative x to nscoord_MIN / 2 and try again.
      resultX = std::max(resultX, nscoord_MIN / 2);
      w = std::max(int64_t(aRect.X()) + aRect.Width(), int64_t(x) + width) -
          resultX;
      if (MOZ_UNLIKELY(w > nscoord_MAX)) {
        w = nscoord_MAX;
      }
    }

    nscoord resultY = std::min(aRect.y, y);
    int64_t h =
        std::max(int64_t(aRect.Y()) + aRect.Height(), int64_t(y) + height) -
        resultY;
    if (MOZ_UNLIKELY(h > nscoord_MAX)) {
      // Clamp huge negative y to nscoord_MIN / 2 and try again.
      resultY = std::max(resultY, nscoord_MIN / 2);
      h = std::max(int64_t(aRect.Y()) + aRect.Height(), int64_t(y) + height) -
          resultY;
      if (MOZ_UNLIKELY(h > nscoord_MAX)) {
        h = nscoord_MAX;
      }
    }
    return nsRect(resultX, resultY, nscoord(w), nscoord(h));
  }

  // Make all nsRect Union methods be saturating.
  [[nodiscard]] nsRect UnionEdges(const nsRect& aRect) const {
    return SaturatingUnionEdges(aRect);
  }
  [[nodiscard]] nsRect Union(const nsRect& aRect) const {
    return SaturatingUnion(aRect);
  }
  [[nodiscard]] nsRect UnsafeUnion(const nsRect& aRect) const {
    return Super::Union(aRect);
  }
  void UnionRect(const nsRect& aRect1, const nsRect& aRect2) {
    *this = aRect1.Union(aRect2);
  }

  [[nodiscard]] nsRect Intersect(const nsRect& aRect) const;

  bool IntersectRect(const nsRect& aRect1, const nsRect& aRect2);

  // Return whether this rect's right or bottom edge overflow int32.
  bool Overflows() const;

  /**
   * Return this rect scaled to a different appunits per pixel (APP) ratio.
   * In the RoundOut version we make the rect the smallest rect containing the
   * unrounded result. In the RoundIn version we make the rect the largest rect
   * contained in the unrounded result.
   * @param aFromAPP the APP to scale from
   * @param aToAPP the APP to scale to
   * @note this can turn an empty rectangle into a non-empty rectangle
   */
  [[nodiscard]] inline nsRect ScaleToOtherAppUnitsRoundOut(
      int32_t aFromAPP, int32_t aToAPP) const;
  [[nodiscard]] inline nsRect ScaleToOtherAppUnitsRoundIn(int32_t aFromAPP,
                                                          int32_t aToAPP) const;

  [[nodiscard]] inline mozilla::gfx::IntRect ScaleToNearestPixels(
      float aXScale, float aYScale, nscoord aAppUnitsPerPixel) const;

  [[nodiscard]] inline mozilla::gfx::IntRect ToNearestPixels(
      nscoord aAppUnitsPerPixel) const;

  // Note: this can turn an empty rectangle into a non-empty rectangle
  [[nodiscard]] inline mozilla::gfx::IntRect ScaleToOutsidePixels(
      float aXScale, float aYScale, nscoord aAppUnitsPerPixel) const;

  // Note: this can turn an empty rectangle into a non-empty rectangle
  [[nodiscard]] inline mozilla::gfx::IntRect ToOutsidePixels(
      nscoord aAppUnitsPerPixel) const;

  [[nodiscard]] inline mozilla::gfx::IntRect ScaleToInsidePixels(
      float aXScale, float aYScale, nscoord aAppUnitsPerPixel) const;

  [[nodiscard]] inline mozilla::gfx::IntRect ToInsidePixels(
      nscoord aAppUnitsPerPixel) const;

  // This is here only to keep IPDL-generated code happy. DO NOT USE.
  bool operator==(const nsRect& aRect) const { return IsEqualEdges(aRect); }

  [[nodiscard]] inline nsRect RemoveResolution(const float aResolution) const;

  [[nodiscard]] mozilla::Maybe<nsRect> EdgeInclusiveIntersection(
      const nsRect& aOther) const {
    nscoord left = std::max(x, aOther.x);
    nscoord top = std::max(y, aOther.y);
    nscoord right = std::min(XMost(), aOther.XMost());
    nscoord bottom = std::min(YMost(), aOther.YMost());
    if (left > right || top > bottom) {
      return mozilla::Nothing();
    }
    return mozilla::Some(nsRect(left, top, right - left, bottom - top));
  }
};

/*
 * App Unit/Pixel conversions
 */

inline nsRect nsRect::ScaleToOtherAppUnitsRoundOut(int32_t aFromAPP,
                                                   int32_t aToAPP) const {
  if (aFromAPP == aToAPP) {
    return *this;
  }

  nsRect rect;
  rect.SetBox(NSToCoordFloor(NSCoordScale(x, aFromAPP, aToAPP)),
              NSToCoordFloor(NSCoordScale(y, aFromAPP, aToAPP)),
              NSToCoordCeil(NSCoordScale(XMost(), aFromAPP, aToAPP)),
              NSToCoordCeil(NSCoordScale(YMost(), aFromAPP, aToAPP)));
  return rect;
}

inline nsRect nsRect::ScaleToOtherAppUnitsRoundIn(int32_t aFromAPP,
                                                  int32_t aToAPP) const {
  if (aFromAPP == aToAPP) {
    return *this;
  }

  nsRect rect;
  rect.SetBox(NSToCoordCeil(NSCoordScale(x, aFromAPP, aToAPP)),
              NSToCoordCeil(NSCoordScale(y, aFromAPP, aToAPP)),
              NSToCoordFloor(NSCoordScale(XMost(), aFromAPP, aToAPP)),
              NSToCoordFloor(NSCoordScale(YMost(), aFromAPP, aToAPP)));
  return rect;
}

#if !defined(ANDROID) && (defined(__SSE2__) || defined(_M_X64) || \
                          (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
// Life would be so much better if we had SSE4 here.
static MOZ_ALWAYS_INLINE __m128i floor_ps2epi32(__m128 x) {
  __m128 one = _mm_set_ps(1.0f, 1.0f, 1.0f, 1.0f);

  __m128 t = _mm_cvtepi32_ps(_mm_cvttps_epi32(x));
  __m128 r = _mm_sub_ps(t, _mm_and_ps(_mm_cmplt_ps(x, t), one));

  return _mm_cvttps_epi32(r);
}

static MOZ_ALWAYS_INLINE __m128i ceil_ps2epi32(__m128 x) {
  __m128 t = _mm_sub_ps(_mm_setzero_ps(), x);
  __m128i r = _mm_sub_epi32(_mm_setzero_si128(), floor_ps2epi32(t));

  return r;
}
#endif

// scale the rect but round to preserve centers
inline mozilla::gfx::IntRect nsRect::ScaleToNearestPixels(
    float aXScale, float aYScale, nscoord aAppUnitsPerPixel) const {
  mozilla::gfx::IntRect rect;
  // Android x86 builds have bindgen issues.
#if !defined(ANDROID) && (defined(__SSE2__) || defined(_M_X64) || \
                          (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
  __m128 appUnitsPacked = _mm_set_ps(aAppUnitsPerPixel, aAppUnitsPerPixel,
                                     aAppUnitsPerPixel, aAppUnitsPerPixel);
  __m128 scalesPacked = _mm_set_ps(aYScale, aXScale, aYScale, aXScale);
  __m128 biasesPacked = _mm_set_ps(0.5f, 0.5f, 0.5f, 0.5f);

  __m128i rectPacked = _mm_loadu_si128((__m128i*)this);
  __m128i topLeft = _mm_slli_si128(rectPacked, 8);

  rectPacked = _mm_add_epi32(rectPacked, topLeft);  // X, Y, XMost(), YMost()

  __m128 rectFloat = _mm_cvtepi32_ps(rectPacked);

  // Scale, i.e. ([ x y xmost ymost ] / aAppUnitsPerPixel) * [ aXScale aYScale
  // aXScale aYScale ]
  rectFloat = _mm_mul_ps(_mm_div_ps(rectFloat, appUnitsPacked), scalesPacked);

  // Floor
  // Executed with bias and roundmode down, since round-nearest rounds 0.5
  // downward half the time.
  rectFloat = _mm_add_ps(rectFloat, biasesPacked);
  rectPacked = floor_ps2epi32(rectFloat);

  topLeft = _mm_slli_si128(rectPacked, 8);
  rectPacked = _mm_sub_epi32(rectPacked, topLeft);  // X, Y, Width, Height

  // Avoid negative width/height due to overflow.
  __m128i mask = _mm_or_si128(_mm_cmpgt_epi32(rectPacked, _mm_setzero_si128()),
                              _mm_set_epi32(0, 0, 0xFFFFFFFF, 0xFFFFFFFF));
  // Mask will now contain [ 0xFFFFFFFF 0xFFFFFFFF (width <= 0 ? 0 : 0xFFFFFFFF)
  // (height <= 0 ? 0 : 0xFFFFFFFF) ]
  rectPacked = _mm_and_si128(rectPacked, mask);

  _mm_storeu_si128((__m128i*)&rect, rectPacked);
#else
  rect.SetNonEmptyBox(
      NSToIntRoundUp(NSAppUnitsToFloatPixels(x, aAppUnitsPerPixel) * aXScale),
      NSToIntRoundUp(NSAppUnitsToFloatPixels(y, aAppUnitsPerPixel) * aYScale),
      NSToIntRoundUp(NSAppUnitsToFloatPixels(XMost(), aAppUnitsPerPixel) *
                     aXScale),
      NSToIntRoundUp(NSAppUnitsToFloatPixels(YMost(), aAppUnitsPerPixel) *
                     aYScale));
#endif
  return rect;
}

// scale the rect but round to smallest containing rect
inline mozilla::gfx::IntRect nsRect::ScaleToOutsidePixels(
    float aXScale, float aYScale, nscoord aAppUnitsPerPixel) const {
  mozilla::gfx::IntRect rect;
  // Android x86 builds have bindgen issues.
#if !defined(ANDROID) && (defined(__SSE2__) || defined(_M_X64) || \
                          (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
  __m128 appUnitsPacked = _mm_set_ps(aAppUnitsPerPixel, aAppUnitsPerPixel,
                                     aAppUnitsPerPixel, aAppUnitsPerPixel);
  __m128 scalesPacked = _mm_set_ps(aYScale, aXScale, aYScale, aXScale);

  __m128i rectPacked = _mm_loadu_si128((__m128i*)this);  // x, y, w, h
  __m128i topLeft = _mm_slli_si128(rectPacked, 8);       // 0, 0, x, y

  rectPacked = _mm_add_epi32(rectPacked, topLeft);  // X, Y, XMost(), YMost()

  __m128 rectFloat = _mm_cvtepi32_ps(rectPacked);

  // Scale i.e. ([ x y xmost ymost ] / aAppUnitsPerPixel) *
  //             [ aXScale aYScale aXScale aYScale ]
  rectFloat = _mm_mul_ps(_mm_div_ps(rectFloat, appUnitsPacked), scalesPacked);
  rectPacked = ceil_ps2epi32(rectFloat);    // xx, xx, XMost(), YMost()
  __m128i tmp = floor_ps2epi32(rectFloat);  // x, y, xx, xx

  // _mm_move_sd is 1 cycle method of getting the blending we want.
  rectPacked = _mm_castpd_si128(
      _mm_move_sd(_mm_castsi128_pd(rectPacked),
                  _mm_castsi128_pd(tmp)));  // x, y, XMost(), YMost()

  topLeft = _mm_slli_si128(rectPacked, 8);          // 0, 0, r.x, r.y
  rectPacked = _mm_sub_epi32(rectPacked, topLeft);  // r.x, r.y, r.w, r.h

  // Avoid negative width/height due to overflow.
  __m128i mask = _mm_or_si128(_mm_cmpgt_epi32(rectPacked, _mm_setzero_si128()),
                              _mm_set_epi32(0, 0, 0xFFFFFFFF, 0xFFFFFFFF));
  // clang-format off
  // Mask will now contain [ 0xFFFFFFFF 0xFFFFFFFF (width <= 0 ? 0 : 0xFFFFFFFF) (height <= 0 ? 0 : 0xFFFFFFFF) ]
  // clang-format on
  rectPacked = _mm_and_si128(rectPacked, mask);

  _mm_storeu_si128((__m128i*)&rect, rectPacked);
#else
  rect.SetNonEmptyBox(
      NSToIntFloor(NSAppUnitsToFloatPixels(x, float(aAppUnitsPerPixel)) *
                   aXScale),
      NSToIntFloor(NSAppUnitsToFloatPixels(y, float(aAppUnitsPerPixel)) *
                   aYScale),
      NSToIntCeil(NSAppUnitsToFloatPixels(XMost(), float(aAppUnitsPerPixel)) *
                  aXScale),
      NSToIntCeil(NSAppUnitsToFloatPixels(YMost(), float(aAppUnitsPerPixel)) *
                  aYScale));
#endif
  return rect;
}

// scale the rect but round to largest contained rect
inline mozilla::gfx::IntRect nsRect::ScaleToInsidePixels(
    float aXScale, float aYScale, nscoord aAppUnitsPerPixel) const {
  mozilla::gfx::IntRect rect;
  rect.SetNonEmptyBox(
      NSToIntCeil(NSAppUnitsToFloatPixels(x, float(aAppUnitsPerPixel)) *
                  aXScale),
      NSToIntCeil(NSAppUnitsToFloatPixels(y, float(aAppUnitsPerPixel)) *
                  aYScale),
      NSToIntFloor(NSAppUnitsToFloatPixels(XMost(), float(aAppUnitsPerPixel)) *
                   aXScale),
      NSToIntFloor(NSAppUnitsToFloatPixels(YMost(), float(aAppUnitsPerPixel)) *
                   aYScale));
  return rect;
}

inline mozilla::gfx::IntRect nsRect::ToNearestPixels(
    nscoord aAppUnitsPerPixel) const {
  return ScaleToNearestPixels(1.0f, 1.0f, aAppUnitsPerPixel);
}

inline mozilla::gfx::IntRect nsRect::ToOutsidePixels(
    nscoord aAppUnitsPerPixel) const {
  return ScaleToOutsidePixels(1.0f, 1.0f, aAppUnitsPerPixel);
}

inline mozilla::gfx::IntRect nsRect::ToInsidePixels(
    nscoord aAppUnitsPerPixel) const {
  return ScaleToInsidePixels(1.0f, 1.0f, aAppUnitsPerPixel);
}

inline nsRect nsRect::RemoveResolution(const float aResolution) const {
  MOZ_ASSERT(aResolution > 0.0f);
  nsRect rect;
  rect.MoveTo(NSToCoordRound(NSCoordToFloat(x) / aResolution),
              NSToCoordRound(NSCoordToFloat(y) / aResolution));
  // A 1x1 rect indicates we are just hit testing a point, so pass down a 1x1
  // rect as well instead of possibly rounding the width or height to zero.
  if (width == 1 && height == 1) {
    rect.SizeTo(1, 1);
  } else {
    rect.SizeTo(NSToCoordCeil(NSCoordToFloat(width) / aResolution),
                NSToCoordCeil(NSCoordToFloat(height) / aResolution));
  }

  return rect;
}

const mozilla::gfx::IntRect& GetMaxSizedIntRect();

// app units are integer multiples of pixels, so no rounding needed
template <class units>
nsRect ToAppUnits(const mozilla::gfx::IntRectTyped<units>& aRect,
                  nscoord aAppUnitsPerPixel) {
  return nsRect(NSIntPixelsToAppUnits(aRect.X(), aAppUnitsPerPixel),
                NSIntPixelsToAppUnits(aRect.Y(), aAppUnitsPerPixel),
                NSIntPixelsToAppUnits(aRect.Width(), aAppUnitsPerPixel),
                NSIntPixelsToAppUnits(aRect.Height(), aAppUnitsPerPixel));
}

struct nsRectCornerRadii final
    : public mozilla::gfx::BaseRectCornerRadii<nscoord, nsSize, nsMargin> {
  using BaseRectCornerRadii::BaseRectCornerRadii;
};

#endif /* NSRECT_H */
