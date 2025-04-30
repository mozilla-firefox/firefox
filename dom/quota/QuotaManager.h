/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_quota_quotamanager_h__
#define mozilla_dom_quota_quotamanager_h__

#include <cstdint>
#include <utility>
#include "Client.h"
#include "ErrorList.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/Assertions.h"
#include "mozilla/InitializedOnce.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/ThreadBound.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/quota/Assertions.h"
#include "mozilla/dom/quota/BackgroundThreadObject.h"
#include "mozilla/dom/quota/CommonMetadata.h"
#include "mozilla/dom/quota/DirectoryLockCategory.h"
#include "mozilla/dom/quota/ForwardDecls.h"
#include "mozilla/dom/quota/HashKeys.h"
#include "mozilla/dom/quota/InitializationTypes.h"
#include "mozilla/dom/quota/NotifyUtils.h"
#include "mozilla/dom/quota/OriginOperationCallbacks.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsTHashMap.h"
#include "nsDebug.h"
#include "nsHashKeys.h"
#include "nsISupports.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTStringRepr.h"
#include "nscore.h"
#include "prenv.h"

#define GTEST_CLASS(testFixture, testName) testFixture##_##testName##_Test

class mozIStorageConnection;
class nsIEventTarget;
class nsIFile;
class nsIRunnable;
class nsIThread;
class nsITimer;

namespace mozilla {

class OriginAttributes;
class OriginAttributesPattern;

namespace ipc {

class PrincipalInfo;

}  // namespace ipc

}  // namespace mozilla

namespace mozilla::dom::quota {

class CanonicalQuotaObject;
class ClearDataOp;
class ClearRequestBase;
class ClientStorageScope;
class ClientUsageArray;
class ClientDirectoryLock;
class ClientDirectoryLockHandle;
class DirectoryLockImpl;
class GroupInfo;
class GroupInfoPair;
class NormalOriginOperationBase;
class OriginDirectoryLock;
class OriginInfo;
class OriginScope;
class QuotaObject;
class UniversalDirectoryLock;

namespace test {
class GTEST_CLASS(TestQuotaManagerAndShutdownFixture,
                  ThumbnailPrivateIdentityTemporaryOriginCount);
}

class QuotaManager final : public BackgroundThreadObject {
  friend class CanonicalQuotaObject;
  friend class ClearDataOp;
  friend class ClearRequestBase;
  friend class ClearStorageOp;
  friend class DirectoryLockImpl;
  friend class FinalizeOriginEvictionOp;
  friend class GroupInfo;
  friend class InitOp;
  friend class InitializePersistentOriginOp;
  friend class InitializePersistentStorageOp;
  friend class InitializeTemporaryGroupOp;
  friend class InitializeTemporaryOriginOp;
  friend class InitTemporaryStorageOp;
  friend class ListCachedOriginsOp;
  friend class OriginInfo;
  friend class PersistOp;
  friend class ShutdownStorageOp;
  friend class test::GTEST_CLASS(TestQuotaManagerAndShutdownFixture,
                                 ThumbnailPrivateIdentityTemporaryOriginCount);
  friend class UniversalDirectoryLock;

  friend Result<PrincipalMetadata, nsresult> GetInfoFromValidatedPrincipalInfo(
      QuotaManager& aQuotaManager,
      const mozilla::ipc::PrincipalInfo& aPrincipalInfo);

  using PrincipalInfo = mozilla::ipc::PrincipalInfo;
  using DirectoryLockTable =
      nsClassHashtable<nsCStringHashKey, nsTArray<NotNull<DirectoryLockImpl*>>>;

  class Observer;

 public:
  using ClientDirectoryLockHandlePromise =
      MozPromise<ClientDirectoryLockHandle, nsresult, true>;

  QuotaManager(const nsAString& aBasePath, const nsAString& aStorageName);

  NS_INLINE_DECL_REFCOUNTING(QuotaManager)

  static nsresult Initialize();

  static bool IsRunningXPCShellTests() {
    static bool kRunningXPCShellTests =
        !!PR_GetEnv("XPCSHELL_TEST_PROFILE_DIR");
    return kRunningXPCShellTests;
  }

  static bool IsRunningGTests() {
    static bool kRunningGTests = !!PR_GetEnv("MOZ_RUN_GTEST");
    return kRunningGTests;
  }

  static const char kReplaceChars[];
  static const char16_t kReplaceChars16[];

  static Result<MovingNotNull<RefPtr<QuotaManager>>, nsresult> GetOrCreate();

  static Result<Ok, nsresult> EnsureCreated();

  // Returns a non-owning reference.
  static QuotaManager* Get();

  // Use only in gtests!
  static nsIObserver* GetObserver();

  // Returns true if we've begun the shutdown process.
  static bool IsShuttingDown();

  static void ShutdownInstance();

  // Use only in gtests!
  static void Reset();

  static bool IsOSMetadata(const nsAString& aFileName);

  static bool IsDotFile(const nsAString& aFileName);

  void RegisterNormalOriginOp(NormalOriginOperationBase& aNormalOriginOp);

