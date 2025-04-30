/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PointerEventHandler.h"
#include "mozilla/EventForwards.h"
#include "nsIContentInlines.h"
#include "nsIFrame.h"
#include "PointerEvent.h"
#include "PointerLockManager.h"
#include "nsRFPService.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "nsUserCharacteristics.h"

namespace mozilla {

using namespace dom;

Maybe<int32_t> PointerEventHandler::sSpoofedPointerId;

// Keeps a map between pointerId and element that currently capturing pointer
// with such pointerId. If pointerId is absent in this map then nobody is
// capturing it. Additionally keep information about pending capturing content.
static nsClassHashtable<nsUint32HashKey, PointerCaptureInfo>*
    sPointerCaptureList;

// Keeps information about pointers such as pointerId, activeState, pointerType,
// primaryState
static nsClassHashtable<nsUint32HashKey, PointerInfo>* sActivePointersIds;

// Keeps track of which BrowserParent requested pointer capture for a pointer
// id.
static nsTHashMap<nsUint32HashKey, BrowserParent*>*
    sPointerCaptureRemoteTargetTable = nullptr;

/* static */
void PointerEventHandler::InitializeStatics() {
  MOZ_ASSERT(!sPointerCaptureList, "InitializeStatics called multiple times!");
  sPointerCaptureList =
      new nsClassHashtable<nsUint32HashKey, PointerCaptureInfo>;
  sActivePointersIds = new nsClassHashtable<nsUint32HashKey, PointerInfo>;
  if (XRE_IsParentProcess()) {
    sPointerCaptureRemoteTargetTable =
        new nsTHashMap<nsUint32HashKey, BrowserParent*>;
  }
}

/* static */
void PointerEventHandler::ReleaseStatics() {
  MOZ_ASSERT(sPointerCaptureList, "ReleaseStatics called without Initialize!");
  delete sPointerCaptureList;
  sPointerCaptureList = nullptr;
  delete sActivePointersIds;
  sActivePointersIds = nullptr;
  if (sPointerCaptureRemoteTargetTable) {
    MOZ_ASSERT(XRE_IsParentProcess());
    delete sPointerCaptureRemoteTargetTable;
    sPointerCaptureRemoteTargetTable = nullptr;
  }
}

/* static */
bool PointerEventHandler::IsPointerEventImplicitCaptureForTouchEnabled() {
  return StaticPrefs::dom_w3c_pointer_events_implicit_capture();
}

/* static */
void PointerEventHandler::UpdateActivePointerState(WidgetMouseEvent* aEvent,
                                                   nsIContent* aTargetContent) {
  if (!aEvent) {
    return;
  }
  switch (aEvent->mMessage) {
    case eMouseEnterIntoWidget:
      if (aEvent->mFlags.mIsSynthesizedForTests) {
        const PointerInfo* const pointerInfo =
            GetPointerInfo(aEvent->pointerId);
        if (pointerInfo && !pointerInfo->mIsSynthesizedForTests) {
          // Do not overwrite the PointerInfo which is set by user input with
          // synthesized pointer move.
          return;
        }
      }
      // In this case we have to know information about available mouse pointers
      sActivePointersIds->InsertOrUpdate(
          aEvent->pointerId,
          MakeUnique<PointerInfo>(
              false, aEvent->mInputSource, true, false, nullptr,
              // XXX build-linux64-base-toolchains (Bb) requires this hack.
              // NOLINTNEXTLINE
              aEvent->mFlags.mIsSynthesizedForTests != false));

      MaybeCacheSpoofedPointerID(aEvent->mInputSource, aEvent->pointerId);
      break;
    case ePointerMove:
      // If the event is a synthesized mouse event, we should register the
      // pointerId for the test if the pointer is not there.
      if (!aEvent->mFlags.mIsSynthesizedForTests ||
          aEvent->mInputSource != MouseEvent_Binding::MOZ_SOURCE_MOUSE) {
        return;
      }
      if (GetPointerInfo(aEvent->pointerId)) {
        return;
      }
      sActivePointersIds->InsertOrUpdate(
          aEvent->pointerId,
          MakeUnique<PointerInfo>(
              /* aActiveState = */ false, MouseEvent_Binding::MOZ_SOURCE_MOUSE,
              /* aPrimaryState = */ true, /* aFromTouchEvent = */ false,
              nullptr, /* aIsOnlySynthesizedForTests = */ true));
      return;
    case ePointerDown:
      // In this case we switch pointer to active state
      if (WidgetPointerEvent* pointerEvent = aEvent->AsPointerEvent()) {
        // XXXedgar, test could possibly synthesize a mousedown event on a
        // coordinate outside the browser window and cause aTargetContent to be
        // nullptr, not sure if this also happens on real usage.
        sActivePointersIds->InsertOrUpdate(
            pointerEvent->pointerId,
            MakeUnique<PointerInfo>(
                true, pointerEvent->mInputSource, pointerEvent->mIsPrimary,
                pointerEvent->mFromTouchEvent,
                aTargetContent ? aTargetContent->OwnerDoc() : nullptr,
                // XXX build-linux64-base-toolchains (Bb) requires this hack.
                // NOLINTNEXTLINE
                pointerEvent->mFlags.mIsSynthesizedForTests != false));
        MaybeCacheSpoofedPointerID(pointerEvent->mInputSource,
                                   pointerEvent->pointerId);
      }
      break;
    case ePointerCancel:
      // pointercancel means a pointer is unlikely to continue to produce
      // pointer events. In that case, we should turn off active state or remove
      // the pointer from active pointers.
    case ePointerUp:
      // In this case we remove information about pointer or turn off active
      // state
      if (WidgetPointerEvent* pointerEvent = aEvent->AsPointerEvent()) {
        if (pointerEvent->mInputSource !=
            MouseEvent_Binding::MOZ_SOURCE_TOUCH) {
          sActivePointersIds->InsertOrUpdate(
              pointerEvent->pointerId,
              MakeUnique<PointerInfo>(
                  false, pointerEvent->mInputSource, pointerEvent->mIsPrimary,
                  pointerEvent->mFromTouchEvent, nullptr,
                  // XXX build-linux64-base-toolchains (Bb) requires this hack.
                  // NOLINTNEXTLINE
                  pointerEvent->mFlags.mIsSynthesizedForTests != false));
        } else {
          // XXX If the PointerInfo is registered with same pointerId as actual
          // pointer and the event is synthesized for tests, we unregister the
          // pointer unexpectedly here.  However, it should be rare and
          // currently, we use only pointerId for the key.  Therefore, we cannot
          // do nothing without changing the key.
          sActivePointersIds->Remove(pointerEvent->pointerId);
        }
      }
      break;
    case eMouseExitFromWidget:
      if (aEvent->mFlags.mIsSynthesizedForTests) {
        const PointerInfo* const pointerInfo =
            GetPointerInfo(aEvent->pointerId);
        if (pointerInfo && !pointerInfo->mIsSynthesizedForTests) {
          // Do not remove the PointerInfo which is set by user input with
          // synthesized pointer move.
          return;
        }
      }
      // In this case we have to remove information about disappeared mouse
      // pointers
      sActivePointersIds->Remove(aEvent->pointerId);
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("event has invalid type");
      break;
  }
}

/* static */
void PointerEventHandler::RequestPointerCaptureById(uint32_t aPointerId,
                                                    Element* aElement) {
  SetPointerCaptureById(aPointerId, aElement);

  if (BrowserChild* browserChild =
          BrowserChild::GetFrom(aElement->OwnerDoc()->GetDocShell())) {
    browserChild->SendRequestPointerCapture(
        aPointerId,
        [aPointerId](bool aSuccess) {
          if (!aSuccess) {
            PointerEventHandler::ReleasePointerCaptureById(aPointerId);
          }
        },
        [](mozilla::ipc::ResponseRejectReason) {});
  }
}

/* static */
void PointerEventHandler::SetPointerCaptureById(uint32_t aPointerId,
                                                Element* aElement) {
  MOZ_ASSERT(aElement);
  sPointerCaptureList->WithEntryHandle(aPointerId, [&](auto&& entry) {
    if (entry) {
      entry.Data()->mPendingElement = aElement;
    } else {
      entry.Insert(MakeUnique<PointerCaptureInfo>(aElement));
    }
  });
}

/* static */
PointerCaptureInfo* PointerEventHandler::GetPointerCaptureInfo(
    uint32_t aPointerId) {
  PointerCaptureInfo* pointerCaptureInfo = nullptr;
  sPointerCaptureList->Get(aPointerId, &pointerCaptureInfo);
  return pointerCaptureInfo;
}

/* static */
void PointerEventHandler::ReleasePointerCaptureById(uint32_t aPointerId) {
  PointerCaptureInfo* pointerCaptureInfo = GetPointerCaptureInfo(aPointerId);
  if (pointerCaptureInfo) {
    if (Element* pendingElement = pointerCaptureInfo->mPendingElement) {
      if (BrowserChild* browserChild = BrowserChild::GetFrom(
              pendingElement->OwnerDoc()->GetDocShell())) {
        browserChild->SendReleasePointerCapture(aPointerId);
      }
    }
    pointerCaptureInfo->mPendingElement = nullptr;
  }
}

/* static */
void PointerEventHandler::ReleaseAllPointerCapture() {
  for (const auto& entry : *sPointerCaptureList) {
    PointerCaptureInfo* data = entry.GetWeak();
    if (data && data->mPendingElement) {
      ReleasePointerCaptureById(entry.GetKey());
    }
  }
}

/* static */
bool PointerEventHandler::SetPointerCaptureRemoteTarget(
    uint32_t aPointerId, dom::BrowserParent* aBrowserParent) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(sPointerCaptureRemoteTargetTable);
  MOZ_ASSERT(aBrowserParent);

