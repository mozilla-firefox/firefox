/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PerformanceMainThread.h"

#include <memory>

#include "LargestContentfulPaint.h"
#include "PerformanceContainerTiming.h"
#include "PerformanceEventTiming.h"
#include "PerformanceInteractionMetrics.h"
#include "PerformanceNavigation.h"
#include "PerformancePaintTiming.h"
#include "SharedLcpMarkerState.h"
#include "js/GCAPI.h"
#include "js/PropertyAndElement.h"  // JS_DefineProperty
#include "jsapi.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/PresShell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEvents.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventCounts.h"
#include "mozilla/dom/FragmentDirective.h"
#include "mozilla/dom/PerformanceEventTimingBinding.h"
#include "mozilla/dom/PerformanceNavigationTiming.h"
#include "mozilla/dom/PerformanceResourceTiming.h"
#include "mozilla/dom/PerformanceTiming.h"
#include "nsContainerFrame.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowInner.h"
#include "nsIChannel.h"
#include "nsIDocShell.h"
#include "nsIHttpChannel.h"
#include "nsRefreshDriver.h"

namespace mozilla::dom {

extern mozilla::LazyLogModule gLCPLogging;

namespace {

void GetURLSpecFromChannel(nsITimedChannel* aChannel, nsAString& aSpec) {
  aSpec.AssignLiteral("document");

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aChannel);
  if (!channel) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = channel->GetURI(getter_AddRefs(uri));
  if (NS_WARN_IF(NS_FAILED(rv)) || !uri) {
    return;
  }

  nsAutoCString spec;
  rv = FragmentDirective::GetSpecIgnoringFragmentDirective(uri, spec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  CopyUTF8toUTF16(spec, aSpec);
}

bool MatchesNameAndType(PerformanceEntry* aEntry, const nsAString& aName,
                        const Optional<nsAString>& aEntryType) {
  return aEntry && aEntry->GetName()->Equals(aName) &&
         (!aEntryType.WasPassed() ||
          aEntry->GetEntryType()->Equals(aEntryType.Value()));
}

}  // namespace

NS_IMPL_CYCLE_COLLECTION_CLASS(PerformanceMainThread)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(PerformanceMainThread,
                                                Performance)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(
      mTiming, mNavigation, mDocEntry, mFCPTiming, mEventTimingEntries,
      mLargestContentfulPaintEntries, mContainerTimingEntries, mFirstInputEvent,
      mPendingPointerDown, mPendingEventTimingEntries, mEventCounts,
      mInteractionMetrics, mCurrentEventTimingEntry, mPendingLCPCandidate)
  tmp->mTextFrameUnions.Clear();
  tmp->mContainerTimingRecords.Clear();
  mozilla::DropJSObjects(tmp);
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(PerformanceMainThread,
                                                  Performance)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
      mTiming, mNavigation, mDocEntry, mFCPTiming, mEventTimingEntries,
      mLargestContentfulPaintEntries, mContainerTimingEntries, mFirstInputEvent,
      mPendingPointerDown, mPendingEventTimingEntries, mEventCounts,
      mTextFrameUnions, mContainerTimingRecords, mInteractionMetrics,
      mCurrentEventTimingEntry, mPendingLCPCandidate)

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(PerformanceMainThread,
                                               Performance)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mMozMemory)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ADDREF_INHERITED(PerformanceMainThread, Performance)
NS_IMPL_RELEASE_INHERITED(PerformanceMainThread, Performance)

// QueryInterface implementation for PerformanceMainThread
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PerformanceMainThread)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, EventTarget)
NS_INTERFACE_MAP_END_INHERITING(Performance)

PerformanceMainThread::PerformanceMainThread(nsPIDOMWindowInner* aWindow,
                                             nsDOMNavigationTiming* aDOMTiming,
                                             nsITimedChannel* aChannel)
    : Performance(aWindow->AsGlobal()),
      mDOMTiming(aDOMTiming),
      mChannel(aChannel) {
  MOZ_ASSERT(aWindow, "Parent window object should be provided");
  if (StaticPrefs::dom_enable_event_timing()) {
    mEventCounts = new class EventCounts(GetParentObject());
  }
  CreateNavigationTimingEntry();

  if (StaticPrefs::dom_enable_largest_contentful_paint()) {
    nsGlobalWindowInner* owner = GetOwnerWindow();
    MarkerInnerWindowId innerWindowID =
        owner ? MarkerInnerWindowId(owner->WindowID())
              : MarkerInnerWindowId::NoId();
    // There might be multiple LCP entries and we only care about the latest one
    // which is also the biggest value. That's why we need to record these
    // markers in two different places:
    // - During the Document unload, so we can record the closed pages.
    // - During the profile capture, so we can record the open pages.
    // We are capturing the second one here.
    RefPtr<SharedLcpMarkerState> sharedLcpMarkerState =
        aDOMTiming->GetSharedLcpMarkerState();
    profiler_add_state_change_callback(
        // Using the "Pausing" state as "GeneratingProfile" profile happens too
        // late; we can not record markers if the profiler is already paused.
        ProfilingState::Pausing,
        [sharedLcpMarkerState = std::move(sharedLcpMarkerState),
         innerWindowID](ProfilingState aProfilingState) {
          sharedLcpMarkerState->MaybeAddLCPProfilerMarker(innerWindowID);
        },
        reinterpret_cast<uintptr_t>(this));
  }
}

