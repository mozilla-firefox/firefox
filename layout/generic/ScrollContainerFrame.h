/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object to wrap rendering objects that should be scrollable */

#ifndef mozilla_ScrollContainerFrame_h_
#define mozilla_ScrollContainerFrame_h_

#include "FrameMetrics.h"
#include "ScrollVelocityQueue.h"
#include "mozilla/Attributes.h"
#include "mozilla/ScrollOrigin.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/dom/WindowBinding.h"  // for mozilla::dom::ScrollBehavior
#include "mozilla/layout/ScrollAnchorContainer.h"
#include "nsContainerFrame.h"
#include "nsExpirationTracker.h"
#include "nsIAnonymousContentCreator.h"
#include "nsIReflowCallback.h"
#include "nsIScrollbarMediator.h"
#include "nsIStatefulFrame.h"
#include "nsQueryFrame.h"
#include "nsThreadUtils.h"

class nsPresContext;
class nsIContent;
class nsAtom;
class AutoContainsBlendModeCapturer;

namespace mozilla {
struct nsDisplayListCollection;
class PresShell;
class PresState;
enum class PhysicalAxis : uint8_t;
enum class StyleScrollbarWidth : uint8_t;
class ScrollContainerFrame;
class ScrollPositionUpdate;
class StickyScrollContainer;
struct ScrollReflowInput;
struct ScrollStyles;
struct StyleScrollSnapAlign;
namespace layers {
class Layer;
class WebRenderLayerManager;
}  // namespace layers
namespace layout {
class ScrollbarActivity;
}  // namespace layout

}  // namespace mozilla

mozilla::ScrollContainerFrame* NS_NewScrollContainerFrame(
    mozilla::PresShell* aPresShell, mozilla::ComputedStyle* aStyle,
    bool aIsRoot);

