/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InputQueue.h"

#include <inttypes.h>

#include "AsyncPanZoomController.h"
#include "GestureEventListener.h"
#include "InputBlockState.h"
#include "OverscrollHandoffState.h"
#include "QueuedInput.h"
#include "mozilla/Assertions.h"
#include "mozilla/EventForwards.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/ToString.h"
#include "mozilla/layers/APZBridge.h"
#include "mozilla/layers/APZThreadUtils.h"

static mozilla::LazyLogModule sApzInpLog("apz.inputqueue");
#define INPQ_LOG(...) MOZ_LOG(sApzInpLog, LogLevel::Debug, (__VA_ARGS__))
#define INPQ_LOG_TEST() MOZ_LOG_TEST(sApzInpLog, LogLevel::Debug)

namespace mozilla {
namespace layers {

InputQueue::InputQueue() = default;

InputQueue::~InputQueue() { mQueuedInputs.Clear(); }

APZEventResult InputQueue::ReceiveInputEvent(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags, InputData& aEvent,
    const Maybe<nsTArray<TouchBehaviorFlags>>& aTouchBehaviors,
    InitialTouchMove aInitialTouchMove) {
  APZThreadUtils::AssertOnControllerThread();

  AutoRunImmediateTimeout timeoutRunner{this};

  switch (aEvent.mInputType) {
    case MULTITOUCH_INPUT: {
      const MultiTouchInput& event = aEvent.AsMultiTouchInput();
      return ReceiveTouchInput(aTarget, aFlags, event, aTouchBehaviors,
                               aInitialTouchMove);
    }

    case SCROLLWHEEL_INPUT: {
      const ScrollWheelInput& event = aEvent.AsScrollWheelInput();
      return ReceiveScrollWheelInput(aTarget, aFlags, event);
    }

    case PANGESTURE_INPUT: {
      const PanGestureInput& event = aEvent.AsPanGestureInput();
      return ReceivePanGestureInput(aTarget, aFlags, event);
    }

    case PINCHGESTURE_INPUT: {
      const PinchGestureInput& event = aEvent.AsPinchGestureInput();
      return ReceivePinchGestureInput(aTarget, aFlags, event);
    }

    case MOUSE_INPUT: {
      MouseInput& event = aEvent.AsMouseInput();
      return ReceiveMouseInput(aTarget, aFlags, event);
    }

    case KEYBOARD_INPUT: {
      // Every keyboard input must have a confirmed target
      MOZ_ASSERT(aTarget && aFlags.mTargetConfirmed);

      const KeyboardInput& event = aEvent.AsKeyboardInput();
      return ReceiveKeyboardInput(aTarget, aFlags, event);
    }

    default: {
      // The `mStatus` for other input type is only used by tests, so just
      // pass through the return value of HandleInputEvent() for now.
      APZEventResult result(aTarget, aFlags);
      nsEventStatus status =
          aTarget->HandleInputEvent(aEvent, aTarget->GetTransformToThis());
      switch (status) {
        case nsEventStatus_eIgnore:
          result.SetStatusAsIgnore();
          break;
        case nsEventStatus_eConsumeNoDefault:
          result.SetStatusAsConsumeNoDefault();
          break;
        case nsEventStatus_eConsumeDoDefault:
          result.SetStatusAsConsumeDoDefault(aTarget);
          break;
        default:
          MOZ_ASSERT_UNREACHABLE("An invalid status");
          break;
      }
      return result;
    }
  }
}

APZEventResult InputQueue::ReceiveTouchInput(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags, const MultiTouchInput& aEvent,
    const Maybe<nsTArray<TouchBehaviorFlags>>& aTouchBehaviors,
    InitialTouchMove aInitialTouchMove) {
  APZEventResult result(aTarget, aFlags);

  RefPtr<TouchBlockState> block;
  bool waitingForContentResponse = false;
  if (aEvent.mType == MultiTouchInput::MULTITOUCH_START) {
    nsTArray<TouchBehaviorFlags> currentBehaviors;
    bool haveBehaviors = false;
    if (mActiveTouchBlock) {
      haveBehaviors =
          mActiveTouchBlock->GetAllowedTouchBehaviors(currentBehaviors);
      // If the behaviours aren't set, but the main-thread response timer on
      // the block is expired we still treat it as though it has behaviors,
      // because in that case we still want to interrupt the fast-fling and
      // use the default behaviours.
      haveBehaviors |= mActiveTouchBlock->IsContentResponseTimerExpired();
    }

    block = StartNewTouchBlock(aTarget, aFlags);
    INPQ_LOG("started new touch block %p id %" PRIu64 " for target %p\n",
             block.get(), block->GetBlockId(), aTarget.get());

    // XXX using the chain from |block| here may be wrong in cases where the
    // target isn't confirmed and the real target turns out to be something
    // else. For now assume this is rare enough that it's not an issue.
    if (mQueuedInputs.IsEmpty() && aEvent.mTouches.Length() == 1 &&
        block->GetOverscrollHandoffChain()->HasFastFlungApzc() &&
        haveBehaviors) {
      // If we're already in a fast fling, and a single finger goes down, then
      // we want special handling for the touch event, because it shouldn't get
      // delivered to content. Note that we don't set this flag when going
      // from a fast fling to a pinch state (i.e. second finger goes down while
      // the first finger is moving).
      block->SetDuringFastFling();
      block->SetConfirmedTargetApzc(
          aTarget, InputBlockState::TargetConfirmationState::eConfirmed,
          InputQueueIterator() /* the block was just created so it has no events
                                */
          ,
          false /* not a scrollbar drag */);
      block->SetAllowedTouchBehaviors(currentBehaviors);
      INPQ_LOG("block %p tagged as fast-motion\n", block.get());
    } else if (aTouchBehaviors) {
      // If this block isn't started during a fast-fling, and APZCTM has
      // provided touch behavior information, then put it on the block so
      // that the ArePointerEventsConsumable call below can use it.
      block->SetAllowedTouchBehaviors(*aTouchBehaviors);
    }

    CancelAnimationsForNewBlock(block);

    waitingForContentResponse = MaybeRequestContentResponse(aTarget, block);
  } else {
    // for touch inputs that don't start a block, APZCTM shouldn't be giving
    // us any touch behaviors.
    MOZ_ASSERT(aTouchBehaviors.isNothing());

    // If the active touch block is for a long tap, add new touch events into
    // the original touch block, to ensure that they're only processed if the
    // original touch block is not prevented.
    block = mActiveTouchBlock && mActiveTouchBlock->ForLongTap()
                ? mPrevActiveTouchBlock.get()
                : mActiveTouchBlock.get();
    if (!block) {
      NS_WARNING(
          "Received a non-start touch event while no touch blocks active!");
      return result;
    }

    INPQ_LOG("received new touch event (type=%d) in block %p\n", aEvent.mType,
             block.get());

    // If this is the first touch-move of the block and it needs to be
    // dispatched to content (e.g. because a touch-start handler has just
    // installed an APZ-aware touchmove listener), wait for a content
    // response before processing it so that the new listener has a chance
    // to preventDefault().
    //
    // The "!HasContentResponded() || !IsDefaultPrevented()" short-circuit
    // also covers the case where the touch-start was not preventDefault-ed
    // and content has already responded: we still want to wait again on
    // the first touch-move. Note that IsDefaultPrevented() must not be
    // called when HasContentResponded() is false; the short-circuit
    // ordering guarantees that.
    if (aInitialTouchMove == InitialTouchMove::Yes &&
        aFlags.IsFastPathApzAwareDispatchToContent() &&
        !block->IsDuringFastFling() &&
        (!block->HasContentResponded() || !block->IsDefaultPrevented())) {
      block->ResetContentResponseTimerExpired();
      ScheduleMainThreadTimeout(aTarget, block);
      waitingForContentResponse = true;
    }
  }

  result.mInputBlockId = block->GetBlockId();

  // Note that the |aTarget| the APZCTM sent us may contradict the confirmed
  // target set on the block. In this case the confirmed target (which may be
  // null) should take priority. This is equivalent to just always using the
  // target (confirmed or not) from the block.
  RefPtr<AsyncPanZoomController> target = block->GetTargetApzc();

  // XXX calling ArePointerEventsConsumable on |target| may be wrong here if
  // the target isn't confirmed and the real target turns out to be something
  // else. For now assume this is rare enough that it's not an issue.
  PointerEventsConsumableFlags consumableFlags;
  if (target) {
    consumableFlags = target->ArePointerEventsConsumable(block, aEvent);
  }
  if (block->IsDuringFastFling()) {
    INPQ_LOG("dropping event due to block %p being in fast motion\n",
             block.get());
    result.SetStatusForFastFling(*block, aFlags, consumableFlags, target);
  } else {  // handling depends on ArePointerEventsConsumable()
    bool consumable = consumableFlags.IsConsumable();
    TouchBlockState::InSlop wasInSlop = block->IsInSlop();
    if (block->UpdateSlopState(aEvent, consumable)) {
      INPQ_LOG("dropping event due to block %p being in %sslop\n", block.get(),
               consumable ? "" : "mini-");
      result.SetStatusAsConsumeNoDefault();
    } else {
      if (block->NeedsContentResponseAfterLongTap(aEvent, wasInSlop)) {
        INPQ_LOG(
            "bailing out from in-stop state in block %p after a long-tap "
            "happened\n",
            block.get());
        block->ResetContentResponseTimerExpired();
        ScheduleMainThreadTimeout(aTarget, block);
      }
      block->SetNeedsToWaitTouchMove(false);
      result.SetStatusForTouchEvent(*block, aFlags, consumableFlags, target);
    }
  }

  if (aEvent.mMayTriggerSwipe) {
    switch (aEvent.mType) {
      case MultiTouchInput::MULTITOUCH_START:
        // Do not wait for a browser gesture response on touchstart. The widget
        // only records the swipable directions here; it needs a move outside
        // slop to decide whether to start a swipe and send a response. Moves
        // within slop are suppressed, and ending or cancelling a touch before
        // that decision does not send a response either. Waiting on touchstart
        // would therefore leave a tap or stationary press queued indefinitely.
        // APZ must also process touchstart to begin tap/long-press recognition.
        // TODO: Handle the case in which the target is not confirmed or touch
        // action is not confirmed.
        if (block->IsTargetConfirmed() && block->HasAllowedTouchBehaviors() &&
            block->TouchActionAllowsPanningX()) {
          MOZ_ASSERT(target, "A confirmed target should be non-null here");
          result.mSwipableDirectionsForTouch =
              block->GetOverscrollHandoffChain()->GetSwipableDirections(
                  *target);
        }
        break;
      case MultiTouchInput::MULTITOUCH_MOVE:
        // Wait before ProcessQueue() so that APZ cannot pan before widget
        // decides if it triggers swipe. Moves whose status is set as
        // eConsumeNoDefault never reach the widget and they get no response, so
        // they must not wait. Note that the widget clears mMayTriggerSwipe
        // after the first swipe decision, so later moves, which don't set
        // browser gesture response, do not wait.
        if (result.GetStatus() != nsEventStatus_eConsumeNoDefault) {
          block->SetNeedsToWaitForBrowserGestureResponse(true);
        }
        break;
      default:
        break;
    }
  }

  mQueuedInputs.AppendElement(std::make_unique<QueuedInput>(aEvent, *block));
  ProcessQueue();

  // If this block just started and is waiting for a content response, but
  // also in a slop state (i.e. touchstart gets delivered to content but
  // not any touchmoves), then we might end up in a situation where we don't
  // get the content response until the timeout is hit because we never exit
  // the slop state. But if that timeout is longer than the long-press timeout,
  // then the long-press gets delayed too. Avoid that by scheduling a callback
  // with the long-press timeout that will force the block to get processed.
  int32_t longTapTimeout = StaticPrefs::ui_click_hold_context_menus_delay();
  int32_t contentTimeout = StaticPrefs::apz_content_response_timeout();
  if (waitingForContentResponse && longTapTimeout < contentTimeout &&
      bool(block->IsInSlop()) && GestureEventListener::IsLongTapEnabled()) {
    MOZ_ASSERT(aEvent.mType == MultiTouchInput::MULTITOUCH_START ||
               aInitialTouchMove == InitialTouchMove::Yes);
    MOZ_ASSERT(!block->IsDuringFastFling());
    RefPtr<Runnable> maybeLongTap = NewRunnableMethod<uint64_t>(
        "layers::InputQueue::MaybeLongTapTimeout", this,
        &InputQueue::MaybeLongTapTimeout, block->GetBlockId());
    INPQ_LOG("scheduling maybe-long-tap timeout for target %p\n",
             aTarget.get());
    aTarget->PostDelayedTask(maybeLongTap.forget(), longTapTimeout);
  }

  return result;
}

APZEventResult InputQueue::ReceiveMouseInput(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags, MouseInput& aEvent) {
  APZEventResult result(aTarget, aFlags);

  // On a new mouse down we can have a new target so we must force a new block
  // with a new target.
  bool newBlock = DragTracker::StartsDrag(aEvent);

  RefPtr<DragBlockState> block = newBlock ? nullptr : mActiveDragBlock.get();
  if (block && block->HasReceivedMouseUp()) {
    block = nullptr;
  }

  if (!block && mDragTracker.InDrag()) {
    // If there's no current drag block, but we're getting a move with a button
    // down, we need to start a new drag block because we're obviously already
    // in the middle of a drag (it probably got interrupted by something else).
    INPQ_LOG(
        "got a drag event outside a drag block, need to create a block to hold "
        "it\n");
    newBlock = true;
  }

  mDragTracker.Update(aEvent);

  if (!newBlock && !block) {
    // This input event is not in a drag block, so we're not doing anything
    // with it, return eIgnore.
    return result;
  }

  if (!block) {
    MOZ_ASSERT(newBlock);
    block = new DragBlockState(aTarget, aFlags, aEvent);

    INPQ_LOG(
        "started new drag block %p id %" PRIu64
        " "
        "for %sconfirmed target %p; on scrollbar: %d; on scrollthumb: %d\n",
        block.get(), block->GetBlockId(), aFlags.mTargetConfirmed ? "" : "un",
        aTarget.get(), aFlags.mHitScrollbar, aFlags.mHitScrollThumb);

    mActiveDragBlock = block;

    if (aFlags.mHitScrollThumb || !aFlags.mHitScrollbar) {
      // If we're running autoscroll, we'll always cancel it during the
      // following call of CancelAnimationsForNewBlock.  At this time,
      // we don't want to fire `click` event on the web content for web-compat
      // with Chrome.  Therefore, we notify widget of it with the flag.
      if ((aEvent.mType == MouseInput::MOUSE_DOWN ||
           aEvent.mType == MouseInput::MOUSE_UP) &&
          block->GetOverscrollHandoffChain()->HasAutoscrollApzc()) {
        aEvent.mPreventClickEvent = true;
      }
      CancelAnimationsForNewBlock(block);
    }
    MaybeRequestContentResponse(aTarget, block);
  }

  result.mInputBlockId = block->GetBlockId();

  mQueuedInputs.AppendElement(std::make_unique<QueuedInput>(aEvent, *block));
  ProcessQueue();

  if (DragTracker::EndsDrag(aEvent)) {
    block->MarkMouseUpReceived();
  }

  // The event is part of a drag block and could potentially cause
  // scrolling, so return DoDefault.
  result.SetStatusAsConsumeDoDefault(*block);
  return result;
}

APZEventResult InputQueue::ReceiveScrollWheelInput(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags, const ScrollWheelInput& aEvent) {
  APZEventResult result(aTarget, aFlags);

  RefPtr<WheelBlockState> block = mActiveWheelBlock.get();
  // If the block is not accepting new events we'll create a new input block
  // (and therefore a new wheel transaction).
  if (block &&
      (!block->ShouldAcceptNewEvent() || block->MaybeTimeout(aEvent))) {
    block = nullptr;
  }

  MOZ_ASSERT(!block || block->InTransaction());

  if (!block) {
    block = new WheelBlockState(aTarget, aFlags, aEvent);
    INPQ_LOG("started new scroll wheel block %p id %" PRIu64
             " for %starget %p\n",
             block.get(), block->GetBlockId(),
             aFlags.mTargetConfirmed ? "confirmed " : "", aTarget.get());

    mActiveWheelBlock = block;

    MaybeRequestContentResponse(aTarget, block);
  } else {
    INPQ_LOG("received new wheel event in block %p\n", block.get());
  }

  result.mInputBlockId = block->GetBlockId();

  // Note that the |aTarget| the APZCTM sent us may contradict the confirmed
  // target set on the block. In this case the confirmed target (which may be
  // null) should take priority. This is equivalent to just always using the
  // target (confirmed or not) from the block, which is what
  // ProcessQueue() does.
  mQueuedInputs.AppendElement(std::make_unique<QueuedInput>(aEvent, *block));

  // The WheelBlockState needs to affix a counter to the event before we process
  // it. Note that the counter is affixed to the copy in the queue rather than
  // |aEvent|.
  block->Update(mQueuedInputs.LastElement()->Input()->AsScrollWheelInput());

  ProcessQueue();

  result.SetStatusAsConsumeDoDefault(*block);
  return result;
}

APZEventResult InputQueue::ReceiveKeyboardInput(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags, const KeyboardInput& aEvent) {
  APZEventResult result(aTarget, aFlags);

  RefPtr<KeyboardBlockState> block = mActiveKeyboardBlock.get();

  // If the block is targeting a different Apzc than this keyboard event then
  // we'll create a new input block
  if (block && block->GetTargetApzc() != aTarget) {
    block = nullptr;
  }

  if (!block) {
    block = new KeyboardBlockState(aTarget);
    INPQ_LOG("started new keyboard block %p id %" PRIu64 " for target %p\n",
             block.get(), block->GetBlockId(), aTarget.get());

    mActiveKeyboardBlock = block;
  } else {
    INPQ_LOG("received new keyboard event in block %p\n", block.get());
  }

  result.mInputBlockId = block->GetBlockId();

  mQueuedInputs.AppendElement(std::make_unique<QueuedInput>(aEvent, *block));

  ProcessQueue();

  // If APZ is allowing passive listeners then we must dispatch the event to
  // content, otherwise we can consume the event.
  if (StaticPrefs::apz_keyboard_passive_listeners()) {
    result.SetStatusAsConsumeDoDefault(*block);
  } else {
    result.SetStatusAsConsumeNoDefault();
  }
  return result;
}

static bool CanScrollTargetHorizontally(const PanGestureInput& aInitialEvent,
                                        PanGestureBlockState* aBlock) {
  PanGestureInput horizontalComponent = aInitialEvent;
  horizontalComponent.mPanDisplacement.y = 0;
  ScrollDirections allowedScrollDirections;
  RefPtr<AsyncPanZoomController> horizontallyScrollableAPZC =
      aBlock->GetOverscrollHandoffChain()->FindScrollTarget(
          horizontalComponent, &allowedScrollDirections,
          OverscrollHandoffChain::IncludeOverscroll::No);
  return horizontallyScrollableAPZC &&
         horizontallyScrollableAPZC == aBlock->GetTargetApzc() &&
         allowedScrollDirections.contains(ScrollDirection::eHorizontal);
}

APZEventResult InputQueue::ReceivePanGestureInput(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags, const PanGestureInput& aEvent) {
  APZEventResult result(aTarget, aFlags);

  if (aEvent.mType == PanGestureInput::PANGESTURE_INTERRUPTED) {
    if (RefPtr<PanGestureBlockState> block = mActivePanGestureBlock.get()) {
      mQueuedInputs.AppendElement(
          std::make_unique<QueuedInput>(aEvent, *block));
      ProcessQueue();
    }
    result.SetStatusAsIgnore();
    return result;
  }

  bool startsNewBlock = aEvent.mType == PanGestureInput::PANGESTURE_MAYSTART ||
                        aEvent.mType == PanGestureInput::PANGESTURE_START;

  RefPtr<PanGestureBlockState> block;
  if (!startsNewBlock) {
    block = mActivePanGestureBlock.get();
  }

  PanGestureInput event = aEvent;

  // Below `SetStatusAsConsumeDoDefault()` preserves `mHandledResult` of
  // `result` which was set in the ctor of APZEventResult at the top of this
  // function based on `aFlag` so that the `mHandledResult` value is reliable to
  // tell whether the event will be handled by the root content APZC at least
  // for swipe-navigation stuff. E.g. if a pan-start event scrolled the root
  // scroll container, we don't need to anything for swipe-navigation.
  result.SetStatusAsConsumeDoDefault();

  bool terminateSynthesizedBlock = false;
  if (!block || block->WasInterrupted()) {
    if (event.mType == PanGestureInput::PANGESTURE_MOMENTUMSTART ||
        event.mType == PanGestureInput::PANGESTURE_MOMENTUMPAN ||
        event.mType == PanGestureInput::PANGESTURE_MOMENTUMEND) {
      // If there are momentum events after an interruption, discard them.
      // However, if there is a non-momentum event (indicating the user
      // continued scrolling on the touchpad), a new input block is started
      // by turning the event into a pan-start below.
      return result;
    }
    if (!startsNewBlock) {
      // A zero-delta pan-end has nothing to scroll and the gesture it stands
      // for is already over, so drop it rather than synthesizing a block whose
      // OnPanBegin would cancel animations. Only do so while an animation is
      // running though: that animation will relieve any overscroll when it
      // ends, whereas otherwise the synthesized block is the only thing left
      // that can snap back overscroll an interrupted gesture caused.
      if (event.mType == PanGestureInput::PANGESTURE_END &&
          event.mPanDisplacement == ScreenPoint{}) {
        RefPtr<const OverscrollHandoffChain> handoffChain =
            aTarget->BuildOverscrollHandoffChain();
        if (handoffChain && handoffChain->HasAnimatingApzc()) {
          INPQ_LOG(
              "dropping a received zero-delta pan-end while an animation is "
              "running\n");
          return result;
        }
      }
      // Only PANGESTURE_MAYSTART or PANGESTURE_START events are allowed to
      // start a new pan gesture block, but we really want to start a new block
      // here, so we magically turn this input into a PANGESTURE_START.
      INPQ_LOG(
          "transmogrifying pan input %d to PANGESTURE_START for new block %p\n",
          event.mType, block.get());
      // If the event is a pan-end, there will be no additional pan-end coming
      // to end the synthesized block we are starting. Turning it into a bare
      // pan-start would leave the synthesized block active in a panning state
      // and, via OnPanBegin's CancelAnimations(ExcludeOverscroll), cancel any
      // running overscroll snap-back animation without ever snapping back --
      // leaving the overscroll gutter stuck. Emit an explicit pan-end below so
      // the block ends (OnPanEnd) and snaps back.
      terminateSynthesizedBlock =
          event.mType == PanGestureInput::PANGESTURE_END;
      event.mType = PanGestureInput::PANGESTURE_START;
    }
    block = new PanGestureBlockState(aTarget, aFlags, event);
    INPQ_LOG("started new pan gesture block %p id %" PRIu64 " for target %p\n",
             block.get(), block->GetBlockId(), aTarget.get());

    if (event.mType == PanGestureInput::PANGESTURE_MAYSTART) {
      block->ConfirmForHoldGesture();
    }

    mActivePanGestureBlock = block;

    const bool waitingForContentResponse =
        MaybeRequestContentResponse(aTarget, block);

    bool targetCanScrollHorizontally =
        CanScrollTargetHorizontally(event, block);
    result.mTargetCanScrollHorizontally = targetCanScrollHorizontally;
    if (event.AllowsSwipe() && !targetCanScrollHorizontally) {
      // We will ask the browser whether this pan event is going to be used for
      // swipe or not, so we need to wait the response.
      block->SetNeedsToWaitForBrowserGestureResponse(true);
      if (!waitingForContentResponse) {
        ScheduleMainThreadTimeout(aTarget, block);
      }
      if (aFlags.mTargetConfirmed) {
        // This event may trigger a swipe gesture, depending on what our caller
        // wants to do it. We need to suspend handling of this block until we
        // get a content response which will tell us whether to proceed or abort
        // the block.
        block->SetNeedsToWaitForContentResponse(true);

        // Inform our caller that we haven't scrolled in response to the event
        // and that a swipe can be started from this event if desired.
        result.SetStatusAsIgnore();
      }
    }
  } else {
    INPQ_LOG("received new pan event (type=%d) in block %p\n", aEvent.mType,
             block.get());
  }

  result.mInputBlockId = block->GetBlockId();

  // Note that the |aTarget| the APZCTM sent us may contradict the confirmed
  // target set on the block. In this case the confirmed target (which may be
  // null) should take priority. This is equivalent to just always using the
  // target (confirmed or not) from the block, which is what
  // ProcessQueue() does.
  mQueuedInputs.AppendElement(std::make_unique<QueuedInput>(event, *block));
  if (terminateSynthesizedBlock) {
    // Immediately end the block we just synthesized from a pan-end so OnPanEnd
    // runs and snaps back any overscroll, rather than leaving the block active.
    PanGestureInput terminator = event;
    terminator.mType = PanGestureInput::PANGESTURE_END;
    terminator.mPanDisplacement = ScreenPoint{};
    terminator.mLocalPanDisplacement = ParentLayerPoint{};
    mQueuedInputs.AppendElement(
        std::make_unique<QueuedInput>(terminator, *block));
  }
  ProcessQueue();

  return result;
}

APZEventResult InputQueue::ReceivePinchGestureInput(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags, const PinchGestureInput& aEvent) {
  APZEventResult result(aTarget, aFlags);

  RefPtr<PinchGestureBlockState> block;
  if (aEvent.mType != PinchGestureInput::PINCHGESTURE_START) {
    block = mActivePinchGestureBlock.get();
  }

  result.SetStatusAsConsumeDoDefault(aTarget);

  if (!block || block->WasInterrupted()) {
    if (aEvent.mType != PinchGestureInput::PINCHGESTURE_START) {
      // Only PINCHGESTURE_START events are allowed to start a new pinch gesture
      // block.
      INPQ_LOG("pinchgesture block %p was interrupted %d\n", block.get(),
               block ? block->WasInterrupted() : 0);
      return result;
    }
    block = new PinchGestureBlockState(aTarget, aFlags);
    INPQ_LOG("started new pinch gesture block %p id %" PRIu64
             " for target %p\n",
             block.get(), block->GetBlockId(), aTarget.get());

    mActivePinchGestureBlock = block;
    block->SetNeedsToWaitForContentResponse(true);

    CancelAnimationsForNewBlock(block);
    MaybeRequestContentResponse(aTarget, block);
  } else {
    INPQ_LOG("received new pinch event (type=%d) in block %p\n", aEvent.mType,
             block.get());
  }

  result.mInputBlockId = block->GetBlockId();

  // Note that the |aTarget| the APZCTM sent us may contradict the confirmed
  // target set on the block. In this case the confirmed target (which may be
  // null) should take priority. This is equivalent to just always using the
  // target (confirmed or not) from the block, which is what
  // ProcessQueue() does.
  mQueuedInputs.AppendElement(std::make_unique<QueuedInput>(aEvent, *block));
  ProcessQueue();

  return result;
}

void InputQueue::CancelAnimationsForNewBlock(InputBlockState* aBlock,
                                             CancelAnimationFlags aExtraFlags) {
  // We want to cancel animations here as soon as possible (i.e. without waiting
  // for content responses) because a finger has gone down and we don't want to
  // keep moving the content under the finger. However, to prevent "future"
  // touchstart events from interfering with "past" animations (i.e. from a
  // previous touch block that is still being processed) we only do this
  // animation-cancellation if there are no older touch blocks still in the
  // queue.
  if (mQueuedInputs.IsEmpty()) {
    aBlock->GetOverscrollHandoffChain()->CancelAnimations(
        aExtraFlags | ExcludeOverscroll | ScrollSnap);
  }
}

bool InputQueue::MaybeRequestContentResponse(
    const RefPtr<AsyncPanZoomController>& aTarget,
    CancelableBlockState* aBlock) {
  bool waitForMainThread = false;
  if (aBlock->IsTargetConfirmed()) {
    // Content won't prevent-default this, so we can just set the flag directly.
    INPQ_LOG("not waiting for content response on block %p\n", aBlock);
    aBlock->SetContentResponse(false);
  } else {
    waitForMainThread = true;
  }
  if (aBlock->AsTouchBlock() &&
      !aBlock->AsTouchBlock()->HasAllowedTouchBehaviors()) {
    INPQ_LOG("waiting for main thread touch-action info on block %p\n", aBlock);
    waitForMainThread = true;
  }
  if (waitForMainThread) {
    // We either don't know for sure if aTarget is the right APZC, or we may
    // need to wait to give content the opportunity to prevent-default the
    // touch events. Either way we schedule a timeout so the main thread stuff
    // can run.
    ScheduleMainThreadTimeout(aTarget, aBlock);
  }
  return waitForMainThread;
}

uint64_t InputQueue::InjectNewTouchBlock(AsyncPanZoomController* aTarget) {
  AutoRunImmediateTimeout timeoutRunner{this};
  TouchBlockState* block = StartNewTouchBlockForLongTap(aTarget);
  INPQ_LOG("injecting new touch block %p with id %" PRIu64 " and target %p\n",
           block, block->GetBlockId(), aTarget);
  ScheduleMainThreadTimeout(aTarget, block);
  return block->GetBlockId();
}

TouchBlockState* InputQueue::StartNewTouchBlock(
    const RefPtr<AsyncPanZoomController>& aTarget,
    TargetConfirmationFlags aFlags) {
  if (mPrevActiveTouchBlock && mActiveTouchBlock &&
      mActiveTouchBlock->ForLongTap()) {
    mPrevActiveTouchBlock->SetWaitingLongTapResult(false);
    mPrevActiveTouchBlock = nullptr;
  }

  TouchBlockState* newBlock =
      new TouchBlockState(aTarget, aFlags, mTouchCounter);

  mActiveTouchBlock = newBlock;
  return newBlock;
}

TouchBlockState* InputQueue::StartNewTouchBlockForLongTap(
    const RefPtr<AsyncPanZoomController>& aTarget) {
  TouchBlockState* newBlock = new TouchBlockState(
      aTarget, TargetConfirmationFlags{true}, mTouchCounter);

  TouchBlockState* currentBlock = GetCurrentTouchBlock();
  // We should never enter here without a current touch block, because this
  // codepath is invoked from the OnLongPress handler in
  // AsyncPanZoomController, which should bail out if there is no current
  // touch block.
  MOZ_ASSERT(currentBlock);
  newBlock->CopyPropertiesFrom(*currentBlock);
  newBlock->SetForLongTap();

  // Tell the original touch block that we are going to fire a long tap event.
  // NOTE: If we get a new touch-move event while we are waiting for a response
  // of the long-tap event, we need to wait it before processing the original
  // touch block because if the long-tap event response prevents us from
  // scrolling we must stop processing any subsequent touch-move events in the
  // same block.
  currentBlock->SetWaitingLongTapResult(true);

  // We need to keep the current block alive, it will be used once after this
  // new touch block for long-tap was processed.
  mPrevActiveTouchBlock = currentBlock;
  mActiveTouchBlock = newBlock;
  return newBlock;
}

InputBlockState* InputQueue::GetCurrentBlock() const {
  APZThreadUtils::AssertOnControllerThread();
  return mQueuedInputs.IsEmpty() ? nullptr : mQueuedInputs[0]->Block();
}

TouchBlockState* InputQueue::GetCurrentTouchBlock() const {
  InputBlockState* block = GetCurrentBlock();
  return block ? block->AsTouchBlock() : mActiveTouchBlock.get();
}

WheelBlockState* InputQueue::GetCurrentWheelBlock() const {
  InputBlockState* block = GetCurrentBlock();
  return block ? block->AsWheelBlock() : mActiveWheelBlock.get();
}

DragBlockState* InputQueue::GetCurrentDragBlock() const {
  InputBlockState* block = GetCurrentBlock();
  return block ? block->AsDragBlock() : mActiveDragBlock.get();
}

PanGestureBlockState* InputQueue::GetCurrentPanGestureBlock() const {
  InputBlockState* block = GetCurrentBlock();
  return block ? block->AsPanGestureBlock() : mActivePanGestureBlock.get();
}

PinchGestureBlockState* InputQueue::GetCurrentPinchGestureBlock() const {
  InputBlockState* block = GetCurrentBlock();
  return block ? block->AsPinchGestureBlock() : mActivePinchGestureBlock.get();
}

KeyboardBlockState* InputQueue::GetCurrentKeyboardBlock() const {
  InputBlockState* block = GetCurrentBlock();
  return block ? block->AsKeyboardBlock() : mActiveKeyboardBlock.get();
}

WheelBlockState* InputQueue::GetActiveWheelTransaction() const {
  WheelBlockState* block = mActiveWheelBlock.get();
  if (!block || !block->InTransaction()) {
    return nullptr;
  }
  return block;
}

bool InputQueue::HasReadyTouchBlock() const {
  return !mQueuedInputs.IsEmpty() &&
         mQueuedInputs[0]->Block()->AsTouchBlock() &&
         mQueuedInputs[0]->Block()->AsTouchBlock()->IsReadyForHandling();
}

bool InputQueue::AllowScrollHandoff() const {
  if (GetCurrentWheelBlock()) {
    return GetCurrentWheelBlock()->AllowScrollHandoff();
  }
  if (GetCurrentPanGestureBlock()) {
    return GetCurrentPanGestureBlock()->AllowScrollHandoff();
  }
  if (GetCurrentKeyboardBlock()) {
    return GetCurrentKeyboardBlock()->AllowScrollHandoff();
  }
  return true;
}

bool InputQueue::IsDragOnScrollbar(bool aHitScrollbar) {
  if (!mDragTracker.InDrag()) {
    return false;
  }
  // Now that we know we are in a drag, get the info from the drag tracker.
  // We keep it in the tracker rather than the block because the block can get
  // interrupted by something else (like a wheel event) and then a new block
  // will get created without the info we want. The tracker will persist though.
  return mDragTracker.IsOnScrollbar(aHitScrollbar);
}

void InputQueue::ScheduleMainThreadTimeout(
    const RefPtr<AsyncPanZoomController>& aTarget,
    CancelableBlockState* aBlock) {
  INPQ_LOG("scheduling main thread timeout for target %p\n", aTarget.get());
  RefPtr<Runnable> timeoutTask = NewRunnableMethod<uint64_t>(
      "layers::InputQueue::MainThreadTimeout", this,
      &InputQueue::MainThreadTimeout, aBlock->GetBlockId());
  int32_t timeout = StaticPrefs::apz_content_response_timeout();
  if (timeout == 0) {
    // If the timeout is zero, treat it as a request to ignore any main
    // thread confirmation and unconditionally use fallback behaviour for
    // when a timeout is reached. This codepath is used by tests that
    // want to exercise the fallback behaviour.
    // To ensure the fallback behaviour is used unconditionally, the timeout
    // is run right away instead of using PostDelayedTask(). However,
    // we can't run it right here, because MainThreadTimeout() expects that
    // the input block has at least one input event in mQueuedInputs, and
    // the event that triggered this call may not have been added to
    // mQueuedInputs yet.
    mImmediateTimeout = std::move(timeoutTask);
  } else {
    aTarget->PostDelayedTask(timeoutTask.forget(), timeout);
  }
}

InputBlockState* InputQueue::GetBlockForId(uint64_t aInputBlockId) {
  return FindBlockForId(aInputBlockId, nullptr);
}

void InputQueue::AddInputBlockCallback(uint64_t aInputBlockId,
                                       InputBlockCallback&& aCallbackInfo) {
  mInputBlockCallbacks.insert(InputBlockCallbackMap::value_type(
      aInputBlockId, std::move(aCallbackInfo)));
}

InputBlockState* InputQueue::FindBlockForId(
    uint64_t aInputBlockId, InputQueueIterator* aOutFirstInput) {
  for (auto it = mQueuedInputs.begin(), end = mQueuedInputs.end(); it != end;
       ++it) {
    if ((*it)->Block()->GetBlockId() == aInputBlockId) {
      if (aOutFirstInput) {
        *aOutFirstInput = InputQueueIterator(it, end);
      }
      return (*it)->Block();
    }
  }

  InputBlockState* block = nullptr;
  if (mActiveTouchBlock && mActiveTouchBlock->GetBlockId() == aInputBlockId) {
    block = mActiveTouchBlock.get();
  } else if (mPrevActiveTouchBlock &&
             mPrevActiveTouchBlock->GetBlockId() == aInputBlockId) {
    block = mPrevActiveTouchBlock.get();
  } else if (mActiveWheelBlock &&
             mActiveWheelBlock->GetBlockId() == aInputBlockId) {
    block = mActiveWheelBlock.get();
  } else if (mActiveDragBlock &&
             mActiveDragBlock->GetBlockId() == aInputBlockId) {
    block = mActiveDragBlock.get();
  } else if (mActivePanGestureBlock &&
             mActivePanGestureBlock->GetBlockId() == aInputBlockId) {
    block = mActivePanGestureBlock.get();
  } else if (mActivePinchGestureBlock &&
             mActivePinchGestureBlock->GetBlockId() == aInputBlockId) {
    block = mActivePinchGestureBlock.get();
  } else if (mActiveKeyboardBlock &&
             mActiveKeyboardBlock->GetBlockId() == aInputBlockId) {
    block = mActiveKeyboardBlock.get();
  }
  // Since we didn't encounter this block while iterating through mQueuedInputs,
  // it must have no events associated with it at the moment.
  if (aOutFirstInput) {
    *aOutFirstInput = InputQueueIterator();
  }
  return block;
}

void InputQueue::MainThreadTimeout(uint64_t aInputBlockId) {
  // It's possible that this function gets called after the controller thread
  // was discarded during shutdown.
  if (!APZThreadUtils::IsControllerThreadAlive()) {
    return;
  }
  APZThreadUtils::AssertOnControllerThread();

  INPQ_LOG("got a main thread timeout; block=%" PRIu64 "\n", aInputBlockId);
  bool success = false;
  InputQueueIterator firstInput;
  InputBlockState* inputBlock = FindBlockForId(aInputBlockId, &firstInput);
  if (inputBlock && inputBlock->AsCancelableBlock()) {
    CancelableBlockState* block = inputBlock->AsCancelableBlock();
    // time out the touch-listener response and also confirm the existing
    // target apzc in the case where the main thread doesn't get back to us
    // fast enough.
    success = block->TimeoutContentResponse();
    success |= block->SetConfirmedTargetApzc(
        block->GetTargetApzc(),
        InputBlockState::TargetConfirmationState::eTimedOut, firstInput,
        // This actually could be a scrollbar drag, but we pass
        // aForScrollbarDrag=false because for scrollbar drags,
        // SetConfirmedTargetApzc() will also be called by ConfirmDragBlock(),
        // and we pass aForScrollbarDrag=true there.
        false);
  } else if (inputBlock) {
    NS_WARNING("input block is not a cancelable block");
  }
  if (success) {
    if (inputBlock->AsTouchBlock() &&
        bool(inputBlock->AsTouchBlock()->IsInSlop())) {
      // If the touch block is still in slop, it's still possible this block
      // needs to send a touchmove to content after the long-press gesture
      // since preventDefault() in a touchmove event handler should stop
      // handling the block at all.
      inputBlock->AsTouchBlock()->SetNeedsToWaitTouchMove(true);
    }
    ProcessQueue();
  }
}

void InputQueue::MaybeLongTapTimeout(uint64_t aInputBlockId) {
  // It's possible that this function gets called after the controller thread
  // was discarded during shutdown.
  if (!APZThreadUtils::IsControllerThreadAlive()) {
    return;
  }
  APZThreadUtils::AssertOnControllerThread();

  INPQ_LOG("got a maybe-long-tap timeout; block=%" PRIu64 "\n", aInputBlockId);

  InputBlockState* inputBlock = FindBlockForId(aInputBlockId, nullptr);
  MOZ_ASSERT(!inputBlock || inputBlock->AsTouchBlock());
  if (inputBlock && bool(inputBlock->AsTouchBlock()->IsInSlop())) {
    // If the block is still in slop, it won't have sent a touchmove to content
    // and so content will not have sent a content response. But also it means
    // the touchstart should trigger a long-press gesture so let's force the
    // block to get processed now.
    MainThreadTimeout(aInputBlockId);
  }
}

void InputQueue::ContentReceivedInputBlock(uint64_t aInputBlockId,
                                           bool aPreventDefault) {
  APZThreadUtils::AssertOnControllerThread();

  INPQ_LOG("got a content response; block=%" PRIu64 " preventDefault=%d\n",
           aInputBlockId, aPreventDefault);
  bool success = false;
  InputBlockState* inputBlock = FindBlockForId(aInputBlockId, nullptr);
  if (inputBlock && inputBlock->AsCancelableBlock()) {
    CancelableBlockState* block = inputBlock->AsCancelableBlock();
    success = block->SetContentResponse(aPreventDefault);
  } else if (inputBlock) {
    NS_WARNING("input block is not a cancelable block");
  } else {
    INPQ_LOG("couldn't find block=%" PRIu64 "\n", aInputBlockId);
  }
  if (success) {
    if (ProcessQueue()) {
      // If we've switched the active touch block back to the original touch
      // block from the block for long-tap, run ProcessQueue again.
      // If we haven't yet received new touch-move events which need to be
      // processed (e.g. we are waiting for a content response for a touch-move
      // event), below ProcessQueue call is mostly no-op.
      ProcessQueue();
    }
  }
}

void InputQueue::SetConfirmedTargetApzc(
    uint64_t aInputBlockId, const RefPtr<AsyncPanZoomController>& aTargetApzc) {
  APZThreadUtils::AssertOnControllerThread();

  INPQ_LOG("got a target apzc; block=%" PRIu64 " guid=%s\n", aInputBlockId,
           aTargetApzc ? ToString(aTargetApzc->GetGuid()).c_str() : "");
  bool success = false;
  InputQueueIterator firstInput;
  InputBlockState* inputBlock = FindBlockForId(aInputBlockId, &firstInput);
  if (inputBlock && inputBlock->AsCancelableBlock()) {
    CancelableBlockState* block = inputBlock->AsCancelableBlock();
    success = block->SetConfirmedTargetApzc(
        aTargetApzc, InputBlockState::TargetConfirmationState::eConfirmed,
        firstInput,
        // This actually could be a scrollbar drag, but we pass
        // aForScrollbarDrag=false because for scrollbar drags,
        // SetConfirmedTargetApzc() will also be called by ConfirmDragBlock(),
        // and we pass aForScrollbarDrag=true there.
        false);
  } else if (inputBlock) {
    NS_WARNING("input block is not a cancelable block");
  }
  if (success) {
    ProcessQueue();
  }
}

void InputQueue::ConfirmDragBlock(
    uint64_t aInputBlockId, const RefPtr<AsyncPanZoomController>& aTargetApzc,
    const AsyncDragMetrics& aDragMetrics) {
  APZThreadUtils::AssertOnControllerThread();

  INPQ_LOG("got a target apzc; block=%" PRIu64 " guid=%s dragtarget=%" PRIu64
           "\n",
           aInputBlockId,
           aTargetApzc ? ToString(aTargetApzc->GetGuid()).c_str() : "",
           aDragMetrics.mViewId);
  bool success = false;
  InputQueueIterator firstInput;
  InputBlockState* inputBlock = FindBlockForId(aInputBlockId, &firstInput);
  if (inputBlock && inputBlock->AsDragBlock()) {
    DragBlockState* block = inputBlock->AsDragBlock();

    // We use the target initial scrollable rect for updating the thumb position
    // during dragging the thumb even if the scrollable rect got expanded during
    // the drag.
    block->SetDragMetrics(aDragMetrics, aTargetApzc->GetScrollableRect());
    success = block->SetConfirmedTargetApzc(
        aTargetApzc, InputBlockState::TargetConfirmationState::eConfirmed,
        firstInput,
        /* aForScrollbarDrag = */ true);
  }
  if (success) {
    ProcessQueue();
  }
}

void InputQueue::SetAllowedTouchBehavior(
    uint64_t aInputBlockId, const nsTArray<TouchBehaviorFlags>& aBehaviors) {
  APZThreadUtils::AssertOnControllerThread();

  INPQ_LOG("got allowed touch behaviours; block=%" PRIu64 "\n", aInputBlockId);
  bool success = false;
  InputBlockState* inputBlock = FindBlockForId(aInputBlockId, nullptr);
  if (inputBlock && inputBlock->AsTouchBlock()) {
    TouchBlockState* block = inputBlock->AsTouchBlock();
    success = block->SetAllowedTouchBehaviors(aBehaviors);
  } else if (inputBlock) {
    NS_WARNING("input block is not a touch block");
  }
  if (success) {
    ProcessQueue();
  }
}

void InputQueue::SetBrowserGestureResponse(uint64_t aInputBlockId,
                                           BrowserGestureResponse aResponse) {
  if (auto* inputBlock = FindBlockForId(aInputBlockId, nullptr)) {
    if (auto* block = inputBlock->AsBrowserGesturableBlock()) {
      block->SetBrowserGestureResponse(aResponse);
    } else {
      NS_WARNING("input block should be browser gesturable here");
    }
  }
  ProcessQueue();
}

static APZHandledResult GetHandledResultFor(
    const AsyncPanZoomController* aApzc,
    const InputBlockState* aCurrentInputBlock, const InputData& aEvent) {
  if (aCurrentInputBlock->ShouldDropEvents()) {
    return APZHandledResult{APZHandledPlace::HandledByContent, aApzc};
  }

  // For the remainder of the function, we know the event was *not*
  // preventDefault()-ed, so we can pass DispatchToContent::No to helpers.

  if (!aApzc) {
    return APZHandledResult{APZHandledPlace::HandledByContent, aApzc};
  }

  Maybe<APZHandledResult> result =
      APZHandledResult::Initialize(aApzc, DispatchToContent::No);

  if (aEvent.mInputType == MULTITOUCH_INPUT) {
    PointerEventsConsumableFlags consumableFlags =
        aApzc->ArePointerEventsConsumable(aCurrentInputBlock->AsTouchBlock(),
                                          aEvent.AsMultiTouchInput());
    APZHandledResult::UpdateForTouchEvent(result, *aCurrentInputBlock,
                                          consumableFlags, aApzc,
                                          DispatchToContent::No);
  }
  // Initialize() and UpdateForTouchEvent() can only produce Nothing() in
  // case of aDispatchToContent=true.
  MOZ_RELEASE_ASSERT(result.isSome());
  return *result;
}

bool InputQueue::ProcessQueue() {
  APZThreadUtils::AssertOnControllerThread();

  while (!mQueuedInputs.IsEmpty()) {
    InputBlockState* curBlock = mQueuedInputs[0]->Block();
    CancelableBlockState* cancelable = curBlock->AsCancelableBlock();
    if (cancelable && !cancelable->IsReadyForHandling()) {
      if (MOZ_UNLIKELY(INPQ_LOG_TEST())) {
        nsAutoCString additionalLog;
        if (curBlock->AsTouchBlock()) {
          // touch
          additionalLog.AppendPrintf(
              "waiting-long-tap-result: %d allowed-touch-behaviors: %d "
              "waiting-browser-gesture-response: %d",
              curBlock->AsTouchBlock()->IsWaitingLongTapResult(),
              curBlock->AsTouchBlock()->HasAllowedTouchBehaviors(),
              curBlock->AsTouchBlock()->IsWaitingForBrowserGestureResponse());
        } else if (curBlock->AsPanGestureBlock()) {
          // pan gesture
          additionalLog.AppendPrintf(
              "waiting-browser-gesture-response: %d waiting-content-response: "
              "%d",
              curBlock->AsPanGestureBlock()
                  ->IsWaitingForBrowserGestureResponse(),
              curBlock->AsPanGestureBlock()->IsWaitingForContentResponse());
        } else if (curBlock->AsPinchGestureBlock()) {
          // pinch gesture
          additionalLog.AppendPrintf(
              "waiting-content-response: %d",
              curBlock->AsPinchGestureBlock()->IsWaitingForContentResponse());
        }

        INPQ_LOG(
            "skip processing %s block %p; target-confirmed: %d "
            "content-responded: %d content-response-expired: %d %s",
            cancelable->Type(), cancelable, cancelable->IsTargetConfirmed(),
            cancelable->HasContentResponded(),
            cancelable->IsContentResponseTimerExpired(), additionalLog.get());
      }
      break;
    }

    INPQ_LOG(
        "processing input from block %p; preventDefault %d shouldDropEvents %d "
        "target %p\n",
        curBlock, cancelable && cancelable->IsDefaultPrevented(),
        curBlock->ShouldDropEvents(), curBlock->GetTargetApzc().get());
    RefPtr<AsyncPanZoomController> target = curBlock->GetTargetApzc();

    // If there is an input block callback registered for this
    // input block, invoke it.
    //
    // NOTE: In the case where the block is a touch block and the block is not
    // ready to invoke the callback because of waiting a touch move response
    // from content, we skip the block.
    if (!curBlock->AsTouchBlock() ||
        curBlock->AsTouchBlock()->IsReadyForCallback()) {
      auto it = mInputBlockCallbacks.find(curBlock->GetBlockId());
      if (it != mInputBlockCallbacks.end()) {
        INPQ_LOG("invoking the callback for input from block %p id %" PRIu64
                 "\n",
                 curBlock, curBlock->GetBlockId());
        APZHandledResult handledResult =
            GetHandledResultFor(target, curBlock, *(mQueuedInputs[0]->Input()));
        it->second(curBlock->GetBlockId(), handledResult);
        // The callback is one-shot; discard it after calling it.
        mInputBlockCallbacks.erase(it);
      }
    }

    // target may be null here if the initial target was unconfirmed and then
    // we later got a confirmed null target. in that case drop the events.
    if (target) {
      // If the event is targeting a different APZC than the previous one,
      // we want to clear the previous APZC's gesture state regardless of
      // whether we're actually dispatching the event or not.
      if (mLastActiveApzc && mLastActiveApzc != target &&
          mTouchCounter.GetActiveTouchCount() > 0) {
        mLastActiveApzc->ResetTouchInputState();
      }
      if (TouchBlockState* touchBlock = curBlock->AsTouchBlock()) {
        touchBlock->UpdateTouchCounter(
            mQueuedInputs[0]->Input()->AsMultiTouchInput());
      }
      if (curBlock->ShouldDropEvents()) {
        if (curBlock->AsTouchBlock()) {
          target->ResetTouchInputState();
        } else if (curBlock->AsPanGestureBlock()) {
          target->ResetPanGestureInputState();
        }
      } else {
        UpdateActiveApzc(target);
        curBlock->DispatchEvent(*(mQueuedInputs[0]->Input()));
      }
    }
    mQueuedInputs.RemoveElementAt(0);
  }

  bool processQueueAgain = false;
  if (CanDiscardBlock(mActiveTouchBlock)) {
    const bool forLongTap = mActiveTouchBlock->ForLongTap();
    const bool wasDefaultPrevented = mActiveTouchBlock->IsDefaultPrevented();
    INPQ_LOG("discarding a touch block %p id %" PRIu64 "\n",
             mActiveTouchBlock.get(), mActiveTouchBlock->GetBlockId());
    mActiveTouchBlock = nullptr;
    MOZ_ASSERT_IF(forLongTap, mPrevActiveTouchBlock);
    if (forLongTap) {
      INPQ_LOG("switching back to the original touch block %p id %" PRIu64 "\n",
               mPrevActiveTouchBlock.get(),
               mPrevActiveTouchBlock->GetBlockId());

      mPrevActiveTouchBlock->SetLongTapProcessed();
      if (wasDefaultPrevented && !mPrevActiveTouchBlock->IsDefaultPrevented()) {
        // Take over the preventDefaulted info for the long-tap event (i.e. for
        // the contextmenu event) to the original touch block so that the
        // original touch block will never process incoming touch events.
        mPrevActiveTouchBlock->ResetContentResponseTimerExpired();
        mPrevActiveTouchBlock->SetContentResponse(true);
      }
      mActiveTouchBlock = mPrevActiveTouchBlock;
      mPrevActiveTouchBlock = nullptr;
      processQueueAgain = true;
    }
  }
  if (CanDiscardBlock(mActiveWheelBlock)) {
    mActiveWheelBlock = nullptr;
  }
  if (CanDiscardBlock(mActiveDragBlock)) {
    mActiveDragBlock = nullptr;
  }
  if (CanDiscardBlock(mActivePanGestureBlock)) {
    mActivePanGestureBlock = nullptr;
  }
  if (CanDiscardBlock(mActivePinchGestureBlock)) {
    mActivePinchGestureBlock = nullptr;
  }
  if (CanDiscardBlock(mActiveKeyboardBlock)) {
    mActiveKeyboardBlock = nullptr;
  }

  return processQueueAgain;
}

bool InputQueue::CanDiscardBlock(InputBlockState* aBlock) {
  if (!aBlock ||
      (aBlock->AsCancelableBlock() &&
       !aBlock->AsCancelableBlock()->IsReadyForHandling()) ||
      aBlock->MustStayActive()) {
    return false;
  }
  InputQueueIterator firstInput;
  FindBlockForId(aBlock->GetBlockId(), &firstInput);
  if (firstInput) {
    // The block has at least one input event still in the queue, so it's
    // not depleted
    return false;
  }
  return true;
}

void InputQueue::UpdateActiveApzc(
    const RefPtr<AsyncPanZoomController>& aNewActive) {
  mLastActiveApzc = aNewActive;
}

void InputQueue::Clear() {
  // On Android, where the controller thread is the Android UI thread,
  // it's possible for this to be called after the main thread has
  // already run the shutdown task that clears the state used to
  // implement APZThreadUtils::AssertOnControllerThread().
  // In such cases, we still want to perform the cleanup.
  if (APZThreadUtils::IsControllerThreadAlive()) {
    APZThreadUtils::AssertOnControllerThread();
  }

  mQueuedInputs.Clear();
  mActiveTouchBlock = nullptr;
  mPrevActiveTouchBlock = nullptr;
  mActiveWheelBlock = nullptr;
  mActiveDragBlock = nullptr;
  mActivePanGestureBlock = nullptr;
  mActivePinchGestureBlock = nullptr;
  mActiveKeyboardBlock = nullptr;
  mLastActiveApzc = nullptr;
}

InputQueue::AutoRunImmediateTimeout::AutoRunImmediateTimeout(InputQueue* aQueue)
    : mQueue(aQueue) {
  MOZ_ASSERT(!mQueue->mImmediateTimeout);
}

InputQueue::AutoRunImmediateTimeout::~AutoRunImmediateTimeout() {
  if (mQueue->mImmediateTimeout) {
    mQueue->mImmediateTimeout->Run();
    mQueue->mImmediateTimeout = nullptr;
  }
}

}  // namespace layers
}  // namespace mozilla
