/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCOMPtr.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsArrayUtils.h"
#include "nsCRT.h"
#include "nsIObserverService.h"
#include "nsIPermissionManager.h"
#include "nsIPrefBranch.h"
#include "nsIXULRuntime.h"
#include "nsToolkitCompsCID.h"
#include "RealTimeRequestSimulator.h"
#include "nsUrlClassifierDBService.h"
#include "nsUrlClassifierUtils.h"
#include "nsUrlClassifierProxies.h"
#include "nsURILoader.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsTArray.h"
#include "nsNetCID.h"
#include "nsThreadUtils.h"
#include "nsProxyRelease.h"
#include "nsString.h"
#include "mozilla/Atomics.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/ErrorNames.h"
#include "mozilla/Mutex.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/glean/UrlClassifierMetrics.h"
#include "mozilla/Logging.h"
#include "prnetdb.h"
#include "Entries.h"
#include "Classifier.h"
#include "ProtocolParser.h"
#include "nsIHttpChannel.h"
#include "nsIPrincipal.h"
#include "nsIUrlListManager.h"
#include "ProtocolParser.h"
#include "nsContentUtils.h"
#include "mozilla/Components.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/ContentClassifierService.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/URLClassifierChild.h"
#include "mozilla/net/UrlClassifierFeatureFactory.h"
#include "mozilla/net/UrlClassifierFeatureResult.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/SyncRunnable.h"
#include "UrlClassifierTelemetryUtils.h"
#include "nsIURLFormatter.h"
#include "nsIUploadChannel.h"
#include "nsStringStream.h"
#include "nsNetUtil.h"
#include "nsThreadUtils.h"
#include "nsToolkitCompsCID.h"

namespace mozilla {
namespace safebrowsing {

nsresult TablesToResponse(const nsACString& tables) {
  if (tables.IsEmpty()) {
    return NS_OK;
  }

  // We don't check mCheckMalware and friends because disabled tables are
  // never included
  if (FindInReadable("-malware-"_ns, tables)) {
    return NS_ERROR_MALWARE_URI;
  }
  if (FindInReadable("-harmful-"_ns, tables)) {
    return NS_ERROR_HARMFUL_URI;
  }
  if (FindInReadable("-phish-"_ns, tables)) {
    return NS_ERROR_PHISHING_URI;
  }
  if (FindInReadable("-unwanted-"_ns, tables)) {
    return NS_ERROR_UNWANTED_URI;
  }
  if (FindInReadable("-track-"_ns, tables)) {
    return NS_ERROR_TRACKING_URI;
  }
  if (FindInReadable("-block-"_ns, tables)) {
    return NS_ERROR_BLOCKED_URI;
  }
  return NS_OK;
}

}  // namespace safebrowsing
}  // namespace mozilla

// This class holds a list of features, their tables, and it stores the lookup
// results.
class nsUrlClassifierDBService::FeatureHolder final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FeatureHolder);

  // In order to avoid multiple lookup for the same table, we have a special
  // array for tables and their results. The Features are stored in a separate
  // array together with the references to their tables.

  class TableData {
   public:
    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(
        nsUrlClassifierDBService::FeatureHolder::TableData);

    explicit TableData(const nsACString& aTable) : mTable(aTable) {}

    nsCString mTable;
    LookupResultArray mResults;

   private:
    ~TableData() = default;
  };

  struct FeatureData {
    RefPtr<nsIUrlClassifierFeature> mFeature;
    nsTArray<RefPtr<TableData>> mTables;
  };

  static already_AddRefed<FeatureHolder> Create(
      nsIURI* aURI, const nsTArray<RefPtr<nsIUrlClassifierFeature>>& aFeatures,
      nsIUrlClassifierFeature::listType aListType) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aURI);

    RefPtr<FeatureHolder> holder = new FeatureHolder(aURI);

    for (nsIUrlClassifierFeature* feature : aFeatures) {
      FeatureData* featureData = holder->mFeatureData.AppendElement();
      MOZ_ASSERT(featureData);

      featureData->mFeature = feature;
      nsTArray<nsCString> tables;
      nsresult rv = feature->GetTables(aListType, tables);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return nullptr;
      }

      for (const nsCString& table : tables) {
        RefPtr<TableData> tableData = holder->GetOrCreateTableData(table);
        MOZ_ASSERT(tableData);
        featureData->mTables.AppendElement(tableData);
      }
    }

    return holder.forget();
  }

  nsresult DoLocalLookup(const nsACString& aSpec,
                         nsUrlClassifierDBServiceWorker* aWorker) {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(aWorker);

    auto timer = mozilla::glean::urlclassifier::cl_check_time.Measure();

    // Get the set of fragments based on the url. This is necessary because we
    // only look up at most 5 URLs per aSpec, even if aSpec has more than 5
    // components.
    nsTArray<nsCString> fragments;
    LookupCache::GetLookupFragments(aSpec, &fragments);

    for (TableData* tableData : mTableData) {
      nsresult rv = aWorker->DoSingleLocalLookupWithURIFragments(
          fragments, tableData->mTable, tableData->mResults);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }

    return NS_OK;
  }

  // This method is used to convert the LookupResultArray from
  // ::DoSingleLocalLookupWithURIFragments to nsIUrlClassifierFeatureResult
  void GetResults(nsTArray<RefPtr<nsIUrlClassifierFeatureResult>>& aResults) {
    MOZ_ASSERT(NS_IsMainThread());

    // For each table, we must concatenate the results of the corresponding
    // tables.

    for (FeatureData& featureData : mFeatureData) {
      nsAutoCString list;
      for (TableData* tableData : featureData.mTables) {
        for (uint32_t i = 0; i < tableData->mResults.Length(); ++i) {
          if (!list.IsEmpty()) {
            list.AppendLiteral(",");
          }
          list.Append(tableData->mResults[i]->mTableName);
        }
      }

      if (list.IsEmpty()) {
        continue;
      }

      RefPtr<mozilla::net::UrlClassifierFeatureResult> result =
          new mozilla::net::UrlClassifierFeatureResult(
              mURI, featureData.mFeature, list);
      aResults.AppendElement(result);
    }
  }

  mozilla::UniquePtr<LookupResultArray> GetTableResults() const {
    mozilla::UniquePtr<LookupResultArray> results =
        std::make_unique<LookupResultArray>();
    if (NS_WARN_IF(!results)) {
      return nullptr;
    }

    for (TableData* tableData : mTableData) {
      results->AppendElements(tableData->mResults);
    }

    return results;
  }

 private:
  explicit FeatureHolder(nsIURI* aURI) : mURI(aURI) {
    MOZ_ASSERT(NS_IsMainThread());
  }

  ~FeatureHolder() {
    class FeatureHolderRelease final : public mozilla::Runnable {
     public:
      explicit FeatureHolderRelease(nsTArray<nsCOMPtr<nsISupports>>&& aDoomed)
          : Runnable("FeatureHolderRelease"), mDoomed(std::move(aDoomed)) {}

      NS_IMETHOD Run() override {
        mDoomed.Clear();
        return NS_OK;
      }

     private:
      ~FeatureHolderRelease() {
        // If we still have some references, let's forget them to avoid crashes.
        // Probably we are shutdown.
        for (nsCOMPtr<nsISupports>& doomed : mDoomed) {
          doomed.forget().leak();
        }
      }

      nsTArray<nsCOMPtr<nsISupports>> mDoomed;
    };

    nsTArray<nsCOMPtr<nsISupports>> arrayToRelease;

    if (mURI) {
      arrayToRelease.AppendElement(mURI.forget());
    }

    for (FeatureData& featureData : mFeatureData) {
      if (featureData.mFeature) {
        arrayToRelease.AppendElement(featureData.mFeature.forget());
      }
    }

    if (arrayToRelease.IsEmpty()) {
      return;
    }

    if (NS_IsMainThread()) {
      arrayToRelease.Clear();
      return;
    }

    RefPtr<FeatureHolderRelease> runnable(
        new FeatureHolderRelease(std::move(arrayToRelease)));
    NS_DispatchToMainThread(runnable.forget());
  }

  already_AddRefed<TableData> GetOrCreateTableData(const nsACString& aTable) {
    for (TableData* tableData : mTableData) {
      if (tableData->mTable == aTable) {
        return do_AddRef(tableData);
      }
    }

    mTableData.AppendElement(new TableData(aTable));
    return do_AddRef(mTableData.LastElement());
  }

  nsCOMPtr<nsIURI> mURI;
  nsTArray<FeatureData> mFeatureData;
  nsTArray<RefPtr<TableData>> mTableData;
};

using namespace mozilla;
using namespace mozilla::safebrowsing;