PerformanceMainThread::~PerformanceMainThread() {
  profiler_remove_state_change_callback(reinterpret_cast<uintptr_t>(this));
  mozilla::DropJSObjects(this);
}

void PerformanceMainThread::GetMozMemory(JSContext* aCx,
                                         JS::MutableHandle<JSObject*> aObj) {
  if (!mMozMemory) {
    JS::Rooted<JSObject*> mozMemoryObj(aCx, JS_NewPlainObject(aCx));
    JS::Rooted<JSObject*> gcMemoryObj(aCx, js::gc::NewMemoryInfoObject(aCx));
    if (!mozMemoryObj || !gcMemoryObj) {
      MOZ_CRASH("out of memory creating performance.mozMemory");
    }
    if (!JS_DefineProperty(aCx, mozMemoryObj, "gc", gcMemoryObj,
                           JSPROP_ENUMERATE)) {
      MOZ_CRASH("out of memory creating performance.mozMemory");
    }
    mMozMemory = mozMemoryObj;
    mozilla::HoldJSObjects(this);
  }

  aObj.set(mMozMemory);
}

PerformanceTiming* PerformanceMainThread::Timing() {
  if (!mTiming) {
    // For navigation timing, the third argument (an nsIHttpChannel) is null
    // since the cross-domain redirect were already checked.  The last
    // argument (zero time) for performance.timing is the navigation start
    // value.
    mTiming = new PerformanceTiming(this, mChannel, nullptr,
                                    mDOMTiming->GetNavigationStart());
  }

  return mTiming;
}

void PerformanceMainThread::DispatchResourceTimingBufferFullEvent() {
  RefPtr<Event> event = NS_NewDOMEvent(this, nullptr, nullptr);
  event->InitEvent(u"resourcetimingbufferfull"_ns, false, false);
  event->SetTrusted(true);
  DispatchEvent(*event);
}

PerformanceNavigation* PerformanceMainThread::Navigation() {
  if (!mNavigation) {
    mNavigation = new PerformanceNavigation(this);
  }

  return mNavigation;
}

/**
 * An entry should be added only after the resource is loaded.
 * This method is not thread safe and can only be called on the main thread.
 */
void PerformanceMainThread::AddEntry(nsIHttpChannel* channel,
                                     nsITimedChannel* timedChannel) {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoString initiatorType;
  nsAutoString entryName;

  UniquePtr<PerformanceTimingData> performanceTimingData(
      PerformanceTimingData::Create(timedChannel, channel, 0, initiatorType,
                                    entryName));
  if (!performanceTimingData) {
    return;
  }
  AddRawEntry(std::move(performanceTimingData), initiatorType, entryName);
}

void PerformanceMainThread::AddEntry(const nsString& entryName,
                                     const nsString& initiatorType,
                                     UniquePtr<PerformanceTimingData>&& aData) {
  AddRawEntry(std::move(aData), initiatorType, entryName);
}

void PerformanceMainThread::AddRawEntry(UniquePtr<PerformanceTimingData> aData,
                                        const nsAString& aInitiatorType,
                                        const nsAString& aEntryName) {
  // The PerformanceResourceTiming object will use the PerformanceTimingData
  // object to get all the required timings.
  auto entry =
      MakeRefPtr<PerformanceResourceTiming>(std::move(aData), this, aEntryName);
  entry->SetInitiatorType(aInitiatorType);
  InsertResourceEntry(entry);
}

void PerformanceMainThread::SetFCPTimingEntry(PerformancePaintTiming* aEntry) {
  MOZ_ASSERT(aEntry);
  if (!mFCPTiming) {
    mFCPTiming = aEntry;
    QueueEntry(aEntry);
  }
}

