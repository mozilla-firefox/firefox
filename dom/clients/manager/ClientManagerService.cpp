/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientManagerService.h"

#include "ClientHandleParent.h"
#include "ClientManagerParent.h"
#include "ClientNavigateOpParent.h"
#include "ClientOpenWindowUtils.h"
#include "ClientPrincipalUtils.h"
#include "ClientSourceParent.h"
#include "jsfriendapi.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/MozPromise.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsIAsyncShutdown.h"
#include "nsIXULRuntime.h"
#include "nsProxyRelease.h"

namespace mozilla::dom {

using mozilla::ipc::AssertIsOnBackgroundThread;
using mozilla::ipc::PrincipalInfo;

namespace {

ClientManagerService* sClientManagerServiceInstance = nullptr;
bool sClientManagerServiceShutdownRegistered = false;

class ClientShutdownBlocker final : public nsIAsyncShutdownBlocker {
  RefPtr<GenericPromise::Private> mPromise;

  ~ClientShutdownBlocker() = default;

 public:
  explicit ClientShutdownBlocker(GenericPromise::Private* aPromise)
      : mPromise(aPromise) {
    MOZ_DIAGNOSTIC_ASSERT(mPromise);
  }

  NS_IMETHOD
  GetName(nsAString& aNameOut) override {
    aNameOut = nsLiteralString(
        u"ClientManagerService: start destroying IPC actors early");
    return NS_OK;
  }

  NS_IMETHOD
  BlockShutdown(nsIAsyncShutdownClient* aClient) override {
    mPromise->Resolve(true, __func__);
    aClient->RemoveBlocker(this);
    return NS_OK;
  }

  NS_IMETHOD
  GetState(nsIPropertyBag**) override { return NS_OK; }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS(ClientShutdownBlocker, nsIAsyncShutdownBlocker)

// Helper function the resolves a MozPromise when we detect that the browser
// has begun to shutdown.
RefPtr<GenericPromise> OnShutdown() {
  RefPtr<GenericPromise::Private> ref = new GenericPromise::Private(__func__);

  nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableFunction("ClientManagerServer::OnShutdown", [ref]() {
        nsCOMPtr<nsIAsyncShutdownService> svc =
            components::AsyncShutdown::Service();
        if (!svc) {
          ref->Resolve(true, __func__);
          return;
        }

        nsCOMPtr<nsIAsyncShutdownClient> phase;
        MOZ_ALWAYS_SUCCEEDS(svc->GetXpcomWillShutdown(getter_AddRefs(phase)));
        if (!phase) {
          ref->Resolve(true, __func__);
          return;
        }

        nsCOMPtr<nsIAsyncShutdownBlocker> blocker =
            new ClientShutdownBlocker(ref);
        nsresult rv =
            phase->AddBlocker(blocker, NS_LITERAL_STRING_FROM_CSTRING(__FILE__),
                              __LINE__, u"ClientManagerService shutdown"_ns);

        if (NS_FAILED(rv)) {
          ref->Resolve(true, __func__);
          return;
        }
      });

  MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));

  return ref;
}

}  // anonymous namespace

ClientManagerService::FutureClientSourceParent::FutureClientSourceParent(
    const IPCClientInfo& aClientInfo)
    : mPrincipalInfo(aClientInfo.principalInfo()), mAssociated(false) {}

ClientManagerService::ClientManagerService() : mShutdown(false) {
  AssertIsOnBackgroundThread();

  // Only register one shutdown handler at a time.  If a previous service
  // instance did this, but shutdown has not come, then we can avoid
  // doing it again.
  if (!sClientManagerServiceShutdownRegistered) {
    sClientManagerServiceShutdownRegistered = true;

    // While the ClientManagerService will be gracefully terminated as windows
    // and workers are naturally killed, this can cause us to do extra work
    // relatively late in the shutdown process.  To avoid this we eagerly begin
    // shutdown at the first sign it has begun.  Since we handle normal shutdown
    // gracefully we don't really need to block anything here.  We just begin
    // destroying our IPC actors immediately.
    OnShutdown()->Then(GetCurrentSerialEventTarget(), __func__, []() {
      // Look up the latest service instance, if it exists.  This may
      // be different from the instance that registered the shutdown
      // handler.
      RefPtr<ClientManagerService> svc = ClientManagerService::GetInstance();
      if (svc) {
        svc->Shutdown();
      }
    });
  }
}

