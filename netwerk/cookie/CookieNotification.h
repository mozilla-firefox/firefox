/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_CookieNotification_h
#define mozilla_net_CookieNotification_h

#include "mozilla/Maybe.h"
#include "nsCOMPtr.h"
#include "nsIArray.h"
#include "nsICookie.h"
#include "nsICookieNotification.h"
#include "nsString.h"
#include "nsTArray.h"

namespace mozilla::net {

class CookieNotification final : public nsICookieNotification {
 public:
  // nsISupports
  NS_DECL_ISUPPORTS
  NS_DECL_NSICOOKIENOTIFICATION

  explicit CookieNotification(nsICookieNotification::Action aAction,
                              nsICookie* aCookie, const nsACString& aBaseDomain,
                              bool aIsThirdParty = false,
                              nsIArray* aBatchDeletedCookies = nullptr,
                              uint64_t aBrowsingContextId = 0,
                              const Maybe<nsID>& aOperationID = Nothing())
      : mAction(aAction),
        mCookie(aCookie),
        mBaseDomain(aBaseDomain),
        mIsThirdParty(aIsThirdParty),
        mBatchDeletedCookies(aBatchDeletedCookies),
        mBrowsingContextId(aBrowsingContextId),
        mOperationID(aOperationID) {}

 private:
  nsICookieNotification::Action mAction;
  nsCOMPtr<nsICookie> mCookie;
  nsCString mBaseDomain;
  bool mIsThirdParty;
  nsCOMPtr<nsIArray> mBatchDeletedCookies;
  uint64_t mBrowsingContextId = 0;
  Maybe<nsID> mOperationID;

  ~CookieNotification() = default;
};

}  // namespace mozilla::net

#endif  // mozilla_net_CookieNotification_h