void PerformanceMainThread::InsertEventTimingEntry(
    PerformanceEventTiming* aEventEntry) {
  mPendingEventTimingEntries.insertBack(aEventEntry);

  if (mHasQueuedRefreshdriverObserver) {
    return;
  }

  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  nsPresContext* presContext = presShell->GetPresContext();
  if (!presContext) {
    return;
  }

  // If the refresh driver already has work to do (pending paint, animations,
  // etc.), register a post-refresh observer so entries are dispatched after
  // the paint with an accurate rendering time. Otherwise, avoid waking up
  // vsync by posting a direct task — entries will be dispatched on the next
  // event-loop iteration with the current time as rendering time.
  if (presContext->RefreshDriver()->HasReasonsToTick()) {
    // Using PostRefreshObserver is fine because we don't
    // run any JS between the `mark paint timing` step and the
    // `pending Event Timing entries` step. So mixing the order
    // here is fine.
    mHasQueuedRefreshdriverObserver = true;

    presContext->RegisterManagedPostRefreshObserver(
        new ManagedPostRefreshObserver(
            presContext, [performance = RefPtr<PerformanceMainThread>(this)](
                             bool aWasCanceled) {
              if (!aWasCanceled) {
                performance->DispatchPendingEventTimingEntries();
              }
              performance->mHasQueuedRefreshdriverObserver = false;
              return ManagedPostRefreshObserver::Unregister::Yes;
            }));
  } else {
    DispatchPendingEventTimingEntries();
  }
}

void PerformanceMainThread::BufferEventTimingEntryIfNeeded(
    PerformanceEventTiming* aEventEntry) {
  if (mEventTimingEntries.Length() < kDefaultEventTimingBufferSize) {
    mEventTimingEntries.AppendElement(aEventEntry);
  }
}

void PerformanceMainThread::BufferLargestContentfulPaintEntryIfNeeded(
    LargestContentfulPaint* aEntry) {
  MOZ_ASSERT(StaticPrefs::dom_enable_largest_contentful_paint());
  if (mLargestContentfulPaintEntries.Length() <
      kMaxLargestContentfulPaintBufferSize) {
    mLargestContentfulPaintEntries.AppendElement(aEntry);
  }
}

void PerformanceMainThread::RecordModalFallbackTime() {
  DOMHighResTimeStamp now = NowUnclamped();
  mLastModalFallbackTime = now;
  if (mCurrentEventTimingEntry) {
    mCurrentEventTimingEntry->SetFallbackTimeIfNotSet(now);
  }
  for (auto* entry : mPendingEventTimingEntries) {
    entry->SetFallbackTimeIfNotSet(now);
  }
}

void PerformanceMainThread::SetCurrentEventTimingEntry(
    PerformanceEventTiming* aEntry) {
  mCurrentEventTimingEntry = aEntry;
}

PerformanceEventTiming* PerformanceMainThread::GetCurrentEventTimingEntry()
    const {
  return mCurrentEventTimingEntry;
}

void PerformanceMainThread::QueueContainerTimingEntry(
    PerformanceContainerTiming* aEntry) {
  MOZ_ASSERT(StaticPrefs::dom_enable_container_timing());
  // Buffer the entry for potential later retrieval with buffered: true
  if (mContainerTimingEntries.Length() < kMaxContainerTimingBufferSize) {
    mContainerTimingEntries.AppendElement(aEntry);
  }
  QueueEntry(aEntry);
}

void PerformanceMainThread::FinalizeContainerTimingEntries() {
  if (!StaticPrefs::dom_enable_container_timing()) {
    return;
  }

  // Stop emitting once a scroll has happened; see the note in
  // ContainerTimingHelpers::MaybeProcessPaintForContainer and
  // https://github.com/WICG/container-timing/issues/13. Recording is already
  // gated there, so no further changes accumulate.
  if (HasDispatchedScrollEvent()) {
    return;
  }

  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  nsPresContext* presContext = presShell->GetPresContext();
  if (!presContext) {
    return;
  }

  // In case we paint outside a tick, this is null. That's okay and per the
  // spec we report it on the next paint.
  TimeStamp paintTime = presContext->GetMarkPaintTimingStart();
  if (paintTime.IsNull()) {
    return;
  }

  // https://wicg.github.io/container-timing/#emit-container-timing-entries
  for (auto iter = mContainerTimingRecords.Iter(); !iter.Done(); iter.Next()) {
    Element* containerRoot = iter.Key();
    ContainerTimingRecord& record = iter.Data();

    if (!record.mHasPendingChanges) {
      continue;
    }

    // Make sure we still clear things for a detached or shadow-tree.
    if (!containerRoot->IsInUncomposedDoc()) {
      record.ClearPendingChanges();
      continue;
    }

    if (record.mFirstRenderTime.IsNull()) {
      record.mFirstRenderTime = paintTime;
    }

    // The intersectionRect is the bounding box of the painted region; size is
    // the total painted area in CSS pixels.
    nsRect intersectionRect = record.mPaintedRegion.GetBounds();
    uint64_t appUnitsPerPixelSquared =
        static_cast<uint64_t>(AppUnitsPerCSSPixel()) * AppUnitsPerCSSPixel();
    uint64_t sizeInPixels = record.mPaintedRegionArea / appUnitsPerPixelSquared;

    // We emit once per frame from the paint pipeline, so mFirstRenderTime
    // stands in for the spec's per-record paintTimingInfo and the emit-frame
    // paintTime stands in for lastNewPaintedAreaPaintTimingInfo.
    RefPtr<PerformanceContainerTiming> containerEntry =
        new PerformanceContainerTiming(this, containerRoot, record.mIdentifier,
                                       intersectionRect, sizeInPixels,
                                       record.mFirstRenderTime, paintTime,
                                       record.mLastNewPaintedAreaElement);
    QueueContainerTimingEntry(containerEntry);

    record.ClearPendingChanges();
  }
}

