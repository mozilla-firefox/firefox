/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DISPLAYITEMCLIP_H_
#define DISPLAYITEMCLIP_H_

#include "mozilla/AlreadyAddRefed.h"
#include "nsMargin.h"
#include "nsRect.h"
#include "nsTArray.h"

class gfxContext;
class nsPresContext;
class nsRegion;

namespace mozilla {
namespace gfx {
class DrawTarget;
class Path;
}  // namespace gfx
namespace layers {
class StackingContextHelper;
}  // namespace layers
namespace wr {
struct ComplexClipRegion;
}  // namespace wr
}  // namespace mozilla

namespace mozilla {

/**
 * An DisplayItemClip represents the intersection of an optional rectangle
 * with a list of rounded rectangles (which is often empty), all in appunits.
 * It can represent everything CSS clipping can do to an element (except for
 * SVG clip-path), including no clipping at all.
 */
class DisplayItemClip {
  typedef mozilla::gfx::DeviceColor DeviceColor;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::Path Path;

 public:
  struct RoundedRect {
    nsRect mRect;
    // Indices into mRadii are the HalfCorner values in gfx/2d/Types.h
    nsRectCornerRadii mRadii;

    // This is here to keep track of how much the rounded rect was
    // inflated or shrunk relative to its reference frame. It is
    // necessary for the correct computation of contoured superellipses
    // used by CSS corner-shape (see bug 2058091).
    // Spec: https://drafts.csswg.org/css-borders/#contour-path
    nsMargin mInset;

    RoundedRect operator+(const nsPoint& aOffset) const {
      RoundedRect r = *this;
      r.mRect += aOffset;
      return r;
    }
    bool operator==(const RoundedRect& aOther) const {
      if (!mRect.IsEqualInterior(aOther.mRect)) {
        return false;
      }
      if (mRadii != aOther.mRadii) {
        return false;
      }
      return true;
    }
  };

  // Constructs a DisplayItemClip that does no clipping at all.
  DisplayItemClip() : mHaveClipRect(false) {}

  void SetTo(const nsRect& aRect);
  void SetTo(const nsRect& aRect, const nsRectCornerRadii* aRadii,
             const nsMargin* aInset = nullptr);
  void SetTo(const nsRect& aRect, const nsRect& aRoundedRect,
             const nsRectCornerRadii* aRadii);
  void IntersectWith(const DisplayItemClip& aOther);

  // Apply this |DisplayItemClip| to the given gfxContext.  Any saving of state
  // or clearing of other clips must be done by the caller.
  // See aBegin/aEnd note on ApplyRoundedRectsTo.
  void ApplyTo(gfxContext* aContext, int32_t A2D) const;

  void ApplyRectTo(gfxContext* aContext, int32_t A2D) const;
  // Applies the rounded rects in this Clip to aContext
  // Will only apply rounded rects from aBegin (inclusive) to aEnd
  // (exclusive) or the number of rounded rects, whichever is smaller.
  void ApplyRoundedRectClipsTo(gfxContext* aContext, int32_t A2DPRInt32,
                               uint32_t aBegin, uint32_t aEnd) const;

  // Draw (fill) the rounded rects in this clip to aContext
  void FillIntersectionOfRoundedRectClips(gfxContext* aContext,
                                          const DeviceColor& aColor,
                                          int32_t aAppUnitsPerDevPixel) const;
  // 'Draw' (create as a path, does not stroke or fill) aRoundRect to aContext
  already_AddRefed<Path> MakeRoundedRectPath(
      DrawTarget& aDrawTarget, int32_t A2D,
      const RoundedRect& aRoundRect) const;

  // Returns true if the intersection of aRect and this clip region is
  // non-empty. This is precise for DisplayItemClips with at most one
  // rounded rectangle. When multiple rounded rectangles are present, we just
  // check that the rectangle intersects all of them (but possibly in different
  // places). So it may return true when the correct answer is false.
  bool MayIntersect(const nsRect& aRect) const;

  // Return a rectangle contained in the intersection of aRect with this
  // clip region. Tries to return the largest possible rectangle, but may
  // not succeed.
  nsRect ApproximateIntersectInward(const nsRect& aRect) const;

  /*
   * Computes a region which contains the clipped area of this DisplayItemClip,
   * or if aOldClip is non-null, the union of the clipped area of this
   * DisplayItemClip with the clipped area of aOldClip translated by aShift.
   * The result is stored in aCombined. If the result would be infinite
   * (because one or both of the clips does no clipping), returns false.
   */
  bool ComputeRegionInClips(const DisplayItemClip* aOldClip,
                            const nsPoint& aShift, nsRegion* aCombined) const;

  // Returns false if aRect is definitely not clipped by a rounded corner in
  // this clip. Returns true if aRect is clipped by a rounded corner in this
  // clip or it can not be quickly determined that it is not clipped by a
  // rounded corner in this clip.
  bool IsRectClippedByRoundedCorner(const nsRect& aRect) const;

  // Returns false if aRect is definitely not clipped by anything in this clip.
  // Fast but not necessarily accurate.
  bool IsRectAffectedByClip(const nsRect& aRect) const;
  bool IsRectAffectedByClip(const nsIntRect& aRect, float aXScale,
                            float aYScale, int32_t A2D) const;

  // Intersection of all rects in this clip ignoring any rounded corners.
  nsRect NonRoundedIntersection() const;

  // Intersect the given rects with all rects in this clip, ignoring any
  // rounded corners.
  nsRect ApplyNonRoundedIntersection(const nsRect& aRect) const;

  // Gets rid of any rounded corners in this clip.
  void RemoveRoundedCorners();

  // Adds the difference between Intersect(*this + aPoint, aBounds) and
  // Intersect(aOther, aOtherBounds) to aDifference (or a bounding-box thereof).
  void AddOffsetAndComputeDifference(const nsPoint& aPoint,
                                     const nsRect& aBounds,
                                     const DisplayItemClip& aOther,
                                     const nsRect& aOtherBounds,
                                     nsRegion* aDifference);

  bool operator==(const DisplayItemClip& aOther) const {
    return mHaveClipRect == aOther.mHaveClipRect &&
           (!mHaveClipRect || mClipRect.IsEqualInterior(aOther.mClipRect)) &&
           mRoundedClipRects == aOther.mRoundedClipRects;
  }

  bool HasClip() const { return mHaveClipRect; }
  const nsRect& GetClipRect() const {
    NS_ASSERTION(HasClip(), "No clip rect!");
    return mClipRect;
  }

  void MoveBy(const nsPoint& aPoint);

  nsCString ToString() const;

  uint32_t GetRoundedRectCount() const { return mRoundedClipRects.Length(); }
  void AppendRoundedRects(nsTArray<RoundedRect>* aArray) const;

  void ToComplexClipRegions(int32_t aAppUnitsPerDevPixel,
                            nsTArray<wr::ComplexClipRegion>& aOutArray) const;

  static const DisplayItemClip& NoClip();

  static void Shutdown();

 private:
  nsRect mClipRect;
  CopyableTArray<RoundedRect> mRoundedClipRects;
  // If mHaveClipRect is false then this object represents no clipping at all
  // and mRoundedClipRects must be empty.
  bool mHaveClipRect;
};

}  // namespace mozilla

#endif /* DISPLAYITEMCLIP_H_ */
