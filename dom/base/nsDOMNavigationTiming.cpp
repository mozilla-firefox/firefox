/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDOMNavigationTiming.h"

#include "GeckoProfiler.h"
#include "SharedLcpMarkerState.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/Document.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsHttp.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsPrintfCString.h"
#include "prtime.h"

using namespace mozilla;

namespace mozilla {

LazyLogModule gPageLoadLog("PageLoad");
#define PAGELOAD_LOG(args) MOZ_LOG(gPageLoadLog, LogLevel::Debug, args)
#define PAGELOAD_LOG_ENABLED() MOZ_LOG_TEST(gPageLoadLog, LogLevel::Error)

}  // namespace mozilla

nsDOMNavigationTiming::nsDOMNavigationTiming(nsDocShell* aDocShell)
    : mSharedLcpMarkerState(MakeRefPtr<SharedLcpMarkerState>()) {
  Clear();

  mDocShell = aDocShell;
}

nsDOMNavigationTiming::~nsDOMNavigationTiming() = default;

void nsDOMNavigationTiming::Clear() {
  mNavigationType = TYPE_RESERVED;
  mNavigationStartHighRes = 0;

  mBeforeUnloadStart = TimeStamp();
  mUnloadStart = TimeStamp();
  mUnloadEnd = TimeStamp();
  mLoadEventStart = TimeStamp();
  mLoadEventEnd = TimeStamp();
  mDOMLoading = TimeStamp();
  mDOMInteractive = TimeStamp();
  mDOMContentLoadedEventStart = TimeStamp();
  mDOMContentLoadedEventEnd = TimeStamp();
  mDOMComplete = TimeStamp();
  mContentfulComposite = TimeStamp();
  mNonBlankPaint = TimeStamp();
  mLargestContentfulRender = TimeStamp();
  {
    auto lock = mSharedLcpMarkerState->mInner.Lock();
    lock->mNavStartTime = TimeStamp();
    lock->mLargestContentfulRender = TimeStamp();
    lock->mLCPElement.Truncate();
    lock->mLCPImageURL.Truncate();
  }

  mDocShellHasBeenActiveSinceNavigationStart = false;
  mForegroundAtLoadEventStart = Nothing();
}

void nsDOMNavigationTiming::Anonymize(nsIURI* aFinalURI) {
  mLoadedURI = aFinalURI;
  mUnloadedURI = nullptr;
  mBeforeUnloadStart = TimeStamp();
  mUnloadStart = TimeStamp();
  mUnloadEnd = TimeStamp();
  auto lock = mSharedLcpMarkerState->mInner.Lock();
  lock->mLCPElement.Truncate();
  lock->mLCPImageURL.Truncate();
}

RefPtr<SharedLcpMarkerState> nsDOMNavigationTiming::GetSharedLcpMarkerState()
    const {
  return mSharedLcpMarkerState;
}

DOMTimeMilliSec nsDOMNavigationTiming::TimeStampToDOM(TimeStamp aStamp) const {
  if (aStamp.IsNull()) {
    return 0;
  }

  TimeDuration duration = aStamp - mNavigationStart;
  return GetNavigationStart() + static_cast<int64_t>(duration.ToMilliseconds());
}

void nsDOMNavigationTiming::NotifyNavigationStart(
    DocShellState aDocShellState) {
  mNavigationStartHighRes = (double)PR_Now() / PR_USEC_PER_MSEC;
  mNavigationStart = TimeStamp::Now();
  mDocShellHasBeenActiveSinceNavigationStart =
      (aDocShellState == DocShellState::eActive);
  PROFILER_MARKER_UNTYPED("Navigation::Start", DOM,
                          MarkerInnerWindowIdFromDocShell(mDocShell));
}

void nsDOMNavigationTiming::NotifyFetchStart(nsIURI* aURI,
                                             Type aNavigationType) {
  mNavigationType = aNavigationType;
  // At the unload event time we don't really know the loading uri.
  // Need it for later check for unload timing access.
  mLoadedURI = aURI;
}

void nsDOMNavigationTiming::NotifyRestoreStart() {
  mNavigationType = TYPE_BACK_FORWARD;
}

void nsDOMNavigationTiming::NotifyBeforeUnload() {
  mBeforeUnloadStart = TimeStamp::Now();
}

