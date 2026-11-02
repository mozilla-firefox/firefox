/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_CookieServiceParent_h
#define mozilla_net_CookieServiceParent_h

#include "mozilla/net/CookieKey.h"
#include "mozilla/net/PCookieServiceParent.h"

class nsIArray;
class nsICookie;
namespace mozilla {
class OriginAttributes;

namespace dom {
class ContentParent;
}  // namespace dom
}  // namespace mozilla

class nsIEffectiveTLDService;

namespace mozilla {
namespace net {

class Cookie;
class CookieService;

class CookieServiceParent : public PCookieServiceParent {
  friend class PCookieServiceParent;

 public:
  // A RAII class to set and unset the cookie processing flag.
  class MOZ_STACK_CLASS CookieProcessingGuard final {
   public:
    explicit CookieProcessingGuard(CookieServiceParent* aActor = nullptr) {
      if (aActor) {
        Initialize(aActor);
      }
    }

    void Initialize(CookieServiceParent* aActor) {
      MOZ_ASSERT(!mProxy && aActor);

      mProxy = aActor->GetLifecycleProxy();
      if (mProxy && mProxy->Get()) {
        auto* actor = static_cast<CookieServiceParent*>(mProxy->Get());
        MOZ_ASSERT(!actor->mProcessingCookie);
        actor->mProcessingCookie = true;
      }
    }

    ~CookieProcessingGuard() {
      if (mProxy && mProxy->Get()) {
        auto* actor = static_cast<CookieServiceParent*>(mProxy->Get());
        MOZ_ASSERT(actor->mProcessingCookie);
        actor->mProcessingCookie = false;
      }
    }

   private:
    RefPtr<mozilla::ipc::ActorLifecycleProxy> mProxy;
  };

  explicit CookieServiceParent(dom::ContentParent* aContentParent);
  virtual ~CookieServiceParent() = default;

  void TrackCookieLoad(nsIChannel* aChannel);

  void RemoveBatchDeletedCookies(nsIArray* aCookieList);

  void RemoveAll();

  void RemoveCookie(const Cookie& aCookie, const nsID* aOperationID);

  void AddCookie(const Cookie& aCookie, const nsID* aOperationID);

  // This will return true if the CookieServiceParent is currently processing
  // an update from the content process. This is used in ContentParent to make
  // sure that we are only forwarding those cookie updates to other content
  // processes, not the one they originated from.
  bool ProcessingCookie() { return mProcessingCookie; }

  bool ContentProcessHasCookie(const Cookie& cookie);
  bool ContentProcessHasCookie(const nsACString& aBaseDomain,
                               const OriginAttributes& aOriginAttributes);
  bool InsecureCookieOrSecureOrigin(const Cookie& cookie);
  void UpdateCookieInContentList(nsIURI* aHostURI,
                                 const OriginAttributes& aOriginAttrs);

  mozilla::ipc::IPCResult SetCookies(
      const nsCString& aBaseDomain, const OriginAttributes& aOriginAttributes,
      nsIURI* aHost, bool aIsThirdParty, const nsTArray<CookieStruct>& aCookies,
      dom::BrowsingContext* aBrowsingContext = nullptr);

 protected:
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  mozilla::ipc::IPCResult RecvSetCookies(
      const nsCString& aBaseDomain, const OriginAttributes& aOriginAttributes,
      nsIURI* aHost, bool aIsThirdParty,
      const nsTArray<CookieStruct>& aCookies);

  mozilla::ipc::IPCResult RecvGetCookieList(
      nsIURI* aHost, const bool& aIsForeign,
      const bool& aIsThirdPartyTrackingResource,
      const bool& aIsThirdPartySocialTrackingResource,
      const bool& aStorageAccessPermissionGranted,
      const uint32_t& aRejectedReason, const bool& aIsSafeTopLevelNav,
      const bool& aIsSameSiteForeign, const bool& aHadCrossSiteRedirects,
      nsTArray<OriginAttributes>&& aAttrsList,
      GetCookieListResolver&& aResolve);

  static void SerializeCookieListTable(
      const nsTArray<RefPtr<Cookie>>& aFoundCookieList,
      nsTArray<CookieStructTable>& aCookiesListTable, nsIURI* aHostURI);

  nsCOMPtr<nsIEffectiveTLDService> mTLDService;
  RefPtr<CookieService> mCookieService;
  bool mProcessingCookie;
  nsTHashMap<CookieKey, bool> mCookieKeysInContent;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_CookieServiceParent_h
