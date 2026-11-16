/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DBAction.h"
#include "FileUtilsImpl.h"
#include "QuotaClientImpl.h"
#include "mozilla/GeckoTrace.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/dom/cache/DBSchema.h"
#include "mozilla/dom/cache/Manager.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/UsageInfo.h"
#include "mozilla/glean/DomCacheMetrics.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsIFile.h"
#include "nsThreadUtils.h"

namespace mozilla::dom::cache {

using mozilla::dom::quota::AssertIsOnIOThread;
using mozilla::dom::quota::Client;
using mozilla::dom::quota::CloneFileAndAppend;
using mozilla::dom::quota::DatabaseUsageType;
using mozilla::dom::quota::GetDirEntryKind;
using mozilla::dom::quota::nsIFileKind;
using mozilla::dom::quota::OriginMetadata;
using mozilla::dom::quota::PrincipalMetadata;
using mozilla::dom::quota::QuotaManager;
using mozilla::dom::quota::UsageInfo;
using mozilla::ipc::AssertIsOnBackgroundThread;

namespace {

constexpr bool IsNonFatalSchemaError(const nsresult aRv) {
  return aRv == NS_ERROR_STORAGE_BUSY || aRv == NS_ERROR_FILE_IS_LOCKED ||
         aRv == NS_ERROR_FILE_ACCESS_DENIED || aRv == NS_ERROR_ABORT ||
         aRv == NS_ERROR_FILE_NO_DEVICE_SPACE || aRv == NS_ERROR_OUT_OF_MEMORY;
}

template <typename StepFunc>
Result<UsageInfo, nsresult> ReduceUsageInfo(nsIFile& aDir,
                                            const Atomic<bool>& aCanceled,
                                            const StepFunc& aStepFunc) {
  QM_TRY_RETURN(quota::ReduceEachFileAtomicCancelable(
      aDir, aCanceled, UsageInfo{},
      [&aStepFunc](UsageInfo usageInfo, const nsCOMPtr<nsIFile>& bodyDir)
          -> Result<UsageInfo, nsresult> {
        QM_TRY(OkIf(!QuotaManager::IsShuttingDown()).mapErr([](const auto&) {
          return NS_ERROR_ABORT;
        }));

        QM_TRY_INSPECT(const auto& stepUsageInfo, aStepFunc(bodyDir));

        return usageInfo + stepUsageInfo;
      }));
}

Result<int64_t, nsresult> GetPaddingSizeFromDB(
    nsIFile& aDir, nsIFile& aDBFile, const OriginMetadata& aOriginMetadata,
    const Maybe<CipherKey>& aMaybeCipherKey) {
  CacheDirectoryMetadata directoryMetadata(aOriginMetadata);
  // directoryMetadata.mDirectoryLockId must be -1 (which is default for new
  // CacheDirectoryMetadata) because this method should only be called from
  // QuotaClient::InitOrigin when the temporary storage hasn't been initialized
  // yet. At that time, the in-memory objects (e.g. OriginInfo) are only being
  // created so it doesn't make sense to tunnel quota information to QuotaVFS
  // to get corresponding QuotaObject instance for the SQLite file).
  MOZ_DIAGNOSTIC_ASSERT(directoryMetadata.mDirectoryLockId == -1);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aDBFile, Exists));
    MOZ_ASSERT(exists);
  }
