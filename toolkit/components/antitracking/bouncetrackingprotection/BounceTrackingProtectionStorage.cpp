/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingProtectionStorage.h"
#include <cstdint>

#include "BounceTrackingStateGlobal.h"
#include "ErrorList.h"
#include "MainThreadUtils.h"
#include "mozIStorageConnection.h"
#include "mozIStorageService.h"
#include "mozIStorageStatement.h"
#include "mozStorageCID.h"
#include "mozilla/Components.h"
#include "mozilla/Mutex.h"
#include "mozilla/Services.h"
#include "nsCOMPtr.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIBounceTrackingProtection.h"
#include "nsIObserverService.h"
#include "nsIPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsStringFwd.h"
#include "nsVariant.h"
#include "nscore.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsCRT.h"

#define BOUNCE_TRACKING_PROTECTION_DB_FILENAME \
  "bounce-tracking-protection.sqlite"_ns
#define SCHEMA_VERSION 1

namespace mozilla {

NS_IMPL_ISUPPORTS(BounceTrackingProtectionStorage, nsIAsyncShutdownBlocker,
                  nsIObserver);

BounceTrackingProtectionStorage::BounceTrackingProtectionStorage()
    : mMutex("mozilla::BounceTrackingProtectionStorage::mMutex"),
      mPendingWrites(0) {};

// static
OriginAttributes BounceTrackingProtectionStorage::NormalizeOriginAttributes(
    const OriginAttributes& aOriginAttributes) {
  OriginAttributes oa = aOriginAttributes;
  oa.StripAttributes(OriginAttributes::STRIP_FIRST_PARTY_DOMAIN |
                     OriginAttributes::STRIP_PARTITION_KEY);
  return oa;
}

RefPtr<BounceTrackingStateGlobal>
BounceTrackingProtectionStorage::GetStateGlobal(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);
  return GetStateGlobal(aPrincipal->OriginAttributesRef());
}

RefPtr<BounceTrackingStateGlobal>
BounceTrackingProtectionStorage::GetStateGlobal(
    const OriginAttributes& aOriginAttributes) {
  return mStateGlobal.Get(NormalizeOriginAttributes(aOriginAttributes));
}

RefPtr<BounceTrackingStateGlobal>
BounceTrackingProtectionStorage::GetOrCreateStateGlobal(
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);
  return GetOrCreateStateGlobal(aPrincipal->OriginAttributesRef());
}

RefPtr<BounceTrackingStateGlobal>
BounceTrackingProtectionStorage::GetOrCreateStateGlobal(
    const OriginAttributes& aOriginAttributes) {
  OriginAttributes oa = NormalizeOriginAttributes(aOriginAttributes);
  return mStateGlobal.GetOrInsertNew(oa, this, oa);
}