  if (PointerLockManager::GetLockedRemoteTarget()) {
    return false;
  }

  BrowserParent* currentRemoteTarget =
      PointerEventHandler::GetPointerCapturingRemoteTarget(aPointerId);
  if (currentRemoteTarget && currentRemoteTarget != aBrowserParent) {
    return false;
  }

  sPointerCaptureRemoteTargetTable->InsertOrUpdate(aPointerId, aBrowserParent);
  return true;
}

/* static */
void PointerEventHandler::ReleasePointerCaptureRemoteTarget(
    BrowserParent* aBrowserParent) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(sPointerCaptureRemoteTargetTable);
  MOZ_ASSERT(aBrowserParent);

  sPointerCaptureRemoteTargetTable->RemoveIf([aBrowserParent](
                                                 const auto& iter) {
    BrowserParent* browserParent = iter.Data();
    MOZ_ASSERT(browserParent, "Null BrowserParent in pointer captured table?");

    return aBrowserParent == browserParent;
  });
}

/* static */
void PointerEventHandler::ReleasePointerCaptureRemoteTarget(
    uint32_t aPointerId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(sPointerCaptureRemoteTargetTable);

  sPointerCaptureRemoteTargetTable->Remove(aPointerId);
}

/* static */
BrowserParent* PointerEventHandler::GetPointerCapturingRemoteTarget(
    uint32_t aPointerId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(sPointerCaptureRemoteTargetTable);

  return sPointerCaptureRemoteTargetTable->Get(aPointerId);
}

