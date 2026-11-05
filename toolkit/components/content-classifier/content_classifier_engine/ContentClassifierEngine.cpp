/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "mozilla/ContentClassifierEngine.h"
#include "ContentClassifierFeatureUtils.h"
#include "ContentClassifierService.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "mozilla/net/UrlClassifierCommon.h"
#include "nsIEffectiveTLDService.h"
#include "nsNetUtil.h"
#include "nsContentUtils.h"
#include "mozilla/Components.h"

namespace mozilla {

namespace {

bool IsNonRecommendedAddonFromLoadInfo(nsILoadInfo* aLoadInfo) {
  MOZ_ASSERT(aLoadInfo);
  extensions::WebExtensionPolicy* policy =
      ContentClassifierFeatureUtils::GetAddonPolicyFromLoadInfo(aLoadInfo);
  return policy && !policy->HasRecommendedState();
}

}  // namespace

ContentClassifierEngineResult ContentClassifierEngine::CheckNetworkRequest(
    const ContentClassifierRequest& aRequest, bool aPreviouslyMatched,
    bool aMatchDocumentAsNetworkRequest) {
  if (!mEngine || !sInitializedETLDService) {
    return ContentClassifierEngineResult(NS_ERROR_NOT_INITIALIZED, mFeature);
  }

  if (!aRequest.mValid) {
    return ContentClassifierEngineResult(NS_ERROR_INVALID_ARG, mFeature);
  }

  bool matched = false;
  bool important = false;
  nsCString exception;

  // The document load type is exclusive to the top-level load.
  const bool topLevelDocumentLoad =
      aRequest.mRequestType.EqualsLiteral("document");

  const nsCString* sourceHostname = &aRequest.mSourceHostname;
  bool thirdParty = aRequest.mThirdPartyToSource;
  if (topLevelDocumentLoad) {
    sourceHostname = &aRequest.mHostname;
    thirdParty = false;
  } else if (mFeature.mUseTopWindowAsSource) {
    sourceHostname = &aRequest.mTopWindowHostname;
    thirdParty = aRequest.mThirdParty;
  }

  // Untyped filter rules never match the document type on their own, so when
  // asked, present a top-level document load to the engine as an untyped
  // network request. So we can check whether the top-level document load
  // matches any filtering rules.
  constexpr auto kOtherRequestType = "other"_ns;
  const nsACString& requestType =
      aMatchDocumentAsNetworkRequest && topLevelDocumentLoad
          ? static_cast<const nsACString&>(kOtherRequestType)
          : static_cast<const nsACString&>(aRequest.mRequestType);

  nsresult rv = content_classifier_engine_check_network_request_preparsed(
      mEngine, &aRequest.mUrl, &aRequest.mHostname, sourceHostname,
      &requestType, thirdParty, aPreviouslyMatched, &matched, &important,
      &exception);
  return ContentClassifierEngineResult(matched, !exception.IsEmpty(), important,
                                       rv, mFeature);
}

ContentClassifierRequest::ContentClassifierRequest(nsIChannel* aChannel)
    : mThirdParty(true), mThirdPartyToSource(true), mValid(false) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv = aChannel->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv) || !uri) return;

  rv = uri->GetSpec(mUrl);
  if (NS_FAILED(rv)) return;

  nsCOMPtr<nsILoadInfo> loadInfo;
  rv = aChannel->GetLoadInfo(getter_AddRefs(loadInfo));
  if (NS_FAILED(rv)) return;

  mPassiveContent = net::ChannelClassifierUtils::IsPassiveContent(aChannel);

  ExtContentPolicyType contentPolicyType =
      loadInfo->GetExternalContentPolicyType();
  switch (contentPolicyType) {
    case ExtContentPolicyType::TYPE_CSP_REPORT:
      mRequestType.AssignLiteral("csp_report");
      break;
    case ExtContentPolicyType::TYPE_DOCUMENT:
      mRequestType.AssignLiteral("document");
      break;
    case ExtContentPolicyType::TYPE_FONT:
      mRequestType.AssignLiteral("font");
      break;
    case ExtContentPolicyType::TYPE_IMAGE:
    case ExtContentPolicyType::TYPE_IMAGESET:
      mRequestType.AssignLiteral("image");
      break;
    case ExtContentPolicyType::TYPE_MEDIA:
      mRequestType.AssignLiteral("media");
      break;
    case ExtContentPolicyType::TYPE_OBJECT:
      mRequestType.AssignLiteral("object");
      break;
    case ExtContentPolicyType::TYPE_BEACON:
    case ExtContentPolicyType::TYPE_PING:
      mRequestType.AssignLiteral("ping");
      break;
    case ExtContentPolicyType::TYPE_SCRIPT:
      mRequestType.AssignLiteral("script");
      break;
    case ExtContentPolicyType::TYPE_STYLESHEET:
      mRequestType.AssignLiteral("stylesheet");
      break;
    case ExtContentPolicyType::TYPE_SUBDOCUMENT:
      mRequestType.AssignLiteral("subdocument");
      break;
    case ExtContentPolicyType::TYPE_WEBSOCKET:
      mRequestType.AssignLiteral("websocket");
      break;
    case ExtContentPolicyType::TYPE_XMLHTTPREQUEST:
      mRequestType.AssignLiteral("xmlhttprequest");
      break;
    default:
      mRequestType.AssignLiteral("other");
      break;
  }
  mPrivateBrowsing = NS_UsePrivateBrowsing(aChannel);

  mIsNonRecommendedAddon = IsNonRecommendedAddonFromLoadInfo(loadInfo);
  mAddonMayLoad = net::UrlClassifierCommon::AddonMayLoad(aChannel, uri);

  // Unwrap nested URI schemes (jar:, view-source:, ...) before looking
  // at the host, mirroring AsyncUrlChannelClassifier::FeatureData::
  // InitializeList which calls NS_GetInnermostURI on the channel URI
  // and then GetHost on that.
  nsCOMPtr<nsIURI> innermostURI = NS_GetInnermostURI(uri);
  if (!innermostURI) return;

  nsCString host;
  rv = innermostURI->GetHost(host);
  if (NS_FAILED(rv)) return;
  // The filtering rules expect brackets of an IPv6 literal. We ensure the
  // brackets when getting the host.
  mHostname = host;
  nsContentUtils::MaybeFixIPv6Host(mHostname);

  nsCOMPtr<nsIEffectiveTLDService> eTLDService =
      components::EffectiveTLD::Service();
  if (!eTLDService) return;

  rv = eTLDService->GetSchemelessSiteFromHost(host, mSchemelessSite);
  if (NS_FAILED(rv)) return;

  // A top-level document load is its own top window.
  if (contentPolicyType == ExtContentPolicyType::TYPE_DOCUMENT) {
    mTopWindowHostname = mHostname;
    mTopWindowSchemelessSite = mSchemelessSite;
    mSourceHostname = mHostname;
    mSourceSchemelessSite = mSchemelessSite;
    mThirdParty = false;
    mThirdPartyToSource = false;
    mValid = true;
    return;
  }

  // Top-window schemeless site is the outermost page's site, used by
  // features whose mUseTopWindowAsSource flag is set.
  nsCOMPtr<nsIURI> topWindowURI;
  net::UrlClassifierCommon::GetTopWindowURIWithFallback(
      aChannel, getter_AddRefs(topWindowURI));
  if (topWindowURI) {
    nsCOMPtr<nsIURI> innermostTopURI = NS_GetInnermostURI(topWindowURI);
    if (innermostTopURI) {
      nsCString topHost;
      if (NS_SUCCEEDED(innermostTopURI->GetHost(topHost))) {
        mTopWindowHostname = topHost;
        nsContentUtils::MaybeFixIPv6Host(mTopWindowHostname);
        rv = eTLDService->GetSchemelessSiteFromHost(topHost,
                                                    mTopWindowSchemelessSite);
        if (NS_FAILED(rv)) {
          mTopWindowSchemelessSite.Truncate();
        }
      }
    }
  }

  // Source schemeless site is the loading frame's site (the immediate
  // initiator of the request), not the outermost top-window.
  nsCOMPtr<nsIPrincipal> loadingPrincipal = loadInfo->GetLoadingPrincipal();
  if (loadingPrincipal) {
    // A system or an expanded principal has neither a host nor a base domain,
    // and both appear on legitimate loads, such as the expanded principal a
    // content script's fetch carries. Classify those with an empty source
    // site rather than dropping the request; any other failure is unexpected
    // and leaves the request invalid.
    if (loadingPrincipal->IsSystemPrincipal() ||
        loadingPrincipal->GetIsExpandedPrincipal()) {
      mSourceHostname.Truncate();
      mSourceSchemelessSite.Truncate();
    } else {
      rv = nsContentUtils::GetHostOrIPv6WithBrackets(loadingPrincipal,
                                                     mSourceHostname);
      if (NS_FAILED(rv)) return;

      rv = loadingPrincipal->GetBaseDomain(mSourceSchemelessSite);
      if (NS_FAILED(rv)) return;
    }
  }

  // Third-party-ness is the precomputed BrowsingContext flag the
  // production AsyncUrl features consult
  // (UrlClassifierFeatureTrackingProtection.cpp uses
  // GetIsThirdPartyContextToTopWindow), not the cookie/storage-aware
  // mozIThirdPartyUtil check.
  mThirdParty = loadInfo->GetIsThirdPartyContextToTopWindow();
  mThirdPartyToSource = !mSchemelessSite.Equals(mSourceSchemelessSite);

  mValid = true;
}