namespace mozilla {

/**
 * The scroll frame creates and manages the scrolling view
 *
 * It only supports having a single child frame that typically is an area
 * frame, but doesn't have to be. The child frame must have a view, though
 *
 * Scroll frames don't support incremental changes, i.e. you can't replace
 * or remove the scrolled frame
 */
class ScrollContainerFrame : public nsContainerFrame,
                             public nsIScrollbarMediator,
                             public nsIAnonymousContentCreator,
                             public nsIReflowCallback,
                             public nsIStatefulFrame {
 public:
  using CSSPoint = mozilla::CSSPoint;
  using Element = dom::Element;
  using ScrollAnchorContainer = layout::ScrollAnchorContainer;
  using SnapTargetSet = nsTHashSet<RefPtr<nsIContent>>;

  friend ScrollContainerFrame* ::NS_NewScrollContainerFrame(
      mozilla::PresShell* aPresShell, ComputedStyle* aStyle, bool aIsRoot);
  friend class layout::ScrollAnchorContainer;

  NS_DECL_QUERYFRAME
  NS_DECL_FRAMEARENA_HELPERS(ScrollContainerFrame)

  void BuildDisplayList(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists) override;

  // Return the sum of inline-size of the scrollbar gutters (if any) at the
  // inline-start and inline-end edges of the scroll frame (for a potential
  // scrollbar that scrolls in the block axis).
  nscoord IntrinsicScrollbarGutterSizeAtInlineEdges() const;

  // Return the size of space created by scrollbar-gutter or actual scrollbars,
  // assuming that the content is *not* overflowing the container. In other
  // words, this space is created by stable scrollbar-gutter or by scrollbars
  // due to "overflow: scroll".
  nsMargin IntrinsicScrollbarGutterSize() const;

  // Compute stable scrollbar-gutter from scrollbar-width and scrollbar-gutter
  // properties.
  //
  // Note: this method doesn't consider overflow property and the space created
  // by the actual scrollbars. That is, if scrollbar-gutter is 'auto', this
  // method just returns empty result.
  nsMargin ComputeStableScrollbarGutter(
      const StyleScrollbarWidth& aStyleScrollbarWidth,
      const StyleScrollbarGutter& aStyleScrollbarGutter) const;

  bool GetBorderRadii(const nsSize& aFrameSize, const nsSize& aBorderArea,
                      nsIFrame::Sides aSkipSides,
                      nsRectCornerRadii&) const final;

  nscoord IntrinsicISize(const IntrinsicSizeInput& aInput,
                         IntrinsicISizeType aType) override;

  void Reflow(nsPresContext* aPresContext, ReflowOutput& aDesiredSize,
              const ReflowInput& aReflowInput,
              nsReflowStatus& aStatus) override;
  void DidReflow(nsPresContext* aPresContext,
                 const ReflowInput* aReflowInput) override;

  bool ComputeCustomOverflow(OverflowAreas& aOverflowAreas) final;

  BaselineSharingGroup GetDefaultBaselineSharingGroup() const override;
  nscoord SynthesizeFallbackBaseline(
      WritingMode aWM, BaselineSharingGroup aBaselineGroup) const override;
  Maybe<nscoord> GetNaturalBaselineBOffset(
      WritingMode aWM, BaselineSharingGroup aBaselineGroup,
      BaselineExportContext aExportContext) const override;

  StickyScrollContainer* GetStickyContainer() const {
    return mStickyContainer.get();
  }
  StickyScrollContainer& EnsureStickyContainer();

  // Recomputes the scrollable overflow area we store in the helper to take
  // children that are affected by perpsective set on the outer frame and scroll
  // at different rates.
  void AdjustForPerspective(nsRect& aScrollableOverflow);

  // Called to set the child frames. We typically have three: the scroll area,
  // the vertical scrollbar, and the horizontal scrollbar.
  void SetInitialChildList(ChildListID aListID,
                           nsFrameList&& aChildList) override;
  void AppendFrames(ChildListID aListID, nsFrameList&& aFrameList) final;
  void InsertFrames(ChildListID aListID, nsIFrame* aPrevFrame,
                    const nsLineList::iterator* aPrevFrameLine,
                    nsFrameList&& aFrameList) final;
  void RemoveFrame(DestroyContext&, ChildListID, nsIFrame*) final;

  void DidSetComputedStyle(ComputedStyle* aOldComputedStyle) override;

  void Destroy(DestroyContext&) override;

  ScrollContainerFrame* GetScrollTargetFrame() const final {
    return const_cast<ScrollContainerFrame*>(this);
  }

  nsContainerFrame* GetContentInsertionFrame() override {
    return GetScrolledFrame()->GetContentInsertionFrame();
  }

  nsPoint GetPositionOfChildIgnoringScrolling(const nsIFrame* aChild) final {
    nsPoint pt = aChild->GetPosition();
    if (aChild == GetScrolledFrame()) {
      pt += GetScrollPosition();
    }
    return pt;
  }

  // nsIAnonymousContentCreator
  nsresult CreateAnonymousContent(nsTArray<ContentInfo>&) override;
  void AppendAnonymousContentTo(nsTArray<nsIContent*>&,
                                uint32_t aFilter) override;

  /**
   * Get the frame for the content that we are scrolling within
   * this scrollable frame.
   */
  nsIFrame* GetScrolledFrame() const { return mScrolledFrame; }

  // Returns the frame for the "button box" (e.g., number spin-box, password
  // reveal button) that lives outside the scrolled area. Used by
  // nsTextControlFrame.
  virtual nsIFrame* GetButtonBoxFrame() const { return nullptr; }

  /**
   * Get the overflow styles (StyleOverflow::Scroll, StyleOverflow::Hidden, or
   * StyleOverflow::Auto) governing the horizontal and vertical scrollbars for
   * this frame.
   *
   * This is special because they can be propagated from the <body> element,
   * unlike other styles.
   */
  ScrollStyles GetScrollStyles() const;

  /**
   * Returns whether we already have anonymous content nodes for all our needed
   * scrollbar parts (or a superset thereof).
   */
  bool HasAllNeededScrollbars() const {
    return GetCurrentAnonymousContent().contains(GetNeededAnonymousContent());
  }

  struct PerAxisScrollDirections {
    bool mToRight = false;
    bool mToBottom = false;
  };

  static PerAxisScrollDirections ComputePerAxisScrollDirections(
      const nsIFrame* aScrolledFrame, bool aForTextInput = false);

  PerAxisScrollDirections ComputePerAxisScrollDirections() const {
    return ComputePerAxisScrollDirections(mScrolledFrame, IsTextInputFrame());
  }

  /**
   * Get the overscroll-behavior styles.
   */
  layers::OverscrollBehaviorInfo GetOverscrollBehaviorInfo() const;

  /**
   * Return the scrollbars which are visible. It's OK to call this during reflow
   * of the scrolled contents, in which case it will reflect the current
   * assumptions about scrollbar visibility.
   */
  layers::ScrollDirections GetScrollbarVisibility() const {
    layers::ScrollDirections result;
    if (mHasHorizontalScrollbar) {
      result += layers::ScrollDirection::eHorizontal;
    }
    if (mHasVerticalScrollbar) {
      result += layers::ScrollDirection::eVertical;
    }
    return result;
  }

  /**
   * Returns the directions in which scrolling is allowed (if the scroll range
   * is at least one device pixel in that direction).
   */
  layers::ScrollDirections GetAvailableScrollingDirections() const;

  /**
   * Returns the directions in which scrolling is allowed when taking into
   * account the visual viewport size and overflow hidden. (An (apz) zoomed in
   * overflow hidden scrollframe is actually user scrollable.)
   */
  layers::ScrollDirections GetAvailableScrollingDirectionsForUserInputEvents()
      const;

  /**
   * Return the actual sizes of all possible scrollbars. Returns 0 for scrollbar
   * positions that don't have a scrollbar or where the scrollbar is not
   * visible. Do not call this while this frame's descendants are being
   * reflowed, it won't be accurate.
   * INCLUDE_VISUAL_VIEWPORT_SCROLLBARS means we include the size of layout
   * scrollbars that are only visible to scroll the visual viewport inside the
   * layout viewport (ie the layout viewport cannot be scrolled) even though
   * there is no layout space set aside for these scrollbars.
   */
  enum class ScrollbarSizesOptions { NONE, INCLUDE_VISUAL_VIEWPORT_SCROLLBARS };
  nsMargin GetActualScrollbarSizes(
      ScrollbarSizesOptions aOptions = ScrollbarSizesOptions::NONE) const;

  /**
   * Return the sizes of all scrollbars assuming that any scrollbars that could
   * be visible due to overflowing content, are. This can be called during
   * reflow of the scrolled contents.
   */
  nsMargin GetDesiredScrollbarSizes() const;

  /**
   * Get the layout size of this frame.
   * Note that this is a value which is not expanded by the minimum scale size.
   * For scroll frames other than the root content document's scroll frame, this
   * value will be the same as GetScrollPortRect().Size().
   *
   * This value is used for Element.clientWidth and clientHeight.
   */
  nsSize GetLayoutSize() const {
    if (mIsUsingMinimumScaleSize) {
      return mICBSize;
    }
    return mScrollPort.Size();
  }

  /**
   * Get the size used for window.innerWidth or innerHeight.
   * This works only for the root scroll container.
   */
  nsSize GetSizeForWindowInnerSize() const;

  /**
   * GetScrolledRect is designed to encapsulate deciding which
   * directions of overflow should be reachable by scrolling and which
   * should not.  Callers should NOT depend on it having any particular
   * behavior.
   *
   * This should only be called when the scrolled frame has been
   * reflowed with the scroll port size given in mScrollPort.
   *
   * Currently it allows scrolling down and to the right for
   * ScrollContainerFrames with LTR directionality, and allows scrolling down
   * and to the left for ScrollContainerFrames with RTL directionality.
   */
  nsRect GetScrolledRect() const;

  /**
   * While an instance of this class is alive, GetScrolledRect() caches its
   * result so that repeated calls don't recompute the (relatively expensive)
   * snapped rect. This is meant to wrap an operation during which
   * GetScrolledRect() may be called many times and the scrolled rect is known
   * not to change, such as building a display list or finishing reflow.
   *
   * The cache and the cached value both live in this stack object, so the only
   * per-frame cost is the stack space and a single pointer, set while an
   * instance is alive. The value is computed lazily, on the first
   * GetScrolledRect() call, so using this class when GetScrolledRect() is never
   * called costs nothing.
   *
   * Passing aReferenceFrame is optional but saves time computing it if the
   * caller has easy access to it.
   *
   * In debug builds, the cached value is recomputed (against that same
   * reference frame) on every subsequent GetScrolledRect() call and asserted to
   * be unchanged, catching cases where the scrolled rect changes unexpectedly
   * during the operation.
   */
  class MOZ_RAII AutoScrolledRectCache {
   public:
    AutoScrolledRectCache(ScrollContainerFrame* aFrame,
                          const nsIFrame* aReferenceFrame);
    ~AutoScrolledRectCache();

   private:
    friend class ScrollContainerFrame;
    const nsRect& GetOrCompute();
    // Drop any cached value so the next GetScrolledRect() recomputes it. Used
    // when an operation legitimately changes the scrolled rect while the cache
    // is alive.
    void Invalidate() { mComputed = false; }

    ScrollContainerFrame* const mFrame;
    const nsIFrame* const mReferenceFrame;
    nsRect mScrolledRect;
    bool mComputed = false;
  };

  /**
   * Get the area of the scrollport relative to the origin of this frame's
   * border-box.
   * This is the area of this frame minus border and scrollbars.
   */
  nsRect GetScrollPortRect() const { return mScrollPort; }
  nsRect GetScrollPortRectAccountingForDynamicToolbar() const {
    auto rect = mScrollPort;
    if (mIsRoot) {
      rect.height += PresContext()->GetBimodalDynamicToolbarHeightInAppUnits();
    }
    return rect;
  }
  nsRect GetScrollPortRectAccountingForMaxDynamicToolbar() const;

  nsSize GetScrolledFrameSize() const {
    return mScrolledFrame->GetContentRectRelativeToSelf().Size();
  }

  /**
   * Get the offset of the scrollport origin relative to the scrolled
   * frame origin. Typically the position will be non-negative.
   * This will always be a multiple of device pixels.
   */
  nsPoint GetScrollPosition() const {
    return mScrollPort.TopLeft() - mScrolledFrame->GetPosition();
  }

  /**
   * For LTR frames, the logical scroll position is the offset of the top left
   * corner of the frame from the top left corner of the scroll port (same as
   * GetScrollPosition).
   * For RTL frames, it is the offset of the top right corner of the frame from
   * the top right corner of the scroll port.
   */
  nsPoint GetLogicalScrollPosition() const {
    nsPoint pt;
    pt.x = IsPhysicalLTR()
               ? mScrollPort.x - mScrolledFrame->GetPosition().x
               : mScrollPort.XMost() - mScrolledFrame->GetRect().XMost();
    pt.y = mScrollPort.y - mScrolledFrame->GetPosition().y;
    return pt;
  }

  /**
   * Get the area that must contain the scroll position. Typically
   * (but not always, e.g. for RTL content) x and y will be 0, and
   * width or height will be nonzero if the content can be scrolled in
   * that direction. Since scroll positions must be a multiple of
   * device pixels, the range extrema will also be a multiple of
   * device pixels.
   */
  nsRect GetScrollRange() const { return GetLayoutScrollRange(); }

  /**
   * Get the size of the view port to use when clamping the scroll
   * position.
   */
  nsSize GetVisualViewportSize() const;

  /**
   * Returns the offset of the visual viewport relative to
   * the origin of the scrolled content. Note that only the RCD-RSF
   * has a distinct visual viewport; for other scroll frames, the
   * visual viewport always coincides with the layout viewport, and
   * consequently the offset this function returns is equal to
   * GetScrollPosition().
   */
  nsPoint GetVisualViewportOffset() const;

  /**
   * Set the visual viewport offset associated with a root scroll frame. This is
   * only valid when called on a root scroll frame and will assert otherwise.
   * aRepaint indicates if we need to ask for a main thread paint if this
   * changes scrollbar positions or not. For example, if the compositor has
   * already put the scrollbars at this position then they don't need to move so
   * we can skip the repaint. Returns true if the offset changed and the scroll
   * frame is still alive after this call.
   */
  bool SetVisualViewportOffset(const nsPoint& aOffset, bool aRepaint);
  /**
   * Get the area that must contain the visual viewport offset.
   */
  nsRect GetVisualScrollRange() const;

  /**
   * Like GetVisualScrollRange but also takes into account overflow: hidden.
   */
  nsRect GetScrollRangeForUserInputEvents() const;

  /**
   * Return how much we would try to scroll by in each direction if
   * asked to scroll by one "line" vertically and horizontally.
   */
  nsSize GetLineScrollAmount() const;
  /**
   * Return how much we would try to scroll by in each direction if
   * asked to scroll by one "page" vertically and horizontally.
   */
  nsSize GetPageScrollAmount() const;

  /**
   * Return scroll-padding value of this frame.
   */
  nsMargin GetScrollPadding() const;

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Clamps aScrollPosition to GetScrollRange and sets the scroll position
   * to that value.
   * @param aRange If non-null, specifies area which contains aScrollPosition
   * and can be used for choosing a performance-optimized scroll position.
   * Any point within this area can be chosen.
   * The choosen point will be as close as possible to aScrollPosition.
   */
  void ScrollTo(nsPoint aScrollPosition, ScrollMode aMode,
                const nsRect* aRange = nullptr,
                ScrollSnapFlags aSnapFlags = ScrollSnapFlags::Disabled,
                ScrollTriggeredByScript aTriggeredByScript =
                    ScrollTriggeredByScript::No) {
    return ScrollToInternal(aScrollPosition, aMode, ScrollOrigin::Other, aRange,
                            aSnapFlags, aTriggeredByScript);
  }

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Scrolls to a particular position in integer CSS pixels.
   * Keeps the exact current horizontal or vertical position if the current
   * position, rounded to CSS pixels, matches aScrollPosition. If
   * aScrollPosition.x/y is different from the current CSS pixel position,
   * makes sure we only move in the direction given by the difference.
   *
   * When aMode is SMOOTH, INSTANT, or NORMAL, GetRoundedScrollPositionCSSPixels
   * (the scroll position after rounding to CSS pixels) will be exactly
   * aScrollPosition at the end of the scroll animation.
   *
   * When aMode is SMOOTH_MSD, intermediate animation frames may be outside the
   * range and / or moving in any direction; GetRoundedScrollPositionCSSPixels
   * will be exactly aScrollPosition at the end of the scroll animation unless
   * the SMOOTH_MSD animation is interrupted.
   */
  void ScrollToCSSPixels(const CSSPoint& aScrollPosition,
                         ScrollMode aMode = ScrollMode::Instant);

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Scrolls to a particular position in float CSS pixels.
   * This does not guarantee that GetRoundedScrollPositionCSSPixels equals
   * aScrollPosition afterward. It tries to scroll as close to
   * aScrollPosition as possible while scrolling by an integer
   * number of layer pixels (so the operation is fast and looks clean).
   * Returns false if the frame was destroyed.
   */
  [[nodiscard]] bool ScrollToCSSPixelsForApz(
      const CSSPoint& aScrollPosition,
      ScrollSnapTargetIds&& aLastSnapTargetIds);

  /**
   * Returns the scroll position in integer CSS pixels, rounded to the nearest
   * pixel.
   */
  CSSIntPoint GetRoundedScrollPositionCSSPixels();

  /**
   * Returns the scroll position in CSS pixels.
   */
  CSSPoint GetScrollPositionCSSPixels() const {
    return CSSPoint::FromAppUnits(GetScrollPosition());
  }

  /**
   * Some platforms (OSX) may generate additional scrolling events even
   * after the user has stopped scrolling, simulating a momentum scrolling
   * effect resulting from fling gestures.
   * SYNTHESIZED_MOMENTUM_EVENT indicates that the scrolling is being requested
   * by such a synthesized event and may be ignored if another scroll has
   * been started since the last actual user input.
   */
  enum ScrollMomentum { NOT_MOMENTUM, SYNTHESIZED_MOMENTUM_EVENT };

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Modifies the current scroll position by aDelta units given by aUnit,
   * clamping it to GetScrollRange. If WHOLE is specified as the unit,
   * content is scrolled all the way in the direction(s) given by aDelta.
   * @param aOverflow if non-null, returns the amount that scrolling
   * was clamped by in each direction (how far we moved the scroll position
   * to bring it back into the legal range). This is never negative. The
   * values are in device pixels.
   */
  void ScrollBy(nsIntPoint aDelta, ScrollUnit aUnit, ScrollMode aMode,
                nsIntPoint* aOverflow = nullptr,
                ScrollOrigin aOrigin = ScrollOrigin::NotSpecified,
                ScrollMomentum aMomentum = NOT_MOMENTUM,
                ScrollSnapFlags aSnapFlags = ScrollSnapFlags::Disabled);

  void ScrollByCSSPixels(const CSSPoint& aDelta,
                         ScrollMode aMode = ScrollMode::Instant) {
    return ScrollByCSSPixelsInternal(aDelta, aMode);
  }

  /**
   * Perform scroll snapping, possibly resulting in a smooth scroll to
   * maintain the scroll snap position constraints.  Velocity sampled from
   * main thread scrolling is used to determine best matching snap point
   * when called after a fling gesture on a trackpad or mouse wheel.
   */
  void ScrollSnap() { ScrollSnap(ScrollMode::SmoothMsd); }

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * This tells the scroll frame to try scrolling to the scroll
   * position that was restored from the history. This must be called
   * at least once after state has been restored. It is called by the
   * scrolled frame itself during reflow, but sometimes state can be
   * restored after reflows are done...
   * XXX should we take an aMode parameter here? Currently it's instant.
   */
  void ScrollToRestoredPosition();

  bool NeedRestorePosition() const {
    return mRestorePos.y != -1 && mLastPos.x != -1 && mLastPos.y != -1;
  }

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Internal method used by scrollbars to notify their scrolling
   * container of changes.
   */
  void ScrollbarCurPosChanged(bool aDoScroll = true);

  void DisableOverlayScrollbars();
  void EnableOverlayScrollbars();

  /**
   * Allows the docshell to request that the scroll frame post an event
   * after being restored from history.
   */
  NS_IMETHOD PostScrolledAreaEventForCurrentArea() final {
    PostScrolledAreaEvent();
    return NS_OK;
  }

  /**
   * Returns true if this scrollframe is being "actively scrolled".
   * This basically means that we should allocate resources in the
   * expectation that scrolling is going to happen.
   */
  bool IsScrollingActive() const;

  /**
   * Returns true if this scroll frame might be scrolled
   * asynchronously by the compositor.
   */
  bool IsMaybeAsynchronouslyScrolled() const {
    // If this is true, then we'll build an ASR, and that's what we want
    // to know I think.
    return mWillBuildScrollableLayer;
  }

  /**
   * Was the current presentation state for this frame restored from history?
   */
  bool DidHistoryRestore() const { return mDidHistoryRestore; }

  /**
   * Clear the flag so that DidHistoryRestore() returns false until the next
   * RestoreState call.
   * @see nsIStatefulFrame::RestoreState
   */
  void ClearDidHistoryRestore() { mDidHistoryRestore = false; }

  /**
   * Mark the frame as having been scrolled at least once, so that it remains
   * active and we can also start storing its scroll position when saving state.
   */
  void MarkEverScrolled();

  /**
   * Determine if the passed in rect is nearly visible according to the frame
   * visibility heuristics for how close it is to the visible scrollport.
   */
  bool IsRectNearlyVisible(const nsRect& aRect) const;

  /**
   * Expand the given rect taking into account which directions we can scroll
   * and how far we want to expand for frame visibility purposes.
   */
  nsRect ExpandRectToNearlyVisible(const nsRect& aRect) const;

  /**
   * Returns the origin that triggered the last instant scroll. Will equal
   * ScrollOrigin::Apz when the compositor's replica frame metrics includes the
   * latest instant scroll.
   */
  ScrollOrigin LastScrollOrigin() const { return mLastScrollOrigin; }

  /**
   * Gets the async scroll animation state of this scroll frame.
   *
   * There are five possible kinds that can overlap.
   * MainThread means async scroll animated by the main thread.
   * APZ scroll animations that are requested from the main thread go through
   * three states: 1) pending, when the main thread has recorded that it wants
   * apz to do a scroll animation, 2) requested, when the main thread has sent
   * the request to the compositor (but it hasn't necessarily arrived yet), and
   * 3) in progress, after apz has responded to the main thread that it got the
   * request.
   * TriggeredByScript means that the async scroll animation was triggered by
   * script, e.g. Element.scrollTo().
   */
  enum class AnimationState {
    MainThread,        // mAsyncScroll || mAsyncSmoothMSDScroll
    APZPending,        // mScrollUpdates.LastElement() is Smooth or SmoothMsd
    APZRequested,      // mApzAnimationRequested
    APZInProgress,     // mCurrentAPZScrollAnimationType !=
                       // APZScrollAniationType::No
    TriggeredByScript  // The animation was triggered with
                       // ScrollTriggeredByScript::Yes
  };
  EnumSet<AnimationState> ScrollAnimationState() const;

  /**
   * Returns the current generation counter for the scrollframe. This counter
   * increments every time the scroll position is set.
   */
  MainThreadScrollGeneration CurrentScrollGeneration() const {
    return mScrollGeneration;
  }

  /**
   * The APZ scroll generation associated with the last APZ scroll offset for
   * which we processed a repaint request.
   */
  APZScrollGeneration ScrollGenerationOnApz() const {
    return mScrollGenerationOnApz;
  }

  /**
   * LastScrollDestination returns the destination of the most recently
   * requested smooth scroll animation.
   */
  nsPoint LastScrollDestination() { return mDestination; }

  /**
   * Returns the list of scroll position updates since the last call to
   * NotifyApzTransaction().
   */
  nsTArray<ScrollPositionUpdate> GetScrollUpdates() const;

  /**
   * Returns true if the scroll frame has any scroll position updates since
   * the last call to NotifyApzTransaction().
   */
  bool HasScrollUpdates() const { return !mScrollUpdates.IsEmpty(); }

  /**
   * Clears the "origin of last scroll" property stored in this frame, if
   * the generation counter passed in matches the current scroll generation
   * counter, and clears the "origin of last smooth scroll" property if the
   * generation counter matches. It also resets whether there's an ongoing apz
   * animation.
   */
  enum class InScrollingGesture : bool { No, Yes };
  void ResetScrollInfoIfNeeded(const MainThreadScrollGeneration& aGeneration,
                               const APZScrollGeneration& aGenerationOnApz,
                               APZScrollAnimationType aAPZScrollAnimationType,
                               InScrollingGesture aInScrollingGesture);

  /**
   * Determine whether it is desirable to be able to asynchronously scroll this
   * scroll frame.
   *
   * NonZeroScrollRangeOnly::No, this function returns true for scroll container
   * whose overscroll-behavior properties are not default even if the container
   * is not scrollable in the direction, overflow: hidden or it's not overflowed
   * in the direction. In other words, NonZeroScrollRangeOnly::Yes, this
   * functions returns false in such cases since the container is zero scroll
   * range, thus it needs no displayport properties.
   */
  enum class NonZeroScrollRangeOnly : bool { No, Yes };
  bool WantAsyncScroll(NonZeroScrollRangeOnly aNonZeroScrollRangeOnly =
                           NonZeroScrollRangeOnly::No) const;

  /**
   * Returns the ScrollMetadata contributed by this frame, if there is one.
   */
  Maybe<layers::ScrollMetadata> ComputeScrollMetadata(
      layers::WebRenderLayerManager* aLayerManager, const nsIFrame* aItemFrame,
      const nsPoint& aOffsetToReferenceFrame) const;

  /**
   * Mark the scrollbar frames for reflow.
   */
  void MarkScrollbarsDirtyForReflow() const;

  /**
   * Invalidate the scrollbar after the marks have been changed.
   */
  void InvalidateScrollbars() const;

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * Update scrollbar to reflect current scroll position
   */
  void UpdateScrollbarPosition();

  void SetTransformingByAPZ(bool aTransforming);
  bool IsTransformingByAPZ() const { return mTransformingByAPZ; }

  /**
   * Notify this scroll frame that it can be scrolled by APZ. In particular,
   * this is called *after* the APZ code has created an APZC for this scroll
   * frame and verified that it is not a scrollinfo layer. Therefore, setting an
   * async transform on it is actually user visible.
   */
  void SetScrollableByAPZ(bool aScrollable);

  /**
   * Notify this scroll frame that it can be zoomed by APZ.
   */
  void SetZoomableByAPZ(bool aZoomable);

  /**
   * Mark this scroll frame as having out-of-flow content inside a CSS filter.
   * Such content will move incorrectly during async-scrolling; to mitigate
   * this, paint skipping is disabled for such scroll frames.
   */
  void SetHasOutOfFlowContentInsideFilter();

  /**
   * Determine if we should build a scrollable layer for this scroll frame and
   * return the result. It will also record this result on the scroll frame.
   * Pass the visible rect in aVisibleRect. On return it will be set to the
   * displayport if there is one.
   * Pass the dirty rect in aDirtyRect. On return it will be set to the
   * dirty rect inside the displayport (ie the dirty rect that should be used).
   * This function will set the display port base rect if aSetBase is true.
   * aSetBase is only allowed to be false if there has been a call with it
   * set to true before on the same paint.
   */
  bool DecideScrollableLayer(nsDisplayListBuilder* aBuilder,
                             nsRect* aVisibleRect, nsRect* aDirtyRect,
                             bool aSetBase) {
    return DecideScrollableLayer(aBuilder, aVisibleRect, aDirtyRect, aSetBase,
                                 nullptr);
  }

  /**
   * Like DecideScrollableLayer but skips computing dirty/visible rects and
   * setting the base rect. In addition, this always ensures a display port is
   * set if this scroll frame could async scroll. DecideScrollableLayer only
   * sets a display port in that situation if ShouldActivateAllScrollFrames
   * returns true. However you should only call this function is
   * ShouldActivateAllScrollFrames returns true anyways.
   * */
  bool DecideScrollableLayerEnsureDisplayport(nsDisplayListBuilder* aBuilder);

  /**
   * Notify the scrollframe that the current scroll offset and origin have been
   * sent over in a layers transaction.
   *
   * This sets a flag on the scrollframe that indicates subsequent changes
   * to the scroll position by "weaker" origins are permitted to overwrite the
   * the scroll origin. Scroll origins that
   * nsLayoutUtils::CanScrollOriginClobberApz returns false for are considered
   * "weaker" than scroll origins for which that function returns true.
   *
   * This function must be called for a scrollframe after all calls to
   * ComputeScrollMetadata in a layers transaction have been completed.
   *
   */
  void NotifyApzTransaction();

  /**
   * Notification that this scroll frame is getting its frame visibility
   * updated. aIgnoreDisplayPort indicates that the display port was ignored
   * (because there was no suitable base rect)
   */
  void NotifyApproximateFrameVisibilityUpdate(bool aIgnoreDisplayPort);

  /**
   * Returns true if this scroll frame had a display port at the last frame
   * visibility update and fills in aDisplayPort with that displayport. Returns
   * false otherwise, and doesn't touch aDisplayPort.
   */
  bool GetDisplayPortAtLastApproximateFrameVisibilityUpdate(
      nsRect* aDisplayPort);

  /**
   * This is called when a descendant scrollframe's has its displayport expired.
   * This function will check to see if this scrollframe may safely expire its
   * own displayport and schedule a timer to do that if it is safe.
   */
  void TriggerDisplayPortExpiration();

  /**
   * Returns information required to determine where to snap to after a scroll.
   */
  ScrollSnapInfo GetScrollSnapInfo();

  void TryResnap();

  /**
   * Post a pending re-snap request if the given |aFrame| is one of the snap
   * points on the last scroll operation.
   */
  void PostPendingResnapIfNeeded(const nsIFrame* aFrame);
  void PostPendingResnap();

  /**
   * Returns a pair of the scroll-snap-align property value both on X and Y axes
   * for the given |aFrame| considering the scroll-snap-type of this scroll
   * container. For example, if the scroll-snap-type is `none`, the pair of
   * scroll-snap-align is also `none none`.
   */
  using PhysicalScrollSnapAlign =
      std::pair<StyleScrollSnapAlignKeyword, StyleScrollSnapAlignKeyword>;
  PhysicalScrollSnapAlign GetScrollSnapAlignFor(const nsIFrame* aFrame) const;

  /**
   * Given the drag event aEvent, determine whether the mouse is near the edge
   * of the scrollable area, and scroll the view in the direction of that edge
   * if so. If scrolling occurred, true is returned. When false is returned, the
   * caller should look for an ancestor to scroll.
   */
  bool DragScroll(WidgetEvent* aEvent);

  void AsyncScrollbarDragInitiated(uint64_t aDragBlockId,
                                   layers::ScrollDirection aDirection);
  void AsyncScrollbarDragRejected();

  /**
   * Returns whether this scroll frame is the root scroll frame of the document
   * that it is in. Note that some documents don't have root scroll frames at
   * all (ie XUL documents) even though they may contain other scroll frames.
   */
  bool IsRootScrollFrameOfDocument() const { return mIsRoot; }

  /**
   * Returns the paint sequence number if this scroll frame was the first
   * scrollable frame for the paint.
   */
  Maybe<uint32_t> IsFirstScrollableFrameSequenceNumber() const {
    return mIsFirstScrollableFrameSequenceNumber;
  }

  /**
   * Set the paint sequence number for the paint in which this was the first
   * scrollable frame that was encountered.
   */
  void SetIsFirstScrollableFrameSequenceNumber(Maybe<uint32_t> aValue) {
    mIsFirstScrollableFrameSequenceNumber = aValue;
  }

  /**
   * Returns the scroll anchor associated with this scrollable frame. This is
   * never null.
   */
  const ScrollAnchorContainer* Anchor() const { return &mAnchor; }
  ScrollAnchorContainer* Anchor() { return &mAnchor; }

  bool SmoothScrollVisual(const nsPoint& aVisualViewportOffset,
                          layers::ScrollOffsetUpdateType aUpdateType,
                          ScrollMode aMode);

  /**
   * Returns true if this scroll frame should perform smooth scroll with the
   * given |aBehavior|.
   */
  bool IsSmoothScroll(
      dom::ScrollBehavior aBehavior = dom::ScrollBehavior::Auto) const;

  /**
   * Returns the ScrollMode that this scroll frame should use for a
   * programmatic scroll governed by CSSOM-View `scroll-behavior`.
   * This includes most programmatic scrolls but not scroll snapping.
   */
  ScrollMode ScrollModeForScrollBehavior(
      dom::ScrollBehavior aBehavior = dom::ScrollBehavior::Auto) const;

  static nscoord GetNonOverlayScrollbarSize(const nsPresContext*,
                                            StyleScrollbarWidth);

  void ScrollByCSSPixelsInternal(
      const CSSPoint& aDelta, ScrollMode aMode = ScrollMode::Instant,
      // This ScrollByCSSPixels is mainly used for Element.scrollBy and
      // Window.scrollBy. An exception is the transmogrification of
      // ScrollToCSSPixels.
      ScrollSnapFlags aSnapFlags = ScrollSnapFlags::IntendedDirection |
                                   ScrollSnapFlags::IntendedEndPosition);

  // nsIReflowCallback
  bool ReflowFinished() override;
  void ReflowCallbackCanceled() final;

  // nsIStatefulFrame
  UniquePtr<PresState> SaveState() final;
  NS_IMETHOD RestoreState(PresState* aState) final;

  // nsIScrollbarMediator
  void ScrollByPage(
      nsScrollbarFrame* aScrollbar, int32_t aDirection,
      ScrollSnapFlags aSnapFlags = ScrollSnapFlags::Disabled) final;
  void ScrollByWhole(
      nsScrollbarFrame* aScrollbar, int32_t aDirection,
      ScrollSnapFlags aSnapFlags = ScrollSnapFlags::Disabled) final;
  void ScrollByLine(nsScrollbarFrame* aScrollbar, int32_t aDirection,
                    ScrollSnapFlags = ScrollSnapFlags::Disabled) final;
  void ScrollByUnit(nsScrollbarFrame* aScrollbar, ScrollMode aMode,
                    int32_t aDirection, ScrollUnit aUnit,
                    ScrollSnapFlags = ScrollSnapFlags::Disabled) final;
  void RepeatButtonScroll(nsScrollbarFrame* aScrollbar) final;
  void ThumbMoved(nsScrollbarFrame* aScrollbar, nscoord aOldPos,
                  nscoord aNewPos) final;
  void ScrollbarReleased(nsScrollbarFrame* aScrollbar) final;
  void VisibilityChanged(bool aVisible) final {}
  nsScrollbarFrame* GetScrollbarBox(bool aVertical) final {
    return aVertical ? mVScrollbarBox : mHScrollbarBox;
  }
  void ScrollbarActivityStarted() const final;
  void ScrollbarActivityStopped() const final;
  bool IsScrollbarOnRight() const final;
  bool ShouldSuppressScrollbarRepaints() const final {
    return mSuppressScrollbarRepaints;
  }

  // Return the scrolled frame.
  void AppendDirectlyOwnedAnonBoxes(nsTArray<OwnedAnonBox>& aResult) final {
    aResult.AppendElement(OwnedAnonBox(GetScrolledFrame()));
  }

#ifdef DEBUG_FRAME_DUMP
  nsresult GetFrameName(nsAString& aResult) const override;
#endif

#ifdef ACCESSIBILITY
  a11y::AccType AccessibleType() override;
#endif

  static void AsyncScrollCallback(ScrollContainerFrame* aInstance,
                                  TimeStamp aTime);
  static void AsyncSmoothMSDScrollCallback(ScrollContainerFrame* aInstance,
                                           TimeDuration aDeltaTime);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   * aRange is the range of allowable scroll positions around the desired
   * aScrollPosition. Null means only aScrollPosition is allowed.
   * This is a closed-ended range --- aRange.XMost()/aRange.YMost() are allowed.
   */
  void ScrollToInternal(
      nsPoint aScrollPosition, ScrollMode aMode,
      ScrollOrigin aOrigin = ScrollOrigin::NotSpecified,
      const nsRect* aRange = nullptr,
      ScrollSnapFlags aSnapFlags = ScrollSnapFlags::Disabled,
      ScrollTriggeredByScript aTriggeredByScript = ScrollTriggeredByScript::No);
  /**
   * @note This method might destroy the frame, pres shell and other objects.
   */
  void ScrollToImpl(
      nsPoint aPt, const nsRect& aRange,
      ScrollOrigin aOrigin = ScrollOrigin::NotSpecified,
      ScrollTriggeredByScript aTriggeredByScript = ScrollTriggeredByScript::No);
  void ScrollVisual();

  enum class LoadingState { Loading, Stopped, Loaded };

  LoadingState GetPageLoadingState();

  /**
   * GetSnapPointForDestination determines which point to snap to after
   * scrolling. aStartPos gives the position before scrolling and aDestination
   * gives the position after scrolling, with no snapping. Behaviour is
   * dependent on the value of aUnit.
   * Returns true if a suitable snap point could be found and aDestination has
   * been updated to a valid snapping position.
   */
  Maybe<SnapDestination> GetSnapPointForDestination(
      ScrollUnit aUnit, ScrollSnapFlags aFlags, const nsPoint& aStartPos,
      const nsPoint& aDestination);

  Maybe<SnapDestination> GetSnapPointForResnap();
  bool NeedsResnap();

  void SetLastSnapTargetIds(UniquePtr<ScrollSnapTargetIds> aId);

  static void SetScrollbarVisibility(nsIFrame* aScrollbar, bool aVisible);

  /**
   * GetUnsnappedScrolledRectInternal is designed to encapsulate deciding which
   * directions of overflow should be reachable by scrolling and which
   * should not.  Callers should NOT depend on it having any particular
   * behavior.
   *
   * Currently it allows scrolling down and to the right for
   * ScrollContainerFrames with LTR directionality, and allows scrolling down
   * and to the left for ScrollContainerFrames with RTL directionality.
   */
  nsRect GetUnsnappedScrolledRectInternal(const nsRect& aScrolledOverflowArea,
                                          const nsSize& aScrollPortSize) const;

  /**
   * Computes the scrolled rect (for GetScrolledRect), snapping to layer pixels
   * relative to aReferenceFrame, or relative to
   * nsLayoutUtils::GetReferenceFrame(this) if aReferenceFrame is null. This is
   * the uncached implementation backing GetScrolledRect() and
   * AutoScrolledRectCache.
   */
  nsRect ComputeScrolledRect(const nsIFrame* aReferenceFrame) const;

  bool IsPhysicalLTR() const { return GetWritingMode().IsPhysicalLTR(); }
  bool IsBidiLTR() const { return GetWritingMode().IsBidiLTR(); }

  bool IsAlwaysActive() const;
  void MarkRecentlyScrolled();
  void MarkNotRecentlyScrolled();
  nsExpirationState* GetExpirationState() { return &mActivityExpirationState; }

  bool UseOverlayScrollbars() const;

  // NOTE: |aStyle| needs to be the computed styles for this scroll container,
  // not for the scrollbars.
  StyleScrollbarWidth ScrollbarWidth(
      const ComputedStyle* aStyle = nullptr) const;

  bool IsLastSnappedTarget(const nsIFrame* aFrame) const;

  // If aBuilder is non-null, returns the value cached on aBuilder. Pass null
  // for aBuilder to get the correct value to cache on a new builder or new
  // frame of painting, or if you need the correct value outside of paint time.
  static bool ShouldActivateAllScrollFrames(nsDisplayListBuilder* aBuilder,
                                            nsIFrame* aFrame);
  nsRect RestrictToRootDisplayPort(const nsRect& aDisplayportBase);
  bool DecideScrollableLayer(nsDisplayListBuilder* aBuilder,
                             nsRect* aVisibleRect, nsRect* aDirtyRect,
                             bool aSetBase, bool* aDirtyRectHasBeenOverriden);
  bool AllowDisplayPortExpiration();
  void ResetDisplayPortExpiryTimer();

  void ScheduleSyntheticMouseMove();
  static void ScrollActivityCallback(nsITimer* aTimer, void* anInstance);

  void HandleScrollbarStyleSwitching();

  bool IsApzAnimationInProgress() const {
    return mCurrentAPZScrollAnimationType != APZScrollAnimationType::No;
  }
  nsPoint LastScrollDestination() const { return mDestination; }

  bool IsLastScrollUpdateAnimating() const;
  bool IsLastScrollUpdateTriggeredByScriptAnimating() const;

  // Update minimum-scale size.  The minimum-scale size will be set/used only
  // if there is overflow-x:hidden region.
  void UpdateMinimumScaleSize(const nsRect& aScrollableOverflow,
                              const nsSize& aICBSize);

  // Return the scroll frame's "true outer size".
  // This is GetSize(), except when we've been sized to reflect a virtual
  // (layout) viewport in which case this returns the outer size used to size
  // the physical (visual) viewport.
  nsSize TrueOuterSize(nsDisplayListBuilder* aBuilder) const;

  already_AddRefed<Element> MakeScrollbar(dom::NodeInfo* aNodeInfo,
                                          bool aVertical,
                                          AnonymousContentKey& aKey);

  void AppendScrollUpdate(const ScrollPositionUpdate& aUpdate);

  bool HasBeenScrolled() const { return mHasBeenScrolled; }

 protected:
  ScrollContainerFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                       bool aIsRoot)
      : ScrollContainerFrame(aStyle, aPresContext, kClassID, aIsRoot) {}
  ScrollContainerFrame(ComputedStyle* aStyle, nsPresContext* aPresContext,
                       nsIFrame::ClassID aID, bool aIsRoot);
  ~ScrollContainerFrame();
  bool GuessHScrollbarNeeded(const ScrollReflowInput& aState);
  bool GuessVScrollbarNeeded(const ScrollReflowInput& aState);