/* static */
void PointerEventHandler::ReleaseAllPointerCaptureRemoteTarget() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(sPointerCaptureRemoteTargetTable);

  for (auto iter = sPointerCaptureRemoteTargetTable->Iter(); !iter.Done();
       iter.Next()) {
    BrowserParent* browserParent = iter.Data();
    MOZ_ASSERT(browserParent, "Null BrowserParent in pointer captured table?");

    Unused << browserParent->SendReleaseAllPointerCapture();
    iter.Remove();
  }
}

/* static */
const PointerInfo* PointerEventHandler::GetPointerInfo(uint32_t aPointerId) {
  return sActivePointersIds->Get(aPointerId);
}

/* static */
void PointerEventHandler::MaybeProcessPointerCapture(WidgetGUIEvent* aEvent) {
  switch (aEvent->mClass) {
    case eMouseEventClass:
      ProcessPointerCaptureForMouse(aEvent->AsMouseEvent());
      break;
    case eTouchEventClass:
      ProcessPointerCaptureForTouch(aEvent->AsTouchEvent());
      break;
    default:
      break;
  }
}

/* static */
void PointerEventHandler::ProcessPointerCaptureForMouse(
    WidgetMouseEvent* aEvent) {
  if (!ShouldGeneratePointerEventFromMouse(aEvent)) {
    return;
  }

  PointerCaptureInfo* info = GetPointerCaptureInfo(aEvent->pointerId);
  if (!info || info->mPendingElement == info->mOverrideElement) {
    return;
  }
  WidgetPointerEvent localEvent(*aEvent);
  InitPointerEventFromMouse(&localEvent, aEvent, eVoidEvent);
  CheckPointerCaptureState(&localEvent);
}

/* static */
void PointerEventHandler::ProcessPointerCaptureForTouch(
    WidgetTouchEvent* aEvent) {
  if (!ShouldGeneratePointerEventFromTouch(aEvent)) {
    return;
  }

  for (uint32_t i = 0; i < aEvent->mTouches.Length(); ++i) {
    Touch* touch = aEvent->mTouches[i];
    if (!TouchManager::ShouldConvertTouchToPointer(touch, aEvent)) {
      continue;
    }
    PointerCaptureInfo* info = GetPointerCaptureInfo(touch->Identifier());
    if (!info || info->mPendingElement == info->mOverrideElement) {
      continue;
    }
    WidgetPointerEvent event(aEvent->IsTrusted(), eVoidEvent, aEvent->mWidget);
    InitPointerEventFromTouch(event, *aEvent, *touch);
    CheckPointerCaptureState(&event);
  }
}

