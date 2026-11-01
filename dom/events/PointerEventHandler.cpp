/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PointerEventHandler.h"

#include "EventStateManager.h"
#include "PointerEvent.h"
#include "PointerLockManager.h"
#include "mozilla/ConnectedAncestorTracker.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "nsIContentInlines.h"
#include "nsIFrame.h"
#include "nsIWeakReferenceUtils.h"
#include "nsLayoutUtils.h"
#include "nsRFPService.h"
#include "nsUserCharacteristics.h"

namespace mozilla {
// MouseLocation logs the mouse location and when/where enqueued synthesized
// mouse move is flushed.  If you don't need all mouse location recording at
// eMouseMove, you can use MouseLocation:3,sync.  Then, it's logged once per
// 50 times.  Otherwise, if you need to log all eMouseMove locations, you can
// use MouseLocation:5,sync.
// Note that this is actually available only on debug builds for saving the
// runtime cost on opt builds.
LazyLogModule gLogMouseLocation("MouseLocation");
// PointerLocation logs all pointer locations and when/where enqueued
// synthesized pointer move is flushed.  If you don't need all pointer location
// recording at ePointerMove, you can use PointerLocation:3,sync.  Then, it's
// logged once per 50 times.  Otherwise, if you need to log all ePointerMove
// locations, you can use PointerLocation:5,sync.
// Note that this is actually available only on debug builds for saving the
// runtime cost on opt builds.
LazyLogModule gLogPointerLocation("PointerLocation");
// Log the updates of sActivePointersIds.
LazyLogModule gLogActivePointers("ActivePointers");

using namespace dom;

Maybe<int32_t> PointerEventHandler::sSpoofedPointerId;
StaticAutoPtr<PointerInfo> PointerEventHandler::sLastMouseInfo;
StaticRefPtr<nsIWeakReference> PointerEventHandler::sLastMousePresShell;
StaticRefPtr<nsIWeakReference> PointerEventHandler::sLastMouseWidget;
Maybe<uint32_t> PointerEventHandler::sLastMousePointerId;
Maybe<uint32_t> PointerEventHandler::sLastPointerId;

// Keeps a map between pointerId and element that currently capturing pointer
// with such pointerId. If pointerId is absent in this map then nobody is
// capturing it. Additionally keep information about pending capturing content.
static nsClassHashtable<nsUint32HashKey, PointerCaptureInfo>*
    sPointerCaptureList;

// Keeps information about pointers such as pointerId, activeState, pointerType,
// primaryState
static nsClassHashtable<nsUint32HashKey, PointerInfo>* sActivePointersIds;

const UniquePtr<PointerInfo>& PointerEventHandler::InsertOrUpdateActivePointer(
    uint32_t aPointerId, UniquePtr<PointerInfo>&& aNewPointerInfo,
    EventMessage aEventMessage, const char* aCallerName) {
  const bool logIt = [&]() {
    if (MOZ_LIKELY(!MOZ_LOG_TEST(gLogActivePointers, LogLevel::Info))) {
      return false;
    }
    const PointerInfo* prevPointerInfo = sActivePointersIds->Get(aPointerId);
    return !prevPointerInfo ||
           !prevPointerInfo->EqualsBasicPointerData(*aNewPointerInfo);
  }();

  const UniquePtr<PointerInfo>& pointerInfo =
      sActivePointersIds->InsertOrUpdate(aPointerId,
                                         std::move(aNewPointerInfo));
  if (MOZ_UNLIKELY(logIt)) {
    MOZ_LOG(
        gLogActivePointers, LogLevel::Info,
        ("InsertOrUpdate: { pointerId=%u, active: %s, inputSource: %s, "
         "primary: %s, fromTouchEvent: %s, synthesizedForTests: %s }, %s in "
         "%s",
         aPointerId, pointerInfo->mIsActive ? "Yes" : "No",
         InputSourceToString(pointerInfo->mInputSource).get(),
         pointerInfo->mIsPrimary ? "Yes" : "No",
         pointerInfo->mFromTouchEvent ? "Yes" : "No",
         pointerInfo->mIsSynthesizedForTests ? "Yes" : "No",
         ToChar(aEventMessage), aCallerName));
  }
  return pointerInfo;
}

void PointerEventHandler::RemoveActivePointer(uint32_t aPointerId,
                                              EventMessage aEventMessage,
                                              const char* aCallerName) {
  MOZ_ASSERT_IF(sLastPointerId, *sLastPointerId != aPointerId);

  sActivePointersIds->Remove(aPointerId);
  MOZ_LOG(
      gLogActivePointers, LogLevel::Info,
      ("Remove: { pointerId=%u }, %s in %s, remaining %u pointers", aPointerId,
       ToChar(aEventMessage), aCallerName, sActivePointersIds->Count()));
}

// Keeps track of which BrowserParent requested pointer capture for a pointer
// id.
static nsTHashMap<nsUint32HashKey, BrowserParent*>*
    sPointerCaptureRemoteTargetTable = nullptr;

// Keep the capturing element at dispatching the last pointer up event to
// consider the following click, auxclick or contextmenu event target.
static StaticRefPtr<nsIWeakReference>
    sPointerCapturingElementAtLastPointerUpEvent;

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
  sPointerCapturingElementAtLastPointerUpEvent = nullptr;
  if (sPointerCaptureRemoteTargetTable) {
    MOZ_ASSERT(XRE_IsParentProcess());
    delete sPointerCaptureRemoteTargetTable;
    sPointerCaptureRemoteTargetTable = nullptr;
  }
  sLastMouseInfo = nullptr;
  sLastMousePresShell = nullptr;
  sLastMouseWidget = nullptr;
  sLastMousePointerId.reset();
}

/* static */
bool PointerEventHandler::IsPointerEventImplicitCaptureForTouchEnabled() {
  return StaticPrefs::dom_w3c_pointer_events_implicit_capture();
}

