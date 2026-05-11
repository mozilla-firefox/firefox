/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerManager.h"

#include <utility>

#include "RemoteWorkerServiceParent.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/dom/ContentChild.h"  // ContentChild::GetSingleton
#include "mozilla/dom/PRemoteWorkerNonLifeCycleOpControllerChild.h"
#include "mozilla/dom/PRemoteWorkerNonLifeCycleOpControllerParent.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/dom/RemoteWorkerController.h"
#include "mozilla/dom/RemoteWorkerNonLifeCycleOpControllerParent.h"
#include "mozilla/dom/RemoteWorkerParent.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundParent.h"
#include "mozilla/net/CookieServiceParent.h"
#include "mozilla/net/NeckoParent.h"
#include "nsCOMPtr.h"
#include "nsIXULRuntime.h"
#include "nsImportModule.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

mozilla::LazyLogModule gRemoteWorkerManagerLog("RemoteWorkerManager");

#ifdef LOG
#  undef LOG
#endif
#define LOG(fmt) \
  MOZ_LOG(gRemoteWorkerManagerLog, mozilla::LogLevel::Verbose, fmt)

namespace mozilla {

using namespace ipc;
using namespace net;

namespace dom {

namespace {

// Raw pointer because this object is kept alive by RemoteWorkerServiceParent
// actors.
RemoteWorkerManager* sRemoteWorkerManager;

bool IsServiceWorker(const RemoteWorkerData& aData) {
  return aData.serviceWorkerData().type() ==
         OptionalServiceWorkerData::TServiceWorkerData;
}

void TransmitPermissionsAndCookiesAndBlobURLsForPrincipalInfo(
    ContentParent* aContentParent, const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aContentParent);

  auto principalOrErr = PrincipalInfoToPrincipal(aPrincipalInfo);

  if (NS_WARN_IF(principalOrErr.isErr())) {
    return;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  aContentParent->TransmitBlobURLsForPrincipal(principal);

  MOZ_ALWAYS_SUCCEEDS(
      aContentParent->TransmitPermissionsForPrincipal(principal));

  CookieServiceParent* cs = nullptr;

  PNeckoParent* neckoParent =
      LoneManagedOrNullAsserts(aContentParent->ManagedPNeckoParent());
  if (neckoParent) {
    PCookieServiceParent* csParent =
        LoneManagedOrNullAsserts(neckoParent->ManagedPCookieServiceParent());
    if (csParent) {
      cs = static_cast<CookieServiceParent*>(csParent);
    }
  }

  if (cs) {
    nsCOMPtr<nsIURI> uri = principal->GetURI();
    cs->UpdateCookieInContentList(uri, principal->OriginAttributesRef());
  } else {
    aContentParent->AddPrincipalToCookieInProcessCache(principal);
  }
}

}  // namespace

// static
bool RemoteWorkerManager::MatchRemoteType(const nsACString& processRemoteType,
                                          const nsACString& workerRemoteType) {
  LOG(("MatchRemoteType [processRemoteType=%s, workerRemoteType=%s]",
       PromiseFlatCString(processRemoteType).get(),
       PromiseFlatCString(workerRemoteType).get()));

  // Respecting COOP and COEP requires processing headers in the parent
  // process in order to choose an appropriate content process, but the
  // workers' ScriptLoader processes headers in content processes. An
  // intermediary step that provides security guarantees is to simply never
  // allow SharedWorkers and ServiceWorkers to exist in a COOP+COEP process.
  // The ultimate goal is to allow these worker types to be put in such
  // processes based on their script response headers.
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1595206
  //
  // RemoteWorkerManager::GetRemoteType should not select this remoteType
  // and so workerRemoteType is not expected to be set to a coop+coep
  // remoteType and here we can just assert that it is not happening.
  MOZ_ASSERT(!IsWebCoopCoepRemoteType(workerRemoteType));

  return processRemoteType.Equals(workerRemoteType);
}

// static
Result<nsCString, nsresult> RemoteWorkerManager::GetRemoteType(
    const nsCOMPtr<nsIPrincipal>& aPrincipal, WorkerKind aWorkerKind) {
  AssertIsOnMainThread();

  MOZ_ASSERT_IF(aWorkerKind == WorkerKind::WorkerKindService,
                aPrincipal->GetIsContentPrincipal());

  // If E10S is fully disabled, there are no decisions to be made, and we need
  // to finish the load in the parent process.
  if (!BrowserTabsRemoteAutostart()) {
    LOG(("GetRemoteType: Loading in parent process as e10s is disabled"));
    return NOT_REMOTE_TYPE;
  }

  nsCString preferredRemoteType = DEFAULT_REMOTE_TYPE;
  if (aWorkerKind == WorkerKind::WorkerKindShared) {
    if (auto* contentChild = ContentChild::GetSingleton()) {
      // For a shared worker set the preferred remote type to the content
      // child process remote type.
      preferredRemoteType = contentChild->GetRemoteType();
    } else if (aPrincipal->IsSystemPrincipal()) {
      preferredRemoteType = NOT_REMOTE_TYPE;
    }
  }

  auto result = IsolationOptionsForWorker(
      aPrincipal, aWorkerKind, preferredRemoteType, FissionAutostart());
  if (NS_WARN_IF(result.isErr())) {
    LOG(("GetRemoteType Abort: IsolationOptionsForWorker failed"));
    return Err(NS_ERROR_DOM_ABORT_ERR);
  }
  auto options = result.unwrap();

  if (MOZ_LOG_TEST(gRemoteWorkerManagerLog, LogLevel::Verbose)) {
    nsCString principalOrigin;
    aPrincipal->GetOrigin(principalOrigin);

    LOG(
        ("GetRemoteType workerType=%s, principal=%s, "
         "preferredRemoteType=%s, selectedRemoteType=%s",
         aWorkerKind == WorkerKind::WorkerKindService ? "service" : "shared",
         principalOrigin.get(), preferredRemoteType.get(),
         options.mRemoteType.get()));
  }

  return options.mRemoteType;
}

// static
bool RemoteWorkerManager::HasExtensionPrincipal(const RemoteWorkerData& aData) {
  auto principalInfo = aData.principalInfo();
  return principalInfo.type() == PrincipalInfo::TContentPrincipalInfo &&
         // This helper method is also called from the background thread and so
         // we can't check if the principal does have an addonPolicy object
         // associated and we have to resort to check the url scheme instead.
         StringBeginsWith(principalInfo.get_ContentPrincipalInfo().spec(),
                          "moz-extension://"_ns);
}

/* static */
already_AddRefed<RemoteWorkerManager> RemoteWorkerManager::GetOrCreate() {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();

  if (!sRemoteWorkerManager) {
    sRemoteWorkerManager = new RemoteWorkerManager();
  }

  RefPtr<RemoteWorkerManager> rwm = sRemoteWorkerManager;
  return rwm.forget();
}

RemoteWorkerManager::RemoteWorkerManager() : mParentActor(nullptr) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!sRemoteWorkerManager);
}