/* static */
void PointerEventHandler::CheckPointerCaptureState(WidgetPointerEvent* aEvent) {
  // Handle pending pointer capture before any pointer events except
  // gotpointercapture / lostpointercapture.
  if (!aEvent) {
    return;
  }
  MOZ_ASSERT(aEvent->mClass == ePointerEventClass);

  PointerCaptureInfo* captureInfo = GetPointerCaptureInfo(aEvent->pointerId);

  // When fingerprinting resistance is enabled, we need to map other pointer
  // ids into the spoofed one. We don't have to do the mapping if the capture
  // info exists for the non-spoofed pointer id because of we won't allow
  // content to set pointer capture other than the spoofed one. Thus, it must be
  // from chrome if the capture info exists in this case. And we don't have to
  // do anything if the pointer id is the same as the spoofed one.
  if (nsContentUtils::ShouldResistFingerprinting("Efficiency Check",
                                                 RFPTarget::PointerId) &&
      aEvent->pointerId != (uint32_t)GetSpoofedPointerIdForRFP() &&
      !captureInfo) {
    PointerCaptureInfo* spoofedCaptureInfo =
        GetPointerCaptureInfo(GetSpoofedPointerIdForRFP());

    // We need to check the target element's document should resist
    // fingerprinting. If not, we don't need to send a capture event
    // since the capture info of the original pointer id doesn't exist
    // in this case.
    if (!spoofedCaptureInfo || !spoofedCaptureInfo->mPendingElement ||
        !spoofedCaptureInfo->mPendingElement->OwnerDoc()
             ->ShouldResistFingerprinting(RFPTarget::PointerEvents)) {
      return;
    }

    captureInfo = spoofedCaptureInfo;
  }

  if (!captureInfo ||
      captureInfo->mPendingElement == captureInfo->mOverrideElement) {
    return;
  }

  RefPtr<Element> overrideElement = captureInfo->mOverrideElement;
  RefPtr<Element> pendingElement = captureInfo->mPendingElement;

  // Update captureInfo before dispatching event since sPointerCaptureList may
  // be changed in the pointer event listener.
  captureInfo->mOverrideElement = captureInfo->mPendingElement;
  if (captureInfo->Empty()) {
    sPointerCaptureList->Remove(aEvent->pointerId);
  }

  if (overrideElement) {
    DispatchGotOrLostPointerCaptureEvent(/* aIsGotCapture */ false, aEvent,
                                         overrideElement);
  }
  if (pendingElement) {
    DispatchGotOrLostPointerCaptureEvent(/* aIsGotCapture */ true, aEvent,
                                         pendingElement);
  }

  // If nobody captures the pointer and the pointer will not be removed, we need
  // to dispatch pointer boundary events if the pointer will keep hovering over
  // somewhere even after the pointer is up.
  // XXX Do we need to check whether there is new pending pointer capture
  // element? But if there is, what should we do?
  if (overrideElement && !pendingElement && aEvent->mWidget &&
      aEvent->mMessage != ePointerCancel &&
      (aEvent->mMessage != ePointerUp || aEvent->InputSourceSupportsHover())) {
    aEvent->mSynthesizeMoveAfterDispatch = true;
  }
}

/* static */
void PointerEventHandler::SynthesizeMoveToDispatchBoundaryEvents(
    const WidgetMouseEvent* aEvent) {
  nsCOMPtr<nsIWidget> widget = aEvent->mWidget;
  if (NS_WARN_IF(!widget)) {
    return;
  }
  Maybe<WidgetMouseEvent> mouseMoveEvent;
  Maybe<WidgetPointerEvent> pointerMoveEvent;
  if (aEvent->mClass == eMouseEventClass) {
    mouseMoveEvent.emplace(true, eMouseMove, aEvent->mWidget,
                           WidgetMouseEvent::eSynthesized);
  } else if (aEvent->mClass == ePointerEventClass) {
    pointerMoveEvent.emplace(true, ePointerMove, aEvent->mWidget);
    pointerMoveEvent->mReason = WidgetMouseEvent::eSynthesized;

    const WidgetPointerEvent* pointerEvent = aEvent->AsPointerEvent();
    MOZ_ASSERT(pointerEvent);
    pointerMoveEvent->mIsPrimary = pointerEvent->mIsPrimary;
    pointerMoveEvent->mFromTouchEvent = pointerEvent->mFromTouchEvent;
    pointerMoveEvent->mWidth = pointerEvent->mWidth;
    pointerMoveEvent->mHeight = pointerEvent->mHeight;
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "The event must be WidgetMouseEvent or WidgetPointerEvent");
  }
  WidgetMouseEvent& event =
      mouseMoveEvent ? mouseMoveEvent.ref() : pointerMoveEvent.ref();
  event.mFlags.mIsSynthesizedForTests = aEvent->mFlags.mIsSynthesizedForTests;
  event.mIgnoreCapturingContent = true;
  event.mRefPoint = aEvent->mRefPoint;
  event.mInputSource = aEvent->mInputSource;
  event.mButtons = aEvent->mButtons;
  event.mModifiers = aEvent->mModifiers;
  event.convertToPointer = false;
  event.AssignPointerHelperData(*aEvent);

  // XXX If the pointer is already over a document in different process, we
  // cannot synthesize the pointermove/mousemove on the document since
  // dispatching events to the parent process is currently allowed only in
  // automation.
  nsEventStatus eventStatus = nsEventStatus_eIgnore;
  widget->DispatchEvent(&event, eventStatus);
}

/* static */
void PointerEventHandler::ImplicitlyCapturePointer(nsIFrame* aFrame,
                                                   WidgetEvent* aEvent) {
  MOZ_ASSERT(aEvent->mMessage == ePointerDown);
  if (!aFrame || !IsPointerEventImplicitCaptureForTouchEnabled()) {
    return;
  }
  WidgetPointerEvent* pointerEvent = aEvent->AsPointerEvent();
  NS_WARNING_ASSERTION(pointerEvent,
                       "Call ImplicitlyCapturePointer with non-pointer event");
  if (!pointerEvent->mFromTouchEvent) {
    // We only implicitly capture the pointer for touch device.
    return;
  }
  nsIContent* target = aFrame->GetContentForEvent(aEvent);
  while (target && !target->IsElement()) {
    target = target->GetParent();
  }
  if (NS_WARN_IF(!target)) {
    return;
  }
  RequestPointerCaptureById(pointerEvent->pointerId, target->AsElement());
}

/* static */
void PointerEventHandler::ImplicitlyReleasePointerCapture(WidgetEvent* aEvent) {
  MOZ_ASSERT(aEvent);
  if (aEvent->mMessage != ePointerUp && aEvent->mMessage != ePointerCancel) {
    return;
  }
  WidgetPointerEvent* pointerEvent = aEvent->AsPointerEvent();
  ReleasePointerCaptureById(pointerEvent->pointerId);
  CheckPointerCaptureState(pointerEvent);
}