/* static */
bool PointerEventHandler::ShouldDispatchClickEventOnCapturingElement(
    const WidgetGUIEvent* aSourceEvent /* = nullptr */) {
  if (!StaticPrefs::
          dom_w3c_pointer_events_dispatch_click_on_pointer_capturing_element()) {
    return false;
  }
  if (!aSourceEvent ||
      !StaticPrefs::
          dom_w3c_pointer_events_dispatch_click_on_pointer_capturing_element_except_touch()) {
    return true;
  }
  MOZ_ASSERT(aSourceEvent->mMessage == eMouseUp ||
             aSourceEvent->mMessage == ePointerUp ||
             aSourceEvent->mMessage == eTouchEnd);
  // Pointer Events defines that `click` event's userEvent is the preceding
  // `pointerup`.  However, Chrome does not follow treat it as so when the
  // `click` is caused by a tap.  For the compatibility with Chrome, we should
  // stop comforming to the spec until Chrome conforms to that.
  if (aSourceEvent->mClass == eTouchEventClass) {
    return false;
  }
  const WidgetMouseEvent* const sourceMouseEvent = aSourceEvent->AsMouseEvent();
  return sourceMouseEvent &&
         sourceMouseEvent->mInputSource != MouseEvent_Binding::MOZ_SOURCE_TOUCH;
}

/* static */
void PointerEventHandler::RecordPointerState(
    const nsPoint& aRefPoint, const WidgetMouseEvent& aMouseEvent) {
  MOZ_ASSERT_IF(aMouseEvent.mMessage == eMouseMove ||
                    aMouseEvent.mMessage == ePointerMove,
                aMouseEvent.IsReal());

  PointerInfo* pointerInfo = sActivePointersIds->Get(aMouseEvent.pointerId);
  if (!pointerInfo) {
    // If there is no pointer info (i.e., no last pointer state too) and the
    // input device is not stationary or the caller wants to clear the last
    // state, we need to do nothing.
    if (!aMouseEvent.InputSourceSupportsHover() ||
        aRefPoint == nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE)) {
      return;
    }
    // If there is no PointerInfo, we need to add an inactive PointeInfo to
    // store the state.
    pointerInfo = InsertOrUpdateActivePointer(
                      aMouseEvent.pointerId,
                      std::make_unique<PointerInfo>(
                          PointerInfo::Active::No, aMouseEvent.mInputSource,
                          PointerInfo::Primary::Yes,
                          PointerInfo::FromTouchEvent::No, nullptr, nullptr,
                          static_cast<PointerInfo::SynthesizeForTests>(
                              aMouseEvent.mFlags.mIsSynthesizedForTests)),
                      aMouseEvent.mMessage, __func__)
                      .get();
  }
  // If the input source is a stationary device and the point is defined, we may
  // need to dispatch synthesized ePointerMove at the pointer later.  So, in
  // that case, we should store the data.
  if (aMouseEvent.InputSourceSupportsHover() &&
      aRefPoint != nsPoint(NS_UNCONSTRAINEDSIZE, NS_UNCONSTRAINEDSIZE)) {
    pointerInfo->RecordLastState(aRefPoint, aMouseEvent);
#ifdef DEBUG
    if (MOZ_LOG_TEST(gLogPointerLocation, LogLevel::Info)) {
      static uint32_t sFrequentMessageCount = 0;
      const bool isFrequentMessage = aMouseEvent.mMessage == ePointerMove;
      if (!isFrequentMessage ||
          MOZ_LOG_TEST(gLogPointerLocation, LogLevel::Verbose) ||
          !(sFrequentMessageCount % 50)) {
        MOZ_LOG(
            gLogPointerLocation,
            isFrequentMessage ? LogLevel::Debug : LogLevel::Info,
            ("got %s on widget:%p at {%d, %d} (pointerId=%u, source=%s)\n",
             ToChar(aMouseEvent.mMessage), aMouseEvent.mWidget.get(),
             sLastMouseInfo->mLastRefPointInRootDoc.x,
             sLastMouseInfo->mLastRefPointInRootDoc.y, aMouseEvent.pointerId,
             InputSourceToString(aMouseEvent.mInputSource).get()));
      }
      if (isFrequentMessage) {
        sFrequentMessageCount++;
      } else {
        // Let's log the next ePointerMove after the other messages.
        sFrequentMessageCount = 0;
      }
    }
#endif  // #ifdef DEBUG
  }
  // Otherwise, i.e., if it's not a stationary device or the caller wants to
  // forget the point, we should clear the last position to abort to synthesize
  // ePointerMove.
  else {
    pointerInfo->ClearLastState();
    MOZ_LOG_DEBUG_ONLY(
        gLogPointerLocation, LogLevel::Info,
        ("got %s on widget:%p, pointer location is cleared (pointerId=%u, "
         "source=%s)\n",
         ToChar(aMouseEvent.mMessage), aMouseEvent.mWidget.get(),
         aMouseEvent.pointerId,
         InputSourceToString(aMouseEvent.mInputSource).get()));
  }
}

