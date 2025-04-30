/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ActorsParent.h"

// Local includes
#include "LSInitializationTypes.h"
#include "LSObject.h"
#include "ReportInternalError.h"

// Global includes
#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <new>
#include <tuple>
#include <type_traits>
#include <utility>
#include "ErrorList.h"
#include "MainThreadUtils.h"
#include "mozIStorageAsyncConnection.h"
#include "mozIStorageConnection.h"
#include "mozIStorageFunction.h"
#include "mozIStorageService.h"
#include "mozIStorageStatement.h"
#include "mozIStorageValueArray.h"
#include "mozStorageCID.h"
#include "mozStorageHelper.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/Logging.h"
#include "mozilla/MacroForEach.h"
#include "mozilla/Maybe.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/NotNull.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/RefPtr.h"
#include "mozilla/Result.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/Utf8.h"
#include "mozilla/Variant.h"
#include "mozilla/dom/ClientManagerService.h"
#include "mozilla/dom/FlippedOnce.h"
#include "mozilla/dom/LSSnapshot.h"
#include "mozilla/dom/LSValue.h"
#include "mozilla/dom/LSWriteOptimizer.h"
#include "mozilla/dom/LSWriteOptimizerImpl.h"
#include "mozilla/dom/LocalStorageCommon.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/PBackgroundLSDatabase.h"
#include "mozilla/dom/PBackgroundLSDatabaseParent.h"
#include "mozilla/dom/PBackgroundLSObserverParent.h"
#include "mozilla/dom/PBackgroundLSRequestParent.h"
#include "mozilla/dom/PBackgroundLSSharedTypes.h"
#include "mozilla/dom/PBackgroundLSSimpleRequestParent.h"
#include "mozilla/dom/PBackgroundLSSnapshotParent.h"
#include "mozilla/dom/SnappyUtils.h"
#include "mozilla/dom/StorageDBUpdater.h"
#include "mozilla/dom/StorageUtils.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/quota/CachingDatabaseConnection.h"
#include "mozilla/dom/quota/CheckedUnsafePtr.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/ClientImpl.h"
#include "mozilla/dom/quota/FirstInitializationAttemptsImpl.h"
#include "mozilla/dom/quota/HashKeys.h"
#include "mozilla/dom/quota/OriginScope.h"
#include "mozilla/dom/quota/PersistenceScope.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "mozilla/dom/quota/PrincipalUtils.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/StorageHelpers.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/QuotaObject.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/dom/quota/ThreadUtils.h"
#include "mozilla/dom/quota/UsageInfo.h"
#include "mozilla/glean/DomLocalstorageMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundParent.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/storage/Variant.h"
#include "NotifyUtils.h"
#include "nsBaseHashtable.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsTHashMap.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsHashKeys.h"
#include "nsIBinaryInputStream.h"
#include "nsIBinaryOutputStream.h"
#include "nsIDirectoryEnumerator.h"
#include "nsIEventTarget.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIOutputStream.h"
#include "nsIRunnable.h"
#include "nsISerialEventTarget.h"
#include "nsISupports.h"
#include "nsIThread.h"
#include "nsITimer.h"
#include "nsIVariant.h"
#include "nsInterfaceHashtable.h"
#include "nsLiteralString.h"
#include "nsNetUtil.h"
#include "nsPointerHashKeys.h"
#include "nsPrintfCString.h"
#include "nsRefPtrHashtable.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsStringFlags.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashSet.h"
#include "nsTLiteralString.h"
#include "nsTStringRepr.h"
#include "nsThreadUtils.h"
#include "nsVariant.h"
#include "nsXPCOM.h"
#include "nsXULAppAPI.h"
#include "nscore.h"
#include "prenv.h"
#include "prtime.h"

#define LS_LOG_TEST() MOZ_LOG_TEST(GetLocalStorageLogger(), LogLevel::Info)
#define LS_LOG(_args) MOZ_LOG(GetLocalStorageLogger(), LogLevel::Info, _args)

#if defined(MOZ_WIDGET_ANDROID)
#  define LS_MOBILE
#endif

namespace mozilla::dom {

using namespace mozilla::dom::quota;
using namespace mozilla::dom::StorageUtils;
using namespace mozilla::ipc;

namespace {

struct ArchivedOriginInfo;
class ArchivedOriginScope;
class Connection;
class ConnectionThread;
class Database;
class Observer;
class PrepareDatastoreOp;
class PreparedDatastore;
class QuotaClient;
class Snapshot;

using ArchivedOriginHashtable =
    nsClassHashtable<nsCStringHashKey, ArchivedOriginInfo>;

/*******************************************************************************
 * Constants
 ******************************************************************************/

// Major schema version. Bump for almost everything.
const uint32_t kMajorSchemaVersion = 5;

// Minor schema version. Should almost always be 0 (maybe bump on release
// branches if we have to).
const uint32_t kMinorSchemaVersion = 0;

// The schema version we store in the SQLite database is a (signed) 32-bit
// integer. The major version is left-shifted 4 bits so the max value is
// 0xFFFFFFF. The minor version occupies the lower 4 bits and its max is 0xF.
static_assert(kMajorSchemaVersion <= 0xFFFFFFF,
              "Major version needs to fit in 28 bits.");
static_assert(kMinorSchemaVersion <= 0xF,
              "Minor version needs to fit in 4 bits.");

const int32_t kSQLiteSchemaVersion =
    int32_t((kMajorSchemaVersion << 4) + kMinorSchemaVersion);

// Changing the value here will override the page size of new databases only.
// A journal mode change and VACUUM are needed to change existing databases, so
// the best way to do that is to use the schema version upgrade mechanism.
const uint32_t kSQLitePageSizeOverride =
#ifdef LS_MOBILE
    512;
#else
    1024;
#endif

static_assert(kSQLitePageSizeOverride == /* mozStorage default */ 0 ||
                  (kSQLitePageSizeOverride % 2 == 0 &&
                   kSQLitePageSizeOverride >= 512 &&
                   kSQLitePageSizeOverride <= 65536),
              "Must be 0 (disabled) or a power of 2 between 512 and 65536!");

// Set to some multiple of the page size to grow the database in larger chunks.
const uint32_t kSQLiteGrowthIncrement = kSQLitePageSizeOverride * 2;

static_assert(kSQLiteGrowthIncrement >= 0 &&
                  kSQLiteGrowthIncrement % kSQLitePageSizeOverride == 0 &&
                  kSQLiteGrowthIncrement < uint32_t(INT32_MAX),
              "Must be 0 (disabled) or a positive multiple of the page size!");

/**
 * The database name for LocalStorage data in a per-origin directory.
 */
constexpr auto kDataFileName = u"data.sqlite"_ns;

/**
 * The journal corresponding to kDataFileName.  (We don't use WAL mode.)
 * Currently only needed in QuotaClient::InitOrigin and only in DEBUG builds.
 * See the corresponding comment in QuotaClient::InitOrigin.
 */
#ifdef DEBUG
constexpr auto kJournalFileName = u"data.sqlite-journal"_ns;
#endif

/**
 * This file contains the current usage of the LocalStorage database as defined
 * by the mozLength totals of all keys and values for the database, which
 * differs from the actual size on disk.  We store this value in a separate
 * file as a cache so that we can initialize the QuotaClient faster.
 * In the future, this file will be eliminated and the information will be
 * stored in PROFILE/storage.sqlite or similar QuotaManager-wide storage.
 *
 * The file contains a binary verification cookie (32-bits) followed by the
 * actual usage (64-bits).
 */
constexpr auto kUsageFileName = u"usage"_ns;

/**
 * Following a QuotaManager idiom, this journal file's existence is a marker
 * that the usage file was in the process of being updated and is currently
 * invalid.  This file is created prior to updating the usage file and only
 * deleted after the usage file has been written and closed and any pending
 * database transactions have been committed.  Note that this idiom is expected
 * to work if Gecko crashes in the middle of a write, but is not expected to be
 * foolproof in the face of a system crash, as we do not explicitly attempt to
 * fsync the directory containing the journal file.
 *
 * If the journal file is found to exist at origin initialization time, the
 * usage will be re-computed from the current state of DATA_FILE_NAME.
 */
constexpr auto kUsageJournalFileName = u"usage-journal"_ns;

static const uint32_t kUsageFileSize = 12;
static const uint32_t kUsageFileCookie = 0x420a420a;

/**
 * How long between the first moment we know we have data to be written on a
 * `Connection` and when we should actually perform the write.  This helps
 * limit disk churn under silly usage patterns and is historically consistent
 * with the previous, legacy implementation.
 *
 * Note that flushing happens downstream of Snapshot checkpointing and its
 * batch mechanism which helps avoid wasteful IPC in the case of silly content
 * code.
 */
const uint32_t kFlushTimeoutMs = 5000;

const bool kDefaultShadowWrites = false;
const uint32_t kDefaultSnapshotPrefill = 16384;
const uint32_t kDefaultSnapshotGradualPrefill = 4096;
const bool kDefaultClientValidation = true;
/**
 * Should all mutations also be reflected in the "shadow" database, which is
 * the legacy webappsstore.sqlite database.  When this is enabled, users can
 * downgrade their version of Firefox and/or otherwise fall back to the legacy
 * implementation without loss of data.  (Older versions of Firefox will
 * recognize the presence of ls-archive.sqlite and purge it and the other
 * LocalStorage directories so privacy is maintained.)
 */
const char kShadowWritesPref[] = "dom.storage.shadow_writes";
/**
 * Byte budget for sending data down to the LSSnapshot instance when it is first
 * created.  If there is less data than this (measured by tallying the string
 * length of the keys and values), all data is sent, otherwise partial data is
 * sent.  See `Snapshot`.
 */
const char kSnapshotPrefillPref[] = "dom.storage.snapshot_prefill";
/**
 * When a specific value is requested by an LSSnapshot that is not already fully
 * populated, gradual prefill is used. This preference specifies the number of
 * bytes to be used to send values beyond the specific value that is requested.
 * (The size of the explicitly requested value does not impact this preference.)
 * Setting the value to 0 disables gradual prefill. Tests may set this value to
 * -1 which is converted to INT_MAX in order to cause gradual prefill to send
 * all values not previously sent.
 */
const char kSnapshotGradualPrefillPref[] =
    "dom.storage.snapshot_gradual_prefill";

const char kClientValidationPref[] = "dom.storage.client_validation";

/**
 * The amount of time a PreparedDatastore instance should stick around after a
 * preload is triggered in order to give time for the page to use LocalStorage
 * without triggering worst-case synchronous jank.
 */
const uint32_t kPreparedDatastoreTimeoutMs = 20000;

/**
 * Cold storage for LocalStorage data extracted from webappsstore.sqlite at
 * LSNG first-run that has not yet been migrated to its own per-origin directory
 * by use.
 *
 * In other words, at first run, LSNG copies the contents of webappsstore.sqlite
 * into this database.  As requests are made for that LocalStorage data, the
 * contents are removed from this database and placed into per-origin QM
 * storage.  So the contents of this database are always old, unused
 * LocalStorage data that we can potentially get rid of at some point in the
 * future.
 */
#define LS_ARCHIVE_FILE_NAME u"ls-archive.sqlite"
/**
 * The legacy LocalStorage database.  Its contents are maintained as our
 * "shadow" database so that LSNG can be disabled without loss of user data.
 */
#define WEB_APPS_STORE_FILE_NAME u"webappsstore.sqlite"

// Shadow database Write Ahead Log's maximum size is 512KB
const uint32_t kShadowMaxWALSize = 512 * 1024;

bool IsOnGlobalConnectionThread();

void AssertIsOnGlobalConnectionThread();

/*******************************************************************************
 * SQLite functions
 ******************************************************************************/

int32_t MakeSchemaVersion(uint32_t aMajorSchemaVersion,
                          uint32_t aMinorSchemaVersion) {
  return int32_t((aMajorSchemaVersion << 4) + aMinorSchemaVersion);
}

nsCString GetArchivedOriginHashKey(const nsACString& aOriginSuffix,
                                   const nsACString& aOriginNoSuffix) {
  return aOriginSuffix + ":"_ns + aOriginNoSuffix;
}

nsresult CreateDataTable(mozIStorageConnection* aConnection) {
  return aConnection->ExecuteSimpleSQL(
      "CREATE TABLE data"
      "( key TEXT PRIMARY KEY"
      ", utf16_length INTEGER NOT NULL"
      ", conversion_type INTEGER NOT NULL"
      ", compression_type INTEGER NOT NULL"
      ", last_access_time INTEGER NOT NULL DEFAULT 0"
      ", value BLOB NOT NULL"
      ");"_ns);
}

nsresult CreateTables(mozIStorageConnection* aConnection) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(aConnection);

  // Table `database`
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "CREATE TABLE database"
      "( origin TEXT NOT NULL"
      ", usage INTEGER NOT NULL DEFAULT 0"
      ", last_vacuum_time INTEGER NOT NULL DEFAULT 0"
      ", last_analyze_time INTEGER NOT NULL DEFAULT 0"
      ", last_vacuum_size INTEGER NOT NULL DEFAULT 0"
      ");"_ns)));

  // Table `data`
  QM_TRY(MOZ_TO_RESULT(CreateDataTable(aConnection)));

  QM_TRY(MOZ_TO_RESULT(aConnection->SetSchemaVersion(kSQLiteSchemaVersion)));

  return NS_OK;
}

nsresult UpgradeSchemaFrom1_0To2_0(mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "ALTER TABLE database ADD COLUMN usage INTEGER NOT NULL DEFAULT 0;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "UPDATE database "
      "SET usage = (SELECT total(utf16Length(key) + utf16Length(value)) "
      "FROM data);"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConnection->SetSchemaVersion(MakeSchemaVersion(2, 0))));

  return NS_OK;
}

nsresult UpgradeSchemaFrom2_0To3_0(mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "ALTER TABLE data ADD COLUMN utf16Length INTEGER NOT NULL DEFAULT 0;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "UPDATE data SET utf16Length = utf16Length(value);"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConnection->SetSchemaVersion(MakeSchemaVersion(3, 0))));

  return NS_OK;
}

nsresult UpgradeSchemaFrom3_0To4_0(mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(aConnection->SetSchemaVersion(MakeSchemaVersion(4, 0))));

  return NS_OK;
}

nsresult UpgradeSchemaFrom4_0To5_0(mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  // Recreate data table in new format following steps at
  // https://www.sqlite.org/lang_altertable.html
  // section "Making Other Kinds Of Table Schema Changes"
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "CREATE TABLE migrated_data"
      "( key TEXT PRIMARY KEY"
      ", utf16_length INTEGER NOT NULL"
      ", conversion_type INTEGER NOT NULL"
      ", compression_type INTEGER NOT NULL"
      ", last_access_time INTEGER NOT NULL DEFAULT 0"
      ", value BLOB NOT NULL"
      ");"_ns)));

  // Reinsert old data, all legacy data is UTF8
  static_assert(1u ==
                static_cast<uint8_t>(LSValue::ConversionType::UTF16_UTF8));
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "INSERT INTO migrated_data (key, utf16_length, conversion_type, "
      "compression_type, last_access_time, value) "
      "SELECT key, utf16Length, 1, compressed, lastAccessTime, value "
      "FROM data;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL("DROP TABLE data;"_ns)));

  // Rename to data
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
      "ALTER TABLE migrated_data RENAME TO data;"_ns)));

  QM_TRY(MOZ_TO_RESULT(aConnection->SetSchemaVersion(MakeSchemaVersion(5, 0))));

  return NS_OK;
}

nsresult SetDefaultPragmas(mozIStorageConnection* aConnection) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(
      aConnection->ExecuteSimpleSQL("PRAGMA synchronous = FULL;"_ns)));

#ifndef LS_MOBILE
  if (kSQLiteGrowthIncrement) {
    // This is just an optimization so ignore the failure if the disk is
    // currently too full.
    QM_TRY(QM_OR_ELSE_WARN_IF(
        // Expression.
        MOZ_TO_RESULT(
            aConnection->SetGrowthIncrement(kSQLiteGrowthIncrement, ""_ns)),
        // Predicate.
        IsSpecificError<NS_ERROR_FILE_TOO_BIG>,
        // Fallback.
        ErrToDefaultOk<>));
  }
#endif  // LS_MOBILE

  return NS_OK;
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult> CreateStorageConnection(
    nsIFile& aDBFile, nsIFile& aUsageFile, const nsACString& aOrigin) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());

  // XXX Common logic should be refactored out of this method and
  // cache::DBAction::OpenDBConnection, and maybe other similar functions.

  QM_TRY_INSPECT(const auto& storageService,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  QM_TRY_UNWRAP(auto connection, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                     nsCOMPtr<mozIStorageConnection>,
                                     storageService, OpenDatabase, &aDBFile,
                                     mozIStorageService::CONNECTION_DEFAULT));

  QM_TRY(MOZ_TO_RESULT(SetDefaultPragmas(connection)));

  // Check to make sure that the database schema is correct.
  // XXX Try to make schemaVersion const.
  QM_TRY_UNWRAP(int32_t schemaVersion,
                MOZ_TO_RESULT_INVOKE_MEMBER(connection, GetSchemaVersion));

  QM_TRY(OkIf(schemaVersion <= kSQLiteSchemaVersion), Err(NS_ERROR_FAILURE));

  if (schemaVersion != kSQLiteSchemaVersion) {
    const bool newDatabase = !schemaVersion;

    if (newDatabase) {
      // Set the page size first.
      if (kSQLitePageSizeOverride) {
        QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL(nsPrintfCString(
            "PRAGMA page_size = %" PRIu32 ";", kSQLitePageSizeOverride))));
      }

      // We have to set the auto_vacuum mode before opening a transaction.
      QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL(
#ifdef LS_MOBILE
          // Turn on full auto_vacuum mode to reclaim disk space on mobile
          // devices (at the cost of some COMMIT speed).
          "PRAGMA auto_vacuum = FULL;"_ns
#else
          // Turn on incremental auto_vacuum mode on desktop builds.
          "PRAGMA auto_vacuum = INCREMENTAL;"_ns
#endif
          )));
    }

    bool vacuumNeeded = false;

    if (newDatabase) {
      mozStorageTransaction transaction(
          connection,
          /* aCommitOnComplete */ false,
          mozIStorageConnection::TRANSACTION_IMMEDIATE);

      QM_TRY(MOZ_TO_RESULT(transaction.Start()));

      QM_TRY(MOZ_TO_RESULT(CreateTables(connection)));

#ifdef DEBUG
      {
        QM_TRY_INSPECT(
            const int32_t& schemaVersion,
            MOZ_TO_RESULT_INVOKE_MEMBER(connection, GetSchemaVersion),
            QM_ASSERT_UNREACHABLE);

        MOZ_ASSERT(schemaVersion == kSQLiteSchemaVersion);
      }
#endif

      QM_TRY_INSPECT(
          const auto& stmt,
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
              nsCOMPtr<mozIStorageStatement>, connection, CreateStatement,
              "INSERT INTO database (origin) VALUES (:origin)"_ns));

      QM_TRY(MOZ_TO_RESULT(stmt->BindUTF8StringByName("origin"_ns, aOrigin)));

      QM_TRY(MOZ_TO_RESULT(stmt->Execute()));

      QM_TRY(MOZ_TO_RESULT(transaction.Commit()));
    } else {
      // This logic needs to change next time we change the schema!
      static_assert(kSQLiteSchemaVersion == int32_t((5 << 4) + 0),
                    "Upgrade function needed due to schema version increase.");

      while (schemaVersion != kSQLiteSchemaVersion) {
        mozStorageTransaction transaction(
            connection,
            /* aCommitOnComplete */ false,
            mozIStorageConnection::TRANSACTION_IMMEDIATE);

        QM_TRY(MOZ_TO_RESULT(transaction.Start()));

        if (schemaVersion == MakeSchemaVersion(1, 0)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeSchemaFrom1_0To2_0(connection)));
        } else if (schemaVersion == MakeSchemaVersion(2, 0)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeSchemaFrom2_0To3_0(connection)));
        } else if (schemaVersion == MakeSchemaVersion(3, 0)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeSchemaFrom3_0To4_0(connection)));
        } else if (schemaVersion == MakeSchemaVersion(4, 0)) {
          QM_TRY(MOZ_TO_RESULT(UpgradeSchemaFrom4_0To5_0(connection)));
          vacuumNeeded = true;
        } else {
          LS_WARNING(
              "Unable to open LocalStorage database, no upgrade path is "
              "available!");
          return Err(NS_ERROR_FAILURE);
        }

        QM_TRY(MOZ_TO_RESULT(transaction.Commit()));

        QM_TRY_UNWRAP(schemaVersion, MOZ_TO_RESULT_INVOKE_MEMBER(
                                         connection, GetSchemaVersion));
      }

      MOZ_ASSERT(schemaVersion == kSQLiteSchemaVersion);
    }

    if (vacuumNeeded) {
      QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL("VACUUM;"_ns)));
    }

    if (newDatabase) {
      // Windows caches the file size, let's force it to stat the file again.
      QM_TRY_INSPECT(const bool& exists,
                     MOZ_TO_RESULT_INVOKE_MEMBER(aDBFile, Exists));
      Unused << exists;

      QM_TRY_INSPECT(const int64_t& fileSize,
                     MOZ_TO_RESULT_INVOKE_MEMBER(aDBFile, GetFileSize));

      MOZ_ASSERT(fileSize > 0);

      const PRTime vacuumTime = PR_Now();
      MOZ_ASSERT(vacuumTime);

      QM_TRY_INSPECT(
          const auto& vacuumTimeStmt,
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCOMPtr<mozIStorageStatement>,
                                            connection, CreateStatement,
                                            "UPDATE database "
                                            "SET last_vacuum_time = :time"
                                            ", last_vacuum_size = :size;"_ns));

      QM_TRY(MOZ_TO_RESULT(
          vacuumTimeStmt->BindInt64ByName("time"_ns, vacuumTime)));

      QM_TRY(
          MOZ_TO_RESULT(vacuumTimeStmt->BindInt64ByName("size"_ns, fileSize)));

      QM_TRY(MOZ_TO_RESULT(vacuumTimeStmt->Execute()));
    }
  }

  return connection;
}

template <typename CorruptedFileHandler>
Result<nsCOMPtr<mozIStorageConnection>, nsresult>
CreateStorageConnectionWithRecovery(
    nsIFile& aDBFile, nsIFile& aUsageFile, const nsACString& aOrigin,
    CorruptedFileHandler&& aCorruptedFileHandler) {
  QM_TRY_RETURN(QM_OR_ELSE_WARN_IF(
      // Expression.
      CreateStorageConnection(aDBFile, aUsageFile, aOrigin),
      // Predicate.
      IsDatabaseCorruptionError,
      // Fallback.
      ([&aDBFile, &aUsageFile, &aOrigin,
        &aCorruptedFileHandler](const nsresult rv)
           -> Result<nsCOMPtr<mozIStorageConnection>, nsresult> {
        // Remove the usage file first (it might not exist at all due
        // to corrupted state, which is ignored here).

        // Usually we only use QM_OR_ELSE_LOG_VERBOSE(_IF) with Remove and
        // NS_ERROR_FILE_NOT_FOUND check, but we're already in the rare case
        // of corruption here, so the use of QM_OR_ELSE_WARN_IF is ok here.
        QM_TRY(QM_OR_ELSE_WARN_IF(
            // Expression.
            MOZ_TO_RESULT(aUsageFile.Remove(false)),
            // Predicate.
            ([](const nsresult rv) { return rv == NS_ERROR_FILE_NOT_FOUND; }),
            // Fallback.
            ErrToDefaultOk<>));

        // Call the corrupted file handler before trying to remove the
        // database file, which might fail.
        aCorruptedFileHandler();

        // Nuke the database file.
        QM_TRY(MOZ_TO_RESULT(aDBFile.Remove(false)));

        QM_TRY_RETURN(CreateStorageConnection(aDBFile, aUsageFile, aOrigin));
      })));
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult> GetStorageConnection(
    const nsAString& aDatabaseFilePath) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(!aDatabaseFilePath.IsEmpty());
  MOZ_ASSERT(StringEndsWith(aDatabaseFilePath, u".sqlite"_ns));

  QM_TRY_INSPECT(const auto& databaseFile, QM_NewLocalFile(aDatabaseFilePath));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(databaseFile, Exists));

  QM_TRY(OkIf(exists), Err(NS_ERROR_FAILURE));

  QM_TRY_INSPECT(const auto& ss,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  QM_TRY_UNWRAP(auto connection,
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                    nsCOMPtr<mozIStorageConnection>, ss, OpenDatabase,
                    databaseFile, mozIStorageService::CONNECTION_DEFAULT));

  QM_TRY(MOZ_TO_RESULT(SetDefaultPragmas(connection)));

  return connection;
}

Result<nsCOMPtr<nsIFile>, nsresult> GetArchiveFile(
    const nsAString& aStoragePath) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aStoragePath.IsEmpty());

  QM_TRY_UNWRAP(auto archiveFile, QM_NewLocalFile(aStoragePath));

  QM_TRY(MOZ_TO_RESULT(
      archiveFile->Append(nsLiteralString(LS_ARCHIVE_FILE_NAME))));

  return archiveFile;
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult>
CreateArchiveStorageConnection(const nsAString& aStoragePath) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aStoragePath.IsEmpty());

  QM_TRY_INSPECT(const auto& archiveFile, GetArchiveFile(aStoragePath));

  // QuotaManager ensures this file always exists.
  DebugOnly<bool> exists;
  MOZ_ASSERT(NS_SUCCEEDED(archiveFile->Exists(&exists)));
  MOZ_ASSERT(exists);

  QM_TRY_INSPECT(const bool& isDirectory,
                 MOZ_TO_RESULT_INVOKE_MEMBER(archiveFile, IsDirectory));

  if (isDirectory) {
    LS_WARNING("ls-archive is not a file!");
    return nsCOMPtr<mozIStorageConnection>{};
  }

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
              archiveFile, mozIStorageService::CONNECTION_DEFAULT),
          // Predicate.
          IsDatabaseCorruptionError,
          // Fallback. Don't throw an error, leave a corrupted ls-archive
          // database as it is.
          ErrToDefaultOk<nsCOMPtr<mozIStorageConnection>>));

  if (connection) {
    const nsresult rv = StorageDBUpdater::Update(connection);
    if (NS_FAILED(rv)) {
      // Don't throw an error, leave a non-updateable ls-archive database as
      // it is.
      return nsCOMPtr<mozIStorageConnection>{};
    }
  }

  return connection;
}

Result<nsCOMPtr<nsIFile>, nsresult> GetShadowFile(const nsAString& aBasePath) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(!aBasePath.IsEmpty());

  QM_TRY_UNWRAP(auto archiveFile, QM_NewLocalFile(aBasePath));

  QM_TRY(MOZ_TO_RESULT(
      archiveFile->Append(nsLiteralString(WEB_APPS_STORE_FILE_NAME))));

  return archiveFile;
}

nsresult SetShadowJournalMode(mozIStorageConnection* aConnection) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(aConnection);

  // Try enabling WAL mode. This can fail in various circumstances so we have to
  // check the results here.
  constexpr auto journalModeQueryStart = "PRAGMA journal_mode = "_ns;
  constexpr auto journalModeWAL = "wal"_ns;

  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement(
                     *aConnection, journalModeQueryStart + journalModeWAL));

  QM_TRY_INSPECT(const auto& journalMode,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsAutoCString, *stmt,
                                                   GetUTF8String, 0));

  if (journalMode.Equals(journalModeWAL)) {
    // WAL mode successfully enabled. Set limits on its size here.

    // Set the threshold for auto-checkpointing the WAL. We don't want giant
    // logs slowing down us.
    QM_TRY_INSPECT(const auto& stmt, CreateAndExecuteSingleStepStatement(
                                         *aConnection, "PRAGMA page_size;"_ns));

    QM_TRY_INSPECT(const int32_t& pageSize,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt32, 0));

    MOZ_ASSERT(pageSize >= 512 && pageSize <= 65536);

    // Note there is a default journal_size_limit set by mozStorage.
    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteSimpleSQL(
        "PRAGMA wal_autocheckpoint = "_ns +
        IntToCString(static_cast<int32_t>(kShadowMaxWALSize / pageSize)))));
  } else {
    QM_TRY(MOZ_TO_RESULT(
        aConnection->ExecuteSimpleSQL(journalModeQueryStart + "truncate"_ns)));
  }

  return NS_OK;
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult> CreateShadowStorageConnection(
    const nsAString& aBasePath) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(!aBasePath.IsEmpty());

  QM_TRY_INSPECT(const auto& shadowFile, GetShadowFile(aBasePath));

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
              shadowFile, mozIStorageService::CONNECTION_DEFAULT),
          // Predicate.
          IsDatabaseCorruptionError,
          // Fallback.
          ([&shadowFile, &ss](const nsresult rv)
               -> Result<nsCOMPtr<mozIStorageConnection>, nsresult> {
            QM_TRY(MOZ_TO_RESULT(shadowFile->Remove(false)));

            QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                nsCOMPtr<mozIStorageConnection>, ss, OpenUnsharedDatabase,
                shadowFile, mozIStorageService::CONNECTION_DEFAULT));
          })));

  QM_TRY(MOZ_TO_RESULT(SetShadowJournalMode(connection)));

  // XXX Depending on whether the *first* call to OpenUnsharedDatabase above
  // failed, we (a) might or (b) might not be dealing with a fresh database
  // here. This is confusing, since in a failure of case (a) we would do the
  // same thing again. Probably, the control flow should be changed here so that
  // it's clear we only delete & create a fresh database once. If we still have
  // a failure then, we better give up. Or, if we really want to handle that,
  // the number of 2 retries seems arbitrary, and we should better do this in
  // some loop until a maximum number of retries is reached.
  //
  // Compare this with QuotaManager::CreateLocalStorageArchiveConnection, which
  // actually tracks if the file was removed before, but it's also more
  // complicated than it should be. Maybe these two methods can be merged (which
  // would mean that a parameter must be added that indicates whether it's
  // handling the shadow file or not).
  QM_TRY(QM_OR_ELSE_WARN(
      // Expression.
      MOZ_TO_RESULT(StorageDBUpdater::Update(connection)),
      // Fallback.
      ([&connection, &shadowFile, &ss](const nsresult) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(connection->Close()));
        QM_TRY(MOZ_TO_RESULT(shadowFile->Remove(false)));

        QM_TRY_UNWRAP(connection, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                      nsCOMPtr<mozIStorageConnection>, ss,
                                      OpenUnsharedDatabase, shadowFile,
                                      mozIStorageService::CONNECTION_DEFAULT));

        QM_TRY(MOZ_TO_RESULT(SetShadowJournalMode(connection)));

        QM_TRY(
            MOZ_TO_RESULT(StorageDBUpdater::CreateCurrentSchema(connection)));

        return Ok{};
      })));

  return connection;
}

Result<nsCOMPtr<mozIStorageConnection>, nsresult> GetShadowStorageConnection(
    const nsAString& aBasePath) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aBasePath.IsEmpty());

  QM_TRY_INSPECT(const auto& shadowFile, GetShadowFile(aBasePath));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(shadowFile, Exists));

  QM_TRY(OkIf(exists), Err(NS_ERROR_FAILURE));

  QM_TRY_INSPECT(const auto& ss,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
      nsCOMPtr<mozIStorageConnection>, ss, OpenUnsharedDatabase, shadowFile,
      mozIStorageService::CONNECTION_DEFAULT));
}

nsresult AttachShadowDatabase(const nsAString& aBasePath,
                              mozIStorageConnection* aConnection) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(!aBasePath.IsEmpty());
  MOZ_ASSERT(aConnection);

  QM_TRY_INSPECT(const auto& shadowFile, GetShadowFile(aBasePath));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(shadowFile, Exists));

    MOZ_ASSERT(exists);
  }
#endif

  QM_TRY_INSPECT(const auto& path, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                       nsString, shadowFile, GetPath));

  QM_TRY_INSPECT(const auto& stmt,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConnection,
                     CreateStatement, "ATTACH DATABASE :path AS shadow;"_ns));

  QM_TRY(MOZ_TO_RESULT(stmt->BindStringByName("path"_ns, path)));

  QM_TRY(MOZ_TO_RESULT(stmt->Execute()));

  return NS_OK;
}

nsresult DetachShadowDatabase(mozIStorageConnection* aConnection) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(
      aConnection->ExecuteSimpleSQL("DETACH DATABASE shadow"_ns)));

  return NS_OK;
}

Result<nsCOMPtr<nsIFile>, nsresult> GetUsageFile(
    const nsAString& aDirectoryPath) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(!aDirectoryPath.IsEmpty());

  QM_TRY_UNWRAP(auto usageFile, QM_NewLocalFile(aDirectoryPath));

  QM_TRY(MOZ_TO_RESULT(usageFile->Append(kUsageFileName)));

  return usageFile;
}

Result<nsCOMPtr<nsIFile>, nsresult> GetUsageJournalFile(
    const nsAString& aDirectoryPath) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(!aDirectoryPath.IsEmpty());

  QM_TRY_UNWRAP(auto usageJournalFile, QM_NewLocalFile(aDirectoryPath));

  QM_TRY(MOZ_TO_RESULT(usageJournalFile->Append(kUsageJournalFileName)));

  return usageJournalFile;
}

// Checks if aFile exists and is a file. Returns true if it exists and is a
// file, false if it doesn't exist, and an error if it exists but isn't a file.
Result<bool, nsresult> ExistsAsFile(nsIFile& aFile) {
  enum class ExistsAsFileResult { DoesNotExist, IsDirectory, IsFile };

  // This is an optimization to check both properties in one OS case, rather
  // than calling Exists first, and then IsDirectory. IsDirectory also checks
  // if the path exists. QM_OR_ELSE_WARN_IF is not used here since we just want
  // to log NS_ERROR_FILE_NOT_FOUND result and not spam the reports.
  QM_TRY_INSPECT(
      const auto& res,
      QM_OR_ELSE_LOG_VERBOSE_IF(
          // Expression.
          MOZ_TO_RESULT_INVOKE_MEMBER(aFile, IsDirectory)
              .map([](const bool isDirectory) {
                return isDirectory ? ExistsAsFileResult::IsDirectory
                                   : ExistsAsFileResult::IsFile;
              }),
          // Predicate.
          ([](const nsresult rv) { return rv == NS_ERROR_FILE_NOT_FOUND; }),
          // Fallback.
          ErrToOk<ExistsAsFileResult::DoesNotExist>));

  QM_TRY(OkIf(res != ExistsAsFileResult::IsDirectory), Err(NS_ERROR_FAILURE));

  return res == ExistsAsFileResult::IsFile;
}

nsresult UpdateUsageFile(nsIFile* aUsageFile, nsIFile* aUsageJournalFile,
                         int64_t aUsage) {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(aUsageFile);
  MOZ_ASSERT(aUsageJournalFile);
  MOZ_ASSERT(aUsage >= 0);

  QM_TRY_INSPECT(const bool& usageJournalFileExists,
                 ExistsAsFile(*aUsageJournalFile));
  if (!usageJournalFileExists) {
    QM_TRY(MOZ_TO_RESULT(
        aUsageJournalFile->Create(nsIFile::NORMAL_FILE_TYPE, 0644)));
  }

  QM_TRY_INSPECT(const auto& stream, NS_NewLocalFileOutputStream(aUsageFile));

  nsCOMPtr<nsIBinaryOutputStream> binaryStream =
      NS_NewObjectOutputStream(stream);

  QM_TRY(MOZ_TO_RESULT(binaryStream->Write32(kUsageFileCookie)));

  QM_TRY(MOZ_TO_RESULT(binaryStream->Write64(aUsage)));

#if defined(EARLY_BETA_OR_EARLIER) || defined(DEBUG)
  QM_TRY(MOZ_TO_RESULT(stream->Flush()));
#endif

  QM_TRY(MOZ_TO_RESULT(stream->Close()));

  return NS_OK;
}