void PerformanceMainThread::DispatchPendingEventTimingEntries() {
  DOMHighResTimeStamp renderingTime = NowUnclamped();

  auto entriesToBeQueuedEnd = mPendingEventTimingEntries.end();
  for (auto it = mPendingEventTimingEntries.begin();
       it != mPendingEventTimingEntries.end(); ++it) {
    // Set its duration if it's not set already.
    PerformanceEventTiming* entry = *it;
    if (entry->RawDuration().isNothing()) {
      // If a modal dialog appeared during event processing, its appearance
      // time is used as the effective rendering time. The dialog provides
      // visual feedback before the next paint, so we use that earlier time.
      // https://github.com/w3c/event-timing/issues/154
      DOMHighResTimeStamp effectiveRenderingTime =
          entry->GetFallbackTime().valueOr(renderingTime);
      entry->SetDuration(effectiveRenderingTime - entry->RawStartTime());
    }

    if (!(mPendingEventTimingEntries.end() != entriesToBeQueuedEnd) &&
        !entry->HasKnownInteractionId()) {
      entriesToBeQueuedEnd = it;
    }
  }

  if (!StaticPrefs::dom_performance_event_timing_enable_interactionid() ||
      mPendingEventTimingEntries.begin() != entriesToBeQueuedEnd) {
    while (mPendingEventTimingEntries.begin() != entriesToBeQueuedEnd) {
      RefPtr<PerformanceEventTiming> entry =
          mPendingEventTimingEntries.popFirst();
      UpdateInteractionTelemetry(entry);
      if (entry->RawDuration().valueOr(0) >= kDefaultEventTimingMinDuration) {
        QueueEntry(entry);
      }

      // Perform the following steps to update the event counts:
      IncEventCount(entry->GetName());

      // If window’s has dispatched input event is false, run the following
      // steps:
      if (StaticPrefs::dom_performance_event_timing_enable_interactionid()) {
        if (!mHasDispatchedInputEvent && entry->InteractionId() != 0) {
          mFirstInputEvent = entry->Clone();
          mFirstInputEvent->SetEntryType(nsGkAtoms::firstInput);
          QueueEntry(mFirstInputEvent);
          SetHasDispatchedInputEvent();
        }
      } else {
        if (!mHasDispatchedInputEvent) {
          switch (entry->GetMessage()) {
            case ePointerDown: {
              mPendingPointerDown = entry->Clone();
              mPendingPointerDown->SetEntryType(nsGkAtoms::firstInput);
              break;
            }
            case ePointerUp: {
              if (mPendingPointerDown) {
                MOZ_ASSERT(!mFirstInputEvent);
                mFirstInputEvent = mPendingPointerDown.forget();
                QueueEntry(mFirstInputEvent);
                SetHasDispatchedInputEvent();
              }
              break;
            }
            case ePointerClick:
            case eKeyDown:
            case eMouseDown: {
              mFirstInputEvent = entry->Clone();
              mFirstInputEvent->SetEntryType(nsGkAtoms::firstInput);
              QueueEntry(mFirstInputEvent);
              SetHasDispatchedInputEvent();
              break;
            }
            default:
              break;
          }
        }
      }
    }
  }
}

