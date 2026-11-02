/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SessionStorageManager.h"

#include "SessionStorage.h"
#include "SessionStorageCache.h"
#include "SessionStorageObserver.h"
#include "StorageIPC.h"
#include "StorageUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/PrincipalHashKey.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/LocalStorageCommon.h"
#include "mozilla/dom/PBackgroundSessionStorageCache.h"
#include "mozilla/dom/PBackgroundSessionStorageManager.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsIXULRuntime.h"
#include "nsTHashMap.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

using namespace StorageUtils;

static bool ExactDomainMatch(const nsACString& aOriginKey,
                             const nsACString& aOriginScope) {
  // aOriginScope is reversed domain with trailing dot.
  // e.g: example.com => moc.elpmaxe. (see StorageUtils.cpp)
  // aOriginKey is reversed domain with trailing dot, plus ":",
  // scheme, and optional port. e.g: moc.elpmaxe.:http:80 (see
  // https://searchfox.org/firefox-main/rev/987f566373ea82403c5c1235b219bd9e7d56a4aa/caps/BasePrincipal.cpp#1539)
  // To check if it is an exact match, we need to ensure that aOriginKey starts
  // with aOriginScope and the first character immediately following the
  // matching part is ":". i.e: Domain part of aOriginKey is identical to
  // aOriginScope.
  return StringBeginsWith(aOriginKey, aOriginScope) &&
         aOriginKey.CharAt(aOriginScope.Length()) == ':';
}

// Parent process, background thread hashmap that stores top context id and
// manager pair.
static StaticAutoPtr<
    nsRefPtrHashtable<nsUint64HashKey, BackgroundSessionStorageManager>>
    sManagers;

bool RecvShutdownBackgroundSessionStorageManagers() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  sManagers = nullptr;
  return true;
}

void RecvPropagateBackgroundSessionStorageManager(
    uint64_t aCurrentTopContextId, uint64_t aTargetTopContextId) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (sManagers) {
    if (RefPtr<BackgroundSessionStorageManager> mgr =
            sManagers->Get(aCurrentTopContextId)) {
      mgr->MaybeDispatchSessionStoreUpdate();
      mgr->SetCurrentBrowsingContextId(aTargetTopContextId);
      // Because of bfcache, we may re-register aTargetTopContextId in
      // CanonicalBrowsingContext::ReplacedBy.
      // XXXBFCache do we want to tweak this behavior and ensure this is
      // called only once?
      sManagers->InsertOrUpdate(aTargetTopContextId, std::move(mgr));
    }
  }
}

bool RecvRemoveBackgroundSessionStorageManager(uint64_t aTopContextId) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (sManagers) {
    RefPtr<BackgroundSessionStorageManager> mgr;
    sManagers->Remove(aTopContextId, getter_AddRefs(mgr));

    if (mgr) {
      mgr->CancelSessionStoreUpdate();
    }
  }

  return true;
}

bool RecvLoadSessionStorageData(
    uint64_t aTopContextId,
    nsTArray<mozilla::dom::SSCacheCopy>&& aCacheCopyList) {
  if (aCacheCopyList.IsEmpty()) {
    return true;
  }

  RefPtr<BackgroundSessionStorageManager> manager =
      BackgroundSessionStorageManager::GetOrCreate(aTopContextId);

  if (!manager) {
    return true;
  }

  for (const auto& cacheInit : aCacheCopyList) {
    OriginAttributes attrs;
    StoragePrincipalHelper::GetOriginAttributes(cacheInit.principalInfo(),
                                                attrs);

    nsAutoCString originAttrs;
    attrs.CreateSuffix(originAttrs);

    manager->UpdateData(originAttrs, cacheInit.originKey(), cacheInit.data());
  }

  return true;
}