Result<UsageInfo, nsresult> LoadUsageFile(nsIFile& aUsageFile) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const int64_t& fileSize,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aUsageFile, GetFileSize));

  QM_TRY(OkIf(fileSize == kUsageFileSize), Err(NS_ERROR_FILE_CORRUPTED));

  QM_TRY_UNWRAP(auto stream, NS_NewLocalFileInputStream(&aUsageFile));

  QM_TRY_INSPECT(const auto& bufferedStream,
                 NS_NewBufferedInputStream(stream.forget(), 16));

  const nsCOMPtr<nsIBinaryInputStream> binaryStream =
      NS_NewObjectInputStream(bufferedStream);

  QM_TRY_INSPECT(const uint32_t& cookie,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read32));

  QM_TRY(OkIf(cookie == kUsageFileCookie), Err(NS_ERROR_FILE_CORRUPTED));

  QM_TRY_INSPECT(const uint64_t& usage,
                 MOZ_TO_RESULT_INVOKE_MEMBER(binaryStream, Read64));

  return UsageInfo{DatabaseUsageType(Some(usage))};
}

/*******************************************************************************
 * Non-actor class declarations
 ******************************************************************************/

/**
 * Coalescing manipulation queue used by `Datastore`.  Used by `Datastore` to
 * update `Datastore::mOrderedItems` efficiently/for code simplification.
 * (Datastore does not actually depend on the coalescing, as mutations are
 * applied atomically when a Snapshot Checkpoints, and with `Datastore::mValues`
 * being updated at the same time the mutations are applied to Datastore's
 * mWriteOptimizer.)
 */
class DatastoreWriteOptimizer final : public LSWriteOptimizer<LSValue> {
 public:
  void ApplyAndReset(nsTArray<LSItemInfo>& aOrderedItems);
};

/**
 * Coalescing manipulation queue used by `Connection`.  Used by `Connection` to
 * buffer and coalesce manipulations applied to the Datastore in batches by
 * Snapshot Checkpointing until flushed to disk.
 */
class ConnectionWriteOptimizer final : public LSWriteOptimizer<LSValue> {
 public:
  // Returns the usage as the success value.
  Result<int64_t, nsresult> Perform(Connection* aConnection,
                                    bool aShadowWrites);

 private:
  /**
   * Handlers for specific mutations.  Each method knows how to `Perform` the
   * manipulation against a `Connection` and the "shadow" database (legacy
   * webappsstore.sqlite database that exists so LSNG can be disabled/safely
   * downgraded from.)
   */
  nsresult PerformInsertOrUpdate(Connection* aConnection, bool aShadowWrites,
                                 const nsAString& aKey, const LSValue& aValue);

  nsresult PerformDelete(Connection* aConnection, bool aShadowWrites,
                         const nsAString& aKey);

  nsresult PerformTruncate(Connection* aConnection, bool aShadowWrites);
};

class DatastoreOperationBase : public Runnable {
  nsCOMPtr<nsIEventTarget> mOwningEventTarget;
  nsresult mResultCode;
  Atomic<bool> mMayProceedOnNonOwningThread;
  bool mMayProceed;

 public:
  nsIEventTarget* OwningEventTarget() const {
    MOZ_ASSERT(mOwningEventTarget);

    return mOwningEventTarget;
  }

  bool IsOnOwningThread() const {
    MOZ_ASSERT(mOwningEventTarget);

    bool current;
    return NS_SUCCEEDED(mOwningEventTarget->IsOnCurrentThread(&current)) &&
           current;
  }

  void AssertIsOnOwningThread() const {
    MOZ_ASSERT(IsOnBackgroundThread());
    MOZ_ASSERT(IsOnOwningThread());
  }

  nsresult ResultCode() const { return mResultCode; }

  void SetFailureCode(nsresult aErrorCode) {
    MOZ_ASSERT(NS_SUCCEEDED(mResultCode));
    MOZ_ASSERT(NS_FAILED(aErrorCode));

    mResultCode = aErrorCode;
  }

  void MaybeSetFailureCode(nsresult aErrorCode) {
    MOZ_ASSERT(NS_FAILED(aErrorCode));

    if (NS_SUCCEEDED(mResultCode)) {
      mResultCode = aErrorCode;
    }
  }

  void NoteComplete() {
    AssertIsOnOwningThread();

    mMayProceed = false;
    mMayProceedOnNonOwningThread = false;
  }

  bool MayProceed() const {
    AssertIsOnOwningThread();

    return mMayProceed;
  }

  // May be called on any thread, but you should call MayProceed() if you know
  // you're on the background thread because it is slightly faster.
  bool MayProceedOnNonOwningThread() const {
    return mMayProceedOnNonOwningThread;
  }

 protected:
  DatastoreOperationBase()
      : Runnable("dom::DatastoreOperationBase"),
        mOwningEventTarget(GetCurrentSerialEventTarget()),
        mResultCode(NS_OK),
        mMayProceedOnNonOwningThread(true),
        mMayProceed(true) {}

  ~DatastoreOperationBase() override { MOZ_ASSERT(!mMayProceed); }
};

class ConnectionDatastoreOperationBase : public DatastoreOperationBase {
 protected:
  RefPtr<Connection> mConnection;
  /**
   * This boolean flag is used by the CloseOp to avoid creating empty databases.
   */
  const bool mEnsureStorageConnection;

 public:
  // This callback will be called on the background thread before releasing the
  // final reference to this request object. Subclasses may perform any
  // additional cleanup here but must always call the base class implementation.
  virtual void Cleanup();

 protected:
  ConnectionDatastoreOperationBase(Connection* aConnection,
                                   bool aEnsureStorageConnection = true);

  ~ConnectionDatastoreOperationBase();

  // Must be overridden in subclasses. Called on the target thread to allow the
  // subclass to perform necessary datastore operations. A successful return
  // value will trigger an OnSuccess callback on the background thread while
  // while a failure value will trigger an OnFailure callback.
  virtual nsresult DoDatastoreWork() = 0;

  // Methods that subclasses may implement.
  virtual void OnSuccess();

  virtual void OnFailure(nsresult aResultCode);

 private:
  void RunOnConnectionThread();

  void RunOnOwningThread();

  // Not to be overridden by subclasses.
  NS_DECL_NSIRUNNABLE
};

class Connection final : public CachingDatabaseConnection {
  friend class ConnectionThread;

  class GetOrCreateTemporaryOriginDirectoryHelper;

  class FlushOp;
  class CloseOp;

  RefPtr<ConnectionThread> mConnectionThread;
  RefPtr<QuotaClient> mQuotaClient;
  nsCOMPtr<nsITimer> mFlushTimer;
  UniquePtr<ArchivedOriginScope> mArchivedOriginScope;
  ConnectionWriteOptimizer mWriteOptimizer;
  // XXX Consider changing this to ClientMetadata.
  const OriginMetadata mOriginMetadata;
  nsString mDirectoryPath;
  /**
   * Propagated from PrepareDatastoreOp. PrepareDatastoreOp may defer the
   * creation of the localstorage client directory and database on the
   * QuotaManager IO thread in its DatabaseWork method to
   * Connection::EnsureStorageConnection, in which case the method needs to know
   * it is responsible for taking those actions (without redundantly performing
   * the existence checks).
   */
  const bool mDatabaseWasNotAvailable;
  bool mHasCreatedDatabase;
  bool mFlushScheduled;
#ifdef DEBUG
  bool mInUpdateBatch;
  bool mFinished;
#endif

 public:
  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::Connection)

  void AssertIsOnOwningThread() const { NS_ASSERT_OWNINGTHREAD(Connection); }

  QuotaClient* GetQuotaClient() const {
    MOZ_ASSERT(mQuotaClient);

    return mQuotaClient;
  }

  ArchivedOriginScope* GetArchivedOriginScope() const {
    return mArchivedOriginScope.get();
  }

  const nsCString& Origin() const { return mOriginMetadata.mOrigin; }

  const nsString& DirectoryPath() const { return mDirectoryPath; }

  void GetFinishInfo(bool& aDatabaseWasNotAvailable,
                     bool& aHasCreatedDatabase) const {
    AssertIsOnOwningThread();
    MOZ_ASSERT(mFinished);

    aDatabaseWasNotAvailable = mDatabaseWasNotAvailable;
    aHasCreatedDatabase = mHasCreatedDatabase;
  }

  //////////////////////////////////////////////////////////////////////////////
  // Methods which can only be called on the owning thread.

  // This method is used to asynchronously execute a connection datastore
  // operation on the connection thread.
  void Dispatch(ConnectionDatastoreOperationBase* aOp);

  // This method is used to asynchronously close the storage connection on the
  // connection thread.
  void Close(nsIRunnable* aCallback);

  void SetItem(const nsString& aKey, const LSValue& aValue, int64_t aDelta,
               bool aIsNewItem);

  void RemoveItem(const nsString& aKey, int64_t aDelta);

  void Clear(int64_t aDelta);

  void BeginUpdateBatch();

  void EndUpdateBatch();

  //////////////////////////////////////////////////////////////////////////////
  // Methods which can only be called on the connection thread.

  nsresult EnsureStorageConnection();

  mozIStorageConnection* StorageConnection() const {
    AssertIsOnGlobalConnectionThread();

    return &MutableStorageConnection();
  }

  void CloseStorageConnection();

  nsresult BeginWriteTransaction();

  nsresult CommitWriteTransaction();

  nsresult RollbackWriteTransaction();

 private:
  // Only created by ConnectionThread.
  Connection(ConnectionThread* aConnectionThread,
             const OriginMetadata& aOriginMetadata,
             UniquePtr<ArchivedOriginScope>&& aArchivedOriginScope,
             bool aDatabaseWasNotAvailable);

  ~Connection();

  void ScheduleFlush();

  void Flush();

  static void FlushTimerCallback(nsITimer* aTimer, void* aClosure);
};

/**
 * Helper to invoke GetOrCreateTemporaryOriginDirectory on the QuotaManager IO
 * thread from the LocalStorage connection thread when creating a database
 * connection on demand. This is necessary because we attempt to defer the
 * creation of the origin directory and the database until absolutely needed,
 * but the directory creation must happen on the QM IO thread for invariant
 * reasons. (We can't just use a mutex because there could be logic on the IO
 * thread that also wants to deal with the same origin, so we need to queue a
 * runnable and wait our turn.)
 */
class Connection::GetOrCreateTemporaryOriginDirectoryHelper final
    : public Runnable {
  mozilla::Monitor mMonitor MOZ_UNANNOTATED;
  const OriginMetadata mOriginMetadata;
  nsString mOriginDirectoryPath;
  nsresult mIOThreadResultCode;
  bool mWaiting;

 public:
  explicit GetOrCreateTemporaryOriginDirectoryHelper(
      const OriginMetadata& aOriginMetadata)
      : Runnable(
            "dom::localstorage::Connection::"
            "GetOrCreateTemporaryOriginDirectoryHelper"),
        mMonitor("GetOrCreateTemporaryOriginDirectoryHelper::mMonitor"),
        mOriginMetadata(aOriginMetadata),
        mIOThreadResultCode(NS_OK),
        mWaiting(true) {
    AssertIsOnGlobalConnectionThread();
  }

  Result<nsString, nsresult> BlockAndReturnOriginDirectoryPath();

 private:
  ~GetOrCreateTemporaryOriginDirectoryHelper() = default;

  nsresult RunOnIOThread();

  NS_DECL_NSIRUNNABLE
};

class Connection::FlushOp final : public ConnectionDatastoreOperationBase {
  ConnectionWriteOptimizer mWriteOptimizer;
  bool mShadowWrites;

 public:
  FlushOp(Connection* aConnection, ConnectionWriteOptimizer&& aWriteOptimizer);

 private:
  nsresult DoDatastoreWork() override;

  void Cleanup() override;
};

class Connection::CloseOp final : public ConnectionDatastoreOperationBase {
  nsCOMPtr<nsIRunnable> mCallback;

 public:
  CloseOp(Connection* aConnection, nsIRunnable* aCallback)
      : ConnectionDatastoreOperationBase(aConnection,
                                         /* aEnsureStorageConnection */ false),
        mCallback(aCallback) {}

 private:
  nsresult DoDatastoreWork() override;

  void Cleanup() override;
};

class ConnectionThread final {
  friend class Connection;

  nsCOMPtr<nsIThread> mThread;
  nsRefPtrHashtable<nsCStringHashKey, Connection> mConnections;

 public:
  ConnectionThread();

  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(ConnectionThread);
  }

  bool IsOnConnectionThread();

  void AssertIsOnConnectionThread();

  already_AddRefed<Connection> CreateConnection(
      const OriginMetadata& aOriginMetadata,
      UniquePtr<ArchivedOriginScope>&& aArchivedOriginScope,
      bool aDatabaseWasNotAvailable);

  void Shutdown();

  NS_INLINE_DECL_REFCOUNTING(ConnectionThread)

 private:
  ~ConnectionThread();
};

/**
 * Canonical state of Storage for an origin, containing all keys and their
 * values in the parent process.  Specifically, this is the state that will
 * be handed out to freshly created Snapshots and that will be persisted to disk
 * when the Connection's flush completes.  State is mutated in batches as
 * Snapshot instances Checkpoint their mutations locally accumulated in the
 * child LSSnapshots.
 */
class Datastore final
    : public SupportsCheckedUnsafePtr<CheckIf<DiagnosticAssertEnabled>> {
  ClientDirectoryLockHandle mDirectoryLockHandle;
  RefPtr<Connection> mConnection;
  RefPtr<QuotaObject> mQuotaObject;
  nsCOMPtr<nsIRunnable> mCompleteCallback;
  /**
   * PrepareDatastoreOps register themselves with the Datastore at
   * and unregister in PrepareDatastoreOp::Cleanup.
   */
  nsTHashSet<PrepareDatastoreOp*> mPrepareDatastoreOps;
  /**
   * PreparedDatastore instances register themselves with their associated
   * Datastore at construction time and unregister at destruction time.  They
   * hang around for kPreparedDatastoreTimeoutMs in order to keep the Datastore
   * from closing itself via MaybeClose(), thereby giving the document enough
   * time to load and access LocalStorage.
   */
  nsTHashSet<PreparedDatastore*> mPreparedDatastores;
  /**
   * A database is live (and in this hashtable) if it has a live LSDatabase
   * actor.  There is at most one Database per origin per content process.  Each
   * Database corresponds to an LSDatabase in its associated content process.
   */
  nsTHashSet<Database*> mDatabases;
  /**
   * A database is active if it has a non-null `mSnapshot`.  As long as there
   * are any active databases final deltas can't be calculated and
   * `UpdateUsage()` can't be invoked.
   */
  nsTHashSet<Database*> mActiveDatabases;
  /**
   * Non-authoritative hashtable representation of mOrderedItems for efficient
   * lookup.
   */
  nsTHashMap<nsStringHashKey, LSValue> mValues;
  /**
   * The authoritative ordered state of the Datastore; mValue also exists as an
   * unordered hashtable for efficient lookup.
   */
  nsTArray<LSItemInfo> mOrderedItems;
  nsTArray<int64_t> mPendingUsageDeltas;
  DatastoreWriteOptimizer mWriteOptimizer;
  const OriginMetadata mOriginMetadata;
  const uint32_t mPrivateBrowsingId;
  int64_t mUsage;
  int64_t mUpdateBatchUsage;
  int64_t mSizeOfKeys;
  int64_t mSizeOfItems;
  bool mClosed;
  bool mInUpdateBatch;
  bool mHasLivePrivateDatastore;

 public:
  // Created by PrepareDatastoreOp.
  Datastore(const OriginMetadata& aOriginMetadata, uint32_t aPrivateBrowsingId,
            int64_t aUsage, int64_t aSizeOfKeys, int64_t aSizeOfItems,
            ClientDirectoryLockHandle&& aDirectoryLockHandle,
            RefPtr<Connection>&& aConnection,
            RefPtr<QuotaObject>&& aQuotaObject,
            nsTHashMap<nsStringHashKey, LSValue>& aValues,
            nsTArray<LSItemInfo>&& aOrderedItems);

  Maybe<ClientDirectoryLock&> MaybeDirectoryLockRef() const {
    AssertIsOnBackgroundThread();

    return ToMaybeRef(mDirectoryLockHandle.get());
  }

  const nsCString& Origin() const { return mOriginMetadata.mOrigin; }

  uint32_t PrivateBrowsingId() const { return mPrivateBrowsingId; }

  bool IsPersistent() const {
    // Private-browsing is forbidden from touching disk.
    return mPrivateBrowsingId == 0;
  }

  void Close();

  bool IsClosed() const {
    AssertIsOnBackgroundThread();

    return mClosed;
  }

  void WaitForConnectionToComplete(nsIRunnable* aCallback);

  void NoteLivePrepareDatastoreOp(PrepareDatastoreOp* aPrepareDatastoreOp);

  void NoteFinishedPrepareDatastoreOp(PrepareDatastoreOp* aPrepareDatastoreOp);

  void NoteLivePrivateDatastore();

  void NoteFinishedPrivateDatastore();

  void NoteLivePreparedDatastore(PreparedDatastore* aPreparedDatastore);

  void NoteFinishedPreparedDatastore(PreparedDatastore* aPreparedDatastore);

  bool HasOtherProcessDatabases(Database* aDatabase);

  void NoteLiveDatabase(Database* aDatabase);

  void NoteFinishedDatabase(Database* aDatabase);

  void NoteActiveDatabase(Database* aDatabase);

  void NoteInactiveDatabase(Database* aDatabase);

  void GetSnapshotLoadInfo(const nsAString& aKey, bool& aAddKeyToUnknownItems,
                           nsTHashtable<nsStringHashKey>& aLoadedItems,
                           nsTArray<LSItemInfo>& aItemInfos,
                           uint32_t& aNextLoadIndex,
                           LSSnapshot::LoadState& aLoadState);

  uint32_t GetLength() const { return mValues.Count(); }

  const nsTArray<LSItemInfo>& GetOrderedItems() const { return mOrderedItems; }

  void GetItem(const nsAString& aKey, LSValue& aValue) const;

  void GetKeys(nsTArray<nsString>& aKeys) const;

  //////////////////////////////////////////////////////////////////////////////
  // Mutation Methods
  //
  // These are only called during Snapshot::Checkpoint

  /**
   * Used by Snapshot::Checkpoint to set a key/value pair as part of an
   * explicit batch.
   */
  void SetItem(Database* aDatabase, const nsString& aKey,
               const LSValue& aValue);

  void RemoveItem(Database* aDatabase, const nsString& aKey);

  void Clear(Database* aDatabase);

  void BeginUpdateBatch(int64_t aSnapshotUsage);

  int64_t EndUpdateBatch(int64_t aSnapshotPeakUsage);

  int64_t GetUsage() const { return mUsage; }

  int64_t AttemptToUpdateUsage(int64_t aMinSize, bool aInitial);

  bool HasOtherProcessObservers(Database* aDatabase);

  void NotifyOtherProcessObservers(Database* aDatabase,
                                   const nsString& aDocumentURI,
                                   const nsString& aKey,
                                   const LSValue& aOldValue,
                                   const LSValue& aNewValue);

  void NoteChangedObserverArray(const nsTArray<NotNull<Observer*>>& aObservers);

  void Stringify(nsACString& aResult) const;

  NS_INLINE_DECL_REFCOUNTING(Datastore)

 private:
  // Reference counted.
  ~Datastore();

  bool UpdateUsage(int64_t aDelta);

  void MaybeClose();

  void ConnectionClosedCallback();

  void CleanupMetadata();

  void NotifySnapshots(Database* aDatabase, const nsAString& aKey,
                       const LSValue& aOldValue, bool aAffectsOrder);

  void NoteChangedDatabaseMap();
};

class PrivateDatastore {
  const NotNull<RefPtr<Datastore>> mDatastore;

 public:
  explicit PrivateDatastore(MovingNotNull<RefPtr<Datastore>> aDatastore)
      : mDatastore(std::move(aDatastore)) {
    AssertIsOnBackgroundThread();

    mDatastore->NoteLivePrivateDatastore();
  }

  ~PrivateDatastore() { mDatastore->NoteFinishedPrivateDatastore(); }

  const Datastore& DatastoreRef() const {
    AssertIsOnBackgroundThread();

    return *mDatastore;
  }

  Datastore& MutableDatastoreRef() const {
    AssertIsOnBackgroundThread();

    return *mDatastore;
  }
};

class PreparedDatastore {
  RefPtr<Datastore> mDatastore;
  nsCOMPtr<nsITimer> mTimer;
  const Maybe<ContentParentId> mContentParentId;
  // Strings share buffers if possible, so it's not a problem to duplicate the
  // origin here.
  const nsCString mOrigin;
  uint64_t mDatastoreId;
  bool mForPreload;
  bool mInvalidated;

 public:
  PreparedDatastore(Datastore* aDatastore,
                    const Maybe<ContentParentId>& aContentParentId,
                    const nsACString& aOrigin, uint64_t aDatastoreId,
                    bool aForPreload)
      : mDatastore(aDatastore),
        mTimer(NS_NewTimer()),
        mContentParentId(aContentParentId),
        mOrigin(aOrigin),
        mDatastoreId(aDatastoreId),
        mForPreload(aForPreload),
        mInvalidated(false) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(aDatastore);
    MOZ_ASSERT(mTimer);

    aDatastore->NoteLivePreparedDatastore(this);

    MOZ_ALWAYS_SUCCEEDS(mTimer->InitWithNamedFuncCallback(
        TimerCallback, this, kPreparedDatastoreTimeoutMs,
        nsITimer::TYPE_ONE_SHOT, "PreparedDatastore::TimerCallback"));
  }

  ~PreparedDatastore() {
    MOZ_ASSERT(mDatastore);
    MOZ_ASSERT(mTimer);

    mTimer->Cancel();

    mDatastore->NoteFinishedPreparedDatastore(this);
  }

  const Datastore& DatastoreRef() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mDatastore);

    return *mDatastore;
  }

  Datastore& MutableDatastoreRef() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mDatastore);

    return *mDatastore;
  }

  const Maybe<ContentParentId>& GetContentParentId() const {
    return mContentParentId;
  }

  const nsCString& Origin() const { return mOrigin; }

  void Invalidate() {
    AssertIsOnBackgroundThread();

    mInvalidated = true;

    if (mForPreload) {
      mTimer->Cancel();

      MOZ_ALWAYS_SUCCEEDS(mTimer->InitWithNamedFuncCallback(
          TimerCallback, this, 0, nsITimer::TYPE_ONE_SHOT,
          "PreparedDatastore::TimerCallback"));
    }
  }

  bool IsInvalidated() const {
    AssertIsOnBackgroundThread();

    return mInvalidated;
  }

 private:
  void Destroy();

  static void TimerCallback(nsITimer* aTimer, void* aClosure);
};

/*******************************************************************************
 * Actor class declarations
 ******************************************************************************/

class Database final
    : public PBackgroundLSDatabaseParent,
      public SupportsCheckedUnsafePtr<CheckIf<DiagnosticAssertEnabled>> {
  RefPtr<Datastore> mDatastore;
  Snapshot* mSnapshot;
  const PrincipalInfo mPrincipalInfo;
  const Maybe<ContentParentId> mContentParentId;
  // Strings share buffers if possible, so it's not a problem to duplicate the
  // origin here.
  nsCString mOrigin;
  mozilla::glean::TimerId mRequestAllowToCloseTimerId;
  uint32_t mPrivateBrowsingId;
  bool mAllowedToClose;
  bool mActorDestroyed;
  bool mRequestedAllowToClose;
#ifdef DEBUG
  bool mActorWasAlive;
#endif

 public:
  // Created in AllocPBackgroundLSDatabaseParent.
  Database(const PrincipalInfo& aPrincipalInfo,
           const Maybe<ContentParentId>& aContentParentId,
           const nsACString& aOrigin, uint32_t aPrivateBrowsingId);

  void AssertIsOnOwningThread() const {
    AssertIsOnBackgroundThread();
    NS_ASSERT_OWNINGTHREAD(mozilla::dom::Database);
  }

  Datastore* GetDatastore() const {
    AssertIsOnOwningThread();
    return mDatastore;
  }

  Maybe<Datastore&> MaybeDatastoreRef() const {
    AssertIsOnOwningThread();

    return ToMaybeRef(mDatastore.get());
  }

  const PrincipalInfo& GetPrincipalInfo() const { return mPrincipalInfo; }

  const Maybe<ContentParentId>& ContentParentIdRef() const {
    return mContentParentId;
  }

  uint32_t PrivateBrowsingId() const { return mPrivateBrowsingId; }

  const nsCString& Origin() const { return mOrigin; }

  void SetActorAlive(Datastore* aDatastore);

  void RegisterSnapshot(Snapshot* aSnapshot);

  void UnregisterSnapshot(Snapshot* aSnapshot);

  Snapshot* GetSnapshot() const {
    AssertIsOnOwningThread();
    return mSnapshot;
  }

  void RequestAllowToClose();

  void ForceKill();

  void Stringify(nsACString& aResult) const;

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::Database, override)

 private:
  // Reference counted.
  ~Database();

  void AllowToClose();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvAllowToClose() override;

  PBackgroundLSSnapshotParent* AllocPBackgroundLSSnapshotParent(
      const nsAString& aDocumentURI, const nsAString& aKey,
      const bool& aIncreasePeakUsage, const int64_t& aMinSize,
      LSSnapshotInitInfo* aInitInfo) override;

  mozilla::ipc::IPCResult RecvPBackgroundLSSnapshotConstructor(
      PBackgroundLSSnapshotParent* aActor, const nsAString& aDocumentURI,
      const nsAString& aKey, const bool& aIncreasePeakUsage,
      const int64_t& aMinSize, LSSnapshotInitInfo* aInitInfo) override;

  bool DeallocPBackgroundLSSnapshotParent(
      PBackgroundLSSnapshotParent* aActor) override;
};

/**
 * Attempts to capture the state of the underlying Datastore at the time of its
 * creation so run-to-completion semantics can be honored.
 *
 * Rather than simply duplicate the contents of `DataStore::mValues` and
 * `Datastore::mOrderedItems` at the time of their creation, the Snapshot tracks
 * mutations to the Datastore as they happen, saving off the state of values as
 * they existed when the Snapshot was created.  In other words, given an initial
 * Datastore state of { foo: 'bar', bar: 'baz' }, the Snapshot won't store those
 * values until it hears via `SaveItem` that "foo" is being over-written.  At
 * that time, it will save off foo='bar' in mValues.
 *
 * ## Quota Allocation ##
 *
 * ## States ##
 *
 */
class Snapshot final : public PBackgroundLSSnapshotParent {
  /**
   * The Database that owns this snapshot.  There is a 1:1 relationship between
   * snapshots and databases.
   */
  RefPtr<Database> mDatabase;
  RefPtr<Datastore> mDatastore;
  /**
   * The set of keys for which values have been sent to the child LSSnapshot.
   * Cleared once all values have been sent as indicated by
   * mLoadedItems.Count()==mTotalLength and therefore mLoadedAllItems should be
   * true.  No requests should be received for keys already in this set, and
   * this is enforced by fatal IPC error (unless fuzzing).
   */
  nsTHashtable<nsStringHashKey> mLoadedItems;
  /**
   * The set of keys for which a RecvLoadValueAndMoreItems request was received
   * but there was no such key, and so null was returned.  The child LSSnapshot
   * will also cache these values, so redundant requests are also handled with
   * fatal process termination just like for mLoadedItems.  Also cleared when
   * mLoadedAllItems becomes true because then the child can infer that all
   * other values must be null.  (Note: this could also be done when
   * mLoadKeysReceived is true as a further optimization, but is not.)
   */
  nsTHashSet<nsString> mUnknownItems;
  /**
   * Values that have changed in mDatastore as reported by SaveItem
   * notifications that are not yet known to the child LSSnapshot.
   *
   * The naive way to snapshot the state of mDatastore would be to duplicate its
   * internal mValues at the time of our creation, but that is wasteful if few
   * changes are made to the Datastore's state.  So we only track values that
   * are changed/evicted from the Datastore as they happen, as reported to us by
   * SaveItem notifications.
   */
  nsTHashMap<nsStringHashKey, LSValue> mValues;
  /**
   * Latched state of mDatastore's keys during a SaveItem notification with
   * aAffectsOrder=true.  The ordered keys needed to be saved off so that a
   * consistent ordering could be presented to the child LSSnapshot when it asks
   * for them via RecvLoadKeys.
   */
  nsTArray<nsString> mKeys;
  nsString mDocumentURI;
  /**
   * The index used for restoring iteration over not yet sent key/value pairs to
   * the child LSSnapshot.
   */
  uint32_t mNextLoadIndex;
  /**
   * The number of key/value pairs that were present in the Datastore at the
   * time the snapshot was created.  Once we have sent this many values to the
   * child LSSnapshot, we can infer that it has received all of the keys/values
   * and set mLoadedAllItems to true and clear mLoadedItems and mUnknownItems.
   * Note that knowing the keys/values is not the same as knowing their ordering
   * and so mKeys may be retained.
   */
  uint32_t mTotalLength;
  int64_t mUsage;
  int64_t mPeakUsage;
  /**
   * True if SaveItem has saved mDatastore's keys into mKeys because a SaveItem
   * notification with aAffectsOrder=true was received.
   */
  bool mSavedKeys;
  bool mActorDestroyed;
  bool mFinishReceived;
  bool mLoadedReceived;
  /**
   * True if LSSnapshot's mLoadState should be LoadState::AllOrderedItems or
   * LoadState::AllUnorderedItems.  It will be AllOrderedItems if the initial
   * snapshot contained all the data or if the state was AllOrderedKeys and
   * successive RecvLoadValueAndMoreItems requests have resulted in the
   * LSSnapshot being told all of the key/value pairs.  It will be
   * AllUnorderedItems if the state was LoadState::Partial and successive
   * RecvLoadValueAndMoreItem requests got all the keys/values but the key
   * ordering was not retrieved.
   */
  bool mLoadedAllItems;
  /**
   * True if LSSnapshot's mLoadState should be LoadState::AllOrderedItems or
   * AllOrderedKeys.  This can occur because of the initial snapshot, or because
   * a RecvLoadKeys request was received.
   */
  bool mLoadKeysReceived;
  bool mSentMarkDirty;

  /**
   * True if there are Database objects in other content processes. The value
   * never gets updated, we instead mark snapshots as dirty when Database
   * objects are added or removed. Marking snapshots as dirty forces creation
   * of new snapshots for new tasks.
   */
  bool mHasOtherProcessDatabases;
  bool mHasOtherProcessObservers;

 public:
  // Created in AllocPBackgroundLSSnapshotParent.
  Snapshot(Database* aDatabase, const nsAString& aDocumentURI);

  void Init(nsTHashtable<nsStringHashKey>& aLoadedItems,
            nsTHashSet<nsString>&& aUnknownItems, uint32_t aNextLoadIndex,
            uint32_t aTotalLength, int64_t aUsage, int64_t aPeakUsage,
            LSSnapshot::LoadState aLoadState, bool aHasOtherProcessDatabases,
            bool aHasOtherProcessObservers) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(aUsage >= 0);
    MOZ_ASSERT(aPeakUsage >= aUsage);
    MOZ_ASSERT_IF(aLoadState != LSSnapshot::LoadState::AllOrderedItems,
                  aNextLoadIndex < aTotalLength);
    MOZ_ASSERT(mTotalLength == 0);
    MOZ_ASSERT(mUsage == -1);
    MOZ_ASSERT(mPeakUsage == -1);

    mLoadedItems.SwapElements(aLoadedItems);
    mUnknownItems = std::move(aUnknownItems);
    mNextLoadIndex = aNextLoadIndex;
    mTotalLength = aTotalLength;
    mUsage = aUsage;
    mPeakUsage = aPeakUsage;
    if (aLoadState == LSSnapshot::LoadState::AllOrderedKeys) {
      MOZ_ASSERT(mUnknownItems.Count() == 0);
      mLoadKeysReceived = true;
    } else if (aLoadState == LSSnapshot::LoadState::AllOrderedItems) {
      MOZ_ASSERT(mLoadedItems.Count() == 0);
      MOZ_ASSERT(mUnknownItems.Count() == 0);
      MOZ_ASSERT(mNextLoadIndex == mTotalLength);
      mLoadedReceived = true;
      mLoadedAllItems = true;
      mLoadKeysReceived = true;
    }
    mHasOtherProcessDatabases = aHasOtherProcessDatabases;
    mHasOtherProcessObservers = aHasOtherProcessObservers;
  }

  /**
   * Called via NotifySnapshots by Datastore whenever it is updating its
   * internal state so that snapshots can save off the state of a value at the
   * time of their creation.
   */
  void SaveItem(const nsAString& aKey, const LSValue& aOldValue,
                bool aAffectsOrder);

  void MarkDirty();

  bool IsDirty() const {
    AssertIsOnBackgroundThread();

    return mSentMarkDirty;
  }

  bool HasOtherProcessDatabases() const {
    AssertIsOnBackgroundThread();

    return mHasOtherProcessDatabases;
  }

  bool HasOtherProcessObservers() const {
    AssertIsOnBackgroundThread();

    return mHasOtherProcessObservers;
  }

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::Snapshot)

 private:
  // Reference counted.
  ~Snapshot();

  mozilla::ipc::IPCResult Checkpoint(nsTArray<LSWriteInfo>&& aWriteInfos);

  mozilla::ipc::IPCResult CheckpointAndNotify(
      nsTArray<LSWriteAndNotifyInfo>&& aWriteAndNotifyInfos);

  void Finish();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvAsyncCheckpoint(
      nsTArray<LSWriteInfo>&& aWriteInfos) override;

  mozilla::ipc::IPCResult RecvAsyncCheckpointAndNotify(
      nsTArray<LSWriteAndNotifyInfo>&& aWriteAndNotifyInfos) override;

  mozilla::ipc::IPCResult RecvSyncCheckpoint(
      nsTArray<LSWriteInfo>&& aWriteInfos) override;

  mozilla::ipc::IPCResult RecvSyncCheckpointAndNotify(
      nsTArray<LSWriteAndNotifyInfo>&& aWriteAndNotifyInfos) override;

  mozilla::ipc::IPCResult RecvAsyncFinish() override;

  mozilla::ipc::IPCResult RecvSyncFinish() override;

  mozilla::ipc::IPCResult RecvLoaded() override;

  mozilla::ipc::IPCResult RecvLoadValueAndMoreItems(
      const nsAString& aKey, LSValue* aValue,
      nsTArray<LSItemInfo>* aItemInfos) override;

  mozilla::ipc::IPCResult RecvLoadKeys(nsTArray<nsString>* aKeys) override;

  mozilla::ipc::IPCResult RecvIncreasePeakUsage(const int64_t& aMinSize,
                                                int64_t* aSize) override;
};

class Observer final : public PBackgroundLSObserverParent {
  const Maybe<ContentParentId> mContentParentId;
  nsCString mOrigin;
  bool mActorDestroyed;