nsresult BounceTrackingProtectionStorage::ClearByType(
    BounceTrackingProtectionStorage::EntryType aType) {
  for (auto iter = mStateGlobal.Iter(); !iter.Done(); iter.Next()) {
    BounceTrackingStateGlobal* stateGlobal = iter.Data();
    MOZ_ASSERT(stateGlobal);
    // Update in memory state. Skip storage so we can batch the writes later.
    nsresult rv = stateGlobal->ClearByType(aType, true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Clear on-disk state for all OriginAttributes by type.
  return DeleteDBEntriesByType(nullptr, aType);
}

nsresult BounceTrackingProtectionStorage::ClearBySiteHost(
    const nsACString& aSiteHost, OriginAttributes* aOriginAttributes) {
  NS_ENSURE_TRUE(!aSiteHost.IsEmpty(), NS_ERROR_INVALID_ARG);

  // If OriginAttributes are passed only clear the matching state global.
  if (aOriginAttributes) {
    RefPtr<BounceTrackingStateGlobal> stateGlobal =
        mStateGlobal.Get(*aOriginAttributes);
    if (stateGlobal) {
      nsresult rv = stateGlobal->ClearSiteHost(aSiteHost, true);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  } else {
    // Otherwise we need to clear the host across all state globals.
    for (auto iter = mStateGlobal.Iter(); !iter.Done(); iter.Next()) {
      BounceTrackingStateGlobal* stateGlobal = iter.Data();
      MOZ_ASSERT(stateGlobal);
      // Update in memory state. Skip storage so we can batch the writes later.
      nsresult rv = stateGlobal->ClearSiteHost(aSiteHost, true);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // Update the database.
  // Private browsing data is not written to disk.
  if (aOriginAttributes && aOriginAttributes->IsPrivateBrowsing()) {
    return NS_OK;
  }
  return DeleteDBEntries(aOriginAttributes, aSiteHost);
}

nsresult BounceTrackingProtectionStorage::ClearByTimeRange(PRTime aFrom,
                                                           PRTime aTo) {
  for (auto iter = mStateGlobal.Iter(); !iter.Done(); iter.Next()) {
    BounceTrackingStateGlobal* stateGlobal = iter.Data();
    MOZ_ASSERT(stateGlobal);
    // Update in memory state. Skip storage so we can batch the writes later.
    nsresult rv =
        stateGlobal->ClearByTimeRange(aFrom, Some(aTo), Nothing(), true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Update the database.
  return DeleteDBEntriesInTimeRange(nullptr, aFrom, Some(aTo));
}

nsresult BounceTrackingProtectionStorage::ClearByOriginAttributesPattern(
    const OriginAttributesPattern& aOriginAttributesPattern,
    const Maybe<nsCString>& aSiteHost) {
  // Clear in memory state.
  for (auto iter = mStateGlobal.Iter(); !iter.Done(); iter.Next()) {
    BounceTrackingStateGlobal* stateGlobal = iter.Data();
    MOZ_ASSERT(stateGlobal);

    if (!aOriginAttributesPattern.Matches(stateGlobal->OriginAttributesRef())) {
      continue;
    }

    // If there is no site host filtering we can remove the entire state global.
    if (aSiteHost.isNothing()) {
      iter.Remove();
      continue;
    }

    // Otherwise we need to clear just the entries that match the site host.
    // Update in memory state. Skip storage so we can batch the writes later.
    nsresult rv = stateGlobal->ClearSiteHost(aSiteHost.ref(), true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Update the database.
  // Private browsing data is not written to disk.
  if (aOriginAttributesPattern.mPrivateBrowsingId.WasPassed() &&
      aOriginAttributesPattern.mPrivateBrowsingId.Value() !=
          nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID) {
    return NS_OK;
  }
  return DeleteDBEntriesByOriginAttributesPattern(aOriginAttributesPattern,
                                                  aSiteHost);
}

nsresult BounceTrackingProtectionStorage::UpdateDBEntry(
    const OriginAttributes& aOriginAttributes, const nsACString& aSiteHost,
    EntryType aEntryType, PRTime aTimeStamp) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aSiteHost.IsEmpty());
  MOZ_ASSERT(aTimeStamp);
  MOZ_ASSERT(aOriginAttributes.mPrivateBrowsingId ==
                 nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID,
             "Must not write private browsing data to disk.");

  nsresult rv = CheckDBAvailable();
  NS_ENSURE_SUCCESS(rv, rv);

  if (MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
    nsAutoCString originAttributeSuffix;
    aOriginAttributes.CreateSuffix(originAttributeSuffix);
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: originAttributes: {}, siteHost={}, entryType={}, "
                "timeStamp={}",
                __FUNCTION__, originAttributeSuffix, aSiteHost, aEntryType,
                aTimeStamp);
  }

  NoteSiteWrittenBeforeLoad(aOriginAttributes, aSiteHost);
  mPendingUpdates.AppendElement(PendingUpdate{
      aOriginAttributes, nsCString(aSiteHost), aEntryType, aTimeStamp});

  return MaybeFlushPendingUpdates();
}

nsresult BounceTrackingProtectionStorage::MaybeFlushPendingUpdates() {
  MOZ_ASSERT(NS_IsMainThread());

  size_t pendingUpdatesCount = mPendingUpdates.Length();

  if (pendingUpdatesCount >=
      StaticPrefs::
          privacy_bounceTrackingProtection_storage_maxPendingUpdates()) {
    return FlushPendingUpdates();
  }

  if (StaticPrefs::privacy_bounceTrackingProtection_enableTestMode()) {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->NotifyObservers(
          nullptr, "bounce-tracking-protection-storage-flush-skipped",
          NS_ConvertUTF8toUTF16(nsPrintfCString("%zu", pendingUpdatesCount))
              .get());
    }
  }

  return NS_OK;
}

nsresult BounceTrackingProtectionStorage::FlushPendingUpdates() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mPendingUpdates.IsEmpty()) {
    return NS_OK;
  }
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: Flushing {} pending updates", __FUNCTION__,
              mPendingUpdates.Length());

  IncrementPendingWrites();

  RefPtr<BounceTrackingProtectionStorage> self = this;
  mBackgroundThread->Dispatch(
      NS_NewRunnableFunction(
          "BounceTrackingProtectionStorage::FlushPendingUpdates",
          [self, updatesToFlush = std::move(mPendingUpdates)]() {
            nsresult rv =
                UpsertDataBulk(self->mDatabaseConnection, updatesToFlush);
            self->DecrementPendingWrites();
            NS_ENSURE_SUCCESS_VOID(rv);

            // Let automated tests know that the flush has been completed.
            if (StaticPrefs::
                    privacy_bounceTrackingProtection_enableTestMode()) {
              // Can't use the observer service on the background thread.
              NS_DispatchToMainThread(NS_NewRunnableFunction(
                  "BounceTrackingProtectionStorage::NotifyFlushComplete", []() {
                    nsCOMPtr<nsIObserverService> observerService =
                        mozilla::services::GetObserverService();
                    if (observerService) {
                      observerService->NotifyObservers(
                          nullptr, "bounce-tracking-protection-storage-flushed",
                          nullptr);
                    }
                  }));
            }
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  return NS_OK;
}

// static
nsresult BounceTrackingProtectionStorage::UpsertDataBulk(
    mozIStorageConnection* aDatabaseConnection,
    const nsTArray<PendingUpdate>& aUpdates) {
  MOZ_ASSERT(!NS_IsMainThread());
  NS_ENSURE_TRUE(aDatabaseConnection, NS_ERROR_NOT_AVAILABLE);
  MOZ_ASSERT(!aUpdates.IsEmpty());

  // Start a transaction for better performance
  nsresult rv = aDatabaseConnection->BeginTransaction();
  NS_ENSURE_SUCCESS(rv, rv);

  // Use a lambda to handle the transaction operations. This means we can easily
  // rollback the transaction if any error occurs.
  nsresult result = [&]() {
    static const nsLiteralCString upsertQuery(
        "INSERT INTO sites (originAttributeSuffix, siteHost, entryType, "
        "timeStamp)"
        "VALUES (:originAttributeSuffix, :siteHost, :entryType, :timeStamp)"
        "ON CONFLICT (originAttributeSuffix, siteHost)"
        "DO UPDATE SET entryType = :entryType, timeStamp = :timeStamp;");

    nsCOMPtr<mozIStorageStatement> upsertStmt;
    nsresult rv = aDatabaseConnection->CreateStatement(
        upsertQuery, getter_AddRefs(upsertStmt));
    NS_ENSURE_SUCCESS(rv, rv);

    for (const auto& update : aUpdates) {
      MOZ_ASSERT(update.mOriginAttributes.mPrivateBrowsingId ==
                     nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID,
                 "Must not write private browsing data to disk.");

      // Serialize OriginAttributes
      nsAutoCString originAttributeSuffix;
      update.mOriginAttributes.CreateSuffix(originAttributeSuffix);

      rv = upsertStmt->BindUTF8StringByName("originAttributeSuffix"_ns,
                                            originAttributeSuffix);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = upsertStmt->BindUTF8StringByName("siteHost"_ns, update.mSiteHost);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = upsertStmt->BindInt32ByName("entryType"_ns,
                                       static_cast<int32_t>(update.mEntryType));
      NS_ENSURE_SUCCESS(rv, rv);

      rv = upsertStmt->BindInt64ByName("timeStamp"_ns, update.mTimeStamp);
      NS_ENSURE_SUCCESS(rv, rv);

      rv = upsertStmt->Execute();
      NS_ENSURE_SUCCESS(rv, rv);

      rv = upsertStmt->Reset();
      NS_ENSURE_SUCCESS(rv, rv);
    }

    return aDatabaseConnection->CommitTransaction();
  }();

  // If at any point during building and executing the query we encounter an
  // error, rollback the transaction.
  if (NS_FAILED(result)) {
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(aDatabaseConnection->RollbackTransaction()),
        "Failed to rollback transaction");
  }

  return result;
}

nsresult BounceTrackingProtectionStorage::DeleteDBEntries(
    OriginAttributes* aOriginAttributes, const nsACString& aSiteHost) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aSiteHost.IsEmpty());
  MOZ_ASSERT(!aOriginAttributes ||
                 aOriginAttributes->mPrivateBrowsingId ==
                     nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID,
             "Must not write private browsing data to the table.");

  nsresult rv = CheckDBAvailable();
  NS_ENSURE_SUCCESS(rv, rv);

  // Flush pending updates to disk before deleting data since pending updates
  // may be affected by the deletion.
  rv = FlushPendingUpdates();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "DeleteData: FlushPendingUpdates failed");

  if (MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
    nsAutoCString originAttributeSuffix("*");
    if (aOriginAttributes) {
      aOriginAttributes->CreateSuffix(originAttributeSuffix);
    }
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: originAttributes: {}, siteHost={}", __FUNCTION__,
                originAttributeSuffix, aSiteHost);
  }

  RefPtr<BounceTrackingProtectionStorage> self = this;
  nsCString siteHost(aSiteHost);
  Maybe<OriginAttributes> originAttributes;
  if (aOriginAttributes) {
    originAttributes.emplace(*aOriginAttributes);
  }
  NoteDeletionBeforeLoad(
      [originAttributes, siteHost](const ImportEntry& aEntry) {
        return (originAttributes.isNothing() ||
                aEntry.mOriginAttributes == *originAttributes) &&
               aEntry.mSiteHost == siteHost;
      });

  IncrementPendingWrites();
  mBackgroundThread->Dispatch(
      NS_NewRunnableFunction(
          "BounceTrackingProtectionStorage::DeleteEntry",
          [self, originAttributes = std::move(originAttributes),
           siteHost = std::move(siteHost)]() {
            nsresult rv = DeleteData(self->mDatabaseConnection,
                                     originAttributes, siteHost);
            self->DecrementPendingWrites();
            NS_ENSURE_SUCCESS_VOID(rv);
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  return NS_OK;
}

nsresult BounceTrackingProtectionStorage::Clear() {
  MOZ_ASSERT(NS_IsMainThread());
  // Clear in memory data.
  mStateGlobal.Clear();

  // Clear pending updates. Since we're clearing all data we must not preserve
  // pending writes.
  mPendingUpdates.Clear();

  // Clear on disk data.
  nsresult rv = CheckDBAvailable();
  NS_ENSURE_SUCCESS(rv, rv);

  NoteDeletionBeforeLoad([](const ImportEntry&) { return true; });

  IncrementPendingWrites();
  RefPtr<BounceTrackingProtectionStorage> self = this;
  mBackgroundThread->Dispatch(
      NS_NewRunnableFunction("BounceTrackingProtectionStorage::Clear",
                             [self]() {
                               nsresult rv =
                                   ClearData(self->mDatabaseConnection);
                               self->DecrementPendingWrites();
                               NS_ENSURE_SUCCESS_VOID(rv);
                             }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
  return NS_OK;
}

RefPtr<GenericPromise> BounceTrackingProtectionStorage::TestFlush() {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = CheckDBAvailable();
  if (NS_FAILED(rv)) {
    return GenericPromise::CreateAndReject(rv, __func__);
  }

  rv = FlushPendingUpdates();
  if (NS_FAILED(rv)) {
    return GenericPromise::CreateAndReject(rv, __func__);
  }

  return InvokeAsync(mBackgroundThread, __func__, []() {
    return GenericPromise::CreateAndResolve(true, __func__);
  });
}

nsresult BounceTrackingProtectionStorage::DeleteDBEntriesInTimeRange(
    OriginAttributes* aOriginAttributes, PRTime aFrom, Maybe<PRTime> aTo,
    Maybe<BounceTrackingProtectionStorage::EntryType> aEntryType) {
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_ARG_MIN(aFrom, 0);
  NS_ENSURE_TRUE(aTo.isNothing() || aTo.value() > aFrom, NS_ERROR_INVALID_ARG);

  nsresult rv = CheckDBAvailable();
  NS_ENSURE_SUCCESS(rv, rv);

  // Flush pending updates to disk before deleting data since pending updates
  // may be affected by the deletion.
  rv = FlushPendingUpdates();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "DeleteDataInTimeRange: FlushPendingUpdates failed");

  RefPtr<BounceTrackingProtectionStorage> self = this;

  Maybe<OriginAttributes> originAttributes;
  if (aOriginAttributes) {
    originAttributes.emplace(*aOriginAttributes);
  }

  NoteDeletionBeforeLoad(
      [originAttributes, aFrom, aTo, aEntryType](const ImportEntry& aEntry) {
        return (originAttributes.isNothing() ||
                aEntry.mOriginAttributes == *originAttributes) &&
               aEntry.mTimeStamp >= aFrom &&
               (aTo.isNothing() || aEntry.mTimeStamp <= *aTo) &&
               (aEntryType.isNothing() || aEntry.mEntryType == *aEntryType);
      });

  IncrementPendingWrites();
  mBackgroundThread->Dispatch(
      NS_NewRunnableFunction(
          "BounceTrackingProtectionStorage::DeleteDBEntriesInTimeRange",
          [self, originAttributes = std::move(originAttributes), aFrom, aTo,
           aEntryType]() {
            nsresult rv =
                DeleteDataInTimeRange(self->mDatabaseConnection,
                                      originAttributes, aFrom, aTo, aEntryType);
            self->DecrementPendingWrites();
            NS_ENSURE_SUCCESS_VOID(rv);
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
  return NS_OK;
}

nsresult BounceTrackingProtectionStorage::DeleteDBEntriesByType(
    OriginAttributes* aOriginAttributes,
    BounceTrackingProtectionStorage::EntryType aEntryType) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = CheckDBAvailable();
  NS_ENSURE_SUCCESS(rv, rv);

  // Flush pending updates to disk before deleting data since pending updates
  // may be affected by the deletion.
  rv = FlushPendingUpdates();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "DeleteDataInTimeRange: FlushPendingUpdates failed");

  RefPtr<BounceTrackingProtectionStorage> self = this;
  Maybe<OriginAttributes> originAttributes;
  if (aOriginAttributes) {
    originAttributes.emplace(*aOriginAttributes);
  }

  NoteDeletionBeforeLoad(
      [originAttributes, aEntryType](const ImportEntry& aEntry) {
        return (originAttributes.isNothing() ||
                aEntry.mOriginAttributes == *originAttributes) &&
               aEntry.mEntryType == aEntryType;
      });

  IncrementPendingWrites();
  mBackgroundThread->Dispatch(
      NS_NewRunnableFunction(
          "BounceTrackingProtectionStorage::DeleteDBEntriesByType",
          [self, originAttributes = std::move(originAttributes), aEntryType]() {
            nsresult rv = self->DeleteDataByType(self->mDatabaseConnection,
                                                 originAttributes, aEntryType);
            self->DecrementPendingWrites();
            NS_ENSURE_SUCCESS_VOID(rv);
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
  return NS_OK;
}

nsresult
BounceTrackingProtectionStorage::DeleteDBEntriesByOriginAttributesPattern(
    const OriginAttributesPattern& aOriginAttributesPattern,
    const Maybe<nsCString>& aSiteHost) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!aOriginAttributesPattern.mPrivateBrowsingId.WasPassed() ||
                 aOriginAttributesPattern.mPrivateBrowsingId.Value() ==
                     nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID,
             "Must not clear private browsing data from the table.");
  if (aSiteHost.isSome()) {
    MOZ_ASSERT(!aSiteHost->IsEmpty(), "aSiteHost must not be empty.");
  }

  nsresult rv = CheckDBAvailable();
  NS_ENSURE_SUCCESS(rv, rv);

  // Flush pending updates to disk before deleting data since pending updates
  // may be affected by the deletion.
  rv = FlushPendingUpdates();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "DeleteDataInTimeRange: FlushPendingUpdates failed");

  NoteDeletionBeforeLoad(
      [aOriginAttributesPattern, aSiteHost](const ImportEntry& aEntry) {
        return aOriginAttributesPattern.Matches(aEntry.mOriginAttributes) &&
               (aSiteHost.isNothing() || aEntry.mSiteHost == *aSiteHost);
      });

  IncrementPendingWrites();
  RefPtr<BounceTrackingProtectionStorage> self = this;

  mBackgroundThread->Dispatch(
      NS_NewRunnableFunction(
          "BounceTrackingProtectionStorage::"
          "DeleteEntriesByOriginAttributesPattern",
          [self, aOriginAttributesPattern, siteHost = aSiteHost]() {
            nsresult rv = DeleteDataByOriginAttributesPattern(
                self->mDatabaseConnection, aOriginAttributesPattern, siteHost);
            self->DecrementPendingWrites();
            NS_ENSURE_SUCCESS_VOID(rv);
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
  return NS_OK;
}

// nsIAsyncShutdownBlocker

NS_IMETHODIMP BounceTrackingProtectionStorage::BlockShutdown(
    nsIAsyncShutdownClient* aClient) {
  MOZ_ASSERT(NS_IsMainThread());
  DebugOnly<nsresult> rv = CheckDBAvailable();
  // If init failed log a warning. This isn't an early return since we still
  // need to try to tear down, including removing the shutdown blocker. A
  // failure state shouldn't cause a shutdown hang.
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "BlockShutdown: Init failed");

  // Flush pending updates to disk before shutting down.
  // This will increment mPendingWrites which we will wait for below.
  rv = FlushPendingUpdates();
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "BlockShutdown: FlushPendingUpdates failed");

  MutexAutoLock lock(mMutex);
  mShuttingDown.Flip();

  RefPtr<BounceTrackingProtectionStorage> self = this;
  mBackgroundThread->Dispatch(
      NS_NewRunnableFunction(
          "BounceTrackingProtectionStorage::BlockShutdown",
          [self]() {
            MutexAutoLock lock(self->mMutex);

            MOZ_ASSERT(self->mPendingWrites == 0);

            if (self->mDatabaseConnection) {
              DebugOnly<nsresult> rv = self->mDatabaseConnection->Close();
              NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                   "Failed to close database connection");
              self->mDatabaseConnection = nullptr;
            }

            nsresult rv = NS_DispatchToMainThread(NS_NewRunnableFunction(
                "BounceTrackingProtectionStorage::BlockShutdown "
                "- mainthread callback",
                [self]() { self->Finalize(); }));
            NS_ENSURE_SUCCESS_VOID(rv);
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  return NS_OK;
}

nsresult BounceTrackingProtectionStorage::CheckDBAvailable() {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  if (mErrored) {
    return NS_ERROR_FAILURE;
  }
  if (mShuttingDown) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return NS_OK;
}

void BounceTrackingProtectionStorage::Finalize() {
  nsCOMPtr<nsIAsyncShutdownClient> asc = GetAsyncShutdownBarrier();
  MOZ_ASSERT(asc);
  DebugOnly<nsresult> rv = asc->RemoveBlocker(this);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
}

// nsIObserver

NS_IMETHODIMP
BounceTrackingProtectionStorage::Observe(nsISupports* aSubject,
                                         const char* aTopic,
                                         const char16_t* aData) {
  AssertIsOnMainThread();
  if (nsCRT::strcmp(aTopic, "last-pb-context-exited") != 0) {
    return nsresult::NS_ERROR_FAILURE;
  }

  uint32_t removedCount = 0;
  // Clear in-memory private browsing entries.
  for (auto iter = mStateGlobal.Iter(); !iter.Done(); iter.Next()) {
    BounceTrackingStateGlobal* stateGlobal = iter.Data();
    MOZ_ASSERT(stateGlobal);
    if (stateGlobal->IsPrivateBrowsing()) {
      iter.Remove();
      removedCount++;
    }
  }
  MOZ_LOG_FMT(
      gBounceTrackingProtectionLog, LogLevel::Debug,
      "{}: last-pb-context-exited: Removed {} private browsing state globals",
      __FUNCTION__, removedCount);

  return NS_OK;
}

// nsIAsyncShutdownBlocker

already_AddRefed<nsIAsyncShutdownClient>
BounceTrackingProtectionStorage::GetAsyncShutdownBarrier() const {
  nsCOMPtr<nsIAsyncShutdownService> svc = components::AsyncShutdown::Service();
  MOZ_RELEASE_ASSERT(svc);

  nsCOMPtr<nsIAsyncShutdownClient> client;
  nsresult rv = svc->GetProfileBeforeChange(getter_AddRefs(client));
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  MOZ_RELEASE_ASSERT(client);

  return client.forget();
}

NS_IMETHODIMP BounceTrackingProtectionStorage::GetState(nsIPropertyBag**) {
  return NS_OK;
}

NS_IMETHODIMP BounceTrackingProtectionStorage::GetName(nsAString& aName) {
  aName.AssignLiteral("BounceTrackingProtectionStorage: Flushing to disk");
  return NS_OK;
}

nsresult BounceTrackingProtectionStorage::Init() {
  nsresult rv = InitInternal();
  if (NS_FAILED(rv)) {
    MutexAutoLock lock(mMutex);
    mErrored.Flip();
  }
  return rv;
}

nsresult BounceTrackingProtectionStorage::InitInternal() {
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}",
              __FUNCTION__);

  // Init shouldn't be called if the feature is disabled.
  NS_ENSURE_TRUE(StaticPrefs::privacy_bounceTrackingProtection_mode() !=
                     nsIBounceTrackingProtection::MODE_DISABLED,
                 NS_ERROR_FAILURE);

  // Register a shutdown blocker so we can flush pending changes to disk before
  // shutdown.
  // Init may also be called during shutdown, e.g. because of clearing data
  // during shutdown.
  nsCOMPtr<nsIAsyncShutdownClient> shutdownBarrier = GetAsyncShutdownBarrier();
  NS_ENSURE_TRUE(shutdownBarrier, NS_ERROR_FAILURE);

  bool closed;
  nsresult rv = shutdownBarrier->GetIsClosed(&closed);
  NS_ENSURE_TRUE(!closed, NS_ERROR_ILLEGAL_DURING_SHUTDOWN);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = shutdownBarrier->AddBlocker(
      this, NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__, u""_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  // Listen for last private browsing context  exited message so we can clean up
  // in memory state when the PBM session ends.
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  NS_ENSURE_TRUE(observerService, NS_ERROR_FAILURE);
  rv = observerService->AddObserver(this, "last-pb-context-exited", false);
  NS_ENSURE_SUCCESS(rv, rv);

  // Create the database file.
  rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                              getter_AddRefs(mDatabaseFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mDatabaseFile->AppendNative(BOUNCE_TRACKING_PROTECTION_DB_FILENAME);
  NS_ENSURE_SUCCESS(rv, rv);

  // Init the database and import data.
  NS_ENSURE_SUCCESS(
      NS_CreateBackgroundTaskQueue("BounceTrackingProtectionStorage",
                                   getter_AddRefs(mBackgroundThread)),
      NS_ERROR_FAILURE);

  RefPtr<BounceTrackingProtectionStorage> self = this;

  // Database operations dispatched to mBackgroundThread after this run once
  // initialization has finished. They are skipped if it failed.
  return mBackgroundThread->Dispatch(
      NS_NewRunnableFunction("BounceTrackingProtectionStorage::Init",
                             [self]() {
                               nsresult rv = self->CreateDatabaseConnection();
                               if (NS_SUCCEEDED(rv)) {
                                 rv = self->LoadMemoryStateFromDisk();
                               }
                               if (NS_WARN_IF(NS_FAILED(rv))) {
                                 if (self->mDatabaseConnection) {
                                   (void)self->mDatabaseConnection->Close();
                                   self->mDatabaseConnection = nullptr;
                                 }
                                 MutexAutoLock lock(self->mMutex);
                                 self->mErrored.Flip();
                               }
                             }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
}

nsresult BounceTrackingProtectionStorage::CreateDatabaseConnection(
    bool aShouldRetry) {
  MOZ_ASSERT(!NS_IsMainThread());
  NS_ENSURE_TRUE(mDatabaseFile, NS_ERROR_NULL_POINTER);

  nsCOMPtr<mozIStorageService> storage =
      do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
  NS_ENSURE_TRUE(storage, NS_ERROR_UNEXPECTED);

  nsresult rv = storage->OpenDatabase(mDatabaseFile,
                                      mozIStorageService::CONNECTION_DEFAULT,
                                      getter_AddRefs(mDatabaseConnection));
  if (rv == NS_ERROR_FILE_CORRUPTED && aShouldRetry) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: Database file is corrupted, removing it and retrying",
                __FUNCTION__);

    rv = mDatabaseFile->Remove(false);
    NS_ENSURE_SUCCESS(rv, rv);
    return CreateDatabaseConnection(false);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_TRUE(mDatabaseConnection, NS_ERROR_UNEXPECTED);
  bool ready = false;
  mDatabaseConnection->GetConnectionReady(&ready);
  // If it fails once remove the db file and try again.
  if (!ready && aShouldRetry) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: Database connection failed (not ready after open), "
                "removing it and retrying",
                __FUNCTION__);

    rv = mDatabaseFile->Remove(false);
    NS_ENSURE_SUCCESS(rv, rv);
    return CreateDatabaseConnection(false);
  }
  NS_ENSURE_TRUE(ready, NS_ERROR_UNEXPECTED);

  return EnsureTable();
}

nsresult BounceTrackingProtectionStorage::EnsureTable() {
  MOZ_ASSERT(!NS_IsMainThread());
  NS_ENSURE_TRUE(mDatabaseConnection, NS_ERROR_UNEXPECTED);

  nsresult rv = mDatabaseConnection->SetSchemaVersion(SCHEMA_VERSION);
  NS_ENSURE_SUCCESS(rv, rv);

  const constexpr auto createTableQuery =
      "CREATE TABLE IF NOT EXISTS sites ("
      "originAttributeSuffix TEXT NOT NULL,"
      "siteHost TEXT NOT NULL, "
      "entryType INTEGER NOT NULL, "
      "timeStamp INTEGER NOT NULL, "
      "PRIMARY KEY (originAttributeSuffix, siteHost)"
      ");"_ns;

  return mDatabaseConnection->ExecuteSimpleSQL(createTableQuery);
}

nsresult BounceTrackingProtectionStorage::LoadMemoryStateFromDisk() {
  MOZ_ASSERT(!NS_IsMainThread(),
             "Must not load the table from disk in the main thread.");

  const constexpr auto selectAllQuery(
      "SELECT originAttributeSuffix, siteHost, entryType, timeStamp FROM sites;"_ns);

  nsCOMPtr<mozIStorageStatement> readStmt;
  nsresult rv = mDatabaseConnection->CreateStatement(selectAllQuery,
                                                     getter_AddRefs(readStmt));
  NS_ENSURE_SUCCESS(rv, rv);

  bool hasResult;
  // Collect DB entries into an array to hand to the main thread later.
  nsTArray<ImportEntry> importEntries;
  while (NS_SUCCEEDED(readStmt->ExecuteStep(&hasResult)) && hasResult) {
    nsAutoCString originAttributeSuffix, siteHost;
    int64_t timeStamp;
    int32_t typeInt;

    rv = readStmt->GetUTF8String(0, originAttributeSuffix);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = readStmt->GetUTF8String(1, siteHost);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = readStmt->GetInt32(2, &typeInt);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = readStmt->GetInt64(3, &timeStamp);
    NS_ENSURE_SUCCESS(rv, rv);

    // Convert entryType field to enum.
    BounceTrackingProtectionStorage::EntryType entryType =
        static_cast<BounceTrackingProtectionStorage::EntryType>(typeInt);
    // Check that the enum value is valid.
    if (NS_WARN_IF(
            entryType !=
                BounceTrackingProtectionStorage::EntryType::BounceTracker &&
            entryType !=
                BounceTrackingProtectionStorage::EntryType::UserActivation)) {
      continue;
    }

    OriginAttributes oa;
    bool success = oa.PopulateFromSuffix(originAttributeSuffix);
    if (NS_WARN_IF(!success)) {
      continue;
    }

    // Collect entries to dispatch to main thread later.
    importEntries.AppendElement(
        ImportEntry{std::move(oa), std::move(siteHost), entryType, timeStamp});
  }

  // We can only access the state map on the main thread.
  RefPtr<BounceTrackingProtectionStorage> self = this;
  return NS_DispatchToMainThread(NS_NewRunnableFunction(
      "BounceTrackingProtectionStorage::LoadMemoryStateFromDisk",
      [self, importEntries = std::move(importEntries)]() {
        self->mMemoryStateLoaded = true;

        // For each entry get or create BounceTrackingStateGlobal and insert it
        // into global state map.
        for (const ImportEntry& entry : importEntries) {
          if (self->IsDeletedBeforeLoad(entry)) {
            continue;
          }

          RefPtr<BounceTrackingStateGlobal> stateGlobal =
              self->GetOrCreateStateGlobal(entry.mOriginAttributes);
          MOZ_ASSERT(stateGlobal);

          nsresult rv;
          if (entry.mEntryType ==
              BounceTrackingProtectionStorage::EntryType::BounceTracker) {
            rv = stateGlobal->RecordBounceTracker(entry.mSiteHost,
                                                  entry.mTimeStamp, true);
          } else {
            rv = stateGlobal->RecordUserActivation(entry.mSiteHost,
                                                   entry.mTimeStamp, true);
          }
          if (NS_WARN_IF(NS_FAILED(rv)) &&
              MOZ_LOG_TEST(gBounceTrackingProtectionLog, LogLevel::Debug)) {
            nsAutoCString originAttributeSuffix;
            entry.mOriginAttributes.CreateSuffix(originAttributeSuffix);

            MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                        "{}: Failed to load entry from disk: "
                        "originAttributeSuffix={}, siteHost={}, entryType={}, "
                        "timeStamp={}",
                        __FUNCTION__, originAttributeSuffix, entry.mSiteHost,
                        entry.mEntryType, entry.mTimeStamp);
          }

          if (self->IsSiteWrittenBeforeLoad(entry)) {
            rv = self->PersistMemoryState(stateGlobal, entry.mSiteHost);
            NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                                 "LoadMemoryStateFromDisk: PersistMemoryState "
                                 "failed");
          }
        }

        self->mSitesWrittenBeforeLoad.Clear();
        self->mDeletionsBeforeLoad.Clear();
      }));
}

nsresult BounceTrackingProtectionStorage::PersistMemoryState(
    BounceTrackingStateGlobal* aStateGlobal, const nsACString& aSiteHost) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aStateGlobal);

  if (Maybe<PRTime> activationTime =
          aStateGlobal->UserActivationMapRef().MaybeGet(aSiteHost)) {
    return UpdateDBEntry(aStateGlobal->OriginAttributesRef(), aSiteHost,
                         EntryType::UserActivation, *activationTime);
  }
  if (Maybe<BounceTrackerCandidate> candidate =
          aStateGlobal->BounceTrackersMapRef().MaybeGet(aSiteHost)) {
    return UpdateDBEntry(aStateGlobal->OriginAttributesRef(), aSiteHost,
                         EntryType::BounceTracker, candidate->mBounceTime);
  }
  return NS_OK;
}

void BounceTrackingProtectionStorage::NoteSiteWrittenBeforeLoad(
    const OriginAttributes& aOriginAttributes, const nsACString& aSiteHost) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mMemoryStateLoaded) {
    mSitesWrittenBeforeLoad.LookupOrInsert(aOriginAttributes).Insert(aSiteHost);
  }
}

void BounceTrackingProtectionStorage::NoteDeletionBeforeLoad(
    std::function<bool(const ImportEntry&)>&& aMatches) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mMemoryStateLoaded) {
    mDeletionsBeforeLoad.AppendElement(std::move(aMatches));
  }
}

