/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* a presentation of a document, part 2 */

#ifndef mozilla_PresShell_h
#define mozilla_PresShell_h

#include <stdio.h>  // for FILE definition

#include "DepthOrderedFrameList.h"
#include "FrameMetrics.h"
#include "LayoutConstants.h"
#include "mozilla/ArenaObjectID.h"
#include "mozilla/Attributes.h"
#include "mozilla/dom/DocumentBinding.h"
#include "mozilla/FlushType.h"
#include "mozilla/layers/FocusTarget.h"
#include "mozilla/Logging.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/PresShellForwards.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "nsColor.h"
#include "nsCOMArray.h"
#include "nsCoord.h"
#include "nsCSSFrameConstructor.h"
#include "nsDOMNavigationTiming.h"
#include "nsFrameState.h"
#include "nsIContent.h"
#include "nsIObserver.h"
#include "nsISelectionController.h"
#include "nsPresArena.h"
#include "nsPresContext.h"
#include "nsQueryFrame.h"
#include "nsRect.h"
#include "nsRefreshObservers.h"
#include "nsStringFwd.h"
#include "nsStubDocumentObserver.h"
#include "nsTHashSet.h"
#include "nsThreadUtils.h"
#include "nsWeakReference.h"
#include "TouchManager.h"
#include "Units.h"
#include "Visibility.h"

#ifdef ACCESSIBILITY
#  include "nsAccessibilityService.h"
#endif

class AutoPointerEventTargetUpdater;
class AutoWeakFrame;
class gfxContext;
class MobileViewportManager;
class nsAutoCauseReflowNotifier;
class nsCanvasFrame;
class nsCaret;
class nsCSSFrameConstructor;
class nsDocShell;
class nsFrameSelection;
class nsIDocShell;
class nsIFrame;
class nsILayoutHistoryState;
class nsINode;
class nsIReflowCallback;
class nsITimer;
class nsPageSequenceFrame;
class nsPIDOMWindowOuter;
class nsPresShellEventCB;
class nsRange;
class nsRefreshDriver;
class nsRegion;
class nsTextFrame;
class nsView;
class nsViewManager;
class nsWindowSizes;
class WeakFrame;
class ZoomConstraintsClient;
struct nsCallbackEventRequest;
struct RangePaintInfo;

#ifdef MOZ_REFLOW_PERF
class ReflowCountMgr;
#endif

