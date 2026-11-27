/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* class that manages rules for positioning floats */

#ifndef nsFloatManager_h_
#define nsFloatManager_h_

#include "mozilla/TypedEnumBits.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WritingModes.h"
#include "nsCoord.h"
#include "nsFrameList.h"  // for DEBUG_FRAME_DUMP
#include "nsIntervalSet.h"
#include "nsPoint.h"
#include "nsTArray.h"

class nsIFrame;
class nsPresContext;
namespace mozilla {
struct ReflowInput;
class PresShell;
}  // namespace mozilla

enum class nsFlowAreaRectFlags : uint32_t {
  NoFlags = 0,
  HasFloats = 1 << 0,
  MayWiden = 1 << 1,
  ISizeIsActuallyNegative = 1 << 2,
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(nsFlowAreaRectFlags)

/**
 * The available space for content not occupied by floats is divided
 * into a sequence of rectangles in the block direction.  However, we
 * need to know not only the rectangle, but also whether it was reduced
 * (from the content rectangle) by floats that actually intruded into
 * the content rectangle. If it has been reduced by floats, then we also
 * track whether the flow area might widen as the floats narrow in the
 * block direction.
 */
struct nsFlowAreaRect {
  mozilla::LogicalRect mRect;

  nsFlowAreaRectFlags mAreaFlags;

  nsFlowAreaRect(mozilla::WritingMode aWritingMode, nscoord aICoord,
                 nscoord aBCoord, nscoord aISize, nscoord aBSize,
                 nsFlowAreaRectFlags aAreaFlags)
      : mRect(aWritingMode, aICoord, aBCoord, aISize, aBSize),
        mAreaFlags(aAreaFlags) {}

  bool HasFloats() const {
    return (bool)(mAreaFlags & nsFlowAreaRectFlags::HasFloats);
  }
  bool MayWiden() const {
    return (bool)(mAreaFlags & nsFlowAreaRectFlags::MayWiden);
  }
  bool ISizeIsActuallyNegative() const {
    return (bool)(mAreaFlags & nsFlowAreaRectFlags::ISizeIsActuallyNegative);
  }
};

#define NS_FLOAT_MANAGER_CACHE_SIZE 64

/**
 * nsFloatManager is responsible for implementing CSS's rules for
 * positioning floats. An nsFloatManager object is created during reflow for
 * any block with NS_BLOCK_BFC. During reflow, the float manager for the nearest
 * such ancestor block is found in ReflowInput::mFloatManager.
 *
 * According to the line-relative mappings in CSS Writing Modes spec [1],
 * line-right and line-left are calculated with respect to the writing mode
 * of the containing block of the floats. All the writing modes passed to
 * nsFloatManager methods should be the containing block's writing mode.
 *
 * However, according to the abstract-to-physical mappings table [2], the
 * 'direction' property of the containing block doesn't affect the
 * interpretation of line-right and line-left. We actually implement this by
 * passing in the writing mode of the block formatting context (BFC), i.e.
 * the of BlockReflowState's writing mode.
 *
 * nsFloatManager uses a special logical coordinate space with inline
 * coordinates on the line-axis and block coordinates on the block-axis
 * based on the writing mode of the block formatting context. All the
 * physical types like nsRect, nsPoint, etc. use this coordinate space. See
 * FloatInfo::mRect for an example.
 *
 * [1] https://drafts.csswg.org/css-writing-modes/#line-mappings
 * [2] https://drafts.csswg.org/css-writing-modes/#logical-to-physical
 */
class nsFloatManager {
 public:
  explicit nsFloatManager(mozilla::WritingMode aWM);
  ~nsFloatManager();

  nsFloatManager(const nsFloatManager&) = delete;
  void operator=(const nsFloatManager&) = delete;

  void* operator new(size_t aSize) noexcept(true);
  void operator delete(void* aPtr, size_t aSize);

  static void Shutdown();

  /**
   * Get float region stored on the frame. (Defaults to mRect if it's
   * not there.) The float region is the area impacted by this float;
   * the coordinates are relative to the containing block frame.
   */
  static mozilla::LogicalRect GetRegionFor(mozilla::WritingMode aWM,
                                           nsIFrame* aFloatFrame,
                                           const nsSize& aContainerSize);
  /**
   * Calculate the float region for this frame using aMargin and the
   * frame's mRect. The region includes the margins around the float,
   * but doesn't include the relative offsets.
   * Note that if the frame is or has a continuation, aMargin's top
   * and/or bottom must be zeroed by the caller.
   */
  static mozilla::LogicalRect CalculateRegionFor(
      mozilla::WritingMode aWM, nsIFrame* aFloatFrame,
      const mozilla::LogicalMargin& aMargin, const nsSize& aContainerSize);
  /**
   * Store the float region on the frame. The region is stored
   * as a delta against the mRect, so repositioning the frame will
   * also reposition the float region.
   */
  static void StoreRegionFor(mozilla::WritingMode aWM, nsIFrame* aFloat,
                             const mozilla::LogicalRect& aRegion,
                             const nsSize& aContainerSize);

