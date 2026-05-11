/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/FontLoaderUtils.h"

#include "gfxUserFontSet.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "nsCOMPtr.h"
#include "nsIChannel.h"
#include "nsIClassOfService.h"
#include "nsIContentPolicy.h"
#include "nsIHttpChannel.h"
#include "nsILoadInfo.h"
#include "nsIReferrerInfo.h"
#include "nsISupportsPriority.h"
#include "nsIURI.h"
#include "nsNetUtil.h"

namespace mozilla {

/* static */ void FontLoaderUtils::BuildChannelFlags(
    nsIURI* aURI, bool aIsPreload,
    nsContentSecurityManager::CORSSecurityMapping& aCorsMapping,
    nsSecurityFlags& aSecurityFlags, nsContentPolicyType& aContentPolicyType) {
  // aCORSMode is ignored.  We always load as crossorigin=anonymous, but a
  // preload started with anything other then "anonymous" will never be found.
  aCorsMapping =
      aURI->SchemeIs("file") || aURI->SchemeIs("resource") ||
              aURI->SchemeIs("chrome")
          ? nsContentSecurityManager::CORSSecurityMapping::
                CORS_NONE_MAPS_TO_INHERITED_CONTEXT
          : nsContentSecurityManager::CORSSecurityMapping::REQUIRE_CORS_CHECKS;

  // Besides the CORS flags, include SEC_ALLOW_CHROME to allow a font to come
  // from a resource:// or chrome:// URL (as long as it is marked
  // contentaccessible)
  aSecurityFlags = nsContentSecurityManager::ComputeSecurityFlags(
                       CORSMode::CORS_NONE, aCorsMapping) |
                   nsILoadInfo::SEC_ALLOW_CHROME;

  aContentPolicyType = aIsPreload ? nsIContentPolicy::TYPE_INTERNAL_FONT_PRELOAD
                                  : nsIContentPolicy::TYPE_FONT;
}

/* static */ nsresult FontLoaderUtils::BuildChannelSetup(
    nsIChannel* aChannel, nsIHttpChannel* aHttpChannel,
    nsIReferrerInfo* aReferrerInfo, const gfxFontFaceSrc* aFontFaceSrc,
    int32_t aSupportsPriorityValue) {
  if (aHttpChannel) {
    nsresult rv = aHttpChannel->SetRequestHeader(
        "Accept"_ns,
        "application/font-woff2;q=1.0,application/font-woff;q=0.9,*/*;q=0.8"_ns,
        false);
    NS_ENSURE_SUCCESS(rv, rv);

    if (aReferrerInfo) {
      rv = aHttpChannel->SetReferrerInfoWithoutClone(aReferrerInfo);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    } else {
      MOZ_ASSERT(aFontFaceSrc);

      rv = aHttpChannel->SetReferrerInfo(aFontFaceSrc->mReferrerInfo);
      (void)NS_WARN_IF(NS_FAILED(rv));

      // For WOFF and WOFF2, we should tell servers/proxies/etc NOT to try
      // and apply additional compression at the content-encoding layer
      if (aFontFaceSrc->mFormatHint == StyleFontFaceSourceFormatKeyword::Woff ||
          aFontFaceSrc->mFormatHint ==
              StyleFontFaceSourceFormatKeyword::Woff2) {
        rv = aHttpChannel->SetRequestHeader("Accept-Encoding"_ns, "identity"_ns,
                                            false);
        NS_ENSURE_SUCCESS(rv, rv);
      }
    }
  }

  nsCOMPtr<nsISupportsPriority> priorityChannel(do_QueryInterface(aChannel));
  if (priorityChannel) {
    priorityChannel->SetPriority(aSupportsPriorityValue);
  }
  nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(aChannel));
  if (cos) {
    cos->AddClassFlags(nsIClassOfService::TailForbidden);
  }

  return NS_OK;
}

