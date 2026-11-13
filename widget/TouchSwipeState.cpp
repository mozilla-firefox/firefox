/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TouchSwipeState.h"

#include "TouchSwipeInfo.h"
#include "Units.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Maybe.h"
#include "mozilla/layers/TouchCounter.h"

namespace mozilla {

void TouchSwipeState::AssertCountConsistency() const {
  const mozilla::DebugOnly<uint32_t> count = touchCounter.GetActiveTouchCount();
  MOZ_ASSERT((phase == TouchSwipeState::Phase::Idle && count == 0) ||
             (phase == TouchSwipeState::Phase::Pending && count == 1) ||
             (phase == TouchSwipeState::Phase::WaitingForAPZ && count == 1) ||
             (phase == TouchSwipeState::Phase::Tracking && count == 1) ||
             (phase == TouchSwipeState::Phase::Rejected && count > 0));
}

void TouchSwipeState::Start(SideBits aSwipableDirections,
                            const ScreenIntPoint& aTouchPoint) {
  // swipableDirections may already have been narrowed if this block waited
  // again on its first touchmove.
  MOZ_ASSERT(
      (phase == Phase::Idle && swipableDirections == SideBits::eLeftRight) ||
          phase == Phase::WaitingForAPZ,
      "This is the start point of touch swipe-to-nav. "
      "TouchSwipeState should have an initial state or waiting state");
  swipableDirections &= aSwipableDirections;
  phase = Phase::Pending;
  previousTouchPoint = Some(aTouchPoint);
}

void TouchSwipeState::Wait(uint64_t aInputBlockId,
                           const ScreenIntPoint& aTouchPoint) {
  MOZ_ASSERT(
      (phase == Phase::Idle && swipableDirections == SideBits::eLeftRight) ||
          phase == Phase::Pending,
      "This is the start point of touch swipe-to-nav. "
      "TouchSwipeState should have an initial state or pending state");
  phase = Phase::WaitingForAPZ;
  touchSwipeInfoQueue = std::make_unique<TouchSwipeInfoQueue>(aInputBlockId);
  previousTouchPoint = Some(aTouchPoint);
}

void TouchSwipeState::Reset() { *this = TouchSwipeState{}; }

void TouchSwipeState::QueueInfo(TouchSwipeInfo&& aInfo) {
  MOZ_ASSERT(touchSwipeInfoQueue);
  MOZ_ASSERT(touchSwipeInfoQueue->inputBlockId == aInfo.result.mInputBlockId);
  touchSwipeInfoQueue->queuedInfo.AppendElement(std::move(aInfo));
}

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
    case TouchSwipeState::Phase::WaitingForAPZ:
      MOZ_ASSERT_UNREACHABLE(
          "Inputs are queued while waiting for APZ and handled after Start()");
      return;
  }
}
}  // namespace mozilla
