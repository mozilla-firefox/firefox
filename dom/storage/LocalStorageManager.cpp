/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LocalStorageManager.h"

#include "LocalStorage.h"
#include "StorageDBThread.h"
#include "StorageIPC.h"
#include "StorageUtils.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/LocalStorageCommon.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsIEffectiveTLDService.h"
#include "nsIObserverService.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

namespace mozilla::dom {

using namespace StorageUtils;

LocalStorageManager* LocalStorageManager::sSelf = nullptr;

// static
uint32_t LocalStorageManager::GetOriginQuota() {
  return StaticPrefs::dom_storage_default_quota() * 1024;  // pref is in kBs
}

// static
uint32_t LocalStorageManager::GetSiteQuota() {
  return std::max(StaticPrefs::dom_storage_default_quota(),
                  StaticPrefs::dom_storage_default_site_quota()) *
         1024;  // pref is in kBs
}

NS_IMPL_ISUPPORTS(LocalStorageManager, nsIDOMStorageManager,
                  nsILocalStorageManager)

LocalStorageManager::LocalStorageManager() : mCaches(8) {
  MOZ_ASSERT(!NextGenLocalStorageEnabled());

  StorageObserver* observer = StorageObserver::Self();
  NS_ASSERTION(
      observer,
      "No StorageObserver, cannot observe private data delete notifications!");

  if (observer) {
    observer->AddSink(this);
  }

  NS_ASSERTION(!sSelf,
               "Somebody is trying to "
               "do_CreateInstance(\"@mozilla/dom/localStorage-manager;1\"");
  sSelf = this;

  if (!XRE_IsParentProcess()) {
    // Do this only on the child process.  The thread IPC bridge
    // is also used to communicate chrome observer notifications.
    // Note: must be called after we set sSelf
    for (const uint32_t id : {0, 1}) {
      StorageDBChild::GetOrCreate(id);
    }
  }
}

LocalStorageManager::~LocalStorageManager() {
  StorageObserver* observer = StorageObserver::Self();
  if (observer) {
    observer->RemoveSink(this);
  }

  sSelf = nullptr;
}

// static
nsAutoCString LocalStorageManager::CreateOrigin(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix) {
  // Note: some hard-coded sqlite statements are dependent on the format this
  // method returns.  Changing this without updating those sqlite statements
  // will cause malfunction.

  nsAutoCString scope;
  scope.Append(aOriginSuffix);
  scope.Append(':');
  scope.Append(aOriginNoSuffix);
  return scope;
}

LocalStorageCache* LocalStorageManager::GetCache(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix) {
  CacheOriginHashtable* table = mCaches.GetOrInsertNew(aOriginSuffix);
  LocalStorageCacheHashKey* entry = table->GetEntry(aOriginNoSuffix);
  if (!entry) {
    return nullptr;
  }

  return entry->cache();
}

already_AddRefed<StorageUsage> LocalStorageManager::GetOriginUsage(
    const nsACString& aOriginNoSuffix, const uint32_t aPrivateBrowsingId) {
  return do_AddRef(mUsages.LookupOrInsertWith(aOriginNoSuffix, [&] {
    auto usage = MakeRefPtr<StorageUsage>(aOriginNoSuffix);

    StorageDBChild* storageChild =
        StorageDBChild::GetOrCreate(aPrivateBrowsingId);
    if (storageChild) {
      storageChild->AsyncGetUsage(usage);
    }

    return usage;
  }));
}

already_AddRefed<LocalStorageCache> LocalStorageManager::PutCache(
    const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix,
    const nsACString& aQuotaKey, nsIPrincipal* aPrincipal) {
  CacheOriginHashtable* table = mCaches.GetOrInsertNew(aOriginSuffix);
  LocalStorageCacheHashKey* entry = table->PutEntry(aOriginNoSuffix);
  RefPtr<LocalStorageCache> cache = entry->cache();

  // Lifetime handled by the cache, do persist
  cache->Init(this, true, aPrincipal, aQuotaKey);
  return cache.forget();
}

void LocalStorageManager::DropCache(LocalStorageCache* aCache) {
  if (!NS_IsMainThread()) {
    NS_WARNING(
        "StorageManager::DropCache called on a non-main thread, shutting "
        "down?");
  }

  CacheOriginHashtable* table = mCaches.GetOrInsertNew(aCache->OriginSuffix());
  table->RemoveEntry(aCache->OriginNoSuffix());
}

nsresult LocalStorageManager::GetStorageInternal(
    CreateMode aCreateMode, mozIDOMWindow* aWindow, nsIPrincipal* aPrincipal,
    nsIPrincipal* aStoragePrincipal, const nsAString& aDocumentURI,
    bool aPrivate, Storage** aRetval) {
  nsAutoCString originAttrSuffix;
  nsAutoCString originKey;
  nsAutoCString quotaKey;

  // Throw if this process shouldn't have local storage access for this origin.
  if (!mozilla::ipc::BackgroundChild::ValidatePrincipal(aStoragePrincipal,
                                                        {})) {
    MOZ_ASSERT_UNREACHABLE("ValidatePrincipal failure in GetStorageInternal");
    return NS_ERROR_NOT_AVAILABLE;
  }

  aStoragePrincipal->OriginAttributesRef().CreateSuffix(originAttrSuffix);

  nsresult rv = aStoragePrincipal->GetStorageOriginKey(originKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  rv = aStoragePrincipal->GetLocalStorageQuotaKey(quotaKey);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<LocalStorageCache> cache = GetCache(originAttrSuffix, originKey);

  // Get or create a cache for the given scope
  if (!cache) {
    if (aCreateMode == CreateMode::UseIfExistsNeverCreate) {
      *aRetval = nullptr;
      return NS_OK;
    }

    if (aCreateMode == CreateMode::CreateIfShouldPreload) {
      const uint32_t privateBrowsingId =
          aStoragePrincipal->GetPrivateBrowsingId();

      // This is a demand to just preload the cache, if the scope has
      // no data stored, bypass creation and preload of the cache.
      StorageDBChild* db = StorageDBChild::Get(privateBrowsingId);
      if (db) {
        if (!db->ShouldPreloadOrigin(LocalStorageManager::CreateOrigin(
                originAttrSuffix, originKey))) {
          return NS_OK;
        }
      } else {
        if (originKey.EqualsLiteral("knalb.:about")) {
          return NS_OK;
        }
      }
    }

#if !defined(MOZ_WIDGET_ANDROID)
    ::mozilla::ipc::PBackgroundChild* backgroundActor =
        ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
    if (NS_WARN_IF(!backgroundActor)) {
      return NS_ERROR_FAILURE;
    }

    uint32_t privateBrowsingId;
    rv = aStoragePrincipal->GetPrivateBrowsingId(&privateBrowsingId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (!backgroundActor->CanSend()) {
      return NS_ERROR_FAILURE;
    }
#endif

    // There is always a single instance of a cache per scope
    // in a single instance of a DOM storage manager.
    cache = PutCache(originAttrSuffix, originKey, quotaKey, aStoragePrincipal);

#if !defined(MOZ_WIDGET_ANDROID)
    LocalStorageCacheChild* actor = new LocalStorageCacheChild(cache);

    MOZ_ALWAYS_TRUE(
        backgroundActor->SendPBackgroundLocalStorageCacheConstructor(
            actor, WrapNotNull(aStoragePrincipal), originKey,
            privateBrowsingId));

    cache->SetActor(actor);
#endif
  }

  if (aRetval) {
    nsCOMPtr<nsPIDOMWindowInner> inner = nsPIDOMWindowInner::From(aWindow);

    RefPtr<Storage> storage =
        new LocalStorage(inner, this, cache, aDocumentURI, aPrincipal,
                         aStoragePrincipal, aPrivate);
    storage.forget(aRetval);
  }

  return NS_OK;
}

NS_IMETHODIMP
LocalStorageManager::PrecacheStorage(nsIPrincipal* aPrincipal,
                                     nsIPrincipal* aStoragePrincipal,
                                     Storage** aRetval) {
  return GetStorageInternal(CreateMode::CreateIfShouldPreload, nullptr,
                            aPrincipal, aStoragePrincipal, u""_ns, false,
                            aRetval);
}

NS_IMETHODIMP
LocalStorageManager::CreateStorage(mozIDOMWindow* aWindow,
                                   nsIPrincipal* aPrincipal,
                                   nsIPrincipal* aStoragePrincipal,
                                   const nsAString& aDocumentURI, bool aPrivate,
                                   Storage** aRetval) {
  return GetStorageInternal(CreateMode::CreateAlways, aWindow, aPrincipal,
                            aStoragePrincipal, aDocumentURI, aPrivate, aRetval);
}

NS_IMETHODIMP
LocalStorageManager::GetStorage(mozIDOMWindow* aWindow,
                                nsIPrincipal* aPrincipal,
                                nsIPrincipal* aStoragePrincipal, bool aPrivate,
                                Storage** aRetval) {
  return GetStorageInternal(CreateMode::UseIfExistsNeverCreate, aWindow,
                            aPrincipal, aStoragePrincipal, u""_ns, aPrivate,
                            aRetval);
}

NS_IMETHODIMP
LocalStorageManager::CloneStorage(Storage* aStorage) {
  // Cloning is supported only for sessionStorage
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
LocalStorageManager::CheckStorage(nsIPrincipal* aPrincipal, Storage* aStorage,
                                  bool* aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aStorage);
  MOZ_ASSERT(aRetval);

  // Only used by sessionStorage.
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
LocalStorageManager::GetNextGenLocalStorageEnabled(bool* aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aResult);

  *aResult = NextGenLocalStorageEnabled();
  return NS_OK;
}

NS_IMETHODIMP
LocalStorageManager::Preload(nsIPrincipal* aPrincipal, JSContext* aContext,
                             Promise** _retval) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(_retval);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
LocalStorageManager::IsPreloaded(nsIPrincipal* aPrincipal, JSContext* aContext,
                                 Promise** _retval) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(_retval);

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
LocalStorageManager::GetState(nsIPrincipal* aPrincipal, JSContext* aContext,
                              Promise** _retval) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(_retval);

  return NS_ERROR_NOT_IMPLEMENTED;
}

void LocalStorageManager::ClearCaches(uint32_t aUnloadFlags,
                                      const OriginAttributesPattern& aPattern,
                                      const nsACString& aOriginScope) {
  for (const auto& cacheEntry : mCaches) {
    OriginAttributes oa;
    DebugOnly<bool> rv = oa.PopulateFromSuffix(cacheEntry.GetKey());
    MOZ_ASSERT(rv);
    if (!aPattern.Matches(oa)) {
      // This table doesn't match the given origin attributes pattern
      continue;
    }

    CacheOriginHashtable* table = cacheEntry.GetWeak();

    for (auto iter2 = table->Iter(); !iter2.Done(); iter2.Next()) {
      LocalStorageCache* cache = iter2.Get()->cache();

      if (aOriginScope.IsEmpty() ||
          StringBeginsWith(cache->OriginNoSuffix(), aOriginScope)) {
        cache->UnloadItems(aUnloadFlags);
      }
    }
  }
}

nsresult LocalStorageManager::Observe(const char* aTopic,
                                      const nsAString& aOriginAttributesPattern,
                                      const nsACString& aOriginScope) {
  OriginAttributesPattern pattern;
  if (!pattern.Init(aOriginAttributesPattern)) {
    NS_ERROR("Cannot parse origin attributes pattern");
    return NS_ERROR_FAILURE;
  }

  // Clear everything, caches + database
  if (!strcmp(aTopic, "cookie-cleared")) {
    ClearCaches(LocalStorageCache::kUnloadComplete, pattern, ""_ns);
    return NS_OK;
  }

  // Clear everything, caches + database
  if (!strcmp(aTopic, "extension:purge-localStorage-caches")) {
    ClearCaches(LocalStorageCache::kUnloadComplete, pattern, aOriginScope);
    return NS_OK;
  }

  if (!strcmp(aTopic, "browser:purge-sessionStorage") ||
      !strcmp(aTopic, "extension:purge-sessionStorage")) {
    // This is only meant for SessionStorageManager.
    return NS_OK;
  }

  // Clear from caches everything that has been stored
  // while in session-only mode
  if (!strcmp(aTopic, "session-only-cleared")) {
    ClearCaches(LocalStorageCache::kUnloadSession, pattern, aOriginScope);
    return NS_OK;
  }

  // Clear all private-browsing caches
  if (!strcmp(aTopic, "private-browsing-data-cleared")) {
    ClearCaches(LocalStorageCache::kUnloadComplete, pattern, ""_ns);
    return NS_OK;
  }

  // Clear localStorage data belonging to an origin pattern
  if (!strcmp(aTopic, "clear-origin-attributes-data") ||
      !strcmp(aTopic, "dom-storage:clear-origin-attributes-data")) {
    ClearCaches(LocalStorageCache::kUnloadComplete, pattern, ""_ns);
    return NS_OK;
  }

  if (!strcmp(aTopic, "profile-change")) {
    // For case caches are still referenced - clear them completely
    ClearCaches(LocalStorageCache::kUnloadComplete, pattern, ""_ns);
    mCaches.Clear();
    return NS_OK;
  }

#ifdef DOM_STORAGE_TESTS
  if (!strcmp(aTopic, "test-reload")) {
    // This immediately completely reloads all caches from the database.
    ClearCaches(LocalStorageCache::kTestReload, pattern, ""_ns);
    return NS_OK;
  }

  if (!strcmp(aTopic, "test-flushed")) {
    if (!XRE_IsParentProcess()) {
      nsCOMPtr<nsIObserverService> obs =
          mozilla::services::GetObserverService();
      if (obs) {
        obs->NotifyObservers(nullptr, "domstorage-test-flushed", nullptr);
      }
    }

    return NS_OK;
  }
#endif

  NS_ERROR("Unexpected topic");
  return NS_ERROR_UNEXPECTED;
}

// static
LocalStorageManager* LocalStorageManager::Self() {
  MOZ_ASSERT(!NextGenLocalStorageEnabled());

  return sSelf;
}

LocalStorageManager* LocalStorageManager::Ensure() {
  MOZ_ASSERT(!NextGenLocalStorageEnabled());

  if (sSelf) {
    return sSelf;
  }

  // Cause sSelf to be populated.
  nsCOMPtr<nsIDOMStorageManager> initializer =
      do_GetService("@mozilla.org/dom/localStorage-manager;1");
  MOZ_ASSERT(sSelf, "Didn't initialize?");

  return sSelf;
}

}  // namespace mozilla::dom