 public:
  // Created in AllocPBackgroundLSObserverParent.
  Observer(const Maybe<ContentParentId>& aContentParentId,
           const nsACString& aOrigin);

  const Maybe<ContentParentId>& ContentParentIdRef() const {
    return mContentParentId;
  }

  const nsCString& Origin() const { return mOrigin; }

  void Observe(Database* aDatabase, const nsString& aDocumentURI,
               const nsString& aKey, const LSValue& aOldValue,
               const LSValue& aNewValue);

  NS_INLINE_DECL_REFCOUNTING(mozilla::dom::Observer)

 private:
  // Reference counted.
  ~Observer();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;
};

class LSRequestBase : public DatastoreOperationBase,
                      public PBackgroundLSRequestParent {
 protected:
  enum class State {
    // Just created on the PBackground thread. Next step is StartingRequest.
    Initial,

    // Waiting to start/starting request on the PBackground thread. Next step is
    // either Nesting if a subclass needs to process more nested states or
    // SendingReadyMessage if a subclass doesn't need any nested processing.
    StartingRequest,

    // Doing nested processing.
    Nesting,

    // Waiting to send/sending the ready message on the PBackground thread. Next
    // step is WaitingForFinish.
    SendingReadyMessage,

    // Waiting for the finish message on the PBackground thread. Next step is
    // SendingResults.
    WaitingForFinish,

    // Waiting to send/sending results on the PBackground thread. Next step is
    // Completed.
    SendingResults,

    // All done.
    Completed
  };

  const LSRequestParams mParams;
  Maybe<ContentParentId> mContentParentId;
  State mState;
  bool mWaitingForFinish;

 public:
  LSRequestBase(const LSRequestParams& aParams,
                const Maybe<ContentParentId>& aContentParentId);

  void Dispatch();

  void StringifyState(nsACString& aResult) const;

  virtual void Stringify(nsACString& aResult) const;

  virtual void Log();

 protected:
  ~LSRequestBase() override;

  virtual nsresult Start() = 0;

  virtual nsresult NestedRun();

  virtual void GetResponse(LSRequestResponse& aResponse) = 0;

  virtual void Cleanup() {}

 private:
  bool VerifyRequestParams();

  nsresult StartRequest();

  void SendReadyMessage();

  nsresult SendReadyMessageInternal();

  void Finish();

  void FinishInternal();

  void SendResults();

 protected:
  // Common nsIRunnable implementation that subclasses may not override.
  NS_IMETHOD
  Run() final;

  // IPDL methods.
  void ActorDestroy(ActorDestroyReason aWhy) override;

 private:
  mozilla::ipc::IPCResult RecvCancel() final;

  mozilla::ipc::IPCResult RecvFinish() final;
};

template <typename T>
class SerializedOp {
 protected:
  void AddBlockingOp(T& aOp) { mBlocking.AppendElement(WrapNotNull(&aOp)); }

  void AddBlockedOnOp(T& aOp) { mBlockedOn.AppendElement(WrapNotNull(&aOp)); }

  void MaybeUnblock(T& aOp) {
    mBlockedOn.RemoveElement(&aOp);
    if (mBlockedOn.IsEmpty()) {
      Unblock();
    }
  }

  virtual void Unblock() = 0;

  nsTArray<NotNull<RefPtr<T>>> mBlocking;
  nsTArray<NotNull<RefPtr<T>>> mBlockedOn;
};

class PrepareDatastoreOp
    : public LSRequestBase,
      public SerializedOp<PrepareDatastoreOp>,
      public SupportsCheckedUnsafePtr<CheckIf<DiagnosticAssertEnabled>> {
  class LoadDataOp;

  class CompressFunction;
  class CompressionTypeFunction;

  enum class NestedState {
    // The nesting has not yet taken place. Next step is
    // CheckExistingOperations.
    BeforeNesting,

    // Starting directory opening on the PBackground thread. The next step is
    // DirectoryOpenPending.
    OpenDirectory,

    // Waiting for directory open allowed on the PBackground thread. The next
    // step is either SendingReadyMessage if directory lock failed to acquire,
    // or CheckExistingOperations if directory lock is acquired.
    DirectoryOpenPending,

    // Checking if a prepare datastore operation is already running for given
    // origin on the PBackground thread. Next step is CheckClosingDatastore.
    CheckExistingOperations,

    // Checking if a datastore is closing the connection for given origin on
    // the PBackground thread. Next step is PreparationPending.
    CheckClosingDatastore,

    // Ensuring quota manager is created and opening directory on the
    // PBackground thread. Next step is either SendingResults if quota manager
    // is not available or DatabaseWorkOpen if quota manager is available. If
    // a datastore already exists for given origin then the next state is
    // SendingReadyMessage.
    PreparationPending,

    // Waiting to do/doing work on the QuotaManager IO thread. Its next step is
    // BeginLoadData.
    DatabaseWorkOpen,

    // Starting a load data operation on the PBackground thread. Next step is
    // DatabaseWorkLoadData.
    BeginLoadData,

    // Waiting to do/doing work on the connection thread. This involves waiting
    // for the LoadDataOp to do its work. Eventually the state will transition
    // to SendingReadyMessage.
    DatabaseWorkLoadData,

    // The nesting has completed.
    AfterNesting
  };

  mozilla::glean::TimerId mProcessingTimerId;
  RefPtr<ClientDirectoryLock> mPendingDirectoryLock;
  ClientDirectoryLockHandle mDirectoryLockHandle;
  ClientDirectoryLockHandle mExtraDirectoryLockHandle;
  RefPtr<Connection> mConnection;
  RefPtr<Datastore> mDatastore;
  UniquePtr<ArchivedOriginScope> mArchivedOriginScope;
  LoadDataOp* mLoadDataOp;
  nsTHashMap<nsStringHashKey, LSValue> mValues;
  nsTArray<LSItemInfo> mOrderedItems;
  OriginMetadata mOriginMetadata;
  nsCString mMainThreadOrigin;
  nsString mDatabaseFilePath;
  uint32_t mPrivateBrowsingId;
  int64_t mUsage;
  int64_t mSizeOfKeys;
  int64_t mSizeOfItems;
  uint64_t mDatastoreId;
  NestedState mNestedState;
  const bool mForPreload;
  const bool mEnableMigration;
  bool mDatabaseNotAvailable;
  // Set when the Datastore has been registered with gPrivateDatastores so that
  // it can be unregistered if an error is encountered in PrepareDatastoreOp.
  FlippedOnce<false> mPrivateDatastoreRegistered;
  // Set when the Datastore has been registered with gPreparedDatastores so
  // that it can be unregistered if an error is encountered in
  // PrepareDatastoreOp.
  FlippedOnce<false> mPreparedDatastoreRegistered;
  bool mInvalidated;

#ifdef DEBUG
  int64_t mDEBUGUsage;
#endif

 public:
  PrepareDatastoreOp(const LSRequestParams& aParams,
                     const Maybe<ContentParentId>& aContentParentId);

  Maybe<ClientDirectoryLock&> MaybeDirectoryLockRef() const {
    AssertIsOnBackgroundThread();

    if (mDirectoryLockHandle) {
      return SomeRef(*mDirectoryLockHandle);
    }
    if (mExtraDirectoryLockHandle) {
      return SomeRef(*mExtraDirectoryLockHandle);
    }
    return Nothing();
  }

  bool OriginIsKnown() const {
    MOZ_ASSERT(IsOnOwningThread() || IsOnIOThread());

    return !mOriginMetadata.mOrigin.IsEmpty();
  }

  const nsCString& Origin() const {
    MOZ_ASSERT(IsOnOwningThread() || IsOnIOThread());
    MOZ_ASSERT(OriginIsKnown());

    return mOriginMetadata.mOrigin;
  }

  void Invalidate() {
    AssertIsOnOwningThread();

    mInvalidated = true;
  }

  void StringifyNestedState(nsACString& aResult) const;

  void Stringify(nsACString& aResult) const override;

  void Log() override;

 private:
  ~PrepareDatastoreOp() override;

  nsresult Start() override;

  nsresult OpenDirectory();

  void Unblock() override {
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(this));
  }

  nsresult CheckExistingOperations();

  nsresult CheckClosingDatastoreInternal();

  nsresult CheckClosingDatastore();

  nsresult BeginDatastorePreparationInternal();

  nsresult BeginDatastorePreparation();

  void SendToIOThread();

  nsresult DatabaseWork();

  nsresult DatabaseNotAvailable();

  nsresult EnsureDirectoryEntry(nsIFile* aEntry, bool aCreateIfNotExists,
                                bool aDirectory,
                                bool* aAlreadyExisted = nullptr);

  nsresult VerifyDatabaseInformation(mozIStorageConnection* aConnection);

  already_AddRefed<QuotaObject> GetQuotaObject();

  nsresult BeginLoadData();

  void FinishNesting();

  nsresult FinishNestingOnNonOwningThread();

  nsresult NestedRun() override;

  void GetResponse(LSRequestResponse& aResponse) override;

  void Cleanup() override;

  void ConnectionClosedCallback();

  void CleanupMetadata();

  // IPDL overrides.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  void DirectoryLockAcquired(ClientDirectoryLockHandle aLockHandle);

  void DirectoryLockFailed();
};

class PrepareDatastoreOp::LoadDataOp final
    : public ConnectionDatastoreOperationBase {
  RefPtr<PrepareDatastoreOp> mPrepareDatastoreOp;

 public:
  explicit LoadDataOp(PrepareDatastoreOp* aPrepareDatastoreOp)
      : ConnectionDatastoreOperationBase(aPrepareDatastoreOp->mConnection),
        mPrepareDatastoreOp(aPrepareDatastoreOp) {}

 private:
  ~LoadDataOp() = default;

  nsresult DoDatastoreWork() override;

  void OnSuccess() override;

  void OnFailure(nsresult aResultCode) override;

  void Cleanup() override;
};

class PrepareDatastoreOp::CompressFunction final : public mozIStorageFunction {
 private:
  ~CompressFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

class PrepareDatastoreOp::CompressionTypeFunction final
    : public mozIStorageFunction {
 private:
  ~CompressionTypeFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

class PrepareObserverOp : public LSRequestBase {
  nsCString mOrigin;

 public:
  PrepareObserverOp(const LSRequestParams& aParams,
                    const Maybe<ContentParentId>& aContentParentId);

 private:
  nsresult Start() override;

  void GetResponse(LSRequestResponse& aResponse) override;
};

class LSSimpleRequestBase : public DatastoreOperationBase,
                            public PBackgroundLSSimpleRequestParent {
 protected:
  enum class State {
    // Just created on the PBackground thread. Next step is StartingRequest.
    Initial,

    // Waiting to start/starting request on the PBackground thread. Next step is
    // SendingResults.
    StartingRequest,

    // Waiting to send/sending results on the PBackground thread. Next step is
    // Completed.
    SendingResults,

    // All done.
    Completed
  };

  const LSSimpleRequestParams mParams;
  Maybe<ContentParentId> mContentParentId;
  State mState;

 public:
  LSSimpleRequestBase(const LSSimpleRequestParams& aParams,
                      const Maybe<ContentParentId>& aContentParentId);

  void Dispatch();

 protected:
  ~LSSimpleRequestBase() override;

  virtual nsresult Start() = 0;

  virtual void GetResponse(LSSimpleRequestResponse& aResponse) = 0;

 private:
  bool VerifyRequestParams();

  nsresult StartRequest();

  void SendResults();

  // Common nsIRunnable implementation that subclasses may not override.
  NS_IMETHOD
  Run() final;

  // IPDL methods.
  void ActorDestroy(ActorDestroyReason aWhy) override;
};

class PreloadedOp : public LSSimpleRequestBase {
  nsCString mOrigin;

 public:
  PreloadedOp(const LSSimpleRequestParams& aParams,
              const Maybe<ContentParentId>& aContentParentId);

 private:
  nsresult Start() override;

  void GetResponse(LSSimpleRequestResponse& aResponse) override;
};

class GetStateOp : public LSSimpleRequestBase {
  nsCString mOrigin;

 public:
  GetStateOp(const LSSimpleRequestParams& aParams,
             const Maybe<ContentParentId>& aContentParentId);

 private:
  nsresult Start() override;

  void GetResponse(LSSimpleRequestResponse& aResponse) override;
};

/*******************************************************************************
 * Other class declarations
 ******************************************************************************/

struct ArchivedOriginInfo {
  OriginAttributes mOriginAttributes;
  nsCString mOriginNoSuffix;

  ArchivedOriginInfo(const OriginAttributes& aOriginAttributes,
                     const nsACString& aOriginNoSuffix)
      : mOriginAttributes(aOriginAttributes),
        mOriginNoSuffix(aOriginNoSuffix) {}
};

class ArchivedOriginScope {
  struct Origin {
    nsCString mOriginSuffix;
    nsCString mOriginNoSuffix;

    Origin(const nsACString& aOriginSuffix, const nsACString& aOriginNoSuffix)
        : mOriginSuffix(aOriginSuffix), mOriginNoSuffix(aOriginNoSuffix) {}

    const nsACString& OriginSuffix() const { return mOriginSuffix; }

    const nsACString& OriginNoSuffix() const { return mOriginNoSuffix; }
  };

  struct Prefix {
    nsCString mOriginNoSuffix;

    explicit Prefix(const nsACString& aOriginNoSuffix)
        : mOriginNoSuffix(aOriginNoSuffix) {}

    const nsACString& OriginNoSuffix() const { return mOriginNoSuffix; }
  };

  struct Pattern {
    UniquePtr<OriginAttributesPattern> mPattern;

    explicit Pattern(const OriginAttributesPattern& aPattern)
        : mPattern(MakeUnique<OriginAttributesPattern>(aPattern)) {}

    Pattern(const Pattern& aOther)
        : mPattern(MakeUnique<OriginAttributesPattern>(*aOther.mPattern)) {}

    Pattern(Pattern&& aOther) = default;

    const OriginAttributesPattern& GetPattern() const {
      MOZ_ASSERT(mPattern);
      return *mPattern;
    }
  };

  struct Null {};

  using DataType = Variant<Origin, Pattern, Prefix, Null>;

  DataType mData;

 public:
  static UniquePtr<ArchivedOriginScope> CreateFromOrigin(
      const nsACString& aOriginAttrSuffix, const nsACString& aOriginKey);

  static UniquePtr<ArchivedOriginScope> CreateFromPrefix(
      const nsACString& aOriginKey);

  static UniquePtr<ArchivedOriginScope> CreateFromPattern(
      const OriginAttributesPattern& aPattern);

  static UniquePtr<ArchivedOriginScope> CreateFromNull();

  bool IsOrigin() const { return mData.is<Origin>(); }

  bool IsPrefix() const { return mData.is<Prefix>(); }

  bool IsPattern() const { return mData.is<Pattern>(); }

  bool IsNull() const { return mData.is<Null>(); }

  const nsACString& OriginSuffix() const {
    MOZ_ASSERT(IsOrigin());

    return mData.as<Origin>().OriginSuffix();
  }

  const nsACString& OriginNoSuffix() const {
    MOZ_ASSERT(IsOrigin() || IsPrefix());

    if (IsOrigin()) {
      return mData.as<Origin>().OriginNoSuffix();
    }
    return mData.as<Prefix>().OriginNoSuffix();
  }

  const OriginAttributesPattern& GetPattern() const {
    MOZ_ASSERT(IsPattern());

    return mData.as<Pattern>().GetPattern();
  }

  nsLiteralCString GetBindingClause() const;

  nsresult BindToStatement(mozIStorageStatement* aStatement) const;

  bool HasMatches(ArchivedOriginHashtable* aHashtable) const;

  void RemoveMatches(ArchivedOriginHashtable* aHashtable) const;

 private:
  // Move constructors
  explicit ArchivedOriginScope(const Origin&& aOrigin) : mData(aOrigin) {}

  explicit ArchivedOriginScope(const Pattern&& aPattern) : mData(aPattern) {}

  explicit ArchivedOriginScope(const Prefix&& aPrefix) : mData(aPrefix) {}

  explicit ArchivedOriginScope(const Null&& aNull) : mData(aNull) {}
};

class QuotaClient final : public mozilla::dom::quota::Client {
  class MatchFunction;

  static QuotaClient* sInstance;

  Mutex mShadowDatabaseMutex MOZ_UNANNOTATED;

 public:
  QuotaClient();

  static QuotaClient* GetInstance() {
    AssertIsOnBackgroundThread();

    return sInstance;
  }

  mozilla::Mutex& ShadowDatabaseMutex() {
    MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());

    return mShadowDatabaseMutex;
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(mozilla::dom::QuotaClient, override)

  Type GetType() override;

  Result<UsageInfo, nsresult> InitOrigin(PersistenceType aPersistenceType,
                                         const OriginMetadata& aOriginMetadata,
                                         const AtomicBool& aCanceled) override;

  nsresult InitOriginWithoutTracking(PersistenceType aPersistenceType,
                                     const OriginMetadata& aOriginMetadata,
                                     const AtomicBool& aCanceled) override;

  Result<UsageInfo, nsresult> GetUsageForOrigin(
      PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
      const AtomicBool& aCanceled) override;

  nsresult AboutToClearOrigins(const PersistenceScope& aPersistenceScope,
                               const OriginScope& aOriginScope) override;

  void OnOriginClearCompleted(const OriginMetadata& aOriginMetadata) override;

  void OnRepositoryClearCompleted(PersistenceType aPersistenceType) override;

  void ReleaseIOThreadObjects() override;

  void AbortOperationsForLocks(
      const DirectoryLockIdTable& aDirectoryLockIds) override;

  void AbortOperationsForProcess(ContentParentId aContentParentId) override;

  void AbortAllOperations() override;

  void StartIdleMaintenance() override;

  void StopIdleMaintenance() override;

 private:
  ~QuotaClient() override;

  void InitiateShutdown() override;
  bool IsShutdownCompleted() const override;
  nsCString GetShutdownStatus() const override;
  void ForceKillActors() override;
  void FinalizeShutdown() override;

  Result<UniquePtr<ArchivedOriginScope>, nsresult> CreateArchivedOriginScope(
      const OriginScope& aOriginScope);

  nsresult PerformDelete(mozIStorageConnection* aConnection,
                         const nsACString& aSchemaName,
                         ArchivedOriginScope* aArchivedOriginScope) const;
};

class QuotaClient::MatchFunction final : public mozIStorageFunction {
  OriginAttributesPattern mPattern;

 public:
  explicit MatchFunction(const OriginAttributesPattern& aPattern)
      : mPattern(aPattern) {}

 private:
  ~MatchFunction() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION
};

/*******************************************************************************
 * Helper classes
 ******************************************************************************/

class MOZ_STACK_CLASS AutoWriteTransaction final {
  Connection* mConnection;
  Maybe<MutexAutoLock> mShadowDatabaseLock;
  bool mShadowWrites;

 public:
  explicit AutoWriteTransaction(bool aShadowWrites);

  ~AutoWriteTransaction();

  nsresult Start(Connection* aConnection);

  nsresult Commit();

 private:
  nsresult LockAndAttachShadowDatabase(Connection* aConnection);

  nsresult DetachShadowDatabaseAndUnlock();
};

/*******************************************************************************
 * Globals
 ******************************************************************************/

#ifdef DEBUG
bool gLocalStorageInitialized = false;
#endif

using PrepareDatastoreOpArray =
    nsTArray<NotNull<CheckedUnsafePtr<PrepareDatastoreOp>>>;

StaticAutoPtr<PrepareDatastoreOpArray> gPrepareDatastoreOps;

// When CheckedUnsafePtr's checking is enabled, it's necessary to ensure that
// the hashtable uses the copy constructor instead of memmove for moving entries
// since memmove will break CheckedUnsafePtr in a memory-corrupting way.
using DatastoreHashKey = std::conditional<DiagnosticAssertEnabled::value,
                                          nsCStringHashKeyWithDisabledMemmove,
                                          nsCStringHashKey>::type;

using DatastoreHashtable =
    nsBaseHashtable<DatastoreHashKey, NotNull<CheckedUnsafePtr<Datastore>>,
                    MovingNotNull<CheckedUnsafePtr<Datastore>>>;

StaticAutoPtr<DatastoreHashtable> gDatastores;

uint64_t gLastDatastoreId = 0;

using PreparedDatastoreHashtable =
    nsClassHashtable<nsUint64HashKey, PreparedDatastore>;

StaticAutoPtr<PreparedDatastoreHashtable> gPreparedDatastores;

using PrivateDatastoreHashtable =
    nsClassHashtable<nsCStringHashKey, PrivateDatastore>;

// Keeps Private Browsing Datastores alive until the private browsing session
// is closed. This is necessary because LocalStorage Private Browsing data is
// (currently) not written to disk and therefore needs to explicitly be kept
// alive in memory so that if a user browses away from a site during a session
// and then back to it that they will still have their data.
//
// The entries are wrapped by PrivateDatastore instances which call
// NoteLivePrivateDatastore and NoteFinishedPrivateDatastore which set and
// clear mHasLivePrivateDatastore which inhibits MaybeClose() from closing the
// datastore (which would discard the data) when there are no active windows
// using LocalStorage for the origin.
//
// The table is cleared when the Private Browsing session is closed, which will
// cause NoteFinishedPrivateDatastore to be called on each Datastore which will
// in turn call MaybeClose which should then discard the Datastore. Or in the
// event of an (unlikely) race where the private browsing windows are still
// being torn down, will cause the Datastore to be discarded when the last
// window actually goes away.
MOZ_RUNINIT UniquePtr<PrivateDatastoreHashtable> gPrivateDatastores;

using DatabaseArray = nsTArray<Database*>;

StaticAutoPtr<DatabaseArray> gDatabases;

using LiveDatabaseArray = nsTArray<NotNull<CheckedUnsafePtr<Database>>>;

StaticAutoPtr<LiveDatabaseArray> gLiveDatabases;

StaticRefPtr<ConnectionThread> gConnectionThread;

uint64_t gLastObserverId = 0;

using PreparedObserverHashtable = nsRefPtrHashtable<nsUint64HashKey, Observer>;

StaticAutoPtr<PreparedObserverHashtable> gPreparedObsevers;

using ObserverHashtable =
    nsClassHashtable<nsCStringHashKey, nsTArray<NotNull<Observer*>>>;

StaticAutoPtr<ObserverHashtable> gObservers;

Atomic<bool> gShadowWrites(kDefaultShadowWrites);
Atomic<int32_t, Relaxed> gSnapshotPrefill(kDefaultSnapshotPrefill);
Atomic<int32_t, Relaxed> gSnapshotGradualPrefill(
    kDefaultSnapshotGradualPrefill);
Atomic<bool> gClientValidation(kDefaultClientValidation);

using UsageHashtable = nsTHashMap<nsCStringHashKey, int64_t>;

StaticAutoPtr<ArchivedOriginHashtable> gArchivedOrigins;

// Can only be touched on the Quota Manager I/O thread.
bool gInitializedShadowStorage = false;

StaticAutoPtr<LSInitializationInfo> gInitializationInfo;

bool IsOnGlobalConnectionThread() {
  MOZ_ASSERT(gConnectionThread);
  return gConnectionThread->IsOnConnectionThread();
}

void AssertIsOnGlobalConnectionThread() {
  MOZ_ASSERT(gConnectionThread);
  gConnectionThread->AssertIsOnConnectionThread();
}

already_AddRefed<Datastore> GetDatastore(const nsACString& aOrigin) {
  AssertIsOnBackgroundThread();

  if (gDatastores) {
    auto maybeDatastore = gDatastores->MaybeGet(aOrigin);
    if (maybeDatastore) {
      RefPtr<Datastore> result(std::move(*maybeDatastore).unwrapBasePtr());
      return result.forget();
    }
  }

  return nullptr;
}

nsresult LoadArchivedOrigins() {
  AssertIsOnIOThread();
  MOZ_ASSERT(!gArchivedOrigins);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_INSPECT(const auto& connection, CreateArchiveStorageConnection(
                                             quotaManager->GetStoragePath()));

  if (!connection) {
    gArchivedOrigins = new ArchivedOriginHashtable();
    return NS_OK;
  }

  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, connection, CreateStatement,
          "SELECT DISTINCT originAttributes, originKey "
          "FROM webappsstore2;"_ns));

  auto archivedOrigins = MakeUnique<ArchivedOriginHashtable>();

  // XXX Actually, this could use a hashtable variant of
  // CollectElementsWhileHasResult
  QM_TRY(quota::CollectWhileHasResult(
      *stmt, [&archivedOrigins](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const auto& originSuffix,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                         GetUTF8String, 0));
        QM_TRY_INSPECT(const auto& originNoSuffix,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, stmt,
                                                         GetUTF8String, 1));

        const nsCString hashKey =
            GetArchivedOriginHashKey(originSuffix, originNoSuffix);

        OriginAttributes originAttributes;
        QM_TRY(OkIf(originAttributes.PopulateFromSuffix(originSuffix)),
               Err(NS_ERROR_FAILURE));

        archivedOrigins->InsertOrUpdate(
            hashKey,
            MakeUnique<ArchivedOriginInfo>(originAttributes, originNoSuffix));

        return Ok{};
      }));

  gArchivedOrigins = archivedOrigins.release();
  return NS_OK;
}

Result<int64_t, nsresult> GetUsage(mozIStorageConnection& aConnection,
                                   ArchivedOriginScope* aArchivedOriginScope) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(
      const auto& stmt,
      ([aArchivedOriginScope,
        &aConnection]() -> Result<nsCOMPtr<mozIStorageStatement>, nsresult> {
        if (aArchivedOriginScope) {
          QM_TRY_RETURN(CreateAndExecuteSingleStepStatement<
                        SingleStepResult::ReturnNullIfNoResult>(
              aConnection,
              "SELECT "
              "total(utf16Length(key) + utf16Length(value)) "
              "FROM webappsstore2 "
              "WHERE originKey = :originKey "
              "AND originAttributes = :originAttributes;"_ns,
              [aArchivedOriginScope](auto& stmt) -> Result<Ok, nsresult> {
                QM_TRY(MOZ_TO_RESULT(
                    aArchivedOriginScope->BindToStatement(&stmt)));
                return Ok{};
              }));
        }

        QM_TRY_RETURN(CreateAndExecuteSingleStepStatement<
                      SingleStepResult::ReturnNullIfNoResult>(
            aConnection, "SELECT usage FROM database"_ns));
      }()));

  QM_TRY(OkIf(stmt), Err(NS_ERROR_FAILURE));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 0));
}

void ShadowWritesPrefChangedCallback(const char* aPrefName, void* aClosure) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!strcmp(aPrefName, kShadowWritesPref));
  MOZ_ASSERT(!aClosure);

  gShadowWrites = Preferences::GetBool(aPrefName, kDefaultShadowWrites);
}

void SnapshotPrefillPrefChangedCallback(const char* aPrefName, void* aClosure) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!strcmp(aPrefName, kSnapshotPrefillPref));
  MOZ_ASSERT(!aClosure);

  int32_t snapshotPrefill =
      Preferences::GetInt(aPrefName, kDefaultSnapshotPrefill);

  // The magic -1 is for use only by tests.
  if (snapshotPrefill == -1) {
    snapshotPrefill = INT32_MAX;
  }

  gSnapshotPrefill = snapshotPrefill;
}

void SnapshotGradualPrefillPrefChangedCallback(const char* aPrefName,
                                               void* aClosure) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!strcmp(aPrefName, kSnapshotGradualPrefillPref));
  MOZ_ASSERT(!aClosure);

  int32_t snapshotGradualPrefill =
      Preferences::GetInt(aPrefName, kDefaultSnapshotGradualPrefill);

  // The magic -1 is for use only by tests.
  if (snapshotGradualPrefill == -1) {
    snapshotGradualPrefill = INT32_MAX;
  }

  gSnapshotGradualPrefill = snapshotGradualPrefill;
}

int64_t GetSnapshotPeakUsagePreincrement(bool aInitial) {
  return aInitial ? StaticPrefs::
                        dom_storage_snapshot_peak_usage_initial_preincrement()
                  : StaticPrefs::
                        dom_storage_snapshot_peak_usage_gradual_preincrement();
}

int64_t GetSnapshotPeakUsageReducedPreincrement(bool aInitial) {
  return aInitial
             ? StaticPrefs::
                   dom_storage_snapshot_peak_usage_reduced_initial_preincrement()
             : StaticPrefs::
                   dom_storage_snapshot_peak_usage_reduced_gradual_preincrement();
}

void ClientValidationPrefChangedCallback(const char* aPrefName,
                                         void* aClosure) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!strcmp(aPrefName, kClientValidationPref));
  MOZ_ASSERT(!aClosure);

  gClientValidation = Preferences::GetBool(aPrefName, kDefaultClientValidation);
}

template <typename Condition>
void InvalidatePrepareDatastoreOpsMatching(const Condition& aCondition) {
  if (!gPrepareDatastoreOps) {
    return;
  }

  for (const auto& prepareDatastoreOp : *gPrepareDatastoreOps) {
    if (aCondition(*prepareDatastoreOp)) {
      prepareDatastoreOp->Invalidate();
    }
  }
}

template <typename Condition>
void InvalidatePreparedDatastoresMatching(const Condition& aCondition) {
  if (!gPreparedDatastores) {
    return;
  }

  for (const auto& preparedDatastore : gPreparedDatastores->Values()) {
    MOZ_ASSERT(preparedDatastore);

    if (aCondition(*preparedDatastore)) {
      preparedDatastore->Invalidate();
    }
  }
}

template <typename Condition>
nsTArray<RefPtr<Database>> CollectDatabasesMatching(Condition aCondition) {
  AssertIsOnBackgroundThread();

  if (!gLiveDatabases) {
    return nsTArray<RefPtr<Database>>{};
  }

  nsTArray<RefPtr<Database>> databases;

  for (const auto& database : *gLiveDatabases) {
    if (aCondition(*database)) {
      databases.AppendElement(database.get());
    }
  }

  return databases;
}

template <typename Condition>
void RequestAllowToCloseDatabasesMatching(Condition aCondition) {
  AssertIsOnBackgroundThread();

  nsTArray<RefPtr<Database>> databases = CollectDatabasesMatching(aCondition);

  for (const auto& database : databases) {
    MOZ_ASSERT(database);

    database->RequestAllowToClose();
  }
}

void ForceKillAllDatabases() {
  AssertIsOnBackgroundThread();

  nsTArray<RefPtr<Database>> databases =
      CollectDatabasesMatching([](const auto&) { return true; });

  for (const auto& database : databases) {
    MOZ_ASSERT(database);

    database->ForceKill();
  }
}

bool VerifyPrincipalInfo(const PrincipalInfo& aPrincipalInfo,
                         const PrincipalInfo& aStoragePrincipalInfo,
                         bool aCheckClientPrincipal) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!quota::IsPrincipalInfoValid(aPrincipalInfo))) {
    return false;
  }

  // Note that the client prinicpal could have a different spec than the node
  // principal but they should have the same origin. It's because the client
  // could be initialized when opening the initial about:blank document and pass
  // to the newly opened window and reuse over there if the new window has the
  // same origin as the initial about:blank document. But, the FilePath could be
  // different. Therefore, we have to ignore comparing the Spec of the
  // principals if we are verifying clinet principal here. Also, when
  // document.domain is set, client principal won't get it. So, we don't compare
  // domain for client princpal too.
  bool result = aCheckClientPrincipal
                    ? StoragePrincipalHelper::
                          VerifyValidClientPrincipalInfoForPrincipalInfo(
                              aStoragePrincipalInfo, aPrincipalInfo)
                    : StoragePrincipalHelper::
                          VerifyValidStoragePrincipalInfoForPrincipalInfo(
                              aStoragePrincipalInfo, aPrincipalInfo);
  if (NS_WARN_IF(!result)) {
    return false;
  }

  return true;
}

bool VerifyClientId(const Maybe<ContentParentId>& aContentParentId,
                    const Maybe<PrincipalInfo>& aPrincipalInfo,
                    const Maybe<nsID>& aClientId) {
  AssertIsOnBackgroundThread();

  if (gClientValidation) {
    if (NS_WARN_IF(aClientId.isNothing())) {
      return false;
    }

    if (NS_WARN_IF(aPrincipalInfo.isNothing())) {
      return false;
    }

    RefPtr<ClientManagerService> svc = ClientManagerService::GetInstance();
    if (svc && NS_WARN_IF(!svc->HasWindow(
                   aContentParentId, aPrincipalInfo.ref(), aClientId.ref()))) {
      return false;
    }
  }

  return true;
}

bool VerifyOriginKey(const nsACString& aOriginKey,
                     const PrincipalInfo& aPrincipalInfo) {
  AssertIsOnBackgroundThread();

  QM_TRY_INSPECT((const auto& [originAttrSuffix, originKey]),
                 GenerateOriginKey2(aPrincipalInfo), false);

  Unused << originAttrSuffix;

  QM_TRY(OkIf(originKey == aOriginKey), false,
         ([&originKey = originKey, &aOriginKey](const auto) {
           LS_WARNING("originKey (%s) doesn't match passed one (%s)!",
                      originKey.get(), nsCString(aOriginKey).get());
         }));

  return true;
}

LSInitializationInfo& MutableInitializationInfoRef(const CreateIfNonExistent&) {
  if (!gInitializationInfo) {
    gInitializationInfo = new LSInitializationInfo();
  }
  return *gInitializationInfo;
}

template <typename Func>
auto ExecuteOriginInitialization(const nsACString& aOrigin,
                                 const LSOriginInitialization aInitialization,
                                 const nsACString& aContext, Func&& aFunc)
    -> std::invoke_result_t<Func, const FirstInitializationAttempt<
                                      LSOriginInitialization, Nothing>&> {
  return ExecuteInitialization(
      MutableInitializationInfoRef(CreateIfNonExistent{})
          .MutableOriginInitializationInfoRef(aOrigin, CreateIfNonExistent{}),
      aInitialization, aContext, std::forward<Func>(aFunc));
}

}  // namespace

/*******************************************************************************
 * Exported functions
 ******************************************************************************/

void InitializeLocalStorage() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!gLocalStorageInitialized);

  // XXX Isn't this redundant? It's already done in InitializeQuotaManager.
  if (!QuotaManager::IsRunningGTests()) {
    // This service has to be started on the main thread currently.
    const nsCOMPtr<mozIStorageService> ss =
        do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);

    QM_WARNONLY_TRY(OkIf(ss));
  }

  Preferences::RegisterCallbackAndCall(ShadowWritesPrefChangedCallback,
                                       kShadowWritesPref);

  Preferences::RegisterCallbackAndCall(SnapshotPrefillPrefChangedCallback,
                                       kSnapshotPrefillPref);

  Preferences::RegisterCallbackAndCall(
      SnapshotGradualPrefillPrefChangedCallback, kSnapshotGradualPrefillPref);

  Preferences::RegisterCallbackAndCall(ClientValidationPrefChangedCallback,
                                       kClientValidationPref);

#ifdef DEBUG
  gLocalStorageInitialized = true;
#endif
}

namespace {

// XXX Merge these three methods into a new factory method Database::Create

already_AddRefed<PBackgroundLSDatabaseParent> AllocPBackgroundLSDatabaseParent(
    const PrincipalInfo& aPrincipalInfo, const uint32_t& aPrivateBrowsingId,
    const uint64_t& aDatastoreId) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  if (NS_WARN_IF(!gPreparedDatastores)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return nullptr;
  }