// MOZ_LOG=UrlClassifierDbService:5
LazyLogModule gUrlClassifierDbServiceLog("UrlClassifierDbService");
#define LOG(args) \
  MOZ_LOG(gUrlClassifierDbServiceLog, mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() \
  MOZ_LOG_TEST(gUrlClassifierDbServiceLog, mozilla::LogLevel::Debug)

#define GETHASH_NOISE_PREF "urlclassifier.gethashnoise"
#define GETHASH_NOISE_DEFAULT 4

// 30 minutes as the maximum negative cache duration.
#define MAXIMUM_NEGATIVE_CACHE_DURATION_SEC (30 * 60 * 1000)

class nsUrlClassifierDBServiceWorker;

// Singleton instance.
static nsUrlClassifierDBService* sUrlClassifierDBService;

nsIThread* nsUrlClassifierDBService::gDbBackgroundThread = nullptr;

// Once we've committed to shutting down, don't do work in the background
// thread.
static Atomic<bool> gShuttingDownThread(false);

NS_IMPL_ISUPPORTS(nsUrlClassifierDBServiceWorker, nsIUrlClassifierDBService)

nsUrlClassifierDBServiceWorker::nsUrlClassifierDBServiceWorker()
    : mUpdateObserverLock("nsUrlClassifierDBServerWorker::mUpdateObserverLock"),
      mInStream(false),
      mGethashNoise(0),
      mPendingLookupLock("nsUrlClassifierDBServerWorker::mPendingLookupLock") {}

nsUrlClassifierDBServiceWorker::~nsUrlClassifierDBServiceWorker() {
  NS_ASSERTION(!mClassifier,
               "Db connection not closed, leaking memory!  Call CloseDb "
               "to close the connection.");
}

nsresult nsUrlClassifierDBServiceWorker::Init(
    uint32_t aGethashNoise, nsCOMPtr<nsIFile> aCacheDir,
    nsUrlClassifierDBService* aDBService) {
  mGethashNoise = aGethashNoise;
  mCacheDir = std::move(aCacheDir);
  mDBService = aDBService;

  ResetUpdate();

  return NS_OK;
}

nsresult nsUrlClassifierDBServiceWorker::QueueLookup(
    const nsACString& aKey,
    nsUrlClassifierDBService::FeatureHolder* aFeatureHolder,
    nsIUrlClassifierLookupCallback* aCallback) {
  MOZ_ASSERT(aFeatureHolder);
  MOZ_ASSERT(aCallback);

  MutexAutoLock lock(mPendingLookupLock);
  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  PendingLookup* lookup = mPendingLookups.AppendElement(fallible);
  if (NS_WARN_IF(!lookup)) return NS_ERROR_OUT_OF_MEMORY;

  lookup->mStartTime = TimeStamp::Now();
  lookup->mKey = aKey;
  lookup->mCallback = aCallback;
  lookup->mFeatureHolder = aFeatureHolder;

  return NS_OK;
}

nsresult nsUrlClassifierDBServiceWorker::DoSingleLocalLookupWithURIFragments(
    const nsTArray<nsCString>& aSpecFragments, const nsACString& aTable,
    LookupResultArray& aResults) {
  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  MOZ_ASSERT(
      !NS_IsMainThread(),
      "DoSingleLocalLookupWithURIFragments must be on background thread");

  // Bail if we haven't been initialized on the background thread.
  if (!mClassifier) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv =
      mClassifier->CheckURIFragments(aSpecFragments, aTable, aResults);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  LOG(("Found %zu results.", aResults.Length()));
  return NS_OK;
}

/**
 * Lookup up a key in the database is a two step process:
 *
 * a) First we look for any Entries in the database that might apply to this
 *    url.  For each URL there are one or two possible domain names to check:
 *    the two-part domain name (example.com) and the three-part name
 *    (www.example.com).  We check the database for both of these.
 * b) If we find any entries, we check the list of fragments for that entry
 *    against the possible subfragments of the URL as described in the
 *    "Simplified Regular Expression Lookup" section of the protocol doc.
 */
nsresult nsUrlClassifierDBServiceWorker::DoLookup(
    const nsACString& spec,
    nsUrlClassifierDBService::FeatureHolder* aFeatureHolder,
    nsIUrlClassifierLookupCallback* c) {
  // Make sure the callback is invoked when a failure occurs,
  // otherwise we will not be able to load any url.
  auto scopeExit = MakeScopeExit([&c]() { c->LookupComplete(nullptr); });

  if (gShuttingDownThread) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  PRIntervalTime clockStart = 0;
  if (LOG_ENABLED()) {
    clockStart = PR_IntervalNow();
  }

  nsresult rv = aFeatureHolder->DoLocalLookup(spec, this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (LOG_ENABLED()) {
    PRIntervalTime clockEnd = PR_IntervalNow();
    LOG(("query took %dms\n",
         PR_IntervalToMilliseconds(clockEnd - clockStart)));
  }

  UniquePtr<LookupResultArray> results = aFeatureHolder->GetTableResults();
  if (NS_WARN_IF(!results)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  LOG(("Found %zu results.", results->Length()));

  for (const RefPtr<const LookupResult> lookupResult : *results) {
    if (!lookupResult->Confirmed() &&
        mDBService->CanComplete(lookupResult->mTableName)) {
      // We're going to be doing a gethash request, add some extra entries.
      // Note that we cannot pass the first two by reference, because we
      // add to completes, which can cause completes to reallocate and move.
      AddNoise(lookupResult->hash.fixedLengthPrefix, lookupResult->mTableName,
               mGethashNoise, *results);
      break;
    }
  }

  // At this point ownership of 'results' is handed to the callback.
  scopeExit.release();
  c->LookupComplete(std::move(results));

  return NS_OK;
}

nsresult nsUrlClassifierDBServiceWorker::HandlePendingLookups() {
  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  MutexAutoLock lock(mPendingLookupLock);
  while (mPendingLookups.Length() > 0) {
    PendingLookup lookup = mPendingLookups[0];
    mPendingLookups.RemoveElementAt(0);
    {
      MutexAutoUnlock unlock(mPendingLookupLock);
      DoLookup(lookup.mKey, lookup.mFeatureHolder, lookup.mCallback);
    }
    glean::urlclassifier::lookup_time_2.AccumulateRawDuration(
        TimeStamp::Now() - lookup.mStartTime);
  }

  return NS_OK;
}

nsresult nsUrlClassifierDBServiceWorker::AddNoise(const Prefix aPrefix,
                                                  const nsCString tableName,
                                                  uint32_t aCount,
                                                  LookupResultArray& results) {
  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  if (aCount < 1) {
    return NS_OK;
  }

  PrefixArray noiseEntries;
  nsresult rv =
      mClassifier->ReadNoiseEntries(aPrefix, tableName, aCount, noiseEntries);
  NS_ENSURE_SUCCESS(rv, rv);

  for (const auto noiseEntry : noiseEntries) {
    RefPtr<LookupResult> result = new LookupResult;
    results.AppendElement(result);

    result->hash.fixedLengthPrefix = noiseEntry;
    result->mNoise = true;
    result->mPartialHashLength = PREFIX_SIZE;  // Noise is always 4-byte,
    result->mTableName.Assign(tableName);
  }

  return NS_OK;
}

// Lookup a key in the db.
NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::Lookup(nsIPrincipal* aPrincipal,
                                       const nsACString& aTables,
                                       nsIUrlClassifierCallback* c) {
  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  return HandlePendingLookups();
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::GetTables(nsIUrlClassifierCallback* c) {
  if (gShuttingDownThread) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = OpenDb();
  if (NS_FAILED(rv)) {
    NS_ERROR("Unable to open SafeBrowsing database");
    return NS_ERROR_FAILURE;
  }

  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString response;
  mClassifier->TableRequest(response);
  LOG(("GetTables: %s", response.get()));
  c->HandleEvent(response);

  return rv;
}

void nsUrlClassifierDBServiceWorker::ResetStream() {
  LOG(("ResetStream"));
  mInStream = false;
  mProtocolParser = nullptr;
}

void nsUrlClassifierDBServiceWorker::ResetUpdate() {
  LOG(("ResetUpdate"));
  mUpdateWaits.Clear();
  mUpdateStatus = NS_OK;
  {
    MutexAutoLock lock(mUpdateObserverLock);
    mUpdateObserver = nullptr;
  }
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::SetHashCompleter(
    const nsACString& tableName, nsIUrlClassifierHashCompleter* completer) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::BeginUpdate(
    nsIUrlClassifierUpdateObserver* observer, const nsACString& tables,
    const nsACString& provider) {
  LOG(("nsUrlClassifierDBServiceWorker::BeginUpdate [%s]",
       PromiseFlatCString(tables).get()));

  if (gShuttingDownThread) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  {
    MutexAutoLock lock(mUpdateObserverLock);
    NS_ENSURE_STATE(!mUpdateObserver);

    nsresult rv = OpenDb();
    if (NS_FAILED(rv)) {
      NS_ERROR("Unable to open SafeBrowsing database");
      return NS_ERROR_FAILURE;
    }

    mUpdateStatus = NS_OK;
    MOZ_ASSERT(mTableUpdates.IsEmpty(),
               "mTableUpdates should have been cleared in FinishUpdate()");
    mUpdateObserver = observer;
    mProvider = provider;
  }
  Classifier::SplitTables(tables, mUpdateTables);

  return NS_OK;
}

// Called from the stream updater.
NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::BeginStream(const nsACString& table) {
  LOG(("nsUrlClassifierDBServiceWorker::BeginStream"));
  MOZ_ASSERT(!NS_IsMainThread(), "Streaming must be on the background thread");

  if (gShuttingDownThread) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  {
    MutexAutoLock lock(mUpdateObserverLock);
    NS_ENSURE_STATE(mUpdateObserver);
  }
  NS_ENSURE_STATE(!mInStream);

  mInStream = true;

  NS_ASSERTION(!mProtocolParser, "Should not have a protocol parser.");

  if (mProvider.EqualsLiteral("google5")) {
    mProtocolParser.reset(new (fallible) ProtocolParserProtobufV5());
  } else if (mProvider.EqualsLiteral("google4")) {
    mProtocolParser.reset(new (fallible) ProtocolParserProtobuf());
  } else if (mProvider.EqualsLiteral("test")) {
    // Check if we should use protobuf to parse the update for the test
    // provider.
    bool useProtobuf = StringEndsWith(mUpdateTables[0], "-proto"_ns);

    if (useProtobuf) {
      mProtocolParser.reset(new (fallible) ProtocolParserProtobuf());
    } else {
      mProtocolParser.reset(new (fallible) ProtocolParserV2());
    }
  } else {
    mProtocolParser.reset(new (fallible) ProtocolParserV2());
  }
  if (!mProtocolParser) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return mProtocolParser->Begin(table, mUpdateTables);
}

/**
 * Updating the database:
 *
 * The Update() method takes a series of chunks separated with control data,
 * as described in
 * http://code.google.com/p/google-safe-browsing/wiki/Protocolv2Spec
 *
 * It will iterate through the control data until it reaches a chunk.  By
 * the time it reaches a chunk, it should have received
 * a) the table to which this chunk applies
 * b) the type of chunk (add, delete, expire add, expire delete).
 * c) the chunk ID
 * d) the length of the chunk.
 *
 * For add and subtract chunks, it needs to read the chunk data (expires
 * don't have any data).  Chunk data is a list of URI fragments whose
 * encoding depends on the type of table (which is indicated by the end
 * of the table name):
 * a) tables ending with -exp are a zlib-compressed list of URI fragments
 *    separated by newlines.
 * b) tables ending with -sha128 have the form
 *    [domain][N][frag0]...[fragN]
 *       16    1   16        16
 *    If N is 0, the domain is reused as a fragment.
 * c) any other tables are assumed to be a plaintext list of URI fragments
 *    separated by newlines.
 *
 * Update() can be fed partial data;  It will accumulate data until there is
 * enough to act on.  Finish() should be called when there will be no more
 * data.
 */
NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::UpdateStream(const nsACString& chunk) {
  if (gShuttingDownThread) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  MOZ_ASSERT(mProtocolParser);

  NS_ENSURE_STATE(mInStream);

  HandlePendingLookups();

  // Feed the chunk to the parser.
  return mProtocolParser->AppendStream(chunk);
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::FinishStream() {
  if (gShuttingDownThread) {
    LOG(("shutting down"));
    return NS_ERROR_NOT_INITIALIZED;
  }

  MutexAutoLock lock(mUpdateObserverLock);

  MOZ_ASSERT(mProtocolParser);

  NS_ENSURE_STATE(mInStream);
  NS_ENSURE_STATE(mUpdateObserver);

  mInStream = false;

  mProtocolParser->End();

  if (NS_SUCCEEDED(mProtocolParser->Status())) {
    // An update can span several streams (V2 forwards), so merge rather than
    // replace. A zero duration is ignored to preserve the previous behaviour
    // of falling back to the last non-zero value we were given.
    nsTArray<TableWaitDuration> waits = mProtocolParser->TakeUpdateWaits();
    for (auto& wait : waits) {
      if (!wait.mWaitSec) {
        continue;
      }
      TableWaitDuration* existing = nullptr;
      for (auto& entry : mUpdateWaits) {
        if (entry.mTable == wait.mTable) {
          existing = &entry;
          break;
        }
      }
      if (existing) {
        existing->mWaitSec = wait.mWaitSec;
      } else {
        mUpdateWaits.AppendElement(std::move(wait));
      }
    }
    // XXX: Only allow forwards from the initial update?
    const nsTArray<ProtocolParser::ForwardedUpdate>& forwards =
        mProtocolParser->Forwards();
    for (uint32_t i = 0; i < forwards.Length(); i++) {
      const ProtocolParser::ForwardedUpdate& forward = forwards[i];
      mUpdateObserver->UpdateUrlRequested(forward.url, forward.table);
    }
    // Hold on to any TableUpdate objects that were created by the
    // parser.
    mTableUpdates.AppendElements(mProtocolParser->GetTableUpdates());
    mProtocolParser->ForgetTableUpdates();

  } else {
    LOG(
        ("nsUrlClassifierDBService::FinishStream Failed to parse the stream "
         "using mProtocolParser."));
    mUpdateStatus = mProtocolParser->Status();
  }
  mUpdateObserver->StreamFinished(mProtocolParser->Status(), 0);

  if (NS_SUCCEEDED(mUpdateStatus)) {
    if (mProtocolParser->ResetRequested()) {
      mClassifier->ResetTables(Classifier::Clear_All,
                               mProtocolParser->TablesToReset());
    }
  }

  mProtocolParser = nullptr;

  return mUpdateStatus;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::FinishUpdate() {
  LOG(("nsUrlClassifierDBServiceWorker::FinishUpdate"));

  MOZ_ASSERT(!NS_IsMainThread(),
             "nsUrlClassifierDBServiceWorker::FinishUpdate "
             "NUST NOT be on the main thread.");

  if (gShuttingDownThread) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  MOZ_ASSERT(!mProtocolParser,
             "Should have been nulled out in FinishStream() "
             "or never created.");

  {
    MutexAutoLock lock(mUpdateObserverLock);
    NS_ENSURE_STATE(mUpdateObserver);
  }

  if (NS_FAILED(mUpdateStatus)) {
    LOG(
        ("nsUrlClassifierDBServiceWorker::FinishUpdate() Not running "
         "ApplyUpdate() since the update has already failed."));
    mTableUpdates.Clear();
    return NotifyUpdateObserver(mUpdateStatus);
  }

  if (mTableUpdates.IsEmpty()) {
    LOG(("Nothing to update. Just notify update observer."));
    return NotifyUpdateObserver(NS_OK);
  }

  RefPtr<nsUrlClassifierDBServiceWorker> self = this;
  nsresult rv = mClassifier->AsyncApplyUpdates(
      mTableUpdates,
      [self](nsresult aRv) -> void { self->NotifyUpdateObserver(aRv); });
  mTableUpdates.Clear();  // Classifier is working on its copy.

  if (NS_FAILED(rv)) {
    LOG(("Failed to start async update. Notify immediately."));
    NotifyUpdateObserver(rv);
  }

  return rv;
}

nsresult nsUrlClassifierDBServiceWorker::NotifyUpdateObserver(
    nsresult aUpdateStatus) {
  MOZ_ASSERT(!NS_IsMainThread(),
             "nsUrlClassifierDBServiceWorker::NotifyUpdateObserver "
             "NUST NOT be on the main thread.");

  LOG(("nsUrlClassifierDBServiceWorker::NotifyUpdateObserver"));

  // We've either
  //  1) failed starting a download stream
  //  2) succeeded in starting a download stream but failed to obtain
  //     table updates
  //  3) succeeded in obtaining table updates but failed to build new
  //     tables.
  //  4) succeeded in building new tables but failed to take them.
  //  5) succeeded in taking new tables.

  mUpdateStatus = aUpdateStatus;

  nsUrlClassifierUtils* urlUtil = nsUrlClassifierUtils::GetInstance();
  if (NS_WARN_IF(!urlUtil)) {
    return NS_ERROR_FAILURE;
  }

  nsCString provider;
  // Assume that all the tables in update should have the same provider.
  urlUtil->GetTelemetryProvider(mUpdateTables.SafeElementAt(0, ""_ns),
                                provider);

  nsresult updateStatus = mUpdateStatus;
  if (NS_FAILED(mUpdateStatus)) {
    updateStatus =
        NS_ERROR_GET_MODULE(mUpdateStatus) == NS_ERROR_MODULE_URL_CLASSIFIER
            ? mUpdateStatus
            : NS_ERROR_UC_UPDATE_UNKNOWN;
  }

  // Do not record telemetry for testing tables.
  if (!provider.EqualsLiteral(TESTING_TABLE_PROVIDER_NAME)) {
    glean::urlclassifier::update_error.Get(provider).AccumulateSingleSample(
        NS_ERROR_GET_CODE(updateStatus));
  }

  MutexAutoLock lock(mUpdateObserverLock);

  if (!mUpdateObserver) {
    // In the normal shutdown process, CancelUpdate() would NOT be
    // called prior to NotifyUpdateObserver(). However, CancelUpdate()
    // is a public API which can be called in the test case at any point.
    // If the call sequence is FinishUpdate() then CancelUpdate(), the later
    // might be executed before NotifyUpdateObserver() which is triggered
    // by the update thread. In this case, we will get null mUpdateObserver.
    NS_WARNING(
        "CancelUpdate() is called before we asynchronously call "
        "NotifyUpdateObserver() in FinishUpdate().");

    // The DB cleanup will be done in CancelUpdate() so we can just return.
    return NS_OK;
  }

  // Null out mUpdateObserver before notifying so that BeginUpdate()
  // becomes available prior to callback.
  nsCOMPtr<nsIUrlClassifierUpdateObserver> updateObserver = nullptr;
  updateObserver.swap(mUpdateObserver);

  if (NS_SUCCEEDED(mUpdateStatus)) {
    nsTArray<nsCString> tables(mUpdateWaits.Length());
    nsTArray<uint32_t> waitSeconds(mUpdateWaits.Length());
    for (const auto& wait : mUpdateWaits) {
      LOG(("Notifying success: %s waits %d sec", wait.mTable.get(),
           wait.mWaitSec));
      tables.AppendElement(wait.mTable);
      waitSeconds.AppendElement(wait.mWaitSec);
    }
    updateObserver->UpdateSuccess(tables, waitSeconds);
  } else {
    if (LOG_ENABLED()) {
      nsAutoCString errorName;
      mozilla::GetErrorName(mUpdateStatus, errorName);
      LOG(("Notifying error: %s (%" PRIu32 ")", errorName.get(),
           static_cast<uint32_t>(mUpdateStatus)));
    }

    updateObserver->UpdateError(mUpdateStatus);
    /*
     * mark the tables as spoiled(clear cache in LookupCache), we don't want to
     * block hosts longer than normal because our update failed
     */
    mClassifier->ResetTables(Classifier::Clear_Cache, mUpdateTables);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::ResetDatabase() {
  nsresult rv = OpenDb();

  if (NS_SUCCEEDED(rv)) {
    mClassifier->Reset();
  }

  rv = CloseDb();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::ReloadDatabase() {
  // This will null out mClassifier
  nsresult rv = CloseDb();
  NS_ENSURE_SUCCESS(rv, rv);

  // Create new mClassifier and load prefixset and completions from disk.
  rv = OpenDb();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::ClearCache() {
  nsTArray<nsCString> tables;
  nsresult rv = mClassifier->ActiveTables(tables);
  NS_ENSURE_SUCCESS(rv, rv);

  mClassifier->ResetTables(Classifier::Clear_Cache, tables);

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::CleanRealTimeSimulatorCache() {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::ExpireRealTimeSimulatorCache() {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::CancelUpdate() {
  LOG(("nsUrlClassifierDBServiceWorker::CancelUpdate"));

  {
    MutexAutoLock lock(mUpdateObserverLock);
    if (!mUpdateObserver) {
      LOG(("No UpdateObserver, nothing to cancel"));

      return NS_OK;
    }

    LOG(("UpdateObserver exists, cancelling"));

    mUpdateStatus = NS_BINDING_ABORTED;

    mUpdateObserver->UpdateError(mUpdateStatus);
  }

  /*
   * mark the tables as spoiled(clear cache in LookupCache), we don't want to
   * block hosts longer than normal because our update failed
   */
  mClassifier->ResetTables(Classifier::Clear_Cache, mUpdateTables);

  ResetStream();
  ResetUpdate();

  return NS_OK;
}

void nsUrlClassifierDBServiceWorker::FlushAndDisableAsyncUpdate() {
  LOG(("nsUrlClassifierDBServiceWorker::FlushAndDisableAsyncUpdate()"));

  if (mClassifier) {
    mClassifier->FlushAndDisableAsyncUpdate();
  }
}

// Allows the main thread to delete the connection which may be in
// a background thread.
// XXX This could be turned into a single shutdown event so the logic
// is simpler in nsUrlClassifierDBService::Shutdown.
nsresult nsUrlClassifierDBServiceWorker::CloseDb() {
  if (mClassifier) {
    mClassifier->Close();
    mClassifier = nullptr;
  }

  // Clear last completion result when close db so we will still cache
  // completion result next time we re-open it.
  mLastResults.Clear();

  LOG(("urlclassifier db closed\n"));

  return NS_OK;
}

nsresult nsUrlClassifierDBServiceWorker::PreShutdown() {
  if (mClassifier) {
    // Classifier close will release all lookup caches which may be a
    // time-consuming job. See Bug 1408631.
    mClassifier->Close();
  }

  // WARNING: nothing we put here should affect an ongoing update thread. When
  // in doubt, put things in Shutdown() instead.
  return NS_OK;
}

nsresult nsUrlClassifierDBServiceWorker::CacheCompletions(
    const ConstCacheResultArray& aResults) {
  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  LOG(("nsUrlClassifierDBServiceWorker::CacheCompletions [%p]", this));
  if (!mClassifier) {
    return NS_OK;
  }

  if (aResults.Length() == 0) {
    return NS_OK;
  }

  if (IsSameAsLastResults(aResults)) {
    LOG(("Skipping completions that have just been cached already."));
    return NS_OK;
  }

  // Only cache results for tables that we have, don't take
  // in tables we might accidentally have hit during a completion.
  // This happens due to goog vs googpub lists existing.
  nsTArray<nsCString> tables;
  nsresult rv = mClassifier->ActiveTables(tables);
  NS_ENSURE_SUCCESS(rv, rv);
  if (LOG_ENABLED()) {
    nsCString s;
    for (size_t i = 0; i < tables.Length(); i++) {
      if (!s.IsEmpty()) {
        s += ",";
      }
      s += tables[i];
    }
    LOG(("Active tables: %s", s.get()));
  }

  ConstTableUpdateArray updates;

  for (const auto& result : aResults) {
    bool activeTable = false;

    for (uint32_t table = 0; table < tables.Length(); table++) {
      if (tables[table].Equals(result->table)) {
        activeTable = true;
        break;
      }
    }
    if (activeTable) {
      UniquePtr<ProtocolParser> pParse;
      // ~ProtocolParserProtobuf() is private and required for
      // std::make_unique<ProtocolParserProtobuf>(...)
      // see https://github.com/llvm/llvm-project/pull/229802
      // NOLINTBEGIN(modernize-make-unique)
      if (result->Ver() == CacheResult::V2) {
        pParse.reset(new ProtocolParserV2());
      } else {
        pParse.reset(new ProtocolParserProtobuf());
      }
      // NOLINTEND(modernize-make-unique)

      RefPtr<TableUpdate> tu = pParse->GetTableUpdate(result->table);

      rv = CacheResultToTableUpdate(result, tu);
      if (NS_FAILED(rv)) {
        // We can bail without leaking here because ForgetTableUpdates
        // hasn't been called yet.
        return rv;
      }
      updates.AppendElement(tu);
      pParse->ForgetTableUpdates();
    } else {
      LOG(("Completion received, but table %s is not active, so not caching.",
           result->table.get()));
    }
  }

  rv = mClassifier->ApplyFullHashes(updates);
  if (NS_SUCCEEDED(rv)) {
    mLastResults = aResults.Clone();
  }
  return rv;
}

nsresult nsUrlClassifierDBServiceWorker::CacheResultToTableUpdate(
    RefPtr<const CacheResult> aCacheResult, RefPtr<TableUpdate> aUpdate) {
  RefPtr<TableUpdateV2> tuV2 = TableUpdate::Cast<TableUpdateV2>(aUpdate);
  if (tuV2) {
    RefPtr<const CacheResultV2> result =
        CacheResult::Cast<const CacheResultV2>(aCacheResult);
    MOZ_ASSERT(result);

    if (result->miss) {
      return tuV2->NewMissPrefix(result->prefix);
    } else {
      LOG(("CacheCompletion hash %X, Addchunk %d",
           result->completion.ToUint32(), result->addChunk));

      nsresult rv = tuV2->NewAddComplete(result->addChunk, result->completion);
      if (NS_FAILED(rv)) {
        return rv;
      }
      return tuV2->NewAddChunk(result->addChunk);
    }
  }

  RefPtr<TableUpdateV4> tuV4 = TableUpdate::Cast<TableUpdateV4>(aUpdate);
  if (tuV4) {
    RefPtr<const CacheResultV4> result =
        CacheResult::Cast<const CacheResultV4>(aCacheResult);
    MOZ_ASSERT(result);

    if (LOG_ENABLED()) {
      const FullHashExpiryCache& fullHashes = result->response.fullHashes;
      for (const auto& entry : fullHashes) {
        Completion completion;
        completion.Assign(entry.GetKey());
        LOG(("CacheCompletion(v4) hash %X, CacheExpireTime %" PRId64,
             completion.ToUint32(), entry.GetData()));
      }
    }

    tuV4->NewFullHashResponse(result->prefix, result->response);
    return NS_OK;
  }

  // tableUpdate object should be either V2 or V4.
  return NS_ERROR_FAILURE;
}

nsresult nsUrlClassifierDBServiceWorker::OpenDb() {
  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  MOZ_ASSERT(!NS_IsMainThread(), "Must initialize DB on background thread");
  // Connection already open, don't do anything.
  if (mClassifier) {
    return NS_OK;
  }

  nsresult rv;
  RefPtr<Classifier> classifier = new (fallible) Classifier();
  if (!classifier) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  rv = classifier->Open(*mCacheDir);
  NS_ENSURE_SUCCESS(rv, rv);

  mClassifier = classifier;

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBServiceWorker::ClearLastResults() {
  MOZ_ASSERT(!NS_IsMainThread(), "Must be on the background thread");
  mLastResults.Clear();
  return NS_OK;
}

nsresult nsUrlClassifierDBServiceWorker::GetCacheInfo(
    const nsACString& aTable, nsIUrlClassifierCacheInfo** aCache) {
  MOZ_ASSERT(!NS_IsMainThread(), "Must be on the background thread");
  if (!mClassifier) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mClassifier->GetCacheInfo(aTable, aCache);
  return NS_OK;
}

bool nsUrlClassifierDBServiceWorker::IsSameAsLastResults(
    const ConstCacheResultArray& aResult) const {
  if (mLastResults.Length() != aResult.Length()) {
    return false;
  }

  bool equal = true;
  for (uint32_t i = 0; i < mLastResults.Length() && equal; i++) {
    RefPtr<const CacheResult> lhs = mLastResults[i];
    RefPtr<const CacheResult> rhs = aResult[i];

    if (lhs->Ver() != rhs->Ver()) {
      return false;
    }

    if (lhs->Ver() == CacheResult::V2) {
      equal = *(CacheResult::Cast<const CacheResultV2>(lhs)) ==
              *(CacheResult::Cast<const CacheResultV2>(rhs));
    } else if (lhs->Ver() == CacheResult::V4) {
      equal = *(CacheResult::Cast<const CacheResultV4>(lhs)) ==
              *(CacheResult::Cast<const CacheResultV4>(rhs));
    }
  }

  return equal;
}

// -------------------------------------------------------------------------
// nsUrlClassifierHashCompleterBase
//
// Base class for handling hash completion callbacks. This class provides
// common functionality for processing hash completion responses from the
// SafeBrowsing server.

class nsUrlClassifierHashCompleterBase
    : public nsIUrlClassifierHashCompleterCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIURLCLASSIFIERHASHCOMPLETERCALLBACK

 protected:
  nsUrlClassifierHashCompleterBase(nsUrlClassifierDBService* aDBService,
                                   nsIUrlClassifierCallback* aCallback)
      : mDBService(aDBService), mCallback(aCallback) {}

  virtual ~nsUrlClassifierHashCompleterBase();

  // Initiates hash completion requests for unconfirmed results in aResults.
  nsresult RequestHashCompletions(LookupResultArray* aResults);

  // Processes a single hash completion response by updating matching results
  // and storing the cache result.
  nsresult ProcessComplete(RefPtr<CacheResult> aCacheResult);

  // Called when all pending hash completions have finished.
  nsresult OnAllCompletionsFinished();

  // Returns the lookup results array for hash completion matching.
  virtual LookupResultArray* GetResultsForCompletion() = 0;

  // Hook called before caching completions (e.g., to cache miss entries).
  virtual void OnBeforeCacheCompletions() {}

  RefPtr<nsUrlClassifierDBService> mDBService;
  nsCOMPtr<nsIUrlClassifierCallback> mCallback;
  ConstCacheResultArray mCacheResults;
  uint32_t mPendingCompletions = 0;
};

NS_IMPL_ISUPPORTS(nsUrlClassifierHashCompleterBase,
                  nsIUrlClassifierHashCompleterCallback)

nsUrlClassifierHashCompleterBase::~nsUrlClassifierHashCompleterBase() {
  if (mCallback) {
    NS_ReleaseOnMainThread("nsUrlClassifierHashCompleterBase::mCallback",
                           mCallback.forget());
  }
}

nsresult nsUrlClassifierHashCompleterBase::RequestHashCompletions(
    LookupResultArray* aResults) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG_POINTER(aResults);

  for (const auto& result : *aResults) {
    if (!result->Confirmed()) {
      nsCOMPtr<nsIUrlClassifierHashCompleter> completer;
      nsCString gethashUrl;
      nsresult rv;
      nsCOMPtr<nsIUrlListManager> listManager =
          do_GetService("@mozilla.org/url-classifier/listmanager;1", &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = listManager->GetGethashUrl(result->mTableName, gethashUrl);
      NS_ENSURE_SUCCESS(rv, rv);
      LOG(("The match from %s needs to be completed at %s",
           result->mTableName.get(), gethashUrl.get()));
      // gethashUrls may be empty in 2 cases: test tables, and on startup where
      // we may have found a prefix in an existing table before the listmanager
      // has registered the table. In the second case we should not call
      // complete.
      if ((!gethashUrl.IsEmpty() ||
           nsUrlClassifierUtils::IsTestTable(result->mTableName)) &&
          mDBService->GetCompleter(result->mTableName,
                                   getter_AddRefs(completer))) {
        // Bug 1323953 - Send the first 4 bytes for completion no matter how
        // long we matched the prefix.
        rv = completer->Complete(result->PartialHash(), gethashUrl,
                                 result->mTableName, this);
        if (NS_SUCCEEDED(rv)) {
          mPendingCompletions++;
        }
      } else {
        // For tables with no hash completer, a complete hash match is
        // good enough, we'll consider it is valid.
        if (result->Complete()) {
          result->mConfirmed = true;
          LOG(("Skipping completion in a table without a valid completer (%s).",
               result->mTableName.get()));
        } else {
          NS_WARNING(
              "Partial match in a table without a valid completer, ignoring "
              "partial match.");
        }
      }
    }
  }

  return NS_OK;
}

nsresult nsUrlClassifierHashCompleterBase::ProcessComplete(
    RefPtr<CacheResult> aCacheResult) {
  LookupResultArray* results = GetResultsForCompletion();
  NS_ENSURE_TRUE(results, NS_ERROR_FAILURE);

  if (!mCacheResults.AppendElement(aCacheResult, fallible)) {
    // OK if this failed, we just won't cache the item.
  }

  for (const auto& result : *results) {
    if (!result->mNoise && result->mTableName.Equals(aCacheResult->table) &&
        aCacheResult->findCompletion(result->CompleteHash())) {
      result->mProtocolConfirmed = true;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierHashCompleterBase::CompletionV2(const nsACString& aCompleteHash,
                                               const nsACString& aTableName,
                                               uint32_t aChunkId) {
  MOZ_ASSERT_UNREACHABLE(
      "CompletionV2 should be overridden by subclasses that need it");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsUrlClassifierHashCompleterBase::CompletionV4(const nsACString& aPartialHash,
                                               const nsACString& aTableName,
                                               uint32_t aNegativeCacheDuration,
                                               nsIArray* aFullHashes) {
  LOG(("nsUrlClassifierHashCompleterBase::CompletionV4 [%p, %s, %d]", this,
       PromiseFlatCString(aTableName).get(), aNegativeCacheDuration));

  MOZ_ASSERT(StringEndsWith(aTableName, "-proto"_ns));

  if (!aFullHashes) {
    return NS_ERROR_INVALID_ARG;
  }

  if (aNegativeCacheDuration > MAXIMUM_NEGATIVE_CACHE_DURATION_SEC) {
    LOG(
        ("Negative cache duration too large, clamping it down to "
         "a reasonable value."));
    aNegativeCacheDuration = MAXIMUM_NEGATIVE_CACHE_DURATION_SEC;
  }

  RefPtr<CacheResultV4> result = new CacheResultV4();

  int64_t nowSec = PR_Now() / PR_USEC_PER_SEC;

  result->table = aTableName;
  result->prefix.Assign(aPartialHash);
  result->response.negativeCacheExpirySec = nowSec + aNegativeCacheDuration;

  uint32_t fullHashCount = 0;
  nsresult rv = aFullHashes->GetLength(&fullHashCount);
  if (NS_FAILED(rv)) {
    return rv;
  }

  for (uint32_t i = 0; i < fullHashCount; i++) {
    nsCOMPtr<nsIFullHashMatch> match = do_QueryElementAt(aFullHashes, i);

    nsCString fullHash;
    match->GetFullHash(fullHash);

    uint32_t duration;
    match->GetCacheDuration(&duration);

    result->response.fullHashes.InsertOrUpdate(fullHash, nowSec + duration);
  }

  return ProcessComplete(result);
}

NS_IMETHODIMP
nsUrlClassifierHashCompleterBase::CompletionFinished(nsresult aStatus) {
  if (LOG_ENABLED()) {
    nsAutoCString errorName;
    mozilla::GetErrorName(aStatus, errorName);
    LOG(("nsUrlClassifierHashCompleterBase::CompletionFinished [%p, %s]", this,
         errorName.get()));
  }

  mPendingCompletions--;
  if (mPendingCompletions == 0) {
    OnAllCompletionsFinished();
  }

  return NS_OK;
}

nsresult nsUrlClassifierHashCompleterBase::OnAllCompletionsFinished() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mPendingCompletions == 0);
  MOZ_ASSERT(mCallback);

  if (NS_WARN_IF(!mCallback)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  LookupResultArray* results = GetResultsForCompletion();
  if (!results) {
    LOG(
        ("nsUrlClassifierHashCompleterBase::OnAllCompletionsFinished "
         "[%p, no results]",
         this));
    return mCallback->HandleEvent(""_ns);
  }

  MOZ_ASSERT(mPendingCompletions == 0,
             "OnAllCompletionsFinished() should never be "
             "called while there are pending completions");

  LOG(
      ("nsUrlClassifierHashCompleterBase::OnAllCompletionsFinished "
       "[%p, %zu results]",
       this, results->Length()));

  nsCOMPtr<nsIUrlClassifierClassifyCallback> classifyCallback =
      do_QueryInterface(mCallback);

  nsTArray<nsCString> tables;
  for (const auto& result : *results) {
    if (result->mNoise) {
      LOG(("Skipping result %s from table %s (noise)",
           result->PartialHashHex().get(), result->mTableName.get()));
      continue;
    }

    if (!result->Confirmed()) {
      LOG(("Skipping result %s from table %s (not confirmed)",
           result->PartialHashHex().get(), result->mTableName.get()));
      glean::urlclassifier::CompletionExtra extra = {
          .hit = Some(false), .tableName = Some(result->mTableName)};
      glean::urlclassifier::completion.Record(Some(extra));
      continue;
    }

    LOG(("Confirmed result %s from table %s", result->PartialHashHex().get(),
         result->mTableName.get()));
    glean::urlclassifier::CompletionExtra extra = {
        .hit = Some(true), .tableName = Some(result->mTableName)};
    glean::urlclassifier::completion.Record(Some(extra));

    if (tables.IndexOf(result->mTableName) == nsTArray<nsCString>::NoIndex) {
      tables.AppendElement(result->mTableName);
    }

    if (classifyCallback) {
      nsCString fullHashString;
      result->hash.complete.ToString(fullHashString);
      classifyCallback->HandleResult(result->mTableName, fullHashString);
    }
  }

  OnBeforeCacheCompletions();

  mDBService->CacheCompletions(mCacheResults);
  mCacheResults.Clear();

  return mCallback->HandleEvent(StringJoin(","_ns, tables));
}

// -------------------------------------------------------------------------
// nsUrlClassifierLookupCallback
//
// This class takes the results of a lookup found on the worker thread
// and handles any necessary partial hash expansions before calling
// the client callback.

class nsUrlClassifierLookupCallback final
    : public nsUrlClassifierHashCompleterBase,
      public nsIUrlClassifierLookupCallback {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIURLCLASSIFIERLOOKUPCALLBACK

  NS_IMETHOD CompletionV2(const nsACString& aCompleteHash,
                          const nsACString& aTableName,
                          uint32_t aChunkId) override;

  nsUrlClassifierLookupCallback(nsUrlClassifierDBService* aDBService,
                                nsIUrlClassifierCallback* aCallback)
      : nsUrlClassifierHashCompleterBase(aDBService, aCallback) {}

 private:
  ~nsUrlClassifierLookupCallback() = default;

  LookupResultArray* GetResultsForCompletion() override {
    return mResults.get();
  }
  void OnBeforeCacheCompletions() override;
  nsresult CacheMisses();

  UniquePtr<LookupResultArray> mResults;
};

NS_IMPL_ISUPPORTS_INHERITED(nsUrlClassifierLookupCallback,
                            nsUrlClassifierHashCompleterBase,
                            nsIUrlClassifierLookupCallback)

NS_IMETHODIMP
nsUrlClassifierLookupCallback::LookupComplete(
    UniquePtr<LookupResultArray> results) {
  NS_ASSERTION(
      mResults == nullptr,
      "Should only get one set of results per nsUrlClassifierLookupCallback!");

  if (!results) {
    OnAllCompletionsFinished();
    return NS_OK;
  }

  mResults = std::move(results);

  nsresult rv = RequestHashCompletions(mResults.get());
  NS_ENSURE_SUCCESS(rv, rv);

  LOG(
      ("nsUrlClassifierLookupCallback::LookupComplete [%p] "
       "%u pending completions",
       this, mPendingCompletions));

  if (mPendingCompletions == 0) {
    OnAllCompletionsFinished();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierLookupCallback::CompletionV2(const nsACString& aCompleteHash,
                                            const nsACString& aTableName,
                                            uint32_t aChunkId) {
  LOG(("nsUrlClassifierLookupCallback::Completion [%p, %s, %d]", this,
       PromiseFlatCString(aTableName).get(), aChunkId));

  MOZ_ASSERT(!StringEndsWith(aTableName, "-proto"_ns));

  RefPtr<CacheResultV2> result = new CacheResultV2();

  result->table = aTableName;
  result->prefix.Assign(aCompleteHash);
  result->completion.Assign(aCompleteHash);
  result->addChunk = aChunkId;

  return ProcessComplete(result);
}

void nsUrlClassifierLookupCallback::OnBeforeCacheCompletions() {
  // Some parts of this gethash request generated no hits at all.
  // Save the prefixes we checked to prevent repeated requests.
  CacheMisses();
}

nsresult nsUrlClassifierLookupCallback::CacheMisses() {
  MOZ_ASSERT(mResults);

  for (const RefPtr<const LookupResult> result : *mResults) {
    // Skip V4 because cache information is already included in the
    // fullhash response so we don't need to manually add it here.
    if (!result->mProtocolV2 || result->Confirmed() || result->mNoise) {
      continue;
    }

    RefPtr<CacheResultV2> cacheResult = new CacheResultV2();

    cacheResult->table = result->mTableName;
    cacheResult->prefix = result->hash.fixedLengthPrefix;
    cacheResult->miss = true;
    if (!mCacheResults.AppendElement(cacheResult, fallible)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }
  return NS_OK;
}

// -------------------------------------------------------------------------
// nsUrlClassifierRealTimeLookupHandler
//
// This class is used to handle the real-time lookup for the SafeBrowsing
// real-time mode. It handles the lookup for the GlobalCache feature and
// determines whether to send the hash completion request or fallback to the
// local list lookup.

class nsUrlClassifierRealTimeLookupHandler final
    : public nsUrlClassifierHashCompleterBase,
      public nsIUrlClassifierLookupCallback {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIURLCLASSIFIERLOOKUPCALLBACK

  nsUrlClassifierRealTimeLookupHandler(nsUrlClassifierDBService* aDBService,
                                       nsIUrlClassifierCallback* aCallback,
                                       bool aIsPrivate)
      : nsUrlClassifierHashCompleterBase(aDBService, aCallback),
        mDebugEnabled(RealTimeRequestSimulator::RealTimeDebugEnabled()),
        mSimulator(
            StaticPrefs::browser_safebrowsing_realTime_simulation_enabled()
                ? RealTimeRequestSimulator::GetInstance()
                : nullptr),
        mIsPrivate(aIsPrivate) {}

  nsresult StartRealTimeLookup(nsIPrincipal* aPrincipal);

 private:
  ~nsUrlClassifierRealTimeLookupHandler() = default;

  nsresult CreateFeatureHolders(nsIURI* aURI);

  nsresult HandleRealTimeLookupComplete(UniquePtr<LookupResultArray>& aResults);
  nsresult HandleLocalListLookupComplete(
      UniquePtr<LookupResultArray>& aResults);

  LookupResultArray* GetResultsForCompletion() override {
    return mLocalListResults.get();
  }

  // Feature holders for the real-time and local list lookups.
  RefPtr<nsUrlClassifierDBService::FeatureHolder> mRealTimeFeatureHolder;
  RefPtr<nsUrlClassifierDBService::FeatureHolder> mLocalListFeatureHolder;

  UniquePtr<LookupResultArray> mLocalListResults;

  // Track if we have completed the real-time lookup.
  Atomic<bool> mHasCompletedRealTimeLookup{false};
  nsCString mKey;

  // Cached debug pref value for off-main-thread access.
  Atomic<bool> mDebugEnabled{false};

  // Simulator instance, obtained on main thread for use on background thread.
  RefPtr<RealTimeRequestSimulator> mSimulator;

  // Indicate if the lookup is for a private browsing session.
  bool mIsPrivate;
};

NS_IMPL_ISUPPORTS_INHERITED(nsUrlClassifierRealTimeLookupHandler,
                            nsUrlClassifierHashCompleterBase,
                            nsIUrlClassifierLookupCallback)

nsresult nsUrlClassifierRealTimeLookupHandler::StartRealTimeLookup(
    nsIPrincipal* aPrincipal) {
  LOG(("nsUrlClassifierRealTimeLookupHandler::StartRealTimeLookup [%p]", this));
  NS_ENSURE_ARG_POINTER(aPrincipal);

  nsCOMPtr<nsIURI> uri;
  // Casting to BasePrincipal, as we can't get InnerMost URI otherwise
  auto* basePrincipal = BasePrincipal::Cast(aPrincipal);
  nsresult rv = basePrincipal->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

  rv = CreateFeatureHolders(uri);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsUrlClassifierUtils* utilsService = nsUrlClassifierUtils::GetInstance();
  if (NS_WARN_IF(!utilsService)) {
    return NS_ERROR_FAILURE;
  }

  // Canonicalize the url
  rv = utilsService->GetKeyForURI(uri, mKey);
  NS_ENSURE_SUCCESS(rv, rv);

  // Start the async lookup for the real-time features. We skip the proxy here
  // because we want to control where the lookup result is handled. This will
  // allow us to handle the real-time lookup result and kick off the local list
  // lookup off the main thread.
  mDBService->LookupURIWithoutProxy(mKey, mRealTimeFeatureHolder, this);

  return NS_OK;
}

nsresult nsUrlClassifierRealTimeLookupHandler::CreateFeatureHolders(
    nsIURI* aURI) {
  LOG(("nsUrlClassifierRealTimeLookupHandler::CreateFeatureHolders [%p]",
       this));
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mRealTimeFeatureHolder && !mLocalListFeatureHolder);

  // Get the features that are used for the SafeBrowsing real-time mode.
  nsTArray<RefPtr<nsIUrlClassifierFeature>> realTimeFeatures;
  mozilla::net::UrlClassifierFeatureFactory::GetRealTimeProtectionFeatures(
      realTimeFeatures);

  // No real-time features found, bail out early.
  if (realTimeFeatures.IsEmpty()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Create the feature holder for the real-time features.
  mRealTimeFeatureHolder = nsUrlClassifierDBService::FeatureHolder::Create(
      aURI, realTimeFeatures, nsIUrlClassifierFeature::blocklist);
  if (NS_WARN_IF(!mRealTimeFeatureHolder)) {
    return NS_ERROR_FAILURE;
  }

  // Get the features that are used for the local list lookup.
  nsTArray<RefPtr<nsIUrlClassifierFeature>> localListFeatures;
  mozilla::net::UrlClassifierFeatureFactory::GetPhishingProtectionFeatures(
      localListFeatures);

  // No local list features found, bail out earlier.
  if (localListFeatures.IsEmpty()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Create the feature holder for the local list features.
  mLocalListFeatureHolder = nsUrlClassifierDBService::FeatureHolder::Create(
      aURI, localListFeatures, nsIUrlClassifierFeature::blocklist);
  if (NS_WARN_IF(!mLocalListFeatureHolder)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult nsUrlClassifierRealTimeLookupHandler::HandleRealTimeLookupComplete(
    UniquePtr<LookupResultArray>& aResults) {
  MOZ_ASSERT(!mHasCompletedRealTimeLookup);

  mHasCompletedRealTimeLookup = true;

  bool hasGlobalCacheHit = aResults && !aResults->IsEmpty();

  // Record Global Cache hit/miss telemetry.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "nsUrlClassifierRealTimeLookupHandler::RecordGlobalCacheTelemetry",
      [hasGlobalCacheHit, isPrivate = mIsPrivate]() {
        const nsCString& etpCategory =
            RealTimeRequestSimulator::ContentBlockingCategory();
        if (!etpCategory.IsEmpty()) {
          nsAutoCString label;
          if (etpCategory.EqualsLiteral("standard") ||
              etpCategory.EqualsLiteral("strict") ||
              etpCategory.EqualsLiteral("custom")) {
            label = etpCategory;
          } else {
            label = "other"_ns;
          }
          label.Append('_');
          label.Append(isPrivate ? "private"_ns : "normal"_ns);

          if (hasGlobalCacheHit) {
            glean::urlclassifier::global_cache_hit.Get(label).Add(1);
          } else {
            glean::urlclassifier::global_cache_miss.Get(label).Add(1);
          }
        }
      }));

  // Notify observers about GlobalCache lookup result (for testing).
  if (mDebugEnabled) {
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "nsUrlClassifierRealTimeLookupHandler::NotifyGlobalCacheResult",
        [hasGlobalCacheHit]() {
          nsCOMPtr<nsIObserverService> observerService =
              mozilla::services::GetObserverService();
          if (observerService) {
            const char16_t* result = hasGlobalCacheHit ? u"hit" : u"miss";
            observerService->NotifyObservers(
                nullptr, "urlclassifier-globalcache-result", result);
          }
        }));
  }

  // Simulation: estimate real-time request traffic when GlobalCache misses.
  if (!hasGlobalCacheHit && mSimulator) {
    mSimulator->SimulateRealTimeRequest(mKey, mIsPrivate);
  }

  // ToDo(Bug 2010022): No results, we need to perform a real-time request. For
  // now, we will just continue with the local list lookup.

  // Continue with the local list lookup.
  mDBService->LookupURIWithoutProxy(mKey, mLocalListFeatureHolder, this);

  return NS_OK;
}

nsresult nsUrlClassifierRealTimeLookupHandler::HandleLocalListLookupComplete(
    UniquePtr<LookupResultArray>& aResults) {
  MOZ_ASSERT(mHasCompletedRealTimeLookup);

  // Dispatch the local list lookup handling to the main thread because we need
  // to handle the lookup result and start the hash completion request in the
  // main thread.
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "nsUrlClassifierRealTimeLookupHandler::HandleLocalListLookupComplete",
      [self = RefPtr{this}, results = std::move(aResults)]() mutable {
        self->mLocalListResults = std::move(results);

        if (self->mLocalListResults) {
          nsresult rv =
              self->RequestHashCompletions(self->mLocalListResults.get());
          if (NS_FAILED(rv)) {
            NS_WARNING("Failed to request hash completions");
            self->OnAllCompletionsFinished();
            return;
          }
        }

        if (self->mPendingCompletions == 0) {
          self->OnAllCompletionsFinished();
        }
      }));

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierRealTimeLookupHandler::LookupComplete(
    UniquePtr<LookupResultArray> aResults) {
  MOZ_ASSERT(!NS_IsMainThread());

  if (!mHasCompletedRealTimeLookup) {
    return HandleRealTimeLookupComplete(aResults);
  }

  return HandleLocalListLookupComplete(aResults);
}

struct LiteralProvider {
  nsLiteralCString name;
  uint8_t priority;
};

struct Provider {
  nsCString name;
  uint8_t priority;
};

// Order matters
// Provider which is not included in this table has the lowest priority 0
static constexpr LiteralProvider kBuiltInProviders[] = {
    {"mozilla"_ns, 1},
    {"google4"_ns, 2},
    {"google"_ns, 3},
};

// -------------------------------------------------------------------------
// Helper class for nsIURIClassifier implementation, handle classify result and
// send back to nsIURIClassifier

class nsUrlClassifierClassifyCallback final
    : public nsIUrlClassifierCallback,
      public nsIUrlClassifierClassifyCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIURLCLASSIFIERCALLBACK
  NS_DECL_NSIURLCLASSIFIERCLASSIFYCALLBACK

  explicit nsUrlClassifierClassifyCallback(nsIURIClassifierCallback* c)
      : mCallback(c) {}

 private:
  struct ClassifyMatchedInfo {
    nsCString table;
    nsCString fullhash;
    Provider provider;
    nsresult errorCode;
  };

  ~nsUrlClassifierClassifyCallback() = default;

  nsCOMPtr<nsIURIClassifierCallback> mCallback;
  nsTArray<ClassifyMatchedInfo> mMatchedArray;
};

NS_IMPL_ISUPPORTS(nsUrlClassifierClassifyCallback, nsIUrlClassifierCallback,
                  nsIUrlClassifierClassifyCallback)

NS_IMETHODIMP
nsUrlClassifierClassifyCallback::HandleEvent(const nsACString& tables) {
  nsresult response = TablesToResponse(tables);
  ClassifyMatchedInfo* matchedInfo = nullptr;

  if (NS_FAILED(response)) {
    // Filter all matched info which has correct response
    // In the case multiple tables found, use the higher priority provider
    nsTArray<ClassifyMatchedInfo> matches;
    for (uint32_t i = 0; i < mMatchedArray.Length(); i++) {
      if (mMatchedArray[i].errorCode == response &&
          (!matchedInfo || matchedInfo->provider.priority <
                               mMatchedArray[i].provider.priority)) {
        matchedInfo = &mMatchedArray[i];
      }
    }
  }

  nsCString provider = matchedInfo ? matchedInfo->provider.name : ""_ns;
  nsCString fullhash = matchedInfo ? matchedInfo->fullhash : ""_ns;
  nsCString table = matchedInfo ? matchedInfo->table : ""_ns;

  mCallback->OnClassifyComplete(response, table, provider, fullhash);
  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierClassifyCallback::HandleResult(const nsACString& aTable,
                                              const nsACString& aFullHash) {
  LOG(
      ("nsUrlClassifierClassifyCallback::HandleResult [%p, table %s full hash "
       "%s]",
       this, PromiseFlatCString(aTable).get(),
       PromiseFlatCString(aFullHash).get()));

  if (NS_WARN_IF(aTable.IsEmpty()) || NS_WARN_IF(aFullHash.IsEmpty())) {
    return NS_ERROR_INVALID_ARG;
  }

  ClassifyMatchedInfo* matchedInfo = mMatchedArray.AppendElement();
  matchedInfo->table = aTable;
  matchedInfo->fullhash = aFullHash;

  nsUrlClassifierUtils* urlUtil = nsUrlClassifierUtils::GetInstance();
  if (NS_WARN_IF(!urlUtil)) {
    return NS_ERROR_FAILURE;
  }

  nsCString provider;
  nsresult rv = urlUtil->GetProvider(aTable, provider);

  if (NS_SUCCEEDED(rv)) {
    matchedInfo->provider.name = std::move(provider);
  } else {
    matchedInfo->provider.name = ""_ns;
  }
  matchedInfo->provider.priority = 0;
  for (auto const& BuiltInProvider : kBuiltInProviders) {
    if (BuiltInProvider.name.Equals(matchedInfo->provider.name)) {
      matchedInfo->provider.priority = BuiltInProvider.priority;
    }
  }
  matchedInfo->errorCode = TablesToResponse(aTable);

  return NS_OK;
}

// -------------------------------------------------------------------------
// Proxy class implementation

NS_IMPL_ADDREF(nsUrlClassifierDBService)
NS_IMPL_RELEASE(nsUrlClassifierDBService)
NS_INTERFACE_MAP_BEGIN(nsUrlClassifierDBService)
  // Only nsIURIClassifier is supported in the content process!
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIUrlClassifierDBService,
                                     XRE_IsParentProcess())
  NS_INTERFACE_MAP_ENTRY(nsIURIClassifier)
  NS_INTERFACE_MAP_ENTRY(nsIUrlClassifierInfo)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIObserver, XRE_IsParentProcess())
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIURIClassifier)
NS_INTERFACE_MAP_END

/* static */
already_AddRefed<nsUrlClassifierDBService>
nsUrlClassifierDBService::GetInstance(nsresult* result) {
  *result = NS_OK;
  if (!sUrlClassifierDBService) {
    sUrlClassifierDBService = new (fallible) nsUrlClassifierDBService();
    if (!sUrlClassifierDBService) {
      *result = NS_ERROR_OUT_OF_MEMORY;
      return nullptr;
    }

    *result = sUrlClassifierDBService->Init();
    if (NS_FAILED(*result)) {
      return nullptr;
    }
  }
  return do_AddRef(sUrlClassifierDBService);
}

nsUrlClassifierDBService::nsUrlClassifierDBService()
    : mInUpdate(false),
      mDisallowCompletionsTablesLock(
          "nsUrlClassifierDBService::mDisallowCompletionsTables") {}

nsUrlClassifierDBService::~nsUrlClassifierDBService() {
  sUrlClassifierDBService = nullptr;
}

nsresult nsUrlClassifierDBService::ReadDisallowCompletionsTablesFromPrefs() {
  nsAutoCString tables;

  Preferences::GetCString(DISALLOW_COMPLETION_TABLE_PREF, tables);

  nsTArray<nsCString> parsed;
  Classifier::SplitTables(tables, parsed);

  MutexAutoLock lock(mDisallowCompletionsTablesLock);
  mDisallowCompletionsTables = std::move(parsed);

  return NS_OK;
}

nsresult nsUrlClassifierDBService::Init() {
  MOZ_ASSERT(NS_IsMainThread(), "Must initialize DB service on main thread");

  switch (XRE_GetProcessType()) {
    case GeckoProcessType_Default:
      // The parent process is supported.
      break;
    case GeckoProcessType_Content:
      // In a content process, we simply forward all requests to the parent
      // process, so we can skip the initialization steps here. Note that since
      // we never register an observer, Shutdown() will also never be called in
      // the content process.
      return NS_OK;
    default:
      // No other process type is supported!
      return NS_ERROR_NOT_AVAILABLE;
  }

  uint32_t hashNoise =
      Preferences::GetUint(GETHASH_NOISE_PREF, GETHASH_NOISE_DEFAULT);
  ReadDisallowCompletionsTablesFromPrefs();

  // Force nsUrlClassifierUtils loading on main thread.
  if (NS_WARN_IF(!nsUrlClassifierUtils::GetInstance())) {
    return NS_ERROR_FAILURE;
  }

  // Directory providers must also be accessed on the main thread.
  nsresult rv;
  nsCOMPtr<nsIFile> cacheDir;
  rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_LOCAL_50_DIR,
                              getter_AddRefs(cacheDir));
  if (NS_FAILED(rv)) {
    rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                getter_AddRefs(cacheDir));
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  // Start the background thread.
  rv = NS_NewNamedThread("URL Classifier", &gDbBackgroundThread);
  if (NS_FAILED(rv)) return rv;

  mWorker = new (fallible) nsUrlClassifierDBServiceWorker();
  if (!mWorker) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  rv = mWorker->Init(hashNoise, cacheDir, this);
  if (NS_FAILED(rv)) {
    mWorker = nullptr;
    return rv;
  }

  // Proxy for calling the worker on the background thread
  mWorkerProxy = new UrlClassifierDBServiceWorkerProxy(mWorker);
  rv = mWorkerProxy->OpenDb();
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Add an observer for shutdown
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (!observerService) return NS_ERROR_FAILURE;

  // The application is about to quit
  observerService->AddObserver(this, "quit-application", false);
  observerService->AddObserver(this, "profile-before-change", false);

  Preferences::AddStrongObserver(this, DISALLOW_COMPLETION_TABLE_PREF);

  // Initialize the ContentClassifierService early so that filter lists are
  // loaded before the first network request is classified.
  RefPtr<ContentClassifierService> contentClassifier =
      ContentClassifierService::GetInstance();

  return NS_OK;
}

// nsChannelClassifier is the only consumer of this interface.
NS_IMETHODIMP
nsUrlClassifierDBService::Classify(nsIPrincipal* aPrincipal,
                                   nsIURIClassifierCallback* c, bool* aResult) {
  NS_ENSURE_ARG(aPrincipal);
  MOZ_ASSERT(c);
  NS_ENSURE_ARG(aResult);

  if (aPrincipal->IsSystemPrincipal()) {
    *aResult = false;
    return NS_OK;
  }

  if (XRE_IsContentProcess()) {
    using namespace mozilla::dom;

    ContentChild* content = ContentChild::GetSingleton();
    MOZ_ASSERT(content);

    auto principal = WrapNotNull(aPrincipal);
    auto actor = static_cast<URLClassifierChild*>(
        content->AllocPURLClassifierChild(principal, aResult));
    MOZ_ASSERT(actor);

    if (!content->SendPURLClassifierConstructor(actor, principal, aResult)) {
      *aResult = false;
      return NS_ERROR_FAILURE;
    }

    actor->SetCallback(c);
    return NS_OK;
  }

  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  nsCOMPtr<nsIPermissionManager> permissionManager =
      components::PermissionManager::Service();
  if (NS_WARN_IF(!permissionManager)) {
    return NS_ERROR_FAILURE;
  }

  uint32_t perm;
  nsresult rv = permissionManager->TestPermissionFromPrincipal(
      aPrincipal, "safe-browsing"_ns, &perm);
  NS_ENSURE_SUCCESS(rv, rv);

  if (perm == nsIPermissionManager::ALLOW_ACTION) {
    *aResult = false;
    return NS_OK;
  }

  if (Classifier::IsRealTimeModeEnabled()) {
    RefPtr<nsUrlClassifierClassifyCallback> callback =
        new (fallible) nsUrlClassifierClassifyCallback(c);
    if (NS_WARN_IF(!callback)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    RefPtr<nsUrlClassifierRealTimeLookupHandler> handler =
        new (fallible) nsUrlClassifierRealTimeLookupHandler(
            this, callback, aPrincipal->GetIsInPrivateBrowsing());
    if (NS_WARN_IF(!handler)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    rv = handler->StartRealTimeLookup(aPrincipal);
    if (rv == NS_ERROR_NOT_AVAILABLE) {
      // No SafeBrowsing features are configured, so there is nothing to
      // classify and no callback will be fired.
      *aResult = false;
      return NS_OK;
    }
    NS_ENSURE_SUCCESS(rv, rv);

    *aResult = true;
    return NS_OK;
  }

  nsTArray<RefPtr<nsIUrlClassifierFeature>> features;
  mozilla::net::UrlClassifierFeatureFactory::GetPhishingProtectionFeatures(
      features);
  if (features.IsEmpty()) {
    *aResult = false;
    return NS_OK;
  }

  nsCOMPtr<nsIURI> uri;
  // Casting to BasePrincipal, as we can't get InnerMost URI otherwise
  auto* basePrincipal = BasePrincipal::Cast(aPrincipal);
  rv = basePrincipal->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

  // Let's keep the features alive and release them on the correct thread.
  RefPtr<FeatureHolder> holder =
      FeatureHolder::Create(uri, features, nsIUrlClassifierFeature::blocklist);
  if (NS_WARN_IF(!holder)) {
    return NS_ERROR_FAILURE;
  }

  uri = NS_GetInnermostURI(uri);
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

  nsUrlClassifierUtils* utilsService = nsUrlClassifierUtils::GetInstance();
  if (NS_WARN_IF(!utilsService)) {
    return NS_ERROR_FAILURE;
  }

  // Canonicalize the url
  nsAutoCString key;
  rv = utilsService->GetKeyForURI(uri, key);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsUrlClassifierClassifyCallback> callback =
      new (fallible) nsUrlClassifierClassifyCallback(c);
  if (NS_WARN_IF(!callback)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // The rest is done async.
  rv = LookupURI(key, holder, callback);
  NS_ENSURE_SUCCESS(rv, rv);

  *aResult = true;
  return NS_OK;
}

class ThreatHitReportListener final : public nsIStreamListener {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  ThreatHitReportListener() = default;

 private:
  ~ThreatHitReportListener() = default;
};

NS_IMPL_ISUPPORTS(ThreatHitReportListener, nsIStreamListener,
                  nsIRequestObserver)

NS_IMETHODIMP
ThreatHitReportListener::OnStartRequest(nsIRequest* aRequest) {
  if (!LOG_ENABLED()) {
    return NS_OK;  // Nothing to do!
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);
  NS_ENSURE_TRUE(httpChannel, NS_OK);

  nsresult rv, status;
  rv = httpChannel->GetStatus(&status);
  NS_ENSURE_SUCCESS(rv, NS_OK);
  nsAutoCString errorName;
  mozilla::GetErrorName(status, errorName);

  uint32_t requestStatus;
  rv = httpChannel->GetResponseStatus(&requestStatus);
  NS_ENSURE_SUCCESS(rv, NS_OK);

  nsAutoCString spec;
  nsCOMPtr<nsIURI> uri;
  rv = httpChannel->GetURI(getter_AddRefs(uri));
  if (NS_SUCCEEDED(rv) && uri) {
    uri->GetAsciiSpec(spec);
  }
  nsCOMPtr<nsIURLFormatter> urlFormatter =
      do_GetService("@mozilla.org/toolkit/URLFormatterService;1");
  nsAutoString trimmed;
  rv = urlFormatter->TrimSensitiveURLs(NS_ConvertUTF8toUTF16(spec), trimmed);
  NS_ENSURE_SUCCESS(rv, NS_OK);

  LOG(
      ("ThreatHitReportListener::OnStartRequest "
       "(status=%s, code=%d, uri=%s, this=%p)",
       errorName.get(), requestStatus, NS_ConvertUTF16toUTF8(trimmed).get(),
       this));

  return NS_OK;
}

NS_IMETHODIMP
ThreatHitReportListener::OnDataAvailable(nsIRequest* aRequest,
                                         nsIInputStream* aInputStream,
                                         uint64_t aOffset, uint32_t aCount) {
  return NS_OK;
}

NS_IMETHODIMP
ThreatHitReportListener::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);
  NS_ENSURE_TRUE(httpChannel, aStatus);

  uint8_t netErrCode =
      NS_FAILED(aStatus) ? mozilla::safebrowsing::NetworkErrorToBucket(aStatus)
                         : 0;
  mozilla::glean::urlclassifier::threathit_network_error.AccumulateSingleSample(
      netErrCode);

  uint32_t requestStatus;
  nsresult rv = httpChannel->GetResponseStatus(&requestStatus);
  NS_ENSURE_SUCCESS(rv, aStatus);
  mozilla::glean::urlclassifier::threathit_remote_status.AccumulateSingleSample(
      mozilla::safebrowsing::HTTPStatusToBucket(requestStatus));

  if (LOG_ENABLED()) {
    nsAutoCString errorName;
    mozilla::GetErrorName(aStatus, errorName);

    nsAutoCString spec;
    nsCOMPtr<nsIURI> uri;
    rv = httpChannel->GetURI(getter_AddRefs(uri));
    if (NS_SUCCEEDED(rv) && uri) {
      uri->GetAsciiSpec(spec);
    }
    nsCOMPtr<nsIURLFormatter> urlFormatter =
        do_GetService("@mozilla.org/toolkit/URLFormatterService;1");
    nsString trimmed;
    rv = urlFormatter->TrimSensitiveURLs(NS_ConvertUTF8toUTF16(spec), trimmed);
    NS_ENSURE_SUCCESS(rv, aStatus);

    LOG(
        ("ThreatHitReportListener::OnStopRequest "
         "(status=%s, code=%d, uri=%s, this=%p)",
         errorName.get(), requestStatus, NS_ConvertUTF16toUTF8(trimmed).get(),
         this));
  }

  return aStatus;
}

NS_IMETHODIMP
nsUrlClassifierDBService::SendThreatHitReport(nsIChannel* aChannel,
                                              const nsACString& aProvider,
                                              const nsACString& aList,
                                              const nsACString& aFullHash) {
  NS_ENSURE_ARG_POINTER(aChannel);

  if (aProvider.IsEmpty()) {
    LOG(("nsUrlClassifierDBService::SendThreatHitReport missing provider"));
    return NS_ERROR_FAILURE;
  }
  if (aList.IsEmpty()) {
    LOG(("nsUrlClassifierDBService::SendThreatHitReport missing list"));
    return NS_ERROR_FAILURE;
  }
  if (aFullHash.IsEmpty()) {
    LOG(("nsUrlClassifierDBService::SendThreatHitReport missing fullhash"));
    return NS_ERROR_FAILURE;
  }

  nsPrintfCString reportUrlPref(
      "browser.safebrowsing.provider.%s.dataSharingURL",
      PromiseFlatCString(aProvider).get());

  nsCOMPtr<nsIURLFormatter> formatter(
      do_GetService("@mozilla.org/toolkit/URLFormatterService;1"));
  if (!formatter) {
    return NS_ERROR_UNEXPECTED;
  }

  nsString urlStr;
  nsresult rv =
      formatter->FormatURLPref(NS_ConvertUTF8toUTF16(reportUrlPref), urlStr);
  NS_ENSURE_SUCCESS(rv, rv);

  if (urlStr.IsEmpty() || u"about:blank"_ns.Equals(urlStr)) {
    LOG(("%s is missing a ThreatHit data reporting URL.",
         PromiseFlatCString(aProvider).get()));
    return NS_OK;
  }

  nsUrlClassifierUtils* utilsService = nsUrlClassifierUtils::GetInstance();
  if (NS_WARN_IF(!utilsService)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString reportBody;
  rv =
      utilsService->MakeThreatHitReport(aChannel, aList, aFullHash, reportBody);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIStringInputStream> sis(
      do_CreateInstance(NS_STRINGINPUTSTREAM_CONTRACTID));
  rv = sis->SetByteStringData(reportBody);
  NS_ENSURE_SUCCESS(rv, rv);

  LOG(("Sending the following ThreatHit report to %s about %s: %s",
       PromiseFlatCString(aProvider).get(), PromiseFlatCString(aList).get(),
       reportBody.get()));

  nsCOMPtr<nsIURI> reportURI;
  rv = NS_NewURI(getter_AddRefs(reportURI), urlStr);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t loadFlags = nsIRequest::LOAD_ANONYMOUS |  // no cookies
                       nsIChannel::INHIBIT_CACHING |
                       nsIChannel::LOAD_BYPASS_CACHE;

  nsCOMPtr<nsIChannel> reportChannel;
  rv = NS_NewChannel(getter_AddRefs(reportChannel), reportURI,
                     nsContentUtils::GetSystemPrincipal(),
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_OTHER,
                     nullptr,  // nsICookieJarSettings
                     nullptr,  // aPerformanceStorage
                     nullptr,  // aLoadGroup
                     nullptr, loadFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILoadInfo> loadInfo = reportChannel->LoadInfo();
  mozilla::OriginAttributes attrs;
  attrs.mFirstPartyDomain.AssignLiteral(NECKO_SAFEBROWSING_FIRST_PARTY_DOMAIN);
  loadInfo->SetOriginAttributes(attrs);

  nsCOMPtr<nsIUploadChannel> uploadChannel(do_QueryInterface(reportChannel));
  NS_ENSURE_TRUE(uploadChannel, NS_ERROR_FAILURE);
  rv = uploadChannel->SetUploadStream(sis, "application/x-protobuf"_ns, -1);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(reportChannel));
  NS_ENSURE_TRUE(httpChannel, NS_ERROR_FAILURE);
  rv = httpChannel->SetRequestMethod("POST"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  // Disable keepalive.
  rv = httpChannel->SetRequestHeader("Connection"_ns, "close"_ns, false);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<ThreatHitReportListener> listener = new ThreatHitReportListener();
  rv = reportChannel->AsyncOpen(listener);
  if (NS_FAILED(rv)) {
    LOG(("Failure to send Safe Browsing ThreatHit report"));
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBService::Lookup(nsIPrincipal* aPrincipal,
                                 const nsACString& aTables,
                                 nsIUrlClassifierCallback* aCallback) {
  // We don't expect someone with SystemPrincipal calls this API(See Bug
  // 813897).
  MOZ_ASSERT(!aPrincipal->IsSystemPrincipal());

  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  nsTArray<nsCString> tableArray;
  Classifier::SplitTables(aTables, tableArray);

  nsCOMPtr<nsIUrlClassifierFeature> feature;
  nsresult rv = CreateFeatureWithTables(
      "lookup"_ns, tableArray, nsTArray<nsCString>(), getter_AddRefs(feature));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> uri;
  // Casting to BasePrincipal, as we can't get InnerMost URI otherwise
  auto* basePrincipal = BasePrincipal::Cast(aPrincipal);
  rv = basePrincipal->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

  nsTArray<RefPtr<nsIUrlClassifierFeature>> features;
  features.AppendElement(feature.get());

  // Let's keep the features alive and release them on the correct thread.
  RefPtr<FeatureHolder> holder =
      FeatureHolder::Create(uri, features, nsIUrlClassifierFeature::blocklist);
  if (NS_WARN_IF(!holder)) {
    return NS_ERROR_FAILURE;
  }

  uri = NS_GetInnermostURI(uri);
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

  nsUrlClassifierUtils* utilsService = nsUrlClassifierUtils::GetInstance();
  if (NS_WARN_IF(!utilsService)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString key;
  // Canonicalize the url
  rv = utilsService->GetKeyForURI(uri, key);
  NS_ENSURE_SUCCESS(rv, rv);

  return LookupURI(key, holder, aCallback);
}

nsresult nsUrlClassifierDBService::LookupURIWithoutProxy(
    const nsACString& aKey, FeatureHolder* aHolder,
    nsIUrlClassifierLookupCallback* aCallback) {
  MOZ_ASSERT(aHolder);
  MOZ_ASSERT(aCallback);

  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  // Queue this lookup and call the lookup function to flush the queue if
  // necessary.
  nsresult rv = mWorker->QueueLookup(aKey, aHolder, aCallback);
  NS_ENSURE_SUCCESS(rv, rv);

  // This seems to just call HandlePendingLookups.
  nsAutoCString dummy;
  return mWorkerProxy->Lookup(nullptr, dummy, nullptr);
}

nsresult nsUrlClassifierDBService::LookupURI(
    const nsACString& aKey, FeatureHolder* aHolder,
    nsIUrlClassifierCallback* aCallback) {
  MOZ_ASSERT(aHolder);
  MOZ_ASSERT(aCallback);

  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  // Create an nsUrlClassifierLookupCallback object.  This object will
  // take care of confirming partial hash matches if necessary before
  // calling the client's callback.
  nsCOMPtr<nsIUrlClassifierLookupCallback> callback =
      new nsUrlClassifierLookupCallback(this, aCallback);

  nsCOMPtr<nsIUrlClassifierLookupCallback> proxyCallback =
      new UrlClassifierLookupCallbackProxy(callback);

  // Queue this lookup and call the lookup function to flush the queue if
  // necessary.
  nsresult rv = mWorker->QueueLookup(aKey, aHolder, proxyCallback);
  NS_ENSURE_SUCCESS(rv, rv);

  // This seems to just call HandlePendingLookups.
  nsAutoCString dummy;
  return mWorkerProxy->Lookup(nullptr, dummy, nullptr);
}

NS_IMETHODIMP
nsUrlClassifierDBService::GetTables(nsIUrlClassifierCallback* c) {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  // The proxy callback uses the current thread.
  nsCOMPtr<nsIUrlClassifierCallback> proxyCallback =
      new UrlClassifierCallbackProxy(c);

  return mWorkerProxy->GetTables(proxyCallback);
}

NS_IMETHODIMP
nsUrlClassifierDBService::SetHashCompleter(
    const nsACString& tableName, nsIUrlClassifierHashCompleter* completer) {
  if (completer) {
    mCompleters.InsertOrUpdate(tableName, completer);
  } else {
    mCompleters.Remove(tableName);
  }
  ClearLastResults();
  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBService::ClearLastResults() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  return mWorkerProxy->ClearLastResults();
}

NS_IMETHODIMP
nsUrlClassifierDBService::BeginUpdate(nsIUrlClassifierUpdateObserver* observer,
                                      const nsACString& updateTables,
                                      const nsACString& provider) {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  if (mInUpdate) {
    LOG(("Already updating, not available"));
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (mWorker->IsBusyUpdating()) {
    // |mInUpdate| used to work well because "notifying update observer"
    // is synchronously done in Worker::FinishUpdate(). Even if the
    // update observer hasn't been notified at this point, we can still
    // dispatch BeginUpdate() since it will NOT be run until the
    // previous Worker::FinishUpdate() returns.
    //
    // However, some tasks in Worker::FinishUpdate() have been moved to
    // another thread. The update observer will NOT be notified when
    // Worker::FinishUpdate() returns. If we only check |mInUpdate|,
    // the following sequence might happen on worker thread:
    //
    // Worker::FinishUpdate() // for update 1
    // Worker::BeginUpdate()  // for update 2
    // Worker::NotifyUpdateObserver() // for update 1
    //
    // So, we have to find out a way to reject BeginUpdate() right here
    // if the previous update observer hasn't been notified.
    //
    // Directly probing the worker's state is the most lightweight solution.
    // No lock is required since Worker::BeginUpdate() and
    // Worker::NotifyUpdateObserver() are by nature mutual exclusive.
    // (both run on worker thread.)
    LOG(("The previous update observer hasn't been notified."));
    return NS_ERROR_NOT_AVAILABLE;
  }

  mInUpdate = true;

  // The proxy observer uses the current thread
  nsCOMPtr<nsIUrlClassifierUpdateObserver> proxyObserver =
      new UrlClassifierUpdateObserverProxy(observer);

  return mWorkerProxy->BeginUpdate(proxyObserver, updateTables, provider);
}

NS_IMETHODIMP
nsUrlClassifierDBService::BeginStream(const nsACString& table) {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  return mWorkerProxy->BeginStream(table);
}

NS_IMETHODIMP
nsUrlClassifierDBService::UpdateStream(const nsACString& aUpdateChunk) {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  return mWorkerProxy->UpdateStream(aUpdateChunk);
}

NS_IMETHODIMP
nsUrlClassifierDBService::FinishStream() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  return mWorkerProxy->FinishStream();
}

NS_IMETHODIMP
nsUrlClassifierDBService::FinishUpdate() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  mInUpdate = false;

  return mWorkerProxy->FinishUpdate();
}

NS_IMETHODIMP
nsUrlClassifierDBService::CancelUpdate() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  mInUpdate = false;

  return mWorkerProxy->CancelUpdate();
}

NS_IMETHODIMP
nsUrlClassifierDBService::ResetDatabase() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  if (mWorker->IsBusyUpdating()) {
    LOG(("Failed to ResetDatabase because of the unfinished update."));
    return NS_ERROR_FAILURE;
  }

  return mWorkerProxy->ResetDatabase();
}

NS_IMETHODIMP
nsUrlClassifierDBService::ReloadDatabase() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  if (mWorker->IsBusyUpdating()) {
    LOG(("Failed to ReloadDatabase because of the unfinished update."));
    return NS_ERROR_FAILURE;
  }

  return mWorkerProxy->ReloadDatabase();
}

NS_IMETHODIMP
nsUrlClassifierDBService::ClearCache() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  return mWorkerProxy->ClearCache();
}

NS_IMETHODIMP
nsUrlClassifierDBService::CleanRealTimeSimulatorCache() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  RefPtr<RealTimeRequestSimulator> simulator =
      RealTimeRequestSimulator::GetInstance();

  BackgroundThread()->Dispatch(NS_NewRunnableFunction(
      "nsUrlClassifierDBService::CleanRealTimeSimulatorCache", [simulator]() {
        if (simulator) {
          simulator->CleanCache();
        }
      }));

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBService::ExpireRealTimeSimulatorCache() {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  RefPtr<RealTimeRequestSimulator> simulator =
      RealTimeRequestSimulator::GetInstance();

  BackgroundThread()->Dispatch(NS_NewRunnableFunction(
      "nsUrlClassifierDBService::ExpireRealTimeSimulatorCache", [simulator]() {
        if (simulator) {
          simulator->ExpireCache();
        }
      }));

  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBService::GetCacheInfo(
    const nsACString& aTable, nsIUrlClassifierGetCacheCallback* aCallback) {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  return mWorkerProxy->GetCacheInfo(aTable, aCallback);
}

nsresult nsUrlClassifierDBService::CacheCompletions(
    const ConstCacheResultArray& results) {
  NS_ENSURE_TRUE(gDbBackgroundThread, NS_ERROR_NOT_INITIALIZED);

  return mWorkerProxy->CacheCompletions(results);
}

bool nsUrlClassifierDBService::CanComplete(const nsACString& aTableName) {
  MutexAutoLock lock(mDisallowCompletionsTablesLock);
  return !mDisallowCompletionsTables.Contains(aTableName);
}

bool nsUrlClassifierDBService::GetCompleter(
    const nsACString& tableName, nsIUrlClassifierHashCompleter** completer) {
  // If we have specified a completer, go ahead and query it. This is only
  // used by tests.
  if (mCompleters.Get(tableName, completer)) {
    return true;
  }

  if (!CanComplete(tableName)) {
    return false;
  }

  // Otherwise, call gethash to find the hash completions.
  return NS_SUCCEEDED(
      CallGetService(NS_URLCLASSIFIERHASHCOMPLETER_CONTRACTID, completer));
}

NS_IMETHODIMP
nsUrlClassifierDBService::Observe(nsISupports* aSubject, const char* aTopic,
                                  const char16_t* aData) {
  if (!strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    ReadDisallowCompletionsTablesFromPrefs();
  } else if (!strcmp(aTopic, "quit-application")) {
    // Tell the update thread to finish as soon as possible.
    gShuttingDownThread = true;

    // The code in ::Shutdown() is run on a 'profile-before-change' event and
    // ensures that objects are freed by blocking on this freeing.
    // We can however speed up the shutdown time by using the worker thread to
    // release, in an earlier event, any objects that cannot affect an ongoing
    // update on the update thread.
    PreShutdown();
  } else if (!strcmp(aTopic, "profile-before-change")) {
    gShuttingDownThread = true;
    Shutdown();
  } else {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

// Post a PreShutdown task to worker thread to release objects without blocking
// main-thread. Notice that shutdown process may still be blocked by PreShutdown
// task when ::Shutdown() is executed and synchronously waits for worker thread
// to finish PreShutdown event.
nsresult nsUrlClassifierDBService::PreShutdown() {
  MOZ_ASSERT(XRE_IsParentProcess());

  if (mWorkerProxy) {
    mWorkerProxy->PreShutdown();
  }

  return NS_OK;
}

// Join the background thread if it exists.
nsresult nsUrlClassifierDBService::Shutdown() {
  LOG(("shutting down db service\n"));
  MOZ_ASSERT(XRE_IsParentProcess());

  if (!gDbBackgroundThread) {
    return NS_OK;
  }

  auto timer = glean::urlclassifier::shutdown_time.Measure();

  mCompleters.Clear();

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefs) {
    prefs->RemoveObserver(DISALLOW_COMPLETION_TABLE_PREF, this);
  }

  // 1. Synchronize with worker thread and update thread by
  //    *synchronously* dispatching an event to worker thread
  //    for shutting down the update thread. The reason not
  //    shutting down update thread directly from main thread
  //    is to avoid racing for Classifier::mUpdateThread
  //    between main thread and the worker thread. (Both threads
  //    would access Classifier::mUpdateThread.)
  //    This event is dispatched unconditionally to avoid
  //    accessing mWorker->mClassifier on the main thread, which
  //    would be a race.
  using Worker = nsUrlClassifierDBServiceWorker;
  RefPtr<nsIRunnable> r = NewRunnableMethod(
      "nsUrlClassifierDBServiceWorker::FlushAndDisableAsyncUpdate", mWorker,
      &Worker::FlushAndDisableAsyncUpdate);
  SyncRunnable::DispatchToThread(gDbBackgroundThread, r);
  // At this point the update thread has been shut down and
  // the worker thread should only have at most one event,
  // which is the callback event.

  // 2. Send CancelUpdate() event to notify the dangling update.
  //    (i.e. BeginUpdate is called but FinishUpdate is not.)
  //    and CloseDb() to clear mClassifier. They will be the last two
  //    events on the worker thread in the shutdown process.
  DebugOnly<nsresult> rv;
  rv = mWorkerProxy->CancelUpdate();
  MOZ_ASSERT(NS_SUCCEEDED(rv), "failed to post 'cancel update' event");
  rv = mWorkerProxy->CloseDb();
  MOZ_ASSERT(NS_SUCCEEDED(rv), "failed to post 'close db' event");
  mWorkerProxy = nullptr;

  // 3. Invalidate XPCOM APIs by nulling out gDbBackgroundThread
  //    since every API checks gDbBackgroundThread first. This has
  //    to be done before calling nsIThread.shutdown because it
  //    will cause the pending events on the joining thread to
  //    be processed.
  nsIThread* backgroundThread = nullptr;
  std::swap(backgroundThread, gDbBackgroundThread);

  // 4. Wait until the worker thread is down.
  if (backgroundThread) {
    backgroundThread->Shutdown();
    NS_RELEASE(backgroundThread);
  }

  mWorker = nullptr;
  return NS_OK;
}

nsIThread* nsUrlClassifierDBService::BackgroundThread() {
  return gDbBackgroundThread;
}

// static
bool nsUrlClassifierDBService::ShutdownHasStarted() {
  return gShuttingDownThread;
}

// static
nsUrlClassifierDBServiceWorker* nsUrlClassifierDBService::GetWorker() {
  nsresult rv;
  RefPtr<nsUrlClassifierDBService> service =
      nsUrlClassifierDBService::GetInstance(&rv);
  if (!service) {
    return nullptr;
  }

  return service->mWorker;
}

NS_IMETHODIMP
nsUrlClassifierDBService::AsyncClassifyLocalWithFeatures(
    nsIURI* aURI, const nsTArray<RefPtr<nsIUrlClassifierFeature>>& aFeatures,
    nsIUrlClassifierFeature::listType aListType,
    nsIUrlClassifierFeatureCallback* aCallback, bool aIdlePriority) {
  MOZ_ASSERT(NS_IsMainThread());

  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  nsCOMPtr<nsIURI> uri = NS_GetInnermostURI(aURI);
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

  // Let's try to use the preferences.
  if (AsyncClassifyLocalWithFeaturesUsingPreferences(uri, aFeatures, aListType,
                                                     aCallback)) {
    return NS_OK;
  }

  nsUrlClassifierUtils* utilsService = nsUrlClassifierUtils::GetInstance();
  if (NS_WARN_IF(!utilsService)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString key;
  // Canonicalize the url
  nsresult rv = utilsService->GetKeyForURI(uri, key);
  NS_ENSURE_SUCCESS(rv, rv);

  if (XRE_IsContentProcess()) {
    using namespace mozilla::dom;
    using namespace mozilla::ipc;

    ContentChild* content = ContentChild::GetSingleton();
    if (NS_WARN_IF(!content || content->IsShuttingDown())) {
      return NS_ERROR_FAILURE;
    }

    auto actor = new URLClassifierLocalChild();

    nsTArray<IPCURLClassifierFeature> ipcFeatures;
    for (nsIUrlClassifierFeature* feature : aFeatures) {
      nsAutoCString name;
      rv = feature->GetName(name);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      nsTArray<nsCString> tables;
      rv = feature->GetTables(aListType, tables);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        continue;
      }

      ipcFeatures.AppendElement(
          IPCURLClassifierFeature(std::move(name), std::move(tables)));
    }

    if (!content->SendPURLClassifierLocalConstructor(actor, aURI,
                                                     ipcFeatures)) {
      return NS_ERROR_FAILURE;
    }

    actor->SetFeaturesAndCallback(aFeatures, aCallback);
    return NS_OK;
  }

  using namespace mozilla::Telemetry;
  auto startTime = TimeStamp::Now();  // For telemetry.

  // Let's keep the features alive and release them on the correct thread.
  RefPtr<FeatureHolder> holder =
      FeatureHolder::Create(aURI, aFeatures, aListType);
  if (NS_WARN_IF(!holder)) {
    return NS_ERROR_FAILURE;
  }

  auto worker = mWorker;

  // Since aCallback will be passed around threads...
  nsMainThreadPtrHandle<nsIUrlClassifierFeatureCallback> callback(
      new nsMainThreadPtrHolder<nsIUrlClassifierFeatureCallback>(
          "nsIURIClassifierFeatureCallback", aCallback));

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      "nsUrlClassifierDBService::AsyncClassifyLocalWithFeatures",
      [worker, key, holder, callback, startTime, aIdlePriority]() -> void {
        holder->DoLocalLookup(key, worker);

        nsCOMPtr<nsIRunnable> cbRunnable = NS_NewRunnableFunction(
            "nsUrlClassifierDBService::AsyncClassifyLocalWithFeatures",
            [callback, holder, startTime]() -> void {
              // Measure the time diff between calling and callback.
              glean::urlclassifier::async_classifylocal_time
                  .AccumulateRawDuration(TimeStamp::Now() - startTime);

              nsTArray<RefPtr<nsIUrlClassifierFeatureResult>> results;
              holder->GetResults(results);

              // |callback| is captured as const value so ...
              auto cb =
                  const_cast<nsIUrlClassifierFeatureCallback*>(callback.get());
              cb->OnClassifyComplete(results);
            });
        if (aIdlePriority) {
          NS_DispatchToMainThreadQueue(cbRunnable.forget(),
                                       EventQueuePriority::Idle);
        } else {
          NS_DispatchToMainThread(cbRunnable);
        }
      });

  if (aIdlePriority) {
    return NS_DispatchToThreadQueue(r.forget(), gDbBackgroundThread,
                                    EventQueuePriority::Idle);
  }
  return gDbBackgroundThread->Dispatch(r, NS_DISPATCH_NORMAL);
}

NS_IMETHODIMP
nsUrlClassifierDBService::AsyncClassifyLocalWithFeatureNames(
    nsIURI* aURI, const nsTArray<nsCString>& aFeatureNames,
    nsIUrlClassifierFeature::listType aListType,
    nsIUrlClassifierFeatureCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  if (gShuttingDownThread) {
    return NS_ERROR_ABORT;
  }

  if (!XRE_IsContentProcess()) {
    nsTArray<RefPtr<nsIUrlClassifierFeature>> features;
    for (const nsCString& featureName : aFeatureNames) {
      nsCOMPtr<nsIUrlClassifierFeature> feature =
          mozilla::net::UrlClassifierFeatureFactory::GetFeatureByName(
              featureName);
      if (NS_WARN_IF(!feature)) {
        continue;
      }
      features.AppendElement(feature);
    }
    MOZ_ASSERT(!features.IsEmpty(),
               "At least one URL classifier feature must be present");
    return AsyncClassifyLocalWithFeatures(aURI, features, aListType, aCallback,
                                          true);
  }

  mozilla::dom::ContentChild* content =
      mozilla::dom::ContentChild::GetSingleton();
  if (NS_WARN_IF(!content || content->IsShuttingDown())) {
    return NS_ERROR_FAILURE;
  }

  auto actor = new mozilla::dom::URLClassifierLocalByNameChild();

  if (!content->SendPURLClassifierLocalByNameConstructor(
          actor, aURI, aFeatureNames, aListType)) {
    return NS_ERROR_FAILURE;
  }

  actor->SetFeaturesAndCallback(aFeatureNames, aCallback);
  return NS_OK;
}

bool nsUrlClassifierDBService::AsyncClassifyLocalWithFeaturesUsingPreferences(
    nsIURI* aURI, const nsTArray<RefPtr<nsIUrlClassifierFeature>>& aFeatures,
    nsIUrlClassifierFeature::listType aListType,
    nsIUrlClassifierFeatureCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoCString host;
  nsresult rv = aURI->GetHost(host);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  nsTArray<RefPtr<nsIUrlClassifierFeatureResult>> results;

  // Let's see if we have special entries set by prefs.
  for (nsIUrlClassifierFeature* feature : aFeatures) {
    bool found = false;

    nsAutoCString tableName;
    rv = feature->HasHostInPreferences(host, aListType, tableName, &found);
    NS_ENSURE_SUCCESS(rv, false);

    if (found) {
      MOZ_ASSERT(!tableName.IsEmpty());
      LOG(("URI found in preferences. Table: %s", tableName.get()));

      RefPtr<mozilla::net::UrlClassifierFeatureResult> result =
          new mozilla::net::UrlClassifierFeatureResult(aURI, feature,
                                                       tableName);
      results.AppendElement(result);
    }
  }

  if (results.IsEmpty()) {
    return false;
  }

  // If we have some match using the preferences, we don't need to continue.
  nsCOMPtr<nsIUrlClassifierFeatureCallback> callback(aCallback);
  nsCOMPtr<nsIRunnable> cbRunnable = NS_NewRunnableFunction(
      "nsUrlClassifierDBService::AsyncClassifyLocalWithFeatures",
      [callback, results = std::move(results)]() {
        callback->OnClassifyComplete(results);
      });

  NS_DispatchToMainThread(cbRunnable);
  return true;
}

NS_IMETHODIMP
nsUrlClassifierDBService::GetFeatureByName(const nsACString& aFeatureName,
                                           nsIUrlClassifierFeature** aFeature) {
  NS_ENSURE_ARG_POINTER(aFeature);
  nsCOMPtr<nsIUrlClassifierFeature> feature =
      mozilla::net::UrlClassifierFeatureFactory::GetFeatureByName(aFeatureName);
  if (NS_WARN_IF(!feature)) {
    return NS_ERROR_FAILURE;
  }

  feature.forget(aFeature);
  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBService::GetFeatureNames(nsTArray<nsCString>& aArray) {
  mozilla::net::UrlClassifierFeatureFactory::GetFeatureNames(aArray);
  return NS_OK;
}

NS_IMETHODIMP
nsUrlClassifierDBService::CreateFeatureWithTables(
    const nsACString& aName, const nsTArray<nsCString>& aBlocklistTables,
    const nsTArray<nsCString>& aEntitylistTables,
    nsIUrlClassifierFeature** aFeature) {
  NS_ENSURE_ARG_POINTER(aFeature);
  nsCOMPtr<nsIUrlClassifierFeature> feature =
      mozilla::net::UrlClassifierFeatureFactory::CreateFeatureWithTables(
          aName, aBlocklistTables, aEntitylistTables);
  if (NS_WARN_IF(!feature)) {
    return NS_ERROR_FAILURE;
  }

  feature.forget(aFeature);
  return NS_OK;
}