bool BounceTrackingProtectionStorage::IsDeletedBeforeLoad(
    const ImportEntry& aEntry) const {
  MOZ_ASSERT(NS_IsMainThread());
  for (const auto& matches : mDeletionsBeforeLoad) {
    if (matches(aEntry)) {
      return true;
    }
  }
  return false;
}

bool BounceTrackingProtectionStorage::IsSiteWrittenBeforeLoad(
    const ImportEntry& aEntry) {
  MOZ_ASSERT(NS_IsMainThread());
  auto sites = mSitesWrittenBeforeLoad.Lookup(aEntry.mOriginAttributes);
  return sites && sites->Contains(aEntry.mSiteHost);
}

void BounceTrackingProtectionStorage::IncrementPendingWrites() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mPendingWrites < std::numeric_limits<uint32_t>::max());
  mPendingWrites++;
}

void BounceTrackingProtectionStorage::DecrementPendingWrites() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mPendingWrites > 0);
  mPendingWrites--;
}

// static
nsresult BounceTrackingProtectionStorage::DeleteData(
    mozIStorageConnection* aDatabaseConnection,
    Maybe<OriginAttributes> aOriginAttributes, const nsACString& aSiteHost) {
  MOZ_ASSERT(!NS_IsMainThread(),
             "Must not write to the table from the main thread.");
  NS_ENSURE_TRUE(aDatabaseConnection, NS_ERROR_NOT_AVAILABLE);
  MOZ_ASSERT(!aSiteHost.IsEmpty());
  MOZ_ASSERT(aOriginAttributes.isNothing() ||
             aOriginAttributes->mPrivateBrowsingId ==
                 nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID);

  nsAutoCString deleteQuery("DELETE FROM sites WHERE siteHost = :siteHost");

  if (aOriginAttributes) {
    deleteQuery.AppendLiteral(
        " AND originAttributeSuffix = :originAttributeSuffix");
  }

  nsCOMPtr<mozIStorageStatement> upsertStmt;
  nsresult rv = aDatabaseConnection->CreateStatement(
      deleteQuery, getter_AddRefs(upsertStmt));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = upsertStmt->BindUTF8StringByName("siteHost"_ns, aSiteHost);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aOriginAttributes) {
    nsAutoCString originAttributeSuffix;
    aOriginAttributes->CreateSuffix(originAttributeSuffix);
    rv = upsertStmt->BindUTF8StringByName("originAttributeSuffix"_ns,
                                          originAttributeSuffix);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return upsertStmt->Execute();
}

