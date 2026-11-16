/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FetchService.h"

#include "FetchLog.h"
#include "FetchParent.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/InternalRequest.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/PerformanceTiming.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/net/CookieJarSettings.h"
#include "nsContentUtils.h"
#include "nsIContentSecurityPolicy.h"
#include "nsICookieJarSettings.h"
#include "nsIIOService.h"
#include "nsILoadGroup.h"
#include "nsILoadInfo.h"
#include "nsIObserverService.h"
#include "nsIPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla::dom {

mozilla::LazyLogModule gFetchLog("Fetch");

// FetchServicePromises

FetchServicePromises::FetchServicePromises()
    : mAvailablePromise(
          MakeRefPtr<FetchServiceResponseAvailablePromise::Private>(__func__)),
      mTimingPromise(
          MakeRefPtr<FetchServiceResponseTimingPromise::Private>(__func__)),
      mEndPromise(
          MakeRefPtr<FetchServiceResponseEndPromise::Private>(__func__)) {
  mAvailablePromise->UseDirectTaskDispatch(__func__);
  mTimingPromise->UseDirectTaskDispatch(__func__);
  mEndPromise->UseDirectTaskDispatch(__func__);
}

RefPtr<FetchServiceResponseAvailablePromise>
FetchServicePromises::GetResponseAvailablePromise() {
  return mAvailablePromise;
}

RefPtr<FetchServiceResponseTimingPromise>
FetchServicePromises::GetResponseTimingPromise() {
  return mTimingPromise;
}

RefPtr<FetchServiceResponseEndPromise>
FetchServicePromises::GetResponseEndPromise() {
  return mEndPromise;
}

void FetchServicePromises::ResolveResponseAvailablePromise(
    FetchServiceResponse&& aResponse, StaticString aMethodName) {
  if (mAvailablePromise) {
    mAvailablePromiseResolved = true;
    mAvailablePromise->Resolve(std::move(aResponse), aMethodName);
  }
}

void FetchServicePromises::RejectResponseAvailablePromise(
    const CopyableErrorResult&& aError, StaticString aMethodName) {
  if (mAvailablePromise) {
    mAvailablePromise->Reject(aError, aMethodName);
  }
}

void FetchServicePromises::ResolveResponseTimingPromise(
    ResponseTiming&& aTiming, StaticString aMethodName) {
  if (mTimingPromise) {
    mTimingPromiseResolved = true;
    mTimingPromise->Resolve(std::move(aTiming), aMethodName);
  }
}

void FetchServicePromises::RejectResponseTimingPromise(
    const CopyableErrorResult&& aError, StaticString aMethodName) {
  if (mTimingPromise) {
    mTimingPromise->Reject(aError, aMethodName);
  }
}

void FetchServicePromises::ResolveResponseEndPromise(ResponseEndArgs&& aArgs,
                                                     StaticString aMethodName) {
  if (mEndPromise) {
    mEndPromiseResolved = true;
    mEndPromise->Resolve(std::move(aArgs), aMethodName);
  }
}

void FetchServicePromises::RejectResponseEndPromise(
    const CopyableErrorResult&& aError, StaticString aMethodName) {
  if (mEndPromise) {
    mEndPromise->Reject(aError, aMethodName);
  }
}

// FetchInstance

