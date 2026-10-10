/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerManager.h"

#include <algorithm>

#include "ServiceWorker.h"
#include "ServiceWorkerContainer.h"
#include "ServiceWorkerEvents.h"
#include "ServiceWorkerInfo.h"
#include "ServiceWorkerJobQueue.h"
#include "ServiceWorkerManagerChild.h"
#include "ServiceWorkerPrivate.h"
#include "ServiceWorkerQuotaUtils.h"
#include "ServiceWorkerRegisterJob.h"
#include "ServiceWorkerRegistrar.h"
#include "ServiceWorkerRegistration.h"
#include "ServiceWorkerScriptCache.h"
#include "ServiceWorkerShutdownBlocker.h"
#include "ServiceWorkerUnregisterJob.h"
#include "ServiceWorkerUpdateJob.h"
#include "ServiceWorkerUtils.h"
#include "jsapi.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ContentBlockingAllowList.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/LoadContext.h"
#include "mozilla/MozPromise.h"
#include "mozilla/PermissionManager.h"
#include "mozilla/Result.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ClientHandle.h"
#include "mozilla/dom/ClientManager.h"
#include "mozilla/dom/ClientSource.h"
#include "mozilla/dom/ConsoleUtils.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ErrorEvent.h"
#include "mozilla/dom/Headers.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/NotificationEvent.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/Request.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/SharedWorker.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/glean/DomServiceworkersMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsIClearDataService.h"
#include "nsICookieJarSettings.h"
#include "nsIDUtils.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIMutableArray.h"
#include "nsINamed.h"
#include "nsINetworkInterceptController.h"
#include "nsIPermissionManager.h"
#include "nsIPrincipal.h"
#include "nsIPushService.h"
#include "nsITimer.h"
#include "nsIUploadChannel2.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsXULAppAPI.h"

#ifdef PostMessage
#  undef PostMessage
#endif

mozilla::LazyLogModule sWorkerTelemetryLog("WorkerTelemetry");

#ifdef LOG
#  undef LOG
#endif
#define LOG(_args) MOZ_LOG(sWorkerTelemetryLog, LogLevel::Debug, _args);

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::ipc;

namespace mozilla::dom {

static_assert(
    nsIHttpChannelInternal::REDIRECT_MODE_FOLLOW ==
        static_cast<uint32_t>(RequestRedirect::Follow),
    "RequestRedirect enumeration value should make Necko Redirect mode value.");
static_assert(
    nsIHttpChannelInternal::REDIRECT_MODE_ERROR ==
        static_cast<uint32_t>(RequestRedirect::Error),
    "RequestRedirect enumeration value should make Necko Redirect mode value.");
static_assert(
    nsIHttpChannelInternal::REDIRECT_MODE_MANUAL ==
        static_cast<uint32_t>(RequestRedirect::Manual),
    "RequestRedirect enumeration value should make Necko Redirect mode value.");
static_assert(
    3 == ContiguousEnumSize<RequestRedirect>::value,
    "RequestRedirect enumeration value should make Necko Redirect mode value.");

static_assert(
    nsIHttpChannelInternal::FETCH_CACHE_MODE_DEFAULT ==
        static_cast<uint32_t>(RequestCache::Default),
    "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(
    nsIHttpChannelInternal::FETCH_CACHE_MODE_NO_STORE ==
        static_cast<uint32_t>(RequestCache::No_store),
    "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(
    nsIHttpChannelInternal::FETCH_CACHE_MODE_RELOAD ==
        static_cast<uint32_t>(RequestCache::Reload),
    "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(
    nsIHttpChannelInternal::FETCH_CACHE_MODE_NO_CACHE ==
        static_cast<uint32_t>(RequestCache::No_cache),
    "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(
    nsIHttpChannelInternal::FETCH_CACHE_MODE_FORCE_CACHE ==
        static_cast<uint32_t>(RequestCache::Force_cache),
    "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(
    nsIHttpChannelInternal::FETCH_CACHE_MODE_ONLY_IF_CACHED ==
        static_cast<uint32_t>(RequestCache::Only_if_cached),
    "RequestCache enumeration value should match Necko Cache mode value.");
static_assert(
    6 == ContiguousEnumSize<RequestCache>::value,
    "RequestCache enumeration value should match Necko Cache mode value.");

static_assert(static_cast<uint16_t>(ServiceWorkerUpdateViaCache::Imports) ==
                  nsIServiceWorkerRegistrationInfo::UPDATE_VIA_CACHE_IMPORTS,
              "nsIServiceWorkerRegistrationInfo::UPDATE_VIA_CACHE_*"
              " should match ServiceWorkerUpdateViaCache enumeration.");
static_assert(static_cast<uint16_t>(ServiceWorkerUpdateViaCache::All) ==
                  nsIServiceWorkerRegistrationInfo::UPDATE_VIA_CACHE_ALL,
              "nsIServiceWorkerRegistrationInfo::UPDATE_VIA_CACHE_*"
              " should match ServiceWorkerUpdateViaCache enumeration.");
static_assert(static_cast<uint16_t>(ServiceWorkerUpdateViaCache::None) ==
                  nsIServiceWorkerRegistrationInfo::UPDATE_VIA_CACHE_NONE,
              "nsIServiceWorkerRegistrationInfo::UPDATE_VIA_CACHE_*"
              " should match ServiceWorkerUpdateViaCache enumeration.");

static StaticRefPtr<ServiceWorkerManager> gInstance;

namespace {

nsresult PopulateRegistrationData(
    nsIPrincipal* aPrincipal,
    const ServiceWorkerRegistrationInfo* aRegistration,
    ServiceWorkerRegistrationData& aData) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aRegistration);

  if (NS_WARN_IF(!BasePrincipal::Cast(aPrincipal)->IsContentPrincipal())) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = PrincipalToPrincipalInfo(aPrincipal, &aData.principal());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  aData.scope() = aRegistration->Scope();
  aData.type() = aRegistration->Type();

  // TODO: When bug 1426401 is implemented we will need to handle more
  //       than just the active worker here.
  RefPtr<ServiceWorkerInfo> active = aRegistration->GetActive();
  MOZ_ASSERT(active);
  if (NS_WARN_IF(!active)) {
    return NS_ERROR_FAILURE;
  }

  aData.currentWorkerURL() = active->ScriptSpec();
  aData.cacheName() = active->CacheName();
  aData.currentWorkerHandlesFetch() = active->HandlesFetch();

  aData.currentWorkerInstalledTime() = active->GetInstalledTime();
  aData.currentWorkerActivatedTime() = active->GetActivatedTime();

  aData.updateViaCache() =
      static_cast<uint32_t>(aRegistration->GetUpdateViaCache());

  aData.lastUpdateTime() = aRegistration->GetLastUpdateTime();

  aData.navigationPreloadState() = aRegistration->GetNavigationPreloadState();

  aData.numberOfAttemptedActivations() =
      aRegistration->GetNumberOfAttemptedActivations();

  aData.isBroken() = aRegistration->IsBroken();

  aData.cacheAPIId() = aRegistration->GetCacheAPIId();

  MOZ_ASSERT(ServiceWorkerRegistrationDataIsValid(aData));

  return NS_OK;
}

class TeardownRunnable final : public Runnable {
 public:
  explicit TeardownRunnable(ServiceWorkerManagerChild* aActor)
      : Runnable("dom::ServiceWorkerManager::TeardownRunnable"),
        mActor(aActor) {
    MOZ_ASSERT(mActor);
  }

  NS_IMETHOD Run() override {
    MOZ_ASSERT(mActor);
    PServiceWorkerManagerChild::Send__delete__(mActor);
    return NS_OK;
  }

 private:
  ~TeardownRunnable() = default;

  RefPtr<ServiceWorkerManagerChild> mActor;
};

constexpr char kFinishShutdownTopic[] = "profile-before-change-qm";
constexpr char kPrivateBrowsingExited[] = "last-pb-context-exited";

constexpr auto kPrivateBrowsingOriginPattern =
    u"{ \"privateBrowsingId\": 1 }"_ns;

// Ref-counted barrier that calls nsIPBMCleanupCallback::Complete() when all
// references are released (i.e. when the caller and all async unregister jobs
// are done). Passed as the nsIServiceWorkerUnregisterCallback to each
// ForceUnregister call; each Unregister job holds a ref and calls back when
// finished, releasing its ref. The destructor fires Complete() once the last
// ref (from the caller or the last job) is released.
class PBMUnregisterBarrier final : public nsIServiceWorkerUnregisterCallback {
 public:
  NS_DECL_ISUPPORTS

  explicit PBMUnregisterBarrier(nsIPBMCleanupCallback* aCb) : mCallback(aCb) {}

  NS_IMETHOD UnregisterSucceeded(bool) override { return NS_OK; }
  NS_IMETHOD UnregisterFailed() override {
    mStatus = NS_ERROR_FAILURE;
    return NS_OK;
  }

 private:
  ~PBMUnregisterBarrier() {
    if (mCallback) {
      mCallback->Complete(mStatus);
    }
  }

  nsCOMPtr<nsIPBMCleanupCallback> mCallback;
  nsresult mStatus = NS_OK;
};

NS_IMPL_ISUPPORTS(PBMUnregisterBarrier, nsIServiceWorkerUnregisterCallback)

already_AddRefed<nsIAsyncShutdownClient> GetAsyncShutdownBarrier() {
  AssertIsOnMainThread();

  nsCOMPtr<nsIAsyncShutdownService> svc = services::GetAsyncShutdownService();
  MOZ_ASSERT(svc);

  nsCOMPtr<nsIAsyncShutdownClient> barrier;
  DebugOnly<nsresult> rv =
      svc->GetProfileChangeTeardown(getter_AddRefs(barrier));
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  return barrier.forget();
}

Result<nsCOMPtr<nsIPrincipal>, nsresult> ScopeToPrincipal(
    nsIURI* aScopeURI, const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(aScopeURI);

  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(aScopeURI, aOriginAttributes);
  if (NS_WARN_IF(!principal)) {
    return Err(NS_ERROR_FAILURE);
  }

  return principal;
}

Result<nsCOMPtr<nsIPrincipal>, nsresult> ScopeToPrincipal(
    const nsACString& aScope, const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(nsContentUtils::IsAbsoluteURL(aScope));

  nsCOMPtr<nsIURI> scopeURI;
  MOZ_TRY(NS_NewURI(getter_AddRefs(scopeURI), aScope));

  return ScopeToPrincipal(scopeURI, aOriginAttributes);
}

}  // namespace

struct ServiceWorkerManager::RegistrationDataPerPrincipal final {
  // Implements a container of keys for the "scope to registration map":
  // https://w3c.github.io/ServiceWorker/#dfn-scope-to-registration-map
  //
  // where each key is an absolute URL.
  //
  // The properties of this map that the spec uses are
  // 1) insertion,
  // 2) removal,
  // 3) iteration of scopes in FIFO order (excluding removed scopes),
  // 4) and finding, for a given path, the maximal length scope which is a
  //    prefix of the path.
  //
  // Additionally, because this is a container of keys for a map, there
  // shouldn't be duplicate scopes.
  //
  // The current implementation uses a dynamic array as the underlying
  // container, which is not optimal for unbounded container sizes (all
  // supported operations are in linear time) but may be superior for small
  // container sizes.
  //
  // If this is proven to be too slow, the underlying storage should be replaced
  // with a linked list of scopes in combination with an ordered map that maps
  // scopes to linked list elements/iterators. This would reduce all of the
  // above operations besides iteration (necessarily linear) to logarithmic
  // time.
  class ScopeContainer final : private nsTArray<nsCString> {
    using Base = nsTArray<nsCString>;

   public:
    using Base::Contains;
    using Base::IsEmpty;
    using Base::Length;

    // No using-declaration to avoid importing the non-const overload.
    decltype(auto) operator[](Base::index_type aIndex) const {
      return Base::operator[](aIndex);
    }

    void InsertScope(const nsACString& aScope) {
      MOZ_DIAGNOSTIC_ASSERT(nsContentUtils::IsAbsoluteURL(aScope));

      if (Contains(aScope)) {
        return;
      }

      AppendElement(aScope);
    }

    void RemoveScope(const nsACString& aScope) {
      MOZ_ALWAYS_TRUE(RemoveElement(aScope));
    }

    // Implements most of "Match Service Worker Registration":
    // https://w3c.github.io/ServiceWorker/#scope-match-algorithm
    Maybe<nsCString> MatchScope(const nsACString& aClientUrl) const {
      Maybe<nsCString> match;

      for (const nsCString& scope : *this) {
        if (StringBeginsWith(aClientUrl, scope)) {
          if (!match || scope.Length() > match->Length()) {
            match = Some(scope);
          }
        }
      }

      // Step 7.2:
      // "Assert: matchingScope’s origin and clientURL’s origin are same
      // origin."
      MOZ_DIAGNOSTIC_ASSERT_IF(match, IsSameOrigin(*match, aClientUrl));

      return match;
    }

   private:
    bool IsSameOrigin(const nsACString& aMatchingScope,
                      const nsACString& aClientUrl) const {
      auto parseResult = ScopeToPrincipal(aMatchingScope, OriginAttributes());

      if (NS_WARN_IF(parseResult.isErr())) {
        return false;
      }

      auto scopePrincipal = parseResult.unwrap();

      parseResult = ScopeToPrincipal(aClientUrl, OriginAttributes());

      if (NS_WARN_IF(parseResult.isErr())) {
        return false;
      }

      auto clientPrincipal = parseResult.unwrap();

      bool equals = false;

      if (NS_WARN_IF(
              NS_FAILED(scopePrincipal->Equals(clientPrincipal, &equals)))) {
        return false;
      }

      return equals;
    }
  };

  ScopeContainer mScopeContainer;

  // Scope to registration.
  // The scope should be a fully qualified valid URL.
  nsRefPtrHashtable<nsCStringHashKey, ServiceWorkerRegistrationInfo> mInfos;

  // Maps scopes to job queues.
  nsRefPtrHashtable<nsCStringHashKey, ServiceWorkerJobQueue> mJobQueues;

  // Map scopes to scheduled update timers.
  nsInterfaceHashtable<nsCStringHashKey, nsITimer> mUpdateTimers;

  // The number of times we have done a quota usage check for this origin for
  // mitigation purposes.  See the docs on nsIServiceWorkerRegistrationInfo,
  // where this value is exposed.
  int32_t mQuotaUsageCheckCount = 0;
};

//////////////////////////
// ServiceWorkerManager //
//////////////////////////

NS_IMPL_ADDREF(ServiceWorkerManager)
NS_IMPL_RELEASE(ServiceWorkerManager)

NS_INTERFACE_MAP_BEGIN(ServiceWorkerManager)
  NS_INTERFACE_MAP_ENTRY(nsIServiceWorkerManager)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIServiceWorkerManager)
NS_INTERFACE_MAP_END

ServiceWorkerManager::ServiceWorkerManager()
    : mActor(nullptr), mShuttingDown(false) {}

