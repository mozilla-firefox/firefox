/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* struct containing the output from nsIFrame::Reflow */

#ifndef mozilla_ReflowOutput_h
#define mozilla_ReflowOutput_h

#include "mozilla/EnumeratedRange.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/WritingModes.h"
#include "nsBoundingMetrics.h"
#include "nsRect.h"

//----------------------------------------------------------------------

namespace mozilla {
struct ReflowInput;

enum class OverflowType : uint8_t { Ink, Scrollable };
constexpr auto AllOverflowTypes() {
  return MakeInclusiveEnumeratedRange(OverflowType::Ink,
                                      OverflowType::Scrollable);
}

// Flags controlling how a child's overflow areas are unioned into a parent's
// overflow areas.
enum class OverflowAreaUnionFlags : uint8_t {
  None = 0,
  // Treat the frame as if it were a scroll container: bypass the contain:layout
  // check so the scrollable overflow of children still contributes.
  AsIfScrolled = 1 << 0,
  // The child being considered is absolutely positioned. The child's overflow
  // is unioned via OverflowAreas::UnionWithAbsoluteOverflowAreas (which skips
  // empty overflow rects).
  ChildIsAbsPos = 1 << 1,
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(OverflowAreaUnionFlags)

struct OverflowAreas {
 public:
  nsRect& InkOverflow() { return mInk; }
  const nsRect& InkOverflow() const { return mInk; }

  nsRect& ScrollableOverflow() { return mScrollable; }
  const nsRect& ScrollableOverflow() const { return mScrollable; }

  nsRect& Overflow(OverflowType aType) {
    return aType == OverflowType::Ink ? InkOverflow() : ScrollableOverflow();
  }
  const nsRect& Overflow(OverflowType aType) const {
    return aType == OverflowType::Ink ? InkOverflow() : ScrollableOverflow();
  }

  OverflowAreas() = default;

  OverflowAreas(const nsRect& aInkOverflow, const nsRect& aScrollableOverflow)
      : mInk(aInkOverflow), mScrollable(aScrollableOverflow) {}

  bool operator==(const OverflowAreas& aOther) const {
    // Scrollable overflow is a point-set rectangle and ink overflow
    // is a pixel-set rectangle.
    return InkOverflow().IsEqualInterior(aOther.InkOverflow()) &&
           ScrollableOverflow().IsEqualEdges(aOther.ScrollableOverflow());
  }

  OverflowAreas operator+(const nsPoint& aPoint) const {
    OverflowAreas result(*this);
    result += aPoint;
    return result;
  }

  OverflowAreas& operator+=(const nsPoint& aPoint) {
    mInk += aPoint;
    mScrollable += aPoint;
    return *this;
  }

  void Clear() { SetAllTo(nsRect()); }

  // Mutates |this| by unioning both overflow areas with |aOther|.
  void UnionWith(const OverflowAreas& aOther);

  // Mutates |this| by unioning both overflow areas with |aOther|, which is
  // assumed to be overflow areas of an absolutely positioned frame.
  void UnionWithAbsoluteOverflowAreas(const OverflowAreas& aOther);

  // Mutates |this| by unioning both overflow areas with |aRect|.
  void UnionAllWith(const nsRect& aRect);

  // Mutates |this| by setting both overflow areas to |aRect|.
  void SetAllTo(const nsRect& aRect);

  // Applies overflow clipping (for e.g. overflow: clip) as needed to both our
  // overflow rects.
  void ApplyClipping(const nsRect& aBounds, PhysicalAxes aClipAxes,
                     const nsMargin& aOverflowMargin) {
    ApplyOverflowClippingOnRect(InkOverflow(), aBounds, aClipAxes,
                                aOverflowMargin);
    ApplyOverflowClippingOnRect(ScrollableOverflow(), aBounds, aClipAxes,
                                aOverflowMargin);
  }

  // Gets the overflow clipping rect for a given element given a rect to clip,
  // the frame bounds, a set of axes, and the overflow margin.
  static nsRect GetOverflowClipRect(const nsRect& aRectToClip,
                                    const nsRect& aBounds,
                                    PhysicalAxes aClipAxes,
                                    const nsMargin& aOverflowMargin);

  // Applies the overflow clipping to a given overflow rect, given the frame
  // bounds, and the physical axes on which to apply the overflow clip.
  static void ApplyOverflowClippingOnRect(nsRect& aOverflowRect,
                                          const nsRect& aBounds,
                                          PhysicalAxes aClipAxes,
                                          const nsMargin& aOverflowMargin);

 private:
  nsRect mInk;
  nsRect mScrollable;
};

/**
 * CollapsingMargin represents a vertical collapsing margin between
 * blocks as described in section 8.3.1 of CSS2.
 * <https://www.w3.org/TR/CSS22/box.html#collapsing-margins>
 *
 * All adjacent vertical margins collapse, and the resulting margin is
 * the sum of the largest positive margin included and the smallest (most
 * negative) negative margin included.
 */
class CollapsingMargin final {
 public:
  bool operator==(const CollapsingMargin&) const = default;

  void Include(nscoord aCoord) {
    if (aCoord > mMostPos) {
      mMostPos = aCoord;
    } else if (aCoord < mMostNeg) {
      mMostNeg = aCoord;
    }
  }

  void Include(const CollapsingMargin& aOther) {
    if (aOther.mMostPos > mMostPos) {
      mMostPos = aOther.mMostPos;
    }
    if (aOther.mMostNeg < mMostNeg) {
      mMostNeg = aOther.mMostNeg;
    }
  }