void nsDOMNavigationTiming::NotifyUnloadAccepted(nsIURI* aOldURI) {
  mUnloadStart = mBeforeUnloadStart;
  mUnloadedURI = aOldURI;
}

void nsDOMNavigationTiming::NotifyUnloadEventStart() {
  mUnloadStart = TimeStamp::Now();
  PROFILER_MARKER("Unload", NETWORK,
                  MarkerOptions(MarkerTiming::IntervalStart(),
                                MarkerInnerWindowIdFromDocShell(mDocShell)),
                  Tracing, "Navigation");
}

void nsDOMNavigationTiming::NotifyUnloadEventEnd() {
  mUnloadEnd = TimeStamp::Now();
  PROFILER_MARKER("Unload", NETWORK,
                  MarkerOptions(MarkerTiming::IntervalEnd(),
                                MarkerInnerWindowIdFromDocShell(mDocShell)),
                  Tracing, "Navigation");
}

void nsDOMNavigationTiming::NotifyLoadEventStart() {
  if (!mLoadEventStart.IsNull()) {
    return;
  }
  mLoadEventStart = TimeStamp::Now();
  mForegroundAtLoadEventStart = Some(DocShellIsActive());

  PROFILER_MARKER("Load", NETWORK,
                  MarkerOptions(MarkerTiming::IntervalStart(),
                                MarkerInnerWindowIdFromDocShell(mDocShell)),
                  Tracing, "Navigation");

  if (IsTopLevelContentDocumentInContentProcess()) {
    TimeStamp now = TimeStamp::Now();

    glean::performance_time::load_event_start.AccumulateRawDuration(
        now - mNavigationStart);
  }
}

void nsDOMNavigationTiming::NotifyLoadEventEnd() {
  if (!mLoadEventEnd.IsNull()) {
    return;
  }
  mLoadEventEnd = TimeStamp::Now();

  PROFILER_MARKER("Load", NETWORK,
                  MarkerOptions(MarkerTiming::IntervalEnd(),
                                MarkerInnerWindowIdFromDocShell(mDocShell)),
                  Tracing, "Navigation");

  if (IsTopLevelContentDocumentInContentProcess()) {
    if (profiler_thread_is_being_profiled_for_markers() ||
        PAGELOAD_LOG_ENABLED()) {
      TimeDuration elapsed = mLoadEventEnd - mNavigationStart;
      TimeDuration duration = mLoadEventEnd - mLoadEventStart;
      nsPrintfCString marker(
          "Document %s loaded after %dms, load event duration %dms",
          nsContentUtils::TruncatedURLForDisplay(mLoadedURI).get(),
          int(elapsed.ToMilliseconds()), int(duration.ToMilliseconds()));
      PAGELOAD_LOG(("%s", marker.get()));
      PROFILER_MARKER_TEXT(
          "DocumentLoad", DOM,
          MarkerOptions(MarkerTiming::Interval(mNavigationStart, mLoadEventEnd),
                        MarkerInnerWindowIdFromDocShell(mDocShell)),
          marker);
    }
    glean::performance_time::load_event_end.AccumulateRawDuration(
        TimeStamp::Now() - mNavigationStart);
  }
}

void nsDOMNavigationTiming::SetDOMLoadingTimeStamp(nsIURI* aURI,
                                                   TimeStamp aValue) {
  if (!mDOMLoading.IsNull()) {
    return;
  }
  mLoadedURI = aURI;
  mDOMLoading = aValue;
}

void nsDOMNavigationTiming::NotifyDOMLoading(nsIURI* aURI) {
  if (!mDOMLoading.IsNull()) {
    return;
  }
  mLoadedURI = aURI;
  mDOMLoading = TimeStamp::Now();

  PROFILER_MARKER_UNTYPED("Navigation::DOMLoading", DOM,
                          MarkerInnerWindowIdFromDocShell(mDocShell));
}

void nsDOMNavigationTiming::NotifyDOMInteractive(nsIURI* aURI) {
  if (!mDOMInteractive.IsNull()) {
    return;
  }
  mLoadedURI = aURI;
  mDOMInteractive = TimeStamp::Now();

  PROFILER_MARKER_UNTYPED("Navigation::DOMInteractive", DOM,
                          MarkerInnerWindowIdFromDocShell(mDocShell));
}

