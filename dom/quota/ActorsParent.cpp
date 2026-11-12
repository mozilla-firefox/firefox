/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ActorsParent.h"

// Local includes
#include "Assertions.h"
#include "CanonicalQuotaObject.h"
#include "ClientUsageArray.h"
#include "DirectoryMetadata.h"
#include "DirtyTrackingAutoLock.h"
#include "FirstInitializationAttemptsImpl.h"
#include "Flatten.h"
#include "GroupInfo.h"
#include "GroupInfoPair.h"
#include "GroupInfoPairImpl.h"
#include "NormalOriginOperationBase.h"
#include "OpenClientDirectoryUtils.h"
#include "OriginCacheMap.h"
#include "OriginInfo.h"
#include "OriginOperationBase.h"
#include "OriginOperations.h"
#include "OriginParser.h"
#include "OriginScope.h"
#include "OriginUpserter.h"
#include "QuotaCommon.h"
#include "QuotaManager.h"
#include "QuotaPrefs.h"
#include "ResolvableNormalOriginOp.h"
#include "SanitizationUtils.h"
#include "ScopedLogExtraInfo.h"
#include "UsageInfo.h"

// Global includes
#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <utility>

#include "DirectoryLockImpl.h"
#include "ErrorList.h"
#include "MainThreadUtils.h"
#include "mozIStorageAsyncConnection.h"
#include "mozIStorageConnection.h"
#include "mozIStorageService.h"
#include "mozIStorageStatement.h"
#include "mozStorageCID.h"
#include "mozStorageHelper.h"
#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/CondVar.h"
#include "mozilla/GeckoTrace.h"
#include "mozilla/InitializedOnce.h"
#include "mozilla/Logging.h"
#include "mozilla/MathAlgorithms.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/NotNull.h"
#include "mozilla/Now.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StorageOriginAttributes.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/SystemPrincipal.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/FileSystemQuotaClientFactory.h"
#include "mozilla/dom/FlippedOnce.h"
#include "mozilla/dom/IndexedDatabaseManager.h"
#include "mozilla/dom/LocalStorageCommon.h"
#include "mozilla/dom/StorageDBUpdater.h"
#include "mozilla/dom/cache/QuotaClient.h"
#include "mozilla/dom/indexedDB/ActorsParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/localstorage/ActorsParent.h"
#include "mozilla/dom/quota/ArtificialFailure.h"
#include "mozilla/dom/quota/AssertionsImpl.h"
#include "mozilla/dom/quota/CheckedUnsafePtr.h"
#include "mozilla/dom/quota/CipherKeyManager.h"
#include "mozilla/dom/quota/CipherKeyStoreBackend.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/Config.h"
#include "mozilla/dom/quota/Constants.h"
#include "mozilla/dom/quota/Date.h"
#include "mozilla/dom/quota/DekNameCodec.h"
#include "mozilla/dom/quota/DirectoryLockInlines.h"
#include "mozilla/dom/quota/FileUtils.h"
#include "mozilla/dom/quota/LockstoreStorageKeyProvider.h"
#include "mozilla/dom/quota/MozPromiseUtils.h"
#include "mozilla/dom/quota/NSSCipherStrategy.h"
#include "mozilla/dom/quota/OriginDirectoryLock.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "mozilla/dom/quota/PrincipalUtils.h"
#include "mozilla/dom/quota/QuotaManagerImpl.h"
#include "mozilla/dom/quota/QuotaManagerService.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/dom/quota/ScopedLogExtraInfo.h"
#include "mozilla/dom/quota/StreamUtils.h"
#include "mozilla/dom/quota/ThreadUtils.h"
#include "mozilla/dom/quota/UniversalDirectoryLock.h"
#include "mozilla/dom/simpledb/ActorsParent.h"
#include "mozilla/fallible.h"
#include "mozilla/glean/DomQuotaMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/net/ExtensionProtocolHandler.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsBaseHashtable.h"
#include "nsCOMPtr.h"
#include "nsCRTGlue.h"
#include "nsClassHashtable.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsDirectoryServiceUtils.h"
#include "nsError.h"
#include "nsIBinaryInputStream.h"
#include "nsIBinaryOutputStream.h"
#include "nsIClearDataService.h"
#include "nsIConsoleService.h"
#include "nsIDUtils.h"
#include "nsIDirectoryEnumerator.h"
#include "nsIEventTarget.h"
#include "nsIFile.h"
#include "nsIFileStreams.h"
#include "nsIInputStream.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIOutputStream.h"
#include "nsIPlatformInfo.h"
#include "nsIPrincipal.h"
#include "nsIQuotaCallbacks.h"
#include "nsIQuotaManagerServiceInternal.h"
#include "nsIQuotaRequests.h"
#include "nsIQuotaUtilsService.h"
#include "nsIRunnable.h"
#include "nsISupports.h"
#include "nsIThread.h"
#include "nsITimer.h"
#include "nsIURI.h"
#include "nsIWidget.h"
#include "nsLiteralString.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"
#include "nsStandardURL.h"
#include "nsString.h"
#include "nsStringFlags.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsTHashtable.h"
#include "nsTLiteralString.h"
#include "nsTPromiseFlatString.h"
#include "nsTStringRepr.h"
#include "nsThreadUtils.h"
#include "nsURLHelper.h"
#include "nsXPCOM.h"
#include "nsXPCOMCID.h"
#include "nsXULAppAPI.h"
#include "prinrval.h"
#include "prio.h"
#include "prtime.h"

// The amount of time, in milliseconds, that our IO thread will stay alive
// after the last event it processes.
#define DEFAULT_THREAD_TIMEOUT_MS 30000

/**
 * If shutdown takes this long, kill actors of a quota client, to avoid reaching
 * the crash timeout.
 */
#define SHUTDOWN_KILL_ACTORS_TIMEOUT_MS 5000

/**
 * Automatically crash the browser if shutdown of a quota client takes this
 * long. We've chosen a value that is long enough that it is unlikely for the
 * problem to be falsely triggered by slow system I/O.  We've also chosen a
 * value long enough so that automated tests should time out and fail if
 * shutdown of a quota client takes too long.  Also, this value is long enough
 * so that testers can notice the timeout; we want to know about the timeouts,
 * not hide them. On the other hand this value is less than 60 seconds which is
 * used by nsTerminator to crash a hung main process.
 */
#define SHUTDOWN_CRASH_BROWSER_TIMEOUT_MS 45000

static_assert(
    SHUTDOWN_CRASH_BROWSER_TIMEOUT_MS > SHUTDOWN_KILL_ACTORS_TIMEOUT_MS,
    "The kill actors timeout must be shorter than the crash browser one.");

// profile-before-change, when we need to shut down quota manager
#define PROFILE_BEFORE_CHANGE_QM_OBSERVER_ID "profile-before-change-qm"

#define KB *1024ULL
#define MB *1024ULL KB
#define GB *1024ULL MB

namespace mozilla::dom::quota {

using namespace mozilla::ipc;

// We want profiles to be platform-independent so we always need to replace
// the same characters on every platform. Windows has the most extensive set
// of illegal characters so we use its FILE_ILLEGAL_CHARACTERS and
// FILE_PATH_SEPARATOR.
const char QuotaManager::kReplaceChars[] = CONTROL_CHARACTERS "/:*?\"<>|\\";
const char16_t QuotaManager::kReplaceChars16[] =
    u"" CONTROL_CHARACTERS "/:*?\"<>|\\";

namespace {

/*******************************************************************************
 * Constants
 ******************************************************************************/

const uint32_t kSQLitePageSizeOverride = 4096;

// Important version history:
// - Bug 1290481 bumped our schema from major.minor 2.0 to 3.0 in Firefox 57
//   which caused Firefox 57 release concerns because the major schema upgrade
//   means anyone downgrading to Firefox 56 will experience a non-operational
//   QuotaManager and all of its clients.
// - Bug 1404344 got very concerned about that and so we decided to effectively
//   rename 3.0 to 2.1, effective in Firefox 57.  This works because post
//   storage.sqlite v1.0, QuotaManager doesn't care about minor storage version
//   increases.  It also works because all the upgrade did was give the DOM
//   Cache API QuotaClient an opportunity to create its newly added .padding
//   files during initialization/upgrade, which isn't functionally necessary as
//   that can be done on demand.

// Major storage version. Bump for backwards-incompatible changes.
// (The next major version should be 4 to distinguish from the Bug 1290481
// downgrade snafu.)
const uint32_t kMajorStorageVersion = 2;

// Minor storage version. Bump for backwards-compatible changes.
const uint32_t kMinorStorageVersion = 4;

// The storage version we store in the SQLite database is a (signed) 32-bit
// integer. The major version is left-shifted 16 bits so the max value is
// 0xFFFF. The minor version occupies the lower 16 bits and its max is 0xFFFF.
static_assert(kMajorStorageVersion <= 0xFFFF,
              "Major version needs to fit in 16 bits.");
static_assert(kMinorStorageVersion <= 0xFFFF,
              "Minor version needs to fit in 16 bits.");

const int32_t kStorageVersion =
    int32_t((kMajorStorageVersion << 16) + kMinorStorageVersion);

// See comments above about why these are a thing.
const int32_t kHackyPreDowngradeStorageVersion = int32_t((3 << 16) + 0);
const int32_t kHackyPostDowngradeStorageVersion = int32_t((2 << 16) + 1);

const char kIndexedDBOriginPrefix[] = "indexeddb://";
const char kResourceOriginPrefix[] = "resource://";

constexpr auto kStorageName = u"storage"_ns;

#define INDEXEDDB_DIRECTORY_NAME u"indexedDB"
#define ARCHIVES_DIRECTORY_NAME u"archives"
#define PERSISTENT_DIRECTORY_NAME u"persistent"
#define PERMANENT_DIRECTORY_NAME u"permanent"
#define TEMPORARY_DIRECTORY_NAME u"temporary"
#define DEFAULT_DIRECTORY_NAME u"default"
#define PRIVATE_DIRECTORY_NAME u"private"
#define TOBEREMOVED_DIRECTORY_NAME u"to-be-removed"

#define WEB_APPS_STORE_FILE_NAME u"webappsstore.sqlite"
#define LS_ARCHIVE_FILE_NAME u"ls-archive.sqlite"
#define LS_ARCHIVE_TMP_FILE_NAME u"ls-archive-tmp.sqlite"

const int32_t kLocalStorageArchiveVersion = 4;

const char kProfileDoChangeTopic[] = "profile-do-change";
const char kContextualIdentityServiceLoadFinishedTopic[] =
    "contextual-identity-service-load-finished";
const char kPrivateBrowsingObserverTopic[] = "last-pb-context-exited";

const int32_t kCacheVersion = 4;

// Sentinel value written at the end of the metadata file to indicate that the
// file includes the extended origin metadata format. The sentinel allows us to
// safely distinguish upgraded files from older versions and maintain backward
// compatibility without requiring a full storage migration. Older files that
// do not contain the sentinel will be handled using a separate code path to
// fill the new fields with default or safe values.
const uint32_t kMetadataSentinel = 0xABCD1234;

/******************************************************************************
 * SQLite functions
 ******************************************************************************/

int32_t MakeStorageVersion(uint32_t aMajorStorageVersion,
                           uint32_t aMinorStorageVersion) {
  return int32_t((aMajorStorageVersion << 16) + aMinorStorageVersion);
}

uint32_t GetMajorStorageVersion(int32_t aStorageVersion) {
  return uint32_t(aStorageVersion >> 16);
}

nsresult CreateTables(mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  // Table `database`
  QM_TRY(MOZ_TO_RESULT(
      aConnection->ExecuteSimpleSQL("CREATE TABLE database"
                                    "( cache_version INTEGER NOT NULL DEFAULT 0"
                                    ");"_ns)));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const int32_t& storageVersion,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, GetSchemaVersion));

    MOZ_ASSERT(storageVersion == 0);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(aConnection->SetSchemaVersion(kStorageVersion)));

  return NS_OK;
}

Result<int32_t, nsresult> LoadCacheVersion(mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement<
                     SingleStepResult::ReturnNullIfNoResult>(
                     aConnection, "SELECT cache_version FROM database"_ns));

  QM_TRY(OkIf(stmt), Err(NS_ERROR_FILE_CORRUPTED));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 0));
}

nsresult SaveCacheVersion(mozIStorageConnection& aConnection,
                          int32_t aVersion) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
          "UPDATE database SET cache_version = :version;"_ns));

  QM_TRY(MOZ_TO_RESULT(stmt->BindInt32ByName("version"_ns, aVersion)));

  QM_TRY(MOZ_TO_RESULT(stmt->Execute()));

  return NS_OK;
}

nsresult CreateCacheTables(mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  // Table `cache`
  QM_TRY(MOZ_TO_RESULT(
      aConnection.ExecuteSimpleSQL("CREATE TABLE cache"
                                   "( valid INTEGER NOT NULL DEFAULT 0"
                                   ", build_id TEXT NOT NULL DEFAULT ''"
                                   ");"_ns)));

  // Table `repository`
  QM_TRY(
      MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL("CREATE TABLE repository"
                                                 "( id INTEGER PRIMARY KEY"
                                                 ", name TEXT NOT NULL"
                                                 ");"_ns)));

  // Table `origin`
  QM_TRY(MOZ_TO_RESULT(
      aConnection.ExecuteSimpleSQL("CREATE TABLE origin"
                                   "( repository_id INTEGER NOT NULL"
                                   ", suffix TEXT"
                                   ", group_ TEXT NOT NULL"
                                   ", origin TEXT NOT NULL"
                                   ", client_usages TEXT NOT NULL"
                                   ", usage INTEGER NOT NULL"
                                   ", last_access_time INTEGER NOT NULL"
                                   ", last_maintenance_date INTEGER NOT NULL"
                                   ", metadata_flags INTEGER NOT NULL"
                                   ", PRIMARY KEY (repository_id, origin)"
                                   ", FOREIGN KEY (repository_id) "
                                   "REFERENCES repository(id) "
                                   ");"_ns)));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const int32_t& cacheVersion, LoadCacheVersion(aConnection));
    MOZ_ASSERT(cacheVersion == 0);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(SaveCacheVersion(aConnection, kCacheVersion)));

  return NS_OK;
}

OkOrErr InvalidateCache(mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  QM_TRY(QM_TO_RESULT(aConnection.ExecuteSimpleSQL("DELETE FROM origin;"_ns)));

  return Ok{};
}

nsresult UpgradeCacheFrom1To2(mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
      "ALTER TABLE origin ADD COLUMN suffix TEXT"_ns)));

  QM_TRY(InvalidateCache(aConnection));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const int32_t& cacheVersion, LoadCacheVersion(aConnection));

    MOZ_ASSERT(cacheVersion == 1);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(SaveCacheVersion(aConnection, 2)));

  return NS_OK;
}

nsresult UpgradeCacheFrom2To3(mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  QM_TRY(InvalidateCache(aConnection));

  QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
      "ALTER TABLE origin ADD COLUMN last_maintenance_date INTEGER NOT NULL"_ns)));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const int32_t& cacheVersion, LoadCacheVersion(aConnection));

    MOZ_ASSERT(cacheVersion == 2);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(SaveCacheVersion(aConnection, 3)));

  return NS_OK;
}

Result<bool, nsresult> DidLatestShutdownFail(
    mozIStorageConnection& aConnection);

Result<bool, nsresult> UpgradeCacheFrom3To4(
    mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const int32_t& cacheVersion, LoadCacheVersion(aConnection));

    MOZ_ASSERT(cacheVersion == 3);
  }
#endif

  // Check for unclean shutdown BEFORE renaming columns, because
  // DidLatestShutdownFail references the old column names.
  QM_TRY_UNWRAP(const bool lastShutdownFailed,
                DidLatestShutdownFail(aConnection));

  QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(nsLiteralCString(
      "ALTER TABLE origin RENAME COLUMN accessed TO metadata_flags;"))));

  if (lastShutdownFailed) {
    // 5 = Initialized (bit 0) | Dirty (bit 2)
    QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
        nsLiteralCString("UPDATE origin SET metadata_flags = "
                         "5 | (metadata_flags << 1) | (persisted << 3);"))));
  } else {
    QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
        nsLiteralCString("UPDATE origin SET metadata_flags = "
                         "1 | (metadata_flags << 1) | (persisted << 3);"))));
  }

  QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
      nsLiteralCString("ALTER TABLE origin DROP COLUMN persisted;"))));

  QM_TRY(MOZ_TO_RESULT(SaveCacheVersion(aConnection, 4)));

  return lastShutdownFailed;
}

struct CacheSetupInfo {
  bool usable;
  bool requiresFullScan;
};

Result<CacheSetupInfo, nsresult> MaybeCreateOrUpgradeCache(
    mozIStorageConnection& aConnection) {
  GECKO_TRACE_SCOPE("dom::quota", "MaybeCreateOrUpgradeCache");

  bool cacheUsable = true;
  bool newlyCreated = false;

  QM_TRY_UNWRAP(int32_t cacheVersion, LoadCacheVersion(aConnection));

  if (cacheVersion > kCacheVersion) {
    cacheUsable = false;
  } else if (cacheVersion != kCacheVersion) {
    const bool newCache = !cacheVersion;

    mozStorageTransaction transaction(
        &aConnection, false, mozIStorageConnection::TRANSACTION_IMMEDIATE);

    QM_TRY(MOZ_TO_RESULT(transaction.Start()));

    if (newCache) {
      newlyCreated = true;

      QM_TRY(MOZ_TO_RESULT(CreateCacheTables(aConnection)));

#ifdef DEBUG
      {
        QM_TRY_INSPECT(const int32_t& cacheVersion,
                       LoadCacheVersion(aConnection));
        MOZ_ASSERT(cacheVersion == kCacheVersion);
      }
#endif

      QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
          nsLiteralCString("INSERT INTO cache (valid, build_id) "
                           "VALUES (0, '')"))));

      nsCOMPtr<mozIStorageStatement> insertStmt;

      for (const PersistenceType persistenceType : kAllPersistenceTypes) {
        if (insertStmt) {
          MOZ_ALWAYS_SUCCEEDS(insertStmt->Reset());
        } else {
          QM_TRY_UNWRAP(insertStmt, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                        nsCOMPtr<mozIStorageStatement>,
                                        aConnection, CreateStatement,
                                        "INSERT INTO repository (id, name) "
                                        "VALUES (:id, :name)"_ns));
        }

        QM_TRY(MOZ_TO_RESULT(
            insertStmt->BindInt32ByName("id"_ns, persistenceType)));

        QM_TRY(MOZ_TO_RESULT(insertStmt->BindUTF8StringByName(
            "name"_ns, PersistenceTypeToString(persistenceType))));

        QM_TRY(MOZ_TO_RESULT(insertStmt->Execute()));
      }

      // An empty origin table would be trusted as "no origins exist," missing
      // all origins already on disk. Force InitializeRepository to scan.
      cacheUsable = false;
    } else {
      // This logic needs to change next time we change the cache!
      static_assert(kCacheVersion == 4,
                    "Upgrade function needed due to cache version increase.");

      while (cacheVersion != kCacheVersion) {
        if (cacheVersion == 1) {
          QM_TRY(MOZ_TO_RESULT(UpgradeCacheFrom1To2(aConnection)));
          cacheUsable = false;
        } else if (cacheVersion == 2) {
          QM_TRY(MOZ_TO_RESULT(UpgradeCacheFrom2To3(aConnection)));
          cacheUsable = false;
        } else if (cacheVersion == 3) {
          QM_TRY_UNWRAP(const bool lastShutdownFailed,
                        UpgradeCacheFrom3To4(aConnection));
          if (lastShutdownFailed) {
            cacheUsable = false;
          }
        } else {
          QM_FAIL(Err(NS_ERROR_FAILURE), []() {
            QM_WARNING(
                "Unable to initialize cache, no upgrade path is "
                "available!");
          });
        }

        QM_TRY_UNWRAP(cacheVersion, LoadCacheVersion(aConnection));
      }

      MOZ_ASSERT(cacheVersion == kCacheVersion);
    }

    QM_TRY(MOZ_TO_RESULT(transaction.Commit()));
  }

  return CacheSetupInfo{cacheUsable, newlyCreated};
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult> CreateWebAppsStoreConnection(
    nsIFile& aWebAppsStoreFile, mozIStorageService& aStorageService) {
  AssertIsOnIOThread();

  // Check if the old database exists at all.
  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aWebAppsStoreFile, Exists));

  if (!exists) {
    // webappsstore.sqlite doesn't exist, return a null connection.
    return nsCOMPtr<mozIStorageConnection>{};
  }

  QM_TRY_INSPECT(const bool& isDirectory,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aWebAppsStoreFile, IsDirectory));

  if (isDirectory) {
    QM_WARNING("webappsstore.sqlite is not a file!");
    return nsCOMPtr<mozIStorageConnection>{};
  }

  QM_TRY_INSPECT(const auto& connection,
                 QM_OR_ELSE_WARN_IF(
                     // Expression.
                     MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                         nsCOMPtr<mozIStorageConnection>, aStorageService,
                         OpenUnsharedDatabase, &aWebAppsStoreFile,
                         mozIStorageService::CONNECTION_DEFAULT),
                     // Predicate.
                     IsDatabaseCorruptionError,
                     // Fallback. Don't throw an error, leave a corrupted
                     // webappsstore database as it is.
                     ErrToDefaultOk<nsCOMPtr<mozIStorageConnection>>));

  if (connection) {
    // Don't propagate an error, leave a non-updateable webappsstore database as
    // it is.
    QM_TRY(MOZ_TO_RESULT(StorageDBUpdater::Update(connection)),
           nsCOMPtr<mozIStorageConnection>{});
  }

  return connection;
}

Result<nsCOMPtr<nsIFile>, QMResult> GetLocalStorageArchiveFile(
    const nsAString& aDirectoryPath) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aDirectoryPath.IsEmpty());

  QM_TRY_UNWRAP(auto lsArchiveFile,
                QM_TO_RESULT_TRANSFORM(QM_NewLocalFile(aDirectoryPath)));

  QM_TRY(QM_TO_RESULT(
      lsArchiveFile->Append(nsLiteralString(LS_ARCHIVE_FILE_NAME))));

  return lsArchiveFile;
}

Result<nsCOMPtr<nsIFile>, nsresult> GetLocalStorageArchiveTmpFile(
    const nsAString& aDirectoryPath) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aDirectoryPath.IsEmpty());

  QM_TRY_UNWRAP(auto lsArchiveTmpFile, QM_NewLocalFile(aDirectoryPath));

  QM_TRY(MOZ_TO_RESULT(
      lsArchiveTmpFile->Append(nsLiteralString(LS_ARCHIVE_TMP_FILE_NAME))));

  return lsArchiveTmpFile;
}

Result<bool, nsresult> IsLocalStorageArchiveInitialized(
    mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  QM_TRY_RETURN(
      MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, TableExists, "database"_ns));
}

nsresult InitializeLocalStorageArchive(mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const auto& initialized,
                   IsLocalStorageArchiveInitialized(*aConnection));
    MOZ_ASSERT(!initialized);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "CREATE TABLE database(version INTEGER NOT NULL DEFAULT 0);"_ns)));

  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
          "INSERT INTO database (version) VALUES (:version)"_ns));

  QM_TRY(MOZ_TO_RESULT(stmt->BindInt32ByName("version"_ns, 0)));
  QM_TRY(MOZ_TO_RESULT(stmt->Execute()));

  return NS_OK;
}

Result<int32_t, nsresult> LoadLocalStorageArchiveVersion(
    mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement<
                     SingleStepResult::ReturnNullIfNoResult>(
                     aConnection, "SELECT version FROM database"_ns));

  QM_TRY(OkIf(stmt), Err(NS_ERROR_FILE_CORRUPTED));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 0));
}

nsresult SaveLocalStorageArchiveVersion(mozIStorageConnection* aConnection,
                                        int32_t aVersion) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = aConnection->CreateStatement(
      "UPDATE database SET version = :version;"_ns, getter_AddRefs(stmt));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = stmt->BindInt32ByName("version"_ns, aVersion);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = stmt->Execute();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

template <typename FileFunc, typename DirectoryFunc>
Result<mozilla::Ok, nsresult> CollectEachFileEntry(
    nsIFile& aDirectory, const FileFunc& aFileFunc,
    const DirectoryFunc& aDirectoryFunc) {
  AssertIsOnIOThread();

  return CollectEachFile(
      aDirectory,
      [&aFileFunc, &aDirectoryFunc](
          const nsCOMPtr<nsIFile>& file) -> Result<mozilla::Ok, nsresult> {
        QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*file));

        switch (dirEntryKind) {
          case nsIFileKind::ExistsAsDirectory:
            return aDirectoryFunc(file);

          case nsIFileKind::ExistsAsFile:
            return aFileFunc(file);

          case nsIFileKind::DoesNotExist:
            // Ignore files that got removed externally while iterating.
            break;
        }

        return Ok{};
      });
}

/******************************************************************************
 * Quota manager class declarations
 ******************************************************************************/

}  // namespace

class PBMQuotaCallback final : public nsIQuotaCallback {
 public:
  NS_DECL_ISUPPORTS

  explicit PBMQuotaCallback(nsIPBMCleanupCallback* aCb) : mCallback(aCb) {}

  NS_IMETHOD OnComplete(nsIQuotaRequest* aRequest) override {
    if (mCallback) {
      nsresult resultCode = NS_OK;
      if (aRequest && NS_FAILED(aRequest->GetResultCode(&resultCode))) {
        resultCode = NS_ERROR_FAILURE;
      }
      mCallback->Complete(resultCode);
    }
    return NS_OK;
  }

 private:
  ~PBMQuotaCallback() = default;
  nsCOMPtr<nsIPBMCleanupCallback> mCallback;
};

NS_IMPL_ISUPPORTS(PBMQuotaCallback, nsIQuotaCallback)

class QuotaManager::Observer final : public nsIObserver {
  static Observer* sInstance;

  bool mPendingProfileChange;
  bool mShutdownComplete;

 public:
  static nsresult Initialize();

  static nsIObserver* GetInstance();

  static void ShutdownCompleted();

 private:
  Observer() : mPendingProfileChange(false), mShutdownComplete(false) {
    MOZ_ASSERT(NS_IsMainThread());
  }

  ~Observer() { MOZ_ASSERT(NS_IsMainThread()); }

  nsresult Init();

  nsresult Shutdown();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER
};

namespace {

/*******************************************************************************
 * Local class declarations
 ******************************************************************************/

}  // namespace

namespace {

class CollectOriginsHelper final : public Runnable {
  uint64_t mMinSizeToBeFreed;

  Mutex& mMutex;
  CondVar mCondVar;

  // The members below are protected by mMutex.
  nsTArray<RefPtr<OriginDirectoryLock>> mLocks;
  uint64_t mSizeToBeFreed;
  bool mWaiting;

 public:
  CollectOriginsHelper(mozilla::Mutex& aMutex, uint64_t aMinSizeToBeFreed);

  // Blocks the current thread until origins are collected on the main thread.
  // The returned value contains an aggregate size of those origins.
  int64_t BlockAndReturnOriginsForEviction(
      nsTArray<RefPtr<OriginDirectoryLock>>& aLocks);

 private:
  ~CollectOriginsHelper() = default;

  NS_IMETHOD
  Run() override;
};

/*******************************************************************************
 * Other class declarations
 ******************************************************************************/

class RecordTimeDeltaHelper final : public Runnable {
  const mozilla::glean::impl::Labeled<
      mozilla::glean::impl::TimingDistributionMetric, DynamicLabel>& mMetric;

  // TimeStamps that are set on the IO thread.
  LazyInitializedOnceNotNull<const TimeStamp> mStartTime;
  LazyInitializedOnceNotNull<const TimeStamp> mEndTime;

  // A TimeStamp that is set on the main thread.
  LazyInitializedOnceNotNull<const TimeStamp> mInitializedTime;

 public:
  explicit RecordTimeDeltaHelper(const mozilla::glean::impl::Labeled<
                                 mozilla::glean::impl::TimingDistributionMetric,
                                 DynamicLabel>& aMetric)
      : Runnable("dom::quota::RecordTimeDeltaHelper"), mMetric(aMetric) {}

  TimeStamp Start();

  TimeStamp End();

 private:
  ~RecordTimeDeltaHelper() = default;

  NS_DECL_NSIRUNNABLE
};

/*******************************************************************************
 * Helper classes
 ******************************************************************************/

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

// Return whether the group was actually updated.
Result<bool, nsresult> MaybeUpdateGroupForOrigin(
    OriginMetadata& aOriginMetadata) {
  MOZ_ASSERT(!NS_IsMainThread());

  bool updated = false;

  if (aOriginMetadata.mOrigin.EqualsLiteral(kChromeOrigin)) {
    if (!aOriginMetadata.mGroup.EqualsLiteral(kChromeOrigin)) {
      aOriginMetadata.mGroup.AssignLiteral(kChromeOrigin);
      updated = true;
    }
  } else {
    nsCOMPtr<nsIPrincipal> principal =
        BasePrincipal::CreateContentPrincipal(aOriginMetadata.mOrigin);
    QM_TRY(MOZ_TO_RESULT(principal));

    QM_TRY_INSPECT(const auto& baseDomain,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, principal,
                                                     GetBaseDomain));

    const nsCString upToDateGroup = baseDomain + aOriginMetadata.mSuffix;

    if (aOriginMetadata.mGroup != upToDateGroup) {
      aOriginMetadata.mGroup = upToDateGroup;
      updated = true;
    }
  }

  return updated;
}

Result<bool, nsresult> MaybeUpdateLastAccessTimeForOrigin(
    FullOriginMetadata& aFullOriginMetadata) {
  GECKO_TRACE_SCOPE("dom::quota", "MaybeUpdateLastAccessTimeForOrigin");

  MOZ_ASSERT(!NS_IsMainThread());

  if (aFullOriginMetadata.mLastAccessTime == INT64_MIN) {
    QuotaManager* quotaManager = QuotaManager::Get();
    MOZ_ASSERT(quotaManager);

    QM_TRY_INSPECT(const auto& metadataFile,
                   quotaManager->GetOriginDirectory(aFullOriginMetadata));

    QM_TRY(MOZ_TO_RESULT(
        metadataFile->Append(nsLiteralString(METADATA_V2_FILE_NAME))));

    QM_TRY_UNWRAP(int64_t timestamp, MOZ_TO_RESULT_INVOKE_MEMBER(
                                         metadataFile, GetLastModifiedTime));

    // Need to convert from milliseconds to microseconds.
    MOZ_ASSERT((INT64_MAX / PR_USEC_PER_MSEC) > timestamp);
    timestamp *= int64_t(PR_USEC_PER_MSEC);

    aFullOriginMetadata.mLastAccessTime = timestamp;

    return true;
  }

  return false;
}

}  // namespace

void ReportInternalError(const char* aFile, uint32_t aLine, const char* aStr) {
  // Get leaf of file path
  for (const char* p = aFile; *p; ++p) {
    if (*p == '/' && *(p + 1)) {
      aFile = p + 1;
    }
  }

  nsContentUtils::LogSimpleConsoleError(
      NS_ConvertUTF8toUTF16(
          nsPrintfCString("Quota %s: %s:%" PRIu32, aStr, aFile, aLine)),
      "quota"_ns,
      false /* Quota Manager is not active in private browsing mode */,
      true /* Quota Manager runs always in a chrome context */);
}

namespace {

Atomic<QuotaManager::CacheInvalidationLevel> gInvalidateQuotaCache{
    QuotaManager::CacheInvalidationLevel::None};
StaticAutoPtr<nsString> gBasePath;
StaticAutoPtr<nsString> gStorageName;
StaticAutoPtr<nsCString> gBuildId;

#ifdef DEBUG
bool gQuotaManagerInitialized = false;
#endif

// Snapshot of dom.quotaManager.encryption.enabled taken at profile-do-change;
// runtime changes of the pref are ignored until restart.
bool gStorageEncryptionEnabled = false;
StaticAutoPtr<nsCString> gCipherKeyStoreProfileId;
StaticRefPtr<CipherKeyStoreBackend> gCipherKeyStore;

// The key store is created eagerly at profile-do-change when the pref is on.
// When the pref is off it is only created if an origin encrypted in an
// earlier session is opened, so that it stays readable.
void EnsureCipherKeyStore() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!gCipherKeyStore && gCipherKeyStoreProfileId) {
    gCipherKeyStore =
        LockstoreStorageKeyProvider::Create(*gCipherKeyStoreProfileId);
  }
}
StaticRefPtr<QuotaManager> gInstance;
mozilla::Atomic<bool> gShutdown(false);

// A time stamp that can only be accessed on the main thread.
TimeStamp gLastOSWake;

// XXX Move to QuotaManager once NormalOriginOperationBase is declared in a
// separate and includable file.
using NormalOriginOpArray =
    nsTArray<CheckedUnsafePtr<NormalOriginOperationBase>>;
StaticAutoPtr<NormalOriginOpArray> gNormalOriginOps;

class StorageOperationBase {
 protected:
  struct OriginProps {
    enum Type { eChrome, eContent, eObsolete, eInvalid };

    NotNull<nsCOMPtr<nsIFile>> mDirectory;
    nsString mLeafName;
    nsCString mSpec;
    OriginAttributes mAttrs;
    int64_t mTimestamp;
    OriginMetadata mOriginMetadata;
    nsCString mOriginalSuffix;

    LazyInitializedOnceEarlyDestructible<const PersistenceType>
        mPersistenceType;
    Type mType;
    bool mNeedsRestore;
    bool mNeedsRestore2;
    bool mIgnore;

   public:
    explicit OriginProps(MovingNotNull<nsCOMPtr<nsIFile>> aDirectory)
        : mDirectory(std::move(aDirectory)),
          mTimestamp(0),
          mType(eContent),
          mNeedsRestore(false),
          mNeedsRestore2(false),
          mIgnore(false) {}

    template <typename PersistenceTypeFunc>
    nsresult Init(PersistenceTypeFunc&& aPersistenceTypeFunc);
  };

  nsTArray<OriginProps> mOriginProps;

  nsCOMPtr<nsIFile> mDirectory;

 public:
  explicit StorageOperationBase(nsIFile* aDirectory) : mDirectory(aDirectory) {
    AssertIsOnIOThread();
  }

  NS_INLINE_DECL_REFCOUNTING(StorageOperationBase)

 protected:
  virtual ~StorageOperationBase() = default;

  static nsresult CreateDirectoryMetadata(
      nsIFile& aDirectory, int64_t aTimestamp,
      const OriginMetadata& aOriginMetadata);

  static nsresult CreateDirectoryMetadata2(
      nsIFile& aDirectory, int64_t aTimestamp, bool aPersisted,
      const OriginMetadata& aOriginMetadata);

  nsresult GetDirectoryMetadata(nsIFile* aDirectory, int64_t& aTimestamp,
                                nsACString& aGroup, nsACString& aOrigin,
                                Nullable<bool>& aIsApp);

  // Upgrade helper to load the contents of ".metadata-v2" files from previous
  // schema versions.  Although QuotaManager has a similar GetDirectoryMetadata2
  // method, it is only intended to read current version ".metadata-v2" files.
  // And unlike the old ".metadata" files, the ".metadata-v2" format can evolve
  // because our "storage.sqlite" lets us track the overall version of the
  // storage directory.
  nsresult GetDirectoryMetadata2(nsIFile* aDirectory, int64_t& aTimestamp,
                                 nsACString& aSuffix, nsACString& aGroup,
                                 nsACString& aOrigin, bool& aIsApp);

  int64_t GetOriginLastModifiedTime(const OriginProps& aOriginProps);

  nsresult RemoveObsoleteOrigin(const OriginProps& aOriginProps);

  /**
   * Rename the origin if the origin string generation from nsIPrincipal
   * changed. This consists of renaming the origin in the metadata files and
   * renaming the origin directory itself. For simplicity, the origin in
   * metadata files is not actually updated, but the metadata files are
   * recreated instead.
   *
   * @param  aOriginProps the properties of the origin to check.
   *
   * @return whether origin was renamed.
   */
  Result<bool, nsresult> MaybeRenameOrigin(const OriginProps& aOriginProps);

  nsresult ProcessOriginDirectories();

  virtual nsresult ProcessOriginDirectory(const OriginProps& aOriginProps) = 0;
};

class RepositoryOperationBase : public StorageOperationBase {
 public:
  explicit RepositoryOperationBase(nsIFile* aDirectory)
      : StorageOperationBase(aDirectory) {}

  nsresult ProcessRepository();

 protected:
  virtual ~RepositoryOperationBase() = default;

  template <typename UpgradeMethod>
  nsresult MaybeUpgradeClients(const OriginProps& aOriginsProps,
                               UpgradeMethod aMethod);

 private:
  virtual PersistenceType PersistenceTypeFromSpec(const nsCString& aSpec) = 0;

  virtual nsresult PrepareOriginDirectory(OriginProps& aOriginProps,
                                          bool* aRemoved) = 0;

  virtual nsresult PrepareClientDirectory(nsIFile* aFile,
                                          const nsAString& aLeafName,
                                          bool& aRemoved);
};

class CreateOrUpgradeDirectoryMetadataHelper final
    : public RepositoryOperationBase {
  nsCOMPtr<nsIFile> mPermanentStorageDir;

  // The legacy PersistenceType, before the default repository introduction.
  enum class LegacyPersistenceType {
    Persistent = 0,
    Temporary
    // The PersistenceType had also PERSISTENCE_TYPE_INVALID, but we don't need
    // it here.
  };

  LazyInitializedOnce<const LegacyPersistenceType> mLegacyPersistenceType;

 public:
  explicit CreateOrUpgradeDirectoryMetadataHelper(nsIFile* aDirectory)
      : RepositoryOperationBase(aDirectory) {}

  nsresult Init();

 private:
  Maybe<LegacyPersistenceType> LegacyPersistenceTypeFromFile(nsIFile& aFile,
                                                             const fallible_t&);

  PersistenceType PersistenceTypeFromLegacyPersistentSpec(
      const nsCString& aSpec);

  PersistenceType PersistenceTypeFromSpec(const nsCString& aSpec) override;

  nsresult MaybeUpgradeOriginDirectory(nsIFile* aDirectory);

  nsresult PrepareOriginDirectory(OriginProps& aOriginProps,
                                  bool* aRemoved) override;

  nsresult ProcessOriginDirectory(const OriginProps& aOriginProps) override;
};

class UpgradeStorageHelperBase : public RepositoryOperationBase {
  LazyInitializedOnce<const PersistenceType> mPersistenceType;

 public:
  explicit UpgradeStorageHelperBase(nsIFile* aDirectory)
      : RepositoryOperationBase(aDirectory) {}

  nsresult Init();

 private:
  PersistenceType PersistenceTypeFromSpec(const nsCString& aSpec) override;
};

class UpgradeStorageFrom0_0To1_0Helper final : public UpgradeStorageHelperBase {
 public:
  explicit UpgradeStorageFrom0_0To1_0Helper(nsIFile* aDirectory)
      : UpgradeStorageHelperBase(aDirectory) {}

 private:
  nsresult PrepareOriginDirectory(OriginProps& aOriginProps,
                                  bool* aRemoved) override;

  nsresult ProcessOriginDirectory(const OriginProps& aOriginProps) override;
};

class UpgradeStorageFrom1_0To2_0Helper final : public UpgradeStorageHelperBase {
 public:
  explicit UpgradeStorageFrom1_0To2_0Helper(nsIFile* aDirectory)
      : UpgradeStorageHelperBase(aDirectory) {}

 private:
  nsresult MaybeRemoveMorgueDirectory(const OriginProps& aOriginProps);

  /**
   * Remove the origin directory if appId is present in origin attributes.
   *
   * @param aOriginProps the properties of the origin to check.
   *
   * @return whether the origin directory was removed.
   */
  Result<bool, nsresult> MaybeRemoveAppsData(const OriginProps& aOriginProps);

  nsresult PrepareOriginDirectory(OriginProps& aOriginProps,
                                  bool* aRemoved) override;

  nsresult ProcessOriginDirectory(const OriginProps& aOriginProps) override;
};

class UpgradeStorageFrom2_0To2_1Helper final : public UpgradeStorageHelperBase {
 public:
  explicit UpgradeStorageFrom2_0To2_1Helper(nsIFile* aDirectory)
      : UpgradeStorageHelperBase(aDirectory) {}

 private:
  nsresult PrepareOriginDirectory(OriginProps& aOriginProps,
                                  bool* aRemoved) override;

  nsresult ProcessOriginDirectory(const OriginProps& aOriginProps) override;
};

class UpgradeStorageFrom2_1To2_2Helper final : public UpgradeStorageHelperBase {
 public:
  explicit UpgradeStorageFrom2_1To2_2Helper(nsIFile* aDirectory)
      : UpgradeStorageHelperBase(aDirectory) {}

 private:
  nsresult PrepareOriginDirectory(OriginProps& aOriginProps,
                                  bool* aRemoved) override;

  nsresult ProcessOriginDirectory(const OriginProps& aOriginProps) override;

  nsresult PrepareClientDirectory(nsIFile* aFile, const nsAString& aLeafName,
                                  bool& aRemoved) override;
};

class RestoreDirectoryMetadata2Helper final : public StorageOperationBase {
  LazyInitializedOnce<const PersistenceType> mPersistenceType;

 public:
  explicit RestoreDirectoryMetadata2Helper(nsIFile* aDirectory)
      : StorageOperationBase(aDirectory) {}

  nsresult Init();

  nsresult RestoreMetadata2File();

 private:
  nsresult ProcessOriginDirectory(const OriginProps& aOriginProps) override;
};

Result<nsAutoString, nsresult> GetPathForStorage(
    nsIFile& aBaseDir, const nsAString& aStorageName) {
  QM_TRY_INSPECT(const auto& storageDir,
                 CloneFileAndAppend(aBaseDir, aStorageName));

  QM_TRY_RETURN(
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, storageDir, GetPath));
}

int64_t GetLastModifiedTime(PersistenceType aPersistenceType, nsIFile& aFile) {
  AssertIsOnIOThread();

  class MOZ_STACK_CLASS Helper final {
   public:
    static nsresult GetLastModifiedTime(nsIFile* aFile, int64_t* aTimestamp) {
      AssertIsOnIOThread();
      MOZ_ASSERT(aFile);
      MOZ_ASSERT(aTimestamp);

      QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*aFile));

      switch (dirEntryKind) {
        case nsIFileKind::ExistsAsDirectory:
          QM_TRY(CollectEachFile(
              *aFile,
              [&aTimestamp](const nsCOMPtr<nsIFile>& file)
                  -> Result<mozilla::Ok, nsresult> {
                QM_TRY(MOZ_TO_RESULT(GetLastModifiedTime(file, aTimestamp)));

                return Ok{};
              }));
          break;

        case nsIFileKind::ExistsAsFile: {
          QM_TRY_INSPECT(const auto& leafName,
                         MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, aFile,
                                                           GetLeafName));

          // Bug 1595445 will handle unknown files here.

          if (IsOriginMetadata(leafName) || IsTempMetadata(leafName) ||
              IsDotFile(leafName)) {
            return NS_OK;
          }

          QM_TRY_UNWRAP(int64_t timestamp, MOZ_TO_RESULT_INVOKE_MEMBER(
                                               aFile, GetLastModifiedTime));

          // Need to convert from milliseconds to microseconds.
          MOZ_ASSERT((INT64_MAX / PR_USEC_PER_MSEC) > timestamp);
          timestamp *= int64_t(PR_USEC_PER_MSEC);

          if (timestamp > *aTimestamp) {
            *aTimestamp = timestamp;
          }
          break;
        }

        case nsIFileKind::DoesNotExist:
          // Ignore files that got removed externally while iterating.
          break;
      }

      return NS_OK;
    }
  };

  if (aPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
    return PR_Now();
  }

  int64_t timestamp = INT64_MIN;
  nsresult rv = Helper::GetLastModifiedTime(&aFile, &timestamp);
  if (NS_FAILED(rv)) {
    timestamp = PR_Now();
  }

  // XXX if there were no suitable files for getting last modified time
  // (timestamp is still set to INT64_MIN), we should return the current time
  // instead of returning INT64_MIN.

  return timestamp;
}

// Returns a bool indicating whether the directory was newly created.
Result<bool, nsresult> EnsureDirectory(nsIFile& aDirectory) {
  AssertIsOnIOThread();

  // Callers call this function without checking if the directory already
  // exists (idempotent usage). QM_OR_ELSE_WARN_IF is not used here since we
  // just want to log NS_ERROR_FILE_ALREADY_EXISTS result and not spam the
  // reports.
  QM_TRY_INSPECT(const auto& exists,
                 QM_OR_ELSE_LOG_VERBOSE_IF(
                     // Expression.
                     MOZ_TO_RESULT_INVOKE_MEMBER(aDirectory, Create,
                                                 nsIFile::DIRECTORY_TYPE, 0755,
                                                 /* aSkipAncestors = */ false)
                         .map([](Ok) { return false; }),
                     // Predicate.
                     IsSpecificError<NS_ERROR_FILE_ALREADY_EXISTS>,
                     // Fallback.
                     ErrToOk<true>));

  if (exists) {
    QM_TRY_INSPECT(const bool& isDirectory,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aDirectory, IsDirectory));
    QM_TRY(OkIf(isDirectory), Err(NS_ERROR_UNEXPECTED));
  }

  return !exists;
}

void GetJarPrefix(bool aInIsolatedMozBrowser, nsACString& aJarPrefix) {
  aJarPrefix.Truncate();

  // Fallback.
  if (!aInIsolatedMozBrowser) {
    return;
  }

  // AppId is an unused b2g identifier. Let's set it to 0 all the time (see bug
  // 1320404).
  // aJarPrefix = appId + "+" + { 't', 'f' } + "+";
  aJarPrefix.AppendInt(0);  // TODO: this is the appId, to be removed.
  aJarPrefix.Append('+');
  aJarPrefix.Append(aInIsolatedMozBrowser ? 't' : 'f');
  aJarPrefix.Append('+');
}

// This method computes and returns our best guess for the temporary storage
// limit (in bytes), based on disk capacity.
Result<int64_t, nsresult> GetTemporaryStorageLimit(nsIFile& aStorageDir) {
  if (nsContentUtils::ShouldResistFingerprinting(
          "The storage limit is set only once and not webpage specific.",
          RFPTarget::DiskStorageLimit)) {
    return nsRFPService::GetSpoofedStorageLimit();
  }

  // The fixed limit pref can be used to override temporary storage limit
  // calculation.
  if (StaticPrefs::dom_quotaManager_temporaryStorage_fixedLimit() >= 0) {
    return static_cast<uint64_t>(
               StaticPrefs::dom_quotaManager_temporaryStorage_fixedLimit()) *
           1024;
  }

  constexpr int64_t teraByte = (1024LL * 1024LL * 1024LL * 1024LL);
  constexpr int64_t maxAllowedCapacity = 8LL * teraByte;

  // Check for disk capacity of user's device on which storage directory lives.
  int64_t diskCapacity = maxAllowedCapacity;

  // Log error when default disk capacity is returned due to the error
  QM_WARNONLY_TRY(MOZ_TO_RESULT(aStorageDir.GetDiskCapacity(&diskCapacity)));

  MOZ_ASSERT(diskCapacity >= 0LL);

  // Allow temporary storage to consume up to 50% of disk capacity.
  int64_t capacityLimit = diskCapacity / 2LL;

  // If the disk capacity reported by the operating system is very
  // large and potentially incorrect due to hardware issues,
  // a hardcoded limit is supplied instead.
  QM_WARNONLY_TRY(
      OkIf(capacityLimit < maxAllowedCapacity),
      ([&capacityLimit](const auto&) { capacityLimit = maxAllowedCapacity; }));

  return capacityLimit;
}

bool IsOriginUnaccessed(const FullOriginMetadata& aFullOriginMetadata,
                        const int64_t aRecentTime) {
  if (aFullOriginMetadata.mLastAccessTime > aRecentTime) {
    return false;
  }

  return (aRecentTime - aFullOriginMetadata.mLastAccessTime) / PR_USEC_PER_SEC >
         StaticPrefs::dom_quotaManager_unaccessedForLongTimeThresholdSec();
}

}  // namespace

/*******************************************************************************
 * Exported functions
 ******************************************************************************/

void InitializeQuotaManager() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!gQuotaManagerInitialized);

  if (!QuotaManager::IsRunningGTests()) {
    // These services have to be started on the main thread currently.
    const nsCOMPtr<mozIStorageService> ss =
        do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
    QM_WARNONLY_TRY(OkIf(ss));

    RefPtr<net::ExtensionProtocolHandler> extensionProtocolHandler =
        net::ExtensionProtocolHandler::GetSingleton();
    QM_WARNONLY_TRY(MOZ_TO_RESULT(extensionProtocolHandler));

    IndexedDatabaseManager* mgr = IndexedDatabaseManager::GetOrCreate();
    QM_WARNONLY_TRY(MOZ_TO_RESULT(mgr));
  }

  QM_WARNONLY_TRY(QM_TO_RESULT(QuotaManager::Initialize()));

#ifdef DEBUG
  gQuotaManagerInitialized = true;
#endif
}

void InitializeScopedLogExtraInfo() {
#ifdef QM_SCOPED_LOG_EXTRA_INFO_ENABLED
  ScopedLogExtraInfo::Initialize();
#endif
}

bool RecvShutdownQuotaManager() {
  AssertIsOnBackgroundThread();

  // If we are already in shutdown, don't call ShutdownInstance()
  // again and return true immediately. We shall see this incident
  // in Telemetry.
  // XXX todo: Make QM_TRY stacks thread-aware (Bug 1735124)
  // XXX todo: Active QM_TRY context for shutdown (Bug 1735170)
  QM_TRY(OkIf(!gShutdown), true);

  QuotaManager::ShutdownInstance();

  return true;
}

QuotaManager::Observer* QuotaManager::Observer::sInstance = nullptr;

// static
nsresult QuotaManager::Observer::Initialize() {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<Observer> observer = new Observer();

  nsresult rv = observer->Init();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  sInstance = observer;

  return NS_OK;
}

// static
nsIObserver* QuotaManager::Observer::GetInstance() {
  MOZ_ASSERT(NS_IsMainThread());

  return sInstance;
}

// static
void QuotaManager::Observer::ShutdownCompleted() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sInstance);

  sInstance->mShutdownComplete = true;
}

nsresult QuotaManager::Observer::Init() {
  MOZ_ASSERT(NS_IsMainThread());

  /**
   * A RAII utility class to manage the registration and automatic
   * unregistration of observers with `nsIObserverService`. This class is
   * designed to simplify observer management, particularly when registering
   * for multiple topics, by ensuring that already registered topics are
   * unregistered if a failure occurs during subsequent registrations.
   */
  class MOZ_RAII Registrar {
   public:
    Registrar(nsIObserverService* aObserverService, nsIObserver* aObserver,
              const char* aTopic)
        : mObserverService(std::move(aObserverService)),
          mObserver(aObserver),
          mTopic(aTopic),
          mUnregisterOnDestruction(false) {
      MOZ_ASSERT(aObserverService);
      MOZ_ASSERT(aObserver);
      MOZ_ASSERT(aTopic);
    }

    ~Registrar() {
      if (mUnregisterOnDestruction) {
        mObserverService->RemoveObserver(mObserver, mTopic);
      }
    }

    nsresult Register() {
      MOZ_ASSERT(!mUnregisterOnDestruction);

      nsresult rv = mObserverService->AddObserver(mObserver, mTopic, false);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      mUnregisterOnDestruction = true;

      return NS_OK;
    }

    void Commit() { mUnregisterOnDestruction = false; }

   private:
    nsIObserverService* mObserverService;
    nsIObserver* mObserver;
    const char* mTopic;
    bool mUnregisterOnDestruction;
  };

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return NS_ERROR_FAILURE;
  }

  Registrar xpcomShutdownRegistrar(obs, this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
  QM_TRY(MOZ_TO_RESULT(xpcomShutdownRegistrar.Register()));

  Registrar profileDoChangeRegistrar(obs, this, kProfileDoChangeTopic);
  QM_TRY(MOZ_TO_RESULT(profileDoChangeRegistrar.Register()));

  Registrar contextualIdentityServiceLoadFinishedRegistrar(
      obs, this, kContextualIdentityServiceLoadFinishedTopic);
  QM_TRY(
      MOZ_TO_RESULT(contextualIdentityServiceLoadFinishedRegistrar.Register()));

  Registrar profileBeforeChangeQmRegistrar(
      obs, this, PROFILE_BEFORE_CHANGE_QM_OBSERVER_ID);
  QM_TRY(MOZ_TO_RESULT(profileBeforeChangeQmRegistrar.Register()));

  Registrar wakeNotificationRegistrar(obs, this, NS_WIDGET_WAKE_OBSERVER_TOPIC);
  QM_TRY(MOZ_TO_RESULT(wakeNotificationRegistrar.Register()));

  Registrar lastPbContextExitedRegistrar(obs, this,
                                         kPrivateBrowsingObserverTopic);
  QM_TRY(MOZ_TO_RESULT(lastPbContextExitedRegistrar.Register()));

  xpcomShutdownRegistrar.Commit();
  profileDoChangeRegistrar.Commit();
  contextualIdentityServiceLoadFinishedRegistrar.Commit();
  profileBeforeChangeQmRegistrar.Commit();
  wakeNotificationRegistrar.Commit();
  lastPbContextExitedRegistrar.Commit();

  return NS_OK;
}

nsresult QuotaManager::Observer::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ALWAYS_SUCCEEDS(obs->RemoveObserver(this, kPrivateBrowsingObserverTopic));
  MOZ_ALWAYS_SUCCEEDS(obs->RemoveObserver(this, NS_WIDGET_WAKE_OBSERVER_TOPIC));
  MOZ_ALWAYS_SUCCEEDS(
      obs->RemoveObserver(this, PROFILE_BEFORE_CHANGE_QM_OBSERVER_ID));
  MOZ_ALWAYS_SUCCEEDS(
      obs->RemoveObserver(this, kContextualIdentityServiceLoadFinishedTopic));
  MOZ_ALWAYS_SUCCEEDS(obs->RemoveObserver(this, kProfileDoChangeTopic));
  MOZ_ALWAYS_SUCCEEDS(obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID));

  sInstance = nullptr;

  // In general, the instance will have died after the latter removal call, so
  // it's not safe to do anything after that point.
  // However, Shutdown is currently called from Observe which is called by the
  // Observer Service which holds a strong reference to the observer while the
  // Observe method is being called.

  return NS_OK;
}

NS_IMPL_ISUPPORTS(QuotaManager::Observer, nsIObserver)

NS_IMETHODIMP
QuotaManager::Observer::Observe(nsISupports* aSubject, const char* aTopic,
                                const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;

  if (!strcmp(aTopic, kProfileDoChangeTopic)) {
    if (NS_WARN_IF(gBasePath)) {
      NS_WARNING(
          "profile-before-change-qm must precede repeated "
          "profile-do-change!");
      return NS_OK;
    }

    gBasePath = new nsString();

    nsCOMPtr<nsIFile> baseDir;
    rv = NS_GetSpecialDirectory(NS_APP_INDEXEDDB_PARENT_DIR,
                                getter_AddRefs(baseDir));
    if (NS_FAILED(rv)) {
      rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                  getter_AddRefs(baseDir));
    }
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    rv = baseDir->GetPath(*gBasePath);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

#ifdef XP_WIN
    // Annotate if our profile lives on a network resource.
    bool isNetworkPath = PathIsNetworkPathW(gBasePath->get());
    CrashReporter::RecordAnnotationBool(
        CrashReporter::Annotation::QuotaManagerStorageIsNetworkResource,
        isNetworkPath);
#endif

    QM_LOG(("Base path: %s", NS_ConvertUTF16toUTF8(*gBasePath).get()));

    gStorageName = new nsString();

    rv = Preferences::GetString("dom.quotaManager.storageName", *gStorageName);
    if (NS_FAILED(rv)) {
      *gStorageName = kStorageName;
    }

    gBuildId = new nsCString();

    nsCOMPtr<nsIPlatformInfo> platformInfo =
        do_GetService("@mozilla.org/xre/app-info;1");
    if (NS_WARN_IF(!platformInfo)) {
      return NS_ERROR_FAILURE;
    }

    rv = platformInfo->GetPlatformBuildID(*gBuildId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    gStorageEncryptionEnabled =
        StaticPrefs::dom_quotaManager_encryption_enabled();

    nsAutoString profileLeaf;
    rv = baseDir->GetLeafName(profileLeaf);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    Result<nsCString, nsresult> res =
        DekNameCodec::ProfileId(NS_ConvertUTF16toUTF8(profileLeaf));
    if (NS_WARN_IF(res.isErr())) {
      return res.unwrapErr();
    }
    gCipherKeyStoreProfileId = new nsCString(res.unwrap());

    if (gStorageEncryptionEnabled) {
      EnsureCipherKeyStore();
    }

    return NS_OK;
  }

  if (!strcmp(aTopic, kContextualIdentityServiceLoadFinishedTopic)) {
    if (NS_WARN_IF(!gBasePath)) {
      NS_WARNING(
          "profile-do-change must precede "
          "contextual-identity-service-load-finished!");
      return NS_OK;
    }

    nsCOMPtr<nsIQuotaManagerServiceInternal> quotaManagerService =
        QuotaManagerService::GetOrCreate();
    if (NS_WARN_IF(!quotaManagerService)) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIQuotaUtilsService> quotaUtilsService =
        do_GetService("@mozilla.org/dom/quota-utils-service;1");
    if (NS_WARN_IF(!quotaUtilsService)) {
      return NS_ERROR_FAILURE;
    }

    uint32_t thumbnailPrivateIdentityId;
    nsresult rv = quotaUtilsService->GetPrivateIdentityId(
        u"userContextIdInternal.thumbnail"_ns, &thumbnailPrivateIdentityId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    rv = quotaManagerService->SetThumbnailPrivateIdentityId(
        thumbnailPrivateIdentityId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    return NS_OK;
  }

  if (!strcmp(aTopic, PROFILE_BEFORE_CHANGE_QM_OBSERVER_ID)) {
    if (NS_WARN_IF(!gBasePath)) {
      NS_WARNING("profile-do-change must precede profile-before-change-qm!");
      return NS_OK;
    }

    // mPendingProfileChange is our re-entrancy guard (the nested event loop
    // below may cause re-entrancy).
    if (mPendingProfileChange) {
      return NS_OK;
    }

    AutoRestore<bool> pending(mPendingProfileChange);
    mPendingProfileChange = true;

    mShutdownComplete = false;

    PBackgroundChild* backgroundActor =
        BackgroundChild::GetOrCreateForCurrentThread();
    if (NS_WARN_IF(!backgroundActor)) {
      return NS_ERROR_FAILURE;
    }

    if (NS_WARN_IF(!backgroundActor->SendShutdownQuotaManager())) {
      return NS_ERROR_FAILURE;
    }

    MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
        "QuotaManager::Observer::Observe profile-before-change-qm"_ns,
        [&]() { return mShutdownComplete; }));

    gBasePath = nullptr;

    gStorageName = nullptr;

    gBuildId = nullptr;

    gCipherKeyStore = nullptr;
    gCipherKeyStoreProfileId = nullptr;

    return NS_OK;
  }

  if (!strcmp(aTopic, kPrivateBrowsingObserverTopic)) {
    nsCOMPtr<nsIPBMCleanupCollector> collector = do_QueryInterface(aSubject);
    nsCOMPtr<nsIPBMCleanupCallback> cb;
    // collector is only set for clearing PBM data via nsIClearDataService.
    // If unset we don't need to signal cleanup completion.
    if (collector) {
      collector->AddPendingCleanup(getter_AddRefs(cb));
    }

    auto* const quotaManagerService = QuotaManagerService::GetOrCreate();
    if (NS_WARN_IF(!quotaManagerService)) {
      if (cb) {
        cb->Complete(NS_ERROR_FAILURE);
      }
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIQuotaRequest> request;
    rv = quotaManagerService->ClearStoragesForPrivateBrowsing(
        nsGetterAddRefs(request));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      if (cb) {
        cb->Complete(rv);
      }
      return rv;
    }

    if (cb) {
      RefPtr<PBMQuotaCallback> wrapper = new PBMQuotaCallback(cb);
      request->SetCallback(wrapper);
    }

    return NS_OK;
  }

  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    rv = Shutdown();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    return NS_OK;
  }

  if (!strcmp(aTopic, NS_WIDGET_WAKE_OBSERVER_TOPIC)) {
    gLastOSWake = TimeStamp::Now();

    return NS_OK;
  }

  NS_WARNING("Unknown observer topic!");
  return NS_OK;
}

/*******************************************************************************
 * Quota manager
 ******************************************************************************/

QuotaManager::QuotaManager(const nsAString& aBasePath,
                           const nsAString& aStorageName)
    : mQuotaMutex("QuotaManager.mQuotaMutex"),
      mInitializedOriginsMutex("QuotaManager.mInitializedOriginsMutex"),
      mBasePath(aBasePath),
      mStorageName(aStorageName),
      mTemporaryStorageUsage(0),
      mNextDirectoryLockId(0),
      mStorageInitialized(false),
      mPersistentStorageInitialized(false),
      mPersistentStorageInitializedInternal(false),
      mTemporaryStorageInitialized(false),
      mTemporaryStorageInitializedInternal(false),
      mInitializingAllTemporaryOrigins(false),
      mAllTemporaryOriginsInitialized(false),
      mCacheUsable(false),
      mCacheRequiresFullScan(false) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!gInstance);
  glean::quotamanager::had_temporary_storage_usage_underflow.Set(false);
}

QuotaManager::~QuotaManager() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!gInstance || gInstance == this);
}

// static
nsresult QuotaManager::Initialize() {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = Observer::Initialize();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

// static
Result<MovingNotNull<RefPtr<QuotaManager>>, nsresult>
QuotaManager::GetOrCreate() {
  AssertIsOnBackgroundThread();

  if (gInstance) {
    return WrapMovingNotNullUnchecked(RefPtr<QuotaManager>{gInstance});
  }

  QM_TRY(OkIf(gBasePath), Err(NS_ERROR_FAILURE), [](const auto&) {
    NS_WARNING(
        "Trying to create QuotaManager before profile-do-change! "
        "Forgot to call do_get_profile()?");
  });

  QM_TRY(OkIf(!IsShuttingDown()), Err(NS_ERROR_FAILURE), [](const auto&) {
    MOZ_ASSERT(false,
               "Trying to create QuotaManager after profile-before-change-qm!");
  });

  auto instance = MakeRefPtr<QuotaManager>(*gBasePath, *gStorageName);

  QM_TRY(MOZ_TO_RESULT(instance->Init()));

  gInstance = instance;

  // Do this before clients have a chance to acquire a directory lock for the
  // private repository.
  gInstance->ClearPrivateRepository();

  return WrapMovingNotNullUnchecked(std::move(instance));
}

Result<Ok, nsresult> QuotaManager::EnsureCreated() {
  AssertIsOnBackgroundThread();

  QM_TRY_RETURN(GetOrCreate().map([](const auto& res) { return Ok{}; }))
}

// static
QuotaManager* QuotaManager::Get() {
  // Does not return an owning reference.
  return gInstance;
}

// static
nsIObserver* QuotaManager::GetObserver() {
  MOZ_ASSERT(NS_IsMainThread());

  return Observer::GetInstance();
}

// static
void QuotaManager::ProcessPendingNormalOriginOperations() {
  MOZ_ASSERT(IsRunningGTests());

  // Processes any pending events that may create normal origin operations.
  // This is needed in cases where an async method (e.g., InitializeStorage)
  // is called without a pre-acquired directory lock, which causes the
  // operation to be created and scheduled after the directory lock is
  // acquired.
  NS_ProcessPendingEvents(nullptr);

  // Wait until all normal origin operations have completed.
  MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
      "QuotaManager::ProcessPendingNormalOriginOperations"_ns,
      []() { return !gNormalOriginOps; }));

  // Once an operation completes, it is removed from gNormalOriginOps. However,
  // there may still be a follow-up event pending that updates a flag after the
  // operation has finished. We need to process that event as well; otherwise,
  // callers of this helper may see inconsistent state.
  // For example, IsStorageInitialized could still return false even after
  // calling InitializeStorage and ProcessPendingNormalOriginOperations.
  NS_ProcessPendingEvents(nullptr);
}

// static
bool QuotaManager::IsShuttingDown() { return gShutdown; }

// static
void QuotaManager::ShutdownInstance() {
  AssertIsOnBackgroundThread();

  if (gInstance) {
    auto recordTimeDeltaHelper =
        MakeRefPtr<RecordTimeDeltaHelper>(glean::dom_quota::shutdown_time);

    recordTimeDeltaHelper->Start();

    // Glean SDK recommends using its own timing APIs where possible. In this
    // case, we use NowExcludingSuspendMs() directly to manually calculate a
    // duration that excludes suspend time. This is a valid exception because
    // our use case is sensitive to suspend, and we need full control over the
    // timing logic.
    //
    // We are currently recording both this and the older helper-based
    // measurement. The results are not directly comparable, since the new API
    // uses monotonic time. If this approach proves more reliable, we'll retire
    // the old telemetry, change the expiration of the new metric to never,
    // and add a matching "including suspend" version.

    const auto startExcludingSuspendMs = NowExcludingSuspendMs();

    gInstance->Shutdown();

    const auto endExcludingSuspendMs = NowExcludingSuspendMs();

    if (startExcludingSuspendMs && endExcludingSuspendMs) {
      const auto duration = TimeDuration::FromMilliseconds(
          *endExcludingSuspendMs - *startExcludingSuspendMs);

      glean::quotamanager_shutdown::total_time_excluding_suspend
          .AccumulateRawDuration(duration);
    }

    recordTimeDeltaHelper->End();

    gInstance = nullptr;
  } else {
    // If we were never initialized, just set the flag to avoid late creation.
    gShutdown = true;
  }

  RefPtr<Runnable> runnable =
      NS_NewRunnableFunction("dom::quota::QuotaManager::ShutdownCompleted",
                             []() { Observer::ShutdownCompleted(); });
  MOZ_ASSERT(runnable);

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(runnable.forget()));
}

// static
void QuotaManager::Reset() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!gInstance);
  MOZ_ASSERT(gShutdown);

  gShutdown = false;
}

// static
bool QuotaManager::IsOSMetadata(const nsAString& aFileName) {
  return mozilla::dom::quota::IsOSMetadata(aFileName);
}

// static
bool QuotaManager::IsDotFile(const nsAString& aFileName) {
  return mozilla::dom::quota::IsDotFile(aFileName);
}

void QuotaManager::RegisterNormalOriginOp(
    NormalOriginOperationBase& aNormalOriginOp) {
  AssertIsOnBackgroundThread();

  if (!gNormalOriginOps) {
    gNormalOriginOps = new NormalOriginOpArray();
  }

  gNormalOriginOps->AppendElement(&aNormalOriginOp);
}

void QuotaManager::UnregisterNormalOriginOp(
    NormalOriginOperationBase& aNormalOriginOp) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(gNormalOriginOps);

  gNormalOriginOps->RemoveElement(&aNormalOriginOp);

  if (gNormalOriginOps->IsEmpty()) {
    gNormalOriginOps = nullptr;
  }
}

void QuotaManager::RegisterDirectoryLock(DirectoryLockImpl& aLock) {
  AssertIsOnOwningThread();

  mDirectoryLocks.AppendElement(WrapNotNullUnchecked(&aLock));

  if (aLock.mExclusive) {
    mExclusiveDirectoryLocks.AppendElement(WrapNotNull(&aLock));
  }

  if (aLock.ShouldUpdateLockIdTable()) {
    MutexAutoLock lock(mQuotaMutex);

    MOZ_DIAGNOSTIC_ASSERT(!mDirectoryLockIdTable.Contains(aLock.Id()));
    mDirectoryLockIdTable.InsertOrUpdate(aLock.Id(),
                                         WrapNotNullUnchecked(&aLock));
  }

  aLock.SetRegistered(true);
}

void QuotaManager::UnregisterDirectoryLock(DirectoryLockImpl& aLock) {
  AssertIsOnOwningThread();

  MOZ_ALWAYS_TRUE(mDirectoryLocks.RemoveElement(&aLock));

  if (aLock.mExclusive) {
    MOZ_ALWAYS_TRUE(mExclusiveDirectoryLocks.RemoveElement(&aLock));
  }

  if (aLock.ShouldUpdateLockIdTable()) {
    MutexAutoLock lock(mQuotaMutex);

    MOZ_DIAGNOSTIC_ASSERT(mDirectoryLockIdTable.Contains(aLock.Id()));
    mDirectoryLockIdTable.Remove(aLock.Id());
  }

  aLock.SetRegistered(false);
}

void QuotaManager::AddPendingDirectoryLock(DirectoryLockImpl& aLock) {
  AssertIsOnOwningThread();

  mPendingDirectoryLocks.AppendElement(&aLock);
}

void QuotaManager::RemovePendingDirectoryLock(DirectoryLockImpl& aLock) {
  AssertIsOnOwningThread();

  MOZ_ALWAYS_TRUE(mPendingDirectoryLocks.RemoveElement(&aLock));
}

uint64_t QuotaManager::CollectOriginsForEviction(
    int64_t aMinSizeToBeFreed, nsTArray<RefPtr<OriginDirectoryLock>>& aLocks) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aLocks.IsEmpty());

  // XXX This looks as if this could/should also use CollectLRUOriginInfosUntil,
  // or maybe a generalization if that.

  struct MOZ_STACK_CLASS Helper final {
    static void GetInactiveOriginInfos(
        const nsTArray<NotNull<RefPtr<OriginInfo>>>& aOriginInfos,
        const nsTArray<NotNull<const DirectoryLockImpl*>>& aLocks,
        OriginInfosFlatTraversable& aInactiveOriginInfos) {
      for (const auto& originInfo : aOriginInfos) {
        MOZ_ASSERT(originInfo->mGroupInfo->mPersistenceType !=
                   PERSISTENCE_TYPE_PERSISTENT);

        if (originInfo->LockedPersisted()) {
          continue;
        }

        // Never evict PERSISTENCE_TYPE_DEFAULT data associated to a
        // moz-extension origin, unlike websites (which may more likely using
        // the local data as a cache but still able to retrieve the same data
        // from the server side) extensions do not have the same data stored
        // anywhere else and evicting the data would result into potential data
        // loss for the users.
        //
        // Also, unlike a website the extensions are explicitly installed and
        // uninstalled by the user and all data associated to the extension
        // principal will be completely removed once the addon is uninstalled.
        if (originInfo->mGroupInfo->mPersistenceType !=
                PERSISTENCE_TYPE_TEMPORARY &&
            originInfo->IsExtensionOrigin()) {
          continue;
        }

        const auto originScope =
            OriginScope::FromOrigin(originInfo->FlattenToOriginMetadata());

        const bool match =
            std::any_of(aLocks.begin(), aLocks.end(),
                        [&originScope](const DirectoryLockImpl* const lock) {
                          return originScope.Matches(lock->GetOriginScope());
                        });

        if (!match) {
          // Inactive origins shouldn't have open files
          if (NS_WARN_IF(originInfo->mCanonicalQuotaObjects.Count())) {
            continue;
          }
          aInactiveOriginInfos.InsertElementSorted(
              originInfo, OriginInfoAccessTimeComparator());
        }
      }
    }
  };

  // Split locks into separate arrays and filter out locks for persistent
  // storage, they can't block us.
  auto [temporaryStorageLocks, defaultStorageLocks,
        privateStorageLocks] = [this] {
    nsTArray<NotNull<const DirectoryLockImpl*>> temporaryStorageLocks;
    nsTArray<NotNull<const DirectoryLockImpl*>> defaultStorageLocks;
    nsTArray<NotNull<const DirectoryLockImpl*>> privateStorageLocks;

    for (NotNull<const DirectoryLockImpl*> const lock : mDirectoryLocks) {
      const PersistenceScope& persistenceScope = lock->PersistenceScopeRef();

      if (persistenceScope.Matches(
              PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_TEMPORARY))) {
        temporaryStorageLocks.AppendElement(lock);
      }

      if (persistenceScope.Matches(
              PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_DEFAULT))) {
        defaultStorageLocks.AppendElement(lock);
      }

      if (persistenceScope.Matches(
              PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_PRIVATE))) {
        privateStorageLocks.AppendElement(lock);
      }
    }

    return std::make_tuple(std::move(temporaryStorageLocks),
                           std::move(defaultStorageLocks),
                           std::move(privateStorageLocks));
  }();

  // Enumerate and process inactive origins. This must be protected by the
  // mutex.
  MutexAutoLock lock(mQuotaMutex);

  const auto [inactiveOrigins, sizeToBeFreed] =
      [this, &temporaryStorageLocks = temporaryStorageLocks,
       &defaultStorageLocks = defaultStorageLocks,
       &privateStorageLocks = privateStorageLocks, aMinSizeToBeFreed] {
        nsTArray<NotNull<RefPtr<const OriginInfo>>> inactiveOrigins;
        for (const auto& entry : mGroupInfoPairs) {
          const auto& pair = entry.GetData();

          MOZ_ASSERT(!entry.GetKey().IsEmpty());
          MOZ_ASSERT(pair);

          RefPtr<GroupInfo> groupInfo =
              pair->LockedGetGroupInfo(PERSISTENCE_TYPE_TEMPORARY);
          if (groupInfo) {
            Helper::GetInactiveOriginInfos(groupInfo->mOriginInfos,
                                           temporaryStorageLocks,
                                           inactiveOrigins);
          }

          groupInfo = pair->LockedGetGroupInfo(PERSISTENCE_TYPE_DEFAULT);
          if (groupInfo) {
            Helper::GetInactiveOriginInfos(
                groupInfo->mOriginInfos, defaultStorageLocks, inactiveOrigins);
          }

          groupInfo = pair->LockedGetGroupInfo(PERSISTENCE_TYPE_PRIVATE);
          if (groupInfo) {
            Helper::GetInactiveOriginInfos(
                groupInfo->mOriginInfos, privateStorageLocks, inactiveOrigins);
          }
        }

#ifdef DEBUG
        // Make sure the array is sorted correctly.
        const bool inactiveOriginsSorted =
            std::is_sorted(inactiveOrigins.cbegin(), inactiveOrigins.cend(),
                           [](const auto& lhs, const auto& rhs) {
                             return lhs->mAccessTime < rhs->mAccessTime;
                           });
        MOZ_ASSERT(inactiveOriginsSorted);
#endif

        // Create a list of inactive and the least recently used origins
        // whose aggregate size is greater or equals the minimal size to be
        // freed.
        int64_t sizeToBeFreed = 0;
        for (uint32_t count = inactiveOrigins.Length(), index = 0;
             index < count; index++) {
          if (sizeToBeFreed >= aMinSizeToBeFreed) {
            inactiveOrigins.TruncateLength(index);
            break;
          }

          sizeToBeFreed += inactiveOrigins[index]->LockedUsage();
        }

        return std::pair(std::move(inactiveOrigins), sizeToBeFreed);
      }();

  if (sizeToBeFreed >= aMinSizeToBeFreed) {
    // Success, add directory locks for these origins, so any other
    // operations for them will be delayed (until origin eviction is finalized).

    for (const auto& originInfo : inactiveOrigins) {
      auto lock = OriginDirectoryLock::CreateForEviction(
          WrapNotNullUnchecked(this), originInfo->mGroupInfo->mPersistenceType,
          originInfo->FlattenToOriginMetadata());

      lock->AcquireImmediately();

      aLocks.AppendElement(lock.forget());
    }

    return sizeToBeFreed;
  }

  return 0;
}

nsresult QuotaManager::Init() {
  AssertIsOnOwningThread();

#ifdef XP_WIN
  CacheUseDOSDevicePathSyntaxPrefValue();
#endif

  QM_TRY_INSPECT(const auto& baseDir, QM_NewLocalFile(mBasePath));

  QM_TRY_UNWRAP(
      do_Init(mIndexedDBPath),
      GetPathForStorage(*baseDir, nsLiteralString(INDEXEDDB_DIRECTORY_NAME)));

  QM_TRY(MOZ_TO_RESULT(baseDir->Append(mStorageName)));

  QM_TRY_UNWRAP(do_Init(mStoragePath),
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, baseDir, GetPath));

  QM_TRY_UNWRAP(
      do_Init(mStorageArchivesPath),
      GetPathForStorage(*baseDir, nsLiteralString(ARCHIVES_DIRECTORY_NAME)));

  QM_TRY_UNWRAP(
      do_Init(mPermanentStoragePath),
      GetPathForStorage(*baseDir, nsLiteralString(PERMANENT_DIRECTORY_NAME)));

  QM_TRY_UNWRAP(
      do_Init(mTemporaryStoragePath),
      GetPathForStorage(*baseDir, nsLiteralString(TEMPORARY_DIRECTORY_NAME)));

  QM_TRY_UNWRAP(
      do_Init(mDefaultStoragePath),
      GetPathForStorage(*baseDir, nsLiteralString(DEFAULT_DIRECTORY_NAME)));

  QM_TRY_UNWRAP(
      do_Init(mPrivateStoragePath),
      GetPathForStorage(*baseDir, nsLiteralString(PRIVATE_DIRECTORY_NAME)));

  QM_TRY_UNWRAP(
      do_Init(mToBeRemovedStoragePath),
      GetPathForStorage(*baseDir, nsLiteralString(TOBEREMOVED_DIRECTORY_NAME)));

  QM_TRY_UNWRAP(do_Init(mIOThread),
                MOZ_TO_RESULT_INVOKE_TYPED(
                    nsCOMPtr<nsIThread>, MOZ_SELECT_OVERLOAD(NS_NewNamedThread),
                    "QuotaManager IO"));

  // XXX This could be eventually moved to nsThreadUtils.h or nsIThread
  // could have an infallible method returning PRThread as return value.
  auto PRThreadFromThread = [](nsIThread* aThread) {
    MOZ_ASSERT(aThread);

    PRThread* result;
    MOZ_ALWAYS_SUCCEEDS(aThread->GetPRThread(&result));
    MOZ_ASSERT(result);

    return result;
  };

  mIOThreadAccessible.Transfer(PRThreadFromThread(*mIOThread));

  static_assert(Client::IDB == 0 && Client::DOMCACHE == 1 && Client::SDB == 2 &&
                    Client::FILESYSTEM == 3 && Client::LS == 4 &&
                    Client::TYPE_MAX == 5,
                "Fix the registration!");

  // Register clients.
  auto clients = decltype(mClients)::ValueType{};
  clients.AppendElement(indexedDB::CreateQuotaClient());
  clients.AppendElement(cache::CreateQuotaClient());
  clients.AppendElement(simpledb::CreateQuotaClient());
  clients.AppendElement(fs::CreateQuotaClient());
  if (NextGenLocalStorageEnabled()) {
    clients.AppendElement(localstorage::CreateQuotaClient());
  } else {
    clients.SetLength(Client::TypeMax());
  }

  mClients.init(std::move(clients));

  MOZ_ASSERT(mClients->Capacity() == Client::TYPE_MAX,
             "Should be using an auto array with correct capacity!");

  mAllClientTypes.init(ClientTypesArray{
      Client::Type::IDB, Client::Type::DOMCACHE, Client::Type::SDB,
      Client::Type::FILESYSTEM, Client::Type::LS});
  mAllClientTypesExceptLS.init(
      ClientTypesArray{Client::Type::IDB, Client::Type::DOMCACHE,
                       Client::Type::SDB, Client::Type::FILESYSTEM});

  return NS_OK;
}

// static
void QuotaManager::MaybeRecordQuotaClientShutdownStep(
    const Client::Type aClientType, const nsACString& aStepDescription) {
  // Callable on any thread.

  auto* const quotaManager = QuotaManager::Get();
  MOZ_DIAGNOSTIC_ASSERT(quotaManager);

  if (quotaManager->IsShuttingDown()) {
    quotaManager->RecordShutdownStep(Some(aClientType), aStepDescription);
  }
}

// static
void QuotaManager::SafeMaybeRecordQuotaClientShutdownStep(
    const Client::Type aClientType, const nsACString& aStepDescription) {
  // Callable on any thread.

  auto* const quotaManager = QuotaManager::Get();

  if (quotaManager && quotaManager->IsShuttingDown()) {
    quotaManager->RecordShutdownStep(Some(aClientType), aStepDescription);
  }
}

void QuotaManager::RecordQuotaManagerShutdownStep(
    const nsACString& aStepDescription) {
  // Callable on any thread.
  MOZ_ASSERT(IsShuttingDown());

  RecordShutdownStep(Nothing{}, aStepDescription);
}

void QuotaManager::MaybeRecordQuotaManagerShutdownStep(
    const nsACString& aStepDescription) {
  // Callable on any thread.

  if (IsShuttingDown()) {
    RecordQuotaManagerShutdownStep(aStepDescription);
  }
}

void QuotaManager::RecordShutdownStep(const Maybe<Client::Type> aClientType,
                                      const nsACString& aStepDescription) {
  MOZ_ASSERT(IsShuttingDown());

  const TimeDuration elapsedSinceShutdownStart =
      TimeStamp::NowLoRes() - *mShutdownStartedAt;

  const auto stepString =
      nsPrintfCString("%fs: %s", elapsedSinceShutdownStart.ToSeconds(),
                      nsPromiseFlatCString(aStepDescription).get());

  if (aClientType) {
    AssertIsOnBackgroundThread();

    mShutdownSteps[*aClientType].Append(stepString + "\n"_ns);
  } else {
    // Callable on any thread.
    MutexAutoLock lock(mQuotaMutex);

    mQuotaManagerShutdownSteps.Append(stepString + "\n"_ns);
  }

  if (QM_LOG_TEST()) {
    const nsAutoCString clientTypeString =
        aClientType ? Client::TypeToText(*aClientType)
                    : nsAutoCString("quota manager"_ns);

    QM_LOG(("%s shutdown step: %s", clientTypeString.get(), stepString.get()));
  }
}

void QuotaManager::Shutdown() {
  AssertIsOnOwningThread();
  MOZ_DIAGNOSTIC_ASSERT(!gShutdown);

  // Define some local helper functions

  auto flagShutdownStarted = [this]() {
    mShutdownStartedAt.init(TimeStamp::NowLoRes());

    // Setting this flag prevents the service from being recreated and prevents
    // further storages from being created.
    gShutdown = true;
  };

  nsCOMPtr<nsITimer> crashBrowserTimer;

  auto crashBrowserTimerCallback = [](nsITimer* aTimer, void* aClosure) {
    auto* const quotaManager = static_cast<QuotaManager*>(aClosure);

    quotaManager->RecordQuotaManagerShutdownStep(
        "crashBrowserTimerCallback"_ns);

    nsCString annotation;

    for (Client::Type type : quotaManager->AllClientTypes()) {
      auto& quotaClient = *(*quotaManager->mClients)[type];

      if (!quotaClient.IsShutdownCompleted()) {
        annotation.AppendPrintf("%s: %s\nIntermediate steps:\n%s\n\n",
                                Client::TypeToText(type).get(),
                                quotaClient.GetShutdownStatus().get(),
                                quotaManager->mShutdownSteps[type].get());
      }
    }

    if (gNormalOriginOps) {
      annotation.AppendPrintf("QM: %zu normal origin ops pending\n",
                              gNormalOriginOps->Length());

      for (const auto& op : *gNormalOriginOps) {
#ifdef QM_COLLECTING_OPERATION_TELEMETRY
        annotation.AppendPrintf("Op: %s pending\n", op->Name());
#endif

        nsCString data;
        op->Stringify(data);

        annotation.AppendPrintf("Op details:\n%s\n", data.get());
      }
    }
    {
      MutexAutoLock lock(quotaManager->mQuotaMutex);

      annotation.AppendPrintf("Intermediate steps:\n%s\n",
                              quotaManager->mQuotaManagerShutdownSteps.get());
    }

    CrashReporter::RecordAnnotationNSCString(
        CrashReporter::Annotation::QuotaManagerShutdownTimeout, annotation);

    MOZ_CRASH("Quota manager shutdown timed out");
  };

  auto startCrashBrowserTimer = [&]() {
    crashBrowserTimer = NS_NewTimer();
    MOZ_ASSERT(crashBrowserTimer);
    if (crashBrowserTimer) {
      RecordQuotaManagerShutdownStep("startCrashBrowserTimer"_ns);
      MOZ_ALWAYS_SUCCEEDS(crashBrowserTimer->InitWithNamedFuncCallback(
          crashBrowserTimerCallback, this, SHUTDOWN_CRASH_BROWSER_TIMEOUT_MS,
          nsITimer::TYPE_ONE_SHOT,
          "quota::QuotaManager::Shutdown::crashBrowserTimer"_ns));
    }
  };

  auto stopCrashBrowserTimer = [&]() {
    if (crashBrowserTimer) {
      RecordQuotaManagerShutdownStep("stopCrashBrowserTimer"_ns);
      QM_WARNONLY_TRY(QM_TO_RESULT(crashBrowserTimer->Cancel()));
    }
  };

  auto initiateShutdownWorkThreads = [this]() {
    RecordQuotaManagerShutdownStep("initiateShutdownWorkThreads"_ns);
    bool needsToWait = false;
    for (Client::Type type : AllClientTypes()) {
      // Clients are supposed to also AbortAllOperations from this point on
      // to speed up shutdown, if possible. Thus pending operations
      // might not be executed anymore.
      needsToWait |= (*mClients)[type]->InitiateShutdownWorkThreads();
    }

    return needsToWait;
  };

  nsCOMPtr<nsITimer> killActorsTimer;

  auto killActorsTimerCallback = [](nsITimer* aTimer, void* aClosure) {
    auto* const quotaManager = static_cast<QuotaManager*>(aClosure);

    quotaManager->RecordQuotaManagerShutdownStep("killActorsTimerCallback"_ns);

    // XXX: This abort is a workaround to unblock shutdown, which
    // ought to be removed by bug 1682326. We probably need more
    // checks to immediately abort new operations during
    // shutdown.
    quotaManager->GetClient(Client::IDB)->AbortAllOperations();

    for (Client::Type type : quotaManager->AllClientTypes()) {
      quotaManager->GetClient(type)->ForceKillActors();
    }
  };

  auto startKillActorsTimer = [&]() {
    killActorsTimer = NS_NewTimer();
    MOZ_ASSERT(killActorsTimer);
    if (killActorsTimer) {
      RecordQuotaManagerShutdownStep("startKillActorsTimer"_ns);
      MOZ_ALWAYS_SUCCEEDS(killActorsTimer->InitWithNamedFuncCallback(
          killActorsTimerCallback, this, SHUTDOWN_KILL_ACTORS_TIMEOUT_MS,
          nsITimer::TYPE_ONE_SHOT,
          "quota::QuotaManager::Shutdown::killActorsTimer"_ns));
    }
  };

  auto stopKillActorsTimer = [&]() {
    if (killActorsTimer) {
      RecordQuotaManagerShutdownStep("stopKillActorsTimer"_ns);
      QM_WARNONLY_TRY(QM_TO_RESULT(killActorsTimer->Cancel()));
    }
  };

  auto isAllClientsShutdownComplete = [this] {
    return std::all_of(AllClientTypes().cbegin(), AllClientTypes().cend(),
                       [&self = *this](const auto type) {
                         return (*self.mClients)[type]->IsShutdownCompleted();
                       });
  };

  auto shutdownAndJoinWorkThreads = [this]() {
    RecordQuotaManagerShutdownStep("shutdownAndJoinWorkThreads"_ns);
    for (Client::Type type : AllClientTypes()) {
      (*mClients)[type]->FinalizeShutdownWorkThreads();
    }
  };

  auto shutdownAndJoinIOThread = [this]() {
    RecordQuotaManagerShutdownStep("shutdownAndJoinIOThread"_ns);

    // Make sure to join with our IO thread.
    QM_WARNONLY_TRY(QM_TO_RESULT((*mIOThread)->Shutdown()));
  };

  auto invalidatePendingDirectoryLocks = [this]() {
    RecordQuotaManagerShutdownStep("invalidatePendingDirectoryLocks"_ns);
    for (RefPtr<DirectoryLockImpl>& lock : mPendingDirectoryLocks) {
      lock->Invalidate();
    }
  };

  // Body of the function

  QM_SCOPED_CONTEXT("dom::quota::QuotaManager::Shutdown"_ns);
  GECKO_TRACE_SCOPE("dom::quota", "QuotaManager::Shutdown");

  // We always need to ensure that firefox does not shutdown with a private
  // repository still on disk. They are ideally cleaned up on PBM session end
  // but, in some cases like PBM autostart (i.e.
  // browser.privatebrowsing.autostart), private repository could only be
  // cleaned up on shutdown. ClearPrivateRepository below runs a async op and is
  // better to do it before we run the ShutdownStorageOp since it expects all
  // cleanup operations to be done by that point. We don't need to use the
  // returned promise here because `ClearPrivateRepository` registers the
  // underlying `ClearPrivateRepositoryOp` in `gNormalOriginOps`.
  ClearPrivateRepository();

  // This must be called before `flagShutdownStarted`, it would fail otherwise.
  // `ShutdownStorageOp` needs to acquire an exclusive directory lock over
  // entire <profile>/storage which will abort any existing operations and wait
  // for all existing directory locks to be released. So the shutdown operation
  // will effectively run after all existing operations.
  // Similar, to ClearPrivateRepository operation above, ShutdownStorageOp also
  // registers it's operation in `gNormalOriginOps` so we don't need to assign
  // returned promise.
  ShutdownStorage();

  flagShutdownStarted();

  startCrashBrowserTimer();

  // XXX: StopIdleMaintenance now just notifies all clients to abort any
  // maintenance work.
  // This could be done as part of QuotaClient::AbortAllOperations.
  StopIdleMaintenance();

  // XXX In theory, we could simplify the code below (and also the `Client`
  // interface) by removing the `initiateShutdownWorkThreads` and
  // `isAllClientsShutdownComplete` calls because it should be sufficient
  // to rely on `ShutdownStorage` to abort all existing operations and to
  // wait for all existing directory locks to be released as well.
  //
  // This might not be possible after adding mInitializingAllTemporaryOrigins
  // to the checks below.

  const bool needsToWait = initiateShutdownWorkThreads() ||
                           static_cast<bool>(gNormalOriginOps) ||
                           mInitializingAllTemporaryOrigins;

  // If any clients cannot shutdown immediately, spin the event loop while we
  // wait on all the threads to close.
  if (needsToWait) {
    startKillActorsTimer();

    MOZ_ALWAYS_TRUE(SpinEventLoopUntil(
        "QuotaManager::Shutdown"_ns, [this, isAllClientsShutdownComplete]() {
          return !gNormalOriginOps && isAllClientsShutdownComplete() &&
                 !mInitializingAllTemporaryOrigins;
        }));

    stopKillActorsTimer();
  }

  shutdownAndJoinWorkThreads();

  shutdownAndJoinIOThread();

  invalidatePendingDirectoryLocks();

  stopCrashBrowserTimer();
}

OriginCacheMap& OriginCacheMap::Inactive() {
  static OriginCacheMap sInstance;
  return sInstance;
}

void QuotaManager::InitQuotaForOrigin(
    const FullOriginMetadata& aFullOriginMetadata, bool aDirectoryExists) {
  InitQuotaForOrigin(aFullOriginMetadata, aDirectoryExists,
                     OriginCacheMap::Inactive());
}

void QuotaManager::InitQuotaForOrigin(
    const FullOriginMetadata& aFullOriginMetadata, bool aDirectoryExists,
    OriginCacheMap& aCacheMap) {
  AssertIsOnIOThread();
  MOZ_ASSERT(IsBestEffortPersistenceType(aFullOriginMetadata.mPersistenceType));

  // When called from the InitializeRepository disk-scan path, claim the
  // matching cache entry so leftover entries can later be deleted as
  // stale rows. If the cached row already matches the metadata we'd
  // write, we can skip the dirty-queue push: the existing row is
  // already correct.
  bool cacheRowMatches = false;

  // Usages are accumulated as a sum, so a scan has to replace them wholesale.
  // Timestamps accumulate as a maximum instead: a scan can only derive
  // timestamps from file modification times, so an existing row that knows
  // about a more recent value wins.
  FullOriginMetadata fullOriginMetadata = aFullOriginMetadata.Clone();

  if (aCacheMap.IsActive()) {
    Maybe<FullOriginMetadata> cachedMetadata = aCacheMap.Extract(
        fullOriginMetadata.mPersistenceType, fullOriginMetadata.mOrigin);
    if (cachedMetadata.isSome()) {
      if (cachedMetadata->mLastAccessTime >
          fullOriginMetadata.mLastAccessTime) {
        fullOriginMetadata.mLastAccessTime = cachedMetadata->mLastAccessTime;
      }

      if (cachedMetadata->mLastMaintenanceDate >
          fullOriginMetadata.mLastMaintenanceDate) {
        fullOriginMetadata.mLastMaintenanceDate =
            cachedMetadata->mLastMaintenanceDate;
      }

      if (cachedMetadata->EqualsIgnoringIntrinsicFields(fullOriginMetadata)) {
        cacheRowMatches = true;
      }
    }
  }

  MutexAutoLock lock(mQuotaMutex);

  RefPtr<GroupInfo> groupInfo = LockedGetOrCreateGroupInfo(
      fullOriginMetadata.mPersistenceType, fullOriginMetadata.mSuffix,
      fullOriginMetadata.mGroup);

  auto originInfo = MakeRefPtr<OriginInfo>(
      groupInfo, aFullOriginMetadata.mOrigin,
      aFullOriginMetadata.mStorageOrigin, aFullOriginMetadata.mIsPrivate,
      aFullOriginMetadata.mIsEncrypted, aFullOriginMetadata.mClientUsages,
      aFullOriginMetadata.mOriginUsage, aFullOriginMetadata.mLastAccessTime,
      aFullOriginMetadata.mLastMaintenanceDate, aFullOriginMetadata.mPersisted,
      aDirectoryExists);

  // A full directory scan is always performed before InitQuotaForOrigin
  // receives a dirty origin. The two paths are:
  //
  // 1. Database cache dirty origins: RestoreAndInitializeOrigin ->
  //    ResolveRepositoryEntry -> InitializeOriginDirectory ->
  //    InitializeOrigin (full scan via CollectEachFile).
  // 2. On-disk .metadata-v2 dirty origins: same InitializeOriginDirectory ->
  //    InitializeOrigin path.
  //
  // This is guaranteed because the secondary-cache shortcut in
  // InitializeOriginDirectory requires !mAccessed, but mDirty can only be
  // true when mAccessed is true. Dirty origins fall through to
  // InitializeOrigin.
  //
  // So the usage data in fullOriginMetadata reflects the actual on-disk
  // state. Enqueuing here ensures the corrected metadata is flushed back.
  //
  // We set mMetadataDirty directly because the OriginInfo is not yet
  // registered in GroupInfo, so DirtyTrackingAutoLock cannot look it up. Set
  // mLastModifiedTime here as well to match OriginInfo::MakeDirty; otherwise,
  // FlushDirtyOriginInfos subtracts a null TimeStamp from now.
  //
  // Enqueue for storage-database flush when the existing row is stale or
  // missing.
  //
  // During a disk scan, dirty origins are enqueued so corrected metadata
  // is flushed back.  When the storage database is fresh (no origin
  // rows, inactive reconciliation map), every origin with a directory
  // is enqueued to populate it for the first time.
  //
  // Origins loaded directly from the storage database are already
  // correct.  Callers pass an active reconciliation map for these, so
  // cacheRowMatches is true and no enqueue happens.
  if (!aFullOriginMetadata.mIsPrivate && !cacheRowMatches &&
      (fullOriginMetadata.mDirty || !aCacheMap.IsActive()) &&
      aDirectoryExists && !mUsageModificationDisabled.load()) {
    originInfo->mLastModifiedTime = TimeStamp::Now();
    originInfo->mMetadataDirty = true;
    auto* message = new UnboundedMPSCQueue<RefPtr<OriginInfo>>::Message();
    message->data = originInfo;
    mDirtyOriginInfos.Push(message);
  }

  groupInfo->LockedAddOriginInfo(WrapNotNullUnchecked(std::move(originInfo)));
}

void QuotaManager::DecreaseUsageForClient(const ClientMetadata& aClientMetadata,
                                          int64_t aSize) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(IsBestEffortPersistenceType(aClientMetadata.mPersistenceType));

  DirtyTrackingAutoLock lock(mQuotaMutex, mGroupInfoPairs, aClientMetadata);
  if (!lock.IsValid()) {
    return;
  }

  RefPtr<OriginInfo> originInfo = lock.GetOriginInfo();
  originInfo->LockedDecreaseUsage(aClientMetadata.mClientType, aSize, lock);
}

void QuotaManager::ResetUsageForClient(const ClientMetadata& aClientMetadata) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(IsBestEffortPersistenceType(aClientMetadata.mPersistenceType));

  DirtyTrackingAutoLock lock(
      mQuotaMutex, mGroupInfoPairs,
      static_cast<const OriginMetadata&>(aClientMetadata));
  if (!lock.IsValid()) {
    return;
  }

  RefPtr<OriginInfo> originInfo = lock.GetOriginInfo();
  originInfo->LockedResetUsageForClient(aClientMetadata.mClientType, lock);
}

UsageInfo QuotaManager::GetUsageForClient(PersistenceType aPersistenceType,
                                          const OriginMetadata& aOriginMetadata,
                                          Client::Type aClientType) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  MutexAutoLock lock(mQuotaMutex);

  GroupInfoPair* pair;
  if (!mGroupInfoPairs.Get(aOriginMetadata.mGroup, &pair)) {
    return UsageInfo{};
  }

  RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(aPersistenceType);
  if (!groupInfo) {
    return UsageInfo{};
  }

  RefPtr<OriginInfo> originInfo =
      groupInfo->LockedGetOriginInfo(aOriginMetadata.mOrigin);
  if (!originInfo) {
    return UsageInfo{};
  }

  return originInfo->LockedGetUsageForClient(aClientType);
}

void QuotaManager::UpdateOriginAccessTime(const OriginMetadata& aOriginMetadata,
                                          int64_t aTimestamp) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  DirtyTrackingAutoLock lock(mQuotaMutex, mGroupInfoPairs, aOriginMetadata);
  if (!lock.IsValid()) {
    return;
  }

  RefPtr<OriginInfo> originInfo = lock.GetOriginInfo();
  originInfo->LockedUpdateAccessTime(aTimestamp, /* aProofOfLock */ lock);
}

void QuotaManager::UpdateOriginMaintenanceDate(
    const OriginMetadata& aOriginMetadata, int32_t aMaintenanceDate) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  MutexAutoLock lock(mQuotaMutex);

  GroupInfoPair* pair;
  if (!mGroupInfoPairs.Get(aOriginMetadata.mGroup, &pair)) {
    return;
  }

  RefPtr<GroupInfo> groupInfo =
      pair->LockedGetGroupInfo(aOriginMetadata.mPersistenceType);
  if (!groupInfo) {
    return;
  }

  RefPtr<OriginInfo> originInfo =
      groupInfo->LockedGetOriginInfo(aOriginMetadata.mOrigin);
  if (!originInfo) {
    return;
  }

  originInfo->LockedUpdateMaintenanceDate(aMaintenanceDate);
}

void QuotaManager::UpdateOriginAccessed(const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  DirtyTrackingAutoLock lock(mQuotaMutex, mGroupInfoPairs, aOriginMetadata);
  if (!lock.IsValid()) {
    return;
  }

  RefPtr<OriginInfo> originInfo = lock.GetOriginInfo();
  originInfo->LockedUpdateAccessed(/* aProofOfLock */ lock);
}

void QuotaManager::RemoveQuota() {
  AssertIsOnIOThread();

  // Prevents new dirty origin infos from being enqueued after RemoveQuota
  // has started tearing down. Without this, a concurrent
  // DirtyTrackingAutoLock could push an OriginInfo after we drain the queue
  // below, causing the flush timer to write to an origin whose quota has
  // already been removed.
  mUsageModificationDisabled.store(true);

  MutexAutoLock lock(mQuotaMutex);

  for (const auto& entry : mGroupInfoPairs) {
    const auto& pair = entry.GetData();

    MOZ_ASSERT(!entry.GetKey().IsEmpty());
    MOZ_ASSERT(pair);

    RefPtr<GroupInfo> groupInfo =
        pair->LockedGetGroupInfo(PERSISTENCE_TYPE_TEMPORARY);
    if (groupInfo) {
      groupInfo->LockedRemoveOriginInfos();
    }

    groupInfo = pair->LockedGetGroupInfo(PERSISTENCE_TYPE_DEFAULT);
    if (groupInfo) {
      groupInfo->LockedRemoveOriginInfos();
    }

    groupInfo = pair->LockedGetGroupInfo(PERSISTENCE_TYPE_PRIVATE);
    if (groupInfo) {
      groupInfo->LockedRemoveOriginInfos();
    }
  }

  mGroupInfoPairs.Clear();

  // Drain any remaining dirty origin infos. RegisterDirtyOriginInfo checks
  // mUsageModificationDisabled before pushing, but a concurrent call on
  // another thread may have already read the flag as false and not yet
  // pushed its entry. Since the push targets a lock-free MPSC queue, it
  // can complete without the quota mutex, so entries may appear after the
  // mutex was acquired.
  RefPtr<OriginInfo> info;
  while (true) {
    if (!mDirtyOriginInfos.Pop(&info)) {
      break;
    }
  }

  MOZ_ASSERT(mTemporaryStorageUsage == 0, "Should be zero!");
}

// XXX Rename this method because the method doesn't load full quota
// information if origin initialization is done lazily.
nsresult QuotaManager::LoadQuota() {
  GECKO_TRACE_SCOPE("dom::quota", "QuotaManager::LoadQuota");

  AssertIsOnIOThread();
  MOZ_ASSERT(mStorageConnection);
  MOZ_ASSERT(!mTemporaryStorageInitializedInternal);

  // If we are shutting down, it's too late to load quota. Stop now: any rescan
  // needed will be done on next startup.
  QM_TRY(OkIf(!IsShuttingDown()), NS_ERROR_ABORT);

  // A list of all unaccessed default or temporary origins.
  nsTArray<FullOriginMetadata> unaccessedOrigins;

  // XXX The list of all unaccessed default or temporary origins can be now
  // generated from mAllTemporaryOrigins.
  auto MaybeCollectUnaccessedOrigin =
      [loadQuotaInfoStartTime = PR_Now(),
       &unaccessedOrigins](const auto& fullOriginMetadata) {
        if (IsOriginUnaccessed(fullOriginMetadata, loadQuotaInfoStartTime)) {
          unaccessedOrigins.AppendElement(fullOriginMetadata);
        }
      };

  auto RestoreAndInitializeOrigin =
      [this, &MaybeCollectUnaccessedOrigin](
          const FullOriginMetadata& aFullOriginMetadata,
          nsTArray<RenameAndInitInfo>& aRenameAndInitInfos)
      -> Result<Ok, nsresult> {
    QM_TRY_INSPECT(const auto& originDirectory,
                   GetOriginDirectory(aFullOriginMetadata));

    QM_TRY(MOZ_TO_RESULT(ResolveRepositoryEntry(
        originDirectory, aFullOriginMetadata.mPersistenceType,
        aRenameAndInitInfos, MaybeCollectUnaccessedOrigin,
        OriginCacheMap::Inactive())));

    return Ok{};
  };

  auto recordTimeDeltaHelper =
      MakeRefPtr<RecordTimeDeltaHelper>(glean::dom_quota::info_load_time);

  const auto startTime = recordTimeDeltaHelper->Start();

  auto LoadQuotaFromCache =
      [&](nsTArray<FullOriginMetadata>& aDirtyOrigins) -> nsresult {
    QM_TRY_INSPECT(
        const auto& stmt,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
            nsCOMPtr<mozIStorageStatement>, mStorageConnection, CreateStatement,
            "SELECT repository_id, suffix, group_, "
            "origin, client_usages, usage, "
            "last_access_time, last_maintenance_date, metadata_flags "
            "FROM origin"_ns));

    // Origins loaded from the storage database are already correct, so
    // InitQuotaForOrigin must not enqueue them for a flush.  We pass an
    // active reconciliation map so each origin matches its own row and
    // cacheRowMatches evaluates to true.
    OriginCacheMap cacheMap;
    cacheMap.Activate();

    QM_TRY(quota::CollectWhileHasResult(
        *stmt,
        [this, &MaybeCollectUnaccessedOrigin, &aDirtyOrigins,
         &cacheMap](auto& stmt) -> Result<Ok, nsresult> {
          QM_TRY_INSPECT(const int32_t& repositoryId,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 0));

          const auto maybePersistenceType =
              PersistenceTypeFromInt32(repositoryId, fallible);
          QM_TRY(OkIf(maybePersistenceType.isSome()), Err(NS_ERROR_FAILURE));

          FullOriginMetadata fullOriginMetadata;

          fullOriginMetadata.mPersistenceType = maybePersistenceType.value();

          QM_TRY_UNWRAP(fullOriginMetadata.mSuffix,
                        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                          GetUTF8String, 1));

          QM_TRY_UNWRAP(fullOriginMetadata.mGroup,
                        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                          GetUTF8String, 2));

          QM_TRY_UNWRAP(fullOriginMetadata.mOrigin,
                        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                          GetUTF8String, 3));

          fullOriginMetadata.mStorageOrigin = fullOriginMetadata.mOrigin;

          const auto extraInfo =
              ScopedLogExtraInfo{ScopedLogExtraInfo::kTagStorageOriginTainted,
                                 fullOriginMetadata.mStorageOrigin};

          fullOriginMetadata.mIsPrivate = false;

          QM_TRY_INSPECT(const auto& clientUsagesText,
                         MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                           GetUTF8String, 4));

          ClientUsageArray clientUsages;
          QM_TRY(MOZ_TO_RESULT(clientUsages.Deserialize(clientUsagesText)));

          fullOriginMetadata.mClientUsages = clientUsages;

          QM_TRY_INSPECT(fullOriginMetadata.mOriginUsage,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 5));

          // fullOriginMetadata.mQuotaVersion is always set to kNoQuotaversion.
          // It is not cached in the database, and caching it would not make
          // sense because mQuotaVersion is currently unused by the L1 quota
          // info cache. Validation for the L1 quota info cache is performed
          // using the build ID instead.
          fullOriginMetadata.mQuotaVersion = kNoQuotaVersion;

          QM_TRY_UNWRAP(fullOriginMetadata.mLastAccessTime,
                        MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 6));
          QM_TRY_UNWRAP(fullOriginMetadata.mLastMaintenanceDate,
                        MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 7));
          QM_TRY_INSPECT(const int32_t& metadataFlags,
                         MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 8));
          fullOriginMetadata.FromMetadataFlags(
              static_cast<uint32_t>(metadataFlags));

          QM_TRY_INSPECT(const bool& groupUpdated,
                         MaybeUpdateGroupForOrigin(fullOriginMetadata));

          (void)groupUpdated;

          QM_TRY_INSPECT(
              const bool& lastAccessTimeUpdated,
              MaybeUpdateLastAccessTimeForOrigin(fullOriginMetadata));

          (void)lastAccessTimeUpdated;

          // We don't need to update the .metadata-v2 file on disk here,
          // EnsureTemporaryOriginIsInitializedInternal is responsible for
          // doing that. We just need to use correct group and last access
          // time before initializing quota for the given origin.

          const bool needsRescan =
#if defined(NIGHTLY_BUILD) || defined(DEBUG)
              !fullOriginMetadata.CheckIfUsageIsConsistent(
                  "LoadQuotaFromCache"_ns);
#else
              false;
#endif
          if (fullOriginMetadata.mDirty || needsRescan) {
            aDirtyOrigins.AppendElement(std::move(fullOriginMetadata));
          } else if (IsBestEffortPersistenceType(
                         /* Persistent origins are initialized separately */
                         fullOriginMetadata.mPersistenceType)) {
            MaybeCollectUnaccessedOrigin(fullOriginMetadata);

            if (fullOriginMetadata.mAccessed) {
              AddTemporaryOrigin(fullOriginMetadata);

              cacheMap.InsertOrUpdate(fullOriginMetadata.mPersistenceType,
                                      fullOriginMetadata.mOrigin,
                                      fullOriginMetadata.Clone());
              InitQuotaForOrigin(fullOriginMetadata,
                                 /* aDirectoryExists */ true, cacheMap);
            }
          }

          return Ok{};
        }));

    return NS_OK;
  };

  QM_TRY_UNWRAP(bool isCacheUseAllowed, ([this]() -> Result<bool, nsresult> {
                  if (!StaticPrefs::dom_quotaManager_loadQuotaFromCache()) {
                    return false;
                  }

                  if (mCacheRequiresFullScan) {
                    return false;
                  }

                  if (mCacheUsable) {
                    QM_TRY_INSPECT(const auto& stmt,
                                   CreateAndExecuteSingleStepStatement<
                                       SingleStepResult::ReturnNullIfNoResult>(
                                       *mStorageConnection,
                                       "SELECT build_id FROM cache"_ns));

                    QM_TRY(OkIf(stmt), Err(NS_ERROR_FILE_CORRUPTED));

                    if (!StaticPrefs::dom_quotaManager_caching_checkBuildId()) {
                      return true;
                    }

                    QM_TRY_INSPECT(const auto& buildId,
                                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                       nsAutoCString, stmt, GetUTF8String, 0));

                    return buildId == *gBuildId;
                  }

                  return false;
                }()));

  if (isCacheUseAllowed) {
    nsTArray<FullOriginMetadata> dirtyOrigins;
    QM_WARNONLY_TRY_UNWRAP(auto maybeOk,
                           MOZ_TO_RESULT(LoadQuotaFromCache(dirtyOrigins)));
    if (!maybeOk) {
      // We could not read the database.
      isCacheUseAllowed = false;
    } else if (!dirtyOrigins.IsEmpty()) {
      // Make sure mUsageModificationDisabled is false, otherwise origin will
      // not be processed in QuotaManager::InitQuotaForOrigin. After a shutdown
      // and reinitialization cycle (e.g. when storage was cleared),
      // RemoveQuota sets mUsageModificationDisabled to true.
      // InitializeFlushTimer would also clear it, but it runs after this
      // code path.
      mUsageModificationDisabled.store(false);

      nsTArray<RenameAndInitInfo> renameAndInitInfos;
      nsTArray<FullOriginMetadata> failedOrigins;
      for (auto& dirtyOrigin : dirtyOrigins) {
        QM_TRY(OkIf(!IsShuttingDown()), NS_ERROR_ABORT);

        QM_WARNONLY_TRY_UNWRAP(
            auto maybeOk,
            RestoreAndInitializeOrigin(dirtyOrigin, renameAndInitInfos));
        if (!maybeOk) {
          failedOrigins.AppendElement(std::move(dirtyOrigin));
        }
      }

      QM_TRY(OkIf(!IsShuttingDown()), NS_ERROR_ABORT);

      if (failedOrigins.IsEmpty()) {
        QM_TRY(MOZ_TO_RESULT(InitializeFlushTimer()));

        return NS_OK;
      }

      for (auto& failedOrigin : failedOrigins) {
        // Persistent origins are initialized separately
        if (!IsBestEffortPersistenceType(failedOrigin.mPersistenceType)) {
          continue;
        }

        auto dirResult = GetOriginDirectory(failedOrigin);
        nsresult rv = NS_OK;
        if (dirResult.isOk()) {
          rv = dirResult.inspect()->Remove(/* aRecursive */ true);
        } else {
          rv = dirResult.inspectErr();
        }

        RemoveTemporaryOrigin(failedOrigin);

        if (NS_FAILED(rv)) {
          nsCString path = dirResult.inspect()->HumanReadablePath();
          QM_LOG(("Error %s: Could not remove directory '%s' ",
                  GetStaticErrorName(rv), path.get()));
          // And from now on we just skip this origin until user sees the error
          // in the logs and removes the directory manually.
        } else {
          AddTemporaryOrigin(failedOrigin);

          InitQuotaForOrigin(failedOrigin, /* aDirectoryExists */ false);
        }
      }

      QM_TRY(MOZ_TO_RESULT(InitializeFlushTimer()));

      return NS_OK;
    } else {
      // Successfully loaded quota info from cache.
      QM_TRY(MOZ_TO_RESULT(InitializeFlushTimer()));

      return NS_OK;
    }
  }

  if (!isCacheUseAllowed) {
    RemoveQuota();
    RemoveTemporaryOrigins();
    unaccessedOrigins.Clear();

    // RemoveQuota sets mUsageModificationDisabled to prevent concurrent
    // dirty-origin pushes during teardown. Re-enable it now so that the
    // disk scan below can register dirty origins for DB write.
    mUsageModificationDisabled.store(false);

    // A keeper to defer the return only in Nightly, so that the telemetry data
    // for whole profile can be collected.
#ifdef NIGHTLY_BUILD
    nsresult statusKeeper = NS_OK;
#endif

    const auto statusKeeperFunc = [&](const nsresult rv) {
      RECORD_IN_NIGHTLY(statusKeeper, rv);
    };

    for (const PersistenceType type :
         kInitializableBestEffortPersistenceTypes) {
      if (NS_WARN_IF(IsShuttingDown())) {
        RETURN_STATUS_OR_RESULT(statusKeeper, NS_ERROR_ABORT);
      }

      QM_TRY(([&]() -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(([this, type, &MaybeCollectUnaccessedOrigin] {
                 const auto innerFunc = [&](const auto&) -> nsresult {
                   return InitializeRepository(type,
                                               MaybeCollectUnaccessedOrigin);
                 };

                 return ExecuteInitialization(
                     type == PERSISTENCE_TYPE_DEFAULT
                         ? Initialization::DefaultRepository
                         : Initialization::TemporaryRepository,
                     innerFunc);
               }())),
               OK_IN_NIGHTLY_PROPAGATE_IN_OTHERS, statusKeeperFunc);

        return Ok{};
      }()));
    }

#ifdef NIGHTLY_BUILD
    if (NS_FAILED(statusKeeper)) {
      return statusKeeper;
    }
#endif
  }

  const auto endTime = recordTimeDeltaHelper->End();

  if (StaticPrefs::dom_quotaManager_checkQuotaInfoLoadTime() &&
      static_cast<uint32_t>((endTime - startTime).ToMilliseconds()) >=
          StaticPrefs::dom_quotaManager_longQuotaInfoLoadTimeThresholdMs() &&
      !unaccessedOrigins.IsEmpty()) {
    QM_WARNONLY_TRY(ArchiveOrigins(unaccessedOrigins));
  }

  QM_TRY(MOZ_TO_RESULT(InitializeFlushTimer()));

  return NS_OK;
}

void QuotaManager::UnloadQuota() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mStorageConnection);
  MOZ_ASSERT(mTemporaryStorageInitializedInternal);

  UninitializeFlushTimer();

  auto autoRemoveQuota = MakeScopeExit([&] { RemoveQuota(); });

  // Group cache-DB upserts (OriginUpserter::Refresh, also reached through
  // SettleDirectoryMetadata2) into common transactions, to prevent each loop
  // iteration from triggering a fsync.
  // Avoid a single transaction though: if UnloadQuota still takes too long and
  // gets killed at shutdown for this reason, we don't want all the UnloadQuota
  // work to be lost, as this could result in a lot of work upon restart.
  const auto createTransaction =
      [this](Maybe<mozStorageTransaction>& transaction) {
        transaction.reset();
        transaction.emplace(mStorageConnection, /* aCommitOnComplete */ false,
                            mozIStorageConnection::TRANSACTION_IMMEDIATE);
        QM_WARNONLY_TRY(MOZ_TO_RESULT(transaction->Start()));
      };
  Maybe<mozStorageTransaction> transaction;
  createTransaction(transaction);

  static const int32_t kTransactionBatchSize = 50;
  int32_t transactionCount = 0;

  {
    MutexAutoLock lock(mQuotaMutex);

    for (auto iter = mGroupInfoPairs.Iter(); !iter.Done(); iter.Next()) {
      MOZ_ASSERT(!iter.Key().IsEmpty());

      GroupInfoPair* const pair = iter.UserData();
      MOZ_ASSERT(pair);

      for (const PersistenceType type : kBestEffortPersistenceTypes) {
        RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(type);
        if (!groupInfo) {
          continue;
        }

        for (const auto& originInfo : groupInfo->mOriginInfos) {
          MOZ_ASSERT(!originInfo->mCanonicalQuotaObjects.Count());

          if (!originInfo->LockedDirectoryExists()) {
            continue;
          }

          if (originInfo->mIsPrivate) {
            continue;
          }

          auto metadata = originInfo->LockedFlattenToFullOriginMetadata();

          if (originInfo->LockedDirty()) {
            // In-memory state diverged from disk: write the .metadata-v2
            // file and upsert the origin row in the cache DB.
            QM_WARNONLY_TRY_UNWRAP(auto originDirectory,
                                   GetOriginDirectory(metadata));
            MOZ_ASSERT(originDirectory);
            if (originDirectory) {
              DebugOnly<nsresult> rv =
                  SettleDirectoryMetadata2(*originDirectory.ref(), metadata);
              MOZ_ASSERT(NS_FAILED(rv) == metadata.mDirty);
              ++transactionCount;
            }
          } else if (mCacheRequiresFullScan) {
            // The cache DB was freshly created (or recreated after
            // corruption) and has no origin rows yet. The .metadata-v2
            // file on disk is already correct — only the DB needs
            // populating. We must not rewrite the file here because the
            // in-memory timestamps may be stale (e.g. lastAccessTime
            // could have been updated on disk after initialization).
            MOZ_ASSERT(mOriginUpserter, "We must have an origin upserter here");
            QM_WARNONLY_TRY(mOriginUpserter->Refresh(metadata));
            ++transactionCount;
          }

          if (transactionCount >= kTransactionBatchSize) {
            QM_WARNONLY_TRY(MOZ_TO_RESULT(transaction->Commit()));
            createTransaction(transaction);
            transactionCount = 0;
          }
        }

        groupInfo->LockedRemoveOriginInfos();
      }

      iter.Remove();
    }
  }

  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, mStorageConnection, CreateStatement,
          "UPDATE cache SET build_id = :buildId;"_ns),
      QM_VOID);

  QM_TRY(MOZ_TO_RESULT(stmt->BindUTF8StringByName("buildId"_ns, *gBuildId)),
         QM_VOID);
  QM_TRY(MOZ_TO_RESULT(stmt->Execute()), QM_VOID);

  QM_TRY(MOZ_TO_RESULT(transaction->Commit()), QM_VOID);
}

void QuotaManager::RemoveOriginFromCacheForEviction(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_ASSERT(mStorageConnection);

  if (!mCacheUsable) {
    return;
  }

  mozStorageTransaction transaction(
      mStorageConnection, false, mozIStorageConnection::TRANSACTION_IMMEDIATE);

  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, mStorageConnection, CreateStatement,
          "DELETE FROM origin WHERE repository_id = :repository_id AND suffix = :suffix AND group_ = :group AND origin = :origin;"_ns),
      QM_VOID);

  QM_TRY(MOZ_TO_RESULT(stmt->BindInt32ByName("repository_id"_ns,
                                             aOriginMetadata.mPersistenceType)),
         QM_VOID);
  QM_TRY(MOZ_TO_RESULT(
             stmt->BindUTF8StringByName("suffix"_ns, aOriginMetadata.mSuffix)),
         QM_VOID);
  QM_TRY(MOZ_TO_RESULT(
             stmt->BindUTF8StringByName("group"_ns, aOriginMetadata.mGroup)),
         QM_VOID);
  QM_TRY(MOZ_TO_RESULT(
             stmt->BindUTF8StringByName("origin"_ns, aOriginMetadata.mOrigin)),
         QM_VOID);
  QM_TRY(MOZ_TO_RESULT(stmt->Execute()), QM_VOID);

  QM_TRY(MOZ_TO_RESULT(transaction.Commit()), QM_VOID);
}

void QuotaManager::RemoveOriginFromCache(
    const OriginMetadata& aOriginMetadata) {
  MOZ_ASSERT(!mTemporaryStorageInitializedInternal);

  RemoveOriginFromCacheForEviction(aOriginMetadata);
}

already_AddRefed<QuotaObject> QuotaManager::GetQuotaObject(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    Client::Type aClientType, nsIFile* aFile, int64_t aFileSize,
    int64_t* aFileSizeOut /* = nullptr */) {
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == aPersistenceType);

  if (aFileSizeOut) {
    *aFileSizeOut = 0;
  }

  if (aPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
    return nullptr;
  }

  QM_TRY_INSPECT(const auto& path,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, aFile, GetPath),
                 nullptr);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const auto& directory, GetOriginDirectory(aOriginMetadata),
                   nullptr);

    nsAutoString clientType;
    QM_TRY(OkIf(Client::TypeToText(aClientType, clientType, fallible)),
           nullptr);

    QM_TRY(MOZ_TO_RESULT(directory->Append(clientType)), nullptr);

    QM_TRY_INSPECT(
        const auto& directoryPath,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, directory, GetPath),
        nullptr);

    MOZ_ASSERT(StringBeginsWith(path, directoryPath));
  }
#endif

  QM_TRY_INSPECT(
      const int64_t fileSize,
      ([&aFile, aFileSize]() -> Result<int64_t, nsresult> {
        if (aFileSize == -1) {
          QM_TRY_INSPECT(const bool& exists,
                         MOZ_TO_RESULT_INVOKE_MEMBER(aFile, Exists));

          if (exists) {
            QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(aFile, GetFileSize));
          }

          return 0;
        }

        return aFileSize;
      }()),
      nullptr);

  RefPtr<QuotaObject> result;
  {
    MutexAutoLock lock(mQuotaMutex);

    GroupInfoPair* pair;
    if (!mGroupInfoPairs.Get(aOriginMetadata.mGroup, &pair)) {
      return nullptr;
    }

    RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(aPersistenceType);

    if (!groupInfo) {
      return nullptr;
    }

    RefPtr<OriginInfo> originInfo =
        groupInfo->LockedGetOriginInfo(aOriginMetadata.mOrigin);

    if (!originInfo) {
      return nullptr;
    }

    // We need this extra raw pointer because we can't assign to the smart
    // pointer directly since QuotaObject::AddRef would try to acquire the same
    // mutex.
    const NotNull<CanonicalQuotaObject*> canonicalQuotaObject =
        originInfo->mCanonicalQuotaObjects.LookupOrInsertWith(path, [&] {
          // Create a new QuotaObject. The hashtable is not responsible to
          // delete the QuotaObject.
          return WrapNotNullUnchecked(new CanonicalQuotaObject(
              originInfo, aClientType, path, fileSize));
        });

    // Addref the QuotaObject and move the ownership to the result. This must
    // happen before we unlock!
    result = canonicalQuotaObject->LockedAddRef();
  }

  if (aFileSizeOut) {
    *aFileSizeOut = fileSize;
  }

  // The caller becomes the owner of the QuotaObject, that is, the caller is
  // is responsible to delete it when the last reference is removed.
  return result.forget();
}

already_AddRefed<QuotaObject> QuotaManager::GetQuotaObject(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    Client::Type aClientType, const nsAString& aPath, int64_t aFileSize,
    int64_t* aFileSizeOut /* = nullptr */) {
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  if (aFileSizeOut) {
    *aFileSizeOut = 0;
  }

  QM_TRY_INSPECT(const auto& file, QM_NewLocalFile(aPath), nullptr);

  return GetQuotaObject(aPersistenceType, aOriginMetadata, aClientType, file,
                        aFileSize, aFileSizeOut);
}

already_AddRefed<QuotaObject> QuotaManager::GetQuotaObject(
    const int64_t aDirectoryLockId, const nsAString& aPath) {
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  Maybe<MutexAutoLock> lock;

  // See the comment for mDirectoryLockIdTable in QuotaManager.h
  if (!IsOnBackgroundThread()) {
    lock.emplace(mQuotaMutex);
  }

  if (auto maybeDirectoryLock =
          mDirectoryLockIdTable.MaybeGet(aDirectoryLockId)) {
    const auto& directoryLock = *maybeDirectoryLock;
    MOZ_DIAGNOSTIC_ASSERT(directoryLock->ShouldUpdateLockIdTable());

    const PersistenceType persistenceType = directoryLock->GetPersistenceType();
    const OriginMetadata& originMetadata = directoryLock->OriginMetadata();
    const Client::Type clientType = directoryLock->ClientType();

    lock.reset();

    return GetQuotaObject(persistenceType, originMetadata, clientType, aPath);
  }

  MOZ_ASSERT(aDirectoryLockId == -1);
  return nullptr;
}

Nullable<bool> QuotaManager::OriginPersisted(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  MutexAutoLock lock(mQuotaMutex);

  RefPtr<OriginInfo> originInfo =
      LockedGetOriginInfo(PERSISTENCE_TYPE_DEFAULT, aOriginMetadata);
  if (originInfo) {
    return Nullable<bool>(originInfo->LockedPersisted());
  }

  return Nullable<bool>();
}

bool QuotaManager::IsOriginEncrypted(const OriginMetadata& aOriginMetadata) {
  if (aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
    return false;
  }

  MutexAutoLock lock(mQuotaMutex);

  RefPtr<OriginInfo> originInfo =
      LockedGetOriginInfo(aOriginMetadata.mPersistenceType, aOriginMetadata);
  if (originInfo) {
    return originInfo->IsEncrypted();
  }

  return mStorageEncryptionEnabled;
}

void QuotaManager::PersistOrigin(const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  DirtyTrackingAutoLock lock(mQuotaMutex, mGroupInfoPairs, aOriginMetadata);
  if (!lock.IsValid()) {
    return;
  }

  RefPtr<OriginInfo> originInfo = lock.GetOriginInfo();
  if (!originInfo->LockedPersisted()) {
    originInfo->LockedPersist(lock);
  }
}

void QuotaManager::AbortOperationsForLocks(
    const DirectoryLockIdTableArray& aLockIds) {
  for (Client::Type type : AllClientTypes()) {
    if (aLockIds[type].Filled()) {
      (*mClients)[type]->AbortOperationsForLocks(aLockIds[type]);
    }
  }
}

void QuotaManager::AbortOperationsForProcess(ContentParentId aContentParentId) {
  AssertIsOnOwningThread();

  for (const RefPtr<Client>& client : *mClients) {
    client->AbortOperationsForProcess(aContentParentId);
  }
}

Result<nsCOMPtr<nsIFile>, nsresult> QuotaManager::GetOriginDirectory(
    const OriginMetadata& aOriginMetadata) const {
  QM_TRY_UNWRAP(
      auto directory,
      QM_NewLocalFile(GetStoragePath(aOriginMetadata.mPersistenceType)));

  QM_TRY(MOZ_TO_RESULT(directory->Append(
      MakeSanitizedOriginString(aOriginMetadata.mStorageOrigin))));

  return directory;
}

Result<bool, nsresult> QuotaManager::DoesOriginDirectoryExist(
    const OriginMetadata& aOriginMetadata) const {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& directory, GetOriginDirectory(aOriginMetadata));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(directory, Exists));
}

Result<nsCOMPtr<nsIFile>, nsresult>
QuotaManager::GetOrCreateTemporaryOriginDirectory(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);
  MOZ_DIAGNOSTIC_ASSERT(IsStorageInitializedInternal());
  MOZ_DIAGNOSTIC_ASSERT(IsTemporaryStorageInitializedInternal());
  MOZ_ASSERT(IsTemporaryGroupInitializedInternal(aOriginMetadata));
  MOZ_ASSERT(IsTemporaryOriginInitializedInternal(aOriginMetadata));

  QM_SCOPED_CONTEXT(
      "dom::quota::QuotaManager::GetOrCreateTemporaryOriginDirectory"_ns);

  // XXX Temporary band-aid fix until the root cause of uninitialized origins
  // after obtaining a client directory lock via OpenClientDirectory is
  // identified.
  QM_TRY(
      // Expression.
      MOZ_TO_RESULT(IsTemporaryOriginInitializedInternal(aOriginMetadata))
          .mapErr([](const nsresult rv) { return NS_ERROR_NOT_INITIALIZED; }),
      // Custom return value.
      QM_PROPAGATE,
      // Cleanup function.
      ([this, aOriginMetadata](const nsresult) {
        MOZ_ALWAYS_SUCCEEDS(mOwningThread->Dispatch(
            NS_NewRunnableFunction(
                "QuotaManager::GetOrCreateTemporaryOriginDirectory",
                [aOriginMetadata]() {
                  QuotaManager* quotaManager = QuotaManager::Get();
                  MOZ_ASSERT(quotaManager);

                  OriginMetadataArray originMetadataArray;
                  originMetadataArray.AppendElement(aOriginMetadata);

                  quotaManager->NoteUninitializedClients(originMetadataArray);
                  quotaManager->NoteUninitializedOrigins(originMetadataArray);
                }),
            NS_DISPATCH_NORMAL));
      }));

  QM_TRY_UNWRAP(auto directory, GetOriginDirectory(aOriginMetadata));

  QM_TRY_INSPECT(const bool& created, EnsureOriginDirectory(*directory));

  if (created) {
    // A new origin directory has been created.

    // We have a temporary origin which has been initialized without ensuring
    // respective origin directory. So OriginInfo already exists and it needs
    // to be updated because the origin directory has been just created.

    auto [timestamp, maintenanceDate, accessed, persisted, dirty] =
        WithOriginInfo(aOriginMetadata, [](const auto& originInfo) {
          const int64_t timestamp = originInfo->LockedAccessTime();
          const int32_t maintenanceDate = originInfo->LockedMaintenanceDate();
          const bool accessed = originInfo->LockedAccessed();
          const bool persisted = originInfo->LockedPersisted();
          const bool dirty = originInfo->LockedDirty();

          originInfo->LockedDirectoryCreated();

          return std::make_tuple(timestamp, maintenanceDate, accessed,
                                 persisted, dirty);
        });

    FullOriginMetadata fullOriginMetadata{
        aOriginMetadata,
        OriginStateMetadata{timestamp, maintenanceDate, accessed, persisted,
                            dirty},
        ClientUsageArray(), /* aUsage */ 0, kCurrentQuotaVersion};

    // Usually, infallible operations are placed after fallible ones. However,
    // since we lack atomic support for creating the origin directory along
    // with its metadata, we need to add the origin to cached origins right
    // after directory creation.
    AddTemporaryOrigin(fullOriginMetadata);

    QM_TRY(MOZ_TO_RESULT(
        CreateDirectoryMetadata2(*directory, fullOriginMetadata)));
  }

  return std::move(directory);
}

Result<Ok, nsresult> QuotaManager::EnsureTemporaryOriginDirectoryCreated(
    const OriginMetadata& aOriginMetadata) {
  QM_TRY_RETURN(GetOrCreateTemporaryOriginDirectory(aOriginMetadata)
                    .map([](const auto& res) { return Ok{}; }));
}

nsresult QuotaManager::CreateDirectoryMetadata2(
    nsIFile& aDirectory, FullOriginMetadata& aFullOriginMetadata) {
  GECKO_TRACE_SCOPE("dom::quota", "QuotaManager::CreateDirectoryMetadata2");

  AssertIsOnIOThread();

  QM_TRY(ArtificialFailure(
      nsIQuotaArtificialFailure::CATEGORY_CREATE_DIRECTORY_METADATA2));

  QM_TRY_INSPECT(const auto& file, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                       nsCOMPtr<nsIFile>, aDirectory, Clone));

  QM_TRY(
      MOZ_TO_RESULT(file->Append(nsLiteralString(METADATA_V2_TMP_FILE_NAME))));

  QM_TRY_INSPECT(const auto& stream,
                 GetBinaryOutputStream(*file, FileFlag::Truncate));
  MOZ_ASSERT(stream);

  QM_TRY(MOZ_TO_RESULT(
      WriteDirectoryMetadataHeader(*stream, aFullOriginMetadata)));

  // Legacy field, previously used for suffix. The value is no longer used, but
  // we continue writing the correct suffix value to preserve compatibility
  // with older builds that may still expect it.
  QM_TRY(
      MOZ_TO_RESULT(stream->WriteStringZ(aFullOriginMetadata.mSuffix.get())));

  // Legacy field, previously used for group. The value is no longer used, but
  // we continue writing the correct group value to preserve compatibility with
  // older builds that may still expect it.
  QM_TRY(MOZ_TO_RESULT(stream->WriteStringZ(aFullOriginMetadata.mGroup.get())));

  QM_TRY(MOZ_TO_RESULT(
      stream->WriteStringZ(aFullOriginMetadata.mStorageOrigin.get())));

  // Legacy field, previously used for isPrivate (and before that, for isApp).
  // The value is no longer used, but we continue writing the correct isPrivate
  // value (true or false) to preserve compatibility with older builds that may
  // still expect it.
  QM_TRY(MOZ_TO_RESULT(stream->WriteBoolean(aFullOriginMetadata.mIsPrivate)));

  QM_TRY(MOZ_TO_RESULT(stream->Write32(kMetadataSentinel)));

  QM_TRY(MOZ_TO_RESULT(stream->Write32(aFullOriginMetadata.mQuotaVersion)));

  QM_TRY(MOZ_TO_RESULT(stream->Write64(aFullOriginMetadata.mOriginUsage)));

  nsCString clientUsagesText;
  aFullOriginMetadata.mClientUsages.Serialize(clientUsagesText);

  QM_TRY(MOZ_TO_RESULT(stream->WriteStringZ(clientUsagesText.get())));

  QM_TRY(MOZ_TO_RESULT(stream->Flush()));

  QM_TRY(MOZ_TO_RESULT(stream->Close()));

  QM_TRY(MOZ_TO_RESULT(
      file->RenameTo(nullptr, nsLiteralString(METADATA_V2_FILE_NAME))));

  // Upsert is the last step - if it fails, SettleDirectoryMetadata2
  // sets origin back dirty.
  // .metadata-v2 data is only relied on when storage database is unusable:
  // all the origins with accessed flag will be scanned regardless of the
  // dirty flag state.
  if (!aFullOriginMetadata.mIsPrivate) {
    MOZ_ASSERT(mOriginUpserter, "We must have an origin upserter here");
    QM_TRY(mOriginUpserter->Refresh(aFullOriginMetadata));
  }

  return NS_OK;
}

nsresult QuotaManager::SettleDirectoryMetadata2(
    nsIFile& aDirectory, FullOriginMetadata& aFullOriginMetadata) {
  AssertIsOnIOThread();

  aFullOriginMetadata.mDirty = false;
  auto resetBackToDirty = MakeScopeExit(
      [&aFullOriginMetadata]() { aFullOriginMetadata.mDirty = true; });

  QM_TRY(
      MOZ_TO_RESULT(CreateDirectoryMetadata2(aDirectory, aFullOriginMetadata)));

  resetBackToDirty.release();

  return NS_OK;
}

nsresult QuotaManager::RestoreDirectoryMetadata2(nsIFile* aDirectory) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);
  MOZ_ASSERT(mStorageConnection);

  glean::quotamanager::restore_origin_directory_metadata_counter.Add();

  RefPtr<RestoreDirectoryMetadata2Helper> helper =
      new RestoreDirectoryMetadata2Helper(aDirectory);

  QM_TRY(MOZ_TO_RESULT(helper->Init()));

  QM_TRY(MOZ_TO_RESULT(helper->RestoreMetadata2File()));

  return NS_OK;
}

Result<FullOriginMetadata, nsresult> ReadFullOriginMetadataFromMetadataV2File(
    nsIFile* aDirectory) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aDirectory);

  QM_TRY_INSPECT(const auto& binaryStream,
                 GetBinaryInputStream(*aDirectory,
                                      nsLiteralString(METADATA_V2_FILE_NAME)));

  FullOriginMetadata fullOriginMetadata;

  QM_TRY(ReadDirectoryMetadataHeader(*binaryStream, fullOriginMetadata));

  // Legacy field, previously used for suffix. This value is no longer used,
  // but still read and discarded to preserve compatibility with older builds
  // that may still expect it.
  QM_TRY_INSPECT(
      const auto& unusedData1,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, binaryStream, ReadCString));
  (void)unusedData1;

  // Legacy field, previously used for group. This value is no longer used, but
  // still read and discarded to preserve compatibility with older builds that
  // may still expect it.
  QM_TRY_INSPECT(
      const auto& unusedData2,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, binaryStream, ReadCString));
  (void)unusedData2;

  QM_TRY_UNWRAP(
      fullOriginMetadata.mStorageOrigin,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, binaryStream, ReadCString));

  const auto extraInfo =
      ScopedLogExtraInfo{ScopedLogExtraInfo::kTagStorageOriginTainted,
                         fullOriginMetadata.mStorageOrigin};

  // Legacy field, previously used for isPrivate (and before that, for isApp).
  // This value is no longer used, but still read and discarded to preserve
  // compatibility with older builds that may still expect it.
  QM_TRY_INSPECT(const bool& unusedData3,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, ReadBoolean));
  (void)unusedData3;

  QM_VERBOSEONLY_TRY_UNWRAP(const auto sentinel,
                            MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read32));

  if (sentinel) {
    if (sentinel.ref() != kMetadataSentinel) {
      QM_TRY(MOZ_TO_RESULT(false));
    }

    QM_TRY_UNWRAP(fullOriginMetadata.mQuotaVersion,
                  MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read32));
    if (fullOriginMetadata.mAccessed &&
        fullOriginMetadata.mQuotaVersion != kCurrentQuotaVersion) {
      // The dirty flag is not stored in older .metadata-v2 files.
      // Conservatively assume dirty=true (requires full scan) when the flag is
      // missing. InitializeOrigin upgrades the version.
      fullOriginMetadata.mDirty = true;
    }

    QM_TRY_UNWRAP(fullOriginMetadata.mOriginUsage,
                  MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read64));

    QM_TRY_INSPECT(const auto& clientUsagesText,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, binaryStream,
                                                     ReadCString));

    ClientUsageArray clientUsages;
    QM_TRY(MOZ_TO_RESULT(clientUsages.Deserialize(clientUsagesText)));

    fullOriginMetadata.mClientUsages = std::move(clientUsages);
  } else {
    fullOriginMetadata.mQuotaVersion = kNoQuotaVersion;
    fullOriginMetadata.mOriginUsage = 0;
    fullOriginMetadata.mClientUsages = ClientUsageArray();
  }

  QM_TRY(MOZ_TO_RESULT(binaryStream->Close()));

  return fullOriginMetadata;
}

Result<Ok, nsresult> UpdateFullOriginMetadataPrincipalProperties(
    QuotaManager& aQuotaManager, FullOriginMetadata& aFullOriginMetadata) {
  auto principal =
      [&storageOrigin =
           aFullOriginMetadata.mStorageOrigin]() -> nsCOMPtr<nsIPrincipal> {
    if (storageOrigin.EqualsLiteral(kChromeOrigin)) {
      return SystemPrincipal::Get();
    }
    return BasePrincipal::CreateContentPrincipal(storageOrigin);
  }();
  QM_TRY(MOZ_TO_RESULT(principal));

  PrincipalInfo principalInfo;
  QM_TRY(MOZ_TO_RESULT(PrincipalToPrincipalInfo(principal, &principalInfo)));

  QM_TRY(MOZ_TO_RESULT(IsPrincipalInfoValid(principalInfo)),
         Err(NS_ERROR_MALFORMED_URI));

  QM_TRY_UNWRAP(auto principalMetadata, GetInfoFromValidatedPrincipalInfo(
                                            aQuotaManager, principalInfo));

  aFullOriginMetadata.mSuffix = std::move(principalMetadata.mSuffix);
  aFullOriginMetadata.mGroup = std::move(principalMetadata.mGroup);
  aFullOriginMetadata.mOrigin = std::move(principalMetadata.mOrigin);
  aFullOriginMetadata.mIsPrivate = principalMetadata.mIsPrivate;

  return Ok{};
}

namespace {

Result<bool, nsresult> DidLatestShutdownFail(
    mozIStorageConnection& aConnection) {
  // The `cache.valid` bit is the source of truth for "did the previous
  // session reach UnloadQuota's commit?" — InvalidateCache writes 0 and
  // UnloadQuota's final UPDATE writes 1 atomically. A row-by-row
  // DB-vs-disk scan is unnecessary here.
  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement<
                     SingleStepResult::ReturnNullIfNoResult>(
                     aConnection, "SELECT valid FROM cache"_ns));

  QM_TRY(OkIf(stmt), Err(NS_ERROR_FILE_CORRUPTED));

  QM_TRY_INSPECT(const int32_t& valid,
                 MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 0));

  return !valid;
}

// Read all rows of the `origin` table for a single repository into an
// `OriginCacheMap`. The map is keyed by (persistence_type, origin) and
// lets the repository-scan path tell, per origin, whether the cached
// row already matches the metadata we'd otherwise rewrite.
//
// This is read-only: no transactional state change. We deliberately
// build a parallel data structure rather than reusing `LoadQuotaFromCache`
// because the cache path also pushes `OriginInfo`s into `mGroupInfoPairs`
// and we need the raw row contents alone here.
Result<OriginCacheMap, nsresult> LoadOriginCacheIntoMap(
    mozIStorageConnection& aConnection, PersistenceType aPersistenceType) {
  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
          "SELECT repository_id, suffix, group_, "
          "origin, client_usages, usage, "
          "last_access_time, last_maintenance_date, metadata_flags "
          "FROM origin "
          "WHERE repository_id = :repository_id"_ns));

  QM_TRY(MOZ_TO_RESULT(
      stmt->BindInt32ByName("repository_id"_ns, aPersistenceType)));

  OriginCacheMap map;
  map.Activate();

  QM_TRY(quota::CollectWhileHasResult(
      *stmt, [&map](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const int32_t& repositoryId,
                       MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 0));

        const auto maybePersistenceType =
            PersistenceTypeFromInt32(repositoryId, fallible);
        QM_TRY(OkIf(maybePersistenceType.isSome()), Err(NS_ERROR_FAILURE));

        FullOriginMetadata fullOriginMetadata;
        fullOriginMetadata.mPersistenceType = maybePersistenceType.value();

        QM_TRY_UNWRAP(fullOriginMetadata.mSuffix,
                      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                        GetUTF8String, 1));
        QM_TRY_UNWRAP(fullOriginMetadata.mGroup,
                      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                        GetUTF8String, 2));
        QM_TRY_UNWRAP(fullOriginMetadata.mOrigin,
                      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                        GetUTF8String, 3));
        fullOriginMetadata.mStorageOrigin = fullOriginMetadata.mOrigin;
        fullOriginMetadata.mIsPrivate = false;

        QM_TRY_INSPECT(const auto& clientUsagesText,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                         GetUTF8String, 4));
        ClientUsageArray clientUsages;
        QM_TRY(MOZ_TO_RESULT(clientUsages.Deserialize(clientUsagesText)));
        fullOriginMetadata.mClientUsages = clientUsages;

        QM_TRY_INSPECT(fullOriginMetadata.mOriginUsage,
                       MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 5));

        // mQuotaVersion is not cached in the DB; mirror LoadQuotaFromCache.
        fullOriginMetadata.mQuotaVersion = kNoQuotaVersion;

        QM_TRY_UNWRAP(fullOriginMetadata.mLastAccessTime,
                      MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 6));
        QM_TRY_UNWRAP(fullOriginMetadata.mLastMaintenanceDate,
                      MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 7));
        QM_TRY_INSPECT(const int32_t& metadataFlags,
                       MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 8));
        fullOriginMetadata.FromMetadataFlags(
            static_cast<uint32_t>(metadataFlags));

        map.InsertOrUpdate(fullOriginMetadata.mPersistenceType,
                           fullOriginMetadata.mOrigin,
                           std::move(fullOriginMetadata));

        return Ok{};
      }));

  return map;
}

// Delete every row in the `origin` table whose key remains in `aMap`
// (origins present in the L1 cache but no longer found on disk).
// Wraps the per-row DELETE in a single immediate transaction.
Result<Ok, nsresult> DeleteStaleOriginRows(mozIStorageConnection& aConnection,
                                           const OriginCacheMap& aMap) {
  if (aMap.IsEmpty()) {
    return Ok{};
  }

  mozStorageTransaction transaction(
      &aConnection, false, mozIStorageConnection::TRANSACTION_IMMEDIATE);
  QM_TRY(MOZ_TO_RESULT(transaction.Start()));

  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
          "DELETE FROM origin "
          "WHERE repository_id = :repository_id AND origin = :origin"_ns));

  QM_TRY(aMap.ForEachEntry(
      [&stmt](PersistenceType aPersistenceType,
              const nsCString& aOrigin) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt->Reset()));
        QM_TRY(MOZ_TO_RESULT(
            stmt->BindInt32ByName("repository_id"_ns, aPersistenceType)));
        QM_TRY(MOZ_TO_RESULT(stmt->BindUTF8StringByName("origin"_ns, aOrigin)));
        QM_TRY(MOZ_TO_RESULT(stmt->Execute()));
        return Ok{};
      }));

  QM_TRY(MOZ_TO_RESULT(transaction.Commit()));

  return Ok{};
}

}  // namespace

Result<FullOriginMetadata, nsresult> QuotaManager::LoadFullOriginMetadata(
    nsIFile* aDirectory, PersistenceType aPersistenceType) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aDirectory);
  MOZ_ASSERT(mStorageConnection);

  QM_TRY_UNWRAP(FullOriginMetadata fullOriginMetadata,
                ReadFullOriginMetadataFromMetadataV2File(aDirectory));

  fullOriginMetadata.mPersistenceType = aPersistenceType;

  const auto extraInfo =
      ScopedLogExtraInfo{ScopedLogExtraInfo::kTagStorageOriginTainted,
                         fullOriginMetadata.mStorageOrigin};

  QM_TRY(
      UpdateFullOriginMetadataPrincipalProperties(*this, fullOriginMetadata));

  QM_TRY_INSPECT(const bool& groupUpdated,
                 MaybeUpdateGroupForOrigin(fullOriginMetadata));

  // A workaround for a bug in GetLastModifiedTime implementation which should
  // have returned the current time instead of INT64_MIN when there were no
  // suitable files for getting last modified time.
  QM_TRY_INSPECT(const bool& lastAccessTimeUpdated,
                 MaybeUpdateLastAccessTimeForOrigin(fullOriginMetadata));

  if (groupUpdated || lastAccessTimeUpdated) {
    QM_TRY(MOZ_TO_RESULT(
        SettleDirectoryMetadata2(*aDirectory, fullOriginMetadata)));
    MOZ_ASSERT(!fullOriginMetadata.mDirty);
  }

  return fullOriginMetadata;
}

Result<FullOriginMetadata, nsresult>
QuotaManager::LoadFullOriginMetadataWithRestore(nsIFile* aDirectory) {
  QM_TRY_INSPECT(const auto& result,
                 LoadFullOriginMetadataWithRestoreAndStatus(aDirectory));

  // The first element of the pair is the FullOriginMetadata, the second is the
  // restore status.
  return result.first;
}

Result<std::pair<FullOriginMetadata, bool /* restore status */>, nsresult>
QuotaManager::LoadFullOriginMetadataWithRestoreAndStatus(nsIFile* aDirectory) {
  // XXX Once the persistence type is stored in the metadata file, this block
  // for getting the persistence type from the parent directory name can be
  // removed.
  nsCOMPtr<nsIFile> parentDir;
  QM_TRY(MOZ_TO_RESULT(aDirectory->GetParent(getter_AddRefs(parentDir))));

  const auto maybePersistenceType =
      PersistenceTypeFromFile(*parentDir, fallible);
  QM_TRY(OkIf(maybePersistenceType.isSome()), Err(NS_ERROR_FAILURE));

  const auto& persistenceType = maybePersistenceType.value();

  bool restored = false;

  QM_TRY_INSPECT(
      const auto& fullOriginMetadata,
      QM_OR_ELSE_WARN(
          // Expression.
          LoadFullOriginMetadata(aDirectory, persistenceType),
          // Fallback.
          ([&aDirectory, &persistenceType, &restored,
            this](const nsresult rv) -> Result<FullOriginMetadata, nsresult> {
            restored = true;
            QM_TRY(MOZ_TO_RESULT(RestoreDirectoryMetadata2(aDirectory)));

            QM_TRY_RETURN(LoadFullOriginMetadata(aDirectory, persistenceType));
          })));

  return std::make_pair(fullOriginMetadata, restored);
}

Result<OriginMetadata, nsresult> QuotaManager::GetOriginMetadata(
    nsIFile* aDirectory) {
  MOZ_ASSERT(aDirectory);
  MOZ_ASSERT(mStorageConnection);

  QM_TRY_INSPECT(
      const auto& leafName,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, aDirectory, GetLeafName));

  // XXX Consider using QuotaManager::ParseOrigin here.
  nsCString spec;
  OriginAttributes attrs;
  nsCString originalSuffix;
  OriginParser::ResultType result = OriginParser::ParseOrigin(
      NS_ConvertUTF16toUTF8(leafName), spec, &attrs, originalSuffix);
  QM_TRY(MOZ_TO_RESULT(result == OriginParser::ValidOrigin));

  QM_TRY_INSPECT(
      const auto& principal,
      ([&spec, &attrs]() -> Result<nsCOMPtr<nsIPrincipal>, nsresult> {
        if (spec.EqualsLiteral(kChromeOrigin)) {
          return nsCOMPtr<nsIPrincipal>(SystemPrincipal::Get());
        }

        nsCOMPtr<nsIURI> uri;
        QM_TRY(MOZ_TO_RESULT(NS_NewURI(getter_AddRefs(uri), spec)));

        return nsCOMPtr<nsIPrincipal>(
            BasePrincipal::CreateContentPrincipal(uri, attrs));
      }()));
  QM_TRY(MOZ_TO_RESULT(principal));

  PrincipalInfo principalInfo;
  QM_TRY(MOZ_TO_RESULT(PrincipalToPrincipalInfo(principal, &principalInfo)));

  QM_TRY(MOZ_TO_RESULT(IsPrincipalInfoValid(principalInfo)),
         Err(NS_ERROR_MALFORMED_URI));

  QM_TRY_UNWRAP(auto principalMetadata,
                GetInfoFromValidatedPrincipalInfo(*this, principalInfo));

  QM_TRY_INSPECT(const auto& parentDirectory,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCOMPtr<nsIFile>,
                                                   aDirectory, GetParent));

  const auto maybePersistenceType =
      PersistenceTypeFromFile(*parentDirectory, fallible);
  QM_TRY(MOZ_TO_RESULT(maybePersistenceType.isSome()));

  return OriginMetadata{std::move(principalMetadata),
                        maybePersistenceType.value()};
}

Result<Ok, nsresult> QuotaManager::RemoveOriginDirectory(nsIFile& aDirectory) {
  AssertIsOnIOThread();

  if (!AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownTeardown)) {
    QM_TRY_RETURN(MOZ_TO_RESULT(aDirectory.Remove(true)));
  }

  QM_TRY_INSPECT(const auto& toBeRemovedStorageDir,
                 QM_NewLocalFile(*mToBeRemovedStoragePath));

  QM_TRY_INSPECT(const bool& created, EnsureDirectory(*toBeRemovedStorageDir));

  (void)created;

  QM_TRY_RETURN(MOZ_TO_RESULT(aDirectory.MoveTo(
      toBeRemovedStorageDir, NSID_TrimBracketsUTF16(nsID::GenerateUUID()))));
}

Result<bool, nsresult> QuotaManager::DoesClientDirectoryExist(
    const ClientMetadata& aClientMetadata) const {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& directory, GetOriginDirectory(aClientMetadata));

  QM_TRY(MOZ_TO_RESULT(
      directory->Append(Client::TypeToString(aClientMetadata.mClientType))));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(directory, Exists));
}

struct RenameAndInitInfo {
  nsCOMPtr<nsIFile> mOriginDirectory;
  FullOriginMetadata mFullOriginMetadata;
};

template <typename OriginFunc>
Result<Ok, nsresult> QuotaManager::InitializeOriginDirectory(
    const nsCOMPtr<nsIFile>& aChildDirectory, const nsAutoString& aLeafName,
    PersistenceType aPersistenceType,
    nsTArray<RenameAndInitInfo>& aRenameAndInitInfos, OriginFunc&& aOriginFunc,
    OriginCacheMap& aCacheMap) {
  QM_TRY_UNWRAP(
      auto maybeMetadata,
      QM_OR_ELSE_WARN_IF(
          // Expression
          LoadFullOriginMetadataWithRestore(aChildDirectory)
              .map([](FullOriginMetadata&& metadata)
                       -> Maybe<FullOriginMetadata> {
                return Some(std::move(metadata));
              }),
          // Predicate.
          IsSpecificError<NS_ERROR_MALFORMED_URI>,
          // Fallback. The origin is permanently unresolvable
          // (e.g. an orphaned moz-extension directory whose
          // extension has been uninstalled), so remove it.
          ([&aChildDirectory](
               const nsresult) -> Result<Maybe<FullOriginMetadata>, nsresult> {
            QM_TRY(MOZ_TO_RESULT(aChildDirectory->Remove(true)));
            return Maybe<FullOriginMetadata>{};
          })));

  if (!maybeMetadata) {
    return Ok{};
  }

  auto metadata = maybeMetadata.extract();

  MOZ_ASSERT(metadata.mPersistenceType == aPersistenceType);

  const auto extraInfo = ScopedLogExtraInfo{
      ScopedLogExtraInfo::kTagStorageOriginTainted, metadata.mStorageOrigin};

  // FIXME(tt): The check for origin name consistency can
  // be removed once we have an upgrade to traverse origin
  // directories and check through the directory metadata
  // files.
  const auto originSanitized = MakeSanitizedOriginCString(metadata.mOrigin);

  NS_ConvertUTF16toUTF8 utf8LeafName(aLeafName);
  if (!originSanitized.Equals(utf8LeafName)) {
    QM_WARNING(
        "The name of the origin directory (%s) doesn't "
        "match the sanitized origin string (%s) in the "
        "metadata file!",
        utf8LeafName.get(), originSanitized.get());

    // If it's the known case, we try to restore the
    // origin directory name if it's possible.
    if (originSanitized.Equals(utf8LeafName + "."_ns)) {
      aRenameAndInitInfos.AppendElement(
          RenameAndInitInfo{std::move(aChildDirectory), std::move(metadata)});
      return Ok{};
    }

    // XXXtt: Try to restore the unknown cases base on the
    // content for their metadata files. Note that if the
    // restore fails, QM should maintain a list and ensure
    // they won't be accessed after initialization.
  }

  if (aPersistenceType != PERSISTENCE_TYPE_PERSISTENT) {
    std::forward<OriginFunc>(aOriginFunc)(metadata);

    AddTemporaryOrigin(metadata);
    QM_LOG(("Temporary origin added: %s, accessed: %d", metadata.mOrigin.get(),
            metadata.mAccessed));

    if (StaticPrefs::dom_quotaManager_loadQuotaFromSecondaryCache() &&
        IsInitializableQuotaVersion(metadata.mQuotaVersion) &&
        !metadata.mAccessed
#if defined(NIGHTLY_BUILD) || defined(DEBUG)
        && metadata.CheckIfUsageIsConsistent("InitializeOriginDirectory"_ns)
#endif
    ) {
      QM_LOG(("Initializing quota for: %s", metadata.mOrigin.get()));
      InitQuotaForOrigin(metadata, /* aDirectoryExists */ true, aCacheMap);

      return Ok{};
    }
  }
  QM_LOG(("Using secondary cache for: %s", metadata.mOrigin.get()));

  QM_TRY(QM_OR_ELSE_WARN_IF(
      // Expression.
      MOZ_TO_RESULT(InitializeOrigin(aChildDirectory, metadata,
                                     /* aForGroup */ false, aCacheMap)),
      // Predicate.
      IsDatabaseCorruptionError,
      // Fallback.
      ([&aChildDirectory, &metadata,
        this](const nsresult rv) -> Result<Ok, nsresult> {
        // If the origin can't be initialized due to
        // corruption, this is a permanent
        // condition, and we need to remove all data
        // for the origin on disk.

        QM_TRY(MOZ_TO_RESULT(aChildDirectory->Remove(true)));

        RemoveTemporaryOrigin(metadata);

        return Ok{};
      })));

  return Ok{};
}

template <typename OriginFunc>
Result<Ok, nsresult> QuotaManager::ResolveRepositoryEntry(
    const nsCOMPtr<nsIFile>& aChildDirectory, PersistenceType aPersistenceType,
    nsTArray<RenameAndInitInfo>& aRenameAndInitInfos, OriginFunc&& aOriginFunc,
    OriginCacheMap& aCacheMap) {
  QM_TRY_INSPECT(const auto& leafName,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsAutoString, aChildDirectory, GetLeafName));

  QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*aChildDirectory));

  switch (dirEntryKind) {
    case nsIFileKind::ExistsAsDirectory: {
      QM_TRY(InitializeOriginDirectory(
          aChildDirectory, leafName, aPersistenceType, aRenameAndInitInfos,
          std::forward<OriginFunc>(aOriginFunc), aCacheMap));
      break;
    }
    case nsIFileKind::ExistsAsFile:
      if (IsOSMetadata(leafName) || IsDotFile(leafName)) {
        break;
      }

      // Unknown files during initialization are now allowed.
      // Just warn if we find them.
      UNKNOWN_FILE_WARNING(leafName);
      break;

    case nsIFileKind::DoesNotExist:
      // Ignore files that got removed externally while
      // iterating.
      break;
  }

  return Ok{};
}

template <typename OriginFunc>
nsresult QuotaManager::InitializeRepository(PersistenceType aPersistenceType,
                                            OriginFunc&& aOriginFunc) {
  GECKO_TRACE_SCOPE("dom::quota", "QuotaManager::InitializeRepository");

  AssertIsOnIOThread();
  MOZ_ASSERT(aPersistenceType == PERSISTENCE_TYPE_PERSISTENT ||
             aPersistenceType == PERSISTENCE_TYPE_TEMPORARY ||
             aPersistenceType == PERSISTENCE_TYPE_DEFAULT);

  // If we are shutting down, it's too late to initialize the repository.
  // Stop now: any rescan needed will be done on next startup.
  QM_TRY(OkIf(!IsShuttingDown()), NS_ERROR_ABORT);

  // Pre-load the L1 cache rows for this repository so the disk walk
  // can decide per-origin whether the cached row already matches the
  // metadata we'd otherwise rewrite. Keys claimed during the walk are
  // removed from the map; the leftovers are stale rows for origins no
  // longer on disk and are deleted post-walk.
  QM_TRY_UNWRAP(
      auto cacheMap,
      ([this, aPersistenceType]() -> Result<OriginCacheMap, nsresult> {
        if (mCacheUsable) {
          return LoadOriginCacheIntoMap(*mStorageConnection, aPersistenceType);
        }
        return OriginCacheMap{};
      }()));

  QM_TRY_INSPECT(const auto& directory,
                 QM_NewLocalFile(GetStoragePath(aPersistenceType)));

  QM_TRY_INSPECT(const bool& created, EnsureDirectory(*directory));

  (void)created;

  uint64_t iterations = 0;

  // A keeper to defer the return only in Nightly, so that the telemetry data
  // for whole profile can be collected
#ifdef NIGHTLY_BUILD
  nsresult statusKeeper = NS_OK;
#endif

  const auto statusKeeperFunc = [&](const nsresult rv) {
    RECORD_IN_NIGHTLY(statusKeeper, rv);
  };

  nsTArray<RenameAndInitInfo> renameAndInitInfos;

  QM_TRY(([&]() -> Result<Ok, nsresult> {
    QM_TRY(
        CollectEachFile(
            *directory,
            [&](nsCOMPtr<nsIFile>&& aChildDirectory) -> Result<Ok, nsresult> {
              if (NS_WARN_IF(IsShuttingDown())) {
                RETURN_STATUS_OR_RESULT(statusKeeper, NS_ERROR_ABORT);
              }

              nsCOMPtr<nsIFile> childDirectory = std::move(aChildDirectory);

              QM_TRY(([this, &iterations, &childDirectory, &renameAndInitInfos,
                       aPersistenceType, &aOriginFunc,
                       &cacheMap]() -> Result<Ok, nsresult> {
                       QM_TRY(ResolveRepositoryEntry(
                           childDirectory, aPersistenceType, renameAndInitInfos,
                           std::forward<OriginFunc>(aOriginFunc), cacheMap));

                       iterations++;

                       return Ok{};
                     }()),
                     OK_IN_NIGHTLY_PROPAGATE_IN_OTHERS, statusKeeperFunc);

              return Ok{};
            }),
        OK_IN_NIGHTLY_PROPAGATE_IN_OTHERS, statusKeeperFunc);

    return Ok{};
  }()));

  for (auto& info : renameAndInitInfos) {
    QM_TRY(([&]() -> Result<Ok, nsresult> {
      if (NS_WARN_IF(IsShuttingDown())) {
        RETURN_STATUS_OR_RESULT(statusKeeper, NS_ERROR_ABORT);
      }

      QM_TRY(
          ([&directory, &info, this, aPersistenceType, &aOriginFunc,
            &cacheMap]() -> Result<Ok, nsresult> {
            const auto& metadata = info.mFullOriginMetadata;

            const auto extraInfo =
                ScopedLogExtraInfo{ScopedLogExtraInfo::kTagStorageOriginTainted,
                                   metadata.mStorageOrigin};

            const auto originDirName =
                MakeSanitizedOriginString(metadata.mOrigin);

            // Check if targetDirectory exist.
            QM_TRY_INSPECT(const auto& targetDirectory,
                           CloneFileAndAppend(*directory, originDirName));

            QM_TRY_INSPECT(const bool& exists, MOZ_TO_RESULT_INVOKE_MEMBER(
                                                   targetDirectory, Exists));

            if (exists) {
              QM_TRY(MOZ_TO_RESULT(info.mOriginDirectory->Remove(true)));

              return Ok{};
            }

            QM_TRY(MOZ_TO_RESULT(
                info.mOriginDirectory->RenameTo(nullptr, originDirName)));

            if (aPersistenceType != PERSISTENCE_TYPE_PERSISTENT) {
              std::forward<OriginFunc>(aOriginFunc)(metadata);

              AddTemporaryOrigin(metadata);

              if (StaticPrefs::dom_quotaManager_loadQuotaFromSecondaryCache() &&
                  IsInitializableQuotaVersion(metadata.mQuotaVersion) &&
                  !metadata.mAccessed) {
                InitQuotaForOrigin(metadata, /* aDirectoryExists */ true,
                                   cacheMap);

                return Ok{};
              }
            }

            // XXX We don't check corruption here ?
            QM_TRY(MOZ_TO_RESULT(InitializeOrigin(targetDirectory, metadata,
                                                  /* aForGroup */ false,
                                                  cacheMap)));

            return Ok{};
          }()),
          OK_IN_NIGHTLY_PROPAGATE_IN_OTHERS, statusKeeperFunc);

      return Ok{};
    }()));
  }

#ifdef NIGHTLY_BUILD
  if (NS_FAILED(statusKeeper)) {
    return statusKeeper;
  }
#endif

  // Anything still in the map after the disk walk is a row that exists
  // in the L1 cache but not on disk. Delete those stale rows.
  if (cacheMap.IsActive()) {
    QM_TRY(DeleteStaleOriginRows(*mStorageConnection, cacheMap));
  }

  glean::quotamanager_initialize_repository::number_of_iterations
      .Get(PersistenceTypeToString(aPersistenceType))
      .AccumulateSingleSample(iterations);

  return NS_OK;
}

nsresult QuotaManager::InitializeOrigin(
    nsIFile* aDirectory, const FullOriginMetadata& aFullOriginMetadata,
    bool aForGroup) {
  return InitializeOrigin(aDirectory, aFullOriginMetadata, aForGroup,
                          OriginCacheMap::Inactive());
}

nsresult QuotaManager::InitializeOrigin(
    nsIFile* aDirectory, const FullOriginMetadata& aFullOriginMetadata,
    bool aForGroup, OriginCacheMap& aCacheMap) {
  GECKO_TRACE_SCOPE("dom::quota", "QuotaManager::InitializeOrigin");

  QM_LOG(("Starting origin initialization for: %s",
          aFullOriginMetadata.mOrigin.get()));

  AssertIsOnIOThread();

  QM_TRY(
      ArtificialFailure(nsIQuotaArtificialFailure::CATEGORY_INITIALIZE_ORIGIN));

  // The ScopedLogExtraInfo is not set here on purpose, so the callers can
  // decide if they want to set it. The extra info can be set sooner this way
  // as well.

  const bool trackQuota =
      aFullOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT;

  if (trackQuota && !aForGroup &&
      QuotaPrefs::LazyOriginInitializationEnabled()) {
    QM_LOG(("Skipping origin initialization for: %s (it will be done lazily)",
            aFullOriginMetadata.mOrigin.get()));

    return NS_OK;
  }

  NotifyOriginInitializationStarted(*this);

  // We need to initialize directories of all clients if they exists and also
  // get the total usage to initialize the quota.

  ClientUsageArray clientUsages;

  // A keeper to defer the return only in Nightly, so that the telemetry data
  // for whole profile can be collected
#ifdef NIGHTLY_BUILD
  nsresult statusKeeper = NS_OK;
#endif

  QM_TRY(([&, statusKeeperFunc = [&](const nsresult rv) {
            RECORD_IN_NIGHTLY(statusKeeper, rv);
          }]() -> Result<Ok, nsresult> {
    QM_TRY(
        CollectEachFile(
            *aDirectory,
            [&](const nsCOMPtr<nsIFile>& file) -> Result<Ok, nsresult> {
              if (NS_WARN_IF(IsShuttingDown())) {
                RETURN_STATUS_OR_RESULT(statusKeeper, NS_ERROR_ABORT);
              }

              QM_TRY(
                  ([this, &file, trackQuota, &aFullOriginMetadata,
                    &clientUsages]() -> Result<Ok, nsresult> {
                    QM_TRY_INSPECT(const auto& leafName,
                                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                       nsAutoString, file, GetLeafName));

                    QM_TRY_INSPECT(const auto& dirEntryKind,
                                   GetDirEntryKind(*file));

                    switch (dirEntryKind) {
                      case nsIFileKind::ExistsAsDirectory: {
                        Client::Type clientType;
                        const bool ok = Client::TypeFromText(
                            leafName, clientType, fallible);
                        if (!ok) {
                          // Unknown directories during initialization are now
                          // allowed. Just warn if we find them.
                          UNKNOWN_FILE_WARNING(leafName);
                          break;
                        }

                        if (trackQuota) {
                          QM_TRY_INSPECT(
                              const auto& usageInfo,
                              (*mClients)[clientType]->InitOrigin(
                                  aFullOriginMetadata.mPersistenceType,
                                  aFullOriginMetadata,
                                  /* aCanceled */ Atomic<bool>(false)));

                          MOZ_ASSERT(!clientUsages[clientType]);

                          if (usageInfo.TotalUsage()) {
                            // XXX(Bug 1683863) Until we identify the root cause
                            // of seemingly converted-from-negative usage
                            // values, we will just treat them as unset here,
                            // but log a warning to the browser console.
                            if (static_cast<int64_t>(*usageInfo.TotalUsage()) >=
                                0) {
                              clientUsages[clientType] = usageInfo.TotalUsage();
                            } else {
#if defined(NIGHTLY_BUILD) || defined(DEBUG)
                              const nsCOMPtr<nsIConsoleService> console =
                                  do_GetService(NS_CONSOLESERVICE_CONTRACTID);
                              if (console) {
                                console->LogStringMessage(
                                    nsString(
                                        u"QuotaManager warning: client "_ns +
                                        leafName +
                                        u" reported negative usage for group "_ns +
                                        NS_ConvertUTF8toUTF16(
                                            aFullOriginMetadata.mGroup) +
                                        u", origin "_ns +
                                        NS_ConvertUTF8toUTF16(
                                            aFullOriginMetadata.mOrigin))
                                        .get());
                              }
#endif
                            }
                          }
                        } else {
                          QM_TRY(MOZ_TO_RESULT(
                              (*mClients)[clientType]
                                  ->InitOriginWithoutTracking(
                                      aFullOriginMetadata.mPersistenceType,
                                      aFullOriginMetadata,
                                      /* aCanceled */ Atomic<bool>(false))));
                        }

                        break;
                      }

                      case nsIFileKind::ExistsAsFile:
                        if (IsOriginMetadata(leafName)) {
                          break;
                        }

                        if (IsTempMetadata(leafName)) {
                          QM_TRY(MOZ_TO_RESULT(
                              file->Remove(/* recursive */ false)));

                          break;
                        }

                        if (IsOSMetadata(leafName) || IsDotFile(leafName)) {
                          break;
                        }

                        // Unknown files during initialization are now allowed.
                        // Just warn if we find them.
                        UNKNOWN_FILE_WARNING(leafName);
                        // Bug 1595448 will handle the case for unknown files
                        // like idb, cache, or ls.
                        break;

                      case nsIFileKind::DoesNotExist:
                        // Ignore files that got removed externally while
                        // iterating.
                        break;
                    }

                    return Ok{};
                  }()),
                  OK_IN_NIGHTLY_PROPAGATE_IN_OTHERS, statusKeeperFunc);

              return Ok{};
            }),
        OK_IN_NIGHTLY_PROPAGATE_IN_OTHERS, statusKeeperFunc);

    return Ok{};
  }()));

#ifdef NIGHTLY_BUILD
  if (NS_FAILED(statusKeeper)) {
    return statusKeeper;
  }
#endif

  if (trackQuota) {
    const auto usage = std::accumulate(
        clientUsages.cbegin(), clientUsages.cend(), CheckedInt64(0),
        [](CheckedInt64 value, const Maybe<int64_t>& clientUsage) {
          QM_ASSERT_NOT_NEGATIVE(clientUsage.valueOr(0));
          return value + clientUsage.valueOr(0);
        });

    // XXX Should we log more information, i.e. the whole clientUsages array, in
    // case usage is not valid?

    QM_TRY(OkIf(usage.isValid()), NS_ERROR_FAILURE);

    FullOriginMetadata fullOriginMetadata = aFullOriginMetadata.Clone();

    fullOriginMetadata.mAccessed = false;

    fullOriginMetadata.mQuotaVersion = kCurrentQuotaVersion;
    fullOriginMetadata.mOriginUsage = usage.value();
    fullOriginMetadata.mClientUsages = clientUsages;

    // Mark the origin dirty so the flush timer writes updated metadata
    // instead of doing synchronous I/O for every origin during init.
    if (!fullOriginMetadata.Equals(aFullOriginMetadata)) {
      fullOriginMetadata.mDirty = true;
    }

    InitQuotaForOrigin(fullOriginMetadata, /* aDirectoryExists */ true,
                       aCacheMap);
  }

  SleepIfEnabled(
      StaticPrefs::dom_quotaManager_originInitialization_pauseOnIOThreadMs());

  QM_LOG(("Ending origin initialization for: %s",
          aFullOriginMetadata.mOrigin.get()));

  return NS_OK;
}

nsresult
QuotaManager::UpgradeFromIndexedDBDirectoryToPersistentStorageDirectory(
    nsIFile* aIndexedDBDir) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aIndexedDBDir);

  const auto innerFunc = [this, &aIndexedDBDir](const auto&) -> nsresult {
    bool isDirectory;
    QM_TRY(MOZ_TO_RESULT(aIndexedDBDir->IsDirectory(&isDirectory)));

    if (!isDirectory) {
      NS_WARNING("indexedDB entry is not a directory!");
      return NS_OK;
    }

    auto persistentStorageDirOrErr = QM_NewLocalFile(*mStoragePath);
    if (NS_WARN_IF(persistentStorageDirOrErr.isErr())) {
      return persistentStorageDirOrErr.unwrapErr();
    }

    nsCOMPtr<nsIFile> persistentStorageDir = persistentStorageDirOrErr.unwrap();

    QM_TRY(MOZ_TO_RESULT(persistentStorageDir->Append(
        nsLiteralString(PERSISTENT_DIRECTORY_NAME))));

    bool exists;
    QM_TRY(MOZ_TO_RESULT(persistentStorageDir->Exists(&exists)));

    if (exists) {
      QM_WARNING("Deleting old <profile>/indexedDB directory!");

      QM_TRY(MOZ_TO_RESULT(aIndexedDBDir->Remove(/* aRecursive */ true)));

      return NS_OK;
    }

    nsCOMPtr<nsIFile> storageDir;
    QM_TRY(MOZ_TO_RESULT(
        persistentStorageDir->GetParent(getter_AddRefs(storageDir))));

    // MoveTo() is atomic if the move happens on the same volume which should
    // be our case, so even if we crash in the middle of the operation nothing
    // breaks next time we try to initialize.
    // However there's a theoretical possibility that the indexedDB directory
    // is on different volume, but it should be rare enough that we don't have
    // to worry about it.
    QM_TRY(MOZ_TO_RESULT(aIndexedDBDir->MoveTo(
        storageDir, nsLiteralString(PERSISTENT_DIRECTORY_NAME))));

    return NS_OK;
  };

  return ExecuteInitialization(Initialization::UpgradeFromIndexedDBDirectory,
                               innerFunc);
}

nsresult
QuotaManager::UpgradeFromPersistentStorageDirectoryToDefaultStorageDirectory(
    nsIFile* aPersistentStorageDir) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aPersistentStorageDir);

  const auto innerFunc = [this,
                          &aPersistentStorageDir](const auto&) -> nsresult {
    QM_TRY_INSPECT(
        const bool& isDirectory,
        MOZ_TO_RESULT_INVOKE_MEMBER(aPersistentStorageDir, IsDirectory));

    if (!isDirectory) {
      NS_WARNING("persistent entry is not a directory!");
      return NS_OK;
    }

    {
      QM_TRY_INSPECT(const auto& defaultStorageDir,
                     QM_NewLocalFile(*mDefaultStoragePath));

      QM_TRY_INSPECT(const bool& exists,
                     MOZ_TO_RESULT_INVOKE_MEMBER(defaultStorageDir, Exists));

      if (exists) {
        QM_WARNING("Deleting old <profile>/storage/persistent directory!");

        QM_TRY(MOZ_TO_RESULT(
            aPersistentStorageDir->Remove(/* aRecursive */ true)));

        return NS_OK;
      }
    }

    {
      // Create real metadata files for origin directories in persistent
      // storage.
      auto helper = MakeRefPtr<CreateOrUpgradeDirectoryMetadataHelper>(
          aPersistentStorageDir);

      QM_TRY(MOZ_TO_RESULT(helper->Init()));

      QM_TRY(MOZ_TO_RESULT(helper->ProcessRepository()));

      // Upgrade metadata files for origin directories in temporary storage.
      QM_TRY_INSPECT(const auto& temporaryStorageDir,
                     QM_NewLocalFile(*mTemporaryStoragePath));

      QM_TRY_INSPECT(const bool& exists,
                     MOZ_TO_RESULT_INVOKE_MEMBER(temporaryStorageDir, Exists));

      if (exists) {
        QM_TRY_INSPECT(
            const bool& isDirectory,
            MOZ_TO_RESULT_INVOKE_MEMBER(temporaryStorageDir, IsDirectory));

        if (!isDirectory) {
          NS_WARNING("temporary entry is not a directory!");
          return NS_OK;
        }

        helper = MakeRefPtr<CreateOrUpgradeDirectoryMetadataHelper>(
            temporaryStorageDir);

        QM_TRY(MOZ_TO_RESULT(helper->Init()));

        QM_TRY(MOZ_TO_RESULT(helper->ProcessRepository()));
      }
    }

    // And finally rename persistent to default.
    QM_TRY(MOZ_TO_RESULT(aPersistentStorageDir->RenameTo(
        nullptr, nsLiteralString(DEFAULT_DIRECTORY_NAME))));

    return NS_OK;
  };

  return ExecuteInitialization(
      Initialization::UpgradeFromPersistentStorageDirectory, innerFunc);
}

template <typename Helper>
nsresult QuotaManager::UpgradeStorage(const int32_t aOldVersion,
                                      const int32_t aNewVersion,
                                      mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aNewVersion > aOldVersion);
  MOZ_ASSERT(aNewVersion <= kStorageVersion);
  MOZ_ASSERT(aConnection);

  for (const PersistenceType persistenceType : kAllPersistenceTypesButPrivate) {
    QM_TRY_UNWRAP(auto directory,
                  QM_NewLocalFile(GetStoragePath(persistenceType)));

    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(directory, Exists));

    if (!exists) {
      continue;
    }

    RefPtr<UpgradeStorageHelperBase> helper = new Helper(directory);

    QM_TRY(MOZ_TO_RESULT(helper->Init()));

    QM_TRY(MOZ_TO_RESULT(helper->ProcessRepository()));
  }

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const int32_t& storageVersion,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, GetSchemaVersion));

    MOZ_ASSERT(storageVersion == aOldVersion);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(aConnection->SetSchemaVersion(aNewVersion)));

  return NS_OK;
}

nsresult QuotaManager::UpgradeStorageFrom0_0To1_0(
    mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  const auto innerFunc = [this, &aConnection](const auto&) -> nsresult {
    QM_TRY(MOZ_TO_RESULT(UpgradeStorage<UpgradeStorageFrom0_0To1_0Helper>(
        0, MakeStorageVersion(1, 0), aConnection)));

    return NS_OK;
  };

  return ExecuteInitialization(Initialization::UpgradeStorageFrom0_0To1_0,
                               innerFunc);
}

nsresult QuotaManager::UpgradeStorageFrom1_0To2_0(
    mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  // The upgrade consists of a number of logically distinct bugs that
  // intentionally got fixed at the same time to trigger just one major
  // version bump.
  //
  //
  // Morgue directory cleanup
  // [Feature/Bug]:
  // The original bug that added "on demand" morgue cleanup is 1165119.
  //
  // [Mutations]:
  // Morgue directories are removed from all origin directories during the
  // upgrade process. Origin initialization and usage calculation doesn't try
  // to remove morgue directories anymore.
  //
  // [Downgrade-incompatible changes]:
  // Morgue directories can reappear if user runs an already upgraded profile
  // in an older version of Firefox. Morgue directories then prevent current
  // Firefox from initializing and using the storage.
  //
  //
  // App data removal
  // [Feature/Bug]:
  // The bug that removes isApp flags is 1311057.
  //
  // [Mutations]:
  // Origin directories with appIds are removed during the upgrade process.
  //
  // [Downgrade-incompatible changes]:
  // Origin directories with appIds can reappear if user runs an already
  // upgraded profile in an older version of Firefox. Origin directories with
  // appIds don't prevent current Firefox from initializing and using the
  // storage, but they wouldn't ever be removed again, potentially causing
  // problems once appId is removed from origin attributes.
  //
  //
  // Strip obsolete origin attributes
  // [Feature/Bug]:
  // The bug that strips obsolete origin attributes is 1314361.
  //
  // [Mutations]:
  // Origin directories with obsolete origin attributes are renamed and their
  // metadata files are updated during the upgrade process.
  //
  // [Downgrade-incompatible changes]:
  // Origin directories with obsolete origin attributes can reappear if user
  // runs an already upgraded profile in an older version of Firefox. Origin
  // directories with obsolete origin attributes don't prevent current Firefox
  // from initializing and using the storage, but they wouldn't ever be upgraded
  // again, potentially causing problems in future.
  //
  //
  // File manager directory renaming (client specific)
  // [Feature/Bug]:
  // The original bug that added "on demand" file manager directory renaming is
  // 1056939.
  //
  // [Mutations]:
  // All file manager directories are renamed to contain the ".files" suffix.
  //
  // [Downgrade-incompatible changes]:
  // File manager directories with the ".files" suffix prevent older versions of
  // Firefox from initializing and using the storage.
  // File manager directories without the ".files" suffix can appear if user
  // runs an already upgraded profile in an older version of Firefox. File
  // manager directories without the ".files" suffix then prevent current
  // Firefox from initializing and using the storage.

  const auto innerFunc = [this, &aConnection](const auto&) -> nsresult {
    QM_TRY(MOZ_TO_RESULT(UpgradeStorage<UpgradeStorageFrom1_0To2_0Helper>(
        MakeStorageVersion(1, 0), MakeStorageVersion(2, 0), aConnection)));

    return NS_OK;
  };

  return ExecuteInitialization(Initialization::UpgradeStorageFrom1_0To2_0,
                               innerFunc);
}

nsresult QuotaManager::UpgradeStorageFrom2_0To2_1(
    mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  // The upgrade is mainly to create a directory padding file in DOM Cache
  // directory to record the overall padding size of an origin.

  const auto innerFunc = [this, &aConnection](const auto&) -> nsresult {
    QM_TRY(MOZ_TO_RESULT(UpgradeStorage<UpgradeStorageFrom2_0To2_1Helper>(
        MakeStorageVersion(2, 0), MakeStorageVersion(2, 1), aConnection)));

    return NS_OK;
  };

  return ExecuteInitialization(Initialization::UpgradeStorageFrom2_0To2_1,
                               innerFunc);
}

nsresult QuotaManager::UpgradeStorageFrom2_1To2_2(
    mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  // The upgrade is mainly to clean obsolete origins in the repositoies, remove
  // asmjs client, and ".tmp" file in the idb folers.

  const auto innerFunc = [this, &aConnection](const auto&) -> nsresult {
    QM_TRY(MOZ_TO_RESULT(UpgradeStorage<UpgradeStorageFrom2_1To2_2Helper>(
        MakeStorageVersion(2, 1), MakeStorageVersion(2, 2), aConnection)));

    return NS_OK;
  };

  return ExecuteInitialization(Initialization::UpgradeStorageFrom2_1To2_2,
                               innerFunc);
}

nsresult QuotaManager::UpgradeStorageFrom2_2To2_3(
    mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  const auto innerFunc = [&aConnection](const auto&) -> nsresult {
    // Table `database`
    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
        nsLiteralCString("CREATE TABLE database"
                         "( cache_version INTEGER NOT NULL DEFAULT 0"
                         ");"))));

    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
        nsLiteralCString("INSERT INTO database (cache_version) "
                         "VALUES (0)"))));

#ifdef DEBUG
    {
      QM_TRY_INSPECT(
          const int32_t& storageVersion,
          MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, GetSchemaVersion));

      MOZ_ASSERT(storageVersion == MakeStorageVersion(2, 2));
    }
#endif

    QM_TRY(
        MOZ_TO_RESULT(aConnection->SetSchemaVersion(MakeStorageVersion(2, 3))));

    return NS_OK;
  };

  return ExecuteInitialization(Initialization::UpgradeStorageFrom2_2To2_3,
                               innerFunc);
}

nsresult QuotaManager::UpgradeStorageFrom2_3To2_4(
    mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  const auto innerFunc = [&aConnection](const auto&) -> nsresult {
#ifdef DEBUG
    {
      QM_TRY_INSPECT(
          const int32_t& storageVersion,
          MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, GetSchemaVersion));

      MOZ_ASSERT(storageVersion == MakeStorageVersion(2, 3));
    }
#endif

    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(nsPrintfCString(
        "PRAGMA page_size = %" PRIu32 ";", kSQLitePageSizeOverride))));

    QM_TRY(MOZ_TO_RESULT(
        aConnection->ExecuteSimpleSQL("PRAGMA auto_vacuum = INCREMENTAL;"_ns)));

    QM_TRY(
        MOZ_TO_RESULT(aConnection->SetSchemaVersion(MakeStorageVersion(2, 4))));

    return NS_OK;
  };

  return ExecuteInitialization(Initialization::UpgradeStorageFrom2_3To2_4,
                               innerFunc);
}

nsresult QuotaManager::MaybeRemoveLocalStorageDataAndArchive(
    nsIFile& aLsArchiveFile) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!CachedNextGenLocalStorageEnabled());

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aLsArchiveFile, Exists));

  if (!exists) {
    // If the ls archive doesn't exist then ls directories can't exist either.
    return NS_OK;
  }

  QM_TRY(MOZ_TO_RESULT(MaybeRemoveLocalStorageDirectories()));

  // We just deleted on-disk LS data that the cache may reference; the cache
  // is definitively inconsistent. Hard, not Soft — this should not be
  // suppressed by the test-only checkBuildId pref.
  InvalidateQuotaCache(CacheInvalidationLevel::Hard);

  // Finally remove the ls archive, so we don't have to check all origin
  // directories next time this method is called.
  QM_TRY(MOZ_TO_RESULT(aLsArchiveFile.Remove(false)));

  return NS_OK;
}

nsresult QuotaManager::MaybeRemoveLocalStorageDirectories() {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& defaultStorageDir,
                 QM_NewLocalFile(*mDefaultStoragePath));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(defaultStorageDir, Exists));

  if (!exists) {
    return NS_OK;
  }

  QM_TRY(CollectEachFile(
      *defaultStorageDir,
      [](const nsCOMPtr<nsIFile>& originDir) -> Result<Ok, nsresult> {
#ifdef DEBUG
        {
          QM_TRY_INSPECT(const bool& exists,
                         MOZ_TO_RESULT_INVOKE_MEMBER(originDir, Exists));
          MOZ_ASSERT(exists);
        }
#endif

        QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*originDir));

        switch (dirEntryKind) {
          case nsIFileKind::ExistsAsDirectory: {
            QM_TRY_INSPECT(
                const auto& lsDir,
                CloneFileAndAppend(*originDir, NS_LITERAL_STRING_FROM_CSTRING(
                                                   LS_DIRECTORY_NAME)));

            {
              QM_TRY_INSPECT(const bool& exists,
                             MOZ_TO_RESULT_INVOKE_MEMBER(lsDir, Exists));

              if (!exists) {
                return Ok{};
              }
            }

            {
              QM_TRY_INSPECT(const bool& isDirectory,
                             MOZ_TO_RESULT_INVOKE_MEMBER(lsDir, IsDirectory));

              if (!isDirectory) {
                QM_WARNING("ls entry is not a directory!");

                return Ok{};
              }
            }

            nsString path;
            QM_TRY(MOZ_TO_RESULT(lsDir->GetPath(path)));

            QM_WARNING("Deleting %s directory!",
                       NS_ConvertUTF16toUTF8(path).get());

            QM_TRY(MOZ_TO_RESULT(lsDir->Remove(/* aRecursive */ true)));

            break;
          }

          case nsIFileKind::ExistsAsFile: {
            QM_TRY_INSPECT(const auto& leafName,
                           MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                               nsAutoString, originDir, GetLeafName));

            // Unknown files during upgrade are allowed. Just warn if we find
            // them.
            if (!IsOSMetadata(leafName)) {
              UNKNOWN_FILE_WARNING(leafName);
            }

            break;
          }

          case nsIFileKind::DoesNotExist:
            // Ignore files that got removed externally while iterating.
            break;
        }
        return Ok{};
      }));

  return NS_OK;
}

Result<Ok, nsresult> QuotaManager::CopyLocalStorageArchiveFromWebAppsStore(
    nsIFile& aLsArchiveFile) const {
  AssertIsOnIOThread();
  MOZ_ASSERT(CachedNextGenLocalStorageEnabled());

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aLsArchiveFile, Exists));
    MOZ_ASSERT(!exists);
  }
#endif

  // Get the storage service first, we will need it at multiple places.
  QM_TRY_INSPECT(const auto& ss,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  // Get the web apps store file.
  QM_TRY_INSPECT(const auto& webAppsStoreFile, QM_NewLocalFile(mBasePath));

  QM_TRY(MOZ_TO_RESULT(
      webAppsStoreFile->Append(nsLiteralString(WEB_APPS_STORE_FILE_NAME))));

  // Now check if the web apps store is useable.
  QM_TRY_INSPECT(const auto& connection,
                 CreateWebAppsStoreConnection(*webAppsStoreFile, *ss));

  if (connection) {
    // Find out the journal mode.
    QM_TRY_INSPECT(const auto& stmt,
                   CreateAndExecuteSingleStepStatement(
                       *connection, "PRAGMA journal_mode;"_ns));

    QM_TRY_INSPECT(const auto& journalMode,
                   MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, *stmt,
                                                     GetUTF8String, 0));

    QM_TRY(MOZ_TO_RESULT(stmt->Finalize()));

    if (journalMode.EqualsLiteral("wal")) {
      // We don't copy the WAL file, so make sure the old database is fully
      // checkpointed.
      QM_TRY(MOZ_TO_RESULT(
          connection->ExecuteSimpleSQL("PRAGMA wal_checkpoint(TRUNCATE);"_ns)));
    }

    // Explicitely close the connection before the old database is copied.
    QM_TRY(MOZ_TO_RESULT(connection->Close()));

    // Copy the old database. The database is copied from
    // <profile>/webappsstore.sqlite to
    // <profile>/storage/ls-archive-tmp.sqlite
    // We use a "-tmp" postfix since we are not done yet.
    QM_TRY_INSPECT(const auto& storageDir, QM_NewLocalFile(*mStoragePath));

    QM_TRY(MOZ_TO_RESULT(webAppsStoreFile->CopyTo(
        storageDir, nsLiteralString(LS_ARCHIVE_TMP_FILE_NAME))));

    QM_TRY_INSPECT(const auto& lsArchiveTmpFile,
                   GetLocalStorageArchiveTmpFile(*mStoragePath));

    if (journalMode.EqualsLiteral("wal")) {
      QM_TRY_INSPECT(
          const auto& lsArchiveTmpConnection,
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
              nsCOMPtr<mozIStorageConnection>, ss, OpenUnsharedDatabase,
              lsArchiveTmpFile, mozIStorageService::CONNECTION_DEFAULT));

      // The archive will only be used for lazy data migration. There won't be
      // any concurrent readers and writers that could benefit from Write-Ahead
      // Logging. So switch to a standard rollback journal. The standard
      // rollback journal also provides atomicity across multiple attached
      // databases which is import for the lazy data migration to work safely.
      QM_TRY(MOZ_TO_RESULT(lsArchiveTmpConnection->ExecuteSimpleSQL(
          "PRAGMA journal_mode = DELETE;"_ns)));

      // Close the connection explicitly. We are going to rename the file below.
      QM_TRY(MOZ_TO_RESULT(lsArchiveTmpConnection->Close()));
    }

    // Finally, rename ls-archive-tmp.sqlite to ls-archive.sqlite
    QM_TRY(MOZ_TO_RESULT(lsArchiveTmpFile->MoveTo(
        nullptr, nsLiteralString(LS_ARCHIVE_FILE_NAME))));

    return Ok{};
  }

  // If webappsstore database is not useable, just create an empty archive.
  // XXX The code below should be removed and the caller should call us only
  // when webappstore.sqlite exists. CreateWebAppsStoreConnection should be
  // reworked to propagate database corruption instead of returning null
  // connection.
  // So, if there's no webappsstore.sqlite
  // MaybeCreateOrUpgradeLocalStorageArchive will call
  // CreateEmptyLocalStorageArchive instead of
  // CopyLocalStorageArchiveFromWebAppsStore.
  // If there's any corruption detected during
  // MaybeCreateOrUpgradeLocalStorageArchive (including nested calls like
  // CopyLocalStorageArchiveFromWebAppsStore and CreateWebAppsStoreConnection)
  // EnsureStorageIsInitializedInternal will fallback to
  // CreateEmptyLocalStorageArchive.

  // Ensure the storage directory actually exists.
  QM_TRY_INSPECT(const auto& storageDirectory, QM_NewLocalFile(*mStoragePath));

  QM_TRY_INSPECT(const bool& created, EnsureDirectory(*storageDirectory));

  (void)created;

  QM_TRY_UNWRAP(auto lsArchiveConnection,
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                    nsCOMPtr<mozIStorageConnection>, ss, OpenUnsharedDatabase,
                    &aLsArchiveFile, mozIStorageService::CONNECTION_DEFAULT));

  QM_TRY(MOZ_TO_RESULT(
      StorageDBUpdater::CreateCurrentSchema(lsArchiveConnection)));

  return Ok{};
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult>
QuotaManager::CreateLocalStorageArchiveConnection(
    nsIFile& aLsArchiveFile) const {
  AssertIsOnIOThread();
  MOZ_ASSERT(CachedNextGenLocalStorageEnabled());

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aLsArchiveFile, Exists));
    MOZ_ASSERT(exists);
  }
#endif

  QM_TRY_INSPECT(const bool& isDirectory,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aLsArchiveFile, IsDirectory));

  // A directory with the name of the archive file is treated as corruption
  // (similarly as wrong content of the file).
  QM_TRY(OkIf(!isDirectory), Err(NS_ERROR_FILE_CORRUPTED));

  QM_TRY_INSPECT(const auto& ss,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  // This may return NS_ERROR_FILE_CORRUPTED too.
  QM_TRY_UNWRAP(auto connection,
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                    nsCOMPtr<mozIStorageConnection>, ss, OpenUnsharedDatabase,
                    &aLsArchiveFile, mozIStorageService::CONNECTION_DEFAULT));

  // The legacy LS implementation removes the database and creates an empty one
  // when the schema can't be updated. The same effect can be achieved here by
  // mapping all errors to NS_ERROR_FILE_CORRUPTED. One such case is tested by
  // sub test case 3 of dom/localstorage/test/unit/test_archive.js
  QM_TRY(
      MOZ_TO_RESULT(StorageDBUpdater::Update(connection))
          .mapErr([](const nsresult rv) { return NS_ERROR_FILE_CORRUPTED; }));

  return connection;
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult>
QuotaManager::RecopyLocalStorageArchiveFromWebAppsStore(
    nsIFile& aLsArchiveFile) {
  AssertIsOnIOThread();
  MOZ_ASSERT(CachedNextGenLocalStorageEnabled());

  QM_TRY(MOZ_TO_RESULT(MaybeRemoveLocalStorageDirectories()));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aLsArchiveFile, Exists));

    MOZ_ASSERT(exists);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(aLsArchiveFile.Remove(false)));

  QM_TRY(CopyLocalStorageArchiveFromWebAppsStore(aLsArchiveFile));

  QM_TRY_UNWRAP(auto connection,
                CreateLocalStorageArchiveConnection(aLsArchiveFile));

  QM_TRY(MOZ_TO_RESULT(InitializeLocalStorageArchive(connection)));

  return connection;
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult>
QuotaManager::DowngradeLocalStorageArchive(nsIFile& aLsArchiveFile) {
  AssertIsOnIOThread();
  MOZ_ASSERT(CachedNextGenLocalStorageEnabled());

  QM_TRY_UNWRAP(auto connection,
                RecopyLocalStorageArchiveFromWebAppsStore(aLsArchiveFile));

  QM_TRY(MOZ_TO_RESULT(
      SaveLocalStorageArchiveVersion(connection, kLocalStorageArchiveVersion)));

  return connection;
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult>
QuotaManager::UpgradeLocalStorageArchiveFromLessThan4To4(
    nsIFile& aLsArchiveFile) {
  AssertIsOnIOThread();
  MOZ_ASSERT(CachedNextGenLocalStorageEnabled());

  QM_TRY_UNWRAP(auto connection,
                RecopyLocalStorageArchiveFromWebAppsStore(aLsArchiveFile));

  QM_TRY(MOZ_TO_RESULT(SaveLocalStorageArchiveVersion(connection, 4)));

  return connection;
}

/*
nsresult QuotaManager::UpgradeLocalStorageArchiveFrom4To5(
    nsCOMPtr<mozIStorageConnection>& aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(CachedNextGenLocalStorageEnabled());

  nsresult rv = SaveLocalStorageArchiveVersion(aConnection, 5);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}
*/

#ifdef DEBUG

void QuotaManager::AssertStorageIsInitializedInternal() const {
  AssertIsOnIOThread();
  MOZ_ASSERT(IsStorageInitializedInternal());
}

#endif  // DEBUG

nsresult QuotaManager::MaybeUpgradeToDefaultStorageDirectory(
    nsIFile& aStorageFile) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& storageFileExists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aStorageFile, Exists));

  if (!storageFileExists) {
    QM_TRY_INSPECT(const auto& indexedDBDir, QM_NewLocalFile(*mIndexedDBPath));

    QM_TRY_INSPECT(const auto& indexedDBDirExists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(indexedDBDir, Exists));

    if (indexedDBDirExists) {
      QM_TRY(MOZ_TO_RESULT(
          UpgradeFromIndexedDBDirectoryToPersistentStorageDirectory(
              indexedDBDir)));
    }

    QM_TRY_INSPECT(const auto& persistentStorageDir,
                   QM_NewLocalFile(*mStoragePath));

    QM_TRY(MOZ_TO_RESULT(persistentStorageDir->Append(
        nsLiteralString(PERSISTENT_DIRECTORY_NAME))));

    QM_TRY_INSPECT(const auto& persistentStorageDirExists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(persistentStorageDir, Exists));

    if (persistentStorageDirExists) {
      QM_TRY(MOZ_TO_RESULT(
          UpgradeFromPersistentStorageDirectoryToDefaultStorageDirectory(
              persistentStorageDir)));
    }
  }

  return NS_OK;
}

nsresult QuotaManager::MaybeCreateOrUpgradeStorage(
    mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();

  QM_TRY_UNWRAP(auto storageVersion,
                MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, GetSchemaVersion));

  // Hacky downgrade logic!
  // If we see major.minor of 3.0, downgrade it to be 2.1.
  if (storageVersion == kHackyPreDowngradeStorageVersion) {
    storageVersion = kHackyPostDowngradeStorageVersion;
    QM_TRY(MOZ_TO_RESULT(aConnection.SetSchemaVersion(storageVersion)),
           QM_PROPAGATE,
           [](const auto&) { MOZ_ASSERT(false, "Downgrade didn't take."); });
  }

  QM_TRY(OkIf(GetMajorStorageVersion(storageVersion) <= kMajorStorageVersion),
         NS_ERROR_FAILURE, [](const auto&) {
           NS_WARNING("Unable to initialize storage, version is too high!");
         });

  if (storageVersion < kStorageVersion) {
    const bool newDatabase = !storageVersion;

    QM_TRY_INSPECT(const auto& storageDir, QM_NewLocalFile(*mStoragePath));

    QM_TRY_INSPECT(const auto& storageDirExists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(storageDir, Exists));

    const bool newDirectory = !storageDirExists;

    if (newDatabase) {
      // Set the page size first.
      if (kSQLitePageSizeOverride) {
        QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(nsPrintfCString(
            "PRAGMA page_size = %" PRIu32 ";", kSQLitePageSizeOverride))));
      }

      QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
          "PRAGMA auto_vacuum = INCREMENTAL;"_ns)));
    }

    mozStorageTransaction transaction(
        &aConnection, false, mozIStorageConnection::TRANSACTION_IMMEDIATE);

    QM_TRY(MOZ_TO_RESULT(transaction.Start()));

    bool vacuum = false;

    // An upgrade method can upgrade the database, the storage or both.
    // The upgrade loop below can only be avoided when there's no database and
    // no storage yet (e.g. new profile).
    if (newDatabase && newDirectory) {
      QM_TRY(MOZ_TO_RESULT(CreateTables(&aConnection)));

#ifdef DEBUG
      {
        QM_TRY_INSPECT(
            const int32_t& storageVersion,
            MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, GetSchemaVersion),
            QM_ASSERT_UNREACHABLE);
        MOZ_ASSERT(storageVersion == kStorageVersion);
      }
#endif

      QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
          nsLiteralCString("INSERT INTO database (cache_version) "
                           "VALUES (0)"))));
    } else {
      // This logic needs to change next time we change the storage!
      static_assert(kStorageVersion == int32_t((2 << 16) + 4),
                    "Upgrade function needed due to storage version increase.");

      while (storageVersion != kStorageVersion) {
        if (storageVersion == 0) {
          QM_TRY(MOZ_TO_RESULT(UpgradeStorageFrom0_0To1_0(&aConnection)));
        } else if (storageVersion == MakeStorageVersion(1, 0)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeStorageFrom1_0To2_0(&aConnection)));
        } else if (storageVersion == MakeStorageVersion(2, 0)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeStorageFrom2_0To2_1(&aConnection)));
        } else if (storageVersion == MakeStorageVersion(2, 1)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeStorageFrom2_1To2_2(&aConnection)));
        } else if (storageVersion == MakeStorageVersion(2, 2)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeStorageFrom2_2To2_3(&aConnection)));
        } else if (storageVersion == MakeStorageVersion(2, 3)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeStorageFrom2_3To2_4(&aConnection)));
          vacuum = true;
        } else {
          QM_FAIL(NS_ERROR_FAILURE, []() {
            NS_WARNING(
                "Unable to initialize storage, no upgrade path is "
                "available!");
          });
        }

        QM_TRY_UNWRAP(storageVersion, MOZ_TO_RESULT_INVOKE_MEMBER(
                                          aConnection, GetSchemaVersion));
      }

      MOZ_ASSERT(storageVersion == kStorageVersion);
    }

    QM_TRY(MOZ_TO_RESULT(transaction.Commit()));

    // Best-effort VACUUM to apply the page_size and auto_vacuum PRAGMAs
    // set inside UpgradeStorageFrom2_3To2_4. If it fails, the database
    // remains functional with the old page size.
    if (vacuum) {
      QM_WARNONLY_TRY(
          MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL("VACUUM;"_ns)));
    }
  }

  return NS_OK;
}

OkOrErr QuotaManager::MaybeRemoveLocalStorageArchiveTmpFile() {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(
      const auto& lsArchiveTmpFile,
      QM_TO_RESULT_TRANSFORM(GetLocalStorageArchiveTmpFile(*mStoragePath)));

  QM_TRY_INSPECT(const bool& exists,
                 QM_TO_RESULT_INVOKE_MEMBER(lsArchiveTmpFile, Exists));

  if (exists) {
    QM_TRY(QM_TO_RESULT(lsArchiveTmpFile->Remove(false)));
  }

  return Ok{};
}

Result<Ok, nsresult> QuotaManager::MaybeCreateOrUpgradeLocalStorageArchive(
    nsIFile& aLsArchiveFile) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(
      const bool& lsArchiveFileExisted,
      ([this, &aLsArchiveFile]() -> Result<bool, nsresult> {
        QM_TRY_INSPECT(const bool& exists,
                       MOZ_TO_RESULT_INVOKE_MEMBER(aLsArchiveFile, Exists));

        if (!exists) {
          QM_TRY(CopyLocalStorageArchiveFromWebAppsStore(aLsArchiveFile));
        }

        return exists;
      }()));

  QM_TRY_UNWRAP(auto connection,
                CreateLocalStorageArchiveConnection(aLsArchiveFile));

  QM_TRY_INSPECT(const auto& initialized,
                 IsLocalStorageArchiveInitialized(*connection));

  if (!initialized) {
    QM_TRY(MOZ_TO_RESULT(InitializeLocalStorageArchive(connection)));
  }

  QM_TRY_UNWRAP(int32_t version, LoadLocalStorageArchiveVersion(*connection));

  if (version > kLocalStorageArchiveVersion) {
    // Close local storage archive connection. We are going to remove underlying
    // file.
    QM_TRY(MOZ_TO_RESULT(connection->Close()));

    // This will wipe the archive and any migrated data and recopy the archive
    // from webappsstore.sqlite.
    QM_TRY_UNWRAP(connection, DowngradeLocalStorageArchive(aLsArchiveFile));

    QM_TRY_UNWRAP(version, LoadLocalStorageArchiveVersion(*connection));

    MOZ_ASSERT(version == kLocalStorageArchiveVersion);
  } else if (version != kLocalStorageArchiveVersion) {
    // The version can be zero either when the archive didn't exist or it did
    // exist, but the archive was created without any version information.
    // We don't need to do any upgrades only if it didn't exist because existing
    // archives without version information must be recopied to really fix bug
    // 1542104. See also bug 1546305 which introduced archive versions.
    if (!lsArchiveFileExisted) {
      MOZ_ASSERT(version == 0);

      QM_TRY(MOZ_TO_RESULT(SaveLocalStorageArchiveVersion(
          connection, kLocalStorageArchiveVersion)));
    } else {
      static_assert(kLocalStorageArchiveVersion == 4,
                    "Upgrade function needed due to LocalStorage archive "
                    "version increase.");

      while (version != kLocalStorageArchiveVersion) {
        if (version < 4) {
          // Close local storage archive connection. We are going to remove
          // underlying file.
          QM_TRY(MOZ_TO_RESULT(connection->Close()));

          // This won't do an "upgrade" in a normal sense. It will wipe the
          // archive and any migrated data and recopy the archive from
          // webappsstore.sqlite
          QM_TRY_UNWRAP(connection, UpgradeLocalStorageArchiveFromLessThan4To4(
                                        aLsArchiveFile));
        } /* else if (version == 4) {
          QM_TRY(MOZ_TO_RESULT(UpgradeLocalStorageArchiveFrom4To5(connection)));
        } */
        else {
          QM_FAIL(Err(NS_ERROR_FAILURE), []() {
            QM_WARNING(
                "Unable to initialize LocalStorage archive, no upgrade path "
                "is available!");
          });
        }

        QM_TRY_UNWRAP(version, LoadLocalStorageArchiveVersion(*connection));
      }

      MOZ_ASSERT(version == kLocalStorageArchiveVersion);
    }
  }

  // At this point, we have finished initializing the local storage archive, and
  // can continue storage initialization. We don't know though if the actual
  // data in the archive file is readable. We can't do a PRAGMA integrity_check
  // here though, because that would be too heavyweight.

  return Ok{};
}

Result<Ok, nsresult> QuotaManager::CreateEmptyLocalStorageArchive(
    nsIFile& aLsArchiveFile) const {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aLsArchiveFile, Exists));

  // If it exists, remove it. It might be a directory, so remove it recursively.
  if (exists) {
    QM_TRY(MOZ_TO_RESULT(aLsArchiveFile.Remove(true)));

    // XXX If we crash right here, the next session will copy the archive from
    // webappsstore.sqlite again!
    // XXX Create a marker file before removing the archive which can be
    // used in MaybeCreateOrUpgradeLocalStorageArchive to create an empty
    // archive instead of recopying it from webapppstore.sqlite (in other
    // words, finishing what was started here).
  }

  QM_TRY_INSPECT(const auto& ss,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  QM_TRY_UNWRAP(const auto connection,
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                    nsCOMPtr<mozIStorageConnection>, ss, OpenUnsharedDatabase,
                    &aLsArchiveFile, mozIStorageService::CONNECTION_DEFAULT));

  QM_TRY(MOZ_TO_RESULT(StorageDBUpdater::CreateCurrentSchema(connection)));

  QM_TRY(MOZ_TO_RESULT(InitializeLocalStorageArchive(connection)));

  QM_TRY(MOZ_TO_RESULT(
      SaveLocalStorageArchiveVersion(connection, kLocalStorageArchiveVersion)));

  return Ok{};
}

RefPtr<BoolPromise> QuotaManager::InitializeStorage() {
  AssertIsOnOwningThread();

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromNull(), OriginScope::FromNull(),
      ClientStorageScope::CreateFromNull(),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If storage is initialized but there's a clear storage or shutdown storage
  // operation already scheduled, we can't immediately resolve the promise and
  // return from the function because the clear and shutdown storage operation
  // uninitializes storage.
  if (mStorageInitialized &&
      !prepareInfo.IsBlockedBy(kUninitStorageOnlyCategory)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire(std::move(prepareInfo))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr(this), directoryLock](
                 const BoolPromise::ResolveOrRejectValue& aValue) mutable {
               if (aValue.IsReject()) {
                 return BoolPromise::CreateAndReject(aValue.RejectValue(),
                                                     __func__);
               }

               return self->InitializeStorage(std::move(directoryLock));
             });
}

RefPtr<BoolPromise> QuotaManager::InitializeStorage(
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If storage is initialized and the directory lock for the initialize
  // storage operation is acquired, we can immediately resolve the promise and
  // return from the function because there can't be a clear storage or
  // shutdown storage operation which would uninitialize storage.
  if (mStorageInitialized) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializeStorageOp =
      CreateInitOp(WrapMovingNotNullUnchecked(this), std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializeStorageOp);

  initializeStorageOp->RunImmediately();

  return initializeStorageOp->OnResults()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        self->mStorageInitialized = true;

        return BoolPromise::CreateAndResolve(true, __func__);
      });
}

RefPtr<BoolPromise> QuotaManager::StorageInitialized() {
  AssertIsOnOwningThread();

  auto storageInitializedOp =
      CreateStorageInitializedOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*storageInitializedOp);

  storageInitializedOp->RunImmediately();

  return storageInitializedOp->OnResults();
}

nsresult QuotaManager::EnsureStorageIsInitializedInternal() {
  GECKO_TRACE_SCOPE("dom::quota",
                    "QuotaManager::EnsureStorageIsInitializedInternal");

  DiagnosticAssertIsOnIOThread();

  const auto innerFunc =
      [&](const auto& firstInitializationAttempt) -> nsresult {
    if (mStorageConnection) {
      MOZ_ASSERT(firstInitializationAttempt.Recorded());
      return NS_OK;
    }

    QM_TRY_INSPECT(const auto& storageFile, QM_NewLocalFile(mBasePath));
    QM_TRY(MOZ_TO_RESULT(storageFile->Append(mStorageName + kSQLiteSuffix)));

    QM_TRY(MOZ_TO_RESULT(MaybeUpgradeToDefaultStorageDirectory(*storageFile)));

    QM_TRY_INSPECT(const auto& ss,
                   MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                           MOZ_SELECT_OVERLOAD(do_GetService),
                                           MOZ_STORAGE_SERVICE_CONTRACTID));

    QM_TRY_UNWRAP(
        auto connection,
        QM_OR_ELSE_WARN_IF(
            // Expression.
            MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                nsCOMPtr<mozIStorageConnection>, ss, OpenUnsharedDatabase,
                storageFile, mozIStorageService::CONNECTION_DEFAULT),
            // Predicate.
            IsDatabaseCorruptionError,
            // Fallback.
            ErrToDefaultOk<nsCOMPtr<mozIStorageConnection>>));

    // OpenUnsharedDatabase only validates the SQLite header
    // (page 1). Internal corruption (such as freelist inconsistency from
    // an interrupted schema migration) passes the open check but breaks
    // all subsequent storage operations. Run a quick integrity check to
    // catch this class of corruption; if it fails, close and null the
    // connection so the nuke-and-rebuild path below handles recovery.
    // See bug 2065480 for details.
    if (connection) {
      QM_WARNONLY_TRY(DatabasePassesIntegrityCheck(*connection)
                          .andThen([](bool ok) -> Result<Ok, nsresult> {
                            return ok ? Result<Ok, nsresult>{Ok{}}
                                      : Err(NS_ERROR_FILE_CORRUPTED);
                          }),
                      // Clean-up: called if DatabasePassesIntegrityCheck failed
                      // or found that the database is corrupted (!ok)
                      [&](const auto&) {
                        // Reset the connection to force database file to be
                        // recreated
                        connection->Close();
                        connection = nullptr;
                      });
    }

    bool storageFileWasCorrupted = false;

    if (!connection) {
      // Nuke the database file.
      QM_TRY(MOZ_TO_RESULT(storageFile->Remove(false)));

      QM_TRY_UNWRAP(connection, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                    nsCOMPtr<mozIStorageConnection>, ss,
                                    OpenUnsharedDatabase, storageFile,
                                    mozIStorageService::CONNECTION_DEFAULT));

      storageFileWasCorrupted = true;
    }

    // We want extra durability for this important file.
    QM_TRY(MOZ_TO_RESULT(
        connection->ExecuteSimpleSQL("PRAGMA synchronous = EXTRA;"_ns)));

    // Check to make sure that the storage version is correct.
    QM_TRY(MOZ_TO_RESULT(MaybeCreateOrUpgradeStorage(*connection)));

    QM_TRY(MaybeRemoveLocalStorageArchiveTmpFile());

    QM_TRY_INSPECT(const auto& lsArchiveFile,
                   GetLocalStorageArchiveFile(*mStoragePath));

    if (CachedNextGenLocalStorageEnabled()) {
      QM_TRY(QM_OR_ELSE_WARN_IF(
          // Expression.
          MaybeCreateOrUpgradeLocalStorageArchive(*lsArchiveFile),
          // Predicate.
          IsDatabaseCorruptionError,
          // Fallback.
          ([&](const nsresult rv) -> Result<Ok, nsresult> {
            QM_TRY_RETURN(CreateEmptyLocalStorageArchive(*lsArchiveFile));
          })));
    } else {
      QM_TRY(
          MOZ_TO_RESULT(MaybeRemoveLocalStorageDataAndArchive(*lsArchiveFile)));
    }

    QM_TRY_INSPECT(const auto& cacheInfo,
                   MaybeCreateOrUpgradeCache(*connection));
    mCacheUsable = cacheInfo.usable;
    mCacheRequiresFullScan = cacheInfo.requiresFullScan;

    if (storageFileWasCorrupted) {
      mCacheUsable = false;
      mCacheRequiresFullScan = true;
    }

    // Read-and-reset the invalidation level. Soft is gated on the
    // checkBuildId pref (so tests using a packaged profile from a different
    // binary can opt out of the build-id-mismatch wipe); Hard always wins.
    const auto invalidationLevel = gInvalidateQuotaCache.exchange(
        QuotaManager::CacheInvalidationLevel::None);
    const bool shouldInvalidateCache =
        (invalidationLevel == QuotaManager::CacheInvalidationLevel::Hard) ||
        (invalidationLevel == QuotaManager::CacheInvalidationLevel::Soft &&
         StaticPrefs::dom_quotaManager_caching_checkBuildId());

    if (mCacheUsable && shouldInvalidateCache) {
      QM_TRY(InvalidateCache(*connection));
    }

    uint32_t pauseOnIOThreadMs =
        StaticPrefs::dom_quotaManager_storageInitialization_pauseOnIOThreadMs();
    if (pauseOnIOThreadMs > 0) {
      PR_Sleep(PR_MillisecondsToInterval(pauseOnIOThreadMs));
    }

    mStorageConnection = std::move(connection);
    mOriginUpserter = MakeRefPtr<OriginUpserter>(mStorageConnection);

    mCipherKeyStore = gCipherKeyStore;
    mStorageEncryptionEnabled = gStorageEncryptionEnabled;
    if (mStorageEncryptionEnabled) {
      // Fail closed: new origins could not be encrypted without a key store.
      QM_TRY(OkIf(static_cast<bool>(mCipherKeyStore)), NS_ERROR_NOT_AVAILABLE);
    }

    return NS_OK;
  };

  return ExecuteInitialization(
      Initialization::Storage,
      "dom::quota::FirstInitializationAttempt::Storage"_ns, innerFunc);
}

Result<RefPtr<CipherKeyManager<NSSCipherStrategy>>, nsresult>
QuotaManager::GetOrCreateCipherKeyManager(const nsACString& aOrigin,
                                          Client::Type aClientType) {
  AssertIsOnIOThread();

  if (!mCipherKeyStore) {
    // LockstoreService can only be created on the main thread.
    QM_TRY(MOZ_TO_RESULT(SyncRunnable::DispatchToThread(
        GetMainThreadSerialEventTarget(),
        NS_NewRunnableFunction("QuotaManager::EnsureCipherKeyStore",
                               []() { EnsureCipherKeyStore(); }))));
    mCipherKeyStore = gCipherKeyStore;
  }

  // An encrypted origin can be neither read nor written without a key store.
  QM_TRY(OkIf(static_cast<bool>(mCipherKeyStore)), Err(NS_ERROR_NOT_AVAILABLE));

  return RefPtr<CipherKeyManager<NSSCipherStrategy>>(
      CipherKeyManager<NSSCipherStrategy>::CreatePersistent(
          "PersistentCipherKeyManager", mCipherKeyStore, aOrigin,
          Client::TypeToText(aClientType)));
}

RefPtr<BoolPromise> QuotaManager::TemporaryStorageInitialized() {
  AssertIsOnOwningThread();

  auto temporaryStorageInitializedOp =
      CreateTemporaryStorageInitializedOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*temporaryStorageInitializedOp);

  temporaryStorageInitializedOp->RunImmediately();

  return temporaryStorageInitializedOp->OnResults();
}

void QuotaManager::FlushDirtyOriginInfos() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mStorageConnection);

  const uint32_t writeQuiescentThresholdMs = std::max(
      100u, StaticPrefs::dom_quotaManager_originWriteQuiescentThresholdMs());
  const auto writeQuiescentThresholdDuration =
      TimeDuration::FromMilliseconds(writeQuiescentThresholdMs);
  const uint32_t maxOriginsToSaveInOneBatch =
      StaticPrefs::dom_quotaManager_maxOriginsToSaveInOneBatch();

  MutexAutoLock lock(mQuotaMutex);

  const auto now = TimeStamp::Now();
  uint32_t flushed = 0;

  nsTArray<RefPtr<OriginInfo>> itemsToRequeue;

  while (true) {
    RefPtr<OriginInfo> info;
    if (!mDirtyOriginInfos.Pop(&info)) {
      break;
    }

    if (flushed >= maxOriginsToSaveInOneBatch) {
      itemsToRequeue.AppendElement(std::move(info));
      continue;
    }

    GroupInfo* groupInfo = info->GetGroupInfo();
    if (!groupInfo) {
      continue;
    }

    // The OriginInfo may have been cleaned by a direct CreateDirectoryMetadata2
    // call (e.g. via InitializeOrigin) since it was queued. Skip the redundant
    // write in that case.
    if (!info->LockedDirty()) {
      continue;
    }

    if (!IsOriginInitialized(groupInfo->GetPersistenceType(), info->Origin())) {
      itemsToRequeue.AppendElement(std::move(info));
      continue;
    }

    const auto age = now - info->GetLastModifiedTime();
    if (age < writeQuiescentThresholdDuration) {
      itemsToRequeue.AppendElement(std::move(info));
      continue;
    }

    MOZ_ASSERT(!info->IsPrivate());

    FullOriginMetadata fullOriginMetadata =
        info->LockedFlattenToFullOriginMetadata();

    // The origin directory may not exist yet if the flush timer fires before
    // the origin has been fully initialized on disk. Requeue and retry on
    // the next cycle.
    auto hasDirectoryResult = DoesOriginDirectoryExist(fullOriginMetadata);
    if (hasDirectoryResult.isErr() || !hasDirectoryResult.inspect()) {
      itemsToRequeue.AppendElement(std::move(info));
      continue;
    }

    // Requeue on transient I/O errors; we expect failures to be short-lived.
    auto directoryResult = GetOriginDirectory(fullOriginMetadata);
    if (directoryResult.isErr()) {
      itemsToRequeue.AppendElement(std::move(info));
      continue;
    }

    const nsCOMPtr<nsIFile>& directory = directoryResult.inspect();

    if (NS_WARN_IF(NS_FAILED(
            SettleDirectoryMetadata2(*directory, fullOriginMetadata)))) {
      itemsToRequeue.AppendElement(std::move(info));
      continue;
    }
    MOZ_ASSERT(!fullOriginMetadata.mDirty);

    info->LockedSetClean();
    ++flushed;
  }

  for (auto&& itemToRequeue : itemsToRequeue) {
    auto* message = new UnboundedMPSCQueue<RefPtr<OriginInfo>>::Message();
    message->data = std::move(itemToRequeue);
    mDirtyOriginInfos.Push(message);
  }
}

RefPtr<UniversalDirectoryLockPromise> QuotaManager::OpenStorageDirectory(
    const PersistenceScope& aPersistenceScope, const OriginScope& aOriginScope,
    const ClientStorageScope& aClientStorageScope, bool aExclusive,
    bool aInitializeOrigins, DirectoryLockCategory aCategory,
    Maybe<RefPtr<UniversalDirectoryLock>&> aPendingDirectoryLockOut) {
  AssertIsOnOwningThread();

  nsTArray<RefPtr<BoolPromise>> promises;

  // Directory locks for specific initializations can be null, indicating that
  // either the initialization should not occur or it has already been
  // completed.

  RefPtr<UniversalDirectoryLock> storageDirectoryLock =
      CreateDirectoryLockForInitialization(
          *this, PersistenceScope::CreateFromNull(), OriginScope::FromNull(),
          ClientStorageScope::CreateFromNull(), mStorageInitialized,
          MakeBlockedByChecker(kUninitStorageOnlyCategory),
          MakeBackInserter(promises));

  RefPtr<UniversalDirectoryLock> persistentStorageDirectoryLock;

  RefPtr<UniversalDirectoryLock> temporaryStorageDirectoryLock;

  if (aInitializeOrigins) {
    if (MatchesPersistentPersistenceScope(aPersistenceScope)) {
      persistentStorageDirectoryLock = CreateDirectoryLockForInitialization(
          *this, PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_PERSISTENT),
          OriginScope::FromNull(), ClientStorageScope::CreateFromNull(),
          mPersistentStorageInitialized,
          MakeBlockedByChecker(kUninitStorageOnlyCategory),
          MakeBackInserter(promises));
    }

    // We match all best effort persistence types, but the persistence scope is
    // created only for the temporary and default persistence type because the
    // repository for the private persistence type is never initialized as part
    // of temporary initialization. However, some other steps of the temporary
    // storage initialization need to be done even for the private persistence
    // type. For example, the initialization of mTemporaryStorageLimit.
    if (MatchesBestEffortPersistenceScope(aPersistenceScope)) {
      temporaryStorageDirectoryLock = CreateDirectoryLockForInitialization(
          *this,
          PersistenceScope::CreateFromSet(PERSISTENCE_TYPE_TEMPORARY,
                                          PERSISTENCE_TYPE_DEFAULT),
          OriginScope::FromNull(), ClientStorageScope::CreateFromNull(),
          mTemporaryStorageInitialized,
          MakeBlockedByChecker(kUninitStorageOnlyCategory),
          MakeBackInserter(promises));
    }
  }

  RefPtr<UniversalDirectoryLock> universalDirectoryLock =
      CreateDirectoryLockInternal(aPersistenceScope, aOriginScope,
                                  aClientStorageScope, aExclusive, aCategory);

  RefPtr<BoolPromise> universalDirectoryLockPromise =
      universalDirectoryLock->Acquire();

  if (aPendingDirectoryLockOut.isSome()) {
    aPendingDirectoryLockOut.ref() = universalDirectoryLock;
  }

  return BoolPromise::All(GetCurrentSerialEventTarget(), promises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](const CopyableTArray<bool>& aResolveValues) {
            return BoolPromise::CreateAndResolve(true, __func__);
          },
          [](nsresult aRejectValue) {
            return BoolPromise::CreateAndReject(aRejectValue, __func__);
          })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             MaybeInitialize(std::move(storageDirectoryLock), this,
                             &QuotaManager::InitializeStorage))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             MaybeInitialize(std::move(persistentStorageDirectoryLock), this,
                             &QuotaManager::InitializePersistentStorage))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             MaybeInitialize(std::move(temporaryStorageDirectoryLock), this,
                             &QuotaManager::InitializeTemporaryStorage))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [universalDirectoryLockPromise =
                  std::move(universalDirectoryLockPromise)](
                 const BoolPromise::ResolveOrRejectValue& aValue) mutable {
               if (aValue.IsReject()) {
                 return BoolPromise::CreateAndReject(aValue.RejectValue(),
                                                     __func__);
               }

               return std::move(universalDirectoryLockPromise);
             })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [universalDirectoryLock = std::move(universalDirectoryLock)](
                 const BoolPromise::ResolveOrRejectValue& aValue) mutable {
               if (aValue.IsReject()) {
                 DropDirectoryLockIfNotDropped(universalDirectoryLock);

                 return UniversalDirectoryLockPromise::CreateAndReject(
                     aValue.RejectValue(), __func__);
               }

               return UniversalDirectoryLockPromise::CreateAndResolve(
                   std::move(universalDirectoryLock), __func__);
             });
}

RefPtr<QuotaManager::ClientDirectoryLockHandlePromise>
QuotaManager::OpenClientDirectory(
    const ClientMetadata& aClientMetadata, bool aInitializeOrigin,
    bool aCreateIfNonExistent,
    Maybe<RefPtr<ClientDirectoryLock>&> aPendingDirectoryLockOut) {
  AssertIsOnOwningThread();

  RefPtr<ClientDirectoryLockHandlePromise> promise =
      OpenClientDirectoryImpl(aClientMetadata, aInitializeOrigin,
                              aCreateIfNonExistent, aPendingDirectoryLockOut);

  NotifyClientDirectoryOpeningStarted(*this);

  return promise;
}

RefPtr<QuotaManager::ClientDirectoryLockHandlePromise>
QuotaManager::OpenClientDirectoryImpl(
    const ClientMetadata& aClientMetadata, bool aInitializeOrigin,
    bool aCreateIfNonExistent,
    Maybe<RefPtr<ClientDirectoryLock>&> aPendingDirectoryLockOut) {
  AssertIsOnOwningThread();

  const auto persistenceType = aClientMetadata.mPersistenceType;

  nsTArray<RefPtr<BoolPromise>> promises;

  // Directory locks for specific initializations can be null, indicating that
  // either the initialization should not occur or it has already been
  // completed.

  RefPtr<UniversalDirectoryLock> storageDirectoryLock =
      CreateDirectoryLockForInitialization(
          *this, PersistenceScope::CreateFromNull(), OriginScope::FromNull(),
          ClientStorageScope::CreateFromNull(), mStorageInitialized,
          MakeBlockedByChecker(kUninitStorageOnlyCategory),
          MakeBackInserter(promises));

  RefPtr<UniversalDirectoryLock> temporaryStorageDirectoryLock;

  RefPtr<UniversalDirectoryLock> groupDirectoryLock;

  if (IsBestEffortPersistenceType(persistenceType)) {
    temporaryStorageDirectoryLock = CreateDirectoryLockForInitialization(
        *this,
        PersistenceScope::CreateFromSet(PERSISTENCE_TYPE_TEMPORARY,
                                        PERSISTENCE_TYPE_DEFAULT),
        OriginScope::FromNull(), ClientStorageScope::CreateFromNull(),
        mTemporaryStorageInitialized,
        MakeBlockedByChecker(kUninitStorageOnlyCategory),
        MakeBackInserter(promises));

    const bool groupInitialized = IsTemporaryGroupInitialized(aClientMetadata);

    groupDirectoryLock = CreateDirectoryLockForInitialization(
        *this,
        PersistenceScope::CreateFromSet(PERSISTENCE_TYPE_TEMPORARY,
                                        PERSISTENCE_TYPE_DEFAULT),
        OriginScope::FromGroup(aClientMetadata.mGroup),
        ClientStorageScope::CreateFromNull(), groupInitialized,
        MakeBlockedByChecker(kUninitStorageOnlyCategory),
        MakeBackInserter(promises));
  }

  RefPtr<UniversalDirectoryLock> originDirectoryLock;

  if (aInitializeOrigin) {
    const bool originInitialized =
        persistenceType == PERSISTENCE_TYPE_PERSISTENT
            ? IsPersistentOriginInitialized(aClientMetadata)
            : IsTemporaryOriginInitialized(aClientMetadata);

    originDirectoryLock = CreateDirectoryLockForInitialization(
        *this, PersistenceScope::CreateFromValue(persistenceType),
        OriginScope::FromOrigin(aClientMetadata),
        ClientStorageScope::CreateFromNull(), originInitialized,
        MakeBlockedByChecker(kUninitOriginsAndBroaderCategories),
        MakeBackInserter(promises));
  }

  RefPtr<UniversalDirectoryLock> clientInitDirectoryLock;

  const bool clientInitialized =
      persistenceType == PERSISTENCE_TYPE_PERSISTENT
          ? IsPersistentClientInitialized(aClientMetadata)
          : IsTemporaryClientInitialized(aClientMetadata);

  clientInitDirectoryLock = CreateDirectoryLockForInitialization(
      *this, PersistenceScope::CreateFromValue(persistenceType),
      OriginScope::FromOrigin(aClientMetadata),
      ClientStorageScope::CreateFromClient(aClientMetadata.mClientType),
      clientInitialized,
      MakeBlockedByChecker(kUninitClientsAndBroaderCategories),
      MakeBackInserter(promises));

  RefPtr<ClientDirectoryLock> clientDirectoryLock =
      CreateDirectoryLock(aClientMetadata, /* aExclusive */ false);

  promises.AppendElement(clientDirectoryLock->Acquire());

  if (aPendingDirectoryLockOut.isSome()) {
    aPendingDirectoryLockOut.ref() = clientDirectoryLock;
  }

  RefPtr<UniversalDirectoryLock> firstAccessDirectoryLock =
      CreateSaveOriginAccessTimeLock(*this, aClientMetadata);

  promises.AppendElement(firstAccessDirectoryLock->Acquire());

  RefPtr<UniversalDirectoryLock> lastAccessDirectoryLock =
      CreateSaveOriginAccessTimeLock(*this, aClientMetadata);

  promises.AppendElement(lastAccessDirectoryLock->Acquire());

  return BoolPromise::All(GetCurrentSerialEventTarget(), promises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [](const CopyableTArray<bool>& aResolveValues) {
            return BoolPromise::CreateAndResolve(true, __func__);
          },
          [](nsresult aRejectValue) {
            return BoolPromise::CreateAndReject(aRejectValue, __func__);
          })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             MaybeInitialize(std::move(storageDirectoryLock), this,
                             &QuotaManager::InitializeStorage))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             MaybeInitialize(std::move(temporaryStorageDirectoryLock), this,
                             &QuotaManager::InitializeTemporaryStorage))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          MaybeInitialize(
              std::move(groupDirectoryLock),
              [self = RefPtr(this), aClientMetadata](
                  RefPtr<UniversalDirectoryLock> groupDirectoryLock) mutable {
                return self->InitializeTemporaryGroup(
                    aClientMetadata, std::move(groupDirectoryLock));
              }))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          MaybeInitialize(
              std::move(originDirectoryLock),
              [self = RefPtr(this), aClientMetadata, aCreateIfNonExistent](
                  RefPtr<UniversalDirectoryLock> originDirectoryLock) mutable {
                if (aClientMetadata.mPersistenceType ==
                    PERSISTENCE_TYPE_PERSISTENT) {
                  return self->InitializePersistentOrigin(
                      aClientMetadata, std::move(originDirectoryLock));
                }

                return self->InitializeTemporaryOrigin(
                    aClientMetadata, aCreateIfNonExistent,
                    std::move(originDirectoryLock));
              }))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             MaybeInitialize(
                 std::move(clientInitDirectoryLock),
                 [self = RefPtr(this), aClientMetadata,
                  aCreateIfNonExistent](RefPtr<UniversalDirectoryLock>
                                            clientInitDirectoryLock) mutable {
                   if (aClientMetadata.mPersistenceType ==
                       PERSISTENCE_TYPE_PERSISTENT) {
                     return self->InitializePersistentClient(
                         aClientMetadata, std::move(clientInitDirectoryLock));
                   }

                   return self->InitializeTemporaryClient(
                       aClientMetadata, aCreateIfNonExistent,
                       std::move(clientInitDirectoryLock));
                 }))
      ->Then(
          GetCurrentSerialEventTarget(), __func__ /* clang formatting anchor */,
          MaybeFinalize(
              std::move(clientDirectoryLock),
              std::move(firstAccessDirectoryLock),
              std::move(lastAccessDirectoryLock),
              [self = RefPtr(this), aClientMetadata](
                  RefPtr<ClientDirectoryLock> clientDirectoryLock,
                  RefPtr<UniversalDirectoryLock> firstAccessDirectoryLock,
                  RefPtr<UniversalDirectoryLock>
                      lastAccessDirectoryLock) mutable {
                auto clientDirectoryLockHandle =
                    ClientDirectoryLockHandle(std::move(clientDirectoryLock));

                self->RegisterClientDirectoryLockHandle(
                    aClientMetadata,
                    [&self = *self, &aClientMetadata, &firstAccessDirectoryLock,
                     &lastAccessDirectoryLock](
                        OpenClientDirectoryInfo&
                            aOpenClientDirectoryInfo) mutable {
                      if (aClientMetadata.mPersistenceType !=
                          PERSISTENCE_TYPE_PERSISTENT) {
                        aOpenClientDirectoryInfo.SetFirstAccessPromise(
                            self.SaveOriginAccessTime(
                                aClientMetadata,
                                std::move(firstAccessDirectoryLock)));

                        aOpenClientDirectoryInfo.SetLastAccessDirectoryLock(
                            std::move(lastAccessDirectoryLock));
                      }
                    });

                clientDirectoryLockHandle.SetRegistered(true);

                SafeDropDirectoryLock(firstAccessDirectoryLock);
                SafeDropDirectoryLock(lastAccessDirectoryLock);

                return Map<ClientDirectoryLockHandlePromise>(
                    self->WithOpenClientDirectoryInfo(
                        aClientMetadata,
                        [aClientMetadata](
                            OpenClientDirectoryInfo& aOpenClientDirectoryInfo) {
                          if (aClientMetadata.mPersistenceType !=
                              PERSISTENCE_TYPE_PERSISTENT) {
                            return aOpenClientDirectoryInfo
                                .AcquireFirstAccessPromise();
                          }

                          return BoolPromise::CreateAndResolve(true, __func__);
                        }),
                    [clientDirectoryLockHandle =
                         std::move(clientDirectoryLockHandle)](
                        const BoolPromise::ResolveOrRejectValue&
                            aValue) mutable {
                      return std::move(clientDirectoryLockHandle);
                    });
              }));
}

RefPtr<ClientDirectoryLock> QuotaManager::CreateDirectoryLock(
    const ClientMetadata& aClientMetadata, bool aExclusive) {
  AssertIsOnOwningThread();

  return ClientDirectoryLock::Create(
      WrapNotNullUnchecked(this), aClientMetadata.mPersistenceType,
      aClientMetadata, aClientMetadata.mClientType, aExclusive);
}

RefPtr<UniversalDirectoryLock> QuotaManager::CreateDirectoryLockInternal(
    const PersistenceScope& aPersistenceScope, const OriginScope& aOriginScope,
    const ClientStorageScope& aClientStorageScope, bool aExclusive,
    DirectoryLockCategory aCategory) {
  AssertIsOnOwningThread();

  return UniversalDirectoryLock::CreateInternal(
      WrapNotNullUnchecked(this), aPersistenceScope, aOriginScope,
      aClientStorageScope, aExclusive, aCategory);
}

bool QuotaManager::IsPendingOrigin(
    const OriginMetadata& aOriginMetadata) const {
  MutexAutoLock lock(mQuotaMutex);

  RefPtr<OriginInfo> originInfo =
      LockedGetOriginInfo(aOriginMetadata.mPersistenceType, aOriginMetadata);

  return originInfo && !originInfo->LockedDirectoryExists();
}

RefPtr<BoolPromise> QuotaManager::InitializePersistentStorage() {
  AssertIsOnOwningThread();

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_PERSISTENT),
      OriginScope::FromNull(), ClientStorageScope::CreateFromNull(),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If persistent storage is initialized but there's a clear storage or
  // shutdown storage operation already scheduled, we can't immediately resolve
  // the promise and return from the function because the clear or shutdown
  // storage operation uninitializes storage.
  if (mPersistentStorageInitialized &&
      !prepareInfo.IsBlockedBy(kUninitStorageOnlyCategory)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire(std::move(prepareInfo))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this), directoryLock](
              const BoolPromise::ResolveOrRejectValue& aValue) mutable {
            if (aValue.IsReject()) {
              return BoolPromise::CreateAndReject(aValue.RejectValue(),
                                                  __func__);
            }

            return self->InitializePersistentStorage(std::move(directoryLock));
          });
}

RefPtr<BoolPromise> QuotaManager::InitializePersistentStorage(
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If persistent storage is initialized and the directory lock for the
  // initialize persistent storage operation is acquired, we can immediately
  // resolve the promise and return from the function because there can't be a
  // clear storage or shutdown storage operation which would uninitialize
  // persistent storage.
  if (mPersistentStorageInitialized) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializePersistentStorageOp = CreateInitializePersistentStorageOp(
      WrapMovingNotNullUnchecked(this), std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializePersistentStorageOp);

  initializePersistentStorageOp->RunImmediately();

  return initializePersistentStorageOp->OnResults()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        self->mPersistentStorageInitialized = true;

        return BoolPromise::CreateAndResolve(true, __func__);
      });
}

RefPtr<BoolPromise> QuotaManager::PersistentStorageInitialized() {
  AssertIsOnOwningThread();

  auto persistentStorageInitializedOp =
      CreatePersistentStorageInitializedOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*persistentStorageInitializedOp);

  persistentStorageInitializedOp->RunImmediately();

  return persistentStorageInitializedOp->OnResults();
}

nsresult QuotaManager::EnsurePersistentStorageIsInitializedInternal() {
  AssertIsOnIOThread();
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);

  const auto innerFunc =
      [&](const auto& firstInitializationAttempt) -> nsresult {
    if (mPersistentStorageInitializedInternal) {
      MOZ_ASSERT(firstInitializationAttempt.Recorded());
      return NS_OK;
    }

    QM_TRY(MOZ_TO_RESULT(ExecuteInitialization(
        Initialization::PersistentRepository, [&](const auto&) -> nsresult {
          return InitializeRepository(PERSISTENCE_TYPE_PERSISTENT,
                                      [](auto&) {});
        })));

    mPersistentStorageInitializedInternal = true;

    return NS_OK;
  };

  return ExecuteInitialization(
      Initialization::TemporaryStorage,
      "dom::quota::FirstInitializationAttempt::PersistentStorage"_ns,
      innerFunc);
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryGroup(
    const PrincipalMetadata& aPrincipalMetadata) {
  AssertIsOnOwningThread();

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromSet(PERSISTENCE_TYPE_TEMPORARY,
                                      PERSISTENCE_TYPE_DEFAULT),
      OriginScope::FromGroup(aPrincipalMetadata.mGroup),
      ClientStorageScope::CreateFromNull(),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If temporary group is initialized but there's a clear storage or shutdown
  // storage operation already scheduled, we can't immediately resolve the
  // promise and return from the function because the clear and shutdown
  // storage operation uninitializes storage.
  if (IsTemporaryGroupInitialized(aPrincipalMetadata) &&
      !prepareInfo.IsBlockedBy(kUninitStorageOnlyCategory)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire(std::move(prepareInfo))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr(this), aPrincipalMetadata, directoryLock](
                 const BoolPromise::ResolveOrRejectValue& aValue) mutable {
               if (aValue.IsReject()) {
                 return BoolPromise::CreateAndReject(aValue.RejectValue(),
                                                     __func__);
               }

               return self->InitializeTemporaryGroup(aPrincipalMetadata,
                                                     std::move(directoryLock));
             });
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryGroup(
    const PrincipalMetadata& aPrincipalMetadata,
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If temporary group is initialized and the directory lock for the
  // initialize temporary group operation is acquired, we can immediately
  // resolve the promise and return from the function because there can't be a
  // clear storage or shutdown storage operation which would uninitialize
  // temporary storage.
  if (IsTemporaryGroupInitialized(aPrincipalMetadata)) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializeTemporaryGroupOp = CreateInitializeTemporaryGroupOp(
      WrapMovingNotNullUnchecked(this), aPrincipalMetadata,
      std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializeTemporaryGroupOp);

  initializeTemporaryGroupOp->RunImmediately();

  return Map<BoolPromise>(
      initializeTemporaryGroupOp->OnResults(),
      [self = RefPtr(this), group = aPrincipalMetadata.mGroup](
          const BoolPromise::ResolveOrRejectValue& aValue) {
        self->mBackgroundThreadAccessible.Access()->mInitializedGroups.Insert(
            group);

        return aValue.ResolveValue();
      });
}

RefPtr<BoolPromise> QuotaManager::TemporaryGroupInitialized(
    const PrincipalMetadata& aPrincipalMetadata) {
  AssertIsOnOwningThread();

  auto temporaryGroupInitializedOp = CreateTemporaryGroupInitializedOp(
      WrapMovingNotNullUnchecked(this), aPrincipalMetadata);

  RegisterNormalOriginOp(*temporaryGroupInitializedOp);

  temporaryGroupInitializedOp->RunImmediately();

  return temporaryGroupInitializedOp->OnResults();
}

bool QuotaManager::IsTemporaryGroupInitialized(
    const PrincipalMetadata& aPrincipalMetadata) {
  AssertIsOnOwningThread();

  return mBackgroundThreadAccessible.Access()->mInitializedGroups.Contains(
      aPrincipalMetadata.mGroup);
}

bool QuotaManager::IsTemporaryGroupInitializedInternal(
    const PrincipalMetadata& aPrincipalMetadata) const {
  AssertIsOnIOThread();

  MutexAutoLock lock(mQuotaMutex);

  return LockedHasGroupInfoPair(aPrincipalMetadata.mGroup);
}

Result<Ok, nsresult> QuotaManager::EnsureTemporaryGroupIsInitializedInternal(
    const PrincipalMetadata& aPrincipalMetadata) {
  AssertIsOnIOThread();
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);
  MOZ_DIAGNOSTIC_ASSERT(mTemporaryStorageInitializedInternal);

  const auto innerFunc = [&aPrincipalMetadata,
                          this](const auto&) -> mozilla::Result<Ok, nsresult> {
    NotifyGroupInitializationStarted(*this);

    const auto& array =
        mIOThreadAccessible.Access()->mAllTemporaryOrigins.Lookup(
            aPrincipalMetadata.mGroup);
    if (!array) {
      return Ok{};
    }

    // XXX At the moment, the loop skips all elements in the array because
    // temporary storage initialization still initializes all temporary
    // origins. This is going to change soon with the planned asynchronous
    // temporary origin initialization done in the background.
    for (const auto& originMetadata : *array) {
      if (NS_WARN_IF(IsShuttingDown())) {
        return Err(NS_ERROR_ABORT);
      }

      if (IsTemporaryOriginInitializedInternal(originMetadata)) {
        continue;
      }

      QM_TRY_UNWRAP(auto directory, GetOriginDirectory(originMetadata));

      QM_TRY_INSPECT(const auto& metadata,
                     LoadFullOriginMetadataWithRestore(directory));

      // XXX Check corruption here!
      QM_TRY(MOZ_TO_RESULT(InitializeOrigin(directory, metadata,
                                            /* aForGroup */ true)));
    }

    // XXX Evict origins that exceed their group limit here.

    SleepIfEnabled(
        StaticPrefs::dom_quotaManager_groupInitialization_pauseOnIOThreadMs());

    return Ok{};
  };

  return ExecuteGroupInitialization(
      aPrincipalMetadata.mGroup, GroupInitialization::TemporaryGroup,
      "dom::quota::FirstOriginInitializationAttempt::TemporaryGroup"_ns,
      innerFunc);
}

RefPtr<BoolPromise> QuotaManager::InitializePersistentOrigin(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_PERSISTENT),
      OriginScope::FromOrigin(aOriginMetadata),
      ClientStorageScope::CreateFromNull(),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If persistent origin is initialized but there's a clear storage, shutdown
  // storage, clear origin, or shutdown origin operation already scheduled, we
  // can't immediately resolve the promise and return from the function because
  // the clear and shutdown storage operations uninitialize storage (which also
  // includes uninitialization of origins) and because clear and shutdown origin
  // operations uninitialize origins directly.
  if (IsPersistentOriginInitialized(aOriginMetadata) &&
      !prepareInfo.IsBlockedBy(kUninitOriginsAndBroaderCategories)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire(std::move(prepareInfo))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr(this), aOriginMetadata, directoryLock](
                 const BoolPromise::ResolveOrRejectValue& aValue) mutable {
               if (aValue.IsReject()) {
                 return BoolPromise::CreateAndReject(aValue.RejectValue(),
                                                     __func__);
               }

               return self->InitializePersistentOrigin(
                   aOriginMetadata, std::move(directoryLock));
             });
}

RefPtr<BoolPromise> QuotaManager::InitializePersistentOrigin(
    const OriginMetadata& aOriginMetadata,
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If persistent origin is initialized and the directory lock for the
  // initialize persistent origin operation is acquired, we can immediately
  // resolve the promise and return from the function because there can't be a
  // clear storage, shutdown storage, clear origin, or shutdown origin
  // operation which would uninitialize storage (which also includes
  // uninitialization of origins), or which would uninitialize origins
  // directly.
  if (IsPersistentOriginInitialized(aOriginMetadata)) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializePersistentOriginOp = CreateInitializePersistentOriginOp(
      WrapMovingNotNullUnchecked(this), aOriginMetadata,
      std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializePersistentOriginOp);

  initializePersistentOriginOp->RunImmediately();

  return Map<BoolPromise>(
      initializePersistentOriginOp->OnResults(),
      [self = RefPtr(this), origin = aOriginMetadata.mOrigin](
          const BoolPromise::ResolveOrRejectValue& aValue) {
        self->NoteInitializedOrigin(PERSISTENCE_TYPE_PERSISTENT, origin);

        return aValue.ResolveValue();
      });
}

RefPtr<BoolPromise> QuotaManager::PersistentOriginInitialized(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

  auto persistentOriginInitializedOp = CreatePersistentOriginInitializedOp(
      WrapMovingNotNullUnchecked(this), aOriginMetadata);

  RegisterNormalOriginOp(*persistentOriginInitializedOp);

  persistentOriginInitializedOp->RunImmediately();

  return persistentOriginInitializedOp->OnResults();
}

bool QuotaManager::IsPersistentOriginInitialized(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

  return IsOriginInitialized(aOriginMetadata.mPersistenceType,
                             aOriginMetadata.mOrigin);
}

bool QuotaManager::IsPersistentOriginInitializedInternal(
    const OriginMetadata& aOriginMetadata) const {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

  return mInitializedOriginsInternal.Contains(aOriginMetadata.mOrigin);
}

Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
QuotaManager::EnsurePersistentOriginIsInitializedInternal(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);

  const auto innerFunc = [&aOriginMetadata,
                          this](const auto& firstInitializationAttempt)
      -> mozilla::Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult> {
    const auto extraInfo =
        ScopedLogExtraInfo{ScopedLogExtraInfo::kTagStorageOriginTainted,
                           aOriginMetadata.mStorageOrigin};

    QM_TRY_UNWRAP(auto directory, GetOriginDirectory(aOriginMetadata));

    if (mInitializedOriginsInternal.Contains(aOriginMetadata.mOrigin)) {
      MOZ_ASSERT(firstInitializationAttempt.Recorded());
      return std::pair(std::move(directory), false);
    }

    QM_TRY_INSPECT(const bool& created, EnsureOriginDirectory(*directory));

    QM_TRY_INSPECT(
        const FullOriginMetadata& fullOriginMetadata,
        ([this, created, &directory,
          &aOriginMetadata]() -> Result<FullOriginMetadata, nsresult> {
          if (created) {
            const int64_t timestamp = PR_Now();

            FullOriginMetadata fullOriginMetadata{
                aOriginMetadata,
                OriginStateMetadata{/* aLastAccessTime */ timestamp,
                                    /* aLastMaintenanceDate */
                                    Date::FromTimestamp(timestamp).ToDays(),
                                    /* aAccessed */ false,
                                    /* aPersisted */ true,
                                    /* aDirty */ false},
                ClientUsageArray(), /* aUsage */ 0, kCurrentQuotaVersion};

            // Only creating .metadata-v2 to reduce IO.
            QM_TRY(MOZ_TO_RESULT(
                CreateDirectoryMetadata2(*directory, fullOriginMetadata)));

            return fullOriginMetadata;
          }

          // Get the metadata. We only use the timestamp.
          QM_TRY_INSPECT(const auto& metadata,
                         LoadFullOriginMetadataWithRestore(directory));

          return metadata;
        }()));

    QM_TRY(MOZ_TO_RESULT(InitializeOrigin(directory, fullOriginMetadata)));

    mInitializedOriginsInternal.AppendElement(aOriginMetadata.mOrigin);

    return std::pair(std::move(directory), created);
  };

  return ExecuteOriginInitialization(
      aOriginMetadata.mOrigin, OriginInitialization::PersistentOrigin,
      "dom::quota::FirstOriginInitializationAttempt::PersistentOrigin"_ns,
      innerFunc);
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryOrigin(
    const OriginMetadata& aOriginMetadata, bool aCreateIfNonExistent) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromValue(aOriginMetadata.mPersistenceType),
      OriginScope::FromOrigin(aOriginMetadata),
      ClientStorageScope::CreateFromNull(),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If temporary origin is initialized but there's a clear storage, shutdown
  // storage, clear origin, or shutdown origin operation already scheduled, we
  // can't immediately resolve the promise and return from the function because
  // the clear and shutdown storage operations uninitialize storage (which also
  // includes uninitialization of origins) and because clear and shutdown origin
  // operations uninitialize origins directly.
  if (IsTemporaryOriginInitialized(aOriginMetadata) &&
      !prepareInfo.IsBlockedBy(kUninitOriginsAndBroaderCategories)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire(std::move(prepareInfo))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr(this), aOriginMetadata, aCreateIfNonExistent,
              directoryLock](
                 const BoolPromise::ResolveOrRejectValue& aValue) mutable {
               if (aValue.IsReject()) {
                 return BoolPromise::CreateAndReject(aValue.RejectValue(),
                                                     __func__);
               }

               return self->InitializeTemporaryOrigin(aOriginMetadata,
                                                      aCreateIfNonExistent,
                                                      std::move(directoryLock));
             });
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryOrigin(
    const OriginMetadata& aOriginMetadata, bool aCreateIfNonExistent,
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If temporary origin is initialized and the directory lock for the
  // initialize temporary origin operation is acquired, we can immediately
  // resolve the promise and return from the function because there can't be a
  // clear storage, shutdown storage, clear origin, or shutdown origin
  // operation which would uninitialize storage (which also includes
  // uninitialization of origins), or which would uninitialize origins
  // directly.
  if (IsTemporaryOriginInitialized(aOriginMetadata)) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializeTemporaryOriginOp = CreateInitializeTemporaryOriginOp(
      WrapMovingNotNullUnchecked(this), aOriginMetadata, aCreateIfNonExistent,
      std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializeTemporaryOriginOp);

  initializeTemporaryOriginOp->RunImmediately();

  return Map<BoolPromise>(
      initializeTemporaryOriginOp->OnResults(),
      [self = RefPtr(this), persistenceType = aOriginMetadata.mPersistenceType,
       origin = aOriginMetadata.mOrigin](
          const BoolPromise::ResolveOrRejectValue& aValue) {
        self->NoteInitializedOrigin(persistenceType, origin);

        return aValue.ResolveValue();
      });
}

RefPtr<BoolPromise> QuotaManager::TemporaryOriginInitialized(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  auto temporaryOriginInitializedOp = CreateTemporaryOriginInitializedOp(
      WrapMovingNotNullUnchecked(this), aOriginMetadata);

  RegisterNormalOriginOp(*temporaryOriginInitializedOp);

  temporaryOriginInitializedOp->RunImmediately();

  return temporaryOriginInitializedOp->OnResults();
}

bool QuotaManager::IsTemporaryOriginInitialized(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  return IsOriginInitialized(aOriginMetadata.mPersistenceType,
                             aOriginMetadata.mOrigin);
}

bool QuotaManager::IsTemporaryOriginInitializedInternal(
    const OriginMetadata& aOriginMetadata) const {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  MutexAutoLock lock(mQuotaMutex);

  RefPtr<OriginInfo> originInfo =
      LockedGetOriginInfo(aOriginMetadata.mPersistenceType, aOriginMetadata);

  return static_cast<bool>(originInfo);
}

Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
QuotaManager::EnsureTemporaryOriginIsInitializedInternal(
    const OriginMetadata& aOriginMetadata, bool aCreateIfNonExistent) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);
  MOZ_DIAGNOSTIC_ASSERT(mTemporaryStorageInitializedInternal);

  const auto innerFunc = [&aOriginMetadata, aCreateIfNonExistent,
                          this](const auto&)
      -> mozilla::Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult> {
    // Get directory for this origin and persistence type.
    QM_TRY_UNWRAP(auto directory, GetOriginDirectory(aOriginMetadata));

    if (IsTemporaryOriginInitializedInternal(aOriginMetadata)) {
      return std::pair(std::move(directory), false);
    }

    // We apply the offset only here when setting the initial access time for
    // a new origin. In theory PersistOp::DoDirectoryWork could also honor this
    // pref, but since the pref is intended for testing only, we do it only
    // here for now for simplicity.
    const int64_t timestamp = []() {
      const int64_t now = PR_Now();
      const uint32_t offsetSec = StaticPrefs::
          dom_quotaManager_temporaryStorage_initialOriginAccessTimeOffsetSec();

      if (offsetSec > 0) {
        CheckedInt<int64_t> ts(now);

        ts -= CheckedInt<int64_t>(offsetSec) * PR_USEC_PER_SEC;
        if (!ts.isValid()) {
          // Offset too large (underflow), use current time as if there was no
          // offset.

          QM_WARNING("Initial origin access time offset too large!");

          return now;
        }

        return ts.value();
      }

      return now;
    }();

    OriginMetadata originMetadataWithEncryption = aOriginMetadata;
    originMetadataWithEncryption.mIsEncrypted = mStorageEncryptionEnabled;

    FullOriginMetadata fullOriginMetadata{
        originMetadataWithEncryption,
        OriginStateMetadata{
            /* aLastAccessTime */ timestamp,
            /* aLastMaintenanceDate */ Date::FromTimestamp(timestamp).ToDays(),
            /* aAccessed */ false,
            /* aPersisted */ false,
            /* aDirty */ false},
        ClientUsageArray(), /* aUsage */ 0, kCurrentQuotaVersion};

    if (!aCreateIfNonExistent) {
      // The caller asked us not to create the origin directory if it doesn't
      // exist yet. But it may already exist from a previous session whose
      // quota cache (storage.sqlite `origin` table) was invalidated at
      // startup (e.g. on a binary version change via
      // QuotaManager::InvalidateQuotaCache). In that case the origin
      // metadata and client data are still on disk but our in-memory state
      // doesn't know about them yet. We must scan the directory to recover
      // mClientUsages — otherwise LS clients that find their `data.sqlite`
      // on disk but get `Nothing` from `GetUsageForClient(LS)` will trip
      // their `MOZ_ASSERT(hasUsage)`.
      bool directoryExists = false;
      QM_TRY(MOZ_TO_RESULT(directory->Exists(&directoryExists)));
      if (directoryExists) {
        QM_TRY_INSPECT(const auto& metadata,
                       LoadFullOriginMetadataWithRestore(directory));

        AddTemporaryOrigin(metadata);

        QM_TRY(MOZ_TO_RESULT(InitializeOrigin(directory, metadata)));
      } else {
        InitQuotaForOrigin(fullOriginMetadata, /* aDirectoryExists */ false);
      }

      return std::pair(std::move(directory), false);
    }

    QM_TRY_INSPECT(const bool& created, EnsureOriginDirectory(*directory));

    if (created) {
      // Usually, infallible operations are placed after fallible ones.
      // However, since we lack atomic support for creating the origin
      // directory along with its metadata, we need to add the origin to cached
      // origins right after directory creation.
      AddTemporaryOrigin(fullOriginMetadata);

      // Only creating .metadata-v2 to reduce IO.
      QM_TRY(MOZ_TO_RESULT(
          CreateDirectoryMetadata2(*directory, fullOriginMetadata)));

      // Don't need to traverse the directory, since it's empty.
      InitQuotaForOrigin(fullOriginMetadata);
    } else {
      QM_TRY_INSPECT(const auto& metadata,
                     LoadFullOriginMetadataWithRestore(directory));

      QM_TRY(MOZ_TO_RESULT(InitializeOrigin(directory, metadata)));
    }

    // TODO: If the metadata file exists and we didn't call
    //       LoadFullOriginMetadataWithRestore for it (because the quota info
    //       was loaded from the cache), then the group in the metadata file
    //       may be wrong, so it should be checked and eventually updated.
    //       It's not a big deal that we are not doing it here, because the
    //       origin will be marked as "accessed", so
    //       LoadFullOriginMetadataWithRestore will be called for the metadata
    //       file in next session in LoadQuotaFromCache.
    //       (If a previous origin initialization failed, we actually do call
    //       LoadFullOriginMetadataWithRestore which updates the group)

    return std::pair(std::move(directory), created);
  };

  return ExecuteOriginInitialization(
      aOriginMetadata.mOrigin, OriginInitialization::TemporaryOrigin,
      "dom::quota::FirstOriginInitializationAttempt::TemporaryOrigin"_ns,
      innerFunc);
}

RefPtr<BoolPromise> QuotaManager::InitializePersistentClient(
    const ClientMetadata& aClientMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_PERSISTENT),
      OriginScope::FromOrigin(aClientMetadata),
      ClientStorageScope::CreateFromClient(aClientMetadata.mClientType),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If the persistent client is initialized but there's a clear storage,
  // shutdown storage, clear origin, shutdown origin, clear client, or shutdown
  // client operation already scheduled, we can't immediately resolve the
  // promise and return from the function. This is because:
  // - clear and shutdown storage operations uninitialize storage,
  //   which cascades to origins and clients;
  // - clear and shutdown origin operations uninitialize origins,
  //   which also uninitialize clients;
  // - clear and shutdown client operations uninitialize the client directly.
  if (IsPersistentClientInitialized(aClientMetadata) &&
      !prepareInfo.IsBlockedBy(kUninitClientsAndBroaderCategories)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this), aClientMetadata,
       directoryLock](const BoolPromise::ResolveOrRejectValue& aValue) mutable {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        return self->InitializePersistentClient(aClientMetadata,
                                                std::move(directoryLock));
      });
}

RefPtr<BoolPromise> QuotaManager::InitializePersistentClient(
    const ClientMetadata& aClientMetadata,
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If the persistent client is initialized and the directory lock for the
  // initialize persistent client operation is acquired, we can immediately
  // resolve the promise and return from the function because there can't be a
  // clear storage, shutdown storage, clear origin, shutdown origin, clear
  // client, or shutdown client operation in progress. These operations would
  // otherwise uninitialize storage (which cascades to origins and clients),
  // uninitialize origins (which also uninitialize clients), or uninitialize
  // the client directly.
  if (IsPersistentClientInitialized(aClientMetadata)) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializePersistentClientOp = CreateInitializePersistentClientOp(
      WrapMovingNotNullUnchecked(this), aClientMetadata,
      std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializePersistentClientOp);

  initializePersistentClientOp->RunImmediately();

  return Map<BoolPromise>(
      initializePersistentClientOp->OnResults(),
      [self = RefPtr(this), origin = aClientMetadata.mOrigin,
       clientType = aClientMetadata.mClientType](
          const BoolPromise::ResolveOrRejectValue& aValue) {
        self->NoteInitializedClient(PERSISTENCE_TYPE_PERSISTENT, origin,
                                    clientType);

        return aValue.ResolveValue();
      });
}

bool QuotaManager::IsPersistentClientInitialized(
    const ClientMetadata& aClientMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

  return IsClientInitialized(aClientMetadata.mPersistenceType,
                             aClientMetadata.mOrigin,
                             aClientMetadata.mClientType);
}

Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
QuotaManager::EnsurePersistentClientIsInitialized(
    const ClientMetadata& aClientMetadata) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);
  MOZ_ASSERT(Client::IsValidType(aClientMetadata.mClientType));
  MOZ_DIAGNOSTIC_ASSERT(IsStorageInitializedInternal());
  MOZ_DIAGNOSTIC_ASSERT(
      IsPersistentOriginInitializedInternal(aClientMetadata.mOrigin));

  QM_TRY_UNWRAP(auto directory, GetOriginDirectory(aClientMetadata));

  QM_TRY(MOZ_TO_RESULT(
      directory->Append(Client::TypeToString(aClientMetadata.mClientType))));

  QM_TRY_UNWRAP(bool created, EnsureDirectory(*directory));

  return std::pair(std::move(directory), created);
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryClient(
    const ClientMetadata& aClientMetadata, bool aCreateIfNonExistent) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromValue(aClientMetadata.mPersistenceType),
      OriginScope::FromOrigin(aClientMetadata),
      ClientStorageScope::CreateFromClient(aClientMetadata.mClientType),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If the temporary client is initialized but there's a clear storage,
  // shutdown storage, clear origin, shutdown origin, clear client, or shutdown
  // client operation already scheduled, we can't immediately resolve the
  // promise and return from the function. This is because:
  // - clear and shutdown storage operations uninitialize storage,
  //   which cascades to origins and clients;
  // - clear and shutdown origin operations uninitialize origins,
  //   which also uninitialize clients;
  // - clear and shutdown client operations uninitialize the client directly.
  if (IsTemporaryClientInitialized(aClientMetadata) &&
      !prepareInfo.IsBlockedBy(kUninitClientsAndBroaderCategories)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this), aClientMetadata, aCreateIfNonExistent,
       directoryLock](const BoolPromise::ResolveOrRejectValue& aValue) mutable {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        return self->InitializeTemporaryClient(
            aClientMetadata, aCreateIfNonExistent, std::move(directoryLock));
      });
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryClient(
    const ClientMetadata& aClientMetadata, bool aCreateIfNonExistent,
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If the temporary client is initialized and the directory lock for the
  // initialize persistent client operation is acquired, we can immediately
  // resolve the promise and return from the function because there can't be a
  // clear storage, shutdown storage, clear origin, shutdown origin, clear
  // client, or shutdown client operation in progress. These operations would
  // otherwise uninitialize storage (which cascades to origins and clients),
  // uninitialize origins (which also uninitialize clients), or uninitialize
  // the client directly.
  if (IsTemporaryClientInitialized(aClientMetadata)) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializeTemporaryClientOp = CreateInitializeTemporaryClientOp(
      WrapMovingNotNullUnchecked(this), aClientMetadata, aCreateIfNonExistent,
      std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializeTemporaryClientOp);

  initializeTemporaryClientOp->RunImmediately();

  return Map<BoolPromise>(
      initializeTemporaryClientOp->OnResults(),
      [self = RefPtr(this), persistenceType = aClientMetadata.mPersistenceType,
       origin = aClientMetadata.mOrigin,
       clientType = aClientMetadata.mClientType](
          const BoolPromise::ResolveOrRejectValue& aValue) {
        self->NoteInitializedClient(persistenceType, origin, clientType);

        return aValue.ResolveValue();
      });
}

bool QuotaManager::IsTemporaryClientInitialized(
    const ClientMetadata& aClientMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  return IsClientInitialized(aClientMetadata.mPersistenceType,
                             aClientMetadata.mOrigin,
                             aClientMetadata.mClientType);
}

Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult>
QuotaManager::EnsureTemporaryClientIsInitialized(
    const ClientMetadata& aClientMetadata, bool aCreateIfNonExistent) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aClientMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);
  MOZ_ASSERT(Client::IsValidType(aClientMetadata.mClientType));
  MOZ_DIAGNOSTIC_ASSERT(IsStorageInitializedInternal());
  MOZ_DIAGNOSTIC_ASSERT(IsTemporaryStorageInitializedInternal());
  MOZ_DIAGNOSTIC_ASSERT(IsTemporaryOriginInitializedInternal(aClientMetadata));

  if (!aCreateIfNonExistent) {
    QM_TRY_UNWRAP(auto directory, GetOriginDirectory(aClientMetadata));

    return std::pair(std::move(directory), false);
  }

  QM_TRY_UNWRAP(auto directory,
                GetOrCreateTemporaryOriginDirectory(aClientMetadata));

  QM_TRY(MOZ_TO_RESULT(
      directory->Append(Client::TypeToString(aClientMetadata.mClientType))));

  QM_TRY_UNWRAP(bool created, EnsureDirectory(*directory));

  return std::pair(std::move(directory), created);
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryStorage() {
  AssertIsOnOwningThread();

  RefPtr<UniversalDirectoryLock> directoryLock = CreateDirectoryLockInternal(
      PersistenceScope::CreateFromSet(PERSISTENCE_TYPE_TEMPORARY,
                                      PERSISTENCE_TYPE_DEFAULT),
      OriginScope::FromNull(), ClientStorageScope::CreateFromNull(),
      /* aExclusive */ false);

  auto prepareInfo = directoryLock->Prepare();

  // If temporary storage is initialized but there's a clear storage or
  // shutdown storage operation already scheduled, we can't immediately resolve
  // the promise and return from the function because the clear and shutdown
  // storage operation uninitializes storage.
  if (mTemporaryStorageInitialized &&
      !prepareInfo.IsBlockedBy(kUninitStorageOnlyCategory)) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  return directoryLock->Acquire(std::move(prepareInfo))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this), directoryLock](
              const BoolPromise::ResolveOrRejectValue& aValue) mutable {
            if (aValue.IsReject()) {
              return BoolPromise::CreateAndReject(aValue.RejectValue(),
                                                  __func__);
            }

            return self->InitializeTemporaryStorage(std::move(directoryLock));
          });
}

RefPtr<BoolPromise> QuotaManager::InitializeTemporaryStorage(
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  // If temporary storage is initialized and the directory lock for the
  // initialize temporary storage operation is acquired, we can immediately
  // resolve the promise and return from the function because there can't be a
  // clear storage or shutdown storage operation which would uninitialize
  // temporary storage.
  if (mTemporaryStorageInitialized) {
    DropDirectoryLock(aDirectoryLock);

    return BoolPromise::CreateAndResolve(true, __func__);
  }

  auto initializeTemporaryStorageOp = CreateInitTemporaryStorageOp(
      WrapMovingNotNullUnchecked(this), std::move(aDirectoryLock));

  RegisterNormalOriginOp(*initializeTemporaryStorageOp);

  initializeTemporaryStorageOp->RunImmediately();

  return initializeTemporaryStorageOp->OnResults()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this)](
          MaybePrincipalMetadataArrayPromise::ResolveOrRejectValue&& aValue) {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        self->mTemporaryStorageInitialized = true;

        if (aValue.ResolveValue() &&
            QuotaPrefs::LazyOriginInitializationEnabled()) {
          self->mBackgroundThreadAccessible.Access()->mUninitializedGroups =
              aValue.ResolveValue().extract();

          if (QuotaPrefs::TriggerOriginInitializationInBackgroundEnabled()) {
            self->InitializeAllTemporaryOrigins();
          }
        }

        return BoolPromise::CreateAndResolve(true, __func__);
      });
}

nsresult QuotaManager::InitializeTemporaryStorageInternal() {
  AssertIsOnIOThread();
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);
  MOZ_DIAGNOSTIC_ASSERT(!mTemporaryStorageInitializedInternal);

  nsCOMPtr<nsIFile> storageDir;
  QM_TRY(MOZ_TO_RESULT(
      NS_NewLocalFile(GetStoragePath(), getter_AddRefs(storageDir))));

  // The storage directory must exist before calling GetTemporaryStorageLimit.
  QM_TRY_INSPECT(const bool& created, EnsureDirectory(*storageDir));

  (void)created;

  QM_TRY_UNWRAP(mTemporaryStorageLimit, GetTemporaryStorageLimit(*storageDir));

  QM_TRY(MOZ_TO_RESULT(LoadQuota()));

  mTemporaryStorageInitializedInternal = true;

  // If origin initialization is done lazily, then there's either no quota
  // information at this point (if the cache couldn't be used) or only
  // partial quota information (origins accessed in a previous session
  // require full initialization). Given that, the cleanup can't be done
  // at this point yet.
  // For the same reason, recording metrics like origins with zero usage can't
  // be done either.
  if (!QuotaPrefs::LazyOriginInitializationEnabled()) {
    CleanupTemporaryStorage();

    RecordTemporaryStorageMetrics();
  }

  return NS_OK;
}

nsresult QuotaManager::EnsureTemporaryStorageIsInitializedInternal() {
  AssertIsOnIOThread();
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);

  const auto innerFunc =
      [&](const auto& firstInitializationAttempt) -> nsresult {
    if (mTemporaryStorageInitializedInternal) {
      MOZ_ASSERT(firstInitializationAttempt.Recorded());
      return NS_OK;
    }

    // Glean SDK recommends using its own timing APIs where possible. In this
    // case, we use NowExcludingSuspendMs() directly to manually calculate a
    // duration that excludes suspend time. This is a valid exception because
    // our use case is sensitive to suspend, and we need full control over the
    // timing logic.

    const auto startExcludingSuspendMs = NowExcludingSuspendMs();

    QM_TRY(MOZ_TO_RESULT(InitializeTemporaryStorageInternal()));

    const auto endExcludingSuspendMs = NowExcludingSuspendMs();

    if (startExcludingSuspendMs && endExcludingSuspendMs) {
      const auto duration = TimeDuration::FromMilliseconds(
          *endExcludingSuspendMs - *startExcludingSuspendMs);

      glean::quotamanager_initialize_temporarystorage::
          total_time_excluding_suspend.AccumulateRawDuration(duration);
    }

    return NS_OK;
  };

  return ExecuteInitialization(
      Initialization::TemporaryStorage,
      "dom::quota::FirstInitializationAttempt::TemporaryStorage"_ns, innerFunc);
}

nsresult QuotaManager::InitializeFlushTimer() {
  // Re-enable dirty origin tracking. RemoveQuota (called during
  // ShutdownStorageInternal and also within LoadQuota when the cache is
  // unusable) disables it; now that initialization is complete and the
  // timer is about to start, new dirty registrations should be accepted.
  mUsageModificationDisabled.store(false);

  const uint32_t flushInterval = std::max(
      100u, StaticPrefs::dom_quotaManager_originMetadataFlushCheckIntervalMs());
  const auto originInfoFlushInterval =
      TimeDuration::FromMilliseconds(flushInterval);

  QM_TRY(MOZ_TO_RESULT(NS_NewTimerWithCallback(
      getter_AddRefs(mFlushDirtyOriginInfosTimer),
      [](nsITimer*) {
        if (QuotaManager* qm = QuotaManager::Get()) {
          qm->FlushDirtyOriginInfos();
        }
      },
      std::ceil(originInfoFlushInterval.ToMilliseconds()),
      nsITimer::TYPE_REPEATING_SLACK_LOW_PRIORITY,
      "FlushDirtyOriginInfosTimer"_ns, mIOThread.ref())));

  return NS_OK;
}

void QuotaManager::UninitializeFlushTimer() {
  // Failed initialization does not set the timer.
  if (mFlushDirtyOriginInfosTimer) {
    QM_WARNONLY_TRY(MOZ_TO_RESULT(mFlushDirtyOriginInfosTimer->Cancel()));
    mFlushDirtyOriginInfosTimer = nullptr;
  }
}

RefPtr<BoolPromise> QuotaManager::InitializeAllTemporaryOrigins() {
  AssertIsOnOwningThread();

  if (mAllTemporaryOriginsInitialized) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }

  // Ensure the promise holder is initialized (a promise is created). This is
  // needed because processNextGroup can synchronously attempt to resolve the
  // promise. Without this, consumers may endlessly wait for an unresolved
  // promise, and in debug builds, an assertion failure may occur at shutdown,
  // indicating the promise exists but was never resolved or rejected.
  RefPtr<BoolPromise> promise =
      mInitializeAllTemporaryOriginsPromiseHolder.Ensure(__func__);

  if (!mInitializingAllTemporaryOrigins) {
    // TODO: make a copy of mUninitializedGroups and use it as the queue.

    mInitializingAllTemporaryOrigins = true;

    auto processNextGroup = [self = RefPtr(this)](
                                auto&& processNextGroupCallback) {
      auto backgroundThreadData = self->mBackgroundThreadAccessible.Access();

      if (backgroundThreadData->mUninitializedGroups.IsEmpty()) {
        self->mInitializingAllTemporaryOrigins = false;
        self->mAllTemporaryOriginsInitialized = true;

        self->mInitializeAllTemporaryOriginsPromiseHolder.ResolveIfExists(
            true, __func__);

        return;
      }

      if (NS_WARN_IF(IsShuttingDown())) {
        self->mInitializingAllTemporaryOrigins = false;

        self->mInitializeAllTemporaryOriginsPromiseHolder.RejectIfExists(
            NS_ERROR_ABORT, __func__);

        return;
      }

      auto principalMetadata =
          backgroundThreadData->mUninitializedGroups.PopLastElement();

      self->InitializeTemporaryGroup(principalMetadata)
          ->Then(GetCurrentSerialEventTarget(), __func__,
                 [processNextGroupCallback](
                     const BoolPromise::ResolveOrRejectValue& aValue) {
                   // TODO: Remove the group from mUninitializedGroups only
                   // when the group initialization succeeded.

                   processNextGroupCallback(processNextGroupCallback);
                 });
    };

    processNextGroup(/* processNextGroupCallback */ processNextGroup);
  }

  return promise;
}

nsresult QuotaManager::FlagOriginInfoAsDirtyOnDisk(
    DirtyTrackingAutoLock& aProofOfLock,
    const OriginStateMetadata& aStateMetadata) {
  AssertNotCurrentThreadOwnsQuotaMutex();

  struct SharedState {
    mozilla::Mutex mMutex;
    mozilla::CondVar mCondVar;
    nsresult mResult;
    std::atomic<bool> mDone;

    SharedState()
        : mMutex("FlagOriginInfoAsDirtyOnDisk"),
          mCondVar(mMutex, "FlagOriginInfoAsDirtyOnDisk::CondVar"),
          mResult(NS_OK),
          mDone(false) {}

    void Done(nsresult result) {
      MutexAutoLock lock(mMutex);
      mResult = result;
      mDone = true;
      mCondVar.Notify();
    }

    nsresult WaitForResult() {
      MutexAutoLock lock(mMutex);
      while (!mDone) {
        mCondVar.Wait();
      }

      return mResult;
    }
  };

  auto sharedState = std::make_shared<SharedState>();

  RefPtr<OriginInfo> originInfo = aProofOfLock.GetOriginInfo();
  uint32_t metadataFlags =
      aStateMetadata.ToMetadataFlags(originInfo->IsEncrypted());
  int64_t lastAccessTime = aStateMetadata.mLastAccessTime;
  nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
      "QuotaManager::FlagOriginInfoAsDirtyOnDisk",
      [sharedState, originInfo = std::move(originInfo), metadataFlags,
       lastAccessTime]() mutable {
        AssertIsOnIOThread();

        QuotaManager* self = QuotaManager::Get();
        if (!self) {
          sharedState->Done(NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
          return;
        }

        nsresult result = NS_OK;
        do {
          if (!self->IsStorageInitializedInternal() ||
              !self->IsTemporaryStorageInitializedInternal()) {
            QM_LOG(("FlagOriginInfoAsDirtyOnDisk: storage not initialized"));
            result = NS_ERROR_NOT_INITIALIZED;
            break;
          }

          result = originInfo->UpdateDirtyMetadata(
              self->mStorageConnection, metadataFlags, lastAccessTime);
        } while (false);

        sharedState->Done(result);
      });

  if (IsOnIOThread()) {
    // If we are already on the IO thread, run the task directly to avoid
    // deadlock.
    runnable->Run();
    return sharedState->WaitForResult();
  }

  nsresult rv = mIOThread->get()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  const auto result = sharedState->WaitForResult();
  return result;
}

void QuotaManager::RegisterDirtyOriginInfo(
    DirtyTrackingAutoLock& aProofOfLock) {
  MOZ_DIAGNOSTIC_ASSERT(aProofOfLock.IsValid());

  if (mUsageModificationDisabled.load()) {
    return;
  }

  auto originInfo = aProofOfLock.GetOriginInfo();

  if (originInfo->IsPrivate()) {
    return;
  }

  auto* message = new UnboundedMPSCQueue<RefPtr<OriginInfo>>::Message();
  message->data = std::move(originInfo);
  mDirtyOriginInfos.Push(message);
}

RefPtr<BoolPromise> QuotaManager::SaveOriginAccessTime(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  RefPtr<UniversalDirectoryLock> directoryLock =
      CreateSaveOriginAccessTimeLock(*this, aOriginMetadata);

  return directoryLock->Acquire()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this), aOriginMetadata,
       directoryLock](const BoolPromise::ResolveOrRejectValue& aValue) mutable {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        return self->SaveOriginAccessTime(aOriginMetadata,
                                          std::move(directoryLock));
      });
}

RefPtr<BoolPromise> QuotaManager::SaveOriginAccessTime(
    const OriginMetadata& aOriginMetadata,
    RefPtr<UniversalDirectoryLock> aDirectoryLock) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);
  MOZ_ASSERT(aDirectoryLock);
  MOZ_ASSERT(aDirectoryLock->Acquired());

  auto saveOriginAccessTimeOp =
      CreateSaveOriginAccessTimeOp(WrapMovingNotNullUnchecked(this),
                                   aOriginMetadata, std::move(aDirectoryLock));

  RegisterNormalOriginOp(*saveOriginAccessTimeOp);

  saveOriginAccessTimeOp->RunImmediately();

  return Map<BoolPromise>(
      saveOriginAccessTimeOp->OnResults(),
      [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue& aValue) {
        if (aValue.ResolveValue()) {
          self->IncreaseSaveOriginAccessTimeCount();
        }

        return aValue.ResolveValue();
      });
}

RefPtr<OriginUsageMetadataArrayPromise> QuotaManager::GetUsage(
    bool aGetAll, RefPtr<BoolPromise> aOnCancelPromise) {
  AssertIsOnOwningThread();

  auto getUsageOp = CreateGetUsageOp(WrapMovingNotNullUnchecked(this), aGetAll);

  RegisterNormalOriginOp(*getUsageOp);

  getUsageOp->RunImmediately();

  if (aOnCancelPromise) {
    RefPtr<BoolPromise> onCancelPromise = std::move(aOnCancelPromise);

    onCancelPromise->Then(
        GetCurrentSerialEventTarget(), __func__,
        [getUsageOp](const BoolPromise::ResolveOrRejectValue& aValue) {
          if (aValue.IsReject()) {
            return;
          }

          if (getUsageOp->Cancel()) {
            NS_WARNING("Canceled more than once?!");
          }
        });
  }

  return getUsageOp->OnResults();
}

RefPtr<UsageInfoPromise> QuotaManager::GetOriginUsage(
    const PrincipalInfo& aPrincipalInfo, RefPtr<BoolPromise> aOnCancelPromise) {
  AssertIsOnOwningThread();

  auto getOriginUsageOp =
      CreateGetOriginUsageOp(WrapMovingNotNullUnchecked(this), aPrincipalInfo);

  RegisterNormalOriginOp(*getOriginUsageOp);

  getOriginUsageOp->RunImmediately();

  if (aOnCancelPromise) {
    RefPtr<BoolPromise> onCancelPromise = std::move(aOnCancelPromise);

    onCancelPromise->Then(
        GetCurrentSerialEventTarget(), __func__,
        [getOriginUsageOp](const BoolPromise::ResolveOrRejectValue& aValue) {
          if (aValue.IsReject()) {
            return;
          }

          if (getOriginUsageOp->Cancel()) {
            NS_WARNING("Canceled more than once?!");
          }
        });
  }

  return getOriginUsageOp->OnResults();
}

RefPtr<UInt64Promise> QuotaManager::GetCachedOriginUsage(
    const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnOwningThread();

  auto getCachedOriginUsageOp = CreateGetCachedOriginUsageOp(
      WrapMovingNotNullUnchecked(this), aPrincipalInfo);

  RegisterNormalOriginOp(*getCachedOriginUsageOp);

  getCachedOriginUsageOp->RunImmediately();

  return getCachedOriginUsageOp->OnResults();
}

RefPtr<CStringArrayPromise> QuotaManager::ListOrigins() {
  AssertIsOnOwningThread();

  auto listOriginsOp = CreateListOriginsOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*listOriginsOp);

  listOriginsOp->RunImmediately();

  return listOriginsOp->OnResults();
}

RefPtr<CStringArrayPromise> QuotaManager::ListCachedOrigins() {
  AssertIsOnOwningThread();

  auto listCachedOriginsOp =
      CreateListCachedOriginsOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*listCachedOriginsOp);

  listCachedOriginsOp->RunImmediately();

  return listCachedOriginsOp->OnResults();
}

RefPtr<BoolPromise> QuotaManager::ClearStoragesForOrigin(
    const Maybe<PersistenceType>& aPersistenceType,
    const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnOwningThread();

  auto clearOriginOp = CreateClearOriginOp(WrapMovingNotNullUnchecked(this),
                                           aPersistenceType, aPrincipalInfo);

  RegisterNormalOriginOp(*clearOriginOp);

  clearOriginOp->RunImmediately();

  return Map<BoolPromise>(
      clearOriginOp->OnResults(),
      [self = RefPtr(this)](
          OriginMetadataArrayPromise::ResolveOrRejectValue&& aValue) {
        self->NoteUninitializedClients(aValue.ResolveValue());
        self->NoteUninitializedOrigins(aValue.ResolveValue());

        return true;
      });
}

RefPtr<BoolPromise> QuotaManager::ClearStoragesForClient(
    Maybe<PersistenceType> aPersistenceType,
    const PrincipalInfo& aPrincipalInfo, Client::Type aClientType) {
  AssertIsOnOwningThread();

  auto clearClientOp =
      CreateClearClientOp(WrapMovingNotNullUnchecked(this), aPersistenceType,
                          aPrincipalInfo, aClientType);

  RegisterNormalOriginOp(*clearClientOp);

  clearClientOp->RunImmediately();

  return Map<BoolPromise>(
      clearClientOp->OnResults(),
      [self = RefPtr(this)](
          ClientMetadataArrayPromise::ResolveOrRejectValue&& aValue) {
        self->NoteUninitializedClients(aValue.ResolveValue());
        return true;
      });
}

RefPtr<BoolPromise> QuotaManager::ClearStoragesForOriginPrefix(
    const Maybe<PersistenceType>& aPersistenceType,
    const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnOwningThread();

  auto clearStoragesForOriginPrefixOp = CreateClearStoragesForOriginPrefixOp(
      WrapMovingNotNullUnchecked(this), aPersistenceType, aPrincipalInfo);

  RegisterNormalOriginOp(*clearStoragesForOriginPrefixOp);

  clearStoragesForOriginPrefixOp->RunImmediately();

  return Map<BoolPromise>(
      clearStoragesForOriginPrefixOp->OnResults(),
      [self = RefPtr(this)](
          OriginMetadataArrayPromise::ResolveOrRejectValue&& aValue) {
        self->NoteUninitializedClients(aValue.ResolveValue());
        self->NoteUninitializedOrigins(aValue.ResolveValue());

        return true;
      });
}

RefPtr<BoolPromise> QuotaManager::ClearStoragesForOriginAttributesPattern(
    const OriginAttributesPattern& aPattern) {
  AssertIsOnOwningThread();

  auto clearDataOp =
      CreateClearDataOp(WrapMovingNotNullUnchecked(this), aPattern);

  RegisterNormalOriginOp(*clearDataOp);

  clearDataOp->RunImmediately();

  return Map<BoolPromise>(
      clearDataOp->OnResults(),
      [self = RefPtr(this)](
          OriginMetadataArrayPromise::ResolveOrRejectValue&& aValue) {
        self->NoteUninitializedClients(aValue.ResolveValue());
        self->NoteUninitializedOrigins(aValue.ResolveValue());

        return true;
      });
}

RefPtr<BoolPromise> QuotaManager::ClearPrivateRepository() {
  AssertIsOnOwningThread();

  auto clearPrivateRepositoryOp =
      CreateClearPrivateRepositoryOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*clearPrivateRepositoryOp);

  clearPrivateRepositoryOp->RunImmediately();

  return Map<BoolPromise>(
      clearPrivateRepositoryOp->OnResults(),
      [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue& aValue) {
        self->NoteUninitializedClients(PERSISTENCE_TYPE_PRIVATE);
        self->NoteUninitializedRepository(PERSISTENCE_TYPE_PRIVATE);

        return aValue.ResolveValue();
      });
}

RefPtr<BoolPromise> QuotaManager::ClearStorage() {
  AssertIsOnOwningThread();

  auto clearStorageOp = CreateClearStorageOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*clearStorageOp);

  clearStorageOp->RunImmediately();

  return clearStorageOp->OnResults()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        self->mInitializedClients.Clear();
        {
          MutexAutoLock lock(self->mInitializedOriginsMutex);
          self->mInitializedOrigins.Clear();
        }
        self->mBackgroundThreadAccessible.Access()->mInitializedGroups.Clear();
        self->mAllTemporaryOriginsInitialized = false;
        self->mTemporaryStorageInitialized = false;
        self->mStorageInitialized = false;

        return BoolPromise::CreateAndResolve(true, __func__);
      });
}

RefPtr<BoolPromise> QuotaManager::ShutdownStoragesForOrigin(
    Maybe<PersistenceType> aPersistenceType,
    const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnOwningThread();

  auto shutdownOriginOp = CreateShutdownOriginOp(
      WrapMovingNotNullUnchecked(this), aPersistenceType, aPrincipalInfo);

  RegisterNormalOriginOp(*shutdownOriginOp);

  shutdownOriginOp->RunImmediately();

  return Map<BoolPromise>(
      shutdownOriginOp->OnResults(),
      [self = RefPtr(this)](
          OriginMetadataArrayPromise::ResolveOrRejectValue&& aValue) {
        self->NoteUninitializedClients(aValue.ResolveValue());
        self->NoteUninitializedOrigins(aValue.ResolveValue());

        return true;
      });
}

RefPtr<BoolPromise> QuotaManager::ShutdownStoragesForClient(
    Maybe<PersistenceType> aPersistenceType,
    const PrincipalInfo& aPrincipalInfo, Client::Type aClientType) {
  AssertIsOnOwningThread();

  auto shutdownClientOp =
      CreateShutdownClientOp(WrapMovingNotNullUnchecked(this), aPersistenceType,
                             aPrincipalInfo, aClientType);

  RegisterNormalOriginOp(*shutdownClientOp);

  shutdownClientOp->RunImmediately();

  return Map<BoolPromise>(
      shutdownClientOp->OnResults(),
      [self = RefPtr(this)](
          ClientMetadataArrayPromise::ResolveOrRejectValue&& aValue) {
        self->NoteUninitializedClients(aValue.ResolveValue());

        return true;
      });
}

RefPtr<BoolPromise> QuotaManager::ShutdownStorage(
    Maybe<OriginOperationCallbackOptions> aCallbackOptions,
    Maybe<OriginOperationCallbacks&> aCallbacks) {
  AssertIsOnOwningThread();

  auto shutdownStorageOp =
      CreateShutdownStorageOp(WrapMovingNotNullUnchecked(this));

  RegisterNormalOriginOp(*shutdownStorageOp);

  shutdownStorageOp->RunImmediately();

  if (aCallbackOptions.isSome() && aCallbacks.isSome()) {
    aCallbacks.ref() = shutdownStorageOp->GetCallbacks(aCallbackOptions.ref());
  }

  return shutdownStorageOp->OnResults()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsReject()) {
          return BoolPromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        self->mInitializedClients.Clear();
        {
          MutexAutoLock lock(self->mInitializedOriginsMutex);
          self->mInitializedOrigins.Clear();
        }
        self->mBackgroundThreadAccessible.Access()->mInitializedGroups.Clear();
        self->mAllTemporaryOriginsInitialized = false;
        self->mTemporaryStorageInitialized = false;
        self->mStorageInitialized = false;

        return BoolPromise::CreateAndResolve(true, __func__);
      });
}

void QuotaManager::ShutdownStorageInternal() {
  AssertIsOnIOThread();

  if (mStorageConnection) {
    mInitializationInfo.ResetOriginInitializationInfos();
    mInitializedOriginsInternal.Clear();

    if (mTemporaryStorageInitializedInternal) {
      UnloadQuota();

      RemoveTemporaryOrigins();

      mTemporaryStorageInitializedInternal = false;
    }

    // Drop the (original-origin <-> uuid-based storage-origin) mappings used
    // for private-browsing origins.
    {
      MutexAutoLock lock(mQuotaMutex);
      mOriginToStorageOriginMap.Clear();
      mStorageOriginToOriginMap.Clear();
    }

    ReleaseIOThreadObjects();

    mOriginUpserter = nullptr;
    mStorageConnection = nullptr;
    mCacheUsable = false;
    mCacheRequiresFullScan = false;
  }

  mInitializationInfo.ResetFirstInitializationAttempts();
}

Result<bool, nsresult> QuotaManager::EnsureOriginDirectory(
    nsIFile& aDirectory) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aDirectory, Exists));

  if (!exists) {
    QM_TRY_INSPECT(
        const auto& leafName,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, aDirectory, GetLeafName)
            .map([](const auto& leafName) {
              return NS_ConvertUTF16toUTF8(leafName);
            }));

    QM_TRY(OkIf(IsSanitizedOriginValid(leafName)), Err(NS_ERROR_FAILURE),
           [](const auto&) {
             QM_WARNING(
                 "Preventing creation of a new origin directory which is not "
                 "supported by our origin parser or is obsolete!");
           });
  }

  QM_TRY_RETURN(EnsureDirectory(aDirectory));
}

nsresult QuotaManager::AboutToClearOrigins(
    const PersistenceScope& aPersistenceScope, const OriginScope& aOriginScope,
    const ClientStorageScope& aClientStorageScope) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aClientStorageScope.IsClient() || aClientStorageScope.IsNull());

  if (aClientStorageScope.IsNull()) {
    for (Client::Type type : AllClientTypes()) {
      QM_TRY(MOZ_TO_RESULT((*mClients)[type]->AboutToClearOrigins(
          aPersistenceScope, aOriginScope)));
    }
  } else {
    QM_TRY(MOZ_TO_RESULT(
        (*mClients)[aClientStorageScope.GetClientType()]->AboutToClearOrigins(
            aPersistenceScope, aOriginScope)));
  }

  return NS_OK;
}

void QuotaManager::OriginClearCompleted(
    const OriginMetadata& aOriginMetadata,
    const ClientStorageScope& aClientStorageScope) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aClientStorageScope.IsClient() || aClientStorageScope.IsNull());

  if (aClientStorageScope.IsNull()) {
    if (aOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
      mInitializedOriginsInternal.RemoveElement(aOriginMetadata.mOrigin);
    } else {
      RemoveTemporaryOrigin(aOriginMetadata);
    }

    for (Client::Type type : AllClientTypes()) {
      (*mClients)[type]->OnOriginClearCompleted(aOriginMetadata);
    }
  } else {
    (*mClients)[aClientStorageScope.GetClientType()]->OnOriginClearCompleted(
        aOriginMetadata);
  }
}

void QuotaManager::RepositoryClearCompleted(PersistenceType aPersistenceType) {
  AssertIsOnIOThread();

  if (aPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
    mInitializedOriginsInternal.Clear();
  } else {
    RemoveTemporaryOrigins(aPersistenceType);
  }

  for (Client::Type type : AllClientTypes()) {
    (*mClients)[type]->OnRepositoryClearCompleted(aPersistenceType);
  }
}

Client* QuotaManager::GetClient(Client::Type aClientType) {
  MOZ_ASSERT(aClientType >= Client::IDB);
  MOZ_ASSERT(aClientType < Client::TypeMax());

  return (*mClients)[aClientType];
}

const AutoTArray<Client::Type, Client::TYPE_MAX>&
QuotaManager::AllClientTypes() {
  if (CachedNextGenLocalStorageEnabled()) {
    return *mAllClientTypes;
  }
  return *mAllClientTypesExceptLS;
}

bool QuotaManager::IsThumbnailPrivateIdentityIdKnown() const {
  AssertIsOnIOThread();

  return mIOThreadAccessible.Access()->mThumbnailPrivateIdentityId.isSome();
}

uint32_t QuotaManager::GetThumbnailPrivateIdentityId() const {
  AssertIsOnIOThread();

  return mIOThreadAccessible.Access()->mThumbnailPrivateIdentityId.ref();
}

void QuotaManager::SetThumbnailPrivateIdentityId(
    uint32_t aThumbnailPrivateIdentityId) {
  AssertIsOnIOThread();

  auto ioThreadData = mIOThreadAccessible.Access();

  ioThreadData->mThumbnailPrivateIdentityId = Some(aThumbnailPrivateIdentityId);
  ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount = 0;

  for (auto iter = ioThreadData->mAllTemporaryOrigins.Iter(); !iter.Done();
       iter.Next()) {
    auto& array = iter.Data();

    for (const auto& originMetadata : array) {
      if (IsUserContextSuffix(originMetadata.mSuffix,
                              GetThumbnailPrivateIdentityId())) {
        AssertNoOverflow(
            ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount, 1);
        ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount++;
      }
    }
  }
}

/* static */
int64_t QuotaManager::GetGroupLimitForLimit(int64_t aLimit) {
  // To avoid one group evicting all the rest, limit the amount any one group
  // can use to 20% resp. a fifth. To prevent individual sites from using
  // exorbitant amounts of storage where there is a lot of free space, cap the
  // group limit to 10GB.
  const auto x = std::min<int64_t>(aLimit / 5, 10 GB);

  // In low-storage situations, make an exception (while not exceeding the total
  // storage limit).
  return std::min<int64_t>(aLimit, std::max<int64_t>(x, 10 MB));
}

int64_t QuotaManager::GetGroupLimit() const {
  return GetGroupLimitForLimit(mTemporaryStorageLimit);
}

Maybe<OriginStateMetadata> QuotaManager::GetOriginStateMetadata(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);
  MOZ_DIAGNOSTIC_ASSERT(mTemporaryStorageInitializedInternal);

  MutexAutoLock lock(mQuotaMutex);

  RefPtr<OriginInfo> originInfo =
      LockedGetOriginInfo(aOriginMetadata.mPersistenceType, aOriginMetadata);
  if (originInfo) {
    return Some(originInfo->LockedFlattenToOriginStateMetadata());
  }

  return Nothing();
}

std::pair<uint64_t, uint64_t> QuotaManager::GetUsageAndLimitForEstimate(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  int64_t originUsage = 0;
  bool persisted = false;

  {
    MutexAutoLock lock(mQuotaMutex);

    GroupInfoPair* pair;
    if (mGroupInfoPairs.Get(aOriginMetadata.mGroup, &pair)) {
      for (const PersistenceType type : kBestEffortPersistenceTypes) {
        RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(type);
        if (!groupInfo) {
          continue;
        }

        RefPtr<OriginInfo> originInfo =
            groupInfo->LockedGetOriginInfo(aOriginMetadata.mOrigin);
        if (!originInfo) {
          continue;
        }

        // The estimate covers everything the origin stores, so the usage of
        // all best-effort repositories is summed up, like GetOriginUsage does:
        // the private repository holds the origin's data in private browsing
        // and the temporary repository holds data stored with the "temporary"
        // persistence type. Only the default repository can be persisted, so
        // this also includes the non-persisted temporary usage of a persisted
        // origin (which used to report its default repository usage only).
        AssertNoOverflow(originUsage, originInfo->LockedUsage());
        originUsage += originInfo->LockedUsage();

        if (type == PERSISTENCE_TYPE_DEFAULT && originInfo->LockedPersisted()) {
          persisted = true;
        }
      }
    }
  }

  // The usage is the origin's own usage as required by
  // https://storage.spec.whatwg.org/#storage-usage, while the limit is still
  // tracked per group (bug 1305665). A persisted origin is exempt from
  // group-limit eviction and is bound by the global temporary storage limit
  // instead. Both values are exposed to content via
  // navigator.storage.estimate() so the usage is clamped to 0.
  return std::pair(QM_CLAMP_TO_ZERO(originUsage),
                   persisted ? mTemporaryStorageLimit : GetGroupLimit());
}

std::pair<uint64_t, uint64_t> QuotaManager::GetGroupUsageAndLimitForEstimate(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  int64_t groupUsage = 0;

  {
    MutexAutoLock lock(mQuotaMutex);

    GroupInfoPair* pair;
    if (mGroupInfoPairs.Get(aOriginMetadata.mGroup, &pair)) {
      for (const PersistenceType type : kBestEffortPersistenceTypes) {
        RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(type);
        if (!groupInfo) {
          continue;
        }

        if (type == PERSISTENCE_TYPE_DEFAULT) {
          RefPtr<OriginInfo> originInfo =
              groupInfo->LockedGetOriginInfo(aOriginMetadata.mOrigin);

          // A persisted origin is exempt from the group limit (its usage is
          // not part of the group usage) and is bound by the global temporary
          // storage limit instead, so it reports its own usage as the total
          // group usage and also against that the temporary storage limit.
          if (originInfo && originInfo->LockedPersisted()) {
            return std::pair(QM_CLAMP_TO_ZERO(originInfo->LockedUsage()),
                             mTemporaryStorageLimit);
          }
        }

        AssertNoOverflow(groupUsage, groupInfo->mUsage);
        groupUsage += groupInfo->mUsage;
      }
    }
  }

  // This is only handed out to the parent process (see
  // nsIQuotaManagerService::estimateGroupUsage).
  return std::pair(QM_CLAMP_TO_ZERO(groupUsage), GetGroupLimit());
}

uint64_t QuotaManager::GetOriginUsage(
    const PrincipalMetadata& aPrincipalMetadata) {
  AssertIsOnIOThread();

  int64_t usage = 0;

  {
    MutexAutoLock lock(mQuotaMutex);

    GroupInfoPair* pair;
    if (mGroupInfoPairs.Get(aPrincipalMetadata.mGroup, &pair)) {
      for (const PersistenceType type : kBestEffortPersistenceTypes) {
        RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(type);
        if (groupInfo) {
          RefPtr<OriginInfo> originInfo =
              groupInfo->LockedGetOriginInfo(aPrincipalMetadata.mOrigin);
          if (originInfo) {
            AssertNoOverflow(usage, originInfo->LockedUsage());
            usage += originInfo->LockedUsage();
          }
        }
      }
    }
  }

  // Exposed to callers outside the quota manager (e.g. via
  // GetCachedOriginUsageOp).
  return QM_CLAMP_TO_ZERO(usage);
}

Maybe<FullOriginMetadata> QuotaManager::GetFullOriginMetadata(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_DIAGNOSTIC_ASSERT(mStorageConnection);
  MOZ_DIAGNOSTIC_ASSERT(mTemporaryStorageInitializedInternal);

  MutexAutoLock lock(mQuotaMutex);

  RefPtr<OriginInfo> originInfo =
      LockedGetOriginInfo(aOriginMetadata.mPersistenceType, aOriginMetadata);
  if (originInfo) {
    return Some(originInfo->LockedFlattenToFullOriginMetadata());
  }

  return Nothing();
}

uint64_t QuotaManager::TotalDirectoryIterations() const {
  AssertIsOnIOThread();

  return mIOThreadAccessible.Access()->mTotalDirectoryIterations;
}

uint64_t QuotaManager::SaveOriginAccessTimeCount() const {
  AssertIsOnOwningThread();

  return mBackgroundThreadAccessible.Access()->mSaveOriginAccessTimeCount;
}

uint64_t QuotaManager::SaveOriginAccessTimeCountInternal() const {
  AssertIsOnIOThread();

  return mIOThreadAccessible.Access()->mSaveOriginAccessTimeCount;
}

// static
void QuotaManager::GetStorageId(PersistenceType aPersistenceType,
                                const nsACString& aOrigin,
                                Client::Type aClientType,
                                nsACString& aDatabaseId) {
  nsAutoCString str;
  str.AppendInt(aPersistenceType);
  str.Append('*');
  str.Append(aOrigin);
  str.Append('*');
  str.AppendInt(aClientType);

  aDatabaseId = str;
}

// static
bool QuotaManager::IsOriginInternal(const nsACString& aOrigin) {
  MOZ_ASSERT(!aOrigin.IsEmpty());

  // The first prompt is not required for these origins.
  if (aOrigin.EqualsLiteral(kChromeOrigin) ||
      StringBeginsWith(aOrigin, nsDependentCString(kIndexedDBOriginPrefix)) ||
      StringBeginsWith(aOrigin, nsDependentCString(kResourceOriginPrefix))) {
    return true;
  }

  return false;
}

// static
bool QuotaManager::AreOriginsEqualOnDisk(const nsACString& aOrigin1,
                                         const nsACString& aOrigin2) {
  return MakeSanitizedOriginCString(aOrigin1) ==
         MakeSanitizedOriginCString(aOrigin2);
}

// static
Result<PrincipalInfo, nsresult> QuotaManager::ParseOrigin(
    const nsACString& aOrigin) {
  // An origin string either corresponds to a SystemPrincipalInfo or a
  // ContentPrincipalInfo, see
  // QuotaManager::GetOriginFromValidatedPrincipalInfo.

  nsCString spec;
  OriginAttributes attrs;
  nsCString originalSuffix;
  const OriginParser::ResultType result = OriginParser::ParseOrigin(
      MakeSanitizedOriginCString(aOrigin), spec, &attrs, originalSuffix);
  QM_TRY(MOZ_TO_RESULT(result == OriginParser::ValidOrigin));

  QM_TRY_INSPECT(
      const auto& principal,
      ([&spec, &attrs]() -> Result<nsCOMPtr<nsIPrincipal>, nsresult> {
        if (spec.EqualsLiteral(kChromeOrigin)) {
          return nsCOMPtr<nsIPrincipal>(SystemPrincipal::Get());
        }

        nsCOMPtr<nsIURI> uri;
        QM_TRY(MOZ_TO_RESULT(NS_NewURI(getter_AddRefs(uri), spec)));

        return nsCOMPtr<nsIPrincipal>(
            BasePrincipal::CreateContentPrincipal(uri, attrs));
      }()));
  QM_TRY(MOZ_TO_RESULT(principal));

  PrincipalInfo principalInfo;
  QM_TRY(MOZ_TO_RESULT(PrincipalToPrincipalInfo(principal, &principalInfo)));

  return std::move(principalInfo);
}

// static
void QuotaManager::InvalidateQuotaCache(CacheInvalidationLevel aLevel) {
  switch (aLevel) {
    case CacheInvalidationLevel::None:
      MOZ_ASSERT_UNREACHABLE("Pass Soft or Hard");
      return;
    case CacheInvalidationLevel::Soft: {
      // Promote only from None — never downgrade Soft over Hard.
      CacheInvalidationLevel expected = CacheInvalidationLevel::None;
      gInvalidateQuotaCache.compareExchange(expected,
                                            CacheInvalidationLevel::Soft);
      return;
    }
    case CacheInvalidationLevel::Hard:
      // Hard always wins.
      gInvalidateQuotaCache = CacheInvalidationLevel::Hard;
      return;
  }
}

OriginMetadataArray QuotaManager::GetTemporaryOrigins(
    PersistenceType aPersistenceType) const {
  AssertIsOnIOThread();

  auto ioThreadData = mIOThreadAccessible.Access();

  OriginMetadataArray originMetadataArray;

  for (auto iter = ioThreadData->mAllTemporaryOrigins.ConstIter(); !iter.Done();
       iter.Next()) {
    const auto& array = iter.Data();
    MOZ_ASSERT(!array.IsEmpty());

    for (const auto& fullOriginMetadata : array) {
      if (fullOriginMetadata.mPersistenceType == aPersistenceType) {
        originMetadataArray.AppendElement(fullOriginMetadata);
      }
    }
  }

  return originMetadataArray;
}

uint64_t QuotaManager::LockedCollectOriginsForEviction(
    uint64_t aMinSizeToBeFreed, nsTArray<RefPtr<OriginDirectoryLock>>& aLocks)
    MOZ_REQUIRES(mQuotaMutex) {
  mQuotaMutex.AssertCurrentThreadOwns();

  RefPtr<CollectOriginsHelper> helper =
      new CollectOriginsHelper(mQuotaMutex, aMinSizeToBeFreed);

  // Unlock while calling out to XPCOM (code behind the dispatch method needs
  // to acquire its own lock which can potentially lead to a deadlock and it
  // also calls an observer that can do various stuff like IO, so it's better
  // to not hold our mutex while that happens).
  {
    MutexAutoUnlock autoUnlock(mQuotaMutex);

    MOZ_ALWAYS_SUCCEEDS(mOwningThread->Dispatch(helper, NS_DISPATCH_NORMAL));
  }

  return helper->BlockAndReturnOriginsForEviction(aLocks);
}

void QuotaManager::LockedMaybeRecordTemporaryStorageUsageUnderflow(
    StaticString aCallSite) {
  mQuotaMutex.AssertCurrentThreadOwns();

  if (mTemporaryStorageUsage >= 0) {
    return;
  }

  glean::quotamanager::had_temporary_storage_usage_underflow.Set(true);

  if (!ThrottleCounter(mTemporaryStorageUsageUnderflowCount)) {
    return;
  }

  glean::quotamanager::TemporaryStorageUsageUnderflowExtra extra;
  extra.callSite.emplace(aCallSite);
  extra.magnitude.emplace(FloorLog2(Abs(mTemporaryStorageUsage)));
  glean::quotamanager::temporary_storage_usage_underflow.Record(Some(extra));
}

void QuotaManager::LockedRemoveQuotaForRepository(
    PersistenceType aPersistenceType) {
  mQuotaMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(aPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  for (auto iter = mGroupInfoPairs.Iter(); !iter.Done(); iter.Next()) {
    auto& pair = iter.Data();

    if (RefPtr<GroupInfo> groupInfo =
            pair->LockedGetGroupInfo(aPersistenceType)) {
      groupInfo->LockedRemoveOriginInfos();

      pair->LockedClearGroupInfo(aPersistenceType);

      if (!pair->LockedHasGroupInfos()) {
        iter.Remove();
      }
    }
  }
}

void QuotaManager::LockedRemoveQuotaForOrigin(
    const OriginMetadata& aOriginMetadata) {
  mQuotaMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  GroupInfoPair* pair;
  if (!mGroupInfoPairs.Get(aOriginMetadata.mGroup, &pair)) {
    return;
  }

  MOZ_ASSERT(pair);

  if (RefPtr<GroupInfo> groupInfo =
          pair->LockedGetGroupInfo(aOriginMetadata.mPersistenceType)) {
    groupInfo->LockedRemoveOriginInfo(aOriginMetadata.mOrigin);

    if (!groupInfo->LockedHasOriginInfos()) {
      pair->LockedClearGroupInfo(aOriginMetadata.mPersistenceType);

      if (!pair->LockedHasGroupInfos()) {
        mGroupInfoPairs.Remove(aOriginMetadata.mGroup);
      }
    }
  }
}

bool QuotaManager::LockedHasGroupInfoPair(const nsACString& aGroup) const {
  mQuotaMutex.AssertCurrentThreadOwns();

  return mGroupInfoPairs.Get(aGroup, nullptr);
}

already_AddRefed<GroupInfo> QuotaManager::LockedGetOrCreateGroupInfo(
    PersistenceType aPersistenceType, const nsACString& aSuffix,
    const nsACString& aGroup) {
  mQuotaMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(aPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  GroupInfoPair* const pair =
      mGroupInfoPairs.GetOrInsertNew(aGroup, aSuffix, aGroup);

  RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(aPersistenceType);
  if (!groupInfo) {
    groupInfo = new GroupInfo(pair, aPersistenceType);
    pair->LockedSetGroupInfo(aPersistenceType, groupInfo);
  }

  return groupInfo.forget();
}

already_AddRefed<OriginInfo> QuotaManager::LockedGetOriginInfo(
    PersistenceType aPersistenceType,
    const OriginMetadata& aOriginMetadata) const {
  mQuotaMutex.AssertCurrentThreadOwns();
  MOZ_ASSERT(aPersistenceType != PERSISTENCE_TYPE_PERSISTENT);

  GroupInfoPair* pair;
  if (mGroupInfoPairs.Get(aOriginMetadata.mGroup, &pair)) {
    RefPtr<GroupInfo> groupInfo = pair->LockedGetGroupInfo(aPersistenceType);
    if (groupInfo) {
      return groupInfo->LockedGetOriginInfo(aOriginMetadata.mOrigin);
    }
  }

  return nullptr;
}

template <typename Collect, typename Pred>
QuotaManager::OriginInfosFlatTraversable
QuotaManager::CollectLRUOriginInfosUntil(Collect&& aCollect, Pred&& aPred) {
  OriginInfosFlatTraversable originInfos;

  std::forward<Collect>(aCollect)(MakeBackInserter(originInfos));

  originInfos.Sort(OriginInfoAccessTimeComparator());

  const auto foundIt = std::find_if(originInfos.cbegin(), originInfos.cend(),
                                    std::forward<Pred>(aPred));

  originInfos.TruncateLength(foundIt - originInfos.cbegin());

  return originInfos;
}

QuotaManager::OriginInfosNestedTraversable
QuotaManager::GetOriginInfosExceedingGroupLimit() const {
  MutexAutoLock lock(mQuotaMutex);

  OriginInfosNestedTraversable originInfos;

  for (const auto& entry : mGroupInfoPairs) {
    const auto& pair = entry.GetData();

    MOZ_ASSERT(!entry.GetKey().IsEmpty());
    MOZ_ASSERT(pair);

    int64_t groupUsage = 0;

    const RefPtr<GroupInfo> temporaryGroupInfo =
        pair->LockedGetGroupInfo(PERSISTENCE_TYPE_TEMPORARY);
    if (temporaryGroupInfo) {
      groupUsage += temporaryGroupInfo->mUsage;
    }

    const RefPtr<GroupInfo> defaultGroupInfo =
        pair->LockedGetGroupInfo(PERSISTENCE_TYPE_DEFAULT);
    if (defaultGroupInfo) {
      groupUsage += defaultGroupInfo->mUsage;
    }

    const RefPtr<GroupInfo> privateGroupInfo =
        pair->LockedGetGroupInfo(PERSISTENCE_TYPE_PRIVATE);
    if (privateGroupInfo) {
      groupUsage += privateGroupInfo->mUsage;
    }

    if (groupUsage > 0) {
      QuotaManager* quotaManager = QuotaManager::Get();
      MOZ_ASSERT(quotaManager, "Shouldn't be null!");

      if (groupUsage > quotaManager->GetGroupLimit()) {
        originInfos.AppendElement(CollectLRUOriginInfosUntil(
            [&pair](auto inserter) {
              pair->MaybeInsertNonPersistedOriginInfos(std::move(inserter));
            },
            [&groupUsage, quotaManager](const auto& originInfo) {
              groupUsage -= originInfo->LockedUsage();

              return groupUsage <= quotaManager->GetGroupLimit();
            }));
      }
    }
  }

  return originInfos;
}

// Uses the same return type as the methods above, so the result can
// eventually be passed to ClearOrigins to easily clear such origins.
QuotaManager::OriginInfosNestedTraversable
QuotaManager::GetOriginInfosExceedingGlobalLimit() const {
  MutexAutoLock lock(mQuotaMutex);

  QuotaManager::OriginInfosNestedTraversable res;
  res.AppendElement(CollectLRUOriginInfosUntil(
      // XXX The lambda only needs to capture this, but due to Bug 1421435 it
      // can't.
      [&](auto inserter) {
        for (const auto& entry : mGroupInfoPairs) {
          const auto& pair = entry.GetData();

          MOZ_ASSERT(!entry.GetKey().IsEmpty());
          MOZ_ASSERT(pair);

          pair->MaybeInsertNonPersistedOriginInfos(inserter);
        }
      },
      [temporaryStorageUsage = mTemporaryStorageUsage,
       temporaryStorageLimit = mTemporaryStorageLimit,
       doomedUsage = int64_t{0}](const auto& originInfo) mutable {
        if (temporaryStorageUsage - doomedUsage <= temporaryStorageLimit) {
          return true;
        }

        doomedUsage += originInfo->LockedUsage();
        return false;
      }));

  return res;
}

QuotaManager::OriginInfosNestedTraversable
QuotaManager::GetOriginInfosWithZeroUsage(
    const Maybe<int64_t>& aCutoffAccessTime) const {
  MutexAutoLock lock(mQuotaMutex);

  QuotaManager::OriginInfosNestedTraversable res;

  OriginInfosFlatTraversable originInfos;

  auto inserter = MakeBackInserter(originInfos);

  for (const auto& entry : mGroupInfoPairs) {
    const auto& pair = entry.GetData();

    MOZ_ASSERT(!entry.GetKey().IsEmpty());
    MOZ_ASSERT(pair);

    pair->MaybeInsertNonPersistedZeroUsageOriginInfos(inserter,
                                                      aCutoffAccessTime);
  }

  res.AppendElement(std::move(originInfos));

  return res;
}

// Based on discussions in
// https://stackoverflow.com/questions/41847525/should-templated-functions-take-lambda-arguments-by-value-or-by-rvalue-reference
// and
// https://stackoverflow.com/questions/56988299/should-i-pass-a-lambda-by-const-reference,
// passing a callable by universal (forwarding) reference is considered a more
// flexible choice from an interface point of view.
//
// In future, we should also ensure this pattern is compatible with clang-tidy
// checks such as modernize-pass-by-value and consider enabling
// cppcoreguidelines-missing-std-forward to catch potential oversights.
template <typename Checker>
void QuotaManager::ClearOrigins(
    const OriginInfosNestedTraversable& aDoomedOriginInfos, Checker&& aChecker,
    const Maybe<size_t>& aMaxOriginsToClear) {
  AssertIsOnIOThread();

  if (QuotaManager::IsShuttingDown()) {
    // Don't block shutdown.
    return;
  }

  auto doomedOriginInfos =
      Flatten<OriginInfosFlatTraversable::value_type>(aDoomedOriginInfos);

  // If there's nothing to do, exit early.
  if (doomedOriginInfos.begin() == doomedOriginInfos.end()) {
    return;
  }

  // It's safer to take ownership of the checker and keep it outside the loop
  // (the checker can have side effects or use move).
  std::decay_t<Checker> checker = std::forward<Checker>(aChecker);

  // If we are in shutdown, we could break off early from clearing origins.
  // In such cases, we would like to track the ones that were already cleared
  // up, such that other essential cleanup could be performed on clearedOrigins.
  // clearedOrigins is used in calls to LockedRemoveQuotaForOrigin and
  // OriginClearCompleted below. We could have used a collection of OriginInfos
  // rather than flattening them to OriginMetadata but groupInfo in OriginInfo
  // is just a raw ptr and LockedRemoveQuotaForOrigin might delete groupInfo and
  // as a result, we would not be able to get origin persistence type required
  // in OriginClearCompleted call after lockedRemoveQuotaForOrigin call.
  nsTArray<OriginMetadata> clearedOrigins;

  // XXX Does this need to be done a) in order and/or b) sequentially?
  for (const auto& doomedOriginInfo : doomedOriginInfos) {
    std::invoke(checker, doomedOriginInfo);

    //  TODO: We are currently only checking for this flag here which
    //  means that we cannot break off once we start cleaning an origin. It
    //  could be better if we could check for shutdown flag while cleaning an
    //  origin such that we could break off early from the cleaning process if
    //  we are stuck cleaning on one huge origin. Bug1797098 has been filed to
    //  track this.
    if (QuotaManager::IsShuttingDown()) {
      break;
    }

    auto originMetadata = doomedOriginInfo->FlattenToOriginMetadata();

    DeleteOriginDirectory(originMetadata);

    clearedOrigins.AppendElement(std::move(originMetadata));

    if (aMaxOriginsToClear && clearedOrigins.Length() >= *aMaxOriginsToClear) {
      break;
    }
  }

  {
    MutexAutoLock lock(mQuotaMutex);

    for (const auto& clearedOrigin : clearedOrigins) {
      LockedRemoveQuotaForOrigin(clearedOrigin);
    }
  }

  for (const auto& clearedOrigin : clearedOrigins) {
    OriginClearCompleted(clearedOrigin, ClientStorageScope::CreateFromNull());
    RemoveOriginFromCacheForEviction(clearedOrigin);
  }
}

void QuotaManager::CleanupTemporaryStorage() {
  AssertIsOnIOThread();

  if (StaticPrefs::
          dom_quotaManager_temporaryStorage_clearNonPersistedZeroUsageOrigins()) {
    // XXX Ideally the clearing would be done asynchronously by storage
    // maintenance service once available.

    // We hardcode a 7-day cutoff for "recently used" origins. Even if such
    // origins have zero usage, skipping them avoids the performance penalty
    // of repeatedly recreating origin directories and metadata files. The
    // value is fixed for now to keep things simple, but could be made
    // configurable in the future if needed.
    static_assert(aDefaultCutoffAccessTime == kSecPerWeek * PR_USEC_PER_SEC);

    // Calculate cutoff time (one week ago). PR_Now() returns microseconds
    // since epoch, so this cannot realistically overflow.
    const int64_t cutoffTime = PR_Now() - aDefaultCutoffAccessTime;

#ifdef DEBUG
    // Verify that origins being cleared meet our criteria:
    // non-persisted, zero usage, and outside cutoff window
    auto checker = [&self = *this, cutoffTime](const auto& doomedOriginInfo) {
      MutexAutoLock lock(self.mQuotaMutex);
      MOZ_ASSERT(!doomedOriginInfo->LockedPersisted());
      MOZ_ASSERT(doomedOriginInfo->LockedUsage() == 0);
      MOZ_ASSERT(doomedOriginInfo->LockedAccessTime() < cutoffTime);
    };
#else
    auto checker = [](const auto&) {};
#endif

    const size_t maxOriginsToClear = StaticPrefs::
        dom_quotaManager_temporaryStorage_maxOriginsToClearDuringCleanup();

    ClearOrigins(GetOriginInfosWithZeroUsage(Some(cutoffTime)),
                 std::move(checker), Some(maxOriginsToClear));
  }

  // Evicting origins that exceed their group limit also affects the global
  // temporary storage usage, so these steps have to be taken sequentially.
  // Combining them doesn't seem worth the added complexity.

#ifdef DEBUG
  auto nonPersistedChecker = [&self = *this](const auto& doomedOriginInfo) {
    MutexAutoLock lock(self.mQuotaMutex);
    MOZ_ASSERT(!doomedOriginInfo->LockedPersisted());
  };
#else
  auto nonPersistedChecker = [](const auto&) {};
#endif

  ClearOrigins(GetOriginInfosExceedingGroupLimit(), nonPersistedChecker);
  ClearOrigins(GetOriginInfosExceedingGlobalLimit(),
               std::move(nonPersistedChecker));

  if (mTemporaryStorageUsage > mTemporaryStorageLimit) {
    // If disk space is still low after origin clear, notify storage pressure.
    NotifyStoragePressure(*this, mTemporaryStorageUsage);
  }
}

void QuotaManager::RecordTemporaryStorageMetrics() {
  AssertIsOnIOThread();

  {
    const auto originInfos = GetOriginInfosWithZeroUsage();

    auto range = Flatten<OriginInfosFlatTraversable::value_type>(originInfos);

    size_t count = std::distance(range.begin(), range.end());

    glean::quotamanager_initialize_temporarystorage::
        non_persisted_zero_usage_origins.AccumulateSingleSample(count);
  }
}

void QuotaManager::DeleteOriginDirectory(
    const OriginMetadata& aOriginMetadata) {
  QM_TRY_INSPECT(const auto& directory, GetOriginDirectory(aOriginMetadata),
                 QM_VOID);

  nsresult rv = directory->Remove(true);
  if (rv != NS_ERROR_FILE_NOT_FOUND && NS_FAILED(rv)) {
    // This should never fail if we've closed all storage connections
    // correctly...
    NS_ERROR("Failed to remove directory!");
  }

  if (mCipherKeyStore) {
    QM_WARNONLY_TRY(
        MOZ_TO_RESULT(mCipherKeyStore->Remove(aOriginMetadata.mOrigin)));
  }
}

void QuotaManager::FinalizeOriginEviction(
    nsTArray<RefPtr<OriginDirectoryLock>>&& aLocks) {
  NS_ASSERTION(!NS_IsMainThread(), "Wrong thread!");

  auto finalizeOriginEviction = [locks = std::move(aLocks)]() mutable {
    QuotaManager* quotaManager = QuotaManager::Get();
    MOZ_ASSERT(quotaManager);

    RefPtr<OriginOperationBase> op = CreateFinalizeOriginEvictionOp(
        WrapMovingNotNull(quotaManager), std::move(locks));

    op->RunImmediately();
  };

  if (IsOnBackgroundThread()) {
    finalizeOriginEviction();
  } else {
    MOZ_ALWAYS_SUCCEEDS(mOwningThread->Dispatch(
        NS_NewRunnableFunction(
            "dom::quota::QuotaManager::FinalizeOriginEviction",
            std::move(finalizeOriginEviction)),
        NS_DISPATCH_NORMAL));
  }
}

Result<Ok, nsresult> QuotaManager::ArchiveOrigins(
    const nsTArray<FullOriginMetadata>& aFullOriginMetadatas) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aFullOriginMetadatas.IsEmpty());

  QM_TRY_INSPECT(const auto& storageArchivesDir,
                 QM_NewLocalFile(*mStorageArchivesPath));

  // Create another subdir, so once we decide to remove all temporary archives,
  // we can remove only the subdir and the parent directory can still be used
  // for something else or similar in future. Otherwise, we would have to
  // figure out a new name for it.
  QM_TRY(MOZ_TO_RESULT(storageArchivesDir->Append(u"0"_ns)));

  PRExplodedTime now;
  PR_ExplodeTime(PR_Now(), PR_LocalTimeParameters, &now);

  const auto dateStr =
      nsPrintfCString("%04hd-%02" PRId32 "-%02" PRId32, now.tm_year,
                      now.tm_month + 1, now.tm_mday);

  QM_TRY_INSPECT(
      const auto& storageArchiveDir,
      CloneFileAndAppend(*storageArchivesDir, NS_ConvertASCIItoUTF16(dateStr)));

  QM_TRY(MOZ_TO_RESULT(
      storageArchiveDir->CreateUnique(nsIFile::DIRECTORY_TYPE, 0700)));

  QM_TRY_INSPECT(const auto& defaultStorageArchiveDir,
                 CloneFileAndAppend(*storageArchiveDir,
                                    nsLiteralString(DEFAULT_DIRECTORY_NAME)));

  QM_TRY_INSPECT(const auto& temporaryStorageArchiveDir,
                 CloneFileAndAppend(*storageArchiveDir,
                                    nsLiteralString(TEMPORARY_DIRECTORY_NAME)));

  for (const auto& fullOriginMetadata : aFullOriginMetadatas) {
    MOZ_ASSERT(
        IsBestEffortPersistenceType(fullOriginMetadata.mPersistenceType));

    QM_TRY_INSPECT(const auto& directory,
                   GetOriginDirectory(fullOriginMetadata));

    // The origin could have been removed, for example due to corruption.
    QM_TRY_INSPECT(
        const auto& moved,
        QM_OR_ELSE_WARN_IF(
            // Expression.
            MOZ_TO_RESULT(
                directory->MoveTo(fullOriginMetadata.mPersistenceType ==
                                          PERSISTENCE_TYPE_DEFAULT
                                      ? defaultStorageArchiveDir
                                      : temporaryStorageArchiveDir,
                                  u""_ns))
                .map([](Ok) { return true; }),
            // Predicate.
            ([](const nsresult rv) { return rv == NS_ERROR_FILE_NOT_FOUND; }),
            // Fallback.
            ErrToOk<false>));

    if (moved) {
      RemoveQuotaForOrigin(fullOriginMetadata.mPersistenceType,
                           fullOriginMetadata);

      RemoveTemporaryOrigin(fullOriginMetadata);
    }
  }

  return Ok{};
}

bool QuotaManager::IsSanitizedOriginValid(const nsACString& aSanitizedOrigin) {
  AssertIsOnIOThread();

  // Do not parse this sanitized origin string, if we already parsed it.
  return mValidOrigins.LookupOrInsertWith(
      aSanitizedOrigin, [&aSanitizedOrigin] {
        nsCString spec;
        OriginAttributes attrs;
        nsCString originalSuffix;
        const auto result = OriginParser::ParseOrigin(aSanitizedOrigin, spec,
                                                      &attrs, originalSuffix);

        return result == OriginParser::ValidOrigin;
      });
}

void QuotaManager::AddTemporaryOrigin(
    const FullOriginMetadata& aFullOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_ASSERT(IsBestEffortPersistenceType(aFullOriginMetadata.mPersistenceType));

  auto ioThreadData = mIOThreadAccessible.Access();

  auto& array = ioThreadData->mAllTemporaryOrigins.LookupOrInsert(
      aFullOriginMetadata.mGroup);

  DebugOnly<bool> containsOrigin = array.Contains(
      aFullOriginMetadata, [](const auto& aLeft, const auto& aRight) {
        if (aLeft.mPersistenceType == aRight.mPersistenceType) {
          return Compare(aLeft.mOrigin, aRight.mOrigin);
        }
        return aLeft.mPersistenceType > aRight.mPersistenceType ? 1 : -1;
      });
  MOZ_ASSERT(!containsOrigin);

  array.AppendElement(aFullOriginMetadata);

  if (IsThumbnailPrivateIdentityIdKnown() &&
      IsUserContextSuffix(aFullOriginMetadata.mSuffix,
                          GetThumbnailPrivateIdentityId())) {
    AssertNoOverflow(
        ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount, 1);
    ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount++;
  }
}

void QuotaManager::RemoveTemporaryOrigin(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
  MOZ_ASSERT(IsBestEffortPersistenceType(aOriginMetadata.mPersistenceType));

  auto ioThreadData = mIOThreadAccessible.Access();

  auto entry =
      ioThreadData->mAllTemporaryOrigins.Lookup(aOriginMetadata.mGroup);
  if (!entry) {
    return;
  }

  auto& array = *entry;

  DebugOnly<size_t> count =
      array.RemoveElementsBy([&aOriginMetadata](const auto& originMetadata) {
        return originMetadata.mPersistenceType ==
                   aOriginMetadata.mPersistenceType &&
               originMetadata.mOrigin == aOriginMetadata.mOrigin;
      });
  MOZ_ASSERT(count <= 1);

  if (array.IsEmpty()) {
    entry.Remove();
  }

  if (IsThumbnailPrivateIdentityIdKnown() &&
      IsUserContextSuffix(aOriginMetadata.mSuffix,
                          GetThumbnailPrivateIdentityId())) {
    AssertNoUnderflow(
        ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount, 1);
    ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount--;
  }
}

void QuotaManager::RemoveTemporaryOrigins(PersistenceType aPersistenceType) {
  AssertIsOnIOThread();
  MOZ_ASSERT(IsBestEffortPersistenceType(aPersistenceType));

  auto ioThreadData = mIOThreadAccessible.Access();

  for (auto iter = ioThreadData->mAllTemporaryOrigins.Iter(); !iter.Done();
       iter.Next()) {
    auto& array = iter.Data();

    uint32_t thumbnailPrivateIdentityTemporaryOriginCount = 0;

    array.RemoveElementsBy([this, aPersistenceType,
                            &thumbnailPrivateIdentityTemporaryOriginCount](
                               const auto& originMetadata) {
      const bool match = originMetadata.mPersistenceType == aPersistenceType;
      if (!match) {
        return false;
      }

      if (IsThumbnailPrivateIdentityIdKnown() &&
          IsUserContextSuffix(originMetadata.mSuffix,
                              GetThumbnailPrivateIdentityId())) {
        AssertNoOverflow(thumbnailPrivateIdentityTemporaryOriginCount, 1);
        thumbnailPrivateIdentityTemporaryOriginCount++;
      }

      return true;
    });

    if (array.IsEmpty()) {
      iter.Remove();
    }

    AssertNoUnderflow(
        ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount,
        thumbnailPrivateIdentityTemporaryOriginCount);
    ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount -=
        thumbnailPrivateIdentityTemporaryOriginCount;
  }
}

void QuotaManager::RemoveTemporaryOrigins() {
  AssertIsOnIOThread();

  auto ioThreadData = mIOThreadAccessible.Access();

  ioThreadData->mAllTemporaryOrigins.Clear();
  ioThreadData->mThumbnailPrivateIdentityTemporaryOriginCount = 0;
}

PrincipalMetadataArray QuotaManager::GetAllTemporaryGroups() const {
  AssertIsOnIOThread();

  auto ioThreadData = mIOThreadAccessible.Access();

  PrincipalMetadataArray principalMetadataArray;

  std::transform(ioThreadData->mAllTemporaryOrigins.Values().cbegin(),
                 ioThreadData->mAllTemporaryOrigins.Values().cend(),
                 MakeBackInserter(principalMetadataArray),
                 [](const auto& array) {
                   MOZ_ASSERT(!array.IsEmpty());

                   // All items in the array have the same PrincipalMetadata,
                   // so we can use any item to get it.
                   return array[0];
                 });

  return principalMetadataArray;
}

OriginMetadataArray QuotaManager::GetAllTemporaryOrigins() const {
  AssertIsOnIOThread();

  auto ioThreadData = mIOThreadAccessible.Access();

  OriginMetadataArray originMetadataArray;

  for (auto iter = ioThreadData->mAllTemporaryOrigins.ConstIter(); !iter.Done();
       iter.Next()) {
    const auto& array = iter.Data();

    for (const auto& originMetadata : array) {
      originMetadataArray.AppendElement(originMetadata);
    }
  }

  return originMetadataArray;
}

uint32_t QuotaManager::ThumbnailPrivateIdentityTemporaryOriginCount() const {
  AssertIsOnIOThread();
  MOZ_ASSERT(IsThumbnailPrivateIdentityIdKnown());

  return mIOThreadAccessible.Access()
      ->mThumbnailPrivateIdentityTemporaryOriginCount;
}

void QuotaManager::NoteInitializedOrigin(PersistenceType aPersistenceType,
                                         const nsACString& aOrigin) {
  MutexAutoLock lock(mInitializedOriginsMutex);

  auto& boolArray = mInitializedOrigins.LookupOrInsertWith(aOrigin, []() {
    BoolArray boolArray;
    boolArray.AppendElements(PERSISTENCE_TYPE_INVALID);
    std::fill(boolArray.begin(), boolArray.end(), false);
    return boolArray;
  });

  boolArray[aPersistenceType] = true;
}

void QuotaManager::NoteUninitializedOrigins(
    const OriginMetadataArray& aOriginMetadataArray) {
  MutexAutoLock lock(mInitializedOriginsMutex);

  for (const auto& originMetadata : aOriginMetadataArray) {
    auto entry = mInitializedOrigins.Lookup(originMetadata.mOrigin);
    if (!entry) {
      return;
    }

    auto& boolArray = *entry;

    if (boolArray[originMetadata.mPersistenceType]) {
      boolArray[originMetadata.mPersistenceType] = false;

      if (std::all_of(boolArray.begin(), boolArray.end(),
                      [](bool entry) { return !entry; })) {
        entry.Remove();
      }
    }
  }
}

void QuotaManager::NoteUninitializedRepository(
    PersistenceType aPersistenceType) {
  MutexAutoLock lock(mInitializedOriginsMutex);

  for (auto iter = mInitializedOrigins.Iter(); !iter.Done(); iter.Next()) {
    auto& boolArray = iter.Data();

    if (boolArray[aPersistenceType]) {
      boolArray[aPersistenceType] = false;

      if (std::all_of(boolArray.begin(), boolArray.end(),
                      [](bool entry) { return !entry; })) {
        iter.Remove();
      }
    }
  }
}

bool QuotaManager::IsOriginInitialized(PersistenceType aPersistenceType,
                                       const nsACString& aOrigin) const {
  MutexAutoLock lock(mInitializedOriginsMutex);

  const auto entry = mInitializedOrigins.Lookup(aOrigin);

  return entry && (*entry)[aPersistenceType];
}

void QuotaManager::NoteInitializedClient(PersistenceType aPersistenceType,
                                         const nsACString& aOrigin,
                                         Client::Type aClientType) {
  AssertIsOnOwningThread();

  auto& bitSetArray = mInitializedClients.LookupOrInsertWith(aOrigin, []() {
    BitSetArray bitSetArray;
    bitSetArray.AppendElements(PERSISTENCE_TYPE_INVALID);
    return bitSetArray;
  });

  bitSetArray[aPersistenceType][aClientType] = true;
}

void QuotaManager::NoteUninitializedClients(
    const ClientMetadataArray& aClientMetadataArray) {
  AssertIsOnOwningThread();

  for (const auto& clientMetadata : aClientMetadataArray) {
    auto entry = mInitializedClients.Lookup(clientMetadata.mOrigin);
    if (!entry) {
      return;
    }

    auto& bitSetArray = *entry;

    if (bitSetArray[clientMetadata.mPersistenceType]
                   [clientMetadata.mClientType]) {
      bitSetArray[clientMetadata.mPersistenceType][clientMetadata.mClientType] =
          false;

      if (std::all_of(bitSetArray.begin(), bitSetArray.end(),
                      [](const auto& entry) { return entry.IsEmpty(); })) {
        entry.Remove();
      }
    }
  }
}

void QuotaManager::NoteUninitializedClients(
    const OriginMetadataArray& aOriginMetadataArray) {
  AssertIsOnOwningThread();

  for (const auto& originMetadata : aOriginMetadataArray) {
    auto entry = mInitializedClients.Lookup(originMetadata.mOrigin);
    if (!entry) {
      return;
    }

    auto& bitSetArray = *entry;

    bitSetArray[originMetadata.mPersistenceType].ResetAll();

    if (std::all_of(bitSetArray.begin(), bitSetArray.end(),
                    [](const auto& entry) { return entry.IsEmpty(); })) {
      entry.Remove();
    }
  }
}

void QuotaManager::NoteUninitializedClients(PersistenceType aPersistenceType) {
  AssertIsOnOwningThread();

  for (auto iter = mInitializedClients.Iter(); !iter.Done(); iter.Next()) {
    auto& bitSetArray = iter.Data();

    bitSetArray[aPersistenceType].ResetAll();

    if (std::all_of(bitSetArray.begin(), bitSetArray.end(),
                    [](const auto& entry) { return entry.IsEmpty(); })) {
      iter.Remove();
    }
  }
}

bool QuotaManager::IsClientInitialized(PersistenceType aPersistenceType,
                                       const nsACString& aOrigin,
                                       Client::Type aClientType) const {
  AssertIsOnOwningThread();

  const auto entry = mInitializedClients.Lookup(aOrigin);

  return entry && (*entry)[aPersistenceType][aClientType];
}

Result<nsCString, nsresult> QuotaManager::EnsureStorageOriginFromOrigin(
    const nsACString& aOrigin) {
  MutexAutoLock lock(mQuotaMutex);

  QM_TRY_UNWRAP(
      auto storageOrigin,
      mOriginToStorageOriginMap.TryLookupOrInsertWith(
          aOrigin, [this, &aOrigin]() -> Result<nsCString, nsresult> {
            // Validate that the origin is well formed. Nothing of it is
            // carried over into the storage origin, which is entirely
            // synthesized below.
            OriginAttributes originAttributes;

            nsCString originNoSuffix;
            QM_TRY(MOZ_TO_RESULT(
                originAttributes.PopulateFromOrigin(aOrigin, originNoSuffix)));

            nsCOMPtr<nsIURI> uri;
            QM_TRY(MOZ_TO_RESULT(
                NS_NewURI(getter_AddRefs(uri),
                          kUUIDOriginScheme + "://"_ns +
                              NSID_TrimBracketsASCII(nsID::GenerateUUID()))));

            nsCOMPtr<nsIPrincipal> principal =
                BasePrincipal::CreateContentPrincipal(uri, OriginAttributes{});
            QM_TRY(MOZ_TO_RESULT(principal));

            QM_TRY_UNWRAP(auto origin,
                          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                              nsAutoCString, principal, GetOrigin));

            mStorageOriginToOriginMap.WithEntryHandle(
                origin,
                [&aOrigin](auto entryHandle) { entryHandle.Insert(aOrigin); });

            return nsCString(std::move(origin));
          }));

  return nsCString(std::move(storageOrigin));
}

Result<nsCString, nsresult> QuotaManager::GetOriginFromStorageOrigin(
    const nsACString& aStorageOrigin) {
  MutexAutoLock lock(mQuotaMutex);

  auto maybeOrigin = mStorageOriginToOriginMap.MaybeGet(aStorageOrigin);
  if (maybeOrigin.isNothing()) {
    return Err(NS_ERROR_FAILURE);
  }

  return maybeOrigin.ref();
}

int64_t QuotaManager::GenerateDirectoryLockId() {
  const int64_t directorylockId = mNextDirectoryLockId;

  if (CheckedInt64 result = CheckedInt64(mNextDirectoryLockId) + 1;
      result.isValid()) {
    mNextDirectoryLockId = result.value();
  } else {
    NS_WARNING("Quota manager has run out of ids for directory locks!");

    // There's very little chance for this to happen given the max size of
    // 64 bit integer but if it happens we can just reset mNextDirectoryLockId
    // to zero since such old directory locks shouldn't exist anymore.
    mNextDirectoryLockId = 0;
  }

  // TODO: Maybe add an assertion here to check that there is no existing
  //       directory lock with given id.

  return directorylockId;
}

template <typename UpdateCallback>
void QuotaManager::RegisterClientDirectoryLockHandle(
    const OriginMetadata& aOriginMetadata, UpdateCallback&& aUpdateCallback) {
  AssertIsOnOwningThread();

  auto backgroundThreadData = mBackgroundThreadAccessible.Access();

  auto& openClientDirectoryInfo =
      backgroundThreadData->mOpenClientDirectoryInfos.LookupOrInsert(
          aOriginMetadata.GetCompositeKey());

  openClientDirectoryInfo.IncreaseClientDirectoryLockHandleCount();

  bool firstHandle =
      (openClientDirectoryInfo.ClientDirectoryLockHandleCount() == 1);

  if (firstHandle) {
    std::forward<UpdateCallback>(aUpdateCallback)(openClientDirectoryInfo);
  }
}

template <typename Callback>
auto QuotaManager::WithOpenClientDirectoryInfo(
    const OriginMetadata& aOriginMetadata, Callback&& aCallback)
    -> std::invoke_result_t<Callback, OpenClientDirectoryInfo&> {
  AssertIsOnOwningThread();

  auto backgroundThreadData = mBackgroundThreadAccessible.Access();

  auto entry = backgroundThreadData->mOpenClientDirectoryInfos.Lookup(
      aOriginMetadata.GetCompositeKey());
  MOZ_ASSERT(entry);

  auto& openClientDirectoryInfo = entry.Data();

  return std::forward<Callback>(aCallback)(openClientDirectoryInfo);
}

template <typename UpdateCallback>
void QuotaManager::UnregisterClientDirectoryLockHandle(
    const OriginMetadata& aOriginMetadata, UpdateCallback&& aUpdateCallback) {
  AssertIsOnOwningThread();

  auto backgroundThreadData = mBackgroundThreadAccessible.Access();

  auto entry = backgroundThreadData->mOpenClientDirectoryInfos.Lookup(
      aOriginMetadata.GetCompositeKey());
  MOZ_ASSERT(entry);

  auto& openClientDirectoryInfo = entry.Data();

  openClientDirectoryInfo.DecreaseClientDirectoryLockHandleCount();

  bool lastHandle =
      openClientDirectoryInfo.ClientDirectoryLockHandleCount() == 0;

  if (lastHandle) {
    std::forward<UpdateCallback>(aUpdateCallback)(openClientDirectoryInfo);

    entry.Remove();
  }
}

void QuotaManager::ClientDirectoryLockHandleDestroy(
    ClientDirectoryLockHandle& aHandle) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aHandle);

  if (!aHandle.IsRegistered()) {
    return;
  }

  const OriginMetadata originMetadata = aHandle->OriginMetadata();

  UnregisterClientDirectoryLockHandle(
      originMetadata, [&self = *this, &originMetadata](
                          OpenClientDirectoryInfo& aOpenClientDirectoryInfo) {
        if (originMetadata.mPersistenceType != PERSISTENCE_TYPE_PERSISTENT &&
            aOpenClientDirectoryInfo.HasLastAccessDirectoryLock()) {
          self.SaveOriginAccessTime(
              originMetadata,
              aOpenClientDirectoryInfo.ForgetLastAccessDirectoryLock());
        }
      });

  aHandle.SetRegistered(false);
}

template <typename Func>
auto QuotaManager::ExecuteInitialization(const Initialization aInitialization,
                                         Func&& aFunc)
    -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                      Initialization, StringGenerator>&> {
  return quota::ExecuteInitialization(mInitializationInfo, aInitialization,
                                      std::forward<Func>(aFunc));
}

template <typename Func>
auto QuotaManager::ExecuteInitialization(const Initialization aInitialization,
                                         const nsACString& aContext,
                                         Func&& aFunc)
    -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                      Initialization, StringGenerator>&> {
  return quota::ExecuteInitialization(mInitializationInfo, aInitialization,
                                      aContext, std::forward<Func>(aFunc));
}

template <typename Func>
auto QuotaManager::ExecuteGroupInitialization(
    const nsACString& aGroup, const GroupInitialization aInitialization,
    const nsACString& aContext, Func&& aFunc)
    -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                      Initialization, StringGenerator>&> {
  return quota::ExecuteInitialization(
      mInitializationInfo.MutableGroupInitializationInfoRef(
          aGroup, CreateIfNonExistent{}),
      aInitialization, aContext, std::forward<Func>(aFunc));
}

template <typename Func>
auto QuotaManager::ExecuteOriginInitialization(
    const nsACString& aOrigin, const OriginInitialization aInitialization,
    const nsACString& aContext, Func&& aFunc)
    -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                      Initialization, StringGenerator>&> {
  return quota::ExecuteInitialization(
      mInitializationInfo.MutableOriginInitializationInfoRef(
          aOrigin, CreateIfNonExistent{}),
      aInitialization, aContext, std::forward<Func>(aFunc));
}

void QuotaManager::IncreaseTotalDirectoryIterations() {
  AssertIsOnIOThread();

  auto ioThreadData = mIOThreadAccessible.Access();

  AssertNoOverflow(ioThreadData->mTotalDirectoryIterations, 1);
  ioThreadData->mTotalDirectoryIterations++;
}

void QuotaManager::IncreaseSaveOriginAccessTimeCount() {
  AssertIsOnOwningThread();

  auto backgroundThreadData = mBackgroundThreadAccessible.Access();

  AssertNoOverflow(backgroundThreadData->mSaveOriginAccessTimeCount, 1);
  backgroundThreadData->mSaveOriginAccessTimeCount++;
}

void QuotaManager::IncreaseSaveOriginAccessTimeCountInternal() {
  AssertIsOnIOThread();

  auto ioThreadData = mIOThreadAccessible.Access();

  AssertNoOverflow(ioThreadData->mSaveOriginAccessTimeCount, 1);
  ioThreadData->mSaveOriginAccessTimeCount++;
}

/*******************************************************************************
 * Local class implementations
 ******************************************************************************/

CollectOriginsHelper::CollectOriginsHelper(mozilla::Mutex& aMutex,
                                           uint64_t aMinSizeToBeFreed)
    : Runnable("dom::quota::CollectOriginsHelper"),
      mMinSizeToBeFreed(aMinSizeToBeFreed),
      mMutex(aMutex),
      mCondVar(aMutex, "CollectOriginsHelper::mCondVar"),
      mSizeToBeFreed(0),
      mWaiting(true) {
  MOZ_ASSERT(!NS_IsMainThread(), "Wrong thread!");
  mMutex.AssertCurrentThreadOwns();
}

int64_t CollectOriginsHelper::BlockAndReturnOriginsForEviction(
    nsTArray<RefPtr<OriginDirectoryLock>>& aLocks) {
  MOZ_ASSERT(!NS_IsMainThread(), "Wrong thread!");
  mMutex.AssertCurrentThreadOwns();

  while (mWaiting) {
    mCondVar.Wait();
  }

  mLocks.SwapElements(aLocks);
  return mSizeToBeFreed;
}

NS_IMETHODIMP
CollectOriginsHelper::Run() {
  AssertIsOnBackgroundThread();

  QuotaManager* quotaManager = QuotaManager::Get();
  NS_ASSERTION(quotaManager, "Shouldn't be null!");

  // We use extra stack vars here to avoid race detector warnings (the same
  // memory accessed with and without the lock held).
  nsTArray<RefPtr<OriginDirectoryLock>> locks;
  uint64_t sizeToBeFreed =
      quotaManager->CollectOriginsForEviction(mMinSizeToBeFreed, locks);

  MutexAutoLock lock(mMutex);

  NS_ASSERTION(mWaiting, "Huh?!");

  mLocks.SwapElements(locks);
  mSizeToBeFreed = sizeToBeFreed;
  mWaiting = false;
  mCondVar.Notify();

  return NS_OK;
}

TimeStamp RecordTimeDeltaHelper::Start() {
  MOZ_ASSERT(IsOnIOThread() || IsOnBackgroundThread());

  // XXX: If a OS sleep/wake occur after mStartTime is initialized but before
  // gLastOSWake is set, then this time duration would still be recorded with
  // key "Normal". We are assumming this is rather rare to happen.
  mStartTime.init(TimeStamp::Now());
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(this));

  return *mStartTime;
}

TimeStamp RecordTimeDeltaHelper::End() {
  MOZ_ASSERT(IsOnIOThread() || IsOnBackgroundThread());

  mEndTime.init(TimeStamp::Now());
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(this));

  return *mEndTime;
}

NS_IMETHODIMP
RecordTimeDeltaHelper::Run() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mInitializedTime.isSome()) {
    // Labels for glean::dom_quota::info_load_time and
    // glean::dom_quota::shutdown_time:
    // Normal: Normal conditions.
    // WasSuspended: There was a OS sleep so that it was suspended.
    // TimeStampErr1: The recorded start time is unexpectedly greater than the
    //                end time.
    // TimeStampErr2: The initialized time for the recording class is unexpectly
    //                greater than the last OS wake time.
    const auto key = [this, wasSuspended = gLastOSWake > *mInitializedTime]() {
      if (wasSuspended) {
        return "WasSuspended"_ns;
      }

      // XXX File a bug if we have data for this key.
      // We found negative values in our query in STMO for
      // ScalarID::QM_REPOSITORIES_INITIALIZATION_TIME. This shouldn't happen
      // because the documentation for TimeStamp::Now() says it returns a
      // monotonically increasing number.
      if (*mStartTime > *mEndTime) {
        return "TimeStampErr1"_ns;
      }

      if (*mInitializedTime > gLastOSWake) {
        return "TimeStampErr2"_ns;
      }

      return "Normal"_ns;
    }();

    mMetric.Get(key).AccumulateRawDuration(*mEndTime - *mStartTime);

    return NS_OK;
  }

  gLastOSWake = TimeStamp::Now();
  mInitializedTime.init(gLastOSWake);

  return NS_OK;
}

// static
nsresult StorageOperationBase::CreateDirectoryMetadata(
    nsIFile& aDirectory, int64_t aTimestamp,
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  StorageOriginAttributes groupAttributes;

  nsCString groupNoSuffix;
  QM_TRY(OkIf(groupAttributes.PopulateFromOrigin(aOriginMetadata.mGroup,
                                                 groupNoSuffix)),
         NS_ERROR_FAILURE);

  nsCString groupPrefix;
  GetJarPrefix(groupAttributes.InIsolatedMozBrowser(), groupPrefix);

  nsCString group = groupPrefix + groupNoSuffix;

  StorageOriginAttributes originAttributes;

  nsCString originNoSuffix;
  QM_TRY(OkIf(originAttributes.PopulateFromOrigin(aOriginMetadata.mOrigin,
                                                  originNoSuffix)),
         NS_ERROR_FAILURE);

  nsCString originPrefix;
  GetJarPrefix(originAttributes.InIsolatedMozBrowser(), originPrefix);

  nsCString origin = originPrefix + originNoSuffix;

  MOZ_ASSERT(groupPrefix == originPrefix);

  QM_TRY_INSPECT(const auto& file, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                       nsCOMPtr<nsIFile>, aDirectory, Clone));

  QM_TRY(MOZ_TO_RESULT(file->Append(nsLiteralString(METADATA_TMP_FILE_NAME))));

  QM_TRY_INSPECT(const auto& stream,
                 GetBinaryOutputStream(*file, FileFlag::Truncate));
  MOZ_ASSERT(stream);

  QM_TRY(MOZ_TO_RESULT(stream->Write64(aTimestamp)));

  QM_TRY(MOZ_TO_RESULT(stream->WriteStringZ(group.get())));

  QM_TRY(MOZ_TO_RESULT(stream->WriteStringZ(origin.get())));

  // Currently unused (used to be isApp).
  QM_TRY(MOZ_TO_RESULT(stream->WriteBoolean(false)));

  QM_TRY(MOZ_TO_RESULT(stream->Flush()));

  QM_TRY(MOZ_TO_RESULT(stream->Close()));

  QM_TRY(MOZ_TO_RESULT(
      file->RenameTo(nullptr, nsLiteralString(METADATA_FILE_NAME))));

  return NS_OK;
}

// static
nsresult StorageOperationBase::CreateDirectoryMetadata2(
    nsIFile& aDirectory, int64_t aTimestamp, bool aPersisted,
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& file, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                       nsCOMPtr<nsIFile>, aDirectory, Clone));

  QM_TRY(
      MOZ_TO_RESULT(file->Append(nsLiteralString(METADATA_V2_TMP_FILE_NAME))));

  QM_TRY_INSPECT(const auto& stream,
                 GetBinaryOutputStream(*file, FileFlag::Truncate));
  MOZ_ASSERT(stream);

  QM_TRY(MOZ_TO_RESULT(stream->Write64(aTimestamp)));

  QM_TRY(MOZ_TO_RESULT(stream->WriteBoolean(aPersisted)));

  // Reserved data 1
  QM_TRY(MOZ_TO_RESULT(stream->Write32(0)));

  // Reserved data 2
  QM_TRY(MOZ_TO_RESULT(stream->Write32(0)));

  // The suffix isn't used right now, but we might need it in future. It's
  // a bit of redundancy we can live with given how painful is to upgrade
  // metadata files.
  QM_TRY(MOZ_TO_RESULT(stream->WriteStringZ(aOriginMetadata.mSuffix.get())));

  QM_TRY(MOZ_TO_RESULT(stream->WriteStringZ(aOriginMetadata.mGroup.get())));

  QM_TRY(MOZ_TO_RESULT(stream->WriteStringZ(aOriginMetadata.mOrigin.get())));

  // Currently unused (used to be isApp).
  QM_TRY(MOZ_TO_RESULT(stream->WriteBoolean(false)));

  QM_TRY(MOZ_TO_RESULT(stream->Flush()));

  QM_TRY(MOZ_TO_RESULT(stream->Close()));

  QM_TRY(MOZ_TO_RESULT(
      file->RenameTo(nullptr, nsLiteralString(METADATA_V2_FILE_NAME))));

  return NS_OK;
}

nsresult StorageOperationBase::GetDirectoryMetadata(nsIFile* aDirectory,
                                                    int64_t& aTimestamp,
                                                    nsACString& aGroup,
                                                    nsACString& aOrigin,
                                                    Nullable<bool>& aIsApp) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);

  QM_TRY_INSPECT(
      const auto& binaryStream,
      GetBinaryInputStream(*aDirectory, nsLiteralString(METADATA_FILE_NAME)));

  QM_TRY_INSPECT(const uint64_t& timestamp,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read64));

  QM_TRY_INSPECT(const auto& group, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                        nsCString, binaryStream, ReadCString));

  QM_TRY_INSPECT(const auto& origin, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                         nsCString, binaryStream, ReadCString));

  Nullable<bool> isApp;
  bool value;
  if (NS_SUCCEEDED(binaryStream->ReadBoolean(&value))) {
    isApp.SetValue(value);
  }

  aTimestamp = timestamp;
  aGroup = group;
  aOrigin = origin;
  aIsApp = std::move(isApp);
  return NS_OK;
}

nsresult StorageOperationBase::GetDirectoryMetadata2(
    nsIFile* aDirectory, int64_t& aTimestamp, nsACString& aSuffix,
    nsACString& aGroup, nsACString& aOrigin, bool& aIsApp) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);

  QM_TRY_INSPECT(const auto& binaryStream,
                 GetBinaryInputStream(*aDirectory,
                                      nsLiteralString(METADATA_V2_FILE_NAME)));

  QM_TRY_INSPECT(const uint64_t& timestamp,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read64));

  QM_TRY_INSPECT(const bool& persisted,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, ReadBoolean));
  (void)persisted;

  QM_TRY_INSPECT(const bool& reservedData1,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read32));
  (void)reservedData1;

  QM_TRY_INSPECT(const bool& reservedData2,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read32));
  (void)reservedData2;

  QM_TRY_INSPECT(const auto& suffix, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                         nsCString, binaryStream, ReadCString));

  QM_TRY_INSPECT(const auto& group, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                        nsCString, binaryStream, ReadCString));

  QM_TRY_INSPECT(const auto& origin, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                         nsCString, binaryStream, ReadCString));

  QM_TRY_INSPECT(const bool& isApp,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, ReadBoolean));

  aTimestamp = timestamp;
  aSuffix = suffix;
  aGroup = group;
  aOrigin = origin;
  aIsApp = isApp;
  return NS_OK;
}

int64_t StorageOperationBase::GetOriginLastModifiedTime(
    const OriginProps& aOriginProps) {
  return GetLastModifiedTime(*aOriginProps.mPersistenceType,
                             *aOriginProps.mDirectory);
}

nsresult StorageOperationBase::RemoveObsoleteOrigin(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  QM_WARNING(
      "Deleting obsolete %s directory that is no longer a legal "
      "origin!",
      NS_ConvertUTF16toUTF8(aOriginProps.mLeafName).get());

  QM_TRY(MOZ_TO_RESULT(aOriginProps.mDirectory->Remove(/* recursive */ true)));

  return NS_OK;
}

Result<bool, nsresult> StorageOperationBase::MaybeRenameOrigin(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  const nsAString& oldLeafName = aOriginProps.mLeafName;

  const auto newLeafName =
      MakeSanitizedOriginString(aOriginProps.mOriginMetadata.mOrigin);

  if (oldLeafName == newLeafName) {
    return false;
  }

  QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata(*aOriginProps.mDirectory,
                                               aOriginProps.mTimestamp,
                                               aOriginProps.mOriginMetadata)));

  QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata2(
      *aOriginProps.mDirectory, aOriginProps.mTimestamp,
      /* aPersisted */ false, aOriginProps.mOriginMetadata)));

  QM_TRY_INSPECT(const auto& newFile,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<nsIFile>, *aOriginProps.mDirectory, GetParent));

  QM_TRY(MOZ_TO_RESULT(newFile->Append(newLeafName)));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(newFile, Exists));

  if (exists) {
    QM_WARNING(
        "Can't rename %s directory to %s, the target already exists, removing "
        "instead of renaming!",
        NS_ConvertUTF16toUTF8(oldLeafName).get(),
        NS_ConvertUTF16toUTF8(newLeafName).get());
  }

  QM_TRY(CallWithDelayedRetriesIfAccessDenied(
      [&exists, &aOriginProps, &newLeafName] {
        if (exists) {
          QM_TRY_RETURN(MOZ_TO_RESULT(
              aOriginProps.mDirectory->Remove(/* recursive */ true)));
        }
        QM_TRY_RETURN(MOZ_TO_RESULT(
            aOriginProps.mDirectory->RenameTo(nullptr, newLeafName)));
      },
      StaticPrefs::dom_quotaManager_directoryRemovalOrRenaming_maxRetries(),
      StaticPrefs::dom_quotaManager_directoryRemovalOrRenaming_delayMs()));

  return true;
}

nsresult StorageOperationBase::ProcessOriginDirectories() {
  AssertIsOnIOThread();
  MOZ_ASSERT(!mOriginProps.IsEmpty());

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  for (auto& originProps : mOriginProps) {
    switch (originProps.mType) {
      case OriginProps::eChrome: {
        originProps.mOriginMetadata = {GetInfoForChrome(),
                                       *originProps.mPersistenceType};
        break;
      }

      case OriginProps::eContent: {
        nsCOMPtr<nsIURI> uri;
        QM_TRY(
            MOZ_TO_RESULT(NS_NewURI(getter_AddRefs(uri), originProps.mSpec)));

        nsCOMPtr<nsIPrincipal> principal =
            BasePrincipal::CreateContentPrincipal(uri, originProps.mAttrs);
        QM_TRY(MOZ_TO_RESULT(principal));

        PrincipalInfo principalInfo;
        QM_TRY(
            MOZ_TO_RESULT(PrincipalToPrincipalInfo(principal, &principalInfo)));

        QM_WARNONLY_TRY_UNWRAP(
            auto valid, MOZ_TO_RESULT(IsPrincipalInfoValid(principalInfo)));

        if (!valid) {
          // Unknown directories during upgrade are allowed. Just warn if we
          // find them.
          UNKNOWN_FILE_WARNING(originProps.mLeafName);
          originProps.mIgnore = true;
          break;
        }

        QM_TRY_UNWRAP(
            auto principalMetadata,
            GetInfoFromValidatedPrincipalInfo(*quotaManager, principalInfo));

        originProps.mOriginMetadata = {std::move(principalMetadata),
                                       *originProps.mPersistenceType};

        break;
      }

      case OriginProps::eObsolete: {
        // There's no way to get info for obsolete origins.
        break;
      }

      default:
        MOZ_CRASH("Bad type!");
    }
  }

  // Don't try to upgrade obsolete origins, remove them right after we detect
  // them.
  for (const auto& originProps : mOriginProps) {
    if (originProps.mType == OriginProps::eObsolete) {
      MOZ_ASSERT(originProps.mOriginMetadata.mSuffix.IsEmpty());
      MOZ_ASSERT(originProps.mOriginMetadata.mGroup.IsEmpty());
      MOZ_ASSERT(originProps.mOriginMetadata.mOrigin.IsEmpty());

      QM_TRY(MOZ_TO_RESULT(RemoveObsoleteOrigin(originProps)));
    } else if (!originProps.mIgnore) {
      MOZ_ASSERT(!originProps.mOriginMetadata.mGroup.IsEmpty());
      MOZ_ASSERT(!originProps.mOriginMetadata.mOrigin.IsEmpty());

      QM_TRY(MOZ_TO_RESULT(ProcessOriginDirectory(originProps)));
    }
  }

  return NS_OK;
}

// XXX Do the fallible initialization in a separate non-static member function
// of StorageOperationBase and eventually get rid of this method and use a
// normal constructor instead.
template <typename PersistenceTypeFunc>
nsresult StorageOperationBase::OriginProps::Init(
    PersistenceTypeFunc&& aPersistenceTypeFunc) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& leafName,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, *mDirectory,
                                                   GetLeafName));

  // XXX Consider using QuotaManager::ParseOrigin here.
  nsCString spec;
  OriginAttributes attrs;
  nsCString originalSuffix;
  OriginParser::ResultType result = OriginParser::ParseOrigin(
      NS_ConvertUTF16toUTF8(leafName), spec, &attrs, originalSuffix);
  if (NS_WARN_IF(result == OriginParser::InvalidOrigin)) {
    mType = OriginProps::eInvalid;
    return NS_OK;
  }

  const auto persistenceType = [&]() -> PersistenceType {
    // XXX We shouldn't continue with initialization if OriginParser returned
    // anything else but ValidOrigin. Otherwise, we have to deal with empty
    // spec when the origin is obsolete, like here. The caller should handle
    // the errors. Until it's fixed, we have to treat obsolete origins as
    // origins with unknown/invalid persistence type.
    if (result != OriginParser::ValidOrigin) {
      return PERSISTENCE_TYPE_INVALID;
    }
    return std::forward<PersistenceTypeFunc>(aPersistenceTypeFunc)(spec);
  }();

  mLeafName = leafName;
  mSpec = std::move(spec);
  mAttrs = std::move(attrs);
  mOriginalSuffix = std::move(originalSuffix);
  mPersistenceType.init(persistenceType);
  if (result == OriginParser::ObsoleteOrigin) {
    mType = eObsolete;
  } else if (mSpec.EqualsLiteral(kChromeOrigin)) {
    mType = eChrome;
  } else {
    mType = eContent;
  }

  return NS_OK;
}

nsresult RepositoryOperationBase::ProcessRepository() {
  AssertIsOnIOThread();

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(mDirectory, Exists),
                   QM_ASSERT_UNREACHABLE);
    MOZ_ASSERT(exists);
  }
#endif

  QM_TRY(CollectEachFileEntry(
      *mDirectory,
      [](const auto& originFile) -> Result<mozilla::Ok, nsresult> {
        QM_TRY_INSPECT(const auto& leafName,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                           nsAutoString, originFile, GetLeafName));

        // Unknown files during upgrade are allowed. Just warn if we find
        // them.
        if (!IsOSMetadata(leafName)) {
          UNKNOWN_FILE_WARNING(leafName);
        }

        return mozilla::Ok{};
      },
      [&self = *this](const auto& originDir) -> Result<mozilla::Ok, nsresult> {
        OriginProps originProps(WrapMovingNotNullUnchecked(originDir));
        QM_TRY(MOZ_TO_RESULT(originProps.Init([&self](const auto& aSpec) {
          return self.PersistenceTypeFromSpec(aSpec);
        })));
        // Bypass invalid origins while upgrading
        QM_TRY(OkIf(originProps.mType != OriginProps::eInvalid), mozilla::Ok{});

        if (originProps.mType != OriginProps::eObsolete) {
          QM_TRY_INSPECT(const bool& removed,
                         MOZ_TO_RESULT_INVOKE_MEMBER(
                             self, PrepareOriginDirectory, originProps));
          if (removed) {
            return mozilla::Ok{};
          }
        }

        self.mOriginProps.AppendElement(std::move(originProps));

        return mozilla::Ok{};
      }));

  if (mOriginProps.IsEmpty()) {
    return NS_OK;
  }

  QM_TRY(MOZ_TO_RESULT(ProcessOriginDirectories()));

  return NS_OK;
}

template <typename UpgradeMethod>
nsresult RepositoryOperationBase::MaybeUpgradeClients(
    const OriginProps& aOriginProps, UpgradeMethod aMethod) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aMethod);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY(CollectEachFileEntry(
      *aOriginProps.mDirectory,
      [](const auto& file) -> Result<mozilla::Ok, nsresult> {
        QM_TRY_INSPECT(
            const auto& leafName,
            MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, file, GetLeafName));

        if (!IsOriginMetadata(leafName) && !IsTempMetadata(leafName)) {
          UNKNOWN_FILE_WARNING(leafName);
        }

        return mozilla::Ok{};
      },
      [quotaManager, &aMethod,
       &self = *this](const auto& dir) -> Result<mozilla::Ok, nsresult> {
        QM_TRY_INSPECT(
            const auto& leafName,
            MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, dir, GetLeafName));

        QM_TRY_INSPECT(const bool& removed,
                       MOZ_TO_RESULT_INVOKE_MEMBER(self, PrepareClientDirectory,
                                                   dir, leafName));
        if (removed) {
          return mozilla::Ok{};
        }

        Client::Type clientType;
        bool ok = Client::TypeFromText(leafName, clientType, fallible);
        if (!ok) {
          UNKNOWN_FILE_WARNING(leafName);
          return mozilla::Ok{};
        }

        Client* client = quotaManager->GetClient(clientType);
        MOZ_ASSERT(client);

        QM_TRY(MOZ_TO_RESULT((client->*aMethod)(dir)));

        return mozilla::Ok{};
      }));

  return NS_OK;
}

nsresult RepositoryOperationBase::PrepareClientDirectory(
    nsIFile* aFile, const nsAString& aLeafName, bool& aRemoved) {
  AssertIsOnIOThread();

  aRemoved = false;
  return NS_OK;
}

nsresult CreateOrUpgradeDirectoryMetadataHelper::Init() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mDirectory);

  const auto maybeLegacyPersistenceType =
      LegacyPersistenceTypeFromFile(*mDirectory, fallible);
  QM_TRY(OkIf(maybeLegacyPersistenceType.isSome()), Err(NS_ERROR_FAILURE));

  mLegacyPersistenceType.init(maybeLegacyPersistenceType.value());

  return NS_OK;
}

Maybe<CreateOrUpgradeDirectoryMetadataHelper::LegacyPersistenceType>
CreateOrUpgradeDirectoryMetadataHelper::LegacyPersistenceTypeFromFile(
    nsIFile& aFile, const fallible_t&) {
  nsAutoString leafName;
  MOZ_ALWAYS_SUCCEEDS(aFile.GetLeafName(leafName));

  if (leafName.Equals(u"persistent"_ns)) {
    return Some(LegacyPersistenceType::Persistent);
  }

  if (leafName.Equals(u"temporary"_ns)) {
    return Some(LegacyPersistenceType::Temporary);
  }

  return Nothing();
}

PersistenceType
CreateOrUpgradeDirectoryMetadataHelper::PersistenceTypeFromLegacyPersistentSpec(
    const nsCString& aSpec) {
  if (QuotaManager::IsOriginInternal(aSpec)) {
    return PERSISTENCE_TYPE_PERSISTENT;
  }

  return PERSISTENCE_TYPE_DEFAULT;
}

PersistenceType CreateOrUpgradeDirectoryMetadataHelper::PersistenceTypeFromSpec(
    const nsCString& aSpec) {
  switch (*mLegacyPersistenceType) {
    case LegacyPersistenceType::Persistent:
      return PersistenceTypeFromLegacyPersistentSpec(aSpec);
    case LegacyPersistenceType::Temporary:
      return PERSISTENCE_TYPE_TEMPORARY;
  }
  MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Bad legacy persistence type value!");
}

nsresult CreateOrUpgradeDirectoryMetadataHelper::MaybeUpgradeOriginDirectory(
    nsIFile* aDirectory) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);

  QM_TRY_INSPECT(
      const auto& metadataFile,
      CloneFileAndAppend(*aDirectory, nsLiteralString(METADATA_FILE_NAME)));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(metadataFile, Exists));

  if (!exists) {
    // Directory structure upgrade needed.
    // Move all files to IDB specific directory.

    nsString idbDirectoryName;
    QM_TRY(OkIf(Client::TypeToText(Client::IDB, idbDirectoryName, fallible)),
           NS_ERROR_FAILURE);

    QM_TRY_INSPECT(const auto& idbDirectory,
                   CloneFileAndAppend(*aDirectory, idbDirectoryName));

    // Usually we only use QM_OR_ELSE_LOG_VERBOSE/QM_OR_ELSE_LOG_VERBOSE_IF
    // with Create and NS_ERROR_FILE_ALREADY_EXISTS check, but typically the
    // idb directory shouldn't exist during the upgrade and the upgrade runs
    // only once in most of the cases, so the use of QM_OR_ELSE_WARN_IF is ok
    // here.
    QM_TRY(QM_OR_ELSE_WARN_IF(
        // Expression.
        MOZ_TO_RESULT(idbDirectory->Create(nsIFile::DIRECTORY_TYPE, 0755)),
        // Predicate.
        IsSpecificError<NS_ERROR_FILE_ALREADY_EXISTS>,
        // Fallback.
        ([&idbDirectory](const nsresult rv) -> Result<Ok, nsresult> {
          QM_TRY_INSPECT(
              const bool& isDirectory,
              MOZ_TO_RESULT_INVOKE_MEMBER(idbDirectory, IsDirectory));

          QM_TRY(OkIf(isDirectory), Err(NS_ERROR_UNEXPECTED));

          return Ok{};
        })));

    QM_TRY(CollectEachFile(
        *aDirectory,
        [&idbDirectory, &idbDirectoryName](
            const nsCOMPtr<nsIFile>& file) -> Result<Ok, nsresult> {
          QM_TRY_INSPECT(const auto& leafName,
                         MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoString, file,
                                                           GetLeafName));

          if (!leafName.Equals(idbDirectoryName)) {
            QM_TRY(MOZ_TO_RESULT(file->MoveTo(idbDirectory, u""_ns)));
          }

          return Ok{};
        }));

    QM_TRY(
        MOZ_TO_RESULT(metadataFile->Create(nsIFile::NORMAL_FILE_TYPE, 0644)));
  }

  return NS_OK;
}

nsresult CreateOrUpgradeDirectoryMetadataHelper::PrepareOriginDirectory(
    OriginProps& aOriginProps, bool* aRemoved) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aRemoved);

  if (*mLegacyPersistenceType == LegacyPersistenceType::Persistent) {
    QM_TRY(MOZ_TO_RESULT(
        MaybeUpgradeOriginDirectory(aOriginProps.mDirectory.get())));

    aOriginProps.mTimestamp = GetOriginLastModifiedTime(aOriginProps);
  } else {
    int64_t timestamp;
    nsCString group;
    nsCString origin;
    Nullable<bool> isApp;

    QM_WARNONLY_TRY_UNWRAP(
        const auto maybeDirectoryMetadata,
        MOZ_TO_RESULT(GetDirectoryMetadata(aOriginProps.mDirectory.get(),
                                           timestamp, group, origin, isApp)));
    if (!maybeDirectoryMetadata) {
      aOriginProps.mTimestamp = GetOriginLastModifiedTime(aOriginProps);
      aOriginProps.mNeedsRestore = true;
    } else if (!isApp.IsNull()) {
      aOriginProps.mIgnore = true;
    }
  }

  *aRemoved = false;
  return NS_OK;
}

nsresult CreateOrUpgradeDirectoryMetadataHelper::ProcessOriginDirectory(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  if (*mLegacyPersistenceType == LegacyPersistenceType::Persistent) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        aOriginProps.mOriginMetadata)));

    // Move internal origins to new persistent storage.
    if (PersistenceTypeFromLegacyPersistentSpec(aOriginProps.mSpec) ==
        PERSISTENCE_TYPE_PERSISTENT) {
      if (!mPermanentStorageDir) {
        QuotaManager* quotaManager = QuotaManager::Get();
        MOZ_ASSERT(quotaManager);

        const nsString& permanentStoragePath =
            quotaManager->GetStoragePath(PERSISTENCE_TYPE_PERSISTENT);

        QM_TRY_UNWRAP(mPermanentStorageDir,
                      QM_NewLocalFile(permanentStoragePath));
      }

      const nsAString& leafName = aOriginProps.mLeafName;

      QM_TRY_INSPECT(const auto& newDirectory,
                     CloneFileAndAppend(*mPermanentStorageDir, leafName));

      QM_TRY_INSPECT(const bool& exists,
                     MOZ_TO_RESULT_INVOKE_MEMBER(newDirectory, Exists));

      if (exists) {
        QM_WARNING("Found %s in storage/persistent and storage/permanent !",
                   NS_ConvertUTF16toUTF8(leafName).get());

        QM_TRY(MOZ_TO_RESULT(
            aOriginProps.mDirectory->Remove(/* recursive */ true)));
      } else {
        QM_TRY(MOZ_TO_RESULT(
            aOriginProps.mDirectory->MoveTo(mPermanentStorageDir, u""_ns)));
      }
    }
  } else if (aOriginProps.mNeedsRestore) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        aOriginProps.mOriginMetadata)));
  } else if (!aOriginProps.mIgnore) {
    QM_TRY_INSPECT(const auto& file,
                   CloneFileAndAppend(*aOriginProps.mDirectory,
                                      nsLiteralString(METADATA_FILE_NAME)));

    QM_TRY_INSPECT(const auto& stream,
                   GetBinaryOutputStream(*file, FileFlag::Append));

    MOZ_ASSERT(stream);

    // Currently unused (used to be isApp).
    QM_TRY(MOZ_TO_RESULT(stream->WriteBoolean(false)));
  }

  return NS_OK;
}

nsresult UpgradeStorageHelperBase::Init() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mDirectory);

  const auto maybePersistenceType =
      PersistenceTypeFromFile(*mDirectory, fallible);
  QM_TRY(OkIf(maybePersistenceType.isSome()), Err(NS_ERROR_FAILURE));

  mPersistenceType.init(maybePersistenceType.value());

  return NS_OK;
}

PersistenceType UpgradeStorageHelperBase::PersistenceTypeFromSpec(
    const nsCString& aSpec) {
  // There's no moving of origin directories between repositories like in the
  // CreateOrUpgradeDirectoryMetadataHelper
  return *mPersistenceType;
}

nsresult UpgradeStorageFrom0_0To1_0Helper::PrepareOriginDirectory(
    OriginProps& aOriginProps, bool* aRemoved) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aRemoved);

  int64_t timestamp;
  nsCString group;
  nsCString origin;
  Nullable<bool> isApp;

  QM_WARNONLY_TRY_UNWRAP(
      const auto maybeDirectoryMetadata,
      MOZ_TO_RESULT(GetDirectoryMetadata(aOriginProps.mDirectory.get(),
                                         timestamp, group, origin, isApp)));
  if (!maybeDirectoryMetadata || isApp.IsNull()) {
    aOriginProps.mTimestamp = GetOriginLastModifiedTime(aOriginProps);
    aOriginProps.mNeedsRestore = true;
  } else {
    aOriginProps.mTimestamp = timestamp;
  }

  *aRemoved = false;
  return NS_OK;
}

nsresult UpgradeStorageFrom0_0To1_0Helper::ProcessOriginDirectory(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  // This handles changes in origin string generation from nsIPrincipal,
  // especially the change from: appId+inMozBrowser+originNoSuffix
  // to: origin (with origin suffix).
  QM_TRY_INSPECT(const bool& renamed, MaybeRenameOrigin(aOriginProps));
  if (renamed) {
    return NS_OK;
  }

  if (aOriginProps.mNeedsRestore) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        aOriginProps.mOriginMetadata)));
  }

  QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata2(
      *aOriginProps.mDirectory, aOriginProps.mTimestamp,
      /* aPersisted */ false, aOriginProps.mOriginMetadata)));

  return NS_OK;
}

nsresult UpgradeStorageFrom1_0To2_0Helper::MaybeRemoveMorgueDirectory(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  // The Cache API was creating top level morgue directories by accident for
  // a short time in nightly.  This unfortunately prevents all storage from
  // working.  So recover these profiles permanently by removing these corrupt
  // directories as part of this upgrade.

  QM_TRY_INSPECT(const auto& morgueDir,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<nsIFile>, *aOriginProps.mDirectory, Clone));

  QM_TRY(MOZ_TO_RESULT(morgueDir->Append(u"morgue"_ns)));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(morgueDir, Exists));

  if (exists) {
    QM_WARNING("Deleting accidental morgue directory!");

    QM_TRY(MOZ_TO_RESULT(morgueDir->Remove(/* recursive */ true)));
  }

  return NS_OK;
}

Result<bool, nsresult> UpgradeStorageFrom1_0To2_0Helper::MaybeRemoveAppsData(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  // TODO: This method was empty for some time due to accidental changes done
  //       in bug 1320404. This led to renaming of origin directories like:
  //         https+++developer.cdn.mozilla.net^appId=1007&inBrowser=1
  //       to:
  //         https+++developer.cdn.mozilla.net^inBrowser=1
  //       instead of just removing them.

  const nsCString& originalSuffix = aOriginProps.mOriginalSuffix;
  if (!originalSuffix.IsEmpty()) {
    MOZ_ASSERT(originalSuffix[0] == '^');

    if (!URLParams::Parse(
            Substring(originalSuffix, 1, originalSuffix.Length() - 1), true,
            [](const nsACString& aName, const nsACString& aValue) {
              if (aName.EqualsLiteral("appId")) {
                return false;
              }
              return true;
            })) {
      QM_TRY(MOZ_TO_RESULT(RemoveObsoleteOrigin(aOriginProps)));

      return true;
    }
  }

  return false;
}

nsresult UpgradeStorageFrom1_0To2_0Helper::PrepareOriginDirectory(
    OriginProps& aOriginProps, bool* aRemoved) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aRemoved);

  QM_TRY(MOZ_TO_RESULT(MaybeRemoveMorgueDirectory(aOriginProps)));

  QM_TRY(MOZ_TO_RESULT(
      MaybeUpgradeClients(aOriginProps, &Client::UpgradeStorageFrom1_0To2_0)));

  QM_TRY_INSPECT(const bool& removed, MaybeRemoveAppsData(aOriginProps));
  if (removed) {
    *aRemoved = true;
    return NS_OK;
  }

  int64_t timestamp;
  nsCString group;
  nsCString origin;
  Nullable<bool> isApp;
  QM_WARNONLY_TRY_UNWRAP(
      const auto maybeDirectoryMetadata,
      MOZ_TO_RESULT(GetDirectoryMetadata(aOriginProps.mDirectory.get(),
                                         timestamp, group, origin, isApp)));
  if (!maybeDirectoryMetadata || isApp.IsNull()) {
    aOriginProps.mNeedsRestore = true;
  }

  nsCString suffix;
  QM_WARNONLY_TRY_UNWRAP(const auto maybeDirectoryMetadata2,
                         MOZ_TO_RESULT(GetDirectoryMetadata2(
                             aOriginProps.mDirectory.get(), timestamp, suffix,
                             group, origin, isApp.SetValue())));
  if (!maybeDirectoryMetadata2) {
    aOriginProps.mTimestamp = GetOriginLastModifiedTime(aOriginProps);
    aOriginProps.mNeedsRestore2 = true;
  } else {
    aOriginProps.mTimestamp = timestamp;
  }

  *aRemoved = false;
  return NS_OK;
}

nsresult UpgradeStorageFrom1_0To2_0Helper::ProcessOriginDirectory(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  // This handles changes in origin string generation from nsIPrincipal,
  // especially the stripping of obsolete origin attributes like addonId.
  QM_TRY_INSPECT(const bool& renamed, MaybeRenameOrigin(aOriginProps));
  if (renamed) {
    return NS_OK;
  }

  if (aOriginProps.mNeedsRestore) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        aOriginProps.mOriginMetadata)));
  }

  if (aOriginProps.mNeedsRestore2) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata2(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        /* aPersisted */ false, aOriginProps.mOriginMetadata)));
  }

  return NS_OK;
}

nsresult UpgradeStorageFrom2_0To2_1Helper::PrepareOriginDirectory(
    OriginProps& aOriginProps, bool* aRemoved) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aRemoved);

  QM_TRY(MOZ_TO_RESULT(
      MaybeUpgradeClients(aOriginProps, &Client::UpgradeStorageFrom2_0To2_1)));

  int64_t timestamp;
  nsCString group;
  nsCString origin;
  Nullable<bool> isApp;
  QM_WARNONLY_TRY_UNWRAP(
      const auto maybeDirectoryMetadata,
      MOZ_TO_RESULT(GetDirectoryMetadata(aOriginProps.mDirectory.get(),
                                         timestamp, group, origin, isApp)));
  if (!maybeDirectoryMetadata || isApp.IsNull()) {
    aOriginProps.mNeedsRestore = true;
  }

  nsCString suffix;
  QM_WARNONLY_TRY_UNWRAP(const auto maybeDirectoryMetadata2,
                         MOZ_TO_RESULT(GetDirectoryMetadata2(
                             aOriginProps.mDirectory.get(), timestamp, suffix,
                             group, origin, isApp.SetValue())));
  if (!maybeDirectoryMetadata2) {
    aOriginProps.mTimestamp = GetOriginLastModifiedTime(aOriginProps);
    aOriginProps.mNeedsRestore2 = true;
  } else {
    aOriginProps.mTimestamp = timestamp;
  }

  *aRemoved = false;
  return NS_OK;
}

nsresult UpgradeStorageFrom2_0To2_1Helper::ProcessOriginDirectory(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  if (aOriginProps.mNeedsRestore) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        aOriginProps.mOriginMetadata)));
  }

  if (aOriginProps.mNeedsRestore2) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata2(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        /* aPersisted */ false, aOriginProps.mOriginMetadata)));
  }

  return NS_OK;
}

nsresult UpgradeStorageFrom2_1To2_2Helper::PrepareOriginDirectory(
    OriginProps& aOriginProps, bool* aRemoved) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aRemoved);

  QM_TRY(MOZ_TO_RESULT(
      MaybeUpgradeClients(aOriginProps, &Client::UpgradeStorageFrom2_1To2_2)));

  int64_t timestamp;
  nsCString group;
  nsCString origin;
  Nullable<bool> isApp;
  QM_WARNONLY_TRY_UNWRAP(
      const auto maybeDirectoryMetadata,
      MOZ_TO_RESULT(GetDirectoryMetadata(aOriginProps.mDirectory.get(),
                                         timestamp, group, origin, isApp)));
  if (!maybeDirectoryMetadata || isApp.IsNull()) {
    aOriginProps.mNeedsRestore = true;
  }

  nsCString suffix;
  QM_WARNONLY_TRY_UNWRAP(const auto maybeDirectoryMetadata2,
                         MOZ_TO_RESULT(GetDirectoryMetadata2(
                             aOriginProps.mDirectory.get(), timestamp, suffix,
                             group, origin, isApp.SetValue())));
  if (!maybeDirectoryMetadata2) {
    aOriginProps.mTimestamp = GetOriginLastModifiedTime(aOriginProps);
    aOriginProps.mNeedsRestore2 = true;
  } else {
    aOriginProps.mTimestamp = timestamp;
  }

  *aRemoved = false;
  return NS_OK;
}

nsresult UpgradeStorageFrom2_1To2_2Helper::ProcessOriginDirectory(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  if (aOriginProps.mNeedsRestore) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        aOriginProps.mOriginMetadata)));
  }

  if (aOriginProps.mNeedsRestore2) {
    QM_TRY(MOZ_TO_RESULT(CreateDirectoryMetadata2(
        *aOriginProps.mDirectory, aOriginProps.mTimestamp,
        /* aPersisted */ false, aOriginProps.mOriginMetadata)));
  }

  return NS_OK;
}

nsresult UpgradeStorageFrom2_1To2_2Helper::PrepareClientDirectory(
    nsIFile* aFile, const nsAString& aLeafName, bool& aRemoved) {
  AssertIsOnIOThread();

  if (Client::IsDeprecatedClient(aLeafName)) {
    QM_WARNING("Deleting deprecated %s client!",
               NS_ConvertUTF16toUTF8(aLeafName).get());

    QM_TRY(MOZ_TO_RESULT(aFile->Remove(true)));

    aRemoved = true;
  } else {
    aRemoved = false;
  }

  return NS_OK;
}

nsresult RestoreDirectoryMetadata2Helper::Init() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mDirectory);

  nsCOMPtr<nsIFile> parentDir;
  QM_TRY(MOZ_TO_RESULT(mDirectory->GetParent(getter_AddRefs(parentDir))));

  const auto maybePersistenceType =
      PersistenceTypeFromFile(*parentDir, fallible);
  QM_TRY(OkIf(maybePersistenceType.isSome()), Err(NS_ERROR_FAILURE));

  mPersistenceType.init(maybePersistenceType.value());

  return NS_OK;
}

nsresult RestoreDirectoryMetadata2Helper::RestoreMetadata2File() {
  OriginProps originProps(WrapMovingNotNull(mDirectory));
  QM_TRY(MOZ_TO_RESULT(originProps.Init(
      [&self = *this](const auto& aSpec) { return *self.mPersistenceType; })));

  QM_TRY(OkIf(originProps.mType != OriginProps::eInvalid), NS_ERROR_FAILURE);

  originProps.mTimestamp = GetOriginLastModifiedTime(originProps);

  mOriginProps.AppendElement(std::move(originProps));

  QM_TRY(MOZ_TO_RESULT(ProcessOriginDirectories()));

  return NS_OK;
}

nsresult RestoreDirectoryMetadata2Helper::ProcessOriginDirectory(
    const OriginProps& aOriginProps) {
  AssertIsOnIOThread();

  QuotaManager* qm = QuotaManager::Get();
  MOZ_DIAGNOSTIC_ASSERT(qm);

  FullOriginMetadata fullOriginMetadata{
      aOriginProps.mOriginMetadata,
      OriginStateMetadata{aOriginProps.mTimestamp,
                          /* aLastMaintenanceDate */
                          Date::FromTimestamp(aOriginProps.mTimestamp).ToDays(),
                          /* aAccessed */ true,
                          /* aPersisted */ false,
                          /* aDirty */ false},
      ClientUsageArray(), /* aUsage */ 0, kNoQuotaVersion};

  // We don't have any approach to restore aPersisted, so reset it to false.
  QM_TRY(MOZ_TO_RESULT(qm->CreateDirectoryMetadata2(*aOriginProps.mDirectory,
                                                    fullOriginMetadata)));

  // CreateDirectoryMetadata2 clears mDirty as part of writing the canonical
  // state; the in-memory metadata reflects the post-write state from here on.
  MOZ_ASSERT(!fullOriginMetadata.mDirty);

  return NS_OK;
}

}  // namespace mozilla::dom::quota
