/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_places.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/glean/PlacesMetrics.h"
#include "mozilla/storage/SQLiteEncryption.h"
#include "mozilla/storage/StoragePathUtil.h"

#include "Database.h"

#include "nsIInterfaceRequestorUtils.h"
#include "nsIFile.h"

#include "nsLocalFile.h"
#include "nsNavBookmarks.h"
#include "nsNavHistory.h"
#include "nsPlacesTables.h"
#include "nsPlacesIndexes.h"
#include "nsPlacesTriggers.h"
#include "nsPlacesMacros.h"
#include "nsVariant.h"
#include "SQLFunctions.h"
#include "ScopedNSSTypes.h"
#include "Helpers.h"
#include "nsFaviconService.h"

#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "prenv.h"
#include "prsystem.h"
#include "nsPrintfCString.h"
#include "mozilla/Components.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozIStorageService.h"
#include "prtime.h"

#include "nsXULAppAPI.h"

// Time between corrupt database backups.
#define RECENT_BACKUP_TIME_MICROSEC (int64_t)86400 * PR_USEC_PER_SEC  // 24H

// Set to the database file name when it was found corrupt by a previous
// maintenance run.
#define PREF_FORCE_DATABASE_REPLACEMENT \
  "places.database.replaceDatabaseOnStartup"

// Whether on corruption we should try to fix the database by cloning it.
#define PREF_DATABASE_CLONEONCORRUPTION "places.database.cloneOnCorruption"

#define PREF_DATABASE_FAVICONS_LASTCORRUPTION \
  "places.database.lastFaviconsCorruptionInDaysFromEpoch"
#define PREF_DATABASE_PLACES_LASTCORRUPTION \
  "places.database.lastPlacesCorruptionInDaysFromEpoch"

// Set to specify the size of the places database growth increments in kibibytes
#define PREF_GROWTH_INCREMENT_KIB "places.database.growthIncrementKiB"

// Set to disable the default robust storage and use volatile, in-memory
// storage without robust transaction flushing guarantees. This makes
// SQLite use much less I/O at the cost of losing data when things crash.
// The pref is only honored if an environment variable is set. The env
// variable is intentionally named something scary to help prevent someone
// from thinking it is a useful performance optimization they should enable.
#define PREF_DISABLE_DURABILITY "places.database.disableDurability"

#define PREF_PREVIEWS_ENABLED "places.previews.enabled"

#define ENV_ALLOW_CORRUPTION \
  "ALLOW_PLACES_DATABASE_TO_LOSE_DATA_AND_BECOME_CORRUPT"

// Maximum size for the WAL file.
// For performance reasons this should be as large as possible, so that more
// transactions can fit into it, and the checkpoint cost is paid less often.
// At the same time, since we use synchronous = NORMAL, an fsync happens only
// at checkpoint time, so we don't want the WAL to grow too much and risk to
// lose all the contained transactions on a crash.
#define DATABASE_MAX_WAL_BYTES 2048000

// Since exceeding the journal limit will cause a truncate, we allow a slightly
// larger limit than DATABASE_MAX_WAL_BYTES to reduce the number of truncates.
// This is the number of bytes the journal can grow over the maximum wal size
// before being truncated.
#define DATABASE_JOURNAL_OVERHEAD_BYTES 2048000

#define BYTES_PER_KIBIBYTE 1024

// This annotation is no longer used & is obsolete, but here for migration.
#define LAST_USED_ANNO "bookmarkPropertiesDialog/folderLastUsed"_ns
// This is key in the meta table that the LAST_USED_ANNO is migrated to.
#define LAST_USED_FOLDERS_META_KEY "places/bookmarks/edit/lastusedfolder"_ns

// We use a fixed title for the mobile root to avoid marking the database as
// corrupt if we can't look up the localized title in the string bundle. Sync
// sets the title to the localized version when it creates the left pane query.
#define MOBILE_ROOT_TITLE "mobile"

// Legacy item annotation used by the old Sync engine.
#define SYNC_PARENT_ANNO "sync/parent"

#define USEC_PER_DAY 86400000000LL

using namespace mozilla;

namespace mozilla::places {

namespace {

////////////////////////////////////////////////////////////////////////////////
//// Helpers

/**
 * Get the filename for a corrupt database.
 */
nsString getCorruptFilename(const nsString& aDbFilename) {
  return aDbFilename + u".corrupt"_ns;
}
/**
 * Get the filename for a recover database.
 */
nsString getRecoverFilename(const nsString& aDbFilename) {
  return aDbFilename + u".recover"_ns;
}

/**
 * Checks whether exists a corrupt database file created not longer than
 * RECENT_BACKUP_TIME_MICROSEC ago.
 */
bool isRecentCorruptFile(const nsCOMPtr<nsIFile>& aCorruptFile) {
  MOZ_ASSERT(NS_IsMainThread());
  bool fileExists = false;
  if (NS_FAILED(aCorruptFile->Exists(&fileExists)) || !fileExists) {
    return false;
  }
  PRTime lastMod = 0;
  return NS_SUCCEEDED(aCorruptFile->GetLastModifiedTime(&lastMod)) &&
         lastMod > 0 && (PR_Now() - lastMod) <= RECENT_BACKUP_TIME_MICROSEC;
}

// Defines the stages in the process of replacing a corrupt database.
enum eCorruptDBReplaceStage : int8_t {
  stage_closing = 0,
  stage_removing,
  stage_reopening,
  stage_replaced,
  stage_cloning,
  stage_cloned,
  stage_count
};

/**
 * Maps a database replacement stage (eCorruptDBReplaceStage) to its string
 * representation.
 */
static constexpr nsLiteralCString sCorruptDBStages[stage_count] = {
    "stage_closing"_ns,  "stage_removing"_ns, "stage_reopening"_ns,
    "stage_replaced"_ns, "stage_cloning"_ns,  "stage_cloned"_ns};

/**
 * Removes a file, optionally adding a suffix to the file name.
 */
void RemoveFileSwallowsErrors(const nsCOMPtr<nsIFile>& aFile,
                              const nsString& aSuffix = u""_ns) {
  nsCOMPtr<nsIFile> file;
  MOZ_ALWAYS_SUCCEEDS(aFile->Clone(getter_AddRefs(file)));
  if (!aSuffix.IsEmpty()) {
    nsAutoString newFileName;
    file->GetLeafName(newFileName);
    newFileName.Append(aSuffix);
    MOZ_ALWAYS_SUCCEEDS(file->SetLeafName(newFileName));
  }
  DebugOnly<nsresult> rv = file->Remove(false);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to remove file.");
}

/**
 * Sets the connection journal mode to one of the JOURNAL_* types.
 *
 * @param aDBConn
 *        The database connection.
 * @param aJournalMode
 *        One of the JOURNAL_* types.
 * @returns the current journal mode.
 * @note this may return a different journal mode than the required one, since
 *       setting it may fail.
 */
enum JournalMode SetJournalMode(nsCOMPtr<mozIStorageConnection>& aDBConn,
                                enum JournalMode aJournalMode) {
  MOZ_ASSERT(NS_IsMainThread());
  nsAutoCString journalMode;
  switch (aJournalMode) {
    default:
      MOZ_FALLTHROUGH_ASSERT("Trying to set an unknown journal mode.");
      // Fall through to the default DELETE journal.
    case JOURNAL_DELETE:
      journalMode.AssignLiteral("delete");
      break;
    case JOURNAL_TRUNCATE:
      journalMode.AssignLiteral("truncate");
      break;
    case JOURNAL_MEMORY:
      journalMode.AssignLiteral("memory");
      break;
    case JOURNAL_WAL:
      journalMode.AssignLiteral("wal");
      break;
  }

  nsCOMPtr<mozIStorageStatement> statement;
  nsAutoCString query(MOZ_STORAGE_UNIQUIFY_QUERY_STR "PRAGMA journal_mode = ");
  query.Append(journalMode);
  aDBConn->CreateStatement(query, getter_AddRefs(statement));
  NS_ENSURE_TRUE(statement, JOURNAL_DELETE);

  bool hasResult = false;
  if (NS_SUCCEEDED(statement->ExecuteStep(&hasResult)) && hasResult &&
      NS_SUCCEEDED(statement->GetUTF8String(0, journalMode))) {
    if (journalMode.EqualsLiteral("delete")) {
      return JOURNAL_DELETE;
    }
    if (journalMode.EqualsLiteral("truncate")) {
      return JOURNAL_TRUNCATE;
    }
    if (journalMode.EqualsLiteral("memory")) {
      return JOURNAL_MEMORY;
    }
    if (journalMode.EqualsLiteral("wal")) {
      return JOURNAL_WAL;
    }
    MOZ_ASSERT(false, "Got an unknown journal mode.");
  }

  return JOURNAL_DELETE;
}

nsresult CreateRoot(nsCOMPtr<mozIStorageConnection>& aDBConn,
                    const nsCString& aRootName, const nsCString& aGuid,
                    const nsCString& titleString, const int32_t position,
                    int64_t& newId) {
  MOZ_ASSERT(NS_IsMainThread());

  // A single creation timestamp for all roots so that the root folder's
  // last modification time isn't earlier than its childrens' creation time.
  static PRTime timestamp = 0;
  if (!timestamp) timestamp = RoundedPRNow();

  // Create a new bookmark folder for the root.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = aDBConn->CreateStatement(
      nsLiteralCString(
          "INSERT INTO moz_bookmarks "
          "(type, position, title, dateAdded, lastModified, guid, parent, "
          "syncChangeCounter, syncStatus) "
          "VALUES (:item_type, :item_position, :item_title,"
          ":date_added, :last_modified, :guid, "
          "IFNULL((SELECT id FROM moz_bookmarks WHERE parent = 0), 0), "
          "1, :sync_status)"),
      getter_AddRefs(stmt));
  if (NS_FAILED(rv)) return rv;

  rv = stmt->BindInt32ByName("item_type"_ns,
                             nsINavBookmarksService::TYPE_FOLDER);
  if (NS_FAILED(rv)) return rv;
  rv = stmt->BindInt32ByName("item_position"_ns, position);
  if (NS_FAILED(rv)) return rv;
  rv = stmt->BindUTF8StringByName("item_title"_ns, titleString);
  if (NS_FAILED(rv)) return rv;
  rv = stmt->BindInt64ByName("date_added"_ns, timestamp);
  if (NS_FAILED(rv)) return rv;
  rv = stmt->BindInt64ByName("last_modified"_ns, timestamp);
  if (NS_FAILED(rv)) return rv;
  rv = stmt->BindUTF8StringByName("guid"_ns, aGuid);
  if (NS_FAILED(rv)) return rv;
  rv = stmt->BindInt32ByName("sync_status"_ns,
                             nsINavBookmarksService::SYNC_STATUS_NEW);
  if (NS_FAILED(rv)) return rv;
  rv = stmt->Execute();
  if (NS_FAILED(rv)) return rv;