ClientManagerService::~ClientManagerService() {
  AssertIsOnBackgroundThread();
  MOZ_DIAGNOSTIC_ASSERT(mSourceTable.Count() == 0);
  MOZ_DIAGNOSTIC_ASSERT(mManagerList.IsEmpty());

  MOZ_DIAGNOSTIC_ASSERT(sClientManagerServiceInstance == this);
  sClientManagerServiceInstance = nullptr;
}

void ClientManagerService::Shutdown() {
  AssertIsOnBackgroundThread();
  MOZ_DIAGNOSTIC_ASSERT(sClientManagerServiceShutdownRegistered);

  // If many ClientManagerService are created and destroyed quickly we can
  // in theory get more than one shutdown listener calling us.
  if (mShutdown) {
    return;
  }
  mShutdown = true;

  // Begin destroying our various manager actors which will in turn destroy
  // all source, handle, and operation actors.
  for (auto actor :
       CopyableAutoTArray<ClientManagerParent*, 16>(mManagerList)) {
    (void)PClientManagerParent::Send__delete__(actor);
  }

  // Destroying manager actors should've also destroyed all source actors, so
  // the only sources left will be future sources, which need to be aborted.
  for (auto& entry : mSourceTable) {
    MOZ_RELEASE_ASSERT(entry.GetData().is<FutureClientSourceParent>());
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Client creation aborted.");
    entry.GetModifiableData()
        ->as<FutureClientSourceParent>()
        .RejectPromiseIfExists(rv);
  }
  mSourceTable.Clear();
}

ClientSourceParent* ClientManagerService::MaybeUnwrapAsExistingSource(
    const SourceTableEntry& aEntry) const {
  AssertIsOnBackgroundThread();

  if (aEntry.is<FutureClientSourceParent>()) {
    return nullptr;
  }

  return aEntry.as<ClientSourceParent*>();
}

ClientSourceParent* ClientManagerService::FindExistingSource(
    const nsID& aID, const PrincipalInfo& aPrincipalInfo) const {
  AssertIsOnBackgroundThread();

  auto entry = mSourceTable.Lookup(aID);

  if (!entry) {
    return nullptr;
  }

  ClientSourceParent* source = MaybeUnwrapAsExistingSource(entry.Data());

  if (!source || NS_WARN_IF(!ClientMatchPrincipalInfo(
                     source->Info().PrincipalInfo(), aPrincipalInfo))) {
    return nullptr;
  }
  return source;
}

// static
already_AddRefed<ClientManagerService>
ClientManagerService::GetOrCreateInstance() {
  AssertIsOnBackgroundThread();

  if (!sClientManagerServiceInstance) {
    sClientManagerServiceInstance = new ClientManagerService();
  }

  RefPtr<ClientManagerService> ref(sClientManagerServiceInstance);
  return ref.forget();
}

// static
already_AddRefed<ClientManagerService> ClientManagerService::GetInstance() {
  AssertIsOnBackgroundThread();

  if (!sClientManagerServiceInstance) {
    return nullptr;
  }

  RefPtr<ClientManagerService> ref(sClientManagerServiceInstance);
  return ref.forget();
}

namespace {

bool IsNullPrincipalInfo(const PrincipalInfo& aPrincipalInfo) {
  return aPrincipalInfo.type() == PrincipalInfo::TNullPrincipalInfo;
}

bool AreBothNullPrincipals(const PrincipalInfo& aPrincipalInfo1,
                           const PrincipalInfo& aPrincipalInfo2) {
  return IsNullPrincipalInfo(aPrincipalInfo1) &&
         IsNullPrincipalInfo(aPrincipalInfo2);
}

}  // anonymous namespace