nsresult FetchService::FetchInstance::Initialize(FetchArgs&& aArgs) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aArgs.is<UnknownArgs>() && mArgs.is<UnknownArgs>());

  mArgs = std::move(aArgs);

  // Get needed information for FetchDriver from passed-in channel.
  if (mArgs.is<NavigationPreloadArgs>()) {
    mRequest = mArgs.as<NavigationPreloadArgs>().mRequest.clonePtr();
    mArgsType = FetchArgsType::NavigationPreload;
    nsIChannel* channel = mArgs.as<NavigationPreloadArgs>().mChannel;
    FETCH_LOG(("FetchInstance::Initialize [%p] request[%p], channel[%p]", this,
               mRequest.unsafeGetRawPtr(), channel));

    nsresult rv;
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    MOZ_ASSERT(loadInfo);

    nsCOMPtr<nsIURI> channelURI;
    rv = channel->GetURI(getter_AddRefs(channelURI));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    nsIScriptSecurityManager* securityManager =
        nsContentUtils::GetSecurityManager();
    if (securityManager) {
      securityManager->GetChannelResultPrincipal(channel,
                                                 getter_AddRefs(mPrincipal));
    }

    if (!mPrincipal) {
      return NS_ERROR_UNEXPECTED;
    }

    // Get loadGroup from channel
    rv = channel->GetLoadGroup(getter_AddRefs(mLoadGroup));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    if (!mLoadGroup) {
      rv = NS_NewLoadGroup(getter_AddRefs(mLoadGroup), mPrincipal);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }

    // Get CookieJarSettings from channel
    rv = loadInfo->GetCookieJarSettings(getter_AddRefs(mCookieJarSettings));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    // Get PerformanceStorage from channel
    mPerformanceStorage = loadInfo->GetPerformanceStorage();
  } else if (mArgs.is<MainThreadFetchArgs>()) {
    mArgsType = FetchArgsType::MainThreadFetch;

    mRequest = mArgs.as<MainThreadFetchArgs>().mRequest.clonePtr();

    FETCH_LOG(("FetchInstance::Initialize [%p] request[%p]", this,
               mRequest.unsafeGetRawPtr()));

    mPrincipal = mArgs.as<MainThreadFetchArgs>().mPrincipal;
    nsresult rv = NS_NewLoadGroup(getter_AddRefs(mLoadGroup), mPrincipal);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (mArgs.as<MainThreadFetchArgs>().mCookieJarSettings.isSome()) {
      net::CookieJarSettings::Deserialize(
          mArgs.as<MainThreadFetchArgs>().mCookieJarSettings.ref(),
          getter_AddRefs(mCookieJarSettings));
    }

    return NS_OK;

  } else {
    mRequest = mArgs.as<WorkerFetchArgs>().mRequest.clonePtr();
    mArgsType = FetchArgsType::WorkerFetch;

    FETCH_LOG(("FetchInstance::Initialize [%p] request[%p]", this,
               mRequest.unsafeGetRawPtr()));

    mPrincipal = mArgs.as<WorkerFetchArgs>().mPrincipal;
    nsresult rv = NS_NewLoadGroup(getter_AddRefs(mLoadGroup), mPrincipal);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (mArgs.as<WorkerFetchArgs>().mCookieJarSettings.isSome()) {
      net::CookieJarSettings::Deserialize(
          mArgs.as<WorkerFetchArgs>().mCookieJarSettings.ref(),
          getter_AddRefs(mCookieJarSettings));
    }
  }

  return NS_OK;
}