RemoteWorkerManager::~RemoteWorkerManager() {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(sRemoteWorkerManager == this);
  sRemoteWorkerManager = nullptr;
}

void RemoteWorkerManager::RegisterActor(RemoteWorkerServiceParent* aActor) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  if (!aActor->IsOtherProcessActor()) {
    MOZ_ASSERT(!mParentActor);
    mParentActor = aActor;
    return;
  }

  MOZ_ASSERT(!mChildActors.Contains(aActor));
  mChildActors.AppendElement(aActor);
}

void RemoteWorkerManager::UnregisterActor(RemoteWorkerServiceParent* aActor) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  if (aActor == mParentActor) {
    mParentActor = nullptr;
  } else {
    MOZ_ASSERT(mChildActors.Contains(aActor));
    mChildActors.RemoveElement(aActor);
  }
}

void RemoteWorkerManager::Launch(RemoteWorkerController* aController,
                                 const RemoteWorkerData& aData,
                                 base::ProcessId aProcessId) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();

  TargetActorAndKeepAlive target = SelectTargetActor(aData, aProcessId);

  // If there is no available actor, try to start a process, and connect to it.
  if (!target.mActor) {
    // Launching is async, so we cannot check for failures right here.
    LaunchNewContentProcess(aData)->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self = RefPtr{this}, controller = RefPtr{aController},
         data = aData](TargetActorAndKeepAlive&& aTarget) {
          if (aTarget.mActor->CanSend()) {
            self->LaunchInternal(controller, aTarget.mActor,
                                 std::move(aTarget.mKeepAlive), data);
          } else {
            controller->CreationFailed();
          }
        },
        [controller = RefPtr{aController}](nsresult) {
          controller->CreationFailed();
        });
    return;
  }

  LaunchInternal(aController, target.mActor, std::move(target.mKeepAlive),
                 aData);
}

