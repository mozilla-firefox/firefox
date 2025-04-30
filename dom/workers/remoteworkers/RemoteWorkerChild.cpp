/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerChild.h"

#include <utility>

#include "MainThreadUtils.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIConsoleReportCollector.h"
#include "nsIInterfaceRequestor.h"
#include "nsIPrincipal.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

#include "RemoteWorkerService.h"
#include "mozilla/ArrayAlgorithm.h"
#include "mozilla/Assertions.h"
#include "mozilla/Attributes.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/FetchEventOpProxyChild.h"
#include "mozilla/dom/IndexedDatabaseManager.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/RemoteWorkerTypes.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/dom/ServiceWorkerInterceptController.h"
#include "mozilla/dom/ServiceWorkerOp.h"
#include "mozilla/dom/ServiceWorkerRegistrationDescriptor.h"
#include "mozilla/dom/ServiceWorkerShutdownState.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/SharedWorkerOp.h"
#include "mozilla/dom/workerinternals/ScriptLoader.h"
#include "mozilla/dom/WorkerCSPContext.h"
#include "mozilla/dom/WorkerError.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/PermissionManager.h"

mozilla::LazyLogModule gRemoteWorkerChildLog("RemoteWorkerChild");

#ifdef LOG
#  undef LOG
#endif
#define LOG(fmt) MOZ_LOG(gRemoteWorkerChildLog, mozilla::LogLevel::Verbose, fmt)

namespace mozilla {

using namespace ipc;

namespace dom {

using workerinternals::ChannelFromScriptURLMainThread;

using remoteworker::Canceled;
using remoteworker::Killed;
using remoteworker::Pending;
using remoteworker::Running;

namespace {

class SharedWorkerInterfaceRequestor final : public nsIInterfaceRequestor {
 public:
  NS_DECL_ISUPPORTS

  SharedWorkerInterfaceRequestor() {
    // This check must match the code nsDocShell::Create.
    if (XRE_IsParentProcess()) {
      mSWController = new ServiceWorkerInterceptController();
    }
  }

  NS_IMETHOD
  GetInterface(const nsIID& aIID, void** aSink) override {
    MOZ_ASSERT(NS_IsMainThread());

    if (mSWController &&
        aIID.Equals(NS_GET_IID(nsINetworkInterceptController))) {
      // If asked for the network intercept controller, ask the outer requestor,
      // which could be the docshell.
      RefPtr<ServiceWorkerInterceptController> swController = mSWController;
      swController.forget(aSink);
      return NS_OK;
    }

    return NS_NOINTERFACE;
  }

 private:
  ~SharedWorkerInterfaceRequestor() = default;

  RefPtr<ServiceWorkerInterceptController> mSWController;
};

NS_IMPL_ADDREF(SharedWorkerInterfaceRequestor)
NS_IMPL_RELEASE(SharedWorkerInterfaceRequestor)
NS_IMPL_QUERY_INTERFACE(SharedWorkerInterfaceRequestor, nsIInterfaceRequestor)

// This is used to propagate the CSP violation when loading the SharedWorker
// main-script and nothing else.
class RemoteWorkerCSPEventListener final : public nsICSPEventListener {
 public:
  NS_DECL_ISUPPORTS

  explicit RemoteWorkerCSPEventListener(RemoteWorkerChild* aActor)
      : mActor(aActor) {};

  NS_IMETHOD OnCSPViolationEvent(const nsAString& aJSON) override {
    mActor->CSPViolationPropagationOnMainThread(aJSON);
    return NS_OK;
  }

 private:
  ~RemoteWorkerCSPEventListener() = default;