  // Return whether we're in an "initial" reflow.  Some reflows with
  // NS_FRAME_FIRST_REFLOW set are NOT "initial" as far as we're concerned.
  bool InInitialReflow() const;

  bool TryLayout(ScrollReflowInput& aState, ReflowOutput* aKidMetrics,
                 bool aAssumeHScroll, bool aAssumeVScroll, bool aForce);

  // Return true if ReflowScrolledFrame is going to do something different based
  // on the presence of a horizontal scrollbar in a horizontal writing mode or a
  // vertical scrollbar in a vertical writing mode.
  bool ScrolledContentDependsOnBSize(const ScrollReflowInput& aState) const;

  void ReflowScrolledFrame(ScrollReflowInput& aState, bool aAssumeHScroll,
                           bool aAssumeVScroll, ReflowOutput* aMetrics);
  void ReflowContents(ScrollReflowInput& aState,
                      const ReflowOutput& aDesiredSize);
  void PlaceScrollArea(ScrollReflowInput& aState,
                       const nsPoint& aScrollPosition);

  void UpdateSticky();
  void UpdatePrevScrolledRect();

  // adjust the scrollbar rectangle aRect to account for any visible resizer.
  // aHasResizer specifies if there is a content resizer, however this method
  // will also check if a widget resizer is present as well.
  void AdjustScrollbarRectForResizer(nsIFrame* aFrame,
                                     nsPresContext* aPresContext, nsRect& aRect,
                                     bool aHasResizer,
                                     layers::ScrollDirection aDirection);
  void LayoutScrollbars(ScrollReflowInput& aState,
                        const nsRect& aInsideBorderArea,
                        const nsRect& aOldScrollPort);
  void LayoutButtonBox(const ScrollReflowInput& aState, nsIFrame* aButtonBox);

