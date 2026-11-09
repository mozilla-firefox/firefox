/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BounceTrackingSite.h"

#include "BounceTrackingProtectionStorage.h"
#include "BounceTrackingState.h"
#include "nsIEffectiveTLDService.h"
#include "nsIPrincipal.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"

namespace mozilla {

BounceTrackingSite::BounceTrackingSite(
    const nsACString& aSiteHost, const OriginAttributes& aOriginAttributes)
    : mSiteHost(aSiteHost) {
  if (!IsNull()) {
    mOriginAttributes =
        BounceTrackingProtectionStorage::NormalizeOriginAttributes(
            aOriginAttributes);
  }
}

// static
Result<BounceTrackingSite, nsresult> BounceTrackingSite::FromPrincipal(
    nsIPrincipal* aPrincipal) {
  NS_ENSURE_TRUE(aPrincipal, Err(NS_ERROR_INVALID_ARG));

  if (!BounceTrackingState::ShouldTrackPrincipal(aPrincipal)) {
    return BounceTrackingSite();
  }

  nsAutoCString siteHost;
  nsresult rv = aPrincipal->GetBaseDomain(siteHost);
  NS_ENSURE_SUCCESS(rv, Err(rv));
  NS_WARNING_ASSERTION(!siteHost.IsEmpty(), "Tracked principal has no site.");

  return BounceTrackingSite(siteHost, aPrincipal->OriginAttributesRef());
}

// static
Result<BounceTrackingSite, nsresult> BounceTrackingSite::FromURI(
    nsIURI* aURI, const OriginAttributes& aOriginAttributes) {
  NS_ENSURE_TRUE(aURI, Err(NS_ERROR_INVALID_ARG));

  if (!net::SchemeIsHttpOrHttps(aURI)) {
    return BounceTrackingSite();
  }

  nsresult rv;
  nsCOMPtr<nsIEffectiveTLDService> tldService =
      do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, Err(rv));

  nsAutoCString siteHost;
  if (NS_FAILED(tldService->GetSchemelessSite(aURI, siteHost))) {
    return BounceTrackingSite();
  }
  MOZ_ASSERT(!siteHost.IsEmpty(), "siteHost should not be empty.");

  return BounceTrackingSite(siteHost, aOriginAttributes);
}

// static
BounceTrackingSite BounceTrackingSite::FromSiteHost(
    const nsACString& aSiteHost, const OriginAttributes& aOriginAttributes) {
  return BounceTrackingSite(aSiteHost, aOriginAttributes);
}

}  // namespace mozilla
