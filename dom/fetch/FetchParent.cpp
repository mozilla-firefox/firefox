/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FetchParent.h"

#include "FetchLog.h"
#include "FetchService.h"
#include "InternalRequest.h"
#include "InternalResponse.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ClientValidation.h"
#include "mozilla/dom/FetchTypes.h"
#include "mozilla/dom/PerformanceTimingTypes.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsIContentPolicy.h"
#include "nsThreadUtils.h"

using namespace mozilla::ipc;

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(FetchParent::FetchParentCSPEventListener, nsICSPEventListener)

FetchParent::FetchParentCSPEventListener::FetchParentCSPEventListener(
    const nsID& aActorID, nsCOMPtr<nsISerialEventTarget> aEventTarget)
    : mActorID(aActorID), mEventTarget(aEventTarget) {
  MOZ_ASSERT(mEventTarget);
  FETCH_LOG(("FetchParentCSPEventListener [%p] actor ID: %s", this,
             mActorID.ToString().get()));
}

NS_IMETHODIMP FetchParent::FetchParentCSPEventListener::OnCSPViolationEvent(
    const nsAString& aJSON, const nsAString& aReportGroupName) {
  AssertIsOnMainThread();
  FETCH_LOG(("FetchParentCSPEventListener::OnCSPViolationEvent [%p]", this));

  nsAutoString json(aJSON);
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [actorID = mActorID, json = std::move(json),
                 reportGroup = nsString{aReportGroupName}]() mutable {
        FETCH_LOG(
            ("FetchParentCSPEventListener::OnCSPViolationEvent, Runnale"));
        RefPtr<FetchParent> actor = FetchParent::GetActorByID(actorID);
        if (actor) {
          actor->OnCSPViolationEvent(json, reportGroup);
        }
      });

  MOZ_ALWAYS_SUCCEEDS(mEventTarget->Dispatch(r, nsIThread::DISPATCH_NORMAL));
  return NS_OK;
}

constinit nsTHashMap<nsIDHashKey, RefPtr<FetchParent>> FetchParent::sActorTable;

/*static*/
RefPtr<FetchParent> FetchParent::GetActorByID(const nsID& aID) {
  AssertIsOnBackgroundThread();
  auto entry = sActorTable.Lookup(aID);
  if (entry) {
    return entry.Data();
  }
  return nullptr;
}

FetchParent::FetchParent() : mID(nsID::GenerateUUID()) {
  FETCH_LOG(("FetchParent::FetchParent [%p]", this));
  AssertIsOnBackgroundThread();
  mBackgroundEventTarget = GetCurrentSerialEventTarget();
  MOZ_ASSERT(mBackgroundEventTarget);
  if (!sActorTable.WithEntryHandle(mID, [&](auto&& entry) {
        if (entry.HasEntry()) {
          return false;
        }
        entry.Insert(this);
        return true;
      })) {
    FETCH_LOG(("FetchParent::FetchParent entry[%p] already exists", this));
  }
}

FetchParent::~FetchParent() {
  FETCH_LOG(("FetchParent::~FetchParent [%p]", this));
  // MOZ_ASSERT(!mBackgroundEventTarget);
  MOZ_ASSERT(mActorDestroyed && mIsDone);
  mResponsePromises = nullptr;
}