void nsDOMNavigationTiming::NotifyDOMComplete(nsIURI* aURI) {
  if (!mDOMComplete.IsNull()) {
    return;
  }
  mLoadedURI = aURI;
  mDOMComplete = TimeStamp::Now();

  PROFILER_MARKER_UNTYPED("Navigation::DOMComplete", DOM,
                          MarkerInnerWindowIdFromDocShell(mDocShell));
}

void nsDOMNavigationTiming::NotifyDOMContentLoadedStart(nsIURI* aURI) {
  if (!mDOMContentLoadedEventStart.IsNull()) {
    return;
  }

  mLoadedURI = aURI;
  mDOMContentLoadedEventStart = TimeStamp::Now();

  PROFILER_MARKER("DOMContentLoaded", NETWORK,
                  MarkerOptions(MarkerTiming::IntervalStart(),
                                MarkerInnerWindowIdFromDocShell(mDocShell)),
                  Tracing, "Navigation");

  if (IsTopLevelContentDocumentInContentProcess()) {
    TimeStamp now = TimeStamp::Now();

    glean::performance_time::dom_content_loaded_start.AccumulateRawDuration(
        now - mNavigationStart);
  }
}

void nsDOMNavigationTiming::NotifyDOMContentLoadedEnd(nsIURI* aURI) {
  if (!mDOMContentLoadedEventEnd.IsNull()) {
    return;
  }

  mLoadedURI = aURI;
  mDOMContentLoadedEventEnd = TimeStamp::Now();

  PROFILER_MARKER("DOMContentLoaded", NETWORK,
                  MarkerOptions(MarkerTiming::IntervalEnd(),
                                MarkerInnerWindowIdFromDocShell(mDocShell)),
                  Tracing, "Navigation");

  if (IsTopLevelContentDocumentInContentProcess()) {
    glean::performance_time::dom_content_loaded_end.AccumulateRawDuration(
        TimeStamp::Now() - mNavigationStart);
  }
}

// static
void nsDOMNavigationTiming::TTITimeoutCallback(nsITimer* aTimer,
                                               void* aClosure) {
  nsDOMNavigationTiming* self = static_cast<nsDOMNavigationTiming*>(aClosure);
  self->TTITimeout(aTimer);
}

#define TTI_WINDOW_SIZE_MS (5 * 1000)