#endif

  QM_TRY_INSPECT(const auto& conn,
                 OpenDBConnection(directoryMetadata, aDBFile, aMaybeCipherKey));

  // Make sure that the database has the latest schema before we try to read
  // from it. We have to do this because GetPaddingSizeFromDB is called
  // by InitOrigin. And it means that SetupAction::RunSyncWithDBOnTarget hasn't
  // checked the schema for the given origin yet).
  QM_TRY(QM_OR_ELSE_WARN_IF(
      // Expression.
      MOZ_TO_RESULT(db::CreateOrMigrateSchema(aDir, *conn)),
      // Predicate.
      ([](const nsresult rv) { return !IsNonFatalSchemaError(rv); }),
      // Fallback.
      ([](const nsresult rv) -> Result<Ok, nsresult> {
        if (rv == NS_ERROR_DOM_NOT_SUPPORTED_ERR) {
          glean::cache::schema_init_error.Get("future_version"_ns).Add();
        } else {
          glean::cache::schema_init_error.Get("other"_ns).Add();
        }
        return Err(NS_ERROR_FILE_CORRUPTED);
      })));

  QM_TRY_RETURN(DirectoryPaddingRestore(aDir, *conn,
                                        /* aMustRestore */ false));
}

Result<int64_t, nsresult> GetTotalDiskUsageFromDB(
    nsIFile& aDir, nsIFile& aDBFile, const OriginMetadata& aOriginMetadata,
    const Maybe<CipherKey>& aMaybeCipherKey) {
  CacheDirectoryMetadata directoryMetadata(aOriginMetadata);
  // directoryMetadata.mDirectoryLockId must be -1 (which is default for new
  // CacheDirectoryMetadata) because this method should only be called from
  // QuotaClient::InitOrigin when the temporary storage hasn't been initialized
  // yet. At that time, the in-memory objects (e.g. OriginInfo) are only being
  // created so it doesn't make sense to tunnel quota information to QuotaVFS
  // to get corresponding QuotaObject instance for the SQLite file).
  MOZ_DIAGNOSTIC_ASSERT(directoryMetadata.mDirectoryLockId == -1);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aDBFile, Exists));
    MOZ_ASSERT(exists);
  }
#endif

  QM_TRY_INSPECT(const auto& conn,
                 OpenDBConnection(directoryMetadata, aDBFile, aMaybeCipherKey));

  // Make sure that the database has the latest schema before we try to read
  // from it. We have to do this because GetTotalDiskUsageFromDB is called
  // by InitOrigin. And it means that SetupAction::RunSyncWithDBOnTarget hasn't
  // checked the schema for the given origin yet).
  QM_TRY(QM_OR_ELSE_WARN_IF(
      // Expression.
      MOZ_TO_RESULT(db::CreateOrMigrateSchema(aDir, *conn)),
      // Predicate.
      ([](const nsresult rv) { return !IsNonFatalSchemaError(rv); }),
      // Fallback.
      ([](const nsresult rv) -> Result<Ok, nsresult> {
        if (rv == NS_ERROR_DOM_NOT_SUPPORTED_ERR) {
          glean::cache::schema_init_error.Get("future_version"_ns).Add();
        } else {
          glean::cache::schema_init_error.Get("other"_ns).Add();
        }
        return Err(NS_ERROR_FILE_CORRUPTED);
      })));

  QM_TRY_RETURN(db::GetTotalDiskUsage(*conn));
}

}  // namespace

CacheQuotaClient::CacheQuotaClient() {
  AssertIsOnBackgroundThread();
  MOZ_DIAGNOSTIC_ASSERT(!sInstance);
  sInstance = this;
}

// static
CacheQuotaClient* CacheQuotaClient::Get() {
  MOZ_DIAGNOSTIC_ASSERT(sInstance);
  return sInstance;
}

CacheQuotaClient::Type CacheQuotaClient::GetType() { return DOMCACHE; }

