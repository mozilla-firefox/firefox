/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StorageIPC.h"

#include "LocalStorageManager.h"
#include "SessionStorageCache.h"
#include "SessionStorageManager.h"
#include "SessionStorageObserver.h"
#include "StorageCommon.h"
#include "StorageUtils.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/LocalStorageCommon.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundParent.h"
#include "nsCOMPtr.h"
#include "nsIPrincipal.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

namespace {

using LocalStorageCacheParentHashtable =
    nsClassHashtable<nsCStringHashKey, nsTArray<LocalStorageCacheParent*>>;

StaticAutoPtr<LocalStorageCacheParentHashtable> gLocalStorageCacheParents;

StorageDBChild* sStorageChild[kPrivateBrowsingIdCount] = {nullptr, nullptr};

// False until we shut the storage child down.
bool sStorageChildDown[kPrivateBrowsingIdCount] = {false, false};

}  // namespace

LocalStorageCacheChild::LocalStorageCacheChild(LocalStorageCache* aCache)
    : mCache(aCache) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aCache);
  aCache->AssertIsOnOwningThread();

  MOZ_COUNT_CTOR(LocalStorageCacheChild);
}

LocalStorageCacheChild::~LocalStorageCacheChild() {
  AssertIsOnOwningThread();

  MOZ_COUNT_DTOR(LocalStorageCacheChild);
}

void LocalStorageCacheChild::SendDeleteMeInternal() {
  AssertIsOnOwningThread();

  if (mCache) {
    mCache->ClearActor();
    mCache = nullptr;

    MOZ_ALWAYS_TRUE(PBackgroundLocalStorageCacheChild::SendDeleteMe());
  }
}

void LocalStorageCacheChild::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  if (mCache) {
    mCache->ClearActor();
    mCache = nullptr;
  }
}

mozilla::ipc::IPCResult LocalStorageCacheChild::RecvObserve(
    NotNull<nsIPrincipal*> aPrincipal, const uint32_t& aPrivateBrowsingId,
    const nsAString& aDocumentURI, const nsAString& aKey,
    const nsAString& aOldValue, const nsAString& aNewValue) {
  AssertIsOnOwningThread();

  Storage::NotifyChange(/* aStorage */ nullptr, aPrincipal, aKey, aOldValue,
                        aNewValue,
                        /* aStorageType */ u"localStorage", aDocumentURI,
                        /* aIsPrivate */ !!aPrivateBrowsingId,
                        /* aImmediateDispatch */ true);

  return IPC_OK();
}

// ----------------------------------------------------------------------------
// Child
// ----------------------------------------------------------------------------

class StorageDBChild::ShutdownObserver final : public nsIObserver {
  // Expected to be only 0 or 1.
  const uint32_t mPrivateBrowsingId;

 public:
  explicit ShutdownObserver(const uint32_t aPrivateBrowsingId)
      : mPrivateBrowsingId(aPrivateBrowsingId) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_RELEASE_ASSERT(aPrivateBrowsingId < kPrivateBrowsingIdCount);
  }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

 private:
  ~ShutdownObserver() { MOZ_ASSERT(NS_IsMainThread()); }
};

void StorageDBChild::AddIPDLReference() {
  MOZ_ASSERT(!mIPCOpen, "Attempting to retain multiple IPDL references");
  mIPCOpen = true;
  AddRef();
}

void StorageDBChild::ReleaseIPDLReference() {
  MOZ_ASSERT(mIPCOpen, "Attempting to release non-existent IPDL reference");
  mIPCOpen = false;
  Release();
}

StorageDBChild::StorageDBChild(LocalStorageManager* aManager,
                               const uint32_t aPrivateBrowsingId)
    : mManager(aManager),
      mPrivateBrowsingId(aPrivateBrowsingId),
      mStatus(NS_OK),
      mIPCOpen(false) {
  MOZ_RELEASE_ASSERT(aPrivateBrowsingId < kPrivateBrowsingIdCount);
  MOZ_ASSERT(!NextGenLocalStorageEnabled());
}

StorageDBChild::~StorageDBChild() = default;

// static
StorageDBChild* StorageDBChild::Get(const uint32_t aPrivateBrowsingId) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(aPrivateBrowsingId < kPrivateBrowsingIdCount);
  MOZ_ASSERT(!NextGenLocalStorageEnabled());

  return sStorageChild[aPrivateBrowsingId];
}

// static
StorageDBChild* StorageDBChild::GetOrCreate(const uint32_t aPrivateBrowsingId) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(aPrivateBrowsingId < kPrivateBrowsingIdCount);
  MOZ_ASSERT(!NextGenLocalStorageEnabled());

  StorageDBChild*& storageChild = sStorageChild[aPrivateBrowsingId];
  if (storageChild || sStorageChildDown[aPrivateBrowsingId]) {
    // When sStorageChildDown is at true, sStorageChild is null.
    // Checking sStorageChildDown flag here prevents reinitialization of
    // the storage child after shutdown.
    return storageChild;
  }

  // Use LocalStorageManager::Ensure in case we're called from
  // DOMSessionStorageManager's initializer and we haven't yet initialized the
  // local storage manager.
  RefPtr<StorageDBChild> newStorageChild =
      new StorageDBChild(LocalStorageManager::Ensure(), aPrivateBrowsingId);

  nsresult rv = newStorageChild->Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  newStorageChild.forget(&storageChild);

  return storageChild;
}

nsTHashSet<nsCString>& StorageDBChild::OriginsHavingData() {
  if (!mOriginsHavingData) {
    mOriginsHavingData = std::make_unique<nsTHashSet<nsCString>>();
  }

  return *mOriginsHavingData;
}