void RemoteWorkerManager::LaunchInternal(
    RemoteWorkerController* aController,
    RemoteWorkerServiceParent* aTargetActor,
    UniqueThreadsafeContentParentKeepAlive&& aKeepAlive,
    const RemoteWorkerData& aData) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aController);
  MOZ_ASSERT(aTargetActor);
  MOZ_ASSERT(aTargetActor == mParentActor ||
             mChildActors.Contains(aTargetActor));

  // We need to send permissions to content processes, but not if we're spawning
  // the worker here in the parent process.
  if (aTargetActor != mParentActor) {
    MOZ_ASSERT(aKeepAlive);

    // This won't cause any race conditions because the content process
    // should wait for the permissions to be received before executing the
    // Service Worker.
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__, [contentHandle = RefPtr{aKeepAlive.get()},
                   principalInfo = aData.principalInfo()] {
          AssertIsOnMainThread();
          if (RefPtr<ContentParent> contentParent =
                  contentHandle->GetContentParent()) {
            TransmitPermissionsAndCookiesAndBlobURLsForPrincipalInfo(
                contentParent, principalInfo);
          }
        });

    MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));
  }

  RefPtr<RemoteWorkerParent> workerActor =
      MakeAndAddRef<RemoteWorkerParent>(std::move(aKeepAlive));

  mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerParent> parentEp;
  mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild> childEp;
  MOZ_ALWAYS_SUCCEEDS(PRemoteWorkerNonLifeCycleOpController::CreateEndpoints(
      &parentEp, &childEp));

  MOZ_ASSERT(!aController->mNonLifeCycleOpController);
  aController->mNonLifeCycleOpController =
      MakeAndAddRef<RemoteWorkerNonLifeCycleOpControllerParent>(aController);

  parentEp.Bind(aController->mNonLifeCycleOpController);

  if (!aTargetActor->SendPRemoteWorkerConstructor(workerActor, aData,
                                                  std::move(childEp))) {
    AsyncCreationFailed(aController);
    return;
  }

  // This makes the link better the 2 actors.
  aController->SetWorkerActor(workerActor);
  workerActor->SetController(aController);
}

void RemoteWorkerManager::AsyncCreationFailed(
    RemoteWorkerController* aController) {
  RefPtr<RemoteWorkerController> controller = aController;
  nsCOMPtr<nsIRunnable> r =
      NS_NewRunnableFunction("RemoteWorkerManager::AsyncCreationFailed",
                             [controller]() { controller->CreationFailed(); });

  NS_DispatchToCurrentThread(r.forget());
}

template <typename Callback>
void RemoteWorkerManager::ForEachActor(
    Callback&& aCallback, const nsACString& aRemoteType,
    Maybe<base::ProcessId> aProcessId) const {
  AssertIsOnBackgroundThread();

  const auto length = mChildActors.Length();

  auto end = static_cast<uint32_t>(rand()) % length;
  if (aProcessId) {
    // Start from the actor with the given processId instead of starting from
    // a random index.
    for (auto j = length - 1; j > 0; j--) {
      if (mChildActors[j]->OtherPid() == *aProcessId) {
        end = j;
        break;
      }
    }
  }

  uint32_t i = end;

  do {
    MOZ_ASSERT(i < mChildActors.Length());
    RemoteWorkerServiceParent* actor = mChildActors[i];

    if (MatchRemoteType(actor->GetRemoteType(), aRemoteType)) {
      ThreadsafeContentParentHandle* contentHandle =
          actor->GetContentParentHandle();

      if (!aCallback(actor, contentHandle)) {
        break;
      }
    }

    i = (i + 1) % length;
  } while (i != end);
}

/**
 * When selecting a target actor for a given remote worker, we have to consider
 * that:
 *
 * - Service Workers can spawn even when their registering page/script isn't
 *   active (e.g. push notifications), so we don't attempt to spawn the worker
 *   in its registering script's process. We search linearly and choose the
 *   search's starting position randomly.
 *
 * - When Fission is enabled, Shared Workers may have to be spawned into
 *   different child process from the one where it has been registered from, and
 *   that child process may be going to be marked as dead and shutdown.
 *
 * ContentParent provides a way to add a KeepAlive, which will prevent the
 * process from being shut down, through a ThreadsafeContentParentHandle in an
 * atomic way. This call will fail if the process is already being shut down.
 * When selecting a content process on the PBackground thread, we'll acquire the
 * KeepAlive in that way.
 */