IPCResult FetchParent::RecvFetchOp(FetchOpArgs&& aArgs) {
  FETCH_LOG(("FetchParent::RecvFetchOp [%p]", this));
  AssertIsOnBackgroundThread();

  if (mReceivedFetchOp.exchange(true)) {
    return IPC_FAIL(this, "FetchOp received more than once on this actor");
  }
  MOZ_ASSERT(!mIsDone);
  if (mActorDestroyed) {
    return IPC_OK();
  }

  mPrincipal = aArgs.principal();
  if (RefPtr<ThreadsafeContentParentHandle> contentHandle =
          BackgroundParent::GetContentParentHandle(Manager())) {
    // The inference process uses ChromeWorkers which have a system principal,
    // so system principals must be allowed there.
    EnumSet<ValidatePrincipalOptions> options;
    if (contentHandle->GetRemoteType().IsInference()) {
      options += ValidatePrincipalOptions::AllowSystemIfLoaded;
    }
    if (!contentHandle->ValidatePrincipal(mPrincipal, options)) {
      return IPC_FAIL(this,
                      "RecvFetchOp principal not allowed for remote type");
    }
    if (!ClientIsValidPrincipalInfo(aArgs.clientInfo().principalInfo(),
                                    contentHandle->LoadedOrigins())) {
      return IPC_FAIL(
          this, "RecvFetchOp clientInfo principal not allowed for remote type");
    }

    if (aArgs.request().contentPolicyType() ==
            nsIContentPolicy::TYPE_INTERNAL_FORCE_ALLOWED_DTD &&
        !StaticPrefs::dom_fetch_allow_force_allowed_dtd()) {
      return IPC_FAIL(this,
                      "RecvFetchOp FORCE_ALLOWED_DTD not allowed from content");
    }

    if (InternalRequest::IsNavigationContentPolicy(
            aArgs.request().contentPolicyType())) {
      return IPC_FAIL(this,
                      "RecvFetchOp navigation content policy type not allowed "
                      "from content");
    }
  }

  mRequest = MakeSafeRefPtr<InternalRequest>(std::move(aArgs.request()));
  mIsWorkerFetch = aArgs.isWorkerRequest();
  mWorkerScript = aArgs.workerScript();
  mClientInfo = Some(ClientInfo(aArgs.clientInfo()));
  if (aArgs.controller().isSome()) {
    mController = Some(ServiceWorkerDescriptor(aArgs.controller().ref()));
  }
  mCookieJarSettings = aArgs.cookieJarSettings();
  mNeedOnDataAvailable = aArgs.needOnDataAvailable();
  mHasCSPEventListener = aArgs.hasCSPEventListener();
  mIsThirdPartyContext = aArgs.isThirdPartyContext();
  mIsOn3PCBExceptionList = aArgs.isOn3PCBExceptionList();

  if (mHasCSPEventListener) {
    mCSPEventListener =
        MakeRefPtr<FetchParentCSPEventListener>(mID, mBackgroundEventTarget);
  }
  mAssociatedBrowsingContextID = aArgs.associatedBrowsingContextID();

  MOZ_ASSERT(!mPromise);
  mPromise = new GenericPromise::Private(__func__);

  RefPtr<FetchParent> self = this;
  mPromise->Then(
      mBackgroundEventTarget, __func__,
      [self](const bool&& result) mutable {
        FETCH_LOG(
            ("FetchParent::RecvFetchOp [%p] Success Callback", self.get()));
        AssertIsOnBackgroundThread();
        self->mPromise = nullptr;
        if (self->mIsDone) {
          FETCH_LOG(("FetchParent::RecvFetchOp [%p] Fetch has already aborted",
                     self.get()));
          if (!self->mActorDestroyed) {
            (void)NS_WARN_IF(
                !self->Send__delete__(self, NS_ERROR_DOM_ABORT_ERR));
          }
          return;
        }
        self->mIsDone = true;
        if (!self->mActorDestroyed && !self->mExtendForCSPEventListener) {
          FETCH_LOG(("FetchParent::RecvFetchOp [%p] Send__delete__(NS_OK)",
                     self.get()));
          (void)NS_WARN_IF(!self->Send__delete__(self, NS_OK));
        }
      },
      [self](const nsresult&& aErr) mutable {
        FETCH_LOG(
            ("FetchParent::RecvFetchOp [%p] Failure Callback", self.get()));
        AssertIsOnBackgroundThread();
        self->mIsDone = true;
        self->mPromise = nullptr;
        if (!self->mActorDestroyed) {
          FETCH_LOG(("FetchParent::RecvFetchOp [%p] Send__delete__(aErr)",
                     self.get()));
          (void)NS_WARN_IF(!self->Send__delete__(self, aErr));
        }
      });

  RefPtr<nsIRunnable> r = NS_NewRunnableFunction(__func__, [self]() mutable {
    FETCH_LOG(
        ("FetchParent::RecvFetchOp [%p], Main Thread Runnable", self.get()));
    AssertIsOnMainThread();
    if (self->mIsDone) {
      MOZ_ASSERT(!self->mResponsePromises);
      MOZ_ASSERT(self->mPromise);
      FETCH_LOG(
          ("FetchParent::RecvFetchOp [%p], Main Thread Runnable, "
           "already aborted",
           self.get()));
      self->mPromise->Reject(NS_ERROR_DOM_ABORT_ERR, __func__);
      return;
    }
    RefPtr<FetchService> fetchService = FetchService::GetInstance();
    MOZ_ASSERT(fetchService);
    MOZ_ASSERT(self->mRequest);
    MOZ_ASSERT(!self->mResponsePromises);
    if (self->mIsWorkerFetch) {
      self->mResponsePromises =
          fetchService->Fetch(AsVariant(FetchService::WorkerFetchArgs(
              {self->mRequest.clonePtr(), self->mPrincipal, self->mWorkerScript,
               *self->mClientInfo, self->mController, self->mCookieJarSettings,
               self->mNeedOnDataAvailable, self->mCSPEventListener,
               self->mAssociatedBrowsingContextID, self->mBackgroundEventTarget,
               self->mID, self->mIsThirdPartyContext,
               MozPromiseRequestHolder<FetchServiceResponseEndPromise>(),
               self->mPromise, self->mIsOn3PCBExceptionList})));
    } else {
      MOZ_ASSERT(self->mRequest->GetKeepalive());
      self->mResponsePromises =
          fetchService->Fetch(AsVariant(FetchService::MainThreadFetchArgs({
              self->mRequest.clonePtr(),
              self->mPrincipal,
              *self->mClientInfo,
              self->mCookieJarSettings,
              self->mNeedOnDataAvailable,
              self->mCSPEventListener,
              self->mAssociatedBrowsingContextID,
              self->mBackgroundEventTarget,
              self->mID,
              self->mIsThirdPartyContext,
          })));
    }

    bool isResolved = self->mResponsePromises->IsResponseEndPromiseResolved();
    if (!isResolved && self->mIsWorkerFetch) {
      // track only unresolved promises for worker fetch requests
      // this is needed for clean-up of keepalive requests
      self->mResponsePromises->GetResponseEndPromise()
          ->Then(
              GetMainThreadSerialEventTarget(), __func__,
              [self](ResponseEndArgs&& aArgs) mutable {
                AssertIsOnMainThread();
                MOZ_ASSERT(self->mPromise);
                self->mPromise->Resolve(true, __func__);
                self->mResponsePromises = nullptr;
              },
              [self](CopyableErrorResult&& aErr) mutable {
                AssertIsOnMainThread();
                MOZ_ASSERT(self->mPromise);
                self->mPromise->Reject(aErr.StealNSResult(), __func__);
                self->mResponsePromises = nullptr;
              })
          ->Track(fetchService->GetResponseEndPromiseHolder(
              self->mResponsePromises));
    } else {
      self->mResponsePromises->GetResponseEndPromise()->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self](ResponseEndArgs&& aArgs) mutable {
            AssertIsOnMainThread();
            MOZ_ASSERT(self->mPromise);
            self->mPromise->Resolve(true, __func__);
            self->mResponsePromises = nullptr;
          },
          [self](CopyableErrorResult&& aErr) mutable {
            AssertIsOnMainThread();
            MOZ_ASSERT(self->mPromise);
            self->mPromise->Reject(aErr.StealNSResult(), __func__);
            self->mResponsePromises = nullptr;
          });
    }
  });

  MOZ_ALWAYS_SUCCEEDS(
      NS_DispatchToMainThread(r.forget(), nsIThread::DISPATCH_NORMAL));

  return IPC_OK();
}

