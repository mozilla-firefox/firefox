/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PerformanceTiming_h
#define mozilla_dom_PerformanceTiming_h

#include "CacheablePerformanceTimingData.h"
#include "Performance.h"
#include "ipc/IPCMessageUtils.h"
#include "ipc/IPCMessageUtilsSpecializations.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/PerformanceTimingTypes.h"
#include "mozilla/net/nsServerTiming.h"
#include "nsContentUtils.h"
#include "nsDOMNavigationTiming.h"
#include "nsITimedChannel.h"
#include "nsRFPService.h"
#include "nsWrapperCache.h"

class nsIHttpChannel;

namespace mozilla::dom {

class PerformanceTiming;
enum class RenderBlockingStatusType : uint8_t;

class PerformanceTimingData final : public CacheablePerformanceTimingData {
  friend class PerformanceTiming;
  friend struct IPC::ParamTraits<mozilla::dom::PerformanceTimingData>;

  // https://w3c.github.io/resource-timing/#dom-performanceresourcetiming-transfersize
  // The transferSize getter steps are to perform the following steps:
  //   1. If this's cache mode is "local", then return 0.
  static constexpr uint64_t kLocalCacheTransferSize = 0;

 public:
  PerformanceTimingData() = default;  // For deserialization
  // This can return null.
  static PerformanceTimingData* Create(nsITimedChannel* aChannel,
                                       nsIHttpChannel* aHttpChannel,
                                       DOMHighResTimeStamp aZeroTime,
                                       nsAString& aInitiatorType,
                                       nsAString& aEntryName);

  PerformanceTimingData(nsITimedChannel* aChannel, nsIHttpChannel* aHttpChannel,
                        DOMHighResTimeStamp aZeroTime);

  static PerformanceTimingData* Create(
      const CacheablePerformanceTimingData& aCachedData,
      DOMHighResTimeStamp aZeroTime, TimeStamp aStartTime, TimeStamp aEndTime,
      RenderBlockingStatusType aRenderBlockingStatus);

 private:
  PerformanceTimingData(const CacheablePerformanceTimingData& aCachedData,
                        DOMHighResTimeStamp aZeroTime, TimeStamp aStartTime,
                        TimeStamp aEndTime,
                        RenderBlockingStatusType aRenderBlockingStatus);

 public:
  explicit PerformanceTimingData(const IPCPerformanceTimingData& aIPCData);

  IPCPerformanceTimingData ToIPC();

  void SetPropertiesFromHttpChannel(nsIHttpChannel* aHttpChannel,
                                    nsITimedChannel* aChannel);

 private:
  void SetTransferSizeFromHttpChannel(nsIHttpChannel* aHttpChannel);

 public:
  uint64_t TransferSize() const { return mTransferSize; }

  /**
   * @param   aStamp
   *          The TimeStamp recorded for a specific event. This TimeStamp can
   *          be null.
   * @return  the duration of an event with a given TimeStamp, relative to the
   *          navigationStart TimeStamp (the moment the user landed on the
   *          page), if the given TimeStamp is valid. Otherwise, it will return
   *          the FetchStart timing value.
   */
  inline DOMHighResTimeStamp TimeStampToReducedDOMHighResOrFetchStart(
      Performance* aPerformance, TimeStamp aStamp) {
    MOZ_ASSERT(aPerformance);

    if (aStamp.IsNull()) {
      return FetchStartHighRes(aPerformance);
    }

    DOMHighResTimeStamp rawTimestamp =
        TimeStampToDOMHighRes(aPerformance, aStamp);

    return nsRFPService::ReduceTimePrecisionAsMSecs(
        rawTimestamp, aPerformance->GetRandomTimelineSeed(),
        aPerformance->GetRTPCallerType());
  }

