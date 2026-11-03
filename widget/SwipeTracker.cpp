/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SwipeTracker.h"

#include "InputData.h"
#include "UnitTransforms.h"
#include "mozilla/FlushType.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/TouchEvents.h"
#include "mozilla/dom/SimpleGestureEventBinding.h"
#include "nsIWidget.h"
#include "nsRefreshDriver.h"

// These values were tweaked to make the physics feel similar to the native
// swipe.
static const double kSpringForce = 250.0;
static const double kSwipeSuccessThreshold = 0.25;

namespace mozilla {

static already_AddRefed<nsRefreshDriver> GetRefreshDriver(nsIWidget& aWidget) {
  nsIWidgetListener* widgetListener = aWidget.GetWidgetListener();
  PresShell* presShell =
      widgetListener ? widgetListener->GetPresShell() : nullptr;
  nsPresContext* presContext =
      presShell ? presShell->GetPresContext() : nullptr;
  RefPtr<nsRefreshDriver> refreshDriver =
      presContext ? presContext->RefreshDriver() : nullptr;
  return refreshDriver.forget();
}

SwipeTracker::SwipeTracker(nsIWidget& aWidget,
                           const PanGestureInput& aSwipeStartEvent,
                           uint32_t aAllowedDirections,
                           uint32_t aSwipeDirection,
                           SwipeInputSource aInputSource)
    : mWidget(do_GetWeakReference(&aWidget)),
      mRefreshDriver(GetRefreshDriver(aWidget)),
      mAxis(0.0, 0.0, 0.0, kSpringForce, 1.0),
      mEventPosition(RoundedToInt(ViewAs<LayoutDevicePixel>(
          aSwipeStartEvent.mPanStartPoint,
          PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent))),
      mLastEventTimeStamp(aSwipeStartEvent.mTimeStamp),
      mAllowedDirections(aAllowedDirections),
      mSwipeDirection(aSwipeDirection),
      mInputSource(aInputSource) {}

void SwipeTracker::StartTracking(const PanGestureInput& aSwipeStartEvent) {
  SendSwipeEvent(eSwipeGestureStart, 0, 0.0, aSwipeStartEvent.mTimeStamp);
  ProcessEvent(aSwipeStartEvent, mInputSource,
               /* aProcessingFirstEvent = */ true);
}

void SwipeTracker::Destroy() { UnregisterFromRefreshDriver(); }

SwipeTracker::~SwipeTracker() {
  MOZ_RELEASE_ASSERT(!mRegisteredWithRefreshDriver,
                     "Destroy needs to be called before deallocating");
}

double SwipeTracker::SwipeSuccessTargetValue() const {
  return mSwipeDirection == dom::SimpleGestureEvent_Binding::DIRECTION_RIGHT
             ? -1.0
             : 1.0;
}

double SwipeTracker::ClampToAllowedRange(double aGestureAmount) const {
  // gestureAmount needs to stay between -1 and 0 when swiping right and
  // between 0 and 1 when swiping left.
  double min =
      mSwipeDirection == dom::SimpleGestureEvent_Binding::DIRECTION_RIGHT ? -1.0
                                                                          : 0.0;
  double max =
      mSwipeDirection == dom::SimpleGestureEvent_Binding::DIRECTION_LEFT ? 1.0
                                                                         : 0.0;
  return std::clamp(aGestureAmount, min, max);
}

bool SwipeTracker::ComputeSwipeSuccess() const {
  double targetValue = SwipeSuccessTargetValue();

  // If the fingers were moving away from the target direction when they were
  // lifted from the touchpad, abort the swipe.
  if (mCurrentVelocity * targetValue <
      -StaticPrefs::widget_swipe_velocity_twitch_tolerance()) {
    return false;
  }

  return (mGestureAmount * targetValue +
          mCurrentVelocity * targetValue *
              StaticPrefs::widget_swipe_success_velocity_contribution()) >=
         kSwipeSuccessThreshold;
}

nsEventStatus SwipeTracker::ProcessEvent(
    const PanGestureInput& aEvent, SwipeInputSource aInputSource,
    bool aProcessingFirstEvent /* = false */) {
  RefPtr<SwipeTracker> selfPin(this);

  if (aInputSource != mInputSource) {
    return mEventsAreControllingSwipe ? nsEventStatus_eConsumeNoDefault
                                      : nsEventStatus_eIgnore;
  }

  // If the fingers have already been lifted or the swipe direction is where
  // navigation is impossible, don't process this event for swiping.
  if (!mEventsAreControllingSwipe || !SwipingInAllowedDirection()) {
    // Return nsEventStatus_eConsumeNoDefault for events from the swipe gesture
    // and nsEventStatus_eIgnore for events of subsequent scroll gestures.
    if (aEvent.mType == PanGestureInput::PANGESTURE_MAYSTART ||
        aEvent.mType == PanGestureInput::PANGESTURE_START) {
      mEventsHaveStartedNewGesture = true;
    }
    return mEventsHaveStartedNewGesture ? nsEventStatus_eIgnore
                                        : nsEventStatus_eConsumeNoDefault;
  }

  nsCOMPtr<nsIWidget> widget = do_QueryReferent(mWidget);
  if (!widget) {
    return nsEventStatus_eIgnore;
  }
  mDeltaTypeIsPage = aEvent.mDeltaType == PanGestureInput::PANDELTA_PAGE;
  double delta = [&]() -> double {
    if (mDeltaTypeIsPage) {
      return -aEvent.mPanDisplacement.x / StaticPrefs::widget_swipe_page_size();
    }
    return -aEvent.mPanDisplacement.x / widget->GetDefaultScaleInternal() /
           StaticPrefs::widget_swipe_pixel_size();
  }();

  mGestureAmount = ClampToAllowedRange(mGestureAmount + delta);
  if (aEvent.mType != PanGestureInput::PANGESTURE_END) {
    if (!aProcessingFirstEvent) {
      double elapsedSeconds = std::max(
          0.008, (aEvent.mTimeStamp - mLastEventTimeStamp).ToSeconds());
      mCurrentVelocity = delta / elapsedSeconds;
    }
    mLastEventTimeStamp = aEvent.mTimeStamp;
  }

  const bool computedSwipeSuccess = ComputeSwipeSuccess();
  if (computedSwipeSuccess != mLastComputedSwipeSuccess) {
    mLastComputedSwipeSuccess = computedSwipeSuccess;
    if (StaticPrefs::widget_swipe_haptic_feedback_enabled()) {
      widget->PerformHapticFeedback(HapticFeedbackType::ThresholdCrossing);
    }
  }
  double eventAmount = mGestureAmount;
  // If ComputeSwipeSuccess returned false because the users fingers were
  // moving slightly away from the target direction then we do not want to
  // display the UI as if we were at the success threshold as that would
  // give a false indication that navigation would happen.
  if (!computedSwipeSuccess && (eventAmount >= kSwipeSuccessThreshold ||
                                eventAmount <= -kSwipeSuccessThreshold)) {
    eventAmount = 0.999 * kSwipeSuccessThreshold;
    if (mGestureAmount < 0.f) {
      eventAmount = -eventAmount;
    }
  }

  SendSwipeEvent(eSwipeGestureUpdate, 0, eventAmount, aEvent.mTimeStamp);

  if (aEvent.mType == PanGestureInput::PANGESTURE_END) {
    mEventsAreControllingSwipe = false;
    if (computedSwipeSuccess) {
      // Let's use same timestamp as previous event because this is caused by
      // the preceding event.
      SendSwipeEvent(eSwipeGesture, mSwipeDirection, 0.0, aEvent.mTimeStamp);
      UnregisterFromRefreshDriver();
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("SwipeTracker::SwipeFinished",
                                 [swipeTracker = RefPtr<SwipeTracker>(this),
                                  timeStamp = aEvent.mTimeStamp] {
                                   swipeTracker->SwipeFinished(timeStamp);
                                 }));
    } else {
      StartAnimating(eventAmount, 0.0);
    }
  }

