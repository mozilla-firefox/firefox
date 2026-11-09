/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BounceTrackingMapEntry_h
#define mozilla_BounceTrackingMapEntry_h

#include "BounceTrackingRecord.h"
#include "BounceTrackingSite.h"
#include "nsIBounceTrackingMapEntry.h"
#include "nsString.h"

namespace mozilla {

/**
 * Represents an entry in the global bounce tracker or user activation map.
 */
class BTPMapEntry {
 public:
  const BounceTrackingSite& SiteRef() const { return mSite; }

  PRTime& TimeStampRef() { return mTimeStamp; }

 protected:
  BTPMapEntry(const BounceTrackingSite& aSite, PRTime aTimeStamp)
      : mSite(aSite), mTimeStamp(aTimeStamp) {
    MOZ_ASSERT(!aSite.IsNull());
  }

  BounceTrackingSite mSite;
  PRTime mTimeStamp;
};

class BounceTrackingMapEntry final : public BTPMapEntry,
                                     public nsIBounceTrackingMapEntry {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIBOUNCETRACKINGMAPENTRY

  BounceTrackingMapEntry(const BounceTrackingSite& aSite, PRTime aTimeStamp)
      : BTPMapEntry(aSite, aTimeStamp) {}

 private:
  ~BounceTrackingMapEntry() = default;
};

/**
 * Represents a log entry for a purged bounce tracker. Extends
 * BounceTrackingMapEntry with the time of purge.
 */
class BounceTrackingPurgeEntry final : public BTPMapEntry,
                                       public nsIBounceTrackingPurgeEntry {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIBOUNCETRACKINGMAPENTRY
  NS_DECL_NSIBOUNCETRACKINGPURGEENTRY
  BounceTrackingPurgeEntry(const BounceTrackingSite& aSite, PRTime aBounceTime,
                           PRTime aPurgeTime)
      : BTPMapEntry(aSite, aBounceTime), mPurgeTime(aPurgeTime) {}

  PRTime& BounceTimeRef() { return mTimeStamp; }

  PRTime& PurgeTimeRef() { return mPurgeTime; }

  const PRTime& PurgeTimeRefConst() const { return mPurgeTime; }

  void SetBounceChainRecord(BounceTrackingRecord* aRecord) {
    mChainRecord = aRecord;
  }

  BounceTrackingRecord* GetBounceChainRecord() const { return mChainRecord; }

 private:
  ~BounceTrackingPurgeEntry() = default;
  // Timestamp of when the purge completed. mTimeStamp is the time of when the
  // bounce ocurred.
  PRTime mPurgeTime;
  RefPtr<BounceTrackingRecord> mChainRecord;
};

}  // namespace mozilla

#endif