/* static */
void PointerEventHandler::MaybeImplicitlyReleasePointerCapture(
    WidgetGUIEvent* aEvent) {
  MOZ_ASSERT(aEvent);
  const EventMessage pointerEventMessage =
      PointerEventHandler::ToPointerEventMessage(aEvent);
  if (pointerEventMessage != ePointerUp &&
      pointerEventMessage != ePointerCancel) {
    return;
  }
  PointerEventHandler::MaybeProcessPointerCapture(aEvent);
}

/* static */
Element* PointerEventHandler::GetPointerCapturingElement(uint32_t aPointerId) {
  PointerCaptureInfo* pointerCaptureInfo = GetPointerCaptureInfo(aPointerId);
  if (pointerCaptureInfo) {
    return pointerCaptureInfo->mOverrideElement;
  }
  return nullptr;
}

/* static */
Element* PointerEventHandler::GetPointerCapturingElement(
    WidgetGUIEvent* aEvent) {
  if ((aEvent->mClass != ePointerEventClass &&
       aEvent->mClass != eMouseEventClass) ||
      aEvent->mMessage == ePointerDown || aEvent->mMessage == eMouseDown) {
    // Pointer capture should only be applied to all pointer events and mouse
    // events except ePointerDown and eMouseDown;
    return nullptr;
  }

  // PointerEventHandler may synthesize ePointerMove event before releasing the
  // mouse capture (it's done by a default handler of eMouseUp) after handling
  // ePointerUp.  Then, we need to dispatch pointer boundary events for the
  // element under the pointer to emulate a pointer move after a pointer
  // capture.  Therefore, we need to ignore the capturing element if the event
  // dispatcher requests it.
  if (aEvent->ShouldIgnoreCapturingContent()) {
    return nullptr;
  }

  WidgetMouseEvent* mouseEvent = aEvent->AsMouseEvent();
  if (!mouseEvent) {
    return nullptr;
  }
  return GetPointerCapturingElement(mouseEvent->pointerId);
}

/* static */
void PointerEventHandler::ReleaseIfCaptureByDescendant(nsIContent* aContent) {
  // We should check that aChild does not contain pointer capturing elements.
  // If it does we should release the pointer capture for the elements.
  if (!sPointerCaptureList->IsEmpty()) {
    for (const auto& entry : *sPointerCaptureList) {
      PointerCaptureInfo* data = entry.GetWeak();
      if (data && data->mPendingElement &&
          data->mPendingElement->IsInclusiveDescendantOf(aContent)) {
        ReleasePointerCaptureById(entry.GetKey());
      }
    }
  }
}

/* static */
void PointerEventHandler::PreHandlePointerEventsPreventDefault(
    WidgetPointerEvent* aPointerEvent, WidgetGUIEvent* aMouseOrTouchEvent) {
  if (!aPointerEvent->mIsPrimary || aPointerEvent->mMessage == ePointerDown) {
    return;
  }
  PointerInfo* pointerInfo = nullptr;
  if (!sActivePointersIds->Get(aPointerEvent->pointerId, &pointerInfo) ||
      !pointerInfo) {
    // The PointerInfo for active pointer should be added for normal cases. But
    // in some cases, we may receive mouse events before adding PointerInfo in
    // sActivePointersIds. (e.g. receive mousemove before
    // eMouseEnterIntoWidget). In these cases, we could ignore them because they
    // are not the events between a DefaultPrevented pointerdown and the
    // corresponding pointerup.
    return;
  }
  if (!pointerInfo->mPreventMouseEventByContent) {
    return;
  }
  aMouseOrTouchEvent->PreventDefault(false);
  aMouseOrTouchEvent->mFlags.mOnlyChromeDispatch = true;
  if (aPointerEvent->mMessage == ePointerUp) {
    pointerInfo->mPreventMouseEventByContent = false;
  }
}

/* static */
void PointerEventHandler::PostHandlePointerEventsPreventDefault(
    WidgetPointerEvent* aPointerEvent, WidgetGUIEvent* aMouseOrTouchEvent) {
  if (!aPointerEvent->mIsPrimary || aPointerEvent->mMessage != ePointerDown ||
      !aPointerEvent->DefaultPreventedByContent()) {
    return;
  }
  PointerInfo* pointerInfo = nullptr;
  if (!sActivePointersIds->Get(aPointerEvent->pointerId, &pointerInfo) ||
      !pointerInfo) {
    // We already added the PointerInfo for active pointer when
    // PresShell::HandleEvent handling pointerdown event.
#ifdef DEBUG
    MOZ_CRASH("Got ePointerDown w/o active pointer info!!");
#endif  // #ifdef DEBUG
    return;
  }
  // PreventDefault only applied for active pointers.
  if (!pointerInfo->mActiveState) {
    return;
  }
  aMouseOrTouchEvent->PreventDefault(false);
  aMouseOrTouchEvent->mFlags.mOnlyChromeDispatch = true;
  pointerInfo->mPreventMouseEventByContent = true;
}

