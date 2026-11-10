/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PerformanceEventTiming.h"

#include <algorithm>

#include "PerformanceInteractionMetrics.h"
#include "PerformanceMainThread.h"
#include "mozilla/EventForwards.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PerformanceEventTimingBinding.h"
#include "nsContentUtils.h"
#include "nsGkAtoms.h"
#include "nsIDocShell.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(PerformanceEventTiming, PerformanceEntry,
                                   mPerformance, mTarget)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PerformanceEventTiming)
NS_INTERFACE_MAP_END_INHERITING(PerformanceEntry)

NS_IMPL_ADDREF_INHERITED(PerformanceEventTiming, PerformanceEntry)
NS_IMPL_RELEASE_INHERITED(PerformanceEventTiming, PerformanceEntry)

PerformanceEventTiming::PerformanceEventTiming(Performance* aPerformance,
                                               const nsAString& aName,
                                               const TimeStamp& aStartTime,
                                               bool aIsCancelable,
                                               EventMessage aMessage)
    : PerformanceEntry(aPerformance->GetParentObject(), aName,
                       nsGkAtoms::event),
      mPerformance(aPerformance),
      mProcessingStart(aPerformance->NowUnclamped()),
      mProcessingEnd(0),
      mStartTime(
          aPerformance->GetDOMTiming()->TimeStampToDOMHighRes(aStartTime)),
      mCancelable(aIsCancelable),
      mMessage(aMessage) {}

PerformanceEventTiming::PerformanceEventTiming(
    const PerformanceEventTiming& aEventTimingEntry)
    : PerformanceEntry(aEventTimingEntry.mPerformance->GetParentObject(),
                       nsDependentAtomString(aEventTimingEntry.GetName()),
                       aEventTimingEntry.GetEntryTypeAsStaticAtom()),
      mPerformance(aEventTimingEntry.mPerformance),
      mProcessingStart(aEventTimingEntry.mProcessingStart),
      mProcessingEnd(aEventTimingEntry.mProcessingEnd),
      mTarget(aEventTimingEntry.mTarget),
      mStartTime(aEventTimingEntry.mStartTime),
      mDuration(aEventTimingEntry.mDuration),
      mCancelable(aEventTimingEntry.mCancelable),
      mInteractionId(aEventTimingEntry.mInteractionId),
      mFallbackTime(aEventTimingEntry.mFallbackTime),
      mMessage(aEventTimingEntry.mMessage) {}

JSObject* PerformanceEventTiming::WrapObject(
    JSContext* cx, JS::Handle<JSObject*> aGivenProto) {
  return PerformanceEventTiming_Binding::Wrap(cx, this, aGivenProto);
}

already_AddRefed<PerformanceEventTiming>
PerformanceEventTiming::TryGenerateEventTiming(const EventTarget* aTarget,
                                               const WidgetEvent* aEvent) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!StaticPrefs::dom_enable_event_timing() ||
      aEvent->mFlags.mOnlyChromeDispatch) {
    return nullptr;
  }

  if (!aEvent->IsTrusted()) {
    return nullptr;
  }

  switch (aEvent->mMessage) {
    case eContextMenu:
    case eMouseDoubleClick:
    case eMouseDown:
    case eMouseEnter:
    case eMouseLeave:
    case eMouseOut:
    case eMouseOver:
    case eMouseUp:
    case ePointerAuxClick:
    case ePointerClick:
    case ePointerOver:
    case ePointerEnter:
    case ePointerDown:
    case ePointerUp:
    case ePointerCancel:
    case ePointerOut:
    case ePointerLeave:
    case ePointerGotCapture:
    case ePointerLostCapture:
    case eTouchStart:
    case eTouchEnd:
    case eTouchCancel:
    case eKeyDown:
    case eKeyPress:
    case eKeyUp:
    case eEditorBeforeInput:
    case eEditorInput:
    case eCompositionStart:
    case eCompositionUpdate:
    case eCompositionEnd:
    case eDragStart:
    case eDragEnd:
    case eDragEnter:
    case eDragLeave:
    case eDragOver:
    case eDrop:
      break;
    default:
      return nullptr;
  }

  nsCOMPtr<nsPIDOMWindowInner> innerWindow =
      do_QueryInterface(aTarget->GetRelevantGlobal());
  if (!innerWindow) {
    return nullptr;
  }

  if (Performance* performance = innerWindow->GetPerformance()) {
    const char16_t* eventName = Event::GetEventName(aEvent->mMessage);
    MOZ_ASSERT(eventName,
               "User defined events shouldn't be considered as event timing");
    auto eventTiming =
        RefPtr<PerformanceEventTiming>(new PerformanceEventTiming(
            performance, nsDependentString(eventName), aEvent->mTimeStamp,
            aEvent->mFlags.mCancelable, aEvent->mMessage));
    performance->SetInteractionId(eventTiming, aEvent);
    return eventTiming.forget();
  }
  return nullptr;
}