nsresult StorageDBChild::Init() {
  MOZ_ASSERT(NS_IsMainThread());

  ::mozilla::ipc::PBackgroundChild* actor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actor)) {
    return NS_ERROR_FAILURE;
  }

  nsString profilePath;
  if (XRE_IsParentProcess() && mPrivateBrowsingId == 0) {
    nsresult rv = StorageDBThread::GetProfilePath(profilePath);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  AddIPDLReference();

  actor->SendPBackgroundStorageConstructor(this, profilePath,
                                           mPrivateBrowsingId);

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  MOZ_ASSERT(observerService);

  nsCOMPtr<nsIObserver> observer = new ShutdownObserver(mPrivateBrowsingId);

  MOZ_ALWAYS_SUCCEEDS(
      observerService->AddObserver(observer, "xpcom-shutdown", false));

  return NS_OK;
}

nsresult StorageDBChild::Shutdown() {
  // There is nothing to do here, IPC will release automatically and
  // the actual thread running on the parent process will also stop
  // automatically in profile-before-change topic observer.
  return NS_OK;
}

void StorageDBChild::AsyncPreload(LocalStorageCacheBridge* aCache,
                                  bool aPriority) {
  if (mIPCOpen) {
    // Adding ref to cache for the time of preload.  This ensures a reference to
    // to the cache and that all keys will load into this cache object.
    mLoadingCaches.Insert(aCache);
    SendAsyncPreload(aCache->OriginSuffix(), aCache->OriginNoSuffix(),
                     aPriority);
  } else {
    // No IPC, no love.  But the LoadDone call is expected.
    aCache->LoadDone(NS_ERROR_UNEXPECTED);
  }
}

void StorageDBChild::AsyncGetUsage(StorageUsageBridge* aUsage) {
  if (mIPCOpen) {
    SendAsyncGetUsage(aUsage->OriginScope());
  }
}

void StorageDBChild::SyncPreload(LocalStorageCacheBridge* aCache,
                                 bool aForceSync) {
  if (NS_FAILED(mStatus)) {
    aCache->LoadDone(mStatus);
    return;
  }

  if (!mIPCOpen) {
    aCache->LoadDone(NS_ERROR_UNEXPECTED);
    return;
  }

  // There is no way to put the child process to a wait state to receive all
  // incoming async responses from the parent, hence we have to do a sync
  // preload instead.  We are smart though, we only demand keys that are left to
  // load in case the async preload has already loaded some keys.
  nsTArray<nsString> keys, values;
  nsresult rv;
  SendPreload(aCache->OriginSuffix(), aCache->OriginNoSuffix(),
              aCache->LoadedCount(), &keys, &values, &rv);

  for (uint32_t i = 0; i < keys.Length(); ++i) {
    aCache->LoadItem(keys[i], values[i]);
  }

  aCache->LoadDone(rv);
}

nsresult StorageDBChild::AsyncAddItem(LocalStorageCacheBridge* aCache,
                                      const nsAString& aKey,
                                      const nsAString& aValue) {
  if (NS_FAILED(mStatus) || !mIPCOpen) {
    return mStatus;
  }

  SendAsyncAddItem(aCache->OriginSuffix(), aCache->OriginNoSuffix(),
                   nsString(aKey), nsString(aValue));
  OriginsHavingData().Insert(aCache->Origin());
  return NS_OK;
}

nsresult StorageDBChild::AsyncUpdateItem(LocalStorageCacheBridge* aCache,
                                         const nsAString& aKey,
                                         const nsAString& aValue) {
  if (NS_FAILED(mStatus) || !mIPCOpen) {
    return mStatus;
  }

  SendAsyncUpdateItem(aCache->OriginSuffix(), aCache->OriginNoSuffix(),
                      nsString(aKey), nsString(aValue));
  OriginsHavingData().Insert(aCache->Origin());
  return NS_OK;
}

nsresult StorageDBChild::AsyncRemoveItem(LocalStorageCacheBridge* aCache,
                                         const nsAString& aKey) {
  if (NS_FAILED(mStatus) || !mIPCOpen) {
    return mStatus;
  }

  SendAsyncRemoveItem(aCache->OriginSuffix(), aCache->OriginNoSuffix(),
                      nsString(aKey));
  return NS_OK;
}

nsresult StorageDBChild::AsyncClear(LocalStorageCacheBridge* aCache) {
  if (NS_FAILED(mStatus) || !mIPCOpen) {
    return mStatus;
  }

  SendAsyncClear(aCache->OriginSuffix(), aCache->OriginNoSuffix());
  OriginsHavingData().Remove(aCache->Origin());
  return NS_OK;
}

bool StorageDBChild::ShouldPreloadOrigin(const nsACString& aOrigin) {
  // Return true if we didn't receive the origins list yet.
  // I tend to rather preserve a bit of early-after-start performance
  // than a bit of memory here.
  return !mOriginsHavingData || mOriginsHavingData->Contains(aOrigin);
}