IPCResult FetchParent::RecvAbortFetchOp(bool aForceAbort) {
  FETCH_LOG(("FetchParent::RecvAbortFetchOp [%p]", this));
  AssertIsOnBackgroundThread();

  if (mIsDone) {
    FETCH_LOG(("FetchParent::RecvAbortFetchOp [%p], Already aborted", this));
    return IPC_OK();
  }

  if (!aForceAbort && mRequest && mRequest->GetKeepalive()) {
    // Keeping FetchParent/FetchChild alive for the main-thread keepalive fetch
    // here is a temporary solution. The cancel logic should always be handled
    // in FetchInstance::Cancel() once all main-thread fetch routing through
    // PFetch.
    if (!mIsWorkerFetch) {
      FETCH_LOG(("Skip aborting fetch as the request is marked keepalive"));
      return IPC_OK();
    }
  } else {
    mIsDone = true;
  }
  RefPtr<FetchParent> self = this;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [self, forceAbort = aForceAbort]() mutable {
        FETCH_LOG(("FetchParent::RecvAbortFetchOp Runnable"));
        AssertIsOnMainThread();
        if (self->mResponsePromises) {
          RefPtr<FetchService> fetchService = FetchService::GetInstance();
          MOZ_ASSERT(fetchService);
          fetchService->CancelFetch(std::move(self->mResponsePromises),
                                    forceAbort);
        }
      });

  MOZ_ALWAYS_SUCCEEDS(
      NS_DispatchToMainThread(r.forget(), nsIThread::DISPATCH_NORMAL));

  return IPC_OK();
}

