/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCSPService.h"

#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/PolicyContainer.h"
#include "nsCOMPtr.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsIContent.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIProtocolHandler.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsString.h"

using namespace mozilla;

static LazyLogModule gCspPRLog("CSP");

CSPService::CSPService() = default;

CSPService::~CSPService() = default;

NS_IMPL_ISUPPORTS(CSPService, nsIContentPolicy)

static bool SubjectToCSP(nsILoadInfo* aLoadInfo, nsIURI* aURI,
                         nsContentPolicyType aContentType) {
  ExtContentPolicyType contentType =
      nsContentUtils::InternalContentPolicyTypeToExternal(aContentType);

  // These content types are not subject to CSP content policy checks:
  // TYPE_CSP_REPORT -- csp can't block csp reports
  // TYPE_DOCUMENT   -- used for frame-ancestors
  if (contentType == ExtContentPolicy::TYPE_CSP_REPORT ||
      contentType == ExtContentPolicy::TYPE_DOCUMENT) {
    return false;
  }

  // The three protocols: data:, blob: and filesystem: share the same
  // protocol flag (URI_IS_LOCAL_RESOURCE) with other protocols,
  // but those three protocols get special attention in CSP and
  // are subject to CSP, hence we have to make sure those
  // protocols are subject to CSP, see:
  // http://www.w3.org/TR/CSP2/#source-list-guid-matching
  if (aURI->SchemeIs("data") || aURI->SchemeIs("blob") ||
      aURI->SchemeIs("filesystem")) {
    return true;
  }

  // For resources that will be used with a system principal we don't want to
  // exempt any protocols from being subject to the CSP.
  // TODO(bug 1945838): Extend this to all content types, not just scripts.
  if (contentType == ExtContentPolicyType::TYPE_SCRIPT) {
    if (BasePrincipal::Cast(aLoadInfo->GetLoadingPrincipal())
            ->IsSystemPrincipal()) {
      return true;
    }
  }

  // Finally we have to allowlist "about:" which does not fall into
  // the category underneath and also "javascript:" which is not
  // subject to CSP content loading rules.
  if (aURI->SchemeIs("about") || aURI->SchemeIs("javascript")) {
    return false;
  }

  // Please note that it should be possible for websites to
  // allowlist their own protocol handlers with respect to CSP,
  // hence we use protocol flags to accomplish that, but we also
  // want resource:, chrome: and moz-icon to be subject to CSP
  // (which also use URI_IS_LOCAL_RESOURCE).
  // Exception to the rule are images, styles, and localization
  // DTDs using a scheme of resource: or chrome:
  bool isImgOrStyleOrDTD = contentType == ExtContentPolicy::TYPE_IMAGE ||
                           contentType == ExtContentPolicy::TYPE_STYLESHEET ||
                           contentType == ExtContentPolicy::TYPE_DTD;
  if (aURI->SchemeIs("resource")) {
    nsAutoCString uriSpec;
    aURI->GetSpec(uriSpec);
    // Exempt pdf.js from being subject to a page's CSP.
    if (StringBeginsWith(uriSpec, "resource://pdf.js/"_ns)) {
      return false;
    }
    // Exempt the script used by the top-level VideoDocument.
    if (contentType == ExtContentPolicyType::TYPE_SCRIPT &&
        uriSpec.EqualsLiteral(
            "resource://content-accessible/TopLevelVideoDocument.js")) {
      return false;
    }
    if (!isImgOrStyleOrDTD) {
      return true;
    }
  }
  if (aURI->SchemeIs("chrome")) {
    nsAutoCString uriSpec;
    aURI->GetSpec(uriSpec);
    // Treat chrome: scripts loaded by pdf.js like resource://pdf.js/ scripts.
    if (contentType == ExtContentPolicyType::TYPE_SCRIPT &&
        nsContentUtils::IsPDFJS(aLoadInfo->GetLoadingPrincipal())) {
      return false;
    }
    if (!isImgOrStyleOrDTD) {
      return true;
    }
  }
  if (aURI->SchemeIs("moz-icon") || aURI->SchemeIs("moz-src")) {
    return true;
  }
  bool match;
  nsresult rv = NS_URIChainHasFlags(
      aURI, nsIProtocolHandler::URI_IS_LOCAL_RESOURCE, &match);
  if (NS_SUCCEEDED(rv) && match) {
    return false;
  }
  // all other protocols are subject To CSP.
  return true;
}