bool ClientManagerService::AddSource(ClientSourceParent* aSource) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aSource);

  auto entry = mSourceTable.Lookup(aSource->Info().Id());
  if (entry) {
    // Do not permit overwriting an existing ClientSource with the same
    // UUID.  This would allow a spoofed ClientParentSource actor to
    // intercept postMessage() intended for the real actor.
    if (entry.Data().is<ClientSourceParent*>()) {
      return false;
    }
    FutureClientSourceParent& placeHolder =
        entry.Data().as<FutureClientSourceParent>();

    const PrincipalInfo& placeHolderPrincipalInfo = placeHolder.PrincipalInfo();
    const PrincipalInfo& sourcePrincipalInfo = aSource->Info().PrincipalInfo();

    // The placeholder FutureClientSourceParent's PrincipalInfo must match the
    // real ClientSourceParent's PrincipalInfo. The only exception is if both
    // are null principals (two null principals are considered unequal).
    if (!AreBothNullPrincipals(placeHolderPrincipalInfo, sourcePrincipalInfo) &&
        NS_WARN_IF(!ClientMatchPrincipalInfo(placeHolderPrincipalInfo,
                                             sourcePrincipalInfo))) {
      return false;
    }

    placeHolder.ResolvePromiseIfExists();
    *entry = AsVariant(aSource);
    return true;
  }
  if (!mSourceTable.WithEntryHandle(aSource->Info().Id(),
                                    [&aSource](auto&& entry) {
                                      if (NS_WARN_IF(entry.HasEntry())) {
                                        return false;
                                      }
                                      entry.Insert(AsVariant(aSource));
                                      return true;
                                    })) {
    return false;
  }
  return true;
}

bool ClientManagerService::RemoveSource(ClientSourceParent* aSource) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aSource);
  auto entry = mSourceTable.Lookup(aSource->Info().Id());
  if (NS_WARN_IF(!entry)) {
    return false;
  }
  entry.Remove();
  return true;
}

bool ClientManagerService::ExpectFutureSource(
    const IPCClientInfo& aClientInfo) {
  AssertIsOnBackgroundThread();

  if (!mSourceTable.WithEntryHandle(
          aClientInfo.id(), [&aClientInfo](auto&& entry) {
            // Prevent overwrites.
            if (entry.HasEntry()) {
              return false;
            }
            entry.Insert(SourceTableEntry(
                VariantIndex<0>(), FutureClientSourceParent(aClientInfo)));
            return true;
          })) {
    return false;
  }

  return true;
}

void ClientManagerService::ForgetFutureSource(
    const IPCClientInfo& aClientInfo) {
  AssertIsOnBackgroundThread();

  auto entry = mSourceTable.Lookup(aClientInfo.id());

  if (entry) {
    if (entry.Data().is<ClientSourceParent*>()) {
      return;
    }

    // For non-e10s case, ClientChannelHelperParent will be freed before real
    // ClientSourceParnet be created. In the end this methoed will be called to
    // release the FutureClientSourceParent. That means a ClientHandle operation
    // which waits for the FutureClientSourceParent will have no chance to
    // connect to the ClientSourceParent. So the FutureClientSourceParent should
    // be keep in this case.
    // IsAssociated() makes sure there is a ClientHandle operation associated
    // with it.
    // More details please refer
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1730350#c2
    if (!XRE_IsE10sParentProcess() &&
        entry.Data().as<FutureClientSourceParent>().IsAssociated()) {
      return;
    }

    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Client creation aborted.");
    entry.Data().as<FutureClientSourceParent>().RejectPromiseIfExists(rv);

    entry.Remove();
  }
}

RefPtr<SourcePromise> ClientManagerService::FindSource(
    const nsID& aID, const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnBackgroundThread();

  auto entry = mSourceTable.Lookup(aID);
  if (!entry) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Unknown client.");
    return SourcePromise::CreateAndReject(rv, __func__);
  }

  if (entry.Data().is<FutureClientSourceParent>()) {
    entry.Data().as<FutureClientSourceParent>().SetAsAssociated();
    return entry.Data().as<FutureClientSourceParent>().Promise();
  }

  ClientSourceParent* source = entry.Data().as<ClientSourceParent*>();
  if (NS_WARN_IF(!ClientMatchPrincipalInfo(source->Info().PrincipalInfo(),
                                           aPrincipalInfo))) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Unknown client.");
    return SourcePromise::CreateAndReject(rv, __func__);
  }

  return SourcePromise::CreateAndResolve(true, __func__);
}