// static
nsresult BounceTrackingProtectionStorage::DeleteDataInTimeRange(
    mozIStorageConnection* aDatabaseConnection,
    Maybe<OriginAttributes> aOriginAttributes, PRTime aFrom, Maybe<PRTime> aTo,
    Maybe<BounceTrackingProtectionStorage::EntryType> aEntryType) {
  MOZ_ASSERT(!NS_IsMainThread(),
             "Must not write to the table from the main thread.");
  NS_ENSURE_TRUE(aDatabaseConnection, NS_ERROR_NOT_AVAILABLE);
  MOZ_ASSERT(aOriginAttributes.isNothing() ||
             aOriginAttributes->mPrivateBrowsingId ==
                 nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID);
  MOZ_ASSERT(aFrom >= 0);
  MOZ_ASSERT(aTo.isNothing() || aTo.value() > aFrom);

  nsAutoCString deleteQuery(
      "DELETE FROM sites "
      "WHERE timeStamp >= :aFrom"_ns);

  if (aTo.isSome()) {
    deleteQuery.AppendLiteral(" AND timeStamp <= :aTo");
  }

  if (aOriginAttributes) {
    deleteQuery.AppendLiteral(
        " AND originAttributeSuffix = :originAttributeSuffix");
  }

  if (aEntryType.isSome()) {
    deleteQuery.AppendLiteral(" AND entryType = :entryType");
  }
  deleteQuery.AppendLiteral(";");

  nsCOMPtr<mozIStorageStatement> deleteStmt;
  nsresult rv = aDatabaseConnection->CreateStatement(
      deleteQuery, getter_AddRefs(deleteStmt));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteStmt->BindInt64ByName("aFrom"_ns, aFrom);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aTo.isSome()) {
    rv = deleteStmt->BindInt64ByName("aTo"_ns, aTo.value());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aOriginAttributes) {
    nsAutoCString originAttributeSuffix;
    aOriginAttributes->CreateSuffix(originAttributeSuffix);
    rv = deleteStmt->BindUTF8StringByName("originAttributeSuffix"_ns,
                                          originAttributeSuffix);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (aEntryType.isSome()) {
    rv = deleteStmt->BindInt32ByName("entryType"_ns,
                                     static_cast<int32_t>(*aEntryType));
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return deleteStmt->Execute();
}

// static
nsresult BounceTrackingProtectionStorage::DeleteDataByType(
    mozIStorageConnection* aDatabaseConnection,
    const Maybe<OriginAttributes>& aOriginAttributes,
    BounceTrackingProtectionStorage::EntryType aEntryType) {
  MOZ_ASSERT(!NS_IsMainThread(),
             "Must not write to the table from the main thread.");
  NS_ENSURE_TRUE(aDatabaseConnection, NS_ERROR_NOT_AVAILABLE);
  MOZ_ASSERT(aOriginAttributes.isNothing() ||
             aOriginAttributes->mPrivateBrowsingId ==
                 nsIScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID);

  nsAutoCString deleteQuery(
      "DELETE FROM sites "
      "WHERE entryType = :entryType"_ns);

  if (aOriginAttributes) {
    deleteQuery.AppendLiteral(
        " AND originAttributeSuffix = :originAttributeSuffix");
  }

  deleteQuery.AppendLiteral(";");

  nsCOMPtr<mozIStorageStatement> deleteStmt;
  nsresult rv = aDatabaseConnection->CreateStatement(
      deleteQuery, getter_AddRefs(deleteStmt));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = deleteStmt->BindInt32ByName("entryType"_ns,
                                   static_cast<int32_t>(aEntryType));
  NS_ENSURE_SUCCESS(rv, rv);

  if (aOriginAttributes) {
    nsAutoCString originAttributeSuffix;
    aOriginAttributes->CreateSuffix(originAttributeSuffix);
    rv = deleteStmt->BindUTF8StringByName("originAttributeSuffix"_ns,
                                          originAttributeSuffix);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return deleteStmt->Execute();
}

nsresult BounceTrackingProtectionStorage::DeleteDataByOriginAttributesPattern(
    mozIStorageConnection* aDatabaseConnection,
    const OriginAttributesPattern& aOriginAttributesPattern,
    const Maybe<nsCString>& aSiteHost) {
  MOZ_ASSERT(!NS_IsMainThread(),
             "Must not write to the table from the main thread.");
  NS_ENSURE_TRUE(aDatabaseConnection, NS_ERROR_NOT_AVAILABLE);
  if (aSiteHost.isSome()) {
    MOZ_ASSERT(!aSiteHost->IsEmpty(), "aSiteHost must not be empty.");
  }

  nsAutoCString deleteQuery(
      "DELETE FROM sites WHERE "
      "ORIGIN_ATTRS_PATTERN_MATCH_OA_SUFFIX(originAttributeSuffix)");

  // Filtering by site host is optional.
  if (aSiteHost.isSome()) {
    deleteQuery.AppendLiteral(" AND siteHost = :siteHost");
  }

  deleteQuery.AppendLiteral(";");

  nsCOMPtr<mozIStorageFunction> patternMatchFunction(
      new OriginAttrsPatternMatchOASuffixSQLFunction(aOriginAttributesPattern));

  nsresult rv = aDatabaseConnection->CreateFunction(
      "ORIGIN_ATTRS_PATTERN_MATCH_OA_SUFFIX"_ns, 1, patternMatchFunction);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIStorageStatement> deleteStmt;
  rv = aDatabaseConnection->CreateStatement(deleteQuery,
                                            getter_AddRefs(deleteStmt));
  NS_ENSURE_SUCCESS(rv, rv);

  if (aSiteHost.isSome()) {
    rv = deleteStmt->BindUTF8StringByName("siteHost"_ns, aSiteHost.ref());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  rv = deleteStmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);

  return aDatabaseConnection->RemoveFunction(
      "ORIGIN_ATTRS_PATTERN_MATCH_OA_SUFFIX"_ns);
}

// static
nsresult BounceTrackingProtectionStorage::ClearData(
    mozIStorageConnection* aDatabaseConnection) {
  MOZ_ASSERT(!NS_IsMainThread(),
             "Must not write to the table from the main thread.");
  NS_ENSURE_ARG_POINTER(aDatabaseConnection);
  return aDatabaseConnection->ExecuteSimpleSQL("DELETE FROM sites;"_ns);
}

NS_IMPL_ISUPPORTS(OriginAttrsPatternMatchOASuffixSQLFunction,
                  mozIStorageFunction)

NS_IMETHODIMP
OriginAttrsPatternMatchOASuffixSQLFunction::OnFunctionCall(
    mozIStorageValueArray* aFunctionArguments, nsIVariant** aResult) {
  nsresult rv;

  nsAutoCString originAttributeSuffix;
  rv = aFunctionArguments->GetUTF8String(0, originAttributeSuffix);
  NS_ENSURE_SUCCESS(rv, rv);

  OriginAttributes originAttributes;
  bool parsedSuccessfully =
      originAttributes.PopulateFromSuffix(originAttributeSuffix);
  NS_ENSURE_TRUE(parsedSuccessfully, NS_ERROR_FAILURE);

  bool result = mPattern.Matches(originAttributes);

  RefPtr<nsVariant> outVar(new nsVariant());
  rv = outVar->SetAsBool(result);
  NS_ENSURE_SUCCESS(rv, rv);

  outVar.forget(aResult);
  return NS_OK;
}

}  // namespace mozilla