mozilla::ipc::IPCResult StorageDBChild::RecvObserve(
    const nsACString& aTopic, const nsAString& aOriginAttributesPattern,
    const nsACString& aOriginScope) {
  MOZ_ASSERT(!XRE_IsParentProcess());

  if (StorageObserver* obs = StorageObserver::Self()) {
    obs->Notify(PromiseFlatCString(aTopic).get(), aOriginAttributesPattern,
                aOriginScope);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBChild::RecvOriginsHavingData(
    nsTArray<nsCString>&& aOrigins) {
  // Force population of mOriginsHavingData even if there are no origins so that
  // ShouldPreloadOrigin does not generate false positives for all origins.
  if (!aOrigins.Length()) {
    (void)OriginsHavingData();
  }

  for (uint32_t i = 0; i < aOrigins.Length(); ++i) {
    OriginsHavingData().Insert(aOrigins[i]);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBChild::RecvLoadItem(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const nsAString& aKey, const nsAString& aValue) {
  LocalStorageCache* aCache =
      mManager->GetCache(aOriginSuffix, aOriginNoSuffix);
  if (aCache) {
    aCache->LoadItem(aKey, aValue);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBChild::RecvLoadDone(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const nsresult& aRv) {
  LocalStorageCache* aCache =
      mManager->GetCache(aOriginSuffix, aOriginNoSuffix);
  if (aCache) {
    aCache->LoadDone(aRv);

    // Just drop reference to this cache now since the load is done.
    mLoadingCaches.Remove(static_cast<LocalStorageCacheBridge*>(aCache));
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBChild::RecvLoadUsage(
    const nsACString& aOriginNoSuffix, const int64_t& aUsage) {
  RefPtr<StorageUsageBridge> scopeUsage =
      mManager->GetOriginUsage(aOriginNoSuffix, mPrivateBrowsingId);
  scopeUsage->LoadUsage(aUsage);
  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBChild::RecvError(const nsresult& aRv) {
  mStatus = aRv;
  return IPC_OK();
}

NS_IMPL_ISUPPORTS(StorageDBChild::ShutdownObserver, nsIObserver)

NS_IMETHODIMP
StorageDBChild::ShutdownObserver::Observe(nsISupports* aSubject,
                                          const char* aTopic,
                                          const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!strcmp(aTopic, "xpcom-shutdown"));

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (NS_WARN_IF(!observerService)) {
    return NS_ERROR_FAILURE;
  }

  (void)observerService->RemoveObserver(this, "xpcom-shutdown");

  StorageDBChild*& storageChild = sStorageChild[mPrivateBrowsingId];
  if (storageChild) {
    sStorageChildDown[mPrivateBrowsingId] = true;

    MOZ_ALWAYS_TRUE(storageChild->PBackgroundStorageChild::SendDeleteMe());

    NS_RELEASE(storageChild);
    storageChild = nullptr;
  }

  return NS_OK;
}

SessionStorageObserverChild::SessionStorageObserverChild(
    SessionStorageObserver* aObserver)
    : mObserver(aObserver) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NextGenLocalStorageEnabled());
  MOZ_ASSERT(aObserver);
  aObserver->AssertIsOnOwningThread();

  MOZ_COUNT_CTOR(SessionStorageObserverChild);
}

SessionStorageObserverChild::~SessionStorageObserverChild() {
  AssertIsOnOwningThread();

  MOZ_COUNT_DTOR(SessionStorageObserverChild);
}

void SessionStorageObserverChild::SendDeleteMeInternal() {
  AssertIsOnOwningThread();

  if (mObserver) {
    mObserver->ClearActor();
    mObserver = nullptr;

    // Don't check result here since IPC may no longer be available due to
    // SessionStorageManager (which holds a strong reference to
    // SessionStorageObserver) being destroyed very late in the game.
    PSessionStorageObserverChild::SendDeleteMe();
  }
}

void SessionStorageObserverChild::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  if (mObserver) {
    mObserver->ClearActor();
    mObserver = nullptr;
  }
}

mozilla::ipc::IPCResult SessionStorageObserverChild::RecvObserve(
    const nsACString& aTopic, const nsAString& aOriginAttributesPattern,
    const nsACString& aOriginScope) {
  AssertIsOnOwningThread();

  if (StorageObserver* obs = StorageObserver::Self()) {
    obs->Notify(PromiseFlatCString(aTopic).get(), aOriginAttributesPattern,
                aOriginScope);
  }

  return IPC_OK();
}

SessionStorageCacheChild::SessionStorageCacheChild(SessionStorageCache* aCache)
    : mCache(aCache) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mCache);

  MOZ_COUNT_CTOR(SessionStorageCacheChild);
}

SessionStorageCacheChild::~SessionStorageCacheChild() {
  AssertIsOnOwningThread();

  MOZ_COUNT_DTOR(SessionStorageCacheChild);
}

void SessionStorageCacheChild::SendDeleteMeInternal() {
  AssertIsOnOwningThread();

  if (mCache) {
    mCache->ClearActor();
    mCache = nullptr;

    MOZ_ALWAYS_TRUE(PBackgroundSessionStorageCacheChild::SendDeleteMe());
  }
}

void SessionStorageCacheChild::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  if (mCache) {
    mCache->ClearActor();
    mCache = nullptr;
  }
}

SessionStorageManagerChild::SessionStorageManagerChild(
    SessionStorageManager* aSSManager)
    : mSSManager(aSSManager) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mSSManager);

  MOZ_COUNT_CTOR(SessionStorageManagerChild);
}

SessionStorageManagerChild::~SessionStorageManagerChild() {
  AssertIsOnOwningThread();

  MOZ_COUNT_DTOR(SessionStorageManagerChild);
}

void SessionStorageManagerChild::SendDeleteMeInternal() {
  AssertIsOnOwningThread();

  if (mSSManager) {
    mSSManager->ClearActor();
    mSSManager = nullptr;

    MOZ_ALWAYS_TRUE(PBackgroundSessionStorageManagerChild::SendDeleteMe());
  }
}

void SessionStorageManagerChild::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  if (mSSManager) {
    mSSManager->ClearActor();
    mSSManager = nullptr;
  }
}

mozilla::ipc::IPCResult SessionStorageManagerChild::RecvClearStoragesForOrigin(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey) {
  AssertIsOnOwningThread();

  if (mSSManager) {
    mSSManager->ClearStoragesForOrigin(aOriginAttrs, aOriginKey);
  }

  return IPC_OK();
}

LocalStorageCacheParent::LocalStorageCacheParent(nsIPrincipal* aPrincipal,
                                                 const nsACString& aOriginKey,
                                                 uint32_t aPrivateBrowsingId)
    : mPrincipal(aPrincipal),
      mOriginKey(aOriginKey),
      mPrivateBrowsingId(aPrivateBrowsingId),
      mActorDestroyed(false) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
}