RefPtr<FetchServicePromises> FetchService::FetchInstance::Fetch() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(mPrincipal);
  MOZ_ASSERT(mLoadGroup);

  nsAutoCString principalSpec;
  MOZ_ALWAYS_SUCCEEDS(mPrincipal->GetAsciiSpec(principalSpec));
  nsCOMPtr<nsIURI> requestURL = mRequest->GetURL();
  FETCH_LOG(("FetchInstance::Fetch [%p], mRequest URL: %s mPrincipal: %s", this,
             requestURL->GetSpecOrDefault().get(), principalSpec.get()));

  nsresult rv;

  if (mRequest->GetKeepalive()) {
    nsAutoCString origin;
    MOZ_ASSERT(mPrincipal);
    mPrincipal->GetOrigin(origin);

    RefPtr<FetchService> fetchService = FetchService::GetInstance();
    MOZ_ASSERT(fetchService);
    if (fetchService->DoesExceedsKeepaliveResourceLimits(origin)) {
      FETCH_LOG(("FetchInstance::Fetch Keepalive request exceeds limit"));
      return FetchService::NetworkErrorResponse(NS_ERROR_DOM_ABORT_ERR, mArgs);
    }
    fetchService->IncrementKeepAliveRequestCount(origin);
  }

  // Create a FetchDriver instance
  mFetchDriver = MakeRefPtr<FetchDriver>(
      mRequest.clonePtr(),               // Fetch Request
      mPrincipal,                        // Principal
      mLoadGroup,                        // LoadGroup
      GetMainThreadSerialEventTarget(),  // MainThreadEventTarget
      mCookieJarSettings,                // CookieJarSettings
      mPerformanceStorage,               // PerformanceStorage
      // For service workers we set
      // tracking fetch to false, but for Keepalive
      // requests from main thread this needs to be
      // changed. See Bug 1892406
      net::ClassificationFlags({0, 0})  // TrackingFlags
  );

  if (mArgsType == FetchArgsType::WorkerFetch) {
    auto& args = mArgs.as<WorkerFetchArgs>();
    mFetchDriver->SetWorkerScript(args.mWorkerScript);
    mFetchDriver->SetClientInfo(args.mClientInfo);
    mFetchDriver->SetController(args.mController);
    if (args.mCSPEventListener) {
      mFetchDriver->SetCSPEventListener(args.mCSPEventListener);
    }
    mFetchDriver->SetAssociatedBrowsingContextID(
        args.mAssociatedBrowsingContextID);
    mFetchDriver->SetIsThirdPartyContext(Some(args.mIsThirdPartyContext));
    mFetchDriver->SetIsOn3PCBExceptionList(args.mIsOn3PCBExceptionList);
  }

  if (mArgsType == FetchArgsType::MainThreadFetch) {
    auto& args = mArgs.as<MainThreadFetchArgs>();
    mFetchDriver->SetClientInfo(args.mClientInfo);
    mFetchDriver->SetAssociatedBrowsingContextID(
        args.mAssociatedBrowsingContextID);
    mFetchDriver->SetIsThirdPartyContext(Some(args.mIsThirdPartyContext));
  }

  mFetchDriver->EnableNetworkInterceptControl();
  mPromises = MakeRefPtr<FetchServicePromises>();

  // Call FetchDriver::Fetch to start fetching.
  // Pass AbortSignalImpl as nullptr since we no need support AbortSignalImpl
  // with FetchService. AbortSignalImpl related information should be passed
  // through PFetch or InterceptedHttpChannel, then call
  // FetchService::CancelFetch() to abort the running fetch.
  rv = mFetchDriver->Fetch(nullptr, this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    FETCH_LOG(
        ("FetchInstance::Fetch FetchDriver::Fetch failed(0x%X)", (uint32_t)rv));
    return FetchService::NetworkErrorResponse(rv, mArgs);
  }

  return mPromises;
}

bool FetchService::FetchInstance::IsLocalHostFetch() const {
  if (!mPrincipal) {
    return false;
  }
  bool res;
  nsresult rv = mPrincipal->GetIsLoopbackHost(&res);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }
  return res;
}

bool FetchService::FetchInstance::IsServiceWorkerEligible() const {
  if (!mArgs.is<WorkerFetchArgs>() ||
      mArgs.as<WorkerFetchArgs>().mController.isNothing()) {
    return false;
  }
  return mRequest && !mRequest->SkipServiceWorker();
}

void FetchService::FetchInstance::Cancel(bool aForceAbort) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  FETCH_LOG(("FetchInstance::Cancel() [%p]", this));

  // If mFetchDriver is not null here, FetchInstance::Fetch() has already
  // started, let mFetchDriver::RunAbortAlgorithm() to call
  // FetchInstance::OnResponseEnd() to resolve the pending promises.
  // Otherwise, resolving the pending promises here.
  if (mFetchDriver) {
    // if keepalive is active and it is NOT user initiated Abort, then
    // do not cancel the request.
    if (mRequest->GetKeepalive() && !aForceAbort) {
      FETCH_LOG(("Cleaning up the worker for keepalive[%p]", this));

      MOZ_ASSERT(mArgs.is<WorkerFetchArgs>());
      if (mArgs.is<WorkerFetchArgs>()) {
        // delete the actors for cleanup for worker keep-alive requests.
        // Non-worker keepalive requests need actors to be active until request
        // completion, because we update request quota per load-group in
        // FetchChild::ActorDestroy.
        MOZ_ASSERT((mArgs.as<WorkerFetchArgs>().mFetchParentPromise));
        if (mArgs.as<WorkerFetchArgs>().mResponseEndPromiseHolder.Exists()) {
          FETCH_LOG(
              ("FetchInstance::Cancel() [%p] mResponseEndPromiseHolder exists",
               this));

          mArgs.as<WorkerFetchArgs>().mResponseEndPromiseHolder.Disconnect();

          // the parent promise resolution leads to deleting of actors
          // mActorDying prevents further access to FetchParent
          mActorDying = true;
          mArgs.as<WorkerFetchArgs>().mFetchParentPromise->Resolve(true,
                                                                   __func__);
        }
      }
      return;
    }
    mFetchDriver->RunAbortAlgorithm();
    return;
  }

  MOZ_ASSERT(mPromises);

  mPromises->ResolveResponseAvailablePromise(
      InternalResponse::NetworkError(NS_ERROR_DOM_ABORT_ERR), __func__);

  mPromises->ResolveResponseTimingPromise(ResponseTiming(), __func__);

  mPromises->ResolveResponseEndPromise(
      ResponseEndArgs(FetchDriverObserver::eAborted), __func__);
}

