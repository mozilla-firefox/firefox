/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* class that manages rules for positioning floats */

#include "nsFloatManager.h"

#include <algorithm>
#include <initializer_list>

#include "gfxContext.h"
#include "mozilla/PresShell.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ShapeUtils.h"
#include "nsBlockFrame.h"
#include "nsDeviceContext.h"
#include "nsError.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsImageRenderer.h"

using namespace mozilla;
using namespace mozilla::image;
using namespace mozilla::gfx;

int32_t nsFloatManager::sCachedFloatManagerCount = 0;
void* nsFloatManager::sCachedFloatManagers[NS_FLOAT_MANAGER_CACHE_SIZE];

/////////////////////////////////////////////////////////////////////////////
// nsFloatManager

nsFloatManager::nsFloatManager(WritingMode aWM)
    :
#ifdef DEBUG
      mWritingMode(aWM),
#endif
      mLineLeft(0),
      mBlockStart(0),
      mPushedLeftFloatPastBreak(false),
      mPushedRightFloatPastBreak(false),
      mSplitLeftFloatAcrossBreak(false),
      mSplitRightFloatAcrossBreak(false) {
  MOZ_COUNT_CTOR(nsFloatManager);
}

nsFloatManager::~nsFloatManager() { MOZ_COUNT_DTOR(nsFloatManager); }

// static
void* nsFloatManager::operator new(size_t aSize) noexcept(true) {
  if (sCachedFloatManagerCount > 0) {
    // We have cached unused instances of this class, return a cached
    // instance in stead of always creating a new one.
    return sCachedFloatManagers[--sCachedFloatManagerCount];
  }

  // The cache is empty, this means we have to create a new instance using
  // the global |operator new|.
  return moz_xmalloc(aSize);
}

void nsFloatManager::operator delete(void* aPtr, size_t aSize) {
  if (!aPtr) {
    return;
  }
  // This float manager is no longer used, if there's still room in
  // the cache we'll cache this float manager, unless the layout
  // module was already shut down.

  if (sCachedFloatManagerCount < NS_FLOAT_MANAGER_CACHE_SIZE &&
      sCachedFloatManagerCount >= 0) {
    // There's still space in the cache for more instances, put this
    // instance in the cache in stead of deleting it.

    sCachedFloatManagers[sCachedFloatManagerCount++] = aPtr;
    return;
  }

  // The cache is full, or the layout module has been shut down,
  // delete this float manager.
  free(aPtr);
}

/* static */
void nsFloatManager::Shutdown() {
  // The layout module is being shut down, clean up the cache and
  // disable further caching.

  int32_t i;

  for (i = 0; i < sCachedFloatManagerCount; i++) {
    void* floatManager = sCachedFloatManagers[i];
    if (floatManager) {
      free(floatManager);
    }
  }

  // Disable further caching.
  sCachedFloatManagerCount = -1;
}

#define CHECK_BLOCK_AND_LINE_DIR(aWM)                                       \
  NS_ASSERTION((aWM).GetBlockDir() == mWritingMode.GetBlockDir() &&         \
                   (aWM).IsLineInverted() == mWritingMode.IsLineInverted(), \
               "incompatible writing modes")

nsFlowAreaRect nsFloatManager::GetFlowArea(
    WritingMode aCBWM, WritingMode aWM, nscoord aBCoord, nscoord aBSize,
    BandInfoType aBandInfoType, ShapeType aShapeType, LogicalRect aContentArea,
    SavedState* aState, const nsSize& aContainerSize) const {
  CHECK_BLOCK_AND_LINE_DIR(aWM);
  NS_ASSERTION(aBSize >= 0, "unexpected max block size");
  NS_ASSERTION(aContentArea.ISize(aWM) >= 0,
               "unexpected content area inline size");

  nscoord blockStart = aBCoord + mBlockStart;
  if (blockStart < nscoord_MIN) {
    NS_WARNING("bad value");
    blockStart = nscoord_MIN;
  }

  // Determine the last float that we should consider.
  uint32_t floatCount;
  if (aState) {
    // Use the provided state.
    floatCount = aState->mFloatInfoCount;
    MOZ_ASSERT(floatCount <= mFloats.Length(), "bad state");
  } else {
    // Use our current state.
    floatCount = mFloats.Length();
  }

  // If there are no floats at all, or we're below the last one, return
  // quickly.
  if (floatCount == 0 || (mFloats[floatCount - 1].mLeftBEnd <= blockStart &&
                          mFloats[floatCount - 1].mRightBEnd <= blockStart)) {
    return nsFlowAreaRect(aWM, aContentArea.IStart(aWM), aBCoord,
                          aContentArea.ISize(aWM), aBSize,
                          nsFlowAreaRectFlags::NoFlags);
  }

  nscoord blockEnd;
  if (aBSize == nscoord_MAX) {
    // This warning (and the two below) are possible to hit on pages
    // with really large objects.
    NS_WARNING_ASSERTION(aBandInfoType == BandInfoType::BandFromPoint,
                         "bad height");
    blockEnd = nscoord_MAX;
  } else {
    blockEnd = blockStart + aBSize;
    if (blockEnd < blockStart || blockEnd > nscoord_MAX) {
      NS_WARNING("bad value");
      blockEnd = nscoord_MAX;
    }
  }
  nscoord lineLeft = mLineLeft + aContentArea.LineLeft(aWM, aContainerSize);
  nscoord lineRight = mLineLeft + aContentArea.LineRight(aWM, aContainerSize);
  if (lineRight < lineLeft) {
    NS_WARNING("bad value");
    lineRight = lineLeft;
  }

  // Walk backwards through the floats until we either hit the front of
  // the list or we're above |blockStart|.
  bool haveFloats = false;
  bool mayWiden = false;
  for (uint32_t i = floatCount; i > 0; --i) {
    const FloatInfo& fi = mFloats[i - 1];
    if (fi.mLeftBEnd <= blockStart && fi.mRightBEnd <= blockStart) {
      // There aren't any more floats that could intersect this band.
      break;
    }
    if (fi.IsEmpty(aShapeType)) {
      // Ignore empty float areas.
      // https://drafts.csswg.org/css-shapes/#relation-to-box-model-and-float-behavior
      continue;
    }

    nscoord floatBStart = fi.BStart(aShapeType);
    nscoord floatBEnd = fi.BEnd(aShapeType);
    if (blockStart < floatBStart &&
        aBandInfoType == BandInfoType::BandFromPoint) {
      // This float is below our band.  Shrink our band's height if needed.
      if (floatBStart < blockEnd) {
        blockEnd = floatBStart;
      }
    }
    // If blockStart == blockEnd (which happens only with WidthWithinHeight),
    // we include floats that begin at our 0-height vertical area.  We
    // need to do this to satisfy the invariant that a
    // WidthWithinHeight call is at least as narrow on both sides as a
    // BandFromPoint call beginning at its blockStart.
    else if (blockStart < floatBEnd &&
             (floatBStart < blockEnd ||
              (floatBStart == blockEnd && blockStart == blockEnd))) {
      // This float is in our band.

      // Shrink our band's width if needed.
      UsedFloat floatStyle = fi.mFrame->StyleDisplay()->UsedFloat(aCBWM);

      // When aBandInfoType is BandFromPoint, we're only intended to
      // consider a point along the y axis rather than a band.
      const nscoord bandBlockEnd =
          aBandInfoType == BandInfoType::BandFromPoint ? blockStart : blockEnd;
      if (floatStyle == UsedFloat::Left) {
        // A left float
        nscoord lineRightEdge =
            fi.LineRight(aShapeType, blockStart, bandBlockEnd);
        if (lineRightEdge > lineLeft) {
          lineLeft = lineRightEdge;
          // Only set haveFloats to true if the float is inside our
          // containing block.  This matches the spec for what some
          // callers want and disagrees for other callers, so we should
          // probably provide better information at some point.
          haveFloats = true;

          // Our area may widen in the block direction if this float may
          // narrow in the block direction.
          mayWiden = mayWiden || fi.MayNarrowInBlockDirection(aShapeType);
        }
      } else {
        // A right float
        nscoord lineLeftEdge =
            fi.LineLeft(aShapeType, blockStart, bandBlockEnd);
        if (lineLeftEdge < lineRight) {
          lineRight = lineLeftEdge;
          // See above.
          haveFloats = true;
          mayWiden = mayWiden || fi.MayNarrowInBlockDirection(aShapeType);
        }
      }

      // Shrink our band's height if needed.
      if (floatBEnd < blockEnd &&
          aBandInfoType == BandInfoType::BandFromPoint) {
        blockEnd = floatBEnd;
      }
    }
  }

  nscoord blockSize =
      (blockEnd == nscoord_MAX) ? nscoord_MAX : (blockEnd - blockStart);
  // convert back from LineLeft/Right to IStart
  nscoord inlineStart =
      aWM.IsBidiLTR()
          ? lineLeft - mLineLeft
          : mLineLeft - lineRight + LogicalSize(aWM, aContainerSize).ISize(aWM);

  nsFlowAreaRectFlags flags =
      (haveFloats ? nsFlowAreaRectFlags::HasFloats
                  : nsFlowAreaRectFlags::NoFlags) |
      (mayWiden ? nsFlowAreaRectFlags::MayWiden : nsFlowAreaRectFlags::NoFlags);
  // Some callers clamp the inline size of nsFlowAreaRect to be nonnegative
  // "for compatibility with nsSpaceManager". So, we set a flag here to record
  // the fact that the ISize is actually negative, so that downstream code can
  // realize that there's no place here where we could put a float-avoiding
  // block (even one with ISize of 0).
  if (lineRight - lineLeft < 0) {
    flags |= nsFlowAreaRectFlags::ISizeIsActuallyNegative;
  }

  return nsFlowAreaRect(aWM, inlineStart, blockStart - mBlockStart,
                        lineRight - lineLeft, blockSize, flags);
}

void nsFloatManager::AddFloat(nsIFrame* aFloatFrame,
                              const LogicalRect& aMarginRect, WritingMode aWM,
                              const nsSize& aContainerSize) {
  CHECK_BLOCK_AND_LINE_DIR(aWM);
  NS_ASSERTION(aMarginRect.ISize(aWM) >= 0, "negative inline size!");
  NS_ASSERTION(aMarginRect.BSize(aWM) >= 0, "negative block size!");

  FloatInfo info(aFloatFrame, mLineLeft, mBlockStart, aMarginRect, aWM,
                 aContainerSize);

  // Set mLeftBEnd and mRightBEnd.
  if (HasAnyFloats()) {
    FloatInfo& tail = mFloats[mFloats.Length() - 1];
    info.mLeftBEnd = tail.mLeftBEnd;
    info.mRightBEnd = tail.mRightBEnd;
  } else {
    info.mLeftBEnd = nscoord_MIN;
    info.mRightBEnd = nscoord_MIN;
  }
  WritingMode cbWM = aFloatFrame->GetParent()->GetWritingMode();
  UsedFloat floatStyle = aFloatFrame->StyleDisplay()->UsedFloat(cbWM);
  MOZ_ASSERT(floatStyle == UsedFloat::Left || floatStyle == UsedFloat::Right,
             "Unexpected float style!");
  nscoord& sideBEnd =
      floatStyle == UsedFloat::Left ? info.mLeftBEnd : info.mRightBEnd;
  nscoord thisBEnd = info.BEnd();
  if (thisBEnd > sideBEnd) {
    sideBEnd = thisBEnd;
  }

  mFloats.AppendElement(std::move(info));
}

// static
LogicalRect nsFloatManager::CalculateRegionFor(WritingMode aWM,
                                               nsIFrame* aFloat,
                                               const LogicalMargin& aMargin,
                                               const nsSize& aContainerSize) {
  // We consider relatively positioned frames at their original position.
  LogicalRect region(aWM,
                     nsRect(aFloat->GetNormalPosition(), aFloat->GetSize()),
                     aContainerSize);

  // Float region includes its margin
  region.Inflate(aWM, aMargin);

  // Don't store rectangles with negative margin-box width or height in
  // the float manager; it can't deal with them.
  if (region.ISize(aWM) < 0) {
    // Preserve the right margin-edge for left floats and the left
    // margin-edge for right floats
    const nsStyleDisplay* display = aFloat->StyleDisplay();
    WritingMode cbWM = aFloat->GetParent()->GetWritingMode();
    UsedFloat floatStyle = display->UsedFloat(cbWM);
    if ((UsedFloat::Left == floatStyle) == aWM.IsBidiLTR()) {
      region.IStart(aWM) = region.IEnd(aWM);
    }
    region.ISize(aWM) = 0;
  }
  if (region.BSize(aWM) < 0) {
    region.BSize(aWM) = 0;
  }
  return region;
}

NS_DECLARE_FRAME_PROPERTY_DELETABLE(FloatRegionProperty, nsMargin)

LogicalRect nsFloatManager::GetRegionFor(WritingMode aWM, nsIFrame* aFloat,
                                         const nsSize& aContainerSize) {
  LogicalRect region = aFloat->GetLogicalRect(aWM, aContainerSize);
  if (nsMargin* storedRegion = aFloat->GetProperty(FloatRegionProperty())) {
    region.Inflate(aWM, LogicalMargin(aWM, *storedRegion));
  }
  return region;
}

void nsFloatManager::StoreRegionFor(WritingMode aWM, nsIFrame* aFloat,
                                    const LogicalRect& aRegion,
                                    const nsSize& aContainerSize) {
  nsRect region = aRegion.GetPhysicalRect(aWM, aContainerSize);
  nsRect rect = aFloat->GetRect();
  if (region.IsEqualEdges(rect)) {
    aFloat->RemoveProperty(FloatRegionProperty());
  } else {
    nsMargin* storedMargin =
        aFloat->GetOrCreateDeletableProperty(FloatRegionProperty());
    *storedMargin = region - rect;
  }
}

nsresult nsFloatManager::RemoveTrailingRegions(nsIFrame* aFrameList) {
  if (!aFrameList) {
    return NS_OK;
  }
  // This could be a good bit simpler if we could guarantee that the
  // floats given were at the end of our list, so we could just search
  // for the head of aFrameList.  (But we can't;
  // layout/reftests/bugs/421710-1.html crashes.)
  nsTHashSet<nsIFrame*> frameSet(1);

  for (nsIFrame* f = aFrameList; f; f = f->GetNextSibling()) {
    frameSet.Insert(f);
  }

  uint32_t newLength = mFloats.Length();
  while (newLength > 0) {
    if (!frameSet.Contains(mFloats[newLength - 1].mFrame)) {
      break;
    }
    --newLength;
  }
  mFloats.TruncateLength(newLength);

#ifdef DEBUG
  for (uint32_t i = 0; i < mFloats.Length(); ++i) {
    NS_ASSERTION(
        !frameSet.Contains(mFloats[i].mFrame),
        "Frame region deletion was requested but we couldn't delete it");
  }
#endif

  return NS_OK;
}

void nsFloatManager::PushState(SavedState* aState) {
  MOZ_ASSERT(aState, "Need a place to save state");

  // This is a cheap push implementation, which
  // only saves the (x,y) and last frame in the mFrameInfoMap
  // which is enough info to get us back to where we should be
  // when pop is called.
  //
  // This push/pop mechanism is used to undo any
  // floats that were added during the unconstrained reflow
  // in nsBlockReflowContext::DoReflowBlock(). (See bug 96736)
  //
  // It should also be noted that the state for mFloatDamage is
  // intentionally not saved or restored in PushState() and PopState(),
  // since that could lead to bugs where damage is missed/dropped when
  // we move from position A to B (during the intermediate incremental
  // reflow mentioned above) and then from B to C during the subsequent
  // reflow. In the typical case A and C will be the same, but not always.
  // Allowing mFloatDamage to accumulate the damage incurred during both
  // reflows ensures that nothing gets missed.
  aState->mLineLeft = mLineLeft;
  aState->mBlockStart = mBlockStart;
  aState->mPushedLeftFloatPastBreak = mPushedLeftFloatPastBreak;
  aState->mPushedRightFloatPastBreak = mPushedRightFloatPastBreak;
  aState->mSplitLeftFloatAcrossBreak = mSplitLeftFloatAcrossBreak;
  aState->mSplitRightFloatAcrossBreak = mSplitRightFloatAcrossBreak;
  aState->mFloatInfoCount = mFloats.Length();
}

