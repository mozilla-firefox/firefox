/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef mozilla_BounceTrackingProtectionStorage_h_
#define mozilla_BounceTrackingProtectionStorage_h_

#include <functional>

#include "mozIStorageFunction.h"
#include "mozilla/Logging.h"
#include "mozilla/MozPromise.h"
#include "mozilla/Mutex.h"
#include "mozilla/ThreadSafety.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/FlippedOnce.h"
#include "nsIAsyncShutdown.h"
#include "nsIFile.h"
#include "nsIObserver.h"
#include "nsISupports.h"
#include "nsTHashMap.h"
#include "nsTHashSet.h"
#include "mozIStorageConnection.h"
#include "mozilla/OriginAttributesHashKey.h"
#include "BounceTrackingSite.h"

class nsIPrincipal;
class mozIStorageConnection;
namespace mozilla {

class BounceTrackingStateGlobal;
class OriginAttributes;

extern LazyLogModule gBounceTrackingProtectionLog;

class BounceTrackingProtectionStorage final : public nsIObserver,
                                              public nsIAsyncShutdownBlocker,
                                              public SupportsWeakPtr {
  friend class BounceTrackingStateGlobal;

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIASYNCSHUTDOWNBLOCKER

 public:
  BounceTrackingProtectionStorage();

  // Initialises the storage including the on-disk database.
  [[nodiscard]] nsresult Init();

  // BTP state is partitioned by neither first party domain nor partition key.
  // Returns the OriginAttributes BTP keys its state on.
  static OriginAttributes NormalizeOriginAttributes(
      const OriginAttributes& aOriginAttributes);

  // Getters for mStateGlobal.
  RefPtr<BounceTrackingStateGlobal> GetStateGlobal(
      const OriginAttributes& aOriginAttributes);

  RefPtr<BounceTrackingStateGlobal> GetStateGlobal(nsIPrincipal* aPrincipal);

  RefPtr<BounceTrackingStateGlobal> GetOrCreateStateGlobal(
      const OriginAttributes& aOriginAttributes);

  RefPtr<BounceTrackingStateGlobal> GetOrCreateStateGlobal(
      nsIPrincipal* aPrincipal);

  using StateGlobalMap =
      nsTHashMap<OriginAttributesHashKey, RefPtr<BounceTrackingStateGlobal>>;
  // Provides a read-only reference to the state global map.
  const StateGlobalMap& StateGlobalMapRef() { return mStateGlobal; }

  // The enum values match the database type field. Updating them requires a DB
  // migration.
  enum class EntryType : uint8_t { BounceTracker = 0, UserActivation = 1 };

  static const char* EntryTypeToString(EntryType aType) {
    switch (aType) {
      case EntryType::BounceTracker:
        return "BounceTracker";
      case EntryType::UserActivation:
        return "UserActivation";
    }
    return "Unknown";
  }

  // Clear all user activation or bounce tracker entries.
  [[nodiscard]] nsresult ClearByType(
      BounceTrackingProtectionStorage::EntryType aType);

  // Clear all state for a given site host. If aOriginAttributes is passed, only
  // entries for that OA will be deleted.
  [[nodiscard]] nsresult ClearBySiteHost(const nsACString& aSiteHost,
                                         OriginAttributes* aOriginAttributes);

  // Clear all state within a given time range.
  [[nodiscard]] nsresult ClearByTimeRange(PRTime aFrom, PRTime aTo);

  // Clear all state for a given OriginAttributesPattern.
  // Optional filtering for site host via aSiteHost.
  [[nodiscard]] nsresult ClearByOriginAttributesPattern(
      const OriginAttributesPattern& aOriginAttributesPattern,
      const Maybe<nsCString>& aSiteHost = Nothing());

  // Clear all state.
  [[nodiscard]] nsresult Clear();

  // Flushes pending updates. Resolves once all queued database operations,
  // including initialization and loading the on-disk state into memory, have
  // completed.
  RefPtr<GenericPromise> TestFlush();

 private:
  [[nodiscard]] nsresult InitInternal();

  ~BounceTrackingProtectionStorage() = default;

  // Worker thread. This should be a valid thread after Init() returns and be
  // destroyed when we finalize
  nsCOMPtr<nsISerialEventTarget> mBackgroundThread;  // main thread only

  // Database connections. Guaranteed to be non-null and working once
  // initialized and not-yet finalized
  RefPtr<mozIStorageConnection> mDatabaseConnection;  // Worker thread only

  // Returns an error if initialization failed or shutdown has started. Does not
  // wait for initialization: database operations dispatched to
  // mBackgroundThread before it has finished run after it.
  [[nodiscard]] nsresult CheckDBAvailable();

  // Called to indicate to the async shutdown service that we are all wrapped
  // up. This also spins down the worker thread, since it is called after all
  // disk database connections are closed.
  void Finalize();

  // Utility function to grab the correct barrier this service needs to shut
  // down by
  already_AddRefed<nsIAsyncShutdownClient> GetAsyncShutdownBarrier() const;

  // Initialises the DB connection on the worker thread.
  // If aShouldRetry is true and the connection fails, the database file will be
  // reset and the connection will be retried.
  [[nodiscard]] nsresult CreateDatabaseConnection(bool aShouldRetry = true);

  // Creates amd initialises the database table if needed. Worker thread only.
  [[nodiscard]] nsresult EnsureTable();

  // Temporary data structure used to import db data into memory.
  struct ImportEntry {
    OriginAttributes mOriginAttributes;
    nsCString mSiteHost;
    EntryType mEntryType;
    PRTime mTimeStamp;
  };

  // Imports state from the database on disk into memory.
  [[nodiscard]] nsresult LoadMemoryStateFromDisk();

  // Used to (thread-safely) track how many operations have been launched to the
  // worker thread so that we can wait for it to hit zero before close the disk
  // database connection
  void IncrementPendingWrites();
  void DecrementPendingWrites();

  // Delete database entries. Worker thread only.
  [[nodiscard]] static nsresult DeleteData(
      mozIStorageConnection* aDatabaseConnection,
      Maybe<OriginAttributes> aOriginAttributes, const nsACString& aSiteHost);

  // Delete all entries before a given time. Worker thread only.
  // If aEntryType is passed only entries of that type will be deleted.
  [[nodiscard]] static nsresult DeleteDataInTimeRange(
      mozIStorageConnection* aDatabaseConnection,
      Maybe<OriginAttributes> aOriginAttributes, PRTime aFrom,
      Maybe<PRTime> aTo,
      Maybe<BounceTrackingProtectionStorage::EntryType> aEntryType = Nothing{});

  // Delete all entries of a specific type.
  // aOriginAttributes can be passed
  [[nodiscard]] nsresult DeleteDataByType(
      mozIStorageConnection* aDatabaseConnection,
      const Maybe<OriginAttributes>& aOriginAttributes,
      BounceTrackingProtectionStorage::EntryType aEntryType);

  // Delete all entries matching the given OriginAttributesPattern. Worker
  // thread only. May pass aSiteHost for additional filtering.
  [[nodiscard]] static nsresult DeleteDataByOriginAttributesPattern(
      mozIStorageConnection* aDatabaseConnection,
      const OriginAttributesPattern& aOriginAttributesPattern,
      const Maybe<nsCString>& aSiteHost = Nothing());

  // Clear all entries from the database.
  [[nodiscard]] static nsresult ClearData(
      mozIStorageConnection* aDatabaseConnection);

  // Structure to hold pending database updates.
  // We batch writes to disk to avoid excessive disk IO.
  using PendingUpdate = ImportEntry;

  // Bulk update database entries. Worker thread only.
  [[nodiscard]] static nsresult UpsertDataBulk(
      mozIStorageConnection* aDatabaseConnection,
      const nsTArray<PendingUpdate>& aUpdates);

  // Service state management. We protect these variables with a mutex.
  Mutex mMutex;

  FlippedOnce<false> mErrored MOZ_GUARDED_BY(mMutex);
  FlippedOnce<false> mShuttingDown MOZ_GUARDED_BY(mMutex);
  uint32_t mPendingWrites MOZ_GUARDED_BY(mMutex);

  // The pending updates to be flushed to the database in bulk.
  // Only holds changes that go through UpdateDBEntry.
  // Main thread access only.
  nsTArray<PendingUpdate> mPendingUpdates;

  // The database file handle. We can only create this in the main thread and
  // need it in the worker to perform blocking disk IO. So we put it on this,
  // since we pass this to the worker anyway
  nsCOMPtr<nsIFile> mDatabaseFile;

  // Map of origin attributes to global state object. This enables us to track
  // bounce tracking state per OA, e.g. to separate private browsing from normal
  // browsing.
  StateGlobalMap mStateGlobal;

  // Whether the on-disk state has been loaded into mStateGlobal. Main thread
  // only.
  bool mMemoryStateLoaded = false;

  // Database operations issued before the on-disk state was loaded into memory.
  // Main thread only.
  // Loaded entries matching a deletion are skipped.
  nsTArray<std::function<bool(const ImportEntry&)>> mDeletionsBeforeLoad;
  // Loaded entries for these sites are merged into the in-memory state and the
  // merged result is written back, since the database row now holds the value
  // written before load.
  nsTHashMap<OriginAttributesHashKey, nsTHashSet<nsCString>>
      mSitesWrittenBeforeLoad;

  void NoteSiteWrittenBeforeLoad(const OriginAttributes& aOriginAttributes,
                                 const nsACString& aSiteHost);
  void NoteDeletionBeforeLoad(
      std::function<bool(const ImportEntry&)>&& aMatches);
  bool IsDeletedBeforeLoad(const ImportEntry& aEntry) const;
  bool IsSiteWrittenBeforeLoad(const ImportEntry& aEntry);

  // Writes the in-memory state of a site to the database.
  [[nodiscard]] nsresult PersistMemoryState(
      BounceTrackingStateGlobal* aStateGlobal, const BounceTrackingSite& aSite);

  // Helpers used to sync updates to BounceTrackingStateGlobal with the
  // database.

  // Updates or inserts a DB entry keyed by OA + site host.
  [[nodiscard]] nsresult UpdateDBEntry(const BounceTrackingSite& aSite,
                                       EntryType aEntryType, PRTime aTimeStamp);

  // Flushes pending updates to the database if the buffer is full
  [[nodiscard]] nsresult MaybeFlushPendingUpdates();

  // Flushes all pending updates to the database
  [[nodiscard]] nsresult FlushPendingUpdates();

  // Deletes a DB entry keyed by OA + site host. If only aSiteHost is passed,
  // all entries for that host will be deleted across OriginAttributes.
  [[nodiscard]] nsresult DeleteDBEntries(OriginAttributes* aOriginAttributes,
                                         const nsACString& aSiteHost);

  // Delete all DB entries before a given time.
  // If aEntryType is passed only entries of that type will be deleted.
  [[nodiscard]] nsresult DeleteDBEntriesInTimeRange(
      OriginAttributes* aOriginAttributes, PRTime aFrom,
      Maybe<PRTime> aTo = Nothing{}, Maybe<EntryType> aEntryType = Nothing{});

  // Delete all DB entries matching the given type.
  // If aOriginAttributes is passed it acts as an additional filter.
  [[nodiscard]] nsresult DeleteDBEntriesByType(
      OriginAttributes* aOriginAttributes,
      BounceTrackingProtectionStorage::EntryType aEntryType);

  // Deletes all DB entries matching the given OriginAttributesPattern.
  // Pass aSiteHost for additional filtering. By default all site hosts are
  // targeted.
  [[nodiscard]] nsresult DeleteDBEntriesByOriginAttributesPattern(
      const OriginAttributesPattern& aOriginAttributesPattern,
      const Maybe<nsCString>& aSiteHost = Nothing());
};

// A SQL function to match DB entries by OriginAttributesPattern.
class OriginAttrsPatternMatchOASuffixSQLFunction final
    : public mozIStorageFunction {
  NS_DECL_ISUPPORTS
  NS_DECL_MOZISTORAGEFUNCTION

  explicit OriginAttrsPatternMatchOASuffixSQLFunction(
      OriginAttributesPattern const& aPattern)
      : mPattern(aPattern) {}
  OriginAttrsPatternMatchOASuffixSQLFunction() = delete;

 private:
  ~OriginAttrsPatternMatchOASuffixSQLFunction() = default;

  OriginAttributesPattern mPattern;
};

inline const char* format_as(BounceTrackingProtectionStorage::EntryType aType) {
  return BounceTrackingProtectionStorage::EntryTypeToString(aType);
}

}  // namespace mozilla

#endif  // mozilla_BounceTrackingProtectionStorage_h_
