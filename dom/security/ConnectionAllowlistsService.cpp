/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ConnectionAllowlistsService.h"

#include "ConnectionAllowlists.h"
#include "PolicyContainer.h"
#include "mozilla/RefPtr.h"
#include "nsCOMPtr.h"
#include "nsIChannelEventSink.h"
#include "nsILoadInfo.h"
#include "nsNetUtil.h"

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(ConnectionAllowlistsService, nsIContentPolicy)

NS_IMETHODIMP
ConnectionAllowlistsService::ShouldLoad(nsIURI* aContentLocation,
                                        nsILoadInfo* aLoadInfo,
                                        int16_t* aDecision) {
  *aDecision = nsIContentPolicy::ACCEPT;

  nsCOMPtr<nsIPolicyContainer> policyContainer =
      aLoadInfo->GetPolicyContainer();
  RefPtr<ConnectionAllowlists> allowlists =
      PolicyContainer::GetConnectionAllowlists(policyContainer);
  if (!allowlists) {
    return NS_OK;
  }

  if (!allowlists->ShouldLoad(aContentLocation, aLoadInfo)) {
    *aDecision = nsIContentPolicy::REJECT_REQUEST;
    NS_SetRequestBlockingReason(
        aLoadInfo, nsILoadInfo::BLOCKING_REASON_CONNECTION_ALLOWLIST);
  }

  return NS_OK;
}

NS_IMETHODIMP
ConnectionAllowlistsService::ShouldProcess(nsIURI* aContentLocation,
                                           nsILoadInfo* aLoadInfo,
                                           int16_t* aDecision) {
  *aDecision = nsIContentPolicy::ACCEPT;
  return NS_OK;
}

/* static */
nsresult ConnectionAllowlistsService::ConsultForRedirect(
    nsIURI* aOriginalURI, nsIURI* aNewURI, nsILoadInfo* aLoadInfo,
    uint32_t aFlags, Maybe<nsresult>& aCancelCode) {
  // Internal redirects (e.g. service worker interception, cache or auth
  // retries) and https: upgrades (HSTS, HTTPS RR, upgrade-insecure-requests,
  // mixed content, HTTPS-Only/First) don't append to the request's URL list,
  // so they aren't redirects for the Connection Allowlists. Fetch does the HSTS
  // and HTTPS RR upgrades after the check (step 10 of
  // https://fetch.spec.whatwg.org/#concept-main-fetch), and ShouldLoad already
  // matched the URL upgraded by upgrade-insecure-requests or mixed content.
  if (aFlags & (nsIChannelEventSink::REDIRECT_INTERNAL |
                nsIChannelEventSink::REDIRECT_STS_UPGRADE)) {
    return NS_OK;
  }

  nsCOMPtr<nsIPolicyContainer> policyContainer =
      aLoadInfo->GetPolicyContainer();
  RefPtr<ConnectionAllowlists> allowlists =
      PolicyContainer::GetConnectionAllowlists(policyContainer);
  if (!allowlists) {
    return NS_OK;
  }

  if (allowlists->ShouldBlockRedirect(aOriginalURI, aLoadInfo)) {
    NS_SetRequestBlockingReason(
        aLoadInfo, nsILoadInfo::BLOCKING_REASON_CONNECTION_ALLOWLIST);
    // Use the same status as a load blocked by a content policy, so that
    // e.g. the frame's error event fires for a blocked navigation.
    aCancelCode = Some(NS_ERROR_CONTENT_BLOCKED);
    return NS_BINDING_FAILED;
  }

  return NS_OK;
}

}  // namespace mozilla::dom