bool RecvGetSessionStorageData(
    uint64_t aTopContextId, uint32_t aSizeLimit, bool aCancelSessionStoreTimer,
    ::mozilla::ipc::PBackgroundParent::GetSessionStorageManagerDataResolver&&
        aResolver) {
  nsTArray<mozilla::dom::SSCacheCopy> data;
  auto resolve = MakeScopeExit([&]() { aResolver(std::move(data)); });

  if (!sManagers) {
    return true;
  }

  RefPtr<BackgroundSessionStorageManager> manager =
      sManagers->Get(aTopContextId);
  if (!manager) {
    return true;
  }

  if (aCancelSessionStoreTimer) {
    manager->CancelSessionStoreUpdate();
  }

  manager->GetData(aSizeLimit, data);

  return true;
}

bool RecvClearStoragesForOrigin(const nsACString& aOriginAttrs,
                                const nsACString& aOriginKey) {
  mozilla::ipc::AssertIsInMainProcess();
  mozilla::ipc::AssertIsOnBackgroundThread();

  if (!sManagers) {
    return true;
  }

  for (auto& entry : *sManagers) {
    entry.GetData()->ClearStoragesForOrigin(aOriginAttrs, aOriginKey);
  }

  return true;
}

void SessionStorageManagerBase::ClearStoragesInternal(
    const OriginAttributesPattern& aPattern, const nsACString& aOriginScope,
    DomainMatchingMode aMode) {
  const bool isExactMatch = aMode == DomainMatchingMode::EXACT_MATCH;

  for (const auto& oaEntry : mOATable) {
    OriginAttributes oa;
    DebugOnly<bool> ok = oa.PopulateFromSuffix(oaEntry.GetKey());
    MOZ_ASSERT(ok);
    if (!aPattern.Matches(oa)) {
      // This table doesn't match the given origin attributes pattern
      continue;
    }

    OriginKeyHashTable* table = oaEntry.GetWeak();
    for (const auto& originKeyEntry : *table) {
      if (aOriginScope.IsEmpty() ||
          (!isExactMatch &&
           StringBeginsWith(originKeyEntry.GetKey(), aOriginScope)) ||
          (isExactMatch &&
           ExactDomainMatch(originKeyEntry.GetKey(), aOriginScope))) {
        const auto cache = originKeyEntry.GetData()->mCache;
        cache->Clear(false);
        cache->ResetWriteInfos();
      }
    }
  }
}

void SessionStorageManagerBase::ClearStoragesForOriginInternal(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey) {
  for (const auto& oaEntry : mOATable) {
    // Filter tables which match the given origin attrs.
    if (oaEntry.GetKey() != aOriginAttrs) {
      continue;
    }

    OriginKeyHashTable* table = oaEntry.GetWeak();
    for (const auto& originKeyEntry : *table) {
      // Match exact origin (without origin attrs).
      if (originKeyEntry.GetKey() != aOriginKey) {
        continue;
      }

      const auto cache = originKeyEntry.GetData()->mCache;
      cache->Clear(false);
      cache->ResetWriteInfos();
    }
  }
}

SessionStorageManagerBase::OriginRecord*
SessionStorageManagerBase::GetOriginRecord(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey,
    const bool aMakeIfNeeded, SessionStorageCache* const aCloneFrom) {
  // XXX It seems aMakeIfNeeded is always known at compile-time, so this could
  // be split into two functions.

  if (aMakeIfNeeded) {
    return mOATable.GetOrInsertNew(aOriginAttrs)
        ->LookupOrInsertWith(
            aOriginKey,
            [&] {
              auto newOriginRecord = MakeUnique<OriginRecord>();
              if (aCloneFrom) {
                newOriginRecord->mCache = aCloneFrom->Clone();
              } else {
                newOriginRecord->mCache = new SessionStorageCache();
              }
              return newOriginRecord;
            })
        .get();
  }

  auto* const table = mOATable.Get(aOriginAttrs);
  if (!table) return nullptr;

  return table->Get(aOriginKey);
}

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(SessionStorageManager)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsIDOMStorageManager)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSessionStorageManager)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(SessionStorageManager, mBrowsingContext)
NS_IMPL_CYCLE_COLLECTING_ADDREF(SessionStorageManager)
NS_IMPL_CYCLE_COLLECTING_RELEASE(SessionStorageManager)