  // Structure that stores the current state of a float manager for
  // Save/Restore purposes.
  struct SavedState {
    explicit SavedState()
        : mFloatInfoCount(0),
          mLineLeft(0),
          mBlockStart(0),
          mPushedLeftFloatPastBreak(false),
          mPushedRightFloatPastBreak(false),
          mSplitLeftFloatAcrossBreak(false),
          mSplitRightFloatAcrossBreak(false) {}

   private:
    uint32_t mFloatInfoCount;
    nscoord mLineLeft, mBlockStart;
    bool mPushedLeftFloatPastBreak;
    bool mPushedRightFloatPastBreak;
    bool mSplitLeftFloatAcrossBreak;
    bool mSplitRightFloatAcrossBreak;

    friend class nsFloatManager;
  };

  /**
   * Translate the current origin by the specified offsets. This
   * creates a new local coordinate space relative to the current
   * coordinate space.
   */
  void Translate(nscoord aLineLeft, nscoord aBlockStart) {
    mLineLeft += aLineLeft;
    mBlockStart += aBlockStart;
  }

  /**
   * Returns the current translation from local coordinate space to
   * world coordinate space. This represents the accumulated calls to
   * Translate().
   */
  void GetTranslation(nscoord& aLineLeft, nscoord& aBlockStart) const {
    aLineLeft = mLineLeft;
    aBlockStart = mBlockStart;
  }

  /**
   * Get information about the area available to content that flows
   * around floats.  Two different types of space can be requested:
   *   BandFromPoint: returns the band containing block-dir coordinate
   *     |aBCoord| (though actually with the top truncated to begin at
   *     aBCoord), but up to at most |aBSize| (which may be nscoord_MAX).
   *     This will return the tallest rectangle whose block start is
   *     |aBCoord| and in which there are no changes in what floats are
   *     on the sides of that rectangle, but will limit the block size
   *     of the rectangle to |aBSize|.  The inline start and end edges
   *     of the rectangle give the area available for line boxes in that
   *     space. The inline size of this resulting rectangle will not be
   *     negative.
   *   WidthWithinHeight: This returns a rectangle whose block start
   *     is aBCoord and whose block size is exactly aBSize.  Its inline
   *     start and end edges give the corresponding edges of the space
   *     that can be used for line boxes *throughout* that space.  (It
   *     is possible that more inline space could be used in part of the
   *     space if a float begins or ends in it.)  The inline size of the
   *     resulting rectangle can be negative.
   *
   * ShapeType can be used to request two different types of flow areas.
   * (This is the float area defined in CSS Shapes Module Level 1 §1.4):
   *    Margin: uses the float element's margin-box to request the flow area.
   *    ShapeOutside: uses the float element's shape-outside value to request
   *      the float area.
   *
   * @param aBCoord [in] block-dir coordinate for block start of available space
   *          desired, which are positioned relative to the current translation.
   * @param aBSize [in] see above
   * @param aContentArea [in] an nsRect representing the content area
   * @param aState [in] If null, use the current state, otherwise, do
   *                    computation based only on floats present in the given
   *                    saved state.
   * @return An nsFlowAreaRect whose:
   *           mRect is the resulting rectangle for line boxes.  It will not
   *             extend beyond aContentArea's inline bounds, but may be
   *             narrower when floats are present.
   *           mHasFloats is whether there are floats at the sides of the
   *             return value including those that do not reduce the line box
   *             inline size at all (because they are entirely in the margins)
   */
  enum class BandInfoType { BandFromPoint, WidthWithinHeight };
  enum class ShapeType { Margin, ShapeOutside };
  nsFlowAreaRect GetFlowArea(mozilla::WritingMode aCBWM,
                             mozilla::WritingMode aWM, nscoord aBCoord,
                             nscoord aBSize, BandInfoType aBandInfoType,
                             ShapeType aShapeType,
                             mozilla::LogicalRect aContentArea,
                             SavedState* aState,
                             const nsSize& aContainerSize) const;

