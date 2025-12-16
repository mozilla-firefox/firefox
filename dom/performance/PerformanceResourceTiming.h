/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_PerformanceResourceTiming_h___
#define mozilla_dom_PerformanceResourceTiming_h___

#include "Performance.h"
#include "PerformanceEntry.h"
#include "PerformanceServerTiming.h"
#include "PerformanceTiming.h"
#include "mozilla/UniquePtr.h"
#include "nsCOMPtr.h"

namespace mozilla::dom {
enum class RenderBlockingStatusType : uint8_t;

#define IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(name)                \
  DOMHighResTimeStamp name(nsIPrincipal& aSubjectPrincipal) const {         \
    bool allowed = !mTimingData->RedirectCountReal()                        \
                       ? TimingAllowedForCaller(aSubjectPrincipal)          \
                       : ReportRedirectForCaller(aSubjectPrincipal, false); \
    return allowed ? mTimingData->name##HighRes(mPerformance) : 0;          \
  }

#define IMPL_RESOURCE_TIMING_TAO_PROTECTED_SIZE_PROP(name)                  \
  uint64_t name(nsIPrincipal& aSubjectPrincipal) const {                    \
    bool allowed = !mTimingData->RedirectCountReal()                        \
                       ? TimingAllowedForCaller(aSubjectPrincipal)          \
                       : ReportRedirectForCaller(aSubjectPrincipal, false); \
    return allowed ? mTimingData->name() : 0;                               \
  }

#define IMPL_RESOURCE_TIMING_CORS_PROTECTED_SIZE_PROP(name)  \
  uint64_t name(nsIPrincipal& aSubjectPrincipal) const {     \
    if (BodyInfoAccessAllowedForCaller(aSubjectPrincipal)) { \
      return mTimingData->name();                            \
    }                                                        \
    return 0;                                                \
  }

// http://www.w3.org/TR/resource-timing/#performanceresourcetiming
class PerformanceResourceTiming : public PerformanceEntry {
 public:
  using TimeStamp = mozilla::TimeStamp;

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(
      PerformanceResourceTiming, PerformanceEntry)

  // aPerformanceTimingData and aPerformance must be non-null
  PerformanceResourceTiming(
      UniquePtr<PerformanceTimingData>&& aPerformanceTimingData,
      Performance* aPerformance, const nsAString& aName);

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  virtual DOMHighResTimeStamp StartTime() const override;

  virtual DOMHighResTimeStamp Duration() const override {
    return ResponseEnd() - StartTime();
  }

  void GetInitiatorType(nsAString& aInitiatorType) const {
    aInitiatorType = mInitiatorType;
  }

  void SetInitiatorType(const nsAString& aInitiatorType) {
    mInitiatorType = aInitiatorType;
  }

  RenderBlockingStatusType RenderBlockingStatus() const;

  void GetNextHopProtocol(nsAString& aNextHopProtocol) const {
    if (mTimingData->TimingAllowed()) {
      aNextHopProtocol = mTimingData->NextHopProtocol();
    }
  }

  DOMHighResTimeStamp WorkerStart() const {
    return mTimingData->WorkerStartHighRes(mPerformance);
  }

  DOMHighResTimeStamp FetchStart() const;

  DOMHighResTimeStamp RedirectStart(nsIPrincipal& aSubjectPrincipal,
                                    bool aEnsureSameOriginAndIgnoreTAO) const {
    // We have to check if all the redirect URIs whether had the same origin or
    // different origins with TAO headers set (since there is no check in
    // RedirectStartHighRes())
    return ReportRedirectForCaller(aSubjectPrincipal,
                                   aEnsureSameOriginAndIgnoreTAO)
               ? mTimingData->RedirectStartHighRes(mPerformance)
               : 0;
  }

  virtual DOMHighResTimeStamp RedirectStart(
      nsIPrincipal& aSubjectPrincipal) const {
    return RedirectStart(aSubjectPrincipal,
                         false /* aEnsureSameOriginAndIgnoreTAO */);
  }

