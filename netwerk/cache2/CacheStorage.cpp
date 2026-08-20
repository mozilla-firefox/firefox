/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheStorage.h"

#include "CacheEntry.h"
#include "CacheLog.h"
#include "CacheObserver.h"
#include "CacheStorageService.h"
#include "nsICacheEntryDoomCallback.h"
#include "nsIURI.h"
#include "nsNetUtil.h"

namespace mozilla::net {

NS_IMPL_ISUPPORTS(CacheStorage, nsICacheStorage)

CacheStorage::CacheStorage(nsILoadContextInfo* aInfo, bool aAllowDisk,
                           bool aSkipSizeCheck, bool aPinning)
    : mLoadContextInfo(aInfo ? GetLoadContextInfo(aInfo) : nullptr),
      mWriteToDisk(aAllowDisk),
      mSkipSizeCheck(aSkipSizeCheck),
      mPinning(aPinning) {}

NS_IMETHODIMP CacheStorage::AsyncOpenURI(nsIURI* aURI,
                                         const nsACString& aIdExtension,
                                         uint32_t aFlags,
                                         nsICacheEntryOpenCallback* aCallback) {
  NS_ENSURE_ARG(aURI);

  nsCOMPtr<nsIURI> noRefURI;
  nsresult rv = NS_GetURIWithoutRef(aURI, getter_AddRefs(noRefURI));
  NS_ENSURE_SUCCESS(rv, rv);

  return AsyncOpenInternal(noRefURI, aIdExtension, aFlags, aCallback);
}

NS_IMETHODIMP CacheStorage::AsyncOpenURIString(
    const nsACString& aURI, const nsACString& aIdExtension, uint32_t aFlags,
    nsICacheEntryOpenCallback* aCallback) {
  // The only entry point that has to parse: the caller has nothing but a spec.
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aURI);
  if (NS_FAILED(rv)) {
    NS_ENSURE_ARG(aCallback);
    aCallback->OnCacheEntryAvailable(nullptr, false, rv);
    return NS_OK;
  }

  nsCOMPtr<nsIURI> noRefURI;
  rv = NS_GetURIWithoutRef(uri, getter_AddRefs(noRefURI));
  NS_ENSURE_SUCCESS(rv, rv);

  return AsyncOpenInternal(noRefURI, aIdExtension, aFlags, aCallback);
}

nsresult CacheStorage::AsyncOpenInternal(nsIURI* aURI,
                                         const nsACString& aIdExtension,
                                         uint32_t aFlags,
                                         nsICacheEntryOpenCallback* aCallback) {
  if (!CacheStorageService::Self()) return NS_ERROR_NOT_INITIALIZED;

  if (MOZ_UNLIKELY(!CacheObserver::UseDiskCache()) && mWriteToDisk &&
      !(aFlags & OPEN_INTERCEPTED)) {
    aCallback->OnCacheEntryAvailable(nullptr, false, NS_ERROR_NOT_AVAILABLE);
    return NS_OK;
  }

  if (MOZ_UNLIKELY(!CacheObserver::UseMemoryCache()) && !mWriteToDisk &&
      !(aFlags & OPEN_INTERCEPTED)) {
    aCallback->OnCacheEntryAvailable(nullptr, false, NS_ERROR_NOT_AVAILABLE);
    return NS_OK;
  }

  NS_ENSURE_ARG(aCallback);

  RefPtr<CacheEntryHandle> entry;
  nsresult rv = CacheStorageService::Self()->AddStorageEntry(
      this, aURI, aIdExtension, aFlags, getter_AddRefs(entry));
  if (NS_FAILED(rv)) {
    aCallback->OnCacheEntryAvailable(nullptr, false, rv);
    return NS_OK;
  }

  // May invoke the callback synchronously
  entry->Entry()->AsyncOpen(aCallback, aFlags);

  return NS_OK;
}