void nsDOMNavigationTiming::TTITimeout(nsITimer* aTimer) {
  // Check TTI: see if it's been 5 seconds since the last Long Task
  TimeStamp now = TimeStamp::Now();
  MOZ_RELEASE_ASSERT(!mContentfulComposite.IsNull(),
                     "TTI timeout with no contentful-composite?");

  nsCOMPtr<nsIThread> mainThread = do_GetMainThread();
  TimeStamp lastLongTaskEnded;
  mainThread->GetLastLongNonIdleTaskEnd(&lastLongTaskEnded);
  // Window starts at mContentfulComposite; any long task before that is ignored
  if (lastLongTaskEnded.IsNull() || lastLongTaskEnded < mContentfulComposite) {
    PAGELOAD_LOG(
        ("no longtask (last was %g ms before ContentfulComposite)",
         lastLongTaskEnded.IsNull()
             ? 0
             : (mContentfulComposite - lastLongTaskEnded).ToMilliseconds()));
    lastLongTaskEnded = mContentfulComposite;
  }
  TimeDuration delta = now - lastLongTaskEnded;
  PAGELOAD_LOG(("TTI delta: %g ms", delta.ToMilliseconds()));
  if (delta.ToMilliseconds() < TTI_WINDOW_SIZE_MS) {
    // Less than 5 seconds since the last long task or start of the window.
    // Schedule another check.
    PAGELOAD_LOG(("TTI: waiting additional %g ms",
                  (TTI_WINDOW_SIZE_MS + 100) - delta.ToMilliseconds()));
    aTimer->InitWithNamedFuncCallback(
        TTITimeoutCallback, this,
        (TTI_WINDOW_SIZE_MS + 100) -
            delta.ToMilliseconds(),  // slightly after the window ends
        nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY,
        "nsDOMNavigationTiming::TTITimeout"_ns);
    return;
  }

  // To correctly implement TTI/TTFI as proposed, we'd need to not
  // fire it until there are no more than 2 network loads.  By the
  // proposed definition, without that we're closer to
  // TimeToFirstInteractive.  There are also arguments about what sort
  // of loads should qualify.

  // XXX check number of network loads, and if > 2 mark to check if loads
  // decreases to 2 (or record that point and let the normal timer here
  // handle it)

  // TTI has occurred!  TTI is either FCP (if there are no longtasks and no
  // DCLEnd in the window that starts at FCP), or at the end of the last
  // Long Task or DOMContentLoadedEnd (whichever is later). lastLongTaskEnded
  // is >= FCP here.

  if (mTTFI.IsNull()) {
    // lastLongTaskEnded is >= mContentfulComposite
    mTTFI = (mDOMContentLoadedEventEnd.IsNull() ||
             lastLongTaskEnded > mDOMContentLoadedEventEnd)
                ? lastLongTaskEnded
                : mDOMContentLoadedEventEnd;
    PAGELOAD_LOG(
        ("TTFI after %dms (LongTask was at %dms, DCL was %dms)",
         int((mTTFI - mNavigationStart).ToMilliseconds()),
         lastLongTaskEnded.IsNull()
             ? 0
             : int((lastLongTaskEnded - mNavigationStart).ToMilliseconds()),
         mDOMContentLoadedEventEnd.IsNull()
             ? 0
             : int((mDOMContentLoadedEventEnd - mNavigationStart)
                       .ToMilliseconds())));
  }
  // XXX Implement TTI via check number of network loads, and if > 2 mark
  // to check if loads decreases to 2 (or record that point and let the
  // normal timer here handle it)

  mTTITimer = nullptr;

  if (profiler_thread_is_being_profiled_for_markers() ||
      PAGELOAD_LOG_ENABLED()) {
    TimeDuration elapsed = mTTFI - mNavigationStart;
    MOZ_ASSERT(elapsed.ToMilliseconds() > 0);
    TimeDuration elapsedLongTask =
        lastLongTaskEnded.IsNull()
            ? TimeDuration()
            : TimeDuration(lastLongTaskEnded - mNavigationStart);
    nsPrintfCString marker(
        "TTFI after %dms (LongTask was at %dms) for URL %s",
        int(elapsed.ToMilliseconds()), int(elapsedLongTask.ToMilliseconds()),
        nsContentUtils::TruncatedURLForDisplay(mLoadedURI).get());

    PROFILER_MARKER_TEXT(
        "TimeToFirstInteractive (TTFI)", DOM,
        MarkerOptions(MarkerTiming::Interval(mNavigationStart, mTTFI),
                      MarkerInnerWindowIdFromDocShell(mDocShell)),
        marker);
  }
}

void nsDOMNavigationTiming::NotifyNonBlankPaintForRootContentDocument() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mNavigationStart.IsNull());

  if (!mNonBlankPaint.IsNull()) {
    return;
  }

  mNonBlankPaint = TimeStamp::Now();

  if (profiler_thread_is_being_profiled_for_markers() ||
      PAGELOAD_LOG_ENABLED()) {
    TimeDuration elapsed = mNonBlankPaint - mNavigationStart;
    nsPrintfCString marker(
        "Non-blank paint after %dms for URL %s, %s",
        int(elapsed.ToMilliseconds()),
        nsContentUtils::TruncatedURLForDisplay(mLoadedURI).get(),
        mDocShellHasBeenActiveSinceNavigationStart
            ? "foreground tab"
            : "this tab was inactive some of the time between navigation start "
              "and first non-blank paint");
    PAGELOAD_LOG(("%s", marker.get()));
    PROFILER_MARKER_TEXT(
        "FirstNonBlankPaint", DOM,
        MarkerOptions(MarkerTiming::Interval(mNavigationStart, mNonBlankPaint),
                      MarkerInnerWindowIdFromDocShell(mDocShell)),
        marker);
  }

  if (mDocShellHasBeenActiveSinceNavigationStart) {
    glean::performance_page::non_blank_paint.AccumulateRawDuration(
        mNonBlankPaint - mNavigationStart);
  }
}

