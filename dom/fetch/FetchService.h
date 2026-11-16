/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef _mozilla_dom_FetchService_h
#define _mozilla_dom_FetchService_h

#include "mozilla/ErrorResult.h"
#include "mozilla/MozPromise.h"
#include "mozilla/RefPtr.h"
#include "mozilla/dom/FetchDriver.h"
#include "mozilla/dom/FetchTypes.h"
#include "mozilla/dom/PerformanceTimingTypes.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsIChannel.h"
#include "nsIObserver.h"
#include "nsTHashMap.h"

class nsILoadGroup;
class nsIPrincipal;
class nsICookieJarSettings;
class PerformanceStorage;

namespace mozilla::dom {

class InternalRequest;
class InternalResponse;
class ClientInfo;
class ServiceWorkerDescriptor;

using FetchServiceResponse = SafeRefPtr<InternalResponse>;
using FetchServiceResponseAvailablePromise =
    MozPromise<FetchServiceResponse, CopyableErrorResult, true>;
using FetchServiceResponseTimingPromise =
    MozPromise<ResponseTiming, CopyableErrorResult, true>;
using FetchServiceResponseEndPromise =
    MozPromise<ResponseEndArgs, CopyableErrorResult, true>;

class FetchServicePromises final {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FetchServicePromises);

 public:
  FetchServicePromises();

  RefPtr<FetchServiceResponseAvailablePromise> GetResponseAvailablePromise();
  RefPtr<FetchServiceResponseTimingPromise> GetResponseTimingPromise();
  RefPtr<FetchServiceResponseEndPromise> GetResponseEndPromise();

  bool IsResponseAvailablePromiseResolved() {
    return mAvailablePromiseResolved;
  }
  bool IsResponseTimingPromiseResolved() { return mTimingPromiseResolved; }
  bool IsResponseEndPromiseResolved() { return mEndPromiseResolved; }

  void ResolveResponseAvailablePromise(FetchServiceResponse&& aResponse,
                                       StaticString aMethodName);
  void RejectResponseAvailablePromise(const CopyableErrorResult&& aError,
                                      StaticString aMethodName);
  void ResolveResponseTimingPromise(ResponseTiming&& aTiming,
                                    StaticString aMethodName);
  void RejectResponseTimingPromise(const CopyableErrorResult&& aError,
                                   StaticString aMethodName);
  void ResolveResponseEndPromise(ResponseEndArgs&& aArgs,
                                 StaticString aMethodName);
  void RejectResponseEndPromise(const CopyableErrorResult&& aError,
                                StaticString aMethodName);

 private:
  ~FetchServicePromises() = default;

  RefPtr<FetchServiceResponseAvailablePromise::Private> mAvailablePromise;
  RefPtr<FetchServiceResponseTimingPromise::Private> mTimingPromise;
  RefPtr<FetchServiceResponseEndPromise::Private> mEndPromise;

  // The MozPromise interface intentionally does not expose synchronous access
  // to the internal resolved/rejected state. Instead, we track whether or not
  // we've called Resolve on the FetchServicePromises.
  bool mAvailablePromiseResolved = false;
  bool mTimingPromiseResolved = false;
  bool mEndPromiseResolved = false;
};

/**
 * FetchService is a singleton object which designed to be used in parent
 * process main thread only. It is used to handle the special fetch requests
 * from ServiceWorkers(by Navigation Preload) and PFetch.
 *
 * FetchService creates FetchInstance internally to represent each Fetch
 * request. It supports an asynchronous fetching, FetchServicePromises is
 * created when a Fetch starts, once the response is ready or any error happens,
 * the FetchServicePromises would be resolved or rejected. The promises
 * consumers can set callbacks to handle the Fetch result.
 */
class FetchService final : public nsIObserver {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  // Used for ServiceWorkerNavigationPreload
  struct NavigationPreloadArgs {
    SafeRefPtr<InternalRequest> mRequest;
    nsCOMPtr<nsIChannel> mChannel;
  };

  // Used for content process worker thread fetch()
  struct WorkerFetchArgs {
    SafeRefPtr<InternalRequest> mRequest;
    nsCOMPtr<nsIPrincipal> mPrincipal;
    nsCString mWorkerScript;
    ClientInfo mClientInfo;
    Maybe<ServiceWorkerDescriptor> mController;
    Maybe<net::CookieJarSettingsArgs> mCookieJarSettings;
    bool mNeedOnDataAvailable;
    nsCOMPtr<nsICSPEventListener> mCSPEventListener;
    uint64_t mAssociatedBrowsingContextID;
    nsCOMPtr<nsISerialEventTarget> mEventTarget;
    nsID mActorID;
    bool mIsThirdPartyContext;
    MozPromiseRequestHolder<FetchServiceResponseEndPromise>
        mResponseEndPromiseHolder;
    RefPtr<GenericPromise::Private> mFetchParentPromise;
    bool mIsOn3PCBExceptionList;
  };

  // Used for content process main thread fetch()
  // Currently this is just used for keepalive request
  // This would be further used for sending all main thread fetch requests
  // through PFetch
  // See Bug 1897129.
  struct MainThreadFetchArgs {
    SafeRefPtr<InternalRequest> mRequest;
    nsCOMPtr<nsIPrincipal> mPrincipal;
    ClientInfo mClientInfo;
    Maybe<net::CookieJarSettingsArgs> mCookieJarSettings;
    bool mNeedOnDataAvailable;
    nsCOMPtr<nsICSPEventListener> mCSPEventListener;
    uint64_t mAssociatedBrowsingContextID;
    nsCOMPtr<nsISerialEventTarget> mEventTarget;
    nsID mActorID;
    bool mIsThirdPartyContext{false};
  };

