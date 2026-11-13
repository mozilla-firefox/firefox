/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TouchSwipeState.h"

#include "Units.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"
#include "mozilla/layers/TouchCounter.h"

namespace mozilla {

void TouchSwipeState::AssertCountConsistency() const {
  const mozilla::DebugOnly<uint32_t> count = touchCounter.GetActiveTouchCount();
  MOZ_ASSERT((phase == TouchSwipeState::Phase::Idle && count == 0) ||
             (phase == TouchSwipeState::Phase::Pending && count == 1) ||
             (phase == TouchSwipeState::Phase::Tracking && count == 1) ||
             (phase == TouchSwipeState::Phase::Rejected && count > 0));
}

void TouchSwipeState::Start(mozilla::SideBits aSwipableDirections,
                            const ScreenIntPoint& aTouchPoint) {
  MOZ_ASSERT(phase == Phase::Idle &&
                 swipableDirections == mozilla::SideBits::eLeftRight,
             "This is the start point of touch swipe-to-nav. "
             "TouchSwipeState should have an initial state");
  swipableDirections &= aSwipableDirections;
  phase = Phase::Pending;
  previousTouchPoint = mozilla::Some(aTouchPoint);
}

void TouchSwipeState::Reset() { *this = TouchSwipeState{}; }

ScreenIntPoint TouchSwipeState::GetDelta(const ScreenIntPoint& point) {
  MOZ_ASSERT(previousTouchPoint.isSome());
  return point - *previousTouchPoint;
}

// We should not use consumed inputs for swipe operation. But we have
// to keep counting the finger and changing phase to avoid inconsistency.
// Note that we don't have to call SetBrowserGestureResponse here at
// all, because APZ waits for that response only when it receives
// MULTITOUCH_MOVE and returns values other than
// nsEventStatus_eConsumeNoDefault. See InputQueue::ReceiveTouchInput.
void TouchSwipeState::ChangePhaseForConsumedTouchInput() {
  const auto count = touchCounter.GetActiveTouchCount();
  switch (phase) {
    case TouchSwipeState::Phase::Idle:
      if (count > 0) {
        // We do not use consumed inputs for swipe operation, even when
        // MULTITOUCH_START by the first finger reaches here,
        phase = TouchSwipeState::Phase::Rejected;
      }
      return;
    case TouchSwipeState::Phase::Pending:
    case TouchSwipeState::Phase::Tracking:
      if (count == 0) {
        Reset();
      } else if (count > 1) {
        phase = TouchSwipeState::Phase::Rejected;
      }
      return;
    case TouchSwipeState::Phase::Rejected:
      if (count == 0) {
        Reset();
      }
      return;
  }
}
}  // namespace mozilla
