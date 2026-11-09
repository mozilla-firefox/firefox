/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_BounceTrackingRecord_h
#define mozilla_BounceTrackingRecord_h

#include "nsIBounceTrackingRecord.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/OriginAttributesHashKey.h"
#include "mozilla/RefPtr.h"
#include "nsStringFwd.h"
#include "nsTHashMap.h"
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

  // aOriginAttributes is the storage partition the site's document ran in.
  void SetInitialHost(const nsACString& aHost,
                      const OriginAttributes& aOriginAttributes);

  const nsACString& GetInitialHost() const;

  void SetFinalHost(const nsACString& aHost,
                    const OriginAttributes& aOriginAttributes);

  const nsACString& GetFinalHost() const;

  // Whether a bounce of aHost in the storage partition aOriginAttributes is
  // exempt from classification as the initial or final site. The exemption is
  // per partition: the user visiting a site in one container does not exempt
  // its bounces in another.
  bool IsInitialSite(const nsACString& aHost,
                     const OriginAttributes& aOriginAttributes) const;
  bool IsFinalSite(const nsACString& aHost,
                   const OriginAttributes& aOriginAttributes) const;

  using BounceHostMap =
      nsTHashMap<OriginAttributesHashKey, nsTHashSet<nsCStringHashKey>>;

  // aOriginAttributes is the storage partition the bounce ran in. A load
  // retargeted into another container runs its hops in more than one.
  void AddBounceHost(const nsACString& aHost,
                     const OriginAttributes& aOriginAttributes);

  // Removes the bounces which ran in the storage partition aOriginAttributes.
  void RemoveBounceHosts(const OriginAttributes& aOriginAttributes);

  void AddUserActivationHost(const nsACString& aHost);

  const BounceHostMap& GetBounceHosts() const;

  // Whether aHost bounced in any storage partition.
  bool HasBounceHost(const nsACString& aHost) const;

  using OriginAttributesPredicate = FunctionRef<bool(const OriginAttributes&)>;

  // Whether any site of this record, the initial and final site included, ran
  // in a storage partition aPredicate matches. "null" bounce entries are no
  // site and don't count.
  bool HasOriginAttributesMatching(OriginAttributesPredicate aPredicate) const;

  // Whether aHost is a site of this record, the initial and final site
  // included, in a storage partition aPredicate matches.
  bool HasSiteHost(const nsACString& aHost,
                   OriginAttributesPredicate aPredicate) const;

  void ClearUserActivationHosts();

  const nsTHashSet<nsCStringHashKey>& GetUserActivationHosts() const;

 private:
  ~BounceTrackingRecord() = default;

  // A site's host. The site this context was on when the current extended
  // navigation started, deliberately not the navigation's initiator: this host
  // is exempt from classification, so a frame must not be able to nominate
  // itself.
  nsAutoCString mInitialHost;
  OriginAttributes mInitialOriginAttributes;

  // A site's host or null. The destination of the current extended navigation.
  // Updated after every document load.
  nsAutoCString mFinalHost;
  OriginAttributes mFinalOriginAttributes;

  // Sets of sites' hosts, keyed by the OriginAttributes of the storage
  // partition they ran in. All server-side and client-side redirects hit during
  // this extended navigation.
  BounceHostMap mBounceHosts;

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
    auto out = aCtx.out();
    auto hostOrUnset = [](const nsACString& aHost) -> std::string_view {
      if (aHost.IsEmpty()) {
        return "<unset>";
      }
      return {aHost.BeginReading(), aHost.Length()};
    };
    nsAutoCString initialSuffix;
    aRec.mInitialOriginAttributes.CreateSuffix(initialSuffix);
    nsAutoCString finalSuffix;
    aRec.mFinalOriginAttributes.CreateSuffix(finalSuffix);
    out = fmt::format_to(out,
                         "{{mInitialHost:{}{}, mFinalHost:{}{}, mBounceHosts:[",
                         hostOrUnset(aRec.mInitialHost), initialSuffix,
                         hostOrUnset(aRec.mFinalHost), finalSuffix);
    bool first = true;
    for (const auto& entry : aRec.mBounceHosts) {
      nsAutoCString suffix;
      entry.GetKey().CreateSuffix(suffix);
      out = fmt::format_to(out, "{}'{}':[{}]", first ? "" : ", ", suffix,
                           entry.GetData());
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