void ClientManagerService::AddManager(ClientManagerParent* aManager) {
  AssertIsOnBackgroundThread();
  MOZ_DIAGNOSTIC_ASSERT(aManager);
  MOZ_ASSERT(!mManagerList.Contains(aManager));
  mManagerList.AppendElement(aManager);

  // If shutdown has already begun then immediately destroy the actor.
  if (mShutdown) {
    (void)PClientManagerParent::Send__delete__(aManager);
  }
}

void ClientManagerService::RemoveManager(ClientManagerParent* aManager) {
  AssertIsOnBackgroundThread();
  MOZ_DIAGNOSTIC_ASSERT(aManager);
  DebugOnly<bool> removed = mManagerList.RemoveElement(aManager);
  MOZ_ASSERT(removed);
}

RefPtr<ClientOpPromise> ClientManagerService::Navigate(
    ThreadsafeContentParentHandle* aOriginContent,
    const ClientNavigateArgs& aArgs) {
  ClientSourceParent* source =
      FindExistingSource(aArgs.target().id(), aArgs.target().principalInfo());
  if (!source) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Unknown client");
    return ClientOpPromise::CreateAndReject(rv, __func__);
  }

  const IPCServiceWorkerDescriptor& serviceWorker = aArgs.serviceWorker();

  // Per https://w3c.github.io/ServiceWorker/#dom-windowclient-navigate step 4,
  // if the service worker does not control the client, reject with a TypeError.
  const Maybe<ServiceWorkerDescriptor>& controller = source->GetController();
  if (controller.isNothing() ||
      controller.ref().Scope() != serviceWorker.scope() ||
      controller.ref().Id() != serviceWorker.id()) {
    CopyableErrorResult rv;
    rv.ThrowTypeError("Client is not controlled by this Service Worker");
    return ClientOpPromise::CreateAndReject(rv, __func__);
  }

  PClientManagerParent* manager = source->Manager();
  MOZ_DIAGNOSTIC_ASSERT(manager);

  // This is safe to do because the ClientSourceChild cannot directly delete
  // itself.  Instead it sends a Teardown message to the parent which then
  // calls delete.  That means we can be sure that we are not racing with
  // source destruction here.
  ClientNavigateOpConstructorArgs args(
      WrapNotNull(source), NotNull<RefPtr<nsIURI>>(aArgs.uri()),
      NotNull<RefPtr<nsIURI>>(aArgs.baseURI()));

  RefPtr<ClientOpPromise::Private> promise =
      new ClientOpPromise::Private(__func__);

  ClientNavigateOpParent* op = new ClientNavigateOpParent(args, promise);
  PClientNavigateOpParent* result =
      manager->SendPClientNavigateOpConstructor(op, args);
  if (!result) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Client is aborted");
    promise->Reject(rv, __func__);
  }

  return promise;
}

namespace {

class PromiseListHolder final {
  RefPtr<ClientOpPromise::Private> mResultPromise;
  nsTArray<RefPtr<ClientOpPromise>> mPromiseList;
  nsTArray<ClientInfoAndState> mResultList;
  uint32_t mOutstandingPromiseCount;

  void ProcessSuccess(const ClientInfoAndState& aResult) {
    mResultList.AppendElement(aResult);
    ProcessCompletion();
  }

  void ProcessCompletion() {
    MOZ_DIAGNOSTIC_ASSERT(mOutstandingPromiseCount > 0);
    mOutstandingPromiseCount -= 1;
    MaybeFinish();
  }

  ~PromiseListHolder() = default;

 public:
  PromiseListHolder()
      : mResultPromise(new ClientOpPromise::Private(__func__)),
        mOutstandingPromiseCount(0) {}

  RefPtr<ClientOpPromise> GetResultPromise() {
    RefPtr<PromiseListHolder> kungFuDeathGrip = this;
    return mResultPromise->Then(
        GetCurrentSerialEventTarget(), __func__,
        [kungFuDeathGrip](const ClientOpPromise::ResolveOrRejectValue& aValue) {
          return ClientOpPromise::CreateAndResolveOrReject(aValue, __func__);
        });
  }