  RefPtr<RemoteWorkerChild> mActor;
};

NS_IMPL_ISUPPORTS(RemoteWorkerCSPEventListener, nsICSPEventListener)

}  // anonymous namespace

RemoteWorkerChild::RemoteWorkerChild(const RemoteWorkerData& aData)
    : mState(VariantType<remoteworker::Pending>(), "RemoteWorkerState"),
      mServiceKeepAlive(RemoteWorkerService::MaybeGetKeepAlive()),
      mIsServiceWorker(aData.serviceWorkerData().type() ==
                       OptionalServiceWorkerData::TServiceWorkerData),
      mPendingOps("PendingRemoteWorkerOps") {
  MOZ_ASSERT(RemoteWorkerService::Thread()->IsOnCurrentThread());
}

RemoteWorkerChild::~RemoteWorkerChild() {
#ifdef DEBUG
  auto lock = mState.Lock();
  MOZ_ASSERT(lock->is<Killed>());
#endif
}

void RemoteWorkerChild::ActorDestroy(ActorDestroyReason) {
  auto launcherData = mLauncherData.Access();

  Unused << NS_WARN_IF(!launcherData->mTerminationPromise.IsEmpty());
  launcherData->mTerminationPromise.RejectIfExists(NS_ERROR_DOM_ABORT_ERR,
                                                   __func__);

  auto lock = mState.Lock();

  // If the worker hasn't shutdown or begun shutdown, we need to ensure it gets
  // canceled.
  if (NS_WARN_IF(!lock->is<Killed>() && !lock->is<Canceled>())) {
    // In terms of strong references to this RemoteWorkerChild, at this moment:
    // - IPC is holding a strong reference that will be dropped in the near
    //   future after this method returns.
    // - If the worker has been started by ExecWorkerOnMainThread, then
    //   WorkerPrivate::mRemoteWorkerController is a strong reference to us.
    //   If the worker has not been started, ExecWorker's runnable lambda will
    //   have a strong reference that will cover the call to
    //   ExecWorkerOnMainThread.
    //   - The WorkerPrivate cancellation and termination callbacks will also
    //     hold strong references, but those callbacks will not outlive the
    //     WorkerPrivate and are not exposed to callers like
    //     mRemoteWorkerController is.
    //
    // Note that this call to RequestWorkerCancellation can still race worker
    // cancellation, in which case the strong reference obtained by
    // NewRunnableMethod can end up being the last strong reference.
    // (RequestWorkerCancellation handles the case that the Worker is already
    // canceled if this happens.)
    RefPtr<nsIRunnable> runnable =
        NewRunnableMethod("RequestWorkerCancellation", this,
                          &RemoteWorkerChild::RequestWorkerCancellation);
    MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(runnable.forget()));
  }
}

void RemoteWorkerChild::ExecWorker(
    const RemoteWorkerData& aData,
    mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
        aChildEp) {
#ifdef DEBUG
  MOZ_ASSERT(GetActorEventTarget()->IsOnCurrentThread());
  auto launcherData = mLauncherData.Access();
  MOZ_ASSERT(CanSend());
#endif

  RefPtr<RemoteWorkerChild> self = this;

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [self = std::move(self), data = aData,
                 childEp = std::move(aChildEp)]() mutable {
        nsresult rv =
            self->ExecWorkerOnMainThread(std::move(data), std::move(childEp));

        // Creation failure will already have been reported via the method
        // above internally using ScopeExit.
        Unused << NS_WARN_IF(NS_FAILED(rv));
      });

  MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));
}

