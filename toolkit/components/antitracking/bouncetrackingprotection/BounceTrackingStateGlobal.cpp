/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingStateGlobal.h"
#include "BounceTrackingProtectionStorage.h"
#include "ErrorList.h"
#include "mozilla/Assertions.h"
#include "mozilla/Logging.h"
#include "nsIBounceTrackingProtection.h"
#include "nsIPrincipal.h"

namespace mozilla {

NS_IMPL_CYCLE_COLLECTION(BounceTrackingStateGlobal);

extern LazyLogModule gBounceTrackingProtectionLog;

BounceTrackingStateGlobal::BounceTrackingStateGlobal(
    BounceTrackingProtectionStorage* aStorage, const OriginAttributes& aAttrs)
    : mStorage(aStorage), mOriginAttributes(aAttrs) {
  MOZ_ASSERT(aStorage);
}

bool BounceTrackingStateGlobal::HasUserActivation(
    const BounceTrackingSite& aSite) const {
  MOZ_ASSERT(IsOwnSite(aSite));
  return mUserActivation.Contains(aSite.SiteHost());
}

nsresult BounceTrackingStateGlobal::RecordUserActivation(
    const BounceTrackingSite& aSite, PRTime aTime, bool aSkipStorage) {
  NS_ENSURE_TRUE(IsOwnSite(aSite), NS_ERROR_INVALID_ARG);
  NS_ENSURE_TRUE(aTime > 0, NS_ERROR_INVALID_ARG);

  // A site must only be in one of the maps at a time.
  bool hasRemoved = mBounceTrackers.Remove(aSite.SiteHost());

  if (hasRemoved) {
    MOZ_LOG_FMT(
        gBounceTrackingProtectionLog, LogLevel::Debug,
        "{}: Removed bounce tracking candidate due to user activation: {}",
        __FUNCTION__, aSite);
  }

  // Make sure we don't overwrite an existing, more recent user activation. This
  // is only relevant for callers that pass in a timestamp that isn't PR_Now(),
  // e.g. when importing user activation data.
  Maybe<PRTime> existingUserActivation =
      mUserActivation.MaybeGet(aSite.SiteHost());
  if (existingUserActivation.isSome() &&
      existingUserActivation.value() >= aTime) {
    MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                "{}: Skip: A more recent user activation already exists for {}",
                __FUNCTION__, aSite);
    return NS_OK;
  }

  mUserActivation.InsertOrUpdate(aSite.SiteHost(), aTime);

  if (aSkipStorage || !ShouldPersistToDisk()) {
    return NS_OK;
  }

  // Write the change to storage.
  NS_ENSURE_TRUE(mStorage, NS_ERROR_FAILURE);
  return mStorage->UpdateDBEntry(
      aSite, BounceTrackingProtectionStorage::EntryType::UserActivation, aTime);
}

nsresult BounceTrackingStateGlobal::TestRemoveUserActivation(
    const BounceTrackingSite& aSite) {
  MOZ_ASSERT(IsOwnSite(aSite));
  bool hasRemoved = mUserActivation.Remove(aSite.SiteHost());

  // Avoid potentially removing a bounce tracking entry if there is no user
  // activation entry.
  if (!hasRemoved) {
    return NS_OK;
  }

  if (!ShouldPersistToDisk()) {
    return NS_OK;
  }

  // Write the change to storage.
  NS_ENSURE_TRUE(mStorage, NS_ERROR_FAILURE);
  return mStorage->DeleteDBEntries(&mOriginAttributes, aSite.SiteHost());
}

nsresult BounceTrackingStateGlobal::ClearUserActivationBefore(PRTime aTime) {
  return ClearByTimeRange(
      0, Some(aTime),
      Some(BounceTrackingProtectionStorage::EntryType::UserActivation));
}