LocalStorageCacheParent::~LocalStorageCacheParent() {
  MOZ_ASSERT(mActorDestroyed);
}

void LocalStorageCacheParent::ActorDestroy(ActorDestroyReason aWhy) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  mActorDestroyed = true;

  MOZ_ASSERT(gLocalStorageCacheParents);

  nsTArray<LocalStorageCacheParent*>* array;
  gLocalStorageCacheParents->Get(mOriginKey, &array);
  MOZ_ASSERT(array);

  array->RemoveElement(this);

  if (array->IsEmpty()) {
    gLocalStorageCacheParents->Remove(mOriginKey);
  }

  if (!gLocalStorageCacheParents->Count()) {
    gLocalStorageCacheParents = nullptr;
  }
}

mozilla::ipc::IPCResult LocalStorageCacheParent::RecvDeleteMe() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  IProtocol* mgr = Manager();
  if (!PBackgroundLocalStorageCacheParent::Send__delete__(this)) {
    return IPC_FAIL_NO_REASON(mgr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult LocalStorageCacheParent::RecvNotify(
    const nsAString& aDocumentURI, const nsAString& aKey,
    const nsAString& aOldValue, const nsAString& aNewValue) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(gLocalStorageCacheParents);

  nsTArray<LocalStorageCacheParent*>* array;
  gLocalStorageCacheParents->Get(mOriginKey, &array);
  MOZ_ASSERT(array);

  for (LocalStorageCacheParent* localStorageCacheParent : *array) {
    if (localStorageCacheParent != this &&
        StorageUtils::PrincipalsEqual(mPrincipal,
                                      localStorageCacheParent->Principal())) {
      (void)localStorageCacheParent->SendObserve(
          WrapNotNull(mPrincipal.get()), mPrivateBrowsingId, aDocumentURI, aKey,
          aOldValue, aNewValue);
    }
  }

  return IPC_OK();
}

// ----------------------------------------------------------------------------
// Parent
// ----------------------------------------------------------------------------

class StorageDBParent::ObserverSink : public StorageObserverSink {
  nsCOMPtr<nsIEventTarget> mOwningEventTarget;

  // Only touched on the PBackground thread.
  StorageDBParent* MOZ_NON_OWNING_REF mActor;

 public:
  explicit ObserverSink(StorageDBParent* aActor)
      : mOwningEventTarget(GetCurrentSerialEventTarget()), mActor(aActor) {
    ::mozilla::ipc::AssertIsOnBackgroundThread();
    MOZ_ASSERT(aActor);
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(StorageDBParent::ObserverSink);

  void Start();

  void Stop();

 private:
  ~ObserverSink() = default;

  void AddSink();

  void RemoveSink();

  void Notify(const nsACString& aTopic,
              const nsAString& aOriginAttributesPattern,
              const nsACString& aOriginScope);

  // StorageObserverSink
  nsresult Observe(const char* aTopic, const nsAString& aOriginAttrPattern,
                   const nsACString& aOriginScope) override;
};

NS_IMPL_ADDREF(StorageDBParent)
NS_IMPL_RELEASE(StorageDBParent)

void StorageDBParent::AddIPDLReference() {
  MOZ_ASSERT(!mIPCOpen, "Attempting to retain multiple IPDL references");
  mIPCOpen = true;
  AddRef();
}

void StorageDBParent::ReleaseIPDLReference() {
  MOZ_ASSERT(mIPCOpen, "Attempting to release non-existent IPDL reference");
  mIPCOpen = false;
  Release();
}

namespace {}  // namespace

StorageDBParent::StorageDBParent(const nsAString& aProfilePath,
                                 const uint32_t aPrivateBrowsingId)
    : mProfilePath(aProfilePath),
      mPrivateBrowsingId(aPrivateBrowsingId),
      mIPCOpen(false) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  // We are always open by IPC only
  AddIPDLReference();
}

StorageDBParent::~StorageDBParent() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (mObserverSink) {
    mObserverSink->Stop();
    mObserverSink = nullptr;
  }
}

void StorageDBParent::Init() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  PBackgroundParent* actor = Manager();
  MOZ_ASSERT(actor);

  if (::mozilla::ipc::BackgroundParent::IsOtherProcessActor(actor)) {
    mObserverSink = new ObserverSink(this);
    mObserverSink->Start();
    // Content processes are not trusted to supply a valid profile path; clear
    // it so that StorageDBThread::Init derives the correct path itself.
    mProfilePath.Truncate();
  }

  StorageDBThread* storageThread = StorageDBThread::Get(mPrivateBrowsingId);
  if (storageThread) {
    nsTArray<nsCString> scopes;
    storageThread->GetOriginsHavingData(&scopes);
    (void)SendOriginsHavingData(scopes);
  }
}

StorageDBParent::CacheParentBridge* StorageDBParent::NewCache(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix) {
  return new CacheParentBridge(this, aOriginSuffix, aOriginNoSuffix);
}

void StorageDBParent::ActorDestroy(ActorDestroyReason aWhy) {
  // Implement me! Bug 1005169
}