  void UnregisterNormalOriginOp(NormalOriginOperationBase& aNormalOriginOp);

  bool IsPersistentOriginInitializedInternal(const nsACString& aOrigin) const {
    AssertIsOnIOThread();

    return mInitializedOriginsInternal.Contains(aOrigin);
  }

  bool IsTemporaryStorageInitializedInternal() const {
    AssertIsOnIOThread();

    return mTemporaryStorageInitializedInternal;
  }

  /**
   * For initialization of an origin where the directory either exists or it
   * does not. The directory exists case is used by InitializeOrigin once it
   * has tallied origin usage by calling each of the QuotaClient InitOrigin
   * methods. It's also used by LoadQuota when quota information is available
   * from the cache. EnsureTemporaryStorageIsInitializedInternal calls this
   * either if the directory exists or it does not depending on requirements
   * of a particular quota client. The special case when origin directory is
   * not created during origin initialization is currently utilized only by
   * LSNG.
   */
  void InitQuotaForOrigin(const FullOriginMetadata& aFullOriginMetadata,
                          const ClientUsageArray& aClientUsages,
                          uint64_t aUsageBytes, bool aDirectoryExists = true);

  // XXX clients can use QuotaObject instead of calling this method directly.
  void DecreaseUsageForClient(const ClientMetadata& aClientMetadata,
                              int64_t aSize);

  void ResetUsageForClient(const ClientMetadata& aClientMetadata);

  UsageInfo GetUsageForClient(PersistenceType aPersistenceType,
                              const OriginMetadata& aOriginMetadata,
                              Client::Type aClientType);

  void UpdateOriginAccessTime(PersistenceType aPersistenceType,
                              const OriginMetadata& aOriginMetadata);

  void RemoveQuota();

  void RemoveQuotaForRepository(PersistenceType aPersistenceType) {
    MutexAutoLock lock(mQuotaMutex);
    LockedRemoveQuotaForRepository(aPersistenceType);
  }

  void RemoveQuotaForOrigin(PersistenceType aPersistenceType,
                            const OriginMetadata& aOriginMetadata) {
    MutexAutoLock lock(mQuotaMutex);
    LockedRemoveQuotaForOrigin(aOriginMetadata);
  }

  nsresult LoadQuota();

  void UnloadQuota();

  void RemoveOriginFromCache(const OriginMetadata& aOriginMetadata);

  already_AddRefed<QuotaObject> GetQuotaObject(
      PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
      Client::Type aClientType, nsIFile* aFile, int64_t aFileSize = -1,
      int64_t* aFileSizeOut = nullptr);

  already_AddRefed<QuotaObject> GetQuotaObject(
      PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
      Client::Type aClientType, const nsAString& aPath, int64_t aFileSize = -1,
      int64_t* aFileSizeOut = nullptr);

  already_AddRefed<QuotaObject> GetQuotaObject(const int64_t aDirectoryLockId,
                                               const nsAString& aPath);

  Nullable<bool> OriginPersisted(const OriginMetadata& aOriginMetadata);

  void PersistOrigin(const OriginMetadata& aOriginMetadata);

  template <typename F>
  auto WithOriginInfo(const OriginMetadata& aOriginMetadata, F aFunction)
      -> std::invoke_result_t<F, const RefPtr<OriginInfo>&>;

  using DirectoryLockIdTableArray =
      AutoTArray<Client::DirectoryLockIdTable, Client::TYPE_MAX>;
  void AbortOperationsForLocks(const DirectoryLockIdTableArray& aLockIds);

  // Called when a process is being shot down. Aborts any running operations
  // for the given process.
  void AbortOperationsForProcess(ContentParentId aContentParentId);

  Result<nsCOMPtr<nsIFile>, nsresult> GetOriginDirectory(
      const OriginMetadata& aOriginMetadata) const;

  Result<bool, nsresult> DoesOriginDirectoryExist(
      const OriginMetadata& aOriginMetadata) const;

  Result<nsCOMPtr<nsIFile>, nsresult> GetOrCreateTemporaryOriginDirectory(
      const OriginMetadata& aOriginMetadata);

  Result<Ok, nsresult> EnsureTemporaryOriginDirectoryCreated(
      const OriginMetadata& aOriginMetadata);

  static nsresult CreateDirectoryMetadata(
      nsIFile& aDirectory, int64_t aTimestamp,
      const OriginMetadata& aOriginMetadata);

  static nsresult CreateDirectoryMetadata2(
      nsIFile& aDirectory, int64_t aTimestamp, bool aPersisted,
      const OriginMetadata& aOriginMetadata);

  nsresult RestoreDirectoryMetadata2(nsIFile* aDirectory);

  // XXX Remove aPersistenceType argument once the persistence type is stored
  // in the metadata file.
  Result<FullOriginMetadata, nsresult> LoadFullOriginMetadata(
      nsIFile* aDirectory, PersistenceType aPersistenceType);

  Result<FullOriginMetadata, nsresult> LoadFullOriginMetadataWithRestore(
      nsIFile* aDirectory);