  PreparedDatastore* preparedDatastore = gPreparedDatastores->Get(aDatastoreId);
  if (NS_WARN_IF(!preparedDatastore)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return nullptr;
  }

  // If we ever decide to return null from this point on, we need to make sure
  // that the datastore is closed and the prepared datastore is removed from the
  // gPreparedDatastores hashtable.
  // We also assume that RecvCreateBackgroundLSDatabaseParent must call
  // RecvPBackgroundLSDatabaseConstructor once we return a valid actor in this
  // method.

  RefPtr<Database> database =
      new Database(aPrincipalInfo, preparedDatastore->GetContentParentId(),
                   preparedDatastore->Origin(), aPrivateBrowsingId);

  return database.forget();
}

bool RecvPBackgroundLSDatabaseConstructor(PBackgroundLSDatabaseParent* aActor,
                                          const PrincipalInfo& aPrincipalInfo,
                                          const uint32_t& aPrivateBrowsingId,
                                          const uint64_t& aDatastoreId) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(gPreparedDatastores);
  MOZ_ASSERT(gPreparedDatastores->Get(aDatastoreId));
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());

  // The actor is now completely built (it has a channel and it's registered
  // as a top level protocol).
  // ActorDestroy will be called if we fail here.

  mozilla::UniquePtr<PreparedDatastore> preparedDatastore;
  gPreparedDatastores->Remove(aDatastoreId, &preparedDatastore);
  MOZ_ASSERT(preparedDatastore);

  auto* database = static_cast<Database*>(aActor);

  database->SetActorAlive(&preparedDatastore->MutableDatastoreRef());

  // It's possible that AbortOperationsForLocks was called before the database
  // actor was created and became live. Let the child know that the database is
  // no longer valid.
  if (preparedDatastore->IsInvalidated()) {
    database->RequestAllowToClose();
  }

  return true;
}

bool RecvCreateBackgroundLSDatabaseParent(
    const PrincipalInfo& aPrincipalInfo, const uint32_t& aPrivateBrowsingId,
    const uint64_t& aDatastoreId,
    Endpoint<PBackgroundLSDatabaseParent>&& aParentEndpoint) {
  RefPtr<PBackgroundLSDatabaseParent> parent = AllocPBackgroundLSDatabaseParent(
      aPrincipalInfo, aPrivateBrowsingId, aDatastoreId);
  if (!parent) {
    return false;
  }

  // Transfer ownership to IPDL.
  MOZ_ALWAYS_TRUE(aParentEndpoint.Bind(parent));

  return RecvPBackgroundLSDatabaseConstructor(parent, aPrincipalInfo,
                                              aPrivateBrowsingId, aDatastoreId);
}

}  // namespace

PBackgroundLSObserverParent* AllocPBackgroundLSObserverParent(
    const uint64_t& aObserverId) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  if (NS_WARN_IF(!gPreparedObsevers)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return nullptr;
  }

  RefPtr<Observer> observer = gPreparedObsevers->Get(aObserverId);
  if (NS_WARN_IF(!observer)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return nullptr;
  }

  // observer->SetObject(this);

  // Transfer ownership to IPDL.
  return observer.forget().take();
}

bool RecvPBackgroundLSObserverConstructor(PBackgroundLSObserverParent* aActor,
                                          const uint64_t& aObserverId) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(gPreparedObsevers);
  MOZ_ASSERT(gPreparedObsevers->GetWeak(aObserverId));

  RefPtr<Observer> observer;
  gPreparedObsevers->Remove(aObserverId, observer.StartAssignment());

  if (!gPreparedObsevers->Count()) {
    gPreparedObsevers = nullptr;
  }

  if (!gObservers) {
    gObservers = new ObserverHashtable();
  }

  const auto notNullObserver = WrapNotNull(observer.get());

  nsTArray<NotNull<Observer*>>* const array =
      gObservers->GetOrInsertNew(notNullObserver->Origin());
  array->AppendElement(notNullObserver);

  if (RefPtr<Datastore> datastore = GetDatastore(observer->Origin())) {
    datastore->NoteChangedObserverArray(*array);
  }

  return true;
}

bool DeallocPBackgroundLSObserverParent(PBackgroundLSObserverParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  // Transfer ownership back from IPDL.
  RefPtr<Observer> actor = dont_AddRef(static_cast<Observer*>(aActor));

  return true;
}

PBackgroundLSRequestParent* AllocPBackgroundLSRequestParent(
    PBackgroundParent* aBackgroundActor, const LSRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != LSRequestParams::T__None);

  if (NS_WARN_IF(!NextGenLocalStorageEnabled())) {
    return nullptr;
  }

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  Maybe<ContentParentId> contentParentId;

  uint64_t childID = BackgroundParent::GetChildID(aBackgroundActor);
  if (childID) {
    contentParentId = Some(ContentParentId(childID));
  }

  RefPtr<LSRequestBase> actor;

  switch (aParams.type()) {
    case LSRequestParams::TLSRequestPreloadDatastoreParams:
    case LSRequestParams::TLSRequestPrepareDatastoreParams: {
      RefPtr<PrepareDatastoreOp> prepareDatastoreOp =
          new PrepareDatastoreOp(aParams, contentParentId);

      if (!gPrepareDatastoreOps) {
        gPrepareDatastoreOps = new PrepareDatastoreOpArray();
      }
      gPrepareDatastoreOps->AppendElement(
          WrapNotNullUnchecked(prepareDatastoreOp.get()));

      actor = std::move(prepareDatastoreOp);

      break;
    }

    case LSRequestParams::TLSRequestPrepareObserverParams: {
      RefPtr<PrepareObserverOp> prepareObserverOp =
          new PrepareObserverOp(aParams, contentParentId);

      actor = std::move(prepareObserverOp);

      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  // Transfer ownership to IPDL.
  return actor.forget().take();
}

bool RecvPBackgroundLSRequestConstructor(PBackgroundLSRequestParent* aActor,
                                         const LSRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != LSRequestParams::T__None);
  MOZ_ASSERT(NextGenLocalStorageEnabled());
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());

  // The actor is now completely built.

  auto* op = static_cast<LSRequestBase*>(aActor);

  op->Dispatch();

  return true;
}

bool DeallocPBackgroundLSRequestParent(PBackgroundLSRequestParent* aActor) {
  AssertIsOnBackgroundThread();

  // Transfer ownership back from IPDL.
  RefPtr<LSRequestBase> actor =
      dont_AddRef(static_cast<LSRequestBase*>(aActor));

  return true;
}

PBackgroundLSSimpleRequestParent* AllocPBackgroundLSSimpleRequestParent(
    PBackgroundParent* aBackgroundActor, const LSSimpleRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != LSSimpleRequestParams::T__None);

  if (NS_WARN_IF(!NextGenLocalStorageEnabled())) {
    return nullptr;
  }

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  Maybe<ContentParentId> contentParentId;

  uint64_t childID = BackgroundParent::GetChildID(aBackgroundActor);
  if (childID) {
    contentParentId = Some(ContentParentId(childID));
  }

  RefPtr<LSSimpleRequestBase> actor;

  switch (aParams.type()) {
    case LSSimpleRequestParams::TLSSimpleRequestPreloadedParams: {
      RefPtr<PreloadedOp> preloadedOp =
          new PreloadedOp(aParams, contentParentId);

      actor = std::move(preloadedOp);

      break;
    }

    case LSSimpleRequestParams::TLSSimpleRequestGetStateParams: {
      RefPtr<GetStateOp> getStateOp = new GetStateOp(aParams, contentParentId);

      actor = std::move(getStateOp);

      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  // Transfer ownership to IPDL.
  return actor.forget().take();
}

bool RecvPBackgroundLSSimpleRequestConstructor(
    PBackgroundLSSimpleRequestParent* aActor,
    const LSSimpleRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != LSSimpleRequestParams::T__None);
  MOZ_ASSERT(NextGenLocalStorageEnabled());
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());

  // The actor is now completely built.

  auto* op = static_cast<LSSimpleRequestBase*>(aActor);

  op->Dispatch();

  return true;
}

bool DeallocPBackgroundLSSimpleRequestParent(
    PBackgroundLSSimpleRequestParent* aActor) {
  AssertIsOnBackgroundThread();

  // Transfer ownership back from IPDL.
  RefPtr<LSSimpleRequestBase> actor =
      dont_AddRef(static_cast<LSSimpleRequestBase*>(aActor));

  return true;
}

namespace localstorage {

already_AddRefed<mozilla::dom::quota::Client> CreateQuotaClient() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(CachedNextGenLocalStorageEnabled());

  RefPtr<QuotaClient> client = new QuotaClient();
  return client.forget();
}

}  // namespace localstorage

/*******************************************************************************
 * DatastoreWriteOptimizer
 ******************************************************************************/

void DatastoreWriteOptimizer::ApplyAndReset(
    nsTArray<LSItemInfo>& aOrderedItems) {
  AssertIsOnOwningThread();

  // The mWriteInfos hash table contains all write infos, but it keeps them in
  // an arbitrary order, which means write infos need to be sorted before being
  // processed. However, the order is not important for deletions and normal
  // updates. Usually, filtering out deletions and updates would require extra
  // work, but we have to check the hash table for each ordered item anyway, so
  // we can remove the write info if it is a deletion or update without adding
  // extra overhead. In the end, only insertions need to be sorted before being
  // processed.

  if (mTruncateInfo) {
    aOrderedItems.Clear();
    mTruncateInfo = nullptr;
  }

  for (int32_t index = aOrderedItems.Length() - 1; index >= 0; index--) {
    LSItemInfo& item = aOrderedItems[index];

    if (auto entry = mWriteInfos.Lookup(item.key())) {
      WriteInfo* writeInfo = entry->get();

      switch (writeInfo->GetType()) {
        case WriteInfo::DeleteItem:
          aOrderedItems.RemoveElementAt(index);
          entry.Remove();
          break;

        case WriteInfo::UpdateItem: {
          auto updateItemInfo = static_cast<UpdateItemInfo*>(writeInfo);
          if (updateItemInfo->UpdateWithMove()) {
            // See the comment in LSWriteOptimizer::InsertItem for more details
            // about the UpdateWithMove flag.

            aOrderedItems.RemoveElementAt(index);
            entry.Data() = MakeUnique<InsertItemInfo>(
                updateItemInfo->SerialNumber(), updateItemInfo->GetKey(),
                updateItemInfo->GetValue());
          } else {
            item.value() = updateItemInfo->GetValue();
            entry.Remove();
          }
          break;
        }

        case WriteInfo::InsertItem:
          break;

        default:
          MOZ_CRASH("Bad type!");
      }
    }
  }

  nsTArray<NotNull<WriteInfo*>> writeInfos;
  GetSortedWriteInfos(writeInfos);

  for (WriteInfo* writeInfo : writeInfos) {
    MOZ_ASSERT(writeInfo->GetType() == WriteInfo::InsertItem);

    auto insertItemInfo = static_cast<InsertItemInfo*>(writeInfo);

    LSItemInfo* itemInfo = aOrderedItems.AppendElement();
    itemInfo->key() = insertItemInfo->GetKey();
    itemInfo->value() = insertItemInfo->GetValue();
  }

  mWriteInfos.Clear();
}

/*******************************************************************************
 * ConnectionWriteOptimizer
 ******************************************************************************/

Result<int64_t, nsresult> ConnectionWriteOptimizer::Perform(
    Connection* aConnection, bool aShadowWrites) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(aConnection);

  // The order of elements is not stored in the database, so write infos don't
  // need to be sorted before being processed.

  if (mTruncateInfo) {
    QM_TRY(MOZ_TO_RESULT(PerformTruncate(aConnection, aShadowWrites)));
  }

  for (const auto& entry : mWriteInfos) {
    const WriteInfo* const writeInfo = entry.GetWeak();

    switch (writeInfo->GetType()) {
      case WriteInfo::InsertItem:
      case WriteInfo::UpdateItem: {
        const auto* const insertItemInfo =
            static_cast<const InsertItemInfo*>(writeInfo);

        QM_TRY(MOZ_TO_RESULT(PerformInsertOrUpdate(
            aConnection, aShadowWrites, insertItemInfo->GetKey(),
            insertItemInfo->GetValue())));

        break;
      }

      case WriteInfo::DeleteItem: {
        const auto* const deleteItemInfo =
            static_cast<const DeleteItemInfo*>(writeInfo);

        QM_TRY(MOZ_TO_RESULT(PerformDelete(aConnection, aShadowWrites,
                                           deleteItemInfo->GetKey())));

        break;
      }

      default:
        MOZ_CRASH("Bad type!");
    }
  }

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "UPDATE database "
      "SET usage = usage + :delta"_ns,
      [this](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByName("delta"_ns, mTotalDelta)));

        return Ok{};
      })));

  QM_TRY_INSPECT(const auto& stmt, CreateAndExecuteSingleStepStatement<
                                       SingleStepResult::ReturnNullIfNoResult>(
                                       aConnection->MutableStorageConnection(),
                                       "SELECT usage FROM database"_ns));

  QM_TRY(OkIf(stmt), Err(NS_ERROR_FAILURE));

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt64, 0));
}

nsresult ConnectionWriteOptimizer::PerformInsertOrUpdate(
    Connection* aConnection, bool aShadowWrites, const nsAString& aKey,
    const LSValue& aValue) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "INSERT OR REPLACE INTO data (key, utf16_length, conversion_type, "
      "compression_type, value) "
      "VALUES(:key, :utf16_length, :conversion_type, :compression_type, :value)"_ns,
      [&aKey, &aValue](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByName("key"_ns, aKey)));
        QM_TRY(MOZ_TO_RESULT(
            stmt.BindInt32ByName("utf16_length"_ns, aValue.UTF16Length())));
        QM_TRY(MOZ_TO_RESULT(stmt.BindInt32ByName(
            "conversion_type"_ns,
            static_cast<int32_t>(aValue.GetConversionType()))));
        QM_TRY(MOZ_TO_RESULT(stmt.BindInt32ByName(
            "compression_type"_ns,
            static_cast<int32_t>(aValue.GetCompressionType()))));

        if (0u == aValue.Length()) {  // Otherwise empty string becomes null
          QM_TRY(MOZ_TO_RESULT(
              stmt.BindUTF8StringByName("value"_ns, aValue.AsCString())));
        } else {
          QM_TRY(MOZ_TO_RESULT(
              stmt.BindUTF8StringAsBlobByName("value"_ns, aValue.AsCString())));
        }

        return Ok{};
      })));

  if (!aShadowWrites) {
    return NS_OK;
  }

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "INSERT OR REPLACE INTO shadow.webappsstore2 "
      "(originAttributes, originKey, scope, key, value) "
      "VALUES (:originAttributes, :originKey, :scope, :key, :value) "_ns,
      [&aConnection, &aKey, &aValue](auto& stmt) -> Result<Ok, nsresult> {
        using ConversionType = LSValue::ConversionType;
        using CompressionType = LSValue::CompressionType;

        const ArchivedOriginScope* const archivedOriginScope =
            aConnection->GetArchivedOriginScope();

        QM_TRY(MOZ_TO_RESULT(archivedOriginScope->BindToStatement(&stmt)));

        QM_TRY(MOZ_TO_RESULT(stmt.BindUTF8StringByName(
            "scope"_ns, Scheme0Scope(archivedOriginScope->OriginSuffix(),
                                     archivedOriginScope->OriginNoSuffix()))));

        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByName("key"_ns, aKey)));

        bool isCompressed =
            CompressionType::UNCOMPRESSED != aValue.GetCompressionType();
        bool isAlreadyConverted =
            ConversionType::NONE != aValue.GetConversionType();

        nsCString buffer;
        const nsCString& valueBlob = aValue.AsCString();
        if (isCompressed) {
          QM_TRY(OkIf(SnappyUncompress(valueBlob, buffer)),
                 Err(NS_ERROR_FAILURE));
        }
        const nsCString& value = isCompressed ? buffer : valueBlob;

        // For shadow writes, we undo buffer swap and convert destructively
        nsCString unconverted;
        if (!isAlreadyConverted) {
          nsString converted;
          QM_TRY(OkIf(PutCStringBytesToString(value, converted)),
                 Err(NS_ERROR_OUT_OF_MEMORY));
          QM_TRY(OkIf(CopyUTF16toUTF8(converted, unconverted, fallible)),
                 Err(NS_ERROR_OUT_OF_MEMORY));  // Corrupt invalid data
        }
        const nsCString& untransformed =
            (!isAlreadyConverted) ? unconverted : value;

        QM_TRY(MOZ_TO_RESULT(
            stmt.BindUTF8StringByName("value"_ns, untransformed)));

        return Ok{};
      })));

  return NS_OK;
}

nsresult ConnectionWriteOptimizer::PerformDelete(Connection* aConnection,
                                                 bool aShadowWrites,
                                                 const nsAString& aKey) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "DELETE FROM data "
      "WHERE key = :key;"_ns,
      [&aKey](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByName("key"_ns, aKey)));

        return Ok{};
      })));

  if (!aShadowWrites) {
    return NS_OK;
  }

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "DELETE FROM shadow.webappsstore2 "
      "WHERE originAttributes = :originAttributes "
      "AND originKey = :originKey "
      "AND key = :key;"_ns,
      [&aConnection, &aKey](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(
            aConnection->GetArchivedOriginScope()->BindToStatement(&stmt)));

        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByName("key"_ns, aKey)));

        return Ok{};
      })));

  return NS_OK;
}

nsresult ConnectionWriteOptimizer::PerformTruncate(Connection* aConnection,
                                                   bool aShadowWrites) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(aConnection);

  QM_TRY(MOZ_TO_RESULT(
      aConnection->ExecuteCachedStatement("DELETE FROM data;"_ns)));

  if (!aShadowWrites) {
    return NS_OK;
  }

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "DELETE FROM shadow.webappsstore2 "
      "WHERE originAttributes = :originAttributes "
      "AND originKey = :originKey;"_ns,
      [&aConnection](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(
            aConnection->GetArchivedOriginScope()->BindToStatement(&stmt)));

        return Ok{};
      })));

  return NS_OK;
}

/*******************************************************************************
 * DatastoreOperationBase
 ******************************************************************************/

/*******************************************************************************
 * ConnectionDatastoreOperationBase
 ******************************************************************************/

ConnectionDatastoreOperationBase::ConnectionDatastoreOperationBase(
    Connection* aConnection, bool aEnsureStorageConnection)
    : mConnection(aConnection),
      mEnsureStorageConnection(aEnsureStorageConnection) {
  MOZ_ASSERT(aConnection);
}

ConnectionDatastoreOperationBase::~ConnectionDatastoreOperationBase() {
  MOZ_ASSERT(!mConnection,
             "ConnectionDatabaseOperationBase::Cleanup() was not called by a "
             "subclass!");
}

void ConnectionDatastoreOperationBase::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mConnection);

  mConnection = nullptr;

  NoteComplete();
}

void ConnectionDatastoreOperationBase::OnSuccess() { AssertIsOnOwningThread(); }

void ConnectionDatastoreOperationBase::OnFailure(nsresult aResultCode) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_FAILED(aResultCode));
}

void ConnectionDatastoreOperationBase::RunOnConnectionThread() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(mConnection);
  MOZ_ASSERT(NS_SUCCEEDED(ResultCode()));

  if (!MayProceedOnNonOwningThread()) {
    SetFailureCode(NS_ERROR_ABORT);
  } else {
    nsresult rv = NS_OK;

    // The boolean flag is only used by the CloseOp to avoid creating empty
    // databases.
    if (mEnsureStorageConnection) {
      rv = mConnection->EnsureStorageConnection();
      if (NS_WARN_IF(NS_FAILED(rv))) {
        SetFailureCode(rv);
      } else {
        MOZ_ASSERT(mConnection->HasStorageConnection());
      }
    }

    if (NS_SUCCEEDED(rv)) {
      rv = DoDatastoreWork();
      if (NS_FAILED(rv)) {
        SetFailureCode(rv);
      }
    }
  }

  MOZ_ALWAYS_SUCCEEDS(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));
}

void ConnectionDatastoreOperationBase::RunOnOwningThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mConnection);

  if (!MayProceed()) {
    MaybeSetFailureCode(NS_ERROR_ABORT);
  }

  if (NS_SUCCEEDED(ResultCode())) {
    OnSuccess();
  } else {
    OnFailure(ResultCode());
  }

  Cleanup();
}

NS_IMETHODIMP
ConnectionDatastoreOperationBase::Run() {
  if (IsOnGlobalConnectionThread()) {
    RunOnConnectionThread();
  } else {
    RunOnOwningThread();
  }

  return NS_OK;
}

/*******************************************************************************
 * Connection implementation
 ******************************************************************************/

Connection::Connection(ConnectionThread* aConnectionThread,
                       const OriginMetadata& aOriginMetadata,
                       UniquePtr<ArchivedOriginScope>&& aArchivedOriginScope,
                       bool aDatabaseWasNotAvailable)
    : mConnectionThread(aConnectionThread),
      mQuotaClient(QuotaClient::GetInstance()),
      mArchivedOriginScope(std::move(aArchivedOriginScope)),
      mOriginMetadata(aOriginMetadata),
      mDatabaseWasNotAvailable(aDatabaseWasNotAvailable),
      mHasCreatedDatabase(false),
      mFlushScheduled(false)
#ifdef DEBUG
      ,
      mInUpdateBatch(false),
      mFinished(false)
#endif
{
  AssertIsOnOwningThread();
  MOZ_ASSERT(!aOriginMetadata.mGroup.IsEmpty());
  MOZ_ASSERT(!aOriginMetadata.mOrigin.IsEmpty());
}

Connection::~Connection() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mFlushScheduled);
  MOZ_ASSERT(!mInUpdateBatch);
  MOZ_ASSERT(mFinished);
}

void Connection::Dispatch(ConnectionDatastoreOperationBase* aOp) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mConnectionThread);

  MOZ_ALWAYS_SUCCEEDS(
      mConnectionThread->mThread->Dispatch(aOp, NS_DISPATCH_NORMAL));
}

void Connection::Close(nsIRunnable* aCallback) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aCallback);

  if (mFlushScheduled) {
    MOZ_ASSERT(mFlushTimer);
    MOZ_ALWAYS_SUCCEEDS(mFlushTimer->Cancel());

    Flush();

    mFlushTimer = nullptr;
  }

  RefPtr<CloseOp> op = new CloseOp(this, aCallback);

  Dispatch(op);
}

void Connection::SetItem(const nsString& aKey, const LSValue& aValue,
                         int64_t aDelta, bool aIsNewItem) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInUpdateBatch);

  if (aIsNewItem) {
    mWriteOptimizer.InsertItem(aKey, aValue, aDelta);
  } else {
    mWriteOptimizer.UpdateItem(aKey, aValue, aDelta);
  }
}

void Connection::RemoveItem(const nsString& aKey, int64_t aDelta) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInUpdateBatch);

  mWriteOptimizer.DeleteItem(aKey, aDelta);
}

void Connection::Clear(int64_t aDelta) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInUpdateBatch);

  mWriteOptimizer.Truncate(aDelta);
}

void Connection::BeginUpdateBatch() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mInUpdateBatch);

#ifdef DEBUG
  mInUpdateBatch = true;
#endif
}

void Connection::EndUpdateBatch() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInUpdateBatch);

  if (mWriteOptimizer.HasWrites() && !mFlushScheduled) {
    ScheduleFlush();
  }

#ifdef DEBUG
  mInUpdateBatch = false;
#endif
}

nsresult Connection::EnsureStorageConnection() {
  AssertIsOnGlobalConnectionThread();

  if (HasStorageConnection()) {
    return NS_OK;
  }

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  if (!mDatabaseWasNotAvailable || mHasCreatedDatabase) {
    MOZ_ASSERT(mOriginMetadata.mPersistenceType == PERSISTENCE_TYPE_DEFAULT);

    QM_TRY_INSPECT(const auto& directoryEntry,
                   quotaManager->GetOriginDirectory(mOriginMetadata));

    QM_TRY(MOZ_TO_RESULT(directoryEntry->Append(
        NS_LITERAL_STRING_FROM_CSTRING(LS_DIRECTORY_NAME))));

    QM_TRY(MOZ_TO_RESULT(directoryEntry->GetPath(mDirectoryPath)));
    QM_TRY(MOZ_TO_RESULT(directoryEntry->Append(kDataFileName)));

    QM_TRY_INSPECT(
        const auto& databaseFilePath,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, directoryEntry, GetPath));

    QM_TRY_UNWRAP(auto storageConnection,
                  GetStorageConnection(databaseFilePath));
    LazyInit(WrapMovingNotNull(std::move(storageConnection)));

    return NS_OK;
  }

  auto helper =
      MakeRefPtr<GetOrCreateTemporaryOriginDirectoryHelper>(mOriginMetadata);

  QM_TRY_INSPECT(const auto& originDirectoryPath,
                 helper->BlockAndReturnOriginDirectoryPath());

  QM_TRY_INSPECT(const auto& directoryEntry,
                 QM_NewLocalFile(originDirectoryPath));

  QM_TRY(MOZ_TO_RESULT(directoryEntry->Append(
      NS_LITERAL_STRING_FROM_CSTRING(LS_DIRECTORY_NAME))));

  QM_TRY(MOZ_TO_RESULT(directoryEntry->GetPath(mDirectoryPath)));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(directoryEntry, Exists));

  if (!exists) {
    QM_TRY(
        MOZ_TO_RESULT(directoryEntry->Create(nsIFile::DIRECTORY_TYPE, 0755)));
  }

  QM_TRY(MOZ_TO_RESULT(directoryEntry->Append(kDataFileName)));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(directoryEntry, Exists));

    MOZ_ASSERT(!exists);
  }
#endif

  QM_TRY_INSPECT(const auto& usageFile, GetUsageFile(mDirectoryPath));

  nsCOMPtr<mozIStorageConnection> storageConnection;

  auto autoRemove = MakeScopeExit([&storageConnection, &directoryEntry] {
    if (storageConnection) {
      MOZ_ALWAYS_SUCCEEDS(storageConnection->Close());
    }

    nsresult rv = directoryEntry->Remove(false);
    if (rv != NS_ERROR_FILE_NOT_FOUND && NS_FAILED(rv)) {
      NS_WARNING("Failed to remove database file!");
    }
  });

  QM_TRY_UNWRAP(storageConnection, CreateStorageConnectionWithRecovery(
                                       *directoryEntry, *usageFile, Origin(),
                                       [] { MOZ_ASSERT_UNREACHABLE(); }));

  MOZ_ASSERT(mQuotaClient);

  MutexAutoLock shadowDatabaseLock(mQuotaClient->ShadowDatabaseMutex());

  nsCOMPtr<mozIStorageConnection> shadowConnection;
  if (!gInitializedShadowStorage) {
    QM_TRY_UNWRAP(shadowConnection,
                  CreateShadowStorageConnection(quotaManager->GetBasePath()));

    gInitializedShadowStorage = true;
  }

  autoRemove.release();

  if (!mHasCreatedDatabase) {
    mHasCreatedDatabase = true;
  }

  LazyInit(WrapMovingNotNull(std::move(storageConnection)));

  return NS_OK;
}

void Connection::CloseStorageConnection() {
  AssertIsOnGlobalConnectionThread();

  CachingDatabaseConnection::Close();
}

nsresult Connection::BeginWriteTransaction() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(HasStorageConnection());

  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("BEGIN IMMEDIATE;"_ns)));

  return NS_OK;
}

nsresult Connection::CommitWriteTransaction() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(HasStorageConnection());

  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("COMMIT;"_ns)));

  return NS_OK;
}

nsresult Connection::RollbackWriteTransaction() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(HasStorageConnection());

  QM_TRY_INSPECT(const auto& stmt, BorrowCachedStatement("ROLLBACK;"_ns));

  // This may fail if SQLite already rolled back the transaction so ignore any
  // errors.
  Unused << stmt->Execute();

  return NS_OK;
}

void Connection::ScheduleFlush() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mWriteOptimizer.HasWrites());
  MOZ_ASSERT(!mFlushScheduled);

  if (!mFlushTimer) {
    mFlushTimer = NS_NewTimer();
    MOZ_ASSERT(mFlushTimer);
  }

  MOZ_ALWAYS_SUCCEEDS(mFlushTimer->InitWithNamedFuncCallback(
      FlushTimerCallback, this, kFlushTimeoutMs, nsITimer::TYPE_ONE_SHOT,
      "Connection::FlushTimerCallback"));

  mFlushScheduled = true;
}

void Connection::Flush() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mFlushScheduled);

  if (mWriteOptimizer.HasWrites()) {
    RefPtr<FlushOp> op = new FlushOp(this, std::move(mWriteOptimizer));

    Dispatch(op);
  }

  mFlushScheduled = false;
}

// static
void Connection::FlushTimerCallback(nsITimer* aTimer, void* aClosure) {
  MOZ_ASSERT(aClosure);

  auto* self = static_cast<Connection*>(aClosure);
  MOZ_ASSERT(self);
  MOZ_ASSERT(self->mFlushScheduled);

  self->Flush();
}

Result<nsString, nsresult>
Connection::GetOrCreateTemporaryOriginDirectoryHelper::
    BlockAndReturnOriginDirectoryPath() {
  AssertIsOnGlobalConnectionThread();

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  MOZ_ALWAYS_SUCCEEDS(
      quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL));

  mozilla::MonitorAutoLock lock(mMonitor);
  while (mWaiting) {
    lock.Wait();
  }

  QM_TRY(MOZ_TO_RESULT(mIOThreadResultCode));

  return mOriginDirectoryPath;
}

nsresult
Connection::GetOrCreateTemporaryOriginDirectoryHelper::RunOnIOThread() {
  AssertIsOnIOThread();

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_INSPECT(
      const auto& directoryEntry,
      quotaManager->GetOrCreateTemporaryOriginDirectory(mOriginMetadata));

  QM_TRY(MOZ_TO_RESULT(directoryEntry->GetPath(mOriginDirectoryPath)));

  return NS_OK;
}

NS_IMETHODIMP
Connection::GetOrCreateTemporaryOriginDirectoryHelper::Run() {
  AssertIsOnIOThread();

  nsresult rv = RunOnIOThread();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mIOThreadResultCode = rv;
  }

  mozilla::MonitorAutoLock lock(mMonitor);
  MOZ_ASSERT(mWaiting);

  mWaiting = false;
  lock.Notify();

  return NS_OK;
}

Connection::FlushOp::FlushOp(Connection* aConnection,
                             ConnectionWriteOptimizer&& aWriteOptimizer)
    : ConnectionDatastoreOperationBase(aConnection),
      mWriteOptimizer(std::move(aWriteOptimizer)),
      mShadowWrites(gShadowWrites) {}

nsresult Connection::FlushOp::DoDatastoreWork() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(mConnection);

  AutoWriteTransaction autoWriteTransaction(mShadowWrites);

  QM_TRY(MOZ_TO_RESULT(autoWriteTransaction.Start(mConnection)));

  QM_TRY_INSPECT(const int64_t& usage,
                 mWriteOptimizer.Perform(mConnection, mShadowWrites));

  QM_TRY_INSPECT(const auto& usageFile,
                 GetUsageFile(mConnection->DirectoryPath()));

  QM_TRY_INSPECT(const auto& usageJournalFile,
                 GetUsageJournalFile(mConnection->DirectoryPath()));

  QM_TRY(MOZ_TO_RESULT(UpdateUsageFile(usageFile, usageJournalFile, usage)));

  QM_TRY(MOZ_TO_RESULT(autoWriteTransaction.Commit()));

  QM_TRY(MOZ_TO_RESULT(usageJournalFile->Remove(false)));

  return NS_OK;
}

void Connection::FlushOp::Cleanup() {
  AssertIsOnOwningThread();

  mWriteOptimizer.Reset();

  MOZ_ASSERT(!mWriteOptimizer.HasWrites());

  ConnectionDatastoreOperationBase::Cleanup();
}

nsresult Connection::CloseOp::DoDatastoreWork() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(mConnection);

  if (mConnection->HasStorageConnection()) {
    mConnection->CloseStorageConnection();
  }

  return NS_OK;
}

void Connection::CloseOp::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mConnection);

  mConnection->mConnectionThread->mConnections.Remove(mConnection->Origin());

#ifdef DEBUG
  MOZ_ASSERT(!mConnection->mFinished);
  mConnection->mFinished = true;
#endif

  nsCOMPtr<nsIRunnable> callback;
  mCallback.swap(callback);

  callback->Run();

  ConnectionDatastoreOperationBase::Cleanup();
}

/*******************************************************************************
 * ConnectionThread implementation
 ******************************************************************************/

ConnectionThread::ConnectionThread() {
  AssertIsOnOwningThread();
  AssertIsOnBackgroundThread();

  MOZ_ALWAYS_SUCCEEDS(NS_NewNamedThread("LS Thread", getter_AddRefs(mThread)));
}

ConnectionThread::~ConnectionThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mConnections.Count());
}

bool ConnectionThread::IsOnConnectionThread() {
  MOZ_ASSERT(mThread);

  bool current;
  return NS_SUCCEEDED(mThread->IsOnCurrentThread(&current)) && current;
}

void ConnectionThread::AssertIsOnConnectionThread() {
  MOZ_ASSERT(IsOnConnectionThread());
}

already_AddRefed<Connection> ConnectionThread::CreateConnection(
    const OriginMetadata& aOriginMetadata,
    UniquePtr<ArchivedOriginScope>&& aArchivedOriginScope,
    bool aDatabaseWasNotAvailable) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!aOriginMetadata.mOrigin.IsEmpty());
  MOZ_DIAGNOSTIC_ASSERT(!mConnections.Contains(aOriginMetadata.mOrigin));

  RefPtr<Connection> connection =
      new Connection(this, aOriginMetadata, std::move(aArchivedOriginScope),
                     aDatabaseWasNotAvailable);
  mConnections.InsertOrUpdate(aOriginMetadata.mOrigin, RefPtr{connection});

  return connection.forget();
}

void ConnectionThread::Shutdown() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mThread);

  mThread->Shutdown();
}

/*******************************************************************************
 * Datastore
 ******************************************************************************/

Datastore::Datastore(const OriginMetadata& aOriginMetadata,
                     uint32_t aPrivateBrowsingId, int64_t aUsage,
                     int64_t aSizeOfKeys, int64_t aSizeOfItems,
                     ClientDirectoryLockHandle&& aDirectoryLockHandle,
                     RefPtr<Connection>&& aConnection,
                     RefPtr<QuotaObject>&& aQuotaObject,
                     nsTHashMap<nsStringHashKey, LSValue>& aValues,
                     nsTArray<LSItemInfo>&& aOrderedItems)
    : mDirectoryLockHandle(std::move(aDirectoryLockHandle)),
      mConnection(std::move(aConnection)),
      mQuotaObject(std::move(aQuotaObject)),
      mOrderedItems(std::move(aOrderedItems)),
      mOriginMetadata(aOriginMetadata),
      mPrivateBrowsingId(aPrivateBrowsingId),
      mUsage(aUsage),
      mUpdateBatchUsage(-1),
      mSizeOfKeys(aSizeOfKeys),
      mSizeOfItems(aSizeOfItems),
      mClosed(false),
      mInUpdateBatch(false),
      mHasLivePrivateDatastore(false) {
  AssertIsOnBackgroundThread();

  mValues.SwapElements(aValues);
}