SessionStorageManager::SessionStorageManager(
    RefPtr<BrowsingContext> aBrowsingContext)
    : mBrowsingContext(std::move(aBrowsingContext)), mActor(nullptr) {
  AssertIsOnMainThread();

  StorageObserver* observer = StorageObserver::Self();
  NS_ASSERTION(
      observer,
      "No StorageObserver, cannot observe private data delete notifications!");

  if (observer) {
    observer->AddSink(this);
  }

  if (!XRE_IsParentProcess() && NextGenLocalStorageEnabled()) {
    // When LSNG is enabled the thread IPC bridge doesn't exist, so we have to
    // create own protocol to distribute chrome observer notifications to
    // content processes.
    mObserver = SessionStorageObserver::Get();

    if (!mObserver) {
      ContentChild* contentActor = ContentChild::GetSingleton();
      MOZ_ASSERT(contentActor);

      RefPtr<SessionStorageObserver> observer = new SessionStorageObserver();

      SessionStorageObserverChild* actor =
          new SessionStorageObserverChild(observer);

      MOZ_ALWAYS_TRUE(
          contentActor->SendPSessionStorageObserverConstructor(actor));

      observer->SetActor(actor);

      mObserver = std::move(observer);
    }
  }
}

SessionStorageManager::~SessionStorageManager() {
  StorageObserver* observer = StorageObserver::Self();
  if (observer) {
    observer->RemoveSink(this);
  }

  if (mActor) {
    mActor->SendDeleteMeInternal();
    MOZ_ASSERT(!mActor, "SendDeleteMeInternal should have cleared!");
  }
}

bool SessionStorageManager::CanLoadData() {
  AssertIsOnMainThread();

  return mBrowsingContext && !mBrowsingContext->IsDiscarded();
}

void SessionStorageManager::SetActor(SessionStorageManagerChild* aActor) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(!mActor);

  mActor = aActor;
}

bool SessionStorageManager::ActorExists() const {
  AssertIsOnMainThread();

  return mActor;
}

void SessionStorageManager::ClearActor() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mActor);

  mActor = nullptr;
}

nsresult SessionStorageManager::EnsureManager() {
  AssertIsOnMainThread();
  MOZ_ASSERT(CanLoadData());

  if (ActorExists()) {
    return NS_OK;
  }

  ::mozilla::ipc::PBackgroundChild* backgroundActor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!backgroundActor)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<SessionStorageManagerChild> actor =
      new SessionStorageManagerChild(this);

  if (!backgroundActor->SendPBackgroundSessionStorageManagerConstructor(
          actor, mBrowsingContext->Top()->Id())) {
    return NS_ERROR_FAILURE;
  }

  SetActor(actor);

  return NS_OK;
}

SessionStorageCacheChild* SessionStorageManager::EnsureCache(
    nsIPrincipal& aPrincipal, const nsACString& aOriginKey,
    SessionStorageCache& aCache) {
  AssertIsOnMainThread();
  MOZ_ASSERT(CanLoadData());
  MOZ_ASSERT(ActorExists());

  if (aCache.Actor()) {
    return aCache.Actor();
  }

  mozilla::ipc::PrincipalInfo info;
  nsresult rv = PrincipalToPrincipalInfo(&aPrincipal, &info);

  if (NS_FAILED(rv)) {
    return nullptr;
  }

  RefPtr<SessionStorageCacheChild> actor =
      new SessionStorageCacheChild(&aCache);
  if (!mActor->SendPBackgroundSessionStorageCacheConstructor(actor, info,
                                                             aOriginKey)) {
    return nullptr;
  }

  aCache.SetActor(actor);

  return actor;
}