  Result<OriginMetadata, nsresult> GetOriginMetadata(nsIFile* aDirectory);

  Result<Ok, nsresult> RemoveOriginDirectory(nsIFile& aDirectory);

  Result<bool, nsresult> DoesClientDirectoryExist(
      const ClientMetadata& aClientMetadata) const;

  RefPtr<UniversalDirectoryLockPromise> OpenStorageDirectory(
      const PersistenceScope& aPersistenceScope,
      const OriginScope& aOriginScope,
      const ClientStorageScope& aClientStorageScope, bool aExclusive,
      bool aInitializeOrigins = false,
      DirectoryLockCategory aCategory = DirectoryLockCategory::None,
      Maybe<RefPtr<UniversalDirectoryLock>&> aPendingDirectoryLockOut =
          Nothing());

  // This is the main entry point into the QuotaManager API.
  // Any storage API implementation (quota client) that participates in
  // centralized quota and storage handling should call this method to obtain
  // a directory lock, ensuring the client’s files are protected from deletion
  // while in use.
  //
  // After a lock is acquired, the client is notified by resolving the returned
  // promise. If the lock couldn't be acquired, the promise is rejected.
  //
  // The returned lock is encapsulated in ClientDirectoryLockHandle, which
  // manages ownership and automatically drops the lock when destroyed. Clients
  // should retain ownership of the handle for as long as the lock is needed.
  //
  // The lock may still be invalidated by a clear operation, so consumers
  // should check its validity and release it as soon as it is no longer
  // required.
  //
  // Internally, QuotaManager may perform various initialization steps before
  // resolving the promise. This can include storage, temporary storage, group
  // and origin initialization.
  //
  // Optionally, an output parameter (aPendingDirectoryLockOut) can be provided
  // to receive a reference to the ClientDirectoryLock before wrapping it in
  // ClientDirectoryLockHandle. This allows tracking pending locks separately.
  RefPtr<ClientDirectoryLockHandlePromise> OpenClientDirectory(
      const ClientMetadata& aClientMetadata, bool aInitializeOrigins = true,
      bool aCreateIfNonExistent = true,
      Maybe<RefPtr<ClientDirectoryLock>&> aPendingDirectoryLockOut = Nothing());

  RefPtr<ClientDirectoryLock> CreateDirectoryLock(
      const ClientMetadata& aClientMetadata, bool aExclusive);

  // XXX RemoveMe once bug 1170279 gets fixed.
  RefPtr<UniversalDirectoryLock> CreateDirectoryLockInternal(
      const PersistenceScope& aPersistenceScope,
      const OriginScope& aOriginScope,
      const ClientStorageScope& aClientStorageScope, bool aExclusive,
      DirectoryLockCategory aCategory = DirectoryLockCategory::None);

  // Collect inactive and the least recently used origins.
  uint64_t CollectOriginsForEviction(
      uint64_t aMinSizeToBeFreed,
      nsTArray<RefPtr<OriginDirectoryLock>>& aLocks);

  /**
   * Helper method to invoke the provided predicate on all "pending" OriginInfo
   * instances. These are origins for which the origin directory has not yet
   * been created but for which quota is already being tracked. This happens,
   * for example, for the LocalStorage client where an origin that previously
   * was not using LocalStorage can start issuing writes which it buffers until
   * eventually flushing them. We defer creating the origin directory for as
   * long as possible in that case, so the directory won't exist. Logic that
   * would otherwise only consult the filesystem also needs to use this method.
   */
  template <typename P>
  void CollectPendingOriginsForListing(P aPredicate);

  bool IsPendingOrigin(const OriginMetadata& aOriginMetadata) const;

  RefPtr<BoolPromise> InitializeStorage();

  RefPtr<BoolPromise> InitializeStorage(
      RefPtr<UniversalDirectoryLock> aDirectoryLock);

  RefPtr<BoolPromise> StorageInitialized();

  bool IsStorageInitialized() const {
    AssertIsOnOwningThread();

    return mStorageInitialized;
  }

  bool IsStorageInitializedInternal() const {
    AssertIsOnIOThread();
    return static_cast<bool>(mStorageConnection);
  }

  void AssertStorageIsInitializedInternal() const
#ifdef DEBUG
      ;
#else
  {
  }
#endif

  RefPtr<BoolPromise> TemporaryStorageInitialized();

 private:
  nsresult EnsureStorageIsInitializedInternal();

 public:
  RefPtr<BoolPromise> InitializePersistentStorage();

  RefPtr<BoolPromise> InitializePersistentStorage(
      RefPtr<UniversalDirectoryLock> aDirectoryLock);

  RefPtr<BoolPromise> PersistentStorageInitialized();

  bool IsPersistentStorageInitialized() const {
    AssertIsOnOwningThread();

    return mPersistentStorageInitialized;
  }

  bool IsPersistentStorageInitializedInternal() const {
    AssertIsOnIOThread();

    return mPersistentStorageInitializedInternal;
  }

 private:
  nsresult EnsurePersistentStorageIsInitializedInternal();