  void LayoutScrollbarPartAtRect(const ScrollReflowInput&,
                                 ReflowInput& aKidReflowInput, const nsRect&);

  PhysicalAxes GetOverflowAxes() const;

  MOZ_CAN_RUN_SCRIPT nsresult FireScrollPortEvent();
  void PostScrollEndEvent();
  void PostOrDeferScrollEndEvent();
  MOZ_CAN_RUN_SCRIPT void FireScrollEndEvent();
  void PostOverflowEvent();

  // Add display items for the top-layer (which includes things like
  // the fullscreen element, its backdrop, and text selection carets)
  // to |aLists|.
  // This is a no-op for scroll frames other than the viewport's
  // root scroll frame.
  // This should be called with an nsDisplayListSet that will be
  // wrapped in the async zoom container, if we're building one.
  // It should not be called with an ASR setter on the stack, as the
  // top-layer items handle setting up their own ASRs.
  void MaybeCreateTopLayerAndWrapRootItems(
      nsDisplayListBuilder*, nsDisplayListCollection&, bool aCreateAsyncZoom,
      bool aCapturedByViewTransition,
      AutoContainsBlendModeCapturer* aAsyncZoomBlendCapture,
      const nsRect& aAsyncZoomClipRect, const nsRectCornerRadii* aRadii);

