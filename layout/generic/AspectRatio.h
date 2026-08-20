/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_AspectRatio_h
#define mozilla_AspectRatio_h

/* The aspect ratio of a box, in a "width / height" format. */

#include <algorithm>
#include <limits>

#include "mozilla/gfx/BaseSize.h"
#include "nsCoord.h"

namespace IPC {
template <typename T>
struct ParamTraits;
}  // namespace IPC

namespace mozilla {

enum class LogicalAxis : uint8_t;
class LogicalSize;
class WritingMode;

enum class UseBoxSizing : uint8_t {
  // The aspect ratio works with content box dimensions always.
  No,
  // The aspect ratio works with the dimensions of the box specified by
  // box-sizing.
  Yes,
};

struct AspectRatio {
  friend struct IPC::ParamTraits<mozilla::AspectRatio>;

  AspectRatio() = default;
  explicit AspectRatio(float aRatio,
                       UseBoxSizing aUseBoxSizing = UseBoxSizing::No)
      : mRatio(std::max(aRatio, 0.0f)), mUseBoxSizing(aUseBoxSizing) {}

  static AspectRatio FromSize(float aWidth, float aHeight,
                              UseBoxSizing aUseBoxSizing = UseBoxSizing::No) {
    if (aWidth == 0.0f || aHeight == 0.0f) {
      // For the degenerate ratio, we don't care about which box sizing we are
      // using, so using default constructor is fine.
      return AspectRatio();
    }
    float ratio = aWidth / aHeight;
    if (!std::isfinite(ratio)) [[unlikely]] {
      return AspectRatio();
    }
    return AspectRatio(ratio, aUseBoxSizing);
  }

  template <typename T, typename Sub, typename Coord>
  static AspectRatio FromSize(const gfx::BaseSize<T, Sub, Coord>& aSize) {
    return FromSize(aSize.Width(), aSize.Height());
  }

  explicit operator bool() const { return mRatio != 0.0f; }

  nscoord ApplyTo(nscoord aCoord) const {
    MOZ_DIAGNOSTIC_ASSERT(*this);
    return NSCoordSaturatingNonnegativeMultiply(aCoord, mRatio);
  }

  float ApplyToFloat(float aFloat) const {
    MOZ_DIAGNOSTIC_ASSERT(*this);
    return mRatio * aFloat;
  }

  // Inverts the ratio, in order to get the height / width ratio.
  [[nodiscard]] AspectRatio Inverted() const {
    if (!*this) {
      return AspectRatio();
    }
    // Clamp to a small epsilon, in case mRatio is absurdly large & produces
    // 0.0f in the division here (so that valid ratios always generate other
    // valid ratios when inverted).
    return AspectRatio(
        std::max(std::numeric_limits<float>::epsilon(), 1.0f / mRatio),
        mUseBoxSizing);
  }

  [[nodiscard]] inline AspectRatio ConvertToWritingMode(
      const WritingMode& aWM) const;

  /**
   * This method computes the ratio-dependent size by the ratio-determining size
   * and aspect-ratio (i.e. preferred aspect ratio). Basically this function
   * will be used in the calculation of 'auto' sizes when the preferred
   * aspect ratio is not 'auto'.
   *
   * @param aRatioDependentAxis  The ratio depenedent axis of the box.
   * @param aWM  The writing mode of the box.
   * @param aRatioDetermingSize  The content-box size on the ratio determining
   *                             axis. Basically, we use this size and |mRatio|
   *                             to compute the size on the ratio-dependent
   *                             axis.
   * @param aContentBoxSizeToBoxSizingAdjust  The border padding box size
   *                                          adjustment. We need this because
   *                                          aspect-ratio should take the
   *                                          box-sizing into account if its
   *                                          style is '<ratio>'. If its style
   *                                          is 'auto & <ratio>', we should use
   *                                          content-box dimensions always.
   *                                          If the callers want the ratio to
   *                                          apply to the content-box size, we
   *                                          should pass a zero LogicalSize.
   *                                          If mUseBoxSizing is No, we ignore
   *                                          this parameter because we should
   *                                          use content box dimensions always.
   *
   * The return value is the content-box size on the ratio-dependent axis.
   * Plese see the definition of the ratio-dependent axis and the
   * ratio-determining axis in the spec:
   * https://drafts.csswg.org/css-sizing-4/#aspect-ratio
   */
  [[nodiscard]] nscoord ComputeRatioDependentSize(
      LogicalAxis aRatioDependentAxis, const WritingMode& aWM,
      nscoord aRatioDeterminingSize,
      const LogicalSize& aContentBoxSizeToBoxSizingAdjust) const;

  bool operator==(const AspectRatio&) const = default;

  bool operator<(const AspectRatio& aOther) const {
    MOZ_ASSERT(
        mUseBoxSizing == aOther.mUseBoxSizing,
        "Do not compare AspectRatio if their mUseBoxSizing are different.");
    return mRatio < aOther.mRatio;
  }

 private:
  // 0.0f represents no aspect ratio.
  float mRatio = 0.0f;
  UseBoxSizing mUseBoxSizing = UseBoxSizing::No;
};

}  // namespace mozilla

#endif  // mozilla_AspectRatio_h