mozilla::ipc::IPCResult StorageDBParent::RecvDeleteMe() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  IProtocol* mgr = Manager();
  if (!PBackgroundStorageParent::Send__delete__(this)) {
    return IPC_FAIL_NO_REASON(mgr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvAsyncPreload(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const bool& aPriority) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  storageThread->AsyncPreload(NewCache(aOriginSuffix, aOriginNoSuffix),
                              aPriority);

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvAsyncGetUsage(
    const nsACString& aOriginNoSuffix) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  // The object releases it self in LoadUsage method
  RefPtr<UsageParentBridge> usage =
      new UsageParentBridge(this, aOriginNoSuffix);

  storageThread->AsyncGetUsage(usage);

  return IPC_OK();
}

namespace {

// We need another implementation of LocalStorageCacheBridge to do
// synchronous IPC preload.  This class just receives Load* notifications
// and fills the returning arguments of RecvPreload with the database
// values for us.
class SyncLoadCacheHelper : public LocalStorageCacheBridge {
 public:
  SyncLoadCacheHelper(const nsACString& aOriginSuffix,
                      const nsACString& aOriginNoSuffix,
                      uint32_t aAlreadyLoadedCount, nsTArray<nsString>* aKeys,
                      nsTArray<nsString>* aValues, nsresult* rv)
      : mMonitor("DOM Storage SyncLoad IPC"),
        mSuffix(aOriginSuffix),
        mOrigin(aOriginNoSuffix),
        mKeys(aKeys),
        mValues(aValues),
        mRv(rv),
        mLoaded(false),
        mLoadedCount(aAlreadyLoadedCount) {
    // Precaution
    *mRv = NS_ERROR_UNEXPECTED;
  }

  virtual nsCString Origin() const override {
    return LocalStorageManager::CreateOrigin(mSuffix, mOrigin);
  }
  virtual const nsCString& OriginNoSuffix() const override { return mOrigin; }
  virtual const nsCString& OriginSuffix() const override { return mSuffix; }
  virtual bool Loaded() override { return mLoaded; }
  virtual uint32_t LoadedCount() override { return mLoadedCount; }
  virtual bool LoadItem(const nsAString& aKey,
                        const nsAString& aValue) override {
    // Called on the aCache background thread
    MOZ_ASSERT(!mLoaded);
    if (mLoaded) {
      return false;
    }

    ++mLoadedCount;
    mKeys->AppendElement(aKey);
    mValues->AppendElement(aValue);
    return true;
  }

  virtual void LoadDone(nsresult aRv) override {
    // Called on the aCache background thread
    MonitorAutoLock monitor(mMonitor);
    MOZ_ASSERT(!mLoaded && mRv);
    mLoaded = true;
    if (mRv) {
      *mRv = aRv;
      mRv = nullptr;
    }
    monitor.Notify();
  }

  virtual void LoadWait() override {
    // Called on the main thread, exits after LoadDone() call
    MonitorAutoLock monitor(mMonitor);
    while (!mLoaded) {
      monitor.Wait();
    }
  }

 private:
  Monitor mMonitor MOZ_UNANNOTATED;
  nsCString mSuffix, mOrigin;
  nsTArray<nsString>* mKeys;
  nsTArray<nsString>* mValues;
  nsresult* mRv;
  bool mLoaded;
  uint32_t mLoadedCount;
};

}  // namespace

mozilla::ipc::IPCResult StorageDBParent::RecvPreload(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const uint32_t& aAlreadyLoadedCount, nsTArray<nsString>* aKeys,
    nsTArray<nsString>* aValues, nsresult* aRv) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  RefPtr<SyncLoadCacheHelper> cache(
      new SyncLoadCacheHelper(aOriginSuffix, aOriginNoSuffix,
                              aAlreadyLoadedCount, aKeys, aValues, aRv));

  storageThread->SyncPreload(cache, true);

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvAsyncAddItem(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const nsAString& aKey, const nsAString& aValue) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  nsresult rv = storageThread->AsyncAddItem(
      NewCache(aOriginSuffix, aOriginNoSuffix), aKey, aValue);
  if (NS_FAILED(rv) && mIPCOpen) {
    (void)SendError(rv);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvAsyncUpdateItem(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const nsAString& aKey, const nsAString& aValue) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  nsresult rv = storageThread->AsyncUpdateItem(
      NewCache(aOriginSuffix, aOriginNoSuffix), aKey, aValue);
  if (NS_FAILED(rv) && mIPCOpen) {
    (void)SendError(rv);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvAsyncRemoveItem(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const nsAString& aKey) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  nsresult rv = storageThread->AsyncRemoveItem(
      NewCache(aOriginSuffix, aOriginNoSuffix), aKey);
  if (NS_FAILED(rv) && mIPCOpen) {
    (void)SendError(rv);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvAsyncClear(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  nsresult rv =
      storageThread->AsyncClear(NewCache(aOriginSuffix, aOriginNoSuffix));
  if (NS_FAILED(rv) && mIPCOpen) {
    (void)SendError(rv);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvAsyncFlush() {
  StorageDBThread* storageThread = StorageDBThread::Get(mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  storageThread->AsyncFlush();

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvStartup() {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvClearAll() {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  storageThread->AsyncClearAll();

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvClearMatchingOrigin(
    const nsACString& aOriginNoSuffix) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  storageThread->AsyncClearMatchingOrigin(aOriginNoSuffix);

  return IPC_OK();
}

mozilla::ipc::IPCResult StorageDBParent::RecvClearMatchingOriginAttributes(
    const OriginAttributesPattern& aPattern) {
  StorageDBThread* storageThread =
      StorageDBThread::GetOrCreate(mProfilePath, mPrivateBrowsingId);
  if (!storageThread) {
    return IPC_FAIL_NO_REASON(this);
  }

  storageThread->AsyncClearMatchingOriginAttributes(aPattern);

  return IPC_OK();
}

void StorageDBParent::Observe(const nsACString& aTopic,
                              const nsAString& aOriginAttributesPattern,
                              const nsACString& aOriginScope) {
  if (mIPCOpen) {
    (void)SendObserve(aTopic, aOriginAttributesPattern, aOriginScope);
  }
}

namespace {

// Results must be sent back on the main thread
class LoadRunnable : public Runnable {
 public:
  enum TaskType { loadItem, loadDone };

  LoadRunnable(StorageDBParent* aParent, TaskType aType,
               const nsACString& aOriginSuffix,
               const nsACString& aOriginNoSuffix,
               const nsAString& aKey = u""_ns, const nsAString& aValue = u""_ns)
      : Runnable("dom::LoadRunnable"),
        mParent(aParent),
        mType(aType),
        mSuffix(aOriginSuffix),
        mOrigin(aOriginNoSuffix),
        mKey(aKey),
        mValue(aValue),
        mRv(NS_ERROR_NOT_INITIALIZED) {}

  LoadRunnable(StorageDBParent* aParent, TaskType aType,
               const nsACString& aOriginSuffix,
               const nsACString& aOriginNoSuffix, nsresult aRv)
      : Runnable("dom::LoadRunnable"),
        mParent(aParent),
        mType(aType),
        mSuffix(aOriginSuffix),
        mOrigin(aOriginNoSuffix),
        mRv(aRv) {}

 private:
  RefPtr<StorageDBParent> mParent;
  TaskType mType;
  nsCString mSuffix, mOrigin;
  nsString mKey;
  nsString mValue;
  nsresult mRv;

  NS_IMETHOD Run() override {
    if (!mParent->IPCOpen()) {
      return NS_OK;
    }

    switch (mType) {
      case loadItem:
        (void)mParent->SendLoadItem(mSuffix, mOrigin, mKey, mValue);
        break;
      case loadDone:
        (void)mParent->SendLoadDone(mSuffix, mOrigin, mRv);
        break;
    }

    mParent = nullptr;

    return NS_OK;
  }
};

}  // namespace

// StorageDBParent::CacheParentBridge

nsCString StorageDBParent::CacheParentBridge::Origin() const {
  return LocalStorageManager::CreateOrigin(mOriginSuffix, mOriginNoSuffix);
}

bool StorageDBParent::CacheParentBridge::LoadItem(const nsAString& aKey,
                                                  const nsAString& aValue) {
  if (mLoaded) {
    return false;
  }

  ++mLoadedCount;

  RefPtr<LoadRunnable> r =
      new LoadRunnable(mParent, LoadRunnable::loadItem, mOriginSuffix,
                       mOriginNoSuffix, aKey, aValue);

  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(r, NS_DISPATCH_NORMAL));

  return true;
}

void StorageDBParent::CacheParentBridge::LoadDone(nsresult aRv) {
  // Prevent send of duplicate LoadDone.
  if (mLoaded) {
    return;
  }

  mLoaded = true;

  RefPtr<LoadRunnable> r = new LoadRunnable(
      mParent, LoadRunnable::loadDone, mOriginSuffix, mOriginNoSuffix, aRv);

  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(r, NS_DISPATCH_NORMAL));
}

void StorageDBParent::CacheParentBridge::LoadWait() {
  // Should never be called on this implementation
  MOZ_ASSERT(false);
}

// XXX Fix me!
// This should be just:
// NS_IMPL_RELEASE_WITH_DESTROY(StorageDBParent::CacheParentBridge, Destroy)
// But due to different strings used for refcount logging and different return
// types, this is done manually for now.
NS_IMETHODIMP_(void)
StorageDBParent::CacheParentBridge::Release(void) {
  MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");
  nsrefcnt count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "LocalStorageCacheBridge");
  if (0 == count) {
    mRefCnt = 1; /* stabilize */
    /* enable this to find non-threadsafe destructors: */
    /* NS_ASSERT_OWNINGTHREAD(_class); */
    Destroy();
  }
}

void StorageDBParent::CacheParentBridge::Destroy() {
  if (mOwningEventTarget->IsOnCurrentThread()) {
    delete this;
    return;
  }

  RefPtr<Runnable> destroyRunnable = NewNonOwningRunnableMethod(
      "CacheParentBridge::Destroy", this, &CacheParentBridge::Destroy);

  MOZ_ALWAYS_SUCCEEDS(
      mOwningEventTarget->Dispatch(destroyRunnable, NS_DISPATCH_NORMAL));
}

// StorageDBParent::UsageParentBridge

namespace {

class UsageRunnable : public Runnable {
 public:
  UsageRunnable(StorageDBParent* aParent, const nsACString& aOriginScope,
                const int64_t& aUsage)
      : Runnable("dom::UsageRunnable"),
        mParent(aParent),
        mOriginScope(aOriginScope),
        mUsage(aUsage) {}

 private:
  NS_IMETHOD Run() override {
    if (!mParent->IPCOpen()) {
      return NS_OK;
    }

    (void)mParent->SendLoadUsage(mOriginScope, mUsage);

    mParent = nullptr;

    return NS_OK;
  }

  RefPtr<StorageDBParent> mParent;
  nsCString mOriginScope;
  int64_t mUsage;
};

}  // namespace

void StorageDBParent::UsageParentBridge::LoadUsage(const int64_t aUsage) {
  RefPtr<UsageRunnable> r = new UsageRunnable(mParent, mOriginScope, aUsage);

  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(r, NS_DISPATCH_NORMAL));
}

// XXX Fix me!
// This should be just:
// NS_IMPL_RELEASE_WITH_DESTROY(StorageDBParent::UsageParentBridge, Destroy)
// But due to different strings used for refcount logging, this is done manually
// for now.
NS_IMETHODIMP_(MozExternalRefCountType)
StorageDBParent::UsageParentBridge::Release(void) {
  MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");
  nsrefcnt count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "StorageUsageBridge");
  if (count == 0) {
    Destroy();
    return 0;
  }
  return count;
}

void StorageDBParent::UsageParentBridge::Destroy() {
  if (mOwningEventTarget->IsOnCurrentThread()) {
    delete this;
    return;
  }

  RefPtr<Runnable> destroyRunnable = NewNonOwningRunnableMethod(
      "UsageParentBridge::Destroy", this, &UsageParentBridge::Destroy);

  MOZ_ALWAYS_SUCCEEDS(
      mOwningEventTarget->Dispatch(destroyRunnable, NS_DISPATCH_NORMAL));
}

void StorageDBParent::ObserverSink::Start() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  RefPtr<Runnable> runnable =
      NewRunnableMethod("StorageDBParent::ObserverSink::AddSink", this,
                        &StorageDBParent::ObserverSink::AddSink);

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(runnable));
}

void StorageDBParent::ObserverSink::Stop() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  mActor = nullptr;

  RefPtr<Runnable> runnable =
      NewRunnableMethod("StorageDBParent::ObserverSink::RemoveSink", this,
                        &StorageDBParent::ObserverSink::RemoveSink);

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(runnable));
}