Result<UsageInfo, nsresult> CacheQuotaClient::InitOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  GECKO_TRACE_SCOPE("dom::cache", "CacheQuotaClient::InitOrigin");

  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == aPersistenceType);

  QuotaManager* const qm = QuotaManager::Get();
  MOZ_DIAGNOSTIC_ASSERT(qm);

  QM_TRY_INSPECT(const auto& dir, qm->GetOriginDirectory(aOriginMetadata));

  QM_TRY(MOZ_TO_RESULT(
      dir->Append(NS_LITERAL_STRING_FROM_CSTRING(DOMCACHE_DIRECTORY_NAME))));

  QM_TRY_INSPECT(
      const auto& cachesSQLiteFile,
      ([dir]() -> Result<nsCOMPtr<nsIFile>, nsresult> {
        QM_TRY_INSPECT(const auto& cachesSQLite,
                       CloneFileAndAppend(*dir, kCachesSQLiteFilename));

        // IsDirectory is used to check if caches.sqlite exists or not. Another
        // benefit of this is that we can test the failed cases by creating a
        // directory named "caches.sqlite".
        QM_TRY_INSPECT(const auto& dirEntryKind,
                       GetDirEntryKind(*cachesSQLite));
        if (dirEntryKind == nsIFileKind::DoesNotExist) {
          // We only ensure padding files and morgue directory get removed like
          // WipeDatabase in DBAction.cpp. The -wal journal file will be
          // automatically deleted by sqlite when the new database is created.
          // XXX Ideally, we would delete the -wal journal file as well (here
          // and also in WipeDatabase).
          // XXX We should have something like WipeDatabaseNoQuota for this.
          // XXX Long term, we might even think about removing entire origin
          // directory because missing caches.sqlite while other files exist can
          // be interpreted as database corruption.
          QM_TRY(MOZ_TO_RESULT(mozilla::dom::cache::DirectoryPaddingDeleteFile(
              *dir, DirPaddingFile::TMP_FILE)));

          QM_TRY(MOZ_TO_RESULT(mozilla::dom::cache::DirectoryPaddingDeleteFile(
              *dir, DirPaddingFile::FILE)));

          QM_TRY_INSPECT(const auto& morgueDir,
                         CloneFileAndAppend(*dir, kMorgueDirectory));

          QM_TRY(MOZ_TO_RESULT(mozilla::dom::cache::RemoveNsIFileRecursively(
              Nothing(), *morgueDir,
              /* aTrackQuota */ false)));

          return nsCOMPtr<nsIFile>{nullptr};
        }

        QM_TRY(OkIf(dirEntryKind == nsIFileKind::ExistsAsFile),
               Err(NS_ERROR_FAILURE));

        return cachesSQLite;
      }()));

  // If the caches.sqlite doesn't exist, then padding files and morgue directory
  // should have been removed if they existed. We ignore the rest of known files
  // because we assume that they will be removed when a new database is created.
  // XXX Ensure the -wel file is removed if the caches.sqlite doesn't exist.
  QM_TRY(OkIf(!!cachesSQLiteFile), UsageInfo{});

  QM_TRY_UNWRAP(
      const auto maybeCipherKey,
      ([this, &aOriginMetadata]() -> Result<Maybe<CipherKey>, nsresult> {
        QM_TRY_UNWRAP(auto cipherKeyManager,
                      GetOrCreateCipherKeyManager(aOriginMetadata));
        if (cipherKeyManager) {
          QM_TRY_UNWRAP(auto key, cipherKeyManager->Ensure("db"_ns));
          return Some(key);
        }
        return Maybe<CipherKey>{};
      }()));

  QM_TRY_INSPECT(
      const auto& paddingSize,
      ([dir, cachesSQLiteFile, &aOriginMetadata,
        &maybeCipherKey]() -> Result<int64_t, nsresult> {
        if (!DirectoryPaddingFileExists(*dir, DirPaddingFile::TMP_FILE)) {
          QM_WARNONLY_TRY_UNWRAP(const auto maybePaddingSize,
                                 DirectoryPaddingGet(*dir));
          if (maybePaddingSize) {
            return maybePaddingSize.ref();
          }
        }

        // If the temporary file still exists or failing to get the padding size
        // from the padding file, then we need to get the padding size from the
        // database and restore the padding file.
        QM_TRY_RETURN(GetPaddingSizeFromDB(*dir, *cachesSQLiteFile,
                                           aOriginMetadata, maybeCipherKey));
      }()));

  QM_TRY_INSPECT(const auto& totalDiskUsage,
                 GetTotalDiskUsageFromDB(*dir, *cachesSQLiteFile,
                                         aOriginMetadata, maybeCipherKey));

  QM_TRY_INSPECT(
      const auto& innerUsageInfo,
      ReduceUsageInfo(
          *dir, aCanceled,
          [](const nsCOMPtr<nsIFile>& file) -> Result<UsageInfo, nsresult> {
            QM_TRY_INSPECT(const auto& leafName,
                           MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, file,
                                                             GetLeafName));

            QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*file));

            switch (dirEntryKind) {
              case nsIFileKind::ExistsAsDirectory:
                if (!leafName.Equals(kMorgueDirectory)) {
                  NS_WARNING("Unknown Cache directory found!");
                }

                break;

              case nsIFileKind::ExistsAsFile:
                // Ignore transient sqlite files and marker files
                if (leafName.EqualsLiteral("caches.sqlite-journal") ||
                    leafName.EqualsLiteral("caches.sqlite-shm") ||
                    StringBeginsWith(leafName, u"caches.sqlite-mj"_ns) ||
                    leafName.EqualsLiteral("context_open.marker")) {
                  break;
                }

                if (leafName.Equals(kCachesSQLiteFilename) ||
                    leafName.EqualsLiteral("caches.sqlite-wal")) {
                  QM_TRY_INSPECT(
                      const int64_t& fileSize,
                      MOZ_TO_RESULT_INVOKE_MEMBER(file, GetFileSize));
                  MOZ_DIAGNOSTIC_ASSERT(fileSize >= 0);

                  return UsageInfo{DatabaseUsageType(Some(fileSize))};
                }

                // Ignore directory padding file
                if (leafName.Equals(kPaddingFilename) ||
                    leafName.Equals(kPaddingTmpFilename)) {
                  break;
                }

                NS_WARNING("Unknown Cache file found!");

                break;

              case nsIFileKind::DoesNotExist:
                // Ignore files that got removed externally while iterating.
                break;
            }

            return UsageInfo{};
          }));

  // FIXME: Separate file usage and database usage in OriginInfo so that the
  // workaround for treating padding file size as database usage can be removed.
  return UsageInfo{DatabaseUsageType(Some(paddingSize))} +
         UsageInfo{DatabaseUsageType(Some(totalDiskUsage))} + innerUsageInfo;
}