  /**
   * The nsITimedChannel records an absolute timestamp for each event.
   * The nsDOMNavigationTiming will record the moment when the user landed on
   * the page. This is a window.performance unique timestamp, so it can be used
   * for all the events (navigation timing and resource timing events).
   *
   * The algorithm operates in 2 steps:
   * 1. The first step is to subtract the two timestamps: the argument (the
   * event's timestamp) and the navigation start timestamp. This will result in
   * a relative timestamp of the event (relative to the navigation start -
   * window.performance.timing.navigationStart).
   * 2. The second step is to add any required offset (the mZeroTime). For now,
   * this offset value is either 0 (for the resource timing), or equal to
   * "performance.navigationStart" (for navigation timing).
   * For the resource timing, mZeroTime is set to 0, causing the result to be a
   * relative time.
   * For the navigation timing, mZeroTime is set to
   * "performance.navigationStart" causing the result be an absolute time.
   *
   * @param   aStamp
   *          The TimeStamp recorded for a specific event. This TimeStamp can't
   *          be null.
   * @return  number of milliseconds value as one of:
   * - relative to the navigation start time, time the user has landed on the
   *   page
   * - an absolute wall clock time since the unix epoch
   */
  inline DOMHighResTimeStamp TimeStampToDOMHighRes(Performance* aPerformance,
                                                   TimeStamp aStamp) const {
    MOZ_ASSERT(aPerformance);
    MOZ_ASSERT(!aStamp.IsNull());

    TimeDuration duration = aStamp - aPerformance->CreationTimeStamp();
    return duration.ToMilliseconds() + mZeroTime;
  }

  // The last channel's AsyncOpen time.  This may occur before the FetchStart
  // in some cases.
  DOMHighResTimeStamp AsyncOpenHighRes(Performance* aPerformance);

  // High resolution (used by resource timing)
  DOMHighResTimeStamp WorkerStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp FetchStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp RedirectStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp RedirectEndHighRes(Performance* aPerformance);
  DOMHighResTimeStamp DomainLookupStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp DomainLookupEndHighRes(Performance* aPerformance);
  DOMHighResTimeStamp ConnectStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp SecureConnectionStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp ConnectEndHighRes(Performance* aPerformance);
  DOMHighResTimeStamp RequestStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp ResponseStartHighRes(Performance* aPerformance);
  DOMHighResTimeStamp FirstInterimResponseStartHighRes(
      Performance* aPerformance);
  DOMHighResTimeStamp FinalResponseHeadersStartHighRes(
      Performance* aPerformance);
  DOMHighResTimeStamp ResponseEndHighRes(Performance* aPerformance);

  DOMHighResTimeStamp ZeroTime() const { return mZeroTime; }

  // If this is false the values of redirectStart/End will be 0 This is false if
  // no redirects occured, or if any of the responses failed the
  // timing-allow-origin check in HttpBaseChannel::TimingAllowCheck
  //
  // If aEnsureSameOriginAndIgnoreTAO is false, it checks if all redirects pass
  // TAO. When it is true, it checks if all redirects are same-origin and
  // ignores the result of TAO.
  bool ShouldReportCrossOriginRedirect(
      bool aEnsureSameOriginAndIgnoreTAO) const;

  RenderBlockingStatusType RenderBlockingStatus() const {
    return mRenderBlockingStatus;
  }

 private:
  TimeStamp mAsyncOpen;
  TimeStamp mRedirectStart;
  TimeStamp mRedirectEnd;
  TimeStamp mDomainLookupStart;
  TimeStamp mDomainLookupEnd;
  TimeStamp mConnectStart;
  TimeStamp mSecureConnectionStart;
  TimeStamp mConnectEnd;
  TimeStamp mRequestStart;
  TimeStamp mResponseStart;
  TimeStamp mFirstInterimResponseStart;
  TimeStamp mFinalResponseHeadersStart;
  TimeStamp mCacheReadStart;
  TimeStamp mResponseEnd;
  TimeStamp mCacheReadEnd;

  // ServiceWorker interception timing information
  TimeStamp mWorkerStart;
  TimeStamp mWorkerRequestStart;
  TimeStamp mWorkerResponseEnd;

  // This is an offset that will be added to each timing ([ms] resolution).
  // There are only 2 possible values: (1) logicaly equal to navigationStart
  // TimeStamp (results are absolute timstamps - wallclock); (2) "0" (results
  // are relative to the navigation start).
  DOMHighResTimeStamp mZeroTime = 0;

  DOMHighResTimeStamp mFetchStart = 0;

  uint64_t mTransferSize = 0;

  RenderBlockingStatusType mRenderBlockingStatus;
};

// Script "performance.timing" object
class PerformanceTiming final : public nsWrapperCache {
 public:
  /**
   * @param   aPerformance
   *          The performance object (the JS parent).
   *          This will allow access to "window.performance.timing" attribute
   * for the navigation timing (can't be null).
   * @param   aChannel
   *          An nsITimedChannel used to gather all the networking timings by
   * both the navigation timing and the resource timing (can't be null).
   * @param   aHttpChannel
   *          An nsIHttpChannel (the resource's http channel).
   *          This will be used by the resource timing cross-domain check
   *          algorithm.
   *          Argument is null for the navigation timing (navigation timing uses
   *          another algorithm for the cross-domain redirects).
   * @param   aZeroTime
   *          The offset that will be added to the timestamp of each event. This
   *          argument should be equal to performance.navigationStart for
   *          navigation timing and "0" for the resource timing.
   */
  PerformanceTiming(Performance* aPerformance, nsITimedChannel* aChannel,
                    nsIHttpChannel* aHttpChannel,
                    DOMHighResTimeStamp aZeroTime);
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(PerformanceTiming)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(PerformanceTiming)