void StorageDBParent::ObserverSink::AddSink() {
  MOZ_ASSERT(NS_IsMainThread());

  StorageObserver* observer = StorageObserver::Self();
  if (observer) {
    observer->AddSink(this);
  }
}

void StorageDBParent::ObserverSink::RemoveSink() {
  MOZ_ASSERT(NS_IsMainThread());

  StorageObserver* observer = StorageObserver::Self();
  if (observer) {
    observer->RemoveSink(this);
  }
}

void StorageDBParent::ObserverSink::Notify(
    const nsACString& aTopic, const nsAString& aOriginAttributesPattern,
    const nsACString& aOriginScope) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (mActor) {
    mActor->Observe(aTopic, aOriginAttributesPattern, aOriginScope);
  }
}

nsresult StorageDBParent::ObserverSink::Observe(
    const char* aTopic, const nsAString& aOriginAttributesPattern,
    const nsACString& aOriginScope) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<Runnable> runnable = NewRunnableMethod<nsCString, nsString, nsCString>(
      "StorageDBParent::ObserverSink::Observe2", this,
      &StorageDBParent::ObserverSink::Notify, aTopic, aOriginAttributesPattern,
      aOriginScope);

  MOZ_ALWAYS_SUCCEEDS(
      mOwningEventTarget->Dispatch(runnable, NS_DISPATCH_NORMAL));

  return NS_OK;
}

