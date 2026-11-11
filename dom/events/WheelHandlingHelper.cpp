/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WheelHandlingHelper.h"

#include <utility>  // for std::swap

#include "DocumentInlines.h"  // for Document and HTMLBodyElement
#include "ScrollAnimationPhysics.h"
#include "Units.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/StaticPrefs_mousewheel.h"
#include "mozilla/StaticPrefs_test.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsITimer.h"
#include "nsPresContext.h"
#include "prtime.h"

static mozilla::LazyLogModule sWheelTransactionLog("dom.wheeltransaction");
#define WTXN_LOG(...) \
  MOZ_LOG(sWheelTransactionLog, LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {

/******************************************************************/
/* mozilla::DeltaValues                                           */
/******************************************************************/

DeltaValues::DeltaValues(WidgetWheelEvent* aEvent)
    : deltaX(aEvent->mDeltaX), deltaY(aEvent->mDeltaY) {}

/******************************************************************/
/* mozilla::WheelHandlingUtils                                    */
/******************************************************************/

/* static */
bool WheelHandlingUtils::CanScrollInRange(nscoord aMin, nscoord aValue,
                                          nscoord aMax, double aDirection) {
  return aDirection > 0.0 ? aValue < static_cast<double>(aMax)
                          : static_cast<double>(aMin) < aValue;
}

/* static */
bool WheelHandlingUtils::CanScrollOn(nsIFrame* aFrame, double aDirectionX,
                                     double aDirectionY) {
  ScrollContainerFrame* scrollContainerFrame = do_QueryFrame(aFrame);
  if (!scrollContainerFrame) {
    return false;
  }
  return CanScrollOn(scrollContainerFrame, aDirectionX, aDirectionY);
}

/* static */
bool WheelHandlingUtils::CanScrollOn(
    ScrollContainerFrame* aScrollContainerFrame, double aDirectionX,
    double aDirectionY) {
  MOZ_ASSERT(aScrollContainerFrame);
  NS_ASSERTION(aDirectionX || aDirectionY,
               "One of the delta values must be non-zero at least");

  nsPoint scrollPt = aScrollContainerFrame->GetVisualViewportOffset();
  nsRect scrollRange =
      aScrollContainerFrame->GetScrollRangeForUserInputEvents();
  layers::ScrollDirections directions =
      aScrollContainerFrame
          ->GetAvailableScrollingDirectionsForUserInputEvents();

  return ((aDirectionX != 0.0) &&
          (directions.contains(layers::ScrollDirection::eHorizontal)) &&
          CanScrollInRange(scrollRange.x, scrollPt.x, scrollRange.XMost(),
                           aDirectionX)) ||
         ((aDirectionY != 0.0) &&
          (directions.contains(layers::ScrollDirection::eVertical)) &&
          CanScrollInRange(scrollRange.y, scrollPt.y, scrollRange.YMost(),
                           aDirectionY));
}

/*static*/ Maybe<layers::ScrollDirection>
WheelHandlingUtils::GetDisregardedWheelScrollDirection(const nsIFrame* aFrame) {
  nsIContent* content = aFrame->GetContent();
  if (!content) {
    return Nothing();
  }
  TextControlElement* textControlElement = TextControlElement::FromNodeOrNull(
      content->IsInNativeAnonymousSubtree()
          ? content->GetClosestNativeAnonymousSubtreeRootParentOrHost()
          : content);
  if (!textControlElement || !textControlElement->IsSingleLineTextControl()) {
    return Nothing();
  }
  // Disregard scroll in the block-flow direction by mouse wheel on a
  // single-line text control. For instance, in tranditional Chinese writing
  // system, a single-line text control cannot be scrolled horizontally with
  // mouse wheel even if they overflow at the right and left edges; Whereas in
  // latin-based writing system, a single-line text control cannot be scrolled
  // vertically with mouse wheel even if they overflow at the top and bottom
  // edges
  return Some(aFrame->GetWritingMode().IsVertical()
                  ? layers::ScrollDirection::eHorizontal
                  : layers::ScrollDirection::eVertical);
}

/******************************************************************/
/* mozilla::WheelTransaction                                      */
/******************************************************************/

constinit AutoWeakFrame WheelTransaction::sScrollTargetFrame;
constinit AutoWeakFrame WheelTransaction::sEventTargetFrame;

bool WheelTransaction::sHandledByApz(false);
uint32_t WheelTransaction::sTime = 0;
uint32_t WheelTransaction::sMouseMoved = 0;
nsITimer* WheelTransaction::sTimer = nullptr;
int32_t WheelTransaction::sScrollSeriesCounter = 0;
bool WheelTransaction::sOwnScrollbars = false;

/* static */
bool WheelTransaction::OutOfTime(uint32_t aBaseTime, uint32_t aThreshold) {
  uint32_t now = PR_IntervalToMilliseconds(PR_IntervalNow());
  return (now - aBaseTime > aThreshold);
}

/* static */
void WheelTransaction::OwnScrollbars(bool aOwn) { sOwnScrollbars = aOwn; }

/* static */
void WheelTransaction::BeginTransaction(nsIFrame* aScrollTargetFrame,
                                        nsIFrame* aEventTargetFrame,
                                        const WidgetWheelEvent* aEvent) {
  NS_ASSERTION(!sScrollTargetFrame && !sEventTargetFrame,
               "previous transaction is not finished!");
  MOZ_ASSERT(aEvent->mMessage == eWheel,
             "Transaction must be started with a wheel event");

  ScrollbarsForWheel::OwnWheelTransaction(false);
  sScrollTargetFrame = aScrollTargetFrame;

  WTXN_LOG("WheelTransaction start for frame=0x%p handled-by-apz=%s",
           aEventTargetFrame, aEvent->mFlags.mHandledByAPZ ? "true" : "false");
  // Set a static event target for the wheel transaction. This will be used
  // to override the event target frame when computing the event target from
  // input coordinates. When there is no stored event target for the current
  // wheel transaction, the event target will not be overridden by the current
  // wheel transaction, but will be computed from the input coordinates.
  sEventTargetFrame = aEventTargetFrame;
  // If the wheel events will be handled by APZ, set a flag here. We can use
  // this later to determine if we need to scroll snap at the end of the
  // wheel operation.
  sHandledByApz = aEvent->mFlags.mHandledByAPZ;

  sScrollSeriesCounter = 0;
  if (!UpdateTransaction(aEvent)) {
    NS_ERROR("BeginTransaction is called even cannot scroll the frame");
    EndTransaction();
  }
}

/* static */
bool WheelTransaction::UpdateTransaction(const WidgetWheelEvent* aEvent) {
  nsIFrame* scrollToFrame = GetScrollTargetFrame();
  ScrollContainerFrame* scrollContainerFrame =
      scrollToFrame->GetScrollTargetFrame();
  if (scrollContainerFrame) {
    scrollToFrame = scrollContainerFrame;
  }

  if (!WheelHandlingUtils::CanScrollOn(scrollToFrame, aEvent->mDeltaX,
                                       aEvent->mDeltaY)) {
    OnFailToScrollTarget();
    // We should not modify the transaction state when the view will not be
    // scrolled actually.
    return false;
  }

  SetTimeout();

  if (sScrollSeriesCounter != 0 &&
      OutOfTime(sTime, StaticPrefs::mousewheel_scroll_series_timeout())) {
    sScrollSeriesCounter = 0;
  }
  sScrollSeriesCounter++;

  // We should use current time instead of WidgetEvent.time.
  // 1. Some events doesn't have the correct creation time.
  // 2. If the computer runs slowly by other processes eating the CPU resource,
  //    the event creation time doesn't keep real time.
  sTime = PR_IntervalToMilliseconds(PR_IntervalNow());
  sMouseMoved = 0;
  return true;
}

/* static */
void WheelTransaction::MayEndTransaction() {
  if (!sOwnScrollbars && ScrollbarsForWheel::IsActive()) {
    ScrollbarsForWheel::OwnWheelTransaction(true);
  } else {
    EndTransaction();
  }
}

/* static */
void WheelTransaction::EndTransaction() {
  if (sTimer) {
    sTimer->Cancel();
  }
  sScrollTargetFrame = nullptr;
  sEventTargetFrame = nullptr;
  sScrollSeriesCounter = 0;
  sHandledByApz = false;
  if (sOwnScrollbars) {
    sOwnScrollbars = false;
    ScrollbarsForWheel::OwnWheelTransaction(false);
    ScrollbarsForWheel::Inactivate();
  }
}

/* static */
bool WheelTransaction::WillHandleDefaultAction(
    WidgetWheelEvent* aWheelEvent, AutoWeakFrame& aScrollTargetWeakFrame,
    AutoWeakFrame& aEventTargetWeakFrame) {
  nsIFrame* lastTargetFrame = GetScrollTargetFrame();
  if (!lastTargetFrame) {
    BeginTransaction(aScrollTargetWeakFrame.GetFrame(),
                     aEventTargetWeakFrame.GetFrame(), aWheelEvent);
  } else if (lastTargetFrame != aScrollTargetWeakFrame.GetFrame()) {
    WTXN_LOG("Wheel transaction ending due to new target frame");
    EndTransaction();
    BeginTransaction(aScrollTargetWeakFrame.GetFrame(),
                     aEventTargetWeakFrame.GetFrame(), aWheelEvent);
  } else {
    UpdateTransaction(aWheelEvent);
  }

  // When the wheel event will not be handled with any frames,
  // UpdateTransaction() fires MozMouseScrollFailed event which is for
  // automated testing.  In the event handler, the target frame might be
  // destroyed.  Then, the caller shouldn't try to handle the default action.
  if (!aScrollTargetWeakFrame.IsAlive()) {
    WTXN_LOG("Wheel transaction ending due to target frame removal");
    EndTransaction();
    return false;
  }

  return true;
}

/* static */
void WheelTransaction::OnEvent(WidgetEvent* aEvent) {
  if (!sScrollTargetFrame) {
    return;
  }

  if (OutOfTime(sTime, StaticPrefs::mousewheel_transaction_timeout())) {
    // Even if the scroll event which is handled after timeout, but onTimeout
    // was not fired by timer, then the scroll event will scroll old frame,
    // therefore, we should call OnTimeout here and ensure to finish the old
    // transaction.
    OnTimeout(nullptr, nullptr);
    return;
  }

  switch (aEvent->mMessage) {
    case eWheel:
      if (sMouseMoved != 0 &&
          OutOfTime(sMouseMoved,
                    StaticPrefs::mousewheel_transaction_ignoremovedelay())) {
        // Terminate the current mousewheel transaction if the mouse moved more
        // than ignoremovedelay milliseconds ago
        WTXN_LOG("Wheel transaction ending due to transaction timeout");
        EndTransaction();
      }
      return;
    case eMouseMove:
    case eDragOver: {
      WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
      if (mouseEvent->IsReal()) {
        // If the cursor is moving to be outside the frame,
        // terminate the scrollwheel transaction.
        LayoutDeviceIntPoint pt = GetScreenPoint(mouseEvent);
        auto r = LayoutDeviceIntRect::FromAppUnitsToNearest(
            sScrollTargetFrame->GetScreenRectInAppUnits(),
            sScrollTargetFrame->PresContext()->AppUnitsPerDevPixel());
        if (!r.Contains(pt)) {
          WTXN_LOG("Wheel transaction ending due to mousemove");
          EndTransaction();
          return;
        }

        // For mouse move events where the wheel transaction is still valid, the
        // stored event target should be reset.
        sEventTargetFrame = nullptr;

        // If the cursor is moving inside the frame, and it is less than
        // ignoremovedelay milliseconds since the last scroll operation, ignore
        // the mouse move; otherwise, record the current mouse move time to be
        // checked later
        if (!sMouseMoved &&
            OutOfTime(sTime,
                      StaticPrefs::mousewheel_transaction_ignoremovedelay())) {
          sMouseMoved = PR_IntervalToMilliseconds(PR_IntervalNow());
        }
      }
      return;
    }
    case eKeyPress:
    case eKeyUp:
    case eKeyDown:
    case eMouseUp:
    case eMouseDown:
    case eMouseDoubleClick:
    case ePointerAuxClick:
    case ePointerClick:
    case eContextMenu:
    case eDrop:
      WTXN_LOG("Wheel transaction ending due to keyboard event");
      EndTransaction();
      return;
    default:
      break;
  }
}

/* static */
void WheelTransaction::OnRemoveElement(nsIContent* aContent) {
  // If we have no current wheel event transaction, there is no internal state
  // to be updated.
  if (!sEventTargetFrame) {
    return;
  }

  if (sEventTargetFrame->GetContent() == aContent) {
    // Only invalidate the wheel transaction event target frame when the
    // remove target is the event target of the wheel event group. The
    // scroll target frame of the wheel event group may still be valid.
    //
    // With the stored event target unset, the target for any following
    // events will be the frame found using the input coordinates.
    sEventTargetFrame = nullptr;
  }
}

/* static */
void WheelTransaction::Shutdown() { NS_IF_RELEASE(sTimer); }

/* static */
void WheelTransaction::OnFailToScrollTarget() {
  MOZ_ASSERT(sScrollTargetFrame, "We don't have mouse scrolling transaction");

  if (StaticPrefs::test_mousescroll()) {
    // This event is used for automated tests, see bug 442774.
    const nsCOMPtr<nsIContent> content = sScrollTargetFrame->GetContent();
    nsContentUtils::DispatchEventOnlyToChrome(
        content, u"MozMouseScrollFailed"_ns, CanBubble::eYes, Cancelable::eYes);
  }
  // The target frame might be destroyed in the event handler, at that time,
  // we need to finish the current transaction
  if (!sScrollTargetFrame) {
    WTXN_LOG("Wheel transaction ending due to failed scroll");
    EndTransaction();
  }
}

/* static */
void WheelTransaction::OnTimeout(nsITimer* aTimer, void* aClosure) {
  if (!sScrollTargetFrame) {
    // The transaction target was destroyed already
    WTXN_LOG("Wheel transaction ending due to target removal");
    EndTransaction();
    return;
  }
  WTXN_LOG("Wheel transaction may end due to timeout");
  // Store the sScrollTargetFrame, the variable becomes null in EndTransaction.
  nsIFrame* frame = sScrollTargetFrame;
  // We need to finish current transaction before DOM event firing. Because
  // the next DOM event might create strange situation for us.
  MayEndTransaction();

  if (StaticPrefs::test_mousescroll()) {
    // This event is used for automated tests, see bug 442774.
    const nsCOMPtr<nsIContent> content = frame->GetContent();
    nsContentUtils::DispatchEventOnlyToChrome(
        content, u"MozMouseScrollTransactionTimeout"_ns, CanBubble::eYes,
        Cancelable::eYes);
  }
}

/* static */
void WheelTransaction::SetTimeout() {
  if (!sTimer) {
    sTimer = NS_NewTimer().take();
    if (!sTimer) {
      return;
    }
  }
  sTimer->Cancel();
  DebugOnly<nsresult> rv = sTimer->InitWithNamedFuncCallback(
      OnTimeout, nullptr, StaticPrefs::mousewheel_transaction_timeout(),
      nsITimer::TYPE_ONE_SHOT, "WheelTransaction::SetTimeout"_ns);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "nsITimer::InitWithNamedFuncCallback failed");
}

/* static */
LayoutDeviceIntPoint WheelTransaction::GetScreenPoint(WidgetGUIEvent* aEvent) {
  NS_ASSERTION(aEvent, "aEvent is null");
  NS_ASSERTION(aEvent->mWidget, "aEvent-mWidget is null");
  return aEvent->mRefPoint + aEvent->mWidget->WidgetToScreenOffset();
}

/* static */
DeltaValues WheelTransaction::AccelerateWheelDelta(WidgetWheelEvent* aEvent) {
  DeltaValues result = OverrideSystemScrollSpeed(aEvent);

  // Don't accelerate the delta values if the event isn't line scrolling.
  if (aEvent->mDeltaMode != dom::WheelEvent_Binding::DOM_DELTA_LINE) {
    return result;
  }

  // Accelerate by the sScrollSeriesCounter
  int32_t start = StaticPrefs::mousewheel_acceleration_start();
  if (start >= 0 && sScrollSeriesCounter >= start) {
    int32_t factor = StaticPrefs::mousewheel_acceleration_factor();
    if (factor > 0) {
      result.deltaX = ComputeAcceleratedWheelDelta(result.deltaX, factor);
      result.deltaY = ComputeAcceleratedWheelDelta(result.deltaY, factor);
    }
  }

  return result;
}

/* static */
double WheelTransaction::ComputeAcceleratedWheelDelta(double aDelta,
                                                      int32_t aFactor) {
  return mozilla::ComputeAcceleratedWheelDelta(aDelta, sScrollSeriesCounter,
                                               aFactor);
}

/* static */
DeltaValues WheelTransaction::OverrideSystemScrollSpeed(
    WidgetWheelEvent* aEvent) {
  MOZ_ASSERT(sScrollTargetFrame, "We don't have mouse scrolling transaction");

  // If the event doesn't scroll to both X and Y, we don't need to do anything
  // here.
  if (!aEvent->mDeltaX && !aEvent->mDeltaY) {
    return DeltaValues(aEvent);
  }

  return DeltaValues(aEvent->OverriddenDeltaX(), aEvent->OverriddenDeltaY());
}

/******************************************************************/
/* mozilla::ScrollbarsForWheel                                    */
/******************************************************************/

constinit AutoWeakFrame ScrollbarsForWheel::sActiveOwner;
constinit AutoWeakFrame
    ScrollbarsForWheel::sActivatedScrollTargets[kNumberOfTargets];

bool ScrollbarsForWheel::sHadWheelStart = false;
bool ScrollbarsForWheel::sOwnWheelTransaction = false;

/* static */
void ScrollbarsForWheel::PrepareToScrollText(EventStateManager* aESM,
                                             nsIFrame* aTargetFrame,
                                             WidgetWheelEvent* aEvent) {
  if (aEvent->mMessage == eWheelOperationStart) {
    WheelTransaction::OwnScrollbars(false);
    if (!IsActive()) {
      TemporarilyActivateAllPossibleScrollTargets(aESM, aTargetFrame, aEvent);
      sHadWheelStart = true;
    }
  } else {
    DeactivateAllTemporarilyActivatedScrollTargets();
  }
}

/* static */
void ScrollbarsForWheel::SetActiveScrollTarget(
    ScrollContainerFrame* aScrollTarget) {
  if (!sHadWheelStart) {
    return;
  }
  if (!aScrollTarget) {
    return;
  }
  sHadWheelStart = false;
  sActiveOwner = aScrollTarget;
  aScrollTarget->ScrollbarActivityStarted();
}

/* static */
void ScrollbarsForWheel::MayInactivate() {
  if (!sOwnWheelTransaction && WheelTransaction::GetScrollTargetFrame()) {
    WheelTransaction::OwnScrollbars(true);
  } else {
    Inactivate();
  }
}

/* static */
void ScrollbarsForWheel::Inactivate() {
  nsIScrollbarMediator* scrollbarMediator = do_QueryFrame(sActiveOwner);
  if (scrollbarMediator) {
    scrollbarMediator->ScrollbarActivityStopped();
  }
  sActiveOwner = nullptr;
  DeactivateAllTemporarilyActivatedScrollTargets();
  if (sOwnWheelTransaction) {
    WTXN_LOG("Wheel transaction ending due to inactive scrollbar");
    sOwnWheelTransaction = false;
    WheelTransaction::OwnScrollbars(false);
    WheelTransaction::EndTransaction();
  }
}

/* static */
bool ScrollbarsForWheel::IsActive() {
  if (sActiveOwner) {
    return true;
  }
  for (auto& sActivatedScrollTarget : sActivatedScrollTargets) {
    if (sActivatedScrollTarget) {
      return true;
    }
  }
  return false;
}

/* static */
void ScrollbarsForWheel::OwnWheelTransaction(bool aOwn) {
  sOwnWheelTransaction = aOwn;
}

/* static */
void ScrollbarsForWheel::TemporarilyActivateAllPossibleScrollTargets(
    EventStateManager* aESM, nsIFrame* aTargetFrame, WidgetWheelEvent* aEvent) {
  AutoWeakFrame weakTargetFrame(aTargetFrame);
  for (size_t i = 0; i < kNumberOfTargets; i++) {
    const DeltaValues* dir = &directions[i];
    AutoWeakFrame* scrollTarget = &sActivatedScrollTargets[i];
    MOZ_ASSERT(!*scrollTarget, "scroll target still temporarily activated!");
    ScrollContainerFrame* target = aESM->ComputeScrollTarget(
        aTargetFrame, dir->deltaX, dir->deltaY, aEvent,
        EventStateManager::COMPUTE_DEFAULT_ACTION_TARGET);
    if (target) {
      *scrollTarget = target;
      target->ScrollbarActivityStarted();
      if (!weakTargetFrame.IsAlive()) {
        return;
      }
    }
  }
}

/* static */
void ScrollbarsForWheel::DeactivateAllTemporarilyActivatedScrollTargets() {
  for (auto& sActivatedScrollTarget : sActivatedScrollTargets) {
    AutoWeakFrame* scrollTarget = &sActivatedScrollTarget;
    if (*scrollTarget) {
      nsIScrollbarMediator* scrollbarMediator = do_QueryFrame(*scrollTarget);
      if (scrollbarMediator) {
        scrollbarMediator->ScrollbarActivityStopped();
      }
      *scrollTarget = nullptr;
    }
  }
}

/******************************************************************/
/* mozilla::WheelDeltaHorizontalizer                              */
/******************************************************************/

void WheelDeltaHorizontalizer::Horizontalize() {
  MOZ_ASSERT(!mWheelEvent.mDeltaValuesHorizontalizedForDefaultHandler,
             "Wheel delta values in one wheel scroll event are being adjusted "
             "a second time");

  // Log the old values.
  mOldDeltaX = mWheelEvent.mDeltaX;
  mOldDeltaZ = mWheelEvent.mDeltaZ;
  mOldOverflowDeltaX = mWheelEvent.mOverflowDeltaX;
  mOldLineOrPageDeltaX = mWheelEvent.mLineOrPageDeltaX;

  // Move deltaY values to deltaX and set both deltaY and deltaZ to 0.
  mWheelEvent.mDeltaX = mWheelEvent.mDeltaY;
  mWheelEvent.mDeltaY = 0.0;
  mWheelEvent.mDeltaZ = 0.0;
  mWheelEvent.mOverflowDeltaX = mWheelEvent.mOverflowDeltaY;
  mWheelEvent.mOverflowDeltaY = 0.0;
  mWheelEvent.mLineOrPageDeltaX = mWheelEvent.mLineOrPageDeltaY;
  mWheelEvent.mLineOrPageDeltaY = 0;

  // Mark it horizontalized in order to restore the delta values when this
  // instance is being destroyed.
  mWheelEvent.mDeltaValuesHorizontalizedForDefaultHandler = true;
  mHorizontalized = true;
}

void WheelDeltaHorizontalizer::CancelHorizontalization() {
  // Restore the horizontalized delta.
  if (mHorizontalized &&
      mWheelEvent.mDeltaValuesHorizontalizedForDefaultHandler) {
    mWheelEvent.mDeltaY = mWheelEvent.mDeltaX;
    mWheelEvent.mDeltaX = mOldDeltaX;
    mWheelEvent.mDeltaZ = mOldDeltaZ;
    mWheelEvent.mOverflowDeltaY = mWheelEvent.mOverflowDeltaX;
    mWheelEvent.mOverflowDeltaX = mOldOverflowDeltaX;
    mWheelEvent.mLineOrPageDeltaY = mWheelEvent.mLineOrPageDeltaX;
    mWheelEvent.mLineOrPageDeltaX = mOldLineOrPageDeltaX;
    mWheelEvent.mDeltaValuesHorizontalizedForDefaultHandler = false;
    mHorizontalized = false;
  }
}

WheelDeltaHorizontalizer::~WheelDeltaHorizontalizer() {
  CancelHorizontalization();
}

/******************************************************************/
/* mozilla::AutoDirWheelDeltaAdjuster                             */
/******************************************************************/

bool AutoDirWheelDeltaAdjuster::ShouldBeAdjusted() {
  // Sometimes, this function can be called more than one time. If we have
  // already checked if the scroll should be adjusted, there's no need to check
  // it again.
  if (mCheckedIfShouldBeAdjusted) {
    return mShouldBeAdjusted;
  }
  mCheckedIfShouldBeAdjusted = true;

  // For an auto-dir wheel scroll, if all the following conditions are met, we
  // should adjust X and Y values:
  // 1. There is only one non-zero value between DeltaX and DeltaY.
  // 2. There is only one direction for the target that overflows and is
  //    scrollable with wheel.
  // 3. The direction described in Condition 1 is orthogonal to the one
  // described in Condition 2.
  if ((mDeltaX && mDeltaY) || (!mDeltaX && !mDeltaY)) {
    return false;
  }
  if (mDeltaX) {
    if (CanScrollAlongXAxis()) {
      return false;
    }
    if (IsHorizontalContentRightToLeft()) {
      mShouldBeAdjusted =
          mDeltaX > 0 ? CanScrollUpwards() : CanScrollDownwards();
    } else {
      mShouldBeAdjusted =
          mDeltaX < 0 ? CanScrollUpwards() : CanScrollDownwards();
    }
    return mShouldBeAdjusted;
  }
  MOZ_ASSERT(0 != mDeltaY);
  if (CanScrollAlongYAxis()) {
    return false;
  }
  if (IsHorizontalContentRightToLeft()) {
    mShouldBeAdjusted =
        mDeltaY > 0 ? CanScrollLeftwards() : CanScrollRightwards();
  } else {
    mShouldBeAdjusted =
        mDeltaY < 0 ? CanScrollLeftwards() : CanScrollRightwards();
  }
  return mShouldBeAdjusted;
}

void AutoDirWheelDeltaAdjuster::Adjust() {
  if (!ShouldBeAdjusted()) {
    return;
  }
  std::swap(mDeltaX, mDeltaY);
  if (IsHorizontalContentRightToLeft()) {
    mDeltaX *= -1;
    mDeltaY *= -1;
  }
  mShouldBeAdjusted = false;
  OnAdjusted();
}

/******************************************************************/
/* mozilla::ESMAutoDirWheelDeltaAdjuster                          */
/******************************************************************/

ESMAutoDirWheelDeltaAdjuster::ESMAutoDirWheelDeltaAdjuster(
    WidgetWheelEvent& aEvent, nsIFrame& aScrollFrame, bool aHonoursRoot)
    : AutoDirWheelDeltaAdjuster(aEvent.mDeltaX, aEvent.mDeltaY),
      mLineOrPageDeltaX(aEvent.mLineOrPageDeltaX),
      mLineOrPageDeltaY(aEvent.mLineOrPageDeltaY),
      mOverflowDeltaX(aEvent.mOverflowDeltaX),
      mOverflowDeltaY(aEvent.mOverflowDeltaY) {
  mScrollTargetFrame = aScrollFrame.GetScrollTargetFrame();
  MOZ_ASSERT(mScrollTargetFrame);

  nsIFrame* honouredFrame = nullptr;
  if (aHonoursRoot) {
    // If we are going to honour root, first try to get the frame for <body> as
    // the honoured root, because <body> is in preference to <html> if the
    // current document is an HTML document.
    dom::Document* document = aScrollFrame.PresShell()->GetDocument();
    if (document) {
      dom::Element* bodyElement = document->GetBodyElement();
      if (bodyElement) {
        honouredFrame = bodyElement->GetPrimaryFrame();
      }
    }

    if (!honouredFrame) {
      // If there is no <body> frame, fall back to the real root frame.
      honouredFrame = aScrollFrame.PresShell()->GetRootScrollContainerFrame();
    }

    if (!honouredFrame) {
      // If there is no root scroll frame, fall back to the current scrolling
      // frame.
      honouredFrame = &aScrollFrame;
    }
  } else {
    honouredFrame = &aScrollFrame;
  }

  WritingMode writingMode = honouredFrame->GetWritingMode();
  // Get whether the honoured frame's content in the horizontal direction starts
  // from right to left(E.g. it's true either if "writing-mode: vertical-rl", or
  // if "writing-mode: horizontal-tb; direction: rtl;" in CSS).
  mIsHorizontalContentRightToLeft = writingMode.IsPhysicalRTL();
}

void ESMAutoDirWheelDeltaAdjuster::OnAdjusted() {
  // Adjust() only adjusted basic deltaX and deltaY, which are not enough for
  // ESM, we should continue to adjust line-or-page and overflow values.
  if (mDeltaX) {
    // A vertical scroll was adjusted to be horizontal.
    MOZ_ASSERT(0 == mDeltaY);

    mLineOrPageDeltaX = mLineOrPageDeltaY;
    mLineOrPageDeltaY = 0;
    mOverflowDeltaX = mOverflowDeltaY;
    mOverflowDeltaY = 0;
  } else {
    // A horizontal scroll was adjusted to be vertical.
    MOZ_ASSERT(0 != mDeltaY);

    mLineOrPageDeltaY = mLineOrPageDeltaX;
    mLineOrPageDeltaX = 0;
    mOverflowDeltaY = mOverflowDeltaX;
    mOverflowDeltaX = 0;
  }
  if (mIsHorizontalContentRightToLeft) {
    // If in RTL writing mode, reverse the side the scroll will go towards.
    mLineOrPageDeltaX *= -1;
    mLineOrPageDeltaY *= -1;
    mOverflowDeltaX *= -1;
    mOverflowDeltaY *= -1;
  }
}

bool ESMAutoDirWheelDeltaAdjuster::CanScrollAlongXAxis() const {
  return mScrollTargetFrame->GetAvailableScrollingDirections().contains(
      layers::ScrollDirection::eHorizontal);
}

bool ESMAutoDirWheelDeltaAdjuster::CanScrollAlongYAxis() const {
  return mScrollTargetFrame->GetAvailableScrollingDirections().contains(
      layers::ScrollDirection::eVertical);
}

bool ESMAutoDirWheelDeltaAdjuster::CanScrollUpwards() const {
  nsPoint scrollPt = mScrollTargetFrame->GetScrollPosition();
  nsRect scrollRange = mScrollTargetFrame->GetScrollRange();
  return static_cast<double>(scrollRange.y) < scrollPt.y;
}

bool ESMAutoDirWheelDeltaAdjuster::CanScrollDownwards() const {
  nsPoint scrollPt = mScrollTargetFrame->GetScrollPosition();
  nsRect scrollRange = mScrollTargetFrame->GetScrollRange();
  return static_cast<double>(scrollRange.YMost()) > scrollPt.y;
}

bool ESMAutoDirWheelDeltaAdjuster::CanScrollLeftwards() const {
  nsPoint scrollPt = mScrollTargetFrame->GetScrollPosition();
  nsRect scrollRange = mScrollTargetFrame->GetScrollRange();
  return static_cast<double>(scrollRange.x) < scrollPt.x;
}

bool ESMAutoDirWheelDeltaAdjuster::CanScrollRightwards() const {
  nsPoint scrollPt = mScrollTargetFrame->GetScrollPosition();
  nsRect scrollRange = mScrollTargetFrame->GetScrollRange();
  return static_cast<double>(scrollRange.XMost()) > scrollPt.x;
}

bool ESMAutoDirWheelDeltaAdjuster::IsHorizontalContentRightToLeft() const {
  return mIsHorizontalContentRightToLeft;
}

/******************************************************************/
/* mozilla::ESMAutoDirWheelDeltaRestorer                          */
/******************************************************************/

/*explicit*/
ESMAutoDirWheelDeltaRestorer::ESMAutoDirWheelDeltaRestorer(
    WidgetWheelEvent& aEvent)
    : mEvent(aEvent),
      mOldDeltaX(aEvent.mDeltaX),
      mOldDeltaY(aEvent.mDeltaY),
      mOldLineOrPageDeltaX(aEvent.mLineOrPageDeltaX),
      mOldLineOrPageDeltaY(aEvent.mLineOrPageDeltaY),
      mOldOverflowDeltaX(aEvent.mOverflowDeltaX),
      mOldOverflowDeltaY(aEvent.mOverflowDeltaY) {}

ESMAutoDirWheelDeltaRestorer::~ESMAutoDirWheelDeltaRestorer() {
  if (mOldDeltaX == mEvent.mDeltaX || mOldDeltaY == mEvent.mDeltaY) {
    // The delta of the event wasn't adjusted during the lifetime of this
    // |ESMAutoDirWheelDeltaRestorer| instance. No need to restore it.
    return;
  }

  bool forRTL = false;

  // First, restore the basic deltaX and deltaY.
  std::swap(mEvent.mDeltaX, mEvent.mDeltaY);
  if (mOldDeltaX != mEvent.mDeltaX || mOldDeltaY != mEvent.mDeltaY) {
    // If X and Y still don't equal to their original values after being
    // swapped, then it must be because they were adjusted for RTL.
    forRTL = true;
    mEvent.mDeltaX *= -1;
    mEvent.mDeltaY *= -1;
    MOZ_ASSERT(mOldDeltaX == mEvent.mDeltaX && mOldDeltaY == mEvent.mDeltaY);
  }

  if (mEvent.mDeltaX) {
    // A horizontal scroll was adjusted to be vertical during the lifetime of
    // this instance.
    MOZ_ASSERT(0 == mEvent.mDeltaY);

    // Restore the line-or-page and overflow values to be horizontal.
    mEvent.mOverflowDeltaX = mEvent.mOverflowDeltaY;
    mEvent.mLineOrPageDeltaX = mEvent.mLineOrPageDeltaY;
    if (forRTL) {
      mEvent.mOverflowDeltaX *= -1;
      mEvent.mLineOrPageDeltaX *= -1;
    }
    mEvent.mOverflowDeltaY = mOldOverflowDeltaY;
    mEvent.mLineOrPageDeltaY = mOldLineOrPageDeltaY;
  } else {
    // A vertical scroll was adjusted to be horizontal during the lifetime of
    // this instance.
    MOZ_ASSERT(0 != mEvent.mDeltaY);

    // Restore the line-or-page and overflow values to be vertical.
    mEvent.mOverflowDeltaY = mEvent.mOverflowDeltaX;
    mEvent.mLineOrPageDeltaY = mEvent.mLineOrPageDeltaX;
    if (forRTL) {
      mEvent.mOverflowDeltaY *= -1;
      mEvent.mLineOrPageDeltaY *= -1;
    }
    mEvent.mOverflowDeltaX = mOldOverflowDeltaX;
    mEvent.mLineOrPageDeltaX = mOldLineOrPageDeltaX;
  }
}

}  // namespace mozilla