void FetchService::FetchInstance::OnResponseEnd(
    FetchDriverObserver::EndReason aReason,
    JS::Handle<JS::Value> aReasonDetails) {
  FETCH_LOG(("FetchInstance::OnResponseEnd [%p] %s", this,
             aReason == eAborted ? "eAborted" : "eNetworking"));

  if (mRequest->GetKeepalive()) {
    nsAutoCString origin;
    MOZ_ASSERT(mPrincipal);
    mPrincipal->GetOrigin(origin);
    RefPtr<FetchService> fetchService = FetchService::GetInstance();
    fetchService->DecrementKeepAliveRequestCount(origin);
  }

  MOZ_ASSERT(mRequest);
  if (mArgsType != FetchArgsType::NavigationPreload) {
    FlushConsoleReport();
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__,
        [endArgs = ResponseEndArgs(aReason), actorID = GetActorID()]() {
          FETCH_LOG(("FetchInstance::OnResponseEnd, Runnable"));
          RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
          if (actor) {
            actor->OnResponseEnd(std::move(endArgs));
          }
        });
    MOZ_ALWAYS_SUCCEEDS(
        GetBackgroundEventTarget()->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  }

  MOZ_ASSERT(mPromises);

  if (mArgs.is<WorkerFetchArgs>() &&
      mArgs.as<WorkerFetchArgs>().mResponseEndPromiseHolder.Exists()) {
    mArgs.as<WorkerFetchArgs>().mResponseEndPromiseHolder.Complete();
  }

  if (aReason == eAborted) {
    // If ResponseAvailablePromise has not resolved yet, resolved with
    // NS_ERROR_DOM_ABORT_ERR response. If the promise is already resolved,
    // this will have no effect.
    mPromises->ResolveResponseAvailablePromise(
        InternalResponse::NetworkError(NS_ERROR_DOM_ABORT_ERR), __func__);

    // If ResponseTimingPromise has not resolved yet, resolved with empty
    // ResponseTiming. If the promise is already resolved, this has no effect.
    mPromises->ResolveResponseTimingPromise(ResponseTiming(), __func__);
    // Resolve the ResponseEndPromise
    mPromises->ResolveResponseEndPromise(ResponseEndArgs(aReason), __func__);
    return;
  }

  MOZ_ASSERT(mPromises->IsResponseAvailablePromiseResolved() &&
             mPromises->IsResponseTimingPromiseResolved());

  // Resolve the ResponseEndPromise
  mPromises->ResolveResponseEndPromise(ResponseEndArgs(aReason), __func__);

  // Remove the FetchInstance from FetchInstanceTable
  RefPtr<FetchService> fetchService = FetchService::GetInstance();
  MOZ_ASSERT(fetchService);
  auto entry = fetchService->mFetchInstanceTable.Lookup(mPromises);
  if (entry) {
    entry.Remove();
    FETCH_LOG(
        ("FetchInstance::OnResponseEnd entry of responsePromise[%p] is "
         "removed",
         mPromises.get()));
  }
}