 public:
  RefPtr<BoolPromise> InitializeTemporaryGroup(
      const PrincipalMetadata& aPrincipalMetadata);

  RefPtr<BoolPromise> InitializeTemporaryGroup(
      const PrincipalMetadata& aPrincipalMetadata,
      RefPtr<UniversalDirectoryLock> aDirectoryLock);

  RefPtr<BoolPromise> TemporaryGroupInitialized(
      const PrincipalMetadata& aPrincipalMetadata);

  bool IsTemporaryGroupInitialized(const PrincipalMetadata& aPrincipalMetadata);

  bool IsTemporaryGroupInitializedInternal(
      const PrincipalMetadata& aPrincipalMetadata) const;

 private:
  Result<Ok, nsresult> EnsureTemporaryGroupIsInitializedInternal(
      const PrincipalMetadata& aPrincipalMetadata);

 public:
  RefPtr<BoolPromise> InitializePersistentOrigin(
      const OriginMetadata& aOriginMetadata);

  RefPtr<BoolPromise> InitializePersistentOrigin(
      const OriginMetadata& aOriginMetadata,
      RefPtr<UniversalDirectoryLock> aDirectoryLock);

  RefPtr<BoolPromise> PersistentOriginInitialized(
      const OriginMetadata& aOriginMetadata);

  bool IsPersistentOriginInitialized(const OriginMetadata& aOriginMetadata);

  bool IsPersistentOriginInitializedInternal(
      const OriginMetadata& aOriginMetadata) const;

 private:
  // Returns a pair of an nsIFile object referring to the directory, and a bool
  // indicating whether the directory was newly created.
  Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
  EnsurePersistentOriginIsInitializedInternal(
      const OriginMetadata& aOriginMetadata);

 public:
  RefPtr<BoolPromise> InitializeTemporaryOrigin(
      const OriginMetadata& aOriginMetadata, bool aCreateIfNonExistent);

  RefPtr<BoolPromise> InitializeTemporaryOrigin(
      const OriginMetadata& aOriginMetadata, bool aCreateIfNonExistent,
      RefPtr<UniversalDirectoryLock> aDirectoryLock);

  RefPtr<BoolPromise> TemporaryOriginInitialized(
      const OriginMetadata& aOriginMetadata);

  bool IsTemporaryOriginInitialized(const OriginMetadata& aOriginMetadata);

  bool IsTemporaryOriginInitializedInternal(
      const OriginMetadata& aOriginMetadata) const;

 private:
  // Returns a pair of an nsIFile object referring to the directory, and a bool
  // indicating whether the directory was newly created.
  Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
  EnsureTemporaryOriginIsInitializedInternal(
      const OriginMetadata& aOriginMetadata, bool aCreateIfNonExistent);

 public:
  RefPtr<BoolPromise> InitializePersistentClient(
      const PrincipalInfo& aPrincipalInfo, Client::Type aClientType);

  // Returns a pair of an nsIFile object referring to the directory, and a bool
  // indicating whether the directory was newly created.
  Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
  EnsurePersistentClientIsInitialized(const ClientMetadata& aClientMetadata);

  RefPtr<BoolPromise> InitializeTemporaryClient(
      PersistenceType aPersistenceType, const PrincipalInfo& aPrincipalInfo,
      Client::Type aClientType);

  // Returns a pair of an nsIFile object referring to the directory, and a bool
  // indicating whether the directory was newly created.
  Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
  EnsureTemporaryClientIsInitialized(const ClientMetadata& aClientMetadata);

  RefPtr<BoolPromise> InitializeTemporaryStorage();

  RefPtr<BoolPromise> InitializeTemporaryStorage(
      RefPtr<UniversalDirectoryLock> aDirectoryLock);

  bool IsTemporaryStorageInitialized() const {
    AssertIsOnOwningThread();

    return mTemporaryStorageInitialized;
  }

 private:
  nsresult InitializeTemporaryStorageInternal();

  nsresult EnsureTemporaryStorageIsInitializedInternal();

 public:
  RefPtr<BoolPromise> InitializeAllTemporaryOrigins();

  RefPtr<BoolPromise> SaveOriginAccessTime(
      const OriginMetadata& aOriginMetadata, int64_t aTimestamp);

  RefPtr<OriginUsageMetadataArrayPromise> GetUsage(
      bool aGetAll, RefPtr<BoolPromise> aOnCancelPromise = nullptr);

  RefPtr<UsageInfoPromise> GetOriginUsage(
      const PrincipalInfo& aPrincipalInfo,
      RefPtr<BoolPromise> aOnCancelPromise = nullptr);

  RefPtr<UInt64Promise> GetCachedOriginUsage(
      const PrincipalInfo& aPrincipalInfo);

  RefPtr<CStringArrayPromise> ListOrigins();

  RefPtr<CStringArrayPromise> ListCachedOrigins();

  RefPtr<BoolPromise> ClearStoragesForOrigin(
      const Maybe<PersistenceType>& aPersistenceType,
      const PrincipalInfo& aPrincipalInfo);