nsresult SessionStorageManager::LoadData(nsIPrincipal& aPrincipal,
                                         SessionStorageCache& aCache) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mActor);

  nsAutoCString originKey;
  nsresult rv = aPrincipal.GetStorageOriginKey(originKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsAutoCString originAttributes;
  aPrincipal.OriginAttributesRef().CreateSuffix(originAttributes);

  auto* const originRecord =
      GetOriginRecord(originAttributes, originKey, true, nullptr);
  MOZ_ASSERT(originRecord);

  if (originRecord->mLoaded) {
    return NS_OK;
  }

  RefPtr<SessionStorageCacheChild> cacheActor =
      EnsureCache(aPrincipal, originKey, aCache);

  if (!cacheActor) {
    return NS_ERROR_FAILURE;
  }

  nsTArray<SSSetItemInfo> data;
  if (!cacheActor->SendLoad(&data)) {
    return NS_ERROR_FAILURE;
  }

  originRecord->mCache->DeserializeData(data);

  originRecord->mLoaded.Flip();
  aCache.SetLoadedOrCloned();

  return NS_OK;
}

void SessionStorageManager::CheckpointData(nsIPrincipal& aPrincipal,
                                           SessionStorageCache& aCache) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mActor);

  nsAutoCString originKey;
  nsresult rv = aPrincipal.GetStorageOriginKey(originKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  return CheckpointDataInternal(aPrincipal, originKey, aCache);
}

void SessionStorageManager::CheckpointDataInternal(
    nsIPrincipal& aPrincipal, const nsACString& aOriginKey,
    SessionStorageCache& aCache) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mActor);

  nsTArray<SSWriteInfo> writeInfos = aCache.SerializeWriteInfos();

  if (writeInfos.IsEmpty()) {
    return;
  }

  RefPtr<SessionStorageCacheChild> cacheActor =
      EnsureCache(aPrincipal, aOriginKey, aCache);

  if (!cacheActor) {
    return;
  }

  (void)cacheActor->SendCheckpoint(writeInfos);

  aCache.ResetWriteInfos();
}

nsresult SessionStorageManager::ClearStoragesForOrigin(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey) {
  AssertIsOnMainThread();

  ClearStoragesForOriginInternal(aOriginAttrs, aOriginKey);

  return NS_OK;
}

NS_IMETHODIMP
SessionStorageManager::PrecacheStorage(nsIPrincipal* aPrincipal,
                                       nsIPrincipal* aStoragePrincipal,
                                       Storage** aRetval) {
  // Nothing to preload.
  return NS_OK;
}

NS_IMETHODIMP
SessionStorageManager::GetSessionStorageCache(
    nsIPrincipal* aPrincipal, nsIPrincipal* aStoragePrincipal,
    RefPtr<SessionStorageCache>* aRetVal) {
  return GetSessionStorageCacheHelper(aStoragePrincipal, true, nullptr,
                                      aRetVal);
}

