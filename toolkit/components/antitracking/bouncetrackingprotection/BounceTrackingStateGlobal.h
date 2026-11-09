/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BounceTrackingStateGlobal_h
#define mozilla_BounceTrackingStateGlobal_h

#include "BounceTrackingMapEntry.h"
#include "BounceTrackingProtectionStorage.h"
#include "BounceTrackingRecord.h"
#include "BounceTrackingSite.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/Maybe.h"
#include "mozilla/WeakPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsTHashMap.h"
#include "nsTArray.h"
#include "nsISupports.h"
#include "fmt/format.h"

namespace mozilla {

struct BounceTrackerCandidate {
  PRTime mBounceTime;
  RefPtr<BounceTrackingRecord> mRecord;
};

/**
 * This class holds the global state maps which are used to keep track of
 * potential bounce trackers and user activations.
 * @see BounceTrackingState for the per browser / tab state.
 *
 * Updates to the state maps are persisted to storage.
 */
class BounceTrackingStateGlobal final {
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(BounceTrackingStateGlobal);
  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(BounceTrackingStateGlobal);

  BounceTrackingStateGlobal(BounceTrackingProtectionStorage* aStorage,
                            const OriginAttributes& aAttrs);

  bool IsPrivateBrowsing() const {
    return mOriginAttributes.IsPrivateBrowsing();
  }

  bool ShouldPersistToDisk() const { return !IsPrivateBrowsing(); }

  const OriginAttributes& OriginAttributesRef() const {
    return mOriginAttributes;
  };

  // The site of aSiteHost in the partition of this state global. Maps of the
  // state global are keyed by site host.
  BounceTrackingSite SiteFor(const nsACString& aSiteHost) const {
    return BounceTrackingSite::FromSiteHost(aSiteHost, mOriginAttributes);
  }

  bool HasUserActivation(const BounceTrackingSite& aSite) const;

  // Store a user interaction flag for the given site. This will remove the
  // site from the bounce tracker map if it exists.
  [[nodiscard]] nsresult RecordUserActivation(const BounceTrackingSite& aSite,
                                              PRTime aTime,
                                              bool aSkipStorage = false);

  // Test-only method to clear a user activation flag.
  [[nodiscard]] nsresult TestRemoveUserActivation(
      const BounceTrackingSite& aSite);

  // Clear any user interactions that happened before aTime.
  [[nodiscard]] nsresult ClearUserActivationBefore(PRTime aTime);

  bool HasBounceTracker(const BounceTrackingSite& aSite) const;

  // Store a bounce tracker flag for the given site. A site which received user
  // interaction recently can not be recorded as a bounce tracker.
  [[nodiscard]] nsresult RecordBounceTracker(
      const BounceTrackingSite& aSite, PRTime aTime, bool aSkipStorage = false,
      BounceTrackingRecord* aRecord = nullptr);

  // Record the fact that we have purged state for a bounce tracker. This is
  // used in the purged trackers log.
  [[nodiscard]] nsresult RecordPurgedTracker(
      const RefPtr<BounceTrackingPurgeEntry>& aEntry);

  // Remove one or many bounce trackers.
  [[nodiscard]] nsresult RemoveBounceTrackers(
      const nsTArray<BounceTrackingSite>& aSites);

  // Clear user activation or bounce tracker map.
  [[nodiscard]] nsresult ClearByType(
      BounceTrackingProtectionStorage::EntryType aType, bool aSkipStorage);

  [[nodiscard]] nsresult ClearSite(const BounceTrackingSite& aSite,
                                   bool aSkipStorage = false);

  [[nodiscard]] nsresult ClearByTimeRange(
      PRTime aFrom, Maybe<PRTime> aTo = Nothing(),
      Maybe<BounceTrackingProtectionStorage::EntryType> aEntryType = Nothing(),
      bool aSkipStorage = false);

  using ChainRecordPredicate = FunctionRef<bool(const BounceTrackingRecord&)>;

  // Drops the bounce chain record of every bounce tracker candidate and purge
  // log entry whose record aPredicate matches. Classification and purging don't
  // depend on the record.
  void DropChainRecordsIf(ChainRecordPredicate aPredicate);

  const nsTHashMap<nsCStringHashKey, PRTime>& UserActivationMapRef() {
    return mUserActivation;
  }

  const nsTHashMap<nsCStringHashKey, BounceTrackerCandidate>&
  BounceTrackersMapRef() {
    return mBounceTrackers;
  }

  const nsTHashMap<nsCStringHashKey,
                   nsTArray<RefPtr<BounceTrackingPurgeEntry>>>&
  RecentPurgesMapRef() {
    return mRecentPurges;
  }

 private:
  ~BounceTrackingStateGlobal() = default;

  // Whether aSite belongs to the partition of this state global.
  bool IsOwnSite(const BounceTrackingSite& aSite) const {
    return !aSite.IsNull() && aSite.OriginAttributesRef() == mOriginAttributes;
  }

  // The storage which manages this state global. Used to persist changes to
  // this state global in storage.
  // This needs to be a weak pointer to avoid BounceTrackingProtectionStorage
  // and BounceTrackingStateGlobal holding strong references to each other
  // leading to memory leaks.
  WeakPtr<BounceTrackingProtectionStorage> mStorage;

  // Origin attributes this state global is associated with. e.g. if the state
  // was associated with a PBM window this would set privateBrowsingId: 1.
  OriginAttributes mOriginAttributes;

  // Map of site hosts to moments. The moments represent the most recent wall
  // clock time at which the user activated a top-level document on the
  // associated site host.
  nsTHashMap<nsCStringHashKey, PRTime> mUserActivation;

  // Map of site hosts to BounceTrackerCandidate. The bounce time represents the
  // first wall clock time since the last execution of the bounce tracking timer
  // at which a page on the given site host performed an action that could
  // indicate stateful bounce tracking took place.
  nsTHashMap<nsCStringHashKey, BounceTrackerCandidate> mBounceTrackers;

  // Log of purges which happened since application startup. Keyed by site host.
  // The log is used for both troubleshooting purposes and for logging warnings
  // to the web console for affected sites.
  nsTHashMap<nsCStringHashKey, nsTArray<RefPtr<BounceTrackingPurgeEntry>>>
      mRecentPurges;

  friend struct fmt::formatter<BounceTrackingStateGlobal>;
};

}  // namespace mozilla

template <>
struct fmt::formatter<mozilla::BounceTrackingStateGlobal>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::BounceTrackingStateGlobal& aGlobal,
              fmt::format_context& aCtx) const {
    nsAutoCString originAttributeSuffix;
    aGlobal.mOriginAttributes.CreateSuffix(originAttributeSuffix);

    auto out = aCtx.out();
    out = fmt::format_to(out, "{{ mOriginAttributes: {}, mUserActivation: {{",
                         originAttributeSuffix);
    bool first = true;
    for (auto iter = aGlobal.mUserActivation.ConstIter(); !iter.Done();
         iter.Next()) {
      if (!first) {
        out = fmt::format_to(out, ", ");
      }
      out = fmt::format_to(out, "{}: {}", iter.Key(), iter.Data());
      first = false;
    }
    out = fmt::format_to(out, "}}, mBounceTrackers: {{");
    first = true;
    for (auto iter = aGlobal.mBounceTrackers.ConstIter(); !iter.Done();
         iter.Next()) {
      if (!first) {
        out = fmt::format_to(out, ", ");
      }
      out = fmt::format_to(out, "{}: {}", iter.Key(), iter.Data().mBounceTime);
      first = false;
    }
    return fmt::format_to(out, "}} }}");
  }
};

#endif