void nsDOMNavigationTiming::NotifyContentfulCompositeForRootContentDocument(
    const mozilla::TimeStamp& aCompositeEndTime) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mNavigationStart.IsNull());

  if (!mContentfulComposite.IsNull()) {
    return;
  }

  mContentfulComposite = aCompositeEndTime;

  if (profiler_thread_is_being_profiled_for_markers() ||
      PAGELOAD_LOG_ENABLED()) {
    TimeDuration elapsed = mContentfulComposite - mNavigationStart;
    nsPrintfCString marker(
        "Contentful composite after %dms for URL %s, %s",
        int(elapsed.ToMilliseconds()),
        nsContentUtils::TruncatedURLForDisplay(mLoadedURI).get(),
        mDocShellHasBeenActiveSinceNavigationStart
            ? "foreground tab"
            : "this tab was inactive some of the time between navigation start "
              "and first non-blank paint");
    PAGELOAD_LOG(("%s", marker.get()));
    PROFILER_MARKER_TEXT(
        "FirstContentfulComposite", DOM,
        MarkerOptions(
            MarkerTiming::Interval(mNavigationStart, mContentfulComposite),
            MarkerInnerWindowIdFromDocShell(mDocShell)),
        marker);
  }

  if (!mTTITimer) {
    mTTITimer = NS_NewTimer();
  }

  // TTI is first checked 5 seconds after the FCP (non-blank-paint is very close
  // to FCP).
  mTTITimer->InitWithNamedFuncCallback(TTITimeoutCallback, this,
                                       TTI_WINDOW_SIZE_MS,
                                       nsITimer::TYPE_ONE_SHOT_LOW_PRIORITY,
                                       "nsDOMNavigationTiming::TTITimeout"_ns);
}

void nsDOMNavigationTiming::NotifyLargestContentfulRenderForRootContentDocument(
    const DOMHighResTimeStamp& aRenderTime, const nsAString& aElement,
    const nsACString& aImageURL) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mNavigationStart.IsNull());

  // This can get called multiple times and updates over time.
  mLargestContentfulRender =
      mNavigationStart + TimeDuration::FromMilliseconds(aRenderTime);
  auto lock = mSharedLcpMarkerState->mInner.Lock();
  lock->mNavStartTime = mNavigationStart;
  lock->mLargestContentfulRender = mLargestContentfulRender;
  lock->mLCPElement = aElement;
  lock->mLCPImageURL = aImageURL;
}

void nsDOMNavigationTiming::NotifyDocShellStateChanged(
    DocShellState aDocShellState) {
  mDocShellHasBeenActiveSinceNavigationStart &=
      (aDocShellState == DocShellState::eActive);
}

namespace geckoprofiler::markers {

struct LCPMarker : public BaseMarkerType<LCPMarker> {
  static constexpr const char* Name = "LargestContentfulPaint";

  using MS = MarkerSchema;
  static constexpr MS::PayloadField PayloadFields[] = {
      {"timeMs", MS::InputType::TimeDuration, "Elapsed Time",
       MS::Format::Duration},
      {"element", MS::InputType::String, "Element", MS::Format::String},
      {"imageURL", MS::InputType::CString, "Image URL", MS::Format::Url}};

  static constexpr MS::Location Locations[] = {MS::Location::MarkerChart,
                                               MS::Location::MarkerTable,
                                               MS::Location::TimelineOverview};
  static constexpr const char* ChartLabel =
      "Largest contentful paint after {marker.data.timeMs}";
  static constexpr const char* TableLabel =
      "Largest contentful paint after {marker.data.timeMs}";

  static void StreamJSONMarkerData(baseprofiler::SpliceableJSONWriter& aWriter,
                                   TimeDuration aElapsedTime,
                                   const ProfilerString16View& aElement,
                                   const ProfilerString8View& aImageURL) {
    aWriter.IntProperty("timeMs",
                        static_cast<uint32_t>(aElapsedTime.ToMilliseconds()));
    if (aElement.Length() > 0) {
      aWriter.StringProperty("element", NS_ConvertUTF16toUTF8(aElement));
    }
    if (aImageURL.Length() > 0) {
      aWriter.StringProperty("imageURL", aImageURL);
    }
  }
};

}  // namespace geckoprofiler::markers