ServiceWorkerManager::~ServiceWorkerManager() {
  // The map will assert if it is not empty when destroyed.
  mRegistrationInfos.Clear();

  // This can happen if the browser is started up in ProfileManager mode, in
  // which case XPCOM will startup and shutdown, but there won't be any
  // profile-* topic notifications. The shutdown blocker expects to be in a
  // NotAcceptingPromises state when it's destroyed, and this transition
  // normally happens in the "profile-change-teardown" notification callback
  // (which won't be called in ProfileManager mode).
  if (!mShuttingDown && mShutdownBlocker) {
    mShutdownBlocker->StopAcceptingPromises();
  }
}

void ServiceWorkerManager::BlockShutdownOn(GenericNonExclusivePromise* aPromise,
                                           uint32_t aShutdownStateId) {
  AssertIsOnMainThread();

  MOZ_ASSERT(mShutdownBlocker);
  MOZ_ASSERT(aPromise);

  mShutdownBlocker->WaitOnPromise(aPromise, aShutdownStateId);
}

void ServiceWorkerManager::Init(ServiceWorkerRegistrar* aRegistrar) {
  // ServiceWorkers now only support parent intercept.  In parent intercept
  // mode, only the parent process ServiceWorkerManager has any state or does
  // anything.
  //
  // It is our goal to completely eliminate support for content process
  // ServiceWorkerManager instances and make getting a SWM instance trigger a
  // fatal assertion.  But until we've reached that point, we make
  // initialization a no-op so that content process ServiceWorkerManager
  // instances will simply have no state and no registrations.
  if (!XRE_IsParentProcess()) {
    return;
  }

  nsCOMPtr<nsIAsyncShutdownClient> shutdownBarrier = GetAsyncShutdownBarrier();

  if (shutdownBarrier) {
    mShutdownBlocker = ServiceWorkerShutdownBlocker::CreateAndRegisterOn(
        *shutdownBarrier, *this);
    MOZ_ASSERT(mShutdownBlocker);
  }

  // This observer notification will be removed by
  // ServiceWorkerManager::MaybeFinishShutdown which currently is triggered by
  // receiving a "profile-before-change-qm" observer notification.  That
  // observer is added by our shutdown blocker which currently fires during the
  // "profile-change-teardown" phase.
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(this, kPrivateBrowsingExited, false);
  }

  MOZ_DIAGNOSTIC_ASSERT(aRegistrar);

  PBackgroundChild* actorChild = BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actorChild)) {
    MaybeStartShutdown();
    return;
  }

  PServiceWorkerManagerChild* actor =
      actorChild->SendPServiceWorkerManagerConstructor();
  if (!actor) {
    MaybeStartShutdown();
    return;
  }

  mActor = static_cast<ServiceWorkerManagerChild*>(actor);

  // mActor must be set before LoadRegistrations is called because it can purge
  // service workers if preferences are disabled.
  nsTArray<ServiceWorkerRegistrationData> data;
  aRegistrar->GetRegistrations(data);
  LoadRegistrations(data);

  mTelemetryLastChange = TimeStamp::Now();

  mETPPermissionObserver = new ETPPermissionObserver();
}

void ServiceWorkerManager::RecordTelemetry(uint32_t aNumber, uint32_t aFetch) {
  // Submit N value pairs to Telemetry for the time we were at those values
  auto now = TimeStamp::Now();
  // round down, with a minimum of 1 repeat.  In theory this gives
  // inaccuracy if there are frequent changes, but that's uncommon.
  uint32_t repeats = (uint32_t)((now - mTelemetryLastChange).ToMilliseconds()) /
                     mTelemetryPeriodMs;
  mTelemetryLastChange = now;
  if (repeats == 0) {
    repeats = 1;
  }
  nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
      "ServiceWorkerTelemetryRunnable", [aNumber, aFetch, repeats]() {
        LOG(("ServiceWorkers running: %u samples of %u/%u", repeats, aNumber,
             aFetch));
        // Don't allocate infinitely huge arrays if someone visits a SW site
        // after a few months running. 1 month is about 500K repeats @ 5s
        // sampling
        uint32_t num_repeats = std::min(repeats, 1000000U);  // 4MB max
        nsTArray<uint64_t> values;

        uint64_t* array = values.AppendElements(num_repeats);
        for (uint32_t i = 0; i < num_repeats; i++) {
          array[i] = aNumber;
        }
        glean::service_worker::running
            .EnumGet(glean::service_worker::RunningLabel::eAll)
            .AccumulateSamples(values);

        for (uint32_t i = 0; i < num_repeats; i++) {
          array[i] = aFetch;
        }
        glean::service_worker::running
            .EnumGet(glean::service_worker::RunningLabel::eFetch)
            .AccumulateSamples(values);
      });
  NS_DispatchBackgroundTask(runnable.forget(), nsIEventTarget::DISPATCH_NORMAL);
}

RefPtr<GenericErrorResultPromise> ServiceWorkerManager::StartControllingClient(
    const ClientInfo& aClientInfo,
    ServiceWorkerRegistrationInfo* aRegistrationInfo,
    bool aControlClientHandle) {
  MOZ_DIAGNOSTIC_ASSERT(aRegistrationInfo->GetActive());

  // XXX We can't use a generic lambda (accepting auto&& entry) like elsewhere
  // with WithEntryHandle, since we get linker errors then using clang+lld. This
  // might be a toolchain issue?
  return mControlledClients.WithEntryHandle(
      aClientInfo.Id(),
      [&](decltype(mControlledClients)::EntryHandle&& entry)
          -> RefPtr<GenericErrorResultPromise> {
        const RefPtr<ServiceWorkerManager> self = this;

        const ServiceWorkerDescriptor& active =
            aRegistrationInfo->GetActive()->Descriptor();

        if (entry) {
          const RefPtr<ServiceWorkerRegistrationInfo> old =
              std::move(entry.Data()->mRegistrationInfo);

          const RefPtr<GenericErrorResultPromise> promise =
              aControlClientHandle
                  ? entry.Data()->mClientHandle->Control(active)
                  : GenericErrorResultPromise::CreateAndResolve(false,
                                                                __func__);

          entry.Data()->mRegistrationInfo = aRegistrationInfo;

          if (old != aRegistrationInfo) {
            StopControllingRegistration(old);
            aRegistrationInfo->StartControllingClient();
          }

          // Always check to see if we failed to actually control the client. In
          // that case remove the client from our list of controlled clients.
          return promise->Then(
              GetMainThreadSerialEventTarget(), __func__,
              [](bool) {
                // do nothing on success
                return GenericErrorResultPromise::CreateAndResolve(true,
                                                                   __func__);
              },
              [self, aClientInfo](const CopyableErrorResult& aRv) {
                // failed to control, forget about this client
                self->StopControllingClient(aClientInfo);
                return GenericErrorResultPromise::CreateAndReject(aRv,
                                                                  __func__);
              });
        }

        RefPtr<ClientHandle> clientHandle = ClientManager::CreateHandle(
            aClientInfo, GetMainThreadSerialEventTarget());

        const RefPtr<GenericErrorResultPromise> promise =
            aControlClientHandle
                ? clientHandle->Control(active)
                : GenericErrorResultPromise::CreateAndResolve(false, __func__);

        aRegistrationInfo->StartControllingClient();

        entry.Insert(
            MakeUnique<ControlledClientData>(clientHandle, aRegistrationInfo));

        clientHandle->OnDetach()->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [self, aClientInfo] { self->StopControllingClient(aClientInfo); });

        // Always check to see if we failed to actually control the client.  In
        // that case removed the client from our list of controlled clients.
        return promise->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [](bool) {
              // do nothing on success
              return GenericErrorResultPromise::CreateAndResolve(true,
                                                                 __func__);
            },
            [self, aClientInfo](const CopyableErrorResult& aRv) {
              // failed to control, forget about this client
              self->StopControllingClient(aClientInfo);
              return GenericErrorResultPromise::CreateAndReject(aRv, __func__);
            });
      });
}

void ServiceWorkerManager::StopControllingClient(
    const ClientInfo& aClientInfo) {
  auto entry = mControlledClients.Lookup(aClientInfo.Id());
  if (!entry) {
    return;
  }

  RefPtr<ServiceWorkerRegistrationInfo> reg =
      std::move(entry.Data()->mRegistrationInfo);

  entry.Remove();

  StopControllingRegistration(reg);
}

void ServiceWorkerManager::MaybeStartShutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mShuttingDown) {
    return;
  }

  mShuttingDown = true;

  for (const auto& dataPtr : mRegistrationInfos.Values()) {
    for (const auto& timerEntry : dataPtr->mUpdateTimers.Values()) {
      timerEntry->Cancel();
    }
    dataPtr->mUpdateTimers.Clear();

    for (const auto& queueEntry : dataPtr->mJobQueues.Values()) {
      queueEntry->CancelAll();
    }
    dataPtr->mJobQueues.Clear();

    for (const auto& registrationEntry : dataPtr->mInfos.Values()) {
      registrationEntry->ShutdownWorkers();
    }

    // ServiceWorkerCleanup may try to unregister registrations, so don't clear
    // mInfos.
  }

  for (const auto& entry : mControlledClients.Values()) {
    entry->mRegistrationInfo->ShutdownWorkers();
  }

  for (auto iter = mOrphanedRegistrations.iter(); !iter.done(); iter.next()) {
    iter.get()->ShutdownWorkers();
  }

  if (mShutdownBlocker) {
    mShutdownBlocker->StopAcceptingPromises();
  }

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(this, kFinishShutdownTopic, false);
    return;
  }

  MaybeFinishShutdown();
}

void ServiceWorkerManager::MaybeFinishShutdown() {
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->RemoveObserver(this, kFinishShutdownTopic);
    obs->RemoveObserver(this, kPrivateBrowsingExited);
  }

  if (!mActor) {
    return;
  }

  mActor->ManagerShuttingDown();

  RefPtr<TeardownRunnable> runnable = new TeardownRunnable(mActor);
  nsresult rv = NS_DispatchToMainThread(runnable);
  (void)NS_WARN_IF(NS_FAILED(rv));
  mActor = nullptr;
  mETPPermissionObserver = nullptr;

  // This also submits final telemetry
  ServiceWorkerPrivate::RunningShutdown();
}

class ServiceWorkerResolveWindowPromiseOnRegisterCallback final
    : public ServiceWorkerJob::Callback {
 public:
  NS_INLINE_DECL_REFCOUNTING(
      ServiceWorkerResolveWindowPromiseOnRegisterCallback, override)

  virtual void JobFinished(ServiceWorkerJob* aJob,
                           ErrorResult& aStatus) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aJob);

    if (aStatus.Failed()) {
      mPromiseHolder.Reject(CopyableErrorResult(aStatus), __func__);
      return;
    }

    MOZ_ASSERT(aJob->GetType() == ServiceWorkerJob::Type::Register);
    RefPtr<ServiceWorkerRegisterJob> registerJob =
        static_cast<ServiceWorkerRegisterJob*>(aJob);
    RefPtr<ServiceWorkerRegistrationInfo> reg = registerJob->GetRegistration();

    mPromiseHolder.Resolve(reg->Descriptor(), __func__);
  }

  virtual void JobDiscarded(ErrorResult& aStatus) override {
    MOZ_ASSERT(NS_IsMainThread());

    mPromiseHolder.Reject(CopyableErrorResult(aStatus), __func__);
  }

  RefPtr<ServiceWorkerRegistrationPromise> Promise() {
    MOZ_ASSERT(NS_IsMainThread());
    return mPromiseHolder.Ensure(__func__);
  }

 private:
  ~ServiceWorkerResolveWindowPromiseOnRegisterCallback() = default;

  MozPromiseHolder<ServiceWorkerRegistrationPromise> mPromiseHolder;
};

NS_IMETHODIMP
ServiceWorkerManager::RegisterForTest(nsIPrincipal* aPrincipal,
                                      const nsAString& aScopeURL,
                                      const nsAString& aScriptURL,
                                      JSContext* aCx,
                                      mozilla::dom::Promise** aPromise) {
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<Promise> outer = Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  if (!StaticPrefs::dom_serviceWorkers_testing_enabled()) {
    outer->MaybeRejectWithAbortError(
        "registerForTest only allowed when dom.serviceWorkers.testing.enabled "
        "is true");
    outer.forget(aPromise);
    return NS_OK;
  }

  if (aPrincipal == nullptr) {
    outer->MaybeRejectWithAbortError("Missing principal");
    outer.forget(aPromise);
    return NS_OK;
  }

  if (aScriptURL.IsEmpty()) {
    outer->MaybeRejectWithAbortError("Missing script url");
    outer.forget(aPromise);
    return NS_OK;
  }

  if (aScopeURL.IsEmpty()) {
    outer->MaybeRejectWithAbortError("Missing scope url");
    outer.forget(aPromise);
    return NS_OK;
  }

  // The ClientType isn't really used here, but ClientType::Window
  // is the least bad choice since this is happening on the main thread.
  Maybe<ClientInfo> clientInfo =
      dom::ClientManager::CreateInfo(ClientType::Window, aPrincipal);

  if (!clientInfo.isSome()) {
    outer->MaybeRejectWithUnknownError("Error creating clientInfo");
    outer.forget(aPromise);
    return NS_OK;
  }

  auto scope = NS_ConvertUTF16toUTF8(aScopeURL);
  auto scriptURL = NS_ConvertUTF16toUTF8(aScriptURL);

  auto regPromise =
      Register(clientInfo.ref(), scope, WorkerType::Classic, scriptURL,
               dom::ServiceWorkerUpdateViaCache::Imports);

  const RefPtr<ServiceWorkerManager> self(this);
  const nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  regPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, outer, principal,
       scope](const ServiceWorkerRegistrationDescriptor& regDesc) {
        RefPtr<ServiceWorkerRegistrationInfo> registration =
            self->GetRegistration(principal, NS_ConvertUTF16toUTF8(scope));
        if (registration) {
          outer->MaybeResolve(registration);
        } else {
          outer->MaybeRejectWithUnknownError(
              "Failed to retrieve ServiceWorkerRegistrationInfo");
        }
      },
      [outer](const mozilla::CopyableErrorResult& err) {
        CopyableErrorResult result(err);
        outer->MaybeReject(std::move(result));
      });

  outer.forget(aPromise);

  return NS_OK;
}

