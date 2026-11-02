/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieNotification.h"

#include "mozilla/dom/BrowsingContext.h"
#include "nsICookieNotification.h"

namespace mozilla::net {

NS_IMETHODIMP
CookieNotification::GetAction(nsICookieNotification::Action* aResult) {
  *aResult = mAction;
  return NS_OK;
}

NS_IMETHODIMP
CookieNotification::GetCookie(nsICookie** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  *aResult = mCookie;
  NS_IF_ADDREF(*aResult);

  return NS_OK;
}

NS_IMETHODIMP CookieNotification::GetBaseDomain(nsACString& aBaseDomain) {
  aBaseDomain = mBaseDomain;

  return NS_OK;
}

NS_IMETHODIMP CookieNotification::GetIsThirdParty(bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  *aResult = mIsThirdParty;
  return NS_OK;
}

NS_IMETHODIMP
CookieNotification::GetBatchDeletedCookies(nsIArray** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  NS_ENSURE_TRUE(mAction == nsICookieNotification::COOKIES_BATCH_DELETED,
                 NS_ERROR_NOT_AVAILABLE);

  *aResult = mBatchDeletedCookies;
  NS_IF_ADDREF(*aResult);

  return NS_OK;
}

NS_IMETHODIMP
CookieNotification::GetBrowsingContextId(uint64_t* aResult) {
  *aResult = mBrowsingContextId;
  return NS_OK;
}

NS_IMETHODIMP
CookieNotification::GetBrowsingContext(dom::BrowsingContext** aResult) {
  *aResult = dom::BrowsingContext::Get(mBrowsingContextId).take();
  return NS_OK;
}

NS_IMETHODIMP
CookieNotification::GetOperationID(nsID** aOperationID) {
  NS_ENSURE_ARG_POINTER(aOperationID);
  *aOperationID = mOperationID ? mOperationID->Clone() : nullptr;
  return NS_OK;
}

NS_IMPL_ISUPPORTS(CookieNotification, nsICookieNotification)

}  // namespace mozilla::net