nsresult CacheQuotaClient::InitOriginWithoutTracking(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  // This is called when a storage/permanent/${origin}/cache directory exists.
  // Even though this shouldn't happen with a "good" profile, we shouldn't
  // return an error here, since that would cause origin initialization to fail.
  // We just warn and otherwise ignore that.
  UNKNOWN_FILE_WARNING(NS_LITERAL_STRING_FROM_CSTRING(DOMCACHE_DIRECTORY_NAME));
  return NS_OK;
}

Result<UsageInfo, nsresult> CacheQuotaClient::GetUsageForOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  // We can't open the database at this point, since it can be already used by
  // the Cache IO thread. Use the cached value instead.

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  return quotaManager->GetUsageForClient(aOriginMetadata.mPersistenceType,
                                         aOriginMetadata, Client::DOMCACHE);
}

void CacheQuotaClient::OnOriginClearCompleted(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  if (aOriginMetadata.mPersistenceType == quota::PERSISTENCE_TYPE_PRIVATE) {
    if (auto entry = mCipherKeyManagers.Lookup(aOriginMetadata.mOrigin)) {
      entry.Data()->Invalidate();
      entry.Remove();
    }
  }
}

void CacheQuotaClient::OnRepositoryClearCompleted(
    PersistenceType aPersistenceType) {
  AssertIsOnIOThread();

  if (aPersistenceType == quota::PERSISTENCE_TYPE_PRIVATE) {
    for (const auto& cipherKeyManager : mCipherKeyManagers.Values()) {
      cipherKeyManager->Invalidate();
    }

    mCipherKeyManagers.Clear();
  }
}