RefPtr<ServiceWorkerRegistrationPromise> ServiceWorkerManager::Register(
    const ClientInfo& aClientInfo, const nsACString& aScopeURL,
    const WorkerType& aType, const nsACString& aScriptURL,
    ServiceWorkerUpdateViaCache aUpdateViaCache) {
  AUTO_PROFILER_MARKER_UNTYPED("SWM Register", DOM, {});

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScopeURL);
  if (NS_FAILED(rv)) {
    // Odd, since it was serialiazed from an nsIURI.
    CopyableErrorResult err;
    err.ThrowInvalidStateError("Scope URL cannot be parsed");
    return ServiceWorkerRegistrationPromise::CreateAndReject(err, __func__);
  }

  nsCOMPtr<nsIURI> scriptURI;
  rv = NS_NewURI(getter_AddRefs(scriptURI), aScriptURL);
  if (NS_FAILED(rv)) {
    // Odd, since it was serialiazed from an nsIURI.
    CopyableErrorResult err;
    err.ThrowInvalidStateError("Script URL cannot be parsed");
    return ServiceWorkerRegistrationPromise::CreateAndReject(err, __func__);
  }

  IgnoredErrorResult err;
  ServiceWorkerScopeAndScriptAreValid(aClientInfo, scopeURI, scriptURI, err);
  if (err.Failed()) {
    return ServiceWorkerRegistrationPromise::CreateAndReject(
        CopyableErrorResult(std::move(err)), __func__);
  }

  // If the previous validation step passed then we must have a principal.
  auto principalOrErr = aClientInfo.GetPrincipal();

  if (NS_WARN_IF(principalOrErr.isErr())) {
    return ServiceWorkerRegistrationPromise::CreateAndReject(
        CopyableErrorResult(principalOrErr.unwrapErr()), __func__);
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  nsAutoCString scopeKey;
  rv = PrincipalToScopeKey(principal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return ServiceWorkerRegistrationPromise::CreateAndReject(
        CopyableErrorResult(rv), __func__);
  }

  RefPtr<ServiceWorkerJobQueue> queue =
      GetOrCreateJobQueue(scopeKey, aScopeURL);

  RefPtr<ServiceWorkerResolveWindowPromiseOnRegisterCallback> cb =
      new ServiceWorkerResolveWindowPromiseOnRegisterCallback();

  auto lifetime = DetermineLifetimeForClient(aClientInfo);

  uint16_t ipAddressSpace = 0;
  auto policyContainerArgs = aClientInfo.GetPolicyContainerArgs();
  if (policyContainerArgs.isSome()) {
    ipAddressSpace =
        static_cast<uint16_t>(policyContainerArgs->ipAddressSpace());
  }

  RefPtr<ServiceWorkerRegisterJob> job = new ServiceWorkerRegisterJob(
      principal, aScopeURL, aType, aScriptURL,
      static_cast<ServiceWorkerUpdateViaCache>(aUpdateViaCache), lifetime,
      ipAddressSpace);

  job->AppendResultCallback(cb);
  queue->ScheduleJob(job);

  MOZ_ASSERT(NS_IsMainThread());

  return cb->Promise();
}

/*
 * Implements the async aspects of the getRegistrations algorithm.
 */
class GetRegistrationsRunnable final : public Runnable {
  const ClientInfo mClientInfo;
  RefPtr<ServiceWorkerRegistrationListPromise::Private> mPromise;

 public:
  explicit GetRegistrationsRunnable(const ClientInfo& aClientInfo)
      : Runnable("dom::ServiceWorkerManager::GetRegistrationsRunnable"),
        mClientInfo(aClientInfo),
        mPromise(new ServiceWorkerRegistrationListPromise::Private(__func__)) {}

  RefPtr<ServiceWorkerRegistrationListPromise> Promise() const {
    return mPromise;
  }

  NS_IMETHOD
  Run() override {
    auto scopeExit = MakeScopeExit(
        [&] { mPromise->Reject(NS_ERROR_DOM_INVALID_STATE_ERR, __func__); });

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (!swm) {
      return NS_OK;
    }

    auto principalOrErr = mClientInfo.GetPrincipal();
    if (NS_WARN_IF(principalOrErr.isErr())) {
      return NS_OK;
    }

    nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

    nsTArray<ServiceWorkerRegistrationDescriptor> array;

    if (NS_WARN_IF(!BasePrincipal::Cast(principal)->IsContentPrincipal())) {
      return NS_OK;
    }

    nsAutoCString scopeKey;
    nsresult rv = swm->PrincipalToScopeKey(principal, scopeKey);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    ServiceWorkerManager::RegistrationDataPerPrincipal* data;
    if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
      scopeExit.release();
      mPromise->Resolve(array, __func__);
      return NS_OK;
    }

    for (uint32_t i = 0; i < data->mScopeContainer.Length(); ++i) {
      RefPtr<ServiceWorkerRegistrationInfo> info =
          data->mInfos.GetWeak(data->mScopeContainer[i]);

      NS_ConvertUTF8toUTF16 scope(data->mScopeContainer[i]);

      nsCOMPtr<nsIURI> scopeURI;
      nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), scope);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        break;
      }

      // Unfortunately we don't seem to have an obvious window id here; in
      // particular ClientInfo does not have one, and neither do service worker
      // registrations, as far as I can tell.
      rv = principal->CheckMayLoadWithReporting(
          scopeURI, false /* allowIfInheritsPrincipal */,
          0 /* innerWindowID */);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      array.AppendElement(info->Descriptor());
    }

    scopeExit.release();
    mPromise->Resolve(array, __func__);

    return NS_OK;
  }
};

RefPtr<ServiceWorkerRegistrationListPromise>
ServiceWorkerManager::GetRegistrations(const ClientInfo& aClientInfo) const {
  RefPtr<GetRegistrationsRunnable> runnable =
      new GetRegistrationsRunnable(aClientInfo);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(runnable));
  return runnable->Promise();
}

/*
 * Implements the async aspects of the getRegistration algorithm.
 */
class GetRegistrationRunnable final : public Runnable {
  const ClientInfo mClientInfo;
  RefPtr<ServiceWorkerRegistrationPromise::Private> mPromise;
  nsCString mURL;

 public:
  GetRegistrationRunnable(const ClientInfo& aClientInfo, const nsACString& aURL)
      : Runnable("dom::ServiceWorkerManager::GetRegistrationRunnable"),
        mClientInfo(aClientInfo),
        mPromise(new ServiceWorkerRegistrationPromise::Private(__func__)),
        mURL(aURL) {}

  RefPtr<ServiceWorkerRegistrationPromise> Promise() const { return mPromise; }

  NS_IMETHOD
  Run() override {
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (!swm) {
      mPromise->Reject(NS_ERROR_DOM_INVALID_STATE_ERR, __func__);
      return NS_OK;
    }

    auto principalOrErr = mClientInfo.GetPrincipal();
    if (NS_WARN_IF(principalOrErr.isErr())) {
      mPromise->Reject(NS_ERROR_DOM_INVALID_STATE_ERR, __func__);
      return NS_OK;
    }

    nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), mURL);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mPromise->Reject(rv, __func__);
      return NS_OK;
    }

    // Unfortunately we don't seem to have an obvious window id here; in
    // particular ClientInfo does not have one, and neither do service worker
    // registrations, as far as I can tell.
    rv = principal->CheckMayLoadWithReporting(
        uri, false /* allowIfInheritsPrincipal */, 0 /* innerWindowID */);
    if (NS_FAILED(rv)) {
      mPromise->Reject(NS_ERROR_DOM_SECURITY_ERR, __func__);
      return NS_OK;
    }

    RefPtr<ServiceWorkerRegistrationInfo> registration =
        swm->GetServiceWorkerRegistrationInfo(principal, uri);

    if (!registration) {
      // Reject with NS_OK means "not found".
      mPromise->Reject(NS_OK, __func__);
      return NS_OK;
    }

    mPromise->Resolve(registration->Descriptor(), __func__);

    return NS_OK;
  }
};

RefPtr<ServiceWorkerRegistrationPromise> ServiceWorkerManager::GetRegistration(
    const ClientInfo& aClientInfo, const nsACString& aURL) const {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<GetRegistrationRunnable> runnable =
      new GetRegistrationRunnable(aClientInfo, aURL);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(runnable));

  return runnable->Promise();
}

NS_IMETHODIMP
ServiceWorkerManager::SendPushEvent(const nsACString& aOriginAttributes,
                                    const nsACString& aScope,
                                    const nsTArray<uint8_t>& aDataBytes,
                                    uint8_t optional_argc) {
  if (optional_argc == 1) {
    // This does one copy here (while constructing the Maybe) and another when
    // we end up copying into the SendPushEventRunnable.  We could fix that to
    // only do one copy by making things between here and there take
    // Maybe<nsTArray<uint8_t>>&&, but then we'd need to copy before we know
    // whether we really need to in PushMessageDispatcher::NotifyWorkers.  Since
    // in practice this only affects JS callers that pass data, and we don't
    // have any right now, let's not worry about it.
    return SendPushEvent(aOriginAttributes, aScope, u""_ns,
                         Some(aDataBytes.Clone()));
  }
  MOZ_ASSERT(optional_argc == 0);
  return SendPushEvent(aOriginAttributes, aScope, u""_ns, Nothing());
}

nsresult ServiceWorkerManager::SendCookieChangeEvent(
    const OriginAttributes& aOriginAttributes, const nsACString& aScope,
    const net::CookieStruct& aCookie, bool aCookieDeleted) {
  nsCOMPtr<nsIPrincipal> principal =
      MOZ_TRY(ScopeToPrincipal(aScope, aOriginAttributes));

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetRegistration(principal, aScope);
  if (NS_WARN_IF(!registration)) {
    return NS_ERROR_FAILURE;
  }

  MOZ_DIAGNOSTIC_ASSERT(registration->Scope().Equals(aScope));

  ServiceWorkerInfo* serviceWorker = registration->GetActive();
  if (NS_WARN_IF(!serviceWorker)) {
    return NS_ERROR_FAILURE;
  }

  return serviceWorker->WorkerPrivate()->SendCookieChangeEvent(
      aCookie, aCookieDeleted, registration);
}

nsresult ServiceWorkerManager::SendPushEvent(
    const nsACString& aOriginAttributes, const nsACString& aScope,
    const nsAString& aMessageId, const Maybe<nsTArray<uint8_t>>& aData) {
  OriginAttributes attrs;
  if (!attrs.PopulateFromSuffix(aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIPrincipal> principal = MOZ_TRY(ScopeToPrincipal(aScope, attrs));

  // The registration handling a push notification must have an exact scope
  // match. This will try to find an exact match, unlike how fetch may find the
  // registration with the longest scope that's a prefix of the fetched URL.
  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetRegistration(principal, aScope);
  if (NS_WARN_IF(!registration)) {
    return NS_ERROR_FAILURE;
  }

  MOZ_DIAGNOSTIC_ASSERT(registration->Scope().Equals(aScope));

  ServiceWorkerInfo* serviceWorker = registration->GetActive();
  if (NS_WARN_IF(!serviceWorker)) {
    return NS_ERROR_FAILURE;
  }

  return serviceWorker->WorkerPrivate()->SendPushEvent(aMessageId, aData,
                                                       registration);
}

NS_IMETHODIMP
ServiceWorkerManager::SendPushSubscriptionChangeEvent(
    const nsACString& aOriginAttributes, const nsACString& aScope,
    nsIPushSubscription* aOldSubscription) {
  OriginAttributes attrs;
  if (!attrs.PopulateFromSuffix(aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  ServiceWorkerInfo* info = GetActiveWorkerInfoForScope(attrs, aScope);
  if (!info) {
    return NS_ERROR_FAILURE;
  }
  return info->WorkerPrivate()->SendPushSubscriptionChangeEvent(
      aOldSubscription);
}

NS_IMETHODIMP
ServiceWorkerManager::SendNotificationClickEvent(
    const nsACString& aOriginSuffix, const nsAString& aScope,
    const IPCNotification& aNotification, const nsAString& aAction) {
  OriginAttributes attrs;
  if (!attrs.PopulateFromSuffix(aOriginSuffix)) {
    return NS_ERROR_INVALID_ARG;
  }

  ServiceWorkerInfo* info =
      GetActiveWorkerInfoForScope(attrs, NS_ConvertUTF16toUTF8(aScope));
  if (!info) {
    return NS_ERROR_FAILURE;
  }

  ServiceWorkerPrivate* workerPrivate = info->WorkerPrivate();

  return workerPrivate->SendNotificationClickEvent(aNotification, aAction);
}

NS_IMETHODIMP
ServiceWorkerManager::SendNotificationCloseEvent(
    const nsACString& aOriginSuffix, const nsAString& aScope,
    const IPCNotification& aNotification) {
  OriginAttributes attrs;
  if (!attrs.PopulateFromSuffix(aOriginSuffix)) {
    return NS_ERROR_INVALID_ARG;
  }

  ServiceWorkerInfo* info =
      GetActiveWorkerInfoForScope(attrs, NS_ConvertUTF16toUTF8(aScope));
  if (!info) {
    return NS_ERROR_FAILURE;
  }

  ServiceWorkerPrivate* workerPrivate = info->WorkerPrivate();

  return workerPrivate->SendNotificationCloseEvent(aNotification);
}

RefPtr<ServiceWorkerRegistrationPromise> ServiceWorkerManager::WhenReady(
    const ClientInfo& aClientInfo) {
  AssertIsOnMainThread();

  for (auto& prd : mPendingReadyList) {
    if (prd->mClientHandle->Info().Id() == aClientInfo.Id() &&
        prd->mClientHandle->Info().PrincipalInfo() ==
            aClientInfo.PrincipalInfo()) {
      return prd->mPromise;
    }
  }

  RefPtr<ServiceWorkerRegistrationInfo> reg =
      GetServiceWorkerRegistrationInfo(aClientInfo);
  if (reg && reg->GetActive()) {
    return ServiceWorkerRegistrationPromise::CreateAndResolve(reg->Descriptor(),
                                                              __func__);
  }

  nsCOMPtr<nsISerialEventTarget> target = GetMainThreadSerialEventTarget();

  RefPtr<ClientHandle> handle =
      ClientManager::CreateHandle(aClientInfo, target);
  mPendingReadyList.AppendElement(MakeUnique<PendingReadyData>(handle));

  RefPtr<ServiceWorkerManager> self(this);
  handle->OnDetach()->Then(target, __func__,
                           [self = std::move(self), aClientInfo] {
                             self->RemovePendingReadyPromise(aClientInfo);
                           });

  return mPendingReadyList.LastElement()->mPromise;
}

void ServiceWorkerManager::CheckPendingReadyPromises() {
  nsTArray<UniquePtr<PendingReadyData>> pendingReadyList =
      std::move(mPendingReadyList);
  for (uint32_t i = 0; i < pendingReadyList.Length(); ++i) {
    UniquePtr<PendingReadyData> prd(std::move(pendingReadyList[i]));

    RefPtr<ServiceWorkerRegistrationInfo> reg =
        GetServiceWorkerRegistrationInfo(prd->mClientHandle->Info());

    if (reg && reg->GetActive()) {
      prd->mPromise->Resolve(reg->Descriptor(), __func__);
    } else {
      mPendingReadyList.AppendElement(std::move(prd));
    }
  }
}

void ServiceWorkerManager::RemovePendingReadyPromise(
    const ClientInfo& aClientInfo) {
  nsTArray<UniquePtr<PendingReadyData>> pendingReadyList =
      std::move(mPendingReadyList);
  for (uint32_t i = 0; i < pendingReadyList.Length(); ++i) {
    UniquePtr<PendingReadyData> prd(std::move(pendingReadyList[i]));

    if (prd->mClientHandle->Info().Id() == aClientInfo.Id() &&
        prd->mClientHandle->Info().PrincipalInfo() ==
            aClientInfo.PrincipalInfo()) {
      prd->mPromise->Reject(NS_ERROR_DOM_ABORT_ERR, __func__);
    } else {
      mPendingReadyList.AppendElement(std::move(prd));
    }
  }
}

void ServiceWorkerManager::NoteInheritedController(
    const ClientInfo& aClientInfo, const ServiceWorkerDescriptor& aController) {
  MOZ_ASSERT(NS_IsMainThread());

  auto principalOrErr = PrincipalInfoToPrincipal(aController.PrincipalInfo());

  if (NS_WARN_IF(principalOrErr.isErr())) {
    return;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  nsCOMPtr<nsIURI> scope;
  nsresult rv = NS_NewURI(getter_AddRefs(scope), aController.Scope());
  NS_ENSURE_SUCCESS_VOID(rv);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetServiceWorkerRegistrationInfo(principal, scope);
  NS_ENSURE_TRUE_VOID(registration);
  NS_ENSURE_TRUE_VOID(registration->GetActive());

  StartControllingClient(aClientInfo, registration,
                         false /* aControlClientHandle */);
}

ServiceWorkerInfo* ServiceWorkerManager::GetActiveWorkerInfoForScope(
    const OriginAttributes& aOriginAttributes, const nsACString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScope);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  auto result = ScopeToPrincipal(scopeURI, aOriginAttributes);
  if (NS_WARN_IF(result.isErr())) {
    return nullptr;
  }

  auto principal = result.unwrap();

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetServiceWorkerRegistrationInfo(principal, scopeURI);
  if (!registration) {
    return nullptr;
  }

  return registration->GetActive();
}

ServiceWorkerInfo* ServiceWorkerManager::GetServiceWorkerByClientInfo(
    const ClientInfo& aClientInfo) const {
  MOZ_ASSERT(NS_IsMainThread());

  auto principalOrErr = aClientInfo.GetPrincipal();
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return nullptr;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(principal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return nullptr;
  }

  // If the ClientInfo has CSP data populated, we need to normalize that off, so
  // make a copy and only propagate the non-CSP fields.
  ClientInfo normalized = ClientInfo(
      aClientInfo.Id(), aClientInfo.AgentClusterId(), aClientInfo.Type(),
      aClientInfo.PrincipalInfo(), aClientInfo.CreationTime(),
      aClientInfo.URL(), aClientInfo.FrameType());

  for (const auto& registration : data->mInfos.Values()) {
    ServiceWorkerInfo* info = registration->GetByClientInfo(normalized);
    if (info) {
      return info;
    }
  }

  return nullptr;
}

ServiceWorkerInfo* ServiceWorkerManager::GetServiceWorkerByDescriptor(
    const ServiceWorkerDescriptor& aServiceWorker) const {
  auto principalOrErr = aServiceWorker.GetPrincipal();
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return nullptr;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetRegistration(principal, aServiceWorker.Scope());
  if (NS_WARN_IF(!registration)) {
    return nullptr;
  }

  return registration->GetByDescriptor(aServiceWorker);
}

namespace {

class UnregisterJobCallback final : public ServiceWorkerJob::Callback {
  nsCOMPtr<nsIServiceWorkerUnregisterCallback> mCallback;

  ~UnregisterJobCallback() { MOZ_ASSERT(!mCallback); }

 public:
  explicit UnregisterJobCallback(nsIServiceWorkerUnregisterCallback* aCallback)
      : mCallback(aCallback) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mCallback);
  }

  void JobFinished(ServiceWorkerJob* aJob, ErrorResult& aStatus) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aJob);
    MOZ_ASSERT(mCallback);

    auto scopeExit = MakeScopeExit([&]() { mCallback = nullptr; });

    if (aStatus.Failed()) {
      mCallback->UnregisterFailed();
      return;
    }

    MOZ_ASSERT(aJob->GetType() == ServiceWorkerJob::Type::Unregister);
    RefPtr<ServiceWorkerUnregisterJob> unregisterJob =
        static_cast<ServiceWorkerUnregisterJob*>(aJob);
    mCallback->UnregisterSucceeded(unregisterJob->GetResult());
  }

  void JobDiscarded(ErrorResult&) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mCallback);

    mCallback->UnregisterFailed();
    mCallback = nullptr;
  }

  NS_INLINE_DECL_REFCOUNTING(UnregisterJobCallback, override)
};

}  // anonymous namespace