void SharedLcpMarkerState::MaybeAddLCPProfilerMarker(
    MarkerInnerWindowId aInnerWindowID) {
  // This method might get called from outside of the main thread, so can't
  // check `profiler_thread_is_being_profiled_for_markers()` here.
  if (!profiler_is_active_and_unpaused()) {
    return;
  }

  auto inner = CopyInner();

  if (!inner.mNavStartTime || !inner.mLargestContentfulRender) {
    return;
  }

  TimeDuration elapsedTime =
      inner.mLargestContentfulRender - inner.mNavStartTime;
  PROFILER_MARKER(
      "LargestContentfulPaint", DOM,
      // Putting this marker to the main thread even if it's
      // called from another one.
      MarkerOptions(MarkerThreadId::MainThread(),
                    MarkerTiming::Interval(inner.mNavStartTime,
                                           inner.mLargestContentfulRender),
                    std::move(aInnerWindowID)),
      LCPMarker, elapsedTime, inner.mLCPElement, inner.mLCPImageURL);
}

mozilla::TimeStamp nsDOMNavigationTiming::GetUnloadEventStartTimeStamp() const {
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  // todo: if you intend to update CheckSameOriginURI to log the error to the
  // console you also need to update the 'aFromPrivateWindow' argument.
  nsresult rv = ssm->CheckSameOriginURI(mLoadedURI, mUnloadedURI, false, false);
  if (NS_SUCCEEDED(rv)) {
    return mUnloadStart;
  }
  return mozilla::TimeStamp();
}

mozilla::TimeStamp nsDOMNavigationTiming::GetUnloadEventEndTimeStamp() const {
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  // todo: if you intend to update CheckSameOriginURI to log the error to the
  // console you also need to update the 'aFromPrivateWindow' argument.
  nsresult rv = ssm->CheckSameOriginURI(mLoadedURI, mUnloadedURI, false, false);
  if (NS_SUCCEEDED(rv)) {
    return mUnloadEnd;
  }
  return mozilla::TimeStamp();
}

bool nsDOMNavigationTiming::DocShellIsActive() const {
  if (!mDocShell) {
    return false;
  }
  auto* bc = mDocShell->GetBrowsingContext();
  return bc && bc->IsActive();
}

bool nsDOMNavigationTiming::IsTopLevelContentDocumentInContentProcess() const {
  if (!mDocShell) {
    return false;
  }
  if (!XRE_IsContentProcess()) {
    return false;
  }
  return mDocShell->GetBrowsingContext()->IsTopContent();
}

nsDOMNavigationTiming::nsDOMNavigationTiming(nsDocShell* aDocShell,
                                             nsDOMNavigationTiming* aOther)
    : mDocShell(aDocShell),
      mUnloadedURI(aOther->mUnloadedURI),
      mLoadedURI(aOther->mLoadedURI),
      mNavigationType(aOther->mNavigationType),
      mNavigationStartHighRes(aOther->mNavigationStartHighRes),
      mNavigationStart(aOther->mNavigationStart),
      mNonBlankPaint(aOther->mNonBlankPaint),
      mContentfulComposite(aOther->mContentfulComposite),
      mLargestContentfulRender(aOther->mLargestContentfulRender),
      mSharedLcpMarkerState(MakeRefPtr<SharedLcpMarkerState>()),
      mBeforeUnloadStart(aOther->mBeforeUnloadStart),
      mUnloadStart(aOther->mUnloadStart),
      mUnloadEnd(aOther->mUnloadEnd),
      mLoadEventStart(aOther->mLoadEventStart),
      mLoadEventEnd(aOther->mLoadEventEnd),
      mDOMLoading(aOther->mDOMLoading),
      mDOMInteractive(aOther->mDOMInteractive),
      mDOMContentLoadedEventStart(aOther->mDOMContentLoadedEventStart),
      mDOMContentLoadedEventEnd(aOther->mDOMContentLoadedEventEnd),
      mDOMComplete(aOther->mDOMComplete),
      mTTFI(aOther->mTTFI),
      mDocShellHasBeenActiveSinceNavigationStart(
          aOther->mDocShellHasBeenActiveSinceNavigationStart),
      mWasActivatedFromNavigationalPrefetch(
          aOther->mWasActivatedFromNavigationalPrefetch),
      mForegroundAtLoadEventStart(aOther->mForegroundAtLoadEventStart) {}

