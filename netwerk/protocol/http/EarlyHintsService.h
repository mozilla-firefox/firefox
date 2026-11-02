/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_EarlyHintsService_h
#define mozilla_net_EarlyHintsService_h

#include "mozilla/Maybe.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/ipc/IdType.h"
#include "nsStringFwd.h"
#include "nsTArray.h"

class nsIChannel;
class nsIURI;
class nsIInterfaceRequestor;
namespace mozilla::dom {
class CanonicalBrowsingContext;
}

namespace mozilla::net {

class EarlyHintConnectArgs;
class OngoingEarlyHints;

class EarlyHintsService {
 public:
  EarlyHintsService();
  ~EarlyHintsService();
  void EarlyHint(const nsACString& aLinkHeader, nsIURI* aBaseURI,
                 nsIChannel* aChannel, const nsACString& aReferrerPolicy,
                 const nsACString& aCSPHeader,
                 const nsACString& aConnectionAllowlistHeader,
                 dom::CanonicalBrowsingContext* aLoadingBrowsingContext);
  void Reset();
  void Cancel(const nsACString& aReason);

  void RegisterLinksAndGetConnectArgs(
      dom::ContentParentId aCpId, nsTArray<EarlyHintConnectArgs>& aOutLinks);

  uint32_t LinkType() const { return mLinkType; }

 private:
  Maybe<TimeStamp> mFirstEarlyHint;
  uint32_t mEarlyHintsCount{0};

  RefPtr<OngoingEarlyHints> mOngoingEarlyHints;
  uint32_t mLinkType = 0;
};

}  // namespace mozilla::net

#endif  // mozilla_net_EarlyHintsService_h