NS_IMETHODIMP
ServiceWorkerManager::Unregister(nsIPrincipal* aPrincipal,
                                 nsIServiceWorkerUnregisterCallback* aCallback,
                                 const nsAString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!aPrincipal) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv;

// This is not accessible by content, and callers should always ensure scope is
// a correct URI, so this is wrapped in DEBUG
#ifdef DEBUG
  nsCOMPtr<nsIURI> scopeURI;
  rv = NS_NewURI(getter_AddRefs(scopeURI), aScope);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_DOM_SECURITY_ERR;
  }
#endif

  nsAutoCString scopeKey;
  rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  NS_ConvertUTF16toUTF8 scope(aScope);
  RefPtr<ServiceWorkerJobQueue> queue = GetOrCreateJobQueue(scopeKey, scope);

  RefPtr<ServiceWorkerUnregisterJob> job =
      new ServiceWorkerUnregisterJob(aPrincipal, scope);

  if (aCallback) {
    RefPtr<UnregisterJobCallback> cb = new UnregisterJobCallback(aCallback);
    job->AppendResultCallback(cb);
  }

  queue->ScheduleJob(job);
  return NS_OK;
}

void ServiceWorkerManager::WorkerIsIdle(ServiceWorkerInfo* aWorker) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aWorker);

  RefPtr<ServiceWorkerRegistrationInfo> reg =
      GetRegistration(aWorker->Principal(), aWorker->Scope());
  if (!reg) {
    return;
  }

  // We only care about the active worker becoming idle because it means we
  // can now promote a waiting worker to active.
  if (reg->GetActive() != aWorker) {
    return;
  }

  // The active worker becoming idle is not a reason to extend a waiting SW's
  // lifetime (or spawn it) on its own because that potentially enables
  // unlimited lifetime extension.  `TryToActivate` will handle upgrading the
  // lifetime extension to a full extension if the registration is controlling
  // pages (and skipWaiting was used).  It's fine for the ServiceWorker to
  // receive its activation message prior to its next functional event.
  reg->TryToActivateAsync(
      ServiceWorkerLifetimeExtension(NoLifetimeExtension{}));
}

already_AddRefed<ServiceWorkerJobQueue>
ServiceWorkerManager::GetOrCreateJobQueue(const nsACString& aKey,
                                          const nsACString& aScope) {
  MOZ_ASSERT(!aKey.IsEmpty());
  ServiceWorkerManager::RegistrationDataPerPrincipal* data;
  // XXX we could use WithEntryHandle here to avoid a hashtable lookup, except
  // that leads to a false positive assertion, see bug 1370674 comment 7.
  if (!mRegistrationInfos.Get(aKey, &data)) {
    data = mRegistrationInfos
               .InsertOrUpdate(aKey, MakeUnique<RegistrationDataPerPrincipal>())
               .get();
  }

  RefPtr queue = data->mJobQueues.GetOrInsertNew(aScope);
  return queue.forget();
}

/* static */
already_AddRefed<ServiceWorkerManager> ServiceWorkerManager::GetInstance() {
  if (!gInstance) {
    RefPtr<ServiceWorkerRegistrar> swr;

    // XXX: Substitute this with an assertion. See comment in Init.
    if (XRE_IsParentProcess()) {
      // Don't (re-)create the ServiceWorkerManager if we are already shutting
      // down.
      if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
        return nullptr;
      }
      // Don't create the ServiceWorkerManager until the ServiceWorkerRegistrar
      // is initialized.
      swr = ServiceWorkerRegistrar::Get();
      if (!swr) {
        return nullptr;
      }
    }

    MOZ_ASSERT(NS_IsMainThread());

    gInstance = new ServiceWorkerManager();
    gInstance->Init(swr);
    ClearOnShutdown(&gInstance);
  }
  RefPtr<ServiceWorkerManager> copy = gInstance.get();
  return copy.forget();
}

void ServiceWorkerManager::ReportToAllClients(
    const nsCString& aScope, const nsString& aMessage,
    const nsCString& aFilename, const nsString& aLine, uint32_t aLineNumber,
    uint32_t aColumnNumber, uint32_t aFlags) {
  ConsoleUtils::ReportForServiceWorkerScope(
      NS_ConvertUTF8toUTF16(aScope), aMessage, aFilename, aLineNumber,
      aColumnNumber, ConsoleUtils::eError);
}

/* static */
void ServiceWorkerManager::LocalizeAndReportToAllClients(
    const nsCString& aScope, const char* aStringKey,
    const nsTArray<nsString>& aParamArray, uint32_t aFlags,
    const nsCString& aFilename, const nsString& aLine, uint32_t aLineNumber,
    uint32_t aColumnNumber) {
  if (!NS_IsMainThread()) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "ServiceWorkerManager::LocalizeAndReportToAllClients",
        [scope = aScope, stringKey = nsCString(aStringKey),
         paramArray = aParamArray.Clone(), aFlags, filename = aFilename,
         line = aLine, aLineNumber, aColumnNumber]() {
          LocalizeAndReportToAllClients(scope, stringKey.get(), paramArray,
                                        aFlags, filename, line, aLineNumber,
                                        aColumnNumber);
        }));
    return;
  }

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (!swm) {
    return;
  }

  nsresult rv;
  nsAutoString message;
  rv = nsContentUtils::FormatLocalizedString(PropertiesFile::DOM_PROPERTIES,
                                             aStringKey, aParamArray, message);
  if (NS_SUCCEEDED(rv)) {
    swm->ReportToAllClients(aScope, message, aFilename, aLine, aLineNumber,
                            aColumnNumber, aFlags);
  } else {
    NS_WARNING("Failed to format and therefore report localized error.");
  }
}

void ServiceWorkerManager::HandleError(
    JSContext* aCx, nsIPrincipal* aPrincipal, const nsCString& aScope,
    const nsCString& aWorkerURL, const nsString& aMessage,
    const nsCString& aFilename, const nsString& aLine, uint32_t aLineNumber,
    uint32_t aColumnNumber, uint32_t aFlags, JSExnType aExnType) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  ServiceWorkerManager::RegistrationDataPerPrincipal* data;
  if (NS_WARN_IF(!mRegistrationInfos.Get(scopeKey, &data))) {
    return;
  }

  // Always report any uncaught exceptions or errors to the console of
  // each client.
  ReportToAllClients(aScope, aMessage, aFilename, aLine, aLineNumber,
                     aColumnNumber, aFlags);
}

void ServiceWorkerManager::PurgeServiceWorker(
    const ServiceWorkerRegistrationData& aRegistration,
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(mActor);
  serviceWorkerScriptCache::PurgeCache(aPrincipal, aRegistration.cacheName());
  MaybeSendUnregister(aPrincipal, aRegistration.scope());
}

void ServiceWorkerManager::LoadRegistration(
    const ServiceWorkerRegistrationData& aRegistration) {
  MOZ_ASSERT(NS_IsMainThread());

  auto principalOrErr = PrincipalInfoToPrincipal(aRegistration.principal());
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return;
  }
  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  if (!StaticPrefs::dom_serviceWorkers_enabled()) {
    // If service workers are disabled, remove the registration from disk
    // instead of loading.
    PurgeServiceWorker(aRegistration, principal);
    return;
  }

  // Purge extensions registrations if they are disabled by prefs.
  if (!StaticPrefs::extensions_backgroundServiceWorker_enabled_AtStartup()) {
    nsCOMPtr<nsIURI> uri = principal->GetURI();

    // We do check the URI scheme here because when this is going to run
    // the extension may not have been loaded yet and the WebExtensionPolicy
    // may not exist yet.
    if (uri->SchemeIs("moz-extension")) {
      PurgeServiceWorker(aRegistration, principal);
      return;
    }
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetRegistration(principal, aRegistration.scope());
  if (!registration) {
    registration = CreateNewRegistration(
        aRegistration.scope(), aRegistration.type(), principal,
        static_cast<ServiceWorkerUpdateViaCache>(
            aRegistration.updateViaCache()),
        aRegistration.navigationPreloadState());
  } else {
    // If active worker script matches our expectations for a "current worker",
    // then we are done. Since scripts with the same URL might have different
    // contents such as updated scripts or scripts with different LoadFlags, we
    // use the CacheName to judge whether the two scripts are identical, where
    // the CacheName is an UUID generated when a new script is found.
    if (registration->GetActive() &&
        registration->GetActive()->CacheName() == aRegistration.cacheName()) {
      // No needs for updates.
      return;
    }
  }

  registration->SetLastUpdateTime(aRegistration.lastUpdateTime());

  nsLoadFlags importsLoadFlags = nsIChannel::LOAD_BYPASS_SERVICE_WORKER;
  if (aRegistration.updateViaCache() !=
      static_cast<uint16_t>(ServiceWorkerUpdateViaCache::None)) {
    importsLoadFlags |= nsIRequest::VALIDATE_ALWAYS;
  }

  const nsCString& currentWorkerURL = aRegistration.currentWorkerURL();
  if (!currentWorkerURL.IsEmpty()) {
    registration->SetActive(new ServiceWorkerInfo(
        registration->Principal(), registration->Scope(), registration->Type(),
        registration->Id(), registration->Version(), currentWorkerURL,
        aRegistration.cacheName(), importsLoadFlags));
    registration->GetActive()->SetHandlesFetch(
        aRegistration.currentWorkerHandlesFetch());
    registration->GetActive()->SetInstalledTime(
        aRegistration.currentWorkerInstalledTime());
    registration->GetActive()->SetActivatedTime(
        aRegistration.currentWorkerActivatedTime());
  }
}

void ServiceWorkerManager::LoadRegistrations(
    const nsTArray<ServiceWorkerRegistrationData>& aRegistrations) {
  MOZ_ASSERT(NS_IsMainThread());
  for (uint32_t i = 0, len = aRegistrations.Length(); i < len; ++i) {
    LoadRegistration(aRegistrations[i]);
  }
}

