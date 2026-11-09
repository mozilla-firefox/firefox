/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingRecord.h"
#include "BounceTrackingProtectionStorage.h"
#include "mozilla/Logging.h"

namespace mozilla {

extern LazyLogModule gBounceTrackingProtectionLog;

NS_IMPL_ISUPPORTS(BounceTrackingRecord, nsIBounceTrackingRecord);

void BounceTrackingRecord::SetInitialHost(
    const nsACString& aHost, const OriginAttributes& aOriginAttributes) {
  mInitialHost = aHost;
  mInitialOriginAttributes =
      BounceTrackingProtectionStorage::NormalizeOriginAttributes(
          aOriginAttributes);
}

const nsACString& BounceTrackingRecord::GetInitialHost() const {
  return mInitialHost;
}

void BounceTrackingRecord::SetFinalHost(
    const nsACString& aHost, const OriginAttributes& aOriginAttributes) {
  mFinalHost = aHost;
  mFinalOriginAttributes =
      BounceTrackingProtectionStorage::NormalizeOriginAttributes(
          aOriginAttributes);
}

bool BounceTrackingRecord::IsInitialSite(
    const nsACString& aHost, const OriginAttributes& aOriginAttributes) const {
  return aHost == mInitialHost && aOriginAttributes == mInitialOriginAttributes;
}

bool BounceTrackingRecord::IsFinalSite(
    const nsACString& aHost, const OriginAttributes& aOriginAttributes) const {
  return aHost == mFinalHost && aOriginAttributes == mFinalOriginAttributes;
}

const nsACString& BounceTrackingRecord::GetFinalHost() const {
  return mFinalHost;
}

void BounceTrackingRecord::AddBounceHost(
    const nsACString& aHost, const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(!aHost.IsEmpty());

  mBounceHosts
      .LookupOrInsert(
          BounceTrackingProtectionStorage::NormalizeOriginAttributes(
              aOriginAttributes))
      .Insert(aHost);
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: {}",
              __FUNCTION__, *this);
}

void BounceTrackingRecord::RemoveBounceHosts(
    const OriginAttributes& aOriginAttributes) {
  mBounceHosts.Remove(
      BounceTrackingProtectionStorage::NormalizeOriginAttributes(
          aOriginAttributes));
  MOZ_LOG_FMT(gBounceTrackingProtectionLog, LogLevel::Debug, "{}: {}",
              __FUNCTION__, *this);
}

void BounceTrackingRecord::AddUserActivationHost(const nsACString& aHost) {
  if (!aHost.IsEmpty()) {
    mUserActivationHosts.Insert(aHost);
  }
}

const BounceTrackingRecord::BounceHostMap&
BounceTrackingRecord::GetBounceHosts() const {
  return mBounceHosts;
}

bool BounceTrackingRecord::HasBounceHost(const nsACString& aHost) const {
  for (const auto& hosts : mBounceHosts.Values()) {
    if (hosts.Contains(aHost)) {
      return true;
    }
  }
  return false;
}

bool BounceTrackingRecord::HasOriginAttributesMatching(
    OriginAttributesPredicate aPredicate) const {
  if ((!mInitialHost.IsEmpty() && aPredicate(mInitialOriginAttributes)) ||
      (!mFinalHost.IsEmpty() && aPredicate(mFinalOriginAttributes))) {
    return true;
  }
  for (const auto& entry : mBounceHosts) {
    if (!aPredicate(entry.GetKey())) {
      continue;
    }
    for (const nsACString& host : entry.GetData()) {
      if (!host.EqualsLiteral("null")) {
        return true;
      }
    }
  }
  return false;
}

bool BounceTrackingRecord::HasSiteHost(
    const nsACString& aHost, OriginAttributesPredicate aPredicate) const {
  if ((aHost == mInitialHost && aPredicate(mInitialOriginAttributes)) ||
      (aHost == mFinalHost && aPredicate(mFinalOriginAttributes))) {
    return true;
  }
  for (const auto& entry : mBounceHosts) {
    if (aPredicate(entry.GetKey()) && entry.GetData().Contains(aHost)) {
      return true;
    }
  }
  return false;
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
  aResult = mInitialHost;
  return NS_OK;
}

NS_IMETHODIMP BounceTrackingRecord::GetFinalHost(nsACString& aResult) {
  aResult = mFinalHost;
  return NS_OK;
}

NS_IMETHODIMP BounceTrackingRecord::GetBounceHosts(
    nsTArray<nsCString>& aResult) {
  for (const auto& hosts : mBounceHosts.Values()) {
    for (const auto& host : hosts) {
      if (!host.EqualsLiteral("null") && !aResult.Contains(host)) {
        aResult.AppendElement(host);
      }
    }
  }
  return NS_OK;
}

}  // namespace mozilla