Datastore::~Datastore() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mClosed);
}

void Datastore::Close() {
  AssertIsOnBackgroundThread();
  MOZ_DIAGNOSTIC_ASSERT(!mClosed);
  MOZ_ASSERT(!mPrepareDatastoreOps.Count());
  MOZ_ASSERT(!mPreparedDatastores.Count());
  MOZ_ASSERT(!mDatabases.Count());
  MOZ_ASSERT(mDirectoryLockHandle);

  mClosed = true;

  if (IsPersistent()) {
    MOZ_ASSERT(mConnection);
    MOZ_ASSERT(mQuotaObject);

    // We can't release the directory lock and unregister itself from the
    // hashtable until the connection is fully closed.
    nsCOMPtr<nsIRunnable> callback =
        NewRunnableMethod("dom::Datastore::ConnectionClosedCallback", this,
                          &Datastore::ConnectionClosedCallback);
    mConnection->Close(callback);
  } else {
    MOZ_ASSERT(!mConnection);
    MOZ_ASSERT(!mQuotaObject);

    // There's no connection, so it's safe to release the directory lock and
    // unregister itself from the hashtable.

    {
      auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
    }

    CleanupMetadata();
  }
}

void Datastore::WaitForConnectionToComplete(nsIRunnable* aCallback) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aCallback);
  MOZ_ASSERT(!mCompleteCallback);
  MOZ_ASSERT(mClosed);

  mCompleteCallback = aCallback;
}

void Datastore::NoteLivePrepareDatastoreOp(
    PrepareDatastoreOp* aPrepareDatastoreOp) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aPrepareDatastoreOp);
  MOZ_ASSERT(!mPrepareDatastoreOps.Contains(aPrepareDatastoreOp));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mPrepareDatastoreOps.Insert(aPrepareDatastoreOp);
}

void Datastore::NoteFinishedPrepareDatastoreOp(
    PrepareDatastoreOp* aPrepareDatastoreOp) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aPrepareDatastoreOp);
  MOZ_ASSERT(mPrepareDatastoreOps.Contains(aPrepareDatastoreOp));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mPrepareDatastoreOps.Remove(aPrepareDatastoreOp);

  QuotaManager::MaybeRecordQuotaClientShutdownStep(
      quota::Client::LS, "PrepareDatastoreOp finished"_ns);

  MaybeClose();
}

void Datastore::NoteLivePrivateDatastore() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mHasLivePrivateDatastore);
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mHasLivePrivateDatastore = true;
}

void Datastore::NoteFinishedPrivateDatastore() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mHasLivePrivateDatastore);
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mHasLivePrivateDatastore = false;

  QuotaManager::MaybeRecordQuotaClientShutdownStep(
      quota::Client::LS, "PrivateDatastore finished"_ns);

  MaybeClose();
}

void Datastore::NoteLivePreparedDatastore(
    PreparedDatastore* aPreparedDatastore) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aPreparedDatastore);
  MOZ_ASSERT(!mPreparedDatastores.Contains(aPreparedDatastore));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mPreparedDatastores.Insert(aPreparedDatastore);
}

void Datastore::NoteFinishedPreparedDatastore(
    PreparedDatastore* aPreparedDatastore) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aPreparedDatastore);
  MOZ_ASSERT(mPreparedDatastores.Contains(aPreparedDatastore));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mPreparedDatastores.Remove(aPreparedDatastore);

  QuotaManager::MaybeRecordQuotaClientShutdownStep(
      quota::Client::LS, "PreparedDatastore finished"_ns);

  MaybeClose();
}

bool Datastore::HasOtherProcessDatabases(Database* aDatabase) {
  AssertIsOnBackgroundThread();

  for (Database* database : mDatabases) {
    if (database->ContentParentIdRef() != aDatabase->ContentParentIdRef()) {
      return true;
    }
  }

  return false;
}

void Datastore::NoteLiveDatabase(Database* aDatabase) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(!mDatabases.Contains(aDatabase));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mDatabases.Insert(aDatabase);

  NoteChangedDatabaseMap();
}

void Datastore::NoteFinishedDatabase(Database* aDatabase) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(mDatabases.Contains(aDatabase));
  MOZ_ASSERT(!mActiveDatabases.Contains(aDatabase));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mClosed);

  mDatabases.Remove(aDatabase);

  NoteChangedDatabaseMap();

  QuotaManager::MaybeRecordQuotaClientShutdownStep(quota::Client::LS,
                                                   "Database finished"_ns);

  MaybeClose();
}

void Datastore::NoteActiveDatabase(Database* aDatabase) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(mDatabases.Contains(aDatabase));
  MOZ_ASSERT(!mActiveDatabases.Contains(aDatabase));
  MOZ_ASSERT(!mClosed);

  mActiveDatabases.Insert(aDatabase);
}

void Datastore::NoteInactiveDatabase(Database* aDatabase) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(mDatabases.Contains(aDatabase));
  MOZ_ASSERT(mActiveDatabases.Contains(aDatabase));
  MOZ_ASSERT(!mClosed);

  mActiveDatabases.Remove(aDatabase);

  if (!mActiveDatabases.Count() && mPendingUsageDeltas.Length()) {
    int64_t finalDelta = 0;

    for (auto delta : mPendingUsageDeltas) {
      finalDelta += delta;
    }

    MOZ_ASSERT(finalDelta <= 0);

    if (finalDelta != 0) {
      DebugOnly<bool> ok = UpdateUsage(finalDelta);
      MOZ_ASSERT(ok);
    }

    mPendingUsageDeltas.Clear();
  }
}

void Datastore::GetSnapshotLoadInfo(const nsAString& aKey,
                                    bool& aAddKeyToUnknownItems,
                                    nsTHashtable<nsStringHashKey>& aLoadedItems,
                                    nsTArray<LSItemInfo>& aItemInfos,
                                    uint32_t& aNextLoadIndex,
                                    LSSnapshot::LoadState& aLoadState) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mClosed);
  MOZ_ASSERT(!mInUpdateBatch);

#ifdef DEBUG
  int64_t sizeOfKeys = 0;
  int64_t sizeOfItems = 0;
  for (auto item : mOrderedItems) {
    int64_t sizeOfKey = static_cast<int64_t>(item.key().Length());
    sizeOfKeys += sizeOfKey;
    sizeOfItems += sizeOfKey + static_cast<int64_t>(item.value().Length());
  }
  MOZ_ASSERT(mSizeOfKeys == sizeOfKeys);
  MOZ_ASSERT(mSizeOfItems == sizeOfItems);
#endif

  // Computes load state optimized for current size of keys and items.
  // Zero key length and value can be passed to do a quick initial estimation.
  // If computed load state is already AllOrderedItems then excluded key length
  // and value length can't make it any better.
  auto GetLoadState = [&](int64_t aKeyLength, int64_t aValueLength) {
    if (mSizeOfKeys - aKeyLength <= gSnapshotPrefill) {
      if (mSizeOfItems - aKeyLength - aValueLength <= gSnapshotPrefill) {
        return LSSnapshot::LoadState::AllOrderedItems;
      }

      return LSSnapshot::LoadState::AllOrderedKeys;
    }

    return LSSnapshot::LoadState::Partial;
  };

  // Value for given aKey if aKey is not void (can be void too if value doesn't
  // exist for given aKey).
  LSValue value;
  // If aKey and value are not void, checkKey will be set to true. Once we find
  // an item for given aKey in one of the loops below, checkKey is set to false
  // to prevent additional comparison of strings (string implementation compares
  // string lengths first to avoid char by char comparison if possible).
  bool checkKey = false;

  // Avoid additional hash lookup if all ordered items fit into initial prefill
  // already.
  LSSnapshot::LoadState loadState = GetLoadState(/* aKeyLength */ 0,
                                                 /* aValueLength */ 0);
  if (loadState != LSSnapshot::LoadState::AllOrderedItems && !aKey.IsVoid()) {
    GetItem(aKey, value);
    if (!value.IsVoid()) {
      // Ok, we have a non void aKey and value.

      // We have to watch for aKey during one of the loops below to exclude it
      // from the size computation. The super fast mode (AllOrderedItems)
      // doesn't have to do that though.
      checkKey = true;

      // We have to compute load state again because aKey length and value
      // length is excluded from the size in this case.
      loadState = GetLoadState(aKey.Length(), value.Length());
    }
  }

  switch (loadState) {
    case LSSnapshot::LoadState::AllOrderedItems: {
      // We're sending all ordered items, we don't need to check keys because
      // mOrderedItems must contain a value for aKey if checkKey is true.

      aItemInfos.AppendElements(mOrderedItems);

      MOZ_ASSERT(aItemInfos.Length() == mValues.Count());
      aNextLoadIndex = mValues.Count();

      aAddKeyToUnknownItems = false;

      break;
    }

    case LSSnapshot::LoadState::AllOrderedKeys: {
      // We don't have enough snapshot budget to send all items, but we do have
      // enough to send all of the keys and to make a best effort to populate as
      // many values as possible. We send void string values once we run out of
      // budget. A complicating factor is that we want to make sure that we send
      // the value for aKey which is a localStorage read that's triggering this
      // request. Since that key can happen anywhere in the list of items, we
      // need to handle it specially.
      //
      // The loop is effectively doing 2 things in parallel:
      //
      //   1. Looking for the `aKey` to send. This is tracked by `checkKey`
      //      which is true if there was an `aKey` specified and until we
      //      populate its value, and false thereafter.
      //   2. Sending values until we run out of `size` budget and switch to
      //      sending void values. `doneSendingValues` tracks when we've run out
      //      of size budget, with `setVoidValue` tracking whether a value
      //      should be sent for each turn of the event loop but can be
      //      overridden when `aKey` is found.

      int64_t size = mSizeOfKeys;
      bool setVoidValue = false;
      bool doneSendingValues = false;
      for (uint32_t index = 0; index < mOrderedItems.Length(); index++) {
        const LSItemInfo& item = mOrderedItems[index];

        const nsString& key = item.key();
        const LSValue& value = item.value();

        if (checkKey && key == aKey) {
          checkKey = false;
          setVoidValue = false;
        } else if (!setVoidValue) {
          if (doneSendingValues) {
            setVoidValue = true;
          } else {
            size += static_cast<int64_t>(value.Length());

            if (size > gSnapshotPrefill) {
              setVoidValue = true;
              doneSendingValues = true;

              // We set doneSendingValues to true and that will guard against
              // entering this branch during next iterations. So aNextLoadIndex
              // is set only once.
              aNextLoadIndex = index;
            }
          }
        }

        LSItemInfo* itemInfo = aItemInfos.AppendElement();
        itemInfo->key() = key;
        if (setVoidValue) {
          itemInfo->value().SetIsVoid(true);
        } else {
          aLoadedItems.PutEntry(key);
          itemInfo->value() = value;
        }
      }

      aAddKeyToUnknownItems = false;

      break;
    }

    case LSSnapshot::LoadState::Partial: {
      int64_t size = 0;
      for (uint32_t index = 0; index < mOrderedItems.Length(); index++) {
        const LSItemInfo& item = mOrderedItems[index];

        const nsString& key = item.key();
        const LSValue& value = item.value();

        if (checkKey && key == aKey) {
          checkKey = false;
        } else {
          size += static_cast<int64_t>(key.Length()) +
                  static_cast<int64_t>(value.Length());

          if (size > gSnapshotPrefill) {
            aNextLoadIndex = index;
            break;
          }
        }

        aLoadedItems.PutEntry(key);

        LSItemInfo* itemInfo = aItemInfos.AppendElement();
        itemInfo->key() = key;
        itemInfo->value() = value;
      }

      aAddKeyToUnknownItems = false;

      if (!aKey.IsVoid()) {
        if (value.IsVoid()) {
          aAddKeyToUnknownItems = true;
        } else if (checkKey) {
          // The item wasn't added in the loop above, add it here.

          LSItemInfo* itemInfo = aItemInfos.AppendElement();
          itemInfo->key() = aKey;
          itemInfo->value() = value;
        }
      }

      MOZ_ASSERT(aItemInfos.Length() < mOrderedItems.Length());

      break;
    }

    default:
      MOZ_CRASH("Bad load state value!");
  }

  aLoadState = loadState;
}

void Datastore::GetItem(const nsAString& aKey, LSValue& aValue) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mClosed);

  if (!mValues.Get(aKey, &aValue)) {
    aValue.SetIsVoid(true);
  }
}

void Datastore::GetKeys(nsTArray<nsString>& aKeys) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mClosed);

  for (auto item : mOrderedItems) {
    aKeys.AppendElement(item.key());
  }
}

void Datastore::SetItem(Database* aDatabase, const nsString& aKey,
                        const LSValue& aValue) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(!mClosed);
  MOZ_ASSERT(mInUpdateBatch);

  LSValue oldValue;
  GetItem(aKey, oldValue);

  if (oldValue != aValue) {
    bool isNewItem = oldValue.IsVoid();

    NotifySnapshots(aDatabase, aKey, oldValue, /* affectsOrder */ isNewItem);

    mValues.InsertOrUpdate(aKey, aValue);

    int64_t delta;

    if (isNewItem) {
      mWriteOptimizer.InsertItem(aKey, aValue);

      int64_t sizeOfKey = static_cast<int64_t>(aKey.Length());

      delta = sizeOfKey + static_cast<int64_t>(aValue.UTF16Length());

      mUpdateBatchUsage += delta;

      mSizeOfKeys += sizeOfKey;
      mSizeOfItems += sizeOfKey + static_cast<int64_t>(aValue.Length());
    } else {
      mWriteOptimizer.UpdateItem(aKey, aValue);

      delta = static_cast<int64_t>(aValue.UTF16Length()) -
              static_cast<int64_t>(oldValue.UTF16Length());

      mUpdateBatchUsage += delta;

      mSizeOfItems += static_cast<int64_t>(aValue.Length()) -
                      static_cast<int64_t>(oldValue.Length());
    }

    if (IsPersistent()) {
      mConnection->SetItem(aKey, aValue, delta, isNewItem);
    }
  }
}

void Datastore::RemoveItem(Database* aDatabase, const nsString& aKey) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(!mClosed);
  MOZ_ASSERT(mInUpdateBatch);

  LSValue oldValue;
  GetItem(aKey, oldValue);

  if (!oldValue.IsVoid()) {
    NotifySnapshots(aDatabase, aKey, oldValue, /* aAffectsOrder */ true);

    mValues.Remove(aKey);

    mWriteOptimizer.DeleteItem(aKey);

    int64_t sizeOfKey = static_cast<int64_t>(aKey.Length());

    int64_t delta = -sizeOfKey - static_cast<int64_t>(oldValue.UTF16Length());

    mUpdateBatchUsage += delta;

    mSizeOfKeys -= sizeOfKey;
    mSizeOfItems -= sizeOfKey + static_cast<int64_t>(oldValue.Length());

    if (IsPersistent()) {
      mConnection->RemoveItem(aKey, delta);
    }
  }
}

void Datastore::Clear(Database* aDatabase) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mClosed);

  if (mValues.Count()) {
    int64_t delta = 0;
    for (const auto& entry : mValues) {
      const nsAString& key = entry.GetKey();
      const LSValue& value = entry.GetData();

      delta += -static_cast<int64_t>(key.Length()) -
               static_cast<int64_t>(value.UTF16Length());

      NotifySnapshots(aDatabase, key, value, /* aAffectsOrder */ true);
    }

    mValues.Clear();

    if (mInUpdateBatch) {
      mWriteOptimizer.Truncate();

      mUpdateBatchUsage += delta;
    } else {
      mOrderedItems.Clear();

      DebugOnly<bool> ok = UpdateUsage(delta);
      MOZ_ASSERT(ok);
    }

    mSizeOfKeys = 0;
    mSizeOfItems = 0;

    if (IsPersistent()) {
      mConnection->Clear(delta);
    }
  }
}

void Datastore::BeginUpdateBatch(int64_t aSnapshotUsage) {
  AssertIsOnBackgroundThread();
  // Don't assert `aSnapshotUsage >= 0`, it can be negative when multiple
  // snapshots are operating in parallel.
  MOZ_ASSERT(!mClosed);
  MOZ_ASSERT(mUpdateBatchUsage == -1);
  MOZ_ASSERT(!mInUpdateBatch);

  mUpdateBatchUsage = aSnapshotUsage;

  if (IsPersistent()) {
    mConnection->BeginUpdateBatch();
  }

  mInUpdateBatch = true;
}

int64_t Datastore::EndUpdateBatch(int64_t aSnapshotPeakUsage) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mClosed);
  MOZ_ASSERT(mInUpdateBatch);

  mWriteOptimizer.ApplyAndReset(mOrderedItems);

  MOZ_ASSERT(!mWriteOptimizer.HasWrites());

  if (aSnapshotPeakUsage >= 0) {
    int64_t delta = mUpdateBatchUsage - aSnapshotPeakUsage;

    if (mActiveDatabases.Count()) {
      // We can't apply deltas while other databases are still active.
      // The final delta must be zero or negative, but individual deltas can
      // be positive. A positive delta can't be applied asynchronously since
      // there's no way to fire the quota exceeded error event.

      mPendingUsageDeltas.AppendElement(delta);
    } else {
      MOZ_ASSERT(delta <= 0);
      if (delta != 0) {
        DebugOnly<bool> ok = UpdateUsage(delta);
        MOZ_ASSERT(ok);
      }
    }
  }

  int64_t result = mUpdateBatchUsage;
  mUpdateBatchUsage = -1;

  if (IsPersistent()) {
    mConnection->EndUpdateBatch();
  }

  mInUpdateBatch = false;

  return result;
}

int64_t Datastore::AttemptToUpdateUsage(int64_t aMinSize, bool aInitial) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT_IF(aInitial, aMinSize >= 0);
  MOZ_ASSERT_IF(!aInitial, aMinSize > 0);

  const int64_t size = aMinSize + GetSnapshotPeakUsagePreincrement(aInitial);

  if (size && UpdateUsage(size)) {
    return size;
  }

  const int64_t reducedSize =
      aMinSize + GetSnapshotPeakUsageReducedPreincrement(aInitial);

  if (reducedSize && UpdateUsage(reducedSize)) {
    return reducedSize;
  }

  if (aMinSize > 0 && UpdateUsage(aMinSize)) {
    return aMinSize;
  }

  return 0;
}

bool Datastore::HasOtherProcessObservers(Database* aDatabase) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);

  if (!gObservers) {
    return false;
  }

  nsTArray<NotNull<Observer*>>* array;
  if (!gObservers->Get(mOriginMetadata.mOrigin, &array)) {
    return false;
  }

  MOZ_ASSERT(array);

  for (Observer* observer : *array) {
    if (observer->ContentParentIdRef() != aDatabase->ContentParentIdRef()) {
      return true;
    }
  }

  return false;
}

void Datastore::NotifyOtherProcessObservers(Database* aDatabase,
                                            const nsString& aDocumentURI,
                                            const nsString& aKey,
                                            const LSValue& aOldValue,
                                            const LSValue& aNewValue) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);

  if (!gObservers) {
    return;
  }

  nsTArray<NotNull<Observer*>>* array;
  if (!gObservers->Get(mOriginMetadata.mOrigin, &array)) {
    return;
  }

  MOZ_ASSERT(array);

  // We do not want to send information about events back to the content process
  // that caused the change.

  for (Observer* observer : *array) {
    if (observer->ContentParentIdRef() != aDatabase->ContentParentIdRef()) {
      observer->Observe(aDatabase, aDocumentURI, aKey, aOldValue, aNewValue);
    }
  }
}

void Datastore::NoteChangedObserverArray(
    const nsTArray<NotNull<Observer*>>& aObservers) {
  AssertIsOnBackgroundThread();

  for (Database* database : mActiveDatabases) {
    Snapshot* snapshot = database->GetSnapshot();
    MOZ_ASSERT(snapshot);

    if (snapshot->IsDirty()) {
      continue;
    }

    bool hasOtherProcessObservers = false;

    for (Observer* observer : aObservers) {
      if (observer->ContentParentIdRef() != database->ContentParentIdRef()) {
        hasOtherProcessObservers = true;
        break;
      }
    }

    if (snapshot->HasOtherProcessObservers() != hasOtherProcessObservers) {
      snapshot->MarkDirty();
    }
  }
}

void Datastore::Stringify(nsACString& aResult) const {
  AssertIsOnBackgroundThread();

  aResult.AppendLiteral("DirectoryLock:");
  aResult.AppendInt(!!mDirectoryLockHandle);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Connection:");
  aResult.AppendInt(!!mConnection);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("QuotaObject:");
  aResult.AppendInt(!!mQuotaObject);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("PrepareDatastoreOps:");
  aResult.AppendInt(mPrepareDatastoreOps.Count());
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("PreparedDatastores:");
  aResult.AppendInt(mPreparedDatastores.Count());
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Databases:");
  aResult.AppendInt(mDatabases.Count());
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("ActiveDatabases:");
  aResult.AppendInt(mActiveDatabases.Count());
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Origin:");
  aResult.Append(AnonymizedOriginString(mOriginMetadata.mOrigin));
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("PrivateBrowsingId:");
  aResult.AppendInt(mPrivateBrowsingId);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Closed:");
  aResult.AppendInt(mClosed);
}

bool Datastore::UpdateUsage(int64_t aDelta) {
  AssertIsOnBackgroundThread();

  // Check internal LocalStorage origin limit.
  int64_t newUsage = mUsage + aDelta;

  MOZ_ASSERT(newUsage >= 0);

  if (newUsage > StaticPrefs::dom_storage_default_quota() * 1024) {
    return false;
  }

  // Check QuotaManager limits (group and global limit).
  if (IsPersistent()) {
    MOZ_ASSERT(mQuotaObject);

    if (!mQuotaObject->MaybeUpdateSize(newUsage, /* aTruncate */ true)) {
      return false;
    }
  }

  // Quota checks passed, set new usage.
  mUsage = newUsage;

  return true;
}

void Datastore::MaybeClose() {
  AssertIsOnBackgroundThread();

  if (!mPrepareDatastoreOps.Count() && !mHasLivePrivateDatastore &&
      !mPreparedDatastores.Count() && !mDatabases.Count()) {
    Close();
  }
}

void Datastore::ConnectionClosedCallback() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(mConnection);
  MOZ_ASSERT(mQuotaObject);
  MOZ_ASSERT(mClosed);

  // Release the quota object first.
  mQuotaObject = nullptr;

  bool databaseWasNotAvailable;
  bool hasCreatedDatabase;
  mConnection->GetFinishInfo(databaseWasNotAvailable, hasCreatedDatabase);

  if (databaseWasNotAvailable && !hasCreatedDatabase) {
    MOZ_ASSERT(mUsage == 0);

    QuotaManager* quotaManager = QuotaManager::Get();
    MOZ_ASSERT(quotaManager);

    quotaManager->ResetUsageForClient(
        ClientMetadata{mOriginMetadata, mozilla::dom::quota::Client::LS});
  }

  mConnection = nullptr;

  // Now it's safe to release the directory lock and unregister itself from
  // the hashtable.

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  CleanupMetadata();

  if (mCompleteCallback) {
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(mCompleteCallback.forget()));
  }
}

void Datastore::CleanupMetadata() {
  AssertIsOnBackgroundThread();

  MOZ_ASSERT(gDatastores);
  const DebugOnly<bool> removed = gDatastores->Remove(mOriginMetadata.mOrigin);
  MOZ_ASSERT(removed);

  QuotaManager::MaybeRecordQuotaClientShutdownStep(quota::Client::LS,
                                                   "Datastore removed"_ns);

  if (!gDatastores->Count()) {
    gDatastores = nullptr;
  }
}

void Datastore::NotifySnapshots(Database* aDatabase, const nsAString& aKey,
                                const LSValue& aOldValue, bool aAffectsOrder) {
  AssertIsOnBackgroundThread();

  for (Database* database : mDatabases) {
    MOZ_ASSERT(database);

    if (database == aDatabase) {
      continue;
    }

    Snapshot* snapshot = database->GetSnapshot();
    if (snapshot) {
      snapshot->SaveItem(aKey, aOldValue, aAffectsOrder);
    }
  }
}

void Datastore::NoteChangedDatabaseMap() {
  AssertIsOnBackgroundThread();

  for (Database* database : mActiveDatabases) {
    Snapshot* snapshot = database->GetSnapshot();
    MOZ_ASSERT(snapshot);

    if (snapshot->IsDirty()) {
      continue;
    }

    if (snapshot->HasOtherProcessDatabases() !=
        HasOtherProcessDatabases(database)) {
      snapshot->MarkDirty();
    }
  }
}

/*******************************************************************************
 * PreparedDatastore
 ******************************************************************************/

void PreparedDatastore::Destroy() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(gPreparedDatastores);
  DebugOnly<bool> removed = gPreparedDatastores->Remove(mDatastoreId);
  MOZ_ASSERT(removed);
}

// static
void PreparedDatastore::TimerCallback(nsITimer* aTimer, void* aClosure) {
  AssertIsOnBackgroundThread();

  auto* self = static_cast<PreparedDatastore*>(aClosure);
  MOZ_ASSERT(self);

  self->Destroy();
}

/*******************************************************************************
 * Database
 ******************************************************************************/

Database::Database(const PrincipalInfo& aPrincipalInfo,
                   const Maybe<ContentParentId>& aContentParentId,
                   const nsACString& aOrigin, uint32_t aPrivateBrowsingId)
    : mSnapshot(nullptr),
      mPrincipalInfo(aPrincipalInfo),
      mContentParentId(aContentParentId),
      mOrigin(aOrigin),
      mRequestAllowToCloseTimerId(0),
      mPrivateBrowsingId(aPrivateBrowsingId),
      mAllowedToClose(false),
      mActorDestroyed(false),
      mRequestedAllowToClose(false)
#ifdef DEBUG
      ,
      mActorWasAlive(false)
#endif
{
  AssertIsOnOwningThread();

  if (!gDatabases) {
    gDatabases = new DatabaseArray();
  }

  gDatabases->AppendElement(this);
}

Database::~Database() {
  AssertIsOnOwningThread();
  MOZ_ASSERT_IF(mActorWasAlive, mAllowedToClose);
  MOZ_ASSERT_IF(mActorWasAlive, mActorDestroyed);

  MOZ_ASSERT(gDatabases);
  gDatabases->RemoveElement(this);

  if (gDatabases->IsEmpty()) {
    gDatabases = nullptr;
  }
}

void Database::SetActorAlive(Datastore* aDatastore) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mActorWasAlive);
  MOZ_ASSERT(!mActorDestroyed);

#ifdef DEBUG
  mActorWasAlive = true;
#endif

  mDatastore = aDatastore;

  mDatastore->NoteLiveDatabase(this);

  if (!gLiveDatabases) {
    gLiveDatabases = new LiveDatabaseArray();
  }

  gLiveDatabases->AppendElement(WrapNotNullUnchecked(this));
}

void Database::RegisterSnapshot(Snapshot* aSnapshot) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aSnapshot);
  MOZ_ASSERT(!mSnapshot);
  MOZ_ASSERT(!mAllowedToClose);

  // Only one snapshot at a time is currently supported.
  mSnapshot = aSnapshot;

  mDatastore->NoteActiveDatabase(this);
}

void Database::UnregisterSnapshot(Snapshot* aSnapshot) {
  MOZ_ASSERT(aSnapshot);
  MOZ_ASSERT(mSnapshot == aSnapshot);

  mSnapshot = nullptr;

  mDatastore->NoteInactiveDatabase(this);
}

void Database::RequestAllowToClose() {
  AssertIsOnOwningThread();

  if (mRequestedAllowToClose) {
    return;
  }

  mRequestedAllowToClose = true;

  // Send the RequestAllowToClose message to the child to avoid racing with the
  // child actor. Except the case when the actor was already destroyed.
  if (mActorDestroyed) {
    MOZ_ASSERT(mAllowedToClose);
    return;
  }

  if (NS_WARN_IF(!SendRequestAllowToClose())) {
    if (!mSnapshot) {
      // This is not necessary, because there should be a runnable scheduled
      // that will call ActorDestroy which calls AllowToClose. However we can
      // speedup the shutdown a bit if we do it here directly, but only if
      // there's no registered snapshot.
      AllowToClose();
    }
  } else {
    mRequestAllowToCloseTimerId =
        glean::localstorage_database::request_allow_to_close_response_time
            .Start();
  }
}

void Database::ForceKill() {
  AssertIsOnOwningThread();

  if (mActorDestroyed) {
    MOZ_ASSERT(mAllowedToClose);
    return;
  }

  Close();
}

void Database::Stringify(nsACString& aResult) const {
  AssertIsOnOwningThread();

  aResult.AppendLiteral("SnapshotRegistered:");
  aResult.AppendInt(!!mSnapshot);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("OtherProcessActor:");
  aResult.AppendInt(mContentParentId.isSome());
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Origin:");
  aResult.Append(AnonymizedOriginString(mOrigin));
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("PrivateBrowsingId:");
  aResult.AppendInt(mPrivateBrowsingId);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("AllowedToClose:");
  aResult.AppendInt(mAllowedToClose);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("ActorDestroyed:");
  aResult.AppendInt(mActorDestroyed);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("RequestedAllowToClose:");
  aResult.AppendInt(mRequestedAllowToClose);
}

void Database::AllowToClose() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mAllowedToClose);
  MOZ_ASSERT(mDatastore);
  MOZ_ASSERT(!mSnapshot);

  mAllowedToClose = true;

  mDatastore->NoteFinishedDatabase(this);

  mDatastore = nullptr;

  MOZ_ASSERT(gLiveDatabases);
  gLiveDatabases->RemoveElement(this);

  QuotaManager::MaybeRecordQuotaClientShutdownStep(quota::Client::LS,
                                                   "Live database removed"_ns);

  if (gLiveDatabases->IsEmpty()) {
    gLiveDatabases = nullptr;
  }
}

void Database::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mActorDestroyed);

  mActorDestroyed = true;

  if (!mAllowedToClose) {
    AllowToClose();
  }
}

mozilla::ipc::IPCResult Database::RecvAllowToClose() {
  AssertIsOnOwningThread();

  if (NS_WARN_IF(mAllowedToClose)) {
    return IPC_FAIL(this, "mAllowedToClose already set!");
  }

  // AllowToClose can be sent from content without a prior request from the
  // parent (RequestAllowToClose). Therefore, we need to check for this;
  // otherwise, we would receive warnings from Glean about timing not running.
  if (mRequestedAllowToClose) {
    // mRequestAllowToCloseTimerId is initialized to zero in the constructor,
    // and the Start() method always returns id that is greater than zero, so
    // we can assert that here.
    MOZ_ASSERT(mRequestAllowToCloseTimerId);

    glean::localstorage_database::request_allow_to_close_response_time
        .StopAndAccumulate(std::move(mRequestAllowToCloseTimerId));
  }

  AllowToClose();

  return IPC_OK();
}

PBackgroundLSSnapshotParent* Database::AllocPBackgroundLSSnapshotParent(
    const nsAString& aDocumentURI, const nsAString& aKey,
    const bool& aIncreasePeakUsage, const int64_t& aMinSize,
    LSSnapshotInitInfo* aInitInfo) {
  AssertIsOnOwningThread();

  if (NS_WARN_IF(aIncreasePeakUsage && aMinSize < 0)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return nullptr;
  }

  if (NS_WARN_IF(mAllowedToClose)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return nullptr;
  }

  RefPtr<Snapshot> snapshot = new Snapshot(this, aDocumentURI);

  // Transfer ownership to IPDL.
  return snapshot.forget().take();
}

mozilla::ipc::IPCResult Database::RecvPBackgroundLSSnapshotConstructor(
    PBackgroundLSSnapshotParent* aActor, const nsAString& aDocumentURI,
    const nsAString& aKey, const bool& aIncreasePeakUsage,
    const int64_t& aMinSize, LSSnapshotInitInfo* aInitInfo) {
  AssertIsOnOwningThread();
  MOZ_ASSERT_IF(aIncreasePeakUsage, aMinSize >= 0);
  MOZ_ASSERT(aInitInfo);
  MOZ_ASSERT(!mAllowedToClose);

  auto* snapshot = static_cast<Snapshot*>(aActor);

  bool addKeyToUnknownItems;
  nsTHashtable<nsStringHashKey> loadedItems;
  nsTArray<LSItemInfo> itemInfos;
  uint32_t nextLoadIndex;
  LSSnapshot::LoadState loadState;
  mDatastore->GetSnapshotLoadInfo(aKey, addKeyToUnknownItems, loadedItems,
                                  itemInfos, nextLoadIndex, loadState);

  nsTHashSet<nsString> unknownItems;
  if (addKeyToUnknownItems) {
    unknownItems.Insert(aKey);
  }

  uint32_t totalLength = mDatastore->GetLength();

  int64_t usage = mDatastore->GetUsage();

  int64_t peakUsage = usage;

  if (aIncreasePeakUsage) {
    int64_t size =
        mDatastore->AttemptToUpdateUsage(aMinSize, /* aInitial */ true);

    peakUsage += size;
  }

  bool hasOtherProcessDatabases = mDatastore->HasOtherProcessDatabases(this);
  bool hasOtherProcessObservers = mDatastore->HasOtherProcessObservers(this);

  snapshot->Init(loadedItems, std::move(unknownItems), nextLoadIndex,
                 totalLength, usage, peakUsage, loadState,
                 hasOtherProcessDatabases, hasOtherProcessObservers);

  RegisterSnapshot(snapshot);

  aInitInfo->addKeyToUnknownItems() = addKeyToUnknownItems;
  aInitInfo->itemInfos() = std::move(itemInfos);
  aInitInfo->totalLength() = totalLength;
  aInitInfo->usage() = usage;
  aInitInfo->peakUsage() = peakUsage;
  aInitInfo->loadState() = loadState;
  aInitInfo->hasOtherProcessDatabases() = hasOtherProcessDatabases;
  aInitInfo->hasOtherProcessObservers() = hasOtherProcessObservers;

  return IPC_OK();
}

bool Database::DeallocPBackgroundLSSnapshotParent(
    PBackgroundLSSnapshotParent* aActor) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aActor);

  // Transfer ownership back from IPDL.
  RefPtr<Snapshot> actor = dont_AddRef(static_cast<Snapshot*>(aActor));

  return true;
}

/*******************************************************************************
 * Snapshot
 ******************************************************************************/

Snapshot::Snapshot(Database* aDatabase, const nsAString& aDocumentURI)
    : mDatabase(aDatabase),
      mDatastore(aDatabase->GetDatastore()),
      mDocumentURI(aDocumentURI),
      mTotalLength(0),
      mUsage(-1),
      mPeakUsage(-1),
      mSavedKeys(false),
      mActorDestroyed(false),
      mFinishReceived(false),
      mLoadedReceived(false),
      mLoadedAllItems(false),
      mLoadKeysReceived(false),
      mSentMarkDirty(false) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);
}

Snapshot::~Snapshot() {
  MOZ_ASSERT(mActorDestroyed);
  MOZ_ASSERT(mFinishReceived);
}

void Snapshot::SaveItem(const nsAString& aKey, const LSValue& aOldValue,
                        bool aAffectsOrder) {
  AssertIsOnBackgroundThread();

  MarkDirty();

  if (mLoadedAllItems) {
    return;
  }

  if (!mLoadedItems.Contains(aKey) && !mUnknownItems.Contains(aKey)) {
    mValues.LookupOrInsert(aKey, aOldValue);
  }

  if (aAffectsOrder && !mSavedKeys) {
    mDatastore->GetKeys(mKeys);
    mSavedKeys = true;
  }
}