static bool IsValidRequestType(const nsACString& aRequestType) {
  return aRequestType.EqualsLiteral("csp_report") ||
         aRequestType.EqualsLiteral("document") ||
         aRequestType.EqualsLiteral("font") ||
         aRequestType.EqualsLiteral("image") ||
         aRequestType.EqualsLiteral("media") ||
         aRequestType.EqualsLiteral("object") ||
         aRequestType.EqualsLiteral("ping") ||
         aRequestType.EqualsLiteral("script") ||
         aRequestType.EqualsLiteral("stylesheet") ||
         aRequestType.EqualsLiteral("subdocument") ||
         aRequestType.EqualsLiteral("websocket") ||
         aRequestType.EqualsLiteral("xmlhttprequest") ||
         aRequestType.EqualsLiteral("other");
}

ContentClassifierRequest::ContentClassifierRequest(
    const nsACString& aUrl, const nsACString& aSourceUrl,
    const nsACString& aTopWindowUrl, const nsACString& aRequestType,
    bool aPrivateBrowsing, bool aForceThirdPartyToTop,
    bool aIsNonRecommendedAddon)
    : mRequestType(aRequestType),
      mThirdParty(false),
      mThirdPartyToSource(true),
      mPrivateBrowsing(aPrivateBrowsing),
      mValid(false),
      mIsNonRecommendedAddon(aIsNonRecommendedAddon),
      mPassiveContent(aRequestType.EqualsLiteral("image") ||
                      aRequestType.EqualsLiteral("media")) {
  if (!IsValidRequestType(aRequestType)) return;

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aUrl);
  if (NS_FAILED(rv)) return;

  rv = uri->GetSpec(mUrl);
  if (NS_FAILED(rv)) return;

  nsCString host;
  rv = uri->GetHost(host);
  if (NS_FAILED(rv)) return;
  mHostname = host;
  nsContentUtils::MaybeFixIPv6Host(mHostname);

  nsCOMPtr<nsIEffectiveTLDService> eTLDService =
      components::EffectiveTLD::Service();
  if (!eTLDService) return;

  rv = eTLDService->GetSchemelessSiteFromHost(host, mSchemelessSite);
  if (NS_FAILED(rv)) return;

  if (!aSourceUrl.IsEmpty()) {
    nsCOMPtr<nsIURI> sourceUri;
    rv = NS_NewURI(getter_AddRefs(sourceUri), aSourceUrl);
    if (NS_FAILED(rv)) return;

    nsCString sourceHost;
    rv = sourceUri->GetHost(sourceHost);
    if (NS_FAILED(rv)) return;
    mSourceHostname = sourceHost;
    nsContentUtils::MaybeFixIPv6Host(mSourceHostname);

    rv = eTLDService->GetSchemelessSiteFromHost(sourceHost,
                                                mSourceSchemelessSite);
    if (NS_FAILED(rv)) return;

    mThirdPartyToSource = !mSchemelessSite.Equals(mSourceSchemelessSite);
  }

  if (!aTopWindowUrl.IsEmpty()) {
    nsCOMPtr<nsIURI> topUri;
    rv = NS_NewURI(getter_AddRefs(topUri), aTopWindowUrl);
    if (NS_FAILED(rv)) return;

    nsCString topHost;
    rv = topUri->GetHost(topHost);
    if (NS_FAILED(rv)) return;
    mTopWindowHostname = topHost;
    nsContentUtils::MaybeFixIPv6Host(mTopWindowHostname);

    rv = eTLDService->GetSchemelessSiteFromHost(topHost,
                                                mTopWindowSchemelessSite);
    if (NS_FAILED(rv)) {
      mTopWindowSchemelessSite.Truncate();
      return;
    }

    mThirdParty = !mSchemelessSite.Equals(mTopWindowSchemelessSite);
  }

  if (aForceThirdPartyToTop) {
    mThirdParty = true;
  }

  mValid = true;
}

}  // namespace mozilla