  RefPtr<BoolPromise> ClearStoragesForClient(
      Maybe<PersistenceType> aPersistenceType,
      const PrincipalInfo& aPrincipalInfo, Client::Type aClientType);

  RefPtr<BoolPromise> ClearStoragesForOriginPrefix(
      const Maybe<PersistenceType>& aPersistenceType,
      const PrincipalInfo& aPrincipalInfo);

  RefPtr<BoolPromise> ClearStoragesForOriginAttributesPattern(
      const OriginAttributesPattern& aPattern);

  RefPtr<BoolPromise> ClearPrivateRepository();

  RefPtr<BoolPromise> ClearStorage();

  RefPtr<BoolPromise> ShutdownStoragesForOrigin(
      Maybe<PersistenceType> aPersistenceType,
      const PrincipalInfo& aPrincipalInfo);

  RefPtr<BoolPromise> ShutdownStoragesForClient(
      Maybe<PersistenceType> aPersistenceType,
      const PrincipalInfo& aPrincipalInfo, Client::Type aClientType);

  RefPtr<BoolPromise> ShutdownStorage(
      Maybe<OriginOperationCallbackOptions> aCallbackOptions = Nothing(),
      Maybe<OriginOperationCallbacks&> aCallbacks = Nothing());

  void ShutdownStorageInternal();

  // Returns a bool indicating whether the directory was newly created.
  Result<bool, nsresult> EnsureOriginDirectory(nsIFile& aDirectory);

  nsresult AboutToClearOrigins(const PersistenceScope& aPersistenceScope,
                               const OriginScope& aOriginScope,
                               const ClientStorageScope& aClientStorageScope);

  void OriginClearCompleted(const OriginMetadata& aOriginMetadata,
                            const ClientStorageScope& aClientStorageScope);

  void RepositoryClearCompleted(PersistenceType aPersistenceType);

  void StartIdleMaintenance() {
    AssertIsOnOwningThread();

    for (const auto& client : *mClients) {
      client->StartIdleMaintenance();
    }

    NotifyMaintenanceStarted(*this);
  }

  void StopIdleMaintenance() {
    AssertIsOnOwningThread();

    for (const auto& client : *mClients) {
      client->StopIdleMaintenance();
    }
  }

  void AssertCurrentThreadOwnsQuotaMutex() {
    mQuotaMutex.AssertCurrentThreadOwns();
  }

  void AssertNotCurrentThreadOwnsQuotaMutex() {
    mQuotaMutex.AssertNotCurrentThreadOwns();
  }

  nsIThread* IOThread() { return mIOThread->get(); }

  Client* GetClient(Client::Type aClientType);

  const AutoTArray<Client::Type, Client::TYPE_MAX>& AllClientTypes();

  const nsString& GetBasePath() const { return mBasePath; }

  const nsString& GetStorageName() const { return mStorageName; }

  const nsString& GetStoragePath() const { return *mStoragePath; }

  const nsString& GetStoragePath(PersistenceType aPersistenceType) const {
    if (aPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
      return *mPermanentStoragePath;
    }

    if (aPersistenceType == PERSISTENCE_TYPE_TEMPORARY) {
      return *mTemporaryStoragePath;
    }

    if (aPersistenceType == PERSISTENCE_TYPE_DEFAULT) {
      return *mDefaultStoragePath;
    }

    MOZ_ASSERT(aPersistenceType == PERSISTENCE_TYPE_PRIVATE);

    return *mPrivateStoragePath;
  }

  bool IsThumbnailPrivateIdentityIdKnown() const;

  uint32_t GetThumbnailPrivateIdentityId() const;

  void SetThumbnailPrivateIdentityId(uint32_t aThumbnailPrivateIdentityId);

  uint64_t GetGroupLimit() const;

  std::pair<uint64_t, uint64_t> GetUsageAndLimitForEstimate(
      const OriginMetadata& aOriginMetadata);

  uint64_t GetOriginUsage(const PrincipalMetadata& aPrincipalMetadata);

  Maybe<FullOriginMetadata> GetFullOriginMetadata(
      const OriginMetadata& aOriginMetadata);

  /**
   * Retrieves the total number of directory iterations performed.
   *
   * @return The total count of directory iterations, which is currently
   *         incremented only during clearing operations.
   */
  uint64_t TotalDirectoryIterations() const;

  // Record a quota client shutdown step, if shutting down.
  // Assumes that the QuotaManager singleton is alive.
  static void MaybeRecordQuotaClientShutdownStep(
      const Client::Type aClientType, const nsACString& aStepDescription);

  // Record a quota client shutdown step, if shutting down.
  // Checks if the QuotaManager singleton is alive.
  static void SafeMaybeRecordQuotaClientShutdownStep(
      Client::Type aClientType, const nsACString& aStepDescription);

  // Record a quota manager shutdown step, use only if shutdown is active.
  void RecordQuotaManagerShutdownStep(const nsACString& aStepDescription);

  // Record a quota manager shutdown step, if shutting down.
  void MaybeRecordQuotaManagerShutdownStep(const nsACString& aStepDescription);