void Snapshot::MarkDirty() {
  AssertIsOnBackgroundThread();

  if (!mSentMarkDirty) {
    Unused << SendMarkDirty();
    mSentMarkDirty = true;
  }
}

void Snapshot::Finish() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mDatabase);
  MOZ_ASSERT(mDatastore);
  MOZ_ASSERT(!mFinishReceived);

  mDatastore->BeginUpdateBatch(mUsage);

  mDatastore->EndUpdateBatch(mPeakUsage);

  mDatabase->UnregisterSnapshot(this);

  mFinishReceived = true;
}

void Snapshot::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  mActorDestroyed = true;

  if (!mFinishReceived) {
    Finish();
  }
}

mozilla::ipc::IPCResult Snapshot::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  IProtocol* mgr = Manager();
  if (!PBackgroundLSSnapshotParent::Send__delete__(this)) {
    return IPC_FAIL(mgr, "Send__delete__ failed!");
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::Checkpoint(
    nsTArray<LSWriteInfo>&& aWriteInfos) {
  AssertIsOnBackgroundThread();
  // Don't assert `mUsage >= 0`, it can be negative when multiple snapshots are
  // operating in parallel.
  MOZ_ASSERT(mPeakUsage >= mUsage);

  if (NS_WARN_IF(aWriteInfos.IsEmpty())) {
    return IPC_FAIL(this, "aWriteInfos is empty!");
  }

  if (NS_WARN_IF(mHasOtherProcessObservers)) {
    return IPC_FAIL(this, "mHasOtherProcessObservers already set!");
  }

  mDatastore->BeginUpdateBatch(mUsage);

  for (uint32_t index = 0; index < aWriteInfos.Length(); index++) {
    const LSWriteInfo& writeInfo = aWriteInfos[index];

    switch (writeInfo.type()) {
      case LSWriteInfo::TLSSetItemInfo: {
        const LSSetItemInfo& info = writeInfo.get_LSSetItemInfo();

        mDatastore->SetItem(mDatabase, info.key(), info.value());

        break;
      }

      case LSWriteInfo::TLSRemoveItemInfo: {
        const LSRemoveItemInfo& info = writeInfo.get_LSRemoveItemInfo();

        mDatastore->RemoveItem(mDatabase, info.key());

        break;
      }

      case LSWriteInfo::TLSClearInfo: {
        mDatastore->Clear(mDatabase);

        break;
      }

      default:
        MOZ_CRASH("Should never get here!");
    }
  }

  mUsage = mDatastore->EndUpdateBatch(-1);

  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::CheckpointAndNotify(
    nsTArray<LSWriteAndNotifyInfo>&& aWriteAndNotifyInfos) {
  AssertIsOnBackgroundThread();
  // Don't assert `mUsage >= 0`, it can be negative when multiple snapshots are
  // operating in parallel.
  MOZ_ASSERT(mPeakUsage >= mUsage);

  if (NS_WARN_IF(aWriteAndNotifyInfos.IsEmpty())) {
    return IPC_FAIL(this, "aWriteAndNotifyInfos is empty!");
  }

  if (NS_WARN_IF(!mHasOtherProcessObservers)) {
    return IPC_FAIL(this, "mHasOtherProcessObservers is not set!");
  }

  mDatastore->BeginUpdateBatch(mUsage);

  for (uint32_t index = 0; index < aWriteAndNotifyInfos.Length(); index++) {
    const LSWriteAndNotifyInfo& writeAndNotifyInfo =
        aWriteAndNotifyInfos[index];

    switch (writeAndNotifyInfo.type()) {
      case LSWriteAndNotifyInfo::TLSSetItemAndNotifyInfo: {
        const LSSetItemAndNotifyInfo& info =
            writeAndNotifyInfo.get_LSSetItemAndNotifyInfo();

        mDatastore->SetItem(mDatabase, info.key(), info.value());

        mDatastore->NotifyOtherProcessObservers(
            mDatabase, mDocumentURI, info.key(), info.oldValue(), info.value());

        break;
      }

      case LSWriteAndNotifyInfo::TLSRemoveItemAndNotifyInfo: {
        const LSRemoveItemAndNotifyInfo& info =
            writeAndNotifyInfo.get_LSRemoveItemAndNotifyInfo();

        mDatastore->RemoveItem(mDatabase, info.key());

        mDatastore->NotifyOtherProcessObservers(mDatabase, mDocumentURI,
                                                info.key(), info.oldValue(),
                                                VoidLSValue());

        break;
      }

      case LSWriteAndNotifyInfo::TLSClearInfo: {
        mDatastore->Clear(mDatabase);

        mDatastore->NotifyOtherProcessObservers(mDatabase, mDocumentURI,
                                                VoidString(), VoidLSValue(),
                                                VoidLSValue());

        break;
      }

      default:
        MOZ_CRASH("Should never get here!");
    }
  }

  mUsage = mDatastore->EndUpdateBatch(-1);

  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::RecvAsyncCheckpoint(
    nsTArray<LSWriteInfo>&& aWriteInfos) {
  return Checkpoint(std::move(aWriteInfos));
}

mozilla::ipc::IPCResult Snapshot::RecvAsyncCheckpointAndNotify(
    nsTArray<LSWriteAndNotifyInfo>&& aWriteAndNotifyInfos) {
  return CheckpointAndNotify(std::move(aWriteAndNotifyInfos));
}

mozilla::ipc::IPCResult Snapshot::RecvSyncCheckpoint(
    nsTArray<LSWriteInfo>&& aWriteInfos) {
  return Checkpoint(std::move(aWriteInfos));
}

mozilla::ipc::IPCResult Snapshot::RecvSyncCheckpointAndNotify(
    nsTArray<LSWriteAndNotifyInfo>&& aWriteAndNotifyInfos) {
  return CheckpointAndNotify(std::move(aWriteAndNotifyInfos));
}

mozilla::ipc::IPCResult Snapshot::RecvAsyncFinish() {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(mFinishReceived)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return IPC_FAIL(this, "Already finished");
  }

  Finish();

  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::RecvSyncFinish() {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(mFinishReceived)) {
    MOZ_ASSERT_UNLESS_FUZZING(false);
    return IPC_FAIL(this, "Already finished");
  }

  Finish();

  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::RecvLoaded() {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(mFinishReceived)) {
    return IPC_FAIL(this, "mFinishReceived already set!");
  }

  if (NS_WARN_IF(mLoadedReceived)) {
    return IPC_FAIL(this, "mLoadedReceived already set!");
  }

  if (NS_WARN_IF(mLoadedAllItems)) {
    return IPC_FAIL(this, "mLoadedAllItems already set!");
  }

  if (NS_WARN_IF(mLoadKeysReceived)) {
    return IPC_FAIL(this, "mLoadKeysReceived already set!");
  }

  mLoadedReceived = true;

  mLoadedItems.Clear();
  mUnknownItems.Clear();
  mValues.Clear();
  mKeys.Clear();
  mLoadedAllItems = true;
  mLoadKeysReceived = true;

  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::RecvLoadValueAndMoreItems(
    const nsAString& aKey, LSValue* aValue, nsTArray<LSItemInfo>* aItemInfos) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aValue);
  MOZ_ASSERT(aItemInfos);
  MOZ_ASSERT(mDatastore);

  if (NS_WARN_IF(mFinishReceived)) {
    return IPC_FAIL(this, "mFinishReceived already set!");
  }

  if (NS_WARN_IF(mLoadedReceived)) {
    return IPC_FAIL(this, "mLoadedReceived already set!");
  }

  if (NS_WARN_IF(mLoadedAllItems)) {
    return IPC_FAIL(this, "mLoadedAllItems already set!");
  }

  if (mLoadedItems.Contains(aKey)) {
    return IPC_FAIL(this, "mLoadedItems already contains aKey!");
  }

  if (mUnknownItems.Contains(aKey)) {
    return IPC_FAIL(this, "mUnknownItems already contains aKey!");
  }

  if (auto entry = mValues.Lookup(aKey)) {
    *aValue = entry.Data();
    entry.Remove();
  } else {
    mDatastore->GetItem(aKey, *aValue);
  }

  if (aValue->IsVoid()) {
    mUnknownItems.Insert(aKey);
  } else {
    mLoadedItems.PutEntry(aKey);

    // mLoadedItems.Count()==mTotalLength is checked below.
  }

  // Load some more key/value pairs (as many as the snapshot gradual prefill
  // byte budget allows).

  if (gSnapshotGradualPrefill > 0) {
    const nsTArray<LSItemInfo>& orderedItems = mDatastore->GetOrderedItems();

    uint32_t length;
    if (mSavedKeys) {
      length = mKeys.Length();
    } else {
      length = orderedItems.Length();
    }

    int64_t size = 0;
    while (mNextLoadIndex < length) {
      // If the datastore's ordering has changed, mSavedKeys will be true and
      // mKeys contains an ordered list of the keys. Otherwise we can use the
      // datastore's key ordering which is still the same as when the snapshot
      // was created.

      nsString key;
      if (mSavedKeys) {
        key = mKeys[mNextLoadIndex];
      } else {
        key = orderedItems[mNextLoadIndex].key();
      }

      // Normally we would do this:
      // if (!mLoadedItems.GetEntry(key)) {
      //   ...
      //   mLoadedItems.PutEntry(key);
      // }
      // but that requires two hash lookups. We can reduce that to just one
      // hash lookup if we always call PutEntry and check the number of entries
      // before and after the put (which is very cheap). However, if we reach
      // the prefill limit, we need to call RemoveEntry, but that is also cheap
      // because we pass the entry (not the key).

      uint32_t countBeforePut = mLoadedItems.Count();
      auto loadedItemEntry = mLoadedItems.PutEntry(key);
      if (countBeforePut != mLoadedItems.Count()) {
        // Check mValues first since that contains values as they existed when
        // our snapshot was created, but have since been changed/removed in the
        // datastore. If it's not there, then the datastore has the
        // still-current value. However, if the datastore's key ordering has
        // changed, we need to do a hash lookup rather than being able to do an
        // optimized direct access to the index.

        LSValue value;
        auto valueEntry = mValues.Lookup(key);
        if (valueEntry) {
          value = valueEntry.Data();
        } else if (mSavedKeys) {
          mDatastore->GetItem(nsString(key), value);
        } else {
          value = orderedItems[mNextLoadIndex].value();
        }

        // All not loaded keys must have a value.
        MOZ_ASSERT(!value.IsVoid());

        size += static_cast<int64_t>(key.Length()) +
                static_cast<int64_t>(value.Length());

        if (size > gSnapshotGradualPrefill) {
          mLoadedItems.RemoveEntry(loadedItemEntry);

          // mNextLoadIndex is not incremented, so we will resume at the same
          // position next time.
          break;
        }

        if (valueEntry) {
          valueEntry.Remove();
        }

        LSItemInfo* itemInfo = aItemInfos->AppendElement();
        itemInfo->key() = key;
        itemInfo->value() = value;
      }

      mNextLoadIndex++;
    }
  }

  if (mLoadedItems.Count() == mTotalLength) {
    mLoadedItems.Clear();
    mUnknownItems.Clear();
#ifdef DEBUG
    const bool allValuesVoid =
        std::all_of(mValues.Values().cbegin(), mValues.Values().cend(),
                    [](const auto& entry) { return entry.IsVoid(); });
    MOZ_ASSERT(allValuesVoid);
#endif
    mValues.Clear();
    mLoadedAllItems = true;
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::RecvLoadKeys(nsTArray<nsString>* aKeys) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aKeys);
  MOZ_ASSERT(mDatastore);

  if (NS_WARN_IF(mFinishReceived)) {
    return IPC_FAIL(this, "mFinishReceived already set!");
  }

  if (NS_WARN_IF(mLoadedReceived)) {
    return IPC_FAIL(this, "mLoadedReceived already set!");
  }

  if (NS_WARN_IF(mLoadKeysReceived)) {
    return IPC_FAIL(this, "mLoadKeysReceived already set!");
  }

  mLoadKeysReceived = true;

  if (mSavedKeys) {
    aKeys->AppendElements(std::move(mKeys));
  } else {
    mDatastore->GetKeys(*aKeys);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult Snapshot::RecvIncreasePeakUsage(const int64_t& aMinSize,
                                                        int64_t* aSize) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aSize);

  if (NS_WARN_IF(aMinSize <= 0)) {
    return IPC_FAIL(this, "aMinSize not valid!");
  }

  if (NS_WARN_IF(mFinishReceived)) {
    return IPC_FAIL(this, "mFinishReceived already set!");
  }

  int64_t size =
      mDatastore->AttemptToUpdateUsage(aMinSize, /* aInitial */ false);

  mPeakUsage += size;

  *aSize = size;

  return IPC_OK();
}

/*******************************************************************************
 * Observer
 ******************************************************************************/

Observer::Observer(const Maybe<ContentParentId>& aContentParentId,
                   const nsACString& aOrigin)
    : mContentParentId(aContentParentId),
      mOrigin(aOrigin),
      mActorDestroyed(false) {
  AssertIsOnBackgroundThread();
}

Observer::~Observer() { MOZ_ASSERT(mActorDestroyed); }

void Observer::Observe(Database* aDatabase, const nsString& aDocumentURI,
                       const nsString& aKey, const LSValue& aOldValue,
                       const LSValue& aNewValue) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);

  Unused << SendObserve(aDatabase->GetPrincipalInfo(),
                        aDatabase->PrivateBrowsingId(), aDocumentURI, aKey,
                        aOldValue, aNewValue);
}

void Observer::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  mActorDestroyed = true;

  MOZ_ASSERT(gObservers);

  nsTArray<NotNull<Observer*>>* array;
  gObservers->Get(mOrigin, &array);
  MOZ_ASSERT(array);

  array->RemoveElement(this);

  if (RefPtr<Datastore> datastore = GetDatastore(mOrigin)) {
    datastore->NoteChangedObserverArray(*array);
  }

  if (array->IsEmpty()) {
    gObservers->Remove(mOrigin);
  }

  if (!gObservers->Count()) {
    gObservers = nullptr;
  }
}

mozilla::ipc::IPCResult Observer::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  IProtocol* mgr = Manager();
  if (!PBackgroundLSObserverParent::Send__delete__(this)) {
    return IPC_FAIL(mgr, "Send__delete__ failed!");
  }
  return IPC_OK();
}

/*******************************************************************************
 * LSRequestBase
 ******************************************************************************/

LSRequestBase::LSRequestBase(const LSRequestParams& aParams,
                             const Maybe<ContentParentId>& aContentParentId)
    : mParams(aParams),
      mContentParentId(aContentParentId),
      mState(State::Initial),
      mWaitingForFinish(false) {}

LSRequestBase::~LSRequestBase() {
  MOZ_ASSERT_IF(MayProceedOnNonOwningThread(),
                mState == State::Initial || mState == State::Completed);
}

void LSRequestBase::Dispatch() {
  AssertIsOnOwningThread();

  mState = State::StartingRequest;

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(this));
}

void LSRequestBase::StringifyState(nsACString& aResult) const {
  AssertIsOnOwningThread();

  switch (mState) {
    case State::Initial:
      aResult.AppendLiteral("Initial");
      return;

    case State::StartingRequest:
      aResult.AppendLiteral("StartingRequest");
      return;

    case State::Nesting:
      aResult.AppendLiteral("Nesting");
      return;

    case State::SendingReadyMessage:
      aResult.AppendLiteral("SendingReadyMessage");
      return;

    case State::WaitingForFinish:
      aResult.AppendLiteral("WaitingForFinish");
      return;

    case State::SendingResults:
      aResult.AppendLiteral("SendingResults");
      return;

    case State::Completed:
      aResult.AppendLiteral("Completed");
      return;

    default:
      MOZ_CRASH("Bad state!");
  }
}

void LSRequestBase::Stringify(nsACString& aResult) const {
  AssertIsOnOwningThread();

  aResult.AppendLiteral("State:");
  StringifyState(aResult);
}

void LSRequestBase::Log() {
  AssertIsOnOwningThread();

  if (!LS_LOG_TEST()) {
    return;
  }

  LS_LOG(("LSRequestBase [%p]", this));

  nsCString state;
  StringifyState(state);

  LS_LOG(("  mState: %s", state.get()));
}

nsresult LSRequestBase::NestedRun() { return NS_OK; }

bool LSRequestBase::VerifyRequestParams() {
  AssertIsOnBackgroundThread();

  MOZ_ASSERT(mParams.type() != LSRequestParams::T__None);

  switch (mParams.type()) {
    case LSRequestParams::TLSRequestPreloadDatastoreParams: {
      const LSRequestCommonParams& params =
          mParams.get_LSRequestPreloadDatastoreParams().commonParams();

      if (NS_WARN_IF(!VerifyPrincipalInfo(
              params.principalInfo(), params.storagePrincipalInfo(), false))) {
        return false;
      }

      if (NS_WARN_IF(
              !VerifyOriginKey(params.originKey(), params.principalInfo()))) {
        return false;
      }

      break;
    }

    case LSRequestParams::TLSRequestPrepareDatastoreParams: {
      const LSRequestPrepareDatastoreParams& params =
          mParams.get_LSRequestPrepareDatastoreParams();

      const LSRequestCommonParams& commonParams = params.commonParams();

      if (NS_WARN_IF(!VerifyPrincipalInfo(commonParams.principalInfo(),
                                          commonParams.storagePrincipalInfo(),
                                          false))) {
        return false;
      }

      if (params.clientPrincipalInfo() &&
          NS_WARN_IF(!VerifyPrincipalInfo(commonParams.principalInfo(),
                                          params.clientPrincipalInfo().ref(),
                                          true))) {
        return false;
      }

      if (NS_WARN_IF(!VerifyClientId(mContentParentId,
                                     params.clientPrincipalInfo(),
                                     params.clientId()))) {
        return false;
      }

      if (NS_WARN_IF(!VerifyOriginKey(commonParams.originKey(),
                                      commonParams.principalInfo()))) {
        return false;
      }

      break;
    }

    case LSRequestParams::TLSRequestPrepareObserverParams: {
      const LSRequestPrepareObserverParams& params =
          mParams.get_LSRequestPrepareObserverParams();

      if (NS_WARN_IF(!VerifyPrincipalInfo(
              params.principalInfo(), params.storagePrincipalInfo(), false))) {
        return false;
      }

      if (params.clientPrincipalInfo() &&
          NS_WARN_IF(!VerifyPrincipalInfo(params.principalInfo(),
                                          params.clientPrincipalInfo().ref(),
                                          true))) {
        return false;
      }

      if (NS_WARN_IF(!VerifyClientId(mContentParentId,
                                     params.clientPrincipalInfo(),
                                     params.clientId()))) {
        return false;
      }

      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  return true;
}

nsresult LSRequestBase::StartRequest() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::StartingRequest);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    return NS_ERROR_ABORT;
  }

#ifdef DEBUG
  // Always verify parameters in DEBUG builds!
  bool trustParams = false;
#else
  bool trustParams = !BackgroundParent::IsOtherProcessActor(Manager());
#endif

  if (!trustParams && NS_WARN_IF(!VerifyRequestParams())) {
    return NS_ERROR_FAILURE;
  }

  QM_TRY(MOZ_TO_RESULT(Start()));

  return NS_OK;
}

void LSRequestBase::SendReadyMessage() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingReadyMessage);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    MaybeSetFailureCode(NS_ERROR_ABORT);
  }

  nsresult rv = SendReadyMessageInternal();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MaybeSetFailureCode(rv);

    FinishInternal();
  }
}

nsresult LSRequestBase::SendReadyMessageInternal() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingReadyMessage);

  if (!MayProceed()) {
    return NS_ERROR_ABORT;
  }

  if (NS_WARN_IF(!SendReady())) {
    return NS_ERROR_FAILURE;
  }

  localstorage::NotifyRequestFinalizationStarted();

  mState = State::WaitingForFinish;

  mWaitingForFinish = true;

  return NS_OK;
}

void LSRequestBase::Finish() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::WaitingForFinish);

  mWaitingForFinish = false;

  FinishInternal();
}

void LSRequestBase::FinishInternal() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingReadyMessage ||
             mState == State::WaitingForFinish);

  mState = State::SendingResults;

  // This LSRequestBase can only be held alive by the IPDL. Run() can end up
  // with clearing that last reference. So we need to add a self reference here.
  RefPtr<LSRequestBase> kungFuDeathGrip = this;

  MOZ_ALWAYS_SUCCEEDS(this->Run());
}

void LSRequestBase::SendResults() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    MaybeSetFailureCode(NS_ERROR_ABORT);
  }

  if (MayProceed()) {
    LSRequestResponse response;

    if (NS_SUCCEEDED(ResultCode())) {
      GetResponse(response);

      MOZ_ASSERT(response.type() != LSRequestResponse::T__None);

      if (response.type() == LSRequestResponse::Tnsresult) {
        MOZ_ASSERT(NS_FAILED(response.get_nsresult()));

        SetFailureCode(response.get_nsresult());
      }
    } else {
      response = ResultCode();
    }

    Unused << PBackgroundLSRequestParent::Send__delete__(this,
                                                         std::move(response));
  }

  Cleanup();

  mState = State::Completed;
}

NS_IMETHODIMP
LSRequestBase::Run() {
  nsresult rv;

  switch (mState) {
    case State::StartingRequest:
      rv = StartRequest();
      break;

    case State::Nesting:
      rv = NestedRun();
      break;

    case State::SendingReadyMessage:
      SendReadyMessage();
      return NS_OK;

    case State::SendingResults:
      SendResults();
      return NS_OK;

    default:
      MOZ_CRASH("Bad state!");
  }

  if (NS_WARN_IF(NS_FAILED(rv)) && mState != State::SendingReadyMessage) {
    MaybeSetFailureCode(rv);

    // Must set mState before dispatching otherwise we will race with the owning
    // thread.
    mState = State::SendingReadyMessage;

    if (IsOnOwningThread()) {
      SendReadyMessage();
    } else {
      MOZ_ALWAYS_SUCCEEDS(
          OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));
    }
  }

  return NS_OK;
}

void LSRequestBase::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  NoteComplete();

  // Assume ActorDestroy can happen at any time, so we can't probe the current
  // state since mState can be modified on any thread (only one thread at a time
  // based on the state machine).  However we can use mWaitingForFinish which is
  // only touched on the owning thread.  If mWaitingForFinisg is true, we can
  // also modify mState since we are guaranteed that there are no pending
  // runnables which would probe mState to decide what code needs to run (there
  // shouldn't be any running runnables on other threads either).

  if (mWaitingForFinish) {
    Finish();
  }

  // We don't have to handle the case when mWaitingForFinish is not true since
  // it means that either nothing has been initialized yet, so nothing to
  // cleanup or there are pending runnables that will detect that the actor has
  // been destroyed and cleanup accordingly.
}

mozilla::ipc::IPCResult LSRequestBase::RecvCancel() {
  AssertIsOnOwningThread();

  Log();

  glean::localstorage_request::recv_cancel_counter.Add();

  const char* crashOnCancel = PR_GetEnv("LSNG_CRASH_ON_CANCEL");
  if (crashOnCancel) {
    MOZ_CRASH("LSNG: Crash on cancel.");
  }

  IProtocol* mgr = Manager();
  if (!PBackgroundLSRequestParent::Send__delete__(this, NS_ERROR_ABORT)) {
    return IPC_FAIL(mgr, "Send__delete__ failed!");
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult LSRequestBase::RecvFinish() {
  AssertIsOnOwningThread();

  Finish();

  return IPC_OK();
}

/*******************************************************************************
 * PrepareDatastoreOp
 ******************************************************************************/

PrepareDatastoreOp::PrepareDatastoreOp(
    const LSRequestParams& aParams,
    const Maybe<ContentParentId>& aContentParentId)
    : LSRequestBase(aParams, aContentParentId),
      mProcessingTimerId(
          glean::localstorage_request::prepare_datastore_processing_time
              .Start()),
      mLoadDataOp(nullptr),
      mPrivateBrowsingId(0),
      mUsage(0),
      mSizeOfKeys(0),
      mSizeOfItems(0),
      mDatastoreId(0),
      mNestedState(NestedState::BeforeNesting),
      mForPreload(aParams.type() ==
                  LSRequestParams::TLSRequestPreloadDatastoreParams),
      mEnableMigration(
          StaticPrefs::
              dom_storage_enable_migration_from_unsupported_legacy_implementation()),
      mDatabaseNotAvailable(false),
      mInvalidated(false)
#ifdef DEBUG
      ,
      mDEBUGUsage(0)
#endif
{
  MOZ_ASSERT(
      aParams.type() == LSRequestParams::TLSRequestPreloadDatastoreParams ||
      aParams.type() == LSRequestParams::TLSRequestPrepareDatastoreParams);
}

PrepareDatastoreOp::~PrepareDatastoreOp() {
  MOZ_DIAGNOSTIC_ASSERT(mDirectoryLockHandle.IsInert());
  MOZ_DIAGNOSTIC_ASSERT(mExtraDirectoryLockHandle.IsInert());
  MOZ_ASSERT_IF(MayProceedOnNonOwningThread(),
                mState == State::Initial || mState == State::Completed);
  MOZ_ASSERT(!mLoadDataOp);
}

void PrepareDatastoreOp::StringifyNestedState(nsACString& aResult) const {
  AssertIsOnOwningThread();

  switch (mNestedState) {
    case NestedState::BeforeNesting:
      aResult.AppendLiteral("BeforeNesting");
      return;

    case NestedState::CheckExistingOperations:
      aResult.AppendLiteral("CheckExistingOperations");
      return;

    case NestedState::CheckClosingDatastore:
      aResult.AppendLiteral("CheckClosingDatastore");
      return;

    case NestedState::PreparationPending:
      aResult.AppendLiteral("PreparationPending");
      return;

    case NestedState::DirectoryOpenPending:
      aResult.AppendLiteral("DirectoryOpenPending");
      return;

    case NestedState::DatabaseWorkOpen:
      aResult.AppendLiteral("DatabaseWorkOpen");
      return;

    case NestedState::BeginLoadData:
      aResult.AppendLiteral("BeginLoadData");
      return;

    case NestedState::DatabaseWorkLoadData:
      aResult.AppendLiteral("DatabaseWorkLoadData");
      return;

    case NestedState::AfterNesting:
      aResult.AppendLiteral("AfterNesting");
      return;

    default:
      MOZ_CRASH("Bad state!");
  }
}

void PrepareDatastoreOp::Stringify(nsACString& aResult) const {
  AssertIsOnOwningThread();

  LSRequestBase::Stringify(aResult);
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Origin:");
  aResult.Append(AnonymizedOriginString(Origin()));
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("NestedState:");
  StringifyNestedState(aResult);
}

void PrepareDatastoreOp::Log() {
  AssertIsOnOwningThread();

  LSRequestBase::Log();

  if (!LS_LOG_TEST()) {
    return;
  }

  nsCString nestedState;
  StringifyNestedState(nestedState);

  LS_LOG(("  mNestedState: %s", nestedState.get()));

  switch (mNestedState) {
    case NestedState::CheckClosingDatastore: {
      for (const auto& blockedOn : mBlockedOn) {
        LS_LOG(("  blockedOn: [%p]", blockedOn.get().get()));

        blockedOn->Log();
      }

      break;
    }

    case NestedState::DirectoryOpenPending: {
      MOZ_ASSERT(mPendingDirectoryLock);

      LS_LOG(("  mPendingDirectoryLock: [%p]", mPendingDirectoryLock.get()));

      mPendingDirectoryLock->Log();

      break;
    }

    default:;
  }
}

nsresult PrepareDatastoreOp::Start() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::StartingRequest);
  MOZ_ASSERT(mNestedState == NestedState::BeforeNesting);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  QM_TRY(QuotaManager::EnsureCreated());

  const LSRequestCommonParams& commonParams =
      mForPreload
          ? mParams.get_LSRequestPreloadDatastoreParams().commonParams()
          : mParams.get_LSRequestPrepareDatastoreParams().commonParams();

  const PrincipalInfo& storagePrincipalInfo =
      commonParams.storagePrincipalInfo();

  if (storagePrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo) {
    mOriginMetadata = {quota::GetInfoForChrome(), PERSISTENCE_TYPE_DEFAULT};
  } else {
    MOZ_ASSERT(storagePrincipalInfo.type() ==
               PrincipalInfo::TContentPrincipalInfo);

    QM_TRY_UNWRAP(auto principalMetadata,
                  quota::GetInfoFromValidatedPrincipalInfo(
                      *QuotaManager::Get(), storagePrincipalInfo));

    mOriginMetadata.mSuffix = std::move(principalMetadata.mSuffix);
    mOriginMetadata.mGroup = std::move(principalMetadata.mGroup);
    // XXX We can probably get rid of mMainThreadOrigin if we change
    // LSRequestBase::Dispatch to synchronously run LSRequestBase::StartRequest
    // through LSRequestBase::Run.
    mMainThreadOrigin = std::move(principalMetadata.mOrigin);
    mOriginMetadata.mStorageOrigin =
        std::move(principalMetadata.mStorageOrigin);
    mOriginMetadata.mIsPrivate = principalMetadata.mIsPrivate;
    mOriginMetadata.mPersistenceType = principalMetadata.mIsPrivate
                                           ? PERSISTENCE_TYPE_PRIVATE
                                           : PERSISTENCE_TYPE_DEFAULT;
  }

  mState = State::Nesting;
  mNestedState = NestedState::OpenDirectory;

  // XXX We could call OpenDirectory directly here or even fold it here instead
  // of dispatching to the same event target.
  MOZ_ALWAYS_SUCCEEDS(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));

  return NS_OK;
}

nsresult PrepareDatastoreOp::OpenDirectory() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::OpenDirectory);
  MOZ_ASSERT(!mDirectoryLockHandle);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    return NS_ERROR_ABORT;
  }

  const LSRequestCommonParams& commonParams =
      mForPreload
          ? mParams.get_LSRequestPreloadDatastoreParams().commonParams()
          : mParams.get_LSRequestPrepareDatastoreParams().commonParams();

  const PrincipalInfo& storagePrincipalInfo =
      commonParams.storagePrincipalInfo();

  nsCString originAttrSuffix;
  uint32_t privateBrowsingId;

  if (storagePrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo) {
    privateBrowsingId = 0;
  } else {
    MOZ_ASSERT(storagePrincipalInfo.type() ==
               PrincipalInfo::TContentPrincipalInfo);

    const ContentPrincipalInfo& info =
        storagePrincipalInfo.get_ContentPrincipalInfo();
    const OriginAttributes& attrs = info.attrs();
    attrs.CreateSuffix(originAttrSuffix);

    privateBrowsingId = attrs.mPrivateBrowsingId;
  }

  mArchivedOriginScope = ArchivedOriginScope::CreateFromOrigin(
      originAttrSuffix, commonParams.originKey());
  MOZ_ASSERT(mArchivedOriginScope);

  // Normally it's safe to access member variables without a mutex because even
  // though we hop between threads, the variables are never accessed by multiple
  // threads at the same time.
  // However, the methods OriginIsKnown and Origin can be called at any time.
  // So we have to make sure the member variable is set on the same thread as
  // those methods are called.
  mOriginMetadata.mOrigin = mMainThreadOrigin;

  MOZ_ASSERT(OriginIsKnown());

  mPrivateBrowsingId = privateBrowsingId;

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  mNestedState = NestedState::DirectoryOpenPending;

  quotaManager
      ->OpenClientDirectory(
          {mOriginMetadata, mozilla::dom::quota::Client::LS},
          /* aInitializeOrigin */ !mForPreload || mEnableMigration,
          /* aCreateIfNonExistent */ false, SomeRef(mPendingDirectoryLock))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this)](QuotaManager::ClientDirectoryLockHandlePromise::
                                    ResolveOrRejectValue&& aValue) {
            self->mPendingDirectoryLock = nullptr;

            if (aValue.IsResolve()) {
              self->DirectoryLockAcquired(std::move(aValue.ResolveValue()));
            } else {
              self->DirectoryLockFailed();
            }
          });

  return NS_OK;
}

nsresult PrepareDatastoreOp::CheckExistingOperations() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::CheckExistingOperations);
  MOZ_ASSERT(gPrepareDatastoreOps);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    return NS_ERROR_ABORT;
  }

  mNestedState = NestedState::CheckClosingDatastore;

  // See if this PrepareDatastoreOp needs to wait.
  bool foundThis = false;
  bool blocked = false;

  for (uint32_t index = gPrepareDatastoreOps->Length(); index > 0; index--) {
    const auto& existingOp = (*gPrepareDatastoreOps)[index - 1];

    if (existingOp == this) {
      foundThis = true;
      continue;
    }

    if (foundThis && existingOp->Origin() == Origin()) {
      existingOp->AddBlockingOp(*this);
      AddBlockedOnOp(*existingOp);
      blocked = true;
    }
  }

  if (!blocked) {
    QM_TRY(MOZ_TO_RESULT(CheckClosingDatastoreInternal()));
  }

  return NS_OK;
}

nsresult PrepareDatastoreOp::CheckClosingDatastore() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::CheckClosingDatastore);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    return NS_ERROR_ABORT;
  }

  QM_TRY(MOZ_TO_RESULT(CheckClosingDatastoreInternal()));

  return NS_OK;
}

nsresult PrepareDatastoreOp::CheckClosingDatastoreInternal() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::CheckClosingDatastore);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  mNestedState = NestedState::PreparationPending;

  RefPtr<Datastore> datastore;
  if ((datastore = GetDatastore(Origin())) && datastore->IsClosed()) {
    datastore->WaitForConnectionToComplete(this);

    return NS_OK;
  }

  QM_TRY(MOZ_TO_RESULT(BeginDatastorePreparationInternal()));

  return NS_OK;
}

nsresult PrepareDatastoreOp::BeginDatastorePreparation() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::PreparationPending);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    return NS_ERROR_ABORT;
  }

  QM_TRY(MOZ_TO_RESULT(BeginDatastorePreparationInternal()));

  return NS_OK;
}

nsresult PrepareDatastoreOp::BeginDatastorePreparationInternal() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::PreparationPending);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());
  MOZ_ASSERT(OriginIsKnown());

  if ((mDatastore = GetDatastore(Origin()))) {
    MOZ_ASSERT(!mDatastore->IsClosed());

    mExtraDirectoryLockHandle = std::move(mDirectoryLockHandle);

    mDatastore->NoteLivePrepareDatastoreOp(this);

    FinishNesting();

    return NS_OK;
  }

  SendToIOThread();

  return NS_OK;
}

void PrepareDatastoreOp::SendToIOThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::PreparationPending);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  // Skip all disk related stuff and transition to SendingReadyMessage if we
  // are preparing a datastore for private browsing.
  // Note that we do use a directory lock for private browsing even though we
  // don't do any stuff on disk. The thing is that without a directory lock,
  // quota manager wouldn't call AbortOperationsForLocks for our private
  // browsing origin when a clear origin operation is requested.
  // AbortOperationsForLocks requests all databases to close and the datastore
  // is destroyed in the end. Any following LocalStorage API call will trigger
  // preparation of a new (empty) datastore.
  if (mPrivateBrowsingId) {
    FinishNesting();

    return;
  }

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  // Must set this before dispatching otherwise we will race with the IO thread.
  mNestedState = NestedState::DatabaseWorkOpen;

  MOZ_ALWAYS_SUCCEEDS(
      quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL));

  localstorage::NotifyDatabaseWorkStarted();
}