  return nsEventStatus_eConsumeNoDefault;
}

void SwipeTracker::StartAnimating(double aStartValue, double aTargetValue) {
  mAxis.SetPosition(aStartValue);
  mAxis.SetDestination(aTargetValue);
  mAxis.SetVelocity(mCurrentVelocity);

  mLastAnimationFrameTime = TimeStamp::Now();

  // Add ourselves as a refresh driver observer. The refresh driver
  // will call WillRefresh for each animation frame until we
  // unregister ourselves.
  MOZ_RELEASE_ASSERT(!mRegisteredWithRefreshDriver,
                     "We only want a single refresh driver registration");
  if (mRefreshDriver) {
    mRefreshDriver->AddRefreshObserver(this, FlushType::Style,
                                       "Swipe animation");
    mRegisteredWithRefreshDriver = true;
  }
}

void SwipeTracker::WillRefresh(TimeStamp aTime) {
  RefPtr<SwipeTracker> selfPin(this);

  // FIXME(emilio): shouldn't we be using `aTime`?
  TimeStamp now = TimeStamp::Now();
  mAxis.Simulate(now - mLastAnimationFrameTime);
  mLastAnimationFrameTime = now;

  const double wholeSize = mDeltaTypeIsPage
                               ? StaticPrefs::widget_swipe_page_size()
                               : StaticPrefs::widget_swipe_pixel_size();
  // NOTE(emilio): It's unclear this makes sense for page-based swiping, but
  // this preserves behavior and all platforms probably will end up converging
  // in pixel-based pan input, so...
  const double minIncrement = 1.0 / wholeSize;
  const bool isFinished = mAxis.IsFinished(minIncrement);

  mGestureAmount = isFinished ? mAxis.GetDestination() : mAxis.GetPosition();
  SendSwipeEvent(eSwipeGestureUpdate, 0, mGestureAmount, now);

  if (isFinished) {
    UnregisterFromRefreshDriver();
    SwipeFinished(now);
  }
}