void PerformanceMainThread::UpdateInteractionTelemetry(
    PerformanceEventTiming* aEntry) {
  // PerformanceEventTiming allocates an entry for every trusted user-input
  // event, including handler-less ones bounded by vsync. Filter them out
  // so the inp* percentiles aren't dominated by no-op events.
  const double rawDur = aEntry->RawDuration().valueOr(0.0);
  if (rawDur < kInpEventDurationThreshold) {
    return;
  }
  const uint32_t dur = static_cast<uint32_t>(
      std::min<double>(rawDur, std::numeric_limits<uint32_t>::max()));
  const EventMessage msg = aEntry->GetMessage();

  switch (msg) {
    case eKeyDown:
    case eKeyPress:
    case eKeyUp:
      mInteractionTelemetry.keypressMaxDuration =
          std::max(mInteractionTelemetry.keypressMaxDuration, dur);
      break;
    case ePointerClick:
      mInteractionTelemetry.mouseClick =
          std::max(mInteractionTelemetry.mouseClick, dur);
      break;
    default:
      break;
  }

  if (aEntry->InteractionId() == 0) {
    return;
  }

  mInteractionTelemetry.inpLongest =
      std::max(mInteractionTelemetry.inpLongest, dur);

  auto& durations = mInteractionTelemetry.interactionEventDurations;
  if (durations.Length() < kMaxInteractionDurations) {
    durations.InsertElementSorted(static_cast<uint16_t>(
        std::min<uint32_t>(dur, std::numeric_limits<uint16_t>::max())));
  }
}

PerformanceInteractionMetrics&
PerformanceMainThread::GetPerformanceInteractionMetrics() {
  return mInteractionMetrics;
}

void PerformanceMainThread::SetInteractionId(
    PerformanceEventTiming* aEventTiming, const WidgetEvent* aEvent) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!StaticPrefs::dom_performance_event_timing_enable_interactionid() ||
      aEvent->mFlags.mOnlyChromeDispatch || !aEvent->IsTrusted()) {
    aEventTiming->SetInteractionId(0);
    return;
  }

  aEventTiming->SetInteractionId(
      mInteractionMetrics.ComputeInteractionId(aEventTiming, aEvent));
}

DOMHighResTimeStamp PerformanceMainThread::GetPerformanceTimingFromString(
    const nsAString& aProperty) {
  // ::Measure expects the values returned from this function to be passed
  // through ReduceTimePrecision already.
  if (!IsPerformanceTimingAttribute(aProperty)) {
    return 0;
  }
  // Values from Timing() are already reduced
  if (aProperty.EqualsLiteral("redirectStart")) {
    return Timing()->RedirectStart();
  }
  if (aProperty.EqualsLiteral("redirectEnd")) {
    return Timing()->RedirectEnd();
  }
  if (aProperty.EqualsLiteral("fetchStart")) {
    return Timing()->FetchStart();
  }
  if (aProperty.EqualsLiteral("domainLookupStart")) {
    return Timing()->DomainLookupStart();
  }
  if (aProperty.EqualsLiteral("domainLookupEnd")) {
    return Timing()->DomainLookupEnd();
  }
  if (aProperty.EqualsLiteral("connectStart")) {
    return Timing()->ConnectStart();
  }
  if (aProperty.EqualsLiteral("secureConnectionStart")) {
    return Timing()->SecureConnectionStart();
  }
  if (aProperty.EqualsLiteral("connectEnd")) {
    return Timing()->ConnectEnd();
  }
  if (aProperty.EqualsLiteral("requestStart")) {
    return Timing()->RequestStart();
  }
  if (aProperty.EqualsLiteral("responseStart")) {
    return Timing()->ResponseStart();
  }
  if (aProperty.EqualsLiteral("responseEnd")) {
    return Timing()->ResponseEnd();
  }
  // Values from GetDOMTiming() are not.
  DOMHighResTimeStamp retValue;
  if (aProperty.EqualsLiteral("navigationStart")) {
    // DOMHighResTimeStamp is in relation to navigationStart, so this will be
    // zero.
    retValue = GetDOMTiming()->GetNavigationStart();
  } else if (aProperty.EqualsLiteral("unloadEventStart")) {
    retValue = GetDOMTiming()->GetUnloadEventStart();
  } else if (aProperty.EqualsLiteral("unloadEventEnd")) {
    retValue = GetDOMTiming()->GetUnloadEventEnd();
  } else if (aProperty.EqualsLiteral("domLoading")) {
    retValue = GetDOMTiming()->GetDomLoading();
  } else if (aProperty.EqualsLiteral("domInteractive")) {
    retValue = GetDOMTiming()->GetDomInteractive();
  } else if (aProperty.EqualsLiteral("domContentLoadedEventStart")) {
    retValue = GetDOMTiming()->GetDomContentLoadedEventStart();
  } else if (aProperty.EqualsLiteral("domContentLoadedEventEnd")) {
    retValue = GetDOMTiming()->GetDomContentLoadedEventEnd();
  } else if (aProperty.EqualsLiteral("domComplete")) {
    retValue = GetDOMTiming()->GetDomComplete();
  } else if (aProperty.EqualsLiteral("loadEventStart")) {
    retValue = GetDOMTiming()->GetLoadEventStart();
  } else if (aProperty.EqualsLiteral("loadEventEnd")) {
    retValue = GetDOMTiming()->GetLoadEventEnd();
  } else {
    MOZ_CRASH(
        "IsPerformanceTimingAttribute and GetPerformanceTimingFromString are "
        "out "
        "of sync");
  }
  return nsRFPService::ReduceTimePrecisionAsMSecs(
      retValue, GetRandomTimelineSeed(), mRTPCallerType);
}