nsresult RemoteWorkerChild::ExecWorkerOnMainThread(
    RemoteWorkerData&& aData,
    mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
        aChildEp) {
  MOZ_ASSERT(NS_IsMainThread());

  // Ensure that the IndexedDatabaseManager is initialized so that if any
  // workers do any IndexedDB calls that all of IDB's prefs/etc. are
  // initialized.
  IndexedDatabaseManager* idm = IndexedDatabaseManager::GetOrCreate();
  if (idm) {
    Unused << NS_WARN_IF(NS_FAILED(idm->EnsureLocale()));
  } else {
    NS_WARNING("Failed to get IndexedDatabaseManager!");
  }

  auto scopeExit =
      MakeScopeExit([&] { ExceptionalErrorTransitionDuringExecWorker(); });

  auto principalOrErr = PrincipalInfoToPrincipal(aData.principalInfo());
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return principalOrErr.unwrapErr();
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  auto loadingPrincipalOrErr =
      PrincipalInfoToPrincipal(aData.loadingPrincipalInfo());
  if (NS_WARN_IF(loadingPrincipalOrErr.isErr())) {
    return loadingPrincipalOrErr.unwrapErr();
  }

  auto partitionedPrincipalOrErr =
      PrincipalInfoToPrincipal(aData.partitionedPrincipalInfo());
  if (NS_WARN_IF(partitionedPrincipalOrErr.isErr())) {
    return partitionedPrincipalOrErr.unwrapErr();
  }

  WorkerLoadInfo info;
  info.mBaseURI = DeserializeURI(aData.baseScriptURL());
  info.mResolvedScriptURI = DeserializeURI(aData.resolvedScriptURL());

  info.mPrincipalInfo = MakeUnique<PrincipalInfo>(aData.principalInfo());
  info.mPartitionedPrincipalInfo =
      MakeUnique<PrincipalInfo>(aData.partitionedPrincipalInfo());

  info.mReferrerInfo = aData.referrerInfo();
  info.mDomain = aData.domain();
  info.mTrials = aData.originTrials();
  info.mPrincipal = principal;
  info.mPartitionedPrincipal = partitionedPrincipalOrErr.unwrap();
  info.mLoadingPrincipal = loadingPrincipalOrErr.unwrap();
  info.mStorageAccess = aData.storageAccess();
  info.mUseRegularPrincipal = aData.useRegularPrincipal();
  info.mUsingStorageAccess = aData.usingStorageAccess();
  info.mIsThirdPartyContext = aData.isThirdPartyContext();
  info.mOriginAttributes =
      BasePrincipal::Cast(principal)->OriginAttributesRef();
  info.mShouldResistFingerprinting = aData.shouldResistFingerprinting();
  Maybe<RFPTargetSet> overriddenFingerprintingSettings;
  if (aData.overriddenFingerprintingSettings().isSome()) {
    overriddenFingerprintingSettings.emplace(
        aData.overriddenFingerprintingSettings().ref());
  }
  info.mOverriddenFingerprintingSettings = overriddenFingerprintingSettings;
  net::CookieJarSettings::Deserialize(aData.cookieJarSettings(),
                                      getter_AddRefs(info.mCookieJarSettings));
  info.mCookieJarSettingsArgs = aData.cookieJarSettings();
  info.mIsOn3PCBExceptionList = aData.isOn3PCBExceptionList();
  info.mSecureContext = aData.isSecureContext()
                            ? WorkerLoadInfo::eSecureContext
                            : WorkerLoadInfo::eInsecureContext;

  WorkerPrivate::OverrideLoadInfoLoadGroup(info, info.mLoadingPrincipal);

  RefPtr<SharedWorkerInterfaceRequestor> requestor =
      new SharedWorkerInterfaceRequestor();
  info.mInterfaceRequestor->SetOuterRequestor(requestor);

  Maybe<ClientInfo> clientInfo;
  if (aData.clientInfo().isSome()) {
    clientInfo.emplace(ClientInfo(aData.clientInfo().ref()));
  }

  if (mIsServiceWorker) {
    info.mSourceInfo = clientInfo;
  } else {
    if (clientInfo.isSome()) {
      Maybe<mozilla::ipc::CSPInfo> cspInfo = clientInfo.ref().GetCspInfo();
      if (cspInfo.isSome()) {
        info.mCSP = CSPInfoToCSP(cspInfo.ref(), nullptr);
        mozilla::Result<UniquePtr<WorkerCSPContext>, nsresult> ctx =
            WorkerCSPContext::CreateFromCSP(info.mCSP);
        if (ctx.isErr()) {
          return ctx.unwrapErr();
        }
        info.mCSPContext = ctx.unwrap();
      }
    }
  }

  nsresult rv = info.SetPrincipalsAndCSPOnMainThread(
      info.mPrincipal, info.mPartitionedPrincipal, info.mLoadGroup, info.mCSP);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsString workerPrivateId;

  if (mIsServiceWorker) {
    ServiceWorkerData& data = aData.serviceWorkerData().get_ServiceWorkerData();

    MOZ_ASSERT(!data.id().IsEmpty());
    workerPrivateId = std::move(data.id());

    info.mServiceWorkerCacheName = data.cacheName();
    info.mServiceWorkerDescriptor.emplace(data.descriptor());
    info.mServiceWorkerRegistrationDescriptor.emplace(
        data.registrationDescriptor());
    info.mLoadFlags = static_cast<nsLoadFlags>(data.loadFlags());
  } else {
    // Top level workers' main script use the document charset for the script
    // uri encoding.
    rv = ChannelFromScriptURLMainThread(
        info.mLoadingPrincipal, nullptr /* parent document */, info.mLoadGroup,
        info.mResolvedScriptURI, aData.workerOptions().mType,
        aData.workerOptions().mCredentials, clientInfo,
        nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER, info.mCookieJarSettings,
        info.mReferrerInfo, getter_AddRefs(info.mChannel));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    nsCOMPtr<nsILoadInfo> loadInfo = info.mChannel->LoadInfo();

    auto* cspEventListener = new RemoteWorkerCSPEventListener(this);
    rv = loadInfo->SetCspEventListener(cspEventListener);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  info.mAgentClusterId = aData.agentClusterId();

  AutoJSAPI jsapi;
  jsapi.Init();

  ErrorResult error;
  RefPtr<RemoteWorkerChild> self = this;
  RefPtr<WorkerPrivate> workerPrivate = WorkerPrivate::Constructor(
      jsapi.cx(), aData.originalScriptURL(), false,
      mIsServiceWorker ? WorkerKindService : WorkerKindShared,
      aData.workerOptions().mCredentials, aData.workerOptions().mType,
      aData.workerOptions().mName, VoidCString(), &info, error,
      std::move(workerPrivateId),
      [self](bool aEverRan) {
        self->OnWorkerCancellationTransitionStateFromPendingOrRunningToCanceled();
      },
      // This will be invoked here on the main thread when the worker is already
      // fully shutdown.  This replaces a prior approach where we would
      // begin to transition when the worker thread would reach the Canceling
      // state.  This lambda ensures that we not only wait for the Killing state
      // to be reached but that the global shutdown has already occurred.
      [self]() { self->TransitionStateFromCanceledToKilled(); },
      std::move(aChildEp));

  if (NS_WARN_IF(error.Failed())) {
    MOZ_ASSERT(!workerPrivate);

    rv = error.StealNSResult();
    return rv;
  }

  workerPrivate->SetRemoteWorkerController(this);

  // This wants to run as a normal task sequentially after the top level script
  // evaluation, so the hybrid target is the correct choice between hybrid and
  // `ControlEventTarget`.
  nsCOMPtr<nsISerialEventTarget> workerTarget =
      workerPrivate->HybridEventTarget();

  nsCOMPtr<nsIRunnable> runnable = NewCancelableRunnableMethod(
      "InitialzeOnWorker", this, &RemoteWorkerChild::InitializeOnWorker);

  {
    MOZ_ASSERT(workerPrivate);
    auto lock = mState.Lock();
    // We MUST be pending here, so direct access is ok.
    lock->as<Pending>().mWorkerPrivate = std::move(workerPrivate);
  }

  if (mIsServiceWorker) {
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__, [workerTarget,
                   initializeWorkerRunnable = std::move(runnable)]() mutable {
          Unused << NS_WARN_IF(NS_FAILED(
              workerTarget->Dispatch(initializeWorkerRunnable.forget())));
        });

    RefPtr<PermissionManager> permissionManager =
        PermissionManager::GetInstance();
    if (!permissionManager) {
      return NS_ERROR_FAILURE;
    }
    permissionManager->WhenPermissionsAvailable(principal, r);
  } else {
    if (NS_WARN_IF(NS_FAILED(workerTarget->Dispatch(runnable.forget())))) {
      rv = NS_ERROR_FAILURE;
      return rv;
    }
  }

  scopeExit.release();

  return NS_OK;
}