void CacheQuotaClient::ReleaseIOThreadObjects() {
  // Nothing to do here as the Context handles cleaning everything up
  // automatically.
}

void CacheQuotaClient::AbortOperationsForLocks(
    const DirectoryLockIdTable& aDirectoryLockIds) {
  AssertIsOnBackgroundThread();

  Manager::Abort(aDirectoryLockIds);
}

void CacheQuotaClient::AbortOperationsForProcess(
    ContentParentId aContentParentId) {
  // The Cache and Context can be shared by multiple client processes.  They
  // are not exclusively owned by a single process.
  //
  // As far as I can tell this is used by QuotaManager to abort operations
  // when a particular process goes away.  We definitely don't want this
  // since we are shared.  Also, the Cache actor code already properly
  // handles asynchronous actor destruction when the child process dies.
  //
  // Therefore, do nothing here.
}

void CacheQuotaClient::AbortAllOperations() {
  AssertIsOnBackgroundThread();

  Manager::AbortAll();
}

void CacheQuotaClient::StartIdleMaintenance() {}

void CacheQuotaClient::StopIdleMaintenance() {}

void CacheQuotaClient::InitiateShutdown() {
  AssertIsOnBackgroundThread();

  Manager::InitiateShutdown();
}

bool CacheQuotaClient::IsShutdownCompleted() const {
  AssertIsOnBackgroundThread();

  return Manager::IsShutdownAllComplete();
}

void CacheQuotaClient::ForceKillActors() {
  // Currently we don't implement killing actors (are there any to kill here?).
}

nsCString CacheQuotaClient::GetShutdownStatus() const {
  AssertIsOnBackgroundThread();

  return Manager::GetShutdownStatus();
}

void CacheQuotaClient::FinalizeShutdown() {
  // Nothing to do here.
}

nsresult CacheQuotaClient::UpgradeStorageFrom2_0To2_1(nsIFile* aDirectory) {
  AssertIsOnIOThread();
  MOZ_DIAGNOSTIC_ASSERT(aDirectory);

  QM_TRY(MOZ_TO_RESULT(DirectoryPaddingInit(*aDirectory)));

  return NS_OK;
}

nsresult CacheQuotaClient::RestorePaddingFileInternal(
    nsIFile* aBaseDir, mozIStorageConnection* aConn) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aBaseDir);
  MOZ_DIAGNOSTIC_ASSERT(aConn);

  QM_TRY_INSPECT(const int64_t& dummyPaddingSize,
                 DirectoryPaddingRestore(*aBaseDir, *aConn,
                                         /* aMustRestore */ true));
  (void)dummyPaddingSize;

  return NS_OK;
}

nsresult CacheQuotaClient::WipePaddingFileInternal(
    const CacheDirectoryMetadata& aDirectoryMetadata, nsIFile* aBaseDir) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aBaseDir);

  MOZ_ASSERT(DirectoryPaddingFileExists(*aBaseDir, DirPaddingFile::FILE));

  QM_TRY_INSPECT(
      const int64_t& paddingSize, ([&aBaseDir]() -> Result<int64_t, nsresult> {
        const bool temporaryPaddingFileExist =
            DirectoryPaddingFileExists(*aBaseDir, DirPaddingFile::TMP_FILE);

        Maybe<int64_t> directoryPaddingGetResult;
        if (!temporaryPaddingFileExist) {
          QM_TRY_UNWRAP(directoryPaddingGetResult,
                        ([&aBaseDir]() -> Result<Maybe<int64_t>, nsresult> {
                          QM_TRY_RETURN(
                              DirectoryPaddingGet(*aBaseDir).map(Some<int64_t>),
                              Maybe<int64_t>{});
                        }()));
        }

        if (temporaryPaddingFileExist || !directoryPaddingGetResult) {
          // XXXtt: Maybe have a method in the QuotaManager to clean the usage
          // under the quota client and the origin. There is nothing we can do
          // to recover the file.
          NS_WARNING("Cannnot read padding size from file!");
          return 0;
        }

        return *directoryPaddingGetResult;
      }()));

  if (paddingSize > 0) {
    DecreaseUsageForDirectoryMetadata(aDirectoryMetadata, paddingSize);
  }

  QM_TRY(MOZ_TO_RESULT(
      DirectoryPaddingDeleteFile(*aBaseDir, DirPaddingFile::FILE)));

  // Remove temporary file if we have one.
  QM_TRY(MOZ_TO_RESULT(
      DirectoryPaddingDeleteFile(*aBaseDir, DirPaddingFile::TMP_FILE)));

  QM_TRY(MOZ_TO_RESULT(DirectoryPaddingInit(*aBaseDir)));

  return NS_OK;
}