SessionStorageObserverParent::SessionStorageObserverParent()
    : mActorDestroyed(false) {
  MOZ_ASSERT(NS_IsMainThread());

  StorageObserver* observer = StorageObserver::Self();
  if (observer) {
    observer->AddSink(this);
  }
}

SessionStorageObserverParent::~SessionStorageObserverParent() {
  MOZ_ASSERT(mActorDestroyed);

  StorageObserver* observer = StorageObserver::Self();
  if (observer) {
    observer->RemoveSink(this);
  }
}

void SessionStorageObserverParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mActorDestroyed);

  mActorDestroyed = true;
}

mozilla::ipc::IPCResult SessionStorageObserverParent::RecvDeleteMe() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mActorDestroyed);

  IProtocol* mgr = Manager();
  if (!PSessionStorageObserverParent::Send__delete__(this)) {
    return IPC_FAIL_NO_REASON(mgr);
  }
  return IPC_OK();
}

nsresult SessionStorageObserverParent::Observe(
    const char* aTopic, const nsAString& aOriginAttributesPattern,
    const nsACString& aOriginScope) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mActorDestroyed) {
    (void)SendObserve(nsDependentCString(aTopic), aOriginAttributesPattern,
                      aOriginScope);
  }
  return NS_OK;
}

SessionStorageCacheParent::SessionStorageCacheParent(
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
    const nsACString& aOriginKey, SessionStorageManagerParent* aActor)
    : mPrincipalInfo(aPrincipalInfo),
      mOriginKey(aOriginKey),
      mManagerActor(aActor) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mManagerActor);
}

SessionStorageCacheParent::~SessionStorageCacheParent() = default;

void SessionStorageCacheParent::ActorDestroy(ActorDestroyReason aWhy) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  mManagerActor = nullptr;
}

mozilla::ipc::IPCResult SessionStorageCacheParent::RecvLoad(
    nsTArray<SSSetItemInfo>* aData) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mManagerActor);

  mLoadReceived.Flip();

  RefPtr<BackgroundSessionStorageManager> manager = mManagerActor->GetManager();
  MOZ_ASSERT(manager);

  OriginAttributes attrs;
  MOZ_ALWAYS_TRUE(
      StoragePrincipalHelper::GetOriginAttributes(mPrincipalInfo, attrs));

  nsAutoCString originAttrs;
  attrs.CreateSuffix(originAttrs);

  manager->CopyDataToContentProcess(originAttrs, mOriginKey, *aData);

  return IPC_OK();
}

mozilla::ipc::IPCResult SessionStorageCacheParent::RecvCheckpoint(
    nsTArray<SSWriteInfo>&& aWriteInfos) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mManagerActor);

  RefPtr<BackgroundSessionStorageManager> manager = mManagerActor->GetManager();
  MOZ_ASSERT(manager);

  OriginAttributes attrs;
  StoragePrincipalHelper::GetOriginAttributes(mPrincipalInfo, attrs);

  nsAutoCString originAttrs;
  attrs.CreateSuffix(originAttrs);

  manager->UpdateData(originAttrs, mOriginKey, aWriteInfos);

  return IPC_OK();
}

mozilla::ipc::IPCResult SessionStorageCacheParent::RecvDeleteMe() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mManagerActor);

  mManagerActor = nullptr;

  IProtocol* mgr = Manager();
  if (!PBackgroundSessionStorageCacheParent::Send__delete__(this)) {
    return IPC_FAIL(
        mgr, "Failed to delete PBackgroundSessionStorageCacheParent actor");
  }
  return IPC_OK();
}