void RemoteWorkerChild::RequestWorkerCancellation() {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(("RequestWorkerCancellation[this=%p]", this));

  // We want to ensure that we've requested the worker be canceled.  So if the
  // worker is running, cancel it.  We can't do this with the lock held,
  // however, because our lambdas will want to manipulate the state.
  RefPtr<WorkerPrivate> cancelWith;
  {
    auto lock = mState.Lock();
    if (lock->is<Pending>()) {
      cancelWith = lock->as<Pending>().mWorkerPrivate;
    } else if (lock->is<Running>()) {
      cancelWith = lock->as<Running>().mWorkerPrivate;
    }
  }

  if (cancelWith) {
    cancelWith->Cancel();
  }
}

// This method will be invoked on the worker after the top-level
// CompileScriptRunnable task has succeeded and as long as the worker has not
// been closed/canceled.  There are edge-cases related to cancellation, but we
// have our caller ensure that we are only called as long as the worker's state
// is Running.
//
// (https://bugzilla.mozilla.org/show_bug.cgi?id=1800659 will eliminate
// cancellation, and the documentation around that bug / in design documents
// helps provide more context about this.)
void RemoteWorkerChild::InitializeOnWorker() {
  nsCOMPtr<nsIRunnable> r =
      NewRunnableMethod("TransitionStateToRunning", this,
                        &RemoteWorkerChild::TransitionStateToRunning);
  MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));
}

