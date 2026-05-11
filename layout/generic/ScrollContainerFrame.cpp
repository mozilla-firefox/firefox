/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* rendering object to wrap rendering objects that should be scrollable */

#include "mozilla/ScrollContainerFrame.h"

#include <stdint.h>

#include <algorithm>
#include <cmath>    // for std::abs(float/double)
#include <cstdlib>  // for std::abs(int/long)
#include <tuple>    // for std::tie

#include "DisplayItemClip.h"
#include "GeckoProfiler.h"
#include "MobileViewportManager.h"
#include "ScrollAnimationBezierPhysics.h"
#include "ScrollAnimationMSDPhysics.h"
#include "ScrollAnimationPhysics.h"
#include "ScrollPositionUpdate.h"
#include "ScrollSnap.h"
#include "ScrollbarActivity.h"
#include "StickyScrollContainer.h"
#include "TextOverflow.h"
#include "UnitTransforms.h"
#include "ViewportFrame.h"
#include "VisualViewport.h"
#include "WindowRenderer.h"
#include "gfxPlatform.h"
#include "mozilla/Attributes.h"
#include "mozilla/ContentEvents.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresState.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/SVGOuterSVGFrame.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ScrollbarPreferences.h"
#include "mozilla/ScrollingMetrics.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_bidi.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_general.h"
#include "mozilla/StaticPrefs_layers.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_mousewheel.h"
#include "mozilla/StaticPrefs_toolkit.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ToString.h"
#include "mozilla/ViewportUtils.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLMarqueeElement.h"
#include "mozilla/dom/HTMLOptionElement.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/ScrollTimeline.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/intl/BidiEmbeddingLevel.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/APZPublicUtils.h"
#include "mozilla/layers/AxisPhysicsMSDModel.h"
#include "mozilla/layers/AxisPhysicsModel.h"
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/ScrollLinkedEffectDetector.h"
#include "mozilla/layers/ScrollingInteractionContext.h"
#include "nsBidiPresUtils.h"
#include "nsBidiUtils.h"
#include "nsBlockFrame.h"
#include "nsCOMPtr.h"
#include "nsCSSRendering.h"
#include "nsContainerFrame.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentUtils.h"
#include "nsDisplayList.h"
#include "nsDocShell.h"
#include "nsFlexContainerFrame.h"
#include "nsFontMetrics.h"
#include "nsGkAtoms.h"
#include "nsHTMLDocument.h"
#include "nsIDocumentViewer.h"
#include "nsIFrameInlines.h"
#include "nsILayoutHistoryState.h"
#include "nsINode.h"
#include "nsIScrollbarMediator.h"
#include "nsIXULRuntime.h"
#include "nsLayoutUtils.h"
#include "nsListControlFrame.h"
#include "nsNameSpaceManager.h"
#include "nsNodeInfoManager.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsPresContextInlines.h"
#include "nsRefreshDriver.h"
#include "nsScrollbarFrame.h"
#include "nsSliderFrame.h"
#include "nsStyleConsts.h"
#include "nsStyleTransformMatrix.h"
#include "nsSubDocumentFrame.h"
#include "nsTextControlFrame.h"
#include "nsViewportInfo.h"

static mozilla::LazyLogModule sApzPaintSkipLog("apz.paintskip");
#define PAINT_SKIP_LOG(...) \
  MOZ_LOG(sApzPaintSkipLog, LogLevel::Debug, (__VA_ARGS__))
static mozilla::LazyLogModule sScrollRestoreLog("scrollrestore");
#define SCROLLRESTORE_LOG(...) \
  MOZ_LOG(sScrollRestoreLog, LogLevel::Debug, (__VA_ARGS__))
static mozilla::LazyLogModule sRootScrollbarsLog("rootscrollbars");
#define ROOT_SCROLLBAR_LOG(...)                                  \
  if (mIsRoot) {                                                 \
    MOZ_LOG(sRootScrollbarsLog, LogLevel::Debug, (__VA_ARGS__)); \
  }
static mozilla::LazyLogModule sDisplayportLog("apz.displayport");

static mozilla::LazyLogModule sScrollEndLog("apz.scrollend");
#define SCROLLEND_LOG(...) \
  MOZ_LOG(sScrollEndLog, LogLevel::Debug, (__VA_ARGS__));

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::layout;
using nsStyleTransformMatrix::TransformReferenceBox;

static ScrollDirections GetOverflowChange(const nsRect& aCurScrolledRect,
                                          const nsRect& aPrevScrolledRect) {
  ScrollDirections result;
  if (aPrevScrolledRect.x != aCurScrolledRect.x ||
      aPrevScrolledRect.width != aCurScrolledRect.width) {
    result += ScrollDirection::eHorizontal;
  }
  if (aPrevScrolledRect.y != aCurScrolledRect.y ||
      aPrevScrolledRect.height != aCurScrolledRect.height) {
    result += ScrollDirection::eVertical;
  }
  return result;
}

/**
 * This class handles the dispatching of scroll events to content.
 *
 * Scroll events are posted to the refresh driver via
 * nsRefreshDriver::PostScrollEvent(), and they are fired during a refresh
 * driver tick, after running requestAnimationFrame callbacks but before
 * the style flush. This allows rAF callbacks to perform scrolling and have
 * that scrolling be reflected on the same refresh driver tick, while at
 * the same time allowing scroll event listeners to make style changes and
 * have those style changes be reflected on the same refresh driver tick.
 *
 * ScrollEvents cannot be refresh observers, because none of the existing
 * categories of refresh observers (FlushType::Style, FlushType::Layout,
 * and FlushType::Display) are run at the desired time in a refresh driver
 * tick. They behave similarly to refresh observers in that their presence
 * causes the refresh driver to tick.
 *
 * ScrollEvents are one-shot runnables; the refresh driver drops them after
 * running them.
 */
class ScrollContainerFrame::ScrollEvent : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE
  explicit ScrollEvent(ScrollContainerFrame* aHelper);
  void Revoke() { mHelper = nullptr; }
  UniquePtr<ProfileChunkedBuffer> mBacktrace;

 private:
  ScrollContainerFrame* mHelper;
};

class ScrollContainerFrame::ScrollEndEvent : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE
  explicit ScrollEndEvent(ScrollContainerFrame* aHelper);
  void Revoke() { mHelper = nullptr; }

 private:
  ScrollContainerFrame* mHelper;
};

class ScrollContainerFrame::AsyncScrollPortEvent : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE
  explicit AsyncScrollPortEvent(ScrollContainerFrame* helper)
      : Runnable("ScrollContainerFrame::AsyncScrollPortEvent"),
        mHelper(helper) {}
  void Revoke() { mHelper = nullptr; }

 private:
  ScrollContainerFrame* mHelper;
};

class ScrollContainerFrame::ScrolledAreaEvent : public Runnable {
 public:
  NS_DECL_NSIRUNNABLE
  explicit ScrolledAreaEvent(ScrollContainerFrame* helper)
      : Runnable("ScrollContainerFrame::ScrolledAreaEvent"), mHelper(helper) {}
  void Revoke() { mHelper = nullptr; }

 private:
  ScrollContainerFrame* mHelper;
};

class ScrollFrameActivityTracker final
    : public nsExpirationTracker<ScrollContainerFrame, 4> {
 public:
  // Wait for 3-4s between scrolls before we remove our layers.
  // That's 4 generations of 1s each.
  enum { TIMEOUT_MS = 1000 };
  explicit ScrollFrameActivityTracker(nsIEventTarget* aEventTarget)
      : nsExpirationTracker<ScrollContainerFrame, 4>(
            TIMEOUT_MS, "ScrollFrameActivityTracker"_ns, aEventTarget) {}
  ~ScrollFrameActivityTracker() { AgeAllGenerations(); }

  virtual void NotifyExpired(ScrollContainerFrame* aObject) override {
    RemoveObject(aObject);
    aObject->MarkNotRecentlyScrolled();
  }
};
static StaticAutoPtr<ScrollFrameActivityTracker> gScrollFrameActivityTracker;

ScrollContainerFrame* NS_NewScrollContainerFrame(mozilla::PresShell* aPresShell,
                                                 ComputedStyle* aStyle,
                                                 bool aIsRoot) {
  return new (aPresShell)
      ScrollContainerFrame(aStyle, aPresShell->GetPresContext(), aIsRoot);
}

NS_IMPL_FRAMEARENA_HELPERS(ScrollContainerFrame)

ScrollContainerFrame::ScrollContainerFrame(ComputedStyle* aStyle,
                                           nsPresContext* aPresContext,
                                           nsIFrame::ClassID aID, bool aIsRoot)
    : nsContainerFrame(aStyle, aPresContext, aID),
      mHScrollbarBox(nullptr),
      mVScrollbarBox(nullptr),
      mScrolledFrame(nullptr),
      mScrollCornerBox(nullptr),
      mResizerBox(nullptr),
      mReferenceFrameDuringPainting(nullptr),
      mAsyncScroll(nullptr),
      mAsyncSmoothMSDScroll(nullptr),
      mLastScrollOrigin(ScrollOrigin::None),
      mDestination(0, 0),
      mRestorePos(-1, -1),
      mLastPos(-1, -1),
      mApzScrollPos(0, 0),
      mLastUpdateFramesPos(-1, -1),
      mScrollParentID(mozilla::layers::ScrollableLayerGuid::NULL_SCROLL_ID),
      mAnchor(this),
      mCurrentAPZScrollAnimationType(APZScrollAnimationType::No),
      mIsFirstScrollableFrameSequenceNumber(Nothing()),
      mInScrollingGesture(InScrollingGesture::No),
      mAllowScrollOriginDowngrade(false),
      mHadDisplayPortAtLastFrameUpdate(false),
      mHasVerticalScrollbar(false),
      mHasHorizontalScrollbar(false),
      mOnlyNeedVScrollbarToScrollVVInsideLV(false),
      mOnlyNeedHScrollbarToScrollVVInsideLV(false),
      mFrameIsUpdatingScrollbar(false),
      mDidHistoryRestore(false),
      mIsRoot(aIsRoot),
      mSkippedScrollbarLayout(false),
      mHadNonInitialReflow(false),
      mFirstReflow(true),
      mHorizontalOverflow(false),
      mVerticalOverflow(false),
      mPostedReflowCallback(false),
      mMayHaveDirtyFixedChildren(false),
      mUpdateScrollbarAttributes(false),
      mHasBeenScrolledRecently(false),
      mWillBuildScrollableLayer(false),
      mIsParentToActiveScrollFrames(false),
      mHasBeenScrolled(false),
      mIgnoreMomentumScroll(false),
      mTransformingByAPZ(false),
      mScrollableByAPZ(false),
      mZoomableByAPZ(false),
      mHasOutOfFlowContentInsideFilter(false),
      mSuppressScrollbarRepaints(false),
      mIsUsingMinimumScaleSize(false),
      mMinimumScaleSizeChanged(false),
      mProcessingScrollEvent(false),
      mApzAnimationRequested(false),
      mApzAnimationTriggeredByScriptRequested(false),
      mReclampVVOffsetInReflowFinished(false),
      mMayScheduleScrollAnimations(false),
      mForceDisableOverlayScrollbars(false),
#ifdef MOZ_WIDGET_ANDROID
      mHasVerticalOverflowForDynamicToolbar(false),
#endif
      mVelocityQueue(PresContext()) {
  AppendScrollUpdate(ScrollPositionUpdate::NewScrollframe(nsPoint()));

  if (PresContext()->UseOverlayScrollbars()) {
    mScrollbarActivity = MakeRefPtr<ScrollbarActivity>(this);
  }

  if (mIsRoot) {
    mZoomableByAPZ = PresShell()->GetZoomableByAPZ();
  }
}

ScrollContainerFrame::~ScrollContainerFrame() = default;

static nsSliderFrame* GetSliderFrame(nsIFrame* aScrollbarFrame) {
  if (!aScrollbarFrame) {
    return nullptr;
  }

  for (const auto& childList : aScrollbarFrame->ChildLists()) {
    for (nsIFrame* frame : childList.mList) {
      if (nsSliderFrame* sliderFrame = do_QueryFrame(frame)) {
        return sliderFrame;
      }
    }
  }
  return nullptr;
}

void ScrollContainerFrame::ScrollbarActivityStarted() const {
  if (mScrollbarActivity) {
    mScrollbarActivity->ActivityStarted();
  }
}

void ScrollContainerFrame::ScrollbarActivityStopped() const {
  if (mScrollbarActivity) {
    mScrollbarActivity->ActivityStopped();
  }
}

void ScrollContainerFrame::Destroy(DestroyContext& aContext) {
  DestroyAbsoluteFrames(aContext);
  if (mIsRoot) {
    PresShell()->ResetVisualViewportOffset();
  }

  mAnchor.Destroy();

  if (mScrollbarActivity) {
    mScrollbarActivity->Destroy();
    mScrollbarActivity = nullptr;
  }

  // Unbind the content created in CreateAnonymousContent later...
  aContext.AddAnonymousContent(mHScrollbarContent.forget());
  aContext.AddAnonymousContent(mVScrollbarContent.forget());
  aContext.AddAnonymousContent(mScrollCornerContent.forget());
  aContext.AddAnonymousContent(mResizerContent.forget());

  if (mPostedReflowCallback) {
    PresShell()->CancelReflowCallback(this);
    mPostedReflowCallback = false;
  }

  if (mDisplayPortExpiryTimer) {
    mDisplayPortExpiryTimer->Cancel();
    mDisplayPortExpiryTimer = nullptr;
  }
  if (mActivityExpirationState.IsTracked()) {
    gScrollFrameActivityTracker->RemoveObject(this);
  }
  if (gScrollFrameActivityTracker && gScrollFrameActivityTracker->IsEmpty()) {
    gScrollFrameActivityTracker = nullptr;
  }

  if (mScrollActivityTimer) {
    mScrollActivityTimer->Cancel();
    mScrollActivityTimer = nullptr;
  }
  RemoveObservers();
  if (mScrollEvent) {
    mScrollEvent->Revoke();
  }
  if (mScrollEndEvent) {
    mScrollEndEvent->Revoke();
  }
  nsContainerFrame::Destroy(aContext);
}

void ScrollContainerFrame::SetInitialChildList(ChildListID aListID,
                                               nsFrameList&& aChildList) {
  nsContainerFrame::SetInitialChildList(aListID, std::move(aChildList));
  ReloadChildFrames();
}

void ScrollContainerFrame::AppendFrames(ChildListID aListID,
                                        nsFrameList&& aFrameList) {
  NS_ASSERTION(aListID == FrameChildListID::Principal,
               "Only main list supported");
  mFrames.AppendFrames(nullptr, std::move(aFrameList));
  ReloadChildFrames();
}

void ScrollContainerFrame::InsertFrames(
    ChildListID aListID, nsIFrame* aPrevFrame,
    const nsLineList::iterator* aPrevFrameLine, nsFrameList&& aFrameList) {
  NS_ASSERTION(aListID == FrameChildListID::Principal,
               "Only main list supported");
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == this,
               "inserting after sibling frame with different parent");
  mFrames.InsertFrames(nullptr, aPrevFrame, std::move(aFrameList));
  ReloadChildFrames();
}

void ScrollContainerFrame::RemoveFrame(DestroyContext& aContext,
                                       ChildListID aListID,
                                       nsIFrame* aOldFrame) {
  NS_ASSERTION(aListID == FrameChildListID::Principal,
               "Only main list supported");
  mFrames.DestroyFrame(aContext, aOldFrame);
  ReloadChildFrames();
}

/**
 HTML scrolling implementation

 All other things being equal, we prefer layouts with fewer scrollbars showing.
*/

namespace mozilla {

enum class ShowScrollbar : uint8_t {
  Auto,
  Always,
  // Never is a misnomer. We can still get a scrollbar if we need to scroll the
  // visual viewport inside the layout viewport. Thus this enum is best thought
  // of as value used by layout, which does not know about the visual viewport.
  // The visual viewport does not affect any layout sizes, so this is sound.
  Never,
};

static ShowScrollbar ShouldShowScrollbar(StyleOverflow aOverflow) {
  switch (aOverflow) {
    case StyleOverflow::Scroll:
      return ShowScrollbar::Always;
    case StyleOverflow::Hidden:
      return ShowScrollbar::Never;
    default:
    case StyleOverflow::Auto:
      return ShowScrollbar::Auto;
  }
}

static bool IsSingleLineTextInput(const nsIFrame* aFrame) {
  const nsTextControlFrame* tcf = do_QueryFrame(aFrame);
  return tcf && !tcf->IsTextArea();
}

struct MOZ_STACK_CLASS ScrollReflowInput {
  // === Filled in by the constructor. Members in this section shouldn't change
  // their values after the constructor. ===
  const ReflowInput& mReflowInput;
  ShowScrollbar mHScrollbar;
  // If the horizontal scrollbar is allowed (even if mHScrollbar ==
  // ShowScrollbar::Never) provided that it is for scrolling the visual viewport
  // inside the layout viewport only.
  bool mHScrollbarAllowedForScrollingVVInsideLV = true;
  ShowScrollbar mVScrollbar;
  // If the vertical scrollbar is allowed (even if mVScrollbar ==
  // ShowScrollbar::Never) provided that it is for scrolling the visual viewport
  // inside the layout viewport only.
  bool mVScrollbarAllowedForScrollingVVInsideLV = true;
  // The area that shouldn't participate in our scrollport (usually just our
  // border)
  nsMargin mScrollportMargin;
  // The inline size of the button box (e.g., number spin-box) that sits
  // outside the scrolled area. This space is reserved on the inline-end side.
  nscoord mButtonISize = 0;

  // === Filled in by ReflowScrolledFrame ===
  OverflowAreas mContentsOverflowAreas;
  // The scrollbar gutter sizes used in the most recent reflow of
  // mScrolledFrame. The writing-mode is the same as the scroll
  // container.
  LogicalMargin mScrollbarGutterFromLastReflow;
  // True if the most recent reflow of mScrolledFrame is with the
  // horizontal scrollbar.
  bool mReflowedContentsWithHScrollbar = false;
  // True if the most recent reflow of mScrolledFrame is with the
  // vertical scrollbar.
  bool mReflowedContentsWithVScrollbar = false;

  // === Filled in when TryLayout succeeds ===
  // The size of the inside-border area
  nsSize mInsideBorderSize;
  // Whether we decided to show the horizontal scrollbar in the most recent
  // TryLayout.
  bool mShowHScrollbar = false;
  // Whether we decided to show the vertical scrollbar in the most recent
  // TryLayout.
  bool mShowVScrollbar = false;
  // If mShow(H|V)Scrollbar is true then
  // mOnlyNeed(V|H)ScrollbarToScrollVVInsideLV indicates if the only reason we
  // need that scrollbar is to scroll the visual viewport inside the layout
  // viewport. These scrollbars are special in that even if they are layout
  // scrollbars they do not take up any layout space.
  bool mOnlyNeedHScrollbarToScrollVVInsideLV = false;
  bool mOnlyNeedVScrollbarToScrollVVInsideLV = false;

  ScrollReflowInput(ScrollContainerFrame* aFrame,
                    const ReflowInput& aReflowInput);

  nscoord VScrollbarMinHeight() const { return mVScrollbarPrefSize.height; }
  nscoord VScrollbarPrefWidth() const { return mVScrollbarPrefSize.width; }
  nscoord HScrollbarMinWidth() const { return mHScrollbarPrefSize.width; }
  nscoord HScrollbarPrefHeight() const { return mHScrollbarPrefSize.height; }

  // Returns the sizes occupied by the scrollbar gutters. If aShowVScroll or
  // aShowHScroll is true, the sizes occupied by the scrollbars are also
  // included.
  nsMargin ScrollbarGutter(bool aShowVScrollbar, bool aShowHScrollbar,
                           bool aScrollbarOnRight) const {
    if (mOverlayScrollbars) {
      return mScrollbarGutter;
    }
    nsMargin gutter = mScrollbarGutter;
    if (aShowVScrollbar && gutter.right == 0 && gutter.left == 0) {
      const nscoord w = VScrollbarPrefWidth();
      if (aScrollbarOnRight) {
        gutter.right = w;
      } else {
        gutter.left = w;
      }
    }
    if (aShowHScrollbar && gutter.bottom == 0) {
      // The horizontal scrollbar is always at the bottom side.
      gutter.bottom = HScrollbarPrefHeight();
    }
    return gutter;
  }

  LogicalMargin KidPadding() const {
    const auto wm = mReflowInput.GetWritingMode();
    LogicalMargin kidPadding = mReflowInput.ComputedLogicalPadding(wm);
    if (IsSingleLineTextInput(mReflowInput.mFrame)) {
      // For single-line text inputs we don't want to include padding in the
      // scrollport in the inline direction.
      kidPadding.IStart(wm) = kidPadding.IEnd(wm) = 0;
    }
    return kidPadding;
  }

  bool OverlayScrollbars() const { return mOverlayScrollbars; }

 private:
  // Filled in by the constructor. Put variables here to keep them unchanged
  // after initializing them in the constructor.
  nsSize mVScrollbarPrefSize;
  nsSize mHScrollbarPrefSize;
  bool mOverlayScrollbars = false;
  // The scrollbar gutter sizes resolved from the scrollbar-gutter and
  // scrollbar-width property.
  nsMargin mScrollbarGutter;
};

static nsMargin GetScrollPortMargin(ScrollContainerFrame* aFrame,
                                    const ReflowInput& aRI) {
  if (!IsSingleLineTextInput(aFrame)) {
    return aRI.ComputedPhysicalBorder();
  }
  const auto wm = aRI.GetWritingMode();
  auto margin = aRI.ComputedLogicalBorder(wm);
  const auto& padding = aRI.ComputedLogicalPadding(wm);
  margin.IStart(wm) += padding.IStart(wm);
  margin.IEnd(wm) += padding.IEnd(wm);
  return margin.GetPhysicalMargin(wm);
}

ScrollReflowInput::ScrollReflowInput(ScrollContainerFrame* aFrame,
                                     const ReflowInput& aReflowInput)
    : mReflowInput(aReflowInput),
      mScrollportMargin(GetScrollPortMargin(aFrame, aReflowInput)),
      mScrollbarGutterFromLastReflow(aFrame->GetWritingMode()) {
  ScrollStyles styles = aFrame->GetScrollStyles();
  mHScrollbar = ShouldShowScrollbar(styles.mHorizontal);
  mVScrollbar = ShouldShowScrollbar(styles.mVertical);
  mOverlayScrollbars = aFrame->UseOverlayScrollbars();

  if (nsScrollbarFrame* scrollbar = aFrame->GetScrollbarBox(false)) {
    mHScrollbarPrefSize = scrollbar->ScrollbarMinSize();
    // A zero minimum size is a bug with non-overlay scrollbars. That means
    // we'll always try to place the scrollbar, even if it will ultimately not
    // fit, see bug 1809630. XUL collapsing is the exception because the
    // front-end uses it.
    MOZ_ASSERT(mHScrollbarPrefSize.width && mHScrollbarPrefSize.height,
               "Shouldn't have a zero horizontal scrollbar-size");
  } else {
    mHScrollbar = ShowScrollbar::Never;
    mHScrollbarAllowedForScrollingVVInsideLV = false;
  }
  if (nsScrollbarFrame* scrollbar = aFrame->GetScrollbarBox(true)) {
    mVScrollbarPrefSize = scrollbar->ScrollbarMinSize();
    // See above.
    MOZ_ASSERT(mVScrollbarPrefSize.width && mVScrollbarPrefSize.height,
               "Shouldn't have a zero vertical scrollbar-size");
  } else {
    mVScrollbar = ShowScrollbar::Never;
    mVScrollbarAllowedForScrollingVVInsideLV = false;
  }

  const auto* scrollbarStyle =
      nsLayoutUtils::StyleForScrollbar(mReflowInput.mFrame);
  // Hide the scrollbar when the scrollbar-width is set to none.
  //
  // Note: In some cases this is unnecessary, because scrollbar-width:none
  // makes us suppress scrollbars in CreateAnonymousContent. But if this frame
  // initially had a non-'none' scrollbar-width and dynamically changed to
  // 'none', then we'll need to handle it here.
  const auto scrollbarWidth =
      nsLayoutUtils::ScrollbarWidthFor(mReflowInput.mFrame);

  if (scrollbarWidth == StyleScrollbarWidth::None) {
    mHScrollbar = ShowScrollbar::Never;
    mHScrollbarAllowedForScrollingVVInsideLV = false;
    mVScrollbar = ShowScrollbar::Never;
    mVScrollbarAllowedForScrollingVVInsideLV = false;
  }

  mScrollbarGutter = aFrame->ComputeStableScrollbarGutter(
      scrollbarWidth, scrollbarStyle->StyleDisplay()->mScrollbarGutter);

  if (nsIFrame* buttonBox = aFrame->GetButtonBoxFrame()) {
    mButtonISize = nsLayoutUtils::IntrinsicForContainer(
        aReflowInput.mRenderingContext, buttonBox,
        IntrinsicISizeType::PrefISize);
  }
}

}  // namespace mozilla

static nsSize ComputeInsideBorderSize(const ScrollReflowInput& aState,
                                      const nsSize& aDesiredInsideBorderSize) {
  // aDesiredInsideBorderSize is the frame size; i.e., it includes
  // borders and padding (but the scrolled child doesn't have
  // borders). The scrolled child has the same padding as us.
  const WritingMode wm = aState.mReflowInput.GetWritingMode();
  const LogicalSize desiredInsideBorderSize(wm, aDesiredInsideBorderSize);
  LogicalSize contentSize = aState.mReflowInput.ComputedSize();
  const LogicalMargin padding = aState.KidPadding();

  if (contentSize.ISize(wm) == NS_UNCONSTRAINEDSIZE) {
    contentSize.ISize(wm) =
        desiredInsideBorderSize.ISize(wm) - padding.IStartEnd(wm);
  }
  if (contentSize.BSize(wm) == NS_UNCONSTRAINEDSIZE) {
    contentSize.BSize(wm) =
        desiredInsideBorderSize.BSize(wm) - padding.BStartEnd(wm);
  }

  contentSize.ISize(wm) =
      aState.mReflowInput.ApplyMinMaxISize(contentSize.ISize(wm));
  contentSize.BSize(wm) =
      aState.mReflowInput.ApplyMinMaxBSize(contentSize.BSize(wm));

  return (contentSize + padding.Size(wm)).GetPhysicalSize(wm);
}

/**
 * Assuming that we know the metrics for our wrapped frame and
 * whether the horizontal and/or vertical scrollbars are present,
 * compute the resulting layout and return true if the layout is
 * consistent. If the layout is consistent then we fill in the
 * computed fields of the ScrollReflowInput.
 *
 * The layout is consistent when both scrollbars are showing if and only
 * if they should be showing. A horizontal scrollbar should be showing if all
 * following conditions are met:
 * 1) the style is not HIDDEN
 * 2) our inside-border height is at least the scrollbar height (i.e., the
 * scrollbar fits vertically)
 * 3) the style is SCROLL, or the kid's overflow-area XMost is
 * greater than the scrollport width
 *
 * @param aForce if true, then we just assume the layout is consistent.
 */
bool ScrollContainerFrame::TryLayout(ScrollReflowInput& aState,
                                     ReflowOutput* aKidMetrics,
                                     bool aAssumeHScroll, bool aAssumeVScroll,
                                     bool aForce) {
  if ((aState.mVScrollbar == ShowScrollbar::Never && aAssumeVScroll) ||
      (aState.mHScrollbar == ShowScrollbar::Never && aAssumeHScroll)) {
    NS_ASSERTION(!aForce, "Shouldn't be forcing a hidden scrollbar to show!");
    return false;
  }

  const auto wm = GetWritingMode();
  const nsMargin scrollbarGutter = aState.ScrollbarGutter(
      aAssumeVScroll, aAssumeHScroll, IsScrollbarOnRight());
  const LogicalMargin logicalScrollbarGutter(wm, scrollbarGutter);

  const bool inlineEndsGutterChanged =
      aState.mScrollbarGutterFromLastReflow.IStartEnd(wm) !=
      logicalScrollbarGutter.IStartEnd(wm);
  const bool blockEndsGutterChanged =
      aState.mScrollbarGutterFromLastReflow.BStartEnd(wm) !=
      logicalScrollbarGutter.BStartEnd(wm);
  const bool shouldReflowScrolledFrame =
      inlineEndsGutterChanged ||
      (blockEndsGutterChanged && ScrolledContentDependsOnBSize(aState));

  if (shouldReflowScrolledFrame) {
    if (blockEndsGutterChanged) {
      nsLayoutUtils::MarkIntrinsicISizesDirtyIfDependentOnBSize(mScrolledFrame);
    }
    aKidMetrics->mOverflowAreas.Clear();
    ROOT_SCROLLBAR_LOG(
        "TryLayout reflowing scrolled frame with scrollbars h=%d, v=%d\n",
        aAssumeHScroll, aAssumeVScroll);
    ReflowScrolledFrame(aState, aAssumeHScroll, aAssumeVScroll, aKidMetrics);
  }

  // The button box (e.g., number spin-box) reserves space on the inline-end.
  // Compute the physical margin for the button box.
  nsMargin buttonBoxMargin;
  if (aState.mButtonISize > 0) {
    LogicalMargin logical(wm);
    logical.IEnd(wm) = aState.mButtonISize;
    buttonBoxMargin = logical.GetPhysicalMargin(wm);
  }
  const nsSize scrollbarGutterSize(
      scrollbarGutter.LeftRight() + buttonBoxMargin.LeftRight(),
      scrollbarGutter.TopBottom() + buttonBoxMargin.TopBottom());

  // First, compute our inside-border size and scrollport size
  nsSize kidSize = GetContainSizeAxes().ContainSize(
      aKidMetrics->PhysicalSize(), *aState.mReflowInput.mFrame);
  const nsSize desiredInsideBorderSize = kidSize + scrollbarGutterSize;
  aState.mInsideBorderSize =
      ComputeInsideBorderSize(aState, desiredInsideBorderSize);

  nsSize layoutSize =
      mIsUsingMinimumScaleSize ? mMinimumScaleSize : aState.mInsideBorderSize;

  const nsSize scrollPortSize = Max(nsSize(), layoutSize - scrollbarGutterSize);
  if (mIsUsingMinimumScaleSize) {
    mICBSize = Max(nsSize(), aState.mInsideBorderSize - scrollbarGutterSize);
  }

  nsSize visualViewportSize = scrollPortSize;
  ROOT_SCROLLBAR_LOG("TryLayout with VV %s\n",
                     ToString(visualViewportSize).c_str());
  mozilla::PresShell* presShell = PresShell();
  // Note: we check for a non-null MobileViepwortManager here, but ideally we
  // should be able to drop that clause as well. It's just that in some cases
  // with extension popups the composition size comes back as stale, because
  // the content viewer is only resized after the popup contents are reflowed.
  // That case also happens to have no APZ and no MVM, so we use that as a
  // way to detect the scenario. Bug 1648669 tracks removing this clause.
  if (mIsRoot && presShell->GetMobileViewportManager()) {
    visualViewportSize = nsLayoutUtils::CalculateCompositionSizeForFrame(
        this, false, &layoutSize);
    visualViewportSize =
        Max(nsSize(0, 0), visualViewportSize - scrollbarGutterSize);

    float resolution = presShell->GetResolution();
    visualViewportSize.width /= resolution;
    visualViewportSize.height /= resolution;
    ROOT_SCROLLBAR_LOG("TryLayout now with VV %s\n",
                       ToString(visualViewportSize).c_str());
  }

  nsRect overflowRect = aState.mContentsOverflowAreas.ScrollableOverflow();
  // If the content height expanded by the minimum-scale will be taller than
  // the scrollable overflow area, we need to expand the area here to tell
  // properly whether we need to render the overlay vertical scrollbar.
  // NOTE: This expanded size should NOT be used for non-overley scrollbars
  // cases since putting the vertical non-overlay scrollbar will make the
  // content width narrow a little bit, which in turn the minimum scale value
  // becomes a bit bigger than before, then the vertical scrollbar is no longer
  // needed, which means the content width becomes the original width, then the
  // minimum-scale is changed to the original one, and so forth.
  if (UseOverlayScrollbars() && mIsUsingMinimumScaleSize &&
      mMinimumScaleSize.height > overflowRect.YMost()) {
    overflowRect.height += mMinimumScaleSize.height - overflowRect.YMost();
  }
  nsRect scrolledRect =
      GetUnsnappedScrolledRectInternal(overflowRect, scrollPortSize);
  ROOT_SCROLLBAR_LOG(
      "TryLayout scrolledRect:%s overflowRect:%s scrollportSize:%s\n",
      ToString(scrolledRect).c_str(), ToString(overflowRect).c_str(),
      ToString(scrollPortSize).c_str());
  nscoord oneDevPixel = PresContext()->DevPixelsToAppUnits(1);

  bool showHScrollbar = aAssumeHScroll;
  bool showVScrollbar = aAssumeVScroll;
  if (!aForce) {
    nsSize sizeToCompare = visualViewportSize;
    if (gfxPlatform::UseDesktopZoomingScrollbars()) {
      sizeToCompare = scrollPortSize;
    }

    // No need to compute showHScrollbar if we got ShowScrollbar::Never.
    if (aState.mHScrollbar != ShowScrollbar::Never) {
      showHScrollbar =
          aState.mHScrollbar == ShowScrollbar::Always ||
          scrolledRect.XMost() >= sizeToCompare.width + oneDevPixel ||
          scrolledRect.x <= -oneDevPixel;
      // TODO(emilio): This should probably check this scrollbar's minimum size
      // in both axes, for consistency?
      if (aState.mHScrollbar == ShowScrollbar::Auto &&
          scrollPortSize.width < aState.HScrollbarMinWidth()) {
        showHScrollbar = false;
      }
      ROOT_SCROLLBAR_LOG("TryLayout wants H Scrollbar: %d =? %d\n",
                         showHScrollbar, aAssumeHScroll);
    }

    // No need to compute showVScrollbar if we got ShowScrollbar::Never.
    if (aState.mVScrollbar != ShowScrollbar::Never) {
      showVScrollbar =
          aState.mVScrollbar == ShowScrollbar::Always ||
          scrolledRect.YMost() >= sizeToCompare.height + oneDevPixel ||
          scrolledRect.y <= -oneDevPixel;
      // TODO(emilio): This should probably check this scrollbar's minimum size
      // in both axes, for consistency?
      if (aState.mVScrollbar == ShowScrollbar::Auto &&
          scrollPortSize.height < aState.VScrollbarMinHeight()) {
        showVScrollbar = false;
      }
      ROOT_SCROLLBAR_LOG("TryLayout wants V Scrollbar: %d =? %d\n",
                         showVScrollbar, aAssumeVScroll);
    }

    if (showHScrollbar != aAssumeHScroll || showVScrollbar != aAssumeVScroll) {
      const nsMargin wantedScrollbarGutter = aState.ScrollbarGutter(
          showVScrollbar, showHScrollbar, IsScrollbarOnRight());
      // We report an inconsistent layout only when the desired visibility of
      // the scrollbars can change the size of the scrollbar gutters.
      if (scrollbarGutter != wantedScrollbarGutter) {
        return false;
      }
    }
  }

  // If we reach here, the layout is consistent. Record the desired visibility
  // of the scrollbars.
  aState.mShowHScrollbar = showHScrollbar;
  aState.mShowVScrollbar = showVScrollbar;
  const nsPoint scrollPortOrigin(
      aState.mScrollportMargin.left + scrollbarGutter.left +
          buttonBoxMargin.left,
      aState.mScrollportMargin.top + scrollbarGutter.top + buttonBoxMargin.top);
  SetScrollPort(nsRect(scrollPortOrigin, scrollPortSize));

  if (mIsRoot && gfxPlatform::UseDesktopZoomingScrollbars()) {
    bool vvChanged = true;
    const bool overlay = aState.OverlayScrollbars();
    // This loop can run at most twice since we can only add a scrollbar once.
    // At this point we've already decided that this layout is consistent so we
    // will return true. Scrollbars added here never take up layout space even
    // if they are layout scrollbars so any changes made here will not make us
    // return false.
    while (vvChanged) {
      vvChanged = false;
      if (!aState.mShowHScrollbar &&
          aState.mHScrollbarAllowedForScrollingVVInsideLV) {
        if (ScrollPort().width >= visualViewportSize.width + oneDevPixel &&
            (overlay ||
             visualViewportSize.width >= aState.HScrollbarMinWidth())) {
          vvChanged = true;
          if (!overlay) {
            visualViewportSize.height -= aState.HScrollbarPrefHeight();
          }
          aState.mShowHScrollbar = true;
          aState.mOnlyNeedHScrollbarToScrollVVInsideLV = true;
          ROOT_SCROLLBAR_LOG("TryLayout added H scrollbar for VV, VV now %s\n",
                             ToString(visualViewportSize).c_str());
        }
      }

      if (!aState.mShowVScrollbar &&
          aState.mVScrollbarAllowedForScrollingVVInsideLV) {
        if (ScrollPort().height >= visualViewportSize.height + oneDevPixel &&
            (overlay ||
             visualViewportSize.height >= aState.VScrollbarMinHeight())) {
          vvChanged = true;
          if (!overlay) {
            visualViewportSize.width -= aState.VScrollbarPrefWidth();
          }
          aState.mShowVScrollbar = true;
          aState.mOnlyNeedVScrollbarToScrollVVInsideLV = true;
          ROOT_SCROLLBAR_LOG("TryLayout added V scrollbar for VV, VV now %s\n",
                             ToString(visualViewportSize).c_str());
        }
      }
    }
  }

  return true;
}

bool ScrollContainerFrame::ScrolledContentDependsOnBSize(
    const ScrollReflowInput& aState) const {
  return mScrolledFrame->HasAnyStateBits(
             NS_FRAME_CONTAINS_RELATIVE_BSIZE |
             NS_FRAME_DESCENDANT_INTRINSIC_ISIZE_DEPENDS_ON_BSIZE) ||
         aState.mReflowInput.ComputedBSize() != NS_UNCONSTRAINEDSIZE ||
         aState.mReflowInput.ComputedMinBSize() > 0 ||
         aState.mReflowInput.ComputedMaxBSize() != NS_UNCONSTRAINEDSIZE;
}

void ScrollContainerFrame::ReflowScrolledFrame(ScrollReflowInput& aState,
                                               bool aAssumeHScroll,
                                               bool aAssumeVScroll,
                                               ReflowOutput* aMetrics) {
  const WritingMode wm = GetWritingMode();
  MOZ_ASSERT(wm == mScrolledFrame->GetWritingMode(), "How?");

  LogicalMargin kidPadding = aState.KidPadding();
  nscoord availISize =
      aState.mReflowInput.ComputedISize() + kidPadding.IStartEnd(wm);

  nscoord computedBSize = aState.mReflowInput.ComputedBSize();
  nscoord computedMinBSize = aState.mReflowInput.ComputedMinBSize();
  nscoord computedMaxBSize = aState.mReflowInput.ComputedMaxBSize();

  const LogicalMargin scrollbarGutter(
      wm, aState.ScrollbarGutter(aAssumeVScroll, aAssumeHScroll,
                                 IsScrollbarOnRight()));
  availISize = std::max(
      0, availISize - scrollbarGutter.IStartEnd(wm) - aState.mButtonISize);
  if (const nscoord blockEndsGutter = scrollbarGutter.BStartEnd(wm);
      blockEndsGutter > 0) {
    if (computedBSize != NS_UNCONSTRAINEDSIZE) {
      computedBSize = std::max(0, computedBSize - blockEndsGutter);
    }
    computedMinBSize = std::max(0, computedMinBSize - blockEndsGutter);
    if (computedMaxBSize != NS_UNCONSTRAINEDSIZE) {
      computedMaxBSize = std::max(0, computedMaxBSize - blockEndsGutter);
    }
  }

  nsPresContext* presContext = PresContext();

  // Pass InitFlags::CallerWillInit so we can pass in the correct padding.
  ReflowInput kidReflowInput(presContext, aState.mReflowInput, mScrolledFrame,
                             LogicalSize(wm, availISize, NS_UNCONSTRAINEDSIZE),
                             Nothing(), ReflowInput::InitFlag::CallerWillInit);
  const WritingMode kidWM = kidReflowInput.GetWritingMode();
  kidReflowInput.Init(presContext, Nothing(), Nothing(), Some(kidPadding));
  kidReflowInput.mFlags.mAssumingHScrollbar = aAssumeHScroll;
  kidReflowInput.mFlags.mAssumingVScrollbar = aAssumeVScroll;
  kidReflowInput.mFlags.mTreatBSizeAsIndefinite =
      aState.mReflowInput.mFlags.mTreatBSizeAsIndefinite;
  kidReflowInput.SetComputedBSize(computedBSize);
  kidReflowInput.SetComputedMinBSize(computedMinBSize);
  kidReflowInput.SetComputedMaxBSize(computedMaxBSize);
  if (aState.mReflowInput.IsBResizeForWM(kidWM)) {
    kidReflowInput.SetBResize(true);
  }
  if (aState.mReflowInput.IsBResizeForPercentagesForWM(kidWM)) {
    kidReflowInput.SetBResizeForPercentages(true);
  }

  // Temporarily set mHasHorizontalScrollbar/mHasVerticalScrollbar to
  // reflect our assumptions while we reflow the child.
  bool didHaveHorizontalScrollbar = mHasHorizontalScrollbar;
  bool didHaveVerticalScrollbar = mHasVerticalScrollbar;
  mHasHorizontalScrollbar = aAssumeHScroll;
  mHasVerticalScrollbar = aAssumeVScroll;

  nsReflowStatus status;
  // No need to pass a true container-size to ReflowChild or
  // FinishReflowChild, because it's only used there when positioning
  // the frame (i.e. if ReflowChildFlags::NoMoveFrame isn't set)
  const nsSize dummyContainerSize;
  ReflowChild(mScrolledFrame, presContext, *aMetrics, kidReflowInput, wm,
              LogicalPoint(wm), dummyContainerSize,
              ReflowChildFlags::NoMoveFrame, status);

  mHasHorizontalScrollbar = didHaveHorizontalScrollbar;
  mHasVerticalScrollbar = didHaveVerticalScrollbar;

  FinishReflowChild(mScrolledFrame, presContext, *aMetrics, &kidReflowInput, wm,
                    LogicalPoint(wm), dummyContainerSize,
                    ReflowChildFlags::NoMoveFrame);

  if (mScrolledFrame->HasAnyStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE)) {
    // Propagate NS_FRAME_CONTAINS_RELATIVE_BSIZE from our inner scrolled frame
    // to ourselves so that our containing block is aware of it.
    //
    // Note: If the scrolled frame has any child whose block-size depends on the
    // containing block's block-size, the NS_FRAME_CONTAINS_RELATIVE_BSIZE bit
    // is set on the scrolled frame when initializing the child's ReflowInput in
    // ReflowInput::InitResizeFlags(). Therefore, we propagate the bit here
    // after we reflowed the scrolled frame.
    AddStateBits(NS_FRAME_CONTAINS_RELATIVE_BSIZE);
  }

  // XXX Some frames (e.g. nsFrameFrame, nsTextFrame) don't
  // bother setting their mOverflowArea. This is wrong because every frame
  // should always set mOverflowArea. In fact nsFrameFrame doesn't
  // support the 'outline' property because of this. Rather than fix the
  // world right now, just fix up the overflow area if necessary. Note that we
  // don't check HasOverflowRect() because it could be set even though the
  // overflow area doesn't include the frame bounds.
  aMetrics->UnionOverflowAreasWithDesiredBounds();

  aState.mContentsOverflowAreas = aMetrics->mOverflowAreas;
  aState.mScrollbarGutterFromLastReflow = scrollbarGutter;
  aState.mReflowedContentsWithHScrollbar = aAssumeHScroll;
  aState.mReflowedContentsWithVScrollbar = aAssumeVScroll;
}

bool ScrollContainerFrame::GuessHScrollbarNeeded(
    const ScrollReflowInput& aState) {
  if (aState.mHScrollbar != ShowScrollbar::Auto) {
    // no guessing required
    return aState.mHScrollbar == ShowScrollbar::Always;
  }
  // We only care about scrollbars that might take up space when trying to guess
  // if we need a scrollbar, so we ignore scrollbars only created to scroll the
  // visual viewport inside the layout viewport because they take up no layout
  // space.
  return mHasHorizontalScrollbar && !mOnlyNeedHScrollbarToScrollVVInsideLV;
}

bool ScrollContainerFrame::GuessVScrollbarNeeded(
    const ScrollReflowInput& aState) {
  if (aState.mVScrollbar != ShowScrollbar::Auto) {
    // no guessing required
    return aState.mVScrollbar == ShowScrollbar::Always;
  }

  // If we've had at least one non-initial reflow, then just assume
  // the state of the vertical scrollbar will be what we determined
  // last time.
  if (mHadNonInitialReflow) {
    // We only care about scrollbars that might take up space when trying to
    // guess if we need a scrollbar, so we ignore scrollbars only created to
    // scroll the visual viewport inside the layout viewport because they take
    // up no layout space.
    return mHasVerticalScrollbar && !mOnlyNeedVScrollbarToScrollVVInsideLV;
  }

  // If this is the initial reflow, guess false because usually
  // we have very little content by then.
  if (InInitialReflow()) {
    return false;
  }

  if (mIsRoot) {
    nsIFrame* f = mScrolledFrame->PrincipalChildList().FirstChild();
    if (f && f->IsSVGOuterSVGFrame() &&
        static_cast<SVGOuterSVGFrame*>(f)->VerticalScrollbarNotNeeded()) {
      // Common SVG case - avoid a bad guess.
      return false;
    }
    // Assume that there will be a scrollbar; it seems to me
    // that 'most pages' do have a scrollbar, and anyway, it's cheaper
    // to do an extra reflow for the pages that *don't* need a
    // scrollbar (because on average they will have less content).
    return true;
  }

  // For non-viewports, just guess that we don't need a scrollbar.
  // XXX I wonder if statistically this is the right idea; I'm
  // basically guessing that there are a lot of overflow:auto DIVs
  // that get their intrinsic size and don't overflow
  return false;
}

bool ScrollContainerFrame::InInitialReflow() const {
  // We're in an initial reflow if NS_FRAME_FIRST_REFLOW is set, unless we're a
  // root scrollframe.  In that case we want to skip this clause altogether.
  // The guess here is that there are lots of overflow:auto divs out there that
  // end up auto-sizing so they don't overflow, and that the root basically
  // always needs a scrollbar if it did last time we loaded this page (good
  // assumption, because our initial reflow is no longer synchronous).
  return !mIsRoot && HasAnyStateBits(NS_FRAME_FIRST_REFLOW);
}

void ScrollContainerFrame::ReflowContents(ScrollReflowInput& aState,
                                          const ReflowOutput& aDesiredSize) {
  const WritingMode desiredWm = aDesiredSize.GetWritingMode();
  ReflowOutput kidDesiredSize(desiredWm);
  ReflowScrolledFrame(aState, GuessHScrollbarNeeded(aState),
                      GuessVScrollbarNeeded(aState), &kidDesiredSize);

  // There's an important special case ... if the child appears to fit
  // in the inside-border rect (but overflows the scrollport), we
  // should try laying it out without a vertical scrollbar. It will
  // usually fit because making the available-width wider will not
  // normally make the child taller. (The only situation I can think
  // of is when you have a line containing %-width inline replaced
  // elements whose percentages sum to more than 100%, so increasing
  // the available width makes the line break where it was fitting
  // before.) If we don't treat this case specially, then we will
  // decide that showing scrollbars is OK because the content
  // overflows when we're showing scrollbars and we won't try to
  // remove the vertical scrollbar.

  // Detecting when we enter this special case is important for when
  // people design layouts that exactly fit the container "most of the
  // time".

  // XXX Is this check really sufficient to catch all the incremental cases
  // where the ideal case doesn't have a scrollbar?
  if ((aState.mReflowedContentsWithHScrollbar ||
       aState.mReflowedContentsWithVScrollbar) &&
      aState.mVScrollbar != ShowScrollbar::Always &&
      aState.mHScrollbar != ShowScrollbar::Always) {
    nsSize kidSize = GetContainSizeAxes().ContainSize(
        kidDesiredSize.PhysicalSize(), *aState.mReflowInput.mFrame);
    nsSize insideBorderSize = ComputeInsideBorderSize(aState, kidSize);
    nsRect scrolledRect = GetUnsnappedScrolledRectInternal(
        kidDesiredSize.ScrollableOverflow(), insideBorderSize);
    if (nsRect(nsPoint(0, 0), insideBorderSize).Contains(scrolledRect)) {
      // Let's pretend we had no scrollbars coming in here
      kidDesiredSize.mOverflowAreas.Clear();
      ReflowScrolledFrame(aState, false, false, &kidDesiredSize);
    }
  }

  if (IsRootScrollFrameOfDocument()) {
    UpdateMinimumScaleSize(aState.mContentsOverflowAreas.ScrollableOverflow(),
                           kidDesiredSize.PhysicalSize());
  }

  // Try vertical scrollbar settings that leave the vertical scrollbar
  // unchanged. Do this first because changing the vertical scrollbar setting is
  // expensive, forcing a reflow always.

  // Try leaving the horizontal scrollbar unchanged first. This will be more
  // efficient.
  ROOT_SCROLLBAR_LOG("Trying layout1 with %d, %d\n",
                     aState.mReflowedContentsWithHScrollbar,
                     aState.mReflowedContentsWithVScrollbar);
  if (TryLayout(aState, &kidDesiredSize, aState.mReflowedContentsWithHScrollbar,
                aState.mReflowedContentsWithVScrollbar, false)) {
    return;
  }
  ROOT_SCROLLBAR_LOG("Trying layout2 with %d, %d\n",
                     !aState.mReflowedContentsWithHScrollbar,
                     aState.mReflowedContentsWithVScrollbar);
  if (TryLayout(aState, &kidDesiredSize,
                !aState.mReflowedContentsWithHScrollbar,
                aState.mReflowedContentsWithVScrollbar, false)) {
    return;
  }

  // OK, now try toggling the vertical scrollbar. The performance advantage
  // of trying the status-quo horizontal scrollbar state
  // does not exist here (we'll have to reflow due to the vertical scrollbar
  // change), so always try no horizontal scrollbar first.
  bool newVScrollbarState = !aState.mReflowedContentsWithVScrollbar;
  ROOT_SCROLLBAR_LOG("Trying layout3 with %d, %d\n", false, newVScrollbarState);
  if (TryLayout(aState, &kidDesiredSize, false, newVScrollbarState, false)) {
    return;
  }
  ROOT_SCROLLBAR_LOG("Trying layout4 with %d, %d\n", true, newVScrollbarState);
  if (TryLayout(aState, &kidDesiredSize, true, newVScrollbarState, false)) {
    return;
  }

  // OK, we're out of ideas. Try again enabling whatever scrollbars we can
  // enable and force the layout to stick even if it's inconsistent.
  // This just happens sometimes.
  ROOT_SCROLLBAR_LOG("Giving up, adding both scrollbars...\n");
  TryLayout(aState, &kidDesiredSize, aState.mHScrollbar != ShowScrollbar::Never,
            aState.mVScrollbar != ShowScrollbar::Never, true);
}

void ScrollContainerFrame::PlaceScrollArea(ScrollReflowInput& aState,
                                           const nsPoint& aScrollPosition) {
  // Set the x,y of the scrolled frame to the correct value
  mScrolledFrame->SetPosition(ScrollPort().TopLeft() - aScrollPosition);

  if (ChildrenHavePerspective()) {
    // Recompute our scrollable overflow, taking perspective children into
    // account.
    if (RecomputePerspectiveChildrenOverflow(this)) {
      aState.mContentsOverflowAreas = mScrolledFrame->GetOverflowAreas();
    }
    AdjustForPerspective(aState.mContentsOverflowAreas.ScrollableOverflow());
  }
  // Preserve the width or height of empty rects
  const nsSize portSize = ScrollPort().Size();
  nsRect scrolledRect = GetUnsnappedScrolledRectInternal(
      aState.mContentsOverflowAreas.ScrollableOverflow(), portSize);
  nsRect scrolledArea = scrolledRect.UnionEdges(nsRect(nsPoint(), portSize));
  // Store the new overflow area. Note that this changes where an outline
  // of the scrolled frame would be painted, but scrolled frames can't have
  // outlines (the outline would go on this scrollframe instead).
  // Using FinishAndStoreOverflow is needed so the overflow rect gets set
  // correctly.  It also messes with the overflow rect in the 'clip' case, but
  // scrolled frames can't have 'overflow' either.
  OverflowAreas overflow(scrolledArea, scrolledArea);
  mScrolledFrame->FinishAndStoreOverflow(overflow, mScrolledFrame->GetSize());
}

nscoord ScrollContainerFrame::IntrinsicScrollbarGutterSizeAtInlineEdges()
    const {
  const auto wm = GetWritingMode();
  const LogicalMargin gutter(wm, IntrinsicScrollbarGutterSize());
  return gutter.IStartEnd(wm);
}

nsMargin ScrollContainerFrame::IntrinsicScrollbarGutterSize() const {
  if (UseOverlayScrollbars()) {
    // Overlay scrollbars do not consume space per spec.
    return {};
  }

  const auto* styleForScrollbar = nsLayoutUtils::StyleForScrollbar(this);
  const auto& styleScrollbarWidth = ScrollbarWidth(styleForScrollbar);
  if (styleScrollbarWidth == StyleScrollbarWidth::None) {
    // Scrollbar shouldn't appear at all with "scrollbar-width: none".
    return {};
  }

  const auto& styleScrollbarGutter =
      styleForScrollbar->StyleDisplay()->mScrollbarGutter;
  nsMargin gutter =
      ComputeStableScrollbarGutter(styleScrollbarWidth, styleScrollbarGutter);
  if (gutter.LeftRight() == 0 || gutter.TopBottom() == 0) {
    // If there is no stable scrollbar-gutter at vertical or horizontal
    // dimension, check if a scrollbar is always shown at that dimension.
    ScrollStyles scrollStyles = GetScrollStyles();
    const nscoord scrollbarSize =
        GetNonOverlayScrollbarSize(PresContext(), styleScrollbarWidth);
    if (gutter.LeftRight() == 0 &&
        scrollStyles.mVertical == StyleOverflow::Scroll) {
      (IsScrollbarOnRight() ? gutter.right : gutter.left) = scrollbarSize;
    }
    if (gutter.TopBottom() == 0 &&
        scrollStyles.mHorizontal == StyleOverflow::Scroll) {
      // The horizontal scrollbar is always at the bottom side.
      gutter.bottom = scrollbarSize;
    }
  }
  return gutter;
}

nsMargin ScrollContainerFrame::ComputeStableScrollbarGutter(
    const StyleScrollbarWidth& aStyleScrollbarWidth,
    const StyleScrollbarGutter& aStyleScrollbarGutter) const {
  if (UseOverlayScrollbars()) {
    // Overlay scrollbars do not consume space per spec.
    return {};
  }

  if (aStyleScrollbarWidth == StyleScrollbarWidth::None) {
    // Scrollbar shouldn't appear at all with "scrollbar-width: none".
    return {};
  }

  if (aStyleScrollbarGutter == StyleScrollbarGutter::AUTO) {
    // Scrollbars create space depending on the 'overflow' property and whether
    // the content overflows. Callers need to check this scenario if they want
    // to consider the space created by the actual scrollbars.
    return {};
  }

  const bool bothEdges =
      bool(aStyleScrollbarGutter & StyleScrollbarGutter::BOTH_EDGES);
  const bool isVerticalWM = GetWritingMode().IsVertical();
  const nscoord scrollbarSize =
      GetNonOverlayScrollbarSize(PresContext(), aStyleScrollbarWidth);

  nsMargin scrollbarGutter;
  if (bothEdges) {
    if (isVerticalWM) {
      scrollbarGutter.top = scrollbarGutter.bottom = scrollbarSize;
    } else {
      scrollbarGutter.left = scrollbarGutter.right = scrollbarSize;
    }
  } else {
    MOZ_ASSERT(bool(aStyleScrollbarGutter & StyleScrollbarGutter::STABLE),
               "scrollbar-gutter value should be 'stable'!");
    if (isVerticalWM) {
      // The horizontal scrollbar-gutter is always at the bottom side.
      scrollbarGutter.bottom = scrollbarSize;
    } else if (IsScrollbarOnRight()) {
      scrollbarGutter.right = scrollbarSize;
    } else {
      scrollbarGutter.left = scrollbarSize;
    }
  }
  return scrollbarGutter;
}

// Legacy, this sucks!
static bool IsMarqueeScrollbox(const nsIFrame& aScrollFrame) {
  return HTMLMarqueeElement::FromNodeOrNull(aScrollFrame.GetContent());
}

nscoord ScrollContainerFrame::IntrinsicISize(const IntrinsicSizeInput& aInput,
                                             IntrinsicISizeType aType) {
  nscoord result = [&] {
    if (const Maybe<nscoord> containISize = ContainIntrinsicISize()) {
      return *containISize;
    }
    if (aType == IntrinsicISizeType::MinISize &&
        MOZ_UNLIKELY(IsMarqueeScrollbox(*this))) {
      return 0;
    }
    return mScrolledFrame->IntrinsicISize(aInput, aType);
  }();

  return NSCoordSaturatingAdd(result,
                              IntrinsicScrollbarGutterSizeAtInlineEdges());
}

// When we have perspective set on the outer scroll frame, and transformed
// children (possibly with preserve-3d) then the effective transform on the
// child depends on the offset to the scroll frame, which changes as we scroll.
// This perspective transform can cause the element to move relative to the
// scrolled inner frame, which would cause the scrollable length changes during
// scrolling if we didn't account for it. Since we don't want scrollHeight/Width
// and the size of scrollbar thumbs to change during scrolling, we compute the
// scrollable overflow by determining the scroll position at which the child
// becomes completely visible within the scrollport rather than using the union
// of the overflow areas at their current position.
static void GetScrollableOverflowForPerspective(
    nsIFrame* aScrolledFrame, nsIFrame* aCurrentFrame,
    const nsRect& aScrollPort, nsPoint aOffset,
    nsRect& aScrolledFrameOverflowArea) {
  // Iterate over all children except pop-ups.
  for (const auto& [list, listID] : aCurrentFrame->ChildLists()) {
    for (nsIFrame* child : list) {
      nsPoint offset = aOffset;

      // When we reach a direct child of the scroll, then we record the offset
      // to convert from that frame's coordinate into the scroll frame's
      // coordinates. Preserve-3d descendant frames use the same offset as their
      // ancestors, since TransformRect already converts us into the coordinate
      // space of the preserve-3d root.
      if (aScrolledFrame == aCurrentFrame) {
        offset = child->GetPosition();
      }

      if (child->Extend3DContext()) {
        // If we're a preserve-3d frame, then recurse and include our
        // descendants since overflow of preserve-3d frames is only included
        // in the post-transform overflow area of the preserve-3d root frame.
        GetScrollableOverflowForPerspective(aScrolledFrame, child, aScrollPort,
                                            offset, aScrolledFrameOverflowArea);
      }

      // If we're transformed, then we want to consider the possibility that
      // this frame might move relative to the scrolled frame when scrolling.
      // For preserve-3d, leaf frames have correct overflow rects relative to
      // themselves. preserve-3d 'nodes' (intermediate frames and the root) have
      // only their untransformed children included in their overflow relative
      // to self, which is what we want to include here.
      if (child->IsTransformed()) {
        // Compute the overflow rect for this leaf transform frame in the
        // coordinate space of the scrolled frame.
        nsPoint scrollPos = aScrolledFrame->GetPosition();
        nsRect preScroll, postScroll;
        {
          // TODO: Can we reuse the reference box?
          TransformReferenceBox refBox(child);
          preScroll = nsDisplayTransform::TransformRect(
              child->ScrollableOverflowRectRelativeToSelf(), child, refBox);
        }

        // Temporarily override the scroll position of the scrolled frame by
        // 10 CSS pixels, and then recompute what the overflow rect would be.
        // This scroll position may not be valid, but that shouldn't matter
        // for our calculations.
        {
          aScrolledFrame->SetPosition(scrollPos + nsPoint(600, 600));
          TransformReferenceBox refBox(child);
          postScroll = nsDisplayTransform::TransformRect(
              child->ScrollableOverflowRectRelativeToSelf(), child, refBox);
          aScrolledFrame->SetPosition(scrollPos);
        }

        // Compute how many app units the overflow rects moves by when we adjust
        // the scroll position by 1 app unit.
        double rightDelta =
            (postScroll.XMost() - preScroll.XMost() + 600.0) / 600.0;
        double bottomDelta =
            (postScroll.YMost() - preScroll.YMost() + 600.0) / 600.0;

        // We can't ever have negative scrolling.
        NS_ASSERTION(rightDelta > 0.0f && bottomDelta > 0.0f,
                     "Scrolling can't be reversed!");

        // Move preScroll into the coordinate space of the scrollport.
        preScroll += offset + scrollPos;

        // For each of the four edges of preScroll, figure out how far they
        // extend beyond the scrollport. Ignore negative values since that means
        // that side is already scrolled in to view and we don't need to add
        // overflow to account for it.
        nsMargin overhang(std::max(0, aScrollPort.Y() - preScroll.Y()),
                          std::max(0, preScroll.XMost() - aScrollPort.XMost()),
                          std::max(0, preScroll.YMost() - aScrollPort.YMost()),
                          std::max(0, aScrollPort.X() - preScroll.X()));

        // Scale according to rightDelta/bottomDelta to adjust for the different
        // scroll rates.
        overhang.top = NSCoordSaturatingMultiply(
            overhang.top, static_cast<float>(1 / bottomDelta));
        overhang.right = NSCoordSaturatingMultiply(
            overhang.right, static_cast<float>(1 / rightDelta));
        overhang.bottom = NSCoordSaturatingMultiply(
            overhang.bottom, static_cast<float>(1 / bottomDelta));
        overhang.left = NSCoordSaturatingMultiply(
            overhang.left, static_cast<float>(1 / rightDelta));

        // Take the minimum overflow rect that would allow the current scroll
        // position, using the size of the scroll port and offset by the
        // inverse of the scroll position.
        nsRect overflow = aScrollPort - scrollPos;

        // Expand it by our margins to get an overflow rect that would allow all
        // edges of our transformed content to be scrolled into view.
        overflow.Inflate(overhang);

        // Merge it with the combined overflow
        aScrolledFrameOverflowArea.UnionRect(aScrolledFrameOverflowArea,
                                             overflow);
      } else if (aCurrentFrame == aScrolledFrame) {
        aScrolledFrameOverflowArea.UnionRect(
            aScrolledFrameOverflowArea,
            child->ScrollableOverflowRectRelativeToParent());
      }
    }
  }
}

BaselineSharingGroup ScrollContainerFrame::GetDefaultBaselineSharingGroup()
    const {
  return mScrolledFrame->GetDefaultBaselineSharingGroup();
}

nscoord ScrollContainerFrame::SynthesizeFallbackBaseline(
    mozilla::WritingMode aWM, BaselineSharingGroup aBaselineGroup) const {
  // Margin-end even for central baselines.
  if (aWM.IsLineInverted()) {
    return -GetLogicalUsedMargin(aWM).BStart(aWM);
  }
  return aBaselineGroup == BaselineSharingGroup::First
             ? BSize(aWM) + GetLogicalUsedMargin(aWM).BEnd(aWM)
             : -GetLogicalUsedMargin(aWM).BEnd(aWM);
}

Maybe<nscoord> ScrollContainerFrame::GetNaturalBaselineBOffset(
    WritingMode aWM, BaselineSharingGroup aBaselineGroup,
    BaselineExportContext aExportContext) const {
  // Block containers (except buttons) that are scrollable always have a last
  // baseline that are synthesized from block-end margin edge.
  // Note(dshin): This behaviour is really only relevant to `inline-block`
  // alignment context. In the context of table/flex/grid alignment, first/last
  // baselines are calculated through `GetFirstLineBaseline`, which does
  // calculations of its own.
  // https://drafts.csswg.org/css-align/#baseline-export
  if (aExportContext == BaselineExportContext::LineLayout &&
      aBaselineGroup == BaselineSharingGroup::Last) {
    if (nsBlockFrame* bf = do_QueryFrame(mScrolledFrame);
        bf && !bf->IsButtonLike()) {
      return Some(SynthesizeFallbackBaseline(aWM, aBaselineGroup));
    }
  }

  if (StyleDisplay()->IsContainLayout()) {
    return Nothing{};
  }

  // OK, here's where we defer to our scrolled frame.
  return mScrolledFrame
      ->GetNaturalBaselineBOffset(aWM, aBaselineGroup, aExportContext)
      .map([this, aWM](nscoord aBaseline) {
        // We have to add our border BStart thickness to whatever it returns, to
        // produce an offset in our frame-rect's coordinate system. (We don't
        // have to add padding, because the scrolled frame handles our padding.)
        LogicalMargin border = GetLogicalUsedBorder(aWM);
        const auto bSize = GetLogicalSize(aWM).BSize(aWM);
        // Clamp the baseline to the border rect. See bug 1791069.
        return CSSMinMax(border.BStart(aWM) + aBaseline, 0, bSize);
      });
}

void ScrollContainerFrame::AdjustForPerspective(nsRect& aScrollableOverflow) {
  // If we have perspective that is being applied to our children, then
  // the effective transform on the child depends on the relative position
  // of the child to us and changes during scrolling.
  MOZ_ASSERT(ChildrenHavePerspective());
  aScrollableOverflow.SetEmpty();
  GetScrollableOverflowForPerspective(mScrolledFrame, mScrolledFrame,
                                      ScrollPort(), nsPoint(),
                                      aScrollableOverflow);
}

void ScrollContainerFrame::Reflow(nsPresContext* aPresContext,
                                  ReflowOutput& aDesiredSize,
                                  const ReflowInput& aReflowInput,
                                  nsReflowStatus& aStatus) {
  MarkInReflow();
  DO_GLOBAL_REFLOW_COUNT("ScrollContainerFrame");
  MOZ_ASSERT(aStatus.IsEmpty(), "Caller should pass a fresh reflow status!");

  HandleScrollbarStyleSwitching();

  ScrollReflowInput state(this, aReflowInput);

  //------------ Handle Incremental Reflow -----------------
  bool reflowHScrollbar = true;
  bool reflowVScrollbar = true;
  bool reflowScrollCorner = true;
  if (!aReflowInput.ShouldReflowAllKids()) {
    auto NeedsReflow = [](const nsIFrame* aFrame) {
      return aFrame && aFrame->IsSubtreeDirty();
    };

    reflowHScrollbar = NeedsReflow(mHScrollbarBox);
    reflowVScrollbar = NeedsReflow(mVScrollbarBox);
    reflowScrollCorner =
        NeedsReflow(mScrollCornerBox) || NeedsReflow(mResizerBox);
  }

  if (mIsRoot) {
    reflowScrollCorner = false;
  }

  const nsRect oldScrollPort = ScrollPort();
  nsRect oldScrolledAreaBounds =
      mScrolledFrame->ScrollableOverflowRectRelativeToParent();
  nsPoint oldScrollPosition = GetScrollPosition();

  ReflowContents(state, aDesiredSize);

  nsSize layoutSize =
      mIsUsingMinimumScaleSize ? mMinimumScaleSize : state.mInsideBorderSize;
  aDesiredSize.Width() = layoutSize.width + state.mScrollportMargin.LeftRight();
  aDesiredSize.Height() =
      layoutSize.height + state.mScrollportMargin.TopBottom();

  // Set the size of the frame now since computing the perspective-correct
  // overflow (within PlaceScrollArea) can rely on it.
  SetSize(aDesiredSize.GetWritingMode(),
          aDesiredSize.Size(aDesiredSize.GetWritingMode()));

  // Restore the old scroll position, for now, even if that's not valid anymore
  // because we changed size. We'll fix it up in a post-reflow callback, because
  // our current size may only be temporary (e.g. we're compute XUL desired
  // sizes).
  PlaceScrollArea(state, oldScrollPosition);
  if (!mPostedReflowCallback) {
    // Make sure we'll try scrolling to restored position
    PresShell()->PostReflowCallback(this);
    mPostedReflowCallback = true;
  }

  bool didOnlyHScrollbar = mOnlyNeedHScrollbarToScrollVVInsideLV;
  bool didOnlyVScrollbar = mOnlyNeedVScrollbarToScrollVVInsideLV;
  mOnlyNeedHScrollbarToScrollVVInsideLV =
      state.mOnlyNeedHScrollbarToScrollVVInsideLV;
  mOnlyNeedVScrollbarToScrollVVInsideLV =
      state.mOnlyNeedVScrollbarToScrollVVInsideLV;

  bool didHaveHScrollbar = mHasHorizontalScrollbar;
  bool didHaveVScrollbar = mHasVerticalScrollbar;
  mHasHorizontalScrollbar = state.mShowHScrollbar;
  mHasVerticalScrollbar = state.mShowVScrollbar;
  const nsRect& newScrollPort = ScrollPort();
  nsRect newScrolledAreaBounds =
      mScrolledFrame->ScrollableOverflowRectRelativeToParent();
  if (mSkippedScrollbarLayout || reflowHScrollbar || reflowVScrollbar ||
      reflowScrollCorner || HasAnyStateBits(NS_FRAME_IS_DIRTY) ||
      didHaveHScrollbar != state.mShowHScrollbar ||
      didHaveVScrollbar != state.mShowVScrollbar ||
      didOnlyHScrollbar != mOnlyNeedHScrollbarToScrollVVInsideLV ||
      didOnlyVScrollbar != mOnlyNeedVScrollbarToScrollVVInsideLV ||
      !oldScrollPort.IsEqualEdges(newScrollPort) ||
      !oldScrolledAreaBounds.IsEqualEdges(newScrolledAreaBounds)) {
    mSkippedScrollbarLayout = false;
    ScrollContainerFrame::SetScrollbarVisibility(mHScrollbarBox,
                                                 state.mShowHScrollbar);
    ScrollContainerFrame::SetScrollbarVisibility(mVScrollbarBox,
                                                 state.mShowVScrollbar);
    // place and reflow scrollbars
    const nsRect insideBorderArea(
        nsPoint(state.mScrollportMargin.left, state.mScrollportMargin.top),
        layoutSize);
    LayoutScrollbars(state, insideBorderArea, oldScrollPort);
  }

  // Layout the button box (e.g., number spin-box) if present.
  if (nsIFrame* buttonBox = GetButtonBoxFrame()) {
    LayoutButtonBox(state, buttonBox);
  }
  if (mIsRoot) {
    if (RefPtr<MobileViewportManager> manager =
            PresShell()->GetMobileViewportManager()) {
      // Note that this runs during layout, and when we get here the root
      // scrollframe has already been laid out. It may have added or removed
      // scrollbars as a result of that layout, so we need to ensure the
      // visual viewport is updated to account for that before we read the
      // visual viewport size.
      manager->UpdateVisualViewportSizeForPotentialScrollbarChange();
    } else if (oldScrollPort.Size() != newScrollPort.Size()) {
      // We want to make sure to send a visual viewport resize event if the
      // scrollport changed sizes for root scroll frames. The
      // MobileViewportManager will do that, but if we don't have one (ie we
      // aren't a root content document for example) we have to send one
      // ourselves.
      if (auto* window = nsGlobalWindowInner::Cast(
              aPresContext->Document()->GetInnerWindow())) {
        window->VisualViewport()->PostResizeEvent();
      }
    }
  }

  // Note that we need to do this after the
  // UpdateVisualViewportSizeForPotentialScrollbarChange call above because that
  // is what updates the visual viewport size and we need it to be up to date.
  if (mIsRoot && !state.OverlayScrollbars() &&
      (didHaveHScrollbar != state.mShowHScrollbar ||
       didHaveVScrollbar != state.mShowVScrollbar ||
       didOnlyHScrollbar != mOnlyNeedHScrollbarToScrollVVInsideLV ||
       didOnlyVScrollbar != mOnlyNeedVScrollbarToScrollVVInsideLV) &&
      PresShell()->IsVisualViewportOffsetSet()) {
    // Removing layout/classic scrollbars can make a previously valid vvoffset
    // invalid. For example, if we are zoomed in on an overflow hidden document
    // and then zoom back out, when apz reaches the initial resolution (ie 1.0)
    // it won't know that we can remove the scrollbars, so the vvoffset can
    // validly be upto the width/height of the scrollbars. After we reflow and
    // remove the scrollbars the only valid vvoffset is (0,0). We could wait
    // until we send the new frame metrics to apz and then have it reply with
    // the new corrected vvoffset but having an inconsistent vvoffset causes
    // problems so trigger the vvoffset to be re-set and re-clamped in
    // ReflowFinished.
    mReclampVVOffsetInReflowFinished = true;
  }

  aDesiredSize.SetOverflowAreasToDesiredBounds();

  UpdateSticky();
  FinishReflowWithAbsoluteFrames(aPresContext, aDesiredSize, aReflowInput,
                                 aStatus);

  if (!InInitialReflow() && !mHadNonInitialReflow) {
    mHadNonInitialReflow = true;
  }

  if (mIsRoot && !oldScrolledAreaBounds.IsEqualEdges(newScrolledAreaBounds)) {
    PostScrolledAreaEvent();
  }

  UpdatePrevScrolledRect();

  aStatus.Reset();  // This type of frame can't be split.
  PostOverflowEvent();
}

void ScrollContainerFrame::DidReflow(nsPresContext* aPresContext,
                                     const ReflowInput* aReflowInput) {
  nsContainerFrame::DidReflow(aPresContext, aReflowInput);
  if (NeedsResnap()) {
    PostPendingResnap();
  } else {
    PresShell()->PostPendingScrollAnchorAdjustment(Anchor());
  }
}

////////////////////////////////////////////////////////////////////////////////

#ifdef DEBUG_FRAME_DUMP
nsresult ScrollContainerFrame::GetFrameName(nsAString& aResult) const {
  return MakeFrameName(u"ScrollContainer"_ns, aResult);
}
#endif

#ifdef ACCESSIBILITY
a11y::AccType ScrollContainerFrame::AccessibleType() {
  if (IsTableCaption()) {
    return GetRect().IsEmpty() ? a11y::eNoType : a11y::eHTMLCaptionType;
  }

  // Create an accessible regardless of focusable state because the state can be
  // changed during frame life cycle without any notifications to accessibility.
  if (Style()->IsPseudoElement() ||
      GetScrollStyles().IsHiddenInBothDirections()) {
    return a11y::eNoType;
  }

  return a11y::eHyperTextType;
}
#endif

NS_QUERYFRAME_HEAD(ScrollContainerFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
  NS_QUERYFRAME_ENTRY(nsIStatefulFrame)
  NS_QUERYFRAME_ENTRY(nsIScrollbarMediator)
  NS_QUERYFRAME_ENTRY(ScrollContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsContainerFrame)

nsMargin ScrollContainerFrame::GetDesiredScrollbarSizes() const {
  if (UseOverlayScrollbars()) {
    return {};
  }

  const auto scrollbarWidth = ScrollbarWidth();
  if (scrollbarWidth == StyleScrollbarWidth::None) {
    return {};
  }

  ScrollStyles styles = GetScrollStyles();
  nsMargin result(0, 0, 0, 0);

  auto size = GetNonOverlayScrollbarSize(PresContext(), scrollbarWidth);
  if (styles.mVertical != StyleOverflow::Hidden) {
    if (IsScrollbarOnRight()) {
      result.right = size;
    } else {
      result.left = size;
    }
  }

  if (styles.mHorizontal != StyleOverflow::Hidden) {
    // We don't currently support any scripts that would require a scrollbar
    // at the top. (Are there any?)
    result.bottom = size;
  }

  return result;
}

nscoord ScrollContainerFrame::GetNonOverlayScrollbarSize(
    const nsPresContext* aPc, StyleScrollbarWidth aScrollbarWidth) {
  const auto size = aPc->Theme()->GetScrollbarSize(aPc, aScrollbarWidth,
                                                   nsITheme::Overlay::No);
  return aPc->DevPixelsToAppUnits(size);
}

void ScrollContainerFrame::HandleScrollbarStyleSwitching() {
  // Check if we switched between scrollbar styles.
  // We need to check the global UseOverlayScrollbars() because whether to
  // disable overlay scrollbars for each scroll container depends on
  // ::-webkit-scrollbar styles on the container and disabling overlay for
  // individual container uses this mScrollbarActivity.
  if (mScrollbarActivity && !PresContext()->UseOverlayScrollbars()) {
    mScrollbarActivity->Destroy();
    mScrollbarActivity = nullptr;
  } else if (!mScrollbarActivity && PresContext()->UseOverlayScrollbars()) {
    mScrollbarActivity = MakeRefPtr<ScrollbarActivity>(this);
  }
}

void ScrollContainerFrame::SetScrollableByAPZ(bool aScrollable) {
  mScrollableByAPZ = aScrollable;
}

void ScrollContainerFrame::SetZoomableByAPZ(bool aZoomable) {
  if (!nsLayoutUtils::UsesAsyncScrolling(this)) {
    // If APZ is disabled on this window, then we're never actually going to
    // do any zooming. So we don't need to do any of the setup for it. Note
    // that this function gets called from ZoomConstraintsClient even if APZ
    // is disabled to indicate the zoomability of content.
    aZoomable = false;
  }
  if (mZoomableByAPZ != aZoomable) {
    // We might be changing the result of DecideScrollableLayer() so schedule a
    // paint to make sure we pick up the result of that change.
    mZoomableByAPZ = aZoomable;
    SchedulePaint();
  }
}

void ScrollContainerFrame::SetHasOutOfFlowContentInsideFilter() {
  mHasOutOfFlowContentInsideFilter = true;
}

bool ScrollContainerFrame::WantAsyncScroll(
    NonZeroScrollRangeOnly aNonZeroScrollRangeOnly) const {
  if (!bool(aNonZeroScrollRangeOnly)) {
    const nsStyleDisplay* disp = GetFrameForStyle()->StyleDisplay();
    if (disp->mOverscrollBehaviorX != StyleOverscrollBehavior::Auto ||
        disp->mOverscrollBehaviorY != StyleOverscrollBehavior::Auto) {
      return true;
    }
  }

  ScrollStyles styles = GetScrollStyles();

  // First, as an optimization because getting the scrollrange is
  // relatively slow, check overflow hidden and not a zoomed scroll frame.
  if (styles.mHorizontal == StyleOverflow::Hidden &&
      styles.mVertical == StyleOverflow::Hidden) {
    if (!mIsRoot || GetVisualViewportSize() == mScrollPort.Size()) {
      return false;
    }
  }

  nscoord oneDevPixel =
      GetScrolledFrame()->PresContext()->AppUnitsPerDevPixel();
  nsRect scrollRange = GetLayoutScrollRange();

  bool isVScrollable = scrollRange.height >= oneDevPixel &&
                       styles.mVertical != StyleOverflow::Hidden;
  bool isHScrollable = scrollRange.width >= oneDevPixel &&
                       styles.mHorizontal != StyleOverflow::Hidden;

  if (isHScrollable || isVScrollable) {
    return true;
  }

  // If the page has a visual viewport size that's different from
  // the layout viewport size at the current zoom level, we need to be
  // able to scroll the visual viewport inside the layout viewport
  // even if the page is not zoomable.
  return mIsRoot && GetVisualViewportSize() != mScrollPort.Size() &&
         !GetVisualScrollRange().IsEqualInterior(scrollRange);
}

static nsRect GetOnePixelRangeAroundPoint(const nsPoint& aPoint,
                                          bool aIsHorizontal) {
  nsRect allowedRange(aPoint, nsSize());
  nscoord halfPixel = nsPresContext::CSSPixelsToAppUnits(0.5f);
  if (aIsHorizontal) {
    allowedRange.x = aPoint.x - halfPixel;
    allowedRange.width = halfPixel * 2 - 1;
  } else {
    allowedRange.y = aPoint.y - halfPixel;
    allowedRange.height = halfPixel * 2 - 1;
  }
  return allowedRange;
}

void ScrollContainerFrame::ScrollByPage(nsScrollbarFrame* aScrollbar,
                                        int32_t aDirection,
                                        ScrollSnapFlags aSnapFlags) {
  ScrollByUnit(aScrollbar, ScrollMode::Smooth, aDirection, ScrollUnit::PAGES,
               aSnapFlags);
}

void ScrollContainerFrame::ScrollByWhole(nsScrollbarFrame* aScrollbar,
                                         int32_t aDirection,
                                         ScrollSnapFlags aSnapFlags) {
  ScrollByUnit(aScrollbar, ScrollMode::Instant, aDirection, ScrollUnit::WHOLE,
               aSnapFlags);
}

void ScrollContainerFrame::ScrollByLine(nsScrollbarFrame* aScrollbar,
                                        int32_t aDirection,
                                        ScrollSnapFlags aSnapFlags) {
  bool isHorizontal = aScrollbar->IsHorizontal();
  nsIntPoint delta;
  if (isHorizontal) {
    const double kScrollMultiplier =
        StaticPrefs::toolkit_scrollbox_horizontalScrollDistance();
    delta.x = static_cast<int32_t>(aDirection * kScrollMultiplier);
    if (GetLineScrollAmount().width * delta.x > GetPageScrollAmount().width) {
      // The scroll frame is so small that the delta would be more
      // than an entire page.  Scroll by one page instead to maintain
      // context.
      ScrollByPage(aScrollbar, aDirection);
      return;
    }
  } else {
    const double kScrollMultiplier =
        StaticPrefs::toolkit_scrollbox_verticalScrollDistance();
    delta.y = static_cast<int32_t>(aDirection * kScrollMultiplier);
    if (GetLineScrollAmount().height * delta.y > GetPageScrollAmount().height) {
      // The scroll frame is so small that the delta would be more
      // than an entire page.  Scroll by one page instead to maintain
      // context.
      ScrollByPage(aScrollbar, aDirection);
      return;
    }
  }

  nsIntPoint overflow;
  ScrollBy(delta, ScrollUnit::LINES, ScrollMode::Smooth, &overflow,
           ScrollOrigin::Other, NOT_MOMENTUM, aSnapFlags);
}

void ScrollContainerFrame::RepeatButtonScroll(nsScrollbarFrame* aScrollbar) {
  aScrollbar->MoveToNewPosition();
}

void ScrollContainerFrame::ThumbMoved(nsScrollbarFrame* aScrollbar,
                                      nscoord aOldPos, nscoord aNewPos) {
  MOZ_ASSERT(aScrollbar != nullptr);
  bool isHorizontal = aScrollbar->IsHorizontal();
  nsPoint current = GetScrollPosition();
  nsPoint dest = current;
  if (isHorizontal) {
    dest.x = IsPhysicalLTR() ? aNewPos : aNewPos - GetLayoutScrollRange().width;
  } else {
    dest.y = aNewPos;
  }
  nsRect allowedRange = GetOnePixelRangeAroundPoint(dest, isHorizontal);

  // Don't try to scroll if we're already at an acceptable place.
  // Don't call Contains here since Contains returns false when the point is
  // on the bottom or right edge of the rectangle.
  if (allowedRange.ClampPoint(current) == current) {
    return;
  }

  ScrollToWithOrigin(
      dest, &allowedRange,
      ScrollOperationParams{ScrollMode::Instant, ScrollOrigin::Other});
}

void ScrollContainerFrame::ScrollbarReleased(nsScrollbarFrame* aScrollbar) {
  // Scrollbar scrolling does not result in fling gestures, clear any
  // accumulated velocity
  mVelocityQueue.Reset();

  // Perform scroll snapping, if needed.  Scrollbar movement uses the same
  // smooth scrolling animation as keyboard scrolling.
  bool didSnap = ScrollSnap(mDestination, ScrollMode::Smooth);

  SCROLLEND_LOG("%s: did-snap=%s scrollend-pending=%s", __FUNCTION__,
                didSnap ? "true" : "false",
                mScrollbarClickAndHoldScrollendPending ? "true" : "false");
  // If a scrollbar click and hold scroll has occured and a scroll-snap will
  // not occur, post a scrollend event. If a scroll snap will occur. Let the
  // scroll-snap scroll post the scrollend event.
  if (!didSnap && mScrollbarClickAndHoldScrollendPending) {
    PostScrollEndEvent();
  }

  mScrollbarClickAndHoldScrollendPending = false;
}

void ScrollContainerFrame::ScrollByUnit(nsScrollbarFrame* aScrollbar,
                                        ScrollMode aMode, int32_t aDirection,
                                        ScrollUnit aUnit,
                                        ScrollSnapFlags aSnapFlags) {
  MOZ_ASSERT(aScrollbar != nullptr);
  bool isHorizontal = aScrollbar->IsHorizontal();
  nsIntPoint delta;
  if (isHorizontal) {
    delta.x = aDirection;
  } else {
    delta.y = aDirection;
  }
  nsIntPoint overflow;
  ScrollBy(delta, aUnit, aMode, &overflow, ScrollOrigin::Other, NOT_MOMENTUM,
           aSnapFlags);
}

//-------------------- Helper ----------------------

// AsyncSmoothMSDScroll has ref counting.
class ScrollContainerFrame::AsyncSmoothMSDScroll final
    : public nsARefreshObserver {
 public:
  AsyncSmoothMSDScroll(const nsPoint& aInitialPosition,
                       const nsPoint& aInitialDestination,
                       const nsSize& aInitialVelocity, const nsRect& aRange,
                       const mozilla::TimeStamp& aStartTime,
                       nsPresContext* aPresContext,
                       UniquePtr<ScrollSnapTargetIds> aSnapTargetIds,
                       ScrollTriggeredByScript aTriggeredByScript)
      : mXAxisModel(aInitialPosition.x, aInitialDestination.x,
                    aInitialVelocity.width,
                    StaticPrefs::layout_css_scroll_snap_spring_constant(),
                    StaticPrefs::layout_css_scroll_snap_damping_ratio()),
        mYAxisModel(aInitialPosition.y, aInitialDestination.y,
                    aInitialVelocity.height,
                    StaticPrefs::layout_css_scroll_snap_spring_constant(),
                    StaticPrefs::layout_css_scroll_snap_damping_ratio()),
        mRange(aRange),
        mStartPosition(aInitialPosition),
        mLastRefreshTime(aStartTime),
        mCallee(nullptr),
        mOneDevicePixelInAppUnits(aPresContext->DevPixelsToAppUnits(1)),
        mSnapTargetIds(std::move(aSnapTargetIds)),
        mTriggeredByScript(aTriggeredByScript) {}

  NS_INLINE_DECL_REFCOUNTING(AsyncSmoothMSDScroll, override)

  nsSize GetVelocity() {
    // In nscoords per second
    return nsSize(mXAxisModel.GetVelocity(), mYAxisModel.GetVelocity());
  }

  nsPoint GetPosition() {
    // In nscoords
    return nsPoint(NSToCoordRound(mXAxisModel.GetPosition()),
                   NSToCoordRound(mYAxisModel.GetPosition()));
  }

  void SetDestination(const nsPoint& aDestination,
                      UniquePtr<ScrollSnapTargetIds> aSnapTargetIds,
                      ScrollTriggeredByScript aTriggeredByScript) {
    mXAxisModel.SetDestination(static_cast<int32_t>(aDestination.x));
    mYAxisModel.SetDestination(static_cast<int32_t>(aDestination.y));
    mSnapTargetIds = std::move(aSnapTargetIds);
    mTriggeredByScript = aTriggeredByScript;
  }

  void SetRange(const nsRect& aRange) { mRange = aRange; }

  nsRect GetRange() { return mRange; }

  nsPoint GetStartPosition() { return mStartPosition; }

  void Simulate(const TimeDuration& aDeltaTime) {
    mXAxisModel.Simulate(aDeltaTime);
    mYAxisModel.Simulate(aDeltaTime);

    nsPoint desired = GetPosition();
    nsPoint clamped = mRange.ClampPoint(desired);
    if (desired.x != clamped.x) {
      // The scroll has hit the "wall" at the left or right edge of the allowed
      // scroll range.
      // Absorb the impact to avoid bounceback effect.
      mXAxisModel.SetVelocity(0.0);
      mXAxisModel.SetPosition(clamped.x);
    }

    if (desired.y != clamped.y) {
      // The scroll has hit the "wall" at the left or right edge of the allowed
      // scroll range.
      // Absorb the impact to avoid bounceback effect.
      mYAxisModel.SetVelocity(0.0);
      mYAxisModel.SetPosition(clamped.y);
    }
  }

  bool IsFinished() {
    return mXAxisModel.IsFinished(mOneDevicePixelInAppUnits) &&
           mYAxisModel.IsFinished(mOneDevicePixelInAppUnits);
  }

  virtual void WillRefresh(mozilla::TimeStamp aTime) override {
    mozilla::TimeDuration deltaTime = aTime - mLastRefreshTime;
    mLastRefreshTime = aTime;

    // The callback may release "this".
    // We don't access members after returning, so no need for KungFuDeathGrip.
    ScrollContainerFrame::AsyncSmoothMSDScrollCallback(mCallee, deltaTime);
  }

  /*
   * Set a refresh observer for smooth scroll iterations (and start observing).
   * Should be used at most once during the lifetime of this object.
   */
  void SetRefreshObserver(ScrollContainerFrame* aCallee) {
    MOZ_ASSERT(aCallee,
               "AsyncSmoothMSDScroll::SetRefreshObserver needs "
               "a non-null aCallee in order to get a refresh driver");
    MOZ_RELEASE_ASSERT(!mCallee,
                       "AsyncSmoothMSDScroll::SetRefreshObserver "
                       "shouldn't be called if we're already registered with "
                       "a refresh driver, via a preexisting mCallee");

    RefreshDriver(aCallee)->AddRefreshObserver(this, FlushType::Style,
                                               "Smooth scroll (MSD) animation");
    mCallee = aCallee;
  }

  /**
   * The mCallee holds a strong ref to us since the refresh driver doesn't.
   * Our dtor and mCallee's Destroy() method both call RemoveObserver() -
   * whichever comes first removes us from the refresh driver.
   */
  void RemoveObserver() {
    if (mCallee) {
      RefreshDriver(mCallee)->RemoveRefreshObserver(this, FlushType::Style);
      mCallee = nullptr;
    }
  }

  UniquePtr<ScrollSnapTargetIds> TakeSnapTargetIds() {
    return std::move(mSnapTargetIds);
  }

  bool WasTriggeredByScript() const {
    return mTriggeredByScript == ScrollTriggeredByScript::Yes;
  }

 private:
  // Private destructor, to discourage deletion outside of Release():
  ~AsyncSmoothMSDScroll() { RemoveObserver(); }

  nsRefreshDriver* RefreshDriver(ScrollContainerFrame* aCallee) {
    return aCallee->PresContext()->RefreshDriver();
  }

  mozilla::layers::AxisPhysicsMSDModel mXAxisModel, mYAxisModel;
  nsRect mRange;
  nsPoint mStartPosition;
  mozilla::TimeStamp mLastRefreshTime;
  ScrollContainerFrame* mCallee;
  nscoord mOneDevicePixelInAppUnits;
  UniquePtr<ScrollSnapTargetIds> mSnapTargetIds;
  ScrollTriggeredByScript mTriggeredByScript;
};

// AsyncScroll has ref counting.
class ScrollContainerFrame::AsyncScroll final : public nsARefreshObserver {
 public:
  typedef mozilla::TimeStamp TimeStamp;
  typedef mozilla::TimeDuration TimeDuration;

  explicit AsyncScroll(ScrollTriggeredByScript aTriggeredByScript)
      : mOrigin(ScrollOrigin::NotSpecified),
        mCallee(nullptr),
        mTriggeredByScript(aTriggeredByScript) {}

 private:
  // Private destructor, to discourage deletion outside of Release():
  ~AsyncScroll() { RemoveObserver(); }

 public:
  void InitSmoothScroll(TimeStamp aTime, nsPoint aInitialPosition,
                        nsPoint aDestination, ScrollOrigin aOrigin,
                        const nsRect& aRange, const nsSize& aCurrentVelocity,
                        UniquePtr<ScrollSnapTargetIds> aSnapTargetIds);
  void Init(nsPoint aInitialPosition, const nsRect& aRange,
            UniquePtr<ScrollSnapTargetIds> aSnapTargetIds) {
    mAnimationPhysics = nullptr;
    mRange = aRange;
    mStartPosition = aInitialPosition;
    mSnapTargetIds = std::move(aSnapTargetIds);
  }

  bool IsSmoothScroll() { return mAnimationPhysics != nullptr; }

  bool IsFinished(const TimeStamp& aTime) const {
    MOZ_RELEASE_ASSERT(mAnimationPhysics);
    return mAnimationPhysics->IsFinished(aTime);
  }

  nsPoint PositionAt(const TimeStamp& aTime) const {
    MOZ_RELEASE_ASSERT(mAnimationPhysics);
    return mAnimationPhysics->PositionAt(aTime);
  }

  nsSize VelocityAt(const TimeStamp& aTime) const {
    MOZ_RELEASE_ASSERT(mAnimationPhysics);
    return mAnimationPhysics->VelocityAt(aTime);
  }

  nsPoint GetStartPosition() const { return mStartPosition; }

  // Most recent scroll origin.
  ScrollOrigin mOrigin;

  // Allowed destination positions around mDestination
  nsRect mRange;

  // Initial position where the async scroll was triggered.
  nsPoint mStartPosition;

 private:
  void InitPreferences(TimeStamp aTime, nsAtom* aOrigin);

  UniquePtr<ScrollAnimationPhysics> mAnimationPhysics;

  // The next section is observer/callback management
  // Bodies of WillRefresh and RefreshDriver contain ScrollContainerFrame
  // specific code.
 public:
  NS_INLINE_DECL_REFCOUNTING(AsyncScroll, override)

  /*
   * Set a refresh observer for smooth scroll iterations (and start observing).
   * Should be used at most once during the lifetime of this object.
   */
  void SetRefreshObserver(ScrollContainerFrame* aCallee) {
    MOZ_ASSERT(aCallee,
               "AsyncScroll::SetRefreshObserver needs "
               "a non-null aCallee in order to get a refresh driver");
    MOZ_RELEASE_ASSERT(!mCallee,
                       "AsyncScroll::SetRefreshObserver "
                       "shouldn't be called if we're already registered with "
                       "a refresh driver, via a preexisting mCallee");

    RefreshDriver(aCallee)->AddRefreshObserver(this, FlushType::Style,
                                               "Smooth scroll animation");
    mCallee = aCallee;
    auto* presShell = mCallee->PresShell();
    MOZ_ASSERT(presShell);
    presShell->SuppressDisplayport(true);
  }

  virtual void WillRefresh(mozilla::TimeStamp aTime) override {
    // The callback may release "this".
    // We don't access members after returning, so no need for KungFuDeathGrip.
    ScrollContainerFrame::AsyncScrollCallback(mCallee, aTime);
  }

  /**
   * The mCallee holds a strong ref to us since the refresh driver doesn't.
   * Our dtor and mCallee's Destroy() method both call RemoveObserver() -
   * whichever comes first removes us from the refresh driver.
   */
  void RemoveObserver() {
    if (mCallee) {
      RefreshDriver(mCallee)->RemoveRefreshObserver(this, FlushType::Style);
      auto* presShell = mCallee->PresShell();
      MOZ_ASSERT(presShell);
      presShell->SuppressDisplayport(false);
      mCallee = nullptr;
    }
  }

  UniquePtr<ScrollSnapTargetIds> TakeSnapTargetIds() {
    return std::move(mSnapTargetIds);
  }

  bool WasTriggeredByScript() const {
    return mTriggeredByScript == ScrollTriggeredByScript::Yes;
  }

 private:
  ScrollContainerFrame* mCallee;
  UniquePtr<ScrollSnapTargetIds> mSnapTargetIds;
  ScrollTriggeredByScript mTriggeredByScript;

  nsRefreshDriver* RefreshDriver(ScrollContainerFrame* aCallee) {
    return aCallee->PresContext()->RefreshDriver();
  }
};

void ScrollContainerFrame::AsyncScroll::InitSmoothScroll(
    TimeStamp aTime, nsPoint aInitialPosition, nsPoint aDestination,
    ScrollOrigin aOrigin, const nsRect& aRange, const nsSize& aCurrentVelocity,
    UniquePtr<ScrollSnapTargetIds> aSnapTargetIds) {
  switch (aOrigin) {
    case ScrollOrigin::NotSpecified:
    case ScrollOrigin::Restore:
    case ScrollOrigin::Relative:
      // We don't have special prefs for "restore", just treat it as "other".
      // "restore" scrolls are (for now) always instant anyway so unless
      // something changes we should never have aOrigin ==
      // ScrollOrigin::Restore here.
      aOrigin = ScrollOrigin::Other;
      break;
    case ScrollOrigin::Apz:
      // Likewise we should never get APZ-triggered scrolls here, and if that
      // changes something is likely broken somewhere.
      MOZ_ASSERT(false);
      break;
    default:
      break;
  };

  // Read preferences only on first iteration or for a different event origin.
  if (!mAnimationPhysics || aOrigin != mOrigin) {
    mOrigin = aOrigin;
    if (StaticPrefs::general_smoothScroll_msdPhysics_enabled()) {
      mAnimationPhysics = MakeUnique<ScrollAnimationMSDPhysics>(
          apz::ScrollAnimationKind::Smooth, aInitialPosition,
          /*smallestVisibleIncrement=*/1.0);
    } else {
      ScrollAnimationBezierPhysicsSettings settings =
          layers::apz::ComputeBezierAnimationSettingsForOrigin(mOrigin);
      mAnimationPhysics =
          MakeUnique<ScrollAnimationBezierPhysics>(aInitialPosition, settings);
    }
  }

  mStartPosition = aInitialPosition;
  mRange = aRange;

  mAnimationPhysics->Update(aTime, aDestination, aCurrentVelocity);
  mSnapTargetIds = std::move(aSnapTargetIds);
}

/*
 * Callback function from AsyncSmoothMSDScroll, used in
 * ScrollContainerFrame::ScrollTo
 */
void ScrollContainerFrame::AsyncSmoothMSDScrollCallback(
    ScrollContainerFrame* aInstance, mozilla::TimeDuration aDeltaTime) {
  NS_ASSERTION(aInstance != nullptr, "aInstance must not be null");
  NS_ASSERTION(aInstance->mAsyncSmoothMSDScroll,
               "Did not expect AsyncSmoothMSDScrollCallback without an active "
               "MSD scroll.");

  nsRect range = aInstance->mAsyncSmoothMSDScroll->GetRange();
  aInstance->mAsyncSmoothMSDScroll->Simulate(aDeltaTime);

  if (!aInstance->mAsyncSmoothMSDScroll->IsFinished()) {
    nsPoint destination = aInstance->mAsyncSmoothMSDScroll->GetPosition();
    // Allow this scroll operation to land on any pixel boundary within the
    // allowed scroll range for this frame.
    // If the MSD is under-dampened or the destination is changed rapidly,
    // it is expected (and desired) that the scrolling may overshoot.
    nsRect intermediateRange = nsRect(destination, nsSize()).UnionEdges(range);
    aInstance->ScrollToImpl(destination, intermediateRange);
    // 'aInstance' might be destroyed here
    return;
  }

  aInstance->CompleteAsyncScroll(
      aInstance->mAsyncSmoothMSDScroll->GetStartPosition(), range,
      aInstance->mAsyncSmoothMSDScroll->TakeSnapTargetIds());
}

/*
 * Callback function from AsyncScroll, used in ScrollContainerFrame::ScrollTo
 */
void ScrollContainerFrame::AsyncScrollCallback(ScrollContainerFrame* aInstance,
                                               mozilla::TimeStamp aTime) {
  MOZ_ASSERT(aInstance != nullptr, "aInstance must not be null");
  MOZ_ASSERT(
      aInstance->mAsyncScroll,
      "Did not expect AsyncScrollCallback without an active async scroll.");

  if (!aInstance || !aInstance->mAsyncScroll) {
    return;  // XXX wallpaper bug 1107353 for now.
  }

  nsRect range = aInstance->mAsyncScroll->mRange;
  if (aInstance->mAsyncScroll->IsSmoothScroll()) {
    if (!aInstance->mAsyncScroll->IsFinished(aTime)) {
      nsPoint destination = aInstance->mAsyncScroll->PositionAt(aTime);
      // Allow this scroll operation to land on any pixel boundary between the
      // current position and the final allowed range.  (We don't want
      // intermediate steps to be more constrained than the final step!)
      nsRect intermediateRange =
          nsRect(aInstance->GetScrollPosition(), nsSize()).UnionEdges(range);
      aInstance->ScrollToImpl(destination, intermediateRange);
      // 'aInstance' might be destroyed here
      return;
    }
  }

  aInstance->CompleteAsyncScroll(aInstance->mAsyncScroll->GetStartPosition(),
                                 range,
                                 aInstance->mAsyncScroll->TakeSnapTargetIds());
}

bool ScrollContainerFrame::SliderFrameInClickAndHold() const {
  if (nsSliderFrame* sliderFrame = GetSliderFrame(mVScrollbarBox)) {
    if (sliderFrame->ClickAndHoldActive()) {
      return true;
    }
  }

  if (nsSliderFrame* sliderFrame = GetSliderFrame(mHScrollbarBox)) {
    return sliderFrame->ClickAndHoldActive();
  }

  return false;
}

void ScrollContainerFrame::SetTransformingByAPZ(bool aTransforming) {
  if (mTransformingByAPZ == aTransforming) {
    return;
  }
  mTransformingByAPZ = aTransforming;
  if (aTransforming) {
    ScrollbarActivityStarted();
  } else {
    ScrollbarActivityStopped();
    PostOrDeferScrollEndEvent();
  }
  if (!css::TextOverflow::HasClippedTextOverflow(this) ||
      css::TextOverflow::HasBlockEllipsis(mScrolledFrame)) {
    // If the block has some overflow marker stuff we should kick off a paint
    // because we have special behaviour for it when APZ scrolling is active.
    SchedulePaint();
  }
}

void ScrollContainerFrame::CompleteAsyncScroll(
    const nsPoint& aStartPosition, const nsRect& aRange,
    UniquePtr<ScrollSnapTargetIds> aSnapTargetIds, ScrollOrigin aOrigin) {
  SetLastSnapTargetIds(std::move(aSnapTargetIds));

  bool scrollPositionChanged = mDestination != aStartPosition;
  bool isNotHandledByApz =
      nsLayoutUtils::CanScrollOriginClobberApz(aOrigin) ||
      ScrollAnimationState().contains(AnimationState::MainThread);

  // Apply desired destination range since this is the last step of scrolling.
  RemoveObservers();
  AutoWeakFrame weakFrame(this);
  ScrollToImpl(mDestination, aRange, aOrigin);
  if (!weakFrame.IsAlive()) {
    return;
  }

  nsPoint finalPos = GetScrollPosition();

  SCROLLEND_LOG(
      "%s: start=%s destination=%s final=%s "
      "is-handled-by-apz=%s",
      __FUNCTION__, ToString(aStartPosition).c_str(),
      ToString(mDestination).c_str(), ToString(finalPos).c_str(),
      !isNotHandledByApz ? "true" : "false");

  // We are done scrolling, set our destination to wherever we actually ended
  // up scrolling to.
  mDestination = finalPos;
  // Post a `scrollend` event for scrolling not handled by APZ, including:
  //
  //  - programmatic instant scrolls
  //  - the end of a smooth scroll animation running on the main thread
  //
  // For scrolling handled by APZ, the `scrollend` event is posted in
  // SetTransformingByAPZ() when the APZC is transitioning from a transforming
  // to a non-transforming state (e.g. a transition from PANNING to NOTHING).
  // The scrollend event should not be fired for a scroll that does not
  // result in a scroll position change.
  if (isNotHandledByApz && scrollPositionChanged) {
    PostOrDeferScrollEndEvent();
  }
}

bool ScrollContainerFrame::HasBgAttachmentLocal() const {
  const nsStyleBackground* bg = StyleBackground();
  return bg->HasLocalBackground();
}

void ScrollContainerFrame::ScrollToInternal(
    nsPoint aScrollPosition, ScrollMode aMode, ScrollOrigin aOrigin,
    const nsRect* aRange, ScrollSnapFlags aSnapFlags,
    ScrollTriggeredByScript aTriggeredByScript) {
  if (aOrigin == ScrollOrigin::NotSpecified) {
    aOrigin = ScrollOrigin::Other;
  }
  ScrollToWithOrigin(
      aScrollPosition, aRange,
      ScrollOperationParams{aMode, aOrigin, aSnapFlags, aTriggeredByScript});
}

void ScrollContainerFrame::ScrollToCSSPixels(const CSSPoint& aScrollPosition,
                                             ScrollMode aMode) {
  CSSPoint currentCSSPixels = GetScrollPositionCSSPixels();
  // Transmogrify this scroll to a relative one if there's any on-going
  // animation in APZ triggered by __user__.
  // Bug 1740164: We will apply it for cases there's no animation in APZ.

  auto scrollAnimationState = ScrollAnimationState();
  bool isScrollAnimating =
      scrollAnimationState.contains(AnimationState::MainThread) ||
      scrollAnimationState.contains(AnimationState::APZPending) ||
      scrollAnimationState.contains(AnimationState::APZRequested);
  if (mCurrentAPZScrollAnimationType ==
          APZScrollAnimationType::TriggeredByUserInput &&
      !isScrollAnimating) {
    CSSPoint delta = aScrollPosition - currentCSSPixels;
    // This transmogrification need to be an intended end position scroll
    // operation.
    ScrollByCSSPixelsInternal(delta, aMode,
                              ScrollSnapFlags::IntendedEndPosition);
    return;
  }

  nscoord halfPixel = nsPresContext::CSSPixelsToAppUnits(0.5f);
  nsPoint pt = CSSPoint::ToAppUnits(aScrollPosition);
  nsRect range(pt.x - halfPixel, pt.y - halfPixel, 2 * halfPixel - 1,
               2 * halfPixel - 1);
  // XXX I don't think the following blocks are needed anymore, now that
  // ScrollToImpl simply tries to scroll an integer number of layer
  // pixels from the current position
  nsPoint current = GetScrollPosition();
  if (currentCSSPixels.x == aScrollPosition.x) {
    pt.x = current.x;
    range.x = pt.x;
    range.width = 0;
  }
  if (currentCSSPixels.y == aScrollPosition.y) {
    pt.y = current.y;
    range.y = pt.y;
    range.height = 0;
  }
  ScrollToWithOrigin(
      pt, &range,
      ScrollOperationParams{
          aMode, ScrollOrigin::Other,
          // This ScrollToCSSPixels is used for Element.scrollTo,
          // Element.scrollTop, Element.scrollLeft and for Window.scrollTo.
          ScrollSnapFlags::IntendedEndPosition, ScrollTriggeredByScript::Yes});
  // 'this' might be destroyed here
}

void ScrollContainerFrame::ScrollToCSSPixelsForApz(
    const CSSPoint& aScrollPosition, ScrollSnapTargetIds&& aLastSnapTargetIds) {
  nsPoint pt = CSSPoint::ToAppUnits(aScrollPosition);
  nscoord halfRange = nsPresContext::CSSPixelsToAppUnits(1000);
  nsRect range(pt.x - halfRange, pt.y - halfRange, 2 * halfRange - 1,
               2 * halfRange - 1);
  ScrollToWithOrigin(
      pt, &range,
      ScrollOperationParams{ScrollMode::Instant, ScrollOrigin::Apz,
                            std::move(aLastSnapTargetIds)});
  // 'this' might be destroyed here
}

CSSIntPoint ScrollContainerFrame::GetRoundedScrollPositionCSSPixels() {
  return CSSIntPoint::FromAppUnitsRounded(GetScrollPosition());
}

/*
 * this method wraps calls to ScrollToImpl(), either in one shot or
 * incrementally, based on the setting of the smoothness scroll pref
 */
void ScrollContainerFrame::ScrollToWithOrigin(nsPoint aScrollPosition,
                                              const nsRect* aRange,
                                              ScrollOperationParams&& aParams) {
  // None is never a valid scroll origin to be passed in.
  MOZ_ASSERT(aParams.mOrigin != ScrollOrigin::None);

  if (aParams.mOrigin != ScrollOrigin::Restore) {
    // If we're doing a non-restore scroll, we don't want to later
    // override it by restoring our saved scroll position.
    SCROLLRESTORE_LOG("%p: Clearing mRestorePos (cur=%s, dst=%s)\n", this,
                      ToString(GetScrollPosition()).c_str(),
                      ToString(aScrollPosition).c_str());
    mRestorePos.x = mRestorePos.y = -1;
  }

  // Stop suppressing displayport while the page is still loading.
  if (MOZ_UNLIKELY(PresShell()->IsDocumentLoading())) {
    PresShell()->SuppressDisplayport(false);
  }

  Maybe<SnapDestination> snapDestination;
  if (!aParams.IsScrollSnapDisabled()) {
    snapDestination = GetSnapPointForDestination(ScrollUnit::DEVICE_PIXELS,
                                                 aParams.mSnapFlags,
                                                 mDestination, aScrollPosition);
    if (snapDestination) {
      aScrollPosition = snapDestination->mPosition;
    }
  }

  nsRect scrollRange = GetLayoutScrollRange();
  mDestination = scrollRange.ClampPoint(aScrollPosition);
  if (mDestination != aScrollPosition &&
      aParams.mOrigin == ScrollOrigin::Restore &&
      GetPageLoadingState() != LoadingState::Loading) {
    // If we're doing a restore but the scroll position is clamped, promote
    // the origin from one that APZ can clobber to one that it can't clobber.
    aParams.mOrigin = ScrollOrigin::Other;
  }

  nsRect range = aRange && snapDestination.isNothing()
                     ? *aRange
                     : nsRect(aScrollPosition, nsSize(0, 0));

  UniquePtr<ScrollSnapTargetIds> snapTargetIds;
  if (snapDestination) {
    snapTargetIds =
        MakeUnique<ScrollSnapTargetIds>(std::move(snapDestination->mTargetIds));
  } else {
    snapTargetIds =
        MakeUnique<ScrollSnapTargetIds>(std::move(aParams.mTargetIds));
  }
  if (aParams.IsInstant()) {
    // Asynchronous scrolling is not allowed, so we'll kill any existing
    // async-scrolling process and do an instant scroll.
    AutoWeakFrame weakFrame(this);
    CompleteAsyncScroll(GetScrollPosition(), range, std::move(snapTargetIds),
                        aParams.mOrigin);
    if (weakFrame.IsAlive()) {
      mApzSmoothScrollDestination = Nothing();
    }
    return;
  }

  if (!aParams.IsSmoothMsd()) {
    // If we get a non-smooth-scroll, reset the cached APZ scroll destination,
    // so that we know to process the next smooth-scroll destined for APZ.
    mApzSmoothScrollDestination = Nothing();
  }

  nsPresContext* presContext = PresContext();
  TimeStamp now =
      presContext->RefreshDriver()->IsTestControllingRefreshesEnabled()
          ? presContext->RefreshDriver()->MostRecentRefresh()
          : TimeStamp::Now();

  nsSize currentVelocity(0, 0);

  const bool canHandoffToApz =
      nsLayoutUtils::AsyncPanZoomEnabled(this) && WantAsyncScroll() &&
      CanApzScrollInTheseDirections(
          DirectionsInDelta(mDestination - GetScrollPosition()));

  if (aParams.IsSmoothMsd()) {
    mIgnoreMomentumScroll = true;
    if (!mAsyncSmoothMSDScroll) {
      nsPoint sv = mVelocityQueue.GetVelocity();
      currentVelocity.width = sv.x;
      currentVelocity.height = sv.y;
      if (mAsyncScroll) {
        if (mAsyncScroll->IsSmoothScroll()) {
          currentVelocity = mAsyncScroll->VelocityAt(now);
        }
        mAsyncScroll = nullptr;
      }

      if (canHandoffToApz) {
        ApzSmoothScrollTo(mDestination, ScrollMode::SmoothMsd, aParams.mOrigin,
                          aParams.mTriggeredByScript, std::move(snapTargetIds),
                          ViewportType::Layout);
        return;
      }

      mAsyncSmoothMSDScroll = MakeRefPtr<AsyncSmoothMSDScroll>(
          GetScrollPosition(), mDestination, currentVelocity,
          GetLayoutScrollRange(), now, presContext, std::move(snapTargetIds),
          aParams.mTriggeredByScript);

      mAsyncSmoothMSDScroll->SetRefreshObserver(this);
    } else {
      // A previous smooth MSD scroll is still in progress, so we just need to
      // update its range and destination.
      mAsyncSmoothMSDScroll->SetRange(GetLayoutScrollRange());
      mAsyncSmoothMSDScroll->SetDestination(
          mDestination, std::move(snapTargetIds), aParams.mTriggeredByScript);
    }

    return;
  }

  if (mAsyncSmoothMSDScroll) {
    currentVelocity = mAsyncSmoothMSDScroll->GetVelocity();
    mAsyncSmoothMSDScroll = nullptr;
  }

  const bool isSmoothScroll =
      aParams.IsSmooth() && nsLayoutUtils::IsSmoothScrollingEnabled();
  if (!mAsyncScroll) {
    if (isSmoothScroll && canHandoffToApz) {
      ApzSmoothScrollTo(mDestination, ScrollMode::Smooth, aParams.mOrigin,
                        aParams.mTriggeredByScript, std::move(snapTargetIds),
                        ViewportType::Layout);
      return;
    }

    mAsyncScroll = MakeRefPtr<AsyncScroll>(aParams.mTriggeredByScript);
    mAsyncScroll->SetRefreshObserver(this);
  }

  if (isSmoothScroll) {
    mAsyncScroll->InitSmoothScroll(now, GetScrollPosition(), mDestination,
                                   aParams.mOrigin, range, currentVelocity,
                                   std::move(snapTargetIds));
  } else {
    mAsyncScroll->Init(GetScrollPosition(), range, std::move(snapTargetIds));
  }
}

void ScrollContainerFrame::MarkScrollbarsDirtyForReflow() const {
  auto* presShell = PresShell();
  if (mVScrollbarBox) {
    presShell->FrameNeedsReflow(mVScrollbarBox,
                                IntrinsicDirty::FrameAncestorsAndDescendants,
                                NS_FRAME_IS_DIRTY);
  }
  if (mHScrollbarBox) {
    presShell->FrameNeedsReflow(mHScrollbarBox,
                                IntrinsicDirty::FrameAncestorsAndDescendants,
                                NS_FRAME_IS_DIRTY);
  }
}

void ScrollContainerFrame::InvalidateScrollbars() const {
  if (mHScrollbarBox) {
    mHScrollbarBox->InvalidateFrameSubtree();
  }
  if (mVScrollbarBox) {
    mVScrollbarBox->InvalidateFrameSubtree();
  }
}

bool ScrollContainerFrame::IsAlwaysActive() const {
  if (nsDisplayItem::ForceActiveLayers()) {
    return true;
  }

  // Unless this is the root scrollframe for a non-chrome document
  // which is the direct child of a chrome document, we default to not
  // being "active".
  if (!(mIsRoot && PresContext()->IsRootContentDocumentCrossProcess())) {
    return false;
  }

  // If we have scrolled before, then we should stay active.
  if (mHasBeenScrolled) {
    return true;
  }

  // If we're overflow:hidden, then start as inactive until
  // we get scrolled manually.
  ScrollStyles styles = GetScrollStyles();
  return (styles.mHorizontal != StyleOverflow::Hidden &&
          styles.mVertical != StyleOverflow::Hidden);
}

void ScrollContainerFrame::RemoveDisplayPortCallback(nsITimer* aTimer,
                                                     void* aClosure) {
  ScrollContainerFrame* sf = static_cast<ScrollContainerFrame*>(aClosure);

  // This function only ever gets called from the expiry timer, so it must
  // be non-null here. Set it to null here so that we don't keep resetting
  // it unnecessarily in MarkRecentlyScrolled().
  MOZ_ASSERT(sf->mDisplayPortExpiryTimer);
  sf->mDisplayPortExpiryTimer = nullptr;

  if (!sf->AllowDisplayPortExpiration() || sf->mIsParentToActiveScrollFrames) {
    // If this is a scroll parent for some other scrollable frame, don't
    // expire the displayport because it would break scroll handoff. Once the
    // descendant scrollframes have their displayports expired, they will
    // trigger the displayport expiration on this scrollframe as well, and
    // mIsParentToActiveScrollFrames will presumably be false when that kicks
    // in.
    return;
  }

  // Remove the displayport from this scrollframe if it's been a while
  // since it's scrolled, except if it needs to be always active. Note that
  // there is one scrollframe that doesn't fall under this general rule, and
  // that is the one that nsLayoutUtils::MaybeCreateDisplayPort decides to put
  // a displayport on (i.e. the first scrollframe that WantAsyncScroll()s).
  // If that scrollframe is this one, we remove the displayport anyway, and
  // as part of the next paint MaybeCreateDisplayPort will put another
  // displayport back on it. Although the displayport will "flicker" off and
  // back on, the layer itself should never disappear, because this all
  // happens between actual painting. If the displayport is reset to a
  // different position that's ok; this scrollframe hasn't been scrolled
  // recently and so the reset should be correct.

  nsIContent* content = sf->GetContent();

  if (ScrollContainerFrame::ShouldActivateAllScrollFrames(nullptr, sf)) {
    // If we are activating all scroll frames then we only want to remove the
    // regular display port and downgrade to a minimal display port.
    MOZ_ASSERT(!content->GetProperty(nsGkAtoms::MinimalDisplayPort));
    content->SetProperty(nsGkAtoms::MinimalDisplayPort,
                         reinterpret_cast<void*>(true));
  } else {
    content->RemoveProperty(nsGkAtoms::MinimalDisplayPort);
    DisplayPortUtils::RemoveDisplayPort(content);
    // Be conservative and unflag this this scrollframe as being scrollable by
    // APZ. If it is still scrollable this will get flipped back soon enough.
    sf->mScrollableByAPZ = false;
  }

  DisplayPortUtils::ExpireDisplayPortOnAsyncScrollableAncestor(sf);
  sf->SchedulePaint();
}

void ScrollContainerFrame::MarkEverScrolled() {
  // Mark this frame as having been scrolled. If this is the root
  // scroll frame of a content document, then IsAlwaysActive()
  // will return true from now on and MarkNotRecentlyScrolled() won't
  // have any effect.
  mHasBeenScrolled = true;

  // PresContext should update the last scroll generation, for
  // NavigateEvents to determine if the root scroller has moved.
  if (mIsRoot) {
    PresContext()->UpdateLastScrollGeneration();
  }
}

void ScrollContainerFrame::MarkNotRecentlyScrolled() {
  if (!mHasBeenScrolledRecently) {
    return;
  }

  mHasBeenScrolledRecently = false;
  SchedulePaint();
}

void ScrollContainerFrame::MarkRecentlyScrolled() {
  mHasBeenScrolledRecently = true;
  if (IsAlwaysActive()) {
    return;
  }

  if (mActivityExpirationState.IsTracked()) {
    gScrollFrameActivityTracker->MarkUsed(this);
  } else {
    if (!gScrollFrameActivityTracker) {
      gScrollFrameActivityTracker =
          new ScrollFrameActivityTracker(GetMainThreadSerialEventTarget());
    }
    gScrollFrameActivityTracker->AddObject(this);
  }

  // If we just scrolled and there's a displayport expiry timer in place,
  // reset the timer.
  ResetDisplayPortExpiryTimer();
}

void ScrollContainerFrame::ResetDisplayPortExpiryTimer() {
  if (mDisplayPortExpiryTimer) {
    mDisplayPortExpiryTimer->InitWithNamedFuncCallback(
        RemoveDisplayPortCallback, this,
        StaticPrefs::apz_displayport_expiry_ms(), nsITimer::TYPE_ONE_SHOT,
        "ScrollContainerFrame::ResetDisplayPortExpiryTimer"_ns);
  }
}

bool ScrollContainerFrame::AllowDisplayPortExpiration() {
  if (IsAlwaysActive()) {
    return false;
  }

  if (mIsRoot && PresContext()->IsRoot()) {
    return false;
  }

  // If this was the first scrollable frame found, this displayport should
  // not expire.
  if (IsFirstScrollableFrameSequenceNumber().isSome()) {
    return false;
  }

  if (ShouldActivateAllScrollFrames(nullptr, this) &&
      GetContent()->GetProperty(nsGkAtoms::MinimalDisplayPort)) {
    return false;
  }
  return true;
}

void ScrollContainerFrame::TriggerDisplayPortExpiration() {
  if (!AllowDisplayPortExpiration()) {
    return;
  }

  if (!StaticPrefs::apz_displayport_expiry_ms()) {
    // a zero time disables the expiry
    return;
  }

  if (!mDisplayPortExpiryTimer) {
    mDisplayPortExpiryTimer = NS_NewTimer();
  }
  ResetDisplayPortExpiryTimer();
}

void ScrollContainerFrame::ScrollVisual() {
  MarkEverScrolled();
  // We need to call this after fixing up the view positions
  // to be consistent with the frame hierarchy.
  MarkRecentlyScrolled();
}

/**
 * Clamp desired scroll position aDesired and range [aDestLower, aDestUpper]
 * to [aBoundLower, aBoundUpper] and then select the appunit value from among
 * aBoundLower, aBoundUpper and those such that (aDesired - aCurrent) *
 * aRes/aAppUnitsPerPixel is an integer (or as close as we can get
 * modulo rounding to appunits) that is in [aDestLower, aDestUpper] and
 * closest to aDesired.  If no such value exists, return the nearest in
 * [aDestLower, aDestUpper].
 */
static nscoord ClampAndAlignWithPixels(nscoord aDesired, nscoord aBoundLower,
                                       nscoord aBoundUpper, nscoord aDestLower,
                                       nscoord aDestUpper,
                                       nscoord aAppUnitsPerPixel, double aRes,
                                       nscoord aCurrent) {
  // Intersect scroll range with allowed range, by clamping the ends
  // of aRange to be within bounds
  nscoord destLower = std::clamp(aDestLower, aBoundLower, aBoundUpper);
  nscoord destUpper = std::clamp(aDestUpper, aBoundLower, aBoundUpper);

  nscoord desired = std::clamp(aDesired, destLower, destUpper);
  if (StaticPrefs::layout_disable_pixel_alignment()) {
    return desired;
  }

  double currentLayerVal = (aRes * aCurrent) / aAppUnitsPerPixel;
  double desiredLayerVal = (aRes * desired) / aAppUnitsPerPixel;
  double delta = desiredLayerVal - currentLayerVal;
  double nearestLayerVal = NS_round(delta) + currentLayerVal;

  // Convert back from PaintedLayer space to appunits relative to the top-left
  // of the scrolled frame.
  nscoord aligned =
      aRes == 0.0
          ? 0.0
          : NSToCoordRoundWithClamp(nearestLayerVal * aAppUnitsPerPixel / aRes);

  // Use a bound if it is within the allowed range and closer to desired than
  // the nearest pixel-aligned value.
  if (aBoundUpper == destUpper &&
      static_cast<decltype(Abs(desired))>(aBoundUpper - desired) <
          Abs(desired - aligned)) {
    return aBoundUpper;
  }

  if (aBoundLower == destLower &&
      static_cast<decltype(Abs(desired))>(desired - aBoundLower) <
          Abs(aligned - desired)) {
    return aBoundLower;
  }

  // Accept the nearest pixel-aligned value if it is within the allowed range.
  if (aligned >= destLower && aligned <= destUpper) {
    return aligned;
  }

  // Check if opposite pixel boundary fits into allowed range.
  double oppositeLayerVal =
      nearestLayerVal + ((nearestLayerVal < desiredLayerVal) ? 1.0 : -1.0);
  nscoord opposite = aRes == 0.0
                         ? 0.0
                         : NSToCoordRoundWithClamp(oppositeLayerVal *
                                                   aAppUnitsPerPixel / aRes);
  if (opposite >= destLower && opposite <= destUpper) {
    return opposite;
  }

  // No alignment available.
  return desired;
}

/**
 * Clamp desired scroll position aPt to aBounds and then snap
 * it to the same layer pixel edges as aCurrent, keeping it within aRange
 * during snapping. aCurrent is the current scroll position.
 */
static nsPoint ClampAndAlignWithLayerPixels(const nsPoint& aPt,
                                            const nsRect& aBounds,
                                            const nsRect& aRange,
                                            const nsPoint& aCurrent,
                                            nscoord aAppUnitsPerPixel,
                                            const MatrixScales& aScale) {
  return nsPoint(
      ClampAndAlignWithPixels(aPt.x, aBounds.x, aBounds.XMost(), aRange.x,
                              aRange.XMost(), aAppUnitsPerPixel, aScale.xScale,
                              aCurrent.x),
      ClampAndAlignWithPixels(aPt.y, aBounds.y, aBounds.YMost(), aRange.y,
                              aRange.YMost(), aAppUnitsPerPixel, aScale.yScale,
                              aCurrent.y));
}

/* static */
void ScrollContainerFrame::ScrollActivityCallback(nsITimer* aTimer,
                                                  void* anInstance) {
  auto* self = static_cast<ScrollContainerFrame*>(anInstance);

  // Fire the synth mouse move.
  self->mScrollActivityTimer->Cancel();
  self->mScrollActivityTimer = nullptr;
  self->PresShell()->SynthesizeMouseMove(true);
}

void ScrollContainerFrame::ScheduleSyntheticMouseMove() {
  if (!mScrollActivityTimer) {
    mScrollActivityTimer = NS_NewTimer(GetMainThreadSerialEventTarget());
    if (!mScrollActivityTimer) {
      return;
    }
  }

  mScrollActivityTimer->InitWithNamedFuncCallback(
      ScrollActivityCallback, this, 100, nsITimer::TYPE_ONE_SHOT,
      "ScrollContainerFrame::ScheduleSyntheticMouseMove"_ns);
}

void ScrollContainerFrame::NotifyApproximateFrameVisibilityUpdate(
    bool aIgnoreDisplayPort) {
  mLastUpdateFramesPos = GetScrollPosition();
  if (aIgnoreDisplayPort) {
    mHadDisplayPortAtLastFrameUpdate = false;
    mDisplayPortAtLastFrameUpdate = nsRect();
  } else {
    mHadDisplayPortAtLastFrameUpdate = DisplayPortUtils::GetDisplayPort(
        GetContent(), &mDisplayPortAtLastFrameUpdate);
  }
}

bool ScrollContainerFrame::GetDisplayPortAtLastApproximateFrameVisibilityUpdate(
    nsRect* aDisplayPort) {
  if (mHadDisplayPortAtLastFrameUpdate) {
    *aDisplayPort = mDisplayPortAtLastFrameUpdate;
  }
  return mHadDisplayPortAtLastFrameUpdate;
}

/* aIncludeCSSTransform controls if we include CSS transforms that are in this
 * process (the BrowserChild EffectsInfo mTransformToAncestorScale will include
 * CSS transforms in ancestor processes in all cases). */
MatrixScales GetPaintedLayerScaleForFrame(nsIFrame* aFrame,
                                          bool aIncludeCSSTransform) {
  MOZ_ASSERT(aFrame, "need a frame");

  nsPresContext* presCtx = aFrame->PresContext()->GetRootPresContext();

  if (!presCtx) {
    presCtx = aFrame->PresContext();
    MOZ_ASSERT(presCtx);
  }

  ParentLayerToScreenScale2D transformToAncestorScale;
  if (aIncludeCSSTransform) {
    transformToAncestorScale =
        nsLayoutUtils::GetTransformToAncestorScaleCrossProcessForFrameMetrics(
            aFrame);
  } else {
    if (BrowserChild* browserChild =
            BrowserChild::GetFrom(aFrame->PresShell())) {
      transformToAncestorScale =
          browserChild->GetEffectsInfo().mTransformToAncestorScale;
    }
  }
  transformToAncestorScale =
      ParentLayerToParentLayerScale(
          presCtx->PresShell()->GetCumulativeResolution()) *
      transformToAncestorScale;

  return transformToAncestorScale.ToUnknownScale();
}

void ScrollContainerFrame::ScrollToImpl(
    nsPoint aPt, const nsRect& aRange, ScrollOrigin aOrigin,
    ScrollTriggeredByScript aTriggeredByScript) {
  // None is never a valid scroll origin to be passed in.
  MOZ_ASSERT(aOrigin != ScrollOrigin::None);

  // Capture whether the original origin is Clamp before it is potentially
  // promoted to Other below. When a reflow expands the container, a Clamp
  // scroll may be triggered but leave the scroll position unchanged,
  // hitting the early-exit below — where we must not cancel an in-progress
  // APZ smooth scroll even if the origin was promoted to Other.
  const bool isForClamping = aOrigin == ScrollOrigin::Clamp;

  // Figure out the effective origin for this scroll request.
  if (aOrigin == ScrollOrigin::NotSpecified) {
    // If no origin was specified, we still want to set it to something that's
    // non-unknown, so that we can use eUnknown to distinguish if the frame was
    // scrolled at all. Default it to some generic placeholder.
    aOrigin = ScrollOrigin::Other;
  }

  // If this scroll is |relative|, but we've already had a user scroll that
  // was not relative, promote this origin to |other|. This ensures that we
  // may only transmit a relative update to APZ if all scrolls since the last
  // transaction or repaint request have been relative.
  if ((aOrigin == ScrollOrigin::Relative || aOrigin == ScrollOrigin::Clamp) &&
      (mLastScrollOrigin != ScrollOrigin::None &&
       mLastScrollOrigin != ScrollOrigin::NotSpecified &&
       mLastScrollOrigin != ScrollOrigin::Relative &&
       mLastScrollOrigin != ScrollOrigin::Clamp &&
       mLastScrollOrigin != ScrollOrigin::Apz)) {
    aOrigin = ScrollOrigin::Other;
  }

  // If the origin is a downgrade, and downgrades are allowed, process the
  // downgrade even if we're going to early-exit because we're already at
  // the correct scroll position. This ensures that if there wasn't a main-
  // thread scroll update pending before a frame reconstruction (as indicated
  // by mAllowScrollOriginDowngrade=true), then after the frame reconstruction
  // the origin is downgraded to "restore" even if the layout scroll offset to
  // be restored is (0,0) (which will take the early-exit below). This is
  // important so that restoration of a *visual* scroll offset (which might be
  // to something other than (0,0)) isn't clobbered.
  bool isScrollOriginDowngrade =
      nsLayoutUtils::CanScrollOriginClobberApz(mLastScrollOrigin) &&
      !nsLayoutUtils::CanScrollOriginClobberApz(aOrigin);
  bool allowScrollOriginChange =
      mAllowScrollOriginDowngrade && isScrollOriginDowngrade;

  if (allowScrollOriginChange) {
    mLastScrollOrigin = aOrigin;
    mAllowScrollOriginDowngrade = false;
  }

  nsPresContext* presContext = PresContext();
  nscoord appUnitsPerDevPixel = presContext->AppUnitsPerDevPixel();
  // 'scale' is our estimate of the scale factor that will be applied
  // when rendering the scrolled content to its own PaintedLayer.
  MatrixScales scale = GetPaintedLayerScaleForFrame(
      mScrolledFrame, /* aIncludeCSSTransform = */ true);
  nsPoint curPos = GetScrollPosition();

  // Try to align aPt with curPos so they have an integer number of layer
  // pixels between them. This gives us the best chance of scrolling without
  // having to invalidate due to changes in subpixel rendering.
  // Note that when we actually draw into a PaintedLayer, the coordinates
  // that get mapped onto the layer buffer pixels are from the display list,
  // which are relative to the display root frame's top-left increasing down,
  // whereas here our coordinates are scroll positions which increase upward
  // and are relative to the scrollport top-left. This difference doesn't
  // actually matter since all we are about is that there be an integer number
  // of layer pixels between pt and curPos.
  nsPoint pt = ClampAndAlignWithLayerPixels(aPt, GetLayoutScrollRange(), aRange,
                                            curPos, appUnitsPerDevPixel, scale);
  if (pt == curPos) {
    // Even if we are bailing out due to no-op main-thread scroll position
    // change, we might need to cancel an APZ smooth scroll that we already
    // kicked off. It might be reasonable to eventually remove the
    // mApzSmoothScrollDestination clause from this if statement, as that
    // may simplify this a bit and should be fine from the APZ side.
    if (mApzSmoothScrollDestination && !isForClamping) {
      if (aOrigin == ScrollOrigin::Relative) {
        AppendScrollUpdate(
            ScrollPositionUpdate::NewRelativeScroll(mApzScrollPos, pt));
        mApzScrollPos = pt;
      } else if (aOrigin != ScrollOrigin::Apz) {
        ScrollOrigin origin =
            (mAllowScrollOriginDowngrade || !isScrollOriginDowngrade)
                ? aOrigin
                : mLastScrollOrigin;
        AppendScrollUpdate(ScrollPositionUpdate::NewScroll(origin, pt));
      }
    }
    return;
  }

  // If we are scrolling the RCD-RSF, and a visual scroll update is pending,
  // cancel it; otherwise, it will clobber this scroll.
  if (IsRootScrollFrameOfDocument() &&
      presContext->IsRootContentDocumentCrossProcess()) {
    auto* ps = presContext->GetPresShell();
    if (const auto& visualScrollUpdate = ps->GetPendingVisualScrollUpdate()) {
      if (visualScrollUpdate->mVisualScrollOffset != aPt) {
        // Only clobber if the scroll was originated by the main thread.
        // Respect the priority of origins (an "eRestore" layout scroll should
        // not clobber an "eMainThread" visual scroll.)
        bool shouldClobber =
            aOrigin == ScrollOrigin::Other ||
            (aOrigin == ScrollOrigin::Restore &&
             visualScrollUpdate->mUpdateType == FrameMetrics::eRestore);
        if (shouldClobber) {
          ps->AcknowledgePendingVisualScrollUpdate();
          ps->ClearPendingVisualScrollUpdate();
        }
      }
    }
  }

  bool needFrameVisibilityUpdate = mLastUpdateFramesPos == nsPoint(-1, -1);

  nsPoint dist(std::abs(pt.x - mLastUpdateFramesPos.x),
               std::abs(pt.y - mLastUpdateFramesPos.y));
  nsSize visualViewportSize = GetVisualViewportSize();
  nscoord horzAllowance = std::max(
      visualViewportSize.width /
          std::max(
              StaticPrefs::
                  layout_framevisibility_amountscrollbeforeupdatehorizontal(),
              1),
      AppUnitsPerCSSPixel());
  nscoord vertAllowance = std::max(
      visualViewportSize.height /
          std::max(
              StaticPrefs::
                  layout_framevisibility_amountscrollbeforeupdatevertical(),
              1),
      AppUnitsPerCSSPixel());
  if (dist.x >= horzAllowance || dist.y >= vertAllowance) {
    needFrameVisibilityUpdate = true;
  }

  nsRect oldDisplayPort;
  nsIContent* content = GetContent();
  DisplayPortUtils::GetDisplayPort(content, &oldDisplayPort);
  oldDisplayPort.MoveBy(-mScrolledFrame->GetPosition());

  // Update frame position for scrolling
  mScrolledFrame->SetPosition(mScrollPort.TopLeft() - pt);

  // If |mLastScrollOrigin| is already set to something that can clobber APZ's
  // scroll offset, then we don't want to change it to something that can't.
  // If we allowed this, then we could end up in a state where APZ ignores
  // legitimate scroll offset updates because the origin has been masked by
  // a later change within the same refresh driver tick.
  allowScrollOriginChange =
      (mAllowScrollOriginDowngrade || !isScrollOriginDowngrade);

  if (allowScrollOriginChange) {
    mLastScrollOrigin = aOrigin;
    mAllowScrollOriginDowngrade = false;
  }

  if (aOrigin == ScrollOrigin::Relative) {
    MOZ_ASSERT(!isScrollOriginDowngrade);
    MOZ_ASSERT(mLastScrollOrigin == ScrollOrigin::Relative);
    AppendScrollUpdate(
        ScrollPositionUpdate::NewRelativeScroll(mApzScrollPos, pt));
    mApzScrollPos = pt;
  } else if (aOrigin == ScrollOrigin::Clamp) {
    AppendScrollUpdate(
        ScrollPositionUpdate::NewRelativeScroll(mApzScrollPos, pt));
    mApzScrollPos = pt;
  } else if (aOrigin != ScrollOrigin::Apz) {
    AppendScrollUpdate(ScrollPositionUpdate::NewScroll(mLastScrollOrigin, pt));
  }

  if (mLastScrollOrigin == ScrollOrigin::Apz) {
    mApzScrollPos = GetScrollPosition();
  }

  ScrollVisual();
  mAnchor.UserScrolled();

  // Only report user-triggered scrolling interactions
  bool jsOnStack = nsContentUtils::GetCurrentJSContext() != nullptr;
  bool scrollingToAnchor = ScrollingInteractionContext::IsScrollingToAnchor();
  if (!jsOnStack && !scrollingToAnchor) {
    nsPoint distanceScrolled(std::abs(pt.x - curPos.x),
                             std::abs(pt.y - curPos.y));
    ScrollingMetrics::OnScrollingInteraction(
        CSSPoint::FromAppUnits(distanceScrolled).Length());
  }

  bool schedulePaint = true;
  if (nsLayoutUtils::AsyncPanZoomEnabled(this) &&
      !nsLayoutUtils::ShouldDisableApzForElement(content) &&
      !content->GetProperty(nsGkAtoms::MinimalDisplayPort) &&
      StaticPrefs::apz_paint_skipping_enabled()) {
    // If APZ is enabled with paint-skipping, there are certain conditions in
    // which we can skip paints:
    // 1) If APZ triggered this scroll, and the tile-aligned displayport is
    //    unchanged.
    // 2) If non-APZ triggered this scroll, but we can handle it by just asking
    //    APZ to update the scroll position. Again we make this conditional on
    //    the tile-aligned displayport being unchanged.
    // We do the displayport check first since it's common to all scenarios,
    // and then if the displayport is unchanged, we check if APZ triggered,
    // or can handle, this scroll. If so, we set schedulePaint to false and
    // skip the paint.
    // Because of bug 1264297, we also don't do paint-skipping for elements with
    // perspective, because the displayport may not have captured everything
    // that needs to be painted. So even if the final tile-aligned displayport
    // is the same, we force a repaint for these elements. Bug 1254260 tracks
    // fixing this properly.
    nsRect displayPort;
    bool usingDisplayPort =
        DisplayPortUtils::GetDisplayPort(content, &displayPort);
    displayPort.MoveBy(-mScrolledFrame->GetPosition());

    PAINT_SKIP_LOG(
        "New scrollpos %s usingDP %d dpEqual %d scrollableByApz "
        "%d perspective %d bglocal %d filter %d\n",
        ToString(CSSPoint::FromAppUnits(GetScrollPosition())).c_str(),
        usingDisplayPort, displayPort.IsEqualEdges(oldDisplayPort),
        mScrollableByAPZ, HasPerspective(), HasBgAttachmentLocal(),
        mHasOutOfFlowContentInsideFilter);
    if (usingDisplayPort && displayPort.IsEqualEdges(oldDisplayPort) &&
        !HasPerspective() && !HasBgAttachmentLocal() &&
        !mHasOutOfFlowContentInsideFilter) {
      bool haveScrollLinkedEffects =
          content->GetComposedDoc()->HasScrollLinkedEffect();
      bool apzDisabled = haveScrollLinkedEffects &&
                         StaticPrefs::apz_disable_for_scroll_linked_effects();
      if (!apzDisabled) {
        if (LastScrollOrigin() == ScrollOrigin::Apz) {
          schedulePaint = false;
          PAINT_SKIP_LOG("Skipping due to APZ scroll\n");
        } else if (mScrollableByAPZ) {
          nsIWidget* widget = GetNearestWidget();
          WindowRenderer* renderer =
              widget ? widget->GetWindowRenderer() : nullptr;
          if (renderer) {
            mozilla::layers::ScrollableLayerGuid::ViewID id;
            bool success = nsLayoutUtils::FindIDFor(content, &id);
            MOZ_ASSERT(success);  // we have a displayport, we better have an ID

            // Schedule an empty transaction to carry over the scroll offset
            // update, instead of a full transaction. This empty transaction
            // might still get squashed into a full transaction if something
            // happens to trigger one.
            MOZ_ASSERT(!mScrollUpdates.IsEmpty());
            success = renderer->AddPendingScrollUpdateForNextTransaction(
                id, mScrollUpdates.LastElement());
            if (success) {
              schedulePaint = false;
              SchedulePaint(nsIFrame::PAINT_COMPOSITE_ONLY);
              PAINT_SKIP_LOG(
                  "Skipping due to APZ-forwarded main-thread scroll\n");
            } else {
              PAINT_SKIP_LOG(
                  "Failed to set pending scroll update on layer manager\n");
            }
          }
        }
      }
    }
  }

  // If the new scroll offset is going to clobber APZ's scroll offset, for
  // the RCD-RSF this will have the effect of updating the visual viewport
  // offset in a way that keeps the relative offset between the layout and
  // visual viewports constant. This will cause APZ to send us a new visual
  // viewport offset, but instead of waiting for  that, just set the value
  // we expect APZ will set ourselves, to minimize the chances of
  // inconsistencies from querying a stale value.
  if (mIsRoot && nsLayoutUtils::CanScrollOriginClobberApz(aOrigin)) {
    AutoWeakFrame weakFrame(this);
    AutoScrollbarRepaintSuppression repaintSuppression(this, weakFrame,
                                                       !schedulePaint);

    nsPoint visualViewportOffset = curPos;
    if (presContext->PresShell()->IsVisualViewportOffsetSet()) {
      visualViewportOffset =
          presContext->PresShell()->GetVisualViewportOffset();
    }
    nsPoint relativeOffset = visualViewportOffset - curPos;

    presContext->PresShell()->SetVisualViewportOffset(pt + relativeOffset,
                                                      curPos);
    if (!weakFrame.IsAlive()) {
      return;
    }
  }

  if (schedulePaint) {
    SchedulePaint();

    if (needFrameVisibilityUpdate) {
      presContext->PresShell()->ScheduleApproximateFrameVisibilityUpdateNow();
    }
  }

  if (ChildrenHavePerspective() && RecomputePerspectiveChildrenOverflow(this)) {
    // The overflow areas of descendants may depend on the scroll position,
    // so ensure they get updated.
    // First we recompute the overflow areas of the transformed children
    // that use the perspective. FinishAndStoreOverflow only calls this
    // if the size changes, so we need to do it manually.
    // Update the overflow for the outer so that we recompute scrollbars.
    UpdateOverflow();
  }

  ScheduleSyntheticMouseMove();

  nsAutoScriptBlocker scriptBlocker;
  PresShell::AutoAssertNoFlush noFlush(*PresShell());

  {  // scope the AutoScrollbarRepaintSuppression
    AutoWeakFrame weakFrame(this);
    AutoScrollbarRepaintSuppression repaintSuppression(this, weakFrame,
                                                       !schedulePaint);
    UpdateScrollbarPosition();
    if (!weakFrame.IsAlive()) {
      return;
    }
  }

  presContext->RecordInteractionTime(
      nsPresContext::InteractionType::ScrollInteraction, TimeStamp::Now());

  PostScrollEvent();
  // If this is a viewport scroll, this could affect the relative offset
  // between layout and visual viewport, so we might have to fire a visual
  // viewport scroll event as well.
  if (mIsRoot) {
    if (auto* window = nsGlobalWindowInner::Cast(
            PresContext()->Document()->GetInnerWindow())) {
      window->VisualViewport()->PostScrollEvent(
          presContext->PresShell()->GetVisualViewportOffset(), curPos);
    }
  }

  // Schedule the scroll-timelines linked to its scrollable frame.
  // if `pt == curPos`, we early return, so the position must be changed at
  // this moment. Therefore, we can schedule scroll animations directly.
  ScheduleScrollAnimations();

  if (mStickyContainer) {
    mStickyContainer->UpdatePositions(pt, /* aSubtreeRoot = */ nullptr);
  }

  if (nsCOMPtr<nsIDocShell> docShell = presContext->GetDocShell()) {
    docShell->NotifyScrollObservers();
  }
}

// Finds the max z-index of the items in aList that meet the following
// conditions
//   1) have z-index auto or z-index >= 0, and
//   2) aFrame is a proper ancestor of the item's frame.
// Returns Nothing() if there is no such item.
static Maybe<int32_t> MaxZIndexInListOfItemsContainedInFrame(
    nsDisplayList* aList, nsIFrame* aFrame) {
  Maybe<int32_t> maxZIndex = Nothing();
  for (nsDisplayItem* item : *aList) {
    int32_t zIndex = item->ZIndex();
    if (zIndex < 0 ||
        !nsLayoutUtils::IsProperAncestorFrame(aFrame, item->Frame())) {
      continue;
    }
    if (!maxZIndex) {
      maxZIndex = Some(zIndex);
    } else {
      maxZIndex = Some(std::max(maxZIndex.value(), zIndex));
    }
  }
  return maxZIndex;
}

template <class T>
static void AppendInternalItemToTop(const nsDisplayListSet& aLists, T* aItem,
                                    const Maybe<int32_t>& aZIndex) {
  if (aZIndex) {
    aItem->SetOverrideZIndex(aZIndex.value());
    aLists.PositionedDescendants()->AppendToTop(aItem);
  } else {
    aLists.Content()->AppendToTop(aItem);
  }
}

static const uint32_t APPEND_OWN_LAYER = 0x1;
static const uint32_t APPEND_POSITIONED = 0x2;
static const uint32_t APPEND_SCROLLBAR_CONTAINER = 0x4;
static const uint32_t APPEND_OVERLAY = 0x8;
static const uint32_t APPEND_TOP = 0x10;

static void AppendToTop(nsDisplayListBuilder* aBuilder,
                        const nsDisplayListSet& aLists, nsDisplayList* aSource,
                        nsIFrame* aSourceFrame, nsIFrame* aScrollFrame,
                        uint32_t aFlags) {
  if (aSource->IsEmpty()) {
    return;
  }

  nsDisplayWrapList* newItem;
  const ActiveScrolledRoot* asr = aBuilder->CurrentActiveScrolledRoot();
  if (aFlags & APPEND_OWN_LAYER) {
    ScrollbarData scrollbarData;
    if (aFlags & APPEND_SCROLLBAR_CONTAINER) {
      scrollbarData = ScrollbarData::CreateForScrollbarContainer(
          aBuilder->GetCurrentScrollbarDirection(),
          aBuilder->GetCurrentScrollbarTarget());
      // Direction should be set
      MOZ_ASSERT(scrollbarData.mDirection.isSome());
    }

    newItem = MakeDisplayItemWithIndex<nsDisplayOwnLayer>(
        aBuilder, aSourceFrame,
        /* aIndex = */ nsDisplayOwnLayer::OwnLayerForScrollbar, aSource, asr,
        nsDisplayItem::ContainerASRType::Constant, nsDisplayOwnLayerFlags::None,
        scrollbarData, true, false);
  } else {
    // Build the wrap list with an index of 1, since the scrollbar frame itself
    // might have already built an nsDisplayWrapList.
    newItem = MakeDisplayItemWithIndex<nsDisplayWrapper>(aBuilder, aSourceFrame,
                                                         1, aSource, false);
  }
  if (!newItem) {
    return;
  }

  if (aFlags & APPEND_POSITIONED) {
    // We want overlay scrollbars to always be on top of the scrolled content,
    // but we don't want them to unnecessarily cover overlapping elements from
    // outside our scroll frame.
    Maybe<int32_t> zIndex = Nothing();
    if (aFlags & APPEND_TOP) {
      zIndex = Some(INT32_MAX);
    } else if (aFlags & APPEND_OVERLAY) {
      zIndex = MaxZIndexInListOfItemsContainedInFrame(
          aLists.PositionedDescendants(), aScrollFrame);
    } else if (aSourceFrame->StylePosition()->mZIndex.IsInteger()) {
      zIndex = Some(aSourceFrame->StylePosition()->mZIndex.integer._0);
    }
    AppendInternalItemToTop(aLists, newItem, zIndex);
  } else {
    aLists.BorderBackground()->AppendToTop(newItem);
  }
}

struct HoveredStateComparator {
  static bool Hovered(const nsIFrame* aFrame) {
    return aFrame->GetContent()->IsElement() &&
           aFrame->GetContent()->AsElement()->State().HasState(
               ElementState::HOVER);
  }

  bool Equals(nsIFrame* A, nsIFrame* B) const {
    return Hovered(A) == Hovered(B);
  }

  bool LessThan(nsIFrame* A, nsIFrame* B) const {
    return !Hovered(A) && Hovered(B);
  }
};

void ScrollContainerFrame::AppendScrollPartsTo(nsDisplayListBuilder* aBuilder,
                                               const nsDisplayListSet& aLists,
                                               bool aCreateLayer,
                                               bool aPositioned) {
  MOZ_ASSERT(!HidesContent());
  const bool overlayScrollbars = UseOverlayScrollbars();

  AutoTArray<nsIFrame*, 3> scrollParts;
  for (nsIFrame* kid : PrincipalChildList()) {
    if (kid == mScrolledFrame ||
        (overlayScrollbars || kid->IsAbsPosContainingBlock()) != aPositioned) {
      continue;
    }

    scrollParts.AppendElement(kid);
  }
  if (scrollParts.IsEmpty()) {
    return;
  }

  // We can't check will-change budget during display list building phase.
  // This means that we will build scroll bar layers for out of budget
  // will-change: scroll position.
  const mozilla::layers::ScrollableLayerGuid::ViewID scrollTargetId =
      aBuilder->BuildCompositorHitTestInfo() && IsScrollingActive()
          ? nsLayoutUtils::FindOrCreateIDFor(mScrolledFrame->GetContent())
          : mozilla::layers::ScrollableLayerGuid::NULL_SCROLL_ID;

  scrollParts.Sort(HoveredStateComparator());

  DisplayListClipState::AutoSaveRestore clipState(aBuilder);
  // Don't let scrollparts extent outside our frame's border-box, if these are
  // viewport scrollbars. They would create layerization problems. This wouldn't
  // normally be an issue but themes can add overflow areas to scrollbar parts.
  if (mIsRoot) {
    nsRect scrollPartsClip(aBuilder->ToReferenceFrame(this),
                           TrueOuterSize(aBuilder));
    clipState.ClipContentDescendants(scrollPartsClip);
  }

  for (nsIFrame* scrollPart : scrollParts) {
    Maybe<ScrollDirection> scrollDirection;
    uint32_t appendToTopFlags = 0;
    if (scrollPart == mVScrollbarBox) {
      scrollDirection.emplace(ScrollDirection::eVertical);
      appendToTopFlags |= APPEND_SCROLLBAR_CONTAINER;
    }
    if (scrollPart == mHScrollbarBox) {
      MOZ_ASSERT(!scrollDirection.isSome());
      scrollDirection.emplace(ScrollDirection::eHorizontal);
      appendToTopFlags |= APPEND_SCROLLBAR_CONTAINER;
    }

    // The display port doesn't necessarily include the scrollbars, so just
    // include all of the scrollbars if we are in a RCD-RSF. We only do
    // this for the root scrollframe of the root content document, which is
    // zoomable, and where the scrollbar sizes are bounded by the widget.
    const nsRect visible =
        mIsRoot && PresContext()->IsRootContentDocumentCrossProcess()
            ? scrollPart->InkOverflowRectRelativeToParent()
            : aBuilder->GetVisibleRect();
    if (visible.IsEmpty()) {
      continue;
    }
    const nsRect dirty =
        mIsRoot && PresContext()->IsRootContentDocumentCrossProcess()
            ? scrollPart->InkOverflowRectRelativeToParent()
            : aBuilder->GetDirtyRect();

    // Always create layers for overlay scrollbars so that we don't create a
    // giant layer covering the whole scrollport if both scrollbars are visible.
    const bool isOverlayScrollbar =
        scrollDirection.isSome() && overlayScrollbars;
    const bool createLayer =
        aCreateLayer || isOverlayScrollbar ||
        StaticPrefs::layout_scrollbars_always_layerize_track();

    nsDisplayListCollection partList(aBuilder);
    {
      nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
          aBuilder, this, visible, dirty);

      nsDisplayListBuilder::AutoCurrentScrollbarInfoSetter infoSetter(
          aBuilder, scrollTargetId, scrollDirection, createLayer);
      BuildDisplayListForChild(
          aBuilder, scrollPart, partList,
          nsIFrame::DisplayChildFlag::ForceStackingContext);
    }

    if (partList.IsEmpty()) {
      continue;
    }

    nsDisplayList list(aBuilder);
    partList.SerializeWithCorrectZOrder(&list, scrollPart->GetContent());

    if (createLayer) {
      appendToTopFlags |= APPEND_OWN_LAYER;
    }
    if (aPositioned) {
      appendToTopFlags |= APPEND_POSITIONED;
    }

    if (isOverlayScrollbar || scrollPart == mResizerBox) {
      if (isOverlayScrollbar && mIsRoot) {
        appendToTopFlags |= APPEND_TOP;
      } else {
        appendToTopFlags |= APPEND_OVERLAY;
        aBuilder->SetDisablePartialUpdates(true);
      }
    }

    {
      nsDisplayListBuilder::AutoBuildingDisplayList buildingForChild(
          aBuilder, scrollPart, visible + GetOffsetTo(scrollPart),
          dirty + GetOffsetTo(scrollPart));
      if (scrollPart->IsTransformed()) {
        nsPoint toOuterReferenceFrame;
        const nsIFrame* outerReferenceFrame = aBuilder->FindReferenceFrameFor(
            scrollPart->GetParent(), &toOuterReferenceFrame);
        toOuterReferenceFrame += scrollPart->GetPosition();

        buildingForChild.SetReferenceFrameAndCurrentOffset(
            outerReferenceFrame, toOuterReferenceFrame);
      }
      nsDisplayListBuilder::AutoCurrentScrollbarInfoSetter infoSetter(
          aBuilder, scrollTargetId, scrollDirection, createLayer);

      ::AppendToTop(aBuilder, aLists, &list, scrollPart, this,
                    appendToTopFlags);
    }
  }
}

nsRect ScrollContainerFrame::ExpandRectToNearlyVisible(
    const nsRect& aRect) const {
  // We don't want to expand a rect in a direction that we can't scroll, so we
  // check the scroll range.
  nsRect scrollRange = GetLayoutScrollRange();
  nsPoint scrollPos = GetScrollPosition();
  nsMargin expand(0, 0, 0, 0);

  nscoord vertShift =
      StaticPrefs::layout_framevisibility_numscrollportheights() * aRect.height;
  if (scrollRange.y < scrollPos.y) {
    expand.top = vertShift;
  }
  if (scrollPos.y < scrollRange.YMost()) {
    expand.bottom = vertShift;
  }

  nscoord horzShift =
      StaticPrefs::layout_framevisibility_numscrollportwidths() * aRect.width;
  if (scrollRange.x < scrollPos.x) {
    expand.left = horzShift;
  }
  if (scrollPos.x < scrollRange.XMost()) {
    expand.right = horzShift;
  }

  nsRect rect = aRect;
  rect.Inflate(expand);
  return rect;
}

// This is similar to a "save-restore" RAII class for
// DisplayListBuilder::ContainsBlendMode(), with a slight enhancement.
// If this class is put on the stack and then unwound, the DL builder's
// ContainsBlendMode flag will be effectively the same as if this class wasn't
// put on the stack. However, if the CaptureContainsBlendMode method is called,
// there will be a difference - the blend mode in the descendant display lists
// will be "captured" and extracted.
// The main goal here is to allow conditionally capturing the flag that
// indicates whether or not a blend mode was encountered in the descendant part
// of the display list.
class MOZ_RAII AutoContainsBlendModeCapturer {
  nsDisplayListBuilder& mBuilder;
  bool mSavedContainsBlendMode;

 public:
  explicit AutoContainsBlendModeCapturer(nsDisplayListBuilder& aBuilder)
      : mBuilder(aBuilder),
        mSavedContainsBlendMode(aBuilder.ContainsBlendMode()) {
    mBuilder.ClearStackingContextBits(
        StackingContextBits::ContainsMixBlendMode);
  }

  bool CaptureContainsBlendMode() {
    // "Capture" the flag by extracting and clearing the ContainsBlendMode flag
    // on the builder.
    const bool capturedBlendMode = mBuilder.ContainsBlendMode();
    mBuilder.ClearStackingContextBits(
        StackingContextBits::ContainsMixBlendMode);
    return capturedBlendMode;
  }

  ~AutoContainsBlendModeCapturer() {
    // If CaptureContainsBlendMode() was called, the descendant blend mode was
    // "captured" and so uncapturedContainsBlendMode will be false. If
    // CaptureContainsBlendMode() wasn't called, then no capture occurred, and
    // uncapturedContainsBlendMode may be true if there was a descendant blend
    // mode. In that case, we set the flag on the DL builder so that we restore
    // state to what it would have been without this RAII class on the stack.
    bool uncapturedContainsBlendMode = mBuilder.ContainsBlendMode();
    if (mSavedContainsBlendMode || uncapturedContainsBlendMode) {
      mBuilder.SetStackingContextBits(
          StackingContextBits::ContainsMixBlendMode);
    } else {
      mBuilder.ClearStackingContextBits(
          StackingContextBits::ContainsMixBlendMode);
    }
  }
};

void ScrollContainerFrame::MaybeCreateTopLayerAndWrapRootItems(
    nsDisplayListBuilder* aBuilder, nsDisplayListCollection& aSet,
    bool aCreateAsyncZoom, bool aCapturedByViewTransition,
    AutoContainsBlendModeCapturer* aAsyncZoomBlendCapture,
    const nsRect& aAsyncZoomClipRect, const nsRectCornerRadii* aRadii) {
  if (!mIsRoot) {
    return;
  }
  nsIFrame* rootStyleFrame = GetFrameForStyle();

  nsDisplayList rootResultList(aBuilder);
  bool serializedList = false;
  auto SerializeList = [&] {
    if (!serializedList) {
      serializedList = true;
      aSet.SerializeWithCorrectZOrder(&rootResultList, GetContent());
    }
  };

  // Create any required items for the 'top layer' and check if they'll be
  // opaque over the entire area of the viewport. If they are, then we can
  // skip building display items for the rest of the page.
  ViewportFrame* viewportParent = do_QueryFrame(GetParent());
  {
    nsDisplayListBuilder::AutoEnterViewTransitionCapture
        inViewTransitionCaptureSetter(aBuilder, aCapturedByViewTransition);
    nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(
        aBuilder);
    DisplayListClipState::AutoSaveRestore clipState(aBuilder);
    if (aBuilder->IsInViewTransitionCapture()) {
      asrSetter.SetCurrentActiveScrolledRoot(nullptr);
      clipState.Clear();
    }
    if (viewportParent) {
      bool topLayerIsOpaque = false;
      if (nsDisplayWrapList* topLayerWrapList =
              viewportParent->BuildDisplayListForContentTopLayer(
                  aBuilder, &topLayerIsOpaque)) {
        // If the top layer content is opaque, and we're the root content
        // document in the process, we can drop the display items behind it. We
        // only support doing this for the root content document in the process,
        // since the top layer content might have fixed position items that have
        // a scrolltarget referencing the APZ data for the document. APZ builds
        // this data implicitly for the root content document in the process,
        // but subdocuments etc need their display items to generate it, so we
        // can't cull those.
        if (topLayerIsOpaque && !serializedList &&
            PresContext()->IsRootContentDocumentInProcess()) {
          aSet.DeleteAll(aBuilder);
        }
        if (serializedList) {
          rootResultList.AppendToTop(topLayerWrapList);
        } else {
          aSet.PositionedDescendants()->AppendToTop(topLayerWrapList);
        }
      }
    }

    if (aCapturedByViewTransition) {
      SerializeList();
      rootResultList.AppendNewToTop<nsDisplayViewTransitionCapture>(
          aBuilder, this, &rootResultList, nullptr, /* aIsRoot = */ true);
    }
  }

  if (rootStyleFrame) {
    bool usingBackdropFilter =
        rootStyleFrame->StyleEffects()->HasBackdropFilters() &&
        rootStyleFrame->IsVisibleForPainting();

    if (rootStyleFrame->StyleEffects()->HasFilters() &&
        !aBuilder->IsForGenerateGlyphMask()) {
      SerializeList();
      rootResultList.AppendNewToTop<nsDisplayFilters>(
          aBuilder, this, &rootResultList, rootStyleFrame, usingBackdropFilter);
    }

    if (usingBackdropFilter) {
      SerializeList();
      nsRect backdropRect =
          GetRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);
      rootResultList.AppendNewToTop<nsDisplayBackdropFilters>(
          aBuilder, this, &rootResultList, backdropRect, rootStyleFrame);
    }
  }

  if (viewportParent) {
    if (nsDisplayWrapList* topLayerWrapList =
            viewportParent->BuildDisplayListForViewTransitionsAndNACTopLayer(
                aBuilder)) {
      if (serializedList) {
        rootResultList.AppendToTop(topLayerWrapList);
      } else {
        aSet.PositionedDescendants()->AppendToTop(topLayerWrapList);
      }
    }
  }

  if (aCreateAsyncZoom) {
    MOZ_ASSERT(mIsRoot);

    // Wrap all our scrolled contents in an nsDisplayAsyncZoom. This will be
    // the layer that gets scaled for APZ zooming. It does not have the
    // scrolled ASR, but it does have the composition bounds clip applied to
    // it. The children have the layout viewport clip applied to them (above).
    // Effectively we are double clipping to the viewport, at potentially
    // different async scales.
    SerializeList();

    if (aAsyncZoomBlendCapture->CaptureContainsBlendMode()) {
      // The async zoom contents contain a mix-blend mode, so let's wrap all
      // those contents into a blend container, and then wrap the blend
      // container in the async zoom container. Otherwise the blend container
      // ends up outside the zoom container which results in blend failure for
      // WebRender.
      nsDisplayItem* blendContainer =
          nsDisplayBlendContainer::CreateForMixBlendMode(
              aBuilder, this, &rootResultList,
              aBuilder->CurrentActiveScrolledRoot(),
              nsDisplayItem::ContainerASRType::Constant);
      rootResultList.AppendToTop(blendContainer);

      // Blend containers can be created or omitted during partial updates
      // depending on the dirty rect. So we basically can't do partial updates
      // if there's a blend container involved. There is equivalent code to this
      // in the BuildDisplayListForStackingContext function as well, with a more
      // detailed comment explaining things better.
      if (aBuilder->IsRetainingDisplayList()) {
        if (aBuilder->IsPartialUpdate()) {
          aBuilder->SetPartialBuildFailed(true);
        } else {
          aBuilder->SetDisablePartialUpdates(true);
        }
      }
    }

    mozilla::layers::FrameMetrics::ViewID viewID =
        nsLayoutUtils::FindOrCreateIDFor(mScrolledFrame->GetContent());

    DisplayListClipState::AutoSaveRestore clipState(aBuilder);
    clipState.ClipContentDescendants(aAsyncZoomClipRect, aRadii);

    rootResultList.AppendNewToTop<nsDisplayAsyncZoom>(
        aBuilder, this, &rootResultList, aBuilder->CurrentActiveScrolledRoot(),
        nsDisplayItem::ContainerASRType::Constant, viewID);
  }

  if (serializedList) {
    aSet.Content()->AppendToTop(&rootResultList);
  }
}

class nsDisplayListFocus final : public nsPaintedDisplayItem {
 public:
  nsDisplayListFocus(nsDisplayListBuilder* aBuilder, nsListControlFrame* aFrame)
      : nsPaintedDisplayItem(aBuilder, aFrame) {
    MOZ_COUNT_CTOR(nsDisplayListFocus);
  }

  Maybe<nsCSSBorderRenderer> Renderer(DrawTarget* aDt) const {
    auto* listFrame = static_cast<nsListControlFrame*>(Frame());
    auto* option = listFrame->GetCurrentOption();
    if (!option) {
      return {};
    }
    nsIFrame* frame = option->GetPrimaryFrame();
    if (!frame) {
      return {};
    }
    nscolor color = LookAndFeel::Color(
        option->Selected() ? LookAndFeel::ColorID::Selecteditemtext
                           : LookAndFeel::ColorID::Selecteditem,
        frame);
    auto rect = frame->GetRectRelativeToSelf() + frame->GetOffsetTo(listFrame) +
                ToReferenceFrame();
    return Some(
        nsCSSRendering::GetBorderRendererForFocus(listFrame, aDt, rect, color));
  }

  MOZ_COUNTED_DTOR_FINAL(nsDisplayListFocus)

  void Paint(nsDisplayListBuilder* aBuilder, gfxContext* aCtx) override {
    if (auto br = Renderer(aCtx->GetDrawTarget())) {
      br->DrawBorders();
    }
  }
  bool CreateWebRenderCommands(
      wr::DisplayListBuilder& aBuilder, wr::IpcResourceUpdateQueue& aResources,
      const StackingContextHelper& aSc,
      layers::RenderRootStateManager* aManager,
      nsDisplayListBuilder* aDisplayListBuilder) override {
    if (auto br = Renderer(nullptr)) {
      br->CreateWebRenderCommands(this, aBuilder, aResources, aSc);
    }
    return true;
  }
  NS_DISPLAY_DECL_NAME("ListFocus", TYPE_LIST_FOCUS)
};

void ScrollContainerFrame::BuildDisplayList(nsDisplayListBuilder* aBuilder,
                                            const nsDisplayListSet& aLists) {
  SetAndNullOnExit<const nsIFrame> tmpBuilder(
      mReferenceFrameDuringPainting, aBuilder->GetCurrentReferenceFrame());
  if (aBuilder->IsForFrameVisibility()) {
    NotifyApproximateFrameVisibilityUpdate(false);
  }

  DisplayBorderBackgroundOutline(aBuilder, aLists);
  if (HidesContent()) {
    return;
  }

  const bool isRootContent =
      mIsRoot && PresContext()->IsRootContentDocumentCrossProcess();

  const bool capturedByViewTransition = [&] {
    if (!mIsRoot) {
      return false;
    }
    auto* styleFrame = GetFrameForStyle();
    return styleFrame &&
           styleFrame->HasAnyStateBits(NS_FRAME_CAPTURED_IN_VIEW_TRANSITION);
  }();

  // Expand the scroll port to the size including the area covered by dynamic
  // toolbar in the case where the dynamic toolbar is being used since
  // position:fixed elements attached to this root scroller might be taller
  // than its scroll port (e.g 100vh). Even if the dynamic toolbar covers the
  // taller area, it doesn't mean the area is clipped by the toolbar because
  // the dynamic toolbar is laid out outside of our topmost window and it
  // transitions without changing our topmost window size.
  const nsRect effectiveScrollPort =
      GetScrollPortRectAccountingForMaxDynamicToolbar();

  // It's safe to get this value before the DecideScrollableLayer call below
  // because that call cannot create a displayport for root scroll frames,
  // and hence it cannot create an ignore scroll frame.
  const bool ignoringThisScrollFrame = aBuilder->GetIgnoreScrollFrame() == this;

  // Overflow clipping can never clip frames outside our subtree, so there
  // is no need to worry about whether we are a moving frame that might clip
  // non-moving frames.
  // Not all our descendants will be clipped by overflow clipping, but all
  // the ones that aren't clipped will be out of flow frames that have already
  // had dirty rects saved for them by their parent frames calling
  // MarkOutOfFlowChildrenForDisplayList, so it's safe to restrict our
  // dirty rect here.
  nsRect visibleRect = aBuilder->GetVisibleRect();
  nsRect dirtyRect = aBuilder->GetDirtyRect();
  if (!ignoringThisScrollFrame) {
    visibleRect = visibleRect.Intersect(effectiveScrollPort);
    dirtyRect = dirtyRect.Intersect(effectiveScrollPort);
  }

  bool dirtyRectHasBeenOverriden = false;
  (void)DecideScrollableLayer(aBuilder, &visibleRect, &dirtyRect,
                              /* aSetBase = */ !mIsRoot,
                              &dirtyRectHasBeenOverriden);

  if (aBuilder->IsForFrameVisibility()) {
    // We expand the dirty rect to catch frames just outside of the scroll port.
    // We use the dirty rect instead of the whole scroll port to prevent
    // too much expansion in the presence of very large (bigger than the
    // viewport) scroll ports.
    dirtyRect = ExpandRectToNearlyVisible(dirtyRect);
    visibleRect = dirtyRect;
  }

  // We put non-overlay scrollbars in their own layers when this is the root
  // scroll frame and we are a toplevel content document. In this situation,
  // the scrollbar(s) would normally be assigned their own layer anyway, since
  // they're not scrolled with the rest of the document. But when both
  // scrollbars are visible, the layer's visible rectangle would be the size
  // of the viewport, so most layer implementations would create a layer buffer
  // that's much larger than necessary. Creating independent layers for each
  // scrollbar works around the problem.
  const bool createLayersForScrollbars = isRootContent;

  nsDisplayListCollection set(aBuilder);

  if (ignoringThisScrollFrame) {
    // If we are a root scroll frame that has a display port we want to add
    // scrollbars, they will be children of the scrollable layer, but they get
    // adjusted by the APZC automatically.
    bool addScrollBars =
        mIsRoot && mWillBuildScrollableLayer && aBuilder->IsPaintingToWindow();

    if (addScrollBars) {
      // Add classic scrollbars.
      AppendScrollPartsTo(aBuilder, set, createLayersForScrollbars, false);
    }

    {
      nsDisplayListBuilder::AutoBuildingDisplayList building(
          aBuilder, this, visibleRect, dirtyRect);

      // Don't clip the scrolled child, and don't paint scrollbars/scrollcorner.
      // The scrolled frame shouldn't have its own background/border, so we
      // can just pass aLists directly.
      BuildDisplayListForChild(aBuilder, mScrolledFrame, set);
    }

    MaybeCreateTopLayerAndWrapRootItems(aBuilder, set,
                                        /* aCreateAsyncZoom = */ false,
                                        /* aCapturedByViewTransition = */ false,
                                        nullptr, nsRect(), nullptr);

    if (addScrollBars) {
      // Add overlay scrollbars.
      AppendScrollPartsTo(aBuilder, set, createLayersForScrollbars, true);
    }

    set.MoveTo(aLists);
    return;
  }

  // Whether we might want to build a scrollable layer for this scroll frame
  // at some point in the future. This controls whether we add the information
  // to the layer tree (a scroll info layer if necessary, and add the right
  // area to the dispatch to content layer event regions) necessary to activate
  // a scroll frame so it creates a scrollable layer.
  const bool couldBuildLayer = [&] {
    if (!aBuilder->IsPaintingToWindow()) {
      return false;
    }
    if (mWillBuildScrollableLayer) {
      return true;
    }
    return nsLayoutUtils::AsyncPanZoomEnabled(this) && WantAsyncScroll();
  }();

  // Now display the scrollbars and scrollcorner. These parts are drawn
  // in the border-background layer, on top of our own background and
  // borders and underneath borders and backgrounds of later elements
  // in the tree.
  // Note that this does not apply for overlay scrollbars; those are drawn
  // in the positioned-elements layer on top of everything else by the call
  // to AppendScrollPartsTo(..., true) further down.
  AppendScrollPartsTo(aBuilder, aLists, createLayersForScrollbars, false);

  mScrollParentID = aBuilder->GetCurrentScrollParentId();

  AutoContainsBlendModeCapturer blendCapture(*aBuilder);

  const bool willBuildAsyncZoomContainer =
      mWillBuildScrollableLayer && aBuilder->ShouldBuildAsyncZoomContainer() &&
      isRootContent;

  nsRect scrollPortClip =
      effectiveScrollPort + aBuilder->ToReferenceFrame(this);
  nsRect clipRect = scrollPortClip;
  // Our override of GetBorderRadii ensures we never have a radius at
  // the corners where we have a scrollbar.
  nsRectCornerRadii radii;
  const bool haveRadii = GetPaddingBoxBorderRadii(radii);
  if (mIsRoot) {
    clipRect.SizeTo(nsLayoutUtils::CalculateCompositionSizeForFrame(
        this, true /* aSubtractScrollbars */,
        nullptr /* aOverrideScrollPortSize */,
        // With the dynamic toolbar, this CalculateCompositionSizeForFrame call
        // basically expands the region being covered up by the dynamic toolbar,
        // but if the root scroll container is not scrollable, e.g. the root
        // element has `overflow: hidden` or `position: fixed`, the function
        // doesn't expand the region since expanding the region in such cases
        // will prevent the content from restoring zooming to 1.0 zoom level
        // such as bug 1652190. That said, this `clipRect` which will be used
        // for the async zoom container needs to be expanded because zoomed-in
        // contents can be scrollable __visually__ so that the region under the
        // dynamic toolbar area will be revealed.
        nsLayoutUtils::IncludeDynamicToolbar::Force));

    // The composition size is essentially in visual coordinates.
    // If we are hit-testing in layout coordinates, transform the clip rect
    // to layout coordinates to match.
    if (aBuilder->IsRelativeToLayoutViewport() && isRootContent) {
      clipRect = ViewportUtils::VisualToLayout(clipRect, PresShell());
    }
  }

  {
    DisplayListClipState::AutoSaveRestore paddingBoxClipState(aBuilder);
    // The border radius goes around the padding box. This usually matches the
    // scrollport, so for most cases we can use a single clip. Further more, if
    // the scrollport doesn't intersect our rounded rect we can avoid the
    // roundedness altogether.
    const bool radiiOnScrollPort = haveRadii && !IsSingleLineTextInput(this);
    if (haveRadii && !radiiOnScrollPort) {
      auto paddingBoxClip =
          GetPaddingRectRelativeToSelf() + aBuilder->ToReferenceFrame(this);
      nsRegion intersection = nsLayoutUtils::RoundedRectIntersectRect(
          paddingBoxClip, radii, clipRect);
      if (!intersection.GetLargestRectangle().Contains(clipRect)) {
        paddingBoxClipState.ClipContainingBlockDescendants(paddingBoxClip,
                                                           &radii);
      }
    }

    DisplayListClipState::AutoSaveRestore scrollPortClipState(aBuilder);

    // If we're building an async zoom container, clip the contents inside
    // to the layout viewport (scrollPortClip). The composition bounds clip
    // (clipRect) will be applied to the zoom container itself in
    // MaybeCreateTopLayerAndWrapRootItems.
    nsRect clipRectForContents =
        willBuildAsyncZoomContainer ? scrollPortClip : clipRect;
    if (mIsRoot) {
      scrollPortClipState.ClipContentDescendants(
          clipRectForContents, radiiOnScrollPort ? &radii : nullptr);
    } else {
      scrollPortClipState.ClipContainingBlockDescendants(
          clipRectForContents, radiiOnScrollPort ? &radii : nullptr);
    }

    nsDisplayListBuilder::AutoCurrentActiveScrolledRootSetter asrSetter(
        aBuilder);

    if (aBuilder->IsInViewTransitionCapture() || capturedByViewTransition) {
      asrSetter.SetCurrentActiveScrolledRoot(nullptr);
    } else {
      if (mWillBuildScrollableLayer && aBuilder->IsPaintingToWindow()) {
        // If this scroll frame has a first scrollable frame sequence number,
        // ensure that it matches the current paint sequence number. If it does
        // not, reset it so that we can expire the displayport. The stored
        // sequence number will not match that of the current paint if the dom
        // was mutated in some way that alters the order of scroll frames.
        if (IsFirstScrollableFrameSequenceNumber().isSome() &&
            *IsFirstScrollableFrameSequenceNumber() !=
                nsDisplayListBuilder::GetPaintSequenceNumber()) {
          SetIsFirstScrollableFrameSequenceNumber(Nothing());
        }
        asrSetter.EnterScrollFrame(this);
      }
      if (couldBuildLayer && mScrolledFrame->GetContent()) {
        asrSetter.SetCurrentScrollParentId(
            nsLayoutUtils::FindOrCreateIDFor(mScrolledFrame->GetContent()));
      }
    }

    if (mWillBuildScrollableLayer && aBuilder->BuildCompositorHitTestInfo()) {
      // Create a hit test info item for the scrolled content that's not
      // clipped to the displayport. This ensures that within the bounds
      // of the scroll frame, the scrolled content is always hit, even
      // if we are checkerboarding.
      CompositorHitTestInfo info =
          mScrolledFrame->GetCompositorHitTestInfoWithoutPointerEvents(
              aBuilder);

      if (mScrolledFrame->Style()->PointerEvents() !=
              StylePointerEvents::None &&
          info != CompositorHitTestInvisibleToHit) {
        auto* hitInfo =
            MakeDisplayItemWithIndex<nsDisplayCompositorHitTestInfo>(
                aBuilder, mScrolledFrame, 1);
        if (hitInfo) {
          aBuilder->SetInheritedCompositorHitTestInfo(info);
          set.BorderBackground()->AppendToTop(hitInfo);
        }
      }
    }

    {
      // Clip our contents to the unsnapped scrolled rect. This makes sure
      // that we don't have display items over the subpixel seam at the edge
      // of the scrolled area.
      DisplayListClipState::AutoSaveRestore scrolledRectClipState(aBuilder);
      nsRect scrolledRectClip =
          GetUnsnappedScrolledRectInternal(
              mScrolledFrame->ScrollableOverflowRect(), mScrollPort.Size()) +
          mScrolledFrame->GetPosition();
      bool clippedToDisplayPort = false;
      if (mWillBuildScrollableLayer && aBuilder->IsPaintingToWindow()) {
        // Clip the contents to the display port.
        // The dirty rect already acts kind of like a clip, in that
        // FrameLayerBuilder intersects item bounds and opaque regions with
        // it, but it doesn't have the consistent snapping behavior of a
        // true clip.
        // For a case where this makes a difference, imagine the following
        // scenario: The display port has an edge that falls on a fractional
        // layer pixel, and there's an opaque display item that covers the
        // whole display port up until that fractional edge, and there is a
        // transparent display item that overlaps the edge. We want to prevent
        // this transparent item from enlarging the scrolled layer's visible
        // region beyond its opaque region. The dirty rect doesn't do that -
        // it gets rounded out, whereas a true clip gets rounded to nearest
        // pixels.
        // If there is no display port, we don't need this because the clip
        // from the scroll port is still applied.
        scrolledRectClip = scrolledRectClip.Intersect(visibleRect);
        clippedToDisplayPort = scrolledRectClip.IsEqualEdges(visibleRect);
      }
      if (clippedToDisplayPort) {
        scrolledRectClipState.ClipToDisplayPort(
            scrolledRectClip + aBuilder->ToReferenceFrame(this));
      } else {
        // We have to do this after the ClipContainingBlockDescendants call
        // above, otherwise that call will clobber the flag set by this call
        // to SetClippedToDisplayPort.
        scrolledRectClipState.ClipContainingBlockDescendants(
            scrolledRectClip + aBuilder->ToReferenceFrame(this));
      }

      nsRect visibleRectForChildren = visibleRect;
      nsRect dirtyRectForChildren = dirtyRect;

      // If we are entering the RCD-RSF, we are crossing the async zoom
      // container boundary, and need to convert from visual to layout
      // coordinates.
      if (willBuildAsyncZoomContainer && aBuilder->IsForEventDelivery()) {
        MOZ_ASSERT(ViewportUtils::IsZoomedContentRoot(mScrolledFrame));
        visibleRectForChildren =
            ViewportUtils::VisualToLayout(visibleRectForChildren, PresShell());
        dirtyRectForChildren =
            ViewportUtils::VisualToLayout(dirtyRectForChildren, PresShell());
      }

      nsDisplayListBuilder::AutoBuildingDisplayList building(
          aBuilder, this, visibleRectForChildren, dirtyRectForChildren);
      nsDisplayListBuilder::AutoEnterViewTransitionCapture
          inViewTransitionCaptureSetter(aBuilder, capturedByViewTransition);
      if (capturedByViewTransition) {
        scrolledRectClipState.Clear();
      }

      BuildDisplayListForChild(aBuilder, mScrolledFrame, set);

      if (nsListControlFrame* lc = do_QueryFrame(this); lc && lc->IsFocused()) {
        set.Outlines()->AppendNewToTop<nsDisplayListFocus>(aBuilder, lc);
      }

      if (dirtyRectHasBeenOverriden &&
          StaticPrefs::layout_display_list_show_rebuild_area()) {
        nsDisplaySolidColor* color = MakeDisplayItem<nsDisplaySolidColor>(
            aBuilder, this,
            dirtyRect + aBuilder->GetCurrentFrameOffsetToReferenceFrame(),
            NS_RGBA(0, 0, 255, 64));
        if (color) {
          color->SetOverrideZIndex(INT32_MAX);
          set.PositionedDescendants()->AppendToTop(color);
        }
      }
    }

    if (aBuilder->IsPaintingToWindow()) {
      mIsParentToActiveScrollFrames =
          ShouldActivateAllScrollFrames(aBuilder, this)
              ? asrSetter.GetContainsNonMinimalDisplayPort()
              : asrSetter.ShouldForceLayerForScrollParent();
    }

    if (asrSetter.ShouldForceLayerForScrollParent()) {
      // Note that forcing layerization of scroll parents follows the scroll
      // handoff chain which is subject to the out-of-flow-frames caveat noted
      // above (where the asrSetter variable is created).
      MOZ_ASSERT(couldBuildLayer && mScrolledFrame->GetContent() &&
                 aBuilder->IsPaintingToWindow());
      if (!mWillBuildScrollableLayer) {
        // Set a displayport so next paint we don't have to force layerization
        // after the fact. It's ok to pass DoNotRepaint here, since we've
        // already painted the change and we're just optimizing it to be
        // detected earlier. We also won't confuse RetainedDisplayLists
        // with the silent change, since we explicitly request partial updates
        // to be disabled on the next paint.
        DisplayPortUtils::SetDisplayPortMargins(
            GetContent(), PresShell(), DisplayPortMargins::Empty(GetContent()),
            DisplayPortUtils::ClearMinimalDisplayPortProperty::Yes, 0,
            DisplayPortUtils::RepaintMode::DoNotRepaint);
        // Call DecideScrollableLayer to recompute mWillBuildScrollableLayer
        // and recompute the current animated geometry root if needed. It's
        // too late to change the dirty rect so pass a copy.
        nsRect copyOfDirtyRect = dirtyRect;
        nsRect copyOfVisibleRect = visibleRect;
        (void)DecideScrollableLayer(aBuilder, &copyOfVisibleRect,
                                    &copyOfDirtyRect,
                                    /* aSetBase = */ false, nullptr);
        if (mWillBuildScrollableLayer) {
          if (ShouldActivateAllScrollFrames(aBuilder, this)) {
            gfxCriticalNoteOnce << "inserted scroll frame";
          }
          MOZ_ASSERT(!ShouldActivateAllScrollFrames(aBuilder, this));
          asrSetter.InsertScrollFrame(this);
          aBuilder->SetDisablePartialUpdates(true);
        }
      }
    }
  }

  if (mWillBuildScrollableLayer && aBuilder->IsPaintingToWindow()) {
    aBuilder->ForceLayerForScrollParent();
  }

  MaybeCreateTopLayerAndWrapRootItems(
      aBuilder, set, willBuildAsyncZoomContainer, capturedByViewTransition,
      &blendCapture, clipRect, haveRadii ? &radii : nullptr);

  // We want to call SetContainsNonMinimalDisplayPort if
  // mWillBuildScrollableLayer is true for any reason other than having a
  // minimal display port.
  if (mWillBuildScrollableLayer && aBuilder->IsPaintingToWindow()) {
    // Since mWillBuildScrollableLayer = HasDisplayPort || mZoomableByAPZ we can
    // simplify this check to avoid getting the display port again.
    if (mZoomableByAPZ ||
        !GetContent()->GetProperty(nsGkAtoms::MinimalDisplayPort)) {
      MOZ_ASSERT(DisplayPortUtils::HasNonMinimalDisplayPort(GetContent()) ||
                 mZoomableByAPZ);
      aBuilder->SetContainsNonMinimalDisplayPort();
    }
  }

  if (couldBuildLayer & StyleVisibility()->IsVisible()) {
    CompositorHitTestInfo info(CompositorHitTestFlags::eVisibleToHitTest,
                               CompositorHitTestFlags::eInactiveScrollframe);
    // If the scroll frame has non-default overscroll-behavior, instruct
    // APZ to require a target confirmation before processing events that
    // hit this scroll frame (that is, to drop the events if a
    // confirmation does not arrive within the timeout period). Otherwise,
    // APZ's fallback behaviour of scrolling the enclosing scroll frame
    // would violate the specified overscroll-behavior.
    auto overscroll = GetOverscrollBehaviorInfo();
    if (overscroll.mBehaviorX != OverscrollBehavior::Auto ||
        overscroll.mBehaviorY != OverscrollBehavior::Auto) {
      info += CompositorHitTestFlags::eRequiresTargetConfirmation;
    }

    nsRect area = effectiveScrollPort + aBuilder->ToReferenceFrame(this);

    // Make sure that APZ will dispatch events back to content so we can
    // create a displayport for this frame. We'll add the item later on.
    if (!mWillBuildScrollableLayer && aBuilder->BuildCompositorHitTestInfo()) {
      // Make sure the z-index of the inactive item is at least zero.
      // Otherwise, it will end up behind non-positioned items in the scrolled
      // content.
      int32_t zIndex = MaxZIndexInListOfItemsContainedInFrame(
                           set.PositionedDescendants(), this)
                           .valueOr(0);
      if (aBuilder->IsPartialUpdate()) {
        for (nsDisplayItem* item : mScrolledFrame->DisplayItems()) {
          if (item->GetType() ==
              DisplayItemType::TYPE_COMPOSITOR_HITTEST_INFO) {
            auto* hitTestItem =
                static_cast<nsDisplayCompositorHitTestInfo*>(item);
            if (hitTestItem->GetHitTestInfo().Info().contains(
                    CompositorHitTestFlags::eInactiveScrollframe)) {
              zIndex = std::max(zIndex, hitTestItem->ZIndex());
              item->SetCantBeReused();
            }
          }
        }
      }
      nsDisplayCompositorHitTestInfo* hitInfo =
          MakeDisplayItemWithIndex<nsDisplayCompositorHitTestInfo>(
              aBuilder, mScrolledFrame, 1, area, info);
      if (hitInfo) {
        AppendInternalItemToTop(set, hitInfo, Some(zIndex));
        aBuilder->SetInheritedCompositorHitTestInfo(info);
      }
    }

    if (aBuilder->ShouldBuildScrollInfoItemsForHoisting()) {
      aBuilder->AppendNewScrollInfoItemForHoisting(
          MakeDisplayItem<nsDisplayScrollInfoLayer>(aBuilder, mScrolledFrame,
                                                    this, info, area));
    }
  }

  // Now display overlay scrollbars and the resizer, if we have one.
  AppendScrollPartsTo(aBuilder, set, createLayersForScrollbars, true);

  set.MoveTo(aLists);
}

nsRect ScrollContainerFrame::RestrictToRootDisplayPort(
    const nsRect& aDisplayportBase) {
  // This function clips aDisplayportBase so that it is no larger than the
  // root frame's displayport (or the root composition bounds, if we can't
  // obtain the root frame's displayport). This is useful for ensuring that
  // the displayport of a tall scrollframe doesn't gobble up all the memory.

  nsPresContext* pc = PresContext();
  const nsPresContext* rootPresContext =
      pc->GetInProcessRootContentDocumentPresContext();
  if (!rootPresContext) {
    rootPresContext = pc->GetRootPresContext();
  }
  if (!rootPresContext) {
    return aDisplayportBase;
  }
  const mozilla::PresShell* const rootPresShell = rootPresContext->PresShell();
  nsIFrame* displayRootFrame = nsLayoutUtils::GetDisplayRootFrame(this);
  nsIFrame* rootFrame = displayRootFrame->IsMenuPopupFrame()
                            ? displayRootFrame
                            : rootPresShell->GetRootScrollContainerFrame();
  if (!rootFrame) {
    rootFrame = rootPresShell->GetRootFrame();
  }
  if (!rootFrame) {
    return aDisplayportBase;
  }

  // Make sure we aren't trying to restrict to our own displayport, which is a
  // circular dependency.
  MOZ_ASSERT(!mIsRoot || rootPresContext != pc);

  nsRect rootDisplayPort;
  bool hasDisplayPort =
      rootFrame->GetContent() && DisplayPortUtils::GetDisplayPort(
                                     rootFrame->GetContent(), &rootDisplayPort);
  if (hasDisplayPort) {
    // The display port of the root frame already factors in it's callback
    // transform, so subtract it out here, the GetCumulativeApzCallbackTransform
    // call below will add it back.
    MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
            ("RestrictToRootDisplayPort: Existing root displayport is %s\n",
             ToString(rootDisplayPort).c_str()));
    if (nsIContent* content = rootFrame->GetContent()) {
      if (void* property =
              content->GetProperty(nsGkAtoms::apzCallbackTransform)) {
        rootDisplayPort -=
            CSSPoint::ToAppUnits(*static_cast<CSSPoint*>(property));
      }
    }
  } else {
    // If we don't have a display port on the root frame let's fall back to
    // the root composition bounds instead.
    nsRect rootCompBounds =
        nsRect(nsPoint(0, 0),
               nsLayoutUtils::CalculateCompositionSizeForFrame(rootFrame));

    // If rootFrame is the RCD-RSF then
    // CalculateCompositionSizeForFrame did not take the document's
    // resolution into account, so we must.
    if (rootPresContext->IsRootContentDocumentCrossProcess() &&
        rootFrame == rootPresShell->GetRootScrollContainerFrame()) {
      MOZ_LOG(
          sDisplayportLog, LogLevel::Verbose,
          ("RestrictToRootDisplayPort: Removing resolution %f from root "
           "composition bounds %s\n",
           rootPresShell->GetResolution(), ToString(rootCompBounds).c_str()));
      rootCompBounds =
          rootCompBounds.RemoveResolution(rootPresShell->GetResolution());
    }

    rootDisplayPort = rootCompBounds;
  }
  MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
          ("RestrictToRootDisplayPort: Intermediate root displayport %s\n",
           ToString(rootDisplayPort).c_str()));

  // We want to convert the root display port from the
  // coordinate space of |rootFrame| to the coordinate space of
  // |this|. We do that with the TransformRect call below.
  // However, since we care about the root display port
  // relative to what the user is actually seeing, we also need to
  // incorporate the APZ callback transforms into this. Most of the
  // time those transforms are negligible, but in some cases (e.g.
  // when a zoom is applied on an overflow:hidden document) it is
  // not (see bug 1280013).
  // XXX: Eventually we may want to create a modified version of
  // TransformRect that includes the APZ callback transforms
  // directly.
  nsLayoutUtils::TransformRect(rootFrame, this, rootDisplayPort);
  MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
          ("RestrictToRootDisplayPort: Transformed root displayport %s\n",
           ToString(rootDisplayPort).c_str()));
  rootDisplayPort += CSSPoint::ToAppUnits(
      nsLayoutUtils::GetCumulativeApzCallbackTransform(this));
  MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
          ("RestrictToRootDisplayPort: Final root displayport %s\n",
           ToString(rootDisplayPort).c_str()));

  // We want to limit aDisplayportBase to be no larger than
  // rootDisplayPort on either axis, but we don't want to just
  // blindly intersect the two, because rootDisplayPort might be
  // offset from where aDisplayportBase is (see bug 1327095 comment
  // 8). Instead, we translate rootDisplayPort so as to maximize the
  // overlap with aDisplayportBase, and *then* do the intersection.
  if (rootDisplayPort.x > aDisplayportBase.x &&
      rootDisplayPort.XMost() > aDisplayportBase.XMost()) {
    // rootDisplayPort is at a greater x-position for both left and
    // right, so translate it such that the XMost() values are the
    // same. This will line up the right edge of the two rects, and
    // might mean that rootDisplayPort.x is smaller than
    // aDisplayportBase.x. We can avoid that by taking the min of the
    // x delta and XMost() delta, but it doesn't really matter
    // because the intersection between the two rects below will end
    // up the same.
    rootDisplayPort.x -= (rootDisplayPort.XMost() - aDisplayportBase.XMost());
  } else if (rootDisplayPort.x < aDisplayportBase.x &&
             rootDisplayPort.XMost() < aDisplayportBase.XMost()) {
    // Analaogous code for when the rootDisplayPort is at a smaller
    // x-position.
    rootDisplayPort.x = aDisplayportBase.x;
  }
  // Do the same for y-axis
  if (rootDisplayPort.y > aDisplayportBase.y &&
      rootDisplayPort.YMost() > aDisplayportBase.YMost()) {
    rootDisplayPort.y -= (rootDisplayPort.YMost() - aDisplayportBase.YMost());
  } else if (rootDisplayPort.y < aDisplayportBase.y &&
             rootDisplayPort.YMost() < aDisplayportBase.YMost()) {
    rootDisplayPort.y = aDisplayportBase.y;
  }
  MOZ_LOG(
      sDisplayportLog, LogLevel::Verbose,
      ("RestrictToRootDisplayPort: Root displayport translated to %s to "
       "better enclose %s\n",
       ToString(rootDisplayPort).c_str(), ToString(aDisplayportBase).c_str()));

  // Now we can do the intersection
  return aDisplayportBase.Intersect(rootDisplayPort);
}

/* static */ bool ScrollContainerFrame::ShouldActivateAllScrollFrames(
    nsDisplayListBuilder* aBuilder, nsIFrame* aFrame) {
  if (aBuilder) {
    return aBuilder->ShouldActivateAllScrollFrames();
  }
  MOZ_ASSERT(aFrame);
  if (StaticPrefs::apz_wr_activate_all_scroll_frames()) {
    return true;
  }
  if (StaticPrefs::apz_wr_activate_all_scroll_frames_when_fission() &&
      FissionAutostart()) {
    return true;
  }
  return StaticPrefs::apz_async_scroll_css_anchor_pos_AtStartup() &&
         aFrame->PresShell()->GetRootPresShell()->HasSeenAnchorPos();
}

bool ScrollContainerFrame::DecideScrollableLayerEnsureDisplayport(
    nsDisplayListBuilder* aBuilder) {
  MOZ_ASSERT(ShouldActivateAllScrollFrames(aBuilder, this));
  nsIContent* content = GetContent();
  bool hasDisplayPort = DisplayPortUtils::HasDisplayPort(content);

  // Note this intentionally differs from DecideScrollableLayer below by not
  // checking ShouldActivateAllScrollFrames.
  if (!hasDisplayPort && aBuilder->IsPaintingToWindow() &&
      nsLayoutUtils::AsyncPanZoomEnabled(this) && WantAsyncScroll()) {
    DisplayPortUtils::SetMinimalDisplayPortDuringPainting(content, PresShell());
    hasDisplayPort = true;
  }

  mWillBuildScrollableLayer = hasDisplayPort || mZoomableByAPZ;
  return mWillBuildScrollableLayer;
}

bool ScrollContainerFrame::DecideScrollableLayer(
    nsDisplayListBuilder* aBuilder, nsRect* aVisibleRect, nsRect* aDirtyRect,
    bool aSetBase, bool* aDirtyRectHasBeenOverriden) {
  if (aBuilder->IsInViewTransitionCapture()) {
    // If we're in a view transition, don't activate the scrollframe. We don't
    // create APZ data for those subtrees anyways and they can't scroll.
    mWillBuildScrollableLayer = false;
    return false;
  }

  nsIContent* content = GetContent();
  bool hasDisplayPort = DisplayPortUtils::HasDisplayPort(content);
  // For hit testing purposes with fission we want to create a
  // minimal display port for every scroll frame that could be active. (We only
  // do this when aSetBase is true because we only want to do this the first
  // time this function is called for the same scroll frame.)
  if (aSetBase && !hasDisplayPort && aBuilder->IsPaintingToWindow() &&
      ShouldActivateAllScrollFrames(aBuilder, this) &&
      nsLayoutUtils::AsyncPanZoomEnabled(this) && WantAsyncScroll()) {
    DisplayPortUtils::SetMinimalDisplayPortDuringPainting(content, PresShell());
    hasDisplayPort = true;
  }

  if (aBuilder->IsPaintingToWindow()) {
    if (aSetBase) {
      nsRect displayportBase = *aVisibleRect;
      nsPresContext* pc = PresContext();

      bool isChromeRootDoc =
          !pc->Document()->IsContentDocument() && !pc->GetParentPresContext();

      if (mIsRoot &&
          (pc->IsRootContentDocumentCrossProcess() || isChromeRootDoc)) {
        displayportBase =
            nsRect(nsPoint(0, 0),
                   nsLayoutUtils::CalculateCompositionSizeForFrame(this));
      } else {
        // Make the displayport base equal to the visible rect restricted to
        // the scrollport and the root composition bounds, relative to the
        // scrollport.
        displayportBase = aVisibleRect->Intersect(mScrollPort);

        mozilla::layers::ScrollableLayerGuid::ViewID viewID =
            mozilla::layers::ScrollableLayerGuid::NULL_SCROLL_ID;
        if (MOZ_LOG_TEST(sDisplayportLog, LogLevel::Verbose)) {
          nsLayoutUtils::FindIDFor(GetContent(), &viewID);
          MOZ_LOG(
              sDisplayportLog, LogLevel::Verbose,
              ("Scroll id %" PRIu64 " has visible rect %s, scroll port %s\n",
               viewID, ToString(*aVisibleRect).c_str(),
               ToString(mScrollPort).c_str()));
        }

        // Only restrict to the root displayport bounds if necessary,
        // as the required coordinate transformation is expensive.
        // And don't call RestrictToRootDisplayPort if we would be trying to
        // restrict to our own display port, which doesn't make sense (ie if we
        // are a root scroll frame in a process root prescontext).
        if (hasDisplayPort && (!mIsRoot || pc->GetParentPresContext()) &&
            !DisplayPortUtils::WillUseEmptyDisplayPortMargins(content)) {
          displayportBase = RestrictToRootDisplayPort(displayportBase);
          MOZ_LOG(sDisplayportLog, LogLevel::Verbose,
                  ("Scroll id %" PRIu64 " has restricted base %s\n", viewID,
                   ToString(displayportBase).c_str()));
        }
        displayportBase -= mScrollPort.TopLeft();
      }

      DisplayPortUtils::SetDisplayPortBase(GetContent(), displayportBase);
    }

    // If we don't have aSetBase == true then should have already
    // been called with aSetBase == true which should have set a
    // displayport base.
    MOZ_ASSERT(content->GetProperty(nsGkAtoms::DisplayPortBase));
    nsRect displayPort;
    hasDisplayPort = DisplayPortUtils::GetDisplayPort(
        content, &displayPort,
        DisplayPortOptions().With(DisplayportRelativeTo::ScrollFrame));

    auto OverrideDirtyRect = [&](const nsRect& aRect) {
      *aDirtyRect = aRect;
      if (aDirtyRectHasBeenOverriden) {
        *aDirtyRectHasBeenOverriden = true;
      }
    };

    if (hasDisplayPort) {
      // Override the dirty rectangle if the displayport has been set.
      *aVisibleRect = displayPort;
      if (aBuilder->IsReusingStackingContextItems() ||
          !aBuilder->IsPartialUpdate() || aBuilder->InInvalidSubtree() ||
          IsFrameModified()) {
        OverrideDirtyRect(displayPort);
      } else if (HasOverrideDirtyRegion()) {
        nsRect* rect = GetProperty(
            nsDisplayListBuilder::DisplayListBuildingDisplayPortRect());
        if (rect) {
          OverrideDirtyRect(*rect);
        }
      }
    } else if (mIsRoot) {
      // The displayPort getter takes care of adjusting for resolution. So if
      // we have resolution but no displayPort then we need to adjust for
      // resolution here.
      auto* presShell = PresShell();
      *aVisibleRect =
          aVisibleRect->RemoveResolution(presShell->GetResolution());
      *aDirtyRect = aDirtyRect->RemoveResolution(presShell->GetResolution());
    }
  }

  // Since making new layers is expensive, only create a scrollable layer
  // for some scroll frames.
  // When a displayport is being used, force building of a layer so that
  // the compositor can find the scrollable layer for async scrolling.
  // If the element is marked 'scrollgrab', also force building of a layer
  // so that APZ can implement scroll grabbing.
  mWillBuildScrollableLayer = hasDisplayPort || mZoomableByAPZ;
  return mWillBuildScrollableLayer;
}

void ScrollContainerFrame::NotifyApzTransaction() {
  mAllowScrollOriginDowngrade = true;
  mApzScrollPos = GetScrollPosition();
  mApzAnimationRequested = IsLastScrollUpdateAnimating();
  mApzAnimationTriggeredByScriptRequested =
      IsLastScrollUpdateTriggeredByScriptAnimating();
  mScrollUpdates.Clear();
  if (mIsRoot) {
    PresShell()->SetResolutionUpdated(false);
  }
}

Maybe<ScrollMetadata> ScrollContainerFrame::ComputeScrollMetadata(
    WebRenderLayerManager* aLayerManager, const nsIFrame* aItemFrame,
    const nsPoint& aOffsetToReferenceFrame) const {
  if (!mWillBuildScrollableLayer) {
    return Nothing();
  }

  bool isRootContent =
      mIsRoot && PresContext()->IsRootContentDocumentCrossProcess();

  MOZ_ASSERT(mScrolledFrame->GetContent());

  return Some(nsLayoutUtils::ComputeScrollMetadata(
      mScrolledFrame, this, GetContent(), aItemFrame, aOffsetToReferenceFrame,
      aLayerManager, mScrollParentID, mScrollPort.Size(), isRootContent));
}

bool ScrollContainerFrame::IsRectNearlyVisible(const nsRect& aRect) const {
  // Use the right rect depending on if a display port is set.
  nsRect displayPort;
  bool usingDisplayport = DisplayPortUtils::GetDisplayPort(
      GetContent(), &displayPort,
      DisplayPortOptions().With(DisplayportRelativeTo::ScrollFrame));

  if (mIsRoot && !usingDisplayport &&
      PresContext()->IsRootContentDocumentInProcess() &&
      !PresContext()->IsRootContentDocumentCrossProcess()) {
    // In the case of the root scroller of OOP iframes, there are cases where
    // any display port value isn't set, e.g. the iframe element is out of view
    // in the parent document. In such cases we'd consider the iframe is not
    // visible.
    return false;
  }

  return aRect.Intersects(
      ExpandRectToNearlyVisible(usingDisplayport ? displayPort : mScrollPort));
}

OverscrollBehaviorInfo ScrollContainerFrame::GetOverscrollBehaviorInfo() const {
  nsIFrame* frame = GetFrameForStyle();
  if (!frame) {
    return {};
  }

  auto& disp = *frame->StyleDisplay();
  return OverscrollBehaviorInfo::FromStyleConstants(disp.mOverscrollBehaviorX,
                                                    disp.mOverscrollBehaviorY);
}

ScrollStyles ScrollContainerFrame::GetScrollStyles() const {
  nsPresContext* presContext = PresContext();
  if (!presContext->IsDynamic() &&
      !(mIsRoot && presContext->HasPaginatedScrolling())) {
    return ScrollStyles(StyleOverflow::Hidden, StyleOverflow::Hidden);
  }

  if (!mIsRoot) {
    if (IsSingleLineTextInput(this)) {
      return !GetWritingMode().IsVertical()
                 ? ScrollStyles(StyleOverflow::Auto, StyleOverflow::Hidden)
                 : ScrollStyles(StyleOverflow::Hidden, StyleOverflow::Auto);
    }
    return ScrollStyles(*StyleDisplay(),
                        ScrollStyles::MapOverflowToValidScrollStyle);
  }

  ScrollStyles result = presContext->GetViewportScrollStylesOverride();
  if (nsDocShell* ds = presContext->GetDocShell()) {
    switch (ds->ScrollbarPreference()) {
      case ScrollbarPreference::Auto:
        break;
      case ScrollbarPreference::Never:
        result.mHorizontal = result.mVertical = StyleOverflow::Hidden;
        break;
    }
  }
  return result;
}

nsRect ScrollContainerFrame::GetLayoutScrollRange() const {
  return GetScrollRange(mScrollPort.width, mScrollPort.height);
}

nsRect ScrollContainerFrame::GetScrollRange(nscoord aWidth,
                                            nscoord aHeight) const {
  nsRect range = GetScrolledRect();
  range.width = std::max(range.width - aWidth, 0);
  range.height = std::max(range.height - aHeight, 0);
  return range;
}

nsRect ScrollContainerFrame::GetVisualScrollRange() const {
  nsSize visualViewportSize = GetVisualViewportSize();
  return GetScrollRange(visualViewportSize.width, visualViewportSize.height);
}

nsSize ScrollContainerFrame::GetVisualViewportSize() const {
  auto* presShell = PresShell();
  if (mIsRoot && presShell->IsVisualViewportSizeSet()) {
    return presShell->GetVisualViewportSize();
  }
  return mScrollPort.Size();
}

nsPoint ScrollContainerFrame::GetVisualViewportOffset() const {
  if (mIsRoot) {
    auto* presShell = PresShell();
    if (auto pendingUpdate = presShell->GetPendingVisualScrollUpdate()) {
      // The pending visual scroll update on the PresShell contains a raw,
      // unclamped offset (basically, whatever was passed to ScrollToVisual()).
      // It will be clamped on the APZ side, but if we use it as the
      // main-thread's visual viewport offset we need to clamp it ourselves.
      // Use GetScrollRangeForUserInputEvents() to do the clamping because this
      // the scroll range that APZ will use.
      return GetScrollRangeForUserInputEvents().ClampPoint(
          pendingUpdate->mVisualScrollOffset);
    }
    return presShell->GetVisualViewportOffset();
  }
  return GetScrollPosition();
}

bool ScrollContainerFrame::SetVisualViewportOffset(const nsPoint& aOffset,
                                                   bool aRepaint) {
  MOZ_ASSERT(mIsRoot);
  AutoWeakFrame weakFrame(this);
  AutoScrollbarRepaintSuppression repaintSuppression(this, weakFrame,
                                                     !aRepaint);

  bool retVal =
      PresShell()->SetVisualViewportOffset(aOffset, GetScrollPosition());
  if (!weakFrame.IsAlive()) {
    return false;
  }
  return retVal;
}

nsRect ScrollContainerFrame::GetVisualOptimalViewingRect() const {
  auto* presShell = PresShell();
  nsRect rect = mScrollPort;
  if (mIsRoot && presShell->IsVisualViewportSizeSet() &&
      presShell->IsVisualViewportOffsetSet()) {
    rect = nsRect(mScrollPort.TopLeft() - GetScrollPosition() +
                      presShell->GetVisualViewportOffset(),
                  presShell->GetVisualViewportSize());
  }
  // NOTE: We intentionally resolve scroll-padding percentages against the
  // scrollport even when the visual viewport is set, see
  // https://github.com/w3c/csswg-drafts/issues/4393.
  rect.Deflate(GetScrollPadding());
  return rect;
}

static void AdjustDestinationForWholeDelta(const nsIntPoint& aDelta,
                                           const nsRect& aScrollRange,
                                           nsPoint& aPoint) {
  if (aDelta.x < 0) {
    aPoint.x = aScrollRange.X();
  } else if (aDelta.x > 0) {
    aPoint.x = aScrollRange.XMost();
  }
  if (aDelta.y < 0) {
    aPoint.y = aScrollRange.Y();
  } else if (aDelta.y > 0) {
    aPoint.y = aScrollRange.YMost();
  }
}

/**
 * Calculate lower/upper scrollBy range in given direction.
 * @param aDelta specifies scrollBy direction, if 0 then range will be 0 size
 * @param aPos desired destination in AppUnits
 * @param aNeg/PosTolerance defines relative range distance
 *   below and above of aPos point
 * @param aMultiplier used for conversion of tolerance into appUnis
 */
static void CalcRangeForScrollBy(int32_t aDelta, nscoord aPos,
                                 float aNegTolerance, float aPosTolerance,
                                 nscoord aMultiplier, nscoord* aLower,
                                 nscoord* aUpper) {
  if (!aDelta) {
    *aLower = *aUpper = aPos;
    return;
  }
  *aLower = aPos - NSToCoordRound(aMultiplier *
                                  (aDelta > 0 ? aNegTolerance : aPosTolerance));
  *aUpper = aPos + NSToCoordRound(aMultiplier *
                                  (aDelta > 0 ? aPosTolerance : aNegTolerance));
}

void ScrollContainerFrame::ScrollBy(nsIntPoint aDelta, ScrollUnit aUnit,
                                    ScrollMode aMode, nsIntPoint* aOverflow,
                                    ScrollOrigin aOrigin,
                                    ScrollMomentum aMomentum,
                                    ScrollSnapFlags aSnapFlags) {
  // When a smooth scroll is being processed on a frame, mouse wheel and
  // trackpad momentum scroll event updates must notcancel the SMOOTH or
  // SMOOTH_MSD scroll animations, enabling Javascript that depends on them to
  // be responsive without forcing the user to wait for the fling animations to
  // completely stop.
  switch (aMomentum) {
    case NOT_MOMENTUM:
      mIgnoreMomentumScroll = false;
      break;
    case SYNTHESIZED_MOMENTUM_EVENT:
      if (mIgnoreMomentumScroll) {
        return;
      }
      break;
  }

  if (mAsyncSmoothMSDScroll != nullptr) {
    // When CSSOM-View scroll-behavior smooth scrolling is interrupted,
    // the scroll is not completed to avoid non-smooth snapping to the
    // prior smooth scroll's destination.
    mDestination = GetScrollPosition();
  }

  nsSize deltaMultiplier;
  float negativeTolerance;
  float positiveTolerance;
  if (aOrigin == ScrollOrigin::NotSpecified) {
    aOrigin = ScrollOrigin::Other;
  }
  bool isGenericOrigin = (aOrigin == ScrollOrigin::Other);

  bool askApzToDoTheScroll = false;
  if ((aSnapFlags == ScrollSnapFlags::Disabled || !NeedsScrollSnap()) &&
      gfxPlatform::UseDesktopZoomingScrollbars() &&
      nsLayoutUtils::AsyncPanZoomEnabled(this) &&
      !nsLayoutUtils::ShouldDisableApzForElement(GetContent()) &&
      (WantAsyncScroll() || mZoomableByAPZ) &&
      CanApzScrollInTheseDirections(DirectionsInDelta(aDelta))) {
    askApzToDoTheScroll = true;
  }

  switch (aUnit) {
    case ScrollUnit::DEVICE_PIXELS: {
      nscoord appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
      deltaMultiplier = nsSize(appUnitsPerDevPixel, appUnitsPerDevPixel);
      if (isGenericOrigin) {
        aOrigin = ScrollOrigin::Pixels;
      }
      negativeTolerance = positiveTolerance = 0.5f;
      break;
    }
    case ScrollUnit::LINES: {
      deltaMultiplier = GetLineScrollAmount();
      if (isGenericOrigin) {
        aOrigin = ScrollOrigin::Lines;
      }
      negativeTolerance = positiveTolerance = 0.1f;
      break;
    }
    case ScrollUnit::PAGES: {
      deltaMultiplier = GetPageScrollAmount();
      if (isGenericOrigin) {
        aOrigin = ScrollOrigin::Pages;
      }
      negativeTolerance = 0.05f;
      positiveTolerance = 0;
      break;
    }
    case ScrollUnit::WHOLE: {
      if (askApzToDoTheScroll) {
        MOZ_ASSERT(aDelta.x >= -1 && aDelta.x <= 1 && aDelta.y >= -1 &&
                   aDelta.y <= 1);
        deltaMultiplier = GetScrollRangeForUserInputEvents().Size();
        break;
      } else {
        nsPoint pos = GetScrollPosition();
        AdjustDestinationForWholeDelta(aDelta, GetLayoutScrollRange(), pos);
        ScrollToWithOrigin(
            pos, nullptr /* range */,
            ScrollOperationParams{aMode, ScrollOrigin::Other, aSnapFlags,
                                  ScrollTriggeredByScript::No});
        // 'this' might be destroyed here
        if (aOverflow) {
          *aOverflow = nsIntPoint(0, 0);
        }
        return;
      }
    }
    default:
      NS_ERROR("Invalid scroll mode");
      return;
  }

  if (askApzToDoTheScroll) {
    // Stop suppressing displayport while the page is still loading.
    if (MOZ_UNLIKELY(PresShell()->IsDocumentLoading())) {
      PresShell()->SuppressDisplayport(false);
    }

    nsPoint delta(
        NSCoordSaturatingNonnegativeMultiply(aDelta.x, deltaMultiplier.width),
        NSCoordSaturatingNonnegativeMultiply(aDelta.y, deltaMultiplier.height));

    AppendScrollUpdate(
        ScrollPositionUpdate::NewPureRelativeScroll(aOrigin, aMode, delta));

    nsIContent* content = GetContent();
    if (!DisplayPortUtils::HasNonMinimalNonZeroDisplayPort(content)) {
      if (MOZ_LOG_TEST(sDisplayportLog, LogLevel::Debug)) {
        mozilla::layers::ScrollableLayerGuid::ViewID viewID =
            mozilla::layers::ScrollableLayerGuid::NULL_SCROLL_ID;
        nsLayoutUtils::FindIDFor(content, &viewID);
        MOZ_LOG(
            sDisplayportLog, LogLevel::Debug,
            ("ScrollBy setting displayport on scrollId=%" PRIu64 "\n", viewID));
      }

      DisplayPortUtils::CalculateAndSetDisplayPortMargins(
          GetScrollTargetFrame(), DisplayPortUtils::RepaintMode::Repaint);
      nsIFrame* frame = do_QueryFrame(GetScrollTargetFrame());
      DisplayPortUtils::SetZeroMarginDisplayPortOnAsyncScrollableAncestors(
          frame);
    }

    SchedulePaint();
    return;
  }

  nsPoint newPos(NSCoordSaturatingAdd(mDestination.x,
                                      NSCoordSaturatingNonnegativeMultiply(
                                          aDelta.x, deltaMultiplier.width)),
                 NSCoordSaturatingAdd(mDestination.y,
                                      NSCoordSaturatingNonnegativeMultiply(
                                          aDelta.y, deltaMultiplier.height)));

  Maybe<SnapDestination> snapDestination;
  if (aSnapFlags != ScrollSnapFlags::Disabled) {
    if (NeedsScrollSnap()) {
      nscoord appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
      deltaMultiplier = nsSize(appUnitsPerDevPixel, appUnitsPerDevPixel);
      negativeTolerance = 0.1f;
      positiveTolerance = 0;
      ScrollUnit snapUnit = aUnit;
      if (aOrigin == ScrollOrigin::MouseWheel) {
        // When using a clicky scroll wheel, snap point selection works the same
        // as keyboard up/down/left/right navigation, but with varying amounts
        // of scroll delta.
        snapUnit = ScrollUnit::LINES;
      }
      snapDestination = GetSnapPointForDestination(snapUnit, aSnapFlags,
                                                   mDestination, newPos);
      if (snapDestination) {
        newPos = snapDestination->mPosition;
      }
    }
  }

  // Calculate desired range values.
  nscoord rangeLowerX, rangeUpperX, rangeLowerY, rangeUpperY;
  CalcRangeForScrollBy(aDelta.x, newPos.x, negativeTolerance, positiveTolerance,
                       deltaMultiplier.width, &rangeLowerX, &rangeUpperX);
  CalcRangeForScrollBy(aDelta.y, newPos.y, negativeTolerance, positiveTolerance,
                       deltaMultiplier.height, &rangeLowerY, &rangeUpperY);
  nsRect range(rangeLowerX, rangeLowerY, rangeUpperX - rangeLowerX,
               rangeUpperY - rangeLowerY);
  AutoWeakFrame weakFrame(this);
  ScrollToWithOrigin(
      newPos, &range,
      snapDestination
          ? ScrollOperationParams{aMode, aOrigin,
                                  std::move(snapDestination->mTargetIds)}
          : ScrollOperationParams{aMode, aOrigin});
  if (!weakFrame.IsAlive()) {
    return;
  }

  if (aOverflow) {
    nsPoint clampAmount = newPos - mDestination;
    float appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
    *aOverflow =
        nsIntPoint(NSAppUnitsToIntPixels(clampAmount.x, appUnitsPerDevPixel),
                   NSAppUnitsToIntPixels(clampAmount.y, appUnitsPerDevPixel));
  }

  if (aUnit == ScrollUnit::DEVICE_PIXELS &&
      !nsLayoutUtils::AsyncPanZoomEnabled(this)) {
    // When APZ is disabled, we must track the velocity
    // on the main thread; otherwise, the APZC will manage this.
    mVelocityQueue.Sample(GetScrollPosition());
  }
}

void ScrollContainerFrame::ScrollByCSSPixelsInternal(
    const CSSPoint& aDelta, ScrollMode aMode, ScrollSnapFlags aSnapFlags) {
  nsPoint current = GetScrollPosition();
  // `current` value above might be a value which was aligned to in
  // layer-pixels, so starting from such points will make the difference between
  // the given position in script (e.g. scrollTo) and the aligned position
  // larger, in the worst case the difference can be observed in CSS pixels.
  // To avoid it, we use the current position in CSS pixels as the start
  // position.  Hopefully it exactly matches the position where it was given by
  // the previous scrolling operation, but there may be some edge cases where
  // the current position in CSS pixels differs from the given position, the
  // cases should be fixed in bug 1556685.
  CSSPoint currentCSSPixels;
  if (StaticPrefs::layout_disable_pixel_alignment()) {
    currentCSSPixels = GetScrollPositionCSSPixels();
  } else {
    currentCSSPixels = GetRoundedScrollPositionCSSPixels();
  }
  nsPoint pt = CSSPoint::ToAppUnits(currentCSSPixels + aDelta);

  nscoord halfPixel = nsPresContext::CSSPixelsToAppUnits(0.5f);
  nsRect range(pt.x - halfPixel, pt.y - halfPixel, 2 * halfPixel - 1,
               2 * halfPixel - 1);
  // XXX I don't think the following blocks are needed anymore, now that
  // ScrollToImpl simply tries to scroll an integer number of layer
  // pixels from the current position
  if (aDelta.x == 0.0f) {
    pt.x = current.x;
    range.x = pt.x;
    range.width = 0;
  }
  if (aDelta.y == 0.0f) {
    pt.y = current.y;
    range.y = pt.y;
    range.height = 0;
  }
  ScrollToWithOrigin(
      pt, &range,
      ScrollOperationParams{aMode, ScrollOrigin::Relative, aSnapFlags,
                            ScrollTriggeredByScript::Yes});
  // 'this' might be destroyed here
}

bool ScrollContainerFrame::ScrollSnap(ScrollMode aMode) {
  float flingSensitivity =
      StaticPrefs::layout_css_scroll_snap_prediction_sensitivity();
  int maxVelocity =
      StaticPrefs::layout_css_scroll_snap_prediction_max_velocity();
  maxVelocity = nsPresContext::CSSPixelsToAppUnits(maxVelocity);
  int maxOffset = maxVelocity * flingSensitivity;
  nsPoint velocity = mVelocityQueue.GetVelocity();
  // Multiply each component individually to avoid integer multiply
  nsPoint predictedOffset =
      nsPoint(velocity.x * flingSensitivity, velocity.y * flingSensitivity);
  predictedOffset.Clamp(maxOffset);
  nsPoint pos = GetScrollPosition();
  nsPoint destinationPos = pos + predictedOffset;
  return ScrollSnap(destinationPos, aMode);
}

bool ScrollContainerFrame::ScrollSnap(const nsPoint& aDestination,
                                      ScrollMode aMode) {
  nsRect scrollRange = GetLayoutScrollRange();
  nsPoint pos = GetScrollPosition();
  nsPoint destination = scrollRange.ClampPoint(aDestination);
  ScrollSnapFlags snapFlags = ScrollSnapFlags::IntendedEndPosition;
  if (mVelocityQueue.GetVelocity() != nsPoint()) {
    snapFlags |= ScrollSnapFlags::IntendedDirection;
  }

  // Bug 1776624 : Consider using mDestination as |aStartPos| argument for this
  // GetSnapPointForDestination call, this function call is the only one call
  // site using `GetScrollPosition()` as |aStartPos|.
  if (auto snapDestination = GetSnapPointForDestination(
          ScrollUnit::DEVICE_PIXELS, snapFlags, pos, destination)) {
    destination = snapDestination->mPosition;
    ScrollToWithOrigin(
        destination, nullptr /* range */,
        ScrollOperationParams{aMode, ScrollOrigin::Other,
                              std::move(snapDestination->mTargetIds)});
    return true;
  }
  return false;
}

nsSize ScrollContainerFrame::GetLineScrollAmount() const {
  RefPtr<nsFontMetrics> fm =
      nsLayoutUtils::GetInflatedFontMetricsForFrame(this);
  NS_ASSERTION(fm, "FontMetrics is null, assuming fontHeight == 1 appunit");
  int32_t appUnitsPerDevPixel = PresContext()->AppUnitsPerDevPixel();
  nscoord minScrollAmountInAppUnits =
      std::max(1, StaticPrefs::mousewheel_min_line_scroll_amount()) *
      appUnitsPerDevPixel;
  nscoord horizontalAmount = fm ? fm->AveCharWidth() : 0;
  nscoord verticalAmount = fm ? fm->MaxHeight() : 0;
  return nsSize(std::max(horizontalAmount, minScrollAmountInAppUnits),
                std::max(verticalAmount, minScrollAmountInAppUnits));
}

/**
 * Compute the scrollport size excluding any fixed-pos and sticky-pos (that are
 * stuck) headers and footers. A header or footer is an box that spans that
 * entire width of the viewport and touches the top (or bottom, respectively) of
 * the viewport. We also want to consider fixed/sticky elements that stack or
 * overlap to effectively create a larger header or footer. Headers and footers
 * that cover more than a third of the the viewport are ignored since they
 * probably aren't true headers and footers and we don't want to restrict
 * scrolling too much in such cases. This is a bit conservative --- some
 * pages use elements as headers or footers that don't span the entire width
 * of the viewport --- but it should be a good start.
 *
 * If aViewportFrame is non-null then the scroll frame is the root scroll
 * frame and we should consider fixed-pos items.
 */
struct TopAndBottom {
  TopAndBottom(nscoord aTop, nscoord aBottom) : top(aTop), bottom(aBottom) {}

  nscoord top, bottom;
};
struct TopComparator {
  bool Equals(const TopAndBottom& A, const TopAndBottom& B) const {
    return A.top == B.top;
  }
  bool LessThan(const TopAndBottom& A, const TopAndBottom& B) const {
    return A.top < B.top;
  }
};
struct ReverseBottomComparator {
  bool Equals(const TopAndBottom& A, const TopAndBottom& B) const {
    return A.bottom == B.bottom;
  }
  bool LessThan(const TopAndBottom& A, const TopAndBottom& B) const {
    return A.bottom > B.bottom;
  }
};

static void AddToListIfHeaderFooter(nsIFrame* aFrame,
                                    nsIFrame* aScrollPortFrame,
                                    const nsRect& aScrollPort,
                                    nsTArray<TopAndBottom>& aList) {
  nsRect r = aFrame->GetRectRelativeToSelf();
  r = nsLayoutUtils::TransformFrameRectToAncestor(aFrame, r, aScrollPortFrame);
  r = r.Intersect(aScrollPort);
  if ((r.width >= aScrollPort.width / 2 ||
       r.width >= NSIntPixelsToAppUnits(800, AppUnitsPerCSSPixel())) &&
      r.height <= aScrollPort.height / 3) {
    aList.AppendElement(TopAndBottom(r.y, r.YMost()));
  }
}

StickyScrollContainer& ScrollContainerFrame::EnsureStickyContainer() {
  if (!mStickyContainer) {
    mStickyContainer = MakeUnique<StickyScrollContainer>(this);
  }
  return *mStickyContainer;
}

static nsSize GetScrollPortSizeExcludingHeadersAndFooters(
    ScrollContainerFrame* aScrollFrame, nsIFrame* aViewportFrame,
    const nsRect& aScrollPort) {
  AutoTArray<TopAndBottom, 10> list;
  if (aViewportFrame) {
    for (nsIFrame* f :
         aViewportFrame->GetChildList(FrameChildListID::Absolute)) {
      AddToListIfHeaderFooter(f, aViewportFrame, aScrollPort, list);
    }
  }

  // Add sticky frames that are currently in "fixed" positions
  if (auto* ssc = aScrollFrame->GetStickyContainer()) {
    for (nsIFrame* f : ssc->GetFrames().IterFromShallowest()) {
      // If it's acting like fixed position.
      if (ssc->IsStuckInYDirection(f)) {
        AddToListIfHeaderFooter(f, aScrollFrame, aScrollPort, list);
      }
    }
  }

  list.Sort(TopComparator());
  nscoord headerBottom = 0;
  for (uint32_t i = 0; i < list.Length(); ++i) {
    if (list[i].top <= headerBottom) {
      headerBottom = std::max(headerBottom, list[i].bottom);
    }
  }

  list.Sort(ReverseBottomComparator());
  nscoord footerTop = aScrollPort.height;
  for (uint32_t i = 0; i < list.Length(); ++i) {
    if (list[i].bottom >= footerTop) {
      footerTop = std::min(footerTop, list[i].top);
    }
  }

  headerBottom = std::min(aScrollPort.height / 3, headerBottom);
  footerTop = std::max(aScrollPort.height - aScrollPort.height / 3, footerTop);

  return nsSize(aScrollPort.width, footerTop - headerBottom);
}

nsSize ScrollContainerFrame::GetPageScrollAmount() const {
  nsSize effectiveScrollPortSize = GetVisualOptimalViewingRect().Size();

  // We want to use the visual viewport size if one is set.
  // The headers/footers adjustment is too complicated to do if there is a
  // visual viewport that differs from the layout viewport, this is probably
  // okay.
  // We also assume that if scroll-padding is in use, the author knows what
  // they're doing and we don't need to auto-account for sticky / fixed stuff.
  if (effectiveScrollPortSize == mScrollPort.Size()) {
    // Reduce effective scrollport height by the height of any
    // fixed-pos/sticky-pos headers or footers
    effectiveScrollPortSize = GetScrollPortSizeExcludingHeadersAndFooters(
        const_cast<ScrollContainerFrame*>(this),
        mIsRoot ? PresShell()->GetRootFrame() : nullptr, mScrollPort);
  }

  nsSize lineScrollAmount = GetLineScrollAmount();
  const int32_t maxOverlapPercent = std::clamp(
      StaticPrefs::toolkit_scrollbox_pagescroll_maxOverlapPercent(), 0, 80);
  const int32_t maxOverlapLines =
      std::max(StaticPrefs::toolkit_scrollbox_pagescroll_maxOverlapLines(), 0);

  // The page increment is the size of the page, minus some overlap.
  return nsSize(
      effectiveScrollPortSize.width -
          std::min(effectiveScrollPortSize.width * maxOverlapPercent / 100,
                   maxOverlapLines * lineScrollAmount.width),
      effectiveScrollPortSize.height -
          std::min(effectiveScrollPortSize.height * maxOverlapPercent / 100,
                   maxOverlapLines * lineScrollAmount.height));
}

/**
 * this code is resposible for restoring the scroll position back to some
 * saved position. if the user has not moved the scroll position manually
 * we keep scrolling down until we get to our original position. keep in
 * mind that content could incrementally be coming in. we only want to stop
 * when we reach our new position.
 */
void ScrollContainerFrame::ScrollToRestoredPosition() {
  if (!NeedRestorePosition()) {
    return;
  }
  // make sure our scroll position did not change for where we last put
  // it. if it does then the user must have moved it, and we no longer
  // need to restore.
  //
  // In the RTL case, we check whether the scroll position changed using the
  // logical scroll position, but we scroll to the physical scroll position in
  // all cases

  // The layout offset we want to restore is the same as the visual offset
  // (for now, may change in bug 1499210), but clamped to the layout scroll
  // range (which can be a subset of the visual scroll range).
  // Note that we can't do the clamping when initializing mRestorePos in
  // RestoreState(), since the scrollable rect (which the clamping depends
  // on) can change over the course of the restoration process.
  nsPoint layoutRestorePos = GetLayoutScrollRange().ClampPoint(mRestorePos);
  nsPoint visualRestorePos = GetVisualScrollRange().ClampPoint(mRestorePos);

  // Continue restoring until both the layout and visual scroll positions
  // reach the destination. (Note that the two can only be different for
  // the root content document's root scroll frame, and when zoomed in).
  // This is necessary to avoid situations where the two offsets get stuck
  // at different values and nothing reconciles them (see bug 1519621 comment
  // 8).
  nsPoint logicalLayoutScrollPos = GetLogicalScrollPosition();

  SCROLLRESTORE_LOG(
      "%p: ScrollToRestoredPosition (mRestorePos=%s, mLastPos=%s, "
      "layoutRestorePos=%s, visualRestorePos=%s, "
      "logicalLayoutScrollPos=%s, "
      "GetLogicalVisualViewportOffset()=%s)\n",
      this, ToString(mRestorePos).c_str(), ToString(mLastPos).c_str(),
      ToString(layoutRestorePos).c_str(), ToString(visualRestorePos).c_str(),
      ToString(logicalLayoutScrollPos).c_str(),
      ToString(GetLogicalVisualViewportOffset()).c_str());

  // if we didn't move, we still need to restore
  if (GetLogicalVisualViewportOffset() == mLastPos ||
      logicalLayoutScrollPos == mLastPos) {
    // if our desired position is different to the scroll position, scroll.
    // remember that we could be incrementally loading so we may enter
    // and scroll many times.
    if (mRestorePos != mLastPos /* GetLogicalVisualViewportOffset() */ ||
        layoutRestorePos != logicalLayoutScrollPos) {
      LoadingState state = GetPageLoadingState();
      if (state == LoadingState::Stopped && !IsSubtreeDirty()) {
        return;
      }
      nsPoint visualScrollToPos = visualRestorePos;
      nsPoint layoutScrollToPos = layoutRestorePos;
      if (!IsPhysicalLTR()) {
        // convert from logical to physical scroll position
        visualScrollToPos.x -=
            (GetVisualViewportSize().width - mScrolledFrame->GetRect().width);
        layoutScrollToPos.x -=
            (GetVisualViewportSize().width - mScrolledFrame->GetRect().width);
      }
      AutoWeakFrame weakFrame(this);
      // It's very important to pass ScrollOrigin::Restore here, so
      // ScrollToWithOrigin won't clear out mRestorePos.
      ScrollToWithOrigin(
          layoutScrollToPos, nullptr,
          ScrollOperationParams{ScrollMode::Instant, ScrollOrigin::Restore});
      if (!weakFrame.IsAlive()) {
        return;
      }
      if (mIsRoot) {
        PresShell()->ScrollToVisual(visualScrollToPos, FrameMetrics::eRestore,
                                    ScrollMode::Instant);
      }
      if (state == LoadingState::Loading || IsSubtreeDirty()) {
        // If we're trying to do a history scroll restore, then we want to
        // keep trying this until we succeed, because the page can be loading
        // incrementally. So re-get the scroll position for the next iteration,
        // it might not be exactly equal to mRestorePos due to rounding and
        // clamping.
        mLastPos = GetLogicalVisualViewportOffset();
        return;
      }
    }
    // If we get here, either we reached the desired position (mLastPos ==
    // mRestorePos) or we're not trying to do a history scroll restore, so
    // we can stop after the scroll attempt above.
    mRestorePos.y = -1;
    mLastPos.x = -1;
    mLastPos.y = -1;
  } else {
    // user moved the position, so we won't need to restore
    mLastPos.x = -1;
    mLastPos.y = -1;
  }
}

ScrollContainerFrame::LoadingState ScrollContainerFrame::GetPageLoadingState() {
  bool loadCompleted = false, stopped = false;
  nsCOMPtr<nsIDocShell> ds = GetContent()->GetComposedDoc()->GetDocShell();
  if (ds) {
    nsCOMPtr<nsIDocumentViewer> viewer;
    ds->GetDocViewer(getter_AddRefs(viewer));
    if (viewer) {
      loadCompleted = viewer->GetLoadCompleted();
      stopped = viewer->GetIsStopped();
    }
  }
  return loadCompleted
             ? (stopped ? LoadingState::Stopped : LoadingState::Loaded)
             : LoadingState::Loading;
}

PhysicalAxes ScrollContainerFrame::GetOverflowAxes() const {
  nsSize scrollportSize = mScrollPort.Size();
  nsSize childSize = GetScrolledRect().Size();

  PhysicalAxes result;

  if (childSize.height > scrollportSize.height) {
    result += PhysicalAxis::Vertical;
  }

  if (childSize.width > scrollportSize.width) {
    result += PhysicalAxis::Horizontal;
  }

  return result;
}

nsresult ScrollContainerFrame::FireScrollPortEvent() {
  mAsyncScrollPortEvent.Forget();

  // TODO(emilio): why do we need the whole WillPaintObserver infrastructure and
  // can't use AddScriptRunner & co? I guess it made sense when we used
  // WillPaintObserver for scroll events too, or when this used to flush.
  //
  // Should we remove this?

  PhysicalAxes overflowAxes = GetOverflowAxes();

  bool newVerticalOverflow = overflowAxes.contains(PhysicalAxis::Vertical);
  bool vertChanged = mVerticalOverflow != newVerticalOverflow;

  bool newHorizontalOverflow = overflowAxes.contains(PhysicalAxis::Horizontal);
  bool horizChanged = mHorizontalOverflow != newHorizontalOverflow;

  if (!vertChanged && !horizChanged) {
    return NS_OK;
  }

  // If both either overflowed or underflowed then we dispatch only one
  // DOM event.
  bool both = vertChanged && horizChanged &&
              newVerticalOverflow == newHorizontalOverflow;
  InternalScrollPortEvent::OrientType orient;
  if (both) {
    orient = InternalScrollPortEvent::eBoth;
    mHorizontalOverflow = newHorizontalOverflow;
    mVerticalOverflow = newVerticalOverflow;
  } else if (vertChanged) {
    orient = InternalScrollPortEvent::eVertical;
    mVerticalOverflow = newVerticalOverflow;
    if (horizChanged) {
      // We need to dispatch a separate horizontal DOM event. Do that the next
      // time around since dispatching the vertical DOM event might destroy
      // the frame.
      PostOverflowEvent();
    }
  } else {
    orient = InternalScrollPortEvent::eHorizontal;
    mHorizontalOverflow = newHorizontalOverflow;
  }

  InternalScrollPortEvent event(
      true,
      (orient == InternalScrollPortEvent::eHorizontal ? mHorizontalOverflow
                                                      : mVerticalOverflow)
          ? eScrollPortOverflow
          : eScrollPortUnderflow,
      nullptr);
  event.mOrient = orient;

  RefPtr<nsPresContext> presContext = PresContext();
  RefPtr target = ScrollEventTargetNode(RootTargetsDocument::No);
  return EventDispatcher::Dispatch(target, presContext, &event);
}

void ScrollContainerFrame::PostOrDeferScrollEndEvent() {
  bool isInScrollbarButtonClickAndHold =
      (mVScrollbarBox ? mVScrollbarBox->GetButtonScrollDirection() : false) ||
      (mHScrollbarBox ? mHScrollbarBox->GetButtonScrollDirection() : false);
  bool isInScrollbarClickAndHold =
      SliderFrameInClickAndHold() || isInScrollbarButtonClickAndHold;

  SCROLLEND_LOG("%s: is-in-click-and-hold=%s", __FUNCTION__,
                isInScrollbarClickAndHold ? "true" : "false");
  if (!isInScrollbarClickAndHold) {
    PostScrollEndEvent();
  } else {
    mScrollbarClickAndHoldScrollendPending = true;
  }
}

void ScrollContainerFrame::PostScrollEndEvent() {
  if (mScrollEndEvent) {
    return;
  }

  // If this is the rood document and is not an iframe, we may need to post
  // a scrollend event to the VisualViewport.
  if (mIsRoot && PresContext()->IsRootContentDocumentCrossProcess() &&
      PresShell()->IsVisualViewportOffsetSet()) {
    if (auto* window = nsGlobalWindowInner::Cast(
            PresContext()->Document()->GetInnerWindow())) {
      window->VisualViewport()->PostScrollEndEvent();
    }
  }

  // The ScrollEndEvent constructor registers itself.
  mScrollEndEvent = MakeRefPtr<ScrollEndEvent>(this);
}

RefPtr<nsINode> ScrollContainerFrame::ScrollEventTargetNode(
    RootTargetsDocument aRootTargetsDocument) const {
  if (aRootTargetsDocument == RootTargetsDocument::Yes && mIsRoot) {
    return PresContext()->Document();
  }
  return mContent.get();
}

void ScrollContainerFrame::FireScrollEndEvent() {
  MOZ_ASSERT(mScrollEndEvent);
  mScrollEndEvent->Revoke();
  mScrollEndEvent = nullptr;

  RefPtr<nsPresContext> presContext = PresContext();
  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetGUIEvent event(true, eScrollend, nullptr);
  event.mFlags.mBubbles = mIsRoot;
  event.mFlags.mCancelable = false;
  RefPtr<nsINode> target = ScrollEventTargetNode(RootTargetsDocument::Yes);
  EventDispatcher::Dispatch(target, presContext, &event, nullptr, &status);
}

void ScrollContainerFrame::ReloadChildFrames() {
  mScrolledFrame = nullptr;
  mHScrollbarBox = nullptr;
  mVScrollbarBox = nullptr;
  mScrollCornerBox = nullptr;
  mResizerBox = nullptr;

  for (nsIFrame* frame : PrincipalChildList()) {
    nsIContent* content = frame->GetContent();
    if (content == GetContent()) {
      NS_ASSERTION(!mScrolledFrame, "Already found the scrolled frame");
      mScrolledFrame = frame;
    } else if (content == mVScrollbarContent) {
      NS_ASSERTION(!mVScrollbarBox, "Found multiple vertical scrollbars?");
      mVScrollbarBox = do_QueryFrame(frame);
      MOZ_ASSERT(mVScrollbarBox, "Not a scrollbar?");
    } else if (content == mHScrollbarContent) {
      NS_ASSERTION(!mHScrollbarBox, "Found multiple horizontal scrollbars?");
      mHScrollbarBox = do_QueryFrame(frame);
      MOZ_ASSERT(mHScrollbarBox, "Not a scrollbar?");
    } else if (content == mResizerContent) {
      NS_ASSERTION(!mResizerBox, "Found multiple resizers");
      mResizerBox = frame;
    } else if (content == mScrollCornerContent) {
      NS_ASSERTION(!mScrollCornerBox, "Found multiple scrollcorners");
      mScrollCornerBox = frame;
    }
  }
}

already_AddRefed<Element> ScrollContainerFrame::MakeScrollbar(
    NodeInfo* aNodeInfo, bool aVertical, AnonymousContentKey& aKey) {
  MOZ_ASSERT(aNodeInfo);
  MOZ_ASSERT(
      aNodeInfo->Equals(nsGkAtoms::scrollbar, nullptr, kNameSpaceID_XUL));

  aKey = AnonymousContentKey::Type_Scrollbar;
  if (aVertical) {
    aKey |= AnonymousContentKey::Flag_Vertical;
  }

  RefPtr<Element> e;
  NS_TrustedNewXULElement(getter_AddRefs(e), do_AddRef(aNodeInfo));

#ifdef DEBUG
  // Scrollbars can get restyled by theme changes.  Whether such a restyle
  // will actually reconstruct them correctly if it involves a frame
  // reconstruct... I don't know.  :(
  e->SetProperty(nsGkAtoms::restylableAnonymousNode,
                 reinterpret_cast<void*>(true));
#endif  // DEBUG

  if (aVertical) {
    e->SetAttr(kNameSpaceID_None, nsGkAtoms::vertical, u"true"_ns, false);
  }

  if (mIsRoot) {
    e->SetProperty(nsGkAtoms::docLevelNativeAnonymousContent,
                   reinterpret_cast<void*>(true));
    e->SetAttr(kNameSpaceID_None, nsGkAtoms::root, u"true"_ns, false);

    // Don't bother making style caching take [root] styles into account.
    aKey = AnonymousContentKey::None;
  }

  return e.forget();
}

auto ScrollContainerFrame::GetCurrentAnonymousContent() const
    -> EnumSet<AnonymousContentType> {
  EnumSet<AnonymousContentType> result;
  if (mHScrollbarContent) {
    result += AnonymousContentType::HorizontalScrollbar;
  }
  if (mVScrollbarContent) {
    result += AnonymousContentType::VerticalScrollbar;
  }
  if (mResizerContent) {
    result += AnonymousContentType::Resizer;
  }
  return result;
}

auto ScrollContainerFrame::GetNeededAnonymousContent() const
    -> EnumSet<AnonymousContentType> {
  nsPresContext* pc = PresContext();

  // Don't create scrollbars if we're an SVG document being used as an image,
  // or if we're printing/print previewing.
  // (In the printing case, we allow scrollbars if this is the child of the
  // viewport & paginated scrolling is enabled, because then we must be the
  // scroll frame for the print preview window, & that does need scrollbars.)
  if (pc->Document()->IsBeingUsedAsImage() ||
      (!pc->IsDynamic() && !(mIsRoot && pc->HasPaginatedScrolling()))) {
    return {};
  }

  EnumSet<AnonymousContentType> result;
  // If we're the scrollframe for the root, then we want to construct our
  // scrollbar frames no matter what.  That way later dynamic changes to
  // propagated overflow styles will show or hide scrollbars on the viewport
  // without requiring frame reconstruction of the viewport (good!).
  //
  // TODO(emilio): Figure out if we can remove this special-case now that we
  // have more targeted optimizations.
  if (mIsRoot) {
    result += AnonymousContentType::HorizontalScrollbar;
    result += AnonymousContentType::VerticalScrollbar;
    // If scrollbar-width is none, don't generate scrollbars.
    // NOTE: This is a non-root case so that we can use this container's
    // mComputedStyle here.
  } else if (ScrollbarWidth(mComputedStyle) != StyleScrollbarWidth::None) {
    ScrollStyles styles = GetScrollStyles();
    if (styles.mHorizontal != StyleOverflow::Hidden) {
      result += AnonymousContentType::HorizontalScrollbar;
    }
    if (styles.mVertical != StyleOverflow::Hidden) {
      result += AnonymousContentType::VerticalScrollbar;
    }
  }

  // Check if the frame is resizable. Note:
  // "The effect of the resize property on generated content is undefined.
  //  Implementations should not apply the resize property to generated
  //  content." [1]
  // For info on what is generated content, see [2].
  // [1]: https://drafts.csswg.org/css-ui/#resize
  // [2]: https://www.w3.org/TR/CSS2/generate.html#content
  auto resizeStyle = StyleDisplay()->mResize;
  if (resizeStyle != StyleResize::None &&
      !HasAnyStateBits(NS_FRAME_GENERATED_CONTENT)) {
    result += AnonymousContentType::Resizer;
  }

  return result;
}

nsresult ScrollContainerFrame::CreateAnonymousContent(
    nsTArray<ContentInfo>& aElements) {
  nsPresContext* presContext = PresContext();
  nsNodeInfoManager* nodeInfoManager =
      presContext->Document()->NodeInfoManager();

  auto neededAnonContent = GetNeededAnonymousContent();
  if (neededAnonContent.isEmpty()) {
    return NS_OK;
  }

  {
    RefPtr<NodeInfo> nodeInfo = nodeInfoManager->GetNodeInfo(
        nsGkAtoms::scrollbar, nullptr, kNameSpaceID_XUL, nsINode::ELEMENT_NODE);
    NS_ENSURE_TRUE(nodeInfo, NS_ERROR_OUT_OF_MEMORY);

    if (neededAnonContent.contains(AnonymousContentType::HorizontalScrollbar)) {
      AnonymousContentKey key;
      mHScrollbarContent = MakeScrollbar(nodeInfo, /* aVertical */ false, key);
      aElements.AppendElement(ContentInfo(mHScrollbarContent, key));
    }

    if (neededAnonContent.contains(AnonymousContentType::VerticalScrollbar)) {
      AnonymousContentKey key;
      mVScrollbarContent = MakeScrollbar(nodeInfo, /* aVertical */ true, key);
      aElements.AppendElement(ContentInfo(mVScrollbarContent, key));
    }
  }

  if (neededAnonContent.contains(AnonymousContentType::Resizer)) {
    MOZ_ASSERT(!mIsRoot, "Root scroll frame shouldn't be resizable");

    RefPtr<NodeInfo> nodeInfo;
    nodeInfo = nodeInfoManager->GetNodeInfo(
        nsGkAtoms::resizer, nullptr, kNameSpaceID_XUL, nsINode::ELEMENT_NODE);
    NS_ENSURE_TRUE(nodeInfo, NS_ERROR_OUT_OF_MEMORY);

    NS_TrustedNewXULElement(getter_AddRefs(mResizerContent), nodeInfo.forget());

    nsAutoString dir;
    switch (StyleDisplay()->mResize) {
      case StyleResize::Horizontal:
        if (IsScrollbarOnRight()) {
          dir.AssignLiteral("right");
        } else {
          dir.AssignLiteral("left");
        }
        break;
      case StyleResize::Vertical:
        dir.AssignLiteral("bottom");
        if (!IsScrollbarOnRight()) {
          mResizerContent->SetAttr(kNameSpaceID_None, nsGkAtoms::flip, u""_ns,
                                   false);
        }
        break;
      case StyleResize::Both:
        if (IsScrollbarOnRight()) {
          dir.AssignLiteral("bottomright");
        } else {
          dir.AssignLiteral("bottomleft");
        }
        break;
      default:
        NS_WARNING("only resizable types should have resizers");
    }
    mResizerContent->SetAttr(kNameSpaceID_None, nsGkAtoms::dir, dir, false);
    aElements.AppendElement(mResizerContent);
  }

  if (neededAnonContent.contains(AnonymousContentType::HorizontalScrollbar) &&
      neededAnonContent.contains(AnonymousContentType::VerticalScrollbar)) {
    AnonymousContentKey key = AnonymousContentKey::Type_ScrollCorner;

    RefPtr<NodeInfo> nodeInfo =
        nodeInfoManager->GetNodeInfo(nsGkAtoms::scrollcorner, nullptr,
                                     kNameSpaceID_XUL, nsINode::ELEMENT_NODE);
    NS_TrustedNewXULElement(getter_AddRefs(mScrollCornerContent),
                            nodeInfo.forget());
    if (mIsRoot) {
      mScrollCornerContent->SetProperty(
          nsGkAtoms::docLevelNativeAnonymousContent,
          reinterpret_cast<void*>(true));
      mScrollCornerContent->SetAttr(kNameSpaceID_None, nsGkAtoms::root,
                                    u"true"_ns, false);

      // Don't bother making style caching take [root="true"] styles into
      // account.
      key = AnonymousContentKey::None;
    }
    aElements.AppendElement(ContentInfo(mScrollCornerContent, key));
  }
  return NS_OK;
}

void ScrollContainerFrame::AppendAnonymousContentTo(
    nsTArray<nsIContent*>& aElements, uint32_t aFilter) {
  if (mHScrollbarContent) {
    aElements.AppendElement(mHScrollbarContent);
  }

  if (mVScrollbarContent) {
    aElements.AppendElement(mVScrollbarContent);
  }

  if (mScrollCornerContent) {
    aElements.AppendElement(mScrollCornerContent);
  }

  if (mResizerContent) {
    aElements.AppendElement(mResizerContent);
  }
}

enum class WebkitScrollbarSize { Auto, Zero, NonZero };

static std::pair<WebkitScrollbarSize, WebkitScrollbarSize>
GetWebkitScrollbarWidthAndHeight(
    const RefPtr<ComputedStyle>& aWebKitScrollbarStyle) {
  MOZ_ASSERT(aWebKitScrollbarStyle);
  const auto webkitScrollbarWidth =
      aWebKitScrollbarStyle->StylePosition()->GetWidth(
          // scrollbar elements are not affected by anchor positioning.
          AnchorPosResolutionParams{nullptr, StylePositionProperty::Static});
  const auto webkitScrollbarHeight =
      aWebKitScrollbarStyle->StylePosition()->GetHeight(
          // scrollbar elements are not affected by anchor positioning.
          AnchorPosResolutionParams{nullptr, StylePositionProperty::Static});
  auto toSize = [](const AnchorResolvedSize& size) {
    if (!size->IsLengthPercentage()) {
      return WebkitScrollbarSize::Auto;
    }
    // On Blink/WebKit %-unit size is treated as 0.
    if (size->AsLengthPercentage().IsLength() &&
        !size->AsLengthPercentage().AsLength().IsZero()) {
      return WebkitScrollbarSize::NonZero;
    }
    return WebkitScrollbarSize::Zero;
  };
  return {toSize(webkitScrollbarWidth), toSize(webkitScrollbarHeight)};
}

void ScrollContainerFrame::DidSetComputedStyle(
    ComputedStyle* aOldComputedStyle) {
  nsContainerFrame::DidSetComputedStyle(aOldComputedStyle);

  // Resolve styles for ::-webkit-scrollbar pseudo element
  if (StaticPrefs::layout_css_fake_webkit_scrollbar_enabled()) {
    mWebKitScrollbarStyle = PresContext()->StyleSet()->ProbePseudoElementStyle(
        *GetContent()->AsElement(), PseudoStyleType::WebkitScrollbar, nullptr,
        mComputedStyle);
  }

  const bool disableOverlayScrollbars =
      [&](const RefPtr<ComputedStyle>& style) {
        // If there's any ::webkit-scrollbar for this container, then check
        // whether there exits non-zero width or height value.
        if (!style) {
          return false;
        }
        if (style->StyleDisplay()->mDisplay == StyleDisplay::None) {
          return false;
        }
        auto [width, height] = GetWebkitScrollbarWidthAndHeight(style);
        return width == WebkitScrollbarSize::NonZero ||
               height == WebkitScrollbarSize::NonZero;
      }(mWebKitScrollbarStyle);

  if (mForceDisableOverlayScrollbars != disableOverlayScrollbars) {
    mForceDisableOverlayScrollbars = disableOverlayScrollbars;
    MarkScrollbarsDirtyForReflow();

    if (mForceDisableOverlayScrollbars) {
      DisableOverlayScrollbars();
    } else {
      EnableOverlayScrollbars();
    }
  }

  if (aOldComputedStyle && !mIsRoot &&
      StyleDisplay()->mScrollSnapType !=
          aOldComputedStyle->StyleDisplay()->mScrollSnapType) {
    PostPendingResnap();
  }
}

void ScrollContainerFrame::RemoveObservers() {
  if (mAsyncScroll) {
    mAsyncScroll->RemoveObserver();
    mAsyncScroll = nullptr;
  }
  if (mAsyncSmoothMSDScroll) {
    mAsyncSmoothMSDScroll->RemoveObserver();
    mAsyncSmoothMSDScroll = nullptr;
  }
}

void ScrollContainerFrame::ActivityOccurred() {
  if (mScrollbarActivity &&
      (mHasHorizontalScrollbar || mHasVerticalScrollbar)) {
    RefPtr<ScrollbarActivity> scrollbarActivity(mScrollbarActivity);
    scrollbarActivity->ActivityOccurred();
  }
}

/**
 * Called when we want to update the scrollbar position, either because
 * scrolling happened or the user moved the scrollbar position and we need to
 * undo that (e.g., when the user clicks to scroll and we're using smooth
 * scrolling, so we need to put the thumb back to its initial position for the
 * start of the smooth sequence).
 */
void ScrollContainerFrame::UpdateScrollbarPosition() {
  mFrameIsUpdatingScrollbar = true;

  nsPoint pt = GetScrollPosition();
  nsRect scrollRange = GetVisualScrollRange();

  if (gfxPlatform::UseDesktopZoomingScrollbars()) {
    pt = GetVisualViewportOffset();
    scrollRange = GetScrollRangeForUserInputEvents();
  }

  if (mVScrollbarBox && mVScrollbarBox->SetCurPos(CSSPixel::FromAppUnitsRounded(
                            pt.y - scrollRange.y))) {
    ActivityOccurred();
  }
  if (mHScrollbarBox && mHScrollbarBox->SetCurPos(CSSPixel::FromAppUnitsRounded(
                            pt.x - scrollRange.x))) {
    ActivityOccurred();
  }

  mFrameIsUpdatingScrollbar = false;
}

void ScrollContainerFrame::ScrollbarCurPosChanged(bool aDoScroll) {
  // Attribute changes on the scrollbars happen in one of three ways:
  // 1) The scrollbar changed the attribute in response to some user event
  // 2) We changed the attribute in response to a ScrollPositionDidChange
  // callback from the scrolling view
  // 3) We changed the attribute to adjust the scrollbars for the start
  // of a smooth scroll operation
  //
  // In cases 2 and 3 we do not need to scroll because we're just
  // updating our scrollbar.
  if (mFrameIsUpdatingScrollbar) {
    return;
  }

  nsRect scrollRange = GetVisualScrollRange();
  nsPoint current = GetScrollPosition() - scrollRange.TopLeft();
  if (gfxPlatform::UseDesktopZoomingScrollbars()) {
    scrollRange = GetScrollRangeForUserInputEvents();
    current = GetVisualViewportOffset() - scrollRange.TopLeft();
  }

  nsPoint dest = current;
  nsRect allowedRange(current, nsSize());
  if (mHScrollbarBox) {
    dest.x = CSSPixel::ToAppUnits(mHScrollbarBox->GetCurPos());
    if (dest.x) {
      const nscoord halfPixel = AppUnitsPerCSSPixel() / 2;
      // Any nscoord value that would round to the attribute value when
      // converted to CSS pixels is allowed.
      allowedRange.x = dest.x - halfPixel;
      allowedRange.width = halfPixel * 2 - 1;
    }
  }
  if (mVScrollbarBox) {
    dest.y = CSSPixel::ToAppUnits(mVScrollbarBox->GetCurPos());
    if (dest.y) {
      const nscoord halfPixel = AppUnitsPerCSSPixel() / 2;
      // Any nscoord value that would round to the attribute value when
      // converted to CSS pixels is allowed.
      allowedRange.y = dest.y - halfPixel;
      allowedRange.height = halfPixel * 2 - 1;
    }
  }
  current += scrollRange.TopLeft();
  dest += scrollRange.TopLeft();
  allowedRange += scrollRange.TopLeft();

  // Don't try to scroll if we're already at an acceptable place.
  // Don't call Contains here since Contains returns false when the point is
  // on the bottom or right edge of the rectangle.
  if (allowedRange.ClampPoint(current) == current) {
    return;
  }

  if (aDoScroll) {
    ScrollToWithOrigin(
        dest, &allowedRange,
        ScrollOperationParams{ScrollMode::Instant, ScrollOrigin::Scrollbars});
  }
  // 'this' might be destroyed here
}

void ScrollContainerFrame::DisableOverlayScrollbars() {
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "ScrollContainerFrame::DisableOverlayScrollbars",
      [weakFrame = std::make_unique<WeakFrame>(this)] {
        if (!weakFrame->IsAlive()) {
          return;
        }
        auto* self = static_cast<ScrollContainerFrame*>(weakFrame->GetFrame());
        if (self->mScrollbarActivity) {
          self->mScrollbarActivity->ActivityStarted();
        }
      }));
}

void ScrollContainerFrame::EnableOverlayScrollbars() {
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "ScrollContainerFrame::EnableOverlayScrollbars",
      [weakFrame = std::make_unique<WeakFrame>(this)] {
        if (!weakFrame->IsAlive()) {
          return;
        }
        auto* self = static_cast<ScrollContainerFrame*>(weakFrame->GetFrame());
        if (self->mScrollbarActivity) {
          self->mScrollbarActivity->ActivityStopped();
        }
      }));
}

/* ============= Scroll events ========== */

ScrollContainerFrame::ScrollEvent::ScrollEvent(ScrollContainerFrame* aHelper)
    : Runnable("ScrollContainerFrame::ScrollEvent"), mHelper(aHelper) {
  mHelper->PresShell()->PostScrollEvent(this);
}

// TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
ScrollContainerFrame::ScrollEvent::Run() {
  if (mHelper) {
    mHelper->FireScrollEvent();
  }
  return NS_OK;
}

ScrollContainerFrame::ScrollEndEvent::ScrollEndEvent(
    ScrollContainerFrame* aHelper)
    : Runnable("ScrollContainerFrame::ScrollEndEvent"), mHelper(aHelper) {
  mHelper->PresShell()->PostScrollEvent(this);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
ScrollContainerFrame::ScrollEndEvent::Run() {
  if (mHelper) {
    mHelper->FireScrollEndEvent();
  }
  return NS_OK;
}

void ScrollContainerFrame::FireScrollEvent() {
  RefPtr<nsIContent> content = GetContent();
  RefPtr<nsPresContext> presContext = PresContext();
  MOZ_ASSERT(mScrollEvent);
  UniquePtr<ProfileChunkedBuffer> backtrace =
      std::move(mScrollEvent->mBacktrace);
  AutoProfilerTracing scrollEventMarker(
      "FireScrollEvent", geckoprofiler::category::GRAPHICS,
      std::move(backtrace),
      geckoprofiler::markers::detail::
          profiler_get_inner_window_id_from_docshell(
              presContext->GetDocShell()));
  mScrollEvent->Revoke();
  mScrollEvent = nullptr;

  bool oldProcessing = mProcessingScrollEvent;
  AutoWeakFrame weakFrame(this);
  auto RestoreProcessingScrollEvent = mozilla::MakeScopeExit([&] {
    if (weakFrame.IsAlive()) {  // Otherwise `this` will be dead too.
      mProcessingScrollEvent = oldProcessing;
    }
  });

  mProcessingScrollEvent = true;

  WidgetGUIEvent event(true, eScroll, nullptr);
  nsEventStatus status = nsEventStatus_eIgnore;
  // Fire viewport scroll events at the document (where they
  // will bubble to the window)
  mozilla::layers::ScrollLinkedEffectDetector detector(
      content->GetComposedDoc(),
      presContext->RefreshDriver()->MostRecentRefresh());
  RefPtr target = ScrollEventTargetNode(RootTargetsDocument::Yes);
  // scroll events fired at elements don't bubble (although scroll events
  // fired at documents do, to the window)
  event.mFlags.mBubbles = mIsRoot;
  EventDispatcher::Dispatch(target, presContext, &event, nullptr, &status);
}

void ScrollContainerFrame::PostScrollEvent() {
  if (mScrollEvent) {
    return;
  }

  // The ScrollEvent constructor registers itself.
  mScrollEvent = MakeRefPtr<ScrollEvent>(this);
  // Capture stack trace now rather when FireScrollEvent runs async
  mScrollEvent->mBacktrace = profiler_capture_backtrace();
}

// TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
ScrollContainerFrame::AsyncScrollPortEvent::Run() {
  return mHelper ? mHelper->FireScrollPortEvent() : NS_OK;
}

void ScrollContainerFrame::PostOverflowEvent() {
  if (mAsyncScrollPortEvent.IsPending()) {
    return;
  }

  if (!nsContentUtils::IsChromeDoc(PresContext()->Document())) {
    return;
  }

  PhysicalAxes overflowAxes = GetOverflowAxes();

  bool newVerticalOverflow = overflowAxes.contains(PhysicalAxis::Vertical);
  bool vertChanged = mVerticalOverflow != newVerticalOverflow;

  bool newHorizontalOverflow = overflowAxes.contains(PhysicalAxis::Horizontal);
  bool horizChanged = mHorizontalOverflow != newHorizontalOverflow;

  if (!vertChanged && !horizChanged) {
    return;
  }

  nsRootPresContext* rpc = PresContext()->GetRootPresContext();
  if (!rpc) {
    return;
  }

  mAsyncScrollPortEvent = MakeRefPtr<AsyncScrollPortEvent>(this);
  rpc->AddWillPaintObserver(mAsyncScrollPortEvent.get());
}

nsIFrame* ScrollContainerFrame::GetFrameForStyle() const {
  if (mIsRoot) {
    if (auto* rootFrame =
            PresContext()->FrameConstructor()->GetRootElementStyleFrame()) {
      return rootFrame;
    }
  }
  return const_cast<ScrollContainerFrame*>(this);
}

bool ScrollContainerFrame::NeedsScrollSnap() const {
  nsIFrame* scrollSnapFrame = GetFrameForStyle();
  if (!scrollSnapFrame) {
    return false;
  }
  return scrollSnapFrame->StyleDisplay()->mScrollSnapType.strictness !=
         StyleScrollSnapStrictness::None;
}

nsSize ScrollContainerFrame::GetSnapportSize() const {
  nsRect snapport = GetScrollPortRect();
  nsMargin scrollPadding = GetScrollPadding();
  snapport.Deflate(scrollPadding);
  return snapport.Size();
}

bool ScrollContainerFrame::IsScrollbarOnRight() const {
  // The position of the scrollbar in top-level windows depends on the pref
  // layout.scrollbar.side. For non-top-level elements, it depends only on the
  // directionaliy of the element (equivalent to a value of "1" for the pref).
  if (!mIsRoot) {
    return IsPhysicalLTR();
  }
  switch (StaticPrefs::layout_scrollbar_side()) {
    default:
    case 0:  // UI directionality
      return StaticPrefs::bidi_direction() == IBMBIDI_TEXTDIRECTION_LTR;
    case 1:  // Document / content directionality
      return IsPhysicalLTR();
    case 2:  // Always right
      return true;
    case 3:  // Always left
      return false;
  }
}

bool ScrollContainerFrame::IsScrollingActive() const {
  const nsStyleDisplay* disp = StyleDisplay();
  if (disp->mWillChange.bits & StyleWillChangeBits::SCROLL) {
    return true;
  }

  nsIContent* content = GetContent();
  return mHasBeenScrolledRecently || IsAlwaysActive() ||
         DisplayPortUtils::HasDisplayPort(content);
}

void ScrollContainerFrame::FinishReflowForScrollbar(
    nsScrollbarFrame* aScrollbar, nscoord aMinXY, nscoord aMaxXY,
    nscoord aCurPosXY, nscoord aPageIncrement) {
  // Scrollbars assume zero is the minimum position, so translate for them.
  bool changed = false;
  changed |=
      aScrollbar->SetCurPos(CSSPixel::FromAppUnitsRounded(aCurPosXY - aMinXY));
  changed |= aScrollbar->SetEnabled(aMaxXY != aMinXY);
  changed |=
      aScrollbar->SetMaxPos(CSSPixel::FromAppUnitsRounded(aMaxXY - aMinXY));
  changed |= aScrollbar->SetPageIncrement(
      CSSPixel::FromAppUnitsRounded(aPageIncrement));
  if (changed) {
    ActivityOccurred();
  }
}

class MOZ_RAII ScrollContainerFrame::AutoMinimumScaleSizeChangeDetector final {
 public:
  explicit AutoMinimumScaleSizeChangeDetector(
      ScrollContainerFrame* aScrollFrame)
      : mHelper(aScrollFrame) {
    MOZ_ASSERT(mHelper);
    MOZ_ASSERT(mHelper->mIsRoot);

    mPreviousMinimumScaleSize = aScrollFrame->mMinimumScaleSize;
    mPreviousIsUsingMinimumScaleSize = aScrollFrame->mIsUsingMinimumScaleSize;
  }
  ~AutoMinimumScaleSizeChangeDetector() {
    if (mPreviousMinimumScaleSize != mHelper->mMinimumScaleSize ||
        mPreviousIsUsingMinimumScaleSize != mHelper->mIsUsingMinimumScaleSize) {
      mHelper->mMinimumScaleSizeChanged = true;
    }
  }

 private:
  ScrollContainerFrame* mHelper;

  nsSize mPreviousMinimumScaleSize;
  bool mPreviousIsUsingMinimumScaleSize;
};

nsSize ScrollContainerFrame::TrueOuterSize(
    nsDisplayListBuilder* aBuilder) const {
  if (!PresShell()->UsesMobileViewportSizing()) {
    return GetSize();
  }

  RefPtr<MobileViewportManager> manager =
      PresShell()->GetMobileViewportManager();
  MOZ_ASSERT(manager);

  LayoutDeviceIntSize displaySize = manager->DisplaySize();

  MOZ_ASSERT(aBuilder);
  // In case of WebRender, we expand the outer size to include the dynamic
  // toolbar area here.
  // In case of non WebRender, we expand the size dynamically in
  // MoveScrollbarForLayerMargin in AsyncCompositionManager.cpp.
  WebRenderLayerManager* layerManager = aBuilder->GetWidgetLayerManager();
  if (layerManager) {
    displaySize.height += ViewAs<LayoutDevicePixel>(
        PresContext()->GetDynamicToolbarMaxHeight(),
        PixelCastJustification::LayoutDeviceIsScreenForBounds);
  }

  return LayoutDeviceSize::ToAppUnits(displaySize,
                                      PresContext()->AppUnitsPerDevPixel());
}

void ScrollContainerFrame::UpdateMinimumScaleSize(
    const nsRect& aScrollableOverflow, const nsSize& aICBSize) {
  MOZ_ASSERT(mIsRoot);

  AutoMinimumScaleSizeChangeDetector minimumScaleSizeChangeDetector(this);

  mIsUsingMinimumScaleSize = false;

  if (!PresShell()->UsesMobileViewportSizing()) {
    return;
  }

  nsPresContext* pc = PresContext();
  MOZ_ASSERT(pc->IsRootContentDocumentCrossProcess(),
             "The pres context should be for the root content document");

  RefPtr<MobileViewportManager> manager =
      PresShell()->GetMobileViewportManager();
  MOZ_ASSERT(manager);

  ScreenIntSize displaySize = ViewAs<ScreenPixel>(
      manager->DisplaySize(),
      PixelCastJustification::LayoutDeviceIsScreenForBounds);
  if (displaySize.width == 0 || displaySize.height == 0) {
    return;
  }
  if (aScrollableOverflow.IsEmpty()) {
    // Bail if the scrollable overflow rect is empty, as we're going to be
    // dividing by it.
    return;
  }

  Document* doc = pc->Document();
  MOZ_ASSERT(doc, "The document should be valid");
  if (doc->GetFullscreenElement()) {
    // Don't use the minimum scale size in the case of fullscreen state.
    // FIXME: 1508177: We will no longer need this.
    return;
  }

  nsViewportInfo viewportInfo = doc->GetViewportInfo(displaySize);
  if (!viewportInfo.IsZoomAllowed()) {
    // Don't apply the minimum scale size if user-scalable=no is specified.
    return;
  }

  // The intrinsic minimum scale is the scale that fits the entire content
  // width into the visual viewport.
  CSSToScreenScale intrinsicMinScale(
      displaySize.width / CSSRect::FromAppUnits(aScrollableOverflow).XMost());

  // The scale used to compute the minimum-scale size is the larger of the
  // intrinsic minimum and the min-scale from the meta viewport tag.
  CSSToScreenScale minScale =
      std::max(intrinsicMinScale, viewportInfo.GetMinZoom());

  // The minimum-scale size is the size of the visual viewport when zoomed
  // to be the minimum scale.
  mMinimumScaleSize = CSSSize::ToAppUnits(ScreenSize(displaySize) / minScale);

  // Ensure the minimum-scale size is never smaller than the ICB size.
  // That could happen if a page has a meta viewport tag with large explicitly
  // specified viewport dimensions (making the ICB large) and also a large
  // minimum scale (making the min-scale size small).
  mMinimumScaleSize = Max(aICBSize, mMinimumScaleSize);

  mIsUsingMinimumScaleSize = true;
}

bool ScrollContainerFrame::ReflowFinished() {
  mPostedReflowCallback = false;

  TryScheduleScrollAnimations();

  if (mIsRoot) {
    if (mMinimumScaleSizeChanged && PresShell()->UsesMobileViewportSizing() &&
        !PresShell()->IsResolutionUpdatedByApz()) {
      RefPtr<MobileViewportManager> manager =
          PresShell()->GetMobileViewportManager();
      MOZ_ASSERT(manager);

      manager->ShrinkToDisplaySizeIfNeeded();
      mMinimumScaleSizeChanged = false;
    }

    if (!UseOverlayScrollbars()) {
      // Layout scrollbars may have added or removed during reflow, so let's
      // update the visual viewport accordingly. Note that this may be a no-op
      // because we might have recomputed the visual viewport size during the
      // reflow itself, just before laying out the fixed-pos items. But there
      // might be cases where that code doesn't run, so this is a sort of
      // backstop to ensure we do that recomputation.
      if (RefPtr<MobileViewportManager> manager =
              PresShell()->GetMobileViewportManager()) {
        manager->UpdateVisualViewportSizeForPotentialScrollbarChange();
      }
    }

#if defined(MOZ_WIDGET_ANDROID)
    const bool hasVerticalOverflow =
        GetOverflowAxes().contains(PhysicalAxis::Vertical) &&
        GetScrollStyles().mVertical != StyleOverflow::Hidden;
    if (!hasVerticalOverflow &&
        (mFirstReflow || mHasVerticalOverflowForDynamicToolbar)) {
      PresShell()->MaybeNotifyShowDynamicToolbar();
    }

    mHasVerticalOverflowForDynamicToolbar = hasVerticalOverflow;
#endif  // defined(MOZ_WIDGET_ANDROID)
  }

  bool doScroll = true;
  if (IsSubtreeDirty()) {
    // We will get another call after the next reflow and scrolling
    // later is less janky.
    doScroll = false;
  }

  if (mFirstReflow) {
    nsPoint currentScrollPos = GetScrollPosition();
    if (!mScrollUpdates.IsEmpty() &&
        mScrollUpdates.LastElement().GetOrigin() == ScrollOrigin::None &&
        currentScrollPos != nsPoint()) {
      // With frame reconstructions, the reconstructed frame may have a nonzero
      // scroll position by the end of the reflow, but without going through
      // RestoreState. In particular this can happen with RTL XUL scrollframes,
      // see https://bugzilla.mozilla.org/show_bug.cgi?id=1664638#c14.
      // Upon construction, the ScrollContainerFrame constructor will have
      // inserted a ScrollPositionUpdate into mScrollUpdates with origin None
      // and a zero scroll position, but here we update that to hold the correct
      // scroll position. Otherwise APZ may end up resetting the scroll position
      // to zero incorrectly. If we ever hit this codepath, it must be on a
      // reflow immediately following the scrollframe construction, so there
      // should be exactly one ScrollPositionUpdate in mScrollUpdates.
      MOZ_ASSERT(mScrollUpdates.Length() == 1);
      MOZ_ASSERT(mScrollUpdates.LastElement().GetGeneration() ==
                 mScrollGeneration);
      MOZ_ASSERT(mScrollUpdates.LastElement().GetDestination() == CSSPoint());
      SCROLLRESTORE_LOG("%p: updating initial SPU to pos %s\n", this,
                        ToString(currentScrollPos).c_str());
      mScrollUpdates.Clear();
      AppendScrollUpdate(
          ScrollPositionUpdate::NewScrollframe(currentScrollPos));
    }

    mFirstReflow = false;
  }

  nsAutoScriptBlocker scriptBlocker;

  if (mReclampVVOffsetInReflowFinished) {
    MOZ_ASSERT(mIsRoot && PresShell()->IsVisualViewportOffsetSet());
    mReclampVVOffsetInReflowFinished = false;
    AutoWeakFrame weakFrame(this);
    PresShell()->SetVisualViewportOffset(PresShell()->GetVisualViewportOffset(),
                                         GetScrollPosition());
    NS_ENSURE_TRUE(weakFrame.IsAlive(), false);
  }

  if (doScroll) {
    ScrollToRestoredPosition();

    // Clamp current scroll position to new bounds. Normally this won't
    // do anything.
    nsPoint currentScrollPos = GetScrollPosition();
    ScrollToImpl(currentScrollPos, nsRect(currentScrollPos, nsSize(0, 0)),
                 ScrollOrigin::Clamp);
    if (ScrollAnimationState().isEmpty()) {
      // We need to have mDestination track the current scroll position,
      // in case it falls outside the new reflow area. mDestination is used
      // by ScrollBy as its starting position.
      mDestination = GetScrollPosition();
    }
  }

  if (!mUpdateScrollbarAttributes) {
    return false;
  }
  mUpdateScrollbarAttributes = false;

  // Update scrollbar attributes.
  if (mMayHaveDirtyFixedChildren) {
    mMayHaveDirtyFixedChildren = false;
    nsIFrame* parent = GetParent();
    if (parent->IsViewportFrame()) {
      for (nsIFrame* fixedChild :
           parent->GetChildList(FrameChildListID::Absolute)) {
        // force a reflow of the fixed child
        PresShell()->MarkPositionedFrameForReflow(fixedChild);
      }
    }
  }

  // Suppress handling of the curpos attribute changes we make here.
  NS_ASSERTION(!mFrameIsUpdatingScrollbar, "We shouldn't be reentering here");
  mFrameIsUpdatingScrollbar = true;

  // Note, in some cases this may get deleted while finishing reflow
  // for scrollbars. XXXmats is this still true now that we have a script
  // blocker in this scope? (if not, remove the weak frame checks below).
  if (mVScrollbarBox || mHScrollbarBox) {
    nsSize visualViewportSize = GetVisualViewportSize();
    nsRect scrollRange = GetVisualScrollRange();
    nsPoint scrollPos = GetScrollPosition();
    nsSize lineScrollAmount = GetLineScrollAmount();

    if (gfxPlatform::UseDesktopZoomingScrollbars()) {
      scrollRange = GetScrollRangeForUserInputEvents();
      scrollPos = GetVisualViewportOffset();
    }

    // If modifying the logic here, be sure to modify the corresponding
    // compositor-side calculation in ScrollThumbUtils::ApplyTransformForAxis().
    AutoWeakFrame weakFrame(this);
    if (mVScrollbarBox) {
      const double kScrollMultiplier =
          StaticPrefs::toolkit_scrollbox_verticalScrollDistance();
      nscoord increment = lineScrollAmount.height * kScrollMultiplier;
      // We normally use (visualViewportSize.height - increment) for height of
      // page scrolling.  However, it is too small when increment is very large.
      // (If increment is larger than visualViewportSize.height, direction of
      // scrolling will be opposite). To avoid it, we use
      // (float(visualViewportSize.height) * 0.8) as lower bound value of height
      // of page scrolling. (bug 383267)
      // XXX shouldn't we use GetPageScrollAmount here?
      nscoord pageincrement = nscoord(visualViewportSize.height - increment);
      nscoord pageincrementMin =
          nscoord(float(visualViewportSize.height) * 0.8);
      FinishReflowForScrollbar(mVScrollbarBox, scrollRange.y,
                               scrollRange.YMost(), scrollPos.y,
                               std::max(pageincrement, pageincrementMin));
    }
    if (mHScrollbarBox) {
      FinishReflowForScrollbar(mHScrollbarBox, scrollRange.x,
                               scrollRange.XMost(), scrollPos.x,
                               nscoord(float(visualViewportSize.width) * 0.8));
    }
    NS_ENSURE_TRUE(weakFrame.IsAlive(), false);
  }

  mFrameIsUpdatingScrollbar = false;
  // We used to rely on the curpos attribute changes above to scroll the
  // view.  However, for scrolling to the left of the viewport, we
  // rescale the curpos attribute, which means that operations like
  // resizing the window while it is scrolled all the way to the left
  // hold the curpos attribute constant at 0 while still requiring
  // scrolling.  So we suppress the effect of the changes above with
  // mFrameIsUpdatingScrollbar and call CurPosAttributeChanged here.
  // (It actually even works some of the time without this, thanks to
  // nsSliderFrame::AttributeChanged's handling of maxpos, but not when
  // we hide the scrollbar on a large size change, such as
  // maximization.)
  if (!mHScrollbarBox && !mVScrollbarBox) {
    return false;
  }
  ScrollbarCurPosChanged(doScroll);
  return doScroll;
}

void ScrollContainerFrame::ReflowCallbackCanceled() {
  mPostedReflowCallback = false;
}

bool ScrollContainerFrame::ComputeCustomOverflow(
    OverflowAreas& aOverflowAreas) {
  ScrollStyles ss = GetScrollStyles();

  // Reflow when the change in overflow leads to one of our scrollbars
  // changing or might require repositioning the scrolled content due to
  // reduced extents.
  nsRect scrolledRect = GetScrolledRect();
  ScrollDirections overflowChange =
      GetOverflowChange(scrolledRect, mPrevScrolledRect);
  mPrevScrolledRect = scrolledRect;

  bool needReflow = false;
  nsPoint scrollPosition = GetScrollPosition();
  if (overflowChange.contains(ScrollDirection::eHorizontal)) {
    if (ss.mHorizontal != StyleOverflow::Hidden || scrollPosition.x ||
        // If we are in minimum-scale size mode, we need to do a reflow to
        // re-compute the minimum-scale size.
        mIsUsingMinimumScaleSize) {
      needReflow = true;
    }
  }
  if (overflowChange.contains(ScrollDirection::eVertical)) {
    if (ss.mVertical != StyleOverflow::Hidden || scrollPosition.y) {
      needReflow = true;
    }
  }

  if (needReflow) {
    // If there are scrollbars, or we're not at the beginning of the pane,
    // the scroll position may change. In this case, mark the frame as
    // needing reflow. Don't use NS_FRAME_IS_DIRTY as dirty as that means
    // we have to reflow the frame and all its descendants, and we don't
    // have to do that here. Only this frame needs to be reflowed.
    PresShell()->FrameNeedsReflow(this, IntrinsicDirty::None,
                                  NS_FRAME_HAS_DIRTY_CHILDREN);
    // Ensure that next time ScrollContainerFrame::Reflow runs, we don't skip
    // updating the scrollbars. (Because the overflow area of the scrolled
    // frame has probably just been updated, Reflow won't see it change.)
    mSkippedScrollbarLayout = true;
    return false;  // reflowing will update overflow
  }
  PostOverflowEvent();
  return nsContainerFrame::ComputeCustomOverflow(aOverflowAreas);
}

void ScrollContainerFrame::UpdateSticky() {
  if (mStickyContainer) {
    mStickyContainer->UpdatePositions(GetScrollPosition(), this);
  }
}

void ScrollContainerFrame::UpdatePrevScrolledRect() {
  // The layout scroll range is determinated by the scrolled rect and the scroll
  // port, so if the scrolled rect is updated, we may have to schedule the
  // associated scroll-driven animations' restyles.
  nsRect currScrolledRect = GetScrolledRect();
  if (!currScrolledRect.IsEqualEdges(mPrevScrolledRect)) {
    mMayScheduleScrollAnimations = true;
  }
  mPrevScrolledRect = currScrolledRect;
}

void ScrollContainerFrame::AdjustScrollbarRectForResizer(
    nsIFrame* aFrame, nsPresContext* aPresContext, nsRect& aRect,
    bool aHasResizer, ScrollDirection aDirection) {
  if ((aDirection == ScrollDirection::eVertical ? aRect.width : aRect.height) ==
      0) {
    return;
  }

  // if a content resizer is present, use its size. Otherwise, check if the
  // widget has a resizer.
  nsRect resizerRect;
  if (aHasResizer) {
    resizerRect = mResizerBox->GetRect();
  } else {
    nsPoint offset;
    nsIWidget* widget = aFrame->GetNearestWidget(offset);
    LayoutDeviceIntRect widgetRect;
    if (!widget || !widget->ShowsResizeIndicator(&widgetRect)) {
      return;
    }

    resizerRect =
        nsRect(aPresContext->DevPixelsToAppUnits(widgetRect.x) - offset.x,
               aPresContext->DevPixelsToAppUnits(widgetRect.y) - offset.y,
               aPresContext->DevPixelsToAppUnits(widgetRect.width),
               aPresContext->DevPixelsToAppUnits(widgetRect.height));
  }

  if (resizerRect.Contains(aRect.BottomRight() - nsPoint(1, 1))) {
    switch (aDirection) {
      case ScrollDirection::eVertical:
        aRect.height = std::max(0, resizerRect.y - aRect.y);
        break;
      case ScrollDirection::eHorizontal:
        aRect.width = std::max(0, resizerRect.x - aRect.x);
        break;
    }
  } else if (resizerRect.Contains(aRect.BottomLeft() + nsPoint(1, -1))) {
    switch (aDirection) {
      case ScrollDirection::eVertical:
        aRect.height = std::max(0, resizerRect.y - aRect.y);
        break;
      case ScrollDirection::eHorizontal: {
        nscoord xmost = aRect.XMost();
        aRect.x = std::max(aRect.x, resizerRect.XMost());
        aRect.width = xmost - aRect.x;
        break;
      }
    }
  }
}

static void AdjustOverlappingScrollbars(nsRect& aVRect, nsRect& aHRect) {
  if (aVRect.IsEmpty() || aHRect.IsEmpty()) {
    return;
  }

  const nsRect oldVRect = aVRect;
  const nsRect oldHRect = aHRect;
  if (oldVRect.Contains(oldHRect.BottomRight() - nsPoint(1, 1))) {
    aHRect.width = std::max(0, oldVRect.x - oldHRect.x);
  } else if (oldVRect.Contains(oldHRect.BottomLeft() - nsPoint(0, 1))) {
    nscoord overlap = std::min(oldHRect.width, oldVRect.XMost() - oldHRect.x);
    aHRect.x += overlap;
    aHRect.width -= overlap;
  }
  if (oldHRect.Contains(oldVRect.BottomRight() - nsPoint(1, 1))) {
    aVRect.height = std::max(0, oldHRect.y - oldVRect.y);
  }
}

void ScrollContainerFrame::LayoutScrollbarPartAtRect(
    const ScrollReflowInput& aState, ReflowInput& aKidReflowInput,
    const nsRect& aRect) {
  nsPresContext* pc = PresContext();
  nsIFrame* kid = aKidReflowInput.mFrame;
  const auto wm = kid->GetWritingMode();
  ReflowOutput desiredSize(wm);
  MOZ_ASSERT(!wm.IsVertical(),
             "Scrollbar parts should have writing-mode: initial");
  MOZ_ASSERT(!wm.IsInlineReversed(),
             "Scrollbar parts should have writing-mode: initial");
  // XXX Maybe get a meaningful container size or something. Shouldn't matter
  // given our asserts above.
  const nsSize containerSize;
  aKidReflowInput.SetComputedISize(aRect.Width());
  aKidReflowInput.SetComputedBSize(aRect.Height());

  const LogicalPoint pos(wm, aRect.TopLeft(), containerSize);
  const auto flags = ReflowChildFlags::Default;
  nsReflowStatus status;
  ReflowOutput kidDesiredSize(wm);
  ReflowChild(kid, pc, kidDesiredSize, aKidReflowInput, wm, pos, containerSize,
              flags, status);
  FinishReflowChild(kid, pc, kidDesiredSize, &aKidReflowInput, wm, pos,
                    containerSize, flags);
}

void ScrollContainerFrame::LayoutButtonBox(const ScrollReflowInput& aState,
                                           nsIFrame* aButtonBox) {
  const auto wm = GetWritingMode();
  nsPresContext* pc = PresContext();

  // The button box is placed on the inline-end side, between the scrollport and
  // the inside-border edge, vertically centered.
  const nscoord buttonISize = aState.mButtonISize;
  const auto kidWM = aButtonBox->GetWritingMode();
  auto availSize =
      LogicalSize(wm, buttonISize, NS_UNCONSTRAINEDSIZE).ConvertTo(kidWM, wm);
  ReflowInput kidRI(pc, aState.mReflowInput, aButtonBox, availSize);
  ReflowOutput kidDesiredSize(kidRI);
  nsReflowStatus status;
  const nsSize containerSize = GetSize();
  ReflowChild(aButtonBox, pc, kidDesiredSize, kidRI, wm, LogicalPoint(wm),
              containerSize, ReflowChildFlags::Default, status);

  // Center the button in the block axis within the content box.
  LogicalRect contentBox(wm, mScrollPort, GetSize());
  contentBox.Deflate(wm, aState.KidPadding());
  const LogicalSize buttonSize =
      kidDesiredSize.Size(kidWM).ConvertTo(wm, kidWM);
  LogicalPoint pos = contentBox.Origin(wm);
  pos.I(wm) += contentBox.ISize(wm);
  pos.B(wm) += (contentBox.BSize(wm) - buttonSize.BSize(wm)) / 2;
  FinishReflowChild(aButtonBox, pc, kidDesiredSize, &kidRI, wm, pos,
                    containerSize, ReflowChildFlags::Default);
}

void ScrollContainerFrame::LayoutScrollbars(ScrollReflowInput& aState,
                                            const nsRect& aInsideBorderArea,
                                            const nsRect& aOldScrollPort) {
  const bool scrollbarOnLeft = !IsScrollbarOnRight();
  const bool overlayScrollbars = UseOverlayScrollbars();
  const bool overlayScrollBarsOnRoot = overlayScrollbars && mIsRoot;
  const bool showVScrollbar = mVScrollbarBox && mHasVerticalScrollbar;
  const bool showHScrollbar = mHScrollbarBox && mHasHorizontalScrollbar;

  nsSize compositionSize = mScrollPort.Size();
  if (overlayScrollBarsOnRoot) {
    compositionSize = nsLayoutUtils::CalculateCompositionSizeForFrame(
        this, false, &compositionSize);
  }

  nsPresContext* presContext = mScrolledFrame->PresContext();
  nsRect vRect;
  if (showVScrollbar) {
    vRect.height =
        overlayScrollBarsOnRoot ? compositionSize.height : mScrollPort.height;
    vRect.y = mScrollPort.y;
    if (scrollbarOnLeft) {
      vRect.width = mScrollPort.x - aInsideBorderArea.x;
      vRect.x = aInsideBorderArea.x;
    } else {
      vRect.width = aInsideBorderArea.XMost() - mScrollPort.XMost();
      vRect.x = mScrollPort.x + compositionSize.width;
    }
    if (overlayScrollbars || mOnlyNeedVScrollbarToScrollVVInsideLV) {
      const nscoord width = aState.VScrollbarPrefWidth();
      // There is no space reserved for the layout scrollbar, it is currently
      // not visible because it is positioned just outside the scrollport. But
      // we know that it needs to be made visible so we shift it back in.
      vRect.width += width;
      if (!scrollbarOnLeft) {
        vRect.x -= width;
      }
    }
  }

  nsRect hRect;
  if (showHScrollbar) {
    hRect.width =
        overlayScrollBarsOnRoot ? compositionSize.width : mScrollPort.width;
    hRect.x = mScrollPort.x;
    hRect.height = aInsideBorderArea.YMost() - mScrollPort.YMost();
    hRect.y = mScrollPort.y + compositionSize.height;

    if (overlayScrollbars || mOnlyNeedHScrollbarToScrollVVInsideLV) {
      const nscoord height = aState.HScrollbarPrefHeight();
      hRect.height += height;
      // There is no space reserved for the layout scrollbar, it is currently
      // not visible because it is positioned just outside the scrollport. But
      // we know that it needs to be made visible so we shift it back in.
      hRect.y -= height;
    }
  }

  const bool hasVisualOnlyScrollbarsOnBothDirections =
      !overlayScrollbars && showHScrollbar &&
      mOnlyNeedHScrollbarToScrollVVInsideLV && showVScrollbar &&
      mOnlyNeedVScrollbarToScrollVVInsideLV;
  nsPresContext* pc = PresContext();

  // place the scrollcorner
  if (mScrollCornerBox) {
    nsRect r(0, 0, 0, 0);
    if (scrollbarOnLeft) {
      // scrollbar (if any) on left
      r.width = showVScrollbar ? mScrollPort.x - aInsideBorderArea.x : 0;
      r.x = aInsideBorderArea.x;
    } else {
      // scrollbar (if any) on right
      r.width =
          showVScrollbar ? aInsideBorderArea.XMost() - mScrollPort.XMost() : 0;
      r.x = aInsideBorderArea.XMost() - r.width;
    }
    NS_ASSERTION(r.width >= 0, "Scroll area should be inside client rect");

    if (showHScrollbar) {
      // scrollbar (if any) on bottom
      // Note we don't support the horizontal scrollbar at the top side.
      r.height = aInsideBorderArea.YMost() - mScrollPort.YMost();
      NS_ASSERTION(r.height >= 0, "Scroll area should be inside client rect");
    }
    r.y = aInsideBorderArea.YMost() - r.height;

    // If we have layout scrollbars and both scrollbars are present and both are
    // only needed to scroll the VV inside the LV then we need a scrollcorner
    // but the above calculation will result in an empty rect, so adjust it.
    if (r.IsEmpty() && hasVisualOnlyScrollbarsOnBothDirections) {
      r.width = vRect.width;
      r.height = hRect.height;
      r.x = scrollbarOnLeft ? mScrollPort.x : mScrollPort.XMost() - r.width;
      r.y = mScrollPort.YMost() - r.height;
    }

    ReflowInput scrollCornerRI(
        pc, aState.mReflowInput, mScrollCornerBox,
        LogicalSize(mScrollCornerBox->GetWritingMode(), r.Size()));
    LayoutScrollbarPartAtRect(aState, scrollCornerRI, r);
  }

  if (mResizerBox) {
    // If a resizer is present, get its size.
    //
    // TODO(emilio): Should this really account for scrollbar-width?
    auto scrollbarWidth = ScrollbarWidth();
    const nscoord scrollbarSize =
        GetNonOverlayScrollbarSize(pc, scrollbarWidth);
    ReflowInput resizerRI(pc, aState.mReflowInput, mResizerBox,
                          LogicalSize(mResizerBox->GetWritingMode()));
    nsSize resizerMinSize = {resizerRI.ComputedMinWidth(),
                             resizerRI.ComputedMinHeight()};

    nsRect r;
    r.width = std::max(std::max(r.width, scrollbarSize), resizerMinSize.width);
    r.x = scrollbarOnLeft ? aInsideBorderArea.x
                          : aInsideBorderArea.XMost() - r.width;
    r.height =
        std::max(std::max(r.height, scrollbarSize), resizerMinSize.height);
    r.y = aInsideBorderArea.YMost() - r.height;

    LayoutScrollbarPartAtRect(aState, resizerRI, r);
  }

  // Note that AdjustScrollbarRectForResizer has to be called after the
  // resizer has been laid out immediately above this because it gets the rect
  // of the resizer frame.
  if (mVScrollbarBox) {
    AdjustScrollbarRectForResizer(this, presContext, vRect, mResizerBox,
                                  ScrollDirection::eVertical);
  }
  if (mHScrollbarBox) {
    AdjustScrollbarRectForResizer(this, presContext, hRect, mResizerBox,
                                  ScrollDirection::eHorizontal);
  }

  // Layout scrollbars can overlap at this point if they are both present and
  // both only needed to scroll the VV inside the LV.
  if (!LookAndFeel::GetInt(LookAndFeel::IntID::AllowOverlayScrollbarsOverlap) ||
      hasVisualOnlyScrollbarsOnBothDirections) {
    AdjustOverlappingScrollbars(vRect, hRect);
  }
  if (mVScrollbarBox) {
    ReflowInput vScrollbarRI(
        pc, aState.mReflowInput, mVScrollbarBox,
        LogicalSize(mVScrollbarBox->GetWritingMode(), vRect.Size()));
    LayoutScrollbarPartAtRect(aState, vScrollbarRI, vRect);
  }
  if (mHScrollbarBox) {
    ReflowInput hScrollbarRI(
        pc, aState.mReflowInput, mHScrollbarBox,
        LogicalSize(mHScrollbarBox->GetWritingMode(), hRect.Size()));
    LayoutScrollbarPartAtRect(aState, hScrollbarRI, hRect);
  }

  // may need to update fixed position children of the viewport,
  // if the client area changed size because of an incremental
  // reflow of a descendant.  (If the outer frame is dirty, the fixed
  // children will be re-laid out anyway)
  if (aOldScrollPort.Size() != mScrollPort.Size() &&
      !HasAnyStateBits(NS_FRAME_IS_DIRTY) && mIsRoot) {
    mMayHaveDirtyFixedChildren = true;
  }

  // post reflow callback to modify scrollbar attributes
  mUpdateScrollbarAttributes = true;
  if (!mPostedReflowCallback) {
    PresShell()->PostReflowCallback(this);
    mPostedReflowCallback = true;
  }
}

static void ReduceRadii(nscoord aXBorder, nscoord aYBorder, nsSize& aRadius) {
  // In order to ensure that the inside edge of the border has no
  // curvature, we need at least one of its radii to be zero.
  if (aRadius.width <= aXBorder || aRadius.height <= aYBorder) {
    return;
  }

  // For any corner where we reduce the radii, preserve the corner's shape.
  double ratio = std::max(double(aXBorder) / aRadius.width,
                          double(aYBorder) / aRadius.height);
  aRadius.width *= ratio;
  aRadius.height *= ratio;
}

/**
 * Implement an override for nsIFrame::GetBorderRadii to ensure that
 * the clipping region for the border radius does not clip the scrollbars.
 *
 * In other words, we require that the border radius be reduced until the
 * inner border radius at the inner edge of the border is 0 wherever we
 * have scrollbars.
 */
bool ScrollContainerFrame::GetBorderRadii(const nsSize& aFrameSize,
                                          const nsSize& aBorderArea,
                                          Sides aSkipSides,
                                          nsRectCornerRadii& aRadii) const {
  if (!nsContainerFrame::GetBorderRadii(aFrameSize, aBorderArea, aSkipSides,
                                        aRadii)) {
    return false;
  }

  // Since we can use GetActualScrollbarSizes (rather than
  // GetDesiredScrollbarSizes) since this doesn't affect reflow, we
  // probably should.
  nsMargin sb = GetActualScrollbarSizes();
  nsMargin border = GetUsedBorder();

  if (sb.left > 0 || sb.top > 0) {
    ReduceRadii(border.left, border.top, aRadii.TopLeft());
  }
  if (sb.top > 0 || sb.right > 0) {
    ReduceRadii(border.right, border.top, aRadii.TopRight());
  }
  if (sb.right > 0 || sb.bottom > 0) {
    ReduceRadii(border.right, border.bottom, aRadii.BottomRight());
  }
  if (sb.bottom > 0 || sb.left > 0) {
    ReduceRadii(border.left, border.bottom, aRadii.BottomLeft());
  }
  return true;
}

static nscoord SnapCoord(nscoord aCoord, double aRes,
                         nscoord aAppUnitsPerPixel) {
  if (StaticPrefs::layout_disable_pixel_alignment()) {
    return aCoord;
  }
  double snappedToLayerPixels = NS_round((aRes * aCoord) / aAppUnitsPerPixel);
  return NSToCoordRoundWithClamp(snappedToLayerPixels * aAppUnitsPerPixel /
                                 aRes);
}

nsRect ScrollContainerFrame::GetScrolledRect() const {
  nsRect result = GetUnsnappedScrolledRectInternal(
      mScrolledFrame->ScrollableOverflowRect(), mScrollPort.Size());

#if 0
  // This happens often enough.
  if (result.width < mScrollPort.width || result.height < mScrollPort.height) {
    NS_WARNING("Scrolled rect smaller than scrollport?");
  }
#endif

  // Expand / contract the result by up to half a layer pixel so that scrolling
  // to the right / bottom edge does not change the layer pixel alignment of
  // the scrolled contents.

  if (result.x == 0 && result.y == 0 && result.width == mScrollPort.width &&
      result.height == mScrollPort.height) {
    // The edges that we would snap are already aligned with the scroll port,
    // so we can skip all the work below.
    return result;
  }

  // For that, we first convert the scroll port and the scrolled rect to rects
  // relative to the reference frame, since that's the space where painting does
  // snapping.
  nsSize visualViewportSize = GetVisualViewportSize();
  const nsIFrame* referenceFrame =
      mReferenceFrameDuringPainting
          ? mReferenceFrameDuringPainting
          : nsLayoutUtils::GetReferenceFrame(
                const_cast<ScrollContainerFrame*>(this));
  nsPoint toReferenceFrame = GetOffsetToCrossDoc(referenceFrame);
  nsRect scrollPort(mScrollPort.TopLeft() + toReferenceFrame,
                    visualViewportSize);
  nsRect scrolledRect = result + scrollPort.TopLeft();

  if (scrollPort.Overflows() || scrolledRect.Overflows()) {
    return result;
  }

  // Now, snap the bottom right corner of both of these rects.
  // We snap to layer pixels, so we need to respect the layer's scale.
  nscoord appUnitsPerDevPixel =
      mScrolledFrame->PresContext()->AppUnitsPerDevPixel();
  MatrixScales scale = GetPaintedLayerScaleForFrame(
      mScrolledFrame, /* aIncludeCSSTransform = */ false);
  if (scale.xScale == 0 || scale.yScale == 0) {
    scale = MatrixScales();
  }

  // Compute bounds for the scroll position, and computed the snapped scrolled
  // rect from the scroll position bounds.
  nscoord snappedScrolledAreaBottom =
      SnapCoord(scrolledRect.YMost(), scale.yScale, appUnitsPerDevPixel);
  nscoord snappedScrollPortBottom =
      SnapCoord(scrollPort.YMost(), scale.yScale, appUnitsPerDevPixel);
  nscoord maximumScrollOffsetY =
      snappedScrolledAreaBottom - snappedScrollPortBottom;
  result.SetBottomEdge(scrollPort.height + maximumScrollOffsetY);

  if (GetScrolledFrameDir() == StyleDirection::Ltr) {
    nscoord snappedScrolledAreaRight =
        SnapCoord(scrolledRect.XMost(), scale.xScale, appUnitsPerDevPixel);
    nscoord snappedScrollPortRight =
        SnapCoord(scrollPort.XMost(), scale.xScale, appUnitsPerDevPixel);
    nscoord maximumScrollOffsetX =
        snappedScrolledAreaRight - snappedScrollPortRight;
    result.SetRightEdge(scrollPort.width + maximumScrollOffsetX);
  } else {
    // In RTL, the scrolled area's right edge is at scrollPort.XMost(),
    // and the scrolled area's x position is zero or negative. We want
    // the right edge to stay flush with the scroll port, so we snap the
    // left edge.
    nscoord snappedScrolledAreaLeft =
        SnapCoord(scrolledRect.x, scale.xScale, appUnitsPerDevPixel);
    nscoord snappedScrollPortLeft =
        SnapCoord(scrollPort.x, scale.xScale, appUnitsPerDevPixel);
    nscoord minimumScrollOffsetX =
        snappedScrolledAreaLeft - snappedScrollPortLeft;
    result.SetLeftEdge(minimumScrollOffsetX);
  }

  return result;
}

nsRect ScrollContainerFrame::GetScrollPortRectAccountingForMaxDynamicToolbar()
    const {
  auto rect = mScrollPort;
  if (mIsRoot && PresContext()->HasDynamicToolbar()) {
    rect.SizeTo(nsLayoutUtils::ExpandHeightForDynamicToolbar(PresContext(),
                                                             rect.Size()));
  }
  return rect;
}

StyleDirection ScrollContainerFrame::GetScrolledFrameDir() const {
  return GetScrolledFrameDir(mScrolledFrame, IsTextInputFrame());
}

StyleDirection ScrollContainerFrame::GetScrolledFrameDir(
    const nsIFrame* aScrolledFrame, bool aForTextInput) {
  // If the scrolled frame has unicode-bidi: plaintext, the paragraph
  // direction set by the text content overrides the direction of the frame
  if (aScrolledFrame->StyleTextReset()->mUnicodeBidi ==
      StyleUnicodeBidi::Plaintext) {
    if (aForTextInput) {
      // HACK: We rely on inputs only overflowing in one direction, so we scroll
      // in whichever direction the input overflows. To be a bit resilient we
      // just use whichever scroll direction would be larger.
      // TODO(emilio): Remove once the check below is subtler.
      auto sr = aScrolledFrame->ScrollableOverflowRectRelativeToSelf();
      auto leftOverflow = -sr.x;
      auto rightOverflow = sr.XMost() - aScrolledFrame->GetRect().Width();
      return leftOverflow > rightOverflow ? StyleDirection::Rtl
                                          : StyleDirection::Ltr;
    }
    // TODO(emilio): This check is rather simplistic, see
    // https://github.com/w3c/csswg-drafts/issues/13816
    if (nsIFrame* child = aScrolledFrame->PrincipalChildList().FirstChild()) {
      return nsBidiPresUtils::ParagraphDirection(child) ==
                     intl::BidiDirection::LTR
                 ? StyleDirection::Ltr
                 : StyleDirection::Rtl;
    }
  }
  return aScrolledFrame->GetWritingMode().IsBidiLTR() ? StyleDirection::Ltr
                                                      : StyleDirection::Rtl;
}

auto ScrollContainerFrame::ComputePerAxisScrollDirections(
    const nsIFrame* aScrolledFrame, bool aForTextInput)
    -> PerAxisScrollDirections {
  auto wm = aScrolledFrame->GetWritingMode();
  auto dir = GetScrolledFrameDir(aScrolledFrame, aForTextInput);
  wm.SetDirectionFromBidiLevel(dir == StyleDirection::Rtl
                                   ? intl::BidiEmbeddingLevel::RTL()
                                   : intl::BidiEmbeddingLevel::LTR());
  bool scrollToRight = wm.IsPhysicalLTR();
  bool scrollToBottom =
      !wm.IsVertical() || wm.GetInlineDir() == WritingMode::InlineDir::TTB;
  if (aScrolledFrame->IsFlexContainerFrame()) {
    // In a flex container, the children flow (and overflow) along the flex
    // container's main axis and cross axis. These are analogous to the
    // inline/block axes, and by default they correspond exactly to those axes;
    // but the flex container's CSS (e.g. flex-direction: column-reverse) may
    // have swapped and/or reversed them, and we need to account for that here.
    const FlexboxAxisInfo info(aScrolledFrame);
    const bool isMainAxisVertical = info.mIsRowOriented == wm.IsVertical();
    if (info.mIsMainAxisReversed) {
      if (isMainAxisVertical) {
        scrollToBottom = !scrollToBottom;
      } else {
        scrollToRight = !scrollToRight;
      }
    }
    if (info.mIsCrossAxisReversed) {
      if (isMainAxisVertical) {
        scrollToRight = !scrollToRight;
      } else {
        scrollToBottom = !scrollToBottom;
      }
    }
  }
  return {scrollToRight, scrollToBottom};
}

nsRect ScrollContainerFrame::GetUnsnappedScrolledRectInternal(
    const nsRect& aScrolledOverflowArea, const nsSize& aScrollPortSize) const {
  nscoord x1 = aScrolledOverflowArea.x, x2 = aScrolledOverflowArea.XMost(),
          y1 = aScrolledOverflowArea.y, y2 = aScrolledOverflowArea.YMost();
  auto dirs = ComputePerAxisScrollDirections();
  // Clamp the horizontal start-edge (x1 or x2, depending whether the logical
  // axis that corresponds to horizontal progresses from left-to-right or
  // right-to-left).
  if (dirs.mToRight) {
    if (x1 < 0) {
      x1 = 0;
    }
  } else {
    if (x2 > aScrollPortSize.width) {
      x2 = aScrollPortSize.width;
    }
    // When the scrolled frame chooses a size larger than its available width
    // (because its padding alone is larger than the available width), we need
    // to keep the start-edge of the scroll frame anchored to the start-edge of
    // the scrollport.
    // When the scrolled frame is RTL, this means moving it in our left-based
    // coordinate system, so we need to compensate for its extra width here by
    // effectively repositioning the frame.
    nscoord extraWidth =
        std::max(0, mScrolledFrame->GetSize().width - aScrollPortSize.width);
    x2 += extraWidth;
  }

  // Similarly, clamp the vertical start-edge (y1 or y2, depending whether the
  // logical axis that corresponds to vertical progresses from top-to-bottom or
  // buttom-to-top).
  if (dirs.mToBottom) {
    if (y1 < 0) {
      y1 = 0;
    }
  } else {
    if (y2 > aScrollPortSize.height) {
      y2 = aScrollPortSize.height;
    }
    nscoord extraHeight =
        std::max(0, mScrolledFrame->GetSize().height - aScrollPortSize.height);
    y2 += extraHeight;
  }

  return nsRect(x1, y1, x2 - x1, y2 - y1);
}

nsMargin ScrollContainerFrame::GetActualScrollbarSizes(
    ScrollbarSizesOptions aOptions /* = ScrollbarSizesOptions::NONE */) const {
  if (IsSingleLineTextInput(this)) {
    // The assumption below that the scrollport usually starts in the padding
    // rect doesn't hold for single line text inputs. Luckily we know those have
    // no scrollbars tho.
    return {};
  }

  nsRect r = GetPaddingRectRelativeToSelf();
  nsMargin m(mScrollPort.y - r.y, r.XMost() - mScrollPort.XMost(),
             r.YMost() - mScrollPort.YMost(), mScrollPort.x - r.x);

  if (aOptions == ScrollbarSizesOptions::INCLUDE_VISUAL_VIEWPORT_SCROLLBARS &&
      !UseOverlayScrollbars()) {
    // If we are using layout scrollbars and they only exist to scroll the
    // visual viewport then they do not take up any layout space (so the
    // scrollport is the same as the padding rect) but they do cover everything
    // below them so some callers may want to include this special type of
    // scrollbars in the returned value.
    if (mHScrollbarBox && mHasHorizontalScrollbar &&
        mOnlyNeedHScrollbarToScrollVVInsideLV) {
      m.bottom += mHScrollbarBox->GetRect().height;
    }
    if (mVScrollbarBox && mHasVerticalScrollbar &&
        mOnlyNeedVScrollbarToScrollVVInsideLV) {
      if (IsScrollbarOnRight()) {
        m.right += mVScrollbarBox->GetRect().width;
      } else {
        m.left += mVScrollbarBox->GetRect().width;
      }
    }
  }

  return m;
}

void ScrollContainerFrame::SetScrollbarVisibility(nsIFrame* aScrollbar,
                                                  bool aVisible) {
  nsScrollbarFrame* scrollbar = do_QueryFrame(aScrollbar);
  if (scrollbar) {
    // See if we have a mediator.
    nsIScrollbarMediator* mediator = scrollbar->GetScrollbarMediator();
    if (mediator) {
      // Inform the mediator of the visibility change.
      mediator->VisibilityChanged(aVisible);
    }
  }
}

bool ScrollContainerFrame::IsLastScrollUpdateAnimating() const {
  if (!mScrollUpdates.IsEmpty()) {
    switch (mScrollUpdates.LastElement().GetMode()) {
      case ScrollMode::Smooth:
      case ScrollMode::SmoothMsd:
        return true;
      case ScrollMode::Instant:
      case ScrollMode::Normal:
        break;
    }
  }
  return false;
}

bool ScrollContainerFrame::IsLastScrollUpdateTriggeredByScriptAnimating()
    const {
  if (!mScrollUpdates.IsEmpty()) {
    const ScrollPositionUpdate& lastUpdate = mScrollUpdates.LastElement();
    if (lastUpdate.WasTriggeredByScript() &&
        (mScrollUpdates.LastElement().GetMode() == ScrollMode::Smooth ||
         mScrollUpdates.LastElement().GetMode() == ScrollMode::SmoothMsd)) {
      return true;
    }
  }
  return false;
}

EnumSet<ScrollContainerFrame::AnimationState>
ScrollContainerFrame::ScrollAnimationState() const {
  EnumSet<AnimationState> retval;
  if (IsApzAnimationInProgress()) {
    retval += AnimationState::APZInProgress;
    if (mCurrentAPZScrollAnimationType ==
        APZScrollAnimationType::TriggeredByScript) {
      retval += AnimationState::TriggeredByScript;
    }
  }

  if (mApzAnimationRequested) {
    retval += AnimationState::APZRequested;
    if (mApzAnimationTriggeredByScriptRequested) {
      retval += AnimationState::TriggeredByScript;
    }
  }

  if (IsLastScrollUpdateAnimating()) {
    retval += AnimationState::APZPending;
    if (IsLastScrollUpdateTriggeredByScriptAnimating()) {
      retval += AnimationState::TriggeredByScript;
    }
  }
  if (mAsyncScroll) {
    retval += AnimationState::MainThread;
    if (mAsyncScroll->WasTriggeredByScript()) {
      retval += AnimationState::TriggeredByScript;
    }
  }

  if (mAsyncSmoothMSDScroll) {
    retval += AnimationState::MainThread;
    if (mAsyncSmoothMSDScroll->WasTriggeredByScript()) {
      retval += AnimationState::TriggeredByScript;
    }
  }
  return retval;
}

void ScrollContainerFrame::ResetScrollInfoIfNeeded(
    const MainThreadScrollGeneration& aGeneration,
    const APZScrollGeneration& aGenerationOnApz,
    APZScrollAnimationType aAPZScrollAnimationType,
    InScrollingGesture aInScrollingGesture) {
  if (aGeneration == mScrollGeneration) {
    mLastScrollOrigin = ScrollOrigin::None;
    mApzAnimationRequested = false;
    mApzAnimationTriggeredByScriptRequested = false;
  }

  mScrollGenerationOnApz = aGenerationOnApz;
  // We can reset this regardless of scroll generation, as this is only set
  // here, as a response to APZ requesting a repaint.
  mCurrentAPZScrollAnimationType = aAPZScrollAnimationType;

  mInScrollingGesture = aInScrollingGesture;
}

UniquePtr<PresState> ScrollContainerFrame::SaveState() {
  nsIScrollbarMediator* mediator = do_QueryFrame(GetScrolledFrame());
  if (mediator) {
    // child handles its own scroll state, so don't bother saving state here
    return nullptr;
  }

  // Don't store a scroll state if we never have been scrolled or restored
  // a previous scroll state, and we're not in the middle of a smooth scroll.
  auto scrollAnimationState = ScrollAnimationState();
  bool isScrollAnimating =
      scrollAnimationState.contains(AnimationState::MainThread) ||
      scrollAnimationState.contains(AnimationState::APZPending) ||
      scrollAnimationState.contains(AnimationState::APZRequested);
  if (!mHasBeenScrolled && !mDidHistoryRestore && !isScrollAnimating) {
    return nullptr;
  }

  UniquePtr<PresState> state = NewPresState();
  bool allowScrollOriginDowngrade =
      !nsLayoutUtils::CanScrollOriginClobberApz(mLastScrollOrigin) ||
      mAllowScrollOriginDowngrade;
  // Save mRestorePos instead of our actual current scroll position, if it's
  // valid and we haven't moved since the last update of mLastPos (same check
  // that ScrollToRestoredPosition uses). This ensures if a reframe occurs
  // while we're in the process of loading content to scroll to a restored
  // position, we'll keep trying after the reframe. Similarly, if we're in the
  // middle of a smooth scroll, store the destination so that when we restore
  // we'll jump straight to the end of the scroll animation, rather than
  // effectively dropping it. Note that the mRestorePos will override the
  // smooth scroll destination if both are present.
  nsPoint pt = GetLogicalVisualViewportOffset();
  if (isScrollAnimating) {
    pt = mDestination;
    allowScrollOriginDowngrade = false;
  }
  SCROLLRESTORE_LOG("%p: SaveState, pt=%s, mLastPos=%s, mRestorePos=%s\n", this,
                    ToString(pt).c_str(), ToString(mLastPos).c_str(),
                    ToString(mRestorePos).c_str());
  if (mRestorePos.y != -1 && pt == mLastPos) {
    pt = mRestorePos;
  }
  state->scrollState() = pt;
  state->allowScrollOriginDowngrade() = allowScrollOriginDowngrade;
  if (mIsRoot) {
    // Only save resolution properties for root scroll frames
    state->resolution() = PresShell()->GetResolution();
  }
  return state;
}

NS_IMETHODIMP ScrollContainerFrame::RestoreState(PresState* aState) {
  mRestorePos = aState->scrollState();
  MOZ_ASSERT(mLastScrollOrigin == ScrollOrigin::None);
  mAllowScrollOriginDowngrade = aState->allowScrollOriginDowngrade();
  // When restoring state, we promote mLastScrollOrigin to a stronger value
  // from the default of eNone, to restore the behaviour that existed when
  // the state was saved. If mLastScrollOrigin was a weaker value previously,
  // then mAllowScrollOriginDowngrade will be true, and so the combination of
  // mAllowScrollOriginDowngrade and the stronger mLastScrollOrigin will allow
  // the same types of scrolls as before. It might be possible to also just
  // save and restore the mAllowScrollOriginDowngrade and mLastScrollOrigin
  // values directly without this sort of fiddling. Something to try in the
  // future or if we tinker with this code more.
  mLastScrollOrigin = ScrollOrigin::Other;
  mDidHistoryRestore = true;
  mLastPos = mScrolledFrame ? GetLogicalVisualViewportOffset() : nsPoint(0, 0);
  SCROLLRESTORE_LOG("%p: RestoreState, set mRestorePos=%s mLastPos=%s\n", this,
                    ToString(mRestorePos).c_str(), ToString(mLastPos).c_str());

  // Resolution properties should only exist on root scroll frames.
  MOZ_ASSERT(mIsRoot || aState->resolution() == 1.0);

  if (mIsRoot) {
    PresShell()->SetResolutionAndScaleTo(
        aState->resolution(), ResolutionChangeOrigin::MainThreadRestore);
  }
  return NS_OK;
}

void ScrollContainerFrame::PostScrolledAreaEvent() {
  if (mScrolledAreaEvent.IsPending()) {
    return;
  }
  mScrolledAreaEvent = MakeRefPtr<ScrolledAreaEvent>(this);
  nsContentUtils::AddScriptRunner(mScrolledAreaEvent.get());
}

////////////////////////////////////////////////////////////////////////////////
// ScrolledArea change event dispatch

// TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230, bug 1535398)
MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
ScrollContainerFrame::ScrolledAreaEvent::Run() {
  if (mHelper) {
    mHelper->FireScrolledAreaEvent();
  }
  return NS_OK;
}

void ScrollContainerFrame::FireScrolledAreaEvent() {
  mScrolledAreaEvent.Forget();

  InternalScrollAreaEvent event(true, eScrolledAreaChanged, nullptr);
  RefPtr<nsPresContext> presContext = PresContext();
  nsIContent* content = GetContent();

  event.mArea = mScrolledFrame->ScrollableOverflowRectRelativeToParent();
  if (RefPtr<Document> doc = content->GetUncomposedDoc()) {
    EventDispatcher::Dispatch(doc, presContext, &event, nullptr);
  }
}

ScrollDirections ScrollContainerFrame::GetAvailableScrollingDirections() const {
  nscoord oneDevPixel =
      GetScrolledFrame()->PresContext()->AppUnitsPerDevPixel();
  ScrollDirections directions;
  nsRect scrollRange = GetScrollRange();
  if (scrollRange.width >= oneDevPixel) {
    directions += ScrollDirection::eHorizontal;
  }
  if (scrollRange.height >= oneDevPixel) {
    directions += ScrollDirection::eVertical;
  }
  return directions;
}

nsRect ScrollContainerFrame::GetScrollRangeForUserInputEvents() const {
  // This function computes a scroll range based on a scrolled rect and scroll
  // port defined as follows:
  //   scrollable rect = overflow:hidden ? layout viewport : scrollable rect
  //   scroll port = have visual viewport ? visual viewport : layout viewport
  // The results in the same notion of scroll range that APZ uses (the combined
  // effect of FrameMetrics::CalculateScrollRange() and
  // nsLayoutUtils::CalculateScrollableRectForFrame).

  ScrollStyles ss = GetScrollStyles();

  nsPoint scrollPos = GetScrollPosition();

  nsRect scrolledRect = GetScrolledRect();
  if (StyleOverflow::Hidden == ss.mHorizontal) {
    scrolledRect.width = mScrollPort.width;
    scrolledRect.x = scrollPos.x;
  }
  if (StyleOverflow::Hidden == ss.mVertical) {
    scrolledRect.height = mScrollPort.height;
    scrolledRect.y = scrollPos.y;
  }

  nsSize scrollPort = GetVisualViewportSize();

  nsRect scrollRange = scrolledRect;
  scrollRange.width = std::max(scrolledRect.width - scrollPort.width, 0);
  scrollRange.height = std::max(scrolledRect.height - scrollPort.height, 0);

  return scrollRange;
}

ScrollDirections
ScrollContainerFrame::GetAvailableScrollingDirectionsForUserInputEvents()
    const {
  nsRect scrollRange = GetScrollRangeForUserInputEvents();

  // We check if there is at least one half of a screen pixel of scroll range to
  // roughly match what apz does when it checks if the change in scroll position
  // in screen pixels round to zero or not.
  // (https://searchfox.org/mozilla-central/rev/2f09184ec781a2667feec87499d4b81b32b6c48e/gfx/layers/apz/src/AsyncPanZoomController.cpp#3210)
  // This isn't quite half a screen pixel, it doesn't take into account CSS
  // transforms, but should be good enough.
  float halfScreenPixel =
      GetScrolledFrame()->PresContext()->AppUnitsPerDevPixel() /
      (PresShell()->GetCumulativeResolution() * 2.f);
  ScrollDirections directions;
  if (scrollRange.width >= halfScreenPixel) {
    directions += ScrollDirection::eHorizontal;
  }
  if (scrollRange.height >= halfScreenPixel) {
    directions += ScrollDirection::eVertical;
  }
  return directions;
}

/**
 * Append scroll positions for valid snap positions into |aSnapInfo| if
 * applicable.
 */
static void AppendScrollPositionsForSnap(
    const nsIFrame* aFrame, const nsIFrame* aScrolledFrame,
    const nsRect& aScrolledRect, const nsMargin& aScrollPadding,
    const nsRect& aScrollRange, WritingMode aWritingModeOnScroller,
    ScrollSnapInfo& aSnapInfo,
    ScrollContainerFrame::SnapTargetSet* aSnapTargets) {
  ScrollSnapTargetId targetId = ScrollSnapUtils::GetTargetIdFor(aFrame);

  nsRect snapArea =
      ScrollSnapUtils::GetSnapAreaFor(aFrame, aScrolledFrame, aScrolledRect);
  // Use the writing-mode on the target element if the snap area is larger than
  // the snapport.
  // https://drafts.csswg.org/css-scroll-snap/#snap-scope
  WritingMode writingMode = ScrollSnapUtils::NeedsToRespectTargetWritingMode(
                                snapArea.Size(), aSnapInfo.mSnapportSize)
                                ? aFrame->GetWritingMode()
                                : aWritingModeOnScroller;

  // These snap range shouldn't be involved with scroll-margin since we just
  // need the visible range of the target element.
  if (snapArea.width > aSnapInfo.mSnapportSize.width) {
    aSnapInfo.mXRangeWiderThanSnapport.AppendElement(
        ScrollSnapInfo::ScrollSnapRange(snapArea, ScrollDirection::eHorizontal,
                                        targetId));
  }
  if (snapArea.height > aSnapInfo.mSnapportSize.height) {
    aSnapInfo.mYRangeWiderThanSnapport.AppendElement(
        ScrollSnapInfo::ScrollSnapRange(snapArea, ScrollDirection::eVertical,
                                        targetId));
  }

  // Shift target rect position by the scroll padding to get the padded
  // position thus we don't need to take account scroll-padding values in
  // ScrollSnapUtils::GetSnapPointForDestination() when it gets called from
  // the compositor thread.
  snapArea.y -= aScrollPadding.top;
  snapArea.x -= aScrollPadding.left;

  LogicalRect logicalTargetRect(writingMode, snapArea, aSnapInfo.mSnapportSize);
  LogicalSize logicalSnapportRect(writingMode, aSnapInfo.mSnapportSize);
  LogicalRect logicalScrollRange(aWritingModeOnScroller, aScrollRange,
                                 // The origin of this logical coordinate system
                                 // what we need here is (0, 0), so we use an
                                 // empty size.
                                 nsSize());
  logicalScrollRange = logicalScrollRange.ConvertTo(
      writingMode, aWritingModeOnScroller, nsSize());

  Maybe<nscoord> blockDirectionPosition;
  const nsStyleDisplay* styleDisplay = aFrame->StyleDisplay();
  nscoord containerBSize = logicalSnapportRect.BSize(writingMode);
  switch (styleDisplay->mScrollSnapAlign.block) {
    case StyleScrollSnapAlignKeyword::None:
      break;
    case StyleScrollSnapAlignKeyword::Start: {
      nscoord candidate = std::clamp(logicalTargetRect.BStart(writingMode),
                                     logicalScrollRange.BStart(writingMode),
                                     logicalScrollRange.BEnd(writingMode));
      blockDirectionPosition.emplace(writingMode.IsVerticalRL() ? -candidate
                                                                : candidate);
      break;
    }
    case StyleScrollSnapAlignKeyword::End: {
      nscoord candidate = std::clamp(
          // What we need here is the scroll position instead of the snap
          // position itself, so we need, for example, the top edge of the
          // scroll port on horizontal-tb when the frame is positioned at
          // the bottom edge of the scroll port. For this reason we subtract
          // containerBSize from BEnd of the target and clamp it inside the
          // scrollable range.
          logicalTargetRect.BEnd(writingMode) - containerBSize,
          logicalScrollRange.BStart(writingMode),
          logicalScrollRange.BEnd(writingMode));
      blockDirectionPosition.emplace(writingMode.IsVerticalRL() ? -candidate
                                                                : candidate);
      break;
    }
    case StyleScrollSnapAlignKeyword::Center: {
      nscoord targetCenter = (logicalTargetRect.BStart(writingMode) +
                              logicalTargetRect.BEnd(writingMode)) /
                             2;
      nscoord halfSnapportSize = containerBSize / 2;
      // Get the center of the target to align with the center of the snapport
      // depending on direction.
      nscoord candidate = std::clamp(targetCenter - halfSnapportSize,
                                     logicalScrollRange.BStart(writingMode),
                                     logicalScrollRange.BEnd(writingMode));
      blockDirectionPosition.emplace(writingMode.IsVerticalRL() ? -candidate
                                                                : candidate);
      break;
    }
  }

  Maybe<nscoord> inlineDirectionPosition;
  nscoord containerISize = logicalSnapportRect.ISize(writingMode);
  switch (styleDisplay->mScrollSnapAlign.inline_) {
    case StyleScrollSnapAlignKeyword::None:
      break;
    case StyleScrollSnapAlignKeyword::Start: {
      nscoord candidate = std::clamp(logicalTargetRect.IStart(writingMode),
                                     logicalScrollRange.IStart(writingMode),
                                     logicalScrollRange.IEnd(writingMode));
      inlineDirectionPosition.emplace(
          writingMode.IsInlineReversed() ? -candidate : candidate);
      break;
    }
    case StyleScrollSnapAlignKeyword::End: {
      nscoord candidate = std::clamp(
          // Same as above BEnd case, we subtract containerISize.
          //
          // Note that the logical scroll range is mapped to [0, x] range even
          // if it's in RTL contents. So for example, if the physical range is
          // [-200, 0], it's mapped to [0, 200], i.e. IStart() is 0, IEnd() is
          // 200. So we can just use std::clamp with the same arguments in both
          // RTL/LTR cases.
          logicalTargetRect.IEnd(writingMode) - containerISize,
          logicalScrollRange.IStart(writingMode),
          logicalScrollRange.IEnd(writingMode));
      inlineDirectionPosition.emplace(
          writingMode.IsInlineReversed() ? -candidate : candidate);
      break;
    }
    case StyleScrollSnapAlignKeyword::Center: {
      nscoord targetCenter = (logicalTargetRect.IStart(writingMode) +
                              logicalTargetRect.IEnd(writingMode)) /
                             2;
      nscoord halfSnapportSize = containerISize / 2;
      // Get the center of the target to align with the center of the snapport
      // depending on direction.
      nscoord candidate = std::clamp(targetCenter - halfSnapportSize,
                                     logicalScrollRange.IStart(writingMode),
                                     logicalScrollRange.IEnd(writingMode));
      inlineDirectionPosition.emplace(
          writingMode.IsInlineReversed() ? -candidate : candidate);
      break;
    }
  }

  if (blockDirectionPosition || inlineDirectionPosition) {
    aSnapInfo.mSnapTargets.AppendElement(
        writingMode.IsVertical()
            ? ScrollSnapInfo::SnapTarget(
                  std::move(blockDirectionPosition),
                  std::move(inlineDirectionPosition), std::move(snapArea),
                  styleDisplay->mScrollSnapStop, targetId)
            : ScrollSnapInfo::SnapTarget(
                  std::move(inlineDirectionPosition),
                  std::move(blockDirectionPosition), std::move(snapArea),
                  styleDisplay->mScrollSnapStop, targetId));
    if (aSnapTargets) {
      aSnapTargets->EnsureInserted(aFrame->GetContent());
    }
  }
}

enum class ContainingBlockContext { Direct, Nested };

/**
 * Collect the scroll positions corresponding to snap positions of frames in the
 * subtree rooted at |aFrame|, relative to |aScrolledFrame|, into |aSnapInfo|.
 */
static void CollectScrollPositionsForSnap(
    nsIFrame* aFrame, nsIFrame* aScrolledFrame, const nsRect& aScrolledRect,
    const nsMargin& aScrollPadding, const nsRect& aScrollRange,
    WritingMode aWritingModeOnScroller, ScrollSnapInfo& aSnapInfo,
    ScrollContainerFrame::SnapTargetSet* aSnapTargets,
    ContainingBlockContext aContext) {
  ScrollContainerFrame* sf = do_QueryFrame(aFrame);
  // Absolute containing block inside a sub-scroller,
  // will not contain any OOF snap targets for the
  // outer scroller.
  if (aFrame->IsAbsPosContainingBlock() &&
      aContext == ContainingBlockContext::Nested) {
    return;
  }
  // Snap positions only affect the nearest ancestor scroll container on the
  // element's containing block chain.
  if (sf) {
    // Note: this function is called initially with `mScrolledFrame` as
    // `aFrame`, not the `ScrollContainerFrame` itself, so this branch is only
    // reached for nested scroll containers.
    // If the sub-scroll container is an abs-pos containing block, all its
    // position:absolute descendants are contained by it, so there are no
    // OOF snap targets to collect for the outer scroll container.
    if (aFrame->IsAbsPosContainingBlock()) {
      return;
    }
    // Sub-scroll container: don't collect its own snap targets, but traverse
    // into it to find OOF placeholders for position:absolute elements whose
    // containing block is an ancestor.
    for (nsIFrame* f : aFrame->PrincipalChildList()) {
      CollectScrollPositionsForSnap(
          f, aScrolledFrame, aScrolledRect, aScrollPadding, aScrollRange,
          aWritingModeOnScroller, aSnapInfo, aSnapTargets,
          ContainingBlockContext::Nested);
    }
    return;
  }

  auto processFrame = [&](nsIFrame* f, ContainingBlockContext aCtx) {
    if (aCtx == ContainingBlockContext::Direct) {
      const nsStyleDisplay* styleDisplay = f->StyleDisplay();
      if (styleDisplay->mScrollSnapAlign.inline_ !=
              StyleScrollSnapAlignKeyword::None ||
          styleDisplay->mScrollSnapAlign.block !=
              StyleScrollSnapAlignKeyword::None) {
        AppendScrollPositionsForSnap(
            f, aScrolledFrame, aScrolledRect, aScrollPadding, aScrollRange,
            aWritingModeOnScroller, aSnapInfo, aSnapTargets);
      }
    }
    CollectScrollPositionsForSnap(
        f, aScrolledFrame, aScrolledRect, aScrollPadding, aScrollRange,
        aWritingModeOnScroller, aSnapInfo, aSnapTargets, aCtx);
  };

  for (nsIFrame* f : aFrame->PrincipalChildList()) {
    if (f->IsPlaceholderFrame()) {
      if (nsIFrame* oof =
              static_cast<nsPlaceholderFrame*>(f)->GetOutOfFlowFrame()) {
        if (nsLayoutUtils::IsProperAncestorFrame(aScrolledFrame, oof)) {
          processFrame(oof, ContainingBlockContext::Direct);
        }
      }
      continue;
    }
    processFrame(f, aContext);
  }
}

static nscoord ResolveScrollPaddingStyleValue(
    const StyleRect<mozilla::NonNegativeLengthPercentageOrAuto>&
        aScrollPaddingStyle,
    Side aSide, const nsSize& aScrollPortSize) {
  if (aScrollPaddingStyle.Get(aSide).IsAuto()) {
    // https://drafts.csswg.org/css-scroll-snap-1/#valdef-scroll-padding-auto
    return 0;
  }

  nscoord percentageBasis;
  switch (aSide) {
    case eSideTop:
    case eSideBottom:
      percentageBasis = aScrollPortSize.height;
      break;
    case eSideLeft:
    case eSideRight:
      percentageBasis = aScrollPortSize.width;
      break;
  }

  return aScrollPaddingStyle.Get(aSide).AsLengthPercentage().Resolve(
      percentageBasis);
}

static nsMargin ResolveScrollPaddingStyle(
    const StyleRect<mozilla::NonNegativeLengthPercentageOrAuto>&
        aScrollPaddingStyle,
    const nsSize& aScrollPortSize) {
  return nsMargin(ResolveScrollPaddingStyleValue(aScrollPaddingStyle, eSideTop,
                                                 aScrollPortSize),
                  ResolveScrollPaddingStyleValue(aScrollPaddingStyle,
                                                 eSideRight, aScrollPortSize),
                  ResolveScrollPaddingStyleValue(aScrollPaddingStyle,
                                                 eSideBottom, aScrollPortSize),
                  ResolveScrollPaddingStyleValue(aScrollPaddingStyle, eSideLeft,
                                                 aScrollPortSize));
}

nsMargin ScrollContainerFrame::GetScrollPadding() const {
  nsIFrame* styleFrame = GetFrameForStyle();
  if (!styleFrame) {
    return nsMargin();
  }

  // The spec says percentage values are relative to the scroll port size.
  // https://drafts.csswg.org/css-scroll-snap-1/#scroll-padding
  return ResolveScrollPaddingStyle(styleFrame->StylePadding()->mScrollPadding,
                                   GetScrollPortRect().Size());
}

ScrollSnapInfo ScrollContainerFrame::ComputeScrollSnapInfo() {
  ScrollSnapInfo result;

  nsIFrame* scrollSnapFrame = GetFrameForStyle();
  if (!scrollSnapFrame) {
    return result;
  }

  const nsStyleDisplay* disp = scrollSnapFrame->StyleDisplay();
  if (disp->mScrollSnapType.strictness == StyleScrollSnapStrictness::None) {
    // We won't be snapping, short-circuit the computation.
    return result;
  }

  WritingMode writingMode = GetWritingMode();
  result.InitializeScrollSnapStrictness(writingMode, disp);

  result.mSnapportSize = GetSnapportSize();
  if (result.mSnapportSize.IsEmpty()) {
    // Ignore any target snap points if the snapport is empty.
    return result;
  }

  CollectScrollPositionsForSnap(mScrolledFrame, mScrolledFrame,
                                GetScrolledRect(), GetScrollPadding(),
                                GetLayoutScrollRange(), writingMode, result,
                                &mSnapTargets, ContainingBlockContext::Direct);
  return result;
}

ScrollSnapInfo ScrollContainerFrame::GetScrollSnapInfo() {
  // TODO(botond): Should we cache it?
  return ComputeScrollSnapInfo();
}

Maybe<SnapDestination> ScrollContainerFrame::GetSnapPointForDestination(
    ScrollUnit aUnit, ScrollSnapFlags aFlags, const nsPoint& aStartPos,
    const nsPoint& aDestination) {
  // We can release the strong references for the previous snap target
  // elements here since calling this ComputeScrollSnapInfo means we are going
  // to evaluate new snap points, thus there's no chance to generating
  // nsIContent instances in between this function call and the function call
  // for the (re-)evaluation.
  mSnapTargets.Clear();
  return ScrollSnapUtils::GetSnapPointForDestination(
      ComputeScrollSnapInfo(), aUnit, aFlags, GetLayoutScrollRange(), aStartPos,
      aDestination);
}

Maybe<SnapDestination> ScrollContainerFrame::GetSnapPointForResnap() {
  nsIContent* focusedContent =
      GetContent()->GetComposedDoc()->GetUnretargetedFocusedContent();

  nsIContent* targetContent =
      PresContext()->EventStateManager()->GetURLTargetContent();

  // While we are reconstructing this scroll container, we might be in the
  // process of restoring the scroll position, we need to respect it.
  nsPoint currentOrRestorePos =
      NeedRestorePosition() ? mRestorePos : GetScrollPosition();
  return ScrollSnapUtils::GetSnapPointForResnap(
      ComputeScrollSnapInfo(), GetLayoutScrollRange(), currentOrRestorePos,
      mLastSnapTargetIds, focusedContent, targetContent, GetWritingMode());
}

bool ScrollContainerFrame::NeedsResnap() {
  return GetSnapPointForResnap().isSome();
}

void ScrollContainerFrame::SetLastSnapTargetIds(
    UniquePtr<ScrollSnapTargetIds> aIds) {
  if (!aIds) {
    mLastSnapTargetIds = nullptr;
    return;
  }

  // This SetLastSnapTargetIds gets called asynchronously so that there's a race
  // condition something like;
  // 1) an async scroll operation triggered snapping to a point on an element
  // 2) during the async scroll operation, the element got removed from this
  //    scroll container
  // 3) re-snapping triggered
  // 4) this SetLastSnapTargetIds got called
  // In such case |aIds| are stale, we shouldn't use it.
  for (const auto* idList : {&aIds->mIdsOnX, &aIds->mIdsOnY}) {
    for (const auto id : *idList) {
      if (!mSnapTargets.Contains(reinterpret_cast<nsIContent*>(id))) {
        mLastSnapTargetIds = nullptr;
        return;
      }
    }
  }

  mLastSnapTargetIds = std::move(aIds);
}

bool ScrollContainerFrame::IsLastSnappedTarget(const nsIFrame* aFrame) const {
  ScrollSnapTargetId id = ScrollSnapUtils::GetTargetIdFor(aFrame);
  MOZ_ASSERT(id != ScrollSnapTargetId::None,
             "This function is supposed to be called for contents");

  if (!mLastSnapTargetIds) {
    return false;
  }

  return mLastSnapTargetIds->mIdsOnX.Contains(id) ||
         mLastSnapTargetIds->mIdsOnY.Contains(id);
}

void ScrollContainerFrame::TryResnap() {
  // If there's any async scroll is running or we are processing pan/touch
  // gestures or scroll thumb dragging,  don't clobber the scroll.
  if (!ScrollAnimationState().isEmpty() ||
      mInScrollingGesture == InScrollingGesture::Yes) {
    return;
  }

  // Same as in GetSnapPointForDestination, We can release the strong references
  // for the previous snap targets here.
  mSnapTargets.Clear();
  if (auto snapDestination = GetSnapPointForResnap()) {
    // We are going to re-snap so that we need to clobber scroll anchoring.
    mAnchor.UserScrolled();

    // Snap to the nearest snap position if exists.
    ScrollToWithOrigin(
        snapDestination->mPosition, nullptr /* range */,
        ScrollOperationParams{
            IsSmoothScroll(ScrollBehavior::Auto) ? ScrollMode::SmoothMsd
                                                 : ScrollMode::Instant,
            ScrollOrigin::Other, std::move(snapDestination->mTargetIds)});
  }
}

void ScrollContainerFrame::PostPendingResnapIfNeeded(const nsIFrame* aFrame) {
  if (!IsLastSnappedTarget(aFrame)) {
    return;
  }

  PostPendingResnap();
}

void ScrollContainerFrame::PostPendingResnap() {
  PresShell()->PostPendingScrollResnap(this);
}

ScrollContainerFrame::PhysicalScrollSnapAlign
ScrollContainerFrame::GetScrollSnapAlignFor(const nsIFrame* aFrame) const {
  StyleScrollSnapAlignKeyword alignForY = StyleScrollSnapAlignKeyword::None;
  StyleScrollSnapAlignKeyword alignForX = StyleScrollSnapAlignKeyword::None;

  nsIFrame* styleFrame = GetFrameForStyle();
  if (!styleFrame) {
    return {alignForX, alignForY};
  }

  const nsStyleDisplay* styleDisplay = aFrame->StyleDisplay();
  if (styleDisplay->mScrollSnapAlign.inline_ ==
          StyleScrollSnapAlignKeyword::None &&
      styleDisplay->mScrollSnapAlign.block ==
          StyleScrollSnapAlignKeyword::None) {
    return {alignForX, alignForY};
  }

  nsSize snapAreaSize =
      ScrollSnapUtils::GetSnapAreaFor(aFrame, mScrolledFrame, GetScrolledRect())
          .Size();
  const WritingMode writingMode =
      ScrollSnapUtils::NeedsToRespectTargetWritingMode(snapAreaSize,
                                                       GetSnapportSize())
          ? aFrame->GetWritingMode()
          : styleFrame->GetWritingMode();

  switch (styleFrame->StyleDisplay()->mScrollSnapType.axis) {
    case StyleScrollSnapAxis::X:
      alignForX = writingMode.IsVertical()
                      ? styleDisplay->mScrollSnapAlign.block
                      : styleDisplay->mScrollSnapAlign.inline_;
      break;
    case StyleScrollSnapAxis::Y:
      alignForY = writingMode.IsVertical()
                      ? styleDisplay->mScrollSnapAlign.inline_
                      : styleDisplay->mScrollSnapAlign.block;
      break;
    case StyleScrollSnapAxis::Block:
      if (writingMode.IsVertical()) {
        alignForX = styleDisplay->mScrollSnapAlign.block;
      } else {
        alignForY = styleDisplay->mScrollSnapAlign.block;
      }
      break;
    case StyleScrollSnapAxis::Inline:
      if (writingMode.IsVertical()) {
        alignForY = styleDisplay->mScrollSnapAlign.inline_;
      } else {
        alignForX = styleDisplay->mScrollSnapAlign.inline_;
      }
      break;
    case StyleScrollSnapAxis::Both:
      if (writingMode.IsVertical()) {
        alignForX = styleDisplay->mScrollSnapAlign.block;
        alignForY = styleDisplay->mScrollSnapAlign.inline_;
      } else {
        alignForX = styleDisplay->mScrollSnapAlign.inline_;
        alignForY = styleDisplay->mScrollSnapAlign.block;
      }
      break;
  }

  return {alignForX, alignForY};
}

bool ScrollContainerFrame::UseOverlayScrollbars() const {
  if (!PresContext()->UseOverlayScrollbars()) {
    return false;
  }
  return !mForceDisableOverlayScrollbars;
}

StyleScrollbarWidth ScrollContainerFrame::ScrollbarWidth(
    const ComputedStyle* aStyle) const {
  if (IsSingleLineTextInput(this)) {
    return StyleScrollbarWidth::None;
  }

  auto PrefGatedScrollbarWidth =
      [](StyleScrollbarWidth aComputedScrollbarWidth) {
        if (MOZ_UNLIKELY(
                StaticPrefs::layout_css_scrollbar_width_thin_disabled()) &&
            aComputedScrollbarWidth == StyleScrollbarWidth::Thin) {
          return StyleScrollbarWidth::Auto;
        }
        return aComputedScrollbarWidth;
      };

  const ComputedStyle* style =
      aStyle ? aStyle : nsLayoutUtils::StyleForScrollbar(this);
  auto scrollbarWidth = style->StyleUIReset()->ComputedScrollbarWidth();
  if (!mWebKitScrollbarStyle ||
      (mWebKitScrollbarStyle->StyleDisplay()->mDisplay != StyleDisplay::None &&
       [&] {
         auto [w, h] = GetWebkitScrollbarWidthAndHeight(mWebKitScrollbarStyle);
         return w != WebkitScrollbarSize::Zero ||
                h != WebkitScrollbarSize::Zero;
       }()) ||
      // On Chrome even if `display: none` or `width: 0; height: 0` is specified
      // on `::-webkit-scrollbar`, non auto `scrollbar-color` or non auto
      // `scrollbar-width` clobbers it.
      scrollbarWidth != StyleScrollbarWidth::Auto ||
      style->StyleUI()->HasCustomScrollbars()) {
    return PrefGatedScrollbarWidth(scrollbarWidth);
  }

  return StyleScrollbarWidth::None;
}

bool ScrollContainerFrame::DragScroll(WidgetEvent* aEvent) {
  // Dragging is allowed while within a 20 pixel border. Note that device pixels
  // are used so that the same margin is used even when zoomed in or out.
  nscoord margin = 20 * PresContext()->AppUnitsPerDevPixel();

  // Don't drag scroll for small scrollareas.
  if (mScrollPort.width < margin * 2 || mScrollPort.height < margin * 2) {
    return false;
  }

  // If willScroll is computed as false, then the frame is already scrolled as
  // far as it can go in both directions. Return false so that an ancestor
  // scrollframe can scroll instead.
  bool willScroll = false;
  nsPoint pnt =
      nsLayoutUtils::GetEventCoordinatesRelativeTo(aEvent, RelativeTo{this});
  nsPoint scrollPoint = GetScrollPosition();
  nsRect rangeRect = GetLayoutScrollRange();

  // Only drag scroll when a scrollbar is present.
  nsPoint offset;
  if (mHasHorizontalScrollbar) {
    if (pnt.x >= mScrollPort.x && pnt.x <= mScrollPort.x + margin) {
      offset.x = -margin;
      if (scrollPoint.x > 0) {
        willScroll = true;
      }
    } else if (pnt.x >= mScrollPort.XMost() - margin &&
               pnt.x <= mScrollPort.XMost()) {
      offset.x = margin;
      if (scrollPoint.x < rangeRect.width) {
        willScroll = true;
      }
    }
  }

  if (mHasVerticalScrollbar) {
    if (pnt.y >= mScrollPort.y && pnt.y <= mScrollPort.y + margin) {
      offset.y = -margin;
      if (scrollPoint.y > 0) {
        willScroll = true;
      }
    } else if (pnt.y >= mScrollPort.YMost() - margin &&
               pnt.y <= mScrollPort.YMost()) {
      offset.y = margin;
      if (scrollPoint.y < rangeRect.height) {
        willScroll = true;
      }
    }
  }

  if (offset.x || offset.y) {
    ScrollToWithOrigin(
        GetScrollPosition() + offset, nullptr /* range */,
        ScrollOperationParams{ScrollMode::Normal, ScrollOrigin::Other});
  }

  return willScroll;
}

static void AsyncScrollbarDragInitiated(uint64_t aDragBlockId,
                                        nsIFrame* aScrollbar) {
  if (nsSliderFrame* sliderFrame = GetSliderFrame(aScrollbar)) {
    sliderFrame->AsyncScrollbarDragInitiated(aDragBlockId);
  }
}

void ScrollContainerFrame::AsyncScrollbarDragInitiated(
    uint64_t aDragBlockId, ScrollDirection aDirection) {
  switch (aDirection) {
    case ScrollDirection::eVertical:
      ::AsyncScrollbarDragInitiated(aDragBlockId, mVScrollbarBox);
      break;
    case ScrollDirection::eHorizontal:
      ::AsyncScrollbarDragInitiated(aDragBlockId, mHScrollbarBox);
      break;
  }
}

static void AsyncScrollbarDragRejected(nsIFrame* aScrollbar) {
  if (nsSliderFrame* sliderFrame = GetSliderFrame(aScrollbar)) {
    sliderFrame->AsyncScrollbarDragRejected();
  }
}

void ScrollContainerFrame::AsyncScrollbarDragRejected() {
  // We don't get told which scrollbar requested the async drag,
  // so we notify both.
  ::AsyncScrollbarDragRejected(mHScrollbarBox);
  ::AsyncScrollbarDragRejected(mVScrollbarBox);
}

void ScrollContainerFrame::ApzSmoothScrollTo(
    const nsPoint& aDestination, ScrollMode aMode, ScrollOrigin aOrigin,
    ScrollTriggeredByScript aTriggeredByScript,
    UniquePtr<ScrollSnapTargetIds> aSnapTargetIds,
    ViewportType aViewportToScroll) {
  if (mApzSmoothScrollDestination == Some(aDestination)) {
    // If we already sent APZ a smooth-scroll request to this
    // destination (i.e. it was the last request
    // we sent), then don't send another one because it is redundant.
    // This is to avoid a scenario where pages do repeated scrollBy
    // calls, incrementing the generation counter, and blocking APZ from
    // syncing the scroll offset back to the main thread.
    // Note that if we get two smooth-scroll requests to the same
    // destination with some other scroll in between,
    // mApzSmoothScrollDestination will get reset to Nothing() and so
    // we shouldn't have the problem where this check discards a
    // legitimate smooth-scroll.
    return;
  }

  // The animation will be handled in the compositor, pass the
  // information needed to start the animation and skip the main-thread
  // animation for this scroll.
  MOZ_ASSERT(aOrigin != ScrollOrigin::None);
  mApzSmoothScrollDestination = Some(aDestination);
  AppendScrollUpdate(ScrollPositionUpdate::NewSmoothScroll(
      aMode, aOrigin, aDestination, aTriggeredByScript,
      std::move(aSnapTargetIds), aViewportToScroll));

  nsIContent* content = GetContent();
  if (!DisplayPortUtils::HasNonMinimalNonZeroDisplayPort(content)) {
    // If this frame doesn't have a displayport then there won't be an
    // APZC instance for it and so there won't be anything to process
    // this smooth scroll request. We should set a displayport on this
    // frame to force an APZC which can handle the request.
    if (MOZ_LOG_TEST(sDisplayportLog, LogLevel::Debug)) {
      mozilla::layers::ScrollableLayerGuid::ViewID viewID =
          mozilla::layers::ScrollableLayerGuid::NULL_SCROLL_ID;
      nsLayoutUtils::FindIDFor(content, &viewID);
      MOZ_LOG(
          sDisplayportLog, LogLevel::Debug,
          ("ApzSmoothScrollTo setting displayport on scrollId=%" PRIu64 "\n",
           viewID));
    }

    DisplayPortUtils::CalculateAndSetDisplayPortMargins(
        GetScrollTargetFrame(), DisplayPortUtils::RepaintMode::Repaint);
    nsIFrame* frame = do_QueryFrame(GetScrollTargetFrame());
    DisplayPortUtils::SetZeroMarginDisplayPortOnAsyncScrollableAncestors(frame);
  }

  // Schedule a paint to ensure that the frame metrics get updated on
  // the compositor thread.
  SchedulePaint();
}

bool ScrollContainerFrame::CanApzScrollInTheseDirections(
    ScrollDirections aDirections) {
  ScrollStyles styles = GetScrollStyles();
  if (aDirections.contains(ScrollDirection::eHorizontal) &&
      styles.mHorizontal == StyleOverflow::Hidden) {
    return false;
  }
  if (aDirections.contains(ScrollDirection::eVertical) &&
      styles.mVertical == StyleOverflow::Hidden) {
    return false;
  }
  return true;
}

bool ScrollContainerFrame::SmoothScrollVisual(
    const nsPoint& aVisualViewportOffset,
    FrameMetrics::ScrollOffsetUpdateType aUpdateType, ScrollMode aMode) {
  MOZ_ASSERT(aMode == ScrollMode::Smooth || aMode == ScrollMode::SmoothMsd);

  bool canDoApzSmoothScroll =
      nsLayoutUtils::AsyncPanZoomEnabled(this) && WantAsyncScroll();
  if (!canDoApzSmoothScroll) {
    return false;
  }

  // Stop suppressing displayport while the page is still loading.
  if (MOZ_UNLIKELY(PresShell()->IsDocumentLoading())) {
    PresShell()->SuppressDisplayport(false);
  }

  // Clamp the destination to the visual scroll range.
  // There is a potential issue here, where |mDestination| is usually
  // clamped to the layout scroll range, and so e.g. a subsequent
  // window.scrollBy() may have an undesired effect. However, as this function
  // is only called internally, this should not be a problem in practice.
  // If it turns out to be, the fix would be:
  //   - add a new "destination" field that doesn't have to be clamped to
  //     the layout scroll range
  //   - clamp mDestination to the layout scroll range here
  //   - make sure ComputeScrollMetadata() picks up the former as the
  //     smooth scroll destination to send to APZ.
  mDestination = GetVisualScrollRange().ClampPoint(aVisualViewportOffset);

  UniquePtr<ScrollSnapTargetIds> snapTargetIds;
  // Perform the scroll.
  ApzSmoothScrollTo(mDestination, aMode,
                    aUpdateType == FrameMetrics::eRestore
                        ? ScrollOrigin::Restore
                        : ScrollOrigin::Other,
                    ScrollTriggeredByScript::No, std::move(snapTargetIds),
                    ViewportType::Visual);
  return true;
}

bool ScrollContainerFrame::IsSmoothScroll(dom::ScrollBehavior aBehavior) const {
  if (aBehavior == dom::ScrollBehavior::Instant) {
    return false;
  }

  // The user smooth scrolling preference should be honored for any requested
  // smooth scrolls. A requested smooth scroll when smooth scrolling is
  // disabled should be equivalent to an instant scroll. This is not enforced
  // for the <scrollbox> XUL element to allow for the browser chrome to
  // override this behavior when toolkit.scrollbox.smoothScroll is enabled.
  if (!GetContent()->IsXULElement(nsGkAtoms::scrollbox)) {
    if (!nsLayoutUtils::IsSmoothScrollingEnabled()) {
      return false;
    }
  } else {
    if (!StaticPrefs::toolkit_scrollbox_smoothScroll()) {
      return false;
    }
  }

  if (aBehavior == dom::ScrollBehavior::Smooth) {
    return true;
  }

  nsIFrame* styleFrame = GetFrameForStyle();
  if (!styleFrame) {
    return false;
  }
  return (aBehavior == dom::ScrollBehavior::Auto &&
          styleFrame->StyleDisplay()->mScrollBehavior ==
              StyleScrollBehavior::Smooth);
}

ScrollMode ScrollContainerFrame::ScrollModeForScrollBehavior(
    dom::ScrollBehavior aBehavior) const {
  if (!IsSmoothScroll(aBehavior)) {
    return ScrollMode::Instant;
  }

  return StaticPrefs::layout_css_scroll_behavior_same_physics_as_user_input()
             ? ScrollMode::Smooth
             : ScrollMode::SmoothMsd;
}

nsTArray<ScrollPositionUpdate> ScrollContainerFrame::GetScrollUpdates() const {
  return mScrollUpdates.Clone();
}

void ScrollContainerFrame::AppendScrollUpdate(
    const ScrollPositionUpdate& aUpdate) {
  mScrollGeneration = aUpdate.GetGeneration();
  mScrollUpdates.AppendElement(aUpdate);
}

void ScrollContainerFrame::ScheduleScrollAnimations() {
  auto* rd = PresContext()->RefreshDriver();
  MOZ_ASSERT(rd);
  // This schedules UpdateAnimationsAndSendEvents in the HTML loop.
  rd->EnsureAnimationUpdate();
}

nsSize ScrollContainerFrame::GetSizeForWindowInnerSize() const {
  MOZ_ASSERT(mIsRoot);

  return mIsUsingMinimumScaleSize ? mMinimumScaleSize
                                  : PresContext()->GetVisibleArea().Size();
}
