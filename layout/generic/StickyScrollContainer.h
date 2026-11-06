/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * compute sticky positioning, both during reflow and when the scrolling
 * container scrolls
 */

#ifndef StickyScrollContainer_h
#define StickyScrollContainer_h

#include "mozilla/DepthOrderedFrameList.h"
#include "mozilla/Maybe.h"
#include "nsPoint.h"
#include "nsRectAbsolute.h"
#include "nsTArray.h"

struct nsRect;
class nsIFrame;

namespace mozilla::layers {
enum class ScrollDirection : uint8_t;
}  // namespace mozilla::layers

namespace mozilla {

class ScrollContainerFrame;

class StickyScrollContainer final {
 public:
  /**
   * Find (and create if necessary) the StickyScrollContainer associated with
   * the scroll container of the given frame, if a scroll container exists.
   */
  static StickyScrollContainer* GetOrCreateForFrame(nsIFrame*);

  /**
   * As GetOrCreateForFrame, but returning null if the scroll container lacks
   * a StickyScrollContainer (instead of creating one). Use this flavor for
   * read-only queries, such as from view timeline code: if no
   * StickyScrollContainer exists there is no sticking to report, and creating
   * one would needlessly mutate layout state.
   */
  static StickyScrollContainer* GetForFrame(const nsIFrame*);

  void AddFrame(nsIFrame* aFrame);
  void RemoveFrame(nsIFrame* aFrame) { mFrames.Remove(aFrame); }

  ScrollContainerFrame* ScrollContainer() const {
    return mScrollContainerFrame;
  }

  // Compute the offsets for a sticky position element
  static void ComputeStickyOffsets(nsIFrame* aFrame);

  /**
   * Compute the position of a sticky positioned frame, based on information
   * stored in its properties along with our scroll frame and scroll position.
   */
  nsPoint ComputePosition(nsIFrame* aFrame) const;

  /**
   * Same as above, but as if our scroll position were zero, so that the result
   * is independent of the current scroll position.
   */
  nsPoint ComputePositionIgnoringScrolling(nsIFrame* aFrame) const;

  /**
   * The translation by which sticky positioning shifts aFrame from its normal
   * position to its sticky position, computed as if our scroll position were
   * zero. All continuations of a sticky frame are shifted by the same amount.
   */
  nsPoint ComputeTranslationIgnoringScrolling(const nsIFrame* aFrame) const;

  /**
   * Compute where a frame should not scroll with the page, represented by the
   * difference of two rectangles.
   */
  void GetScrollRanges(nsIFrame* aFrame, nsRectAbsolute* aOuter,
                       nsRectAbsolute* aInner) const;

  /**
   * Describes how a frame sticks to one side of the scrollport along one axis.
   * Sides here are physical: the start side is the frame's top or left edge
   * and the end side its bottom or right edge, regardless of writing mode,
   * direction or flex-direction.
   */
  struct StickyScrollRange {
    // The scroll offset at which sticking to this side begins or ends, in the
    // signed space of ScrollContainerFrame::GetScrollPosition().
    nscoord mScrollPosition = 0;
    // How far sticking to this side can displace the frame before the
    // containing block stops it. Never negative.
    nscoord mMaxOffset = 0;
  };

  /**
   * The per-side sticky ranges for one axis. A side is Nothing() if the frame
   * doesn't stick that way, or if the sticky offsets haven't been computed
   * yet.
   */
  struct StickyScrollRanges {
    Maybe<StickyScrollRange> mStartSide;
    Maybe<StickyScrollRange> mEndSide;
  };

  /**
   * Compute, for each side of the given axis, the range of scroll offsets
   * over which sticking displaces aFrame, and by how much. This re-expresses
   * the stick and containment rectangles of ComputeStickyLimits() (see its
   * documentation) as scroll offset ranges for a single axis.
   *
   * Compare GetScrollRanges(), which describes the same sticking as the 2-D
   * region of scroll positions over which aFrame does not move with the
   * scrolled content, rather than as per-side scroll offset thresholds and
   * displacement limits along one axis.
   */
  StickyScrollRanges GetStickyScrollRangesForAxis(
      const nsIFrame* aFrame, layers::ScrollDirection aAxis) const;

  /**
   * Compute and set the position of a frame and its following continuations.
   */
  void PositionContinuations(nsIFrame* aFrame);

  /**
   * Compute and set the position of all sticky frames, given the current
   * scroll position of the scroll frame. If not in reflow, aSubtreeRoot should
   * be null; otherwise, overflow-area updates will be limited to not affect
   * aSubtreeRoot or its ancestors.
   */
  void UpdatePositions(nsPoint aScrollPosition, nsIFrame* aSubtreeRoot);

  ~StickyScrollContainer() = default;

  const DepthOrderedFrameList& GetFrames() const { return mFrames; }

  /**
   * Returns true if the frame is "stuck" in the y direction, ie it's acting
   * like fixed position. aFrame should be in GetFrames().
   */
  bool IsStuckInYDirection(nsIFrame* aFrame) const;

  /**
   * Calls FrameNeedsReflow on all sticky position children.
   */
  void MarkFramesForReflow();

  void SetShouldFlatten(bool aShouldFlatten) {
    mShouldFlatten = aShouldFlatten;
  }
  bool ShouldFlattenAway() const { return mShouldFlatten; }

  explicit StickyScrollContainer(ScrollContainerFrame* aScrollContainerFrame);

 private:
  /**
   * Whether ComputeStickyLimits() positions its returned StickyLimits::mStick
   * rectangle relative to the current scroll position, or as if the scroll
   * position were zero (in which case the result is independent of the current
   * scroll position).
   */
  enum class StickyLimitSpace : uint8_t {
    RelativeToCurrentScroll,
    IgnoreCurrentScroll,
  };

  struct StickyLimits {
    // Based on the scroll container.
    nsRect mStick;
    // Based on the containing block.
    nsRect mContain;
  };

  /**
   * Compute the two rectangles that determine sticky positioning. Sticky
   * positioning keeps the frame position (its upper-left corner) always within
   * |mContain| and secondarily within |mStick|.
   */
  StickyLimits ComputeStickyLimits(
      nsIFrame* aFrame, StickyLimitSpace aSpace =
                            StickyLimitSpace::RelativeToCurrentScroll) const;

  nsPoint DoComputePosition(nsIFrame* aFrame, StickyLimitSpace aSpace) const;

  ScrollContainerFrame* const mScrollContainerFrame;
  DepthOrderedFrameList mFrames;
  nsPoint mScrollPosition;
  bool mShouldFlatten = false;
};

}  // namespace mozilla

#endif /* StickyScrollContainer_h */