namespace mozilla {
class AccessibleCaretEventHub;
class FallbackRenderer;
class GeckoMVMContext;
class nsDisplayList;
class nsDisplayListBuilder;
class OverflowChangedTracker;
class ProfileChunkedBuffer;
class ScrollContainerFrame;
class StyleSheet;

#ifdef ACCESSIBILITY
namespace a11y {
class DocAccessible;
}  // namespace a11y
#endif

namespace dom {
class BrowserParent;
class Element;
class Event;
class HTMLSlotElement;
class Selection;
class PerformanceMainThread;
}  // namespace dom

namespace gfx {
class SourceSurface;
}  // namespace gfx

namespace layers {
class LayerManager;
struct LayersId;
}  // namespace layers

namespace layout {
class ScrollAnchorContainer;
}  // namespace layout

// 039d8ffc-fa55-42d7-a53a-388cb129b052
#define NS_PRESSHELL_IID \
  {0x039d8ffc, 0xfa55, 0x42d7, {0xa5, 0x3a, 0x38, 0x8c, 0xb1, 0x29, 0xb0, 0x52}}

#undef NOISY_INTERRUPTIBLE_REFLOW

/**
 * Presentation shell. Presentation shells are the controlling point for
 * managing the presentation of a document.  The presentation shell holds a
 * live reference to the document, the presentation context, the style
 * manager, the style set and the root frame.
 *
 * When this object is Release'd, it will release the document, the
 * presentation context, the style manager, the style set and the root frame.
 */

struct SingleCanvasBackground {
  nscolor mColor = 0;
  bool mCSSSpecified = false;
};

class PresShell final : public nsStubDocumentObserver,
                        public nsISelectionController,
                        public nsIObserver,
                        public nsSupportsWeakReference {
  typedef dom::Document Document;
  typedef dom::Element Element;
  typedef gfx::SourceSurface SourceSurface;
  typedef layers::FocusTarget FocusTarget;
  typedef layers::FrameMetrics FrameMetrics;
  typedef layers::LayerManager LayerManager;

  // A set type for tracking visible frames, for use by the visibility code in
  // PresShell. The set contains nsIFrame* pointers.
  typedef nsTHashSet<nsIFrame*> VisibleFrames;

 public:
  explicit PresShell(Document* aDocument);

  // nsISupports
  NS_DECL_ISUPPORTS

  NS_DECLARE_STATIC_IID_ACCESSOR(NS_PRESSHELL_IID)

  static bool AccessibleCaretEnabled(nsIDocShell* aDocShell);

  /**
   * Return the active content currently capturing the mouse if any.
   */
  static nsIContent* GetCapturingContent() {
    return sCapturingContentInfo.mContent;
  }

  /**
   */
  static dom::BrowserParent* GetCapturingRemoteTarget() {
    MOZ_ASSERT(XRE_IsParentProcess());
    return sCapturingContentInfo.mRemoteTarget;
  }

  /**
   * Allow or disallow mouse capturing.
   */
  static void AllowMouseCapture(bool aAllowed) {
    sCapturingContentInfo.mAllowed = aAllowed;
  }

  /**
   * Returns true if there is an active mouse capture that wants to prevent
   * drags.
   */
  static bool IsMouseCapturePreventingDrag() {
    return sCapturingContentInfo.mPreventDrag && sCapturingContentInfo.mContent;
  }

  static void ClearMouseCaptureOnView(nsView* aView);

  // Clear the capture content if it exists in this process.
  static void ClearMouseCapture();

  // If a frame in the subtree rooted at aFrame is capturing the mouse then
  // clears that capture.
  //
  // NOTE(emilio): This is needed only so that mouse events captured by a remote
  // frame don't remain being captured by the frame while hidden, see
  // dom/events/test/browser_mouse_enterleave_switch_tab.js, which is the only
  // test that meaningfully exercises this code path.
  //
  // We could consider maybe removing this, since the capturing content gets
  // reset on mouse/pointerdown? Or maybe exposing an API so that the front-end
  // does this.
  static void ClearMouseCapture(nsIFrame* aFrame);

#ifdef ACCESSIBILITY
  /**
   * Return the document accessible for this PresShell if there is one.
   */
  a11y::DocAccessible* GetDocAccessible() const { return mDocAccessible; }

  /**
   * Set the document accessible for this PresShell.
   */
  void SetDocAccessible(a11y::DocAccessible* aDocAccessible) {
    mDocAccessible = aDocAccessible;
  }
#endif  // #ifdef ACCESSIBILITY

  /**
   * See `mLastOverWindowPointerLocation`.
   */
  const nsPoint& GetLastOverWindowPointerLocation() const {
    return mLastOverWindowPointerLocation;
  }

  MOZ_CAN_RUN_SCRIPT void Init(nsPresContext*, nsViewManager*);

  /**
   * All callers are responsible for calling |Destroy| after calling
   * |EndObservingDocument|.  It needs to be separate only because form
   * controls incorrectly store their data in the frames rather than the
   * content model and printing calls |EndObservingDocument| multiple
   * times to make form controls behave nicely when printed.
   */
  void Destroy();

  bool IsDestroying() { return mIsDestroying; }

  /**
   * All frames owned by the shell are allocated from an arena.  They
   * are also recycled using free lists.  Separate free lists are
   * maintained for each frame type (aID), which must always correspond
   * to the same aSize value.  AllocateFrame is infallible and will abort
   * on out-of-memory.
   */
  void* AllocateFrame(nsQueryFrame::FrameIID aID, size_t aSize) {
#define FRAME_ID(classname, ...)                                  \
  static_assert(size_t(nsQueryFrame::FrameIID::classname##_id) == \
                    size_t(eArenaObjectID_##classname),           \
                "");
#define ABSTRACT_FRAME_ID(classname)                              \
  static_assert(size_t(nsQueryFrame::FrameIID::classname##_id) == \
                    size_t(eArenaObjectID_##classname),           \
                "");
#include "mozilla/FrameIdList.h"
#undef FRAME_ID
#undef ABSTRACT_FRAME_ID
    return AllocateByObjectID(ArenaObjectID(size_t(aID)), aSize);
  }

  void FreeFrame(nsQueryFrame::FrameIID aID, void* aPtr) {
    return FreeByObjectID(ArenaObjectID(size_t(aID)), aPtr);
  }

  void* AllocateByObjectID(ArenaObjectID aID, size_t aSize) {
    void* result = mFrameArena.Allocate(aID, aSize);
    RecordAlloc(result);
    return result;
  }

  void FreeByObjectID(ArenaObjectID aID, void* aPtr) {
    RecordFree(aPtr);
    if (!mIsDestroying) {
      mFrameArena.Free(aID, aPtr);
    }
  }

  Document* GetDocument() const { return mDocument; }

  nsPresContext* GetPresContext() const { return mPresContext; }

  nsViewManager* GetViewManager() const { return mViewManager; }

  nsRefreshDriver* GetRefreshDriver() const;

  nsCSSFrameConstructor* FrameConstructor() const {
    return mFrameConstructor.get();
  }

  /**
   * FrameSelection will return the Frame based selection API.
   * You cannot go back and forth anymore with QI between nsIDOM sel and
   * nsIFrame sel.
   */
  already_AddRefed<nsFrameSelection> FrameSelection();

  /**
   * ConstFrameSelection returns an object which methods are safe to use for
   * example in nsIFrame code.
   */
  const nsFrameSelection* ConstFrameSelection() const { return mSelection; }

  // Start receiving notifications from our document. If called after Destroy,
  // this will be ignored.
  void BeginObservingDocument();

  // Stop receiving notifications from our document. If called after Destroy,
  // this will be ignored.
  void EndObservingDocument();

  bool IsObservingDocument() const { return mIsObservingDocument; }

  /**
   * Return whether Initialize() was previously called.
   */
  bool DidInitialize() const { return mDidInitialize; }

  /**
   * Perform initialization. Constructs the frame for the root content
   * object and then enqueues a reflow of the frame model.
   *
   * Callers of this method must hold a reference to this shell that
   * is guaranteed to survive through arbitrary script execution.
   * Calling Initialize can execute arbitrary script.
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY nsresult Initialize();

  /**
   * Schedule a reflow for the frame model into a new width and height.  The
   * coordinates for aWidth and aHeight must be in standard nscoord's.
   *
   * Returns whether layout might have changed.
   */
  MOZ_CAN_RUN_SCRIPT void ResizeReflow(
      nscoord aWidth, nscoord aHeight,
      ResizeReflowOptions = ResizeReflowOptions::NoOption);
  MOZ_CAN_RUN_SCRIPT bool ResizeReflowIgnoreOverride(
      nscoord aWidth, nscoord aHeight,
      ResizeReflowOptions = ResizeReflowOptions::NoOption);
  MOZ_CAN_RUN_SCRIPT void ForceResizeReflowWithCurrentDimensions();

  /** Schedule a resize event if applicable. */
  enum class ResizeEventKind : uint8_t { Regular, Visual };
  void ScheduleResizeEventIfNeeded(ResizeEventKind = ResizeEventKind::Regular);

  void PostScrollEvent(mozilla::Runnable*);

  /**
   * Returns true if the document hosted by this presShell is in a devtools
   * Responsive Design Mode browsing context.
   */
  bool InRDMPane();

#if defined(MOZ_WIDGET_ANDROID)
  /**
   * If the dynamic toolbar is not expanded, notify the app to do so.
   */
  void MaybeNotifyShowDynamicToolbar();
#endif  // defined(MOZ_WIDGET_ANDROID)

  void RefreshZoomConstraintsForScreenSizeChange();

 private:
  /**
   * This is what ResizeReflowIgnoreOverride does when not shrink-wrapping (that
   * is, when ResizeReflowOptions::BSizeLimit is not specified).
   */
  bool SimpleResizeReflow(nscoord aWidth, nscoord aHeight);

  bool CanHandleUserInputEvents(WidgetGUIEvent* aGUIEvent);

  void ScrollFrameIntoVisualViewport(Maybe<nsPoint>& aDestination,
                                     const nsRect& aPositionFixedRect,
                                     ScrollFlags aScrollFlags);

 public:
  /**
   * Updates pending layout, assuming reasonable (up-to-date, or mid-update for
   * container queries) styling of the page. Returns whether a reflow did not
   * get interrupted (and thus layout should be considered fully up-to-date).
   */
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool DoFlushLayout(bool aInterruptible);

  /**
   * Note that the assumptions that determine whether we need a mobile viewport
   * manager may have changed.
   */
  MOZ_CAN_RUN_SCRIPT void MaybeRecreateMobileViewportManager(
      bool aAfterInitialization);

  /**
   * Returns true if this document uses mobile viewport sizing (including
   * processing of <meta name="viewport"> tags).
   *
   * Note that having a MobileViewportManager does not necessarily mean using
   * mobile viewport sizing, as with desktop zooming we can have a
   * MobileViewportManager on desktop, but we only want to do mobile viewport
   * sizing on mobile. (TODO: Rename MobileViewportManager to reflect its more
   * general role.)
   */
  bool UsesMobileViewportSizing() const;

  void ResetWasLastReflowInterrupted() { mWasLastReflowInterrupted = false; }

  /**
   * Get the MobileViewportManager used to manage the document's mobile
   * viewport. Will return null in situations where we don't have a mobile
   * viewport, and for documents that are not the root content document.
   */
  MobileViewportManager* GetMobileViewportManager() const;

  /**
   * Called when document load completes.
   */
  void LoadComplete();

  /**
   * This calls through to the frame constructor to get the root frame.
   */
  nsIFrame* GetRootFrame() const { return mFrameConstructor->GetRootFrame(); }

  /**
   * Get root scroll container frame from the frame constructor.
   */
  ScrollContainerFrame* GetRootScrollContainerFrame() const;

  /**
   * Get the current focused content or DOM selection that should be the
   * target for scrolling.
   */
  already_AddRefed<nsIContent> GetContentForScrolling() const;

  /**
   * Get the DOM selection that should be the target for scrolling, if there
   * is no focused content.
   */
  already_AddRefed<nsIContent> GetSelectedContentForScrolling() const;

  /**
   * Gets nearest scroll container frame from the specified content node. The
   * frame is scrollable with overflow:scroll or overflow:auto in some direction
   * when aDirection is eEither. Otherwise, this returns a nearest scroll
   * container frame that is scrollable in the specified direction.
   */
  ScrollContainerFrame* GetScrollContainerFrameToScrollForContent(
      nsIContent* aContent, layers::ScrollDirections aDirections);

  /**
   * Gets nearest scroll container frame from current focused content or DOM
   * selection if there is no focused content. The frame is scrollable with
   * overflow:scroll or overflow:auto in some direction when aDirection is
   * eEither. Otherwise, this returns a nearest scroll container frame that is
   * scrollable in the specified direction.
   */
  ScrollContainerFrame* GetScrollContainerFrameToScroll(
      layers::ScrollDirections aDirections);

  /**
   * Returns the page sequence frame associated with the frame hierarchy.
   * Returns nullptr if not a paginated view.
   */
  nsPageSequenceFrame* GetPageSequenceFrame() const;

  /**
   * Returns the canvas frame associated with the frame hierarchy.
   * Returns nullptr if is XUL document.
   */
  nsCanvasFrame* GetCanvasFrame() const;

  void PostPendingScrollAnchorSelection(
      layout::ScrollAnchorContainer* aContainer);
  void FlushPendingScrollAnchorSelections();
  void PostPendingScrollAnchorAdjustment(
      layout::ScrollAnchorContainer* aContainer);

  void PostPendingScrollResnap(ScrollContainerFrame* aScrollContainerFrame);
  void FlushPendingScrollResnap();

  void CancelAllPendingReflows();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void NotifyCounterStylesAreDirty();

  bool FrameIsAncestorOfDirtyRoot(nsIFrame* aFrame) const;

  /**
   * Destroy the frames for aElement, and reconstruct them asynchronously if
   * needed.
   *
   * Note that this may destroy frames for an arbitrary ancestor, depending on
   * the frame tree structure.
   */
  void DestroyFramesForAndRestyle(Element* aElement);

  /**
   * Called when a ShadowRoot will be attached to an element (and thus the flat
   * tree children will go away).
   */
  void ShadowRootWillBeAttached(Element& aElement);

  /**
   * Handles all the layout stuff needed when the slot assignment for an element
   * is about to change.
   *
   * Only called when the slot attribute of the element changes, the rest of
   * the changes should be handled in ShadowRoot.
   */
  void SlotAssignmentWillChange(Element& aElement,
                                dom::HTMLSlotElement* aOldSlot,
                                dom::HTMLSlotElement* aNewSlot);

  /**
   * Handles when a CustomStateSet state is about to be removed or added.
   */
  void CustomStatesWillChange(Element& aElement);
  void CustomStateChanged(Element& aElement, nsAtom* aState);

  void PostRecreateFramesFor(Element*);
  void RestyleForAnimation(Element*, RestyleHint);

  /**
   * Determine if it is safe to flush all pending notifications.
   */
  bool IsSafeToFlush() const;

  /**
   * Informs the document's FontFaceSet that the refresh driver ticked,
   * flushing style and layout.
   */
  void NotifyFontFaceSetOnRefresh();

  void StartObservingRefreshDriver();

  /**
   * Callbacks will be called even if reflow itself fails for
   * some reason.
   */
  nsresult PostReflowCallback(nsIReflowCallback* aCallback);
  void CancelReflowCallback(nsIReflowCallback* aCallback);

  void ScheduleBeforeFirstPaint();
  void UnsuppressAndInvalidate();

  void ClearFrameRefs(nsIFrame* aFrame);

  enum class CanMoveLastSelectionForToString { No, Yes };
  // Clears the selection of the older focused frame selection if any.
  void FrameSelectionWillTakeFocus(nsFrameSelection&,
                                   CanMoveLastSelectionForToString);

  // Clears and repaint mFocusedFrameSelection if it matches the argument.
  void FrameSelectionWillLoseFocus(nsFrameSelection&);

  // Update mLastSelectionForToString to the given frame selection.
  void UpdateLastSelectionForToString(const nsFrameSelection*);

  /**
   * Get a reference rendering context. This is a context that should not
   * be rendered to, but is suitable for measuring text and performing
   * other non-rendering operations. Guaranteed to return non-null.
   */
  mozilla::UniquePtr<gfxContext> CreateReferenceRenderingContext();

  /**
   * Scrolls the view of the document so that the given area of a frame
   * is visible, if possible. Layout is not flushed before scrolling.
   *
   * @param aRect Relative to aTargetFrame. If none, the bounding box of
   * aTargetFrame will be used. The rect edges will be respected even if the
   * rect is empty.
   * @param aVertical see ScrollContentIntoView and ScrollAxis
   * @param aHorizontal see ScrollContentIntoView and ScrollAxis
   * @param aScrollFlags if ScrollFirstAncestorOnly is set, only the
   * nearest scrollable ancestor is scrolled, otherwise all
   * scrollable ancestors may be scrolled if necessary
   * if ScrollOverflowHidden is set then we may scroll in a direction
   * even if overflow:hidden is specified in that direction; otherwise
   * we will not scroll in that direction when overflow:hidden is
   * set for that direction
   * If ScrollNoParentFrames is set then we only scroll
   * nodes in this document, not in any parent documents which
   * contain this document in a iframe or the like.
   * If AxesAreLogical is set, then the aVertical param actually refers to the
   * frame's block axis, and the aHorizontal param to its inline axis, rather
   * than to physical directions.
   * @return true if any scrolling happened, false if no scrolling happened
   */
  MOZ_CAN_RUN_SCRIPT
  bool ScrollFrameIntoView(nsIFrame* aTargetFrame,
                           const Maybe<nsRect>& aKnownRectRelativeToTarget,
                           ScrollAxis aVertical, ScrollAxis aHorizontal,
                           ScrollFlags aScrollFlags);

  /**
   * Suppress notification of the frame constructor that frames are being
   * destroyed.
   */
  void SetIgnoreFrameDestruction(bool aIgnore);

  /**
   * Get the AccessibleCaretEventHub, if it exists. AddRefs it.
   */
  already_AddRefed<AccessibleCaretEventHub> GetAccessibleCaretEventHub() const;

  /**
   * Get the caret, if it exists. AddRefs it.
   */
  already_AddRefed<nsCaret> GetCaret() const;

  /**
   * Set the current caret to a new caret. To undo this, call RestoreCaret.
   */
  void SetCaret(nsCaret* aNewCaret);

  /**
   * Restore the caret to the original caret that this pres shell was created
   * with.
   */
  void RestoreCaret();

  dom::Selection* GetCurrentSelection(SelectionType aSelectionType);

  /**
   * Gets the last selection that took focus in this document. This is basically
   * the frame selection that's visible to the user.
   */
  nsFrameSelection* GetLastFocusedFrameSelection();

  const nsFrameSelection* GetLastSelectionForToString() const {
    return mLastSelectionForToString;
  }

  /**
   * Interface to dispatch events via the presshell
   * @note The caller must have a strong reference to the PresShell.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult HandleEventWithTarget(WidgetEvent* aEvent, nsIFrame* aFrame,
                                 nsIContent* aContent,
                                 nsEventStatus* aEventStatus,
                                 bool aIsHandlingNativeEvent = false,
                                 nsIContent** aTargetContent = nullptr,
                                 nsIContent* aOverrideClickTarget = nullptr) {
    MOZ_ASSERT(aEvent);
    EventHandler eventHandler(*this);
    return eventHandler.HandleEventWithTarget(
        aEvent, aFrame, aContent, aEventStatus, aIsHandlingNativeEvent,
        aTargetContent, aOverrideClickTarget);
  }

  /**
   * Dispatch event to content only (NOT full processing)
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                    WidgetEvent* aEvent,
                                    nsEventStatus* aStatus);

  /**
   * Dispatch event to content only (NOT full processing)
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                    dom::Event* aEvent, nsEventStatus* aStatus);

  /**
   * Return whether or not the event is valid to be dispatched
   */
  bool CanDispatchEvent(const WidgetGUIEvent* aEvent = nullptr) const;

  /**
   * Gets the current target event frame from the PresShell
   */
  nsIFrame* GetCurrentEventFrame();

  /**
   * Gets the current target event frame from the PresShell
   */
  already_AddRefed<nsIContent> GetEventTargetContent(WidgetEvent* aEvent);

  /**
   * Get and set the history state for the current document
   */
  nsresult CaptureHistoryState(nsILayoutHistoryState** aLayoutHistoryState);

  /**
   * Determine if reflow is currently locked
   * returns true if reflow is locked, false otherwise
   */
  bool IsReflowLocked() const { return mIsReflowing; }

  /**
   * Called to find out if painting is suppressed for this presshell.  If it is
   * suppressd, we don't allow the painting of any layer but the background, and
   * we don't recur into our children.
   */
  bool IsPaintingSuppressed() const { return mPaintingSuppressed; }

  void TryUnsuppressPaintingSoon();

  void UnsuppressPainting();
  void InitPaintSuppressionTimer();
  void CancelPaintSuppressionTimer();

  /**
   * Reconstruct frames for all elements in the document
   */
  MOZ_CAN_RUN_SCRIPT void ReconstructFrames();

  /**
   * See if reflow verification is enabled. To enable reflow verification add
   * "verifyreflow:1" to your MOZ_LOG environment variable (any non-zero
   * debug level will work). Or, call SetVerifyReflowEnable with true.
   */
  static bool GetVerifyReflowEnable();

  /**
   * Set the verify-reflow enable flag.
   */
  static void SetVerifyReflowEnable(bool aEnabled);

  nsIFrame* GetAbsoluteContainingBlock(nsIFrame* aFrame);

#ifdef MOZ_REFLOW_PERF
  void DumpReflows();
  void CountReflows(const char* aName, nsIFrame* aFrame);
  void PaintCount(const char* aName, gfxContext* aRenderingContext,
                  nsPresContext* aPresContext, nsIFrame* aFrame,
                  const nsPoint& aOffset, uint32_t aColor);
  void SetPaintFrameCount(bool aOn);
  bool IsPaintingFrameCounts();
#endif  // #ifdef MOZ_REFLOW_PERF

  // Debugging hooks
#ifdef DEBUG
  void ListComputedStyles(FILE* out, int32_t aIndent = 0);
#endif
#if defined(DEBUG) || defined(MOZ_LAYOUT_DEBUGGER)
  void ListStyleSheets(FILE* out, int32_t aIndent = 0);
#endif

  /**
   * Stop all refresh drivers and carets in this presentation and
   * in the presentations of subdocuments.  Resets painting to a suppressed
   * state.
   * XXX this should include image animations
   */
  void Freeze(bool aIncludeSubDocuments = true);
  bool IsFrozen() { return mFrozen; }

  /**
   * Restarts refresh drivers in this presentation and in the
   * presentations of subdocuments, then do a full invalidate of the content
   * area.
   */
  void Thaw(bool aIncludeSubDocuments = true);

  void FireOrClearDelayedEvents(bool aFireEvents);

  /**
   * When this shell is disconnected from its containing docshell, we
   * lose our container pointer.  However, we'd still like to be able to target
   * user events at the docshell's parent.  This pointer allows us to do that.
   * It should not be used for any other purpose.
   */
  void SetForwardingContainer(const WeakPtr<nsDocShell>& aContainer);

  /**
   * Render the document into an arbitrary gfxContext
   * Designed for getting a picture of a document or a piece of a document
   * Note that callers will generally want to call FlushPendingNotifications
   * to get an up-to-date view of the document
   * @param aRect is the region to capture into the offscreen buffer, in the
   * root frame's coordinate system (if aIgnoreViewportScrolling is false)
   * or in the root scrolled frame's coordinate system
   * (if aIgnoreViewportScrolling is true). The coordinates are in appunits.
   * @param aFlags see below;
   *   set RenderDocumentFlags::IsUntrusted if the contents may be passed to
   * malicious agents. E.g. we might choose not to paint the contents of
   * sensitive widgets such as the file name in a file upload widget, and we
   * might choose not to paint themes.
   *   set RenderDocumentFlags::IgnoreViewportScrolling to ignore clipping and
   *  scrollbar painting due to scrolling in the viewport
   *   set RenderDocumentFlags::ResetViewportScrolling to temporarily set the
   * viewport scroll position to 0 so that position:fixed elements are drawn
   * at their initial position.
   *   set RenderDocumentFlags::DrawCaret to draw the caret if one would be
   *  visible (by default the caret is never drawn)
   *   set RenderDocumentFlags::UseWidgetLayers to force rendering to go
   *  through the layer manager for the window. This may be unexpectedly slow
   * (if the layer manager must read back data from the GPU) or low-quality
   * (if the layer manager reads back pixel data and scales it
   * instead of rendering using the appropriate scaling). It may also
   * slow everything down if the area rendered does not correspond to the
   * normal visible area of the window.
   *   set RenderDocumentFlags::AsyncDecodeImages to avoid having images
   * synchronously decoded during rendering.
   * (by default images decode synchronously with RenderDocument)
   *   set RenderDocumentFlags::DocumentRelative to render the document as if
   * there has been no scrolling and interpret |aRect| relative to the document
   * instead of the CSS viewport. Only considered if
   * RenderDocumentFlags::IgnoreViewportScrolling is set or the document is in
   * ignore viewport scrolling mode
   * (PresShell::SetIgnoreViewportScrolling/IgnoringViewportScrolling).
   *   set RenderDocumentFlags::UseHighQualityScaling to enable downscale on
   *   decode for images.
   * @param aBackgroundColor a background color to render onto
   * @param aRenderedContext the gfxContext to render to. We render so that
   * one CSS pixel in the source document is rendered to one unit in the current
   * transform.
   */
  nsresult RenderDocument(const nsRect& aRect, RenderDocumentFlags aFlags,
                          nscolor aBackgroundColor,
                          gfxContext* aRenderedContext);

  /**
   * Renders a node aNode to a surface and returns it. The aRegion may be used
   * to clip the rendering. This region is measured in CSS pixels from the
   * edge of the presshell area. The aPoint, aScreenRect and aFlags arguments
   * function in a similar manner as RenderSelection.
   */
  already_AddRefed<SourceSurface> RenderNode(nsINode* aNode,
                                             const Maybe<CSSIntRegion>& aRegion,
                                             const LayoutDeviceIntPoint aPoint,
                                             LayoutDeviceIntRect* aScreenRect,
                                             RenderImageFlags aFlags);

  /**
   * Renders a selection to a surface and returns it. This method is primarily
   * intended to create the drag feedback when dragging a selection.
   *
   * aScreenRect will be filled in with the bounding rectangle of the
   * selection area on screen.
   *
   * If the area of the selection is large and the RenderImageFlags::AutoScale
   * is set, the image will be scaled down. The argument aPoint is used in this
   * case as a reference point when determining the new screen rectangle after
   * scaling. Typically, this will be the mouse position, so that the screen
   * rectangle is positioned such that the mouse is over the same point in the
   * scaled image as in the original. When scaling does not occur, the mouse
   * point isn't used because the position can be determined from the displayed
   * frames.
   */
  already_AddRefed<SourceSurface> RenderSelection(
      dom::Selection* aSelection, const LayoutDeviceIntPoint aPoint,
      LayoutDeviceIntRect* aScreenRect, RenderImageFlags aFlags);

  void AddAutoWeakFrame(AutoWeakFrame* aWeakFrame);
  void AddWeakFrame(WeakFrame* aWeakFrame);

  void RemoveAutoWeakFrame(AutoWeakFrame* aWeakFrame);
  void RemoveWeakFrame(WeakFrame* aWeakFrame);

  /**
   * Stop or restart non synthetic test mouse event handling on *all*
   * presShells.
   *
   * @param aDisable If true, disable all non synthetic test mouse
   * events on all presShells.  Otherwise, enable them.
   */
  void DisableNonTestMouseEvents(bool aDisable);

  /**
   * Record the background color of the most recently drawn canvas. This color
   * is composited on top of the user's default background color and then used
   * to draw the background color of the canvas. See PresShell::Paint,
   * PresShell::PaintDefaultBackground, and nsDocShell::SetupNewViewer;
   * bug 488242, bug 476557 and other bugs mentioned there.
   */
  void SetCanvasBackground(nscolor aColor) {
    mCanvasBackground.mViewport.mColor = aColor;
  }
  nscolor GetCanvasBackground() const {
    return mCanvasBackground.mViewport.mColor;
  }

  const SingleCanvasBackground& GetCanvasBackground(bool aForPage) const {
    return aForPage ? mCanvasBackground.mPage : mCanvasBackground.mViewport;
  }

  struct CanvasBackground {
    // The canvas frame background for the whole viewport.
    SingleCanvasBackground mViewport;
    // The canvas frame background for a printed page. Note that when
    // print-previewing / in paged mode we have multiple canvas frames (one for
    // the viewport, one for each page).
    SingleCanvasBackground mPage;
  };

  // Use the current frame tree (if it exists) to update the background color of
  // the canvas frames.
  CanvasBackground ComputeCanvasBackground() const;
  void UpdateCanvasBackground();

  /**
   * Computes the backstop color for the view: transparent if in a transparent
   * widget, otherwise the PresContext default background color. This color is
   * only visible if the contents of the view as a whole are translucent.
   */
  nscolor ComputeBackstopColor(nsView* aDisplayRoot);

  void ObserveNativeAnonMutationsForPrint(bool aObserve) {
    mObservesMutationsForPrint = aObserve;
  }
  bool ObservesNativeAnonMutationsForPrint() {
    return mObservesMutationsForPrint;
  }

  void ActivenessMaybeChanged();
  bool IsActive() const { return mIsActive; }

  /**
   * Keep track of how many times this presshell has been rendered to
   * a window.
   */
  uint64_t GetPaintCount() { return mPaintCount; }
  void IncrementPaintCount() { ++mPaintCount; }

  /**
   * Get the root DOM window of this presShell.
   */
  already_AddRefed<nsPIDOMWindowOuter> GetRootWindow();

  /**
   * This returns the focused DOM window under our top level window.
   * I.e., when we are deactive, this returns the *last* focused DOM window.
   */
  already_AddRefed<nsPIDOMWindowOuter> GetFocusedDOMWindowInOurWindow();

  /**
   * Get the focused content under this window.
   */
  already_AddRefed<nsIContent> GetFocusedContentInOurWindow() const;

  /**
   * Get the window renderer for the widget of the root view, if it has
   * one.
   */
  WindowRenderer* GetWindowRenderer();

  /**
   * Return true iff there is a widget rendering this presShell and that
   * widget is APZ-enabled.
   */
  bool AsyncPanZoomEnabled();

  /**
   * Track whether we're ignoring viewport scrolling for the purposes
   * of painting.  If we are ignoring, then layers aren't clipped to
   * the CSS viewport and scrollbars aren't drawn.
   */
  bool IgnoringViewportScrolling() const {
    return !!(mRenderingStateFlags &
              RenderingStateFlags::IgnoringViewportScrolling);
  }

  float GetResolution() const { return mResolution.valueOr(1.0); }
  float GetCumulativeResolution() const;

  /**
   * Accessors for a flag that tracks whether the most recent change to
   * the pres shell's resolution was originated by the main thread.
   */
  bool IsResolutionUpdated() const { return mResolutionUpdated; }
  void SetResolutionUpdated(bool aUpdated) { mResolutionUpdated = aUpdated; }

  /**
   * Returns true if the resolution has ever been changed by APZ.
   */
  bool IsResolutionUpdatedByApz() const { return mResolutionUpdatedByApz; }

  /**
   * Used by session restore code to restore a resolution before the first
   * paint.
   */
  void SetRestoreResolution(float aResolution,
                            LayoutDeviceIntSize aDisplaySize);

  /**
   * Returns whether we are in a DrawWindow() call that used the
   * DRAWWINDOW_DO_NOT_FLUSH flag.
   */
  bool InDrawWindowNotFlushing() const {
    return !!(mRenderingStateFlags &
              RenderingStateFlags::DrawWindowNotFlushing);
  }

  /**
   * Set the isFirstPaint flag.
   */
  void SetIsFirstPaint(bool aIsFirstPaint) { mIsFirstPaint = aIsFirstPaint; }

  /**
   * Get the isFirstPaint flag.
   */
  bool GetIsFirstPaint() const { return mIsFirstPaint; }

  uint32_t GetPresShellId() { return mPresShellId; }

  /**
   * Dispatch a mouse move event based on the most recent mouse position if
   * this PresShell is visible. This is used when the contents of the page
   * moved (aFromScroll is false) or scrolled (aFromScroll is true).
   */
  void SynthesizeMouseMove(bool aFromScroll);

  MOZ_CAN_RUN_SCRIPT
  nsresult HandleEvent(nsIFrame* aFrame, WidgetGUIEvent* aEvent,
                       bool aDontRetargetEvents, nsEventStatus* aEventStatus);
  bool ShouldIgnoreInvalidation();
  /**
   * Notify that we called Paint with PaintFlags::PaintComposite.
   * Fires on the presshell for the painted widget.
   * This is issued at a time when it's safe to modify widget geometry.
   */
  MOZ_CAN_RUN_SCRIPT void DidPaintWindow();

  bool IsVisible() const;
  bool IsUnderHiddenEmbedderElement() const {
    return mUnderHiddenEmbedderElement;
  }
  void SetIsUnderHiddenEmbedderElement(bool aUnderHiddenEmbedderElement) {
    mUnderHiddenEmbedderElement = aUnderHiddenEmbedderElement;
  }

  MOZ_CAN_RUN_SCRIPT
  void DispatchSynthMouseMove(WidgetGUIEvent* aEvent);

  /* Temporarily ignore the Displayport for better paint performance. We
   * trigger a repaint once suppression is disabled. Without that
   * the displayport may get left at the suppressed size for an extended
   * period of time and result in unnecessary checkerboarding (see bug
   * 1255054). */
  void SuppressDisplayport(bool aEnabled);

  /* Whether or not displayport suppression should be turned on. Note that
   * this only affects the return value of |IsDisplayportSuppressed()|, and
   * doesn't change the value of the internal counter.
   */
  void RespectDisplayportSuppression(bool aEnabled);

  /* Whether or not the displayport is currently suppressed. */
  bool IsDisplayportSuppressed();

  void AddSizeOfIncludingThis(nsWindowSizes& aWindowSizes) const;

  /**
   * Methods that retrieve the cached font inflation preferences.
   */
  uint32_t FontSizeInflationEmPerLine() const {
    return mFontSizeInflationEmPerLine;
  }

  uint32_t FontSizeInflationMinTwips() const {
    return mFontSizeInflationMinTwips;
  }

  uint32_t FontSizeInflationLineThreshold() const {
    return mFontSizeInflationLineThreshold;
  }

  bool FontSizeInflationForceEnabled() const {
    return mFontSizeInflationForceEnabled;
  }

  bool FontSizeInflationDisabledInMasterProcess() const {
    return mFontSizeInflationDisabledInMasterProcess;
  }

  bool FontSizeInflationEnabled() const { return mFontSizeInflationEnabled; }

  /**
   * Recomputes whether font-size inflation is enabled.
   */
  void RecomputeFontSizeInflationEnabled();

  /**
   * Return true if the most recent interruptible reflow was interrupted.
   */
  bool IsReflowInterrupted() const { return mWasLastReflowInterrupted; }

  /**
   * Return true if the the interruptible reflows have to be suppressed.
   * This may happen only if if the most recent reflow was interrupted.
   */
  bool SuppressInterruptibleReflows() const {
    return mWasLastReflowInterrupted;
  }

  //////////////////////////////////////////////////////////////////////////////
  // Approximate frame visibility tracking public API.
  //////////////////////////////////////////////////////////////////////////////

  /**
   * Schedule an update of the list of approximately visible frames "soon".
   * This lets the refresh driver know that we want a visibility update in the
   * near future. The refresh driver applies its own heuristics and throttling
   * to decide when to actually perform the visibility update.
   */
  void ScheduleApproximateFrameVisibilityUpdateSoon();

  /**
   * Schedule an update of the list of approximately visible frames "now". The
   * update runs asynchronously, but it will be posted to the event loop
   * immediately. Prefer the "soon" variation of this method when possible, as
   * this variation ignores the refresh driver's heuristics.
   */
  void ScheduleApproximateFrameVisibilityUpdateNow();

  /**
   * Clears the current list of approximately visible frames on this pres shell
   * and replaces it with frames that are in the display list @aList.
   */
  void RebuildApproximateFrameVisibilityDisplayList(const nsDisplayList& aList);
  void RebuildApproximateFrameVisibility(nsRect* aRect = nullptr,
                                         bool aRemoveOnly = false);

  /**
   * Ensures @aFrame is in the list of approximately visible frames.
   */
  void EnsureFrameInApproximatelyVisibleList(nsIFrame* aFrame);

  /// Removes @aFrame from the list of approximately visible frames if present.
  void RemoveFrameFromApproximatelyVisibleList(nsIFrame* aFrame);

  /// Whether we should assume all frames are visible.
  bool AssumeAllFramesVisible();

  /**
   * Returns whether the document's style set's rule processor for the
   * specified level of the cascade is shared by multiple style sets.
   *
   * @param aSheetType One of the nsIStyleSheetService.*_SHEET constants.
   */
  nsresult HasRuleProcessorUsedByMultipleStyleSets(uint32_t aSheetType,
                                                   bool* aRetVal);

  /**
   * Returns whether or not the document has ever handled user input
   */
  bool HasHandledUserInput() const { return mHasHandledUserInput; }

  MOZ_CAN_RUN_SCRIPT void RunResizeSteps();
  MOZ_CAN_RUN_SCRIPT void RunScrollSteps();

  void NativeAnonymousContentWillBeRemoved(nsIContent* aAnonContent);

  /**
   * See HTMLDocument.setKeyPressEventModel() in HTMLDocument.webidl for the
   * detail.
   */
  void SetKeyPressEventModel(uint16_t aKeyPressEventModel) {
    mForceUseLegacyKeyCodeAndCharCodeValues |=
        aKeyPressEventModel ==
        dom::Document_Binding::KEYPRESS_EVENT_MODEL_SPLIT;
  }

  bool AddRefreshObserver(nsARefreshObserver* aObserver, FlushType aFlushType,
                          const char* aObserverDescription);
  bool RemoveRefreshObserver(nsARefreshObserver* aObserver,
                             FlushType aFlushType);

  bool AddPostRefreshObserver(nsAPostRefreshObserver*);
  bool AddPostRefreshObserver(mozilla::ManagedPostRefreshObserver*) = delete;
  bool RemovePostRefreshObserver(nsAPostRefreshObserver*);
  bool RemovePostRefreshObserver(mozilla::ManagedPostRefreshObserver*) = delete;

  // Represents an update to the visual scroll offset that will be sent to APZ.
  // The update type is used to determine priority compared to other scroll
  // updates.
  struct VisualScrollUpdate {
    nsPoint mVisualScrollOffset;
    FrameMetrics::ScrollOffsetUpdateType mUpdateType;
    bool mAcknowledged = false;
  };

  // Ask APZ in the next transaction to scroll to the given visual viewport
  // offset (relative to the document).
  // This is intended to be used when desired in cases where the browser
  // internally triggers scrolling; scrolling triggered explicitly by web
  // content (such as via window.scrollTo() should scroll the layout viewport
  // only).
  // If scrolling "far away", i.e. not just within the existing layout
  // viewport, it's recommended to use both ScrollContainerFrame.ScrollTo*()
  // (via window.scrollTo if calling from JS) *and* this function; otherwise,
  // temporary checkerboarding may result. If doing this:
  //   * Be sure to call ScrollTo*() first, as a subsequent layout scroll
  //     in the same transaction will cancel the pending visual scroll.
  //   * Keep in mind that ScrollTo*() can tear down the pres shell and
  //     frame tree. Depending on how the pres shell is obtained for the
  //     subsequent ScrollToVisual() call, AutoWeakFrame or similar may
  //     need to be used.
  // Please request APZ review if adding a new call site.
  void ScrollToVisual(const nsPoint& aVisualViewportOffset,
                      FrameMetrics::ScrollOffsetUpdateType aUpdateType,
                      ScrollMode aMode);
  void AcknowledgePendingVisualScrollUpdate();
  void ClearPendingVisualScrollUpdate();
  const Maybe<VisualScrollUpdate>& GetPendingVisualScrollUpdate() const {
    return mPendingVisualScrollUpdate;
  }

  nsPoint GetLayoutViewportOffset() const;
  nsSize GetLayoutViewportSize() const;

  /**
   * Documents belonging to an invisible DocShell must not be painted ever.
   */
  bool IsNeverPainting() { return mIsNeverPainting; }

  void SetNeverPainting(bool aNeverPainting) {
    mIsNeverPainting = aNeverPainting;
  }

  bool MightHavePendingFontLoads() const {
    return mNeedLayoutFlush || mNeedStyleFlush;
  }

  void SyncWindowProperties(bool aSync);
  struct WindowSizeConstraints {
    nsSize mMinSize;
    nsSize mMaxSize;
  };
  WindowSizeConstraints GetWindowSizeConstraints();

  Document* GetPrimaryContentDocument();

  struct MOZ_RAII AutoAssertNoFlush {
    explicit AutoAssertNoFlush(PresShell& aPresShell)
        : mPresShell(aPresShell), mOldForbidden(mPresShell.mForbiddenToFlush) {
      mPresShell.mForbiddenToFlush = true;
    }

    ~AutoAssertNoFlush() { mPresShell.mForbiddenToFlush = mOldForbidden; }

    PresShell& mPresShell;
    const bool mOldForbidden;
  };

  NS_IMETHOD GetSelectionFromScript(RawSelectionType aRawSelectionType,
                                    dom::Selection** aSelection) override;
  dom::Selection* GetSelection(RawSelectionType aRawSelectionType) override;

  NS_IMETHOD SetDisplaySelection(int16_t aToggle) override;
  NS_IMETHOD GetDisplaySelection(int16_t* aToggle) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD ScrollSelectionIntoView(
      RawSelectionType aRawSelectionType, SelectionRegion aRegion,
      ControllerScrollFlags aFlags) override;
  using nsISelectionController::ScrollSelectionIntoView;
  NS_IMETHOD RepaintSelection(RawSelectionType aRawSelectionType) override;
  void SelectionWillTakeFocus() override;
  void SelectionWillLoseFocus() override;

  // Implements the "focus fix-up rule". Returns true if the focus moved (in
  // which case we might need to update layout again).
  // See https://github.com/whatwg/html/issues/8225
  bool NeedsFocusFixUp() const;
  MOZ_CAN_RUN_SCRIPT bool FixUpFocus();

  /**
   * Set a "resolution" for the document, which if not 1.0 will
   * allocate more or fewer pixels for rescalable content by a factor
   * of |resolution| in both dimensions.  Return NS_OK iff the
   * resolution bounds are sane, and the resolution of this was
   * actually updated.
   *
   * Also increase the scale of the content by the same amount
   * (that's the "AndScaleTo" part).
   *
   * The resolution defaults to 1.0.
   *
   * |aOrigin| specifies who originated the resolution change. For changes
   * sent by APZ, pass ResolutionChangeOrigin::Apz. For changes sent by
   * the main thread, pass ResolutionChangeOrigin::MainThreadAdjustment (similar
   * to the |aOrigin| parameter of ScrollContainerFrame::ScrollToCSSPixels()).
   */
  nsresult SetResolutionAndScaleTo(float aResolution,
                                   ResolutionChangeOrigin aOrigin);

  ResolutionChangeOrigin GetLastResolutionChangeOrigin() {
    return mLastResolutionChangeOrigin;
  }

  // Widget notificiations
  void WindowSizeMoveDone();

  void BackingScaleFactorChanged() { mPresContext->UIResolutionChangedSync(); }

  /**
   * Does any painting work required to update retained paint state, and pushes
   * it the compositor (if any). Requests a composite, either by scheduling a
   * remote composite, or invalidating the widget so that we get a call to
   * SyncPaintFallback from the widget paint event.
   */
  MOZ_CAN_RUN_SCRIPT
  void PaintAndRequestComposite(nsView* aView, PaintFlags aFlags);

  /**
   * Does an immediate paint+composite using the FallbackRenderer (which must
   * be the current WindowRenderer for the root frame's widget).
   */
  MOZ_CAN_RUN_SCRIPT
  void SyncPaintFallback(nsView* aView);

  /**
   * Notify that we're going to call Paint with PaintFlags::PaintLayers
   * on the pres shell for a widget (which might not be this one, since
   * WillPaint is called on all presshells in the same toplevel window as the
   * painted widget). This is issued at a time when it's safe to modify
   * widget geometry.
   */
  MOZ_CAN_RUN_SCRIPT void WillPaint();
  void SchedulePaint();

  // caret handling
  NS_IMETHOD SetCaretEnabled(bool aInEnable) override;
  NS_IMETHOD SetCaretReadOnly(bool aReadOnly) override;
  NS_IMETHOD GetCaretEnabled(bool* aOutEnabled) override;
  NS_IMETHOD SetCaretVisibilityDuringSelection(bool aVisibility) override;
  NS_IMETHOD GetCaretVisible(bool* _retval) override;

  /**
   * Should the images have borders etc.  Actual visual effects are determined
   * by the frames.  Visual effects may not effect layout, only display.
   * Takes effect on next repaint, does not force a repaint itself.
   *
   * @param aFlags may be multiple of nsISelectionDisplay::DISPLAY_*.
   */
  NS_IMETHOD SetSelectionFlags(int16_t aFlags) override;
  NS_IMETHOD GetSelectionFlags(int16_t* aFlags) override;

  /**
   * Gets the current state of non text selection effects
   * @return   current state of non text selection,
   *           as set by SetDisplayNonTextSelection
   */
  int16_t GetSelectionFlags() const { return mSelectionFlags; }

  // nsISelectionController

  MOZ_CAN_RUN_SCRIPT NS_IMETHOD PhysicalMove(int16_t aDirection,
                                             int16_t aAmount,
                                             bool aExtend) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD CharacterMove(bool aForward,
                                              bool aExtend) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD WordMove(bool aForward, bool aExtend) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD LineMove(bool aForward, bool aExtend) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD IntraLineMove(bool aForward,
                                              bool aExtend) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD PageMove(bool aForward, bool aExtend) override;
  NS_IMETHOD ScrollPage(bool aForward) override;
  NS_IMETHOD ScrollLine(bool aForward) override;
  NS_IMETHOD ScrollCharacter(bool aRight) override;
  NS_IMETHOD CompleteScroll(bool aForward) override;
  MOZ_CAN_RUN_SCRIPT NS_IMETHOD CompleteMove(bool aForward,
                                             bool aExtend) override;

  // Notifies that the state of the document has changed.
  void DocumentStatesChanged(dom::DocumentState);

  // nsIDocumentObserver
  NS_DECL_NSIDOCUMENTOBSERVER_BEGINLOAD
  NS_DECL_NSIDOCUMENTOBSERVER_ENDLOAD
  NS_DECL_NSIDOCUMENTOBSERVER_CONTENTSTATECHANGED

  // nsIMutationObserver
  NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTEWILLCHANGE
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

  NS_DECL_NSIOBSERVER

  // Inline methods defined in PresShellInlines.h
  inline void EnsureStyleFlush();
  inline void EnsureLayoutFlush();
  inline void SetNeedStyleFlush();
  inline void SetNeedLayoutFlush();
  inline void SetNeedThrottledAnimationFlush();
  inline ServoStyleSet* StyleSet() const;

  /**
   * Whether we might need a flush for the given flush type.  If this
   * function returns false, we definitely don't need to flush.
   *
   * @param aFlushType The flush type to check.  This must be
   *   >= FlushType::Style.  This also returns true if a throttled
   *   animation flush is required.
   */
  bool NeedFlush(FlushType aType) const {
    MOZ_ASSERT(aType >= FlushType::Style);
    return mNeedStyleFlush || mNeedThrottledAnimationFlush ||
           (mNeedLayoutFlush && aType >= FlushType::InterruptibleLayout);
  }

  /**
   * Returns true if we might need to flush layout, even if we haven't scheduled
   * one yet (as opposed to HasPendingReflow, which returns true if a flush is
   * scheduled or will soon be scheduled).
   */
  bool NeedLayoutFlush() const { return mNeedLayoutFlush; }

  bool NeedStyleFlush() const { return mNeedStyleFlush; }

  /**
   * Flush pending notifications of the type specified.  This method
   * will not affect the content model; it'll just affect style and
   * frames. Callers that actually want up-to-date presentation (other
   * than the document itself) should probably be calling
   * Document::FlushPendingNotifications.
   *
   * This method can execute script, which can destroy this presshell object
   * unless someone is holding a reference to it on the stack.  The presshell
   * itself will ensure it lives up until the method returns, but callers who
   * plan to use the presshell after this call should hold a strong ref
   * themselves!
   *
   * @param aType the type of notifications to flush
   */
  MOZ_CAN_RUN_SCRIPT
  void FlushPendingNotifications(FlushType aType) {
    if (!NeedFlush(aType)) {
      return;
    }

    DoFlushPendingNotifications(aType);
  }

  MOZ_CAN_RUN_SCRIPT
  void FlushPendingNotifications(ChangesToFlush aType) {
    if (!NeedFlush(aType.mFlushType)) {
      return;
    }

    DoFlushPendingNotifications(aType);
  }

  /**
   * Tell the pres shell that a frame needs to be marked dirty and needs
   * Reflow.  It's OK if this is an ancestor of the frame needing reflow as
   * long as the ancestor chain between them doesn't cross a reflow root.
   *
   * The bit to add should be NS_FRAME_IS_DIRTY, NS_FRAME_HAS_DIRTY_CHILDREN
   * or nsFrameState(0); passing 0 means that dirty bits won't be set on the
   * frame or its ancestors/descendants, but that intrinsic widths will still
   * be marked dirty.  Passing aIntrinsicDirty = eResize and aBitToAdd = 0
   * would result in no work being done, so don't do that.
   */
  void FrameNeedsReflow(
      nsIFrame* aFrame, IntrinsicDirty aIntrinsicDirty, nsFrameState aBitToAdd,
      ReflowRootHandling aRootHandling = ReflowRootHandling::InferFromBitToAdd);

  /**
   * Calls FrameNeedsReflow on all fixed position children of the root frame.
   */
  void MarkFixedFramesForReflow(IntrinsicDirty aIntrinsicDirty);

  /**
   * Similar to above MarkFixedFramesForReflow, but for sticky position children
   * stuck to the root frame.
   */
  void MarkStickyFramesForReflow();

  void MaybeReflowForInflationScreenSizeChange();

  // This function handles all the work after VisualViewportSize is set
  // or reset.
  void CompleteChangeToVisualViewportSize();

  /**
   * The return value indicates whether the offset actually changed.
   */
  bool SetVisualViewportOffset(const nsPoint& aScrollOffset,
                               const nsPoint& aPrevLayoutScrollPos);

  void ResetVisualViewportOffset();
  nsPoint GetVisualViewportOffset() const {
    if (mVisualViewportOffset.isSome()) {
      return *mVisualViewportOffset;
    }
    return GetLayoutViewportOffset();
  }
  bool IsVisualViewportOffsetSet() const {
    return mVisualViewportOffset.isSome();
  }

  void SetVisualViewportSize(nscoord aWidth, nscoord aHeight);
  void ResetVisualViewportSize();
  bool IsVisualViewportSizeSet() { return mVisualViewportSizeSet; }
  nsSize GetVisualViewportSize() {
    NS_ASSERTION(mVisualViewportSizeSet,
                 "asking for visual viewport size when its not set?");
    return mVisualViewportSize;
  }

  nsPoint GetVisualViewportOffsetRelativeToLayoutViewport() const;

  // Returns state of the dynamic toolbar.
  DynamicToolbarState GetDynamicToolbarState() const {
    if (!mPresContext) {
      return DynamicToolbarState::None;
    }

    return mPresContext->GetDynamicToolbarState();
  }
  // Returns the visual viewport size during the dynamic toolbar is being
  // shown/hidden.
  nsSize GetVisualViewportSizeUpdatedByDynamicToolbar() const;

  // Trigger refreshing the MobileViewportManager's size metrics.
  void RefreshViewportSize();

  /* Enable/disable author style level. Disabling author style disables the
   * entire author level of the cascade, including the HTML preshint level.
   */
  // XXX these could easily be inlined, but there is a circular #include
  // problem with nsStyleSet.
  void SetAuthorStyleDisabled(bool aDisabled);
  bool GetAuthorStyleDisabled() const;

  // aSheetType is one of the nsIStyleSheetService *_SHEET constants.
  void NotifyStyleSheetServiceSheetAdded(StyleSheet* aSheet,
                                         uint32_t aSheetType);
  void NotifyStyleSheetServiceSheetRemoved(StyleSheet* aSheet,
                                           uint32_t aSheetType);

  // DoReflow returns whether the reflow finished without interruption
  // If aFrame is not the root frame, the caller must pass a non-null
  // aOverflowTracker.
  bool DoReflow(nsIFrame* aFrame, bool aInterruptible,
                OverflowChangedTracker* aOverflowTracker);

  /**
   * Add a solid color item to the bottom of aList with frame aFrame and bounds
   * aBounds. aBackstopColor is composed behind the background color of the
   * canvas, and it is transparent by default.
   *
   * We attempt to make the background color part of the scrolled canvas (to
   * reduce transparent layers), and if async scrolling is enabled (and the
   * background is opaque) then we add a second, unscrolled item to handle the
   * checkerboarding case.
   */
  void AddCanvasBackgroundColorItem(
      nsDisplayListBuilder* aBuilder, nsDisplayList* aList, nsIFrame* aFrame,
      const nsRect& aBounds, nscolor aBackstopColor = NS_RGBA(0, 0, 0, 0));

  size_t SizeOfTextRuns(MallocSizeOf aMallocSizeOf) const;

  static PresShell* GetShellForEventTarget(nsIFrame* aFrame,
                                           nsIContent* aContent);
  static PresShell* GetShellForTouchEvent(WidgetGUIEvent* aEvent);

  /**
   * Informs the pres shell that the document is now at the anchor with
   * the given name or range.  If |aScroll| is true, scrolls the view of the
   * document so that the anchor with the specified name is displayed at
   * the top of the window.  If |aAnchorName| is empty, then this informs
   * the pres shell that there is no current target, and |aScroll| must
   * be false.  If |aAdditionalScrollFlags| is ScrollFlags::ScrollSmoothAuto
   * and |aScroll| is true, the scrolling may be performed with an animation.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult GoToAnchor(const nsAString& aAnchorName,
                      const nsRange* aFirstTextDirective, bool aScroll,
                      ScrollFlags aAdditionalScrollFlags = ScrollFlags::None);

  /**
   * Tells the presshell to scroll again to the last anchor scrolled to by
   * GoToAnchor, if any. This scroll only happens if the scroll
   * position has not changed since the last GoToAnchor (modulo scroll anchoring
   * adjustments). This is called by nsDocumentViewer::LoadComplete. This clears
   * the last anchor scrolled to by GoToAnchor (we don't want to keep it alive
   * if it's removed from the DOM), so don't call this more than once.
   */
  MOZ_CAN_RUN_SCRIPT nsresult ScrollToAnchor();

  /**
   * When scroll anchoring adjusts positions in the root frame during page load,
   * it may move our scroll position in the root frame.
   *
   * While that's generally desirable, when scrolling to an anchor via an id-ref
   * we have a more direct target. If the id-ref points to something that cannot
   * be selected as a scroll anchor container (like an image or an inline), we
   * may select a node following it as a scroll anchor, and if then stuff is
   * inserted on top, we may end up moving the id-ref element offscreen to the
   * top inadvertently.
   *
   * On page load, the document viewer will call ScrollToAnchor(), and will only
   * scroll to the anchor again if the scroll position is not changed. We don't
   * want scroll anchoring adjustments to prevent this, so account for them.
   */
  void RootScrollFrameAdjusted(nscoord aYAdjustment) {
    if (mLastAnchorScrolledTo) {
      mLastAnchorScrollPositionY += aYAdjustment;
    }
  }

  /**
   * Scrolls the view of the document so that the primary frame of the content
   * is displayed in the window. Layout is flushed before scrolling.
   *
   * @param aContent  The content object of which primary frame should be
   *                  scrolled into view.
   * @param aVertical How to align the frame vertically and when to do so.
   *                  This is a ScrollAxis of Where and When.
   * @param aHorizontal How to align the frame horizontally and when to do so.
   *                  This is a ScrollAxis of Where and When.
   * @param aScrollFlags  If ScrollFlags::ScrollFirstAncestorOnly is set,
   *                      only the nearest scrollable ancestor is scrolled,
   *                      otherwise all scrollable ancestors may be scrolled
   *                      if necessary.  If ScrollFlags::ScrollOverflowHidden
   *                      is set then we may scroll in a direction even if
   *                      overflow:hidden is specified in that direction;
   *                      otherwise we will not scroll in that direction when
   *                      overflow:hidden is set for that direction.  If
   *                      ScrollFlags::ScrollNoParentFrames is set then we
   *                      only scroll nodes in this document, not in any
   *                      parent documents which contain this document in a
   *                      iframe or the like.  If ScrollFlags::ScrollSmooth
   *                      is set and CSSOM-VIEW scroll-behavior is enabled,
   *                      we will scroll smoothly using
   *                      ScrollContainerFrame::ScrollMode::SMOOTH_MSD;
   *                      otherwise, ScrollContainerFrame::ScrollMode::INSTANT
   *                      will be used.  If ScrollFlags::ScrollSmoothAuto is
   *                      set, the CSSOM-View scroll-behavior attribute is
   *                      set to 'smooth' on the scroll frame, and CSSOM-VIEW
   *                      scroll-behavior is enabled, we will scroll smoothly
   *                      using ScrollContainerFrame::ScrollMode::SMOOTH_MSD;
   *                      otherwise, ScrollContainerFrame::ScrollMode::INSTANT
   *                      will be used.
   *                      If ScrollFlags::AxesAreLogical is set, then the
   *                      aVertical param actually refers to the element's
   *                      block axis, and the aHorizontal param to its inline
   *                      axis, rather than to physical directions.
   */
  MOZ_CAN_RUN_SCRIPT
  nsresult ScrollContentIntoView(nsIContent* aContent, ScrollAxis aVertical,
                                 ScrollAxis aHorizontal,
                                 ScrollFlags aScrollFlags);

  /**
   * When capturing content is set, it traps all mouse events and retargets
   * them at this content node. If capturing is not allowed
   * (gCaptureInfo.mAllowed is false), then capturing is not set. However, if
   * the CaptureFlags::IgnoreAllowedState is set, the allowed state is ignored
   * and capturing is set regardless. To disable capture, pass null for the
   * value of aContent.
   *
   * If CaptureFlags::RetargetedToElement is set, all mouse events are
   * targeted at aContent only. Otherwise, mouse events are targeted at
   * aContent or its descendants. That is, descendants of aContent receive
   * mouse events as they normally would, but mouse events outside of aContent
   * are retargeted to aContent.
   *
   * If CaptureFlags::PreventDragStart is set then drags are prevented from
   * starting while this capture is active.
   *
   * If CaptureFlags::PointerLock is set, similar to
   * CaptureFlags::RetargetToElement, then events are targeted at aContent,
   * but capturing is held more strongly (i.e., calls to SetCapturingContent()
   * won't unlock unless CaptureFlags::PointerLock is set again).
   */
  static void SetCapturingContent(nsIContent* aContent, CaptureFlags aFlags,
                                  WidgetEvent* aEvent = nullptr);

  /**
   * Alias for SetCapturingContent(nullptr, CaptureFlags::None) for making
   * callers what they do clearer.
   */
  static void ReleaseCapturingContent() {
    PresShell::SetCapturingContent(nullptr, CaptureFlags::None);
  }

  static void ReleaseCapturingRemoteTarget(dom::BrowserParent* aBrowserParent) {
    MOZ_ASSERT(XRE_IsParentProcess());
    if (sCapturingContentInfo.mRemoteTarget == aBrowserParent) {
      sCapturingContentInfo.mRemoteTarget = nullptr;
    }
  }

  // Called at the end of nsLayoutUtils::PaintFrame() if we were painting to
  // the widget.
  // This is used to clear any pending visual scroll updates that have been
  // acknowledged, to make sure they don't stick around for the next paint.
  void EndPaint();

  /**
   * Tell the presshell that the given frame's reflow was interrupted.  This
   * will mark as having dirty children a path from the given frame (inclusive)
   * to the nearest ancestor with a dirty subtree, or to the reflow root
   * currently being reflowed if no such ancestor exists (inclusive).  This is
   * to be done immediately after reflow of the current reflow root completes.
   * This method must only be called during reflow, and the frame it's being
   * called on must be in the process of being reflowed when it's called.  This
   * method doesn't mark any intrinsic widths dirty and doesn't add any bits
   * other than NS_FRAME_HAS_DIRTY_CHILDREN.
   */
  void FrameNeedsToContinueReflow(nsIFrame* aFrame);

  /**
   * Notification sent by a frame informing the pres shell that it is about to
   * be destroyed.
   * This allows any outstanding references to the frame to be cleaned up
   */
  void NotifyDestroyingFrame(nsIFrame* aFrame);

  bool GetZoomableByAPZ() const;

  bool ReflowForHiddenContentIfNeeded();
  void UpdateHiddenContentInForcedLayout(nsIFrame*);
  /**
   * If this frame has content hidden via `content-visibilty` that has a pending
   * reflow, force the content to reflow immediately.
   */
  void EnsureReflowIfFrameHasHiddenContent(nsIFrame*);

  /**
   * Whether or not this presshell is  is forcing a reflow of hidden content in
   * this frame via EnsureReflowIfFrameHasHiddenContent().
   */
  bool IsForcingLayoutForHiddenContent(const nsIFrame*) const;

  void RegisterContentVisibilityAutoFrame(nsIFrame* aFrame) {
    mContentVisibilityAutoFrames.Insert(aFrame);
  }
  void UnregisterContentVisibilityAutoFrame(nsIFrame* aFrame) {
    mContentVisibilityAutoFrames.Remove(aFrame);
  }
  bool HasContentVisibilityAutoFrames() const {
    return !mContentVisibilityAutoFrames.IsEmpty();
  }

  void UpdateRelevancyOfContentVisibilityAutoFrames();
  void ScheduleContentRelevancyUpdate(ContentRelevancyReason aReason);
  void UpdateContentRelevancyImmediately(ContentRelevancyReason aReason);

  // Determination of proximity to the viewport.
  // Refer to "update the rendering: step 14", see
  // https://html.spec.whatwg.org/#update-the-rendering
  struct ProximityToViewportResult {
    bool mHadInitialDetermination = false;
    bool mAnyScrollIntoViewFlag = false;
  };
  ProximityToViewportResult DetermineProximityToViewport();

  void ClearTemporarilyVisibleForScrolledIntoViewDescendantFlags() const;

  // A cache that contains all fully selected nodes per selection instance.
  // Only non-null during reflow.
  dom::SelectionNodeCache* GetSelectionNodeCache() {
    return mSelectionNodeCache;
  }

 private:
  ~PresShell();

  void SetIsActive(bool aIsActive);
  bool ComputeActiveness() const;

  MOZ_CAN_RUN_SCRIPT
  void PaintInternal(nsView* aViewToPaint, PaintInternalFlags aFlags);

  // Refresh observer management.
  void ScheduleFlush();

  /**
   * Does the actual work of figuring out the current state of font size
   * inflation.
   */
  bool DetermineFontSizeInflationState();

  void RecordAlloc(void* aPtr) {
#ifdef DEBUG
    if (!mAllocatedPointers) {
      return;  // Hash set was presumably freed to avert OOM.
    }
    MOZ_ASSERT(!mAllocatedPointers->Contains(aPtr));
    if (!mAllocatedPointers->Insert(aPtr, fallible)) {
      // Yikes! We're nearly out of memory, and this insertion would've pushed
      // us over the ledge. At this point, we discard & stop using this set,
      // since we don't have enough memory to keep it accurate from this point
      // onwards. Hopefully this helps relieve the memory pressure a bit, too.
      mAllocatedPointers = nullptr;
    }
#endif
  }

  void RecordFree(void* aPtr) {
#ifdef DEBUG
    if (!mAllocatedPointers) {
      return;  // Hash set was presumably freed to avert OOM.
    }
    MOZ_ASSERT(mAllocatedPointers->Contains(aPtr));
    mAllocatedPointers->Remove(aPtr);
#endif
  }

  struct EventTargetInfo {
    EventTargetInfo() = default;
    EventTargetInfo(EventMessage aEventMessage, nsIFrame* aFrame,
                    nsIContent* aContent)
        : mFrame(aFrame), mContent(aContent), mEventMessage(aEventMessage) {}

    [[nodiscard]] bool IsSet() const { return mFrame || mContent; }
    void Clear() {
      mEventMessage = eVoidEvent;
      mFrame = nullptr;
      mContent = nullptr;
    }
    void ClearFrame() { mFrame = nullptr; }
    void UpdateFrameAndContent(nsIFrame* aFrame, nsIContent* aContent) {
      mFrame = aFrame;
      mContent = aContent;
    }
    void SetFrameAndContent(EventMessage aEventMessage, nsIFrame* aFrame,
                            nsIContent* aContent) {
      mEventMessage = aEventMessage;
      mFrame = aFrame;
      mContent = aContent;
    }

    nsIFrame* mFrame = nullptr;
    nsCOMPtr<nsIContent> mContent;
    EventMessage mEventMessage = eVoidEvent;
  };

  void PushCurrentEventInfo(const EventTargetInfo& aInfo);
  void PushCurrentEventInfo(EventTargetInfo&& aInfo);
  void PopCurrentEventInfo();
  nsIContent* GetCurrentEventContent();

  friend class ::nsAutoCauseReflowNotifier;

  void WillCauseReflow();
  MOZ_CAN_RUN_SCRIPT void DidCauseReflow();

  void CancelPostedReflowCallbacks();
  void FlushPendingScrollAnchorAdjustments();

  void SetPendingVisualScrollUpdate(
      const nsPoint& aVisualViewportOffset,
      FrameMetrics::ScrollOffsetUpdateType aUpdateType);

#ifdef MOZ_REFLOW_PERF
  UniquePtr<ReflowCountMgr> mReflowCountMgr;
#endif

  void WillDoReflow();

  // This data is stored as a content property (nsGkAtoms::scrolling) on
  // mContentToScrollTo when we have a pending ScrollIntoView.
  struct ScrollIntoViewData {
    ScrollAxis mContentScrollVAxis;
    ScrollAxis mContentScrollHAxis;
    ScrollFlags mContentToScrollToFlags;
  };

  static LazyLogModule gLog;

  DOMHighResTimeStamp GetPerformanceNowUnclamped();

  bool ScheduleReflowOffTimer();

  friend class ::AutoPointerEventTargetUpdater;

  // ProcessReflowCommands returns whether we processed all our dirty roots
  // without interruptions.
  MOZ_CAN_RUN_SCRIPT bool ProcessReflowCommands(bool aInterruptible);

  /**
   * Callback handler for whether reflow happened.
   *
   * @param aInterruptible Whether or not reflow interruption is allowed.
   */
  MOZ_CAN_RUN_SCRIPT void DidDoReflow(bool aInterruptible);

  MOZ_CAN_RUN_SCRIPT void HandlePostedReflowCallbacks(bool aInterruptible);

  /**
   * Helper for ScrollContentIntoView()
   */
  MOZ_CAN_RUN_SCRIPT void DoScrollContentIntoView();

  /**
   * Methods to handle changes to user and UA sheet lists that we get
   * notified about.
   */
  void AddUserSheet(StyleSheet*);
  void AddAgentSheet(StyleSheet*);
  void AddAuthorSheet(StyleSheet*);

  /**
   * Initialize cached font inflation preference values and do an initial
   * computation to determine if font inflation is enabled.
   *
   * @see nsLayoutUtils::sFontSizeInflationEmPerLine
   * @see nsLayoutUtils::sFontSizeInflationMinTwips
   * @see nsLayoutUtils::sFontSizeInflationLineThreshold
   */
  void SetupFontInflation();

  /**
   * Implementation methods for FlushPendingNotifications.
   */
  MOZ_CAN_RUN_SCRIPT void DoFlushPendingNotifications(FlushType aType);
  MOZ_CAN_RUN_SCRIPT void DoFlushPendingNotifications(ChangesToFlush aType);

  struct RenderingState {
    explicit RenderingState(PresShell* aPresShell)
        : mResolution(aPresShell->mResolution),
          mRenderingStateFlags(aPresShell->mRenderingStateFlags) {}
    Maybe<float> mResolution;
    RenderingStateFlags mRenderingStateFlags;
  };

  struct AutoSaveRestoreRenderingState {
    explicit AutoSaveRestoreRenderingState(PresShell* aPresShell)
        : mPresShell(aPresShell), mOldState(aPresShell) {}

    ~AutoSaveRestoreRenderingState() {
      mPresShell->mRenderingStateFlags = mOldState.mRenderingStateFlags;
      mPresShell->mResolution = mOldState.mResolution;
#ifdef ACCESSIBILITY
      if (nsAccessibilityService* accService = GetAccService()) {
        accService->NotifyOfResolutionChange(mPresShell,
                                             mPresShell->GetResolution());
      }
#endif
    }

    PresShell* mPresShell;
    RenderingState mOldState;
  };
  void SetRenderingState(const RenderingState& aState);

  friend class ::nsPresShellEventCB;

  // methods for painting a range to an offscreen buffer

  // given a display list, clip the items within the list to
  // the range
  nsRect ClipListToRange(nsDisplayListBuilder* aBuilder, nsDisplayList* aList,
                         nsRange* aRange);

  // create a RangePaintInfo for the range aRange containing the
  // display list needed to paint the range to a surface
  UniquePtr<RangePaintInfo> CreateRangePaintInfo(nsRange* aRange,
                                                 nsRect& aSurfaceRect,
                                                 bool aForPrimarySelection);

  /*
   * Paint the items to a new surface and return it.
   *
   * aSelection - selection being painted, if any
   * aRegion - clip region, if any
   * aArea - area that the surface occupies, relative to the root frame
   * aPoint - reference point, typically the mouse position
   * aScreenRect - [out] set to the area of the screen the painted area should
   *               be displayed at
   * aFlags - set RenderImageFlags::AutoScale to scale down large images, but
   * it must not be set if a custom image was specified
   */
  already_AddRefed<SourceSurface> PaintRangePaintInfo(
      const nsTArray<UniquePtr<RangePaintInfo>>& aItems,
      dom::Selection* aSelection, const Maybe<CSSIntRegion>& aRegion,
      nsRect aArea, const LayoutDeviceIntPoint aPoint,
      LayoutDeviceIntRect* aScreenRect, RenderImageFlags aFlags);

  // Hide a view if it is a popup
  void HideViewIfPopup(nsView* aView);

  // Utility method to restore the root scrollframe state
  void RestoreRootScrollPosition();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void MaybeReleaseCapturingContent();

  class DelayedEvent {
   public:
    virtual ~DelayedEvent() = default;
    virtual void Dispatch() {}
    virtual bool IsKeyPressEvent() { return false; }
  };

  class DelayedInputEvent : public DelayedEvent {
   public:
    void Dispatch() override;

   protected:
    DelayedInputEvent();
    ~DelayedInputEvent() override;

    WidgetInputEvent* mEvent;
  };

  class DelayedMouseEvent : public DelayedInputEvent {
   public:
    explicit DelayedMouseEvent(WidgetMouseEvent* aEvent);
  };

  class DelayedPointerEvent : public DelayedInputEvent {
   public:
    explicit DelayedPointerEvent(WidgetPointerEvent* aEvent);
  };

  class DelayedKeyEvent : public DelayedInputEvent {
   public:
    explicit DelayedKeyEvent(WidgetKeyboardEvent* aEvent);
    bool IsKeyPressEvent() override;
  };

  /**
   * return the nsPoint represents the location of the mouse event relative to
   * the root document in visual coordinates
   */
  nsPoint GetEventLocation(const WidgetMouseEvent& aEvent) const;

  // Check if aEvent is a mouse event and record the mouse location for later
  // synth mouse moves.
  void RecordPointerLocation(WidgetGUIEvent* aEvent);
  inline bool MouseLocationWasSetBySynthesizedMouseEventForTests() const;
  class nsSynthMouseMoveEvent final : public nsARefreshObserver {
   public:
    nsSynthMouseMoveEvent(PresShell* aPresShell, bool aFromScroll)
        : mPresShell(aPresShell), mFromScroll(aFromScroll) {
      NS_ASSERTION(mPresShell, "null parameter");
    }

   private:
    // Private destructor, to discourage deletion outside of Release():
    ~nsSynthMouseMoveEvent() { Revoke(); }

   public:
    NS_INLINE_DECL_REFCOUNTING(nsSynthMouseMoveEvent, override)

    void Revoke();

    MOZ_CAN_RUN_SCRIPT
    void WillRefresh(TimeStamp aTime) override { Run(); }

    MOZ_CAN_RUN_SCRIPT void Run() {
      if (mPresShell) {
        RefPtr<PresShell> shell = mPresShell;
        shell->ProcessSynthMouseMoveEvent(mFromScroll);
      }
    }

   private:
    PresShell* mPresShell;
    bool mFromScroll;
  };
  MOZ_CAN_RUN_SCRIPT void ProcessSynthMouseMoveEvent(bool aFromScroll);

  void UpdateImageLockingState();

  already_AddRefed<PresShell> GetParentPresShellForEventHandling();

  /**
   * Return a frame for a view which is the closest ancestor view which has
   * a frame.  I.e., if the closest ancestor view does not have a frame,
   * this returns a frame for the next closest ancestor view.
   */
  [[nodiscard]] nsIFrame* GetClosestAncestorFrameForAncestorView() const;

  /**
   * EventHandler is implementation of PresShell::HandleEvent().
   */
  class MOZ_STACK_CLASS EventHandler final {
   public:
    EventHandler() = delete;
    EventHandler(const EventHandler& aOther) = delete;
    explicit EventHandler(PresShell& aPresShell)
        : mPresShell(aPresShell), mCurrentEventInfoSetter(nullptr) {}
    explicit EventHandler(RefPtr<PresShell>&& aPresShell)
        : mPresShell(std::move(aPresShell)), mCurrentEventInfoSetter(nullptr) {}

    /**
     * HandleEvent() may dispatch aGUIEvent.  This may redirect the event to
     * another PresShell, or the event may be handled by other classes like
     * AccessibleCaretEventHub, or discarded.  Otherwise, this sets current
     * event info of mPresShell and calls HandleEventWithCurrentEventInfo()
     * to dispatch the event into the DOM tree.
     *
     * @param aWeakFrameForPresShell    The frame for PresShell.  If PresShell
     *                                  has root frame, it should be set.
     *                                  Otherwise, a frame which contains the
     *                                  PresShell should be set instead.  I.e.,
     *                                  in the latter case, the frame is in
     *                                  a parent document.
     * @param aGUIEvent                 Event to be handled.  Must be a trusted
     *                                  event.
     * @param aDontRetargetEvents       true if this shouldn't redirect the
     *                                  event to different PresShell.
     *                                  false if this can redirect the event to
     *                                  different PresShell.
     * @param aEventStatus              [in/out] EventStatus of aGUIEvent.
     */
    MOZ_CAN_RUN_SCRIPT nsresult HandleEvent(
        AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
        bool aDontRetargetEvents, nsEventStatus* aEventStatus);

    /**
     * HandleEventWithTarget() tries to dispatch aEvent on aContent after
     * setting current event target content to aNewEventContent and current
     * event frame to aNewEventFrame temporarily.  Note that this supports
     * WidgetEvent, not WidgetGUIEvent.  So, you can dispatch a simple event
     * with this.
     *
     * @param aEvent                    Event to be dispatched.  Must be a
     *                                  trusted event.
     * @param aNewEventFrame            Temporal new event frame.
     * @param aNewEventContent          Temporal new event content.
     * @param aEventStatus              [in/out] EventStuatus of aEvent.
     * @param aIsHandlingNativeEvent    true if aEvent represents a native
     *                                  event.
     * @param aTargetContent            This is used only when aEvent is a
     *                                  pointer event.  If
     *                                  PresShell::mPointerEventTarget is
     *                                  changed during dispatching aEvent,
     *                                  this is set to the new target.
     * @param aOverrideClickTarget      Override click event target.
     */
    MOZ_CAN_RUN_SCRIPT
    nsresult HandleEventWithTarget(WidgetEvent* aEvent,
                                   nsIFrame* aNewEventFrame,
                                   nsIContent* aNewEventContent,
                                   nsEventStatus* aEventStatus,
                                   bool aIsHandlingNativeEvent,
                                   nsIContent** aTargetContent,
                                   nsIContent* aOverrideClickTarget);

    /**
     * OnPresShellDestroy() is called when every PresShell instance is being
     * destroyed.
     */
    static inline void OnPresShellDestroy(Document* aDocument);

   private:
    static bool InZombieDocument(nsIContent* aContent);
    static nsIPrincipal* GetDocumentPrincipalToCompareWithBlacklist(
        PresShell& aPresShell);

    /**
     * HandleEventUsingCoordinates() handles aGUIEvent whose
     * IsUsingCoordinates() returns true with the following helper methods.
     *
     * @param aWeakFrameForPresShell    The frame for PresShell.  See
     *                                  explanation of HandleEvent() for the
     *                                  details.
     * @param aGUIEvent                 The handling event.  Make sure that
     *                                  its IsUsingCoordinates() returns true.
     * @param aEventStatus              The status of aGUIEvent.
     * @param aDontRetargetEvents       true if we've already retarget document.
     *                                  Otherwise, false.
     */
    MOZ_CAN_RUN_SCRIPT nsresult HandleEventUsingCoordinates(
        AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
        nsEventStatus* aEventStatus, bool aDontRetargetEvents);

    /**
     * EventTargetData struct stores a set of a PresShell (event handler),
     * a frame (to handle the event) and a content (event target for the frame).
     */
    struct MOZ_STACK_CLASS EventTargetData final {
      EventTargetData() = delete;
      EventTargetData(const EventTargetData& aOther) = delete;
      explicit EventTargetData(nsIFrame* aFrameToHandleEvent) {
        SetFrameAndComputePresShell(aFrameToHandleEvent);
      }

      void SetFrameAndComputePresShell(nsIFrame* aFrameToHandleEvent);
      void SetFrameAndComputePresShellAndContent(nsIFrame* aFrameToHandleEvent,
                                                 WidgetGUIEvent* aGUIEvent);
      void SetContentForEventFromFrame(WidgetGUIEvent* aGUIEvent);

      nsPresContext* GetPresContext() const {
        return mPresShell ? mPresShell->GetPresContext() : nullptr;
      };
      EventStateManager* GetEventStateManager() const {
        nsPresContext* presContext = GetPresContext();
        return presContext ? presContext->EventStateManager() : nullptr;
      }
      Document* GetDocument() const {
        return mPresShell ? mPresShell->GetDocument() : nullptr;
      }

      /**
       * Return content of the frame if and only if a frame is set.
       * I.e., this may return non-element node even when GetContent() returns
       * an element node.
       */
      nsIContent* GetFrameContent() const;

      nsIFrame* GetFrame() const { return mFrame; }
      nsIContent* GetContent() const { return mContent; }

      /**
       * Set the event target content and the topmost frame at the event point.
       * This checks whether the relation is correct if aContent is not nullptr.
       * If you set aGUIEvent, the check is done with strict way, but otherwise,
       * it checks whether aContent is a proper inclusive ancestor of
       * mFrame->GetContent() or not.
       */
      void SetFrameAndContent(nsIFrame* aFrame, nsIContent* aContent = nullptr,
                              const WidgetGUIEvent* aGUIEvent = nullptr) {
        mFrame = aFrame;
        mContent = aContent ? aContent : GetFrameContent();
        AssertIfEventTargetContentAndFrameContentMismatch(aGUIEvent);
      }

      /**
       * Set the event target content and clear the frame.
       */
      void SetContent(nsIContent* aContent) {
        mContent = aContent;
        if (mFrame && GetFrameContent() != aContent) {
          mFrame = nullptr;
        }
      }

      /**
       * MaybeRetargetToActiveDocument() tries retarget aGUIEvent into
       * active document if there is.  Note that this does not support to
       * retarget mContent.  Make sure it is nullptr before calling this.
       *
       * @param aGUIEvent       The handling event.
       * @return                true if retargetted.
       */
      bool MaybeRetargetToActiveDocument(WidgetGUIEvent* aGUIEvent);

      /**
       * ComputeElementFromFrame() computes mContent for aGUIEvent.  If
       * mContent is set by this method, mContent is always nullptr or an
       * Element.
       *
       * @param aGUIEvent       The handling event.
       * @return                true if caller can keep handling the event.
       *                        Otherwise, false.
       *                        Note that even if this returns true, mContent
       *                        may be nullptr.
       */
      bool ComputeElementFromFrame(WidgetGUIEvent* aGUIEvent);

      /**
       * UpdateTouchEventTarget() updates mFrame, mPresShell and mContent if
       * aGUIEvent is a touch event and there is new proper target.
       *
       * @param aGUIEvent       The handled event.  If it's not a touch event,
       *                        this method does nothing.
       */
      void UpdateTouchEventTarget(WidgetGUIEvent* aGUIEvent);

      /**
       * UpdateWheelEventTarget() updates mFrame, mPresShell, and mContent if
       * aGUIEvent is a wheel event and aGUIEvent should be grouped with prior
       * wheel events.
       *
       * @param aGUIEvent       The handled event.  If it's not a wheel event,
       *                        this method does nothing.
       */
      void UpdateWheelEventTarget(WidgetGUIEvent* aGUIEvent);

     private:
      void AssertIfEventTargetContentAndFrameContentMismatch(
          const WidgetGUIEvent* aGUIEvent = nullptr) const;

     public:
      RefPtr<PresShell> mPresShell;
      nsCOMPtr<nsIContent> mOverrideClickTarget;

     private:
      nsIFrame* mFrame = nullptr;
      // mContent is the event target content for mFrame->GetContent().
      // This may be nullptr even if mFrame is not nullptr.
      // This may be an ancestor element of mFrame->GetContent() or native
      // anonymous root content parent.
      // This may be not an ancestor element of mFrame->GetContent() if
      // mFrame->GetContentForEvent() returns such element. E.g., clicking in
      // <area>, mContent is the <area> but mFrame->GetContent() is an <img>.
      nsCOMPtr<nsIContent> mContent;
    };

    /**
     * MaybeFlushPendingNotifications() maybe flush pending notifications if
     * aGUIEvent should be handled with the latest layout.
     *
     * @param aGUIEvent                 The handling event.
     * @return                          true if this actually flushes pending
     *                                  layout and that has caused changing the
     *                                  layout.
     */
    MOZ_CAN_RUN_SCRIPT
    bool MaybeFlushPendingNotifications(WidgetGUIEvent* aGUIEvent);

    /**
     * GetFrameToHandleNonTouchEvent() returns a frame to handle the event.
     * This may flush pending layout if the target is in child PresShell.
     *
     * @param aWeakRootFrameToHandleEvent   The root frame to handle the event.
     * @param aGUIEvent                 The handling event.
     * @return                          The frame which should handle the
     *                                  event.  nullptr if the caller should
     *                                  stop handling the event.
     */
    MOZ_CAN_RUN_SCRIPT nsIFrame* GetFrameToHandleNonTouchEvent(
        AutoWeakFrame& aWeakRootFrameToHandleEvent, WidgetGUIEvent* aGUIEvent);

    /**
     * ComputeEventTargetFrameAndPresShellAtEventPoint() computes event
     * target frame at the event point of aGUIEvent and set it to
     * aEventTargetData.
     *
     * @param aWeakRootFrameToHandleEvent   The root frame to handle aGUIEvent.
     * @param aGUIEvent                 The handling event.
     * @param aEventTargetData          [out] Its frame and PresShell will
     *                                  be set.
     * @return                          true if the caller can handle the
     *                                  event.  Otherwise, false.
     */
    MOZ_CAN_RUN_SCRIPT bool ComputeEventTargetFrameAndPresShellAtEventPoint(
        AutoWeakFrame& aWeakRootFrameToHandleEvent, WidgetGUIEvent* aGUIEvent,
        EventTargetData* aEventTargetData);

    /**
     * DispatchPrecedingPointerEvent() dispatches preceding pointer event for
     * aGUIEvent if Pointer Events is enabled.
     *
     * @param aWeakFrameForPresShell    The frame for PresShell.  See
     *                                  explanation of HandleEvent() for the
     *                                  details.
     * @param aGUIEvent                 The handled event.
     * @param aPointerCapturingContent  The content which is capturing pointer
     *                                  events if there is.  Otherwise, nullptr.
     * @param aDontRetargetEvents       Set aDontRetargetEvents of
     *                                  HandleEvent() which called this method.
     * @param aEventTargetData          [in/out] Event target data of
     *                                  aGUIEvent.  If pointer event listeners
     *                                  change the DOM tree or reframe the
     *                                  target, updated by this method.
     * @param aEventStatus              [in/out] The event status of aGUIEvent.
     * @return                          true if the caller can handle the
     *                                  event.  Otherwise, false.
     */
    MOZ_CAN_RUN_SCRIPT bool DispatchPrecedingPointerEvent(
        AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
        nsIContent* aPointerCapturingContent, bool aDontRetargetEvents,
        EventTargetData* aEventTargetData, nsEventStatus* aEventStatus);

    /**
     * MaybeDiscardEvent() checks whether it's safe to handle aGUIEvent right
     * now.  If it's not safe, this may notify somebody of discarding event if
     * necessary.
     *
     * @param aGUIEvent   Handling event.
     * @return            true if it's not safe to handle the event.
     */
    bool MaybeDiscardEvent(WidgetGUIEvent* aGUIEvent);

    /**
     * GetCapturingContentFor() returns capturing content for aGUIEvent.
     * If aGUIEvent is not related to capturing, this returns nullptr.
     */
    static nsIContent* GetCapturingContentFor(WidgetGUIEvent* aGUIEvent);

    /**
     * GetRetargetEventDocument() returns a document if aGUIEvent should be
     * handled in another document.
     *
     * @param aGUIEvent                 Handling event.
     * @param aRetargetEventDocument    Document which should handle aGUIEvent.
     * @return                          true if caller can keep handling
     *                                  aGUIEvent.
     */
    bool GetRetargetEventDocument(WidgetGUIEvent* aGUIEvent,
                                  Document** aRetargetEventDocument);

    /**
     * GetFrameForHandlingEventWith() returns a frame which should be used as
     * aFrameForPresShell of HandleEvent().  See @return for the details.
     *
     * @param aGUIEvent                 Handling event.
     * @param aRetargetDocument         Document which aGUIEvent should be
     *                                  fired on.  Typically, should be result
     *                                  of GetRetargetEventDocument().
     * @param aFrameForPresShell        The frame for PresShell.  See
     *                                  explanation of HandleEvent() for the
     *                                  details.
     * @return                          nullptr if caller should stop handling
     *                                  the event.
     *                                  aFrameForPresShell if caller should
     *                                  keep handling the event by itself.
     *                                  Otherwise, caller should handle it with
     *                                  another PresShell which is result of
     *                                  nsIFrame::PresContext()->GetPresShell().
     */
    nsIFrame* GetFrameForHandlingEventWith(WidgetGUIEvent* aGUIEvent,
                                           Document* aRetargetDocument,
                                           nsIFrame* aFrameForPresShell);

    /**
     * MaybeHandleEventWithAnotherPresShell() may handle aGUIEvent with another
     * PresShell.
     *
     * @param aWeakFrameForPresShell    The frame for PresShell.  See
     *                                  explanation of HandleEvent() for the
     *                                  details.
     * @param aGUIEvent                 Handling event.
     * @param aEventStatus              [in/out] EventStatus of aGUIEvent.
     * @param aRv                       [out] Returns error if this gets an
     *                                  error handling the event.
     * @return                          false if caller needs to keep handling
     *                                  the event by itself.
     *                                  true if caller shouldn't keep handling
     *                                  the event.  Note that when no PresShell
     *                                  can handle the event, this returns true.
     */
    MOZ_CAN_RUN_SCRIPT bool MaybeHandleEventWithAnotherPresShell(
        AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
        nsEventStatus* aEventStatus, nsresult* aRv);

    MOZ_CAN_RUN_SCRIPT
    nsresult RetargetEventToParent(WidgetGUIEvent* aGUIEvent,
                                   nsEventStatus* aEventStatus);

    /**
     * MaybeHandleEventWithAccessibleCaret() may handle aGUIEvent with
     * AccessibleCaretEventHub if it's necessary.
     *
     * @param aWeakFrameForPresShell
     *                          The frame for PresShell. See explanation of
     *                          HandleEvent() for the details.
     * @param aGUIEvent         Event may be handled by AccessibleCaretEventHub.
     * @param aEventStatus      [in/out] EventStatus of aGUIEvent.
     * @return                  true if AccessibleCaretEventHub handled the
     *                          event and caller shouldn't keep handling it.
     */
    MOZ_CAN_RUN_SCRIPT bool MaybeHandleEventWithAccessibleCaret(
        AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
        nsEventStatus* aEventStatus);

    /**
     * Maybe dispatch mouse events for aTouchEnd.  This should be called after
     * aTouchEndEvent is dispatched into the DOM.
     */
    MOZ_CAN_RUN_SCRIPT void MaybeSynthesizeCompatMouseEventsForTouchEnd(
        const WidgetTouchEvent* aTouchEndEvent,
        const nsEventStatus* aStatus) const;

    /**
     * MaybeDiscardOrDelayKeyboardEvent() may discared or put aGUIEvent into
     * the delayed event queue if it's a keyboard event and if we should do so.
     * If aGUIEvent is not a keyboard event, this does nothing.
     *
     * @param aGUIEvent         The handling event.
     * @return                  true if this method discard the event or
     *                          put it into the delayed event queue.
     */
    bool MaybeDiscardOrDelayKeyboardEvent(WidgetGUIEvent* aGUIEvent);

    /**
     * MaybeDiscardOrDelayMouseEvent() may discard or put aGUIEvent into the
     * delayed event queue if it's a mouse event and if we should do so.
     * If aGUIEvent is not a mouse event, this does nothing.
     * If there is suppressed event listener like debugger of devtools, this
     * notifies it of the event after discard or put it into the delayed
     * event queue.
     *
     * @param aFrameToHandleEvent       The frame to handle aGUIEvent.
     * @param aGUIEvent                 The handling event.
     * @return                          true if this method discard the event
     *                                  or put it into the delayed event queue.
     */
    bool MaybeDiscardOrDelayMouseEvent(nsIFrame* aFrameToHandleEvent,
                                       WidgetGUIEvent* aGUIEvent);

    /**
     * MaybeFlushThrottledStyles() tries to flush pending animation.  If it's
     * flushed and then aWeakFrameForPresShell is reframed, this updates it to
     * track the new frame (or keep nullptr if it's not available anymore).
     *
     * @param aWeakFrameForPresShell    The frame for PresShell.  See
     *                                  explanation of HandleEvent() for the
     *                                  details.  This can be nullptr.
     * @return                          Maybe new frame for mPresShell.
     *                                  If aFrameForPresShell is not nullptr
     *                                  and hasn't been destroyed, returns
     *                                  aFrameForPresShell as-is.
     */
    MOZ_CAN_RUN_SCRIPT void MaybeFlushThrottledStyles(
        AutoWeakFrame& aWeakFrameForPresShell);

    /**
     * ComputeRootFrameToHandleEvent() returns root frame to handle the event.
     * For example, if there is a popup, this returns the popup frame.
     * If there is capturing content and it's in a scrolled frame, returns
     * the scrolled frame.
     *
     * @param aFrameForPresShell                The frame for PresShell.  See
     *                                          explanation of HandleEvent() for
     *                                          the details.
     * @param aGUIEvent                         The handling event.
     * @param aCapturingContent                 Capturing content if there is.
     *                                          nullptr, otherwise.
     * @param aIsCapturingContentIgnored        [out] true if aCapturingContent
     *                                          is not nullptr but it should be
     *                                          ignored to handle the event.
     * @param aIsCaptureRetargeted              [out] true if aCapturingContent
     *                                          is not nullptr but it's
     *                                          retargeted.
     * @return                                  Root frame to handle the event.
     */
    nsIFrame* ComputeRootFrameToHandleEvent(nsIFrame* aFrameForPresShell,
                                            WidgetGUIEvent* aGUIEvent,
                                            nsIContent* aCapturingContent,
                                            bool* aIsCapturingContentIgnored,
                                            bool* aIsCaptureRetargeted);

    /**
     * ComputeRootFrameToHandleEventWithPopup() returns popup frame if there
     * is a popup and we should handle the event in it.  Otherwise, returns
     * aRootFrameToHandleEvent.
     *
     * @param aRootFrameToHandleEvent           Candidate root frame to handle
     *                                          the event.
     * @param aGUIEvent                         The handling event.
     * @param aCapturingContent                 Capturing content if there is.
     *                                          nullptr, otherwise.
     * @param aIsCapturingContentIgnored        [out] true if aCapturingContent
     *                                          is not nullptr but it should be
     *                                          ignored to handle the event.
     * @return                                  A popup frame if there is a
     *                                          popup and we should handle the
     *                                          event in it.  Otherwise,
     *                                          aRootFrameToHandleEvent.
     *                                          I.e., never returns nullptr.
     */
    nsIFrame* ComputeRootFrameToHandleEventWithPopup(
        nsIFrame* aRootFrameToHandleEvent, WidgetGUIEvent* aGUIEvent,
        nsIContent* aCapturingContent, bool* aIsCapturingContentIgnored);

    /**
     * ComputeRootFrameToHandleEventWithCapturingContent() returns root frame
     * to handle event for the capturing content, or aRootFrameToHandleEvent
     * if it should be ignored.
     *
     * @param aRootFrameToHandleEvent           Candidate root frame to handle
     *                                          the event.
     * @param aCapturingContent                 Capturing content.  nullptr is
     *                                          not allowed.
     * @param aIsCapturingContentIgnored        [out] true if aCapturingContent
     *                                          is not nullptr but it should be
     *                                          ignored to handle the event.
     * @param aIsCaptureRetargeted              [out] true if aCapturingContent
     *                                          is not nullptr but it's
     *                                          retargeted.
     * @return                                  A popup frame if there is a
     *                                          popup and we should handle the
     *                                          event in it.  Otherwise,
     *                                          aRootFrameToHandleEvent.
     *                                          I.e., never returns nullptr.
     */
    nsIFrame* ComputeRootFrameToHandleEventWithCapturingContent(
        nsIFrame* aRootFrameToHandleEvent, nsIContent* aCapturingContent,
        bool* aIsCapturingContentIgnored, bool* aIsCaptureRetargeted);

    /**
     * HandleEventWithPointerCapturingContentWithoutItsFrame() handles
     * aGUIEvent with aPointerCapturingContent when it does not have primary
     * frame.
     *
     * @param aWeakFrameForPresShell    The frame for PresShell.  See
     *                                  explanation of HandleEvent() for the
     *                                  details.
     * @param aGUIEvent                 The handling event.
     * @param aPointerCapturingContent  Current pointer capturing content.
     *                                  Must not be nullptr.
     * @param aEventStatus              [in/out] The event status of aGUIEvent.
     * @return                          Basically, result of
     *                                  HandleEventWithTarget().
     */
    MOZ_CAN_RUN_SCRIPT nsresult
    HandleEventWithPointerCapturingContentWithoutItsFrame(
        AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
        nsIContent* aPointerCapturingContent, nsEventStatus* aEventStatus);

    /**
     * HandleEventAtFocusedContent() handles aGUIEvent at focused content.
     *
     * @param aGUIEvent         The handling event which should be handled at
     *                          focused content.
     * @param aEventStatus      [in/out] The event status of aGUIEvent.
     */
    MOZ_CAN_RUN_SCRIPT
    nsresult HandleEventAtFocusedContent(WidgetGUIEvent* aGUIEvent,
                                         nsEventStatus* aEventStatus);

    /**
     * ComputeFocusedEventTargetElement() returns event target element for
     * aGUIEvent which should be handled with focused content.
     * This may set/unset sLastKeyDownEventTarget if necessary.
     *
     * @param aGUIEvent                 The handling event.
     * @return                          The element which should be the event
     *                                  target of aGUIEvent.
     */
    dom::Element* ComputeFocusedEventTargetElement(WidgetGUIEvent* aGUIEvent);

    /**
     * MaybeHandleEventWithAnotherPresShell() may handle aGUIEvent with another
     * PresShell.
     *
     * @param aEventTargetElement       The event target element of aGUIEvent.
     * @param aGUIEvent                 Handling event.
     * @param aEventStatus              [in/out] EventStatus of aGUIEvent.
     * @param aRv                       [out] Returns error if this gets an
     *                                  error handling the event.
     * @return                          false if caller needs to keep handling
     *                                  the event by itself.
     *                                  true if caller shouldn't keep handling
     *                                  the event.  Note that when no PresShell
     *                                  can handle the event, this returns true.
     */
    MOZ_CAN_RUN_SCRIPT
    bool MaybeHandleEventWithAnotherPresShell(dom::Element* aEventTargetElement,
                                              WidgetGUIEvent* aGUIEvent,
                                              nsEventStatus* aEventStatus,
                                              nsresult* aRv);

    /**
     * HandleRetargetedEvent() dispatches aGUIEvent on the PresShell without
     * retargetting.  This should be used only when caller computes final
     * target of aGUIEvent.
     *
     * @param aGUIEvent         Event to be dispatched.
     * @param aEventStatus      [in/out] EventStatus of aGUIEvent.
     * @param aTarget           The final target of aGUIEvent.
     */
    MOZ_CAN_RUN_SCRIPT
    nsresult HandleRetargetedEvent(WidgetGUIEvent* aGUIEvent,
                                   nsEventStatus* aEventStatus,
                                   nsIContent* aTarget) {
      AutoCurrentEventInfoSetter eventInfoSetter(
          *this, EventTargetInfo(aGUIEvent->mMessage, nullptr, aTarget));
      if (!mPresShell->GetCurrentEventFrame()) {
        return NS_OK;
      }
      nsCOMPtr<nsIContent> overrideClickTarget;
      return HandleEventWithCurrentEventInfo(aGUIEvent, aEventStatus, true,
                                             overrideClickTarget);
    }

    /**
     * HandleEventWithFrameForPresShell() handles aGUIEvent with the frame
     * for mPresShell.
     *
     * @param aWeakFrameForPresShell    The frame for mPresShell.
     * @param aGUIEvent                 The handling event.  It shouldn't be
     *                                  handled with using coordinates nor
     *                                  handled at focused content.
     * @param aEventStatus              [in/out] The status of aGUIEvent.
     */
    MOZ_CAN_RUN_SCRIPT nsresult HandleEventWithFrameForPresShell(
        AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
        nsEventStatus* aEventStatus);

    /**
     * HandleEventWithCurrentEventInfo() prepares to dispatch aEvent into the
     * DOM, dispatches aEvent into the DOM with using current event info of
     * mPresShell and notifies EventStateManager of that.
     *
     * @param aEvent                    Event to be dispatched.
     * @param aEventStatus              [in/out] EventStatus of aEvent.
     * @param aIsHandlingNativeEvent    true if aGUIEvent represents a native
     *                                  event.
     * @param aOverrideClickTarget      Override click event target.
     */
    MOZ_CAN_RUN_SCRIPT
    nsresult HandleEventWithCurrentEventInfo(WidgetEvent* aEvent,
                                             nsEventStatus* aEventStatus,
                                             bool aIsHandlingNativeEvent,
                                             nsIContent* aOverrideClickTarget);

    /**
     * HandlingTimeAccumulator() may accumulate handling time of telemetry
     * for each type of events.
     */
    class MOZ_STACK_CLASS HandlingTimeAccumulator final {
     public:
      HandlingTimeAccumulator() = delete;
      HandlingTimeAccumulator(const HandlingTimeAccumulator& aOther) = delete;
      HandlingTimeAccumulator(const EventHandler& aEventHandler,
                              const WidgetEvent* aEvent);
      ~HandlingTimeAccumulator();

     private:
      const EventHandler& mEventHandler;
      const WidgetEvent* mEvent;
      TimeStamp mHandlingStartTime;
    };

    /**
     * RecordEventPreparationPerformance() records event preparation performance
     * with telemetry only when aEvent is a trusted event.
     *
     * @param aEvent            The handling event which we've finished
     *                          preparing something to dispatch.
     */
    void RecordEventPreparationPerformance(const WidgetEvent* aEvent);

    /**
     * RecordEventHandlingResponsePerformance() records event handling response
     * performance with telemetry.
     *
     * @param aEvent            The handled event.
     */
    void RecordEventHandlingResponsePerformance(const WidgetEvent* aEvent);

    /**
     * PrepareToDispatchEvent() prepares to dispatch aEvent.
     *
     * @param aEvent                    The handling event.
     * @param aEventStatus              [in/out] The status of aEvent.
     * @param aTouchIsNew               [out] Set to true if the event is an
     *                                  eTouchMove event and it represents new
     *                                  touch.  Otherwise, set to false.
     * @return                          true if the caller can dispatch the
     *                                  event into the DOM.
     */
    MOZ_CAN_RUN_SCRIPT
    bool PrepareToDispatchEvent(WidgetEvent* aEvent,
                                nsEventStatus* aEventStatus, bool* aTouchIsNew);

    /**
     * MaybeHandleKeyboardEventBeforeDispatch() may handle aKeyboardEvent
     * if it should do something before dispatched into the DOM.
     *
     * @param aKeyboardEvent    The handling keyboard event.
     */
    MOZ_CAN_RUN_SCRIPT
    void MaybeHandleKeyboardEventBeforeDispatch(
        WidgetKeyboardEvent* aKeyboardEvent);

    /**
     * This and the next two helper methods are used to target and position the
     * context menu when the keyboard shortcut is used to open it.
     *
     * If another menu is open, the context menu is opened relative to the
     * active menuitem within the menu, or the menu itself if no item is active.
     * Otherwise, if the caret is visible, the menu is opened near the caret.
     * Otherwise, if a selectable list such as a listbox is focused, the
     * current item within the menu is opened relative to this item.
     * Otherwise, the context menu is opened at the topleft corner of the
     * view.
     *
     * Returns true if the context menu event should fire and false if it should
     * not.
     */
    MOZ_CAN_RUN_SCRIPT
    bool AdjustContextMenuKeyEvent(WidgetMouseEvent* aMouseEvent);

    MOZ_CAN_RUN_SCRIPT
    bool PrepareToUseCaretPosition(nsIWidget* aEventWidget,
                                   LayoutDeviceIntPoint& aTargetPt);

    /**
     * Get the selected item and coordinates in device pixels relative to root
     * document's root view for element, first ensuring the element is onscreen.
     */
    MOZ_CAN_RUN_SCRIPT
    void GetCurrentItemAndPositionForElement(dom::Element* aFocusedElement,
                                             nsIContent** aTargetToUse,
                                             LayoutDeviceIntPoint& aTargetPt,
                                             nsIWidget* aRootWidget);

    /**
     * Return the override click target if there is for aGUIEvent.  Otherwise,
     * nullptr. And this may return error if the caller shouldn't keep handling
     * the event.
     */
    [[nodiscard]] Result<nsIContent*, nsresult> GetOverrideClickTarget(
        WidgetGUIEvent* aGUIEvent, nsIFrame* aFrameForPresShell);

    /**
     * DispatchEvent() tries to dispatch aEvent and notifies aEventStateManager
     * of doing it.
     *
     * @param aEventStateManager        EventStateManager which should handle
     *                                  the event before/after dispatching
     *                                  aEvent into the DOM.
     * @param aEvent                    The handling event.
     * @param aTouchIsNew               Set this to true when the message is
     *                                  eTouchMove and it's newly touched.
     *                                  Then, the "touchmove" event becomes
     *                                  cancelable.
     * @param aEventStatus              [in/out] The status of aEvent.
     * @param aOverrideClickTarget      Override click event target.
     */
    MOZ_CAN_RUN_SCRIPT nsresult
    DispatchEvent(EventStateManager* aEventStateManager, WidgetEvent* aEvent,
                  bool aTouchIsNew, nsEventStatus* aEventStatus,
                  nsIContent* aOverrideClickTarget);

    /**
     * DispatchEventToDOM() actually dispatches aEvent into the DOM tree.
     *
     * @param aEvent            Event to be dispatched into the DOM tree.
     * @param aEventStatus      [in/out] EventStatus of aEvent.
     * @param aEventCB          The callback kicked when the event moves
     *                          from the default group to the system group.
     */
    MOZ_CAN_RUN_SCRIPT nsresult
    DispatchEventToDOM(WidgetEvent* aEvent, nsEventStatus* aEventStatus,
                       nsPresShellEventCB* aEventCB);

    /**
     * DispatchTouchEventToDOM() dispatches touch events into the DOM tree.
     *
     * @param aEvent            The source of events to be dispatched into the
     *                          DOM tree.
     * @param aEventStatus      [in/out] EventStatus of aEvent.
     * @param aEventCB          The callback kicked when the events move
     *                          from the default group to the system group.
     * @param aTouchIsNew       Set this to true when the message is eTouchMove
     *                          and it's newly touched.  Then, the "touchmove"
     *                          event becomes cancelable.
     */
    MOZ_CAN_RUN_SCRIPT void DispatchTouchEventToDOM(
        WidgetEvent* aEvent, nsEventStatus* aEventStatus,
        nsPresShellEventCB* aEventCB, bool aTouchIsNew);

    /**
     * FinalizeHandlingEvent() should be called after calling DispatchEvent()
     * and then, this cleans up the state of mPresShell and aEvent.
     *
     * @param aEvent            The handled event.
     * @param aStatus           The status of aEvent.  Must not be nullptr.
     */
    MOZ_CAN_RUN_SCRIPT void FinalizeHandlingEvent(WidgetEvent* aEvent,
                                                  const nsEventStatus* aStatus);

    /**
     * AutoCurrentEventInfoSetter() pushes and pops current event info of
     * aEventHandler.mPresShell.
     */
    struct MOZ_RAII AutoCurrentEventInfoSetter final {
      explicit AutoCurrentEventInfoSetter(EventHandler& aEventHandler)
          : mEventHandler(aEventHandler) {
        MOZ_DIAGNOSTIC_ASSERT(!mEventHandler.mCurrentEventInfoSetter);
        mEventHandler.mCurrentEventInfoSetter = this;
        mEventHandler.mPresShell->PushCurrentEventInfo(EventTargetInfo());
      }
      AutoCurrentEventInfoSetter(EventHandler& aEventHandler,
                                 const EventTargetInfo& aInfo)
          : mEventHandler(aEventHandler) {
        MOZ_DIAGNOSTIC_ASSERT(!mEventHandler.mCurrentEventInfoSetter);
        mEventHandler.mCurrentEventInfoSetter = this;
        mEventHandler.mPresShell->PushCurrentEventInfo(aInfo);
      }
      AutoCurrentEventInfoSetter(EventHandler& aEventHandler,
                                 EventTargetInfo&& aInfo)
          : mEventHandler(aEventHandler) {
        MOZ_DIAGNOSTIC_ASSERT(!mEventHandler.mCurrentEventInfoSetter);
        mEventHandler.mCurrentEventInfoSetter = this;
        mEventHandler.mPresShell->PushCurrentEventInfo(
            std::forward<EventTargetInfo>(aInfo));
      }
      AutoCurrentEventInfoSetter(EventHandler& aEventHandler,
                                 EventMessage aEventMessage,
                                 EventTargetData& aEventTargetData)
          : mEventHandler(aEventHandler) {
        MOZ_DIAGNOSTIC_ASSERT(!mEventHandler.mCurrentEventInfoSetter);
        mEventHandler.mCurrentEventInfoSetter = this;
        mEventHandler.mPresShell->PushCurrentEventInfo(
            EventTargetInfo(aEventMessage, aEventTargetData.GetFrame(),
                            aEventTargetData.GetContent()));
      }
      ~AutoCurrentEventInfoSetter() {
        mEventHandler.mPresShell->PopCurrentEventInfo();
        mEventHandler.mCurrentEventInfoSetter = nullptr;
      }

     private:
      EventHandler& mEventHandler;
    };

    /**
     * Wrapper methods to access methods of mPresShell.
     */
    nsPresContext* GetPresContext() const {
      return mPresShell->GetPresContext();
    }
    Document* GetDocument() const { return mPresShell->GetDocument(); }
    nsCSSFrameConstructor* FrameConstructor() const {
      return mPresShell->FrameConstructor();
    }
    already_AddRefed<nsPIDOMWindowOuter> GetFocusedDOMWindowInOurWindow() {
      return mPresShell->GetFocusedDOMWindowInOurWindow();
    }
    already_AddRefed<PresShell> GetParentPresShellForEventHandling() {
      return mPresShell->GetParentPresShellForEventHandling();
    }

    bool UpdateFocusSequenceNumber(nsIFrame* aFrameForPresShell,
                                   uint64_t aEventFocusSequenceNumber);

    MOZ_KNOWN_LIVE const OwningNonNull<PresShell> mPresShell;
    AutoCurrentEventInfoSetter* mCurrentEventInfoSetter;
    static TimeStamp sLastInputCreated;
    static TimeStamp sLastInputProcessed;
    static StaticRefPtr<dom::Element> sLastKeyDownEventTargetElement;
  };

  PresShell* GetRootPresShell() const;

  bool IsTransparentContainerElement() const;
  ColorScheme DefaultBackgroundColorScheme() const;
  nscolor GetDefaultBackgroundColorToDraw() const;

  //////////////////////////////////////////////////////////////////////////////
  // Approximate frame visibility tracking implementation.
  //////////////////////////////////////////////////////////////////////////////

  void UpdateApproximateFrameVisibility();
  void DoUpdateApproximateFrameVisibility(bool aRemoveOnly);

  void ClearApproximatelyVisibleFramesList(
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing());
  static void ClearApproximateFrameVisibilityVisited(nsView* aView,
                                                     bool aClear);
  static void MarkFramesInListApproximatelyVisible(const nsDisplayList& aList);
  void MarkFramesInSubtreeApproximatelyVisible(nsIFrame* aFrame,
                                               const nsRect& aRect,
                                               bool aRemoveOnly = false);

  void DecApproximateVisibleCount(
      VisibleFrames& aFrames,
      const Maybe<OnNonvisible>& aNonvisibleAction = Nothing());

  nsRevocableEventPtr<nsRunnableMethod<PresShell>>
      mUpdateApproximateFrameVisibilityEvent;

  // A set of frames that were visible or could be visible soon at the time
  // that we last did an approximate frame visibility update.
  VisibleFrames mApproximatelyVisibleFrames;

#ifdef DEBUG
  MOZ_CAN_RUN_SCRIPT_BOUNDARY bool VerifyIncrementalReflow();
  MOZ_CAN_RUN_SCRIPT_BOUNDARY void DoVerifyReflow();
  void VerifyHasDirtyRootAncestor(nsIFrame* aFrame);

  bool mInVerifyReflow = false;
  // The reflow root under which we're currently reflowing.  Null when
  // not in reflow.
  nsIFrame* mCurrentReflowRoot = nullptr;
#endif  // #ifdef DEBUG

 private:
  // IMPORTANT: The ownership implicit in the following member variables
  // has been explicitly checked.  If you add any members to this class,
  // please make the ownership explicit (pinkerton, scc).

  // These are the same Document and PresContext owned by the DocViewer.
  // we must share ownership.
  // mDocument and mPresContext should've never been cleared nor swapped with
  // another instance while PresShell instance is alive so that it's safe to
  // call their can-run- script methods without local RefPtr variables.
  MOZ_KNOWN_LIVE RefPtr<Document> const mDocument;
  MOZ_KNOWN_LIVE RefPtr<nsPresContext> const mPresContext;
  UniquePtr<nsCSSFrameConstructor> mFrameConstructor;
  nsViewManager* mViewManager;  // [WEAK] docViewer owns it so I don't have to
  RefPtr<nsFrameSelection> mSelection;
  // The frame selection that last took focus on this shell, which we need to
  // hide if we focus another selection. May or may not be the same as
  // `mSelection`.
  RefPtr<nsFrameSelection> mFocusedFrameSelection;

  // This the frame selection that will be used when getSelection().toString()
  // is called. See nsIContent::CanStartSelection for its reasoning.
  RefPtr<const nsFrameSelection> mLastSelectionForToString;

  RefPtr<nsCaret> mCaret;
  RefPtr<nsCaret> mOriginalCaret;
  RefPtr<AccessibleCaretEventHub> mAccessibleCaretEventHub;
  WeakPtr<nsDocShell> mForwardingContainer;

  // The `performance.now()` value when we last started to process reflows.
  DOMHighResTimeStamp mLastReflowStart{0.0};

#ifdef DEBUG
  // We track allocated pointers in a diagnostic hash set, to assert against
  // missing/double frees. This set is allocated infallibly in the PresShell
  // constructor's initialization list. The set can get quite large, so we use
  // fallible allocation when inserting into it; and if these operations ever
  // fail, then we just get rid of the set and stop using this diagnostic from
  // that point on.  (There's not much else we can do, when the set grows
  // larger than the available memory.)
  UniquePtr<nsTHashSet<void*>> mAllocatedPointers{
      MakeUnique<nsTHashSet<void*>>()};
#endif

  // A list of stack weak frames. This is a pointer to the last item in the
  // list.
  AutoWeakFrame* mAutoWeakFrames;

  // A hash table of heap allocated weak frames.
  nsTHashSet<WeakFrame*> mWeakFrames;

  // Reflow roots that need to be reflowed.
  DepthOrderedFrameList mDirtyRoots;

  // These two fields capture call stacks of any changes that require a restyle
  // or a reflow. Only the first change per restyle / reflow is recorded (the
  // one that caused a call to SetNeedStyleFlush() / SetNeedLayoutFlush()).
  UniquePtr<ProfileChunkedBuffer> mStyleCause;
  UniquePtr<ProfileChunkedBuffer> mReflowCause;

  nsTArray<UniquePtr<DelayedEvent>> mDelayedEvents;

  nsRevocableEventPtr<nsSynthMouseMoveEvent> mSynthMouseMoveEvent;

  TouchManager mTouchManager;

  RefPtr<ZoomConstraintsClient> mZoomConstraintsClient;
  RefPtr<GeckoMVMContext> mMVMContext;
  RefPtr<MobileViewportManager> mMobileViewportManager;

  // This timer controls painting suppression.  Until it fires
  // or all frames are constructed, we won't paint anything but
  // our <body> background and scrollbars.
  nsCOMPtr<nsITimer> mPaintSuppressionTimer;

  // Information about live content (which still stay in DOM tree).
  // Used in case we need re-dispatch event after sending pointer event,
  // when target of pointer event was deleted during executing user handlers.
  nsCOMPtr<nsIContent> mPointerEventTarget;

  nsCOMPtr<nsIContent> mLastAnchorScrolledTo;

  // Text directives are supposed to be scrolled to the center of the viewport.
  // Since `ScrollToAnchor()` might get called after `GoToAnchor()` during a
  // load, the vertical view position should be preserved.
  enum class AnchorScrollType : bool { Anchor, TextDirective };
  AnchorScrollType mLastAnchorScrollType = AnchorScrollType::Anchor;

  // Information needed to properly handle scrolling content into view if the
  // pre-scroll reflow flush can be interrupted.  mContentToScrollTo is non-null
  // between the initial scroll attempt and the first time we finish processing
  // all our dirty roots.  mContentToScrollTo has a content property storing the
  // details for the scroll operation, see ScrollIntoViewData above.
  nsCOMPtr<nsIContent> mContentToScrollTo;

#ifdef ACCESSIBILITY
  a11y::DocAccessible* mDocAccessible;
#endif  // #ifdef ACCESSIBILITY

  EventTargetInfo mCurrentEventTarget;
  nsTArray<EventTargetInfo> mCurrentEventTargetStack;
  // Set of frames that we should mark with NS_FRAME_HAS_DIRTY_CHILDREN after
  // we finish reflowing mCurrentReflowRoot.
  nsTHashSet<nsIFrame*> mFramesToDirty;
  nsTHashSet<ScrollContainerFrame*> mPendingScrollAnchorSelection;
  nsTHashSet<ScrollContainerFrame*> mPendingScrollAnchorAdjustment;
  nsTHashSet<ScrollContainerFrame*> mPendingScrollResnap;
  // Pending list of scroll/scrollend/etc events.
  nsTArray<RefPtr<Runnable>> mPendingScrollEvents;

  nsTHashSet<nsIContent*> mHiddenContentInForcedLayout;

  nsTHashSet<nsIFrame*> mContentVisibilityAutoFrames;

  // The type of content relevancy to update the next time content relevancy
  // updates are triggered for `content-visibility: auto` frames.
  ContentRelevancy mContentVisibilityRelevancyToUpdate;

  nsCallbackEventRequest* mFirstCallbackEventRequest = nullptr;
  nsCallbackEventRequest* mLastCallbackEventRequest = nullptr;

  // This is used for synthetic mouse events that are sent when what is under
  // the mouse pointer may have changed without the mouse moving (eg scrolling,
  // change to the document contents).
  // It is set only on a presshell for a root document, this value represents
  // the last observed location of the mouse relative to that root document,
  // in visual coordinates. It is set to (NS_UNCONSTRAINEDSIZE,
  // NS_UNCONSTRAINEDSIZE) if the mouse isn't over our window or there is no
  // last observed mouse location for some reason.
  nsPoint mMouseLocation;
  // This is used for the synthetic mouse events too.  This is set when a mouse
  // event is dispatched into the DOM.
  static int16_t sMouseButtons;
  // The last observed pointer location relative to that root document in visual
  // coordinates.
  nsPoint mLastOverWindowPointerLocation;
  // This is an APZ state variable that tracks the target guid for the last
  // mouse event that was processed (corresponding to mMouseLocation). This is
  // needed for the synthetic mouse events.
  layers::ScrollableLayerGuid mMouseEventTargetGuid;

  // Only populated on root content documents.
  nsSize mVisualViewportSize;

  using Arena = nsPresArena<8192, ArenaObjectID, eArenaObjectID_COUNT>;
  Arena mFrameArena;

  Maybe<nsPoint> mVisualViewportOffset;

  // A pending visual scroll offset that we will ask APZ to scroll to
  // during the next transaction. Cleared when we send the transaction.
  // Only applicable to the RCD pres shell.
  Maybe<VisualScrollUpdate> mPendingVisualScrollUpdate;

  // Used to force allocation and rendering of proportionally more or
  // less pixels in both dimensions.
  Maybe<float> mResolution;
  ResolutionChangeOrigin mLastResolutionChangeOrigin;

  TimeStamp mLoadBegin;  // used to time loads

  TimeStamp mLastOSWake;

  // Count of the number of times this presshell has been painted to a window.
  uint64_t mPaintCount;

  // The focus sequence number of the last processed input event
  uint64_t mAPZFocusSequenceNumber;

  nscoord mLastAnchorScrollPositionY = 0;

  // Most recent canvas background color.
  CanvasBackground mCanvasBackground;

  int32_t mActiveSuppressDisplayport;

  uint32_t mPresShellId;

  // Cached font inflation values. This is done to prevent changing of font
  // inflation until a page is reloaded.
  uint32_t mFontSizeInflationEmPerLine;
  uint32_t mFontSizeInflationMinTwips;
  uint32_t mFontSizeInflationLineThreshold;

  // Can be multiple of nsISelectionDisplay::DISPLAY_*.
  int16_t mSelectionFlags;

  // This is used to protect ourselves from triggering reflow while in the
  // middle of frame construction and the like... it really shouldn't be
  // needed, one hopes, but it is for now.
  uint16_t mChangeNestCount;

  // Flags controlling how our document is rendered.  These persist
  // between paints and so are tied with retained layer pixels.
  // PresShell flushes retained layers when the rendering state
  // changes in a way that prevents us from being able to (usefully)
  // re-use old pixels.
  RenderingStateFlags mRenderingStateFlags;

  bool mCaretEnabled : 1;

  // True if a layout flush might not be a no-op
  bool mNeedLayoutFlush : 1;

  // True if a style flush might not be a no-op
  bool mNeedStyleFlush : 1;

  // True if there are throttled animations that would be processed when
  // performing a flush with mFlushAnimations == true.
  bool mNeedThrottledAnimationFlush : 1;

  bool mVisualViewportSizeSet : 1;

  bool mDidInitialize : 1;
  bool mIsDestroying : 1;
  bool mIsReflowing : 1;
  bool mIsObservingDocument : 1;

  // Whether we shouldn't ever get to FlushPendingNotifications. This flag is
  // meant only to sanity-check / assert that FlushPendingNotifications doesn't
  // happen during certain periods of time. It shouldn't be made public nor used
  // for other purposes.
  bool mForbiddenToFlush : 1;

  // We've been disconnected from the document.  We will refuse to paint the
  // document until either our timer fires or all frames are constructed.
  bool mIsDocumentGone : 1;
  bool mHaveShutDown : 1;

  // For all documents we initially lock down painting.
  bool mPaintingSuppressed : 1;

  // Indicates that it is safe to unlock painting once all pending reflows
  // have been processed.
  bool mShouldUnsuppressPainting : 1;

  bool mIgnoreFrameDestruction : 1;

  bool mIsActive : 1;
  bool mFrozen : 1;
  bool mIsFirstPaint : 1;
  bool mObservesMutationsForPrint : 1;

  // Whether the most recent interruptible reflow was actually interrupted:
  bool mWasLastReflowInterrupted : 1;

  bool mResizeEventPending : 1;
  bool mVisualViewportResizeEventPending : 1;

  bool mFontSizeInflationForceEnabled : 1;
  bool mFontSizeInflationDisabledInMasterProcess : 1;
  bool mFontSizeInflationEnabled : 1;

  // If a document belongs to an invisible DocShell, this flag must be set
  // to true, so we can avoid any paint calls for widget related to this
  // presshell.
  bool mIsNeverPainting : 1;

  // Whether the most recent change to the pres shell resolution was
  // originated by the main thread.
  bool mResolutionUpdated : 1;

  // True if the resolution has been ever changed by APZ.
  bool mResolutionUpdatedByApz : 1;

  // Whether this presshell is hidden by 'vibility:hidden' on an ancestor
  // nsSubDocumentFrame.
  bool mUnderHiddenEmbedderElement : 1;

  bool mDocumentLoading : 1;
  bool mNoDelayedMouseEvents : 1;
  bool mNoDelayedKeyEvents : 1;

  bool mApproximateFrameVisibilityVisited : 1;

  // Whether the last chrome-only escape key event is consumed.
  bool mIsLastChromeOnlyEscapeKeyConsumed : 1;

  // Whether the widget has received a paint message yet.
  bool mHasReceivedPaintMessage : 1;

  bool mIsLastKeyDownCanceled : 1;

  // Whether we have ever handled a user input event
  bool mHasHandledUserInput : 1;

  // Whether we should dispatch keypress events even for non-printable keys
  // for keeping backward compatibility.
  bool mForceDispatchKeyPressEventsForNonPrintableKeys : 1;
  // Whether we should set keyCode or charCode value of keypress events whose
  // value is zero to the other value or not.  When this is set to true, we
  // should keep using legacy keyCode and charCode values (i.e., one of them
  // is always 0).
  bool mForceUseLegacyKeyCodeAndCharCodeValues : 1;
  // Whether mForceDispatchKeyPressEventsForNonPrintableKeys and
  // mForceUseLegacyKeyCodeAndCharCodeValues are initialized.
  bool mInitializedWithKeyPressEventDispatchingBlacklist : 1;

  // Set to true if mMouseLocation is set by a mouse event which is synthesized
  // for tests.
  bool mMouseLocationWasSetBySynthesizedMouseEventForTests : 1;

  bool mHasTriedFastUnsuppress : 1;

  bool mProcessingReflowCommands : 1;
  bool mPendingDidDoReflow : 1;

  // The last TimeStamp when the keyup event did not exit fullscreen because it
  // was consumed.
  TimeStamp mLastConsumedEscapeKeyUpForFullscreen;

  // The `SelectionNodeCache` is tightly coupled with the PresShell.
  // It should only be possible to create a cache from within a PresShell.
  // The created cache sets itself into `this`. Therefore, it's necessary to use
  // `friend` here to avoid having setters.
  friend dom::SelectionNodeCache;
  dom::SelectionNodeCache* mSelectionNodeCache{nullptr};

  struct CapturingContentInfo final {
    CapturingContentInfo()
        : mRemoteTarget(nullptr),
          mAllowed(false),
          mPointerLock(false),
          mRetargetToElement(false),
          mPreventDrag(false) {}

    // capture should only be allowed during a mousedown event
    StaticRefPtr<nsIContent> mContent;
    dom::BrowserParent* mRemoteTarget;
    bool mAllowed;
    bool mPointerLock;
    bool mRetargetToElement;
    bool mPreventDrag;
  };
  static CapturingContentInfo sCapturingContentInfo;

  static bool sDisableNonTestMouseEvents;

  static bool sProcessInteractable;
};

NS_DEFINE_STATIC_IID_ACCESSOR(PresShell, NS_PRESSHELL_IID)

}  // namespace mozilla

#endif  // mozilla_PresShell_h