  /**
   * Add a float that comes after all floats previously added.  Its
   * block start must be even with or below the top of all previous
   * floats.
   *
   * aMarginRect is relative to the current translation.  The caller
   * must ensure aMarginRect.height >= 0 and aMarginRect.width >= 0.
   */
  void AddFloat(nsIFrame* aFloatFrame, const mozilla::LogicalRect& aMarginRect,
                mozilla::WritingMode aWM, const nsSize& aContainerSize);

  /**
   * Notify that we tried to place a float that could not fit at all and
   * had to be pushed to the next page/column?  (If so, we can't place
   * any more floats in this page/column because of the rule that the
   * top of a float cannot be above the top of an earlier float.  It
   * also means that any clear needs to continue to the next column.)
   */
  void SetPushedLeftFloatPastBreak() { mPushedLeftFloatPastBreak = true; }
  void SetPushedRightFloatPastBreak() { mPushedRightFloatPastBreak = true; }

  /**
   * Notify that we split a float, with part of it needing to be pushed
   * to the next page/column.  (This means that any 'clear' needs to
   * continue to the next page/column.)
   */
  void SetSplitLeftFloatAcrossBreak() { mSplitLeftFloatAcrossBreak = true; }
  void SetSplitRightFloatAcrossBreak() { mSplitRightFloatAcrossBreak = true; }

  /**
   * Remove the regions associated with this floating frame and its
   * next-sibling list.  Some of the frames may never have been added;
   * we just skip those. This is not fully general; it only works as
   * long as the N frames to be removed are the last N frames to have
   * been added; if there's a frame in the middle of them that should
   * not be removed, YOU LOSE.
   */
  nsresult RemoveTrailingRegions(nsIFrame* aFrameList);

  bool HasAnyFloats() const { return !mFloats.IsEmpty(); }

  /**
   * Methods for dealing with the propagation of float damage during
   * reflow.
   */
  bool HasFloatDamage() const { return !mFloatDamage.IsEmpty(); }

  void IncludeInDamage(nscoord aIntervalBegin, nscoord aIntervalEnd) {
    mFloatDamage.IncludeInterval(aIntervalBegin + mBlockStart,
                                 aIntervalEnd + mBlockStart);
  }

  bool IntersectsDamage(nscoord aIntervalBegin, nscoord aIntervalEnd) const {
    return mFloatDamage.Intersects(aIntervalBegin + mBlockStart,
                                   aIntervalEnd + mBlockStart);
  }

  /**
   * Saves the current state of the float manager into aState.
   */
  void PushState(SavedState* aState);

  /**
   * Restores the float manager to the saved state.
   *
   * These states must be managed using stack discipline. PopState can only
   * be used after PushState has been used to save the state, and it can only
   * be used once --- although it can be omitted; saved states can be ignored.
   * States must be popped in the reverse order they were pushed.  A
   * call to PopState invalidates any saved states Pushed after the
   * state passed to PopState was pushed.
   */
  void PopState(SavedState* aState);

  /**
   * Get the block start of the last float placed into the float
   * manager, to enforce the rule that a float can't be above an earlier
   * float. Returns the minimum nscoord value if there are no floats.
   *
   * The result is relative to the current translation.
   */
  nscoord LowestFloatBStart() const;

  /**
   * Return the coordinate of the lowest float matching aClearType in
   * this float manager. Returns aBCoord if there are no matching
   * floats.
   *
   * Both aBCoord and the result are relative to the current translation.
   */
  nscoord ClearFloats(nscoord aBCoord, mozilla::UsedClear aClearType) const;

  /**
   * Checks if clear would pass into the floats' BFC's next-in-flow,
   * i.e. whether floats affecting this clear have continuations.
   */
  bool ClearContinues(mozilla::UsedClear aClearType) const;

  void AssertStateMatches(SavedState* aState) const {
    NS_ASSERTION(
        aState->mLineLeft == mLineLeft && aState->mBlockStart == mBlockStart &&
            aState->mPushedLeftFloatPastBreak == mPushedLeftFloatPastBreak &&
            aState->mPushedRightFloatPastBreak == mPushedRightFloatPastBreak &&
            aState->mSplitLeftFloatAcrossBreak == mSplitLeftFloatAcrossBreak &&
            aState->mSplitRightFloatAcrossBreak ==
                mSplitRightFloatAcrossBreak &&
            aState->mFloatInfoCount == mFloats.Length(),
        "float manager state should match saved state");
  }

#ifdef DEBUG_FRAME_DUMP
  /**
   * Dump the state of the float manager out to a file.
   */
  nsresult List(FILE* out) const;
#endif

 private:
  class ShapeInfo;
  class RoundedBoxShapeInfo;
  class EllipseShapeInfo;
  class PolygonShapeInfo;
  class ImageShapeInfo;