  void AddPromise(RefPtr<ClientOpPromise>&& aPromise) {
    mPromiseList.AppendElement(std::move(aPromise));
    MOZ_DIAGNOSTIC_ASSERT(mPromiseList.LastElement());
    mOutstandingPromiseCount += 1;

    RefPtr<PromiseListHolder> self(this);
    mPromiseList.LastElement()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self](const ClientOpResult& aResult) {
          // TODO: This is pretty clunky.  Try to figure out a better
          //       wait for MatchAll() and Claim() to share this code
          //       even though they expect different return values.
          if (aResult.type() == ClientOpResult::TClientInfoAndState) {
            self->ProcessSuccess(aResult.get_ClientInfoAndState());
          } else {
            self->ProcessCompletion();
          }
        },
        [self](const CopyableErrorResult& aResult) {
          self->ProcessCompletion();
        });
  }

  void MaybeFinish() {
    if (!mOutstandingPromiseCount) {
      mResultPromise->Resolve(CopyableTArray(mResultList.Clone()), __func__);
    }
  }

  NS_INLINE_DECL_REFCOUNTING(PromiseListHolder)
};

}  // anonymous namespace

RefPtr<ClientOpPromise> ClientManagerService::MatchAll(
    ThreadsafeContentParentHandle* aOriginContent,
    const ClientMatchAllArgs& aArgs) {
  AssertIsOnBackgroundThread();

  ServiceWorkerDescriptor swd(aArgs.serviceWorker());
  const PrincipalInfo& principalInfo = swd.PrincipalInfo();

  RefPtr<PromiseListHolder> promiseList = new PromiseListHolder();

  for (const auto& entry : mSourceTable) {
    ClientSourceParent* source = MaybeUnwrapAsExistingSource(entry.GetData());

    if (!source || source->IsFrozen() || !source->ExecutionReady()) {
      continue;
    }

    if (aArgs.type() != ClientType::All &&
        source->Info().Type() != aArgs.type()) {
      continue;
    }

    if (!ClientMatchPrincipalInfo(source->Info().PrincipalInfo(),
                                  principalInfo)) {
      continue;
    }

    if (!aArgs.includeUncontrolled()) {
      const Maybe<ServiceWorkerDescriptor>& controller =
          source->GetController();
      if (controller.isNothing()) {
        continue;
      }

      if (controller.ref().Id() != swd.Id() ||
          controller.ref().Scope() != swd.Scope()) {
        continue;
      }
    }

    promiseList->AddPromise(source->StartOp(ClientGetInfoAndStateArgs(
        source->Info().Id(), source->Info().PrincipalInfo())));
  }

  // Maybe finish the promise now in case we didn't find any matching clients.
  promiseList->MaybeFinish();

  return promiseList->GetResultPromise();
}

namespace {

RefPtr<ClientOpPromise> ClaimOnMainThread(
    const ClientInfo& aClientInfo, const ServiceWorkerDescriptor& aDescriptor) {
  RefPtr<ClientOpPromise::Private> promise =
      new ClientOpPromise::Private(__func__);

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [promise, clientInfo = std::move(aClientInfo),
                 desc = std::move(aDescriptor)]() {
        auto scopeExit = MakeScopeExit([&] {
          // This will truncate the URLs if they have embedded nulls, if that
          // can happen, but for // our purposes here that's OK.
          nsPrintfCString err(
              "Service worker at <%s> can't claim Client at <%s>",
              desc.ScriptURL().get(), clientInfo.URL().get());
          CopyableErrorResult rv;
          rv.ThrowInvalidStateError(err);
          promise->Reject(rv, __func__);
        });

        RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
        NS_ENSURE_TRUE_VOID(swm);

        RefPtr<GenericErrorResultPromise> inner =
            swm->MaybeClaimClient(clientInfo, desc);
        inner->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [promise](bool aResult) {
              promise->Resolve(CopyableErrorResult(), __func__);
            },
            [promise](const CopyableErrorResult& aRv) {
              promise->Reject(aRv, __func__);
            });

        scopeExit.release();
      });

  MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));

  return promise;
}

}  // anonymous namespace