void ServiceWorkerManager::StoreRegistration(
    nsIPrincipal* aPrincipal, ServiceWorkerRegistrationInfo* aRegistration) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aRegistration);

  if (mShuttingDown) {
    return;
  }

  // Do not store private browsing registrations to disk; our in-memory state
  // suffices.
  if (aPrincipal->GetIsInPrivateBrowsing()) {
    // If we are seeing a PBM principal, PBM support must be enabled.
    MOZ_ASSERT(StaticPrefs::dom_serviceWorkers_privateBrowsing_enabled());
    return;
  }

  // Do not store a registration for addons that are not installed, not enabled
  // or installed temporarily.
  //
  // If the dom.serviceWorkers.testing.persistTemporaryInstalledAddons is set
  // to true, the registration for a temporary installed addon will still be
  // persisted (only meant to be used to make it easier to test some particular
  // scenario with a temporary installed addon which doesn't need to be signed
  // to be installed on release channel builds).
  if (aPrincipal->SchemeIs("moz-extension")) {
    RefPtr<extensions::WebExtensionPolicy> addonPolicy =
        BasePrincipal::Cast(aPrincipal)->AddonPolicy();
    if (!addonPolicy || !addonPolicy->Active() ||
        (addonPolicy->TemporarilyInstalled() &&
         !StaticPrefs::
             dom_serviceWorkers_testing_persistTemporarilyInstalledAddons())) {
      return;
    }
  }

  ServiceWorkerRegistrationData data;
  nsresult rv = PopulateRegistrationData(aPrincipal, aRegistration, data);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  PrincipalInfo principalInfo;
  if (NS_WARN_IF(
          NS_FAILED(PrincipalToPrincipalInfo(aPrincipal, &principalInfo)))) {
    return;
  }

  mActor->SendRegister(data);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(
    const ClientInfo& aClientInfo) const {
  auto principalOrErr = aClientInfo.GetPrincipal();
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return nullptr;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aClientInfo.URL());
  NS_ENSURE_SUCCESS(rv, nullptr);

  return GetServiceWorkerRegistrationInfo(principal, uri);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(nsIPrincipal* aPrincipal,
                                                       nsIURI* aURI) const {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aURI);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return GetServiceWorkerRegistrationInfo(scopeKey, aURI);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetServiceWorkerRegistrationInfo(
    const nsACString& aScopeKey, nsIURI* aURI) const {
  MOZ_ASSERT(aURI);

  nsAutoCString spec;
  nsresult rv = aURI->GetSpec(spec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  nsAutoCString scope;
  RegistrationDataPerPrincipal* data;
  if (!FindScopeForPath(aScopeKey, spec, &data, scope)) {
    return nullptr;
  }

  MOZ_ASSERT(data);

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  data->mInfos.Get(scope, getter_AddRefs(registration));
  // ordered scopes and registrations better be in sync.
  MOZ_ASSERT(registration);

#ifdef DEBUG
  nsAutoCString origin;
  rv = registration->Principal()->GetOrigin(origin);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  MOZ_ASSERT(origin.Equals(aScopeKey));
#endif

  return registration.forget();
}

/* static */
nsresult ServiceWorkerManager::PrincipalToScopeKey(nsIPrincipal* aPrincipal,
                                                   nsACString& aKey) {
  MOZ_ASSERT(aPrincipal);

  if (!BasePrincipal::Cast(aPrincipal)->IsContentPrincipal()) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv = aPrincipal->GetOrigin(aKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

/* static */
nsresult ServiceWorkerManager::PrincipalInfoToScopeKey(
    const PrincipalInfo& aPrincipalInfo, nsACString& aKey) {
  if (aPrincipalInfo.type() != PrincipalInfo::TContentPrincipalInfo) {
    return NS_ERROR_FAILURE;
  }

  auto content = aPrincipalInfo.get_ContentPrincipalInfo();

  nsAutoCString suffix;
  content.attrs().CreateSuffix(suffix);

  aKey = content.originNoSuffix();
  aKey.Append(suffix);

  return NS_OK;
}

/* static */
void ServiceWorkerManager::AddScopeAndRegistration(
    const nsACString& aScope, ServiceWorkerRegistrationInfo* aInfo) {
  MOZ_ASSERT(aInfo);
  MOZ_ASSERT(aInfo->Principal());
  MOZ_ASSERT(!aInfo->IsUnregistered());

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (!swm) {
    // browser shutdown
    return;
  }

  nsAutoCString scopeKey;
  nsresult rv = swm->PrincipalToScopeKey(aInfo->Principal(), scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  MOZ_ASSERT(!scopeKey.IsEmpty());

  auto* const data = swm->mRegistrationInfos.GetOrInsertNew(scopeKey);
  data->mScopeContainer.InsertScope(aScope);
  data->mInfos.InsertOrUpdate(aScope, RefPtr{aInfo});
  swm->NotifyListenersOnRegister(aInfo);
}

/* static */
bool ServiceWorkerManager::FindScopeForPath(
    const nsACString& aScopeKey, const nsACString& aPath,
    RegistrationDataPerPrincipal** aData, nsACString& aMatch) {
  MOZ_ASSERT(aData);

  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();

  if (!swm || !swm->mRegistrationInfos.Get(aScopeKey, aData)) {
    return false;
  }

  Maybe<nsCString> scope = (*aData)->mScopeContainer.MatchScope(aPath);

  if (scope) {
    // scope.isSome() will still truen true after this; we are just moving the
    // string inside the Maybe, so the Maybe will contain an empty string.
    aMatch = std::move(*scope);
  }

  return scope.isSome();
}

/* static */
bool ServiceWorkerManager::HasScope(nsIPrincipal* aPrincipal,
                                    const nsACString& aScope) {
  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (!swm) {
    return false;
  }

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  RegistrationDataPerPrincipal* data;
  if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
    return false;
  }

  return data->mScopeContainer.Contains(aScope);
}

/* static */
void ServiceWorkerManager::RemoveScopeAndRegistration(
    ServiceWorkerRegistrationInfo* aRegistration) {
  RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
  if (!swm) {
    return;
  }

  nsAutoCString scopeKey;
  nsresult rv = swm->PrincipalToScopeKey(aRegistration->Principal(), scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!swm->mRegistrationInfos.Get(scopeKey, &data)) {
    return;
  }

  if (auto entry = data->mUpdateTimers.Lookup(aRegistration->Scope())) {
    entry.Data()->Cancel();
    entry.Remove();
  }

  // Verify there are no controlled clients for the purged registration.
  for (auto iter = swm->mControlledClients.Iter(); !iter.Done(); iter.Next()) {
    auto& reg = iter.UserData()->mRegistrationInfo;
    if (reg->Scope().Equals(aRegistration->Scope()) &&
        reg->Principal()->Equals(aRegistration->Principal()) &&
        reg->IsCorrupt()) {
      iter.Remove();
    }
  }

  RefPtr<ServiceWorkerRegistrationInfo> info;
  data->mInfos.Remove(aRegistration->Scope(), getter_AddRefs(info));
  aRegistration->SetUnregistered();
  data->mScopeContainer.RemoveScope(aRegistration->Scope());
  swm->NotifyListenersOnUnregister(info);

  swm->MaybeRemoveRegistrationInfo(scopeKey);
}

void ServiceWorkerManager::MaybeRemoveRegistrationInfo(
    const nsACString& aScopeKey) {
  if (auto entry = mRegistrationInfos.Lookup(aScopeKey)) {
    if (entry.Data()->mScopeContainer.IsEmpty() &&
        entry.Data()->mJobQueues.Count() == 0) {
      entry.Remove();

      // Need to reset the mQuotaUsageCheckCount, if
      // RegistrationDataPerPrincipal:: mScopeContainer is empty. This
      // RegistrationDataPerPrincipal might be reused, such that quota usage
      // mitigation can be triggered for the new added registration.
    } else if (entry.Data()->mScopeContainer.IsEmpty() &&
               entry.Data()->mQuotaUsageCheckCount) {
      entry.Data()->mQuotaUsageCheckCount = 0;
    }
  }
}

bool ServiceWorkerManager::StartControlling(
    const ClientInfo& aClientInfo,
    const ServiceWorkerDescriptor& aServiceWorker) {
  MOZ_ASSERT(NS_IsMainThread());

  auto principalOrErr =
      PrincipalInfoToPrincipal(aServiceWorker.PrincipalInfo());

  if (NS_WARN_IF(principalOrErr.isErr())) {
    return false;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  nsCOMPtr<nsIURI> scope;
  nsresult rv = NS_NewURI(getter_AddRefs(scope), aServiceWorker.Scope());
  NS_ENSURE_SUCCESS(rv, false);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetServiceWorkerRegistrationInfo(principal, scope);
  NS_ENSURE_TRUE(registration, false);
  NS_ENSURE_TRUE(registration->GetActive(), false);

  StartControllingClient(aClientInfo, registration);

  return true;
}

void ServiceWorkerManager::MaybeCheckNavigationUpdate(
    const ClientInfo& aClientInfo) {
  MOZ_ASSERT(NS_IsMainThread());
  // We perform these success path navigation update steps when the
  // document tells us its more or less done loading.  This avoids
  // slowing down page load and also lets pages consistently get
  // updatefound events when they fire.
  //
  // 9.8.20 If respondWithEntered is false, then:
  // 9.8.22 Else: (respondWith was entered and succeeded)
  //    If request is a non-subresource request, then: Invoke Soft Update
  //    algorithm.
  ControlledClientData* data = mControlledClients.Get(aClientInfo.Id());
  if (data && data->mRegistrationInfo) {
    data->mRegistrationInfo->MaybeScheduleUpdate();
  }
}

void ServiceWorkerManager::StopControllingRegistration(
    ServiceWorkerRegistrationInfo* aRegistration) {
  aRegistration->StopControllingClient();
  if (aRegistration->IsControllingClients()) {
    return;
  }

  if (aRegistration->IsUnregistered()) {
    if (aRegistration->IsIdle()) {
      aRegistration->Clear();
    } else {
      aRegistration->ClearWhenIdle();
    }
    return;
  }

  // We used to aggressively terminate the worker at this point, but it
  // caused problems.  There are more uses for a service worker than actively
  // controlled documents.  We need to let the worker naturally terminate
  // in case it's handling push events, message events, etc.
  aRegistration->TryToActivateAsync(
      ServiceWorkerLifetimeExtension(NoLifetimeExtension{}));
}

NS_IMETHODIMP
ServiceWorkerManager::GetScopeForUrl(nsIPrincipal* aPrincipal,
                                     const nsAString& aUrl, nsAString& aScope) {
  MOZ_ASSERT(aPrincipal);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aUrl);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ServiceWorkerRegistrationInfo> r =
      GetServiceWorkerRegistrationInfo(aPrincipal, uri);
  if (!r) {
    return NS_ERROR_FAILURE;
  }

  CopyUTF8toUTF16(r->Scope(), aScope);
  return NS_OK;
}

namespace {

class ContinueDispatchFetchEventRunnable : public Runnable {
  RefPtr<ServiceWorkerPrivate> mServiceWorkerPrivate;
  nsCOMPtr<nsIInterceptedChannel> mChannel;
  nsCOMPtr<nsILoadGroup> mLoadGroup;

 public:
  ContinueDispatchFetchEventRunnable(
      ServiceWorkerPrivate* aServiceWorkerPrivate,
      nsIInterceptedChannel* aChannel, nsILoadGroup* aLoadGroup)
      : Runnable(
            "dom::ServiceWorkerManager::ContinueDispatchFetchEventRunnable"),
        mServiceWorkerPrivate(aServiceWorkerPrivate),
        mChannel(aChannel),
        mLoadGroup(aLoadGroup) {
    MOZ_ASSERT(aServiceWorkerPrivate);
    MOZ_ASSERT(aChannel);
  }

  void HandleError() {
    MOZ_ASSERT(NS_IsMainThread());
    NS_WARNING("Unexpected error while dispatching fetch event!");
    nsresult rv = mChannel->ResetInterception(false);
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to resume intercepted network request");
      mChannel->CancelInterception(rv);
    }
  }

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    nsCOMPtr<nsIChannel> channel;
    nsresult rv = mChannel->GetChannel(getter_AddRefs(channel));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      HandleError();
      return NS_OK;
    }

    // The channel might have encountered an unexpected error while ensuring
    // the upload stream is cloneable.  Check here and reset the interception
    // if that happens.
    nsresult status;
    rv = channel->GetStatus(&status);
    if (NS_WARN_IF(NS_FAILED(rv) || NS_FAILED(status))) {
      HandleError();
      return NS_OK;
    }

    nsString clientId;
    nsString resultingClientId;
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    Maybe<ClientInfo> clientInfo = loadInfo->GetClientInfo();
    if (clientInfo.isSome()) {
      clientId = NSID_TrimBracketsUTF16(clientInfo->Id());
    }

    // Having an initial or reserved client are mutually exclusive events:
    // either an initial client is used upon navigating an about:blank
    // iframe, or a new, reserved environment/client is created (e.g.
    // upon a top-level navigation). See step 4 of
    // https://html.spec.whatwg.org/#process-a-navigate-fetch as well as
    // https://github.com/w3c/ServiceWorker/issues/1228#issuecomment-345132444
    Maybe<ClientInfo> resulting = loadInfo->GetInitialClientInfo();

    if (resulting.isNothing()) {
      resulting = loadInfo->GetReservedClientInfo();
    } else {
      MOZ_ASSERT(loadInfo->GetReservedClientInfo().isNothing());
    }

    if (resulting.isSome()) {
      resultingClientId = NSID_TrimBracketsUTF16(resulting->Id());
    }

    rv = mServiceWorkerPrivate->SendFetchEvent(mChannel, mLoadGroup, clientId,
                                               resultingClientId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      HandleError();
    }

    return NS_OK;
  }
};

}  // anonymous namespace

