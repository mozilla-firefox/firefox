/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_cache_DBSchema_h
#define mozilla_dom_cache_DBSchema_h

#include "mozilla/dom/cache/Types.h"
#include "nsError.h"
#include "nsString.h"
#include "nsTArrayForwardDeclare.h"

class mozIStorageConnection;
struct nsID;

namespace mozilla::dom::cache {

class CacheQueryParams;
class CacheRequest;
class CacheResponse;
struct SavedRequest;
struct SavedResponse;

namespace db {

// Note, this cannot be executed within a transaction.
nsresult CreateOrMigrateSchema(nsIFile& aDBDir, mozIStorageConnection& aConn);

// Note, this cannot be executed within a transaction.
nsresult InitializeConnection(mozIStorageConnection& aConn);

Result<CacheId, nsresult> CreateCacheId(mozIStorageConnection& aConn);

Result<DeletionInfo, nsresult> DeleteCacheId(mozIStorageConnection& aConn,
                                             CacheId aCacheId);

Result<AutoTArray<CacheId, 8>, nsresult> FindOrphanedCacheIds(
    mozIStorageConnection& aConn);

// Deletes the caches that are neither named in storage nor have any entry
// left, which DeleteCacheId leaves behind when some entries were pending
// deletion. Only safe when content cannot reference any cache, i.e. at setup.
nsresult DeleteEmptyOrphanedCaches(mozIStorageConnection& aConn);

struct OrphanedBodies {
  nsTArray<nsID> mBodyIdList;
  // Disk size the database records for the bodies, i.e. what usage_info drops
  // once they are deleted. The quota usage must be decreased by the same
  // amount.
  int64_t mDiskSize = 0;
};

// Bodies of the entries pending deletion, whose body files are still on disk.
Result<OrphanedBodies, nsresult> FindOrphanedBodies(
    mozIStorageConnection& aConn);

// Deletes all the entries pending deletion. Only safe when content cannot
// reference any body, i.e. at setup. The body files must be deleted afterwards.
nsresult DeleteAllOrphanedEntries(mozIStorageConnection& aConn);

// Detaches the given bodies from the entries pending deletion and deletes the
// entries that have no body left. Returns the disk size the database recorded
// for the detached bodies, by which the quota usage must be decreased once the
// transaction is committed. The body files must be deleted afterwards.
Result<int64_t, nsresult> DeleteOrphanedBodyIds(
    mozIStorageConnection& aConn, const nsTArray<nsID>& aBodyIdList);

Result<int64_t, nsresult> FindOverallPaddingSize(mozIStorageConnection& aConn);

Result<int64_t, nsresult> GetTotalDiskUsage(mozIStorageConnection& aConn);

Result<nsTHashSet<nsID>, nsresult> GetKnownBodyIds(
    mozIStorageConnection& aConn);

Result<Maybe<SavedResponse>, nsresult> CacheMatch(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const CacheRequest& aRequest, const CacheQueryParams& aParams);

Result<nsTArray<SavedResponse>, nsresult> CacheMatchAll(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const Maybe<CacheRequest>& aMaybeRequest, const CacheQueryParams& aParams);

Result<DeletionInfo, nsresult> CachePut(mozIStorageConnection& aConn,
                                        CacheId aCacheId,
                                        const CacheRequest& aRequest,
                                        const nsID* aRequestBodyId,
                                        const CacheResponse& aResponse,
                                        const nsID* aResponseBodyId);

Result<Maybe<DeletionInfo>, nsresult> CacheDelete(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const CacheRequest& aRequest, const CacheQueryParams& aParams);

Result<nsTArray<SavedRequest>, nsresult> CacheKeys(
    mozIStorageConnection& aConn, CacheId aCacheId,
    const Maybe<CacheRequest>& aMaybeRequest, const CacheQueryParams& aParams);

Result<Maybe<SavedResponse>, nsresult> StorageMatch(
    mozIStorageConnection& aConn, Namespace aNamespace,
    const CacheRequest& aRequest, const CacheQueryParams& aParams);

Result<Maybe<CacheId>, nsresult> StorageGetCacheId(mozIStorageConnection& aConn,
                                                   Namespace aNamespace,
                                                   const nsAString& aKey);

nsresult StoragePutCache(mozIStorageConnection& aConn, Namespace aNamespace,
                         const nsAString& aKey, CacheId aCacheId);

nsresult StorageForgetCache(mozIStorageConnection& aConn, Namespace aNamespace,
                            const nsAString& aKey);

Result<nsTArray<nsString>, nsresult> StorageGetKeys(
    mozIStorageConnection& aConn, Namespace aNamespace);

// Note, this works best when its NOT executed within a transaction.
nsresult IncrementalVacuum(mozIStorageConnection& aConn);

// We will wipe out databases with a schema versions less than this.  Newer
// versions will be migrated on open to the latest schema version.
extern const int32_t kFirstShippedSchemaVersion;

}  // namespace db
}  // namespace mozilla::dom::cache

#endif  // mozilla_dom_cache_DBSchema_h