/* static */
void PointerEventHandler::RecordMouseState(
    PresShell& aRootPresShell, const WidgetMouseEvent& aMouseEvent) {
  MOZ_ASSERT(aRootPresShell.IsRoot());
  if (!sLastMouseInfo) {
    sLastMouseInfo = new PointerInfo();
  }
  sLastMousePresShell = do_GetWeakReference(&aRootPresShell);
  sLastMouseWidget = do_GetWeakReference(aMouseEvent.mWidget.get());
  sLastMousePointerId = Some(aMouseEvent.pointerId);
  sLastMouseInfo->mLastRefPointInRootDoc =
      aRootPresShell.GetEventLocation(aMouseEvent);
  sLastMouseInfo->mLastTargetGuid =
      layers::InputAPZContext::GetTargetLayerGuid();
  // FIXME: Don't trust the synthesized for tests flag of drag events.
  if (aMouseEvent.mClass != eDragEventClass) {
    // eMouseDown and eMouseUp may cause some mouse boundary events. So,
    // mIsActive should be set to the new state after handling the mouse
    // boundary events because when this is called, we have not dispatched any
    // mouse boundary events. After that, WillDispatchMouseEventToDOM() is
    // called and update mIsActive to the latest one.
    sLastMouseInfo->mIsActive = !!aMouseEvent.ComputeButtonsBeforeDispatch();
    sLastMouseInfo->mInputSource = aMouseEvent.mInputSource;
    sLastMouseInfo->mIsSynthesizedForTests =
        aMouseEvent.mFlags.mIsSynthesizedForTests;
  }
#ifdef DEBUG
  if (MOZ_LOG_TEST(gLogMouseLocation, LogLevel::Info)) {
    static uint32_t sFrequentMessageCount = 0;
    const bool isFrequentMessage =
        aMouseEvent.mMessage == eMouseMove || aMouseEvent.mMessage == eDragOver;
    if (!isFrequentMessage ||
        MOZ_LOG_TEST(gLogMouseLocation, LogLevel::Verbose) ||
        !(sFrequentMessageCount % 50)) {
      MOZ_LOG(
          gLogMouseLocation,
          isFrequentMessage ? LogLevel::Debug : LogLevel::Info,
          ("[ps=%p]got %s on widget:%p at {%d, %d} (pointerId=%u, source=%s)\n",
           &aRootPresShell, ToChar(aMouseEvent.mMessage),
           aMouseEvent.mWidget.get(), sLastMouseInfo->mLastRefPointInRootDoc.x,
           sLastMouseInfo->mLastRefPointInRootDoc.y, aMouseEvent.pointerId,
           InputSourceToString(aMouseEvent.mInputSource).get()));
    }
    if (isFrequentMessage) {
      sFrequentMessageCount++;
    } else {
      // Let's log the next eMouseMove or eDragOver after the other
      // messages.
      sFrequentMessageCount = 0;
    }
  }
#endif  // #ifdef DEBUG
}

/* static */
void PointerEventHandler::WillDispatchMouseEventToDOM(
    const WidgetMouseEvent& aMouseEvent) {
  if (!sLastMouseInfo) {
    return;
  }
  // aMouseEvent is going to be dispatched into the DOM. This means that the web
  // app can know the new state and we should handle things with the new state
  // starting from here. Therefore, we should update mIsActive right now.
  sLastMouseInfo->mIsActive = !!aMouseEvent.mButtons;
}

/* static */
void PointerEventHandler::ClearMouseState(PresShell& aRootPresShell,
                                          const WidgetMouseEvent& aMouseEvent) {
  MOZ_ASSERT(aRootPresShell.IsRoot());
  const RefPtr<PresShell> lastMousePresShell =
      do_QueryReferent(sLastMousePresShell);
  if (lastMousePresShell != &aRootPresShell) {
    return;
  }
  sLastMouseInfo->ClearLastState();
  sLastMouseInfo->mLastTargetGuid =
      layers::InputAPZContext::GetTargetLayerGuid();
  sLastMouseInfo->mInputSource = MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;
  sLastMouseInfo->mIsSynthesizedForTests =
      aMouseEvent.mFlags.mIsSynthesizedForTests;
  sLastMousePointerId.reset();
  MOZ_LOG_DEBUG_ONLY(gLogMouseLocation, LogLevel::Info,
                     ("[ps=%p]got %s on widget:%p, mouse location is cleared "
                      "(pointerId=%u, source=%s)\n",
                      &aRootPresShell, ToChar(aMouseEvent.mMessage),
                      aMouseEvent.mWidget.get(), aMouseEvent.pointerId,
                      InputSourceToString(aMouseEvent.mInputSource).get()));
}

/* static */
LazyLogModule& PointerEventHandler::MouseLocationLogRef() {
  return gLogMouseLocation;
}

/* static */
LazyLogModule& PointerEventHandler::PointerLocationLogRef() {
  return gLogPointerLocation;
}