void nsFloatManager::PopState(SavedState* aState) {
  MOZ_ASSERT(aState, "No state to restore?");

  mLineLeft = aState->mLineLeft;
  mBlockStart = aState->mBlockStart;
  mPushedLeftFloatPastBreak = aState->mPushedLeftFloatPastBreak;
  mPushedRightFloatPastBreak = aState->mPushedRightFloatPastBreak;
  mSplitLeftFloatAcrossBreak = aState->mSplitLeftFloatAcrossBreak;
  mSplitRightFloatAcrossBreak = aState->mSplitRightFloatAcrossBreak;

  NS_ASSERTION(aState->mFloatInfoCount <= mFloats.Length(),
               "somebody misused PushState/PopState");
  mFloats.TruncateLength(aState->mFloatInfoCount);
}

nscoord nsFloatManager::LowestFloatBStart() const {
  if (mPushedLeftFloatPastBreak || mPushedRightFloatPastBreak) {
    return nscoord_MAX;
  }
  if (!HasAnyFloats()) {
    return nscoord_MIN;
  }
  return mFloats[mFloats.Length() - 1].BStart() - mBlockStart;
}

#ifdef DEBUG_FRAME_DUMP
void DebugListFloatManager(const nsFloatManager* aFloatManager) {
  aFloatManager->List(stdout);
}

nsresult nsFloatManager::List(FILE* out) const {
  if (!HasAnyFloats()) {
    return NS_OK;
  }

  for (uint32_t i = 0; i < mFloats.Length(); ++i) {
    const FloatInfo& fi = mFloats[i];
    fprintf_stderr(out,
                   "Float %u: frame=%p rect={%d,%d,%d,%d} BEnd={l:%d, r:%d}\n",
                   i, static_cast<void*>(fi.mFrame), fi.LineLeft(), fi.BStart(),
                   fi.ISize(), fi.BSize(), fi.mLeftBEnd, fi.mRightBEnd);
  }
  return NS_OK;
}
#endif

nscoord nsFloatManager::ClearFloats(nscoord aBCoord,
                                    UsedClear aClearType) const {
  if (!HasAnyFloats()) {
    return aBCoord;
  }

  nscoord blockEnd = aBCoord + mBlockStart;

  const FloatInfo& tail = mFloats[mFloats.Length() - 1];
  switch (aClearType) {
    case UsedClear::Both:
      blockEnd = std::max(blockEnd, tail.mLeftBEnd);
      blockEnd = std::max(blockEnd, tail.mRightBEnd);
      break;
    case UsedClear::Left:
      blockEnd = std::max(blockEnd, tail.mLeftBEnd);
      break;
    case UsedClear::Right:
      blockEnd = std::max(blockEnd, tail.mRightBEnd);
      break;
    case UsedClear::None:
      // Do nothing
      break;
  }

  blockEnd -= mBlockStart;

  return blockEnd;
}

bool nsFloatManager::ClearContinues(UsedClear aClearType) const {
  return ((mPushedLeftFloatPastBreak || mSplitLeftFloatAcrossBreak) &&
          (aClearType == UsedClear::Both || aClearType == UsedClear::Left)) ||
         ((mPushedRightFloatPastBreak || mSplitRightFloatAcrossBreak) &&
          (aClearType == UsedClear::Both || aClearType == UsedClear::Right));
}

/////////////////////////////////////////////////////////////////////////////
// ShapeInfo is an abstract class for implementing all the shapes in CSS
// Shapes Module. A subclass needs to override all the methods to adjust
// the flow area with respect to its shape.
//
class nsFloatManager::ShapeInfo {
 public:
  virtual ~ShapeInfo() = default;

  virtual nscoord LineLeft(const nscoord aBStart,
                           const nscoord aBEnd) const = 0;
  virtual nscoord LineRight(const nscoord aBStart,
                            const nscoord aBEnd) const = 0;
  virtual nscoord BStart() const = 0;
  virtual nscoord BEnd() const = 0;
  virtual bool IsEmpty() const = 0;

  // Does this shape possibly get inline narrower in the BStart() to BEnd()
  // span when proceeding in the block direction? This is false for unrounded
  // rectangles that span all the way to BEnd(), but could be true for other
  // shapes. Note that we don't care if the BEnd() falls short of the margin
  // rect -- the ShapeInfo can only affect float behavior in the span between
  // BStart() and BEnd().
  virtual bool MayNarrowInBlockDirection() const = 0;

  // Translate the current origin by the specified offsets.
  virtual void Translate(nscoord aLineLeft, nscoord aBlockStart) = 0;

  static LogicalRect ComputeShapeBoxRect(StyleShapeBox, nsIFrame* const aFrame,
                                         const LogicalRect& aMarginRect,
                                         WritingMode aWM);

  // Convert the LogicalRect to the special logical coordinate space used
  // in float manager.
  static nsRect ConvertToFloatLogical(const LogicalRect& aRect, WritingMode aWM,
                                      const nsSize& aContainerSize) {
    return nsRect(aRect.LineLeft(aWM, aContainerSize), aRect.BStart(aWM),
                  aRect.ISize(aWM), aRect.BSize(aWM));
  }

  static UniquePtr<ShapeInfo> CreateShapeBox(nsIFrame* const aFrame,
                                             nscoord aShapeMargin,
                                             const LogicalRect& aShapeBoxRect,
                                             WritingMode aWM,
                                             const nsSize& aContainerSize);

  static UniquePtr<ShapeInfo> CreateBasicShape(
      const StyleBasicShape& aBasicShape, nscoord aShapeMargin,
      nsIFrame* const aFrame, const LogicalRect& aShapeBoxRect,
      const LogicalRect& aMarginRect, WritingMode aWM,
      const nsSize& aContainerSize);

  static UniquePtr<ShapeInfo> CreateInset(const StyleBasicShape& aBasicShape,
                                          nscoord aShapeMargin,
                                          nsIFrame* aFrame,
                                          const LogicalRect& aShapeBoxRect,
                                          WritingMode aWM,
                                          const nsSize& aContainerSize);

  static UniquePtr<ShapeInfo> CreateCircleOrEllipse(
      const StyleBasicShape& aBasicShape, nscoord aShapeMargin,
      nsIFrame* const aFrame, const LogicalRect& aShapeBoxRect, WritingMode aWM,
      const nsSize& aContainerSize);

  static UniquePtr<ShapeInfo> CreatePolygon(const StyleBasicShape& aBasicShape,
                                            nscoord aShapeMargin,
                                            nsIFrame* const aFrame,
                                            const LogicalRect& aShapeBoxRect,
                                            const LogicalRect& aMarginRect,
                                            WritingMode aWM,
                                            const nsSize& aContainerSize);

  static UniquePtr<ShapeInfo> CreateImageShape(const StyleImage& aShapeImage,
                                               float aShapeImageThreshold,
                                               nscoord aShapeMargin,
                                               nsIFrame* const aFrame,
                                               const LogicalRect& aMarginRect,
                                               WritingMode aWM,
                                               const nsSize& aContainerSize);

 protected:
  // Compute the minimum line-axis difference between the bounding shape
  // box and its rounded corner within the given band (block-axis region).
  // This is used as a helper function to compute the LineRight() and
  // LineLeft(). See the picture in the implementation for an example.
  // RadiusL and RadiusB stand for radius on the line-axis and block-axis.
  //
  // Returns radius-x diff on the line-axis, or 0 if there's no rounded
  // corner within the given band.
  static nscoord ComputeEllipseLineInterceptDiff(
      const nscoord aShapeBoxBStart, const nscoord aShapeBoxBEnd,
      const nscoord aBStartCornerRadiusL, const nscoord aBStartCornerRadiusB,
      const nscoord aBEndCornerRadiusL, const nscoord aBEndCornerRadiusB,
      const nscoord aBandBStart, const nscoord aBandBEnd);

  static nscoord XInterceptAtY(const nscoord aY, const nscoord aRadiusX,
                               const nscoord aRadiusY);

  // Convert the physical point to the special logical coordinate space
  // used in float manager.
  static nsPoint ConvertToFloatLogical(const nsPoint& aPoint, WritingMode aWM,
                                       const nsSize& aContainerSize);

  // Convert the half corner radii (nscoord[8]) to the special logical
  // coordinate space used in float manager.
  static nsRectCornerRadii ConvertToFloatLogical(const nsRectCornerRadii&,
                                                 WritingMode aWM);

  // Some ShapeInfo subclasses may define their float areas in intervals.
  // Each interval is a rectangle that is one device pixel deep in the block
  // axis. The values are stored as block edges in the y coordinates,
  // and inline edges as the x coordinates. Interval arrays should be sorted
  // on increasing y values. This function uses a binary search to find the
  // first interval that contains aTargetY. If no such interval exists, this
  // function returns aIntervals.Length().
  static size_t MinIntervalIndexContainingY(const nsTArray<nsRect>& aIntervals,
                                            const nscoord aTargetY);

  // This interval function is designed to handle the arguments to ::LineLeft()
  // and LineRight() and interpret them for the supplied aIntervals.
  static nscoord LineEdge(const nsTArray<nsRect>& aIntervals,
                          const nscoord aBStart, const nscoord aBEnd,
                          bool aIsLineLeft);

  // These types, constants, and functions are useful for ShapeInfos that
  // allocate a distance field. Efficient distance field calculations use
  // integer values that are 5X the Euclidean distance. MAX_MARGIN_5X is the
  // largest possible margin that we can calculate (in 5X integer dev pixels),
  // given these constraints.
  typedef uint16_t dfType;
  static const dfType MAX_CHAMFER_VALUE;
  static const dfType MAX_MARGIN;
  static const dfType MAX_MARGIN_5X;

  // This function returns a typed, overflow-safe value of aShapeMargin in
  // 5X integer dev pixels.
  static dfType CalcUsedShapeMargin5X(nscoord aShapeMargin,
                                      int32_t aAppUnitsPerDevPixel);
};

const nsFloatManager::ShapeInfo::dfType
    nsFloatManager::ShapeInfo::MAX_CHAMFER_VALUE = 11;

const nsFloatManager::ShapeInfo::dfType nsFloatManager::ShapeInfo::MAX_MARGIN =
    (std::numeric_limits<dfType>::max() - MAX_CHAMFER_VALUE) / 5;

const nsFloatManager::ShapeInfo::dfType
    nsFloatManager::ShapeInfo::MAX_MARGIN_5X = MAX_MARGIN * 5;