RefPtr<GenericNonExclusivePromise> RemoteWorkerChild::GetTerminationPromise() {
  auto launcherData = mLauncherData.Access();
  return launcherData->mTerminationPromise.Ensure(__func__);
}

void RemoteWorkerChild::CreationSucceededOnAnyThread() {
  CreationSucceededOrFailedOnAnyThread(true);
}

void RemoteWorkerChild::CreationFailedOnAnyThread() {
  CreationSucceededOrFailedOnAnyThread(false);
}

void RemoteWorkerChild::CreationSucceededOrFailedOnAnyThread(
    bool aDidCreationSucceed) {
#ifdef DEBUG
  {
    auto lock = mState.Lock();
    MOZ_ASSERT_IF(aDidCreationSucceed, lock->is<Running>());
    MOZ_ASSERT_IF(!aDidCreationSucceed, lock->is<Killed>());
  }
#endif

  RefPtr<RemoteWorkerChild> self = this;

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__,
      [self = std::move(self), didCreationSucceed = aDidCreationSucceed] {
        auto launcherData = self->mLauncherData.Access();

        if (!self->CanSend() || launcherData->mDidSendCreated) {
          return;
        }

        Unused << self->SendCreated(didCreationSucceed);
        launcherData->mDidSendCreated = true;
      });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void RemoteWorkerChild::CloseWorkerOnMainThread() {
  AssertIsOnMainThread();

  LOG(("CloseWorkerOnMainThread[this=%p]", this));

  // We can't hold the state lock while calling WorkerPrivate::Cancel because
  // the lambda callback will want to touch the state, so save off the
  // WorkerPrivate so we can cancel it (if we need to cancel it).
  RefPtr<WorkerPrivate> cancelWith;
  {
    auto lock = mState.Lock();

    if (lock->is<Pending>()) {
      cancelWith = lock->as<Pending>().mWorkerPrivate;
      // There should be no way for this code to run before we
      // ExecWorkerOnMainThread runs, which means that either it should have
      // set a WorkerPrivate on Pending, or its error handling should already
      // have transitioned us to Canceled and Killing in that order.  (It's
      // also possible that it assigned a WorkerPrivate and subsequently we
      // transitioned to Running, which would put us in the next branch.)
      MOZ_DIAGNOSTIC_ASSERT(cancelWith);
    } else if (lock->is<Running>()) {
      cancelWith = lock->as<Running>().mWorkerPrivate;
    }
  }

  // It's very okay for us to not have a WorkerPrivate here if we've already
  // canceled the worker or if errors happened.
  if (cancelWith) {
    cancelWith->Cancel();
  }
}

