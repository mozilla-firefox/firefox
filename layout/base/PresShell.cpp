/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* a presentation of a document, part 2 */

#include "mozilla/PresShell.h"

#include <algorithm>

#include "AutoProfilerStyleMarker.h"
#include "ChildIterator.h"
#include "gfxContext.h"
#include "gfxPlatform.h"
#include "gfxUserFontSet.h"
#include "gfxUtils.h"
#include "MobileViewportManager.h"
#include "mozilla/AccessibleCaretEventHub.h"
#include "mozilla/AnimationEventDispatcher.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/CaretAssociationHint.h"
#include "mozilla/ContentIterator.h"
#include "mozilla/css/ImageLoader.h"
#include "mozilla/DisplayPortUtils.h"
#include "mozilla/dom/AncestorIterator.h"
#include "mozilla/dom/BrowserBridgeChild.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/DOMIntersectionObserver.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/ElementInlines.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/dom/FragmentDirective.h"
#include "mozilla/dom/HTMLAreaElement.h"
#include "mozilla/dom/ImageTracker.h"
#include "mozilla/dom/LargestContentfulPaint.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PerformanceMainThread.h"
#include "mozilla/dom/PointerEventBinding.h"
#include "mozilla/dom/PointerEventHandler.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/ShadowIncludingTreeIterator.h"
#include "mozilla/dom/SVGAnimationElement.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/dom/TouchEvent.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/EditorBase.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventForwards.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/GeckoMVMContext.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/GlobalStyleSheetCache.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/InputTaskManager.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/layers/APZPublicUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/FocusTarget.h"
#include "mozilla/layers/InputAPZContext.h"
#include "mozilla/layers/ScrollingInteractionContext.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/WebRenderUserData.h"
#include "mozilla/layout/ScrollAnchorContainer.h"
#include "mozilla/Likely.h"
#include "mozilla/Logging.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/PerfStats.h"
#include "mozilla/PointerLockManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/RangeUtils.h"
#include "mozilla/RefPtr.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ScrollTimelineAnimationTracker.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/ServoBindings.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_font.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_test.h"
#include "mozilla/StaticPrefs_toolkit.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/StyleSheetInlines.h"
#include "mozilla/SVGFragmentIdentifier.h"
#include "mozilla/SVGObserverUtils.h"
#include "mozilla/glean/GfxMetrics.h"
#include "mozilla/glean/LayoutMetrics.h"
#include "mozilla/Telemetry.h"
#include "mozilla/TextComposition.h"
#include "mozilla/TextEvents.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/Try.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/ViewportFrame.h"
#include "mozilla/ViewportUtils.h"
#include "nsAnimationManager.h"
#include "nsAutoLayoutPhase.h"
#include "nsCanvasFrame.h"
#include "nsCaret.h"
#include "nsClassHashtable.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsContainerFrame.h"
#include "nsContentList.h"
#include "nsCRTGlue.h"
#include "nsCSSFrameConstructor.h"
#include "nsCSSRendering.h"
#include "nsDisplayList.h"
#include "nsDocShell.h"  // for reflow observation
#include "nsDOMNavigationTiming.h"
#include "nsError.h"
#include "nsFlexContainerFrame.h"
#include "nsFocusManager.h"
#include "nsFrameSelection.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowOuter.h"
#include "nsHashKeys.h"
#include "nsIBaseWindow.h"
#include "nsIContent.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIDOMXULMenuListElement.h"
#include "nsIDOMXULMultSelectCntrlEl.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIDragSession.h"
#include "nsIFrame.h"
#include "nsIFrameInlines.h"
#include "nsILayoutHistoryState.h"
#include "nsILineIterator.h"  // for ScrollContentIntoView
#include "nsImageFrame.h"
#include "nsIObserverService.h"
#include "nsIReflowCallback.h"
#include "nsIScreen.h"
#include "nsIScreenManager.h"
#include "nsITimer.h"
#include "nsIURI.h"
#include "nsLayoutUtils.h"
#include "nsMenuPopupFrame.h"
#include "nsNameSpaceManager.h"  // for Pref-related rule management (bugs 22963,20760,31816)
#include "nsNetUtil.h"
#include "nsNetUtil.h"
#include "nsPageSequenceFrame.h"
#include "nsPIDOMWindow.h"
#include "nsPlaceholderFrame.h"
#include "nsPresContext.h"
#include "nsQueryObject.h"
#include "nsRange.h"
#include "nsReadableUtils.h"
#include "nsRefreshDriver.h"
#include "nsRegion.h"
#include "nsStyleChangeList.h"
#include "nsStyleSheetService.h"
#include "nsSubDocumentFrame.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"
#include "nsTransitionManager.h"
#include "nsTreeBodyFrame.h"
#include "nsTreeColumns.h"
#include "nsView.h"
#include "nsViewManager.h"
#include "nsViewportInfo.h"
#include "nsWindowSizes.h"
#include "nsXPCOM.h"
#include "nsXULElement.h"
#include "OverflowChangedTracker.h"
#include "PLDHashTable.h"
#include "PositionedEventTargeting.h"
#include "prenv.h"
#include "prinrval.h"
#include "ScrollSnap.h"
#include "StickyScrollContainer.h"
#include "Units.h"
#include "VisualViewport.h"
#include "XULTreeElement.h"
#include "ZoomConstraintsClient.h"

#ifdef XP_WIN
#  include "winuser.h"
#endif

#ifdef MOZ_REFLOW_PERF
#  include "nsFontMetrics.h"
#endif

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessible.h"
#  ifdef DEBUG
#    include "mozilla/a11y/Logging.h"
#  endif
#endif

// define the scalfactor of drag and drop images
// relative to the max screen height/width
#define RELATIVE_SCALEFACTOR 0.0925f

using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;
using namespace mozilla::gfx;
using namespace mozilla::layout;
using PaintFrameFlags = nsLayoutUtils::PaintFrameFlags;
typedef ScrollableLayerGuid::ViewID ViewID;

MOZ_RUNINIT PresShell::CapturingContentInfo PresShell::sCapturingContentInfo;

// RangePaintInfo is used to paint ranges to offscreen buffers
struct RangePaintInfo {
  nsDisplayListBuilder mBuilder;
  nsDisplayList mList;

  // offset of builder's reference frame to the root frame
  nsPoint mRootOffset;

  // Resolution at which the items are normally painted. So if we're painting
  // these items in a range separately from the "full display list", we may want
  // to paint them at this resolution.
  float mResolution = 1.0;

  explicit RangePaintInfo(nsIFrame* aFrame)
      : mBuilder(aFrame, nsDisplayListBuilderMode::Painting, false),
        mList(&mBuilder) {
    MOZ_COUNT_CTOR(RangePaintInfo);
    mBuilder.BeginFrame();
  }

  ~RangePaintInfo() {
    mList.DeleteAll(&mBuilder);
    mBuilder.EndFrame();
    MOZ_COUNT_DTOR(RangePaintInfo);
  }
};

#undef NOISY

// ----------------------------------------------------------------------

#ifdef DEBUG
// Set the environment variable GECKO_VERIFY_REFLOW_FLAGS to one or
// more of the following flags (comma separated) for handy debug
// output.
static VerifyReflowFlags gVerifyReflowFlags;

struct VerifyReflowFlagData {
  const char* name;
  VerifyReflowFlags bit;
};

static const VerifyReflowFlagData gFlags[] = {
    // clang-format off
  { "verify",                VerifyReflowFlags::On },
  { "reflow",                VerifyReflowFlags::Noisy },
  { "all",                   VerifyReflowFlags::All },
  { "list-commands",         VerifyReflowFlags::DumpCommands },
  { "noisy-commands",        VerifyReflowFlags::NoisyCommands },
  { "really-noisy-commands", VerifyReflowFlags::ReallyNoisyCommands },
  { "resize",                VerifyReflowFlags::DuringResizeReflow },
    // clang-format on
};

#  define NUM_VERIFY_REFLOW_FLAGS (sizeof(gFlags) / sizeof(gFlags[0]))

static void ShowVerifyReflowFlags() {
  printf("Here are the available GECKO_VERIFY_REFLOW_FLAGS:\n");
  const VerifyReflowFlagData* flag = gFlags;
  const VerifyReflowFlagData* limit = gFlags + NUM_VERIFY_REFLOW_FLAGS;
  while (flag < limit) {
    printf("  %s\n", flag->name);
    ++flag;
  }
  printf("Note: GECKO_VERIFY_REFLOW_FLAGS is a comma separated list of flag\n");
  printf("names (no whitespace)\n");
}
#endif

//========================================================================
//========================================================================
//========================================================================
#ifdef MOZ_REFLOW_PERF
class ReflowCountMgr;

static const char kGrandTotalsStr[] = "Grand Totals";

// Counting Class
class ReflowCounter {
 public:
  explicit ReflowCounter(ReflowCountMgr* aMgr = nullptr);
  ~ReflowCounter();

  void ClearTotals();
  void DisplayTotals(const char* aStr);
  void DisplayDiffTotals(const char* aStr);
  void DisplayHTMLTotals(const char* aStr);

  void Add() { mTotal++; }
  void Add(uint32_t aTotal) { mTotal += aTotal; }

  void CalcDiffInTotals();
  void SetTotalsCache();

  void SetMgr(ReflowCountMgr* aMgr) { mMgr = aMgr; }

  uint32_t GetTotal() { return mTotal; }

 protected:
  void DisplayTotals(uint32_t aTotal, const char* aTitle);
  void DisplayHTMLTotals(uint32_t aTotal, const char* aTitle);

  uint32_t mTotal;
  uint32_t mCacheTotal;

  ReflowCountMgr* mMgr;  // weak reference (don't delete)
};

// Counting Class
class IndiReflowCounter {
 public:
  explicit IndiReflowCounter(ReflowCountMgr* aMgr = nullptr)
      : mFrame(nullptr),
        mCount(0),
        mMgr(aMgr),
        mCounter(aMgr),
        mHasBeenOutput(false) {}
  virtual ~IndiReflowCounter() = default;

  nsAutoString mName;
  nsIFrame* mFrame;  // weak reference (don't delete)
  int32_t mCount;

  ReflowCountMgr* mMgr;  // weak reference (don't delete)

  ReflowCounter mCounter;
  bool mHasBeenOutput;
};

//--------------------
// Manager Class
//--------------------
class ReflowCountMgr {
 public:
  ReflowCountMgr();
  virtual ~ReflowCountMgr();

  void ClearTotals();
  void ClearGrandTotals();
  void DisplayTotals(const char* aStr);
  void DisplayHTMLTotals(const char* aStr);
  void DisplayDiffsInTotals();

  void Add(const char* aName, nsIFrame* aFrame);
  ReflowCounter* LookUp(const char* aName);

  void PaintCount(const char* aName, gfxContext* aRenderingContext,
                  nsPresContext* aPresContext, nsIFrame* aFrame,
                  const nsPoint& aOffset, uint32_t aColor);

  FILE* GetOutFile() { return mFD; }

  void SetPresContext(nsPresContext* aPresContext) {
    mPresContext = aPresContext;  // weak reference
  }
  void SetPresShell(PresShell* aPresShell) {
    mPresShell = aPresShell;  // weak reference
  }

  void SetDumpFrameCounts(bool aVal) { mDumpFrameCounts = aVal; }
  void SetDumpFrameByFrameCounts(bool aVal) { mDumpFrameByFrameCounts = aVal; }
  void SetPaintFrameCounts(bool aVal) { mPaintFrameByFrameCounts = aVal; }

  bool IsPaintingFrameCounts() { return mPaintFrameByFrameCounts; }

 protected:
  void DisplayTotals(uint32_t aTotal, uint32_t* aDupArray, char* aTitle);
  void DisplayHTMLTotals(uint32_t aTotal, uint32_t* aDupArray, char* aTitle);

  void DoGrandTotals();
  void DoIndiTotalsTree();

  // HTML Output Methods
  void DoGrandHTMLTotals();

  nsClassHashtable<nsCharPtrHashKey, ReflowCounter> mCounts;
  nsClassHashtable<nsCharPtrHashKey, IndiReflowCounter> mIndiFrameCounts;
  FILE* mFD;

  bool mDumpFrameCounts;
  bool mDumpFrameByFrameCounts;
  bool mPaintFrameByFrameCounts;

  bool mCycledOnce;

  // Root Frame for Individual Tracking
  nsPresContext* mPresContext;
  PresShell* mPresShell;

  // ReflowCountMgr gReflowCountMgr;
};
#endif
//========================================================================

// comment out to hide caret
#define SHOW_CARET

// The upper bound on the amount of time to spend reflowing, in
// microseconds.  When this bound is exceeded and reflow commands are
// still queued up, a reflow event is posted.  The idea is for reflow
// to not hog the processor beyond the time specifed in
// gMaxRCProcessingTime.  This data member is initialized from the
// layout.reflow.timeslice pref.
#define NS_MAX_REFLOW_TIME 1000000
static int32_t gMaxRCProcessingTime = -1;

struct nsCallbackEventRequest {
  nsIReflowCallback* callback;
  nsCallbackEventRequest* next;
};

// ----------------------------------------------------------------------------

class nsAutoCauseReflowNotifier {
 public:
  MOZ_CAN_RUN_SCRIPT explicit nsAutoCauseReflowNotifier(PresShell* aPresShell)
      : mPresShell(aPresShell) {
    mPresShell->WillCauseReflow();
  }
  MOZ_CAN_RUN_SCRIPT ~nsAutoCauseReflowNotifier() {
    // This check should not be needed. Currently the only place that seem
    // to need it is the code that deals with bug 337586.
    if (!mPresShell->mHaveShutDown) {
      RefPtr<PresShell> presShell(mPresShell);
      presShell->DidCauseReflow();
    } else {
      nsContentUtils::RemoveScriptBlocker();
    }
  }

  PresShell* mPresShell;
};

class MOZ_STACK_CLASS nsPresShellEventCB : public EventDispatchingCallback {
 public:
  explicit nsPresShellEventCB(PresShell* aPresShell) : mPresShell(aPresShell) {}

  MOZ_CAN_RUN_SCRIPT
  virtual void HandleEvent(EventChainPostVisitor& aVisitor) override {
    if (aVisitor.mPresContext && aVisitor.mEvent->mClass != eBasicEventClass) {
      if (aVisitor.mEvent->mMessage == eMouseDown ||
          aVisitor.mEvent->mMessage == eMouseUp) {
        // Mouse-up and mouse-down events call nsIFrame::HandlePress/Release
        // which call GetContentOffsetsFromPoint which requires up-to-date
        // layout. Bring layout up-to-date now so that GetCurrentEventFrame()
        // below will return a real frame and we don't have to worry about
        // destroying it by flushing later.
        MOZ_KnownLive(mPresShell)->FlushPendingNotifications(FlushType::Layout);
      } else if (aVisitor.mEvent->mMessage == eWheel &&
                 aVisitor.mEventStatus != nsEventStatus_eConsumeNoDefault) {
        nsIFrame* frame = mPresShell->GetCurrentEventFrame();
        if (frame) {
          // chrome (including addons) should be able to know if content
          // handles both D3E "wheel" event and legacy mouse scroll events.
          // We should dispatch legacy mouse events before dispatching the
          // "wheel" event into system group.
          RefPtr<EventStateManager> esm =
              aVisitor.mPresContext->EventStateManager();
          esm->DispatchLegacyMouseScrollEvents(
              frame, aVisitor.mEvent->AsWheelEvent(), &aVisitor.mEventStatus);
        }
      }
      nsIFrame* frame = mPresShell->GetCurrentEventFrame();
      if (!frame && (aVisitor.mEvent->mMessage == eMouseUp ||
                     aVisitor.mEvent->mMessage == eTouchEnd)) {
        // Redirect BUTTON_UP and TOUCH_END events to the root frame to ensure
        // that capturing is released.
        frame = mPresShell->GetRootFrame();
      }
      if (frame) {
        frame->HandleEvent(aVisitor.mPresContext, aVisitor.mEvent->AsGUIEvent(),
                           &aVisitor.mEventStatus);
      }
    }
  }

  RefPtr<PresShell> mPresShell;
};

class nsBeforeFirstPaintDispatcher : public Runnable {
 public:
  explicit nsBeforeFirstPaintDispatcher(Document* aDocument)
      : mozilla::Runnable("nsBeforeFirstPaintDispatcher"),
        mDocument(aDocument) {}

  // Fires the "before-first-paint" event so that interested parties (right now,
  // the mobile browser) are aware of it.
  NS_IMETHOD Run() override {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->NotifyObservers(ToSupports(mDocument),
                                       "before-first-paint", nullptr);
    }
    return NS_OK;
  }

 private:
  RefPtr<Document> mDocument;
};

// This is a helper class to track whether the targeted frame is destroyed after
// dispatching pointer events. In that case, we need the original targeted
// content so that we can dispatch the mouse events to it.
class MOZ_RAII AutoPointerEventTargetUpdater final {
 public:
  AutoPointerEventTargetUpdater(PresShell* aShell, WidgetEvent* aEvent,
                                nsIFrame* aFrame, nsIContent* aTargetContent,
                                nsIContent** aOutTargetContent) {
    MOZ_ASSERT(aEvent);
    if (!aOutTargetContent || aEvent->mClass != ePointerEventClass) {
      // Make the destructor happy.
      mOutTargetContent = nullptr;
      return;
    }
    MOZ_ASSERT(aShell);
    MOZ_ASSERT_IF(aFrame && aFrame->GetContent(),
                  aShell->GetDocument() == aFrame->GetContent()->OwnerDoc());

    mShell = aShell;
    mWeakFrame = aFrame;
    mOutTargetContent = aOutTargetContent;
    mFromTouch = aEvent->AsPointerEvent()->mFromTouchEvent;
    // Touch event target may have no frame, e.g., removed from the DOM
    MOZ_ASSERT_IF(!mFromTouch, aFrame);
    // The frame may be a text frame, but the event target should be an element
    // node.  Therefore, refer aTargetContent first, then, if we have only a
    // frame, we should use inclusive ancestor of the content.
    mOriginalPointerEventTarget =
        aShell->mPointerEventTarget = [&]() -> nsIContent* {
      nsIContent* const target =
          aTargetContent ? aTargetContent
                         : (aFrame ? aFrame->GetContent() : nullptr);
      if (MOZ_UNLIKELY(!target)) {
        return nullptr;
      }
      if (target->IsElement() ||
          !IsForbiddenDispatchingToNonElementContent(aEvent->mMessage)) {
        return target;
      }
      return target->GetInclusiveFlattenedTreeAncestorElement();
    }();
  }

  ~AutoPointerEventTargetUpdater() {
    if (!mOutTargetContent || !mShell || mWeakFrame.IsAlive()) {
      return;
    }
    if (mFromTouch) {
      // If the source event is a touch event, the touch event target should
      // always be same target as preceding ePointerDown.  Therefore, we should
      // always set it back to the original event target.
      mOriginalPointerEventTarget.swap(*mOutTargetContent);
    } else {
      // If the source event is not a touch event (must be a mouse event in
      // this case), the event should be fired on the closest inclusive ancestor
      // of the pointer event target which is still connected.  The mutations
      // are tracked by PresShell::ContentRemoved.  Therefore, we should set it.
      mShell->mPointerEventTarget.swap(*mOutTargetContent);
    }
  }

 private:
  RefPtr<PresShell> mShell;
  nsCOMPtr<nsIContent> mOriginalPointerEventTarget;
  AutoWeakFrame mWeakFrame;
  nsIContent** mOutTargetContent;
  bool mFromTouch = false;
};

bool PresShell::sDisableNonTestMouseEvents = false;
int16_t PresShell::sMouseButtons = MouseButtonsFlag::eNoButtons;

LazyLogModule PresShell::gLog("PresShell");

#ifdef DEBUG
// MouseLocation logs the mouse location and when/where enqueued synthesized
// mouse move is flushed.  If you don't need all mouse location recording at
// eMouseMove, you can use MouseLocation:3,sync.  Then, it's logged once per
// 50 times.  Otherwise, if you need to log all eMouseMove locations, you can
// use MouseLocation:5,sync.
LazyLogModule gLogMouseLocation("MouseLocation");
#endif

TimeStamp PresShell::EventHandler::sLastInputCreated;
TimeStamp PresShell::EventHandler::sLastInputProcessed;
StaticRefPtr<Element> PresShell::EventHandler::sLastKeyDownEventTargetElement;

bool PresShell::sProcessInteractable = false;

static bool gVerifyReflowEnabled;

bool PresShell::GetVerifyReflowEnable() {
#ifdef DEBUG
  static bool firstTime = true;
  if (firstTime) {
    firstTime = false;
    char* flags = PR_GetEnv("GECKO_VERIFY_REFLOW_FLAGS");
    if (flags) {
      bool error = false;

      for (;;) {
        char* comma = strchr(flags, ',');
        if (comma) *comma = '\0';

        bool found = false;
        const VerifyReflowFlagData* flag = gFlags;
        const VerifyReflowFlagData* limit = gFlags + NUM_VERIFY_REFLOW_FLAGS;
        while (flag < limit) {
          if (nsCRT::strcasecmp(flag->name, flags) == 0) {
            gVerifyReflowFlags |= flag->bit;
            found = true;
            break;
          }
          ++flag;
        }

        if (!found) error = true;

        if (!comma) break;

        *comma = ',';
        flags = comma + 1;
      }

      if (error) ShowVerifyReflowFlags();
    }

    if (VerifyReflowFlags::On & gVerifyReflowFlags) {
      gVerifyReflowEnabled = true;

      printf("Note: verifyreflow is enabled");
      if (VerifyReflowFlags::Noisy & gVerifyReflowFlags) {
        printf(" (noisy)");
      }
      if (VerifyReflowFlags::All & gVerifyReflowFlags) {
        printf(" (all)");
      }
      if (VerifyReflowFlags::DumpCommands & gVerifyReflowFlags) {
        printf(" (show reflow commands)");
      }
      if (VerifyReflowFlags::NoisyCommands & gVerifyReflowFlags) {
        printf(" (noisy reflow commands)");
        if (VerifyReflowFlags::ReallyNoisyCommands & gVerifyReflowFlags) {
          printf(" (REALLY noisy reflow commands)");
        }
      }
      printf("\n");
    }
  }
#endif
  return gVerifyReflowEnabled;
}

void PresShell::SetVerifyReflowEnable(bool aEnabled) {
  gVerifyReflowEnabled = aEnabled;
}

void PresShell::AddAutoWeakFrame(AutoWeakFrame* aWeakFrame) {
  if (aWeakFrame->GetFrame()) {
    aWeakFrame->GetFrame()->AddStateBits(NS_FRAME_EXTERNAL_REFERENCE);
  }
  aWeakFrame->SetPreviousWeakFrame(mAutoWeakFrames);
  mAutoWeakFrames = aWeakFrame;
}

void PresShell::AddWeakFrame(WeakFrame* aWeakFrame) {
  if (aWeakFrame->GetFrame()) {
    aWeakFrame->GetFrame()->AddStateBits(NS_FRAME_EXTERNAL_REFERENCE);
  }
  MOZ_ASSERT(!mWeakFrames.Contains(aWeakFrame));
  mWeakFrames.Insert(aWeakFrame);
}

void PresShell::RemoveAutoWeakFrame(AutoWeakFrame* aWeakFrame) {
  if (mAutoWeakFrames == aWeakFrame) {
    mAutoWeakFrames = aWeakFrame->GetPreviousWeakFrame();
    return;
  }
  AutoWeakFrame* nextWeak = mAutoWeakFrames;
  while (nextWeak && nextWeak->GetPreviousWeakFrame() != aWeakFrame) {
    nextWeak = nextWeak->GetPreviousWeakFrame();
  }
  if (nextWeak) {
    nextWeak->SetPreviousWeakFrame(aWeakFrame->GetPreviousWeakFrame());
  }
}

void PresShell::RemoveWeakFrame(WeakFrame* aWeakFrame) {
  MOZ_ASSERT(mWeakFrames.Contains(aWeakFrame));
  mWeakFrames.Remove(aWeakFrame);
}

already_AddRefed<nsFrameSelection> PresShell::FrameSelection() {
  RefPtr<nsFrameSelection> ret = mSelection;
  return ret.forget();
}

//----------------------------------------------------------------------

static uint32_t sNextPresShellId = 0;

/* static */
bool PresShell::AccessibleCaretEnabled(nsIDocShell* aDocShell) {
  // If the pref forces it on, then enable it.
  if (StaticPrefs::layout_accessiblecaret_enabled()) {
    return true;
  }
  // If the touch pref is on, and touch events are enabled (this depends
  // on the specific device running), then enable it.
  if (StaticPrefs::layout_accessiblecaret_enabled_on_touch() &&
      dom::TouchEvent::PrefEnabled(aDocShell)) {
    return true;
  }
  // Otherwise, disabled.
  return false;
}

PresShell::PresShell(Document* aDocument)
    : mDocument(aDocument),
      mViewManager(nullptr),
      mLastSelectionForToString(nullptr),
      mAutoWeakFrames(nullptr),
#ifdef ACCESSIBILITY
      mDocAccessible(nullptr),
#endif  // ACCESSIBILITY
      mMouseLocation(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE),
      mLastResolutionChangeOrigin(ResolutionChangeOrigin::Apz),
      mPaintCount(0),
      mAPZFocusSequenceNumber(0),
      mActiveSuppressDisplayport(0),
      mPresShellId(++sNextPresShellId),
      mFontSizeInflationEmPerLine(0),
      mFontSizeInflationMinTwips(0),
      mFontSizeInflationLineThreshold(0),
      mSelectionFlags(nsISelectionDisplay::DISPLAY_TEXT |
                      nsISelectionDisplay::DISPLAY_IMAGES),
      mChangeNestCount(0),
      mRenderingStateFlags(RenderingStateFlags::None),
      mCaretEnabled(false),
      mNeedLayoutFlush(true),
      mNeedStyleFlush(true),
      mNeedThrottledAnimationFlush(true),
      mVisualViewportSizeSet(false),
      mDidInitialize(false),
      mIsDestroying(false),
      mIsReflowing(false),
      mIsObservingDocument(false),
      mForbiddenToFlush(false),
      mIsDocumentGone(false),
      mHaveShutDown(false),
      mPaintingSuppressed(false),
      mShouldUnsuppressPainting(false),
      mIgnoreFrameDestruction(false),
      mIsActive(true),
      mFrozen(false),
      mIsFirstPaint(true),
      mObservesMutationsForPrint(false),
      mWasLastReflowInterrupted(false),
      mResizeEventPending(false),
      mVisualViewportResizeEventPending(false),
      mFontSizeInflationForceEnabled(false),
      mFontSizeInflationDisabledInMasterProcess(false),
      mFontSizeInflationEnabled(false),
      mIsNeverPainting(false),
      mResolutionUpdated(false),
      mResolutionUpdatedByApz(false),
      mUnderHiddenEmbedderElement(false),
      mDocumentLoading(false),
      mNoDelayedMouseEvents(false),
      mNoDelayedKeyEvents(false),
      mApproximateFrameVisibilityVisited(false),
      mIsLastChromeOnlyEscapeKeyConsumed(false),
      mHasReceivedPaintMessage(false),
      mIsLastKeyDownCanceled(false),
      mHasHandledUserInput(false),
      mForceDispatchKeyPressEventsForNonPrintableKeys(false),
      mForceUseLegacyKeyCodeAndCharCodeValues(false),
      mInitializedWithKeyPressEventDispatchingBlacklist(false),
      mMouseLocationWasSetBySynthesizedMouseEventForTests(false),
      mHasTriedFastUnsuppress(false),
      mProcessingReflowCommands(false),
      mPendingDidDoReflow(false) {
  MOZ_LOG(gLog, LogLevel::Debug, ("PresShell::PresShell this=%p", this));
  MOZ_ASSERT(aDocument);

#ifdef MOZ_REFLOW_PERF
  mReflowCountMgr = MakeUnique<ReflowCountMgr>();
  mReflowCountMgr->SetPresContext(mPresContext);
  mReflowCountMgr->SetPresShell(this);
#endif
  mLastOSWake = mLoadBegin = TimeStamp::Now();
}

NS_INTERFACE_TABLE_HEAD(PresShell)
  NS_INTERFACE_TABLE_BEGIN
    // In most cases, PresShell should be treated as concrete class, but need to
    // QI for weak reference.  Therefore, the case needed by do_QueryReferent()
    // should be tested first.
    NS_INTERFACE_TABLE_ENTRY(PresShell, PresShell)
    NS_INTERFACE_TABLE_ENTRY(PresShell, nsIDocumentObserver)
    NS_INTERFACE_TABLE_ENTRY(PresShell, nsISelectionController)
    NS_INTERFACE_TABLE_ENTRY(PresShell, nsISelectionDisplay)
    NS_INTERFACE_TABLE_ENTRY(PresShell, nsIObserver)
    NS_INTERFACE_TABLE_ENTRY(PresShell, nsISupportsWeakReference)
    NS_INTERFACE_TABLE_ENTRY(PresShell, nsIMutationObserver)
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(PresShell, nsISupports, nsIObserver)
  NS_INTERFACE_TABLE_END
  NS_INTERFACE_TABLE_TO_MAP_SEGUE
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(PresShell)
NS_IMPL_RELEASE(PresShell)

PresShell::~PresShell() {
  MOZ_RELEASE_ASSERT(!mForbiddenToFlush,
                     "Flag should only be set temporarily, while doing things "
                     "that shouldn't cause destruction");
  MOZ_LOG(gLog, LogLevel::Debug, ("PresShell::~PresShell this=%p", this));

  if (!mHaveShutDown) {
    MOZ_ASSERT_UNREACHABLE("Someone did not call PresShell::Destroy()");
    Destroy();
  }

  NS_ASSERTION(mCurrentEventTargetStack.IsEmpty(),
               "Huh, event content left on the stack in pres shell dtor!");
  NS_ASSERTION(mFirstCallbackEventRequest == nullptr &&
                   mLastCallbackEventRequest == nullptr,
               "post-reflow queues not empty.  This means we're leaking");

  MOZ_ASSERT(!mAllocatedPointers || mAllocatedPointers->IsEmpty(),
             "Some pres arena objects were not freed");

  mFrameConstructor = nullptr;
}

/**
 * Initialize the presentation shell. Create view manager and style
 * manager.
 * Note this can't be merged into our constructor because caret initialization
 * calls AddRef() on us.
 */
void PresShell::Init(nsPresContext* aPresContext, nsViewManager* aViewManager) {
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(aPresContext);
  MOZ_ASSERT(aViewManager);
  MOZ_ASSERT(!mViewManager, "already initialized");

  mViewManager = aViewManager;

  // mDocument is now set.  It might have a display document whose "need layout/
  // style" flush flags are not set, but ours will be set.  To keep these
  // consistent, call the flag setting functions to propagate those flags up
  // to the display document.
  SetNeedLayoutFlush();
  SetNeedStyleFlush();

  // Create our frame constructor.
  mFrameConstructor = MakeUnique<nsCSSFrameConstructor>(mDocument, this);

  // The document viewer owns both view manager and pres shell.
  mViewManager->SetPresShell(this);

  // Bind the context to the presentation shell.
  // FYI: We cannot initialize mPresContext in the constructor because we
  //      cannot call AttachPresShell() in it and once we initialize
  //      mPresContext, other objects may refer refresh driver or restyle
  //      manager via mPresContext and that causes hitting MOZ_ASSERT in some
  //      places.  Therefore, we should initialize mPresContext here with
  //      const_cast hack since we want to guarantee that mPresContext lives
  //      as long as the PresShell.
  const_cast<RefPtr<nsPresContext>&>(mPresContext) = aPresContext;
  mPresContext->AttachPresShell(this);

  mPresContext->InitFontCache();

  // FIXME(emilio, bug 1544185): Some Android code somehow depends on the shell
  // being eagerly registered as a style flush observer. This shouldn't be
  // needed otherwise.
  EnsureStyleFlush();

  const bool accessibleCaretEnabled =
      AccessibleCaretEnabled(mDocument->GetDocShell());
  if (accessibleCaretEnabled) {
    // Need to happen before nsFrameSelection has been set up.
    mAccessibleCaretEventHub = new AccessibleCaretEventHub(this);
    mAccessibleCaretEventHub->Init();
  }

  mSelection = new nsFrameSelection(this, accessibleCaretEnabled);

  // Important: this has to happen after the selection has been set up
#ifdef SHOW_CARET
  // make the caret
  mCaret = new nsCaret();
  mCaret->Init(this);
  mOriginalCaret = mCaret;

  // SetCaretEnabled(true);       // make it show in browser windows
#endif
  // set up selection to be displayed in document
  // Don't enable selection for print media
  nsPresContext::nsPresContextType type = mPresContext->Type();
  if (type != nsPresContext::eContext_PrintPreview &&
      type != nsPresContext::eContext_Print) {
    SetDisplaySelection(nsISelectionController::SELECTION_DISABLED);
  }

  if (gMaxRCProcessingTime == -1) {
    gMaxRCProcessingTime =
        Preferences::GetInt("layout.reflow.timeslice", NS_MAX_REFLOW_TIME);
  }

  if (nsStyleSheetService* ss = nsStyleSheetService::GetInstance()) {
    ss->RegisterPresShell(this);
  }

  {
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      os->AddObserver(this, "memory-pressure", false);
      os->AddObserver(this, NS_WIDGET_WAKE_OBSERVER_TOPIC, false);
      if (XRE_IsParentProcess() && !sProcessInteractable) {
        os->AddObserver(this, "sessionstore-one-or-no-tab-restored", false);
      }
      os->AddObserver(this, "font-info-updated", false);
      os->AddObserver(this, "internal-look-and-feel-changed", false);
    }
  }

#ifdef MOZ_REFLOW_PERF
  if (mReflowCountMgr) {
    bool paintFrameCounts =
        Preferences::GetBool("layout.reflow.showframecounts");

    bool dumpFrameCounts =
        Preferences::GetBool("layout.reflow.dumpframecounts");

    bool dumpFrameByFrameCounts =
        Preferences::GetBool("layout.reflow.dumpframebyframecounts");

    mReflowCountMgr->SetDumpFrameCounts(dumpFrameCounts);
    mReflowCountMgr->SetDumpFrameByFrameCounts(dumpFrameByFrameCounts);
    mReflowCountMgr->SetPaintFrameCounts(paintFrameCounts);
  }
#endif

  for (DocumentTimeline* timelines : mDocument->Timelines()) {
    timelines->UpdateLastRefreshDriverTime();
  }

  // Get our activeness from the docShell.
  ActivenessMaybeChanged();

  // Setup our font inflation preferences.
  mFontSizeInflationEmPerLine = StaticPrefs::font_size_inflation_emPerLine();
  mFontSizeInflationMinTwips = StaticPrefs::font_size_inflation_minTwips();
  mFontSizeInflationLineThreshold =
      StaticPrefs::font_size_inflation_lineThreshold();
  mFontSizeInflationForceEnabled =
      StaticPrefs::font_size_inflation_forceEnabled();
  mFontSizeInflationDisabledInMasterProcess =
      StaticPrefs::font_size_inflation_disabledInMasterProcess();
  // We'll compute the font size inflation state in Initialize(), when we know
  // the document type.

  mTouchManager.Init(this, mDocument);

  if (mPresContext->IsRootContentDocumentCrossProcess()) {
    mZoomConstraintsClient = new ZoomConstraintsClient();
    mZoomConstraintsClient->Init(this, mDocument);

    // We call this to create mMobileViewportManager, if it is needed.
    MaybeRecreateMobileViewportManager(false);
  }

  if (nsCOMPtr<nsIDocShell> docShell = mPresContext->GetDocShell()) {
    if (BrowsingContext* bc = docShell->GetBrowsingContext()) {
      mUnderHiddenEmbedderElement = bc->IsUnderHiddenEmbedderElement();
    }
  }
}

enum TextPerfLogType { eLog_reflow, eLog_loaddone, eLog_totals };

static void LogTextPerfStats(gfxTextPerfMetrics* aTextPerf,
                             PresShell* aPresShell,
                             const gfxTextPerfMetrics::TextCounts& aCounts,
                             float aTime, TextPerfLogType aLogType,
                             const char* aURL) {
  LogModule* tpLog = gfxPlatform::GetLog(eGfxLog_textperf);

  // ignore XUL contexts unless at debug level
  mozilla::LogLevel logLevel = LogLevel::Warning;
  if (aCounts.numContentTextRuns == 0) {
    logLevel = LogLevel::Debug;
  }

  if (!MOZ_LOG_TEST(tpLog, logLevel)) {
    return;
  }

  char prefix[256];

  switch (aLogType) {
    case eLog_reflow:
      SprintfLiteral(prefix, "(textperf-reflow) %p time-ms: %7.0f", aPresShell,
                     aTime);
      break;
    case eLog_loaddone:
      SprintfLiteral(prefix, "(textperf-loaddone) %p time-ms: %7.0f",
                     aPresShell, aTime);
      break;
    default:
      MOZ_ASSERT(aLogType == eLog_totals, "unknown textperf log type");
      SprintfLiteral(prefix, "(textperf-totals) %p", aPresShell);
  }

  double hitRatio = 0.0;
  uint32_t lookups = aCounts.wordCacheHit + aCounts.wordCacheMiss;
  if (lookups) {
    hitRatio = double(aCounts.wordCacheHit) / double(lookups);
  }

  if (aLogType == eLog_loaddone) {
    MOZ_LOG(
        tpLog, logLevel,
        ("%s reflow: %d chars: %d "
         "[%s] "
         "content-textruns: %d chrome-textruns: %d "
         "max-textrun-len: %d "
         "word-cache-lookups: %d word-cache-hit-ratio: %4.3f "
         "word-cache-space: %d word-cache-long: %d "
         "pref-fallbacks: %d system-fallbacks: %d "
         "textruns-const: %d textruns-destr: %d "
         "generic-lookups: %d "
         "cumulative-textruns-destr: %d\n",
         prefix, aTextPerf->reflowCount, aCounts.numChars, (aURL ? aURL : ""),
         aCounts.numContentTextRuns, aCounts.numChromeTextRuns,
         aCounts.maxTextRunLen, lookups, hitRatio, aCounts.wordCacheSpaceRules,
         aCounts.wordCacheLong, aCounts.fallbackPrefs, aCounts.fallbackSystem,
         aCounts.textrunConst, aCounts.textrunDestr, aCounts.genericLookups,
         aTextPerf->cumulative.textrunDestr));
  } else {
    MOZ_LOG(
        tpLog, logLevel,
        ("%s reflow: %d chars: %d "
         "content-textruns: %d chrome-textruns: %d "
         "max-textrun-len: %d "
         "word-cache-lookups: %d word-cache-hit-ratio: %4.3f "
         "word-cache-space: %d word-cache-long: %d "
         "pref-fallbacks: %d system-fallbacks: %d "
         "textruns-const: %d textruns-destr: %d "
         "generic-lookups: %d "
         "cumulative-textruns-destr: %d\n",
         prefix, aTextPerf->reflowCount, aCounts.numChars,
         aCounts.numContentTextRuns, aCounts.numChromeTextRuns,
         aCounts.maxTextRunLen, lookups, hitRatio, aCounts.wordCacheSpaceRules,
         aCounts.wordCacheLong, aCounts.fallbackPrefs, aCounts.fallbackSystem,
         aCounts.textrunConst, aCounts.textrunDestr, aCounts.genericLookups,
         aTextPerf->cumulative.textrunDestr));
  }
}

bool PresShell::InRDMPane() {
  if (Document* doc = GetDocument()) {
    if (BrowsingContext* bc = doc->GetBrowsingContext()) {
      return bc->InRDMPane();
    }
  }
  return false;
}

#if defined(MOZ_WIDGET_ANDROID)
void PresShell::MaybeNotifyShowDynamicToolbar() {
  const DynamicToolbarState dynToolbarState = GetDynamicToolbarState();
  if ((dynToolbarState == DynamicToolbarState::Collapsed ||
       dynToolbarState == DynamicToolbarState::InTransition)) {
    MOZ_ASSERT(mPresContext &&
               mPresContext->IsRootContentDocumentCrossProcess());
    if (BrowserChild* browserChild = BrowserChild::GetFrom(this)) {
      browserChild->SendShowDynamicToolbar();
    }
  }
}
#endif  // defined(MOZ_WIDGET_ANDROID)

void PresShell::Destroy() {
  // Do not add code before this line please!
  if (mHaveShutDown) {
    return;
  }

  NS_ASSERTION(!nsContentUtils::IsSafeToRunScript(),
               "destroy called on presshell while scripts not blocked");

  [[maybe_unused]] nsIURI* uri = mDocument->GetDocumentURI();
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_RELEVANT_FOR_JS(
      "Layout tree destruction", LAYOUT_Destroy,
      uri ? uri->GetSpecOrDefault() : "N/A"_ns);

  // Try to determine if the page is the user had a meaningful opportunity to
  // zoom this page. This is not 100% accurate but should be "good enough" for
  // telemetry purposes.
  auto isUserZoomablePage = [&]() -> bool {
    if (mIsFirstPaint) {
      // Page was never painted, so it wasn't zoomable by the user. We get a
      // handful of these "transient" presShells.
      return false;
    }
    if (!mPresContext->IsRootContentDocumentCrossProcess()) {
      // Not a root content document, so APZ doesn't support zooming it.
      return false;
    }
    if (InRDMPane()) {
      // Responsive design mode is a special case that we want to ignore here.
      return false;
    }
    if (mDocument && mDocument->IsInitialDocument()) {
      // Ignore initial about:blank page loads
      return false;
    }
    if (XRE_IsContentProcess() &&
        IsExtensionRemoteType(ContentChild::GetSingleton()->GetRemoteType())) {
      // Also omit presShells from the extension process because they sometimes
      // can't be zoomed by the user.
      return false;
    }
    // Otherwise assume the page is user-zoomable.
    return true;
  };
  if (isUserZoomablePage()) {
    glean::apz_zoom::activity
        .EnumGet(static_cast<glean::apz_zoom::ActivityLabel>(
            IsResolutionUpdatedByApz()))
        .Add();
  }

  // dump out cumulative text perf metrics
  gfxTextPerfMetrics* tp;
  if (mPresContext && (tp = mPresContext->GetTextPerfMetrics())) {
    tp->Accumulate();
    if (tp->cumulative.numChars > 0) {
      LogTextPerfStats(tp, this, tp->cumulative, 0.0, eLog_totals, nullptr);
    }
  }
  if (mPresContext) {
    if (gfxUserFontSet* fs = mPresContext->GetUserFontSet()) {
      uint32_t fontCount;
      uint64_t fontSize;
      fs->GetLoadStatistics(fontCount, fontSize);
      glean::webfont::per_page.Add(fontCount);
      glean::webfont::size_per_page.Accumulate(uint32_t(fontSize / 1024));
    } else {
      glean::webfont::per_page.Add(0);
      glean::webfont::size_per_page.Accumulate(0);
    }
  }

#ifdef MOZ_REFLOW_PERF
  DumpReflows();
  mReflowCountMgr = nullptr;
#endif

  if (mZoomConstraintsClient) {
    mZoomConstraintsClient->Destroy();
    mZoomConstraintsClient = nullptr;
  }
  if (mMobileViewportManager) {
    mMobileViewportManager->Destroy();
    mMobileViewportManager = nullptr;
    mMVMContext = nullptr;
  }

#ifdef ACCESSIBILITY
  if (mDocAccessible) {
#  ifdef DEBUG
    if (a11y::logging::IsEnabled(a11y::logging::eDocDestroy))
      a11y::logging::DocDestroy("presshell destroyed", mDocument);
#  endif

    mDocAccessible->Shutdown();
    mDocAccessible = nullptr;
  }
#endif  // ACCESSIBILITY

  MaybeReleaseCapturingContent();

  EventHandler::OnPresShellDestroy(mDocument);

  if (mContentToScrollTo) {
    mContentToScrollTo->RemoveProperty(nsGkAtoms::scrolling);
    mContentToScrollTo = nullptr;
  }

  if (mPresContext) {
    // We need to notify the destroying the nsPresContext to ESM for
    // suppressing to use from ESM.
    mPresContext->EventStateManager()->NotifyDestroyPresContext(mPresContext);
  }

  if (nsStyleSheetService* ss = nsStyleSheetService::GetInstance()) {
    ss->UnregisterPresShell(this);
  }

  {
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      os->RemoveObserver(this, "memory-pressure");
      os->RemoveObserver(this, NS_WIDGET_WAKE_OBSERVER_TOPIC);
      if (XRE_IsParentProcess()) {
        os->RemoveObserver(this, "sessionstore-one-or-no-tab-restored");
      }
      os->RemoveObserver(this, "font-info-updated");
      os->RemoveObserver(this, "internal-look-and-feel-changed");
    }
  }

  // If our paint suppression timer is still active, kill it.
  CancelPaintSuppressionTimer();

  mSynthMouseMoveEvent.Revoke();

  mUpdateApproximateFrameVisibilityEvent.Revoke();

  ClearApproximatelyVisibleFramesList(Some(OnNonvisible::DiscardImages));

  if (mOriginalCaret) {
    mOriginalCaret->Terminate();
  }
  if (mCaret && mCaret != mOriginalCaret) {
    mCaret->Terminate();
  }
  mCaret = mOriginalCaret = nullptr;

  mFocusedFrameSelection = nullptr;

  if (mSelection) {
    RefPtr<nsFrameSelection> frameSelection = mSelection;
    frameSelection->DisconnectFromPresShell();
  }

  mIsDestroying = true;

  // We can't release all the event content in
  // mCurrentEventContentStack here since there might be code on the
  // stack that will release the event content too. Double release
  // bad!

  // The frames will be torn down, so remove them from the current
  // event frame stack (since they'd be dangling references if we'd
  // leave them in) and null out the mCurrentEventFrame pointer as
  // well.

  mCurrentEventTarget.ClearFrame();

  for (EventTargetInfo& eventTargetInfo : mCurrentEventTargetStack) {
    eventTargetInfo.ClearFrame();
  }

  mFramesToDirty.Clear();
  mPendingScrollAnchorSelection.Clear();
  mPendingScrollAnchorAdjustment.Clear();
  mPendingScrollResnap.Clear();

  if (mViewManager) {
    // Clear the view manager's weak pointer back to |this| in case it
    // was leaked.
    mViewManager->SetPresShell(nullptr);
    mViewManager = nullptr;
  }

  // This shell must be removed from the document before the frame
  // hierarchy is torn down to avoid finding deleted frames through
  // this presshell while the frames are being torn down
  if (mDocument) {
    NS_ASSERTION(mDocument->GetPresShell() == this, "Wrong shell?");
    mDocument->ClearServoRestyleRoot();
    mDocument->DeletePresShell();
  }

  if (mPresContext) {
    mPresContext->AnimationEventDispatcher()->ClearEventQueue();
  }

  // Revoke any pending events.  We need to do this and cancel pending reflows
  // before we destroy the frame constructor, since apparently frame destruction
  // sometimes spins the event queue when plug-ins are involved(!).
  // XXXmats is this still needed now that plugins are gone?
  CancelAllPendingReflows();
  CancelPostedReflowCallbacks();

  // Destroy the frame constructor. This will destroy the frame hierarchy
  mFrameConstructor->WillDestroyFrameTree();

  NS_WARNING_ASSERTION(!mAutoWeakFrames && mWeakFrames.IsEmpty(),
                       "Weak frames alive after destroying FrameManager");
  while (mAutoWeakFrames) {
    mAutoWeakFrames->Clear(this);
  }
  const nsTArray<WeakFrame*> weakFrames = ToArray(mWeakFrames);
  for (WeakFrame* weakFrame : weakFrames) {
    weakFrame->Clear(this);
  }

  // Terminate AccessibleCaretEventHub after tearing down the frame tree so that
  // we don't need to remove caret element's frame in
  // AccessibleCaret::RemoveCaretElement().
  if (mAccessibleCaretEventHub) {
    mAccessibleCaretEventHub->Terminate();
    mAccessibleCaretEventHub = nullptr;
  }

  if (mPresContext) {
    // We hold a reference to the pres context, and it holds a weak link back
    // to us. To avoid the pres context having a dangling reference, set its
    // pres shell to nullptr
    mPresContext->DetachPresShell();
  }

  mHaveShutDown = true;

  mTouchManager.Destroy();
}

void PresShell::StartObservingRefreshDriver() {
  nsRefreshDriver* rd = mPresContext->RefreshDriver();
  if (mResizeEventPending || mVisualViewportResizeEventPending) {
    rd->ScheduleRenderingPhase(mozilla::RenderingPhase::ResizeSteps);
  }
  if (mNeedLayoutFlush || mNeedStyleFlush) {
    rd->ScheduleRenderingPhase(mozilla::RenderingPhase::Layout);
  }
}

nsRefreshDriver* PresShell::GetRefreshDriver() const {
  return mPresContext ? mPresContext->RefreshDriver() : nullptr;
}

void PresShell::SetAuthorStyleDisabled(bool aStyleDisabled) {
  if (aStyleDisabled != StyleSet()->GetAuthorStyleDisabled()) {
    StyleSet()->SetAuthorStyleDisabled(aStyleDisabled);
    mDocument->ApplicableStylesChanged();

    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->NotifyObservers(
          ToSupports(mDocument), "author-style-disabled-changed", nullptr);
    }
  }
}

bool PresShell::GetAuthorStyleDisabled() const {
  return StyleSet()->GetAuthorStyleDisabled();
}

void PresShell::AddUserSheet(StyleSheet* aSheet) {
  // Make sure this does what nsDocumentViewer::CreateStyleSet does wrt
  // ordering. We want this new sheet to come after all the existing stylesheet
  // service sheets (which are at the start), but before other user sheets; see
  // nsIStyleSheetService.idl for the ordering.

  nsStyleSheetService* sheetService = nsStyleSheetService::GetInstance();
  nsTArray<RefPtr<StyleSheet>>& userSheets = *sheetService->UserStyleSheets();

  // Search for the place to insert the new user sheet. Since all of the
  // stylesheet service provided user sheets should be at the start of the style
  // set's list, and aSheet should be at the end of userSheets. Given that, we
  // can find the right place to insert the new sheet based on the length of
  // userSheets.
  MOZ_ASSERT(aSheet);
  MOZ_ASSERT(userSheets.LastElement() == aSheet);

  size_t index = userSheets.Length() - 1;

  // Assert that all of userSheets (except for the last, new element) matches up
  // with what's in the style set.
  for (size_t i = 0; i < index; ++i) {
    MOZ_ASSERT(StyleSet()->SheetAt(StyleOrigin::User, i) == userSheets[i]);
  }

  if (index == static_cast<size_t>(StyleSet()->SheetCount(StyleOrigin::User))) {
    StyleSet()->AppendStyleSheet(*aSheet);
  } else {
    StyleSheet* ref = StyleSet()->SheetAt(StyleOrigin::User, index);
    StyleSet()->InsertStyleSheetBefore(*aSheet, *ref);
  }

  mDocument->ApplicableStylesChanged();
}

void PresShell::AddAgentSheet(StyleSheet* aSheet) {
  // Make sure this does what nsDocumentViewer::CreateStyleSet does
  // wrt ordering.
  StyleSet()->AppendStyleSheet(*aSheet);
  mDocument->ApplicableStylesChanged();
}

void PresShell::AddAuthorSheet(StyleSheet* aSheet) {
  // Document specific "additional" Author sheets should be stronger than the
  // ones added with the StyleSheetService.
  StyleSheet* firstAuthorSheet = mDocument->GetFirstAdditionalAuthorSheet();
  if (firstAuthorSheet) {
    StyleSet()->InsertStyleSheetBefore(*aSheet, *firstAuthorSheet);
  } else {
    StyleSet()->AppendStyleSheet(*aSheet);
  }

  mDocument->ApplicableStylesChanged();
}

bool PresShell::NeedsFocusFixUp() const {
  if (NS_WARN_IF(!mDocument)) {
    return false;
  }

  nsIContent* currentFocus = mDocument->GetUnretargetedFocusedContent(
      Document::IncludeChromeOnly::Yes);
  if (!currentFocus) {
    return false;
  }

  // If focus target is an area element with one or more shapes that are
  // focusable areas.
  if (auto* area = HTMLAreaElement::FromNode(currentFocus)) {
    if (nsFocusManager::IsAreaElementFocusable(*area)) {
      return false;
    }
  }

  nsIFrame* f = currentFocus->GetPrimaryFrame();
  if (f && f->IsFocusable()) {
    return false;
  }

  if (currentFocus == mDocument->GetBody() ||
      currentFocus == mDocument->GetRootElement()) {
    return false;
  }

  return true;
}

bool PresShell::FixUpFocus() {
  if (!NeedsFocusFixUp()) {
    return false;
  }
  RefPtr fm = nsFocusManager::GetFocusManager();
  nsCOMPtr<nsPIDOMWindowOuter> window = mDocument->GetWindow();
  if (NS_WARN_IF(!window)) {
    return false;
  }
  fm->ClearFocus(window);
  return true;
}

void PresShell::SelectionWillTakeFocus() {
  if (mSelection) {
    FrameSelectionWillTakeFocus(*mSelection,
                                CanMoveLastSelectionForToString::No);
  }
}

void PresShell::SelectionWillLoseFocus() {
  // Do nothing, the main selection is the default focused selection.
}

// Selection repainting code relies on selection offsets being properly
// adjusted (see bug 1626291), so we need to wait until the DOM is finished
// notifying.
static void RepaintNormalSelectionWhenSafe(nsFrameSelection& aFrameSelection) {
  if (nsContentUtils::IsSafeToRunScript()) {
    aFrameSelection.RepaintSelection(SelectionType::eNormal);
    return;
  }

  // Note that importantly we don't defer changing the DisplaySelection. That'd
  // be potentially racy with other code that may change it.
  nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
      "RepaintNormalSelectionWhenSafe",
      [sel = RefPtr<nsFrameSelection>(&aFrameSelection)] {
        sel->RepaintSelection(SelectionType::eNormal);
      }));
}

void PresShell::FrameSelectionWillLoseFocus(nsFrameSelection& aFrameSelection) {
  if (mFocusedFrameSelection != &aFrameSelection) {
    return;
  }

  // Do nothing, the main selection is the default focused selection.
  if (&aFrameSelection == mSelection) {
    return;
  }

  RefPtr<nsFrameSelection> old = std::move(mFocusedFrameSelection);
  MOZ_ASSERT(!mFocusedFrameSelection);

  if (old->GetDisplaySelection() != nsISelectionController::SELECTION_HIDDEN) {
    old->SetDisplaySelection(nsISelectionController::SELECTION_HIDDEN);
    RepaintNormalSelectionWhenSafe(*old);
  }

  if (mSelection) {
    FrameSelectionWillTakeFocus(*mSelection,
                                CanMoveLastSelectionForToString::No);
  }
}

void PresShell::FrameSelectionWillTakeFocus(
    nsFrameSelection& aFrameSelection,
    CanMoveLastSelectionForToString aCanMoveLastSelectionForToString) {
  if (StaticPrefs::dom_selection_mimic_chrome_tostring_enabled()) {
    if (aCanMoveLastSelectionForToString ==
        CanMoveLastSelectionForToString::Yes) {
      UpdateLastSelectionForToString(&aFrameSelection);
    }
  }
  if (mFocusedFrameSelection == &aFrameSelection) {
#ifdef XP_MACOSX
    // FIXME: Mac needs to update the global selection cache, even if the
    // document's focused selection doesn't change, and this is currently done
    // from RepaintSelection. Maybe we should move part of the global selection
    // handling here, or something of that sort, unclear.
    RepaintNormalSelectionWhenSafe(aFrameSelection);
#endif
    return;
  }

  RefPtr<nsFrameSelection> old = std::move(mFocusedFrameSelection);
  mFocusedFrameSelection = &aFrameSelection;

  if (old &&
      old->GetDisplaySelection() != nsISelectionController::SELECTION_HIDDEN) {
    old->SetDisplaySelection(nsISelectionController::SELECTION_HIDDEN);
    RepaintNormalSelectionWhenSafe(*old);
  }

  if (aFrameSelection.GetDisplaySelection() !=
      nsISelectionController::SELECTION_ON) {
    aFrameSelection.SetDisplaySelection(nsISelectionController::SELECTION_ON);
    RepaintNormalSelectionWhenSafe(aFrameSelection);
  }
}

void PresShell::UpdateLastSelectionForToString(
    const nsFrameSelection* aFrameSelection) {
  if (mLastSelectionForToString != aFrameSelection) {
    mLastSelectionForToString = aFrameSelection;
  }
}

NS_IMETHODIMP
PresShell::SetDisplaySelection(int16_t aToggle) {
  mSelection->SetDisplaySelection(aToggle);
  return NS_OK;
}

NS_IMETHODIMP
PresShell::GetDisplaySelection(int16_t* aToggle) {
  *aToggle = mSelection->GetDisplaySelection();
  return NS_OK;
}

NS_IMETHODIMP
PresShell::GetSelectionFromScript(RawSelectionType aRawSelectionType,
                                  Selection** aSelection) {
  if (!aSelection || !mSelection) {
    return NS_ERROR_NULL_POINTER;
  }

  RefPtr<Selection> selection =
      mSelection->GetSelection(ToSelectionType(aRawSelectionType));

  if (!selection) {
    return NS_ERROR_INVALID_ARG;
  }

  selection.forget(aSelection);
  return NS_OK;
}

Selection* PresShell::GetSelection(RawSelectionType aRawSelectionType) {
  if (!mSelection) {
    return nullptr;
  }

  return mSelection->GetSelection(ToSelectionType(aRawSelectionType));
}

Selection* PresShell::GetCurrentSelection(SelectionType aSelectionType) {
  if (!mSelection) {
    return nullptr;
  }

  return mSelection->GetSelection(aSelectionType);
}

nsFrameSelection* PresShell::GetLastFocusedFrameSelection() {
  return mFocusedFrameSelection ? mFocusedFrameSelection : mSelection;
}

NS_IMETHODIMP
PresShell::ScrollSelectionIntoView(RawSelectionType aRawSelectionType,
                                   SelectionRegion aRegion,
                                   ControllerScrollFlags aFlags) {
  if (!mSelection) {
    return NS_ERROR_NULL_POINTER;
  }

  RefPtr<nsFrameSelection> frameSelection = mSelection;
  return frameSelection->ScrollSelectionIntoView(
      ToSelectionType(aRawSelectionType), aRegion, aFlags);
}

NS_IMETHODIMP
PresShell::RepaintSelection(RawSelectionType aRawSelectionType) {
  if (!mSelection) {
    return NS_ERROR_NULL_POINTER;
  }

  if (MOZ_UNLIKELY(mIsDestroying)) {
    return NS_OK;
  }

  RefPtr<nsFrameSelection> frameSelection = mSelection;
  return frameSelection->RepaintSelection(ToSelectionType(aRawSelectionType));
}

// Make shell be a document observer
void PresShell::BeginObservingDocument() {
  if (mDocument && !mIsDestroying) {
    mIsObservingDocument = true;
    if (mIsDocumentGone) {
      NS_WARNING(
          "Adding a presshell that was disconnected from the document "
          "as a document observer?  Sounds wrong...");
      mIsDocumentGone = false;
    }
  }
}

// Make shell stop being a document observer
void PresShell::EndObservingDocument() {
  // XXXbz do we need to tell the frame constructor that the document
  // is gone, perhaps?  Except for printing it's NOT gone, sometimes.
  mIsDocumentGone = true;
  mIsObservingDocument = false;
}

void PresShell::InitPaintSuppressionTimer() {
  // Default to PAINTLOCK_EVENT_DELAY if we can't get the pref value.
  Document* doc = mDocument->GetDisplayDocument()
                      ? mDocument->GetDisplayDocument()
                      : mDocument.get();
  const bool inProcess = !doc->GetBrowsingContext() ||
                         doc->GetBrowsingContext()->Top()->IsInProcess();
  int32_t delay = inProcess
                      ? StaticPrefs::nglayout_initialpaint_delay()
                      : StaticPrefs::nglayout_initialpaint_delay_in_oopif();
  mPaintSuppressionTimer->InitWithNamedFuncCallback(
      [](nsITimer* aTimer, void* aPresShell) {
        RefPtr<PresShell> self = static_cast<PresShell*>(aPresShell);
        self->UnsuppressPainting();
      },
      this, delay, nsITimer::TYPE_ONE_SHOT,
      "PresShell::sPaintSuppressionCallback");
}

nsresult PresShell::Initialize() {
  if (mIsDestroying) {
    return NS_OK;
  }

  if (!mDocument) {
    // Nothing to do
    return NS_OK;
  }

  MOZ_LOG(gLog, LogLevel::Debug, ("PresShell::Initialize this=%p", this));

  NS_ASSERTION(!mDidInitialize, "Why are we being called?");

  RefPtr<PresShell> kungFuDeathGrip(this);

  RecomputeFontSizeInflationEnabled();
  MOZ_DIAGNOSTIC_ASSERT(!mIsDestroying);

  // Ensure the pres context doesn't think it has changed, since we haven't even
  // started layout. This avoids spurious restyles / reflows afterwards.
  //
  // Note that this is very intentionally before setting mDidInitialize so it
  // doesn't notify the document, or run media query change events.
  mPresContext->FlushPendingMediaFeatureValuesChanged();
  MOZ_DIAGNOSTIC_ASSERT(!mIsDestroying);

  mDidInitialize = true;

#ifdef DEBUG
  if (VerifyReflowFlags::NoisyCommands & gVerifyReflowFlags) {
    if (mDocument) {
      nsIURI* uri = mDocument->GetDocumentURI();
      if (uri) {
        printf("*** PresShell::Initialize (this=%p, url='%s')\n", (void*)this,
               uri->GetSpecOrDefault().get());
      }
    }
  }
#endif

  // Get the root frame from the frame constructor.
  // XXXbz it would be nice to move this somewhere else... like frame manager
  // Init(), say.  But we need to make sure our views are all set up by the
  // time we do this!
  MOZ_ASSERT(!mFrameConstructor->GetRootFrame(),
             "How did that happen, exactly?");
  ViewportFrame* rootFrame;
  {
    nsAutoScriptBlocker scriptBlocker;
    rootFrame = mFrameConstructor->ConstructRootFrame();
    mFrameConstructor->SetRootFrame(rootFrame);
  }

  NS_ENSURE_STATE(!mHaveShutDown);

  if (!rootFrame) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (Element* root = mDocument->GetRootElement()) {
    {
      nsAutoCauseReflowNotifier reflowNotifier(this);
      // Have the style sheet processor construct frame for the root
      // content object down
      mFrameConstructor->ContentInserted(
          root, nsCSSFrameConstructor::InsertionKind::Sync);
    }
    // Something in mFrameConstructor->ContentInserted may have caused
    // Destroy() to get called, bug 337586.  Or, nsAutoCauseReflowNotifier
    // (which sets up a script blocker) going out of scope may have killed us
    // too
    NS_ENSURE_STATE(!mHaveShutDown);
  }

  mDocument->MaybeScheduleRendering();

  NS_ASSERTION(rootFrame, "How did that happen?");

  // Note: when the frame was created above it had the NS_FRAME_IS_DIRTY bit
  // set, but XBL processing could have caused a reflow which clears it.
  if (MOZ_LIKELY(rootFrame->HasAnyStateBits(NS_FRAME_IS_DIRTY))) {
    // Unset the DIRTY bits so that FrameNeedsReflow() will work right.
    rootFrame->RemoveStateBits(NS_FRAME_IS_DIRTY | NS_FRAME_HAS_DIRTY_CHILDREN);
    NS_ASSERTION(!mDirtyRoots.Contains(rootFrame),
                 "Why is the root in mDirtyRoots already?");
    FrameNeedsReflow(rootFrame, IntrinsicDirty::None, NS_FRAME_IS_DIRTY);
    NS_ASSERTION(mDirtyRoots.Contains(rootFrame),
                 "Should be in mDirtyRoots now");
    NS_ASSERTION(mNeedStyleFlush || mNeedLayoutFlush,
                 "Why no reflow scheduled?");
  }

  // Restore our root scroll position now if we're getting here after EndLoad
  // got called, since this is our one chance to do it.  Note that we need not
  // have reflowed for this to work; when the scrollframe is finally reflowed
  // it'll pick up the position we store in it here.
  if (!mDocumentLoading) {
    RestoreRootScrollPosition();
  }

  // For printing, we just immediately unsuppress.
  if (!mPresContext->IsPaginated()) {
    // Kick off a one-shot timer based off our pref value.  When this timer
    // fires, if painting is still locked down, then we will go ahead and
    // trigger a full invalidate and allow painting to proceed normally.
    mPaintingSuppressed = true;
    // Don't suppress painting if the document isn't loading.
    Document::ReadyState readyState = mDocument->GetReadyStateEnum();
    if (readyState != Document::READYSTATE_COMPLETE) {
      mPaintSuppressionTimer = NS_NewTimer();
    }
    if (!mPaintSuppressionTimer) {
      mPaintingSuppressed = false;
    } else {
      // Initialize the timer.
      mPaintSuppressionTimer->SetTarget(GetMainThreadSerialEventTarget());
      InitPaintSuppressionTimer();
      if (mHasTriedFastUnsuppress) {
        // Someone tried to unsuppress painting before Initialize was called so
        // unsuppress painting rather soon.
        mHasTriedFastUnsuppress = false;
        TryUnsuppressPaintingSoon();
        MOZ_ASSERT(mHasTriedFastUnsuppress);
      }
    }
  }

  // If we get here and painting is not suppressed, we still want to run the
  // unsuppression logic, so set mShouldUnsuppressPainting to true.
  if (!mPaintingSuppressed) {
    mShouldUnsuppressPainting = true;
  }

  return NS_OK;  // XXX this needs to be real. MMP
}

void PresShell::TryUnsuppressPaintingSoon() {
  if (mHasTriedFastUnsuppress) {
    return;
  }
  mHasTriedFastUnsuppress = true;

  if (!mDidInitialize || !IsPaintingSuppressed() || !XRE_IsContentProcess()) {
    return;
  }

  if (!mDocument->IsInitialDocument() &&
      mDocument->DidHitCompleteSheetCache() &&
      mPresContext->IsRootContentDocumentCrossProcess()) {
    // Try to unsuppress faster on a top level page if it uses stylesheet
    // cache, since that hints that many resources can be painted sooner than
    // in a cold page load case.
    NS_DispatchToCurrentThreadQueue(
        NS_NewRunnableFunction("PresShell::TryUnsuppressPaintingSoon",
                               [self = RefPtr{this}]() -> void {
                                 if (self->IsPaintingSuppressed()) {
                                   PROFILER_MARKER_UNTYPED(
                                       "Fast paint unsuppression", GRAPHICS);
                                   self->UnsuppressPainting();
                                 }
                               }),
        EventQueuePriority::Control);
  }
}

void PresShell::RefreshZoomConstraintsForScreenSizeChange() {
  if (mZoomConstraintsClient) {
    mZoomConstraintsClient->ScreenSizeChanged();
  }
}

void PresShell::ForceResizeReflowWithCurrentDimensions() {
  nscoord currentWidth = 0;
  nscoord currentHeight = 0;
  mViewManager->GetWindowDimensions(&currentWidth, &currentHeight);
  ResizeReflow(currentWidth, currentHeight);
}

void PresShell::ResizeReflow(nscoord aWidth, nscoord aHeight,
                             ResizeReflowOptions aOptions) {
  if (mZoomConstraintsClient) {
    // If we have a ZoomConstraintsClient and the available screen area
    // changed, then we might need to disable double-tap-to-zoom, so notify
    // the ZCC to update itself.
    mZoomConstraintsClient->ScreenSizeChanged();
  }
  if (UsesMobileViewportSizing()) {
    // If we are using mobile viewport sizing, request a reflow from the MVM.
    // It can recompute the final CSS viewport and trigger a call to
    // ResizeReflowIgnoreOverride if it changed. We don't force adjusting
    // of resolution, because that is only necessary when we are destroying
    // the MVM.
    MOZ_ASSERT(mMobileViewportManager);
    mMobileViewportManager->RequestReflow(false);
    return;
  }
  ResizeReflowIgnoreOverride(aWidth, aHeight, aOptions);
}

bool PresShell::SimpleResizeReflow(nscoord aWidth, nscoord aHeight) {
  MOZ_ASSERT(aWidth != NS_UNCONSTRAINEDSIZE);
  MOZ_ASSERT(aHeight != NS_UNCONSTRAINEDSIZE);
  nsSize oldSize = mPresContext->GetVisibleArea().Size();
  mPresContext->SetVisibleArea(nsRect(0, 0, aWidth, aHeight));
  nsIFrame* rootFrame = GetRootFrame();
  if (!rootFrame) {
    return false;
  }
  WritingMode wm = rootFrame->GetWritingMode();
  bool isBSizeChanging =
      wm.IsVertical() ? oldSize.width != aWidth : oldSize.height != aHeight;
  if (isBSizeChanging) {
    nsLayoutUtils::MarkIntrinsicISizesDirtyIfDependentOnBSize(rootFrame);
    rootFrame->SetHasBSizeChange(true);
  }
  FrameNeedsReflow(rootFrame, IntrinsicDirty::None,
                   NS_FRAME_HAS_DIRTY_CHILDREN);

  if (mMobileViewportManager) {
    mMobileViewportManager->UpdateSizesBeforeReflow();
  }
  return true;
}

bool PresShell::CanHandleUserInputEvents(WidgetGUIEvent* aGUIEvent) {
  if (XRE_IsParentProcess()) {
    return true;
  }

  if (aGUIEvent->mFlags.mIsSynthesizedForTests &&
      !StaticPrefs::dom_input_events_security_isUserInputHandlingDelayTest()) {
    return true;
  }

  if (!aGUIEvent->IsUserAction()) {
    return true;
  }

  if (nsPresContext* rootPresContext = mPresContext->GetRootPresContext()) {
    return rootPresContext->UserInputEventsAllowed();
  }

  return true;
}

void PresShell::PostScrollEvent(Runnable* aEvent) {
  MOZ_ASSERT(aEvent);
  mPendingScrollEvents.AppendElement(aEvent);

  // If we (or any descendant docs) have any content visibility: auto elements,
  // we also need to run its proximity to the viewport on scroll. Same for
  // intersection observers.
  //
  // We don't need to mark ourselves as needing a layout flush. We don't need to
  // get flushed, we just need the viewport relevancy / content-visibility: auto
  // viewport proximity phases to run.
  mPresContext->RefreshDriver()->ScheduleRenderingPhases(
      {RenderingPhase::ScrollSteps, RenderingPhase::Layout,
       RenderingPhase::UpdateIntersectionObservations});
}

void PresShell::ScheduleResizeEventIfNeeded(ResizeEventKind aKind) {
  if (mIsDestroying) {
    return;
  }
  if (MOZ_UNLIKELY(mDocument->GetBFCacheEntry())) {
    return;
  }
  if (aKind == ResizeEventKind::Regular) {
    mResizeEventPending = true;
  } else {
    MOZ_ASSERT(aKind == ResizeEventKind::Visual);
    mVisualViewportResizeEventPending = true;
  }
  mPresContext->RefreshDriver()->ScheduleRenderingPhase(
      RenderingPhase::ResizeSteps);
}

bool PresShell::ResizeReflowIgnoreOverride(nscoord aWidth, nscoord aHeight,
                                           ResizeReflowOptions aOptions) {
  MOZ_ASSERT(!mIsReflowing, "Shouldn't be in reflow here!");

  // Historically we never fired resize events if there was no root frame by the
  // time this function got called.
  const bool initialized = mDidInitialize;
  RefPtr<PresShell> kungFuDeathGrip(this);

  auto postResizeEventIfNeeded = [this, initialized]() {
    if (initialized) {
      ScheduleResizeEventIfNeeded(ResizeEventKind::Regular);
    }
  };

  if (!(aOptions & ResizeReflowOptions::BSizeLimit)) {
    nsSize oldSize = mPresContext->GetVisibleArea().Size();
    if (oldSize == nsSize(aWidth, aHeight)) {
      return false;
    }

    bool changed = SimpleResizeReflow(aWidth, aHeight);
    postResizeEventIfNeeded();
    return changed;
  }

  // Make sure that style is flushed before setting the pres context
  // VisibleArea.
  //
  // Otherwise we may end up with bogus viewport units resolved against the
  // unconstrained bsize, or restyling the whole document resolving viewport
  // units against targetWidth, which may end up doing wasteful work.
  mDocument->FlushPendingNotifications(FlushType::Frames);

  nsIFrame* rootFrame = GetRootFrame();
  if (mIsDestroying || !rootFrame) {
    // If we don't have a root frame yet, that means we haven't had our initial
    // reflow... If that's the case, and aWidth or aHeight is unconstrained,
    // ignore them altogether.
    if (aHeight == NS_UNCONSTRAINEDSIZE || aWidth == NS_UNCONSTRAINEDSIZE) {
      // We can't do the work needed for SizeToContent without a root
      // frame, and we want to return before setting the visible area.
      return false;
    }

    mPresContext->SetVisibleArea(nsRect(0, 0, aWidth, aHeight));
    // There isn't anything useful we can do if the initial reflow hasn't
    // happened.
    return true;
  }

  WritingMode wm = rootFrame->GetWritingMode();
  MOZ_ASSERT((wm.IsVertical() ? aHeight : aWidth) != NS_UNCONSTRAINEDSIZE,
             "unconstrained isize not allowed");

  nscoord targetWidth = aWidth;
  nscoord targetHeight = aHeight;
  if (wm.IsVertical()) {
    targetWidth = NS_UNCONSTRAINEDSIZE;
  } else {
    targetHeight = NS_UNCONSTRAINEDSIZE;
  }

  mPresContext->SetVisibleArea(nsRect(0, 0, targetWidth, targetHeight));
  // XXX Do a full invalidate at the beginning so that invalidates along
  // the way don't have region accumulation issues?

  // For height:auto BSizes (i.e. layout-controlled), descendant
  // intrinsic sizes can't depend on them. So the only other case is
  // viewport-controlled BSizes which we handle here.
  nsLayoutUtils::MarkIntrinsicISizesDirtyIfDependentOnBSize(rootFrame);
  rootFrame->SetHasBSizeChange(true);
  FrameNeedsReflow(rootFrame, IntrinsicDirty::None,
                   NS_FRAME_HAS_DIRTY_CHILDREN);

  {
    nsAutoCauseReflowNotifier crNotifier(this);
    WillDoReflow();

    // Kick off a top-down reflow
    AUTO_LAYOUT_PHASE_ENTRY_POINT(GetPresContext(), Reflow);
    nsViewManager::AutoDisableRefresh refreshBlocker(mViewManager);

    mDirtyRoots.Remove(rootFrame);
    DoReflow(rootFrame, true, nullptr);

    const bool reflowAgain =
        wm.IsVertical() ? mPresContext->GetVisibleArea().width > aWidth
                        : mPresContext->GetVisibleArea().height > aHeight;

    if (reflowAgain) {
      mPresContext->SetVisibleArea(nsRect(0, 0, aWidth, aHeight));
      rootFrame->SetHasBSizeChange(true);
      DoReflow(rootFrame, true, nullptr);
    }
  }

  // Now, we may have been destroyed by the destructor of
  // `nsAutoCauseReflowNotifier`.

  mPendingDidDoReflow = true;
  DidDoReflow(true);

  // the reflow above should've set our bsize if it was NS_UNCONSTRAINEDSIZE,
  // and the isize shouldn't be NS_UNCONSTRAINEDSIZE anyway.
  MOZ_DIAGNOSTIC_ASSERT(
      mPresContext->GetVisibleArea().width != NS_UNCONSTRAINEDSIZE,
      "width should not be NS_UNCONSTRAINEDSIZE after reflow");
  MOZ_DIAGNOSTIC_ASSERT(
      mPresContext->GetVisibleArea().height != NS_UNCONSTRAINEDSIZE,
      "height should not be NS_UNCONSTRAINEDSIZE after reflow");

  postResizeEventIfNeeded();
  return true;
}

// https://drafts.csswg.org/cssom-view/#document-run-the-resize-steps
void PresShell::RunResizeSteps() {
  if (!mResizeEventPending && !mVisualViewportResizeEventPending) {
    return;
  }
  if (mIsDocumentGone) {
    return;
  }

  RefPtr window = nsGlobalWindowInner::Cast(mDocument->GetInnerWindow());
  if (!window) {
    return;
  }

  if (mResizeEventPending) {
    // Clear it before firing, just in case the event triggers another resize
    // event. Such event will fire next tick.
    mResizeEventPending = false;
    WidgetEvent event(true, mozilla::eResize);
    nsEventStatus status = nsEventStatus_eIgnore;

    if (RefPtr<nsPIDOMWindowOuter> outer = window->GetOuterWindow()) {
      // MOZ_KnownLive due to bug 1506441
      EventDispatcher::Dispatch(MOZ_KnownLive(nsGlobalWindowOuter::Cast(outer)),
                                mPresContext, &event, nullptr, &status);
    }
  }

  if (mVisualViewportResizeEventPending) {
    mVisualViewportResizeEventPending = false;
    RefPtr vv = window->VisualViewport();
    vv->FireResizeEvent();
  }
}

// https://drafts.csswg.org/cssom-view/#document-run-the-scroll-steps
// But note: https://github.com/w3c/csswg-drafts/issues/11164
void PresShell::RunScrollSteps() {
  // Scroll events are one-shot, so after running them we can drop them.
  // However, dispatching a scroll event can potentially cause more scroll
  // events to be posted, so we move the initial set into a temporary array
  // first. (Newly posted scroll events will be dispatched on the next tick.)
  auto events = std::move(mPendingScrollEvents);
  for (auto& event : events) {
    event->Run();
  }
}

static nsIContent* GetNativeAnonymousSubtreeRoot(nsIContent* aContent) {
  if (!aContent) {
    return nullptr;
  }
  return aContent->GetClosestNativeAnonymousSubtreeRoot();
}

void PresShell::NativeAnonymousContentWillBeRemoved(nsIContent* aAnonContent) {
  MOZ_ASSERT(aAnonContent->IsRootOfNativeAnonymousSubtree());
  mPresContext->EventStateManager()->NativeAnonymousContentRemoved(
      aAnonContent);
#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->ContentRemoved(this, aAnonContent);
  }
#endif
  if (mDocument->DevToolsAnonymousAndShadowEventsEnabled()) {
    aAnonContent->QueueDevtoolsAnonymousEvent(/* aIsRemove = */ true);
  }
  if (nsIContent* root =
          GetNativeAnonymousSubtreeRoot(mCurrentEventTarget.mContent)) {
    if (aAnonContent == root) {
      mCurrentEventTarget.UpdateFrameAndContent(
          nullptr, aAnonContent->GetFlattenedTreeParent());
    }
  }

  for (EventTargetInfo& eventTargetInfo : mCurrentEventTargetStack) {
    nsIContent* anon = GetNativeAnonymousSubtreeRoot(eventTargetInfo.mContent);
    if (aAnonContent == anon) {
      eventTargetInfo.UpdateFrameAndContent(
          nullptr, aAnonContent->GetFlattenedTreeParent());
    }
  }
}

void PresShell::SetIgnoreFrameDestruction(bool aIgnore) {
  if (mDocument) {
    // We need to tell the ImageLoader to drop all its references to frames
    // because they're about to go away and it won't get notifications of that.
    mDocument->StyleImageLoader()->ClearFrames(mPresContext);
  }
  mIgnoreFrameDestruction = aIgnore;
}

void PresShell::NotifyDestroyingFrame(nsIFrame* aFrame) {
  // We must remove these from FrameLayerBuilder::DisplayItemData::mFrameList
  // here, otherwise the DisplayItemData destructor will use the destroyed frame
  // when it tries to remove it from the (array) value of this property.
  aFrame->RemoveDisplayItemDataForDeletion();

  if (!mIgnoreFrameDestruction) {
    if (aFrame->HasImageRequest()) {
      mDocument->StyleImageLoader()->DropRequestsForFrame(aFrame);
    }

    mFrameConstructor->NotifyDestroyingFrame(aFrame);

    mDirtyRoots.Remove(aFrame);

    // Remove frame properties
    aFrame->RemoveAllProperties();

    const auto ComputeTargetContent =
        [&aFrame](const EventTargetInfo& aEventTargetInfo) -> nsIContent* {
      if (!IsForbiddenDispatchingToNonElementContent(
              aEventTargetInfo.mEventMessage)) {
        return aFrame->GetContent();
      }
      return aFrame->GetContent()
                 ? aFrame->GetContent()
                       ->GetInclusiveFlattenedTreeAncestorElement()
                 : nullptr;
    };

    if (aFrame == mCurrentEventTarget.mFrame) {
      mCurrentEventTarget.UpdateFrameAndContent(
          nullptr, ComputeTargetContent(mCurrentEventTarget));
    }

    for (EventTargetInfo& eventTargetInfo : mCurrentEventTargetStack) {
      if (aFrame == eventTargetInfo.mFrame) {
        // One of our stack frames was deleted.  Get its content so that when we
        // pop it we can still get its new frame from its content
        eventTargetInfo.UpdateFrameAndContent(
            nullptr, ComputeTargetContent(eventTargetInfo));
      }
    }

    mFramesToDirty.Remove(aFrame);

    if (ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(aFrame)) {
      mPendingScrollAnchorSelection.Remove(scrollContainerFrame);
      mPendingScrollAnchorAdjustment.Remove(scrollContainerFrame);
      mPendingScrollResnap.Remove(scrollContainerFrame);
    }
  }
}

already_AddRefed<nsCaret> PresShell::GetCaret() const {
  RefPtr<nsCaret> caret = mCaret;
  return caret.forget();
}

already_AddRefed<AccessibleCaretEventHub>
PresShell::GetAccessibleCaretEventHub() const {
  RefPtr<AccessibleCaretEventHub> eventHub = mAccessibleCaretEventHub;
  return eventHub.forget();
}

void PresShell::SetCaret(nsCaret* aNewCaret) {
  if (mCaret == aNewCaret) {
    return;
  }
  if (mCaret) {
    mCaret->SchedulePaint();
  }
  mCaret = aNewCaret;
  if (aNewCaret) {
    aNewCaret->SchedulePaint();
  }
}

void PresShell::RestoreCaret() { SetCaret(mOriginalCaret); }

NS_IMETHODIMP PresShell::SetCaretEnabled(bool aInEnable) {
  bool oldEnabled = mCaretEnabled;

  mCaretEnabled = aInEnable;

  if (mCaretEnabled != oldEnabled) {
    MOZ_ASSERT(mCaret);
    if (mCaret) {
      mCaret->SetVisible(mCaretEnabled);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP PresShell::SetCaretReadOnly(bool aReadOnly) {
  if (mCaret) {
    mCaret->SetCaretReadOnly(aReadOnly);
  }
  return NS_OK;
}

NS_IMETHODIMP PresShell::GetCaretEnabled(bool* aOutEnabled) {
  NS_ENSURE_ARG_POINTER(aOutEnabled);
  *aOutEnabled = mCaretEnabled;
  return NS_OK;
}

NS_IMETHODIMP PresShell::SetCaretVisibilityDuringSelection(bool aVisibility) {
  if (mCaret) {
    mCaret->SetVisibilityDuringSelection(aVisibility);
  }
  return NS_OK;
}

NS_IMETHODIMP PresShell::GetCaretVisible(bool* aOutIsVisible) {
  *aOutIsVisible = false;
  if (mCaret) {
    *aOutIsVisible = mCaret->IsVisible();
  }
  return NS_OK;
}

NS_IMETHODIMP PresShell::SetSelectionFlags(int16_t aFlags) {
  mSelectionFlags = aFlags;
  return NS_OK;
}

NS_IMETHODIMP PresShell::GetSelectionFlags(int16_t* aFlags) {
  if (!aFlags) {
    return NS_ERROR_INVALID_ARG;
  }

  *aFlags = mSelectionFlags;
  return NS_OK;
}

// implementation of nsISelectionController

NS_IMETHODIMP
PresShell::PhysicalMove(int16_t aDirection, int16_t aAmount, bool aExtend) {
  RefPtr<nsFrameSelection> frameSelection = mSelection;
  return frameSelection->PhysicalMove(aDirection, aAmount, aExtend);
}

NS_IMETHODIMP
PresShell::CharacterMove(bool aForward, bool aExtend) {
  RefPtr<nsFrameSelection> frameSelection = mSelection;
  return frameSelection->CharacterMove(aForward, aExtend);
}

NS_IMETHODIMP
PresShell::WordMove(bool aForward, bool aExtend) {
  RefPtr<nsFrameSelection> frameSelection = mSelection;
  nsresult result = frameSelection->WordMove(aForward, aExtend);
  // if we can't go down/up any more we must then move caret completely to
  // end/beginning respectively.
  if (NS_FAILED(result)) {
    result = CompleteMove(aForward, aExtend);
  }
  return result;
}

NS_IMETHODIMP
PresShell::LineMove(bool aForward, bool aExtend) {
  RefPtr<nsFrameSelection> frameSelection = mSelection;
  nsresult result = frameSelection->LineMove(aForward, aExtend);
  // if we can't go down/up any more we must then move caret completely to
  // end/beginning respectively.
  if (NS_FAILED(result)) {
    result = CompleteMove(aForward, aExtend);
  }
  return result;
}

NS_IMETHODIMP
PresShell::IntraLineMove(bool aForward, bool aExtend) {
  RefPtr<nsFrameSelection> frameSelection = mSelection;
  return frameSelection->IntraLineMove(aForward, aExtend);
}

NS_IMETHODIMP
PresShell::PageMove(bool aForward, bool aExtend) {
  nsIFrame* frame = nullptr;
  if (!aExtend) {
    frame = GetScrollContainerFrameToScroll(VerticalScrollDirection);
    // If there is no scrollable frame, get the frame to move caret instead.
  }
  if (!frame || frame->PresContext() != mPresContext) {
    frame = mSelection->GetFrameToPageSelect();
    if (!frame) {
      return NS_OK;
    }
  }
  // We may scroll parent scrollable element of current selection limiter.
  // In such case, we don't want to scroll selection into view unless
  // selection is changed.
  RefPtr<nsFrameSelection> frameSelection = mSelection;
  return frameSelection->PageMove(
      aForward, aExtend, frame, nsFrameSelection::SelectionIntoView::IfChanged);
}

NS_IMETHODIMP
PresShell::ScrollPage(bool aForward) {
  ScrollContainerFrame* scrollContainerFrame =
      GetScrollContainerFrameToScroll(VerticalScrollDirection);
  ScrollMode scrollMode = apz::GetScrollModeForOrigin(ScrollOrigin::Pages);
  if (scrollContainerFrame) {
    scrollContainerFrame->ScrollBy(nsIntPoint(0, aForward ? 1 : -1),
                                   ScrollUnit::PAGES, scrollMode, nullptr,
                                   mozilla::ScrollOrigin::NotSpecified,
                                   ScrollContainerFrame::NOT_MOMENTUM,
                                   ScrollSnapFlags::IntendedDirection |
                                       ScrollSnapFlags::IntendedEndPosition);
  }
  return NS_OK;
}

NS_IMETHODIMP
PresShell::ScrollLine(bool aForward) {
  ScrollContainerFrame* scrollContainerFrame =
      GetScrollContainerFrameToScroll(VerticalScrollDirection);
  ScrollMode scrollMode = apz::GetScrollModeForOrigin(ScrollOrigin::Lines);
  if (scrollContainerFrame) {
    nsRect scrollPort = scrollContainerFrame->GetScrollPortRect();
    nsSize lineSize = scrollContainerFrame->GetLineScrollAmount();
    int32_t lineCount = StaticPrefs::toolkit_scrollbox_verticalScrollDistance();
    if (lineCount * lineSize.height > scrollPort.Height()) {
      return ScrollPage(aForward);
    }
    scrollContainerFrame->ScrollBy(
        nsIntPoint(0, aForward ? lineCount : -lineCount), ScrollUnit::LINES,
        scrollMode, nullptr, mozilla::ScrollOrigin::NotSpecified,
        ScrollContainerFrame::NOT_MOMENTUM, ScrollSnapFlags::IntendedDirection);
  }
  return NS_OK;
}

NS_IMETHODIMP
PresShell::ScrollCharacter(bool aRight) {
  ScrollContainerFrame* scrollContainerFrame =
      GetScrollContainerFrameToScroll(HorizontalScrollDirection);
  ScrollMode scrollMode = apz::GetScrollModeForOrigin(ScrollOrigin::Lines);
  if (scrollContainerFrame) {
    int32_t h = StaticPrefs::toolkit_scrollbox_horizontalScrollDistance();
    scrollContainerFrame->ScrollBy(
        nsIntPoint(aRight ? h : -h, 0), ScrollUnit::LINES, scrollMode, nullptr,
        mozilla::ScrollOrigin::NotSpecified, ScrollContainerFrame::NOT_MOMENTUM,
        ScrollSnapFlags::IntendedDirection);
  }
  return NS_OK;
}

NS_IMETHODIMP
PresShell::CompleteScroll(bool aForward) {
  ScrollContainerFrame* scrollContainerFrame =
      GetScrollContainerFrameToScroll(VerticalScrollDirection);
  ScrollMode scrollMode = apz::GetScrollModeForOrigin(ScrollOrigin::Other);
  if (scrollContainerFrame) {
    scrollContainerFrame->ScrollBy(nsIntPoint(0, aForward ? 1 : -1),
                                   ScrollUnit::WHOLE, scrollMode, nullptr,
                                   mozilla::ScrollOrigin::NotSpecified,
                                   ScrollContainerFrame::NOT_MOMENTUM,
                                   ScrollSnapFlags::IntendedEndPosition);
  }
  return NS_OK;
}

NS_IMETHODIMP
PresShell::CompleteMove(bool aForward, bool aExtend) {
  // Beware! This may flush notifications via synchronous
  // ScrollSelectionIntoView.
  RefPtr<nsFrameSelection> frameSelection = mSelection;
  Element* const limiter = frameSelection->GetAncestorLimiter();
  nsIFrame* frame = limiter ? limiter->GetPrimaryFrame()
                            : FrameConstructor()->GetRootElementFrame();
  if (!frame) {
    return NS_ERROR_FAILURE;
  }
  nsIFrame::CaretPosition pos = frame->GetExtremeCaretPosition(!aForward);

  const nsFrameSelection::FocusMode focusMode =
      aExtend ? nsFrameSelection::FocusMode::kExtendSelection
              : nsFrameSelection::FocusMode::kCollapseToNewPoint;
  frameSelection->HandleClick(
      MOZ_KnownLive(pos.mResultContent) /* bug 1636889 */, pos.mContentOffset,
      pos.mContentOffset, focusMode,
      aForward ? CaretAssociationHint::After : CaretAssociationHint::Before);
  if (limiter) {
    // HandleClick resets ancestorLimiter, so set it again.
    frameSelection->SetAncestorLimiter(limiter);
  }

  // After ScrollSelectionIntoView(), the pending notifications might be
  // flushed and PresShell/PresContext/Frames may be dead. See bug 418470.
  return ScrollSelectionIntoView(SelectionType::eNormal,
                                 nsISelectionController::SELECTION_FOCUS_REGION,
                                 SelectionScrollMode::SyncFlush);
}

// end implementations nsISelectionController

ScrollContainerFrame* PresShell::GetRootScrollContainerFrame() const {
  if (!mFrameConstructor) {
    return nullptr;
  }
  nsIFrame* rootFrame = mFrameConstructor->GetRootFrame();
  if (!rootFrame) {
    return nullptr;
  }
  nsIFrame* theFrame = rootFrame->PrincipalChildList().FirstChild();
  if (!theFrame || !theFrame->IsScrollContainerFrame()) {
    return nullptr;
  }
  return static_cast<ScrollContainerFrame*>(theFrame);
}

nsPageSequenceFrame* PresShell::GetPageSequenceFrame() const {
  return mFrameConstructor->GetPageSequenceFrame();
}

nsCanvasFrame* PresShell::GetCanvasFrame() const {
  return mFrameConstructor->GetCanvasFrame();
}

void PresShell::RestoreRootScrollPosition() {
  if (ScrollContainerFrame* sf = GetRootScrollContainerFrame()) {
    sf->ScrollToRestoredPosition();
  }
}

void PresShell::MaybeReleaseCapturingContent() {
  RefPtr<nsFrameSelection> frameSelection = FrameSelection();
  if (frameSelection) {
    frameSelection->SetDragState(false);
  }
  if (sCapturingContentInfo.mContent &&
      sCapturingContentInfo.mContent->OwnerDoc() == mDocument) {
    PresShell::ReleaseCapturingContent();
  }
}

void PresShell::BeginLoad(Document* aDocument) {
  mDocumentLoading = true;

  gfxTextPerfMetrics* tp = nullptr;
  if (mPresContext) {
    tp = mPresContext->GetTextPerfMetrics();
  }

  bool shouldLog = MOZ_LOG_TEST(gLog, LogLevel::Debug);
  if (shouldLog || tp) {
    mLoadBegin = TimeStamp::Now();
  }

  if (shouldLog) {
    nsIURI* uri = mDocument->GetDocumentURI();
    MOZ_LOG(gLog, LogLevel::Debug,
            ("(presshell) %p load begin [%s]\n", this,
             uri ? uri->GetSpecOrDefault().get() : ""));
  }
}

void PresShell::EndLoad(Document* aDocument) {
  MOZ_ASSERT(aDocument == mDocument, "Wrong document");

  RestoreRootScrollPosition();

  mDocumentLoading = false;
}

void PresShell::LoadComplete() {
  gfxTextPerfMetrics* tp = nullptr;
  if (mPresContext) {
    tp = mPresContext->GetTextPerfMetrics();
  }

  // log load
  bool shouldLog = MOZ_LOG_TEST(gLog, LogLevel::Debug);
  if (shouldLog || tp) {
    TimeDuration loadTime = TimeStamp::Now() - mLoadBegin;
    nsIURI* uri = mDocument->GetDocumentURI();
    nsAutoCString spec;
    if (uri) {
      spec = uri->GetSpecOrDefault();
    }
    if (shouldLog) {
      MOZ_LOG(gLog, LogLevel::Debug,
              ("(presshell) %p load done time-ms: %9.2f [%s]\n", this,
               loadTime.ToMilliseconds(), spec.get()));
    }
    if (tp) {
      tp->Accumulate();
      if (tp->cumulative.numChars > 0) {
        LogTextPerfStats(tp, this, tp->cumulative, loadTime.ToMilliseconds(),
                         eLog_loaddone, spec.get());
      }
    }
  }
}

#ifdef DEBUG
void PresShell::VerifyHasDirtyRootAncestor(nsIFrame* aFrame) {
  // XXXbz due to bug 372769, can't actually assert anything here...
  // XXX Since bug 372769 is now fixed, the assertion is being enabled in bug
  //     1758104.
#  if 0
  // XXXbz shouldn't need this part; remove it once FrameNeedsReflow
  // handles the root frame correctly.
  if (!aFrame->GetParent()) {
    return;
  }

  // Make sure that there is a reflow root ancestor of |aFrame| that's
  // in mDirtyRoots already.
  while (aFrame && aFrame->HasAnyStateBits(NS_FRAME_HAS_DIRTY_CHILDREN)) {
    if ((aFrame->HasAnyStateBits(NS_FRAME_REFLOW_ROOT |
                                 NS_FRAME_DYNAMIC_REFLOW_ROOT) ||
         !aFrame->GetParent()) &&
        mDirtyRoots.Contains(aFrame)) {
      return;
    }

    aFrame = aFrame->GetParent();
  }

  MOZ_ASSERT_UNREACHABLE(
      "Frame has dirty bits set but isn't scheduled to be "
      "reflowed?");
#  endif
}
#endif

void PresShell::PostPendingScrollAnchorSelection(
    mozilla::layout::ScrollAnchorContainer* aContainer) {
  mPendingScrollAnchorSelection.Insert(aContainer->ScrollContainer());
}

void PresShell::FlushPendingScrollAnchorSelections() {
  for (ScrollContainerFrame* scroll : mPendingScrollAnchorSelection) {
    scroll->Anchor()->SelectAnchor();
  }
  mPendingScrollAnchorSelection.Clear();
}

void PresShell::PostPendingScrollAnchorAdjustment(
    ScrollAnchorContainer* aContainer) {
  mPendingScrollAnchorAdjustment.Insert(aContainer->ScrollContainer());
}

void PresShell::FlushPendingScrollAnchorAdjustments() {
  for (ScrollContainerFrame* scroll : mPendingScrollAnchorAdjustment) {
    scroll->Anchor()->ApplyAdjustments();
  }
  mPendingScrollAnchorAdjustment.Clear();
}

void PresShell::PostPendingScrollResnap(
    ScrollContainerFrame* aScrollContainerFrame) {
  mPendingScrollResnap.Insert(aScrollContainerFrame);
}

void PresShell::FlushPendingScrollResnap() {
  for (ScrollContainerFrame* scrollContainerFrame : mPendingScrollResnap) {
    scrollContainerFrame->TryResnap();
  }
  mPendingScrollResnap.Clear();
}

void PresShell::FrameNeedsReflow(nsIFrame* aFrame,
                                 IntrinsicDirty aIntrinsicDirty,
                                 nsFrameState aBitToAdd,
                                 ReflowRootHandling aRootHandling) {
  MOZ_ASSERT(aBitToAdd == NS_FRAME_IS_DIRTY ||
                 aBitToAdd == NS_FRAME_HAS_DIRTY_CHILDREN || !aBitToAdd,
             "Unexpected bits being added");

  // FIXME bug 478135
  NS_ASSERTION(
      aIntrinsicDirty != IntrinsicDirty::FrameAncestorsAndDescendants ||
          aBitToAdd != NS_FRAME_HAS_DIRTY_CHILDREN,
      "bits don't correspond to style change reason");

  // FIXME bug 457400
  NS_ASSERTION(!mIsReflowing, "can't mark frame dirty during reflow");

  // If we've not yet done the initial reflow, then don't bother
  // enqueuing a reflow command yet.
  if (!mDidInitialize) {
    return;
  }

  // If we're already destroying, don't bother with this either.
  if (mIsDestroying) {
    return;
  }

#ifdef DEBUG
  // printf("gShellCounter: %d\n", gShellCounter++);
  if (mInVerifyReflow) return;

  if (VerifyReflowFlags::NoisyCommands & gVerifyReflowFlags) {
    printf("\nPresShell@%p: frame %p needs reflow\n", (void*)this,
           (void*)aFrame);
    if (VerifyReflowFlags::ReallyNoisyCommands & gVerifyReflowFlags) {
      printf("Current content model:\n");
      Element* rootElement = mDocument->GetRootElement();
      if (rootElement) {
        rootElement->List(stdout, 0);
      }
    }
  }
#endif

  AutoTArray<nsIFrame*, 4> subtrees;
  subtrees.AppendElement(aFrame);

  do {
    nsIFrame* subtreeRoot = subtrees.PopLastElement();

    // Grab |wasDirty| now so we can go ahead and update the bits on
    // subtreeRoot.
    bool wasDirty = subtreeRoot->IsSubtreeDirty();
    subtreeRoot->AddStateBits(aBitToAdd);

    // Determine whether we need to keep looking for the next ancestor
    // reflow root if subtreeRoot itself is a reflow root.
    bool targetNeedsReflowFromParent;
    switch (aRootHandling) {
      case ReflowRootHandling::PositionOrSizeChange:
        targetNeedsReflowFromParent = true;
        break;
      case ReflowRootHandling::NoPositionOrSizeChange:
        targetNeedsReflowFromParent = false;
        break;
      case ReflowRootHandling::InferFromBitToAdd:
        targetNeedsReflowFromParent = (aBitToAdd == NS_FRAME_IS_DIRTY);
        break;
    }

    auto FrameIsReflowRoot = [](const nsIFrame* aFrame) {
      return aFrame->HasAnyStateBits(NS_FRAME_REFLOW_ROOT |
                                     NS_FRAME_DYNAMIC_REFLOW_ROOT);
    };

    auto CanStopClearingAncestorIntrinsics = [&](const nsIFrame* aFrame) {
      return FrameIsReflowRoot(aFrame) && aFrame != subtreeRoot;
    };

    auto IsReflowBoundary = [&](const nsIFrame* aFrame) {
      return FrameIsReflowRoot(aFrame) &&
             (aFrame != subtreeRoot || !targetNeedsReflowFromParent);
    };

    // Mark the intrinsic widths as dirty on the frame, all of its ancestors,
    // and all of its descendants, if needed:

    if (aIntrinsicDirty != IntrinsicDirty::None) {
      // Mark argument and all ancestors dirty. (Unless we hit a reflow root
      // that should contain the reflow.
      for (nsIFrame* a = subtreeRoot;
           a && !CanStopClearingAncestorIntrinsics(a); a = a->GetParent()) {
        a->MarkIntrinsicISizesDirty();
        if (a->IsAbsolutelyPositioned()) {
          // If we get here, 'a' is abspos, so its subtree's intrinsic sizing
          // has no effect on its ancestors' intrinsic sizing. So, don't loop
          // upwards any further.
          break;
        }
      }
    }

    const bool frameAncestorAndDescendantISizesDirty =
        (aIntrinsicDirty == IntrinsicDirty::FrameAncestorsAndDescendants);
    const bool dirty = (aBitToAdd == NS_FRAME_IS_DIRTY);
    if (frameAncestorAndDescendantISizesDirty || dirty) {
      // Mark all descendants dirty (using an nsTArray stack rather than
      // recursion).
      // Note that ReflowInput::InitResizeFlags has some similar
      // code; see comments there for how and why it differs.
      AutoTArray<nsIFrame*, 32> stack;
      stack.AppendElement(subtreeRoot);

      do {
        nsIFrame* f = stack.PopLastElement();

        if (frameAncestorAndDescendantISizesDirty && f->IsPlaceholderFrame()) {
          // Call `GetOutOfFlowFrame` directly because we can get here from
          // frame destruction and the placeholder might be already torn down.
          if (nsIFrame* oof =
                  static_cast<nsPlaceholderFrame*>(f)->GetOutOfFlowFrame()) {
            if (!nsLayoutUtils::IsProperAncestorFrame(subtreeRoot, oof)) {
              // We have another distinct subtree we need to mark.
              subtrees.AppendElement(oof);
            }
          }
        }

        for (const auto& childList : f->ChildLists()) {
          for (nsIFrame* kid : childList.mList) {
            if (frameAncestorAndDescendantISizesDirty) {
              kid->MarkIntrinsicISizesDirty();
            }
            if (dirty) {
              kid->AddStateBits(NS_FRAME_IS_DIRTY);
            }
            stack.AppendElement(kid);
          }
        }
      } while (stack.Length() != 0);
    }

    // Skip setting dirty bits up the tree if we weren't given a bit to add.
    if (!aBitToAdd) {
      continue;
    }

    // Set NS_FRAME_HAS_DIRTY_CHILDREN bits (via nsIFrame::ChildIsDirty)
    // up the tree until we reach either a frame that's already dirty or
    // a reflow root.
    nsIFrame* f = subtreeRoot;
    for (;;) {
      if (IsReflowBoundary(f) || !f->GetParent()) {
        // we've hit a reflow root or the root frame
        if (!wasDirty) {
          mDirtyRoots.Add(f);
          SetNeedLayoutFlush();
        }
#ifdef DEBUG
        else {
          VerifyHasDirtyRootAncestor(f);
        }
#endif

        break;
      }

      nsIFrame* child = f;
      f = f->GetParent();
      wasDirty = f->IsSubtreeDirty();
      f->ChildIsDirty(child);
      NS_ASSERTION(f->HasAnyStateBits(NS_FRAME_HAS_DIRTY_CHILDREN),
                   "ChildIsDirty didn't do its job");
      if (wasDirty) {
        // This frame was already marked dirty.
#ifdef DEBUG
        VerifyHasDirtyRootAncestor(f);
#endif
        break;
      }
    }
  } while (subtrees.Length() != 0);

  EnsureLayoutFlush();
}

void PresShell::FrameNeedsToContinueReflow(nsIFrame* aFrame) {
  NS_ASSERTION(mIsReflowing, "Must be in reflow when marking path dirty.");
  MOZ_ASSERT(mCurrentReflowRoot, "Must have a current reflow root here");
  NS_ASSERTION(
      aFrame == mCurrentReflowRoot ||
          nsLayoutUtils::IsProperAncestorFrame(mCurrentReflowRoot, aFrame),
      "Frame passed in is not the descendant of mCurrentReflowRoot");
  NS_ASSERTION(aFrame->HasAnyStateBits(NS_FRAME_IN_REFLOW),
               "Frame passed in not in reflow?");

  mFramesToDirty.Insert(aFrame);
}

already_AddRefed<nsIContent> PresShell::GetContentForScrolling() const {
  if (nsCOMPtr<nsIContent> focused = GetFocusedContentInOurWindow()) {
    return focused.forget();
  }
  return GetSelectedContentForScrolling();
}

already_AddRefed<nsIContent> PresShell::GetSelectedContentForScrolling() const {
  nsCOMPtr<nsIContent> selectedContent;
  if (mSelection) {
    Selection& domSelection = mSelection->NormalSelection();
    selectedContent = nsIContent::FromNodeOrNull(domSelection.GetFocusNode());
  }
  return selectedContent.forget();
}

ScrollContainerFrame* PresShell::GetScrollContainerFrameToScrollForContent(
    nsIContent* aContent, ScrollDirections aDirections) {
  ScrollContainerFrame* scrollContainerFrame = nullptr;
  if (aContent) {
    nsIFrame* startFrame = aContent->GetPrimaryFrame();
    if (startFrame) {
      scrollContainerFrame = startFrame->GetScrollTargetFrame();
      if (scrollContainerFrame) {
        startFrame = scrollContainerFrame->GetScrolledFrame();
      }
      scrollContainerFrame =
          nsLayoutUtils::GetNearestScrollableFrameForDirection(startFrame,
                                                               aDirections);
    }
  }
  if (!scrollContainerFrame) {
    scrollContainerFrame = GetRootScrollContainerFrame();
    if (!scrollContainerFrame || !scrollContainerFrame->GetScrolledFrame()) {
      return nullptr;
    }
    scrollContainerFrame = nsLayoutUtils::GetNearestScrollableFrameForDirection(
        scrollContainerFrame->GetScrolledFrame(), aDirections);
  }
  return scrollContainerFrame;
}

ScrollContainerFrame* PresShell::GetScrollContainerFrameToScroll(
    ScrollDirections aDirections) {
  nsCOMPtr<nsIContent> content = GetContentForScrolling();
  return GetScrollContainerFrameToScrollForContent(content.get(), aDirections);
}

void PresShell::CancelAllPendingReflows() { mDirtyRoots.Clear(); }

static bool DestroyFramesAndStyleDataFor(
    Element* aElement, nsPresContext& aPresContext,
    RestyleManager::IncludeRoot aIncludeRoot) {
  bool didReconstruct =
      aPresContext.FrameConstructor()->DestroyFramesFor(aElement);
  RestyleManager::ClearServoDataFromSubtree(aElement, aIncludeRoot);
  return didReconstruct;
}

void PresShell::SlotAssignmentWillChange(Element& aElement,
                                         HTMLSlotElement* aOldSlot,
                                         HTMLSlotElement* aNewSlot) {
  MOZ_ASSERT(aOldSlot != aNewSlot);

  if (MOZ_UNLIKELY(!mDidInitialize)) {
    return;
  }

  // If the old slot is about to become empty and show fallback, let layout know
  // that it needs to do work.
  if (aOldSlot && aOldSlot->AssignedNodes().Length() == 1 &&
      aOldSlot->HasChildren()) {
    DestroyFramesForAndRestyle(aOldSlot);
  }

  // Ensure the new element starts off clean.
  DestroyFramesAndStyleDataFor(&aElement, *mPresContext,
                               RestyleManager::IncludeRoot::Yes);

  if (aNewSlot) {
    // If the new slot will stop showing fallback content, we need to reframe it
    // altogether.
    if (aNewSlot->AssignedNodes().IsEmpty() && aNewSlot->HasChildren()) {
      DestroyFramesForAndRestyle(aNewSlot);
      // Otherwise we just care about the element, but we need to ensure that
      // something takes care of traversing to the relevant slot, if needed.
    } else if (aNewSlot->HasServoData() &&
               !Servo_Element_IsDisplayNone(aNewSlot)) {
      // Set the reframe bits...
      aNewSlot->NoteDescendantsNeedFramesForServo();
      aElement.SetFlags(NODE_NEEDS_FRAME);
      // Now the style dirty bits. Note that we can't just do
      // aElement.NoteDirtyForServo(), because the new slot is not setup yet.
      aNewSlot->SetHasDirtyDescendantsForServo();
      aNewSlot->NoteDirtySubtreeForServo();
    }
  }
}

#ifdef DEBUG
static void AssertNoFramesOrStyleDataInDescendants(Element& aElement) {
  for (nsINode* node : ShadowIncludingTreeIterator(aElement)) {
    nsIContent* c = nsIContent::FromNode(node);
    if (c == &aElement) {
      continue;
    }
    // FIXME(emilio): The <area> check is needed because of bug 135040.
    MOZ_ASSERT(!c->GetPrimaryFrame() || c->IsHTMLElement(nsGkAtoms::area));
    MOZ_ASSERT(!c->IsElement() || !c->AsElement()->HasServoData());
  }
}
#endif

void PresShell::DestroyFramesForAndRestyle(Element* aElement) {
#ifdef DEBUG
  auto postCondition = MakeScopeExit([&]() {
    MOZ_ASSERT(!aElement->GetPrimaryFrame());
    AssertNoFramesOrStyleDataInDescendants(*aElement);
  });
#endif

  MOZ_ASSERT(aElement);
  if (!aElement->HasServoData()) {
    // Nothing to do here, the element already is out of the flat tree or is not
    // styled.
    return;
  }

  // Mark ourselves as not safe to flush while we're doing frame destruction.
  nsAutoScriptBlocker scriptBlocker;
  ++mChangeNestCount;

  const bool didReconstruct = FrameConstructor()->DestroyFramesFor(aElement);
  // Clear the style data from all the flattened tree descendants, but _not_
  // from us, since otherwise we wouldn't see the reframe.
  RestyleManager::ClearServoDataFromSubtree(aElement,
                                            RestyleManager::IncludeRoot::No);
  auto changeHint =
      didReconstruct ? nsChangeHint(0) : nsChangeHint_ReconstructFrame;
  mPresContext->RestyleManager()->PostRestyleEvent(
      aElement, RestyleHint::RestyleSubtree(), changeHint);

  --mChangeNestCount;
}

void PresShell::ShadowRootWillBeAttached(Element& aElement) {
#ifdef DEBUG
  auto postCondition = MakeScopeExit(
      [&]() { AssertNoFramesOrStyleDataInDescendants(aElement); });
#endif

  if (!aElement.HasServoData()) {
    // Nothing to do here, the element already is out of the flat tree or is not
    // styled.
    return;
  }

  if (!aElement.HasChildren()) {
    // The element has no children, just avoid the work.
    return;
  }

  // Mark ourselves as not safe to flush while we're doing frame destruction.
  nsAutoScriptBlocker scriptBlocker;
  ++mChangeNestCount;

  // NOTE(emilio): We use FlattenedChildIterator intentionally here (rather than
  // StyleChildrenIterator), since we don't want to remove ::before / ::after
  // content.
  FlattenedChildIterator iter(&aElement);
  nsCSSFrameConstructor* fc = FrameConstructor();
  for (nsIContent* c = iter.GetNextChild(); c; c = iter.GetNextChild()) {
    fc->DestroyFramesFor(c);
    if (c->IsElement()) {
      RestyleManager::ClearServoDataFromSubtree(c->AsElement());
    }
  }

#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->ScheduleAccessibilitySubtreeUpdate(this, &aElement);
  }
#endif

  --mChangeNestCount;
}

void PresShell::PostRecreateFramesFor(Element* aElement) {
  if (MOZ_UNLIKELY(!mDidInitialize)) {
    // Nothing to do here. In fact, if we proceed and aElement is the root, we
    // will crash.
    return;
  }

  mPresContext->RestyleManager()->PostRestyleEvent(
      aElement, RestyleHint{0}, nsChangeHint_ReconstructFrame);
}

void PresShell::RestyleForAnimation(Element* aElement, RestyleHint aHint) {
  // Now that we no longer have separate non-animation and animation
  // restyles, this method having a distinct identity is less important,
  // but it still seems useful to offer as a "more public" API and as a
  // checkpoint for these restyles to go through.
  mPresContext->RestyleManager()->PostRestyleEvent(aElement, aHint,
                                                   nsChangeHint(0));
}

void PresShell::SetForwardingContainer(const WeakPtr<nsDocShell>& aContainer) {
  mForwardingContainer = aContainer;
}

void PresShell::ClearFrameRefs(nsIFrame* aFrame) {
  mPresContext->EventStateManager()->ClearFrameRefs(aFrame);

  AutoWeakFrame* weakFrame = mAutoWeakFrames;
  while (weakFrame) {
    AutoWeakFrame* prev = weakFrame->GetPreviousWeakFrame();
    if (weakFrame->GetFrame() == aFrame) {
      // This removes weakFrame from mAutoWeakFrames.
      weakFrame->Clear(this);
    }
    weakFrame = prev;
  }

  AutoTArray<WeakFrame*, 4> toRemove;
  for (WeakFrame* weakFrame : mWeakFrames) {
    if (weakFrame->GetFrame() == aFrame) {
      toRemove.AppendElement(weakFrame);
    }
  }
  for (WeakFrame* weakFrame : toRemove) {
    weakFrame->Clear(this);
  }
}

UniquePtr<gfxContext> PresShell::CreateReferenceRenderingContext() {
  if (mPresContext->IsScreen()) {
    return gfxContext::CreateOrNull(
        gfxPlatform::GetPlatform()->ScreenReferenceDrawTarget().get());
  }

  // We assume the devCtx has positive width and height for this call.
  // However, width and height, may be outside of the reasonable range
  // so rc may still be null.
  nsDeviceContext* devCtx = mPresContext->DeviceContext();
  return devCtx->CreateReferenceRenderingContext();
}

// https://html.spec.whatwg.org/#scroll-to-the-fragment-identifier
nsresult PresShell::GoToAnchor(const nsAString& aAnchorName,
                               const nsRange* aFirstTextDirective, bool aScroll,
                               ScrollFlags aAdditionalScrollFlags) {
  if (!mDocument) {
    return NS_ERROR_FAILURE;
  }

  const Element* root = mDocument->GetRootElement();
  if (root && root->IsSVGElement(nsGkAtoms::svg)) {
    // We need to execute this even if there is an empty anchor name
    // so that any existing SVG fragment identifier effect is removed
    if (SVGFragmentIdentifier::ProcessFragmentIdentifier(mDocument,
                                                         aAnchorName)) {
      return NS_OK;
    }
  }

  // Hold a reference to the ESM in case event dispatch tears us down.
  RefPtr<EventStateManager> esm = mPresContext->EventStateManager();

  // https://wicg.github.io/scroll-to-text-fragment/#invoking-text-directives
  // From "Monkeypatching HTML § 7.4.6.3 Scrolling to a fragment:"
  // 3.4. If target is a range, then:
  // 3.4.1 Set target to be the first common ancestor of target's start node and
  //       end node.
  // 3.4.2 While target is non-null and is not an element, set target to
  //       target's parent.
  // ------
  // Common closest ancestor is not suitable here, as it can scroll to positions
  // where no text directive is visible. Instead, scroll to the start container
  // of the text directive.
  // see https://bugzil.la/1906895 and
  // https://github.com/WICG/scroll-to-text-fragment/issues/259
  Element* textFragmentTargetElement = [&aFirstTextDirective]() -> Element* {
    nsINode* node = aFirstTextDirective
                        ? aFirstTextDirective->GetStartContainer()
                        : nullptr;
    while (node && !node->IsElement()) {
      node = node->GetParent();
    }
    return Element::FromNodeOrNull(node);
  }();
  const bool thereIsATextFragment = !!textFragmentTargetElement;

  // 1. If there is no indicated part of the document, set the Document's target
  //    element to null.
  //
  // FIXME(emilio): Per spec empty fragment string should take the same
  // code-path as "top"!
  if (aAnchorName.IsEmpty() && !thereIsATextFragment) {
    NS_ASSERTION(!aScroll, "can't scroll to empty anchor name");
    esm->SetContentState(nullptr, ElementState::URLTARGET);
    return NS_OK;
  }

  // 2. If the indicated part of the document is the top of the document,
  // then:
  // (handled below when `target` is null, and anchor is `top`)

  // 3.1. Let target be element that is the indicated part of the document.
  //
  // https://html.spec.whatwg.org/#target-element
  // https://html.spec.whatwg.org/#find-a-potential-indicated-element
  RefPtr<Element> target = textFragmentTargetElement;
  if (!target) {
    target = nsContentUtils::GetTargetElement(mDocument, aAnchorName);
  }

  // 1. If there is no indicated part of the document, set the Document's
  //    target element to null.
  // 2.1. Set the Document's target element to null.
  // 3.2. Set the Document's target element to target.
  esm->SetContentState(target, ElementState::URLTARGET);

  // TODO: Spec probably needs a section to account for this.
  if (ScrollContainerFrame* rootScroll = GetRootScrollContainerFrame()) {
    if (rootScroll->DidHistoryRestore()) {
      // Scroll position restored from history trumps scrolling to anchor.
      aScroll = false;
      rootScroll->ClearDidHistoryRestore();
    }
  }

  if (target) {
    // 3.4 Run the ancestor details revealing algorithm on target.
    target->RevealAncestorClosedDetails();
    // 3.5 Run the ancestor hidden-until-found revealing algorithm on target.
    // https://html.spec.whatwg.org/#ancestor-hidden-until-found-revealing-algorithm
    ErrorResult rv;
    target->RevealAncestorHiddenUntilFoundAndFireBeforematchEvent(rv);
    if (MOZ_UNLIKELY(rv.Failed())) {
      return rv.StealNSResult();
    }

    if (aScroll) {
      // https://wicg.github.io/scroll-to-text-fragment/#invoking-text-directives
      // From "Monkeypatching HTML § 7.4.6.3 Scrolling to a fragment:"
      // 3.9 Let blockPosition be "center" if scrollTarget is a range, "start"
      //     otherwise.
      // Implementation note: Use `ScrollSelectionIntoView` for text fragment,
      // since the text fragment is stored as a `eTargetText` selection.
      //
      // 3.4. Scroll target into view, with behavior set to "auto", block set to
      //      "start", and inline set to "nearest".
      // FIXME(emilio): Not all callers pass ScrollSmoothAuto (but we use auto
      // smooth scroll for `top` regardless below, so maybe they should!).
      ScrollingInteractionContext scrollToAnchorContext(true);
      if (thereIsATextFragment) {
        MOZ_TRY(ScrollSelectionIntoView(
            SelectionType::eTargetText,
            nsISelectionController::SELECTION_ANCHOR_REGION,
            ScrollAxis(WhereToScroll::Center, WhenToScroll::Always),
            ScrollAxis(),
            ScrollFlags::AnchorScrollFlags | aAdditionalScrollFlags,
            SelectionScrollMode::SyncFlush));
      } else {
        MOZ_TRY(ScrollContentIntoView(
            target, ScrollAxis(WhereToScroll::Start, WhenToScroll::Always),
            ScrollAxis(),
            ScrollFlags::AnchorScrollFlags | aAdditionalScrollFlags));
      }
      if (ScrollContainerFrame* rootScroll = GetRootScrollContainerFrame()) {
        mLastAnchorScrolledTo = target;
        mLastAnchorScrollPositionY = rootScroll->GetScrollPosition().y;
        mLastAnchorScrollType = thereIsATextFragment
                                    ? AnchorScrollType::TextDirective
                                    : AnchorScrollType::Anchor;
      }
    }

    {
      // 3.6. Move the sequential focus navigation starting point to target.
      //
      // Move the caret to the anchor. That way tabbing will start from the new
      // location.
      //
      // TODO(emilio): Do we want to do this even if aScroll is false?
      //
      // NOTE: Intentionally out of order for now with the focus steps, see
      // https://github.com/whatwg/html/issues/7759
      RefPtr<nsRange> jumpToRange = nsRange::Create(mDocument);
      nsCOMPtr<nsIContent> nodeToSelect = target.get();
      while (nodeToSelect->GetFirstChild()) {
        nodeToSelect = nodeToSelect->GetFirstChild();
      }
      jumpToRange->SelectNodeContents(*nodeToSelect, IgnoreErrors());
      RefPtr sel = &mSelection->NormalSelection();
      MOZ_ASSERT(sel);
      sel->RemoveAllRanges(IgnoreErrors());
      sel->AddRangeAndSelectFramesAndNotifyListeners(*jumpToRange,
                                                     IgnoreErrors());
      if (!StaticPrefs::layout_selectanchor()) {
        // Use a caret (collapsed selection) at the start of the anchor.
        sel->CollapseToStart(IgnoreErrors());
      }
    }

    // 3.5. Run the focusing steps for target, with the Document's viewport as
    // the fallback target.
    //
    // Note that ScrollContentIntoView flushes, so we don't need to do that
    // again here. We also don't need to scroll again either.
    //
    // We intentionally focus the target only when aScroll is true, we need to
    // sort out if the spec needs to differentiate these cases. When aScroll is
    // false we still clear the focus unconditionally, that's legacy behavior,
    // maybe we shouldn't do it.
    //
    // TODO(emilio): Do we really want to clear the focus even if aScroll is
    // false?
    const bool shouldFocusTarget = [&] {
      if (!aScroll || thereIsATextFragment) {
        return false;
      }
      nsIFrame* targetFrame = target->GetPrimaryFrame();
      return targetFrame && targetFrame->IsFocusable();
    }();

    if (shouldFocusTarget) {
      FocusOptions options;
      options.mPreventScroll = true;
      target->Focus(options, CallerType::NonSystem, IgnoreErrors());
    } else if (RefPtr<nsIFocusManager> fm = nsFocusManager::GetFocusManager()) {
      if (nsPIDOMWindowOuter* win = mDocument->GetWindow()) {
        // Now focus the document itself if focus is on an element within it.
        nsCOMPtr<mozIDOMWindowProxy> focusedWindow;
        fm->GetFocusedWindow(getter_AddRefs(focusedWindow));
        if (SameCOMIdentity(win, focusedWindow)) {
          fm->ClearFocus(focusedWindow);
        }
      }
    }

    // If the target is an animation element, activate the animation
    if (auto* animationElement = SVGAnimationElement::FromNode(target.get())) {
      animationElement->ActivateByHyperlink();
    }

#ifdef ACCESSIBILITY
    if (nsAccessibilityService* accService = GetAccService()) {
      nsIContent* a11yTarget = target;
      if (thereIsATextFragment) {
        // A text fragment starts in a text leaf node. `target` is the element
        // parent, but there may be many other children of that element before
        // the start of the text fragment. Explicitly use the start leaf node
        // here to get a11y clients as close as possible to the fragment (on
        // platforms which support this).
        a11yTarget = nsIContent::FromNodeOrNull(
            aFirstTextDirective->GetStartContainer());
        if (!a11yTarget) {
          a11yTarget = target;
        }
      }
      accService->NotifyOfAnchorJumpTo(a11yTarget);
    }
#endif
  } else if (nsContentUtils::EqualsIgnoreASCIICase(aAnchorName, u"top"_ns)) {
    // 2.2. Scroll to the beginning of the document for the Document.
    ScrollContainerFrame* sf = GetRootScrollContainerFrame();
    // Check |aScroll| after setting |rv| so we set |rv| to the same
    // thing whether or not |aScroll| is true.
    if (aScroll && sf) {
      ScrollMode scrollMode =
          sf->IsSmoothScroll() ? ScrollMode::SmoothMsd : ScrollMode::Instant;
      // Scroll to the top of the page
      sf->ScrollTo(nsPoint(0, 0), scrollMode);
    }
  } else {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult PresShell::ScrollToAnchor() {
  NS_ASSERTION(mDidInitialize, "should have done initial reflow by now");
  if (mLastAnchorScrollType == AnchorScrollType::Anchor) {
    nsCOMPtr<nsIContent> lastAnchor = std::move(mLastAnchorScrolledTo);
    if (!lastAnchor) {
      return NS_OK;
    }

    ScrollContainerFrame* rootScroll = GetRootScrollContainerFrame();
    if (!rootScroll ||
        mLastAnchorScrollPositionY != rootScroll->GetScrollPosition().y) {
      return NS_OK;
    }
    return ScrollContentIntoView(
        lastAnchor, ScrollAxis(WhereToScroll::Start, WhenToScroll::Always),
        ScrollAxis(), ScrollFlags::AnchorScrollFlags);
  }

  return ScrollSelectionIntoView(
      SelectionType::eTargetText,
      nsISelectionController::SELECTION_ANCHOR_REGION,
      ScrollAxis(WhereToScroll::Center, WhenToScroll::Always), ScrollAxis(),
      ScrollFlags::AnchorScrollFlags, SelectionScrollMode::SyncFlush);
}

/*
 * Helper (per-continuation) for ScrollContentIntoView.
 *
 * @param aContainerFrame [in] the frame which aRect is relative to
 * @param aFrame [in] Frame whose bounds should be unioned
 * @param aUseWholeLineHeightForInlines [in] if true, then for inline frames
 * we should include the top of the line in the added rectangle
 * @param aRect [inout] rect into which its bounds should be unioned
 * @param aHaveRect [inout] whether aRect contains data yet
 * @param aPrevBlock [inout] the block aLines is a line iterator for
 * @param aLines [inout] the line iterator we're using
 * @param aCurLine [inout] the line to start looking from in this iterator
 */
static void AccumulateFrameBounds(nsIFrame* aContainerFrame, nsIFrame* aFrame,
                                  bool aUseWholeLineHeightForInlines,
                                  nsRect& aRect, bool& aHaveRect,
                                  nsIFrame*& aPrevBlock,
                                  nsILineIterator*& aLines, int32_t& aCurLine) {
  nsIFrame* frame = aFrame;
  nsRect frameBounds = nsRect(nsPoint(0, 0), aFrame->GetSize());

  // If this is an inline frame and either the bounds height is 0 (quirks
  // layout model) or aUseWholeLineHeightForInlines is set, we need to
  // change the top of the bounds to include the whole line.
  if (frameBounds.height == 0 || aUseWholeLineHeightForInlines) {
    nsIFrame* prevFrame = aFrame;
    nsIFrame* f = aFrame;

    while (f && f->IsLineParticipant() && !f->IsTransformed() &&
           !f->IsAbsPosContainingBlock()) {
      prevFrame = f;
      f = prevFrame->GetParent();
    }

    if (f != aFrame && f && f->IsBlockFrame()) {
      // find the line containing aFrame and increase the top of |offset|.
      if (f != aPrevBlock) {
        aLines = f->GetLineIterator();
        aPrevBlock = f;
        aCurLine = 0;
      }
      if (aLines) {
        int32_t index = aLines->FindLineContaining(prevFrame, aCurLine);
        if (index >= 0) {
          auto line = aLines->GetLine(index).unwrap();
          frameBounds += frame->GetOffsetTo(f);
          frame = f;
          if (line.mLineBounds.y < frameBounds.y) {
            frameBounds.height = frameBounds.YMost() - line.mLineBounds.y;
            frameBounds.y = line.mLineBounds.y;
          }
        }
      }
    }
  }

  nsRect transformedBounds = nsLayoutUtils::TransformFrameRectToAncestor(
      frame, frameBounds, aContainerFrame);

  if (aHaveRect) {
    // We can't use nsRect::UnionRect since it drops empty rects on
    // the floor, and we need to include them.  (Thus we need
    // aHaveRect to know when to drop the initial value on the floor.)
    aRect = aRect.UnionEdges(transformedBounds);
  } else {
    aHaveRect = true;
    aRect = transformedBounds;
  }
}

static bool ComputeNeedToScroll(WhenToScroll aWhenToScroll, nscoord aLineSize,
                                nscoord aRectMin, nscoord aRectMax,
                                nscoord aViewMin, nscoord aViewMax) {
  // See how the rect should be positioned in a given axis.
  switch (aWhenToScroll) {
    case WhenToScroll::Always:
      // The caller wants the frame as visible as possible
      return true;
    case WhenToScroll::IfNotVisible:
      if (aLineSize > (aRectMax - aRectMin)) {
        // If the line size is greater than the size of the rect
        // to scroll into view, do not use the line size to determine
        // if we need to scroll.
        aLineSize = 0;
      }

      // Scroll only if no part of the frame is visible in this view.
      return aRectMax - aLineSize <= aViewMin ||
             aRectMin + aLineSize >= aViewMax;
    case WhenToScroll::IfNotFullyVisible:
      // Scroll only if part of the frame is hidden and more can fit in view
      return !(aRectMin >= aViewMin && aRectMax <= aViewMax) &&
             std::min(aViewMax, aRectMax) - std::max(aRectMin, aViewMin) <
                 aViewMax - aViewMin;
  }
  return false;
}

static nscoord ComputeWhereToScroll(WhereToScroll aWhereToScroll,
                                    nscoord aOriginalCoord, nscoord aRectMin,
                                    nscoord aRectMax, nscoord aViewMin,
                                    nscoord aViewMax, nscoord* aRangeMin,
                                    nscoord* aRangeMax) {
  nscoord resultCoord = aOriginalCoord;
  nscoord scrollPortLength = aViewMax - aViewMin;
  if (!aWhereToScroll.mPercentage) {
    // Scroll the minimum amount necessary to show as much as possible of the
    // frame. If the frame is too large, don't hide any initially visible part
    // of it.
    nscoord min = std::min(aRectMin, aRectMax - scrollPortLength);
    nscoord max = std::max(aRectMin, aRectMax - scrollPortLength);
    resultCoord = std::clamp(aOriginalCoord, min, max);
  } else {
    float percent = aWhereToScroll.mPercentage.value() / 100.0f;
    nscoord frameAlignCoord =
        NSToCoordRound(aRectMin + (aRectMax - aRectMin) * percent);
    resultCoord = NSToCoordRound(frameAlignCoord - scrollPortLength * percent);
  }
  // Force the scroll range to extend to include resultCoord.
  *aRangeMin = std::min(resultCoord, aRectMax - scrollPortLength);
  *aRangeMax = std::max(resultCoord, aRectMin);
  return resultCoord;
}

static WhereToScroll GetApplicableWhereToScroll(
    const ScrollContainerFrame* aScrollContainerFrame,
    const nsIFrame* aScrollableFrame, const nsIFrame* aTarget,
    ScrollDirection aScrollDirection, WhereToScroll aOriginal) {
  MOZ_ASSERT(do_QueryFrame(aScrollContainerFrame) == aScrollableFrame);
  if (aTarget == aScrollableFrame) {
    return aOriginal;
  }

  StyleScrollSnapAlignKeyword align =
      aScrollDirection == ScrollDirection::eHorizontal
          ? aScrollContainerFrame->GetScrollSnapAlignFor(aTarget).first
          : aScrollContainerFrame->GetScrollSnapAlignFor(aTarget).second;

  switch (align) {
    case StyleScrollSnapAlignKeyword::None:
      return aOriginal;
    case StyleScrollSnapAlignKeyword::Start:
      return WhereToScroll::Start;
    case StyleScrollSnapAlignKeyword::Center:
      return WhereToScroll::Center;
    case StyleScrollSnapAlignKeyword::End:
      return WhereToScroll::End;
  }
  return aOriginal;
}

static ScrollMode GetScrollModeForScrollIntoView(
    const ScrollContainerFrame* aScrollContainerFrame,
    ScrollFlags aScrollFlags) {
  ScrollMode scrollMode = ScrollMode::Instant;
  // Default to an instant scroll, but if the scroll behavior given is "auto"
  // or "smooth", use that as the specified behavior. If the user has disabled
  // smooth scrolls, a given mode of "auto" or "smooth" should not result in
  // a smooth scroll.
  ScrollBehavior behavior = ScrollBehavior::Instant;
  if (aScrollFlags & ScrollFlags::ScrollSmooth) {
    behavior = ScrollBehavior::Smooth;
  } else if (aScrollFlags & ScrollFlags::ScrollSmoothAuto) {
    behavior = ScrollBehavior::Auto;
  }
  if (aScrollContainerFrame->IsSmoothScroll(behavior)) {
    scrollMode = ScrollMode::SmoothMsd;
  }
  return scrollMode;
}

/**
 * This function takes a scroll container frame, a rect in the coordinate system
 * of the scrolled frame, and a desired percentage-based scroll
 * position and attempts to scroll the rect to that position in the
 * visual viewport.
 *
 * This needs to work even if aRect has a width or height of zero.
 */
static Maybe<nsPoint> ScrollToShowRect(
    ScrollContainerFrame* aScrollContainerFrame,
    const nsIFrame* aScrollableFrame, const nsIFrame* aTarget,
    const nsRect& aRect, const Sides aScrollPaddingSkipSides,
    const nsMargin& aMargin, ScrollAxis aVertical, ScrollAxis aHorizontal,
    ScrollFlags aScrollFlags) {
  nsPoint scrollPt = aScrollContainerFrame->GetVisualViewportOffset();
  const nsPoint originalScrollPt = scrollPt;
  const nsRect visibleRect(scrollPt,
                           aScrollContainerFrame->GetVisualViewportSize());

  const nsMargin padding = [&] {
    nsMargin p = aScrollContainerFrame->GetScrollPadding();
    p.ApplySkipSides(aScrollPaddingSkipSides);
    return p + aMargin;
  }();

  const nsRect rectToScrollIntoView = [&] {
    nsRect r(aRect);
    r.Inflate(padding);
    return r.Intersect(aScrollContainerFrame->GetScrolledRect());
  }();

  nsSize lineSize;
  // Don't call GetLineScrollAmount unless we actually need it. Not only
  // does this save time, but it's not safe to call GetLineScrollAmount
  // during reflow (because it depends on font size inflation and doesn't
  // use the in-reflow-safe font-size inflation path). If we did call it,
  // it would assert and possible give the wrong result.
  if (aVertical.mWhenToScroll == WhenToScroll::IfNotVisible ||
      aHorizontal.mWhenToScroll == WhenToScroll::IfNotVisible) {
    lineSize = aScrollContainerFrame->GetLineScrollAmount();
  }
  ScrollStyles ss = aScrollContainerFrame->GetScrollStyles();
  nsRect allowedRange(scrollPt, nsSize(0, 0));
  ScrollDirections directions =
      aScrollContainerFrame->GetAvailableScrollingDirections();

  if (((aScrollFlags & ScrollFlags::ScrollOverflowHidden) ||
       ss.mVertical != StyleOverflow::Hidden) &&
      (!aVertical.mOnlyIfPerceivedScrollableDirection ||
       (directions.contains(ScrollDirection::eVertical)))) {
    if (ComputeNeedToScroll(aVertical.mWhenToScroll, lineSize.height, aRect.y,
                            aRect.YMost(), visibleRect.y + padding.top,
                            visibleRect.YMost() - padding.bottom)) {
      // If the scroll-snap-align on the frame is valid, we need to respect it.
      WhereToScroll whereToScroll = GetApplicableWhereToScroll(
          aScrollContainerFrame, aScrollableFrame, aTarget,
          ScrollDirection::eVertical, aVertical.mWhereToScroll);

      nscoord maxHeight;
      scrollPt.y = ComputeWhereToScroll(
          whereToScroll, scrollPt.y, rectToScrollIntoView.y,
          rectToScrollIntoView.YMost(), visibleRect.y, visibleRect.YMost(),
          &allowedRange.y, &maxHeight);
      allowedRange.height = maxHeight - allowedRange.y;
    }
  }

  if (((aScrollFlags & ScrollFlags::ScrollOverflowHidden) ||
       ss.mHorizontal != StyleOverflow::Hidden) &&
      (!aHorizontal.mOnlyIfPerceivedScrollableDirection ||
       (directions.contains(ScrollDirection::eHorizontal)))) {
    if (ComputeNeedToScroll(aHorizontal.mWhenToScroll, lineSize.width, aRect.x,
                            aRect.XMost(), visibleRect.x + padding.left,
                            visibleRect.XMost() - padding.right)) {
      // If the scroll-snap-align on the frame is valid, we need to respect it.
      WhereToScroll whereToScroll = GetApplicableWhereToScroll(
          aScrollContainerFrame, aScrollableFrame, aTarget,
          ScrollDirection::eHorizontal, aHorizontal.mWhereToScroll);

      nscoord maxWidth;
      scrollPt.x = ComputeWhereToScroll(
          whereToScroll, scrollPt.x, rectToScrollIntoView.x,
          rectToScrollIntoView.XMost(), visibleRect.x, visibleRect.XMost(),
          &allowedRange.x, &maxWidth);
      allowedRange.width = maxWidth - allowedRange.x;
    }
  }

  // If we don't need to scroll, then don't try since it might cancel
  // a current smooth scroll operation.
  if (scrollPt == originalScrollPt) {
    return Nothing();
  }

  ScrollMode scrollMode =
      GetScrollModeForScrollIntoView(aScrollContainerFrame, aScrollFlags);
  nsIFrame* frame = do_QueryFrame(aScrollContainerFrame);
  AutoWeakFrame weakFrame(frame);
  aScrollContainerFrame->ScrollTo(scrollPt, scrollMode, &allowedRange,
                                  ScrollSnapFlags::IntendedEndPosition,
                                  aScrollFlags & ScrollFlags::TriggeredByScript
                                      ? ScrollTriggeredByScript::Yes
                                      : ScrollTriggeredByScript::No);
  return Some(scrollPt);
}

nsresult PresShell::ScrollContentIntoView(nsIContent* aContent,
                                          ScrollAxis aVertical,
                                          ScrollAxis aHorizontal,
                                          ScrollFlags aScrollFlags) {
  NS_ENSURE_TRUE(aContent, NS_ERROR_NULL_POINTER);
  RefPtr<Document> composedDoc = aContent->GetComposedDoc();
  NS_ENSURE_STATE(composedDoc);

  NS_ASSERTION(mDidInitialize, "should have done initial reflow by now");

  if (mContentToScrollTo) {
    mContentToScrollTo->RemoveProperty(nsGkAtoms::scrolling);
  }
  mContentToScrollTo = aContent;
  ScrollIntoViewData* data = new ScrollIntoViewData();
  data->mContentScrollVAxis = aVertical;
  data->mContentScrollHAxis = aHorizontal;
  data->mContentToScrollToFlags = aScrollFlags;
  if (NS_FAILED(mContentToScrollTo->SetProperty(
          nsGkAtoms::scrolling, data,
          nsINode::DeleteProperty<PresShell::ScrollIntoViewData>))) {
    mContentToScrollTo = nullptr;
  }

  // If the target frame has an ancestor of a `content-visibility: auto`
  // element ensure that it is laid out, so that the boundary rectangle is
  // correct.
  // Additionally, ensure that all ancestor elements with 'content-visibility:
  // auto' are set to 'visible'. so that they are laid out as visible before
  // scrolling, improving the accuracy of the scroll position, especially when
  // the scroll target is within the overflow area. And here invoking
  // 'SetTemporarilyVisibleForScrolledIntoViewDescendant' would make the
  // intersection observer knows that it should generate entries for these
  // c-v:auto ancestors, so that the content relevancy could be checked again
  // after scrolling. https://drafts.csswg.org/css-contain-2/#cv-notes
  bool reflowedForHiddenContent = false;
  if (mContentToScrollTo) {
    if (nsIFrame* frame = mContentToScrollTo->GetPrimaryFrame()) {
      bool hasContentVisibilityAutoAncestor = false;
      auto* ancestor = frame->GetClosestContentVisibilityAncestor(
          nsIFrame::IncludeContentVisibility::Auto);
      while (ancestor) {
        if (auto* element = Element::FromNodeOrNull(ancestor->GetContent())) {
          hasContentVisibilityAutoAncestor = true;
          element->SetTemporarilyVisibleForScrolledIntoViewDescendant(true);
          element->SetVisibleForContentVisibility(true);
        }
        ancestor = ancestor->GetClosestContentVisibilityAncestor(
            nsIFrame::IncludeContentVisibility::Auto);
      }
      if (hasContentVisibilityAutoAncestor) {
        UpdateHiddenContentInForcedLayout(frame);
        // TODO: There might be the other already scheduled relevancy updates,
        // other than caused be scrollIntoView.
        UpdateContentRelevancyImmediately(ContentRelevancyReason::Visible);
        reflowedForHiddenContent = ReflowForHiddenContentIfNeeded();
      }
    }
  }

  if (!reflowedForHiddenContent) {
    // Flush layout and attempt to scroll in the process.
    if (PresShell* presShell = composedDoc->GetPresShell()) {
      presShell->SetNeedLayoutFlush();
    }
    composedDoc->FlushPendingNotifications(FlushType::InterruptibleLayout);
  }

  // If mContentToScrollTo is non-null, that means we interrupted the reflow
  // (or suppressed it altogether because we're suppressing interruptible
  // flushes right now) and won't necessarily get the position correct, but do
  // a best-effort scroll here.  The other option would be to do this inside
  // FlushPendingNotifications, but I'm not sure the repeated scrolling that
  // could trigger if reflows keep getting interrupted would be more desirable
  // than a single best-effort scroll followed by one final scroll on the first
  // completed reflow.
  if (mContentToScrollTo) {
    DoScrollContentIntoView();
  }
  return NS_OK;
}

static nsMargin GetScrollMargin(const nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);
  // If we're focusing something that can't be targeted by content, allow
  // content to customize the margin.
  //
  // TODO: This is also a bit of an issue for delegated focus, see
  // https://github.com/whatwg/html/issues/7033.
  if (aFrame->GetContent() && aFrame->GetContent()->ChromeOnlyAccess()) {
    // XXX Should we use nsIContent::FindFirstNonChromeOnlyAccessContent()
    // instead of nsINode::GetClosestNativeAnonymousSubtreeRootParentOrHost()?
    if (const nsIContent* userContent =
            aFrame->GetContent()
                ->GetClosestNativeAnonymousSubtreeRootParentOrHost()) {
      if (const nsIFrame* frame = userContent->GetPrimaryFrame()) {
        return frame->StyleMargin()->GetScrollMargin();
      }
    }
  }
  return aFrame->StyleMargin()->GetScrollMargin();
}

void PresShell::DoScrollContentIntoView() {
  NS_ASSERTION(mDidInitialize, "should have done initial reflow by now");

  nsIFrame* frame = mContentToScrollTo->GetPrimaryFrame();

  if (!frame || frame->IsHiddenByContentVisibilityOnAnyAncestor(
                    nsIFrame::IncludeContentVisibility::Hidden)) {
    mContentToScrollTo->RemoveProperty(nsGkAtoms::scrolling);
    mContentToScrollTo = nullptr;
    return;
  }

  if (frame->HasAnyStateBits(NS_FRAME_FIRST_REFLOW)) {
    // The reflow flush before this scroll got interrupted, and this frame's
    // coords and size are all zero, and it has no content showing anyway.
    // Don't bother scrolling to it.  We'll try again when we finish up layout.
    return;
  }

  auto* data = static_cast<ScrollIntoViewData*>(
      mContentToScrollTo->GetProperty(nsGkAtoms::scrolling));
  if (MOZ_UNLIKELY(!data)) {
    mContentToScrollTo = nullptr;
    return;
  }

  ScrollFrameIntoView(frame, Nothing(), data->mContentScrollVAxis,
                      data->mContentScrollHAxis, data->mContentToScrollToFlags);
}

static bool NeedToVisuallyScroll(const nsSize& aLayoutViewportSize,
                                 const nsRect& aPositionFixedRect) {
  // position:fixed elements are fixed to the layout viewport, thus the
  // coordinate system is (0, 0) origin.
  // (and the maximum visible position is the layout viewport size, elements
  // outside of the size will never be laid out)
  const nsRect layoutViewport = nsRect(nsPoint(), aLayoutViewportSize);

  // `BaseRect::Intersects(const Sub& aRect)` does return false if `aRect` is
  // empty, but we do want to visually scroll to empty position:fixed elements
  // if the elements are inside the layout viewport.
  if (aPositionFixedRect.IsEmpty()) {
    if (aPositionFixedRect.x > layoutViewport.XMost() ||
        aPositionFixedRect.XMost() < layoutViewport.x ||
        aPositionFixedRect.y > layoutViewport.YMost() ||
        aPositionFixedRect.YMost() < layoutViewport.y) {
      return false;
    }
    return true;
  }

  if (!layoutViewport.Intersects(aPositionFixedRect)) {
    return false;
  }
  return true;
}

void PresShell::ScrollFrameIntoVisualViewport(Maybe<nsPoint>& aDestination,
                                              const nsRect& aPositionFixedRect,
                                              ScrollFlags aScrollFlags) {
  PresShell* root = GetRootPresShell();
  if (!root) {
    return;
  }

  if (!root->GetPresContext()->IsRootContentDocumentCrossProcess()) {
    return;
  }

  ScrollContainerFrame* rootScrollContainer =
      root->GetRootScrollContainerFrame();
  if (!rootScrollContainer) {
    return;
  }

  if (!aDestination) {
    // If we have in the top level content document but we didn't reach to
    // the root scroll container in the frame tree walking up loop in
    // ScrollFrameIntoView, it means the target element is inside a
    // position:fixed subtree.
    if (!StaticPrefs::layout_scroll_fixed_content_into_view_visually()) {
      return;
    }

    const nsSize visualViewportSize =
        rootScrollContainer->GetVisualViewportSize();

    const nsSize layoutViewportSize = root->GetLayoutViewportSize();
    const nsRect layoutViewport = nsRect(nsPoint(), layoutViewportSize);
    // `positon:fixed` element are attached/fixed to the ViewportFrame, which is
    // the parent of the root scroll container frame, thus what we need here is
    // the visible area of the position:fixed element inside the root scroll
    // container frame.
    // For example, if the top left position of the fixed element is (-100,
    // -100), it's outside of the scrollable range either in the layout viewport
    // or the visual viewport. Likewise, if the right bottom position of the
    // fixed element is (110vw, 110vh), it's also outside of the scrollable
    // range.
    const nsRect clampedPositionFixedRect =
        aPositionFixedRect.MoveInsideAndClamp(layoutViewport);
    // If the position:fixed element is already inside the visual viewport, we
    // don't need to scroll visually.
    if (clampedPositionFixedRect.y >= 0 &&
        clampedPositionFixedRect.YMost() <= visualViewportSize.height &&
        clampedPositionFixedRect.x >= 0 &&
        clampedPositionFixedRect.XMost() <= visualViewportSize.width) {
      return;
    }

    // If the position:fixed element is totally outside of the the layout
    // viewport, it will never be in the viewport.
    if (!NeedToVisuallyScroll(layoutViewportSize, aPositionFixedRect)) {
      return;
    }
    // Offset the position:fixed element position by the layout scroll
    // position because the position:fixed origin (0, 0) is the layout scroll
    // position. Otherwise if we've already scrolled, this scrollIntoView
    // operaiton will jump back to near (0, 0) position.
    // Bug 1947470: We need to calculate the destination with `WhereToScroll`
    // options.
    const nsPoint layoutOffset = rootScrollContainer->GetScrollPosition();
    aDestination = Some(aPositionFixedRect.TopLeft() + layoutOffset);
  }

  // NOTE: It seems chrome doesn't respect the root element's
  // scroll-behavior for visual scrolling.
  ScrollMode scrollMode =
      GetScrollModeForScrollIntoView(rootScrollContainer, aScrollFlags);
  root->ScrollToVisual(*aDestination, FrameMetrics::eMainThread, scrollMode);
}

bool PresShell::ScrollFrameIntoView(
    nsIFrame* aTargetFrame, const Maybe<nsRect>& aKnownRectRelativeToTarget,
    ScrollAxis aVertical, ScrollAxis aHorizontal, ScrollFlags aScrollFlags) {
  // If the AxesAreLogical flag is set, the aVertical and aHorizontal params
  // actually refer to block and inline axes respectively, so we resolve them
  // to physical axes/directions here.
  // XXX Maybe we should convert more of the following code to logical axes,
  // if it's convenient for more callers to work that way?
  if (aScrollFlags & ScrollFlags::AxesAreLogical) {
    // The aVertical parameter actually refers to the element's block axis,
    // and aHorizontal to its inline axis. Potentially reverse/swap them,
    // according to its writing mode and directionality.
    WritingMode wm = aTargetFrame->GetWritingMode();
    if (wm.IsVerticalRL()) {
      // Reverse the block-axis percentage.
      if (aVertical.mWhereToScroll.mPercentage) {
        aVertical.mWhereToScroll.mPercentage =
            Some(100 - aVertical.mWhereToScroll.mPercentage.value());
      }
    }
    if (wm.IsInlineReversed()) {
      // Reverse the inline-axis percentage.
      if (aHorizontal.mWhereToScroll.mPercentage) {
        aHorizontal.mWhereToScroll.mPercentage =
            Some(100 - aHorizontal.mWhereToScroll.mPercentage.value());
      }
    }
    if (wm.IsVertical()) {
      std::swap(aVertical, aHorizontal);
    }
    // Remove the AxesAreLogical flag, to make it clear that methods we call
    // always get physical axes from here on.
    aScrollFlags &= ~ScrollFlags::AxesAreLogical;
  }

  // The scroll margin only applies to the whole bounds of the element, so don't
  // apply it if we get an arbitrary rect / point to scroll to.
  const nsMargin scrollMargin =
      aKnownRectRelativeToTarget ? nsMargin() : GetScrollMargin(aTargetFrame);

  Sides skipPaddingSides;
  const auto MaybeSkipPaddingSides = [&](nsIFrame* aFrame) {
    if (!aFrame->IsStickyPositioned()) {
      return;
    }
    const nsPoint pos = aFrame->GetPosition();
    const nsPoint normalPos = aFrame->GetNormalPosition();
    if (pos == normalPos) {
      return;  // Frame is not stuck.
    }
    // If we're targetting a sticky element, make sure not to apply
    // scroll-padding on the direction we're stuck.
    const auto* stylePosition = aFrame->StylePosition();
    const auto positionProperty = aFrame->StyleDisplay()->mPosition;
    for (auto side : AllPhysicalSides()) {
      if (stylePosition->GetAnchorResolvedInset(side, positionProperty)
              ->IsAuto()) {
        continue;
      }
      // See if this axis is stuck.
      const bool yAxis = side == eSideTop || side == eSideBottom;
      const bool stuck = yAxis ? pos.y != normalPos.y : pos.x != normalPos.x;
      if (!stuck) {
        continue;
      }
      skipPaddingSides |= SideToSideBit(side);
    }
  };

  nsIFrame* container = aTargetFrame;

  bool inPositionFixedSubtree = false;
  auto isPositionFixed = [&](const nsIFrame* aFrame) -> bool {
    return aFrame->StyleDisplay()->mPosition == StylePositionProperty::Fixed &&
           nsLayoutUtils::IsReallyFixedPos(aFrame);
  };
  // This function needs to work even if rect has a width or height of 0.
  nsRect rect = [&] {
    if (aKnownRectRelativeToTarget) {
      return *aKnownRectRelativeToTarget;
    }
    MaybeSkipPaddingSides(aTargetFrame);
    while (nsIFrame* parent = container->GetParent()) {
      if (isPositionFixed(container)) {
        inPositionFixedSubtree = true;
      }
      container = parent;
      if (container->IsScrollContainerOrSubclass()) {
        // We really just need a non-fragmented frame so that we can accumulate
        // the bounds of all our continuations relative to it. We shouldn't jump
        // out of our nearest scrollable frame, and that's an ok reference
        // frame, so try to use that, or the root frame if there's nothing to
        // scroll in this document.
        break;
      }
      MaybeSkipPaddingSides(container);
    }
    MOZ_DIAGNOSTIC_ASSERT(container);

    nsRect targetFrameBounds;
    {
      bool haveRect = false;
      const bool useWholeLineHeightForInlines =
          aVertical.mWhenToScroll != WhenToScroll::IfNotFullyVisible;
      AutoAssertNoDomMutations
          guard;  // Ensure use of nsILineIterators is safe.
      nsIFrame* prevBlock = nullptr;
      // Reuse the same line iterator across calls to AccumulateFrameBounds.
      // We set it every time we detect a new block (stored in prevBlock).
      nsILineIterator* lines = nullptr;
      // The last line we found a continuation on in |lines|.  We assume that
      // later continuations cannot come on earlier lines.
      int32_t curLine = 0;
      nsIFrame* frame = aTargetFrame;
      do {
        AccumulateFrameBounds(container, frame, useWholeLineHeightForInlines,
                              targetFrameBounds, haveRect, prevBlock, lines,
                              curLine);
      } while ((frame = frame->GetNextContinuation()));
    }

    return targetFrameBounds;
  }();
  bool didScroll = false;
  const nsIFrame* target = aTargetFrame;
  Maybe<nsPoint> rootScrollDestination;
  // Walk up the frame hierarchy scrolling the rect into view and
  // keeping rect relative to container
  do {
    if (isPositionFixed(container)) {
      inPositionFixedSubtree = true;
    }

    if (ScrollContainerFrame* sf = do_QueryFrame(container)) {
      nsPoint oldPosition = sf->GetScrollPosition();
      nsRect targetRect = rect;
      // Inflate the scrolled rect by the container's padding in each dimension,
      // unless we have 'overflow-clip-box-*: content-box' in that dimension.
      auto* disp = container->StyleDisplay();
      if (disp->mOverflowClipBoxBlock == StyleOverflowClipBox::ContentBox ||
          disp->mOverflowClipBoxInline == StyleOverflowClipBox::ContentBox) {
        WritingMode wm = container->GetWritingMode();
        bool cbH = (wm.IsVertical() ? disp->mOverflowClipBoxBlock
                                    : disp->mOverflowClipBoxInline) ==
                   StyleOverflowClipBox::ContentBox;
        bool cbV = (wm.IsVertical() ? disp->mOverflowClipBoxInline
                                    : disp->mOverflowClipBoxBlock) ==
                   StyleOverflowClipBox::ContentBox;
        nsMargin padding = container->GetUsedPadding();
        if (!cbH) {
          padding.left = padding.right = nscoord(0);
        }
        if (!cbV) {
          padding.top = padding.bottom = nscoord(0);
        }
        targetRect.Inflate(padding);
      }

      targetRect -= sf->GetScrolledFrame()->GetPosition();

      {
        AutoWeakFrame wf(container);
        Maybe<nsPoint> destination = ScrollToShowRect(
            sf, container, target, targetRect, skipPaddingSides, scrollMargin,
            aVertical, aHorizontal, aScrollFlags);
        if (!wf.IsAlive()) {
          return didScroll;
        }

        if (sf->IsRootScrollFrameOfDocument() &&
            sf->PresContext()->IsRootContentDocumentCrossProcess()) {
          rootScrollDestination = destination;
        }
      }

      nsPoint newPosition = sf->LastScrollDestination();
      // If the scroll position increased, that means our content moved up,
      // so our rect's offset should decrease
      rect += oldPosition - newPosition;

      if (oldPosition != newPosition) {
        didScroll = true;
      }

      // only scroll one container when this flag is set
      if (aScrollFlags & ScrollFlags::ScrollFirstAncestorOnly) {
        break;
      }

      // This scroll container will be the next target element in the nearest
      // ancestor scroll container.
      target = container;
      // We found a sticky scroll container, we shouldn't skip that side
      // anymore.
      skipPaddingSides = {};
    }

    MaybeSkipPaddingSides(container);

    nsIFrame* parent = container->GetParent();
    NS_ASSERTION(parent || !container->IsTransformed(),
                 "viewport shouldnt be transformed");
    if (parent && container->IsTransformed()) {
      rect =
          nsLayoutUtils::TransformFrameRectToAncestor(container, rect, parent);
    } else {
      rect += container->GetPosition();
    }
    if (!parent && !(aScrollFlags & ScrollFlags::ScrollNoParentFrames)) {
      nsPoint extraOffset(0, 0);
      int32_t APD = container->PresContext()->AppUnitsPerDevPixel();
      parent = nsLayoutUtils::GetCrossDocParentFrameInProcess(container,
                                                              &extraOffset);
      if (parent) {
        int32_t parentAPD = parent->PresContext()->AppUnitsPerDevPixel();
        rect = rect.ScaleToOtherAppUnitsRoundOut(APD, parentAPD);
        rect += extraOffset;
      } else {
        nsCOMPtr<nsIDocShell> docShell =
            container->PresContext()->GetDocShell();
        if (BrowserChild* browserChild = BrowserChild::GetFrom(docShell)) {
          // Defer to the parent document if this is an out-of-process iframe.
          Unused << browserChild->SendScrollRectIntoView(
              rect, aVertical, aHorizontal, aScrollFlags, APD);
        }
      }
    }
    container = parent;
  } while (container);

  // If this is inside the top level content document process (and a direct
  // descendant of it), also call ScrollToVisual() since we want to
  // scroll the rect into view visually, and that may require scrolling
  // the visual viewport in scenarios where there is not enough layout
  // scroll range.
  if (!rootScrollDestination && !inPositionFixedSubtree) {
    return didScroll;
  }

  ScrollFrameIntoVisualViewport(rootScrollDestination, rect, aScrollFlags);

  return didScroll;
}

void PresShell::SchedulePaint() {
  if (MOZ_UNLIKELY(mIsDestroying)) {
    return;
  }
  if (nsPresContext* presContext = GetPresContext()) {
    presContext->RefreshDriver()->SchedulePaint();
  }
}

void PresShell::DispatchSynthMouseMove(WidgetGUIEvent* aEvent) {
  AUTO_PROFILER_TRACING_MARKER_DOCSHELL("Paint", "DispatchSynthMouseMove",
                                        GRAPHICS, mPresContext->GetDocShell());
  nsEventStatus status = nsEventStatus_eIgnore;
  nsView* targetView = nsView::GetViewFor(aEvent->mWidget);
  if (!targetView) {
    return;
  }
  RefPtr<nsViewManager> viewManager = targetView->GetViewManager();
  viewManager->DispatchEvent(aEvent, targetView, &status);
}

void PresShell::ClearMouseCaptureOnView(nsView* aView) {
  if (nsIContent* capturingContent = GetCapturingContent()) {
    if (aView) {
      // if a view was specified, ensure that the captured content is within
      // this view.
      nsIFrame* frame = capturingContent->GetPrimaryFrame();
      if (frame) {
        nsView* view = frame->GetClosestView();
        // if there is no view, capturing won't be handled any more, so
        // just release the capture.
        if (view) {
          do {
            if (view == aView) {
              ReleaseCapturingContent();
              // the view containing the captured content likely disappeared so
              // disable capture for now.
              AllowMouseCapture(false);
              break;
            }

            view = view->GetParent();
          } while (view);
          // return if the view wasn't found
          return;
        }
      }
    }

    ReleaseCapturingContent();
  }

  // disable mouse capture until the next mousedown as a dialog has opened
  // or a drag has started. Otherwise, someone could start capture during
  // the modal dialog or drag.
  AllowMouseCapture(false);
}

void PresShell::ClearMouseCapture() {
  ReleaseCapturingContent();
  AllowMouseCapture(false);
}

void PresShell::ClearMouseCapture(nsIFrame* aFrame) {
  MOZ_ASSERT(aFrame);

  nsIContent* capturingContent = GetCapturingContent();
  if (!capturingContent) {
    return;
  }

  nsIFrame* capturingFrame = capturingContent->GetPrimaryFrame();
  const bool shouldClear =
      !capturingFrame ||
      nsLayoutUtils::IsAncestorFrameCrossDocInProcess(aFrame, capturingFrame);
  if (shouldClear) {
    ClearMouseCapture();
  }
}

nsresult PresShell::CaptureHistoryState(nsILayoutHistoryState** aState) {
  MOZ_ASSERT(nullptr != aState, "null state pointer");

  // We actually have to mess with the docshell here, since we want to
  // store the state back in it.
  // XXXbz this isn't really right, since this is being called in the
  // content viewer's Hide() method...  by that point the docshell's
  // state could be wrong.  We should sort out a better ownership
  // model for the layout history state.
  nsCOMPtr<nsIDocShell> docShell(mPresContext->GetDocShell());
  if (!docShell) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsILayoutHistoryState> historyState;
  docShell->GetLayoutHistoryState(getter_AddRefs(historyState));
  if (!historyState) {
    // Create the document state object
    historyState = NS_NewLayoutHistoryState();
    docShell->SetLayoutHistoryState(historyState);
  }

  *aState = historyState;
  NS_IF_ADDREF(*aState);

  // Capture frame state for the entire frame hierarchy
  nsIFrame* rootFrame = mFrameConstructor->GetRootFrame();
  if (!rootFrame) {
    return NS_OK;
  }

  mFrameConstructor->CaptureFrameState(rootFrame, historyState);

  return NS_OK;
}

void PresShell::ScheduleBeforeFirstPaint() {
  if (!mDocument->IsResourceDoc()) {
    // Notify observers that a new page is about to be drawn. Execute this
    // as soon as it is safe to run JS, which is guaranteed to be before we
    // go back to the event loop and actually draw the page.
    MOZ_LOG(gLog, LogLevel::Debug,
            ("PresShell::ScheduleBeforeFirstPaint this=%p", this));

    nsContentUtils::AddScriptRunner(
        new nsBeforeFirstPaintDispatcher(mDocument));
  }
}

void PresShell::UnsuppressAndInvalidate() {
  // Note: We ignore the EnsureVisible check for resource documents, because
  // they won't have a docshell, so they'll always fail EnsureVisible.
  if ((!mDocument->IsResourceDoc() && !mPresContext->EnsureVisible()) ||
      mHaveShutDown) {
    // No point; we're about to be torn down anyway.
    return;
  }

  ScheduleBeforeFirstPaint();

  PROFILER_MARKER_UNTYPED("UnsuppressAndInvalidate", GRAPHICS);

  mPaintingSuppressed = false;
  if (nsIFrame* rootFrame = mFrameConstructor->GetRootFrame()) {
    // let's assume that outline on a root frame is not supported
    rootFrame->InvalidateFrame();
  }

  if (mPresContext->IsRootContentDocumentCrossProcess()) {
    if (auto* bc = BrowserChild::GetFrom(mDocument->GetDocShell())) {
      if (mDocument->IsInitialDocument()) {
        bc->SendDidUnsuppressPaintingNormalPriority();
      } else {
        bc->SendDidUnsuppressPainting();
      }
    }
  }

  // now that painting is unsuppressed, focus may be set on the document
  if (nsPIDOMWindowOuter* win = mDocument->GetWindow()) {
    win->SetReadyForFocus();
  }

  if (!mHaveShutDown) {
    SynthesizeMouseMove(false);
    ScheduleApproximateFrameVisibilityUpdateNow();
  }
}

void PresShell::CancelPaintSuppressionTimer() {
  if (mPaintSuppressionTimer) {
    mPaintSuppressionTimer->Cancel();
    mPaintSuppressionTimer = nullptr;
  }
}

void PresShell::UnsuppressPainting() {
  CancelPaintSuppressionTimer();

  if (mIsDocumentGone || !mPaintingSuppressed) {
    return;
  }

  // If we have reflows pending, just wait until we process
  // the reflows and get all the frames where we want them
  // before actually unlocking the painting.  Otherwise
  // go ahead and unlock now.
  if (!mDirtyRoots.IsEmpty()) {
    mShouldUnsuppressPainting = true;
  } else {
    UnsuppressAndInvalidate();
  }
}

// Post a request to handle an arbitrary callback after reflow has finished.
nsresult PresShell::PostReflowCallback(nsIReflowCallback* aCallback) {
  void* result = AllocateByObjectID(eArenaObjectID_nsCallbackEventRequest,
                                    sizeof(nsCallbackEventRequest));
  nsCallbackEventRequest* request = (nsCallbackEventRequest*)result;

  request->callback = aCallback;
  request->next = nullptr;

  if (mLastCallbackEventRequest) {
    mLastCallbackEventRequest = mLastCallbackEventRequest->next = request;
  } else {
    mFirstCallbackEventRequest = request;
    mLastCallbackEventRequest = request;
  }

  return NS_OK;
}

void PresShell::CancelReflowCallback(nsIReflowCallback* aCallback) {
  nsCallbackEventRequest* before = nullptr;
  nsCallbackEventRequest* node = mFirstCallbackEventRequest;
  while (node) {
    nsIReflowCallback* callback = node->callback;

    if (callback == aCallback) {
      nsCallbackEventRequest* toFree = node;
      if (node == mFirstCallbackEventRequest) {
        node = node->next;
        mFirstCallbackEventRequest = node;
        NS_ASSERTION(before == nullptr, "impossible");
      } else {
        node = node->next;
        before->next = node;
      }

      if (toFree == mLastCallbackEventRequest) {
        mLastCallbackEventRequest = before;
      }

      FreeByObjectID(eArenaObjectID_nsCallbackEventRequest, toFree);
    } else {
      before = node;
      node = node->next;
    }
  }
}

void PresShell::CancelPostedReflowCallbacks() {
  while (mFirstCallbackEventRequest) {
    nsCallbackEventRequest* node = mFirstCallbackEventRequest;
    mFirstCallbackEventRequest = node->next;
    if (!mFirstCallbackEventRequest) {
      mLastCallbackEventRequest = nullptr;
    }
    nsIReflowCallback* callback = node->callback;
    FreeByObjectID(eArenaObjectID_nsCallbackEventRequest, node);
    if (callback) {
      callback->ReflowCallbackCanceled();
    }
  }
}

void PresShell::HandlePostedReflowCallbacks(bool aInterruptible) {
  while (true) {
    // Call all our callbacks, tell us if we need to flush again.
    bool shouldFlush = false;
    while (mFirstCallbackEventRequest) {
      nsCallbackEventRequest* node = mFirstCallbackEventRequest;
      mFirstCallbackEventRequest = node->next;
      if (!mFirstCallbackEventRequest) {
        mLastCallbackEventRequest = nullptr;
      }
      nsIReflowCallback* callback = node->callback;
      FreeByObjectID(eArenaObjectID_nsCallbackEventRequest, node);
      if (callback && callback->ReflowFinished()) {
        shouldFlush = true;
      }
    }

    if (!shouldFlush || mIsDestroying) {
      return;
    }

    // The flush might cause us to have more callbacks.
    const auto flushType =
        aInterruptible ? FlushType::InterruptibleLayout : FlushType::Layout;
    FlushPendingNotifications(flushType);
  }
}

bool PresShell::IsSafeToFlush() const {
  // Not safe if we are getting torn down, reflowing, or in the middle of frame
  // construction.
  if (mIsReflowing || mChangeNestCount || mIsDestroying) {
    return false;
  }

  // Not safe if we are painting
  if (nsViewManager* viewManager = GetViewManager()) {
    bool isPainting = false;
    viewManager->IsPainting(isPainting);
    if (isPainting) {
      return false;
    }
  }

  return true;
}

void PresShell::NotifyFontFaceSetOnRefresh() {
  if (FontFaceSet* set = mDocument->GetFonts()) {
    set->DidRefresh();
  }
}

void PresShell::DoFlushPendingNotifications(FlushType aType) {
  // by default, flush animations if aType >= FlushType::Style
  mozilla::ChangesToFlush flush(aType, aType >= FlushType::Style,
                                aType >= FlushType::Layout);
  FlushPendingNotifications(flush);
}

#ifdef DEBUG
static void AssertFrameSubtreeIsSane(const nsIFrame& aRoot) {
  if (const nsIContent* content = aRoot.GetContent()) {
    MOZ_ASSERT(content->GetFlattenedTreeParentNodeForStyle(),
               "Node not in the flattened tree still has a frame?");
  }

  for (const auto& childList : aRoot.ChildLists()) {
    for (const nsIFrame* child : childList.mList) {
      AssertFrameSubtreeIsSane(*child);
    }
  }
}
#endif

static inline void AssertFrameTreeIsSane(const PresShell& aPresShell) {
#ifdef DEBUG
  if (const nsIFrame* root = aPresShell.GetRootFrame()) {
    AssertFrameSubtreeIsSane(*root);
  }
#endif
}

static void TriggerPendingScrollTimelineAnimations(Document* aDocument) {
  auto* tracker = aDocument->GetScrollTimelineAnimationTracker();
  if (!tracker || !tracker->HasPendingAnimations()) {
    return;
  }
  tracker->TriggerPendingAnimations();
}

void PresShell::DoFlushPendingNotifications(mozilla::ChangesToFlush aFlush) {
  // FIXME(emilio, bug 1530177): Turn into a release assert when bug 1530188 and
  // bug 1530190 are fixed.
  MOZ_DIAGNOSTIC_ASSERT(!mForbiddenToFlush, "This is bad!");

  // Per our API contract, hold a strong ref to ourselves until we return.
  RefPtr<PresShell> kungFuDeathGrip = this;

  /**
   * VERY IMPORTANT: If you add some sort of new flushing to this
   * method, make sure to add the relevant SetNeedLayoutFlush or
   * SetNeedStyleFlush calls on the shell.
   */
  FlushType flushType = aFlush.mFlushType;

  if (aFlush.mUpdateRelevancy) {
    // If needed, first update the relevancy of any content of elements with
    // `content-visibility: auto` so that the values returned from e.g. script
    // queries are up-to-date.
    UpdateRelevancyOfContentVisibilityAutoFrames();
  }

  MOZ_ASSERT(NeedFlush(flushType), "Why did we get called?");

  AUTO_PROFILER_MARKER_TEXT(
      "DoFlushPendingNotifications", LAYOUT,
      MarkerOptions(MarkerStack::Capture(), MarkerInnerWindowIdFromDocShell(
                                                mPresContext->GetDocShell())),
      nsDependentCString(kFlushTypeNames[flushType]));
  AUTO_PROFILER_LABEL_DYNAMIC_CSTR_NONSENSITIVE(
      "PresShell::DoFlushPendingNotifications", LAYOUT,
      kFlushTypeNames[flushType]);

#ifdef ACCESSIBILITY
#  ifdef DEBUG
  if (nsAccessibilityService* accService = GetAccService()) {
    NS_ASSERTION(!accService->IsProcessingRefreshDriverNotification(),
                 "Flush during accessible tree update!");
  }
#  endif
#endif

  NS_ASSERTION(flushType >= FlushType::Style, "Why did we get called?");

  bool isSafeToFlush = IsSafeToFlush();

  // If layout could possibly trigger scripts, then it's only safe to flush if
  // it's safe to run script.
  bool hasHadScriptObject;
  if (mDocument->GetScriptHandlingObject(hasHadScriptObject) ||
      hasHadScriptObject) {
    isSafeToFlush = isSafeToFlush && nsContentUtils::IsSafeToRunScript();
  }

  // Don't flush if the doc is already in the bfcache.
  if (MOZ_UNLIKELY(mDocument->GetPresShell() != this)) {
    MOZ_DIAGNOSTIC_ASSERT(!mDocument->GetPresShell(),
                          "Where did this shell come from?");
    isSafeToFlush = false;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mIsDestroying || !isSafeToFlush);
  MOZ_DIAGNOSTIC_ASSERT(mIsDestroying || mViewManager);
  MOZ_DIAGNOSTIC_ASSERT(mIsDestroying || mDocument->HasShellOrBFCacheEntry());

  if (!isSafeToFlush) {
    return;
  }

  // Make sure the view manager stays alive.
  RefPtr<nsViewManager> viewManager = mViewManager;
  // We need to make sure external resource documents are flushed too (for
  // example, svg filters that reference a filter in an external document
  // need the frames in the external document to be constructed for the
  // filter to work). We only need external resources to be flushed when the
  // main document is flushing >= FlushType::Frames, so we flush external
  // resources here instead of Document::FlushPendingNotifications.
  mDocument->FlushExternalResources(flushType);

  // Force flushing of any pending content notifications that might have
  // queued up while our event was pending.  That will ensure that we don't
  // construct frames for content right now that's still waiting to be
  // notified on,
  mDocument->FlushPendingNotifications(FlushType::ContentAndNotify);

  mDocument->UpdateSVGUseElementShadowTrees();

  // Process pending restyles, since any flush of the presshell wants
  // up-to-date style data.
  if (MOZ_LIKELY(!mIsDestroying)) {
    viewManager->FlushDelayedResize();
    mPresContext->FlushPendingMediaFeatureValuesChanged();
  }

  if (MOZ_LIKELY(!mIsDestroying)) {
    // Now that we have flushed media queries, update the rules before looking
    // up @font-face / @counter-style / @font-feature-values rules.
    StyleSet()->UpdateStylistIfNeeded();

    // Flush any pending update of the user font set, since that could
    // cause style changes (for updating ex/ch units, and to cause a
    // reflow).
    mDocument->FlushUserFontSet();

    mPresContext->FlushCounterStyles();

    mPresContext->FlushFontFeatureValues();

    mPresContext->FlushFontPaletteValues();

    // Flush any requested SMIL samples.
    if (mDocument->HasAnimationController()) {
      mDocument->GetAnimationController()->FlushResampleRequests();
    }
  }

  // The FlushResampleRequests() above might have flushed style changes.
  if (MOZ_LIKELY(!mIsDestroying)) {
    if (aFlush.mFlushAnimations) {
      mPresContext->EffectCompositor()->PostRestyleForThrottledAnimations();
      mNeedThrottledAnimationFlush = false;
    }

    nsAutoScriptBlocker scriptBlocker;
    Maybe<uint64_t> innerWindowID;
    if (auto* window = mDocument->GetInnerWindow()) {
      innerWindowID = Some(window->WindowID());
    }
    AutoProfilerStyleMarker tracingStyleFlush(std::move(mStyleCause),
                                              innerWindowID);
    PerfStats::AutoMetricRecording<PerfStats::Metric::Styling> autoRecording;

    mPresContext->RestyleManager()->ProcessPendingRestyles();
    mNeedStyleFlush = false;
  }

  AssertFrameTreeIsSane(*this);

  if (flushType >= (SuppressInterruptibleReflows()
                        ? FlushType::Layout
                        : FlushType::InterruptibleLayout) &&
      !mIsDestroying) {
    if (DoFlushLayout(/* aInterruptible = */ flushType < FlushType::Layout)) {
      if (mContentToScrollTo) {
        DoScrollContentIntoView();
        if (mContentToScrollTo) {
          mContentToScrollTo->RemoveProperty(nsGkAtoms::scrolling);
          mContentToScrollTo = nullptr;
        }
      }
    }
    // FIXME(emilio): Maybe we should assert here but it's not 100% sure it'd
    // hold right now, UnsuppressAndInvalidate and so on can run script...
    if (MOZ_LIKELY(mDirtyRoots.IsEmpty())) {
      mNeedLayoutFlush = false;
    }
  }

  FlushPendingScrollResnap();

  if (MOZ_LIKELY(!mIsDestroying)) {
    // Try to trigger pending scroll-driven animations after we flush
    // style and layout (if any). If we try to trigger them after flushing
    // style but the frame tree is not ready, we will check them again after
    // we flush layout because the requirement to trigger scroll-driven
    // animations is that the associated scroll containers are ready (i.e. the
    // scroll-timeline is active), and this depends on the readiness of the
    // scrollable frame and the primary frame of the scroll container.
    TriggerPendingScrollTimelineAnimations(mDocument);
  }

  if (flushType >= FlushType::Layout) {
    if (!mIsDestroying) {
      viewManager->UpdateWidgetGeometry();
    }
  }
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::CharacterDataChanged(
    nsIContent* aContent, const CharacterDataChangeInfo& aInfo) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected CharacterDataChanged");
  MOZ_ASSERT(aContent->OwnerDoc() == mDocument, "Unexpected document");

  nsAutoCauseReflowNotifier crNotifier(this);

  mPresContext->RestyleManager()->CharacterDataChanged(aContent, aInfo);
  mFrameConstructor->CharacterDataChanged(aContent, aInfo);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::ElementStateChanged(
    Document* aDocument, Element* aElement, ElementState aStateMask) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected ContentStateChanged");
  MOZ_ASSERT(aDocument == mDocument, "Unexpected aDocument");

  if (mDidInitialize) {
    nsAutoCauseReflowNotifier crNotifier(this);
    mPresContext->RestyleManager()->ElementStateChanged(aElement, aStateMask);
  }
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::CustomStatesWillChange(
    Element& aElement) {
  if (MOZ_UNLIKELY(!mDidInitialize)) {
    return;
  }

  mPresContext->RestyleManager()->CustomStatesWillChange(aElement);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::CustomStateChanged(
    Element& aElement, nsAtom* aState) {
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected CustomStateChanged");
  MOZ_ASSERT(aState, "Unexpected empty state");

  if (mDidInitialize) {
    nsAutoCauseReflowNotifier crNotifier(this);
    mPresContext->RestyleManager()->CustomStateChanged(aElement, aState);
  }
}

void PresShell::DocumentStatesChanged(DocumentState aStateMask) {
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected DocumentStatesChanged");
  MOZ_ASSERT(mDocument);
  MOZ_ASSERT(!aStateMask.IsEmpty());

  if (mDidInitialize) {
    StyleSet()->InvalidateStyleForDocumentStateChanges(aStateMask);
  }

  if (aStateMask.HasState(DocumentState::WINDOW_INACTIVE)) {
    if (nsIFrame* root = mFrameConstructor->GetRootFrame()) {
      root->SchedulePaint();
    }
  }
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::AttributeWillChange(
    Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
    int32_t aModType) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected AttributeWillChange");
  MOZ_ASSERT(aElement->OwnerDoc() == mDocument, "Unexpected document");

  // XXXwaterson it might be more elegant to wait until after the
  // initial reflow to begin observing the document. That would
  // squelch any other inappropriate notifications as well.
  if (mDidInitialize) {
    nsAutoCauseReflowNotifier crNotifier(this);
    mPresContext->RestyleManager()->AttributeWillChange(aElement, aNameSpaceID,
                                                        aAttribute, aModType);
  }
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::AttributeChanged(
    Element* aElement, int32_t aNameSpaceID, nsAtom* aAttribute,
    int32_t aModType, const nsAttrValue* aOldValue) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected AttributeChanged");
  MOZ_ASSERT(aElement->OwnerDoc() == mDocument, "Unexpected document");

  // XXXwaterson it might be more elegant to wait until after the
  // initial reflow to begin observing the document. That would
  // squelch any other inappropriate notifications as well.
  if (mDidInitialize) {
    nsAutoCauseReflowNotifier crNotifier(this);
    mPresContext->RestyleManager()->AttributeChanged(
        aElement, aNameSpaceID, aAttribute, aModType, aOldValue);
  }
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::ContentAppended(
    nsIContent* aFirstNewContent) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected ContentAppended");
  MOZ_ASSERT(aFirstNewContent->OwnerDoc() == mDocument, "Unexpected document");

  // We never call ContentAppended with a document as the container, so we can
  // assert that we have an nsIContent parent.
  MOZ_ASSERT(aFirstNewContent->GetParent());
  MOZ_ASSERT(aFirstNewContent->GetParent()->IsElement() ||
             aFirstNewContent->GetParent()->IsShadowRoot());

  if (!mDidInitialize) {
    return;
  }

  nsAutoCauseReflowNotifier crNotifier(this);

  // Call this here so it only happens for real content mutations and
  // not cases when the frame constructor calls its own methods to force
  // frame reconstruction.
  mPresContext->RestyleManager()->ContentAppended(aFirstNewContent);

  mFrameConstructor->ContentAppended(
      aFirstNewContent, nsCSSFrameConstructor::InsertionKind::Async);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::ContentInserted(
    nsIContent* aChild) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected ContentInserted");
  MOZ_ASSERT(aChild->OwnerDoc() == mDocument, "Unexpected document");

  if (!mDidInitialize) {
    return;
  }

  nsAutoCauseReflowNotifier crNotifier(this);

  // Call this here so it only happens for real content mutations and
  // not cases when the frame constructor calls its own methods to force
  // frame reconstruction.
  mPresContext->RestyleManager()->ContentInserted(aChild);

  mFrameConstructor->ContentInserted(
      aChild, nsCSSFrameConstructor::InsertionKind::Async);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY void PresShell::ContentWillBeRemoved(
    nsIContent* aChild, const BatchRemovalState*) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript());
  MOZ_ASSERT(!mIsDocumentGone, "Unexpected ContentRemoved");
  MOZ_ASSERT(aChild->OwnerDoc() == mDocument, "Unexpected document");
  // Notify the ESM that the content has been removed, so that
  // it can clean up any state related to the content.

  mPresContext->EventStateManager()->ContentRemoved(mDocument, aChild);

  nsAutoCauseReflowNotifier crNotifier(this);

  // After removing aChild from tree we should save information about live
  // ancestor
  if (mPointerEventTarget &&
      mPointerEventTarget->IsInclusiveDescendantOf(aChild)) {
    mPointerEventTarget = aChild->GetParent();
  }

  mFrameConstructor->ContentWillBeRemoved(
      aChild, nsCSSFrameConstructor::REMOVE_CONTENT);

  // NOTE(emilio): It's important that this goes after the frame constructor
  // stuff, otherwise the frame constructor can't see elements which are
  // display: contents / display: none, because we'd have cleared all the style
  // data from there.
  mPresContext->RestyleManager()->ContentWillBeRemoved(aChild);
}

void PresShell::NotifyCounterStylesAreDirty() {
  // TODO: Looks like that nsFrameConstructor::NotifyCounterStylesAreDirty()
  //       does not run script.  If so, we don't need to block script with
  //       nsAutoCauseReflowNotifier here.  Instead, there should be methods
  //       and stack only class which manages only mChangeNestCount for
  //       avoiding unnecessary `MOZ_CAN_RUN_SCRIPT` marking.
  nsAutoCauseReflowNotifier reflowNotifier(this);
  mFrameConstructor->NotifyCounterStylesAreDirty();
}

bool PresShell::FrameIsAncestorOfDirtyRoot(nsIFrame* aFrame) const {
  return mDirtyRoots.FrameIsAncestorOfAnyElement(aFrame);
}

void PresShell::ReconstructFrames() {
  MOZ_ASSERT(!mFrameConstructor->GetRootFrame() || mDidInitialize,
             "Must not have root frame before initial reflow");
  if (!mDidInitialize || mIsDestroying) {
    // Nothing to do here
    return;
  }

  if (Element* root = mDocument->GetRootElement()) {
    PostRecreateFramesFor(root);
  }

  mDocument->FlushPendingNotifications(FlushType::Frames);
}

nsresult PresShell::RenderDocument(const nsRect& aRect,
                                   RenderDocumentFlags aFlags,
                                   nscolor aBackgroundColor,
                                   gfxContext* aThebesContext) {
  NS_ENSURE_TRUE(!(aFlags & RenderDocumentFlags::IsUntrusted),
                 NS_ERROR_NOT_IMPLEMENTED);

  nsRootPresContext* rootPresContext = mPresContext->GetRootPresContext();
  if (rootPresContext) {
    rootPresContext->FlushWillPaintObservers();
    if (mIsDestroying) {
      return NS_OK;
    }
  }

  nsAutoScriptBlocker blockScripts;

  // Set up the rectangle as the path in aThebesContext
  gfxRect r(0, 0, nsPresContext::AppUnitsToFloatCSSPixels(aRect.width),
            nsPresContext::AppUnitsToFloatCSSPixels(aRect.height));
  aThebesContext->NewPath();
#ifdef MOZ_GFX_OPTIMIZE_MOBILE
  aThebesContext->SnappedRectangle(r);
#else
  aThebesContext->Rectangle(r);
#endif

  nsIFrame* rootFrame = mFrameConstructor->GetRootFrame();
  if (!rootFrame) {
    // Nothing to paint, just fill the rect
    aThebesContext->SetColor(sRGBColor::FromABGR(aBackgroundColor));
    aThebesContext->Fill();
    return NS_OK;
  }

  gfxContextAutoSaveRestore save(aThebesContext);

  MOZ_ASSERT(aThebesContext->CurrentOp() == CompositionOp::OP_OVER);

  aThebesContext->Clip();

  nsDeviceContext* devCtx = mPresContext->DeviceContext();

  gfxPoint offset(-nsPresContext::AppUnitsToFloatCSSPixels(aRect.x),
                  -nsPresContext::AppUnitsToFloatCSSPixels(aRect.y));
  gfxFloat scale =
      gfxFloat(devCtx->AppUnitsPerDevPixel()) / AppUnitsPerCSSPixel();

  // Since canvas APIs use floats to set up their matrices, we may have some
  // slight rounding errors here.  We use NudgeToIntegers() here to adjust
  // matrix components that are integers up to the accuracy of floats to be
  // those integers.
  gfxMatrix newTM = aThebesContext->CurrentMatrixDouble()
                        .PreTranslate(offset)
                        .PreScale(scale, scale)
                        .NudgeToIntegers();
  aThebesContext->SetMatrixDouble(newTM);

  AutoSaveRestoreRenderingState _(this);

  bool wouldFlushRetainedLayers = false;
  PaintFrameFlags flags = PaintFrameFlags::IgnoreSuppression;
  if (aThebesContext->CurrentMatrix().HasNonIntegerTranslation()) {
    flags |= PaintFrameFlags::InTransform;
  }
  if (!(aFlags & RenderDocumentFlags::AsyncDecodeImages)) {
    flags |= PaintFrameFlags::SyncDecodeImages;
  }
  if (aFlags & RenderDocumentFlags::UseHighQualityScaling) {
    flags |= PaintFrameFlags::UseHighQualityScaling;
  }
  if (aFlags & RenderDocumentFlags::UseWidgetLayers) {
    // We only support using widget layers on display root's with widgets.
    nsView* view = rootFrame->GetView();
    if (view && view->GetWidget() &&
        nsLayoutUtils::GetDisplayRootFrame(rootFrame) == rootFrame) {
      WindowRenderer* renderer = view->GetWidget()->GetWindowRenderer();
      // WebRenderLayerManagers in content processes
      // don't support taking snapshots.
      if (renderer &&
          (!renderer->AsKnowsCompositor() || XRE_IsParentProcess())) {
        flags |= PaintFrameFlags::WidgetLayers;
      }
    }
  }
  if (!(aFlags & RenderDocumentFlags::DrawCaret)) {
    wouldFlushRetainedLayers = true;
    flags |= PaintFrameFlags::HideCaret;
  }
  if (aFlags & RenderDocumentFlags::IgnoreViewportScrolling) {
    wouldFlushRetainedLayers = !IgnoringViewportScrolling();
    mRenderingStateFlags |= RenderingStateFlags::IgnoringViewportScrolling;
  }
  if (aFlags & RenderDocumentFlags::ResetViewportScrolling) {
    wouldFlushRetainedLayers = true;
    flags |= PaintFrameFlags::ResetViewportScrolling;
  }
  if (aFlags & RenderDocumentFlags::DrawWindowNotFlushing) {
    mRenderingStateFlags |= RenderingStateFlags::DrawWindowNotFlushing;
  }
  if (aFlags & RenderDocumentFlags::DocumentRelative) {
    // XXX be smarter about this ... drawWindow might want a rect
    // that's "pretty close" to what our retained layer tree covers.
    // In that case, it wouldn't disturb normal rendering too much,
    // and we should allow it.
    wouldFlushRetainedLayers = true;
    flags |= PaintFrameFlags::DocumentRelative;
  }

  // Don't let drawWindow blow away our retained layer tree
  if ((flags & PaintFrameFlags::WidgetLayers) && wouldFlushRetainedLayers) {
    flags &= ~PaintFrameFlags::WidgetLayers;
  }

  nsLayoutUtils::PaintFrame(aThebesContext, rootFrame, nsRegion(aRect),
                            aBackgroundColor,
                            nsDisplayListBuilderMode::Painting, flags);

  return NS_OK;
}

/*
 * Clip the display list aList to a range. Returns the clipped
 * rectangle surrounding the range.
 */
nsRect PresShell::ClipListToRange(nsDisplayListBuilder* aBuilder,
                                  nsDisplayList* aList, nsRange* aRange) {
  // iterate though the display items and add up the bounding boxes of each.
  // This will allow the total area of the frames within the range to be
  // determined. To do this, remove an item from the bottom of the list, check
  // whether it should be part of the range, and if so, append it to the top
  // of the temporary list tmpList. If the item is a text frame at the end of
  // the selection range, clip it to the portion of the text frame that is
  // part of the selection. Then, append the wrapper to the top of the list.
  // Otherwise, just delete the item and don't append it.
  nsRect surfaceRect;

  for (nsDisplayItem* i : aList->TakeItems()) {
    if (i->GetType() == DisplayItemType::TYPE_CONTAINER) {
      aList->AppendToTop(i);
      surfaceRect.UnionRect(
          surfaceRect, ClipListToRange(aBuilder, i->GetChildren(), aRange));
      continue;
    }

    // itemToInsert indiciates the item that should be inserted into the
    // temporary list. If null, no item should be inserted.
    nsDisplayItem* itemToInsert = nullptr;
    nsIFrame* frame = i->Frame();
    nsIContent* content = frame->GetContent();
    if (content) {
      bool atStart =
          content == aRange->GetMayCrossShadowBoundaryStartContainer();
      bool atEnd = content == aRange->GetMayCrossShadowBoundaryEndContainer();
      if ((atStart || atEnd) && frame->IsTextFrame()) {
        auto [frameStartOffset, frameEndOffset] = frame->GetOffsets();

        int32_t highlightStart =
            atStart ? std::max(static_cast<int32_t>(
                                   aRange->MayCrossShadowBoundaryStartOffset()),
                               frameStartOffset)
                    : frameStartOffset;
        int32_t highlightEnd =
            atEnd ? std::min(static_cast<int32_t>(
                                 aRange->MayCrossShadowBoundaryEndOffset()),
                             frameEndOffset)
                  : frameEndOffset;
        if (highlightStart < highlightEnd) {
          // determine the location of the start and end edges of the range.
          nsPoint startPoint, endPoint;
          frame->GetPointFromOffset(highlightStart, &startPoint);
          frame->GetPointFromOffset(highlightEnd, &endPoint);

          // The clip rectangle is determined by taking the the start and
          // end points of the range, offset from the reference frame.
          // Because of rtl, the end point may be to the left of (or above,
          // in vertical mode) the start point, so x (or y) is set to the
          // lower of the values.
          nsRect textRect(aBuilder->ToReferenceFrame(frame), frame->GetSize());
          if (frame->GetWritingMode().IsVertical()) {
            nscoord y = std::min(startPoint.y, endPoint.y);
            textRect.y += y;
            textRect.height = std::max(startPoint.y, endPoint.y) - y;
          } else {
            nscoord x = std::min(startPoint.x, endPoint.x);
            textRect.x += x;
            textRect.width = std::max(startPoint.x, endPoint.x) - x;
          }
          surfaceRect.UnionRect(surfaceRect, textRect);

          const ActiveScrolledRoot* asr = i->GetActiveScrolledRoot();

          DisplayItemClip newClip;
          newClip.SetTo(textRect);

          const DisplayItemClipChain* newClipChain =
              aBuilder->AllocateDisplayItemClipChain(newClip, asr, nullptr);

          i->IntersectClip(aBuilder, newClipChain, true);
          itemToInsert = i;
        }
      }
      // Don't try to descend into subdocuments.
      // If this ever changes we'd need to add handling for subdocuments with
      // different zoom levels.
      else if (content->GetComposedDoc() ==
               aRange->GetMayCrossShadowBoundaryStartContainer()
                   ->GetComposedDoc()) {
        // if the node is within the range, append it to the temporary list
        bool before, after;
        nsresult rv =
            RangeUtils::CompareNodeToRange(content, aRange, &before, &after);
        if (NS_SUCCEEDED(rv) && !before && !after) {
          itemToInsert = i;
          bool snap;
          surfaceRect.UnionRect(surfaceRect, i->GetBounds(aBuilder, &snap));
        }
      }
    }

    // insert the item into the list if necessary. If the item has a child
    // list, insert that as well
    nsDisplayList* sublist = i->GetSameCoordinateSystemChildren();
    if (itemToInsert || sublist) {
      aList->AppendToTop(itemToInsert ? itemToInsert : i);
      // if the item is a list, iterate over it as well
      if (sublist) {
        surfaceRect.UnionRect(surfaceRect,
                              ClipListToRange(aBuilder, sublist, aRange));
      }
    } else {
      // otherwise, just delete the item and don't readd it to the list
      i->Destroy(aBuilder);
    }
  }

  return surfaceRect;
}

#ifdef DEBUG
#  include <stdio.h>

static bool gDumpRangePaintList = false;
#endif

UniquePtr<RangePaintInfo> PresShell::CreateRangePaintInfo(
    nsRange* aRange, nsRect& aSurfaceRect, bool aForPrimarySelection) {
  nsIFrame* ancestorFrame = nullptr;
  nsIFrame* rootFrame = GetRootFrame();

  // If the start or end of the range is the document, just use the root
  // frame, otherwise get the common ancestor of the two endpoints of the
  // range.
  nsINode* startContainer = aRange->GetMayCrossShadowBoundaryStartContainer();
  nsINode* endContainer = aRange->GetMayCrossShadowBoundaryEndContainer();
  Document* doc = startContainer->GetComposedDoc();
  if (startContainer == doc || endContainer == doc) {
    ancestorFrame = rootFrame;
  } else {
    nsINode* ancestor =
        StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()
            ? nsContentUtils::GetClosestCommonShadowIncludingInclusiveAncestor(
                  startContainer, endContainer)
            : nsContentUtils::GetClosestCommonInclusiveAncestor(startContainer,
                                                                endContainer);
    NS_ASSERTION(!ancestor || ancestor->IsContent(),
                 "common ancestor is not content");

    while (ancestor && ancestor->IsContent()) {
      ancestorFrame = ancestor->AsContent()->GetPrimaryFrame();
      if (ancestorFrame) {
        break;
      }

      ancestor = ancestor->GetParentOrShadowHostNode();
    }

    // use the nearest ancestor frame that includes all continuations as the
    // root for building the display list
    while (ancestorFrame &&
           nsLayoutUtils::GetNextContinuationOrIBSplitSibling(ancestorFrame)) {
      ancestorFrame = ancestorFrame->GetParent();
    }
  }

  if (!ancestorFrame) {
    return nullptr;
  }

  // get a display list containing the range
  auto info = MakeUnique<RangePaintInfo>(ancestorFrame);
  info->mBuilder.SetIncludeAllOutOfFlows();
  if (aForPrimarySelection) {
    info->mBuilder.SetSelectedFramesOnly();
  }
  info->mBuilder.EnterPresShell(ancestorFrame);

  ContentSubtreeIterator subtreeIter;
  nsresult rv = StaticPrefs::dom_shadowdom_selection_across_boundary_enabled()
                    ? subtreeIter.InitWithAllowCrossShadowBoundary(aRange)
                    : subtreeIter.Init(aRange);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  auto BuildDisplayListForNode = [&](nsINode* aNode) {
    if (MOZ_UNLIKELY(!aNode->IsContent())) {
      return;
    }
    nsIFrame* frame = aNode->AsContent()->GetPrimaryFrame();
    // XXX deal with frame being null due to display:contents
    for (; frame;
         frame = nsLayoutUtils::GetNextContinuationOrIBSplitSibling(frame)) {
      info->mBuilder.SetVisibleRect(frame->InkOverflowRect());
      info->mBuilder.SetDirtyRect(frame->InkOverflowRect());
      frame->BuildDisplayListForStackingContext(&info->mBuilder, &info->mList);
    }
  };
  if (startContainer->NodeType() == nsINode::TEXT_NODE) {
    BuildDisplayListForNode(startContainer);
  }
  for (; !subtreeIter.IsDone(); subtreeIter.Next()) {
    nsCOMPtr<nsINode> node = subtreeIter.GetCurrentNode();
    BuildDisplayListForNode(node);
  }
  if (endContainer != startContainer &&
      endContainer->NodeType() == nsINode::TEXT_NODE) {
    BuildDisplayListForNode(endContainer);
  }

  // If one of the ancestor presShells (including this one) has a resolution
  // set, we may have some APZ zoom applied. That means we may want to rasterize
  // the nodes at that zoom level. Populate `info` with the relevant information
  // so that the caller can decide what to do. Also wrap the display list in
  // appropriate nsDisplayAsyncZoom display items. This code handles the general
  // case with nested async zooms (even though that never actually happens),
  // because it fell out of the implementation for free.
  //
  // TODO: Do we need to do the same for ancestor transforms?
  for (nsPresContext* ctx = GetPresContext(); ctx;
       ctx = ctx->GetParentPresContext()) {
    PresShell* shell = ctx->PresShell();
    float resolution = shell->GetResolution();

    // If we are at the root document in the process, try to see if documents
    // in enclosing processes have a resolution and include that as well.
    if (!ctx->GetParentPresContext()) {
      // xScale is an arbitrary choice. Outside of edge cases involving CSS
      // transforms, xScale == yScale so it doesn't matter.
      resolution *= ViewportUtils::TryInferEnclosingResolution(shell).xScale;
    }

    if (resolution == 1.0) {
      continue;
    }

    info->mResolution *= resolution;
    nsIFrame* rootScrollContainerFrame = shell->GetRootScrollContainerFrame();
    ViewID zoomedId = nsLayoutUtils::FindOrCreateIDFor(
        rootScrollContainerFrame->GetContent());

    nsDisplayList wrapped(&info->mBuilder);
    wrapped.AppendNewToTop<nsDisplayAsyncZoom>(&info->mBuilder,
                                               rootScrollContainerFrame,
                                               &info->mList, nullptr, zoomedId);
    info->mList.AppendToTop(&wrapped);
  }

#ifdef DEBUG
  if (gDumpRangePaintList) {
    fprintf(stderr, "CreateRangePaintInfo --- before ClipListToRange:\n");
    nsIFrame::PrintDisplayList(&(info->mBuilder), info->mList);
  }
#endif

  nsRect rangeRect = ClipListToRange(&info->mBuilder, &info->mList, aRange);

  info->mBuilder.LeavePresShell(ancestorFrame, &info->mList);

#ifdef DEBUG
  if (gDumpRangePaintList) {
    fprintf(stderr, "CreateRangePaintInfo --- after ClipListToRange:\n");
    nsIFrame::PrintDisplayList(&(info->mBuilder), info->mList);
  }
#endif

  // determine the offset of the reference frame for the display list
  // to the root frame. This will allow the coordinates used when painting
  // to all be offset from the same point
  info->mRootOffset = ancestorFrame->GetBoundingClientRect().TopLeft();
  rangeRect.MoveBy(info->mRootOffset);
  aSurfaceRect.UnionRect(aSurfaceRect, rangeRect);

  return info;
}

already_AddRefed<SourceSurface> PresShell::PaintRangePaintInfo(
    const nsTArray<UniquePtr<RangePaintInfo>>& aItems, Selection* aSelection,
    const Maybe<CSSIntRegion>& aRegion, nsRect aArea,
    const LayoutDeviceIntPoint aPoint, LayoutDeviceIntRect* aScreenRect,
    RenderImageFlags aFlags) {
  nsPresContext* pc = GetPresContext();
  if (!pc || aArea.width == 0 || aArea.height == 0) {
    return nullptr;
  }

  // use the rectangle to create the surface
  LayoutDeviceIntRect pixelArea = LayoutDeviceIntRect::FromAppUnitsToOutside(
      aArea, pc->AppUnitsPerDevPixel());

  // if the image should not be resized, scale must be 1
  float scale = 1.0;

  const nsRect maxSize = pc->DeviceContext()->GetClientRect();

  // check if the image should be resized
  bool resize = !!(aFlags & RenderImageFlags::AutoScale);

  if (resize) {
    // check if image-resizing-algorithm should be used
    if (aFlags & RenderImageFlags::IsImage) {
      // get max screensize
      int32_t maxWidth = pc->AppUnitsToDevPixels(maxSize.width);
      int32_t maxHeight = pc->AppUnitsToDevPixels(maxSize.height);
      // resize image relative to the screensize
      // get best height/width relative to screensize
      float bestHeight = float(maxHeight) * RELATIVE_SCALEFACTOR;
      float bestWidth = float(maxWidth) * RELATIVE_SCALEFACTOR;
      // calculate scale for bestWidth
      float adjustedScale = bestWidth / float(pixelArea.width);
      // get the worst height (height when width is perfect)
      float worstHeight = float(pixelArea.height) * adjustedScale;
      // get the difference of best and worst height
      float difference = bestHeight - worstHeight;
      // halve the difference and add it to worstHeight to get
      // the best compromise between bestHeight and bestWidth,
      // then calculate the corresponding scale factor
      adjustedScale = (worstHeight + difference / 2) / float(pixelArea.height);
      // prevent upscaling
      scale = std::min(scale, adjustedScale);
    } else {
      // get half of max screensize
      int32_t maxWidth = pc->AppUnitsToDevPixels(maxSize.width >> 1);
      int32_t maxHeight = pc->AppUnitsToDevPixels(maxSize.height >> 1);
      if (pixelArea.width > maxWidth || pixelArea.height > maxHeight) {
        // divide the maximum size by the image size in both directions.
        // Whichever direction produces the smallest result determines how much
        // should be scaled.
        if (pixelArea.width > maxWidth) {
          scale = std::min(scale, float(maxWidth) / pixelArea.width);
        }
        if (pixelArea.height > maxHeight) {
          scale = std::min(scale, float(maxHeight) / pixelArea.height);
        }
      }
    }

    // Pick a resolution scale factor that is the highest we need for any of
    // the items. This means some items may get rendered at a higher-than-needed
    // resolution but at least nothing will be avoidably blurry.
    float resolutionScale = 1.0;
    for (const UniquePtr<RangePaintInfo>& rangeInfo : aItems) {
      resolutionScale = std::max(resolutionScale, rangeInfo->mResolution);
    }
    float unclampedResolution = resolutionScale;
    // Clamp the resolution scale so that `pixelArea` when scaled by `scale` and
    // `resolutionScale` isn't bigger than `maxSize`. This prevents creating
    // giant/unbounded images.
    resolutionScale =
        std::min(resolutionScale, maxSize.width / (scale * pixelArea.width));
    resolutionScale =
        std::min(resolutionScale, maxSize.height / (scale * pixelArea.height));
    // The following assert should only get hit if pixelArea scaled by `scale`
    // alone would already have been bigger than `maxSize`, which should never
    // be the case. For release builds we handle gracefully by reverting
    // resolutionScale to 1.0 to avoid unexpected consequences.
    MOZ_ASSERT(resolutionScale >= 1.0);
    resolutionScale = std::max(1.0f, resolutionScale);

    scale *= resolutionScale;

    // Now we need adjust the output screen position of the surface based on the
    // scaling factor and any APZ zoom that may be in effect. The goal is here
    // to set `aScreenRect`'s top-left corner (in screen-relative LD pixels)
    // such that the scaling effect on the surface appears anchored  at `aPoint`
    // ("anchor" here is like "transform-origin"). When this code is e.g. used
    // to generate a drag image for dragging operations, `aPoint` refers to the
    // position of the mouse cursor (also in screen-relative LD pixels), and the
    // user-visible effect of doing this is that the point at which the user
    // clicked to start the drag remains under the mouse during the drag.

    // In order to do this we first compute the top-left corner of the
    // pixelArea is screen-relative LD pixels.
    LayoutDevicePoint visualPoint = ViewportUtils::ToScreenRelativeVisual(
        LayoutDevicePoint(pixelArea.TopLeft()), pc);
    // And then adjust the output screen position based on that, which we can do
    // since everything here is screen-relative LD pixels. Note that the scale
    // factor we use here is the effective "transform" scale applied to the
    // content we're painting, relative to the scale at which it would normally
    // get painted at as part of page rendering (`unclampedResolution`).
    float scaleRelativeToNormalContent = scale / unclampedResolution;
    aScreenRect->x =
        NSToIntFloor(aPoint.x - float(aPoint.x.value - visualPoint.x.value) *
                                    scaleRelativeToNormalContent);
    aScreenRect->y =
        NSToIntFloor(aPoint.y - float(aPoint.y.value - visualPoint.y.value) *
                                    scaleRelativeToNormalContent);

    pixelArea.width = NSToIntFloor(float(pixelArea.width) * scale);
    pixelArea.height = NSToIntFloor(float(pixelArea.height) * scale);
    if (!pixelArea.width || !pixelArea.height) {
      return nullptr;
    }
  } else {
    // move aScreenRect to the position of the surface in screen coordinates
    LayoutDevicePoint visualPoint = ViewportUtils::ToScreenRelativeVisual(
        LayoutDevicePoint(pixelArea.TopLeft()), pc);
    aScreenRect->MoveTo(RoundedToInt(visualPoint));
  }
  aScreenRect->width = pixelArea.width;
  aScreenRect->height = pixelArea.height;

  RefPtr<DrawTarget> dt =
      gfxPlatform::GetPlatform()->CreateOffscreenContentDrawTarget(
          IntSize(pixelArea.width, pixelArea.height), SurfaceFormat::B8G8R8A8);
  if (!dt || !dt->IsValid()) {
    return nullptr;
  }

  gfxContext ctx(dt);

  if (aRegion) {
    RefPtr<PathBuilder> builder = dt->CreatePathBuilder(FillRule::FILL_WINDING);

    // Convert aRegion from CSS pixels to dev pixels
    nsIntRegion region = aRegion->ToAppUnits(AppUnitsPerCSSPixel())
                             .ToOutsidePixels(pc->AppUnitsPerDevPixel());
    for (auto iter = region.RectIter(); !iter.Done(); iter.Next()) {
      const IntRect& rect = iter.Get();

      builder->MoveTo(rect.TopLeft());
      builder->LineTo(rect.TopRight());
      builder->LineTo(rect.BottomRight());
      builder->LineTo(rect.BottomLeft());
      builder->LineTo(rect.TopLeft());
    }

    RefPtr<Path> path = builder->Finish();
    ctx.Clip(path);
  }

  gfxMatrix initialTM = ctx.CurrentMatrixDouble();

  if (resize) {
    initialTM.PreScale(scale, scale);
  }

  // translate so that points are relative to the surface area
  gfxPoint surfaceOffset = nsLayoutUtils::PointToGfxPoint(
      -aArea.TopLeft(), pc->AppUnitsPerDevPixel());
  initialTM.PreTranslate(surfaceOffset);

  // temporarily hide the selection so that text is drawn normally. If a
  // selection is being rendered, use that, otherwise use the presshell's
  // selection.
  RefPtr<nsFrameSelection> frameSelection;
  if (aSelection) {
    frameSelection = aSelection->GetFrameSelection();
  } else {
    frameSelection = FrameSelection();
  }
  int16_t oldDisplaySelection = frameSelection->GetDisplaySelection();
  frameSelection->SetDisplaySelection(nsISelectionController::SELECTION_HIDDEN);

  // next, paint each range in the selection
  for (const UniquePtr<RangePaintInfo>& rangeInfo : aItems) {
    // the display lists paint relative to the offset from the reference
    // frame, so account for that translation too:
    gfxPoint rootOffset = nsLayoutUtils::PointToGfxPoint(
        rangeInfo->mRootOffset, pc->AppUnitsPerDevPixel());
    ctx.SetMatrixDouble(initialTM.PreTranslate(rootOffset));
    aArea.MoveBy(-rangeInfo->mRootOffset.x, -rangeInfo->mRootOffset.y);
    nsRegion visible(aArea);
    rangeInfo->mList.PaintRoot(&rangeInfo->mBuilder, &ctx,
                               nsDisplayList::PAINT_DEFAULT, Nothing());
    aArea.MoveBy(rangeInfo->mRootOffset.x, rangeInfo->mRootOffset.y);
  }

  // restore the old selection display state
  frameSelection->SetDisplaySelection(oldDisplaySelection);

  return dt->Snapshot();
}

already_AddRefed<SourceSurface> PresShell::RenderNode(
    nsINode* aNode, const Maybe<CSSIntRegion>& aRegion,
    const LayoutDeviceIntPoint aPoint, LayoutDeviceIntRect* aScreenRect,
    RenderImageFlags aFlags) {
  // area will hold the size of the surface needed to draw the node, measured
  // from the root frame.
  nsRect area;
  nsTArray<UniquePtr<RangePaintInfo>> rangeItems;

  // nothing to draw if the node isn't in a document
  if (!aNode->IsInComposedDoc()) {
    return nullptr;
  }

  RefPtr<nsRange> range = nsRange::Create(aNode);
  IgnoredErrorResult rv;
  range->SelectNode(*aNode, rv);
  if (rv.Failed()) {
    return nullptr;
  }

  UniquePtr<RangePaintInfo> info = CreateRangePaintInfo(range, area, false);
  if (info) {
    // XXX(Bug 1631371) Check if this should use a fallible operation as it
    // pretended earlier, or change the return type to void.
    rangeItems.AppendElement(std::move(info));
  }

  Maybe<CSSIntRegion> region = aRegion;
  if (region) {
    // combine the area with the supplied region
    CSSIntRect rrectPixels = region->GetBounds();

    nsRect rrect = ToAppUnits(rrectPixels, AppUnitsPerCSSPixel());
    area.IntersectRect(area, rrect);

    nsPresContext* pc = GetPresContext();
    if (!pc) {
      return nullptr;
    }

    // move the region so that it is offset from the topleft corner of the
    // surface
    region->MoveBy(-nsPresContext::AppUnitsToIntCSSPixels(area.x),
                   -nsPresContext::AppUnitsToIntCSSPixels(area.y));
  }

  return PaintRangePaintInfo(rangeItems, nullptr, region, area, aPoint,
                             aScreenRect, aFlags);
}

already_AddRefed<SourceSurface> PresShell::RenderSelection(
    Selection* aSelection, const LayoutDeviceIntPoint aPoint,
    LayoutDeviceIntRect* aScreenRect, RenderImageFlags aFlags) {
  // area will hold the size of the surface needed to draw the selection,
  // measured from the root frame.
  nsRect area;
  nsTArray<UniquePtr<RangePaintInfo>> rangeItems;

  // iterate over each range and collect them into the rangeItems array.
  // This is done so that the size of selection can be determined so as
  // to allocate a surface area
  const uint32_t rangeCount = aSelection->RangeCount();
  NS_ASSERTION(rangeCount > 0, "RenderSelection called with no selection");
  for (const uint32_t r : IntegerRange(rangeCount)) {
    MOZ_ASSERT(aSelection->RangeCount() == rangeCount);
    RefPtr<nsRange> range = aSelection->GetRangeAt(r);

    UniquePtr<RangePaintInfo> info = CreateRangePaintInfo(range, area, true);
    if (info) {
      // XXX(Bug 1631371) Check if this should use a fallible operation as it
      // pretended earlier.
      rangeItems.AppendElement(std::move(info));
    }
  }

  return PaintRangePaintInfo(rangeItems, aSelection, Nothing(), area, aPoint,
                             aScreenRect, aFlags);
}

static void AddDisplayItemToBottom(nsDisplayListBuilder* aBuilder,
                                   nsDisplayList* aList, nsDisplayItem* aItem) {
  nsDisplayList list(aBuilder);
  list.AppendToTop(aItem);
  list.AppendToTop(aList);
  aList->AppendToTop(&list);
}

void PresShell::AddCanvasBackgroundColorItem(nsDisplayListBuilder* aBuilder,
                                             nsDisplayList* aList,
                                             nsIFrame* aFrame,
                                             const nsRect& aBounds,
                                             nscolor aBackstopColor) {
  if (aBounds.IsEmpty()) {
    return;
  }
  const bool isViewport = aFrame->IsViewportFrame();
  const SingleCanvasBackground* canvasBg;
  if (isViewport) {
    canvasBg = &mCanvasBackground.mViewport;
  } else if (aFrame->IsPageContentFrame()) {
    canvasBg = &mCanvasBackground.mPage;
  } else {
    // We don't want to add an item for the canvas background color if the frame
    // (sub)tree we are painting doesn't include any canvas frames.
    return;
  }

  const nscolor bgcolor = NS_ComposeColors(aBackstopColor, canvasBg->mColor);
  if (NS_GET_A(bgcolor) == 0) {
    return;
  }

  // With async scrolling, we'd like to have two instances of the background
  // color: one that scrolls with the content and one underneath which does not
  // scroll with the content, but which can be shown during checkerboarding and
  // overscroll and the dynamic toolbar movement. We can only do that if the
  // color is opaque.
  //
  // We also need to paint the background if CSS hasn't specified it (since
  // otherwise nsCanvasFrame might not paint it). Note that non-CSS-specified
  // backgrounds shouldn't ever be semi-transparent.
  const bool forceUnscrolledItem =
      nsLayoutUtils::UsesAsyncScrolling(aFrame) && NS_GET_A(bgcolor) == 255;
  if (!canvasBg->mCSSSpecified || forceUnscrolledItem) {
    MOZ_ASSERT(NS_GET_A(bgcolor) == 255);
    const bool isRootContentDocumentCrossProcess =
        mPresContext->IsRootContentDocumentCrossProcess();
    MOZ_ASSERT_IF(
        isViewport && isRootContentDocumentCrossProcess &&
            mPresContext->HasDynamicToolbar(),
        aBounds.Size() ==
            nsLayoutUtils::ExpandHeightForDynamicToolbar(
                mPresContext, aFrame->InkOverflowRectRelativeToSelf().Size()));

    nsDisplaySolidColor* item = MakeDisplayItem<nsDisplaySolidColor>(
        aBuilder, aFrame, aBounds, bgcolor);
    if (canvasBg->mCSSSpecified && isRootContentDocumentCrossProcess) {
      item->SetIsCheckerboardBackground();
    }
    AddDisplayItemToBottom(aBuilder, aList, item);
  }
}

bool PresShell::IsTransparentContainerElement() const {
  if (mDocument->IsInitialDocument()) {
    switch (StaticPrefs::layout_css_initial_document_transparency()) {
      case 3:
        return true;
      case 2:
        if (!mDocument->IsTopLevelContentDocument()) {
          return true;
        }
        [[fallthrough]];
      case 1:
        if (mDocument->IsLikelyContentInaccessibleTopLevelAboutBlank()) {
          return true;
        }
        [[fallthrough]];
      default:
        break;
    }
  }

  nsPresContext* pc = GetPresContext();
  if (!pc->IsRootContentDocumentCrossProcess()) {
    if (mDocument->IsInChromeDocShell()) {
      return true;
    }
    // Frames are transparent except if their used embedder color-scheme is
    // mismatched, in which case we use an opaque background to avoid
    // black-on-black or white-on-white text, see
    // https://github.com/w3c/csswg-drafts/issues/4772
    if (BrowsingContext* bc = mDocument->GetBrowsingContext()) {
      switch (bc->GetEmbedderColorSchemes().mUsed) {
        case dom::PrefersColorSchemeOverride::Light:
          return pc->DefaultBackgroundColorScheme() == ColorScheme::Light;
        case dom::PrefersColorSchemeOverride::Dark:
          return pc->DefaultBackgroundColorScheme() == ColorScheme::Dark;
        case dom::PrefersColorSchemeOverride::None:
          break;
      }
    }
    return true;
  }

  nsIDocShell* docShell = pc->GetDocShell();
  if (!docShell) {
    return false;
  }
  nsPIDOMWindowOuter* pwin = docShell->GetWindow();
  if (!pwin) {
    return false;
  }
  if (Element* containerElement = pwin->GetFrameElementInternal()) {
    return containerElement->HasAttr(nsGkAtoms::transparent);
  }
  if (BrowserChild* tab = BrowserChild::GetFrom(docShell)) {
    // Check if presShell is the top PresShell. Only the top can influence the
    // canvas background color.
    return this == tab->GetTopLevelPresShell() && tab->IsTransparent();
  }
  return false;
}

nscolor PresShell::GetDefaultBackgroundColorToDraw() const {
  if (!mPresContext) {
    return NS_RGB(255, 255, 255);
  }
  return mPresContext->DefaultBackgroundColor();
}

void PresShell::UpdateCanvasBackground() {
  mCanvasBackground = ComputeCanvasBackground();
}

static SingleCanvasBackground ComputeSingleCanvasBackground(nsIFrame* aCanvas) {
  MOZ_ASSERT(aCanvas->IsCanvasFrame());
  const nsIFrame* bgFrame = nsCSSRendering::FindBackgroundFrame(aCanvas);
  nscolor color = NS_RGBA(0, 0, 0, 0);
  bool drawBackgroundImage = false;
  bool drawBackgroundColor = false;
  if (!bgFrame->IsThemed()) {
    // Ignore the CSS background-color if -moz-appearance is used.
    color = nsCSSRendering::DetermineBackgroundColor(
        aCanvas->PresContext(), bgFrame->Style(), aCanvas, drawBackgroundImage,
        drawBackgroundColor);
  }
  return {color, drawBackgroundColor};
}

PresShell::CanvasBackground PresShell::ComputeCanvasBackground() const {
  // If we have a frame tree and it has style information that
  // specifies the background color of the canvas, update our local
  // cache of that color.
  nsIFrame* canvas = GetCanvasFrame();
  if (!canvas) {
    nscolor color = GetDefaultBackgroundColorToDraw();
    // If the root element of the document (ie html) has style 'display: none'
    // then the document's background color does not get drawn; return the color
    // we actually draw.
    return {{color, false}, {color, false}};
  }

  auto viewportBg = ComputeSingleCanvasBackground(canvas);
  if (!IsTransparentContainerElement()) {
    viewportBg.mColor =
        NS_ComposeColors(GetDefaultBackgroundColorToDraw(), viewportBg.mColor);
  }
  auto pageBg = viewportBg;
  nsCanvasFrame* docElementCb =
      mFrameConstructor->GetDocElementContainingBlock();
  if (canvas != docElementCb) {
    // We're in paged mode / print / print-preview, and just computed the "root"
    // canvas background. Compute the doc element containing block background
    // too.
    MOZ_ASSERT(mPresContext->IsRootPaginatedDocument());
    pageBg = ComputeSingleCanvasBackground(docElementCb);
  }
  return {viewportBg, pageBg};
}

nscolor PresShell::ComputeBackstopColor(nsView* aDisplayRoot) {
  nsIWidget* widget = aDisplayRoot->GetWidget();
  if (widget &&
      (widget->GetTransparencyMode() != widget::TransparencyMode::Opaque ||
       widget->WidgetPaintsBackground())) {
    // Within a transparent widget, so the backstop color must be
    // totally transparent.
    return NS_RGBA(0, 0, 0, 0);
  }
  // Within an opaque widget (or no widget at all), so the backstop
  // color must be totally opaque. The user's default background
  // as reported by the prescontext is guaranteed to be opaque.
  return GetDefaultBackgroundColorToDraw();
}

struct PaintParams {
  nscolor mBackgroundColor;
};

WindowRenderer* PresShell::GetWindowRenderer() {
  NS_ASSERTION(mViewManager, "Should have view manager");

  nsView* rootView = mViewManager->GetRootView();
  if (rootView) {
    if (nsIWidget* widget = rootView->GetWidget()) {
      return widget->GetWindowRenderer();
    }
  }
  return nullptr;
}

bool PresShell::AsyncPanZoomEnabled() {
  NS_ASSERTION(mViewManager, "Should have view manager");
  nsView* rootView = mViewManager->GetRootView();
  if (rootView) {
    if (nsIWidget* widget = rootView->GetWidget()) {
      return widget->AsyncPanZoomEnabled();
    }
  }
  return gfxPlatform::AsyncPanZoomEnabled();
}

nsresult PresShell::SetResolutionAndScaleTo(float aResolution,
                                            ResolutionChangeOrigin aOrigin) {
  if (!(aResolution > 0.0)) {
    return NS_ERROR_ILLEGAL_VALUE;
  }
  if (aResolution == mResolution.valueOr(0.0)) {
    MOZ_ASSERT(mResolution.isSome());
    return NS_OK;
  }

  // GetResolution handles mResolution being nothing by returning 1 so this
  // is checking that the resolution is actually changing.
  bool resolutionUpdated = aResolution != GetResolution();

  mLastResolutionChangeOrigin = aOrigin;

  RenderingState state(this);
  state.mResolution = Some(aResolution);
  SetRenderingState(state);
  if (mMobileViewportManager) {
    mMobileViewportManager->ResolutionUpdated(aOrigin);
  }
  // Changing the resolution changes the visual viewport size which may
  // make the current visual viewport offset out-of-bounds (if the size
  // increased). APZ will reconcile this by sending a clamped visual
  // viewport offset on the next repaint, but to avoid main-thread code
  // observing an out-of-bounds offset until then, reclamp it here.
  if (IsVisualViewportOffsetSet()) {
    SetVisualViewportOffset(GetVisualViewportOffset(),
                            GetLayoutViewportOffset());
  }
  if (aOrigin == ResolutionChangeOrigin::Apz) {
    mResolutionUpdatedByApz = true;
  } else if (resolutionUpdated) {
    mResolutionUpdated = true;
  }

  if (auto* window = nsGlobalWindowInner::Cast(mDocument->GetInnerWindow())) {
    window->VisualViewport()->PostResizeEvent();
  }

  return NS_OK;
}

float PresShell::GetCumulativeResolution() const {
  float resolution = GetResolution();
  nsPresContext* parentCtx = GetPresContext()->GetParentPresContext();
  if (parentCtx) {
    resolution *= parentCtx->PresShell()->GetCumulativeResolution();
  }
  return resolution;
}

void PresShell::SetRestoreResolution(float aResolution,
                                     LayoutDeviceIntSize aDisplaySize) {
  if (mMobileViewportManager) {
    mMobileViewportManager->SetRestoreResolution(aResolution, aDisplaySize);
  }
}

void PresShell::SetRenderingState(const RenderingState& aState) {
  if (GetResolution() != aState.mResolution.valueOr(1.f)) {
    if (nsIFrame* frame = GetRootFrame()) {
      frame->SchedulePaint();
    }
  }

  mRenderingStateFlags = aState.mRenderingStateFlags;
  mResolution = aState.mResolution;
#ifdef ACCESSIBILITY
  if (nsAccessibilityService* accService = GetAccService()) {
    accService->NotifyOfResolutionChange(this, GetResolution());
  }
#endif
}

void PresShell::SynthesizeMouseMove(bool aFromScroll) {
  if (!StaticPrefs::layout_reflow_synthMouseMove()) {
    return;
  }

  if (mPaintingSuppressed || !mIsActive || !mPresContext) {
    return;
  }

  if (!mPresContext->IsRoot()) {
    if (PresShell* rootPresShell = GetRootPresShell()) {
      rootPresShell->SynthesizeMouseMove(aFromScroll);
    }
    return;
  }

  if (mMouseLocation == nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE)) {
    return;
  }

  if (!mSynthMouseMoveEvent.IsPending()) {
    auto ev = MakeRefPtr<nsSynthMouseMoveEvent>(this, aFromScroll);

    GetPresContext()->RefreshDriver()->AddRefreshObserver(
        ev, FlushType::Display, "Synthetic mouse move event");
    mSynthMouseMoveEvent = std::move(ev);
  }
}

static nsView* FindFloatingViewContaining(nsPresContext* aRootPresContext,
                                          nsIWidget* aRootWidget,
                                          const LayoutDeviceIntPoint& aPt) {
  nsIFrame* popupFrame = nsLayoutUtils::GetPopupFrameForPoint(
      aRootPresContext, aRootWidget, aPt,
      nsLayoutUtils::GetPopupFrameForPointFlags::OnlyReturnFramesWithWidgets);
  return popupFrame ? popupFrame->GetView() : nullptr;
}

/*
 * This finds the first view with a frame that contains the given point in a
 * postorder traversal of the view tree, assuming that the point is not in a
 * floating view.  It assumes that only floating views extend outside the bounds
 * of their parents.
 *
 * This methods should only be called if FindFloatingViewContaining returns
 * null.
 *
 * aPt is relative aRelativeToView with the viewport type
 * aRelativeToViewportType. aRelativeToView will always have a frame. If aView
 * has a frame then aRelativeToView will be aView. (The reason aRelativeToView
 * and aView are separate is because we need to traverse into views without
 * frames (ie the inner view of a subdocument frame) but we can only easily
 * transform between views using TransformPoint which takes frames.)
 */
static nsView* FindViewContaining(nsView* aRelativeToView,
                                  ViewportType aRelativeToViewportType,
                                  nsView* aView, nsPoint aPt) {
  MOZ_ASSERT(aRelativeToView->GetFrame());

  if (aView->GetVisibility() == ViewVisibility::Hide) {
    return nullptr;
  }

  nsIFrame* frame = aView->GetFrame();
  if (frame) {
    if (!frame->PresShell()->IsActive() ||
        !frame->IsVisibleConsideringAncestors(
            nsIFrame::VISIBILITY_CROSS_CHROME_CONTENT_BOUNDARY)) {
      return nullptr;
    }

    // We start out in visual coords and then if we cross the zoom boundary we
    // become in layout coords. The zoom boundary always occurs in a document
    // with IsRootContentDocumentCrossProcess. The root view of such a document
    // is outside the zoom boundary and any child view must be inside the zoom
    // boundary because we only create views for certain kinds of frames and
    // none of them can be between the root frame and the zoom boundary.
    bool crossingZoomBoundary = false;
    if (aRelativeToViewportType == ViewportType::Visual) {
      if (!aRelativeToView->GetParent() ||
          aRelativeToView->GetViewManager() !=
              aRelativeToView->GetParent()->GetViewManager()) {
        if (aRelativeToView->GetFrame()
                ->PresContext()
                ->IsRootContentDocumentCrossProcess()) {
          crossingZoomBoundary = true;
        }
      }
    }

    ViewportType nextRelativeToViewportType = aRelativeToViewportType;
    if (crossingZoomBoundary) {
      nextRelativeToViewportType = ViewportType::Layout;
    }

    nsLayoutUtils::TransformResult result = nsLayoutUtils::TransformPoint(
        RelativeTo{aRelativeToView->GetFrame(), aRelativeToViewportType},
        RelativeTo{frame, nextRelativeToViewportType}, aPt);
    if (result != nsLayoutUtils::TRANSFORM_SUCCEEDED) {
      return nullptr;
    }

    // Even though aPt is in visual coordinates until we cross the zoom boundary
    // it is valid to compare it to view coords (which are in layout coords)
    // because visual coords are the same as layout coords for every view
    // outside of the zoom boundary except for the root view of the root content
    // document.
    // For the root view of the root content document, its bounds don't
    // actually correspond to what is visible when we have a
    // MobileViewportManager. So we skip the hit test. This is okay because the
    // point has already been hit test: 1) if we are the root view in the
    // process then the point comes from a real mouse event so it must have been
    // over our widget, or 2) if we are the root of a subdocument then
    // hittesting against the view of the subdocument frame that contains us
    // already happened and succeeded before getting here.
    if (!crossingZoomBoundary) {
      if (!aView->GetDimensions().Contains(aPt)) {
        return nullptr;
      }
    }

    aRelativeToView = aView;
    aRelativeToViewportType = nextRelativeToViewportType;
  }

  for (nsView* v = aView->GetFirstChild(); v; v = v->GetNextSibling()) {
    nsView* r =
        FindViewContaining(aRelativeToView, aRelativeToViewportType, v, aPt);
    if (r) {
      return r;
    }
  }

  return frame ? aView : nullptr;
}

static BrowserBridgeChild* GetChildBrowser(nsView* aView) {
  if (!aView) {
    return nullptr;
  }
  nsIFrame* frame = aView->GetFrame();
  if (!frame && aView->GetParent()) {
    // If frame is null then view is an anonymous inner view, and we want
    // the frame from the corresponding outer view.
    frame = aView->GetParent()->GetFrame();
  }
  if (!frame || !frame->GetContent()) {
    return nullptr;
  }
  return BrowserBridgeChild::GetFrom(frame->GetContent());
}

void PresShell::ProcessSynthMouseMoveEvent(bool aFromScroll) {
  // If drag session has started, we shouldn't synthesize mousemove event.
  nsView* rootView = mViewManager ? mViewManager->GetRootView() : nullptr;
  if (!rootView || !rootView->HasWidget()) {
    mSynthMouseMoveEvent.Forget();
    return;
  }
  nsCOMPtr<nsIDragSession> dragSession =
      nsContentUtils::GetDragSession(rootView->GetWidget());
  if (dragSession) {
    // Don't forget it.  We need to synthesize a mouse move when the drag
    // session ends.
    return;
  }

  // allow new event to be posted while handling this one only if the
  // source of the event is a scroll (to prevent infinite reflow loops)
  if (aFromScroll) {
    mSynthMouseMoveEvent.Forget();
  }

  if (mMouseLocation == nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE) ||
      !mPresContext) {
    mSynthMouseMoveEvent.Forget();
    return;
  }

  NS_ASSERTION(mPresContext->IsRoot(), "Only a root pres shell should be here");

  // Hold a ref to ourselves so DispatchEvent won't destroy us (since
  // we need to access members after we call DispatchEvent).
  RefPtr<PresShell> kungFuDeathGrip(this);

#ifdef DEBUG
  MOZ_LOG(gLogMouseLocation, LogLevel::Info,
          ("[ps=%p]synthesizing mouse move to (%d,%d)\n", this,
           mMouseLocation.x, mMouseLocation.y));
#endif

  int32_t APD = mPresContext->AppUnitsPerDevPixel();

  // We need a widget to put in the event we are going to dispatch so we look
  // for a view that has a widget and the mouse location is over. We first look
  // for floating views, if there isn't one we use the root view. |view| holds
  // that view.
  nsView* view = nullptr;

  // The appunits per devpixel ratio of |view|.
  int32_t viewAPD;

  // mRefPoint will be mMouseLocation relative to the widget of |view|, the
  // widget we will put in the event we dispatch, in viewAPD appunits
  nsPoint refpoint(0, 0);

  // We always dispatch the event to the pres shell that contains the view that
  // the mouse is over. pointVM is the VM of that pres shell.
  nsViewManager* pointVM = nullptr;

  if (rootView->GetFrame()) {
    view = FindFloatingViewContaining(
        mPresContext, rootView->GetWidget(),
        LayoutDeviceIntPoint::FromAppUnitsToNearest(
            mMouseLocation + rootView->ViewToWidgetOffset(), APD));
  }

  nsView* pointView = view;
  if (!view) {
    view = rootView;
    if (rootView->GetFrame()) {
      pointView = FindViewContaining(rootView, ViewportType::Visual, rootView,
                                     mMouseLocation);
    } else {
      pointView = rootView;
    }
    // pointView can be null in situations related to mouse capture
    pointVM = (pointView ? pointView : view)->GetViewManager();
    refpoint = mMouseLocation + rootView->ViewToWidgetOffset();
    viewAPD = APD;
  } else {
    pointVM = view->GetViewManager();
    nsIFrame* frame = view->GetFrame();
    NS_ASSERTION(frame, "floating views can't be anonymous");
    viewAPD = frame->PresContext()->AppUnitsPerDevPixel();
    refpoint = mMouseLocation;
    DebugOnly<nsLayoutUtils::TransformResult> result =
        nsLayoutUtils::TransformPoint(
            RelativeTo{rootView->GetFrame(), ViewportType::Visual},
            RelativeTo{frame, ViewportType::Layout}, refpoint);
    MOZ_ASSERT(result == nsLayoutUtils::TRANSFORM_SUCCEEDED);
    refpoint += view->ViewToWidgetOffset();
  }
  NS_ASSERTION(view->GetWidget(), "view should have a widget here");
  WidgetMouseEvent event(true, eMouseMove, view->GetWidget(),
                         WidgetMouseEvent::eSynthesized);

  // If the last cursor location was set by a synthesized mouse event for tests,
  // running test should expect a restyle or a DOM mutation under the cursor may
  // cause mouse boundary events in a remote process if the cursor is over a
  // remote content.  Therefore, the events should not be ignored by
  // PresShell::HandleEvent in the remote process.  So we need to mark the
  // synthesized eMouseMove as "synthesized for tests".
  event.mFlags.mIsSynthesizedForTests =
      mMouseLocationWasSetBySynthesizedMouseEventForTests;

  event.mRefPoint =
      LayoutDeviceIntPoint::FromAppUnitsToNearest(refpoint, viewAPD);
  event.mButtons = PresShell::sMouseButtons;
  // XXX set event.mModifiers ?
  // XXX mnakano I think that we should get the latest information from widget.

  if (BrowserBridgeChild* bbc = GetChildBrowser(pointView)) {
    // If we have a BrowserBridgeChild, we're going to be dispatching this
    // mouse event into an OOP iframe of the current document.
    event.mLayersId = bbc->GetLayersId();
    bbc->SendDispatchSynthesizedMouseEvent(event);
  } else if (RefPtr<PresShell> presShell = pointVM->GetPresShell()) {
    // Since this gets run in a refresh tick there isn't an InputAPZContext on
    // the stack from the nsBaseWidget. We need to simulate one with at least
    // the correct target guid, so that the correct callback transform gets
    // applied if this event goes to a child process. The input block id is set
    // to 0 because this is a synthetic event which doesn't really belong to any
    // input block. Same for the APZ response field.
    InputAPZContext apzContext(mMouseEventTargetGuid, 0, nsEventStatus_eIgnore);
    presShell->DispatchSynthMouseMove(&event);
  }

  if (!aFromScroll) {
    mSynthMouseMoveEvent.Forget();
  }
}

/* static */
void PresShell::MarkFramesInListApproximatelyVisible(
    const nsDisplayList& aList) {
  for (nsDisplayItem* item : aList) {
    nsDisplayList* sublist = item->GetChildren();
    if (sublist) {
      MarkFramesInListApproximatelyVisible(*sublist);
      continue;
    }

    nsIFrame* frame = item->Frame();
    MOZ_ASSERT(frame);

    if (!frame->TrackingVisibility()) {
      continue;
    }

    // Use the presshell containing the frame.
    PresShell* presShell = frame->PresShell();
    MOZ_ASSERT(!presShell->AssumeAllFramesVisible());
    if (presShell->mApproximatelyVisibleFrames.EnsureInserted(frame)) {
      // The frame was added to mApproximatelyVisibleFrames, so increment its
      // visible count.
      frame->IncApproximateVisibleCount();
    }
  }
}

/* static */
void PresShell::DecApproximateVisibleCount(
    VisibleFrames& aFrames, const Maybe<OnNonvisible>& aNonvisibleAction
    /* = Nothing() */) {
  for (nsIFrame* frame : aFrames) {
    // Decrement the frame's visible count if we're still tracking its
    // visibility. (We may not be, if the frame disabled visibility tracking
    // after we added it to the visible frames list.)
    if (frame->TrackingVisibility()) {
      frame->DecApproximateVisibleCount(aNonvisibleAction);
    }
  }
}

void PresShell::RebuildApproximateFrameVisibilityDisplayList(
    const nsDisplayList& aList) {
  MOZ_ASSERT(!mApproximateFrameVisibilityVisited, "already visited?");
  mApproximateFrameVisibilityVisited = true;

  // Remove the entries of the mApproximatelyVisibleFrames hashtable and put
  // them in oldApproxVisibleFrames.
  VisibleFrames oldApproximatelyVisibleFrames =
      std::move(mApproximatelyVisibleFrames);

  MarkFramesInListApproximatelyVisible(aList);

  DecApproximateVisibleCount(oldApproximatelyVisibleFrames);
}

/* static */
void PresShell::ClearApproximateFrameVisibilityVisited(nsView* aView,
                                                       bool aClear) {
  nsViewManager* vm = aView->GetViewManager();
  if (aClear) {
    PresShell* presShell = vm->GetPresShell();
    if (!presShell->mApproximateFrameVisibilityVisited) {
      presShell->ClearApproximatelyVisibleFramesList();
    }
    presShell->mApproximateFrameVisibilityVisited = false;
  }
  for (nsView* v = aView->GetFirstChild(); v; v = v->GetNextSibling()) {
    ClearApproximateFrameVisibilityVisited(v, v->GetViewManager() != vm);
  }
}

void PresShell::ClearApproximatelyVisibleFramesList(
    const Maybe<OnNonvisible>& aNonvisibleAction
    /* = Nothing() */) {
  DecApproximateVisibleCount(mApproximatelyVisibleFrames, aNonvisibleAction);
  mApproximatelyVisibleFrames.Clear();
}

void PresShell::MarkFramesInSubtreeApproximatelyVisible(
    nsIFrame* aFrame, const nsRect& aRect, bool aRemoveOnly /* = false */) {
  MOZ_DIAGNOSTIC_ASSERT(aFrame, "aFrame arg should be a valid frame pointer");
  MOZ_ASSERT(aFrame->PresShell() == this, "wrong presshell");

  if (aFrame->TrackingVisibility() && aFrame->StyleVisibility()->IsVisible() &&
      (!aRemoveOnly ||
       aFrame->GetVisibility() == Visibility::ApproximatelyVisible)) {
    MOZ_ASSERT(!AssumeAllFramesVisible());
    if (mApproximatelyVisibleFrames.EnsureInserted(aFrame)) {
      // The frame was added to mApproximatelyVisibleFrames, so increment its
      // visible count.
      aFrame->IncApproximateVisibleCount();
    }
  }

  nsSubDocumentFrame* subdocFrame = do_QueryFrame(aFrame);
  if (subdocFrame) {
    PresShell* presShell = subdocFrame->GetSubdocumentPresShellForPainting(
        nsSubDocumentFrame::IGNORE_PAINT_SUPPRESSION);
    if (presShell && !presShell->AssumeAllFramesVisible()) {
      nsRect rect = aRect;
      nsIFrame* root = presShell->GetRootFrame();
      if (root) {
        rect.MoveBy(aFrame->GetOffsetToCrossDoc(root));
      } else {
        rect.MoveBy(-aFrame->GetContentRectRelativeToSelf().TopLeft());
      }
      rect = rect.ScaleToOtherAppUnitsRoundOut(
          aFrame->PresContext()->AppUnitsPerDevPixel(),
          presShell->GetPresContext()->AppUnitsPerDevPixel());

      presShell->RebuildApproximateFrameVisibility(&rect);
    }
    return;
  }

  nsRect rect = aRect;

  if (ScrollContainerFrame* scrollFrame = do_QueryFrame(aFrame)) {
    bool ignoreDisplayPort = false;
    if (DisplayPortUtils::IsMissingDisplayPortBaseRect(aFrame->GetContent())) {
      // We can properly set the base rect for root scroll frames on top level
      // and root content documents. Otherwise the base rect we compute might
      // be way too big without the limiting that
      // ScrollContainerFrame::DecideScrollableLayer does, so we just ignore the
      // displayport in that case.
      nsPresContext* pc = aFrame->PresContext();
      if (scrollFrame->IsRootScrollFrameOfDocument() &&
          (pc->IsRootContentDocumentCrossProcess() ||
           (pc->IsChrome() && !pc->GetParentPresContext()))) {
        nsRect baseRect(
            nsPoint(), nsLayoutUtils::CalculateCompositionSizeForFrame(aFrame));
        DisplayPortUtils::SetDisplayPortBase(aFrame->GetContent(), baseRect);
      } else {
        ignoreDisplayPort = true;
      }
    }

    nsRect displayPort;
    bool usingDisplayport =
        !ignoreDisplayPort &&
        DisplayPortUtils::GetDisplayPortForVisibilityTesting(
            aFrame->GetContent(), &displayPort);

    scrollFrame->NotifyApproximateFrameVisibilityUpdate(!usingDisplayport);

    if (usingDisplayport) {
      rect = displayPort;
    } else {
      rect = rect.Intersect(scrollFrame->GetScrollPortRect());
    }
    rect = scrollFrame->ExpandRectToNearlyVisible(rect);
  }

  bool preserves3DChildren = aFrame->Extend3DContext();

  for (const auto& [list, listID] : aFrame->ChildLists()) {
    for (nsIFrame* child : list) {
      // Note: This assert should be trivially satisfied, just by virtue of how
      // nsFrameList and its iterator works (with nullptr being an end-of-list
      // sentinel which should terminate the loop).  But we do somehow get
      // crash reports inside this loop that suggest `child` is null...
      MOZ_DIAGNOSTIC_ASSERT(child, "shouldn't have null values in child lists");
      nsRect r = rect - child->GetPosition();
      if (!r.IntersectRect(r, child->InkOverflowRect())) {
        continue;
      }
      if (child->IsTransformed()) {
        // for children of a preserve3d element we just pass down the same dirty
        // rect
        if (!preserves3DChildren ||
            !child->Combines3DTransformWithAncestors()) {
          const nsRect overflow = child->InkOverflowRectRelativeToSelf();
          nsRect out;
          if (nsDisplayTransform::UntransformRect(r, overflow, child, &out)) {
            r = out;
          } else {
            r.SetEmpty();
          }
        }
      }
      MarkFramesInSubtreeApproximatelyVisible(child, r, aRemoveOnly);
    }
  }
}

void PresShell::RebuildApproximateFrameVisibility(
    nsRect* aRect, bool aRemoveOnly /* = false */) {
  MOZ_ASSERT(!mApproximateFrameVisibilityVisited, "already visited?");
  mApproximateFrameVisibilityVisited = true;

  nsIFrame* rootFrame = GetRootFrame();
  if (!rootFrame) {
    return;
  }

  // Remove the entries of the mApproximatelyVisibleFrames hashtable and put
  // them in oldApproximatelyVisibleFrames.
  VisibleFrames oldApproximatelyVisibleFrames =
      std::move(mApproximatelyVisibleFrames);

  nsRect vis(nsPoint(0, 0), rootFrame->GetSize());
  if (aRect) {
    vis = *aRect;
  }

  // If we are in-process root but not the top level content, we need to take
  // the intersection with the iframe visible rect.
  if (mPresContext->IsRootContentDocumentInProcess() &&
      !mPresContext->IsRootContentDocumentCrossProcess()) {
    // There are two possibilities that we can't get the iframe's visible
    // rect other than the iframe is out side of ancestors' display ports.
    // a) the BrowserChild is being torn down
    // b) the visible rect hasn't been delivered the BrowserChild
    // In both cases we consider the visible rect is empty.
    Maybe<nsRect> visibleRect;
    if (BrowserChild* browserChild = BrowserChild::GetFrom(this)) {
      visibleRect = browserChild->GetVisibleRect();
    }
    vis = vis.Intersect(visibleRect.valueOr(nsRect()));
  }

  MarkFramesInSubtreeApproximatelyVisible(rootFrame, vis, aRemoveOnly);

  DecApproximateVisibleCount(oldApproximatelyVisibleFrames);
}

void PresShell::UpdateApproximateFrameVisibility() {
  DoUpdateApproximateFrameVisibility(/* aRemoveOnly = */ false);
}

void PresShell::DoUpdateApproximateFrameVisibility(bool aRemoveOnly) {
  MOZ_ASSERT(
      !mPresContext || mPresContext->IsRootContentDocumentInProcess(),
      "Updating approximate frame visibility on a non-root content document?");

  mUpdateApproximateFrameVisibilityEvent.Revoke();

  if (mHaveShutDown || mIsDestroying) {
    return;
  }

  // call update on that frame
  nsIFrame* rootFrame = GetRootFrame();
  if (!rootFrame) {
    ClearApproximatelyVisibleFramesList(Some(OnNonvisible::DiscardImages));
    return;
  }

  RebuildApproximateFrameVisibility(/* aRect = */ nullptr, aRemoveOnly);
  ClearApproximateFrameVisibilityVisited(rootFrame->GetView(), true);

#ifdef DEBUG_FRAME_VISIBILITY_DISPLAY_LIST
  // This can be used to debug the frame walker by comparing beforeFrameList
  // and mApproximatelyVisibleFrames in RebuildFrameVisibilityDisplayList to see
  // if they produce the same results (mApproximatelyVisibleFrames holds the
  // frames the display list thinks are visible, beforeFrameList holds the
  // frames the frame walker thinks are visible).
  nsDisplayListBuilder builder(
      rootFrame, nsDisplayListBuilderMode::FRAME_VISIBILITY, false);
  nsRect updateRect(nsPoint(0, 0), rootFrame->GetSize());
  nsIFrame* rootScroll = GetRootScrollFrame();
  if (rootScroll) {
    nsIContent* content = rootScroll->GetContent();
    if (content) {
      Unused << nsLayoutUtils::GetDisplayPortForVisibilityTesting(
          content, &updateRect, RelativeTo::ScrollFrame);
    }

    if (IgnoringViewportScrolling()) {
      builder.SetIgnoreScrollFrame(rootScroll);
    }
  }
  builder.IgnorePaintSuppression();
  builder.EnterPresShell(rootFrame);
  nsDisplayList list;
  rootFrame->BuildDisplayListForStackingContext(&builder, updateRect, &list);
  builder.LeavePresShell(rootFrame, &list);

  RebuildApproximateFrameVisibilityDisplayList(list);

  ClearApproximateFrameVisibilityVisited(rootFrame->GetView(), true);

  list.DeleteAll(&builder);
#endif
}

bool PresShell::AssumeAllFramesVisible() {
  if (!StaticPrefs::layout_framevisibility_enabled() || !mPresContext ||
      !mDocument) {
    return true;
  }

  // We assume all frames are visible in print, print preview, chrome, and
  // resource docs and don't keep track of them.
  if (mPresContext->Type() == nsPresContext::eContext_PrintPreview ||
      mPresContext->Type() == nsPresContext::eContext_Print ||
      mPresContext->IsChrome() || mDocument->IsResourceDoc()) {
    return true;
  }

  // If we're assuming all frames are visible in the top level content
  // document, we need to in subdocuments as well. Otherwise we can get in a
  // situation where things like animations won't work in subdocuments because
  // their frames appear not to be visible, since we won't schedule an image
  // visibility update if the top level content document is assuming all
  // frames are visible.
  //
  // Note that it's not safe to call IsRootContentDocumentInProcess() if we're
  // currently being destroyed, so we have to check that first.
  if (!mHaveShutDown && !mIsDestroying &&
      !mPresContext->IsRootContentDocumentInProcess()) {
    nsPresContext* presContext =
        mPresContext->GetInProcessRootContentDocumentPresContext();
    if (presContext && presContext->PresShell()->AssumeAllFramesVisible()) {
      return true;
    }
  }

  return false;
}

void PresShell::ScheduleApproximateFrameVisibilityUpdateSoon() {
  if (AssumeAllFramesVisible()) {
    return;
  }

  if (!mPresContext) {
    return;
  }

  nsRefreshDriver* refreshDriver = mPresContext->RefreshDriver();
  if (!refreshDriver) {
    return;
  }

  // Ask the refresh driver to update frame visibility soon.
  refreshDriver->ScheduleFrameVisibilityUpdate();
}

void PresShell::ScheduleApproximateFrameVisibilityUpdateNow() {
  if (AssumeAllFramesVisible()) {
    return;
  }

  if (!mPresContext->IsRootContentDocumentInProcess()) {
    nsPresContext* presContext =
        mPresContext->GetInProcessRootContentDocumentPresContext();
    if (!presContext) {
      return;
    }
    MOZ_ASSERT(presContext->IsRootContentDocumentInProcess(),
               "Didn't get a root prescontext from "
               "GetInProcessRootContentDocumentPresContext?");
    presContext->PresShell()->ScheduleApproximateFrameVisibilityUpdateNow();
    return;
  }

  if (mHaveShutDown || mIsDestroying) {
    return;
  }

  if (mUpdateApproximateFrameVisibilityEvent.IsPending()) {
    return;
  }

  RefPtr<nsRunnableMethod<PresShell>> event =
      NewRunnableMethod("PresShell::UpdateApproximateFrameVisibility", this,
                        &PresShell::UpdateApproximateFrameVisibility);
  nsresult rv = mDocument->Dispatch(do_AddRef(event));

  if (NS_SUCCEEDED(rv)) {
    mUpdateApproximateFrameVisibilityEvent = std::move(event);
  }
}

void PresShell::EnsureFrameInApproximatelyVisibleList(nsIFrame* aFrame) {
  if (!aFrame->TrackingVisibility()) {
    return;
  }

  if (AssumeAllFramesVisible()) {
    aFrame->IncApproximateVisibleCount();
    return;
  }

#ifdef DEBUG
  // Make sure it's in this pres shell.
  nsCOMPtr<nsIContent> content = aFrame->GetContent();
  if (content) {
    PresShell* presShell = content->OwnerDoc()->GetPresShell();
    MOZ_ASSERT(!presShell || presShell == this, "wrong shell");
  }
#endif

  if (mApproximatelyVisibleFrames.EnsureInserted(aFrame)) {
    // We inserted a new entry.
    aFrame->IncApproximateVisibleCount();
  }
}

void PresShell::RemoveFrameFromApproximatelyVisibleList(nsIFrame* aFrame) {
#ifdef DEBUG
  // Make sure it's in this pres shell.
  nsCOMPtr<nsIContent> content = aFrame->GetContent();
  if (content) {
    PresShell* presShell = content->OwnerDoc()->GetPresShell();
    MOZ_ASSERT(!presShell || presShell == this, "wrong shell");
  }
#endif

  if (AssumeAllFramesVisible()) {
    MOZ_ASSERT(mApproximatelyVisibleFrames.Count() == 0,
               "Shouldn't have any frames in the table");
    return;
  }

  if (mApproximatelyVisibleFrames.EnsureRemoved(aFrame) &&
      aFrame->TrackingVisibility()) {
    // aFrame was in the hashtable, and we're still tracking its visibility,
    // so we need to decrement its visible count.
    aFrame->DecApproximateVisibleCount();
  }
}

void PresShell::PaintAndRequestComposite(nsView* aView, PaintFlags aFlags) {
  if (!mIsActive) {
    return;
  }

  WindowRenderer* renderer = aView->GetWidget()->GetWindowRenderer();
  NS_ASSERTION(renderer, "Must be in paint event");
  if (renderer->AsFallback()) {
    // The fallback renderer doesn't do any retaining, so we
    // just need to notify the view and widget that we're invalid, and
    // we'll do a paint+composite from the PaintWindow callback.
    GetViewManager()->InvalidateView(aView);
    return;
  }

  // Otherwise we're a retained WebRenderLayerManager, so we want to call
  // Paint to update with any changes and push those to WR.
  PaintInternalFlags flags = PaintInternalFlags::None;
  if (aFlags & PaintFlags::PaintSyncDecodeImages) {
    flags |= PaintInternalFlags::PaintSyncDecodeImages;
  }
  if (aFlags & PaintFlags::PaintCompositeOffscreen) {
    flags |= PaintInternalFlags::PaintCompositeOffscreen;
  }
  PaintInternal(aView, flags);
}

void PresShell::SyncPaintFallback(nsView* aView) {
  if (!mIsActive) {
    return;
  }

  WindowRenderer* renderer = aView->GetWidget()->GetWindowRenderer();
  NS_ASSERTION(renderer->AsFallback(),
               "Can't do Sync paint for remote renderers");
  if (!renderer->AsFallback()) {
    return;
  }

  PaintInternal(aView, PaintInternalFlags::PaintComposite);
  GetPresContext()->NotifyDidPaintForSubtree();
}

void PresShell::PaintInternal(nsView* aViewToPaint, PaintInternalFlags aFlags) {
  nsCString url;
  nsIURI* uri = mDocument->GetDocumentURI();
  Document* contentRoot = GetPrimaryContentDocument();
  if (contentRoot) {
    uri = contentRoot->GetDocumentURI();
  }
  url = uri ? uri->GetSpecOrDefault() : "N/A"_ns;
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_RELEVANT_FOR_JS(
      "Paint", GRAPHICS, Substring(url, std::min(size_t(128), url.Length())));

  Maybe<js::AutoAssertNoContentJS> nojs;

  // On Android, Flash can call into content JS during painting, so we can't
  // assert there. However, we don't rely on this assertion on Android because
  // we don't paint while JS is running.
#if !defined(MOZ_WIDGET_ANDROID)
  if (!(aFlags & PaintInternalFlags::PaintComposite)) {
    // We need to allow content JS when the flag is set since we may trigger
    // MozAfterPaint events in content in those cases.
    nojs.emplace(dom::danger::GetJSContext());
  }
#endif

  NS_ASSERTION(!mIsDestroying, "painting a destroyed PresShell");
  NS_ASSERTION(aViewToPaint, "null view");

  MOZ_ASSERT(!mApproximateFrameVisibilityVisited, "Should have been cleared");

  if (!mIsActive) {
    return;
  }

  nsIFrame* frame = aViewToPaint->GetFrame();

  FocusTarget focusTarget;
  if (StaticPrefs::apz_keyboard_enabled_AtStartup()) {
    // Update the focus target for async keyboard scrolling. This will be
    // forwarded to APZ by nsDisplayList::PaintRoot. We need to to do this
    // before we enter the paint phase because dispatching eVoid events can
    // cause layout to happen.
    uint64_t focusSequenceNumber;
    if (nsMenuPopupFrame* popup = do_QueryFrame(frame)) {
      focusSequenceNumber = popup->GetAPZFocusSequenceNumber();
    } else {
      focusSequenceNumber = mAPZFocusSequenceNumber;
    }
    focusTarget = FocusTarget(this, focusSequenceNumber);
  }

  nsPresContext* presContext = GetPresContext();
  AUTO_LAYOUT_PHASE_ENTRY_POINT(presContext, Paint);

  WindowRenderer* renderer = aViewToPaint->GetWidget()->GetWindowRenderer();
  NS_ASSERTION(renderer, "Must be in paint event");
  WebRenderLayerManager* layerManager = renderer->AsWebRender();

  // Whether or not we should set first paint when painting is suppressed
  // is debatable. For now we'll do it because B2G relied on first paint
  // to configure the viewport and we only want to do that when we have
  // real content to paint. See Bug 798245
  if (mIsFirstPaint && !mPaintingSuppressed) {
    MOZ_LOG(gLog, LogLevel::Debug,
            ("PresShell::Paint, first paint, this=%p", this));

    if (layerManager) {
      layerManager->SetIsFirstPaint();
    }
    mIsFirstPaint = false;
  }

  const bool offscreen =
      bool(aFlags & PaintInternalFlags::PaintCompositeOffscreen);

  if (!renderer->BeginTransaction(url)) {
    return;
  }

  // Send an updated focus target with this transaction. Be sure to do this
  // before we paint in the case this is an empty transaction.
  if (layerManager) {
    layerManager->SetFocusTarget(focusTarget);
  }

  if (frame) {
    if (!(aFlags & PaintInternalFlags::PaintSyncDecodeImages) &&
        !frame->HasAnyStateBits(NS_FRAME_UPDATE_LAYER_TREE)) {
      if (layerManager) {
        layerManager->SetTransactionIdAllocator(presContext->RefreshDriver());
      }

      if (renderer->EndEmptyTransaction(
              (aFlags & PaintInternalFlags::PaintComposite)
                  ? WindowRenderer::END_DEFAULT
                  : WindowRenderer::END_NO_COMPOSITE)) {
        return;
      }
    }
    frame->RemoveStateBits(NS_FRAME_UPDATE_LAYER_TREE);
  }

  nscolor bgcolor = ComputeBackstopColor(aViewToPaint);
  PaintFrameFlags flags =
      PaintFrameFlags::WidgetLayers | PaintFrameFlags::ExistingTransaction;

  // We force sync-decode for printing / print-preview (printing already does
  // this from nsPageSequenceFrame::PrintNextSheet).
  // We also force sync-decoding via pref for reftests.
  if (aFlags & PaintInternalFlags::PaintSyncDecodeImages ||
      mDocument->IsStaticDocument() ||
      StaticPrefs::image_testing_decode_sync_enabled()) {
    flags |= PaintFrameFlags::SyncDecodeImages;
  }
  if (aFlags & PaintInternalFlags::PaintCompositeOffscreen) {
    flags |= PaintFrameFlags::CompositeOffscreen;
  }
  if (renderer->GetBackendType() == layers::LayersBackend::LAYERS_WR) {
    flags |= PaintFrameFlags::ForWebRender;
  }

  if (frame) {
    // We can paint directly into the widget using its layer manager.
    SelectionNodeCache cache(*this);
    nsLayoutUtils::PaintFrame(nullptr, frame, nsRegion(), bgcolor,
                              nsDisplayListBuilderMode::Painting, flags);
    return;
  }

  bgcolor = NS_ComposeColors(bgcolor, mCanvasBackground.mViewport.mColor);

  if (renderer->GetBackendType() == layers::LayersBackend::LAYERS_WR) {
    LayoutDeviceRect bounds = LayoutDeviceRect::FromAppUnits(
        presContext->GetVisibleArea(), presContext->AppUnitsPerDevPixel());
    WebRenderBackgroundData data(wr::ToLayoutRect(bounds),
                                 wr::ToColorF(ToDeviceColor(bgcolor)));
    WrFiltersHolder wrFilters;

    layerManager->SetTransactionIdAllocator(presContext->RefreshDriver());
    layerManager->EndTransactionWithoutLayer(
        nullptr, nullptr, std::move(wrFilters), &data, 0, offscreen);
    return;
  }

  FallbackRenderer* fallback = renderer->AsFallback();
  MOZ_ASSERT(fallback);

  if (aFlags & PaintInternalFlags::PaintComposite) {
    nsIntRect bounds = presContext->GetVisibleArea().ToOutsidePixels(
        presContext->AppUnitsPerDevPixel());
    fallback->EndTransactionWithColor(bounds, ToDeviceColor(bgcolor));
  }
}

// static
void PresShell::SetCapturingContent(nsIContent* aContent, CaptureFlags aFlags,
                                    WidgetEvent* aEvent) {
  // If capture was set for pointer lock, don't unlock unless we are coming
  // out of pointer lock explicitly.
  if (!aContent && sCapturingContentInfo.mPointerLock &&
      !(aFlags & CaptureFlags::PointerLock)) {
    return;
  }

  sCapturingContentInfo.mContent = nullptr;
  sCapturingContentInfo.mRemoteTarget = nullptr;

  // only set capturing content if allowed or the
  // CaptureFlags::IgnoreAllowedState or CaptureFlags::PointerLock are used.
  if ((aFlags & CaptureFlags::IgnoreAllowedState) ||
      sCapturingContentInfo.mAllowed || (aFlags & CaptureFlags::PointerLock)) {
    if (aContent) {
      sCapturingContentInfo.mContent = aContent;
    }
    if (aEvent) {
      MOZ_ASSERT(XRE_IsParentProcess());
      MOZ_ASSERT(aEvent->mMessage == eMouseDown);
      MOZ_ASSERT(aEvent->HasBeenPostedToRemoteProcess());
      sCapturingContentInfo.mRemoteTarget =
          BrowserParent::GetLastMouseRemoteTarget();
      MOZ_ASSERT(sCapturingContentInfo.mRemoteTarget);
    }
    // CaptureFlags::PointerLock is the same as
    // CaptureFlags::RetargetToElement & CaptureFlags::IgnoreAllowedState.
    sCapturingContentInfo.mRetargetToElement =
        !!(aFlags & CaptureFlags::RetargetToElement) ||
        !!(aFlags & CaptureFlags::PointerLock);
    sCapturingContentInfo.mPreventDrag =
        !!(aFlags & CaptureFlags::PreventDragStart);
    sCapturingContentInfo.mPointerLock = !!(aFlags & CaptureFlags::PointerLock);
  }
}

nsIContent* PresShell::GetCurrentEventContent() {
  if (mCurrentEventTarget.mContent &&
      mCurrentEventTarget.mContent->GetComposedDoc() != mDocument) {
    mCurrentEventTarget.Clear();
  }
  return mCurrentEventTarget.mContent;
}

nsIFrame* PresShell::GetCurrentEventFrame() {
  if (MOZ_UNLIKELY(mIsDestroying)) {
    return nullptr;
  }

  // GetCurrentEventContent() makes sure the content is still in the
  // same document that this pres shell belongs to. If not, then the
  // frame shouldn't get an event, nor should we even assume its safe
  // to try and find the frame.
  nsIContent* content = GetCurrentEventContent();
  if (!mCurrentEventTarget.mFrame && content) {
    mCurrentEventTarget.mFrame = content->GetPrimaryFrame();
    MOZ_ASSERT_IF(
        mCurrentEventTarget.mFrame,
        mCurrentEventTarget.mFrame->PresContext()->GetPresShell() == this);
  }
  return mCurrentEventTarget.mFrame;
}

already_AddRefed<nsIContent> PresShell::GetEventTargetContent(
    WidgetEvent* aEvent) {
  nsCOMPtr<nsIContent> content = GetCurrentEventContent();
  if (!content) {
    if (nsIFrame* currentEventFrame = GetCurrentEventFrame()) {
      content = currentEventFrame->GetContentForEvent(aEvent);
      NS_ASSERTION(!content || content->GetComposedDoc() == mDocument,
                   "handing out content from a different doc");
    }
  }
  return content.forget();
}

void PresShell::PushCurrentEventInfo(const EventTargetInfo& aInfo) {
  if (mCurrentEventTarget.IsSet()) {
    // XXX Why do we insert first item instead of append it? This requires to
    // move the previous items...
    mCurrentEventTargetStack.InsertElementAt(0, std::move(mCurrentEventTarget));
  }
  mCurrentEventTarget = aInfo;
}

void PresShell::PushCurrentEventInfo(EventTargetInfo&& aInfo) {
  if (mCurrentEventTarget.IsSet()) {
    mCurrentEventTargetStack.InsertElementAt(0, std::move(mCurrentEventTarget));
  }
  mCurrentEventTarget = std::move(aInfo);
}

void PresShell::PopCurrentEventInfo() {
  mCurrentEventTarget.Clear();

  if (!mCurrentEventTargetStack.IsEmpty()) {
    mCurrentEventTarget = std::move(mCurrentEventTargetStack[0]);
    mCurrentEventTargetStack.RemoveElementAt(0);

    // Don't use it if it has moved to a different document.
    if (mCurrentEventTarget.mContent &&
        mCurrentEventTarget.mContent->GetComposedDoc() != mDocument) {
      mCurrentEventTarget.Clear();
    }
  }
}

// static
bool PresShell::EventHandler::InZombieDocument(nsIContent* aContent) {
  // If a content node points to a null document, or the document is not
  // attached to a window, then it is possibly in a zombie document,
  // about to be replaced by a newly loading document.
  // Such documents cannot handle DOM events.
  // It might actually be in a node not attached to any document,
  // in which case there is not parent presshell to retarget it to.
  Document* doc = aContent->GetComposedDoc();
  return !doc || !doc->GetWindow();
}

already_AddRefed<nsPIDOMWindowOuter> PresShell::GetRootWindow() {
  nsCOMPtr<nsPIDOMWindowOuter> window = mDocument->GetWindow();
  if (window) {
    nsCOMPtr<nsPIDOMWindowOuter> rootWindow = window->GetPrivateRoot();
    NS_ASSERTION(rootWindow, "nsPIDOMWindow::GetPrivateRoot() returns NULL");
    return rootWindow.forget();
  }

  // If we don't have DOM window, we're zombie, we should find the root window
  // with our parent shell.
  RefPtr<PresShell> parentPresShell = GetParentPresShellForEventHandling();
  NS_ENSURE_TRUE(parentPresShell, nullptr);
  return parentPresShell->GetRootWindow();
}

already_AddRefed<nsPIDOMWindowOuter>
PresShell::GetFocusedDOMWindowInOurWindow() {
  nsCOMPtr<nsPIDOMWindowOuter> rootWindow = GetRootWindow();
  NS_ENSURE_TRUE(rootWindow, nullptr);
  nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
  nsFocusManager::GetFocusedDescendant(rootWindow,
                                       nsFocusManager::eIncludeAllDescendants,
                                       getter_AddRefs(focusedWindow));
  return focusedWindow.forget();
}

already_AddRefed<nsIContent> PresShell::GetFocusedContentInOurWindow() const {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (fm && mDocument) {
    RefPtr<Element> focusedElement;
    fm->GetFocusedElementForWindow(mDocument->GetWindow(), false, nullptr,
                                   getter_AddRefs(focusedElement));
    return focusedElement.forget();
  }
  return nullptr;
}

already_AddRefed<PresShell> PresShell::GetParentPresShellForEventHandling() {
  if (!mPresContext) {
    return nullptr;
  }

  // Now, find the parent pres shell and send the event there
  RefPtr<nsDocShell> docShell = mPresContext->GetDocShell();
  if (!docShell) {
    docShell = mForwardingContainer.get();
  }

  // Might have gone away, or never been around to start with
  if (!docShell) {
    return nullptr;
  }

  BrowsingContext* bc = docShell->GetBrowsingContext();
  if (!bc) {
    return nullptr;
  }

  RefPtr<BrowsingContext> parentBC;
  if (XRE_IsParentProcess()) {
    parentBC = bc->Canonical()->GetParentCrossChromeBoundary();
  } else {
    parentBC = bc->GetParent();
  }

  RefPtr<nsIDocShell> parentDocShell =
      parentBC ? parentBC->GetDocShell() : nullptr;
  if (!parentDocShell) {
    return nullptr;
  }

  RefPtr<PresShell> parentPresShell = parentDocShell->GetPresShell();
  return parentPresShell.forget();
}

nsresult PresShell::EventHandler::RetargetEventToParent(
    WidgetGUIEvent* aGUIEvent, nsEventStatus* aEventStatus) {
  // Send this events straight up to the parent pres shell.
  // We do this for keystroke events in zombie documents or if either a frame
  // or a root content is not present.
  // That way at least the UI key bindings can work.

  RefPtr<PresShell> parentPresShell = GetParentPresShellForEventHandling();
  NS_ENSURE_TRUE(parentPresShell, NS_ERROR_FAILURE);

  // Fake the event as though it's from the parent pres shell's root frame.
  return parentPresShell->HandleEvent(parentPresShell->GetRootFrame(),
                                      aGUIEvent, true, aEventStatus);
}

void PresShell::DisableNonTestMouseEvents(bool aDisable) {
  sDisableNonTestMouseEvents = aDisable;
}

bool PresShell::MouseLocationWasSetBySynthesizedMouseEventForTests() const {
  if (!mPresContext) {
    return false;
  }
  if (mPresContext->IsRoot()) {
    return mMouseLocationWasSetBySynthesizedMouseEventForTests;
  }
  PresShell* rootPresShell = GetRootPresShell();
  return rootPresShell &&
         rootPresShell->mMouseLocationWasSetBySynthesizedMouseEventForTests;
}

nsPoint PresShell::GetEventLocation(const WidgetMouseEvent& aEvent) const {
  nsIFrame* rootFrame = GetRootFrame();
  if (rootFrame) {
    RelativeTo relativeTo{rootFrame};
    if (rootFrame->PresContext()->IsRootContentDocumentCrossProcess()) {
      relativeTo.mViewportType = ViewportType::Visual;
    }
    return nsLayoutUtils::GetEventCoordinatesRelativeTo(&aEvent, relativeTo);
  }

  nsView* rootView = mViewManager->GetRootView();
  return nsLayoutUtils::TranslateWidgetToView(mPresContext, aEvent.mWidget,
                                              aEvent.mRefPoint, rootView);
}

void PresShell::RecordPointerLocation(WidgetGUIEvent* aEvent) {
  if (!mPresContext) {
    return;
  }

  if (!mPresContext->IsRoot()) {
    PresShell* rootPresShell = GetRootPresShell();
    if (rootPresShell) {
      rootPresShell->RecordPointerLocation(aEvent);
    }
    return;
  }

  switch (aEvent->mMessage) {
    case eMouseMove:
      if (!aEvent->AsMouseEvent()->IsReal()) {
        break;
      }
      [[fallthrough]];
    case eMouseEnterIntoWidget:
    case eMouseDown:
    case eMouseUp:
    case eDragEnter:
    case eDragStart:
    case eDragOver:
    case eDrop: {
      mMouseLocation = GetEventLocation(*aEvent->AsMouseEvent());
      mMouseEventTargetGuid = InputAPZContext::GetTargetLayerGuid();
      // FIXME: Don't trust the synthesized for tests flag of drag events.
      if (aEvent->mClass != eDragEventClass) {
        mMouseLocationWasSetBySynthesizedMouseEventForTests =
            aEvent->mFlags.mIsSynthesizedForTests;
      }
#ifdef DEBUG
      if (MOZ_LOG_TEST(gLogMouseLocation, LogLevel::Info)) {
        static uint32_t sFrequentMessageCount = 0;
        const bool isFrequestMessage =
            aEvent->mMessage == eMouseMove || aEvent->mMessage == eDragOver;
        if (!isFrequestMessage ||
            MOZ_LOG_TEST(gLogMouseLocation, LogLevel::Verbose) ||
            !(sFrequentMessageCount % 50)) {
          MOZ_LOG(gLogMouseLocation,
                  isFrequestMessage ? LogLevel::Debug : LogLevel::Info,
                  ("[ps=%p]got %s for %p at {%d, %d}\n", this,
                   ToChar(aEvent->mMessage), aEvent->mWidget.get(),
                   mMouseLocation.x, mMouseLocation.y));
        }
        if (isFrequestMessage) {
          sFrequentMessageCount++;
        } else {
          // Let's log the next eMouseMove or eDragOver after the other
          // messages.
          sFrequentMessageCount = 0;
        }
      }
#endif
      if (aEvent->mMessage == eMouseEnterIntoWidget ||
          aEvent->mClass == eDragEventClass) {
        SynthesizeMouseMove(false);
      }
      break;
    }
    case eDragExit:
      if (aEvent->mRelatedTarget) {
        // not exit from the widget
        break;
      }
      [[fallthrough]];
    case eMouseExitFromWidget: {
      // Although we only care about the mouse moving into an area for which
      // this pres shell doesn't receive mouse move events, we don't check which
      // widget the mouse exit was for since this seems to vary by platform.
      // Hopefully this won't matter at all since we'll get the mouse move or
      // enter after the mouse exit when the mouse moves from one of our widgets
      // into another.
      mMouseLocation = nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
      mMouseEventTargetGuid = InputAPZContext::GetTargetLayerGuid();
      mMouseLocationWasSetBySynthesizedMouseEventForTests =
          aEvent->mFlags.mIsSynthesizedForTests;
#ifdef DEBUG
      MOZ_LOG(gLogMouseLocation, LogLevel::Info,
              ("[ps=%p]got %s for %p, mouse location is cleared\n", this,
               ToChar(aEvent->mMessage), aEvent->mWidget.get()));
#endif
      break;
    }
    case ePointerMove:
      if (!aEvent->AsMouseEvent()->IsReal()) {
        break;
      }
      [[fallthrough]];
    case ePointerDown:
    case ePointerUp: {
      // TODO: instead, encapsulate `mMouseLocation` and
      // `mLastOverWindowPointerLocation` in a struct.
      mLastOverWindowPointerLocation =
          GetEventLocation(*aEvent->AsMouseEvent());
      break;
    }
    default:
      break;
  }
}

void PresShell::nsSynthMouseMoveEvent::Revoke() {
  if (mPresShell) {
    mPresShell->GetPresContext()->RefreshDriver()->RemoveRefreshObserver(
        this, FlushType::Display);
    mPresShell = nullptr;
  }
}

nsIFrame* PresShell::GetClosestAncestorFrameForAncestorView() const {
  nsViewManager* vm = GetViewManager();
  if (!vm) {
    return nullptr;
  }
  nsView* view = vm->GetRootView();
  while (view && !view->GetFrame()) {
    view = view->GetParent();
  }

  nsIFrame* frame = nullptr;
  if (view) {
    frame = view->GetFrame();
  }

  return frame;
}

static CallState FlushThrottledStyles(Document& aDocument) {
  PresShell* presShell = aDocument.GetPresShell();
  if (presShell && presShell->IsVisible()) {
    if (nsPresContext* presContext = presShell->GetPresContext()) {
      presContext->RestyleManager()->UpdateOnlyAnimationStyles();
    }
  }

  aDocument.EnumerateSubDocuments(FlushThrottledStyles);
  return CallState::Continue;
}

bool PresShell::CanDispatchEvent(const WidgetGUIEvent* aEvent) const {
  bool rv =
      mPresContext && !mHaveShutDown && nsContentUtils::IsSafeToRunScript();
  if (aEvent) {
    rv &= (aEvent && aEvent->mWidget && !aEvent->mWidget->Destroyed());
  }
  return rv;
}

/* static */
PresShell* PresShell::GetShellForEventTarget(nsIFrame* aFrame,
                                             nsIContent* aContent) {
  if (aFrame) {
    return aFrame->PresShell();
  }
  if (aContent) {
    Document* doc = aContent->GetComposedDoc();
    if (!doc) {
      return nullptr;
    }
    return doc->GetPresShell();
  }
  return nullptr;
}

/* static */
PresShell* PresShell::GetShellForTouchEvent(WidgetGUIEvent* aEvent) {
  switch (aEvent->mMessage) {
    case eTouchMove:
    case eTouchCancel:
    case eTouchEnd: {
      // get the correct shell to dispatch to
      WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();
      for (dom::Touch* touch : touchEvent->mTouches) {
        if (!touch) {
          return nullptr;
        }

        RefPtr<dom::Touch> oldTouch =
            TouchManager::GetCapturedTouch(touch->Identifier());
        if (!oldTouch) {
          return nullptr;
        }

        nsIContent* const content =
            nsIContent::FromEventTargetOrNull(oldTouch->GetTarget());
        if (!content) {
          return nullptr;
        }

        if (PresShell* const presShell = content->OwnerDoc()->GetPresShell()) {
          return presShell;
        }
      }
      return nullptr;
    }
    default:
      return nullptr;
  }
}

nsresult PresShell::HandleEvent(nsIFrame* aFrameForPresShell,
                                WidgetGUIEvent* aGUIEvent,
                                bool aDontRetargetEvents,
                                nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aGUIEvent);

  AutoWeakFrame weakFrameForPresShell(aFrameForPresShell);

  // Running tests must not expect that some mouse boundary events are fired
  // when something occurs in the parent process, e.g., when a popup is
  // opened/closed at the last mouse cursor position in the parent process (the
  // position may be different from the position which stored in this process).
  // Therefore, let's ignore synthesized mouse events coming form another
  // process if and only if they are not caused by the API.
  if (aGUIEvent->CameFromAnotherProcess() && XRE_IsContentProcess() &&
      !aGUIEvent->mFlags.mIsSynthesizedForTests &&
      MouseLocationWasSetBySynthesizedMouseEventForTests()) {
    switch (aGUIEvent->mMessage) {
      // Synthesized eMouseMove will case mouse boundary events like mouseover,
      // mouseout, and :hover state is changed at dispatching the events.
      case eMouseMove:
      // eMouseExitFromWidget comes from the parent process if the cursor
      // crosses a puppet widget boundary.  Then, the event will be handled as a
      // synthesized eMouseMove in this process and may cause unexpected
      // `mouseout` and `mouseleave`.
      case eMouseExitFromWidget:
      // eMouseEnterIntoWidget causes updating the hover state under the event
      // position which may be different from the last cursor position
      // synthesized in this process.
      case eMouseEnterIntoWidget:
        if (!aGUIEvent->AsMouseEvent()->IsReal()) {
          return NS_OK;
        }
        break;
      default:
        break;
    }
  }

  // Here we are granting some delays to ensure that user input events are
  // created while the page content may not be visible to the user are not
  // processed.
  // The main purpose of this is to avoid user inputs are handled in the
  // new document where as the user inputs were originally targeting some
  // content in the old document.
  if (!CanHandleUserInputEvents(aGUIEvent)) {
    return NS_OK;
  }

  // If there is a composition and we got a pointing device events which may not
  // impossible to continue the composition, we should notify the editor of the
  // event before dispatching it.  Then, composition will be commited before
  // the editor loses focus.  This behavior is compatible with Chrome.
  // FIXME: Perhaps, we should do same thing before dispatching touch events.
  switch (aGUIEvent->mMessage) {
    case eMouseDown:
    case eMouseUp: {
      nsPIDOMWindowOuter* const focusedWindow =
          nsFocusManager::GetFocusedWindowStatic();
      if (!focusedWindow) {
        break;
      }
      Document* const focusedDocument = focusedWindow->GetExtantDoc();
      if (!focusedDocument) {
        break;
      }
      nsPresContext* const focusedPresContext =
          focusedDocument->GetPresContext();
      if (!focusedPresContext) {
        break;
      }
      const RefPtr<TextComposition> textComposition =
          IMEStateManager::GetTextCompositionFor(focusedPresContext);
      if (!textComposition) {
        break;
      }
      // If there is a composition and it's managed by an editor, let's notify
      // the editor of mouse button event.  The editor commits the composition
      // unless IME consumes the event.
      if (RefPtr<EditorBase> editorBase = textComposition->GetEditorBase()) {
        MOZ_ASSERT(aGUIEvent->AsMouseEvent());
        editorBase->WillHandleMouseButtonEvent(*aGUIEvent->AsMouseEvent());
      }
      // Otherwise, we should commit the orphan composition instead.
      else if (nsCOMPtr<nsIWidget> widget = textComposition->GetWidget()) {
        textComposition->RequestToCommit(widget, false);
      }
      if (!CanHandleUserInputEvents(aGUIEvent)) {
        return NS_OK;
      }
      // Before bug 1945988, we dispatched the mouse button event without
      // committing composition and the editor will commit composition.
      // However, for compatibility with the other browsers, we started
      // committing composition before dispatching the mouse event.  To keep
      // the traditional behavior, we should compute aFrameForPresShell if it's
      // reframed by dispatching the composition events (and input events)
      // above.  Otherwise, we stop dispatching the mouse button events newly in
      // such case.
      if (MOZ_UNLIKELY(!weakFrameForPresShell.IsAlive())) {
        FlushPendingNotifications(FlushType::Layout);
        if (MOZ_UNLIKELY(IsDestroying())) {
          return NS_OK;
        }
        nsIFrame* const newFrameForPresShell =
            GetClosestAncestorFrameForAncestorView();
        if (MOZ_UNLIKELY(!newFrameForPresShell)) {
          return NS_OK;
        }
        weakFrameForPresShell = newFrameForPresShell;
      }
      break;
    }
    default:
      break;
  }

  if (mPresContext) {
    switch (aGUIEvent->mMessage) {
      case eMouseMove:
        if (!aGUIEvent->AsMouseEvent()->IsReal()) {
          break;
        }
        [[fallthrough]];
      case eMouseDown:
      case eMouseUp: {
        // We should flush pending mousemove event now because some mouse
        // boundary events which should've already been dispatched before a user
        // input may have not been dispatched.  E.g., if a mousedown event
        // listener removed or appended an element under the cursor and mouseup
        // event comes immediately after that, mouseover or mouseout may have
        // not been dispatched on the new element yet.
        // XXX If eMouseMove is not propery dispatched before eMouseDown and
        // a `mousedown` event listener removes the event target or its
        // ancestor, eMouseOver will be dispatched between eMouseDown and
        // eMouseUp.  That could cause unexpected behavior if a `mouseover`
        // event listener assumes it's always disptached before `mousedown`.
        // However, we're not sure whether it could happen with users' input.
        // FIXME: Perhaps, we need to do this for all events which are directly
        // caused by user input, e.g., eKeyDown, etc.
        RefPtr<PresShell> rootPresShell =
            mPresContext->IsRoot() ? this : GetRootPresShell();
        if (rootPresShell && rootPresShell->mSynthMouseMoveEvent.IsPending()) {
          RefPtr<nsSynthMouseMoveEvent> synthMouseMoveEvent =
              rootPresShell->mSynthMouseMoveEvent.get();
          synthMouseMoveEvent->Run();
          if (IsDestroying()) {
            return NS_OK;
          }
          // XXX If the frame or "this" is reframed, it might be better to
          // recompute the frame.  However, it could treat the user input on
          // unexpected element.  Therefore, we should not do that until we'd
          // get a bug report caused by that.
          if (MOZ_UNLIKELY(!weakFrameForPresShell.IsAlive())) {
            return NS_OK;
          }
        }
        break;
      }
      default:
        break;
    }
  }

  EventHandler eventHandler(*this);
  return eventHandler.HandleEvent(weakFrameForPresShell, aGUIEvent,
                                  aDontRetargetEvents, aEventStatus);
}

bool PresShell::EventHandler::UpdateFocusSequenceNumber(
    nsIFrame* aFrameForPresShell, uint64_t aEventFocusSequenceNumber) {
  uint64_t focusSequenceNumber;
  nsMenuPopupFrame* popup = do_QueryFrame(aFrameForPresShell);
  if (popup) {
    focusSequenceNumber = popup->GetAPZFocusSequenceNumber();
  } else {
    focusSequenceNumber = mPresShell->mAPZFocusSequenceNumber;
  }
  if (focusSequenceNumber >= aEventFocusSequenceNumber) {
    return false;
  }

  if (popup) {
    popup->UpdateAPZFocusSequenceNumber(aEventFocusSequenceNumber);
  } else {
    mPresShell->mAPZFocusSequenceNumber = aEventFocusSequenceNumber;
  }
  return true;
}

nsresult PresShell::EventHandler::HandleEvent(
    AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    bool aDontRetargetEvents, nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_DIAGNOSTIC_ASSERT(aGUIEvent->IsTrusted());
  MOZ_ASSERT(aEventStatus);

  NS_ASSERTION(aWeakFrameForPresShell.IsAlive(),
               "aWeakFrameForPresShell should refer a frame");

  // Update the latest focus sequence number with this new sequence number;
  // the next transasction that gets sent to the compositor will carry this over
  if (UpdateFocusSequenceNumber(aWeakFrameForPresShell.GetFrame(),
                                aGUIEvent->mFocusSequenceNumber)) {
    if (aWeakFrameForPresShell.IsAlive() &&
        StaticPrefs::apz_keyboard_focus_optimization()) {
      aWeakFrameForPresShell->SchedulePaint(nsIFrame::PAINT_COMPOSITE_ONLY);
    }
  }

  if (mPresShell->IsDestroying() ||
      (PresShell::sDisableNonTestMouseEvents &&
       !aGUIEvent->mFlags.mIsSynthesizedForTests &&
       aGUIEvent->HasMouseEventMessage())) {
    return NS_OK;
  }

  mPresShell->RecordPointerLocation(aGUIEvent);

  const bool wasFrameForPresShellNull = !aWeakFrameForPresShell.GetFrame();
  if (MaybeHandleEventWithAccessibleCaret(aWeakFrameForPresShell, aGUIEvent,
                                          aEventStatus)) {
    // Handled by AccessibleCaretEventHub.
    return NS_OK;
  }

  if (MaybeDiscardEvent(aGUIEvent)) {
    // Cannot handle the event for now.
    return NS_OK;
  }

  if (!aDontRetargetEvents) {
    const DebugOnly<bool> wasFrameForPresShellAlive =
        aWeakFrameForPresShell.IsAlive();
    // If aGUIEvent should be handled in another PresShell, we should call its
    // HandleEvent() and do nothing here.
    nsresult rv = NS_OK;
    if (MaybeHandleEventWithAnotherPresShell(aWeakFrameForPresShell, aGUIEvent,
                                             aEventStatus, &rv)) {
      // Handled by another PresShell or nobody can handle the event.
      return rv;
    }
    // If MaybeHandleEventWithAnotherPresShell() returns false, it shouldn't
    // have run any script.  So, aWeakFrameForPresShell must be alive.
    MOZ_ASSERT_IF(wasFrameForPresShellAlive, aWeakFrameForPresShell.IsAlive());
  }

  if (MaybeDiscardOrDelayKeyboardEvent(aGUIEvent)) {
    // The event is discarded or put into the delayed event queue.
    return NS_OK;
  }

  if (aGUIEvent->IsUsingCoordinates()) {
    return HandleEventUsingCoordinates(aWeakFrameForPresShell, aGUIEvent,
                                       aEventStatus, aDontRetargetEvents);
  }

  // Activation events need to be dispatched even if no frame was found, since
  // we don't want the focus to be out of sync.
  if (MOZ_UNLIKELY(wasFrameForPresShellNull)) {
    if (!NS_EVENT_NEEDS_FRAME(aGUIEvent)) {
      // Push nullptr for both current event target content and frame since
      // there is no frame but the event does not require a frame.
      AutoCurrentEventInfoSetter eventInfoSetter(*this);
      return HandleEventWithCurrentEventInfo(aGUIEvent, aEventStatus, true,
                                             nullptr);
    }

    if (aGUIEvent->HasKeyEventMessage()) {
      // Keypress events in new blank tabs should not be completely thrown away.
      // Retarget them -- the parent chrome shell might make use of them.
      return RetargetEventToParent(aGUIEvent, aEventStatus);
    }

    return NS_OK;
  }

  if (aGUIEvent->IsTargetedAtFocusedContent()) {
    return HandleEventAtFocusedContent(aGUIEvent, aEventStatus);
  }

  return HandleEventWithFrameForPresShell(aWeakFrameForPresShell, aGUIEvent,
                                          aEventStatus);
}

nsresult PresShell::EventHandler::HandleEventUsingCoordinates(
    AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    nsEventStatus* aEventStatus, bool aDontRetargetEvents) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aGUIEvent->IsUsingCoordinates());
  MOZ_ASSERT(aEventStatus);

  // The given frame has already been reframed.  Then, we don't need to handle
  // the event.
  if (MOZ_UNLIKELY(!aWeakFrameForPresShell.IsAlive())) {
    return NS_OK;
  }

  // Flush pending notifications to handle the event with the latest layout.
  // But if it causes destroying the frame for mPresShell, stop handling the
  // event. (why?)
  MaybeFlushPendingNotifications(aGUIEvent);
  if (MOZ_UNLIKELY(!aWeakFrameForPresShell.IsAlive())) {
    *aEventStatus = nsEventStatus_eIgnore;
    return NS_OK;
  }

  // XXX Retrieving capturing content here.  However, some of the following
  //     methods allow to run script.  So, isn't it possible the capturing
  //     content outdated?
  nsCOMPtr<nsIContent> capturingContent =
      EventHandler::GetCapturingContentFor(aGUIEvent);
  if (GetDocument() && aGUIEvent->mClass == eTouchEventClass) {
    PointerLockManager::Unlock("TouchEvent");
  }

  MaybeFlushThrottledStyles(aWeakFrameForPresShell);
  if (MOZ_UNLIKELY(!aWeakFrameForPresShell.IsAlive())) {
    return NS_OK;
  }

  bool isCapturingContentIgnored = false;
  bool isCaptureRetargeted = false;
  AutoWeakFrame weakRootFrameToHandleEvent(ComputeRootFrameToHandleEvent(
      aWeakFrameForPresShell.GetFrame(), aGUIEvent, capturingContent,
      &isCapturingContentIgnored, &isCaptureRetargeted));
  if (isCapturingContentIgnored) {
    capturingContent = nullptr;
  }

  // The order to generate pointer event is
  // 1. check pending pointer capture.
  // 2. check if there is a capturing content.
  // 3. hit test
  // 4. dispatch pointer events
  // 5. check whether the targets of all Touch instances are in the same
  //    document and suppress invalid instances.
  // 6. dispatch mouse or touch events.

  // Try to keep frame for following check, because frame can be damaged
  // during MaybeProcessPointerCapture.
  PointerEventHandler::MaybeProcessPointerCapture(aGUIEvent);
  // Prevent application crashes, in case damaged frame.
  if (MOZ_UNLIKELY(!weakRootFrameToHandleEvent.IsAlive())) {
    NS_WARNING("Nothing to handle this event!");
    return NS_OK;
  }

  // Only capture mouse events and pointer events.
  const RefPtr<Element> pointerCapturingElement =
      PointerEventHandler::GetPointerCapturingElement(aGUIEvent);

  if (pointerCapturingElement) {
    weakRootFrameToHandleEvent = pointerCapturingElement->GetPrimaryFrame();
    if (!weakRootFrameToHandleEvent.IsAlive()) {
      return HandleEventWithPointerCapturingContentWithoutItsFrame(
          aWeakFrameForPresShell, aGUIEvent, pointerCapturingElement,
          aEventStatus);
    }
  }

  WidgetMouseEvent* mouseEvent = aGUIEvent->AsMouseEvent();
  bool isWindowLevelMouseExit =
      (aGUIEvent->mMessage == eMouseExitFromWidget) &&
      (mouseEvent &&
       (mouseEvent->mExitFrom.value() == WidgetMouseEvent::ePlatformTopLevel ||
        mouseEvent->mExitFrom.value() == WidgetMouseEvent::ePuppet));

  // Get the frame at the event point. However, don't do this if we're
  // capturing and retargeting the event because the captured frame will
  // be used instead below. Also keep using the root frame if we're dealing
  // with a window-level mouse exit event since we want to start sending
  // mouse out events at the root EventStateManager.
  EventTargetData eventTargetData(weakRootFrameToHandleEvent.GetFrame());
  if (!isCaptureRetargeted && !isWindowLevelMouseExit &&
      !pointerCapturingElement) {
    if (!ComputeEventTargetFrameAndPresShellAtEventPoint(
            weakRootFrameToHandleEvent, aGUIEvent, &eventTargetData)) {
      *aEventStatus = nsEventStatus_eIgnore;
      return NS_OK;
    }
  }

  // if a node is capturing the mouse, check if the event needs to be
  // retargeted at the capturing content instead. This will be the case when
  // capture retargeting is being used, no frame was found or the frame's
  // content is not a descendant of the capturing content.
  if (capturingContent && !pointerCapturingElement &&
      (PresShell::sCapturingContentInfo.mRetargetToElement ||
       !eventTargetData.GetFrameContent() ||
       !nsContentUtils::ContentIsCrossDocDescendantOf(
           eventTargetData.GetFrameContent(), capturingContent))) {
    nsIFrame* capturingFrame = capturingContent->GetPrimaryFrame();
    if (capturingFrame) {
      eventTargetData.SetFrameAndComputePresShell(capturingFrame);
    }
  }

  if (NS_WARN_IF(!eventTargetData.GetFrame())) {
    return NS_OK;
  }

  // Suppress mouse event if it's being targeted at an element inside
  // a document which needs events suppressed
  if (MaybeDiscardOrDelayMouseEvent(eventTargetData.GetFrame(), aGUIEvent)) {
    return NS_OK;
  }

  // Check if we have an active EventStateManager which isn't the
  // EventStateManager of the current PresContext.  If that is the case, and
  // mouse is over some ancestor document, forward event handling to the
  // active document.  This way content can get mouse events even when mouse
  // is over the chrome or outside the window.
  if (eventTargetData.MaybeRetargetToActiveDocument(aGUIEvent) &&
      NS_WARN_IF(!eventTargetData.GetFrame())) {
    return NS_OK;
  }

  // Wheel events only apply to elements. If this is a wheel event, attempt to
  // update the event target from the current wheel transaction before we
  // compute the element from the target frame.
  eventTargetData.UpdateWheelEventTarget(aGUIEvent);

  if (!eventTargetData.ComputeElementFromFrame(aGUIEvent)) {
    return NS_OK;
  }
  // Note that even if ComputeElementFromFrame() returns true,
  // eventTargetData.mContent can be nullptr here.

  // Dispatch a pointer event if Pointer Events is enabled.  Note that if
  // pointer event listeners change the layout, eventTargetData is
  // automatically updated.
  if (!DispatchPrecedingPointerEvent(
          aWeakFrameForPresShell, aGUIEvent, pointerCapturingElement,
          aDontRetargetEvents, &eventTargetData, aEventStatus)) {
    return NS_OK;
  }

  // Handle the event in the correct shell.
  // We pass the subshell's root frame as the frame to start from. This is
  // the only correct alternative; if the event was captured then it
  // must have been captured by us or some ancestor shell and we
  // now ask the subshell to dispatch it normally.
  EventHandler eventHandler(*eventTargetData.mPresShell);
  AutoCurrentEventInfoSetter eventInfoSetter(eventHandler, aGUIEvent->mMessage,
                                             eventTargetData);
  // eventTargetData is on the stack and is guaranteed to keep its
  // mOverrideClickTarget alive, so we can just use MOZ_KnownLive here.
  nsresult rv = eventHandler.HandleEventWithCurrentEventInfo(
      aGUIEvent, aEventStatus, true,
      MOZ_KnownLive(eventTargetData.mOverrideClickTarget));
  if (NS_FAILED(rv) ||
      MOZ_UNLIKELY(eventTargetData.mPresShell->IsDestroying())) {
    return rv;
  }

  if (aGUIEvent->mMessage == eTouchEnd) {
    MaybeSynthesizeCompatMouseEventsForTouchEnd(aGUIEvent->AsTouchEvent(),
                                                aEventStatus);
  }

  return NS_OK;
}

bool PresShell::EventHandler::MaybeFlushPendingNotifications(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);

  switch (aGUIEvent->mMessage) {
    case eMouseDown:
    case eMouseUp: {
      RefPtr<nsPresContext> presContext = mPresShell->GetPresContext();
      if (NS_WARN_IF(!presContext)) {
        return false;
      }
      uint64_t framesConstructedCount = presContext->FramesConstructedCount();
      uint64_t framesReflowedCount = presContext->FramesReflowedCount();

      MOZ_KnownLive(mPresShell)->FlushPendingNotifications(FlushType::Layout);
      return framesConstructedCount != presContext->FramesConstructedCount() ||
             framesReflowedCount != presContext->FramesReflowedCount();
    }
    default:
      return false;
  }
}

// The type of coordinates to use for hit-testing input events
// that are relative to the RCD's viewport frame.
// On most platforms, use visual coordinates so that scrollbars
// can be targeted.
// On mobile, use layout coordinates because hit-testing in
// visual coordinates clashes with mobile viewport sizing, where
// the ViewportFrame is sized to the initial containing block
// (ICB) size, which is in layout coordinates. This is fine
// because we don't need to be able to target scrollbars on mobile
// (scrollbar dragging isn't supported).
static ViewportType ViewportTypeForInputEventsRelativeToRoot() {
#ifdef MOZ_WIDGET_ANDROID
  return ViewportType::Layout;
#else
  return ViewportType::Visual;
#endif
}

nsIFrame* PresShell::EventHandler::GetFrameToHandleNonTouchEvent(
    AutoWeakFrame& aWeakRootFrameToHandleEvent, WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aGUIEvent->mClass != eTouchEventClass);

  if (MOZ_UNLIKELY(!aWeakRootFrameToHandleEvent.IsAlive())) {
    return nullptr;
  }

  ViewportType viewportType = ViewportType::Layout;
  if (aWeakRootFrameToHandleEvent->Type() == LayoutFrameType::Viewport) {
    nsPresContext* pc = aWeakRootFrameToHandleEvent->PresContext();
    if (pc->IsChrome()) {
      viewportType = ViewportType::Visual;
    } else if (pc->IsRootContentDocumentCrossProcess()) {
      viewportType = ViewportTypeForInputEventsRelativeToRoot();
    }
  }
  RelativeTo relativeTo{aWeakRootFrameToHandleEvent.GetFrame(), viewportType};
  nsPoint eventPoint =
      nsLayoutUtils::GetEventCoordinatesRelativeTo(aGUIEvent, relativeTo);

  uint32_t flags = 0;
  if (aGUIEvent->IsMouseEventClassOrHasClickRelatedPointerEvent()) {
    WidgetMouseEvent* mouseEvent = aGUIEvent->AsMouseEvent();
    if (mouseEvent && mouseEvent->mIgnoreRootScrollFrame) {
      flags |= INPUT_IGNORE_ROOT_SCROLL_FRAME;
    }
  }

  nsIFrame* targetFrame =
      FindFrameTargetedByInputEvent(aGUIEvent, relativeTo, eventPoint, flags);
  if (!targetFrame) {
    return aWeakRootFrameToHandleEvent.GetFrame();
  }

  if (targetFrame->PresShell() == mPresShell) {
    // If found target is in mPresShell, we've already found it in the latest
    // layout so that we can use it.
    return targetFrame;
  }

  // If target is in a child document, we've not flushed its layout yet.
  PresShell* childPresShell = targetFrame->PresShell();
  EventHandler childEventHandler(*childPresShell);
  bool layoutChanged =
      childEventHandler.MaybeFlushPendingNotifications(aGUIEvent);
  if (!aWeakRootFrameToHandleEvent.IsAlive()) {
    // Stop handling the event if the root frame to handle event is destroyed
    // by the reflow. (but why?)
    return nullptr;
  }
  if (!layoutChanged) {
    // If the layout in the child PresShell hasn't been changed, we don't
    // need to recompute the target.
    return targetFrame;
  }

  // Finally, we need to recompute the target with the latest layout.
  targetFrame =
      FindFrameTargetedByInputEvent(aGUIEvent, relativeTo, eventPoint, flags);

  return targetFrame ? targetFrame : aWeakRootFrameToHandleEvent.GetFrame();
}

bool PresShell::EventHandler::ComputeEventTargetFrameAndPresShellAtEventPoint(
    AutoWeakFrame& aWeakRootFrameToHandleEvent, WidgetGUIEvent* aGUIEvent,
    EventTargetData* aEventTargetData) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aEventTargetData);

  if (aGUIEvent->mClass == eTouchEventClass) {
    nsIFrame* targetFrameAtTouchEvent = TouchManager::SetupTarget(
        aGUIEvent->AsTouchEvent(), aWeakRootFrameToHandleEvent.GetFrame());
    aEventTargetData->SetFrameAndComputePresShell(targetFrameAtTouchEvent);
    return true;
  }

  nsIFrame* targetFrame =
      GetFrameToHandleNonTouchEvent(aWeakRootFrameToHandleEvent, aGUIEvent);
  aEventTargetData->SetFrameAndComputePresShell(targetFrame);
  return !!aEventTargetData->GetFrame();
}

bool PresShell::EventHandler::DispatchPrecedingPointerEvent(
    AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    nsIContent* aPointerCapturingContent, bool aDontRetargetEvents,
    EventTargetData* aEventTargetData, nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aEventTargetData);
  MOZ_ASSERT(aEventStatus);

  // Dispatch pointer events from the mouse or touch events. Regarding
  // pointer events from mouse, we should dispatch those pointer events to
  // the same target as the source mouse events. We pass the frame found
  // in hit test to PointerEventHandler and dispatch pointer events to it.
  //
  // Regarding pointer events from touch, the behavior is different. Touch
  // events are dispatched to the same target as the target of touchstart.
  // Multiple touch points must be dispatched to the same document. Pointer
  // events from touch can be dispatched to different documents. We Pass the
  // original frame to PointerEventHandler, reentry PresShell::HandleEvent,
  // and do hit test for each point.
  auto targetFrameOrError = [&]() -> Result<nsIFrame*, nsresult> {
    if (aGUIEvent->mClass == eTouchEventClass) {
      // If aWeakFrameForPresShell has already been reframed before this is
      // called, we don't need to handle the event.
      if (MOZ_UNLIKELY(!aWeakFrameForPresShell.IsAlive())) {
        return Err(NS_ERROR_FAILURE);
      }
      return aWeakFrameForPresShell.GetFrame();
    }
    return aEventTargetData->GetFrame();
  }();
  if (MOZ_UNLIKELY(targetFrameOrError.isErr())) {
    return false;
  }
  nsIFrame* targetFrame = targetFrameOrError.unwrap();

  if (aPointerCapturingContent) {
    Result<nsIContent*, nsresult> overrideClickTargetOrError =
        GetOverrideClickTarget(aGUIEvent, aWeakFrameForPresShell.GetFrame());
    if (MOZ_UNLIKELY(overrideClickTargetOrError.isErr())) {
      return false;
    }
    aEventTargetData->mOverrideClickTarget =
        overrideClickTargetOrError.unwrap();
    aEventTargetData->mPresShell =
        PresShell::GetShellForEventTarget(nullptr, aPointerCapturingContent);
    if (!aEventTargetData->mPresShell) {
      // If we can't process event for the capturing content, release
      // the capture.
      PointerEventHandler::ReleaseIfCaptureByDescendant(
          aPointerCapturingContent);
      return false;
    }

    targetFrame = aPointerCapturingContent->GetPrimaryFrame();
    aEventTargetData->SetFrameAndContent(targetFrame, aPointerCapturingContent);
  }

  AutoWeakFrame weakTargetFrame(targetFrame);
  AutoWeakFrame weakFrame(aEventTargetData->GetFrame());
  nsCOMPtr<nsIContent> pointerEventTargetContent(
      aEventTargetData->GetContent());
  RefPtr<PresShell> presShell(aEventTargetData->mPresShell);
  nsCOMPtr<nsIContent> mouseOrTouchEventTargetContent;
  PointerEventHandler::DispatchPointerFromMouseOrTouch(
      presShell, aEventTargetData->GetFrame(), pointerEventTargetContent,
      aGUIEvent, aDontRetargetEvents, aEventStatus,
      getter_AddRefs(mouseOrTouchEventTargetContent));

  // If the target frame is alive, the caller should keep handling the event
  // unless event target frame is destroyed.
  if (weakTargetFrame.IsAlive() && weakFrame.IsAlive()) {
    aEventTargetData->UpdateTouchEventTarget(aGUIEvent);
    return true;
  }

  presShell->FlushPendingNotifications(FlushType::Layout);
  if (MOZ_UNLIKELY(mPresShell->IsDestroying())) {
    return false;
  }

  // The spec defines that mouse events must be dispatched to the same target as
  // the pointer event.
  // The Touch Events spec defines that touch events must be dispatched to the
  // same target as touch start and the other browsers dispatch touch events
  // even if the touch event target is not connected to the document.
  // Retargetting the event is handled by AutoPointerEventTargetUpdater and
  // mouseOrTouchEventTargetContent stores the result.

  // If the target is no longer participating in its ownerDocument's tree,
  // fire the event at the original target's nearest ancestor node.
  if (!mouseOrTouchEventTargetContent) {
    MOZ_ASSERT(aGUIEvent->IsMouseEventClassOrHasClickRelatedPointerEvent());
    return false;
  }

  aEventTargetData->SetFrameAndContent(
      mouseOrTouchEventTargetContent->GetPrimaryFrame(),
      mouseOrTouchEventTargetContent);
  aEventTargetData->mPresShell =
      mouseOrTouchEventTargetContent->IsInComposedDoc()
          ? PresShell::GetShellForEventTarget(aEventTargetData->GetFrame(),
                                              aEventTargetData->GetContent())
          : mouseOrTouchEventTargetContent->OwnerDoc()->GetPresShell();

  // If new target PresShel is not found, we cannot keep handling the event.
  if (!aEventTargetData->mPresShell) {
    return false;
  }

  aEventTargetData->UpdateTouchEventTarget(aGUIEvent);
  return true;
}

/**
 * Event retargetting may retarget a mouse event and change the reference point.
 * If event retargetting changes the reference point of a event that accessible
 * caret will not handle, restore the original reference point.
 */
class AutoEventTargetPointResetter {
 public:
  explicit AutoEventTargetPointResetter(WidgetGUIEvent* aGUIEvent)
      : mGUIEvent(aGUIEvent),
        mRefPoint(aGUIEvent->mRefPoint),
        mHandledByAccessibleCaret(false) {}

  void SetHandledByAccessibleCaret() { mHandledByAccessibleCaret = true; }

  ~AutoEventTargetPointResetter() {
    if (!mHandledByAccessibleCaret) {
      mGUIEvent->mRefPoint = mRefPoint;
    }
  }

 private:
  WidgetGUIEvent* mGUIEvent;
  LayoutDeviceIntPoint mRefPoint;
  bool mHandledByAccessibleCaret;
};

bool PresShell::EventHandler::MaybeHandleEventWithAccessibleCaret(
    AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aEventStatus);

  // Don't dispatch event to AccessibleCaretEventHub when the event status
  // is nsEventStatus_eConsumeNoDefault. This might be happened when content
  // preventDefault on the pointer events. In such case, we also call
  // preventDefault on mouse events to stop default behaviors.
  if (*aEventStatus == nsEventStatus_eConsumeNoDefault) {
    return false;
  }

  if (!AccessibleCaretEnabled(GetDocument()->GetDocShell())) {
    return false;
  }

  // AccessibleCaretEventHub handles only mouse, touch, and keyboard events.
  if (!aGUIEvent->IsMouseEventClassOrHasClickRelatedPointerEvent() &&
      aGUIEvent->mClass != eTouchEventClass &&
      aGUIEvent->mClass != eKeyboardEventClass) {
    return false;
  }

  AutoEventTargetPointResetter autoEventTargetPointResetter(aGUIEvent);
  // First, try the event hub at the event point to handle a long press to
  // select a word in an unfocused window.
  do {
    EventTargetData eventTargetData(nullptr);
    if (!ComputeEventTargetFrameAndPresShellAtEventPoint(
            aWeakFrameForPresShell, aGUIEvent, &eventTargetData)) {
      break;
    }

    if (!eventTargetData.mPresShell) {
      break;
    }

    RefPtr<AccessibleCaretEventHub> eventHub =
        eventTargetData.mPresShell->GetAccessibleCaretEventHub();
    if (!eventHub) {
      break;
    }

    *aEventStatus = eventHub->HandleEvent(aGUIEvent);
    if (*aEventStatus != nsEventStatus_eConsumeNoDefault) {
      break;
    }

    // If the event is consumed, cancel APZC panning by setting
    // mMultipleActionsPrevented.
    aGUIEvent->mFlags.mMultipleActionsPrevented = true;
    autoEventTargetPointResetter.SetHandledByAccessibleCaret();
    return true;
  } while (false);

  // Then, we target the event to the event hub at the focused window.
  nsCOMPtr<nsPIDOMWindowOuter> window = GetFocusedDOMWindowInOurWindow();
  if (!window) {
    return false;
  }
  RefPtr<Document> retargetEventDoc = window->GetExtantDoc();
  if (!retargetEventDoc) {
    return false;
  }
  RefPtr<PresShell> presShell = retargetEventDoc->GetPresShell();
  if (!presShell) {
    return false;
  }

  RefPtr<AccessibleCaretEventHub> eventHub =
      presShell->GetAccessibleCaretEventHub();
  if (!eventHub) {
    return false;
  }
  *aEventStatus = eventHub->HandleEvent(aGUIEvent);
  if (*aEventStatus != nsEventStatus_eConsumeNoDefault) {
    return false;
  }
  // If the event is consumed, cancel APZC panning by setting
  // mMultipleActionsPrevented.
  aGUIEvent->mFlags.mMultipleActionsPrevented = true;
  autoEventTargetPointResetter.SetHandledByAccessibleCaret();
  return true;
}

void PresShell::EventHandler::MaybeSynthesizeCompatMouseEventsForTouchEnd(
    const WidgetTouchEvent* aTouchEndEvent,
    const nsEventStatus* aStatus) const {
  MOZ_ASSERT(aTouchEndEvent->mMessage == eTouchEnd);

  // If the eTouchEnd event is dispatched via APZ, APZCCallbackHelper dispatches
  // a set of mouse events with better handling.  Therefore, we don't need to
  // handle that here.
  if (!aTouchEndEvent->mFlags.mIsSynthesizedForTests ||
      StaticPrefs::test_events_async_enabled()) {
    return;
  }

  // If the tap was consumed or 2 or more touches occurred, we don't need the
  // compatibility mouse events.
  if (*aStatus == nsEventStatus_eConsumeNoDefault ||
      !TouchManager::IsSingleTapEndToDoDefault(aTouchEndEvent)) {
    return;
  }

  if (NS_WARN_IF(!aTouchEndEvent->mWidget)) {
    return;
  }

  nsCOMPtr<nsIWidget> widget = aTouchEndEvent->mWidget;

  // NOTE: I think that we don't need to implement a double click here becase
  // WebDriver does not support a way to synthesize a double click and Chrome
  // does not fire "dblclick" even if doing `pointerDown().pointerUp()` twice.
  // FIXME: Currently we don't support long tap.
  RefPtr<PresShell> presShell = mPresShell;
  for (const EventMessage message : {eMouseMove, eMouseDown, eMouseUp}) {
    if (MOZ_UNLIKELY(presShell->IsDestroying())) {
      break;
    }
    nsIFrame* const frameForPresShell =
        presShell->GetClosestAncestorFrameForAncestorView();
    if (!frameForPresShell) {
      break;
    }
    WidgetMouseEvent event(true, message, widget, WidgetMouseEvent::eReal);
    event.mRefPoint = aTouchEndEvent->mTouches[0]->mRefPoint;
    event.mButton = MouseButton::ePrimary;
    event.mButtons = message == eMouseDown ? MouseButtonsFlag::ePrimaryFlag
                                           : MouseButtonsFlag::eNoButtons;
    event.mInputSource = MouseEvent_Binding::MOZ_SOURCE_TOUCH;
    event.mClickCount = message == eMouseMove ? 0 : 1;
    event.mModifiers = aTouchEndEvent->mModifiers;
    event.convertToPointer = false;
    if (TouchManager::IsPrecedingTouchPointerDownConsumedByContent()) {
      event.PreventDefault(false);
      event.mFlags.mOnlyChromeDispatch = true;
    }
    nsEventStatus mouseEventStatus = nsEventStatus_eIgnore;
    presShell->HandleEvent(frameForPresShell, &event, false, &mouseEventStatus);
  }
}

bool PresShell::EventHandler::MaybeDiscardEvent(WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);

  // If it is safe to dispatch events now, don't discard the event.
  if (nsContentUtils::IsSafeToRunScript()) {
    return false;
  }

  // If the event does not cause dispatching DOM event (i.e., internal event),
  // we can keep handling it even when it's not safe to run script.
  if (!aGUIEvent->IsAllowedToDispatchDOMEvent()) {
    return false;
  }

  // If the event is a composition event, we need to let IMEStateManager know
  // it's discarded because it needs to listen all composition events to manage
  // TextComposition instance.
  if (aGUIEvent->mClass == eCompositionEventClass) {
    IMEStateManager::OnCompositionEventDiscarded(
        aGUIEvent->AsCompositionEvent());
  }

#ifdef DEBUG
  if (aGUIEvent->IsIMERelatedEvent()) {
    nsPrintfCString warning("%s event is discarded",
                            ToChar(aGUIEvent->mMessage));
    NS_WARNING(warning.get());
  }
#endif  // #ifdef DEBUG

  nsContentUtils::WarnScriptWasIgnored(GetDocument());
  return true;
}

// static
nsIContent* PresShell::EventHandler::GetCapturingContentFor(
    WidgetGUIEvent* aGUIEvent) {
  if (aGUIEvent->mClass != ePointerEventClass &&
      aGUIEvent->mClass != eWheelEventClass &&
      !aGUIEvent->HasMouseEventMessage()) {
    return nullptr;
  }

  // PointerEventHandler may synthesize ePointerMove event before releasing the
  // mouse capture (it's done by a default handler of eMouseUp) after handling
  // ePointerUp.  Then, we need to dispatch pointer boundary events for the
  // element under the pointer to emulate a pointer move after a pointer
  // capture.  Therefore, we need to ignore the capturing element if the event
  // dispatcher requests it.
  if (aGUIEvent->ShouldIgnoreCapturingContent()) {
    return nullptr;
  }

  return PresShell::GetCapturingContent();
}

bool PresShell::EventHandler::GetRetargetEventDocument(
    WidgetGUIEvent* aGUIEvent, Document** aRetargetEventDocument) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aRetargetEventDocument);

  *aRetargetEventDocument = nullptr;

  // key and IME related events should not cross top level window boundary.
  // Basically, such input events should be fired only on focused widget.
  // However, some IMEs might need to clean up composition after focused
  // window is deactivated.  And also some tests on MozMill want to test key
  // handling on deactivated window because MozMill window can be activated
  // during tests.  So, there is no merit the events should be redirected to
  // active window.  So, the events should be handled on the last focused
  // content in the last focused DOM window in same top level window.
  // Note, if no DOM window has been focused yet, we can discard the events.
  if (aGUIEvent->IsTargetedAtFocusedWindow()) {
    nsCOMPtr<nsPIDOMWindowOuter> window = GetFocusedDOMWindowInOurWindow();
    // No DOM window in same top level window has not been focused yet,
    // discard the events.
    if (!window) {
      return false;
    }

    RefPtr<Document> retargetEventDoc = window->GetExtantDoc();
    if (!retargetEventDoc) {
      return false;
    }
    retargetEventDoc.forget(aRetargetEventDocument);
    return true;
  }

  const nsIContent* const capturingContent =
      aGUIEvent->ShouldIgnoreCapturingContent()
          ? nullptr
          : EventHandler::GetCapturingContentFor(aGUIEvent);
  if (capturingContent) {
    // if the mouse is being captured then retarget the mouse event at the
    // document that is being captured.
    RefPtr<Document> retargetEventDoc = capturingContent->GetComposedDoc();
    retargetEventDoc.forget(aRetargetEventDocument);
    return true;
  }

#ifdef ANDROID
  if (aGUIEvent->mClass == eTouchEventClass ||
      aGUIEvent->IsMouseEventClassOrHasClickRelatedPointerEvent() ||
      aGUIEvent->mClass == eWheelEventClass) {
    RefPtr<Document> retargetEventDoc = mPresShell->GetPrimaryContentDocument();
    retargetEventDoc.forget(aRetargetEventDocument);
    return true;
  }
#endif  // #ifdef ANDROID

  // When we don't find another document to handle the event, we need to keep
  // handling the event by ourselves.
  return true;
}

nsIFrame* PresShell::EventHandler::GetFrameForHandlingEventWith(
    WidgetGUIEvent* aGUIEvent, Document* aRetargetDocument,
    nsIFrame* aFrameForPresShell) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aRetargetDocument);

  RefPtr<PresShell> retargetPresShell = aRetargetDocument->GetPresShell();
  // Even if the document doesn't have PresShell, i.e., it's invisible, we
  // need to dispatch only KeyboardEvent in its nearest visible document
  // because key focus shouldn't be caught by invisible document.
  if (!retargetPresShell) {
    if (!aGUIEvent->HasKeyEventMessage()) {
      return nullptr;
    }
    Document* retargetEventDoc = aRetargetDocument;
    while (!retargetPresShell) {
      retargetEventDoc = retargetEventDoc->GetInProcessParentDocument();
      if (!retargetEventDoc) {
        return nullptr;
      }
      retargetPresShell = retargetEventDoc->GetPresShell();
    }
  }

  // If the found PresShell is this instance, caller needs to keep handling
  // aGUIEvent by itself.  Therefore, return the given frame which was set
  // to aFrame of HandleEvent().
  if (retargetPresShell == mPresShell) {
    return aFrameForPresShell;
  }

  // Use root frame of the new PresShell if there is.
  nsIFrame* rootFrame = retargetPresShell->GetRootFrame();
  if (rootFrame) {
    return rootFrame;
  }

  // Otherwise, and if aGUIEvent requires content of PresShell, caller should
  // stop handling the event.
  if (aGUIEvent->mMessage == eQueryTextContent ||
      aGUIEvent->IsContentCommandEvent()) {
    return nullptr;
  }

  // Otherwise, use nearest ancestor frame which includes the PresShell.
  return retargetPresShell->GetClosestAncestorFrameForAncestorView();
}

bool PresShell::EventHandler::MaybeHandleEventWithAnotherPresShell(
    AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    nsEventStatus* aEventStatus, nsresult* aRv) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aEventStatus);
  MOZ_ASSERT(aRv);

  *aRv = NS_OK;

  RefPtr<Document> retargetEventDoc;
  if (!GetRetargetEventDocument(aGUIEvent, getter_AddRefs(retargetEventDoc))) {
    // Nobody can handle this event.  So, treat as handled by somebody to make
    // caller do nothing anymore.
    return true;
  }

  // If there is no proper retarget document, the caller should handle the
  // event by itself.
  if (!retargetEventDoc) {
    return false;
  }

  nsIFrame* frame = GetFrameForHandlingEventWith(
      aGUIEvent, retargetEventDoc, aWeakFrameForPresShell.GetFrame());
  if (!frame) {
    // Nobody can handle this event.  So, treat as handled by somebody to make
    // caller do nothing anymore.
    // NOTE: If aWeakFrameForPresShell does not refer a frame (i.e., it's
    // already been reframed) and aGUIEvent needs to be handled in mPresShell,
    // we are here because GetFrameForHandlingEventWith() returns
    // aWeakFrameForPresShell.GetFrame() as-is. In the case, we don't need to
    // handle aGUIEvent, so, it's fine to return true from this method.
    return true;
  }

  // If we reached same frame as set to HandleEvent(), the caller should handle
  // the event by itself.
  if (frame == aWeakFrameForPresShell.GetFrame()) {
    return false;
  }

  // We need to handle aGUIEvent with another PresShell.
  RefPtr<PresShell> presShell = frame->PresContext()->PresShell();
  *aRv = presShell->HandleEvent(frame, aGUIEvent, true, aEventStatus);
  return true;
}

bool PresShell::EventHandler::MaybeDiscardOrDelayKeyboardEvent(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);

  if (aGUIEvent->mClass != eKeyboardEventClass) {
    return false;
  }

  Document* document = GetDocument();
  if (!document || !document->EventHandlingSuppressed()) {
    return false;
  }

  MOZ_ASSERT_IF(InputTaskManager::CanSuspendInputEvent(),
                !InputTaskManager::Get()->IsSuspended());

  if (aGUIEvent->mMessage == eKeyDown) {
    mPresShell->mNoDelayedKeyEvents = true;
  } else if (!mPresShell->mNoDelayedKeyEvents) {
    UniquePtr<DelayedKeyEvent> delayedKeyEvent =
        MakeUnique<DelayedKeyEvent>(aGUIEvent->AsKeyboardEvent());
    mPresShell->mDelayedEvents.AppendElement(std::move(delayedKeyEvent));
  }
  aGUIEvent->mFlags.mIsSuppressedOrDelayed = true;
  return true;
}

bool PresShell::EventHandler::MaybeDiscardOrDelayMouseEvent(
    nsIFrame* aFrameToHandleEvent, WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aFrameToHandleEvent);
  MOZ_ASSERT(aGUIEvent);

  if (!aGUIEvent->IsMouseEventClassOrHasClickRelatedPointerEvent()) {
    return false;
  }

  if (!aFrameToHandleEvent->PresContext()
           ->Document()
           ->EventHandlingSuppressed()) {
    return false;
  }

  MOZ_ASSERT_IF(InputTaskManager::CanSuspendInputEvent() &&
                    aGUIEvent->mMessage != eMouseMove,
                !InputTaskManager::Get()->IsSuspended());

  RefPtr<PresShell> ps = aFrameToHandleEvent->PresShell();

  if (aGUIEvent->mMessage == eMouseDown) {
    ps->mNoDelayedMouseEvents = true;
  } else if (!ps->mNoDelayedMouseEvents) {
    if (aGUIEvent->mMessage == eMouseUp ||
        aGUIEvent->mMessage == eMouseExitFromWidget) {
      UniquePtr<DelayedMouseEvent> delayedMouseEvent =
          MakeUnique<DelayedMouseEvent>(aGUIEvent->AsMouseEvent());
      ps->mDelayedEvents.AppendElement(std::move(delayedMouseEvent));
    }
    // contextmenu is triggered after right mouseup on Windows and right
    // mousedown on other platforms.
    else if (aGUIEvent->mMessage == eContextMenu) {
      UniquePtr<DelayedPointerEvent> delayedPointerEvent =
          MakeUnique<DelayedPointerEvent>(aGUIEvent->AsPointerEvent());
      ps->mDelayedEvents.AppendElement(std::move(delayedPointerEvent));
    }
  }

  // If there is a suppressed event listener associated with the document,
  // notify it about the suppressed mouse event. This allows devtools
  // features to continue receiving mouse events even when the devtools
  // debugger has paused execution in a page.
  RefPtr<EventListener> suppressedListener = aFrameToHandleEvent->PresContext()
                                                 ->Document()
                                                 ->GetSuppressedEventListener();
  if (!suppressedListener ||
      aGUIEvent->AsMouseEvent()->mReason == WidgetMouseEvent::eSynthesized) {
    return true;
  }

  if (auto* target = aFrameToHandleEvent->GetContentForEvent(aGUIEvent)) {
    aGUIEvent->mTarget = target;
  }

  nsCOMPtr<EventTarget> eventTarget = aGUIEvent->mTarget;
  RefPtr<Event> event = EventDispatcher::CreateEvent(
      eventTarget, aFrameToHandleEvent->PresContext(), aGUIEvent, u""_ns);

  suppressedListener->HandleEvent(*event);
  return true;
}

void PresShell::EventHandler::MaybeFlushThrottledStyles(
    AutoWeakFrame& aWeakFrameForPresShell) {
  if (!GetDocument()) {
    return;
  }

  PresShell* rootPresShell = mPresShell->GetRootPresShell();
  if (NS_WARN_IF(!rootPresShell)) {
    return;
  }
  Document* rootDocument = rootPresShell->GetDocument();
  if (NS_WARN_IF(!rootDocument)) {
    return;
  }

  {  // scope for scriptBlocker.
    nsAutoScriptBlocker scriptBlocker;
    FlushThrottledStyles(*rootDocument);
  }

  if (MOZ_UNLIKELY(!aWeakFrameForPresShell.IsAlive()) &&
      MOZ_LIKELY(!mPresShell->IsDestroying())) {
    // FIXME: aWeakFrameForPresShell may be target content's frame if aGUIEvent
    // of the caller is a touch event.  So, we need to use different computation
    // for such cases.
    aWeakFrameForPresShell =
        mPresShell->GetClosestAncestorFrameForAncestorView();
  }
}

nsIFrame* PresShell::EventHandler::ComputeRootFrameToHandleEvent(
    nsIFrame* aFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    nsIContent* aCapturingContent, bool* aIsCapturingContentIgnored,
    bool* aIsCaptureRetargeted) {
  MOZ_ASSERT(aFrameForPresShell);
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aIsCapturingContentIgnored);
  MOZ_ASSERT(aIsCaptureRetargeted);

  nsIFrame* rootFrameToHandleEvent = ComputeRootFrameToHandleEventWithPopup(
      aFrameForPresShell, aGUIEvent, aCapturingContent,
      aIsCapturingContentIgnored);
  if (*aIsCapturingContentIgnored) {
    // If the capturing content is ignored, we don't need to respect it.
    return rootFrameToHandleEvent;
  }

  if (!aCapturingContent) {
    return rootFrameToHandleEvent;
  }

  // If we have capturing content, let's compute root frame with it again.
  return ComputeRootFrameToHandleEventWithCapturingContent(
      rootFrameToHandleEvent, aCapturingContent, aIsCapturingContentIgnored,
      aIsCaptureRetargeted);
}

nsIFrame* PresShell::EventHandler::ComputeRootFrameToHandleEventWithPopup(
    nsIFrame* aRootFrameToHandleEvent, WidgetGUIEvent* aGUIEvent,
    nsIContent* aCapturingContent, bool* aIsCapturingContentIgnored) {
  MOZ_ASSERT(aRootFrameToHandleEvent);
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aIsCapturingContentIgnored);

  *aIsCapturingContentIgnored = false;

  nsPresContext* framePresContext = aRootFrameToHandleEvent->PresContext();
  nsPresContext* rootPresContext = framePresContext->GetRootPresContext();
  NS_ASSERTION(rootPresContext == GetPresContext()->GetRootPresContext(),
               "How did we end up outside the connected "
               "prescontext/viewmanager hierarchy?");
  nsIFrame* popupFrame = nsLayoutUtils::GetPopupFrameForEventCoordinates(
      rootPresContext, aGUIEvent);
  if (!popupFrame) {
    return aRootFrameToHandleEvent;
  }

  // If a remote browser is currently capturing input break out if we
  // detect a chrome generated popup.
  // XXXedgar, do we need to check fission OOP iframe?
  if (aCapturingContent &&
      EventStateManager::IsTopLevelRemoteTarget(aCapturingContent)) {
    *aIsCapturingContentIgnored = true;
  }

  // If the popupFrame is an ancestor of the 'frame', the frame should
  // handle the event, otherwise, the popup should handle it.
  if (nsContentUtils::ContentIsCrossDocDescendantOf(
          framePresContext->GetPresShell()->GetDocument(),
          popupFrame->GetContent())) {
    return aRootFrameToHandleEvent;
  }

  // If we aren't starting our event dispatch from the root frame of the
  // root prescontext, then someone must be capturing the mouse. In that
  // case we only want to use the popup list if the capture is
  // inside the popup.
  if (framePresContext == rootPresContext &&
      aRootFrameToHandleEvent == FrameConstructor()->GetRootFrame()) {
    return popupFrame;
  }

  if (aCapturingContent && !*aIsCapturingContentIgnored &&
      aCapturingContent->IsInclusiveDescendantOf(popupFrame->GetContent())) {
    return popupFrame;
  }

  return aRootFrameToHandleEvent;
}

nsIFrame*
PresShell::EventHandler::ComputeRootFrameToHandleEventWithCapturingContent(
    nsIFrame* aRootFrameToHandleEvent, nsIContent* aCapturingContent,
    bool* aIsCapturingContentIgnored, bool* aIsCaptureRetargeted) {
  MOZ_ASSERT(aRootFrameToHandleEvent);
  MOZ_ASSERT(aCapturingContent);
  MOZ_ASSERT(aIsCapturingContentIgnored);
  MOZ_ASSERT(aIsCaptureRetargeted);

  *aIsCapturingContentIgnored = false;
  *aIsCaptureRetargeted = false;

  // If a capture is active, determine if the BrowsingContext is active. If
  // not, clear the capture and target the mouse event normally instead. This
  // would occur if the mouse button is held down while a tab change occurs.
  // If the BrowsingContext is active, look for a scrolling container.
  BrowsingContext* bc = GetPresContext()->Document()->GetBrowsingContext();
  if (!bc || !bc->IsActive()) {
    ClearMouseCapture();
    *aIsCapturingContentIgnored = true;
    return aRootFrameToHandleEvent;
  }

  if (PresShell::sCapturingContentInfo.mRetargetToElement) {
    *aIsCaptureRetargeted = true;
    return aRootFrameToHandleEvent;
  }

  nsIFrame* captureFrame = aCapturingContent->GetPrimaryFrame();
  if (!captureFrame) {
    return aRootFrameToHandleEvent;
  }

  // scrollable frames should use the scrolling container as the root instead
  // of the document
  ScrollContainerFrame* scrollFrame = do_QueryFrame(captureFrame);
  return scrollFrame ? scrollFrame->GetScrolledFrame()
                     : aRootFrameToHandleEvent;
}

nsresult
PresShell::EventHandler::HandleEventWithPointerCapturingContentWithoutItsFrame(
    AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    nsIContent* aPointerCapturingContent, nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aPointerCapturingContent);
  MOZ_ASSERT(!aPointerCapturingContent->GetPrimaryFrame(),
             "Handle the event with frame rather than only with the content");
  MOZ_ASSERT(aEventStatus);

  RefPtr<PresShell> presShellForCapturingContent =
      PresShell::GetShellForEventTarget(nullptr, aPointerCapturingContent);
  if (!presShellForCapturingContent) {
    // If we can't process event for the capturing content, release
    // the capture.
    PointerEventHandler::ReleaseIfCaptureByDescendant(aPointerCapturingContent);
    // Since we don't dispatch ePointeUp nor ePointerCancel in this case,
    // EventStateManager::PostHandleEvent does not have a chance to dispatch
    // ePointerLostCapture event.  Therefore, we need to dispatch it here.
    PointerEventHandler::MaybeImplicitlyReleasePointerCapture(aGUIEvent);
    return NS_OK;
  }

  Result<nsIContent*, nsresult> overrideClickTargetOrError =
      GetOverrideClickTarget(aGUIEvent, aWeakFrameForPresShell.GetFrame());
  if (MOZ_UNLIKELY(overrideClickTargetOrError.isErr())) {
    return NS_OK;
  }
  nsCOMPtr<nsIContent> overrideClickTarget =
      overrideClickTargetOrError.unwrap();

  // Dispatch events to the capturing content even it's frame is
  // destroyed.
  PointerEventHandler::DispatchPointerFromMouseOrTouch(
      presShellForCapturingContent, nullptr, aPointerCapturingContent,
      aGUIEvent, false, aEventStatus, nullptr);

  if (presShellForCapturingContent == mPresShell) {
    return HandleEventWithTarget(aGUIEvent, nullptr, aPointerCapturingContent,
                                 aEventStatus, true, nullptr,
                                 overrideClickTarget);
  }

  EventHandler eventHandlerForCapturingContent(
      std::move(presShellForCapturingContent));
  return eventHandlerForCapturingContent.HandleEventWithTarget(
      aGUIEvent, nullptr, aPointerCapturingContent, aEventStatus, true, nullptr,
      overrideClickTarget);
}

nsresult PresShell::EventHandler::HandleEventAtFocusedContent(
    WidgetGUIEvent* aGUIEvent, nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aGUIEvent->IsTargetedAtFocusedContent());
  MOZ_ASSERT(aEventStatus);

  AutoCurrentEventInfoSetter eventInfoSetter(*this);

  RefPtr<Element> eventTargetElement =
      ComputeFocusedEventTargetElement(aGUIEvent);

  // mCurrentEventTarget is cleared by eventInfoSetter and
  // ComputeFocusedEventTargetElement shouldn't set it again.
  MOZ_ASSERT(!mPresShell->mCurrentEventTarget.IsSet());

  if (eventTargetElement) {
    nsresult rv = NS_OK;
    if (MaybeHandleEventWithAnotherPresShell(eventTargetElement, aGUIEvent,
                                             aEventStatus, &rv)) {
      return rv;
    }
  }

  // If we cannot handle the event with mPresShell, let's try to handle it
  // with parent PresShell.
  // However, we don't want to handle IME related events with parent document
  // because it may leak the content of parent document and the IME state was
  // set for the empty document.  So, dispatching on the parent document may be
  // handled by nobody. Additionally, IMEContentObserver may send notifications
  // to PuppetWidget in a content process while document which is in the design
  // mode but does not have content nodes has focus.  At that time, PuppetWidget
  // makes ContentCacheInChild collect the latest content data with dispatching
  // query content events.  Therefore, we want they handle in the empty document
  // rather than the parent document.  So, we must not retarget in this case
  // anyway.
  mPresShell->mCurrentEventTarget.SetFrameAndContent(
      aGUIEvent->mMessage, nullptr, eventTargetElement);
  if (aGUIEvent->mClass != eCompositionEventClass &&
      aGUIEvent->mClass != eQueryContentEventClass &&
      aGUIEvent->mClass != eSelectionEventClass &&
      (!mPresShell->GetCurrentEventContent() ||
       !mPresShell->GetCurrentEventFrame() ||
       InZombieDocument(mPresShell->mCurrentEventTarget.mContent))) {
    return RetargetEventToParent(aGUIEvent, aEventStatus);
  }

  nsresult rv =
      HandleEventWithCurrentEventInfo(aGUIEvent, aEventStatus, true, nullptr);
  return rv;
}

Element* PresShell::EventHandler::ComputeFocusedEventTargetElement(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aGUIEvent->IsTargetedAtFocusedContent());

  // key and IME related events go to the focused frame in this DOM window.
  nsPIDOMWindowOuter* window = GetDocument()->GetWindow();
  nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
  Element* eventTargetElement = nsFocusManager::GetFocusedDescendant(
      window, nsFocusManager::eOnlyCurrentWindow,
      getter_AddRefs(focusedWindow));

  // otherwise, if there is no focused content or the focused content has
  // no frame, just use the root content. This ensures that key events
  // still get sent to the window properly if nothing is focused or if a
  // frame goes away while it is focused.
  if (!eventTargetElement || !eventTargetElement->GetPrimaryFrame()) {
    eventTargetElement = GetDocument()->GetUnfocusedKeyEventTarget();
  }

  switch (aGUIEvent->mMessage) {
    case eKeyDown:
      sLastKeyDownEventTargetElement = eventTargetElement;
      return eventTargetElement;
    case eKeyPress:
    case eKeyUp:
      if (!sLastKeyDownEventTargetElement) {
        return eventTargetElement;
      }
      // If a different element is now focused for the keypress/keyup event
      // than what was focused during the keydown event, check if the new
      // focused element is not in a chrome document any more, and if so,
      // retarget the event back at the keydown target. This prevents a
      // content area from grabbing the focus from chrome in-between key
      // events.
      if (eventTargetElement) {
        bool keyDownIsChrome = nsContentUtils::IsChromeDoc(
            sLastKeyDownEventTargetElement->GetComposedDoc());
        if (keyDownIsChrome != nsContentUtils::IsChromeDoc(
                                   eventTargetElement->GetComposedDoc()) ||
            (keyDownIsChrome && BrowserParent::GetFrom(eventTargetElement))) {
          eventTargetElement = sLastKeyDownEventTargetElement;
        }
      }

      if (aGUIEvent->mMessage == eKeyUp) {
        sLastKeyDownEventTargetElement = nullptr;
      }
      [[fallthrough]];
    default:
      return eventTargetElement;
  }
}

bool PresShell::EventHandler::MaybeHandleEventWithAnotherPresShell(
    Element* aEventTargetElement, WidgetGUIEvent* aGUIEvent,
    nsEventStatus* aEventStatus, nsresult* aRv) {
  MOZ_ASSERT(aEventTargetElement);
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(!aGUIEvent->IsUsingCoordinates());
  MOZ_ASSERT(aEventStatus);
  MOZ_ASSERT(aRv);

  Document* eventTargetDocument = aEventTargetElement->OwnerDoc();
  if (!eventTargetDocument || eventTargetDocument == GetDocument()) {
    *aRv = NS_OK;
    return false;
  }

  RefPtr<PresShell> eventTargetPresShell = eventTargetDocument->GetPresShell();
  if (!eventTargetPresShell) {
    *aRv = NS_OK;
    return true;  // No PresShell can handle the event.
  }

  EventHandler eventHandler(std::move(eventTargetPresShell));
  *aRv = eventHandler.HandleRetargetedEvent(aGUIEvent, aEventStatus,
                                            aEventTargetElement);
  return true;
}

nsresult PresShell::EventHandler::HandleEventWithFrameForPresShell(
    AutoWeakFrame& aWeakFrameForPresShell, WidgetGUIEvent* aGUIEvent,
    nsEventStatus* aEventStatus) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(!aGUIEvent->IsUsingCoordinates());
  MOZ_ASSERT(!aGUIEvent->IsTargetedAtFocusedContent());
  MOZ_ASSERT(aEventStatus);

  // If aWeakFrameForPresShell has already been reframed before this is called,
  // we don't need to handle the event.
  if (MOZ_UNLIKELY(!aWeakFrameForPresShell.IsAlive())) {
    return NS_OK;
  }

  AutoCurrentEventInfoSetter eventInfoSetter(
      *this, EventTargetInfo(aGUIEvent->mMessage,
                             aWeakFrameForPresShell.GetFrame(), nullptr));

  nsresult rv = NS_OK;
  if (mPresShell->GetCurrentEventFrame()) {
    rv =
        HandleEventWithCurrentEventInfo(aGUIEvent, aEventStatus, true, nullptr);
  }

  return rv;
}

Document* PresShell::GetPrimaryContentDocument() {
  nsPresContext* context = GetPresContext();
  if (!context || !context->IsRoot()) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShellTreeItem> shellAsTreeItem = context->GetDocShell();
  if (!shellAsTreeItem) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShellTreeOwner> owner;
  shellAsTreeItem->GetTreeOwner(getter_AddRefs(owner));
  if (!owner) {
    return nullptr;
  }

  // now get the primary content shell (active tab)
  nsCOMPtr<nsIDocShellTreeItem> item;
  owner->GetPrimaryContentShell(getter_AddRefs(item));
  nsCOMPtr<nsIDocShell> childDocShell = do_QueryInterface(item);
  if (!childDocShell) {
    return nullptr;
  }

  return childDocShell->GetExtantDocument();
}

nsresult PresShell::EventHandler::HandleEventWithTarget(
    WidgetEvent* aEvent, nsIFrame* aNewEventFrame, nsIContent* aNewEventContent,
    nsEventStatus* aEventStatus, bool aIsHandlingNativeEvent,
    nsIContent** aTargetContent, nsIContent* aOverrideClickTarget) {
  MOZ_ASSERT(aEvent);
  MOZ_DIAGNOSTIC_ASSERT(aEvent->IsTrusted());
  MOZ_ASSERT(!aNewEventFrame || aNewEventFrame->PresShell() == mPresShell,
             "wrong shell");
  // NOTE: We don't require that the document still have a PresShell.
  // See bug 1375940.
  NS_ASSERTION(!aNewEventContent || aNewEventContent->IsInComposedDoc(),
               "event for content that isn't in a document");
  NS_ENSURE_STATE(!aNewEventContent ||
                  aNewEventContent->GetComposedDoc() == GetDocument());
  if (aEvent->mClass == ePointerEventClass ||
      aEvent->mClass == eDragEventClass) {
    mPresShell->RecordPointerLocation(aEvent->AsMouseEvent());
  }
  AutoPointerEventTargetUpdater updater(mPresShell, aEvent, aNewEventFrame,
                                        aNewEventContent, aTargetContent);
  AutoCurrentEventInfoSetter eventInfoSetter(
      *this,
      EventTargetInfo(aEvent->mMessage, aNewEventFrame, aNewEventContent));
  nsresult rv = HandleEventWithCurrentEventInfo(aEvent, aEventStatus, false,
                                                aOverrideClickTarget);
  return rv;
}

namespace {

class MOZ_RAII AutoEventHandler final {
 public:
  AutoEventHandler(WidgetEvent* aEvent, Document* aDocument) : mEvent(aEvent) {
    MOZ_ASSERT(mEvent);
    MOZ_ASSERT(mEvent->IsTrusted());

    if (mEvent->mMessage == eMouseDown) {
      PresShell::ReleaseCapturingContent();
      PresShell::AllowMouseCapture(true);
    }
    if (NeedsToUpdateCurrentMouseBtnState()) {
      WidgetMouseEvent* mouseEvent = mEvent->AsMouseEvent();
      if (mouseEvent) {
        EventStateManager::sCurrentMouseBtn = mouseEvent->mButton;
      }
    }
  }

  ~AutoEventHandler() {
    if (mEvent->mMessage == eMouseDown) {
      PresShell::AllowMouseCapture(false);
    }
    if (NeedsToUpdateCurrentMouseBtnState()) {
      EventStateManager::sCurrentMouseBtn = MouseButton::eNotPressed;
    }
  }

 protected:
  bool NeedsToUpdateCurrentMouseBtnState() const {
    return mEvent->mMessage == eMouseDown || mEvent->mMessage == eMouseUp ||
           mEvent->mMessage == ePointerDown || mEvent->mMessage == ePointerUp;
  }

  WidgetEvent* mEvent;
};

}  // anonymous namespace

nsresult PresShell::EventHandler::HandleEventWithCurrentEventInfo(
    WidgetEvent* aEvent, nsEventStatus* aEventStatus,
    bool aIsHandlingNativeEvent, nsIContent* aOverrideClickTarget) {
  MOZ_ASSERT(aEvent);
  MOZ_ASSERT(aEventStatus);

  RefPtr<EventStateManager> manager = GetPresContext()->EventStateManager();

  // If we cannot handle the event with mPresShell because of no target,
  // just record the response time.
  // XXX Is this intentional?  In such case, the score is really good because
  //     of nothing to do.  So, it may make average and median better.
  if (NS_EVENT_NEEDS_FRAME(aEvent) && !mPresShell->GetCurrentEventFrame() &&
      !mPresShell->GetCurrentEventContent()) {
    RecordEventHandlingResponsePerformance(aEvent);
    return NS_OK;
  }

  if (mPresShell->mCurrentEventTarget.mContent &&
      aEvent->IsTargetedAtFocusedWindow() &&
      aEvent->AllowFlushingPendingNotifications()) {
    if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
      // This may run script now.  So, mPresShell might be destroyed after here.
      nsCOMPtr<nsIContent> currentEventContent =
          mPresShell->mCurrentEventTarget.mContent;
      fm->FlushBeforeEventHandlingIfNeeded(currentEventContent);
    }
  }

  bool touchIsNew = false;
  if (!PrepareToDispatchEvent(aEvent, aEventStatus, &touchIsNew)) {
    return NS_OK;
  }

  // We finished preparing to dispatch the event.  So, let's record the
  // performance.
  RecordEventPreparationPerformance(aEvent);

  AutoHandlingUserInputStatePusher userInpStatePusher(
      UserActivation::IsUserInteractionEvent(aEvent), aEvent);
  AutoEventHandler eventHandler(aEvent, GetDocument());
  AutoPopupStatePusher popupStatePusher(
      PopupBlocker::GetEventPopupControlState(aEvent));

  // FIXME. If the event was reused, we need to clear the old target,
  // bug 329430
  aEvent->mTarget = nullptr;

  nsresult rv = DispatchEvent(manager, aEvent, touchIsNew, aEventStatus,
                              aOverrideClickTarget);

  if (!mPresShell->IsDestroying() && aIsHandlingNativeEvent &&
      aEvent->mClass != eQueryContentEventClass) {
    // Ensure that notifications to IME should be sent before getting next
    // native event from the event queue.
    // XXX Should we check the event message or event class instead of
    //     using aIsHandlingNativeEvent?
    manager->TryToFlushPendingNotificationsToIME();
  }

  FinalizeHandlingEvent(aEvent, aEventStatus);

  RecordEventHandlingResponsePerformance(aEvent);

  return rv;  // Result of DispatchEvent()
}

nsresult PresShell::EventHandler::DispatchEvent(
    EventStateManager* aEventStateManager, WidgetEvent* aEvent,
    bool aTouchIsNew, nsEventStatus* aEventStatus,
    nsIContent* aOverrideClickTarget) {
  MOZ_ASSERT(aEventStateManager);
  MOZ_ASSERT(aEvent);
  MOZ_ASSERT(aEventStatus);

  // 1. Give event to event manager for pre event state changes and
  //    generation of synthetic events.
  {  // Scope for presContext
    RefPtr<nsPresContext> presContext = GetPresContext();
    nsCOMPtr<nsIContent> eventContent =
        mPresShell->mCurrentEventTarget.mContent;
    nsresult rv = aEventStateManager->PreHandleEvent(
        presContext, aEvent, mPresShell->mCurrentEventTarget.mFrame,
        eventContent, aEventStatus, aOverrideClickTarget);
    if (NS_FAILED(rv)) {
      return rv;
    }
    // Let's retarget eMouseMove target if the preceding mouse boundary events
    // caused removing the target from the tree and EventStateManager knows that
    // the deepest connected mouseenter target which was an ancestor of the
    // removed target.  This matches with Chrome Canary with enabling the
    // new mouse/pointer boundary event feature.  However, they stop dispatching
    // "pointermove" in the same case.  Therefore, for now, we should do this
    // only for eMouseMove.
    if (eventContent && aEvent->mMessage == eMouseMove &&
        (!eventContent->IsInComposedDoc() ||
         eventContent->OwnerDoc() != mPresShell->GetDocument())) {
      const OverOutElementsWrapper* const boundaryEventTargets =
          aEventStateManager->GetExtantMouseBoundaryEventTarget();
      const nsIContent* outEventTarget =
          boundaryEventTargets ? boundaryEventTargets->GetOutEventTarget()
                               : nullptr;
      nsIContent* const deepestLeaveEventTarget =
          boundaryEventTargets
              ? boundaryEventTargets->GetDeepestLeaveEventTarget()
              : nullptr;
      // If the last "over" target (next "out" target) is there, it means that
      // it was temporarily removed.  In such case, EventStateManager treats
      // it as never disconnected.  Therefore, we need to do nothing here.
      // Additionally, if there is no last deepest "enter" event target, we
      // lost the target.  Therefore, we should keep the traditional behavior,
      // to dispatch it on the Document node.
      if (!outEventTarget && deepestLeaveEventTarget) {
        nsIFrame* const frame =
            deepestLeaveEventTarget->GetPrimaryFrame(FlushType::Layout);
        if (MOZ_UNLIKELY(mPresShell->IsDestroying())) {
          return NS_OK;
        }
        if (frame) {
          mPresShell->mCurrentEventTarget.mFrame = frame;
          mPresShell->mCurrentEventTarget.mContent = deepestLeaveEventTarget;
        }
      }
    }
  }

  // 2. Give event to the DOM for third party and JS use.
  bool wasHandlingKeyBoardEvent = nsContentUtils::IsHandlingKeyBoardEvent();
  if (aEvent->mClass == eKeyboardEventClass) {
    nsContentUtils::SetIsHandlingKeyBoardEvent(true);
  }
  // If EventStateManager or something wants reply from remote process and
  // needs to win any other event listeners in chrome, the event is both
  // stopped its propagation and marked as "waiting reply from remote
  // process".  In this case, PresShell shouldn't dispatch the event into
  // the DOM tree because they don't have a chance to stop propagation in
  // the system event group.  On the other hand, if its propagation is not
  // stopped, that means that the event may be reserved by chrome.  If it's
  // reserved by chrome, the event shouldn't be sent to any remote
  // processes.  In this case, PresShell needs to dispatch the event to
  // the DOM tree for checking if it's reserved.
  if (aEvent->IsAllowedToDispatchDOMEvent() &&
      !(aEvent->PropagationStopped() &&
        aEvent->IsWaitingReplyFromRemoteProcess())) {
    MOZ_ASSERT(nsContentUtils::IsSafeToRunScript(),
               "Somebody changed aEvent to cause a DOM event!");
    nsPresShellEventCB eventCB(mPresShell);
    if (nsIFrame* target = mPresShell->GetCurrentEventFrame()) {
      if (target->OnlySystemGroupDispatch(aEvent->mMessage)) {
        aEvent->StopPropagation();
      }
    }
    if (aEvent->mClass == eTouchEventClass) {
      DispatchTouchEventToDOM(aEvent, aEventStatus, &eventCB, aTouchIsNew);
    } else {
      DispatchEventToDOM(aEvent, aEventStatus, &eventCB);
    }
  }

  nsContentUtils::SetIsHandlingKeyBoardEvent(wasHandlingKeyBoardEvent);

  if (mPresShell->IsDestroying()) {
    return NS_OK;
  }

  // 3. Give event to event manager for post event state changes and
  //    generation of synthetic events.
  // Refetch the prescontext, in case it changed.
  RefPtr<nsPresContext> presContext = GetPresContext();
  return aEventStateManager->PostHandleEvent(
      presContext, aEvent, mPresShell->GetCurrentEventFrame(), aEventStatus,
      aOverrideClickTarget);
}

bool PresShell::EventHandler::PrepareToDispatchEvent(
    WidgetEvent* aEvent, nsEventStatus* aEventStatus, bool* aTouchIsNew) {
  MOZ_ASSERT(aEvent->IsTrusted());
  MOZ_ASSERT(aEventStatus);
  MOZ_ASSERT(aTouchIsNew);

  *aTouchIsNew = false;
  if (aEvent->IsUserAction()) {
    mPresShell->mHasHandledUserInput = true;
  }

  switch (aEvent->mMessage) {
    case eKeyPress:
    case eKeyDown:
    case eKeyUp: {
      WidgetKeyboardEvent* keyboardEvent = aEvent->AsKeyboardEvent();
      MaybeHandleKeyboardEventBeforeDispatch(keyboardEvent);
      return true;
    }
    case eMouseMove: {
      bool allowCapture = EventStateManager::GetActiveEventStateManager() &&
                          GetPresContext() &&
                          GetPresContext()->EventStateManager() ==
                              EventStateManager::GetActiveEventStateManager();
      PresShell::AllowMouseCapture(allowCapture);
      return true;
    }
    case eDrop: {
      nsCOMPtr<nsIDragSession> session =
          nsContentUtils::GetDragSession(GetPresContext());
      if (session) {
        bool onlyChromeDrop = false;
        session->GetOnlyChromeDrop(&onlyChromeDrop);
        if (onlyChromeDrop) {
          aEvent->mFlags.mOnlyChromeDispatch = true;
        }
      }
      return true;
    }
    case eDragExit: {
      if (!StaticPrefs::dom_event_dragexit_enabled()) {
        aEvent->mFlags.mOnlyChromeDispatch = true;
      }
      return true;
    }
    case eContextMenu: {
      // If we cannot open context menu even though eContextMenu is fired, we
      // should stop dispatching it into the DOM.
      WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
      if (mouseEvent->IsContextMenuKeyEvent() &&
          !AdjustContextMenuKeyEvent(mouseEvent)) {
        return false;
      }

      // If "Shift" state is active, context menu should be forcibly opened even
      // if web apps want to prevent it since we respect our users' intention.
      // In this case, we don't fire "contextmenu" event on web content because
      // of not cancelable.
      if (mouseEvent->IsShift() &&
          StaticPrefs::dom_event_contextmenu_shift_suppresses_event()) {
        aEvent->mFlags.mOnlyChromeDispatch = true;
        aEvent->mFlags.mRetargetToNonNativeAnonymous = true;
      }
      return true;
    }
    case eTouchStart:
    case eTouchMove:
    case eTouchEnd:
    case eTouchCancel:
    case eTouchPointerCancel:
      return mPresShell->mTouchManager.PreHandleEvent(
          aEvent, aEventStatus, *aTouchIsNew,
          mPresShell->mCurrentEventTarget.mContent);
    default:
      return true;
  }
}

void PresShell::EventHandler::FinalizeHandlingEvent(
    WidgetEvent* aEvent, const nsEventStatus* aStatus) {
  switch (aEvent->mMessage) {
    case eKeyPress:
    case eKeyDown:
    case eKeyUp: {
      if (aEvent->AsKeyboardEvent()->mKeyCode == NS_VK_ESCAPE) {
        if (aEvent->mMessage == eKeyUp) {
          // Reset this flag after key up is handled.
          mPresShell->mIsLastChromeOnlyEscapeKeyConsumed = false;
        } else {
          if (aEvent->mFlags.mOnlyChromeDispatch &&
              aEvent->mFlags.mDefaultPreventedByChrome) {
            mPresShell->mIsLastChromeOnlyEscapeKeyConsumed = true;
          }
          if (aEvent->mMessage == eKeyDown &&
              !aEvent->mFlags.mDefaultPrevented) {
            if (RefPtr<Document> doc = GetDocument()) {
              if (StaticPrefs::dom_closewatcher_enabled()) {
                doc->ProcessCloseRequest();
              } else {
                doc->HandleEscKey();
              }
            }
          }
        }
      }
      if (aEvent->mMessage == eKeyDown) {
        mPresShell->mIsLastKeyDownCanceled = aEvent->mFlags.mDefaultPrevented;
      }
      break;
    }
    case eMouseUp:
      // reset the capturing content now that the mouse button is up
      PresShell::ReleaseCapturingContent();
      break;
    case eMouseMove:
      PresShell::AllowMouseCapture(false);
      break;
    case eDrag:
    case eDragEnd:
    case eDragEnter:
    case eDragExit:
    case eDragLeave:
    case eDragOver:
    case eDrop: {
      // After any drag event other than dragstart (which is handled
      // separately, as we need to collect the data first), the DataTransfer
      // needs to be made protected, and then disconnected.
      DataTransfer* dataTransfer = aEvent->AsDragEvent()->mDataTransfer;
      if (dataTransfer) {
        dataTransfer->Disconnect();
      }
      break;
    }
    case eTouchStart:
    case eTouchMove:
    case eTouchEnd:
    case eTouchCancel:
    case eTouchPointerCancel:
    case eMouseLongTap:
    case eContextMenu: {
      mPresShell->mTouchManager.PostHandleEvent(aEvent, aStatus);
      break;
    }
    default:
      break;
  }

  if (WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent()) {
    if (mouseEvent->mSynthesizeMoveAfterDispatch) {
      PointerEventHandler::SynthesizeMoveToDispatchBoundaryEvents(mouseEvent);
    }
  }
}

void PresShell::EventHandler::MaybeHandleKeyboardEventBeforeDispatch(
    WidgetKeyboardEvent* aKeyboardEvent) {
  MOZ_ASSERT(aKeyboardEvent);

  if (aKeyboardEvent->mKeyCode != NS_VK_ESCAPE) {
    return;
  }

  // If we're in fullscreen mode, exit from it forcibly when Escape key is
  // pressed.
  Document* doc = mPresShell->GetCurrentEventContent()
                      ? mPresShell->mCurrentEventTarget.mContent->OwnerDoc()
                      : nullptr;
  Document* root = nsContentUtils::GetInProcessSubtreeRootDocument(doc);
  if (root && root->GetFullscreenElement()) {
    // Prevent default action on ESC key press when exiting
    // DOM fullscreen mode. This prevents the browser ESC key
    // handler from stopping all loads in the document, which
    // would cause <video> loads to stop.
    // XXX We need to claim the Escape key event which will be
    //     dispatched only into chrome is already consumed by
    //     content because we need to prevent its default here
    //     for some reasons (not sure) but we need to detect
    //     if a chrome event handler will call PreventDefault()
    //     again and check it later.
    aKeyboardEvent->PreventDefaultBeforeDispatch(CrossProcessForwarding::eStop);
    aKeyboardEvent->mFlags.mOnlyChromeDispatch = true;

    // The event listeners in chrome can prevent this ESC behavior by
    // calling prevent default on the preceding keydown/press events.
    if (aKeyboardEvent->mMessage == eKeyUp) {
      bool shouldExitFullscreen =
          !mPresShell->mIsLastChromeOnlyEscapeKeyConsumed;
      if (!shouldExitFullscreen) {
        if (mPresShell->mLastConsumedEscapeKeyUpForFullscreen &&
            (aKeyboardEvent->mTimeStamp -
             mPresShell->mLastConsumedEscapeKeyUpForFullscreen) <=
                TimeDuration::FromMilliseconds(
                    StaticPrefs::
                        dom_fullscreen_force_exit_on_multiple_escape_interval())) {
          shouldExitFullscreen = true;
          mPresShell->mLastConsumedEscapeKeyUpForFullscreen = TimeStamp();
        } else {
          mPresShell->mLastConsumedEscapeKeyUpForFullscreen =
              aKeyboardEvent->mTimeStamp;
        }
      }

      if (shouldExitFullscreen) {
        // ESC key released while in DOM fullscreen mode.
        // Fully exit all browser windows and documents from
        // fullscreen mode.
        Document::AsyncExitFullscreen(nullptr);
      }
    }
  }

  if (XRE_IsParentProcess() &&
      !mPresShell->mIsLastChromeOnlyEscapeKeyConsumed) {
    if (PointerLockManager::GetLockedRemoteTarget() ||
        PointerLockManager::IsLocked()) {
      // XXX See above comment to understand the reason why this needs
      //     to claim that the Escape key event is consumed by content
      //     even though it will be dispatched only into chrome.
      aKeyboardEvent->PreventDefaultBeforeDispatch(
          CrossProcessForwarding::eStop);
      aKeyboardEvent->mFlags.mOnlyChromeDispatch = true;
      if (aKeyboardEvent->mMessage == eKeyUp) {
        PointerLockManager::Unlock("EscapeKey");
      }
    }
  }
}

void PresShell::EventHandler::RecordEventPreparationPerformance(
    const WidgetEvent* aEvent) {
  MOZ_ASSERT(aEvent);

  switch (aEvent->mMessage) {
    case eKeyPress:
    case eKeyDown:
    case eKeyUp:
      if (aEvent->AsKeyboardEvent()->ShouldInteractionTimeRecorded()) {
        GetPresContext()->RecordInteractionTime(
            nsPresContext::InteractionType::KeyInteraction, aEvent->mTimeStamp);
      }
      glean::layout::input_event_queued_keyboard.AccumulateRawDuration(
          TimeStamp::Now() - aEvent->mTimeStamp);
      return;

    case eMouseDown:
    case eMouseUp:
      glean::layout::input_event_queued_click.AccumulateRawDuration(
          TimeStamp::Now() - aEvent->mTimeStamp);
      [[fallthrough]];
    case ePointerDown:
    case ePointerUp:
      GetPresContext()->RecordInteractionTime(
          nsPresContext::InteractionType::ClickInteraction, aEvent->mTimeStamp);
      return;

    case eMouseMove:
      GetPresContext()->RecordInteractionTime(
          nsPresContext::InteractionType::MouseMoveInteraction,
          aEvent->mTimeStamp);
      return;

    default:
      return;
  }
}

void PresShell::EventHandler::RecordEventHandlingResponsePerformance(
    const WidgetEvent* aEvent) {
  if (!Telemetry::CanRecordBase() || aEvent->mTimeStamp.IsNull() ||
      aEvent->mTimeStamp <= mPresShell->mLastOSWake ||
      !aEvent->AsInputEvent()) {
    return;
  }

  TimeStamp now = TimeStamp::Now();
  TimeDuration duration = now - aEvent->mTimeStamp;
  glean::layout::input_event_response.AccumulateRawDuration(duration);
  if (GetDocument() &&
      GetDocument()->GetReadyStateEnum() != Document::READYSTATE_COMPLETE) {
    glean::layout::load_input_event_response.AccumulateRawDuration(duration);
  }

  if (!sLastInputProcessed || sLastInputProcessed < aEvent->mTimeStamp) {
    if (sLastInputProcessed) {
      // This input event was created after we handled the last one.
      // Accumulate the previous events' coalesced duration.
      glean::layout::input_event_response_coalesced.AccumulateRawDuration(
          sLastInputProcessed - sLastInputCreated);

      if (MOZ_UNLIKELY(!PresShell::sProcessInteractable)) {
        // For content process, we use the ready state of
        // top-level-content-document to know if the process has finished the
        // start-up.
        // For parent process, see the topic
        // 'sessionstore-one-or-no-tab-restored' in PresShell::Observe.
        if (XRE_IsContentProcess() && GetDocument() &&
            GetDocument()->IsTopLevelContentDocument()) {
          switch (GetDocument()->GetReadyStateEnum()) {
            case Document::READYSTATE_INTERACTIVE:
            case Document::READYSTATE_COMPLETE:
              PresShell::sProcessInteractable = true;
              break;
            default:
              break;
          }
        }
      }
    }
    sLastInputCreated = aEvent->mTimeStamp;
  } else if (aEvent->mTimeStamp < sLastInputCreated) {
    // This event was created before the last input. May be processing out
    // of order, so coalesce backwards, too.
    sLastInputCreated = aEvent->mTimeStamp;
  }
  sLastInputProcessed = now;
}

// static
nsIPrincipal*
PresShell::EventHandler::GetDocumentPrincipalToCompareWithBlacklist(
    PresShell& aPresShell) {
  nsPresContext* presContext = aPresShell.GetPresContext();
  if (NS_WARN_IF(!presContext)) {
    return nullptr;
  }
  return presContext->Document()->GetPrincipalForPrefBasedHacks();
}

nsresult PresShell::EventHandler::DispatchEventToDOM(
    WidgetEvent* aEvent, nsEventStatus* aEventStatus,
    nsPresShellEventCB* aEventCB) {
  nsresult rv = NS_OK;
  nsCOMPtr<nsINode> eventTarget = mPresShell->mCurrentEventTarget.mContent;
  nsPresShellEventCB* eventCBPtr = aEventCB;
  if (!eventTarget) {
    nsCOMPtr<nsIContent> targetContent;
    if (mPresShell->mCurrentEventTarget.mFrame) {
      targetContent =
          mPresShell->mCurrentEventTarget.mFrame->GetContentForEvent(aEvent);
      if (targetContent && !targetContent->IsElement() &&
          IsForbiddenDispatchingToNonElementContent(aEvent->mMessage)) {
        targetContent =
            targetContent->GetInclusiveFlattenedTreeAncestorElement();
      }
    }
    if (targetContent) {
      eventTarget = targetContent;
    } else if (GetDocument()) {
      eventTarget = GetDocument();
      // If we don't have any content, the callback wouldn't probably
      // do nothing.
      eventCBPtr = nullptr;
    }
  }
  if (eventTarget) {
    if (eventTarget->OwnerDoc()->ShouldResistFingerprinting(
            RFPTarget::WidgetEvents) &&
        aEvent->IsBlockedForFingerprintingResistance()) {
      aEvent->mFlags.mOnlySystemGroupDispatchInContent = true;
    } else if (aEvent->mMessage == eKeyPress) {
      // If eKeyPress event is marked as not dispatched in the default event
      // group in web content, it's caused by non-printable key or key
      // combination.  In this case, UI Events declares that browsers
      // shouldn't dispatch keypress event.  However, some web apps may be
      // broken with this strict behavior due to historical issue.
      // Therefore, we need to keep dispatching keypress event for such keys
      // even with breaking the standard.
      // Similarly, the other browsers sets non-zero value of keyCode or
      // charCode of keypress event to the other.  Therefore, we should
      // behave so, however, some web apps may be broken.  On such web apps,
      // we should keep using legacy our behavior.
      if (!mPresShell->mInitializedWithKeyPressEventDispatchingBlacklist) {
        mPresShell->mInitializedWithKeyPressEventDispatchingBlacklist = true;
        nsCOMPtr<nsIPrincipal> principal =
            GetDocumentPrincipalToCompareWithBlacklist(*mPresShell);
        if (principal) {
          mPresShell->mForceDispatchKeyPressEventsForNonPrintableKeys =
              principal->IsURIInPrefList(
                  "dom.keyboardevent.keypress.hack.dispatch_non_printable_"
                  "keys") ||
              principal->IsURIInPrefList(
                  "dom.keyboardevent.keypress.hack."
                  "dispatch_non_printable_keys.addl");

          mPresShell->mForceUseLegacyKeyCodeAndCharCodeValues |=
              principal->IsURIInPrefList(
                  "dom.keyboardevent.keypress.hack."
                  "use_legacy_keycode_and_charcode") ||
              principal->IsURIInPrefList(
                  "dom.keyboardevent.keypress.hack."
                  "use_legacy_keycode_and_charcode.addl");
        }
      }
      if (mPresShell->mForceDispatchKeyPressEventsForNonPrintableKeys) {
        aEvent->mFlags.mOnlySystemGroupDispatchInContent = false;
      }
      if (mPresShell->mForceUseLegacyKeyCodeAndCharCodeValues) {
        aEvent->AsKeyboardEvent()->mUseLegacyKeyCodeAndCharCodeValues = true;
      }
    }

    if (aEvent->mClass == eCompositionEventClass) {
      RefPtr<nsPresContext> presContext = GetPresContext();
      RefPtr<BrowserParent> browserParent =
          IMEStateManager::GetActiveBrowserParent();
      IMEStateManager::DispatchCompositionEvent(
          eventTarget, presContext, browserParent, aEvent->AsCompositionEvent(),
          aEventStatus, eventCBPtr);
    } else {
      if (aEvent->IsMouseEventClassOrHasClickRelatedPointerEvent()) {
        PresShell::sMouseButtons = aEvent->AsMouseEvent()->mButtons;
#ifdef DEBUG
        if (eventTarget->IsContent() && !eventTarget->IsElement()) {
          NS_WARNING(nsPrintfCString(
                         "%s (IsReal()=%s) target is not an elemnet content "
                         "node, %s\n",
                         ToChar(aEvent->mMessage),
                         aEvent->AsMouseEvent()->IsReal() ? "true" : "false",
                         ToString(*eventTarget).c_str())
                         .get());
          MOZ_CRASH("MouseEvent target must be an element");
        }
#endif  // #ifdef DEBUG
      }
      RefPtr<nsPresContext> presContext = GetPresContext();
      EventDispatcher::Dispatch(eventTarget, presContext, aEvent, nullptr,
                                aEventStatus, eventCBPtr);
    }
  }
  return rv;
}

void PresShell::EventHandler::DispatchTouchEventToDOM(
    WidgetEvent* aEvent, nsEventStatus* aEventStatus,
    nsPresShellEventCB* aEventCB, bool aTouchIsNew) {
  // calling preventDefault on touchstart or the first touchmove for a
  // point prevents mouse events. calling it on the touchend should
  // prevent click dispatching.
  bool canPrevent = (aEvent->mMessage == eTouchStart) ||
                    (aEvent->mMessage == eTouchMove && aTouchIsNew) ||
                    (aEvent->mMessage == eTouchEnd);
  bool preventDefault = false;
  nsEventStatus tmpStatus = nsEventStatus_eIgnore;
  WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();

  // loop over all touches and dispatch events on any that have changed
  for (dom::Touch* touch : touchEvent->mTouches) {
    // We should remove all suppressed touch instances in
    // TouchManager::PreHandleEvent.
    MOZ_ASSERT(!touch->mIsTouchEventSuppressed);

    if (!touch || !touch->mChanged) {
      continue;
    }

    nsCOMPtr<EventTarget> targetPtr = touch->mTarget;
    nsCOMPtr<nsIContent> content = do_QueryInterface(targetPtr);
    if (!content) {
      continue;
    }

    Document* doc = content->OwnerDoc();
    nsIContent* capturingContent = PresShell::GetCapturingContent();
    if (capturingContent) {
      if (capturingContent->OwnerDoc() != doc) {
        // Wrong document, don't dispatch anything.
        continue;
      }
      content = capturingContent;
    }
    // copy the event
    MOZ_ASSERT(touchEvent->IsTrusted());
    WidgetTouchEvent newEvent(true, touchEvent->mMessage, touchEvent->mWidget);
    newEvent.AssignTouchEventData(*touchEvent, false);
    newEvent.mTarget = targetPtr;
    newEvent.mFlags.mHandledByAPZ = touchEvent->mFlags.mHandledByAPZ;

    RefPtr<PresShell> contentPresShell;
    if (doc == GetDocument()) {
      contentPresShell = doc->GetPresShell();
      if (contentPresShell) {
        // XXXsmaug huge hack. Pushing possibly capturing content,
        //         even though event target is something else.
        contentPresShell->PushCurrentEventInfo(EventTargetInfo(
            newEvent.mMessage, content->GetPrimaryFrame(), content));
      }
    }

    RefPtr<nsPresContext> presContext = doc->GetPresContext();
    if (!presContext) {
      if (contentPresShell) {
        contentPresShell->PopCurrentEventInfo();
      }
      continue;
    }

    tmpStatus = nsEventStatus_eIgnore;
    EventDispatcher::Dispatch(targetPtr, presContext, &newEvent, nullptr,
                              &tmpStatus, aEventCB);
    if (nsEventStatus_eConsumeNoDefault == tmpStatus ||
        newEvent.mFlags.mMultipleActionsPrevented) {
      preventDefault = true;
    }

    if (newEvent.mFlags.mMultipleActionsPrevented) {
      touchEvent->mFlags.mMultipleActionsPrevented = true;
    }

    if (contentPresShell) {
      contentPresShell->PopCurrentEventInfo();
    }
  }

  if (preventDefault && canPrevent) {
    *aEventStatus = nsEventStatus_eConsumeNoDefault;
  } else {
    *aEventStatus = nsEventStatus_eIgnore;
  }
}

// Dispatch event to content only (NOT full processing)
// See also HandleEventWithTarget which does full event processing.
nsresult PresShell::HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                             WidgetEvent* aEvent,
                                             nsEventStatus* aStatus) {
  nsresult rv = NS_OK;

  PushCurrentEventInfo(
      EventTargetInfo(aEvent->mMessage, nullptr, aTargetContent));

  // Bug 41013: Check if the event should be dispatched to content.
  // It's possible that we are in the middle of destroying the window
  // and the js context is out of date. This check detects the case
  // that caused a crash in bug 41013, but there may be a better way
  // to handle this situation!
  nsCOMPtr<nsISupports> container = mPresContext->GetContainerWeak();
  if (container) {
    // Dispatch event to content
    rv = EventDispatcher::Dispatch(aTargetContent, mPresContext, aEvent,
                                   nullptr, aStatus);
  }

  PopCurrentEventInfo();
  return rv;
}

// See the method above.
nsresult PresShell::HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                             Event* aEvent,
                                             nsEventStatus* aStatus) {
  nsresult rv = NS_OK;

  PushCurrentEventInfo(EventTargetInfo(aEvent->WidgetEventPtr()->mMessage,
                                       nullptr, aTargetContent));
  nsCOMPtr<nsISupports> container = mPresContext->GetContainerWeak();
  if (container) {
    rv = EventDispatcher::DispatchDOMEvent(aTargetContent, nullptr, aEvent,
                                           mPresContext, aStatus);
  }

  PopCurrentEventInfo();
  return rv;
}

bool PresShell::EventHandler::AdjustContextMenuKeyEvent(
    WidgetMouseEvent* aMouseEvent) {
  // if a menu is open, open the context menu relative to the active item on the
  // menu.
  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    nsIFrame* popupFrame = pm->GetTopPopup(widget::PopupType::Menu);
    if (popupFrame) {
      nsIFrame* itemFrame = (static_cast<nsMenuPopupFrame*>(popupFrame))
                                ->GetCurrentMenuItemFrame();
      if (!itemFrame) {
        itemFrame = popupFrame;
      }

      nsCOMPtr<nsIWidget> widget = popupFrame->GetNearestWidget();
      aMouseEvent->mWidget = widget;
      LayoutDeviceIntPoint widgetPoint = widget->WidgetToScreenOffset();
      aMouseEvent->mRefPoint =
          LayoutDeviceIntPoint::FromAppUnitsToNearest(
              itemFrame->GetScreenRectInAppUnits().BottomLeft(),
              itemFrame->PresContext()->AppUnitsPerDevPixel()) -
          widgetPoint;

      mPresShell->mCurrentEventTarget.SetFrameAndContent(
          aMouseEvent->mMessage, itemFrame,
          itemFrame->GetContent()
              ? itemFrame->GetContent()
                    ->GetInclusiveFlattenedTreeAncestorElement()
              : nullptr);

      return true;
    }
  }

  // If we're here because of the key-equiv for showing context menus, we
  // have to twiddle with the NS event to make sure the context menu comes
  // up in the upper left of the relevant content area before we create
  // the DOM event. Since we never call InitMouseEvent() on the event,
  // the client X/Y will be 0,0. We can make use of that if the widget is null.
  // Use the root view manager's widget since it's most likely to have one,
  // and the coordinates returned by GetCurrentItemAndPositionForElement
  // are relative to the widget of the root of the root view manager.
  nsRootPresContext* rootPC = GetPresContext()->GetRootPresContext();
  aMouseEvent->mRefPoint = LayoutDeviceIntPoint(0, 0);
  if (rootPC) {
    aMouseEvent->mWidget =
        rootPC->PresShell()->GetViewManager()->GetRootWidget();
    if (aMouseEvent->mWidget) {
      // default the refpoint to the topleft of our document
      nsPoint offset(0, 0);
      nsIFrame* rootFrame = FrameConstructor()->GetRootFrame();
      if (rootFrame) {
        nsView* view = rootFrame->GetClosestView(&offset);
        offset += view->GetOffsetToWidget(aMouseEvent->mWidget);
        aMouseEvent->mRefPoint = LayoutDeviceIntPoint::FromAppUnitsToNearest(
            offset, GetPresContext()->AppUnitsPerDevPixel());
      }
    }
  } else {
    aMouseEvent->mWidget = nullptr;
  }

  // see if we should use the caret position for the popup
  LayoutDeviceIntPoint caretPoint;
  // Beware! This may flush notifications via synchronous
  // ScrollSelectionIntoView.
  if (PrepareToUseCaretPosition(MOZ_KnownLive(aMouseEvent->mWidget),
                                caretPoint)) {
    // caret position is good
    int32_t devPixelRatio = GetPresContext()->AppUnitsPerDevPixel();
    caretPoint = LayoutDeviceIntPoint::FromAppUnitsToNearest(
        ViewportUtils::LayoutToVisual(
            LayoutDeviceIntPoint::ToAppUnits(caretPoint, devPixelRatio),
            GetPresContext()->PresShell()),
        devPixelRatio);
    aMouseEvent->mRefPoint = caretPoint;
    return true;
  }

  // If we're here because of the key-equiv for showing context menus, we
  // have to reset the event target to the currently focused element. Get it
  // from the focus controller.
  RefPtr<Element> currentFocus = nsFocusManager::GetFocusedElementStatic();

  // Reset event coordinates relative to focused frame in view
  if (currentFocus) {
    nsCOMPtr<nsIContent> currentPointElement;
    GetCurrentItemAndPositionForElement(
        currentFocus, getter_AddRefs(currentPointElement),
        aMouseEvent->mRefPoint, MOZ_KnownLive(aMouseEvent->mWidget));
    if (currentPointElement) {
      mPresShell->mCurrentEventTarget.SetFrameAndContent(
          aMouseEvent->mMessage, nullptr, currentPointElement);
      mPresShell->GetCurrentEventFrame();
    }
  }

  return true;
}

// PresShell::EventHandler::PrepareToUseCaretPosition
//
//    This checks to see if we should use the caret position for popup context
//    menus. Returns true if the caret position should be used, and the
//    coordinates of that position is returned in aTargetPt. This function
//    will also scroll the window as needed to make the caret visible.
//
//    The event widget should be the widget that generated the event, and
//    whose coordinate system the resulting event's mRefPoint should be
//    relative to.  The returned point is in device pixels realtive to the
//    widget passed in.
bool PresShell::EventHandler::PrepareToUseCaretPosition(
    nsIWidget* aEventWidget, LayoutDeviceIntPoint& aTargetPt) {
  nsresult rv;

  // check caret visibility
  RefPtr<nsCaret> caret = mPresShell->GetCaret();
  NS_ENSURE_TRUE(caret, false);

  bool caretVisible = caret->IsVisible();
  if (!caretVisible) {
    return false;
  }

  // caret selection, this is a temporary weak reference, so no refcounting is
  // needed
  Selection* domSelection = caret->GetSelection();
  NS_ENSURE_TRUE(domSelection, false);

  // since the match could be an anonymous textnode inside a
  // <textarea> or text <input>, we need to get the outer frame
  // note: frames are not refcounted
  nsIFrame* frame = nullptr;  // may be nullptr
  nsINode* node = domSelection->GetFocusNode();
  NS_ENSURE_TRUE(node, false);
  nsCOMPtr<nsIContent> content = nsIContent::FromNode(node);
  if (content) {
    nsIContent* nonNative = content->FindFirstNonChromeOnlyAccessContent();
    content = nonNative;
  }

  if (content) {
    // It seems like ScrollSelectionIntoView should be enough, but it's
    // not. The problem is that scrolling the selection into view when it is
    // below the current viewport will align the top line of the frame exactly
    // with the bottom of the window. This is fine, BUT, the popup event causes
    // the control to be re-focused which does this exact call to
    // ScrollContentIntoView, which has a one-pixel disagreement of whether the
    // frame is actually in view. The result is that the frame is aligned with
    // the top of the window, but the menu is still at the bottom.
    //
    // Doing this call first forces the frame to be in view, eliminating the
    // problem. The only difference in the result is that if your cursor is in
    // an edit box below the current view, you'll get the edit box aligned with
    // the top of the window. This is arguably better behavior anyway.
    rv = MOZ_KnownLive(mPresShell)
             ->ScrollContentIntoView(
                 content,
                 ScrollAxis(WhereToScroll::Nearest, WhenToScroll::IfNotVisible),
                 ScrollAxis(WhereToScroll::Nearest, WhenToScroll::IfNotVisible),
                 ScrollFlags::ScrollOverflowHidden);
    NS_ENSURE_SUCCESS(rv, false);
    frame = content->GetPrimaryFrame();
    NS_WARNING_ASSERTION(frame, "No frame for focused content?");
  }

  // Actually scroll the selection (ie caret) into view. Note that this must
  // be synchronous since we will be checking the caret position on the screen.
  //
  // Be easy about errors, and just don't scroll in those cases. Better to have
  // the correct menu at a weird place than the wrong menu.
  // After ScrollSelectionIntoView(), the pending notifications might be
  // flushed and PresShell/PresContext/Frames may be dead. See bug 418470.
  nsCOMPtr<nsISelectionController> selCon;
  if (frame) {
    frame->GetSelectionController(GetPresContext(), getter_AddRefs(selCon));
  } else {
    selCon = static_cast<nsISelectionController*>(mPresShell);
  }
  if (selCon) {
    rv = selCon->ScrollSelectionIntoView(
        SelectionType::eNormal, nsISelectionController::SELECTION_FOCUS_REGION,
        SelectionScrollMode::SyncFlush);
    NS_ENSURE_SUCCESS(rv, false);
  }

  nsPresContext* presContext = GetPresContext();

  // get caret position relative to the closest view
  nsRect caretCoords;
  nsIFrame* caretFrame = caret->GetGeometry(&caretCoords);
  if (!caretFrame) {
    return false;
  }
  nsPoint viewOffset;
  nsView* view = caretFrame->GetClosestView(&viewOffset);
  if (!view) {
    return false;
  }
  // and then get the caret coords relative to the event widget
  if (aEventWidget) {
    viewOffset += view->GetOffsetToWidget(aEventWidget);
  }
  caretCoords.MoveBy(viewOffset);

  // caret coordinates are in app units, convert to pixels
  aTargetPt.x =
      presContext->AppUnitsToDevPixels(caretCoords.x + caretCoords.width);
  aTargetPt.y =
      presContext->AppUnitsToDevPixels(caretCoords.y + caretCoords.height);

  // make sure rounding doesn't return a pixel which is outside the caret
  // (e.g. one line lower)
  aTargetPt.y -= 1;

  return true;
}

void PresShell::EventHandler::GetCurrentItemAndPositionForElement(
    Element* aFocusedElement, nsIContent** aTargetToUse,
    LayoutDeviceIntPoint& aTargetPt, nsIWidget* aRootWidget) {
  nsCOMPtr<nsIContent> focusedContent = aFocusedElement;
  MOZ_KnownLive(mPresShell)
      ->ScrollContentIntoView(focusedContent, ScrollAxis(), ScrollAxis(),
                              ScrollFlags::ScrollOverflowHidden);

  nsPresContext* presContext = GetPresContext();

  bool istree = false, checkLineHeight = true;
  nscoord extraTreeY = 0;

  // Set the position to just underneath the current item for multi-select
  // lists or just underneath the selected item for single-select lists. If
  // the element is not a list, or there is no selection, leave the position
  // as is.
  nsCOMPtr<Element> item;
  nsCOMPtr<nsIDOMXULMultiSelectControlElement> multiSelect =
      aFocusedElement->AsXULMultiSelectControl();
  if (multiSelect) {
    checkLineHeight = false;

    int32_t currentIndex;
    multiSelect->GetCurrentIndex(&currentIndex);
    if (currentIndex >= 0) {
      RefPtr<XULTreeElement> tree = XULTreeElement::FromNode(focusedContent);
      // Tree view special case (tree items have no frames)
      // Get the focused row and add its coordinates, which are already in
      // pixels
      // XXX Boris, should we create a new interface so that this doesn't
      // need to know about trees? Something like nsINodelessChildCreator
      // which could provide the current focus coordinates?
      if (tree) {
        tree->EnsureRowIsVisible(currentIndex);
        int32_t firstVisibleRow = tree->GetFirstVisibleRow();
        int32_t rowHeight = tree->RowHeight();

        extraTreeY += nsPresContext::CSSPixelsToAppUnits(
            (currentIndex - firstVisibleRow + 1) * rowHeight);
        istree = true;

        RefPtr<nsTreeColumns> cols = tree->GetColumns();
        if (cols) {
          nsTreeColumn* col = cols->GetFirstColumn();
          if (col) {
            RefPtr<Element> colElement = col->Element();
            nsIFrame* frame = colElement->GetPrimaryFrame();
            if (frame) {
              extraTreeY += frame->GetSize().height;
            }
          }
        }
      } else {
        multiSelect->GetCurrentItem(getter_AddRefs(item));
      }
    }
  } else {
    // don't check menulists as the selected item will be inside a popup.
    nsCOMPtr<nsIDOMXULMenuListElement> menulist =
        aFocusedElement->AsXULMenuList();
    if (!menulist) {
      nsCOMPtr<nsIDOMXULSelectControlElement> select =
          aFocusedElement->AsXULSelectControl();
      if (select) {
        checkLineHeight = false;
        select->GetSelectedItem(getter_AddRefs(item));
      }
    }
  }

  if (item) {
    focusedContent = item;
  }

  nsIFrame* frame = focusedContent->GetPrimaryFrame();
  if (frame) {
    NS_ASSERTION(
        frame->PresContext() == GetPresContext(),
        "handling event for focused content that is not in our document?");

    nsPoint frameOrigin(0, 0);

    // Get the frame's origin within its view
    nsView* view = frame->GetClosestView(&frameOrigin);
    NS_ASSERTION(view, "No view for frame");

    // View's origin relative the widget
    if (aRootWidget) {
      frameOrigin += view->GetOffsetToWidget(aRootWidget);
    }

    // Start context menu down and to the right from top left of frame
    // use the lineheight. This is a good distance to move the context
    // menu away from the top left corner of the frame. If we always
    // used the frame height, the context menu could end up far away,
    // for example when we're focused on linked images.
    // On the other hand, we want to use the frame height if it's less
    // than the current line height, so that the context menu appears
    // associated with the correct frame.
    nscoord extra = 0;
    if (!istree) {
      extra = frame->GetSize().height;
      if (checkLineHeight) {
        ScrollContainerFrame* scrollContainerFrame =
            nsLayoutUtils::GetNearestScrollContainerFrame(
                frame, nsLayoutUtils::SCROLLABLE_INCLUDE_HIDDEN |
                           nsLayoutUtils::SCROLLABLE_FIXEDPOS_FINDS_ROOT);
        if (scrollContainerFrame) {
          nsSize scrollAmount = scrollContainerFrame->GetLineScrollAmount();
          int32_t APD = presContext->AppUnitsPerDevPixel();
          int32_t scrollAPD =
              scrollContainerFrame->PresContext()->AppUnitsPerDevPixel();
          scrollAmount = scrollAmount.ScaleToOtherAppUnits(scrollAPD, APD);
          if (extra > scrollAmount.height) {
            extra = scrollAmount.height;
          }
        }
      }
    }

    aTargetPt.x = presContext->AppUnitsToDevPixels(frameOrigin.x);
    aTargetPt.y =
        presContext->AppUnitsToDevPixels(frameOrigin.y + extra + extraTreeY);
  }

  NS_IF_ADDREF(*aTargetToUse = focusedContent);
}

bool PresShell::ShouldIgnoreInvalidation() {
  return mPaintingSuppressed || !mIsActive || mIsNeverPainting;
}

void PresShell::WillPaint() {
  // Check the simplest things first.  In particular, it's important to
  // check mIsActive before making any of the more expensive calls such
  // as GetRootPresContext, for the case of a browser with a large
  // number of tabs.
  // Don't bother doing anything if some viewmanager in our tree is painting
  // while we still have painting suppressed or we are not active.
  if (!mIsActive || mPaintingSuppressed || !IsVisible()) {
    return;
  }

  nsRootPresContext* rootPresContext = mPresContext->GetRootPresContext();
  if (!rootPresContext) {
    // In some edge cases, such as when we don't have a root frame yet,
    // we can't find the root prescontext. There's nothing to do in that
    // case.
    return;
  }

  rootPresContext->FlushWillPaintObservers();
  if (mIsDestroying) {
    return;
  }

  // Process reflows, if we have them, to reduce flicker due to invalidates and
  // reflow being interspersed.  Note that we _do_ allow this to be
  // interruptible; if we can't do all the reflows it's better to flicker a bit
  // than to freeze up.
  FlushPendingNotifications(ChangesToFlush(FlushType::InterruptibleLayout,
                                           /* aFlushAnimations = */ false,
                                           /* aUpdateRelevancy = */ false));
}

void PresShell::DidPaintWindow() {
  nsRootPresContext* rootPresContext = mPresContext->GetRootPresContext();
  if (rootPresContext != mPresContext) {
    // This could be a popup's presshell. No point in notifying XPConnect
    // about compositing of popups.
    return;
  }

  if (!mHasReceivedPaintMessage) {
    mHasReceivedPaintMessage = true;

    nsCOMPtr<nsIObserverService> obsvc = services::GetObserverService();
    if (obsvc && mDocument) {
      nsPIDOMWindowOuter* window = mDocument->GetWindow();
      if (window && nsGlobalWindowOuter::Cast(window)->IsChromeWindow()) {
        obsvc->NotifyObservers(window, "widget-first-paint", nullptr);
      }
    }
  }
}

bool PresShell::IsVisible() const {
  if (!mIsActive || !mViewManager) {
    return false;
  }

  nsView* view = mViewManager->GetRootView();
  if (!view) {
    return true;
  }

  // inner view of subdoc frame
  view = view->GetParent();
  if (!view) {
    return true;
  }

  // subdoc view
  view = view->GetParent();
  if (!view) {
    return true;
  }

  nsIFrame* frame = view->GetFrame();
  if (!frame) {
    return true;
  }

  return frame->IsVisibleConsideringAncestors(
      nsIFrame::VISIBILITY_CROSS_CHROME_CONTENT_BOUNDARY);
}

void PresShell::SuppressDisplayport(bool aEnabled) {
  if (aEnabled) {
    mActiveSuppressDisplayport++;
  } else if (mActiveSuppressDisplayport > 0) {
    bool isSuppressed = IsDisplayportSuppressed();
    mActiveSuppressDisplayport--;
    if (isSuppressed && !IsDisplayportSuppressed()) {
      // We unsuppressed the displayport, trigger a paint
      if (nsIFrame* rootFrame = mFrameConstructor->GetRootFrame()) {
        rootFrame->SchedulePaint();
      }
    }
  }
}

static bool sDisplayPortSuppressionRespected = true;

void PresShell::RespectDisplayportSuppression(bool aEnabled) {
  bool isSuppressed = IsDisplayportSuppressed();
  sDisplayPortSuppressionRespected = aEnabled;
  if (isSuppressed && !IsDisplayportSuppressed()) {
    // We unsuppressed the displayport, trigger a paint
    if (nsIFrame* rootFrame = mFrameConstructor->GetRootFrame()) {
      rootFrame->SchedulePaint();
    }
  }
}

bool PresShell::IsDisplayportSuppressed() {
  return sDisplayPortSuppressionRespected && mActiveSuppressDisplayport > 0;
}

static CallState FreezeSubDocument(Document& aDocument) {
  if (PresShell* presShell = aDocument.GetPresShell()) {
    presShell->Freeze();
  }
  return CallState::Continue;
}

void PresShell::Freeze(bool aIncludeSubDocuments) {
  mUpdateApproximateFrameVisibilityEvent.Revoke();

  MaybeReleaseCapturingContent();

  if (mCaret) {
    SetCaretEnabled(false);
  }

  mPaintingSuppressed = true;

  if (aIncludeSubDocuments && mDocument) {
    mDocument->EnumerateSubDocuments(FreezeSubDocument);
  }

  nsPresContext* presContext = GetPresContext();
  if (presContext) {
    presContext->DisableInteractionTimeRecording();
    if (presContext->RefreshDriver()->GetPresContext() == presContext) {
      presContext->RefreshDriver()->Freeze();
    }

    if (nsPresContext* rootPresContext = presContext->GetRootPresContext()) {
      rootPresContext->ResetUserInputEventsAllowed();
    }
  }

  mFrozen = true;
  if (mDocument) {
    UpdateImageLockingState();
  }
}

void PresShell::FireOrClearDelayedEvents(bool aFireEvents) {
  mNoDelayedMouseEvents = false;
  mNoDelayedKeyEvents = false;
  if (!aFireEvents) {
    mDelayedEvents.Clear();
    return;
  }

  if (mDocument) {
    RefPtr<Document> doc = mDocument;
    while (!mIsDestroying && mDelayedEvents.Length() &&
           !doc->EventHandlingSuppressed()) {
      UniquePtr<DelayedEvent> ev = std::move(mDelayedEvents[0]);
      mDelayedEvents.RemoveElementAt(0);
      if (ev->IsKeyPressEvent() && mIsLastKeyDownCanceled) {
        continue;
      }
      ev->Dispatch();
    }
    if (!doc->EventHandlingSuppressed()) {
      mDelayedEvents.Clear();
    }
  }
}

void PresShell::Thaw(bool aIncludeSubDocuments) {
  nsPresContext* presContext = GetPresContext();
  if (presContext &&
      presContext->RefreshDriver()->GetPresContext() == presContext) {
    presContext->RefreshDriver()->Thaw();
  }

  if (aIncludeSubDocuments && mDocument) {
    mDocument->EnumerateSubDocuments([](Document& aSubDoc) {
      if (PresShell* presShell = aSubDoc.GetPresShell()) {
        presShell->Thaw();
      }
      return CallState::Continue;
    });
  }

  // Get the activeness of our presshell, as this might have changed
  // while we were in the bfcache
  ActivenessMaybeChanged();

  // We're now unfrozen
  mFrozen = false;
  UpdateImageLockingState();

  UnsuppressPainting();

  // In case the above UnsuppressPainting call didn't start the
  // refresh driver, we manually start the refresh driver to
  // ensure nsPresContext::MaybeIncreaseMeasuredTicksSinceLoading
  // can be called for user input events handling.
  if (presContext && presContext->IsRoot()) {
    if (!presContext->RefreshDriver()->HasPendingTick()) {
      presContext->RefreshDriver()->InitializeTimer();
    }
  }
}

//--------------------------------------------------------
// Start of protected and private methods on the PresShell
//--------------------------------------------------------

void PresShell::WillCauseReflow() {
  nsContentUtils::AddScriptBlocker();
  ++mChangeNestCount;
}

void PresShell::DidCauseReflow() {
  NS_ASSERTION(mChangeNestCount != 0, "Unexpected call to DidCauseReflow()");
  --mChangeNestCount;
  nsContentUtils::RemoveScriptBlocker();
}

void PresShell::WillDoReflow() {
  mDocument->FlushUserFontSet();

  mPresContext->FlushCounterStyles();

  mPresContext->FlushFontFeatureValues();

  mPresContext->FlushFontPaletteValues();

  mLastReflowStart = GetPerformanceNowUnclamped();
}

void PresShell::DidDoReflow(bool aInterruptible) {
  MOZ_ASSERT(mPendingDidDoReflow);
  if (!nsContentUtils::IsSafeToRunScript()) {
    // If we're reflowing while script-blocked (e.g. from container query
    // updates), defer our reflow callbacks until the end of our next layout
    // flush.
    SetNeedLayoutFlush();
    return;
  }

  auto clearPendingDidDoReflow =
      MakeScopeExit([&] { mPendingDidDoReflow = false; });

  mHiddenContentInForcedLayout.Clear();

  HandlePostedReflowCallbacks(aInterruptible);

  if (mIsDestroying) {
    return;
  }

  {
    nsAutoScriptBlocker scriptBlocker;
    AutoAssertNoFlush noReentrantFlush(*this);
    if (nsCOMPtr<nsIDocShell> docShell = mPresContext->GetDocShell()) {
      DOMHighResTimeStamp now = GetPerformanceNowUnclamped();
      docShell->NotifyReflowObservers(aInterruptible, mLastReflowStart, now);
    }

    if (StaticPrefs::layout_reflow_synthMouseMove()) {
      SynthesizeMouseMove(false);
    }

    mPresContext->NotifyMissingFonts();
  }

  if (mIsDestroying) {
    return;
  }

  if (mDirtyRoots.IsEmpty()) {
    // We only unsuppress painting if we're out of reflows.  It's pointless to
    // do so if reflows are still pending, since reflows are just going to
    // thrash the frames around some more.  By waiting we avoid an overeager
    // "jitter" effect.
    if (mShouldUnsuppressPainting) {
      mShouldUnsuppressPainting = false;
      UnsuppressAndInvalidate();
    }
  } else {
    // If any new reflow commands were enqueued during the reflow (or we didn't
    // reflow everything because we were interrupted), schedule another reflow
    // event to process them.
    //
    // Note that we want to do this after DidDoReflow(), since that method can
    // change whether there are dirty roots around by flushing, and there's no
    // point in posting a reflow event just to have the flush revoke it.
    EnsureLayoutFlush();
  }
}

DOMHighResTimeStamp PresShell::GetPerformanceNowUnclamped() {
  DOMHighResTimeStamp now = 0;

  if (nsPIDOMWindowInner* window = mDocument->GetInnerWindow()) {
    Performance* perf = window->GetPerformance();

    if (perf) {
      now = perf->NowUnclamped();
    }
  }

  return now;
}

bool PresShell::DoReflow(nsIFrame* target, bool aInterruptible,
                         OverflowChangedTracker* aOverflowTracker) {
  [[maybe_unused]] nsIURI* uri = mDocument->GetDocumentURI();
  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_RELEVANT_FOR_JS(
      "Reflow", LAYOUT_Reflow, uri ? uri->GetSpecOrDefault() : "N/A"_ns);

  PerfStats::AutoMetricRecording<PerfStats::Metric::Reflowing> autoRecording;

  gfxTextPerfMetrics* tp = mPresContext->GetTextPerfMetrics();
  TimeStamp timeStart;
  if (tp) {
    tp->Accumulate();
    tp->reflowCount++;
    timeStart = TimeStamp::Now();
  }

  // set up a cache that saves all nodes contained in each selection,
  // allowing a fast lookup in `nsTextFrame::IsFrameSelected()`.
  // This cache only lives throughout this reflow call.
  SelectionNodeCache cache(*this);

  // Schedule a paint, but don't actually mark this frame as changed for
  // retained DL building purposes. If any child frames get moved, then
  // they will schedule paint again. We could probaby skip this, and just
  // schedule a similar paint when a frame is deleted.
  target->SchedulePaint(nsIFrame::PAINT_DEFAULT, false);

  Maybe<uint64_t> innerWindowID;
  if (auto* window = mDocument->GetInnerWindow()) {
    innerWindowID = Some(window->WindowID());
  }
  AutoProfilerTracing tracingLayoutFlush(
      "Paint", aInterruptible ? "Reflow (interruptible)" : "Reflow (sync)",
      geckoprofiler::category::LAYOUT, std::move(mReflowCause), innerWindowID);
  mReflowCause = nullptr;

  FlushPendingScrollAnchorSelections();

  const bool isRoot = target == mFrameConstructor->GetRootFrame();

  MOZ_ASSERT(isRoot || aOverflowTracker,
             "caller must provide overflow tracker when reflowing "
             "non-root frames");

  // CreateReferenceRenderingContext can return nullptr
  UniquePtr<gfxContext> rcx(CreateReferenceRenderingContext());

#ifdef DEBUG
  mCurrentReflowRoot = target;
#endif

  // If the target frame is the root of the frame hierarchy, then
  // use all the available space. If it's simply a `reflow root',
  // then use the target frame's size as the available space.
  WritingMode wm = target->GetWritingMode();
  LogicalSize size(wm);
  if (isRoot) {
    size = LogicalSize(wm, mPresContext->GetVisibleArea().Size());
  } else {
    size = target->GetLogicalSize();
  }

  OverflowAreas oldOverflow;  // initialized and used only when !isRoot
  if (!isRoot) {
    oldOverflow = target->GetOverflowAreas();
  }

  NS_ASSERTION(!target->GetNextInFlow() && !target->GetPrevInFlow(),
               "reflow roots should never split");

  // Don't pass size directly to the reflow input, since a
  // constrained height implies page/column breaking.
  LogicalSize reflowSize(wm, size.ISize(wm), NS_UNCONSTRAINEDSIZE);
  ReflowInput reflowInput(mPresContext, target, rcx.get(), reflowSize,
                          ReflowInput::InitFlag::CallerWillInit);

  if (isRoot) {
    reflowInput.Init(mPresContext);
  } else {
    // Initialize reflow input with current used border and padding,
    // in case this was set specially by the parent frame when the reflow root
    // was reflowed by its parent.
    reflowInput.Init(mPresContext, Nothing(),
                     Some(target->GetLogicalUsedBorder(wm)),
                     Some(target->GetLogicalUsedPadding(wm)));
  }

  // fix the computed height
  NS_ASSERTION(reflowInput.ComputedPhysicalMargin() == nsMargin(0, 0, 0, 0),
               "reflow input should not set margin for reflow roots");
  if (size.BSize(wm) != NS_UNCONSTRAINEDSIZE) {
    nscoord computedBSize =
        size.BSize(wm) -
        reflowInput.ComputedLogicalBorderPadding(wm).BStartEnd(wm);
    computedBSize = std::max(computedBSize, 0);
    reflowInput.SetComputedBSize(computedBSize);
  }
  NS_ASSERTION(
      reflowInput.ComputedISize() ==
          size.ISize(wm) -
              reflowInput.ComputedLogicalBorderPadding(wm).IStartEnd(wm),
      "reflow input computed incorrect inline size");

  mPresContext->ReflowStarted(aInterruptible);
  mIsReflowing = true;

  nsReflowStatus status;
  ReflowOutput desiredSize(reflowInput);
  target->Reflow(mPresContext, desiredSize, reflowInput, status);

  // If an incremental reflow is initiated at a frame other than the
  // root frame, then its desired size had better not change!  If it's
  // initiated at the root, then the size better not change unless its
  // height was unconstrained to start with.
  nsRect boundsRelativeToTarget =
      nsRect(0, 0, desiredSize.Width(), desiredSize.Height());
  const bool isBSizeLimitReflow =
      isRoot && size.BSize(wm) == NS_UNCONSTRAINEDSIZE;
  NS_ASSERTION(isBSizeLimitReflow || desiredSize.Size(wm) == size,
               "non-root frame's desired size changed during an "
               "incremental reflow");
  NS_ASSERTION(status.IsEmpty(), "reflow roots should never split");

  target->SetSize(boundsRelativeToTarget.Size());

  // Always use boundsRelativeToTarget here, not desiredSize.InkOverflowRect(),
  // because for root frames (where they could be different, since root frames
  // are allowed to have overflow) the root view bounds need to match the
  // viewport bounds; the view manager "window dimensions" code depends on it.
  if (target->HasView()) {
    nsContainerFrame::SyncFrameViewAfterReflow(
        mPresContext, target, target->GetView(), boundsRelativeToTarget);
  }

  target->DidReflow(mPresContext, nullptr);
  if (target->IsInScrollAnchorChain()) {
    ScrollAnchorContainer* container = ScrollAnchorContainer::FindFor(target);
    PostPendingScrollAnchorAdjustment(container);
  }
  if (MOZ_UNLIKELY(isBSizeLimitReflow)) {
    mPresContext->SetVisibleArea(boundsRelativeToTarget);
  }

#ifdef DEBUG
  mCurrentReflowRoot = nullptr;
#endif

  if (!isRoot && oldOverflow != target->GetOverflowAreas()) {
    // The overflow area changed.  Propagate this change to ancestors.
    aOverflowTracker->AddFrame(target->GetParent(),
                               OverflowChangedTracker::CHILDREN_CHANGED);
  }

  NS_ASSERTION(
      mPresContext->HasPendingInterrupt() || mFramesToDirty.Count() == 0,
      "Why do we need to dirty anything if not interrupted?");

  mIsReflowing = false;
  bool interrupted = mPresContext->HasPendingInterrupt();
  if (interrupted) {
    // Make sure target gets reflowed again.
    for (const auto& key : mFramesToDirty) {
      // Mark frames dirty until target frame.
      for (nsIFrame* f = key; f && !f->IsSubtreeDirty(); f = f->GetParent()) {
        f->AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
        if (f->IsFlexItem()) {
          nsFlexContainerFrame::MarkCachedFlexMeasurementsDirty(f);
        }

        if (f == target) {
          break;
        }
      }
    }

    NS_ASSERTION(target->IsSubtreeDirty(), "Why is the target not dirty?");
    mDirtyRoots.Add(target);
    SetNeedLayoutFlush();

    // Clear mFramesToDirty after we've done the target->IsSubtreeDirty()
    // assertion so that if it fails it's easier to see what's going on.
#ifdef NOISY_INTERRUPTIBLE_REFLOW
    printf("mFramesToDirty.Count() == %u\n", mFramesToDirty.Count());
#endif /* NOISY_INTERRUPTIBLE_REFLOW */
    mFramesToDirty.Clear();

    // Any FlushPendingNotifications with interruptible reflows
    // should be suppressed now. We don't want to do extra reflow work
    // before our reflow event happens.
    mWasLastReflowInterrupted = true;
    EnsureLayoutFlush();
  }

  // dump text perf metrics for reflows with significant text processing
  if (tp) {
    if (tp->current.numChars > 100) {
      TimeDuration reflowTime = TimeStamp::Now() - timeStart;
      LogTextPerfStats(tp, this, tp->current, reflowTime.ToMilliseconds(),
                       eLog_reflow, nullptr);
    }
    tp->Accumulate();
  }

  return !interrupted;
}

#ifdef DEBUG
void PresShell::DoVerifyReflow() {
  if (GetVerifyReflowEnable()) {
    // First synchronously render what we have so far so that we can
    // see it.
    nsView* rootView = mViewManager->GetRootView();
    mViewManager->InvalidateView(rootView);

    FlushPendingNotifications(FlushType::Layout);
    mInVerifyReflow = true;
    bool ok = VerifyIncrementalReflow();
    mInVerifyReflow = false;
    if (VerifyReflowFlags::All & gVerifyReflowFlags) {
      printf("ProcessReflowCommands: finished (%s)\n", ok ? "ok" : "failed");
    }

    if (!mDirtyRoots.IsEmpty()) {
      printf("XXX yikes! reflow commands queued during verify-reflow\n");
    }
  }
}
#endif

// used with Telemetry metrics
#define NS_LONG_REFLOW_TIME_MS 5000

bool PresShell::ProcessReflowCommands(bool aInterruptible) {
  if (mDirtyRoots.IsEmpty() && !mShouldUnsuppressPainting &&
      !mPendingDidDoReflow) {
    // Nothing to do; bail out
    return true;
  }

  const bool wasProcessingReflowCommands = mProcessingReflowCommands;
  auto restoreProcessingReflowCommands = MakeScopeExit(
      [&] { mProcessingReflowCommands = wasProcessingReflowCommands; });
  mProcessingReflowCommands = true;

  auto timerStart = mozilla::TimeStamp::Now();
  bool interrupted = false;
  if (!mDirtyRoots.IsEmpty()) {
#ifdef DEBUG
    if (VerifyReflowFlags::DumpCommands & gVerifyReflowFlags) {
      printf("ProcessReflowCommands: begin incremental reflow\n");
    }
#endif

    // If reflow is interruptible, then make a note of our deadline.
    const PRIntervalTime deadline =
        aInterruptible
            ? PR_IntervalNow() + PR_MicrosecondsToInterval(gMaxRCProcessingTime)
            : (PRIntervalTime)0;

    // Scope for the reflow entry point
    nsAutoScriptBlocker scriptBlocker;
    WillDoReflow();
    AUTO_LAYOUT_PHASE_ENTRY_POINT(GetPresContext(), Reflow);
    nsViewManager::AutoDisableRefresh refreshBlocker(mViewManager);

    OverflowChangedTracker overflowTracker;

    do {
      // Send an incremental reflow notification to the target frame.
      nsIFrame* target = mDirtyRoots.PopShallowestRoot();

      if (!target->IsSubtreeDirty()) {
        // It's not dirty anymore, which probably means the notification
        // was posted in the middle of a reflow (perhaps with a reflow
        // root in the middle).  Don't do anything.
        continue;
      }

      interrupted = !DoReflow(target, aInterruptible, &overflowTracker);

      // Keep going until we're out of reflow commands, or we've run
      // past our deadline, or we're interrupted.
    } while (!interrupted && !mDirtyRoots.IsEmpty() &&
             (!aInterruptible || PR_IntervalNow() < deadline));

    interrupted = !mDirtyRoots.IsEmpty();

    overflowTracker.Flush();

    if (!interrupted) {
      // We didn't get interrupted. Go ahead and perform scroll anchor
      // adjustments.
      FlushPendingScrollAnchorAdjustments();
    }
    mPendingDidDoReflow = true;
  }

  // Exiting the scriptblocker might have killed us. If we were processing
  // scroll commands, let the outermost call deal with it.
  if (!mIsDestroying && mPendingDidDoReflow && !wasProcessingReflowCommands) {
    DidDoReflow(aInterruptible);
  }

#ifdef DEBUG
  if (VerifyReflowFlags::DumpCommands & gVerifyReflowFlags) {
    printf("\nPresShell::ProcessReflowCommands() finished: this=%p\n",
           (void*)this);
  }
  DoVerifyReflow();
#endif

  {
    TimeDuration elapsed = TimeStamp::Now() - timerStart;
    int32_t intElapsed = int32_t(elapsed.ToMilliseconds());
    if (intElapsed > NS_LONG_REFLOW_TIME_MS) {
      glean::layout::long_reflow_interruptible
          .EnumGet(aInterruptible
                       ? glean::layout::LongReflowInterruptibleLabel::eTrue
                       : glean::layout::LongReflowInterruptibleLabel::eFalse)
          .Add();
    }
  }

  return !interrupted;
}

bool PresShell::DoFlushLayout(bool aInterruptible) {
  mFrameConstructor->RecalcQuotesAndCounters();
  return ProcessReflowCommands(aInterruptible);
}

void PresShell::WindowSizeMoveDone() {
  if (mPresContext) {
    EventStateManager::ClearGlobalActiveContent(nullptr);
    ClearMouseCapture();
  }
}

NS_IMETHODIMP
PresShell::Observe(nsISupports* aSubject, const char* aTopic,
                   const char16_t* aData) {
  if (mIsDestroying) {
    NS_WARNING("our observers should have been unregistered by now");
    return NS_OK;
  }

  if (!nsCRT::strcmp(aTopic, "memory-pressure")) {
    if (!AssumeAllFramesVisible() &&
        mPresContext->IsRootContentDocumentInProcess()) {
      DoUpdateApproximateFrameVisibility(/* aRemoveOnly = */ true);
    }
    return NS_OK;
  }

  if (!nsCRT::strcmp(aTopic, NS_WIDGET_WAKE_OBSERVER_TOPIC)) {
    mLastOSWake = TimeStamp::Now();
    return NS_OK;
  }

  // For parent process, user may expect the UI is interactable after a
  // tab (previously opened page or home page) has restored.
  if (!nsCRT::strcmp(aTopic, "sessionstore-one-or-no-tab-restored")) {
    MOZ_ASSERT(XRE_IsParentProcess());
    sProcessInteractable = true;

    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      os->RemoveObserver(this, "sessionstore-one-or-no-tab-restored");
    }
    return NS_OK;
  }

  if (!nsCRT::strcmp(aTopic, "font-info-updated")) {
    // See how gfxPlatform::ForceGlobalReflow encodes this.
    bool needsReframe = aData && !!aData[0];
    mPresContext->ForceReflowForFontInfoUpdate(needsReframe);
    return NS_OK;
  }

  // The "look-and-feel-changed" notification for JS observers will be
  // dispatched HandleGlobalThemeChange once LookAndFeel caches are cleared.
  if (!nsCRT::strcmp(aTopic, "internal-look-and-feel-changed")) {
    // See how LookAndFeel::NotifyChangedAllWindows encodes this.
    auto kind = widget::ThemeChangeKind(aData[0]);
    mPresContext->ThemeChanged(kind);
    return NS_OK;
  }

  NS_WARNING("unrecognized topic in PresShell::Observe");
  return NS_ERROR_FAILURE;
}

bool PresShell::AddRefreshObserver(nsARefreshObserver* aObserver,
                                   FlushType aFlushType,
                                   const char* aObserverDescription) {
  nsPresContext* presContext = GetPresContext();
  if (MOZ_UNLIKELY(!presContext)) {
    return false;
  }
  presContext->RefreshDriver()->AddRefreshObserver(aObserver, aFlushType,
                                                   aObserverDescription);
  return true;
}

bool PresShell::RemoveRefreshObserver(nsARefreshObserver* aObserver,
                                      FlushType aFlushType) {
  nsPresContext* presContext = GetPresContext();
  return presContext && presContext->RefreshDriver()->RemoveRefreshObserver(
                            aObserver, aFlushType);
}

bool PresShell::AddPostRefreshObserver(nsAPostRefreshObserver* aObserver) {
  nsPresContext* presContext = GetPresContext();
  if (!presContext) {
    return false;
  }
  presContext->RefreshDriver()->AddPostRefreshObserver(aObserver);
  return true;
}

bool PresShell::RemovePostRefreshObserver(nsAPostRefreshObserver* aObserver) {
  nsPresContext* presContext = GetPresContext();
  if (!presContext) {
    return false;
  }
  presContext->RefreshDriver()->RemovePostRefreshObserver(aObserver);
  return true;
}

void PresShell::ScheduleFlush() {
  if (MOZ_UNLIKELY(IsDestroying()) ||
      MOZ_UNLIKELY(mDocument->GetBFCacheEntry())) {
    return;
  }
  mPresContext->RefreshDriver()->ScheduleRenderingPhase(RenderingPhase::Layout);
}

//------------------------------------------------------
// End of protected and private methods on the PresShell
//------------------------------------------------------

//------------------------------------------------------------------
//-- Delayed event Classes Impls
//------------------------------------------------------------------

PresShell::DelayedInputEvent::DelayedInputEvent()
    : DelayedEvent(), mEvent(nullptr) {}

PresShell::DelayedInputEvent::~DelayedInputEvent() { delete mEvent; }

void PresShell::DelayedInputEvent::Dispatch() {
  if (!mEvent || !mEvent->mWidget) {
    return;
  }
  nsCOMPtr<nsIWidget> widget = mEvent->mWidget;
  nsEventStatus status;
  widget->DispatchEvent(mEvent, status);
}

PresShell::DelayedMouseEvent::DelayedMouseEvent(WidgetMouseEvent* aEvent) {
  MOZ_DIAGNOSTIC_ASSERT(aEvent->IsTrusted());
  WidgetMouseEvent* mouseEvent =
      new WidgetMouseEvent(true, aEvent->mMessage, aEvent->mWidget,
                           aEvent->mReason, aEvent->mContextMenuTrigger);
  mouseEvent->AssignMouseEventData(*aEvent, false);
  mEvent = mouseEvent;
}

PresShell::DelayedPointerEvent::DelayedPointerEvent(
    WidgetPointerEvent* aEvent) {
  MOZ_DIAGNOSTIC_ASSERT(aEvent->IsTrusted());
  MOZ_ASSERT(aEvent->mMessage == eContextMenu);
  WidgetPointerEvent* pointerEvent = new WidgetPointerEvent(
      true, aEvent->mMessage, aEvent->mWidget, aEvent->mContextMenuTrigger);
  pointerEvent->AssignPointerEventData(*aEvent, false);
  mEvent = pointerEvent;
}

PresShell::DelayedKeyEvent::DelayedKeyEvent(WidgetKeyboardEvent* aEvent) {
  MOZ_DIAGNOSTIC_ASSERT(aEvent->IsTrusted());
  WidgetKeyboardEvent* keyEvent =
      new WidgetKeyboardEvent(true, aEvent->mMessage, aEvent->mWidget);
  keyEvent->AssignKeyEventData(*aEvent, false);
  keyEvent->mFlags.mIsSynthesizedForTests =
      aEvent->mFlags.mIsSynthesizedForTests;
  keyEvent->mFlags.mIsSuppressedOrDelayed = true;
  mEvent = keyEvent;
}

bool PresShell::DelayedKeyEvent::IsKeyPressEvent() {
  return mEvent->mMessage == eKeyPress;
}

// Start of DEBUG only code

#ifdef DEBUG

static void LogVerifyMessage(nsIFrame* k1, nsIFrame* k2, const char* aMsg) {
  nsAutoString n1, n2;
  if (k1) {
    k1->GetFrameName(n1);
  } else {
    n1.AssignLiteral(u"(null)");
  }

  if (k2) {
    k2->GetFrameName(n2);
  } else {
    n2.AssignLiteral(u"(null)");
  }

  printf("verifyreflow: %s %p != %s %p  %s\n",
         NS_LossyConvertUTF16toASCII(n1).get(), (void*)k1,
         NS_LossyConvertUTF16toASCII(n2).get(), (void*)k2, aMsg);
}

static void LogVerifyMessage(nsIFrame* k1, nsIFrame* k2, const char* aMsg,
                             const nsRect& r1, const nsRect& r2) {
  printf("VerifyReflow Error:\n");
  nsAutoString name;

  if (k1) {
    k1->GetFrameName(name);
    printf("  %s %p ", NS_LossyConvertUTF16toASCII(name).get(), (void*)k1);
  }
  printf("{%d, %d, %d, %d} != \n", r1.x, r1.y, r1.width, r1.height);

  if (k2) {
    k2->GetFrameName(name);
    printf("  %s %p ", NS_LossyConvertUTF16toASCII(name).get(), (void*)k2);
  }
  printf("{%d, %d, %d, %d}\n  %s\n", r2.x, r2.y, r2.width, r2.height, aMsg);
}

static void LogVerifyMessage(nsIFrame* k1, nsIFrame* k2, const char* aMsg,
                             const nsIntRect& r1, const nsIntRect& r2) {
  printf("VerifyReflow Error:\n");
  nsAutoString name;

  if (k1) {
    k1->GetFrameName(name);
    printf("  %s %p ", NS_LossyConvertUTF16toASCII(name).get(), (void*)k1);
  }
  printf("{%d, %d, %d, %d} != \n", r1.x, r1.y, r1.width, r1.height);

  if (k2) {
    k2->GetFrameName(name);
    printf("  %s %p ", NS_LossyConvertUTF16toASCII(name).get(), (void*)k2);
  }
  printf("{%d, %d, %d, %d}\n  %s\n", r2.x, r2.y, r2.width, r2.height, aMsg);
}

static bool CompareTrees(nsPresContext* aFirstPresContext,
                         nsIFrame* aFirstFrame,
                         nsPresContext* aSecondPresContext,
                         nsIFrame* aSecondFrame) {
  if (!aFirstPresContext || !aFirstFrame || !aSecondPresContext ||
      !aSecondFrame)
    return true;
  // XXX Evil hack to reduce false positives; I can't seem to figure
  // out how to flush scrollbar changes correctly
  // if (aFirstFrame->IsScrollbarFrame())
  //  return true;
  bool ok = true;
  const auto& childLists1 = aFirstFrame->ChildLists();
  const auto& childLists2 = aSecondFrame->ChildLists();
  auto iterLists1 = childLists1.begin();
  auto iterLists2 = childLists2.begin();
  do {
    const nsFrameList& kids1 = iterLists1 != childLists1.end()
                                   ? iterLists1->mList
                                   : nsFrameList::EmptyList();
    const nsFrameList& kids2 = iterLists2 != childLists2.end()
                                   ? iterLists2->mList
                                   : nsFrameList::EmptyList();
    int32_t l1 = kids1.GetLength();
    int32_t l2 = kids2.GetLength();
    if (l1 != l2) {
      ok = false;
      LogVerifyMessage(kids1.FirstChild(), kids2.FirstChild(),
                       "child counts don't match: ");
      printf("%d != %d\n", l1, l2);
      if (!(VerifyReflowFlags::All & gVerifyReflowFlags)) {
        break;
      }
    }

    LayoutDeviceIntRect r1, r2;
    nsView* v1;
    nsView* v2;
    for (auto kids1Iter = kids1.begin(), kids2Iter = kids2.begin();;
         ++kids1Iter, ++kids2Iter) {
      nsIFrame* k1 = *kids1Iter;
      nsIFrame* k2 = *kids2Iter;
      if (((nullptr == k1) && (nullptr != k2)) ||
          ((nullptr != k1) && (nullptr == k2))) {
        ok = false;
        LogVerifyMessage(k1, k2, "child lists are different\n");
        break;
      } else if (nullptr != k1) {
        // Verify that the frames are the same size
        if (!k1->GetRect().IsEqualInterior(k2->GetRect())) {
          ok = false;
          LogVerifyMessage(k1, k2, "(frame rects)", k1->GetRect(),
                           k2->GetRect());
        }

        // Make sure either both have views or neither have views; if they
        // do have views, make sure the views are the same size. If the
        // views have widgets, make sure they both do or neither does. If
        // they do, make sure the widgets are the same size.
        v1 = k1->GetView();
        v2 = k2->GetView();
        if (((nullptr == v1) && (nullptr != v2)) ||
            ((nullptr != v1) && (nullptr == v2))) {
          ok = false;
          LogVerifyMessage(k1, k2, "child views are not matched\n");
        } else if (nullptr != v1) {
          if (!v1->GetBounds().IsEqualInterior(v2->GetBounds())) {
            LogVerifyMessage(k1, k2, "(view rects)", v1->GetBounds(),
                             v2->GetBounds());
          }

          nsIWidget* w1 = v1->GetWidget();
          nsIWidget* w2 = v2->GetWidget();
          if (((nullptr == w1) && (nullptr != w2)) ||
              ((nullptr != w1) && (nullptr == w2))) {
            ok = false;
            LogVerifyMessage(k1, k2, "child widgets are not matched\n");
          } else if (nullptr != w1) {
            r1 = w1->GetBounds();
            r2 = w2->GetBounds();
            if (!r1.IsEqualEdges(r2)) {
              LogVerifyMessage(k1, k2, "(widget rects)", r1.ToUnknownRect(),
                               r2.ToUnknownRect());
            }
          }
        }
        if (!ok && !(VerifyReflowFlags::All & gVerifyReflowFlags)) {
          break;
        }

        // XXX Should perhaps compare their float managers.

        // Compare the sub-trees too
        if (!CompareTrees(aFirstPresContext, k1, aSecondPresContext, k2)) {
          ok = false;
          if (!(VerifyReflowFlags::All & gVerifyReflowFlags)) {
            break;
          }
        }
      } else {
        break;
      }
    }
    if (!ok && (!(VerifyReflowFlags::All & gVerifyReflowFlags))) {
      break;
    }

    ++iterLists1;
    ++iterLists2;
    const bool lists1Done = iterLists1 == childLists1.end();
    const bool lists2Done = iterLists2 == childLists2.end();
    if (lists1Done != lists2Done ||
        (!lists1Done && iterLists1->mID != iterLists2->mID)) {
      if (!(VerifyReflowFlags::All & gVerifyReflowFlags)) {
        ok = false;
      }
      LogVerifyMessage(kids1.FirstChild(), kids2.FirstChild(),
                       "child list names are not matched: ");
      fprintf(stdout, "%s != %s\n",
              !lists1Done ? ChildListName(iterLists1->mID) : "(null)",
              !lists2Done ? ChildListName(iterLists2->mID) : "(null)");
      break;
    }
  } while (ok && iterLists1 != childLists1.end());

  return ok;
}
#endif

#if 0
static nsIFrame*
FindTopFrame(nsIFrame* aRoot)
{
  if (aRoot) {
    nsIContent* content = aRoot->GetContent();
    if (content) {
      nsAtom* tag;
      content->GetTag(tag);
      if (nullptr != tag) {
        NS_RELEASE(tag);
        return aRoot;
      }
    }

    // Try one of the children
    for (nsIFrame* kid : aRoot->PrincipalChildList()) {
      nsIFrame* result = FindTopFrame(kid);
      if (nullptr != result) {
        return result;
      }
    }
  }
  return nullptr;
}
#endif

#ifdef DEBUG

// After an incremental reflow, we verify the correctness by doing a
// full reflow into a fresh frame tree.
bool PresShell::VerifyIncrementalReflow() {
  if (VerifyReflowFlags::Noisy & gVerifyReflowFlags) {
    printf("Building Verification Tree...\n");
  }

  // Create a presentation context to view the new frame tree
  auto cx = MakeRefPtr<nsRootPresContext>(
      mDocument, mPresContext->IsPaginated()
                     ? nsPresContext::eContext_PrintPreview
                     : nsPresContext::eContext_Galley);

  nsDeviceContext* dc = mPresContext->DeviceContext();
  nsresult rv = cx->Init(dc);
  NS_ENSURE_SUCCESS(rv, false);

  // Get our scrolling preference
  nsView* rootView = mViewManager->GetRootView();
  NS_ENSURE_TRUE(rootView->HasWidget(), false);
  nsIWidget* parentWidget = rootView->GetWidget();

  // Create a new view manager.
  auto vm = MakeRefPtr<nsViewManager>();
  rv = vm->Init(dc);
  NS_ENSURE_SUCCESS(rv, false);

  // Create a child window of the parent that is our "root view/window"
  // Create a view
  nsRect tbounds = mPresContext->GetVisibleArea();
  nsView* view = vm->CreateView(tbounds, nullptr);
  NS_ENSURE_TRUE(view, false);

  // now create the widget for the view
  rv = view->CreateWidget(parentWidget, true);
  NS_ENSURE_SUCCESS(rv, false);

  // Setup hierarchical relationship in view manager
  vm->SetRootView(view);

  // Make the new presentation context the same size as our
  // presentation context.
  cx->SetVisibleArea(mPresContext->GetVisibleArea());

  RefPtr<PresShell> presShell = mDocument->CreatePresShell(cx, vm);
  NS_ENSURE_TRUE(presShell, false);

  // Note that after we create the shell, we must make sure to destroy it
  presShell->SetVerifyReflowEnable(
      false);  // turn off verify reflow while we're
               // reflowing the test frame tree
  vm->SetPresShell(presShell);
  {
    nsAutoCauseReflowNotifier crNotifier(this);
    presShell->Initialize();
  }
  presShell->FlushPendingNotifications(FlushType::Layout);
  presShell->SetVerifyReflowEnable(
      true);  // turn on verify reflow again now that
              // we're done reflowing the test frame tree
  // Force the non-primary presshell to unsuppress; it doesn't want to normally
  // because it thinks it's hidden
  presShell->mPaintingSuppressed = false;
  if (VerifyReflowFlags::Noisy & gVerifyReflowFlags) {
    printf("Verification Tree built, comparing...\n");
  }

  // Now that the document has been reflowed, use its frame tree to
  // compare against our frame tree.
  nsIFrame* root1 = mFrameConstructor->GetRootFrame();
  nsIFrame* root2 = presShell->GetRootFrame();
  bool ok = CompareTrees(mPresContext, root1, cx, root2);
  if (!ok && (VerifyReflowFlags::Noisy & gVerifyReflowFlags)) {
    printf("Verify reflow failed, primary tree:\n");
    root1->List(stdout);
    printf("Verification tree:\n");
    root2->List(stdout);
  }

#  if 0
  // Sample code for dumping page to png
  // XXX Needs to be made more flexible
  if (!ok) {
    nsString stra;
    static int num = 0;
    stra.AppendLiteral("C:\\mozilla\\mozilla\\debug\\filea");
    stra.AppendInt(num);
    stra.AppendLiteral(".png");
    gfxUtils::WriteAsPNG(presShell, stra);
    nsString strb;
    strb.AppendLiteral("C:\\mozilla\\mozilla\\debug\\fileb");
    strb.AppendInt(num);
    strb.AppendLiteral(".png");
    gfxUtils::WriteAsPNG(presShell, strb);
    ++num;
  }
#  endif

  presShell->EndObservingDocument();
  presShell->Destroy();
  if (VerifyReflowFlags::Noisy & gVerifyReflowFlags) {
    printf("Finished Verifying Reflow...\n");
  }

  return ok;
}

// Layout debugging hooks
void PresShell::ListComputedStyles(FILE* out, int32_t aIndent) {
  nsIFrame* rootFrame = GetRootFrame();
  if (rootFrame) {
    rootFrame->Style()->List(out, aIndent);
  }

  // The root element's frame's ComputedStyle is the root of a separate tree.
  Element* rootElement = mDocument->GetRootElement();
  if (rootElement) {
    nsIFrame* rootElementFrame = rootElement->GetPrimaryFrame();
    if (rootElementFrame) {
      rootElementFrame->Style()->List(out, aIndent);
    }
  }
}
#endif

#if defined(DEBUG) || defined(MOZ_LAYOUT_DEBUGGER)
void PresShell::ListStyleSheets(FILE* out, int32_t aIndent) {
  auto ListStyleSheetsAtOrigin = [this, out, aIndent](StyleOrigin origin) {
    int32_t sheetCount = StyleSet()->SheetCount(origin);
    for (int32_t i = 0; i < sheetCount; ++i) {
      StyleSet()->SheetAt(origin, i)->List(out, aIndent);
    }
  };

  ListStyleSheetsAtOrigin(StyleOrigin::UserAgent);
  ListStyleSheetsAtOrigin(StyleOrigin::User);
  ListStyleSheetsAtOrigin(StyleOrigin::Author);
}
#endif

//=============================================================
//=============================================================
//-- Debug Reflow Counts
//=============================================================
//=============================================================
#ifdef MOZ_REFLOW_PERF
//-------------------------------------------------------------
void PresShell::DumpReflows() {
  if (mReflowCountMgr) {
    nsAutoCString uriStr;
    if (mDocument) {
      nsIURI* uri = mDocument->GetDocumentURI();
      if (uri) {
        uri->GetPathQueryRef(uriStr);
      }
    }
    mReflowCountMgr->DisplayTotals(uriStr.get());
    mReflowCountMgr->DisplayHTMLTotals(uriStr.get());
    mReflowCountMgr->DisplayDiffsInTotals();
  }
}

//-------------------------------------------------------------
void PresShell::CountReflows(const char* aName, nsIFrame* aFrame) {
  if (mReflowCountMgr) {
    mReflowCountMgr->Add(aName, aFrame);
  }
}

//-------------------------------------------------------------
void PresShell::PaintCount(const char* aName, gfxContext* aRenderingContext,
                           nsPresContext* aPresContext, nsIFrame* aFrame,
                           const nsPoint& aOffset, uint32_t aColor) {
  if (mReflowCountMgr) {
    mReflowCountMgr->PaintCount(aName, aRenderingContext, aPresContext, aFrame,
                                aOffset, aColor);
  }
}

//-------------------------------------------------------------
void PresShell::SetPaintFrameCount(bool aPaintFrameCounts) {
  if (mReflowCountMgr) {
    mReflowCountMgr->SetPaintFrameCounts(aPaintFrameCounts);
  }
}

bool PresShell::IsPaintingFrameCounts() {
  if (mReflowCountMgr) return mReflowCountMgr->IsPaintingFrameCounts();
  return false;
}

//------------------------------------------------------------------
//-- Reflow Counter Classes Impls
//------------------------------------------------------------------

//------------------------------------------------------------------
ReflowCounter::ReflowCounter(ReflowCountMgr* aMgr) : mMgr(aMgr) {
  ClearTotals();
  SetTotalsCache();
}

//------------------------------------------------------------------
ReflowCounter::~ReflowCounter() = default;

//------------------------------------------------------------------
void ReflowCounter::ClearTotals() { mTotal = 0; }

//------------------------------------------------------------------
void ReflowCounter::SetTotalsCache() { mCacheTotal = mTotal; }

//------------------------------------------------------------------
void ReflowCounter::CalcDiffInTotals() { mCacheTotal = mTotal - mCacheTotal; }

//------------------------------------------------------------------
void ReflowCounter::DisplayTotals(const char* aStr) {
  DisplayTotals(mTotal, aStr ? aStr : "Totals");
}

//------------------------------------------------------------------
void ReflowCounter::DisplayDiffTotals(const char* aStr) {
  DisplayTotals(mCacheTotal, aStr ? aStr : "Diff Totals");
}

//------------------------------------------------------------------
void ReflowCounter::DisplayHTMLTotals(const char* aStr) {
  DisplayHTMLTotals(mTotal, aStr ? aStr : "Totals");
}

//------------------------------------------------------------------
void ReflowCounter::DisplayTotals(uint32_t aTotal, const char* aTitle) {
  // figure total
  if (aTotal == 0) {
    return;
  }
  ReflowCounter* gTots = (ReflowCounter*)mMgr->LookUp(kGrandTotalsStr);

  printf("%25s\t", aTitle);
  printf("%d\t", aTotal);
  if (gTots != this && aTotal > 0) {
    gTots->Add(aTotal);
  }
}

//------------------------------------------------------------------
void ReflowCounter::DisplayHTMLTotals(uint32_t aTotal, const char* aTitle) {
  if (aTotal == 0) {
    return;
  }

  ReflowCounter* gTots = (ReflowCounter*)mMgr->LookUp(kGrandTotalsStr);
  FILE* fd = mMgr->GetOutFile();
  if (!fd) {
    return;
  }

  fprintf(fd, "<tr><td><center>%s</center></td>", aTitle);
  fprintf(fd, "<td><center>%d</center></td></tr>\n", aTotal);

  if (gTots != this && aTotal > 0) {
    gTots->Add(aTotal);
  }
}

//------------------------------------------------------------------
//-- ReflowCountMgr
//------------------------------------------------------------------

#  define KEY_BUF_SIZE_FOR_PTR \
    24  // adequate char[] buffer to sprintf a pointer

ReflowCountMgr::ReflowCountMgr() : mCounts(10), mIndiFrameCounts(10) {
  mCycledOnce = false;
  mDumpFrameCounts = false;
  mDumpFrameByFrameCounts = false;
  mPaintFrameByFrameCounts = false;
}

//------------------------------------------------------------------
ReflowCountMgr::~ReflowCountMgr() = default;

//------------------------------------------------------------------
ReflowCounter* ReflowCountMgr::LookUp(const char* aName) {
  return mCounts.Get(aName);
}

//------------------------------------------------------------------
void ReflowCountMgr::Add(const char* aName, nsIFrame* aFrame) {
  NS_ASSERTION(aName != nullptr, "Name shouldn't be null!");

  if (mDumpFrameCounts) {
    auto* const counter = mCounts.GetOrInsertNew(aName, this);
    counter->Add();
  }

  if ((mDumpFrameByFrameCounts || mPaintFrameByFrameCounts) &&
      aFrame != nullptr) {
    char key[KEY_BUF_SIZE_FOR_PTR];
    SprintfLiteral(key, "%p", (void*)aFrame);
    auto* const counter =
        mIndiFrameCounts
            .LookupOrInsertWith(key,
                                [&aName, &aFrame, this]() {
                                  auto counter =
                                      MakeUnique<IndiReflowCounter>(this);
                                  counter->mFrame = aFrame;
                                  counter->mName.AssignASCII(aName);
                                  return counter;
                                })
            .get();
    // this eliminates extra counts from super classes
    if (counter && counter->mName.EqualsASCII(aName)) {
      counter->mCount++;
      counter->mCounter.Add(1);
    }
  }
}

//------------------------------------------------------------------
void ReflowCountMgr::PaintCount(const char* aName,
                                gfxContext* aRenderingContext,
                                nsPresContext* aPresContext, nsIFrame* aFrame,
                                const nsPoint& aOffset, uint32_t aColor) {
  if (mPaintFrameByFrameCounts && aFrame != nullptr) {
    char key[KEY_BUF_SIZE_FOR_PTR];
    SprintfLiteral(key, "%p", (void*)aFrame);
    IndiReflowCounter* counter = mIndiFrameCounts.Get(key);
    if (counter != nullptr && counter->mName.EqualsASCII(aName)) {
      DrawTarget* drawTarget = aRenderingContext->GetDrawTarget();
      int32_t appUnitsPerDevPixel = aPresContext->AppUnitsPerDevPixel();

      aRenderingContext->Save();
      gfxPoint devPixelOffset =
          nsLayoutUtils::PointToGfxPoint(aOffset, appUnitsPerDevPixel);
      aRenderingContext->SetMatrixDouble(
          aRenderingContext->CurrentMatrixDouble().PreTranslate(
              devPixelOffset));

      // We don't care about the document language or user fonts here;
      // just get a default Latin font.
      nsFont font(StyleGenericFontFamily::Serif, Length::FromPixels(11));
      nsFontMetrics::Params params;
      params.language = nsGkAtoms::x_western;
      params.textPerf = aPresContext->GetTextPerfMetrics();
      params.featureValueLookup = aPresContext->GetFontFeatureValuesLookup();
      RefPtr<nsFontMetrics> fm = aPresContext->GetMetricsFor(font, params);

      char buf[16];
      int len = SprintfLiteral(buf, "%d", counter->mCount);
      nscoord x = 0, y = fm->MaxAscent();
      nscoord width, height = fm->MaxHeight();
      fm->SetTextRunRTL(false);
      width = fm->GetWidth(buf, len, drawTarget);

      sRGBColor color;
      sRGBColor color2;
      if (aColor != 0) {
        color = sRGBColor::FromABGR(aColor);
        color2 = sRGBColor(0.f, 0.f, 0.f);
      } else {
        gfx::Float rc = 0.f, gc = 0.f, bc = 0.f;
        if (counter->mCount < 5) {
          rc = 1.f;
          gc = 1.f;
        } else if (counter->mCount < 11) {
          gc = 1.f;
        } else {
          rc = 1.f;
        }
        color = sRGBColor(rc, gc, bc);
        color2 = sRGBColor(rc / 2, gc / 2, bc / 2);
      }

      nsRect rect(0, 0, width + 15, height + 15);
      Rect devPxRect =
          NSRectToSnappedRect(rect, appUnitsPerDevPixel, *drawTarget);
      ColorPattern black(ToDeviceColor(sRGBColor::OpaqueBlack()));
      drawTarget->FillRect(devPxRect, black);

      aRenderingContext->SetColor(color2);
      fm->DrawString(buf, len, x + 15, y + 15, aRenderingContext);
      aRenderingContext->SetColor(color);
      fm->DrawString(buf, len, x, y, aRenderingContext);

      aRenderingContext->Restore();
    }
  }
}

//------------------------------------------------------------------
void ReflowCountMgr::DoGrandTotals() {
  mCounts.WithEntryHandle(kGrandTotalsStr, [this](auto&& entry) {
    if (!entry) {
      entry.Insert(MakeUnique<ReflowCounter>(this));
    } else {
      entry.Data()->ClearTotals();
    }
  });

  printf("\t\t\t\tTotal\n");
  for (uint32_t i = 0; i < 78; i++) {
    printf("-");
  }
  printf("\n");
  for (const auto& entry : mCounts) {
    entry.GetData()->DisplayTotals(entry.GetKey());
  }
}

static void RecurseIndiTotals(
    nsPresContext* aPresContext,
    nsClassHashtable<nsCharPtrHashKey, IndiReflowCounter>& aHT,
    nsIFrame* aParentFrame, int32_t aLevel) {
  if (aParentFrame == nullptr) {
    return;
  }

  char key[KEY_BUF_SIZE_FOR_PTR];
  SprintfLiteral(key, "%p", (void*)aParentFrame);
  IndiReflowCounter* counter = aHT.Get(key);
  if (counter) {
    counter->mHasBeenOutput = true;
    char* name = ToNewCString(counter->mName);
    for (int32_t i = 0; i < aLevel; i++) printf(" ");
    printf("%s - %p   [%d][", name, (void*)aParentFrame, counter->mCount);
    printf("%d", counter->mCounter.GetTotal());
    printf("]\n");
    free(name);
  }

  for (nsIFrame* child : aParentFrame->PrincipalChildList()) {
    RecurseIndiTotals(aPresContext, aHT, child, aLevel + 1);
  }
}

//------------------------------------------------------------------
void ReflowCountMgr::DoIndiTotalsTree() {
  printf("\n------------------------------------------------\n");
  printf("-- Individual Frame Counts\n");
  printf("------------------------------------------------\n");

  if (mPresShell) {
    nsIFrame* rootFrame = mPresShell->GetRootFrame();
    RecurseIndiTotals(mPresContext, mIndiFrameCounts, rootFrame, 0);
    printf("------------------------------------------------\n");
    printf("-- Individual Counts of Frames not in Root Tree\n");
    printf("------------------------------------------------\n");
    for (const auto& counter : mIndiFrameCounts.Values()) {
      if (!counter->mHasBeenOutput) {
        char* name = ToNewCString(counter->mName);
        printf("%s - %p   [%d][", name, (void*)counter->mFrame,
               counter->mCount);
        printf("%d", counter->mCounter.GetTotal());
        printf("]\n");
        free(name);
      }
    }
  }
}

//------------------------------------------------------------------
void ReflowCountMgr::DoGrandHTMLTotals() {
  mCounts.WithEntryHandle(kGrandTotalsStr, [this](auto&& entry) {
    if (!entry) {
      entry.Insert(MakeUnique<ReflowCounter>(this));
    } else {
      entry.Data()->ClearTotals();
    }
  });

  static const char* title[] = {"Class", "Reflows"};
  fprintf(mFD, "<tr>");
  for (uint32_t i = 0; i < std::size(title); i++) {
    fprintf(mFD, "<td><center><b>%s<b></center></td>", title[i]);
  }
  fprintf(mFD, "</tr>\n");

  for (const auto& entry : mCounts) {
    entry.GetData()->DisplayHTMLTotals(entry.GetKey());
  }
}

//------------------------------------
void ReflowCountMgr::DisplayTotals(const char* aStr) {
  if (mDumpFrameCounts) {
    DoGrandTotals();
  }
  if (mDumpFrameByFrameCounts) {
    DoIndiTotalsTree();
  }
}
//------------------------------------
void ReflowCountMgr::DisplayHTMLTotals(const char* aStr) {
#  ifdef WIN32x  // XXX NOT XP!
  char name[1024];

  char* sptr = strrchr(aStr, '/');
  if (sptr) {
    sptr++;
    strcpy(name, sptr);
    char* eptr = strrchr(name, '.');
    if (eptr) {
      *eptr = 0;
    }
    strcat(name, "_stats.html");
  }
  mFD = fopen(name, "w");
  if (mFD) {
    fprintf(mFD, "<html><head><title>Reflow Stats</title></head><body>\n");
    const char* title = aStr ? aStr : "No name";
    fprintf(mFD,
            "<center><b>%s</b><br><table border=1 "
            "style=\"background-color:#e0e0e0\">",
            title);
    DoGrandHTMLTotals();
    fprintf(mFD, "</center></table>\n");
    fprintf(mFD, "</body></html>\n");
    fclose(mFD);
    mFD = nullptr;
  }
#  endif  // not XP!
}

//------------------------------------------------------------------
void ReflowCountMgr::ClearTotals() {
  for (const auto& data : mCounts.Values()) {
    data->ClearTotals();
  }
}

//------------------------------------------------------------------
void ReflowCountMgr::ClearGrandTotals() {
  mCounts.WithEntryHandle(kGrandTotalsStr, [&](auto&& entry) {
    if (!entry) {
      entry.Insert(MakeUnique<ReflowCounter>(this));
    } else {
      entry.Data()->ClearTotals();
      entry.Data()->SetTotalsCache();
    }
  });
}

//------------------------------------------------------------------
void ReflowCountMgr::DisplayDiffsInTotals() {
  if (mCycledOnce) {
    printf("Differences\n");
    for (int32_t i = 0; i < 78; i++) {
      printf("-");
    }
    printf("\n");
    ClearGrandTotals();
  }

  for (const auto& entry : mCounts) {
    if (mCycledOnce) {
      entry.GetData()->CalcDiffInTotals();
      entry.GetData()->DisplayDiffTotals(entry.GetKey());
    }
    entry.GetData()->SetTotalsCache();
  }

  mCycledOnce = true;
}

#endif  // MOZ_REFLOW_PERF

nsIFrame* PresShell::GetAbsoluteContainingBlock(nsIFrame* aFrame) {
  return FrameConstructor()->GetAbsoluteContainingBlock(
      aFrame, nsCSSFrameConstructor::ABS_POS);
}

void PresShell::ActivenessMaybeChanged() {
  if (!mDocument) {
    return;
  }
  SetIsActive(ComputeActiveness());
}

// A PresShell being active means that it is visible (or close to be visible, if
// the front-end is warming it). That means that when it is active we always
// tick its refresh driver at full speed if needed.
//
// Image documents behave specially in the sense that they are always "active"
// and never "in the active tab". However these documents tick manually so
// there's not much to worry about there.
bool PresShell::ComputeActiveness() const {
  MOZ_LOG(gLog, LogLevel::Debug,
          ("PresShell::ComputeActiveness(%s, %d)\n",
           mDocument->GetDocumentURI()
               ? mDocument->GetDocumentURI()->GetSpecOrDefault().get()
               : "(no uri)",
           mIsActive));

  Document* doc = mDocument;

  if (doc->IsBeingUsedAsImage()) {
    // Documents used as an image can remain active. They do not tick their
    // refresh driver if not painted, and they can't run script or such so they
    // can't really observe much else.
    //
    // Image docs can be displayed in multiple docs at the same time so the "in
    // active tab" bool doesn't make much sense for them.
    return true;
  }

  if (Document* displayDoc = doc->GetDisplayDocument()) {
    // Ok, we're an external resource document -- we need to use our display
    // document's docshell to determine "IsActive" status, since we lack
    // a browsing context of our own.
    MOZ_ASSERT(!doc->GetBrowsingContext(),
               "external resource doc shouldn't have its own BC");
    doc = displayDoc;
  }

  BrowsingContext* bc = doc->GetBrowsingContext();
  const bool inActiveTab = bc && bc->IsActive();

  MOZ_LOG(gLog, LogLevel::Debug,
          (" > BrowsingContext %p  active: %d", bc, inActiveTab));

  if (StaticPrefs::layout_testing_top_level_always_active() && bc &&
      bc->IsTop()) {
    MOZ_LOG(gLog, LogLevel::Debug, (" > Activeness overridden by pref"));
    return true;
  }

  Document* root = nsContentUtils::GetInProcessSubtreeRootDocument(doc);
  if (auto* browserChild = BrowserChild::GetFrom(root->GetDocShell())) {
    // We might want to activate a tab even though the browsing-context is not
    // active if the BrowserChild is considered visible. This serves two
    // purposes:
    //
    //  * For top-level tabs, we use this for tab warming. The browsing-context
    //    might still be inactive, but we want to activate the pres shell and
    //    the refresh driver.
    //
    //  * For oop iframes, we do want to throttle them if they're not visible.
    //
    // TODO(emilio): Consider unifying the in-process vs. fission iframe
    // throttling code (in-process throttling for non-visible iframes lives
    // right now in Document::ShouldThrottleFrameRequests(), but that only
    // throttles rAF).
    if (!browserChild->IsVisible()) {
      MOZ_LOG(gLog, LogLevel::Debug,
              (" > BrowserChild %p is not visible", browserChild));
      return false;
    }

    // If the browser is visible but just due to be preserving layers
    // artificially, we do want to fall back to the browsing context activeness
    // instead. Otherwise we do want to be active for the use cases above.
    if (!browserChild->IsPreservingLayers()) {
      MOZ_LOG(gLog, LogLevel::Debug,
              (" > BrowserChild %p is visible and not preserving layers",
               browserChild));
      return true;
    }
    MOZ_LOG(
        gLog, LogLevel::Debug,
        (" > BrowserChild %p is visible and preserving layers", browserChild));
  }
  return inActiveTab;
}

void PresShell::SetIsActive(bool aIsActive) {
  MOZ_ASSERT(mDocument, "should only be called with a document");

  const bool activityChanged = mIsActive != aIsActive;

  mIsActive = aIsActive;

  nsPresContext* presContext = GetPresContext();
  if (presContext &&
      presContext->RefreshDriver()->GetPresContext() == presContext) {
    presContext->RefreshDriver()->SetActivity(aIsActive);
  }

  if (activityChanged) {
    // Propagate state-change to my resource documents' PresShells and other
    // subdocuments.
    //
    // Note that it is fine to not propagate to fission iframes. Those will
    // become active / inactive as needed as a result of they getting painted /
    // not painted eventually.
    auto recurse = [aIsActive](Document& aSubDoc) {
      if (PresShell* presShell = aSubDoc.GetPresShell()) {
        presShell->SetIsActive(aIsActive);
      }
      return CallState::Continue;
    };
    mDocument->EnumerateExternalResources(recurse);
    mDocument->EnumerateSubDocuments(recurse);
  }

  UpdateImageLockingState();

  if (activityChanged) {
#if defined(MOZ_WIDGET_ANDROID)
    if (!aIsActive && presContext &&
        presContext->IsRootContentDocumentCrossProcess()) {
      // Reset the dynamic toolbar offset state.
      presContext->UpdateDynamicToolbarOffset(0);
    }
#endif
  }

  if (aIsActive) {
#ifdef ACCESSIBILITY
    if (nsAccessibilityService* accService = GetAccService()) {
      accService->PresShellActivated(this);
    }
#endif
    if (nsIFrame* rootFrame = GetRootFrame()) {
      rootFrame->SchedulePaint();
    }
  }
}

MobileViewportManager* PresShell::GetMobileViewportManager() const {
  return mMobileViewportManager;
}

Maybe<MobileViewportManager::ManagerType> UseMobileViewportManager(
    PresShell* aPresShell, Document* aDocument) {
  // If we're not using APZ, we won't be able to zoom, so there is no
  // point in having an MVM.
  if (nsPresContext* presContext = aPresShell->GetPresContext()) {
    if (nsIWidget* widget = presContext->GetNearestWidget()) {
      if (!widget->AsyncPanZoomEnabled()) {
        return Nothing();
      }
    }
  }
  if (nsLayoutUtils::ShouldHandleMetaViewport(aDocument)) {
    return Some(MobileViewportManager::ManagerType::VisualAndMetaViewport);
  }
  if (nsLayoutUtils::AllowZoomingForDocument(aDocument)) {
    return Some(MobileViewportManager::ManagerType::VisualViewportOnly);
  }
  return Nothing();
}

void PresShell::MaybeRecreateMobileViewportManager(bool aAfterInitialization) {
  // Determine if we require a MobileViewportManager, and what kind if so. We
  // need one any time we allow resolution zooming for a document, and any time
  // we want to obey <meta name="viewport"> tags for it.
  Maybe<MobileViewportManager::ManagerType> mvmType =
      UseMobileViewportManager(this, mDocument);

  if (mvmType.isNothing() && !mMobileViewportManager) {
    // We don't need one and don't have it. So we're done.
    return;
  }
  if (mvmType && mMobileViewportManager &&
      *mvmType == mMobileViewportManager->GetManagerType()) {
    // We need one and we have one of the correct type, so we're done.
    return;
  }

  if (!mPresContext->IsRootContentDocumentCrossProcess()) {
    MOZ_ASSERT(!mMobileViewportManager, "We never create MVMs for subframes");
    return;
  }

  if (mMobileViewportManager) {
    // We have one, but we need to either destroy it completely to replace it
    // with another one of the correct type. So either way, let's destroy the
    // one we have.
    mMobileViewportManager->Destroy();
    mMobileViewportManager = nullptr;
    mMVMContext = nullptr;

    ResetVisualViewportSize();
  }

  if (mvmType) {
    // Let's create the MVM of the type that we need. At this point we shouldn't
    // have one.
    MOZ_ASSERT(!mMobileViewportManager);

    mMVMContext = new GeckoMVMContext(mDocument, this);
    mMobileViewportManager = new MobileViewportManager(mMVMContext, *mvmType);
    if (MOZ_UNLIKELY(
            MOZ_LOG_TEST(MobileViewportManager::gLog, LogLevel::Debug))) {
      nsIURI* uri = mDocument->GetDocumentURI();
      MOZ_LOG(
          MobileViewportManager::gLog, LogLevel::Debug,
          ("Created MVM %p (type %d) for URI %s", mMobileViewportManager.get(),
           (int)*mvmType, uri ? uri->GetSpecOrDefault().get() : "(null)"));
    }
    if (BrowserChild* browserChild = BrowserChild::GetFrom(this)) {
      mMobileViewportManager->UpdateKeyboardHeight(
          browserChild->GetKeyboardHeight());
    }
  }

  if (aAfterInitialization) {
    // Setting the initial viewport will trigger a reflow.
    if (mMobileViewportManager) {
      mMobileViewportManager->SetInitialViewport();
    } else {
      // Force a reflow to our correct view manager size.
      ForceResizeReflowWithCurrentDimensions();
    }
    // After we clear out the MVM and the MVMContext, also reset the
    // resolution to 1.
    SetResolutionAndScaleTo(1.0f, ResolutionChangeOrigin::MainThreadRestore);
  }
}

bool PresShell::UsesMobileViewportSizing() const {
  return mMobileViewportManager &&
         nsLayoutUtils::ShouldHandleMetaViewport(mDocument);
}

/*
 * Determines the current image locking state. Called when one of the
 * dependent factors changes.
 */
void PresShell::UpdateImageLockingState() {
  // We're locked if we're both thawed and active.
  const bool locked = !mFrozen && mIsActive;
  auto* tracker = mDocument->ImageTracker();
  if (locked == tracker->GetLockingState()) {
    return;
  }

  tracker->SetLockingState(locked);
  if (locked) {
    // Request decodes for visible image frames; we want to start decoding as
    // quickly as possible when we get foregrounded to minimize flashing.
    for (const auto& key : mApproximatelyVisibleFrames) {
      if (nsImageFrame* imageFrame = do_QueryFrame(key)) {
        imageFrame->MaybeDecodeForPredictedSize();
      }
    }
  }
}

PresShell* PresShell::GetRootPresShell() const {
  if (mPresContext) {
    nsPresContext* rootPresContext = mPresContext->GetRootPresContext();
    if (rootPresContext) {
      return rootPresContext->PresShell();
    }
  }
  return nullptr;
}

void PresShell::AddSizeOfIncludingThis(nsWindowSizes& aSizes) const {
  MallocSizeOf mallocSizeOf = aSizes.mState.mMallocSizeOf;
  mFrameArena.AddSizeOfExcludingThis(aSizes, Arena::ArenaKind::PresShell);
  aSizes.mLayoutPresShellSize += mallocSizeOf(this);
  if (mCaret) {
    aSizes.mLayoutPresShellSize += mCaret->SizeOfIncludingThis(mallocSizeOf);
  }
  aSizes.mLayoutPresShellSize +=
      mApproximatelyVisibleFrames.ShallowSizeOfExcludingThis(mallocSizeOf) +
      mFramesToDirty.ShallowSizeOfExcludingThis(mallocSizeOf) +
      mPendingScrollAnchorSelection.ShallowSizeOfExcludingThis(mallocSizeOf) +
      mPendingScrollAnchorAdjustment.ShallowSizeOfExcludingThis(mallocSizeOf);

  aSizes.mLayoutTextRunsSize += SizeOfTextRuns(mallocSizeOf);

  aSizes.mLayoutPresContextSize +=
      mPresContext->SizeOfIncludingThis(mallocSizeOf);

  mFrameConstructor->AddSizeOfIncludingThis(aSizes);
}

size_t PresShell::SizeOfTextRuns(MallocSizeOf aMallocSizeOf) const {
  nsIFrame* rootFrame = mFrameConstructor->GetRootFrame();
  if (!rootFrame) {
    return 0;
  }

  // clear the TEXT_RUN_MEMORY_ACCOUNTED flags
  nsLayoutUtils::SizeOfTextRunsForFrames(rootFrame, nullptr,
                                         /* clear = */ true);

  // collect the total memory in use for textruns
  return nsLayoutUtils::SizeOfTextRunsForFrames(rootFrame, aMallocSizeOf,
                                                /* clear = */ false);
}

void PresShell::MarkFixedFramesForReflow(IntrinsicDirty aIntrinsicDirty) {
  nsIFrame* rootFrame = mFrameConstructor->GetRootFrame();
  if (rootFrame) {
    const nsFrameList& childList =
        rootFrame->GetChildList(FrameChildListID::Fixed);
    for (nsIFrame* childFrame : childList) {
      FrameNeedsReflow(childFrame, aIntrinsicDirty, NS_FRAME_IS_DIRTY);
    }
  }
}

void PresShell::MarkStickyFramesForReflow() {
  ScrollContainerFrame* sc = GetRootScrollContainerFrame();
  if (!sc) {
    return;
  }

  StickyScrollContainer* ssc =
      StickyScrollContainer::GetStickyScrollContainerForScrollFrame(sc);
  if (!ssc) {
    return;
  }

  ssc->MarkFramesForReflow();
}

static void AppendSubtree(nsIDocShell* aDocShell,
                          nsTArray<nsCOMPtr<nsIDocumentViewer>>& aArray) {
  if (nsCOMPtr<nsIDocumentViewer> viewer = aDocShell->GetDocViewer()) {
    aArray.AppendElement(viewer);
  }

  int32_t n = aDocShell->GetInProcessChildCount();
  for (int32_t i = 0; i < n; i++) {
    nsCOMPtr<nsIDocShellTreeItem> childItem;
    aDocShell->GetInProcessChildAt(i, getter_AddRefs(childItem));
    if (childItem) {
      nsCOMPtr<nsIDocShell> child(do_QueryInterface(childItem));
      AppendSubtree(child, aArray);
    }
  }
}

void PresShell::MaybeReflowForInflationScreenSizeChange() {
  nsPresContext* pc = GetPresContext();
  const bool fontInflationWasEnabled = FontSizeInflationEnabled();
  RecomputeFontSizeInflationEnabled();
  bool changed = false;
  if (FontSizeInflationEnabled() && FontSizeInflationMinTwips() != 0) {
    pc->ScreenSizeInchesForFontInflation(&changed);
  }

  changed = changed || fontInflationWasEnabled != FontSizeInflationEnabled();
  if (!changed) {
    return;
  }
  if (nsCOMPtr<nsIDocShell> docShell = pc->GetDocShell()) {
    nsTArray<nsCOMPtr<nsIDocumentViewer>> array;
    AppendSubtree(docShell, array);
    for (uint32_t i = 0, iEnd = array.Length(); i < iEnd; ++i) {
      nsCOMPtr<nsIDocumentViewer> viewer = array[i];
      if (RefPtr<PresShell> descendantPresShell = viewer->GetPresShell()) {
        nsIFrame* rootFrame = descendantPresShell->GetRootFrame();
        if (rootFrame) {
          descendantPresShell->FrameNeedsReflow(
              rootFrame, IntrinsicDirty::FrameAncestorsAndDescendants,
              NS_FRAME_IS_DIRTY);
        }
      }
    }
  }
}

void PresShell::CompleteChangeToVisualViewportSize() {
  // This can get called during reflow, if the caller wants to get the latest
  // visual viewport size after scrollbars have been added/removed. In such a
  // case, we don't need to mark things as dirty because the things that we
  // would mark dirty either just got updated (the root scrollframe's
  // scrollbars), or will be laid out later during this reflow cycle (fixed-pos
  // items). Callers that update the visual viewport during a reflow are
  // responsible for maintaining these invariants.
  if (!mIsReflowing) {
    if (ScrollContainerFrame* sf = GetRootScrollContainerFrame()) {
      sf->MarkScrollbarsDirtyForReflow();
    }
    MarkFixedFramesForReflow(IntrinsicDirty::None);
  }

  MaybeReflowForInflationScreenSizeChange();

  if (auto* window = nsGlobalWindowInner::Cast(mDocument->GetInnerWindow())) {
    window->VisualViewport()->PostResizeEvent();
  }
}

void PresShell::SetVisualViewportSize(nscoord aWidth, nscoord aHeight) {
  MOZ_ASSERT(aWidth >= 0.0 && aHeight >= 0.0);

  if (!mVisualViewportSizeSet || mVisualViewportSize.width != aWidth ||
      mVisualViewportSize.height != aHeight) {
    mVisualViewportSizeSet = true;
    mVisualViewportSize.width = aWidth;
    mVisualViewportSize.height = aHeight;

    CompleteChangeToVisualViewportSize();
  }
}

void PresShell::ResetVisualViewportSize() {
  if (mVisualViewportSizeSet) {
    mVisualViewportSizeSet = false;
    mVisualViewportSize.width = 0;
    mVisualViewportSize.height = 0;

    CompleteChangeToVisualViewportSize();
  }
}

bool PresShell::SetVisualViewportOffset(const nsPoint& aScrollOffset,
                                        const nsPoint& aPrevLayoutScrollPos) {
  nsPoint newOffset = aScrollOffset;
  ScrollContainerFrame* rootScrollContainerFrame =
      GetRootScrollContainerFrame();
  if (rootScrollContainerFrame) {
    // See the comment in ScrollContainerFrame::Reflow above the call to
    // SetVisualViewportOffset for why we need to do this.
    nsRect scrollRange =
        rootScrollContainerFrame->GetScrollRangeForUserInputEvents();
    if (!scrollRange.Contains(newOffset)) {
      newOffset.x = std::min(newOffset.x, scrollRange.XMost());
      newOffset.x = std::max(newOffset.x, scrollRange.x);
      newOffset.y = std::min(newOffset.y, scrollRange.YMost());
      newOffset.y = std::max(newOffset.y, scrollRange.y);
    }
  }

  // Careful here not to call GetVisualViewportOffset to get the previous visual
  // viewport offset because if mVisualViewportOffset is nothing then we'll get
  // the layout scroll position directly from the scroll frame and it has likely
  // already been updated.
  nsPoint prevOffset = aPrevLayoutScrollPos;
  if (mVisualViewportOffset.isSome()) {
    prevOffset = *mVisualViewportOffset;
  }
  if (prevOffset == newOffset) {
    return false;
  }

  mVisualViewportOffset = Some(newOffset);

  if (auto* window = nsGlobalWindowInner::Cast(mDocument->GetInnerWindow())) {
    window->VisualViewport()->PostScrollEvent(prevOffset, aPrevLayoutScrollPos);
  }

  if (IsVisualViewportSizeSet() && rootScrollContainerFrame) {
    rootScrollContainerFrame->Anchor()->UserScrolled();
  }

  if (gfxPlatform::UseDesktopZoomingScrollbars()) {
    if (rootScrollContainerFrame) {
      rootScrollContainerFrame->UpdateScrollbarPosition();
    }
  }

  return true;
}

void PresShell::ResetVisualViewportOffset() { mVisualViewportOffset.reset(); }

void PresShell::RefreshViewportSize() {
  if (mMobileViewportManager) {
    mMobileViewportManager->RefreshViewportSize(false);
  }
}

void PresShell::ScrollToVisual(const nsPoint& aVisualViewportOffset,
                               FrameMetrics::ScrollOffsetUpdateType aUpdateType,
                               ScrollMode aMode) {
  MOZ_ASSERT(aMode == ScrollMode::Instant || aMode == ScrollMode::SmoothMsd);

  if (aMode == ScrollMode::SmoothMsd) {
    if (ScrollContainerFrame* sf = GetRootScrollContainerFrame()) {
      if (sf->SmoothScrollVisual(aVisualViewportOffset, aUpdateType)) {
        return;
      }
    }
  }

  // If the caller asked for instant scroll, or if we failed
  // to do a smooth scroll, do an instant scroll.
  SetPendingVisualScrollUpdate(aVisualViewportOffset, aUpdateType);
}

void PresShell::SetPendingVisualScrollUpdate(
    const nsPoint& aVisualViewportOffset,
    FrameMetrics::ScrollOffsetUpdateType aUpdateType) {
  mPendingVisualScrollUpdate =
      Some(VisualScrollUpdate{aVisualViewportOffset, aUpdateType});

  // The pending update is picked up during the next paint.
  // Schedule a paint to make sure one will happen.
  if (nsIFrame* rootFrame = GetRootFrame()) {
    rootFrame->SchedulePaint();
  }
}

void PresShell::ClearPendingVisualScrollUpdate() {
  if (mPendingVisualScrollUpdate && mPendingVisualScrollUpdate->mAcknowledged) {
    mPendingVisualScrollUpdate = mozilla::Nothing();
  }
}

void PresShell::AcknowledgePendingVisualScrollUpdate() {
  MOZ_ASSERT(mPendingVisualScrollUpdate);
  mPendingVisualScrollUpdate->mAcknowledged = true;
}

nsPoint PresShell::GetVisualViewportOffsetRelativeToLayoutViewport() const {
  return GetVisualViewportOffset() - GetLayoutViewportOffset();
}

nsPoint PresShell::GetLayoutViewportOffset() const {
  nsPoint result;
  if (ScrollContainerFrame* sf = GetRootScrollContainerFrame()) {
    result = sf->GetScrollPosition();
  }
  return result;
}

nsSize PresShell::GetLayoutViewportSize() const {
  nsSize result;
  if (ScrollContainerFrame* sf = GetRootScrollContainerFrame()) {
    result = sf->GetScrollPortRect().Size();
  }
  return result;
}

nsSize PresShell::GetVisualViewportSizeUpdatedByDynamicToolbar() const {
  NS_ASSERTION(mVisualViewportSizeSet,
               "asking for visual viewport size when its not set?");
  if (!mMobileViewportManager) {
    return mVisualViewportSize;
  }

  MOZ_ASSERT(GetDynamicToolbarState() == DynamicToolbarState::InTransition ||
             GetDynamicToolbarState() == DynamicToolbarState::Collapsed);

  nsSize sizeUpdatedByDynamicToolbar =
      mMobileViewportManager->GetVisualViewportSizeUpdatedByDynamicToolbar();
  return sizeUpdatedByDynamicToolbar == nsSize() ? mVisualViewportSize
                                                 : sizeUpdatedByDynamicToolbar;
}

void PresShell::RecomputeFontSizeInflationEnabled() {
  mFontSizeInflationEnabled = DetermineFontSizeInflationState();
}

bool PresShell::DetermineFontSizeInflationState() {
  MOZ_ASSERT(mPresContext, "our pres context should not be null");
  if (mPresContext->IsChrome()) {
    return false;
  }

  if (FontSizeInflationEmPerLine() == 0 && FontSizeInflationMinTwips() == 0) {
    return false;
  }

  // Force-enabling font inflation always trumps the heuristics here.
  if (!FontSizeInflationForceEnabled()) {
    if (BrowserChild* tab = BrowserChild::GetFrom(this)) {
      // We're in a child process.  Cancel inflation if we're not
      // async-pan zoomed.
      if (!tab->AsyncPanZoomEnabled()) {
        return false;
      }
    } else if (XRE_IsParentProcess()) {
      // We're in the master process.  Cancel inflation if it's been
      // explicitly disabled.
      if (FontSizeInflationDisabledInMasterProcess()) {
        return false;
      }
    }
  }

  Maybe<LayoutDeviceIntSize> displaySize;
  // The MVM already caches the top-level content viewer size and is therefore
  // the fastest way of getting that data.
  if (mPresContext->IsRootContentDocumentCrossProcess()) {
    if (mMobileViewportManager) {
      displaySize = Some(mMobileViewportManager->DisplaySize());
    }
  } else if (PresShell* rootPresShell = GetRootPresShell()) {
    // With any luck, we can get at the root content document without any cross-
    // process shenanigans.
    if (auto mvm = rootPresShell->GetMobileViewportManager()) {
      displaySize = Some(mvm->DisplaySize());
    }
  }

  if (!displaySize) {
    // Unfortunately, it looks like the root content document lives in a
    // different process. For consistency's sake it would be best to always use
    // the content viewer size of the root content document, but it's not worth
    // the effort, because this only makes a difference in the case of pages
    // with an explicitly sized viewport (neither "width=device-width" nor a
    // completely missing viewport tag) being loaded within a frame, which is
    // hopefully a relatively exotic case.
    // More to the point, these viewport size and zoom-based calculations don't
    // really make sense for frames anyway, so instead of creating a way to
    // access the content viewer size of the top level document cross-process,
    // we probably rather want frames to simply inherit the font inflation state
    // of their top-level parent and should therefore invest any time spent on
    // getting things to work cross-process into that (bug 1724311).

    // Until we get around to that though, we just use the content viewer size
    // of however high we can get within the same process.

    // (This also serves as a fallback code path if the MVM isn't available,
    // e.g. when debugging in non-e10s mode on Desktop.)
    nsPresContext* topContext =
        mPresContext->GetInProcessRootContentDocumentPresContext();
    LayoutDeviceIntSize result;
    if (!nsLayoutUtils::GetDocumentViewerSize(topContext, result)) {
      return false;
    }
    displaySize = Some(result);
  }

  ScreenIntSize screenSize = ViewAs<ScreenPixel>(
      displaySize.value(),
      PixelCastJustification::LayoutDeviceIsScreenForBounds);
  nsViewportInfo vInf = GetDocument()->GetViewportInfo(screenSize);

  CSSToScreenScale defaultScale =
      mPresContext->CSSToDevPixelScale() * LayoutDeviceToScreenScale(1.0);

  if (vInf.GetDefaultZoom() >= defaultScale || vInf.IsAutoSizeEnabled()) {
    return false;
  }

  return true;
}

static nsIWidget* GetPresContextContainerWidget(nsPresContext* aPresContext) {
  nsCOMPtr<nsISupports> container = aPresContext->Document()->GetContainer();
  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(container);
  if (!baseWindow) {
    return nullptr;
  }

  nsCOMPtr<nsIWidget> mainWidget;
  baseWindow->GetMainWidget(getter_AddRefs(mainWidget));
  return mainWidget;
}

static bool IsTopLevelWidget(nsIWidget* aWidget) {
  using WindowType = mozilla::widget::WindowType;

  auto windowType = aWidget->GetWindowType();
  return windowType == WindowType::TopLevel ||
         windowType == WindowType::Dialog || windowType == WindowType::Popup;
}

PresShell::WindowSizeConstraints PresShell::GetWindowSizeConstraints() {
  nsSize minSize(0, 0);
  nsSize maxSize(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE);
  nsIFrame* rootFrame = FrameConstructor()->GetRootElementStyleFrame();
  if (!rootFrame || !mPresContext) {
    return {minSize, maxSize};
  }
  const auto* pos = rootFrame->StylePosition();
  const auto positionProperty = rootFrame->StyleDisplay()->mPosition;
  if (const auto styleMinWidth = pos->GetMinWidth(positionProperty);
      styleMinWidth->ConvertsToLength()) {
    minSize.width = styleMinWidth->ToLength();
  }
  if (const auto styleMinHeight = pos->GetMinHeight(positionProperty);
      styleMinHeight->ConvertsToLength()) {
    minSize.height = styleMinHeight->ToLength();
  }
  if (const auto maxWidth = pos->GetMaxWidth(positionProperty);
      maxWidth->ConvertsToLength()) {
    maxSize.width = maxWidth->ToLength();
  }
  if (const auto maxHeight = pos->GetMaxHeight(positionProperty);
      maxHeight->ConvertsToLength()) {
    maxSize.height = maxHeight->ToLength();
  }
  return {minSize, maxSize};
}

void PresShell::SyncWindowProperties(bool aSync) {
  if (XRE_IsContentProcess()) {
    return;
  }

  nsView* view = mViewManager->GetRootView();
  if (!view || !view->HasWidget()) {
    return;
  }

  RefPtr pc = mPresContext;
  if (!pc) {
    return;
  }

  nsCOMPtr<nsIWidget> windowWidget = GetPresContextContainerWidget(pc);
  if (!windowWidget || !IsTopLevelWidget(windowWidget)) {
    return;
  }

  if (!aSync) {
    view->SetNeedsWindowPropertiesSync();
    return;
  }

  nsIFrame* rootFrame = FrameConstructor()->GetRootElementStyleFrame();
  if (!rootFrame) {
    return;
  }

  // Apply color scheme to the top level window widget.
  windowWidget->SetColorScheme(
      Some(LookAndFeel::ColorSchemeForFrame(rootFrame)));

  AutoWeakFrame weak(rootFrame);
  auto* canvas = GetCanvasFrame();
  windowWidget->SetTransparencyMode(nsLayoutUtils::GetFrameTransparency(
      canvas ? canvas : rootFrame, rootFrame));
  if (!weak.IsAlive()) {
    return;
  }

  const auto& constraints = GetWindowSizeConstraints();
  nsContainerFrame::SetSizeConstraints(pc, windowWidget, constraints.mMinSize,
                                       constraints.mMaxSize);
}

nsresult PresShell::HasRuleProcessorUsedByMultipleStyleSets(uint32_t aSheetType,
                                                            bool* aRetVal) {
  *aRetVal = false;
  return NS_OK;
}

void PresShell::NotifyStyleSheetServiceSheetAdded(StyleSheet* aSheet,
                                                  uint32_t aSheetType) {
  switch (aSheetType) {
    case nsIStyleSheetService::AGENT_SHEET:
      AddAgentSheet(aSheet);
      break;
    case nsIStyleSheetService::USER_SHEET:
      AddUserSheet(aSheet);
      break;
    case nsIStyleSheetService::AUTHOR_SHEET:
      AddAuthorSheet(aSheet);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected aSheetType value");
      break;
  }
}

void PresShell::NotifyStyleSheetServiceSheetRemoved(StyleSheet* aSheet,
                                                    uint32_t aSheetType) {
  StyleSet()->RemoveStyleSheet(*aSheet);
  mDocument->ApplicableStylesChanged();
}

Result<nsIContent*, nsresult> PresShell::EventHandler::GetOverrideClickTarget(
    WidgetGUIEvent* aGUIEvent, nsIFrame* aFrameForPresShell) {
  if (aGUIEvent->mMessage != eMouseUp) {
    return nullptr;
  }

  // If aFrameForPresShell has already been reframed before this is called,
  // we cannot keep handling aGUIEvent.
  if (MOZ_UNLIKELY(!aFrameForPresShell)) {
    return Err(NS_ERROR_FAILURE);
  }

  MOZ_ASSERT(aGUIEvent->mClass == eMouseEventClass);
  WidgetMouseEvent* mouseEvent = aGUIEvent->AsMouseEvent();

  uint32_t flags = 0;
  RelativeTo relativeTo{aFrameForPresShell};
  nsPoint eventPoint =
      nsLayoutUtils::GetEventCoordinatesRelativeTo(aGUIEvent, relativeTo);
  if (mouseEvent->mIgnoreRootScrollFrame) {
    flags |= INPUT_IGNORE_ROOT_SCROLL_FRAME;
  }

  nsIFrame* target =
      FindFrameTargetedByInputEvent(aGUIEvent, relativeTo, eventPoint, flags);
  if (!target) {
    return nullptr;
  }

  nsIContent* overrideClickTarget = target->GetContent();
  return overrideClickTarget
             ? overrideClickTarget->GetInclusiveFlattenedTreeAncestorElement()
             : nullptr;
}

/******************************************************************************
 * PresShell::EventHandler::EventTargetData
 ******************************************************************************/

void PresShell::EventHandler::EventTargetData::SetFrameAndComputePresShell(
    nsIFrame* aFrameToHandleEvent) {
  if (aFrameToHandleEvent) {
    mFrame = aFrameToHandleEvent;
    mPresShell = aFrameToHandleEvent->PresShell();
  } else {
    mFrame = nullptr;
    mPresShell = nullptr;
  }
}

void PresShell::EventHandler::EventTargetData::
    SetFrameAndComputePresShellAndContent(nsIFrame* aFrameToHandleEvent,
                                          WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aFrameToHandleEvent);
  MOZ_ASSERT(aGUIEvent);

  SetFrameAndComputePresShell(aFrameToHandleEvent);
  SetContentForEventFromFrame(aGUIEvent);
}

void PresShell::EventHandler::EventTargetData::SetContentForEventFromFrame(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(mFrame);
  mContent = mFrame->GetContentForEvent(aGUIEvent);
  AssertIfEventTargetContentAndFrameContentMismatch(aGUIEvent);
}

nsIContent* PresShell::EventHandler::EventTargetData::GetFrameContent() const {
  return mFrame ? mFrame->GetContent() : nullptr;
}

void PresShell::EventHandler::EventTargetData::
    AssertIfEventTargetContentAndFrameContentMismatch(
        const WidgetGUIEvent* aGUIEvent) const {
#ifdef DEBUG
  if (!mContent || !mFrame || !mFrame->GetContent()) {
    return;
  }

  // If we know the event, we can compute the target correctly.
  if (aGUIEvent) {
    MOZ_ASSERT(mContent == mFrame->GetContentForEvent(aGUIEvent));
    return;
  }

  // Otherwise, we can check only whether mContent is an inclusive ancestor
  // element or not.
  if (!mContent->IsElement()) {
    MOZ_ASSERT(mContent == mFrame->GetContent());
    return;
  }
  const Element* const closestInclusiveAncestorElement =
      mFrame->GetContent()->GetInclusiveFlattenedTreeAncestorElement();
  if (closestInclusiveAncestorElement == mContent) {
    return;
  }
  if (closestInclusiveAncestorElement->IsInNativeAnonymousSubtree() &&
      (mContent == closestInclusiveAncestorElement
                       ->FindFirstNonChromeOnlyAccessContent())) {
    return;
  }
  NS_WARNING(nsPrintfCString("mContent=%s", ToString(*mContent).c_str()).get());
  NS_WARNING(nsPrintfCString("mFrame->GetContent()=%s",
                             ToString(*mFrame->GetContent()).c_str())
                 .get());
  MOZ_ASSERT(mContent == mFrame->GetContent());
#endif  // #ifdef DEBUG
}

bool PresShell::EventHandler::EventTargetData::MaybeRetargetToActiveDocument(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(mFrame);
  MOZ_ASSERT(mPresShell);
  MOZ_ASSERT(!mContent, "Doesn't support to retarget the content");

  EventStateManager* activeESM =
      EventStateManager::GetActiveEventStateManager();
  if (!activeESM) {
    return false;
  }

  if (aGUIEvent->mClass != ePointerEventClass &&
      !aGUIEvent->HasMouseEventMessage()) {
    return false;
  }

  if (activeESM == GetEventStateManager()) {
    return false;
  }

  if (aGUIEvent->ShouldIgnoreCapturingContent()) {
    return false;
  }

  nsPresContext* activePresContext = activeESM->GetPresContext();
  if (!activePresContext) {
    return false;
  }

  PresShell* activePresShell = activePresContext->GetPresShell();
  if (!activePresShell) {
    return false;
  }

  // Note, currently for backwards compatibility we don't forward mouse events
  // to the active document when mouse is over some subdocument.
  if (!nsContentUtils::ContentIsCrossDocDescendantOf(
          activePresShell->GetDocument(), GetDocument())) {
    return false;
  }

  SetFrameAndComputePresShell(activePresShell->GetRootFrame());
  return true;
}

bool PresShell::EventHandler::EventTargetData::ComputeElementFromFrame(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);
  MOZ_ASSERT(aGUIEvent->IsUsingCoordinates());
  MOZ_ASSERT(mPresShell);
  MOZ_ASSERT(mFrame);

  SetContentForEventFromFrame(aGUIEvent);

  // If there is no content for this frame, target it anyway.  Some frames can
  // be targeted but do not have content, particularly windows with scrolling
  // off.
  if (!mContent) {
    return true;
  }

  // Bug 103055, bug 185889: mouse events apply to *elements*, not all nodes.
  // Thus we get the nearest element parent here.
  // XXX we leave the frame the same even if we find an element parent, so that
  // the text frame will receive the event (selection and friends are the ones
  // who care about that anyway)
  //
  // We use weak pointers because during this tight loop, the node
  // will *not* go away.  And this happens on every mousemove.
  mContent = mContent->GetInclusiveFlattenedTreeAncestorElement();

  // If we found an element, target it.  Otherwise, target *nothing*.
  return !!mContent;
}

void PresShell::EventHandler::EventTargetData::UpdateWheelEventTarget(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);

  if (aGUIEvent->mMessage != eWheel) {
    return;
  }

  // If dom.event.wheel-event-groups.enabled is not set or the stored
  // event target is removed, we will not get a event target frame from the
  // wheel transaction here.
  nsIFrame* groupFrame = WheelTransaction::GetEventTargetFrame();
  if (!groupFrame) {
    return;
  }

  // If dom.event.wheel-event-groups.enabled is set and whe have a stored
  // event target from the wheel transaction, override the event target.
  SetFrameAndComputePresShellAndContent(groupFrame, aGUIEvent);
}

void PresShell::EventHandler::EventTargetData::UpdateTouchEventTarget(
    WidgetGUIEvent* aGUIEvent) {
  MOZ_ASSERT(aGUIEvent);

  if (aGUIEvent->mClass != eTouchEventClass) {
    return;
  }

  if (aGUIEvent->mMessage == eTouchStart) {
    WidgetTouchEvent* touchEvent = aGUIEvent->AsTouchEvent();
    nsIFrame* newFrame =
        TouchManager::SuppressInvalidPointsAndGetTargetedFrame(touchEvent);
    if (!newFrame) {
      return;
    }
    SetFrameAndComputePresShellAndContent(newFrame, aGUIEvent);
    return;
  }

  PresShell* newPresShell = PresShell::GetShellForTouchEvent(aGUIEvent);
  if (!newPresShell) {
    return;  // XXX Why don't we stop handling the event in this case?
  }

  // Touch events (except touchstart) are dispatching to the captured
  // element. Get correct shell from it.
  mPresShell = newPresShell;
}

void PresShell::EndPaint() {
  ClearPendingVisualScrollUpdate();

  if (mDocument) {
    mDocument->EnumerateSubDocuments([](Document& aSubDoc) {
      if (PresShell* presShell = aSubDoc.GetPresShell()) {
        presShell->EndPaint();
      }
      return CallState::Continue;
    });

    if (nsPresContext* presContext = GetPresContext()) {
      if (PerformanceMainThread* perf =
              presContext->GetPerformanceMainThread()) {
        perf->FinalizeLCPEntriesForText();
      }
    }
  }
}

bool PresShell::GetZoomableByAPZ() const {
  return mZoomConstraintsClient && mZoomConstraintsClient->GetAllowZoom();
}

bool PresShell::ReflowForHiddenContentIfNeeded() {
  if (mHiddenContentInForcedLayout.IsEmpty()) {
    return false;
  }
  mDocument->FlushPendingNotifications(FlushType::Layout);
  mHiddenContentInForcedLayout.Clear();
  return true;
}

void PresShell::UpdateHiddenContentInForcedLayout(nsIFrame* aFrame) {
  if (!aFrame || !aFrame->IsSubtreeDirty()) {
    return;
  }

  nsIFrame* topmostFrameWithContentHidden = nullptr;
  for (nsIFrame* cur = aFrame->GetInFlowParent(); cur;
       cur = cur->GetInFlowParent()) {
    if (cur->HidesContent()) {
      topmostFrameWithContentHidden = cur;
      mHiddenContentInForcedLayout.Insert(cur->GetContent());
    }
  }

  if (mHiddenContentInForcedLayout.IsEmpty()) {
    return;
  }

  // Queue and immediately flush a reflow for this node.
  MOZ_ASSERT(topmostFrameWithContentHidden);
  FrameNeedsReflow(topmostFrameWithContentHidden, IntrinsicDirty::None,
                   NS_FRAME_IS_DIRTY);
}

void PresShell::EnsureReflowIfFrameHasHiddenContent(nsIFrame* aFrame) {
  MOZ_ASSERT(mHiddenContentInForcedLayout.IsEmpty());

  UpdateHiddenContentInForcedLayout(aFrame);
  ReflowForHiddenContentIfNeeded();
}

bool PresShell::IsForcingLayoutForHiddenContent(const nsIFrame* aFrame) const {
  return mHiddenContentInForcedLayout.Contains(aFrame->GetContent());
}

void PresShell::UpdateRelevancyOfContentVisibilityAutoFrames() {
  if (mContentVisibilityRelevancyToUpdate.isEmpty()) {
    return;
  }

  for (nsIFrame* frame : mContentVisibilityAutoFrames) {
    frame->UpdateIsRelevantContent(mContentVisibilityRelevancyToUpdate);
  }

  if (nsPresContext* presContext = GetPresContext()) {
    presContext->UpdateHiddenByContentVisibilityForAnimationsIfNeeded();
  }

  mContentVisibilityRelevancyToUpdate.clear();
}

void PresShell::ScheduleContentRelevancyUpdate(ContentRelevancyReason aReason) {
  if (MOZ_UNLIKELY(mIsDestroying)) {
    return;
  }
  mContentVisibilityRelevancyToUpdate += aReason;
  EnsureLayoutFlush();
}

PresShell::ProximityToViewportResult PresShell::DetermineProximityToViewport() {
  ProximityToViewportResult result;
  if (mContentVisibilityAutoFrames.IsEmpty()) {
    return result;
  }

  auto margin = LengthPercentage::FromPercentage(
      StaticPrefs::layout_css_content_visibility_relevant_content_margin() /
      100.0f);

  auto rootMargin = StyleRect<LengthPercentage>::WithAllSides(margin);

  auto input = DOMIntersectionObserver::ComputeInput(
      *mDocument, /* aRoot = */ nullptr, &rootMargin);

  for (nsIFrame* frame : mContentVisibilityAutoFrames) {
    auto* element = frame->GetContent()->AsElement();
    result.mAnyScrollIntoViewFlag |=
        element->TemporarilyVisibleForScrolledIntoViewDescendant();

    // 14.2.3.1
    Maybe<bool> oldVisibility = element->GetVisibleForContentVisibility();
    bool checkForInitialDetermination =
        oldVisibility.isNothing() &&
        (element->GetContentRelevancy().isNothing() ||
         element->GetContentRelevancy()->isEmpty());

    // 14.2.3.2
    bool intersects =
        DOMIntersectionObserver::Intersect(
            input, *element, DOMIntersectionObserver::BoxToUse::OverflowClip,
            DOMIntersectionObserver::IsForProximityToViewport::Yes)
            .Intersects();
    element->SetVisibleForContentVisibility(intersects);

    // 14.2.3.3
    if (checkForInitialDetermination && intersects) {
      // Initial determination happens sync, otherwise on the next rendering
      // opportunity.
      frame->UpdateIsRelevantContent(ContentRelevancyReason::Visible);
      result.mHadInitialDetermination = true;
    } else if (oldVisibility.isNothing() || *oldVisibility != intersects) {
      ScheduleContentRelevancyUpdate(ContentRelevancyReason::Visible);
    }
  }
  if (nsPresContext* presContext = GetPresContext()) {
    presContext->UpdateHiddenByContentVisibilityForAnimationsIfNeeded();
  }

  return result;
}

void PresShell::ClearTemporarilyVisibleForScrolledIntoViewDescendantFlags()
    const {
  for (nsIFrame* frame : mContentVisibilityAutoFrames) {
    frame->GetContent()
        ->AsElement()
        ->SetTemporarilyVisibleForScrolledIntoViewDescendant(false);
  }
}

void PresShell::UpdateContentRelevancyImmediately(
    ContentRelevancyReason aReason) {
  if (MOZ_UNLIKELY(mIsDestroying)) {
    return;
  }

  mContentVisibilityRelevancyToUpdate += aReason;

  EnsureLayoutFlush();
  UpdateRelevancyOfContentVisibilityAutoFrames();
}