void SwipeTracker::CancelSwipe(const TimeStamp& aTimeStamp) {
  SendSwipeEvent(eSwipeGestureEnd, 0, 0.0, aTimeStamp);
}

void SwipeTracker::SwipeFinished(const TimeStamp& aTimeStamp) {
  SendSwipeEvent(eSwipeGestureEnd, 0, 0.0, aTimeStamp);
  nsCOMPtr<nsIWidget> widget = do_QueryReferent(mWidget);
  if (!widget) {
    return;
  }
  widget->SwipeFinished();
}

void SwipeTracker::UnregisterFromRefreshDriver() {
  if (mRegisteredWithRefreshDriver) {
    MOZ_ASSERT(mRefreshDriver, "How were we able to register, then?");
    mRefreshDriver->RemoveRefreshObserver(this, FlushType::Style);
    mRegisteredWithRefreshDriver = false;
  }
}

/* static */ WidgetSimpleGestureEvent SwipeTracker::CreateSwipeGestureEvent(
    EventMessage aMsg, nsIWidget* aWidget,
    const LayoutDeviceIntPoint& aPosition, const TimeStamp& aTimeStamp) {
  // XXX Why isn't this initialized with nsCocoaUtils::InitInputEvent()?
  WidgetSimpleGestureEvent geckoEvent(true, aMsg, aWidget);
  geckoEvent.mModifiers = 0;
  // XXX How about geckoEvent.mTime?
  geckoEvent.mTimeStamp = aTimeStamp;
  geckoEvent.mRefPoint = aPosition;
  geckoEvent.mButtons = 0;
  return geckoEvent;
}

void SwipeTracker::SendSwipeEvent(EventMessage aMsg, uint32_t aDirection,
                                  double aDelta, const TimeStamp& aTimeStamp) {
  nsCOMPtr<nsIWidget> widget = do_QueryReferent(mWidget);
  if (!widget) {
    return;
  }
  WidgetSimpleGestureEvent geckoEvent =
      CreateSwipeGestureEvent(aMsg, widget, mEventPosition, aTimeStamp);
  geckoEvent.mDirection = aDirection;
  geckoEvent.mDelta = aDelta;
  geckoEvent.mAllowedDirections = mAllowedDirections;
  widget->DispatchWindowEvent(geckoEvent);
}

// static
bool SwipeTracker::CanTriggerSwipe(const PanGestureInput& aPanInput,
                                   SwipeInputSource aInputSource) {
  if (StaticPrefs::widget_disable_swipe_tracker()) {
    return false;
  }

  if (aPanInput.mType != PanGestureInput::PANGESTURE_START) {
    return false;
  }

  // Only initiate horizontal tracking for events whose horizontal element is
  // at least |thresholdRatio| times larger than its vertical element. This
  // minimizes performance problems with vertical scrolls (by minimizing the
  // possibility that they'll be misinterpreted as horizontal swipes), while
  // still tolerating a small vertical element to a true horizontal swipe.  The
  // number '8' was arrived at by trial and error. '3.7' aims to make the
  // threshold degree roughly 15 degree. ('8' was too tight for touch swipe.)
  const double thresholdRatio =
      aInputSource == SwipeInputSource::Touchpad ? 8.0 : 3.7;
  return std::abs(aPanInput.mPanDisplacement.x) >
         std::abs(aPanInput.mPanDisplacement.y) * thresholdRatio;
}

}  // namespace mozilla