/**
 * Error reporting method
 */
void RemoteWorkerChild::ErrorPropagation(const ErrorValue& aValue) {
  MOZ_ASSERT(GetActorEventTarget()->IsOnCurrentThread());

  if (!CanSend()) {
    return;
  }

  Unused << SendError(aValue);
}

void RemoteWorkerChild::ErrorPropagationDispatch(nsresult aError) {
  MOZ_ASSERT(NS_FAILED(aError));

  RefPtr<RemoteWorkerChild> self = this;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "RemoteWorkerChild::ErrorPropagationDispatch",
      [self = std::move(self), aError]() { self->ErrorPropagation(aError); });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void RemoteWorkerChild::ErrorPropagationOnMainThread(
    const WorkerErrorReport* aReport, bool aIsErrorEvent) {
  AssertIsOnMainThread();

  ErrorValue value;
  if (aIsErrorEvent) {
    ErrorData data(
        aReport->mIsWarning, aReport->mLineNumber, aReport->mColumnNumber,
        aReport->mMessage, aReport->mFilename,
        TransformIntoNewArray(aReport->mNotes, [](const WorkerErrorNote& note) {
          return ErrorDataNote(note.mLineNumber, note.mColumnNumber,
                               note.mMessage, note.mFilename);
        }));
    value = data;
  } else {
    value = void_t();
  }

  RefPtr<RemoteWorkerChild> self = this;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "RemoteWorkerChild::ErrorPropagationOnMainThread",
      [self = std::move(self), value]() { self->ErrorPropagation(value); });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void RemoteWorkerChild::CSPViolationPropagationOnMainThread(
    const nsAString& aJSON) {
  AssertIsOnMainThread();

  RefPtr<RemoteWorkerChild> self = this;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "RemoteWorkerChild::ErrorPropagationDispatch",
      [self = std::move(self), json = nsString(aJSON)]() {
        CSPViolation violation(json);
        self->ErrorPropagation(violation);
      });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void RemoteWorkerChild::NotifyLock(bool aCreated) {
  nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableFunction(__func__, [self = RefPtr(this), aCreated] {
        if (!self->CanSend()) {
          return;
        }

        Unused << self->SendNotifyLock(aCreated);
      });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void RemoteWorkerChild::NotifyWebTransport(bool aCreated) {
  nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableFunction(__func__, [self = RefPtr(this), aCreated] {
        if (!self->CanSend()) {
          return;
        }

        Unused << self->SendNotifyWebTransport(aCreated);
      });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void RemoteWorkerChild::FlushReportsOnMainThread(
    nsIConsoleReportCollector* aReporter) {
  AssertIsOnMainThread();

  bool reportErrorToBrowserConsole = true;

  // Flush the reports.
  for (uint32_t i = 0, len = mWindowIDs.Length(); i < len; ++i) {
    aReporter->FlushReportsToConsole(
        mWindowIDs[i], nsIConsoleReportCollector::ReportAction::Save);
    reportErrorToBrowserConsole = false;
  }

  // Finally report to browser console if there is no any window.
  if (reportErrorToBrowserConsole) {
    aReporter->FlushReportsToConsole(0);
    return;
  }

  aReporter->ClearConsoleReports();
}

/**
 * Worker state transition methods
 */
void RemoteWorkerChild::
    OnWorkerCancellationTransitionStateFromPendingOrRunningToCanceled() {
  auto lock = mState.Lock();

  LOG(("TransitionStateFromPendingOrRunningToCanceled[this=%p]", this));

  if (lock->is<Pending>()) {
    TransitionStateFromPendingToCanceled(lock.ref());
  } else if (lock->is<Running>()) {
    *lock = VariantType<remoteworker::Canceled>();
  } else {
    MOZ_ASSERT(false, "State should have been Pending or Running");
  }
}

void RemoteWorkerChild::TransitionStateFromPendingToCanceled(
    RemoteWorkerState& aState) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aState.is<Pending>());
  LOG(("TransitionStateFromPendingToCanceled[this=%p]", this));

  CancelAllPendingOps(aState);

  aState = VariantType<remoteworker::Canceled>();
}