  template <typename F>
  void MaybeRecordQuotaManagerShutdownStepWith(F&& aFunc);

  static void GetStorageId(PersistenceType aPersistenceType,
                           const nsACString& aOrigin, Client::Type aClientType,
                           nsACString& aDatabaseId);

  static bool IsOriginInternal(const nsACString& aOrigin);

  static bool AreOriginsEqualOnDisk(const nsACString& aOrigin1,
                                    const nsACString& aOrigin2);

  // XXX This method currently expects the original origin string (not yet
  // sanitized).
  static Result<PrincipalInfo, nsresult> ParseOrigin(const nsACString& aOrigin);

  static void InvalidateQuotaCache();

 private:
  virtual ~QuotaManager();

  nsresult Init();

  void Shutdown();

  void RegisterDirectoryLock(DirectoryLockImpl& aLock);

  void UnregisterDirectoryLock(DirectoryLockImpl& aLock);

  void AddPendingDirectoryLock(DirectoryLockImpl& aLock);

  void RemovePendingDirectoryLock(DirectoryLockImpl& aLock);

  uint64_t LockedCollectOriginsForEviction(
      uint64_t aMinSizeToBeFreed,
      nsTArray<RefPtr<OriginDirectoryLock>>& aLocks);

  void LockedRemoveQuotaForRepository(PersistenceType aPersistenceType);

  void LockedRemoveQuotaForOrigin(const OriginMetadata& aOriginMetadata);

  bool LockedHasGroupInfoPair(const nsACString& aGroup) const;

  already_AddRefed<GroupInfo> LockedGetOrCreateGroupInfo(
      PersistenceType aPersistenceType, const nsACString& aSuffix,
      const nsACString& aGroup);

  already_AddRefed<OriginInfo> LockedGetOriginInfo(
      PersistenceType aPersistenceType,
      const OriginMetadata& aOriginMetadata) const;

  nsresult UpgradeFromIndexedDBDirectoryToPersistentStorageDirectory(
      nsIFile* aIndexedDBDir);

  nsresult UpgradeFromPersistentStorageDirectoryToDefaultStorageDirectory(
      nsIFile* aPersistentStorageDir);

  nsresult MaybeUpgradeToDefaultStorageDirectory(nsIFile& aStorageFile);

  template <typename Helper>
  nsresult UpgradeStorage(const int32_t aOldVersion, const int32_t aNewVersion,
                          mozIStorageConnection* aConnection);

  nsresult UpgradeStorageFrom0_0To1_0(mozIStorageConnection* aConnection);

  nsresult UpgradeStorageFrom1_0To2_0(mozIStorageConnection* aConnection);

  nsresult UpgradeStorageFrom2_0To2_1(mozIStorageConnection* aConnection);

  nsresult UpgradeStorageFrom2_1To2_2(mozIStorageConnection* aConnection);

  nsresult UpgradeStorageFrom2_2To2_3(mozIStorageConnection* aConnection);

  nsresult MaybeCreateOrUpgradeStorage(mozIStorageConnection& aConnection);

  OkOrErr MaybeRemoveLocalStorageArchiveTmpFile();

  nsresult MaybeRemoveLocalStorageDataAndArchive(nsIFile& aLsArchiveFile);

  nsresult MaybeRemoveLocalStorageDirectories();

  Result<Ok, nsresult> CopyLocalStorageArchiveFromWebAppsStore(
      nsIFile& aLsArchiveFile) const;

  Result<nsCOMPtr<mozIStorageConnection>, nsresult>
  CreateLocalStorageArchiveConnection(nsIFile& aLsArchiveFile) const;

  Result<nsCOMPtr<mozIStorageConnection>, nsresult>
  RecopyLocalStorageArchiveFromWebAppsStore(nsIFile& aLsArchiveFile);

  Result<nsCOMPtr<mozIStorageConnection>, nsresult>
  DowngradeLocalStorageArchive(nsIFile& aLsArchiveFile);

  Result<nsCOMPtr<mozIStorageConnection>, nsresult>
  UpgradeLocalStorageArchiveFromLessThan4To4(nsIFile& aLsArchiveFile);

  /*
  nsresult UpgradeLocalStorageArchiveFrom4To5();
  */

  Result<Ok, nsresult> MaybeCreateOrUpgradeLocalStorageArchive(
      nsIFile& aLsArchiveFile);

  Result<Ok, nsresult> CreateEmptyLocalStorageArchive(
      nsIFile& aLsArchiveFile) const;

  template <typename OriginFunc>
  nsresult InitializeRepository(PersistenceType aPersistenceType,
                                OriginFunc&& aOriginFunc);

  nsresult InitializeOrigin(PersistenceType aPersistenceType,
                            const OriginMetadata& aOriginMetadata,
                            int64_t aAccessTime, bool aPersisted,
                            nsIFile* aDirectory, bool aForGroup = false);

  using OriginInfosFlatTraversable =
      nsTArray<NotNull<RefPtr<const OriginInfo>>>;

  using OriginInfosNestedTraversable =
      nsTArray<nsTArray<NotNull<RefPtr<const OriginInfo>>>>;

