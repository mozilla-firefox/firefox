/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingMapEntry.h"

namespace mozilla {

// BounceTrackingMapEntry

NS_IMPL_ISUPPORTS(BounceTrackingMapEntry, nsIBounceTrackingMapEntry);

NS_IMETHODIMP BounceTrackingMapEntry::GetSiteHost(nsACString& aSiteHost) {
  aSiteHost = mSite.SiteHost();
  return NS_OK;
}
NS_IMETHODIMP BounceTrackingMapEntry::GetTimeStamp(PRTime* aTimeStamp) {
  *aTimeStamp = mTimeStamp;
  return NS_OK;
}

// BounceTrackingPurgeEntry

NS_IMPL_ISUPPORTS(BounceTrackingPurgeEntry, nsIBounceTrackingPurgeEntry,
                  nsIBounceTrackingMapEntry);

NS_IMETHODIMP BounceTrackingPurgeEntry::GetSiteHost(nsACString& aSiteHost) {
  aSiteHost = mSite.SiteHost();
  return NS_OK;
}
NS_IMETHODIMP BounceTrackingPurgeEntry::GetTimeStamp(PRTime* aTimeStamp) {
  *aTimeStamp = mTimeStamp;
  return NS_OK;
}

NS_IMETHODIMP BounceTrackingPurgeEntry::GetPurgeTime(PRTime* aPurgeTime) {
  *aPurgeTime = mPurgeTime;
  return NS_OK;
}

NS_IMETHODIMP BounceTrackingPurgeEntry::GetBounceTrackingRecord(
    nsIBounceTrackingRecord** aResult) {
  RefPtr<BounceTrackingRecord> record = mChainRecord;
  record.forget(aResult);
  return NS_OK;
}

}  // namespace mozilla