void ServiceWorkerManager::DispatchFetchEvent(nsIInterceptedChannel* aChannel,
                                              ErrorResult& aRv) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCOMPtr<nsIChannel> internalChannel;
  aRv = aChannel->GetChannel(getter_AddRefs(internalChannel));
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  nsCOMPtr<nsILoadGroup> loadGroup;
  aRv = internalChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = internalChannel->LoadInfo();
  RefPtr<ServiceWorkerInfo> serviceWorker;

  if (!nsContentUtils::IsNonSubresourceRequest(internalChannel)) {
    const Maybe<ServiceWorkerDescriptor>& controller =
        loadInfo->GetController();
    if (NS_WARN_IF(controller.isNothing())) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    RefPtr<ServiceWorkerRegistrationInfo> registration;
    nsresult rv = GetClientRegistration(loadInfo->GetClientInfo().ref(),
                                        getter_AddRefs(registration));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return;
    }

    serviceWorker = registration->GetActive();
    if (NS_WARN_IF(!serviceWorker) ||
        NS_WARN_IF(serviceWorker->Descriptor().Id() != controller.ref().Id())) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }
  } else {
    nsCOMPtr<nsIURI> uri;
    aRv = aChannel->GetSecureUpgradedChannelURI(getter_AddRefs(uri));
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    // non-subresource request means the URI contains the principal
    OriginAttributes attrs = loadInfo->GetOriginAttributes();
    if (StaticPrefs::privacy_partition_serviceWorkers()) {
      StoragePrincipalHelper::GetOriginAttributes(
          internalChannel, attrs,
          StoragePrincipalHelper::eForeignPartitionedPrincipal);
    }

    nsCOMPtr<nsIPrincipal> principal =
        BasePrincipal::CreateContentPrincipal(uri, attrs);

    RefPtr<ServiceWorkerRegistrationInfo> registration =
        GetServiceWorkerRegistrationInfo(principal, uri);
    if (NS_WARN_IF(!registration)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    // While we only enter this method if IsAvailable() previously saw
    // an active worker, it is possible for that worker to be removed
    // before we get to this point.  Therefore we must handle a nullptr
    // active worker here.
    serviceWorker = registration->GetActive();
    if (NS_WARN_IF(!serviceWorker)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return;
    }

    // If there is a reserved client it should be marked as controlled before
    // the FetchEvent is dispatched.
    Maybe<ClientInfo> clientInfo = loadInfo->GetReservedClientInfo();

    // Also override the initial about:blank controller since the real
    // network load may be intercepted by a different service worker.  If
    // the intial about:blank has a controller here its simply been
    // inherited from its parent.
    if (clientInfo.isNothing()) {
      clientInfo = loadInfo->GetInitialClientInfo();

      // TODO: We need to handle the case where the initial about:blank is
      //       controlled, but the final document load is not.  Right now
      //       the spec does not really say what to do.  There currently
      //       is no way for the controller to be cleared from a client in
      //       the spec or our implementation.  We may want to force a
      //       new inner window to be created instead of reusing the
      //       initial about:blank global.  See bug 1419620 and the spec
      //       issue here: https://github.com/w3c/ServiceWorker/issues/1232
    }

    if (clientInfo.isSome()) {
      // ClientChannelHelper is not called for STS upgrades that get
      // intercepted by a service worker when interception occurs in
      // the content process.  Therefore the reserved client is not
      // properly cleared in that case leading to a situation where
      // a ClientSource with an http:// principal is controlled by
      // a ServiceWorker with an https:// principal.
      //
      // This does not occur when interception is handled by the
      // simpler InterceptedHttpChannel approach in the parent.
      //
      // As a temporary work around check for this principal mismatch
      // here and perform the ClientChannelHelper's replacement of
      // reserved client automatically.
      if (!XRE_IsParentProcess()) {
        auto clientPrincipalOrErr = clientInfo.ref().GetPrincipal();

        nsCOMPtr<nsIPrincipal> clientPrincipal;
        if (clientPrincipalOrErr.isOk()) {
          clientPrincipal = clientPrincipalOrErr.unwrap();
        }

        if (!clientPrincipal || !clientPrincipal->Equals(principal)) {
          UniquePtr<ClientSource> reservedClient =
              loadInfo->TakeReservedClientSource();

          nsCOMPtr<nsISerialEventTarget> target =
              reservedClient ? reservedClient->EventTarget()
                             : GetMainThreadSerialEventTarget();

          reservedClient.reset();
          reservedClient = ClientManager::CreateSource(ClientType::Window,
                                                       target, principal);

          loadInfo->GiveReservedClientSource(std::move(reservedClient));

          clientInfo = loadInfo->GetReservedClientInfo();
        }
      }

      // First, attempt to mark the reserved client controlled directly.  This
      // will update the controlled status in the ClientManagerService in the
      // parent.  It will also eventually propagate back to the ClientSource.
      StartControllingClient(clientInfo.ref(), registration);
    }

    uint32_t redirectMode = nsIHttpChannelInternal::REDIRECT_MODE_MANUAL;
    nsCOMPtr<nsIHttpChannelInternal> http = do_QueryInterface(internalChannel);
    MOZ_ALWAYS_SUCCEEDS(http->GetRedirectMode(&redirectMode));

    // Synthetic redirects for non-subresource requests with a "follow"
    // redirect mode may switch controllers.  This is basically worker
    // scripts right now.  In this case we need to explicitly clear the
    // controller to avoid assertions on the SetController() below.
    if (redirectMode == nsIHttpChannelInternal::REDIRECT_MODE_FOLLOW) {
      loadInfo->ClearController();
    }

    // But we also note the reserved state on the LoadInfo.  This allows the
    // ClientSource to be updated immediately after the nsIChannel starts.
    // This is necessary to have the correct controller in place for immediate
    // follow-on requests.
    loadInfo->SetController(serviceWorker->Descriptor());
  }

  MOZ_DIAGNOSTIC_ASSERT(serviceWorker);

  RefPtr<ContinueDispatchFetchEventRunnable> continueRunnable =
      new ContinueDispatchFetchEventRunnable(serviceWorker->WorkerPrivate(),
                                             aChannel, loadGroup);

  // When this service worker was registered, we also sent down the permissions
  // for the runnable. They should have arrived by now, but we still need to
  // wait for them if they have not.
  RefPtr<PermissionManager> permMgr = PermissionManager::GetInstance();
  if (permMgr) {
    permMgr->WhenPermissionsAvailable(serviceWorker->Principal(),
                                      continueRunnable);
  } else {
    continueRunnable->HandleError();
  }
}

ServiceWorkerLifetimeExtension ServiceWorkerManager::DetermineLifetimeForClient(
    const ClientInfo& aClientInfo) {
  // For everything but ServiceWorkers this should extend the ServiceWorker's
  // lifetime.
  if (aClientInfo.Type() == ClientType::Serviceworker) {
    // But for the ServiceWorker we need to find out the originating
    // ServiceWorker's lifetime and use that.
    ServiceWorkerInfo* source = GetServiceWorkerByClientInfo(aClientInfo);
    if (source) {
      return ServiceWorkerLifetimeExtension(PropagatedLifetimeExtension{
          .mDeadline = source->LifetimeDeadline(),
      });
    }

    // If we can't find the ServiceWorker, then it's gone and we shouldn't
    // extend the lifetime.
    return ServiceWorkerLifetimeExtension(NoLifetimeExtension{});
  }

  return ServiceWorkerLifetimeExtension(FullLifetimeExtension{});
}

ServiceWorkerLifetimeExtension
ServiceWorkerManager::DetermineLifetimeForServiceWorker(
    const ServiceWorkerDescriptor& aServiceWorker) {
  ServiceWorkerInfo* source = GetServiceWorkerByDescriptor(aServiceWorker);
  if (source) {
    return ServiceWorkerLifetimeExtension(PropagatedLifetimeExtension{
        .mDeadline = source->LifetimeDeadline(),
    });
  }

  // If we can't find the ServiceWorker, then it's gone and we shouldn't
  // extend the lifetime.
  return ServiceWorkerLifetimeExtension(NoLifetimeExtension{});
}

bool ServiceWorkerManager::IsAvailable(nsIPrincipal* aPrincipal, nsIURI* aURI,
                                       nsIChannel* aChannel) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aURI);
  MOZ_ASSERT(aChannel);

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetServiceWorkerRegistrationInfo(aPrincipal, aURI);

  if (!registration || !registration->GetActive()) {
    return false;
  }

  // Checking if the matched service worker handles fetch events or not.
  // If it does, directly return true and handle the client controlling logic
  // in DispatchFetchEvent(). otherwise, do followings then return false.
  // 1. Set the matched service worker as the controller of LoadInfo and
  //    correspoinding ClinetInfo
  // 2. Maybe schedule a soft update
  if (!registration->GetActive()->HandlesFetch()) {
    // Checkin if the channel is not allowed for the service worker.
    auto storageAccess = StorageAllowedForChannel(aChannel);
    nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

    if (storageAccess <= StorageAccess::eDeny) {
      if (!StaticPrefs::privacy_partition_serviceWorkers()) {
        return false;
      }

      nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
      loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));

      if (!StoragePartitioningEnabled(storageAccess, cookieJarSettings)) {
        return false;
      }
    }

    // ServiceWorkerInterceptController::ShouldPrepareForIntercept() handles the
    // subresource cases. Must be non-subresource case here.
    MOZ_ASSERT(nsContentUtils::IsNonSubresourceRequest(aChannel));

    Maybe<ClientInfo> clientInfo = loadInfo->GetReservedClientInfo();
    if (clientInfo.isNothing()) {
      clientInfo = loadInfo->GetInitialClientInfo();
    }

    if (clientInfo.isSome()) {
      StartControllingClient(clientInfo.ref(), registration);
    }

    uint32_t redirectMode = nsIHttpChannelInternal::REDIRECT_MODE_MANUAL;
    nsCOMPtr<nsIHttpChannelInternal> http = do_QueryInterface(aChannel);
    MOZ_ALWAYS_SUCCEEDS(http->GetRedirectMode(&redirectMode));

    // Synthetic redirects for non-subresource requests with a "follow"
    // redirect mode may switch controllers.  This is basically worker
    // scripts right now.  In this case we need to explicitly clear the
    // controller to avoid assertions on the SetController() below.
    if (redirectMode == nsIHttpChannelInternal::REDIRECT_MODE_FOLLOW) {
      loadInfo->ClearController();
    }

    loadInfo->SetController(registration->GetActive()->Descriptor());

    // https://w3c.github.io/ServiceWorker/#on-fetch-request-algorithm 17.1
    // try schedule a soft-update for non-subresource case.
    registration->MaybeScheduleUpdate();
    return false;
  }
  // Found a matching service worker which handles fetch events, return true.
  return true;
}

nsresult ServiceWorkerManager::GetClientRegistration(
    const ClientInfo& aClientInfo,
    ServiceWorkerRegistrationInfo** aRegistrationInfo) {
  ControlledClientData* data = mControlledClients.Get(aClientInfo.Id());
  if (!data || !data->mRegistrationInfo) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // If the document is controlled, the current worker MUST be non-null.
  if (!data->mRegistrationInfo->GetActive()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<ServiceWorkerRegistrationInfo> ref = data->mRegistrationInfo;
  ref.forget(aRegistrationInfo);
  return NS_OK;
}

int32_t ServiceWorkerManager::GetPrincipalQuotaUsageCheckCount(
    nsIPrincipal* aPrincipal) {
  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return -1;
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return -1;
  }

  return data->mQuotaUsageCheckCount;
}

void ServiceWorkerManager::CheckPrincipalQuotaUsage(nsIPrincipal* aPrincipal,
                                                    const nsACString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return;
  }

  // Had already schedule a quota usage check.
  if (data->mQuotaUsageCheckCount != 0) {
    return;
  }

  ++data->mQuotaUsageCheckCount;

  // Get the corresponding ServiceWorkerRegistrationInfo here. Unregisteration
  // might be triggered later, should get it here before it be removed from
  // data.mInfos, such that NotifyListenersOnQuotaCheckFinish() can notify the
  // corresponding ServiceWorkerRegistrationInfo after asynchronous quota
  // checking finish.
  RefPtr<ServiceWorkerRegistrationInfo> info;
  data->mInfos.Get(aScope, getter_AddRefs(info));
  MOZ_ASSERT(info);

  RefPtr<ServiceWorkerManager> self = this;

  ClearQuotaUsageIfNeeded(aPrincipal, [self, info](bool aResult) {
    MOZ_ASSERT(NS_IsMainThread());
    self->NotifyListenersOnQuotaUsageCheckFinish(info);
  });
}

void ServiceWorkerManager::SoftUpdate(const OriginAttributes& aOriginAttributes,
                                      const nsACString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mShuttingDown) {
    return;
  }

  SoftUpdateInternal(aOriginAttributes, aScope, nullptr);
}

namespace {

class UpdateJobCallback final : public ServiceWorkerJob::Callback {
  RefPtr<ServiceWorkerUpdateFinishCallback> mCallback;

  ~UpdateJobCallback() { MOZ_ASSERT(!mCallback); }

 public:
  explicit UpdateJobCallback(ServiceWorkerUpdateFinishCallback* aCallback)
      : mCallback(aCallback) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mCallback);
  }

  void JobFinished(ServiceWorkerJob* aJob, ErrorResult& aStatus) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aJob);
    MOZ_ASSERT(mCallback);

    auto scopeExit = MakeScopeExit([&]() { mCallback = nullptr; });

    if (aStatus.Failed()) {
      mCallback->UpdateFailed(aStatus);
      return;
    }

    MOZ_DIAGNOSTIC_ASSERT(aJob->GetType() == ServiceWorkerJob::Type::Update);
    RefPtr<ServiceWorkerUpdateJob> updateJob =
        static_cast<ServiceWorkerUpdateJob*>(aJob);
    RefPtr<ServiceWorkerRegistrationInfo> reg = updateJob->GetRegistration();
    mCallback->UpdateSucceeded(reg);
  }

  void JobDiscarded(ErrorResult& aStatus) override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mCallback);

    mCallback->UpdateFailed(aStatus);
    mCallback = nullptr;
  }

  NS_INLINE_DECL_REFCOUNTING(UpdateJobCallback, override)
};

}  // anonymous namespace

void ServiceWorkerManager::SoftUpdateInternal(
    const OriginAttributes& aOriginAttributes, const nsACString& aScope,
    ServiceWorkerUpdateFinishCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  if (mShuttingDown) {
    return;
  }

  auto result = ScopeToPrincipal(aScope, aOriginAttributes);
  if (NS_WARN_IF(result.isErr())) {
    return;
  }

  auto principal = result.unwrap();

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(principal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetRegistration(scopeKey, aScope);
  if (NS_WARN_IF(!registration)) {
    return;
  }

  // "If registration's installing worker is not null, abort these steps."
  if (registration->GetInstalling()) {
    return;
  }

  // "Let newestWorker be the result of running Get Newest Worker algorithm
  // passing registration as its argument.
  // If newestWorker is null, abort these steps."
  RefPtr<ServiceWorkerInfo> newest = registration->Newest();
  if (!newest) {
    return;
  }

  // "If the registration queue for registration is empty, invoke Update
  // algorithm, or its equivalent, with client, registration as its argument."
  // TODO(catalinb): We don't implement the force bypass cache flag.
  // See: https://github.com/slightlyoff/ServiceWorker/issues/759
  RefPtr<ServiceWorkerJobQueue> queue = GetOrCreateJobQueue(scopeKey, aScope);

  // Soft updates always correspond to functional events.  Functional events
  // are always events that should result in a fresh, full lifetime extension.
  //
  // The set of known events at this time is documented at
  // https://w3c.github.io/ServiceWorker/#execution-context-events at this time.
  //
  // Note that Lifecycle events (install, activate) are explicitly not
  // functional events.
  auto lifetime = ServiceWorkerLifetimeExtension(FullLifetimeExtension{});

  RefPtr<ServiceWorkerUpdateJob> job = new ServiceWorkerUpdateJob(
      principal, registration->Scope(), newest->ScriptSpec(),
      registration->GetUpdateViaCache(), lifetime);

  if (aCallback) {
    RefPtr<UpdateJobCallback> cb = new UpdateJobCallback(aCallback);
    job->AppendResultCallback(cb);
  }

  queue->ScheduleJob(job);
}

void ServiceWorkerManager::Update(
    const ClientInfo& aClientInfo, nsIPrincipal* aPrincipal,
    const nsACString& aScope, nsCString aNewestWorkerScriptUrl,
    ServiceWorkerUpdateFinishCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aNewestWorkerScriptUrl.IsEmpty());

  UpdateInternal(aClientInfo, aPrincipal, aScope,
                 std::move(aNewestWorkerScriptUrl), aCallback);
}