  struct FloatInfo {
    nsIFrame* const mFrame;
    // The lowest block-ends of left/right floats up to and including
    // this one.
    nscoord mLeftBEnd, mRightBEnd;

    FloatInfo(nsIFrame* aFrame, nscoord aLineLeft, nscoord aBlockStart,
              const mozilla::LogicalRect& aMarginRect, mozilla::WritingMode aWM,
              const nsSize& aContainerSize);

    nscoord LineLeft() const { return mRect.x; }
    nscoord LineRight() const { return mRect.XMost(); }
    nscoord ISize() const { return mRect.width; }
    nscoord BStart() const { return mRect.y; }
    nscoord BEnd() const { return mRect.YMost(); }
    nscoord BSize() const { return mRect.height; }
    bool IsEmpty() const { return mRect.IsEmpty(); }

    // aBStart and aBEnd are the starting and ending coordinate of a band.
    // LineLeft() and LineRight() return the innermost line-left extent and
    // line-right extent within the given band, respectively.
    nscoord LineLeft(ShapeType aShapeType, const nscoord aBStart,
                     const nscoord aBEnd) const;
    nscoord LineRight(ShapeType aShapeType, const nscoord aBStart,
                      const nscoord aBEnd) const;
    nscoord BStart(ShapeType aShapeType) const;
    nscoord BEnd(ShapeType aShapeType) const;
    bool IsEmpty(ShapeType aShapeType) const;
    bool MayNarrowInBlockDirection(ShapeType aShapeType) const;

#ifdef NS_BUILD_REFCNT_LOGGING
    FloatInfo(FloatInfo&& aOther);
    ~FloatInfo();
#endif

    // NB! This is really a logical rect in a writing mode suitable for
    // placing floats, which is not necessarily the actual writing mode
    // either of the block which created the float manager or the block
    // that is calling the float manager. The inline coordinates are in
    // the line-relative axis of the float manager and its block
    // coordinates are in the float manager's block direction.
    nsRect mRect;
    // Pointer to a concrete subclass of ShapeInfo or null, which means that
    // there is no shape-outside.
    mozilla::UniquePtr<ShapeInfo> mShapeInfo;
  };

#ifdef DEBUG
  // Store the writing mode from the block frame which establishes the block
  // formatting context (BFC) when the nsFloatManager is created.
  mozilla::WritingMode mWritingMode;
#endif

  // Translation from local to global coordinate space.
  nscoord mLineLeft, mBlockStart;
  // We use 11 here in order to fill up the jemalloc allocatoed chunk nicely,
  // see https://bugzilla.mozilla.org/show_bug.cgi?id=1362876#c6.
  AutoTArray<FloatInfo, 11> mFloats;
  nsIntervalSet mFloatDamage;

  // Did we try to place a float that could not fit at all and had to be
  // pushed to the next page/column?  If so, we can't place any more
  // floats in this page/column because of the rule that the top of a
  // float cannot be above the top of an earlier float.  And we also
  // need to apply this information to 'clear', and thus need to
  // separate left and right floats.
  bool mPushedLeftFloatPastBreak;
  bool mPushedRightFloatPastBreak;

  // Did we split a float, with part of it needing to be pushed to the
  // next page/column.  This means that any 'clear' needs to continue to
  // the next page/column.
  bool mSplitLeftFloatAcrossBreak;
  bool mSplitRightFloatAcrossBreak;

  static int32_t sCachedFloatManagerCount;
  static void* sCachedFloatManagers[NS_FLOAT_MANAGER_CACHE_SIZE];
};

/**
 * A helper class to manage maintenance of the float manager during
 * nsBlockFrame::Reflow. It automatically restores the old float
 * manager in the reflow input when the object goes out of scope.
 */
class nsAutoFloatManager {
  using ReflowInput = mozilla::ReflowInput;

 public:
  explicit nsAutoFloatManager(ReflowInput& aReflowInput)
      : mReflowInput(aReflowInput), mOld(nullptr) {}

  ~nsAutoFloatManager();

  /**
   * Create a new float manager for the specified frame. This will
   * `remember' the old float manager, and install the new float
   * manager in the reflow input.
   */
  void CreateFloatManager(nsPresContext* aPresContext);

 protected:
  ReflowInput& mReflowInput;
  mozilla::UniquePtr<nsFloatManager> mNew;

  // A non-owning pointer, which points to the object owned by
  // nsAutoFloatManager::mNew.
  nsFloatManager* mOld;
};

#endif /* !defined(nsFloatManager_h_) */
