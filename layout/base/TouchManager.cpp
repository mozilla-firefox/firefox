/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "TouchManager.h"

#include "Units.h"
#include "mozilla/EventForwards.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_test.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/PointerEventHandler.h"
#include "mozilla/layers/InputAPZContext.h"
#include "nsIContent.h"
#include "nsIFrame.h"
#include "nsLayoutUtils.h"
#include "nsView.h"
#include "PositionedEventTargeting.h"

using namespace mozilla::dom;

namespace mozilla {

StaticAutoPtr<nsTHashMap<nsUint32HashKey, TouchManager::TouchInfo>>
    TouchManager::sCaptureTouchList;
layers::LayersId TouchManager::sCaptureTouchLayersId;
TimeStamp TouchManager::sSingleTouchStartTimeStamp;
LayoutDeviceIntPoint TouchManager::sSingleTouchStartPoint;
bool TouchManager::sPrecedingTouchPointerDownConsumedByContent = false;

/*static*/
void TouchManager::InitializeStatics() {
  NS_ASSERTION(!sCaptureTouchList, "InitializeStatics called multiple times!");
  sCaptureTouchList = new nsTHashMap<nsUint32HashKey, TouchManager::TouchInfo>;
  sCaptureTouchLayersId = layers::LayersId{0};
}

/*static*/
void TouchManager::ReleaseStatics() {
  NS_ASSERTION(sCaptureTouchList, "ReleaseStatics called without Initialize!");
  sCaptureTouchList = nullptr;
}

void TouchManager::Init(PresShell* aPresShell, Document* aDocument) {
  mPresShell = aPresShell;
  mDocument = aDocument;
}

void TouchManager::Destroy() {
  EvictTouches(mDocument);
  mDocument = nullptr;
  mPresShell = nullptr;
}

static nsIContent* GetNonAnonymousAncestor(EventTarget* aTarget) {
  nsIContent* content = nsIContent::FromEventTargetOrNull(aTarget);
  if (content && content->IsInNativeAnonymousSubtree()) {
    content = content->FindFirstNonChromeOnlyAccessContent();
  }
  return content;
}

/*static*/
void TouchManager::EvictTouchPoint(RefPtr<Touch>& aTouch,
                                   Document* aLimitToDocument) {
  nsCOMPtr<nsINode> node(
      nsINode::FromEventTargetOrNull(aTouch->mOriginalTarget));
  if (node) {
    Document* doc = node->GetComposedDoc();
    if (doc && (!aLimitToDocument || aLimitToDocument == doc)) {
      PresShell* presShell = doc->GetPresShell();
      if (presShell) {
        nsIFrame* frame = presShell->GetRootFrame();
        if (frame) {
          nsCOMPtr<nsIWidget> widget =
              frame->GetView()->GetNearestWidget(nullptr);
          if (widget) {
            WidgetTouchEvent event(true, eTouchEnd, widget);
            event.mTouches.AppendElement(aTouch);
            nsEventStatus status;
            widget->DispatchEvent(&event, status);
          }
        }
      }
    }
  }
  if (!node || !aLimitToDocument || node->OwnerDoc() == aLimitToDocument) {
    sCaptureTouchList->Remove(aTouch->Identifier());
  }
}

/*static*/
void TouchManager::AppendToTouchList(
    WidgetTouchEvent::TouchArrayBase* aTouchList) {
  for (const auto& data : sCaptureTouchList->Values()) {
    const RefPtr<Touch>& touch = data.mTouch;
    touch->mChanged = false;
    aTouchList->AppendElement(touch);
  }
}

void TouchManager::EvictTouches(Document* aLimitToDocument) {
  WidgetTouchEvent::AutoTouchArray touches;
  AppendToTouchList(&touches);
  for (uint32_t i = 0; i < touches.Length(); ++i) {
    EvictTouchPoint(touches[i], aLimitToDocument);
  }
  sCaptureTouchLayersId = layers::LayersId{0};
}

/* static */
nsIFrame* TouchManager::SetupTarget(WidgetTouchEvent* aEvent,
                                    nsIFrame* aFrame) {
  MOZ_ASSERT(aEvent);

  if (!aEvent || aEvent->mMessage != eTouchStart) {
    // All touch events except for touchstart use a captured target.
    return aFrame;
  }

  nsIFrame* target = aFrame;
  for (int32_t i = aEvent->mTouches.Length(); i;) {
    --i;
    dom::Touch* touch = aEvent->mTouches[i];

    int32_t id = touch->Identifier();
    if (!TouchManager::HasCapturedTouch(id)) {
      // find the target for this touch
      RelativeTo relativeTo{aFrame};
      nsPoint eventPoint = nsLayoutUtils::GetEventCoordinatesRelativeTo(
          aEvent, touch->mRefPoint, relativeTo);
      target = FindFrameTargetedByInputEvent(aEvent, relativeTo, eventPoint);
      if (target) {
        nsIContent* targetContent = target->GetContentForEvent(aEvent);
        touch->SetTouchTarget(targetContent
                                  ? targetContent->GetAsElementOrParentElement()
                                  : nullptr);
      } else {
        aEvent->mTouches.RemoveElementAt(i);
      }
    } else {
      // This touch is an old touch, we need to ensure that is not
      // marked as changed and set its target correctly
      touch->mChanged = false;
      RefPtr<dom::Touch> oldTouch = TouchManager::GetCapturedTouch(id);
      if (oldTouch) {
        touch->SetTouchTarget(oldTouch->mOriginalTarget);
      }
    }
  }
  return target;
}

/* static */
nsIFrame* TouchManager::SuppressInvalidPointsAndGetTargetedFrame(
    WidgetTouchEvent* aEvent) {
  MOZ_ASSERT(aEvent);

  if (!aEvent || aEvent->mMessage != eTouchStart) {
    // All touch events except for touchstart use a captured target.
    return nullptr;
  }

  // if this is a continuing session, ensure that all these events are
  // in the same document by taking the target of the events already in
  // the capture list
  nsCOMPtr<nsIContent> anyTarget;
  if (aEvent->mTouches.Length() > 1) {
    anyTarget = TouchManager::GetAnyCapturedTouchTarget();
  }

  nsIFrame* frame = nullptr;
  for (uint32_t i = aEvent->mTouches.Length(); i;) {
    --i;
    dom::Touch* touch = aEvent->mTouches[i];
    if (TouchManager::HasCapturedTouch(touch->Identifier())) {
      continue;
    }

    MOZ_ASSERT(touch->mOriginalTarget);
    nsIContent* const targetContent =
        nsIContent::FromEventTargetOrNull(touch->GetTarget());
    if (MOZ_UNLIKELY(!targetContent)) {
      touch->mIsTouchEventSuppressed = true;
      continue;
    }

    // Even if the target content is not connected, we should dispatch the touch
    // start event except when the target content is owned by different
    // document.
    if (MOZ_UNLIKELY(!targetContent->IsInComposedDoc())) {
      if (anyTarget && anyTarget->OwnerDoc() != targetContent->OwnerDoc()) {
        touch->mIsTouchEventSuppressed = true;
        continue;
      }
      if (!anyTarget) {
        anyTarget = targetContent;
      }
      touch->SetTouchTarget(targetContent->GetAsElementOrParentElement());
      if (PresShell* const presShell =
              targetContent->OwnerDoc()->GetPresShell()) {
        if (nsIFrame* rootFrame = presShell->GetRootFrame()) {
          frame = rootFrame;
        }
      }
      continue;
    }

    nsIFrame* targetFrame = targetContent->GetPrimaryFrame();
    if (targetFrame && !anyTarget) {
      anyTarget = targetContent;
    } else {
      nsIFrame* newTargetFrame = nullptr;
      for (nsIFrame* f = targetFrame; f;
           f = nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(f)) {
        if (f->PresContext()->Document() == anyTarget->OwnerDoc()) {
          newTargetFrame = f;
          break;
        }
        // We must be in a subdocument so jump directly to the root frame.
        // GetParentOrPlaceholderForCrossDoc gets called immediately to
        // jump up to the containing document.
        f = f->PresShell()->GetRootFrame();
      }
      // if we couldn't find a target frame in the same document as
      // anyTarget, remove the touch from the capture touch list, as
      // well as the event->mTouches array. touchmove events that aren't
      // in the captured touch list will be discarded
      if (!newTargetFrame) {
        touch->mIsTouchEventSuppressed = true;
      } else {
        targetFrame = newTargetFrame;
        nsIContent* newTargetContent = targetFrame->GetContentForEvent(aEvent);
        touch->SetTouchTarget(
            newTargetContent ? newTargetContent->GetAsElementOrParentElement()
                             : nullptr);
      }
    }
    if (targetFrame) {
      frame = targetFrame;
    }
  }
  return frame;
}

bool TouchManager::PreHandleEvent(WidgetEvent* aEvent, nsEventStatus* aStatus,
                                  bool& aTouchIsNew,
                                  nsCOMPtr<nsIContent>& aCurrentEventContent) {
  MOZ_DIAGNOSTIC_ASSERT(aEvent->IsTrusted());

  // NOTE: If you need to handle new event messages here, you need to add new
  //       cases in PresShell::EventHandler::PrepareToDispatchEvent().
  switch (aEvent->mMessage) {
    case eTouchStart: {
      WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();
      // if there is only one touch in this touchstart event, assume that it is
      // the start of a new touch session and evict any old touches in the
      // queue
      if (touchEvent->mTouches.Length() == 1) {
        EvictTouches();
        // Per
        // https://w3c.github.io/touch-events/#touchevent-implementer-s-note,
        // all touch event should be dispatched to the same document that first
        // touch event associated to. We cache layers id of the first touchstart
        // event, all subsequent touch events will use the same layers id.
        sCaptureTouchLayersId = aEvent->mLayersId;
        sSingleTouchStartTimeStamp = aEvent->mTimeStamp;
        sSingleTouchStartPoint = touchEvent->mTouches[0]->mRefPoint;
        const PointerInfo* pointerInfo = PointerEventHandler::GetPointerInfo(
            touchEvent->mTouches[0]->Identifier());
        sPrecedingTouchPointerDownConsumedByContent =
            pointerInfo && pointerInfo->mPreventMouseEventByContent;
      } else {
        touchEvent->mLayersId = sCaptureTouchLayersId;
        sSingleTouchStartTimeStamp = TimeStamp();
      }
      // Add any new touches to the queue
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      for (int32_t i = touches.Length(); i;) {
        --i;
        Touch* touch = touches[i];
        int32_t id = touch->Identifier();
        if (!sCaptureTouchList->Get(id, nullptr)) {
          // If it is not already in the queue, it is a new touch
          touch->mChanged = true;
        }
        touch->mMessage = aEvent->mMessage;
        TouchInfo info = {
            touch, GetNonAnonymousAncestor(touch->mOriginalTarget), true};
        sCaptureTouchList->InsertOrUpdate(id, info);
        if (touch->mIsTouchEventSuppressed) {
          // We're going to dispatch touch event. Remove this touch instance if
          // it is suppressed.
          touches.RemoveElementAt(i);
          continue;
        }
      }
      break;
    }
    case eTouchMove: {
      // Check for touches that changed. Mark them add to queue
      WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      touchEvent->mLayersId = sCaptureTouchLayersId;
      bool haveChanged = false;
      for (int32_t i = touches.Length(); i;) {
        --i;
        Touch* touch = touches[i];
        if (!touch) {
          continue;
        }
        int32_t id = touch->Identifier();
        touch->mMessage = aEvent->mMessage;

        TouchInfo info;
        if (!sCaptureTouchList->Get(id, &info)) {
          touches.RemoveElementAt(i);
          continue;
        }
        const RefPtr<Touch> oldTouch = info.mTouch;
        if (!oldTouch->Equals(touch)) {
          touch->mChanged = true;
          haveChanged = true;
        }

        nsCOMPtr<EventTarget> targetPtr = oldTouch->mOriginalTarget;
        if (!targetPtr) {
          touches.RemoveElementAt(i);
          continue;
        }
        nsCOMPtr<nsINode> targetNode(do_QueryInterface(targetPtr));
        if (!targetNode->IsInComposedDoc()) {
          targetPtr = info.mNonAnonymousTarget;
        }
        touch->SetTouchTarget(targetPtr);

        info.mTouch = touch;
        // info.mNonAnonymousTarget is still valid from above
        sCaptureTouchList->InsertOrUpdate(id, info);
        // if we're moving from touchstart to touchmove for this touch
        // we allow preventDefault to prevent mouse events
        if (oldTouch->mMessage != touch->mMessage) {
          aTouchIsNew = true;
        }
        if (oldTouch->mIsTouchEventSuppressed) {
          touch->mIsTouchEventSuppressed = true;
          touches.RemoveElementAt(i);
          continue;
        }
      }
      // is nothing has changed, we should just return
      if (!haveChanged) {
        if (aTouchIsNew) {
          // however, if this is the first touchmove after a touchstart,
          // it is special in that preventDefault is allowed on it, so
          // we must dispatch it to content even if nothing changed. we
          // arbitrarily pick the first touch point to be the "changed"
          // touch because firing an event with no changed events doesn't
          // work.
          for (uint32_t i = 0; i < touchEvent->mTouches.Length(); ++i) {
            if (touchEvent->mTouches[i]) {
              touchEvent->mTouches[i]->mChanged = true;
              break;
            }
          }
        } else {
          // This touch event isn't going to be dispatched on the main-thread,
          // we need to tell it to APZ because returned nsEventStatus is
          // unreliable to tell whether the event was preventDefaulted or not.
          layers::InputAPZContext::SetDropped();
          return false;
        }
      }
      break;
    }
    case eTouchEnd:
    case eTouchCancel: {
      // Remove the changed touches
      // need to make sure we only remove touches that are ending here
      WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      touchEvent->mLayersId = sCaptureTouchLayersId;
      for (int32_t i = touches.Length(); i;) {
        --i;
        Touch* touch = touches[i];
        if (!touch) {
          continue;
        }
        touch->mMessage = aEvent->mMessage;
        touch->mChanged = true;

        int32_t id = touch->Identifier();
        TouchInfo info;
        if (!sCaptureTouchList->Get(id, &info)) {
          continue;
        }
        nsCOMPtr<EventTarget> targetPtr = info.mTouch->mOriginalTarget;
        nsCOMPtr<nsINode> targetNode(do_QueryInterface(targetPtr));
        if (targetNode && !targetNode->IsInComposedDoc()) {
          targetPtr = info.mNonAnonymousTarget;
        }

        aCurrentEventContent = do_QueryInterface(targetPtr);
        touch->SetTouchTarget(targetPtr);
        sCaptureTouchList->Remove(id);
        if (info.mTouch->mIsTouchEventSuppressed) {
          touches.RemoveElementAt(i);
          continue;
        }
      }
      // add any touches left in the touch list, but ensure changed=false
      AppendToTouchList(&touches);
      break;
    }
    case eTouchPointerCancel: {
      // Don't generate pointer events by touch events after eTouchPointerCancel
      // is received.
      WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();
      WidgetTouchEvent::TouchArray& touches = touchEvent->mTouches;
      touchEvent->mLayersId = sCaptureTouchLayersId;
      for (uint32_t i = 0; i < touches.Length(); ++i) {
        Touch* touch = touches[i];
        if (!touch) {
          continue;
        }
        int32_t id = touch->Identifier();
        TouchInfo info;
        if (!sCaptureTouchList->Get(id, &info)) {
          continue;
        }
        info.mConvertToPointer = false;
        sCaptureTouchList->InsertOrUpdate(id, info);
      }
      break;
    }
    default:
      break;
  }
  return true;
}

void TouchManager::PostHandleEvent(const WidgetEvent* aEvent,
                                   const nsEventStatus* aStatus) {
  switch (aEvent->mMessage) {
    case eTouchMove: {
      if (sSingleTouchStartTimeStamp.IsNull()) {
        break;
      }
      if (*aStatus == nsEventStatus_eConsumeNoDefault) {
        sSingleTouchStartTimeStamp = TimeStamp();
        break;
      }
      const WidgetTouchEvent* touchEvent = aEvent->AsTouchEvent();
      if (touchEvent->mTouches.Length() > 1) {
        sSingleTouchStartTimeStamp = TimeStamp();
        break;
      }
      if (touchEvent->mTouches.Length() == 1) {
        // If the touch moved too far from the start point, don't treat the
        // touch as a tap.
        const float distance =
            static_cast<float>((sSingleTouchStartPoint -
                                aEvent->AsTouchEvent()->mTouches[0]->mRefPoint)
                                   .Length());
        const float maxDistance =
            StaticPrefs::apz_touch_start_tolerance() *
            (MOZ_LIKELY(touchEvent->mWidget) ? touchEvent->mWidget->GetDPI()
                                             : 96.0f);
        if (distance > maxDistance) {
          sSingleTouchStartTimeStamp = TimeStamp();
        }
      }
      break;
    }
    case eTouchStart:
    case eTouchEnd:
      if (*aStatus == nsEventStatus_eConsumeNoDefault &&
          !sSingleTouchStartTimeStamp.IsNull()) {
        sSingleTouchStartTimeStamp = TimeStamp();
      }
      break;
    case eTouchCancel:
    case eTouchPointerCancel:
    case eMouseLongTap:
    case eContextMenu: {
      if (!sSingleTouchStartTimeStamp.IsNull()) {
        sSingleTouchStartTimeStamp = TimeStamp();
      }
      break;
    }
    default:
      break;
  }
}

/*static*/
already_AddRefed<nsIContent> TouchManager::GetAnyCapturedTouchTarget() {
  nsCOMPtr<nsIContent> result = nullptr;
  if (sCaptureTouchList->Count() == 0) {
    return result.forget();
  }
  for (const auto& data : sCaptureTouchList->Values()) {
    const RefPtr<Touch>& touch = data.mTouch;
    if (touch) {
      EventTarget* target = touch->GetTarget();
      if (target) {
        result = nsIContent::FromEventTargetOrNull(target);
        break;
      }
    }
  }
  return result.forget();
}

/*static*/
bool TouchManager::HasCapturedTouch(int32_t aId) {
  return sCaptureTouchList->Contains(aId);
}

/*static*/
already_AddRefed<Touch> TouchManager::GetCapturedTouch(int32_t aId) {
  RefPtr<Touch> touch;
  TouchInfo info;
  if (sCaptureTouchList->Get(aId, &info)) {
    touch = info.mTouch;
  }
  return touch.forget();
}

/*static*/
bool TouchManager::ShouldConvertTouchToPointer(const Touch* aTouch,
                                               const WidgetTouchEvent* aEvent) {
  if (!aTouch || !aTouch->convertToPointer) {
    return false;
  }
  TouchInfo info;
  if (!sCaptureTouchList->Get(aTouch->Identifier(), &info)) {
    // This check runs before the TouchManager has the touch registered in its
    // touch list. It's because we dispatching pointer events before handling
    // touch events. So we convert eTouchStart to pointerdown even it's not
    // registered.
    // Check WidgetTouchEvent::mMessage because Touch::mMessage is assigned when
    // pre-handling touch events.
    return aEvent->mMessage == eTouchStart;
  }

  if (!info.mConvertToPointer) {
    return false;
  }

  switch (aEvent->mMessage) {
    case eTouchStart: {
      // We don't want to fire duplicated pointerdown.
      return false;
    }
    case eTouchMove: {
      return !aTouch->Equals(info.mTouch);
    }
    default:
      break;
  }
  return true;
}

/* static */
bool TouchManager::IsSingleTapEndToDoDefault(
    const WidgetTouchEvent* aTouchEndEvent) {
  MOZ_ASSERT(aTouchEndEvent);
  MOZ_ASSERT(aTouchEndEvent->mFlags.mIsSynthesizedForTests);
  MOZ_ASSERT(!StaticPrefs::test_events_async_enabled());
  if (sSingleTouchStartTimeStamp.IsNull() ||
      aTouchEndEvent->mTouches.Length() != 1) {
    return false;
  }
  // If it's pressed long time, we should not treat it as a single tap because
  // a long press should cause opening context menu by default.
  if ((aTouchEndEvent->mTimeStamp - sSingleTouchStartTimeStamp)
          .ToMilliseconds() > StaticPrefs::apz_max_tap_time()) {
    return false;
  }
  NS_WARNING_ASSERTION(aTouchEndEvent->mTouches[0]->mChanged,
                       "The single tap end should be changed");
  return true;
}

/* static */
bool TouchManager::IsPrecedingTouchPointerDownConsumedByContent() {
  return sPrecedingTouchPointerDownConsumedByContent;
}

}  // namespace mozilla