nsresult SessionStorageManager::GetSessionStorageCacheHelper(
    nsIPrincipal* aPrincipal, bool aMakeIfNeeded,
    SessionStorageCache* aCloneFrom, RefPtr<SessionStorageCache>* aRetVal) {
  nsAutoCString originKey;
  nsAutoCString originAttributes;
  nsresult rv = aPrincipal->GetStorageOriginKey(originKey);
  aPrincipal->OriginAttributesRef().CreateSuffix(originAttributes);
  if (NS_FAILED(rv)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return GetSessionStorageCacheHelper(originAttributes, originKey,
                                      aMakeIfNeeded, aCloneFrom, aRetVal);
}

nsresult SessionStorageManager::GetSessionStorageCacheHelper(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey,
    bool aMakeIfNeeded, SessionStorageCache* aCloneFrom,
    RefPtr<SessionStorageCache>* aRetVal) {
  if (OriginRecord* const originRecord = GetOriginRecord(
          aOriginAttrs, aOriginKey, aMakeIfNeeded, aCloneFrom)) {
    *aRetVal = originRecord->mCache;
  } else {
    *aRetVal = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
SessionStorageManager::CreateStorage(mozIDOMWindow* aWindow,
                                     nsIPrincipal* aPrincipal,
                                     nsIPrincipal* aStoragePrincipal,
                                     const nsAString& aDocumentURI,
                                     bool aPrivate, Storage** aRetval) {
  RefPtr<SessionStorageCache> cache;
  nsresult rv = GetSessionStorageCache(aPrincipal, aStoragePrincipal, &cache);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsPIDOMWindowInner> inner = nsPIDOMWindowInner::From(aWindow);

  RefPtr<SessionStorage> storage =
      new SessionStorage(inner, aPrincipal, aStoragePrincipal, cache, this,
                         aDocumentURI, aPrivate);

  storage.forget(aRetval);
  return NS_OK;
}

NS_IMETHODIMP
SessionStorageManager::GetStorage(mozIDOMWindow* aWindow,
                                  nsIPrincipal* aPrincipal,
                                  nsIPrincipal* aStoragePrincipal,
                                  bool aPrivate, Storage** aRetval) {
  *aRetval = nullptr;

  RefPtr<SessionStorageCache> cache;
  nsresult rv =
      GetSessionStorageCacheHelper(aStoragePrincipal, false, nullptr, &cache);
  if (NS_FAILED(rv) || !cache) {
    return rv;
  }

  nsCOMPtr<nsPIDOMWindowInner> inner = nsPIDOMWindowInner::From(aWindow);

  RefPtr<SessionStorage> storage = new SessionStorage(
      inner, aPrincipal, aStoragePrincipal, cache, this, u""_ns, aPrivate);

  storage.forget(aRetval);
  return NS_OK;
}

NS_IMETHODIMP
SessionStorageManager::CloneStorage(Storage* aStorage) {
  if (NS_WARN_IF(!aStorage)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (aStorage->Type() != Storage::eSessionStorage) {
    return NS_ERROR_UNEXPECTED;
  }

  // ToDo: At the moment, we clone the cache on the child process and then
  // send the checkpoint.  It would be nicer if we either serailizing all the
  // data and sync to the parent process directly or clonig storage on the
  // parnet process and sync it to the child process on demand.

  RefPtr<SessionStorageCache> cache;
  nsresult rv = GetSessionStorageCacheHelper(
      aStorage->StoragePrincipal(), true,
      static_cast<SessionStorage*>(aStorage)->Cache(), &cache);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // If cache was cloned from other storage, then we shouldn't load the cache
  // at the first access.
  cache->SetLoadedOrCloned();

  if (CanLoadData()) {
    rv = EnsureManager();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    CheckpointData(*aStorage->StoragePrincipal(), *cache);
  }

  return rv;
}

NS_IMETHODIMP
SessionStorageManager::CheckStorage(nsIPrincipal* aPrincipal, Storage* aStorage,
                                    bool* aRetval) {
  if (NS_WARN_IF(!aStorage)) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!aPrincipal) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  *aRetval = false;

  RefPtr<SessionStorageCache> cache;
  nsresult rv =
      GetSessionStorageCacheHelper(aPrincipal, false, nullptr, &cache);
  if (NS_FAILED(rv) || !cache) {
    return rv;
  }

  if (aStorage->Type() != Storage::eSessionStorage) {
    return NS_OK;
  }

  RefPtr<SessionStorage> sessionStorage =
      static_cast<SessionStorage*>(aStorage);
  if (sessionStorage->Cache() != cache) {
    return NS_OK;
  }

  if (!StorageUtils::PrincipalsEqual(aStorage->StoragePrincipal(),
                                     aPrincipal)) {
    return NS_OK;
  }

  *aRetval = true;
  return NS_OK;
}

void SessionStorageManager::ClearStorages(
    const OriginAttributesPattern& aPattern, const nsACString& aOriginScope,
    DomainMatchingMode aMode) {
  if (CanLoadData()) {
    nsresult rv = EnsureManager();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    mActor->SendClearStorages(aPattern, nsCString(aOriginScope), aMode);
  }

  ClearStoragesInternal(aPattern, aOriginScope, aMode);
}

nsresult SessionStorageManager::Observe(
    const char* aTopic, const nsAString& aOriginAttributesPattern,
    const nsACString& aOriginScope) {
  // StorageObserver only holds a weak pointer to us. Parsing the pattern can
  // GC, which can finish an in-progress cycle collection and destroy this
  // manager if its BrowsingContext has been discarded.
  RefPtr<SessionStorageManager> kungFuDeathGrip = this;

  OriginAttributesPattern pattern;
  if (!pattern.Init(aOriginAttributesPattern)) {
    NS_ERROR("Cannot parse origin attributes pattern");
    return NS_ERROR_FAILURE;
  }

  // Clear everything, caches + database
  if (!strcmp(aTopic, "cookie-cleared")) {
    ClearStorages(pattern, ""_ns);
    return NS_OK;
  }

  // Clear from caches everything that has been stored
  // while in session-only mode
  if (!strcmp(aTopic, "session-only-cleared")) {
    ClearStorages(pattern, aOriginScope);
    return NS_OK;
  }

  // Clear everything (including so and pb data) from caches and database
  // for the given domain and subdomains.
  if (!strcmp(aTopic, "browser:purge-sessionStorage")) {
    ClearStorages(pattern, aOriginScope);
    return NS_OK;
  }

  // Clear everything (including so and pb data) from caches and database
  // for the given domain.
  if (!strcmp(aTopic, "extension:purge-sessionStorage")) {
    ClearStorages(pattern, aOriginScope, DomainMatchingMode::EXACT_MATCH);
    return NS_OK;
  }

  // Clear entries which match an OriginAttributesPattern.
  if (!strcmp(aTopic, "dom-storage:clear-origin-attributes-data") ||
      !strcmp(aTopic, "session-storage:clear-origin-attributes-data")) {
    ClearStorages(pattern, aOriginScope);
    return NS_OK;
  }

  if (!strcmp(aTopic, "profile-change")) {
    // For case caches are still referenced - clear them completely
    ClearStorages(pattern, ""_ns);
    mOATable.Clear();
    return NS_OK;
  }

  return NS_OK;
}

SessionStorageManager::OriginRecord::~OriginRecord() = default;

// static
void BackgroundSessionStorageManager::RemoveManager(uint64_t aTopContextId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  AssertIsOnMainThread();

  ::mozilla::ipc::PBackgroundChild* backgroundActor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!backgroundActor)) {
    return;
  }

  if (NS_WARN_IF(!backgroundActor->SendRemoveBackgroundSessionStorageManager(
          aTopContextId))) {
    return;
  }
}

// static
void BackgroundSessionStorageManager::PropagateManager(
    uint64_t aCurrentTopContextId, uint64_t aTargetTopContextId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  AssertIsOnMainThread();
  MOZ_ASSERT(aCurrentTopContextId != aTargetTopContextId);

  ::mozilla::ipc::PBackgroundChild* backgroundActor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!backgroundActor)) {
    return;
  }

  if (NS_WARN_IF(!backgroundActor->SendPropagateBackgroundSessionStorageManager(
          aCurrentTopContextId, aTargetTopContextId))) {
    return;
  }
}