/* static */
void IPC::ParamTraits<nsDOMNavigationTiming*>::Write(
    MessageWriter* aWriter, nsDOMNavigationTiming* aParam) {
  bool isNull = !aParam;
  WriteParam(aWriter, isNull);
  if (isNull) {
    return;
  }

  RefPtr<nsIURI> unloadedURI = aParam->mUnloadedURI.get();
  RefPtr<nsIURI> loadedURI = aParam->mLoadedURI.get();
  WriteParam(aWriter, unloadedURI ? Some(unloadedURI) : Nothing());
  WriteParam(aWriter, loadedURI ? Some(loadedURI) : Nothing());
  WriteParam(aWriter, uint32_t(aParam->mNavigationType));
  WriteParam(aWriter, aParam->mNavigationStartHighRes);
  WriteParam(aWriter, aParam->mNavigationStart);
  WriteParam(aWriter, aParam->mNonBlankPaint);
  WriteParam(aWriter, aParam->mContentfulComposite);
  WriteParam(aWriter, aParam->mBeforeUnloadStart);
  WriteParam(aWriter, aParam->mUnloadStart);
  WriteParam(aWriter, aParam->mUnloadEnd);
  WriteParam(aWriter, aParam->mLoadEventStart);
  WriteParam(aWriter, aParam->mLoadEventEnd);
  WriteParam(aWriter, aParam->mDOMLoading);
  WriteParam(aWriter, aParam->mDOMInteractive);
  WriteParam(aWriter, aParam->mDOMContentLoadedEventStart);
  WriteParam(aWriter, aParam->mDOMContentLoadedEventEnd);
  WriteParam(aWriter, aParam->mDOMComplete);
  WriteParam(aWriter, aParam->mTTFI);
  WriteParam(aWriter, aParam->mDocShellHasBeenActiveSinceNavigationStart);
  WriteParam(aWriter, aParam->mWasActivatedFromNavigationalPrefetch);
  WriteParam(aWriter, aParam->mForegroundAtLoadEventStart);
}

/* static */
bool IPC::ParamTraits<nsDOMNavigationTiming*>::Read(
    IPC::MessageReader* aReader, RefPtr<nsDOMNavigationTiming>* aResult) {
  bool isNull;
  if (!ReadParam(aReader, &isNull)) {
    return false;
  }
  if (isNull) {
    *aResult = nullptr;
    return true;
  }

  auto timing = MakeRefPtr<nsDOMNavigationTiming>(nullptr);
  uint32_t type;
  Maybe<RefPtr<nsIURI>> unloadedURI;
  Maybe<RefPtr<nsIURI>> loadedURI;
  if (!ReadParam(aReader, &unloadedURI) || !ReadParam(aReader, &loadedURI) ||
      !ReadParam(aReader, &type) ||
      !ReadParam(aReader, &timing->mNavigationStartHighRes) ||
      !ReadParam(aReader, &timing->mNavigationStart) ||
      !ReadParam(aReader, &timing->mNonBlankPaint) ||
      !ReadParam(aReader, &timing->mContentfulComposite) ||
      !ReadParam(aReader, &timing->mBeforeUnloadStart) ||
      !ReadParam(aReader, &timing->mUnloadStart) ||
      !ReadParam(aReader, &timing->mUnloadEnd) ||
      !ReadParam(aReader, &timing->mLoadEventStart) ||
      !ReadParam(aReader, &timing->mLoadEventEnd) ||
      !ReadParam(aReader, &timing->mDOMLoading) ||
      !ReadParam(aReader, &timing->mDOMInteractive) ||
      !ReadParam(aReader, &timing->mDOMContentLoadedEventStart) ||
      !ReadParam(aReader, &timing->mDOMContentLoadedEventEnd) ||
      !ReadParam(aReader, &timing->mDOMComplete) ||
      !ReadParam(aReader, &timing->mTTFI) ||
      !ReadParam(aReader,
                 &timing->mDocShellHasBeenActiveSinceNavigationStart) ||
      !ReadParam(aReader, &timing->mWasActivatedFromNavigationalPrefetch) ||
      !ReadParam(aReader, &timing->mForegroundAtLoadEventStart)) {
    return false;
  }
  timing->mNavigationType = nsDOMNavigationTiming::Type(type);
  if (unloadedURI) {
    timing->mUnloadedURI = std::move(*unloadedURI);
  }
  if (loadedURI) {
    timing->mLoadedURI = std::move(*loadedURI);
  }
  *aResult = std::move(timing);
  return true;
}