/* static */ nsresult CSPService::ConsultCSP(nsIURI* aContentLocation,
                                             nsILoadInfo* aLoadInfo,
                                             int16_t* aDecision) {
  if (!aContentLocation) {
    return NS_ERROR_FAILURE;
  }

  nsContentPolicyType contentType = aLoadInfo->InternalContentPolicyType();

  nsCOMPtr<nsICSPEventListener> cspEventListener;
  nsresult rv =
      aLoadInfo->GetCspEventListener(getter_AddRefs(cspEventListener));
  NS_ENSURE_SUCCESS(rv, rv);

  if (MOZ_LOG_TEST(gCspPRLog, LogLevel::Debug)) {
    MOZ_LOG(gCspPRLog, LogLevel::Debug,
            ("CSPService::ShouldLoad called for %s",
             aContentLocation->GetSpecOrDefault().get()));
  }

  // default decision, CSP can revise it if there's a policy to enforce
  *aDecision = nsIContentPolicy::ACCEPT;

  // No need to continue processing the CSP if the load should *not* be subject
  // to CSP. Please note, the correct way to opt-out of CSP using a custom
  // protocolHandler is to set one of the nsIProtocolHandler flags
  // that are allowlistet in subjectToCSP()
  if (!SubjectToCSP(aLoadInfo, aContentLocation, contentType)) {
    return NS_OK;
  }

  // 1) Apply speculate CSP for preloads
  bool isPreload = nsContentUtils::IsPreloadType(contentType);

  if (isPreload) {
    nsCOMPtr<nsIContentSecurityPolicy> preloadCsp = aLoadInfo->GetPreloadCsp();
    if (preloadCsp) {
      // obtain the enforcement decision
      rv = preloadCsp->ShouldLoad(
          contentType, cspEventListener, aLoadInfo, aContentLocation,
          nullptr,  // no redirect, aOriginal URL is null.
          false, aDecision);
      NS_ENSURE_SUCCESS(rv, rv);

      // if the preload policy already denied the load, then there
      // is no point in checking the real policy
      if (NS_CP_REJECTED(*aDecision)) {
        NS_SetRequestBlockingReason(
            aLoadInfo, nsILoadInfo::BLOCKING_REASON_CONTENT_POLICY_PRELOAD);

        return NS_OK;
      }
    }
  }

  // 2) Apply actual CSP to all loads. Please note that in case
  // the csp should be overruled (e.g. by an ExpandedPrincipal)
  // then loadinfo->GetCsp() returns that CSP instead of the
  // document's CSP.
  nsCOMPtr<nsIPolicyContainer> policyContainer =
      aLoadInfo->GetPolicyContainer();
  nsCOMPtr<nsIContentSecurityPolicy> csp =
      PolicyContainer::GetCSP(policyContainer);

  if (csp) {
    rv = csp->ShouldLoad(
        contentType, cspEventListener, aLoadInfo, aContentLocation,
        /* aOriginalURIIfRedirect */ nullptr,
        !isPreload && aLoadInfo->GetSendCSPViolationEvents(), aDecision);

    if (NS_CP_REJECTED(*aDecision)) {
      NS_SetRequestBlockingReason(
          aLoadInfo, nsILoadInfo::BLOCKING_REASON_CONTENT_POLICY_GENERAL);
    }

    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

/* nsIContentPolicy implementation */
NS_IMETHODIMP
CSPService::ShouldLoad(nsIURI* aContentLocation, nsILoadInfo* aLoadInfo,
                       int16_t* aDecision) {
  return ConsultCSP(aContentLocation, aLoadInfo, aDecision);
}

NS_IMETHODIMP
CSPService::ShouldProcess(nsIURI* aContentLocation, nsILoadInfo* aLoadInfo,
                          int16_t* aDecision) {
  if (!aContentLocation) {
    return NS_ERROR_FAILURE;
  }
  nsContentPolicyType contentType = aLoadInfo->InternalContentPolicyType();

  if (MOZ_LOG_TEST(gCspPRLog, LogLevel::Debug)) {
    MOZ_LOG(gCspPRLog, LogLevel::Debug,
            ("CSPService::ShouldProcess called for %s",
             aContentLocation->GetSpecOrDefault().get()));
  }

  // ShouldProcess is only relevant to TYPE_OBJECT, so let's convert the
  // internal contentPolicyType to the mapping external one.
  // If it is not TYPE_OBJECT, we can return at this point.
  // Note that we should still pass the internal contentPolicyType
  // (contentType) to ShouldLoad().
  ExtContentPolicyType policyType =
      nsContentUtils::InternalContentPolicyTypeToExternal(contentType);

  if (policyType != ExtContentPolicy::TYPE_OBJECT) {
    *aDecision = nsIContentPolicy::ACCEPT;
    return NS_OK;
  }

  return ShouldLoad(aContentLocation, aLoadInfo, aDecision);
}

nsresult CSPService::ConsultCSPForRedirect(nsIURI* aOriginalURI,
                                           nsIURI* aNewURI,
                                           nsILoadInfo* aLoadInfo,
                                           uint32_t aFlags,
                                           Maybe<nsresult>& aCancelCode) {
  // No need to continue processing if CSP is disabled or if the protocol
  // is *not* subject to CSP.
  // Please note, the correct way to opt-out of CSP using a custom
  // protocolHandler is to set one of the nsIProtocolHandler flags
  // that are allowlistet in subjectToCSP()
  nsContentPolicyType policyType = aLoadInfo->InternalContentPolicyType();
  if (!SubjectToCSP(aLoadInfo, aNewURI, policyType)) {
    return NS_OK;
  }

  nsCOMPtr<nsICSPEventListener> cspEventListener;
  nsresult rv =
      aLoadInfo->GetCspEventListener(getter_AddRefs(cspEventListener));
  MOZ_ALWAYS_SUCCEEDS(rv);

  bool isPreload = nsContentUtils::IsPreloadType(policyType);

  /* On redirect, if the content policy is a preload type, rejecting the
   * preload results in the load silently failing, so we pass true to
   * the aSendViolationReports parameter. See Bug 1219453.
   */

  int16_t decision = nsIContentPolicy::ACCEPT;

  // 1) Apply speculative CSP for preloads
  if (isPreload) {
    nsCOMPtr<nsIContentSecurityPolicy> preloadCsp = aLoadInfo->GetPreloadCsp();
    if (preloadCsp) {
      // Pass  originalURI to indicate the redirect
      preloadCsp->ShouldLoad(
          policyType,  // load type per nsIContentPolicy (uint32_t)
          cspEventListener, aLoadInfo,
          aNewURI,       // nsIURI
          aOriginalURI,  // Original nsIURI
          true,          // aSendViolationReports
          &decision);

      // if the preload policy already denied the load, then there
      // is no point in checking the real policy
      if (NS_CP_REJECTED(decision)) {
        aCancelCode = Some(NS_ERROR_DOM_BAD_URI);
        return NS_BINDING_FAILED;
      }
    }
  }

  // 2) Apply actual CSP to all loads
  nsCOMPtr<nsIPolicyContainer> policyContainer =
      aLoadInfo->GetPolicyContainer();
  nsCOMPtr<nsIContentSecurityPolicy> csp =
      PolicyContainer::GetCSP(policyContainer);
  if (csp) {
    // Pass  originalURI to indicate the redirect
    csp->ShouldLoad(policyType,  // load type per nsIContentPolicy (uint32_t)
                    cspEventListener, aLoadInfo,
                    aNewURI,       // nsIURI
                    aOriginalURI,  // Original nsIURI
                    true,          // aSendViolationReports
                    &decision);
    if (NS_CP_REJECTED(decision)) {
      aCancelCode = Some(NS_ERROR_DOM_BAD_URI);
      return NS_BINDING_FAILED;
    }
  }

  return NS_OK;
}