void FetchService::FetchInstance::OnResponseAvailableInternal(
    SafeRefPtr<InternalResponse> aResponse) {
  FETCH_LOG(("FetchInstance::OnResponseAvailableInternal [%p]", this));
  mResponse = std::move(aResponse);

  nsCOMPtr<nsIInputStream> body;
  mResponse->GetUnfilteredBody(getter_AddRefs(body));
  FETCH_LOG(
      ("FetchInstance::OnResponseAvailableInternal [%p] response body: %p",
       this, body.get()));
  MOZ_ASSERT(mRequest);

  if (mArgsType != FetchArgsType::NavigationPreload && !mActorDying) {
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__,
        [response = mResponse.clonePtr(), actorID = GetActorID()]() mutable {
          FETCH_LOG(("FetchInstance::OnResponseAvailableInternal Runnable"));
          RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
          if (actor) {
            actor->OnResponseAvailableInternal(std::move(response));
          }
        });
    MOZ_ALWAYS_SUCCEEDS(
        GetBackgroundEventTarget()->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  }

  MOZ_ASSERT(mPromises);

  // Resolve the ResponseAvailablePromise
  mPromises->ResolveResponseAvailablePromise(mResponse.clonePtr(), __func__);
}

bool FetchService::FetchInstance::NeedOnDataAvailable() {
  if (mArgs.is<WorkerFetchArgs>()) {
    return mArgs.as<WorkerFetchArgs>().mNeedOnDataAvailable;
  }

  if (mArgs.is<MainThreadFetchArgs>()) {
    return mArgs.as<MainThreadFetchArgs>().mNeedOnDataAvailable;
  }

  return false;
}

void FetchService::FetchInstance::OnDataAvailable() {
  FETCH_LOG(("FetchInstance::OnDataAvailable [%p]", this));

  if (!NeedOnDataAvailable()) {
    return;
  }

  MOZ_ASSERT(mRequest);

  if (mArgsType != FetchArgsType::NavigationPreload && !mActorDying) {
    nsCOMPtr<nsIRunnable> r =
        NS_NewRunnableFunction(__func__, [actorID = GetActorID()]() {
          FETCH_LOG(("FetchInstance::OnDataAvailable, Runnable"));
          RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
          if (actor) {
            actor->OnDataAvailable();
          }
        });
    MOZ_ALWAYS_SUCCEEDS(
        GetBackgroundEventTarget()->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  }
}

void FetchService::FetchInstance::FlushConsoleReport() {
  FETCH_LOG(("FetchInstance::FlushConsoleReport [%p]", this));

  if (mArgsType != FetchArgsType::NavigationPreload && !mActorDying) {
    if (!mReporter) {
      return;
    }
    nsTArray<net::ConsoleReportCollected> reports;
    mReporter->StealConsoleReports(reports);
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__,
        [actorID = GetActorID(), consoleReports = std::move(reports)]() {
          FETCH_LOG(("FetchInstance::FlushConsolReport, Runnable"));
          RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
          if (actor) {
            actor->OnFlushConsoleReport(std::move(consoleReports));
          }
        });
    MOZ_ALWAYS_SUCCEEDS(
        GetBackgroundEventTarget()->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  }
}

void FetchService::FetchInstance::OnReportPerformanceTiming() {
  FETCH_LOG(("FetchInstance::OnReportPerformanceTiming [%p]", this));
  MOZ_ASSERT(mFetchDriver);
  MOZ_ASSERT(mPromises);

  if (mPromises->IsResponseTimingPromiseResolved()) {
    return;
  }

  ResponseTiming timing;
  UniquePtr<PerformanceTimingData> performanceTiming(
      mFetchDriver->GetPerformanceTimingData(timing.initiatorType(),
                                             timing.entryName()));
  // FetchDriver has no corresponding performance timing when fetch() failed.
  // Resolve the ResponseTimingPromise with empty timing.
  if (!performanceTiming) {
    mPromises->ResolveResponseTimingPromise(ResponseTiming(), __func__);
    return;
  }
  timing.timingData() = performanceTiming->ToIPC();
  // Force replace initiatorType for ServiceWorkerNavgationPreload.
  if (mArgsType == FetchArgsType::NavigationPreload) {
    timing.initiatorType() = u"navigation"_ns;
  } else if (!mActorDying) {
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__, [actorID = GetActorID(), timing = timing]() {
          FETCH_LOG(("FetchInstance::OnReportPerformanceTiming, Runnable"));
          RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
          if (actor) {
            actor->OnReportPerformanceTiming(std::move(timing));
          }
        });
    MOZ_ALWAYS_SUCCEEDS(
        GetBackgroundEventTarget()->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  }

  mPromises->ResolveResponseTimingPromise(std::move(timing), __func__);
}

void FetchService::FetchInstance::OnNotifyNetworkMonitorAlternateStack(
    uint64_t aChannelID) {
  FETCH_LOG(("FetchInstance::OnNotifyNetworkMonitorAlternateStack [%p]", this));
  MOZ_ASSERT(mFetchDriver);
  MOZ_ASSERT(mPromises);

  if (mArgsType != FetchArgsType::WorkerFetch &&
      mArgsType != FetchArgsType::MainThreadFetch) {
    // Fetch type not supported
    return;
  }

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [actorID = GetActorID(), channelID = aChannelID]() {
        FETCH_LOG(
            ("FetchInstance::OnNotifyNetworkMonitorAlternateStack, Runnable"));
        RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
        if (actor) {
          actor->OnNotifyNetworkMonitorAlternateStack(channelID);
        }
      });

  MOZ_ALWAYS_SUCCEEDS(
      GetBackgroundEventTarget()->Dispatch(r, nsIThread::DISPATCH_NORMAL));
}