  OriginInfosNestedTraversable GetOriginInfosExceedingGroupLimit() const;

  OriginInfosNestedTraversable GetOriginInfosExceedingGlobalLimit() const;

  void ClearOrigins(const OriginInfosNestedTraversable& aDoomedOriginInfos);

  void CleanupTemporaryStorage();

  void DeleteOriginDirectory(const OriginMetadata& aOriginMetadata);

  void FinalizeOriginEviction(nsTArray<RefPtr<OriginDirectoryLock>>&& aLocks);

  Result<Ok, nsresult> ArchiveOrigins(
      const nsTArray<FullOriginMetadata>& aFullOriginMetadatas);

  void ReleaseIOThreadObjects() {
    AssertIsOnIOThread();

    for (Client::Type type : AllClientTypes()) {
      (*mClients)[type]->ReleaseIOThreadObjects();
    }
  }

  DirectoryLockTable& GetDirectoryLockTable(PersistenceType aPersistenceType);

  void ClearDirectoryLockTables();

  void AddTemporaryOrigin(const FullOriginMetadata& aFullOriginMetadata);

  void RemoveTemporaryOrigin(const OriginMetadata& aOriginMetadata);

  void RemoveTemporaryOrigins(PersistenceType aPersistenceType);

  void RemoveTemporaryOrigins();

  /**
   * Retrieves the count of thumbnail private identity temporary origins.
   *
   * This method returns the current count of temporary origins associated with
   * thumbnail private identity contexts. It requires that the thumbnail
   * private identity id is known.
   *
   * @return The count of thumbnail private identity temporary origins.
   *
   * @note The thumbnail private identity id must be known before calling this
   *   method. If the id is not known, it will cause a debug assertion failure
   *   due to the `MOZ_ASSERT`.
   */
  uint32_t ThumbnailPrivateIdentityTemporaryOriginCount() const;

  PrincipalMetadataArray GetAllTemporaryGroups() const;

  OriginMetadataArray GetAllTemporaryOrigins() const;

  void NoteInitializedOrigin(PersistenceType aPersistenceType,
                             const nsACString& aOrigin);

  void NoteUninitializedOrigins(
      const OriginMetadataArray& aOriginMetadataArray);

  void NoteUninitializedRepository(PersistenceType aPersistenceType);

  bool IsOriginInitialized(PersistenceType aPersistenceType,
                           const nsACString& aOrigin) const;

  bool IsSanitizedOriginValid(const nsACString& aSanitizedOrigin);

  Result<nsCString, nsresult> EnsureStorageOriginFromOrigin(
      const nsACString& aOrigin);

  Result<nsCString, nsresult> GetOriginFromStorageOrigin(
      const nsACString& aStorageOrigin);

  int64_t GenerateDirectoryLockId();

  bool ShutdownStarted() const;

  void RecordShutdownStep(Maybe<Client::Type> aClientType,
                          const nsACString& aStepDescription);

  template <typename Func>
  auto ExecuteInitialization(Initialization aInitialization, Func&& aFunc)
      -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                        Initialization, StringGenerator>&>;

  template <typename Func>
  auto ExecuteInitialization(Initialization aInitialization,
                             const nsACString& aContext, Func&& aFunc)
      -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                        Initialization, StringGenerator>&>;

  template <typename Func>
  auto ExecuteGroupInitialization(const nsACString& aGroup,
                                  const GroupInitialization aInitialization,
                                  const nsACString& aContext, Func&& aFunc)
      -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                        Initialization, StringGenerator>&>;

  template <typename Func>
  auto ExecuteOriginInitialization(const nsACString& aOrigin,
                                   const OriginInitialization aInitialization,
                                   const nsACString& aContext, Func&& aFunc)
      -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                        Initialization, StringGenerator>&>;

  /**
   * Increments the counter tracking the total number of directory iterations.
   *
   * @note This is currently called only during clearing operations to update
   *       the mTotalDirectoryIterations member.
   */
  void IncreaseTotalDirectoryIterations();

  template <typename Iterator>
  static void MaybeInsertNonPersistedOriginInfos(
      Iterator aDest, const RefPtr<GroupInfo>& aTemporaryGroupInfo,
      const RefPtr<GroupInfo>& aDefaultGroupInfo,
      const RefPtr<GroupInfo>& aPrivateGroupInfo);

  template <typename Collect, typename Pred>
  static OriginInfosFlatTraversable CollectLRUOriginInfosUntil(
      Collect&& aCollect, Pred&& aPred);

  // Thread on which IO is performed.
  LazyInitializedOnceNotNull<const nsCOMPtr<nsIThread>> mIOThread;

  nsCOMPtr<mozIStorageConnection> mStorageConnection;

  EnumeratedArray<Client::Type, nsCString, size_t(Client::TYPE_MAX)>
      mShutdownSteps;
  LazyInitializedOnce<const TimeStamp> mShutdownStartedAt;

  // Accesses to mQuotaManagerShutdownSteps must be protected by mQuotaMutex.
  nsCString mQuotaManagerShutdownSteps;