void RemoteWorkerChild::TransitionStateFromCanceledToKilled() {
  AssertIsOnMainThread();

  LOG(("TransitionStateFromCanceledToKilled[this=%p]", this));

  auto lock = mState.Lock();
  MOZ_ASSERT(lock->is<Canceled>());

  *lock = VariantType<remoteworker::Killed>();

  RefPtr<RemoteWorkerChild> self = this;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(__func__, [self]() {
    auto launcherData = self->mLauncherData.Access();

    // (We maintain the historical ordering of resolving this promise prior to
    // calling SendClose, however the previous code used 2 separate dispatches
    // to this thread for the resolve and SendClose, and there inherently
    // would be a race between the runnables resulting from the resolved
    // promise and the promise containing the call to SendClose.  Now it's
    // entirely clear that our call to SendClose will effectively run before
    // any of the resolved promises are able to do anything.)
    launcherData->mTerminationPromise.ResolveIfExists(true, __func__);

    if (self->CanSend()) {
      Unused << self->SendClose();
    }
  });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void RemoteWorkerChild::TransitionStateToRunning() {
  AssertIsOnMainThread();

  LOG(("TransitionStateToRunning[this=%p]", this));

  nsTArray<RefPtr<RemoteWorkerOp>> pendingOps;

  {
    auto lock = mState.Lock();

    // Because this is an async notification sent from the worker to the main
    // thread, it's very possible that we've already decided on the main thread
    // to transition to the Canceled state, in which case there is nothing for
    // us to do here.
    if (!lock->is<Pending>()) {
      LOG(("State is already not pending in TransitionStateToRunning[this=%p]!",
           this));
      return;
    }

    RefPtr<WorkerPrivate> workerPrivate =
        std::move(lock->as<Pending>().mWorkerPrivate);
    pendingOps = std::move(lock->as<Pending>().mPendingOps);

    // Move the worker private into place to avoid gratuitous ref churn; prior
    // comments here suggest the Variant can't accept a move.
    *lock = VariantType<remoteworker::Running>();
    lock->as<Running>().mWorkerPrivate = std::move(workerPrivate);
  }

  CreationSucceededOnAnyThread();

  RefPtr<RemoteWorkerChild> self = this;
  for (auto& op : pendingOps) {
    op->StartOnMainThread(self);
  }
}

void RemoteWorkerChild::ExceptionalErrorTransitionDuringExecWorker() {
  AssertIsOnMainThread();

  LOG(("ExceptionalErrorTransitionDuringExecWorker[this=%p]", this));

  // This method is called synchronously by ExecWorkerOnMainThread in the event
  // of any error.  Because we only transition to Running on the main thread
  // as the result of a notification from the worker, we know our state will be
  // Pending, but mWorkerPrivate may or may not be null, as we may not have
  // gotten to spawning the worker.
  //
  // In the event the worker exists, we need to Cancel() it.  We must do this
  // without the lock held because our call to Cancel() will invoke the
  // cancellation callback we created which will call TransitionStateToCanceled,
  // and we can't be holding the lock when that happens.

  RefPtr<WorkerPrivate> cancelWith;

  {
    auto lock = mState.Lock();

    MOZ_ASSERT(lock->is<Pending>());
    if (lock->is<Pending>()) {
      cancelWith = lock->as<Pending>().mWorkerPrivate;
      if (!cancelWith) {
        // The worker wasn't actually created, so we should synthetically
        // transition to canceled and onward.  Since we have the lock,
        // perform the transition now for clarity, but we'll handle the rest of
        // this case after dropping the lock.
        TransitionStateFromPendingToCanceled(lock.ref());
      }
    }
  }

  if (cancelWith) {
    cancelWith->Cancel();
  } else {
    TransitionStateFromCanceledToKilled();
    CreationFailedOnAnyThread();
  }
}

void RemoteWorkerChild::CancelAllPendingOps(RemoteWorkerState& aState) {
  MOZ_ASSERT(aState.is<Pending>());

  auto pendingOps = std::move(aState.as<Pending>().mPendingOps);

  for (auto& op : pendingOps) {
    op->Cancel();
  }
}

void RemoteWorkerChild::PendRemoteWorkerOp(RefPtr<RemoteWorkerOp> aOp) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(mIsThawing);

  auto pendingOps = mPendingOps.Lock();

  pendingOps->AppendElement(std::move(aOp));
}