// static
void BackgroundSessionStorageManager::LoadData(
    uint64_t aTopContextId,
    const nsTArray<mozilla::dom::SSCacheCopy>& aCacheCopyList) {
  MOZ_ASSERT(XRE_IsParentProcess());
  AssertIsOnMainThread();

  ::mozilla::ipc::PBackgroundChild* backgroundActor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!backgroundActor)) {
    return;
  }

  if (NS_WARN_IF(!backgroundActor->SendLoadSessionStorageManagerData(
          aTopContextId, aCacheCopyList))) {
    return;
  }
}

// static
BackgroundSessionStorageManager* BackgroundSessionStorageManager::GetOrCreate(
    uint64_t aTopContextId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (!sManagers) {
    sManagers = new nsRefPtrHashtable<nsUint64HashKey,
                                      BackgroundSessionStorageManager>();
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "dom::BackgroundSessionStorageManager::GetOrCreate", [] {
          RunOnShutdown(
              [] {
                ::mozilla::ipc::PBackgroundChild* backgroundActor = ::mozilla::
                    ipc::BackgroundChild::GetOrCreateForCurrentThread();
                if (NS_WARN_IF(!backgroundActor)) {
                  return;
                }

                if (NS_WARN_IF(
                        !backgroundActor
                             ->SendShutdownBackgroundSessionStorageManagers())) {
                  return;
                }
              },
              ShutdownPhase::XPCOMShutdown);
        }));
  }

  return sManagers
      ->LookupOrInsertWith(
          aTopContextId,
          [aTopContextId] {
            return new BackgroundSessionStorageManager(aTopContextId);
          })
      .get();
}