void ServiceWorkerManager::UpdateInternal(
    const ClientInfo& aClientInfo, nsIPrincipal* aPrincipal,
    const nsACString& aScope, nsCString&& aNewestWorkerScriptUrl,
    ServiceWorkerUpdateFinishCallback* aCallback) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aCallback);
  MOZ_ASSERT(!aNewestWorkerScriptUrl.IsEmpty());

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetRegistration(scopeKey, aScope);
  if (NS_WARN_IF(!registration)) {
    ErrorResult error;
    error.ThrowTypeError<MSG_SW_UPDATE_BAD_REGISTRATION>(aScope, "uninstalled");
    aCallback->UpdateFailed(error);

    // In case the callback does not consume the exception
    error.SuppressException();
    return;
  }

  RefPtr<ServiceWorkerJobQueue> queue = GetOrCreateJobQueue(scopeKey, aScope);

  auto lifetime = DetermineLifetimeForClient(aClientInfo);

  // "Let job be the result of running Create Job with update, registration’s
  // scope url, newestWorker’s script url, promise, and the context object’s
  // relevant settings object."
  RefPtr<ServiceWorkerUpdateJob> job = new ServiceWorkerUpdateJob(
      aPrincipal, registration->Scope(), std::move(aNewestWorkerScriptUrl),
      registration->GetUpdateViaCache(), lifetime);

  RefPtr<UpdateJobCallback> cb = new UpdateJobCallback(aCallback);
  job->AppendResultCallback(cb);

  // "Invoke Schedule Job with job."
  queue->ScheduleJob(job);
}

RefPtr<GenericErrorResultPromise> ServiceWorkerManager::MaybeClaimClient(
    const ClientInfo& aClientInfo,
    ServiceWorkerRegistrationInfo* aWorkerRegistration) {
  MOZ_DIAGNOSTIC_ASSERT(aWorkerRegistration);

  if (!aWorkerRegistration->GetActive()) {
    CopyableErrorResult rv;
    rv.ThrowInvalidStateError("Worker is not active");
    return GenericErrorResultPromise::CreateAndReject(rv, __func__);
  }

  // Same origin check
  auto principalOrErr = aClientInfo.GetPrincipal();

  if (NS_WARN_IF(principalOrErr.isErr())) {
    CopyableErrorResult rv;
    rv.ThrowSecurityError("Could not extract client's principal");
    return GenericErrorResultPromise::CreateAndReject(rv, __func__);
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  if (!aWorkerRegistration->Principal()->Equals(principal)) {
    CopyableErrorResult rv;
    rv.ThrowSecurityError("Worker is for a different origin");
    return GenericErrorResultPromise::CreateAndReject(rv, __func__);
  }

  // The registration that should be controlling the client
  RefPtr<ServiceWorkerRegistrationInfo> matchingRegistration =
      GetServiceWorkerRegistrationInfo(aClientInfo);

  // The registration currently controlling the client
  RefPtr<ServiceWorkerRegistrationInfo> controllingRegistration;
  GetClientRegistration(aClientInfo, getter_AddRefs(controllingRegistration));

  if (aWorkerRegistration != matchingRegistration ||
      aWorkerRegistration == controllingRegistration) {
    return GenericErrorResultPromise::CreateAndResolve(true, __func__);
  }

  return StartControllingClient(aClientInfo, aWorkerRegistration);
}

RefPtr<GenericErrorResultPromise> ServiceWorkerManager::MaybeClaimClient(
    const ClientInfo& aClientInfo,
    const ServiceWorkerDescriptor& aServiceWorker) {
  auto principalOrErr = aServiceWorker.GetPrincipal();
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return GenericErrorResultPromise::CreateAndResolve(false, __func__);
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      GetRegistration(principal, aServiceWorker.Scope());

  // While ServiceWorkerManager is distributed across child processes its
  // possible for us to sometimes get a claim for a new worker that has
  // not propagated to this process yet.  For now, simply note that we
  // are done.  The fix for this is to move the SWM to the parent process
  // so there are no consistency errors.
  if (NS_WARN_IF(!registration) || NS_WARN_IF(!registration->GetActive())) {
    return GenericErrorResultPromise::CreateAndResolve(false, __func__);
  }

  return MaybeClaimClient(aClientInfo, registration);
}

void ServiceWorkerManager::UpdateClientControllers(
    ServiceWorkerRegistrationInfo* aRegistration) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<ServiceWorkerInfo> activeWorker = aRegistration->GetActive();
  MOZ_DIAGNOSTIC_ASSERT(activeWorker);

  AutoTArray<RefPtr<ClientHandle>, 16> handleList;
  for (const auto& client : mControlledClients.Values()) {
    if (client->mRegistrationInfo != aRegistration) {
      continue;
    }

    handleList.AppendElement(client->mClientHandle);
  }

  // Fire event after iterating mControlledClients is done to prevent
  // modification by reentering from the event handlers during iteration.
  for (auto& handle : handleList) {
    RefPtr<GenericErrorResultPromise> p =
        handle->Control(activeWorker->Descriptor());

    RefPtr<ServiceWorkerManager> self = this;

    // If we fail to control the client, then automatically remove it
    // from our list of controlled clients.
    p->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [](bool) {
          // do nothing on success
        },
        [self, clientInfo = handle->Info()](const CopyableErrorResult& aRv) {
          // failed to control, forget about this client
          self->StopControllingClient(clientInfo);
        });
  }
}

void ServiceWorkerManager::EvictFromBFCache(
    ServiceWorkerRegistrationInfo* aRegistration) {
  MOZ_ASSERT(NS_IsMainThread());
  for (const auto& client : mControlledClients.Values()) {
    if (client->mRegistrationInfo == aRegistration) {
      client->mClientHandle->EvictFromBFCache();
    }
  }
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetRegistration(nsIPrincipal* aPrincipal,
                                      const nsACString& aScope) const {
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  return GetRegistration(scopeKey, aScope);
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetRegistration(const PrincipalInfo& aPrincipalInfo,
                                      const nsACString& aScope) const {
  nsAutoCString scopeKey;
  nsresult rv = PrincipalInfoToScopeKey(aPrincipalInfo, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  return GetRegistration(scopeKey, aScope);
}

NS_IMETHODIMP
ServiceWorkerManager::ReloadRegistrationsForTest() {
  if (NS_WARN_IF(!StaticPrefs::dom_serviceWorkers_testing_enabled())) {
    return NS_ERROR_FAILURE;
  }

  // Let's keep it simple and fail if there are any controlled client,
  // the test case can take care of making sure there is none when this
  // method will be called.
  if (NS_WARN_IF(!mControlledClients.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  for (const auto& info : mRegistrationInfos.Values()) {
    for (ServiceWorkerRegistrationInfo* reg : info->mInfos.Values()) {
      MOZ_ASSERT(reg);
      reg->ForceShutdown();
    }
  }

  mRegistrationInfos.Clear();

  nsTArray<ServiceWorkerRegistrationData> data;
  RefPtr<ServiceWorkerRegistrar> swr = ServiceWorkerRegistrar::Get();
  if (NS_WARN_IF(!swr->ReloadDataForTest())) {
    return NS_ERROR_FAILURE;
  }
  swr->GetRegistrations(data);
  LoadRegistrations(data);

  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::RegisterForAddonPrincipal(nsIPrincipal* aPrincipal,
                                                JSContext* aCx,
                                                dom::Promise** aPromise) {
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<Promise> outer = Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  auto enabled =
      StaticPrefs::extensions_backgroundServiceWorker_enabled_AtStartup();
  if (!enabled) {
    outer->MaybeRejectWithNotAllowedError(
        "Disabled. extensions.backgroundServiceWorker.enabled is false");
    outer.forget(aPromise);
    return NS_OK;
  }

  MOZ_ASSERT(aPrincipal);
  auto* addonPolicy = BasePrincipal::Cast(aPrincipal)->AddonPolicy();
  if (!addonPolicy) {
    outer->MaybeRejectWithNotAllowedError("Not an extension principal");
    outer.forget(aPromise);
    return NS_OK;
  }

  nsCString scope;
  auto result = addonPolicy->GetURL(u""_ns);
  if (result.isOk()) {
    scope.Assign(NS_ConvertUTF16toUTF8(result.unwrap()));
  } else {
    outer->MaybeRejectWithUnknownError("Unable to resolve addon scope URL");
    outer.forget(aPromise);
    return NS_OK;
  }

  nsString scriptURL;
  addonPolicy->GetBackgroundWorker(scriptURL);

  if (scriptURL.IsEmpty()) {
    outer->MaybeRejectWithNotFoundError("Missing background worker script url");
    outer.forget(aPromise);
    return NS_OK;
  }

  Maybe<ClientInfo> clientInfo =
      dom::ClientManager::CreateInfo(ClientType::All, aPrincipal);

  if (!clientInfo.isSome()) {
    outer->MaybeRejectWithUnknownError("Error creating clientInfo");
    outer.forget(aPromise);
    return NS_OK;
  }

  auto regPromise = Register(clientInfo.ref(), scope, WorkerType::Classic,
                             NS_ConvertUTF16toUTF8(scriptURL),
                             dom::ServiceWorkerUpdateViaCache::Imports);

  const RefPtr<ServiceWorkerManager> self(this);
  const nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  regPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self, outer, principal,
       scope](const ServiceWorkerRegistrationDescriptor& regDesc) {
        RefPtr<ServiceWorkerRegistrationInfo> registration =
            self->GetRegistration(principal, scope);
        if (registration) {
          outer->MaybeResolve(registration);
        } else {
          outer->MaybeRejectWithUnknownError(
              "Failed to retrieve ServiceWorkerRegistrationInfo");
        }
      },
      [outer](const mozilla::CopyableErrorResult& err) {
        CopyableErrorResult result(err);
        outer->MaybeReject(std::move(result));
      });

  outer.forget(aPromise);

  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::GetRegistrationForAddonPrincipal(
    nsIPrincipal* aPrincipal, nsIServiceWorkerRegistrationInfo** aInfo) {
  MOZ_ASSERT(aPrincipal);

  MOZ_ASSERT(aPrincipal);
  auto* addonPolicy = BasePrincipal::Cast(aPrincipal)->AddonPolicy();
  if (!addonPolicy) {
    return NS_ERROR_FAILURE;
  }

  nsCString scope;
  auto result = addonPolicy->GetURL(u""_ns);
  if (result.isOk()) {
    scope.Assign(NS_ConvertUTF16toUTF8(result.unwrap()));
  } else {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), scope);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ServiceWorkerRegistrationInfo> info =
      GetServiceWorkerRegistrationInfo(aPrincipal, scopeURI);
  if (!info) {
    aInfo = nullptr;
    return NS_OK;
  }
  info.forget(aInfo);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::WakeForExtensionAPIEvent(
    const nsAString& aExtensionBaseURL, const nsAString& aAPINamespace,
    const nsAString& aAPIEventName, JSContext* aCx, dom::Promise** aPromise) {
  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<Promise> outer = Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  auto enabled =
      StaticPrefs::extensions_backgroundServiceWorker_enabled_AtStartup();
  if (!enabled) {
    outer->MaybeRejectWithNotAllowedError(
        "Disabled. extensions.backgroundServiceWorker.enabled is false");
    outer.forget(aPromise);
    return NS_OK;
  }

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aExtensionBaseURL);
  if (NS_FAILED(rv)) {
    outer->MaybeReject(rv);
    outer.forget(aPromise);
    return NS_OK;
  }

  nsCOMPtr<nsIPrincipal> principal = MOZ_TRY(ScopeToPrincipal(scopeURI, {}));

  auto* addonPolicy = BasePrincipal::Cast(principal)->AddonPolicy();
  if (NS_WARN_IF(!addonPolicy)) {
    outer->MaybeRejectWithNotAllowedError(
        "Not an extension principal or extension disabled");
    outer.forget(aPromise);
    return NS_OK;
  }

  OriginAttributes attrs;
  ServiceWorkerInfo* info = GetActiveWorkerInfoForScope(
      attrs, NS_ConvertUTF16toUTF8(aExtensionBaseURL));
  if (NS_WARN_IF(!info)) {
    outer->MaybeRejectWithInvalidStateError(
        "No active worker for the extension background service worker");
    outer.forget(aPromise);
    return NS_OK;
  }

  ServiceWorkerPrivate* workerPrivate = info->WorkerPrivate();
  auto result =
      workerPrivate->WakeForExtensionAPIEvent(aAPINamespace, aAPIEventName);
  if (result.isErr()) {
    outer->MaybeReject(result.propagateErr());
    outer.forget(aPromise);
    return NS_OK;
  }

  RefPtr<ServiceWorkerPrivate::PromiseExtensionWorkerHasListener> innerPromise =
      result.unwrap();

  innerPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [outer](bool aSubscribedEvent) { outer->MaybeResolve(aSubscribedEvent); },
      [outer](nsresult aErrorResult) { outer->MaybeReject(aErrorResult); });

  outer.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::GetRegistrationByPrincipal(
    nsIPrincipal* aPrincipal, const nsAString& aScope,
    nsIServiceWorkerRegistrationInfo** aInfo) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aInfo);

  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScope);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<ServiceWorkerRegistrationInfo> info =
      GetServiceWorkerRegistrationInfo(aPrincipal, scopeURI);
  if (!info) {
    return NS_ERROR_FAILURE;
  }
  info.forget(aInfo);

  return NS_OK;
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetRegistration(const nsACString& aScopeKey,
                                      const nsACString& aScope) const {
  RefPtr<ServiceWorkerRegistrationInfo> reg;

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(aScopeKey, &data)) {
    return reg.forget();
  }

  data->mInfos.Get(aScope, getter_AddRefs(reg));
  return reg.forget();
}

already_AddRefed<ServiceWorkerRegistrationInfo>
ServiceWorkerManager::CreateNewRegistration(
    const nsCString& aScope, const WorkerType& aType, nsIPrincipal* aPrincipal,
    ServiceWorkerUpdateViaCache aUpdateViaCache,
    IPCNavigationPreloadState aNavigationPreloadState) {
#ifdef DEBUG
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIURI> scopeURI;
  nsresult rv = NS_NewURI(getter_AddRefs(scopeURI), aScope);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  RefPtr<ServiceWorkerRegistrationInfo> tmp =
      GetRegistration(aPrincipal, aScope);
  MOZ_ASSERT(!tmp);
#endif

  RefPtr<ServiceWorkerRegistrationInfo> registration =
      new ServiceWorkerRegistrationInfo(aScope, aType, aPrincipal,
                                        aUpdateViaCache,
                                        std::move(aNavigationPreloadState));

  // From now on ownership of registration is with
  // mServiceWorkerRegistrationInfos.
  AddScopeAndRegistration(aScope, registration);
  return registration.forget();
}

void ServiceWorkerManager::MaybeRemoveRegistration(
    ServiceWorkerRegistrationInfo* aRegistration) {
  MOZ_ASSERT(aRegistration);
  RefPtr<ServiceWorkerInfo> newest = aRegistration->Newest();
  if (!newest && HasScope(aRegistration->Principal(), aRegistration->Scope())) {
    RemoveRegistration(aRegistration);
  }
}

void ServiceWorkerManager::RemoveRegistration(
    ServiceWorkerRegistrationInfo* aRegistration) {
  // Note, we do not need to call mActor->SendUnregister() here.  There are a
  // few ways we can get here: 1) Through a normal unregister which calls
  // SendUnregister() in the
  //    unregister job Start() method.
  // 2) Through origin storage being purged.  These result in ForceUnregister()
  //    starting unregister jobs which in turn call SendUnregister().
  // 3) Through the failure to install a new service worker.  Since we don't
  //    store the registration until install succeeds, we do not need to call
  //    SendUnregister here.
  MOZ_ASSERT(HasScope(aRegistration->Principal(), aRegistration->Scope()));

  RemoveScopeAndRegistration(aRegistration);
}

NS_IMETHODIMP
ServiceWorkerManager::GetAllRegistrations(nsIArray** aResult) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIMutableArray> array(do_CreateInstance(NS_ARRAY_CONTRACTID));
  if (!array) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  for (const auto& info : mRegistrationInfos.Values()) {
    for (ServiceWorkerRegistrationInfo* reg : info->mInfos.Values()) {
      MOZ_ASSERT(reg);

      array->AppendElement(reg);
    }
  }

  array.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::RemoveRegistrationsByOriginAttributes(
    const nsAString& aPattern, nsIServiceWorkerUnregisterCallback* aCallback) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  MOZ_ASSERT(!aPattern.IsEmpty());

  OriginAttributesPattern pattern;
  MOZ_ALWAYS_TRUE(pattern.Init(aPattern));

  for (const auto& data : mRegistrationInfos.Values()) {
    // We can use iteration because ForceUnregister (and Unregister) are
    // async. Otherwise doing some R/W operations on an hashtable during
    // iteration will crash.
    for (ServiceWorkerRegistrationInfo* reg : data->mInfos.Values()) {
      MOZ_ASSERT(reg);
      MOZ_ASSERT(reg->Principal());

      if (pattern.Matches(reg->Principal()->OriginAttributesRef())) {
        ForceUnregister(data.get(), reg, aCallback);
      }
    }
  }

  return NS_OK;
}