void FetchParent::OnResponseAvailableInternal(
    SafeRefPtr<InternalResponse>&& aResponse) {
  FETCH_LOG(("FetchParent::OnResponseAvailableInternal [%p]", this));
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aResponse);
  MOZ_ASSERT(!mActorDestroyed);

  if (mIsDone && aResponse->Type() != ResponseType::Error) {
    FETCH_LOG(
        ("FetchParent::OnResponseAvailableInternal [%p] "
         "Fetch has already aborted",
         this));
    return;
  }

  // To monitor the stream status between processes, response's body can not
  // be serialized as RemoteLazyInputStream. Such that stream close can be
  // propagated to FetchDriver in the parent process.
  aResponse->SetSerializeAsLazy(false);

  // CSP violation notification is asynchronous. Extending the FetchParent's
  // life cycle for the notificaiton.
  if (aResponse->Type() == ResponseType::Error &&
      aResponse->GetErrorCode() == NS_ERROR_CONTENT_BLOCKED &&
      mCSPEventListener) {
    FETCH_LOG(
        ("FetchParent::OnResponseAvailableInternal [%p] "
         "NS_ERROR_CONTENT_BLOCKED",
         this));
    mExtendForCSPEventListener = true;
  }

  (void)SendOnResponseAvailableInternal(
      aResponse->ToParentToChildInternalResponse());
}

void FetchParent::OnResponseEnd(const ResponseEndArgs& aArgs) {
  FETCH_LOG(("FetchParent::OnResponseEnd [%p]", this));
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  if (mIsDone && aArgs.endReason() != FetchDriverObserver::eAborted) {
    FETCH_LOG(
        ("FetchParent::OnResponseEnd [%p] "
         "Fetch has already aborted",
         this));
    return;
  }

  (void)SendOnResponseEnd(aArgs);
}

void FetchParent::OnDataAvailable() {
  FETCH_LOG(("FetchParent::OnDataAvailable [%p]", this));
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  (void)SendOnDataAvailable();
}

void FetchParent::OnFlushConsoleReport(
    const nsTArray<net::ConsoleReportCollected>& aReports) {
  FETCH_LOG(("FetchParent::OnFlushConsoleReport [%p]", this));
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  (void)SendOnFlushConsoleReport(aReports);
}

void FetchParent::OnReportPerformanceTiming(const ResponseTiming&& aTiming) {
  FETCH_LOG(("FetchParent::OnReportPerformanceTiming [%p]", this));
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  (void)SendOnReportPerformanceTiming(aTiming);
}

void FetchParent::OnNotifyNetworkMonitorAlternateStack(uint64_t aChannelID) {
  FETCH_LOG(("FetchParent::OnNotifyNetworkMonitorAlternateStack [%p]", this));
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  (void)SendOnNotifyNetworkMonitorAlternateStack(aChannelID);
}

void FetchParent::ActorDestroy(ActorDestroyReason aReason) {
  FETCH_LOG(("FetchParent::ActorDestroy [%p]", this));
  AssertIsOnBackgroundThread();
  mActorDestroyed = true;
  auto entry = sActorTable.Lookup(mID);
  if (entry) {
    entry.Remove();
    FETCH_LOG(("FetchParent::ActorDestroy entry [%p] removed", this));
  }
  // mRequest can be null when FetchParent has not yet received RecvFetchOp()
  if (!mRequest) {
    return;
  }

  // Abort the existing fetch.
  // Actor can be destoried by shutdown when still fetching.
  RecvAbortFetchOp(false);

  // mBackgroundEventTarget = nullptr;
}

nsICSPEventListener* FetchParent::GetCSPEventListener() {
  return mCSPEventListener;
}

void FetchParent::OnCSPViolationEvent(const nsAString& aJSON,
                                      const nsAString& aReportGroupName) {
  FETCH_LOG(("FetchParent::OnCSPViolationEvent [%p]", this));
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mHasCSPEventListener);
  MOZ_ASSERT(!mActorDestroyed);

  (void)SendOnCSPViolationEvent(aJSON, aReportGroupName);
}

}  // namespace mozilla::dom