nsresult BounceTrackingStateGlobal::ClearSite(const BounceTrackingSite& aSite,
                                              bool aSkipStorage) {
  NS_ENSURE_TRUE(IsOwnSite(aSite), NS_ERROR_INVALID_ARG);

  bool removedUserActivation = mUserActivation.Remove(aSite.SiteHost());
  bool removedBounceTracker = mBounceTrackers.Remove(aSite.SiteHost());
  if (removedUserActivation || removedBounceTracker) {
    MOZ_ASSERT(removedUserActivation != removedBounceTracker,
               "A site must only be in one of the maps at a time.");
  }

  mRecentPurges.Remove(aSite.SiteHost());

  if (aSkipStorage || !ShouldPersistToDisk()) {
    return NS_OK;
  }

  NS_ENSURE_TRUE(mStorage, NS_ERROR_FAILURE);
  return mStorage->DeleteDBEntries(&mOriginAttributes, aSite.SiteHost());
}

nsresult BounceTrackingStateGlobal::ClearByTimeRange(
    PRTime aFrom, Maybe<PRTime> aTo,
    Maybe<BounceTrackingProtectionStorage::EntryType> aEntryType,
    bool aSkipStorage) {
  NS_ENSURE_ARG_MIN(aFrom, 0);
  NS_ENSURE_TRUE(!aTo || aTo.value() > aFrom, NS_ERROR_INVALID_ARG);

  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
              "{}: Clearing user activations by time range from {} to {} {}",
              __FUNCTION__, aFrom, aTo.valueOr(0), *this);

  // Clear in memory user activation data.
  if (aEntryType.isNothing() ||
      aEntryType.value() ==
          BounceTrackingProtectionStorage::EntryType::UserActivation) {
    for (auto iter = mUserActivation.Iter(); !iter.Done(); iter.Next()) {
      if (iter.Data() >= aFrom &&
          (aTo.isNothing() || iter.Data() <= aTo.value())) {
        MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                    "{}: Remove user activation for {}", __FUNCTION__,
                    iter.Key());
        iter.Remove();
      }
    }
  }

  // Clear in memory bounce tracker data.
  if (aEntryType.isNothing() ||
      aEntryType.value() ==
          BounceTrackingProtectionStorage::EntryType::BounceTracker) {
    for (auto iter = mBounceTrackers.Iter(); !iter.Done(); iter.Next()) {
      if (iter.Data().mBounceTime >= aFrom &&
          (aTo.isNothing() || iter.Data().mBounceTime <= aTo.value())) {
        MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                    "{}: Remove bouncer tracker for {}", __FUNCTION__,
                    iter.Key());
        iter.Remove();
      }
    }
  }

  // Clear purge tracker log if there is no type filter. Used for
  // ClearDataService.
  if (aEntryType.isNothing()) {
    for (auto iter = mRecentPurges.Iter(); !iter.Done(); iter.Next()) {
      auto& purgeArray = iter.Data();

      // Use RemoveElementsBy to safely remove elements from nsTArray while
      // iterating.
      purgeArray.RemoveElementsBy([&](const auto& entry) {
        const PRTime& purgeTime = entry->PurgeTimeRefConst();
        bool shouldRemove =
            purgeTime >= aFrom && (aTo.isNothing() || purgeTime <= aTo.value());

        if (shouldRemove) {
          MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug,
                      "{}: Remove purge log entry for site {}", __FUNCTION__,
                      iter.Key());
        }
        return shouldRemove;
      });

      // If the array is now empty, remove the entire entry from mRecentPurges
      if (purgeArray.IsEmpty()) {
        iter.Remove();
      }
    }
  }

  if (aSkipStorage || !ShouldPersistToDisk()) {
    return NS_OK;
  }

  // Write the change to storage.
  NS_ENSURE_TRUE(mStorage, NS_ERROR_FAILURE);
  return mStorage->DeleteDBEntriesInTimeRange(&mOriginAttributes, aFrom, aTo,
                                              aEntryType);
}

void BounceTrackingStateGlobal::DropChainRecordsIf(
    ChainRecordPredicate aPredicate) {
  for (auto iter = mBounceTrackers.Iter(); !iter.Done(); iter.Next()) {
    RefPtr<BounceTrackingRecord>& record = iter.Data().mRecord;
    if (record && aPredicate(*record)) {
      record = nullptr;
    }
  }
  for (auto iter = mRecentPurges.Iter(); !iter.Done(); iter.Next()) {
    for (const RefPtr<BounceTrackingPurgeEntry>& entry : iter.Data()) {
      BounceTrackingRecord* record = entry->GetBounceChainRecord();
      if (record && aPredicate(*record)) {
        entry->SetBounceChainRecord(nullptr);
      }
    }
  }
}