void PerformanceMainThread::InsertUserEntry(PerformanceEntry* aEntry) {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoCString uri;
  double markCreationEpoch = 0;

  if (StaticPrefs::dom_performance_enable_user_timing_logging() ||
      StaticPrefs::dom_performance_enable_notify_performance_timing()) {
    nsresult rv = NS_ERROR_FAILURE;
    nsGlobalWindowInner* owner = GetOwnerWindow();
    if (owner && owner->GetDocumentURI()) {
      rv = owner->GetDocumentURI()->GetHost(uri);
    }

    if (NS_FAILED(rv)) {
      // If we have no URI, just put in "none".
      uri.AssignLiteral("none");
    }

    // PR_Now() returns a signed 64-bit integer. Since it represents a
    // timestamp, only ~32-bits will represent the value which should safely fit
    // into a double.
    markCreationEpoch = static_cast<double>(PR_Now() / PR_USEC_PER_MSEC);

    if (StaticPrefs::dom_performance_enable_user_timing_logging()) {
      Performance::LogEntry(aEntry, uri);
    }
  }

  if (StaticPrefs::dom_performance_enable_notify_performance_timing()) {
    TimingNotification(aEntry, uri, markCreationEpoch);
  }

  Performance::InsertUserEntry(aEntry);
}

TimeStamp PerformanceMainThread::CreationTimeStamp() const {
  return GetDOMTiming()->GetNavigationStartTimeStamp();
}

DOMHighResTimeStamp PerformanceMainThread::CreationTime() const {
  return GetDOMTiming()->GetNavigationStart();
}

void PerformanceMainThread::CreateNavigationTimingEntry() {
  MOZ_ASSERT(!mDocEntry, "mDocEntry should be null.");

  if (!StaticPrefs::dom_enable_performance_navigation_timing()) {
    return;
  }

  nsAutoString name;
  GetURLSpecFromChannel(mChannel, name);

  auto timing = std::make_unique<PerformanceTimingData>(mChannel, nullptr, 0);

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel);
  if (httpChannel) {
    timing->SetPropertiesFromHttpChannel(httpChannel, mChannel);
  }

  mDocEntry = new PerformanceNavigationTiming(std::move(timing), this, name);
}

void PerformanceMainThread::UpdateNavigationTimingEntry() {
  if (!mDocEntry) {
    return;
  }

  // Let's update some values.
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel);
  if (httpChannel) {
    mDocEntry->UpdatePropertiesFromHttpChannel(httpChannel, mChannel);
  }

  // Not in Create(): the cache disposition is unresolved until the load ends.
  if (mDOMTiming && mDOMTiming->WasActivatedFromNavigationalPrefetch() &&
      mDocEntry->ServedFromCache()) {
    mDocEntry->SetDeliveryType(u"navigational-prefetch"_ns);
  }
}

void PerformanceMainThread::QueueNavigationTimingEntry() {
  if (!mDocEntry) {
    return;
  }

  UpdateNavigationTimingEntry();

  QueueEntry(mDocEntry);
}

void PerformanceMainThread::QueueLargestContentfulPaintEntry(
    LargestContentfulPaint* aEntry) {
  MOZ_ASSERT(StaticPrefs::dom_enable_largest_contentful_paint());
  QueueEntry(aEntry);
}

EventCounts* PerformanceMainThread::EventCounts() {
  MOZ_ASSERT(StaticPrefs::dom_enable_event_timing());
  return mEventCounts;
}

uint64_t PerformanceMainThread::InteractionCount() {
  MOZ_ASSERT(StaticPrefs::dom_performance_event_timing_enable_interactionid());
  return mInteractionMetrics.InteractionCount();
}