/* static */
void PointerEventHandler::InitPointerEventFromMouse(
    WidgetPointerEvent* aPointerEvent, WidgetMouseEvent* aMouseEvent,
    EventMessage aMessage) {
  MOZ_ASSERT(aPointerEvent);
  MOZ_ASSERT(aMouseEvent);
  aPointerEvent->pointerId = aMouseEvent->pointerId;
  aPointerEvent->mInputSource = aMouseEvent->mInputSource;
  aPointerEvent->mMessage = aMessage;
  aPointerEvent->mButton = aMouseEvent->mMessage == eMouseMove
                               ? MouseButton::eNotPressed
                               : aMouseEvent->mButton;

  aPointerEvent->mButtons = aMouseEvent->mButtons;
  aPointerEvent->mPressure = aMouseEvent->ComputeMouseButtonPressure();
}

/* static */
void PointerEventHandler::InitPointerEventFromTouch(
    WidgetPointerEvent& aPointerEvent, const WidgetTouchEvent& aTouchEvent,
    const mozilla::dom::Touch& aTouch) {
  // Use mButton/mButtons only when mButton got a value (from pen input)
  int16_t button = aTouchEvent.mMessage == eTouchMove ? MouseButton::eNotPressed
                   : aTouchEvent.mButton != MouseButton::eNotPressed
                       ? aTouchEvent.mButton
                       : MouseButton::ePrimary;
  int16_t buttons = aTouchEvent.mMessage == eTouchEnd
                        ? MouseButtonsFlag::eNoButtons
                    : aTouchEvent.mButton != MouseButton::eNotPressed
                        ? aTouchEvent.mButtons
                        : MouseButtonsFlag::ePrimaryFlag;

  // XXX: This doesn't support multi pen scenario (bug 1904865)
  if (aTouchEvent.mInputSource == MouseEvent_Binding::MOZ_SOURCE_TOUCH) {
    // Only the first touch would be the primary pointer.
    aPointerEvent.mIsPrimary =
        aTouchEvent.mMessage == eTouchStart
            ? !HasActiveTouchPointer()
            : GetPointerPrimaryState(aTouch.Identifier());
  }
  aPointerEvent.pointerId = aTouch.Identifier();
  aPointerEvent.mRefPoint = aTouch.mRefPoint;
  aPointerEvent.mModifiers = aTouchEvent.mModifiers;
  aPointerEvent.mWidth = aTouch.RadiusX(CallerType::System);
  aPointerEvent.mHeight = aTouch.RadiusY(CallerType::System);
  aPointerEvent.tiltX = aTouch.tiltX;
  aPointerEvent.tiltY = aTouch.tiltY;
  aPointerEvent.twist = aTouch.twist;
  aPointerEvent.mTimeStamp = aTouchEvent.mTimeStamp;
  aPointerEvent.mFlags = aTouchEvent.mFlags;
  aPointerEvent.mButton = button;
  aPointerEvent.mButtons = buttons;
  aPointerEvent.mInputSource = aTouchEvent.mInputSource;
  aPointerEvent.mFromTouchEvent = true;
  aPointerEvent.mPressure = aTouch.mForce;
}

/* static */
void PointerEventHandler::InitCoalescedEventFromPointerEvent(
    WidgetPointerEvent& aCoalescedEvent,
    const WidgetPointerEvent& aSourceEvent) {
  aCoalescedEvent.mFlags.mCancelable = false;
  aCoalescedEvent.mFlags.mBubbles = false;

  aCoalescedEvent.mTimeStamp = aSourceEvent.mTimeStamp;
  aCoalescedEvent.mRefPoint = aSourceEvent.mRefPoint;
  aCoalescedEvent.mModifiers = aSourceEvent.mModifiers;

  // WidgetMouseEventBase
  aCoalescedEvent.mButton = aSourceEvent.mButton;
  aCoalescedEvent.mButtons = aSourceEvent.mButtons;
  aCoalescedEvent.mPressure = aSourceEvent.mPressure;
  aCoalescedEvent.mInputSource = aSourceEvent.mInputSource;

  // pointerId, tiltX, tiltY, twist, tangentialPressure and convertToPointer.
  aCoalescedEvent.AssignPointerHelperData(aSourceEvent);

  // WidgetPointerEvent
  aCoalescedEvent.mWidth = aSourceEvent.mWidth;
  aCoalescedEvent.mHeight = aSourceEvent.mHeight;
  aCoalescedEvent.mIsPrimary = aSourceEvent.mIsPrimary;
  aCoalescedEvent.mFromTouchEvent = aSourceEvent.mFromTouchEvent;
}

/* static */
EventMessage PointerEventHandler::ToPointerEventMessage(
    const WidgetGUIEvent* aMouseOrTouchEvent) {
  MOZ_ASSERT(aMouseOrTouchEvent);

  switch (aMouseOrTouchEvent->mMessage) {
    case eMouseMove:
      return ePointerMove;
    case eMouseUp:
      return aMouseOrTouchEvent->AsMouseEvent()->mButtons ? ePointerMove
                                                          : ePointerUp;
    case eMouseDown: {
      const WidgetMouseEvent* mouseEvent = aMouseOrTouchEvent->AsMouseEvent();
      return mouseEvent->mButtons & ~nsContentUtils::GetButtonsFlagForButton(
                                        mouseEvent->mButton)
                 ? ePointerMove
                 : ePointerDown;
    }
    case eTouchMove:
      return ePointerMove;
    case eTouchEnd:
      return ePointerUp;
    case eTouchStart:
      return ePointerDown;
    case eTouchCancel:
    case eTouchPointerCancel:
      return ePointerCancel;
    default:
      return eVoidEvent;
  }
}

