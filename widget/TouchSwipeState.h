/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_TouchSwipeState_h
#define mozilla_TouchSwipeState_h

#include "Units.h"
#include "mozilla/Maybe.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/layers/TouchCounter.h"

namespace mozilla {

struct TouchSwipeInfo;
struct TouchSwipeInfoQueue;

// TouchSwipeState phase transition:
// - Initially Idle:       wait for a MULTITOUCH_START.
// - Idle -> Pending:      a MULTITOUCH_START records APZ's swipable
//                         directions.
// - Idle -> WaitingForAPZ:
//                         APZ cannot tell the swipable directions on
//                         MULTITOUCH_START yet. Subsequent inputs are queued.
// - Pending -> WaitingForAPZ:
//                         APZ starts waiting for a content response on the
//                         first MULTITOUCH_MOVE out of slop. Subsequent
//                         inputs are queued.
// - WaitingForAPZ -> Pending:
//                         APZ notifies the swipable directions. The queued
//                         inputs are replayed from Pending.
// - WaitingForAPZ -> Idle:
//                         a MULTITOUCH_START of a new gesture arrives while
//                         waiting. The queue is reset, and the new touch is
//                         handled from Idle.
// - WaitingForAPZ -> Rejected:
//                         a finger is added while waiting. The queue is
//                         reset.
// - Pending -> Tracking:  a MULTITOUCH_MOVE passes navigation-direction
//                         checks. In this case, APZ drops the block's events
//                         and the subsequent touch events are processed as
//                         swipe.
// - Tracking -> Rejected: a finger is added during swipe. Swipe is canceled
//                         and SwipeTracker is cleared.
// - Pending -> Rejected:  a finger is added, or a MULTITOUCH_MOVE fails
//                         navigation-direction checks.
// - Idle -> Rejected:     multiple fingers touch at the same time, or the
//                         first finger's touch is consumed by APZ.
// - ANY -> Idle:          the last touch ends, or MULTITOUCH_CANCEL resets
//                         any phase to the initial Idle state. During
//                         WaitingForAPZ, the phase does not go back to Idle
//                         right after the last touch ends, but when the queued
//                         inputs are replayed.
// Adding a finger and the last touch ending change the phase even if APZ
// consumes the event.
struct TouchSwipeState {
  // Nothing until Start() records the first touch point.
  mozilla::Maybe<ScreenIntPoint> previousTouchPoint;
  mozilla::SideBits swipableDirections = mozilla::SideBits::eLeftRight;
  enum class Phase {
    // Wait for touchstart to determine the swipable directions.
    Idle,
    // Swipable directions are known. Wait for movement outside APZ's slop
    // region, then check whether the movement can start a browser swipe.
    Pending,
    // Convert touch movements into pan gesture input for SwipeTracker.
    Tracking,
    // Do not reconsider swipe navigation until all touches end or CANCEL
    // resets the state.
    Rejected,
    // Wait for APZ response to trigger or reject swipe.
    WaitingForAPZ,
  };
  Phase phase = Phase::Idle;
  // Swipe-to-nav on touchscreen doesn't support a multi-finger gesture. This
  // tracks the number of fingers on the screen. It is updated before passing
  // events to APZ so that it also counts the events APZ consumes.
  mozilla::layers::TouchCounter touchCounter;

  // Non-null only while phase is WaitingForAPZ.
  UniquePtr<TouchSwipeInfoQueue> touchSwipeInfoQueue;

  void AssertCountConsistency() const;

  void Start(mozilla::SideBits aSwipableDirections,
             const ScreenIntPoint& aTouchPoint);

  void Wait(uint64_t aInputBlockId, const ScreenIntPoint& aTouchPoint);

  void Reset();

  ScreenIntPoint GetDelta(const ScreenIntPoint& point);

  // We should not use consumed inputs for swipe operation. But we have
  // to keep counting the finger and changing phase to avoid inconsistency.
  // Note that we don't have to call SetBrowserGestureResponse here at
  // all, because APZ waits for that response only when it receives
  // MULTITOUCH_MOVE and returns values other than
  // nsEventStatus_eConsumeNoDefault. See InputQueue::ReceiveTouchInput.
  void ChangePhaseForConsumedTouchInput();

  void QueueInfo(TouchSwipeInfo&& aInfo);
};

}  // namespace mozilla

#endif  // mozilla_TouchSwipeState_h