nsresult PrepareDatastoreOp::DatabaseWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mArchivedOriginScope);
  MOZ_ASSERT(mUsage == 0);
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::DatabaseWorkOpen);

  const auto innerFunc = [&](const auto&) -> nsresult {
    // XXX This function is too long, refactor it into helper functions for
    // readability.

    if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
        !MayProceedOnNonOwningThread()) {
      return NS_ERROR_ABORT;
    }

    QuotaManager* quotaManager = QuotaManager::Get();
    MOZ_ASSERT(quotaManager);

    const UsageInfo usageInfo = quotaManager->GetUsageForClient(
        PERSISTENCE_TYPE_DEFAULT, mOriginMetadata,
        mozilla::dom::quota::Client::LS);

    const bool hasUsage = usageInfo.DatabaseUsage().isSome();
    MOZ_ASSERT(usageInfo.FileUsage().isNothing());

    if (!gArchivedOrigins) {
      QM_TRY(MOZ_TO_RESULT(LoadArchivedOrigins()));
      MOZ_ASSERT(gArchivedOrigins);
    }

    bool hasDataForMigration =
        mEnableMigration && mArchivedOriginScope->HasMatches(gArchivedOrigins);

    // If there's nothing to preload (except the case when we want to migrate
    // data during preloading), then we can finish the operation without
    // creating a datastore in GetResponse (GetResponse won't create a datastore
    // if mDatatabaseNotAvailable and mForPreload are both true).
    if (mForPreload && !hasUsage && !hasDataForMigration) {
      return DatabaseNotAvailable();
    }

    // The origin directory doesn't need to be created when we don't have data
    // for migration. It will be created on the connection thread in
    // Connection::EnsureStorageConnection.
    // However, origin quota must be initialized, GetQuotaObject in GetResponse
    // would fail otherwise.
    QM_TRY_INSPECT(
        const auto& directoryEntry,
        ([hasDataForMigration, &quotaManager,
          this]() -> mozilla::Result<nsCOMPtr<nsIFile>, nsresult> {
          if (hasDataForMigration) {
            QM_TRY_RETURN(quotaManager->GetOrCreateTemporaryOriginDirectory(
                mOriginMetadata));
          }

          MOZ_ASSERT(mOriginMetadata.mPersistenceType ==
                     PERSISTENCE_TYPE_DEFAULT);

          QM_TRY_RETURN(quotaManager->GetOriginDirectory(mOriginMetadata));
        }()));

    QM_TRY(MOZ_TO_RESULT(directoryEntry->Append(
        NS_LITERAL_STRING_FROM_CSTRING(LS_DIRECTORY_NAME))));

    QM_TRY_INSPECT(
        const auto& directoryPath,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, directoryEntry, GetPath));

    // The ls directory doesn't need to be created when we don't have data for
    // migration. It will be created on the connection thread in
    // Connection::EnsureStorageConnection.
    QM_TRY(MOZ_TO_RESULT(
        EnsureDirectoryEntry(directoryEntry,
                             /* aCreateIfNotExists */ hasDataForMigration,
                             /* aIsDirectory */ true)));

    QM_TRY(MOZ_TO_RESULT(directoryEntry->Append(kDataFileName)));

    QM_TRY(MOZ_TO_RESULT(directoryEntry->GetPath(mDatabaseFilePath)));

    // The database doesn't need to be created when we don't have data for
    // migration. It will be created on the connection thread in
    // Connection::EnsureStorageConnection.
    bool alreadyExisted;
    QM_TRY(MOZ_TO_RESULT(
        EnsureDirectoryEntry(directoryEntry,
                             /* aCreateIfNotExists */ hasDataForMigration,
                             /* aIsDirectory */ false, &alreadyExisted)));

    if (alreadyExisted) {
      // The database does exist.
      MOZ_ASSERT(hasUsage);

      // XXX Change type of mUsage to UsageInfo or DatabaseUsageType.
      mUsage = usageInfo.DatabaseUsage().valueOr(0);
    } else {
      // The database doesn't exist.
      MOZ_ASSERT(!hasUsage);

      if (!hasDataForMigration) {
        // The database doesn't exist and we don't have data for migration.
        // Finish the operation, but create an empty datastore in GetResponse
        // (GetResponse will create an empty datastore if mDatabaseNotAvailable
        // is true and mForPreload is false).
        return DatabaseNotAvailable();
      }
    }

    // We initialized mDatabaseFilePath and mUsage, GetQuotaObject can now be
    // called.
    const RefPtr<QuotaObject> quotaObject = GetQuotaObject();

    QM_TRY(OkIf(quotaObject), Err(NS_ERROR_FAILURE));

    QM_TRY_INSPECT(const auto& usageFile, GetUsageFile(directoryPath));

    QM_TRY_INSPECT(const auto& usageJournalFile,
                   GetUsageJournalFile(directoryPath));

    QM_TRY_INSPECT(
        const auto& connection,
        (CreateStorageConnectionWithRecovery(
            *directoryEntry, *usageFile, Origin(), [&quotaObject, this] {
              // This is called when the usage file was removed or we notice
              // that the usage file doesn't exist anymore. Adjust the usage
              // accordingly.

              MOZ_ALWAYS_TRUE(
                  quotaObject->MaybeUpdateSize(0, /* aTruncate */ true));

              mUsage = 0;
            })));

    QM_TRY(MOZ_TO_RESULT(VerifyDatabaseInformation(connection)));

    if (hasDataForMigration) {
      MOZ_ASSERT(mUsage == 0);

      {
        QM_TRY_INSPECT(const auto& archiveFile,
                       GetArchiveFile(quotaManager->GetStoragePath()));

        auto autoArchiveDatabaseAttacher =
            AutoDatabaseAttacher(connection, archiveFile, "archive"_ns);

        QM_TRY(MOZ_TO_RESULT(autoArchiveDatabaseAttacher.Attach()));

        QM_TRY_INSPECT(const int64_t& newUsage,
                       GetUsage(*connection, mArchivedOriginScope.get()));

        QM_TRY(
            OkIf(quotaObject->MaybeUpdateSize(newUsage, /* aTruncate */ true)),
            NS_ERROR_FILE_NO_DEVICE_SPACE);

        auto autoUpdateSize = MakeScopeExit([&quotaObject] {
          MOZ_ALWAYS_TRUE(
              quotaObject->MaybeUpdateSize(0, /* aTruncate */ true));
        });

        mozStorageTransaction transaction(
            connection, false, mozIStorageConnection::TRANSACTION_IMMEDIATE);

        QM_TRY(MOZ_TO_RESULT(transaction.Start()));

        {
          nsCOMPtr<mozIStorageFunction> function = new CompressFunction();

          QM_TRY(MOZ_TO_RESULT(
              connection->CreateFunction("compress"_ns, 1, function)));

          function = new CompressionTypeFunction();

          QM_TRY(MOZ_TO_RESULT(
              connection->CreateFunction("compressionType"_ns, 1, function)));

          QM_TRY_INSPECT(
              const auto& stmt,
              MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                  nsCOMPtr<mozIStorageStatement>, connection, CreateStatement,
                  "INSERT INTO data (key, utf16_length, conversion_type, "
                  "compression_type, value) "
                  "SELECT key, utf16Length(value), :conversionType, "
                  "compressionType(value), compress(value)"
                  "FROM webappsstore2 "
                  "WHERE originKey = :originKey "
                  "AND originAttributes = :originAttributes;"_ns));

          QM_TRY(MOZ_TO_RESULT(stmt->BindInt32ByName(
              "conversionType"_ns,
              static_cast<int32_t>(LSValue::ConversionType::UTF16_UTF8))));

          QM_TRY(MOZ_TO_RESULT(mArchivedOriginScope->BindToStatement(stmt)));

          QM_TRY(MOZ_TO_RESULT(stmt->Execute()));

          QM_TRY(MOZ_TO_RESULT(connection->RemoveFunction("compress"_ns)));

          QM_TRY(
              MOZ_TO_RESULT(connection->RemoveFunction("compressionType"_ns)));
        }

        {
          QM_TRY_INSPECT(
              const auto& stmt,
              MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                  nsCOMPtr<mozIStorageStatement>, connection, CreateStatement,
                  "UPDATE database SET usage = :usage;"_ns));

          QM_TRY(MOZ_TO_RESULT(stmt->BindInt64ByName("usage"_ns, newUsage)));

          QM_TRY(MOZ_TO_RESULT(stmt->Execute()));
        }

        {
          QM_TRY_INSPECT(
              const auto& stmt,
              MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                  nsCOMPtr<mozIStorageStatement>, connection, CreateStatement,
                  "DELETE FROM webappsstore2 "
                  "WHERE originKey = :originKey "
                  "AND originAttributes = :originAttributes;"_ns));

          QM_TRY(MOZ_TO_RESULT(mArchivedOriginScope->BindToStatement(stmt)));
          QM_TRY(MOZ_TO_RESULT(stmt->Execute()));
        }

        QM_TRY(MOZ_TO_RESULT(
            UpdateUsageFile(usageFile, usageJournalFile, newUsage)));
        QM_TRY(MOZ_TO_RESULT(transaction.Commit()));

        autoUpdateSize.release();

        QM_TRY(MOZ_TO_RESULT(usageJournalFile->Remove(false)));

        mUsage = newUsage;

        QM_TRY(MOZ_TO_RESULT(autoArchiveDatabaseAttacher.Detach()));
      }

      MOZ_ASSERT(gArchivedOrigins);
      MOZ_ASSERT(mArchivedOriginScope->HasMatches(gArchivedOrigins));
      mArchivedOriginScope->RemoveMatches(gArchivedOrigins);
    }

    nsCOMPtr<mozIStorageConnection> shadowConnection;
    if (!gInitializedShadowStorage) {
      QM_TRY_UNWRAP(shadowConnection,
                    CreateShadowStorageConnection(quotaManager->GetBasePath()));

      gInitializedShadowStorage = true;
    }

    // Must close connections before dispatching otherwise we might race with
    // the connection thread which needs to open the same databases.
    MOZ_ALWAYS_SUCCEEDS(connection->Close());

    if (shadowConnection) {
      MOZ_ALWAYS_SUCCEEDS(shadowConnection->Close());
    }

    SleepIfEnabled(
        StaticPrefs::dom_storage_databaseInitialization_pauseOnIOThreadMs());

    // Must set this before dispatching otherwise we will race with the owning
    // thread.
    mNestedState = NestedState::BeginLoadData;

    QM_TRY(
        MOZ_TO_RESULT(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL)));

    return NS_OK;
  };

  return ExecuteOriginInitialization(
      mOriginMetadata.mOrigin, LSOriginInitialization::Datastore,
      "dom::localstorage::FirstOriginInitializationAttempt::Datastore"_ns,
      innerFunc);
}

nsresult PrepareDatastoreOp::DatabaseNotAvailable() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::DatabaseWorkOpen);

  mDatabaseNotAvailable = true;

  nsresult rv = FinishNestingOnNonOwningThread();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

nsresult PrepareDatastoreOp::EnsureDirectoryEntry(nsIFile* aEntry,
                                                  bool aCreateIfNotExists,
                                                  bool aIsDirectory,
                                                  bool* aAlreadyExisted) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aEntry);

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aEntry, Exists));

  if (!exists) {
    if (!aCreateIfNotExists) {
      if (aAlreadyExisted) {
        *aAlreadyExisted = false;
      }
      return NS_OK;
    }

    if (aIsDirectory) {
      QM_TRY(MOZ_TO_RESULT(aEntry->Create(nsIFile::DIRECTORY_TYPE, 0755)));
    }
  }
#ifdef DEBUG
  else {
    bool isDirectory;
    MOZ_ASSERT(NS_SUCCEEDED(aEntry->IsDirectory(&isDirectory)));
    MOZ_ASSERT(isDirectory == aIsDirectory);
  }
#endif

  if (aAlreadyExisted) {
    *aAlreadyExisted = exists;
  }
  return NS_OK;
}

nsresult PrepareDatastoreOp::VerifyDatabaseInformation(
    mozIStorageConnection* aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);

  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement<
                     SingleStepResult::ReturnNullIfNoResult>(
                     *aConnection, "SELECT origin FROM database"_ns));

  QM_TRY(OkIf(stmt), NS_ERROR_FILE_CORRUPTED);

  QM_TRY_INSPECT(const auto& origin, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                         nsCString, stmt, GetUTF8String, 0));

  QM_TRY(OkIf(QuotaManager::AreOriginsEqualOnDisk(Origin(), origin)),
         NS_ERROR_FILE_CORRUPTED);

  return NS_OK;
}

already_AddRefed<QuotaObject> PrepareDatastoreOp::GetQuotaObject() {
  MOZ_ASSERT(IsOnOwningThread() || IsOnIOThread());
  MOZ_ASSERT(!mOriginMetadata.mGroup.IsEmpty());
  MOZ_ASSERT(OriginIsKnown());
  MOZ_ASSERT(!mDatabaseFilePath.IsEmpty());

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  RefPtr<QuotaObject> quotaObject = quotaManager->GetQuotaObject(
      PERSISTENCE_TYPE_DEFAULT, mOriginMetadata,
      mozilla::dom::quota::Client::LS, mDatabaseFilePath, mUsage);

  if (!quotaObject) {
    LS_WARNING("Failed to get quota object for group (%s) and origin (%s)!",
               mOriginMetadata.mGroup.get(), Origin().get());
  }

  return quotaObject.forget();
}

nsresult PrepareDatastoreOp::BeginLoadData() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::BeginLoadData);
  MOZ_ASSERT(!mConnection);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    return NS_ERROR_ABORT;
  }

  if (!gConnectionThread) {
    gConnectionThread = new ConnectionThread();
  }

  mConnection = gConnectionThread->CreateConnection(
      mOriginMetadata, std::move(mArchivedOriginScope),
      /* aDatabaseWasNotAvailable */ false);
  MOZ_ASSERT(mConnection);

  // Must set this before dispatching otherwise we will race with the
  // connection thread.
  mNestedState = NestedState::DatabaseWorkLoadData;

  // Can't assign to mLoadDataOp directly since that's a weak reference and
  // LoadDataOp is reference counted.
  RefPtr<LoadDataOp> loadDataOp = new LoadDataOp(this);

  // This add refs loadDataOp.
  mConnection->Dispatch(loadDataOp);

  // This is cleared in LoadDataOp::Cleanup() before the load data op is
  // destroyed.
  mLoadDataOp = loadDataOp;

  return NS_OK;
}

void PrepareDatastoreOp::FinishNesting() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);

  // The caller holds a strong reference to us, no need for a self reference
  // before calling Run().

  mState = State::SendingReadyMessage;
  mNestedState = NestedState::AfterNesting;

  MOZ_ALWAYS_SUCCEEDS(Run());
}

nsresult PrepareDatastoreOp::FinishNestingOnNonOwningThread() {
  MOZ_ASSERT(!IsOnOwningThread());
  MOZ_ASSERT(mState == State::Nesting);

  // Must set mState before dispatching otherwise we will race with the owning
  // thread.
  mState = State::SendingReadyMessage;
  mNestedState = NestedState::AfterNesting;

  QM_TRY(
      MOZ_TO_RESULT(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL)));

  return NS_OK;
}

nsresult PrepareDatastoreOp::NestedRun() {
  nsresult rv;

  switch (mNestedState) {
    case NestedState::OpenDirectory:
      rv = OpenDirectory();
      break;

    case NestedState::CheckExistingOperations:
      rv = CheckExistingOperations();
      break;

    case NestedState::CheckClosingDatastore:
      rv = CheckClosingDatastore();
      break;

    case NestedState::PreparationPending:
      rv = BeginDatastorePreparation();
      break;

    case NestedState::DatabaseWorkOpen:
      rv = DatabaseWork();
      break;

    case NestedState::BeginLoadData:
      rv = BeginLoadData();
      break;

    default:
      MOZ_CRASH("Bad state!");
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    mNestedState = NestedState::AfterNesting;

    return rv;
  }

  return NS_OK;
}

void PrepareDatastoreOp::GetResponse(LSRequestResponse& aResponse) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);
  MOZ_ASSERT(NS_SUCCEEDED(ResultCode()));
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  // A datastore is not created when we are just trying to preload data and
  // there's no database file.
  if (mDatabaseNotAvailable && mForPreload) {
    LSRequestPreloadDatastoreResponse preloadDatastoreResponse;

    aResponse = preloadDatastoreResponse;

    return;
  }

  if (!mDatastore) {
    MOZ_ASSERT(mUsage == mDEBUGUsage);

    RefPtr<QuotaObject> quotaObject;

    if (mPrivateBrowsingId == 0) {
      if (!mConnection) {
        // This can happen when there's no database file.
        MOZ_ASSERT(mDatabaseNotAvailable);

        // Even though there's no database file, we need to create a connection
        // and pass it to datastore.
        if (!gConnectionThread) {
          gConnectionThread = new ConnectionThread();
        }

        mConnection = gConnectionThread->CreateConnection(
            mOriginMetadata, std::move(mArchivedOriginScope),
            /* aDatabaseWasNotAvailable */ true);
        MOZ_ASSERT(mConnection);
      }

      quotaObject = GetQuotaObject();
      if (!quotaObject) {
        aResponse = NS_ERROR_FAILURE;
        return;
      }
    }

    MOZ_ASSERT(mDirectoryLockHandle);
    MOZ_ASSERT_IF(mDirectoryLockHandle->Invalidated(), mInvalidated);

    mDatastore = new Datastore(
        mOriginMetadata, mPrivateBrowsingId, mUsage, mSizeOfKeys, mSizeOfItems,
        std::move(mDirectoryLockHandle), std::move(mConnection),
        std::move(quotaObject), mValues, std::move(mOrderedItems));

    mDatastore->NoteLivePrepareDatastoreOp(this);

    if (!gDatastores) {
      gDatastores = new DatastoreHashtable();
    }

    MOZ_DIAGNOSTIC_ASSERT(!gDatastores->Contains(Origin()));
    gDatastores->InsertOrUpdate(Origin(),
                                WrapMovingNotNullUnchecked(mDatastore));
  }

  if (mPrivateBrowsingId && !mInvalidated) {
    if (!gPrivateDatastores) {
      gPrivateDatastores = MakeUnique<PrivateDatastoreHashtable>();
    }

    gPrivateDatastores->LookupOrInsertWith(Origin(), [&] {
      auto privateDatastore =
          MakeUnique<PrivateDatastore>(WrapMovingNotNull(mDatastore));

      mPrivateDatastoreRegistered.Flip();

      return privateDatastore;
    });
  }

  mDatastoreId = ++gLastDatastoreId;

  if (!gPreparedDatastores) {
    gPreparedDatastores = new PreparedDatastoreHashtable();
  }
  const auto& preparedDatastore = gPreparedDatastores->InsertOrUpdate(
      mDatastoreId, MakeUnique<PreparedDatastore>(
                        mDatastore, mContentParentId, Origin(), mDatastoreId,
                        /* aForPreload */ mForPreload));

  if (mInvalidated) {
    preparedDatastore->Invalidate();
  }

  mPreparedDatastoreRegistered.Flip();

  if (mForPreload) {
    LSRequestPreloadDatastoreResponse preloadDatastoreResponse;
    preloadDatastoreResponse.invalidated() = mInvalidated;

    aResponse = preloadDatastoreResponse;
  } else {
    const LSRequestCommonParams& commonParams =
        mParams.get_LSRequestPrepareDatastoreParams().commonParams();

    const PrincipalInfo& storagePrincipalInfo =
        commonParams.storagePrincipalInfo();

    Endpoint<PBackgroundLSDatabaseParent> parentEndpoint;
    Endpoint<PBackgroundLSDatabaseChild> childEndpoint;
    MOZ_ALWAYS_SUCCEEDS(PBackgroundLSDatabase::CreateEndpoints(&parentEndpoint,
                                                               &childEndpoint));

    if (!RecvCreateBackgroundLSDatabaseParent(storagePrincipalInfo,
                                              mPrivateBrowsingId, mDatastoreId,
                                              std::move(parentEndpoint))) {
      aResponse = NS_ERROR_FAILURE;
    } else {
      LSRequestPrepareDatastoreResponse prepareDatastoreResponse;
      prepareDatastoreResponse.databaseChildEndpoint() =
          std::move(childEndpoint);
      prepareDatastoreResponse.invalidated() = mInvalidated;

      aResponse = std::move(prepareDatastoreResponse);
    }
  }
}

void PrepareDatastoreOp::Cleanup() {
  AssertIsOnOwningThread();

  if (mDatastore) {
    MOZ_ASSERT(!mDirectoryLockHandle);
    MOZ_ASSERT(!mConnection);

    if (NS_FAILED(ResultCode())) {
      if (mPrivateDatastoreRegistered) {
        MOZ_ASSERT(gPrivateDatastores);
        DebugOnly<bool> removed = gPrivateDatastores->Remove(Origin());
        MOZ_ASSERT(removed);

        if (!gPrivateDatastores->Count()) {
          gPrivateDatastores = nullptr;
        }
      }

      if (mPreparedDatastoreRegistered) {
        // Just in case we failed to send datastoreId to the child, we need to
        // destroy prepared datastore, otherwise it won't be destroyed until
        // the timer fires (after 20 seconds).
        MOZ_ASSERT(gPreparedDatastores);
        MOZ_ASSERT(mDatastoreId > 0);
        DebugOnly<bool> removed = gPreparedDatastores->Remove(mDatastoreId);
        MOZ_ASSERT(removed);

        if (!gPreparedDatastores->Count()) {
          gPreparedDatastores = nullptr;
        }
      }
    }

    // Make sure to release the datastore on this thread.

    mDatastore->NoteFinishedPrepareDatastoreOp(this);

    mDatastore = nullptr;

    {
      auto extraDirectoryLockHandle = std::move(mExtraDirectoryLockHandle);
    }

    CleanupMetadata();
  } else if (mConnection) {
    // If we have a connection then the operation must have failed and there
    // must be a directory lock too.
    MOZ_ASSERT(NS_FAILED(ResultCode()));
    MOZ_ASSERT(mDirectoryLockHandle);
    MOZ_ASSERT(!mExtraDirectoryLockHandle);

    // We must close the connection on the connection thread before releasing
    // it on this thread. The directory lock can't be released either.
    nsCOMPtr<nsIRunnable> callback =
        NewRunnableMethod("dom::OpenDatabaseOp::ConnectionClosedCallback", this,
                          &PrepareDatastoreOp::ConnectionClosedCallback);

    mConnection->Close(callback);
  } else {
    // If we don't have a connection, but we do have a directory lock then the
    // operation must have failed or we were preloading a datastore and there
    // was no physical database on disk.
    MOZ_ASSERT_IF(mDirectoryLockHandle,
                  NS_FAILED(ResultCode()) || mDatabaseNotAvailable);
    MOZ_ASSERT(!mExtraDirectoryLockHandle);

    // There's no connection, so it's safe to release the directory lock and
    // unregister itself from the array.

    {
      auto destroyingDdirectoryLockHandle = std::move(mDirectoryLockHandle);
    }

    CleanupMetadata();
  }
}

void PrepareDatastoreOp::ConnectionClosedCallback() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_FAILED(ResultCode()));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(mConnection);

  mConnection = nullptr;

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  CleanupMetadata();
}

void PrepareDatastoreOp::CleanupMetadata() {
  AssertIsOnOwningThread();

  for (const NotNull<RefPtr<PrepareDatastoreOp>>& blockingOp : mBlocking) {
    blockingOp->MaybeUnblock(*this);
  }
  mBlocking.Clear();

  MOZ_ASSERT(gPrepareDatastoreOps);
  gPrepareDatastoreOps->RemoveElement(this);

  QuotaManager::MaybeRecordQuotaClientShutdownStep(
      quota::Client::LS, "PrepareDatastoreOp completed"_ns);

  if (gPrepareDatastoreOps->IsEmpty()) {
    gPrepareDatastoreOps = nullptr;
  }

  if (NS_SUCCEEDED(ResultCode())) {
    glean::localstorage_request::prepare_datastore_processing_time
        .StopAndAccumulate(std::move(mProcessingTimerId));
  } else {
    glean::localstorage_request::prepare_datastore_processing_time.Cancel(
        std::move(mProcessingTimerId));
  }
}

void PrepareDatastoreOp::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  LSRequestBase::ActorDestroy(aWhy);

  if (mLoadDataOp) {
    mLoadDataOp->NoteComplete();
  }
}

void PrepareDatastoreOp::DirectoryLockAcquired(
    ClientDirectoryLockHandle aLockHandle) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  mPendingDirectoryLock = nullptr;

  mDirectoryLockHandle = std::move(aLockHandle);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed() || mDirectoryLockHandle->Invalidated()) {
    MaybeSetFailureCode(NS_ERROR_ABORT);

    FinishNesting();

    return;
  }

  mNestedState = NestedState::CheckExistingOperations;

  // XXX We could call CheckExistingOperations directly here or even fold it
  // here instead of dispatching to the same event target
  MOZ_ALWAYS_SUCCEEDS(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));
}

void PrepareDatastoreOp::DirectoryLockFailed() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Nesting);
  MOZ_ASSERT(mNestedState == NestedState::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  mPendingDirectoryLock = nullptr;

  MaybeSetFailureCode(NS_ERROR_FAILURE);

  FinishNesting();
}

nsresult PrepareDatastoreOp::LoadDataOp::DoDatastoreWork() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(mConnection);
  MOZ_ASSERT(mPrepareDatastoreOp);
  MOZ_ASSERT(mPrepareDatastoreOp->mState == State::Nesting);
  MOZ_ASSERT(mPrepareDatastoreOp->mNestedState ==
             NestedState::DatabaseWorkLoadData);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !MayProceedOnNonOwningThread()) {
    return NS_ERROR_ABORT;
  }

  QM_TRY_INSPECT(
      const auto& stmt,
      mConnection->BorrowCachedStatement(
          "SELECT key, utf16_length, conversion_type, compression_type, value "
          "FROM data;"_ns));

  QM_TRY(quota::CollectWhileHasResult(
      *stmt, [this](auto& stmt) -> mozilla::Result<Ok, nsresult> {
        QM_TRY_UNWRAP(auto key, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                    nsString, stmt, GetString, 0));

        LSValue value;
        QM_TRY(MOZ_TO_RESULT(value.InitFromStatement(&stmt, 1)));

        mPrepareDatastoreOp->mValues.InsertOrUpdate(key, value);
        mPrepareDatastoreOp->mSizeOfKeys += key.Length();
        mPrepareDatastoreOp->mSizeOfItems += key.Length() + value.Length();
#ifdef DEBUG
        mPrepareDatastoreOp->mDEBUGUsage += key.Length() + value.UTF16Length();
#endif

        auto item = mPrepareDatastoreOp->mOrderedItems.AppendElement();
        item->key() = std::move(key);
        item->value() = std::move(value);

        return Ok{};
      }));

  return NS_OK;
}

void PrepareDatastoreOp::LoadDataOp::OnSuccess() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mPrepareDatastoreOp);
  MOZ_ASSERT(mPrepareDatastoreOp->mState == State::Nesting);
  MOZ_ASSERT(mPrepareDatastoreOp->mNestedState ==
             NestedState::DatabaseWorkLoadData);
  MOZ_ASSERT(mPrepareDatastoreOp->mLoadDataOp == this);

  mPrepareDatastoreOp->FinishNesting();
}

void PrepareDatastoreOp::LoadDataOp::OnFailure(nsresult aResultCode) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mPrepareDatastoreOp);
  MOZ_ASSERT(mPrepareDatastoreOp->mState == State::Nesting);
  MOZ_ASSERT(mPrepareDatastoreOp->mNestedState ==
             NestedState::DatabaseWorkLoadData);
  MOZ_ASSERT(mPrepareDatastoreOp->mLoadDataOp == this);

  mPrepareDatastoreOp->SetFailureCode(aResultCode);

  mPrepareDatastoreOp->FinishNesting();
}

void PrepareDatastoreOp::LoadDataOp::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mPrepareDatastoreOp);
  MOZ_ASSERT(mPrepareDatastoreOp->mLoadDataOp == this);

  mPrepareDatastoreOp->mLoadDataOp = nullptr;
  mPrepareDatastoreOp = nullptr;

  ConnectionDatastoreOperationBase::Cleanup();
}

NS_IMPL_ISUPPORTS(PrepareDatastoreOp::CompressFunction, mozIStorageFunction)

NS_IMETHODIMP
PrepareDatastoreOp::CompressFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aFunctionArguments);
  MOZ_ASSERT(aResult);

#ifdef DEBUG
  {
    uint32_t argCount;
    MOZ_ALWAYS_SUCCEEDS(aFunctionArguments->GetNumEntries(&argCount));
    MOZ_ASSERT(argCount == 1);

    int32_t type;
    MOZ_ALWAYS_SUCCEEDS(aFunctionArguments->GetTypeOfIndex(0, &type));
    MOZ_ASSERT(type == mozIStorageValueArray::VALUE_TYPE_TEXT);
  }
#endif

  QM_TRY_INSPECT(const auto& value,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCString, aFunctionArguments, GetUTF8String, 0));

  nsCString compressed;
  QM_TRY(OkIf(SnappyCompress(value, compressed)), NS_ERROR_OUT_OF_MEMORY);

  const nsCString& buffer = compressed.IsVoid() ? value : compressed;

  // mozStorage transforms empty blobs into null values, but our database
  // schema doesn't allow null values. We can workaround this by storing
  // empty buffers as UTF8 text (SQLite supports the type affinity, so the type
  // of the column is not fixed).
  nsCOMPtr<nsIVariant> result;
  if (0u == buffer.Length()) {  // Otherwise empty string becomes null
    result = new storage::UTF8TextVariant(buffer);
  } else {
    result = new storage::BlobVariant(std::make_pair(
        static_cast<const void*>(buffer.get()), int(buffer.Length())));
  }

  result.forget(aResult);
  return NS_OK;
}

NS_IMPL_ISUPPORTS(PrepareDatastoreOp::CompressionTypeFunction,
                  mozIStorageFunction)

NS_IMETHODIMP
PrepareDatastoreOp::CompressionTypeFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aFunctionArguments);
  MOZ_ASSERT(aResult);

#ifdef DEBUG
  {
    uint32_t argCount;
    MOZ_ALWAYS_SUCCEEDS(aFunctionArguments->GetNumEntries(&argCount));
    MOZ_ASSERT(argCount == 1);

    int32_t type;
    MOZ_ALWAYS_SUCCEEDS(aFunctionArguments->GetTypeOfIndex(0, &type));
    MOZ_ASSERT(type == mozIStorageValueArray::VALUE_TYPE_TEXT);
  }
#endif

  QM_TRY_INSPECT(const auto& value,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCString, aFunctionArguments, GetUTF8String, 0));

  nsCString compressed;
  QM_TRY(OkIf(SnappyCompress(value, compressed)), NS_ERROR_OUT_OF_MEMORY);

  const int32_t compression = static_cast<int32_t>(
      compressed.IsVoid() ? LSValue::CompressionType::UNCOMPRESSED
                          : LSValue::CompressionType::SNAPPY);

  nsCOMPtr<nsIVariant> result = new storage::IntegerVariant(compression);

  result.forget(aResult);
  return NS_OK;
}

/*******************************************************************************
 * PrepareObserverOp
 ******************************************************************************/

PrepareObserverOp::PrepareObserverOp(
    const LSRequestParams& aParams,
    const Maybe<ContentParentId>& aContentParentId)
    : LSRequestBase(aParams, aContentParentId) {
  MOZ_ASSERT(aParams.type() ==
             LSRequestParams::TLSRequestPrepareObserverParams);
}

nsresult PrepareObserverOp::Start() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::StartingRequest);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  const LSRequestPrepareObserverParams params =
      mParams.get_LSRequestPrepareObserverParams();

  const PrincipalInfo& storagePrincipalInfo = params.storagePrincipalInfo();

  if (storagePrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo) {
    mOrigin = quota::GetOriginForChrome();
  } else {
    MOZ_ASSERT(storagePrincipalInfo.type() ==
               PrincipalInfo::TContentPrincipalInfo);

    mOrigin = quota::GetOriginFromValidatedPrincipalInfo(storagePrincipalInfo);
  }

  mState = State::SendingReadyMessage;
  MOZ_ALWAYS_SUCCEEDS(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));

  return NS_OK;
}

void PrepareObserverOp::GetResponse(LSRequestResponse& aResponse) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);
  MOZ_ASSERT(NS_SUCCEEDED(ResultCode()));
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  uint64_t observerId = ++gLastObserverId;

  RefPtr<Observer> observer = new Observer(mContentParentId, mOrigin);

  if (!gPreparedObsevers) {
    gPreparedObsevers = new PreparedObserverHashtable();
  }
  gPreparedObsevers->InsertOrUpdate(observerId, std::move(observer));

  LSRequestPrepareObserverResponse prepareObserverResponse;
  prepareObserverResponse.observerId() = observerId;

  aResponse = prepareObserverResponse;
}

/*******************************************************************************
+ * LSSimpleRequestBase
+
******************************************************************************/

LSSimpleRequestBase::LSSimpleRequestBase(
    const LSSimpleRequestParams& aParams,
    const Maybe<ContentParentId>& aContentParentId)
    : mParams(aParams),
      mContentParentId(aContentParentId),
      mState(State::Initial) {}

LSSimpleRequestBase::~LSSimpleRequestBase() {
  MOZ_ASSERT_IF(MayProceedOnNonOwningThread(),
                mState == State::Initial || mState == State::Completed);
}

void LSSimpleRequestBase::Dispatch() {
  AssertIsOnOwningThread();

  mState = State::StartingRequest;

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(this));
}

bool LSSimpleRequestBase::VerifyRequestParams() {
  AssertIsOnBackgroundThread();

  MOZ_ASSERT(mParams.type() != LSSimpleRequestParams::T__None);

  switch (mParams.type()) {
    case LSSimpleRequestParams::TLSSimpleRequestPreloadedParams: {
      const LSSimpleRequestPreloadedParams& params =
          mParams.get_LSSimpleRequestPreloadedParams();

      if (NS_WARN_IF(!VerifyPrincipalInfo(
              params.principalInfo(), params.storagePrincipalInfo(), false))) {
        return false;
      }

      break;
    }

    case LSSimpleRequestParams::TLSSimpleRequestGetStateParams: {
      const LSSimpleRequestGetStateParams& params =
          mParams.get_LSSimpleRequestGetStateParams();

      if (NS_WARN_IF(!VerifyPrincipalInfo(
              params.principalInfo(), params.storagePrincipalInfo(), false))) {
        return false;
      }

      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  return true;
}

nsresult LSSimpleRequestBase::StartRequest() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::StartingRequest);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    return NS_ERROR_ABORT;
  }

#ifdef DEBUG
  // Always verify parameters in DEBUG builds!
  bool trustParams = false;
#else
  bool trustParams = !BackgroundParent::IsOtherProcessActor(Manager());
#endif

  if (!trustParams && NS_WARN_IF(!VerifyRequestParams())) {
    return NS_ERROR_FAILURE;
  }

  QM_TRY(MOZ_TO_RESULT(Start()));

  return NS_OK;
}