void PerformanceMainThread::GetEntries(
    nsTArray<RefPtr<PerformanceEntry>>& aRetval) {
  aRetval = mResourceEntries.Clone();
  aRetval.AppendElements(mUserEntries);

  if (mDocEntry) {
    aRetval.AppendElement(mDocEntry);
  }

  if (mFCPTiming) {
    aRetval.AppendElement(mFCPTiming);
  }

  if (mFirstInputEvent) {
    aRetval.AppendElement(mFirstInputEvent);
  }
  aRetval.Sort(PerformanceEntryComparator());
}

void PerformanceMainThread::GetEntriesByType(
    const nsAString& aEntryType, nsTArray<RefPtr<PerformanceEntry>>& aRetval) {
  RefPtr<nsAtom> type = NS_Atomize(aEntryType);
  if (type == nsGkAtoms::navigation) {
    aRetval.Clear();

    if (mDocEntry) {
      aRetval.AppendElement(mDocEntry);
    }
    return;
  }

  if (type == nsGkAtoms::paint) {
    if (mFCPTiming) {
      aRetval.AppendElement(mFCPTiming);
      return;
    }
  }

  if (type == nsGkAtoms::firstInput && mFirstInputEvent) {
    aRetval.AppendElement(mFirstInputEvent);
    return;
  }

  Performance::GetEntriesByType(aEntryType, aRetval);
}
void PerformanceMainThread::GetEntriesByTypeForObserver(
    const nsAString& aEntryType, nsTArray<RefPtr<PerformanceEntry>>& aRetval) {
  if (aEntryType.EqualsLiteral("event")) {
    aRetval.AppendElements(mEventTimingEntries);
    return;
  }

  if (StaticPrefs::dom_enable_largest_contentful_paint()) {
    if (aEntryType.EqualsLiteral("largest-contentful-paint")) {
      aRetval.AppendElements(mLargestContentfulPaintEntries);
      return;
    }
  }

  if (StaticPrefs::dom_enable_container_timing()) {
    if (aEntryType.Equals(kContainerTimingName)) {
      aRetval.AppendElements(mContainerTimingEntries);
      return;
    }
  }

  return GetEntriesByType(aEntryType, aRetval);
}

void PerformanceMainThread::GetEntriesByName(
    const nsAString& aName, const Optional<nsAString>& aEntryType,
    nsTArray<RefPtr<PerformanceEntry>>& aRetval) {
  Performance::GetEntriesByName(aName, aEntryType, aRetval);

  if (MatchesNameAndType(mFCPTiming, aName, aEntryType)) {
    aRetval.InsertElementSorted(mFCPTiming, PerformanceEntryComparator());
  }

  if (MatchesNameAndType(mFirstInputEvent, aName, aEntryType)) {
    aRetval.InsertElementSorted(mFirstInputEvent, PerformanceEntryComparator());
  }

  // The navigation entry is the first one. If it exists and the name matches,
  // let put it in front.
  if (MatchesNameAndType(mDocEntry, aName, aEntryType)) {
    aRetval.InsertElementAt(0, mDocEntry);
  }
}

mozilla::PresShell* PerformanceMainThread::GetPresShell() {
  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    return nullptr;
  }
  if (Document* doc = global->GetAsInnerWindow()->GetExtantDoc()) {
    return doc->GetPresShell();
  }
  return nullptr;
}

void PerformanceMainThread::IncEventCount(const nsAtom* aType) {
  MOZ_ASSERT(StaticPrefs::dom_enable_event_timing());

  // This occurs when the pref was false when the performance
  // object was first created, and became true later. It's
  // okay to return early because eventCounts is not exposed.
  if (!mEventCounts) {
    return;
  }

  IgnoredErrorResult rv;
  uint64_t count = EventCounts_Binding::MaplikeHelpers::Get(
      mEventCounts, nsDependentAtomString(aType), rv);
  if (rv.Failed()) {
    return;
  }
  EventCounts_Binding::MaplikeHelpers::Set(
      mEventCounts, nsDependentAtomString(aType), ++count, rv);
}

size_t PerformanceMainThread::SizeOfEventEntries(
    mozilla::MallocSizeOf aMallocSizeOf) const {
  size_t eventEntries = 0;
  for (const PerformanceEventTiming* entry : mEventTimingEntries) {
    eventEntries += entry->SizeOfIncludingThis(aMallocSizeOf);
  }
  return eventEntries;
}