/* static */
void PointerEventHandler::DispatchPointerFromMouseOrTouch(
    PresShell* aShell, nsIFrame* aEventTargetFrame,
    nsIContent* aEventTargetContent, WidgetGUIEvent* aMouseOrTouchEvent,
    bool aDontRetargetEvents, nsEventStatus* aStatus,
    nsIContent** aMouseOrTouchEventTarget /* = nullptr */) {
  MOZ_ASSERT(aEventTargetFrame || aEventTargetContent);
  MOZ_ASSERT(aMouseOrTouchEvent);

  EventMessage pointerMessage = eVoidEvent;
  if (aMouseOrTouchEvent->mClass == eMouseEventClass) {
    WidgetMouseEvent* mouseEvent = aMouseOrTouchEvent->AsMouseEvent();
    // Don't dispatch pointer events caused by a mouse when simulating touch
    // devices in RDM.
    Document* doc = aShell->GetDocument();
    if (!doc) {
      return;
    }

    BrowsingContext* bc = doc->GetBrowsingContext();
    if (bc && bc->TouchEventsOverride() == TouchEventsOverride::Enabled &&
        bc->InRDMPane()) {
      return;
    }

    // 1. If it is not mouse then it is likely will come as touch event
    // 2. We don't synthesize pointer events for those events that are not
    //    dispatched to DOM.
    if (!mouseEvent->convertToPointer ||
        !aMouseOrTouchEvent->IsAllowedToDispatchDOMEvent()) {
      return;
    }

    pointerMessage = PointerEventHandler::ToPointerEventMessage(mouseEvent);
    if (pointerMessage == eVoidEvent) {
      return;
    }
    WidgetPointerEvent event(*mouseEvent);
    InitPointerEventFromMouse(&event, mouseEvent, pointerMessage);
    event.convertToPointer = mouseEvent->convertToPointer = false;
    RefPtr<PresShell> shell(aShell);
    if (!aEventTargetFrame) {
      shell = PresShell::GetShellForEventTarget(nullptr, aEventTargetContent);
      if (!shell) {
        return;
      }
    }
    PreHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
    // Dispatch pointer event to the same target which is found by the
    // corresponding mouse event.
    shell->HandleEventWithTarget(&event, aEventTargetFrame, aEventTargetContent,
                                 aStatus, true, aMouseOrTouchEventTarget);
    PostHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
    // If pointer capture is released, we need to synthesize eMouseMove to
    // dispatch mouse boundary events later.
    mouseEvent->mSynthesizeMoveAfterDispatch |=
        event.mSynthesizeMoveAfterDispatch;
  } else if (aMouseOrTouchEvent->mClass == eTouchEventClass) {
    WidgetTouchEvent* touchEvent = aMouseOrTouchEvent->AsTouchEvent();
    // loop over all touches and dispatch pointer events on each touch
    // copy the event
    pointerMessage = PointerEventHandler::ToPointerEventMessage(touchEvent);
    if (pointerMessage == eVoidEvent) {
      return;
    }
    RefPtr<PresShell> shell(aShell);
    for (uint32_t i = 0; i < touchEvent->mTouches.Length(); ++i) {
      Touch* touch = touchEvent->mTouches[i];
      if (!TouchManager::ShouldConvertTouchToPointer(touch, touchEvent)) {
        continue;
      }

      WidgetPointerEvent event(touchEvent->IsTrusted(), pointerMessage,
                               touchEvent->mWidget);

      InitPointerEventFromTouch(event, *touchEvent, *touch);
      event.convertToPointer = touch->convertToPointer = false;
      event.mCoalescedWidgetEvents = touch->mCoalescedWidgetEvents;
      if (aMouseOrTouchEvent->mMessage == eTouchStart) {
        // We already did hit test for touchstart in PresShell. We should
        // dispatch pointerdown to the same target as touchstart.
        nsCOMPtr<nsIContent> content =
            nsIContent::FromEventTargetOrNull(touch->mTarget);
        if (!content) {
          continue;
        }

        nsIFrame* frame = content->GetPrimaryFrame();
        shell = PresShell::GetShellForEventTarget(frame, content);
        if (!shell) {
          continue;
        }

        PreHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
        shell->HandleEventWithTarget(&event, frame, content, aStatus, true,
                                     aMouseOrTouchEventTarget);
        PostHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
      } else {
        // We didn't hit test for other touch events. Spec doesn't mention that
        // all pointer events should be dispatched to the same target as their
        // corresponding touch events. Call PresShell::HandleEvent so that we do
        // hit test for pointer events.
        // FIXME: If aDontRetargetEvents is true and the event is fired on
        // different document, we cannot track the pointer event target when
        // it's removed from the tree.
        PreHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
        shell->HandleEvent(aEventTargetFrame, &event, aDontRetargetEvents,
                           aStatus);
        PostHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
      }
    }
  }
}