  void AppendScrollPartsTo(nsDisplayListBuilder* aBuilder,
                           const nsDisplayListSet& aLists, bool aCreateLayer,
                           bool aPositioned);

  void PostScrollEvent();
  MOZ_CAN_RUN_SCRIPT void FireScrollEvent();
  void PostScrolledAreaEvent();
  MOZ_CAN_RUN_SCRIPT void FireScrolledAreaEvent();

  void FinishReflowForScrollbar(nsScrollbarFrame*, nscoord aMinXY,
                                nscoord aMaxXY, nscoord aCurPosXY,
                                nscoord aPageIncrement);
  void ActivityOccurred();

  nsRect GetLayoutScrollRange() const;
  // Get the scroll range assuming the viewport has size (aWidth, aHeight).
  nsRect GetScrollRange(nscoord aWidth, nscoord aHeight) const;

  const nsRect& ScrollPort() const { return mScrollPort; }
  void SetScrollPort(const nsRect& aNewScrollPort) {
    if (!mScrollPort.IsEqualEdges(aNewScrollPort)) {
      mMayScheduleScrollAnimations = true;
    }
    mScrollPort = aNewScrollPort;
  }

  // Gets the node that is a suitable scroll event target for our events.
  enum class RootTargetsDocument : bool { No, Yes };
  RefPtr<nsINode> ScrollEventTargetNode(RootTargetsDocument) const;

