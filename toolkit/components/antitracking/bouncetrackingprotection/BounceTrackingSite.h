/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BounceTrackingSite_h
#define mozilla_BounceTrackingSite_h

#include "mozilla/HashFunctions.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Result.h"
#include "nsHashKeys.h"
#include "nsString.h"
#include "fmt/format.h"

class nsIPrincipal;
class nsIURI;

namespace mozilla {

// A schemeless site together with the storage partition it ran in, which is
// the OriginAttributes stripped of first party domain and partition key. BTP
// state is kept per site and partition. Either a site or the null site, which
// stands in for documents BTP does not track and belongs to no partition.
class BounceTrackingSite final {
 public:
  // The null site.
  BounceTrackingSite() = default;

  // The site of aPrincipal in its own partition, or the null site if BTP does
  // not track aPrincipal.
  static Result<BounceTrackingSite, nsresult> FromPrincipal(
      nsIPrincipal* aPrincipal);

  // The site of aURI loaded in aOriginAttributes, or the null site if aURI is
  // not http(s) or has no site.
  static Result<BounceTrackingSite, nsresult> FromURI(
      nsIURI* aURI, const OriginAttributes& aOriginAttributes);

  // For sites persisted as a site host or passed in as one over XPCOM. Prefer
  // FromPrincipal or FromURI. aSiteHost must already be a schemeless site,
  // e.g. "example.com" rather than "www.example.com", or empty for the null
  // site.
  static BounceTrackingSite FromSiteHost(
      const nsACString& aSiteHost, const OriginAttributes& aOriginAttributes);

  bool IsNull() const { return mSiteHost.IsEmpty(); }

  // Empty for the null site.
  const nsCString& SiteHost() const { return mSiteHost; }

  const OriginAttributes& OriginAttributesRef() const {
    MOZ_ASSERT(!IsNull());
    return mOriginAttributes;
  }

  bool operator==(const BounceTrackingSite& aOther) const {
    return mSiteHost == aOther.mSiteHost &&
           mOriginAttributes == aOther.mOriginAttributes;
  }
  bool operator!=(const BounceTrackingSite& aOther) const {
    return !(*this == aOther);
  }

  PLDHashNumber Hash() const {
    return AddToHash(HashString(mSiteHost), mOriginAttributes.Hash());
  }

 private:
  BounceTrackingSite(const nsACString& aSiteHost,
                     const OriginAttributes& aOriginAttributes);

  nsCString mSiteHost;
  OriginAttributes mOriginAttributes;
};

using BounceTrackingSiteHashKey = nsGenericHashKey<BounceTrackingSite>;

}  // namespace mozilla

template <>
struct fmt::formatter<mozilla::BounceTrackingSite>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::BounceTrackingSite& aSite,
              fmt::format_context& aCtx) const {
    if (aSite.IsNull()) {
      return fmt::format_to(aCtx.out(), "null");
    }
    nsAutoCString suffix;
    aSite.OriginAttributesRef().CreateSuffix(suffix);
    return fmt::format_to(aCtx.out(), "{}{}", aSite.SiteHost(), suffix);
  }
};

#endif