  nsDOMNavigationTiming* GetDOMTiming() const {
    return mPerformance->GetDOMTiming();
  }

  Performance* GetParentObject() const { return mPerformance; }

  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> aGivenProto) override;

  // PerformanceNavigation WebIDL methods
  DOMTimeMilliSec NavigationStart() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetNavigationStart(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec UnloadEventStart() {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetUnloadEventStart(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec UnloadEventEnd() {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetUnloadEventEnd(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  // Low resolution (used by navigation timing)
  DOMTimeMilliSec FetchStart();
  DOMTimeMilliSec RedirectStart();
  DOMTimeMilliSec RedirectEnd();
  DOMTimeMilliSec DomainLookupStart();
  DOMTimeMilliSec DomainLookupEnd();
  DOMTimeMilliSec ConnectStart();
  DOMTimeMilliSec SecureConnectionStart();
  DOMTimeMilliSec ConnectEnd();
  DOMTimeMilliSec RequestStart();
  DOMTimeMilliSec ResponseStart();
  DOMTimeMilliSec ResponseEnd();

  DOMTimeMilliSec DomLoading() {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetDomLoading(), mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec DomInteractive() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetDomInteractive(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec DomContentLoadedEventStart() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetDomContentLoadedEventStart(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec DomContentLoadedEventEnd() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetDomContentLoadedEventEnd(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec DomComplete() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetDomComplete(), mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec LoadEventStart() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetLoadEventStart(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec LoadEventEnd() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetLoadEventEnd(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec TimeToNonBlankPaint() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetTimeToNonBlankPaint(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec TimeToContentfulPaint() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetTimeToContentfulComposite(),
        mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  DOMTimeMilliSec TimeToFirstInteractive() const {
    if (!StaticPrefs::dom_enable_performance()) {
      return 0;
    }
    return nsRFPService::ReduceTimePrecisionAsMSecs(
        GetDOMTiming()->GetTimeToTTFI(), mPerformance->GetRandomTimelineSeed(),
        mPerformance->GetRTPCallerType());
  }

  PerformanceTimingData* Data() const { return mTimingData.get(); }

 private:
  ~PerformanceTiming();

  bool IsTopLevelContentDocument() const;

  RefPtr<Performance> mPerformance;

  UniquePtr<PerformanceTimingData> mTimingData;
};

}  // namespace mozilla::dom

namespace IPC {

template <>
struct ParamTraits<mozilla::dom::PerformanceTimingData> {
  using paramType = mozilla::dom::PerformanceTimingData;
  static void Write(IPC::MessageWriter* aWriter, const paramType& aParam) {
    WriteParam(aWriter, aParam.mServerTiming);
    WriteParam(aWriter, aParam.mNextHopProtocol);
    WriteParam(aWriter, aParam.mAsyncOpen);
    WriteParam(aWriter, aParam.mRedirectStart);
    WriteParam(aWriter, aParam.mRedirectEnd);
    WriteParam(aWriter, aParam.mDomainLookupStart);
    WriteParam(aWriter, aParam.mDomainLookupEnd);
    WriteParam(aWriter, aParam.mConnectStart);
    WriteParam(aWriter, aParam.mSecureConnectionStart);
    WriteParam(aWriter, aParam.mConnectEnd);
    WriteParam(aWriter, aParam.mRequestStart);
    WriteParam(aWriter, aParam.mResponseStart);
    WriteParam(aWriter, aParam.mFirstInterimResponseStart);
    WriteParam(aWriter, aParam.mFinalResponseHeadersStart);
    WriteParam(aWriter, aParam.mCacheReadStart);
    WriteParam(aWriter, aParam.mResponseEnd);
    WriteParam(aWriter, aParam.mCacheReadEnd);
    WriteParam(aWriter, aParam.mWorkerStart);
    WriteParam(aWriter, aParam.mWorkerRequestStart);
    WriteParam(aWriter, aParam.mWorkerResponseEnd);
    WriteParam(aWriter, aParam.mZeroTime);
    WriteParam(aWriter, aParam.mFetchStart);
    WriteParam(aWriter, aParam.mEncodedBodySize);
    WriteParam(aWriter, aParam.mTransferSize);
    WriteParam(aWriter, aParam.mDecodedBodySize);
    WriteParam(aWriter, aParam.mResponseStatus);
    WriteParam(aWriter, aParam.mRedirectCount);
    WriteParam(aWriter, aParam.mContentType);
    WriteParam(aWriter, aParam.mAllRedirectsSameOrigin);
    WriteParam(aWriter, aParam.mAllRedirectsPassTAO);
    WriteParam(aWriter, aParam.mSecureConnection);
    WriteParam(aWriter, aParam.mBodyInfoAccessAllowed);
    WriteParam(aWriter, aParam.mTimingAllowed);
    WriteParam(aWriter, aParam.mInitialized);
  }

  static bool Read(IPC::MessageReader* aReader, paramType* aResult) {
    return ReadParam(aReader, &aResult->mServerTiming) &&
           ReadParam(aReader, &aResult->mNextHopProtocol) &&
           ReadParam(aReader, &aResult->mAsyncOpen) &&
           ReadParam(aReader, &aResult->mRedirectStart) &&
           ReadParam(aReader, &aResult->mRedirectEnd) &&
           ReadParam(aReader, &aResult->mDomainLookupStart) &&
           ReadParam(aReader, &aResult->mDomainLookupEnd) &&
           ReadParam(aReader, &aResult->mConnectStart) &&
           ReadParam(aReader, &aResult->mSecureConnectionStart) &&
           ReadParam(aReader, &aResult->mConnectEnd) &&
           ReadParam(aReader, &aResult->mRequestStart) &&
           ReadParam(aReader, &aResult->mResponseStart) &&
           ReadParam(aReader, &aResult->mFirstInterimResponseStart) &&
           ReadParam(aReader, &aResult->mFinalResponseHeadersStart) &&
           ReadParam(aReader, &aResult->mCacheReadStart) &&
           ReadParam(aReader, &aResult->mResponseEnd) &&
           ReadParam(aReader, &aResult->mCacheReadEnd) &&
           ReadParam(aReader, &aResult->mWorkerStart) &&
           ReadParam(aReader, &aResult->mWorkerRequestStart) &&
           ReadParam(aReader, &aResult->mWorkerResponseEnd) &&
           ReadParam(aReader, &aResult->mZeroTime) &&
           ReadParam(aReader, &aResult->mFetchStart) &&
           ReadParam(aReader, &aResult->mEncodedBodySize) &&
           ReadParam(aReader, &aResult->mTransferSize) &&
           ReadParam(aReader, &aResult->mDecodedBodySize) &&
           ReadParam(aReader, &aResult->mResponseStatus) &&
           ReadParam(aReader, &aResult->mRedirectCount) &&
           ReadParam(aReader, &aResult->mContentType) &&
           ReadParam(aReader, &aResult->mAllRedirectsSameOrigin) &&
           ReadParam(aReader, &aResult->mAllRedirectsPassTAO) &&
           ReadParam(aReader, &aResult->mSecureConnection) &&
           ReadParam(aReader, &aResult->mBodyInfoAccessAllowed) &&
           ReadParam(aReader, &aResult->mTimingAllowed) &&
           ReadParam(aReader, &aResult->mInitialized);
  }
};

template <>
struct ParamTraits<nsIServerTiming*> {
  static void Write(IPC::MessageWriter* aWriter, nsIServerTiming* aParam) {
    nsAutoCString name;
    (void)aParam->GetName(name);
    double duration = 0;
    (void)aParam->GetDuration(&duration);
    nsAutoCString description;
    (void)aParam->GetDescription(description);
    WriteParam(aWriter, name);
    WriteParam(aWriter, duration);
    WriteParam(aWriter, description);
  }

  static bool Read(IPC::MessageReader* aReader,
                   RefPtr<nsIServerTiming>* aResult) {
    nsAutoCString name;
    double duration;
    nsAutoCString description;
    if (!ReadParam(aReader, &name) || !ReadParam(aReader, &duration) ||
        !ReadParam(aReader, &description)) {
      return false;
    }

    RefPtr<nsServerTiming> timing = new nsServerTiming();
    timing->SetName(name);
    timing->SetDuration(duration);
    timing->SetDescription(description);
    *aResult = timing.forget();
    return true;
  }
};

}  // namespace IPC

#endif  // mozilla_dom_PerformanceTiming_h