  /**
   * Return the 'optimal viewing region' as a rect suitable for use by
   * scroll anchoring. This rect is in the same coordinate space as
   * 'GetScrollPortRect', and accounts for 'scroll-padding' as defined by:
   *
   * https://drafts.csswg.org/css-scroll-snap-1/#optimal-viewing-region
   */
  nsRect GetVisualOptimalViewingRect() const;

  /**
   * For LTR frames, this is the same as GetVisualViewportOffset().
   * For RTL frames, we take the offset from the top right corner of the frame
   * to the top right corner of the visual viewport.
   */
  nsPoint GetLogicalVisualViewportOffset() const {
    nsPoint pt = GetVisualViewportOffset();
    if (!IsPhysicalLTR()) {
      pt.x += GetVisualViewportSize().width - mScrolledFrame->GetRect().width;
    }
    return pt;
  }
  bool ScrollSnap(ScrollMode aMode);
  bool ScrollSnap(const nsPoint& aDestination,
                  ScrollMode aMode = ScrollMode::SmoothMsd);

  bool HasPendingScrollRestoration() const {
    return mRestorePos != nsPoint(-1, -1);
  }

  bool IsProcessingScrollEvent() const { return mProcessingScrollEvent; }

  class AutoScrollbarRepaintSuppression;
  friend class AutoScrollbarRepaintSuppression;
  class AutoScrollbarRepaintSuppression {
   public:
    AutoScrollbarRepaintSuppression(ScrollContainerFrame* aFrame,
                                    AutoWeakFrame& aWeakOuter, bool aSuppress)
        : mFrame(aFrame),
          mWeakOuter(aWeakOuter),
          mOldSuppressValue(aFrame->mSuppressScrollbarRepaints) {
      mFrame->mSuppressScrollbarRepaints = aSuppress;
    }

