/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_CookieServiceChild_h_
#define mozilla_net_CookieServiceChild_h_

#include "CookieKey.h"
#include "mozIThirdPartyUtil.h"
#include "mozilla/net/PCookieServiceChild.h"
#include "nsClassHashtable.h"
#include "nsICookieService.h"
#include "nsThreadUtils.h"
#include "nsWeakReference.h"

class nsIEffectiveTLDService;
class nsILoadInfo;

namespace mozilla {
namespace net {

class Cookie;
class CookieStruct;

class CookieServiceChild final : public PCookieServiceChild,
                                 public nsICookieService,
                                 public nsSupportsWeakReference {
  friend class PCookieServiceChild;

 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICOOKIESERVICE

  typedef nsTArray<RefPtr<Cookie>> CookiesList;
  typedef nsClassHashtable<CookieKey, CookiesList> CookiesMap;

  CookieServiceChild();

  void Init();

  static already_AddRefed<CookieServiceChild> GetSingleton();

  RefPtr<GenericPromise> TrackCookieLoad(nsIChannel* aChannel);

  mozilla::ipc::IPCResult RecvRemoveCookie(const CookieStruct& aCookie,
                                           const OriginAttributes& aAttrs,
                                           const Maybe<nsID>& aOperationID);

  mozilla::ipc::IPCResult RecvAddCookie(const CookieStruct& aCookie,
                                        const OriginAttributes& aAttrs,
                                        const Maybe<nsID>& aOperationID);

 private:
  ~CookieServiceChild();

  enum class CookieNotificationAction {
    NoActionNeeded,
    CookieAdded,
    CookieChanged,
    CookieDeleted,
  };

  CookieNotificationAction RecordDocumentCookie(Cookie* aCookie,
                                                const OriginAttributes& aAttrs);

  void NotifyObservers(Cookie* aCookie, const OriginAttributes& aAttrs,
                       CookieNotificationAction aAction,
                       const Maybe<nsID>& aOperationID = Nothing());

  static bool RequireThirdPartyCheck(nsILoadInfo* aLoadInfo);

  mozilla::ipc::IPCResult RecvTrackCookiesLoad(
      nsTArray<CookieStructTable>&& aCookiesListTable);

  mozilla::ipc::IPCResult RecvRemoveAll();

  mozilla::ipc::IPCResult RecvRemoveBatchDeletedCookies(
      nsTArray<CookieStruct>&& aCookiesList,
      nsTArray<OriginAttributes>&& aAttrsList);

  void RemoveSingleCookie(const CookieStruct& aCookie,
                          const OriginAttributes& aAttrs,
                          const Maybe<nsID>& aOperationID);

  CookiesMap mCookiesMap;
  nsCOMPtr<mozIThirdPartyUtil> mThirdPartyUtil;
  nsCOMPtr<nsIEffectiveTLDService> mTLDService;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_CookieServiceChild_h_