// static
nsresult FontLoaderUtils::BuildChannel(
    nsIChannel** aChannel, nsIURI* aURI, const CORSMode aCORSMode,
    const dom::ReferrerPolicy& aReferrerPolicy,
    gfxUserFontEntry* aUserFontEntry, const gfxFontFaceSrc* aFontFaceSrc,
    dom::Document* aDocument, nsILoadGroup* aLoadGroup,
    nsIInterfaceRequestor* aCallbacks, bool aIsPreload,
    int32_t aSupportsPriorityValue) {
  nsresult rv;

  nsIPrincipal* principal =
      aUserFontEntry ? (aUserFontEntry->GetPrincipal()
                            ? aUserFontEntry->GetPrincipal()->NodePrincipal()
                            : nullptr)
                     : aDocument->NodePrincipal();

  nsContentSecurityManager::CORSSecurityMapping corsMapping;
  nsSecurityFlags securityFlags;
  nsContentPolicyType contentPolicyType;
  BuildChannelFlags(aURI, aIsPreload, corsMapping, securityFlags,
                    contentPolicyType);

  nsCOMPtr<nsIChannel> channel;
  // Note we are calling NS_NewChannelWithTriggeringPrincipal() with both a
  // node and a principal.  This is because the document where the font is
  // being loaded might have a different origin from the principal of the
  // stylesheet that initiated the font load.
  rv = NS_NewChannelWithTriggeringPrincipal(getter_AddRefs(channel), aURI,
                                            aDocument, principal, securityFlags,
                                            contentPolicyType,
                                            nullptr,  // PerformanceStorage
                                            aLoadGroup);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
  nsCOMPtr<nsIReferrerInfo> referrerInfo;
  if (httpChannel && !aFontFaceSrc) {
    referrerInfo = MakeAndAddRef<dom::ReferrerInfo>(
        aDocument->GetDocumentURIAsReferrer(), aReferrerPolicy);
    rv = httpChannel->SetReferrerInfoWithoutClone(referrerInfo);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  rv = BuildChannelSetup(channel, httpChannel, referrerInfo, aFontFaceSrc,
                         aSupportsPriorityValue);
  NS_ENSURE_SUCCESS(rv, rv);

  channel.forget(aChannel);
  return NS_OK;
}

// static
nsresult FontLoaderUtils::BuildChannel(
    nsIChannel** aChannel, nsIURI* aURI, const CORSMode aCORSMode,
    const dom::ReferrerPolicy& aReferrerPolicy,
    gfxUserFontEntry* aUserFontEntry, const gfxFontFaceSrc* aFontFaceSrc,
    dom::WorkerPrivate* aWorkerPrivate, nsILoadGroup* aLoadGroup,
    nsIInterfaceRequestor* aCallbacks) {
  nsresult rv;

  nsIPrincipal* principal =
      aUserFontEntry ? (aUserFontEntry->GetPrincipal()
                            ? aUserFontEntry->GetPrincipal()->NodePrincipal()
                            : nullptr)
                     : aWorkerPrivate->GetPrincipal();

  nsContentSecurityManager::CORSSecurityMapping corsMapping;
  nsSecurityFlags securityFlags;
  nsContentPolicyType contentPolicyType;
  BuildChannelFlags(aURI, /* aIsPreload */ false, corsMapping, securityFlags,
                    contentPolicyType);

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannelWithTriggeringPrincipal(
      getter_AddRefs(channel), aURI, aWorkerPrivate->GetLoadingPrincipal(),
      principal, securityFlags, contentPolicyType, nullptr, nullptr,
      aLoadGroup);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));

  nsCOMPtr<nsIReferrerInfo> referrerInfo;
  if (httpChannel && !aFontFaceSrc) {
    referrerInfo =
        static_cast<dom::ReferrerInfo*>(aWorkerPrivate->GetReferrerInfo())
            ->CloneWithNewPolicy(aReferrerPolicy);
  }

  rv = BuildChannelSetup(channel, httpChannel, referrerInfo, aFontFaceSrc,
                         nsISupportsPriority::PRIORITY_HIGH);
  NS_ENSURE_SUCCESS(rv, rv);

  channel.forget(aChannel);
  return NS_OK;
}

}  // namespace mozilla