void ServiceWorkerManager::ForceUnregister(
    RegistrationDataPerPrincipal* aRegistrationData,
    ServiceWorkerRegistrationInfo* aRegistration,
    nsIServiceWorkerUnregisterCallback* aCallback) {
  MOZ_ASSERT(aRegistrationData);
  MOZ_ASSERT(aRegistration);

  RefPtr<ServiceWorkerJobQueue> queue;
  aRegistrationData->mJobQueues.Get(aRegistration->Scope(),
                                    getter_AddRefs(queue));
  if (queue) {
    queue->CancelAll();
  }

  if (auto entry =
          aRegistrationData->mUpdateTimers.Lookup(aRegistration->Scope())) {
    entry.Data()->Cancel();
    entry.Remove();
  }

  // Since Unregister is async, it is ok to call it in an enumeration.
  Unregister(aRegistration->Principal(), aCallback,
             NS_ConvertUTF8toUTF16(aRegistration->Scope()));
}

NS_IMETHODIMP
ServiceWorkerManager::AddListener(nsIServiceWorkerManagerListener* aListener) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!aListener || mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mListeners.AppendElement(aListener);

  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::RemoveListener(
    nsIServiceWorkerManagerListener* aListener) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!aListener || !mListeners.Contains(aListener)) {
    return NS_ERROR_INVALID_ARG;
  }

  mListeners.RemoveElement(aListener);

  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::Observe(nsISupports* aSubject, const char* aTopic,
                              const char16_t* aData) {
  if (strcmp(aTopic, kFinishShutdownTopic) == 0) {
    MaybeFinishShutdown();
    return NS_OK;
  }

  if (strcmp(aTopic, kPrivateBrowsingExited) == 0) {
    nsCOMPtr<nsIPBMCleanupCollector> collector = do_QueryInterface(aSubject);
    RefPtr<PBMUnregisterBarrier> barrier;
    if (collector) {
      nsCOMPtr<nsIPBMCleanupCallback> cb;
      collector->AddPendingCleanup(getter_AddRefs(cb));
      if (cb) {
        barrier = new PBMUnregisterBarrier(cb);
      }
    }

    // barrier ref is held here and by each Unregister job; Complete() fires
    // in the destructor when the last ref (this scope or last job) drops.
    RemoveRegistrationsByOriginAttributes(kPrivateBrowsingOriginPattern,
                                          barrier);

    return NS_OK;
  }

  MOZ_CRASH("Received message we aren't supposed to be registered for!");
  return NS_OK;
}

NS_IMETHODIMP
ServiceWorkerManager::PropagateUnregister(
    nsIPrincipal* aPrincipal, nsIServiceWorkerUnregisterCallback* aCallback,
    const nsAString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);

  // Return earlier with an explicit failure if this xpcom method is called
  // when the ServiceWorkerManager is not initialized yet or it is already
  // shutting down.
  if (NS_WARN_IF(!mActor)) {
    return NS_ERROR_FAILURE;
  }

  PrincipalInfo principalInfo;
  if (NS_WARN_IF(
          NS_FAILED(PrincipalToPrincipalInfo(aPrincipal, &principalInfo)))) {
    return NS_ERROR_FAILURE;
  }

  mActor->SendPropagateUnregister(principalInfo, aScope);

  nsresult rv = Unregister(aPrincipal, aCallback, aScope);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void ServiceWorkerManager::NotifyListenersOnRegister(
    nsIServiceWorkerRegistrationInfo* aInfo) {
  nsTArray<nsCOMPtr<nsIServiceWorkerManagerListener>> listeners(
      mListeners.Clone());
  for (size_t index = 0; index < listeners.Length(); ++index) {
    listeners[index]->OnRegister(aInfo);
  }
}

void ServiceWorkerManager::NotifyListenersOnUnregister(
    nsIServiceWorkerRegistrationInfo* aInfo) {
  nsTArray<nsCOMPtr<nsIServiceWorkerManagerListener>> listeners(
      mListeners.Clone());
  for (size_t index = 0; index < listeners.Length(); ++index) {
    listeners[index]->OnUnregister(aInfo);
  }
}

void ServiceWorkerManager::NotifyListenersOnQuotaUsageCheckFinish(
    nsIServiceWorkerRegistrationInfo* aRegistration) {
  nsTArray<nsCOMPtr<nsIServiceWorkerManagerListener>> listeners(
      mListeners.Clone());
  for (size_t index = 0; index < listeners.Length(); ++index) {
    listeners[index]->OnQuotaUsageCheckFinish(aRegistration);
  }
}

class UpdateTimerCallback final : public nsITimerCallback, public nsINamed {
  nsCOMPtr<nsIPrincipal> mPrincipal;
  const nsCString mScope;

  ~UpdateTimerCallback() = default;

 public:
  UpdateTimerCallback(nsIPrincipal* aPrincipal, const nsACString& aScope)
      : mPrincipal(aPrincipal), mScope(aScope) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mPrincipal);
    MOZ_ASSERT(!mScope.IsEmpty());
  }

  NS_IMETHOD
  Notify(nsITimer* aTimer) override {
    MOZ_ASSERT(NS_IsMainThread());

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (!swm) {
      // shutting down, do nothing
      return NS_OK;
    }

    swm->UpdateTimerFired(mPrincipal, mScope);
    return NS_OK;
  }

  NS_IMETHOD
  GetName(nsACString& aName) override {
    aName.AssignLiteral("UpdateTimerCallback");
    return NS_OK;
  }

  NS_DECL_ISUPPORTS
};

NS_IMPL_ISUPPORTS(UpdateTimerCallback, nsITimerCallback, nsINamed)

void ServiceWorkerManager::ScheduleUpdateTimer(nsIPrincipal* aPrincipal,
                                               const nsACString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(!aScope.IsEmpty());

  if (mShuttingDown) {
    return;
  }

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return;
  }

  data->mUpdateTimers.WithEntryHandle(
      aScope, [&aPrincipal, &aScope](auto&& entry) {
        if (entry) {
          // In case there is already a timer scheduled, just use the original
          // schedule time.  We don't want to push it out to a later time since
          // that could allow updates to be starved forever if events are
          // continuously fired.
          return;
        }

        nsCOMPtr<nsITimerCallback> callback =
            new UpdateTimerCallback(aPrincipal, aScope);

        const uint32_t UPDATE_DELAY_MS = 1000;

        nsCOMPtr<nsITimer> timer;

        const nsresult rv =
            NS_NewTimerWithCallback(getter_AddRefs(timer), callback,
                                    UPDATE_DELAY_MS, nsITimer::TYPE_ONE_SHOT);

        if (NS_WARN_IF(NS_FAILED(rv))) {
          return;
        }

        entry.Insert(std::move(timer));
      });
}

void ServiceWorkerManager::UpdateTimerFired(nsIPrincipal* aPrincipal,
                                            const nsACString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(!aScope.IsEmpty());

  if (mShuttingDown) {
    return;
  }

  // First cleanup the timer.
  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return;
  }

  if (auto entry = data->mUpdateTimers.Lookup(aScope)) {
    entry.Data()->Cancel();
    entry.Remove();
  }

  RefPtr<ServiceWorkerRegistrationInfo> registration;
  data->mInfos.Get(aScope, getter_AddRefs(registration));
  if (!registration) {
    return;
  }

  if (!registration->CheckAndClearIfUpdateNeeded()) {
    return;
  }

  OriginAttributes attrs = aPrincipal->OriginAttributesRef();

  SoftUpdate(attrs, aScope);
}

void ServiceWorkerManager::MaybeSendUnregister(nsIPrincipal* aPrincipal,
                                               const nsACString& aScope) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(!aScope.IsEmpty());

  if (!mActor) {
    return;
  }

  PrincipalInfo principalInfo;
  nsresult rv = PrincipalToPrincipalInfo(aPrincipal, &principalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  (void)mActor->SendUnregister(principalInfo, NS_ConvertUTF8toUTF16(aScope));
}

void ServiceWorkerManager::AddOrphanedRegistration(
    ServiceWorkerRegistrationInfo* aRegistration) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aRegistration);
  MOZ_ASSERT(aRegistration->IsUnregistered());
  MOZ_ASSERT(!aRegistration->IsControllingClients());
  MOZ_ASSERT(!aRegistration->IsIdle());
  MOZ_ASSERT(!mOrphanedRegistrations.has(aRegistration));

  MOZ_ALWAYS_TRUE(mOrphanedRegistrations.putNew(aRegistration));
}

void ServiceWorkerManager::RemoveOrphanedRegistration(
    ServiceWorkerRegistrationInfo* aRegistration) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aRegistration);
  MOZ_ASSERT(aRegistration->IsUnregistered());
  MOZ_ASSERT(!aRegistration->IsControllingClients());
  MOZ_ASSERT(aRegistration->IsIdle());
  MOZ_ASSERT(mOrphanedRegistrations.has(aRegistration));

  mOrphanedRegistrations.remove(aRegistration);
}

uint32_t ServiceWorkerManager::MaybeInitServiceWorkerShutdownProgress() const {
  if (!mShutdownBlocker) {
    return ServiceWorkerShutdownBlocker::kInvalidShutdownStateId;
  }

  return mShutdownBlocker->CreateShutdownState();
}

void ServiceWorkerManager::ReportServiceWorkerShutdownProgress(
    uint32_t aShutdownStateId,
    ServiceWorkerShutdownState::Progress aProgress) const {
  MOZ_ASSERT(mShutdownBlocker);
  mShutdownBlocker->ReportShutdownProgress(aShutdownStateId, aProgress);
}

nsRefPtrHashtable<nsCStringHashKey, ServiceWorkerRegistrationInfo>
ServiceWorkerManager::GetRegistrations(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scopeKey;
  nsresult rv = PrincipalToScopeKey(aPrincipal, scopeKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsRefPtrHashtable<nsCStringHashKey, ServiceWorkerRegistrationInfo>();
  }

  RegistrationDataPerPrincipal* data;
  if (!mRegistrationInfos.Get(scopeKey, &data)) {
    return nsRefPtrHashtable<nsCStringHashKey, ServiceWorkerRegistrationInfo>();
  }

  return data->mInfos.Clone();
}

// ETPPermissionObserver implementation
ETPPermissionObserver::ETPPermissionObserver() { RegisterObserverEvents(); }
ETPPermissionObserver::~ETPPermissionObserver() { UnregisterObserverEvents(); }

NS_IMETHODIMP ETPPermissionObserver::Observe(nsISupports* aSubject,
                                             const char* aTopic,
                                             const char16_t* aSomeData) {
  nsCOMPtr<nsIPermission> perm = nullptr;
  perm = do_QueryInterface(aSubject);
  if (!perm) {
    return NS_OK;
  }

  nsCOMPtr<nsIPrincipal> principal = nullptr;
  perm->GetPrincipal(getter_AddRefs(principal));
  if (!principal) {
    return NS_OK;
  }

  bool isContentPrincipal = false;
  principal->GetIsContentPrincipal(&isContentPrincipal);
  if (!isContentPrincipal) {
    return NS_OK;
  }

  nsAutoCString type;
  perm->GetType(type);
  if (!type.EqualsLiteral("trackingprotection") &&
      !type.EqualsLiteral("trackingprotection-pb")) {
    return NS_OK;
  }

  RefPtr<dom::ServiceWorkerManager> swm =
      dom::ServiceWorkerManager::GetInstance();
  auto registrations = swm->GetRegistrations(principal);

  for (const auto& registrationInfo : registrations.Values()) {
    RefPtr<dom::ServiceWorkerInfo> swInfo =
        registrationInfo->NewestIncludingEvaluating();
    if (!swInfo) {
      continue;
    }

    bool isOnContentBlockingAllowList = ContentBlockingAllowList::Check(
        principal, principal->GetIsInPrivateBrowsing());

    swInfo->WorkerPrivate()->UpdateIsOnContentBlockingAllowList(
        isOnContentBlockingAllowList);
  }

  return NS_OK;
}

void ETPPermissionObserver::RegisterObserverEvents() {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  MOZ_ASSERT(observerService);

  if (observerService) {
    observerService->AddObserver(this, "perm-changed", false);
  }
}

void ETPPermissionObserver::UnregisterObserverEvents() {
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  if (observerService) {
    observerService->RemoveObserver(this, "perm-changed");
  }
}

NS_IMPL_ISUPPORTS(ETPPermissionObserver, nsIObserver, nsISupports)

}  // namespace mozilla::dom