  mutable mozilla::Mutex mQuotaMutex MOZ_UNANNOTATED;

  nsClassHashtable<nsCStringHashKey, GroupInfoPair> mGroupInfoPairs;

  // Maintains a list of directory locks that are queued.
  nsTArray<RefPtr<DirectoryLockImpl>> mPendingDirectoryLocks;

  // Maintains a list of directory locks that are acquired or queued. It can be
  // accessed on the owning (PBackground) thread only.
  nsTArray<NotNull<DirectoryLockImpl*>> mDirectoryLocks;

  // Only modifed on the owning thread, but read on multiple threads. Therefore
  // all modifications (including those on the owning thread) and all reads off
  // the owning thread must be protected by mQuotaMutex. In other words, only
  // reads on the owning thread don't have to be protected by mQuotaMutex.
  nsTHashMap<nsUint64HashKey, NotNull<DirectoryLockImpl*>>
      mDirectoryLockIdTable;

  // Directory lock tables that are used to update origin access time.
  DirectoryLockTable mTemporaryDirectoryLockTable;
  DirectoryLockTable mDefaultDirectoryLockTable;
  DirectoryLockTable mPrivateDirectoryLockTable;

  // Things touched on the owning (PBackground) thread only.
  struct BackgroundThreadAccessible {
    PrincipalMetadataArray mUninitializedGroups;
    nsTHashSet<nsCString> mInitializedGroups;
  };
  ThreadBound<BackgroundThreadAccessible> mBackgroundThreadAccessible;

  using BoolArray = AutoTArray<bool, PERSISTENCE_TYPE_INVALID>;
  nsTHashMap<nsCStringHashKeyWithDisabledMemmove, BoolArray>
      mInitializedOrigins;

  // Things touched on the IO thread only.
  struct IOThreadAccessible {
    nsTHashMap<nsCStringHashKey, nsTArray<FullOriginMetadata>>
        mAllTemporaryOrigins;
    Maybe<uint32_t> mThumbnailPrivateIdentityId;
    // Tracks the total number of directory iterations.
    // Note: This is currently incremented only during clearing operations.
    uint64_t mTotalDirectoryIterations = 0;
    // Tracks the count of thumbnail private identity temporary origins.
    uint32_t mThumbnailPrivateIdentityTemporaryOriginCount = 0;
  };
  ThreadBound<IOThreadAccessible> mIOThreadAccessible;

  // A list of all successfully initialized persistent origins. This list isn't
  // protected by any mutex but it is only ever touched on the IO thread.
  nsTArray<nsCString> mInitializedOriginsInternal;

  // A hash table that is used to cache origin parser results for given
  // sanitized origin strings. This hash table isn't protected by any mutex but
  // it is only ever touched on the IO thread.
  nsTHashMap<nsCStringHashKey, bool> mValidOrigins;

  // These maps are protected by mQuotaMutex.
  nsTHashMap<nsCStringHashKey, nsCString> mOriginToStorageOriginMap;
  nsTHashMap<nsCStringHashKey, nsCString> mStorageOriginToOriginMap;

  // This array is populated at initialization time and then never modified, so
  // it can be iterated on any thread.
  LazyInitializedOnce<const AutoTArray<RefPtr<Client>, Client::TYPE_MAX>>
      mClients;

  using ClientTypesArray = AutoTArray<Client::Type, Client::TYPE_MAX>;
  LazyInitializedOnce<const ClientTypesArray> mAllClientTypes;
  LazyInitializedOnce<const ClientTypesArray> mAllClientTypesExceptLS;

  // This object isn't protected by any mutex but it is only ever touched on
  // the IO thread.
  InitializationInfo mInitializationInfo;

  const nsString mBasePath;
  const nsString mStorageName;
  LazyInitializedOnce<const nsString> mIndexedDBPath;
  LazyInitializedOnce<const nsString> mStoragePath;
  LazyInitializedOnce<const nsString> mStorageArchivesPath;
  LazyInitializedOnce<const nsString> mPermanentStoragePath;
  LazyInitializedOnce<const nsString> mTemporaryStoragePath;
  LazyInitializedOnce<const nsString> mDefaultStoragePath;
  LazyInitializedOnce<const nsString> mPrivateStoragePath;
  LazyInitializedOnce<const nsString> mToBeRemovedStoragePath;

  MozPromiseHolder<BoolPromise> mInitializeAllTemporaryOriginsPromiseHolder;

  uint64_t mTemporaryStorageLimit;
  uint64_t mTemporaryStorageUsage;
  int64_t mNextDirectoryLockId;
  bool mStorageInitialized;
  bool mPersistentStorageInitialized;
  bool mPersistentStorageInitializedInternal;
  bool mTemporaryStorageInitialized;
  bool mTemporaryStorageInitializedInternal;
  bool mInitializingAllTemporaryOrigins;
  bool mAllTemporaryOriginsInitialized;
  bool mCacheUsable;
};

}  // namespace mozilla::dom::quota

#endif /* mozilla_dom_quota_quotamanager_h__ */