void RemoteWorkerChild::RunAllPendingOpsOnMainThread() {
  RefPtr<RemoteWorkerChild> self = this;

  auto pendingOps = mPendingOps.Lock();

  for (auto& op : *pendingOps) {
    op->StartOnMainThread(self);
  }

  pendingOps->Clear();
}

void RemoteWorkerChild::MaybeStartOp(RefPtr<RemoteWorkerOp>&& aOp) {
  MOZ_ASSERT(aOp);

  if (mIsThawing) {
    PendRemoteWorkerOp(std::move(aOp));
    return;
  }

  auto lock = mState.Lock();

  if (!aOp->MaybeStart(this, lock.ref())) {
    // Maybestart returns false only if we are <Pending>.
    lock->as<Pending>().mPendingOps.AppendElement(std::move(aOp));
  }
}

IPCResult RemoteWorkerChild::RecvExecOp(SharedWorkerOpArgs&& aOpArgs) {
  MOZ_ASSERT(!mIsServiceWorker);

  MaybeStartOp(new SharedWorkerOp(std::move(aOpArgs)));

  return IPC_OK();
}

IPCResult RemoteWorkerChild::RecvExecServiceWorkerOp(
    ServiceWorkerOpArgs&& aArgs, ExecServiceWorkerOpResolver&& aResolve) {
  MOZ_ASSERT(mIsServiceWorker);
  MOZ_ASSERT(
      aArgs.type() !=
          ServiceWorkerOpArgs::TParentToChildServiceWorkerFetchEventOpArgs,
      "FetchEvent operations should be sent via PFetchEventOp(Proxy) actors!");

  MaybeReportServiceWorkerShutdownProgress(aArgs);

  MaybeStartOp(ServiceWorkerOp::Create(std::move(aArgs), std::move(aResolve)));

  return IPC_OK();
}

RefPtr<GenericPromise>
RemoteWorkerChild::MaybeSendSetServiceWorkerSkipWaitingFlag() {
  RefPtr<GenericPromise::Private> promise =
      new GenericPromise::Private(__func__);

  RefPtr<RemoteWorkerChild> self = this;

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(__func__, [self = std::move(
                                                                  self),
                                                              promise] {
    if (!self->CanSend()) {
      promise->Reject(NS_ERROR_DOM_ABORT_ERR, __func__);
      return;
    }

    self->SendSetServiceWorkerSkipWaitingFlag()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [promise](
            const SetServiceWorkerSkipWaitingFlagPromise::ResolveOrRejectValue&
                aResult) {
          if (NS_WARN_IF(aResult.IsReject())) {
            promise->Reject(NS_ERROR_DOM_ABORT_ERR, __func__);
            return;
          }

          promise->Resolve(aResult.ResolveValue(), __func__);
        });
  });

  GetActorEventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);

  return promise;
}

/**
 * PFetchEventOpProxy methods
 */
already_AddRefed<PFetchEventOpProxyChild>
RemoteWorkerChild::AllocPFetchEventOpProxyChild(
    const ParentToChildServiceWorkerFetchEventOpArgs& aArgs) {
  return RefPtr{new FetchEventOpProxyChild()}.forget();
}

IPCResult RemoteWorkerChild::RecvPFetchEventOpProxyConstructor(
    PFetchEventOpProxyChild* aActor,
    const ParentToChildServiceWorkerFetchEventOpArgs& aArgs) {
  MOZ_ASSERT(aActor);

  (static_cast<FetchEventOpProxyChild*>(aActor))->Initialize(aArgs);

  return IPC_OK();
}

}  // namespace dom
}  // namespace mozilla