  DOMHighResTimeStamp RedirectEnd(nsIPrincipal& aSubjectPrincipal,
                                  bool aEnsureSameOriginAndIgnoreTAO) const {
    // We have to check if all the redirect URIs whether had the same origin or
    // different origins with TAO headers set (since there is no check in
    // RedirectEndHighRes())
    return ReportRedirectForCaller(aSubjectPrincipal,
                                   aEnsureSameOriginAndIgnoreTAO)
               ? mTimingData->RedirectEndHighRes(mPerformance)
               : 0;
  }

  virtual DOMHighResTimeStamp RedirectEnd(
      nsIPrincipal& aSubjectPrincipal) const {
    return RedirectEnd(aSubjectPrincipal,
                       false /* aEnsureSameOriginAndIgnoreTAO */);
  }

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(DomainLookupStart)

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(DomainLookupEnd)

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(ConnectStart)

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(ConnectEnd)

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(RequestStart)

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(ResponseStart)

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(FirstInterimResponseStart)

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(FinalResponseHeadersStart)

  DOMHighResTimeStamp ResponseEnd() const {
    return mTimingData->ResponseEndHighRes(mPerformance);
  }

  IMPL_RESOURCE_TIMING_TAO_PROTECTED_TIMING_PROP(SecureConnectionStart)

  virtual const PerformanceResourceTiming* ToResourceTiming() const override {
    return this;
  }

  IMPL_RESOURCE_TIMING_CORS_PROTECTED_SIZE_PROP(EncodedBodySize);
  IMPL_RESOURCE_TIMING_CORS_PROTECTED_SIZE_PROP(DecodedBodySize);

  uint64_t TransferSize(nsIPrincipal& aSubjectPrincipal) const {
    const bool allowed =
        !mTimingData->RedirectCountReal()
            ? TimingAllowedForCaller(aSubjectPrincipal)
            : ReportRedirectForCaller(aSubjectPrincipal, false);
    if (!allowed) {
      return 0;
    }
    // Resource is cached.
    if (!mTimingData->TransferSize()) {
      return 0;
    }
    auto encodedBodySize = EncodedBodySize(aSubjectPrincipal);
    // The constant number added to transferSize replaces exposing the
    // total byte size of the HTTP headers, as that may expose the
    // presence of certain cookies.
    // https://github.com/w3c/resource-timing/issues/238
    return encodedBodySize + 300;
  }

  uint16_t ResponseStatus(nsIPrincipal& aSubjectPrincipal) const {
    if (BodyInfoAccessAllowedForCaller(aSubjectPrincipal)) {
      return mTimingData->ResponseStatus();
    }
    return 0;
  }

  void GetContentType(nsAString& aContentType,
                      nsIPrincipal& aSubjectPrincipal) const {
    if (BodyInfoAccessAllowedForCaller(aSubjectPrincipal) ==
        nsITimedChannel::BodyInfoAccess::ALLOW_ALL) {
      aContentType = mTimingData->ContentType();
    }
  }

  void GetServerTiming(nsTArray<RefPtr<PerformanceServerTiming>>& aRetval,
                       nsIPrincipal& aSubjectPrincipal);

  size_t SizeOfIncludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) const override;

 protected:
  virtual ~PerformanceResourceTiming();

  size_t SizeOfExcludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) const override;

  // Check if caller has access to the bodyInfo of the request, per Fetch spec.
  nsITimedChannel::BodyInfoAccess BodyInfoAccessAllowedForCaller(
      nsIPrincipal& aCaller) const;

  // Check if caller has access to cross-origin timings, either by the rules
  // from the spec, or based on addon permissions.
  bool TimingAllowedForCaller(nsIPrincipal& aCaller) const;

  // Check if cross-origin redirects should be reported to the caller.
  bool ReportRedirectForCaller(nsIPrincipal& aCaller,
                               bool aEnsureSameOriginAndIgnoreTAO) const;

  nsString mInitiatorType;
  const UniquePtr<PerformanceTimingData> mTimingData;  // always non-null
  RefPtr<Performance> mPerformance;

  // The same initial requested URI as the `name` attribute.
  nsCOMPtr<nsIURI> mOriginalURI;

 private:
  mutable Maybe<DOMHighResTimeStamp> mCachedStartTime;
};

}  // namespace mozilla::dom

#endif /* mozilla_dom_PerformanceResourceTiming_h___ */