bool BounceTrackingStateGlobal::HasBounceTracker(
    const BounceTrackingSite& aSite) const {
  MOZ_ASSERT(IsOwnSite(aSite));
  return mBounceTrackers.Contains(aSite.SiteHost());
}

nsresult BounceTrackingStateGlobal::RecordBounceTracker(
    const BounceTrackingSite& aSite, PRTime aTime, bool aSkipStorage,
    BounceTrackingRecord* aRecord) {
  NS_ENSURE_TRUE(IsOwnSite(aSite), NS_ERROR_INVALID_ARG);
  NS_ENSURE_TRUE(aTime > 0, NS_ERROR_INVALID_ARG);

  // Can not record a bounce tracker if the site has a user activation.
  NS_ENSURE_TRUE(!mUserActivation.Contains(aSite.SiteHost()), NS_ERROR_FAILURE);
  mBounceTrackers.InsertOrUpdate(aSite.SiteHost(),
                                 BounceTrackerCandidate{aTime, aRecord});

  if (aSkipStorage || !ShouldPersistToDisk()) {
    return NS_OK;
  }

  // Write the change to storage.
  NS_ENSURE_TRUE(mStorage, NS_ERROR_FAILURE);
  return mStorage->UpdateDBEntry(
      aSite, BounceTrackingProtectionStorage::EntryType::BounceTracker, aTime);
}

nsresult BounceTrackingStateGlobal::RecordPurgedTracker(
    const RefPtr<BounceTrackingPurgeEntry>& aEntry) {
  MOZ_ASSERT(StaticPrefs::privacy_bounceTrackingProtection_mode() ==
                 nsIBounceTrackingProtection::MODE_ENABLED,
             "Should only record purged trackers when the feature is enabled.");

  NS_ENSURE_ARG_POINTER(aEntry);
  // Ensure that entries unrelated to this state global can not be added.
  bool entryOAMatchesStateGlobalOA = IsOwnSite(aEntry->SiteRef());
  MOZ_ASSERT(entryOAMatchesStateGlobalOA);
  NS_ENSURE_TRUE(entryOAMatchesStateGlobalOA, NS_ERROR_INVALID_ARG);

  nsTArray<RefPtr<BounceTrackingPurgeEntry>>& entriesForSite =
      mRecentPurges.LookupOrInsert(aEntry->SiteRef().SiteHost());
  entriesForSite.AppendElement(aEntry);

  return NS_OK;
}

nsresult BounceTrackingStateGlobal::RemoveBounceTrackers(
    const nsTArray<BounceTrackingSite>& aSites) {
  for (const BounceTrackingSite& site : aSites) {
    MOZ_ASSERT(IsOwnSite(site));
    mBounceTrackers.Remove(site.SiteHost());

    // TODO: Create a bulk delete query.
    if (ShouldPersistToDisk()) {
      NS_ENSURE_TRUE(mStorage, NS_ERROR_FAILURE);
      nsresult rv =
          mStorage->DeleteDBEntries(&mOriginAttributes, site.SiteHost());
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  return NS_OK;
}

nsresult BounceTrackingStateGlobal::ClearByType(
    BounceTrackingProtectionStorage::EntryType aType, bool aSkipStorage) {
  if (aType == BounceTrackingProtectionStorage::EntryType::BounceTracker) {
    mBounceTrackers.Clear();
  } else {
    MOZ_ASSERT(aType ==
               BounceTrackingProtectionStorage::EntryType::UserActivation);
    mUserActivation.Clear();
  }

  if (aSkipStorage || !ShouldPersistToDisk()) {
    return NS_OK;
  }

  NS_ENSURE_TRUE(mStorage, NS_ERROR_FAILURE);
  return mStorage->DeleteDBEntriesByType(
      &mOriginAttributes,
      BounceTrackingProtectionStorage::EntryType::BounceTracker);
}

}  // namespace mozilla