RemoteWorkerManager::TargetActorAndKeepAlive
RemoteWorkerManager::SelectTargetActorInternal(
    const RemoteWorkerData& aData, base::ProcessId aProcessId) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mChildActors.IsEmpty());

  RemoteWorkerServiceParent* actor = nullptr;
  UniqueThreadsafeContentParentKeepAlive keepAlive;

  const auto& workerRemoteType = aData.remoteType();

  ForEachActor(
      [&](RemoteWorkerServiceParent* aActor,
          ThreadsafeContentParentHandle* aContentHandle) {
        // Make sure to choose an actor related to a child process that is not
        // going to shutdown while we are still in the process of launching the
        // remote worker.
        //
        // ForEachActor will start from the child actor coming from the child
        // process with a pid equal to aProcessId if any, otherwise it would
        // start from a random actor in the mChildActors array, this guarantees
        // that we will choose that actor if it does also match the remote type.
        if ((keepAlive = aContentHandle->TryAddKeepAlive())) {
          actor = aActor;
          return false;
        }
        MOZ_ASSERT(!actor);
        return true;
      },
      workerRemoteType, IsServiceWorker(aData) ? Nothing() : Some(aProcessId));

  return {actor, std::move(keepAlive)};
}

RemoteWorkerManager::TargetActorAndKeepAlive
RemoteWorkerManager::SelectTargetActor(const RemoteWorkerData& aData,
                                       base::ProcessId aProcessId) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();

  // System principal workers should run on the parent process.
  if (aData.principalInfo().type() == PrincipalInfo::TSystemPrincipalInfo) {
    MOZ_ASSERT(mParentActor);
    return {mParentActor, nullptr};
  }

  // Extension principal workers are allowed to run on the parent process
  // when "extensions.webextensions.remote" pref is false.
  if (aProcessId == base::GetCurrentProcId() &&
      aData.remoteType().Equals(NOT_REMOTE_TYPE) &&
      !StaticPrefs::extensions_webextensions_remote() &&
      HasExtensionPrincipal(aData)) {
    MOZ_ASSERT(mParentActor);
    return {mParentActor, nullptr};
  }

  // If e10s is off, use the parent process.
  if (!BrowserTabsRemoteAutostart()) {
    MOZ_ASSERT(mParentActor);
    return {mParentActor, nullptr};
  }

  // We shouldn't have to worry about content-principal parent-process workers.
  MOZ_ASSERT(aProcessId != base::GetCurrentProcId());

  if (mChildActors.IsEmpty()) {
    return {nullptr, nullptr};
  }

  return SelectTargetActorInternal(aData, aProcessId);
}

RefPtr<RemoteWorkerManager::LaunchProcessPromise>
RemoteWorkerManager::LaunchNewContentProcess(const RemoteWorkerData& aData) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();

  // Request a process making sure to specify aPreferUsed=true.  For a given
  // remoteType there's a pool size limit.  If we pass aPreferUsed here, then if
  // there's any process in the pool already, we will use that.  If we pass
  // false (which is the default if omitted), then this call will spawn a new
  // process if the pool isn't at its limit yet.
  //
  // (Our intent is never to grow the pool size here.  Our logic gets here
  // because our current logic on PBackground is only aware of
  // RemoteWorkerServiceParent actors that have registered themselves, which is
  // fundamentally unaware of processes that will match in the future when they
  // register.  So we absolutely are fine with and want any existing processes.)
  return InvokeAsync(GetMainThreadSerialEventTarget(), __func__,
                     [remoteType = aData.remoteType()]() {
                       if (AppShutdown::IsInOrBeyond(
                               ShutdownPhase::AppShutdownConfirmed)) {
                         return ContentParent::LaunchPromise::CreateAndReject(
                             NS_ERROR_ILLEGAL_DURING_SHUTDOWN, __func__);
                       }

                       return ContentParent::GetNewOrUsedBrowserProcessAsync(
                           /* aRemoteType = */ remoteType,
                           /* aGroup */ nullptr,
                           hal::ProcessPriority::PROCESS_PRIORITY_FOREGROUND,
                           /* aPreferUsed */ true);
                     })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [](UniqueContentParentKeepAlive&& aContentParent) {
            RefPtr<RemoteWorkerServiceParent> actor =
                aContentParent->GetRemoteWorkerServiceParent();
            MOZ_ASSERT(actor, "RemoteWorkerServiceParent not initialized?");
            return RemoteWorkerManager::LaunchProcessPromise::CreateAndResolve(
                TargetActorAndKeepAlive{
                    actor, UniqueContentParentKeepAliveToThreadsafe(
                               std::move(aContentParent))},
                __func__);
          },
          [](nsresult aError) {
            return RemoteWorkerManager::LaunchProcessPromise::CreateAndReject(
                aError, __func__);
          });
}

}  // namespace dom
}  // namespace mozilla