/* static */
void PointerEventHandler::UpdatePointerActiveState(WidgetMouseEvent* aEvent,
                                                   nsIContent* aTargetContent) {
  if (!aEvent) {
    return;
  }
  switch (aEvent->mMessage) {
    case eMouseEnterIntoWidget: {
      const PointerInfo* const pointerInfo = GetPointerInfo(aEvent->pointerId);
      if (aEvent->mFlags.mIsSynthesizedForTests) {
        if (pointerInfo && !pointerInfo->mIsSynthesizedForTests) {
          // Do not overwrite the PointerInfo which is set by user input with
          // synthesized pointer move.
          return;
        }
      }

      // Update sLastPointerId so that the synthesized eMouseMove which
      // PresShell schedules in response to eMouseEnterIntoWidget can pass the
      // IsLastPointerId check in EventStateManager::UpdateCursor. Without this,
      // a fullscreen window swap (or any other widget-level event that fires
      // eMouseExitFromWidget followed by eMouseEnterIntoWidget while the
      // pointer hasn't actually moved) clears sLastPointerId and never
      // restores it, leaving the cursor stuck in whatever state it had before
      // the swap until the next real pointer event (bug 2031413).
      UpdateLastPointerId(aEvent->pointerId, aEvent->mMessage);

      // In this case we have to know information about available mouse pointers
      InsertOrUpdateActivePointer(
          aEvent->pointerId,
          std::make_unique<PointerInfo>(
              PointerInfo::Active::No, aEvent->mInputSource,
              PointerInfo::Primary::Yes, PointerInfo::FromTouchEvent::No,
              nullptr, pointerInfo,
              static_cast<PointerInfo::SynthesizeForTests>(
                  aEvent->mFlags.mIsSynthesizedForTests)),
          aEvent->mMessage, __func__);
      MaybeCacheSpoofedPointerID(aEvent->mInputSource, aEvent->pointerId);
      break;
    }
    case ePointerMove: {
      if (aEvent->IsReal()) {
        UpdateLastPointerId(aEvent->pointerId, aEvent->mMessage);
      }
      // If the event is a synthesized mouse event, we should register the
      // pointerId for the test if the pointer is not there.
      if (!aEvent->mFlags.mIsSynthesizedForTests ||
          aEvent->mInputSource != MouseEvent_Binding::MOZ_SOURCE_MOUSE) {
        return;
      }
      const PointerInfo* const pointerInfo = GetPointerInfo(aEvent->pointerId);
      if (pointerInfo) {
        return;
      }
      InsertOrUpdateActivePointer(
          aEvent->pointerId,
          std::make_unique<PointerInfo>(
              PointerInfo::Active::No, MouseEvent_Binding::MOZ_SOURCE_MOUSE,
              PointerInfo::Primary::Yes, PointerInfo::FromTouchEvent::No,
              nullptr, pointerInfo, PointerInfo::SynthesizeForTests::Yes),
          aEvent->mMessage, __func__);
      return;
    }
    case ePointerDown:
      UpdateLastPointerId(aEvent->pointerId, aEvent->mMessage);
      sPointerCapturingElementAtLastPointerUpEvent = nullptr;
      // In this case we switch pointer to active state
      if (WidgetPointerEvent* pointerEvent = aEvent->AsPointerEvent()) {
        // XXXedgar, test could possibly synthesize a mousedown event on a
        // coordinate outside the browser window and cause aTargetContent to be
        // nullptr, not sure if this also happens on real usage.
        InsertOrUpdateActivePointer(
            pointerEvent->pointerId,
            std::make_unique<PointerInfo>(
                PointerInfo::Active::Yes, *pointerEvent,
                aTargetContent ? aTargetContent->OwnerDoc() : nullptr,
                GetPointerInfo(aEvent->pointerId)),
            pointerEvent->mMessage, __func__);
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
          UpdateLastPointerId(aEvent->pointerId, aEvent->mMessage);
          InsertOrUpdateActivePointer(
              pointerEvent->pointerId,
              std::make_unique<PointerInfo>(PointerInfo::Active::No,
                                            *pointerEvent, nullptr,
                                            GetPointerInfo(aEvent->pointerId)),
              pointerEvent->mMessage, __func__);
        } else {
          MaybeForgetLastPointerId(aEvent->pointerId, aEvent->mMessage);
          // XXX If the PointerInfo is registered with same pointerId as actual
          // pointer and the event is synthesized for tests, we unregister the
          // pointer unexpectedly here.  However, it should be rare and
          // currently, we use only pointerId for the key.  Therefore, we cannot
          // do nothing without changing the key.
          RemoveActivePointer(aEvent->pointerId, aEvent->mMessage, __func__);
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
      MaybeForgetLastPointerId(aEvent->pointerId, aEvent->mMessage);
      // In this case we have to remove information about disappeared mouse
      // pointers
      RemoveActivePointer(aEvent->pointerId, aEvent->mMessage, __func__);
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
      entry.Insert(std::make_unique<PointerCaptureInfo>(aElement));
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

    (void)browserParent->SendReleaseAllPointerCapture();
    iter.Remove();
  }
}

/* static */
const PointerInfo* PointerEventHandler::GetPointerInfo(uint32_t aPointerId) {
  return sActivePointersIds->Get(aPointerId);
}

/* static */
const PointerInfo* PointerEventHandler::GetLastMouseInfo(
    const PresShell* aRootPresShell /* = nullptr */) {
  if (!sLastMousePresShell || !sLastMouseInfo) {
    return nullptr;
  }
  if (aRootPresShell) {
    const RefPtr<PresShell> lastMousePresShell =
        do_QueryReferent(sLastMousePresShell);
    if (lastMousePresShell != aRootPresShell) {
      return nullptr;
    }
  }
  return sLastMouseInfo;
}

/* static */
Maybe<uint32_t> PointerEventHandler::TryClaimOrphanedLastMouseInfo(
    PresShell& aRootPresShell) {
  MOZ_ASSERT(aRootPresShell.IsRoot());
  if (!sLastMouseInfo || !sLastMouseInfo->HasLastState() ||
      !sLastMousePointerId) {
    return Nothing();
  }
  // If another live PresShell still owns the state, leave it alone.
  if (sLastMousePresShell) {
    const RefPtr<PresShell> previousOwner =
        do_QueryReferent(sLastMousePresShell);
    if (previousOwner) {
      return Nothing();
    }
  }
  // The previous owner has been torn down. PresShell::IsRoot() only means
  // root of the in-process presContext tree, so the previous owner could
  // have been on a different top-level window than aRootPresShell. Only
  // claim the state if the widget that recorded it is the same as
  // aRootPresShell's widget; otherwise the cached coordinates would land
  // in the wrong window's root-frame space.
  if (!sLastMouseWidget) {
    return Nothing();
  }
  const nsCOMPtr<nsIWidget> previousWidget = do_QueryReferent(sLastMouseWidget);
  if (!previousWidget || previousWidget != aRootPresShell.GetOwnWidget()) {
    return Nothing();
  }
  // Rebind ownership to the new PresShell so subsequent calls to
  // GetLastMouseInfo(this) work.
  sLastMousePresShell = do_GetWeakReference(&aRootPresShell);
  return sLastMousePointerId;
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
  WidgetPointerEvent localEvent =
      WidgetPointerEvent::MakeCopyFromMouseEvent(*aEvent);
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

  if (!captureInfo ||
      captureInfo->mPendingElement == captureInfo->mOverrideElement) {
    return;
  }

  const RefPtr<Element> overrideElement = captureInfo->mOverrideElement;
  RefPtr<Element> pendingElement = captureInfo->mPendingElement;

  // Update captureInfo before dispatching event since sPointerCaptureList may
  // be changed in the pointer event listener.
  captureInfo->mOverrideElement = captureInfo->mPendingElement;
  if (captureInfo->Empty()) {
    sPointerCaptureList->Remove(aEvent->pointerId);
    captureInfo = nullptr;
  }

  if (overrideElement) {
    DispatchGotOrLostPointerCaptureEvent(/* aIsGotCapture */ false, aEvent,
                                         overrideElement);
    // A `lostpointercapture` event listener may have removed the new pointer
    // capture element from the tree.  Then, we shouldn't dispatch
    // `gotpointercapture` on the node.
    if (pendingElement && !pendingElement->IsInComposedDoc()) {
      // We won't dispatch `gotpointercapture`, so, we should never fire
      // `lostpointercapture` on it at processing the next pending pointer
      // capture.
      if ((captureInfo = GetPointerCaptureInfo(aEvent->pointerId)) &&
          captureInfo->mOverrideElement == pendingElement) {
        captureInfo->mOverrideElement = nullptr;
        if (captureInfo->Empty()) {
          sPointerCaptureList->Remove(aEvent->pointerId);
          captureInfo = nullptr;
        }
      }
      pendingElement = nullptr;
    } else {
      captureInfo = nullptr;  // Maybe destroyed
    }
  }
  if (pendingElement) {
    DispatchGotOrLostPointerCaptureEvent(/* aIsGotCapture */ true, aEvent,
                                         pendingElement);
    captureInfo = nullptr;  // Maybe destroyed
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
  widget->DispatchEvent(&event);
}

/* static */
void PointerEventHandler::ImplicitlyCapturePointer(nsIFrame* aFrame,
                                                   const WidgetEvent& aEvent) {
  MOZ_ASSERT(aEvent.mMessage == ePointerDown);
  if (!aFrame || !IsPointerEventImplicitCaptureForTouchEnabled()) {
    return;
  }
  const WidgetPointerEvent* pointerEvent = aEvent.AsPointerEvent();
  NS_WARNING_ASSERTION(pointerEvent,
                       "Call ImplicitlyCapturePointer with non-pointer event");
  if (!pointerEvent->mFromTouchEvent) {
    // We only implicitly capture the pointer for touch device.
    return;
  }
  nsIContent* target = aFrame->GetEventTargetContent(aEvent);
  if (NS_WARN_IF(!target) || NS_WARN_IF(!target->IsElement())) {
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
Element* PointerEventHandler::GetPendingPointerCapturingElement(
    uint32_t aPointerId) {
  PointerCaptureInfo* pointerCaptureInfo = GetPointerCaptureInfo(aPointerId);
  if (pointerCaptureInfo) {
    return pointerCaptureInfo->mPendingElement;
  }
  return nullptr;
}

/* static */
Element* PointerEventHandler::GetPointerCapturingElement(
    const WidgetGUIEvent* aEvent) {
  return GetPointerCapturingElementInternal(CapturingState::Override, aEvent);
}

/* static */
Element* PointerEventHandler::GetPendingPointerCapturingElement(
    const WidgetGUIEvent* aEvent) {
  return GetPointerCapturingElementInternal(CapturingState::Pending, aEvent);
}

/* static */
Element* PointerEventHandler::GetPointerCapturingElementInternal(
    CapturingState aCapturingState, const WidgetGUIEvent* aEvent) {
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

  const WidgetMouseEvent* const mouseEvent = aEvent->AsMouseEvent();
  if (!mouseEvent) {
    return nullptr;
  }
  return aCapturingState == CapturingState::Pending
             ? GetPendingPointerCapturingElement(mouseEvent->pointerId)
             : GetPointerCapturingElement(mouseEvent->pointerId);
}

/* static */
RefPtr<Element>
PointerEventHandler::GetPointerCapturingElementAtLastPointerUp() {
  return do_QueryReferent(sPointerCapturingElementAtLastPointerUpEvent);
}

void PointerEventHandler::ReleasePointerCapturingElementAtLastPointerUp() {
  sPointerCapturingElementAtLastPointerUpEvent = nullptr;
}

/* static */
void PointerEventHandler::SetPointerCapturingElementAtLastPointerUp(
    nsWeakPtr&& aPointerCapturingElement) {
  sPointerCapturingElementAtLastPointerUpEvent =
      aPointerCapturingElement.forget();
}

/* static */
void PointerEventHandler::ReleaseIfCaptureByDescendant(nsIContent* aContent) {
  MOZ_ASSERT(aContent);
  // We should check that aChild does not contain pointer capturing elements.
  // If it does we should release the pointer capture for the elements.
  if (!sPointerCaptureList->IsEmpty() && aContent->IsElement()) {
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
    PresShell* aPresShell, WidgetPointerEvent* aPointerEvent,
    WidgetGUIEvent* aMouseOrTouchEvent) {
  MOZ_ASSERT(aPresShell);

  if (!aPointerEvent->mIsPrimary || aPointerEvent->mMessage != ePointerDown ||
      !aPointerEvent->DefaultPreventedByContent()) {
    return;
  }
  PointerInfo* pointerInfo = nullptr;
  if (!sActivePointersIds->Get(aPointerEvent->pointerId, &pointerInfo) ||
      !pointerInfo) {
    // We already added the PointerInfo for active pointer when
    // PresShell::HandleEvent handling pointerdown event. However, PreShell can
    // be destroyed during handling of the pointerdown event, which causes the
    // PointerInfo to be removed.
    MOZ_ASSERT(aPresShell->IsDestroying(),
               "If we got ePointerDown w/o active pointer info, the PresShell "
               "should be destroying!!");
    return;
  }
  // PreventDefault only applied for active pointers.
  if (!pointerInfo->mIsActive) {
    return;
  }
  aMouseOrTouchEvent->PreventDefault(false);
  aMouseOrTouchEvent->mFlags.mOnlyChromeDispatch = true;
  pointerInfo->mPreventMouseEventByContent = true;
}

/* static */
void PointerEventHandler::InitPointerEventFromMouse(
    WidgetPointerEvent* aPointerEvent, const WidgetMouseEvent* aMouseEvent,
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
  int16_t button = aTouchEvent.mMessage == eTouchRawUpdate ||
                           aTouchEvent.mMessage == eTouchMove
                       ? MouseButton::eNotPressed
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
  aPointerEvent.mTilt = aTouch.mTilt;
  aPointerEvent.twist = aTouch.twist;
  aPointerEvent.mAngle = aTouch.mAngle;
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
  aCoalescedEvent.mLastRefPoint = aSourceEvent.mLastRefPoint;
  aCoalescedEvent.mModifiers = aSourceEvent.mModifiers;

  // WidgetMouseEventBase
  aCoalescedEvent.mButton = aSourceEvent.mButton;
  aCoalescedEvent.mButtons = aSourceEvent.mButtons;
  aCoalescedEvent.mPressure = aSourceEvent.mPressure;
  aCoalescedEvent.mInputSource = aSourceEvent.mInputSource;

  // pointerId, tiltX, tiltY, twist, tangentialPressure and convertToPointer.
  aCoalescedEvent.AssignPointerHelperData(aSourceEvent);

  // WidgetMouseEvent
  aCoalescedEvent.mMovement = aSourceEvent.mMovement;

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
    case eMouseRawUpdate:
    case eTouchRawUpdate:
      return ePointerRawUpdate;
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
bool PointerEventHandler::NeedToDispatchPointerRawUpdate(
    const Document* aDocument) {
  const nsPIDOMWindowInner* const innerWindow =
      aDocument ? aDocument->GetInnerWindow() : nullptr;
  return innerWindow && innerWindow->HasPointerRawUpdateEventListeners() &&
         innerWindow->IsSecureContext();
}

/* static */
nsresult PointerEventHandler::DispatchPointerEventWithTarget(
    EventMessage aPointerEventMessage,
    const WidgetMouseEvent& aMouseOrPointerEvent,
    const AutoWeakFrame& aTargetWeakFrame, nsIContent* aTargetContent,
    nsEventStatus* aStatus /* = nullptr */) {
  Maybe<WidgetPointerEvent> pointerEvent;
  if (aMouseOrPointerEvent.mClass == ePointerEventClass) {
    pointerEvent.emplace(aPointerEventMessage,
                         *aMouseOrPointerEvent.AsPointerEvent());
  } else {
    pointerEvent.emplace(
        WidgetPointerEvent::MakeCopyFromMouseEvent(aMouseOrPointerEvent));
    PointerEventHandler::InitPointerEventFromMouse(
        pointerEvent.ptr(), &aMouseOrPointerEvent, ePointerCancel);
  }
  pointerEvent->convertToPointer = false;

  return DispatchPointerEventWithTarget(pointerEvent.ref(), aTargetWeakFrame,
                                        aTargetContent, aStatus);
}

/* static */
nsresult PointerEventHandler::DispatchPointerEventWithTarget(
    EventMessage aPointerEventMessage, const WidgetTouchEvent& aTouchEvent,
    size_t aTouchIndex, const AutoWeakFrame& aTargetWeakFrame,
    nsIContent* aTargetContent, nsEventStatus* aStatus /* = nullptr */) {
  WidgetPointerEvent pointerEvent(aTouchEvent.IsTrusted(), aPointerEventMessage,
                                  aTouchEvent.mWidget);
  PointerEventHandler::InitPointerEventFromTouch(
      pointerEvent, aTouchEvent, *aTouchEvent.mTouches[aTouchIndex]);
  pointerEvent.convertToPointer = false;

  return DispatchPointerEventWithTarget(pointerEvent, aTargetWeakFrame,
                                        aTargetContent, aStatus);
}

/* static */
nsresult PointerEventHandler::DispatchPointerEventWithTarget(
    WidgetPointerEvent& aPointerEvent, const AutoWeakFrame& aTargetWeakFrame,
    nsIContent* aTargetContent, nsEventStatus* aStatus /* = nullptr */) {
  if (aStatus) {
    *aStatus = nsEventStatus_eIgnore;
  }

  AutoWeakFrame targetWeakFrame(aTargetWeakFrame);
  nsCOMPtr<nsIContent> targetContent = aTargetContent;
  if (targetWeakFrame) {
    MOZ_ASSERT_IF(
        targetContent,
        targetContent == targetWeakFrame->GetEventTargetContent(aPointerEvent));
    if (!targetContent) {
      targetContent = targetWeakFrame->GetEventTargetContent(aPointerEvent);
      if (NS_WARN_IF(!targetContent)) {
        return NS_ERROR_FAILURE;
      }
    }
  } else if (NS_WARN_IF(!targetContent)) {
    return NS_ERROR_FAILURE;
  }
  const RefPtr<PresShell> presShell =
      targetWeakFrame ? targetWeakFrame->PresShell()
                      : targetContent->OwnerDoc()->GetPresShell();
  if (NS_WARN_IF(!presShell)) {
    return NS_ERROR_FAILURE;
  }

  // If the event is not a gotpointercapture, lostpointercapture, click,
  // auxclick or contextmenu event, run the process pending pointer capture
  // steps for this PointerEvent.
  switch (aPointerEvent.mMessage) {
    case ePointerGotCapture:
    case ePointerLostCapture:
    case ePointerClick:
    case ePointerAuxClick:
    case eContextMenu:
      break;
    default: {
      Maybe<AutoConnectedAncestorTracker> trackTargetContent;
      if (targetContent->IsInComposedDoc()) {
        trackTargetContent.emplace(*targetContent);
      }
      CheckPointerCaptureState(&aPointerEvent);
      // If the event target was disconnected from the document, we should use
      // its connected ancestor as the target of aPointerEvent.
      if (trackTargetContent && trackTargetContent->ContentWasRemoved()) {
        MOZ_ASSERT(!targetWeakFrame);
        targetContent = trackTargetContent->GetConnectedContent();
        if (NS_WARN_IF(!targetContent)) {
          targetWeakFrame = nullptr;
          // FIXME: If we lost the target content with dispatching
          // ePointerGotCapture or ePointerLostCapture event, we may need to
          // retarget the pointer event to the Document.
          return NS_ERROR_FAILURE;
        }
      }
      break;
    }
  }

  // The active document of the pointerId and pointer capture for the pointerId
  // should be handled by EventStateManager::PreHandleEvent() immediately before
  // dispatching the event to the DOM.

  // Pointer boundary events should be handled by
  // EventStateManager::PreHandleEvent() too.

  nsEventStatus dummyStatus = nsEventStatus_eIgnore;
  nsresult rv = presShell->HandleEventWithTarget(
      &aPointerEvent, targetWeakFrame, targetContent,
      aStatus ? aStatus : &dummyStatus);

  // EventStateManager must have store the last `pointerover` target at handling
  // the pointer boundary events. So, we need to do nothing here.

  return rv;
}

/* static */
void PointerEventHandler::DispatchPointerFromMouseOrTouch(
    PresShell* aShell, nsIFrame* aEventTargetFrame,
    nsIContent* aEventTargetContent, Element* aPointerCapturingElement,
    WidgetGUIEvent* aMouseOrTouchEvent, bool aDontRetargetEvents,
    nsEventStatus* aStatus,
    nsIContent** aMouseOrTouchEventTarget /* = nullptr */) {
  MOZ_ASSERT(aEventTargetFrame || aEventTargetContent);
  MOZ_ASSERT(aMouseOrTouchEvent);

  nsWeakPtr pointerCapturingElementWeak =
      do_GetWeakReference(aPointerCapturingElement);
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
        bc->Top()->InRDMPane()) {
      return;
    }

    // If it is not mouse then it is likely will come as touch event.
    if (!mouseEvent->convertToPointer) {
      return;
    }

    // Normal synthesized mouse move events are marked as "not convert to
    // pointer" by PresShell::ProcessSynthMouseOrPointerMoveEvent().  However:
    // 1. if the event is synthesized via nsIDOMWindowUtils, it's not marked as
    // so because there is no synthesized pointer move dispatcher.  So, we need
    // to dispatch synthesized pointer move from here.  This path may be used by
    // mochitests which check the synthesized mouse/pointer boundary event
    // behavior.
    // 2. if the event comes from another process and our content will be moved
    // underneath the mouse cursor.  In this case, we should handle preceding
    // ePointerMove.
    // FIXME: In the latter case, we may need to synthesize ePointerMove for the
    // other pointers too.
    if (mouseEvent->IsSynthesized()) {
      if (!StaticPrefs::
              dom_event_pointer_boundary_dispatch_when_layout_change() ||
          !mouseEvent->InputSourceSupportsHover()) {
        return;
      }
      // So, if the pointer is captured, we don't need to dispatch pointer
      // boundary events since pointer boundary events should be fired before
      // gotpointercapture.
      PointerCaptureInfo* const captureInfo =
          GetPointerCaptureInfo(mouseEvent->pointerId);
      if (captureInfo && captureInfo->mOverrideElement) {
        return;
      }
    }

    pointerMessage = PointerEventHandler::ToPointerEventMessage(mouseEvent);
    if (pointerMessage == eVoidEvent) {
      return;
    }
#ifdef DEBUG
    if (pointerMessage == ePointerRawUpdate) {
      const nsIContent* const targetContent =
          aEventTargetContent ? aEventTargetContent
                              : aEventTargetFrame->GetContent();
      NS_ASSERTION(targetContent, "Where do we want to try to dispatch?");
      if (targetContent) {
        NS_ASSERTION(
            targetContent->IsInComposedDoc(),
            nsPrintfCString("Do we want to dispatch ePointerRawUpdate onto "
                            "disconnected content? (targetContent=%s)",
                            ToString(*targetContent).c_str())
                .get());
        if (!NeedToDispatchPointerRawUpdate(targetContent->OwnerDoc())) {
          NS_ASSERTION(
              false,
              nsPrintfCString(
                  "Did we fail to retarget the document? (targetContent=%s)",
                  ToString(*targetContent).c_str())
                  .get());
        }
      }
    }
#endif  // #ifdef DEBUG
    WidgetPointerEvent event =
        WidgetPointerEvent::MakeCopyFromMouseEvent(*mouseEvent);
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
    PostHandlePointerEventsPreventDefault(shell, &event, aMouseOrTouchEvent);
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
    // If the touch is a single tap release, we will dispatch click or auxclick
    // event later unless it's suppressed.  The event target should be the
    // pointer capturing element right now, i.e., at dispatching ePointerUp.
    // Although we cannot know whether the touch is a single tap here, we should
    // store the last touch pointer capturing element.  If this is not a single
    // tap end, the stored element will be ignored due to not dispatching click
    // nor auxclick.
    if (touchEvent->mMessage == eTouchEnd &&
        touchEvent->mTouches.Length() == 1) {
      MOZ_ASSERT(!pointerCapturingElementWeak);
      pointerCapturingElementWeak = do_GetWeakReference(
          GetPointerCapturingElement(touchEvent->mTouches[0]->Identifier()));
    }
    RefPtr<PresShell> shell(aShell);
    // PreHandlePointerEventsPreventDefault() and
    // PostHandlePointerEventsPreventDefault() can change touchEvent->mFlags
    // when they handle the pointer event of the primary touch. The pointer
    // events of the other touches must not get those flags.
    const BaseEventFlags touchEventFlags = touchEvent->mFlags;
    for (uint32_t i = 0; i < touchEvent->mTouches.Length(); ++i) {
      Touch* touch = touchEvent->mTouches[i];
      if (!TouchManager::ShouldConvertTouchToPointer(touch, touchEvent)) {
        continue;
      }

      WidgetPointerEvent event(touchEvent->IsTrusted(), pointerMessage,
                               touchEvent->mWidget);

      InitPointerEventFromTouch(event, *touchEvent, *touch);
      event.mFlags = touchEventFlags;
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

        nsIFrame* frame = nsLayoutUtils::GetPrimaryOrAreaImageFrame(content);
        shell = PresShell::GetShellForEventTarget(frame, content);
        if (!shell) {
          continue;
        }

        PreHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
        shell->HandleEventWithTarget(&event, frame, content, aStatus, true,
                                     aMouseOrTouchEventTarget);
        PostHandlePointerEventsPreventDefault(shell, &event,
                                              aMouseOrTouchEvent);
      } else {
        // We didn't hit test for other touch events. Spec doesn't mention that
        // all pointer events should be dispatched to the same target as their
        // corresponding touch events. Call PresShell::HandleEvent so that we do
        // hit test for pointer events.
        // FIXME: If aDontRetargetEvents is false and the event is fired on
        // different document, we cannot track the pointer event target when
        // it's removed from the tree.
        PreHandlePointerEventsPreventDefault(&event, aMouseOrTouchEvent);
        shell->HandleEvent(aEventTargetFrame, &event, aDontRetargetEvents,
                           aStatus);
        PostHandlePointerEventsPreventDefault(shell, &event,
                                              aMouseOrTouchEvent);
      }
    }
  }
  // If we dispatched an ePointerUp event while an element capturing the
  // pointer, we should keep storing it to consider click, auxclick and
  // contextmenu event target later.
  if (!aShell->IsDestroying() && pointerMessage == ePointerUp &&
      pointerCapturingElementWeak) {
    SetPointerCapturingElementAtLastPointerUp(
        std::move(pointerCapturingElementWeak));
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
  if (const RefPtr<Element> capturingElementAtLastPointerUp =
          GetPointerCapturingElementAtLastPointerUp()) {
    // The pointer capturing element may belong to different document from the
    // destroying nsPresContext. Check whether the composed document's
    // nsPresContext is the destroying one or not.
    if (capturingElementAtLastPointerUp->GetPresContext(
            Element::eForComposedDoc) == aPresContext) {
      ReleasePointerCapturingElementAtLastPointerUp();
    }
  }
  // Clean up active pointer info.
  // XXX: This was added primarily for touch input. Could this cause any
  // web-compat issue for mouse input in edge cases?
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
  if (aEvent.IsSynthesized()) {
    return false;
  }
  // And we should not start with raw update events, which should be used only
  // for notifying web apps of the pointer state changes ASAP.
  if (aEvent.mMessage == ePointerRawUpdate) {
    return false;
  }
  MOZ_ASSERT(aEvent.mMessage != eMouseRawUpdate);
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
    return pointerInfo->mInputSource;
  }
  return MouseEvent_Binding::MOZ_SOURCE_UNKNOWN;
}

/* static */
bool PointerEventHandler::GetPointerPrimaryState(uint32_t aPointerId) {
  PointerInfo* pointerInfo = nullptr;
  if (sActivePointersIds->Get(aPointerId, &pointerInfo) && pointerInfo) {
    return pointerInfo->mIsPrimary;
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
  // Don't allow uncomposed element to capture a pointer.
  if (NS_WARN_IF(aIsGotCapture && !aCaptureTarget->IsInComposedDoc())) {
    return;
  }
  const OwningNonNull<Document> targetDoc = *aCaptureTarget->OwnerDoc();
  const RefPtr<PresShell> presShell = targetDoc->GetPresShell();
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

  localEvent.AssignPointerEventData(*aPointerEvent, /* aCopyTargets */ true,
                                    /* aCopyCoalescedEvents */ false);
  DebugOnly<nsresult> rv = presShell->HandleEventWithTarget(
      &localEvent, nsLayoutUtils::GetPrimaryOrAreaImageFrame(aCaptureTarget),
      aCaptureTarget, &status);

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

void PointerEventHandler::UpdateLastPointerId(uint32_t aPointerId,
                                              EventMessage aEventMessage) {
  if (sLastPointerId && *sLastPointerId == aPointerId) {
    return;
  }
  MOZ_LOG_DEBUG_ONLY(
      EventStateManager::MouseCursorUpdateLogRef(), LogLevel::Info,
      ("PointerEventHandler::UpdateLastPointerId(): "
       "Last pointerId (%s) is changed to %u when %s",
       ToString(sLastPointerId).c_str(), aPointerId, ToChar(aEventMessage)));
  sLastPointerId = Some(aPointerId);
}

void PointerEventHandler::MaybeForgetLastPointerId(uint32_t aPointerId,
                                                   EventMessage aEventMessage) {
  if (!sLastPointerId || *sLastPointerId != aPointerId) {
    return;
  }
  sLastPointerId.reset();
  MOZ_LOG_DEBUG_ONLY(EventStateManager::MouseCursorUpdateLogRef(),
                     LogLevel::Info,
                     ("PointerEventHandler::MaybeForgetLastPointerId(): "
                      "Last pointerId (%u) is changed to Nothing when %s",
                      aPointerId, ToChar(aEventMessage)));
}

}  // namespace mozilla
