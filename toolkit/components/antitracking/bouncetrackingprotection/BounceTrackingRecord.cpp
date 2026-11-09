/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingRecord.h"
#include "BounceTrackingProtectionStorage.h"
#include "mozilla/Logging.h"

namespace mozilla {

extern LazyLogModule gBounceTrackingProtectionLog;

NS_IMPL_ISUPPORTS(BounceTrackingRecord, nsIBounceTrackingRecord);

void BounceTrackingRecord::SetInitialSite(const BounceTrackingSite& aSite) {
  mInitialSite = aSite;
}

const BounceTrackingSite& BounceTrackingRecord::InitialSite() const {
  return mInitialSite;
}

void BounceTrackingRecord::SetFinalSite(const BounceTrackingSite& aSite) {
  mFinalSite = aSite;
}

const BounceTrackingSite& BounceTrackingRecord::FinalSite() const {
  return mFinalSite;
}

void BounceTrackingRecord::AddBounceSite(const BounceTrackingSite& aSite) {
  mBounceSites.Insert(aSite);
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: {}",
              __FUNCTION__, *this);
}

const BounceTrackingRecord::BounceSiteSet& BounceTrackingRecord::BounceSites()
    const {
  return mBounceSites;
}

void BounceTrackingRecord::RemoveBounceSites(
    const OriginAttributes& aOriginAttributes) {
  OriginAttributes originAttributes =
      BounceTrackingProtectionStorage::NormalizeOriginAttributes(
          aOriginAttributes);
  mBounceSites.RemoveIf([&](const BounceTrackingSite& aSite) {
    return !aSite.IsNull() && aSite.OriginAttributesRef() == originAttributes;
  });
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: {}",
              __FUNCTION__, *this);
}

bool BounceTrackingRecord::HasBounceSiteHost(
    const nsACString& aSiteHost) const {
  MOZ_ASSERT(!aSiteHost.IsEmpty());
  for (const BounceTrackingSite& site : mBounceSites) {
    if (site.SiteHost() == aSiteHost) {
      return true;
    }
  }
  return false;
}

bool BounceTrackingRecord::HasOriginAttributesMatching(
    OriginAttributesPredicate aPredicate) const {
  auto matches = [&](const BounceTrackingSite& aSite) {
    return !aSite.IsNull() && aPredicate(aSite.OriginAttributesRef());
  };
  if (matches(mInitialSite) || matches(mFinalSite)) {
    return true;
  }
  for (const BounceTrackingSite& site : mBounceSites) {
    if (matches(site)) {
      return true;
    }
  }
  return false;
}

bool BounceTrackingRecord::HasSiteHost(
    const nsACString& aSiteHost, OriginAttributesPredicate aPredicate) const {
  MOZ_ASSERT(!aSiteHost.IsEmpty());
  auto matches = [&](const BounceTrackingSite& aSite) {
    return aSite.SiteHost() == aSiteHost &&
           aPredicate(aSite.OriginAttributesRef());
  };
  if (matches(mInitialSite) || matches(mFinalSite)) {
    return true;
  }
  for (const BounceTrackingSite& site : mBounceSites) {
    if (matches(site)) {
      return true;
    }
  }
  return false;
}

void BounceTrackingRecord::AddUserActivationSite(
    const BounceTrackingSite& aSite) {
  if (!aSite.IsNull()) {
    mUserActivationHosts.Insert(aSite.SiteHost());
  }
}

const nsTHashSet<nsCStringHashKey>&
BounceTrackingRecord::GetUserActivationHosts() const {
  return mUserActivationHosts;
}

void BounceTrackingRecord::ClearUserActivationHosts() {
  mUserActivationHosts.Clear();
}

// nsIBounceTrackingRecord

NS_IMETHODIMP BounceTrackingRecord::GetInitialHost(nsACString& aResult) {
  aResult = mInitialSite.SiteHost();
  return NS_OK;
}

NS_IMETHODIMP BounceTrackingRecord::GetFinalHost(nsACString& aResult) {
  aResult = mFinalSite.SiteHost();
  return NS_OK;
}

NS_IMETHODIMP BounceTrackingRecord::GetBounceHosts(
    nsTArray<nsCString>& aResult) {
  for (const BounceTrackingSite& site : mBounceSites) {
    if (!site.IsNull() && !aResult.Contains(site.SiteHost())) {
      aResult.AppendElement(site.SiteHost());
    }
  }
  return NS_OK;
}

}  // namespace mozilla