  void Zero() {
    mMostPos = 0;
    mMostNeg = 0;
  }

  bool IsZero() const { return mMostPos == 0 && mMostNeg == 0; }

  nscoord Get() const { return mMostPos + mMostNeg; }

 private:
  // The largest positive margin included.
  nscoord mMostPos = 0;

  // The smallest negative margin included.
  nscoord mMostNeg = 0;
};

/**
 * ReflowOutput is initialized by a parent frame as a parameter passing to
 * Reflow() to allow a child frame to return its desired size and alignment
 * information.
 *
 * ReflowOutput's constructor usually takes a parent frame's WritingMode (or
 * ReflowInput) because it is more convenient for the parent frame to use the
 * stored Size() after reflowing the child frame. However, it can actually
 * accept any WritingMode (or ReflowInput) because SetSize() knows how to
 * convert a size in any writing mode to the stored writing mode.
 *
 * @see nsIFrame::Reflow() for more information.
 */
class ReflowOutput {
 public:
  explicit ReflowOutput(WritingMode aWritingMode)
      : mSize(aWritingMode), mWritingMode(aWritingMode) {}

  // A convenient constructor to get WritingMode in ReflowInput.
  explicit ReflowOutput(const ReflowInput& aReflowInput);

  nscoord ISize(WritingMode aWritingMode) const {
    return mSize.ISize(aWritingMode);
  }
  nscoord BSize(WritingMode aWritingMode) const {
    return mSize.BSize(aWritingMode);
  }
  LogicalSize Size(WritingMode aWritingMode) const {
    return mSize.ConvertTo(aWritingMode, mWritingMode);
  }

  nscoord& ISize(WritingMode aWritingMode) { return mSize.ISize(aWritingMode); }
  nscoord& BSize(WritingMode aWritingMode) { return mSize.BSize(aWritingMode); }

  // Set inline and block size from a LogicalSize, converting to our
  // writing mode as necessary.
  void SetSize(WritingMode aWM, const LogicalSize& aSize) {
    mSize = aSize.ConvertTo(mWritingMode, aWM);
  }

  // Set both inline and block size to zero -- no need for a writing mode!
  void ClearSize() { mSize.SizeTo(mWritingMode, 0, 0); }

  // Width and Height are physical dimensions, independent of writing mode.
  // Accessing these is slightly more expensive than accessing the logical
  // dimensions; as far as possible, client code should work purely with logical
  // dimensions.
  nscoord Width() const { return mSize.Width(mWritingMode); }
  nscoord Height() const { return mSize.Height(mWritingMode); }
  nscoord& Width() {
    return mWritingMode.IsVertical() ? mSize.BSize(mWritingMode)
                                     : mSize.ISize(mWritingMode);
  }
  nscoord& Height() {
    return mWritingMode.IsVertical() ? mSize.ISize(mWritingMode)
                                     : mSize.BSize(mWritingMode);
  }

  nsSize PhysicalSize() const { return mSize.GetPhysicalSize(mWritingMode); }

  // It's only meaningful to consider "ascent" on the block-start side of the
  // frame, so no need to pass a writing mode argument
  enum { ASK_FOR_BASELINE = nscoord_MAX };
  nscoord BlockStartAscent() const { return mBlockStartAscent; }
  void SetBlockStartAscent(nscoord aAscent) { mBlockStartAscent = aAscent; }

  // Metrics that _exactly_ enclose the text to allow precise MathML placements.
  nsBoundingMetrics mBoundingMetrics;  // [OUT]

  // Carried out block-end margin values. This is the collapsed
  // (generational) block-end margin value.
  CollapsingMargin mCarriedOutBEndMargin;

  // Set when the frame needs to retry reflow to apply text-box-trim-end
  // at a fragment boundary.
  bool mNeedsTextBoxTrimAtFragmentEndRetry = false;

  // For frames that have content that overflow their content area
  // (HasOverflowAreas() is true) these rectangles represent the total
  // area of the frame including visible overflow, i.e., don't include
  // overflowing content that is hidden.  The rects are in the local
  // coordinate space of the frame, and should be at least as big as the
  // desired size. If there is no content that overflows, then the
  // overflow area is identical to the desired size and should be {0, 0,
  // width, height}.
  OverflowAreas mOverflowAreas;

  nsRect& InkOverflow() { return mOverflowAreas.InkOverflow(); }
  const nsRect& InkOverflow() const { return mOverflowAreas.InkOverflow(); }
  nsRect& ScrollableOverflow() { return mOverflowAreas.ScrollableOverflow(); }
  const nsRect& ScrollableOverflow() const {
    return mOverflowAreas.ScrollableOverflow();
  }

  // Set all of mOverflowAreas to (0, 0, width, height).
  void SetOverflowAreasToDesiredBounds();

  // Union all of mOverflowAreas with (0, 0, width, height).
  void UnionOverflowAreasWithDesiredBounds();

  WritingMode GetWritingMode() const { return mWritingMode; }

 private:
  // Desired size of a frame's border-box.
  LogicalSize mSize;

  // Baseline (in block direction), or the default value ASK_FOR_BASELINE.
  nscoord mBlockStartAscent = ASK_FOR_BASELINE;

  WritingMode mWritingMode;
};

}  // namespace mozilla

#endif  // mozilla_ReflowOutput_h