nsID FetchService::FetchInstance::GetActorID() {
  if (mArgsType == FetchArgsType::WorkerFetch) {
    return mArgs.as<WorkerFetchArgs>().mActorID;
  }

  if (mArgsType == FetchArgsType::MainThreadFetch) {
    return mArgs.as<MainThreadFetchArgs>().mActorID;
  }

  MOZ_ASSERT_UNREACHABLE("GetActorID called for unexpected mArgsType");

  return {};
}

nsCOMPtr<nsISerialEventTarget>
FetchService::FetchInstance::GetBackgroundEventTarget() {
  if (mArgsType == FetchArgsType::WorkerFetch) {
    return mArgs.as<WorkerFetchArgs>().mEventTarget;
  }

  if (mArgsType == FetchArgsType::MainThreadFetch) {
    return mArgs.as<MainThreadFetchArgs>().mEventTarget;
  }

  MOZ_ASSERT_UNREACHABLE(
      "GetBackgroundEventTarget called for unexpected mArgsType");

  return {};
}

// FetchService

NS_IMPL_ISUPPORTS(FetchService, nsIObserver)

StaticRefPtr<FetchService> gInstance;

/*static*/
already_AddRefed<FetchService> FetchService::GetInstance() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  if (!gInstance) {
    gInstance = MakeRefPtr<FetchService>();
    nsresult rv = gInstance->RegisterNetworkObserver();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      gInstance = nullptr;
      return nullptr;
    }
    ClearOnShutdown(&gInstance);
  }
  RefPtr<FetchService> service = gInstance;
  return service.forget();
}