BackgroundSessionStorageManager::BackgroundSessionStorageManager(
    uint64_t aBrowsingContextId)
    : mCurrentBrowsingContextId(aBrowsingContextId) {
  MOZ_ASSERT(XRE_IsParentProcess());
  ::mozilla::ipc::AssertIsOnBackgroundThread();
}

BackgroundSessionStorageManager::~BackgroundSessionStorageManager() = default;

void BackgroundSessionStorageManager::CopyDataToContentProcess(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey,
    nsTArray<SSSetItemInfo>& aData) {
  MOZ_ASSERT(XRE_IsParentProcess());
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  auto* const originRecord =
      GetOriginRecord(aOriginAttrs, aOriginKey, false, nullptr);
  if (!originRecord) {
    return;
  }

  aData = originRecord->mCache->SerializeData();
}

/* static */
RefPtr<BackgroundSessionStorageManager::DataPromise>
BackgroundSessionStorageManager::GetData(BrowsingContext* aContext,
                                         uint32_t aSizeLimit,
                                         bool aClearSessionStoreTimer) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aContext->IsTop());

  AssertIsOnMainThread();

  ::mozilla::ipc::PBackgroundChild* backgroundActor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!backgroundActor)) {
    return DataPromise::CreateAndReject(
        ::mozilla::ipc::ResponseRejectReason::SendError, __func__);
  }

  return backgroundActor->SendGetSessionStorageManagerData(
      aContext->Id(), aSizeLimit, aClearSessionStoreTimer);
}

void BackgroundSessionStorageManager::GetData(
    uint32_t aSizeLimit, nsTArray<SSCacheCopy>& aCacheCopyList) {
  for (auto& managerActor : mParticipatingActors) {
    for (auto* cacheActor :
         managerActor->ManagedPBackgroundSessionStorageCacheParent()) {
      auto* cache = static_cast<SessionStorageCacheParent*>(cacheActor);
      ::mozilla::ipc::PrincipalInfo info = cache->PrincipalInfo();

      OriginAttributes attributes;
      StoragePrincipalHelper::GetOriginAttributes(cache->PrincipalInfo(),
                                                  attributes);

      nsAutoCString originAttrs;
      attributes.CreateSuffix(originAttrs);

      auto* record =
          GetOriginRecord(originAttrs, cache->OriginKey(), false, nullptr);

      if (!record) {
        continue;
      }

      if (record->mCache->GetOriginQuotaUsage() > aSizeLimit) {
        continue;
      }

      nsTArray<SSSetItemInfo> data = record->mCache->SerializeData();
      if (data.IsEmpty()) {
        continue;
      }

      SSCacheCopy& cacheCopy = *aCacheCopyList.AppendElement();
      cacheCopy.originKey() = cache->OriginKey();
      cacheCopy.principalInfo() = info;
      cacheCopy.data().SwapElements(data);
    }
  }
}

void BackgroundSessionStorageManager::UpdateData(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey,
    const nsTArray<SSWriteInfo>& aWriteInfos) {
  MOZ_ASSERT(XRE_IsParentProcess());
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  auto* const originRecord =
      GetOriginRecord(aOriginAttrs, aOriginKey, true, nullptr);
  MOZ_ASSERT(originRecord);

  MaybeScheduleSessionStoreUpdate();

  originRecord->mCache->DeserializeWriteInfos(aWriteInfos);
}

