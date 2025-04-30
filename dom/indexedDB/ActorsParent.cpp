/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ActorsParent.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <new>
#include <numeric>
#include <tuple>
#include <type_traits>
#include <utility>
#include "ActorsParentCommon.h"
#include "CrashAnnotations.h"
#include "DatabaseFileInfo.h"
#include "DatabaseFileManager.h"
#include "DatabaseFileManagerImpl.h"
#include "DBSchema.h"
#include "ErrorList.h"
#include "IDBCursorType.h"
#include "IDBObjectStore.h"
#include "IDBTransaction.h"
#include "IndexedDBCommon.h"
#include "IndexedDatabaseInlines.h"
#include "IndexedDatabaseManager.h"
#include "IndexedDBCipherKeyManager.h"
#include "KeyPath.h"
#include "MainThreadUtils.h"
#include "ProfilerHelpers.h"
#include "ReportInternalError.h"
#include "SafeRefPtr.h"
#include "SchemaUpgrades.h"
#include "chrome/common/ipc_channel.h"
#include "ipc/IPCMessageUtils.h"
#include "js/RootingAPI.h"
#include "js/StructuredClone.h"
#include "js/Value.h"
#include "jsapi.h"
#include "mozIStorageAsyncConnection.h"
#include "mozIStorageConnection.h"
#include "mozIStorageFunction.h"
#include "mozIStorageProgressHandler.h"
#include "mozIStorageService.h"
#include "mozIStorageStatement.h"
#include "mozIStorageValueArray.h"
#include "mozStorageCID.h"
#include "mozStorageHelper.h"
#include "mozilla/Algorithm.h"
#include "mozilla/ArrayAlgorithm.h"
#include "mozilla/ArrayIterator.h"
#include "mozilla/Assertions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/Casting.h"
#include "mozilla/CondVar.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/InitializedOnce.h"
#include "mozilla/Logging.h"
#include "mozilla/MacroForEach.h"
#include "mozilla/Maybe.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/NotNull.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/RefCountType.h"
#include "mozilla/RefCounted.h"
#include "mozilla/RemoteLazyInputStreamParent.h"
#include "mozilla/RemoteLazyInputStreamStorage.h"
#include "mozilla/Result.h"
#include "mozilla/ResultExtensions.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/SnappyCompressOutputStream.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Unused.h"
#include "mozilla/Variant.h"
#include "mozilla/dom/BlobImpl.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/FileBlobImpl.h"
#include "mozilla/dom/FlippedOnce.h"
#include "mozilla/dom/IDBCursorBinding.h"
#include "mozilla/dom/IDBFactory.h"
#include "mozilla/dom/IPCBlob.h"
#include "mozilla/dom/IPCBlobUtils.h"
#include "mozilla/dom/IndexedDatabase.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/PContentParent.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/indexedDB/IDBResult.h"
#include "mozilla/dom/indexedDB/Key.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBCursor.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBCursorParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBDatabase.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBDatabaseFileParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBDatabaseParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBFactory.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBFactoryParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBFactoryRequestParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBRequest.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBRequestParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBSharedTypes.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBTransactionParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIDBVersionChangeTransactionParent.h"
#include "mozilla/dom/indexedDB/PBackgroundIndexedDBUtilsParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/quota/Assertions.h"
#include "mozilla/dom/quota/CachingDatabaseConnection.h"
#include "mozilla/dom/quota/CheckedUnsafePtr.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/ClientImpl.h"
#include "mozilla/dom/quota/ConditionalCompilation.h"
#include "mozilla/dom/quota/DirectoryLock.h"
#include "mozilla/dom/quota/DirectoryLockInlines.h"
#include "mozilla/dom/quota/DecryptingInputStream_impl.h"
#include "mozilla/dom/quota/EncryptingOutputStream_impl.h"
#include "mozilla/dom/quota/ErrorHandling.h"
#include "mozilla/dom/quota/FileStreams.h"
#include "mozilla/dom/quota/OriginScope.h"
#include "mozilla/dom/quota/PersistenceScope.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "mozilla/dom/quota/PrincipalUtils.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/QuotaObject.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/dom/quota/ThreadUtils.h"
#include "mozilla/dom/quota/UniversalDirectoryLock.h"
#include "mozilla/dom/quota/UsageInfo.h"
#include "mozilla/fallible.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/InputStreamParams.h"
#include "mozilla/ipc/PBackgroundParent.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/mozalloc.h"
#include "mozilla/storage/Variant.h"
#include "NotifyUtils.h"
#include "nsBaseHashtable.h"
#include "nsCOMPtr.h"
#include "nsClassHashtable.h"
#include "nsContentUtils.h"
#include "nsTHashMap.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsEscape.h"
#include "nsHashKeys.h"
#include "nsIAsyncInputStream.h"
#include "nsID.h"
#include "nsIDUtils.h"
#include "nsIDirectoryEnumerator.h"
#include "nsIEventTarget.h"
#include "nsIFile.h"
#include "nsIFileProtocolHandler.h"
#include "nsIFileStreams.h"
#include "nsIFileURL.h"
#include "nsIInputStream.h"
#include "nsIOutputStream.h"
#include "nsIProtocolHandler.h"
#include "nsIRunnable.h"
#include "nsISupports.h"
#include "nsISupportsPriority.h"
#include "nsISupportsUtils.h"
#include "nsIThread.h"
#include "nsIThreadInternal.h"
#include "nsITimer.h"
#include "nsIURIMutator.h"
#include "nsIVariant.h"
#include "nsLiteralString.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsProxyRelease.h"
#include "nsServiceManagerUtils.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsStringFlags.h"
#include "nsStringFwd.h"
#include "nsTArray.h"
#include "nsTHashSet.h"
#include "nsTHashtable.h"
#include "nsTLiteralString.h"
#include "nsTStringRepr.h"
#include "nsThreadPool.h"
#include "nsThreadUtils.h"
#include "nscore.h"
#include "prinrval.h"
#include "prio.h"
#include "prsystem.h"
#include "prthread.h"
#include "prtime.h"
#include "prtypes.h"
#include "snappy/snappy.h"

struct JSContext;
class JSObject;
template <class T>
class nsPtrHashKey;

#define IDB_DEBUG_LOG(_args) \
  MOZ_LOG(IndexedDatabaseManager::GetLoggingModule(), LogLevel::Debug, _args)

#if defined(MOZ_WIDGET_ANDROID)
#  define IDB_MOBILE
#endif

// Helper macros to reduce assertion verbosity
// AUUF == ASSERT_UNREACHABLE_UNLESS_FUZZING
#ifdef DEBUG
#  ifdef FUZZING
#    define NS_AUUF_OR_WARN(...) NS_WARNING(__VA_ARGS__)
#  else
#    define NS_AUUF_OR_WARN(...) MOZ_ASSERT(false, __VA_ARGS__)
#  endif
#  define NS_AUUF_OR_WARN_IF(cond) \
    [](bool aCond) {               \
      if (MOZ_UNLIKELY(aCond)) {   \
        NS_AUUF_OR_WARN(#cond);    \
      }                            \
      return aCond;                \
    }((cond))
#else
#  define NS_AUUF_OR_WARN(...) \
    do {                       \
    } while (false)
#  define NS_AUUF_OR_WARN_IF(cond) static_cast<bool>(cond)
#endif

namespace mozilla {

namespace dom::indexedDB {

using namespace mozilla::dom::quota;
using namespace mozilla::ipc;
using mozilla::dom::quota::Client;

namespace {

class ConnectionPool;
class Database;
struct DatabaseActorInfo;
class DatabaseFile;
class DatabaseLoggingInfo;
class DatabaseMaintenance;
class Factory;
class Maintenance;
class OpenDatabaseOp;
class TransactionBase;
class TransactionDatabaseOperationBase;
class VersionChangeTransaction;
template <bool StatementHasIndexKeyBindings>
struct ValuePopulateResponseHelper;

/*******************************************************************************
 * Constants
 ******************************************************************************/

const int32_t kStorageProgressGranularity = 1000;

// Changing the value here will override the page size of new databases only.
// A journal mode change and VACUUM are needed to change existing databases, so
// the best way to do that is to use the schema version upgrade mechanism.
const uint32_t kSQLitePageSizeOverride =
#ifdef IDB_MOBILE
    2048;
#else
    4096;
#endif

static_assert(kSQLitePageSizeOverride == /* mozStorage default */ 0 ||
                  (kSQLitePageSizeOverride % 2 == 0 &&
                   kSQLitePageSizeOverride >= 512 &&
                   kSQLitePageSizeOverride <= 65536),
              "Must be 0 (disabled) or a power of 2 between 512 and 65536!");

// Set to -1 to use SQLite's default, 0 to disable, or some positive number to
// enforce a custom limit.
const int32_t kMaxWALPages = 5000;  // 20MB on desktop, 10MB on mobile.

// Set to some multiple of the page size to grow the database in larger chunks.
const uint32_t kSQLiteGrowthIncrement = kSQLitePageSizeOverride * 2;

static_assert(kSQLiteGrowthIncrement >= 0 &&
                  kSQLiteGrowthIncrement % kSQLitePageSizeOverride == 0 &&
                  kSQLiteGrowthIncrement < uint32_t(INT32_MAX),
              "Must be 0 (disabled) or a positive multiple of the page size!");

// The maximum number of threads that can be used for database activity at a
// single time. Please keep in sync with the constants in
// test_connection_idle_maintenance*.js tests
const uint32_t kMaxConnectionThreadCount = 20;

static_assert(kMaxConnectionThreadCount, "Must have at least one thread!");

// The maximum number of threads to keep when idle. Until we switch to the STS
// pool, we can reduce the number of idle threads kept around thanks to the
// grace timeout.
const uint32_t kMaxIdleConnectionThreadCount = 1;

static_assert(kMaxConnectionThreadCount >= kMaxIdleConnectionThreadCount,
              "Idle thread limit must be less than total thread limit!");

// The length of time that wanted idle threads will stay alive before being shut
// down.
const uint32_t kConnectionThreadMaxIdleMS = 30 * 1000;  // 30 seconds

// The length of time that excess idle threads will stay alive before being shut
// down.
const uint32_t kConnectionThreadGraceIdleMS = 500;  // 0.5 seconds

// The length of time that database connections will be held open after all
// transactions have completed before doing idle maintenance. Please keep in
// sync with the timeouts in test_connection_idle_maintenance*.js tests
const uint32_t kConnectionIdleMaintenanceMS = 2 * 1000;  // 2 seconds

// The length of time that database connections will be held open after all
// transactions and maintenance have completed.
const uint32_t kConnectionIdleCloseMS = 10 * 1000;  // 10 seconds

#define SAVEPOINT_CLAUSE "SAVEPOINT sp;"_ns

// For efficiency reasons, kEncryptedStreamBlockSize must be a multiple of large
// 4k disk sectors.
static_assert(kEncryptedStreamBlockSize % 4096 == 0);
// Similarly, the file copy buffer size must be a multiple of the encrypted
// block size.
static_assert(kFileCopyBufferSize % kEncryptedStreamBlockSize == 0);

constexpr auto kFileManagerDirectoryNameSuffix = u".files"_ns;
constexpr auto kSQLiteSuffix = u".sqlite"_ns;
constexpr auto kSQLiteJournalSuffix = u".sqlite-journal"_ns;
constexpr auto kSQLiteSHMSuffix = u".sqlite-shm"_ns;
constexpr auto kSQLiteWALSuffix = u".sqlite-wal"_ns;

// The following constants define all names of binding parameters in statements,
// where they are bound by name. This should include all parameter names which
// are bound by name. Binding may be done by index when the statement definition
// and binding are done in the same local scope, and no other reasons prevent
// using the indexes (e.g. multiple statement variants with differing number or
// order of parameters). Neither the styles of specifying parameter names
// (literally vs. via these constants) nor the binding styles (by index vs. by
// name) should not be mixed for the same statement. The decision must be made
// for each statement based on the proximity of statement and binding calls.
constexpr auto kStmtParamNameCurrentKey = "current_key"_ns;
constexpr auto kStmtParamNameRangeBound = "range_bound"_ns;
constexpr auto kStmtParamNameObjectStorePosition = "object_store_position"_ns;
constexpr auto kStmtParamNameLowerKey = "lower_key"_ns;
constexpr auto kStmtParamNameUpperKey = "upper_key"_ns;
constexpr auto kStmtParamNameKey = "key"_ns;
constexpr auto kStmtParamNameObjectStoreId = "object_store_id"_ns;
constexpr auto kStmtParamNameIndexId = "index_id"_ns;
// TODO: Maybe the uses of kStmtParamNameId should be replaced by more
// specific constants such as kStmtParamNameObjectStoreId.
constexpr auto kStmtParamNameId = "id"_ns;
constexpr auto kStmtParamNameValue = "value"_ns;
constexpr auto kStmtParamNameObjectDataKey = "object_data_key"_ns;
constexpr auto kStmtParamNameIndexDataValues = "index_data_values"_ns;
constexpr auto kStmtParamNameData = "data"_ns;
constexpr auto kStmtParamNameFileIds = "file_ids"_ns;
constexpr auto kStmtParamNameValueLocale = "value_locale"_ns;
constexpr auto kStmtParamNameLimit = "limit"_ns;

// The following constants define some names of columns in tables, which are
// referred to in remote locations, e.g. in calls to
// GetBindingClauseForKeyRange.
constexpr auto kColumnNameKey = "key"_ns;
constexpr auto kColumnNameValue = "value"_ns;
constexpr auto kColumnNameAliasSortKey = "sort_column"_ns;

// SQL fragments used at multiple locations.
constexpr auto kOpenLimit = " LIMIT "_ns;

// The deletion marker file is created before RemoveDatabaseFilesAndDirectory
// begins deleting a database. It is removed as the last step of deletion. If a
// deletion marker file is found when initializing the origin, the deletion
// routine is run again to ensure that the database and all of its related files
// are removed. The primary goal of this mechanism is to avoid situations where
// a database has been partially deleted, leading to inconsistent state for the
// origin.
constexpr auto kIdbDeletionMarkerFilePrefix = u"idb-deleting-"_ns;

const uint32_t kDeleteTimeoutMs = 1000;

#ifdef DEBUG

const int32_t kDEBUGThreadPriority = nsISupportsPriority::PRIORITY_NORMAL;
const uint32_t kDEBUGThreadSleepMS = 0;

// Set to a non-zero number to enable debugging of transaction event targets.
// It will cause sleeping after every transaction runnable!
//
// This can be useful for discovering race conditions related to switching to
// another thread. Such races are usually avoided by using MozPromise or
// RunAfterProcessingCurrentEvent. Chaos mode doesn't always help with
// uncovering these issues, and only a precisely targeted sleep call can
// simulate the problem.
const uint32_t kDEBUGTransactionThreadSleepMS = 0;

// Make sure that we notice if we ever accidentally check in a non-zero value.
#  ifdef MOZILLA_OFFICIAL
static_assert(kDEBUGTransactionThreadSleepMS == 0);
#  endif

#endif

/*******************************************************************************
 * Metadata classes
 ******************************************************************************/

// Can be instantiated either on the QuotaManager IO thread or on a
// versionchange transaction thread. These threads can never race so this is
// totally safe.
struct FullIndexMetadata {
  IndexMetadata mCommonMetadata = {0,     nsString(), KeyPath(0), nsCString(),
                                   false, false,      false};

  FlippedOnce<false> mDeleted;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FullIndexMetadata)

 private:
  ~FullIndexMetadata() = default;
};

using IndexTable = nsTHashMap<nsUint64HashKey, SafeRefPtr<FullIndexMetadata>>;

// Can be instantiated either on the QuotaManager IO thread or on a
// versionchange transaction thread. These threads can never race so this is
// totally safe.
struct FullObjectStoreMetadata {
  ObjectStoreMetadata mCommonMetadata;
  IndexTable mIndexes;

  // The auto increment ids are touched on both the background thread and the
  // transaction I/O thread, and they must be kept in sync, so we need a mutex
  // to protect them.
  struct AutoIncrementIds {
    int64_t next;
    int64_t committed;
  };
  DataMutex<AutoIncrementIds> mAutoIncrementIds;

  FlippedOnce<false> mDeleted;

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FullObjectStoreMetadata);

  bool HasLiveIndexes() const;

  FullObjectStoreMetadata(ObjectStoreMetadata aCommonMetadata,
                          const AutoIncrementIds& aAutoIncrementIds)
      : mCommonMetadata{std::move(aCommonMetadata)},
        mAutoIncrementIds{AutoIncrementIds{aAutoIncrementIds},
                          "FullObjectStoreMetadata"} {}

 private:
  ~FullObjectStoreMetadata() = default;
};

using ObjectStoreTable =
    nsTHashMap<nsUint64HashKey, SafeRefPtr<FullObjectStoreMetadata>>;

static_assert(
    std::is_same_v<IndexOrObjectStoreId,
                   std::remove_cv_t<std::remove_reference_t<
                       decltype(std::declval<const ObjectStoreGetParams&>()
                                    .objectStoreId())>>>);
static_assert(
    std::is_same_v<
        IndexOrObjectStoreId,
        std::remove_cv_t<std::remove_reference_t<
            decltype(std::declval<const IndexGetParams&>().objectStoreId())>>>);

struct FullDatabaseMetadata final : AtomicSafeRefCounted<FullDatabaseMetadata> {
  DatabaseMetadata mCommonMetadata;
  nsCString mDatabaseId;
  nsString mFilePath;
  ObjectStoreTable mObjectStores;

  IndexOrObjectStoreId mNextObjectStoreId = 0;
  IndexOrObjectStoreId mNextIndexId = 0;

 public:
  explicit FullDatabaseMetadata(const DatabaseMetadata& aCommonMetadata)
      : mCommonMetadata(aCommonMetadata) {
    AssertIsOnBackgroundThread();
  }

  [[nodiscard]] SafeRefPtr<FullDatabaseMetadata> Duplicate() const;

  MOZ_DECLARE_REFCOUNTED_TYPENAME(FullDatabaseMetadata)
};

template <class Enumerable>
auto MatchMetadataNameOrId(const Enumerable& aEnumerable,
                           IndexOrObjectStoreId aId,
                           Maybe<const nsAString&> aName = Nothing()) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aId);

  const auto it = std::find_if(
      aEnumerable.cbegin(), aEnumerable.cend(),
      [aId, aName](const auto& entry) {
        MOZ_ASSERT(entry.GetKey() != 0);

        const auto& value = entry.GetData();
        MOZ_ASSERT(value);

        return !value->mDeleted &&
               (aId == value->mCommonMetadata.id() ||
                (aName && *aName == value->mCommonMetadata.name()));
      });

  return ToMaybeRef(it != aEnumerable.cend() ? it->GetData().unsafeGetRawPtr()
                                             : nullptr);
}

/*******************************************************************************
 * SQLite functions
 ******************************************************************************/

// WARNING: the hash function used for the database name must not change.
// That's why this function exists separately from mozilla::HashString(), even
// though it is (at the time of writing) equivalent. See bug 780408 and bug
// 940315 for details.
uint32_t HashName(const nsAString& aName) {
  struct Helper {
    static uint32_t RotateBitsLeft32(uint32_t aValue, uint8_t aBits) {
      MOZ_ASSERT(aBits < 32);
      return (aValue << aBits) | (aValue >> (32 - aBits));
    }
  };

  static const uint32_t kGoldenRatioU32 = 0x9e3779b9u;

  return std::accumulate(aName.BeginReading(), aName.EndReading(), uint32_t(0),
                         [](uint32_t hash, char16_t ch) {
                           return kGoldenRatioU32 *
                                  (Helper::RotateBitsLeft32(hash, 5) ^ ch);
                         });
}

nsresult ClampResultCode(nsresult aResultCode) {
  if (NS_SUCCEEDED(aResultCode) ||
      NS_ERROR_GET_MODULE(aResultCode) == NS_ERROR_MODULE_DOM_INDEXEDDB) {
    return aResultCode;
  }

  switch (aResultCode) {
    case NS_ERROR_FILE_NO_DEVICE_SPACE:
      return NS_ERROR_DOM_INDEXEDDB_QUOTA_ERR;
    case NS_ERROR_STORAGE_CONSTRAINT:
      return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
    default:
#ifdef DEBUG
      nsPrintfCString message("Converting non-IndexedDB error code (0x%" PRIX32
                              ") to "
                              "NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR",
                              static_cast<uint32_t>(aResultCode));
      NS_WARNING(message.get());
#else
        ;
#endif
  }

  IDB_REPORT_INTERNAL_ERR();
  return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
}

Result<nsCOMPtr<nsIFileURL>, nsresult> GetDatabaseFileURL(
    nsIFile& aDatabaseFile, const int64_t aDirectoryLockId,
    const Maybe<CipherKey>& aMaybeKey) {
  MOZ_ASSERT(aDirectoryLockId >= -1);

  QM_TRY_INSPECT(
      const auto& protocolHandler,
      MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<nsIProtocolHandler>,
                              MOZ_SELECT_OVERLOAD(do_GetService),
                              NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "file"));

  QM_TRY_INSPECT(const auto& fileHandler,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<nsIFileProtocolHandler>,
                                         MOZ_SELECT_OVERLOAD(do_QueryInterface),
                                         protocolHandler));

  QM_TRY_INSPECT(const auto& mutator, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                          nsCOMPtr<nsIURIMutator>, fileHandler,
                                          NewFileURIMutator, &aDatabaseFile));

  // aDirectoryLockId should only be -1 when we are called
  // - from DatabaseFileManager::InitDirectory when the temporary storage
  //    hasn't been initialized yet. At that time, the in-memory objects (e.g.
  //    OriginInfo) are only being created so it doesn't make sense to tunnel
  //    quota information to QuotaVFS to get corresponding QuotaObject instances
  //    for SQLite files.
  // - from DeleteDatabaseOp::LoadPreviousVersion, since this might require
  //   temporarily exceeding the quota limit before the database can be
  //   deleted.
  const nsCString directoryLockIdClause =
      "&directoryLockId="_ns + IntToCString(aDirectoryLockId);

  const auto keyClause = [&aMaybeKey] {
    nsAutoCString keyClause;
    if (aMaybeKey) {
      keyClause.AssignLiteral("&key=");
      for (uint8_t byte : IndexedDBCipherStrategy::SerializeKey(*aMaybeKey)) {
        keyClause.AppendPrintf("%02x", byte);
      }
    }
    return keyClause;
  }();

  QM_TRY_UNWRAP(auto result, ([&mutator, &directoryLockIdClause, &keyClause] {
                  nsCOMPtr<nsIFileURL> result;
                  nsresult rv = NS_MutateURI(mutator)
                                    .SetQuery("cache=private"_ns +
                                              directoryLockIdClause + keyClause)
                                    .Finalize(result);
                  return NS_SUCCEEDED(rv)
                             ? Result<nsCOMPtr<nsIFileURL>, nsresult>{result}
                             : Err(rv);
                }()));

  return result;
}

nsLiteralCString GetDefaultSynchronousMode() {
  return IndexedDatabaseManager::FullSynchronous() ? "FULL"_ns : "NORMAL"_ns;
}

nsresult SetDefaultPragmas(mozIStorageConnection& aConnection) {
  MOZ_ASSERT(!NS_IsMainThread());

  static constexpr auto kBuiltInPragmas =
      // We use foreign keys in DEBUG builds only because there is a performance
      // cost to using them.
      "PRAGMA foreign_keys = "
#ifdef DEBUG
      "ON"
#else
      "OFF"
#endif
      ";"

      // The "INSERT OR REPLACE" statement doesn't fire the update trigger,
      // instead it fires only the insert trigger. This confuses the update
      // refcount function. This behavior changes with enabled recursive
      // triggers, so the statement fires the delete trigger first and then the
      // insert trigger.
      "PRAGMA recursive_triggers = ON;"

      // We aggressively truncate the database file when idle so don't bother
      // overwriting the WAL with 0 during active periods.
      "PRAGMA secure_delete = OFF;"_ns;

  QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(kBuiltInPragmas)));

  QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(nsAutoCString{
      "PRAGMA synchronous = "_ns + GetDefaultSynchronousMode() + ";"_ns})));

#ifndef IDB_MOBILE
  if (kSQLiteGrowthIncrement) {
    // This is just an optimization so ignore the failure if the disk is
    // currently too full.
    QM_TRY(QM_OR_ELSE_WARN_IF(
        // Expression.
        MOZ_TO_RESULT(
            aConnection.SetGrowthIncrement(kSQLiteGrowthIncrement, ""_ns)),
        // Predicate.
        IsSpecificError<NS_ERROR_FILE_TOO_BIG>,
        // Fallback.
        ErrToDefaultOk<>));
  }
#endif  // IDB_MOBILE

  return NS_OK;
}

nsresult SetJournalMode(mozIStorageConnection& aConnection) {
  MOZ_ASSERT(!NS_IsMainThread());

  // Try enabling WAL mode. This can fail in various circumstances so we have to
  // check the results here.
  constexpr auto journalModeQueryStart = "PRAGMA journal_mode = "_ns;
  constexpr auto journalModeWAL = "wal"_ns;

  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement(
                     aConnection, journalModeQueryStart + journalModeWAL));

  QM_TRY_INSPECT(
      const auto& journalMode,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, *stmt, GetUTF8String, 0));

  if (journalMode.Equals(journalModeWAL)) {
    // WAL mode successfully enabled. Maybe set limits on its size here.
    if (kMaxWALPages >= 0) {
      QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
          "PRAGMA wal_autocheckpoint = "_ns + IntToCString(kMaxWALPages))));
    }
  } else {
    NS_WARNING("Failed to set WAL mode, falling back to normal journal mode.");
#ifdef IDB_MOBILE
    QM_TRY(MOZ_TO_RESULT(
        aConnection.ExecuteSimpleSQL(journalModeQueryStart + "truncate"_ns)));
#endif
  }

  return NS_OK;
}

Result<MovingNotNull<nsCOMPtr<mozIStorageConnection>>, nsresult> OpenDatabase(
    mozIStorageService& aStorageService, nsIFileURL& aFileURL,
    const uint32_t aTelemetryId = 0) {
  const nsAutoCString telemetryFilename =
      aTelemetryId ? "indexedDB-"_ns + IntToCString(aTelemetryId) +
                         NS_ConvertUTF16toUTF8(kSQLiteSuffix)
                   : nsAutoCString();

  QM_TRY_UNWRAP(auto connection,
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                    nsCOMPtr<mozIStorageConnection>, aStorageService,
                    OpenDatabaseWithFileURL, &aFileURL, telemetryFilename,
                    mozIStorageService::CONNECTION_INTERRUPTIBLE));

  return WrapMovingNotNull(std::move(connection));
}

Result<MovingNotNull<nsCOMPtr<mozIStorageConnection>>, nsresult>
OpenDatabaseAndHandleBusy(mozIStorageService& aStorageService,
                          nsIFileURL& aFileURL,
                          const uint32_t aTelemetryId = 0) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());

  using ConnectionType = Maybe<MovingNotNull<nsCOMPtr<mozIStorageConnection>>>;

  QM_TRY_UNWRAP(auto connection,
                QM_OR_ELSE_WARN_IF(
                    // Expression
                    OpenDatabase(aStorageService, aFileURL, aTelemetryId)
                        .map([](auto connection) -> ConnectionType {
                          return Some(std::move(connection));
                        }),
                    // Predicate.
                    IsSpecificError<NS_ERROR_STORAGE_BUSY>,
                    // Fallback.
                    ErrToDefaultOk<ConnectionType>));

  if (connection.isNothing()) {
#ifdef DEBUG
    {
      nsCString path;
      MOZ_ALWAYS_SUCCEEDS(aFileURL.GetFileName(path));

      nsPrintfCString message(
          "Received NS_ERROR_STORAGE_BUSY when attempting to open database "
          "'%s', retrying for up to 10 seconds",
          path.get());
      NS_WARNING(message.get());
    }
#endif

    // Another thread must be checkpointing the WAL. Wait up to 10 seconds for
    // that to complete.
    const TimeStamp start = TimeStamp::NowLoRes();

    do {
      PR_Sleep(PR_MillisecondsToInterval(100));

      QM_TRY_UNWRAP(connection,
                    QM_OR_ELSE_WARN_IF(
                        // Expression.
                        OpenDatabase(aStorageService, aFileURL, aTelemetryId)
                            .map([](auto connection) -> ConnectionType {
                              return Some(std::move(connection));
                            }),
                        // Predicate.
                        ([&start](nsresult aValue) {
                          return aValue == NS_ERROR_STORAGE_BUSY &&
                                 TimeStamp::NowLoRes() - start <=
                                     TimeDuration::FromSeconds(10);
                        }),
                        // Fallback.
                        ErrToDefaultOk<ConnectionType>));
    } while (connection.isNothing());
  }

  return connection.extract();
}

// Returns true if a given nsIFile exists and is a directory. Returns false if
// it doesn't exist. Returns an error if it exists, but is not a directory, or
// any other error occurs.
Result<bool, nsresult> ExistsAsDirectory(nsIFile& aDirectory) {
  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aDirectory, Exists));

  if (exists) {
    QM_TRY_INSPECT(const bool& isDirectory,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aDirectory, IsDirectory));

    QM_TRY(OkIf(isDirectory), Err(NS_ERROR_FAILURE));
  }

  return exists;
}

constexpr nsresult mapNoDeviceSpaceError(nsresult aRv) {
  if (aRv == NS_ERROR_FILE_NO_DEVICE_SPACE) {
    // mozstorage translates SQLITE_FULL to
    // NS_ERROR_FILE_NO_DEVICE_SPACE, which we know better as
    // NS_ERROR_DOM_INDEXEDDB_QUOTA_ERR.
    return NS_ERROR_DOM_INDEXEDDB_QUOTA_ERR;
  }
  return aRv;
}

Result<MovingNotNull<nsCOMPtr<mozIStorageConnection>>, nsresult>
CreateStorageConnection(nsIFile& aDBFile, nsIFile& aFMDirectory,
                        const nsAString& aName, const nsACString& aOrigin,
                        const int64_t aDirectoryLockId,
                        const uint32_t aTelemetryId,
                        const Maybe<CipherKey>& aMaybeKey) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectoryLockId >= -1);

  AUTO_PROFILER_LABEL("CreateStorageConnection", DOM);

  QM_TRY_INSPECT(const auto& dbFileUrl,
                 GetDatabaseFileURL(aDBFile, aDirectoryLockId, aMaybeKey));

  QM_TRY_INSPECT(const auto& storageService,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  QM_TRY_UNWRAP(
      auto connection,
      QM_OR_ELSE_WARN_IF(
          // Expression.
          OpenDatabaseAndHandleBusy(*storageService, *dbFileUrl, aTelemetryId)
              .map([](auto connection) -> nsCOMPtr<mozIStorageConnection> {
                return std::move(connection).unwrapBasePtr();
              }),
          // Predicate.
          ([&aName](nsresult aValue) {
            // If we're just opening the database during origin initialization,
            // then we don't want to erase any files. The failure here will fail
            // origin initialization too.
            return IsDatabaseCorruptionError(aValue) && !aName.IsVoid();
          }),
          // Fallback.
          ErrToDefaultOk<nsCOMPtr<mozIStorageConnection>>));

  if (!connection) {
    // XXX Shouldn't we also update quota usage?

    // Nuke the database file.
    QM_TRY(MOZ_TO_RESULT(aDBFile.Remove(false)));
    QM_TRY_INSPECT(const bool& existsAsDirectory,
                   ExistsAsDirectory(aFMDirectory));

    if (existsAsDirectory) {
      QM_TRY(MOZ_TO_RESULT(aFMDirectory.Remove(true)));
    }

    QM_TRY_UNWRAP(connection, OpenDatabaseAndHandleBusy(
                                  *storageService, *dbFileUrl, aTelemetryId));
  }

  QM_TRY(MOZ_TO_RESULT(SetDefaultPragmas(*connection)));
  QM_TRY(MOZ_TO_RESULT(connection->EnableModule("filesystem"_ns)));

  // Check to make sure that the database schema is correct.
  QM_TRY_INSPECT(const int32_t& schemaVersion,
                 MOZ_TO_RESULT_INVOKE_MEMBER(connection, GetSchemaVersion));

  // Unknown schema will fail origin initialization too.
  QM_TRY(OkIf(schemaVersion || !aName.IsVoid()),
         Err(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR), [](const auto&) {
           IDB_WARNING("Unable to open IndexedDB database, schema is not set!");
         });

  QM_TRY(
      OkIf(schemaVersion <= kSQLiteSchemaVersion),
      Err(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR), [](const auto&) {
        IDB_WARNING("Unable to open IndexedDB database, schema is too high!");
      });

  bool journalModeSet = false;

  if (schemaVersion != kSQLiteSchemaVersion) {
    const bool newDatabase = !schemaVersion;

    if (newDatabase) {
      // Set the page size first.
      const auto sqlitePageSizeOverride =
          aMaybeKey ? 8192 : kSQLitePageSizeOverride;
      if (sqlitePageSizeOverride) {
        QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL(nsPrintfCString(
            "PRAGMA page_size = %" PRIu32 ";", sqlitePageSizeOverride))));
      }

      // We have to set the auto_vacuum mode before opening a transaction.
      QM_TRY((MOZ_TO_RESULT_INVOKE_MEMBER(
                  connection, ExecuteSimpleSQL,
#ifdef IDB_MOBILE
                  // Turn on full auto_vacuum mode to reclaim disk space on
                  // mobile devices (at the cost of some COMMIT speed).
                  "PRAGMA auto_vacuum = FULL;"_ns
#else
                  // Turn on incremental auto_vacuum mode on desktop builds.
                  "PRAGMA auto_vacuum = INCREMENTAL;"_ns
#endif
                  )
                  .mapErr(mapNoDeviceSpaceError)));

      QM_TRY(MOZ_TO_RESULT(SetJournalMode(*connection)));

      journalModeSet = true;
    } else {
#ifdef DEBUG
      // Disable foreign key support while upgrading. This has to be done before
      // starting a transaction.
      MOZ_ALWAYS_SUCCEEDS(
          connection->ExecuteSimpleSQL("PRAGMA foreign_keys = OFF;"_ns));
#endif
    }

    bool vacuumNeeded = false;

    mozStorageTransaction transaction(
        connection.get(), false, mozIStorageConnection::TRANSACTION_IMMEDIATE);

    QM_TRY(MOZ_TO_RESULT(transaction.Start()));

    if (newDatabase) {
      QM_TRY(MOZ_TO_RESULT(CreateTables(*connection)));

#ifdef DEBUG
      {
        QM_TRY_INSPECT(
            const int32_t& schemaVersion,
            MOZ_TO_RESULT_INVOKE_MEMBER(connection, GetSchemaVersion),
            QM_ASSERT_UNREACHABLE);
        MOZ_ASSERT(schemaVersion == kSQLiteSchemaVersion);
      }
#endif

      // The parameter names are not used, parameters are bound by index only
      // locally in the same function.
      QM_TRY_INSPECT(
          const auto& stmt,
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
              nsCOMPtr<mozIStorageStatement>, connection, CreateStatement,
              "INSERT INTO database (name, origin) "
              "VALUES (:name, :origin)"_ns));

      QM_TRY(MOZ_TO_RESULT(stmt->BindStringByIndex(0, aName)));
      QM_TRY(MOZ_TO_RESULT(stmt->BindUTF8StringByIndex(1, aOrigin)));
      QM_TRY(MOZ_TO_RESULT(stmt->Execute()));
    } else {
      QM_TRY_UNWRAP(vacuumNeeded, MaybeUpgradeSchema(*connection, schemaVersion,
                                                     aFMDirectory, aOrigin));
    }

    QM_TRY(MOZ_TO_RESULT_INVOKE_MEMBER(transaction, Commit)
               .mapErr(mapNoDeviceSpaceError));

#ifdef DEBUG
    if (!newDatabase) {
      // Re-enable foreign key support after doing a foreign key check.
      QM_TRY_INSPECT(const bool& foreignKeyError,
                     CreateAndExecuteSingleStepStatement<
                         SingleStepResult::ReturnNullIfNoResult>(
                         *connection, "PRAGMA foreign_key_check;"_ns),
                     QM_ASSERT_UNREACHABLE);

      MOZ_ASSERT(!foreignKeyError, "Database has inconsisistent foreign keys!");

      MOZ_ALWAYS_SUCCEEDS(
          connection->ExecuteSimpleSQL("PRAGMA foreign_keys = OFF;"_ns));
    }
#endif

    if (kSQLitePageSizeOverride && !newDatabase) {
      QM_TRY_INSPECT(const auto& stmt,
                     CreateAndExecuteSingleStepStatement(
                         *connection, "PRAGMA page_size;"_ns));

      QM_TRY_INSPECT(const int32_t& pageSize,
                     MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt32, 0));
      MOZ_ASSERT(pageSize >= 512 && pageSize <= 65536);

      if (kSQLitePageSizeOverride != uint32_t(pageSize)) {
        // We must not be in WAL journal mode to change the page size.
        QM_TRY(MOZ_TO_RESULT(
            connection->ExecuteSimpleSQL("PRAGMA journal_mode = DELETE;"_ns)));

        QM_TRY_INSPECT(const auto& stmt,
                       CreateAndExecuteSingleStepStatement(
                           *connection, "PRAGMA journal_mode;"_ns));

        QM_TRY_INSPECT(const auto& journalMode,
                       MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCString, *stmt,
                                                         GetUTF8String, 0));

        if (journalMode.EqualsLiteral("delete")) {
          // Successfully set to rollback journal mode so changing the page size
          // is possible with a VACUUM.
          QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL(nsPrintfCString(
              "PRAGMA page_size = %" PRIu32 ";", kSQLitePageSizeOverride))));

          // We will need to VACUUM in order to change the page size.
          vacuumNeeded = true;
        } else {
          NS_WARNING(
              "Failed to set journal_mode for database, unable to "
              "change the page size!");
        }
      }
    }

    if (vacuumNeeded) {
      QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL("VACUUM;"_ns)));
    }

    if (newDatabase || vacuumNeeded) {
      if (journalModeSet) {
        // Make sure we checkpoint to get an accurate file size.
        QM_TRY(MOZ_TO_RESULT(
            connection->ExecuteSimpleSQL("PRAGMA wal_checkpoint(FULL);"_ns)));
      }

      QM_TRY_INSPECT(const int64_t& fileSize,
                     MOZ_TO_RESULT_INVOKE_MEMBER(aDBFile, GetFileSize));
      MOZ_ASSERT(fileSize > 0);

      PRTime vacuumTime = PR_Now();
      MOZ_ASSERT(vacuumTime);

      // The parameter names are not used, parameters are bound by index only
      // locally in the same function.
      QM_TRY_INSPECT(
          const auto& vacuumTimeStmt,
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsCOMPtr<mozIStorageStatement>,
                                            connection, CreateStatement,
                                            "UPDATE database "
                                            "SET last_vacuum_time = :time"
                                            ", last_vacuum_size = :size;"_ns));

      QM_TRY(MOZ_TO_RESULT(vacuumTimeStmt->BindInt64ByIndex(0, vacuumTime)));
      QM_TRY(MOZ_TO_RESULT(vacuumTimeStmt->BindInt64ByIndex(1, fileSize)));
      QM_TRY(MOZ_TO_RESULT(vacuumTimeStmt->Execute()));
    }
  }

  if (!journalModeSet) {
    QM_TRY(MOZ_TO_RESULT(SetJournalMode(*connection)));
  }

  return WrapMovingNotNullUnchecked(std::move(connection));
}

nsCOMPtr<nsIFile> GetFileForPath(const nsAString& aPath) {
  MOZ_ASSERT(!aPath.IsEmpty());

  QM_TRY_RETURN(QM_NewLocalFile(aPath), nullptr);
}

Result<MovingNotNull<nsCOMPtr<mozIStorageConnection>>, nsresult>
GetStorageConnection(nsIFile& aDatabaseFile, const int64_t aDirectoryLockId,
                     const uint32_t aTelemetryId,
                     const Maybe<CipherKey>& aMaybeKey) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aDirectoryLockId >= 0);

  AUTO_PROFILER_LABEL("GetStorageConnection", DOM);

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aDatabaseFile, Exists));

  QM_TRY(OkIf(exists), Err(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR),
         IDB_REPORT_INTERNAL_ERR_LAMBDA);

  QM_TRY_INSPECT(
      const auto& dbFileUrl,
      GetDatabaseFileURL(aDatabaseFile, aDirectoryLockId, aMaybeKey));

  QM_TRY_INSPECT(const auto& storageService,
                 MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<mozIStorageService>,
                                         MOZ_SELECT_OVERLOAD(do_GetService),
                                         MOZ_STORAGE_SERVICE_CONTRACTID));

  QM_TRY_UNWRAP(
      nsCOMPtr<mozIStorageConnection> connection,
      OpenDatabaseAndHandleBusy(*storageService, *dbFileUrl, aTelemetryId));

  QM_TRY(MOZ_TO_RESULT(SetDefaultPragmas(*connection)));

  QM_TRY(MOZ_TO_RESULT(SetJournalMode(*connection)));

  return WrapMovingNotNullUnchecked(std::move(connection));
}

Result<MovingNotNull<nsCOMPtr<mozIStorageConnection>>, nsresult>
GetStorageConnection(const nsAString& aDatabaseFilePath,
                     const int64_t aDirectoryLockId,
                     const uint32_t aTelemetryId,
                     const Maybe<CipherKey>& aMaybeKey) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(!aDatabaseFilePath.IsEmpty());
  MOZ_ASSERT(StringEndsWith(aDatabaseFilePath, kSQLiteSuffix));
  MOZ_ASSERT(aDirectoryLockId >= 0);

  nsCOMPtr<nsIFile> dbFile = GetFileForPath(aDatabaseFilePath);

  QM_TRY(OkIf(dbFile), Err(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR),
         IDB_REPORT_INTERNAL_ERR_LAMBDA);

  return GetStorageConnection(*dbFile, aDirectoryLockId, aTelemetryId,
                              aMaybeKey);
}

/*******************************************************************************
 * ConnectionPool declarations
 ******************************************************************************/

class DatabaseConnection final : public CachingDatabaseConnection {
  friend class ConnectionPool;

  enum class CheckpointMode { Full, Restart, Truncate };

 public:
  class AutoSavepoint;
  class UpdateRefcountFunction;

 private:
  InitializedOnce<const NotNull<SafeRefPtr<DatabaseFileManager>>> mFileManager;
  RefPtr<UpdateRefcountFunction> mUpdateRefcountFunction;
  RefPtr<QuotaObject> mQuotaObject;
  RefPtr<QuotaObject> mJournalQuotaObject;
  IDBTransaction::Durability mLastDurability;
  bool mInReadTransaction;
  bool mInWriteTransaction;

#ifdef DEBUG
  uint32_t mDEBUGSavepointCount;
#endif

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(DatabaseConnection)

  UpdateRefcountFunction* GetUpdateRefcountFunction() const {
    AssertIsOnConnectionThread();

    return mUpdateRefcountFunction;
  }

  nsresult BeginWriteTransaction(const IDBTransaction::Durability aDurability);

  nsresult CommitWriteTransaction();

  void RollbackWriteTransaction();

  void FinishWriteTransaction();

  nsresult StartSavepoint();

  nsresult ReleaseSavepoint();

  nsresult RollbackSavepoint();

  nsresult Checkpoint() {
    AssertIsOnConnectionThread();

    return CheckpointInternal(CheckpointMode::Full);
  }

  void DoIdleProcessing(bool aNeedsCheckpoint,
                        const Atomic<bool>& aInterrupted);

  void Close();

  nsresult DisableQuotaChecks();

  void EnableQuotaChecks();

 private:
  DatabaseConnection(
      MovingNotNull<nsCOMPtr<mozIStorageConnection>> aStorageConnection,
      MovingNotNull<SafeRefPtr<DatabaseFileManager>> aFileManager);

  ~DatabaseConnection();

  nsresult Init();

  nsresult CheckpointInternal(CheckpointMode aMode);

  Result<uint32_t, nsresult> GetFreelistCount(
      CachedStatement& aCachedStatement);

  /**
   * On success, returns whether some pages were freed.
   */
  Result<bool, nsresult> ReclaimFreePagesWhileIdle(
      CachedStatement& aFreelistStatement, CachedStatement& aRollbackStatement,
      uint32_t aFreelistCount, bool aNeedsCheckpoint,
      const Atomic<bool>& aInterrupted);

  Result<int64_t, nsresult> GetFileSize(const nsAString& aPath);
};

class MOZ_STACK_CLASS DatabaseConnection::AutoSavepoint final {
  DatabaseConnection* mConnection;
#ifdef DEBUG
  const TransactionBase* mDEBUGTransaction;
#endif

 public:
  AutoSavepoint();
  ~AutoSavepoint();

  nsresult Start(const TransactionBase& aTransaction);

  nsresult Commit();
};

class DatabaseConnection::UpdateRefcountFunction final
    : public mozIStorageFunction {
  class FileInfoEntry;

  enum class UpdateType { Increment, Decrement };

  DatabaseConnection* const mConnection;
  DatabaseFileManager& mFileManager;
  nsClassHashtable<nsUint64HashKey, FileInfoEntry> mFileInfoEntries;
  nsTHashMap<nsUint64HashKey, NotNull<FileInfoEntry*>> mSavepointEntriesIndex;

  nsTArray<int64_t> mJournalsToCreateBeforeCommit;
  nsTArray<int64_t> mJournalsToRemoveAfterCommit;
  nsTArray<int64_t> mJournalsToRemoveAfterAbort;

  bool mInSavepoint;

 public:
  NS_DECL_ISUPPORTS_ONEVENTTARGET
  NS_DECL_MOZISTORAGEFUNCTION

  UpdateRefcountFunction(DatabaseConnection* aConnection,
                         DatabaseFileManager& aFileManager);

  nsresult WillCommit();

  void DidCommit();

  void DidAbort();

  void StartSavepoint();

  void ReleaseSavepoint();

  void RollbackSavepoint();

  void Reset();

 private:
  ~UpdateRefcountFunction() = default;

  nsresult ProcessValue(mozIStorageValueArray* aValues, int32_t aIndex,
                        UpdateType aUpdateType);

  nsresult CreateJournals();

  nsresult RemoveJournals(const nsTArray<int64_t>& aJournals);
};

class DatabaseConnection::UpdateRefcountFunction::FileInfoEntry final {
  SafeRefPtr<DatabaseFileInfo> mFileInfo;
  int32_t mDelta;
  int32_t mSavepointDelta;

 public:
  explicit FileInfoEntry(SafeRefPtr<DatabaseFileInfo> aFileInfo)
      : mFileInfo(std::move(aFileInfo)), mDelta(0), mSavepointDelta(0) {
    MOZ_COUNT_CTOR(DatabaseConnection::UpdateRefcountFunction::FileInfoEntry);
  }

  void IncDeltas(bool aUpdateSavepointDelta) {
    ++mDelta;
    if (aUpdateSavepointDelta) {
      ++mSavepointDelta;
    }
  }
  void DecDeltas(bool aUpdateSavepointDelta) {
    --mDelta;
    if (aUpdateSavepointDelta) {
      --mSavepointDelta;
    }
  }
  void DecBySavepointDelta() { mDelta -= mSavepointDelta; }
  SafeRefPtr<DatabaseFileInfo> ReleaseFileInfo() {
    return std::move(mFileInfo);
  }
  void MaybeUpdateDBRefs() {
    if (mDelta) {
      mFileInfo->UpdateDBRefs(mDelta);
    }
  }

  int32_t Delta() const { return mDelta; }
  int32_t SavepointDelta() const { return mSavepointDelta; }

  ~FileInfoEntry() {
    MOZ_COUNT_DTOR(DatabaseConnection::UpdateRefcountFunction::FileInfoEntry);
  }
};

class ConnectionPool final {
 public:
  class FinishCallback;

 private:
  class ConnectionRunnable;
  class CloseConnectionRunnable;
  struct DatabaseInfo;
  struct DatabaseCompleteCallback;
  class FinishCallbackWrapper;
  class IdleConnectionRunnable;

#ifdef DEBUG
  class TransactionRunnable;
#endif
  class TransactionInfo;
  struct TransactionInfoPair;

  struct IdleResource {
    TimeStamp mIdleTime;

    IdleResource(const IdleResource& aOther) = delete;
    IdleResource(IdleResource&& aOther) noexcept
        : IdleResource(aOther.mIdleTime) {}
    IdleResource& operator=(const IdleResource& aOther) = delete;
    IdleResource& operator=(IdleResource&& aOther) = delete;

   protected:
    explicit IdleResource(const TimeStamp& aIdleTime);

    ~IdleResource();
  };

  struct IdleDatabaseInfo final : public IdleResource {
    InitializedOnce<const NotNull<DatabaseInfo*>> mDatabaseInfo;

   public:
    explicit IdleDatabaseInfo(DatabaseInfo& aDatabaseInfo);

    IdleDatabaseInfo(const IdleDatabaseInfo& aOther) = delete;
    IdleDatabaseInfo(IdleDatabaseInfo&& aOther) noexcept
        : IdleResource(std::move(aOther)),
          mDatabaseInfo{std::move(aOther.mDatabaseInfo)} {
      MOZ_ASSERT(mDatabaseInfo);

      MOZ_COUNT_CTOR(ConnectionPool::IdleDatabaseInfo);
    }
    IdleDatabaseInfo& operator=(const IdleDatabaseInfo& aOther) = delete;
    IdleDatabaseInfo& operator=(IdleDatabaseInfo&& aOther) = delete;

    ~IdleDatabaseInfo();

    bool operator==(const IdleDatabaseInfo& aOther) const {
      return *mDatabaseInfo == *aOther.mDatabaseInfo;
    }

    bool operator==(const DatabaseInfo* aDatabaseInfo) const {
      return *mDatabaseInfo == aDatabaseInfo;
    }

    bool operator<(const IdleDatabaseInfo& aOther) const {
      return mIdleTime < aOther.mIdleTime;
    }
  };

  struct PerformingIdleMaintenanceDatabaseInfo {
    const NotNull<DatabaseInfo*> mDatabaseInfo;
    RefPtr<IdleConnectionRunnable> mIdleConnectionRunnable;

    PerformingIdleMaintenanceDatabaseInfo(
        DatabaseInfo& aDatabaseInfo,
        RefPtr<IdleConnectionRunnable> aIdleConnectionRunnable);

    PerformingIdleMaintenanceDatabaseInfo(
        const PerformingIdleMaintenanceDatabaseInfo& aOther) = delete;
    PerformingIdleMaintenanceDatabaseInfo(
        PerformingIdleMaintenanceDatabaseInfo&& aOther) noexcept
        : mDatabaseInfo{aOther.mDatabaseInfo},
          mIdleConnectionRunnable{std::move(aOther.mIdleConnectionRunnable)} {
      MOZ_COUNT_CTOR(ConnectionPool::PerformingIdleMaintenanceDatabaseInfo);
    }
    PerformingIdleMaintenanceDatabaseInfo& operator=(
        const PerformingIdleMaintenanceDatabaseInfo& aOther) = delete;
    PerformingIdleMaintenanceDatabaseInfo& operator=(
        PerformingIdleMaintenanceDatabaseInfo&& aOther) = delete;

    ~PerformingIdleMaintenanceDatabaseInfo();

    bool operator==(const DatabaseInfo* aDatabaseInfo) const {
      return mDatabaseInfo == aDatabaseInfo;
    }
  };

  // This mutex guards mDatabases, see below.
  Mutex mDatabasesMutex MOZ_UNANNOTATED;

  nsCOMPtr<nsIThreadPool> mIOTarget;
  nsTArray<IdleDatabaseInfo> mIdleDatabases;
  nsTArray<PerformingIdleMaintenanceDatabaseInfo>
      mDatabasesPerformingIdleMaintenance;
  nsCOMPtr<nsITimer> mIdleTimer;
  TimeStamp mTargetIdleTime;

  // Only modifed on the owning thread, but read on multiple threads. Therefore
  // all modifications and all reads off the owning thread must be protected by
  // mDatabasesMutex.
  nsClassHashtable<nsCStringHashKey, DatabaseInfo> mDatabases;

  nsClassHashtable<nsUint64HashKey, TransactionInfo> mTransactions;
  nsTArray<NotNull<TransactionInfo*>> mQueuedTransactions;

  nsTArray<UniquePtr<DatabaseCompleteCallback>> mCompleteCallbacks;

  uint64_t mNextTransactionId;
  FlippedOnce<false> mShutdownRequested;
  FlippedOnce<false> mShutdownComplete;

 public:
  ConnectionPool();

  void AssertIsOnOwningThread() const {
    NS_ASSERT_OWNINGTHREAD(ConnectionPool);
  }

  Result<RefPtr<DatabaseConnection>, nsresult> GetOrCreateConnection(
      const Database& aDatabase);

  uint64_t Start(const nsID& aBackgroundChildLoggingId,
                 const nsACString& aDatabaseId, int64_t aLoggingSerialNumber,
                 const nsTArray<nsString>& aObjectStoreNames,
                 bool aIsWriteTransaction,
                 TransactionDatabaseOperationBase* aTransactionOp);

  void Dispatch(uint64_t aTransactionId, nsIRunnable* aRunnable);

  void Finish(uint64_t aTransactionId, FinishCallback* aCallback);

  void CloseDatabaseWhenIdle(const nsACString& aDatabaseId) {
    Unused << CloseDatabaseWhenIdleInternal(aDatabaseId);
  }

  void WaitForDatabaseToComplete(const nsCString& aDatabaseId,
                                 nsIRunnable* aCallback);

  void Shutdown();

  NS_INLINE_DECL_REFCOUNTING(ConnectionPool)

 private:
  ~ConnectionPool();

  static void IdleTimerCallback(nsITimer* aTimer, void* aClosure);

  static uint32_t SerialNumber() { return ++sSerialNumber; }

  static uint32_t sSerialNumber;

  void Cleanup();

  void AdjustIdleTimer();

  void CancelIdleTimer();

  void CloseIdleDatabases();

  bool ScheduleTransaction(TransactionInfo& aTransactionInfo,
                           bool aFromQueuedTransactions);

  void NoteFinishedTransaction(uint64_t aTransactionId);

  void ScheduleQueuedTransactions();

  void NoteIdleDatabase(DatabaseInfo& aDatabaseInfo);

  void NoteClosedDatabase(DatabaseInfo& aDatabaseInfo);

  bool MaybeFireCallback(DatabaseCompleteCallback* aCallback);

  void PerformIdleDatabaseMaintenance(DatabaseInfo& aDatabaseInfo);

  void CloseDatabase(DatabaseInfo& aDatabaseInfo) const;

  bool CloseDatabaseWhenIdleInternal(const nsACString& aDatabaseId);
};

class ConnectionPool::ConnectionRunnable : public Runnable {
 protected:
  DatabaseInfo& mDatabaseInfo;
  nsCOMPtr<nsIEventTarget> mOwningEventTarget;

  explicit ConnectionRunnable(DatabaseInfo& aDatabaseInfo);

  ~ConnectionRunnable() override = default;
};

class ConnectionPool::IdleConnectionRunnable final : public ConnectionRunnable {
  const bool mNeedsCheckpoint;
  Atomic<bool> mInterrupted;

 public:
  IdleConnectionRunnable(DatabaseInfo& aDatabaseInfo, bool aNeedsCheckpoint)
      : ConnectionRunnable(aDatabaseInfo), mNeedsCheckpoint(aNeedsCheckpoint) {}

  NS_INLINE_DECL_REFCOUNTING_INHERITED(IdleConnectionRunnable,
                                       ConnectionRunnable)

  void Interrupt() { mInterrupted = true; }

 private:
  ~IdleConnectionRunnable() override = default;

  NS_DECL_NSIRUNNABLE
};

class ConnectionPool::CloseConnectionRunnable final
    : public ConnectionRunnable {
 public:
  explicit CloseConnectionRunnable(DatabaseInfo& aDatabaseInfo)
      : ConnectionRunnable(aDatabaseInfo) {}

  NS_INLINE_DECL_REFCOUNTING_INHERITED(CloseConnectionRunnable,
                                       ConnectionRunnable)

 private:
  ~CloseConnectionRunnable() override = default;

  NS_DECL_NSIRUNNABLE
};

struct ConnectionPool::DatabaseInfo final {
  friend class mozilla::DefaultDelete<DatabaseInfo>;

  RefPtr<ConnectionPool> mConnectionPool;
  const nsCString mDatabaseId;
  RefPtr<DatabaseConnection> mConnection;
  nsClassHashtable<nsStringHashKey, TransactionInfoPair> mBlockingTransactions;
  nsTArray<NotNull<TransactionInfo*>> mTransactionsScheduledDuringClose;
  nsTArray<NotNull<TransactionInfo*>> mScheduledWriteTransactions;
  Maybe<TransactionInfo&> mRunningWriteTransaction;
  RefPtr<TaskQueue> mEventTarget;
  uint32_t mReadTransactionCount;
  uint32_t mWriteTransactionCount;
  bool mNeedsCheckpoint;
  bool mIdle;
  FlippedOnce<false> mCloseOnIdle;
  bool mClosing;

#ifdef DEBUG
  nsISerialEventTarget* mDEBUGConnectionEventTarget;
#endif

  DatabaseInfo(ConnectionPool* aConnectionPool, const nsACString& aDatabaseId);

  void AssertIsOnConnectionThread() const {
    MOZ_ASSERT(mDEBUGConnectionEventTarget);
    MOZ_ASSERT(GetCurrentSerialEventTarget() == mDEBUGConnectionEventTarget);
  }

  uint64_t TotalTransactionCount() const {
    return mReadTransactionCount + mWriteTransactionCount;
  }

  nsresult Dispatch(already_AddRefed<nsIRunnable> aRunnable);

 private:
  ~DatabaseInfo();

  DatabaseInfo(const DatabaseInfo&) = delete;
  DatabaseInfo& operator=(const DatabaseInfo&) = delete;
};

struct ConnectionPool::DatabaseCompleteCallback final {
  friend class DefaultDelete<DatabaseCompleteCallback>;

  nsCString mDatabaseId;
  nsCOMPtr<nsIRunnable> mCallback;

  DatabaseCompleteCallback(const nsCString& aDatabaseIds,
                           nsIRunnable* aCallback);

 private:
  ~DatabaseCompleteCallback();
};

class NS_NO_VTABLE ConnectionPool::FinishCallback : public nsIRunnable {
 public:
  // Called on the owning thread before any additional transactions are
  // unblocked.
  virtual void TransactionFinishedBeforeUnblock() = 0;

  // Called on the owning thread after additional transactions may have been
  // unblocked.
  virtual void TransactionFinishedAfterUnblock() = 0;

 protected:
  FinishCallback() = default;

  virtual ~FinishCallback() = default;
};

class ConnectionPool::FinishCallbackWrapper final : public Runnable {
  RefPtr<ConnectionPool> mConnectionPool;
  RefPtr<FinishCallback> mCallback;
  nsCOMPtr<nsIEventTarget> mOwningEventTarget;
  uint64_t mTransactionId;
  bool mHasRunOnce;

 public:
  FinishCallbackWrapper(ConnectionPool* aConnectionPool,
                        uint64_t aTransactionId, FinishCallback* aCallback);

  NS_INLINE_DECL_REFCOUNTING_INHERITED(FinishCallbackWrapper, Runnable)

 private:
  ~FinishCallbackWrapper() override;

  NS_DECL_NSIRUNNABLE
};

#ifdef DEBUG

class ConnectionPool::TransactionRunnable final : public Runnable {
 public:
  explicit TransactionRunnable(nsCOMPtr<nsIRunnable> aRunnable);

 private:
  NS_DECL_NSIRUNNABLE

  nsCOMPtr<nsIRunnable> mRunnable;
};

#endif

class ConnectionPool::TransactionInfo final {
  friend class mozilla::DefaultDelete<TransactionInfo>;

  nsTHashSet<TransactionInfo*> mBlocking;
  nsTArray<NotNull<TransactionInfo*>> mBlockingOrdered;

 public:
  DatabaseInfo& mDatabaseInfo;
  const nsID mBackgroundChildLoggingId;
  const nsCString mDatabaseId;
  const uint64_t mTransactionId;
  const int64_t mLoggingSerialNumber;
  const nsTArray<nsString> mObjectStoreNames;
  nsTHashSet<TransactionInfo*> mBlockedOn;
  nsTArray<nsCOMPtr<nsIRunnable>> mQueuedRunnables;
  const bool mIsWriteTransaction;
  bool mRunning;

#ifdef DEBUG
  FlippedOnce<false> mFinished;
#endif

  TransactionInfo(DatabaseInfo& aDatabaseInfo,
                  const nsID& aBackgroundChildLoggingId,
                  const nsACString& aDatabaseId, uint64_t aTransactionId,
                  int64_t aLoggingSerialNumber,
                  const nsTArray<nsString>& aObjectStoreNames,
                  bool aIsWriteTransaction,
                  TransactionDatabaseOperationBase* aTransactionOp);

  void AddBlockingTransaction(TransactionInfo& aTransactionInfo);

  void RemoveBlockingTransactions();

 private:
  ~TransactionInfo();

  void MaybeUnblock(TransactionInfo& aTransactionInfo);
};

struct ConnectionPool::TransactionInfoPair final {
  // Multiple reading transactions can block future writes.
  nsTArray<NotNull<TransactionInfo*>> mLastBlockingWrites;
  // But only a single writing transaction can block future reads.
  Maybe<TransactionInfo&> mLastBlockingReads;

#if defined(DEBUG) || defined(NS_BUILD_REFCNT_LOGGING)
  TransactionInfoPair();
  ~TransactionInfoPair();
#endif
};

/*******************************************************************************
 * Actor class declarations
 ******************************************************************************/

template <IDBCursorType CursorType>
class CommonOpenOpHelper;
template <IDBCursorType CursorType>
class IndexOpenOpHelper;
template <IDBCursorType CursorType>
class ObjectStoreOpenOpHelper;
template <IDBCursorType CursorType>
class OpenOpHelper;

class DatabaseOperationBase : public Runnable,
                              public mozIStorageProgressHandler {
  template <IDBCursorType CursorType>
  friend class OpenOpHelper;

 protected:
  class AutoSetProgressHandler;

  using UniqueIndexTable = nsTHashMap<nsUint64HashKey, bool>;

  const nsCOMPtr<nsIEventTarget> mOwningEventTarget;
  const nsID mBackgroundChildLoggingId;
  const uint64_t mLoggingSerialNumber;

 private:
  nsresult mResultCode = NS_OK;
  Atomic<bool> mOperationMayProceed;
  FlippedOnce<false> mActorDestroyed;

 public:
  NS_DECL_ISUPPORTS_INHERITED

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

  void NoteActorDestroyed() {
    AssertIsOnOwningThread();

    mActorDestroyed.EnsureFlipped();
    mOperationMayProceed = false;
  }

  bool IsActorDestroyed() const {
    AssertIsOnOwningThread();

    return mActorDestroyed;
  }

  // May be called on any thread, but you should call IsActorDestroyed() if
  // you know you're on the background thread because it is slightly faster.
  bool OperationMayProceed() const { return mOperationMayProceed; }

  const nsID& BackgroundChildLoggingId() const {
    return mBackgroundChildLoggingId;
  }

  uint64_t LoggingSerialNumber() const { return mLoggingSerialNumber; }

  nsresult ResultCode() const { return mResultCode; }

  void SetFailureCode(nsresult aFailureCode) {
    MOZ_ASSERT(NS_SUCCEEDED(mResultCode));
    OverrideFailureCode(aFailureCode);
  }

  void SetFailureCodeIfUnset(nsresult aFailureCode) {
    if (NS_SUCCEEDED(mResultCode)) {
      OverrideFailureCode(aFailureCode);
    }
  }

  bool HasFailed() const { return NS_FAILED(mResultCode); }

 protected:
  DatabaseOperationBase(const nsID& aBackgroundChildLoggingId,
                        uint64_t aLoggingSerialNumber)
      : Runnable("dom::indexedDB::DatabaseOperationBase"),
        mOwningEventTarget(GetCurrentSerialEventTarget()),
        mBackgroundChildLoggingId(aBackgroundChildLoggingId),
        mLoggingSerialNumber(aLoggingSerialNumber),
        mOperationMayProceed(true) {
    AssertIsOnOwningThread();
  }

  ~DatabaseOperationBase() override { MOZ_ASSERT(mActorDestroyed); }

  void OverrideFailureCode(nsresult aFailureCode) {
    MOZ_ASSERT(NS_FAILED(aFailureCode));

    mResultCode = aFailureCode;
  }

  static nsAutoCString MaybeGetBindingClauseForKeyRange(
      const Maybe<SerializedKeyRange>& aOptionalKeyRange,
      const nsACString& aKeyColumnName);

  static nsAutoCString GetBindingClauseForKeyRange(
      const SerializedKeyRange& aKeyRange, const nsACString& aKeyColumnName);

  static uint64_t ReinterpretDoubleAsUInt64(double aDouble);

  static nsresult BindKeyRangeToStatement(const SerializedKeyRange& aKeyRange,
                                          mozIStorageStatement* aStatement);

  static nsresult BindKeyRangeToStatement(const SerializedKeyRange& aKeyRange,
                                          mozIStorageStatement* aStatement,
                                          const nsCString& aLocale);

  static Result<IndexDataValuesAutoArray, nsresult>
  IndexDataValuesFromUpdateInfos(const nsTArray<IndexUpdateInfo>& aUpdateInfos,
                                 const UniqueIndexTable& aUniqueIndexTable);

  static nsresult InsertIndexTableRows(
      DatabaseConnection* aConnection, IndexOrObjectStoreId aObjectStoreId,
      const Key& aObjectStoreKey, const nsTArray<IndexDataValue>& aIndexValues);

  static nsresult DeleteIndexDataTableRows(
      DatabaseConnection* aConnection, const Key& aObjectStoreKey,
      const nsTArray<IndexDataValue>& aIndexValues);

  static nsresult DeleteObjectStoreDataTableRowsWithIndexes(
      DatabaseConnection* aConnection, IndexOrObjectStoreId aObjectStoreId,
      const Maybe<SerializedKeyRange>& aKeyRange);

  static nsresult UpdateIndexValues(
      DatabaseConnection* aConnection, IndexOrObjectStoreId aObjectStoreId,
      const Key& aObjectStoreKey, const nsTArray<IndexDataValue>& aIndexValues);

  static Result<bool, nsresult> ObjectStoreHasIndexes(
      DatabaseConnection& aConnection, IndexOrObjectStoreId aObjectStoreId);

 private:
  template <typename KeyTransformation>
  static nsresult MaybeBindKeyToStatement(
      const Key& aKey, mozIStorageStatement* aStatement,
      const nsACString& aParameterName,
      const KeyTransformation& aKeyTransformation);

  template <typename KeyTransformation>
  static nsresult BindTransformedKeyRangeToStatement(
      const SerializedKeyRange& aKeyRange, mozIStorageStatement* aStatement,
      const KeyTransformation& aKeyTransformation);

  // Not to be overridden by subclasses.
  NS_DECL_MOZISTORAGEPROGRESSHANDLER
};

class MOZ_STACK_CLASS DatabaseOperationBase::AutoSetProgressHandler final {
  Maybe<mozIStorageConnection&> mConnection;
#ifdef DEBUG
  DatabaseOperationBase* mDEBUGDatabaseOp;
#endif

 public:
  AutoSetProgressHandler();

  ~AutoSetProgressHandler();

  nsresult Register(mozIStorageConnection& aConnection,
                    DatabaseOperationBase* aDatabaseOp);

  void Unregister();
};

class TransactionDatabaseOperationBase : public DatabaseOperationBase {
  enum class InternalState {
    Initial,
    DatabaseWork,
    SendingPreprocess,
    WaitingForContinue,
    SendingResults,
    Completed
  };

  InitializedOnce<const NotNull<SafeRefPtr<TransactionBase>>> mTransaction;
  // Unique request id within the context of the transaction, allocated by the
  // transaction in the content process starting from 0. Values less than 0 are
  // impossible and forbidden. Used to support the explicit commit() request.
  const int64_t mRequestId;
  InternalState mInternalState = InternalState::Initial;
  bool mWaitingForContinue = false;
  const bool mTransactionIsAborted;

 protected:
  const int64_t mTransactionLoggingSerialNumber;

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
 protected:
  // A check only enables when the diagnostic assert turns on. It assumes the
  // mUpdateRefcountFunction is a nullptr because the previous
  // StartTransactionOp failed on the connection thread and the next write
  // operation (e.g. ObjectstoreAddOrPutRequestOp) doesn't have enough time to
  // catch up the failure information.
  bool mAssumingPreviousOperationFail = false;
#endif

 public:
  void AssertIsOnConnectionThread() const
#ifdef DEBUG
      ;
#else
  {
  }
#endif

  uint64_t StartOnConnectionPool(const nsID& aBackgroundChildLoggingId,
                                 const nsACString& aDatabaseId,
                                 int64_t aLoggingSerialNumber,
                                 const nsTArray<nsString>& aObjectStoreNames,
                                 bool aIsWriteTransaction);

  void DispatchToConnectionPool();

  TransactionBase& Transaction() { return **mTransaction; }

  const TransactionBase& Transaction() const { return **mTransaction; }

  bool IsWaitingForContinue() const {
    AssertIsOnOwningThread();

    return mWaitingForContinue;
  }

  void NoteContinueReceived();

  int64_t TransactionLoggingSerialNumber() const {
    return mTransactionLoggingSerialNumber;
  }

  // May be overridden by subclasses if they need to perform work on the
  // background thread before being dispatched. Returning false will kill the
  // child actors and prevent dispatch.
  virtual bool Init(TransactionBase& aTransaction);

  // This callback will be called on the background thread before releasing the
  // final reference to this request object. Subclasses may perform any
  // additional cleanup here but must always call the base class implementation.
  virtual void Cleanup();

 protected:
  TransactionDatabaseOperationBase(SafeRefPtr<TransactionBase> aTransaction,
                                   int64_t aRequestId);

  TransactionDatabaseOperationBase(SafeRefPtr<TransactionBase> aTransaction,
                                   const int64_t aRequestId,
                                   uint64_t aLoggingSerialNumber);

  ~TransactionDatabaseOperationBase() override;

  virtual void RunOnConnectionThread();

  // Must be overridden in subclasses. Called on the target thread to allow the
  // subclass to perform necessary database or file operations. A successful
  // return value will trigger a SendSuccessResult callback on the background
  // thread while a failure value will trigger a SendFailureResult callback.
  virtual nsresult DoDatabaseWork(DatabaseConnection* aConnection) = 0;

  // May be overriden in subclasses. Called on the background thread to decide
  // if the subclass needs to send any preprocess info to the child actor.
  virtual bool HasPreprocessInfo();

  // May be overriden in subclasses. Called on the background thread to allow
  // the subclass to serialize its preprocess info and send it to the child
  // actor. A successful return value will trigger a wait for a
  // NoteContinueReceived callback on the background thread while a failure
  // value will trigger a SendFailureResult callback.
  virtual nsresult SendPreprocessInfo();

  // Must be overridden in subclasses. Called on the background thread to allow
  // the subclass to serialize its results and send them to the child actor. A
  // failed return value will trigger a SendFailureResult callback.
  virtual nsresult SendSuccessResult() = 0;

  // Must be overridden in subclasses. Called on the background thread to allow
  // the subclass to send its failure code. Returning false will cause the
  // transaction to be aborted with aResultCode. Returning true will not cause
  // the transaction to be aborted.
  virtual bool SendFailureResult(nsresult aResultCode) = 0;

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  auto MakeAutoSavepointCleanupHandler(DatabaseConnection& aConnection) {
    return [this, &aConnection](const auto) {
      if (!aConnection.GetUpdateRefcountFunction()) {
        mAssumingPreviousOperationFail = true;
      }
    };
  }
#endif

 private:
  void SendToConnectionPool();

  void SendPreprocess();

  void SendResults();

  void SendPreprocessInfoOrResults(bool aSendPreprocessInfo);

  // Not to be overridden by subclasses.
  NS_DECL_NSIRUNNABLE
};

class Factory final : public PBackgroundIDBFactoryParent,
                      public AtomicSafeRefCounted<Factory> {
  nsCString mSystemLocale;
  RefPtr<DatabaseLoggingInfo> mLoggingInfo;

#ifdef DEBUG
  bool mActorDestroyed;
#endif

  // Reference counted.
  ~Factory() override;

 public:
  [[nodiscard]] static SafeRefPtr<Factory> Create(
      const LoggingInfo& aLoggingInfo, const nsACString& aSystemLocale);

  DatabaseLoggingInfo* GetLoggingInfo() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mLoggingInfo);

    return mLoggingInfo;
  }

  const nsCString& GetSystemLocale() const { return mSystemLocale; }

  MOZ_DECLARE_REFCOUNTED_TYPENAME(mozilla::dom::indexedDB::Factory)
  MOZ_INLINE_DECL_SAFEREFCOUNTING_INHERITED(Factory, AtomicSafeRefCounted)

  // Only constructed in Create().
  Factory(RefPtr<DatabaseLoggingInfo> aLoggingInfo,
          const nsACString& aSystemLocale);

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  PBackgroundIDBFactoryRequestParent* AllocPBackgroundIDBFactoryRequestParent(
      const FactoryRequestParams& aParams) override;

  mozilla::ipc::IPCResult RecvPBackgroundIDBFactoryRequestConstructor(
      PBackgroundIDBFactoryRequestParent* aActor,
      const FactoryRequestParams& aParams) override;

  bool DeallocPBackgroundIDBFactoryRequestParent(
      PBackgroundIDBFactoryRequestParent* aActor) override;

  mozilla::ipc::IPCResult RecvGetDatabases(
      const PersistenceType& aPersistenceType,
      const PrincipalInfo& aPrincipalInfo,
      GetDatabasesResolver&& aResolve) override;

 private:
  Maybe<ContentParentId> GetContentParentId() const;
};

class WaitForTransactionsHelper final : public Runnable {
  const nsCString mDatabaseId;
  nsCOMPtr<nsIRunnable> mCallback;

  enum class State { Initial = 0, WaitingForTransactions, Complete } mState;

 public:
  WaitForTransactionsHelper(const nsACString& aDatabaseId,
                            nsIRunnable* aCallback)
      : Runnable("dom::indexedDB::WaitForTransactionsHelper"),
        mDatabaseId(aDatabaseId),
        mCallback(aCallback),
        mState(State::Initial) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(!aDatabaseId.IsEmpty());
    MOZ_ASSERT(aCallback);
  }

  void WaitForTransactions();

  NS_INLINE_DECL_REFCOUNTING_INHERITED(WaitForTransactionsHelper, Runnable)

 private:
  ~WaitForTransactionsHelper() override {
    MOZ_ASSERT(!mCallback);
    MOZ_ASSERT(mState == State::Complete);
  }

  void MaybeWaitForTransactions();

  void CallCallback();

  NS_DECL_NSIRUNNABLE
};

class Database final
    : public PBackgroundIDBDatabaseParent,
      public SupportsCheckedUnsafePtr<CheckIf<DiagnosticAssertEnabled>>,
      public AtomicSafeRefCounted<Database> {
  friend class VersionChangeTransaction;

  class StartTransactionOp;
  class UnmapBlobCallback;

 private:
  SafeRefPtr<Factory> mFactory;
  SafeRefPtr<FullDatabaseMetadata> mMetadata;
  SafeRefPtr<DatabaseFileManager> mFileManager;
  ClientDirectoryLockHandle mDirectoryLockHandle;
  nsTHashSet<TransactionBase*> mTransactions;
  nsTHashMap<nsIDHashKey, SafeRefPtr<DatabaseFileInfo>> mMappedBlobs;
  RefPtr<DatabaseConnection> mConnection;
  const PrincipalInfo mPrincipalInfo;
  const Maybe<ContentParentId> mOptionalContentParentId;
  // XXX Consider changing this to ClientMetadata.
  const quota::OriginMetadata mOriginMetadata;
  const nsCString mId;
  const nsString mFilePath;
  const Maybe<const CipherKey> mKey;
  int64_t mDirectoryLockId;
  const uint32_t mTelemetryId;
  const PersistenceType mPersistenceType;
  const bool mInPrivateBrowsing;
  FlippedOnce<false> mClosed;
  FlippedOnce<false> mInvalidated;
  FlippedOnce<false> mActorWasAlive;
  FlippedOnce<false> mActorDestroyed;
  nsCOMPtr<nsIEventTarget> mBackgroundThread;
#ifdef DEBUG
  bool mAllBlobsUnmapped;
#endif

 public:
  // Created by OpenDatabaseOp.
  Database(SafeRefPtr<Factory> aFactory, const PrincipalInfo& aPrincipalInfo,
           const Maybe<ContentParentId>& aOptionalContentParentId,
           const quota::OriginMetadata& aOriginMetadata, uint32_t aTelemetryId,
           SafeRefPtr<FullDatabaseMetadata> aMetadata,
           SafeRefPtr<DatabaseFileManager> aFileManager,
           ClientDirectoryLockHandle aDirectoryLockHandle,
           bool aInPrivateBrowsing, const Maybe<const CipherKey>& aMaybeKey);

  void AssertIsOnConnectionThread() const {
#ifdef DEBUG
    // mConnection is used to cache the result from ConnectionPool's
    // GetOrCreateConnection method (potentially avoiding a lock and a hash
    // lookup). However, once the connection is closed, the task queue for the
    // given database is also destroyed, so the connection, which caches the
    // event target it was created on, is no longer reliable for asserting that
    // the current thread is the connection thread (mConnection might be reset
    // when EnsureConnection is called again, but in the meantime, we have to
    // fallback to just checking the main thread and the PBackgroud thread).
    if (mConnection && !mConnection->Closed()) {
      mConnection->AssertIsOnConnectionThread();
    } else {
      MOZ_ASSERT(!NS_IsMainThread());
      MOZ_ASSERT(!IsOnBackgroundThread());
      MOZ_ASSERT(mInvalidated);
    }
#endif
  }

  NS_IMETHOD_(MozExternalRefCountType) AddRef() override {
    return AtomicSafeRefCounted<Database>::AddRef();
  }
  NS_IMETHOD_(MozExternalRefCountType) Release() override {
    return AtomicSafeRefCounted<Database>::Release();
  }

  MOZ_DECLARE_REFCOUNTED_TYPENAME(mozilla::dom::indexedDB::Database)

  void Invalidate();

  bool IsOwnedByProcess(ContentParentId aContentParentId) const {
    return mOptionalContentParentId &&
           mOptionalContentParentId.value() == aContentParentId;
  }

  const quota::OriginMetadata& OriginMetadata() const {
    return mOriginMetadata;
  }

  const nsCString& Id() const { return mId; }

  Maybe<ClientDirectoryLock&> MaybeDirectoryLockRef() const {
    AssertIsOnBackgroundThread();

    return ToMaybeRef(mDirectoryLockHandle.get());
  }

  int64_t DirectoryLockId() const { return mDirectoryLockId; }

  uint32_t TelemetryId() const { return mTelemetryId; }

  PersistenceType Type() const { return mPersistenceType; }

  const nsString& FilePath() const { return mFilePath; }

  DatabaseFileManager& GetFileManager() const { return *mFileManager; }

  MovingNotNull<SafeRefPtr<DatabaseFileManager>> GetFileManagerPtr() const {
    return WrapMovingNotNull(mFileManager.clonePtr());
  }

  const FullDatabaseMetadata& Metadata() const {
    MOZ_ASSERT(mMetadata);
    return *mMetadata;
  }

  SafeRefPtr<FullDatabaseMetadata> MetadataPtr() const {
    MOZ_ASSERT(mMetadata);
    return mMetadata.clonePtr();
  }

  PBackgroundParent* GetBackgroundParent() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(!IsActorDestroyed());

    return Manager()->Manager();
  }

  DatabaseLoggingInfo* GetLoggingInfo() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mFactory);

    return mFactory->GetLoggingInfo();
  }

  bool RegisterTransaction(TransactionBase& aTransaction);

  void UnregisterTransaction(TransactionBase& aTransaction);

  void SetActorAlive();

  void MapBlob(const IPCBlob& aIPCBlob, SafeRefPtr<DatabaseFileInfo> aFileInfo);

  bool IsActorAlive() const {
    AssertIsOnBackgroundThread();

    return mActorWasAlive && !mActorDestroyed;
  }

  bool IsActorDestroyed() const {
    AssertIsOnBackgroundThread();

    return mActorWasAlive && mActorDestroyed;
  }

  bool IsClosed() const {
    AssertIsOnBackgroundThread();

    return mClosed;
  }

  bool IsInvalidated() const {
    AssertIsOnBackgroundThread();

    return mInvalidated;
  }

  nsresult EnsureConnection();

  DatabaseConnection* GetConnection() const {
#ifdef DEBUG
    if (mConnection) {
      mConnection->AssertIsOnConnectionThread();
    }
#endif

    return mConnection;
  }

  void Stringify(nsACString& aResult) const;

  bool IsInPrivateBrowsing() const {
    AssertIsOnBackgroundThread();
    return mInPrivateBrowsing;
  }

  const Maybe<const CipherKey>& MaybeKeyRef() const {
    // This can be called on any thread, as it is const.
    MOZ_ASSERT(mKey.isSome() == mInPrivateBrowsing);
    return mKey;
  }

  ~Database() override {
    MOZ_ASSERT(mClosed);
    MOZ_ASSERT_IF(mActorWasAlive, mActorDestroyed);

    NS_ProxyRelease("ReleaseIDBFactory", mBackgroundThread.get(),
                    mFactory.forget());
  }

 private:
  [[nodiscard]] SafeRefPtr<DatabaseFileInfo> GetBlob(const IPCBlob& aIPCBlob);

  void UnmapBlob(const nsID& aID);

  void UnmapAllBlobs();

  bool CloseInternal();

  void MaybeCloseConnection();

  void ConnectionClosedCallback();

  void CleanupMetadata();

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  PBackgroundIDBDatabaseFileParent* AllocPBackgroundIDBDatabaseFileParent(
      const IPCBlob& aIPCBlob) override;

  bool DeallocPBackgroundIDBDatabaseFileParent(
      PBackgroundIDBDatabaseFileParent* aActor) override;

  already_AddRefed<PBackgroundIDBTransactionParent>
  AllocPBackgroundIDBTransactionParent(
      const nsTArray<nsString>& aObjectStoreNames, const Mode& aMode,
      const Durability& aDurability) override;

  mozilla::ipc::IPCResult RecvPBackgroundIDBTransactionConstructor(
      PBackgroundIDBTransactionParent* aActor,
      nsTArray<nsString>&& aObjectStoreNames, const Mode& aMode,
      const Durability& aDurability) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvBlocked() override;

  mozilla::ipc::IPCResult RecvClose() override;

  template <typename T>
  static bool InvalidateAll(const nsTBaseHashSet<nsPtrHashKey<T>>& aTable);
};

class Database::StartTransactionOp final
    : public TransactionDatabaseOperationBase {
  friend class Database;

 private:
  explicit StartTransactionOp(SafeRefPtr<TransactionBase> aTransaction)
      : TransactionDatabaseOperationBase(std::move(aTransaction),
                                         /* aRequestId */ 0,
                                         /* aLoggingSerialNumber */ 0) {}

  ~StartTransactionOp() override = default;

  void RunOnConnectionThread() override;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  nsresult SendSuccessResult() override;

  bool SendFailureResult(nsresult aResultCode) override;

  void Cleanup() override;
};

class Database::UnmapBlobCallback final
    : public RemoteLazyInputStreamParentCallback {
  SafeRefPtr<Database> mDatabase;
  nsCOMPtr<nsISerialEventTarget> mBackgroundThread;

 public:
  explicit UnmapBlobCallback(SafeRefPtr<Database> aDatabase)
      : mDatabase(std::move(aDatabase)),
        mBackgroundThread(GetCurrentSerialEventTarget()) {
    AssertIsOnBackgroundThread();
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(Database::UnmapBlobCallback, override)

  void ActorDestroyed(const nsID& aID) override {
    MOZ_ASSERT(mDatabase);
    mBackgroundThread->Dispatch(NS_NewRunnableFunction(
        "UnmapBlobCallback", [aID, database = std::move(mDatabase)] {
          AssertIsOnBackgroundThread();
          database->UnmapBlob(aID);
        }));
  }

 private:
  ~UnmapBlobCallback() = default;
};

/**
 * In coordination with IDBDatabase's mFileActors weak-map on the child side, a
 * long-lived mapping from a child process's live Blobs to their corresponding
 * DatabaseFileInfo in our owning database.  Assists in avoiding redundant IPC
 * traffic and disk storage.  This includes both:
 * - Blobs retrieved from this database and sent to the child that do not need
 *   to be written to disk because they already exist on disk in this database's
 *   files directory.
 * - Blobs retrieved from other databases or from anywhere else that will need
 *   to be written to this database's files directory.  In this case we will
 *   hold a reference to its BlobImpl in mBlobImpl until we have successfully
 *   written the Blob to disk.
 *
 * Relevant Blob context: Blobs sent from the parent process to child processes
 * are automatically linked back to their source BlobImpl when the child process
 * references the Blob via IPC. This is done using the internal IPCBlob
 * inputStream actor ID to DatabaseFileInfo mapping. However, when getting an
 * actor in the child process for sending an in-child-created Blob to the
 * parent process, there is (currently) no Blob machinery to automatically
 * establish and reuse a long-lived Actor.  As a result, without IDB's weak-map
 * cleverness, a memory-backed Blob repeatedly sent from the child to the parent
 * would appear as a different Blob each time, requiring the Blob data to be
 * sent over IPC each time as well as potentially needing to be written to disk
 * each time.
 *
 * This object remains alive as long as there is an active child actor or an
 * ObjectStoreAddOrPutRequestOp::StoredFileInfo for a queued or active add/put
 * op is holding a reference to us.
 */
class DatabaseFile final : public PBackgroundIDBDatabaseFileParent {
  // mBlobImpl's ownership lifecycle:
  // - Initialized on the background thread at creation time.  Then
  //   responsibility is handed off to the connection thread.
  // - Checked and used by the connection thread to generate a stream to write
  //   the blob to disk by an add/put operation.
  // - Cleared on the connection thread once the file has successfully been
  //   written to disk.
  InitializedOnce<const RefPtr<BlobImpl>> mBlobImpl;
  const SafeRefPtr<DatabaseFileInfo> mFileInfo;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(mozilla::dom::indexedDB::DatabaseFile);

  const DatabaseFileInfo& GetFileInfo() const {
    AssertIsOnBackgroundThread();

    return *mFileInfo;
  }

  SafeRefPtr<DatabaseFileInfo> GetFileInfoPtr() const {
    AssertIsOnBackgroundThread();

    return mFileInfo.clonePtr();
  }

  /**
   * If mBlobImpl is non-null (implying the contents of this file have not yet
   * been written to disk), then return an input stream. Otherwise, if mBlobImpl
   * is null (because the contents have been written to disk), returns null.
   */
  [[nodiscard]] nsCOMPtr<nsIInputStream> GetInputStream(ErrorResult& rv) const;

  /**
   * To be called upon successful copying of the stream GetInputStream()
   * returned so that we won't try and redundantly write the file to disk in the
   * future.  This is a separate step from GetInputStream() because
   * the write could fail due to quota errors that happen now but that might
   * not happen in a future attempt.
   */
  void WriteSucceededClearBlobImpl() {
    MOZ_ASSERT(!IsOnBackgroundThread());

    MOZ_ASSERT(*mBlobImpl);
    mBlobImpl.destroy();
  }

 public:
  // Called when sending to the child.
  explicit DatabaseFile(SafeRefPtr<DatabaseFileInfo> aFileInfo)
      : mBlobImpl{nullptr}, mFileInfo(std::move(aFileInfo)) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mFileInfo);
  }

  // Called when receiving from the child.
  DatabaseFile(RefPtr<BlobImpl> aBlobImpl,
               SafeRefPtr<DatabaseFileInfo> aFileInfo)
      : mBlobImpl(std::move(aBlobImpl)), mFileInfo(std::move(aFileInfo)) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(*mBlobImpl);
    MOZ_ASSERT(mFileInfo);
  }

 private:
  ~DatabaseFile() override = default;

  void ActorDestroy(ActorDestroyReason aWhy) override {
    AssertIsOnBackgroundThread();
  }
};

nsCOMPtr<nsIInputStream> DatabaseFile::GetInputStream(ErrorResult& rv) const {
  // We should only be called from our DB connection thread, not the background
  // thread.
  MOZ_ASSERT(!IsOnBackgroundThread());

  // If we were constructed without a BlobImpl, or WriteSucceededClearBlobImpl
  // was already called, return nullptr.
  if (!mBlobImpl || !*mBlobImpl) {
    return nullptr;
  }

  nsCOMPtr<nsIInputStream> inputStream;
  (*mBlobImpl)->CreateInputStream(getter_AddRefs(inputStream), rv);
  if (rv.Failed()) {
    return nullptr;
  }

  return inputStream;
}

class TransactionBase : public AtomicSafeRefCounted<TransactionBase> {
  friend class CursorBase;

  template <IDBCursorType CursorType>
  friend class Cursor;

  class CommitOp;

 protected:
  using Mode = IDBTransaction::Mode;
  using Durability = IDBTransaction::Durability;

 private:
  const SafeRefPtr<Database> mDatabase;
  nsTArray<SafeRefPtr<FullObjectStoreMetadata>>
      mModifiedAutoIncrementObjectStoreMetadataArray;
  LazyInitializedOnceNotNull<const uint64_t> mTransactionId;
  const nsCString mDatabaseId;
  const int64_t mLoggingSerialNumber;
  uint64_t mActiveRequestCount;
  Atomic<bool> mInvalidatedOnAnyThread;
  const Mode mMode;
  const Durability mDurability;
  FlippedOnce<false> mInitialized;
  FlippedOnce<false> mHasBeenActiveOnConnectionThread;
  FlippedOnce<false> mActorDestroyed;
  FlippedOnce<false> mInvalidated;

 protected:
  nsresult mResultCode;
  FlippedOnce<false> mCommitOrAbortReceived;
  FlippedOnce<false> mCommittedOrAborted;
  FlippedOnce<false> mForceAborted;
  LazyInitializedOnce<const Maybe<int64_t>> mLastRequestBeforeCommit;
  Maybe<int64_t> mLastFailedRequest;

 public:
  void AssertIsOnConnectionThread() const {
    MOZ_ASSERT(mDatabase);
    mDatabase->AssertIsOnConnectionThread();
  }

  bool IsActorDestroyed() const {
    AssertIsOnBackgroundThread();

    return mActorDestroyed;
  }

  // Must be called on the background thread.
  bool IsInvalidated() const {
    MOZ_ASSERT(IsOnBackgroundThread(), "Use IsInvalidatedOnAnyThread()");
    MOZ_ASSERT_IF(mInvalidated, NS_FAILED(mResultCode));

    return mInvalidated;
  }

  // May be called on any thread, but is more expensive than IsInvalidated().
  bool IsInvalidatedOnAnyThread() const { return mInvalidatedOnAnyThread; }

  void Init(const uint64_t aTransactionId) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(aTransactionId);

    mTransactionId.init(aTransactionId);
    mInitialized.Flip();
  }

  void SetActiveOnConnectionThread() {
    AssertIsOnConnectionThread();
    mHasBeenActiveOnConnectionThread.Flip();
  }

  MOZ_DECLARE_REFCOUNTED_TYPENAME(mozilla::dom::indexedDB::TransactionBase)

  void Abort(nsresult aResultCode, bool aForce);

  uint64_t TransactionId() const { return *mTransactionId; }

  const nsACString& DatabaseId() const { return mDatabaseId; }

  Mode GetMode() const { return mMode; }

  Durability GetDurability() const { return mDurability; }

  const Database& GetDatabase() const {
    MOZ_ASSERT(mDatabase);

    return *mDatabase;
  }

  Database& GetMutableDatabase() const {
    MOZ_ASSERT(mDatabase);

    return *mDatabase;
  }

  SafeRefPtr<Database> GetDatabasePtr() const {
    MOZ_ASSERT(mDatabase);

    return mDatabase.clonePtr();
  }

  DatabaseLoggingInfo* GetLoggingInfo() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mDatabase);

    return mDatabase->GetLoggingInfo();
  }

  int64_t LoggingSerialNumber() const { return mLoggingSerialNumber; }

  bool IsAborted() const {
    AssertIsOnBackgroundThread();

    return NS_FAILED(mResultCode);
  }

  [[nodiscard]] SafeRefPtr<FullObjectStoreMetadata> GetMetadataForObjectStoreId(
      IndexOrObjectStoreId aObjectStoreId) const;

  [[nodiscard]] SafeRefPtr<FullIndexMetadata> GetMetadataForIndexId(
      FullObjectStoreMetadata& aObjectStoreMetadata,
      IndexOrObjectStoreId aIndexId) const;

  PBackgroundParent* GetBackgroundParent() const {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(!IsActorDestroyed());

    return GetDatabase().GetBackgroundParent();
  }

  void NoteModifiedAutoIncrementObjectStore(
      const SafeRefPtr<FullObjectStoreMetadata>& aMetadata);

  void ForgetModifiedAutoIncrementObjectStore(
      FullObjectStoreMetadata& aMetadata);

  void NoteActiveRequest();

  void NoteFinishedRequest(int64_t aRequestId, nsresult aResultCode);

  void Invalidate();

  virtual ~TransactionBase();

 protected:
  TransactionBase(SafeRefPtr<Database> aDatabase, Mode aMode,
                  Durability aDurability);

  void NoteActorDestroyed() {
    AssertIsOnBackgroundThread();

    mActorDestroyed.Flip();
  }

#ifdef DEBUG
  // Only called by VersionChangeTransaction.
  void FakeActorDestroyed() { mActorDestroyed.EnsureFlipped(); }
#endif

  mozilla::ipc::IPCResult RecvCommit(IProtocol* aActor,
                                     const Maybe<int64_t> aLastRequest);

  mozilla::ipc::IPCResult RecvAbort(IProtocol* aActor, nsresult aResultCode);

  void MaybeCommitOrAbort() {
    AssertIsOnBackgroundThread();

    // If we've already committed or aborted then there's nothing else to do.
    if (mCommittedOrAborted) {
      return;
    }

    // If there are active requests then we have to wait for those requests to
    // complete (see NoteFinishedRequest).
    if (mActiveRequestCount) {
      return;
    }

    // If we haven't yet received a commit or abort message then there could be
    // additional requests coming so we should wait unless we're being forced to
    // abort.
    if (!mCommitOrAbortReceived && !mForceAborted) {
      return;
    }

    CommitOrAbort();
  }

  PBackgroundIDBRequestParent* AllocRequest(const int64_t aRequestId,
                                            RequestParams&& aParams,
                                            bool aTrustParams);

  bool StartRequest(PBackgroundIDBRequestParent* aActor);

  bool DeallocRequest(PBackgroundIDBRequestParent* aActor);

  already_AddRefed<PBackgroundIDBCursorParent> AllocCursor(
      const OpenCursorParams& aParams, bool aTrustParams);

  bool StartCursor(PBackgroundIDBCursorParent* aActor, const int64_t aRequestId,
                   const OpenCursorParams& aParams);

  virtual void UpdateMetadata(nsresult aResult) {}

  virtual void SendCompleteNotification(nsresult aResult) = 0;

 private:
  bool VerifyRequestParams(const RequestParams& aParams) const;

  bool VerifyRequestParams(const SerializedKeyRange& aParams) const;

  bool VerifyRequestParams(const ObjectStoreAddPutParams& aParams) const;

  bool VerifyRequestParams(const Maybe<SerializedKeyRange>& aParams) const;

  void CommitOrAbort();
};

class TransactionBase::CommitOp final : public DatabaseOperationBase,
                                        public ConnectionPool::FinishCallback {
  friend class TransactionBase;

  SafeRefPtr<TransactionBase> mTransaction;
  nsresult mResultCode;  ///< TODO: There is also a mResultCode in
                         ///< DatabaseOperationBase. Is there a reason not to
                         ///< use that? At least a more specific name should be
                         ///< given to this one.

 private:
  CommitOp(SafeRefPtr<TransactionBase> aTransaction, nsresult aResultCode);

  ~CommitOp() override = default;

  // Writes new autoIncrement counts to database.
  nsresult WriteAutoIncrementCounts();

  // Updates counts after a database activity has finished.
  void CommitOrRollbackAutoIncrementCounts();

  void AssertForeignKeyConsistency(DatabaseConnection* aConnection)
#ifdef DEBUG
      ;
#else
  {
  }
#endif

  NS_DECL_NSIRUNNABLE

  void TransactionFinishedBeforeUnblock() override;

  void TransactionFinishedAfterUnblock() override;

 public:
  // We need to declare all of nsISupports, because FinishCallback has
  // a pure-virtual nsISupports declaration.
  NS_DECL_ISUPPORTS_INHERITED
};

class NormalTransaction final : public TransactionBase,
                                public PBackgroundIDBTransactionParent {
  nsTArray<SafeRefPtr<FullObjectStoreMetadata>> mObjectStores;

  // Reference counted.
  ~NormalTransaction() override = default;

  bool IsSameProcessActor();

  // Only called by TransactionBase.
  void SendCompleteNotification(nsresult aResult) override;

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvCommit(
      const Maybe<int64_t>& aLastRequest) override;

  mozilla::ipc::IPCResult RecvAbort(const nsresult& aResultCode) override;

  PBackgroundIDBRequestParent* AllocPBackgroundIDBRequestParent(
      const int64_t& aRequestId, const RequestParams& aParams) override;

  mozilla::ipc::IPCResult RecvPBackgroundIDBRequestConstructor(
      PBackgroundIDBRequestParent* aActor, const int64_t& aRequestId,
      const RequestParams& aParams) override;

  bool DeallocPBackgroundIDBRequestParent(
      PBackgroundIDBRequestParent* aActor) override;

  already_AddRefed<PBackgroundIDBCursorParent> AllocPBackgroundIDBCursorParent(
      const int64_t& aRequestId, const OpenCursorParams& aParams) override;

  mozilla::ipc::IPCResult RecvPBackgroundIDBCursorConstructor(
      PBackgroundIDBCursorParent* aActor, const int64_t& aRequestId,
      const OpenCursorParams& aParams) override;

 public:
  // This constructor is only called by Database.
  NormalTransaction(
      SafeRefPtr<Database> aDatabase, TransactionBase::Mode aMode,
      TransactionBase::Durability aDurability,
      nsTArray<SafeRefPtr<FullObjectStoreMetadata>>&& aObjectStores);

  MOZ_INLINE_DECL_SAFEREFCOUNTING_INHERITED(NormalTransaction, TransactionBase)
};

class VersionChangeTransaction final
    : public TransactionBase,
      public PBackgroundIDBVersionChangeTransactionParent {
  friend class OpenDatabaseOp;

  RefPtr<OpenDatabaseOp> mOpenDatabaseOp;
  SafeRefPtr<FullDatabaseMetadata> mOldMetadata;

  FlippedOnce<false> mActorWasAlive;

 public:
  // Only called by OpenDatabaseOp.
  explicit VersionChangeTransaction(OpenDatabaseOp* aOpenDatabaseOp);

  MOZ_INLINE_DECL_SAFEREFCOUNTING_INHERITED(VersionChangeTransaction,
                                            TransactionBase)

 private:
  // Reference counted.
  ~VersionChangeTransaction() override;

  bool IsSameProcessActor();

  // Only called by OpenDatabaseOp.
  bool CopyDatabaseMetadata();

  void SetActorAlive();

  // Only called by TransactionBase.
  void UpdateMetadata(nsresult aResult) override;

  // Only called by TransactionBase.
  void SendCompleteNotification(nsresult aResult) override;

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvCommit(
      const Maybe<int64_t>& aLastRequest) override;

  mozilla::ipc::IPCResult RecvAbort(const nsresult& aResultCode) override;

  mozilla::ipc::IPCResult RecvCreateObjectStore(
      const ObjectStoreMetadata& aMetadata) override;

  mozilla::ipc::IPCResult RecvDeleteObjectStore(
      const IndexOrObjectStoreId& aObjectStoreId) override;

  mozilla::ipc::IPCResult RecvRenameObjectStore(
      const IndexOrObjectStoreId& aObjectStoreId,
      const nsAString& aName) override;

  mozilla::ipc::IPCResult RecvCreateIndex(
      const IndexOrObjectStoreId& aObjectStoreId,
      const IndexMetadata& aMetadata) override;

  mozilla::ipc::IPCResult RecvDeleteIndex(
      const IndexOrObjectStoreId& aObjectStoreId,
      const IndexOrObjectStoreId& aIndexId) override;

  mozilla::ipc::IPCResult RecvRenameIndex(
      const IndexOrObjectStoreId& aObjectStoreId,
      const IndexOrObjectStoreId& aIndexId, const nsAString& aName) override;

  PBackgroundIDBRequestParent* AllocPBackgroundIDBRequestParent(
      const int64_t& aRequestId, const RequestParams& aParams) override;

  mozilla::ipc::IPCResult RecvPBackgroundIDBRequestConstructor(
      PBackgroundIDBRequestParent* aActor, const int64_t& aRequestId,
      const RequestParams& aParams) override;

  bool DeallocPBackgroundIDBRequestParent(
      PBackgroundIDBRequestParent* aActor) override;

  already_AddRefed<PBackgroundIDBCursorParent> AllocPBackgroundIDBCursorParent(
      const int64_t& aRequestId, const OpenCursorParams& aParams) override;

  mozilla::ipc::IPCResult RecvPBackgroundIDBCursorConstructor(
      PBackgroundIDBCursorParent* aActor, const int64_t& aRequestId,
      const OpenCursorParams& aParams) override;
};

class FactoryOp
    : public DatabaseOperationBase,
      public SupportsCheckedUnsafePtr<CheckIf<DiagnosticAssertEnabled>> {
 public:
  struct MaybeBlockedDatabaseInfo final {
    SafeRefPtr<Database> mDatabase;
    bool mBlocked;

    MaybeBlockedDatabaseInfo(MaybeBlockedDatabaseInfo&&) = default;
    MaybeBlockedDatabaseInfo& operator=(MaybeBlockedDatabaseInfo&&) = default;

    MOZ_IMPLICIT MaybeBlockedDatabaseInfo(SafeRefPtr<Database> aDatabase)
        : mDatabase(std::move(aDatabase)), mBlocked(false) {
      MOZ_ASSERT(mDatabase);

      MOZ_COUNT_CTOR(FactoryOp::MaybeBlockedDatabaseInfo);
    }

    ~MaybeBlockedDatabaseInfo() {
      MOZ_COUNT_DTOR(FactoryOp::MaybeBlockedDatabaseInfo);
    }

    bool operator==(const Database* aOther) const {
      return mDatabase == aOther;
    }

    Database* operator->() const& MOZ_NO_ADDREF_RELEASE_ON_RETURN {
      return mDatabase.unsafeGetRawPtr();
    }
  };

 protected:
  enum class State {
    // Just created on the PBackground thread, dispatched to the current thread.
    // Next step is either SendingResults if opening initialization failed, or
    // DirectoryOpenPending if the opening initialization succeeded.
    Initial,

    // Waiting for directory open allowed on the PBackground thread. The next
    // step is either SendingResults if directory lock failed to acquire, or
    // DirectoryWorkOpen if the factory operation is not tied up to a specific
    // database, or DatabaseOpenPending otherwise.
    DirectoryOpenPending,

    // Waiting to do/doing directory work on the QuotaManager IO thread. Its
    // next step is DirectoryWorkDone if directory work was successful or
    // SendingResults if directory work failed.
    DirectoryWorkOpen,

    // Checking if database work can be started. If the database is not blocked
    // by other factory operations then the next step is DatabaseWorkOpen.
    // Otherwise the next step is DatabaseOpenPending.
    DirectoryWorkDone,

    // Waiting for database open allowed on the PBackground thread. The next
    // step is DatabaseWorkOpen.
    DatabaseOpenPending,

    // Waiting to do/doing work on the QuotaManager IO thread. Its next step is
    // either BeginVersionChange if the requested version doesn't match the
    // existing database version or SendingResults if the versions match.
    DatabaseWorkOpen,

    // Starting a version change transaction or deleting a database on the
    // PBackground thread. We need to notify other databases that a version
    // change is about to happen, and maybe tell the request that a version
    // change has been blocked. If databases are notified then the next step is
    // WaitingForOtherDatabasesToClose. Otherwise the next step is
    // WaitingForTransactionsToComplete.
    BeginVersionChange,

    // Waiting for other databases to close on the PBackground thread. This
    // state may persist until all databases are closed. The next state is
    // WaitingForTransactionsToComplete.
    WaitingForOtherDatabasesToClose,

    // Waiting for all transactions that could interfere with this operation to
    // complete on the PBackground thread. Next state is
    // DatabaseWorkVersionChange.
    WaitingForTransactionsToComplete,

    // Waiting to do/doing work on the "work thread". This involves waiting for
    // the VersionChangeOp (OpenDatabaseOp and DeleteDatabaseOp each have a
    // different implementation) to do its work. If the VersionChangeOp is
    // OpenDatabaseOp and it succeeded then the next state is
    // DatabaseWorkVersionUpdate. Otherwise the next step is SendingResults.
    DatabaseWorkVersionChange,

    // Waiting to do/doing finalization work on the QuotaManager IO thread.
    // Eventually the state will transition to SendingResults.
    DatabaseWorkVersionUpdate,

    // Waiting to send/sending results on the PBackground thread. Next step is
    // Completed.
    SendingResults,

    // All done.
    Completed
  };

  // Must be released on the background thread!
  SafeRefPtr<Factory> mFactory;

  Maybe<ContentParentId> mContentParentId;

  // Must be released on the main thread!
  ClientDirectoryLockHandle mDirectoryLockHandle;

  nsTArray<NotNull<RefPtr<FactoryOp>>> mBlocking;
  nsTHashSet<RefPtr<FactoryOp>> mBlockedOn;

  nsTArray<MaybeBlockedDatabaseInfo> mMaybeBlockedDatabases;

  const PrincipalInfo mPrincipalInfo;
  OriginMetadata mOriginMetadata;
  Maybe<nsString> mDatabaseName;
  Maybe<nsCString> mDatabaseId;
  Maybe<nsString> mDatabaseFilePath;
  int64_t mDirectoryLockId;
  const PersistenceType mPersistenceType;
  State mState;
  bool mWaitingForPermissionRetry;
  bool mEnforcingQuota;
  const bool mDeleting;
  FlippedOnce<false> mInPrivateBrowsing;

 public:
  const nsACString& Origin() const {
    AssertIsOnOwningThread();

    return mOriginMetadata.mOrigin;
  }

  const Maybe<nsString>& DatabaseNameRef() const {
    AssertIsOnOwningThread();

    return mDatabaseName;
  }

  bool DatabaseFilePathIsKnown() const {
    AssertIsOnOwningThread();

    return mDatabaseFilePath.isSome();
  }

  const nsAString& DatabaseFilePath() const {
    AssertIsOnOwningThread();
    MOZ_ASSERT(mDatabaseFilePath);

    return mDatabaseFilePath.ref();
  }

  nsresult DispatchThisAfterProcessingCurrentEvent(
      nsCOMPtr<nsIEventTarget> aEventTarget);

  void NoteDatabaseBlocked(Database* aDatabase);

  void NoteDatabaseClosed(Database* aDatabase);

#ifdef DEBUG
  bool HasBlockedDatabases() const { return !mMaybeBlockedDatabases.IsEmpty(); }
#endif

  void StringifyState(nsACString& aResult) const;

  void Stringify(nsACString& aResult) const;

 protected:
  FactoryOp(SafeRefPtr<Factory> aFactory,
            const Maybe<ContentParentId>& aContentParentId,
            const PersistenceType aPersistenceType,
            const PrincipalInfo& aPrincipalInfo,
            const Maybe<nsString>& aDatabaseName, bool aDeleting);

  ~FactoryOp() override {
    // Normally this would be out-of-line since it is a virtual function but
    // MSVC 2010 fails to link for some reason if it is not inlined here...
    MOZ_ASSERT_IF(OperationMayProceed(),
                  mState == State::Initial || mState == State::Completed);
  }

  nsresult Open();

  nsresult DirectoryOpen();

  nsresult DirectoryWorkDone();

  nsresult SendToIOThread();

  void WaitForTransactions();

  void CleanupMetadata();

  void FinishSendResults();

  nsresult SendVersionChangeMessages(DatabaseActorInfo* aDatabaseActorInfo,
                                     Maybe<Database&> aOpeningDatabase,
                                     uint64_t aOldVersion,
                                     const Maybe<uint64_t>& aNewVersion);

  // Methods that subclasses must implement.
  virtual nsresult DoDirectoryWork() = 0;

  virtual nsresult DatabaseOpen() = 0;

  virtual nsresult DoDatabaseWork() = 0;

  virtual nsresult BeginVersionChange() = 0;

  virtual bool AreActorsAlive() = 0;

  virtual nsresult DispatchToWorkThread() = 0;

  virtual nsresult DoVersionUpdate() = 0;

  // Should only be called by Run().
  virtual void SendResults() = 0;

  // Common nsIRunnable implementation that subclasses may not override.
  NS_IMETHOD
  Run() final;

  void DirectoryLockAcquired(ClientDirectoryLockHandle aLockHandle);

  void DirectoryLockFailed();

  virtual void SendBlockedNotification() = 0;

 private:
  // Test whether this FactoryOp needs to wait for the given op.
  bool MustWaitFor(const FactoryOp& aExistingOp);

  void AddBlockingOp(FactoryOp& aOp) {
    AssertIsOnOwningThread();

    mBlocking.AppendElement(WrapNotNull(&aOp));
  }

  void AddBlockedOnOp(FactoryOp& aOp) {
    AssertIsOnOwningThread();

    mBlockedOn.Insert(&aOp);
  }

  void MaybeUnblock(FactoryOp& aOp) {
    AssertIsOnOwningThread();

    mBlockedOn.Remove(&aOp);
    if (mBlockedOn.IsEmpty()) {
      MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(this));
    }
  }
};

class FactoryRequestOp : public FactoryOp,
                         public PBackgroundIDBFactoryRequestParent {
 protected:
  const CommonFactoryRequestParams mCommonParams;

  FactoryRequestOp(SafeRefPtr<Factory> aFactory,
                   const Maybe<ContentParentId>& aContentParentId,
                   const CommonFactoryRequestParams& aCommonParams,
                   bool aDeleting)
      : FactoryOp(std::move(aFactory), aContentParentId,
                  aCommonParams.metadata().persistenceType(),
                  aCommonParams.principalInfo(),
                  Some(aCommonParams.metadata().name()), aDeleting),
        mCommonParams(aCommonParams) {}

  nsresult DoDirectoryWork() override;

  // IPDL methods.
  void ActorDestroy(ActorDestroyReason aWhy) override;
};

class OpenDatabaseOp final : public FactoryRequestOp {
  friend class Database;
  friend class VersionChangeTransaction;

  class VersionChangeOp;

  SafeRefPtr<FullDatabaseMetadata> mMetadata;

  uint64_t mRequestedVersion;
  SafeRefPtr<DatabaseFileManager> mFileManager;

  SafeRefPtr<Database> mDatabase;
  SafeRefPtr<VersionChangeTransaction> mVersionChangeTransaction;

  // This is only set while a VersionChangeOp is live. It holds a strong
  // reference to its OpenDatabaseOp object so this is a weak pointer to avoid
  // cycles.
  VersionChangeOp* mVersionChangeOp;

  MoveOnlyFunction<void()> mCompleteCallback;

  uint32_t mTelemetryId;

 public:
  OpenDatabaseOp(SafeRefPtr<Factory> aFactory,
                 const Maybe<ContentParentId>& aContentParentId,
                 const CommonFactoryRequestParams& aParams);

 private:
  ~OpenDatabaseOp() override { MOZ_ASSERT(!mVersionChangeOp); }

  nsresult LoadDatabaseInformation(mozIStorageConnection& aConnection);

  nsresult SendUpgradeNeeded();

  void EnsureDatabaseActor();

  nsresult EnsureDatabaseActorIsAlive();

  mozilla::Result<DatabaseSpec, nsresult> MetadataToSpec() const;

  void AssertMetadataConsistency(const FullDatabaseMetadata& aMetadata)
#ifdef DEBUG
      ;
#else
  {
  }
#endif

  void ConnectionClosedCallback();

  void ActorDestroy(ActorDestroyReason aWhy) override;

  nsresult DatabaseOpen() override;

  nsresult DoDatabaseWork() override;

  nsresult BeginVersionChange() override;

  bool AreActorsAlive() override;

  void SendBlockedNotification() override;

  nsresult DispatchToWorkThread() override;

  nsresult DoVersionUpdate() override;

  void SendResults() override;

  static nsresult UpdateLocaleAwareIndex(mozIStorageConnection& aConnection,
                                         const IndexMetadata& aIndexMetadata,
                                         const nsCString& aLocale);
};

class OpenDatabaseOp::VersionChangeOp final
    : public TransactionDatabaseOperationBase {
  friend class OpenDatabaseOp;

  RefPtr<OpenDatabaseOp> mOpenDatabaseOp;
  const uint64_t mRequestedVersion;
  uint64_t mPreviousVersion;

 private:
  explicit VersionChangeOp(OpenDatabaseOp* aOpenDatabaseOp)
      : TransactionDatabaseOperationBase(
            aOpenDatabaseOp->mVersionChangeTransaction.clonePtr(),
            /* aRequestId */ 0, aOpenDatabaseOp->LoggingSerialNumber()),
        mOpenDatabaseOp(aOpenDatabaseOp),
        mRequestedVersion(aOpenDatabaseOp->mRequestedVersion),
        mPreviousVersion(
            aOpenDatabaseOp->mMetadata->mCommonMetadata.version()) {
    MOZ_ASSERT(aOpenDatabaseOp);
    MOZ_ASSERT(mRequestedVersion);
  }

  ~VersionChangeOp() override { MOZ_ASSERT(!mOpenDatabaseOp); }

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  nsresult SendSuccessResult() override;

  bool SendFailureResult(nsresult aResultCode) override;

  void Cleanup() override;
};

class DeleteDatabaseOp final : public FactoryRequestOp {
  class VersionChangeOp;

  nsString mDatabaseDirectoryPath;
  nsString mDatabaseFilenameBase;
  uint64_t mPreviousVersion;

 public:
  DeleteDatabaseOp(SafeRefPtr<Factory> aFactory,
                   const Maybe<ContentParentId>& aContentParentId,
                   const CommonFactoryRequestParams& aParams)
      : FactoryRequestOp(std::move(aFactory), aContentParentId, aParams,
                         /* aDeleting */ true),
        mPreviousVersion(0) {}

 private:
  ~DeleteDatabaseOp() override = default;

  void LoadPreviousVersion(nsIFile& aDatabaseFile);

  nsresult DatabaseOpen() override;

  nsresult DoDatabaseWork() override;

  nsresult BeginVersionChange() override;

  bool AreActorsAlive() override;

  void SendBlockedNotification() override;

  nsresult DispatchToWorkThread() override;

  nsresult DoVersionUpdate() override;

  void SendResults() override;
};

class DeleteDatabaseOp::VersionChangeOp final : public DatabaseOperationBase {
  friend class DeleteDatabaseOp;

  RefPtr<DeleteDatabaseOp> mDeleteDatabaseOp;

 private:
  explicit VersionChangeOp(DeleteDatabaseOp* aDeleteDatabaseOp)
      : DatabaseOperationBase(aDeleteDatabaseOp->BackgroundChildLoggingId(),
                              aDeleteDatabaseOp->LoggingSerialNumber()),
        mDeleteDatabaseOp(aDeleteDatabaseOp) {
    MOZ_ASSERT(aDeleteDatabaseOp);
    MOZ_ASSERT(!aDeleteDatabaseOp->mDatabaseDirectoryPath.IsEmpty());
  }

  ~VersionChangeOp() override = default;

  nsresult RunOnIOThread();

  void RunOnOwningThread();

  NS_DECL_NSIRUNNABLE
};

class GetDatabasesOp final : public FactoryOp {
  nsTHashMap<nsStringHashKey, DatabaseMetadata> mDatabaseMetadataTable;
  nsTArray<DatabaseMetadata> mDatabaseMetadataArray;
  Factory::GetDatabasesResolver mResolver;

 public:
  GetDatabasesOp(SafeRefPtr<Factory> aFactory,
                 const Maybe<ContentParentId>& aContentParentId,
                 const PersistenceType aPersistenceType,
                 const PrincipalInfo& aPrincipalInfo,
                 Factory::GetDatabasesResolver&& aResolver)
      : FactoryOp(std::move(aFactory), aContentParentId, aPersistenceType,
                  aPrincipalInfo, Nothing(), /* aDeleting */ false),
        mResolver(std::move(aResolver)) {}

 private:
  ~GetDatabasesOp() override = default;

  nsresult DatabasesNotAvailable();

  nsresult DoDirectoryWork() override;

  nsresult DatabaseOpen() override;

  nsresult DoDatabaseWork() override;

  nsresult BeginVersionChange() override;

  bool AreActorsAlive() override;

  void SendBlockedNotification() override;

  nsresult DispatchToWorkThread() override;

  nsresult DoVersionUpdate() override;

  void SendResults() override;
};

class VersionChangeTransactionOp : public TransactionDatabaseOperationBase {
 public:
  void Cleanup() override;

 protected:
  explicit VersionChangeTransactionOp(
      SafeRefPtr<VersionChangeTransaction> aTransaction)
      : TransactionDatabaseOperationBase(std::move(aTransaction),
                                         /* aRequestId */ 0) {}

  ~VersionChangeTransactionOp() override = default;

 private:
  nsresult SendSuccessResult() override;

  bool SendFailureResult(nsresult aResultCode) override;
};

class CreateObjectStoreOp final : public VersionChangeTransactionOp {
  friend class VersionChangeTransaction;

  const ObjectStoreMetadata mMetadata;

 private:
  // Only created by VersionChangeTransaction.
  CreateObjectStoreOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                      const ObjectStoreMetadata& aMetadata)
      : VersionChangeTransactionOp(std::move(aTransaction)),
        mMetadata(aMetadata) {
    MOZ_ASSERT(aMetadata.id());
  }

  ~CreateObjectStoreOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;
};

class DeleteObjectStoreOp final : public VersionChangeTransactionOp {
  friend class VersionChangeTransaction;

  const SafeRefPtr<FullObjectStoreMetadata> mMetadata;
  const bool mIsLastObjectStore;

 private:
  // Only created by VersionChangeTransaction.
  DeleteObjectStoreOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                      SafeRefPtr<FullObjectStoreMetadata> aMetadata,
                      const bool aIsLastObjectStore)
      : VersionChangeTransactionOp(std::move(aTransaction)),
        mMetadata(std::move(aMetadata)),
        mIsLastObjectStore(aIsLastObjectStore) {
    MOZ_ASSERT(mMetadata->mCommonMetadata.id());
  }

  ~DeleteObjectStoreOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;
};

class RenameObjectStoreOp final : public VersionChangeTransactionOp {
  friend class VersionChangeTransaction;

  const int64_t mId;
  const nsString mNewName;

 private:
  // Only created by VersionChangeTransaction.
  RenameObjectStoreOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                      FullObjectStoreMetadata& aMetadata)
      : VersionChangeTransactionOp(std::move(aTransaction)),
        mId(aMetadata.mCommonMetadata.id()),
        mNewName(aMetadata.mCommonMetadata.name()) {
    MOZ_ASSERT(mId);
  }

  ~RenameObjectStoreOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;
};

class CreateIndexOp final : public VersionChangeTransactionOp {
  friend class VersionChangeTransaction;

  class UpdateIndexDataValuesFunction;

  const IndexMetadata mMetadata;
  Maybe<UniqueIndexTable> mMaybeUniqueIndexTable;
  const SafeRefPtr<DatabaseFileManager> mFileManager;
  const nsCString mDatabaseId;
  const IndexOrObjectStoreId mObjectStoreId;

 private:
  // Only created by VersionChangeTransaction.
  CreateIndexOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                IndexOrObjectStoreId aObjectStoreId,
                const IndexMetadata& aMetadata);

  ~CreateIndexOp() override = default;

  nsresult InsertDataFromObjectStore(DatabaseConnection* aConnection);

  nsresult InsertDataFromObjectStoreInternal(
      DatabaseConnection* aConnection) const;

  bool Init(TransactionBase& aTransaction) override;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;
};

class CreateIndexOp::UpdateIndexDataValuesFunction final
    : public mozIStorageFunction {
  RefPtr<CreateIndexOp> mOp;
  RefPtr<DatabaseConnection> mConnection;
  const NotNull<SafeRefPtr<Database>> mDatabase;

 public:
  UpdateIndexDataValuesFunction(CreateIndexOp* aOp,
                                DatabaseConnection* aConnection,
                                SafeRefPtr<Database> aDatabase)
      : mOp(aOp),
        mConnection(aConnection),
        mDatabase(WrapNotNull(std::move(aDatabase))) {
    MOZ_ASSERT(aOp);
    MOZ_ASSERT(aConnection);
    aConnection->AssertIsOnConnectionThread();
  }

  NS_DECL_ISUPPORTS

 private:
  ~UpdateIndexDataValuesFunction() = default;

  NS_DECL_MOZISTORAGEFUNCTION
};

class DeleteIndexOp final : public VersionChangeTransactionOp {
  friend class VersionChangeTransaction;

  const IndexOrObjectStoreId mObjectStoreId;
  const IndexOrObjectStoreId mIndexId;
  const bool mUnique;
  const bool mIsLastIndex;

 private:
  // Only created by VersionChangeTransaction.
  DeleteIndexOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                IndexOrObjectStoreId aObjectStoreId,
                IndexOrObjectStoreId aIndexId, const bool aUnique,
                const bool aIsLastIndex);

  ~DeleteIndexOp() override = default;

  nsresult RemoveReferencesToIndex(
      DatabaseConnection* aConnection, const Key& aObjectDataKey,
      nsTArray<IndexDataValue>& aIndexValues) const;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;
};

class RenameIndexOp final : public VersionChangeTransactionOp {
  friend class VersionChangeTransaction;

  const IndexOrObjectStoreId mObjectStoreId;
  const IndexOrObjectStoreId mIndexId;
  const nsString mNewName;

 private:
  // Only created by VersionChangeTransaction.
  RenameIndexOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                FullIndexMetadata& aMetadata,
                IndexOrObjectStoreId aObjectStoreId)
      : VersionChangeTransactionOp(std::move(aTransaction)),
        mObjectStoreId(aObjectStoreId),
        mIndexId(aMetadata.mCommonMetadata.id()),
        mNewName(aMetadata.mCommonMetadata.name()) {
    MOZ_ASSERT(mIndexId);
  }

  ~RenameIndexOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;
};

class NormalTransactionOp : public TransactionDatabaseOperationBase,
                            public PBackgroundIDBRequestParent {
#ifdef DEBUG
  bool mResponseSent;
#endif

 public:
  void Cleanup() override;

 protected:
  NormalTransactionOp(SafeRefPtr<TransactionBase> aTransaction,
                      const int64_t aRequestId)
      : TransactionDatabaseOperationBase(std::move(aTransaction), aRequestId)
#ifdef DEBUG
        ,
        mResponseSent(false)
#endif
  {
  }

  ~NormalTransactionOp() override = default;

  // An overload of DatabaseOperationBase's function that can avoid doing extra
  // work on non-versionchange transactions.
  mozilla::Result<bool, nsresult> ObjectStoreHasIndexes(
      DatabaseConnection& aConnection, IndexOrObjectStoreId aObjectStoreId,
      bool aMayHaveIndexes);

  virtual mozilla::Result<PreprocessParams, nsresult> GetPreprocessParams();

  // Subclasses use this override to set the IPDL response value.
  virtual void GetResponse(RequestResponse& aResponse,
                           size_t* aResponseSize) = 0;

 private:
  nsresult SendPreprocessInfo() override;

  nsresult SendSuccessResult() override;

  bool SendFailureResult(nsresult aResultCode) override;

  // IPDL methods.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvContinue(
      const PreprocessResponse& aResponse) final;
};

class ObjectStoreAddOrPutRequestOp final : public NormalTransactionOp {
  friend class TransactionBase;

  using PersistenceType = mozilla::dom::quota::PersistenceType;

  class StoredFileInfo final {
    InitializedOnce<const NotNull<SafeRefPtr<DatabaseFileInfo>>> mFileInfo;
    // Either nothing, a file actor or a non-Blob-backed inputstream to write to
    // disk.
    using FileActorOrInputStream =
        Variant<Nothing, RefPtr<DatabaseFile>, nsCOMPtr<nsIInputStream>>;
    InitializedOnce<const FileActorOrInputStream> mFileActorOrInputStream;
#ifdef DEBUG
    const StructuredCloneFileBase::FileType mType;
#endif
    void EnsureCipherKey();
    void AssertInvariants() const;

    StoredFileInfo(SafeRefPtr<DatabaseFileInfo> aFileInfo,
                   RefPtr<DatabaseFile> aFileActor);

    StoredFileInfo(SafeRefPtr<DatabaseFileInfo> aFileInfo,
                   nsCOMPtr<nsIInputStream> aInputStream);

   public:
#if defined(NS_BUILD_REFCNT_LOGGING)
    // Only for MOZ_COUNT_CTOR.
    StoredFileInfo(StoredFileInfo&& aOther)
        : mFileInfo{std::move(aOther.mFileInfo)},
          mFileActorOrInputStream{std::move(aOther.mFileActorOrInputStream)}
#  ifdef DEBUG
          ,
          mType{aOther.mType}
#  endif
    {
      MOZ_COUNT_CTOR(ObjectStoreAddOrPutRequestOp::StoredFileInfo);
    }
#else
    StoredFileInfo(StoredFileInfo&&) = default;
#endif

    static StoredFileInfo CreateForBlob(SafeRefPtr<DatabaseFileInfo> aFileInfo,
                                        RefPtr<DatabaseFile> aFileActor);
    static StoredFileInfo CreateForStructuredClone(
        SafeRefPtr<DatabaseFileInfo> aFileInfo,
        nsCOMPtr<nsIInputStream> aInputStream);

#if defined(DEBUG) || defined(NS_BUILD_REFCNT_LOGGING)
    ~StoredFileInfo() {
      AssertIsOnBackgroundThread();
      AssertInvariants();

      MOZ_COUNT_DTOR(ObjectStoreAddOrPutRequestOp::StoredFileInfo);
    }
#endif

    bool IsValid() const { return static_cast<bool>(mFileInfo); }

    const DatabaseFileInfo& GetFileInfo() const { return **mFileInfo; }

    bool ShouldCompress() const;

    void NotifyWriteSucceeded() const;

    using InputStreamResult =
        mozilla::Result<nsCOMPtr<nsIInputStream>, nsresult>;
    InputStreamResult GetInputStream();

    void Serialize(nsString& aText) const;
  };
  class SCInputStream;

  ObjectStoreAddPutParams mParams;
  Maybe<UniqueIndexTable> mUniqueIndexTable;

  // This must be non-const so that we can update the mNextAutoIncrementId field
  // if we are modifying an autoIncrement objectStore.
  SafeRefPtr<FullObjectStoreMetadata> mMetadata;

  nsTArray<StoredFileInfo> mStoredFileInfos;

  Key mResponse;
  const OriginMetadata mOriginMetadata;
  const PersistenceType mPersistenceType;
  const bool mOverwrite;
  bool mObjectStoreMayHaveIndexes;
  bool mDataOverThreshold;

 private:
  // Only created by TransactionBase.
  ObjectStoreAddOrPutRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                               const int64_t aRequestId,
                               RequestParams&& aParams);

  ~ObjectStoreAddOrPutRequestOp() override = default;

  nsresult RemoveOldIndexDataValues(DatabaseConnection* aConnection);

  bool Init(TransactionBase& aTransaction) override;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override;

  void Cleanup() override;
};

void ObjectStoreAddOrPutRequestOp::StoredFileInfo::AssertInvariants() const {
  // The only allowed types are eStructuredClone, eBlob and eMutableFile.
  MOZ_ASSERT(StructuredCloneFileBase::eStructuredClone == mType ||
             StructuredCloneFileBase::eBlob == mType ||
             StructuredCloneFileBase::eMutableFile == mType);

  // mFileInfo and a file actor in mFileActorOrInputStream are present until
  // the object is moved away, but an inputStream in mFileActorOrInputStream
  // can be released early.
  MOZ_ASSERT_IF(static_cast<bool>(mFileActorOrInputStream) &&
                    mFileActorOrInputStream->is<RefPtr<DatabaseFile>>(),
                static_cast<bool>(mFileInfo));

  if (mFileInfo) {
    // In a non-moved StoredFileInfo, one of the following is true:
    // - This was an overflow structured clone (eStructuredClone) and
    //   storedFileInfo.mFileActorOrInputStream CAN be a non-nullptr input
    //   stream (but that might have been release by ReleaseInputStream).
    MOZ_ASSERT_IF(
        StructuredCloneFileBase::eStructuredClone == mType,
        !mFileActorOrInputStream ||
            (mFileActorOrInputStream->is<nsCOMPtr<nsIInputStream>>() &&
             mFileActorOrInputStream->as<nsCOMPtr<nsIInputStream>>()));

    // - This is a reference to a Blob (eBlob) that may or may not have
    //   already been written to disk.  storedFileInfo.mFileActorOrInputStream
    //   MUST be a non-null file actor, but its GetInputStream may return
    //   nullptr (so don't assert on that).
    MOZ_ASSERT_IF(StructuredCloneFileBase::eBlob == mType,
                  mFileActorOrInputStream->is<RefPtr<DatabaseFile>>() &&
                      mFileActorOrInputStream->as<RefPtr<DatabaseFile>>());

    // - It's a mutable file (eMutableFile).  No writing will be performed,
    // and storedFileInfo.mFileActorOrInputStream is Nothing.
    MOZ_ASSERT_IF(StructuredCloneFileBase::eMutableFile == mType,
                  mFileActorOrInputStream->is<Nothing>());
  }
}

void ObjectStoreAddOrPutRequestOp::StoredFileInfo::EnsureCipherKey() {
  const auto& fileInfo = GetFileInfo();
  const auto& fileManager = fileInfo.Manager();

  // No need to generate cipher keys if we are not in PBM
  if (!fileManager.IsInPrivateBrowsingMode()) {
    return;
  }

  nsCString keyId;
  keyId.AppendInt(fileInfo.Id());

  fileManager.MutableCipherKeyManagerRef().Ensure(keyId);
}

ObjectStoreAddOrPutRequestOp::StoredFileInfo::StoredFileInfo(
    SafeRefPtr<DatabaseFileInfo> aFileInfo, RefPtr<DatabaseFile> aFileActor)
    : mFileInfo{WrapNotNull(std::move(aFileInfo))},
      mFileActorOrInputStream{std::move(aFileActor)}
#ifdef DEBUG
      ,
      mType{StructuredCloneFileBase::eBlob}
#endif
{
  AssertIsOnBackgroundThread();
  AssertInvariants();

  EnsureCipherKey();
  MOZ_COUNT_CTOR(ObjectStoreAddOrPutRequestOp::StoredFileInfo);
}

ObjectStoreAddOrPutRequestOp::StoredFileInfo::StoredFileInfo(
    SafeRefPtr<DatabaseFileInfo> aFileInfo,
    nsCOMPtr<nsIInputStream> aInputStream)
    : mFileInfo{WrapNotNull(std::move(aFileInfo))},
      mFileActorOrInputStream{std::move(aInputStream)}
#ifdef DEBUG
      ,
      mType{StructuredCloneFileBase::eStructuredClone}
#endif
{
  AssertIsOnBackgroundThread();
  AssertInvariants();

  EnsureCipherKey();
  MOZ_COUNT_CTOR(ObjectStoreAddOrPutRequestOp::StoredFileInfo);
}

ObjectStoreAddOrPutRequestOp::StoredFileInfo
ObjectStoreAddOrPutRequestOp::StoredFileInfo::CreateForBlob(
    SafeRefPtr<DatabaseFileInfo> aFileInfo, RefPtr<DatabaseFile> aFileActor) {
  return {std::move(aFileInfo), std::move(aFileActor)};
}

ObjectStoreAddOrPutRequestOp::StoredFileInfo
ObjectStoreAddOrPutRequestOp::StoredFileInfo::CreateForStructuredClone(
    SafeRefPtr<DatabaseFileInfo> aFileInfo,
    nsCOMPtr<nsIInputStream> aInputStream) {
  return {std::move(aFileInfo), std::move(aInputStream)};
}

bool ObjectStoreAddOrPutRequestOp::StoredFileInfo::ShouldCompress() const {
  // Must not be called after moving.
  MOZ_ASSERT(IsValid());

  // Compression is only necessary for eStructuredClone, i.e. when
  // mFileActorOrInputStream stored an input stream. However, this is only
  // called after GetInputStream, when mFileActorOrInputStream has been
  // cleared, which is only possible for this type.
  const bool res = !mFileActorOrInputStream;
  MOZ_ASSERT(res == (StructuredCloneFileBase::eStructuredClone == mType));
  return res;
}

void ObjectStoreAddOrPutRequestOp::StoredFileInfo::NotifyWriteSucceeded()
    const {
  MOZ_ASSERT(IsValid());

  // For eBlob, clear the blob implementation.
  if (mFileActorOrInputStream &&
      mFileActorOrInputStream->is<RefPtr<DatabaseFile>>()) {
    mFileActorOrInputStream->as<RefPtr<DatabaseFile>>()
        ->WriteSucceededClearBlobImpl();
  }

  // For the other types, no action is necessary.
}

ObjectStoreAddOrPutRequestOp::StoredFileInfo::InputStreamResult
ObjectStoreAddOrPutRequestOp::StoredFileInfo::GetInputStream() {
  if (!mFileActorOrInputStream) {
    MOZ_ASSERT(StructuredCloneFileBase::eStructuredClone == mType);
    return nsCOMPtr<nsIInputStream>{};
  }

  // For the different cases, see also the comments in AssertInvariants.
  return mFileActorOrInputStream->match(
      [](const Nothing&) -> InputStreamResult {
        return nsCOMPtr<nsIInputStream>{};
      },
      [](const RefPtr<DatabaseFile>& databaseActor) -> InputStreamResult {
        ErrorResult rv;
        auto inputStream = databaseActor->GetInputStream(rv);
        if (NS_WARN_IF(rv.Failed())) {
          return Err(rv.StealNSResult());
        }

        return inputStream;
      },
      [this](const nsCOMPtr<nsIInputStream>& inputStream) -> InputStreamResult {
        auto res = inputStream;
        // destroy() clears the inputStream parameter, so we needed to make a
        // copy before
        mFileActorOrInputStream.destroy();
        AssertInvariants();
        return res;
      });
}

void ObjectStoreAddOrPutRequestOp::StoredFileInfo::Serialize(
    nsString& aText) const {
  AssertInvariants();
  MOZ_ASSERT(IsValid());

  const int64_t id = (*mFileInfo)->Id();

  auto structuredCloneHandler = [&aText, id](const nsCOMPtr<nsIInputStream>&) {
    // eStructuredClone
    aText.Append('.');
    aText.AppendInt(id);
  };

  // If mFileActorOrInputStream was moved, we had an inputStream before.
  if (!mFileActorOrInputStream) {
    structuredCloneHandler(nullptr);
    return;
  }

  // This encoding is parsed in DeserializeStructuredCloneFile.
  mFileActorOrInputStream->match(
      [&aText, id](const Nothing&) {
        // eMutableFile
        aText.AppendInt(-id);
      },
      [&aText, id](const RefPtr<DatabaseFile>&) {
        // eBlob
        aText.AppendInt(id);
      },
      structuredCloneHandler);
}

class ObjectStoreAddOrPutRequestOp::SCInputStream final
    : public nsIInputStream {
  const JSStructuredCloneData& mData;
  JSStructuredCloneData::Iterator mIter;

 public:
  explicit SCInputStream(const JSStructuredCloneData& aData)
      : mData(aData), mIter(aData.Start()) {}

 private:
  virtual ~SCInputStream() = default;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIINPUTSTREAM
};

class ObjectStoreGetRequestOp final : public NormalTransactionOp {
  friend class TransactionBase;

  const IndexOrObjectStoreId mObjectStoreId;
  SafeRefPtr<Database> mDatabase;
  const Maybe<SerializedKeyRange> mOptionalKeyRange;
  AutoTArray<StructuredCloneReadInfoParent, 1> mResponse;
  PBackgroundParent* mBackgroundParent;
  uint32_t mPreprocessInfoCount;
  const uint32_t mLimit;
  const bool mGetAll;

 private:
  // Only created by TransactionBase.
  ObjectStoreGetRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                          const int64_t aRequestId,
                          const RequestParams& aParams, bool aGetAll);

  ~ObjectStoreGetRequestOp() override = default;

  template <typename T>
  mozilla::Result<T, nsresult> ConvertResponse(
      StructuredCloneReadInfoParent&& aInfo);

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  bool HasPreprocessInfo() override;

  mozilla::Result<PreprocessParams, nsresult> GetPreprocessParams() override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override;
};

class ObjectStoreGetKeyRequestOp final : public NormalTransactionOp {
  friend class TransactionBase;

  const IndexOrObjectStoreId mObjectStoreId;
  const Maybe<SerializedKeyRange> mOptionalKeyRange;
  const uint32_t mLimit;
  const bool mGetAll;
  nsTArray<Key> mResponse;

 private:
  // Only created by TransactionBase.
  ObjectStoreGetKeyRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                             const int64_t aRequestId,
                             const RequestParams& aParams, bool aGetAll);

  ~ObjectStoreGetKeyRequestOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override;
};

class ObjectStoreDeleteRequestOp final : public NormalTransactionOp {
  friend class TransactionBase;

  const ObjectStoreDeleteParams mParams;
  ObjectStoreDeleteResponse mResponse;
  bool mObjectStoreMayHaveIndexes;

 private:
  ObjectStoreDeleteRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                             const int64_t aRequestId,
                             const ObjectStoreDeleteParams& aParams);

  ~ObjectStoreDeleteRequestOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override {
    aResponse = std::move(mResponse);
    *aResponseSize = 0;
  }
};

class ObjectStoreClearRequestOp final : public NormalTransactionOp {
  friend class TransactionBase;

  const ObjectStoreClearParams mParams;
  ObjectStoreClearResponse mResponse;
  bool mObjectStoreMayHaveIndexes;

 private:
  ObjectStoreClearRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                            const int64_t aRequestId,
                            const ObjectStoreClearParams& aParams);

  ~ObjectStoreClearRequestOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override {
    aResponse = std::move(mResponse);
    *aResponseSize = 0;
  }
};

class ObjectStoreCountRequestOp final : public NormalTransactionOp {
  friend class TransactionBase;

  const ObjectStoreCountParams mParams;
  ObjectStoreCountResponse mResponse;

 private:
  ObjectStoreCountRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                            const int64_t aRequestId,
                            const ObjectStoreCountParams& aParams)
      : NormalTransactionOp(std::move(aTransaction), aRequestId),
        mParams(aParams) {}

  ~ObjectStoreCountRequestOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override {
    aResponse = std::move(mResponse);
    *aResponseSize = sizeof(uint64_t);
  }
};

class IndexRequestOpBase : public NormalTransactionOp {
 protected:
  const SafeRefPtr<FullIndexMetadata> mMetadata;

 protected:
  IndexRequestOpBase(SafeRefPtr<TransactionBase> aTransaction,
                     const int64_t aRequestId, const RequestParams& aParams)
      : NormalTransactionOp(std::move(aTransaction), aRequestId),
        mMetadata(IndexMetadataForParams(Transaction(), aParams)) {}

  ~IndexRequestOpBase() override = default;

 private:
  static SafeRefPtr<FullIndexMetadata> IndexMetadataForParams(
      const TransactionBase& aTransaction, const RequestParams& aParams);
};

class IndexGetRequestOp final : public IndexRequestOpBase {
  friend class TransactionBase;

  SafeRefPtr<Database> mDatabase;
  const Maybe<SerializedKeyRange> mOptionalKeyRange;
  AutoTArray<StructuredCloneReadInfoParent, 1> mResponse;
  PBackgroundParent* mBackgroundParent;
  const uint32_t mLimit;
  const bool mGetAll;

 private:
  // Only created by TransactionBase.
  IndexGetRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                    const int64_t aRequestId, const RequestParams& aParams,
                    bool aGetAll);

  ~IndexGetRequestOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override;
};

class IndexGetKeyRequestOp final : public IndexRequestOpBase {
  friend class TransactionBase;

  const Maybe<SerializedKeyRange> mOptionalKeyRange;
  AutoTArray<Key, 1> mResponse;
  const uint32_t mLimit;
  const bool mGetAll;

 private:
  // Only created by TransactionBase.
  IndexGetKeyRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                       const int64_t aRequestId, const RequestParams& aParams,
                       bool aGetAll);

  ~IndexGetKeyRequestOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override;
};

class IndexCountRequestOp final : public IndexRequestOpBase {
  friend class TransactionBase;

  const IndexCountParams mParams;
  IndexCountResponse mResponse;

 private:
  // Only created by TransactionBase.
  IndexCountRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                      const int64_t aRequestId, const RequestParams& aParams)
      : IndexRequestOpBase(std::move(aTransaction), aRequestId, aParams),
        mParams(aParams.get_IndexCountParams()) {}

  ~IndexCountRequestOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  void GetResponse(RequestResponse& aResponse, size_t* aResponseSize) override {
    aResponse = std::move(mResponse);
    *aResponseSize = sizeof(uint64_t);
  }
};

template <IDBCursorType CursorType>
class Cursor;

constexpr IDBCursorType ToKeyOnlyType(const IDBCursorType aType) {
  MOZ_ASSERT(aType == IDBCursorType::ObjectStore ||
             aType == IDBCursorType::ObjectStoreKey ||
             aType == IDBCursorType::Index || aType == IDBCursorType::IndexKey);
  switch (aType) {
    case IDBCursorType::ObjectStore:
      [[fallthrough]];
    case IDBCursorType::ObjectStoreKey:
      return IDBCursorType::ObjectStoreKey;
    case IDBCursorType::Index:
      [[fallthrough]];
    case IDBCursorType::IndexKey:
      return IDBCursorType::IndexKey;
  }
}

template <IDBCursorType CursorType>
using CursorPosition = CursorData<ToKeyOnlyType(CursorType)>;

#ifdef DEBUG
constexpr indexedDB::OpenCursorParams::Type ToOpenCursorParamsType(
    const IDBCursorType aType) {
  MOZ_ASSERT(aType == IDBCursorType::ObjectStore ||
             aType == IDBCursorType::ObjectStoreKey ||
             aType == IDBCursorType::Index || aType == IDBCursorType::IndexKey);
  switch (aType) {
    case IDBCursorType::ObjectStore:
      return indexedDB::OpenCursorParams::TObjectStoreOpenCursorParams;
    case IDBCursorType::ObjectStoreKey:
      return indexedDB::OpenCursorParams::TObjectStoreOpenKeyCursorParams;
    case IDBCursorType::Index:
      return indexedDB::OpenCursorParams::TIndexOpenCursorParams;
    case IDBCursorType::IndexKey:
      return indexedDB::OpenCursorParams::TIndexOpenKeyCursorParams;
  }
}
#endif

class CursorBase : public PBackgroundIDBCursorParent {
  friend class TransactionBase;
  template <IDBCursorType CursorType>
  friend class CommonOpenOpHelper;

 protected:
  const SafeRefPtr<TransactionBase> mTransaction;

  // This should only be touched on the PBackground thread to check whether
  // the objectStore has been deleted. Holding these saves a hash lookup for
  // every call to continue()/advance().
  InitializedOnce<const NotNull<SafeRefPtr<FullObjectStoreMetadata>>>
      mObjectStoreMetadata;

  const IndexOrObjectStoreId mObjectStoreId;

  LazyInitializedOnce<const Key>
      mLocaleAwareRangeBound;  ///< If the cursor is based on a key range, the
                               ///< bound in the direction of iteration (e.g.
                               ///< the upper bound in case of mDirection ==
                               ///< NEXT). If the cursor is based on a key, it
                               ///< is unset. If mLocale is set, this was
                               ///< converted to mLocale.

  const Direction mDirection;

  const int32_t mMaxExtraCount;

  const bool mIsSameProcessActor;

  struct ConstructFromTransactionBase {};

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(mozilla::dom::indexedDB::CursorBase,
                                        final)

  CursorBase(SafeRefPtr<TransactionBase> aTransaction,
             SafeRefPtr<FullObjectStoreMetadata> aObjectStoreMetadata,
             Direction aDirection,
             ConstructFromTransactionBase aConstructionTag);

 protected:
  // Reference counted.
  ~CursorBase() override { MOZ_ASSERT(!mObjectStoreMetadata); }

 private:
  virtual bool Start(const int64_t aRequestId,
                     const OpenCursorParams& aParams) = 0;
};

class IndexCursorBase : public CursorBase {
 public:
  bool IsLocaleAware() const { return !mLocale.IsEmpty(); }

  IndexCursorBase(SafeRefPtr<TransactionBase> aTransaction,
                  SafeRefPtr<FullObjectStoreMetadata> aObjectStoreMetadata,
                  SafeRefPtr<FullIndexMetadata> aIndexMetadata,
                  Direction aDirection,
                  ConstructFromTransactionBase aConstructionTag)
      : CursorBase{std::move(aTransaction), std::move(aObjectStoreMetadata),
                   aDirection, aConstructionTag},
        mIndexMetadata(WrapNotNull(std::move(aIndexMetadata))),
        mIndexId((*mIndexMetadata)->mCommonMetadata.id()),
        mUniqueIndex((*mIndexMetadata)->mCommonMetadata.unique()),
        mLocale((*mIndexMetadata)->mCommonMetadata.locale()) {}

 protected:
  IndexOrObjectStoreId Id() const { return mIndexId; }

  // This should only be touched on the PBackground thread to check whether
  // the index has been deleted. Holding these saves a hash lookup for every
  // call to continue()/advance().
  InitializedOnce<const NotNull<SafeRefPtr<FullIndexMetadata>>> mIndexMetadata;
  const IndexOrObjectStoreId mIndexId;
  const bool mUniqueIndex;
  const nsCString
      mLocale;  ///< The locale if the cursor is locale-aware, otherwise empty.

  struct ContinueQueries {
    nsCString mContinueQuery;
    nsCString mContinueToQuery;
    nsCString mContinuePrimaryKeyQuery;

    const nsACString& GetContinueQuery(const bool hasContinueKey,
                                       const bool hasContinuePrimaryKey) const {
      return hasContinuePrimaryKey ? mContinuePrimaryKeyQuery
             : hasContinueKey      ? mContinueToQuery
                                   : mContinueQuery;
    }
  };
};

class ObjectStoreCursorBase : public CursorBase {
 public:
  using CursorBase::CursorBase;

  static constexpr bool IsLocaleAware() { return false; }

 protected:
  IndexOrObjectStoreId Id() const { return mObjectStoreId; }

  struct ContinueQueries {
    nsCString mContinueQuery;
    nsCString mContinueToQuery;

    const nsACString& GetContinueQuery(const bool hasContinueKey,
                                       const bool hasContinuePrimaryKey) const {
      MOZ_ASSERT(!hasContinuePrimaryKey);
      return hasContinueKey ? mContinueToQuery : mContinueQuery;
    }
  };
};

using FilesArray = nsTArray<nsTArray<StructuredCloneFileParent>>;

struct PseudoFilesArray {
  static constexpr bool IsEmpty() { return true; }

  static constexpr void Clear() {}
};

template <IDBCursorType CursorType>
using FilesArrayT =
    std::conditional_t<!CursorTypeTraits<CursorType>::IsKeyOnlyCursor,
                       FilesArray, PseudoFilesArray>;

class ValueCursorBase {
  friend struct ValuePopulateResponseHelper<true>;
  friend struct ValuePopulateResponseHelper<false>;

 protected:
  explicit ValueCursorBase(TransactionBase* const aTransaction)
      : mDatabase(aTransaction->GetDatabasePtr()),
        mFileManager(mDatabase->GetFileManagerPtr()),
        mBackgroundParent(WrapNotNull(aTransaction->GetBackgroundParent())) {
    MOZ_ASSERT(mDatabase);
  }

  void ProcessFiles(CursorResponse& aResponse, const FilesArray& aFiles);

  ~ValueCursorBase() { MOZ_ASSERT(!mBackgroundParent); }

  const SafeRefPtr<Database> mDatabase;
  const NotNull<SafeRefPtr<DatabaseFileManager>> mFileManager;

  InitializedOnce<const NotNull<PBackgroundParent*>> mBackgroundParent;
};

class KeyCursorBase {
 protected:
  explicit KeyCursorBase(TransactionBase* const /*aTransaction*/) {}

  static constexpr void ProcessFiles(CursorResponse& aResponse,
                                     const PseudoFilesArray& aFiles) {}
};

template <IDBCursorType CursorType>
class CursorOpBaseHelperBase;

template <IDBCursorType CursorType>
class Cursor final
    : public std::conditional_t<
          CursorTypeTraits<CursorType>::IsObjectStoreCursor,
          ObjectStoreCursorBase, IndexCursorBase>,
      public std::conditional_t<CursorTypeTraits<CursorType>::IsKeyOnlyCursor,
                                KeyCursorBase, ValueCursorBase> {
  using Base =
      std::conditional_t<CursorTypeTraits<CursorType>::IsObjectStoreCursor,
                         ObjectStoreCursorBase, IndexCursorBase>;

  using KeyValueBase =
      std::conditional_t<CursorTypeTraits<CursorType>::IsKeyOnlyCursor,
                         KeyCursorBase, ValueCursorBase>;

  static constexpr bool IsIndexCursor =
      !CursorTypeTraits<CursorType>::IsObjectStoreCursor;

  static constexpr bool IsValueCursor =
      !CursorTypeTraits<CursorType>::IsKeyOnlyCursor;

  class CursorOpBase;
  class OpenOp;
  class ContinueOp;

  using Base::Id;
  using CursorBase::Manager;
  using CursorBase::mDirection;
  using CursorBase::mObjectStoreId;
  using CursorBase::mTransaction;
  using typename CursorBase::ActorDestroyReason;

  using TypedOpenOpHelper =
      std::conditional_t<IsIndexCursor, IndexOpenOpHelper<CursorType>,
                         ObjectStoreOpenOpHelper<CursorType>>;

  friend class CursorOpBaseHelperBase<CursorType>;
  friend class CommonOpenOpHelper<CursorType>;
  friend TypedOpenOpHelper;
  friend class OpenOpHelper<CursorType>;

  CursorOpBase* mCurrentlyRunningOp = nullptr;

  LazyInitializedOnce<const typename Base::ContinueQueries> mContinueQueries;

  // Only called by TransactionBase.
  bool Start(const int64_t aRequestId, const OpenCursorParams& aParams) final;

  void SendResponseInternal(CursorResponse& aResponse,
                            const FilesArrayT<CursorType>& aFiles);

  // Must call SendResponseInternal!
  bool SendResponse(const CursorResponse& aResponse) = delete;

  // IPDL methods.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvContinue(
      const int64_t& aRequestId, const CursorRequestParams& aParams,
      const Key& aCurrentKey, const Key& aCurrentObjectStoreKey) override;

 public:
  Cursor(SafeRefPtr<TransactionBase> aTransaction,
         SafeRefPtr<FullObjectStoreMetadata> aObjectStoreMetadata,
         SafeRefPtr<FullIndexMetadata> aIndexMetadata,
         typename Base::Direction aDirection,
         typename Base::ConstructFromTransactionBase aConstructionTag)
      : Base{std::move(aTransaction), std::move(aObjectStoreMetadata),
             std::move(aIndexMetadata), aDirection, aConstructionTag},
        KeyValueBase{this->mTransaction.unsafeGetRawPtr()} {}

  Cursor(SafeRefPtr<TransactionBase> aTransaction,
         SafeRefPtr<FullObjectStoreMetadata> aObjectStoreMetadata,
         typename Base::Direction aDirection,
         typename Base::ConstructFromTransactionBase aConstructionTag)
      : Base{std::move(aTransaction), std::move(aObjectStoreMetadata),
             aDirection, aConstructionTag},
        KeyValueBase{this->mTransaction.unsafeGetRawPtr()} {}

 private:
  void SetOptionalKeyRange(const Maybe<SerializedKeyRange>& aOptionalKeyRange,
                           bool* aOpen);

  bool VerifyRequestParams(const CursorRequestParams& aParams,
                           const CursorPosition<CursorType>& aPosition) const;

  ~Cursor() final = default;
};

template <IDBCursorType CursorType>
class Cursor<CursorType>::CursorOpBase
    : public TransactionDatabaseOperationBase {
  friend class CursorOpBaseHelperBase<CursorType>;

 protected:
  RefPtr<Cursor> mCursor;
  FilesArrayT<CursorType> mFiles;  // TODO: Consider removing this member
                                   // entirely if we are no value cursor.

  CursorResponse mResponse;

#ifdef DEBUG
  bool mResponseSent;
#endif

 protected:
  explicit CursorOpBase(Cursor* aCursor, const int64_t aRequestId)
      : TransactionDatabaseOperationBase(aCursor->mTransaction.clonePtr(),
                                         /* aRequestId */ aRequestId),
        mCursor(aCursor)
#ifdef DEBUG
        ,
        mResponseSent(false)
#endif
  {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(aCursor);
  }

  ~CursorOpBase() override = default;

  bool SendFailureResult(nsresult aResultCode) final;
  nsresult SendSuccessResult() final;

  void Cleanup() override;
};

template <IDBCursorType CursorType>
class OpenOpHelper;

using ResponseSizeOrError = Result<size_t, nsresult>;

template <IDBCursorType CursorType>
class CursorOpBaseHelperBase {
 public:
  explicit CursorOpBaseHelperBase(
      typename Cursor<CursorType>::CursorOpBase& aOp)
      : mOp{aOp} {}

  ResponseSizeOrError PopulateResponseFromStatement(mozIStorageStatement* aStmt,
                                                    bool aInitializeResponse,
                                                    Key* const aOptOutSortKey);

  void PopulateExtraResponses(mozIStorageStatement* aStmt,
                              uint32_t aMaxExtraCount,
                              const size_t aInitialResponseSize,
                              const nsACString& aOperation,
                              Key* const aOptPreviousSortKey);

 protected:
  Cursor<CursorType>& GetCursor() {
    MOZ_ASSERT(mOp.mCursor);
    return *mOp.mCursor;
  }

  void SetResponse(CursorResponse aResponse) {
    mOp.mResponse = std::move(aResponse);
  }

 protected:
  typename Cursor<CursorType>::CursorOpBase& mOp;
};

class CommonOpenOpHelperBase {
 protected:
  static void AppendConditionClause(const nsACString& aColumnName,
                                    const nsACString& aStatementParameterName,
                                    bool aLessThan, bool aEquals,
                                    nsCString& aResult);
};

template <IDBCursorType CursorType>
class CommonOpenOpHelper : public CursorOpBaseHelperBase<CursorType>,
                           protected CommonOpenOpHelperBase {
 public:
  explicit CommonOpenOpHelper(typename Cursor<CursorType>::OpenOp& aOp)
      : CursorOpBaseHelperBase<CursorType>{aOp} {}

 protected:
  using CursorOpBaseHelperBase<CursorType>::GetCursor;
  using CursorOpBaseHelperBase<CursorType>::PopulateExtraResponses;
  using CursorOpBaseHelperBase<CursorType>::PopulateResponseFromStatement;
  using CursorOpBaseHelperBase<CursorType>::SetResponse;

  const Maybe<SerializedKeyRange>& GetOptionalKeyRange() const {
    // This downcast is safe, since we initialized mOp from an OpenOp in the
    // ctor.
    return static_cast<typename Cursor<CursorType>::OpenOp&>(this->mOp)
        .mOptionalKeyRange;
  }

  nsresult ProcessStatementSteps(mozIStorageStatement* aStmt);
};

template <IDBCursorType CursorType>
class ObjectStoreOpenOpHelper : protected CommonOpenOpHelper<CursorType> {
 public:
  using CommonOpenOpHelper<CursorType>::CommonOpenOpHelper;

 protected:
  using CommonOpenOpHelper<CursorType>::GetCursor;
  using CommonOpenOpHelper<CursorType>::GetOptionalKeyRange;
  using CommonOpenOpHelper<CursorType>::AppendConditionClause;

  void PrepareKeyConditionClauses(const nsACString& aDirectionClause,
                                  const nsACString& aQueryStart);
};

template <IDBCursorType CursorType>
class IndexOpenOpHelper : protected CommonOpenOpHelper<CursorType> {
 public:
  using CommonOpenOpHelper<CursorType>::CommonOpenOpHelper;

 protected:
  using CommonOpenOpHelper<CursorType>::GetCursor;
  using CommonOpenOpHelper<CursorType>::GetOptionalKeyRange;
  using CommonOpenOpHelper<CursorType>::AppendConditionClause;

  void PrepareIndexKeyConditionClause(
      const nsACString& aDirectionClause,
      const nsLiteralCString& aObjectDataKeyPrefix, nsAutoCString aQueryStart);
};

template <>
class OpenOpHelper<IDBCursorType::ObjectStore>
    : public ObjectStoreOpenOpHelper<IDBCursorType::ObjectStore> {
 public:
  using ObjectStoreOpenOpHelper<
      IDBCursorType::ObjectStore>::ObjectStoreOpenOpHelper;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection);
};

template <>
class OpenOpHelper<IDBCursorType::ObjectStoreKey>
    : public ObjectStoreOpenOpHelper<IDBCursorType::ObjectStoreKey> {
 public:
  using ObjectStoreOpenOpHelper<
      IDBCursorType::ObjectStoreKey>::ObjectStoreOpenOpHelper;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection);
};

template <>
class OpenOpHelper<IDBCursorType::Index>
    : IndexOpenOpHelper<IDBCursorType::Index> {
 private:
  void PrepareKeyConditionClauses(const nsACString& aDirectionClause,
                                  nsAutoCString aQueryStart) {
    PrepareIndexKeyConditionClause(aDirectionClause, "index_table."_ns,
                                   std::move(aQueryStart));
  }

 public:
  using IndexOpenOpHelper<IDBCursorType::Index>::IndexOpenOpHelper;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection);
};

template <>
class OpenOpHelper<IDBCursorType::IndexKey>
    : IndexOpenOpHelper<IDBCursorType::IndexKey> {
 private:
  void PrepareKeyConditionClauses(const nsACString& aDirectionClause,
                                  nsAutoCString aQueryStart) {
    PrepareIndexKeyConditionClause(aDirectionClause, ""_ns,
                                   std::move(aQueryStart));
  }

 public:
  using IndexOpenOpHelper<IDBCursorType::IndexKey>::IndexOpenOpHelper;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection);
};

template <IDBCursorType CursorType>
class Cursor<CursorType>::OpenOp final : public CursorOpBase {
  friend class Cursor<CursorType>;
  friend class CommonOpenOpHelper<CursorType>;

  const Maybe<SerializedKeyRange> mOptionalKeyRange;

  using CursorOpBase::mCursor;
  using CursorOpBase::mResponse;

  // Only created by Cursor.
  OpenOp(Cursor* const aCursor, const int64_t aRequestId,
         const Maybe<SerializedKeyRange>& aOptionalKeyRange)
      : CursorOpBase(aCursor, aRequestId),
        mOptionalKeyRange(aOptionalKeyRange) {}

  // Reference counted.
  ~OpenOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;
};

template <IDBCursorType CursorType>
class Cursor<CursorType>::ContinueOp final
    : public Cursor<CursorType>::CursorOpBase {
  friend class Cursor<CursorType>;

  using CursorOpBase::mCursor;
  using CursorOpBase::mResponse;
  const CursorRequestParams mParams;

  // Only created by Cursor.
  ContinueOp(Cursor* const aCursor, int64_t aRequestId,
             CursorRequestParams aParams, CursorPosition<CursorType> aPosition)
      : CursorOpBase(aCursor, aRequestId),
        mParams(std::move(aParams)),
        mCurrentPosition{std::move(aPosition)} {
    MOZ_ASSERT(mParams.type() != CursorRequestParams::T__None);
  }

  // Reference counted.
  ~ContinueOp() override = default;

  nsresult DoDatabaseWork(DatabaseConnection* aConnection) override;

  const CursorPosition<CursorType> mCurrentPosition;
};

class Utils final : public PBackgroundIndexedDBUtilsParent {
#ifdef DEBUG
  bool mActorDestroyed;
#endif

 public:
  Utils();

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(mozilla::dom::indexedDB::Utils)

 private:
  // Reference counted.
  ~Utils() override;

  // IPDL methods are only called by IPDL.
  void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvDeleteMe() override;

  mozilla::ipc::IPCResult RecvGetFileReferences(
      const PersistenceType& aPersistenceType, const nsACString& aOrigin,
      const nsAString& aDatabaseName, const int64_t& aFileId, int32_t* aRefCnt,
      int32_t* aDBRefCnt, bool* aResult) override;

  mozilla::ipc::IPCResult RecvDoMaintenance(
      DoMaintenanceResolver&& aResolver) override;
};

/*******************************************************************************
 * Other class declarations
 ******************************************************************************/

struct DatabaseActorInfo final {
  friend class mozilla::DefaultDelete<DatabaseActorInfo>;

  SafeRefPtr<FullDatabaseMetadata> mMetadata;
  nsTArray<NotNull<CheckedUnsafePtr<Database>>> mLiveDatabases;
  RefPtr<FactoryOp> mWaitingFactoryOp;

  DatabaseActorInfo(SafeRefPtr<FullDatabaseMetadata> aMetadata,
                    NotNull<Database*> aDatabase)
      : mMetadata(std::move(aMetadata)) {
    MOZ_COUNT_CTOR(DatabaseActorInfo);

    mLiveDatabases.AppendElement(aDatabase);
  }

 private:
  ~DatabaseActorInfo() {
    MOZ_ASSERT(mLiveDatabases.IsEmpty());
    MOZ_ASSERT(!mWaitingFactoryOp || !mWaitingFactoryOp->HasBlockedDatabases());

    MOZ_COUNT_DTOR(DatabaseActorInfo);
  }
};

class DatabaseLoggingInfo final {
#ifdef DEBUG
  // Just for potential warnings.
  friend class Factory;
#endif

  LoggingInfo mLoggingInfo;

 public:
  explicit DatabaseLoggingInfo(const LoggingInfo& aLoggingInfo)
      : mLoggingInfo(aLoggingInfo) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(aLoggingInfo.nextTransactionSerialNumber());
    MOZ_ASSERT(aLoggingInfo.nextVersionChangeTransactionSerialNumber());
    MOZ_ASSERT(aLoggingInfo.nextRequestSerialNumber());
  }

  const nsID& Id() const {
    AssertIsOnBackgroundThread();

    return mLoggingInfo.backgroundChildLoggingId();
  }

  int64_t NextTransactionSN(IDBTransaction::Mode aMode) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mLoggingInfo.nextTransactionSerialNumber() < INT64_MAX);
    MOZ_ASSERT(mLoggingInfo.nextVersionChangeTransactionSerialNumber() >
               INT64_MIN);

    if (aMode == IDBTransaction::Mode::VersionChange) {
      return mLoggingInfo.nextVersionChangeTransactionSerialNumber()--;
    }

    return mLoggingInfo.nextTransactionSerialNumber()++;
  }

  uint64_t NextRequestSN() {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mLoggingInfo.nextRequestSerialNumber() < UINT64_MAX);

    return mLoggingInfo.nextRequestSerialNumber()++;
  }

  NS_INLINE_DECL_REFCOUNTING(DatabaseLoggingInfo)

 private:
  ~DatabaseLoggingInfo();
};

class QuotaClient final : public mozilla::dom::quota::Client {
  friend class GetDatabasesOp;

  static QuotaClient* sInstance;

  nsCOMPtr<nsIEventTarget> mBackgroundThread;
  nsCOMPtr<nsITimer> mDeleteTimer;
  nsTArray<RefPtr<Maintenance>> mMaintenanceQueue;
  RefPtr<Maintenance> mCurrentMaintenance;
  RefPtr<nsThreadPool> mMaintenanceThreadPool;
  nsClassHashtable<nsRefPtrHashKey<DatabaseFileManager>, nsTArray<int64_t>>
      mPendingDeleteInfos;

 public:
  QuotaClient();

  static QuotaClient* GetInstance() {
    AssertIsOnBackgroundThread();

    return sInstance;
  }

  nsIEventTarget* BackgroundThread() const {
    MOZ_ASSERT(mBackgroundThread);
    return mBackgroundThread;
  }

  nsresult AsyncDeleteFile(DatabaseFileManager* aFileManager, int64_t aFileId);

  nsresult FlushPendingFileDeletions();

  RefPtr<BoolPromise> DoMaintenance();

  RefPtr<Maintenance> GetCurrentMaintenance() const {
    return mCurrentMaintenance;
  }

  void NoteFinishedMaintenance(Maintenance* aMaintenance) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(aMaintenance);
    MOZ_ASSERT(mCurrentMaintenance == aMaintenance);

    mCurrentMaintenance = nullptr;

    QuotaManager::MaybeRecordQuotaClientShutdownStep(quota::Client::IDB,
                                                     "Maintenance finished"_ns);

    ProcessMaintenanceQueue();
  }

  nsThreadPool* GetOrCreateThreadPool();

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(mozilla::dom::indexedDB::QuotaClient,
                                        override)

  mozilla::dom::quota::Client::Type GetType() override;

  nsresult UpgradeStorageFrom1_0To2_0(nsIFile* aDirectory) override;

  nsresult UpgradeStorageFrom2_1To2_2(nsIFile* aDirectory) override;

  Result<UsageInfo, nsresult> InitOrigin(PersistenceType aPersistenceType,
                                         const OriginMetadata& aOriginMetadata,
                                         const AtomicBool& aCanceled) override;

  nsresult InitOriginWithoutTracking(PersistenceType aPersistenceType,
                                     const OriginMetadata& aOriginMetadata,
                                     const AtomicBool& aCanceled) override;

  Result<UsageInfo, nsresult> GetUsageForOrigin(
      PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
      const AtomicBool& aCanceled) override;

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

  static void DeleteTimerCallback(nsITimer* aTimer, void* aClosure);

  void AbortAllMaintenances();

  Result<nsCOMPtr<nsIFile>, nsresult> GetDirectory(
      const OriginMetadata& aOriginMetadata);

  struct SubdirectoriesToProcessAndDatabaseFilenames {
    AutoTArray<nsString, 20> subdirsToProcess;
    nsTHashSet<nsString> databaseFilenames{20};
  };

  struct SubdirectoriesToProcessAndDatabaseFilenamesAndObsoleteFilenames {
    AutoTArray<nsString, 20> subdirsToProcess;
    nsTHashSet<nsString> databaseFilenames{20};
    nsTHashSet<nsString> obsoleteFilenames{20};
  };

  enum class ObsoleteFilenamesHandling { Include, Omit };

  template <ObsoleteFilenamesHandling ObsoleteFilenames>
  using GetDatabaseFilenamesResult = std::conditional_t<
      ObsoleteFilenames == ObsoleteFilenamesHandling::Include,
      SubdirectoriesToProcessAndDatabaseFilenamesAndObsoleteFilenames,
      SubdirectoriesToProcessAndDatabaseFilenames>;

  // Returns a two-part or three-part structure:
  //
  // The first part is an array of subdirectories to process.
  //
  // The second part is a hashtable of database filenames.
  //
  // When ObsoleteFilenames is ObsoleteFilenamesHandling::Include, will also
  // collect files based on the marker files. For now,
  // GetUsageForOriginInternal() is the only consumer of this result because it
  // checks those unfinished deletion and clean them up after that.
  template <ObsoleteFilenamesHandling ObsoleteFilenames =
                ObsoleteFilenamesHandling::Omit>
  Result<GetDatabaseFilenamesResult<ObsoleteFilenames>,
         nsresult> static GetDatabaseFilenames(nsIFile& aDirectory,
                                               const AtomicBool& aCanceled);

  nsresult GetUsageForOriginInternal(PersistenceType aPersistenceType,
                                     const OriginMetadata& aOriginMetadata,
                                     const AtomicBool& aCanceled,
                                     bool aInitializing, UsageInfo* aUsageInfo);

  // Runs on the PBackground thread. Checks to see if there's a queued
  // Maintenance to run.
  void ProcessMaintenanceQueue();
};

class DeleteFilesRunnable final : public Runnable {
  using ClientDirectoryLock = mozilla::dom::quota::ClientDirectoryLock;

  enum State {
    // Just created on the PBackground thread. Next step is
    // State_DirectoryOpenPending.
    State_Initial,

    // Waiting for directory open allowed on the main thread. The next step is
    // State_DatabaseWorkOpen.
    State_DirectoryOpenPending,

    // Waiting to do/doing work on the QuotaManager IO thread. The next step is
    // State_UnblockingOpen.
    State_DatabaseWorkOpen,

    // Notifying the QuotaManager that it can proceed to the next operation on
    // the main thread. Next step is State_Completed.
    State_UnblockingOpen,

    // All done.
    State_Completed
  };

  nsCOMPtr<nsIEventTarget> mOwningEventTarget;
  SafeRefPtr<DatabaseFileManager> mFileManager;
  ClientDirectoryLockHandle mDirectoryLockHandle;
  nsTArray<int64_t> mFileIds;
  State mState;
  DEBUGONLY(bool mDEBUGCountsAsPending = false);

  static uint64_t sPendingRunnables;

 public:
  DeleteFilesRunnable(SafeRefPtr<DatabaseFileManager> aFileManager,
                      nsTArray<int64_t>&& aFileIds);

  void RunImmediately();

  static bool IsDeletionPending() { return sPendingRunnables > 0; }

 private:
#ifdef DEBUG
  ~DeleteFilesRunnable();
#else
  ~DeleteFilesRunnable() = default;
#endif

  void Open();

  void DoDatabaseWork();

  void Finish();

  void UnblockOpen();

  NS_DECL_NSIRUNNABLE

  void DirectoryLockAcquired(ClientDirectoryLockHandle aLockHandle);

  void DirectoryLockFailed();
};

class Maintenance final : public Runnable {
  struct DirectoryInfo final {
    InitializedOnce<const OriginMetadata> mOriginMetadata;
    InitializedOnce<const nsTArray<nsString>> mDatabasePaths;
    const PersistenceType mPersistenceType;

    DirectoryInfo(PersistenceType aPersistenceType,
                  OriginMetadata aOriginMetadata,
                  nsTArray<nsString>&& aDatabasePaths);

    DirectoryInfo(const DirectoryInfo& aOther) = delete;
    DirectoryInfo(DirectoryInfo&& aOther) = delete;

    ~DirectoryInfo() { MOZ_COUNT_DTOR(Maintenance::DirectoryInfo); }
  };

  enum class State {
    // Newly created on the PBackground thread. Will proceed immediately or be
    // added to the maintenance queue. The next step is either
    // DirectoryOpenPending if IndexedDatabaseManager is running, or
    // CreateIndexedDatabaseManager if not.
    Initial = 0,

    // Create IndexedDatabaseManager on the main thread. The next step is either
    // Finishing if IndexedDatabaseManager initialization fails, or
    // IndexedDatabaseManagerOpen if initialization succeeds.
    CreateIndexedDatabaseManager,

    // Call OpenDirectory() on the PBackground thread. The next step is
    // DirectoryOpenPending.
    IndexedDatabaseManagerOpen,

    // Waiting for directory open allowed on the PBackground thread. The next
    // step is either Finishing if directory lock failed to acquire, or
    // DirectoryWorkOpen if directory lock is acquired.
    DirectoryOpenPending,

    // Waiting to do/doing work on the QuotaManager IO thread. The next step is
    // BeginDatabaseMaintenance.
    DirectoryWorkOpen,

    // Dispatching a runnable for each database on the PBackground thread. The
    // next state is either WaitingForDatabaseMaintenancesToComplete if at least
    // one runnable has been dispatched, or Finishing otherwise.
    BeginDatabaseMaintenance,

    // Waiting for DatabaseMaintenance to finish on maintenance thread pool.
    // The next state is Finishing if the last runnable has finished.
    WaitingForDatabaseMaintenancesToComplete,

    // Waiting to finish/finishing on the PBackground thread. The next step is
    // Completed.
    Finishing,

    // All done.
    Complete
  };

  RefPtr<QuotaClient> mQuotaClient;
  MozPromiseHolder<BoolPromise> mPromiseHolder;
  PRTime mStartTime;
  RefPtr<UniversalDirectoryLock> mPendingDirectoryLock;
  // The directory lock is normally dropped by BeginDatabaseMaintenance, but if
  // something fails (in any method), the Finish method will do the cleanup.
  RefPtr<UniversalDirectoryLock> mDirectoryLock;
  nsTArray<nsCOMPtr<nsIRunnable>> mCompleteCallbacks;
  nsTArray<DirectoryInfo> mDirectoryInfos;
  nsTHashMap<nsStringHashKey, DatabaseMaintenance*> mDatabaseMaintenances;
  nsresult mResultCode;
  Atomic<bool> mAborted;
  bool mOpenStorageForAllRepositoriesFailed;
  State mState;

 public:
  explicit Maintenance(QuotaClient* aQuotaClient)
      : Runnable("dom::indexedDB::Maintenance"),
        mQuotaClient(aQuotaClient),
        mStartTime(PR_Now()),
        mResultCode(NS_OK),
        mAborted(false),
        mOpenStorageForAllRepositoriesFailed(false),
        mState(State::Initial) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(aQuotaClient);
    MOZ_ASSERT(QuotaClient::GetInstance() == aQuotaClient);
    MOZ_ASSERT(mStartTime);
  }

  nsIEventTarget* BackgroundThread() const {
    MOZ_ASSERT(mQuotaClient);
    return mQuotaClient->BackgroundThread();
  }

  PRTime StartTime() const { return mStartTime; }

  bool IsAborted() const { return mAborted; }

  void RunImmediately() {
    MOZ_ASSERT(mState == State::Initial);

    Unused << this->Run();
  }

  RefPtr<BoolPromise> OnResults() {
    AssertIsOnBackgroundThread();

    return mPromiseHolder.Ensure(__func__);
  }

  void Abort();

  void RegisterDatabaseMaintenance(DatabaseMaintenance* aDatabaseMaintenance);

  void UnregisterDatabaseMaintenance(DatabaseMaintenance* aDatabaseMaintenance);

  bool HasDatabaseMaintenances() const { return mDatabaseMaintenances.Count(); }

  RefPtr<DatabaseMaintenance> GetDatabaseMaintenance(
      const nsAString& aDatabasePath) const {
    AssertIsOnBackgroundThread();

    return mDatabaseMaintenances.Get(aDatabasePath);
  }

  void WaitForCompletion(nsIRunnable* aCallback) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(mDatabaseMaintenances.Count());

    mCompleteCallbacks.AppendElement(aCallback);
  }

  void Stringify(nsACString& aResult) const;

 private:
  ~Maintenance() override {
    MOZ_ASSERT(mState == State::Complete);
    MOZ_ASSERT(!mDatabaseMaintenances.Count());
  }

  // Runs on the PBackground thread. Checks if IndexedDatabaseManager is
  // running. Calls OpenDirectory() or dispatches to the main thread on which
  // CreateIndexedDatabaseManager() is called.
  nsresult Start();

  // Runs on the main thread. Once IndexedDatabaseManager is created it will
  // dispatch to the PBackground thread on which OpenDirectory() is called.
  nsresult CreateIndexedDatabaseManager();

  RefPtr<UniversalDirectoryLockPromise> OpenStorageDirectory(
      const PersistenceScope& aPersistenceScope, bool aInitializeOrigins);

  // Runs on the PBackground thread. Once QuotaManager has given a lock it will
  // call DirectoryOpen().
  nsresult OpenDirectory();

  // Runs on the PBackground thread. Dispatches to the QuotaManager I/O thread.
  nsresult DirectoryOpen();

  // Runs on the QuotaManager I/O thread. Once it finds databases it will
  // dispatch to the PBackground thread on which BeginDatabaseMaintenance()
  // is called.
  nsresult DirectoryWork();

  // Runs on the PBackground thread. It dispatches a runnable for each database.
  nsresult BeginDatabaseMaintenance();

  // Runs on the PBackground thread. Called when the maintenance is finished or
  // if any of above methods fails.
  void Finish();

  NS_DECL_NSIRUNNABLE

  void DirectoryLockAcquired(UniversalDirectoryLock* aLock);

  void DirectoryLockFailed();
};

Maintenance::DirectoryInfo::DirectoryInfo(PersistenceType aPersistenceType,
                                          OriginMetadata aOriginMetadata,
                                          nsTArray<nsString>&& aDatabasePaths)
    : mOriginMetadata(std::move(aOriginMetadata)),
      mDatabasePaths(std::move(aDatabasePaths)),
      mPersistenceType(aPersistenceType) {
  MOZ_ASSERT(aPersistenceType != PERSISTENCE_TYPE_INVALID);
  MOZ_ASSERT(!mOriginMetadata->mGroup.IsEmpty());
  MOZ_ASSERT(!mOriginMetadata->mOrigin.IsEmpty());
#ifdef DEBUG
  MOZ_ASSERT(!mDatabasePaths->IsEmpty());
  for (const nsAString& databasePath : *mDatabasePaths) {
    MOZ_ASSERT(!databasePath.IsEmpty());
  }
#endif

  MOZ_COUNT_CTOR(Maintenance::DirectoryInfo);
}

class DatabaseMaintenance final : public Runnable {
  // The minimum amount of time that has passed since the last vacuum before we
  // will attempt to analyze the database for fragmentation.
  static const PRTime kMinVacuumAge =
      PRTime(PR_USEC_PER_SEC) * 60 * 60 * 24 * 7;

  // If the percent of database pages that are not in contiguous order is higher
  // than this percentage we will attempt a vacuum.
  static const int32_t kPercentUnorderedThreshold = 30;

  // If the percent of file size growth since the last vacuum is higher than
  // this percentage we will attempt a vacuum.
  static const int32_t kPercentFileSizeGrowthThreshold = 10;

  // The number of freelist pages beyond which we will favor an incremental
  // vacuum over a full vacuum.
  static const int32_t kMaxFreelistThreshold = 5;

  // If the percent of unused file bytes in the database exceeds this percentage
  // then we will attempt a full vacuum.
  static const int32_t kPercentUnusedThreshold = 20;

  enum class MaintenanceAction { Nothing = 0, IncrementalVacuum, FullVacuum };

  RefPtr<Maintenance> mMaintenance;
  // The directory lock is dropped in RunOnOwningThread which serves as a
  // cleanup method and is always called.
  RefPtr<ClientDirectoryLock> mDirectoryLock;
  const OriginMetadata mOriginMetadata;
  const nsString mDatabasePath;
  int64_t mDirectoryLockId;
  nsCOMPtr<nsIRunnable> mCompleteCallback;
  const PersistenceType mPersistenceType;
  const Maybe<CipherKey> mMaybeKey;
  Atomic<bool> mAborted;
  DataMutex<nsCOMPtr<mozIStorageConnection>> mSharedStorageConnection;

 public:
  DatabaseMaintenance(Maintenance* aMaintenance,
                      RefPtr<ClientDirectoryLock> aDirectoryLock,
                      PersistenceType aPersistenceType,
                      const OriginMetadata& aOriginMetadata,
                      const nsAString& aDatabasePath,
                      const Maybe<CipherKey>& aMaybeKey)
      : Runnable("dom::indexedDB::DatabaseMaintenance"),
        mMaintenance(aMaintenance),
        mDirectoryLock(std::move(aDirectoryLock)),
        mOriginMetadata(aOriginMetadata),
        mDatabasePath(aDatabasePath),
        mPersistenceType(aPersistenceType),
        mMaybeKey{aMaybeKey},
        mAborted(false),
        mSharedStorageConnection("sharedStorageConnection") {
    MOZ_ASSERT(mDirectoryLock);

    MOZ_ASSERT(mDirectoryLock->Id() >= 0);
    mDirectoryLockId = mDirectoryLock->Id();
  }

  const nsAString& DatabasePath() const { return mDatabasePath; }

  void WaitForCompletion(nsIRunnable* aCallback) {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(!mCompleteCallback);

    mCompleteCallback = aCallback;
  }

  void Stringify(nsACString& aResult) const;

  nsresult Abort();

 private:
  ~DatabaseMaintenance() override = default;

  // Runs on maintenance thread pool. Does maintenance on the database.
  void PerformMaintenanceOnDatabase();

  // Runs on maintenance thread pool as part of PerformMaintenanceOnDatabase.
  nsresult CheckIntegrity(mozIStorageConnection& aConnection, bool* aOk);

  // Runs on maintenance thread pool as part of PerformMaintenanceOnDatabase.
  nsresult DetermineMaintenanceAction(mozIStorageConnection& aConnection,
                                      nsIFile* aDatabaseFile,
                                      MaintenanceAction* aMaintenanceAction);

  // Runs on maintenance thread pool as part of PerformMaintenanceOnDatabase.
  void IncrementalVacuum(mozIStorageConnection& aConnection);

  // Runs on maintenance thread pool as part of PerformMaintenanceOnDatabase.
  void FullVacuum(mozIStorageConnection& aConnection, nsIFile* aDatabaseFile);

  // Runs on the PBackground thread. It dispatches a complete callback and
  // unregisters from Maintenance.
  void RunOnOwningThread();

  // Runs on maintenance thread pool. Once it performs database maintenance
  // it will dispatch to the PBackground thread on which RunOnOwningThread()
  // is called.
  void RunOnConnectionThread();

  // TODO: Could QuotaClient::IsShuttingDownOnNonBackgroundThread() call
  // be part of mMaintenance::IsAborted() ?
  inline bool IsAborted() const {
    return mMaintenance->IsAborted() || mAborted ||
           NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread());
  }

  NS_DECL_NSIRUNNABLE
};

#ifdef DEBUG

class DEBUGThreadSlower final : public nsIThreadObserver {
 public:
  DEBUGThreadSlower() {
    AssertIsOnBackgroundThread();
    MOZ_ASSERT(kDEBUGThreadSleepMS);
  }

  NS_DECL_ISUPPORTS

 private:
  ~DEBUGThreadSlower() { AssertIsOnBackgroundThread(); }

  NS_DECL_NSITHREADOBSERVER
};

#endif  // DEBUG

/*******************************************************************************
 * Helper classes
 ******************************************************************************/

// XXX Get rid of FileHelper and move the functions into DatabaseFileManager.
// Then, DatabaseFileManager::Get(Journal)Directory and
// DatabaseFileManager::GetFileForId might eventually be made private.
class MOZ_STACK_CLASS FileHelper final {
  const SafeRefPtr<DatabaseFileManager> mFileManager;

  LazyInitializedOnce<const NotNull<nsCOMPtr<nsIFile>>> mFileDirectory;
  LazyInitializedOnce<const NotNull<nsCOMPtr<nsIFile>>> mJournalDirectory;

  class ReadCallback;
  LazyInitializedOnce<const NotNull<RefPtr<ReadCallback>>> mReadCallback;

 public:
  explicit FileHelper(SafeRefPtr<DatabaseFileManager>&& aFileManager)
      : mFileManager(std::move(aFileManager)) {
    MOZ_ASSERT(mFileManager);
  }

  nsresult Init();

  [[nodiscard]] nsCOMPtr<nsIFile> GetFile(const DatabaseFileInfo& aFileInfo);

  [[nodiscard]] nsCOMPtr<nsIFile> GetJournalFile(
      const DatabaseFileInfo& aFileInfo);

  nsresult CreateFileFromStream(nsIFile& aFile, nsIFile& aJournalFile,
                                nsIInputStream& aInputStream, bool aCompress,
                                const Maybe<CipherKey>& aMaybeKey);

 private:
  nsresult SyncCopy(nsIInputStream& aInputStream,
                    nsIOutputStream& aOutputStream, char* aBuffer,
                    uint32_t aBufferSize);

  nsresult SyncRead(nsIInputStream& aInputStream, char* aBuffer,
                    uint32_t aBufferSize, uint32_t* aRead);
};

/*******************************************************************************
 * Helper Functions
 ******************************************************************************/

bool GetFilenameBase(const nsAString& aFilename, const nsAString& aSuffix,
                     nsDependentSubstring& aFilenameBase) {
  MOZ_ASSERT(!aFilename.IsEmpty());
  MOZ_ASSERT(aFilenameBase.IsEmpty());

  if (!StringEndsWith(aFilename, aSuffix) ||
      aFilename.Length() == aSuffix.Length()) {
    return false;
  }

  MOZ_ASSERT(aFilename.Length() > aSuffix.Length());

  aFilenameBase.Rebind(aFilename, 0, aFilename.Length() - aSuffix.Length());
  return true;
}

class EncryptedFileBlobImpl final : public FileBlobImpl {
 public:
  EncryptedFileBlobImpl(const nsCOMPtr<nsIFile>& aNativeFile,
                        const DatabaseFileInfo::IdType aId,
                        const CipherKey& aKey)
      : FileBlobImpl{aNativeFile}, mKey{aKey} {
    SetFileId(aId);
  }

  uint64_t GetSize(ErrorResult& aRv) override {
    nsCOMPtr<nsIInputStream> inputStream;
    CreateInputStream(getter_AddRefs(inputStream), aRv);

    if (aRv.Failed()) {
      return 0;
    }

    MOZ_ASSERT(inputStream);

    QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(inputStream, Available), 0,
                  [&aRv](const nsresult rv) { aRv = rv; });
  }

  void CreateInputStream(nsIInputStream** aInputStream,
                         ErrorResult& aRv) const override {
    nsCOMPtr<nsIInputStream> baseInputStream;
    FileBlobImpl::CreateInputStream(getter_AddRefs(baseInputStream), aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return;
    }

    *aInputStream =
        MakeAndAddRef<DecryptingInputStream<IndexedDBCipherStrategy>>(
            WrapNotNull(std::move(baseInputStream)), kEncryptedStreamBlockSize,
            mKey)
            .take();
  }

  void GetBlobImplType(nsAString& aBlobImplType) const override {
    aBlobImplType = u"EncryptedFileBlobImpl"_ns;
  }

  already_AddRefed<BlobImpl> CreateSlice(uint64_t aStart, uint64_t aLength,
                                         const nsAString& aContentType,
                                         ErrorResult& aRv) const override {
    MOZ_CRASH("Not implemented because this should be unreachable.");
  }

 private:
  const CipherKey mKey;
};

RefPtr<BlobImpl> CreateFileBlobImpl(const Database& aDatabase,
                                    const nsCOMPtr<nsIFile>& aNativeFile,
                                    const DatabaseFileInfo::IdType aId) {
  if (aDatabase.IsInPrivateBrowsing()) {
    nsCString keyId;
    keyId.AppendInt(aId);

    const auto& key =
        aDatabase.GetFileManager().MutableCipherKeyManagerRef().Get(keyId);

    MOZ_RELEASE_ASSERT(key.isSome());
    return MakeRefPtr<EncryptedFileBlobImpl>(aNativeFile, aId, *key);
  }

  auto impl = MakeRefPtr<FileBlobImpl>(aNativeFile);
  impl->SetFileId(aId);

  return impl;
}

Result<nsTArray<SerializedStructuredCloneFile>, nsresult>
SerializeStructuredCloneFiles(const SafeRefPtr<Database>& aDatabase,
                              const nsTArray<StructuredCloneFileParent>& aFiles,
                              bool aForPreprocess) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabase);

  if (aFiles.IsEmpty()) {
    return nsTArray<SerializedStructuredCloneFile>{};
  }

  const nsCOMPtr<nsIFile> directory =
      aDatabase->GetFileManager().GetCheckedDirectory();
  QM_TRY(OkIf(directory), Err(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR),
         IDB_REPORT_INTERNAL_ERR_LAMBDA);

  nsTArray<SerializedStructuredCloneFile> serializedStructuredCloneFiles;
  QM_TRY(OkIf(serializedStructuredCloneFiles.SetCapacity(aFiles.Length(),
                                                         fallible)),
         Err(NS_ERROR_OUT_OF_MEMORY));

  QM_TRY(TransformIfAbortOnErr(
      aFiles, MakeBackInserter(serializedStructuredCloneFiles),
      [aForPreprocess](const auto& file) {
        return !aForPreprocess ||
               file.Type() == StructuredCloneFileBase::eStructuredClone;
      },
      [&directory, &aDatabase, aForPreprocess](
          const auto& file) -> Result<SerializedStructuredCloneFile, nsresult> {
        const int64_t fileId = file.FileInfo().Id();
        MOZ_ASSERT(fileId > 0);

        const nsCOMPtr<nsIFile> nativeFile =
            mozilla::dom::indexedDB::DatabaseFileManager::GetCheckedFileForId(
                directory, fileId);
        QM_TRY(OkIf(nativeFile), Err(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR),
               IDB_REPORT_INTERNAL_ERR_LAMBDA);

        switch (file.Type()) {
          case StructuredCloneFileBase::eStructuredClone:
            if (!aForPreprocess) {
              return SerializedStructuredCloneFile{
                  null_t(), StructuredCloneFileBase::eStructuredClone};
            }

            [[fallthrough]];

          case StructuredCloneFileBase::eBlob: {
            const auto impl = CreateFileBlobImpl(*aDatabase, nativeFile,
                                                 file.FileInfo().Id());

            IPCBlob ipcBlob;

            // This can only fail if the child has crashed.
            QM_TRY(MOZ_TO_RESULT(IPCBlobUtils::Serialize(impl, ipcBlob)),
                   Err(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR),
                   IDB_REPORT_INTERNAL_ERR_LAMBDA);

            aDatabase->MapBlob(ipcBlob, file.FileInfoPtr());

            return SerializedStructuredCloneFile{ipcBlob, file.Type()};
          }

          case StructuredCloneFileBase::eMutableFile:
          case StructuredCloneFileBase::eWasmBytecode:
          case StructuredCloneFileBase::eWasmCompiled: {
            // Set file() to null, support for storing WebAssembly.Modules has
            // been removed in bug 1469395. Support for de-serialization of
            // WebAssembly.Modules modules has been removed in bug 1561876.
            // Support for MutableFile has been removed in bug 1500343. Full
            // removal is tracked in bug 1487479.

            return SerializedStructuredCloneFile{null_t(), file.Type()};
          }

          default:
            MOZ_CRASH("Should never get here!");
        }
      }));

  return std::move(serializedStructuredCloneFiles);
}

bool IsFileNotFoundError(const nsresult aRv) {
  return aRv == NS_ERROR_FILE_NOT_FOUND;
}

enum struct Idempotency { Yes, No };

// Delete a file, decreasing the quota usage as appropriate. If the file no
// longer exists but aIdempotency is Idempotency::Yes, success is returned,
// although quota usage can't be decreased. (With the assumption being that the
// file was already deleted prior to this logic running, and the non-existent
// file was no longer tracked by quota because it didn't exist at
// initialization time or a previous deletion call updated the usage.)
nsresult DeleteFile(nsIFile& aFile, QuotaManager* const aQuotaManager,
                    const PersistenceType aPersistenceType,
                    const OriginMetadata& aOriginMetadata,
                    const Idempotency aIdempotency) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());

  // Callers which pass Idempotency::Yes call this function without checking if
  // the file already exists (idempotent usage). QM_OR_ELSE_WARN_IF is not used
  // here since we just want to log NS_ERROR_FILE_NOT_FOUND results and not spam
  // the reports.
  // Theoretically, there should be no QM_OR_ELSE_(WARN|LOG_VERBOSE)_IF when a
  // caller passes Idempotency::No, but it's simpler when the predicate just
  // always returns false in that case.

  const auto isIgnorableError = [&aIdempotency]() -> bool (*)(nsresult) {
    if (aIdempotency == Idempotency::Yes) {
      return IsFileNotFoundError;
    }

    return [](const nsresult rv) { return false; };
  }();

  QM_TRY_INSPECT(
      const auto& fileSize,
      ([aQuotaManager, &aFile,
        isIgnorableError]() -> Result<Maybe<int64_t>, nsresult> {
        if (aQuotaManager) {
          QM_TRY_INSPECT(
              const Maybe<int64_t>& fileSize,
              QM_OR_ELSE_LOG_VERBOSE_IF(
                  // Expression.
                  MOZ_TO_RESULT_INVOKE_MEMBER(aFile, GetFileSize)
                      .map([](const int64_t val) { return Some(val); }),
                  // Predicate.
                  isIgnorableError,
                  // Fallback.
                  ErrToDefaultOk<Maybe<int64_t>>));

          // XXX Can we really assert that the file size is not 0 if
          // it existed? This might be violated by external
          // influences.
          MOZ_ASSERT(!fileSize || fileSize.value() >= 0);

          return fileSize;
        }

        return Some(int64_t(0));
      }()));

  if (!fileSize) {
    return NS_OK;
  }

  QM_TRY_INSPECT(const auto& didExist,
                 QM_OR_ELSE_LOG_VERBOSE_IF(
                     // Expression.
                     MOZ_TO_RESULT(aFile.Remove(false)).map(Some<Ok>),
                     // Predicate.
                     isIgnorableError,
                     // Fallback.
                     ErrToDefaultOk<Maybe<Ok>>));

  if (!didExist) {
    // XXX If we get here, this means that the file still existed when we
    // queried its size, but no longer when we tried to remove it. Not sure if
    // this should really be silently accepted in idempotent mode.
    return NS_OK;
  }

  if (fileSize.value() > 0) {
    MOZ_ASSERT(aQuotaManager);

    aQuotaManager->DecreaseUsageForClient(
        ClientMetadata{aOriginMetadata, Client::IDB}, fileSize.value());
  }

  return NS_OK;
}

nsresult DeleteFile(nsIFile& aDirectory, const nsAString& aFilename,
                    QuotaManager* const aQuotaManager,
                    const PersistenceType aPersistenceType,
                    const OriginMetadata& aOriginMetadata,
                    const Idempotency aIdempotent) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aFilename.IsEmpty());

  QM_TRY_INSPECT(const auto& file, CloneFileAndAppend(aDirectory, aFilename));

  return DeleteFile(*file, aQuotaManager, aPersistenceType, aOriginMetadata,
                    aIdempotent);
}

// Delete files in a directory that you think exists. If the directory doesn't
// exist, an error will not be returned, but warning telemetry will be
// generated! So only call this on directories that you know exist (idempotent
// usage, but it's not recommended).
nsresult DeleteFilesNoQuota(nsIFile& aFile) {
  AssertIsOnIOThread();

  QM_TRY_INSPECT(const auto& didExist,
                 QM_OR_ELSE_WARN_IF(
                     // Expression.
                     MOZ_TO_RESULT(aFile.Remove(true)).map(Some<Ok>),
                     // Predicate.
                     IsFileNotFoundError,
                     // Fallback.
                     ErrToDefaultOk<Maybe<Ok>>));

  Unused << didExist;

  return NS_OK;
}

nsresult DeleteFilesNoQuota(nsIFile* aDirectory, const nsAString& aFilename) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);
  MOZ_ASSERT(!aFilename.IsEmpty());

  // The current using function hasn't initialized the origin, so in here we
  // don't update the size of origin. Adding this assertion for preventing from
  // misusing.
  DebugOnly<QuotaManager*> quotaManager = QuotaManager::Get();
  MOZ_ASSERT(!quotaManager->IsTemporaryStorageInitializedInternal());

  QM_TRY_INSPECT(const auto& file, CloneFileAndAppend(*aDirectory, aFilename));

  QM_TRY(MOZ_TO_RESULT(DeleteFilesNoQuota(*file)));

  return NS_OK;
}

// CreateMarkerFile and RemoveMarkerFile are a pair of functions to indicate
// whether having removed all the files successfully. The marker file should
// be checked before executing the next operation or initialization.
Result<nsCOMPtr<nsIFile>, nsresult> CreateMarkerFile(
    nsIFile& aBaseDirectory, const nsAString& aDatabaseNameBase) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aDatabaseNameBase.IsEmpty());

  QM_TRY_INSPECT(
      const auto& markerFile,
      CloneFileAndAppend(aBaseDirectory,
                         kIdbDeletionMarkerFilePrefix + aDatabaseNameBase));

  // Callers call this function without checking if the file already exists
  // (idempotent usage). QM_OR_ELSE_WARN_IF is not used here since we just want
  // to log NS_ERROR_FILE_ALREADY_EXISTS result and not spam the reports.
  //
  // TODO: In theory if this file exists, then RemoveDatabaseFilesAndDirectory
  // should have cleaned it up, but obviously we can crash and not clean it up,
  // which is the whole point of the marker file. In that case, we'll realize
  // the marker file exists in OpenDatabaseOp::DoDatabaseWork or
  // GetUsageForOriginInternal and resume the removal by calling
  // RemoveDatabaseFilesAndDirectory again, but we will also try to create the
  // marker file again, so if we see this marker file, it is part
  // of our standard operating procedure to redundantly try and create the
  // marker here. We currently treat this as idempotent usage, but we could
  // add an additional argument to RemoveDatabaseFilesAndDirectory which would
  // indicate that we are resuming an unfinished removal, so the marker already
  // exists and doesn't have to be created, and change
  // QM_OR_ELSE_LOG_VERBOSE_IF to QM_OR_ELSE_WARN_IF in the end.
  QM_TRY(QM_OR_ELSE_LOG_VERBOSE_IF(
      // Expression.
      MOZ_TO_RESULT(markerFile->Create(nsIFile::NORMAL_FILE_TYPE, 0644)),
      // Predicate.
      IsSpecificError<NS_ERROR_FILE_ALREADY_EXISTS>,
      // Fallback.
      ErrToDefaultOk<>));

  return markerFile;
}

nsresult RemoveMarkerFile(nsIFile* aMarkerFile) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aMarkerFile);

  DebugOnly<bool> exists;
  MOZ_ASSERT(NS_SUCCEEDED(aMarkerFile->Exists(&exists)));
  MOZ_ASSERT(exists);

  QM_TRY(MOZ_TO_RESULT(aMarkerFile->Remove(false)));

  return NS_OK;
}

Result<Ok, nsresult> DeleteFileManagerDirectory(
    nsIFile& aFileManagerDirectory, QuotaManager* aQuotaManager,
    const PersistenceType aPersistenceType,
    const OriginMetadata& aOriginMetadata) {
  // XXX In theory, deleting can continue for other files in case of a failure,
  // leaving only those files behind that cause the problem actually. However,
  // the current architecture doesn't allow having more databases (for the same
  // name) on disk, so trying to delete as much as possible won't help much
  // because we need to delete entire .files directory in the end anyway.
  QM_TRY(DatabaseFileManager::TraverseFiles(
      aFileManagerDirectory,
      // KnownDirEntryOp
      [&aQuotaManager, aPersistenceType, &aOriginMetadata](
          nsIFile& file, const bool isDirectory) -> Result<Ok, nsresult> {
        if (isDirectory) {
          // The journal directory doesn't count towards quota.
          QM_TRY_RETURN(MOZ_TO_RESULT(DeleteFilesNoQuota(file)));
        }

        // Stored files do count towards quota.
        QM_TRY_RETURN(
            MOZ_TO_RESULT(DeleteFile(file, aQuotaManager, aPersistenceType,
                                     aOriginMetadata, Idempotency::Yes)));
      },
      // UnknownDirEntryOp
      [aPersistenceType, &aOriginMetadata](
          nsIFile& file, const bool isDirectory) -> Result<Ok, nsresult> {
        // Unknown files and directories don't count towards quota.

        if (isDirectory) {
          QM_TRY_RETURN(MOZ_TO_RESULT(DeleteFilesNoQuota(file)));
        }

        QM_TRY_RETURN(MOZ_TO_RESULT(
            DeleteFile(file, /* doesn't count */ nullptr, aPersistenceType,
                       aOriginMetadata, Idempotency::Yes)));
      }));

  QM_TRY_RETURN(MOZ_TO_RESULT(aFileManagerDirectory.Remove(false)));
}

// Idempotently delete all the parts of an IndexedDB database including its
// SQLite database file, its WAL journal, it's shared-memory file, and its
// Blob/Files sub-directory. A marker file is created prior to performing the
// deletion so that in the event we crash or fail to successfully delete the
// database and its files, we will re-attempt the deletion the next time the
// origin is initialized using this method. Because this means the method may be
// called on a partially deleted database, this method uses DeleteFile which
// succeeds when the file we ask it to delete does not actually exist. The
// marker file is removed once deletion has successfully completed.
nsresult RemoveDatabaseFilesAndDirectory(nsIFile& aBaseDirectory,
                                         const nsAString& aDatabaseFilenameBase,
                                         QuotaManager* aQuotaManager,
                                         const PersistenceType aPersistenceType,
                                         const OriginMetadata& aOriginMetadata,
                                         const nsAString& aDatabaseName) {
  AssertIsOnIOThread();
  MOZ_ASSERT(!aDatabaseFilenameBase.IsEmpty());

  AUTO_PROFILER_LABEL("RemoveDatabaseFilesAndDirectory", DOM);

  QM_TRY_UNWRAP(auto markerFile,
                CreateMarkerFile(aBaseDirectory, aDatabaseFilenameBase));

  // The database file counts towards quota.
  QM_TRY(MOZ_TO_RESULT(DeleteFile(
      aBaseDirectory, aDatabaseFilenameBase + kSQLiteSuffix, aQuotaManager,
      aPersistenceType, aOriginMetadata, Idempotency::Yes)));

  // .sqlite-journal files don't count towards quota.
  QM_TRY(MOZ_TO_RESULT(DeleteFile(aBaseDirectory,
                                  aDatabaseFilenameBase + kSQLiteJournalSuffix,
                                  /* doesn't count */ nullptr, aPersistenceType,
                                  aOriginMetadata, Idempotency::Yes)));

  // .sqlite-shm files don't count towards quota.
  QM_TRY(MOZ_TO_RESULT(DeleteFile(aBaseDirectory,
                                  aDatabaseFilenameBase + kSQLiteSHMSuffix,
                                  /* doesn't count */ nullptr, aPersistenceType,
                                  aOriginMetadata, Idempotency::Yes)));

  // .sqlite-wal files do count towards quota.
  QM_TRY(MOZ_TO_RESULT(DeleteFile(
      aBaseDirectory, aDatabaseFilenameBase + kSQLiteWALSuffix, aQuotaManager,
      aPersistenceType, aOriginMetadata, Idempotency::Yes)));

  // The files directory counts towards quota.
  QM_TRY_INSPECT(
      const auto& fmDirectory,
      CloneFileAndAppend(aBaseDirectory, aDatabaseFilenameBase +
                                             kFileManagerDirectoryNameSuffix));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(fmDirectory, Exists));

  if (exists) {
    QM_TRY_INSPECT(const bool& isDirectory,
                   MOZ_TO_RESULT_INVOKE_MEMBER(fmDirectory, IsDirectory));

    QM_TRY(OkIf(isDirectory), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

    QM_TRY(DeleteFileManagerDirectory(*fmDirectory, aQuotaManager,
                                      aPersistenceType, aOriginMetadata));
  }

  IndexedDatabaseManager* mgr = IndexedDatabaseManager::Get();
  MOZ_ASSERT_IF(aQuotaManager, mgr);

  if (mgr) {
    mgr->InvalidateFileManager(aPersistenceType, aOriginMetadata.mOrigin,
                               aDatabaseName);
  }

  QM_TRY(MOZ_TO_RESULT(RemoveMarkerFile(markerFile)));

  return NS_OK;
}

/*******************************************************************************
 * Globals
 ******************************************************************************/

// Counts the number of "live" Factory, FactoryOp and Database instances.
uint64_t gBusyCount = 0;

using FactoryOpArray = nsTArray<CheckedUnsafePtr<FactoryOp>>;

StaticAutoPtr<FactoryOpArray> gFactoryOps;

// Maps a database id to information about live database actors.
using DatabaseActorHashtable =
    nsClassHashtable<nsCStringHashKey, DatabaseActorInfo>;

StaticAutoPtr<DatabaseActorHashtable> gLiveDatabaseHashtable;

StaticRefPtr<ConnectionPool> gConnectionPool;

using DatabaseLoggingInfoHashtable =
    nsTHashMap<nsIDHashKey, DatabaseLoggingInfo*>;

StaticAutoPtr<DatabaseLoggingInfoHashtable> gLoggingInfoHashtable;

using TelemetryIdHashtable = nsTHashMap<nsUint32HashKey, uint32_t>;

StaticAutoPtr<TelemetryIdHashtable> gTelemetryIdHashtable;

// Protects all reads and writes to gTelemetryIdHashtable.
StaticAutoPtr<Mutex> gTelemetryIdMutex;

// For private browsing, maps the raw database names provided by content to a
// replacement UUID in order to avoid exposing the name of the database on
// disk or a directly derived value, such as the non-private-browsing
// representation. This mapping will be the same for all databases with the
// same name across all storage keys/origins for the lifetime of the IDB
// QuotaClient. In tests, the QuotaClient may be created and destroyed multiple
// times, but for normal browser use the QuotaClient will last until the
// browser shuts down. Bug 1831835 will improve this implementation to avoid
// using the same mapping across storage keys and to deal with the resulting
// lifecycle issues of the additional memory use.
using StorageDatabaseNameHashtable = nsTHashMap<nsString, nsString>;

StaticAutoPtr<StorageDatabaseNameHashtable> gStorageDatabaseNameHashtable;

// Protects all reads and writes to gStorageDatabaseNameHashtable.
StaticAutoPtr<Mutex> gStorageDatabaseNameMutex;

#ifdef DEBUG

StaticRefPtr<DEBUGThreadSlower> gDEBUGThreadSlower;

#endif  // DEBUG

void IncreaseBusyCount() {
  AssertIsOnBackgroundThread();

  // If this is the first instance then we need to do some initialization.
  if (!gBusyCount) {
    MOZ_ASSERT(!gFactoryOps);
    gFactoryOps = new FactoryOpArray();

    MOZ_ASSERT(!gLiveDatabaseHashtable);
    gLiveDatabaseHashtable = new DatabaseActorHashtable();

    MOZ_ASSERT(!gLoggingInfoHashtable);
    gLoggingInfoHashtable = new DatabaseLoggingInfoHashtable();

#ifdef DEBUG
    if (kDEBUGThreadPriority != nsISupportsPriority::PRIORITY_NORMAL) {
      NS_WARNING(
          "PBackground thread debugging enabled, priority has been "
          "modified!");
      nsCOMPtr<nsISupportsPriority> thread =
          do_QueryInterface(NS_GetCurrentThread());
      MOZ_ASSERT(thread);

      MOZ_ALWAYS_SUCCEEDS(thread->SetPriority(kDEBUGThreadPriority));
    }

    if (kDEBUGThreadSleepMS) {
      NS_WARNING(
          "PBackground thread debugging enabled, sleeping after every "
          "event!");
      nsCOMPtr<nsIThreadInternal> thread =
          do_QueryInterface(NS_GetCurrentThread());
      MOZ_ASSERT(thread);

      gDEBUGThreadSlower = new DEBUGThreadSlower();

      MOZ_ALWAYS_SUCCEEDS(thread->AddObserver(gDEBUGThreadSlower));
    }
#endif  // DEBUG
  }

  gBusyCount++;
}

void DecreaseBusyCount() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(gBusyCount);

  // Clean up if there are no more instances.
  if (--gBusyCount == 0) {
    MOZ_ASSERT(gLoggingInfoHashtable);
    gLoggingInfoHashtable = nullptr;

    MOZ_ASSERT(gLiveDatabaseHashtable);
    MOZ_ASSERT(!gLiveDatabaseHashtable->Count());
    gLiveDatabaseHashtable = nullptr;

    MOZ_ASSERT(gFactoryOps);
    MOZ_ASSERT(gFactoryOps->IsEmpty());
    gFactoryOps = nullptr;

#ifdef DEBUG
    if (kDEBUGThreadPriority != nsISupportsPriority::PRIORITY_NORMAL) {
      nsCOMPtr<nsISupportsPriority> thread =
          do_QueryInterface(NS_GetCurrentThread());
      MOZ_ASSERT(thread);

      MOZ_ALWAYS_SUCCEEDS(
          thread->SetPriority(nsISupportsPriority::PRIORITY_NORMAL));
    }

    if (kDEBUGThreadSleepMS) {
      MOZ_ASSERT(gDEBUGThreadSlower);

      nsCOMPtr<nsIThreadInternal> thread =
          do_QueryInterface(NS_GetCurrentThread());
      MOZ_ASSERT(thread);

      MOZ_ALWAYS_SUCCEEDS(thread->RemoveObserver(gDEBUGThreadSlower));

      gDEBUGThreadSlower = nullptr;
    }
#endif  // DEBUG
  }
}

template <typename Condition>
void InvalidateLiveDatabasesMatching(const Condition& aCondition) {
  AssertIsOnBackgroundThread();

  if (!gLiveDatabaseHashtable) {
    return;
  }

  // Invalidating a Database will cause it to be removed from the
  // gLiveDatabaseHashtable entries' mLiveDatabases, and, if it was the last
  // element in mLiveDatabases, to remove the whole hashtable entry. Therefore,
  // we need to make a temporary list of the databases to invalidate to avoid
  // iterator invalidation.

  nsTArray<SafeRefPtr<Database>> databases;

  for (const auto& liveDatabasesEntry : gLiveDatabaseHashtable->Values()) {
    for (const auto& database : liveDatabasesEntry->mLiveDatabases) {
      if (aCondition(*database)) {
        databases.AppendElement(
            SafeRefPtr{database.get(), AcquireStrongRefFromRawPtr{}});
      }
    }
  }

  for (const auto& database : databases) {
    database->Invalidate();
  }
}

uint32_t TelemetryIdForFile(nsIFile* aFile) {
  // May be called on any thread!

  MOZ_ASSERT(aFile);
  MOZ_ASSERT(gTelemetryIdMutex);

  // The storage directory is structured like this:
  //
  //   <profile>/storage/<persistence>/<origin>/idb/<filename>.sqlite
  //
  // For the purposes of this function we're only concerned with the
  // <persistence>, <origin>, and <filename> pieces.

  nsString filename;
  MOZ_ALWAYS_SUCCEEDS(aFile->GetLeafName(filename));

  // Make sure we were given a database file.
  MOZ_ASSERT(StringEndsWith(filename, kSQLiteSuffix));

  filename.Truncate(filename.Length() - kSQLiteSuffix.Length());

  // Get the "idb" directory.
  nsCOMPtr<nsIFile> idbDirectory;
  MOZ_ALWAYS_SUCCEEDS(aFile->GetParent(getter_AddRefs(idbDirectory)));

  DebugOnly<nsString> idbLeafName;
  MOZ_ASSERT(NS_SUCCEEDED(idbDirectory->GetLeafName(idbLeafName)));
  MOZ_ASSERT(static_cast<nsString&>(idbLeafName).EqualsLiteral("idb"));

  // Get the <origin> directory.
  nsCOMPtr<nsIFile> originDirectory;
  MOZ_ALWAYS_SUCCEEDS(idbDirectory->GetParent(getter_AddRefs(originDirectory)));

  nsString origin;
  MOZ_ALWAYS_SUCCEEDS(originDirectory->GetLeafName(origin));

  // Any databases in these directories are owned by the application and should
  // not have their filenames masked. Hopefully they also appear in the
  // Telemetry.cpp whitelist.
  if (origin.EqualsLiteral("chrome") ||
      origin.EqualsLiteral("moz-safe-about+home")) {
    return 0;
  }

  // Get the <persistence> directory.
  nsCOMPtr<nsIFile> persistenceDirectory;
  MOZ_ALWAYS_SUCCEEDS(
      originDirectory->GetParent(getter_AddRefs(persistenceDirectory)));

  nsString persistence;
  MOZ_ALWAYS_SUCCEEDS(persistenceDirectory->GetLeafName(persistence));

  constexpr auto separator = u"*"_ns;

  uint32_t hashValue =
      HashString(persistence + separator + origin + separator + filename);

  MutexAutoLock lock(*gTelemetryIdMutex);

  if (!gTelemetryIdHashtable) {
    gTelemetryIdHashtable = new TelemetryIdHashtable();
  }

  return gTelemetryIdHashtable->LookupOrInsertWith(hashValue, [] {
    static uint32_t sNextId = 1;

    // We're locked, no need for atomics.
    return sNextId++;
  });
}

nsAutoString GetDatabaseFilenameBase(const nsAString& aDatabaseName,
                                     bool aIsPrivate) {
  nsAutoString databaseFilenameBase;

  if (aIsPrivate) {
    MOZ_DIAGNOSTIC_ASSERT(gStorageDatabaseNameMutex);

    MutexAutoLock lock(*gStorageDatabaseNameMutex);

    if (!gStorageDatabaseNameHashtable) {
      gStorageDatabaseNameHashtable = new StorageDatabaseNameHashtable();
    }

    databaseFilenameBase.Append(
        gStorageDatabaseNameHashtable->LookupOrInsertWith(aDatabaseName, []() {
          return NSID_TrimBracketsUTF16(nsID::GenerateUUID());
        }));

    return databaseFilenameBase;
  }

  // WARNING: do not change this hash function. See the comment in HashName()
  // for details.
  databaseFilenameBase.AppendInt(HashName(aDatabaseName));

  nsAutoCString escapedName;
  if (!NS_Escape(NS_ConvertUTF16toUTF8(aDatabaseName), escapedName,
                 url_XPAlphas)) {
    MOZ_CRASH("Can't escape database name!");
  }

  const char* forwardIter = escapedName.BeginReading();
  const char* backwardIter = escapedName.EndReading() - 1;

  nsAutoCString substring;
  while (forwardIter <= backwardIter && substring.Length() < 21) {
    if (substring.Length() % 2) {
      substring.Append(*backwardIter--);
    } else {
      substring.Append(*forwardIter++);
    }
  }

  databaseFilenameBase.AppendASCII(substring.get(), substring.Length());

  return databaseFilenameBase;
}

const CommonIndexOpenCursorParams& GetCommonIndexOpenCursorParams(
    const OpenCursorParams& aParams) {
  switch (aParams.type()) {
    case OpenCursorParams::TIndexOpenCursorParams:
      return aParams.get_IndexOpenCursorParams().commonIndexParams();
    case OpenCursorParams::TIndexOpenKeyCursorParams:
      return aParams.get_IndexOpenKeyCursorParams().commonIndexParams();
    default:
      MOZ_CRASH("Should never get here!");
  }
}

const CommonOpenCursorParams& GetCommonOpenCursorParams(
    const OpenCursorParams& aParams) {
  switch (aParams.type()) {
    case OpenCursorParams::TObjectStoreOpenCursorParams:
      return aParams.get_ObjectStoreOpenCursorParams().commonParams();
    case OpenCursorParams::TObjectStoreOpenKeyCursorParams:
      return aParams.get_ObjectStoreOpenKeyCursorParams().commonParams();
    case OpenCursorParams::TIndexOpenCursorParams:
    case OpenCursorParams::TIndexOpenKeyCursorParams:
      return GetCommonIndexOpenCursorParams(aParams).commonParams();
    default:
      MOZ_CRASH("Should never get here!");
  }
}

// TODO: Using nsCString as a return type here seems to lead to a dependency on
// some temporaries, which I did not expect. Is it a good idea that the default
// operator+ behaviour constructs such strings? It is certainly useful as an
// optimization, but this should be better done via an appropriately named
// function rather than an operator.
nsAutoCString MakeColumnPairSelectionList(
    const nsLiteralCString& aPlainColumnName,
    const nsLiteralCString& aLocaleAwareColumnName,
    const nsLiteralCString& aSortColumnAlias, const bool aIsLocaleAware) {
  return aPlainColumnName +
         (aIsLocaleAware ? EmptyCString() : " as "_ns + aSortColumnAlias) +
         ", "_ns + aLocaleAwareColumnName +
         (aIsLocaleAware ? " as "_ns + aSortColumnAlias : EmptyCString());
}

constexpr bool IsIncreasingOrder(const IDBCursorDirection aDirection) {
  MOZ_ASSERT(aDirection == IDBCursorDirection::Next ||
             aDirection == IDBCursorDirection::Nextunique ||
             aDirection == IDBCursorDirection::Prev ||
             aDirection == IDBCursorDirection::Prevunique);

  return aDirection == IDBCursorDirection::Next ||
         aDirection == IDBCursorDirection::Nextunique;
}

constexpr bool IsUnique(const IDBCursorDirection aDirection) {
  MOZ_ASSERT(aDirection == IDBCursorDirection::Next ||
             aDirection == IDBCursorDirection::Nextunique ||
             aDirection == IDBCursorDirection::Prev ||
             aDirection == IDBCursorDirection::Prevunique);

  return aDirection == IDBCursorDirection::Nextunique ||
         aDirection == IDBCursorDirection::Prevunique;
}

// TODO: In principle, this could be constexpr, if operator+(nsLiteralCString,
// nsLiteralCString) were constexpr and returned a literal type.
nsAutoCString MakeDirectionClause(const IDBCursorDirection aDirection) {
  return " ORDER BY "_ns + kColumnNameKey +
         (IsIncreasingOrder(aDirection) ? " ASC"_ns : " DESC"_ns);
}

enum struct ComparisonOperator {
  LessThan,
  LessOrEquals,
  Equals,
  GreaterThan,
  GreaterOrEquals,
};

constexpr nsLiteralCString GetComparisonOperatorString(
    const ComparisonOperator aComparisonOperator) {
  switch (aComparisonOperator) {
    case ComparisonOperator::LessThan:
      return "<"_ns;
    case ComparisonOperator::LessOrEquals:
      return "<="_ns;
    case ComparisonOperator::Equals:
      return "=="_ns;
    case ComparisonOperator::GreaterThan:
      return ">"_ns;
    case ComparisonOperator::GreaterOrEquals:
      return ">="_ns;
  }

  // TODO: This is just to silence the "control reaches end of non-void
  // function" warning. Cannot use MOZ_CRASH in a constexpr function,
  // unfortunately.
  return ""_ns;
}

nsAutoCString GetKeyClause(const nsACString& aColumnName,
                           const ComparisonOperator aComparisonOperator,
                           const nsLiteralCString& aStmtParamName) {
  return aColumnName + " "_ns +
         GetComparisonOperatorString(aComparisonOperator) + " :"_ns +
         aStmtParamName;
}

nsAutoCString GetSortKeyClause(const ComparisonOperator aComparisonOperator,
                               const nsLiteralCString& aStmtParamName) {
  return GetKeyClause(kColumnNameAliasSortKey, aComparisonOperator,
                      aStmtParamName);
}

template <IDBCursorType CursorType>
struct PopulateResponseHelper;

struct CommonPopulateResponseHelper {
  explicit CommonPopulateResponseHelper(
      const TransactionDatabaseOperationBase& aOp)
      : mOp{aOp} {}

  nsresult GetKeys(mozIStorageStatement* const aStmt,
                   Key* const aOptOutSortKey) {
    QM_TRY(MOZ_TO_RESULT(GetCommonKeys(aStmt)));

    if (aOptOutSortKey) {
      *aOptOutSortKey = mPosition;
    }

    return NS_OK;
  }

  nsresult GetCommonKeys(mozIStorageStatement* const aStmt) {
    MOZ_ASSERT(mPosition.IsUnset());

    QM_TRY(MOZ_TO_RESULT(mPosition.SetFromStatement(aStmt, 0)));

    IDB_LOG_MARK_PARENT_TRANSACTION_REQUEST(
        "PRELOAD: Populating response with key %s", "Populating%.0s",
        IDB_LOG_ID_STRING(mOp.BackgroundChildLoggingId()),
        mOp.TransactionLoggingSerialNumber(), mOp.LoggingSerialNumber(),
        mPosition.GetBuffer().get());

    return NS_OK;
  }

  template <typename Response>
  void FillKeys(Response& aResponse) {
    MOZ_ASSERT(!mPosition.IsUnset());
    aResponse.key() = std::move(mPosition);
  }

  template <typename Response>
  static size_t GetKeySize(const Response& aResponse) {
    return aResponse.key().GetBuffer().Length();
  }

 protected:
  const Key& GetPosition() const { return mPosition; }

 private:
  const TransactionDatabaseOperationBase& mOp;
  Key mPosition;
};

struct IndexPopulateResponseHelper : CommonPopulateResponseHelper {
  using CommonPopulateResponseHelper::CommonPopulateResponseHelper;

  nsresult GetKeys(mozIStorageStatement* const aStmt,
                   Key* const aOptOutSortKey) {
    MOZ_ASSERT(mLocaleAwarePosition.IsUnset());
    MOZ_ASSERT(mObjectStorePosition.IsUnset());

    QM_TRY(MOZ_TO_RESULT(CommonPopulateResponseHelper::GetCommonKeys(aStmt)));

    QM_TRY(MOZ_TO_RESULT(mLocaleAwarePosition.SetFromStatement(aStmt, 1)));

    QM_TRY(MOZ_TO_RESULT(mObjectStorePosition.SetFromStatement(aStmt, 2)));

    if (aOptOutSortKey) {
      *aOptOutSortKey =
          mLocaleAwarePosition.IsUnset() ? GetPosition() : mLocaleAwarePosition;
    }

    return NS_OK;
  }

  template <typename Response>
  void FillKeys(Response& aResponse) {
    MOZ_ASSERT(!mLocaleAwarePosition.IsUnset());
    MOZ_ASSERT(!mObjectStorePosition.IsUnset());

    CommonPopulateResponseHelper::FillKeys(aResponse);
    aResponse.sortKey() = std::move(mLocaleAwarePosition);
    aResponse.objectKey() = std::move(mObjectStorePosition);
  }

  template <typename Response>
  static size_t GetKeySize(Response& aResponse) {
    return CommonPopulateResponseHelper::GetKeySize(aResponse) +
           aResponse.sortKey().GetBuffer().Length() +
           aResponse.objectKey().GetBuffer().Length();
  }

 private:
  Key mLocaleAwarePosition, mObjectStorePosition;
};

struct KeyPopulateResponseHelper {
  static constexpr nsresult MaybeGetCloneInfo(
      mozIStorageStatement* const /*aStmt*/, const CursorBase& /*aCursor*/) {
    return NS_OK;
  }

  template <typename Response>
  static constexpr void MaybeFillCloneInfo(Response& /*aResponse*/,
                                           FilesArray* const /*aFiles*/) {}

  template <typename Response>
  static constexpr size_t MaybeGetCloneInfoSize(const Response& /*aResponse*/) {
    return 0;
  }
};

template <bool StatementHasIndexKeyBindings>
struct ValuePopulateResponseHelper {
  nsresult MaybeGetCloneInfo(mozIStorageStatement* const aStmt,
                             const ValueCursorBase& aCursor) {
    constexpr auto offset = StatementHasIndexKeyBindings ? 2 : 0;

    QM_TRY_UNWRAP(auto cloneInfo,
                  GetStructuredCloneReadInfoFromStatement(
                      aStmt, 2 + offset, 1 + offset, *aCursor.mFileManager));

    mCloneInfo.init(std::move(cloneInfo));

    if (mCloneInfo->HasPreprocessInfo()) {
      IDB_WARNING("Preprocessing for cursors not yet implemented!");
      return NS_ERROR_NOT_IMPLEMENTED;
    }

    return NS_OK;
  }

  template <typename Response>
  void MaybeFillCloneInfo(Response& aResponse, FilesArray* const aFiles) {
    auto cloneInfo = mCloneInfo.release();
    aResponse.cloneInfo().data().data = cloneInfo.ReleaseData();
    aFiles->AppendElement(cloneInfo.ReleaseFiles());
  }

  template <typename Response>
  static size_t MaybeGetCloneInfoSize(const Response& aResponse) {
    return aResponse.cloneInfo().data().data.Size();
  }

 private:
  LazyInitializedOnceEarlyDestructible<const StructuredCloneReadInfoParent>
      mCloneInfo;
};

template <>
struct PopulateResponseHelper<IDBCursorType::ObjectStore>
    : ValuePopulateResponseHelper<false>, CommonPopulateResponseHelper {
  using CommonPopulateResponseHelper::CommonPopulateResponseHelper;

  static auto& GetTypedResponse(CursorResponse* const aResponse) {
    return aResponse->get_ArrayOfObjectStoreCursorResponse();
  }
};

template <>
struct PopulateResponseHelper<IDBCursorType::ObjectStoreKey>
    : KeyPopulateResponseHelper, CommonPopulateResponseHelper {
  using CommonPopulateResponseHelper::CommonPopulateResponseHelper;

  static auto& GetTypedResponse(CursorResponse* const aResponse) {
    return aResponse->get_ArrayOfObjectStoreKeyCursorResponse();
  }
};

template <>
struct PopulateResponseHelper<IDBCursorType::Index>
    : ValuePopulateResponseHelper<true>, IndexPopulateResponseHelper {
  using IndexPopulateResponseHelper::IndexPopulateResponseHelper;

  static auto& GetTypedResponse(CursorResponse* const aResponse) {
    return aResponse->get_ArrayOfIndexCursorResponse();
  }
};

template <>
struct PopulateResponseHelper<IDBCursorType::IndexKey>
    : KeyPopulateResponseHelper, IndexPopulateResponseHelper {
  using IndexPopulateResponseHelper::IndexPopulateResponseHelper;

  static auto& GetTypedResponse(CursorResponse* const aResponse) {
    return aResponse->get_ArrayOfIndexKeyCursorResponse();
  }
};

nsresult DispatchAndReturnFileReferences(
    PersistenceType aPersistenceType, const nsACString& aOrigin,
    const nsAString& aDatabaseName, const int64_t aFileId,
    int32_t* const aMemRefCnt, int32_t* const aDBRefCnt, bool* const aResult) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aMemRefCnt);
  MOZ_ASSERT(aDBRefCnt);
  MOZ_ASSERT(aResult);

  *aResult = false;
  *aMemRefCnt = -1;
  *aDBRefCnt = -1;

  mozilla::Monitor monitor MOZ_ANNOTATED(__func__);
  bool waiting = true;

  auto lambda = [&] {
    AssertIsOnIOThread();

    {
      IndexedDatabaseManager* const mgr = IndexedDatabaseManager::Get();
      MOZ_ASSERT(mgr);

      const SafeRefPtr<DatabaseFileManager> fileManager =
          mgr->GetFileManager(aPersistenceType, aOrigin, aDatabaseName);

      if (fileManager) {
        const SafeRefPtr<DatabaseFileInfo> fileInfo =
            fileManager->GetFileInfo(aFileId);

        if (fileInfo) {
          fileInfo->GetReferences(aMemRefCnt, aDBRefCnt);

          if (*aMemRefCnt != -1) {
            // We added an extra temp ref, so account for that accordingly.
            (*aMemRefCnt)--;
          }

          *aResult = true;
        }
      }
    }

    mozilla::MonitorAutoLock lock(monitor);
    MOZ_ASSERT(waiting);

    waiting = false;
    lock.Notify();
  };

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  // XXX can't we simply use NS_DispatchAndSpinEventLoopUntilComplete instead of
  // using a monitor?
  QM_TRY(MOZ_TO_RESULT(quotaManager->IOThread()->Dispatch(
      NS_NewRunnableFunction("GetFileReferences", std::move(lambda)),
      NS_DISPATCH_NORMAL)));

  mozilla::MonitorAutoLock autolock(monitor);
  while (waiting) {
    autolock.Wait();
  }

  return NS_OK;
}

class DeserializeIndexValueHelper final : public Runnable {
 public:
  DeserializeIndexValueHelper(int64_t aIndexID, const KeyPath& aKeyPath,
                              bool aMultiEntry, const nsACString& aLocale,
                              StructuredCloneReadInfoParent& aCloneReadInfo,
                              nsTArray<IndexUpdateInfo>& aUpdateInfoArray)
      : Runnable("DeserializeIndexValueHelper"),
        mMonitor("DeserializeIndexValueHelper::mMonitor"),
        mIndexID(aIndexID),
        mKeyPath(aKeyPath),
        mMultiEntry(aMultiEntry),
        mLocale(aLocale),
        mCloneReadInfo(aCloneReadInfo),
        mUpdateInfoArray(aUpdateInfoArray),
        mStatus(NS_ERROR_FAILURE) {}

  nsresult DispatchAndWait() {
    // FIXME(Bug 1637530) Re-enable optimization using a non-system-principaled
    // JS context
#if 0
    // We don't need to go to the main-thread and use the sandbox. Let's create
    // the updateInfo data here.
    if (!mCloneReadInfo.Data().Size()) {
      AutoJSAPI jsapi;
      jsapi.Init();

      JS::Rooted<JS::Value> value(jsapi.cx());
      value.setUndefined();

      ErrorResult rv;
      IDBObjectStore::AppendIndexUpdateInfo(
          mIndexID, mKeyPath, mMultiEntry, &mUpdateInfoArray,
          /* aAutoIncrementedObjectStoreKeyPath */ VoidString(), &rv);
      return rv.Failed() ? rv.StealNSResult() : NS_OK;
    }
#endif

    // The operation will continue on the main-thread.

    MOZ_ASSERT(!(mCloneReadInfo.Data().Size() % sizeof(uint64_t)));

    MonitorAutoLock lock(mMonitor);

    RefPtr<Runnable> self = this;
    QM_TRY(MOZ_TO_RESULT(SchedulerGroup::Dispatch(self.forget())));

    lock.Wait();
    return mStatus;
  }

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(NS_IsMainThread());

    AutoJSAPI jsapi;
    jsapi.Init();
    JSContext* const cx = jsapi.cx();

    JS::Rooted<JSObject*> global(cx, GetSandbox(cx));

    QM_TRY(OkIf(global), NS_OK,
           [this](const NotOk) { OperationCompleted(NS_ERROR_FAILURE); });

    const JSAutoRealm ar(cx, global);

    JS::Rooted<JS::Value> value(cx);
    QM_TRY(MOZ_TO_RESULT(DeserializeIndexValue(cx, &value)), NS_OK,
           [this](const nsresult rv) { OperationCompleted(rv); });

    ErrorResult errorResult;
    IDBObjectStore::AppendIndexUpdateInfo(
        mIndexID, mKeyPath, mMultiEntry, mLocale, cx, value, &mUpdateInfoArray,
        /* aAutoIncrementedObjectStoreKeyPath */ VoidString(), &errorResult);
    QM_TRY(OkIf(!errorResult.Failed()), NS_OK,
           ([this, &errorResult](const NotOk) {
             OperationCompleted(errorResult.StealNSResult());
           }));

    OperationCompleted(NS_OK);
    return NS_OK;
  }

 private:
  nsresult DeserializeIndexValue(JSContext* aCx,
                                 JS::MutableHandle<JS::Value> aValue) {
    static const JSStructuredCloneCallbacks callbacks = {
        StructuredCloneReadCallback<StructuredCloneReadInfoParent>,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr};

    if (!JS_ReadStructuredClone(
            aCx, mCloneReadInfo.Data(), JS_STRUCTURED_CLONE_VERSION,
            JS::StructuredCloneScope::DifferentProcessForIndexedDB, aValue,
            JS::CloneDataPolicy(), &callbacks, &mCloneReadInfo)) {
      return NS_ERROR_DOM_DATA_CLONE_ERR;
    }

    return NS_OK;
  }

  void OperationCompleted(nsresult aStatus) {
    mStatus = aStatus;

    MonitorAutoLock lock(mMonitor);
    lock.Notify();
  }

  Monitor mMonitor MOZ_UNANNOTATED;

  const int64_t mIndexID;
  const KeyPath& mKeyPath;
  const bool mMultiEntry;
  const nsCString mLocale;
  StructuredCloneReadInfoParent& mCloneReadInfo;
  nsTArray<IndexUpdateInfo>& mUpdateInfoArray;
  nsresult mStatus;
};

auto DeserializeIndexValueToUpdateInfos(
    int64_t aIndexID, const KeyPath& aKeyPath, bool aMultiEntry,
    const nsACString& aLocale, StructuredCloneReadInfoParent& aCloneReadInfo) {
  MOZ_ASSERT(!NS_IsMainThread());

  using ArrayType = AutoTArray<IndexUpdateInfo, 32>;
  using ResultType = Result<ArrayType, nsresult>;

  ArrayType updateInfoArray;
  const auto helper = MakeRefPtr<DeserializeIndexValueHelper>(
      aIndexID, aKeyPath, aMultiEntry, aLocale, aCloneReadInfo,
      updateInfoArray);
  const nsresult rv = helper->DispatchAndWait();
  return NS_FAILED(rv) ? Err(rv) : ResultType{std::move(updateInfoArray)};
}

bool IsSome(
    const Maybe<CachingDatabaseConnection::BorrowedStatement>& aMaybeStmt) {
  return aMaybeStmt.isSome();
}

already_AddRefed<nsIThreadPool> MakeConnectionIOTarget() {
  nsCOMPtr<nsIThreadPool> threadPool = new nsThreadPool();

  MOZ_ALWAYS_SUCCEEDS(threadPool->SetThreadLimit(kMaxConnectionThreadCount));

  MOZ_ALWAYS_SUCCEEDS(
      threadPool->SetIdleThreadLimit(kMaxIdleConnectionThreadCount));

  MOZ_ALWAYS_SUCCEEDS(
      threadPool->SetIdleThreadMaximumTimeout(kConnectionThreadMaxIdleMS));

  MOZ_ALWAYS_SUCCEEDS(
      threadPool->SetIdleThreadGraceTimeout(kConnectionThreadGraceIdleMS));

  MOZ_ALWAYS_SUCCEEDS(threadPool->SetName("IndexedDB IO"_ns));

  return threadPool.forget();
}

}  // namespace

/*******************************************************************************
 * Exported functions
 ******************************************************************************/

already_AddRefed<PBackgroundIDBFactoryParent> AllocPBackgroundIDBFactoryParent(
    const LoggingInfo& aLoggingInfo, const nsACString& aSystemLocale) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  if (NS_AUUF_OR_WARN_IF(!aLoggingInfo.nextTransactionSerialNumber()) ||
      NS_AUUF_OR_WARN_IF(
          !aLoggingInfo.nextVersionChangeTransactionSerialNumber()) ||
      NS_AUUF_OR_WARN_IF(!aLoggingInfo.nextRequestSerialNumber())) {
    return nullptr;
  }

  SafeRefPtr<Factory> actor = Factory::Create(aLoggingInfo, aSystemLocale);
  MOZ_ASSERT(actor);

  return actor.forget();
}

bool RecvPBackgroundIDBFactoryConstructor(
    PBackgroundIDBFactoryParent* aActor, const LoggingInfo& /* aLoggingInfo */,
    const nsACString& /* aSystemLocale */) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());

  return true;
}

PBackgroundIndexedDBUtilsParent* AllocPBackgroundIndexedDBUtilsParent() {
  AssertIsOnBackgroundThread();

  RefPtr<Utils> actor = new Utils();

  return actor.forget().take();
}

bool DeallocPBackgroundIndexedDBUtilsParent(
    PBackgroundIndexedDBUtilsParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  RefPtr<Utils> actor = dont_AddRef(static_cast<Utils*>(aActor));
  return true;
}

bool RecvFlushPendingFileDeletions() {
  AssertIsOnBackgroundThread();

  if (QuotaClient* quotaClient = QuotaClient::GetInstance()) {
    QM_WARNONLY_TRY(QM_TO_RESULT(quotaClient->FlushPendingFileDeletions()));
  }

  return true;
}

RefPtr<mozilla::dom::quota::Client> CreateQuotaClient() {
  AssertIsOnBackgroundThread();

  return MakeRefPtr<QuotaClient>();
}

nsresult DatabaseFileManager::AsyncDeleteFile(int64_t aFileId) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!ContainsFileInfo(aFileId));

  QuotaClient* quotaClient = QuotaClient::GetInstance();
  if (quotaClient) {
    QM_TRY(MOZ_TO_RESULT(quotaClient->AsyncDeleteFile(this, aFileId)));
  }

  return NS_OK;
}

/*******************************************************************************
 * DatabaseConnection implementation
 ******************************************************************************/

DatabaseConnection::DatabaseConnection(
    MovingNotNull<nsCOMPtr<mozIStorageConnection>> aStorageConnection,
    MovingNotNull<SafeRefPtr<DatabaseFileManager>> aFileManager)
    : CachingDatabaseConnection(std::move(aStorageConnection)),
      mFileManager(std::move(aFileManager)),
      mLastDurability(IDBTransaction::Durability::Default),
      mInReadTransaction(false),
      mInWriteTransaction(false)
#ifdef DEBUG
      ,
      mDEBUGSavepointCount(0)
#endif
{
  AssertIsOnConnectionThread();
  MOZ_ASSERT(mFileManager);
}

DatabaseConnection::~DatabaseConnection() {
  MOZ_ASSERT(!mFileManager);
  MOZ_ASSERT(!mUpdateRefcountFunction);
  MOZ_DIAGNOSTIC_ASSERT(!mInWriteTransaction);
  MOZ_ASSERT(!mDEBUGSavepointCount);
}

nsresult DatabaseConnection::Init() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(!mInReadTransaction);
  MOZ_ASSERT(!mInWriteTransaction);

  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("BEGIN;"_ns)));

  mInReadTransaction = true;

  return NS_OK;
}

nsresult DatabaseConnection::BeginWriteTransaction(
    const IDBTransaction::Durability aDurability) {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(HasStorageConnection());
  MOZ_ASSERT(mInReadTransaction);
  MOZ_ASSERT(!mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::BeginWriteTransaction", DOM);

  // Release our read locks.
  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("ROLLBACK;"_ns)));

  mInReadTransaction = false;

  if (mLastDurability != aDurability) {
    auto synchronousMode = [aDurability]() -> nsLiteralCString {
      switch (aDurability) {
        case IDBTransaction::Durability::Default:
          return GetDefaultSynchronousMode();

        case IDBTransaction::Durability::Strict:
          return "EXTRA"_ns;

        case IDBTransaction::Durability::Relaxed:
          return "OFF"_ns;

        default:
          MOZ_CRASH("Unknown CheckpointMode!");
      }
    }();

    QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("PRAGMA synchronous = "_ns +
                                                synchronousMode + ";"_ns)));

    mLastDurability = aDurability;
  }

  if (!mUpdateRefcountFunction) {
    MOZ_ASSERT(mFileManager);

    RefPtr<UpdateRefcountFunction> function =
        new UpdateRefcountFunction(this, **mFileManager);

    QM_TRY(MOZ_TO_RESULT(MutableStorageConnection().CreateFunction(
        "update_refcount"_ns,
        /* aNumArguments */ 2, function)));

    mUpdateRefcountFunction = std::move(function);
  }

  // This one cannot obviously use ExecuteCachedStatement because of the custom
  // error handling for Execute only. If only Execute can produce
  // NS_ERROR_STORAGE_BUSY, we could actually use ExecuteCachedStatement and
  // simplify this.
  QM_TRY_INSPECT(const auto& beginStmt,
                 BorrowCachedStatement("BEGIN IMMEDIATE;"_ns));

  QM_TRY(QM_OR_ELSE_WARN_IF(
      // Expression.
      MOZ_TO_RESULT(beginStmt->Execute()),
      // Predicate.
      IsSpecificError<NS_ERROR_STORAGE_BUSY>,
      // Fallback.
      ([&beginStmt](nsresult rv) {
        NS_WARNING(
            "Received NS_ERROR_STORAGE_BUSY when attempting to start write "
            "transaction, retrying for up to 10 seconds");

        // Another thread must be using the database. Wait up to 10 seconds
        // for that to complete.
        const TimeStamp start = TimeStamp::NowLoRes();

        while (true) {
          PR_Sleep(PR_MillisecondsToInterval(100));

          rv = beginStmt->Execute();
          if (rv != NS_ERROR_STORAGE_BUSY ||
              TimeStamp::NowLoRes() - start > TimeDuration::FromSeconds(10)) {
            break;
          }
        }

        return MOZ_TO_RESULT(rv);
      })));

  mInWriteTransaction = true;

  return NS_OK;
}

nsresult DatabaseConnection::CommitWriteTransaction() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(HasStorageConnection());
  MOZ_ASSERT(!mInReadTransaction);
  MOZ_ASSERT(mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::CommitWriteTransaction", DOM);

  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("COMMIT;"_ns)));

  mInWriteTransaction = false;
  return NS_OK;
}

void DatabaseConnection::RollbackWriteTransaction() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(!mInReadTransaction);
  MOZ_DIAGNOSTIC_ASSERT(HasStorageConnection());

  AUTO_PROFILER_LABEL("DatabaseConnection::RollbackWriteTransaction", DOM);

  if (!mInWriteTransaction) {
    return;
  }

  QM_WARNONLY_TRY(
      BorrowCachedStatement("ROLLBACK;"_ns)
          .andThen([&self = *this](const auto& stmt) -> Result<Ok, nsresult> {
            // This may fail if SQLite already rolled back the transaction
            // so ignore any errors.

            // XXX ROLLBACK can fail quite normmally if a previous statement
            // failed to execute successfully so SQLite rolled back the
            // transaction already. However, if it failed because of some other
            // reason, we could try to close the connection.
            Unused << stmt->Execute();

            self.mInWriteTransaction = false;
            return Ok{};
          }));
}

void DatabaseConnection::FinishWriteTransaction() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(HasStorageConnection());
  MOZ_ASSERT(!mInReadTransaction);
  MOZ_ASSERT(!mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::FinishWriteTransaction", DOM);

  if (mUpdateRefcountFunction) {
    mUpdateRefcountFunction->Reset();
  }

  QM_WARNONLY_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("BEGIN;"_ns))
                      .andThen([&](const auto) -> Result<Ok, nsresult> {
                        mInReadTransaction = true;
                        return Ok{};
                      }));
}

nsresult DatabaseConnection::StartSavepoint() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(HasStorageConnection());
  MOZ_ASSERT(mUpdateRefcountFunction);
  MOZ_ASSERT(mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::StartSavepoint", DOM);

  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement(SAVEPOINT_CLAUSE)));

  mUpdateRefcountFunction->StartSavepoint();

#ifdef DEBUG
  MOZ_ASSERT(mDEBUGSavepointCount < UINT32_MAX);
  mDEBUGSavepointCount++;
#endif

  return NS_OK;
}

nsresult DatabaseConnection::ReleaseSavepoint() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(HasStorageConnection());
  MOZ_ASSERT(mUpdateRefcountFunction);
  MOZ_ASSERT(mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::ReleaseSavepoint", DOM);

  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement("RELEASE "_ns SAVEPOINT_CLAUSE)));

  mUpdateRefcountFunction->ReleaseSavepoint();

#ifdef DEBUG
  MOZ_ASSERT(mDEBUGSavepointCount);
  mDEBUGSavepointCount--;
#endif

  return NS_OK;
}

nsresult DatabaseConnection::RollbackSavepoint() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(HasStorageConnection());
  MOZ_ASSERT(mUpdateRefcountFunction);
  MOZ_ASSERT(mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::RollbackSavepoint", DOM);

#ifdef DEBUG
  MOZ_ASSERT(mDEBUGSavepointCount);
  mDEBUGSavepointCount--;
#endif

  mUpdateRefcountFunction->RollbackSavepoint();

  QM_TRY_INSPECT(const auto& stmt,
                 BorrowCachedStatement("ROLLBACK TO "_ns SAVEPOINT_CLAUSE));

  // This may fail if SQLite already rolled back the savepoint so ignore any
  // errors.
  Unused << stmt->Execute();

  return NS_OK;
}

nsresult DatabaseConnection::CheckpointInternal(CheckpointMode aMode) {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(!mInReadTransaction);
  MOZ_ASSERT(!mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::CheckpointInternal", DOM);

  nsAutoCString stmtString;
  stmtString.AssignLiteral("PRAGMA wal_checkpoint(");

  switch (aMode) {
    case CheckpointMode::Full:
      // Ensures that the database is completely checkpointed and flushed to
      // disk.
      stmtString.AppendLiteral("FULL");
      break;

    case CheckpointMode::Restart:
      // Like Full, but also ensures that the next write will start overwriting
      // the existing WAL file rather than letting the WAL file grow.
      stmtString.AppendLiteral("RESTART");
      break;

    case CheckpointMode::Truncate:
      // Like Restart but also truncates the existing WAL file.
      stmtString.AppendLiteral("TRUNCATE");
      break;

    default:
      MOZ_CRASH("Unknown CheckpointMode!");
  }

  stmtString.AppendLiteral(");");

  QM_TRY(MOZ_TO_RESULT(ExecuteCachedStatement(stmtString)));

  return NS_OK;
}

void DatabaseConnection::DoIdleProcessing(bool aNeedsCheckpoint,
                                          const Atomic<bool>& aInterrupted) {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(mInReadTransaction);
  MOZ_ASSERT(!mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::DoIdleProcessing", DOM);

  CachingDatabaseConnection::CachedStatement freelistStmt;
  const uint32_t freelistCount = [this, &freelistStmt] {
    QM_TRY_RETURN(GetFreelistCount(freelistStmt), 0u);
  }();

  CachedStatement rollbackStmt;
  CachedStatement beginStmt;
  if (aNeedsCheckpoint || freelistCount) {
    QM_TRY_UNWRAP(rollbackStmt, GetCachedStatement("ROLLBACK;"_ns), QM_VOID);
    QM_TRY_UNWRAP(beginStmt, GetCachedStatement("BEGIN;"_ns), QM_VOID);

    // Release the connection's normal transaction. It's possible that it could
    // fail, but that isn't a problem here.
    Unused << rollbackStmt.Borrow()->Execute();

    mInReadTransaction = false;
  }

  const bool freedSomePages =
      freelistCount && [this, &freelistStmt, &rollbackStmt, freelistCount,
                        aNeedsCheckpoint, &aInterrupted] {
        // Warn in case of an error, but do not propagate it. Just indicate we
        // didn't free any pages.
        QM_TRY_INSPECT(
            const bool& res,
            ReclaimFreePagesWhileIdle(freelistStmt, rollbackStmt, freelistCount,
                                      aNeedsCheckpoint, aInterrupted),
            false);

        // Make sure we didn't leave a transaction running.
        MOZ_ASSERT(!mInReadTransaction);
        MOZ_ASSERT(!mInWriteTransaction);

        return res;
      }();

  // Truncate the WAL if we were asked to or if we managed to free some space.
  if (aNeedsCheckpoint || freedSomePages) {
    QM_WARNONLY_TRY(QM_TO_RESULT(CheckpointInternal(CheckpointMode::Truncate)));
  }

  // Finally try to restart the read transaction if we rolled it back earlier.
  if (beginStmt) {
    QM_WARNONLY_TRY(
        MOZ_TO_RESULT(beginStmt.Borrow()->Execute())
            .andThen([&self = *this](const Ok) -> Result<Ok, nsresult> {
              self.mInReadTransaction = true;
              return Ok{};
            }));
  }
}

Result<bool, nsresult> DatabaseConnection::ReclaimFreePagesWhileIdle(
    CachedStatement& aFreelistStatement, CachedStatement& aRollbackStatement,
    uint32_t aFreelistCount, bool aNeedsCheckpoint,
    const Atomic<bool>& aInterrupted) {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(aFreelistStatement);
  MOZ_ASSERT(aRollbackStatement);
  MOZ_ASSERT(aFreelistCount);
  MOZ_ASSERT(!mInReadTransaction);
  MOZ_ASSERT(!mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::ReclaimFreePagesWhileIdle", DOM);

  uint32_t pauseOnConnectionThreadMs = StaticPrefs::
      dom_indexedDB_connectionIdleMaintenance_pauseOnConnectionThreadMs();
  if (pauseOnConnectionThreadMs > 0) {
    PR_Sleep(PR_MillisecondsToInterval(pauseOnConnectionThreadMs));
  }

  // Make sure we don't keep working if anything else needs this thread.
  if (aInterrupted) {
    return false;
  }

  // Make all the statements we'll need up front.

  // Only try to free 10% at a time so that we can bail out if this connection
  // suddenly becomes active or if the thread is needed otherwise.
  QM_TRY_INSPECT(
      const auto& incrementalVacuumStmt,
      GetCachedStatement(
          "PRAGMA incremental_vacuum("_ns +
          IntToCString(std::max(uint64_t(1), uint64_t(aFreelistCount / 10))) +
          ");"_ns));

  QM_TRY_INSPECT(const auto& beginImmediateStmt,
                 GetCachedStatement("BEGIN IMMEDIATE;"_ns));

  QM_TRY_INSPECT(const auto& commitStmt, GetCachedStatement("COMMIT;"_ns));

  if (aNeedsCheckpoint) {
    // Freeing pages is a journaled operation, so it will require additional WAL
    // space. However, we're idle and are about to checkpoint anyway, so doing a
    // RESTART checkpoint here should allow us to reuse any existing space.
    QM_TRY(MOZ_TO_RESULT(CheckpointInternal(CheckpointMode::Restart)));
  }

  // Start the write transaction.
  QM_TRY(MOZ_TO_RESULT(beginImmediateStmt.Borrow()->Execute()));

  mInWriteTransaction = true;

  bool freedSomePages = false;

  const auto rollback = [&aRollbackStatement, this](const auto&) {
    MOZ_ASSERT(mInWriteTransaction);

    // Something failed, make sure we roll everything back.
    Unused << aRollbackStatement.Borrow()->Execute();

    // XXX Is rollback infallible? Shouldn't we check the result?

    mInWriteTransaction = false;
  };

  uint64_t previousFreelistCount = (uint64_t)aFreelistCount + 1;

  QM_TRY(CollectWhile(
             [&aFreelistCount, &previousFreelistCount,
              &aInterrupted]() -> Result<bool, nsresult> {
               if (aInterrupted) {
                 // On interrupt, abort and roll back this transaction. It's ok
                 // if we never make progress here because the idle service
                 // should eventually reclaim this space.
                 return false;
               }
               // If we were not able to free anything, we might either see
               // a DB that has no auto-vacuum support at all or some other
               // (hopefully temporary) condition that prevents vacuum from
               // working. Just carry on in non-DEBUG.
               bool madeProgress = previousFreelistCount != aFreelistCount;
               previousFreelistCount = aFreelistCount;
               MOZ_ASSERT(madeProgress);
               QM_WARNONLY_TRY(MOZ_TO_RESULT(madeProgress));
               return madeProgress && (aFreelistCount != 0);
             },
             [&aFreelistStatement, &aFreelistCount, &incrementalVacuumStmt,
              &freedSomePages, this]() -> mozilla::Result<Ok, nsresult> {
               QM_TRY(MOZ_TO_RESULT(incrementalVacuumStmt.Borrow()->Execute()));

               freedSomePages = true;

               QM_TRY_UNWRAP(aFreelistCount,
                             GetFreelistCount(aFreelistStatement));

               return Ok{};
             })
             .andThen([&commitStmt, &freedSomePages, &aInterrupted, &rollback,
                       this](Ok) -> Result<Ok, nsresult> {
               if (aInterrupted) {
                 rollback(Ok{});
                 freedSomePages = false;
               }

               if (freedSomePages) {
                 // Commit the write transaction.
                 QM_TRY(MOZ_TO_RESULT(commitStmt.Borrow()->Execute()),
                        QM_PROPAGATE,
                        [](const auto&) { NS_WARNING("Failed to commit!"); });

                 mInWriteTransaction = false;
               }

               return Ok{};
             }),
         QM_PROPAGATE, rollback);

  return freedSomePages;
}

Result<uint32_t, nsresult> DatabaseConnection::GetFreelistCount(
    CachedStatement& aCachedStatement) {
  AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("DatabaseConnection::GetFreelistCount", DOM);

  if (!aCachedStatement) {
    QM_TRY_UNWRAP(aCachedStatement,
                  GetCachedStatement("PRAGMA freelist_count;"_ns));
  }

  const auto borrowedStatement = aCachedStatement.Borrow();

  QM_TRY_UNWRAP(const DebugOnly<bool> hasResult,
                MOZ_TO_RESULT_INVOKE_MEMBER(&*borrowedStatement, ExecuteStep));

  MOZ_ASSERT(hasResult);

  QM_TRY_INSPECT(const int32_t& freelistCount,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*borrowedStatement, GetInt32, 0));

  MOZ_ASSERT(freelistCount >= 0);

  return uint32_t(freelistCount);
}

void DatabaseConnection::Close() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(!mDEBUGSavepointCount);
  MOZ_DIAGNOSTIC_ASSERT(!mInWriteTransaction);

  AUTO_PROFILER_LABEL("DatabaseConnection::Close", DOM);

  if (mUpdateRefcountFunction) {
    MOZ_ALWAYS_SUCCEEDS(
        MutableStorageConnection().RemoveFunction("update_refcount"_ns));
    mUpdateRefcountFunction = nullptr;
  }

  CachingDatabaseConnection::Close();

  mFileManager.destroy();
}

nsresult DatabaseConnection::DisableQuotaChecks() {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(HasStorageConnection());

  if (!mQuotaObject) {
    MOZ_ASSERT(!mJournalQuotaObject);

    QM_TRY(MOZ_TO_RESULT(MutableStorageConnection().GetQuotaObjects(
        getter_AddRefs(mQuotaObject), getter_AddRefs(mJournalQuotaObject))));

    MOZ_ASSERT(mQuotaObject);
    MOZ_ASSERT(mJournalQuotaObject);
  }

  mQuotaObject->DisableQuotaCheck();
  mJournalQuotaObject->DisableQuotaCheck();

  return NS_OK;
}

void DatabaseConnection::EnableQuotaChecks() {
  AssertIsOnConnectionThread();
  if (!mQuotaObject) {
    MOZ_ASSERT(!mJournalQuotaObject);

    // DisableQuotaChecks failed earlier, so we don't need to enable quota
    // checks again.
    return;
  }

  MOZ_ASSERT(mJournalQuotaObject);

  const RefPtr<QuotaObject> quotaObject = std::move(mQuotaObject);
  const RefPtr<QuotaObject> journalQuotaObject = std::move(mJournalQuotaObject);

  quotaObject->EnableQuotaCheck();
  journalQuotaObject->EnableQuotaCheck();

  QM_TRY_INSPECT(const int64_t& fileSize, GetFileSize(quotaObject->Path()),
                 QM_VOID);
  QM_TRY_INSPECT(const int64_t& journalFileSize,
                 GetFileSize(journalQuotaObject->Path()), QM_VOID);

  DebugOnly<bool> result = journalQuotaObject->MaybeUpdateSize(
      journalFileSize, /* aTruncate */ true);
  MOZ_ASSERT(result);

  result = quotaObject->MaybeUpdateSize(fileSize, /* aTruncate */ true);
  MOZ_ASSERT(result);
}

Result<int64_t, nsresult> DatabaseConnection::GetFileSize(
    const nsAString& aPath) {
  MOZ_ASSERT(!aPath.IsEmpty());

  QM_TRY_INSPECT(const auto& file, QM_NewLocalFile(aPath));
  QM_TRY_INSPECT(const bool& exists, MOZ_TO_RESULT_INVOKE_MEMBER(file, Exists));

  if (exists) {
    QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(file, GetFileSize));
  }

  return 0;
}

DatabaseConnection::AutoSavepoint::AutoSavepoint()
    : mConnection(nullptr)
#ifdef DEBUG
      ,
      mDEBUGTransaction(nullptr)
#endif
{
  MOZ_COUNT_CTOR(DatabaseConnection::AutoSavepoint);
}

DatabaseConnection::AutoSavepoint::~AutoSavepoint() {
  MOZ_COUNT_DTOR(DatabaseConnection::AutoSavepoint);

  if (mConnection) {
    mConnection->AssertIsOnConnectionThread();
    MOZ_ASSERT(mDEBUGTransaction);
    MOZ_ASSERT(
        mDEBUGTransaction->GetMode() == IDBTransaction::Mode::ReadWrite ||
        mDEBUGTransaction->GetMode() == IDBTransaction::Mode::ReadWriteFlush ||
        mDEBUGTransaction->GetMode() == IDBTransaction::Mode::Cleanup ||
        mDEBUGTransaction->GetMode() == IDBTransaction::Mode::VersionChange);

    QM_WARNONLY_TRY(QM_TO_RESULT(mConnection->RollbackSavepoint()));
  }
}

nsresult DatabaseConnection::AutoSavepoint::Start(
    const TransactionBase& aTransaction) {
  MOZ_ASSERT(aTransaction.GetMode() == IDBTransaction::Mode::ReadWrite ||
             aTransaction.GetMode() == IDBTransaction::Mode::ReadWriteFlush ||
             aTransaction.GetMode() == IDBTransaction::Mode::Cleanup ||
             aTransaction.GetMode() == IDBTransaction::Mode::VersionChange);

  DatabaseConnection* connection = aTransaction.GetDatabase().GetConnection();
  MOZ_ASSERT(connection);
  connection->AssertIsOnConnectionThread();

  // The previous operation failed to begin a write transaction and the
  // following opertion jumped to the connection thread before the previous
  // operation has updated its failure to the transaction.
  if (!connection->GetUpdateRefcountFunction()) {
    NS_WARNING(
        "The connection was closed because the previous operation "
        "failed!");
    return NS_ERROR_DOM_INDEXEDDB_ABORT_ERR;
  }

  MOZ_ASSERT(!mConnection);
  MOZ_ASSERT(!mDEBUGTransaction);

  QM_TRY(MOZ_TO_RESULT(connection->StartSavepoint()));

  mConnection = connection;
#ifdef DEBUG
  mDEBUGTransaction = &aTransaction;
#endif

  return NS_OK;
}

nsresult DatabaseConnection::AutoSavepoint::Commit() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mDEBUGTransaction);

  QM_TRY(MOZ_TO_RESULT(mConnection->ReleaseSavepoint()));

  mConnection = nullptr;
#ifdef DEBUG
  mDEBUGTransaction = nullptr;
#endif

  return NS_OK;
}

DatabaseConnection::UpdateRefcountFunction::UpdateRefcountFunction(
    DatabaseConnection* const aConnection, DatabaseFileManager& aFileManager)
    : mConnection(aConnection),
      mFileManager(aFileManager),
      mInSavepoint(false) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
}

nsresult DatabaseConnection::UpdateRefcountFunction::WillCommit() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mConnection->HasStorageConnection());

  AUTO_PROFILER_LABEL("DatabaseConnection::UpdateRefcountFunction::WillCommit",
                      DOM);

  // The parameter names are not used, parameters are bound by index
  // only locally in the same function.
  auto update =
      [updateStatement = LazyStatement{*mConnection,
                                       "UPDATE file "
                                       "SET refcount = refcount + :delta "
                                       "WHERE id = :id"_ns},
       selectStatement = LazyStatement{*mConnection,
                                       "SELECT id "
                                       "FROM file "
                                       "WHERE id = :id"_ns},
       insertStatement =
           LazyStatement{
               *mConnection,
               "INSERT INTO file (id, refcount) VALUES(:id, :delta)"_ns},
       this](int64_t aId, int32_t aDelta) mutable -> Result<Ok, nsresult> {
    AUTO_PROFILER_LABEL(
        "DatabaseConnection::UpdateRefcountFunction::WillCommit::Update", DOM);
    {
      QM_TRY_INSPECT(const auto& borrowedUpdateStatement,
                     updateStatement.Borrow());

      QM_TRY(
          MOZ_TO_RESULT(borrowedUpdateStatement->BindInt32ByIndex(0, aDelta)));
      QM_TRY(MOZ_TO_RESULT(borrowedUpdateStatement->BindInt64ByIndex(1, aId)));
      QM_TRY(MOZ_TO_RESULT(borrowedUpdateStatement->Execute()));
    }

    QM_TRY_INSPECT(
        const int32_t& rows,
        MOZ_TO_RESULT_INVOKE_MEMBER(mConnection->MutableStorageConnection(),
                                    GetAffectedRows));

    if (rows > 0) {
      QM_TRY_INSPECT(
          const bool& hasResult,
          selectStatement
              .BorrowAndExecuteSingleStep(
                  [aId](auto& stmt) -> Result<Ok, nsresult> {
                    QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(0, aId)));
                    return Ok{};
                  })
              .map(IsSome));

      if (!hasResult) {
        // Don't have to create the journal here, we can create all at once,
        // just before commit
        mJournalsToCreateBeforeCommit.AppendElement(aId);
      }

      return Ok{};
    }

    QM_TRY_INSPECT(const auto& borrowedInsertStatement,
                   insertStatement.Borrow());

    QM_TRY(MOZ_TO_RESULT(borrowedInsertStatement->BindInt64ByIndex(0, aId)));
    QM_TRY(MOZ_TO_RESULT(borrowedInsertStatement->BindInt32ByIndex(1, aDelta)));
    QM_TRY(MOZ_TO_RESULT(borrowedInsertStatement->Execute()));

    mJournalsToRemoveAfterCommit.AppendElement(aId);

    return Ok{};
  };

  QM_TRY(CollectEachInRange(
      mFileInfoEntries, [&update](const auto& entry) -> Result<Ok, nsresult> {
        const auto delta = entry.GetData()->Delta();
        if (delta) {
          QM_TRY(update(entry.GetKey(), delta));
        }

        return Ok{};
      }));

  QM_TRY(MOZ_TO_RESULT(CreateJournals()));

  return NS_OK;
}

void DatabaseConnection::UpdateRefcountFunction::DidCommit() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("DatabaseConnection::UpdateRefcountFunction::DidCommit",
                      DOM);

  for (const auto& entry : mFileInfoEntries.Values()) {
    entry->MaybeUpdateDBRefs();
  }

  QM_WARNONLY_TRY(QM_TO_RESULT(RemoveJournals(mJournalsToRemoveAfterCommit)));
}

void DatabaseConnection::UpdateRefcountFunction::DidAbort() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("DatabaseConnection::UpdateRefcountFunction::DidAbort",
                      DOM);

  QM_WARNONLY_TRY(QM_TO_RESULT(RemoveJournals(mJournalsToRemoveAfterAbort)));
}

void DatabaseConnection::UpdateRefcountFunction::StartSavepoint() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(!mInSavepoint);
  MOZ_ASSERT(!mSavepointEntriesIndex.Count());

  mInSavepoint = true;
}

void DatabaseConnection::UpdateRefcountFunction::ReleaseSavepoint() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mInSavepoint);

  mSavepointEntriesIndex.Clear();
  mInSavepoint = false;
}

void DatabaseConnection::UpdateRefcountFunction::RollbackSavepoint() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(mInSavepoint);

  for (const auto& entry : mSavepointEntriesIndex.Values()) {
    entry->DecBySavepointDelta();
  }

  mInSavepoint = false;
  mSavepointEntriesIndex.Clear();
}

void DatabaseConnection::UpdateRefcountFunction::Reset() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(!mSavepointEntriesIndex.Count());
  MOZ_ASSERT(!mInSavepoint);

  mJournalsToCreateBeforeCommit.Clear();
  mJournalsToRemoveAfterCommit.Clear();
  mJournalsToRemoveAfterAbort.Clear();

  // DatabaseFileInfo implementation automatically removes unreferenced files,
  // but it's done asynchronously and with a delay. We want to remove them (and
  // decrease quota usage) before we fire the commit event.
  for (const auto& entry : mFileInfoEntries.Values()) {
    // We need to move mFileInfo into a raw pointer in order to release it
    // explicitly with aSyncDeleteFile == true.
    DatabaseFileInfo* const fileInfo = entry->ReleaseFileInfo().forget().take();
    MOZ_ASSERT(fileInfo);

    fileInfo->Release(/* aSyncDeleteFile */ true);
  }

  mFileInfoEntries.Clear();
}

nsresult DatabaseConnection::UpdateRefcountFunction::ProcessValue(
    mozIStorageValueArray* aValues, int32_t aIndex, UpdateType aUpdateType) {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(aValues);

  AUTO_PROFILER_LABEL(
      "DatabaseConnection::UpdateRefcountFunction::ProcessValue", DOM);

  QM_TRY_INSPECT(const int32_t& type,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aValues, GetTypeOfIndex, aIndex));

  if (type == mozIStorageValueArray::VALUE_TYPE_NULL) {
    return NS_OK;
  }

  QM_TRY_INSPECT(const auto& ids, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                      nsString, aValues, GetString, aIndex));

  QM_TRY_INSPECT(const auto& files,
                 DeserializeStructuredCloneFiles(mFileManager, ids));

  for (const StructuredCloneFileParent& file : files) {
    const int64_t id = file.FileInfo().Id();
    MOZ_ASSERT(id > 0);

    const auto entry =
        WrapNotNull(mFileInfoEntries.GetOrInsertNew(id, file.FileInfoPtr()));

    if (mInSavepoint) {
      mSavepointEntriesIndex.InsertOrUpdate(id, entry);
    }

    switch (aUpdateType) {
      case UpdateType::Increment:
        entry->IncDeltas(mInSavepoint);
        break;
      case UpdateType::Decrement:
        entry->DecDeltas(mInSavepoint);
        break;
      default:
        MOZ_CRASH("Unknown update type!");
    }
  }

  return NS_OK;
}

nsresult DatabaseConnection::UpdateRefcountFunction::CreateJournals() {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL(
      "DatabaseConnection::UpdateRefcountFunction::CreateJournals", DOM);

  const nsCOMPtr<nsIFile> journalDirectory = mFileManager.GetJournalDirectory();
  QM_TRY(OkIf(journalDirectory), NS_ERROR_FAILURE);

  for (const int64_t id : mJournalsToCreateBeforeCommit) {
    const nsCOMPtr<nsIFile> file =
        DatabaseFileManager::GetFileForId(journalDirectory, id);
    QM_TRY(OkIf(file), NS_ERROR_FAILURE);

    QM_TRY(MOZ_TO_RESULT(file->Create(nsIFile::NORMAL_FILE_TYPE, 0644)));

    mJournalsToRemoveAfterAbort.AppendElement(id);
  }

  return NS_OK;
}

nsresult DatabaseConnection::UpdateRefcountFunction::RemoveJournals(
    const nsTArray<int64_t>& aJournals) {
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL(
      "DatabaseConnection::UpdateRefcountFunction::RemoveJournals", DOM);

  nsCOMPtr<nsIFile> journalDirectory = mFileManager.GetJournalDirectory();
  QM_TRY(OkIf(journalDirectory), NS_ERROR_FAILURE);

  for (const auto& journal : aJournals) {
    nsCOMPtr<nsIFile> file =
        DatabaseFileManager::GetFileForId(journalDirectory, journal);
    QM_TRY(OkIf(file), NS_ERROR_FAILURE);

    QM_WARNONLY_TRY(QM_TO_RESULT(file->Remove(false)));
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS(DatabaseConnection::UpdateRefcountFunction,
                  mozIStorageFunction)

NS_IMETHODIMP
DatabaseConnection::UpdateRefcountFunction::OnFunctionCall(
    mozIStorageValueArray* aValues, nsIVariant** _retval) {
  MOZ_ASSERT(aValues);
  MOZ_ASSERT(_retval);

  AUTO_PROFILER_LABEL(
      "DatabaseConnection::UpdateRefcountFunction::OnFunctionCall", DOM);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const uint32_t& numEntries,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aValues, GetNumEntries),
                   QM_ASSERT_UNREACHABLE);

    MOZ_ASSERT(numEntries == 2);

    QM_TRY_INSPECT(const int32_t& type1,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aValues, GetTypeOfIndex, 0),
                   QM_ASSERT_UNREACHABLE);

    QM_TRY_INSPECT(const int32_t& type2,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aValues, GetTypeOfIndex, 1),
                   QM_ASSERT_UNREACHABLE);

    MOZ_ASSERT(!(type1 == mozIStorageValueArray::VALUE_TYPE_NULL &&
                 type2 == mozIStorageValueArray::VALUE_TYPE_NULL));
  }
#endif

  QM_TRY(MOZ_TO_RESULT(ProcessValue(aValues, 0, UpdateType::Decrement)));

  QM_TRY(MOZ_TO_RESULT(ProcessValue(aValues, 1, UpdateType::Increment)));

  return NS_OK;
}

/*******************************************************************************
 * ConnectionPool implementation
 ******************************************************************************/

ConnectionPool::ConnectionPool()
    : mDatabasesMutex("ConnectionPool::mDatabasesMutex"),
      mIOTarget(MakeConnectionIOTarget()),
      mIdleTimer(NS_NewTimer()),
      mNextTransactionId(0) {
  AssertIsOnOwningThread();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mIdleTimer);
}

ConnectionPool::~ConnectionPool() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mIdleDatabases.IsEmpty());
  MOZ_ASSERT(!mIdleTimer);
  MOZ_ASSERT(mTargetIdleTime.IsNull());
  MOZ_ASSERT(!mDatabases.Count());
  MOZ_ASSERT(!mTransactions.Count());
  MOZ_ASSERT(mQueuedTransactions.IsEmpty());
  MOZ_ASSERT(mCompleteCallbacks.IsEmpty());
  MOZ_ASSERT(mShutdownRequested);
  MOZ_ASSERT(mShutdownComplete);
}

// static
void ConnectionPool::IdleTimerCallback(nsITimer* aTimer, void* aClosure) {
  MOZ_ASSERT(aTimer);
  MOZ_ASSERT(aClosure);

  AUTO_PROFILER_LABEL("ConnectionPool::IdleTimerCallback", DOM);

  auto& self = *static_cast<ConnectionPool*>(aClosure);
  MOZ_ASSERT(self.mIdleTimer);
  MOZ_ASSERT(SameCOMIdentity(self.mIdleTimer, aTimer));
  MOZ_ASSERT(!self.mTargetIdleTime.IsNull());

  self.mTargetIdleTime = TimeStamp();

  // Cheat a little.
  const TimeStamp now =
      TimeStamp::NowLoRes() + TimeDuration::FromMilliseconds(500);

  // XXX Move this to ArrayAlgorithm.h?
  const auto removeUntil = [](auto& array, auto&& cond) {
    const auto begin = array.begin(), end = array.end();
    array.RemoveElementsRange(
        begin, std::find_if(begin, end, std::forward<decltype(cond)>(cond)));
  };

  removeUntil(self.mIdleDatabases, [now, &self](const auto& info) {
    if (now >= info.mIdleTime) {
      if ((*info.mDatabaseInfo)->mIdle) {
        self.PerformIdleDatabaseMaintenance(*info.mDatabaseInfo.ref());
      } else {
        self.CloseDatabase(*info.mDatabaseInfo.ref());
      }

      return false;
    }

    return true;
  });

  self.AdjustIdleTimer();
}

Result<RefPtr<DatabaseConnection>, nsresult>
ConnectionPool::GetOrCreateConnection(const Database& aDatabase) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());

  AUTO_PROFILER_LABEL("ConnectionPool::GetOrCreateConnection", DOM);

  DatabaseInfo* dbInfo;
  {
    MutexAutoLock lock(mDatabasesMutex);

    dbInfo = mDatabases.Get(aDatabase.Id());
  }

  MOZ_ASSERT(dbInfo);

  if (dbInfo->mConnection) {
    dbInfo->AssertIsOnConnectionThread();

    return dbInfo->mConnection;
  }

  MOZ_ASSERT(!dbInfo->mDEBUGConnectionEventTarget);

  QM_TRY_UNWRAP(
      MovingNotNull<nsCOMPtr<mozIStorageConnection>> storageConnection,
      GetStorageConnection(aDatabase.FilePath(), aDatabase.DirectoryLockId(),
                           aDatabase.TelemetryId(), aDatabase.MaybeKeyRef()));

  RefPtr<DatabaseConnection> connection = new DatabaseConnection(
      std::move(storageConnection), aDatabase.GetFileManagerPtr());

  QM_TRY(MOZ_TO_RESULT(connection->Init()));

  dbInfo->mConnection = connection;

  IDB_DEBUG_LOG(("ConnectionPool created connection 0x%p for '%s'",
                 dbInfo->mConnection.get(),
                 NS_ConvertUTF16toUTF8(aDatabase.FilePath()).get()));

#ifdef DEBUG
  dbInfo->mDEBUGConnectionEventTarget = GetCurrentSerialEventTarget();
#endif

  return connection;
}

uint64_t ConnectionPool::Start(
    const nsID& aBackgroundChildLoggingId, const nsACString& aDatabaseId,
    int64_t aLoggingSerialNumber, const nsTArray<nsString>& aObjectStoreNames,
    bool aIsWriteTransaction,
    TransactionDatabaseOperationBase* aTransactionOp) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!aDatabaseId.IsEmpty());
  MOZ_ASSERT(mNextTransactionId < UINT64_MAX);
  MOZ_ASSERT(!mShutdownRequested);

  AUTO_PROFILER_LABEL("ConnectionPool::Start", DOM);

  const uint64_t transactionId = ++mNextTransactionId;

  // To avoid always acquiring a lock, we don't use WithEntryHandle here, which
  // would require a lock in any case.
  DatabaseInfo* dbInfo = mDatabases.Get(aDatabaseId);

  const bool databaseInfoIsNew = !dbInfo;

  if (databaseInfoIsNew) {
    MutexAutoLock lock(mDatabasesMutex);

    dbInfo = mDatabases
                 .InsertOrUpdate(aDatabaseId,
                                 MakeUnique<DatabaseInfo>(this, aDatabaseId))
                 .get();
  }

  MOZ_ASSERT(!mTransactions.Contains(transactionId));
  auto& transactionInfo = *mTransactions.InsertOrUpdate(
      transactionId, MakeUnique<TransactionInfo>(
                         *dbInfo, aBackgroundChildLoggingId, aDatabaseId,
                         transactionId, aLoggingSerialNumber, aObjectStoreNames,
                         aIsWriteTransaction, aTransactionOp));

  if (aIsWriteTransaction) {
    MOZ_ASSERT(dbInfo->mWriteTransactionCount < UINT32_MAX);
    dbInfo->mWriteTransactionCount++;
  } else {
    MOZ_ASSERT(dbInfo->mReadTransactionCount < UINT32_MAX);
    dbInfo->mReadTransactionCount++;
  }

  auto& blockingTransactions = dbInfo->mBlockingTransactions;

  for (const nsAString& objectStoreName : aObjectStoreNames) {
    TransactionInfoPair* blockInfo =
        blockingTransactions.GetOrInsertNew(objectStoreName);

    // Mark what we are blocking on.
    if (const auto maybeBlockingRead = blockInfo->mLastBlockingReads) {
      transactionInfo.mBlockedOn.Insert(&maybeBlockingRead.ref());
      maybeBlockingRead->AddBlockingTransaction(transactionInfo);
    }

    if (aIsWriteTransaction) {
      for (const auto blockingWrite : blockInfo->mLastBlockingWrites) {
        transactionInfo.mBlockedOn.Insert(blockingWrite);
        blockingWrite->AddBlockingTransaction(transactionInfo);
      }

      blockInfo->mLastBlockingReads = SomeRef(transactionInfo);
      blockInfo->mLastBlockingWrites.Clear();
    } else {
      blockInfo->mLastBlockingWrites.AppendElement(
          WrapNotNullUnchecked(&transactionInfo));
    }
  }

  if (!transactionInfo.mBlockedOn.Count()) {
    Unused << ScheduleTransaction(transactionInfo,
                                  /* aFromQueuedTransactions */ false);
  }

  if (!databaseInfoIsNew &&
      (mIdleDatabases.RemoveElement(dbInfo) ||
       mDatabasesPerformingIdleMaintenance.RemoveElement(dbInfo))) {
    AdjustIdleTimer();
  }

  return transactionId;
}

void ConnectionPool::Dispatch(uint64_t aTransactionId, nsIRunnable* aRunnable) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aRunnable);

  AUTO_PROFILER_LABEL("ConnectionPool::Dispatch", DOM);

  auto* const transactionInfo = mTransactions.Get(aTransactionId);
  MOZ_ASSERT(transactionInfo);
  MOZ_ASSERT(!transactionInfo->mFinished);

  if (transactionInfo->mRunning) {
    DatabaseInfo& dbInfo = transactionInfo->mDatabaseInfo;
    MOZ_ASSERT(dbInfo.mEventTarget);
    MOZ_ASSERT(!dbInfo.mClosing);
    MOZ_ASSERT_IF(
        transactionInfo->mIsWriteTransaction,
        dbInfo.mRunningWriteTransaction &&
            dbInfo.mRunningWriteTransaction.refEquals(*transactionInfo));

    MOZ_ALWAYS_SUCCEEDS(dbInfo.Dispatch(do_AddRef(aRunnable)));
  } else {
    transactionInfo->mQueuedRunnables.AppendElement(aRunnable);
  }
}

void ConnectionPool::Finish(uint64_t aTransactionId,
                            FinishCallback* aCallback) {
  AssertIsOnOwningThread();

#ifdef DEBUG
  auto* const transactionInfo = mTransactions.Get(aTransactionId);
  MOZ_ASSERT(transactionInfo);
  MOZ_ASSERT(!transactionInfo->mFinished);
#endif

  AUTO_PROFILER_LABEL("ConnectionPool::Finish", DOM);

  RefPtr<FinishCallbackWrapper> wrapper =
      new FinishCallbackWrapper(this, aTransactionId, aCallback);

  Dispatch(aTransactionId, wrapper);

#ifdef DEBUG
  transactionInfo->mFinished.Flip();
#endif
}

void ConnectionPool::WaitForDatabaseToComplete(const nsCString& aDatabaseId,
                                               nsIRunnable* aCallback) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!aDatabaseId.IsEmpty());
  MOZ_ASSERT(aCallback);

  AUTO_PROFILER_LABEL("ConnectionPool::WaitForDatabaseToComplete", DOM);

  if (!CloseDatabaseWhenIdleInternal(aDatabaseId)) {
    Unused << aCallback->Run();
    return;
  }

  mCompleteCallbacks.EmplaceBack(
      MakeUnique<DatabaseCompleteCallback>(aDatabaseId, aCallback));
}

void ConnectionPool::Shutdown() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mShutdownComplete);

  AUTO_PROFILER_LABEL("ConnectionPool::Shutdown", DOM);

  mShutdownRequested.Flip();

  CancelIdleTimer();
  MOZ_ASSERT(mTargetIdleTime.IsNull());

  mIdleTimer = nullptr;

  CloseIdleDatabases();

  if (!mDatabases.Count()) {
    MOZ_ASSERT(!mTransactions.Count());

    Cleanup();

    MOZ_ASSERT(mShutdownComplete);

    mIOTarget->Shutdown();

    return;
  }

  MOZ_ALWAYS_TRUE(SpinEventLoopUntil("ConnectionPool::Shutdown"_ns, [&]() {
    return static_cast<bool>(mShutdownComplete);
  }));

  mIOTarget->Shutdown();
}

void ConnectionPool::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mShutdownRequested);
  MOZ_ASSERT(!mShutdownComplete);
  MOZ_ASSERT(!mDatabases.Count());
  MOZ_ASSERT(!mTransactions.Count());

  AUTO_PROFILER_LABEL("ConnectionPool::Cleanup", DOM);

  if (!mCompleteCallbacks.IsEmpty()) {
    // Run all callbacks manually now.

    {
      auto completeCallbacks = std::move(mCompleteCallbacks);
      for (const auto& completeCallback : completeCallbacks) {
        MOZ_ASSERT(completeCallback);
        MOZ_ASSERT(completeCallback->mCallback);

        Unused << completeCallback->mCallback->Run();
      }

      // We expect no new callbacks being completed by running the existing
      // ones.
      MOZ_ASSERT(mCompleteCallbacks.IsEmpty());
    }

    // And make sure they get processed.
    nsIThread* currentThread = NS_GetCurrentThread();
    MOZ_ASSERT(currentThread);

    MOZ_ALWAYS_SUCCEEDS(NS_ProcessPendingEvents(currentThread));
  }

  mShutdownComplete.Flip();
}

void ConnectionPool::AdjustIdleTimer() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mIdleTimer);

  AUTO_PROFILER_LABEL("ConnectionPool::AdjustIdleTimer", DOM);

  // Figure out the next time at which we should release idle resources. This
  // includes both databases and threads.
  TimeStamp newTargetIdleTime;
  MOZ_ASSERT(newTargetIdleTime.IsNull());

  if (!mIdleDatabases.IsEmpty()) {
    newTargetIdleTime = mIdleDatabases[0].mIdleTime;
  }

  MOZ_ASSERT_IF(newTargetIdleTime.IsNull(), mIdleDatabases.IsEmpty());

  // Cancel the timer if it was running and the new target time is different.
  if (!mTargetIdleTime.IsNull() &&
      (newTargetIdleTime.IsNull() || mTargetIdleTime != newTargetIdleTime)) {
    CancelIdleTimer();

    MOZ_ASSERT(mTargetIdleTime.IsNull());
  }

  // Schedule the timer if we have a target time different than before.
  if (!newTargetIdleTime.IsNull() &&
      (mTargetIdleTime.IsNull() || mTargetIdleTime != newTargetIdleTime)) {
    double delta = (newTargetIdleTime - TimeStamp::NowLoRes()).ToMilliseconds();

    uint32_t delay;
    if (delta > 0) {
      delay = uint32_t(std::min(delta, double(UINT32_MAX)));
    } else {
      delay = 0;
    }

    MOZ_ALWAYS_SUCCEEDS(mIdleTimer->InitWithNamedFuncCallback(
        IdleTimerCallback, this, delay, nsITimer::TYPE_ONE_SHOT,
        "ConnectionPool::IdleTimerCallback"));

    mTargetIdleTime = newTargetIdleTime;
  }
}

void ConnectionPool::CancelIdleTimer() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mIdleTimer);

  if (!mTargetIdleTime.IsNull()) {
    MOZ_ALWAYS_SUCCEEDS(mIdleTimer->Cancel());

    mTargetIdleTime = TimeStamp();
    MOZ_ASSERT(mTargetIdleTime.IsNull());
  }
}

void ConnectionPool::CloseIdleDatabases() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mShutdownRequested);

  AUTO_PROFILER_LABEL("ConnectionPool::CloseIdleDatabases", DOM);

  if (!mIdleDatabases.IsEmpty()) {
    for (IdleDatabaseInfo& idleInfo : mIdleDatabases) {
      CloseDatabase(*idleInfo.mDatabaseInfo.ref());
    }
    mIdleDatabases.Clear();
  }

  if (!mDatabasesPerformingIdleMaintenance.IsEmpty()) {
    for (PerformingIdleMaintenanceDatabaseInfo& performingIdleMaintenanceInfo :
         mDatabasesPerformingIdleMaintenance) {
      CloseDatabase(*performingIdleMaintenanceInfo.mDatabaseInfo);
    }
    mDatabasesPerformingIdleMaintenance.Clear();
  }
}

bool ConnectionPool::ScheduleTransaction(TransactionInfo& aTransactionInfo,
                                         bool aFromQueuedTransactions) {
  AssertIsOnOwningThread();

  AUTO_PROFILER_LABEL("ConnectionPool::ScheduleTransaction", DOM);

  DatabaseInfo& dbInfo = aTransactionInfo.mDatabaseInfo;

  dbInfo.mIdle = false;

  if (dbInfo.mClosing) {
    MOZ_ASSERT(!mIdleDatabases.Contains(&dbInfo));
    MOZ_ASSERT(
        !dbInfo.mTransactionsScheduledDuringClose.Contains(&aTransactionInfo));

    dbInfo.mTransactionsScheduledDuringClose.AppendElement(
        WrapNotNullUnchecked(&aTransactionInfo));
    return true;
  }

  if (!dbInfo.mEventTarget) {
    const uint32_t serialNumber = SerialNumber();
    const nsCString serialName =
        nsPrintfCString("IndexedDB #%" PRIu32, serialNumber);

    dbInfo.mEventTarget =
        TaskQueue::Create(do_AddRef(mIOTarget), serialName.get());
    MOZ_ASSERT(dbInfo.mEventTarget);
    IDB_DEBUG_LOG(("ConnectionPool created task queue %" PRIu32, serialNumber));
  }

  // The number of active operations equals the number of databases minus idle
  // databases. The maximum number of database operations which can make
  // progress at the same time is kMaxConnectionThreadCount. If we are at this
  // limit, all idle processing is interrupted to make room for user
  // transactions.
  if (mDatabases.Count() >=
          (mIdleDatabases.Length() + kMaxConnectionThreadCount) &&
      !mDatabasesPerformingIdleMaintenance.IsEmpty()) {
    const auto& busyDbs = mDatabasesPerformingIdleMaintenance;
    for (auto dbInfo = busyDbs.rbegin(); dbInfo != busyDbs.rend(); ++dbInfo) {
      (*dbInfo).mIdleConnectionRunnable->Interrupt();
    }
  }

  if (aTransactionInfo.mIsWriteTransaction) {
    if (dbInfo.mRunningWriteTransaction) {
      // SQLite only allows one write transaction at a time so queue this
      // transaction for later.
      MOZ_ASSERT(
          !dbInfo.mScheduledWriteTransactions.Contains(&aTransactionInfo));

      dbInfo.mScheduledWriteTransactions.AppendElement(
          WrapNotNullUnchecked(&aTransactionInfo));
      return true;
    }

    dbInfo.mRunningWriteTransaction = SomeRef(aTransactionInfo);
    dbInfo.mNeedsCheckpoint = true;
  }

  MOZ_ASSERT(!aTransactionInfo.mRunning);
  aTransactionInfo.mRunning = true;

  nsTArray<nsCOMPtr<nsIRunnable>>& queuedRunnables =
      aTransactionInfo.mQueuedRunnables;

  if (!queuedRunnables.IsEmpty()) {
    for (auto& queuedRunnable : queuedRunnables) {
      MOZ_ALWAYS_SUCCEEDS(dbInfo.Dispatch(queuedRunnable.forget()));
    }

    queuedRunnables.Clear();
  }

  return true;
}

void ConnectionPool::NoteFinishedTransaction(uint64_t aTransactionId) {
  AssertIsOnOwningThread();

  AUTO_PROFILER_LABEL("ConnectionPool::NoteFinishedTransaction", DOM);

  auto* const transactionInfo = mTransactions.Get(aTransactionId);
  MOZ_ASSERT(transactionInfo);
  MOZ_ASSERT(transactionInfo->mRunning);
  MOZ_ASSERT(transactionInfo->mFinished);

  transactionInfo->mRunning = false;

  DatabaseInfo& dbInfo = transactionInfo->mDatabaseInfo;
  MOZ_ASSERT(mDatabases.Get(transactionInfo->mDatabaseId) == &dbInfo);
  MOZ_ASSERT(dbInfo.mEventTarget);

  // Schedule the next write transaction if there are any queued.
  if (dbInfo.mRunningWriteTransaction &&
      dbInfo.mRunningWriteTransaction.refEquals(*transactionInfo)) {
    MOZ_ASSERT(transactionInfo->mIsWriteTransaction);
    MOZ_ASSERT(dbInfo.mNeedsCheckpoint);

    dbInfo.mRunningWriteTransaction = Nothing();

    if (!dbInfo.mScheduledWriteTransactions.IsEmpty()) {
      const auto nextWriteTransaction = dbInfo.mScheduledWriteTransactions[0];

      dbInfo.mScheduledWriteTransactions.RemoveElementAt(0);

      MOZ_ALWAYS_TRUE(ScheduleTransaction(*nextWriteTransaction,
                                          /* aFromQueuedTransactions */ false));
    }
  }

  for (const auto& objectStoreName : transactionInfo->mObjectStoreNames) {
    TransactionInfoPair* blockInfo =
        dbInfo.mBlockingTransactions.Get(objectStoreName);
    MOZ_ASSERT(blockInfo);

    if (transactionInfo->mIsWriteTransaction && blockInfo->mLastBlockingReads &&
        blockInfo->mLastBlockingReads.refEquals(*transactionInfo)) {
      blockInfo->mLastBlockingReads = Nothing();
    }

    blockInfo->mLastBlockingWrites.RemoveElement(transactionInfo);
  }

  transactionInfo->RemoveBlockingTransactions();

  if (transactionInfo->mIsWriteTransaction) {
    MOZ_ASSERT(dbInfo.mWriteTransactionCount);
    dbInfo.mWriteTransactionCount--;
  } else {
    MOZ_ASSERT(dbInfo.mReadTransactionCount);
    dbInfo.mReadTransactionCount--;
  }

  mTransactions.Remove(aTransactionId);

  if (!dbInfo.TotalTransactionCount()) {
    MOZ_ASSERT(!dbInfo.mIdle);
    dbInfo.mIdle = true;

    NoteIdleDatabase(dbInfo);
  }
}

void ConnectionPool::ScheduleQueuedTransactions() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mQueuedTransactions.IsEmpty());

  AUTO_PROFILER_LABEL("ConnectionPool::ScheduleQueuedTransactions", DOM);

  const auto foundIt = std::find_if(
      mQueuedTransactions.begin(), mQueuedTransactions.end(),
      [&me = *this](const auto& queuedTransaction) {
        return !me.ScheduleTransaction(*queuedTransaction,
                                       /* aFromQueuedTransactions */ true);
      });

  mQueuedTransactions.RemoveElementsRange(mQueuedTransactions.begin(), foundIt);

  AdjustIdleTimer();
}

void ConnectionPool::NoteIdleDatabase(DatabaseInfo& aDatabaseInfo) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!aDatabaseInfo.TotalTransactionCount());
  MOZ_ASSERT(aDatabaseInfo.mEventTarget);
  MOZ_ASSERT(!mIdleDatabases.Contains(&aDatabaseInfo));

  AUTO_PROFILER_LABEL("ConnectionPool::NoteIdleDatabase", DOM);

  const bool otherDatabasesWaiting = !mQueuedTransactions.IsEmpty();

  // We check mShutdownRequested because when it is true, mIdleTimer is null.
  if (mShutdownRequested || otherDatabasesWaiting ||
      aDatabaseInfo.mCloseOnIdle) {
    // Make sure we close the connection if we're shutting down or giving the
    // thread to another database.
    CloseDatabase(aDatabaseInfo);

    if (otherDatabasesWaiting) {
      ScheduleQueuedTransactions();
    }

    return;
  }

  mIdleDatabases.InsertElementSorted(IdleDatabaseInfo{aDatabaseInfo});

  AdjustIdleTimer();
}

void ConnectionPool::NoteClosedDatabase(DatabaseInfo& aDatabaseInfo) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDatabaseInfo.mClosing);
  MOZ_ASSERT(!mIdleDatabases.Contains(&aDatabaseInfo));

  AUTO_PROFILER_LABEL("ConnectionPool::NoteClosedDatabase", DOM);

  aDatabaseInfo.mClosing = false;

  // Schedule any transactions that were started while we were closing the
  // connection.
  if (!mQueuedTransactions.IsEmpty()) {
    ScheduleQueuedTransactions();
  } else if (!aDatabaseInfo.TotalTransactionCount() && !mShutdownRequested) {
    AdjustIdleTimer();
  }

  // Schedule any transactions that were started while we were closing the
  // connection.
  if (aDatabaseInfo.TotalTransactionCount()) {
    auto& scheduledTransactions =
        aDatabaseInfo.mTransactionsScheduledDuringClose;

    MOZ_ASSERT(!scheduledTransactions.IsEmpty());

    for (const auto& scheduledTransaction : scheduledTransactions) {
      Unused << ScheduleTransaction(*scheduledTransaction,
                                    /* aFromQueuedTransactions */ false);
    }

    scheduledTransactions.Clear();

    return;
  }

  // There are no more transactions and the connection has been closed. We're
  // done with this database.
  {
    MutexAutoLock lock(mDatabasesMutex);

    mDatabases.Remove(aDatabaseInfo.mDatabaseId);
  }

  // That just deleted |aDatabaseInfo|, we must not access that below.

  // See if we need to fire any complete callbacks now that the database is
  // finished.
  mCompleteCallbacks.RemoveLastElements(
      mCompleteCallbacks.end() -
      std::remove_if(mCompleteCallbacks.begin(), mCompleteCallbacks.end(),
                     [&me = *this](const auto& completeCallback) {
                       return me.MaybeFireCallback(completeCallback.get());
                     }));

  // If that was the last database and we're supposed to be shutting down then
  // we are finished.
  if (mShutdownRequested && !mDatabases.Count()) {
    MOZ_ASSERT(!mTransactions.Count());
    Cleanup();
  }
}

bool ConnectionPool::MaybeFireCallback(DatabaseCompleteCallback* aCallback) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aCallback);
  MOZ_ASSERT(!aCallback->mDatabaseId.IsEmpty());
  MOZ_ASSERT(aCallback->mCallback);

  AUTO_PROFILER_LABEL("ConnectionPool::MaybeFireCallback", DOM);

  if (mDatabases.Get(aCallback->mDatabaseId)) {
    return false;
  }

  Unused << aCallback->mCallback->Run();
  return true;
}

void ConnectionPool::PerformIdleDatabaseMaintenance(
    DatabaseInfo& aDatabaseInfo) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!aDatabaseInfo.TotalTransactionCount());
  MOZ_ASSERT(aDatabaseInfo.mEventTarget);
  MOZ_ASSERT(aDatabaseInfo.mIdle);
  MOZ_ASSERT(!aDatabaseInfo.mCloseOnIdle);
  MOZ_ASSERT(!aDatabaseInfo.mClosing);
  MOZ_ASSERT(mIdleDatabases.Contains(&aDatabaseInfo));
  MOZ_ASSERT(!mDatabasesPerformingIdleMaintenance.Contains(&aDatabaseInfo));

  const bool neededCheckpoint = aDatabaseInfo.mNeedsCheckpoint;

  aDatabaseInfo.mNeedsCheckpoint = false;
  aDatabaseInfo.mIdle = false;

  auto idleConnectionRunnable =
      MakeRefPtr<IdleConnectionRunnable>(aDatabaseInfo, neededCheckpoint);

  mDatabasesPerformingIdleMaintenance.AppendElement(
      PerformingIdleMaintenanceDatabaseInfo{aDatabaseInfo,
                                            idleConnectionRunnable});

  MOZ_ALWAYS_SUCCEEDS(aDatabaseInfo.mEventTarget->Dispatch(
      idleConnectionRunnable.forget(), NS_DISPATCH_NORMAL));
}

void ConnectionPool::CloseDatabase(DatabaseInfo& aDatabaseInfo) const {
  AssertIsOnOwningThread();
  MOZ_DIAGNOSTIC_ASSERT(!aDatabaseInfo.TotalTransactionCount());
  MOZ_ASSERT(aDatabaseInfo.mEventTarget);
  MOZ_ASSERT(!aDatabaseInfo.mClosing);

  aDatabaseInfo.mIdle = false;
  aDatabaseInfo.mNeedsCheckpoint = false;
  aDatabaseInfo.mClosing = true;

  MOZ_ALWAYS_SUCCEEDS(aDatabaseInfo.Dispatch(
      MakeAndAddRef<CloseConnectionRunnable>(aDatabaseInfo)));
}

bool ConnectionPool::CloseDatabaseWhenIdleInternal(
    const nsACString& aDatabaseId) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!aDatabaseId.IsEmpty());

  AUTO_PROFILER_LABEL("ConnectionPool::CloseDatabaseWhenIdleInternal", DOM);

  if (DatabaseInfo* dbInfo = mDatabases.Get(aDatabaseId)) {
    if (mIdleDatabases.RemoveElement(dbInfo) ||
        mDatabasesPerformingIdleMaintenance.RemoveElement(dbInfo)) {
      CloseDatabase(*dbInfo);
      AdjustIdleTimer();
    } else {
      dbInfo->mCloseOnIdle.EnsureFlipped();
    }

    return true;
  }

  return false;
}

ConnectionPool::ConnectionRunnable::ConnectionRunnable(
    DatabaseInfo& aDatabaseInfo)
    : Runnable("dom::indexedDB::ConnectionPool::ConnectionRunnable"),
      mDatabaseInfo(aDatabaseInfo),
      mOwningEventTarget(GetCurrentSerialEventTarget()) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabaseInfo.mConnectionPool);
  aDatabaseInfo.mConnectionPool->AssertIsOnOwningThread();
  MOZ_ASSERT(mOwningEventTarget);
}

NS_IMETHODIMP
ConnectionPool::IdleConnectionRunnable::Run() {
  MOZ_ASSERT(!mDatabaseInfo.mIdle);

  const nsCOMPtr<nsIEventTarget> owningThread = std::move(mOwningEventTarget);

  if (owningThread) {
    mDatabaseInfo.AssertIsOnConnectionThread();

    // The connection could be null if EnsureConnection() didn't run or was not
    // successful in TransactionDatabaseOperationBase::RunOnConnectionThread().
    if (mDatabaseInfo.mConnection) {
      mDatabaseInfo.mConnection->DoIdleProcessing(mNeedsCheckpoint,
                                                  mInterrupted);
    }

    MOZ_ALWAYS_SUCCEEDS(owningThread->Dispatch(this, NS_DISPATCH_NORMAL));
    return NS_OK;
  }

  AssertIsOnBackgroundThread();

  RefPtr<ConnectionPool> connectionPool = mDatabaseInfo.mConnectionPool;
  MOZ_ASSERT(connectionPool);

  if (mDatabaseInfo.mClosing || mDatabaseInfo.TotalTransactionCount()) {
    MOZ_ASSERT(!connectionPool->mDatabasesPerformingIdleMaintenance.Contains(
        &mDatabaseInfo));
  } else {
    MOZ_ALWAYS_TRUE(
        connectionPool->mDatabasesPerformingIdleMaintenance.RemoveElement(
            &mDatabaseInfo));

    connectionPool->NoteIdleDatabase(mDatabaseInfo);
  }

  return NS_OK;
}

NS_IMETHODIMP
ConnectionPool::CloseConnectionRunnable::Run() {
  AUTO_PROFILER_LABEL("ConnectionPool::CloseConnectionRunnable::Run", DOM);

  if (mOwningEventTarget) {
    MOZ_ASSERT(mDatabaseInfo.mClosing);

    const nsCOMPtr<nsIEventTarget> owningThread = std::move(mOwningEventTarget);

    // The connection could be null if EnsureConnection() didn't run or was not
    // successful in TransactionDatabaseOperationBase::RunOnConnectionThread().
    if (mDatabaseInfo.mConnection) {
      mDatabaseInfo.AssertIsOnConnectionThread();

      mDatabaseInfo.mConnection->Close();

      IDB_DEBUG_LOG(("ConnectionPool closed connection 0x%p",
                     mDatabaseInfo.mConnection.get()));

      mDatabaseInfo.mConnection = nullptr;

#ifdef DEBUG
      mDatabaseInfo.mDEBUGConnectionEventTarget = nullptr;
#endif
    }

    MOZ_ALWAYS_SUCCEEDS(owningThread->Dispatch(this, NS_DISPATCH_NORMAL));
    return NS_OK;
  }

  RefPtr<ConnectionPool> connectionPool = mDatabaseInfo.mConnectionPool;
  MOZ_ASSERT(connectionPool);

  connectionPool->NoteClosedDatabase(mDatabaseInfo);
  return NS_OK;
}

ConnectionPool::DatabaseInfo::DatabaseInfo(ConnectionPool* aConnectionPool,
                                           const nsACString& aDatabaseId)
    : mConnectionPool(aConnectionPool),
      mDatabaseId(aDatabaseId),
      mReadTransactionCount(0),
      mWriteTransactionCount(0),
      mNeedsCheckpoint(false),
      mIdle(false),
      mClosing(false)
#ifdef DEBUG
      ,
      mDEBUGConnectionEventTarget(nullptr)
#endif
{
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aConnectionPool);
  aConnectionPool->AssertIsOnOwningThread();
  MOZ_ASSERT(!aDatabaseId.IsEmpty());

  MOZ_COUNT_CTOR(ConnectionPool::DatabaseInfo);
}

ConnectionPool::DatabaseInfo::~DatabaseInfo() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mConnection);
  MOZ_ASSERT(mScheduledWriteTransactions.IsEmpty());
  MOZ_ASSERT(!mRunningWriteTransaction);
  MOZ_ASSERT(!TotalTransactionCount());

  MOZ_COUNT_DTOR(ConnectionPool::DatabaseInfo);
}

nsresult ConnectionPool::DatabaseInfo::Dispatch(
    already_AddRefed<nsIRunnable> aRunnable) {
  nsCOMPtr<nsIRunnable> runnable = aRunnable;

#ifdef DEBUG
  if (kDEBUGTransactionThreadSleepMS) {
    runnable = MakeRefPtr<TransactionRunnable>(std::move(runnable));
  }
#endif

  return mEventTarget->Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
}

ConnectionPool::DatabaseCompleteCallback::DatabaseCompleteCallback(
    const nsCString& aDatabaseId, nsIRunnable* aCallback)
    : mDatabaseId(aDatabaseId), mCallback(aCallback) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mDatabaseId.IsEmpty());
  MOZ_ASSERT(aCallback);

  MOZ_COUNT_CTOR(ConnectionPool::DatabaseCompleteCallback);
}

ConnectionPool::DatabaseCompleteCallback::~DatabaseCompleteCallback() {
  AssertIsOnBackgroundThread();

  MOZ_COUNT_DTOR(ConnectionPool::DatabaseCompleteCallback);
}

ConnectionPool::FinishCallbackWrapper::FinishCallbackWrapper(
    ConnectionPool* aConnectionPool, uint64_t aTransactionId,
    FinishCallback* aCallback)
    : Runnable("dom::indexedDB::ConnectionPool::FinishCallbackWrapper"),
      mConnectionPool(aConnectionPool),
      mCallback(aCallback),
      mOwningEventTarget(GetCurrentSerialEventTarget()),
      mTransactionId(aTransactionId),
      mHasRunOnce(false) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aConnectionPool);
  MOZ_ASSERT(aCallback);
  MOZ_ASSERT(mOwningEventTarget);
}

ConnectionPool::FinishCallbackWrapper::~FinishCallbackWrapper() {
  MOZ_ASSERT(!mConnectionPool);
  MOZ_ASSERT(!mCallback);
}

nsresult ConnectionPool::FinishCallbackWrapper::Run() {
  MOZ_ASSERT(mConnectionPool);
  MOZ_ASSERT(mCallback);
  MOZ_ASSERT(mOwningEventTarget);

  AUTO_PROFILER_LABEL("ConnectionPool::FinishCallbackWrapper::Run", DOM);

  if (!mHasRunOnce) {
    MOZ_ASSERT(!IsOnBackgroundThread());

    mHasRunOnce = true;

    Unused << mCallback->Run();

    MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL));

    return NS_OK;
  }

  mConnectionPool->AssertIsOnOwningThread();
  MOZ_ASSERT(mHasRunOnce);

  RefPtr<ConnectionPool> connectionPool = std::move(mConnectionPool);
  RefPtr<FinishCallback> callback = std::move(mCallback);

  callback->TransactionFinishedBeforeUnblock();

  connectionPool->NoteFinishedTransaction(mTransactionId);

  callback->TransactionFinishedAfterUnblock();

  return NS_OK;
}

uint32_t ConnectionPool::sSerialNumber = 0u;

#ifdef DEBUG

ConnectionPool::TransactionRunnable::TransactionRunnable(
    nsCOMPtr<nsIRunnable> aRunnable)
    : Runnable("dom::indexedDB::ConnectionPool::TransactionRunnable"),
      mRunnable(std::move(aRunnable)) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(kDEBUGTransactionThreadSleepMS);
}

nsresult ConnectionPool::TransactionRunnable::Run() {
  MOZ_ASSERT(!IsOnBackgroundThread());

  QM_TRY(MOZ_TO_RESULT(mRunnable->Run()));

  MOZ_ALWAYS_TRUE(PR_Sleep(PR_MillisecondsToInterval(
                      kDEBUGTransactionThreadSleepMS)) == PR_SUCCESS);

  return NS_OK;
}

#endif

ConnectionPool::IdleResource::IdleResource(const TimeStamp& aIdleTime)
    : mIdleTime(aIdleTime) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!aIdleTime.IsNull());

  MOZ_COUNT_CTOR(ConnectionPool::IdleResource);
}

ConnectionPool::IdleResource::~IdleResource() {
  AssertIsOnBackgroundThread();

  MOZ_COUNT_DTOR(ConnectionPool::IdleResource);
}

ConnectionPool::IdleDatabaseInfo::IdleDatabaseInfo(DatabaseInfo& aDatabaseInfo)
    : IdleResource(
          TimeStamp::NowLoRes() +
          (aDatabaseInfo.mIdle
               ? TimeDuration::FromMilliseconds(kConnectionIdleMaintenanceMS)
               : TimeDuration::FromMilliseconds(kConnectionIdleCloseMS))),
      mDatabaseInfo(WrapNotNullUnchecked(&aDatabaseInfo)) {
  AssertIsOnBackgroundThread();

  MOZ_COUNT_CTOR(ConnectionPool::IdleDatabaseInfo);
}

ConnectionPool::IdleDatabaseInfo::~IdleDatabaseInfo() {
  AssertIsOnBackgroundThread();

  MOZ_COUNT_DTOR(ConnectionPool::IdleDatabaseInfo);
}

ConnectionPool::PerformingIdleMaintenanceDatabaseInfo::
    PerformingIdleMaintenanceDatabaseInfo(
        DatabaseInfo& aDatabaseInfo,
        RefPtr<IdleConnectionRunnable> aIdleConnectionRunnable)
    : mDatabaseInfo(WrapNotNullUnchecked(&aDatabaseInfo)),
      mIdleConnectionRunnable(std::move(aIdleConnectionRunnable)) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mIdleConnectionRunnable);

  MOZ_COUNT_CTOR(ConnectionPool::PerformingIdleMaintenanceDatabaseInfo);
}

ConnectionPool::PerformingIdleMaintenanceDatabaseInfo::
    ~PerformingIdleMaintenanceDatabaseInfo() {
  AssertIsOnBackgroundThread();

  MOZ_COUNT_DTOR(ConnectionPool::PerformingIdleMaintenanceDatabaseInfo);
}

ConnectionPool::TransactionInfo::TransactionInfo(
    DatabaseInfo& aDatabaseInfo, const nsID& aBackgroundChildLoggingId,
    const nsACString& aDatabaseId, uint64_t aTransactionId,
    int64_t aLoggingSerialNumber, const nsTArray<nsString>& aObjectStoreNames,
    bool aIsWriteTransaction, TransactionDatabaseOperationBase* aTransactionOp)
    : mDatabaseInfo(aDatabaseInfo),
      mBackgroundChildLoggingId(aBackgroundChildLoggingId),
      mDatabaseId(aDatabaseId),
      mTransactionId(aTransactionId),
      mLoggingSerialNumber(aLoggingSerialNumber),
      mObjectStoreNames(aObjectStoreNames.Clone()),
      mIsWriteTransaction(aIsWriteTransaction),
      mRunning(false) {
  AssertIsOnBackgroundThread();
  aDatabaseInfo.mConnectionPool->AssertIsOnOwningThread();

  MOZ_COUNT_CTOR(ConnectionPool::TransactionInfo);

  if (aTransactionOp) {
    mQueuedRunnables.AppendElement(aTransactionOp);
  }
}

ConnectionPool::TransactionInfo::~TransactionInfo() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mBlockedOn.Count());
  MOZ_ASSERT(mQueuedRunnables.IsEmpty());
  MOZ_ASSERT(!mRunning);
  MOZ_ASSERT(mFinished);

  MOZ_COUNT_DTOR(ConnectionPool::TransactionInfo);
}

void ConnectionPool::TransactionInfo::AddBlockingTransaction(
    TransactionInfo& aTransactionInfo) {
  AssertIsOnBackgroundThread();

  // XXX Does it really make sense to have both mBlocking and mBlockingOrdered,
  // just to reduce the algorithmic complexity of this Contains check? This was
  // mentioned in the context of Bug 1290853, but no real justification was
  // given. There was the suggestion of encapsulating this in an
  // insertion-ordered hashtable implementation, which seems like a good idea.
  // If we had that, this would be the appropriate data structure to use here.
  if (mBlocking.EnsureInserted(&aTransactionInfo)) {
    mBlockingOrdered.AppendElement(WrapNotNullUnchecked(&aTransactionInfo));
  }
}

void ConnectionPool::TransactionInfo::RemoveBlockingTransactions() {
  AssertIsOnBackgroundThread();

  for (const auto blockedInfo : mBlockingOrdered) {
    blockedInfo->MaybeUnblock(*this);
  }

  mBlocking.Clear();
  mBlockingOrdered.Clear();
}

void ConnectionPool::TransactionInfo::MaybeUnblock(
    TransactionInfo& aTransactionInfo) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mBlockedOn.Contains(&aTransactionInfo));

  mBlockedOn.Remove(&aTransactionInfo);
  if (mBlockedOn.IsEmpty()) {
    ConnectionPool* connectionPool = mDatabaseInfo.mConnectionPool;
    MOZ_ASSERT(connectionPool);
    connectionPool->AssertIsOnOwningThread();

    Unused << connectionPool->ScheduleTransaction(
        *this,
        /* aFromQueuedTransactions */ false);
  }
}

#if defined(DEBUG) || defined(NS_BUILD_REFCNT_LOGGING)
ConnectionPool::TransactionInfoPair::TransactionInfoPair() {
  AssertIsOnBackgroundThread();

  MOZ_COUNT_CTOR(ConnectionPool::TransactionInfoPair);
}

ConnectionPool::TransactionInfoPair::~TransactionInfoPair() {
  AssertIsOnBackgroundThread();

  MOZ_COUNT_DTOR(ConnectionPool::TransactionInfoPair);
}
#endif

/*******************************************************************************
 * Metadata classes
 ******************************************************************************/

bool FullObjectStoreMetadata::HasLiveIndexes() const {
  AssertIsOnBackgroundThread();

  return std::any_of(mIndexes.Values().cbegin(), mIndexes.Values().cend(),
                     [](const auto& entry) { return !entry->mDeleted; });
}

SafeRefPtr<FullDatabaseMetadata> FullDatabaseMetadata::Duplicate() const {
  AssertIsOnBackgroundThread();

  // FullDatabaseMetadata contains two hash tables of pointers that we need to
  // duplicate so we can't just use the copy constructor.
  auto newMetadata = MakeSafeRefPtr<FullDatabaseMetadata>(mCommonMetadata);

  newMetadata->mDatabaseId = mDatabaseId;
  newMetadata->mFilePath = mFilePath;
  newMetadata->mNextObjectStoreId = mNextObjectStoreId;
  newMetadata->mNextIndexId = mNextIndexId;

  for (const auto& objectStoreEntry : mObjectStores) {
    const auto& objectStoreValue = objectStoreEntry.GetData();

    auto newOSMetadata = MakeSafeRefPtr<FullObjectStoreMetadata>(
        objectStoreValue->mCommonMetadata, [&objectStoreValue] {
          const auto&& srcLocked = objectStoreValue->mAutoIncrementIds.Lock();
          return *srcLocked;
        }());

    for (const auto& indexEntry : objectStoreValue->mIndexes) {
      const auto& value = indexEntry.GetData();

      auto newIndexMetadata = MakeSafeRefPtr<FullIndexMetadata>();

      newIndexMetadata->mCommonMetadata = value->mCommonMetadata;

      if (NS_WARN_IF(!newOSMetadata->mIndexes.InsertOrUpdate(
              indexEntry.GetKey(), std::move(newIndexMetadata), fallible))) {
        return nullptr;
      }
    }

    MOZ_ASSERT(objectStoreValue->mIndexes.Count() ==
               newOSMetadata->mIndexes.Count());

    if (NS_WARN_IF(!newMetadata->mObjectStores.InsertOrUpdate(
            objectStoreEntry.GetKey(), std::move(newOSMetadata), fallible))) {
      return nullptr;
    }
  }

  MOZ_ASSERT(mObjectStores.Count() == newMetadata->mObjectStores.Count());

  return newMetadata;
}

DatabaseLoggingInfo::~DatabaseLoggingInfo() {
  AssertIsOnBackgroundThread();

  if (gLoggingInfoHashtable) {
    const nsID& backgroundChildLoggingId =
        mLoggingInfo.backgroundChildLoggingId();

    MOZ_ASSERT(gLoggingInfoHashtable->Get(backgroundChildLoggingId) == this);

    gLoggingInfoHashtable->Remove(backgroundChildLoggingId);
  }
}

/*******************************************************************************
 * Factory
 ******************************************************************************/

Factory::Factory(RefPtr<DatabaseLoggingInfo> aLoggingInfo,
                 const nsACString& aSystemLocale)
    : mSystemLocale(aSystemLocale),
      mLoggingInfo(std::move(aLoggingInfo))
#ifdef DEBUG
      ,
      mActorDestroyed(false)
#endif
{
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
}

Factory::~Factory() { MOZ_ASSERT(mActorDestroyed); }

// static
SafeRefPtr<Factory> Factory::Create(const LoggingInfo& aLoggingInfo,
                                    const nsACString& aSystemLocale) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());

  // Balanced in ActoryDestroy().
  IncreaseBusyCount();

  MOZ_ASSERT(gLoggingInfoHashtable);
  RefPtr<DatabaseLoggingInfo> loggingInfo =
      gLoggingInfoHashtable->WithEntryHandle(
          aLoggingInfo.backgroundChildLoggingId(), [&](auto&& entry) {
            if (entry) {
              [[maybe_unused]] const auto& loggingInfo = entry.Data();
              MOZ_ASSERT(aLoggingInfo.backgroundChildLoggingId() ==
                         loggingInfo->Id());
#if !FUZZING
              NS_WARNING_ASSERTION(
                  aLoggingInfo.nextTransactionSerialNumber() ==
                      loggingInfo->mLoggingInfo.nextTransactionSerialNumber(),
                  "NextTransactionSerialNumber doesn't match!");
              NS_WARNING_ASSERTION(
                  aLoggingInfo.nextVersionChangeTransactionSerialNumber() ==
                      loggingInfo->mLoggingInfo
                          .nextVersionChangeTransactionSerialNumber(),
                  "NextVersionChangeTransactionSerialNumber doesn't match!");
              NS_WARNING_ASSERTION(
                  aLoggingInfo.nextRequestSerialNumber() ==
                      loggingInfo->mLoggingInfo.nextRequestSerialNumber(),
                  "NextRequestSerialNumber doesn't match!");
#endif  // !FUZZING
            } else {
              entry.Insert(new DatabaseLoggingInfo(aLoggingInfo));
            }

            return do_AddRef(entry.Data());
          });

  return MakeSafeRefPtr<Factory>(std::move(loggingInfo), aSystemLocale);
}

void Factory::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

#ifdef DEBUG
  mActorDestroyed = true;
#endif

  // Match the IncreaseBusyCount in Create().
  DecreaseBusyCount();
}

mozilla::ipc::IPCResult Factory::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  QM_WARNONLY_TRY(OkIf(PBackgroundIDBFactoryParent::Send__delete__(this)));

  return IPC_OK();
}

PBackgroundIDBFactoryRequestParent*
Factory::AllocPBackgroundIDBFactoryRequestParent(
    const FactoryRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != FactoryRequestParams::T__None);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread())) {
    return nullptr;
  }

  const CommonFactoryRequestParams* commonParams;

  switch (aParams.type()) {
    case FactoryRequestParams::TOpenDatabaseRequestParams: {
      const OpenDatabaseRequestParams& params =
          aParams.get_OpenDatabaseRequestParams();
      commonParams = &params.commonParams();
      break;
    }

    case FactoryRequestParams::TDeleteDatabaseRequestParams: {
      const DeleteDatabaseRequestParams& params =
          aParams.get_DeleteDatabaseRequestParams();
      commonParams = &params.commonParams();
      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  MOZ_ASSERT(commonParams);

  const DatabaseMetadata& metadata = commonParams->metadata();

  if (NS_AUUF_OR_WARN_IF(!IsValidPersistenceType(metadata.persistenceType()))) {
    return nullptr;
  }

  const PrincipalInfo& principalInfo = commonParams->principalInfo();

  if (NS_AUUF_OR_WARN_IF(!quota::IsPrincipalInfoValid(principalInfo))) {
    IPC_FAIL(this, "Invalid principal!");
    return nullptr;
  }

  MOZ_ASSERT(principalInfo.type() == PrincipalInfo::TSystemPrincipalInfo ||
             principalInfo.type() == PrincipalInfo::TContentPrincipalInfo);

  if (NS_AUUF_OR_WARN_IF(
          principalInfo.type() == PrincipalInfo::TSystemPrincipalInfo &&
          metadata.persistenceType() != PERSISTENCE_TYPE_PERSISTENT)) {
    return nullptr;
  }

  if (NS_AUUF_OR_WARN_IF(
          principalInfo.type() == PrincipalInfo::TContentPrincipalInfo &&
          QuotaManager::IsOriginInternal(
              principalInfo.get_ContentPrincipalInfo().originNoSuffix()) &&
          metadata.persistenceType() != PERSISTENCE_TYPE_PERSISTENT)) {
    return nullptr;
  }

  Maybe<ContentParentId> contentParentId = GetContentParentId();

  auto actor = [&]() -> RefPtr<FactoryRequestOp> {
    if (aParams.type() == FactoryRequestParams::TOpenDatabaseRequestParams) {
      return MakeRefPtr<OpenDatabaseOp>(SafeRefPtrFromThis(), contentParentId,
                                        *commonParams);
    } else {
      return MakeRefPtr<DeleteDatabaseOp>(SafeRefPtrFromThis(), contentParentId,
                                          *commonParams);
    }
  }();

  gFactoryOps->AppendElement(actor);

  // Balanced in CleanupMetadata() which is/must always called by SendResults().
  IncreaseBusyCount();

  // Transfer ownership to IPDL.
  return actor.forget().take();
}

mozilla::ipc::IPCResult Factory::RecvPBackgroundIDBFactoryRequestConstructor(
    PBackgroundIDBFactoryRequestParent* aActor,
    const FactoryRequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != FactoryRequestParams::T__None);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());

  auto* op = static_cast<FactoryRequestOp*>(aActor);

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(op));
  return IPC_OK();
}

bool Factory::DeallocPBackgroundIDBFactoryRequestParent(
    PBackgroundIDBFactoryRequestParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  // Transfer ownership back from IPDL.
  RefPtr<FactoryRequestOp> op =
      dont_AddRef(static_cast<FactoryRequestOp*>(aActor));
  return true;
}

mozilla::ipc::IPCResult Factory::RecvGetDatabases(
    const PersistenceType& aPersistenceType,
    const PrincipalInfo& aPrincipalInfo, GetDatabasesResolver&& aResolve) {
  AssertIsOnBackgroundThread();

  auto ResolveGetDatabasesAndReturn = [&aResolve](const nsresult rv) {
    aResolve(rv);
    return IPC_OK();
  };

  QM_TRY(MOZ_TO_RESULT(!QuotaClient::IsShuttingDownOnBackgroundThread()),
         ResolveGetDatabasesAndReturn);

  QM_TRY(MOZ_TO_RESULT(IsValidPersistenceType(aPersistenceType)),
         QM_IPC_FAIL(this));

  QM_TRY(MOZ_TO_RESULT(quota::IsPrincipalInfoValid(aPrincipalInfo)),
         QM_IPC_FAIL(this));

  MOZ_ASSERT(aPrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo ||
             aPrincipalInfo.type() == PrincipalInfo::TContentPrincipalInfo);

  PersistenceType persistenceType =
      IDBFactory::GetPersistenceType(aPrincipalInfo);

  QM_TRY(MOZ_TO_RESULT(aPersistenceType == persistenceType), QM_IPC_FAIL(this));

  Maybe<ContentParentId> contentParentId = GetContentParentId();

  auto op = MakeRefPtr<GetDatabasesOp>(SafeRefPtrFromThis(), contentParentId,
                                       aPersistenceType, aPrincipalInfo,
                                       std::move(aResolve));

  gFactoryOps->AppendElement(op);

  // Balanced in CleanupMetadata() which is/must always called by SendResults().
  IncreaseBusyCount();

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(op));

  return IPC_OK();
}

Maybe<ContentParentId> Factory::GetContentParentId() const {
  uint64_t childID = BackgroundParent::GetChildID(Manager());
  if (childID) {
    // If childID is not zero we are dealing with an other-process actor. We
    // want to initialize OpenDatabaseOp/DeleteDatabaseOp here with the ID
    // (and later also Database) in that case, so Database::IsOwnedByProcess
    // can find Databases belonging to a particular content process when
    // QuotaClient::AbortOperationsForProcess is called which is currently used
    // to abort operations for content processes only.
    return Some(ContentParentId(childID));
  }

  return Nothing();
}

/*******************************************************************************
 * WaitForTransactionsHelper
 ******************************************************************************/

void WaitForTransactionsHelper::WaitForTransactions() {
  MOZ_ASSERT(mState == State::Initial);

  Unused << this->Run();
}

void WaitForTransactionsHelper::MaybeWaitForTransactions() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::Initial);

  RefPtr<ConnectionPool> connectionPool = gConnectionPool.get();
  if (connectionPool) {
    mState = State::WaitingForTransactions;

    connectionPool->WaitForDatabaseToComplete(mDatabaseId, this);

    return;
  }

  CallCallback();
}

void WaitForTransactionsHelper::CallCallback() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::Initial ||
             mState == State::WaitingForTransactions);

  const nsCOMPtr<nsIRunnable> callback = std::move(mCallback);

  callback->Run();

  mState = State::Complete;
}

NS_IMETHODIMP
WaitForTransactionsHelper::Run() {
  MOZ_ASSERT(mState != State::Complete);
  MOZ_ASSERT(mCallback);

  switch (mState) {
    case State::Initial:
      MaybeWaitForTransactions();
      break;

    case State::WaitingForTransactions:
      CallCallback();
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  return NS_OK;
}

/*******************************************************************************
 * Database
 ******************************************************************************/

Database::Database(SafeRefPtr<Factory> aFactory,
                   const PrincipalInfo& aPrincipalInfo,
                   const Maybe<ContentParentId>& aOptionalContentParentId,
                   const quota::OriginMetadata& aOriginMetadata,
                   uint32_t aTelemetryId,
                   SafeRefPtr<FullDatabaseMetadata> aMetadata,
                   SafeRefPtr<DatabaseFileManager> aFileManager,
                   ClientDirectoryLockHandle aDirectoryLockHandle,
                   bool aInPrivateBrowsing,
                   const Maybe<const CipherKey>& aMaybeKey)
    : mFactory(std::move(aFactory)),
      mMetadata(std::move(aMetadata)),
      mFileManager(std::move(aFileManager)),
      mDirectoryLockHandle(std::move(aDirectoryLockHandle)),
      mPrincipalInfo(aPrincipalInfo),
      mOptionalContentParentId(aOptionalContentParentId),
      mOriginMetadata(aOriginMetadata),
      mId(mMetadata->mDatabaseId),
      mFilePath(mMetadata->mFilePath),
      mKey(aMaybeKey),
      mTelemetryId(aTelemetryId),
      mPersistenceType(mMetadata->mCommonMetadata.persistenceType()),
      mInPrivateBrowsing(aInPrivateBrowsing),
      mBackgroundThread(GetCurrentSerialEventTarget())
#ifdef DEBUG
      ,
      mAllBlobsUnmapped(false)
#endif
{
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mFactory);
  MOZ_ASSERT(mMetadata);
  MOZ_ASSERT(mFileManager);

  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(mDirectoryLockHandle->Id() >= 0);
  mDirectoryLockId = mDirectoryLockHandle->Id();
}

template <typename T>
bool Database::InvalidateAll(const nsTBaseHashSet<nsPtrHashKey<T>>& aTable) {
  AssertIsOnBackgroundThread();

  const uint32_t count = aTable.Count();
  if (!count) {
    return true;
  }

  // XXX Does this really need to be fallible?
  QM_TRY_INSPECT(const auto& elementsToInvalidate,
                 TransformIntoNewArray(
                     aTable, [](const auto& entry) { return entry; }, fallible),
                 false);

  IDB_REPORT_INTERNAL_ERR();

  for (const auto& elementToInvalidate : elementsToInvalidate) {
    MOZ_ASSERT(elementToInvalidate);

    elementToInvalidate->Invalidate();
  }

  return true;
}

void Database::Invalidate() {
  AssertIsOnBackgroundThread();

  if (mInvalidated) {
    return;
  }

  mInvalidated.Flip();

  if (mActorWasAlive && !mActorDestroyed) {
    Unused << SendInvalidate();
  }

  QM_WARNONLY_TRY(OkIf(InvalidateAll(mTransactions)));

  MOZ_ALWAYS_TRUE(CloseInternal());
}

nsresult Database::EnsureConnection() {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());

  AUTO_PROFILER_LABEL("Database::EnsureConnection", DOM);

  if (!mConnection || !mConnection->HasStorageConnection()) {
    QM_TRY_UNWRAP(mConnection, gConnectionPool->GetOrCreateConnection(*this));
  }

  AssertIsOnConnectionThread();

  return NS_OK;
}

bool Database::RegisterTransaction(TransactionBase& aTransaction) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mTransactions.Contains(&aTransaction));
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(!mInvalidated);
  MOZ_ASSERT(!mClosed);

  if (NS_WARN_IF(!mTransactions.Insert(&aTransaction, fallible))) {
    return false;
  }

  return true;
}

void Database::UnregisterTransaction(TransactionBase& aTransaction) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mTransactions.Contains(&aTransaction));

  mTransactions.Remove(&aTransaction);

  MaybeCloseConnection();
}

void Database::SetActorAlive() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  mActorWasAlive.Flip();
}

void Database::MapBlob(const IPCBlob& aIPCBlob,
                       SafeRefPtr<DatabaseFileInfo> aFileInfo) {
  AssertIsOnBackgroundThread();

  const RemoteLazyStream& stream = aIPCBlob.inputStream();
  MOZ_ASSERT(stream.type() == RemoteLazyStream::TRemoteLazyInputStream);

  nsID id{};
  MOZ_ALWAYS_SUCCEEDS(
      stream.get_RemoteLazyInputStream()->GetInternalStreamID(id));

  MOZ_ASSERT(!mMappedBlobs.Contains(id));
  mMappedBlobs.InsertOrUpdate(id, std::move(aFileInfo));

  RefPtr<UnmapBlobCallback> callback =
      new UnmapBlobCallback(SafeRefPtrFromThis());

  auto storage = RemoteLazyInputStreamStorage::Get();
  MOZ_ASSERT(storage.isOk());
  storage.inspect()->StoreCallback(id, callback);
}

void Database::Stringify(nsACString& aResult) const {
  AssertIsOnBackgroundThread();

  constexpr auto kQuotaGenericDelimiterString = "|"_ns;

  aResult.Append(
      "DirectoryLock:"_ns + IntToCString(!!mDirectoryLockHandle) +
      kQuotaGenericDelimiterString +
      //
      "Transactions:"_ns + IntToCString(mTransactions.Count()) +
      kQuotaGenericDelimiterString +
      //
      "OtherProcessActor:"_ns +
      IntToCString(
          BackgroundParent::IsOtherProcessActor(GetBackgroundParent())) +
      kQuotaGenericDelimiterString +
      //
      "Origin:"_ns + AnonymizedOriginString(mOriginMetadata.mOrigin) +
      kQuotaGenericDelimiterString +
      //
      "PersistenceType:"_ns + PersistenceTypeToString(mPersistenceType) +
      kQuotaGenericDelimiterString +
      //
      "Closed:"_ns + IntToCString(static_cast<bool>(mClosed)) +
      kQuotaGenericDelimiterString +
      //
      "Invalidated:"_ns + IntToCString(static_cast<bool>(mInvalidated)) +
      kQuotaGenericDelimiterString +
      //
      "ActorWasAlive:"_ns + IntToCString(static_cast<bool>(mActorWasAlive)) +
      kQuotaGenericDelimiterString +
      //
      "ActorDestroyed:"_ns + IntToCString(static_cast<bool>(mActorDestroyed)));
}

SafeRefPtr<DatabaseFileInfo> Database::GetBlob(const IPCBlob& aIPCBlob) {
  AssertIsOnBackgroundThread();

  RefPtr<RemoteLazyInputStream> lazyStream;
  switch (aIPCBlob.inputStream().type()) {
    case RemoteLazyStream::TIPCStream: {
      const InputStreamParams& inputStreamParams =
          aIPCBlob.inputStream().get_IPCStream().stream();
      if (inputStreamParams.type() !=
          InputStreamParams::TRemoteLazyInputStreamParams) {
        return nullptr;
      }
      lazyStream = inputStreamParams.get_RemoteLazyInputStreamParams().stream();
      break;
    }
    case RemoteLazyStream::TRemoteLazyInputStream:
      lazyStream = aIPCBlob.inputStream().get_RemoteLazyInputStream();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Unknown RemoteLazyStream type");
      return nullptr;
  }

  if (!lazyStream) {
    MOZ_ASSERT_UNREACHABLE("Unexpected null stream");
    return nullptr;
  }

  nsID id{};
  nsresult rv = lazyStream->GetInternalStreamID(id);
  if (NS_FAILED(rv)) {
    MOZ_ASSERT_UNREACHABLE(
        "Received RemoteLazyInputStream doesn't have an actor connection");
    return nullptr;
  }

  const auto fileInfo = mMappedBlobs.Lookup(id);
  return fileInfo ? fileInfo->clonePtr() : nullptr;
}

void Database::UnmapBlob(const nsID& aID) {
  AssertIsOnBackgroundThread();

  MOZ_ASSERT_IF(!mAllBlobsUnmapped, mMappedBlobs.Contains(aID));
  mMappedBlobs.Remove(aID);
}

void Database::UnmapAllBlobs() {
  AssertIsOnBackgroundThread();

#ifdef DEBUG
  mAllBlobsUnmapped = true;
#endif

  mMappedBlobs.Clear();
}

bool Database::CloseInternal() {
  AssertIsOnBackgroundThread();

  if (mClosed) {
    if (NS_WARN_IF(!IsInvalidated())) {
      // Signal misbehaving child for sending the close message twice.
      return false;
    }

    // Ignore harmless race when we just invalidated the database.
    return true;
  }

  mClosed.Flip();

  if (gConnectionPool) {
    gConnectionPool->CloseDatabaseWhenIdle(Id());
  }

  DatabaseActorInfo* info;
  MOZ_ALWAYS_TRUE(gLiveDatabaseHashtable->Get(Id(), &info));

  MOZ_ASSERT(info->mLiveDatabases.Contains(this));

  if (info->mWaitingFactoryOp) {
    info->mWaitingFactoryOp->NoteDatabaseClosed(this);
  }

  MaybeCloseConnection();

  return true;
}

void Database::MaybeCloseConnection() {
  AssertIsOnBackgroundThread();

  if (!mTransactions.Count() && IsClosed() && mDirectoryLockHandle) {
    nsCOMPtr<nsIRunnable> callback =
        NewRunnableMethod("dom::indexedDB::Database::ConnectionClosedCallback",
                          this, &Database::ConnectionClosedCallback);

    RefPtr<WaitForTransactionsHelper> helper =
        new WaitForTransactionsHelper(Id(), callback);
    helper->WaitForTransactions();
  }
}

void Database::ConnectionClosedCallback() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mClosed);
  MOZ_ASSERT(!mTransactions.Count());

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  CleanupMetadata();

  UnmapAllBlobs();

  if (IsInvalidated() && IsActorAlive()) {
    // Step 3 and 4 of "5.2 Closing a Database":
    // 1. Wait for all transactions to complete.
    // 2. Fire a close event if forced flag is set, i.e., IsInvalidated() in our
    //    implementation.
    Unused << SendCloseAfterInvalidationComplete();
  }
}

void Database::CleanupMetadata() {
  AssertIsOnBackgroundThread();

  DatabaseActorInfo* info;
  MOZ_ALWAYS_TRUE(gLiveDatabaseHashtable->Get(Id(), &info));
  MOZ_ALWAYS_TRUE(info->mLiveDatabases.RemoveElement(this));

  QuotaManager::MaybeRecordQuotaClientShutdownStep(
      quota::Client::IDB, "Live database entry removed"_ns);

  if (info->mLiveDatabases.IsEmpty()) {
    MOZ_ASSERT(!info->mWaitingFactoryOp ||
               !info->mWaitingFactoryOp->HasBlockedDatabases());
    gLiveDatabaseHashtable->Remove(Id());

    QuotaManager::MaybeRecordQuotaClientShutdownStep(
        quota::Client::IDB, "gLiveDatabaseHashtable entry removed"_ns);
  }

  // Match the IncreaseBusyCount in OpenDatabaseOp::EnsureDatabaseActor().
  DecreaseBusyCount();
}

void Database::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();

  mActorDestroyed.Flip();

  if (!IsInvalidated()) {
    Invalidate();
  }
}

PBackgroundIDBDatabaseFileParent*
Database::AllocPBackgroundIDBDatabaseFileParent(const IPCBlob& aIPCBlob) {
  AssertIsOnBackgroundThread();

  SafeRefPtr<DatabaseFileInfo> fileInfo = GetBlob(aIPCBlob);
  RefPtr<DatabaseFile> actor;

  if (fileInfo) {
    actor = new DatabaseFile(std::move(fileInfo));
  } else {
    // This is a blob we haven't seen before.
    fileInfo = mFileManager->CreateFileInfo();
    if (NS_WARN_IF(!fileInfo)) {
      return nullptr;
    }

    actor = new DatabaseFile(IPCBlobUtils::Deserialize(aIPCBlob),
                             std::move(fileInfo));
  }

  MOZ_ASSERT(actor);

  return actor.forget().take();
}

bool Database::DeallocPBackgroundIDBDatabaseFileParent(
    PBackgroundIDBDatabaseFileParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  RefPtr<DatabaseFile> actor = dont_AddRef(static_cast<DatabaseFile*>(aActor));
  return true;
}

already_AddRefed<PBackgroundIDBTransactionParent>
Database::AllocPBackgroundIDBTransactionParent(
    const nsTArray<nsString>& aObjectStoreNames, const Mode& aMode,
    const Durability& aDurability) {
  AssertIsOnBackgroundThread();

  // Once a database is closed it must not try to open new transactions.
  if (NS_WARN_IF(mClosed)) {
    MOZ_ASSERT_UNLESS_FUZZING(mInvalidated);
    return nullptr;
  }

  if (NS_AUUF_OR_WARN_IF(aObjectStoreNames.IsEmpty())) {
    return nullptr;
  }

  if (NS_AUUF_OR_WARN_IF(aMode != IDBTransaction::Mode::ReadOnly &&
                         aMode != IDBTransaction::Mode::ReadWrite &&
                         aMode != IDBTransaction::Mode::ReadWriteFlush &&
                         aMode != IDBTransaction::Mode::Cleanup)) {
    return nullptr;
  }

  if (NS_AUUF_OR_WARN_IF(aDurability != IDBTransaction::Durability::Default &&
                         aDurability != IDBTransaction::Durability::Strict &&
                         aDurability != IDBTransaction::Durability::Relaxed)) {
    return nullptr;
  }

  const ObjectStoreTable& objectStores = mMetadata->mObjectStores;
  const uint32_t nameCount = aObjectStoreNames.Length();

  if (NS_AUUF_OR_WARN_IF(nameCount > objectStores.Count())) {
    return nullptr;
  }

  QM_TRY_UNWRAP(
      auto objectStoreMetadatas,
      TransformIntoNewArrayAbortOnErr(
          aObjectStoreNames,
          [lastName = Maybe<const nsString&>{},
           &objectStores](const nsString& name) mutable
              -> mozilla::Result<SafeRefPtr<FullObjectStoreMetadata>,
                                 nsresult> {
            if (lastName) {
              // Make sure that this name is sorted properly and not a
              // duplicate.
              if (NS_AUUF_OR_WARN_IF(name <= lastName.ref())) {
                return Err(NS_ERROR_FAILURE);
              }
            }
            lastName = SomeRef(name);

            const auto foundIt =
                std::find_if(objectStores.cbegin(), objectStores.cend(),
                             [&name](const auto& entry) {
                               const auto& value = entry.GetData();
                               MOZ_ASSERT(entry.GetKey());
                               return name == value->mCommonMetadata.name() &&
                                      !value->mDeleted;
                             });
            if (foundIt == objectStores.cend()) {
              MOZ_ASSERT_UNLESS_FUZZING(false, "ObjectStore not found.");
              return Err(NS_ERROR_FAILURE);
            }

            return foundIt->GetData().clonePtr();
          },
          fallible),
      nullptr);

  return MakeSafeRefPtr<NormalTransaction>(SafeRefPtrFromThis(), aMode,
                                           aDurability,
                                           std::move(objectStoreMetadatas))
      .forget();
}

mozilla::ipc::IPCResult Database::RecvPBackgroundIDBTransactionConstructor(
    PBackgroundIDBTransactionParent* aActor,
    nsTArray<nsString>&& aObjectStoreNames, const Mode& aMode,
    const Durability& aDurability) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(!aObjectStoreNames.IsEmpty());
  MOZ_ASSERT(aMode == IDBTransaction::Mode::ReadOnly ||
             aMode == IDBTransaction::Mode::ReadWrite ||
             aMode == IDBTransaction::Mode::ReadWriteFlush ||
             aMode == IDBTransaction::Mode::Cleanup);
  MOZ_ASSERT(aDurability == IDBTransaction::Durability::Default ||
             aDurability == IDBTransaction::Durability::Strict ||
             aDurability == IDBTransaction::Durability::Relaxed);
  MOZ_ASSERT(!mClosed);

  if (IsInvalidated()) {
    // This is an expected race. We don't want the child to die here, just don't
    // actually do any work.
    return IPC_OK();
  }

  if (!gConnectionPool) {
    gConnectionPool = new ConnectionPool();
  }

  auto* transaction = static_cast<NormalTransaction*>(aActor);

  RefPtr<StartTransactionOp> startOp = new StartTransactionOp(
      SafeRefPtr{transaction, AcquireStrongRefFromRawPtr{}});

  uint64_t transactionId = startOp->StartOnConnectionPool(
      GetLoggingInfo()->Id(), mMetadata->mDatabaseId,
      transaction->LoggingSerialNumber(), aObjectStoreNames,
      aMode != IDBTransaction::Mode::ReadOnly);

  transaction->Init(transactionId);

  if (NS_WARN_IF(!RegisterTransaction(*transaction))) {
    IDB_REPORT_INTERNAL_ERR();
    transaction->Abort(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR, /* aForce */ false);
    return IPC_OK();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult Database::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  QM_WARNONLY_TRY(OkIf(PBackgroundIDBDatabaseParent::Send__delete__(this)));

  return IPC_OK();
}

mozilla::ipc::IPCResult Database::RecvBlocked() {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(mClosed)) {
    // Even though the sender checks the DB for not being closed, too,
    // there is a potential race with an ongoing origin clearing which
    // might have invalidated the DB in the meantime. Just ignore.
    return IPC_OK();
  }

  DatabaseActorInfo* info;
  MOZ_ALWAYS_TRUE(gLiveDatabaseHashtable->Get(Id(), &info));
  MOZ_ASSERT(info->mLiveDatabases.Contains(this));

  if (NS_WARN_IF(!info->mWaitingFactoryOp)) {
    return IPC_FAIL(this, "Database info has no mWaitingFactoryOp!");
  }

  info->mWaitingFactoryOp->NoteDatabaseBlocked(this);

  return IPC_OK();
}

mozilla::ipc::IPCResult Database::RecvClose() {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!CloseInternal())) {
    return IPC_FAIL(this, "CloseInternal failed!");
  }

  return IPC_OK();
}

void Database::StartTransactionOp::RunOnConnectionThread() {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(!HasFailed());

  IDB_LOG_MARK_PARENT_TRANSACTION("Beginning database work", "DB Start",
                                  IDB_LOG_ID_STRING(mBackgroundChildLoggingId),
                                  mTransactionLoggingSerialNumber);

  TransactionDatabaseOperationBase::RunOnConnectionThread();
}

nsresult Database::StartTransactionOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  Transaction().SetActiveOnConnectionThread();

  if (Transaction().GetMode() == IDBTransaction::Mode::Cleanup) {
    DebugOnly<nsresult> rv = aConnection->DisableQuotaChecks();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "DisableQuotaChecks failed, trying to continue "
                         "cleanup transaction with quota checks enabled");
  }

  if (Transaction().GetMode() != IDBTransaction::Mode::ReadOnly) {
    QM_TRY(MOZ_TO_RESULT(
        aConnection->BeginWriteTransaction(Transaction().GetDurability())));
  }

  return NS_OK;
}

nsresult Database::StartTransactionOp::SendSuccessResult() {
  // We don't need to do anything here.
  return NS_OK;
}

bool Database::StartTransactionOp::SendFailureResult(
    nsresult /* aResultCode */) {
  IDB_REPORT_INTERNAL_ERR();

  // Abort the transaction.
  return false;
}

void Database::StartTransactionOp::Cleanup() {
#ifdef DEBUG
  // StartTransactionOp is not a normal database operation that is tied to an
  // actor. Do this to make our assertions happy.
  NoteActorDestroyed();
#endif

  TransactionDatabaseOperationBase::Cleanup();
}

/*******************************************************************************
 * TransactionBase
 ******************************************************************************/

TransactionBase::TransactionBase(SafeRefPtr<Database> aDatabase, Mode aMode,
                                 Durability aDurability)
    : mDatabase(std::move(aDatabase)),
      mDatabaseId(mDatabase->Id()),
      mLoggingSerialNumber(
          mDatabase->GetLoggingInfo()->NextTransactionSN(aMode)),
      mActiveRequestCount(0),
      mInvalidatedOnAnyThread(false),
      mMode(aMode),
      mDurability(aDurability),
      mResultCode(NS_OK) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mDatabase);
  MOZ_ASSERT(mLoggingSerialNumber);
}

TransactionBase::~TransactionBase() {
  MOZ_ASSERT(!mActiveRequestCount);
  MOZ_ASSERT(mActorDestroyed);
  MOZ_ASSERT_IF(mInitialized, mCommittedOrAborted);
}

void TransactionBase::Abort(nsresult aResultCode, bool aForce) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(NS_FAILED(aResultCode));

  if (NS_SUCCEEDED(mResultCode)) {
    mResultCode = aResultCode;
  }

  if (aForce) {
    mForceAborted.EnsureFlipped();
  }

  MaybeCommitOrAbort();
}

mozilla::ipc::IPCResult TransactionBase::RecvCommit(
    IProtocol* aActor, const Maybe<int64_t> aLastRequest) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(
        aActor, "Attempt to commit an already comitted/aborted transaction!");
  }

  mCommitOrAbortReceived.Flip();
  mLastRequestBeforeCommit.init(aLastRequest);
  MaybeCommitOrAbort();

  return IPC_OK();
}

mozilla::ipc::IPCResult TransactionBase::RecvAbort(IProtocol* aActor,
                                                   nsresult aResultCode) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(NS_SUCCEEDED(aResultCode))) {
    return IPC_FAIL(aActor, "aResultCode must not be a success code!");
  }

  if (NS_WARN_IF(NS_ERROR_GET_MODULE(aResultCode) !=
                 NS_ERROR_MODULE_DOM_INDEXEDDB)) {
    return IPC_FAIL(aActor, "aResultCode does not refer to IndexedDB!");
  }

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(
        aActor, "Attempt to abort an already comitted/aborted transaction!");
  }

  mCommitOrAbortReceived.Flip();
  Abort(aResultCode, /* aForce */ false);

  return IPC_OK();
}

void TransactionBase::CommitOrAbort() {
  AssertIsOnBackgroundThread();

  mCommittedOrAborted.Flip();

  if (!mInitialized) {
    return;
  }

  // In case of a failed request and explicitly committed transaction, abort
  // (cf. https://w3c.github.io/IndexedDB/#async-execute-request step 5.3
  // vs. 5.4). It's worth emphasizing this can only happen here when we are
  // committing explicitly, otherwise the decision is made by the child.
  if (NS_SUCCEEDED(mResultCode) && mLastFailedRequest &&
      *mLastRequestBeforeCommit &&
      *mLastFailedRequest == **mLastRequestBeforeCommit) {
    mResultCode = NS_ERROR_DOM_INDEXEDDB_ABORT_ERR;
  }

  RefPtr<CommitOp> commitOp =
      new CommitOp(SafeRefPtrFromThis(), ClampResultCode(mResultCode));

  gConnectionPool->Finish(TransactionId(), commitOp);
}

SafeRefPtr<FullObjectStoreMetadata>
TransactionBase::GetMetadataForObjectStoreId(
    IndexOrObjectStoreId aObjectStoreId) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aObjectStoreId);

  if (!aObjectStoreId) {
    return nullptr;
  }

  auto metadata = mDatabase->Metadata().mObjectStores.Lookup(aObjectStoreId);
  if (!metadata || (*metadata)->mDeleted) {
    return nullptr;
  }

  MOZ_ASSERT((*metadata)->mCommonMetadata.id() == aObjectStoreId);

  return metadata->clonePtr();
}

SafeRefPtr<FullIndexMetadata> TransactionBase::GetMetadataForIndexId(
    FullObjectStoreMetadata& aObjectStoreMetadata,
    IndexOrObjectStoreId aIndexId) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aIndexId);

  if (!aIndexId) {
    return nullptr;
  }

  auto metadata = aObjectStoreMetadata.mIndexes.Lookup(aIndexId);
  if (!metadata || (*metadata)->mDeleted) {
    return nullptr;
  }

  MOZ_ASSERT((*metadata)->mCommonMetadata.id() == aIndexId);

  return metadata->clonePtr();
}

void TransactionBase::NoteModifiedAutoIncrementObjectStore(
    const SafeRefPtr<FullObjectStoreMetadata>& aMetadata) {
  AssertIsOnConnectionThread();

  if (!mModifiedAutoIncrementObjectStoreMetadataArray.Contains(aMetadata)) {
    mModifiedAutoIncrementObjectStoreMetadataArray.AppendElement(
        aMetadata.clonePtr());
  }
}

void TransactionBase::ForgetModifiedAutoIncrementObjectStore(
    FullObjectStoreMetadata& aMetadata) {
  AssertIsOnConnectionThread();

  mModifiedAutoIncrementObjectStoreMetadataArray.RemoveElement(&aMetadata);
}

bool TransactionBase::VerifyRequestParams(const RequestParams& aParams) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != RequestParams::T__None);

  switch (aParams.type()) {
    case RequestParams::TObjectStoreAddParams: {
      const ObjectStoreAddPutParams& params =
          aParams.get_ObjectStoreAddParams().commonParams();
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params))) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStorePutParams: {
      const ObjectStoreAddPutParams& params =
          aParams.get_ObjectStorePutParams().commonParams();
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params))) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStoreGetParams: {
      const ObjectStoreGetParams& params = aParams.get_ObjectStoreGetParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.keyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStoreGetKeyParams: {
      const ObjectStoreGetKeyParams& params =
          aParams.get_ObjectStoreGetKeyParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.keyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStoreGetAllParams: {
      const ObjectStoreGetAllParams& params =
          aParams.get_ObjectStoreGetAllParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.optionalKeyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStoreGetAllKeysParams: {
      const ObjectStoreGetAllKeysParams& params =
          aParams.get_ObjectStoreGetAllKeysParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.optionalKeyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStoreDeleteParams: {
      if (NS_AUUF_OR_WARN_IF(mMode != IDBTransaction::Mode::ReadWrite &&
                             mMode != IDBTransaction::Mode::ReadWriteFlush &&
                             mMode != IDBTransaction::Mode::Cleanup &&
                             mMode != IDBTransaction::Mode::VersionChange)) {
        return false;
      }

      const ObjectStoreDeleteParams& params =
          aParams.get_ObjectStoreDeleteParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.keyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStoreClearParams: {
      if (NS_AUUF_OR_WARN_IF(mMode != IDBTransaction::Mode::ReadWrite &&
                             mMode != IDBTransaction::Mode::ReadWriteFlush &&
                             mMode != IDBTransaction::Mode::Cleanup &&
                             mMode != IDBTransaction::Mode::VersionChange)) {
        return false;
      }

      const ObjectStoreClearParams& params =
          aParams.get_ObjectStoreClearParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      break;
    }

    case RequestParams::TObjectStoreCountParams: {
      const ObjectStoreCountParams& params =
          aParams.get_ObjectStoreCountParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.optionalKeyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TIndexGetParams: {
      const IndexGetParams& params = aParams.get_IndexGetParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      const SafeRefPtr<FullIndexMetadata> indexMetadata =
          GetMetadataForIndexId(*objectStoreMetadata, params.indexId());
      if (NS_AUUF_OR_WARN_IF(!indexMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.keyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TIndexGetKeyParams: {
      const IndexGetKeyParams& params = aParams.get_IndexGetKeyParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      const SafeRefPtr<FullIndexMetadata> indexMetadata =
          GetMetadataForIndexId(*objectStoreMetadata, params.indexId());
      if (NS_AUUF_OR_WARN_IF(!indexMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.keyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TIndexGetAllParams: {
      const IndexGetAllParams& params = aParams.get_IndexGetAllParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      const SafeRefPtr<FullIndexMetadata> indexMetadata =
          GetMetadataForIndexId(*objectStoreMetadata, params.indexId());
      if (NS_AUUF_OR_WARN_IF(!indexMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.optionalKeyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TIndexGetAllKeysParams: {
      const IndexGetAllKeysParams& params = aParams.get_IndexGetAllKeysParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      const SafeRefPtr<FullIndexMetadata> indexMetadata =
          GetMetadataForIndexId(*objectStoreMetadata, params.indexId());
      if (NS_AUUF_OR_WARN_IF(!indexMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.optionalKeyRange()))) {
        return false;
      }
      break;
    }

    case RequestParams::TIndexCountParams: {
      const IndexCountParams& params = aParams.get_IndexCountParams();
      const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
          GetMetadataForObjectStoreId(params.objectStoreId());
      if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
        return false;
      }
      const SafeRefPtr<FullIndexMetadata> indexMetadata =
          GetMetadataForIndexId(*objectStoreMetadata, params.indexId());
      if (NS_AUUF_OR_WARN_IF(!indexMetadata)) {
        return false;
      }
      if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(params.optionalKeyRange()))) {
        return false;
      }
      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  return true;
}

bool TransactionBase::VerifyRequestParams(
    const SerializedKeyRange& aParams) const {
  AssertIsOnBackgroundThread();

  // XXX Check more here?

  if (aParams.isOnly()) {
    if (NS_AUUF_OR_WARN_IF(aParams.lower().IsUnset())) {
      return false;
    }
    if (NS_AUUF_OR_WARN_IF(!aParams.upper().IsUnset())) {
      return false;
    }
    if (NS_AUUF_OR_WARN_IF(aParams.lowerOpen())) {
      return false;
    }
    if (NS_AUUF_OR_WARN_IF(aParams.upperOpen())) {
      return false;
    }
  } else if (NS_AUUF_OR_WARN_IF(aParams.lower().IsUnset() &&
                                aParams.upper().IsUnset())) {
    return false;
  }

  return true;
}

bool TransactionBase::VerifyRequestParams(
    const ObjectStoreAddPutParams& aParams) const {
  AssertIsOnBackgroundThread();

  if (NS_AUUF_OR_WARN_IF(mMode != IDBTransaction::Mode::ReadWrite &&
                         mMode != IDBTransaction::Mode::ReadWriteFlush &&
                         mMode != IDBTransaction::Mode::VersionChange)) {
    return false;
  }

  SafeRefPtr<FullObjectStoreMetadata> objMetadata =
      GetMetadataForObjectStoreId(aParams.objectStoreId());
  if (NS_AUUF_OR_WARN_IF(!objMetadata)) {
    return false;
  }

  if (NS_AUUF_OR_WARN_IF(!aParams.cloneInfo().data().data.Size())) {
    return false;
  }

  if (objMetadata->mCommonMetadata.autoIncrement() &&
      objMetadata->mCommonMetadata.keyPath().IsValid() &&
      aParams.key().IsUnset()) {
    const SerializedStructuredCloneWriteInfo& cloneInfo = aParams.cloneInfo();

    if (NS_AUUF_OR_WARN_IF(!cloneInfo.offsetToKeyProp())) {
      return false;
    }

    if (NS_AUUF_OR_WARN_IF(cloneInfo.data().data.Size() < sizeof(uint64_t))) {
      return false;
    }

    if (NS_AUUF_OR_WARN_IF(cloneInfo.offsetToKeyProp() >
                           (cloneInfo.data().data.Size() - sizeof(uint64_t)))) {
      return false;
    }
  } else if (NS_AUUF_OR_WARN_IF(aParams.cloneInfo().offsetToKeyProp())) {
    return false;
  }

  for (const auto& updateInfo : aParams.indexUpdateInfos()) {
    SafeRefPtr<FullIndexMetadata> indexMetadata =
        GetMetadataForIndexId(*objMetadata, updateInfo.indexId());
    if (NS_AUUF_OR_WARN_IF(!indexMetadata)) {
      return false;
    }

    if (NS_AUUF_OR_WARN_IF(updateInfo.value().IsUnset())) {
      return false;
    }

    MOZ_ASSERT(!updateInfo.value().GetBuffer().IsEmpty());
  }

  for (const FileAddInfo& fileAddInfo : aParams.fileAddInfos()) {
    const PBackgroundIDBDatabaseFileParent* file =
        fileAddInfo.file().AsParent();

    switch (fileAddInfo.type()) {
      case StructuredCloneFileBase::eBlob:
        if (NS_AUUF_OR_WARN_IF(!file)) {
          return false;
        }
        break;

      case StructuredCloneFileBase::eMutableFile: {
        return false;
      }

      case StructuredCloneFileBase::eStructuredClone:
      case StructuredCloneFileBase::eWasmBytecode:
      case StructuredCloneFileBase::eWasmCompiled:
      case StructuredCloneFileBase::eEndGuard:
        MOZ_ASSERT_UNLESS_FUZZING(false, "Unsupported.");
        return false;

      default:
        MOZ_CRASH("Should never get here!");
    }
  }

  return true;
}

bool TransactionBase::VerifyRequestParams(
    const Maybe<SerializedKeyRange>& aParams) const {
  AssertIsOnBackgroundThread();

  if (aParams.isSome()) {
    if (NS_AUUF_OR_WARN_IF(!VerifyRequestParams(aParams.ref()))) {
      return false;
    }
  }

  return true;
}

void TransactionBase::NoteActiveRequest() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mActiveRequestCount < UINT64_MAX);

  mActiveRequestCount++;
}

void TransactionBase::NoteFinishedRequest(const int64_t aRequestId,
                                          const nsresult aResultCode) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mActiveRequestCount);

  mActiveRequestCount--;

  if (NS_FAILED(aResultCode)) {
    mLastFailedRequest = Some(aRequestId);
  }

  MaybeCommitOrAbort();
}

void TransactionBase::Invalidate() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mInvalidated == mInvalidatedOnAnyThread);

  if (!mInvalidated) {
    mInvalidated.Flip();
    mInvalidatedOnAnyThread = true;

    Abort(NS_ERROR_DOM_INDEXEDDB_ABORT_ERR, /* aForce */ false);
  }
}

PBackgroundIDBRequestParent* TransactionBase::AllocRequest(
    const int64_t aRequestId, RequestParams&& aParams, bool aTrustParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != RequestParams::T__None);

#ifdef DEBUG
  // Always verify parameters in DEBUG builds!
  aTrustParams = false;
#endif

  if (NS_AUUF_OR_WARN_IF(!aTrustParams && !VerifyRequestParams(aParams))) {
    return nullptr;
  }

  if (NS_AUUF_OR_WARN_IF(mCommitOrAbortReceived)) {
    return nullptr;
  }

  RefPtr<NormalTransactionOp> actor;

  switch (aParams.type()) {
    case RequestParams::TObjectStoreAddParams:
    case RequestParams::TObjectStorePutParams:
      actor = new ObjectStoreAddOrPutRequestOp(SafeRefPtrFromThis(), aRequestId,
                                               std::move(aParams));
      break;

    case RequestParams::TObjectStoreGetParams:
      actor =
          new ObjectStoreGetRequestOp(SafeRefPtrFromThis(), aRequestId, aParams,
                                      /* aGetAll */ false);
      break;

    case RequestParams::TObjectStoreGetAllParams:
      actor =
          new ObjectStoreGetRequestOp(SafeRefPtrFromThis(), aRequestId, aParams,
                                      /* aGetAll */ true);
      break;

    case RequestParams::TObjectStoreGetKeyParams:
      actor = new ObjectStoreGetKeyRequestOp(SafeRefPtrFromThis(), aRequestId,
                                             aParams,
                                             /* aGetAll */ false);
      break;

    case RequestParams::TObjectStoreGetAllKeysParams:
      actor = new ObjectStoreGetKeyRequestOp(SafeRefPtrFromThis(), aRequestId,
                                             aParams,
                                             /* aGetAll */ true);
      break;

    case RequestParams::TObjectStoreDeleteParams:
      actor =
          new ObjectStoreDeleteRequestOp(SafeRefPtrFromThis(), aRequestId,
                                         aParams.get_ObjectStoreDeleteParams());
      break;

    case RequestParams::TObjectStoreClearParams:
      actor =
          new ObjectStoreClearRequestOp(SafeRefPtrFromThis(), aRequestId,
                                        aParams.get_ObjectStoreClearParams());
      break;

    case RequestParams::TObjectStoreCountParams:
      actor =
          new ObjectStoreCountRequestOp(SafeRefPtrFromThis(), aRequestId,
                                        aParams.get_ObjectStoreCountParams());
      break;

    case RequestParams::TIndexGetParams:
      actor = new IndexGetRequestOp(SafeRefPtrFromThis(), aRequestId, aParams,
                                    /* aGetAll */ false);
      break;

    case RequestParams::TIndexGetKeyParams:
      actor =
          new IndexGetKeyRequestOp(SafeRefPtrFromThis(), aRequestId, aParams,
                                   /* aGetAll */ false);
      break;

    case RequestParams::TIndexGetAllParams:
      actor = new IndexGetRequestOp(SafeRefPtrFromThis(), aRequestId, aParams,
                                    /* aGetAll */ true);
      break;

    case RequestParams::TIndexGetAllKeysParams:
      actor =
          new IndexGetKeyRequestOp(SafeRefPtrFromThis(), aRequestId, aParams,
                                   /* aGetAll */ true);
      break;

    case RequestParams::TIndexCountParams:
      actor =
          new IndexCountRequestOp(SafeRefPtrFromThis(), aRequestId, aParams);
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  MOZ_ASSERT(actor);

  // Transfer ownership to IPDL.
  return actor.forget().take();
}

bool TransactionBase::StartRequest(PBackgroundIDBRequestParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  auto* op = static_cast<NormalTransactionOp*>(aActor);

  if (NS_WARN_IF(!op->Init(*this))) {
    op->Cleanup();
    return false;
  }

  op->DispatchToConnectionPool();
  return true;
}

bool TransactionBase::DeallocRequest(
    PBackgroundIDBRequestParent* const aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  // Transfer ownership back from IPDL.
  const RefPtr<NormalTransactionOp> actor =
      dont_AddRef(static_cast<NormalTransactionOp*>(aActor));
  return true;
}

already_AddRefed<PBackgroundIDBCursorParent> TransactionBase::AllocCursor(
    const OpenCursorParams& aParams, bool aTrustParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != OpenCursorParams::T__None);

#ifdef DEBUG
  // Always verify parameters in DEBUG builds!
  aTrustParams = false;
#endif

  const OpenCursorParams::Type type = aParams.type();
  SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata;
  SafeRefPtr<FullIndexMetadata> indexMetadata;
  CursorBase::Direction direction;

  // First extract the parameters common to all open cursor variants.
  const auto& commonParams = GetCommonOpenCursorParams(aParams);
  objectStoreMetadata =
      GetMetadataForObjectStoreId(commonParams.objectStoreId());
  if (NS_AUUF_OR_WARN_IF(!objectStoreMetadata)) {
    return nullptr;
  }
  if (aTrustParams && NS_AUUF_OR_WARN_IF(!VerifyRequestParams(
                          commonParams.optionalKeyRange()))) {
    return nullptr;
  }
  direction = commonParams.direction();

  // Now, for the index open cursor variants, extract the additional parameter.
  if (type == OpenCursorParams::TIndexOpenCursorParams ||
      type == OpenCursorParams::TIndexOpenKeyCursorParams) {
    const auto& commonIndexParams = GetCommonIndexOpenCursorParams(aParams);
    indexMetadata = GetMetadataForIndexId(*objectStoreMetadata,
                                          commonIndexParams.indexId());
    if (NS_AUUF_OR_WARN_IF(!indexMetadata)) {
      return nullptr;
    }
  }

  if (NS_AUUF_OR_WARN_IF(mCommitOrAbortReceived)) {
    return nullptr;
  }

  // Create Cursor and transfer ownership to IPDL.
  switch (type) {
    case OpenCursorParams::TObjectStoreOpenCursorParams:
      MOZ_ASSERT(!indexMetadata);
      return MakeAndAddRef<Cursor<IDBCursorType::ObjectStore>>(
          SafeRefPtrFromThis(), std::move(objectStoreMetadata), direction,
          CursorBase::ConstructFromTransactionBase{});
    case OpenCursorParams::TObjectStoreOpenKeyCursorParams:
      MOZ_ASSERT(!indexMetadata);
      return MakeAndAddRef<Cursor<IDBCursorType::ObjectStoreKey>>(
          SafeRefPtrFromThis(), std::move(objectStoreMetadata), direction,
          CursorBase::ConstructFromTransactionBase{});
    case OpenCursorParams::TIndexOpenCursorParams:
      return MakeAndAddRef<Cursor<IDBCursorType::Index>>(
          SafeRefPtrFromThis(), std::move(objectStoreMetadata),
          std::move(indexMetadata), direction,
          CursorBase::ConstructFromTransactionBase{});
    case OpenCursorParams::TIndexOpenKeyCursorParams:
      return MakeAndAddRef<Cursor<IDBCursorType::IndexKey>>(
          SafeRefPtrFromThis(), std::move(objectStoreMetadata),
          std::move(indexMetadata), direction,
          CursorBase::ConstructFromTransactionBase{});
    default:
      MOZ_CRASH("Cannot get here.");
  }
}

bool TransactionBase::StartCursor(PBackgroundIDBCursorParent* const aActor,
                                  const int64_t aRequestId,
                                  const OpenCursorParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != OpenCursorParams::T__None);

  auto* const op = static_cast<CursorBase*>(aActor);

  if (NS_WARN_IF(!op->Start(aRequestId, aParams))) {
    return false;
  }

  return true;
}

/*******************************************************************************
 * NormalTransaction
 ******************************************************************************/

NormalTransaction::NormalTransaction(
    SafeRefPtr<Database> aDatabase, TransactionBase::Mode aMode,
    TransactionBase::Durability aDurability,
    nsTArray<SafeRefPtr<FullObjectStoreMetadata>>&& aObjectStores)
    : TransactionBase(std::move(aDatabase), aMode, aDurability),
      mObjectStores{std::move(aObjectStores)} {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mObjectStores.IsEmpty());
}

bool NormalTransaction::IsSameProcessActor() {
  AssertIsOnBackgroundThread();

  PBackgroundParent* const actor = Manager()->Manager()->Manager();
  MOZ_ASSERT(actor);

  return !BackgroundParent::IsOtherProcessActor(actor);
}

void NormalTransaction::SendCompleteNotification(nsresult aResult) {
  AssertIsOnBackgroundThread();

  if (!IsActorDestroyed()) {
    Unused << SendComplete(aResult);
  }
}

void NormalTransaction::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();

  NoteActorDestroyed();

  if (!mCommittedOrAborted) {
    if (NS_SUCCEEDED(mResultCode)) {
      IDB_REPORT_INTERNAL_ERR();
      mResultCode = NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }

    mForceAborted.EnsureFlipped();

    MaybeCommitOrAbort();
  }
}

mozilla::ipc::IPCResult NormalTransaction::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!IsActorDestroyed());

  QM_WARNONLY_TRY(OkIf(PBackgroundIDBTransactionParent::Send__delete__(this)));

  return IPC_OK();
}

mozilla::ipc::IPCResult NormalTransaction::RecvCommit(
    const Maybe<int64_t>& aLastRequest) {
  AssertIsOnBackgroundThread();

  return TransactionBase::RecvCommit(this, aLastRequest);
}

mozilla::ipc::IPCResult NormalTransaction::RecvAbort(
    const nsresult& aResultCode) {
  AssertIsOnBackgroundThread();

  return TransactionBase::RecvAbort(this, aResultCode);
}

PBackgroundIDBRequestParent*
NormalTransaction::AllocPBackgroundIDBRequestParent(
    const int64_t& aRequestId, const RequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != RequestParams::T__None);

  return AllocRequest(aRequestId,
                      std::move(const_cast<RequestParams&>(aParams)),
                      IsSameProcessActor());
}

mozilla::ipc::IPCResult NormalTransaction::RecvPBackgroundIDBRequestConstructor(
    PBackgroundIDBRequestParent* const aActor, const int64_t& aRequestId,
    const RequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != RequestParams::T__None);

  if (!StartRequest(aActor)) {
    return IPC_FAIL(this, "StartRequest failed!");
  }
  return IPC_OK();
}

bool NormalTransaction::DeallocPBackgroundIDBRequestParent(
    PBackgroundIDBRequestParent* const aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  return DeallocRequest(aActor);
}

already_AddRefed<PBackgroundIDBCursorParent>
NormalTransaction::AllocPBackgroundIDBCursorParent(
    const int64_t& aRequestId, const OpenCursorParams& aParams) {
  AssertIsOnBackgroundThread();

  return AllocCursor(aParams, IsSameProcessActor());
}

mozilla::ipc::IPCResult NormalTransaction::RecvPBackgroundIDBCursorConstructor(
    PBackgroundIDBCursorParent* const aActor, const int64_t& aRequestId,
    const OpenCursorParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != OpenCursorParams::T__None);

  if (!StartCursor(aActor, aRequestId, aParams)) {
    return IPC_FAIL(this, "StartCursor failed!");
  }
  return IPC_OK();
}

/*******************************************************************************
 * VersionChangeTransaction
 ******************************************************************************/

VersionChangeTransaction::VersionChangeTransaction(
    OpenDatabaseOp* aOpenDatabaseOp)
    : TransactionBase(aOpenDatabaseOp->mDatabase.clonePtr(),
                      IDBTransaction::Mode::VersionChange,
                      // VersionChange must not change durability.
                      IDBTransaction::Durability::Default),  // Not used.
      mOpenDatabaseOp(aOpenDatabaseOp) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aOpenDatabaseOp);
}

VersionChangeTransaction::~VersionChangeTransaction() {
#ifdef DEBUG
  // Silence the base class' destructor assertion if we never made this actor
  // live.
  FakeActorDestroyed();
#endif
}

bool VersionChangeTransaction::IsSameProcessActor() {
  AssertIsOnBackgroundThread();

  PBackgroundParent* actor = Manager()->Manager()->Manager();
  MOZ_ASSERT(actor);

  return !BackgroundParent::IsOtherProcessActor(actor);
}

void VersionChangeTransaction::SetActorAlive() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!IsActorDestroyed());

  mActorWasAlive.Flip();
}

bool VersionChangeTransaction::CopyDatabaseMetadata() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mOldMetadata);

  const auto& origMetadata = GetDatabase().Metadata();

  SafeRefPtr<FullDatabaseMetadata> newMetadata = origMetadata.Duplicate();
  if (NS_WARN_IF(!newMetadata)) {
    return false;
  }

  // Replace the live metadata with the new mutable copy.
  DatabaseActorInfo* info;
  MOZ_ALWAYS_TRUE(gLiveDatabaseHashtable->Get(origMetadata.mDatabaseId, &info));
  MOZ_ASSERT(!info->mLiveDatabases.IsEmpty());
  MOZ_ASSERT(info->mMetadata == &origMetadata);

  mOldMetadata = std::move(info->mMetadata);
  info->mMetadata = std::move(newMetadata);

  // Replace metadata pointers for all live databases.
  for (const auto& liveDatabase : info->mLiveDatabases) {
    liveDatabase->mMetadata = info->mMetadata.clonePtr();
  }

  return true;
}

void VersionChangeTransaction::UpdateMetadata(nsresult aResult) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mOpenDatabaseOp);
  MOZ_ASSERT(!!mActorWasAlive == !!mOpenDatabaseOp->mDatabase);
  MOZ_ASSERT_IF(mActorWasAlive, !mOpenDatabaseOp->mDatabaseId.ref().IsEmpty());

  if (IsActorDestroyed() || !mActorWasAlive) {
    return;
  }

  SafeRefPtr<FullDatabaseMetadata> oldMetadata = std::move(mOldMetadata);

  DatabaseActorInfo* info;
  if (!gLiveDatabaseHashtable->Get(oldMetadata->mDatabaseId, &info)) {
    return;
  }

  MOZ_ASSERT(!info->mLiveDatabases.IsEmpty());

  if (NS_SUCCEEDED(aResult)) {
    // Remove all deleted objectStores and indexes, then mark immutable.
    info->mMetadata->mObjectStores.RemoveIf([](const auto& objectStoreIter) {
      MOZ_ASSERT(objectStoreIter.Key());
      const SafeRefPtr<FullObjectStoreMetadata>& metadata =
          objectStoreIter.Data();
      MOZ_ASSERT(metadata);

      if (metadata->mDeleted) {
        return true;
      }

      metadata->mIndexes.RemoveIf([](const auto& indexIter) -> bool {
        MOZ_ASSERT(indexIter.Key());
        const SafeRefPtr<FullIndexMetadata>& index = indexIter.Data();
        MOZ_ASSERT(index);

        return index->mDeleted;
      });
      metadata->mIndexes.MarkImmutable();

      return false;
    });

    info->mMetadata->mObjectStores.MarkImmutable();
  } else {
    // Replace metadata pointers for all live databases.
    info->mMetadata = std::move(oldMetadata);

    for (auto& liveDatabase : info->mLiveDatabases) {
      liveDatabase->mMetadata = info->mMetadata.clonePtr();
    }
  }
}

void VersionChangeTransaction::SendCompleteNotification(nsresult aResult) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mOpenDatabaseOp);
  MOZ_ASSERT(!mOpenDatabaseOp->mCompleteCallback);
  MOZ_ASSERT_IF(!mActorWasAlive, mOpenDatabaseOp->HasFailed());
  MOZ_ASSERT_IF(!mActorWasAlive, mOpenDatabaseOp->mState >
                                     OpenDatabaseOp::State::SendingResults);

  const RefPtr<OpenDatabaseOp> openDatabaseOp = std::move(mOpenDatabaseOp);

  if (!mActorWasAlive) {
    return;
  }

  openDatabaseOp->mCompleteCallback =
      [self = SafeRefPtr{this, AcquireStrongRefFromRawPtr{}}, aResult]() {
        if (!self->IsActorDestroyed()) {
          Unused << self->SendComplete(aResult);
        }
      };

  auto handleError = [openDatabaseOp](const nsresult rv) {
    openDatabaseOp->SetFailureCodeIfUnset(NS_ERROR_DOM_INDEXEDDB_ABORT_ERR);

    openDatabaseOp->mState = OpenDatabaseOp::State::SendingResults;

    MOZ_ALWAYS_SUCCEEDS(openDatabaseOp->Run());
  };

  if (NS_FAILED(aResult)) {
    // 3.3.1 Opening a database:
    // "If the upgrade transaction was aborted, run the steps for closing a
    //  database connection with connection, create and return a new AbortError
    //  exception and abort these steps."
    handleError(NS_ERROR_DOM_INDEXEDDB_ABORT_ERR);
    return;
  }

  openDatabaseOp->mState = OpenDatabaseOp::State::DatabaseWorkVersionUpdate;

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY(MOZ_TO_RESULT(quotaManager->IOThread()->Dispatch(openDatabaseOp,
                                                          NS_DISPATCH_NORMAL))
             .mapErr(
                 [](const auto) { return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR; }),
         QM_VOID, handleError);
}

void VersionChangeTransaction::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();

  NoteActorDestroyed();

  if (!mCommittedOrAborted) {
    if (NS_SUCCEEDED(mResultCode)) {
      IDB_REPORT_INTERNAL_ERR();
      mResultCode = NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }

    mForceAborted.EnsureFlipped();

    MaybeCommitOrAbort();
  }
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!IsActorDestroyed());

  QM_WARNONLY_TRY(
      OkIf(PBackgroundIDBVersionChangeTransactionParent::Send__delete__(this)));

  return IPC_OK();
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvCommit(
    const Maybe<int64_t>& aLastRequest) {
  AssertIsOnBackgroundThread();

  return TransactionBase::RecvCommit(this, aLastRequest);
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvAbort(
    const nsresult& aResultCode) {
  AssertIsOnBackgroundThread();

  return TransactionBase::RecvAbort(this, aResultCode);
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvCreateObjectStore(
    const ObjectStoreMetadata& aMetadata) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!aMetadata.id())) {
    return IPC_FAIL(this, "No metadata ID!");
  }

  const SafeRefPtr<FullDatabaseMetadata> dbMetadata =
      GetDatabase().MetadataPtr();

  if (NS_WARN_IF(aMetadata.id() != dbMetadata->mNextObjectStoreId)) {
    return IPC_FAIL(this, "Requested metadata ID does not match next ID!");
  }

  if (NS_WARN_IF(
          MatchMetadataNameOrId(dbMetadata->mObjectStores, aMetadata.id(),
                                SomeRef<const nsAString&>(aMetadata.name()))
              .isSome())) {
    return IPC_FAIL(this, "MatchMetadataNameOrId failed!");
  }

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(this, "Transaction is already committed/aborted!");
  }

  const int64_t initialAutoIncrementId = aMetadata.autoIncrement() ? 1 : 0;
  auto newMetadata = MakeSafeRefPtr<FullObjectStoreMetadata>(
      aMetadata, FullObjectStoreMetadata::AutoIncrementIds{
                     initialAutoIncrementId, initialAutoIncrementId});

  if (NS_WARN_IF(!dbMetadata->mObjectStores.InsertOrUpdate(
          aMetadata.id(), std::move(newMetadata), fallible))) {
    return IPC_FAIL(this, "mObjectStores.InsertOrUpdate failed!");
  }

  dbMetadata->mNextObjectStoreId++;

  RefPtr<CreateObjectStoreOp> op = new CreateObjectStoreOp(
      SafeRefPtrFromThis().downcast<VersionChangeTransaction>(), aMetadata);

  if (NS_WARN_IF(!op->Init(*this))) {
    op->Cleanup();
    return IPC_FAIL(this, "ObjectStoreOp initialization failed!");
  }

  op->DispatchToConnectionPool();

  return IPC_OK();
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvDeleteObjectStore(
    const IndexOrObjectStoreId& aObjectStoreId) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!aObjectStoreId)) {
    return IPC_FAIL(this, "No ObjectStoreId!");
  }

  const auto& dbMetadata = GetDatabase().Metadata();
  MOZ_ASSERT(dbMetadata.mNextObjectStoreId > 0);

  if (NS_WARN_IF(aObjectStoreId >= dbMetadata.mNextObjectStoreId)) {
    return IPC_FAIL(this, "Invalid ObjectStoreId!");
  }

  SafeRefPtr<FullObjectStoreMetadata> foundMetadata =
      GetMetadataForObjectStoreId(aObjectStoreId);

  if (NS_WARN_IF(!foundMetadata)) {
    return IPC_FAIL(this, "No metadata found for ObjectStoreId!");
  }

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(this, "Transaction is already committed/aborted!");
  }

  foundMetadata->mDeleted.Flip();

  DebugOnly<bool> foundTargetId = false;
  const bool isLastObjectStore = std::all_of(
      dbMetadata.mObjectStores.begin(), dbMetadata.mObjectStores.end(),
      [&foundTargetId, aObjectStoreId](const auto& objectStoreEntry) -> bool {
        if (uint64_t(aObjectStoreId) == objectStoreEntry.GetKey()) {
          foundTargetId = true;
          return true;
        }

        return objectStoreEntry.GetData()->mDeleted;
      });
  MOZ_ASSERT_IF(isLastObjectStore, foundTargetId);

  RefPtr<DeleteObjectStoreOp> op = new DeleteObjectStoreOp(
      SafeRefPtrFromThis().downcast<VersionChangeTransaction>(),
      std::move(foundMetadata), isLastObjectStore);

  if (NS_WARN_IF(!op->Init(*this))) {
    op->Cleanup();
    return IPC_FAIL(this, "ObjectStoreOp initialization failed!");
  }

  op->DispatchToConnectionPool();

  return IPC_OK();
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvRenameObjectStore(
    const IndexOrObjectStoreId& aObjectStoreId, const nsAString& aName) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!aObjectStoreId)) {
    return IPC_FAIL(this, "No ObjectStoreId!");
  }

  {
    const auto& dbMetadata = GetDatabase().Metadata();
    MOZ_ASSERT(dbMetadata.mNextObjectStoreId > 0);

    if (NS_WARN_IF(aObjectStoreId >= dbMetadata.mNextObjectStoreId)) {
      return IPC_FAIL(this, "Invalid ObjectStoreId!");
    }
  }

  SafeRefPtr<FullObjectStoreMetadata> foundMetadata =
      GetMetadataForObjectStoreId(aObjectStoreId);

  if (NS_WARN_IF(!foundMetadata)) {
    return IPC_FAIL(this, "No metadata found for ObjectStoreId!");
  }

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(this, "Transaction is already committed/aborted!");
  }

  foundMetadata->mCommonMetadata.name() = aName;

  RefPtr<RenameObjectStoreOp> renameOp = new RenameObjectStoreOp(
      SafeRefPtrFromThis().downcast<VersionChangeTransaction>(),
      *foundMetadata);

  if (NS_WARN_IF(!renameOp->Init(*this))) {
    renameOp->Cleanup();
    return IPC_FAIL(this, "ObjectStoreOp initialization failed!");
  }

  renameOp->DispatchToConnectionPool();

  return IPC_OK();
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvCreateIndex(
    const IndexOrObjectStoreId& aObjectStoreId,
    const IndexMetadata& aMetadata) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!aObjectStoreId)) {
    return IPC_FAIL(this, "No ObjectStoreId!");
  }

  if (NS_WARN_IF(!aMetadata.id())) {
    return IPC_FAIL(this, "No Metadata id!");
  }

  const auto dbMetadata = GetDatabase().MetadataPtr();

  if (NS_WARN_IF(aMetadata.id() != dbMetadata->mNextIndexId)) {
    return IPC_FAIL(this, "Requested metadata ID does not match next ID!");
  }

  SafeRefPtr<FullObjectStoreMetadata> foundObjectStoreMetadata =
      GetMetadataForObjectStoreId(aObjectStoreId);

  if (NS_WARN_IF(!foundObjectStoreMetadata)) {
    return IPC_FAIL(this, "GetMetadataForObjectStoreId failed!");
  }

  if (NS_WARN_IF(MatchMetadataNameOrId(
                     foundObjectStoreMetadata->mIndexes, aMetadata.id(),
                     SomeRef<const nsAString&>(aMetadata.name()))
                     .isSome())) {
    return IPC_FAIL(this, "MatchMetadataNameOrId failed!");
  }

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(this, "Transaction is already committed/aborted!");
  }

  auto newMetadata = MakeSafeRefPtr<FullIndexMetadata>();
  newMetadata->mCommonMetadata = aMetadata;

  if (NS_WARN_IF(!foundObjectStoreMetadata->mIndexes.InsertOrUpdate(
          aMetadata.id(), std::move(newMetadata), fallible))) {
    return IPC_FAIL(this, "mIndexes.InsertOrUpdate failed!");
  }

  dbMetadata->mNextIndexId++;

  RefPtr<CreateIndexOp> op = new CreateIndexOp(
      SafeRefPtrFromThis().downcast<VersionChangeTransaction>(), aObjectStoreId,
      aMetadata);

  if (NS_WARN_IF(!op->Init(*this))) {
    op->Cleanup();
    return IPC_FAIL(this, "ObjectStoreOp initialization failed!");
  }

  op->DispatchToConnectionPool();

  return IPC_OK();
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvDeleteIndex(
    const IndexOrObjectStoreId& aObjectStoreId,
    const IndexOrObjectStoreId& aIndexId) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!aObjectStoreId)) {
    return IPC_FAIL(this, "No ObjectStoreId!");
  }

  if (NS_WARN_IF(!aIndexId)) {
    return IPC_FAIL(this, "No Index id!");
  }
  {
    const auto& dbMetadata = GetDatabase().Metadata();
    MOZ_ASSERT(dbMetadata.mNextObjectStoreId > 0);
    MOZ_ASSERT(dbMetadata.mNextIndexId > 0);

    if (NS_WARN_IF(aObjectStoreId >= dbMetadata.mNextObjectStoreId)) {
      return IPC_FAIL(this, "Requested ObjectStoreId does not match next ID!");
    }

    if (NS_WARN_IF(aIndexId >= dbMetadata.mNextIndexId)) {
      return IPC_FAIL(this, "Requested IndexId does not match next ID!");
    }
  }

  SafeRefPtr<FullObjectStoreMetadata> foundObjectStoreMetadata =
      GetMetadataForObjectStoreId(aObjectStoreId);

  if (NS_WARN_IF(!foundObjectStoreMetadata)) {
    return IPC_FAIL(this, "GetMetadataForObjectStoreId failed!");
  }

  SafeRefPtr<FullIndexMetadata> foundIndexMetadata =
      GetMetadataForIndexId(*foundObjectStoreMetadata, aIndexId);

  if (NS_WARN_IF(!foundIndexMetadata)) {
    return IPC_FAIL(this, "GetMetadataForIndexId failed!");
  }

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(this, "Transaction is already committed/aborted!");
  }

  foundIndexMetadata->mDeleted.Flip();

  DebugOnly<bool> foundTargetId = false;
  const bool isLastIndex =
      std::all_of(foundObjectStoreMetadata->mIndexes.cbegin(),
                  foundObjectStoreMetadata->mIndexes.cend(),
                  [&foundTargetId, aIndexId](const auto& indexEntry) -> bool {
                    if (uint64_t(aIndexId) == indexEntry.GetKey()) {
                      foundTargetId = true;
                      return true;
                    }

                    return indexEntry.GetData()->mDeleted;
                  });
  MOZ_ASSERT_IF(isLastIndex, foundTargetId);

  RefPtr<DeleteIndexOp> op = new DeleteIndexOp(
      SafeRefPtrFromThis().downcast<VersionChangeTransaction>(), aObjectStoreId,
      aIndexId, foundIndexMetadata->mCommonMetadata.unique(), isLastIndex);

  if (NS_WARN_IF(!op->Init(*this))) {
    op->Cleanup();
    return IPC_FAIL(this, "ObjectStoreOp initialization failed!");
  }

  op->DispatchToConnectionPool();

  return IPC_OK();
}

mozilla::ipc::IPCResult VersionChangeTransaction::RecvRenameIndex(
    const IndexOrObjectStoreId& aObjectStoreId,
    const IndexOrObjectStoreId& aIndexId, const nsAString& aName) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(!aObjectStoreId)) {
    return IPC_FAIL(this, "No ObjectStoreId!");
  }

  if (NS_WARN_IF(!aIndexId)) {
    return IPC_FAIL(this, "No Index id!");
  }

  const SafeRefPtr<FullDatabaseMetadata> dbMetadata =
      GetDatabase().MetadataPtr();
  MOZ_ASSERT(dbMetadata);
  MOZ_ASSERT(dbMetadata->mNextObjectStoreId > 0);
  MOZ_ASSERT(dbMetadata->mNextIndexId > 0);

  if (NS_WARN_IF(aObjectStoreId >= dbMetadata->mNextObjectStoreId)) {
    return IPC_FAIL(this, "Requested ObjectStoreId does not match next ID!");
  }

  if (NS_WARN_IF(aIndexId >= dbMetadata->mNextIndexId)) {
    return IPC_FAIL(this, "Requested IndexId does not match next ID!");
  }

  SafeRefPtr<FullObjectStoreMetadata> foundObjectStoreMetadata =
      GetMetadataForObjectStoreId(aObjectStoreId);

  if (NS_WARN_IF(!foundObjectStoreMetadata)) {
    return IPC_FAIL(this, "GetMetadataForObjectStoreId failed!");
  }

  SafeRefPtr<FullIndexMetadata> foundIndexMetadata =
      GetMetadataForIndexId(*foundObjectStoreMetadata, aIndexId);

  if (NS_WARN_IF(!foundIndexMetadata)) {
    return IPC_FAIL(this, "GetMetadataForIndexId failed!");
  }

  if (NS_WARN_IF(mCommitOrAbortReceived)) {
    return IPC_FAIL(this, "Transaction is already committed/aborted!");
  }

  foundIndexMetadata->mCommonMetadata.name() = aName;

  RefPtr<RenameIndexOp> renameOp = new RenameIndexOp(
      SafeRefPtrFromThis().downcast<VersionChangeTransaction>(),
      *foundIndexMetadata, aObjectStoreId);

  if (NS_WARN_IF(!renameOp->Init(*this))) {
    renameOp->Cleanup();
    return IPC_FAIL(this, "ObjectStoreOp initialization failed!");
  }

  renameOp->DispatchToConnectionPool();

  return IPC_OK();
}

PBackgroundIDBRequestParent*
VersionChangeTransaction::AllocPBackgroundIDBRequestParent(
    const int64_t& aRequestId, const RequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != RequestParams::T__None);

  return AllocRequest(aRequestId,
                      std::move(const_cast<RequestParams&>(aParams)),
                      IsSameProcessActor());
}

mozilla::ipc::IPCResult
VersionChangeTransaction::RecvPBackgroundIDBRequestConstructor(
    PBackgroundIDBRequestParent* aActor, const int64_t& aRequestId,
    const RequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != RequestParams::T__None);

  if (!StartRequest(aActor)) {
    return IPC_FAIL(this, "StartRequest failed!");
  }
  return IPC_OK();
}

bool VersionChangeTransaction::DeallocPBackgroundIDBRequestParent(
    PBackgroundIDBRequestParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  return DeallocRequest(aActor);
}

already_AddRefed<PBackgroundIDBCursorParent>
VersionChangeTransaction::AllocPBackgroundIDBCursorParent(
    const int64_t& aRequestId, const OpenCursorParams& aParams) {
  AssertIsOnBackgroundThread();

  return AllocCursor(aParams, IsSameProcessActor());
}

mozilla::ipc::IPCResult
VersionChangeTransaction::RecvPBackgroundIDBCursorConstructor(
    PBackgroundIDBCursorParent* aActor, const int64_t& aRequestId,
    const OpenCursorParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(aParams.type() != OpenCursorParams::T__None);

  if (!StartCursor(aActor, aRequestId, aParams)) {
    return IPC_FAIL(this, "StartCursor failed!");
  }
  return IPC_OK();
}

/*******************************************************************************
 * CursorBase
 ******************************************************************************/

CursorBase::CursorBase(SafeRefPtr<TransactionBase> aTransaction,
                       SafeRefPtr<FullObjectStoreMetadata> aObjectStoreMetadata,
                       const Direction aDirection,
                       const ConstructFromTransactionBase /*aConstructionTag*/)
    : mTransaction(std::move(aTransaction)),
      mObjectStoreMetadata(WrapNotNull(std::move(aObjectStoreMetadata))),
      mObjectStoreId((*mObjectStoreMetadata)->mCommonMetadata.id()),
      mDirection(aDirection),
      mMaxExtraCount(IndexedDatabaseManager::MaxPreloadExtraRecords()),
      mIsSameProcessActor(!BackgroundParent::IsOtherProcessActor(
          mTransaction->GetBackgroundParent())) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mTransaction);

  static_assert(
      OpenCursorParams::T__None == 0 && OpenCursorParams::T__Last == 4,
      "Lots of code here assumes only four types of cursors!");
}

template <IDBCursorType CursorType>
bool Cursor<CursorType>::VerifyRequestParams(
    const CursorRequestParams& aParams,
    const CursorPosition<CursorType>& aPosition) const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != CursorRequestParams::T__None);
  MOZ_ASSERT(this->mObjectStoreMetadata);
  if constexpr (IsIndexCursor) {
    MOZ_ASSERT(this->mIndexMetadata);
  }

#ifdef DEBUG
  {
    const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
        mTransaction->GetMetadataForObjectStoreId(mObjectStoreId);
    if (objectStoreMetadata) {
      MOZ_ASSERT(objectStoreMetadata == (*this->mObjectStoreMetadata));
    } else {
      MOZ_ASSERT((*this->mObjectStoreMetadata)->mDeleted);
    }

    if constexpr (IsIndexCursor) {
      if (objectStoreMetadata) {
        const SafeRefPtr<FullIndexMetadata> indexMetadata =
            mTransaction->GetMetadataForIndexId(*objectStoreMetadata,
                                                this->mIndexId);
        if (indexMetadata) {
          MOZ_ASSERT(indexMetadata == *this->mIndexMetadata);
        } else {
          MOZ_ASSERT((*this->mIndexMetadata)->mDeleted);
        }
      }
    }
  }
#endif

  if (NS_AUUF_OR_WARN_IF((*this->mObjectStoreMetadata)->mDeleted)) {
    return false;
  }

  if constexpr (IsIndexCursor) {
    if (NS_AUUF_OR_WARN_IF(this->mIndexMetadata &&
                           (*this->mIndexMetadata)->mDeleted)) {
      return false;
    }
  }

  const Key& sortKey = aPosition.GetSortKey(this->IsLocaleAware());

  switch (aParams.type()) {
    case CursorRequestParams::TContinueParams: {
      const Key& key = aParams.get_ContinueParams().key();
      if (!key.IsUnset()) {
        switch (mDirection) {
          case IDBCursorDirection::Next:
          case IDBCursorDirection::Nextunique:
            if (NS_AUUF_OR_WARN_IF(key <= sortKey)) {
              return false;
            }
            break;

          case IDBCursorDirection::Prev:
          case IDBCursorDirection::Prevunique:
            if (NS_AUUF_OR_WARN_IF(key >= sortKey)) {
              return false;
            }
            break;

          default:
            MOZ_CRASH("Should never get here!");
        }
      }
      break;
    }

    case CursorRequestParams::TContinuePrimaryKeyParams: {
      if constexpr (IsIndexCursor) {
        const Key& key = aParams.get_ContinuePrimaryKeyParams().key();
        const Key& primaryKey =
            aParams.get_ContinuePrimaryKeyParams().primaryKey();
        MOZ_ASSERT(!key.IsUnset());
        MOZ_ASSERT(!primaryKey.IsUnset());
        switch (mDirection) {
          case IDBCursorDirection::Next:
            if (NS_AUUF_OR_WARN_IF(key < sortKey ||
                                   (key == sortKey &&
                                    primaryKey <= aPosition.mObjectStoreKey))) {
              return false;
            }
            break;

          case IDBCursorDirection::Prev:
            if (NS_AUUF_OR_WARN_IF(key > sortKey ||
                                   (key == sortKey &&
                                    primaryKey >= aPosition.mObjectStoreKey))) {
              return false;
            }
            break;

          default:
            MOZ_CRASH("Should never get here!");
        }
      }
      break;
    }

    case CursorRequestParams::TAdvanceParams:
      if (NS_AUUF_OR_WARN_IF(!aParams.get_AdvanceParams().count())) {
        return false;
      }
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  return true;
}

template <IDBCursorType CursorType>
bool Cursor<CursorType>::Start(const int64_t aRequestId,
                               const OpenCursorParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() == ToOpenCursorParamsType(CursorType));
  MOZ_ASSERT(this->mObjectStoreMetadata);

  if (NS_AUUF_OR_WARN_IF(mCurrentlyRunningOp)) {
    return false;
  }

  const Maybe<SerializedKeyRange>& optionalKeyRange =
      GetCommonOpenCursorParams(aParams).optionalKeyRange();

  const RefPtr<OpenOp> openOp = new OpenOp(this, aRequestId, optionalKeyRange);

  if (NS_WARN_IF(!openOp->Init(*mTransaction))) {
    openOp->Cleanup();
    return false;
  }

  openOp->DispatchToConnectionPool();
  mCurrentlyRunningOp = openOp;

  return true;
}

void ValueCursorBase::ProcessFiles(CursorResponse& aResponse,
                                   const FilesArray& aFiles) {
  MOZ_ASSERT_IF(
      aResponse.type() == CursorResponse::Tnsresult ||
          aResponse.type() == CursorResponse::Tvoid_t ||
          aResponse.type() ==
              CursorResponse::TArrayOfObjectStoreKeyCursorResponse ||
          aResponse.type() == CursorResponse::TArrayOfIndexKeyCursorResponse,
      aFiles.IsEmpty());

  for (size_t i = 0; i < aFiles.Length(); ++i) {
    const auto& files = aFiles[i];
    if (!files.IsEmpty()) {
      // TODO: Replace this assertion by one that checks if the response type
      // matches the cursor type, at a more generic location.
      MOZ_ASSERT(aResponse.type() ==
                     CursorResponse::TArrayOfObjectStoreCursorResponse ||
                 aResponse.type() ==
                     CursorResponse::TArrayOfIndexCursorResponse);

      SerializedStructuredCloneReadInfo* serializedInfo = nullptr;
      switch (aResponse.type()) {
        case CursorResponse::TArrayOfObjectStoreCursorResponse: {
          auto& responses = aResponse.get_ArrayOfObjectStoreCursorResponse();
          MOZ_ASSERT(i < responses.Length());
          serializedInfo = &responses[i].cloneInfo();
          break;
        }

        case CursorResponse::TArrayOfIndexCursorResponse: {
          auto& responses = aResponse.get_ArrayOfIndexCursorResponse();
          MOZ_ASSERT(i < responses.Length());
          serializedInfo = &responses[i].cloneInfo();
          break;
        }

        default:
          MOZ_CRASH("Should never get here!");
      }

      MOZ_ASSERT(serializedInfo);
      MOZ_ASSERT(serializedInfo->files().IsEmpty());
      MOZ_ASSERT(this->mDatabase);

      QM_TRY_UNWRAP(serializedInfo->files(),
                    SerializeStructuredCloneFiles(this->mDatabase, files,
                                                  /* aForPreprocess */ false),
                    QM_VOID, [&aResponse](const nsresult result) {
                      aResponse = ClampResultCode(result);
                    });
    }
  }
}

template <IDBCursorType CursorType>
void Cursor<CursorType>::SendResponseInternal(
    CursorResponse& aResponse, const FilesArrayT<CursorType>& aFiles) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aResponse.type() != CursorResponse::T__None);
  MOZ_ASSERT_IF(aResponse.type() == CursorResponse::Tnsresult,
                NS_FAILED(aResponse.get_nsresult()));
  MOZ_ASSERT_IF(aResponse.type() == CursorResponse::Tnsresult,
                NS_ERROR_GET_MODULE(aResponse.get_nsresult()) ==
                    NS_ERROR_MODULE_DOM_INDEXEDDB);
  MOZ_ASSERT(this->mObjectStoreMetadata);
  MOZ_ASSERT(mCurrentlyRunningOp);

  KeyValueBase::ProcessFiles(aResponse, aFiles);

  // Work around the deleted function by casting to the base class.
  QM_WARNONLY_TRY(OkIf(
      static_cast<PBackgroundIDBCursorParent*>(this)->SendResponse(aResponse)));

  mCurrentlyRunningOp = nullptr;
}

template <IDBCursorType CursorType>
void Cursor<CursorType>::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();

  if (mCurrentlyRunningOp) {
    mCurrentlyRunningOp->NoteActorDestroyed();
  }

  if constexpr (IsValueCursor) {
    this->mBackgroundParent.destroy();
  }
  this->mObjectStoreMetadata.destroy();
  if constexpr (IsIndexCursor) {
    this->mIndexMetadata.destroy();
  }
}

template <IDBCursorType CursorType>
mozilla::ipc::IPCResult Cursor<CursorType>::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(this->mObjectStoreMetadata);

  if (NS_WARN_IF(mCurrentlyRunningOp)) {
    return IPC_FAIL(
        this,
        "Attempt to delete a cursor with a non-null mCurrentlyRunningOp!");
  }

  QM_WARNONLY_TRY(OkIf(PBackgroundIDBCursorParent::Send__delete__(this)));

  return IPC_OK();
}

template <IDBCursorType CursorType>
mozilla::ipc::IPCResult Cursor<CursorType>::RecvContinue(
    const int64_t& aRequestId, const CursorRequestParams& aParams,
    const Key& aCurrentKey, const Key& aCurrentObjectStoreKey) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() != CursorRequestParams::T__None);
  MOZ_ASSERT(this->mObjectStoreMetadata);
  if constexpr (IsIndexCursor) {
    MOZ_ASSERT(this->mIndexMetadata);
  }

  const bool trustParams =
#ifdef DEBUG
      // Always verify parameters in DEBUG builds!
      false
#else
      this->mIsSameProcessActor
#endif
      ;

  MOZ_ASSERT(!aCurrentKey.IsUnset());

  QM_TRY_UNWRAP(
      auto position,
      ([&]() -> Result<CursorPosition<CursorType>, mozilla::ipc::IPCResult> {
        if constexpr (IsIndexCursor) {
          auto localeAwarePosition = Key{};
          if (this->IsLocaleAware()) {
            QM_TRY_UNWRAP(
                localeAwarePosition,
                aCurrentKey.ToLocaleAwareKey(this->mLocale),
                Err(IPC_FAIL(this, "aCurrentKey.ToLocaleAwareKey failed!")));
          }
          return CursorPosition<CursorType>{aCurrentKey, localeAwarePosition,
                                            aCurrentObjectStoreKey};
        } else {
          return CursorPosition<CursorType>{aCurrentKey};
        }
      }()));

  if (!trustParams && !VerifyRequestParams(aParams, position)) {
    return IPC_FAIL(this, "VerifyRequestParams failed!");
  }

  if (NS_WARN_IF(mCurrentlyRunningOp)) {
    return IPC_FAIL(this, "Cursor is CurrentlyRunningOp!");
  }

  if (NS_WARN_IF(mTransaction->mCommitOrAbortReceived)) {
    return IPC_FAIL(this, "Transaction is already committed/aborted!");
  }

  const RefPtr<ContinueOp> continueOp =
      new ContinueOp(this, aRequestId, aParams, std::move(position));
  if (NS_WARN_IF(!continueOp->Init(*mTransaction))) {
    continueOp->Cleanup();
    return IPC_FAIL(this, "ContinueOp initialization failed!");
  }

  continueOp->DispatchToConnectionPool();
  mCurrentlyRunningOp = continueOp;

  return IPC_OK();
}

/*******************************************************************************
 * DatabaseFileManager
 ******************************************************************************/

DatabaseFileManager::MutexType DatabaseFileManager::sMutex;

DatabaseFileManager::DatabaseFileManager(
    PersistenceType aPersistenceType,
    const quota::OriginMetadata& aOriginMetadata,
    const nsAString& aDatabaseName, const nsCString& aDatabaseID,
    const nsAString& aDatabaseFilePath, bool aEnforcingQuota,
    bool aIsInPrivateBrowsingMode)
    : mPersistenceType(aPersistenceType),
      mOriginMetadata(aOriginMetadata),
      mDatabaseName(aDatabaseName),
      mDatabaseID(aDatabaseID),
      mDatabaseFilePath(aDatabaseFilePath),
      mCipherKeyManager(
          aIsInPrivateBrowsingMode
              ? new IndexedDBCipherKeyManager("IndexedDBCipherKeyManager")
              : nullptr),
      mDatabaseVersion(0),
      mEnforcingQuota(aEnforcingQuota),
      mIsInPrivateBrowsingMode(aIsInPrivateBrowsingMode) {}

uint64_t DatabaseFileManager::DatabaseVersion() const {
  AssertIsOnIOThread();

  return mDatabaseVersion;
}

void DatabaseFileManager::UpdateDatabaseVersion(uint64_t aDatabaseVersion) {
  AssertIsOnIOThread();

  mDatabaseVersion = aDatabaseVersion;
}

nsresult DatabaseFileManager::Init(nsIFile* aDirectory,
                                   const uint64_t aDatabaseVersion,
                                   mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);

  {
    QM_TRY_INSPECT(const bool& existsAsDirectory,
                   ExistsAsDirectory(*aDirectory));

    if (!existsAsDirectory) {
      QM_TRY(MOZ_TO_RESULT(aDirectory->Create(nsIFile::DIRECTORY_TYPE, 0755)));
    }

    QM_TRY_UNWRAP(auto path, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                 nsString, aDirectory, GetPath));

    mDirectoryPath.init(std::move(path));
  }

  QM_TRY_INSPECT(const auto& journalDirectory,
                 CloneFileAndAppend(*aDirectory, kJournalDirectoryName));

  // We don't care if it doesn't exist at all, but if it does exist, make sure
  // it's a directory.
  QM_TRY_INSPECT(const bool& existsAsDirectory,
                 ExistsAsDirectory(*journalDirectory));
  Unused << existsAsDirectory;

  {
    QM_TRY_UNWRAP(auto path, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                 nsString, journalDirectory, GetPath));

    mJournalDirectoryPath.init(std::move(path));
  }

  mDatabaseVersion = aDatabaseVersion;

  QM_TRY_INSPECT(const auto& stmt,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConnection,
                     CreateStatement, "SELECT id, refcount FROM file"_ns));

  QM_TRY(
      CollectWhileHasResult(*stmt, [this](auto& stmt) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const int64_t& id,
                       MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 0));
        QM_TRY_INSPECT(const int32_t& dbRefCnt,
                       MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt32, 1));

        // We put a raw pointer into the hash table, so the memory refcount will
        // be 0, but the dbRefCnt is non-zero, which will keep the
        // DatabaseFileInfo object alive.
        MOZ_ASSERT(dbRefCnt > 0);
        DebugOnly ok = static_cast<bool>(CreateFileInfo(Some(id), dbRefCnt));
        MOZ_ASSERT(ok);

        return Ok{};
      }));

  mInitialized.Flip();

  return NS_OK;
}

nsCOMPtr<nsIFile> DatabaseFileManager::GetDirectory() {
  if (!this->AssertValid()) {
    return nullptr;
  }

  return GetFileForPath(*mDirectoryPath);
}

nsCOMPtr<nsIFile> DatabaseFileManager::GetCheckedDirectory() {
  auto directory = GetDirectory();
  if (NS_WARN_IF(!directory)) {
    return nullptr;
  }

  DebugOnly<bool> exists;
  MOZ_ASSERT(NS_SUCCEEDED(directory->Exists(&exists)));
  MOZ_ASSERT(exists);

  DebugOnly<bool> isDirectory;
  MOZ_ASSERT(NS_SUCCEEDED(directory->IsDirectory(&isDirectory)));
  MOZ_ASSERT(isDirectory);

  return directory;
}

nsCOMPtr<nsIFile> DatabaseFileManager::GetJournalDirectory() {
  if (!this->AssertValid()) {
    return nullptr;
  }

  return GetFileForPath(*mJournalDirectoryPath);
}

nsCOMPtr<nsIFile> DatabaseFileManager::EnsureJournalDirectory() {
  // This can happen on the IO or on a transaction thread.
  MOZ_ASSERT(!NS_IsMainThread());

  auto journalDirectory = GetFileForPath(*mJournalDirectoryPath);
  QM_TRY(OkIf(journalDirectory), nullptr);

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(journalDirectory, Exists),
                 nullptr);

  if (exists) {
    QM_TRY_INSPECT(const bool& isDirectory,
                   MOZ_TO_RESULT_INVOKE_MEMBER(journalDirectory, IsDirectory),
                   nullptr);

    QM_TRY(OkIf(isDirectory), nullptr);
  } else {
    QM_TRY(
        MOZ_TO_RESULT(journalDirectory->Create(nsIFile::DIRECTORY_TYPE, 0755)),
        nullptr);
  }

  return journalDirectory;
}

// static
nsCOMPtr<nsIFile> DatabaseFileManager::GetFileForId(nsIFile* aDirectory,
                                                    int64_t aId) {
  MOZ_ASSERT(aDirectory);
  MOZ_ASSERT(aId > 0);

  QM_TRY_RETURN(CloneFileAndAppend(*aDirectory, IntToString(aId)), nullptr);
}

// static
nsCOMPtr<nsIFile> DatabaseFileManager::GetCheckedFileForId(nsIFile* aDirectory,
                                                           int64_t aId) {
  auto file = GetFileForId(aDirectory, aId);
  if (NS_WARN_IF(!file)) {
    return nullptr;
  }

  DebugOnly<bool> exists;
  MOZ_ASSERT(NS_SUCCEEDED(file->Exists(&exists)));
  MOZ_ASSERT(exists);

  DebugOnly<bool> isFile;
  MOZ_ASSERT(NS_SUCCEEDED(file->IsFile(&isFile)));
  MOZ_ASSERT(isFile);

  return file;
}

// static
nsresult DatabaseFileManager::InitDirectory(nsIFile& aDirectory,
                                            nsIFile& aDatabaseFile,
                                            const nsACString& aOrigin,
                                            uint32_t aTelemetryId) {
  AssertIsOnIOThread();

  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aDirectory, Exists));

    if (!exists) {
      return NS_OK;
    }

    QM_TRY_INSPECT(const bool& isDirectory,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aDirectory, IsDirectory));
    QM_TRY(OkIf(isDirectory), NS_ERROR_FAILURE);
  }

  QM_TRY_INSPECT(const auto& journalDirectory,
                 CloneFileAndAppend(aDirectory, kJournalDirectoryName));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(journalDirectory, Exists));

  if (exists) {
    QM_TRY_INSPECT(const bool& isDirectory,
                   MOZ_TO_RESULT_INVOKE_MEMBER(journalDirectory, IsDirectory));
    QM_TRY(OkIf(isDirectory), NS_ERROR_FAILURE);

    bool hasJournals = false;

    QM_TRY(CollectEachFile(
        *journalDirectory,
        [&hasJournals](const nsCOMPtr<nsIFile>& file) -> Result<Ok, nsresult> {
          QM_TRY_INSPECT(
              const auto& leafName,
              MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, file, GetLeafName));

          nsresult rv;
          leafName.ToInteger64(&rv);
          if (NS_SUCCEEDED(rv)) {
            hasJournals = true;
          } else {
            UNKNOWN_FILE_WARNING(leafName);
          }

          return Ok{};
        }));

    if (hasJournals) {
      QM_TRY_UNWRAP(const NotNull<nsCOMPtr<mozIStorageConnection>> connection,
                    CreateStorageConnection(
                        aDatabaseFile, aDirectory, VoidString(), aOrigin,
                        /* aDirectoryLockId */ -1, aTelemetryId, Nothing{}));

      mozStorageTransaction transaction(connection.get(), false);

      QM_TRY(MOZ_TO_RESULT(transaction.Start()))

      QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL(
          "CREATE VIRTUAL TABLE fs USING filesystem;"_ns)));

      // The parameter names are not used, parameters are bound by index only
      // locally in the same function.
      QM_TRY_INSPECT(
          const auto& stmt,
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
              nsCOMPtr<mozIStorageStatement>, *connection, CreateStatement,
              "SELECT name, (name IN (SELECT id FROM file)) FROM fs WHERE path = :path"_ns));

      QM_TRY_INSPECT(const auto& path,
                     MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                         nsString, journalDirectory, GetPath));

      QM_TRY(MOZ_TO_RESULT(stmt->BindStringByIndex(0, path)));

      QM_TRY(CollectWhileHasResult(
          *stmt,
          [&aDirectory, &journalDirectory](auto& stmt) -> Result<Ok, nsresult> {
            nsString name;
            QM_TRY(MOZ_TO_RESULT(stmt.GetString(0, name)));

            nsresult rv;
            name.ToInteger64(&rv);
            if (NS_FAILED(rv)) {
              return Ok{};
            }

            int32_t flag = stmt.AsInt32(1);

            if (!flag) {
              QM_TRY_INSPECT(const auto& file,
                             CloneFileAndAppend(aDirectory, name));

              if (NS_FAILED(file->Remove(false))) {
                NS_WARNING("Failed to remove orphaned file!");
              }
            }

            QM_TRY_INSPECT(const auto& journalFile,
                           CloneFileAndAppend(*journalDirectory, name));

            if (NS_FAILED(journalFile->Remove(false))) {
              NS_WARNING("Failed to remove journal file!");
            }

            return Ok{};
          }));

      QM_TRY(MOZ_TO_RESULT(connection->ExecuteSimpleSQL("DROP TABLE fs;"_ns)));
      QM_TRY(MOZ_TO_RESULT(transaction.Commit()));
    }
  }

  return NS_OK;
}

// static
Result<FileUsageType, nsresult> DatabaseFileManager::GetUsage(
    nsIFile* aDirectory) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);

  FileUsageType usage;

  QM_TRY(TraverseFiles(
      *aDirectory,
      // KnownDirEntryOp
      [&usage](nsIFile& file, const bool isDirectory) -> Result<Ok, nsresult> {
        if (isDirectory) {
          return Ok{};
        }

        // Usually we only use QM_OR_ELSE_LOG_VERBOSE(_IF) with Remove and
        // NS_ERROR_FILE_NOT_FOUND check, but the file was found by a directory
        // traversal and ToInteger on the name succeeded, so it should be our
        // file and if the file disappears, the use of QM_OR_ELSE_WARN_IF is ok
        // here.
        QM_TRY_INSPECT(const auto& thisUsage,
                       QM_OR_ELSE_WARN_IF(
                           // Expression.
                           MOZ_TO_RESULT_INVOKE_MEMBER(file, GetFileSize)
                               .map([](const int64_t fileSize) {
                                 return FileUsageType(Some(uint64_t(fileSize)));
                               }),
                           // Predicate.
                           ([](const nsresult rv) {
                             return rv == NS_ERROR_FILE_NOT_FOUND;
                           }),
                           // Fallback. If the file does no longer exist, treat
                           // it as 0-sized.
                           ErrToDefaultOk<FileUsageType>));

        usage += thisUsage;

        return Ok{};
      },
      // UnknownDirEntryOp
      [](nsIFile&, const bool) -> Result<Ok, nsresult> { return Ok{}; }));

  return usage;
}

nsresult DatabaseFileManager::SyncDeleteFile(const int64_t aId) {
  MOZ_ASSERT(!ContainsFileInfo(aId));

  if (!this->AssertValid()) {
    return NS_ERROR_UNEXPECTED;
  }

  const auto directory = GetDirectory();
  QM_TRY(OkIf(directory), NS_ERROR_FAILURE);

  const auto journalDirectory = GetJournalDirectory();
  QM_TRY(OkIf(journalDirectory), NS_ERROR_FAILURE);

  const nsCOMPtr<nsIFile> file = GetFileForId(directory, aId);
  QM_TRY(OkIf(file), NS_ERROR_FAILURE);

  const nsCOMPtr<nsIFile> journalFile = GetFileForId(journalDirectory, aId);
  QM_TRY(OkIf(journalFile), NS_ERROR_FAILURE);

  return SyncDeleteFile(*file, *journalFile);
}

nsresult DatabaseFileManager::SyncDeleteFile(nsIFile& aFile,
                                             nsIFile& aJournalFile) const {
  QuotaManager* const quotaManager =
      EnforcingQuota() ? QuotaManager::Get() : nullptr;
  MOZ_ASSERT_IF(EnforcingQuota(), quotaManager);

  QM_TRY(MOZ_TO_RESULT(DeleteFile(aFile, quotaManager, Type(), OriginMetadata(),
                                  Idempotency::No)));

  QM_TRY(MOZ_TO_RESULT(aJournalFile.Remove(false)));

  return NS_OK;
}

nsresult DatabaseFileManager::Invalidate() {
  if (mCipherKeyManager) {
    mCipherKeyManager->Invalidate();
  }

  QM_TRY(MOZ_TO_RESULT(FileInfoManager::Invalidate()));

  return NS_OK;
}

/*******************************************************************************
 * QuotaClient
 ******************************************************************************/

QuotaClient* QuotaClient::sInstance = nullptr;

QuotaClient::QuotaClient() : mDeleteTimer(NS_NewTimer()) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!sInstance, "We expect this to be a singleton!");
  MOZ_ASSERT(!gTelemetryIdMutex);

  // Always create this so that later access to gTelemetryIdHashtable can be
  // properly synchronized.
  gTelemetryIdMutex = new Mutex("IndexedDB gTelemetryIdMutex");

  gStorageDatabaseNameMutex = new Mutex("IndexedDB gStorageDatabaseNameMutex");

  sInstance = this;
}

QuotaClient::~QuotaClient() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(sInstance == this, "We expect this to be a singleton!");
  MOZ_ASSERT(gTelemetryIdMutex);
  MOZ_ASSERT(!mMaintenanceThreadPool);

  // No one else should be able to touch gTelemetryIdHashtable now that the
  // QuotaClient has gone away.
  gTelemetryIdHashtable = nullptr;
  gTelemetryIdMutex = nullptr;

  gStorageDatabaseNameHashtable = nullptr;
  gStorageDatabaseNameMutex = nullptr;

  sInstance = nullptr;
}

nsresult QuotaClient::AsyncDeleteFile(DatabaseFileManager* aFileManager,
                                      int64_t aFileId) {
  AssertIsOnBackgroundThread();

  if (IsShuttingDownOnBackgroundThread()) {
    // Whoops! We want to delete an IndexedDB disk-backed File but it's too late
    // to actually delete the file! This means we're going to "leak" the file
    // and leave it around when we shouldn't! (The file will stay around until
    // next storage initialization is triggered when the app is started again).
    // Fixing this is tracked by bug 1539377.

    return NS_OK;
  }

  MOZ_ASSERT(mDeleteTimer);
  MOZ_ALWAYS_SUCCEEDS(mDeleteTimer->Cancel());

  QM_TRY(MOZ_TO_RESULT(mDeleteTimer->InitWithNamedFuncCallback(
      DeleteTimerCallback, this, kDeleteTimeoutMs, nsITimer::TYPE_ONE_SHOT,
      "dom::indexeddb::QuotaClient::AsyncDeleteFile")));

  mPendingDeleteInfos.GetOrInsertNew(aFileManager)->AppendElement(aFileId);

  return NS_OK;
}

nsresult QuotaClient::FlushPendingFileDeletions() {
  AssertIsOnBackgroundThread();

  QM_TRY(MOZ_TO_RESULT(mDeleteTimer->Cancel()));

  DeleteTimerCallback(mDeleteTimer, this);

  return NS_OK;
}

RefPtr<BoolPromise> QuotaClient::DoMaintenance() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!IsShuttingDownOnBackgroundThread());

  if (!mBackgroundThread) {
    mBackgroundThread = GetCurrentSerialEventTarget();
  }

  auto maintenance = MakeRefPtr<Maintenance>(this);

  mMaintenanceQueue.AppendElement(maintenance);
  ProcessMaintenanceQueue();

  return maintenance->OnResults();
}

nsThreadPool* QuotaClient::GetOrCreateThreadPool() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!IsShuttingDownOnBackgroundThread());

  if (!mMaintenanceThreadPool) {
    RefPtr<nsThreadPool> threadPool = new nsThreadPool();

    // PR_GetNumberOfProcessors() can return -1 on error, so make sure we
    // don't set some huge number here. We add 2 in case some threads block on
    // the disk I/O.
    const uint32_t threadCount =
        std::max(int32_t(PR_GetNumberOfProcessors()), int32_t(1)) + 2;

    MOZ_ALWAYS_SUCCEEDS(threadPool->SetThreadLimit(threadCount));

    // Don't keep more than one idle thread.
    MOZ_ALWAYS_SUCCEEDS(threadPool->SetIdleThreadLimit(1));

    // Don't keep idle threads alive very long.
    MOZ_ALWAYS_SUCCEEDS(
        threadPool->SetIdleThreadMaximumTimeout(5 * PR_MSEC_PER_SEC));

    MOZ_ALWAYS_SUCCEEDS(threadPool->SetName("IndexedDB Mnt"_ns));

    mMaintenanceThreadPool = std::move(threadPool);
  }

  return mMaintenanceThreadPool;
}

mozilla::dom::quota::Client::Type QuotaClient::GetType() {
  return QuotaClient::IDB;
}

nsresult QuotaClient::UpgradeStorageFrom1_0To2_0(nsIFile* aDirectory) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);

  QM_TRY_INSPECT(const auto& databaseFilenamesInfo,
                 GetDatabaseFilenames(*aDirectory,
                                      /* aCanceled */ AtomicBool{false}));
  // FIXME: use structural binding once we support c++20.
  const auto& subdirsToProcess = databaseFilenamesInfo.subdirsToProcess;
  const auto& databaseFilenames = databaseFilenamesInfo.databaseFilenames;

  QM_TRY(CollectEachInRange(
      subdirsToProcess,
      [&databaseFilenames = databaseFilenames,
       aDirectory](const nsAString& subdirName) -> Result<Ok, nsresult> {
        // If the directory has the correct suffix then it should exist in
        // databaseFilenames.
        nsDependentSubstring subdirNameBase;
        if (GetFilenameBase(subdirName, kFileManagerDirectoryNameSuffix,
                            subdirNameBase)) {
          QM_WARNONLY_TRY(OkIf(databaseFilenames.Contains(subdirNameBase)));
          return Ok{};
        }

        // The directory didn't have the right suffix but we might need to
        // rename it. Check to see if we have a database that references this
        // directory.
        QM_TRY_INSPECT(
            const auto& subdirNameWithSuffix,
            ([&databaseFilenames,
              &subdirName]() -> Result<nsAutoString, NotOk> {
              if (databaseFilenames.Contains(subdirName)) {
                return nsAutoString{subdirName +
                                    kFileManagerDirectoryNameSuffix};
              }

              // Windows doesn't allow a directory to end with a dot ('.'), so
              // we have to check that possibility here too. We do this on all
              // platforms, because the origin directory may have been created
              // on Windows and now accessed on different OS.
              const nsAutoString subdirNameWithDot = subdirName + u"."_ns;
              QM_TRY(OkIf(databaseFilenames.Contains(subdirNameWithDot)),
                     Err(NotOk{}));

              return nsAutoString{subdirNameWithDot +
                                  kFileManagerDirectoryNameSuffix};
            }()),
            Ok{});

        // We do have a database that uses this subdir so we should rename it
        // now.
        QM_TRY_INSPECT(const auto& subdir,
                       CloneFileAndAppend(*aDirectory, subdirName));

        DebugOnly<bool> isDirectory;
        MOZ_ASSERT(NS_SUCCEEDED(subdir->IsDirectory(&isDirectory)));
        MOZ_ASSERT(isDirectory);

        // Check if the subdir with suffix already exists before renaming.
        QM_TRY_INSPECT(const auto& subdirWithSuffix,
                       CloneFileAndAppend(*aDirectory, subdirNameWithSuffix));

        QM_TRY_INSPECT(const bool& exists,
                       MOZ_TO_RESULT_INVOKE_MEMBER(subdirWithSuffix, Exists));

        if (exists) {
          IDB_WARNING("Deleting old %s files directory!",
                      NS_ConvertUTF16toUTF8(subdirName).get());

          QM_TRY(MOZ_TO_RESULT(subdir->Remove(/* aRecursive */ true)));

          return Ok{};
        }

        // Finally, rename the subdir.
        QM_TRY(MOZ_TO_RESULT(subdir->RenameTo(nullptr, subdirNameWithSuffix)));

        return Ok{};
      }));

  return NS_OK;
}

nsresult QuotaClient::UpgradeStorageFrom2_1To2_2(nsIFile* aDirectory) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aDirectory);

  QM_TRY(CollectEachFile(
      *aDirectory, [](const nsCOMPtr<nsIFile>& file) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*file));

        switch (dirEntryKind) {
          case nsIFileKind::ExistsAsDirectory:
            break;

          case nsIFileKind::ExistsAsFile: {
            QM_TRY_INSPECT(
                const auto& leafName,
                MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, file, GetLeafName));

            // It's reported that files ending with ".tmp" somehow live in the
            // indexedDB directories in Bug 1503883. Such files shouldn't exist
            // in the indexedDB directory so remove them in this upgrade.
            if (StringEndsWith(leafName, u".tmp"_ns)) {
              IDB_WARNING("Deleting unknown temporary file!");

              QM_TRY(MOZ_TO_RESULT(file->Remove(false)));
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

Result<UsageInfo, nsresult> QuotaClient::InitOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(this, GetUsageForOriginInternal,
                                            aPersistenceType, aOriginMetadata,
                                            aCanceled,
                                            /* aInitializing*/ true));
}

nsresult QuotaClient::InitOriginWithoutTracking(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  return GetUsageForOriginInternal(aPersistenceType, aOriginMetadata, aCanceled,
                                   /* aInitializing*/ true, nullptr);
}

Result<UsageInfo, nsresult> QuotaClient::GetUsageForOrigin(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(this, GetUsageForOriginInternal,
                                            aPersistenceType, aOriginMetadata,
                                            aCanceled,
                                            /* aInitializing*/ false));
}

nsresult QuotaClient::GetUsageForOriginInternal(
    PersistenceType aPersistenceType, const OriginMetadata& aOriginMetadata,
    const AtomicBool& aCanceled, const bool aInitializing,
    UsageInfo* aUsageInfo) {
  AssertIsOnIOThread();
  MOZ_ASSERT(aOriginMetadata.mPersistenceType == aPersistenceType);

  QM_TRY_INSPECT(const nsCOMPtr<nsIFile>& directory,
                 GetDirectory(aOriginMetadata));

  // We need to see if there are any files in the directory already. If they
  // are database files then we need to cleanup stored files (if it's needed)
  // and also get the usage.

  // XXX Can we avoid unwrapping into non-const variables here? (Only
  // databaseFilenames is currently modified below)
  QM_TRY_UNWRAP((auto [subdirsToProcess, databaseFilenames, obsoleteFilenames]),
                GetDatabaseFilenames<ObsoleteFilenamesHandling::Include>(
                    *directory, aCanceled));

  if (aInitializing) {
    QM_TRY(CollectEachInRange(
        subdirsToProcess,
        [&directory, &obsoleteFilenames = obsoleteFilenames,
         &databaseFilenames = databaseFilenames, aPersistenceType,
         &aOriginMetadata](
            const nsAString& subdirName) -> Result<Ok, nsresult> {
          // The directory must have the correct suffix.
          nsDependentSubstring subdirNameBase;
          QM_TRY(QM_OR_ELSE_WARN(
                     // Expression.
                     ([&subdirName, &subdirNameBase] {
                       QM_TRY_RETURN(OkIf(GetFilenameBase(
                           subdirName, kFileManagerDirectoryNameSuffix,
                           subdirNameBase)));
                     }()),
                     // Fallback.
                     ([&directory,
                       &subdirName](const NotOk) -> Result<Ok, nsresult> {
                       // If there is an unexpected directory in the idb
                       // directory, trying to delete at first instead of
                       // breaking the whole initialization.
                       QM_TRY(MOZ_TO_RESULT(
                                  DeleteFilesNoQuota(directory, subdirName)),
                              Err(NS_ERROR_UNEXPECTED));

                       return Ok{};
                     })),
                 Ok{});

          if (obsoleteFilenames.Contains(subdirNameBase)) {
            // If this fails, it probably means we are in a serious situation.
            // e.g. Filesystem corruption. Will handle this in bug 1521541.
            QM_TRY(MOZ_TO_RESULT(RemoveDatabaseFilesAndDirectory(
                       *directory, subdirNameBase, /* aQuotaManager */ nullptr,
                       aPersistenceType, aOriginMetadata,
                       /* aDatabaseName */ u""_ns)),
                   Err(NS_ERROR_UNEXPECTED));

            databaseFilenames.Remove(subdirNameBase);
            return Ok{};
          }

          // The directory base must exist in databaseFilenames.
          // If there is an unexpected directory in the idb directory, trying to
          // delete at first instead of breaking the whole initialization.

          // XXX This is still somewhat quirky. It would be nice to make it
          // clear that the warning handler is infallible, which would also
          // remove the need for the error type conversion.
          QM_WARNONLY_TRY(QM_OR_ELSE_WARN(
              // Expression.
              OkIf(databaseFilenames.Contains(subdirNameBase))
                  .mapErr([](const NotOk) { return NS_ERROR_FAILURE; }),
              // Fallback.
              ([&directory,
                &subdirName](const nsresult) -> Result<Ok, nsresult> {
                // XXX It seems if we really got here, we can fail the
                // MOZ_ASSERT(!quotaManager->IsTemporaryStorageInitializedInternal());
                // assertion in DeleteFilesNoQuota.
                QM_TRY(MOZ_TO_RESULT(DeleteFilesNoQuota(directory, subdirName)),
                       Err(NS_ERROR_UNEXPECTED));

                return Ok{};
              })));

          return Ok{};
        }));
  }

  for (const auto& databaseFilename : databaseFilenames) {
    if (aCanceled) {
      break;
    }

    QM_TRY_INSPECT(
        const auto& fmDirectory,
        CloneFileAndAppend(*directory,
                           databaseFilename + kFileManagerDirectoryNameSuffix));

    QM_TRY_INSPECT(
        const auto& databaseFile,
        CloneFileAndAppend(*directory, databaseFilename + kSQLiteSuffix));

    if (aInitializing) {
      QM_TRY(MOZ_TO_RESULT(DatabaseFileManager::InitDirectory(
          *fmDirectory, *databaseFile, aOriginMetadata.mOrigin,
          TelemetryIdForFile(databaseFile))));
    }

    if (aUsageInfo) {
      {
        QM_TRY_INSPECT(const int64_t& fileSize,
                       MOZ_TO_RESULT_INVOKE_MEMBER(databaseFile, GetFileSize));

        MOZ_ASSERT(fileSize >= 0);

        *aUsageInfo += DatabaseUsageType(Some(uint64_t(fileSize)));
      }

      {
        QM_TRY_INSPECT(const auto& walFile,
                       CloneFileAndAppend(*directory,
                                          databaseFilename + kSQLiteWALSuffix));

        // QM_OR_ELSE_WARN_IF is not used here since we just want to log
        // NS_ERROR_FILE_NOT_FOUND result and not spam the reports (the -wal
        // file doesn't have to exist).
        QM_TRY_INSPECT(const int64_t& walFileSize,
                       QM_OR_ELSE_LOG_VERBOSE_IF(
                           // Expression.
                           MOZ_TO_RESULT_INVOKE_MEMBER(walFile, GetFileSize),
                           // Predicate.
                           ([](const nsresult rv) {
                             return rv == NS_ERROR_FILE_NOT_FOUND;
                           }),
                           // Fallback.
                           (ErrToOk<0, int64_t>)));
        MOZ_ASSERT(walFileSize >= 0);
        *aUsageInfo += DatabaseUsageType(Some(uint64_t(walFileSize)));
      }

      {
        QM_TRY_INSPECT(const auto& fileUsage,
                       DatabaseFileManager::GetUsage(fmDirectory));

        *aUsageInfo += fileUsage;
      }
    }
  }

  return NS_OK;
}

void QuotaClient::OnOriginClearCompleted(
    const OriginMetadata& aOriginMetadata) {
  AssertIsOnIOThread();

  if (IndexedDatabaseManager* mgr = IndexedDatabaseManager::Get()) {
    mgr->InvalidateFileManagers(aOriginMetadata.mPersistenceType,
                                aOriginMetadata.mOrigin);
  }
}

void QuotaClient::OnRepositoryClearCompleted(PersistenceType aPersistenceType) {
  AssertIsOnIOThread();

  if (IndexedDatabaseManager* mgr = IndexedDatabaseManager::Get()) {
    mgr->InvalidateFileManagers(aPersistenceType);
  }
}

void QuotaClient::ReleaseIOThreadObjects() {
  AssertIsOnIOThread();

  if (IndexedDatabaseManager* mgr = IndexedDatabaseManager::Get()) {
    mgr->InvalidateAllFileManagers();
  }
}

void QuotaClient::AbortOperationsForLocks(
    const DirectoryLockIdTable& aDirectoryLockIds) {
  AssertIsOnBackgroundThread();

  InvalidateLiveDatabasesMatching([&aDirectoryLockIds](const auto& database) {
    // If the database is registered in gLiveDatabaseHashtable then it must have
    // a directory lock.
    return IsLockForObjectContainedInLockTable(database, aDirectoryLockIds);
  });
}

void QuotaClient::AbortOperationsForProcess(ContentParentId aContentParentId) {
  AssertIsOnBackgroundThread();

  InvalidateLiveDatabasesMatching([&aContentParentId](const auto& database) {
    return database.IsOwnedByProcess(aContentParentId);
  });
}

void QuotaClient::AbortAllOperations() {
  AssertIsOnBackgroundThread();

  AbortAllMaintenances();

  InvalidateLiveDatabasesMatching([](const auto&) { return true; });
}

void QuotaClient::StartIdleMaintenance() {
  AssertIsOnBackgroundThread();
  if (IsShuttingDownOnBackgroundThread()) {
    MOZ_ASSERT(false, "!IsShuttingDownOnBackgroundThread()");
    return;
  }

  DoMaintenance();
}

void QuotaClient::StopIdleMaintenance() {
  AssertIsOnBackgroundThread();

  AbortAllMaintenances();
}

void QuotaClient::InitiateShutdown() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(IsShuttingDownOnBackgroundThread());

  if (mDeleteTimer) {
    // QuotaClient::AsyncDeleteFile will not schedule new timers beyond
    // shutdown. And we expect all critical (PBM) deletions to have been
    // triggered before this point via ClearPrivateRepository (w/out using
    // DeleteFilesRunnable at all).
    mDeleteTimer->Cancel();
    mDeleteTimer = nullptr;
    mPendingDeleteInfos.Clear();
  }

  AbortAllOperations();
}

bool QuotaClient::IsShutdownCompleted() const {
  return (!gFactoryOps || gFactoryOps->IsEmpty()) &&
         (!gLiveDatabaseHashtable || !gLiveDatabaseHashtable->Count()) &&
         !mCurrentMaintenance && !DeleteFilesRunnable::IsDeletionPending();
}

void QuotaClient::ForceKillActors() {
  // Currently we don't implement force killing actors.
}

nsCString QuotaClient::GetShutdownStatus() const {
  AssertIsOnBackgroundThread();

  nsCString data;

  if (gFactoryOps && !gFactoryOps->IsEmpty()) {
    data.Append("FactoryOperations: "_ns +
                IntToCString(static_cast<uint32_t>(gFactoryOps->Length())) +
                " ("_ns);

    // XXX It might be confusing to remove duplicates here, as the actual list
    // won't match the count then.
    nsTHashSet<nsCString> ids;
    std::transform(gFactoryOps->cbegin(), gFactoryOps->cend(),
                   MakeInserter(ids), [](const auto& factoryOp) {
                     MOZ_ASSERT(factoryOp);

                     nsCString id;
                     factoryOp->Stringify(id);
                     return id;
                   });

    StringJoinAppend(data, ", "_ns, ids);

    data.Append(")\n");
  }

  if (gLiveDatabaseHashtable && gLiveDatabaseHashtable->Count()) {
    data.Append("LiveDatabases: "_ns +
                IntToCString(gLiveDatabaseHashtable->Count()) + " ("_ns);

    // XXX What's the purpose of adding these to a hashtable before joining them
    // to the string? (Maybe this used to be an ordered container before???)
    nsTHashSet<nsCString> ids;

    for (const auto& entry : gLiveDatabaseHashtable->Values()) {
      MOZ_ASSERT(entry);

      std::transform(entry->mLiveDatabases.cbegin(),
                     entry->mLiveDatabases.cend(), MakeInserter(ids),
                     [](const auto& database) {
                       nsCString id;
                       database->Stringify(id);
                       return id;
                     });
    }

    StringJoinAppend(data, ", "_ns, ids);

    data.Append(")\n");
  }

  if (mCurrentMaintenance) {
    data.Append("IdleMaintenance: 1 (");
    mCurrentMaintenance->Stringify(data);
    data.Append(")\n");
  }

  return data;
}

void QuotaClient::FinalizeShutdown() {
  RefPtr<ConnectionPool> connectionPool = gConnectionPool.get();
  if (connectionPool) {
    connectionPool->Shutdown();

    gConnectionPool = nullptr;
  }

  if (mMaintenanceThreadPool) {
    mMaintenanceThreadPool->Shutdown();
    mMaintenanceThreadPool = nullptr;
  }
}

void QuotaClient::DeleteTimerCallback(nsITimer* aTimer, void* aClosure) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aTimer);

  // Even though we do not schedule new timers after shutdown has started,
  // an already existing one might fire afterwards (actually we think it
  // shouldn't, but there is no reason to enforce this invariant). We can
  // just ignore it, the cleanup work is done in InitiateShutdown.
  if (NS_WARN_IF(IsShuttingDownOnBackgroundThread())) {
    return;
  }

  auto* const self = static_cast<QuotaClient*>(aClosure);
  MOZ_ASSERT(self);
  MOZ_ASSERT(self->mDeleteTimer);
  MOZ_ASSERT(SameCOMIdentity(self->mDeleteTimer, aTimer));

  for (const auto& pendingDeleteInfoEntry : self->mPendingDeleteInfos) {
    const auto& key = pendingDeleteInfoEntry.GetKey();
    const auto& value = pendingDeleteInfoEntry.GetData();
    MOZ_ASSERT(!value->IsEmpty());

    RefPtr<DeleteFilesRunnable> runnable = new DeleteFilesRunnable(
        SafeRefPtr{key, AcquireStrongRefFromRawPtr{}}, std::move(*value));

    MOZ_ASSERT(value->IsEmpty());

    runnable->RunImmediately();
  }

  self->mPendingDeleteInfos.Clear();
}

void QuotaClient::AbortAllMaintenances() {
  if (mCurrentMaintenance) {
    mCurrentMaintenance->Abort();
  }

  for (const auto& maintenance : mMaintenanceQueue) {
    maintenance->Abort();
  }
}

Result<nsCOMPtr<nsIFile>, nsresult> QuotaClient::GetDirectory(
    const OriginMetadata& aOriginMetadata) {
  QuotaManager* const quotaManager = QuotaManager::Get();
  NS_ASSERTION(quotaManager, "This should never fail!");

  QM_TRY_INSPECT(const auto& directory,
                 quotaManager->GetOriginDirectory(aOriginMetadata));

  MOZ_ASSERT(directory);

  QM_TRY(MOZ_TO_RESULT(
      directory->Append(NS_LITERAL_STRING_FROM_CSTRING(IDB_DIRECTORY_NAME))));

  return directory;
}

template <QuotaClient::ObsoleteFilenamesHandling ObsoleteFilenames>
Result<QuotaClient::GetDatabaseFilenamesResult<ObsoleteFilenames>, nsresult>
QuotaClient::GetDatabaseFilenames(nsIFile& aDirectory,
                                  const AtomicBool& aCanceled) {
  AssertIsOnIOThread();

  GetDatabaseFilenamesResult<ObsoleteFilenames> result;

  QM_TRY(CollectEachFileAtomicCancelable(
      aDirectory, aCanceled,
      [&result](const nsCOMPtr<nsIFile>& file) -> Result<Ok, nsresult> {
        QM_TRY_INSPECT(const auto& leafName, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                                 nsString, file, GetLeafName));

        QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*file));

        switch (dirEntryKind) {
          case nsIFileKind::ExistsAsDirectory:
            result.subdirsToProcess.AppendElement(leafName);
            break;

          case nsIFileKind::ExistsAsFile: {
            if constexpr (ObsoleteFilenames ==
                          ObsoleteFilenamesHandling::Include) {
              if (StringBeginsWith(leafName, kIdbDeletionMarkerFilePrefix)) {
                result.obsoleteFilenames.Insert(
                    Substring(leafName, kIdbDeletionMarkerFilePrefix.Length()));
                break;
              }
            }

            // Skip OS metadata files. These files are only used in different
            // platforms, but the profile can be shared across different
            // operating systems, so we check it on all platforms.
            if (QuotaManager::IsOSMetadata(leafName)) {
              break;
            }

            // Skip files starting with ".".
            if (QuotaManager::IsDotFile(leafName)) {
              break;
            }

            // Skip SQLite temporary files. These files take up space on disk
            // but will be deleted as soon as the database is opened, so we
            // don't count them towards quota.
            if (StringEndsWith(leafName, kSQLiteJournalSuffix) ||
                StringEndsWith(leafName, kSQLiteSHMSuffix)) {
              break;
            }

            // The SQLite WAL file does count towards quota, but it is handled
            // below once we find the actual database file.
            if (StringEndsWith(leafName, kSQLiteWALSuffix)) {
              break;
            }

            nsDependentSubstring leafNameBase;
            if (!GetFilenameBase(leafName, kSQLiteSuffix, leafNameBase)) {
              UNKNOWN_FILE_WARNING(leafName);
              break;
            }

            result.databaseFilenames.Insert(leafNameBase);
            break;
          }

          case nsIFileKind::DoesNotExist:
            // Ignore files that got removed externally while iterating.
            break;
        }

        return Ok{};
      }));

  return result;
}

void QuotaClient::ProcessMaintenanceQueue() {
  AssertIsOnBackgroundThread();

  if (mCurrentMaintenance || mMaintenanceQueue.IsEmpty()) {
    return;
  }

  mCurrentMaintenance = mMaintenanceQueue[0];
  mMaintenanceQueue.RemoveElementAt(0);

  mCurrentMaintenance->RunImmediately();
}

/*******************************************************************************
 * DeleteFilesRunnable
 ******************************************************************************/

uint64_t DeleteFilesRunnable::sPendingRunnables = 0;

DeleteFilesRunnable::DeleteFilesRunnable(
    SafeRefPtr<DatabaseFileManager> aFileManager, nsTArray<int64_t>&& aFileIds)
    : Runnable("dom::indexeddb::DeleteFilesRunnable"),
      mOwningEventTarget(GetCurrentSerialEventTarget()),
      mFileManager(std::move(aFileManager)),
      mFileIds(std::move(aFileIds)),
      mState(State_Initial) {}

#ifdef DEBUG
DeleteFilesRunnable::~DeleteFilesRunnable() {
  MOZ_ASSERT(!mDEBUGCountsAsPending);
}
#endif

void DeleteFilesRunnable::RunImmediately() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State_Initial);

  Unused << this->Run();
}

void DeleteFilesRunnable::Open() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State_Initial);

  MOZ_ASSERT(!mDEBUGCountsAsPending);
  sPendingRunnables++;
  DEBUGONLY(mDEBUGCountsAsPending = true);

  QuotaManager* const quotaManager = QuotaManager::Get();
  if (NS_WARN_IF(!quotaManager)) {
    Finish();
    return;
  }

  mState = State_DirectoryOpenPending;

  quotaManager
      ->OpenClientDirectory(
          {mFileManager->OriginMetadata(), quota::Client::IDB})
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this)](QuotaManager::ClientDirectoryLockHandlePromise::
                                    ResolveOrRejectValue&& aValue) {
            if (aValue.IsResolve()) {
              self->DirectoryLockAcquired(std::move(aValue.ResolveValue()));
            } else {
              self->DirectoryLockFailed();
            }
          });
}

void DeleteFilesRunnable::DoDatabaseWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State_DatabaseWorkOpen);

  if (!mFileManager->Invalidated()) {
    for (int64_t fileId : mFileIds) {
      if (NS_FAILED(mFileManager->SyncDeleteFile(fileId))) {
        NS_WARNING("Failed to delete file!");
      }
    }
  }

  Finish();
}

void DeleteFilesRunnable::Finish() {
  MOZ_ASSERT(mState != State_UnblockingOpen);

  // Must set mState before dispatching otherwise we will race with the main
  // thread.
  mState = State_UnblockingOpen;

  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL));
}

void DeleteFilesRunnable::UnblockOpen() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State_UnblockingOpen);

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  MOZ_ASSERT(mDEBUGCountsAsPending);
  sPendingRunnables--;
  DEBUGONLY(mDEBUGCountsAsPending = false);

  mState = State_Completed;
}

NS_IMETHODIMP
DeleteFilesRunnable::Run() {
  switch (mState) {
    case State_Initial:
      Open();
      break;

    case State_DatabaseWorkOpen:
      DoDatabaseWork();
      break;

    case State_UnblockingOpen:
      UnblockOpen();
      break;

    case State_DirectoryOpenPending:
    default:
      MOZ_CRASH("Should never get here!");
  }

  return NS_OK;
}

void DeleteFilesRunnable::DirectoryLockAcquired(
    ClientDirectoryLockHandle aLockHandle) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State_DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  mDirectoryLockHandle = std::move(aLockHandle);

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  // Must set this before dispatching otherwise we will race with the IO thread
  mState = State_DatabaseWorkOpen;

  QM_TRY(MOZ_TO_RESULT(
             quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL)),
         QM_VOID, [this](const nsresult) { Finish(); });
}

void DeleteFilesRunnable::DirectoryLockFailed() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State_DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  Finish();
}

void Maintenance::Abort() {
  AssertIsOnBackgroundThread();

  // Safe because mDatabaseMaintenances is modified
  // only in the background thread
  for (const auto& aDatabaseMaintenance : mDatabaseMaintenances) {
    aDatabaseMaintenance.GetData()->Abort();
  }

  mAborted = true;
}

void Maintenance::RegisterDatabaseMaintenance(
    DatabaseMaintenance* aDatabaseMaintenance) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabaseMaintenance);
  MOZ_ASSERT(mState == State::BeginDatabaseMaintenance);
  MOZ_ASSERT(
      !mDatabaseMaintenances.Contains(aDatabaseMaintenance->DatabasePath()));

  mDatabaseMaintenances.InsertOrUpdate(aDatabaseMaintenance->DatabasePath(),
                                       aDatabaseMaintenance);
}

void Maintenance::UnregisterDatabaseMaintenance(
    DatabaseMaintenance* aDatabaseMaintenance) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aDatabaseMaintenance);
  MOZ_ASSERT(mState == State::WaitingForDatabaseMaintenancesToComplete);
  MOZ_ASSERT(mDatabaseMaintenances.Get(aDatabaseMaintenance->DatabasePath()));

  mDatabaseMaintenances.Remove(aDatabaseMaintenance->DatabasePath());

  if (mDatabaseMaintenances.Count()) {
    return;
  }

  for (const auto& completeCallback : mCompleteCallbacks) {
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(completeCallback));
  }
  mCompleteCallbacks.Clear();

  mState = State::Finishing;
  Finish();
}

void Maintenance::Stringify(nsACString& aResult) const {
  AssertIsOnBackgroundThread();

  aResult.Append("DatabaseMaintenances: "_ns +
                 IntToCString(mDatabaseMaintenances.Count()) + " ("_ns);

  // XXX It might be confusing to remove duplicates here, as the actual list
  // won't match the count then.
  nsTHashSet<nsCString> ids;
  std::transform(mDatabaseMaintenances.Values().cbegin(),
                 mDatabaseMaintenances.Values().cend(), MakeInserter(ids),
                 [](const auto& entry) {
                   MOZ_ASSERT(entry);

                   nsCString id;
                   entry->Stringify(id);

                   return id;
                 });

  StringJoinAppend(aResult, ", "_ns, ids);

  aResult.Append(")");
}

nsresult Maintenance::Start() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::Initial);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsAborted()) {
    return NS_ERROR_ABORT;
  }

  // Make sure that the IndexedDatabaseManager is running so that we can check
  // for low disk space mode.

  if (IndexedDatabaseManager::Get()) {
    OpenDirectory();
    return NS_OK;
  }

  mState = State::CreateIndexedDatabaseManager;
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(this));

  return NS_OK;
}

nsresult Maintenance::CreateIndexedDatabaseManager() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mState == State::CreateIndexedDatabaseManager);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      IsAborted()) {
    return NS_ERROR_ABORT;
  }

  IndexedDatabaseManager* const mgr = IndexedDatabaseManager::GetOrCreate();
  if (NS_WARN_IF(!mgr)) {
    return NS_ERROR_FAILURE;
  }

  mState = State::IndexedDatabaseManagerOpen;
  MOZ_ALWAYS_SUCCEEDS(
      mQuotaClient->BackgroundThread()->Dispatch(this, NS_DISPATCH_NORMAL));

  return NS_OK;
}

RefPtr<UniversalDirectoryLockPromise> Maintenance::OpenStorageDirectory(
    const PersistenceScope& aPersistenceScope, bool aInitializeOrigins) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
  MOZ_ASSERT(!mDirectoryLock);
  MOZ_ASSERT(!mAborted);
  MOZ_ASSERT(mState == State::DirectoryOpenPending);

  QuotaManager* quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  // Return a shared lock for <profile>/storage/*/*/idb
  return quotaManager->OpenStorageDirectory(
      aPersistenceScope, OriginScope::FromNull(),
      ClientStorageScope::CreateFromClient(Client::IDB),
      /* aExclusive */ false, aInitializeOrigins, DirectoryLockCategory::None,
      SomeRef(mPendingDirectoryLock));
}

nsresult Maintenance::OpenDirectory() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::Initial ||
             mState == State::IndexedDatabaseManagerOpen);
  MOZ_ASSERT(!mDirectoryLock);
  MOZ_ASSERT(QuotaManager::Get());

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsAborted()) {
    return NS_ERROR_ABORT;
  }

  mState = State::DirectoryOpenPending;

  // Since idle maintenance may occur before persistent or temporary storage is
  // initialized, make sure it's initialized here (all persistent and
  // non-persistent origins need to be cleaned up and quota info needs to be
  // loaded for non-persistent origins).

  OpenStorageDirectory(PersistenceScope::CreateFromNull(),
                       /* aInitializeOrigins */ true)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this)](
              const UniversalDirectoryLockPromise::ResolveOrRejectValue&
                  aValue) {
            if (aValue.IsResolve()) {
              self->DirectoryLockAcquired(aValue.ResolveValue());
              return;
            }

            // Don't fail whole idle maintenance in case of an error, the
            // persistent repository can still be processed.

            self->mPendingDirectoryLock = nullptr;
            self->mOpenStorageForAllRepositoriesFailed = true;

            if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
                self->IsAborted()) {
              self->DirectoryLockFailed();
              return;
            }

            self->OpenStorageDirectory(PersistenceScope::CreateFromValue(
                                           PERSISTENCE_TYPE_PERSISTENT),
                                       /* aInitializeOrigins */ true)
                ->Then(GetCurrentSerialEventTarget(), __func__,
                       [self](const UniversalDirectoryLockPromise::
                                  ResolveOrRejectValue& aValue) {
                         if (aValue.IsResolve()) {
                           self->DirectoryLockAcquired(aValue.ResolveValue());
                         } else {
                           self->DirectoryLockFailed();
                         }
                       });
          });

  return NS_OK;
}

nsresult Maintenance::DirectoryOpen() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(mDirectoryLock);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsAborted()) {
    return NS_ERROR_ABORT;
  }

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  mState = State::DirectoryWorkOpen;

  QM_TRY(MOZ_TO_RESULT(
             quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL)),
         NS_ERROR_FAILURE);

  return NS_OK;
}

nsresult Maintenance::DirectoryWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DirectoryWorkOpen);

  // The storage directory is structured like this:
  //
  //   <profile>/storage/<persistence>/<origin>/idb/*.sqlite
  //
  // We have to find all database files that match any persistence type and any
  // origin. We ignore anything out of the ordinary for now.

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      IsAborted()) {
    return NS_ERROR_ABORT;
  }

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  const nsCOMPtr<nsIFile> storageDir =
      GetFileForPath(quotaManager->GetStoragePath());
  QM_TRY(OkIf(storageDir), NS_ERROR_FAILURE);

  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(storageDir, Exists));

    // XXX No warning here?
    if (!exists) {
      return NS_ERROR_NOT_AVAILABLE;
    }
  }

  {
    QM_TRY_INSPECT(const bool& isDirectory,
                   MOZ_TO_RESULT_INVOKE_MEMBER(storageDir, IsDirectory));

    QM_TRY(OkIf(isDirectory), NS_ERROR_FAILURE);
  }

  // There are currently only 4 persistence types, and we want to iterate them
  // in this order:
  static const PersistenceType kPersistenceTypes[] = {
      PERSISTENCE_TYPE_PERSISTENT, PERSISTENCE_TYPE_DEFAULT,
      PERSISTENCE_TYPE_TEMPORARY, PERSISTENCE_TYPE_PRIVATE};

  static_assert(
      std::size(kPersistenceTypes) == size_t(PERSISTENCE_TYPE_INVALID),
      "Something changed with available persistence types!");

  constexpr auto idbDirName =
      NS_LITERAL_STRING_FROM_CSTRING(IDB_DIRECTORY_NAME);

  for (const PersistenceType persistenceType : kPersistenceTypes) {
    // Loop over "<persistence>" directories.
    if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
        IsAborted()) {
      return NS_ERROR_ABORT;
    }

    // Don't do any maintenance for private browsing databases, which are only
    // temporary.
    if (persistenceType == PERSISTENCE_TYPE_PRIVATE) {
      continue;
    }

    const bool persistent = persistenceType == PERSISTENCE_TYPE_PERSISTENT;

    if (!persistent && mOpenStorageForAllRepositoriesFailed) {
      // Non-persistent (best effort) repositories can't be processed if
      // temporary storage initialization failed.
      continue;
    }

    // XXX persistenceType == PERSISTENCE_TYPE_PERSISTENT shouldn't be a special
    // case...
    const auto persistenceTypeString =
        persistenceType == PERSISTENCE_TYPE_PERSISTENT
            ? "permanent"_ns
            : PersistenceTypeToString(persistenceType);

    QM_TRY_INSPECT(const auto& persistenceDir,
                   CloneFileAndAppend(*storageDir, NS_ConvertASCIItoUTF16(
                                                       persistenceTypeString)));

    {
      QM_TRY_INSPECT(const bool& exists,
                     MOZ_TO_RESULT_INVOKE_MEMBER(persistenceDir, Exists));

      if (!exists) {
        continue;
      }

      QM_TRY_INSPECT(const bool& isDirectory,
                     MOZ_TO_RESULT_INVOKE_MEMBER(persistenceDir, IsDirectory));

      if (NS_WARN_IF(!isDirectory)) {
        continue;
      }
    }

    // Loop over "<origin>/idb" directories.
    QM_TRY(CollectEachFile(
        *persistenceDir,
        [this, &quotaManager, persistenceType, &idbDirName](
            const nsCOMPtr<nsIFile>& originDir) -> Result<Ok, nsresult> {
          if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
              IsAborted()) {
            return Err(NS_ERROR_ABORT);
          }

          QM_TRY_INSPECT(const auto& dirEntryKind, GetDirEntryKind(*originDir));

          switch (dirEntryKind) {
            case nsIFileKind::ExistsAsFile:
              break;

            case nsIFileKind::ExistsAsDirectory: {
              // Get the necessary information about the origin
              // (GetOriginMetadata also checks if it's a valid origin).

              QM_TRY_INSPECT(const auto& metadata,
                             quotaManager->GetOriginMetadata(originDir),
                             // Not much we can do here...
                             Ok{});

              // We now use a dedicated repository for private browsing
              // databases, but there could be some forgotten private browsing
              // databases in other repositories, so it's better to check for
              // that and don't do any maintenance for such databases.
              if (metadata.mIsPrivate) {
                return Ok{};
              }

              QM_TRY_INSPECT(const auto& idbDir,
                             CloneFileAndAppend(*originDir, idbDirName));

              QM_TRY_INSPECT(const bool& exists,
                             MOZ_TO_RESULT_INVOKE_MEMBER(idbDir, Exists));

              if (!exists) {
                return Ok{};
              }

              QM_TRY_INSPECT(const bool& isDirectory,
                             MOZ_TO_RESULT_INVOKE_MEMBER(idbDir, IsDirectory));

              QM_TRY(OkIf(isDirectory), Ok{});

              nsTArray<nsString> databasePaths;

              // Loop over files in the "idb" directory.
              QM_TRY(CollectEachFile(
                  *idbDir,
                  [this, &databasePaths](const nsCOMPtr<nsIFile>& idbDirFile)
                      -> Result<Ok, nsresult> {
                    if (NS_WARN_IF(QuotaClient::
                                       IsShuttingDownOnNonBackgroundThread()) ||
                        IsAborted()) {
                      return Err(NS_ERROR_ABORT);
                    }

                    QM_TRY_UNWRAP(auto idbFilePath,
                                  MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                      nsString, idbDirFile, GetPath));

                    if (!StringEndsWith(idbFilePath, kSQLiteSuffix)) {
                      return Ok{};
                    }

                    QM_TRY_INSPECT(const auto& dirEntryKind,
                                   GetDirEntryKind(*idbDirFile));

                    switch (dirEntryKind) {
                      case nsIFileKind::ExistsAsDirectory:
                        break;

                      case nsIFileKind::ExistsAsFile:
                        // Found a database.

                        MOZ_ASSERT(!databasePaths.Contains(idbFilePath));

                        databasePaths.AppendElement(std::move(idbFilePath));
                        break;

                      case nsIFileKind::DoesNotExist:
                        // Ignore files that got removed externally while
                        // iterating.
                        break;
                    }

                    return Ok{};
                  }));

              if (!databasePaths.IsEmpty()) {
                mDirectoryInfos.EmplaceBack(persistenceType, metadata,
                                            std::move(databasePaths));
              }

              break;
            }

            case nsIFileKind::DoesNotExist:
              // Ignore files that got removed externally while iterating.
              break;
          }

          return Ok{};
        }));
  }

  mState = State::BeginDatabaseMaintenance;

  MOZ_ALWAYS_SUCCEEDS(
      mQuotaClient->BackgroundThread()->Dispatch(this, NS_DISPATCH_NORMAL));

  return NS_OK;
}

nsresult Maintenance::BeginDatabaseMaintenance() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::BeginDatabaseMaintenance);

  class MOZ_STACK_CLASS Helper final {
   public:
    static bool IsSafeToRunMaintenance(const nsAString& aDatabasePath) {
      if (gFactoryOps) {
        for (uint32_t index = gFactoryOps->Length(); index > 0; index--) {
          CheckedUnsafePtr<FactoryOp>& existingOp = (*gFactoryOps)[index - 1];

          if (existingOp->DatabaseNameRef().isNothing()) {
            return false;
          }

          if (!existingOp->DatabaseFilePathIsKnown()) {
            continue;
          }

          if (existingOp->DatabaseFilePath() == aDatabasePath) {
            return false;
          }
        }
      }

      if (gLiveDatabaseHashtable) {
        return std::all_of(
            gLiveDatabaseHashtable->Values().cbegin(),
            gLiveDatabaseHashtable->Values().cend(),
            [&aDatabasePath](const auto& liveDatabasesEntry) {
              const auto& liveDatabases = liveDatabasesEntry->mLiveDatabases;
              return std::all_of(liveDatabases.cbegin(), liveDatabases.cend(),
                                 [&aDatabasePath](const auto& database) {
                                   return database->FilePath() != aDatabasePath;
                                 });
            });
      }

      return true;
    }
  };

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsAborted()) {
    return NS_ERROR_ABORT;
  }

  RefPtr<nsThreadPool> threadPool;

  for (DirectoryInfo& directoryInfo : mDirectoryInfos) {
    for (const nsAString& databasePath : *directoryInfo.mDatabasePaths) {
      if (Helper::IsSafeToRunMaintenance(databasePath)) {
        RefPtr<ClientDirectoryLock> directoryLock =
            mDirectoryLock->SpecializeForClient(directoryInfo.mPersistenceType,
                                                *directoryInfo.mOriginMetadata,
                                                Client::IDB);
        MOZ_ASSERT(directoryLock);

        // No key needs to be passed here, because we skip encrypted databases
        // in DoDirectoryWork as long as they are only used in private browsing
        // mode.
        const auto databaseMaintenance = MakeRefPtr<DatabaseMaintenance>(
            this, std::move(directoryLock), directoryInfo.mPersistenceType,
            *directoryInfo.mOriginMetadata, databasePath, Nothing{});

        if (!threadPool) {
          threadPool = mQuotaClient->GetOrCreateThreadPool();
          MOZ_ASSERT(threadPool);
        }

        // Perform database maintenance on a TaskQueue, as database connections
        // require a serial event target when being opened in order to allow
        // memory pressure notifications to clear caches (bug 1806751).
        const auto taskQueue = TaskQueue::Create(
            do_AddRef(threadPool), "IndexedDB Database Maintenance");

        MOZ_ALWAYS_SUCCEEDS(
            taskQueue->Dispatch(databaseMaintenance, NS_DISPATCH_NORMAL));

        RegisterDatabaseMaintenance(databaseMaintenance);
      }
    }
  }

  mDirectoryInfos.Clear();

  DropDirectoryLock(mDirectoryLock);

  if (mDatabaseMaintenances.Count()) {
    mState = State::WaitingForDatabaseMaintenancesToComplete;
  } else {
    mState = State::Finishing;
    Finish();
  }

  return NS_OK;
}

void Maintenance::Finish() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::Finishing);

  if (NS_SUCCEEDED(mResultCode)) {
    mPromiseHolder.ResolveIfExists(true, __func__);
  } else {
    mPromiseHolder.RejectIfExists(mResultCode, __func__);

    nsCString errorName;
    GetErrorName(mResultCode, errorName);

    IDB_WARNING("Maintenance finished with error: %s", errorName.get());
  }

  SafeDropDirectoryLock(mDirectoryLock);

  // It can happen that we are only referenced by mCurrentMaintenance which is
  // cleared in NoteFinishedMaintenance()
  const RefPtr<Maintenance> kungFuDeathGrip = this;

  mQuotaClient->NoteFinishedMaintenance(this);

  mState = State::Complete;
}

NS_IMETHODIMP
Maintenance::Run() {
  MOZ_ASSERT(mState != State::Complete);

  const auto handleError = [this](const nsresult rv) {
    if (mState != State::Finishing) {
      if (NS_SUCCEEDED(mResultCode)) {
        mResultCode = rv;
      }

      // Must set mState before dispatching otherwise we will race with the
      // owning thread.
      mState = State::Finishing;

      if (IsOnBackgroundThread()) {
        Finish();
      } else {
        MOZ_ALWAYS_SUCCEEDS(mQuotaClient->BackgroundThread()->Dispatch(
            this, NS_DISPATCH_NORMAL));
      }
    }
  };

  switch (mState) {
    case State::Initial:
      QM_TRY(MOZ_TO_RESULT(Start()), NS_OK, handleError);
      break;

    case State::CreateIndexedDatabaseManager:
      QM_TRY(MOZ_TO_RESULT(CreateIndexedDatabaseManager()), NS_OK, handleError);
      break;

    case State::IndexedDatabaseManagerOpen:
      QM_TRY(MOZ_TO_RESULT(OpenDirectory()), NS_OK, handleError);
      break;

    case State::DirectoryWorkOpen:
      QM_TRY(MOZ_TO_RESULT(DirectoryWork()), NS_OK, handleError);
      break;

    case State::BeginDatabaseMaintenance:
      QM_TRY(MOZ_TO_RESULT(BeginDatabaseMaintenance()), NS_OK, handleError);
      break;

    case State::Finishing:
      Finish();
      break;

    default:
      MOZ_CRASH("Bad state!");
  }

  return NS_OK;
}

void Maintenance::DirectoryLockAcquired(UniversalDirectoryLock* aLock) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLock);

  mDirectoryLock = std::exchange(mPendingDirectoryLock, nullptr);

  nsresult rv = DirectoryOpen();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    if (NS_SUCCEEDED(mResultCode)) {
      mResultCode = rv;
    }

    mState = State::Finishing;
    Finish();

    return;
  }
}

void Maintenance::DirectoryLockFailed() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLock);

  mPendingDirectoryLock = nullptr;

  if (NS_SUCCEEDED(mResultCode)) {
    mResultCode = NS_ERROR_FAILURE;
  }

  mState = State::Finishing;
  Finish();
}

void DatabaseMaintenance::Stringify(nsACString& aResult) const {
  AssertIsOnBackgroundThread();

  aResult.AppendLiteral("Origin:");
  aResult.Append(AnonymizedOriginString(mOriginMetadata.mOrigin));
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("PersistenceType:");
  aResult.Append(PersistenceTypeToString(mPersistenceType));
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Duration:");
  aResult.AppendInt((PR_Now() - mMaintenance->StartTime()) / PR_USEC_PER_MSEC);
}

nsresult DatabaseMaintenance::Abort() {
  AssertIsOnBackgroundThread();

  // StopIdleMaintenance and AbortAllOperations may request abort independently
  if (!mAborted.compareExchange(false, true)) {
    return NS_OK;
  }

  {
    auto shardStorageConnectionLocked = mSharedStorageConnection.Lock();
    if (nsCOMPtr<mozIStorageConnection> connection =
            *shardStorageConnectionLocked) {
      QM_TRY(MOZ_TO_RESULT(connection->Interrupt()));
    }
  }

  return NS_OK;
}

void DatabaseMaintenance::PerformMaintenanceOnDatabase() {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(mMaintenance);
  MOZ_ASSERT(mMaintenance->StartTime());
  MOZ_ASSERT(mDirectoryLock);
  MOZ_ASSERT(!mDatabasePath.IsEmpty());
  MOZ_ASSERT(!mOriginMetadata.mGroup.IsEmpty());
  MOZ_ASSERT(!mOriginMetadata.mOrigin.IsEmpty());

  if (NS_WARN_IF(IsAborted())) {
    return;
  }

  const nsCOMPtr<nsIFile> databaseFile = GetFileForPath(mDatabasePath);
  MOZ_ASSERT(databaseFile);

  QM_TRY_UNWRAP(
      const NotNull<nsCOMPtr<mozIStorageConnection>> connection,
      GetStorageConnection(*databaseFile, mDirectoryLockId,
                           TelemetryIdForFile(databaseFile), mMaybeKey),
      QM_VOID);

  auto autoClearConnection = MakeScopeExit([&]() {
    auto sharedStorageConnectionLocked = mSharedStorageConnection.Lock();
    sharedStorageConnectionLocked.ref() = nullptr;
    connection->Close();
  });

  {
    auto sharedStorageConnectionLocked = mSharedStorageConnection.Lock();
    sharedStorageConnectionLocked.ref() = connection;
  }

  auto databaseIsOk = false;
  QM_TRY(MOZ_TO_RESULT(CheckIntegrity(*connection, &databaseIsOk)), QM_VOID);

  QM_TRY(OkIf(databaseIsOk), QM_VOID, [](auto result) {
    // XXX Handle this somehow! Probably need to clear all storage for the
    //     origin. See Bug 1760612.
    MOZ_ASSERT(false, "Database corruption detected!");
  });

  MaintenanceAction maintenanceAction;
  QM_TRY(MOZ_TO_RESULT(DetermineMaintenanceAction(*connection, databaseFile,
                                                  &maintenanceAction)),
         QM_VOID);

  switch (maintenanceAction) {
    case MaintenanceAction::Nothing:
      break;

    case MaintenanceAction::IncrementalVacuum:
      IncrementalVacuum(*connection);
      break;

    case MaintenanceAction::FullVacuum:
      FullVacuum(*connection, databaseFile);
      break;

    default:
      MOZ_CRASH("Unknown MaintenanceAction!");
  }
}

nsresult DatabaseMaintenance::CheckIntegrity(mozIStorageConnection& aConnection,
                                             bool* aOk) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aOk);

  if (NS_WARN_IF(IsAborted())) {
    return NS_ERROR_ABORT;
  }

  // First do a full integrity_check. Scope statements tightly here because
  // later operations require zero live statements.
  {
    QM_TRY_INSPECT(const auto& stmt,
                   CreateAndExecuteSingleStepStatement(
                       aConnection, "PRAGMA integrity_check(1);"_ns));

    QM_TRY_INSPECT(const auto& result, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                           nsString, *stmt, GetString, 0));

    QM_TRY(OkIf(result.EqualsLiteral("ok")), NS_OK,
           [&aOk](const auto) { *aOk = false; });
  }

  // Now enable and check for foreign key constraints.
  {
    QM_TRY_INSPECT(
        const int32_t& foreignKeysWereEnabled,
        ([&aConnection]() -> Result<int32_t, nsresult> {
          QM_TRY_INSPECT(const auto& stmt,
                         CreateAndExecuteSingleStepStatement(
                             aConnection, "PRAGMA foreign_keys;"_ns));

          QM_TRY_RETURN(MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt32, 0));
        }()));

    if (!foreignKeysWereEnabled) {
      QM_TRY(MOZ_TO_RESULT(
          aConnection.ExecuteSimpleSQL("PRAGMA foreign_keys = ON;"_ns)));
    }

    QM_TRY_INSPECT(const bool& foreignKeyError,
                   CreateAndExecuteSingleStepStatement<
                       SingleStepResult::ReturnNullIfNoResult>(
                       aConnection, "PRAGMA foreign_key_check;"_ns));

    if (!foreignKeysWereEnabled) {
      QM_TRY(MOZ_TO_RESULT(
          aConnection.ExecuteSimpleSQL("PRAGMA foreign_keys = OFF;"_ns)));
    }

    if (foreignKeyError) {
      *aOk = false;
      return NS_OK;
    }
  }

  *aOk = true;
  return NS_OK;
}

nsresult DatabaseMaintenance::DetermineMaintenanceAction(
    mozIStorageConnection& aConnection, nsIFile* aDatabaseFile,
    MaintenanceAction* aMaintenanceAction) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aDatabaseFile);
  MOZ_ASSERT(aMaintenanceAction);

  if (NS_WARN_IF(IsAborted())) {
    return NS_ERROR_ABORT;
  }

  QM_TRY_INSPECT(const int32_t& schemaVersion,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aConnection, GetSchemaVersion));

  // Don't do anything if the schema version is less than 18; before that
  // version no databases had |auto_vacuum == INCREMENTAL| set and we didn't
  // track the values needed for the heuristics below.
  if (schemaVersion < MakeSchemaVersion(18, 0)) {
    *aMaintenanceAction = MaintenanceAction::Nothing;
    return NS_OK;
  }

  // This method shouldn't make any permanent changes to the database, so make
  // sure everything gets rolled back when we leave.
  mozStorageTransaction transaction(&aConnection,
                                    /* aCommitOnComplete */ false);

  QM_TRY(MOZ_TO_RESULT(transaction.Start()))

  // Check to see when we last vacuumed this database.
  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement(
                     aConnection,
                     "SELECT last_vacuum_time, last_vacuum_size "
                     "FROM database;"_ns));

  QM_TRY_INSPECT(const PRTime& lastVacuumTime,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt64, 0));

  QM_TRY_INSPECT(const int64_t& lastVacuumSize,
                 MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt64, 1));

  NS_ASSERTION(lastVacuumSize > 0,
               "Thy last vacuum size shall be greater than zero, less than "
               "zero shall thy last vacuum size not be. Zero is right out.");

  const PRTime startTime = mMaintenance->StartTime();

  // This shouldn't really be possible...
  if (NS_WARN_IF(startTime <= lastVacuumTime)) {
    *aMaintenanceAction = MaintenanceAction::Nothing;
    return NS_OK;
  }

  if (startTime - lastVacuumTime < kMinVacuumAge) {
    *aMaintenanceAction = MaintenanceAction::IncrementalVacuum;
    return NS_OK;
  }

  // It has been more than a week since the database was vacuumed, so gather
  // statistics on its usage to see if vacuuming is worthwhile.

  // Create a temporary copy of the dbstat table to speed up the queries that
  // come later.
  QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL(
      "CREATE VIRTUAL TABLE __stats__ USING dbstat;"
      "CREATE TEMP TABLE __temp_stats__ AS SELECT * FROM __stats__;"_ns)));

  {  // Calculate the percentage of the database pages that are not in
     // contiguous order.
    QM_TRY_INSPECT(
        const auto& stmt,
        CreateAndExecuteSingleStepStatement(
            aConnection,
            "SELECT SUM(__ts1__.pageno != __ts2__.pageno + 1) * 100.0 / "
            "COUNT(*) "
            "FROM __temp_stats__ AS __ts1__, __temp_stats__ AS __ts2__ "
            "WHERE __ts1__.name = __ts2__.name "
            "AND __ts1__.rowid = __ts2__.rowid + 1;"_ns));

    QM_TRY_INSPECT(const int32_t& percentUnordered,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt32, 0));

    MOZ_ASSERT(percentUnordered >= 0);
    MOZ_ASSERT(percentUnordered <= 100);

    if (percentUnordered >= kPercentUnorderedThreshold) {
      *aMaintenanceAction = MaintenanceAction::FullVacuum;
      return NS_OK;
    }
  }

  // Don't try a full vacuum if the file hasn't grown by 10%.
  QM_TRY_INSPECT(const int64_t& currentFileSize,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aDatabaseFile, GetFileSize));

  if (currentFileSize <= lastVacuumSize ||
      (((currentFileSize - lastVacuumSize) * 100 / currentFileSize) <
       kPercentFileSizeGrowthThreshold)) {
    *aMaintenanceAction = MaintenanceAction::IncrementalVacuum;
    return NS_OK;
  }

  {  // See if there are any free pages that we can reclaim.
    QM_TRY_INSPECT(const auto& stmt,
                   CreateAndExecuteSingleStepStatement(
                       aConnection, "PRAGMA freelist_count;"_ns));

    QM_TRY_INSPECT(const int32_t& freelistCount,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt32, 0));

    MOZ_ASSERT(freelistCount >= 0);

    // If we have too many free pages then we should try an incremental
    // vacuum. If that causes too much fragmentation then we'll try a full
    // vacuum later.
    if (freelistCount > kMaxFreelistThreshold) {
      *aMaintenanceAction = MaintenanceAction::IncrementalVacuum;
      return NS_OK;
    }
  }

  {  // Calculate the percentage of unused bytes on pages in the database.
    QM_TRY_INSPECT(
        const auto& stmt,
        CreateAndExecuteSingleStepStatement(
            aConnection,
            "SELECT SUM(unused) * 100.0 / SUM(pgsize) FROM __temp_stats__;"_ns));

    QM_TRY_INSPECT(const int32_t& percentUnused,
                   MOZ_TO_RESULT_INVOKE_MEMBER(*stmt, GetInt32, 0));

    MOZ_ASSERT(percentUnused >= 0);
    MOZ_ASSERT(percentUnused <= 100);

    *aMaintenanceAction = percentUnused >= kPercentUnusedThreshold
                              ? MaintenanceAction::FullVacuum
                              : MaintenanceAction::IncrementalVacuum;
  }

  return NS_OK;
}

void DatabaseMaintenance::IncrementalVacuum(
    mozIStorageConnection& aConnection) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());

  if (NS_WARN_IF(IsAborted())) {
    return;
  }

  nsresult rv = aConnection.ExecuteSimpleSQL("PRAGMA incremental_vacuum;"_ns);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }
}

void DatabaseMaintenance::FullVacuum(mozIStorageConnection& aConnection,
                                     nsIFile* aDatabaseFile) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aDatabaseFile);

  if (NS_WARN_IF(IsAborted())) {
    return;
  }

  QM_WARNONLY_TRY(([&]() -> Result<Ok, nsresult> {
    QM_TRY(MOZ_TO_RESULT(aConnection.ExecuteSimpleSQL("VACUUM;"_ns)));

    const PRTime vacuumTime = PR_Now();
    MOZ_ASSERT(vacuumTime > 0);

    QM_TRY_INSPECT(const int64_t& fileSize,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aDatabaseFile, GetFileSize));

    MOZ_ASSERT(fileSize > 0);

    // The parameter names are not used, parameters are bound by index only
    // locally in the same function.
    QM_TRY_INSPECT(const auto& stmt, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                         nsCOMPtr<mozIStorageStatement>,
                                         aConnection, CreateStatement,
                                         "UPDATE database "
                                         "SET last_vacuum_time = :time"
                                         ", last_vacuum_size = :size;"_ns));

    QM_TRY(MOZ_TO_RESULT(stmt->BindInt64ByIndex(0, vacuumTime)));

    QM_TRY(MOZ_TO_RESULT(stmt->BindInt64ByIndex(1, fileSize)));

    QM_TRY(MOZ_TO_RESULT(stmt->Execute()));
    return Ok{};
  }()));
}

void DatabaseMaintenance::RunOnOwningThread() {
  AssertIsOnBackgroundThread();

  DropDirectoryLock(mDirectoryLock);

  if (mCompleteCallback) {
    MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(mCompleteCallback.forget()));
  }

  mMaintenance->UnregisterDatabaseMaintenance(this);
}

void DatabaseMaintenance::RunOnConnectionThread() {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());

  PerformMaintenanceOnDatabase();

  MOZ_ALWAYS_SUCCEEDS(
      mMaintenance->BackgroundThread()->Dispatch(this, NS_DISPATCH_NORMAL));
}

NS_IMETHODIMP
DatabaseMaintenance::Run() {
  if (IsOnBackgroundThread()) {
    RunOnOwningThread();
  } else {
    RunOnConnectionThread();
  }

  return NS_OK;
}

/*******************************************************************************
 * Local class implementations
 ******************************************************************************/

// static
nsAutoCString DatabaseOperationBase::MaybeGetBindingClauseForKeyRange(
    const Maybe<SerializedKeyRange>& aOptionalKeyRange,
    const nsACString& aKeyColumnName) {
  return aOptionalKeyRange.isSome()
             ? GetBindingClauseForKeyRange(aOptionalKeyRange.ref(),
                                           aKeyColumnName)
             : nsAutoCString{};
}

// static
nsAutoCString DatabaseOperationBase::GetBindingClauseForKeyRange(
    const SerializedKeyRange& aKeyRange, const nsACString& aKeyColumnName) {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(!aKeyColumnName.IsEmpty());

  constexpr auto andStr = " AND "_ns;
  constexpr auto spacecolon = " :"_ns;

  nsAutoCString result;
  if (aKeyRange.isOnly()) {
    // Both keys equal.
    result =
        andStr + aKeyColumnName + " ="_ns + spacecolon + kStmtParamNameLowerKey;
  } else {
    if (!aKeyRange.lower().IsUnset()) {
      // Lower key is set.
      result.Append(andStr + aKeyColumnName);
      result.AppendLiteral(" >");
      if (!aKeyRange.lowerOpen()) {
        result.AppendLiteral("=");
      }
      result.Append(spacecolon + kStmtParamNameLowerKey);
    }

    if (!aKeyRange.upper().IsUnset()) {
      // Upper key is set.
      result.Append(andStr + aKeyColumnName);
      result.AppendLiteral(" <");
      if (!aKeyRange.upperOpen()) {
        result.AppendLiteral("=");
      }
      result.Append(spacecolon + kStmtParamNameUpperKey);
    }
  }

  MOZ_ASSERT(!result.IsEmpty());

  return result;
}

// static
uint64_t DatabaseOperationBase::ReinterpretDoubleAsUInt64(double aDouble) {
  // This is a duplicate of the js engine's byte munging in StructuredClone.cpp
  return BitwiseCast<uint64_t>(aDouble);
}

// static
template <typename KeyTransformation>
nsresult DatabaseOperationBase::MaybeBindKeyToStatement(
    const Key& aKey, mozIStorageStatement* const aStatement,
    const nsACString& aParameterName,
    const KeyTransformation& aKeyTransformation) {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aStatement);

  if (!aKey.IsUnset()) {
    // XXX This case distinction could be avoided if QM_TRY_INSPECT would also
    // work with a function not returning a Result<V, E> but simply a V (which
    // is const Key& here) and then assuming it is always a success. Or the
    // transformation could be changed to return Result<const V&, void> but I
    // don't think that Result supports that at the moment.
    if constexpr (std::is_reference_v<
                      std::invoke_result_t<KeyTransformation, Key>>) {
      QM_TRY(MOZ_TO_RESULT(aKeyTransformation(aKey).BindToStatement(
          aStatement, aParameterName)));
    } else {
      QM_TRY_INSPECT(const auto& transformedKey, aKeyTransformation(aKey));
      QM_TRY(MOZ_TO_RESULT(
          transformedKey.BindToStatement(aStatement, aParameterName)));
    }
  }

  return NS_OK;
}

// static
template <typename KeyTransformation>
nsresult DatabaseOperationBase::BindTransformedKeyRangeToStatement(
    const SerializedKeyRange& aKeyRange, mozIStorageStatement* const aStatement,
    const KeyTransformation& aKeyTransformation) {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aStatement);

  QM_TRY(MOZ_TO_RESULT(MaybeBindKeyToStatement(aKeyRange.lower(), aStatement,
                                               kStmtParamNameLowerKey,
                                               aKeyTransformation)));

  if (aKeyRange.isOnly()) {
    return NS_OK;
  }

  QM_TRY(MOZ_TO_RESULT(MaybeBindKeyToStatement(aKeyRange.upper(), aStatement,
                                               kStmtParamNameUpperKey,
                                               aKeyTransformation)));

  return NS_OK;
}

// static
nsresult DatabaseOperationBase::BindKeyRangeToStatement(
    const SerializedKeyRange& aKeyRange,
    mozIStorageStatement* const aStatement) {
  return BindTransformedKeyRangeToStatement(
      aKeyRange, aStatement, [](const Key& key) -> const auto& { return key; });
}

// static
nsresult DatabaseOperationBase::BindKeyRangeToStatement(
    const SerializedKeyRange& aKeyRange, mozIStorageStatement* const aStatement,
    const nsCString& aLocale) {
  MOZ_ASSERT(!aLocale.IsEmpty());

  return BindTransformedKeyRangeToStatement(
      aKeyRange, aStatement,
      [&aLocale](const Key& key) { return key.ToLocaleAwareKey(aLocale); });
}

// static
void CommonOpenOpHelperBase::AppendConditionClause(
    const nsACString& aColumnName, const nsACString& aStatementParameterName,
    bool aLessThan, bool aEquals, nsCString& aResult) {
  aResult += " AND "_ns + aColumnName + " "_ns;

  if (aLessThan) {
    aResult.Append('<');
  } else {
    aResult.Append('>');
  }

  if (aEquals) {
    aResult.Append('=');
  }

  aResult += " :"_ns + aStatementParameterName;
}

// static
Result<IndexDataValuesAutoArray, nsresult>
DatabaseOperationBase::IndexDataValuesFromUpdateInfos(
    const nsTArray<IndexUpdateInfo>& aUpdateInfos,
    const UniqueIndexTable& aUniqueIndexTable) {
  MOZ_ASSERT_IF(!aUpdateInfos.IsEmpty(), aUniqueIndexTable.Count());

  AUTO_PROFILER_LABEL("DatabaseOperationBase::IndexDataValuesFromUpdateInfos",
                      DOM);

  // XXX We could use TransformIntoNewArray here if it allowed to specify that
  // an AutoArray should be created.
  IndexDataValuesAutoArray indexValues;

  if (NS_WARN_IF(!indexValues.SetCapacity(aUpdateInfos.Length(), fallible))) {
    IDB_REPORT_INTERNAL_ERR();
    return Err(NS_ERROR_OUT_OF_MEMORY);
  }

  std::transform(aUpdateInfos.cbegin(), aUpdateInfos.cend(),
                 MakeBackInserter(indexValues),
                 [&aUniqueIndexTable](const IndexUpdateInfo& updateInfo) {
                   const IndexOrObjectStoreId& indexId = updateInfo.indexId();

                   bool unique = false;
                   MOZ_ALWAYS_TRUE(aUniqueIndexTable.Get(indexId, &unique));

                   return IndexDataValue{indexId, unique, updateInfo.value(),
                                         updateInfo.localizedValue()};
                 });
  indexValues.Sort();

  return indexValues;
}

// static
nsresult DatabaseOperationBase::InsertIndexTableRows(
    DatabaseConnection* aConnection, const IndexOrObjectStoreId aObjectStoreId,
    const Key& aObjectStoreKey, const nsTArray<IndexDataValue>& aIndexValues) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(!aObjectStoreKey.IsUnset());

  AUTO_PROFILER_LABEL("DatabaseOperationBase::InsertIndexTableRows", DOM);

  const uint32_t count = aIndexValues.Length();
  if (!count) {
    return NS_OK;
  }

  auto insertUniqueStmt = DatabaseConnection::LazyStatement{
      *aConnection,
      "INSERT INTO unique_index_data "
      "(index_id, value, object_store_id, "
      "object_data_key, value_locale) "
      "VALUES (:"_ns +
          kStmtParamNameIndexId + ", :"_ns + kStmtParamNameValue + ", :"_ns +
          kStmtParamNameObjectStoreId + ", :"_ns + kStmtParamNameObjectDataKey +
          ", :"_ns + kStmtParamNameValueLocale + ");"_ns};
  auto insertStmt = DatabaseConnection::LazyStatement{
      *aConnection,
      "INSERT OR IGNORE INTO index_data "
      "(index_id, value, object_data_key, "
      "object_store_id, value_locale) "
      "VALUES (:"_ns +
          kStmtParamNameIndexId + ", :"_ns + kStmtParamNameValue + ", :"_ns +
          kStmtParamNameObjectDataKey + ", :"_ns + kStmtParamNameObjectStoreId +
          ", :"_ns + kStmtParamNameValueLocale + ");"_ns};

  for (uint32_t index = 0; index < count; index++) {
    const IndexDataValue& info = aIndexValues[index];

    auto& stmt = info.mUnique ? insertUniqueStmt : insertStmt;

    QM_TRY_INSPECT(const auto& borrowedStmt, stmt.Borrow());

    QM_TRY(MOZ_TO_RESULT(
        borrowedStmt->BindInt64ByName(kStmtParamNameIndexId, info.mIndexId)));
    QM_TRY(MOZ_TO_RESULT(
        info.mPosition.BindToStatement(&*borrowedStmt, kStmtParamNameValue)));
    QM_TRY(MOZ_TO_RESULT(info.mLocaleAwarePosition.BindToStatement(
        &*borrowedStmt, kStmtParamNameValueLocale)));
    QM_TRY(MOZ_TO_RESULT(borrowedStmt->BindInt64ByName(
        kStmtParamNameObjectStoreId, aObjectStoreId)));
    QM_TRY(MOZ_TO_RESULT(aObjectStoreKey.BindToStatement(
        &*borrowedStmt, kStmtParamNameObjectDataKey)));

    // QM_OR_ELSE_WARN_IF is not used here since we just want to log the
    // collision and not spam the reports.
    QM_TRY(QM_OR_ELSE_LOG_VERBOSE_IF(
        // Expression.
        MOZ_TO_RESULT(borrowedStmt->Execute()),
        // Predicate.
        ([&info, index, &aIndexValues](nsresult rv) {
          if (rv == NS_ERROR_STORAGE_CONSTRAINT && info.mUnique) {
            // If we're inserting multiple entries for the same unique
            // index, then we might have failed to insert due to
            // colliding with another entry for the same index in which
            // case we should ignore it.
            for (int32_t index2 = int32_t(index) - 1;
                 index2 >= 0 && aIndexValues[index2].mIndexId == info.mIndexId;
                 --index2) {
              if (info.mPosition == aIndexValues[index2].mPosition) {
                // We found a key with the same value for the same
                // index. So we must have had a collision with a value
                // we just inserted.
                return true;
              }
            }
          }

          return false;
        }),
        // Fallback.
        ErrToDefaultOk<>));
  }

  return NS_OK;
}

// static
nsresult DatabaseOperationBase::DeleteIndexDataTableRows(
    DatabaseConnection* aConnection, const Key& aObjectStoreKey,
    const nsTArray<IndexDataValue>& aIndexValues) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(!aObjectStoreKey.IsUnset());

  AUTO_PROFILER_LABEL("DatabaseOperationBase::DeleteIndexDataTableRows", DOM);

  const uint32_t count = aIndexValues.Length();
  if (!count) {
    return NS_OK;
  }

  auto deleteUniqueStmt = DatabaseConnection::LazyStatement{
      *aConnection, "DELETE FROM unique_index_data WHERE index_id = :"_ns +
                        kStmtParamNameIndexId + " AND value = :"_ns +
                        kStmtParamNameValue + ";"_ns};
  auto deleteStmt = DatabaseConnection::LazyStatement{
      *aConnection, "DELETE FROM index_data WHERE index_id = :"_ns +
                        kStmtParamNameIndexId + " AND value = :"_ns +
                        kStmtParamNameValue + " AND object_data_key = :"_ns +
                        kStmtParamNameObjectDataKey + ";"_ns};

  for (uint32_t index = 0; index < count; index++) {
    const IndexDataValue& indexValue = aIndexValues[index];

    auto& stmt = indexValue.mUnique ? deleteUniqueStmt : deleteStmt;

    QM_TRY_INSPECT(const auto& borrowedStmt, stmt.Borrow());

    QM_TRY(MOZ_TO_RESULT(borrowedStmt->BindInt64ByName(kStmtParamNameIndexId,
                                                       indexValue.mIndexId)));

    QM_TRY(MOZ_TO_RESULT(indexValue.mPosition.BindToStatement(
        &*borrowedStmt, kStmtParamNameValue)));

    if (!indexValue.mUnique) {
      QM_TRY(MOZ_TO_RESULT(aObjectStoreKey.BindToStatement(
          &*borrowedStmt, kStmtParamNameObjectDataKey)));
    }

    QM_TRY(MOZ_TO_RESULT(borrowedStmt->Execute()));
  }

  return NS_OK;
}

// static
nsresult DatabaseOperationBase::DeleteObjectStoreDataTableRowsWithIndexes(
    DatabaseConnection* aConnection, const IndexOrObjectStoreId aObjectStoreId,
    const Maybe<SerializedKeyRange>& aKeyRange) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(aObjectStoreId);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& hasIndexes,
                   ObjectStoreHasIndexes(*aConnection, aObjectStoreId),
                   QM_PROPAGATE, [](const auto&) { MOZ_ASSERT(false); });
    MOZ_ASSERT(hasIndexes,
               "Don't use this slow method if there are no indexes!");
  }
#endif

  AUTO_PROFILER_LABEL(
      "DatabaseOperationBase::DeleteObjectStoreDataTableRowsWithIndexes", DOM);

  const bool singleRowOnly = aKeyRange.isSome() && aKeyRange.ref().isOnly();

  const auto keyRangeClause =
      MaybeGetBindingClauseForKeyRange(aKeyRange, kColumnNameKey);

  Key objectStoreKey;
  QM_TRY_INSPECT(
      const auto& selectStmt,
      ([singleRowOnly, &aConnection, &objectStoreKey, &aKeyRange,
        &keyRangeClause]()
           -> Result<CachingDatabaseConnection::BorrowedStatement, nsresult> {
        if (singleRowOnly) {
          QM_TRY_UNWRAP(auto selectStmt,
                        aConnection->BorrowCachedStatement(
                            "SELECT index_data_values "
                            "FROM object_data "
                            "WHERE object_store_id = :"_ns +
                            kStmtParamNameObjectStoreId + " AND key = :"_ns +
                            kStmtParamNameKey + ";"_ns));

          objectStoreKey = aKeyRange.ref().lower();

          QM_TRY(MOZ_TO_RESULT(
              objectStoreKey.BindToStatement(&*selectStmt, kStmtParamNameKey)));

          return selectStmt;
        }

        QM_TRY_UNWRAP(
            auto selectStmt,
            aConnection->BorrowCachedStatement(
                "SELECT index_data_values, "_ns + kColumnNameKey +
                " FROM object_data WHERE object_store_id = :"_ns +
                kStmtParamNameObjectStoreId + keyRangeClause + ";"_ns));

        if (aKeyRange.isSome()) {
          QM_TRY(MOZ_TO_RESULT(
              BindKeyRangeToStatement(aKeyRange.ref(), &*selectStmt)));
        }

        return selectStmt;
      }()));

  QM_TRY(MOZ_TO_RESULT(selectStmt->BindInt64ByName(kStmtParamNameObjectStoreId,
                                                   aObjectStoreId)));

  DebugOnly<uint32_t> resultCountDEBUG = 0;

  QM_TRY(CollectWhileHasResult(
      *selectStmt,
      [singleRowOnly, &objectStoreKey, &aConnection, &resultCountDEBUG,
       indexValues = IndexDataValuesAutoArray{}](
          auto& selectStmt) mutable -> Result<Ok, nsresult> {
        if (!singleRowOnly) {
          QM_TRY(
              MOZ_TO_RESULT(objectStoreKey.SetFromStatement(&selectStmt, 1)));

          indexValues.ClearAndRetainStorage();
        }

        QM_TRY(MOZ_TO_RESULT(
            ReadCompressedIndexDataValues(selectStmt, 0, indexValues)));
        QM_TRY(MOZ_TO_RESULT(DeleteIndexDataTableRows(
            aConnection, objectStoreKey, indexValues)));

        resultCountDEBUG++;

        return Ok{};
      }));

  MOZ_ASSERT_IF(singleRowOnly, resultCountDEBUG <= 1);

  QM_TRY_UNWRAP(
      auto deleteManyStmt,
      aConnection->BorrowCachedStatement(
          "DELETE FROM object_data "_ns + "WHERE object_store_id = :"_ns +
          kStmtParamNameObjectStoreId + keyRangeClause + ";"_ns));

  QM_TRY(MOZ_TO_RESULT(deleteManyStmt->BindInt64ByName(
      kStmtParamNameObjectStoreId, aObjectStoreId)));

  if (aKeyRange.isSome()) {
    QM_TRY(MOZ_TO_RESULT(
        BindKeyRangeToStatement(aKeyRange.ref(), &*deleteManyStmt)));
  }

  QM_TRY(MOZ_TO_RESULT(deleteManyStmt->Execute()));

  return NS_OK;
}

// static
nsresult DatabaseOperationBase::UpdateIndexValues(
    DatabaseConnection* aConnection, const IndexOrObjectStoreId aObjectStoreId,
    const Key& aObjectStoreKey, const nsTArray<IndexDataValue>& aIndexValues) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(!aObjectStoreKey.IsUnset());

  AUTO_PROFILER_LABEL("DatabaseOperationBase::UpdateIndexValues", DOM);

  QM_TRY_UNWRAP((auto [indexDataValues, indexDataValuesLength]),
                MakeCompressedIndexDataValues(aIndexValues));

  MOZ_ASSERT(!indexDataValuesLength == !(indexDataValues.get()));

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "UPDATE object_data SET index_data_values = :"_ns +
          kStmtParamNameIndexDataValues + " WHERE object_store_id = :"_ns +
          kStmtParamNameObjectStoreId + " AND key = :"_ns + kStmtParamNameKey +
          ";"_ns,
      [&indexDataValues = indexDataValues,
       indexDataValuesLength = indexDataValuesLength, aObjectStoreId,
       &aObjectStoreKey](
          mozIStorageStatement& updateStmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(
            indexDataValues
                ? updateStmt.BindAdoptedBlobByName(
                      kStmtParamNameIndexDataValues, indexDataValues.release(),
                      indexDataValuesLength)
                : updateStmt.BindNullByName(kStmtParamNameIndexDataValues)));

        QM_TRY(MOZ_TO_RESULT(updateStmt.BindInt64ByName(
            kStmtParamNameObjectStoreId, aObjectStoreId)));

        QM_TRY(MOZ_TO_RESULT(
            aObjectStoreKey.BindToStatement(&updateStmt, kStmtParamNameKey)));

        return Ok{};
      })));

  return NS_OK;
}

// static
Result<bool, nsresult> DatabaseOperationBase::ObjectStoreHasIndexes(
    DatabaseConnection& aConnection,
    const IndexOrObjectStoreId aObjectStoreId) {
  aConnection.AssertIsOnConnectionThread();
  MOZ_ASSERT(aObjectStoreId);

  QM_TRY_RETURN(aConnection
                    .BorrowAndExecuteSingleStepStatement(
                        "SELECT id "
                        "FROM object_store_index "
                        "WHERE object_store_id = :"_ns +
                            kStmtParamNameObjectStoreId + kOpenLimit + "1;"_ns,
                        [aObjectStoreId](auto& stmt) -> Result<Ok, nsresult> {
                          QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByName(
                              kStmtParamNameObjectStoreId, aObjectStoreId)));
                          return Ok{};
                        })
                    .map(IsSome));
}

NS_IMPL_ISUPPORTS_INHERITED(DatabaseOperationBase, Runnable,
                            mozIStorageProgressHandler)

NS_IMETHODIMP
DatabaseOperationBase::OnProgress(mozIStorageConnection* aConnection,
                                  bool* _retval) {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(_retval);

  // This is intentionally racy.
  *_retval = QuotaClient::IsShuttingDownOnNonBackgroundThread() ||
             !OperationMayProceed();
  return NS_OK;
}

DatabaseOperationBase::AutoSetProgressHandler::AutoSetProgressHandler()
    : mConnection(Nothing())
#ifdef DEBUG
      ,
      mDEBUGDatabaseOp(nullptr)
#endif
{
  MOZ_ASSERT(!IsOnBackgroundThread());
}

DatabaseOperationBase::AutoSetProgressHandler::~AutoSetProgressHandler() {
  MOZ_ASSERT(!IsOnBackgroundThread());

  if (mConnection) {
    Unregister();
  }
}

nsresult DatabaseOperationBase::AutoSetProgressHandler::Register(
    mozIStorageConnection& aConnection, DatabaseOperationBase* aDatabaseOp) {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aDatabaseOp);
  MOZ_ASSERT(!mConnection);

  QM_TRY_UNWRAP(
      const DebugOnly oldProgressHandler,
      MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
          nsCOMPtr<mozIStorageProgressHandler>, aConnection, SetProgressHandler,
          kStorageProgressGranularity, aDatabaseOp));

  MOZ_ASSERT(!oldProgressHandler.inspect());

  mConnection = SomeRef(aConnection);
#ifdef DEBUG
  mDEBUGDatabaseOp = aDatabaseOp;
#endif

  return NS_OK;
}

void DatabaseOperationBase::AutoSetProgressHandler::Unregister() {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(mConnection);

  nsCOMPtr<mozIStorageProgressHandler> oldHandler;
  MOZ_ALWAYS_SUCCEEDS(
      mConnection->RemoveProgressHandler(getter_AddRefs(oldHandler)));
  MOZ_ASSERT(oldHandler == mDEBUGDatabaseOp);

  mConnection = Nothing();
}

FactoryOp::FactoryOp(SafeRefPtr<Factory> aFactory,
                     const Maybe<ContentParentId>& aContentParentId,
                     const PersistenceType aPersistenceType,
                     const PrincipalInfo& aPrincipalInfo,
                     const Maybe<nsString>& aDatabaseName, bool aDeleting)
    : DatabaseOperationBase(aFactory->GetLoggingInfo()->Id(),
                            aFactory->GetLoggingInfo()->NextRequestSN()),
      mFactory(std::move(aFactory)),
      mContentParentId(aContentParentId),
      mPrincipalInfo(aPrincipalInfo),
      mDatabaseName(aDatabaseName),
      mDirectoryLockId(-1),
      mPersistenceType(aPersistenceType),
      mState(State::Initial),
      mWaitingForPermissionRetry(false),
      mEnforcingQuota(true),
      mDeleting(aDeleting) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mFactory);
  MOZ_ASSERT(!QuotaClient::IsShuttingDownOnBackgroundThread());
}

nsresult FactoryOp::DispatchThisAfterProcessingCurrentEvent(
    nsCOMPtr<nsIEventTarget> aEventTarget) {
  QM_TRY(MOZ_TO_RESULT(RunAfterProcessingCurrentEvent(
      [eventTarget = std::move(aEventTarget), self = RefPtr(this)]() mutable {
        QM_WARNONLY_TRY(MOZ_TO_RESULT(
            eventTarget->Dispatch(self.forget(), NS_DISPATCH_NORMAL)));
      })));

  return NS_OK;
}

void FactoryOp::NoteDatabaseBlocked(Database* aDatabase) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(mState == State::WaitingForOtherDatabasesToClose);
  MOZ_ASSERT(!mMaybeBlockedDatabases.IsEmpty());
  MOZ_ASSERT(mMaybeBlockedDatabases.Contains(aDatabase));

  // Only send the blocked event if all databases have reported back. If the
  // database was closed then it will have been removed from the array.
  // Otherwise if it was blocked its |mBlocked| flag will be true.
  bool sendBlockedEvent = true;

  for (auto& info : mMaybeBlockedDatabases) {
    if (info == aDatabase) {
      // This database was blocked, mark accordingly.
      info.mBlocked = true;
    } else if (!info.mBlocked) {
      // A database has not yet reported back yet, don't send the event yet.
      sendBlockedEvent = false;
    }
  }

  if (sendBlockedEvent) {
    SendBlockedNotification();
  }
}

void FactoryOp::NoteDatabaseClosed(Database* const aDatabase) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDatabase);
  MOZ_ASSERT(mState == State::WaitingForOtherDatabasesToClose);
  MOZ_ASSERT(!mMaybeBlockedDatabases.IsEmpty());
  MOZ_ASSERT(mMaybeBlockedDatabases.Contains(aDatabase));

  mMaybeBlockedDatabases.RemoveElement(aDatabase);

  if (!mMaybeBlockedDatabases.IsEmpty()) {
    return;
  }

  DatabaseActorInfo* info;
  MOZ_ALWAYS_TRUE(gLiveDatabaseHashtable->Get(mDatabaseId.ref(), &info));
  MOZ_ASSERT(info->mWaitingFactoryOp == this);

  if (AreActorsAlive()) {
    // The IPDL strong reference has not yet been released, so we can clear
    // mWaitingFactoryOp immediately.
    info->mWaitingFactoryOp = nullptr;

    WaitForTransactions();
    return;
  }

  // The IPDL strong reference has been released, mWaitingFactoryOp holds the
  // last strong reference to us, so we need to move it to a stack variable
  // instead of clearing it immediately (We could clear it immediately if only
  // the other actor is destroyed, but we don't need to optimize for that, and
  // move it anyway).
  const RefPtr<FactoryOp> waitingFactoryOp = std::move(info->mWaitingFactoryOp);

  IDB_REPORT_INTERNAL_ERR();
  SetFailureCodeIfUnset(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);

  // We hold a strong ref in waitingFactoryOp, so it's safe to call Run()
  // directly.

  mState = State::SendingResults;
  MOZ_ALWAYS_SUCCEEDS(Run());
}

void FactoryOp::StringifyState(nsACString& aResult) const {
  AssertIsOnOwningThread();

  switch (mState) {
    case State::Initial:
      aResult.AppendLiteral("Initial");
      return;

    case State::DirectoryOpenPending:
      aResult.AppendLiteral("DirectoryOpenPending");
      return;

    case State::DirectoryWorkOpen:
      aResult.AppendLiteral("DirectoryWorkOpen");
      return;

    case State::DirectoryWorkDone:
      aResult.AppendLiteral("DirectoryWorkDone");
      return;

    case State::DatabaseOpenPending:
      aResult.AppendLiteral("DatabaseOpenPending");
      return;

    case State::DatabaseWorkOpen:
      aResult.AppendLiteral("DatabaseWorkOpen");
      return;

    case State::BeginVersionChange:
      aResult.AppendLiteral("BeginVersionChange");
      return;

    case State::WaitingForOtherDatabasesToClose:
      aResult.AppendLiteral("WaitingForOtherDatabasesToClose");
      return;

    case State::WaitingForTransactionsToComplete:
      aResult.AppendLiteral("WaitingForTransactionsToComplete");
      return;

    case State::DatabaseWorkVersionChange:
      aResult.AppendLiteral("DatabaseWorkVersionChange");
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

void FactoryOp::Stringify(nsACString& aResult) const {
  AssertIsOnOwningThread();

  aResult.AppendLiteral("PersistenceType:");
  aResult.Append(PersistenceTypeToString(mPersistenceType));
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("Origin:");
  aResult.Append(AnonymizedOriginString(mOriginMetadata.mOrigin));
  aResult.Append(kQuotaGenericDelimiter);

  aResult.AppendLiteral("State:");
  StringifyState(aResult);
}

nsresult FactoryOp::Open() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::Initial);
  MOZ_ASSERT(mOriginMetadata.mOrigin.IsEmpty());
  MOZ_ASSERT(!mDirectoryLockHandle);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  QM_TRY(QuotaManager::EnsureCreated());

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_UNWRAP(
      auto principalMetadata,
      quota::GetInfoFromValidatedPrincipalInfo(*quotaManager, mPrincipalInfo));

  mOriginMetadata = {std::move(principalMetadata), mPersistenceType};

  if (mPrincipalInfo.type() == PrincipalInfo::TSystemPrincipalInfo) {
    MOZ_ASSERT(mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

    mEnforcingQuota = false;
  } else if (mPrincipalInfo.type() == PrincipalInfo::TContentPrincipalInfo) {
    const ContentPrincipalInfo& contentPrincipalInfo =
        mPrincipalInfo.get_ContentPrincipalInfo();

    MOZ_ASSERT_IF(
        QuotaManager::IsOriginInternal(contentPrincipalInfo.originNoSuffix()),
        mPersistenceType == PERSISTENCE_TYPE_PERSISTENT);

    mEnforcingQuota = mPersistenceType != PERSISTENCE_TYPE_PERSISTENT;

    if (mOriginMetadata.mIsPrivate) {
      if (StaticPrefs::dom_indexedDB_privateBrowsing_enabled()) {
        // Explicitly disallow moz-extension urls from using the encrypted
        // indexedDB storage mode when the caller is an extension (see Bug
        // 1841806).
        if (StringBeginsWith(contentPrincipalInfo.originNoSuffix(),
                             "moz-extension:"_ns)) {
          return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
        }

        mInPrivateBrowsing.Flip();
      } else {
        return NS_ERROR_DOM_INDEXEDDB_NOT_ALLOWED_ERR;
      }
    }
  } else {
    MOZ_ASSERT(false);
  }

  if (mDatabaseName.isSome()) {
    nsCString databaseId;

    QuotaManager::GetStorageId(mPersistenceType, mOriginMetadata.mOrigin,
                               Client::IDB, databaseId);

    databaseId.Append('*');
    databaseId.Append(NS_ConvertUTF16toUTF8(mDatabaseName.ref()));

    mDatabaseId = Some(std::move(databaseId));

    // Need to get database file path before opening the directory.
    // XXX: For what reason?
    QM_TRY_UNWRAP(
        auto databaseFilePath,
        ([this, quotaManager]() -> mozilla::Result<nsString, nsresult> {
          QM_TRY_INSPECT(const auto& dbFile,
                         quotaManager->GetOriginDirectory(mOriginMetadata));

          QM_TRY(MOZ_TO_RESULT(dbFile->Append(
              NS_LITERAL_STRING_FROM_CSTRING(IDB_DIRECTORY_NAME))));

          QM_TRY(MOZ_TO_RESULT(dbFile->Append(
              GetDatabaseFilenameBase(mDatabaseName.ref(),
                                      mOriginMetadata.mIsPrivate) +
              kSQLiteSuffix)));

          QM_TRY_RETURN(
              MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, dbFile, GetPath));
        }()));

    mDatabaseFilePath = Some(std::move(databaseFilePath));
  }

  // Open directory
  mState = State::DirectoryOpenPending;

  quotaManager->OpenClientDirectory({mOriginMetadata, Client::IDB})
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this)](QuotaManager::ClientDirectoryLockHandlePromise::
                                    ResolveOrRejectValue&& aValue) {
            if (aValue.IsResolve()) {
              self->DirectoryLockAcquired(std::move(aValue.ResolveValue()));
            } else {
              self->DirectoryLockFailed();
            }
          });

  return NS_OK;
}

nsresult FactoryOp::DirectoryOpen() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(mDirectoryLockHandle);

  if (mDatabaseName.isNothing()) {
    QuotaManager* const quotaManager = QuotaManager::Get();
    MOZ_ASSERT(quotaManager);

    // Must set this before dispatching otherwise we will race with the IO
    // thread.
    mState = State::DirectoryWorkOpen;

    QM_TRY(MOZ_TO_RESULT(
               quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL)),
           NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR, IDB_REPORT_INTERNAL_ERR_LAMBDA);

    return NS_OK;
  }

  mState = State::DirectoryWorkDone;
  MOZ_ALWAYS_SUCCEEDS(Run());

  return NS_OK;
}

nsresult FactoryOp::DirectoryWorkDone() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DirectoryWorkDone);
  MOZ_ASSERT(mDirectoryLockHandle);
  MOZ_ASSERT(gFactoryOps);

  // See if this FactoryOp needs to wait.
  const bool blocked = [&self = *this] {
    bool foundThis = false;
    bool blocked = false;

    for (const auto& existingOp : Reversed(*gFactoryOps)) {
      if (existingOp == &self) {
        foundThis = true;
        continue;
      }

      if (foundThis && self.MustWaitFor(*existingOp)) {
        existingOp->AddBlockingOp(self);
        self.AddBlockedOnOp(*existingOp);
        blocked = true;
      }
    }

    return blocked;
  }() || [&self = *this] {
    QuotaClient* quotaClient = QuotaClient::GetInstance();
    MOZ_ASSERT(quotaClient);

    if (RefPtr<Maintenance> currentMaintenance =
            quotaClient->GetCurrentMaintenance()) {
      if (self.mDatabaseName.isSome()) {
        if (RefPtr<DatabaseMaintenance> databaseMaintenance =
                currentMaintenance->GetDatabaseMaintenance(
                    self.mDatabaseFilePath.ref())) {
          databaseMaintenance->WaitForCompletion(&self);
          return true;
        }
      } else if (currentMaintenance->HasDatabaseMaintenances()) {
        currentMaintenance->WaitForCompletion(&self);
        return true;
      }
    }

    return false;
  }();

  mState = State::DatabaseOpenPending;
  if (!blocked) {
    QM_TRY(MOZ_TO_RESULT(DatabaseOpen()));
  }

  return NS_OK;
}

nsresult FactoryOp::SendToIOThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DatabaseOpenPending);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      !OperationMayProceed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  // Must set this before dispatching otherwise we will race with the IO thread.
  mState = State::DatabaseWorkOpen;

  QM_TRY(MOZ_TO_RESULT(
             quotaManager->IOThread()->Dispatch(this, NS_DISPATCH_NORMAL)),
         NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR, IDB_REPORT_INTERNAL_ERR_LAMBDA);

  NotifyDatabaseWorkStarted();

  return NS_OK;
}

void FactoryOp::WaitForTransactions() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::BeginVersionChange ||
             mState == State::WaitingForOtherDatabasesToClose);
  MOZ_ASSERT(!mDatabaseId.ref().IsEmpty());
  MOZ_ASSERT(!IsActorDestroyed());

  mState = State::WaitingForTransactionsToComplete;

  RefPtr<WaitForTransactionsHelper> helper =
      new WaitForTransactionsHelper(mDatabaseId.ref(), this);
  helper->WaitForTransactions();
}

void FactoryOp::CleanupMetadata() {
  AssertIsOnOwningThread();

  for (const NotNull<RefPtr<FactoryOp>>& blockingOp : mBlocking) {
    blockingOp->MaybeUnblock(*this);
  }
  mBlocking.Clear();

  MOZ_ASSERT(gFactoryOps);
  gFactoryOps->RemoveElement(this);

  // We might get here even after QuotaManagerOpen failed, so we need to check
  // if we have a quota manager.
  quota::QuotaManager::SafeMaybeRecordQuotaClientShutdownStep(
      quota::Client::IDB, "An element was removed from gFactoryOps"_ns);

  // Match the IncreaseBusyCount in AllocPBackgroundIDBFactoryRequestParent().
  DecreaseBusyCount();
}

void FactoryOp::FinishSendResults() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);
  MOZ_ASSERT(mFactory);

  mState = State::Completed;

  // Make sure to release the factory on this thread.
  mFactory = nullptr;
}

nsresult FactoryOp::SendVersionChangeMessages(
    DatabaseActorInfo* aDatabaseActorInfo, Maybe<Database&> aOpeningDatabase,
    uint64_t aOldVersion, const Maybe<uint64_t>& aNewVersion) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aDatabaseActorInfo);
  MOZ_ASSERT(mState == State::BeginVersionChange);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());
  MOZ_ASSERT(!IsActorDestroyed());

  const uint32_t expectedCount = mDeleting ? 0 : 1;
  const uint32_t liveCount = aDatabaseActorInfo->mLiveDatabases.Length();
  if (liveCount > expectedCount) {
    nsTArray<MaybeBlockedDatabaseInfo> maybeBlockedDatabases;
    for (const auto& database : aDatabaseActorInfo->mLiveDatabases) {
      if ((!aOpeningDatabase || database.get() != &aOpeningDatabase.ref()) &&
          !database->IsClosed() &&
          NS_WARN_IF(!maybeBlockedDatabases.AppendElement(
              SafeRefPtr{database.get(), AcquireStrongRefFromRawPtr{}},
              fallible))) {
        return NS_ERROR_OUT_OF_MEMORY;
      }
    }

    mMaybeBlockedDatabases = std::move(maybeBlockedDatabases);
  }

  // We don't want to wait forever if we were not able to send the
  // message.
  mMaybeBlockedDatabases.RemoveLastElements(
      mMaybeBlockedDatabases.end() -
      std::remove_if(mMaybeBlockedDatabases.begin(),
                     mMaybeBlockedDatabases.end(),
                     [aOldVersion, &aNewVersion](auto& maybeBlockedDatabase) {
                       return !maybeBlockedDatabase->SendVersionChange(
                           aOldVersion, aNewVersion);
                     }));

  return NS_OK;
}  // namespace indexedDB

bool FactoryOp::MustWaitFor(const FactoryOp& aExistingOp) {
  AssertIsOnOwningThread();

  // If the persistence types don't overlap, the op can proceed.
  if (aExistingOp.mPersistenceType != mPersistenceType) {
    return false;
  }

  // If the origins don't overlap, the op can proceed.
  if (aExistingOp.mOriginMetadata.mOrigin != mOriginMetadata.mOrigin) {
    return false;
  }

  // If the database ids don't overlap, the op can proceed.
  if (!aExistingOp.mDatabaseId.isNothing() && !mDatabaseId.isNothing() &&
      aExistingOp.mDatabaseId.ref() != mDatabaseId.ref()) {
    return false;
  }

  return true;
}

// Run() assumes that the caller holds a strong reference to the object that
// can't be cleared while Run() is being executed.
// So if you call Run() directly (as opposed to dispatching to an event queue)
// you need to make sure there's such a reference.
// See bug 1356824 for more details.
NS_IMETHODIMP
FactoryOp::Run() {
  const auto handleError = [this](const nsresult rv) {
    if (mState != State::SendingResults) {
      SetFailureCodeIfUnset(rv);

      // Must set mState before dispatching otherwise we will race with the
      // owning thread.
      mState = State::SendingResults;

      if (IsOnOwningThread()) {
        SendResults();
      } else {
        MOZ_ALWAYS_SUCCEEDS(
            DispatchThisAfterProcessingCurrentEvent(mOwningEventTarget));
      }
    }
  };

  switch (mState) {
    case State::Initial:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(Open()), handleError);
      break;

    case State::DirectoryWorkOpen:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(DoDirectoryWork()), handleError);
      break;

    case State::DirectoryWorkDone:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(DirectoryWorkDone()), handleError);
      break;

    case State::DatabaseOpenPending:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(DatabaseOpen()), handleError);
      break;

    case State::DatabaseWorkOpen:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(DoDatabaseWork()), handleError);
      break;

    case State::BeginVersionChange:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(BeginVersionChange()), handleError);
      break;

    case State::WaitingForTransactionsToComplete:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(DispatchToWorkThread()), handleError);
      break;

    case State::DatabaseWorkVersionUpdate:
      QM_WARNONLY_TRY(MOZ_TO_RESULT(DoVersionUpdate()), handleError);
      break;

    case State::SendingResults:
      SendResults();
      break;

    default:
      MOZ_CRASH("Bad state!");
  }

  return NS_OK;
}

void FactoryOp::DirectoryLockAcquired(ClientDirectoryLockHandle aLockHandle) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(aLockHandle);
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  mDirectoryLockHandle = std::move(aLockHandle);

  MOZ_ASSERT(mDirectoryLockHandle->Id() >= 0);
  mDirectoryLockId = mDirectoryLockHandle->Id();

  auto cleanupAndReturn = [self = RefPtr(this)](const nsresult rv) {
    self->SetFailureCodeIfUnset(rv);

    // The caller holds a strong reference to us, no need for a self reference
    // before calling Run().

    self->mState = State::SendingResults;
    MOZ_ALWAYS_SUCCEEDS(self->Run());
  };

  if (mDirectoryLockHandle->Invalidated()) {
    return cleanupAndReturn(NS_ERROR_DOM_INDEXEDDB_ABORT_ERR);
  }

  QM_WARNONLY_TRY(MOZ_TO_RESULT(DirectoryOpen()), cleanupAndReturn);
}

void FactoryOp::DirectoryLockFailed() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DirectoryOpenPending);
  MOZ_ASSERT(!mDirectoryLockHandle);

  if (!HasFailed()) {
    IDB_REPORT_INTERNAL_ERR();
    SetFailureCode(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  }

  // The caller holds a strong reference to us, no need for a self reference
  // before calling Run().

  mState = State::SendingResults;
  MOZ_ALWAYS_SUCCEEDS(Run());
}

nsresult FactoryRequestOp::DoDirectoryWork() {
  MOZ_CRASH("Not implemented because this should be unreachable.");
}

void FactoryRequestOp::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();

  NoteActorDestroyed();
}

OpenDatabaseOp::OpenDatabaseOp(SafeRefPtr<Factory> aFactory,
                               const Maybe<ContentParentId>& aContentParentId,
                               const CommonFactoryRequestParams& aParams)
    : FactoryRequestOp(std::move(aFactory), aContentParentId, aParams,
                       /* aDeleting */ false),
      mMetadata(MakeSafeRefPtr<FullDatabaseMetadata>(aParams.metadata())),
      mRequestedVersion(aParams.metadata().version()),
      mVersionChangeOp(nullptr),
      mTelemetryId(0) {}

void OpenDatabaseOp::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  FactoryRequestOp::ActorDestroy(aWhy);

  if (mVersionChangeOp) {
    mVersionChangeOp->NoteActorDestroyed();
  }
}

nsresult OpenDatabaseOp::DatabaseOpen() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DatabaseOpenPending);

  nsresult rv = SendToIOThread();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

nsresult OpenDatabaseOp::DoDatabaseWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DatabaseWorkOpen);

  AUTO_PROFILER_LABEL("OpenDatabaseOp::DoDatabaseWork", DOM);

  QM_TRY(OkIf(!QuotaClient::IsShuttingDownOnNonBackgroundThread()),
         NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR, IDB_REPORT_INTERNAL_ERR_LAMBDA);

  if (!OperationMayProceed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  const nsAString& databaseName = mCommonParams.metadata().name();
  const PersistenceType persistenceType =
      mCommonParams.metadata().persistenceType();

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_INSPECT(
      const auto& dbDirectory,
      ([persistenceType, &quotaManager,
        this]() -> mozilla::Result<nsCOMPtr<nsIFile>, nsresult> {
        if (persistenceType == PERSISTENCE_TYPE_PERSISTENT) {
          QM_TRY_RETURN(quotaManager->GetOriginDirectory(mOriginMetadata));
        }

        QM_TRY_RETURN(
            quotaManager->GetOrCreateTemporaryOriginDirectory(mOriginMetadata));
      }()));

  QM_TRY(MOZ_TO_RESULT(
      dbDirectory->Append(NS_LITERAL_STRING_FROM_CSTRING(IDB_DIRECTORY_NAME))));

  {
    QM_TRY_INSPECT(const bool& exists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(dbDirectory, Exists));

    if (!exists) {
      QM_TRY(MOZ_TO_RESULT(dbDirectory->Create(nsIFile::DIRECTORY_TYPE, 0755)));
    }
#ifdef DEBUG
    else {
      bool isDirectory;
      MOZ_ASSERT(NS_SUCCEEDED(dbDirectory->IsDirectory(&isDirectory)));
      MOZ_ASSERT(isDirectory);
    }
#endif
  }

  const auto databaseFilenameBase =
      GetDatabaseFilenameBase(databaseName, mOriginMetadata.mIsPrivate);

  QM_TRY_INSPECT(const auto& markerFile,
                 CloneFileAndAppend(*dbDirectory, kIdbDeletionMarkerFilePrefix +
                                                      databaseFilenameBase));

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(markerFile, Exists));

  if (exists) {
    // Delete the database and directroy since they should be deleted in
    // previous operation.
    // Note: only update usage to the QuotaManager when mEnforcingQuota == true
    QM_TRY(MOZ_TO_RESULT(RemoveDatabaseFilesAndDirectory(
        *dbDirectory, databaseFilenameBase,
        mEnforcingQuota ? quotaManager : nullptr, persistenceType,
        mOriginMetadata, databaseName)));
  }

  QM_TRY_INSPECT(
      const auto& dbFile,
      CloneFileAndAppend(*dbDirectory, databaseFilenameBase + kSQLiteSuffix));

  mTelemetryId = TelemetryIdForFile(dbFile);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(
        const auto& databaseFilePath,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, dbFile, GetPath));

    MOZ_ASSERT(databaseFilePath == mDatabaseFilePath.ref());
  }
#endif

  QM_TRY_INSPECT(
      const auto& fmDirectory,
      CloneFileAndAppend(*dbDirectory, databaseFilenameBase +
                                           kFileManagerDirectoryNameSuffix));

  IndexedDatabaseManager* const idm = IndexedDatabaseManager::Get();
  MOZ_ASSERT(idm);

  SafeRefPtr<DatabaseFileManager> fileManager = idm->GetFileManager(
      persistenceType, mOriginMetadata.mOrigin, databaseName);

  if (!fileManager) {
    fileManager = MakeSafeRefPtr<DatabaseFileManager>(
        persistenceType, mOriginMetadata, databaseName, mDatabaseId.ref(),
        mDatabaseFilePath.ref(), mEnforcingQuota, mInPrivateBrowsing);
  }

  Maybe<const CipherKey> maybeKey =
      mInPrivateBrowsing
          ? Some(fileManager->MutableCipherKeyManagerRef().Ensure())
          : Nothing();

  MOZ_RELEASE_ASSERT(mInPrivateBrowsing == maybeKey.isSome());

  QM_TRY_UNWRAP(
      NotNull<nsCOMPtr<mozIStorageConnection>> connection,
      CreateStorageConnection(*dbFile, *fmDirectory, databaseName,
                              mOriginMetadata.mOrigin, mDirectoryLockId,
                              mTelemetryId, maybeKey));

  AutoSetProgressHandler asph;
  QM_TRY(MOZ_TO_RESULT(asph.Register(*connection, this)));

  QM_TRY(MOZ_TO_RESULT(LoadDatabaseInformation(*connection)));

  MOZ_ASSERT(mMetadata->mNextObjectStoreId > mMetadata->mObjectStores.Count());
  MOZ_ASSERT(mMetadata->mNextIndexId > 0);

  // See if we need to do a versionchange transaction

  // Optional version semantics.
  if (!mRequestedVersion) {
    // If the requested version was not specified and the database was created,
    // treat it as if version 1 were requested.
    // Otherwise, treat it as if the current version were requested.
    mRequestedVersion = mMetadata->mCommonMetadata.version() == 0
                            ? 1
                            : mMetadata->mCommonMetadata.version();
  }

  QM_TRY(OkIf(mMetadata->mCommonMetadata.version() <= mRequestedVersion),
         NS_ERROR_DOM_INDEXEDDB_VERSION_ERR);

  if (!fileManager->Initialized()) {
    QM_TRY(MOZ_TO_RESULT(fileManager->Init(
        fmDirectory, mMetadata->mCommonMetadata.version(), *connection)));

    idm->AddFileManager(fileManager.clonePtr());
  }

  mFileManager = std::move(fileManager);

  // Must close connection before dispatching otherwise we might race with the
  // connection thread which needs to open the same database.
  asph.Unregister();

  MOZ_ALWAYS_SUCCEEDS(connection->Close());

  SleepIfEnabled(
      StaticPrefs::dom_indexedDB_databaseInitialization_pauseOnIOThreadMs());

  // Must set mState before dispatching otherwise we will race with the owning
  // thread.
  mState = (mMetadata->mCommonMetadata.version() == mRequestedVersion)
               ? State::SendingResults
               : State::BeginVersionChange;

  QM_TRY(MOZ_TO_RESULT(
      DispatchThisAfterProcessingCurrentEvent(mOwningEventTarget)));

  return NS_OK;
}

nsresult OpenDatabaseOp::LoadDatabaseInformation(
    mozIStorageConnection& aConnection) {
  AssertIsOnIOThread();
  MOZ_ASSERT(mMetadata);

  {
    // Load version information.
    QM_TRY_INSPECT(
        const auto& stmt,
        CreateAndExecuteSingleStepStatement<
            SingleStepResult::ReturnNullIfNoResult>(
            aConnection, "SELECT name, origin, version FROM database"_ns));

    QM_TRY(OkIf(stmt), NS_ERROR_FILE_CORRUPTED);

    QM_TRY_INSPECT(const auto& databaseName, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                                 nsString, stmt, GetString, 0));

    QM_TRY(OkIf(mCommonParams.metadata().name() == databaseName),
           NS_ERROR_FILE_CORRUPTED);

    QM_TRY_INSPECT(const auto& origin, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                           nsCString, stmt, GetUTF8String, 1));

    // We can't just compare these strings directly. See bug 1339081 comment 69.
    QM_TRY(OkIf(QuotaManager::AreOriginsEqualOnDisk(mOriginMetadata.mOrigin,
                                                    origin)),
           NS_ERROR_FILE_CORRUPTED);

    QM_TRY_INSPECT(const int64_t& version,
                   MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 2));

    mMetadata->mCommonMetadata.version() = uint64_t(version);
  }

  ObjectStoreTable& objectStores = mMetadata->mObjectStores;

  QM_TRY_INSPECT(
      const auto& lastObjectStoreId,
      ([&aConnection,
        &objectStores]() -> mozilla::Result<IndexOrObjectStoreId, nsresult> {
        // Load object store names and ids.
        QM_TRY_INSPECT(
            const auto& stmt,
            MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
                "SELECT id, auto_increment, name, key_path "
                "FROM object_store"_ns));

        IndexOrObjectStoreId lastObjectStoreId = 0;

        QM_TRY(CollectWhileHasResult(
            *stmt,
            [&lastObjectStoreId, &objectStores,
             usedIds = Maybe<nsTHashSet<uint64_t>>{},
             usedNames = Maybe<nsTHashSet<nsString>>{}](
                auto& stmt) mutable -> mozilla::Result<Ok, nsresult> {
              QM_TRY_INSPECT(const IndexOrObjectStoreId& objectStoreId,
                             MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 0));

              if (!usedIds) {
                usedIds.emplace();
              }

              QM_TRY(OkIf(objectStoreId > 0), Err(NS_ERROR_FILE_CORRUPTED));
              QM_TRY(OkIf(!usedIds.ref().Contains(objectStoreId)),
                     Err(NS_ERROR_FILE_CORRUPTED));

              QM_TRY(OkIf(usedIds.ref().Insert(objectStoreId, fallible)),
                     Err(NS_ERROR_OUT_OF_MEMORY));

              nsString name;
              QM_TRY(MOZ_TO_RESULT(stmt.GetString(2, name)));

              if (!usedNames) {
                usedNames.emplace();
              }

              QM_TRY(OkIf(!usedNames.ref().Contains(name)),
                     Err(NS_ERROR_FILE_CORRUPTED));

              QM_TRY(OkIf(usedNames.ref().Insert(name, fallible)),
                     Err(NS_ERROR_OUT_OF_MEMORY));

              ObjectStoreMetadata commonMetadata;
              commonMetadata.id() = objectStoreId;
              commonMetadata.name() = std::move(name);

              QM_TRY_INSPECT(
                  const int32_t& columnType,
                  MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetTypeOfIndex, 3));

              if (columnType == mozIStorageStatement::VALUE_TYPE_NULL) {
                commonMetadata.keyPath() = KeyPath(0);
              } else {
                MOZ_ASSERT(columnType == mozIStorageStatement::VALUE_TYPE_TEXT);

                nsString keyPathSerialization;
                QM_TRY(MOZ_TO_RESULT(stmt.GetString(3, keyPathSerialization)));

                commonMetadata.keyPath() =
                    KeyPath::DeserializeFromString(keyPathSerialization);
                QM_TRY(OkIf(commonMetadata.keyPath().IsValid()),
                       Err(NS_ERROR_FILE_CORRUPTED));
              }

              QM_TRY_INSPECT(const int64_t& nextAutoIncrementId,
                             MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 1));

              commonMetadata.autoIncrement() = !!nextAutoIncrementId;

              QM_TRY(OkIf(objectStores.InsertOrUpdate(
                         objectStoreId,
                         MakeSafeRefPtr<FullObjectStoreMetadata>(
                             std::move(commonMetadata),
                             FullObjectStoreMetadata::AutoIncrementIds{
                                 nextAutoIncrementId, nextAutoIncrementId}),
                         fallible)),
                     Err(NS_ERROR_OUT_OF_MEMORY));

              lastObjectStoreId = std::max(lastObjectStoreId, objectStoreId);

              return Ok{};
            }));

        return lastObjectStoreId;
      }()));

  QM_TRY_INSPECT(
      const auto& lastIndexId,
      ([this, &aConnection,
        &objectStores]() -> mozilla::Result<IndexOrObjectStoreId, nsresult> {
        // Load index information
        QM_TRY_INSPECT(
            const auto& stmt,
            MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
                "SELECT "
                "id, object_store_id, name, key_path, "
                "unique_index, multientry, "
                "locale, is_auto_locale "
                "FROM object_store_index"_ns));

        IndexOrObjectStoreId lastIndexId = 0;

        QM_TRY(CollectWhileHasResult(
            *stmt,
            [this, &lastIndexId, &objectStores, &aConnection,
             usedIds = Maybe<nsTHashSet<uint64_t>>{},
             usedNames = Maybe<nsTHashSet<nsString>>{}](
                auto& stmt) mutable -> mozilla::Result<Ok, nsresult> {
              QM_TRY_INSPECT(const IndexOrObjectStoreId& objectStoreId,
                             MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 1));

              // XXX Why does this return NS_ERROR_OUT_OF_MEMORY if we don't
              // know the object store id?

              auto objectStoreMetadata = objectStores.Lookup(objectStoreId);
              QM_TRY(OkIf(static_cast<bool>(objectStoreMetadata)),
                     Err(NS_ERROR_OUT_OF_MEMORY));

              MOZ_ASSERT((*objectStoreMetadata)->mCommonMetadata.id() ==
                         objectStoreId);

              IndexOrObjectStoreId indexId;
              QM_TRY(MOZ_TO_RESULT(stmt.GetInt64(0, &indexId)));

              if (!usedIds) {
                usedIds.emplace();
              }

              QM_TRY(OkIf(indexId > 0), Err(NS_ERROR_FILE_CORRUPTED));
              QM_TRY(OkIf(!usedIds.ref().Contains(indexId)),
                     Err(NS_ERROR_FILE_CORRUPTED));

              QM_TRY(OkIf(usedIds.ref().Insert(indexId, fallible)),
                     Err(NS_ERROR_OUT_OF_MEMORY));

              nsString name;
              QM_TRY(MOZ_TO_RESULT(stmt.GetString(2, name)));

              const nsAutoString hashName =
                  IntToString(indexId) + u":"_ns + name;

              if (!usedNames) {
                usedNames.emplace();
              }

              QM_TRY(OkIf(!usedNames.ref().Contains(hashName)),
                     Err(NS_ERROR_FILE_CORRUPTED));

              QM_TRY(OkIf(usedNames.ref().Insert(hashName, fallible)),
                     Err(NS_ERROR_OUT_OF_MEMORY));

              auto indexMetadata = MakeSafeRefPtr<FullIndexMetadata>();
              indexMetadata->mCommonMetadata.id() = indexId;
              indexMetadata->mCommonMetadata.name() = name;

#ifdef DEBUG
              {
                int32_t columnType;
                nsresult rv = stmt.GetTypeOfIndex(3, &columnType);
                MOZ_ASSERT(NS_SUCCEEDED(rv));
                MOZ_ASSERT(columnType != mozIStorageStatement::VALUE_TYPE_NULL);
              }
#endif

              nsString keyPathSerialization;
              QM_TRY(MOZ_TO_RESULT(stmt.GetString(3, keyPathSerialization)));

              indexMetadata->mCommonMetadata.keyPath() =
                  KeyPath::DeserializeFromString(keyPathSerialization);
              QM_TRY(OkIf(indexMetadata->mCommonMetadata.keyPath().IsValid()),
                     Err(NS_ERROR_FILE_CORRUPTED));

              int32_t scratch;
              QM_TRY(MOZ_TO_RESULT(stmt.GetInt32(4, &scratch)));

              indexMetadata->mCommonMetadata.unique() = !!scratch;

              QM_TRY(MOZ_TO_RESULT(stmt.GetInt32(5, &scratch)));

              indexMetadata->mCommonMetadata.multiEntry() = !!scratch;

              const bool localeAware = !stmt.IsNull(6);
              if (localeAware) {
                QM_TRY(MOZ_TO_RESULT(stmt.GetUTF8String(
                    6, indexMetadata->mCommonMetadata.locale())));

                QM_TRY(MOZ_TO_RESULT(stmt.GetInt32(7, &scratch)));

                indexMetadata->mCommonMetadata.autoLocale() = !!scratch;

                // Update locale-aware indexes if necessary
                const nsCString& indexedLocale =
                    indexMetadata->mCommonMetadata.locale();
                const bool& isAutoLocale =
                    indexMetadata->mCommonMetadata.autoLocale();
                const nsCString& systemLocale = mFactory->GetSystemLocale();
                if (!systemLocale.IsEmpty() && isAutoLocale &&
                    !indexedLocale.Equals(systemLocale)) {
                  QM_TRY(MOZ_TO_RESULT(UpdateLocaleAwareIndex(
                      aConnection, indexMetadata->mCommonMetadata,
                      systemLocale)));
                }
              }

              QM_TRY(OkIf((*objectStoreMetadata)
                              ->mIndexes.InsertOrUpdate(
                                  indexId, std::move(indexMetadata), fallible)),
                     Err(NS_ERROR_OUT_OF_MEMORY));

              lastIndexId = std::max(lastIndexId, indexId);

              return Ok{};
            }));

        return lastIndexId;
      }()));

  QM_TRY(OkIf(lastObjectStoreId != INT64_MAX),
         NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR, IDB_REPORT_INTERNAL_ERR_LAMBDA);
  QM_TRY(OkIf(lastIndexId != INT64_MAX), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR,
         IDB_REPORT_INTERNAL_ERR_LAMBDA);

  mMetadata->mNextObjectStoreId = lastObjectStoreId + 1;
  mMetadata->mNextIndexId = lastIndexId + 1;

  return NS_OK;
}

/* static */
nsresult OpenDatabaseOp::UpdateLocaleAwareIndex(
    mozIStorageConnection& aConnection, const IndexMetadata& aIndexMetadata,
    const nsCString& aLocale) {
  const auto indexTable =
      aIndexMetadata.unique() ? "unique_index_data"_ns : "index_data"_ns;

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  const nsCString readQuery = "SELECT value, object_data_key FROM "_ns +
                              indexTable + " WHERE index_id = :index_id"_ns;

  QM_TRY_INSPECT(const auto& readStmt,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConnection,
                     CreateStatement, readQuery));

  QM_TRY(MOZ_TO_RESULT(readStmt->BindInt64ByIndex(0, aIndexMetadata.id())));

  QM_TRY(CollectWhileHasResult(
      *readStmt,
      [&aConnection, &indexTable, &aIndexMetadata, &aLocale,
       writeStmt = nsCOMPtr<mozIStorageStatement>{}](
          auto& readStmt) mutable -> mozilla::Result<Ok, nsresult> {
        if (!writeStmt) {
          QM_TRY_UNWRAP(
              writeStmt,
              MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                  nsCOMPtr<mozIStorageStatement>, aConnection, CreateStatement,
                  "UPDATE "_ns + indexTable + "SET value_locale = :"_ns +
                      kStmtParamNameValueLocale + " WHERE index_id = :"_ns +
                      kStmtParamNameIndexId + " AND value = :"_ns +
                      kStmtParamNameValue + " AND object_data_key = :"_ns +
                      kStmtParamNameObjectDataKey));
        }

        mozStorageStatementScoper scoper(writeStmt);
        QM_TRY(MOZ_TO_RESULT(writeStmt->BindInt64ByName(kStmtParamNameIndexId,
                                                        aIndexMetadata.id())));

        Key oldKey, objectStorePosition;
        QM_TRY(MOZ_TO_RESULT(oldKey.SetFromStatement(&readStmt, 0)));
        QM_TRY(MOZ_TO_RESULT(
            oldKey.BindToStatement(writeStmt, kStmtParamNameValue)));

        QM_TRY_INSPECT(const auto& newSortKey,
                       oldKey.ToLocaleAwareKey(aLocale));

        QM_TRY(MOZ_TO_RESULT(
            newSortKey.BindToStatement(writeStmt, kStmtParamNameValueLocale)));
        QM_TRY(
            MOZ_TO_RESULT(objectStorePosition.SetFromStatement(&readStmt, 1)));
        QM_TRY(MOZ_TO_RESULT(objectStorePosition.BindToStatement(
            writeStmt, kStmtParamNameObjectDataKey)));

        QM_TRY(MOZ_TO_RESULT(writeStmt->Execute()));

        return Ok{};
      }));

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  static constexpr auto metaQuery =
      "UPDATE object_store_index SET "
      "locale = :locale WHERE id = :id"_ns;

  QM_TRY_INSPECT(const auto& metaStmt,
                 MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                     nsCOMPtr<mozIStorageStatement>, aConnection,
                     CreateStatement, metaQuery));

  QM_TRY(MOZ_TO_RESULT(
      metaStmt->BindStringByIndex(0, NS_ConvertASCIItoUTF16(aLocale))));

  QM_TRY(MOZ_TO_RESULT(metaStmt->BindInt64ByIndex(1, aIndexMetadata.id())));

  QM_TRY(MOZ_TO_RESULT(metaStmt->Execute()));

  return NS_OK;
}

nsresult OpenDatabaseOp::BeginVersionChange() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::BeginVersionChange);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());
  MOZ_ASSERT(mMetadata->mCommonMetadata.version() <= mRequestedVersion);
  MOZ_ASSERT(!mDatabase);
  MOZ_ASSERT(!mVersionChangeTransaction);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    IDB_REPORT_INTERNAL_ERR();
    QM_TRY(MOZ_TO_RESULT(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR));
  }

  EnsureDatabaseActor();

  if (mDatabase->IsInvalidated()) {
    IDB_REPORT_INTERNAL_ERR();
    QM_TRY(MOZ_TO_RESULT(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR));
  }

  MOZ_ASSERT(!mDatabase->IsClosed());

  DatabaseActorInfo* info;
  MOZ_ALWAYS_TRUE(gLiveDatabaseHashtable->Get(mDatabaseId.ref(), &info));

  MOZ_ASSERT(info->mLiveDatabases.Contains(mDatabase.unsafeGetRawPtr()));
  MOZ_ASSERT(!info->mWaitingFactoryOp);
  MOZ_ASSERT(info->mMetadata == mMetadata);

  auto transaction = MakeSafeRefPtr<VersionChangeTransaction>(this);

  if (NS_WARN_IF(!transaction->CopyDatabaseMetadata())) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  MOZ_ASSERT(info->mMetadata != mMetadata);
  mMetadata = info->mMetadata.clonePtr();

  const Maybe<uint64_t> newVersion = Some(mRequestedVersion);

  QM_TRY(MOZ_TO_RESULT(SendVersionChangeMessages(
      info, mDatabase.maybeDeref(), mMetadata->mCommonMetadata.version(),
      newVersion)));

  mVersionChangeTransaction = std::move(transaction);

  if (mMaybeBlockedDatabases.IsEmpty()) {
    // We don't need to wait on any databases, just jump to the transaction
    // pool.
    WaitForTransactions();
    return NS_OK;
  }

  // If the actor gets destroyed, mWaitingFactoryOp will hold the last strong
  // reference to us.
  info->mWaitingFactoryOp = this;

  mState = State::WaitingForOtherDatabasesToClose;
  return NS_OK;
}

bool OpenDatabaseOp::AreActorsAlive() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mDatabase);

  return !(IsActorDestroyed() || mDatabase->IsActorDestroyed());
}

void OpenDatabaseOp::SendBlockedNotification() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::WaitingForOtherDatabasesToClose);

  if (!IsActorDestroyed()) {
    Unused << SendBlocked(mMetadata->mCommonMetadata.version());
  }
}

nsresult OpenDatabaseOp::DispatchToWorkThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::WaitingForTransactionsToComplete);
  MOZ_ASSERT(mVersionChangeTransaction);
  MOZ_ASSERT(mVersionChangeTransaction->GetMode() ==
             IDBTransaction::Mode::VersionChange);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed() || mDatabase->IsInvalidated()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  mState = State::DatabaseWorkVersionChange;

  // Intentionally empty.
  nsTArray<nsString> objectStoreNames;

  const int64_t loggingSerialNumber =
      mVersionChangeTransaction->LoggingSerialNumber();
  const nsID& backgroundChildLoggingId =
      mVersionChangeTransaction->GetLoggingInfo()->Id();

  if (NS_WARN_IF(!mDatabase->RegisterTransaction(*mVersionChangeTransaction))) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  if (!gConnectionPool) {
    gConnectionPool = new ConnectionPool();
  }

  RefPtr<VersionChangeOp> versionChangeOp = new VersionChangeOp(this);

  uint64_t transactionId = versionChangeOp->StartOnConnectionPool(
      backgroundChildLoggingId, mVersionChangeTransaction->DatabaseId(),
      loggingSerialNumber, objectStoreNames,
      /* aIsWriteTransaction */ true);

  mVersionChangeOp = versionChangeOp;

  mVersionChangeTransaction->NoteActiveRequest();
  mVersionChangeTransaction->Init(transactionId);

  return NS_OK;
}

nsresult OpenDatabaseOp::SendUpgradeNeeded() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DatabaseWorkVersionChange);
  MOZ_ASSERT(mVersionChangeTransaction);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());
  MOZ_ASSERT(!HasFailed());
  MOZ_ASSERT_IF(!IsActorDestroyed(), mDatabase);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  const SafeRefPtr<VersionChangeTransaction> transaction =
      std::move(mVersionChangeTransaction);

  nsresult rv = EnsureDatabaseActorIsAlive();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // Transfer ownership to IPDL.
  transaction->SetActorAlive();

  if (!mDatabase->SendPBackgroundIDBVersionChangeTransactionConstructor(
          transaction.unsafeGetRawPtr(), mMetadata->mCommonMetadata.version(),
          mRequestedVersion, mMetadata->mNextObjectStoreId,
          mMetadata->mNextIndexId)) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  return NS_OK;
}

nsresult OpenDatabaseOp::DoVersionUpdate() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DatabaseWorkVersionUpdate);
  MOZ_ASSERT(!HasFailed());

  AUTO_PROFILER_LABEL("OpenDatabaseOp::DoVersionUpdate", DOM);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !OperationMayProceed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  mFileManager->UpdateDatabaseVersion(mRequestedVersion);

  mState = State::SendingResults;

  QM_TRY(MOZ_TO_RESULT(
      DispatchThisAfterProcessingCurrentEvent(mOwningEventTarget)));

  return NS_OK;
}

void OpenDatabaseOp::SendResults() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());
  MOZ_ASSERT_IF(!HasFailed(), !mVersionChangeTransaction);

  if (mCompleteCallback) {
    auto completeCallback = std::move(mCompleteCallback);
    completeCallback();
  }

  DebugOnly<DatabaseActorInfo*> info = nullptr;
  MOZ_ASSERT_IF(mDatabaseId.isSome() && gLiveDatabaseHashtable &&
                    gLiveDatabaseHashtable->Get(mDatabaseId.ref(), &info),
                !info->mWaitingFactoryOp);

  if (mVersionChangeTransaction) {
    MOZ_ASSERT(HasFailed());

    mVersionChangeTransaction->Abort(ResultCode(), /* aForce */ true);
    mVersionChangeTransaction = nullptr;
  }

  if (IsActorDestroyed()) {
    SetFailureCodeIfUnset(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  } else {
    FactoryRequestResponse response;

    if (!HasFailed()) {
      // If we just successfully completed a versionchange operation then we
      // need to update the version in our metadata.
      mMetadata->mCommonMetadata.version() = mRequestedVersion;

      nsresult rv = EnsureDatabaseActorIsAlive();
      if (NS_SUCCEEDED(rv)) {
        // We successfully opened a database so use its actor as the success
        // result for this request.

        // XXX OpenDatabaseRequestResponse stores a raw pointer, can this be
        // avoided?
        response = OpenDatabaseRequestResponse{
            WrapNotNull(mDatabase.unsafeGetRawPtr())};
      } else {
        response = ClampResultCode(rv);
#ifdef DEBUG
        SetFailureCode(response.get_nsresult());
#endif
      }
    } else {
#ifdef DEBUG
      // If something failed then our metadata pointer is now bad. No one should
      // ever touch it again though so just null it out in DEBUG builds to make
      // sure we find such cases.
      mMetadata = nullptr;
#endif
      response = ClampResultCode(ResultCode());
    }

    Unused << PBackgroundIDBFactoryRequestParent::Send__delete__(this,
                                                                 response);
  }

  if (mDatabase) {
    MOZ_ASSERT(!mDirectoryLockHandle);

    if (HasFailed()) {
      mDatabase->Invalidate();
    }

    // Make sure to release the database on this thread.
    mDatabase = nullptr;

    CleanupMetadata();
  } else if (mDirectoryLockHandle) {
    // ConnectionClosedCallback will call CleanupMetadata().
    nsCOMPtr<nsIRunnable> callback = NewRunnableMethod(
        "dom::indexedDB::OpenDatabaseOp::ConnectionClosedCallback", this,
        &OpenDatabaseOp::ConnectionClosedCallback);

    RefPtr<WaitForTransactionsHelper> helper =
        new WaitForTransactionsHelper(mDatabaseId.ref(), callback);
    helper->WaitForTransactions();
  } else {
    CleanupMetadata();
  }

  FinishSendResults();
}

void OpenDatabaseOp::ConnectionClosedCallback() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(HasFailed());
  MOZ_ASSERT(mDirectoryLockHandle);

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  CleanupMetadata();
}

void OpenDatabaseOp::EnsureDatabaseActor() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::BeginVersionChange ||
             mState == State::DatabaseWorkVersionChange ||
             mState == State::SendingResults);
  MOZ_ASSERT(!HasFailed());
  MOZ_ASSERT(mDatabaseFilePath.isSome());
  MOZ_ASSERT(!IsActorDestroyed());

  if (mDatabase) {
    return;
  }

  MOZ_ASSERT(mMetadata->mDatabaseId.IsEmpty());
  mMetadata->mDatabaseId = mDatabaseId.ref();

  MOZ_ASSERT(mMetadata->mFilePath.IsEmpty());
  mMetadata->mFilePath = mDatabaseFilePath.ref();

  DatabaseActorInfo* info;
  if (gLiveDatabaseHashtable->Get(mDatabaseId.ref(), &info)) {
    AssertMetadataConsistency(*info->mMetadata);
    mMetadata = info->mMetadata.clonePtr();
  }

  Maybe<const CipherKey> maybeKey =
      mInPrivateBrowsing ? mFileManager->MutableCipherKeyManagerRef().Get()
                         : Nothing();

  MOZ_RELEASE_ASSERT(mInPrivateBrowsing == maybeKey.isSome());

  const bool directoryLockInvalidated = mDirectoryLockHandle->Invalidated();

  // XXX Shouldn't Manager() return already_AddRefed when
  // PBackgroundIDBFactoryParent is declared refcounted?
  mDatabase = MakeSafeRefPtr<Database>(
      SafeRefPtr{static_cast<Factory*>(Manager()),
                 AcquireStrongRefFromRawPtr{}},
      mCommonParams.principalInfo(), mContentParentId, mOriginMetadata,
      mTelemetryId, mMetadata.clonePtr(), mFileManager.clonePtr(),
      std::move(mDirectoryLockHandle), mInPrivateBrowsing, maybeKey);

  if (info) {
    info->mLiveDatabases.AppendElement(
        WrapNotNullUnchecked(mDatabase.unsafeGetRawPtr()));
  } else {
    // XXX Maybe use LookupOrInsertWith above, to avoid a second lookup here?
    info = gLiveDatabaseHashtable
               ->InsertOrUpdate(
                   mDatabaseId.ref(),
                   MakeUnique<DatabaseActorInfo>(
                       mMetadata.clonePtr(),
                       WrapNotNullUnchecked(mDatabase.unsafeGetRawPtr())))
               .get();
  }

  if (directoryLockInvalidated) {
    mDatabase->Invalidate();
  }

  // Balanced in Database::CleanupMetadata().
  IncreaseBusyCount();
}

nsresult OpenDatabaseOp::EnsureDatabaseActorIsAlive() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DatabaseWorkVersionChange ||
             mState == State::SendingResults);
  MOZ_ASSERT(!HasFailed());
  MOZ_ASSERT(!IsActorDestroyed());

  EnsureDatabaseActor();

  if (mDatabase->IsActorAlive()) {
    return NS_OK;
  }

  auto* const factory = static_cast<Factory*>(Manager());

  QM_TRY_INSPECT(const auto& spec, MetadataToSpec());

  mDatabase->SetActorAlive();

  if (!factory->SendPBackgroundIDBDatabaseConstructor(
          mDatabase.unsafeGetRawPtr(), spec, WrapNotNull(this))) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  if (mDatabase->IsInvalidated()) {
    Unused << mDatabase->SendInvalidate();
  }

  return NS_OK;
}

Result<DatabaseSpec, nsresult> OpenDatabaseOp::MetadataToSpec() const {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mMetadata);

  DatabaseSpec spec;
  spec.metadata() = mMetadata->mCommonMetadata;

  QM_TRY_UNWRAP(spec.objectStores(),
                TransformIntoNewArrayAbortOnErr(
                    mMetadata->mObjectStores,
                    [](const auto& objectStoreEntry)
                        -> mozilla::Result<ObjectStoreSpec, nsresult> {
                      FullObjectStoreMetadata* metadata =
                          objectStoreEntry.GetWeak();
                      MOZ_ASSERT(objectStoreEntry.GetKey());
                      MOZ_ASSERT(metadata);

                      ObjectStoreSpec objectStoreSpec;
                      objectStoreSpec.metadata() = metadata->mCommonMetadata;

                      QM_TRY_UNWRAP(auto indexes,
                                    TransformIntoNewArray(
                                        metadata->mIndexes,
                                        [](const auto& indexEntry) {
                                          FullIndexMetadata* indexMetadata =
                                              indexEntry.GetWeak();
                                          MOZ_ASSERT(indexEntry.GetKey());
                                          MOZ_ASSERT(indexMetadata);

                                          return indexMetadata->mCommonMetadata;
                                        },
                                        fallible));

                      objectStoreSpec.indexes() = std::move(indexes);

                      return objectStoreSpec;
                    },
                    fallible));

  return spec;
}

#ifdef DEBUG

void OpenDatabaseOp::AssertMetadataConsistency(
    const FullDatabaseMetadata& aMetadata) {
  AssertIsOnBackgroundThread();

  const FullDatabaseMetadata& thisDB = *mMetadata;
  const FullDatabaseMetadata& otherDB = aMetadata;

  MOZ_ASSERT(&thisDB != &otherDB);

  MOZ_ASSERT(thisDB.mCommonMetadata.name() == otherDB.mCommonMetadata.name());
  MOZ_ASSERT(thisDB.mCommonMetadata.version() ==
             otherDB.mCommonMetadata.version());
  MOZ_ASSERT(thisDB.mCommonMetadata.persistenceType() ==
             otherDB.mCommonMetadata.persistenceType());
  MOZ_ASSERT(thisDB.mDatabaseId == otherDB.mDatabaseId);
  MOZ_ASSERT(thisDB.mFilePath == otherDB.mFilePath);

  // |thisDB| reflects the latest objectStore and index ids that have committed
  // to disk. The in-memory metadata |otherDB| keeps track of objectStores and
  // indexes that were created and then removed as well, so the next ids for
  // |otherDB| may be higher than for |thisDB|.
  MOZ_ASSERT(thisDB.mNextObjectStoreId <= otherDB.mNextObjectStoreId);
  MOZ_ASSERT(thisDB.mNextIndexId <= otherDB.mNextIndexId);

  MOZ_ASSERT(thisDB.mObjectStores.Count() == otherDB.mObjectStores.Count());

  for (const auto& thisObjectStore : thisDB.mObjectStores.Values()) {
    MOZ_ASSERT(thisObjectStore);
    MOZ_ASSERT(!thisObjectStore->mDeleted);

    auto otherObjectStore = MatchMetadataNameOrId(
        otherDB.mObjectStores, thisObjectStore->mCommonMetadata.id());
    MOZ_ASSERT(otherObjectStore);

    MOZ_ASSERT(thisObjectStore != &otherObjectStore.ref());

    MOZ_ASSERT(thisObjectStore->mCommonMetadata.id() ==
               otherObjectStore->mCommonMetadata.id());
    MOZ_ASSERT(thisObjectStore->mCommonMetadata.name() ==
               otherObjectStore->mCommonMetadata.name());
    MOZ_ASSERT(thisObjectStore->mCommonMetadata.autoIncrement() ==
               otherObjectStore->mCommonMetadata.autoIncrement());
    MOZ_ASSERT(thisObjectStore->mCommonMetadata.keyPath() ==
               otherObjectStore->mCommonMetadata.keyPath());
    // mNextAutoIncrementId and mCommittedAutoIncrementId may be modified
    // concurrently with this OpenOp, so it is not possible to assert equality
    // here. It's also possible that we've written the new ids to disk but not
    // yet updated the in-memory count.
    // TODO The first part of the comment should probably be rephrased. I think
    // it still applies but it sounds as if this were thread-unsafe like it was
    // before, which isn't true anymore.
    {
      const auto&& thisAutoIncrementIds =
          thisObjectStore->mAutoIncrementIds.Lock();
      const auto&& otherAutoIncrementIds =
          otherObjectStore->mAutoIncrementIds.Lock();

      MOZ_ASSERT(thisAutoIncrementIds->next <= otherAutoIncrementIds->next);
      MOZ_ASSERT(
          thisAutoIncrementIds->committed <= otherAutoIncrementIds->committed ||
          thisAutoIncrementIds->committed == otherAutoIncrementIds->next);
    }
    MOZ_ASSERT(!otherObjectStore->mDeleted);

    MOZ_ASSERT(thisObjectStore->mIndexes.Count() ==
               otherObjectStore->mIndexes.Count());

    for (const auto& thisIndex : thisObjectStore->mIndexes.Values()) {
      MOZ_ASSERT(thisIndex);
      MOZ_ASSERT(!thisIndex->mDeleted);

      auto otherIndex = MatchMetadataNameOrId(otherObjectStore->mIndexes,
                                              thisIndex->mCommonMetadata.id());
      MOZ_ASSERT(otherIndex);

      MOZ_ASSERT(thisIndex != &otherIndex.ref());

      MOZ_ASSERT(thisIndex->mCommonMetadata.id() ==
                 otherIndex->mCommonMetadata.id());
      MOZ_ASSERT(thisIndex->mCommonMetadata.name() ==
                 otherIndex->mCommonMetadata.name());
      MOZ_ASSERT(thisIndex->mCommonMetadata.keyPath() ==
                 otherIndex->mCommonMetadata.keyPath());
      MOZ_ASSERT(thisIndex->mCommonMetadata.unique() ==
                 otherIndex->mCommonMetadata.unique());
      MOZ_ASSERT(thisIndex->mCommonMetadata.multiEntry() ==
                 otherIndex->mCommonMetadata.multiEntry());
      MOZ_ASSERT(!otherIndex->mDeleted);
    }
  }
}

#endif  // DEBUG

nsresult OpenDatabaseOp::VersionChangeOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mOpenDatabaseOp);
  MOZ_ASSERT(mOpenDatabaseOp->mState == State::DatabaseWorkVersionChange);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !OperationMayProceed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  AUTO_PROFILER_LABEL("OpenDatabaseOp::VersionChangeOp::DoDatabaseWork", DOM);

  IDB_LOG_MARK_PARENT_TRANSACTION("Beginning database work", "DB Start",
                                  IDB_LOG_ID_STRING(mBackgroundChildLoggingId),
                                  mTransactionLoggingSerialNumber);

  Transaction().SetActiveOnConnectionThread();

  QM_TRY(MOZ_TO_RESULT(
      aConnection->BeginWriteTransaction(Transaction().GetDurability())));

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "UPDATE database SET version = :version;"_ns,
      ([&self = *this](
           mozIStorageStatement& updateStmt) -> mozilla::Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(
            updateStmt.BindInt64ByIndex(0, int64_t(self.mRequestedVersion))));

        return Ok{};
      }))));

  return NS_OK;
}

nsresult OpenDatabaseOp::VersionChangeOp::SendSuccessResult() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mOpenDatabaseOp);
  MOZ_ASSERT(mOpenDatabaseOp->mState == State::DatabaseWorkVersionChange);
  MOZ_ASSERT(mOpenDatabaseOp->mVersionChangeOp == this);

  nsresult rv = mOpenDatabaseOp->SendUpgradeNeeded();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

bool OpenDatabaseOp::VersionChangeOp::SendFailureResult(nsresult aResultCode) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mOpenDatabaseOp);
  MOZ_ASSERT(mOpenDatabaseOp->mState == State::DatabaseWorkVersionChange);
  MOZ_ASSERT(mOpenDatabaseOp->mVersionChangeOp == this);

  mOpenDatabaseOp->SetFailureCode(aResultCode);
  mOpenDatabaseOp->mState = State::SendingResults;

  MOZ_ALWAYS_SUCCEEDS(mOpenDatabaseOp->Run());

  return false;
}

void OpenDatabaseOp::VersionChangeOp::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mOpenDatabaseOp);
  MOZ_ASSERT(mOpenDatabaseOp->mVersionChangeOp == this);

  mOpenDatabaseOp->mVersionChangeOp = nullptr;
  mOpenDatabaseOp = nullptr;

#ifdef DEBUG
  // A bit hacky but the VersionChangeOp is not generated in response to a
  // child request like most other database operations. Do this to make our
  // assertions happy.
  //
  // XXX: Depending on timing, in most cases, NoteActorDestroyed will not have
  // been destroyed before, but in some cases it has. This should be reworked in
  // a way this hack is not necessary. There are also several similar cases in
  // other *Op classes.
  if (!IsActorDestroyed()) {
    NoteActorDestroyed();
  }
#endif

  TransactionDatabaseOperationBase::Cleanup();
}

void DeleteDatabaseOp::LoadPreviousVersion(nsIFile& aDatabaseFile) {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DatabaseWorkOpen);
  MOZ_ASSERT(!mPreviousVersion);

  AUTO_PROFILER_LABEL("DeleteDatabaseOp::LoadPreviousVersion", DOM);

  nsresult rv;

  nsCOMPtr<mozIStorageService> ss =
      do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  IndexedDatabaseManager* const idm = IndexedDatabaseManager::Get();
  MOZ_ASSERT(idm);

  const PersistenceType persistenceType =
      mCommonParams.metadata().persistenceType();
  const nsAString& databaseName = mCommonParams.metadata().name();

  SafeRefPtr<DatabaseFileManager> fileManager = idm->GetFileManager(
      persistenceType, mOriginMetadata.mOrigin, databaseName);

  if (!fileManager) {
    fileManager = MakeSafeRefPtr<DatabaseFileManager>(
        persistenceType, mOriginMetadata, databaseName, mDatabaseId.ref(),
        mDatabaseFilePath.ref(), mEnforcingQuota, mInPrivateBrowsing);
  }

  const auto maybeKey =
      mInPrivateBrowsing
          ? Some(fileManager->MutableCipherKeyManagerRef().Ensure())
          : Nothing();

  MOZ_RELEASE_ASSERT(mInPrivateBrowsing == maybeKey.isSome());

  // Pass -1 as the directoryLockId to disable quota checking, since we might
  // temporarily exceed quota before deleting the database.
  QM_TRY_INSPECT(const auto& dbFileUrl,
                 GetDatabaseFileURL(aDatabaseFile, -1, maybeKey), QM_VOID);

  QM_TRY_UNWRAP(const NotNull<nsCOMPtr<mozIStorageConnection>> connection,
                OpenDatabaseAndHandleBusy(*ss, *dbFileUrl), QM_VOID);

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const auto& stmt,
                   CreateAndExecuteSingleStepStatement<
                       SingleStepResult::ReturnNullIfNoResult>(
                       *connection, "SELECT name FROM database"_ns),
                   QM_VOID);

    QM_TRY(OkIf(stmt), QM_VOID);

    nsString databaseName;
    rv = stmt->GetString(0, databaseName);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    MOZ_ASSERT(mCommonParams.metadata().name() == databaseName);
  }
#endif

  QM_TRY_INSPECT(const auto& stmt,
                 CreateAndExecuteSingleStepStatement<
                     SingleStepResult::ReturnNullIfNoResult>(
                     *connection, "SELECT version FROM database"_ns),
                 QM_VOID);

  QM_TRY(OkIf(stmt), QM_VOID);

  int64_t version;
  rv = stmt->GetInt64(0, &version);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  mPreviousVersion = uint64_t(version);
}

nsresult DeleteDatabaseOp::DatabaseOpen() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DatabaseOpenPending);

  nsresult rv = SendToIOThread();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

nsresult DeleteDatabaseOp::DoDatabaseWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DatabaseWorkOpen);
  MOZ_ASSERT(mOriginMetadata.mPersistenceType ==
             mCommonParams.metadata().persistenceType());

  AUTO_PROFILER_LABEL("DeleteDatabaseOp::DoDatabaseWork", DOM);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !OperationMayProceed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  const nsAString& databaseName = mCommonParams.metadata().name();

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_UNWRAP(auto directory,
                quotaManager->GetOriginDirectory(mOriginMetadata));

  QM_TRY(MOZ_TO_RESULT(
      directory->Append(NS_LITERAL_STRING_FROM_CSTRING(IDB_DIRECTORY_NAME))));

  QM_TRY_UNWRAP(mDatabaseDirectoryPath, MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                                            nsString, directory, GetPath));

  mDatabaseFilenameBase =
      GetDatabaseFilenameBase(databaseName, mOriginMetadata.mIsPrivate);

  QM_TRY_INSPECT(
      const auto& dbFile,
      CloneFileAndAppend(*directory, mDatabaseFilenameBase + kSQLiteSuffix));

#ifdef DEBUG
  {
    QM_TRY_INSPECT(
        const auto& databaseFilePath,
        MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, dbFile, GetPath));

    MOZ_ASSERT(databaseFilePath == mDatabaseFilePath.ref());
  }
#endif

  QM_TRY_INSPECT(const bool& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(dbFile, Exists));

  if (exists) {
    // Parts of this function may fail but that shouldn't prevent us from
    // deleting the file eventually.
    LoadPreviousVersion(*dbFile);

    mState = State::BeginVersionChange;
  } else {
    mState = State::SendingResults;
  }

  QM_TRY(MOZ_TO_RESULT(
      DispatchThisAfterProcessingCurrentEvent(mOwningEventTarget)));

  return NS_OK;
}

nsresult DeleteDatabaseOp::BeginVersionChange() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::BeginVersionChange);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    IDB_REPORT_INTERNAL_ERR();
    QM_TRY(MOZ_TO_RESULT(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR));
  }

  DatabaseActorInfo* info;
  if (gLiveDatabaseHashtable->Get(mDatabaseId.ref(), &info)) {
    MOZ_ASSERT(!info->mWaitingFactoryOp);

    nsresult rv =
        SendVersionChangeMessages(info, Nothing(), mPreviousVersion, Nothing());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (!mMaybeBlockedDatabases.IsEmpty()) {
      // If the actor gets destroyed, mWaitingFactoryOp will hold the last
      // strong reference to us.
      info->mWaitingFactoryOp = this;

      mState = State::WaitingForOtherDatabasesToClose;
      return NS_OK;
    }
  }

  // No other databases need to be notified, just make sure that all
  // transactions are complete.
  WaitForTransactions();
  return NS_OK;
}

bool DeleteDatabaseOp::AreActorsAlive() {
  AssertIsOnOwningThread();

  return !IsActorDestroyed();
}

nsresult DeleteDatabaseOp::DispatchToWorkThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::WaitingForTransactionsToComplete);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnBackgroundThread()) ||
      IsActorDestroyed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  mState = State::DatabaseWorkVersionChange;

  RefPtr<VersionChangeOp> versionChangeOp = new VersionChangeOp(this);

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  nsresult rv = quotaManager->IOThread()->Dispatch(versionChangeOp.forget(),
                                                   NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  return NS_OK;
}

void DeleteDatabaseOp::SendBlockedNotification() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::WaitingForOtherDatabasesToClose);

  if (!IsActorDestroyed()) {
    Unused << SendBlocked(mPreviousVersion);
  }
}

nsresult DeleteDatabaseOp::DoVersionUpdate() {
  MOZ_CRASH("Not implemented because this should be unreachable.");
}

void DeleteDatabaseOp::SendResults() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);
  MOZ_ASSERT(mMaybeBlockedDatabases.IsEmpty());

  DebugOnly<DatabaseActorInfo*> info = nullptr;
  MOZ_ASSERT_IF(mDatabaseId.isSome() && gLiveDatabaseHashtable &&
                    gLiveDatabaseHashtable->Get(mDatabaseId.ref(), &info),
                !info->mWaitingFactoryOp);

  if (!IsActorDestroyed()) {
    FactoryRequestResponse response;

    if (!HasFailed()) {
      response = DeleteDatabaseRequestResponse(mPreviousVersion);
    } else {
      response = ClampResultCode(ResultCode());
    }

    Unused << PBackgroundIDBFactoryRequestParent::Send__delete__(this,
                                                                 response);
  }

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  CleanupMetadata();

  FinishSendResults();
}

nsresult DeleteDatabaseOp::VersionChangeOp::RunOnIOThread() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mDeleteDatabaseOp->mState == State::DatabaseWorkVersionChange);

  AUTO_PROFILER_LABEL("DeleteDatabaseOp::VersionChangeOp::RunOnIOThread", DOM);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !OperationMayProceed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  const PersistenceType& persistenceType =
      mDeleteDatabaseOp->mCommonParams.metadata().persistenceType();

  QuotaManager* quotaManager =
      mDeleteDatabaseOp->mEnforcingQuota ? QuotaManager::Get() : nullptr;

  MOZ_ASSERT_IF(mDeleteDatabaseOp->mEnforcingQuota, quotaManager);

  nsCOMPtr<nsIFile> directory =
      GetFileForPath(mDeleteDatabaseOp->mDatabaseDirectoryPath);
  if (NS_WARN_IF(!directory)) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  nsresult rv = RemoveDatabaseFilesAndDirectory(
      *directory, mDeleteDatabaseOp->mDatabaseFilenameBase, quotaManager,
      persistenceType, mDeleteDatabaseOp->mOriginMetadata,
      mDeleteDatabaseOp->mCommonParams.metadata().name());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void DeleteDatabaseOp::VersionChangeOp::RunOnOwningThread() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mDeleteDatabaseOp->mState == State::DatabaseWorkVersionChange);

  const RefPtr<DeleteDatabaseOp> deleteOp = std::move(mDeleteDatabaseOp);

  if (deleteOp->IsActorDestroyed()) {
    IDB_REPORT_INTERNAL_ERR();
    deleteOp->SetFailureCode(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  } else if (HasFailed()) {
    deleteOp->SetFailureCodeIfUnset(ResultCode());
  } else {
    DatabaseActorInfo* info;

    // Inform all the other databases that they are now invalidated. That
    // should remove the previous metadata from our table.
    if (gLiveDatabaseHashtable->Get(deleteOp->mDatabaseId.ref(), &info)) {
      MOZ_ASSERT(!info->mLiveDatabases.IsEmpty());
      MOZ_ASSERT(!info->mWaitingFactoryOp);

      nsTArray<SafeRefPtr<Database>> liveDatabases;
      if (NS_WARN_IF(!liveDatabases.SetCapacity(info->mLiveDatabases.Length(),
                                                fallible))) {
        deleteOp->SetFailureCode(NS_ERROR_OUT_OF_MEMORY);
      } else {
        std::transform(info->mLiveDatabases.cbegin(),
                       info->mLiveDatabases.cend(),
                       MakeBackInserter(liveDatabases),
                       [](const auto& aDatabase) -> SafeRefPtr<Database> {
                         return {aDatabase.get(), AcquireStrongRefFromRawPtr{}};
                       });

#ifdef DEBUG
        // The code below should result in the deletion of |info|. Set to null
        // here to make sure we find invalid uses later.
        info = nullptr;
#endif

        for (const auto& database : liveDatabases) {
          database->Invalidate();
        }

        MOZ_ASSERT(!gLiveDatabaseHashtable->Get(deleteOp->mDatabaseId.ref()));
      }
    }
  }

  // We hold a strong ref to the deleteOp, so it's safe to call Run() directly.

  deleteOp->mState = State::SendingResults;
  MOZ_ALWAYS_SUCCEEDS(deleteOp->Run());

#ifdef DEBUG
  // A bit hacky but the DeleteDatabaseOp::VersionChangeOp is not really a
  // normal database operation that is tied to an actor. Do this to make our
  // assertions happy.
  NoteActorDestroyed();
#endif
}

nsresult DeleteDatabaseOp::VersionChangeOp::Run() {
  nsresult rv;

  if (IsOnIOThread()) {
    rv = RunOnIOThread();
  } else {
    RunOnOwningThread();
    rv = NS_OK;
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    SetFailureCodeIfUnset(rv);

    MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL));
  }

  return NS_OK;
}

nsresult GetDatabasesOp::DatabasesNotAvailable() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DatabaseWorkOpen);

  mState = State::SendingResults;

  QM_TRY(MOZ_TO_RESULT(
      DispatchThisAfterProcessingCurrentEvent(mOwningEventTarget)));

  return NS_OK;
}

nsresult GetDatabasesOp::DoDirectoryWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DirectoryWorkOpen);

  // This state (DirectoryWorkOpen) runs immediately on the I/O thread, before
  // waiting for existing factory operations to complete (at which point
  // DoDatabaseWork will be invoked). To match the spec, we must snapshot the
  // current state of any databases that are being created (version = 0) or
  // upgraded (version >= 1) now. If we only sampled these values in
  // DoDatabaseWork, we would only see their post-creation/post-upgrade
  // versions, which would be incorrect.

  IndexedDatabaseManager* const idm = IndexedDatabaseManager::Get();
  MOZ_ASSERT(idm);

  const auto& fileManagers =
      idm->GetFileManagers(mPersistenceType, mOriginMetadata.mOrigin);

  for (const auto& fileManager : fileManagers) {
    auto& metadata =
        mDatabaseMetadataTable.LookupOrInsert(fileManager->DatabaseFilePath());
    metadata.name() = fileManager->DatabaseName();
    metadata.version() = fileManager->DatabaseVersion();
  }

  // Must set this before dispatching otherwise we will race with the IO thread.
  mState = State::DirectoryWorkDone;

  QM_TRY(MOZ_TO_RESULT(mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL)));

  return NS_OK;
}

nsresult GetDatabasesOp::DatabaseOpen() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::DatabaseOpenPending);

  nsresult rv = SendToIOThread();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

nsresult GetDatabasesOp::DoDatabaseWork() {
  AssertIsOnIOThread();
  MOZ_ASSERT(mState == State::DatabaseWorkOpen);

  AUTO_PROFILER_LABEL("GetDatabasesOp::DoDatabaseWork", DOM);

  if (NS_WARN_IF(QuotaClient::IsShuttingDownOnNonBackgroundThread()) ||
      !OperationMayProceed()) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  QuotaManager* const quotaManager = QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  {
    QM_TRY_INSPECT(const bool& exists,
                   quotaManager->DoesOriginDirectoryExist(mOriginMetadata));
    if (!exists) {
      return DatabasesNotAvailable();
    }
  }

  // XXX Is this really needed ?
  QM_TRY(([&quotaManager,
           this]() -> mozilla::Result<nsCOMPtr<nsIFile>, nsresult> {
    if (mPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
      QM_TRY_RETURN(quotaManager->GetOriginDirectory(mOriginMetadata));
    }

    QM_TRY_RETURN(
        quotaManager->GetOrCreateTemporaryOriginDirectory(mOriginMetadata));
  }()
                          .map([](const auto& res) { return Ok{}; })));

  {
    QM_TRY_INSPECT(const bool& exists,
                   quotaManager->DoesClientDirectoryExist(
                       ClientMetadata{mOriginMetadata, Client::IDB}));
    if (!exists) {
      return DatabasesNotAvailable();
    }
  }

  QM_TRY_INSPECT(
      const auto& clientDirectory,
      ([&quotaManager, this]()
           -> mozilla::Result<std::pair<nsCOMPtr<nsIFile>, bool>, nsresult> {
        if (mPersistenceType == PERSISTENCE_TYPE_PERSISTENT) {
          QM_TRY_RETURN(quotaManager->EnsurePersistentClientIsInitialized(
              ClientMetadata{mOriginMetadata, Client::IDB}));
        }

        QM_TRY_RETURN(quotaManager->EnsureTemporaryClientIsInitialized(
            ClientMetadata{mOriginMetadata, Client::IDB}));
      }()
                  .map([](const auto& res) { return res.first; })));

  QM_TRY_INSPECT(
      (const auto& [subdirsToProcess, databaseFilenames]),
      QuotaClient::GetDatabaseFilenames(*clientDirectory,
                                        /* aCanceled */ Atomic<bool>{false}));

  for (const auto& databaseFilename : databaseFilenames) {
    QM_TRY_INSPECT(
        const auto& databaseFile,
        CloneFileAndAppend(*clientDirectory, databaseFilename + kSQLiteSuffix));

    nsString path;
    databaseFile->GetPath(path);

    // Use the snapshotted values from DoDirectoryWork which correctly
    // snapshotted the state of any pending creations/upgrades. This does mean
    // that we need to skip reporting databases that had a version of 0 at that
    // time because they were still being created. In the event that any other
    // creation or upgrade requests are made after our operation is created,
    // this operation will block those, so it's not possible for this set of
    // data to get out of sync. The snapshotting (using cached database name
    // and version in DatabaseFileManager) also guarantees that we are not
    // touching the SQLite database here on the QuotaManager I/O thread which
    // is already open on the connection thread.

    auto metadata = mDatabaseMetadataTable.Lookup(path);
    if (metadata) {
      if (metadata->version() != 0) {
        mDatabaseMetadataArray.AppendElement(DatabaseMetadata(
            metadata->name(), metadata->version(), mPersistenceType));
      }

      continue;
    }

    // Since the database is not already open (there was no DatabaseFileManager
    // for snapshotting in DoDirectoryWork which could provide us with the
    // database name and version without needing to open the SQLite database),
    // it is safe and necessary for us to open the database on this thread and
    // retrieve its name and version. We do not need to worry about racing a
    // database open because database opens can only be processed on this
    // thread and we are performing the steps below synchronously.

    QM_TRY_INSPECT(
        const auto& fmDirectory,
        CloneFileAndAppend(*clientDirectory,
                           databaseFilename + kFileManagerDirectoryNameSuffix));

    QM_TRY_UNWRAP(
        const NotNull<nsCOMPtr<mozIStorageConnection>> connection,
        CreateStorageConnection(*databaseFile, *fmDirectory, VoidString(),
                                mOriginMetadata.mOrigin, mDirectoryLockId,
                                TelemetryIdForFile(databaseFile), Nothing{}));

    {
      // Load version information.
      QM_TRY_INSPECT(const auto& stmt,
                     CreateAndExecuteSingleStepStatement<
                         SingleStepResult::ReturnNullIfNoResult>(
                         *connection, "SELECT name, version FROM database"_ns));

      QM_TRY(OkIf(stmt), NS_ERROR_FILE_CORRUPTED);

      QM_TRY_INSPECT(
          const auto& databaseName,
          MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(nsString, stmt, GetString, 0));

      QM_TRY_INSPECT(const int64_t& version,
                     MOZ_TO_RESULT_INVOKE_MEMBER(stmt, GetInt64, 1));

      mDatabaseMetadataArray.AppendElement(
          DatabaseMetadata(databaseName, version, mPersistenceType));
    }
  }

  mState = State::SendingResults;

  QM_TRY(MOZ_TO_RESULT(
      DispatchThisAfterProcessingCurrentEvent(mOwningEventTarget)));

  return NS_OK;
}

nsresult GetDatabasesOp::BeginVersionChange() {
  MOZ_CRASH("Not implemented because this should be unreachable.");
}

bool GetDatabasesOp::AreActorsAlive() {
  MOZ_CRASH("Not implemented because this should be unreachable.");
}

void GetDatabasesOp::SendBlockedNotification() {
  MOZ_CRASH("Not implemented because this should be unreachable.");
}

nsresult GetDatabasesOp::DispatchToWorkThread() {
  MOZ_CRASH("Not implemented because this should be unreachable.");
}

nsresult GetDatabasesOp::DoVersionUpdate() {
  MOZ_CRASH("Not implemented because this should be unreachable.");
}

void GetDatabasesOp::SendResults() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mState == State::SendingResults);

#ifdef DEBUG
  NoteActorDestroyed();
#endif

  if (HasFailed()) {
    mResolver(ClampResultCode(ResultCode()));
  } else {
    mResolver(mDatabaseMetadataArray);
  }

  {
    auto destroyingDirectoryLockHandle = std::move(mDirectoryLockHandle);
  }

  CleanupMetadata();

  FinishSendResults();
}

TransactionDatabaseOperationBase::TransactionDatabaseOperationBase(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId)
    : DatabaseOperationBase(aTransaction->GetLoggingInfo()->Id(),
                            aTransaction->GetLoggingInfo()->NextRequestSN()),
      mTransaction(WrapNotNull(std::move(aTransaction))),
      mRequestId(aRequestId),
      mTransactionIsAborted((*mTransaction)->IsAborted()),
      mTransactionLoggingSerialNumber((*mTransaction)->LoggingSerialNumber()) {
  MOZ_ASSERT(LoggingSerialNumber());
}

TransactionDatabaseOperationBase::TransactionDatabaseOperationBase(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId,
    uint64_t aLoggingSerialNumber)
    : DatabaseOperationBase(aTransaction->GetLoggingInfo()->Id(),
                            aLoggingSerialNumber),
      mTransaction(WrapNotNull(std::move(aTransaction))),
      mRequestId(aRequestId),
      mTransactionIsAborted((*mTransaction)->IsAborted()),
      mTransactionLoggingSerialNumber((*mTransaction)->LoggingSerialNumber()) {}

TransactionDatabaseOperationBase::~TransactionDatabaseOperationBase() {
  MOZ_ASSERT(mInternalState == InternalState::Completed);
  MOZ_ASSERT(!mTransaction,
             "TransactionDatabaseOperationBase::Cleanup() was not called by a "
             "subclass!");
}

#ifdef DEBUG

void TransactionDatabaseOperationBase::AssertIsOnConnectionThread() const {
  (*mTransaction)->AssertIsOnConnectionThread();
}

#endif  // DEBUG

uint64_t TransactionDatabaseOperationBase::StartOnConnectionPool(
    const nsID& aBackgroundChildLoggingId, const nsACString& aDatabaseId,
    int64_t aLoggingSerialNumber, const nsTArray<nsString>& aObjectStoreNames,
    bool aIsWriteTransaction) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::Initial);

  // Must set mInternalState before dispatching otherwise we will race with the
  // connection thread.
  mInternalState = InternalState::DatabaseWork;

  return gConnectionPool->Start(aBackgroundChildLoggingId, aDatabaseId,
                                aLoggingSerialNumber, aObjectStoreNames,
                                aIsWriteTransaction, this);
}

void TransactionDatabaseOperationBase::DispatchToConnectionPool() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::Initial);

  Unused << this->Run();
}

void TransactionDatabaseOperationBase::RunOnConnectionThread() {
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(mInternalState == InternalState::DatabaseWork);
  MOZ_ASSERT(!HasFailed());

  AUTO_PROFILER_LABEL("TransactionDatabaseOperationBase::RunOnConnectionThread",
                      DOM);

  // There are several cases where we don't actually have to to any work here.

  if (mTransactionIsAborted || (*mTransaction)->IsInvalidatedOnAnyThread()) {
    // This transaction is already set to be aborted or invalidated.
    SetFailureCode(NS_ERROR_DOM_INDEXEDDB_ABORT_ERR);
  } else if (!OperationMayProceed()) {
    // The operation was canceled in some way, likely because the child process
    // has crashed.
    IDB_REPORT_INTERNAL_ERR();
    OverrideFailureCode(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
  } else {
    Database& database = (*mTransaction)->GetMutableDatabase();

    // Here we're actually going to perform the database operation.
    nsresult rv = database.EnsureConnection();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      SetFailureCode(rv);
    } else {
      DatabaseConnection* connection = database.GetConnection();
      MOZ_ASSERT(connection);

      auto& storageConnection = connection->MutableStorageConnection();

      AutoSetProgressHandler autoProgress;
      if (mLoggingSerialNumber) {
        rv = autoProgress.Register(storageConnection, this);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          SetFailureCode(rv);
        }
      }

      if (NS_SUCCEEDED(rv)) {
        if (mLoggingSerialNumber) {
          IDB_LOG_MARK_PARENT_TRANSACTION_REQUEST(
              "Beginning database work", "DB Start",
              IDB_LOG_ID_STRING(mBackgroundChildLoggingId),
              mTransactionLoggingSerialNumber, mLoggingSerialNumber);
        }

        rv = DoDatabaseWork(connection);

        if (mLoggingSerialNumber) {
          IDB_LOG_MARK_PARENT_TRANSACTION_REQUEST(
              "Finished database work", "DB End",
              IDB_LOG_ID_STRING(mBackgroundChildLoggingId),
              mTransactionLoggingSerialNumber, mLoggingSerialNumber);
        }

        if (NS_FAILED(rv)) {
          SetFailureCode(rv);
        }
      }
    }
  }

  // Must set mInternalState before dispatching otherwise we will race with the
  // owning thread.
  if (HasPreprocessInfo()) {
    mInternalState = InternalState::SendingPreprocess;
  } else {
    mInternalState = InternalState::SendingResults;
  }

  MOZ_ALWAYS_SUCCEEDS(mOwningEventTarget->Dispatch(this, NS_DISPATCH_NORMAL));
}

bool TransactionDatabaseOperationBase::HasPreprocessInfo() { return false; }

nsresult TransactionDatabaseOperationBase::SendPreprocessInfo() {
  return NS_OK;
}

void TransactionDatabaseOperationBase::NoteContinueReceived() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::WaitingForContinue);

  mWaitingForContinue = false;

  mInternalState = InternalState::SendingResults;

  // This TransactionDatabaseOperationBase can only be held alive by the IPDL.
  // Run() can end up with clearing that last reference. So we need to add
  // a self reference here.
  RefPtr<TransactionDatabaseOperationBase> kungFuDeathGrip = this;

  Unused << this->Run();
}

void TransactionDatabaseOperationBase::SendToConnectionPool() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::Initial);

  // Must set mInternalState before dispatching otherwise we will race with the
  // connection thread.
  mInternalState = InternalState::DatabaseWork;

  gConnectionPool->Dispatch((*mTransaction)->TransactionId(), this);

  (*mTransaction)->NoteActiveRequest();
}

void TransactionDatabaseOperationBase::SendPreprocess() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::SendingPreprocess);

  SendPreprocessInfoOrResults(/* aSendPreprocessInfo */ true);
}

void TransactionDatabaseOperationBase::SendResults() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::SendingResults);

  SendPreprocessInfoOrResults(/* aSendPreprocessInfo */ false);
}

void TransactionDatabaseOperationBase::SendPreprocessInfoOrResults(
    bool aSendPreprocessInfo) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::SendingPreprocess ||
             mInternalState == InternalState::SendingResults);

  // The flag is raised only when there is no mUpdateRefcountFunction for the
  // executing operation. It assume that is because the previous
  // StartTransactionOp was failed to begin a write transaction and it reported
  // when this operation has already jumped to the Connection thread.
  MOZ_DIAGNOSTIC_ASSERT_IF(mAssumingPreviousOperationFail,
                           (*mTransaction)->IsAborted());

  if (NS_WARN_IF(IsActorDestroyed())) {
    // Normally we wouldn't need to send any notifications if the actor was
    // already destroyed, but this can be a VersionChangeOp which needs to
    // notify its parent operation (OpenDatabaseOp) about the failure.
    // So SendFailureResult needs to be called even when the actor was
    // destroyed.  Normal operations redundantly check if the actor was
    // destroyed in SendSuccessResult and SendFailureResult, therefore it's
    // ok to call it in all cases here.
    if (!HasFailed()) {
      IDB_REPORT_INTERNAL_ERR();
      SetFailureCode(NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR);
    }
  } else if ((*mTransaction)->IsInvalidated() || (*mTransaction)->IsAborted()) {
    // Aborted transactions always see their requests fail with ABORT_ERR,
    // even if the request succeeded or failed with another error.
    OverrideFailureCode(NS_ERROR_DOM_INDEXEDDB_ABORT_ERR);
  }

  const nsresult rv = [aSendPreprocessInfo, this] {
    if (HasFailed()) {
      return ResultCode();
    }
    if (aSendPreprocessInfo) {
      // This should not release the IPDL reference.
      return SendPreprocessInfo();
    }
    // This may release the IPDL reference.
    return SendSuccessResult();
  }();

  if (NS_FAILED(rv)) {
    SetFailureCodeIfUnset(rv);

    // This should definitely release the IPDL reference.
    if (!SendFailureResult(rv)) {
      // Abort the transaction.
      (*mTransaction)->Abort(rv, /* aForce */ false);
    }
  }

  if (aSendPreprocessInfo && !HasFailed()) {
    mInternalState = InternalState::WaitingForContinue;

    mWaitingForContinue = true;
  } else {
    if (mLoggingSerialNumber) {
      (*mTransaction)->NoteFinishedRequest(mRequestId, ResultCode());
    }

    Cleanup();

    mInternalState = InternalState::Completed;
  }
}

bool TransactionDatabaseOperationBase::Init(TransactionBase& aTransaction) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mInternalState == InternalState::Initial);

  return true;
}

void TransactionDatabaseOperationBase::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mInternalState == InternalState::SendingResults);

  mTransaction.destroy();
}

NS_IMETHODIMP
TransactionDatabaseOperationBase::Run() {
  switch (mInternalState) {
    case InternalState::Initial:
      SendToConnectionPool();
      return NS_OK;

    case InternalState::DatabaseWork:
      RunOnConnectionThread();
      return NS_OK;

    case InternalState::SendingPreprocess:
      SendPreprocess();
      return NS_OK;

    case InternalState::SendingResults:
      SendResults();
      return NS_OK;

    default:
      MOZ_CRASH("Bad state!");
  }
}

TransactionBase::CommitOp::CommitOp(SafeRefPtr<TransactionBase> aTransaction,
                                    nsresult aResultCode)
    : DatabaseOperationBase(aTransaction->GetLoggingInfo()->Id(),
                            aTransaction->GetLoggingInfo()->NextRequestSN()),
      mTransaction(std::move(aTransaction)),
      mResultCode(aResultCode) {
  MOZ_ASSERT(mTransaction);
  MOZ_ASSERT(LoggingSerialNumber());
}

nsresult TransactionBase::CommitOp::WriteAutoIncrementCounts() {
  MOZ_ASSERT(mTransaction);
  mTransaction->AssertIsOnConnectionThread();
  MOZ_ASSERT(mTransaction->GetMode() == IDBTransaction::Mode::ReadWrite ||
             mTransaction->GetMode() == IDBTransaction::Mode::ReadWriteFlush ||
             mTransaction->GetMode() == IDBTransaction::Mode::Cleanup ||
             mTransaction->GetMode() == IDBTransaction::Mode::VersionChange);

  const nsTArray<SafeRefPtr<FullObjectStoreMetadata>>& metadataArray =
      mTransaction->mModifiedAutoIncrementObjectStoreMetadataArray;

  if (!metadataArray.IsEmpty()) {
    DatabaseConnection* connection =
        mTransaction->GetDatabase().GetConnection();
    MOZ_ASSERT(connection);

    // The parameter names are not used, parameters are bound by index only
    // locally in the same function.
    auto stmt = DatabaseConnection::LazyStatement(
        *connection,
        "UPDATE object_store "
        "SET auto_increment = :auto_increment WHERE id "
        "= :object_store_id;"_ns);

    for (const auto& metadata : metadataArray) {
      MOZ_ASSERT(!metadata->mDeleted);

      const int64_t nextAutoIncrementId = [&metadata] {
        const auto&& lockedAutoIncrementIds =
            metadata->mAutoIncrementIds.Lock();
        return lockedAutoIncrementIds->next;
      }();

      MOZ_ASSERT(nextAutoIncrementId > 1);

      QM_TRY_INSPECT(const auto& borrowedStmt, stmt.Borrow());

      QM_TRY(MOZ_TO_RESULT(
          borrowedStmt->BindInt64ByIndex(1, metadata->mCommonMetadata.id())));

      QM_TRY(MOZ_TO_RESULT(
          borrowedStmt->BindInt64ByIndex(0, nextAutoIncrementId)));

      QM_TRY(MOZ_TO_RESULT(borrowedStmt->Execute()));
    }
  }

  return NS_OK;
}

void TransactionBase::CommitOp::CommitOrRollbackAutoIncrementCounts() {
  MOZ_ASSERT(mTransaction);
  mTransaction->AssertIsOnConnectionThread();
  MOZ_ASSERT(mTransaction->GetMode() == IDBTransaction::Mode::ReadWrite ||
             mTransaction->GetMode() == IDBTransaction::Mode::ReadWriteFlush ||
             mTransaction->GetMode() == IDBTransaction::Mode::Cleanup ||
             mTransaction->GetMode() == IDBTransaction::Mode::VersionChange);

  const auto& metadataArray =
      mTransaction->mModifiedAutoIncrementObjectStoreMetadataArray;

  if (!metadataArray.IsEmpty()) {
    bool committed = NS_SUCCEEDED(mResultCode);

    for (const auto& metadata : metadataArray) {
      auto&& lockedAutoIncrementIds = metadata->mAutoIncrementIds.Lock();

      if (committed) {
        lockedAutoIncrementIds->committed = lockedAutoIncrementIds->next;
      } else {
        lockedAutoIncrementIds->next = lockedAutoIncrementIds->committed;
      }
    }
  }
}

#ifdef DEBUG

void TransactionBase::CommitOp::AssertForeignKeyConsistency(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  MOZ_ASSERT(mTransaction);
  mTransaction->AssertIsOnConnectionThread();
  MOZ_ASSERT(mTransaction->GetMode() != IDBTransaction::Mode::ReadOnly);

  {
    QM_TRY_INSPECT(
        const auto& pragmaStmt,
        CreateAndExecuteSingleStepStatement(
            aConnection->MutableStorageConnection(), "PRAGMA foreign_keys;"_ns),
        QM_ASSERT_UNREACHABLE_VOID);

    int32_t foreignKeysEnabled;
    MOZ_ALWAYS_SUCCEEDS(pragmaStmt->GetInt32(0, &foreignKeysEnabled));

    MOZ_ASSERT(foreignKeysEnabled,
               "Database doesn't have foreign keys enabled!");
  }

  {
    QM_TRY_INSPECT(const bool& foreignKeyError,
                   CreateAndExecuteSingleStepStatement<
                       SingleStepResult::ReturnNullIfNoResult>(
                       aConnection->MutableStorageConnection(),
                       "PRAGMA foreign_key_check;"_ns),
                   QM_ASSERT_UNREACHABLE_VOID);

    MOZ_ASSERT(!foreignKeyError, "Database has inconsisistent foreign keys!");
  }
}

#endif  // DEBUG

NS_IMPL_ISUPPORTS_INHERITED0(TransactionBase::CommitOp, DatabaseOperationBase)

NS_IMETHODIMP
TransactionBase::CommitOp::Run() {
  MOZ_ASSERT(mTransaction);
  mTransaction->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("TransactionBase::CommitOp::Run", DOM);

  IDB_LOG_MARK_PARENT_TRANSACTION_REQUEST(
      "Beginning database work", "DB Start",
      IDB_LOG_ID_STRING(mBackgroundChildLoggingId),
      mTransaction->LoggingSerialNumber(), mLoggingSerialNumber);

  if (mTransaction->GetMode() != IDBTransaction::Mode::ReadOnly &&
      mTransaction->mHasBeenActiveOnConnectionThread) {
    if (DatabaseConnection* connection =
            mTransaction->GetDatabase().GetConnection()) {
      // May be null if the VersionChangeOp was canceled.
      DatabaseConnection::UpdateRefcountFunction* fileRefcountFunction =
          connection->GetUpdateRefcountFunction();

      if (NS_SUCCEEDED(mResultCode)) {
        if (fileRefcountFunction) {
          mResultCode = fileRefcountFunction->WillCommit();
          NS_WARNING_ASSERTION(NS_SUCCEEDED(mResultCode),
                               "WillCommit() failed!");
        }

        if (NS_SUCCEEDED(mResultCode)) {
          mResultCode = WriteAutoIncrementCounts();
          NS_WARNING_ASSERTION(NS_SUCCEEDED(mResultCode),
                               "WriteAutoIncrementCounts() failed!");

          if (NS_SUCCEEDED(mResultCode)) {
            AssertForeignKeyConsistency(connection);

            mResultCode = connection->CommitWriteTransaction();
            NS_WARNING_ASSERTION(NS_SUCCEEDED(mResultCode), "Commit failed!");

            if (NS_SUCCEEDED(mResultCode) &&
                mTransaction->GetMode() ==
                    IDBTransaction::Mode::ReadWriteFlush) {
              mResultCode = connection->Checkpoint();
            }

            if (NS_SUCCEEDED(mResultCode) && fileRefcountFunction) {
              fileRefcountFunction->DidCommit();
            }
          }
        }
      }

      if (NS_FAILED(mResultCode)) {
        if (fileRefcountFunction) {
          fileRefcountFunction->DidAbort();
        }

        connection->RollbackWriteTransaction();
      }

      CommitOrRollbackAutoIncrementCounts();

      connection->FinishWriteTransaction();

      if (mTransaction->GetMode() == IDBTransaction::Mode::Cleanup) {
        connection->DoIdleProcessing(/* aNeedsCheckpoint */ true,
                                     /* aInterrupted */ Atomic<bool>(false));

        connection->EnableQuotaChecks();
      }
    }
  }

  IDB_LOG_MARK_PARENT_TRANSACTION_REQUEST(
      "Finished database work", "DB End",
      IDB_LOG_ID_STRING(mBackgroundChildLoggingId),
      mTransaction->LoggingSerialNumber(), mLoggingSerialNumber);

  IDB_LOG_MARK_PARENT_TRANSACTION("Finished database work", "DB End",
                                  IDB_LOG_ID_STRING(mBackgroundChildLoggingId),
                                  mTransaction->LoggingSerialNumber());

  return NS_OK;
}

void TransactionBase::CommitOp::TransactionFinishedBeforeUnblock() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mTransaction);

  AUTO_PROFILER_LABEL("CommitOp::TransactionFinishedBeforeUnblock", DOM);

  if (!IsActorDestroyed()) {
    mTransaction->UpdateMetadata(mResultCode);
  }
}

void TransactionBase::CommitOp::TransactionFinishedAfterUnblock() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mTransaction);

  IDB_LOG_MARK_PARENT_TRANSACTION(
      "Finished with result 0x%" PRIx32, "Transaction finished (0x%" PRIx32 ")",
      IDB_LOG_ID_STRING(mTransaction->GetLoggingInfo()->Id()),
      mTransaction->LoggingSerialNumber(), static_cast<uint32_t>(mResultCode));

  mTransaction->SendCompleteNotification(ClampResultCode(mResultCode));

  mTransaction->GetMutableDatabase().UnregisterTransaction(*mTransaction);

  mTransaction = nullptr;

#ifdef DEBUG
  // A bit hacky but the CommitOp is not really a normal database operation
  // that is tied to an actor. Do this to make our assertions happy.
  NoteActorDestroyed();
#endif
}

nsresult VersionChangeTransactionOp::SendSuccessResult() {
  AssertIsOnOwningThread();

  // Nothing to send here, the API assumes that this request always succeeds.
  return NS_OK;
}

bool VersionChangeTransactionOp::SendFailureResult(nsresult aResultCode) {
  AssertIsOnOwningThread();

  // The only option here is to cause the transaction to abort.
  return false;
}

void VersionChangeTransactionOp::Cleanup() {
  AssertIsOnOwningThread();

#ifdef DEBUG
  // A bit hacky but the VersionChangeTransactionOp is not generated in response
  // to a child request like most other database operations. Do this to make our
  // assertions happy.
  NoteActorDestroyed();
#endif

  TransactionDatabaseOperationBase::Cleanup();
}

nsresult CreateObjectStoreOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("CreateObjectStoreOp::DoDatabaseWork", DOM);

#ifdef DEBUG
  {
    // Make sure that we're not creating an object store with the same name as
    // another that already exists. This should be impossible because we should
    // have thrown an error long before now...
    // The parameter names are not used, parameters are bound by index only
    // locally in the same function.
    QM_TRY_INSPECT(const bool& hasResult,
                   aConnection
                       ->BorrowAndExecuteSingleStepStatement(
                           "SELECT name "
                           "FROM object_store "
                           "WHERE name = :name;"_ns,
                           [&self = *this](auto& stmt) -> Result<Ok, nsresult> {
                             QM_TRY(MOZ_TO_RESULT(stmt.BindStringByIndex(
                                 0, self.mMetadata.name())));
                             return Ok{};
                           })
                       .map(IsSome),
                   QM_ASSERT_UNREACHABLE);

    MOZ_ASSERT(!hasResult);
  }
#endif

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "INSERT INTO object_store (id, auto_increment, name, key_path) "
      "VALUES (:id, :auto_increment, :name, :key_path);"_ns,
      [&metadata =
           mMetadata](mozIStorageStatement& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(0, metadata.id())));

        QM_TRY(MOZ_TO_RESULT(
            stmt.BindInt32ByIndex(1, metadata.autoIncrement() ? 1 : 0)));

        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByIndex(2, metadata.name())));

        if (metadata.keyPath().IsValid()) {
          QM_TRY(MOZ_TO_RESULT(stmt.BindStringByIndex(
              3, metadata.keyPath().SerializeToString())));
        } else {
          QM_TRY(MOZ_TO_RESULT(stmt.BindNullByIndex(3)));
        }

        return Ok{};
      })));

#ifdef DEBUG
  {
    int64_t id;
    MOZ_ALWAYS_SUCCEEDS(
        aConnection->MutableStorageConnection().GetLastInsertRowID(&id));
    MOZ_ASSERT(mMetadata.id() == id);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  return NS_OK;
}

nsresult DeleteObjectStoreOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("DeleteObjectStoreOp::DoDatabaseWork", DOM);

#ifdef DEBUG
  {
    // Make sure |mIsLastObjectStore| is telling the truth.
    QM_TRY_INSPECT(
        const auto& stmt,
        aConnection->BorrowCachedStatement("SELECT id FROM object_store;"_ns),
        QM_ASSERT_UNREACHABLE);

    bool foundThisObjectStore = false;
    bool foundOtherObjectStore = false;

    while (true) {
      bool hasResult;
      MOZ_ALWAYS_SUCCEEDS(stmt->ExecuteStep(&hasResult));

      if (!hasResult) {
        break;
      }

      int64_t id;
      MOZ_ALWAYS_SUCCEEDS(stmt->GetInt64(0, &id));

      if (id == mMetadata->mCommonMetadata.id()) {
        foundThisObjectStore = true;
      } else {
        foundOtherObjectStore = true;
      }
    }

    MOZ_ASSERT_IF(mIsLastObjectStore,
                  foundThisObjectStore && !foundOtherObjectStore);
    MOZ_ASSERT_IF(!mIsLastObjectStore,
                  foundThisObjectStore && foundOtherObjectStore);
  }
#endif

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  if (mIsLastObjectStore) {
    // We can just delete everything if this is the last object store.
    QM_TRY(MOZ_TO_RESULT(
        aConnection->ExecuteCachedStatement("DELETE FROM index_data;"_ns)));

    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
        "DELETE FROM unique_index_data;"_ns)));

    QM_TRY(MOZ_TO_RESULT(
        aConnection->ExecuteCachedStatement("DELETE FROM object_data;"_ns)));

    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
        "DELETE FROM object_store_index;"_ns)));

    QM_TRY(MOZ_TO_RESULT(
        aConnection->ExecuteCachedStatement("DELETE FROM object_store;"_ns)));
  } else {
    QM_TRY_INSPECT(
        const bool& hasIndexes,
        ObjectStoreHasIndexes(*aConnection, mMetadata->mCommonMetadata.id()));

    const auto bindObjectStoreIdToFirstParameter =
        [this](mozIStorageStatement& stmt) -> Result<Ok, nsresult> {
      QM_TRY(MOZ_TO_RESULT(
          stmt.BindInt64ByIndex(0, mMetadata->mCommonMetadata.id())));

      return Ok{};
    };

    // The parameter name :object_store_id in the SQL statements below is not
    // used for binding, parameters are bound by index only locally by
    // bindObjectStoreIdToFirstParameter.
    if (hasIndexes) {
      QM_TRY(MOZ_TO_RESULT(DeleteObjectStoreDataTableRowsWithIndexes(
          aConnection, mMetadata->mCommonMetadata.id(), Nothing())));

      // Now clean up the object store index table.
      QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
          "DELETE FROM object_store_index "
          "WHERE object_store_id = :object_store_id;"_ns,
          bindObjectStoreIdToFirstParameter)));
    } else {
      // We only have to worry about object data if this object store has no
      // indexes.
      QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
          "DELETE FROM object_data "
          "WHERE object_store_id = :object_store_id;"_ns,
          bindObjectStoreIdToFirstParameter)));
    }

    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
        "DELETE FROM object_store "
        "WHERE id = :object_store_id;"_ns,
        bindObjectStoreIdToFirstParameter)));

#ifdef DEBUG
    {
      int32_t deletedRowCount;
      MOZ_ALWAYS_SUCCEEDS(
          aConnection->MutableStorageConnection().GetAffectedRows(
              &deletedRowCount));
      MOZ_ASSERT(deletedRowCount == 1);
    }
#endif
  }

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  if (mMetadata->mCommonMetadata.autoIncrement()) {
    Transaction().ForgetModifiedAutoIncrementObjectStore(*mMetadata);
  }

  return NS_OK;
}

nsresult RenameObjectStoreOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("RenameObjectStoreOp::DoDatabaseWork", DOM);

#ifdef DEBUG
  {
    // Make sure that we're not renaming an object store with the same name as
    // another that already exists. This should be impossible because we should
    // have thrown an error long before now...
    // The parameter names are not used, parameters are bound by index only
    // locally in the same function.
    QM_TRY_INSPECT(
        const bool& hasResult,
        aConnection
            ->BorrowAndExecuteSingleStepStatement(
                "SELECT name "
                "FROM object_store "
                "WHERE name = :name AND id != :id;"_ns,
                [&self = *this](auto& stmt) -> Result<Ok, nsresult> {
                  QM_TRY(
                      MOZ_TO_RESULT(stmt.BindStringByIndex(0, self.mNewName)));

                  QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(1, self.mId)));
                  return Ok{};
                })
            .map(IsSome),
        QM_ASSERT_UNREACHABLE);

    MOZ_ASSERT(!hasResult);
  }
#endif

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "UPDATE object_store "
      "SET name = :name "
      "WHERE id = :id;"_ns,
      [&self = *this](mozIStorageStatement& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByIndex(0, self.mNewName)));

        QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(1, self.mId)));

        return Ok{};
      })));

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  return NS_OK;
}

CreateIndexOp::CreateIndexOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                             const IndexOrObjectStoreId aObjectStoreId,
                             const IndexMetadata& aMetadata)
    : VersionChangeTransactionOp(std::move(aTransaction)),
      mMetadata(aMetadata),
      mFileManager(Transaction().GetDatabase().GetFileManagerPtr()),
      mDatabaseId(Transaction().DatabaseId()),
      mObjectStoreId(aObjectStoreId) {
  MOZ_ASSERT(aObjectStoreId);
  MOZ_ASSERT(aMetadata.id());
  MOZ_ASSERT(mFileManager);
  MOZ_ASSERT(!mDatabaseId.IsEmpty());
}

nsresult CreateIndexOp::InsertDataFromObjectStore(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mMaybeUniqueIndexTable);

  AUTO_PROFILER_LABEL("CreateIndexOp::InsertDataFromObjectStore", DOM);

  auto& storageConnection = aConnection->MutableStorageConnection();

  RefPtr<UpdateIndexDataValuesFunction> updateFunction =
      new UpdateIndexDataValuesFunction(this, aConnection,
                                        Transaction().GetDatabasePtr());

  constexpr auto updateFunctionName = "update_index_data_values"_ns;

  nsresult rv =
      storageConnection.CreateFunction(updateFunctionName, 4, updateFunction);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = InsertDataFromObjectStoreInternal(aConnection);

  MOZ_ALWAYS_SUCCEEDS(storageConnection.RemoveFunction(updateFunctionName));

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

nsresult CreateIndexOp::InsertDataFromObjectStoreInternal(
    DatabaseConnection* aConnection) const {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mMaybeUniqueIndexTable);

  MOZ_ASSERT(aConnection->HasStorageConnection());

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "UPDATE object_data "
      "SET index_data_values = update_index_data_values "
      "(key, index_data_values, file_ids, data) "
      "WHERE object_store_id = :object_store_id;"_ns,
      [objectStoredId =
           mObjectStoreId](mozIStorageStatement& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(0, objectStoredId)));

        return Ok{};
      })));

  return NS_OK;
}

bool CreateIndexOp::Init(TransactionBase& aTransaction) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mObjectStoreId);
  MOZ_ASSERT(mMaybeUniqueIndexTable.isNothing());

  const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
      aTransaction.GetMetadataForObjectStoreId(mObjectStoreId);
  MOZ_ASSERT(objectStoreMetadata);

  const uint32_t indexCount = objectStoreMetadata->mIndexes.Count();
  if (!indexCount) {
    return true;
  }

  auto uniqueIndexTable = UniqueIndexTable{indexCount};

  for (const auto& value : objectStoreMetadata->mIndexes.Values()) {
    MOZ_ASSERT(!uniqueIndexTable.Contains(value->mCommonMetadata.id()));

    if (NS_WARN_IF(!uniqueIndexTable.InsertOrUpdate(
            value->mCommonMetadata.id(), value->mCommonMetadata.unique(),
            fallible))) {
      IDB_REPORT_INTERNAL_ERR();
      NS_WARNING("out of memory");
      return false;
    }
  }

  uniqueIndexTable.MarkImmutable();

  mMaybeUniqueIndexTable.emplace(std::move(uniqueIndexTable));

  return true;
}

nsresult CreateIndexOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("CreateIndexOp::DoDatabaseWork", DOM);

#ifdef DEBUG
  {
    // Make sure that we're not creating an index with the same name and object
    // store as another that already exists. This should be impossible because
    // we should have thrown an error long before now...
    // The parameter names are not used, parameters are bound by index only
    // locally in the same function.
    QM_TRY_INSPECT(
        const bool& hasResult,
        aConnection
            ->BorrowAndExecuteSingleStepStatement(
                "SELECT name "
                "FROM object_store_index "
                "WHERE object_store_id = :object_store_id AND name = :name;"_ns,
                [&self = *this](auto& stmt) -> Result<Ok, nsresult> {
                  QM_TRY(MOZ_TO_RESULT(
                      stmt.BindInt64ByIndex(0, self.mObjectStoreId)));
                  QM_TRY(MOZ_TO_RESULT(
                      stmt.BindStringByIndex(1, self.mMetadata.name())));
                  return Ok{};
                })
            .map(IsSome),
        QM_ASSERT_UNREACHABLE);

    MOZ_ASSERT(!hasResult);
  }
#endif

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "INSERT INTO object_store_index (id, name, key_path, unique_index, "
      "multientry, object_store_id, locale, "
      "is_auto_locale) "
      "VALUES (:id, :name, :key_path, :unique, :multientry, "
      ":object_store_id, :locale, :is_auto_locale)"_ns,
      [&metadata = mMetadata, objectStoreId = mObjectStoreId](
          mozIStorageStatement& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(0, metadata.id())));

        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByIndex(1, metadata.name())));

        QM_TRY(MOZ_TO_RESULT(
            stmt.BindStringByIndex(2, metadata.keyPath().SerializeToString())));

        QM_TRY(
            MOZ_TO_RESULT(stmt.BindInt32ByIndex(3, metadata.unique() ? 1 : 0)));

        QM_TRY(MOZ_TO_RESULT(
            stmt.BindInt32ByIndex(4, metadata.multiEntry() ? 1 : 0)));
        QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(5, objectStoreId)));

        QM_TRY(MOZ_TO_RESULT(
            metadata.locale().IsEmpty()
                ? stmt.BindNullByIndex(6)
                : stmt.BindUTF8StringByIndex(6, metadata.locale())));

        QM_TRY(MOZ_TO_RESULT(stmt.BindInt32ByIndex(7, metadata.autoLocale())));

        return Ok{};
      })));

#ifdef DEBUG
  {
    int64_t id;
    MOZ_ALWAYS_SUCCEEDS(
        aConnection->MutableStorageConnection().GetLastInsertRowID(&id));
    MOZ_ASSERT(mMetadata.id() == id);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(InsertDataFromObjectStore(aConnection)));

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  return NS_OK;
}

NS_IMPL_ISUPPORTS(CreateIndexOp::UpdateIndexDataValuesFunction,
                  mozIStorageFunction);

NS_IMETHODIMP
CreateIndexOp::UpdateIndexDataValuesFunction::OnFunctionCall(
    mozIStorageValueArray* aValues, nsIVariant** _retval) {
  MOZ_ASSERT(aValues);
  MOZ_ASSERT(_retval);
  MOZ_ASSERT(mConnection);
  mConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mOp);
  MOZ_ASSERT(mOp->mFileManager);

  AUTO_PROFILER_LABEL(
      "CreateIndexOp::UpdateIndexDataValuesFunction::OnFunctionCall", DOM);

#ifdef DEBUG
  {
    uint32_t argCount;
    MOZ_ALWAYS_SUCCEEDS(aValues->GetNumEntries(&argCount));
    MOZ_ASSERT(argCount == 4);  // key, index_data_values, file_ids, data

    int32_t valueType;
    MOZ_ALWAYS_SUCCEEDS(aValues->GetTypeOfIndex(0, &valueType));
    MOZ_ASSERT(valueType == mozIStorageValueArray::VALUE_TYPE_BLOB);

    MOZ_ALWAYS_SUCCEEDS(aValues->GetTypeOfIndex(1, &valueType));
    MOZ_ASSERT(valueType == mozIStorageValueArray::VALUE_TYPE_NULL ||
               valueType == mozIStorageValueArray::VALUE_TYPE_BLOB);

    MOZ_ALWAYS_SUCCEEDS(aValues->GetTypeOfIndex(2, &valueType));
    MOZ_ASSERT(valueType == mozIStorageValueArray::VALUE_TYPE_NULL ||
               valueType == mozIStorageValueArray::VALUE_TYPE_TEXT);

    MOZ_ALWAYS_SUCCEEDS(aValues->GetTypeOfIndex(3, &valueType));
    MOZ_ASSERT(valueType == mozIStorageValueArray::VALUE_TYPE_BLOB ||
               valueType == mozIStorageValueArray::VALUE_TYPE_INTEGER);
  }
#endif

  QM_TRY_UNWRAP(auto cloneInfo, GetStructuredCloneReadInfoFromValueArray(
                                    aValues,
                                    /* aDataIndex */ 3,
                                    /* aFileIdsIndex */ 2, *mOp->mFileManager));

  const IndexMetadata& metadata = mOp->mMetadata;
  const IndexOrObjectStoreId& objectStoreId = mOp->mObjectStoreId;

  // XXX does this really need a non-const cloneInfo?
  QM_TRY_INSPECT(const auto& updateInfos,
                 DeserializeIndexValueToUpdateInfos(
                     metadata.id(), metadata.keyPath(), metadata.multiEntry(),
                     metadata.locale(), cloneInfo));

  if (updateInfos.IsEmpty()) {
    // XXX See if we can do this without copying...

    nsCOMPtr<nsIVariant> unmodifiedValue;

    // No changes needed, just return the original value.
    QM_TRY_INSPECT(const int32_t& valueType,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aValues, GetTypeOfIndex, 1));

    MOZ_ASSERT(valueType == mozIStorageValueArray::VALUE_TYPE_NULL ||
               valueType == mozIStorageValueArray::VALUE_TYPE_BLOB);

    if (valueType == mozIStorageValueArray::VALUE_TYPE_NULL) {
      unmodifiedValue = new storage::NullVariant();
      unmodifiedValue.forget(_retval);
      return NS_OK;
    }

    MOZ_ASSERT(valueType == mozIStorageValueArray::VALUE_TYPE_BLOB);

    const uint8_t* blobData;
    uint32_t blobDataLength;
    QM_TRY(
        MOZ_TO_RESULT(aValues->GetSharedBlob(1, &blobDataLength, &blobData)));

    const std::pair<uint8_t*, int> copiedBlobDataPair(
        static_cast<uint8_t*>(malloc(blobDataLength)), blobDataLength);

    if (!copiedBlobDataPair.first) {
      IDB_REPORT_INTERNAL_ERR();
      return NS_ERROR_OUT_OF_MEMORY;
    }

    memcpy(copiedBlobDataPair.first, blobData, blobDataLength);

    unmodifiedValue = new storage::AdoptedBlobVariant(copiedBlobDataPair);
    unmodifiedValue.forget(_retval);

    return NS_OK;
  }

  Key key;
  QM_TRY(MOZ_TO_RESULT(key.SetFromValueArray(aValues, 0)));

  QM_TRY_UNWRAP(auto indexValues, ReadCompressedIndexDataValues(*aValues, 1));

  const bool hadPreviousIndexValues = !indexValues.IsEmpty();

  const uint32_t updateInfoCount = updateInfos.Length();

  QM_TRY(OkIf(indexValues.SetCapacity(indexValues.Length() + updateInfoCount,
                                      fallible)),
         NS_ERROR_OUT_OF_MEMORY, IDB_REPORT_INTERNAL_ERR_LAMBDA);

  // First construct the full list to update the index_data_values row.
  for (const IndexUpdateInfo& info : updateInfos) {
    MOZ_ALWAYS_TRUE(indexValues.InsertElementSorted(
        IndexDataValue(metadata.id(), metadata.unique(), info.value(),
                       info.localizedValue()),
        fallible));
  }

  QM_TRY_UNWRAP((auto [indexValuesBlob, indexValuesBlobLength]),
                MakeCompressedIndexDataValues(indexValues));

  MOZ_ASSERT(!indexValuesBlobLength == !(indexValuesBlob.get()));

  nsCOMPtr<nsIVariant> value;

  if (!indexValuesBlob) {
    value = new storage::NullVariant();

    value.forget(_retval);
    return NS_OK;
  }

  // Now insert the new table rows. We only need to construct a new list if
  // the full list is different.
  if (hadPreviousIndexValues) {
    indexValues.ClearAndRetainStorage();

    MOZ_ASSERT(indexValues.Capacity() >= updateInfoCount);

    for (const IndexUpdateInfo& info : updateInfos) {
      MOZ_ALWAYS_TRUE(indexValues.InsertElementSorted(
          IndexDataValue(metadata.id(), metadata.unique(), info.value(),
                         info.localizedValue()),
          fallible));
    }
  }

  QM_TRY(MOZ_TO_RESULT(
      InsertIndexTableRows(mConnection, objectStoreId, key, indexValues)));

  value = new storage::AdoptedBlobVariant(
      std::pair(indexValuesBlob.release(), indexValuesBlobLength));

  value.forget(_retval);
  return NS_OK;
}

DeleteIndexOp::DeleteIndexOp(SafeRefPtr<VersionChangeTransaction> aTransaction,
                             const IndexOrObjectStoreId aObjectStoreId,
                             const IndexOrObjectStoreId aIndexId,
                             const bool aUnique, const bool aIsLastIndex)
    : VersionChangeTransactionOp(std::move(aTransaction)),
      mObjectStoreId(aObjectStoreId),
      mIndexId(aIndexId),
      mUnique(aUnique),
      mIsLastIndex(aIsLastIndex) {
  MOZ_ASSERT(aObjectStoreId);
  MOZ_ASSERT(aIndexId);
}

nsresult DeleteIndexOp::RemoveReferencesToIndex(
    DatabaseConnection* aConnection, const Key& aObjectStoreKey,
    nsTArray<IndexDataValue>& aIndexValues) const {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(!IsOnBackgroundThread());
  MOZ_ASSERT(aConnection);
  MOZ_ASSERT(!aObjectStoreKey.IsUnset());
  MOZ_ASSERT_IF(!mIsLastIndex, !aIndexValues.IsEmpty());

  AUTO_PROFILER_LABEL("DeleteIndexOp::RemoveReferencesToIndex", DOM);

  if (mIsLastIndex) {
    // There is no need to parse the previous entry in the index_data_values
    // column if this is the last index. Simply set it to NULL.
    QM_TRY_INSPECT(const auto& stmt,
                   aConnection->BorrowCachedStatement(
                       "UPDATE object_data "
                       "SET index_data_values = NULL "
                       "WHERE object_store_id = :"_ns +
                       kStmtParamNameObjectStoreId + " AND key = :"_ns +
                       kStmtParamNameKey + ";"_ns));

    QM_TRY(MOZ_TO_RESULT(
        stmt->BindInt64ByName(kStmtParamNameObjectStoreId, mObjectStoreId)));

    QM_TRY(MOZ_TO_RESULT(
        aObjectStoreKey.BindToStatement(&*stmt, kStmtParamNameKey)));

    QM_TRY(MOZ_TO_RESULT(stmt->Execute()));

    return NS_OK;
  }

  {
    IndexDataValue search;
    search.mIndexId = mIndexId;

    // Use raw pointers for search to avoid redundant index validity checks.
    // Maybe this should better be encapsulated in nsTArray.
    const auto* const begin = aIndexValues.Elements();
    const auto* const end = aIndexValues.Elements() + aIndexValues.Length();

    const auto indexIdComparator = [](const IndexDataValue& aA,
                                      const IndexDataValue& aB) {
      return aA.mIndexId < aB.mIndexId;
    };

    MOZ_ASSERT(std::is_sorted(begin, end, indexIdComparator));

    const auto [beginRange, endRange] =
        std::equal_range(begin, end, search, indexIdComparator);
    if (beginRange == end) {
      IDB_REPORT_INTERNAL_ERR();
      return NS_ERROR_FILE_CORRUPTED;
    }

    aIndexValues.RemoveElementsAt(beginRange - begin, endRange - beginRange);
  }

  QM_TRY(MOZ_TO_RESULT(UpdateIndexValues(aConnection, mObjectStoreId,
                                         aObjectStoreKey, aIndexValues)));

  return NS_OK;
}

nsresult DeleteIndexOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

#ifdef DEBUG
  {
    // Make sure |mIsLastIndex| is telling the truth.
    // The parameter names are not used, parameters are bound by index only
    // locally in the same function.
    QM_TRY_INSPECT(const auto& stmt,
                   aConnection->BorrowCachedStatement(
                       "SELECT id "
                       "FROM object_store_index "
                       "WHERE object_store_id = :object_store_id;"_ns),
                   QM_ASSERT_UNREACHABLE);

    MOZ_ALWAYS_SUCCEEDS(stmt->BindInt64ByIndex(0, mObjectStoreId));

    bool foundThisIndex = false;
    bool foundOtherIndex = false;

    while (true) {
      bool hasResult;
      MOZ_ALWAYS_SUCCEEDS(stmt->ExecuteStep(&hasResult));

      if (!hasResult) {
        break;
      }

      int64_t id;
      MOZ_ALWAYS_SUCCEEDS(stmt->GetInt64(0, &id));

      if (id == mIndexId) {
        foundThisIndex = true;
      } else {
        foundOtherIndex = true;
      }
    }

    MOZ_ASSERT_IF(mIsLastIndex, foundThisIndex && !foundOtherIndex);
    MOZ_ASSERT_IF(!mIsLastIndex, foundThisIndex && foundOtherIndex);
  }
#endif

  AUTO_PROFILER_LABEL("DeleteIndexOp::DoDatabaseWork", DOM);

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  // mozStorage warns that these statements trigger a sort operation but we
  // don't care because this is a very rare call and we expect it to be slow.
  // The cost of having an index on this field is too high.
  QM_TRY_INSPECT(
      const auto& selectStmt,
      aConnection->BorrowCachedStatement(
          mUnique
              ? (mIsLastIndex
                     ? "/* do not warn (bug someone else) */ "
                       "SELECT value, object_data_key "
                       "FROM unique_index_data "
                       "WHERE index_id = :"_ns +
                           kStmtParamNameIndexId +
                           " ORDER BY object_data_key ASC;"_ns
                     : "/* do not warn (bug out) */ "
                       "SELECT unique_index_data.value, "
                       "unique_index_data.object_data_key, "
                       "object_data.index_data_values "
                       "FROM unique_index_data "
                       "JOIN object_data "
                       "ON unique_index_data.object_data_key = object_data.key "
                       "WHERE unique_index_data.index_id = :"_ns +
                           kStmtParamNameIndexId +
                           " AND object_data.object_store_id = :"_ns +
                           kStmtParamNameObjectStoreId +
                           " ORDER BY unique_index_data.object_data_key ASC;"_ns)
              : (mIsLastIndex
                     ? "/* do not warn (bug me not) */ "
                       "SELECT value, object_data_key "
                       "FROM index_data "
                       "WHERE index_id = :"_ns +
                           kStmtParamNameIndexId +
                           " AND object_store_id = :"_ns +
                           kStmtParamNameObjectStoreId +
                           " ORDER BY object_data_key ASC;"_ns
                     : "/* do not warn (bug off) */ "
                       "SELECT index_data.value, "
                       "index_data.object_data_key, "
                       "object_data.index_data_values "
                       "FROM index_data "
                       "JOIN object_data "
                       "ON index_data.object_data_key = object_data.key "
                       "WHERE index_data.index_id = :"_ns +
                           kStmtParamNameIndexId +
                           " AND object_data.object_store_id = :"_ns +
                           kStmtParamNameObjectStoreId +
                           " ORDER BY index_data.object_data_key ASC;"_ns)));

  QM_TRY(MOZ_TO_RESULT(
      selectStmt->BindInt64ByName(kStmtParamNameIndexId, mIndexId)));

  if (!mUnique || !mIsLastIndex) {
    QM_TRY(MOZ_TO_RESULT(selectStmt->BindInt64ByName(
        kStmtParamNameObjectStoreId, mObjectStoreId)));
  }

  Key lastObjectStoreKey;
  IndexDataValuesAutoArray lastIndexValues;

  QM_TRY(CollectWhileHasResult(
      *selectStmt,
      [this, &aConnection, &lastObjectStoreKey, &lastIndexValues,
       deleteIndexRowStmt =
           DatabaseConnection::LazyStatement{
               *aConnection,
               mUnique
                   ? "DELETE FROM unique_index_data "
                     "WHERE index_id = :"_ns +
                         kStmtParamNameIndexId + " AND value = :"_ns +
                         kStmtParamNameValue + ";"_ns
                   : "DELETE FROM index_data "
                     "WHERE index_id = :"_ns +
                         kStmtParamNameIndexId + " AND value = :"_ns +
                         kStmtParamNameValue + " AND object_data_key = :"_ns +
                         kStmtParamNameObjectDataKey + ";"_ns}](
          auto& selectStmt) mutable -> Result<Ok, nsresult> {
        // We always need the index key to delete the index row.
        Key indexKey;
        QM_TRY(MOZ_TO_RESULT(indexKey.SetFromStatement(&selectStmt, 0)));

        QM_TRY(OkIf(!indexKey.IsUnset()), Err(NS_ERROR_FILE_CORRUPTED),
               IDB_REPORT_INTERNAL_ERR_LAMBDA);

        // Don't call |lastObjectStoreKey.BindToStatement()| directly because we
        // don't want to copy the same key multiple times.
        const uint8_t* objectStoreKeyData;
        uint32_t objectStoreKeyDataLength;
        QM_TRY(MOZ_TO_RESULT(selectStmt.GetSharedBlob(
            1, &objectStoreKeyDataLength, &objectStoreKeyData)));

        QM_TRY(OkIf(objectStoreKeyDataLength), Err(NS_ERROR_FILE_CORRUPTED),
               IDB_REPORT_INTERNAL_ERR_LAMBDA);

        const nsDependentCString currentObjectStoreKeyBuffer(
            reinterpret_cast<const char*>(objectStoreKeyData),
            objectStoreKeyDataLength);
        if (currentObjectStoreKeyBuffer != lastObjectStoreKey.GetBuffer()) {
          // We just walked to the next object store key.
          if (!lastObjectStoreKey.IsUnset()) {
            // Before we move on to the next key we need to update the previous
            // key's index_data_values column.
            QM_TRY(MOZ_TO_RESULT(RemoveReferencesToIndex(
                aConnection, lastObjectStoreKey, lastIndexValues)));
          }

          // Save the object store key.
          lastObjectStoreKey = Key(currentObjectStoreKeyBuffer);

          // And the |index_data_values| row if this isn't the only index.
          if (!mIsLastIndex) {
            lastIndexValues.ClearAndRetainStorage();
            QM_TRY(MOZ_TO_RESULT(
                ReadCompressedIndexDataValues(selectStmt, 2, lastIndexValues)));

            QM_TRY(OkIf(!lastIndexValues.IsEmpty()),
                   Err(NS_ERROR_FILE_CORRUPTED),
                   IDB_REPORT_INTERNAL_ERR_LAMBDA);
          }
        }

        // Now delete the index row.
        {
          QM_TRY_INSPECT(const auto& borrowedDeleteIndexRowStmt,
                         deleteIndexRowStmt.Borrow());

          QM_TRY(MOZ_TO_RESULT(borrowedDeleteIndexRowStmt->BindInt64ByName(
              kStmtParamNameIndexId, mIndexId)));

          QM_TRY(MOZ_TO_RESULT(indexKey.BindToStatement(
              &*borrowedDeleteIndexRowStmt, kStmtParamNameValue)));

          if (!mUnique) {
            QM_TRY(MOZ_TO_RESULT(lastObjectStoreKey.BindToStatement(
                &*borrowedDeleteIndexRowStmt, kStmtParamNameObjectDataKey)));
          }

          QM_TRY(MOZ_TO_RESULT(borrowedDeleteIndexRowStmt->Execute()));
        }

        return Ok{};
      }));

  // Take care of the last key.
  if (!lastObjectStoreKey.IsUnset()) {
    MOZ_ASSERT_IF(!mIsLastIndex, !lastIndexValues.IsEmpty());

    QM_TRY(MOZ_TO_RESULT(RemoveReferencesToIndex(
        aConnection, lastObjectStoreKey, lastIndexValues)));
  }

  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "DELETE FROM object_store_index "
      "WHERE id = :index_id;"_ns,
      [indexId =
           mIndexId](mozIStorageStatement& deleteStmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(deleteStmt.BindInt64ByIndex(0, indexId)));

        return Ok{};
      })));

#ifdef DEBUG
  {
    int32_t deletedRowCount;
    MOZ_ALWAYS_SUCCEEDS(aConnection->MutableStorageConnection().GetAffectedRows(
        &deletedRowCount));
    MOZ_ASSERT(deletedRowCount == 1);
  }
#endif

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  return NS_OK;
}

nsresult RenameIndexOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("RenameIndexOp::DoDatabaseWork", DOM);

#ifdef DEBUG
  {
    // Make sure that we're not renaming an index with the same name as another
    // that already exists. This should be impossible because we should have
    // thrown an error long before now...
    // The parameter names are not used, parameters are bound by index only
    // locally in the same function.
    QM_TRY_INSPECT(const bool& hasResult,
                   aConnection
                       ->BorrowAndExecuteSingleStepStatement(
                           "SELECT name "
                           "FROM object_store_index "
                           "WHERE object_store_id = :object_store_id "
                           "AND name = :name "
                           "AND id != :id;"_ns,
                           [&self = *this](auto& stmt) -> Result<Ok, nsresult> {
                             QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(
                                 0, self.mObjectStoreId)));
                             QM_TRY(MOZ_TO_RESULT(
                                 stmt.BindStringByIndex(1, self.mNewName)));
                             QM_TRY(MOZ_TO_RESULT(
                                 stmt.BindInt64ByIndex(2, self.mIndexId)));

                             return Ok{};
                           })
                       .map(IsSome),
                   QM_ASSERT_UNREACHABLE);

    MOZ_ASSERT(!hasResult);
  }
#else
  Unused << mObjectStoreId;
#endif

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
      "UPDATE object_store_index "
      "SET name = :name "
      "WHERE id = :id;"_ns,
      [&self = *this](mozIStorageStatement& stmt) -> Result<Ok, nsresult> {
        QM_TRY(MOZ_TO_RESULT(stmt.BindStringByIndex(0, self.mNewName)));

        QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByIndex(1, self.mIndexId)));

        return Ok{};
      })));

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  return NS_OK;
}

Result<bool, nsresult> NormalTransactionOp::ObjectStoreHasIndexes(
    DatabaseConnection& aConnection, const IndexOrObjectStoreId aObjectStoreId,
    const bool aMayHaveIndexes) {
  aConnection.AssertIsOnConnectionThread();
  MOZ_ASSERT(aObjectStoreId);

  if (Transaction().GetMode() == IDBTransaction::Mode::VersionChange &&
      aMayHaveIndexes) {
    // If this is a version change transaction then mObjectStoreMayHaveIndexes
    // could be wrong (e.g. if a unique index failed to be created due to a
    // constraint error). We have to check on this thread by asking the database
    // directly.
    QM_TRY_RETURN(DatabaseOperationBase::ObjectStoreHasIndexes(aConnection,
                                                               aObjectStoreId));
  }

#ifdef DEBUG
  QM_TRY_INSPECT(
      const bool& hasIndexes,
      DatabaseOperationBase::ObjectStoreHasIndexes(aConnection, aObjectStoreId),
      QM_ASSERT_UNREACHABLE);
  MOZ_ASSERT(aMayHaveIndexes == hasIndexes);
#endif

  return aMayHaveIndexes;
}

Result<PreprocessParams, nsresult> NormalTransactionOp::GetPreprocessParams() {
  return PreprocessParams{};
}

nsresult NormalTransactionOp::SendPreprocessInfo() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!IsActorDestroyed());

  QM_TRY_INSPECT(const auto& params, GetPreprocessParams());

  MOZ_ASSERT(params.type() != PreprocessParams::T__None);

  if (NS_WARN_IF(!PBackgroundIDBRequestParent::SendPreprocess(params))) {
    IDB_REPORT_INTERNAL_ERR();
    return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
  }

  return NS_OK;
}

nsresult NormalTransactionOp::SendSuccessResult() {
  AssertIsOnOwningThread();

  if (!IsActorDestroyed()) {
    static const size_t kMaxIDBMsgOverhead = 1024 * 1024 * 10;  // 10MB
    const uint32_t maximalSizeFromPref =
        IndexedDatabaseManager::MaxSerializedMsgSize();
    MOZ_ASSERT(maximalSizeFromPref > kMaxIDBMsgOverhead);
    const size_t kMaxMessageSize = maximalSizeFromPref - kMaxIDBMsgOverhead;

    RequestResponse response;
    size_t responseSize = kMaxMessageSize;
    GetResponse(response, &responseSize);

    // TODO: Adjust the calculation of the response size in relevant
    // GetResponse methods to account for the fallback to shared memory during
    // serialization of the primary key and index keys if their size exceeds
    // IPC::kMessageBufferShmemThreshold. This ensures the calculated size
    // accurately reflects the actual IPC message size.
    // See also bug 1945040.

    if (responseSize >= kMaxMessageSize) {
      nsPrintfCString warning(
          "The serialized value is too large"
          " (size=%zu bytes, max=%zu bytes).",
          responseSize, kMaxMessageSize);
      NS_WARNING(warning.get());
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }

    MOZ_ASSERT(response.type() != RequestResponse::T__None);

    if (response.type() == RequestResponse::Tnsresult) {
      MOZ_ASSERT(NS_FAILED(response.get_nsresult()));

      return response.get_nsresult();
    }

    if (NS_WARN_IF(
            !PBackgroundIDBRequestParent::Send__delete__(this, response))) {
      IDB_REPORT_INTERNAL_ERR();
      return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
    }
  }

#ifdef DEBUG
  mResponseSent = true;
#endif

  return NS_OK;
}

bool NormalTransactionOp::SendFailureResult(nsresult aResultCode) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_FAILED(aResultCode));

  bool result = false;

  if (!IsActorDestroyed()) {
    result = PBackgroundIDBRequestParent::Send__delete__(
        this, ClampResultCode(aResultCode));
  }

#ifdef DEBUG
  mResponseSent = true;
#endif

  return result;
}

void NormalTransactionOp::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT_IF(!IsActorDestroyed(), mResponseSent);

  TransactionDatabaseOperationBase::Cleanup();
}

void NormalTransactionOp::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnOwningThread();

  NoteActorDestroyed();

  // Assume ActorDestroy can happen at any time, so we can't probe the current
  // state since mInternalState can be modified on any thread (only one thread
  // at a time based on the state machine).
  // However we can use mWaitingForContinue which is only touched on the owning
  // thread.  If mWaitingForContinue is true, we can also modify mInternalState
  // since we are guaranteed that there are no pending runnables which would
  // probe mInternalState to decide what code needs to run (there shouldn't be
  // any running runnables on other threads either).

  if (IsWaitingForContinue()) {
    NoteContinueReceived();
  }

  // We don't have to handle the case when mWaitingForContinue is not true since
  // it means that either nothing has been initialized yet, so nothing to
  // cleanup or there are pending runnables that will detect that the actor has
  // been destroyed and cleanup accordingly.
}

mozilla::ipc::IPCResult NormalTransactionOp::RecvContinue(
    const PreprocessResponse& aResponse) {
  AssertIsOnOwningThread();

  switch (aResponse.type()) {
    case PreprocessResponse::Tnsresult:
      SetFailureCode(aResponse.get_nsresult());
      break;

    case PreprocessResponse::TObjectStoreGetPreprocessResponse:
    case PreprocessResponse::TObjectStoreGetAllPreprocessResponse:
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  NoteContinueReceived();

  return IPC_OK();
}

ObjectStoreAddOrPutRequestOp::ObjectStoreAddOrPutRequestOp(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId,
    RequestParams&& aParams)
    : NormalTransactionOp(std::move(aTransaction), aRequestId),
      mParams(
          std::move(aParams.type() == RequestParams::TObjectStoreAddParams
                        ? aParams.get_ObjectStoreAddParams().commonParams()
                        : aParams.get_ObjectStorePutParams().commonParams())),
      mOriginMetadata(Transaction().GetDatabase().OriginMetadata()),
      mPersistenceType(Transaction().GetDatabase().Type()),
      mOverwrite(aParams.type() == RequestParams::TObjectStorePutParams),
      mObjectStoreMayHaveIndexes(false) {
  MOZ_ASSERT(aParams.type() == RequestParams::TObjectStoreAddParams ||
             aParams.type() == RequestParams::TObjectStorePutParams);

  mMetadata =
      Transaction().GetMetadataForObjectStoreId(mParams.objectStoreId());
  MOZ_ASSERT(mMetadata);

  mObjectStoreMayHaveIndexes = mMetadata->HasLiveIndexes();

  mDataOverThreshold =
      snappy::MaxCompressedLength(mParams.cloneInfo().data().data.Size()) >
      IndexedDatabaseManager::DataThreshold();
}

nsresult ObjectStoreAddOrPutRequestOp::RemoveOldIndexDataValues(
    DatabaseConnection* aConnection) {
  AssertIsOnConnectionThread();
  MOZ_ASSERT(aConnection);
  MOZ_ASSERT(mOverwrite);
  MOZ_ASSERT(!mResponse.IsUnset());

#ifdef DEBUG
  {
    QM_TRY_INSPECT(const bool& hasIndexes,
                   DatabaseOperationBase::ObjectStoreHasIndexes(
                       *aConnection, mParams.objectStoreId()),
                   QM_ASSERT_UNREACHABLE);

    MOZ_ASSERT(hasIndexes,
               "Don't use this slow method if there are no indexes!");
  }
#endif

  QM_TRY_INSPECT(
      const auto& indexValuesStmt,
      aConnection->BorrowAndExecuteSingleStepStatement(
          "SELECT index_data_values "
          "FROM object_data "
          "WHERE object_store_id = :"_ns +
              kStmtParamNameObjectStoreId + " AND key = :"_ns +
              kStmtParamNameKey + ";"_ns,
          [&self = *this](auto& stmt) -> mozilla::Result<Ok, nsresult> {
            QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByName(
                kStmtParamNameObjectStoreId, self.mParams.objectStoreId())));

            QM_TRY(MOZ_TO_RESULT(
                self.mResponse.BindToStatement(&stmt, kStmtParamNameKey)));

            return Ok{};
          }));

  if (indexValuesStmt) {
    QM_TRY_INSPECT(const auto& existingIndexValues,
                   ReadCompressedIndexDataValues(**indexValuesStmt, 0));

    QM_TRY(MOZ_TO_RESULT(
        DeleteIndexDataTableRows(aConnection, mResponse, existingIndexValues)));
  }

  return NS_OK;
}

bool ObjectStoreAddOrPutRequestOp::Init(TransactionBase& aTransaction) {
  AssertIsOnOwningThread();

  const nsTArray<IndexUpdateInfo>& indexUpdateInfos =
      mParams.indexUpdateInfos();

  if (!indexUpdateInfos.IsEmpty()) {
    mUniqueIndexTable.emplace();

    for (const auto& updateInfo : indexUpdateInfos) {
      auto indexMetadata = mMetadata->mIndexes.Lookup(updateInfo.indexId());
      MOZ_ALWAYS_TRUE(indexMetadata);

      MOZ_ASSERT(!(*indexMetadata)->mDeleted);

      const IndexOrObjectStoreId& indexId =
          (*indexMetadata)->mCommonMetadata.id();
      const bool& unique = (*indexMetadata)->mCommonMetadata.unique();

      MOZ_ASSERT(indexId == updateInfo.indexId());
      MOZ_ASSERT_IF(!(*indexMetadata)->mCommonMetadata.multiEntry(),
                    !mUniqueIndexTable.ref().Contains(indexId));

      if (NS_WARN_IF(!mUniqueIndexTable.ref().InsertOrUpdate(indexId, unique,
                                                             fallible))) {
        return false;
      }
    }
  } else if (mOverwrite) {
    mUniqueIndexTable.emplace();
  }

  if (mUniqueIndexTable.isSome()) {
    mUniqueIndexTable.ref().MarkImmutable();
  }

  QM_TRY_UNWRAP(
      mStoredFileInfos,
      TransformIntoNewArray(
          mParams.fileAddInfos(),
          [](const auto& fileAddInfo) {
            MOZ_ASSERT(fileAddInfo.type() == StructuredCloneFileBase::eBlob ||
                       fileAddInfo.type() ==
                           StructuredCloneFileBase::eMutableFile);

            switch (fileAddInfo.type()) {
              case StructuredCloneFileBase::eBlob: {
                PBackgroundIDBDatabaseFileParent* file =
                    fileAddInfo.file().AsParent();
                MOZ_ASSERT(file);

                auto* const fileActor = static_cast<DatabaseFile*>(file);
                MOZ_ASSERT(fileActor);

                return StoredFileInfo::CreateForBlob(
                    fileActor->GetFileInfoPtr(), fileActor);
              }

              default:
                MOZ_CRASH("Should never get here!");
            }
          },
          fallible),
      false);

  if (mDataOverThreshold) {
    auto fileInfo =
        aTransaction.GetDatabase().GetFileManager().CreateFileInfo();
    if (NS_WARN_IF(!fileInfo)) {
      return false;
    }

    mStoredFileInfos.EmplaceBack(StoredFileInfo::CreateForStructuredClone(
        std::move(fileInfo),
        MakeRefPtr<SCInputStream>(mParams.cloneInfo().data().data)));
  }

  return true;
}

nsresult ObjectStoreAddOrPutRequestOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(aConnection->HasStorageConnection());

  AUTO_PROFILER_LABEL("ObjectStoreAddOrPutRequestOp::DoDatabaseWork", DOM);

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  QM_TRY_INSPECT(const bool& objectStoreHasIndexes,
                 ObjectStoreHasIndexes(*aConnection, mParams.objectStoreId(),
                                       mObjectStoreMayHaveIndexes));

  // This will be the final key we use.
  Key& key = mResponse;
  key = mParams.key();

  const bool keyUnset = key.IsUnset();
  const IndexOrObjectStoreId osid = mParams.objectStoreId();

  // First delete old index_data_values if we're overwriting something and we
  // have indexes.
  if (mOverwrite && !keyUnset && objectStoreHasIndexes) {
    QM_TRY(MOZ_TO_RESULT(RemoveOldIndexDataValues(aConnection)));
  }

  int64_t autoIncrementNum = 0;

  {
    // The "|| keyUnset" here is mostly a debugging tool. If a key isn't
    // specified we should never have a collision and so it shouldn't matter
    // if we allow overwrite or not. By not allowing overwrite we raise
    // detectable errors rather than corrupting data.
    const auto optReplaceDirective =
        (!mOverwrite || keyUnset) ? ""_ns : "OR REPLACE "_ns;
    QM_TRY_INSPECT(const auto& stmt,
                   aConnection->BorrowCachedStatement(
                       "INSERT "_ns + optReplaceDirective +
                       "INTO object_data "
                       "(object_store_id, key, file_ids, data) "
                       "VALUES (:"_ns +
                       kStmtParamNameObjectStoreId + ", :"_ns +
                       kStmtParamNameKey + ", :"_ns + kStmtParamNameFileIds +
                       ", :"_ns + kStmtParamNameData + ");"_ns));

    QM_TRY(MOZ_TO_RESULT(
        stmt->BindInt64ByName(kStmtParamNameObjectStoreId, osid)));

    const SerializedStructuredCloneWriteInfo& cloneInfo = mParams.cloneInfo();
    const JSStructuredCloneData& cloneData = cloneInfo.data().data;
    const size_t cloneDataSize = cloneData.Size();

    MOZ_ASSERT(!keyUnset || mMetadata->mCommonMetadata.autoIncrement(),
               "Should have key unless autoIncrement");

    if (mMetadata->mCommonMetadata.autoIncrement()) {
      if (keyUnset) {
        {
          const auto&& lockedAutoIncrementIds =
              mMetadata->mAutoIncrementIds.Lock();

          autoIncrementNum = lockedAutoIncrementIds->next;
        }

        MOZ_ASSERT(autoIncrementNum > 0);

        if (autoIncrementNum > (1LL << 53)) {
          return NS_ERROR_DOM_INDEXEDDB_CONSTRAINT_ERR;
        }

        QM_TRY(key.SetFromInteger(autoIncrementNum));

        // Update index keys if primary key is preserved in child.
        for (auto& updateInfo : mParams.indexUpdateInfos()) {
          updateInfo.value().MaybeUpdateAutoIncrementKey(autoIncrementNum);
        }
      } else if (key.IsFloat()) {
        double numericKey = key.ToFloat();
        numericKey = std::min(numericKey, double(1LL << 53));
        numericKey = floor(numericKey);

        const auto&& lockedAutoIncrementIds =
            mMetadata->mAutoIncrementIds.Lock();
        if (numericKey >= lockedAutoIncrementIds->next) {
          autoIncrementNum = numericKey;
        }
      }

      if (keyUnset && mMetadata->mCommonMetadata.keyPath().IsValid()) {
        const SerializedStructuredCloneWriteInfo& cloneInfo =
            mParams.cloneInfo();
        MOZ_ASSERT(cloneInfo.offsetToKeyProp());
        MOZ_ASSERT(cloneDataSize > sizeof(uint64_t));
        MOZ_ASSERT(cloneInfo.offsetToKeyProp() <=
                   (cloneDataSize - sizeof(uint64_t)));

        // Special case where someone put an object into an autoIncrement'ing
        // objectStore with no key in its keyPath set. We needed to figure out
        // which row id we would get above before we could set that properly.
        uint64_t keyPropValue =
            ReinterpretDoubleAsUInt64(static_cast<double>(autoIncrementNum));

        static const size_t keyPropSize = sizeof(uint64_t);

        char keyPropBuffer[keyPropSize];
        LittleEndian::writeUint64(keyPropBuffer, keyPropValue);

        auto iter = cloneData.Start();
        MOZ_ALWAYS_TRUE(cloneData.Advance(iter, cloneInfo.offsetToKeyProp()));
        MOZ_ALWAYS_TRUE(
            cloneData.UpdateBytes(iter, keyPropBuffer, keyPropSize));
      }
    }

    key.BindToStatement(&*stmt, kStmtParamNameKey);

    if (mDataOverThreshold) {
      // The data we store in the SQLite database is a (signed) 64-bit integer.
      // The flags are left-shifted 32 bits so the max value is 0xFFFFFFFF.
      // The file_ids index occupies the lower 32 bits and its max is
      // 0xFFFFFFFF.
      static const uint32_t kCompressedFlag = (1 << 0);

      uint32_t flags = 0;
      flags |= kCompressedFlag;

      const uint32_t index = mStoredFileInfos.Length() - 1;

      const int64_t data = (uint64_t(flags) << 32) | index;

      QM_TRY(MOZ_TO_RESULT(stmt->BindInt64ByName(kStmtParamNameData, data)));
    } else {
      AutoTArray<char, 4096> flatCloneData;  // 4096 from JSStructuredCloneData
      QM_TRY(OkIf(flatCloneData.SetLength(cloneDataSize, fallible)),
             Err(NS_ERROR_OUT_OF_MEMORY));

      {
        auto iter = cloneData.Start();
        MOZ_ALWAYS_TRUE(
            cloneData.ReadBytes(iter, flatCloneData.Elements(), cloneDataSize));
      }

      // Compress the bytes before adding into the database.
      const char* const uncompressed = flatCloneData.Elements();
      const size_t uncompressedLength = cloneDataSize;

      size_t compressedLength = snappy::MaxCompressedLength(uncompressedLength);

      UniqueFreePtr<char> compressed(
          static_cast<char*>(malloc(compressedLength)));
      if (NS_WARN_IF(!compressed)) {
        return NS_ERROR_OUT_OF_MEMORY;
      }

      snappy::RawCompress(uncompressed, uncompressedLength, compressed.get(),
                          &compressedLength);

      uint8_t* const dataBuffer =
          reinterpret_cast<uint8_t*>(compressed.release());
      const size_t dataBufferLength = compressedLength;

      QM_TRY(MOZ_TO_RESULT(stmt->BindAdoptedBlobByName(
          kStmtParamNameData, dataBuffer, dataBufferLength)));
    }

    if (!mStoredFileInfos.IsEmpty()) {
      // Moved outside the loop to allow it to be cached when demanded by the
      // first write.  (We may have mStoredFileInfos without any required
      // writes.)
      Maybe<FileHelper> fileHelper;
      nsAutoString fileIds;

      for (auto& storedFileInfo : mStoredFileInfos) {
        MOZ_ASSERT(storedFileInfo.IsValid());

        QM_TRY_INSPECT(const auto& inputStream,
                       storedFileInfo.GetInputStream());

        if (inputStream) {
          if (fileHelper.isNothing()) {
            fileHelper.emplace(Transaction().GetDatabase().GetFileManagerPtr());
            QM_TRY(MOZ_TO_RESULT(fileHelper->Init()),
                   NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR,
                   IDB_REPORT_INTERNAL_ERR_LAMBDA);
          }

          const DatabaseFileInfo& fileInfo = storedFileInfo.GetFileInfo();
          const DatabaseFileManager& fileManager = fileInfo.Manager();

          const auto file = fileHelper->GetFile(fileInfo);
          QM_TRY(OkIf(file), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR,
                 IDB_REPORT_INTERNAL_ERR_LAMBDA);

          const auto journalFile = fileHelper->GetJournalFile(fileInfo);
          QM_TRY(OkIf(journalFile), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR,
                 IDB_REPORT_INTERNAL_ERR_LAMBDA);

          nsCString fileKeyId;
          fileKeyId.AppendInt(fileInfo.Id());

          const auto maybeKey =
              fileManager.IsInPrivateBrowsingMode()
                  ? fileManager.MutableCipherKeyManagerRef().Get(fileKeyId)
                  : Nothing();

          QM_TRY(MOZ_TO_RESULT(fileHelper->CreateFileFromStream(
                                   *file, *journalFile, *inputStream,
                                   storedFileInfo.ShouldCompress(), maybeKey))
                     .mapErr([](const nsresult rv) {
                       if (NS_ERROR_GET_MODULE(rv) !=
                           NS_ERROR_MODULE_DOM_INDEXEDDB) {
                         IDB_REPORT_INTERNAL_ERR();
                         return NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR;
                       }
                       return rv;
                     }),
                 QM_PROPAGATE,
                 ([&fileManager, &file = *file,
                   &journalFile = *journalFile](const auto) {
                   // Try to remove the file if the copy failed.
                   QM_TRY(MOZ_TO_RESULT(
                              fileManager.SyncDeleteFile(file, journalFile)),
                          QM_VOID);
                 }));

          storedFileInfo.NotifyWriteSucceeded();
        }

        if (!fileIds.IsEmpty()) {
          fileIds.Append(' ');
        }
        storedFileInfo.Serialize(fileIds);
      }

      QM_TRY(MOZ_TO_RESULT(
          stmt->BindStringByName(kStmtParamNameFileIds, fileIds)));
    } else {
      QM_TRY(MOZ_TO_RESULT(stmt->BindNullByName(kStmtParamNameFileIds)));
    }

    QM_TRY(MOZ_TO_RESULT(stmt->Execute()), QM_PROPAGATE,
           [keyUnset = DebugOnly{keyUnset}](const nsresult rv) {
             if (rv == NS_ERROR_STORAGE_CONSTRAINT) {
               MOZ_ASSERT(!keyUnset, "Generated key had a collision!");
             }
           });
  }

  // Update our indexes if needed.
  if (!mParams.indexUpdateInfos().IsEmpty()) {
    MOZ_ASSERT(mUniqueIndexTable.isSome());

    // Write the index_data_values column.
    QM_TRY_INSPECT(const auto& indexValues,
                   IndexDataValuesFromUpdateInfos(mParams.indexUpdateInfos(),
                                                  mUniqueIndexTable.ref()));

    QM_TRY(
        MOZ_TO_RESULT(UpdateIndexValues(aConnection, osid, key, indexValues)));

    QM_TRY(MOZ_TO_RESULT(
        InsertIndexTableRows(aConnection, osid, key, indexValues)));
  }

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  if (autoIncrementNum) {
    {
      auto&& lockedAutoIncrementIds = mMetadata->mAutoIncrementIds.Lock();

      lockedAutoIncrementIds->next = autoIncrementNum + 1;
    }

    Transaction().NoteModifiedAutoIncrementObjectStore(mMetadata);
  }

  return NS_OK;
}

void ObjectStoreAddOrPutRequestOp::GetResponse(RequestResponse& aResponse,
                                               size_t* aResponseSize) {
  AssertIsOnOwningThread();

  if (mOverwrite) {
    aResponse = ObjectStorePutResponse(mResponse);
    *aResponseSize = mResponse.GetBuffer().Length();
  } else {
    aResponse = ObjectStoreAddResponse(mResponse);
    *aResponseSize = mResponse.GetBuffer().Length();
  }
}

void ObjectStoreAddOrPutRequestOp::Cleanup() {
  AssertIsOnOwningThread();

  mStoredFileInfos.Clear();

  NormalTransactionOp::Cleanup();
}

NS_IMPL_ISUPPORTS(ObjectStoreAddOrPutRequestOp::SCInputStream, nsIInputStream)

NS_IMETHODIMP
ObjectStoreAddOrPutRequestOp::SCInputStream::Close() { return NS_OK; }

NS_IMETHODIMP
ObjectStoreAddOrPutRequestOp::SCInputStream::Available(uint64_t* _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
ObjectStoreAddOrPutRequestOp::SCInputStream::StreamStatus() { return NS_OK; }

NS_IMETHODIMP
ObjectStoreAddOrPutRequestOp::SCInputStream::Read(char* aBuf, uint32_t aCount,
                                                  uint32_t* _retval) {
  return ReadSegments(NS_CopySegmentToBuffer, aBuf, aCount, _retval);
}

NS_IMETHODIMP
ObjectStoreAddOrPutRequestOp::SCInputStream::ReadSegments(
    nsWriteSegmentFun aWriter, void* aClosure, uint32_t aCount,
    uint32_t* _retval) {
  *_retval = 0;

  while (aCount) {
    uint32_t count = std::min(uint32_t(mIter.RemainingInSegment()), aCount);
    if (!count) {
      // We've run out of data in the last segment.
      break;
    }

    uint32_t written;
    nsresult rv =
        aWriter(this, aClosure, mIter.Data(), *_retval, count, &written);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      // InputStreams do not propagate errors to caller.
      return NS_OK;
    }

    // Writer should write what we asked it to write.
    MOZ_ASSERT(written == count);

    *_retval += count;
    aCount -= count;

    if (NS_WARN_IF(!mData.Advance(mIter, count))) {
      // InputStreams do not propagate errors to caller.
      return NS_OK;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
ObjectStoreAddOrPutRequestOp::SCInputStream::IsNonBlocking(bool* _retval) {
  *_retval = false;
  return NS_OK;
}

ObjectStoreGetRequestOp::ObjectStoreGetRequestOp(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId,
    const RequestParams& aParams, bool aGetAll)
    : NormalTransactionOp(std::move(aTransaction), aRequestId),
      mObjectStoreId(aGetAll
                         ? aParams.get_ObjectStoreGetAllParams().objectStoreId()
                         : aParams.get_ObjectStoreGetParams().objectStoreId()),
      mDatabase(Transaction().GetDatabasePtr()),
      mOptionalKeyRange(
          aGetAll ? aParams.get_ObjectStoreGetAllParams().optionalKeyRange()
                  : Some(aParams.get_ObjectStoreGetParams().keyRange())),
      mBackgroundParent(Transaction().GetBackgroundParent()),
      mPreprocessInfoCount(0),
      mLimit(aGetAll ? aParams.get_ObjectStoreGetAllParams().limit() : 1),
      mGetAll(aGetAll) {
  MOZ_ASSERT(aParams.type() == RequestParams::TObjectStoreGetParams ||
             aParams.type() == RequestParams::TObjectStoreGetAllParams);
  MOZ_ASSERT(mObjectStoreId);
  MOZ_ASSERT(mDatabase);
  MOZ_ASSERT_IF(!aGetAll, mOptionalKeyRange.isSome());
  MOZ_ASSERT(mBackgroundParent);
}

template <typename T>
Result<T, nsresult> ObjectStoreGetRequestOp::ConvertResponse(
    StructuredCloneReadInfoParent&& aInfo) {
  T result;

  static_assert(std::is_same_v<T, SerializedStructuredCloneReadInfo> ||
                std::is_same_v<T, PreprocessInfo>);

  if constexpr (std::is_same_v<T, SerializedStructuredCloneReadInfo>) {
    result.data().data = aInfo.ReleaseData();
    result.hasPreprocessInfo() = aInfo.HasPreprocessInfo();
  }

  QM_TRY_UNWRAP(result.files(), SerializeStructuredCloneFiles(
                                    mDatabase, aInfo.Files(),
                                    std::is_same_v<T, PreprocessInfo>));

  return result;
}

nsresult ObjectStoreGetRequestOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT_IF(!mGetAll, mOptionalKeyRange.isSome());
  MOZ_ASSERT_IF(!mGetAll, mLimit == 1);

  AUTO_PROFILER_LABEL("ObjectStoreGetRequestOp::DoDatabaseWork", DOM);

  const nsCString query =
      "SELECT file_ids, data "
      "FROM object_data "
      "WHERE object_store_id = :"_ns +
      kStmtParamNameObjectStoreId +
      MaybeGetBindingClauseForKeyRange(mOptionalKeyRange, kColumnNameKey) +
      " ORDER BY key ASC"_ns +
      (mLimit ? kOpenLimit + IntToCString(mLimit) : EmptyCString());

  QM_TRY_INSPECT(const auto& stmt, aConnection->BorrowCachedStatement(query));

  QM_TRY(MOZ_TO_RESULT(
      stmt->BindInt64ByName(kStmtParamNameObjectStoreId, mObjectStoreId)));

  if (mOptionalKeyRange.isSome()) {
    QM_TRY(MOZ_TO_RESULT(
        BindKeyRangeToStatement(mOptionalKeyRange.ref(), &*stmt)));
  }

  QM_TRY(CollectWhileHasResult(
      *stmt, [this](auto& stmt) mutable -> mozilla::Result<Ok, nsresult> {
        QM_TRY_UNWRAP(auto cloneInfo,
                      GetStructuredCloneReadInfoFromStatement(
                          &stmt, 1, 0, mDatabase->GetFileManager()));

        if (cloneInfo.HasPreprocessInfo()) {
          mPreprocessInfoCount++;
        }

        QM_TRY(OkIf(mResponse.EmplaceBack(fallible, std::move(cloneInfo))),
               Err(NS_ERROR_OUT_OF_MEMORY));

        return Ok{};
      }));

  MOZ_ASSERT_IF(!mGetAll, mResponse.Length() <= 1);

  return NS_OK;
}

bool ObjectStoreGetRequestOp::HasPreprocessInfo() {
  return mPreprocessInfoCount > 0;
}

Result<PreprocessParams, nsresult>
ObjectStoreGetRequestOp::GetPreprocessParams() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(!mResponse.IsEmpty());

  if (mGetAll) {
    auto params = ObjectStoreGetAllPreprocessParams();

    auto& preprocessInfos = params.preprocessInfos();
    if (NS_WARN_IF(
            !preprocessInfos.SetCapacity(mPreprocessInfoCount, fallible))) {
      return Err(NS_ERROR_OUT_OF_MEMORY);
    }

    QM_TRY(TransformIfAbortOnErr(
        std::make_move_iterator(mResponse.begin()),
        std::make_move_iterator(mResponse.end()),
        MakeBackInserter(preprocessInfos),
        [](const auto& info) { return info.HasPreprocessInfo(); },
        [&self = *this](StructuredCloneReadInfoParent&& info) {
          return self.ConvertResponse<PreprocessInfo>(std::move(info));
        }));

    return PreprocessParams{std::move(params)};
  }

  auto params = ObjectStoreGetPreprocessParams();

  QM_TRY_UNWRAP(params.preprocessInfo(),
                ConvertResponse<PreprocessInfo>(std::move(mResponse[0])));

  return PreprocessParams{std::move(params)};
}

void ObjectStoreGetRequestOp::GetResponse(RequestResponse& aResponse,
                                          size_t* aResponseSize) {
  MOZ_ASSERT_IF(mLimit, mResponse.Length() <= mLimit);

  if (mGetAll) {
    aResponse = ObjectStoreGetAllResponse();
    *aResponseSize = 0;

    if (!mResponse.IsEmpty()) {
      QM_TRY_UNWRAP(
          aResponse.get_ObjectStoreGetAllResponse().cloneInfos(),
          TransformIntoNewArrayAbortOnErr(
              std::make_move_iterator(mResponse.begin()),
              std::make_move_iterator(mResponse.end()),
              [this, &aResponseSize](StructuredCloneReadInfoParent&& info) {
                *aResponseSize += info.Size();
                return ConvertResponse<SerializedStructuredCloneReadInfo>(
                    std::move(info));
              },
              fallible),
          QM_VOID, [&aResponse](const nsresult result) { aResponse = result; });
    }

    return;
  }

  aResponse = ObjectStoreGetResponse();
  *aResponseSize = 0;

  if (!mResponse.IsEmpty()) {
    SerializedStructuredCloneReadInfo& serializedInfo =
        aResponse.get_ObjectStoreGetResponse().cloneInfo();

    *aResponseSize += mResponse[0].Size();
    QM_TRY_UNWRAP(serializedInfo,
                  ConvertResponse<SerializedStructuredCloneReadInfo>(
                      std::move(mResponse[0])),
                  QM_VOID,
                  [&aResponse](const nsresult result) { aResponse = result; });
  }
}

ObjectStoreGetKeyRequestOp::ObjectStoreGetKeyRequestOp(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId,
    const RequestParams& aParams, bool aGetAll)
    : NormalTransactionOp(std::move(aTransaction), aRequestId),
      mObjectStoreId(
          aGetAll ? aParams.get_ObjectStoreGetAllKeysParams().objectStoreId()
                  : aParams.get_ObjectStoreGetKeyParams().objectStoreId()),
      mOptionalKeyRange(
          aGetAll ? aParams.get_ObjectStoreGetAllKeysParams().optionalKeyRange()
                  : Some(aParams.get_ObjectStoreGetKeyParams().keyRange())),
      mLimit(aGetAll ? aParams.get_ObjectStoreGetAllKeysParams().limit() : 1),
      mGetAll(aGetAll) {
  MOZ_ASSERT(aParams.type() == RequestParams::TObjectStoreGetKeyParams ||
             aParams.type() == RequestParams::TObjectStoreGetAllKeysParams);
  MOZ_ASSERT(mObjectStoreId);
  MOZ_ASSERT_IF(!aGetAll, mOptionalKeyRange.isSome());
}

nsresult ObjectStoreGetKeyRequestOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("ObjectStoreGetKeyRequestOp::DoDatabaseWork", DOM);

  const nsCString query =
      "SELECT key "
      "FROM object_data "
      "WHERE object_store_id = :"_ns +
      kStmtParamNameObjectStoreId +
      MaybeGetBindingClauseForKeyRange(mOptionalKeyRange, kColumnNameKey) +
      " ORDER BY key ASC"_ns +
      (mLimit ? " LIMIT "_ns + IntToCString(mLimit) : EmptyCString());

  QM_TRY_INSPECT(const auto& stmt, aConnection->BorrowCachedStatement(query));

  nsresult rv =
      stmt->BindInt64ByName(kStmtParamNameObjectStoreId, mObjectStoreId);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (mOptionalKeyRange.isSome()) {
    rv = BindKeyRangeToStatement(mOptionalKeyRange.ref(), &*stmt);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  QM_TRY(CollectWhileHasResult(
      *stmt, [this](auto& stmt) mutable -> mozilla::Result<Ok, nsresult> {
        Key* const key = mResponse.AppendElement(fallible);
        QM_TRY(OkIf(key), Err(NS_ERROR_OUT_OF_MEMORY));
        QM_TRY(MOZ_TO_RESULT(key->SetFromStatement(&stmt, 0)));

        return Ok{};
      }));

  MOZ_ASSERT_IF(!mGetAll, mResponse.Length() <= 1);

  return NS_OK;
}

void ObjectStoreGetKeyRequestOp::GetResponse(RequestResponse& aResponse,
                                             size_t* aResponseSize) {
  MOZ_ASSERT_IF(mLimit, mResponse.Length() <= mLimit);

  if (mGetAll) {
    aResponse = ObjectStoreGetAllKeysResponse();
    *aResponseSize = std::accumulate(mResponse.begin(), mResponse.end(), 0u,
                                     [](size_t old, const auto& entry) {
                                       return old + entry.GetBuffer().Length();
                                     });

    aResponse.get_ObjectStoreGetAllKeysResponse().keys() = std::move(mResponse);

    return;
  }

  aResponse = ObjectStoreGetKeyResponse();
  *aResponseSize = 0;

  if (!mResponse.IsEmpty()) {
    *aResponseSize = mResponse[0].GetBuffer().Length();
    aResponse.get_ObjectStoreGetKeyResponse().key() = std::move(mResponse[0]);
  }
}

ObjectStoreDeleteRequestOp::ObjectStoreDeleteRequestOp(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId,
    const ObjectStoreDeleteParams& aParams)
    : NormalTransactionOp(std::move(aTransaction), aRequestId),
      mParams(aParams),
      mObjectStoreMayHaveIndexes(false) {
  AssertIsOnBackgroundThread();

  SafeRefPtr<FullObjectStoreMetadata> metadata =
      Transaction().GetMetadataForObjectStoreId(mParams.objectStoreId());
  MOZ_ASSERT(metadata);

  mObjectStoreMayHaveIndexes = metadata->HasLiveIndexes();
}

nsresult ObjectStoreDeleteRequestOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  AUTO_PROFILER_LABEL("ObjectStoreDeleteRequestOp::DoDatabaseWork", DOM);

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  QM_TRY_INSPECT(const bool& objectStoreHasIndexes,
                 ObjectStoreHasIndexes(*aConnection, mParams.objectStoreId(),
                                       mObjectStoreMayHaveIndexes));

  if (objectStoreHasIndexes) {
    QM_TRY(MOZ_TO_RESULT(DeleteObjectStoreDataTableRowsWithIndexes(
        aConnection, mParams.objectStoreId(), Some(mParams.keyRange()))));
  } else {
    const auto keyRangeClause =
        GetBindingClauseForKeyRange(mParams.keyRange(), kColumnNameKey);

    QM_TRY(MOZ_TO_RESULT(aConnection->ExecuteCachedStatement(
        "DELETE FROM object_data "
        "WHERE object_store_id = :"_ns +
            kStmtParamNameObjectStoreId + keyRangeClause + ";"_ns,
        [&params = mParams](
            mozIStorageStatement& stmt) -> mozilla::Result<Ok, nsresult> {
          QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByName(kStmtParamNameObjectStoreId,
                                                    params.objectStoreId())));

          QM_TRY(
              MOZ_TO_RESULT(BindKeyRangeToStatement(params.keyRange(), &stmt)));

          return Ok{};
        })));
  }

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  return NS_OK;
}

ObjectStoreClearRequestOp::ObjectStoreClearRequestOp(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId,
    const ObjectStoreClearParams& aParams)
    : NormalTransactionOp(std::move(aTransaction), aRequestId),
      mParams(aParams),
      mObjectStoreMayHaveIndexes(false) {
  AssertIsOnBackgroundThread();

  SafeRefPtr<FullObjectStoreMetadata> metadata =
      Transaction().GetMetadataForObjectStoreId(mParams.objectStoreId());
  MOZ_ASSERT(metadata);

  mObjectStoreMayHaveIndexes = metadata->HasLiveIndexes();
}

nsresult ObjectStoreClearRequestOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("ObjectStoreClearRequestOp::DoDatabaseWork", DOM);

  DatabaseConnection::AutoSavepoint autoSave;
  QM_TRY(MOZ_TO_RESULT(autoSave.Start(Transaction()))
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
             ,
         QM_PROPAGATE, MakeAutoSavepointCleanupHandler(*aConnection)
#endif
  );

  QM_TRY_INSPECT(const bool& objectStoreHasIndexes,
                 ObjectStoreHasIndexes(*aConnection, mParams.objectStoreId(),
                                       mObjectStoreMayHaveIndexes));

  // The parameter names are not used, parameters are bound by index only
  // locally in the same function.
  QM_TRY(MOZ_TO_RESULT(
      objectStoreHasIndexes
          ? DeleteObjectStoreDataTableRowsWithIndexes(
                aConnection, mParams.objectStoreId(), Nothing())
          : aConnection->ExecuteCachedStatement(
                "DELETE FROM object_data "
                "WHERE object_store_id = :object_store_id;"_ns,
                [objectStoreId =
                     mParams.objectStoreId()](mozIStorageStatement& stmt)
                    -> mozilla::Result<Ok, nsresult> {
                  QM_TRY(
                      MOZ_TO_RESULT(stmt.BindInt64ByIndex(0, objectStoreId)));

                  return Ok{};
                })));

  QM_TRY(MOZ_TO_RESULT(autoSave.Commit()));

  return NS_OK;
}

nsresult ObjectStoreCountRequestOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("ObjectStoreCountRequestOp::DoDatabaseWork", DOM);

  const auto keyRangeClause = MaybeGetBindingClauseForKeyRange(
      mParams.optionalKeyRange(), kColumnNameKey);

  QM_TRY_INSPECT(
      const auto& maybeStmt,
      aConnection->BorrowAndExecuteSingleStepStatement(
          "SELECT count(*) "
          "FROM object_data "
          "WHERE object_store_id = :"_ns +
              kStmtParamNameObjectStoreId + keyRangeClause,
          [&params = mParams](auto& stmt) -> mozilla::Result<Ok, nsresult> {
            QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByName(
                kStmtParamNameObjectStoreId, params.objectStoreId())));

            if (params.optionalKeyRange().isSome()) {
              QM_TRY(MOZ_TO_RESULT(BindKeyRangeToStatement(
                  params.optionalKeyRange().ref(), &stmt)));
            }

            return Ok{};
          }));

  QM_TRY(OkIf(maybeStmt.isSome()), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR,
         [](const auto) {
           // XXX Why do we have an assertion here, but not at most other
           // places using IDB_REPORT_INTERNAL_ERR(_LAMBDA)?
           MOZ_ASSERT(false, "This should never be possible!");
           IDB_REPORT_INTERNAL_ERR();
         });

  const auto& stmt = *maybeStmt;

  const int64_t count = stmt->AsInt64(0);
  QM_TRY(OkIf(count >= 0), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR, [](const auto) {
    // XXX Why do we have an assertion here, but not at most other places using
    // IDB_REPORT_INTERNAL_ERR(_LAMBDA)?
    MOZ_ASSERT(false, "This should never be possible!");
    IDB_REPORT_INTERNAL_ERR();
  });

  mResponse.count() = count;

  return NS_OK;
}

// static
SafeRefPtr<FullIndexMetadata> IndexRequestOpBase::IndexMetadataForParams(
    const TransactionBase& aTransaction, const RequestParams& aParams) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParams.type() == RequestParams::TIndexGetParams ||
             aParams.type() == RequestParams::TIndexGetKeyParams ||
             aParams.type() == RequestParams::TIndexGetAllParams ||
             aParams.type() == RequestParams::TIndexGetAllKeysParams ||
             aParams.type() == RequestParams::TIndexCountParams);

  IndexOrObjectStoreId objectStoreId;
  IndexOrObjectStoreId indexId;

  switch (aParams.type()) {
    case RequestParams::TIndexGetParams: {
      const IndexGetParams& params = aParams.get_IndexGetParams();
      objectStoreId = params.objectStoreId();
      indexId = params.indexId();
      break;
    }

    case RequestParams::TIndexGetKeyParams: {
      const IndexGetKeyParams& params = aParams.get_IndexGetKeyParams();
      objectStoreId = params.objectStoreId();
      indexId = params.indexId();
      break;
    }

    case RequestParams::TIndexGetAllParams: {
      const IndexGetAllParams& params = aParams.get_IndexGetAllParams();
      objectStoreId = params.objectStoreId();
      indexId = params.indexId();
      break;
    }

    case RequestParams::TIndexGetAllKeysParams: {
      const IndexGetAllKeysParams& params = aParams.get_IndexGetAllKeysParams();
      objectStoreId = params.objectStoreId();
      indexId = params.indexId();
      break;
    }

    case RequestParams::TIndexCountParams: {
      const IndexCountParams& params = aParams.get_IndexCountParams();
      objectStoreId = params.objectStoreId();
      indexId = params.indexId();
      break;
    }

    default:
      MOZ_CRASH("Should never get here!");
  }

  const SafeRefPtr<FullObjectStoreMetadata> objectStoreMetadata =
      aTransaction.GetMetadataForObjectStoreId(objectStoreId);
  MOZ_ASSERT(objectStoreMetadata);

  SafeRefPtr<FullIndexMetadata> indexMetadata =
      aTransaction.GetMetadataForIndexId(*objectStoreMetadata, indexId);
  MOZ_ASSERT(indexMetadata);

  return indexMetadata;
}

IndexGetRequestOp::IndexGetRequestOp(SafeRefPtr<TransactionBase> aTransaction,
                                     const int64_t aRequestId,
                                     const RequestParams& aParams, bool aGetAll)
    : IndexRequestOpBase(std::move(aTransaction), aRequestId, aParams),
      mDatabase(Transaction().GetDatabasePtr()),
      mOptionalKeyRange(aGetAll
                            ? aParams.get_IndexGetAllParams().optionalKeyRange()
                            : Some(aParams.get_IndexGetParams().keyRange())),
      mBackgroundParent(Transaction().GetBackgroundParent()),
      mLimit(aGetAll ? aParams.get_IndexGetAllParams().limit() : 1),
      mGetAll(aGetAll) {
  MOZ_ASSERT(aParams.type() == RequestParams::TIndexGetParams ||
             aParams.type() == RequestParams::TIndexGetAllParams);
  MOZ_ASSERT(mDatabase);
  MOZ_ASSERT_IF(!aGetAll, mOptionalKeyRange.isSome());
  MOZ_ASSERT(mBackgroundParent);
}

nsresult IndexGetRequestOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT_IF(!mGetAll, mOptionalKeyRange.isSome());
  MOZ_ASSERT_IF(!mGetAll, mLimit == 1);

  AUTO_PROFILER_LABEL("IndexGetRequestOp::DoDatabaseWork", DOM);

  const auto indexTable = mMetadata->mCommonMetadata.unique()
                              ? "unique_index_data "_ns
                              : "index_data "_ns;

  QM_TRY_INSPECT(
      const auto& stmt,
      aConnection->BorrowCachedStatement(
          "SELECT file_ids, data "
          "FROM object_data "
          "INNER JOIN "_ns +
          indexTable +
          "AS index_table "
          "ON object_data.object_store_id = "
          "index_table.object_store_id "
          "AND object_data.key = "
          "index_table.object_data_key "
          "WHERE index_id = :"_ns +
          kStmtParamNameIndexId +
          MaybeGetBindingClauseForKeyRange(mOptionalKeyRange,
                                           kColumnNameValue) +
          (mLimit ? kOpenLimit + IntToCString(mLimit) : EmptyCString())));

  QM_TRY(MOZ_TO_RESULT(stmt->BindInt64ByName(kStmtParamNameIndexId,
                                             mMetadata->mCommonMetadata.id())));

  if (mOptionalKeyRange.isSome()) {
    QM_TRY(MOZ_TO_RESULT(
        BindKeyRangeToStatement(mOptionalKeyRange.ref(), &*stmt)));
  }

  QM_TRY(CollectWhileHasResult(
      *stmt, [this](auto& stmt) mutable -> mozilla::Result<Ok, nsresult> {
        QM_TRY_UNWRAP(auto cloneInfo,
                      GetStructuredCloneReadInfoFromStatement(
                          &stmt, 1, 0, mDatabase->GetFileManager()));

        if (cloneInfo.HasPreprocessInfo()) {
          IDB_WARNING("Preprocessing for indexes not yet implemented!");
          return Err(NS_ERROR_NOT_IMPLEMENTED);
        }

        QM_TRY(OkIf(mResponse.EmplaceBack(fallible, std::move(cloneInfo))),
               Err(NS_ERROR_OUT_OF_MEMORY));

        return Ok{};
      }));

  MOZ_ASSERT_IF(!mGetAll, mResponse.Length() <= 1);

  return NS_OK;
}

// XXX This is more or less a duplicate of ObjectStoreGetRequestOp::GetResponse
void IndexGetRequestOp::GetResponse(RequestResponse& aResponse,
                                    size_t* aResponseSize) {
  MOZ_ASSERT_IF(!mGetAll, mResponse.Length() <= 1);

  auto convertResponse = [this](StructuredCloneReadInfoParent&& info)
      -> mozilla::Result<SerializedStructuredCloneReadInfo, nsresult> {
    SerializedStructuredCloneReadInfo result;

    result.data().data = info.ReleaseData();

    QM_TRY_UNWRAP(result.files(), SerializeStructuredCloneFiles(
                                      mDatabase, info.Files(), false));

    return result;
  };

  if (mGetAll) {
    aResponse = IndexGetAllResponse();
    *aResponseSize = 0;

    if (!mResponse.IsEmpty()) {
      QM_TRY_UNWRAP(
          aResponse.get_IndexGetAllResponse().cloneInfos(),
          TransformIntoNewArrayAbortOnErr(
              std::make_move_iterator(mResponse.begin()),
              std::make_move_iterator(mResponse.end()),
              [convertResponse,
               &aResponseSize](StructuredCloneReadInfoParent&& info) {
                *aResponseSize += info.Size();
                return convertResponse(std::move(info));
              },
              fallible),
          QM_VOID, [&aResponse](const nsresult result) { aResponse = result; });
    }

    return;
  }

  aResponse = IndexGetResponse();
  *aResponseSize = 0;

  if (!mResponse.IsEmpty()) {
    SerializedStructuredCloneReadInfo& serializedInfo =
        aResponse.get_IndexGetResponse().cloneInfo();

    *aResponseSize += mResponse[0].Size();
    QM_TRY_UNWRAP(serializedInfo, convertResponse(std::move(mResponse[0])),
                  QM_VOID,
                  [&aResponse](const nsresult result) { aResponse = result; });
  }
}

IndexGetKeyRequestOp::IndexGetKeyRequestOp(
    SafeRefPtr<TransactionBase> aTransaction, const int64_t aRequestId,
    const RequestParams& aParams, bool aGetAll)
    : IndexRequestOpBase(std::move(aTransaction), aRequestId, aParams),
      mOptionalKeyRange(
          aGetAll ? aParams.get_IndexGetAllKeysParams().optionalKeyRange()
                  : Some(aParams.get_IndexGetKeyParams().keyRange())),
      mLimit(aGetAll ? aParams.get_IndexGetAllKeysParams().limit() : 1),
      mGetAll(aGetAll) {
  MOZ_ASSERT(aParams.type() == RequestParams::TIndexGetKeyParams ||
             aParams.type() == RequestParams::TIndexGetAllKeysParams);
  MOZ_ASSERT_IF(!aGetAll, mOptionalKeyRange.isSome());
}

nsresult IndexGetKeyRequestOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT_IF(!mGetAll, mOptionalKeyRange.isSome());
  MOZ_ASSERT_IF(!mGetAll, mLimit == 1);

  AUTO_PROFILER_LABEL("IndexGetKeyRequestOp::DoDatabaseWork", DOM);

  const bool hasKeyRange = mOptionalKeyRange.isSome();

  const auto indexTable = mMetadata->mCommonMetadata.unique()
                              ? "unique_index_data "_ns
                              : "index_data "_ns;

  const nsCString query =
      "SELECT object_data_key "
      "FROM "_ns +
      indexTable + "WHERE index_id = :"_ns + kStmtParamNameIndexId +
      MaybeGetBindingClauseForKeyRange(mOptionalKeyRange, kColumnNameValue) +
      (mLimit ? kOpenLimit + IntToCString(mLimit) : EmptyCString());

  QM_TRY_INSPECT(const auto& stmt, aConnection->BorrowCachedStatement(query));

  QM_TRY(MOZ_TO_RESULT(stmt->BindInt64ByName(kStmtParamNameIndexId,
                                             mMetadata->mCommonMetadata.id())));

  if (hasKeyRange) {
    QM_TRY(MOZ_TO_RESULT(
        BindKeyRangeToStatement(mOptionalKeyRange.ref(), &*stmt)));
  }

  QM_TRY(CollectWhileHasResult(
      *stmt, [this](auto& stmt) mutable -> mozilla::Result<Ok, nsresult> {
        Key* const key = mResponse.AppendElement(fallible);
        QM_TRY(OkIf(key), Err(NS_ERROR_OUT_OF_MEMORY));
        QM_TRY(MOZ_TO_RESULT(key->SetFromStatement(&stmt, 0)));

        return Ok{};
      }));

  MOZ_ASSERT_IF(!mGetAll, mResponse.Length() <= 1);

  return NS_OK;
}

void IndexGetKeyRequestOp::GetResponse(RequestResponse& aResponse,
                                       size_t* aResponseSize) {
  MOZ_ASSERT_IF(!mGetAll, mResponse.Length() <= 1);

  if (mGetAll) {
    aResponse = IndexGetAllKeysResponse();
    *aResponseSize = std::accumulate(mResponse.begin(), mResponse.end(), 0u,
                                     [](size_t old, const auto& entry) {
                                       return old + entry.GetBuffer().Length();
                                     });

    aResponse.get_IndexGetAllKeysResponse().keys() = std::move(mResponse);

    return;
  }

  aResponse = IndexGetKeyResponse();
  *aResponseSize = 0;

  if (!mResponse.IsEmpty()) {
    *aResponseSize = mResponse[0].GetBuffer().Length();
    aResponse.get_IndexGetKeyResponse().key() = std::move(mResponse[0]);
  }
}

nsresult IndexCountRequestOp::DoDatabaseWork(DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();

  AUTO_PROFILER_LABEL("IndexCountRequestOp::DoDatabaseWork", DOM);

  const auto indexTable = mMetadata->mCommonMetadata.unique()
                              ? "unique_index_data "_ns
                              : "index_data "_ns;

  const auto keyRangeClause = MaybeGetBindingClauseForKeyRange(
      mParams.optionalKeyRange(), kColumnNameValue);

  QM_TRY_INSPECT(
      const auto& maybeStmt,
      aConnection->BorrowAndExecuteSingleStepStatement(
          "SELECT count(*) "
          "FROM "_ns +
              indexTable + "WHERE index_id = :"_ns + kStmtParamNameIndexId +
              keyRangeClause,
          [&self = *this](auto& stmt) -> mozilla::Result<Ok, nsresult> {
            QM_TRY(MOZ_TO_RESULT(stmt.BindInt64ByName(
                kStmtParamNameIndexId, self.mMetadata->mCommonMetadata.id())));

            if (self.mParams.optionalKeyRange().isSome()) {
              QM_TRY(MOZ_TO_RESULT(BindKeyRangeToStatement(
                  self.mParams.optionalKeyRange().ref(), &stmt)));
            }

            return Ok{};
          }));

  QM_TRY(OkIf(maybeStmt.isSome()), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR,
         [](const auto) {
           // XXX Why do we have an assertion here, but not at most other
           // places using IDB_REPORT_INTERNAL_ERR(_LAMBDA)?
           MOZ_ASSERT(false, "This should never be possible!");
           IDB_REPORT_INTERNAL_ERR();
         });

  const auto& stmt = *maybeStmt;

  const int64_t count = stmt->AsInt64(0);
  QM_TRY(OkIf(count >= 0), NS_ERROR_DOM_INDEXEDDB_UNKNOWN_ERR, [](const auto) {
    // XXX Why do we have an assertion here, but not at most other places using
    // IDB_REPORT_INTERNAL_ERR(_LAMBDA)?
    MOZ_ASSERT(false, "This should never be possible!");
    IDB_REPORT_INTERNAL_ERR();
  });

  mResponse.count() = count;

  return NS_OK;
}

template <IDBCursorType CursorType>
bool Cursor<CursorType>::CursorOpBase::SendFailureResult(nsresult aResultCode) {
  AssertIsOnOwningThread();
  MOZ_ASSERT(NS_FAILED(aResultCode));
  MOZ_ASSERT(mCursor);
  MOZ_ASSERT(mCursor->mCurrentlyRunningOp == this);
  MOZ_ASSERT(!mResponseSent);

  if (!IsActorDestroyed()) {
    mResponse = ClampResultCode(aResultCode);

    // This is an expected race when the transaction is invalidated after
    // data is retrieved from database.
    //
    // TODO: There seem to be other cases when mFiles is non-empty here, which
    // have been present before adding cursor preloading, but with cursor
    // preloading they have become more frequent (also during startup). One
    // possible cause with cursor preloading is to be addressed by Bug 1597191.
    NS_WARNING_ASSERTION(
        !mFiles.IsEmpty() && !Transaction().IsInvalidated(),
        "Expected empty mFiles when transaction has not been invalidated");

    // SendResponseInternal will assert when mResponse.type() is
    // CursorResponse::Tnsresult and mFiles is non-empty, so we clear mFiles
    // here.
    mFiles.Clear();

    mCursor->SendResponseInternal(mResponse, mFiles);
  }

#ifdef DEBUG
  mResponseSent = true;
#endif
  return false;
}

template <IDBCursorType CursorType>
void Cursor<CursorType>::CursorOpBase::Cleanup() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mCursor);
  MOZ_ASSERT_IF(!IsActorDestroyed(), mResponseSent);

  mCursor = nullptr;

#ifdef DEBUG
  // A bit hacky but the CursorOp request is not generated in response to a
  // child request like most other database operations. Do this to make our
  // assertions happy.
  NoteActorDestroyed();
#endif

  TransactionDatabaseOperationBase::Cleanup();
}

template <IDBCursorType CursorType>
ResponseSizeOrError
CursorOpBaseHelperBase<CursorType>::PopulateResponseFromStatement(
    mozIStorageStatement* const aStmt, const bool aInitializeResponse,
    Key* const aOptOutSortKey) {
  mOp.Transaction().AssertIsOnConnectionThread();
  MOZ_ASSERT_IF(aInitializeResponse,
                mOp.mResponse.type() == CursorResponse::T__None);
  MOZ_ASSERT_IF(!aInitializeResponse,
                mOp.mResponse.type() != CursorResponse::T__None);
  MOZ_ASSERT_IF(
      mOp.mFiles.IsEmpty() &&
          (mOp.mResponse.type() ==
               CursorResponse::TArrayOfObjectStoreCursorResponse ||
           mOp.mResponse.type() == CursorResponse::TArrayOfIndexCursorResponse),
      aInitializeResponse);

  auto populateResponseHelper = PopulateResponseHelper<CursorType>{mOp};
  auto previousKey = aOptOutSortKey ? std::move(*aOptOutSortKey) : Key{};

  QM_TRY(MOZ_TO_RESULT(populateResponseHelper.GetKeys(aStmt, aOptOutSortKey)));

  // aOptOutSortKey must be set iff the cursor is a unique cursor. For unique
  // cursors, we need to skip records with the same key. The SQL queries
  // currently do not filter these out.
  if (aOptOutSortKey && !previousKey.IsUnset() &&
      previousKey == *aOptOutSortKey) {
    return 0;
  }

  QM_TRY(MOZ_TO_RESULT(
      populateResponseHelper.MaybeGetCloneInfo(aStmt, GetCursor())));

  // CAUTION: It is important that only the part of the function above this
  // comment may fail, and modifications to the data structure (in particular
  // mResponse and mFiles) may only be made below. This is necessary to allow to
  // discard entries that were attempted to be preloaded without causing an
  // inconsistent state.

  if (aInitializeResponse) {
    mOp.mResponse = std::remove_reference_t<
        decltype(populateResponseHelper.GetTypedResponse(&mOp.mResponse))>();
  }

  auto& responses = populateResponseHelper.GetTypedResponse(&mOp.mResponse);
  auto& response = *responses.AppendElement();

  populateResponseHelper.FillKeys(response);
  if constexpr (!CursorTypeTraits<CursorType>::IsKeyOnlyCursor) {
    populateResponseHelper.MaybeFillCloneInfo(response, &mOp.mFiles);
  }

  return populateResponseHelper.GetKeySize(response) +
         populateResponseHelper.MaybeGetCloneInfoSize(response);
}

template <IDBCursorType CursorType>
void CursorOpBaseHelperBase<CursorType>::PopulateExtraResponses(
    mozIStorageStatement* const aStmt, const uint32_t aMaxExtraCount,
    const size_t aInitialResponseSize, const nsACString& aOperation,
    Key* const aOptPreviousSortKey) {
  mOp.AssertIsOnConnectionThread();

  const auto extraCount = [&]() -> uint32_t {
    auto accumulatedResponseSize = aInitialResponseSize;
    uint32_t extraCount = 0;

    do {
      bool hasResult;
      nsresult rv = aStmt->ExecuteStep(&hasResult);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        // In case of a failure on one step, do not attempt to execute further
        // steps, but use the results already populated.

        break;
      }

      if (!hasResult) {
        break;
      }

      // PopulateResponseFromStatement does not modify the data in case of
      // failure, so we can just use the results already populated, and discard
      // any remaining entries, and signal overall success. Probably, future
      // attempts to access the same entry will fail as well, but it might never
      // be accessed by the application.
      QM_TRY_INSPECT(
          const auto& responseSize,
          PopulateResponseFromStatement(aStmt, false, aOptPreviousSortKey),
          extraCount, [](const auto&) {
            // TODO: Maybe disable preloading for this cursor? The problem will
            // probably reoccur on the next attempt, and disabling preloading
            // will reduce latency. However, if some problematic entry will be
            // skipped over, after that it might be fine again. To judge this,
            // the causes for such failures would need to be analyzed more
            // thoroughly. Since this seems to be rare, maybe no further action
            // is necessary at all.
          });

      // Check accumulated size of individual responses and maybe break early.
      accumulatedResponseSize += responseSize;
      if (accumulatedResponseSize > IPC::Channel::kMaximumMessageSize / 2) {
        IDB_LOG_MARK_PARENT_TRANSACTION_REQUEST(
            "PRELOAD: %s: Dropping entries because maximum message size is "
            "exceeded: %" PRIu32 "/%zu bytes",
            "%.0s Dropping too large (%" PRIu32 "/%zu)",
            IDB_LOG_ID_STRING(mOp.mBackgroundChildLoggingId),
            mOp.mTransactionLoggingSerialNumber, mOp.mLoggingSerialNumber,
            PromiseFlatCString(aOperation).get(), extraCount,
            accumulatedResponseSize);

        break;
      }

      // TODO: Do not count entries skipped for unique cursors.
      ++extraCount;
    } while (true);

    return extraCount;
  }();

  IDB_LOG_MARK_PARENT_TRANSACTION_REQUEST(
      "PRELOAD: %s: Number of extra results populated: %" PRIu32 "/%" PRIu32,
      "%.0s Populated (%" PRIu32 "/%" PRIu32 ")",
      IDB_LOG_ID_STRING(mOp.mBackgroundChildLoggingId),
      mOp.mTransactionLoggingSerialNumber, mOp.mLoggingSerialNumber,
      PromiseFlatCString(aOperation).get(), extraCount, aMaxExtraCount);
}

template <IDBCursorType CursorType>
void Cursor<CursorType>::SetOptionalKeyRange(
    const Maybe<SerializedKeyRange>& aOptionalKeyRange, bool* const aOpen) {
  MOZ_ASSERT(aOpen);

  Key localeAwareRangeBound;

  if (aOptionalKeyRange.isSome()) {
    const SerializedKeyRange& range = aOptionalKeyRange.ref();

    const bool lowerBound = !IsIncreasingOrder(mDirection);
    *aOpen =
        !range.isOnly() && (lowerBound ? range.lowerOpen() : range.upperOpen());

    const auto& bound =
        (range.isOnly() || lowerBound) ? range.lower() : range.upper();
    if constexpr (IsIndexCursor) {
      if (this->IsLocaleAware()) {
        // XXX Don't we need to propagate the error?
        QM_TRY_UNWRAP(localeAwareRangeBound,
                      bound.ToLocaleAwareKey(this->mLocale), QM_VOID);
      } else {
        localeAwareRangeBound = bound;
      }
    } else {
      localeAwareRangeBound = bound;
    }
  } else {
    *aOpen = false;
  }

  this->mLocaleAwareRangeBound.init(std::move(localeAwareRangeBound));
}

template <IDBCursorType CursorType>
void ObjectStoreOpenOpHelper<CursorType>::PrepareKeyConditionClauses(
    const nsACString& aDirectionClause, const nsACString& aQueryStart) {
  const bool isIncreasingOrder = IsIncreasingOrder(GetCursor().mDirection);

  nsAutoCString keyRangeClause;
  nsAutoCString continueToKeyRangeClause;
  AppendConditionClause(kStmtParamNameKey, kStmtParamNameCurrentKey,
                        !isIncreasingOrder, false, keyRangeClause);
  AppendConditionClause(kStmtParamNameKey, kStmtParamNameCurrentKey,
                        !isIncreasingOrder, true, continueToKeyRangeClause);

  {
    bool open;
    GetCursor().SetOptionalKeyRange(GetOptionalKeyRange(), &open);

    if (GetOptionalKeyRange().isSome() &&
        !GetCursor().mLocaleAwareRangeBound->IsUnset()) {
      AppendConditionClause(kStmtParamNameKey, kStmtParamNameRangeBound,
                            isIncreasingOrder, !open, keyRangeClause);
      AppendConditionClause(kStmtParamNameKey, kStmtParamNameRangeBound,
                            isIncreasingOrder, !open, continueToKeyRangeClause);
    }
  }

  const nsAutoCString suffix =
      aDirectionClause + kOpenLimit + ":"_ns + kStmtParamNameLimit;

  GetCursor().mContinueQueries.init(
      aQueryStart + keyRangeClause + suffix,
      aQueryStart + continueToKeyRangeClause + suffix);
}

template <IDBCursorType CursorType>
void IndexOpenOpHelper<CursorType>::PrepareIndexKeyConditionClause(
    const nsACString& aDirectionClause,
    const nsLiteralCString& aObjectDataKeyPrefix, nsAutoCString aQueryStart) {
  const bool isIncreasingOrder = IsIncreasingOrder(GetCursor().mDirection);

  {
    bool open;
    GetCursor().SetOptionalKeyRange(GetOptionalKeyRange(), &open);
    if (GetOptionalKeyRange().isSome() &&
        !GetCursor().mLocaleAwareRangeBound->IsUnset()) {
      AppendConditionClause(kColumnNameAliasSortKey, kStmtParamNameRangeBound,
                            isIncreasingOrder, !open, aQueryStart);
    }
  }

  nsCString continueQuery, continueToQuery, continuePrimaryKeyQuery;

  continueToQuery =
      aQueryStart + " AND "_ns +
      GetSortKeyClause(isIncreasingOrder ? ComparisonOperator::GreaterOrEquals
                                         : ComparisonOperator::LessOrEquals,
                       kStmtParamNameCurrentKey);

  switch (GetCursor().mDirection) {
    case IDBCursorDirection::Next:
    case IDBCursorDirection::Prev:
      continueQuery =
          aQueryStart + " AND "_ns +
          GetSortKeyClause(isIncreasingOrder
                               ? ComparisonOperator::GreaterOrEquals
                               : ComparisonOperator::LessOrEquals,
                           kStmtParamNameCurrentKey) +
          " AND ( "_ns +
          GetSortKeyClause(isIncreasingOrder ? ComparisonOperator::GreaterThan
                                             : ComparisonOperator::LessThan,
                           kStmtParamNameCurrentKey) +
          " OR "_ns +
          GetKeyClause(aObjectDataKeyPrefix + "object_data_key"_ns,
                       isIncreasingOrder ? ComparisonOperator::GreaterThan
                                         : ComparisonOperator::LessThan,
                       kStmtParamNameObjectStorePosition) +
          " ) "_ns;

      continuePrimaryKeyQuery =
          aQueryStart +
          " AND ("
          "("_ns +
          GetSortKeyClause(ComparisonOperator::Equals,
                           kStmtParamNameCurrentKey) +
          " AND "_ns +
          GetKeyClause(aObjectDataKeyPrefix + "object_data_key"_ns,
                       isIncreasingOrder ? ComparisonOperator::GreaterOrEquals
                                         : ComparisonOperator::LessOrEquals,
                       kStmtParamNameObjectStorePosition) +
          ") OR "_ns +
          GetSortKeyClause(isIncreasingOrder ? ComparisonOperator::GreaterThan
                                             : ComparisonOperator::LessThan,
                           kStmtParamNameCurrentKey) +
          ")"_ns;
      break;

    case IDBCursorDirection::Nextunique:
    case IDBCursorDirection::Prevunique:
      continueQuery =
          aQueryStart + " AND "_ns +
          GetSortKeyClause(isIncreasingOrder ? ComparisonOperator::GreaterThan
                                             : ComparisonOperator::LessThan,
                           kStmtParamNameCurrentKey);
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  const nsAutoCString suffix =
      aDirectionClause + kOpenLimit + ":"_ns + kStmtParamNameLimit;
  continueQuery += suffix;
  continueToQuery += suffix;
  if (!continuePrimaryKeyQuery.IsEmpty()) {
    continuePrimaryKeyQuery += suffix;
  }

  GetCursor().mContinueQueries.init(std::move(continueQuery),
                                    std::move(continueToQuery),
                                    std::move(continuePrimaryKeyQuery));
}

template <IDBCursorType CursorType>
nsresult CommonOpenOpHelper<CursorType>::ProcessStatementSteps(
    mozIStorageStatement* const aStmt) {
  QM_TRY_INSPECT(const bool& hasResult,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aStmt, ExecuteStep));

  if (!hasResult) {
    SetResponse(void_t{});
    return NS_OK;
  }

  Key previousKey;
  auto* optPreviousKey =
      IsUnique(GetCursor().mDirection) ? &previousKey : nullptr;

  QM_TRY_INSPECT(const auto& responseSize,
                 PopulateResponseFromStatement(aStmt, true, optPreviousKey));

  // The degree to which extra responses on OpenOp can actually be used depends
  // on the parameters of subsequent ContinueOp operations, see also comment in
  // ContinueOp::DoDatabaseWork.
  //
  // TODO: We should somehow evaluate the effects of this. Maybe use a smaller
  // extra count than for ContinueOp?
  PopulateExtraResponses(aStmt, GetCursor().mMaxExtraCount, responseSize,
                         "OpenOp"_ns, optPreviousKey);

  return NS_OK;
}

nsresult OpenOpHelper<IDBCursorType::ObjectStore>::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(GetCursor().mObjectStoreId);

  AUTO_PROFILER_LABEL("Cursor::OpenOp::DoObjectStoreDatabaseWork", DOM);

  const bool usingKeyRange = GetOptionalKeyRange().isSome();

  const nsCString queryStart = "SELECT "_ns + kColumnNameKey +
                               ", file_ids, data "
                               "FROM object_data "
                               "WHERE object_store_id = :"_ns +
                               kStmtParamNameId;

  const auto keyRangeClause =
      DatabaseOperationBase::MaybeGetBindingClauseForKeyRange(
          GetOptionalKeyRange(), kColumnNameKey);

  const auto& directionClause = MakeDirectionClause(GetCursor().mDirection);

  // Note: Changing the number or order of SELECT columns in the query will
  // require changes to CursorOpBase::PopulateResponseFromStatement.
  const nsCString firstQuery = queryStart + keyRangeClause + directionClause +
                               kOpenLimit +
                               IntToCString(1 + GetCursor().mMaxExtraCount);

  QM_TRY_INSPECT(const auto& stmt,
                 aConnection->BorrowCachedStatement(firstQuery));

  QM_TRY(MOZ_TO_RESULT(
      stmt->BindInt64ByName(kStmtParamNameId, GetCursor().mObjectStoreId)));

  if (usingKeyRange) {
    QM_TRY(MOZ_TO_RESULT(DatabaseOperationBase::BindKeyRangeToStatement(
        GetOptionalKeyRange().ref(), &*stmt)));
  }

  // Now we need to make the query for ContinueOp.
  PrepareKeyConditionClauses(directionClause, queryStart);

  return ProcessStatementSteps(&*stmt);
}

nsresult OpenOpHelper<IDBCursorType::ObjectStoreKey>::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(GetCursor().mObjectStoreId);

  AUTO_PROFILER_LABEL("Cursor::OpenOp::DoObjectStoreKeyDatabaseWork", DOM);

  const bool usingKeyRange = GetOptionalKeyRange().isSome();

  const nsCString queryStart = "SELECT "_ns + kColumnNameKey +
                               " FROM object_data "
                               "WHERE object_store_id = :"_ns +
                               kStmtParamNameId;

  const auto keyRangeClause =
      DatabaseOperationBase::MaybeGetBindingClauseForKeyRange(
          GetOptionalKeyRange(), kColumnNameKey);

  const auto& directionClause = MakeDirectionClause(GetCursor().mDirection);

  // Note: Changing the number or order of SELECT columns in the query will
  // require changes to CursorOpBase::PopulateResponseFromStatement.
  const nsCString firstQuery =
      queryStart + keyRangeClause + directionClause + kOpenLimit + "1"_ns;

  QM_TRY_INSPECT(const auto& stmt,
                 aConnection->BorrowCachedStatement(firstQuery));

  QM_TRY(MOZ_TO_RESULT(
      stmt->BindInt64ByName(kStmtParamNameId, GetCursor().mObjectStoreId)));

  if (usingKeyRange) {
    QM_TRY(MOZ_TO_RESULT(DatabaseOperationBase::BindKeyRangeToStatement(
        GetOptionalKeyRange().ref(), &*stmt)));
  }

  // Now we need to make the query to get the next match.
  PrepareKeyConditionClauses(directionClause, queryStart);

  return ProcessStatementSteps(&*stmt);
}

nsresult OpenOpHelper<IDBCursorType::Index>::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(GetCursor().mObjectStoreId);
  MOZ_ASSERT(GetCursor().mIndexId);

  AUTO_PROFILER_LABEL("Cursor::OpenOp::DoIndexDatabaseWork", DOM);

  const bool usingKeyRange = GetOptionalKeyRange().isSome();

  const auto indexTable =
      GetCursor().mUniqueIndex ? "unique_index_data"_ns : "index_data"_ns;

  // The result of MakeColumnPairSelectionList is stored in a local variable,
  // since inlining it into the next statement causes a crash on some Mac OS X
  // builds (see https://bugzilla.mozilla.org/show_bug.cgi?id=1168606#c110).
  const auto columnPairSelectionList = MakeColumnPairSelectionList(
      "index_table.value"_ns, "index_table.value_locale"_ns,
      kColumnNameAliasSortKey, GetCursor().IsLocaleAware());
  const nsCString sortColumnAlias =
      "SELECT "_ns + columnPairSelectionList + ", "_ns;

  const nsAutoCString queryStart = sortColumnAlias +
                                   "index_table.object_data_key, "
                                   "object_data.file_ids, "
                                   "object_data.data "
                                   "FROM "_ns +
                                   indexTable +
                                   " AS index_table "
                                   "JOIN object_data "
                                   "ON index_table.object_store_id = "
                                   "object_data.object_store_id "
                                   "AND index_table.object_data_key = "
                                   "object_data.key "
                                   "WHERE index_table.index_id = :"_ns +
                                   kStmtParamNameId;

  const auto keyRangeClause =
      DatabaseOperationBase::MaybeGetBindingClauseForKeyRange(
          GetOptionalKeyRange(), kColumnNameAliasSortKey);

  nsAutoCString directionClause = " ORDER BY "_ns + kColumnNameAliasSortKey;

  switch (GetCursor().mDirection) {
    case IDBCursorDirection::Next:
    case IDBCursorDirection::Nextunique:
      directionClause.AppendLiteral(" ASC, index_table.object_data_key ASC");
      break;

    case IDBCursorDirection::Prev:
      directionClause.AppendLiteral(" DESC, index_table.object_data_key DESC");
      break;

    case IDBCursorDirection::Prevunique:
      directionClause.AppendLiteral(" DESC, index_table.object_data_key ASC");
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  // Note: Changing the number or order of SELECT columns in the query will
  // require changes to CursorOpBase::PopulateResponseFromStatement.
  const nsCString firstQuery = queryStart + keyRangeClause + directionClause +
                               kOpenLimit +
                               IntToCString(1 + GetCursor().mMaxExtraCount);

  QM_TRY_INSPECT(const auto& stmt,
                 aConnection->BorrowCachedStatement(firstQuery));

  QM_TRY(MOZ_TO_RESULT(
      stmt->BindInt64ByName(kStmtParamNameId, GetCursor().mIndexId)));

  if (usingKeyRange) {
    if (GetCursor().IsLocaleAware()) {
      QM_TRY(MOZ_TO_RESULT(DatabaseOperationBase::BindKeyRangeToStatement(
          GetOptionalKeyRange().ref(), &*stmt, GetCursor().mLocale)));
    } else {
      QM_TRY(MOZ_TO_RESULT(DatabaseOperationBase::BindKeyRangeToStatement(
          GetOptionalKeyRange().ref(), &*stmt)));
    }
  }

  // TODO: At least the last two statements are almost the same in all
  // DoDatabaseWork variants, consider removing this duplication.

  // Now we need to make the query to get the next match.
  PrepareKeyConditionClauses(directionClause, std::move(queryStart));

  return ProcessStatementSteps(&*stmt);
}

nsresult OpenOpHelper<IDBCursorType::IndexKey>::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(GetCursor().mObjectStoreId);
  MOZ_ASSERT(GetCursor().mIndexId);

  AUTO_PROFILER_LABEL("Cursor::OpenOp::DoIndexKeyDatabaseWork", DOM);

  const bool usingKeyRange = GetOptionalKeyRange().isSome();

  const auto table =
      GetCursor().mUniqueIndex ? "unique_index_data"_ns : "index_data"_ns;

  // The result of MakeColumnPairSelectionList is stored in a local variable,
  // since inlining it into the next statement causes a crash on some Mac OS X
  // builds (see https://bugzilla.mozilla.org/show_bug.cgi?id=1168606#c110).
  const auto columnPairSelectionList = MakeColumnPairSelectionList(
      "value"_ns, "value_locale"_ns, kColumnNameAliasSortKey,
      GetCursor().IsLocaleAware());
  const nsCString sortColumnAlias =
      "SELECT "_ns + columnPairSelectionList + ", "_ns;

  const nsAutoCString queryStart = sortColumnAlias +
                                   "object_data_key "
                                   " FROM "_ns +
                                   table + " WHERE index_id = :"_ns +
                                   kStmtParamNameId;

  const auto keyRangeClause =
      DatabaseOperationBase::MaybeGetBindingClauseForKeyRange(
          GetOptionalKeyRange(), kColumnNameAliasSortKey);

  nsAutoCString directionClause = " ORDER BY "_ns + kColumnNameAliasSortKey;

  switch (GetCursor().mDirection) {
    case IDBCursorDirection::Next:
    case IDBCursorDirection::Nextunique:
      directionClause.AppendLiteral(" ASC, object_data_key ASC");
      break;

    case IDBCursorDirection::Prev:
      directionClause.AppendLiteral(" DESC, object_data_key DESC");
      break;

    case IDBCursorDirection::Prevunique:
      directionClause.AppendLiteral(" DESC, object_data_key ASC");
      break;

    default:
      MOZ_CRASH("Should never get here!");
  }

  // Note: Changing the number or order of SELECT columns in the query will
  // require changes to CursorOpBase::PopulateResponseFromStatement.
  const nsCString firstQuery =
      queryStart + keyRangeClause + directionClause + kOpenLimit + "1"_ns;

  QM_TRY_INSPECT(const auto& stmt,
                 aConnection->BorrowCachedStatement(firstQuery));

  QM_TRY(MOZ_TO_RESULT(
      stmt->BindInt64ByName(kStmtParamNameId, GetCursor().mIndexId)));

  if (usingKeyRange) {
    if (GetCursor().IsLocaleAware()) {
      QM_TRY(MOZ_TO_RESULT(DatabaseOperationBase::BindKeyRangeToStatement(
          GetOptionalKeyRange().ref(), &*stmt, GetCursor().mLocale)));
    } else {
      QM_TRY(MOZ_TO_RESULT(DatabaseOperationBase::BindKeyRangeToStatement(
          GetOptionalKeyRange().ref(), &*stmt)));
    }
  }

  // Now we need to make the query to get the next match.
  PrepareKeyConditionClauses(directionClause, std::move(queryStart));

  return ProcessStatementSteps(&*stmt);
}

template <IDBCursorType CursorType>
nsresult Cursor<CursorType>::OpenOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mCursor);
  MOZ_ASSERT(!mCursor->mContinueQueries);

  AUTO_PROFILER_LABEL("Cursor::OpenOp::DoDatabaseWork", DOM);

  auto helper = OpenOpHelper<CursorType>{*this};
  const auto rv = helper.DoDatabaseWork(aConnection);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

template <IDBCursorType CursorType>
nsresult Cursor<CursorType>::CursorOpBase::SendSuccessResult() {
  AssertIsOnOwningThread();
  MOZ_ASSERT(mCursor);
  MOZ_ASSERT(mCursor->mCurrentlyRunningOp == this);
  MOZ_ASSERT(mResponse.type() != CursorResponse::T__None);

  if (IsActorDestroyed()) {
    return NS_ERROR_DOM_INDEXEDDB_ABORT_ERR;
  }

  mCursor->SendResponseInternal(mResponse, mFiles);

#ifdef DEBUG
  mResponseSent = true;
#endif
  return NS_OK;
}

template <IDBCursorType CursorType>
nsresult Cursor<CursorType>::ContinueOp::DoDatabaseWork(
    DatabaseConnection* aConnection) {
  MOZ_ASSERT(aConnection);
  aConnection->AssertIsOnConnectionThread();
  MOZ_ASSERT(mCursor);
  MOZ_ASSERT(mCursor->mObjectStoreId);
  MOZ_ASSERT(!mCursor->mContinueQueries->mContinueQuery.IsEmpty());
  MOZ_ASSERT(!mCursor->mContinueQueries->mContinueToQuery.IsEmpty());
  MOZ_ASSERT(!mCurrentPosition.mKey.IsUnset());

  if constexpr (IsIndexCursor) {
    MOZ_ASSERT_IF(
        mCursor->mDirection == IDBCursorDirection::Next ||
            mCursor->mDirection == IDBCursorDirection::Prev,
        !mCursor->mContinueQueries->mContinuePrimaryKeyQuery.IsEmpty());
    MOZ_ASSERT(mCursor->mIndexId);
    MOZ_ASSERT(!mCurrentPosition.mObjectStoreKey.IsUnset());
  }

  AUTO_PROFILER_LABEL("Cursor::ContinueOp::DoDatabaseWork", DOM);

  // We need to pick a query based on whether or not a key was passed to the
  // continue function. If not we'll grab the next item in the database that
  // is greater than (or less than, if we're running a PREV cursor) the current
  // key. If a key was passed we'll grab the next item in the database that is
  // greater than (or less than, if we're running a PREV cursor) or equal to the
  // key that was specified.
  //
  // TODO: The description above is not complete, it does not take account of
  // ContinuePrimaryKey nor Advance.
  //
  // Note: Changing the number or order of SELECT columns in the query will
  // require changes to CursorOpBase::PopulateResponseFromStatement.

  const uint32_t advanceCount =
      mParams.type() == CursorRequestParams::TAdvanceParams
          ? mParams.get_AdvanceParams().count()
          : 1;
  MOZ_ASSERT(advanceCount > 0);

  bool hasContinueKey = false;
  bool hasContinuePrimaryKey = false;

  auto explicitContinueKey = Key{};

  switch (mParams.type()) {
    case CursorRequestParams::TContinueParams:
      if (!mParams.get_ContinueParams().key().IsUnset()) {
        hasContinueKey = true;
        explicitContinueKey = mParams.get_ContinueParams().key();
      }
      break;
    case CursorRequestParams::TContinuePrimaryKeyParams:
      MOZ_ASSERT(!mParams.get_ContinuePrimaryKeyParams().key().IsUnset());
      MOZ_ASSERT(
          !mParams.get_ContinuePrimaryKeyParams().primaryKey().IsUnset());
      MOZ_ASSERT(mCursor->mDirection == IDBCursorDirection::Next ||
                 mCursor->mDirection == IDBCursorDirection::Prev);
      hasContinueKey = true;
      hasContinuePrimaryKey = true;
      explicitContinueKey = mParams.get_ContinuePrimaryKeyParams().key();
      break;
    case CursorRequestParams::TAdvanceParams:
      break;
    default:
      MOZ_CRASH("Should never get here!");
  }

  // TODO: Whether it makes sense to preload depends on the kind of the
  // subsequent operations, not of the current operation. We could assume that
  // the subsequent operations are:
  // - the same as the current operation (with the same parameter values)
  // - as above, except for Advance, where we assume the count will be 1 on the
  // next call
  // - basic operations (Advance with count 1 or Continue-without-key)
  //
  // For now, we implement the second option for now (which correspond to
  // !hasContinueKey).
  //
  // Based on that, we could in both cases either preload for any assumed
  // subsequent operations, or only for the basic operations. For now, we
  // preload only for an assumed basic operation. Other operations would require
  // more work on the client side for invalidation, and may not make any sense
  // at all.
  const uint32_t maxExtraCount = hasContinueKey ? 0 : mCursor->mMaxExtraCount;

  QM_TRY_INSPECT(const auto& stmt,
                 aConnection->BorrowCachedStatement(
                     mCursor->mContinueQueries->GetContinueQuery(
                         hasContinueKey, hasContinuePrimaryKey)));

  QM_TRY(MOZ_TO_RESULT(stmt->BindUTF8StringByName(
      kStmtParamNameLimit,
      IntToCString(advanceCount + mCursor->mMaxExtraCount))));

  QM_TRY(MOZ_TO_RESULT(stmt->BindInt64ByName(kStmtParamNameId, mCursor->Id())));

  // Bind current key.
  const auto& continueKey =
      hasContinueKey ? explicitContinueKey
                     : mCurrentPosition.GetSortKey(mCursor->IsLocaleAware());
  QM_TRY(MOZ_TO_RESULT(
      continueKey.BindToStatement(&*stmt, kStmtParamNameCurrentKey)));

  // Bind range bound if it is specified.
  if (!mCursor->mLocaleAwareRangeBound->IsUnset()) {
    QM_TRY(MOZ_TO_RESULT(mCursor->mLocaleAwareRangeBound->BindToStatement(
        &*stmt, kStmtParamNameRangeBound)));
  }

  // Bind object store position if duplicates are allowed and we're not
  // continuing to a specific key.
  if constexpr (IsIndexCursor) {
    if (!hasContinueKey && (mCursor->mDirection == IDBCursorDirection::Next ||
                            mCursor->mDirection == IDBCursorDirection::Prev)) {
      QM_TRY(MOZ_TO_RESULT(mCurrentPosition.mObjectStoreKey.BindToStatement(
          &*stmt, kStmtParamNameObjectStorePosition)));
    } else if (hasContinuePrimaryKey) {
      QM_TRY(MOZ_TO_RESULT(
          mParams.get_ContinuePrimaryKeyParams().primaryKey().BindToStatement(
              &*stmt, kStmtParamNameObjectStorePosition)));
    }
  }

  // TODO: Why do we query the records we don't need and skip them here, rather
  // than using a OFFSET clause in the query?
  for (uint32_t index = 0; index < advanceCount; index++) {
    QM_TRY_INSPECT(const bool& hasResult,
                   MOZ_TO_RESULT_INVOKE_MEMBER(&*stmt, ExecuteStep));

    if (!hasResult) {
      mResponse = void_t();
      return NS_OK;
    }
  }

  Key previousKey;
  auto* const optPreviousKey =
      IsUnique(mCursor->mDirection) ? &previousKey : nullptr;

  auto helper = CursorOpBaseHelperBase<CursorType>{*this};
  QM_TRY_INSPECT(const auto& responseSize, helper.PopulateResponseFromStatement(
                                               &*stmt, true, optPreviousKey));

  helper.PopulateExtraResponses(&*stmt, maxExtraCount, responseSize,
                                "ContinueOp"_ns, optPreviousKey);

  return NS_OK;
}

Utils::Utils()
#ifdef DEBUG
    : mActorDestroyed(false)
#endif
{
  AssertIsOnBackgroundThread();
}

Utils::~Utils() { MOZ_ASSERT(mActorDestroyed); }

void Utils::ActorDestroy(ActorDestroyReason aWhy) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

#ifdef DEBUG
  mActorDestroyed = true;
#endif
}

mozilla::ipc::IPCResult Utils::RecvDeleteMe() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mActorDestroyed);

  QM_WARNONLY_TRY(OkIf(PBackgroundIndexedDBUtilsParent::Send__delete__(this)));

  return IPC_OK();
}

mozilla::ipc::IPCResult Utils::RecvGetFileReferences(
    const PersistenceType& aPersistenceType, const nsACString& aOrigin,
    const nsAString& aDatabaseName, const int64_t& aFileId, int32_t* aRefCnt,
    int32_t* aDBRefCnt, bool* aResult) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aRefCnt);
  MOZ_ASSERT(aDBRefCnt);
  MOZ_ASSERT(aResult);
  MOZ_ASSERT(!mActorDestroyed);

  if (NS_WARN_IF(!IndexedDatabaseManager::Get())) {
    return IPC_FAIL(this, "No IndexedDatabaseManager active!");
  }

  if (NS_WARN_IF(!QuotaManager::Get())) {
    return IPC_FAIL(this, "No QuotaManager active!");
  }

  if (NS_WARN_IF(!StaticPrefs::dom_indexedDB_testing())) {
    return IPC_FAIL(this, "IndexedDB is not in testing mode!");
  }

  if (NS_WARN_IF(!IsValidPersistenceType(aPersistenceType))) {
    return IPC_FAIL(this, "PersistenceType is not valid!");
  }

  if (NS_WARN_IF(aOrigin.IsEmpty())) {
    return IPC_FAIL(this, "Origin is empty!");
  }

  if (NS_WARN_IF(aDatabaseName.IsEmpty())) {
    return IPC_FAIL(this, "DatabaseName is empty!");
  }

  if (NS_WARN_IF(aFileId == 0)) {
    return IPC_FAIL(this, "No FileId!");
  }

  nsresult rv =
      DispatchAndReturnFileReferences(aPersistenceType, aOrigin, aDatabaseName,
                                      aFileId, aRefCnt, aDBRefCnt, aResult);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IPC_FAIL(this, "DispatchAndReturnFileReferences failed!");
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult Utils::RecvDoMaintenance(
    DoMaintenanceResolver&& aResolver) {
  AssertIsOnBackgroundThread();

  QM_TRY(MOZ_TO_RESULT(!QuotaManager::IsShuttingDown()),
         ResolveNSResultAndReturn(aResolver));

  QM_TRY(QuotaManager::EnsureCreated(), ResolveNSResultAndReturn(aResolver));

  QuotaClient* quotaClient = QuotaClient::GetInstance();
  QM_TRY(MOZ_TO_RESULT(quotaClient), QM_IPC_FAIL(this));

  quotaClient->DoMaintenance()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [self = RefPtr(this), resolver = std::move(aResolver)](
          const BoolPromise::ResolveOrRejectValue& aValue) {
        if (!self->CanSend()) {
          return;
        }

        if (aValue.IsResolve()) {
          resolver(NS_OK);
        } else {
          resolver(aValue.RejectValue());
        }
      });

  return IPC_OK();
}

#ifdef DEBUG

NS_IMPL_ISUPPORTS(DEBUGThreadSlower, nsIThreadObserver)

NS_IMETHODIMP
DEBUGThreadSlower::OnDispatchedEvent() { MOZ_CRASH("Should never be called!"); }

NS_IMETHODIMP
DEBUGThreadSlower::OnProcessNextEvent(nsIThreadInternal* /* aThread */,
                                      bool /* aMayWait */) {
  return NS_OK;
}

NS_IMETHODIMP
DEBUGThreadSlower::AfterProcessNextEvent(nsIThreadInternal* /* aThread */,
                                         bool /* aEventWasProcessed */) {
  MOZ_ASSERT(kDEBUGThreadSleepMS);

  MOZ_ALWAYS_TRUE(PR_Sleep(PR_MillisecondsToInterval(kDEBUGThreadSleepMS)) ==
                  PR_SUCCESS);
  return NS_OK;
}

#endif  // DEBUG

nsresult FileHelper::Init() {
  MOZ_ASSERT(!IsOnBackgroundThread());

  auto fileDirectory = mFileManager->GetCheckedDirectory();
  if (NS_WARN_IF(!fileDirectory)) {
    return NS_ERROR_FAILURE;
  }

  auto journalDirectory = mFileManager->EnsureJournalDirectory();
  if (NS_WARN_IF(!journalDirectory)) {
    return NS_ERROR_FAILURE;
  }

  DebugOnly<bool> exists;
  MOZ_ASSERT(NS_SUCCEEDED(journalDirectory->Exists(&exists)));
  MOZ_ASSERT(exists);

  DebugOnly<bool> isDirectory;
  MOZ_ASSERT(NS_SUCCEEDED(journalDirectory->IsDirectory(&isDirectory)));
  MOZ_ASSERT(isDirectory);

  mFileDirectory.init(WrapNotNullUnchecked(std::move(fileDirectory)));
  mJournalDirectory.init(WrapNotNullUnchecked(std::move(journalDirectory)));

  return NS_OK;
}

nsCOMPtr<nsIFile> FileHelper::GetFile(const DatabaseFileInfo& aFileInfo) {
  MOZ_ASSERT(!IsOnBackgroundThread());

  return mFileManager->GetFileForId(mFileDirectory->get(), aFileInfo.Id());
}

nsCOMPtr<nsIFile> FileHelper::GetJournalFile(
    const DatabaseFileInfo& aFileInfo) {
  MOZ_ASSERT(!IsOnBackgroundThread());

  return mFileManager->GetFileForId(mJournalDirectory->get(), aFileInfo.Id());
}

nsresult FileHelper::CreateFileFromStream(nsIFile& aFile, nsIFile& aJournalFile,
                                          nsIInputStream& aInputStream,
                                          bool aCompress,
                                          const Maybe<CipherKey>& aMaybeKey) {
  MOZ_ASSERT(!IsOnBackgroundThread());

  QM_TRY_INSPECT(const auto& exists,
                 MOZ_TO_RESULT_INVOKE_MEMBER(aFile, Exists));

  // DOM blobs that are being stored in IDB are cached by calling
  // IDBDatabase::GetOrCreateFileActorForBlob. So if the same DOM blob is stored
  // again under a different key or in a different object store, we just add
  // a new reference instead of creating a new copy (all such stored blobs share
  // the same id).
  // However, it can happen that CreateFileFromStream failed due to quota
  // exceeded error and for some reason the orphaned file couldn't be deleted
  // immediately. Now, if the operation is being repeated, the DOM blob is
  // already cached, so it has the same file id which clashes with the orphaned
  // file. We could do some tricks to restore previous copy loop, but it's safer
  // to just delete the orphaned file and start from scratch.
  // This corner case is partially simulated in test_file_copy_failure.js
  if (exists) {
    QM_TRY_INSPECT(const auto& isFile,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aFile, IsFile));

    QM_TRY(OkIf(isFile), NS_ERROR_FAILURE);

    QM_TRY_INSPECT(const auto& journalExists,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aJournalFile, Exists));

    QM_TRY(OkIf(journalExists), NS_ERROR_FAILURE);

    QM_TRY_INSPECT(const auto& journalIsFile,
                   MOZ_TO_RESULT_INVOKE_MEMBER(aJournalFile, IsFile));

    QM_TRY(OkIf(journalIsFile), NS_ERROR_FAILURE);

    IDB_WARNING("Deleting orphaned file!");

    QM_TRY(MOZ_TO_RESULT(mFileManager->SyncDeleteFile(aFile, aJournalFile)));
  }

  // Create a journal file first.
  QM_TRY(MOZ_TO_RESULT(aJournalFile.Create(nsIFile::NORMAL_FILE_TYPE, 0644)));

  // Now try to copy the stream.
  QM_TRY_UNWRAP(nsCOMPtr<nsIOutputStream> fileOutputStream,
                CreateFileOutputStream(mFileManager->Type(),
                                       mFileManager->OriginMetadata(),
                                       Client::IDB, &aFile));

  AutoTArray<char, kFileCopyBufferSize> buffer;
  const auto actualOutputStream =
      [aCompress, &aMaybeKey, &buffer,
       baseOutputStream =
           std::move(fileOutputStream)]() mutable -> nsCOMPtr<nsIOutputStream> {
    if (aMaybeKey) {
      baseOutputStream =
          MakeRefPtr<EncryptingOutputStream<IndexedDBCipherStrategy>>(
              std::move(baseOutputStream), kEncryptedStreamBlockSize,
              *aMaybeKey);
    }

    if (aCompress) {
      auto snappyOutputStream =
          MakeRefPtr<SnappyCompressOutputStream>(baseOutputStream);

      buffer.SetLength(snappyOutputStream->BlockSize());

      return snappyOutputStream;
    }

    buffer.SetLength(kFileCopyBufferSize);
    return std::move(baseOutputStream);
  }();

  QM_TRY(MOZ_TO_RESULT(SyncCopy(aInputStream, *actualOutputStream,
                                buffer.Elements(), buffer.Length())));

  return NS_OK;
}

class FileHelper::ReadCallback final : public nsIInputStreamCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  ReadCallback()
      : mMutex("ReadCallback::mMutex"),
        mCondVar(mMutex, "ReadCallback::mCondVar"),
        mInputAvailable(false) {}

  NS_IMETHOD
  OnInputStreamReady(nsIAsyncInputStream* aStream) override {
    mozilla::MutexAutoLock autolock(mMutex);

    mInputAvailable = true;
    mCondVar.Notify();

    return NS_OK;
  }

  nsresult AsyncWait(nsIAsyncInputStream* aStream, uint32_t aBufferSize,
                     nsIEventTarget* aTarget) {
    MOZ_ASSERT(aStream);
    mozilla::MutexAutoLock autolock(mMutex);

    nsresult rv = aStream->AsyncWait(this, 0, aBufferSize, aTarget);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    mInputAvailable = false;
    while (!mInputAvailable) {
      mCondVar.Wait();
    }

    return NS_OK;
  }

 private:
  ~ReadCallback() = default;

  mozilla::Mutex mMutex MOZ_UNANNOTATED;
  mozilla::CondVar mCondVar;
  bool mInputAvailable;
};

NS_IMPL_ADDREF(FileHelper::ReadCallback);
NS_IMPL_RELEASE(FileHelper::ReadCallback);

NS_INTERFACE_MAP_BEGIN(FileHelper::ReadCallback)
  NS_INTERFACE_MAP_ENTRY(nsIInputStreamCallback)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIInputStreamCallback)
NS_INTERFACE_MAP_END

nsresult FileHelper::SyncRead(nsIInputStream& aInputStream, char* const aBuffer,
                              const uint32_t aBufferSize,
                              uint32_t* const aRead) {
  MOZ_ASSERT(!IsOnBackgroundThread());

  // Let's try to read, directly.
  nsresult rv = aInputStream.Read(aBuffer, aBufferSize, aRead);
  if (NS_SUCCEEDED(rv) || rv != NS_BASE_STREAM_WOULD_BLOCK) {
    return rv;
  }

  // We need to proceed async.
  nsCOMPtr<nsIAsyncInputStream> asyncStream = do_QueryInterface(&aInputStream);
  if (!asyncStream) {
    return rv;
  }

  if (!mReadCallback) {
    mReadCallback.init(MakeNotNull<RefPtr<ReadCallback>>());
  }

  // We just need any thread with an event loop for receiving the
  // OnInputStreamReady callback. Let's use the I/O thread.
  nsCOMPtr<nsIEventTarget> target =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  MOZ_ASSERT(target);

  rv = (*mReadCallback)->AsyncWait(asyncStream, aBufferSize, target);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return SyncRead(aInputStream, aBuffer, aBufferSize, aRead);
}

nsresult FileHelper::SyncCopy(nsIInputStream& aInputStream,
                              nsIOutputStream& aOutputStream,
                              char* const aBuffer, const uint32_t aBufferSize) {
  MOZ_ASSERT(!IsOnBackgroundThread());

  AUTO_PROFILER_LABEL("FileHelper::SyncCopy", DOM);

  nsresult rv;

  do {
    uint32_t numRead;
    rv = SyncRead(aInputStream, aBuffer, aBufferSize, &numRead);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      break;
    }

    if (!numRead) {
      break;
    }

    uint32_t numWrite;
    rv = aOutputStream.Write(aBuffer, numRead, &numWrite);
    if (rv == NS_ERROR_FILE_NO_DEVICE_SPACE) {
      rv = NS_ERROR_DOM_INDEXEDDB_QUOTA_ERR;
    }
    if (NS_WARN_IF(NS_FAILED(rv))) {
      break;
    }

    if (NS_WARN_IF(numWrite != numRead)) {
      rv = NS_ERROR_FAILURE;
      break;
    }
  } while (true);

  if (NS_SUCCEEDED(rv)) {
    rv = aOutputStream.Flush();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  nsresult rv2 = aOutputStream.Close();
  if (NS_WARN_IF(NS_FAILED(rv2))) {
    return NS_SUCCEEDED(rv) ? rv2 : rv;
  }

  return rv;
}

}  // namespace dom::indexedDB
}  // namespace mozilla

#undef IDB_MOBILE
#undef IDB_DEBUG_LOG