    ~AutoScrollbarRepaintSuppression() {
      if (mWeakOuter.IsAlive()) {
        mFrame->mSuppressScrollbarRepaints = mOldSuppressValue;
      }
    }

   private:
    ScrollContainerFrame* mFrame;
    AutoWeakFrame& mWeakOuter;
    bool mOldSuppressValue;
  };

  struct ScrollOperationParams {
    ScrollOperationParams(const ScrollOperationParams&) = delete;
    ScrollOperationParams(ScrollMode aMode, ScrollOrigin aOrigin)
        : mMode(aMode), mOrigin(aOrigin) {}
    ScrollOperationParams(ScrollMode aMode, ScrollOrigin aOrigin,
                          ScrollSnapTargetIds&& aSnapTargetIds)
        : ScrollOperationParams(aMode, aOrigin) {
      mTargetIds = std::move(aSnapTargetIds);
    }
    ScrollOperationParams(ScrollMode aMode, ScrollOrigin aOrigin,
                          ScrollSnapFlags aSnapFlags,
                          ScrollTriggeredByScript aTriggeredByScript)
        : ScrollOperationParams(aMode, aOrigin) {
      mSnapFlags = aSnapFlags;
      mTriggeredByScript = aTriggeredByScript;
    }

    ScrollMode mMode;
    ScrollOrigin mOrigin;
    ScrollSnapFlags mSnapFlags = ScrollSnapFlags::Disabled;
    ScrollTriggeredByScript mTriggeredByScript = ScrollTriggeredByScript::No;
    ScrollSnapTargetIds mTargetIds;

    bool IsInstant() const { return mMode == ScrollMode::Instant; }
    bool IsSmoothMsd() const { return mMode == ScrollMode::SmoothMsd; }
    bool IsSmooth() const { return mMode == ScrollMode::Smooth; }
    bool IsScrollSnapDisabled() const {
      return mSnapFlags == ScrollSnapFlags::Disabled;
    }
  };

  /**
   * @note This method might destroy the frame, pres shell and other objects.
   *
   * A caller can ask this ScrollToWithOrigin() function to perform snapping by
   * passing in aParams.mSnapFlags != ScrollSnapFlags::Disabled. Alternatively,
   * a caller may want to do its own snapping, in which case it should pass
   * ScrollSnapFlags::Disabled and populate aParams.mTargetIds based on the
   * result of the snapping.
   */
  void ScrollToWithOrigin(nsPoint aScrollPosition, const nsRect* aRange,
                          ScrollOperationParams&& aParams);

  void CompleteAsyncScroll(const nsPoint& aStartPosition, const nsRect& aRange,
                           UniquePtr<ScrollSnapTargetIds> aSnapTargetIds,
                           ScrollOrigin aOrigin = ScrollOrigin::NotSpecified);

  bool SliderFrameInClickAndHold() const;
  bool HasPerspective() const { return ChildrenHavePerspective(); }
  bool HasBgAttachmentLocal() const;
  StyleDirection GetScrolledFrameDir() const;

  // Ask APZ to smooth scroll to |aDestination|.
  // This method does not clamp the destination; callers should clamp it to
  // either the layout or the visual scroll range (APZ will happily smooth
  // scroll to either).
  void ApzSmoothScrollTo(const nsPoint& aDestination, ScrollMode, ScrollOrigin,
                         ScrollTriggeredByScript,
                         UniquePtr<ScrollSnapTargetIds> aSnapTargetIds,
                         ViewportType aViewportToScroll);

  // Check whether APZ can scroll in the provided directions, keeping in mind
  // that APZ currently cannot scroll along axes which are overflow:hidden.
  bool CanApzScrollInTheseDirections(layers::ScrollDirections aDirections);

  // Removes any RefreshDriver observers we might have registered.
  void RemoveObservers();

 private:
  static StyleDirection GetScrolledFrameDir(const nsIFrame*,
                                            bool aForTextInput);

  class AsyncScroll;
  class AsyncSmoothMSDScroll;
  class AutoMinimumScaleSizeChangeDetector;

  enum class AnonymousContentType {
    VerticalScrollbar,
    HorizontalScrollbar,
    Resizer,
  };
  EnumSet<AnonymousContentType> GetNeededAnonymousContent() const;
  EnumSet<AnonymousContentType> GetCurrentAnonymousContent() const;

  // If a child frame was added or removed on the scrollframe,
  // reload our child frame list.
  // We need this if a scrollbar frame is recreated.
  void ReloadChildFrames();

  // NOTE: Use GetScrollStyles() if you want `overflow` property info.
  nsIFrame* GetFrameForStyle() const;

  // Compute all scroll snap related information and store eash snap target
  // element in |mSnapTargets|.
  ScrollSnapInfo ComputeScrollSnapInfo();

  bool NeedsScrollSnap() const;

  // Returns the snapport size of this scroll container.
  // https://drafts.csswg.org/css-scroll-snap/#scroll-snapport
  nsSize GetSnapportSize() const;

  // Schedule the scroll-driven animations.
  void ScheduleScrollAnimations();
  void TryScheduleScrollAnimations() {
    if (!mMayScheduleScrollAnimations) {
      return;
    }
    ScheduleScrollAnimations();
    mMayScheduleScrollAnimations = false;
  }

  static void RemoveDisplayPortCallback(nsITimer* aTimer, void* aClosure);

  // owning references to the nsIAnonymousContentCreator-built content
  nsCOMPtr<Element> mHScrollbarContent;
  nsCOMPtr<Element> mVScrollbarContent;
  nsCOMPtr<Element> mScrollCornerContent;
  nsCOMPtr<Element> mResizerContent;

  class ScrollEvent;
  class ScrollEndEvent;
  class AsyncScrollPortEvent;
  class ScrolledAreaEvent;

  RefPtr<ScrollEvent> mScrollEvent;
  RefPtr<ScrollEndEvent> mScrollEndEvent;
  nsRevocableEventPtr<AsyncScrollPortEvent> mAsyncScrollPortEvent;
  nsRevocableEventPtr<ScrolledAreaEvent> mScrolledAreaEvent;
  nsScrollbarFrame* mHScrollbarBox;
  nsScrollbarFrame* mVScrollbarBox;
  nsIFrame* mScrolledFrame;
  nsIFrame* mScrollCornerBox;
  nsIFrame* mResizerBox;
  // Non-null while an AutoScrolledRectCache is active for this frame; the cache
  // itself lives in that stack object. See AutoScrolledRectCache.
  AutoScrolledRectCache* mScrolledRectCache;
  RefPtr<AsyncScroll> mAsyncScroll;
  RefPtr<AsyncSmoothMSDScroll> mAsyncSmoothMSDScroll;
  RefPtr<layout::ScrollbarActivity> mScrollbarActivity;
  // Placed immediately before the 12-byte mApzSmoothScrollDestination so this
  // 4-byte field fills what would otherwise be alignment padding.
  nsExpirationState mActivityExpirationState;
  Maybe<nsPoint> mApzSmoothScrollDestination;
  MainThreadScrollGeneration mScrollGeneration;
  APZScrollGeneration mScrollGenerationOnApz;

  nsTArray<ScrollPositionUpdate> mScrollUpdates;

  // The minimum-scale size, this is specific to mobile environments where the
  // initial-scale can be less than 1.0.
  // See
  // https://github.com/bokand/bokand.github.io/blob/master/web_viewports_explainer.md#minimum-scale
  // for details.
  nsSize mMinimumScaleSize;

  // Stores the ICB size for the root document if this frame is using the
  // minimum scale size for |mScrollPort|.
  nsSize mICBSize;

  // Where we're currently scrolling to, if we're scrolling asynchronously.
  // If we're not in the middle of an asynchronous scroll then this is
  // just the current scroll position. ScrollBy will choose its
  // destination based on this value.
  nsPoint mDestination;

  // A goal position to try to scroll to as content loads. As long as mLastPos
  // matches the current logical scroll position, we try to scroll to
  // mRestorePos after every reflow --- because after each time content is
  // loaded/added to the scrollable element, there will be a reflow.
  // Note that for frames where layout and visual viewport aren't one and the
  // same thing, this scroll position will be the logical scroll position of
  // the *visual* viewport, as its position will be more relevant to the user.
  nsPoint mRestorePos;
  // The last logical position we scrolled to while trying to restore
  // mRestorePos, or 0,0 when this is a new frame. Set to -1,-1 once we've
  // scrolled for any reason other than trying to restore mRestorePos.
  // Just as with mRestorePos, this position will be the logical position of
  // the *visual* viewport where available.
  nsPoint mLastPos;