NS_IMETHODIMP CacheStorage::OpenTruncate(nsIURI* aURI,
                                         const nsACString& aIdExtension,
                                         nsICacheEntry** aCacheEntry) {
  if (!CacheStorageService::Self()) return NS_ERROR_NOT_INITIALIZED;

  nsresult rv;

  nsCOMPtr<nsIURI> noRefURI;
  rv = NS_GetURIWithoutRef(aURI, getter_AddRefs(noRefURI));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<CacheEntryHandle> handle;
  rv = CacheStorageService::Self()->AddStorageEntry(
      this, noRefURI, aIdExtension,
      nsICacheStorage::OPEN_TRUNCATE,  // replace any existing one
      getter_AddRefs(handle));
  NS_ENSURE_SUCCESS(rv, rv);

  // Just open w/o callback, similar to nsICacheEntry.recreate().
  handle->Entry()->AsyncOpen(nullptr, OPEN_TRUNCATE);

  // Return a write handler, consumer is supposed to fill in the entry.
  RefPtr<CacheEntryHandle> writeHandle = handle->Entry()->NewWriteHandle();
  writeHandle.forget(aCacheEntry);

  return NS_OK;
}

NS_IMETHODIMP CacheStorage::Exists(nsIURI* aURI, const nsACString& aIdExtension,
                                   bool* aResult) {
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG(aResult);

  if (!CacheStorageService::Self()) return NS_ERROR_NOT_INITIALIZED;

  nsresult rv;

  nsCOMPtr<nsIURI> noRefURI;
  rv = NS_GetURIWithoutRef(aURI, getter_AddRefs(noRefURI));
  NS_ENSURE_SUCCESS(rv, rv);

  return CacheStorageService::Self()->CheckStorageEntry(this, noRefURI,
                                                        aIdExtension, aResult);
}

NS_IMETHODIMP
CacheStorage::GetCacheIndexEntryAttrs(nsIURI* aURI,
                                      const nsACString& aIdExtension,
                                      bool* aHasAltData, uint32_t* aSizeInKB) {
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG(aHasAltData);
  NS_ENSURE_ARG(aSizeInKB);
  if (!CacheStorageService::Self()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv;

  nsCOMPtr<nsIURI> noRefURI;
  rv = NS_GetURIWithoutRef(aURI, getter_AddRefs(noRefURI));
  NS_ENSURE_SUCCESS(rv, rv);

  return CacheStorageService::Self()->GetCacheIndexEntryAttrs(
      this, noRefURI, aIdExtension, aHasAltData, aSizeInKB);
}

NS_IMETHODIMP CacheStorage::AsyncDoomURI(nsIURI* aURI,
                                         const nsACString& aIdExtension,
                                         nsICacheEntryDoomCallback* aCallback) {
  if (!CacheStorageService::Self()) return NS_ERROR_NOT_INITIALIZED;

  nsresult rv;

  nsCOMPtr<nsIURI> noRefURI;
  rv = NS_GetURIWithoutRef(aURI, getter_AddRefs(noRefURI));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = CacheStorageService::Self()->DoomStorageEntry(this, noRefURI,
                                                     aIdExtension, aCallback);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP CacheStorage::AsyncEvictStorage(
    nsICacheEntryDoomCallback* aCallback) {
  if (!CacheStorageService::Self()) return NS_ERROR_NOT_INITIALIZED;

  nsresult rv =
      CacheStorageService::Self()->DoomStorageEntries(this, aCallback);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP CacheStorage::AsyncVisitStorage(nsICacheStorageVisitor* aVisitor,
                                              bool aVisitEntries) {
  LOG(("CacheStorage::AsyncVisitStorage [this=%p, cb=%p, disk=%d]", this,
       aVisitor, (bool)mWriteToDisk));
  if (!CacheStorageService::Self()) return NS_ERROR_NOT_INITIALIZED;

  nsresult rv = CacheStorageService::Self()->WalkStorageEntries(
      this, aVisitEntries, aVisitor);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

}  // namespace mozilla::net