  newId = nsNavBookmarks::sLastInsertedItemId;
  return NS_OK;
}

nsresult SetupDurability(nsCOMPtr<mozIStorageConnection>& aDBConn,
                         int32_t aDBPageSize) {
  nsresult rv;
  if (PR_GetEnv(ENV_ALLOW_CORRUPTION) &&
      Preferences::GetBool(PREF_DISABLE_DURABILITY, false)) {
    // Volatile storage was requested. Use the in-memory journal (no
    // filesystem I/O) and don't sync the filesystem after writing.
    SetJournalMode(aDBConn, JOURNAL_MEMORY);
    rv = aDBConn->ExecuteSimpleSQL("PRAGMA synchronous = OFF"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // Be sure to set journal mode after page_size.  WAL would prevent the
    // change otherwise.
    if (JOURNAL_WAL == SetJournalMode(aDBConn, JOURNAL_WAL)) {
      // Set the WAL journal size limit.
      int32_t checkpointPages =
          static_cast<int32_t>(DATABASE_MAX_WAL_BYTES / aDBPageSize);
      nsAutoCString checkpointPragma("PRAGMA wal_autocheckpoint = ");
      checkpointPragma.AppendInt(checkpointPages);
      rv = aDBConn->ExecuteSimpleSQL(checkpointPragma);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      // Ignore errors, if we fail here the database could be considered corrupt
      // and we won't be able to go on, even if it's just matter of a bogus file
      // system.  The default mode (DELETE) will be fine in such a case.
      (void)SetJournalMode(aDBConn, JOURNAL_TRUNCATE);

      // Set synchronous to FULL to ensure maximum data integrity, even in
      // case of crashes or unclean shutdowns.
      rv = aDBConn->ExecuteSimpleSQL("PRAGMA synchronous = FULL"_ns);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // The journal is usually free to grow for performance reasons, but it never
  // shrinks back.  Since the space taken may be problematic, limit its size.
  nsAutoCString journalSizePragma("PRAGMA journal_size_limit = ");
  journalSizePragma.AppendInt(DATABASE_MAX_WAL_BYTES +
                              DATABASE_JOURNAL_OVERHEAD_BYTES);
  (void)aDBConn->ExecuteSimpleSQL(journalSizePragma);

  // Grow places in |growthIncrementKiB| increments to limit fragmentation on
  // disk. By default, it's 5 MB.
  int32_t growthIncrementKiB =
      Preferences::GetInt(PREF_GROWTH_INCREMENT_KIB, 5 * BYTES_PER_KIBIBYTE);
  if (growthIncrementKiB > 0) {
    (void)aDBConn->SetGrowthIncrement(growthIncrementKiB * BYTES_PER_KIBIBYTE,
                                      ""_ns);
  }
  return NS_OK;
}

nsresult AttachDatabase(nsCOMPtr<mozIStorageConnection>& aDBConn,
                        const nsACString& aPath, const nsACString& aName) {
  nsresult rv;
  nsCString path;
  path = aPath;

  bool encryptionEnabled =
      StaticPrefs::security_storage_encryption_sqlite_enabled();
  if (encryptionEnabled) {
    storage::EncryptionStatus encStatus;
    rv = storage::GetDatabaseEncryptionStatus(path, encStatus);
    NS_ENSURE_SUCCESS(rv, rv);

    // Outside-profile DBs attach unencrypted. In-profile attach targets always
    // pre-exist, so load the DEK (never create one).
    if (encStatus == storage::EncryptionStatus::Encrypted) {
      nsCString dbKey;
      rv = storage::GetEncryptionKey(path, storage::OpenIntent::LoadExisting,
                                     dbKey);
      NS_ENSURE_SUCCESS(rv, rv);

      storage::PreparePathForURI(path);
      path = nsPrintfCString("file:%s?key=%s", path.get(), dbKey.get());
    }
  }

  nsCOMPtr<mozIStorageStatement> stmt;
  rv = aDBConn->CreateStatement("ATTACH DATABASE :path AS "_ns + aName,
                                getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = stmt->BindUTF8StringByName("path"_ns, path);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  // The journal limit must be set apart for each database.
  nsAutoCString journalSizePragma("PRAGMA favicons.journal_size_limit = ");
  journalSizePragma.AppendInt(DATABASE_MAX_WAL_BYTES +
                              DATABASE_JOURNAL_OVERHEAD_BYTES);
  (void)aDBConn->ExecuteSimpleSQL(journalSizePragma);

  return NS_OK;
}

PRTime GetNow() {
  nsNavHistory* history = nsNavHistory::GetHistoryService();
  PRTime now;
  if (history) {
    // Optimization to avoid calling PR_Now() too often.
    now = history->GetNow();
  } else {
    now = PR_Now();
  }
  return now;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
//// Database

PLACES_FACTORY_SINGLETON_IMPLEMENTATION(Database, gDatabase)

NS_IMPL_ISUPPORTS(Database, nsIObserver, nsISupportsWeakReference)

Database::Database()
    : mMainThreadStatements(mMainConn),
      mMainThreadAsyncStatements(mMainConn),
      mAsyncThreadStatements(mMainConn),
      mDBPageSize(0),
      mDatabaseStatus(nsINavHistoryService::DATABASE_STATUS_OK),
      mClosed(false),
      mClientsShutdown(new ClientsShutdownBlocker()),
      mConnectionShutdown(new ConnectionShutdownBlocker(this)),
      mMaxUrlLength(0),
      mCacheObservers(TOPIC_PLACES_INIT_COMPLETE),
      mRootId(-1),
      mMenuRootId(-1),
      mTagsRootId(-1),
      mUnfiledRootId(-1),
      mToolbarRootId(-1),
      mMobileRootId(-1) {
  MOZ_ASSERT(!XRE_IsContentProcess(),
             "Cannot instantiate Places in the content process");
  // Attempting to create two instances of the service?
  MOZ_ASSERT(!gDatabase);
  gDatabase = this;
}

already_AddRefed<nsIAsyncShutdownClient>
Database::GetProfileChangeTeardownPhase() {
  nsCOMPtr<nsIAsyncShutdownService> asyncShutdownSvc =
      components::AsyncShutdown::Service();
  MOZ_ASSERT(asyncShutdownSvc);
  if (NS_WARN_IF(!asyncShutdownSvc)) {
    return nullptr;
  }

  // Consumers of Places should shutdown before us, at profile-change-teardown.
  nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase;
  DebugOnly<nsresult> rv =
      asyncShutdownSvc->GetProfileChangeTeardown(getter_AddRefs(shutdownPhase));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  return shutdownPhase.forget();
}

already_AddRefed<nsIAsyncShutdownClient>
Database::GetProfileBeforeChangePhase() {
  nsCOMPtr<nsIAsyncShutdownService> asyncShutdownSvc =
      components::AsyncShutdown::Service();
  MOZ_ASSERT(asyncShutdownSvc);
  if (NS_WARN_IF(!asyncShutdownSvc)) {
    return nullptr;
  }

  // Consumers of Places should shutdown before us, at profile-change-teardown.
  nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase;
  DebugOnly<nsresult> rv =
      asyncShutdownSvc->GetProfileBeforeChange(getter_AddRefs(shutdownPhase));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  return shutdownPhase.forget();
}

Database::~Database() = default;

already_AddRefed<mozIStorageAsyncStatement> Database::GetAsyncStatement(
    const nsACString& aQuery) {
  if (PlacesShutdownBlocker::sIsStarted || NS_FAILED(EnsureConnection())) {
    return nullptr;
  }

  MOZ_ASSERT(NS_IsMainThread());
  return mMainThreadAsyncStatements.GetCachedStatement(aQuery);
}

already_AddRefed<mozIStorageStatement> Database::GetStatement(
    const nsACString& aQuery) {
  if (PlacesShutdownBlocker::sIsStarted) {
    return nullptr;
  }
  if (NS_IsMainThread()) {
    if (NS_FAILED(EnsureConnection())) {
      return nullptr;
    }
    return mMainThreadStatements.GetCachedStatement(aQuery);
  }
  // In the async case, the connection must have been started on the main-thread
  // already.
  MOZ_ASSERT(mMainConn);
  return mAsyncThreadStatements.GetCachedStatement(aQuery);
}

already_AddRefed<nsIAsyncShutdownClient> Database::GetClientsShutdown() {
  if (mClientsShutdown) return mClientsShutdown->GetClient();
  return nullptr;
}

already_AddRefed<nsIAsyncShutdownClient> Database::GetConnectionShutdown() {
  if (mConnectionShutdown) return mConnectionShutdown->GetClient();
  return nullptr;
}

// static
already_AddRefed<Database> Database::GetDatabase() {
  if (PlacesShutdownBlocker::sIsStarted) {
    return nullptr;
  }
  return GetSingleton();
}

nsresult Database::Init() {
  MOZ_ASSERT(NS_IsMainThread());

  // DO NOT FAIL HERE, otherwise we would never break the cycle between this
  // object and the shutdown blockers, causing unexpected leaks.

  {
    // First of all Places clients should block profile-change-teardown.
    nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase =
        GetProfileChangeTeardownPhase();
    MOZ_ASSERT(shutdownPhase);
    if (shutdownPhase) {
      nsresult rv = shutdownPhase->AddBlocker(
          static_cast<nsIAsyncShutdownBlocker*>(mClientsShutdown.get()),
          NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__, u""_ns);
      if (NS_FAILED(rv)) {
        // Might occur if we're already shutting down, see bug#1753165
        PlacesShutdownBlocker::sIsStarted = true;
        NS_WARNING("Cannot add shutdown blocker for profile-change-teardown");
      }
    }
  }

  {
    // Then connection closing should block profile-before-change.
    nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase =
        GetProfileBeforeChangePhase();
    MOZ_ASSERT(shutdownPhase);
    if (shutdownPhase) {
      nsresult rv = shutdownPhase->AddBlocker(
          static_cast<nsIAsyncShutdownBlocker*>(mConnectionShutdown.get()),
          NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__, u""_ns);
      if (NS_FAILED(rv)) {
        // Might occur if we're already shutting down, see bug#1753165
        PlacesShutdownBlocker::sIsStarted = true;
        NS_WARNING("Cannot add shutdown blocker for profile-before-change");
      }
    }
  }

  // Finally observe profile shutdown notifications.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    (void)os->AddObserver(this, TOPIC_PROFILE_CHANGE_TEARDOWN, true);
  }
  return NS_OK;
}

nsresult Database::EnsureConnection() {
  // Run this only once.
  if (mMainConn ||
      mDatabaseStatus == nsINavHistoryService::DATABASE_STATUS_LOCKED) {
    return NS_OK;
  }
  // Don't try to create a database too late.
  if (PlacesShutdownBlocker::sIsStarted) {
    return NS_ERROR_FAILURE;
  }

  MOZ_ASSERT(NS_IsMainThread(),
             "Database initialization must happen on the main-thread");

  {
    bool initSucceeded = false;
    auto notify = MakeScopeExit([&]() {
      // If the database connection cannot be opened, it may just be locked
      // by third parties.  Set a locked state.
      if (!initSucceeded) {
        mMainConn = nullptr;
        mDatabaseStatus = nsINavHistoryService::DATABASE_STATUS_LOCKED;
      }
      // Notify at the next tick, to avoid re-entrancy problems.
      NS_DispatchToMainThread(
          NewRunnableMethod("places::Database::EnsureConnection()", this,
                            &Database::NotifyConnectionInitalized));
    });

    nsCOMPtr<mozIStorageService> storage =
        do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
    NS_ENSURE_STATE(storage);

    nsCOMPtr<nsIFile> profileDir;
    nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                         getter_AddRefs(profileDir));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsIFile> databaseFile;
    rv = profileDir->Clone(getter_AddRefs(databaseFile));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = databaseFile->Append(DATABASE_FILENAME);
    NS_ENSURE_SUCCESS(rv, rv);
    bool databaseExisted = false;
    rv = databaseFile->Exists(&databaseExisted);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoString corruptDbName;
    if (NS_SUCCEEDED(Preferences::GetString(PREF_FORCE_DATABASE_REPLACEMENT,
                                            corruptDbName)) &&
        !corruptDbName.IsEmpty()) {
      // If this pref is set, maintenance required a database replacement, due
      // to integrity corruption. Be sure to clear the pref to avoid handling it
      // more than once.
      (void)Preferences::ClearUser(PREF_FORCE_DATABASE_REPLACEMENT);

      // The database is corrupt, backup and replace it with a new one.
      nsCOMPtr<nsIFile> fileToBeReplaced;
      bool fileExists = false;
      if (NS_SUCCEEDED(profileDir->Clone(getter_AddRefs(fileToBeReplaced))) &&
          NS_SUCCEEDED(fileToBeReplaced->Exists(&fileExists)) && fileExists) {
        rv = BackupAndReplaceDatabaseFile(storage, corruptDbName, true, false);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }

    // Open the database file.  If it does not exist a new one will be created.
    // Use an unshared connection, it will consume more memory but avoid shared
    // cache contentions across threads.
    rv = storage->OpenUnsharedDatabase(databaseFile,
                                       mozIStorageService::CONNECTION_DEFAULT,
                                       getter_AddRefs(mMainConn));
    if (rv == NS_ERROR_STORAGE_IOERR || rv == NS_ERROR_FILE_IS_LOCKED ||
        rv == NS_ERROR_STORAGE_BUSY) {
      // Transient lock or I/O error (e.g. network filesystem, AV software):
      // retry once before giving up.
      rv = storage->OpenUnsharedDatabase(databaseFile,
                                         mozIStorageService::CONNECTION_DEFAULT,
                                         getter_AddRefs(mMainConn));
    }
    if (NS_SUCCEEDED(rv) && !databaseExisted) {
      mDatabaseStatus = nsINavHistoryService::DATABASE_STATUS_CREATE;
    } else if (rv == NS_ERROR_FILE_CORRUPTED) {
      // Set places last corruption time in prefs for troubleshooting.
      CheckedInt<int32_t> daysSinceEpoch = GetNow() / USEC_PER_DAY;
      if (daysSinceEpoch.isValid()) {
        Preferences::SetInt(PREF_DATABASE_PLACES_LASTCORRUPTION,
                            daysSinceEpoch.value());
      }
      // The database is corrupt, backup and replace it with a new one.
      rv = BackupAndReplaceDatabaseFile(storage, DATABASE_FILENAME, true, true);
      // Fallback to catch-all handler.
    }
    NS_ENSURE_SUCCESS(rv, rv);

    // Initialize the database schema.  In case of failure the existing schema
    // is is corrupt or incoherent, thus the database should be replaced.
    bool databaseMigrated = false;
    rv = SetupDatabaseConnection(storage);
    bool shouldTryToCloneDb = true;
    if (NS_SUCCEEDED(rv)) {
      // Failing to initialize the schema may indicate a corruption.
      rv = InitSchema(&databaseMigrated);
      if (NS_FAILED(rv)) {
        // Cloning the db on a schema migration may not be a good idea, since we
        // may end up cloning the schema problems.
        shouldTryToCloneDb = false;
        if (rv == NS_ERROR_STORAGE_BUSY || rv == NS_ERROR_FILE_IS_LOCKED ||
            rv == NS_ERROR_FILE_NO_DEVICE_SPACE ||
            rv == NS_ERROR_OUT_OF_MEMORY) {
          // The database is not corrupt, though some migration step failed.
          // This may be caused by concurrent use of sync and async Storage APIs
          // or by a system issue.
          // The best we can do is trying again. If it should still fail, Places
          // won't work properly and will be handled as LOCKED.
          rv = InitSchema(&databaseMigrated);
          if (NS_FAILED(rv)) {
            rv = NS_ERROR_FILE_IS_LOCKED;
          }
        } else {
          rv = NS_ERROR_FILE_CORRUPTED;
        }
      }
    }
    if (NS_WARN_IF(NS_FAILED(rv))) {
      if (rv != NS_ERROR_FILE_IS_LOCKED) {
        mDatabaseStatus = nsINavHistoryService::DATABASE_STATUS_CORRUPT;
      }
      // Some errors may not indicate a database corruption, for those cases we
      // just bail out without throwing away a possibly valid places.sqlite.
      if (rv == NS_ERROR_FILE_CORRUPTED) {
        // Set places and favicons last corruption time in prefs for
        // troubleshooting.
        CheckedInt<int32_t> daysSinceEpoch = GetNow() / USEC_PER_DAY;
        if (daysSinceEpoch.isValid()) {
          Preferences::SetInt(PREF_DATABASE_PLACES_LASTCORRUPTION,
                              daysSinceEpoch.value());
          Preferences::SetInt(PREF_DATABASE_FAVICONS_LASTCORRUPTION,
                              daysSinceEpoch.value());
        }

        // Since we don't know which database is corrupt, we must replace both.
        rv = BackupAndReplaceDatabaseFile(storage, DATABASE_FAVICONS_FILENAME,
                                          false, false);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = BackupAndReplaceDatabaseFile(storage, DATABASE_FILENAME,
                                          shouldTryToCloneDb, true);
        NS_ENSURE_SUCCESS(rv, rv);
        // Try to initialize the new database again.
        rv = SetupDatabaseConnection(storage);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = InitSchema(&databaseMigrated);
      }
      // Bail out if we couldn't fix the database.
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (databaseMigrated) {
      mDatabaseStatus = nsINavHistoryService::DATABASE_STATUS_UPGRADED;
    }

    // Initialize here all the items that are not part of the on-disk database,
    // like views, temp triggers or temp tables.  The database should not be
    // considered corrupt if any of the following fails.

    rv = InitTempEntities();
    NS_ENSURE_SUCCESS(rv, rv);

    rv = CheckRoots();
    NS_ENSURE_SUCCESS(rv, rv);

    initSucceeded = true;
  }
  return NS_OK;
}

nsresult Database::NotifyConnectionInitalized() {
  MOZ_ASSERT(NS_IsMainThread());
  // Notify about Places initialization.
  nsCOMArray<nsIObserver> entries;
  mCacheObservers.GetEntries(entries);
  for (int32_t idx = 0; idx < entries.Count(); ++idx) {
    MOZ_ALWAYS_SUCCEEDS(
        entries[idx]->Observe(nullptr, TOPIC_PLACES_INIT_COMPLETE, nullptr));
  }
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    MOZ_ALWAYS_SUCCEEDS(
        obs->NotifyObservers(nullptr, TOPIC_PLACES_INIT_COMPLETE, nullptr));
  }
  return NS_OK;
}

nsresult Database::EnsureFaviconsDatabaseAttached(
    const nsCOMPtr<mozIStorageService>& aStorage) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIFile> databaseFile;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                         getter_AddRefs(databaseFile));
  NS_ENSURE_STATE(databaseFile);
  nsresult rv = databaseFile->Append(DATABASE_FAVICONS_FILENAME);
  NS_ENSURE_SUCCESS(rv, rv);
  nsString iconsPath;
  rv = databaseFile->GetPath(iconsPath);
  NS_ENSURE_SUCCESS(rv, rv);

  bool fileExists = false;
  if (NS_SUCCEEDED(databaseFile->Exists(&fileExists)) && fileExists) {
    return AttachDatabase(mMainConn, NS_ConvertUTF16toUTF8(iconsPath),
                          DATABASE_FAVICONS_SCHEMANAME);
  }

  // Open the database file, this will also create it.
  nsCOMPtr<mozIStorageConnection> conn;
  rv = aStorage->OpenUnsharedDatabase(databaseFile,
                                      mozIStorageService::CONNECTION_DEFAULT,
                                      getter_AddRefs(conn));
  NS_ENSURE_SUCCESS(rv, rv);

  {
    // Ensure we'll close the connection when done.
    auto cleanup = MakeScopeExit([&]() {
      // We cannot use AsyncClose() here, because by the time we try to ATTACH
      // this database, its transaction could be still be running and that would
      // cause the ATTACH query to fail.
      MOZ_ALWAYS_TRUE(NS_SUCCEEDED(conn->Close()));
    });

    // Enable incremental vacuum for this database. Since it will contain even
    // large blobs and can be cleared with history, it's worth to have it.
    // Note that it will be necessary to manually use PRAGMA incremental_vacuum.
    rv = conn->ExecuteSimpleSQL("PRAGMA auto_vacuum = INCREMENTAL"_ns);
    NS_ENSURE_SUCCESS(rv, rv);

#if !defined(HAVE_64BIT_BUILD)
    // Ensure that temp tables are held in memory, not on disk, on 32 bit
    // platforms.
    rv = conn->ExecuteSimpleSQL("PRAGMA temp_store = MEMORY"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
#endif

    int32_t defaultPageSize;
    rv = conn->GetDefaultPageSize(&defaultPageSize);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = SetupDurability(conn, defaultPageSize);
    NS_ENSURE_SUCCESS(rv, rv);

    // We are going to update the database, so everything from now on should be
    // in a transaction for performances.
    mozStorageTransaction transaction(conn, false);
    // XXX Handle the error, bug 1696133.
    (void)NS_WARN_IF(NS_FAILED(transaction.Start()));
    rv = conn->ExecuteSimpleSQL(CREATE_MOZ_ICONS);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = conn->ExecuteSimpleSQL(CREATE_IDX_MOZ_ICONS_ICONURLHASH);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = conn->ExecuteSimpleSQL(CREATE_MOZ_PAGES_W_ICONS);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = conn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PAGES_W_ICONS_ICONURLHASH);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = conn->ExecuteSimpleSQL(CREATE_MOZ_ICONS_TO_PAGES);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = conn->ExecuteSimpleSQL(CREATE_IDX_MOZ_ICONS_TO_PAGES_ICON(""));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = transaction.Commit();
    NS_ENSURE_SUCCESS(rv, rv);

    // The scope exit will take care of closing the connection.
  }

  rv = AttachDatabase(mMainConn, NS_ConvertUTF16toUTF8(iconsPath),
                      DATABASE_FAVICONS_SCHEMANAME);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::BackupAndReplaceDatabaseFile(
    nsCOMPtr<mozIStorageService>& aStorage, const nsString& aDbFilename,
    bool aTryToClone, bool aReopenConnection) {
  MOZ_ASSERT(NS_IsMainThread());

  if (aDbFilename.Equals(DATABASE_FILENAME)) {
    mDatabaseStatus = nsINavHistoryService::DATABASE_STATUS_CORRUPT;
  } else {
    // Due to OS file lockings, attached databases can't be cloned properly,
    // otherwise trying to reattach them later would fail.
    aTryToClone = false;
  }

  nsCOMPtr<nsIFile> profDir;
  nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(profDir));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIFile> databaseFile;
  rv = profDir->Clone(getter_AddRefs(databaseFile));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = databaseFile->Append(aDbFilename);
  NS_ENSURE_SUCCESS(rv, rv);

  // If we already failed in the last 24 hours avoid to create another corrupt
  // file, since doing so, in some situation, could cause us to create a new
  // corrupt file at every try to access any Places service.  That is bad
  // because it would quickly fill the user's disk space without any notice.
  nsCOMPtr<nsIFile> corruptFile;
  rv = profDir->Clone(getter_AddRefs(corruptFile));
  NS_ENSURE_SUCCESS(rv, rv);
  nsString corruptFilename = getCorruptFilename(aDbFilename);
  rv = corruptFile->Append(corruptFilename);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!isRecentCorruptFile(corruptFile)) {
    // Ensure we never create more than one corrupt file.
    nsCOMPtr<nsIFile> corruptFile;
    rv = profDir->Clone(getter_AddRefs(corruptFile));
    NS_ENSURE_SUCCESS(rv, rv);
    nsString corruptFilename = getCorruptFilename(aDbFilename);
    rv = corruptFile->Append(corruptFilename);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = corruptFile->Remove(false);
    if (NS_FAILED(rv) && rv != NS_ERROR_FILE_NOT_FOUND) {
      return rv;
    }

    nsCOMPtr<nsIFile> backup;
    (void)BackupDatabaseFile(databaseFile, corruptFilename, profDir,
                             getter_AddRefs(backup));
  }

  // If anything fails from this point on, we have a stale connection or
  // database file, and there's not much more we can do.
  // The only thing we can try to do is to replace the database on the next
  // startup, and report the problem through telemetry.
  {
    eCorruptDBReplaceStage stage = stage_closing;
    auto guard = MakeScopeExit([&]() {
      // In case we failed to close the connection or remove the database file,
      // we want to try again at the next startup.
      if (stage == stage_closing || stage == stage_removing) {
        Preferences::SetString(PREF_FORCE_DATABASE_REPLACEMENT, aDbFilename);
      }
      // Report the corruption through telemetry.
      glean::places::places_database_corruption_handling_stage
          .Get(NS_ConvertUTF16toUTF8(aDbFilename))
          .Set(sCorruptDBStages[stage]);
    });

    // Close database connection if open.
    if (mMainConn) {
      rv = mMainConn->SpinningSynchronousClose();
      NS_ENSURE_SUCCESS(rv, rv);
      mMainConn = nullptr;
    }

    // Remove the broken database.
    stage = stage_removing;
    rv = databaseFile->Remove(false);
    if (NS_FAILED(rv) && rv != NS_ERROR_FILE_NOT_FOUND) {
      return rv;
    }

    // Create a new database file and try to clone tables from the corrupt one.
    bool cloned = false;
    if (aTryToClone &&
        Preferences::GetBool(PREF_DATABASE_CLONEONCORRUPTION, true)) {
      stage = stage_cloning;
      rv = TryToCloneTablesFromCorruptDatabase(aStorage, databaseFile);
      if (NS_SUCCEEDED(rv)) {
        // If we cloned successfully, we should not consider the database
        // corrupt anymore, otherwise we could reimport default bookmarks.
        mDatabaseStatus = nsINavHistoryService::DATABASE_STATUS_OK;
        cloned = true;
      }
    }

    if (aReopenConnection) {
      // Use an unshared connection, it will consume more memory but avoid
      // shared cache contentions across threads.
      stage = stage_reopening;
      rv = aStorage->OpenUnsharedDatabase(
          databaseFile, mozIStorageService::CONNECTION_DEFAULT,
          getter_AddRefs(mMainConn));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    stage = cloned ? stage_cloned : stage_replaced;
  }

  return NS_OK;
}

nsresult Database::TryToCloneTablesFromCorruptDatabase(
    const nsCOMPtr<mozIStorageService>& aStorage,
    const nsCOMPtr<nsIFile>& aDatabaseFile) {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoString filename;
  nsresult rv = aDatabaseFile->GetLeafName(filename);

  nsCOMPtr<nsIFile> corruptFile;
  rv = aDatabaseFile->Clone(getter_AddRefs(corruptFile));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = corruptFile->SetLeafName(getCorruptFilename(filename));
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoString path;
  rv = corruptFile->GetPath(path);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIFile> recoverFile;
  rv = aDatabaseFile->Clone(getter_AddRefs(recoverFile));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = recoverFile->SetLeafName(getRecoverFilename(filename));
  NS_ENSURE_SUCCESS(rv, rv);
  // Ensure there's no previous recover file.
  rv = recoverFile->Remove(false);
  if (NS_FAILED(rv) && rv != NS_ERROR_FILE_NOT_FOUND) {
    return rv;
  }

  nsCOMPtr<mozIStorageConnection> conn;
  auto guard = MakeScopeExit([&]() {
    if (conn) {
      (void)conn->Close();
    }
    RemoveFileSwallowsErrors(recoverFile);
  });

  rv = aStorage->OpenUnsharedDatabase(recoverFile,
                                      mozIStorageService::CONNECTION_DEFAULT,
                                      getter_AddRefs(conn));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = AttachDatabase(conn, NS_ConvertUTF16toUTF8(path), "corrupt"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  mozStorageTransaction transaction(conn, false);

  // XXX Handle the error, bug 1696133.
  (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

  // Copy the schema version.
  nsCOMPtr<mozIStorageStatement> stmt;
  (void)conn->CreateStatement("PRAGMA corrupt.user_version"_ns,
                              getter_AddRefs(stmt));
  NS_ENSURE_TRUE(stmt, NS_ERROR_OUT_OF_MEMORY);
  bool hasResult;
  rv = stmt->ExecuteStep(&hasResult);
  NS_ENSURE_SUCCESS(rv, rv);
  int32_t schemaVersion = stmt->AsInt32(0);
  rv = conn->SetSchemaVersion(schemaVersion);
  NS_ENSURE_SUCCESS(rv, rv);

  // Recreate the tables.
  rv = conn->CreateStatement(
      nsLiteralCString(
          "SELECT name, sql FROM corrupt.sqlite_master "
          "WHERE type = 'table' AND name BETWEEN 'moz_' AND 'moza'"),
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);
  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    nsAutoCString name;
    rv = stmt->GetUTF8String(0, name);
    NS_ENSURE_SUCCESS(rv, rv);
    nsAutoCString query;
    rv = stmt->GetUTF8String(1, query);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = conn->ExecuteSimpleSQL(query);
    NS_ENSURE_SUCCESS(rv, rv);
    // Copy the table contents.
    rv = conn->ExecuteSimpleSQL("INSERT INTO main."_ns + name +
                                " SELECT * FROM corrupt."_ns + name);
    if (NS_FAILED(rv)) {
      rv = conn->ExecuteSimpleSQL("INSERT INTO main."_ns + name +
                                  " SELECT * FROM corrupt."_ns + name +
                                  " ORDER BY rowid DESC"_ns);
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Recreate the indices.  Doing this after data addition is faster.
  rv = conn->CreateStatement(
      nsLiteralCString(
          "SELECT sql FROM corrupt.sqlite_master "
          "WHERE type <> 'table' AND name BETWEEN 'moz_' AND 'moza'"),
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);
  hasResult = false;
  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    nsAutoCString query;
    rv = stmt->GetUTF8String(0, query);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = conn->ExecuteSimpleSQL(query);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  rv = stmt->Finalize();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ALWAYS_SUCCEEDS(conn->Close());
  conn = nullptr;
  rv = recoverFile->RenameTo(nullptr, filename);
  NS_ENSURE_SUCCESS(rv, rv);

  RemoveFileSwallowsErrors(corruptFile);
  RemoveFileSwallowsErrors(corruptFile, u"-wal"_ns);
  RemoveFileSwallowsErrors(corruptFile, u"-shm"_ns);

  guard.release();
  return NS_OK;
}

nsresult Database::SetupDatabaseConnection(
    nsCOMPtr<mozIStorageService>& aStorage) {
  MOZ_ASSERT(NS_IsMainThread());

  // Using immediate transactions allows the main connection to retry writes
  // that fail with `SQLITE_BUSY` because a cloned connection has locked the
  // database for writing.
  nsresult rv = mMainConn->SetDefaultTransactionType(
      mozIStorageConnection::TRANSACTION_IMMEDIATE);
  NS_ENSURE_SUCCESS(rv, rv);

  // WARNING: any statement executed before setting the journal mode must be
  // finalized, since SQLite doesn't allow changing the journal mode if there
  // is any outstanding statement.

  {
    // Get the page size.  This may be different than the default if the
    // database file already existed with a different page size.
    nsCOMPtr<mozIStorageStatement> statement;
    rv = mMainConn->CreateStatement(
        nsLiteralCString(MOZ_STORAGE_UNIQUIFY_QUERY_STR "PRAGMA page_size"),
        getter_AddRefs(statement));
    NS_ENSURE_SUCCESS(rv, rv);
    bool hasResult = false;
    rv = statement->ExecuteStep(&hasResult);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(hasResult, NS_ERROR_FILE_CORRUPTED);
    rv = statement->GetInt32(0, &mDBPageSize);
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_TRUE(mDBPageSize > 0, NS_ERROR_FILE_CORRUPTED);
  }

#if !defined(HAVE_64BIT_BUILD)
  // Ensure that temp tables are held in memory, not on disk, on 32 bit
  // platforms.
  rv = mMainConn->ExecuteSimpleSQL(nsLiteralCString(
      MOZ_STORAGE_UNIQUIFY_QUERY_STR "PRAGMA temp_store = MEMORY"));
  NS_ENSURE_SUCCESS(rv, rv);
#endif

  rv = SetupDurability(mMainConn, mDBPageSize);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString busyTimeoutPragma("PRAGMA busy_timeout = ");
  busyTimeoutPragma.AppendInt(DATABASE_BUSY_TIMEOUT_MS);
  (void)mMainConn->ExecuteSimpleSQL(busyTimeoutPragma);

  // Enable FOREIGN KEY support. This is a strict requirement.
  rv = mMainConn->ExecuteSimpleSQL(nsLiteralCString(
      MOZ_STORAGE_UNIQUIFY_QUERY_STR "PRAGMA foreign_keys = ON"));
  NS_ENSURE_SUCCESS(rv, rv);
#ifdef DEBUG
  {
    // There are a few cases where setting foreign_keys doesn't work:
    //  * in the middle of a multi-statement transaction
    //  * if the SQLite library in use doesn't support them
    // Since we need foreign_keys, let's at least assert in debug mode.
    nsCOMPtr<mozIStorageStatement> stmt;
    mMainConn->CreateStatement("PRAGMA foreign_keys"_ns, getter_AddRefs(stmt));
    bool hasResult = false;
    if (stmt && NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
      int32_t fkState = stmt->AsInt32(0);
      MOZ_ASSERT(fkState, "Foreign keys should be enabled");
    }
  }
#endif

  // Note: attaching new databases may require updating `ConcurrentConnection`.

  // Attach the favicons database to the main connection.
  rv = EnsureFaviconsDatabaseAttached(aStorage);
  if (NS_FAILED(rv)) {
    if (rv != NS_ERROR_FILE_CORRUPTED) {
      // The failure is not due to corruption; retry once before giving up.
      rv = EnsureFaviconsDatabaseAttached(aStorage);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      // The favicons database is corrupt.
      // Set last corruption time in prefs for troubleshooting.
      CheckedInt<int32_t> daysSinceEpoch = GetNow() / USEC_PER_DAY;
      if (daysSinceEpoch.isValid()) {
        Preferences::SetInt(PREF_DATABASE_FAVICONS_LASTCORRUPTION,
                            daysSinceEpoch.value());
      }

      // Replace and reattach it.
      nsCOMPtr<nsIFile> iconsFile;
      rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                  getter_AddRefs(iconsFile));
      NS_ENSURE_SUCCESS(rv, rv);
      rv = iconsFile->Append(DATABASE_FAVICONS_FILENAME);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = iconsFile->Remove(false);
      if (NS_FAILED(rv) && rv != NS_ERROR_FILE_NOT_FOUND) {
        return rv;
      }
      rv = EnsureFaviconsDatabaseAttached(aStorage);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // Create favicons temp entities.
  rv = mMainConn->ExecuteSimpleSQL(CREATE_ICONS_AFTERINSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  // We use our functions during migration, so initialize them now.
  rv = InitFunctions(mMainConn);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::InitSchema(bool* aDatabaseMigrated) {
  MOZ_ASSERT(NS_IsMainThread());
  *aDatabaseMigrated = false;

  // Get the database schema version.
  int32_t currentSchemaVersion;
  nsresult rv = mMainConn->GetSchemaVersion(&currentSchemaVersion);
  NS_ENSURE_SUCCESS(rv, rv);
  bool databaseInitialized = currentSchemaVersion > 0;

  if (databaseInitialized &&
      currentSchemaVersion == nsINavHistoryService::DATABASE_SCHEMA_VERSION) {
    // The database is up to date and ready to go.
    return NS_OK;
  }

  // We are going to update the database, so everything from now on should be in
  // a transaction for performances.
  mozStorageTransaction transaction(mMainConn, false);

  // XXX Handle the error, bug 1696133.
  (void)NS_WARN_IF(NS_FAILED(transaction.Start()));

  if (databaseInitialized) {
    // Migration How-to:
    //
    // 1. increment PLACES_SCHEMA_VERSION.
    // 2. implement a method that performs upgrade to your version from the
    //    previous one.
    //
    // NOTE: The downgrade process is pretty much complicated by the fact old
    //       versions cannot know what a new version is going to implement.
    //       The only thing we will do for downgrades is setting back the schema
    //       version, so that next upgrades will run again the migration step.

    if (currentSchemaVersion < nsINavHistoryService::DATABASE_SCHEMA_VERSION) {
      *aDatabaseMigrated = true;

      if (currentSchemaVersion < 52) {
        // These are versions older than Firefox 68 ESR that are not supported
        // anymore.  In this case it's safer to just replace the database.
        return NS_ERROR_FILE_CORRUPTED;
      }

      // Firefox 62 uses schema version 52.
      // Firefox 68 uses schema version 52. - This is an ESR.

      if (currentSchemaVersion < 53) {
        rv = MigrateV53Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 69 uses schema version 53
      // Firefox 72 is a watershed release.

      if (currentSchemaVersion < 54) {
        rv = MigrateV54Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 81 uses schema version 54

      if (currentSchemaVersion < 55) {
        rv = MigrateV55Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      if (currentSchemaVersion < 56) {
        rv = MigrateV56Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      if (currentSchemaVersion < 57) {
        rv = MigrateV57Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 91 uses schema version 57

      // The schema 58 migration is no longer needed.

      // Firefox 92 uses schema version 58

      // The schema 59 migration is no longer needed.

      // Firefox 94 uses schema version 59

      if (currentSchemaVersion < 60) {
        rv = MigrateV60Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 96 uses schema version 60

      if (currentSchemaVersion < 61) {
        rv = MigrateV61Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // The schema 62 migration is no longer needed.

      // Firefox 97 uses schema version 62

      // The schema 63 migration is no longer needed.

      // Firefox 98 uses schema version 63

      // The schema 64 migration is no longer needed.

      // Firefox 99 uses schema version 64

      // The schema 65 migration is no longer needed.

      // The schema 66 migration is no longer needed.

      // Firefox 100 uses schema version 66

      if (currentSchemaVersion < 67) {
        rv = MigrateV67Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // The schema 68 migration is no longer needed.

      // Firefox 103 uses schema version 68

      if (currentSchemaVersion < 69) {
        rv = MigrateV69Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 104 uses schema version 69

      if (currentSchemaVersion < 70) {
        rv = MigrateV70Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      if (currentSchemaVersion < 71) {
        rv = MigrateV71Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 110 uses schema version 71

      if (currentSchemaVersion < 72) {
        rv = MigrateV72Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 111 uses schema version 72

      if (currentSchemaVersion < 73) {
        rv = MigrateV73Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 114  uses schema version 73

      if (currentSchemaVersion < 74) {
        rv = MigrateV74Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 115  uses schema version 74

      if (currentSchemaVersion < 75) {
        rv = MigrateV75Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 118 uses schema version 75

      // Version 76 was not correctly invoked and thus removed.

      if (currentSchemaVersion < 77) {
        rv = MigrateV77Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 125 uses schema version 77

      if (currentSchemaVersion < 78) {
        rv = MigrateV78Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 132 uses schema version 78

      if (currentSchemaVersion < 79) {
        rv = MigrateV79Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 140 uses schema version 79

      if (currentSchemaVersion < 80) {
        rv = MigrateV80Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 140 uses schema version 80

      if (currentSchemaVersion < 81) {
        rv = MigrateV81Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      if (currentSchemaVersion < 82) {
        rv = MigrateV82Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 141 uses schema version 82

      if (currentSchemaVersion < 83) {
        rv = MigrateV83Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // The schema 84 migration was the same as 85, we had to re-run it to
      // correct issues with origin frecency.

      // Firefox 147 uses schema version 84

      if (currentSchemaVersion < 85) {
        rv = MigrateV85Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 148 uses schema version 85

      if (currentSchemaVersion < 86) {
        rv = MigrateV86Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 150 uses schema version 86

      if (currentSchemaVersion < 87) {
        rv = MigrateV87Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 155 uses schema version 87

      if (currentSchemaVersion < 88) {
        rv = MigrateV88Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 160 uses schema version 88

      if (currentSchemaVersion < 89) {
        rv = MigrateV89Up();
        NS_ENSURE_SUCCESS(rv, rv);
      }

      // Firefox 162 uses schema version 89

      // Schema Upgrades must add migration code here.
      // >>> IMPORTANT! <<<
      // NEVER MIX UP SYNC AND ASYNC EXECUTION IN MIGRATORS, YOU MAY LOCK THE
      // CONNECTION AND CAUSE FURTHER STEPS TO FAIL.
      // In case, set a bool and do the async work in the ScopeExit guard just
      // before the migration steps.
    }
  } else {
    // This is a new database, so we have to create all the tables and indices.

    // moz_origins.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_ORIGINS);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_places.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_EXTRA);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_URL_HASH);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_REVHOST);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_VISITCOUNT);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_FRECENCY);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_LASTVISITDATE);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_GUID);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_ORIGIN_ID);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_ALT_FRECENCY);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_historyvisits.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS_EXTRA);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_PLACEDATE);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_FROMVISIT);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_HISTORYVISITS_VISITDATE);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_inputhistory.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_INPUTHISTORY);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_bookmarks.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_BOOKMARKS);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_BOOKMARKS_DELETED);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_BOOKMARKS_PLACETYPE);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_BOOKMARKS_PARENTPOSITION);
    NS_ENSURE_SUCCESS(rv, rv);
    rv =
        mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_BOOKMARKS_PLACELASTMODIFIED);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_BOOKMARKS_DATEADDED);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_BOOKMARKS_GUID);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_keywords.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_KEYWORDS);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_KEYWORDS_PLACEPOSTDATA);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_anno_attributes.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_ANNO_ATTRIBUTES);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_annos.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_ANNOS);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_ANNOS_PLACEATTRIBUTE);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_items_annos.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_ITEMS_ANNOS);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_ITEMSANNOS_PLACEATTRIBUTE);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_meta.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_META);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_places_metadata
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_METADATA);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(
        CREATE_IDX_MOZ_PLACES_METADATA_PLACECREATED);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_METADATA_REFERRER);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_places_metadata_search_queries
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_METADATA_SEARCH_QUERIES);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_previews_tombstones
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PREVIEWS_TOMBSTONES);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_newtab_story
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_NEWTAB_STORY_CLICK);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_NEWTAB_STORY_IMPRESSION);
    NS_ENSURE_SUCCESS(rv, rv);
    // Add newtab_story timestamp index.
    rv = mMainConn->ExecuteSimpleSQL(
        CREATE_IDX_MOZ_NEWTAB_STORY_CLICK_TIMESTAMP);
    NS_ENSURE_SUCCESS(rv, rv);
    rv =
        mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_NEWTAB_IMPRESSION_TIMESTAMP);
    NS_ENSURE_SUCCESS(rv, rv);

    // moz_newtab_shortcuts_interaction
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_NEWTAB_SHORTCUTS_INTERACTION);
    NS_ENSURE_SUCCESS(rv, rv);
    // Add moz_newtab_shortcuts_interaction timestamp index.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_NEWTAB_SHORTCUTS_TIMESTAMP);
    NS_ENSURE_SUCCESS(rv, rv);
    // Add moz_newtab_shortcuts_interaction place_id index
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_NEWTAB_SHORTCUTS_PLACEID);
    NS_ENSURE_SUCCESS(rv, rv);

    // The favicons database may predate this one.
    rv = mMainConn->ExecuteSimpleSQL(
        CREATE_IDX_MOZ_ICONS_TO_PAGES_ICON("favicons."));
    NS_ENSURE_SUCCESS(rv, rv);

    // The bookmarks roots get initialized in CheckRoots().
  }

  // Set the schema version to the current one.
  rv = mMainConn->SetSchemaVersion(
      nsINavHistoryService::DATABASE_SCHEMA_VERSION);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = transaction.Commit();
  NS_ENSURE_SUCCESS(rv, rv);

  // ANY FAILURE IN THIS METHOD WILL CAUSE US TO MARK THE DATABASE AS CORRUPT
  // AND TRY TO REPLACE IT.
  // DO NOT PUT HERE ANYTHING THAT IS NOT RELATED TO INITIALIZATION OR MODIFYING
  // THE DISK DATABASE.

  return NS_OK;
}

nsresult Database::CheckRoots() {
  MOZ_ASSERT(NS_IsMainThread());

  // If the database has just been created, skip straight to the part where
  // we create the roots.
  if (mDatabaseStatus == nsINavHistoryService::DATABASE_STATUS_CREATE) {
    return EnsureBookmarkRoots(0, /* shouldReparentRoots */ false);
  }

  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      nsLiteralCString("SELECT guid, id, position, parent FROM moz_bookmarks "
                       "WHERE guid IN ( "
                       "'" ROOT_GUID "', '" MENU_ROOT_GUID
                       "', '" TOOLBAR_ROOT_GUID "', "
                       "'" TAGS_ROOT_GUID "', '" UNFILED_ROOT_GUID
                       "', '" MOBILE_ROOT_GUID "' )"),
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  nsAutoCString guid;
  int32_t maxPosition = 0;
  bool shouldReparentRoots = false;
  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    rv = stmt->GetUTF8String(0, guid);
    NS_ENSURE_SUCCESS(rv, rv);

    int64_t parentId = stmt->AsInt64(3);

    if (guid.EqualsLiteral(ROOT_GUID)) {
      mRootId = stmt->AsInt64(1);
      shouldReparentRoots |= parentId != 0;
    } else {
      maxPosition = std::max(stmt->AsInt32(2), maxPosition);

      if (guid.EqualsLiteral(MENU_ROOT_GUID)) {
        mMenuRootId = stmt->AsInt64(1);
      } else if (guid.EqualsLiteral(TOOLBAR_ROOT_GUID)) {
        mToolbarRootId = stmt->AsInt64(1);
      } else if (guid.EqualsLiteral(TAGS_ROOT_GUID)) {
        mTagsRootId = stmt->AsInt64(1);
      } else if (guid.EqualsLiteral(UNFILED_ROOT_GUID)) {
        mUnfiledRootId = stmt->AsInt64(1);
      } else if (guid.EqualsLiteral(MOBILE_ROOT_GUID)) {
        mMobileRootId = stmt->AsInt64(1);
      }
      shouldReparentRoots |= parentId != mRootId;
    }
  }

  rv = EnsureBookmarkRoots(maxPosition + 1, shouldReparentRoots);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::EnsureBookmarkRoots(const int32_t startPosition,
                                       bool shouldReparentRoots) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;

  if (mRootId < 1) {
    // The first root's title is an empty string.
    rv = CreateRoot(mMainConn, "places"_ns, "root________"_ns, ""_ns, 0,
                    mRootId);

    if (NS_FAILED(rv)) return rv;
  }

  int32_t position = startPosition;

  // For the other roots, the UI doesn't rely on the value in the database, so
  // just set it to something simple to make it easier for humans to read.
  if (mMenuRootId < 1) {
    rv = CreateRoot(mMainConn, "menu"_ns, "menu________"_ns, "menu"_ns,
                    position, mMenuRootId);
    if (NS_FAILED(rv)) return rv;
    position++;
  }

  if (mToolbarRootId < 1) {
    rv = CreateRoot(mMainConn, "toolbar"_ns, "toolbar_____"_ns, "toolbar"_ns,
                    position, mToolbarRootId);
    if (NS_FAILED(rv)) return rv;
    position++;
  }

  if (mTagsRootId < 1) {
    rv = CreateRoot(mMainConn, "tags"_ns, "tags________"_ns, "tags"_ns,
                    position, mTagsRootId);
    if (NS_FAILED(rv)) return rv;
    position++;
  }

  if (mUnfiledRootId < 1) {
    rv = CreateRoot(mMainConn, "unfiled"_ns, "unfiled_____"_ns, "unfiled"_ns,
                    position, mUnfiledRootId);
    if (NS_FAILED(rv)) return rv;
    position++;
  }

  if (mMobileRootId < 1) {
    int64_t mobileRootId = CreateMobileRoot();
    if (mobileRootId <= 0) return NS_ERROR_FAILURE;
    {
      nsCOMPtr<mozIStorageStatement> mobileRootSyncStatusStmt;
      rv = mMainConn->CreateStatement(
          nsLiteralCString("UPDATE moz_bookmarks SET syncStatus = "
                           ":sync_status WHERE id = :id"),
          getter_AddRefs(mobileRootSyncStatusStmt));
      if (NS_FAILED(rv)) return rv;

      rv = mobileRootSyncStatusStmt->BindInt32ByName(
          "sync_status"_ns, nsINavBookmarksService::SYNC_STATUS_NEW);
      if (NS_FAILED(rv)) return rv;
      rv = mobileRootSyncStatusStmt->BindInt64ByName("id"_ns, mobileRootId);
      if (NS_FAILED(rv)) return rv;

      rv = mobileRootSyncStatusStmt->Execute();
      if (NS_FAILED(rv)) return rv;

      mMobileRootId = mobileRootId;
    }
  }

  if (!shouldReparentRoots) {
    return NS_OK;
  }

  // At least one root had the wrong parent, so we need to ensure that
  // all roots are parented correctly, fix their positions, and bump the
  // Sync change counter.
  rv = mMainConn->ExecuteSimpleSQL(nsLiteralCString(
      "CREATE TEMP TRIGGER moz_ensure_bookmark_roots_trigger "
      "AFTER UPDATE OF parent ON moz_bookmarks FOR EACH ROW "
      "WHEN OLD.parent <> NEW.parent "
      "BEGIN "
      "UPDATE moz_bookmarks SET "
      "syncChangeCounter = syncChangeCounter + 1 "
      "WHERE id IN (OLD.parent, NEW.parent, NEW.id); "

      "UPDATE moz_bookmarks SET "
      "position = position - 1 "
      "WHERE parent = OLD.parent AND position >= OLD.position; "

      // Fix the positions of the root's old siblings. Since we've already
      // moved the root, we need to exclude it from the subquery.
      "UPDATE moz_bookmarks SET "
      "position = IFNULL((SELECT MAX(position) + 1 FROM moz_bookmarks "
      "WHERE parent = NEW.parent AND "
      "id <> NEW.id), 0)"
      "WHERE id = NEW.id; "
      "END"));
  if (NS_FAILED(rv)) return rv;
  auto guard = MakeScopeExit([&]() {
    (void)mMainConn->ExecuteSimpleSQL(
        "DROP TRIGGER moz_ensure_bookmark_roots_trigger"_ns);
  });

  nsCOMPtr<mozIStorageStatement> reparentStmt;
  rv = mMainConn->CreateStatement(
      nsLiteralCString(
          "UPDATE moz_bookmarks SET "
          "parent = CASE id WHEN :root_id THEN 0 ELSE :root_id END "
          "WHERE id IN (:root_id, :menu_root_id, :toolbar_root_id, "
          ":tags_root_id, "
          ":unfiled_root_id, :mobile_root_id)"),
      getter_AddRefs(reparentStmt));
  if (NS_FAILED(rv)) return rv;

  rv = reparentStmt->BindInt64ByName("root_id"_ns, mRootId);
  if (NS_FAILED(rv)) return rv;
  rv = reparentStmt->BindInt64ByName("menu_root_id"_ns, mMenuRootId);
  if (NS_FAILED(rv)) return rv;
  rv = reparentStmt->BindInt64ByName("toolbar_root_id"_ns, mToolbarRootId);
  if (NS_FAILED(rv)) return rv;
  rv = reparentStmt->BindInt64ByName("tags_root_id"_ns, mTagsRootId);
  if (NS_FAILED(rv)) return rv;
  rv = reparentStmt->BindInt64ByName("unfiled_root_id"_ns, mUnfiledRootId);
  if (NS_FAILED(rv)) return rv;
  rv = reparentStmt->BindInt64ByName("mobile_root_id"_ns, mMobileRootId);
  if (NS_FAILED(rv)) return rv;

  rv = reparentStmt->Execute();
  if (NS_FAILED(rv)) return rv;

  return NS_OK;
}

// static
nsresult Database::InitFunctions(mozIStorageConnection* aMainConn) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = GetUnreversedHostFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = MatchAutoCompleteFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = CalculateFrecencyFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = GenerateGUIDFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = IsValidGUIDFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = FixupURLFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = StoreLastInsertedIdFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = HashFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = GetQueryParamFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = GetPrefixFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = GetHostAndPortFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = StripPrefixAndUserinfoFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = NoteSyncChangeFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = InvalidateDaysOfHistoryFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = SHA256HexFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = SetShouldStartFrecencyRecalculationFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = TargetFolderGuidFunction::create(aMainConn);
  NS_ENSURE_SUCCESS(rv, rv);

  if (StaticPrefs::places_frecency_pages_alternative_featureGate_AtStartup()) {
    rv = CalculateAltFrecencyFunction::create(aMainConn);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult Database::InitTempEntities() {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv =
      mMainConn->ExecuteSimpleSQL(CREATE_HISTORYVISITS_AFTERINSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(CREATE_HISTORYVISITS_AFTERDELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(CREATE_PLACES_AFTERINSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(CREATE_UPDATEORIGINSDELETE_TEMP);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_ORPHAN_ICONS_VIEW);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_UPDATEORIGINSDELETE_AFTERDELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  if (Preferences::GetBool(PREF_PREVIEWS_ENABLED, false)) {
    rv = mMainConn->ExecuteSimpleSQL(
        CREATE_PLACES_AFTERDELETE_WPREVIEWS_TRIGGER);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    rv = mMainConn->ExecuteSimpleSQL(CREATE_PLACES_AFTERDELETE_TRIGGER);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mMainConn->ExecuteSimpleSQL(CREATE_PLACES_AFTERUPDATE_FRECENCY_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_PLACES_AFTERUPDATE_RECALC_FRECENCY_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_ORIGINS_AFTERUPDATE_RECALC_FRECENCY_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(CREATE_ORIGINS_AFTERUPDATE_FRECENCY_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_BOOKMARKS_FOREIGNCOUNT_AFTERDELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_BOOKMARKS_FOREIGNCOUNT_AFTERINSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_BOOKMARKS_FOREIGNCOUNT_AFTERUPDATE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_KEYWORDS_FOREIGNCOUNT_AFTERDELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_KEYWORDS_FOREIGNCOUNT_AFTERINSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      CREATE_KEYWORDS_FOREIGNCOUNT_AFTERUPDATE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv =
      mMainConn->ExecuteSimpleSQL(CREATE_BOOKMARKS_DELETED_AFTERINSERT_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv =
      mMainConn->ExecuteSimpleSQL(CREATE_BOOKMARKS_DELETED_AFTERDELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(CREATE_PLACES_METADATA_AFTERDELETE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  // Thresholds chosen for elevating a visit to a higher bucket.
  bool useAlternative =
      StaticPrefs::places_frecency_pages_alternative_featureGate_AtStartup();
  int32_t viewTimeMs =
      (useAlternative
           ? StaticPrefs::
                     places_frecency_pages_alternative_interactions_viewTimeSeconds_AtStartup() *
                 1000
           : StaticPrefs::
                     places_frecency_pages_interactions_viewTimeSeconds_AtStartup() *
                 1000);
  int32_t viewTimeIfManyKeypressesMs =
      (useAlternative
           ? StaticPrefs::
                     places_frecency_pages_alternative_interactions_viewTimeIfManyKeypressesSeconds_AtStartup() *
                 1000
           : StaticPrefs::
                     places_frecency_pages_interactions_viewTimeIfManyKeypressesSeconds_AtStartup() *
                 1000);
  int32_t manyKeypresses =
      (useAlternative
           ? StaticPrefs::
                 places_frecency_pages_alternative_interactions_manyKeypresses_AtStartup()
           : StaticPrefs::
                 places_frecency_pages_interactions_manyKeypresses_AtStartup());
  rv = mMainConn->ExecuteSimpleSQL(CREATE_PLACES_METADATA_AFTERINSERT_TRIGGER(
      viewTimeMs, viewTimeIfManyKeypressesMs, manyKeypresses));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(CREATE_PLACES_METADATA_AFTERUPDATE_TRIGGER(
      viewTimeMs, viewTimeIfManyKeypressesMs, manyKeypresses));
  NS_ENSURE_SUCCESS(rv, rv);

  // Create triggers to remove rows with empty json
  rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_EXTRA_AFTERUPDATE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);
  rv =
      mMainConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS_AFTERUPDATE_TRIGGER);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::MigrateV53Up() {
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement("SELECT 1 FROM moz_items_annos"_ns,
                                           getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    // Likely we removed the table.
    return NS_OK;
  }

  // Remove all item annotations but SYNC_PARENT_ANNO.
  rv = mMainConn->CreateStatement(
      nsLiteralCString(
          "DELETE FROM moz_items_annos "
          "WHERE anno_attribute_id NOT IN ( "
          "  SELECT id FROM moz_anno_attributes WHERE name = :anno_name "
          ") "),
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindUTF8StringByName("anno_name"_ns,
                                  nsLiteralCString(SYNC_PARENT_ANNO));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(nsLiteralCString(
      "DELETE FROM moz_anno_attributes WHERE id IN ( "
      "  SELECT id FROM moz_anno_attributes "
      "  EXCEPT "
      "  SELECT DISTINCT anno_attribute_id FROM moz_annos "
      "  EXCEPT "
      "  SELECT DISTINCT anno_attribute_id FROM moz_items_annos "
      ")"));
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::MigrateV54Up() {
  // Add an expiration column to moz_icons_to_pages.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT expire_ms FROM moz_icons_to_pages"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_icons_to_pages "
        "ADD COLUMN expire_ms INTEGER NOT NULL DEFAULT 0 "_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set all the zero-ed entries as expired today, they won't be removed until
  // the next related page load.
  rv = mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_icons_to_pages "
      "SET expire_ms = strftime('%s','now','localtime','start "
      "of day','utc') * 1000 "
      "WHERE expire_ms = 0 "_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::MigrateV55Up() {
  // Add places metadata tables.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT id FROM moz_places_metadata"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    // Create the tables.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_METADATA);
    NS_ENSURE_SUCCESS(rv, rv);
    // moz_places_metadata_search_queries.
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_METADATA_SEARCH_QUERIES);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult Database::MigrateV56Up() {
  // Add places metadata (place_id, created_at) index.
  return mMainConn->ExecuteSimpleSQL(
      CREATE_IDX_MOZ_PLACES_METADATA_PLACECREATED);
}

nsresult Database::MigrateV57Up() {
  // Add the scrolling columns to the metadata.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT scrolling_time FROM moz_places_metadata"_ns,
      getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_places_metadata "
        "ADD COLUMN scrolling_time INTEGER NOT NULL DEFAULT 0 "_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = mMainConn->CreateStatement(
      "SELECT scrolling_distance FROM moz_places_metadata"_ns,
      getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_places_metadata "
        "ADD COLUMN scrolling_distance INTEGER NOT NULL DEFAULT 0 "_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV60Up() {
  // Add the site_name column to moz_places.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT site_name FROM moz_places"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_places ADD COLUMN site_name TEXT"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV61Up() {
  // Add previews tombstones table if necessary.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT hash FROM moz_previews_tombstones"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PREVIEWS_TOMBSTONES);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV67Up() {
  // Align all input field in moz_inputhistory to lowercase. If there are
  // multiple records that expresses the same input, use maximum use_count from
  // them to carry on the experience of the past.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->ExecuteSimpleSQL(
      "INSERT INTO moz_inputhistory "
      "SELECT place_id, LOWER(input), use_count FROM moz_inputhistory "
      "  WHERE LOWER(input) <> input "
      "ON CONFLICT DO "
      "  UPDATE SET use_count = MAX(use_count, EXCLUDED.use_count)"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DELETE FROM moz_inputhistory WHERE LOWER(input) <> input"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::MigrateV69Up() {
  // Add source and annotation column to places table.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT source FROM moz_historyvisits"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_historyvisits "
        "ADD COLUMN source INTEGER DEFAULT 0 NOT NULL"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_historyvisits "
        "ADD COLUMN triggeringPlaceId INTEGER"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult Database::MigrateV70Up() {
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT recalc_frecency FROM moz_places LIMIT 1 "_ns,
      getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    // Add recalc_frecency column, indicating frecency has to be recalculated.
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_places "
        "ADD COLUMN recalc_frecency INTEGER NOT NULL DEFAULT 0 "_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // We must do the following updates regardless, for downgrade/upgrade cases.

  // moz_origins frecency is, at the time of this migration, the sum of all the
  // positive frecencies of pages linked to that origin. Frecencies that were
  // set to negative to request recalculation are thus not accounted for, and
  // since we're about to flip them to positive we should add them to their
  // origin. Then we must also update origins stats.
  // We ignore frecency = -1 because it's just an indication to recalculate
  // frecency and not an actual frecency value that was flipped, thus it would
  // not make sense to count it for the origin.
  rv = mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_origins "
      "SET frecency = frecency + abs_frecency "
      "FROM (SELECT origin_id, ABS(frecency) AS abs_frecency FROM moz_places "
      "WHERE frecency < -1) AS places "
      "WHERE moz_origins.id = places.origin_id"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "INSERT OR REPLACE INTO moz_meta(key, value) VALUES "
      "('origin_frecency_count', "
      "(SELECT COUNT(*) FROM moz_origins WHERE frecency > 0) "
      "), "
      "('origin_frecency_sum', "
      "(SELECT TOTAL(frecency) FROM moz_origins WHERE frecency > 0) "
      "), "
      "('origin_frecency_sum_of_squares', "
      "(SELECT TOTAL(frecency * frecency) FROM moz_origins WHERE frecency > 0) "
      ") "_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  // Now set recalc_frecency = 1 and positive frecency to any page having a
  // negative frecency.
  // Note we don't flip frecency = -1, since we skipped it above when updating
  // origins, and it remains an acceptable value yet, until the recalculation.
  rv = mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_places "
      "SET recalc_frecency = 1, "
      "    frecency = CASE WHEN frecency = -1 THEN -1 ELSE ABS(frecency) END "
      "WHERE frecency < 0 "_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::MigrateV71Up() {
  // Fix the foreign counts. We ignore failures as the tables may not exist.
  mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_places "
      "SET foreign_count = foreign_count - 1 "
      "WHERE id in (SELECT place_id FROM moz_places_metadata_snapshots)"_ns);
  mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_places "
      "SET foreign_count = foreign_count - 1 "
      "WHERE id in (SELECT place_id FROM moz_session_to_places)"_ns);

  // Remove unused snapshots and session tables and indexes.
  nsresult rv = mMainConn->ExecuteSimpleSQL(
      "DROP INDEX IF EXISTS moz_places_metadata_snapshots_pinnedindex"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP INDEX IF EXISTS moz_places_metadata_snapshots_extra_typeindex"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP TABLE IF EXISTS moz_places_metadata_groups_to_snapshots"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP TABLE IF EXISTS moz_places_metadata_snapshots_groups"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP TABLE IF EXISTS moz_places_metadata_snapshots_extra"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP TABLE IF EXISTS moz_places_metadata_snapshots"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP TABLE IF EXISTS moz_session_to_places"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP TABLE IF EXISTS moz_session_metadata"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::MigrateV72Up() {
  // Recalculate frecency of unvisited bookmarks.
  nsresult rv = mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_places "
      "SET recalc_frecency = 1 "
      "WHERE foreign_count > 0 AND visit_count = 0"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult Database::MigrateV73Up() {
  // Add recalc_frecency, alt_frecency and recalc_alt_frecency to moz_origins.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT recalc_frecency FROM moz_origins"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_origins "
        "ADD COLUMN recalc_frecency INTEGER NOT NULL DEFAULT 0"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_origins "
        "ADD COLUMN alt_frecency INTEGER"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_origins "
        "ADD COLUMN recalc_alt_frecency INTEGER NOT NULL DEFAULT 0"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV74Up() {
  // Add alt_frecency and recalc_alt_frecency to moz_places.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT alt_frecency FROM moz_places"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_places "
        "ADD COLUMN alt_frecency INTEGER"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_places "
        "ADD COLUMN recalc_alt_frecency INTEGER NOT NULL DEFAULT 0"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_PLACES_ALT_FRECENCY);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV75Up() {
  // Add *_extra tables for moz_places and moz_historyvisits
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT sync_json FROM moz_places_extra"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    nsresult rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_PLACES_EXTRA);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_HISTORYVISITS_EXTRA);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV77Up() {
  // Recalculate origins frecency.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_origins SET recalc_frecency = 1"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult Database::MigrateV78Up() {
  // Add flags to moz_icons.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement("SELECT flags FROM moz_icons"_ns,
                                           getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_icons "
        "ADD COLUMN flags INTEGER NOT NULL DEFAULT 0"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV79Up() {
  // Add newtab_story tables for moz_newtab_story_click and
  // moz_newtab_story_impression
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT feature FROM moz_newtab_story_click"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    nsresult rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_NEWTAB_STORY_CLICK);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_NEWTAB_STORY_IMPRESSION);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV80Up() {
  // v79 indices had a typo so we're recreating them here.
  nsresult rv =
      mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_NEWTAB_STORY_CLICK_TIMESTAMP);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_NEWTAB_IMPRESSION_TIMESTAMP);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult Database::MigrateV81Up() {
  // v79 indices had a typo, v80 tried to remove them but it got the names
  // wrong, so we're effectively removing them here.
  nsresult rv = mMainConn->ExecuteSimpleSQL(
      "DROP INDEX IF EXISTS moz_newtab_story_click_idx_newtab_click_timestamp"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mMainConn->ExecuteSimpleSQL(
      "DROP INDEX IF EXISTS moz_newtab_story_click_idx_newtab_impression_timestamp"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult Database::MigrateV82Up() {
  // Create moz_newtab_shortcuts_interaction table and associated indexes.
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT id FROM moz_newtab_shortcuts_interaction"_ns,
      getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(CREATE_MOZ_NEWTAB_SHORTCUTS_INTERACTION);
    NS_ENSURE_SUCCESS(rv, rv);

    // Add moz_newtab_shortcuts_interaction timestamp index
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_NEWTAB_SHORTCUTS_TIMESTAMP);
    NS_ENSURE_SUCCESS(rv, rv);

    // Add moz_newtab_shortcuts_interaction place_id index
    rv = mMainConn->ExecuteSimpleSQL(CREATE_IDX_MOZ_NEWTAB_SHORTCUTS_PLACEID);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV83Up() {
  // Recalculate frecency due to changing calculate_frecency.
  nsresult rv = mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_places SET recalc_frecency = 1 WHERE frecency > 0"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult Database::MigrateV85Up() {
  // Recalculate frecency due to changing frecency and giving too high a bonus
  // for non-typed URLs.
  nsresult rv = mMainConn->ExecuteSimpleSQL(
      "UPDATE moz_origins "
      "SET recalc_frecency = 1 "
      "WHERE frecency > 1"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

nsresult Database::MigrateV86Up() {
  nsCOMPtr<mozIStorageStatement> stmt;

  // Check and add block_until_ms if missing.
  nsresult rv = mMainConn->CreateStatement(
      "SELECT block_until_ms FROM moz_origins"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_origins "
        "ADD COLUMN block_until_ms INTEGER"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Check and add block_pages_until_ms if missing.
  rv = mMainConn->CreateStatement(
      "SELECT block_pages_until_ms FROM moz_origins"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_origins "
        "ADD COLUMN block_pages_until_ms INTEGER"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV87Up() {
  // Invert the moz_origins UNIQUE constraint to (host, prefix), so the higher
  // cardinality column comes first and queries only filtering on host, like the
  // address bar ones, can use the index.

  {
    // The statement must be finalized before the table it reads is dropped.
    nsCOMPtr<mozIStorageStatement> stmt;
    nsresult rv = mMainConn->CreateStatement(
        nsLiteralCString("SELECT 1 FROM sqlite_master "
                         "WHERE type = 'table' AND name = 'moz_origins' "
                         "AND sql LIKE '%UNIQUE (host, prefix)%'"),
        getter_AddRefs(stmt));
    NS_ENSURE_SUCCESS(rv, rv);
    bool hasResult = false;
    if (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
      // The constraint has already been inverted.
      return NS_OK;
    }
  }

  // The table must be rebuilt, but it can't be dropped while moz_places
  // references it. Deferring the foreign keys allows the drop: it leaves every
  // referencing page orphaned until the table is refilled, which resolves them
  // again before the transaction commits.
  nsresult rv =
      mMainConn->ExecuteSimpleSQL("PRAGMA defer_foreign_keys = ON"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(
      "CREATE TEMP TABLE moz_origins_stash_temp AS "
      "SELECT * FROM moz_origins"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL("DROP TABLE moz_origins"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL(
      "CREATE TABLE moz_origins ( "
      "id INTEGER PRIMARY KEY, "
      "prefix TEXT NOT NULL, "
      "host TEXT NOT NULL, "
      "frecency INTEGER NOT NULL, "
      "recalc_frecency INTEGER NOT NULL DEFAULT 0, "
      "alt_frecency INTEGER, "
      "recalc_alt_frecency INTEGER NOT NULL DEFAULT 0, "
      "block_until_ms INTEGER, "
      "block_pages_until_ms INTEGER, "
      "UNIQUE (host, prefix) "
      ")"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  // Inserting the rows into a table with the same name as the original resolves
  // the deferred constraint violations accumulated during the DROP.
  rv = mMainConn->ExecuteSimpleSQL(
      "INSERT INTO moz_origins "
      "(id, prefix, host, frecency, recalc_frecency, alt_frecency, "
      "recalc_alt_frecency, block_until_ms, block_pages_until_ms) "
      "SELECT id, prefix, host, frecency, recalc_frecency, alt_frecency, "
      "recalc_alt_frecency, block_until_ms, block_pages_until_ms "
      "FROM moz_origins_stash_temp"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mMainConn->ExecuteSimpleSQL("DROP TABLE moz_origins_stash_temp"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Database::MigrateV88Up() {
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = mMainConn->CreateStatement(
      "SELECT userContextId FROM moz_historyvisits"_ns, getter_AddRefs(stmt));
  if (NS_FAILED(rv)) {
    rv = mMainConn->ExecuteSimpleSQL(
        "ALTER TABLE moz_historyvisits "
        "ADD COLUMN userContextId INTEGER NOT NULL DEFAULT 0"_ns);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult Database::MigrateV89Up() {
  return mMainConn->ExecuteSimpleSQL(
      CREATE_IDX_MOZ_ICONS_TO_PAGES_ICON("favicons."));
}

int64_t Database::CreateMobileRoot() {
  MOZ_ASSERT(NS_IsMainThread());

  // Create the mobile root, ignoring conflicts if one already exists (for
  // example, if the user downgraded to an earlier release channel).
  nsCOMPtr<mozIStorageStatement> createStmt;
  nsresult rv = mMainConn->CreateStatement(
      nsLiteralCString(
          "INSERT OR IGNORE INTO moz_bookmarks "
          "(type, title, dateAdded, lastModified, guid, position, parent) "
          "SELECT :item_type, :item_title, :timestamp, :timestamp, :guid, "
          "IFNULL((SELECT MAX(position) + 1 FROM moz_bookmarks p WHERE "
          "p.parent = b.id), 0), b.id "
          "FROM moz_bookmarks b WHERE b.parent = 0"),
      getter_AddRefs(createStmt));
  if (NS_FAILED(rv)) return -1;

  rv = createStmt->BindInt32ByName("item_type"_ns,
                                   nsINavBookmarksService::TYPE_FOLDER);
  if (NS_FAILED(rv)) return -1;
  rv = createStmt->BindUTF8StringByName("item_title"_ns,
                                        nsLiteralCString(MOBILE_ROOT_TITLE));
  if (NS_FAILED(rv)) return -1;
  rv = createStmt->BindInt64ByName("timestamp"_ns, RoundedPRNow());
  if (NS_FAILED(rv)) return -1;
  rv = createStmt->BindUTF8StringByName("guid"_ns,
                                        nsLiteralCString(MOBILE_ROOT_GUID));
  if (NS_FAILED(rv)) return -1;

  rv = createStmt->Execute();
  if (NS_FAILED(rv)) return -1;

  // Find the mobile root ID. We can't use the last inserted ID because the
  // root might already exist, and we ignore on conflict.
  nsCOMPtr<mozIStorageStatement> findIdStmt;
  rv = mMainConn->CreateStatement(
      "SELECT id FROM moz_bookmarks WHERE guid = :guid"_ns,
      getter_AddRefs(findIdStmt));
  if (NS_FAILED(rv)) return -1;

  rv = findIdStmt->BindUTF8StringByName("guid"_ns,
                                        nsLiteralCString(MOBILE_ROOT_GUID));
  if (NS_FAILED(rv)) return -1;

  bool hasResult = false;
  rv = findIdStmt->ExecuteStep(&hasResult);
  if (NS_FAILED(rv) || !hasResult) return -1;

  int64_t rootId;
  rv = findIdStmt->GetInt64(0, &rootId);
  if (NS_FAILED(rv)) return -1;

  return rootId;
}

void Database::Shutdown() {
  // As the last step in the shutdown path, finalize the database handle.
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mClosed);

  // Break cycles with the shutdown blockers.
  mClientsShutdown = nullptr;
  nsCOMPtr<mozIStorageCompletionCallback> connectionShutdown =
      std::move(mConnectionShutdown);

  if (!mMainConn) {
    // The connection has never been initialized. Just mark it as closed.
    mClosed = true;
    (void)connectionShutdown->Complete(NS_OK, nullptr);
    return;
  }

#ifdef DEBUG
  {
    bool hasResult;
    nsCOMPtr<mozIStorageStatement> stmt;

    // Sanity check for missing guids.
    nsresult rv =
        mMainConn->CreateStatement(nsLiteralCString("SELECT 1 "
                                                    "FROM moz_places "
                                                    "WHERE guid IS NULL "),
                                   getter_AddRefs(stmt));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = stmt->ExecuteStep(&hasResult);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    MOZ_ASSERT(!hasResult, "Found a page without a GUID!");
    rv = mMainConn->CreateStatement(nsLiteralCString("SELECT 1 "
                                                     "FROM moz_bookmarks "
                                                     "WHERE guid IS NULL "),
                                    getter_AddRefs(stmt));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = stmt->ExecuteStep(&hasResult);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    MOZ_ASSERT(!hasResult, "Found a bookmark without a GUID!");

    // Sanity check for unrounded dateAdded and lastModified values (bug
    // 1107308).
    rv = mMainConn->CreateStatement(
        nsLiteralCString(
            "SELECT 1 "
            "FROM moz_bookmarks "
            "WHERE dateAdded % 1000 > 0 OR lastModified % 1000 > 0 LIMIT 1"),
        getter_AddRefs(stmt));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = stmt->ExecuteStep(&hasResult);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    MOZ_ASSERT(!hasResult, "Found unrounded dates!");

    // Sanity check url_hash
    rv = mMainConn->CreateStatement(
        "SELECT 1 FROM moz_places WHERE url_hash = 0"_ns, getter_AddRefs(stmt));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = stmt->ExecuteStep(&hasResult);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    MOZ_ASSERT(!hasResult, "Found a place without a hash!");

    // Sanity check unique urls
    rv = mMainConn->CreateStatement(
        nsLiteralCString(
            "SELECT 1 FROM moz_places GROUP BY url HAVING count(*) > 1 "),
        getter_AddRefs(stmt));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = stmt->ExecuteStep(&hasResult);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    MOZ_ASSERT(!hasResult, "Found a duplicate url!");

    // Sanity check NULL urls
    rv = mMainConn->CreateStatement(
        "SELECT 1 FROM moz_places WHERE url ISNULL "_ns, getter_AddRefs(stmt));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = stmt->ExecuteStep(&hasResult);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    MOZ_ASSERT(!hasResult, "Found a NULL url!");
  }
#endif

  mMainThreadStatements.FinalizeStatements();
  mMainThreadAsyncStatements.FinalizeStatements();

  RefPtr<FinalizeStatementCacheProxy<mozIStorageStatement>> event =
      new FinalizeStatementCacheProxy<mozIStorageStatement>(
          mAsyncThreadStatements, NS_ISUPPORTS_CAST(nsIObserver*, this));
  DispatchToAsyncThread(event);

  mClosed = true;

  // Execute PRAGMA optimize as last step, this will ensure proper database
  // performance across restarts. The 0x12 flags mean: run ANALYZE on tables
  // that might benefit (0x02), with a row limit to keep runtime bounded (0x10).
  nsCOMPtr<mozIStoragePendingStatement> ps;
  MOZ_ALWAYS_SUCCEEDS(mMainConn->ExecuteSimpleSQLAsync(
      "PRAGMA optimize(0x12)"_ns, nullptr, getter_AddRefs(ps)));

  if (NS_FAILED(mMainConn->AsyncClose(connectionShutdown))) {
    (void)connectionShutdown->Complete(NS_ERROR_UNEXPECTED, nullptr);
  }
  mMainConn = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
//// nsIObserver

NS_IMETHODIMP
Database::Observe(nsISupports* aSubject, const char* aTopic,
                  const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());
  if (strcmp(aTopic, TOPIC_PROFILE_CHANGE_TEARDOWN) == 0) {
    // Tests simulating shutdown may cause multiple notifications.
    if (PlacesShutdownBlocker::sIsStarted) {
      return NS_OK;
    }

    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    NS_ENSURE_STATE(os);

    // If shutdown happens in the same mainthread loop as init, observers could
    // handle the places-init-complete notification after xpcom-shutdown, when
    // the connection does not exist anymore.  Removing those observers would
    // be less expensive but may cause their RemoveObserver calls to throw.
    // Thus notify the topic now, so they stop listening for it.
    nsCOMPtr<nsISimpleEnumerator> e;
    if (NS_SUCCEEDED(os->EnumerateObservers(TOPIC_PLACES_INIT_COMPLETE,
                                            getter_AddRefs(e))) &&
        e) {
      bool hasMore = false;
      while (NS_SUCCEEDED(e->HasMoreElements(&hasMore)) && hasMore) {
        nsCOMPtr<nsISupports> supports;
        if (NS_SUCCEEDED(e->GetNext(getter_AddRefs(supports)))) {
          nsCOMPtr<nsIObserver> observer = do_QueryInterface(supports);
          (void)observer->Observe(observer, TOPIC_PLACES_INIT_COMPLETE,
                                  nullptr);
        }
      }
    }

    // Notify all Places users that we are about to shutdown.
    (void)os->NotifyObservers(nullptr, TOPIC_PLACES_SHUTDOWN, nullptr);
  } else if (strcmp(aTopic, TOPIC_SIMULATE_PLACES_SHUTDOWN) == 0) {
    // This notification is (and must be) only used by tests that are trying
    // to simulate Places shutdown out of the normal shutdown path.

    // Tests simulating shutdown may cause re-entrance.
    if (PlacesShutdownBlocker::sIsStarted) {
      return NS_OK;
    }

    // We are simulating a shutdown, so invoke the shutdown blockers,
    // wait for them, then proceed with connection shutdown.
    // Since we are already going through shutdown, but it's not the real one,
    // we won't need to block the real one anymore, so we can unblock it.
    {
      nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase =
          GetProfileChangeTeardownPhase();
      if (shutdownPhase) {
        shutdownPhase->RemoveBlocker(mClientsShutdown.get());
      }
      (void)mClientsShutdown->BlockShutdown(nullptr);
    }

    // Spin the events loop until the clients are done.
    // Note, this is just for tests, specifically test_clearHistory_shutdown.js
    SpinEventLoopUntil("places:Database::Observe(SIMULATE_PLACES_SHUTDOWN)"_ns,
                       [&]() {
                         return mClientsShutdown->State() ==
                                PlacesShutdownBlocker::States::RECEIVED_DONE;
                       });

    {
      nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase =
          GetProfileBeforeChangePhase();
      if (shutdownPhase) {
        shutdownPhase->RemoveBlocker(mConnectionShutdown.get());
      }
      (void)mConnectionShutdown->BlockShutdown(nullptr);
    }
  }
  return NS_OK;
}

}  // namespace mozilla::places