void LSSimpleRequestBase::SendResults() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !MayProceed()) {
    MaybeSetFailureCode(NS_ERROR_ABORT);
  }

  if (MayProceed()) {
    LSSimpleRequestResponse response;

    if (NS_SUCCEEDED(ResultCode())) {
      GetResponse(response);
    } else {
      response = ResultCode();
    }

    Unused << PBackgroundLSSimpleRequestParent::Send__delete__(this, response);
  }

  mState = State::Completed;
}

NS_IMETHODIMP
LSSimpleRequestBase::Run() {
  nsresult rv;

  switch (mState) {
    case State::StartingRequest:
      rv = StartRequest();
      break;

    case State::SendingResults:
      SendResults();
      return NS_OK;

    default:
      MOZ_CRASH("Bad state!");
  }

  if (NS_WARN_IF(NS_FAILED(rv)) && mState != State::SendingResults) {
    MaybeSetFailureCode(rv);

    // Must set mState before dispatching otherwise we will race with the owning
    // thread.
    mState = State::SendingResults;

    if (IsOnOwningThread()) {
      SendResults();
    } else {
      MOZ_ALWAYS_SUCCEEDS(
          OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));
    }
  }

  return NS_OK;
}

void LSSimpleRequestBase::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  NoteComplete();
}

/*******************************************************************************
 * PreloadedOp
 ******************************************************************************/

PreloadedOp::PreloadedOp(const LSSimpleRequestParams& aParams,
                         const Maybe<ContentParentId>& aContentParentId)
    : LSSimpleRequestBase(aParams, aContentParentId) {
  MOZ_ASSERT(aParams.type() ==
             LSSimpleRequestParams::TLSSimpleRequestPreloadedParams);
}

nsresult PreloadedOp::Start() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::StartingRequest);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  const LSSimpleRequestPreloadedParams& params =
      mParams.get_LSSimpleRequestPreloadedParams();

  const PrincipalInfo& storagePrincipalInfo = params.storagePrincipalInfo();

  MOZ_ASSERT(
      storagePrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo ||
      storagePrincipalInfo.type() == PrincipalInfo::TContentPrincipalInfo);
  mOrigin =
      storagePrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo
          ? nsCString{quota::GetOriginForChrome()}
          : quota::GetOriginFromValidatedPrincipalInfo(storagePrincipalInfo);

  mState = State::SendingResults;
  MOZ_ALWAYS_SUCCEEDS(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));

  return NS_OK;
}

void PreloadedOp::GetResponse(LSSimpleRequestResponse& aResponse) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);
  MOZ_ASSERT(NS_SUCCEEDED(ResultCode()));
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  bool preloaded;
  RefPtr<Datastore> datastore;
  if ((datastore = GetDatastore(mOrigin)) && !datastore->IsClosed()) {
    preloaded = true;
  } else {
    preloaded = false;
  }

  LSSimpleRequestPreloadedResponse preloadedResponse;
  preloadedResponse.preloaded() = preloaded;

  aResponse = preloadedResponse;
}

/*******************************************************************************
 * GetStateOp
 ******************************************************************************/

GetStateOp::GetStateOp(const LSSimpleRequestParams& aParams,
                       const Maybe<ContentParentId>& aContentParentId)
    : LSSimpleRequestBase(aParams, aContentParentId) {
  MOZ_ASSERT(aParams.type() ==
             LSSimpleRequestParams::TLSSimpleRequestGetStateParams);
}

nsresult GetStateOp::Start() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::StartingRequest);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  const LSSimpleRequestGetStateParams& params =
      mParams.get_LSSimpleRequestGetStateParams();

  const PrincipalInfo& storagePrincipalInfo = params.storagePrincipalInfo();

  MOZ_ASSERT(
      storagePrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo ||
      storagePrincipalInfo.type() == PrincipalInfo::TContentPrincipalInfo);
  mOrigin =
      storagePrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo
          ? nsCString{quota::GetOriginForChrome()}
          : quota::GetOriginFromValidatedPrincipalInfo(storagePrincipalInfo);

  mState = State::SendingResults;
  MOZ_ALWAYS_SUCCEEDS(OwningEventTarget()->Dispatch(this, NS_DISPATCH_NORMAL));

  return NS_OK;
}

void GetStateOp::GetResponse(LSSimpleRequestResponse& aResponse) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);
  MOZ_ASSERT(NS_SUCCEEDED(ResultCode()));
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(MayProceed());

  LSSimpleRequestGetStateResponse getStateResponse;

  if (RefPtr<Datastore> datastore = GetDatastore(mOrigin)) {
    if (!datastore->IsClosed()) {
      getStateResponse.itemInfos() = datastore->GetOrderedItems().Clone();
    }
  }

  aResponse = getStateResponse;
}

/*******************************************************************************
 * ArchivedOriginScope
 ******************************************************************************/

// static
UniquePtr<ArchivedOriginScope> ArchivedOriginScope::CreateFromOrigin(
    const nsACString& aOriginAttrSuffix, const nsACString& aOriginKey) {
  return WrapUnique(
      new ArchivedOriginScope(Origin(aOriginAttrSuffix, aOriginKey)));
}

// static
UniquePtr<ArchivedOriginScope> ArchivedOriginScope::CreateFromPrefix(
    const nsACString& aOriginKey) {
  return WrapUnique(new ArchivedOriginScope(Prefix(aOriginKey)));
}

// static
UniquePtr<ArchivedOriginScope> ArchivedOriginScope::CreateFromPattern(
    const OriginAttributesPattern& aPattern) {
  return WrapUnique(new ArchivedOriginScope(Pattern(aPattern)));
}

// static
UniquePtr<ArchivedOriginScope> ArchivedOriginScope::CreateFromNull() {
  return WrapUnique(new ArchivedOriginScope(Null()));
}

nsLiteralCString ArchivedOriginScope::GetBindingClause() const {
  return mData.match(
      [](const Origin&) {
        return " WHERE originKey = :originKey "
               "AND originAttributes = :originAttributes"_ns;
      },
      [](const Pattern&) {
        return " WHERE originAttributes MATCH :originAttributesPattern"_ns;
      },
      [](const Prefix&) { return " WHERE originKey = :originKey"_ns; },
      [](const Null&) { return ""_ns; });
}

nsresult ArchivedOriginScope::BindToStatement(
    mozIStorageStatement* aStmt) const {
  MOZ_ASSERT(IsOnIOThread() || IsOnGlobalConnectionThread());
  MOZ_ASSERT(aStmt);

  struct Matcher {
    mozIStorageStatement* mStmt;

    explicit Matcher(mozIStorageStatement* aStmt) : mStmt(aStmt) {}

    nsresult operator()(const Origin& aOrigin) {
      QM_TRY(MOZ_TO_RESULT(mStmt->BindUTF8StringByName(
          "originKey"_ns, aOrigin.OriginNoSuffix())));

      QM_TRY(MOZ_TO_RESULT(mStmt->BindUTF8StringByName(
          "originAttributes"_ns, aOrigin.OriginSuffix())));

      return NS_OK;
    }

    nsresult operator()(const Prefix& aPrefix) {
      QM_TRY(MOZ_TO_RESULT(mStmt->BindUTF8StringByName(
          "originKey"_ns, aPrefix.OriginNoSuffix())));

      return NS_OK;
    }

    nsresult operator()(const Pattern& aPattern) {
      QM_TRY(MOZ_TO_RESULT(mStmt->BindUTF8StringByName(
          "originAttributesPattern"_ns, "pattern1"_ns)));

      return NS_OK;
    }

    nsresult operator()(const Null& aNull) { return NS_OK; }
  };

  QM_TRY(MOZ_TO_RESULT(mData.match(Matcher(aStmt))));

  return NS_OK;
}

bool ArchivedOriginScope::HasMatches(
    ArchivedOriginHashtable* aHashtable) const {
  AssertIsOnIOThread();
  MOZ_ASSERT(aHashtable);

  return mData.match(
      [aHashtable](const Origin& aOrigin) {
        const nsCString hashKey = GetArchivedOriginHashKey(
            aOrigin.OriginSuffix(), aOrigin.OriginNoSuffix());

        return aHashtable->Contains(hashKey);
      },
      [aHashtable](const Pattern& aPattern) {
        return std::any_of(
            aHashtable->Values().cbegin(), aHashtable->Values().cend(),
            [&aPattern](const auto& entry) {
              return aPattern.GetPattern().Matches(entry->mOriginAttributes);
            });
      },
      [aHashtable](const Prefix& aPrefix) {
        return std::any_of(
            aHashtable->Values().cbegin(), aHashtable->Values().cend(),
            [&aPrefix](const auto& entry) {
              return entry->mOriginNoSuffix == aPrefix.OriginNoSuffix();
            });
      },
      [aHashtable](const Null& aNull) { return !aHashtable->IsEmpty(); });
}

void ArchivedOriginScope::RemoveMatches(
    ArchivedOriginHashtable* aHashtable) const {
  AssertIsOnIOThread();
  MOZ_ASSERT(aHashtable);

  struct Matcher {
    ArchivedOriginHashtable* mHashtable;

    explicit Matcher(ArchivedOriginHashtable* aHashtable)
        : mHashtable(aHashtable) {}

    void operator()(const Origin& aOrigin) {
      nsCString hashKey = GetArchivedOriginHashKey(aOrigin.OriginSuffix(),
                                                   aOrigin.OriginNoSuffix());

      mHashtable->Remove(hashKey);
    }

    void operator()(const Prefix& aPrefix) {
      for (auto iter = mHashtable->Iter(); !iter.Done(); iter.Next()) {
        const auto& archivedOriginInfo = iter.Data();

        if (archivedOriginInfo->mOriginNoSuffix == aPrefix.OriginNoSuffix()) {
          iter.Remove();
        }
      }
    }

    void operator()(const Pattern& aPattern) {
      for (auto iter = mHashtable->Iter(); !iter.Done(); iter.Next()) {
        const auto& archivedOriginInfo = iter.Data();

        if (aPattern.GetPattern().Matches(
                archivedOriginInfo->mOriginAttributes)) {
          iter.Remove();
        }
      }
    }

    void operator()(const Null& aNull) { mHashtable->Clear(); }
  };

  mData.match(Matcher(aHashtable));
}

/*******************************************************************************
 * QuotaClient
 ******************************************************************************/

QuotaClient* QuotaClient::sInstance = nullptr;

QuotaClient::QuotaClient()
    : mShadowDatabaseMutex("LocalStorage mShadowDatabaseMutex") {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!sInstance, "We expect this to be a singleton!");

  sInstance = this;
}

QuotaClient::~QuotaClient() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(sInstance == this, "We expect this to be a singleton!");

  sInstance = nullptr;
}

mozilla::dom::quota::Client::Type QuotaClient::GetType() {
  return QuotaClient::LS;
}

Result<UsageInfo, nsresult> QuotaClient::InitOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aPersistenceType == PERSISTENCE_TYPE_DEFAULT);
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == aPersistenceType);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_INSPECT(const auto& directory,
                 quotaManager->GetOriginDirectory(aOriginMetadata));

  MOZ_ASSERT(directory);

  QM_TRY(MOZ_TO_RESULT(
      directory->Append(NS_LITERAL_STRING_FROM_CSTRING(LS_DIRECTORY_NAME))));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(directory, Exists));
    MOZ_ASSERT(exists);
  }
#endif

  QM_TRY_INSPECT(const auto& directoryPath, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                                nsString, directory, GetPath));

  QM_TRY_INSPECT(const auto& usageFile, GetUsageFile(directoryPath));

  // XXX Try to make usageFileExists const
  QM_TRY_UNWRAP(bool usageFileExists, ExistsAsFile(*usageFile));

  QM_TRY_INSPECT(const auto& usageJournalFile,
                 GetUsageJournalFile(directoryPath));

  QM_TRY_INSPECT(const bool& usageJournalFileExists,
                 ExistsAsFile(*usageJournalFile));

  if (usageJournalFileExists) {
    if (usageFileExists) {
      QM_TRY(MOZ_TO_RESULT(usageFile->Remove(false)));

      usageFileExists = false;
    }

    QM_TRY(MOZ_TO_RESULT(usageJournalFile->Remove(false)));
  }

  QM_TRY_INSPECT(const auto& file,
                 CloneFileAndAppend(*directory, kDataFileName));

  QM_TRY_INSPECT(const bool& fileExists, ExistsAsFile(*file));

  QM_TRY_INSPECT(
      const UsageInfo& res,
      ([fileExists, usageFileExists, &file, &usageFile, &usageJournalFile,
        &aOriginMetadata]() -> Result<UsageInfo, nsresult> {
        if (fileExists) {
          QM_TRY_RETURN(QM_OR_ELSE_WARN(
              // Expression. To simplify control flow, we call LoadUsageFile
              // unconditionally here, even though it will necessarily fail if
              // usageFileExists is false.
              LoadUsageFile(*usageFile),
              // Fallback.
              ([&file, &usageFile, &usageJournalFile, &aOriginMetadata](
                   const nsresult) -> Result<UsageInfo, nsresult> {
                QM_TRY_INSPECT(
                    const auto& connection,
                    CreateStorageConnectionWithRecovery(
                        *file, *usageFile, aOriginMetadata.mOrigin, [] {}));

                QM_TRY_INSPECT(const int64_t& usage,
                               GetUsage(*connection,
                                        /* aArchivedOriginScope */ nullptr));

                QM_TRY(MOZ_TO_RESULT(
                    UpdateUsageFile(usageFile, usageJournalFile, usage)));

                QM_TRY(MOZ_TO_RESULT(usageJournalFile->Remove(false)));

                MOZ_ASSERT(usage >= 0);
                return UsageInfo{DatabaseUsageType(Some(uint64_t(usage)))};
              })));
        }

        if (usageFileExists) {
          QM_TRY(MOZ_TO_RESULT(usageFile->Remove(false)));
        }

        return UsageInfo{};
      }()));

  // Report unknown files in debug builds, but don't fail, just warn (we don't
  // report unknown files in release builds because that requires extra
  // scanning of the directory which would slow down entire initialization for
  // little benefit).

#ifdef DEBUG
  QM_TRY(CollectEachFileAtomicCancelable(
      *directory, aCanceled,
      [](const nsCOMPtr<nsIFile>& file) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*file));

        switch (dirEntryKind) {
          case nsIFileKind::ExistsAsDirectory:
            Unused << WARN_IF_FILE_IS_UNKNOWN(*file);
            break;

          case nsIFileKind::ExistsAsFile: {
            QM_TRY_INSPECT(
                const auto& leafName,
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, file, GetLeafName));

            if (leafName.Equals(kDataFileName) ||
                leafName.Equals(kJournalFileName) ||
                leafName.Equals(kUsageFileName) ||
                leafName.Equals(kUsageJournalFileName)) {
              return Ok{};
            }

            Unused << WARN_IF_FILE_IS_UNKNOWN(*file);

            break;
          }

          case nsIFileKind::DoesNotExist:
            // Ignore files that got removed externally while iterating.
            break;
        }
        return Ok{};
      }));
#endif

  return res;
}

nsresult QuotaClient::InitOriginWithoutTracking(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  // This is called when a storage/permanent/${origin}/ls directory exists. Even
  // though this shouldn't happen with a "good" profile, we shouldn't return an
  // error here, since that would cause origin initialization to fail. We just
  // warn and otherwise ignore that.
  UNKNOWN_FILE_WARNING(NS_LITERAL_STRING_FROM_CSTRING(LS_DIRECTORY_NAME));
  return NS_OK;
}

Result<UsageInfo, nsresult> QuotaClient::GetUsageForOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aPersistenceType == PERSISTENCE_TYPE_DEFAULT);

  // We can't open the database at this point, since it can be already used
  // by the connection thread. Use the cached value instead.

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  return quotaManager->GetUsageForClient(PERSISTENCE_TYPE_DEFAULT,
                                         aOriginMetadata, Client::LS);
}

nsresult QuotaClient::AboutToClearOrigins(
    const PersistenceScope& aPersistenceScope,
    const OriginScope& aOriginScope) {
  AssertIsOnIOThread();

  // This method is not called when the clearing is triggered by the eviction
  // process. It's on purpose to avoid a problem with the origin access time
  // which can be described as follows:
  // When there's a storage pressure condition and quota manager starts
  // collecting origins for eviction, there can be an origin that hasn't been
  // touched for long time. However, the old implementation of local storage
  // could have touched the origin only recently and the new implementation
  // hasn't had a chance to create a new per origin database for it yet (the
  // data is still in the archive database), so the origin access time hasn't
  // been updated either. In the end, the origin would be evicted despite the
  // fact that there was recent local storage activity.
  // So this method clears the archived data and shadow database entries for
  // given origin scope, but only if it's a privacy-related origin clearing.

  if (!aPersistenceScope.Matches(
          PersistenceScope::CreateFromValue(PERSISTENCE_TYPE_DEFAULT))) {
    return NS_OK;
  }

  // There can be no data for the system principal in the archive or the shadow
  // database. This early return silences potential warnings caused by failed
  // `CreateAerchivedOriginScope` because it calls `GenerateOriginKey2` which
  // doesn't support the system principal.
  if (aOriginScope.IsOrigin() &&
      aOriginScope.GetOrigin() == quota::GetOriginForChrome()) {
    return NS_OK;
  }

  const bool shadowWrites = gShadowWrites;

  QM_TRY_INSPECT(const auto& archivedOriginScope,
                 CreateArchivedOriginScope(aOriginScope));

  if (!gArchivedOrigins) {
    QM_TRY(MOZ_TO_RESULT(LoadArchivedOrigins()));
    MOZ_ASSERT(gArchivedOrigins);
  }

  const bool hasDataForRemoval =
      archivedOriginScope->HasMatches(gArchivedOrigins);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  const nsString& basePath = quotaManager->GetBasePath();

  {
    MutexAutoLock shadowDatabaseLock(mShadowDatabaseMutex);

    QM_TRY_INSPECT(
        const auto& connection,
        ([&basePath]() -> Result<nsCOMPtr<mozIStorageConnection>, nsresult> {
          if (gInitializedShadowStorage) {
            QM_TRY_RETURN(GetShadowStorageConnection(basePath));
          }

          QM_TRY_UNWRAP(auto connection,
                        CreateShadowStorageConnection(basePath));

          gInitializedShadowStorage = true;

          return connection;
        }()));

    {
      Maybe<AutoDatabaseAttacher> maybeAutoArchiveDatabaseAttacher;

      if (hasDataForRemoval) {
        QM_TRY_INSPECT(const auto& archiveFile,
                       GetArchiveFile(quotaManager->GetStoragePath()));

        maybeAutoArchiveDatabaseAttacher.emplace(
            AutoDatabaseAttacher(connection, archiveFile, "archive"_ns));

        QM_TRY(MOZ_TO_RESULT(maybeAutoArchiveDatabaseAttacher->Attach()));
      }

      if (archivedOriginScope->IsPattern()) {
        nsCOMPtr<mozIStorageFunction> function(
            new MatchFunction(archivedOriginScope->GetPattern()));

        QM_TRY(
            MOZ_TO_RESULT(connection->CreateFunction("match"_ns, 2, function)));
      }

      {
        QM_TRY_INSPECT(const auto& stmt,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                           nsCOMPtr<mozIStorageStatement>, connection,
                           CreateStatement, "BEGIN IMMEDIATE;"_ns));

        QM_TRY(MOZ_TO_RESULT(stmt->Execute()));
      }

      if (shadowWrites) {
        QM_TRY(MOZ_TO_RESULT(
            PerformDelete(connection, "main"_ns, archivedOriginScope.get())));
      }

      if (hasDataForRemoval) {
        QM_TRY(MOZ_TO_RESULT(PerformDelete(connection, "archive"_ns,
                                           archivedOriginScope.get())));
      }

      {
        QM_TRY_INSPECT(const auto& stmt,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                           nsCOMPtr<mozIStorageStatement>, connection,
                           CreateStatement, "COMMIT;"_ns));

        QM_TRY(MOZ_TO_RESULT(stmt->Execute()));
      }

      if (archivedOriginScope->IsPattern()) {
        QM_TRY(MOZ_TO_RESULT(connection->RemoveFunction("match"_ns)));
      }

      if (hasDataForRemoval) {
        MOZ_ASSERT(maybeAutoArchiveDatabaseAttacher.isSome());
        QM_TRY(MOZ_TO_RESULT(maybeAutoArchiveDatabaseAttacher->Detach()));

        maybeAutoArchiveDatabaseAttacher.reset();

        MOZ_ASSERT(gArchivedOrigins);
        MOZ_ASSERT(archivedOriginScope->HasMatches(gArchivedOrigins));
        archivedOriginScope->RemoveMatches(gArchivedOrigins);
      }
    }
    QM_TRY(MOZ_TO_RESULT(connection->Close()));
  }

  if (aOriginScope.IsNull()) {
    QM_TRY_INSPECT(const auto& shadowFile, GetShadowFile(basePath));

    QM_TRY(MOZ_TO_RESULT(shadowFile->Remove(false)));

    gInitializedShadowStorage = false;
  }

  return NS_OK;
}

void QuotaClient::OnOriginClearCompleted(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();
}

void QuotaClient::OnRepositoryClearCompleted(PersistenceType aPersistenceType) {
  AssertIsOnIOThread();
}

void QuotaClient::ReleaseIOThreadObjects() {
  AssertIsOnIOThread();

  gInitializationInfo = nullptr;

  // Delete archived origins hashtable since QuotaManager clears the whole
  // storage directory including ls-archive.sqlite.

  gArchivedOrigins = nullptr;
}

void QuotaClient::AbortOperationsForLocks(
    const DirectoryLockIdTable& aDirectoryLockIds) {
  AssertIsOnBackgroundThread();

  // A PrepareDatastoreOp object could already acquire a directory lock for
  // the given origin. Its last step is creation of a Datastore object (which
  // will take ownership of the directory lock) and a PreparedDatastore object
  // which keeps the Datastore alive until a database actor is created.
  // We need to invalidate the PreparedDatastore object when it's created,
  // otherwise the Datastore object can block the origin clear operation for
  // long time. It's not a problem that we don't fail the PrepareDatastoreOp
  // immediatelly (avoiding the creation of the Datastore and PreparedDatastore
  // object). We will call RequestAllowToClose on the database actor once it's
  // created and the child actor will respond by sending AllowToClose which
  // will close the Datastore on the parent side (the closing releases the
  // directory lock).

  InvalidatePrepareDatastoreOpsMatching(
      [&aDirectoryLockIds](const auto& prepareDatastoreOp) {
        // Check if the PrepareDatastoreOp holds an acquired DirectoryLock.
        // Origin clearing can't be blocked by this PrepareDatastoreOp if there
        // is no acquired DirectoryLock. If there is an acquired DirectoryLock,
        // check if the table contains the lock for the PrepareDatastoreOp.
        return IsLockForObjectAcquiredAndContainedInLockTable(
            prepareDatastoreOp, aDirectoryLockIds);
      });

  if (gPrivateDatastores) {
    gPrivateDatastores->RemoveIf([&aDirectoryLockIds](const auto& iter) {
      const auto& privateDatastore = iter.Data();

      // The PrivateDatastore::mDatastore member is not cleared until the
      // PrivateDatastore is destroyed.
      auto& datastore = privateDatastore->MutableDatastoreRef();

      // If the PrivateDatastore exists then it must be registered in
      // Datastore::mHasLivePrivateDatastore as well. The Datastore must have
      // a DirectoryLock if there is a registered PrivateDatastore.
      bool result =
          IsLockForObjectContainedInLockTable(datastore, aDirectoryLockIds);

      // The datastore should be closed after removing from gPrivateDatastores
      // (and eventually unregistered from gDatastores, so a new private
      // browsing session won't see any data from a previous private browsing
      // session) but just in case something still keeps alive the datastore,
      // let's explicitly clear it here.
      if (result) {
        datastore.Clear(nullptr);
      }

      return result;
    });

    if (!gPrivateDatastores->Count()) {
      gPrivateDatastores = nullptr;
    }
  }

  InvalidatePreparedDatastoresMatching([&aDirectoryLockIds](
                                           const auto& preparedDatastore) {
    // The PreparedDatastore::mDatastore member is not cleared until the
    // PreparedDatastore is destroyed.
    const auto& datastore = preparedDatastore.DatastoreRef();

    // If the PreparedDatastore exists then it must be registered in
    // Datastore::mPreparedDatastores as well. The Datastore must have a
    // DirectoryLock if there are registered PreparedDatastore objects.
    return IsLockForObjectContainedInLockTable(datastore, aDirectoryLockIds);
  });

  RequestAllowToCloseDatabasesMatching(
      [&aDirectoryLockIds](const auto& database) {
        const auto& maybeDatastore = database.MaybeDatastoreRef();

        // If the Database is registered in gLiveDatabases then it must have a
        // Datastore.
        MOZ_ASSERT(maybeDatastore.isSome());

        // If the Database is registered in gLiveDatabases then it must be
        // registered in Datastore::mDatabases as well. The Datastore must have
        // a DirectoryLock if there are registered Database objects.
        return IsLockForObjectContainedInLockTable(*maybeDatastore,
                                                   aDirectoryLockIds);
      });
}

void QuotaClient::AbortOperationsForProcess(ContentParentId aContentParentId) {
  AssertIsOnBackgroundThread();

  // XXX Quota Manager should do the wrapping.
  Maybe<ContentParentId> contentParentId;

  if (aContentParentId) {
    contentParentId = Some(aContentParentId);
  }

  // XXX We could try to invalidate other objects here.

  RequestAllowToCloseDatabasesMatching(
      [&contentParentId](const auto& database) {
        return database.ContentParentIdRef() == contentParentId;
      });
}

void QuotaClient::AbortAllOperations() {
  AssertIsOnBackgroundThread();

  InvalidatePrepareDatastoreOpsMatching([](const auto& prepareDatastoreOp) {
    return prepareDatastoreOp.MaybeDirectoryLockRef();
  });

  if (gPrivateDatastores) {
    gPrivateDatastores = nullptr;
  }

  InvalidatePreparedDatastoresMatching([](const auto&) { return true; });

  RequestAllowToCloseDatabasesMatching([](const auto&) { return true; });
}

void QuotaClient::StartIdleMaintenance() { AssertIsOnBackgroundThread(); }

void QuotaClient::StopIdleMaintenance() { AssertIsOnBackgroundThread(); }

void QuotaClient::InitiateShutdown() {
  // gPrepareDatastoreOps are short lived objects running a state machine.
  // The shutdown flag is checked between states, so we don't have to notify
  // all the objects here.
  // Allocation of a new PrepareDatastoreOp object is prevented once the
  // shutdown flag is set.
  // When the last PrepareDatastoreOp finishes, the gPrepareDatastoreOps array
  // is destroyed.

  if (gPreparedDatastores) {
    gPreparedDatastores = nullptr;
  }

  if (gPrivateDatastores) {
    gPrivateDatastores = nullptr;
  }

  RequestAllowToCloseDatabasesMatching([](const auto&) { return true; });

  if (gPreparedObsevers) {
    gPreparedObsevers = nullptr;
  }
}

bool QuotaClient::IsShutdownCompleted() const {
  // Don't have to check gPrivateDatastores and gPreparedDatastores since we
  // nulled it out in InitiateShutdown.
  return !gPrepareDatastoreOps && !gDatastores && !gLiveDatabases;
}

void QuotaClient::ForceKillActors() { ForceKillAllDatabases(); }

nsCString QuotaClient::GetShutdownStatus() const {
  AssertIsOnBackgroundThread();

  nsCString data;

  if (gPrepareDatastoreOps) {
    data.Append("PrepareDatastoreOperations: ");
    data.AppendInt(static_cast<uint32_t>(gPrepareDatastoreOps->Length()));
    data.Append(" (");

    // XXX What's the purpose of adding these to a hashtable before joining them
    // to the string? (Maybe this used to be an ordered container before???)
    nsTHashSet<nsCString> ids;
    std::transform(gPrepareDatastoreOps->cbegin(), gPrepareDatastoreOps->cend(),
                   MakeInserter(ids), [](const auto& prepareDatastoreOp) {
                     nsCString id;
                     prepareDatastoreOp->Stringify(id);
                     return id;
                   });

    StringJoinAppend(data, ", "_ns, ids);

    data.Append(")\n");
  }

  if (gDatastores) {
    data.Append("Datastores: ");
    data.AppendInt(gDatastores->Count());
    data.Append(" (");

    // XXX It might be confusing to remove duplicates here, as the actual list
    // won't match the count then.
    nsTHashSet<nsCString> ids;
    std::transform(gDatastores->Values().cbegin(), gDatastores->Values().cend(),
                   MakeInserter(ids), [](const auto& entry) {
                     nsCString id;
                     entry->Stringify(id);
                     return id;
                   });

    StringJoinAppend(data, ", "_ns, ids);

    data.Append(")\n");
  }

  if (gLiveDatabases) {
    data.Append("LiveDatabases: ");
    data.AppendInt(static_cast<uint32_t>(gLiveDatabases->Length()));
    data.Append(" (");

    // XXX It might be confusing to remove duplicates here, as the actual list
    // won't match the count then.
    nsTHashSet<nsCString> ids;
    std::transform(gLiveDatabases->cbegin(), gLiveDatabases->cend(),
                   MakeInserter(ids), [](const auto& database) {
                     nsCString id;
                     database->Stringify(id);
                     return id;
                   });

    StringJoinAppend(data, ", "_ns, ids);

    data.Append(")\n");
  }

  return data;
}

void QuotaClient::FinalizeShutdown() {
  // And finally, shutdown the connection thread.
  if (gConnectionThread) {
    gConnectionThread->Shutdown();

    gConnectionThread = nullptr;
  }

  if (gDatabases) {
    nsTArray<RefPtr<Database>> databases;

    for (const auto& database : *gDatabases) {
      databases.AppendElement(database);
    }

    for (const auto& database : databases) {
      database->Close();
    }
  }
}

Result<UniquePtr<ArchivedOriginScope>, nsresult>
QuotaClient::CreateArchivedOriginScope(const OriginScope& aOriginScope) {
  AssertIsOnIOThread();

  if (aOriginScope.IsOrigin()) {
    QM_TRY_INSPECT(const auto& principalInfo,
                   QuotaManager::ParseOrigin(aOriginScope.GetOrigin()));

    QM_TRY_INSPECT((const auto& [originAttrSuffix, originKey]),
                   GenerateOriginKey2(principalInfo));

    return ArchivedOriginScope::CreateFromOrigin(originAttrSuffix, originKey);
  }

  if (aOriginScope.IsPrefix()) {
    QM_TRY_INSPECT(const auto& principalInfo,
                   QuotaManager::ParseOrigin(aOriginScope.GetOriginNoSuffix()));

    QM_TRY_INSPECT((const auto& [originAttrSuffix, originKey]),
                   GenerateOriginKey2(principalInfo));

    Unused << originAttrSuffix;

    return ArchivedOriginScope::CreateFromPrefix(originKey);
  }

  if (aOriginScope.IsPattern()) {
    return ArchivedOriginScope::CreateFromPattern(aOriginScope.GetPattern());
  }

  MOZ_ASSERT(aOriginScope.IsNull());

  return ArchivedOriginScope::CreateFromNull();
}

nsresult QuotaClient::PerformDelete(
    mozIStorageConnection* aConnection, const nsACString& aSchemaName,
    ArchivedOriginScope* aArchivedOriginScope) const {
  AssertIsOnIOThread();
  MOZ_ASSERT(aConnection);
  MOZ_ASSERT(aArchivedOriginScope);

  QM_TRY_INSPECT(
      const auto& stmt,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
          "DELETE FROM "_ns + aSchemaName + ".webappsstore2"_ns +
              aArchivedOriginScope->GetBindingClause() + ";"_ns));

  QM_TRY(MOZ_TO_RESULT(aArchivedOriginScope->BindToStatement(stmt)));

  QM_TRY(MOZ_TO_RESULT(stmt->Execute()));

  return NS_OK;
}

NS_IMPL_ISUPPORTS(QuotaClient::MatchFunction, mozIStorageFunction)

NS_IMETHODIMP
QuotaClient::MatchFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aFunctionArguments);
  MOZ_ASSERT(aResult);

  QM_TRY_INSPECT(const auto& suffix,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsAutoCString, aFunctionArguments, GetUTF8String, 1));

  OriginAttributes oa;
  QM_TRY(OkIf(oa.PopulateFromSuffix(suffix)), NS_ERROR_FAILURE);

  const bool result = mPattern.Matches(oa);

  RefPtr<nsVariant> outVar(new nsVariant());
  QM_TRY(MOZ_TO_RESULT(outVar->SetAsBool(result)));

  outVar.forget(aResult);
  return NS_OK;
}

/*******************************************************************************
 * AutoWriteTransaction
 ******************************************************************************/

AutoWriteTransaction::AutoWriteTransaction(bool aShadowWrites)
    : mConnection(nullptr), mShadowWrites(aShadowWrites) {
  AssertIsOnGlobalConnectionThread();

  MOZ_COUNT_CTOR(mozilla::dom::AutoWriteTransaction);
}

AutoWriteTransaction::~AutoWriteTransaction() {
  AssertIsOnGlobalConnectionThread();

  MOZ_COUNT_DTOR(mozilla::dom::AutoWriteTransaction);

  if (mConnection) {
    QM_WARNONLY_TRY(QM_TO_RESULT(mConnection->RollbackWriteTransaction()));

    if (mShadowWrites) {
      QM_WARNONLY_TRY(QM_TO_RESULT(DetachShadowDatabaseAndUnlock()));
    }
  }
}

nsresult AutoWriteTransaction::Start(Connection* aConnection) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(aConnection);
  MOZ_ASSERT(!mConnection);

  if (mShadowWrites) {
    QM_TRY(MOZ_TO_RESULT(LockAndAttachShadowDatabase(aConnection)));
  }

  QM_TRY(MOZ_TO_RESULT(aConnection->BeginWriteTransaction()));

  mConnection = aConnection;

  return NS_OK;
}

nsresult AutoWriteTransaction::Commit() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(mConnection);

  QM_TRY(MOZ_TO_RESULT(mConnection->CommitWriteTransaction()));

  if (mShadowWrites) {
    QM_TRY(MOZ_TO_RESULT(DetachShadowDatabaseAndUnlock()));
  }

  mConnection = nullptr;

  return NS_OK;
}

nsresult AutoWriteTransaction::LockAndAttachShadowDatabase(
    Connection* aConnection) {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(aConnection);
  MOZ_ASSERT(!mConnection);
  MOZ_ASSERT(mShadowDatabaseLock.isNothing());
  MOZ_ASSERT(mShadowWrites);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  mShadowDatabaseLock.emplace(
      aConnection->GetQuotaClient()->ShadowDatabaseMutex());

  QM_TRY(MOZ_TO_RESULT(AttachShadowDatabase(
      quotaManager->GetBasePath(), &aConnection->MutableStorageConnection())));

  return NS_OK;
}

nsresult AutoWriteTransaction::DetachShadowDatabaseAndUnlock() {
  AssertIsOnGlobalConnectionThread();
  MOZ_ASSERT(mConnection);
  MOZ_ASSERT(mShadowDatabaseLock.isSome());
  MOZ_ASSERT(mShadowWrites);

  nsCOMPtr<mozIStorageConnection> storageConnection =
      mConnection->StorageConnection();
  MOZ_ASSERT(storageConnection);

  QM_TRY(MOZ_TO_RESULT(DetachShadowDatabase(storageConnection)));

  mShadowDatabaseLock.reset();

  return NS_OK;
}

}  // namespace mozilla::dom