/*static*/
RefPtr<FetchServicePromises> FetchService::NetworkErrorResponse(
    nsresult aRv, const FetchArgs& aArgs) {
  if (aArgs.is<WorkerFetchArgs>()) {
    const WorkerFetchArgs& args = aArgs.as<WorkerFetchArgs>();
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__, [aRv, actorID = args.mActorID]() mutable {
          FETCH_LOG(
              ("FetchService::PropagateErrorResponse runnable aError: 0x%X",
               (uint32_t)aRv));
          RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
          if (actor) {
            actor->OnResponseAvailableInternal(
                InternalResponse::NetworkError(aRv));
            actor->OnResponseEnd(
                ResponseEndArgs(FetchDriverObserver::eAborted));
          }
        });
    MOZ_ALWAYS_SUCCEEDS(
        args.mEventTarget->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  } else if (aArgs.is<MainThreadFetchArgs>()) {
    const MainThreadFetchArgs& args = aArgs.as<MainThreadFetchArgs>();
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__, [aRv, actorID = args.mActorID]() mutable {
          FETCH_LOG(
              ("FetchService::PropagateErrorResponse runnable aError: 0x%X",
               (uint32_t)aRv));
          RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
          if (actor) {
            actor->OnResponseAvailableInternal(
                InternalResponse::NetworkError(aRv));
            actor->OnResponseEnd(
                ResponseEndArgs(FetchDriverObserver::eAborted));
          }
        });
    MOZ_ALWAYS_SUCCEEDS(
        args.mEventTarget->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  }

  RefPtr<FetchServicePromises> promises = MakeRefPtr<FetchServicePromises>();
  promises->ResolveResponseAvailablePromise(InternalResponse::NetworkError(aRv),
                                            __func__);
  promises->ResolveResponseTimingPromise(ResponseTiming(), __func__);
  promises->ResolveResponseEndPromise(
      ResponseEndArgs(FetchDriverObserver::eAborted), __func__);
  return promises;
}

FetchService::FetchService() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
}

FetchService::~FetchService() {
  MOZ_ALWAYS_SUCCEEDS(UnregisterNetworkObserver());
}

nsresult FetchService::RegisterNetworkObserver() {
  AssertIsOnMainThread();
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (!observerService) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIIOService> ioService = components::IO::Service();
  if (!ioService) {
    return NS_ERROR_UNEXPECTED;
  }

  nsresult rv = observerService->AddObserver(
      this, NS_IOSERVICE_OFFLINE_STATUS_TOPIC, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = observerService->AddObserver(this, "xpcom-shutdown", false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ioService->GetOffline(&mOffline);
  NS_ENSURE_SUCCESS(rv, rv);
  mObservingNetwork = true;

  return NS_OK;
}

nsresult FetchService::UnregisterNetworkObserver() {
  AssertIsOnMainThread();
  nsresult rv;
  if (mObservingNetwork) {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      rv = observerService->RemoveObserver(this,
                                           NS_IOSERVICE_OFFLINE_STATUS_TOPIC);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = observerService->RemoveObserver(this, "xpcom-shutdown");
      NS_ENSURE_SUCCESS(rv, rv);
    }
    mObservingNetwork = false;
  }
  return NS_OK;
}

void FetchService::IncrementKeepAliveRequestCount(const nsACString& aOrigin) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  FETCH_LOG(("FetchService::IncrementKeepAliveRequestCount [origin=%s]\n",
             PromiseFlatCString(aOrigin).get()));
  ++mTotalKeepAliveRequests;
  uint32_t count = mPendingKeepAliveRequestsPerOrigin.Get(aOrigin) + 1;
  mPendingKeepAliveRequestsPerOrigin.InsertOrUpdate(aOrigin, count);
}

void FetchService::DecrementKeepAliveRequestCount(const nsACString& aOrigin) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  FETCH_LOG(("FetchService::DecrementKeepAliveRequestCount [origin=%s]\n",
             PromiseFlatCString(aOrigin).get()));
  MOZ_ASSERT(mTotalKeepAliveRequests > 0);
  if (mTotalKeepAliveRequests) {
    --mTotalKeepAliveRequests;
  }

  uint32_t count = mPendingKeepAliveRequestsPerOrigin.Get(aOrigin);
  MOZ_ASSERT(count > 0);
  if (count) {
    --count;
    if (count == 0) {
      mPendingKeepAliveRequestsPerOrigin.Remove(aOrigin);
    } else {
      mPendingKeepAliveRequestsPerOrigin.InsertOrUpdate(aOrigin, count);
    }
  }
}

bool FetchService::DoesExceedsKeepaliveResourceLimits(
    const nsACString& origin) {
  if (mTotalKeepAliveRequests >=
      StaticPrefs::dom_fetchKeepalive_total_request_limit()) {
    return true;
  }

  if (mPendingKeepAliveRequestsPerOrigin.Get(origin) >=
      StaticPrefs::dom_fetchKeepalive_request_limit_per_origin()) {
    return true;
  }

  return false;
}