void PerformanceMainThread::ProcessElementTiming() {
  if (!StaticPrefs::dom_enable_largest_contentful_paint()) {
    return;
  }
  const bool shouldLCPDataEmpty =
      HasDispatchedInputEvent() || HasDispatchedScrollEvent();
  MOZ_ASSERT_IF(shouldLCPDataEmpty, mTextFrameUnions.IsEmpty());

  if (shouldLCPDataEmpty) {
    return;
  }

  nsPresContext* presContext = GetPresShell()->GetPresContext();
  MOZ_ASSERT(presContext);

  // After https://github.com/w3c/largest-contentful-paint/issues/104 is
  // resolved, LargestContentfulPaint and FirstContentfulPaint should
  // be using the same timestamp, which should be the same timestamp
  // as to what https://w3c.github.io/paint-timing/#mark-paint-timing step 2
  // defines.
  // TODO(sefeng): Check the timestamp after this issue is resolved.
  TimeStamp rawNowTime = presContext->GetMarkPaintTimingStart();

  MOZ_ASSERT(GetRelevantGlobal());
  Document* document = GetRelevantGlobal()->GetAsInnerWindow()->GetExtantDoc();
  if (!document ||
      !nsContentUtils::GetInProcessSubtreeRootDocument(document)->IsActive()) {
    return;
  }

  nsTArray<ImagePendingRendering> imagesPendingRendering =
      std::move(mImagesPendingRendering);
  for (const auto& imagePendingRendering : imagesPendingRendering) {
    RefPtr<Element> element = imagePendingRendering.GetElement();
    if (!element) {
      continue;
    }

    MOZ_ASSERT(imagePendingRendering.mLoadTime <= rawNowTime);
    if (imgRequestProxy* requestProxy =
            imagePendingRendering.GetImgRequestProxy()) {
      requestProxy->GetLCPTimings().Set(imagePendingRendering.mLoadTime,
                                        rawNowTime);
    }
  }

  MOZ_ASSERT(mImagesPendingRendering.IsEmpty());
}

void PerformanceMainThread::FinalizeLCPEntriesForText() {
  nsPresContext* presContext = GetPresShell()->GetPresContext();
  MOZ_ASSERT(presContext);

  bool canFinalize = StaticPrefs::dom_enable_largest_contentful_paint() &&
                     !presContext->HasStoppedGeneratingLCP();
  nsTHashMap<nsRefPtrHashKey<Element>, nsRect> textFrameUnion =
      std::move(GetTextFrameUnions());
  if (canFinalize) {
    for (const auto& textFrameUnion : textFrameUnion) {
      LCPHelpers::FinalizeLCPEntryForText(
          this, presContext->GetMarkPaintTimingStart(), textFrameUnion.GetKey(),
          textFrameUnion.GetData(), presContext);
    }
  }
  MOZ_ASSERT(GetTextFrameUnions().IsEmpty());
}

void PerformanceMainThread::QueuePendingLCPCandidate() {
  if (RefPtr<LargestContentfulPaint> candidate =
          std::move(mPendingLCPCandidate)) {
    candidate->QueueEntry();
  }
}

bool PerformanceMainThread::IsPendingLCPCandidate(
    Element* aElement, imgRequestProxy* aImgRequestProxy) {
  Document* doc = aElement->GetComposedDoc();
  MOZ_ASSERT(doc, "Element should be connected when it's painted");
  if (!aElement->HasFlag(ELEMENT_IN_CONTENT_IDENTIFIER_FOR_LCP)) {
    MOZ_ASSERT(!doc->ContentIdentifiersForLCP().Contains(aElement));
    return false;
  }

  if (auto entry = doc->ContentIdentifiersForLCP().Lookup(aElement)) {
    return entry.Data().Contains(aImgRequestProxy);
  }

  MOZ_ASSERT_UNREACHABLE("we should always have an entry when the flag exists");
  return false;
}

bool PerformanceMainThread::UpdateLargestContentfulPaintSize(double aSize) {
  if (aSize > mLargestContentfulPaintSize) {
    mLargestContentfulPaintSize = aSize;
    return true;
  }
  return false;
}

void PerformanceMainThread::SetHasDispatchedScrollEvent() {
  mHasDispatchedScrollEvent = true;
  ClearGeneratedTempDataForLCP();
  ClearContainerTimingData();
}

void PerformanceMainThread::SetHasDispatchedInputEvent() {
  mHasDispatchedInputEvent = true;
  ClearGeneratedTempDataForLCP();
}

void PerformanceMainThread::ClearGeneratedTempDataForLCP() {
  mTextFrameUnions.Clear();
  mImagesPendingRendering.Clear();
  mPendingLCPCandidate = nullptr;

  nsIGlobalObject* global = GetRelevantGlobal();
  if (!global) {
    return;
  }

  if (Document* document = global->GetAsInnerWindow()->GetExtantDoc()) {
    document->ContentIdentifiersForLCP().Clear();
  }
}

void PerformanceMainThread::ClearContainerTimingData() {
  mContainerTimingRecords.Clear();
}
}  // namespace mozilla::dom