/* static */
void PointerEventHandler::NotifyDestroyPresContext(
    nsPresContext* aPresContext) {
  // Clean up pointer capture info
  for (auto iter = sPointerCaptureList->Iter(); !iter.Done(); iter.Next()) {
    PointerCaptureInfo* data = iter.UserData();
    MOZ_ASSERT(data, "how could we have a null PointerCaptureInfo here?");
    if (data->mPendingElement &&
        data->mPendingElement->GetPresContext(Element::eForComposedDoc) ==
            aPresContext) {
      data->mPendingElement = nullptr;
    }
    if (data->mOverrideElement &&
        data->mOverrideElement->GetPresContext(Element::eForComposedDoc) ==
            aPresContext) {
      data->mOverrideElement = nullptr;
    }
    if (data->Empty()) {
      iter.Remove();
    }
  }
  // Clean up active pointer info
  for (auto iter = sActivePointersIds->Iter(); !iter.Done(); iter.Next()) {
    PointerInfo* data = iter.UserData();
    MOZ_ASSERT(data, "how could we have a null PointerInfo here?");
    if (data->mActiveDocument &&
        data->mActiveDocument->GetPresContext() == aPresContext) {
      iter.Remove();
    }
  }
}

bool PointerEventHandler::IsDragAndDropEnabled(WidgetMouseEvent& aEvent) {
  // We shouldn't start a drag session if the event is synthesized one because
  // aEvent doesn't have enough information for initializing the ePointerCancel.
  if (!aEvent.IsReal()) {
    return false;
  }
#ifdef XP_WIN
  if (StaticPrefs::dom_w3c_pointer_events_dispatch_by_pointer_messages()) {
    // WM_POINTER does not support drag and drop, see bug 1692277
    return (aEvent.mInputSource != dom::MouseEvent_Binding::MOZ_SOURCE_PEN &&
            aEvent.mReason != WidgetMouseEvent::eSynthesized);  // bug 1692151
  }
#endif
  return true;
}

/* static */
uint16_t PointerEventHandler::GetPointerType(uint32_t aPointerId) {
  PointerInfo* pointerInfo = nullptr;
  if (sActivePointersIds->Get(aPointerId, &pointerInfo) && pointerInfo) {
    return pointerInfo->mPointerType;
  }
  return MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;
}

/* static */
bool PointerEventHandler::GetPointerPrimaryState(uint32_t aPointerId) {
  PointerInfo* pointerInfo = nullptr;
  if (sActivePointersIds->Get(aPointerId, &pointerInfo) && pointerInfo) {
    return pointerInfo->mPrimaryState;
  }
  return false;
}

/* static */
bool PointerEventHandler::HasActiveTouchPointer() {
  for (auto iter = sActivePointersIds->ConstIter(); !iter.Done(); iter.Next()) {
    if (iter.Data()->mFromTouchEvent) {
      return true;
    }
  }
  return false;
}

/* static */
void PointerEventHandler::DispatchGotOrLostPointerCaptureEvent(
    bool aIsGotCapture, const WidgetPointerEvent* aPointerEvent,
    Element* aCaptureTarget) {
  Document* targetDoc = aCaptureTarget->OwnerDoc();
  RefPtr<PresShell> presShell = targetDoc->GetPresShell();
  if (NS_WARN_IF(!presShell || presShell->IsDestroying())) {
    return;
  }

  if (!aIsGotCapture && !aCaptureTarget->IsInComposedDoc()) {
    // If the capturing element was removed from the DOM tree, fire
    // ePointerLostCapture at the document.
    PointerEventInit init;
    init.mPointerId = aPointerEvent->pointerId;
    init.mBubbles = true;
    init.mComposed = true;
    ConvertPointerTypeToString(aPointerEvent->mInputSource, init.mPointerType);
    init.mIsPrimary = aPointerEvent->mIsPrimary;
    RefPtr<PointerEvent> event;
    event = PointerEvent::Constructor(aCaptureTarget, u"lostpointercapture"_ns,
                                      init);
    targetDoc->DispatchEvent(*event);
    return;
  }
  nsEventStatus status = nsEventStatus_eIgnore;
  WidgetPointerEvent localEvent(
      aPointerEvent->IsTrusted(),
      aIsGotCapture ? ePointerGotCapture : ePointerLostCapture,
      aPointerEvent->mWidget);

  localEvent.AssignPointerEventData(*aPointerEvent, true);
  DebugOnly<nsresult> rv = presShell->HandleEventWithTarget(
      &localEvent, aCaptureTarget->GetPrimaryFrame(), aCaptureTarget, &status);

  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "DispatchGotOrLostPointerCaptureEvent failed");
}

/* static */
void PointerEventHandler::MaybeCacheSpoofedPointerID(uint16_t aInputSource,
                                                     uint32_t aPointerId) {
  if (sSpoofedPointerId.isSome() || aInputSource != SPOOFED_POINTER_INTERFACE) {
    return;
  }

  sSpoofedPointerId.emplace(aPointerId);
}

}  // namespace mozilla