NS_IMETHODIMP FetchService::Observe(nsISupports* aSubject, const char* aTopic,
                                    const char16_t* aData) {
  FETCH_LOG(("FetchService::Observe topic: %s", aTopic));
  AssertIsOnMainThread();
  MOZ_ASSERT(!strcmp(aTopic, NS_IOSERVICE_OFFLINE_STATUS_TOPIC) ||
             !strcmp(aTopic, "xpcom-shutdown"));

  if (!strcmp(aTopic, "xpcom-shutdown")) {
    // Going to shutdown, unregister the network status observer to avoid
    // receiving
    nsresult rv = UnregisterNetworkObserver();
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  if (nsDependentString(aData).EqualsLiteral(NS_IOSERVICE_ONLINE)) {
    mOffline = false;
  } else {
    mOffline = true;
    // Network is offline, cancel the running fetch that is not to local server.
    mFetchInstanceTable.RemoveIf([](auto& entry) {
      bool res = entry.Data()->IsLocalHostFetch();
      if (res) {
        return false;
      }
      entry.Data()->Cancel(true);
      return true;
    });
  }
  return NS_OK;
}

RefPtr<FetchServicePromises> FetchService::Fetch(FetchArgs&& aArgs) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  FETCH_LOG(("FetchService::Fetch (%s)", aArgs.is<NavigationPreloadArgs>()
                                             ? "NavigationPreload"
                                             : "WorkerFetch"));
  // Create FetchInstance
  RefPtr<FetchInstance> fetch = MakeRefPtr<FetchInstance>();

  // Call FetchInstance::Initialize() to get needed information for
  // FetchDriver
  nsresult rv = fetch->Initialize(std::move(aArgs));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NetworkErrorResponse(rv, fetch->Args());
  }

  // A controlled client's request has to be dispatched to the service worker
  // before the network is consulted, so it cannot be failed here. If no
  // service worker answers it, the channel fails on its own while offline.
  if (mOffline && !fetch->IsLocalHostFetch() &&
      !fetch->IsServiceWorkerEligible()) {
    FETCH_LOG(("FetchService::Fetch network offline"));
    return NetworkErrorResponse(NS_ERROR_OFFLINE, fetch->Args());
  }

  // Call FetchInstance::Fetch() to start an asynchronous fetching.
  RefPtr<FetchServicePromises> promises = fetch->Fetch();
  MOZ_ASSERT(promises);

  if (!promises->IsResponseAvailablePromiseResolved()) {
    // Insert the created FetchInstance into FetchInstanceTable.
    if (!mFetchInstanceTable.WithEntryHandle(promises, [&](auto&& entry) {
          if (entry.HasEntry()) {
            return false;
          }
          entry.Insert(fetch);
          return true;
        })) {
      FETCH_LOG(
          ("FetchService::Fetch entry[%p] already exists", promises.get()));
      return NetworkErrorResponse(NS_ERROR_UNEXPECTED, fetch->Args());
    }
    FETCH_LOG(("FetchService::Fetch entry[%p] of FetchInstance[%p] added",
               promises.get(), fetch.get()));
  }
  return promises;
}

void FetchService::CancelFetch(const RefPtr<FetchServicePromises>&& aPromises,
                               bool aForceAbort) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPromises);
  FETCH_LOG(("FetchService::CancelFetch aPromises[%p]", aPromises.get()));

  auto entry = mFetchInstanceTable.Lookup(aPromises);
  if (entry) {
    // Notice any modifications here before entry.Remove() probably should be
    // reflected to Observe() offline case.
    entry.Data()->Cancel(aForceAbort);
    entry.Remove();
    FETCH_LOG(
        ("FetchService::CancelFetch entry [%p] removed", aPromises.get()));
  }
}

MozPromiseRequestHolder<FetchServiceResponseEndPromise>&
FetchService::GetResponseEndPromiseHolder(
    const RefPtr<FetchServicePromises>& aPromises) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPromises);
  auto entry = mFetchInstanceTable.Lookup(aPromises);
  MOZ_ASSERT(entry);
  return entry.Data()->GetResponseEndPromiseHolder();
}

}  // namespace mozilla::dom
