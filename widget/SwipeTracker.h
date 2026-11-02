/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SwipeTracker_h
#define SwipeTracker_h

#include "EventForwards.h"
#include "InputData.h"
#include "Units.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/layers/AxisPhysicsMSDModel.h"
#include "nsIWeakReferenceUtils.h"
#include "nsRefreshObservers.h"

class nsIWidget;
class nsRefreshDriver;

namespace mozilla {

enum class SwipeInputSource : uint8_t { Touchscreen, Touchpad };

/**
 * SwipeTracker turns PanGestureInput events into swipe events
 * (WidgetSimpleGestureEvent) and dispatches them into Gecko.
 * The swiping behavior mirrors the behavior of the Cocoa API
 * -[NSEvent
 *     trackSwipeEventWithOptions:dampenAmountThresholdMin:max:usingHandler:].
 * The advantage of using this class over the Cocoa API is that this class
 * properly supports submitting queued up events to it, and that it hopefully
 * doesn't intermittently break scrolling the way the Cocoa API does (bug
 * 927702).
 *
 * The swipe direction is either left or right. It is determined before the
 * SwipeTracker is created and stays fixed during the swipe.
 * During the swipe, the swipe has a current "value" which is between 0 and the
 * target value. The target value is either 1 (swiping left) or -1 (swiping
 * right) - see SwipeSuccessTargetValue().
 * A swipe can either succeed or fail. If it succeeds, the swipe animation
 * animates towards the success target value; if it fails, it animates back to
 * a value of 0. A swipe can only succeed if the user is swiping in an allowed
 * direction. (Since both the allowed directions and the swipe direction are
 * known at swipe start time, it's clear from the beginning whether a swipe is
 * doomed to fail. In that case, the purpose of the SwipeTracker is to simulate
 * a bounce-back animation.)
 */
class SwipeTracker final : public nsARefreshObserver {
 public:
  NS_INLINE_DECL_REFCOUNTING(SwipeTracker, override)

  SwipeTracker(nsIWidget& aWidget, const PanGestureInput& aSwipeStartEvent,
               uint32_t aAllowedDirections, uint32_t aSwipeDirection,
               SwipeInputSource aInputSource);

  void Destroy();

  void StartTracking(const PanGestureInput& aSwipeStartEvent);

  nsEventStatus ProcessEvent(const PanGestureInput& aEvent,
                             SwipeInputSource aInputSource,
                             bool aProcessingFirstEvent = false);
  void CancelSwipe(const TimeStamp& aTimeStamp);
  bool EventsAreControllingSwipe() const { return mEventsAreControllingSwipe; }

  static WidgetSimpleGestureEvent CreateSwipeGestureEvent(
      EventMessage aMsg, nsIWidget* aWidget,
      const LayoutDeviceIntPoint& aPosition, const TimeStamp& aTimeStamp);

  // nsARefreshObserver
  void WillRefresh(mozilla::TimeStamp aTime) override;

  static bool CanTriggerSwipe(const PanGestureInput& aPanInput,
                              SwipeInputSource aInputSource);

 protected:
  ~SwipeTracker();

  bool SwipingInAllowedDirection() const {
    return mAllowedDirections & mSwipeDirection;
  }
  double SwipeSuccessTargetValue() const;
  double ClampToAllowedRange(double aGestureAmount) const;
  bool ComputeSwipeSuccess() const;
  void StartAnimating(double aStartValue, double aTargetValue);
  void SwipeFinished(const TimeStamp& aTimeStamp);
  void UnregisterFromRefreshDriver();
  void SendSwipeEvent(EventMessage aMsg, uint32_t aDirection, double aDelta,
                      const TimeStamp& aTimeStamp);

  nsWeakPtr mWidget;
  RefPtr<nsRefreshDriver> mRefreshDriver;
  layers::AxisPhysicsMSDModel mAxis;
  const LayoutDeviceIntPoint mEventPosition;
  TimeStamp mLastEventTimeStamp;
  TimeStamp mLastAnimationFrameTime;
  const uint32_t mAllowedDirections;
  const uint32_t mSwipeDirection;
  const SwipeInputSource mInputSource;
  double mGestureAmount = 0.0;
  double mCurrentVelocity = 0.0;
  bool mDeltaTypeIsPage = false;
  bool mEventsAreControllingSwipe = true;
  bool mEventsHaveStartedNewGesture = false;
  bool mRegisteredWithRefreshDriver = false;
  bool mLastComputedSwipeSuccess = false;
};

struct SwipeEventQueue {
  SwipeEventQueue(uint32_t aAllowedDirections, uint64_t aInputBlockId)
      : allowedDirections(aAllowedDirections), inputBlockId(aInputBlockId) {}

  nsTArray<PanGestureInput> queuedEvents;
  uint32_t allowedDirections;
  uint64_t inputBlockId;
};

}  // namespace mozilla

#endif  // SwipeTracker_h