bool PerformanceEventTiming::ShouldAddEntryToBuffer(double aDuration) const {
  if (GetEntryType() == nsGkAtoms::firstInput) {
    return true;
  }
  MOZ_ASSERT(GetEntryType() == nsGkAtoms::event);
  return RawDuration().isSome() && Duration() >= aDuration;
}

bool PerformanceEventTiming::ShouldAddEntryToObserverBuffer(
    PerformanceObserverInit& aOption) const {
  if (!PerformanceEntry::ShouldAddEntryToObserverBuffer(aOption)) {
    return false;
  }

  const double minDuration =
      aOption.mDurationThreshold.WasPassed()
          ? std::max(aOption.mDurationThreshold.Value(),
                     PerformanceMainThread::kDefaultEventTimingMinDuration)
          : PerformanceMainThread::kDefaultEventTimingDurationThreshold;

  return ShouldAddEntryToBuffer(minDuration);
}

void PerformanceEventTiming::BufferEntryIfNeeded() {
  if (ShouldAddEntryToBuffer(
          PerformanceMainThread::kDefaultEventTimingDurationThreshold)) {
    if (GetEntryType() != nsGkAtoms::firstInput) {
      MOZ_ASSERT(GetEntryType() == nsGkAtoms::event);
      mPerformance->BufferEventTimingEntryIfNeeded(this);
    }
  }
}

nsINode* PerformanceEventTiming::GetTarget() const {
  nsCOMPtr<Element> element = do_QueryReferent(mTarget);
  if (!element) {
    return nullptr;
  }

  nsCOMPtr<nsPIDOMWindowInner> global =
      do_QueryInterface(element->GetRelevantGlobal());
  if (!global) {
    return nullptr;
  }
  return nsContentUtils::GetAnElementForTiming(element, global->GetExtantDoc(),
                                               mPerformance->GetParentObject());
}

void PerformanceEventTiming::FinalizeEventTiming(const WidgetEvent* aEvent) {
  MOZ_ASSERT(aEvent);
  EventTarget* target = aEvent->mTarget;
  if (!target) {
    return;
  }
  nsCOMPtr<nsPIDOMWindowInner> global =
      do_QueryInterface(target->GetRelevantGlobal());
  if (!global) {
    return;
  }

  // If a modal dialog appeared before this event was dispatched (e.g. a keyup
  // queued during an alert), cap processingEnd at the dialog appearance time.
  DOMHighResTimeStamp lastModalFallback =
      mPerformance->GetLastModalFallbackTime();
  if (lastModalFallback > 0 && mStartTime < lastModalFallback) {
    SetFallbackTimeIfNotSet(lastModalFallback);
  }

  // Cap processingEnd at the fallback time if a modal dialog appeared during
  // event processing. The dialog provides visual feedback before processing
  // actually completes, so its appearance time is the effective processing end.
  // https://github.com/w3c/event-timing/issues/154
  mProcessingEnd = mFallbackTime.valueOr(mPerformance->NowUnclamped());

  Element* element = Element::FromEventTarget(target);
  if (!element || element->ChromeOnlyAccess()) {
    return;
  }

  mTarget = do_GetWeakReference(element);

  mPerformance->InsertEventTimingEntry(this);
}
}  // namespace mozilla::dom