SessionStorageManagerParent::SessionStorageManagerParent(uint64_t aTopContextId)
    : mBackgroundManager(
          BackgroundSessionStorageManager::GetOrCreate(aTopContextId)) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mBackgroundManager);
  mBackgroundManager->AddParticipatingActor(this);
}

SessionStorageManagerParent::~SessionStorageManagerParent() = default;

void SessionStorageManagerParent::ActorDestroy(ActorDestroyReason aWhy) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (mBackgroundManager) {
    mBackgroundManager->RemoveParticipatingActor(this);
  }

  mBackgroundManager = nullptr;
}

already_AddRefed<PBackgroundSessionStorageCacheParent>
SessionStorageManagerParent::AllocPBackgroundSessionStorageCacheParent(
    const PrincipalInfo& aPrincipalInfo, const nsACString& aOriginKey) {
  return MakeAndAddRef<SessionStorageCacheParent>(aPrincipalInfo, aOriginKey,
                                                  this);
}

BackgroundSessionStorageManager* SessionStorageManagerParent::GetManager()
    const {
  return mBackgroundManager;
}

mozilla::ipc::IPCResult SessionStorageManagerParent::RecvClearStorages(
    const OriginAttributesPattern& aPattern, const nsACString& aOriginScope,
    const DomainMatchingMode& aMode) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  mBackgroundManager->ClearStorages(aPattern, aOriginScope, aMode);
  return IPC_OK();
}

mozilla::ipc::IPCResult SessionStorageManagerParent::RecvDeleteMe() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mBackgroundManager);

  mBackgroundManager->RemoveParticipatingActor(this);

  mBackgroundManager = nullptr;

  IProtocol* mgr = Manager();
  if (!PBackgroundSessionStorageManagerParent::Send__delete__(this)) {
    return IPC_FAIL(
        mgr, "Failed to delete PBackgroundSessionStorageManagerParent actor");
  }
  return IPC_OK();
}

/*******************************************************************************
 * Exported functions
 ******************************************************************************/

PBackgroundLocalStorageCacheParent* AllocPBackgroundLocalStorageCacheParent(
    nsIPrincipal* aPrincipal, const nsACString& aOriginKey,
    const uint32_t& aPrivateBrowsingId) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  RefPtr<LocalStorageCacheParent> actor =
      new LocalStorageCacheParent(aPrincipal, aOriginKey, aPrivateBrowsingId);

  // Transfer ownership to IPDL.
  return actor.forget().take();
}

mozilla::ipc::IPCResult RecvPBackgroundLocalStorageCacheConstructor(
    mozilla::ipc::PBackgroundParent* aBackgroundActor,
    PBackgroundLocalStorageCacheParent* aActor, const nsACString& aOriginKey,
    const uint32_t& aPrivateBrowsingId) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  auto* actor = static_cast<LocalStorageCacheParent*>(aActor);

  if (!gLocalStorageCacheParents) {
    gLocalStorageCacheParents = new LocalStorageCacheParentHashtable();
  }

  gLocalStorageCacheParents->GetOrInsertNew(aOriginKey)->AppendElement(actor);

  // We are currently trusting the content process not to lie to us.  It is
  // future work to consult the ClientManager to determine whether this is a
  // legitimate origin for the content process.

  return IPC_OK();
}

bool DeallocPBackgroundLocalStorageCacheParent(
    PBackgroundLocalStorageCacheParent* aActor) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  // Transfer ownership back from IPDL.
  RefPtr<LocalStorageCacheParent> actor =
      dont_AddRef(static_cast<LocalStorageCacheParent*>(aActor));

  return true;
}

PBackgroundStorageParent* AllocPBackgroundStorageParent(
    const nsAString& aProfilePath, const uint32_t& aPrivateBrowsingId) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (NS_WARN_IF(NextGenLocalStorageEnabled()) ||
      NS_WARN_IF(aPrivateBrowsingId >= kPrivateBrowsingIdCount)) {
    return nullptr;
  }

  return new StorageDBParent(aProfilePath, aPrivateBrowsingId);
}

mozilla::ipc::IPCResult RecvPBackgroundStorageConstructor(
    PBackgroundStorageParent* aActor, const nsAString& aProfilePath,
    const uint32_t& aPrivateBrowsingId) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aPrivateBrowsingId < kPrivateBrowsingIdCount);
  MOZ_ASSERT(!NextGenLocalStorageEnabled());

  auto* actor = static_cast<StorageDBParent*>(aActor);
  actor->Init();
  return IPC_OK();
}

bool DeallocPBackgroundStorageParent(PBackgroundStorageParent* aActor) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  StorageDBParent* actor = static_cast<StorageDBParent*>(aActor);
  actor->ReleaseIPDLReference();
  return true;
}

PSessionStorageObserverParent* AllocPSessionStorageObserverParent() {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<SessionStorageObserverParent> actor =
      new SessionStorageObserverParent();

  // Transfer ownership to IPDL.
  return actor.forget().take();
}

bool RecvPSessionStorageObserverConstructor(
    PSessionStorageObserverParent* aActor) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aActor);

  return true;
}

bool DeallocPSessionStorageObserverParent(
    PSessionStorageObserverParent* aActor) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aActor);

  // Transfer ownership back from IPDL.
  RefPtr<SessionStorageObserverParent> actor =
      dont_AddRef(static_cast<SessionStorageObserverParent*>(aActor));

  return true;
}

already_AddRefed<PBackgroundSessionStorageManagerParent>
AllocPBackgroundSessionStorageManagerParent(const uint64_t& aTopContextId) {
  return MakeAndAddRef<SessionStorageManagerParent>(aTopContextId);
}

}  // namespace mozilla::dom