/////////////////////////////////////////////////////////////////////////////
// EllipseShapeInfo
//
// Implements shape-outside: circle() and shape-outside: ellipse().
//
class nsFloatManager::EllipseShapeInfo final
    : public nsFloatManager::ShapeInfo {
 public:
  // Construct the float area using math to calculate the shape boundary.
  // This is the fast path and should be used when shape-margin is negligible,
  // or when the two values of aRadii are roughly equal. Those two conditions
  // are defined by ShapeMarginIsNegligible() and RadiiAreRoughlyEqual(). In
  // those cases, we can conveniently represent the entire float area using
  // an ellipse.
  EllipseShapeInfo(const nsPoint& aCenter, const nsSize& aRadii,
                   nscoord aShapeMargin);

  // Construct the float area using rasterization to calculate the shape
  // boundary. This constructor accounts for the fact that applying
  // 'shape-margin' to an ellipse produces a shape that is not mathematically
  // representable as an ellipse.
  EllipseShapeInfo(const nsPoint& aCenter, const nsSize& aRadii,
                   nscoord aShapeMargin, int32_t aAppUnitsPerDevPixel);

  static bool ShapeMarginIsNegligible(nscoord aShapeMargin) {
    // For now, only return true for a shape-margin of 0. In the future, if
    // we want to enable use of the fast-path constructor more often, this
    // limit could be increased;
    static const nscoord SHAPE_MARGIN_NEGLIGIBLE_MAX(0);
    return aShapeMargin <= SHAPE_MARGIN_NEGLIGIBLE_MAX;
  }

  static bool RadiiAreRoughlyEqual(const nsSize& aRadii) {
    // For now, only return true when we are exactly equal. In the future, if
    // we want to enable use of the fast-path constructor more often, this
    // could be generalized to allow radii that are in some close proportion
    // to each other.
    return aRadii.width == aRadii.height;
  }
  nscoord LineEdge(const nscoord aBStart, const nscoord aBEnd,
                   bool aLeft) const;
  nscoord LineLeft(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord LineRight(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord BStart() const override {
    return mCenter.y - mRadii.height - mShapeMargin;
  }
  nscoord BEnd() const override {
    return mCenter.y + mRadii.height + mShapeMargin;
  }
  bool IsEmpty() const override {
    // An EllipseShapeInfo is never empty, because an ellipse or circle with
    // a zero radius acts like a point, and an ellipse with one zero radius
    // acts like a line.
    return false;
  }
  bool MayNarrowInBlockDirection() const override { return true; }

  void Translate(nscoord aLineLeft, nscoord aBlockStart) override {
    mCenter.MoveBy(aLineLeft, aBlockStart);

    for (nsRect& interval : mIntervals) {
      interval.MoveBy(aLineLeft, aBlockStart);
    }
  }

 private:
  // The position of the center of the ellipse. The coordinate space is the
  // same as FloatInfo::mRect.
  nsPoint mCenter;
  // The radii of the ellipse in app units. The width and height represent
  // the line-axis and block-axis radii of the ellipse.
  nsSize mRadii;
  // The shape-margin of the ellipse in app units. If this value is greater
  // than zero, then we calculate the bounds of the ellipse + margin using
  // numerical methods and store the values in mIntervals.
  nscoord mShapeMargin;

  // An interval is slice of the float area defined by this EllipseShapeInfo.
  // Each interval is a rectangle that is one pixel deep in the block
  // axis. The values are stored as block edges in the y coordinates,
  // and inline edges as the x coordinates.

  // The intervals are stored in ascending order on y.
  nsTArray<nsRect> mIntervals;
};

nsFloatManager::EllipseShapeInfo::EllipseShapeInfo(const nsPoint& aCenter,
                                                   const nsSize& aRadii,
                                                   nscoord aShapeMargin)
    : mCenter(aCenter),
      mRadii(aRadii),
      mShapeMargin(
          0)  // We intentionally ignore the value of aShapeMargin here.
{
  MOZ_ASSERT(
      RadiiAreRoughlyEqual(aRadii) || ShapeMarginIsNegligible(aShapeMargin),
      "This constructor should only be called when margin is "
      "negligible or radii are roughly equal.");

  // We add aShapeMargin into the radii, and we earlier stored a mShapeMargin
  // of zero.
  mRadii.width += aShapeMargin;
  mRadii.height += aShapeMargin;
}

nsFloatManager::EllipseShapeInfo::EllipseShapeInfo(const nsPoint& aCenter,
                                                   const nsSize& aRadii,
                                                   nscoord aShapeMargin,
                                                   int32_t aAppUnitsPerDevPixel)
    : mCenter(aCenter), mRadii(aRadii), mShapeMargin(aShapeMargin) {
  if (RadiiAreRoughlyEqual(aRadii) || ShapeMarginIsNegligible(aShapeMargin)) {
    // Mimic the behavior of the simple constructor, by adding aShapeMargin
    // into the radii, and then storing mShapeMargin of zero.
    mRadii.width += mShapeMargin;
    mRadii.height += mShapeMargin;
    mShapeMargin = 0;
    return;
  }

  // We have to calculate a distance field from the ellipse edge, then build
  // intervals based on pixels with less than aShapeMargin distance to an
  // edge pixel.

  // mCenter and mRadii have already been translated into logical coordinates.
  // x = inline, y = block. Due to symmetry, we only need to calculate the
  // distance field for one quadrant of the ellipse. We choose the positive-x,
  // positive-y quadrant (the lower right quadrant in horizontal-tb writing
  // mode). We choose this quadrant because it allows us to traverse our
  // distance field in memory order, which is more cache efficient.
  // When we apply these intervals in LineLeft() and LineRight(), we
  // account for block ranges that hit other quadrants, or hit multiple
  // quadrants.

  // Given this setup, computing the distance field is a one-pass O(n)
  // operation that runs from block top-to-bottom, inline left-to-right. We
  // use a chamfer 5-7-11 5x5 matrix to compute minimum distance to an edge
  // pixel. This integer math computation is reasonably close to the true
  // Euclidean distance. The distances will be approximately 5x the true
  // distance, quantized in integer units. The 5x is factored away in the
  // comparison which builds the intervals.
  dfType usedMargin5X =
      CalcUsedShapeMargin5X(aShapeMargin, aAppUnitsPerDevPixel);

  // Calculate the bounds of one quadrant of the ellipse, in integer device
  // pixels. These bounds are equal to the rectangle defined by the radii,
  // plus the shape-margin value in both dimensions.
  const LayoutDeviceIntSize bounds =
      LayoutDevicePixel::FromAppUnitsRounded(mRadii, aAppUnitsPerDevPixel) +
      LayoutDeviceIntSize(usedMargin5X / 5, usedMargin5X / 5);

  // Since our distance field is computed with a 5x5 neighborhood, but only
  // looks in the negative block and negative inline directions, it is
  // effectively a 3x3 neighborhood. We need to expand our distance field
  // outwards by a further 2 pixels in both axes (on the minimum block edge
  // and the minimum inline edge). We call this edge area the expanded region.

  static const uint32_t iExpand = 2;
  static const uint32_t bExpand = 2;

  // Clamp the size of our distance field sizes to prevent multiplication
  // overflow.
  static const uint32_t DF_SIDE_MAX =
      floor(sqrt((double)(std::numeric_limits<int32_t>::max())));
  const uint32_t iSize = std::min(bounds.width + iExpand, DF_SIDE_MAX);
  const uint32_t bSize = std::min(bounds.height + bExpand, DF_SIDE_MAX);
  auto df = MakeUniqueFallible<dfType[]>(iSize * bSize);
  if (!df) {
    // Without a distance field, we can't reason about the float area.
    return;
  }

  // Single pass setting distance field, in positive block direction, three
  // cases:
  // 1) Expanded region pixel: set to MAX_MARGIN_5X.
  // 2) Pixel within the ellipse: set to 0.
  // 3) Other pixel: set to minimum neighborhood distance value, computed
  //                 with 5-7-11 chamfer.

  for (uint32_t b = 0; b < bSize; ++b) {
    bool bIsInExpandedRegion(b < bExpand);
    nscoord bInAppUnits = (b - bExpand) * aAppUnitsPerDevPixel;
    bool bIsMoreThanEllipseBEnd(bInAppUnits > mRadii.height);

    // Find the i intercept of the ellipse edge for this block row, and
    // adjust it to compensate for the expansion of the inline dimension.
    // If we're in the expanded region, or if we're using a b that's more
    // than the bEnd of the ellipse, the intercept is nscoord_MIN.
    // We have one other special case to consider: when the ellipse has no
    // height. In that case we treat the bInAppUnits == 0 case as
    // intercepting at the width of the ellipse. All other cases solve
    // the intersection mathematically.
    const int32_t iIntercept =
        (bIsInExpandedRegion || bIsMoreThanEllipseBEnd)
            ? nscoord_MIN
            : iExpand + NSAppUnitsToIntPixels(
                            (!!mRadii.height || bInAppUnits)
                                ? XInterceptAtY(bInAppUnits, mRadii.width,
                                                mRadii.height)
                                : mRadii.width,
                            aAppUnitsPerDevPixel);

    // Set iMax in preparation for this block row.
    int32_t iMax = iIntercept;

    for (uint32_t i = 0; i < iSize; ++i) {
      const uint32_t index = i + b * iSize;
      MOZ_ASSERT(index < (iSize * bSize),
                 "Our distance field index should be in-bounds.");

      // Handle our three cases, in order.
      if (i < iExpand || bIsInExpandedRegion) {
        // Case 1: Expanded reqion pixel.
        df[index] = MAX_MARGIN_5X;
      } else if ((int32_t)i <= iIntercept) {
        // Case 2: Pixel within the ellipse, or just outside the edge of it.
        // Having a positive height indicates that there's an area we can
        // be inside of.
        df[index] = (!!mRadii.height) ? 0 : 5;
      } else {
        // Case 3: Other pixel.

        // Backward-looking neighborhood distance from target pixel X
        // with chamfer 5-7-11 looks like:
        //
        // +--+--+--+
        // |  |11|  |
        // +--+--+--+
        // |11| 7| 5|
        // +--+--+--+
        // |  | 5| X|
        // +--+--+--+
        //
        // X should be set to the minimum of the values of all of the numbered
        // neighbors summed with the value in that chamfer cell.
        MOZ_ASSERT(index - iSize - 2 < (iSize * bSize) &&
                       index - (iSize * 2) - 1 < (iSize * bSize),
                   "Our distance field most extreme indices should be "
                   "in-bounds.");

        // clang-format off
        df[index] = std::min<dfType>(df[index - 1] + 5,
                    std::min<dfType>(df[index - iSize] + 5,
                    std::min<dfType>(df[index - iSize - 1] + 7,
                    std::min<dfType>(df[index - iSize - 2] + 11,
                    df[index - (iSize * 2) - 1] + 11))));
        // clang-format on

        // Check the df value and see if it's less than or equal to the
        // usedMargin5X value.
        if (df[index] <= usedMargin5X) {
          MOZ_ASSERT(iMax < (int32_t)i);
          iMax = i;
        } else {
          // Since we're computing the bottom-right quadrant, there's no way
          // for a later i value in this row to be within the usedMargin5X
          // value. Likewise, every row beyond us will encounter this
          // condition with an i value less than or equal to our i value now.
          // Since our chamfer only looks upward and leftward, we can stop
          // calculating for the rest of the row, because the distance field
          // values there will never be looked at in a later row's chamfer
          // calculation.
          break;
        }
      }
    }

    // It's very likely, though not guaranteed that we will find an pixel
    // within the shape-margin distance for each block row. This may not
    // always be true due to rounding errors.
    if (iMax > nscoord_MIN) {
      // Origin for this interval is at the center of the ellipse, adjusted
      // in the positive block direction by bInAppUnits.
      nsPoint origin(aCenter.x, aCenter.y + bInAppUnits);
      // Size is an inline iMax plus 1 (to account for the whole pixel) dev
      // pixels, by 1 block dev pixel. We convert this to app units.
      nsSize size((iMax - iExpand + 1) * aAppUnitsPerDevPixel,
                  aAppUnitsPerDevPixel);
      mIntervals.AppendElement(nsRect(origin, size));
    }
  }
}

nscoord nsFloatManager::EllipseShapeInfo::LineEdge(const nscoord aBStart,
                                                   const nscoord aBEnd,
                                                   bool aIsLineLeft) const {
  // If no mShapeMargin, just compute the edge using math.
  if (mShapeMargin == 0) {
    nscoord lineDiff = ComputeEllipseLineInterceptDiff(
        BStart(), BEnd(), mRadii.width, mRadii.height, mRadii.width,
        mRadii.height, aBStart, aBEnd);
    return mCenter.x + (aIsLineLeft ? (-mRadii.width + lineDiff)
                                    : (mRadii.width - lineDiff));
  }

  // We are checking against our intervals. Make sure we have some.
  if (mIntervals.IsEmpty()) {
    NS_WARNING("With mShapeMargin > 0, we can't proceed without intervals.");
    return aIsLineLeft ? nscoord_MAX : nscoord_MIN;
  }

  // Map aBStart and aBEnd into our intervals. Our intervals are calculated
  // for the lower-right quadrant (in terms of horizontal-tb writing mode).
  // If aBStart and aBEnd span the center of the ellipse, then we know we
  // are at the maximum displacement from the center.
  bool bStartIsAboveCenter = (aBStart < mCenter.y);
  bool bEndIsBelowOrAtCenter = (aBEnd >= mCenter.y);
  if (bStartIsAboveCenter && bEndIsBelowOrAtCenter) {
    return mCenter.x + (aIsLineLeft ? (-mRadii.width - mShapeMargin)
                                    : (mRadii.width + mShapeMargin));
  }

  // aBStart and aBEnd don't span the center. Since the intervals are
  // strictly wider approaching the center (the start of the mIntervals
  // array), we only need to find the interval at the block value closest to
  // the center. We find the min of aBStart, aBEnd, and their reflections --
  // whichever two of them are within the lower-right quadrant. When we
  // reflect from the upper-right quadrant to the lower-right, we have to
  // subtract 1 from the reflection, to account that block values are always
  // addressed from the leading block edge.

  // The key example is when we check with aBStart == aBEnd at the top of the
  // intervals. That block line would be considered contained in the
  // intervals (though it has no height), but its reflection would not be
  // within the intervals unless we subtract 1.
  nscoord bSmallestWithinIntervals = std::min(
      bStartIsAboveCenter ? aBStart + (mCenter.y - aBStart) * 2 - 1 : aBStart,
      bEndIsBelowOrAtCenter ? aBEnd : aBEnd + (mCenter.y - aBEnd) * 2 - 1);

  MOZ_ASSERT(bSmallestWithinIntervals >= mCenter.y &&
                 bSmallestWithinIntervals < BEnd(),
             "We should have a block value within the float area.");

  size_t index =
      MinIntervalIndexContainingY(mIntervals, bSmallestWithinIntervals);
  if (index >= mIntervals.Length()) {
    // This indicates that our intervals don't cover the block value
    // bSmallestWithinIntervals. This can happen when rounding error in the
    // distance field calculation resulted in the last block pixel row not
    // contributing to the float area. As long as we're within one block pixel
    // past the last interval, this is an expected outcome.
#ifdef DEBUG
    nscoord onePixelPastLastInterval =
        mIntervals[mIntervals.Length() - 1].YMost() +
        mIntervals[mIntervals.Length() - 1].Height();
    NS_WARNING_ASSERTION(bSmallestWithinIntervals < onePixelPastLastInterval,
                         "We should have found a matching interval for this "
                         "block value.");
#endif
    return aIsLineLeft ? nscoord_MAX : nscoord_MIN;
  }

  // The interval is storing the line right value. If aIsLineLeft is true,
  // return the line right value reflected about the center. Since this is
  // an inline measurement, it's just checking the distance to an edge, and
  // not a collision with a specific pixel. For that reason, we don't need
  // to subtract 1 from the reflection, as we did with the block reflection.
  nscoord iLineRight = mIntervals[index].XMost();
  return aIsLineLeft ? iLineRight - (iLineRight - mCenter.x) * 2 : iLineRight;
}

nscoord nsFloatManager::EllipseShapeInfo::LineLeft(const nscoord aBStart,
                                                   const nscoord aBEnd) const {
  return LineEdge(aBStart, aBEnd, true);
}

nscoord nsFloatManager::EllipseShapeInfo::LineRight(const nscoord aBStart,
                                                    const nscoord aBEnd) const {
  return LineEdge(aBStart, aBEnd, false);
}

/////////////////////////////////////////////////////////////////////////////
// RoundedBoxShapeInfo
//
// Implements shape-outside: <shape-box> and shape-outside: inset().
//
class nsFloatManager::RoundedBoxShapeInfo final
    : public nsFloatManager::ShapeInfo {
 public:
  RoundedBoxShapeInfo(const nsRect& aRect, nsRectCornerRadii&& aRadii)
      : mRect(aRect),
        mRadii(std::move(aRadii)),
        mHasRadii(!mRadii.IsEmpty()),
        mShapeMargin(0) {}

  RoundedBoxShapeInfo(const nsRect& aRect, nsRectCornerRadii&& aRadii,
                      nscoord aShapeMargin, int32_t aAppUnitsPerDevPixel);

  nscoord LineLeft(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord LineRight(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord BStart() const override { return mRect.y; }
  nscoord BEnd() const override { return mRect.YMost(); }
  bool IsEmpty() const override {
    // A RoundedBoxShapeInfo is never empty, because if it is collapsed to
    // zero area, it acts like a point. If it is collapsed further, to become
    // inside-out, it acts like a rect in the same shape as the inside-out
    // rect.
    return false;
  }
  bool MayNarrowInBlockDirection() const override {
    // Only possible to narrow if there is radii.
    return mHasRadii;
  }

  void Translate(nscoord aLineLeft, nscoord aBlockStart) override {
    mRect.MoveBy(aLineLeft, aBlockStart);

    if (mShapeMargin > 0) {
      MOZ_ASSERT(mLogicalTopLeftCorner && mLogicalTopRightCorner &&
                     mLogicalBottomLeftCorner && mLogicalBottomRightCorner,
                 "If we have positive shape-margin, we should have corners.");
      mLogicalTopLeftCorner->Translate(aLineLeft, aBlockStart);
      mLogicalTopRightCorner->Translate(aLineLeft, aBlockStart);
      mLogicalBottomLeftCorner->Translate(aLineLeft, aBlockStart);
      mLogicalBottomRightCorner->Translate(aLineLeft, aBlockStart);
    }
  }

  static bool EachCornerHasBalancedRadii(const nsRectCornerRadii& aRadii) {
    return aRadii.TopLeft().IsSquare() && aRadii.TopRight().IsSquare() &&
           aRadii.BottomLeft().IsSquare() && aRadii.BottomRight().IsSquare();
  }

 private:
  // The rect of the rounded box shape in the float manager's coordinate
  // space.
  nsRect mRect;
  // The half corner radii of the reference box. It's an nsRectCornerRadii
  // in the float manager's coordinate space.
  const nsRectCornerRadii mRadii;

  // Whether there's a radii.
  const bool mHasRadii;

  // A shape-margin value extends the boundaries of the float area. When our
  // first constructor is used, it is for the creation of rounded boxes that
  // can ignore shape-margin -- either because it was specified as zero or
  // because the box shape and radii can be inflated to account for it. When
  // our second constructor is used, we store the shape-margin value here.
  const nscoord mShapeMargin;

  // If our second constructor is called (which implies mShapeMargin > 0),
  // we will construct EllipseShapeInfo objects for each corner. We use the
  // float logical naming here, where LogicalTopLeftCorner means the BStart
  // LineLeft corner, and similarly for the other corners.
  UniquePtr<EllipseShapeInfo> mLogicalTopLeftCorner;
  UniquePtr<EllipseShapeInfo> mLogicalTopRightCorner;
  UniquePtr<EllipseShapeInfo> mLogicalBottomLeftCorner;
  UniquePtr<EllipseShapeInfo> mLogicalBottomRightCorner;
};

nsFloatManager::RoundedBoxShapeInfo::RoundedBoxShapeInfo(
    const nsRect& aRect, nsRectCornerRadii&& aRadii, nscoord aShapeMargin,
    int32_t aAppUnitsPerDevPixel)
    : mRect(aRect),
      mRadii(std::move(aRadii)),
      mHasRadii(true),
      mShapeMargin(aShapeMargin) {
  MOZ_ASSERT(mShapeMargin > 0 && !EachCornerHasBalancedRadii(mRadii),
             "Slow constructor should only be used for for shape-margin > 0 "
             "and radii with elliptical corners.");

  // Before we inflate mRect by mShapeMargin, construct each of our corners.
  // If we do it in this order, it's a bit simpler to calculate the center
  // of each of the corners.
  mLogicalTopLeftCorner = MakeUnique<EllipseShapeInfo>(
      nsPoint(mRect.X() + mRadii[eCornerTopLeftX],
              mRect.Y() + mRadii[eCornerTopLeftY]),
      nsSize(mRadii[eCornerTopLeftX], mRadii[eCornerTopLeftY]), mShapeMargin,
      aAppUnitsPerDevPixel);

  mLogicalTopRightCorner = MakeUnique<EllipseShapeInfo>(
      nsPoint(mRect.XMost() - mRadii[eCornerTopRightX],
              mRect.Y() + mRadii[eCornerTopRightY]),
      nsSize(mRadii[eCornerTopRightX], mRadii[eCornerTopRightY]), mShapeMargin,
      aAppUnitsPerDevPixel);

  mLogicalBottomLeftCorner = MakeUnique<EllipseShapeInfo>(
      nsPoint(mRect.X() + mRadii[eCornerBottomLeftX],
              mRect.YMost() - mRadii[eCornerBottomLeftY]),
      nsSize(mRadii[eCornerBottomLeftX], mRadii[eCornerBottomLeftY]),
      mShapeMargin, aAppUnitsPerDevPixel);

  mLogicalBottomRightCorner = MakeUnique<EllipseShapeInfo>(
      nsPoint(mRect.XMost() - mRadii[eCornerBottomRightX],
              mRect.YMost() - mRadii[eCornerBottomRightY]),
      nsSize(mRadii[eCornerBottomRightX], mRadii[eCornerBottomRightY]),
      mShapeMargin, aAppUnitsPerDevPixel);

  // Now we inflate our mRect by mShapeMargin.
  mRect.Inflate(mShapeMargin);
}

nscoord nsFloatManager::RoundedBoxShapeInfo::LineLeft(
    const nscoord aBStart, const nscoord aBEnd) const {
  if (mShapeMargin == 0) {
    if (!mHasRadii) {
      return mRect.x;
    }

    nscoord lineLeftDiff = ComputeEllipseLineInterceptDiff(
        mRect.y, mRect.YMost(), mRadii[eCornerTopLeftX],
        mRadii[eCornerTopLeftY], mRadii[eCornerBottomLeftX],
        mRadii[eCornerBottomLeftY], aBStart, aBEnd);
    return mRect.x + lineLeftDiff;
  }

  MOZ_ASSERT(mLogicalTopLeftCorner && mLogicalBottomLeftCorner,
             "If we have positive shape-margin, we should have corners.");

  // Determine if aBEnd is within our top corner.
  if (aBEnd < mLogicalTopLeftCorner->BEnd()) {
    return mLogicalTopLeftCorner->LineLeft(aBStart, aBEnd);
  }

  // Determine if aBStart is within our bottom corner.
  if (aBStart >= mLogicalBottomLeftCorner->BStart()) {
    return mLogicalBottomLeftCorner->LineLeft(aBStart, aBEnd);
  }

  // Either aBStart or aBEnd or both are within the flat part of our left
  // edge. Because we've already inflated our mRect to encompass our
  // mShapeMargin, we can just return the edge.
  return mRect.X();
}

nscoord nsFloatManager::RoundedBoxShapeInfo::LineRight(
    const nscoord aBStart, const nscoord aBEnd) const {
  if (mShapeMargin == 0) {
    if (!mHasRadii) {
      return mRect.XMost();
    }

    nscoord lineRightDiff = ComputeEllipseLineInterceptDiff(
        mRect.y, mRect.YMost(), mRadii[eCornerTopRightX],
        mRadii[eCornerTopRightY], mRadii[eCornerBottomRightX],
        mRadii[eCornerBottomRightY], aBStart, aBEnd);
    return mRect.XMost() - lineRightDiff;
  }

  MOZ_ASSERT(mLogicalTopRightCorner && mLogicalBottomRightCorner,
             "If we have positive shape-margin, we should have corners.");

  // Determine if aBEnd is within our top corner.
  if (aBEnd < mLogicalTopRightCorner->BEnd()) {
    return mLogicalTopRightCorner->LineRight(aBStart, aBEnd);
  }

  // Determine if aBStart is within our bottom corner.
  if (aBStart >= mLogicalBottomRightCorner->BStart()) {
    return mLogicalBottomRightCorner->LineRight(aBStart, aBEnd);
  }

  // Either aBStart or aBEnd or both are within the flat part of our right
  // edge. Because we've already inflated our mRect to encompass our
  // mShapeMargin, we can just return the edge.
  return mRect.XMost();
}

/////////////////////////////////////////////////////////////////////////////
// PolygonShapeInfo
//
// Implements shape-outside: polygon().
//
class nsFloatManager::PolygonShapeInfo final
    : public nsFloatManager::ShapeInfo {
 public:
  explicit PolygonShapeInfo(nsTArray<nsPoint>&& aVertices);
  PolygonShapeInfo(nsTArray<nsPoint>&& aVertices, nscoord aShapeMargin,
                   int32_t aAppUnitsPerDevPixel, const nsRect& aMarginRect);

  nscoord LineLeft(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord LineRight(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord BStart() const override { return mBStart; }
  nscoord BEnd() const override { return mBEnd; }
  bool IsEmpty() const override {
    // A PolygonShapeInfo is never empty, because the parser prevents us from
    // creating a shape with no vertices. If we only have 1 vertex, the
    // shape acts like a point. With 2 non-coincident vertices, the shape
    // acts like a line.
    return false;
  }
  bool MayNarrowInBlockDirection() const override { return true; }

  void Translate(nscoord aLineLeft, nscoord aBlockStart) override;

 private:
  // Helper method for determining the mBStart and mBEnd based on the
  // vertices' y extent.
  void ComputeExtent();

  // Helper method for implementing LineLeft() and LineRight().
  nscoord ComputeLineIntercept(
      const nscoord aBStart, const nscoord aBEnd,
      nscoord (*aCompareOp)(std::initializer_list<nscoord>),
      const nscoord aLineInterceptInitialValue) const;

  // Given a horizontal line y, and two points p1 and p2 forming a line
  // segment L. Solve x for the intersection of y and L. This method
  // assumes y and L do intersect, and L is *not* horizontal.
  static nscoord XInterceptAtY(const nscoord aY, const nsPoint& aP1,
                               const nsPoint& aP2);

  // The vertices of the polygon in the float manager's coordinate space.
  nsTArray<nsPoint> mVertices;

  // An interval is slice of the float area defined by this PolygonShapeInfo.
  // These are only generated and used in float area calculations for
  // shape-margin > 0. Each interval is a rectangle that is one device pixel
  // deep in the block axis. The values are stored as block edges in the y
  // coordinates, and inline edges as the x coordinates.

  // The intervals are stored in ascending order on y.
  nsTArray<nsRect> mIntervals;

  // Computed block start and block end value of the polygon shape. These
  // initial values are set to correct values in ComputeExtent(), which is
  // called from all constructors. Afterwards, mBStart is guaranteed to be
  // less than or equal to mBEnd.
  nscoord mBStart = nscoord_MAX;
  nscoord mBEnd = nscoord_MIN;
};

nsFloatManager::PolygonShapeInfo::PolygonShapeInfo(
    nsTArray<nsPoint>&& aVertices)
    : mVertices(std::move(aVertices)) {
  ComputeExtent();
}

nsFloatManager::PolygonShapeInfo::PolygonShapeInfo(
    nsTArray<nsPoint>&& aVertices, nscoord aShapeMargin,
    int32_t aAppUnitsPerDevPixel, const nsRect& aMarginRect)
    : mVertices(std::move(aVertices)) {
  MOZ_ASSERT(aShapeMargin > 0,
             "This constructor should only be used for a "
             "polygon with a positive shape-margin.");

  ComputeExtent();

  // With a positive aShapeMargin, we have to calculate a distance
  // field from the opaque pixels, then build intervals based on
  // them being within aShapeMargin distance to an opaque pixel.

  // Roughly: for each pixel in the margin box, we need to determine the
  // distance to the nearest opaque image-pixel.  If that distance is less
  // than aShapeMargin, we consider this margin-box pixel as being part of
  // the float area.

  // Computing the distance field is a two-pass O(n) operation.
  // We use a chamfer 5-7-11 5x5 matrix to compute minimum distance
  // to an opaque pixel. This integer math computation is reasonably
  // close to the true Euclidean distance. The distances will be
  // approximately 5x the true distance, quantized in integer units.
  // The 5x is factored away in the comparison used in the final
  // pass which builds the intervals.
  dfType usedMargin5X =
      CalcUsedShapeMargin5X(aShapeMargin, aAppUnitsPerDevPixel);

  // Allocate our distance field.  The distance field has to cover
  // the entire aMarginRect, since aShapeMargin could bleed into it.
  // Conveniently, our vertices have been converted into this same space,
  // so if we cover the aMarginRect, we cover all the vertices.
  const LayoutDeviceIntSize marginRectDevPixels =
      LayoutDevicePixel::FromAppUnitsRounded(aMarginRect.Size(),
                                             aAppUnitsPerDevPixel);

  // Since our distance field is computed with a 5x5 neighborhood,
  // we need to expand our distance field by a further 4 pixels in
  // both axes, 2 on the leading edge and 2 on the trailing edge.
  // We call this edge area the "expanded region".
  static const uint32_t kiExpansionPerSide = 2;
  static const uint32_t kbExpansionPerSide = 2;

  // Clamp the size of our distance field sizes to prevent multiplication
  // overflow.
  static const uint32_t DF_SIDE_MAX =
      floor(sqrt((double)(std::numeric_limits<int32_t>::max())));

  // Clamp the margin plus 2X the expansion values between expansion + 1 and
  // DF_SIDE_MAX. This ensures that the distance field allocation doesn't
  // overflow during multiplication, and the reverse iteration doesn't
  // underflow.
  const uint32_t iSize =
      std::max(std::min(marginRectDevPixels.width + (kiExpansionPerSide * 2),
                        DF_SIDE_MAX),
               kiExpansionPerSide + 1);
  const uint32_t bSize =
      std::max(std::min(marginRectDevPixels.height + (kbExpansionPerSide * 2),
                        DF_SIDE_MAX),
               kbExpansionPerSide + 1);

  // Since the margin-box size is CSS controlled, and large values will
  // generate large iSize and bSize values, we do a fallible allocation for
  // the distance field. If allocation fails, we early exit and layout will
  // be wrong, but we'll avoid aborting from OOM.
  auto df = MakeUniqueFallible<dfType[]>(iSize * bSize);
  if (!df) {
    // Without a distance field, we can't reason about the float area.
    return;
  }

  // First pass setting distance field, starting at top-left, three cases:
  // 1) Expanded region pixel: set to MAX_MARGIN_5X.
  // 2) Pixel within the polygon: set to 0.
  // 3) Other pixel: set to minimum backward-looking neighborhood
  //                 distance value, computed with 5-7-11 chamfer.

  for (uint32_t b = 0; b < bSize; ++b) {
    // Find the left and right i intercepts of the polygon edge for this
    // block row, and adjust them to compensate for the expansion of the
    // inline dimension. If we're in the expanded region, or if we're using
    // a b that's less than the bStart of the polygon, the intercepts are
    // the nscoord min and max limits.
    nscoord bInAppUnits = (b - kbExpansionPerSide) * aAppUnitsPerDevPixel;
    bool bIsInExpandedRegion(b < kbExpansionPerSide ||
                             b >= bSize - kbExpansionPerSide);

    // We now figure out the i values that correspond to the left edge and
    // the right edge of the polygon at one-dev-pixel-thick strip of b. We
    // have a ComputeLineIntercept function that takes and returns app unit
    // coordinates in the space of aMarginRect. So to pass in b values, we
    // first have to add the aMarginRect.y value. And for the values that we
    // get out, we have to subtract away the aMarginRect.x value before
    // converting the app units to dev pixels.
    nscoord bInAppUnitsMarginRect = bInAppUnits + aMarginRect.y;
    bool bIsLessThanPolygonBStart(bInAppUnitsMarginRect < mBStart);
    bool bIsMoreThanPolygonBEnd(bInAppUnitsMarginRect > mBEnd);

    const int32_t iLeftEdge =
        (bIsInExpandedRegion || bIsLessThanPolygonBStart ||
         bIsMoreThanPolygonBEnd)
            ? nscoord_MAX
            : kiExpansionPerSide +
                  NSAppUnitsToIntPixels(
                      ComputeLineIntercept(
                          bInAppUnitsMarginRect,
                          bInAppUnitsMarginRect + aAppUnitsPerDevPixel,
                          std::min<nscoord>, nscoord_MAX) -
                          aMarginRect.x,
                      aAppUnitsPerDevPixel);

    const int32_t iRightEdge =
        (bIsInExpandedRegion || bIsLessThanPolygonBStart ||
         bIsMoreThanPolygonBEnd)
            ? nscoord_MIN
            : kiExpansionPerSide +
                  NSAppUnitsToIntPixels(
                      ComputeLineIntercept(
                          bInAppUnitsMarginRect,
                          bInAppUnitsMarginRect + aAppUnitsPerDevPixel,
                          std::max<nscoord>, nscoord_MIN) -
                          aMarginRect.x,
                      aAppUnitsPerDevPixel);

    for (uint32_t i = 0; i < iSize; ++i) {
      const uint32_t index = i + b * iSize;
      MOZ_ASSERT(index < (iSize * bSize),
                 "Our distance field index should be in-bounds.");

      // Handle our three cases, in order.
      if (i < kiExpansionPerSide || i >= iSize - kiExpansionPerSide ||
          bIsInExpandedRegion) {
        // Case 1: Expanded pixel.
        df[index] = MAX_MARGIN_5X;
      } else if ((int32_t)i >= iLeftEdge && (int32_t)i <= iRightEdge) {
        // Case 2: Polygon pixel, either inside or just adjacent to the right
        // edge. We need this special distinction to detect a space between
        // edges that is less than one dev pixel.
        df[index] = (int32_t)i < iRightEdge ? 0 : 5;
      } else {
        // Case 3: Other pixel.

        // Backward-looking neighborhood distance from target pixel X
        // with chamfer 5-7-11 looks like:
        //
        // +--+--+--+--+--+
        // |  |11|  |11|  |
        // +--+--+--+--+--+
        // |11| 7| 5| 7|11|
        // +--+--+--+--+--+
        // |  | 5| X|  |  |
        // +--+--+--+--+--+
        //
        // X should be set to the minimum of MAX_MARGIN_5X and the
        // values of all of the numbered neighbors summed with the
        // value in that chamfer cell.
        MOZ_ASSERT(index - (iSize * 2) - 1 < (iSize * bSize) &&
                       index - iSize - 2 < (iSize * bSize),
                   "Our distance field most extreme indices should be "
                   "in-bounds.");

        // clang-format off
        df[index] = std::min<dfType>(MAX_MARGIN_5X,
                    std::min<dfType>(df[index - (iSize * 2) - 1] + 11,
                    std::min<dfType>(df[index - (iSize * 2) + 1] + 11,
                    std::min<dfType>(df[index - iSize - 2] + 11,
                    std::min<dfType>(df[index - iSize - 1] + 7,
                    std::min<dfType>(df[index - iSize] + 5,
                    std::min<dfType>(df[index - iSize + 1] + 7,
                    std::min<dfType>(df[index - iSize + 2] + 11,
                                     df[index - 1] + 5))))))));
        // clang-format on
      }
    }
  }

  // Okay, time for the second pass. This pass is in reverse order from
  // the first pass. All of our opaque pixels have been set to 0, and all
  // of our expanded region pixels have been set to MAX_MARGIN_5X. Other
  // pixels have been set to some value between those two (inclusive) but
  // this hasn't yet taken into account the neighbors that were processed
  // after them in the first pass. This time we reverse iterate so we can
  // apply the forward-looking chamfer.

  // This time, we constrain our outer and inner loop to ignore the
  // expanded region pixels. For each pixel we iterate, we set the df value
  // to the minimum forward-looking neighborhood distance value, computed
  // with a 5-7-11 chamfer. We also check each df value against the
  // usedMargin5X threshold, and use that to set the iMin and iMax values
  // for the interval we'll create for that block axis value (b).

  // At the end of each row, if any of the other pixels had a value less
  // than usedMargin5X, we create an interval.
  for (uint32_t b = bSize - kbExpansionPerSide - 1; b >= kbExpansionPerSide;
       --b) {
    // iMin tracks the first df pixel and iMax the last df pixel whose
    // df[] value is less than usedMargin5X. Set iMin and iMax in
    // preparation for this row or column.
    int32_t iMin = iSize;
    int32_t iMax = -1;

    for (uint32_t i = iSize - kiExpansionPerSide - 1; i >= kiExpansionPerSide;
         --i) {
      const uint32_t index = i + b * iSize;
      MOZ_ASSERT(index < (iSize * bSize),
                 "Our distance field index should be in-bounds.");

      // Only apply the chamfer calculation if the df value is not
      // already 0, since the chamfer can only reduce the value.
      if (df[index]) {
        // Forward-looking neighborhood distance from target pixel X
        // with chamfer 5-7-11 looks like:
        //
        // +--+--+--+--+--+
        // |  |  | X| 5|  |
        // +--+--+--+--+--+
        // |11| 7| 5| 7|11|
        // +--+--+--+--+--+
        // |  |11|  |11|  |
        // +--+--+--+--+--+
        //
        // X should be set to the minimum of its current value and
        // the values of all of the numbered neighbors summed with
        // the value in that chamfer cell.
        MOZ_ASSERT(index + (iSize * 2) + 1 < (iSize * bSize) &&
                       index + iSize + 2 < (iSize * bSize),
                   "Our distance field most extreme indices should be "
                   "in-bounds.");

        // clang-format off
        df[index] = std::min<dfType>(df[index],
                    std::min<dfType>(df[index + (iSize * 2) + 1] + 11,
                    std::min<dfType>(df[index + (iSize * 2) - 1] + 11,
                    std::min<dfType>(df[index + iSize + 2] + 11,
                    std::min<dfType>(df[index + iSize + 1] + 7,
                    std::min<dfType>(df[index + iSize] + 5,
                    std::min<dfType>(df[index + iSize - 1] + 7,
                    std::min<dfType>(df[index + iSize - 2] + 11,
                                     df[index + 1] + 5))))))));
        // clang-format on
      }

      // Finally, we can check the df value and see if it's less than
      // or equal to the usedMargin5X value.
      if (df[index] <= usedMargin5X) {
        if (iMax == -1) {
          iMax = i;
        }
        MOZ_ASSERT(iMin > (int32_t)i);
        iMin = i;
      }
    }

    if (iMax != -1) {
      // Our interval values, iMin, iMax, and b are all calculated from
      // the expanded region, which is based on the margin rect. To create
      // our interval, we have to subtract kiExpansionPerSide from iMin and
      // iMax, and subtract kbExpansionPerSide from b to account for the
      // expanded region edges.  This produces coords that are relative to
      // our margin-rect.

      // Origin for this interval is at the aMarginRect origin, adjusted in
      // the block direction by b in app units, and in the inline direction
      // by iMin in app units.
      nsPoint origin(
          aMarginRect.x + (iMin - kiExpansionPerSide) * aAppUnitsPerDevPixel,
          aMarginRect.y + (b - kbExpansionPerSide) * aAppUnitsPerDevPixel);

      // Size is the difference in iMax and iMin, plus 1 (to account for the
      // whole pixel) dev pixels, by 1 block dev pixel. We don't bother
      // subtracting kiExpansionPerSide from iMin and iMax in this case
      // because we only care about the distance between them. We convert
      // everything to app units.
      nsSize size((iMax - iMin + 1) * aAppUnitsPerDevPixel,
                  aAppUnitsPerDevPixel);

      mIntervals.AppendElement(nsRect(origin, size));
    }
  }

  // Reverse the intervals keep the array sorted on the block direction.
  mIntervals.Reverse();

  // Adjust our extents by aShapeMargin. This may cause overflow of some
  // kind if aShapeMargin is large, so we do some clamping to maintain the
  // invariant mBStart <= mBEnd.
  mBStart = std::min(mBStart, mBStart - aShapeMargin);
  mBEnd = std::max(mBEnd, mBEnd + aShapeMargin);
}

nscoord nsFloatManager::PolygonShapeInfo::LineLeft(const nscoord aBStart,
                                                   const nscoord aBEnd) const {
  // Use intervals if we have them.
  if (!mIntervals.IsEmpty()) {
    return LineEdge(mIntervals, aBStart, aBEnd, true);
  }

  // We want the line-left-most inline-axis coordinate where the
  // (block-axis) aBStart/aBEnd band crosses a line segment of the polygon.
  // To get that, we start as line-right as possible (at nscoord_MAX). Then
  // we iterate each line segment to compute its intersection point with the
  // band (if any) and using std::min() successively to get the smallest
  // inline-coordinates among those intersection points.
  //
  // Note: std::min<nscoord> means the function std::min() with template
  // parameter nscoord, not the minimum value of nscoord.
  return ComputeLineIntercept(aBStart, aBEnd, std::min<nscoord>, nscoord_MAX);
}

nscoord nsFloatManager::PolygonShapeInfo::LineRight(const nscoord aBStart,
                                                    const nscoord aBEnd) const {
  // Use intervals if we have them.
  if (!mIntervals.IsEmpty()) {
    return LineEdge(mIntervals, aBStart, aBEnd, false);
  }

  // Similar to LineLeft(). Though here, we want the line-right-most
  // inline-axis coordinate, so we instead start at nscoord_MIN and use
  // std::max() to get the biggest inline-coordinate among those
  // intersection points.
  return ComputeLineIntercept(aBStart, aBEnd, std::max<nscoord>, nscoord_MIN);
}

void nsFloatManager::PolygonShapeInfo::ComputeExtent() {
  // mBStart and mBEnd are the lower and the upper bounds of all the
  // vertex.y, respectively. The vertex.y is actually on the block-axis of
  // the float manager's writing mode.
  for (const nsPoint& vertex : mVertices) {
    mBStart = std::min(mBStart, vertex.y);
    mBEnd = std::max(mBEnd, vertex.y);
  }

  MOZ_ASSERT(mBStart <= mBEnd,
             "Start of float area should be less than "
             "or equal to the end.");
}

nscoord nsFloatManager::PolygonShapeInfo::ComputeLineIntercept(
    const nscoord aBStart, const nscoord aBEnd,
    nscoord (*aCompareOp)(std::initializer_list<nscoord>),
    const nscoord aLineInterceptInitialValue) const {
  MOZ_ASSERT(aBStart <= aBEnd,
             "The band's block start is greater than its block end?");

  const size_t len = mVertices.Length();
  nscoord lineIntercept = aLineInterceptInitialValue;

  // We have some special treatment of horizontal lines between vertices.
  // Generally, we can ignore the impact of the horizontal lines since their
  // endpoints will be included in the lines preceeding or following them.
  // However, it's possible the polygon is entirely a horizontal line,
  // possibly built from more than one horizontal segment. In such a case,
  // we need to have the horizontal line(s) contribute to the line intercepts.
  // We do this by accepting horizontal lines until we find a non-horizontal
  // line, after which all further horizontal lines are ignored.
  bool canIgnoreHorizontalLines = false;

  // Iterate each line segment {p0, p1}, {p1, p2}, ..., {pn, p0}.
  for (size_t i = 0; i < len; ++i) {
    const nsPoint* smallYVertex = &mVertices[i];
    const nsPoint* bigYVertex = &mVertices[(i + 1) % len];

    // Swap the two points to satisfy the requirement for calling
    // XInterceptAtY.
    if (smallYVertex->y > bigYVertex->y) {
      std::swap(smallYVertex, bigYVertex);
    }

    // Generally, we need to ignore line segments that either don't intersect
    // the band, or merely touch it. However, if the polygon has no block extent
    // (it is a point, or a horizontal line), and the band touches the line
    // segment, we let that line segment through.
    if ((aBStart >= bigYVertex->y || aBEnd <= smallYVertex->y) &&
        !(mBStart == mBEnd && aBStart == bigYVertex->y)) {
      // Skip computing the intercept if the band doesn't intersect the
      // line segment.
      continue;
    }

    nscoord bStartLineIntercept;
    nscoord bEndLineIntercept;

    if (smallYVertex->y == bigYVertex->y) {
      // The line is horizontal; see if we can ignore it.
      if (canIgnoreHorizontalLines) {
        continue;
      }

      // For a horizontal line that we can't ignore, we treat the two x value
      // ends as the bStartLineIntercept and bEndLineIntercept. It doesn't
      // matter which is applied to which, because they'll both be applied
      // to aCompareOp.
      bStartLineIntercept = smallYVertex->x;
      bEndLineIntercept = bigYVertex->x;
    } else {
      // This is not a horizontal line. We can now ignore all future
      // horizontal lines.
      canIgnoreHorizontalLines = true;

      bStartLineIntercept =
          aBStart <= smallYVertex->y
              ? smallYVertex->x
              : XInterceptAtY(aBStart, *smallYVertex, *bigYVertex);
      bEndLineIntercept =
          aBEnd >= bigYVertex->y
              ? bigYVertex->x
              : XInterceptAtY(aBEnd, *smallYVertex, *bigYVertex);
    }

    // If either new intercept is more extreme than lineIntercept (per
    // aCompareOp), then update lineIntercept to that value.
    lineIntercept =
        aCompareOp({lineIntercept, bStartLineIntercept, bEndLineIntercept});
  }

  return lineIntercept;
}

void nsFloatManager::PolygonShapeInfo::Translate(nscoord aLineLeft,
                                                 nscoord aBlockStart) {
  for (nsPoint& vertex : mVertices) {
    vertex.MoveBy(aLineLeft, aBlockStart);
  }
  for (nsRect& interval : mIntervals) {
    interval.MoveBy(aLineLeft, aBlockStart);
  }
  mBStart += aBlockStart;
  mBEnd += aBlockStart;
}

/* static */
nscoord nsFloatManager::PolygonShapeInfo::XInterceptAtY(const nscoord aY,
                                                        const nsPoint& aP1,
                                                        const nsPoint& aP2) {
  // Solve for x in the linear equation: x = x1 + (y-y1) * (x2-x1) / (y2-y1),
  // where aP1 = (x1, y1) and aP2 = (x2, y2).

  MOZ_ASSERT(aP1.y <= aY && aY <= aP2.y,
             "This function won't work if the horizontal line at aY and "
             "the line segment (aP1, aP2) do not intersect!");

  MOZ_ASSERT(aP1.y != aP2.y,
             "A horizontal line segment results in dividing by zero error!");

  return aP1.x + (aY - aP1.y) * (aP2.x - aP1.x) / (aP2.y - aP1.y);
}

/////////////////////////////////////////////////////////////////////////////
// ImageShapeInfo
//
// Implements shape-outside: <image>
//
class nsFloatManager::ImageShapeInfo final : public nsFloatManager::ShapeInfo {
 public:
  ImageShapeInfo(uint8_t* aAlphaPixels, int32_t aStride,
                 const LayoutDeviceIntSize& aImageSize,
                 int32_t aAppUnitsPerDevPixel, float aShapeImageThreshold,
                 nscoord aShapeMargin, const nsRect& aContentRect,
                 const nsRect& aMarginRect, WritingMode aWM,
                 const nsSize& aContainerSize);

  nscoord LineLeft(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord LineRight(const nscoord aBStart, const nscoord aBEnd) const override;
  nscoord BStart() const override { return mBStart; }
  nscoord BEnd() const override { return mBEnd; }
  bool IsEmpty() const override { return mIntervals.IsEmpty(); }
  bool MayNarrowInBlockDirection() const override { return true; }

  void Translate(nscoord aLineLeft, nscoord aBlockStart) override;

 private:
  // An interval is slice of the float area defined by this ImageShapeInfo.
  // Each interval is a rectangle that is one pixel deep in the block
  // axis. The values are stored as block edges in the y coordinates,
  // and inline edges as the x coordinates.

  // The intervals are stored in ascending order on y.
  nsTArray<nsRect> mIntervals;

  nscoord mBStart = nscoord_MAX;
  nscoord mBEnd = nscoord_MIN;

  // CreateInterval transforms the supplied aIMin and aIMax and aB
  // values into an interval that respects the writing mode. An
  // aOffsetFromContainer can be provided if the aIMin, aIMax, aB
  // values were generated relative to something other than the container
  // rect (such as the content rect or margin rect).
  void CreateInterval(int32_t aIMin, int32_t aIMax, int32_t aB,
                      int32_t aAppUnitsPerDevPixel,
                      const nsPoint& aOffsetFromContainer, WritingMode aWM,
                      const nsSize& aContainerSize);
};

nsFloatManager::ImageShapeInfo::ImageShapeInfo(
    uint8_t* aAlphaPixels, int32_t aStride,
    const LayoutDeviceIntSize& aImageSize, int32_t aAppUnitsPerDevPixel,
    float aShapeImageThreshold, nscoord aShapeMargin,
    const nsRect& aContentRect, const nsRect& aMarginRect, WritingMode aWM,
    const nsSize& aContainerSize) {
  MOZ_ASSERT(aShapeImageThreshold >= 0.0 && aShapeImageThreshold <= 1.0,
             "The computed value of shape-image-threshold is wrong!");

  const uint8_t threshold = NSToIntFloor(aShapeImageThreshold * 255);

  MOZ_ASSERT(aImageSize.width >= 0 && aImageSize.height >= 0,
             "Image size must be non-negative for our math to work.");
  const uint32_t w = aImageSize.width;
  const uint32_t h = aImageSize.height;

  if (aShapeMargin <= 0) {
    // Without a positive aShapeMargin, all we have to do is a
    // direct threshold comparison of the alpha pixels.
    // https://drafts.csswg.org/css-shapes-1/#valdef-shape-image-threshold-number

    // Scan the pixels in a double loop. For horizontal writing modes, we do
    // this row by row, from top to bottom. For vertical writing modes, we do
    // column by column, from left to right. We define the two loops
    // generically, then figure out the rows and cols within the inner loop.
    const uint32_t bSize = aWM.IsVertical() ? w : h;
    const uint32_t iSize = aWM.IsVertical() ? h : w;
    for (uint32_t b = 0; b < bSize; ++b) {
      // iMin and max store the start and end of the float area for the row
      // or column represented by this iteration of the outer loop.
      int32_t iMin = -1;
      int32_t iMax = -1;

      for (uint32_t i = 0; i < iSize; ++i) {
        const uint32_t col = aWM.IsVertical() ? b : i;
        const uint32_t row = aWM.IsVertical() ? i : b;
        const uint32_t index = col + row * aStride;

        // Determine if the alpha pixel at this row and column has a value
        // greater than the threshold. If it does, update our iMin and iMax
        // values to track the edges of the float area for this row or column.
        // https://drafts.csswg.org/css-shapes-1/#valdef-shape-image-threshold-number
        const uint8_t alpha = aAlphaPixels[index];
        if (alpha > threshold) {
          if (iMin == -1) {
            iMin = i;
          }
          MOZ_ASSERT(iMax < (int32_t)i);
          iMax = i;
        }
      }

      // At the end of a row or column; did we find something?
      if (iMin != -1) {
        // We need to supply an offset of the content rect top left, since
        // our col and row have been calculated from the content rect,
        // instead of the margin rect (against which floats are applied).
        CreateInterval(iMin, iMax, b, aAppUnitsPerDevPixel,
                       aContentRect.TopLeft(), aWM, aContainerSize);
      }
    }

    if (aWM.IsVerticalRL()) {
      // vertical-rl or sideways-rl.
      // Because we scan the columns from left to right, we need to reverse
      // the array so that it's sorted (in ascending order) on the block
      // direction.
      mIntervals.Reverse();
    }
  } else {
    // With a positive aShapeMargin, we have to calculate a distance
    // field from the opaque pixels, then build intervals based on
    // them being within aShapeMargin distance to an opaque pixel.

    // Roughly: for each pixel in the margin box, we need to determine the
    // distance to the nearest opaque image-pixel.  If that distance is less
    // than aShapeMargin, we consider this margin-box pixel as being part of
    // the float area.

    // Computing the distance field is a two-pass O(n) operation.
    // We use a chamfer 5-7-11 5x5 matrix to compute minimum distance
    // to an opaque pixel. This integer math computation is reasonably
    // close to the true Euclidean distance. The distances will be
    // approximately 5x the true distance, quantized in integer units.
    // The 5x is factored away in the comparison used in the final
    // pass which builds the intervals.
    dfType usedMargin5X =
        CalcUsedShapeMargin5X(aShapeMargin, aAppUnitsPerDevPixel);

    // Allocate our distance field.  The distance field has to cover
    // the entire aMarginRect, since aShapeMargin could bleed into it,
    // beyond the content rect covered by aAlphaPixels. To make this work,
    // we calculate a dfOffset value which is the top left of the content
    // rect relative to the margin rect.
    nsPoint offsetPoint = aContentRect.TopLeft() - aMarginRect.TopLeft();
    LayoutDeviceIntPoint dfOffset = LayoutDevicePixel::FromAppUnitsRounded(
        offsetPoint, aAppUnitsPerDevPixel);

    // Since our distance field is computed with a 5x5 neighborhood,
    // we need to expand our distance field by a further 4 pixels in
    // both axes, 2 on the leading edge and 2 on the trailing edge.
    // We call this edge area the "expanded region".

    // Our expansion amounts need to be the same for our math to work.
    static uint32_t kExpansionPerSide = 2;
    // Since dfOffset will be used in comparisons against expanded region
    // pixel values, it's convenient to add expansion amounts to dfOffset in
    // both axes, to simplify comparison math later.
    dfOffset.x += kExpansionPerSide;
    dfOffset.y += kExpansionPerSide;

    // In all these calculations, we purposely ignore aStride, because
    // we don't have to replicate the packing that we received in
    // aAlphaPixels. When we need to convert from df coordinates to
    // alpha coordinates, we do that with math based on row and col.
    const LayoutDeviceIntSize marginRectDevPixels =
        LayoutDevicePixel::FromAppUnitsRounded(aMarginRect.Size(),
                                               aAppUnitsPerDevPixel);

    // Clamp the size of our distance field sizes to prevent multiplication
    // overflow.
    static const uint32_t DF_SIDE_MAX =
        floor(sqrt((double)(std::numeric_limits<int32_t>::max())));

    // Clamp the margin plus 2X the expansion values between expansion + 1
    // and DF_SIDE_MAX. This ensures that the distance field allocation
    // doesn't overflow during multiplication, and the reverse iteration
    // doesn't underflow.
    const uint32_t wEx =
        std::max(std::min(marginRectDevPixels.width + (kExpansionPerSide * 2),
                          DF_SIDE_MAX),
                 kExpansionPerSide + 1);
    const uint32_t hEx =
        std::max(std::min(marginRectDevPixels.height + (kExpansionPerSide * 2),
                          DF_SIDE_MAX),
                 kExpansionPerSide + 1);

    // Since the margin-box size is CSS controlled, and large values will
    // generate large wEx and hEx values, we do a falliable allocation for
    // the distance field. If allocation fails, we early exit and layout will
    // be wrong, but we'll avoid aborting from OOM.
    auto df = MakeUniqueFallible<dfType[]>(wEx * hEx);
    if (!df) {
      // Without a distance field, we can't reason about the float area.
      return;
    }

    const uint32_t bSize = aWM.IsVertical() ? wEx : hEx;
    const uint32_t iSize = aWM.IsVertical() ? hEx : wEx;

    // First pass setting distance field, starting at top-left, three cases:
    // 1) Expanded region pixel: set to MAX_MARGIN_5X.
    // 2) Image pixel with alpha greater than threshold: set to 0.
    // 3) Other pixel: set to minimum backward-looking neighborhood
    //                 distance value, computed with 5-7-11 chamfer.

    // Scan the pixels in a double loop. For horizontal writing modes, we do
    // this row by row, from top to bottom. For vertical writing modes, we do
    // column by column, from left to right. We define the two loops
    // generically, then figure out the rows and cols within the inner loop.
    for (uint32_t b = 0; b < bSize; ++b) {
      for (uint32_t i = 0; i < iSize; ++i) {
        const uint32_t col = aWM.IsVertical() ? b : i;
        const uint32_t row = aWM.IsVertical() ? i : b;
        const uint32_t index = col + row * wEx;
        MOZ_ASSERT(index < (wEx * hEx),
                   "Our distance field index should be in-bounds.");

        // Handle our three cases, in order.
        if (col < kExpansionPerSide || col >= wEx - kExpansionPerSide ||
            row < kExpansionPerSide || row >= hEx - kExpansionPerSide) {
          // Case 1: Expanded pixel.
          df[index] = MAX_MARGIN_5X;
        } else if ((int32_t)col >= dfOffset.x &&
                   (int32_t)col < (dfOffset.x + aImageSize.width) &&
                   (int32_t)row >= dfOffset.y &&
                   (int32_t)row < (dfOffset.y + aImageSize.height) &&
                   aAlphaPixels[col - dfOffset.x.value +
                                (row - dfOffset.y.value) * aStride] >
                       threshold) {
          // Case 2: Image pixel that is opaque.
          DebugOnly<uint32_t> alphaIndex =
              col - dfOffset.x.value + (row - dfOffset.y.value) * aStride;
          MOZ_ASSERT(alphaIndex < (aStride * h),
                     "Our aAlphaPixels index should be in-bounds.");

          df[index] = 0;
        } else {
          // Case 3: Other pixel.
          if (aWM.IsVertical()) {
            // Column-by-column, starting at the left, each column
            // top-to-bottom.
            // Backward-looking neighborhood distance from target pixel X
            // with chamfer 5-7-11 looks like:
            //
            // +--+--+--+
            // |  |11|  |   |    +
            // +--+--+--+   |   /|
            // |11| 7| 5|   |  / |
            // +--+--+--+   | /  V
            // |  | 5| X|   |/
            // +--+--+--+   +
            // |11| 7|  |
            // +--+--+--+
            // |  |11|  |
            // +--+--+--+
            //
            // X should be set to the minimum of MAX_MARGIN_5X and the
            // values of all of the numbered neighbors summed with the
            // value in that chamfer cell.
            MOZ_ASSERT(index - wEx - 2 < (iSize * bSize) &&
                           index + wEx - 2 < (iSize * bSize) &&
                           index - (wEx * 2) - 1 < (iSize * bSize),
                       "Our distance field most extreme indices should be "
                       "in-bounds.");

            // clang-format off
            df[index] = std::min<dfType>(MAX_MARGIN_5X,
                        std::min<dfType>(df[index - wEx - 2] + 11,
                        std::min<dfType>(df[index + wEx - 2] + 11,
                        std::min<dfType>(df[index - (wEx * 2) - 1] + 11,
                        std::min<dfType>(df[index - wEx - 1] + 7,
                        std::min<dfType>(df[index - 1] + 5,
                        std::min<dfType>(df[index + wEx - 1] + 7,
                        std::min<dfType>(df[index + (wEx * 2) - 1] + 11,
                                         df[index - wEx] + 5))))))));
            // clang-format on
          } else {
            // Row-by-row, starting at the top, each row left-to-right.
            // Backward-looking neighborhood distance from target pixel X
            // with chamfer 5-7-11 looks like:
            //
            // +--+--+--+--+--+
            // |  |11|  |11|  |   ----+
            // +--+--+--+--+--+      /
            // |11| 7| 5| 7|11|     /
            // +--+--+--+--+--+    /
            // |  | 5| X|  |  |   +-->
            // +--+--+--+--+--+
            //
            // X should be set to the minimum of MAX_MARGIN_5X and the
            // values of all of the numbered neighbors summed with the
            // value in that chamfer cell.
            MOZ_ASSERT(index - (wEx * 2) - 1 < (iSize * bSize) &&
                           index - wEx - 2 < (iSize * bSize),
                       "Our distance field most extreme indices should be "
                       "in-bounds.");

            // clang-format off
            df[index] = std::min<dfType>(MAX_MARGIN_5X,
                        std::min<dfType>(df[index - (wEx * 2) - 1] + 11,
                        std::min<dfType>(df[index - (wEx * 2) + 1] + 11,
                        std::min<dfType>(df[index - wEx - 2] + 11,
                        std::min<dfType>(df[index - wEx - 1] + 7,
                        std::min<dfType>(df[index - wEx] + 5,
                        std::min<dfType>(df[index - wEx + 1] + 7,
                        std::min<dfType>(df[index - wEx + 2] + 11,
                                         df[index - 1] + 5))))))));
            // clang-format on
          }
        }
      }
    }

    // Okay, time for the second pass. This pass is in reverse order from
    // the first pass. All of our opaque pixels have been set to 0, and all
    // of our expanded region pixels have been set to MAX_MARGIN_5X. Other
    // pixels have been set to some value between those two (inclusive) but
    // this hasn't yet taken into account the neighbors that were processed
    // after them in the first pass. This time we reverse iterate so we can
    // apply the forward-looking chamfer.

    // This time, we constrain our outer and inner loop to ignore the
    // expanded region pixels. For each pixel we iterate, we set the df value
    // to the minimum forward-looking neighborhood distance value, computed
    // with a 5-7-11 chamfer. We also check each df value against the
    // usedMargin5X threshold, and use that to set the iMin and iMax values
    // for the interval we'll create for that block axis value (b).

    // At the end of each row (or column in vertical writing modes),
    // if any of the other pixels had a value less than usedMargin5X,
    // we create an interval. Note: "bSize - kExpansionPerSide - 1" is the
    // index of the final row of pixels before the trailing expanded region.
    for (uint32_t b = bSize - kExpansionPerSide - 1; b >= kExpansionPerSide;
         --b) {
      // iMin tracks the first df pixel and iMax the last df pixel whose
      // df[] value is less than usedMargin5X. Set iMin and iMax in
      // preparation for this row or column.
      int32_t iMin = iSize;
      int32_t iMax = -1;

      // Note: "iSize - kExpansionPerSide - 1" is the index of the final row
      // of pixels before the trailing expanded region.
      for (uint32_t i = iSize - kExpansionPerSide - 1; i >= kExpansionPerSide;
           --i) {
        const uint32_t col = aWM.IsVertical() ? b : i;
        const uint32_t row = aWM.IsVertical() ? i : b;
        const uint32_t index = col + row * wEx;
        MOZ_ASSERT(index < (wEx * hEx),
                   "Our distance field index should be in-bounds.");

        // Only apply the chamfer calculation if the df value is not
        // already 0, since the chamfer can only reduce the value.
        if (df[index]) {
          if (aWM.IsVertical()) {
            // Column-by-column, starting at the right, each column
            // bottom-to-top.
            // Forward-looking neighborhood distance from target pixel X
            // with chamfer 5-7-11 looks like:
            //
            // +--+--+--+
            // |  |11|  |        +
            // +--+--+--+       /|
            // |  | 7|11|   A  / |
            // +--+--+--+   | /  |
            // | X| 5|  |   |/   |
            // +--+--+--+   +    |
            // | 5| 7|11|
            // +--+--+--+
            // |  |11|  |
            // +--+--+--+
            //
            // X should be set to the minimum of its current value and
            // the values of all of the numbered neighbors summed with
            // the value in that chamfer cell.
            MOZ_ASSERT(index + wEx + 2 < (wEx * hEx) &&
                           index + (wEx * 2) + 1 < (wEx * hEx) &&
                           index - (wEx * 2) + 1 < (wEx * hEx),
                       "Our distance field most extreme indices should be "
                       "in-bounds.");

            // clang-format off
            df[index] = std::min<dfType>(df[index],
                        std::min<dfType>(df[index + wEx + 2] + 11,
                        std::min<dfType>(df[index - wEx + 2] + 11,
                        std::min<dfType>(df[index + (wEx * 2) + 1] + 11,
                        std::min<dfType>(df[index + wEx + 1] + 7,
                        std::min<dfType>(df[index + 1] + 5,
                        std::min<dfType>(df[index - wEx + 1] + 7,
                        std::min<dfType>(df[index - (wEx * 2) + 1] + 11,
                                         df[index + wEx] + 5))))))));
            // clang-format on
          } else {
            // Row-by-row, starting at the bottom, each row right-to-left.
            // Forward-looking neighborhood distance from target pixel X
            // with chamfer 5-7-11 looks like:
            //
            // +--+--+--+--+--+
            // |  |  | X| 5|  |    <--+
            // +--+--+--+--+--+      /
            // |11| 7| 5| 7|11|     /
            // +--+--+--+--+--+    /
            // |  |11|  |11|  |   +----
            // +--+--+--+--+--+
            //
            // X should be set to the minimum of its current value and
            // the values of all of the numbered neighbors summed with
            // the value in that chamfer cell.
            MOZ_ASSERT(index + (wEx * 2) + 1 < (wEx * hEx) &&
                           index + wEx + 2 < (wEx * hEx),
                       "Our distance field most extreme indices should be "
                       "in-bounds.");

            // clang-format off
            df[index] = std::min<dfType>(df[index],
                        std::min<dfType>(df[index + (wEx * 2) + 1] + 11,
                        std::min<dfType>(df[index + (wEx * 2) - 1] + 11,
                        std::min<dfType>(df[index + wEx + 2] + 11,
                        std::min<dfType>(df[index + wEx + 1] + 7,
                        std::min<dfType>(df[index + wEx] + 5,
                        std::min<dfType>(df[index + wEx - 1] + 7,
                        std::min<dfType>(df[index + wEx - 2] + 11,
                                         df[index + 1] + 5))))))));
            // clang-format on
          }
        }

        // Finally, we can check the df value and see if it's less than
        // or equal to the usedMargin5X value.
        if (df[index] <= usedMargin5X) {
          if (iMax == -1) {
            iMax = i;
          }
          MOZ_ASSERT(iMin > (int32_t)i);
          iMin = i;
        }
      }

      if (iMax != -1) {
        // Our interval values, iMin, iMax, and b are all calculated from
        // the expanded region, which is based on the margin rect. To create
        // our interval, we have to subtract kExpansionPerSide from (iMin,
        // iMax, and b) to account for the expanded region edges. This
        // produces coords that are relative to our margin-rect, so we pass
        // in aMarginRect.TopLeft() to make CreateInterval convert to our
        // container's coordinate space.
        CreateInterval(iMin - kExpansionPerSide, iMax - kExpansionPerSide,
                       b - kExpansionPerSide, aAppUnitsPerDevPixel,
                       aMarginRect.TopLeft(), aWM, aContainerSize);
      }
    }

    if (!aWM.IsVerticalRL()) {
      // Anything other than vertical-rl or sideways-rl.
      // Because we assembled our intervals on the bottom-up pass,
      // they are reversed for most writing modes. Reverse them to
      // keep the array sorted on the block direction.
      mIntervals.Reverse();
    }
  }

  if (!mIntervals.IsEmpty()) {
    mBStart = mIntervals[0].Y();
    mBEnd = mIntervals.LastElement().YMost();
  }
}

void nsFloatManager::ImageShapeInfo::CreateInterval(
    int32_t aIMin, int32_t aIMax, int32_t aB, int32_t aAppUnitsPerDevPixel,
    const nsPoint& aOffsetFromContainer, WritingMode aWM,
    const nsSize& aContainerSize) {
  // Store an interval as an nsRect with our inline axis values stored in x
  // and our block axis values stored in y. The position is dependent on
  // the writing mode, but the size is the same for all writing modes.

  // Size is the difference in inline axis edges stored as x, and one
  // block axis pixel stored as y. For the inline axis, we add 1 to aIMax
  // because we want to capture the far edge of the last pixel.
  nsSize size(((aIMax + 1) - aIMin) * aAppUnitsPerDevPixel,
              aAppUnitsPerDevPixel);

  // Since we started our scanning of the image pixels from the top left,
  // the interval position starts from the origin of the content rect,
  // converted to logical coordinates.
  nsPoint origin =
      ConvertToFloatLogical(aOffsetFromContainer, aWM, aContainerSize);

  // Depending on the writing mode, we now move the origin.
  if (aWM.IsVerticalRL()) {
    // vertical-rl or sideways-rl.
    // These writing modes proceed from the top right, and each interval
    // moves in a positive inline direction and negative block direction.
    // That means that the intervals will be reversed after all have been
    // constructed. We add 1 to aB to capture the end of the block axis pixel.
    origin.MoveBy(aIMin * aAppUnitsPerDevPixel,
                  (aB + 1) * -aAppUnitsPerDevPixel);
  } else if (aWM.IsSidewaysLR()) {
    // This writing mode proceeds from the bottom left, and each interval
    // moves in a negative inline direction and a positive block direction.
    // We add 1 to aIMax to capture the end of the inline axis pixel.
    origin.MoveBy((aIMax + 1) * -aAppUnitsPerDevPixel,
                  aB * aAppUnitsPerDevPixel);
  } else {
    // horizontal-tb or vertical-lr.
    // These writing modes proceed from the top left and each interval
    // moves in a positive step in both inline and block directions.
    origin.MoveBy(aIMin * aAppUnitsPerDevPixel, aB * aAppUnitsPerDevPixel);
  }

  mIntervals.AppendElement(nsRect(origin, size));
}

nscoord nsFloatManager::ImageShapeInfo::LineLeft(const nscoord aBStart,
                                                 const nscoord aBEnd) const {
  return LineEdge(mIntervals, aBStart, aBEnd, true);
}

nscoord nsFloatManager::ImageShapeInfo::LineRight(const nscoord aBStart,
                                                  const nscoord aBEnd) const {
  return LineEdge(mIntervals, aBStart, aBEnd, false);
}

void nsFloatManager::ImageShapeInfo::Translate(nscoord aLineLeft,
                                               nscoord aBlockStart) {
  for (nsRect& interval : mIntervals) {
    interval.MoveBy(aLineLeft, aBlockStart);
  }

  mBStart += aBlockStart;
  mBEnd += aBlockStart;
}

/////////////////////////////////////////////////////////////////////////////
// FloatInfo

nsFloatManager::FloatInfo::FloatInfo(nsIFrame* aFrame, nscoord aLineLeft,
                                     nscoord aBlockStart,
                                     const LogicalRect& aMarginRect,
                                     WritingMode aWM,
                                     const nsSize& aContainerSize)
    : mFrame(aFrame),
      mLeftBEnd(nscoord_MIN),
      mRightBEnd(nscoord_MIN),
      mRect(ShapeInfo::ConvertToFloatLogical(aMarginRect, aWM, aContainerSize) +
            nsPoint(aLineLeft, aBlockStart)) {
  MOZ_COUNT_CTOR(nsFloatManager::FloatInfo);
  using ShapeOutsideType = StyleShapeOutside::Tag;

  if (IsEmpty()) {
    // Per spec, a float area defined by a shape is clipped to the float’s
    // margin box. Therefore, no need to create a shape info if the float's
    // margin box is empty, since a float area can only be smaller than the
    // margin box.

    // https://drafts.csswg.org/css-shapes/#relation-to-box-model-and-float-behavior
    return;
  }

  const nsStyleDisplay* styleDisplay = mFrame->StyleDisplay();
  const auto& shapeOutside = styleDisplay->mShapeOutside;

  nscoord shapeMargin = shapeOutside.IsNone()
                            ? 0
                            : nsLayoutUtils::ResolveToLength<true>(
                                  styleDisplay->mShapeMargin,
                                  LogicalSize(aWM, aContainerSize).ISize(aWM));

  switch (shapeOutside.tag) {
    case ShapeOutsideType::None:
      // No need to create shape info.
      return;

    case ShapeOutsideType::Image: {
      float shapeImageThreshold = styleDisplay->mShapeImageThreshold;
      mShapeInfo = ShapeInfo::CreateImageShape(
          shapeOutside.AsImage(), shapeImageThreshold, shapeMargin, mFrame,
          aMarginRect, aWM, aContainerSize);
      if (!mShapeInfo) {
        // Image is not ready, or fails to load, etc.
        return;
      }

      break;
    }

    case ShapeOutsideType::Box: {
      // Initialize <shape-box>'s reference rect.
      LogicalRect shapeBoxRect = ShapeInfo::ComputeShapeBoxRect(
          shapeOutside.AsBox(), mFrame, aMarginRect, aWM);
      mShapeInfo = ShapeInfo::CreateShapeBox(mFrame, shapeMargin, shapeBoxRect,
                                             aWM, aContainerSize);
      break;
    }

    case ShapeOutsideType::Shape: {
      const auto& shape = *shapeOutside.AsShape()._0;
      // Initialize <shape-box>'s reference rect.
      LogicalRect shapeBoxRect = ShapeInfo::ComputeShapeBoxRect(
          shapeOutside.AsShape()._1, mFrame, aMarginRect, aWM);
      mShapeInfo =
          ShapeInfo::CreateBasicShape(shape, shapeMargin, mFrame, shapeBoxRect,
                                      aMarginRect, aWM, aContainerSize);
      break;
    }
  }

  MOZ_ASSERT(mShapeInfo,
             "All shape-outside values except none should have mShapeInfo!");

  // Translate the shape to the same origin as nsFloatManager.
  mShapeInfo->Translate(aLineLeft, aBlockStart);
}

#ifdef NS_BUILD_REFCNT_LOGGING
nsFloatManager::FloatInfo::FloatInfo(FloatInfo&& aOther)
    : mFrame(std::move(aOther.mFrame)),
      mLeftBEnd(std::move(aOther.mLeftBEnd)),
      mRightBEnd(std::move(aOther.mRightBEnd)),
      mRect(std::move(aOther.mRect)),
      mShapeInfo(std::move(aOther.mShapeInfo)) {
  MOZ_COUNT_CTOR(nsFloatManager::FloatInfo);
}

nsFloatManager::FloatInfo::~FloatInfo() {
  MOZ_COUNT_DTOR(nsFloatManager::FloatInfo);
}
#endif

nscoord nsFloatManager::FloatInfo::LineLeft(ShapeType aShapeType,
                                            const nscoord aBStart,
                                            const nscoord aBEnd) const {
  if (aShapeType == ShapeType::Margin) {
    return LineLeft();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return LineLeft();
  }
  // Clip the flow area to the margin-box because
  // https://drafts.csswg.org/css-shapes-1/#relation-to-box-model-and-float-behavior
  // says "When a shape is used to define a float area, the shape is clipped
  // to the float’s margin box."
  return std::max(LineLeft(), mShapeInfo->LineLeft(aBStart, aBEnd));
}

nscoord nsFloatManager::FloatInfo::LineRight(ShapeType aShapeType,
                                             const nscoord aBStart,
                                             const nscoord aBEnd) const {
  if (aShapeType == ShapeType::Margin) {
    return LineRight();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return LineRight();
  }
  // Clip the flow area to the margin-box. See LineLeft().
  return std::min(LineRight(), mShapeInfo->LineRight(aBStart, aBEnd));
}

nscoord nsFloatManager::FloatInfo::BStart(ShapeType aShapeType) const {
  if (aShapeType == ShapeType::Margin) {
    return BStart();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return BStart();
  }
  // Clip the flow area to the margin-box. See LineLeft().
  return std::max(BStart(), mShapeInfo->BStart());
}

nscoord nsFloatManager::FloatInfo::BEnd(ShapeType aShapeType) const {
  if (aShapeType == ShapeType::Margin) {
    return BEnd();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return BEnd();
  }
  // Clip the flow area to the margin-box. See LineLeft().
  return std::min(BEnd(), mShapeInfo->BEnd());
}

bool nsFloatManager::FloatInfo::IsEmpty(ShapeType aShapeType) const {
  if (aShapeType == ShapeType::Margin) {
    return IsEmpty();
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return IsEmpty();
  }
  return mShapeInfo->IsEmpty();
}

bool nsFloatManager::FloatInfo::MayNarrowInBlockDirection(
    ShapeType aShapeType) const {
  // This function mirrors the cases of the three argument versions of
  // LineLeft() and LineRight(). This function returns true if and only if
  // either of those functions could possibly return "narrower" values with
  // increasing aBStart values. "Narrower" means closer to the far end of
  // the float shape.
  if (aShapeType == ShapeType::Margin) {
    return false;
  }

  MOZ_ASSERT(aShapeType == ShapeType::ShapeOutside);
  if (!mShapeInfo) {
    return false;
  }

  return mShapeInfo->MayNarrowInBlockDirection();
}

/////////////////////////////////////////////////////////////////////////////
// ShapeInfo

/* static */
LogicalRect nsFloatManager::ShapeInfo::ComputeShapeBoxRect(
    StyleShapeBox aBox, nsIFrame* const aFrame, const LogicalRect& aMarginRect,
    WritingMode aWM) {
  LogicalRect rect = aMarginRect;

  switch (aBox) {
    case StyleShapeBox::ContentBox:
      rect.Deflate(aWM, aFrame->GetLogicalUsedPadding(aWM));
      [[fallthrough]];
    case StyleShapeBox::PaddingBox:
      rect.Deflate(aWM, aFrame->GetLogicalUsedBorder(aWM));
      [[fallthrough]];
    case StyleShapeBox::BorderBox:
      rect.Deflate(aWM, aFrame->GetLogicalUsedMargin(aWM));
      break;
    case StyleShapeBox::MarginBox:
      // Do nothing. rect is already a margin rect.
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown shape box");
      break;
  }

  return rect;
}

/* static */ UniquePtr<nsFloatManager::ShapeInfo>
nsFloatManager::ShapeInfo::CreateShapeBox(nsIFrame* const aFrame,
                                          nscoord aShapeMargin,
                                          const LogicalRect& aShapeBoxRect,
                                          WritingMode aWM,
                                          const nsSize& aContainerSize) {
  nsRect logicalShapeBoxRect =
      ConvertToFloatLogical(aShapeBoxRect, aWM, aContainerSize);

  // Inflate logicalShapeBoxRect by aShapeMargin.
  logicalShapeBoxRect.Inflate(aShapeMargin);

  nsRectCornerRadii physicalRadii;
  bool hasRadii = aFrame->GetShapeBoxBorderRadii(physicalRadii);
  if (!hasRadii) {
    return MakeUnique<RoundedBoxShapeInfo>(logicalShapeBoxRect,
                                           std::move(physicalRadii));
  }

  // Add aShapeMargin to each of the radii.
  for (auto corner : AllPhysicalCorners()) {
    physicalRadii[corner] += nsSize(aShapeMargin, aShapeMargin);
  }

  return MakeUnique<RoundedBoxShapeInfo>(
      logicalShapeBoxRect, ConvertToFloatLogical(physicalRadii, aWM));
}

/* static */ UniquePtr<nsFloatManager::ShapeInfo>
nsFloatManager::ShapeInfo::CreateBasicShape(const StyleBasicShape& aBasicShape,
                                            nscoord aShapeMargin,
                                            nsIFrame* const aFrame,
                                            const LogicalRect& aShapeBoxRect,
                                            const LogicalRect& aMarginRect,
                                            WritingMode aWM,
                                            const nsSize& aContainerSize) {
  switch (aBasicShape.tag) {
    case StyleBasicShape::Tag::Polygon:
      return CreatePolygon(aBasicShape, aShapeMargin, aFrame, aShapeBoxRect,
                           aMarginRect, aWM, aContainerSize);
    case StyleBasicShape::Tag::Circle:
    case StyleBasicShape::Tag::Ellipse:
      return CreateCircleOrEllipse(aBasicShape, aShapeMargin, aFrame,
                                   aShapeBoxRect, aWM, aContainerSize);
    case StyleBasicShape::Tag::Rect:
      return CreateInset(aBasicShape, aShapeMargin, aFrame, aShapeBoxRect, aWM,
                         aContainerSize);
    case StyleBasicShape::Tag::PathOrShape:
      MOZ_ASSERT_UNREACHABLE("Unsupported basic shape");
  }
  return nullptr;
}

/* static */ UniquePtr<nsFloatManager::ShapeInfo>
nsFloatManager::ShapeInfo::CreateInset(const StyleBasicShape& aBasicShape,
                                       nscoord aShapeMargin, nsIFrame* aFrame,
                                       const LogicalRect& aShapeBoxRect,
                                       WritingMode aWM,
                                       const nsSize& aContainerSize) {
  // Use physical coordinates to compute inset() because the top, right,
  // bottom and left offsets are physical.
  // https://drafts.csswg.org/css-shapes-1/#funcdef-inset
  nsRect physicalShapeBoxRect =
      aShapeBoxRect.GetPhysicalRect(aWM, aContainerSize);
  const nsRect insetRect = ShapeUtils::ComputeInsetRect(
      aBasicShape.AsRect().rect, physicalShapeBoxRect);

  nsRect logicalInsetRect = ConvertToFloatLogical(
      LogicalRect(aWM, insetRect, aContainerSize), aWM, aContainerSize);
  nsRectCornerRadii physicalRadii;
  bool hasRadii = ShapeUtils::ComputeRectRadii(aBasicShape.AsRect().round,
                                               physicalShapeBoxRect, insetRect,
                                               physicalRadii);

  // With a zero shape-margin, we will be able to use the fast constructor.
  if (aShapeMargin == 0) {
    if (!hasRadii) {
      return MakeUnique<RoundedBoxShapeInfo>(logicalInsetRect,
                                             std::move(physicalRadii));
    }
    return MakeUnique<RoundedBoxShapeInfo>(
        logicalInsetRect, ConvertToFloatLogical(physicalRadii, aWM));
  }

  // With a positive shape-margin, we might still be able to use the fast
  // constructor. With no radii, we can build a rounded box by inflating
  // logicalInsetRect, and supplying aShapeMargin as the radius for all
  // corners.
  if (!hasRadii) {
    logicalInsetRect.Inflate(aShapeMargin);
    return MakeUnique<RoundedBoxShapeInfo>(logicalInsetRect,
                                           nsRectCornerRadii(aShapeMargin));
  }

  // If we have radii, and they have balanced/equal corners, we can inflate
  // both logicalInsetRect and all the radii and use the fast constructor.
  if (RoundedBoxShapeInfo::EachCornerHasBalancedRadii(physicalRadii)) {
    logicalInsetRect.Inflate(aShapeMargin);
    for (auto corner : AllPhysicalCorners()) {
      physicalRadii[corner] += nsSize(aShapeMargin, aShapeMargin);
    }
    return MakeUnique<RoundedBoxShapeInfo>(
        logicalInsetRect, ConvertToFloatLogical(physicalRadii, aWM));
  }

  // With positive shape-margin and elliptical radii, we have to use the
  // slow constructor.
  nsDeviceContext* dc = aFrame->PresContext()->DeviceContext();
  int32_t appUnitsPerDevPixel = dc->AppUnitsPerDevPixel();
  return MakeUnique<RoundedBoxShapeInfo>(
      logicalInsetRect, ConvertToFloatLogical(physicalRadii, aWM), aShapeMargin,
      appUnitsPerDevPixel);
}

/* static */ UniquePtr<nsFloatManager::ShapeInfo>
nsFloatManager::ShapeInfo::CreateCircleOrEllipse(
    const StyleBasicShape& aBasicShape, nscoord aShapeMargin,
    nsIFrame* const aFrame, const LogicalRect& aShapeBoxRect, WritingMode aWM,
    const nsSize& aContainerSize) {
  // Use physical coordinates to compute the center of circle() or ellipse()
  // since the <position> keywords such as 'left', 'top', etc. are physical.
  // https://drafts.csswg.org/css-shapes-1/#funcdef-ellipse
  nsRect physicalShapeBoxRect =
      aShapeBoxRect.GetPhysicalRect(aWM, aContainerSize);
  nsPoint physicalCenter = ShapeUtils::ComputeCircleOrEllipseCenter(
      aBasicShape, physicalShapeBoxRect);
  nsPoint logicalCenter =
      ConvertToFloatLogical(physicalCenter, aWM, aContainerSize);

  // Compute the circle or ellipse radii.
  nsSize radii;
  if (aBasicShape.IsCircle()) {
    nscoord radius = ShapeUtils::ComputeCircleRadius(
        aBasicShape, physicalCenter, physicalShapeBoxRect);
    // Circles can use the three argument, math constructor for
    // EllipseShapeInfo.
    radii = nsSize(radius, radius);
    return MakeUnique<EllipseShapeInfo>(logicalCenter, radii, aShapeMargin);
  }

  MOZ_ASSERT(aBasicShape.IsEllipse());
  nsSize physicalRadii = ShapeUtils::ComputeEllipseRadii(
      aBasicShape, physicalCenter, physicalShapeBoxRect);
  LogicalSize logicalRadii(aWM, physicalRadii);
  radii = nsSize(logicalRadii.ISize(aWM), logicalRadii.BSize(aWM));

  // If radii are close to the same value, or if aShapeMargin is small
  // enough (as specified in css pixels), then we can use the three argument
  // constructor for EllipseShapeInfo, which uses math for a more efficient
  // method of float area computation.
  if (EllipseShapeInfo::ShapeMarginIsNegligible(aShapeMargin) ||
      EllipseShapeInfo::RadiiAreRoughlyEqual(radii)) {
    return MakeUnique<EllipseShapeInfo>(logicalCenter, radii, aShapeMargin);
  }

  // We have to use the full constructor for EllipseShapeInfo. This
  // computes the float area using a rasterization method.
  nsDeviceContext* dc = aFrame->PresContext()->DeviceContext();
  int32_t appUnitsPerDevPixel = dc->AppUnitsPerDevPixel();
  return MakeUnique<EllipseShapeInfo>(logicalCenter, radii, aShapeMargin,
                                      appUnitsPerDevPixel);
}

/* static */ UniquePtr<nsFloatManager::ShapeInfo>
nsFloatManager::ShapeInfo::CreatePolygon(const StyleBasicShape& aBasicShape,
                                         nscoord aShapeMargin,
                                         nsIFrame* const aFrame,
                                         const LogicalRect& aShapeBoxRect,
                                         const LogicalRect& aMarginRect,
                                         WritingMode aWM,
                                         const nsSize& aContainerSize) {
  // Use physical coordinates to compute each (xi, yi) vertex because CSS
  // represents them using physical coordinates.
  // https://drafts.csswg.org/css-shapes-1/#funcdef-polygon
  nsRect physicalShapeBoxRect =
      aShapeBoxRect.GetPhysicalRect(aWM, aContainerSize);

  // Get physical vertices.
  nsTArray<nsPoint> vertices =
      ShapeUtils::ComputePolygonVertices(aBasicShape, physicalShapeBoxRect);

  // Convert all the physical vertices to logical.
  for (nsPoint& vertex : vertices) {
    vertex = ConvertToFloatLogical(vertex, aWM, aContainerSize);
  }

  if (aShapeMargin == 0) {
    return MakeUnique<PolygonShapeInfo>(std::move(vertices));
  }

  nsRect marginRect = ConvertToFloatLogical(aMarginRect, aWM, aContainerSize);

  // We have to use the full constructor for PolygonShapeInfo. This
  // computes the float area using a rasterization method.
  int32_t appUnitsPerDevPixel = aFrame->PresContext()->AppUnitsPerDevPixel();
  return MakeUnique<PolygonShapeInfo>(std::move(vertices), aShapeMargin,
                                      appUnitsPerDevPixel, marginRect);
}

/* static */ UniquePtr<nsFloatManager::ShapeInfo>
nsFloatManager::ShapeInfo::CreateImageShape(const StyleImage& aShapeImage,
                                            float aShapeImageThreshold,
                                            nscoord aShapeMargin,
                                            nsIFrame* const aFrame,
                                            const LogicalRect& aMarginRect,
                                            WritingMode aWM,
                                            const nsSize& aContainerSize) {
  MOZ_ASSERT(&aShapeImage == &aFrame->StyleDisplay()->mShapeOutside.AsImage(),
             "aFrame should be the frame that we got aShapeImage from");

  nsImageRenderer imageRenderer(aFrame, &aShapeImage,
                                nsImageRenderer::FLAG_SYNC_DECODE_IMAGES);

  if (!imageRenderer.PrepareImage()) {
    // The image is not ready yet.  Boost its loading priority since it will
    // affect layout.
    if (imgRequestProxy* req = aShapeImage.GetImageRequest()) {
      req->BoostPriority(imgIRequest::CATEGORY_SIZE_QUERY);
    }
    return nullptr;
  }

  nsRect contentRect = aFrame->GetContentRect();

  // Create a draw target and draw shape image on it.
  nsDeviceContext* dc = aFrame->PresContext()->DeviceContext();
  int32_t appUnitsPerDevPixel = dc->AppUnitsPerDevPixel();
  LayoutDeviceIntSize contentSizeInDevPixels =
      LayoutDeviceIntSize::FromAppUnitsRounded(contentRect.Size(),
                                               appUnitsPerDevPixel);

  // Use empty CSSSizeOrRatio to force set the preferred size as the frame's
  // content box size.
  imageRenderer.SetPreferredSize(CSSSizeOrRatio(), contentRect.Size());

  RefPtr<gfx::DrawTarget> drawTarget =
      gfxPlatform::GetPlatform()->CreateOffscreenCanvasDrawTarget(
          contentSizeInDevPixels.ToUnknownSize(), gfx::SurfaceFormat::A8);
  if (!drawTarget) {
    return nullptr;
  }

  gfxContext context(drawTarget);

  ImgDrawResult result =
      imageRenderer.DrawShapeImage(aFrame->PresContext(), context);

  if (result != ImgDrawResult::SUCCESS) {
    return nullptr;
  }

  // Retrieve the pixel image buffer to create the image shape info.
  RefPtr<SourceSurface> sourceSurface = drawTarget->Snapshot();
  RefPtr<DataSourceSurface> dataSourceSurface = sourceSurface->GetDataSurface();
  DataSourceSurface::ScopedMap map(dataSourceSurface, DataSourceSurface::READ);

  if (!map.IsMapped()) {
    return nullptr;
  }

  MOZ_ASSERT(sourceSurface->GetSize() == contentSizeInDevPixels.ToUnknownSize(),
             "Who changes the size?");

  nsRect marginRect = aMarginRect.GetPhysicalRect(aWM, aContainerSize);

  uint8_t* alphaPixels = map.GetData();
  int32_t stride = map.GetStride();

  // NOTE: ImageShapeInfo constructor does not keep a persistent copy of
  // alphaPixels; it's only used during the constructor to compute pixel ranges.
  return MakeUnique<ImageShapeInfo>(alphaPixels, stride, contentSizeInDevPixels,
                                    appUnitsPerDevPixel, aShapeImageThreshold,
                                    aShapeMargin, contentRect, marginRect, aWM,
                                    aContainerSize);
}

/* static */
nscoord nsFloatManager::ShapeInfo::ComputeEllipseLineInterceptDiff(
    const nscoord aShapeBoxBStart, const nscoord aShapeBoxBEnd,
    const nscoord aBStartCornerRadiusL, const nscoord aBStartCornerRadiusB,
    const nscoord aBEndCornerRadiusL, const nscoord aBEndCornerRadiusB,
    const nscoord aBandBStart, const nscoord aBandBEnd) {
  // An example for the band intersecting with the top right corner of an
  // ellipse with writing-mode horizontal-tb.
  //
  //                             lineIntercept lineDiff
  //                                    |       |
  //  +---------------------------------|-------|-+---- aShapeBoxBStart
  //  |                ##########^      |       | |
  //  |            ##############|####  |       | |
  //  +---------#################|######|-------|-+---- aBandBStart
  //  |       ###################|######|##     | |
  //  |     aBStartCornerRadiusB |######|###    | |
  //  |    ######################|######|#####  | |
  //  +---#######################|<-----------><->^---- aBandBEnd
  //  |  ########################|##############  |
  //  |  ########################|##############  |---- b
  //  | #########################|############### |
  //  | ######################## v<-------------->v
  //  |###################### aBStartCornerRadiusL|
  //  |###########################################|
  //  |###########################################|
  //  |###########################################|
  //  |###########################################|
  //  | ######################################### |
  //  | ######################################### |
  //  |  #######################################  |
  //  |  #######################################  |
  //  |   #####################################   |
  //  |    ###################################    |
  //  |      ###############################      |
  //  |       #############################       |
  //  |         #########################         |
  //  |            ###################            |
  //  |                ###########                |
  //  +-------------------------------------------+----- aShapeBoxBEnd

  NS_ASSERTION(aShapeBoxBStart <= aShapeBoxBEnd, "Bad shape box coordinates!");
  NS_ASSERTION(aBandBStart <= aBandBEnd, "Bad band coordinates!");

  nscoord lineDiff = 0;

  // If the band intersects both the block-start and block-end corners, we
  // don't need to enter either branch because the correct lineDiff is 0.
  if (aBStartCornerRadiusB > 0 && aBandBEnd >= aShapeBoxBStart &&
      aBandBEnd <= aShapeBoxBStart + aBStartCornerRadiusB) {
    // The band intersects only the block-start corner.
    nscoord b = aBStartCornerRadiusB - (aBandBEnd - aShapeBoxBStart);
    nscoord lineIntercept =
        XInterceptAtY(b, aBStartCornerRadiusL, aBStartCornerRadiusB);
    lineDiff = aBStartCornerRadiusL - lineIntercept;
  } else if (aBEndCornerRadiusB > 0 &&
             aBandBStart >= aShapeBoxBEnd - aBEndCornerRadiusB &&
             aBandBStart <= aShapeBoxBEnd) {
    // The band intersects only the block-end corner.
    nscoord b = aBEndCornerRadiusB - (aShapeBoxBEnd - aBandBStart);
    nscoord lineIntercept =
        XInterceptAtY(b, aBEndCornerRadiusL, aBEndCornerRadiusB);
    lineDiff = aBEndCornerRadiusL - lineIntercept;
  }

  return lineDiff;
}

/* static */
nscoord nsFloatManager::ShapeInfo::XInterceptAtY(const nscoord aY,
                                                 const nscoord aRadiusX,
                                                 const nscoord aRadiusY) {
  // Solve for x in the ellipse equation (x/radiusX)^2 + (y/radiusY)^2 = 1.
  MOZ_ASSERT(aRadiusY > 0);
  const auto ratioY = aY / static_cast<double>(aRadiusY);
  MOZ_ASSERT(ratioY <= 1, "Why is position y outside of the radius on y-axis?");
  return NSToCoordTrunc(aRadiusX * std::sqrt(1 - ratioY * ratioY));
}

/* static */
nsPoint nsFloatManager::ShapeInfo::ConvertToFloatLogical(
    const nsPoint& aPoint, WritingMode aWM, const nsSize& aContainerSize) {
  LogicalPoint logicalPoint(aWM, aPoint, aContainerSize);
  return nsPoint(logicalPoint.LineRelative(aWM, aContainerSize),
                 logicalPoint.B(aWM));
}

/* static */ nsRectCornerRadii nsFloatManager::ShapeInfo::ConvertToFloatLogical(
    const nsRectCornerRadii& aRadii, WritingMode aWM) {
  nsRectCornerRadii logicalRadii;

  // Get the physical side for line-left and line-right since border radii
  // are on the physical axis.
  Side lineLeftSide = aWM.PhysicalSide(
      aWM.LogicalSideForLineRelativeDir(LineRelativeDir::Left));
  logicalRadii[eCornerTopLeftX] =
      aRadii[SideToHalfCorner(lineLeftSide, true, false)];
  logicalRadii[eCornerTopLeftY] =
      aRadii[SideToHalfCorner(lineLeftSide, true, true)];
  logicalRadii[eCornerBottomLeftX] =
      aRadii[SideToHalfCorner(lineLeftSide, false, false)];
  logicalRadii[eCornerBottomLeftY] =
      aRadii[SideToHalfCorner(lineLeftSide, false, true)];

  Side lineRightSide = aWM.PhysicalSide(
      aWM.LogicalSideForLineRelativeDir(LineRelativeDir::Right));
  logicalRadii[eCornerTopRightX] =
      aRadii[SideToHalfCorner(lineRightSide, false, false)];
  logicalRadii[eCornerTopRightY] =
      aRadii[SideToHalfCorner(lineRightSide, false, true)];
  logicalRadii[eCornerBottomRightX] =
      aRadii[SideToHalfCorner(lineRightSide, true, false)];
  logicalRadii[eCornerBottomRightY] =
      aRadii[SideToHalfCorner(lineRightSide, true, true)];

  if (aWM.IsLineInverted()) {
    // When IsLineInverted() is true, i.e. aWM is vertical-lr,
    // line-over/line-under are inverted from block-start/block-end. So the
    // relationship reverses between which corner comes first going
    // clockwise, and which corner is block-start versus block-end. We need
    // to swap the values stored in top and bottom corners.
    std::swap(logicalRadii[eCornerTopLeftX], logicalRadii[eCornerBottomLeftX]);
    std::swap(logicalRadii[eCornerTopLeftY], logicalRadii[eCornerBottomLeftY]);
    std::swap(logicalRadii[eCornerTopRightX],
              logicalRadii[eCornerBottomRightX]);
    std::swap(logicalRadii[eCornerTopRightY],
              logicalRadii[eCornerBottomRightY]);
  }

  return logicalRadii;
}

/* static */
size_t nsFloatManager::ShapeInfo::MinIntervalIndexContainingY(
    const nsTArray<nsRect>& aIntervals, const nscoord aTargetY) {
  // Perform a binary search to find the minimum index of an interval
  // that contains aTargetY. If no such interval exists, return a value
  // equal to the number of intervals.
  size_t startIdx = 0;
  size_t endIdx = aIntervals.Length();
  while (startIdx < endIdx) {
    size_t midIdx = startIdx + (endIdx - startIdx) / 2;
    if (aIntervals[midIdx].ContainsY(aTargetY)) {
      return midIdx;
    }
    nscoord midY = aIntervals[midIdx].Y();
    if (midY < aTargetY) {
      startIdx = midIdx + 1;
    } else {
      endIdx = midIdx;
    }
  }

  return endIdx;
}

/* static */
nscoord nsFloatManager::ShapeInfo::LineEdge(const nsTArray<nsRect>& aIntervals,
                                            const nscoord aBStart,
                                            const nscoord aBEnd,
                                            bool aIsLineLeft) {
  MOZ_ASSERT(aBStart <= aBEnd,
             "The band's block start is greater than its block end?");

  // Find all the intervals whose rects overlap the aBStart to
  // aBEnd range, and find the most constraining inline edge
  // depending on the value of aLeft.

  // Since the intervals are stored in block-axis order, we need
  // to find the first interval that overlaps aBStart and check
  // succeeding intervals until we get past aBEnd.

  nscoord lineEdge = aIsLineLeft ? nscoord_MAX : nscoord_MIN;

  size_t intervalCount = aIntervals.Length();
  for (size_t i = MinIntervalIndexContainingY(aIntervals, aBStart);
       i < intervalCount; ++i) {
    // We can always get the bCoord from the intervals' mLineLeft,
    // since the y() coordinate is duplicated in both points in the
    // interval.
    auto& interval = aIntervals[i];
    nscoord bCoord = interval.Y();
    if (bCoord >= aBEnd) {
      break;
    }
    // Get the edge from the interval point indicated by aLeft.
    if (aIsLineLeft) {
      lineEdge = std::min(lineEdge, interval.X());
    } else {
      lineEdge = std::max(lineEdge, interval.XMost());
    }
  }

  return lineEdge;
}

/* static */ nsFloatManager::ShapeInfo::dfType
nsFloatManager::ShapeInfo::CalcUsedShapeMargin5X(nscoord aShapeMargin,
                                                 int32_t aAppUnitsPerDevPixel) {
  // Our distance field has to be able to hold values equal to the
  // maximum shape-margin value that we care about faithfully rendering,
  // times 5. A 16-bit unsigned int can represent up to ~ 65K which means
  // we can handle a margin up to ~ 13K device pixels. That's good enough
  // for practical usage. Any supplied shape-margin value higher than this
  // maximum will be clamped.
  static const float MAX_MARGIN_5X_FLOAT = (float)MAX_MARGIN_5X;

  // Convert aShapeMargin to dev pixels, convert that into 5x-dev-pixel
  // space, then clamp to MAX_MARGIN_5X_FLOAT.
  float shapeMarginDevPixels5X =
      5.0f * NSAppUnitsToFloatPixels(aShapeMargin, aAppUnitsPerDevPixel);
  NS_WARNING_ASSERTION(shapeMarginDevPixels5X <= MAX_MARGIN_5X_FLOAT,
                       "shape-margin is too large and is being clamped.");

  // We calculate a minimum in float space, which takes care of any overflow
  // or infinity that may have occurred earlier from multiplication of
  // too-large aShapeMargin values.
  float usedMargin5XFloat =
      std::min(shapeMarginDevPixels5X, MAX_MARGIN_5X_FLOAT);
  return (dfType)NSToIntRound(usedMargin5XFloat);
}

//----------------------------------------------------------------------

nsAutoFloatManager::~nsAutoFloatManager() {
  // Restore the old float manager in the reflow input if necessary.
  if (mNew) {
#ifdef DEBUG
    if (nsBlockFrame::gNoisyFloatManager) {
      printf("restoring old float manager %p\n", mOld);
    }
#endif

    mReflowInput.mFloatManager = mOld;

#ifdef DEBUG
    if (nsBlockFrame::gNoisyFloatManager) {
      if (mOld) {
        mReflowInput.mFrame->ListTag(stdout);
        printf(": float manager %p after reflow\n", mOld);
        mOld->List(stdout);
      }
    }
#endif
  }
}

void nsAutoFloatManager::CreateFloatManager(nsPresContext* aPresContext) {
  MOZ_ASSERT(!mNew, "Redundant call to CreateFloatManager!");

  // Create a new float manager and install it in the reflow
  // input. `Remember' the old float manager so we can restore it
  // later.
  mNew = MakeUnique<nsFloatManager>(mReflowInput.GetWritingMode());

#ifdef DEBUG
  if (nsBlockFrame::gNoisyFloatManager) {
    printf("constructed new float manager %p (replacing %p)\n", mNew.get(),
           mReflowInput.mFloatManager);
  }
#endif

  // Set the float manager in the existing reflow input.
  mOld = mReflowInput.mFloatManager;
  mReflowInput.mFloatManager = mNew.get();
}
