/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BounceTrackingRecord_h
#define mozilla_BounceTrackingRecord_h

#include "BounceTrackingSite.h"
#include "nsIBounceTrackingRecord.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/RefPtr.h"
#include "nsStringFwd.h"
#include "nsTHashSet.h"
#include "fmt/format.h"

namespace mozilla {

namespace dom {
class CanonicalBrowsingContext;
}

// Stores per-tab data relevant to bounce tracking protection for every extended
// navigation. Also implements nsIBounceTrackingRecord for XPCOM exposure.
class BounceTrackingRecord final : public nsIBounceTrackingRecord {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIBOUNCETRACKINGRECORD

  BounceTrackingRecord() = default;

  void SetInitialSite(const BounceTrackingSite& aSite);

  const BounceTrackingSite& InitialSite() const;

  void SetFinalSite(const BounceTrackingSite& aSite);

  const BounceTrackingSite& FinalSite() const;

  using BounceSiteSet = nsTHashSet<BounceTrackingSiteHashKey>;

  void AddBounceSite(const BounceTrackingSite& aSite);

  const BounceSiteSet& BounceSites() const;

  // Removes the bounces which ran in the storage partition aOriginAttributes.
  void RemoveBounceSites(const OriginAttributes& aOriginAttributes);

  // Whether aSiteHost bounced in any storage partition.
  bool HasBounceSiteHost(const nsACString& aSiteHost) const;

  void AddUserActivationSite(const BounceTrackingSite& aSite);

  using OriginAttributesPredicate = FunctionRef<bool(const OriginAttributes&)>;

  // Whether any site of this record, the initial and final site included, ran
  // in a storage partition aPredicate matches. Null sites don't count.
  bool HasOriginAttributesMatching(OriginAttributesPredicate aPredicate) const;

  // Whether aSiteHost is a site of this record, the initial and final site
  // included, in a storage partition aPredicate matches.
  bool HasSiteHost(const nsACString& aSiteHost,
                   OriginAttributesPredicate aPredicate) const;

  void ClearUserActivationHosts();

  const nsTHashSet<nsCStringHashKey>& GetUserActivationHosts() const;

 private:
  ~BounceTrackingRecord() = default;

  // The site this context was on when the current extended navigation started,
  // deliberately not the navigation's initiator: this site is exempt from
  // classification, so a frame must not be able to nominate itself. The
  // exemption is per partition: the user visiting a site in one container does
  // not exempt its bounces in another.
  BounceTrackingSite mInitialSite;

  // The destination of the current extended navigation. Updated after every
  // document load. Exempt from classification like the initial site.
  BounceTrackingSite mFinalSite;

  // All server-side and client-side redirects hit during this extended
  // navigation. A load retargeted into another container runs its hops in
  // more than one partition, so each is kept with the partition it ran in.
  BounceSiteSet mBounceSites;

  // A set of sites' hosts. All sites which received user activation during
  // this extended navigation.
  // This is not used by bounce tracking protection itself, but are instead
  // used to enable storage access heuristics. See Bug 1935235.
  nsTHashSet<nsCStringHashKey> mUserActivationHosts;

  friend struct fmt::formatter<BounceTrackingRecord>;
};

inline auto format_as(const nsTHashSet<nsCStringHashKey>& aSet) {
  return fmt::join(aSet, ",");
}

}  // namespace mozilla

template <>
struct fmt::formatter<mozilla::BounceTrackingRecord>
    : fmt::formatter<std::string_view> {
  auto format(const mozilla::BounceTrackingRecord& aRec,
              fmt::format_context& aCtx) const {
    auto out = fmt::format_to(
        aCtx.out(), "{{mInitialSite:{}, mFinalSite:{}, mBounceSites:[",
        aRec.mInitialSite, aRec.mFinalSite);
    bool first = true;
    for (const mozilla::BounceTrackingSite& site : aRec.mBounceSites) {
      out = fmt::format_to(out, "{}{}", first ? "" : ",", site);
      first = false;
    }
    return fmt::format_to(out, "], mUserActivationHosts:[{}]}}",
                          aRec.mUserActivationHosts);
  }
};

template <>
struct fmt::formatter<RefPtr<mozilla::BounceTrackingRecord>>
    : fmt::formatter<std::string_view> {
  auto format(const RefPtr<mozilla::BounceTrackingRecord>& aRec,
              fmt::format_context& aCtx) const {
    if (aRec) {
      return fmt::formatter<mozilla::BounceTrackingRecord>{}.format(*aRec,
                                                                    aCtx);
    }
    return fmt::format_to(aCtx.out(), "null");
  }
};

#endif