Result<RefPtr<CipherKeyManager>, nsresult>
CacheQuotaClient::GetOrCreateCipherKeyManager(
    const PrincipalMetadata& aMetadata) {
  AssertIsOnIOThread();

  const auto& origin = aMetadata.mOrigin;
  if (aMetadata.mIsPrivate) {
    return RefPtr<CipherKeyManager>(
        mCipherKeyManagers.LookupOrInsertWith(origin, [&] {
          return CipherKeyManager::CreateEphemeral("CacheCipherKeyManager");
        }));
  }

  if (!aMetadata.mIsEncrypted) {
    return RefPtr<CipherKeyManager>(nullptr);
  }

  if (auto entry = mCipherKeyManagers.Lookup(origin)) {
    return RefPtr<CipherKeyManager>(entry.Data());
  }

  QM_TRY_UNWRAP(auto cipherKeyManager,
                quota::QuotaManager::Get()->GetOrCreateCipherKeyManager(
                    origin, quota::Client::DOMCACHE));
  mCipherKeyManagers.InsertOrUpdate(origin, cipherKeyManager);
  return cipherKeyManager;
}

CacheQuotaClient::~CacheQuotaClient() {
  AssertIsOnBackgroundThread();
  MOZ_DIAGNOSTIC_ASSERT(sInstance == this);

  sInstance = nullptr;
}

// static
CacheQuotaClient* CacheQuotaClient::sInstance = nullptr;

// static
already_AddRefed<quota::Client> CreateQuotaClient() {
  AssertIsOnBackgroundThread();

  RefPtr<CacheQuotaClient> ref = new CacheQuotaClient();
  return ref.forget();
}

// static
nsresult RestorePaddingFile(nsIFile* aBaseDir, mozIStorageConnection* aConn) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aBaseDir);
  MOZ_DIAGNOSTIC_ASSERT(aConn);

  RefPtr<CacheQuotaClient> cacheQuotaClient = CacheQuotaClient::Get();
  MOZ_DIAGNOSTIC_ASSERT(cacheQuotaClient);

  QM_TRY(MOZ_TO_RESULT(
      cacheQuotaClient->RestorePaddingFileInternal(aBaseDir, aConn)));

  return NS_OK;
}

// static
nsresult WipePaddingFile(const CacheDirectoryMetadata& aDirectoryMetadata,
                         nsIFile* aBaseDir) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_DIAGNOSTIC_ASSERT(aBaseDir);

  RefPtr<CacheQuotaClient> cacheQuotaClient = CacheQuotaClient::Get();
  MOZ_DIAGNOSTIC_ASSERT(cacheQuotaClient);

  QM_TRY(MOZ_TO_RESULT(
      cacheQuotaClient->WipePaddingFileInternal(aDirectoryMetadata, aBaseDir)));

  return NS_OK;
}

}  // namespace mozilla::dom::cache