RefPtr<ClientOpPromise> ClientManagerService::Claim(
    ThreadsafeContentParentHandle* aOriginContent,
    const ClientClaimArgs& aArgs) {
  AssertIsOnBackgroundThread();

  const IPCServiceWorkerDescriptor& serviceWorker = aArgs.serviceWorker();
  const PrincipalInfo& principalInfo = serviceWorker.principalInfo();

  RefPtr<PromiseListHolder> promiseList = new PromiseListHolder();

  for (const auto& entry : mSourceTable) {
    ClientSourceParent* source = MaybeUnwrapAsExistingSource(entry.GetData());

    if (!source) {
      continue;
    }

    if (!ClientMatchPrincipalInfo(source->Info().PrincipalInfo(),
                                  principalInfo)) {
      continue;
    }

    const Maybe<ServiceWorkerDescriptor>& controller = source->GetController();
    if (controller.isSome() &&
        controller.ref().Scope() == serviceWorker.scope() &&
        controller.ref().Id() == serviceWorker.id()) {
      continue;
    }

    // TODO: This logic to determine if a service worker should control
    //       a particular client should be moved to the ServiceWorkerManager.
    //       This can't happen until the SWM is moved to the parent process,
    //       though.
    if (!source->ExecutionReady() ||
        source->Info().Type() == ClientType::Serviceworker ||
        source->Info().URL().Find(serviceWorker.scope()) != 0) {
      continue;
    }

    if (source->IsFrozen()) {
      (void)source->SendEvictFromBFCache();
      continue;
    }

    promiseList->AddPromise(ClaimOnMainThread(
        source->Info(), ServiceWorkerDescriptor(serviceWorker)));
  }

  // Maybe finish the promise now in case we didn't find any matching clients.
  promiseList->MaybeFinish();

  return promiseList->GetResultPromise();
}

RefPtr<ClientOpPromise> ClientManagerService::GetInfoAndState(
    ThreadsafeContentParentHandle* aOriginContent,
    const ClientGetInfoAndStateArgs& aArgs) {
  ClientSourceParent* source =
      FindExistingSource(aArgs.id(), aArgs.principalInfo());

  if (!source) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Unknown client");
    return ClientOpPromise::CreateAndReject(rv, __func__);
  }

  if (!source->ExecutionReady()) {
    RefPtr<ClientManagerService> self = this;

    // rejection ultimately converted to `undefined` in Clients::Get
    return source->ExecutionReadyPromise()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self = std::move(self), aArgs]() -> RefPtr<ClientOpPromise> {
          ClientSourceParent* source =
              self->FindExistingSource(aArgs.id(), aArgs.principalInfo());

          if (!source) {
            CopyableErrorResult rv;
            rv.ThrowInvalidStateError("Unknown client");
            return ClientOpPromise::CreateAndReject(rv, __func__);
          }

          return source->StartOp(aArgs);
        });
  }

  return source->StartOp(aArgs);
}

RefPtr<ClientOpPromise> ClientManagerService::OpenWindow(
    ThreadsafeContentParentHandle* aOriginContent,
    const ClientOpenWindowArgs& aArgs) {
  return InvokeAsync(GetMainThreadSerialEventTarget(), __func__,
                     [originContent = RefPtr{aOriginContent}, aArgs]()
                         MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                           return ClientOpenWindow(originContent, aArgs);
                         });
}

bool ClientManagerService::HasWindow(
    ThreadsafeContentParentHandle* aContentParentHandle,
    const PrincipalInfo& aPrincipalInfo, const nsID& aClientId) {
  AssertIsOnBackgroundThread();

  ClientSourceParent* source = FindExistingSource(aClientId, aPrincipalInfo);
  if (!source) {
    return false;
  }

  if (!source->ExecutionReady()) {
    return false;
  }

  if (source->Info().Type() != ClientType::Window) {
    return false;
  }

  if (!source->IsOwnedByProcess(aContentParentHandle)) {
    return false;
  }

  return true;
}

}  // namespace mozilla::dom