  // The latest scroll position we've sent or received from APZ. This
  // represents the main thread's best knowledge of the APZ scroll position,
  // and is used to calculate relative scroll offset updates.
  nsPoint mApzScrollPos;

  nsCOMPtr<nsITimer> mScrollActivityTimer;

  // The scroll position where we last updated frame visibility.
  nsPoint mLastUpdateFramesPos;
  nsRect mDisplayPortAtLastFrameUpdate;

  nsRect mPrevScrolledRect;

  layers::ScrollableLayerGuid::ViewID mScrollParentID;

  // Timer to remove the displayport some time after scrolling has stopped
  nsCOMPtr<nsITimer> mDisplayPortExpiryTimer;

  ScrollAnchorContainer mAnchor;

  // We keep holding a strong reference for each snap target element until the
  // next snapping happens so that it avoids using the same nsIContent* pointer
  // for newly created contents in this scroll container. Otherwise we will try
  // to match different nsIContent(s) generated at the same address during
  // re-snapping.
  SnapTargetSet mSnapTargets;

  // The paint sequence number if the scroll frame is the first scrollable frame
  // encountered.
  Maybe<uint32_t> mIsFirstScrollableFrameSequenceNumber;

  // Computed style of ::webkit-scrollbar pseudo element for this scroll
  // container.
  RefPtr<ComputedStyle> mWebKitScrollbarStyle;

  // mCurrentAPZScrollAnimationType and mLastScrollOrigin are grouped here, just
  // before the bitfields, so these small fields pack together rather than
  // leaving padding between larger 8-byte-aligned members.

  // Representing there's an APZ animation is in progress and what caused the
  // animation. Note that this is only set when repainted via APZ, which means
  // that there may be a request for an APZ animation in flight for example,
  // while this is still `No`. In order to answer "is an APZ animation in the
  // process of starting or in progress" you need to check mScrollUpdates,
  // mApzAnimationRequested, and this type.
  APZScrollAnimationType mCurrentAPZScrollAnimationType;

  ScrollOrigin mLastScrollOrigin;

  // Representing whether the APZC corresponding to this frame is now in the
  // middle of handling a gesture (e.g. a pan gesture).
  InScrollingGesture mInScrollingGesture : 1;

  bool mAllowScrollOriginDowngrade : 1;
  bool mHadDisplayPortAtLastFrameUpdate : 1;

  // True if the most recent reflow of the scroll container frame has
  // the vertical scrollbar shown.
  bool mHasVerticalScrollbar : 1;
  // True if the most recent reflow of the scroll container frame has the
  // horizontal scrollbar shown.
  bool mHasHorizontalScrollbar : 1;

  // If mHas(Vertical|Horizontal)Scrollbar is true then
  // mOnlyNeed(V|H)ScrollbarToScrollVVInsideLV indicates if the only reason we
  // need that scrollbar is to scroll the visual viewport inside the layout
  // viewport. These scrollbars are special in that even if they are layout
  // scrollbars they do not take up any layout space.
  bool mOnlyNeedVScrollbarToScrollVVInsideLV : 1;
  bool mOnlyNeedHScrollbarToScrollVVInsideLV : 1;
  bool mFrameIsUpdatingScrollbar : 1;
  bool mDidHistoryRestore : 1;
  // Is this the scrollframe for the document's viewport?
  bool mIsRoot : 1;
  // If true, we skipped a scrollbar layout due to mSuppressScrollbarUpdate
  // being set at some point.  That means we should lay out scrollbars even if
  // it might not strictly be needed next time mSuppressScrollbarUpdate is
  // false.
  bool mSkippedScrollbarLayout : 1;

  bool mHadNonInitialReflow : 1;
  // Initially true; first call to ReflowFinished() sets it to false.
  bool mFirstReflow : 1;
  // State used only by PostScrollEvents so we know
  // which overflow states have changed.
  bool mHorizontalOverflow : 1;
  bool mVerticalOverflow : 1;
  bool mPostedReflowCallback : 1;
  bool mMayHaveDirtyFixedChildren : 1;
  // If true, need to actually update our scrollbar attributes in the
  // reflow callback.
  bool mUpdateScrollbarAttributes : 1;
  // If true, we should be prepared to scroll using this scrollframe
  // by placing descendant content into its own layer(s)
  bool mHasBeenScrolledRecently : 1;

  // If true, the scroll frame should always be active because we always build
  // a scrollable layer. Used for asynchronous scrolling.
  bool mWillBuildScrollableLayer : 1;

  // True if, as of the previous paint, this scroll frame was async-inactive
  // (ie no displayport) but had descendant async-active scroll frames.
  bool mInactiveWithActiveDescendantScrollFrames : 1;

  // True if this reflow changed our scroll port or scrolled area bounds (the
  // amount of scrollable space). Set in Reflow and used in ReflowFinished.
  bool mScrollPortOrScrolledAreaBoundsChanged : 1;

  // If true, the scroll frame is an ancestor of other "active" scrolling
  // frames, where "active" means has a non-minimal display port if
  // ShouldActivateAllScrollFrames is true, or has a display port if
  // ShouldActivateAllScrollFrames is false. And this means that we shouldn't
  // expire the display port (if ShouldActivateAllScrollFrames is true then
  // expiring a display port means making it minimal, otherwise it means
  // removing the display port). If those descendant scrollframes have their
  // display ports removed or made minimal, then we expire our display port.
  bool mIsParentToActiveScrollFrames : 1;

  // True if this frame has been scrolled at least once
  bool mHasBeenScrolled : 1;

  // True if the events synthesized by OSX to produce momentum scrolling should
  // be ignored.  Reset when the next real, non-synthesized scroll event occurs.
  bool mIgnoreMomentumScroll : 1;

  // True if the APZ is in the process of async-transforming this scrollframe,
  // (as best as we can tell on the main thread, anyway).
  bool mTransformingByAPZ : 1;

  // True if APZ can scroll this frame asynchronously (i.e. it has an APZC
  // set up for this frame and it's not a scrollinfo layer).
  bool mScrollableByAPZ : 1;

  // True if the APZ is allowed to zoom this scrollframe.
  bool mZoomableByAPZ : 1;

  // True if the scroll frame contains out-of-flow content and is inside
  // a CSS filter.
  bool mHasOutOfFlowContentInsideFilter : 1;

  // True if we don't want the scrollbar to repaint itself right now.
  bool mSuppressScrollbarRepaints : 1;

  // True if we are using the minimum scale size instead of ICB for scroll port.
  bool mIsUsingMinimumScaleSize : 1;

  // True if the minimum scale size has been changed since the last reflow.
  bool mMinimumScaleSizeChanged : 1;

  // True if we're processing an scroll event.
  bool mProcessingScrollEvent : 1;

  // This is true from the time a scroll animation is requested of APZ to the
  // time that APZ responds with an up-to-date repaint request. More precisely,
  // this is flipped to true if a repaint request is dispatched to APZ where
  // the most recent scroll request is a smooth scroll, and it is cleared when
  // mApzAnimationInProgress is updated.
  bool mApzAnimationRequested : 1;

  // Similar to above mApzAnimationRequested but the request came from script,
  // e.g., scrollBy().
  bool mApzAnimationTriggeredByScriptRequested : 1;

  // Whether we need to reclamp the visual viewport offset in ReflowFinished.
  bool mReclampVVOffsetInReflowFinished : 1;

  // Whether we need to schedule the scroll-driven animations.
  bool mMayScheduleScrollAnimations : 1;

  // Whether we need to ensure a scrollend is fired at the end of a scrollbar
  // click and hold gesture.
  bool mScrollbarClickAndHoldScrollendPending : 1;

  // Whether the overlay scrollbars are disabled on this container.
  bool mForceDisableOverlayScrollbars : 1;

#ifdef MOZ_WIDGET_ANDROID
  // True if this scrollable frame was vertically overflowed on the last reflow.
  bool mHasVerticalOverflowForDynamicToolbar : 1;
#endif

  layout::ScrollVelocityQueue mVelocityQueue;

  // NOTE: On mobile this value might be factoring into overflow:hidden region
  // in the case of the top level document.
  nsRect mScrollPort;
  UniquePtr<ScrollSnapTargetIds> mLastSnapTargetIds;
  // Lazily created on demand, see StickyScrollContainer::GetOrCreateForFrame.
  UniquePtr<StickyScrollContainer> mStickyContainer;
};

}  // namespace mozilla

#endif /* mozilla_ScrollContainerFrame_h_ */