  struct UnknownArgs {};

  using FetchArgs = Variant<NavigationPreloadArgs, WorkerFetchArgs,
                            MainThreadFetchArgs, UnknownArgs>;

  enum class FetchArgsType {
    NavigationPreload,
    WorkerFetch,
    MainThreadFetch,
    Unknown,
  };
  static already_AddRefed<FetchService> GetInstance();

  static RefPtr<FetchServicePromises> NetworkErrorResponse(
      nsresult aRv, const FetchArgs& aArgs = AsVariant(UnknownArgs{}));

  FetchService();

  // This method creates a FetchInstance to trigger fetch.
  // The created FetchInstance is saved in mFetchInstanceTable
  RefPtr<FetchServicePromises> Fetch(FetchArgs&& aArgs);

  void CancelFetch(const RefPtr<FetchServicePromises>&& aPromises,
                   bool aForceAbort);

  MozPromiseRequestHolder<FetchServiceResponseEndPromise>&
  GetResponseEndPromiseHolder(const RefPtr<FetchServicePromises>& aPromises);

 private:
  /**
   * FetchInstance is an internal representation for each Fetch created by
   * FetchService.
   * FetchInstance is also a FetchDriverObserver which has responsibility to
   * resolve/reject the FetchServicePromises.
   * FetchInstance triggers fetch by instancing a FetchDriver with proper
   * initialization. The general usage flow of FetchInstance is as follows
   *
   * RefPtr<FetchInstance> fetch = MakeRefPtr<FetchInstance>();
   * fetch->Initialize(FetchArgs args);
   * RefPtr<FetchServicePromises> fetch->Fetch();
   */
  class FetchInstance final : public FetchDriverObserver {
   public:
    FetchInstance() = default;

    nsresult Initialize(FetchArgs&& aArgs);

    const FetchArgs& Args() { return mArgs; }
    MozPromiseRequestHolder<FetchServiceResponseEndPromise>&
    GetResponseEndPromiseHolder() {
      MOZ_ASSERT(mArgs.is<WorkerFetchArgs>());
      return mArgs.as<WorkerFetchArgs>().mResponseEndPromiseHolder;
    }

    RefPtr<FetchServicePromises> Fetch();

    void Cancel(bool aForceAbort);

    bool IsLocalHostFetch() const;

    // True when the request must be offered to a service worker's fetch
    // handler before the network is considered, i.e. the client is controlled
    // and the request's service-workers mode is "all".
    bool IsServiceWorkerEligible() const;

    /* FetchDriverObserver interface */
    void OnResponseEnd(FetchDriverObserver::EndReason aReason,
                       JS::Handle<JS::Value> aReasonDetails) override;
    void OnResponseAvailableInternal(
        SafeRefPtr<InternalResponse> aResponse) override;
    bool NeedOnDataAvailable() override;
    void OnDataAvailable() override;
    void FlushConsoleReport() override;
    void OnReportPerformanceTiming() override;
    void OnNotifyNetworkMonitorAlternateStack(uint64_t aChannelID) override;

   private:
    ~FetchInstance() = default;
    nsCOMPtr<nsISerialEventTarget> GetBackgroundEventTarget();
    nsID GetActorID();

    SafeRefPtr<InternalRequest> mRequest;
    nsCOMPtr<nsIPrincipal> mPrincipal;
    nsCOMPtr<nsILoadGroup> mLoadGroup;
    nsCOMPtr<nsICookieJarSettings> mCookieJarSettings;
    RefPtr<PerformanceStorage> mPerformanceStorage;
    FetchArgs mArgs{AsVariant(FetchService::UnknownArgs())};
    RefPtr<FetchDriver> mFetchDriver;
    SafeRefPtr<InternalResponse> mResponse;
    RefPtr<FetchServicePromises> mPromises;
    FetchArgsType mArgsType;
    Atomic<bool> mActorDying{false};
  };

  ~FetchService();

  nsresult RegisterNetworkObserver();
  nsresult UnregisterNetworkObserver();

  // Update pending keepalive fetch requests count
  void IncrementKeepAliveRequestCount(const nsACString& aOrigin);
  void DecrementKeepAliveRequestCount(const nsACString& aOrigin);

  // Check if the number of pending keepalive fetch requests exceeds the
  // configured limit
  // We limit the number of pending keepalive requests on two levels:
  // 1. per origin - controlled by pref
  // dom.fetchKeepalive.request_limit_per_origin)
  // 2. per browser instance - controlled by pref
  // dom.fetchKeepalive.total_request_limit
  bool DoesExceedsKeepaliveResourceLimits(const nsACString& aOrigin);

  // This is a container to manage the generated fetches.
  nsTHashMap<nsRefPtrHashKey<FetchServicePromises>, RefPtr<FetchInstance> >
      mFetchInstanceTable;
  bool mObservingNetwork{false};
  bool mOffline{false};

  // map to key origin to number of pending keepalive fetch requests
  nsTHashMap<nsCStringHashKey, uint32_t> mPendingKeepAliveRequestsPerOrigin;

  // total pending keepalive fetch requests per browser instance
  uint32_t mTotalKeepAliveRequests{0};
};

}  // namespace mozilla::dom

#endif  // _mozilla_dom_FetchService_h