void BackgroundSessionStorageManager::UpdateData(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey,
    const nsTArray<SSSetItemInfo>& aData) {
  MOZ_ASSERT(XRE_IsParentProcess());
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  auto* const originRecord =
      GetOriginRecord(aOriginAttrs, aOriginKey, true, nullptr);
  MOZ_ASSERT(originRecord);

  originRecord->mCache->DeserializeData(aData);
}

void BackgroundSessionStorageManager::ClearStorages(
    const OriginAttributesPattern& aPattern, const nsACString& aOriginScope,
    DomainMatchingMode aMode) {
  MOZ_ASSERT(XRE_IsParentProcess());
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  ClearStoragesInternal(aPattern, aOriginScope, aMode);
}

void BackgroundSessionStorageManager::ClearStoragesForOrigin(
    const nsACString& aOriginAttrs, const nsACString& aOriginKey) {
  ::mozilla::ipc::AssertIsInMainProcess();
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  for (auto& managerActor : mParticipatingActors) {
    QM_WARNONLY_TRY(OkIf(managerActor->SendClearStoragesForOrigin(
        nsCString(aOriginAttrs), nsCString(aOriginKey))));
  }

  ClearStoragesForOriginInternal(aOriginAttrs, aOriginKey);
}

void BackgroundSessionStorageManager::SetCurrentBrowsingContextId(
    uint64_t aBrowsingContextId) {
  MOZ_DIAGNOSTIC_ASSERT(aBrowsingContextId != mCurrentBrowsingContextId);
  mCurrentBrowsingContextId = aBrowsingContextId;
}

void BackgroundSessionStorageManager::MaybeScheduleSessionStoreUpdate() {
  if (!SessionStorePlatformCollection()) {
    return;
  }

  if (mSessionStoreCallbackTimer) {
    return;
  }

  if (StaticPrefs::browser_sessionstore_debug_no_auto_updates()) {
    DispatchSessionStoreUpdate();
    return;
  }

  auto result = NS_NewTimerWithFuncCallback(
      [](nsITimer*, void* aClosure) {
        auto* mgr = static_cast<BackgroundSessionStorageManager*>(aClosure);
        mgr->DispatchSessionStoreUpdate();
      },
      this, StaticPrefs::browser_sessionstore_interval(),
      nsITimer::TYPE_ONE_SHOT,
      "BackgroundSessionStorageManager::DispatchSessionStoreUpdate"_ns);

  if (result.isErr()) {
    return;
  }

  mSessionStoreCallbackTimer = result.unwrap();
}

void BackgroundSessionStorageManager::MaybeDispatchSessionStoreUpdate() {
  if (mSessionStoreCallbackTimer) {
    BackgroundSessionStorageManager::DispatchSessionStoreUpdate();
  }
}

void BackgroundSessionStorageManager::DispatchSessionStoreUpdate() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "CanonicalBrowsingContext::UpdateSessionStore",
      [targetBrowsingContextId = mCurrentBrowsingContextId]() {
        CanonicalBrowsingContext::UpdateSessionStoreForStorage(
            targetBrowsingContextId);
      }));

  CancelSessionStoreUpdate();
}

void BackgroundSessionStorageManager::CancelSessionStoreUpdate() {
  if (mSessionStoreCallbackTimer) {
    mSessionStoreCallbackTimer->Cancel();
    mSessionStoreCallbackTimer = nullptr;
  }
}

void BackgroundSessionStorageManager::AddParticipatingActor(
    SessionStorageManagerParent* aActor) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  mParticipatingActors.AppendElement(aActor);
}

void BackgroundSessionStorageManager::RemoveParticipatingActor(
    SessionStorageManagerParent* aActor) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  mParticipatingActors.RemoveElement(aActor);
}

}  // namespace mozilla::dom
