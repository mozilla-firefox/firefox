/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/LoadInfo.h"

#include "js/Array.h"               // JS::NewArrayObject
#include "js/PropertyAndElement.h"  // JS_DefineElement
#include "mozilla/Assertions.h"
#include "mozilla/ExpandedPrincipal.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ClientSource.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/DOMTypes.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/dom/InternalRequest.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozIThirdPartyUtil.h"
#include "ThirdPartyUtil.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIDocShell.h"
#include "mozilla/dom/Document.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIScriptElement.h"
#include "nsISupportsImpl.h"
#include "nsISupportsUtils.h"
#include "nsIXPConnect.h"
#include "nsDocShell.h"
#include "nsGlobalWindowInner.h"
#include "nsMixedContentBlocker.h"
#include "nsQueryObject.h"
#include "nsRedirectHistoryEntry.h"
#include "nsSandboxFlags.h"
#include "nsICookieService.h"

using namespace mozilla::dom;

namespace mozilla::net {

static nsCString CurrentRemoteType() {
  MOZ_ASSERT(XRE_IsParentProcess() || XRE_IsContentProcess());
  if (ContentChild* cc = ContentChild::GetSingleton()) {
    return nsCString(cc->GetRemoteType());
  }
  return NOT_REMOTE_TYPE;
}

static nsContentPolicyType InternalContentPolicyTypeForFrame(
    CanonicalBrowsingContext* aBrowsingContext) {
  const auto& maybeEmbedderElementType =
      aBrowsingContext->GetEmbedderElementType();
  MOZ_ASSERT(maybeEmbedderElementType.isSome());
  auto embedderElementType = maybeEmbedderElementType.value();

  // Assign same type as in nsDocShell::DetermineContentType.
  // N.B. internal content policy type will never be TYPE_DOCUMENT
  return embedderElementType.EqualsLiteral("iframe")
             ? nsIContentPolicy::TYPE_INTERNAL_IFRAME
             : nsIContentPolicy::TYPE_INTERNAL_FRAME;
}

/* static */ already_AddRefed<LoadInfo> LoadInfo::CreateForDocument(
    dom::CanonicalBrowsingContext* aBrowsingContext, nsIURI* aURI,
    nsIPrincipal* aTriggeringPrincipal, const nsACString& aTriggeringRemoteType,
    const OriginAttributes& aOriginAttributes, nsSecurityFlags aSecurityFlags,
    uint32_t aSandboxFlags) {
  return MakeAndAddRef<LoadInfo>(aBrowsingContext, aURI, aTriggeringPrincipal,
                                 aTriggeringRemoteType, aOriginAttributes,
                                 aSecurityFlags, aSandboxFlags);
}

/* static */ already_AddRefed<LoadInfo> LoadInfo::CreateForFrame(
    dom::CanonicalBrowsingContext* aBrowsingContext,
    nsIPrincipal* aTriggeringPrincipal, const nsACString& aTriggeringRemoteType,
    nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags) {
  return MakeAndAddRef<LoadInfo>(aBrowsingContext, aTriggeringPrincipal,
                                 aTriggeringRemoteType, aSecurityFlags,
                                 aSandboxFlags);
}

/* static */ already_AddRefed<LoadInfo> LoadInfo::CreateForNonDocument(
    dom::WindowGlobalParent* aParentWGP, nsIPrincipal* aTriggeringPrincipal,
    nsContentPolicyType aContentPolicyType, nsSecurityFlags aSecurityFlags,
    uint32_t aSandboxFlags) {
  return MakeAndAddRef<LoadInfo>(
      aParentWGP, aTriggeringPrincipal, aParentWGP->GetRemoteType(),
      aContentPolicyType, aSecurityFlags, aSandboxFlags);
}

LoadInfo::LoadInfo(
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    nsINode* aLoadingContext, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType,
    const Maybe<mozilla::dom::ClientInfo>& aLoadingClientInfo,
    const Maybe<mozilla::dom::ServiceWorkerDescriptor>& aController,
    uint32_t aSandboxFlags)
    : mLoadingPrincipal(aLoadingContext ? aLoadingContext->NodePrincipal()
                                        : aLoadingPrincipal),
      mTriggeringPrincipal(aTriggeringPrincipal ? aTriggeringPrincipal
                                                : mLoadingPrincipal.get()),
      mTriggeringRemoteType(CurrentRemoteType()),
      mSandboxedNullPrincipalID(nsID::GenerateUUID()),
      mClientInfo(aLoadingClientInfo),
      mController(aController),
      mLoadingContext(do_GetWeakReference(aLoadingContext)),
      mSecurityFlags(aSecurityFlags),
      mSandboxFlags(aSandboxFlags),
      mInternalContentPolicyType(aContentPolicyType) {
  MOZ_ASSERT(mLoadingPrincipal);
  MOZ_ASSERT(mTriggeringPrincipal);

#ifdef DEBUG
  // TYPE_DOCUMENT loads initiated by javascript tests will go through
  // nsIOService and use the wrong constructor.  Don't enforce the
  // !TYPE_DOCUMENT check in those cases
  bool skipContentTypeCheck = false;
  skipContentTypeCheck =
      Preferences::GetBool("network.loadinfo.skip_type_assertion");
#endif

  // This constructor shouldn't be used for TYPE_DOCUMENT loads that don't
  // have a loadingPrincipal
  MOZ_ASSERT(skipContentTypeCheck || mLoadingPrincipal ||
             mInternalContentPolicyType != nsIContentPolicy::TYPE_DOCUMENT);

  // We should only get an explicit controller for subresource requests.
  MOZ_DIAGNOSTIC_ASSERT(aController.isNothing() ||
                        !nsContentUtils::IsNonSubresourceInternalPolicyType(
                            mInternalContentPolicyType));

  // TODO(bug 1259873): Above, we initialize mIsThirdPartyContext to false
  // meaning that consumers of LoadInfo that don't pass a context or pass a
  // context from which we can't find a window will default to assuming that
  // they're 1st party. It would be nice if we could default "safe" and assume
  // that we are 3rd party until proven otherwise.

  // if consumers pass both, aLoadingContext and aLoadingPrincipal
  // then the loadingPrincipal must be the same as the node's principal
  MOZ_ASSERT(!aLoadingContext || !aLoadingPrincipal ||
             aLoadingContext->NodePrincipal() == aLoadingPrincipal);

  // if the load is sandboxed, we can not also inherit the principal
  if (mSandboxFlags & SANDBOXED_ORIGIN) {
    mForceInheritPrincipalDropped =
        (mSecurityFlags & nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL);
    mSecurityFlags &= ~nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
  }

  ExtContentPolicyType externalType =
      nsContentUtils::InternalContentPolicyTypeToExternal(aContentPolicyType);

  if (aLoadingContext) {
    // Ensure that all network requests for a window client have the ClientInfo
    // properly set.  Workers must currently pass the loading ClientInfo
    // explicitly. We allow main thread requests to explicitly pass the value as
    // well.
    if (mClientInfo.isNothing()) {
      mClientInfo = aLoadingContext->OwnerDoc()->GetClientInfo();
    }

    // For subresource loads set the service worker based on the calling
    // context's controller.  Workers must currently pass the controller in
    // explicitly.  We allow main thread requests to explicitly pass the value
    // as well, but otherwise extract from the loading context here.
    if (mController.isNothing() &&
        !nsContentUtils::IsNonSubresourceInternalPolicyType(
            mInternalContentPolicyType)) {
      mController = aLoadingContext->OwnerDoc()->GetController();
    }

    nsCOMPtr<nsPIDOMWindowOuter> contextOuter =
        aLoadingContext->OwnerDoc()->GetWindow();
    if (contextOuter) {
      ComputeIsThirdPartyContext(contextOuter);
      RefPtr<dom::BrowsingContext> bc = contextOuter->GetBrowsingContext();
      MOZ_ASSERT(bc);
      mBrowsingContextID = bc->Id();

      nsGlobalWindowInner* innerWindow =
          nsGlobalWindowInner::Cast(contextOuter->GetCurrentInnerWindow());
      if (innerWindow) {
        mTopLevelPrincipal = innerWindow->GetTopLevelAntiTrackingPrincipal();

        if (!mTopLevelPrincipal &&
            externalType == ExtContentPolicy::TYPE_SUBDOCUMENT && bc->IsTop()) {
          // If this is the first level iframe, innerWindow is our top-level
          // principal.
          mTopLevelPrincipal = innerWindow->GetPrincipal();
        }
      }

      // Let's inherit the cookie behavior and permission from the parent
      // document.
      mCookieJarSettings = aLoadingContext->OwnerDoc()->CookieJarSettings();
    }

    mInnerWindowID = aLoadingContext->OwnerDoc()->InnerWindowID();
    RefPtr<WindowContext> ctx = WindowContext::GetById(mInnerWindowID);
    if (ctx) {
      mLoadingEmbedderPolicy = ctx->GetEmbedderPolicy();
    }
    mDocumentHasUserInteracted =
        aLoadingContext->OwnerDoc()->UserHasInteracted();

    // Inherit HTTPS-Only Mode flags from parent document.
    mHttpsOnlyStatus |= nsHTTPSOnlyUtils::GetStatusForSubresourceLoad(
        aLoadingContext->OwnerDoc()->HttpsOnlyStatus());

    // When the element being loaded is a frame, we choose the frame's window
    // for the window ID and the frame element's window as the parent
    // window. This is the behavior that Chrome exposes to add-ons.
    // NB: If the frameLoaderOwner doesn't have a frame loader, then the load
    // must be coming from an object (such as a plugin) that's loaded into it
    // instead of a document being loaded. In that case, treat this object like
    // any other non-document-loading element.
    RefPtr<nsFrameLoaderOwner> frameLoaderOwner =
        do_QueryObject(aLoadingContext);
    RefPtr<nsFrameLoader> fl =
        frameLoaderOwner ? frameLoaderOwner->GetFrameLoader() : nullptr;
    if (fl) {
      nsCOMPtr<nsIDocShell> docShell = fl->GetDocShell(IgnoreErrors());
      if (docShell) {
        nsCOMPtr<nsPIDOMWindowOuter> outerWindow = do_GetInterface(docShell);
        if (outerWindow) {
          RefPtr<dom::BrowsingContext> bc = outerWindow->GetBrowsingContext();
          mFrameBrowsingContextID = bc ? bc->Id() : 0;
        }
      }
    }

    // if the document forces all mixed content to be blocked, then we
    // store that bit for all requests on the loadinfo.
    mBlockAllMixedContent =
        aLoadingContext->OwnerDoc()->GetBlockAllMixedContent(false) ||
        (nsContentUtils::IsPreloadType(mInternalContentPolicyType) &&
         aLoadingContext->OwnerDoc()->GetBlockAllMixedContent(true));

    if (mLoadingPrincipal && BasePrincipal::Cast(mTriggeringPrincipal)
                                 ->OverridesCSP(mLoadingPrincipal)) {
      // if the load is triggered by an addon which potentially overrides the
      // CSP of the document, then do not force insecure requests to be
      // upgraded.
      mUpgradeInsecureRequests = false;
    } else {
      // if the document forces all requests to be upgraded from http to https,
      // then we should do that for all requests. If it only forces preloads to
      // be upgraded then we should enforce upgrade insecure requests only for
      // preloads.
      mUpgradeInsecureRequests =
          aLoadingContext->OwnerDoc()->GetUpgradeInsecureRequests(false) ||
          (nsContentUtils::IsPreloadType(mInternalContentPolicyType) &&
           aLoadingContext->OwnerDoc()->GetUpgradeInsecureRequests(true));
    }

    if (nsMixedContentBlocker::IsUpgradableContentType(
            mInternalContentPolicyType, /* aConsiderPrefs */ false)) {
      // Check the load is within a secure context but ignore loopback URLs
      if (mLoadingPrincipal->GetIsOriginPotentiallyTrustworthy() &&
          !mLoadingPrincipal->GetIsLoopbackHost()) {
        if (nsMixedContentBlocker::IsUpgradableContentType(
                mInternalContentPolicyType, /* aConsiderPrefs */ true)) {
          mBrowserUpgradeInsecureRequests = true;
        } else {
          mBrowserWouldUpgradeInsecureRequests = true;
        }
      }
    }
  }
  mOriginAttributes = mLoadingPrincipal->OriginAttributesRef();

  // We need to do this after inheriting the document's origin attributes
  // above, in case the loading principal ends up being the system principal.
  if (aLoadingContext) {
    nsCOMPtr<nsILoadContext> loadContext =
        aLoadingContext->OwnerDoc()->GetLoadContext();
    nsCOMPtr<nsIDocShell> docShell = aLoadingContext->OwnerDoc()->GetDocShell();
    if (loadContext && docShell &&
        docShell->GetBrowsingContext()->IsContent()) {
      bool usePrivateBrowsing;
      nsresult rv = loadContext->GetUsePrivateBrowsing(&usePrivateBrowsing);
      if (NS_SUCCEEDED(rv)) {
        mOriginAttributes.SyncAttributesWithPrivateBrowsing(usePrivateBrowsing);
      }
    }

    if (!loadContext) {
      // Things like svg documents being used as images don't have a load
      // context or a docshell, in that case try to inherit private browsing
      // from the documents channel (which is how we determine which imgLoader
      // is used).
      nsCOMPtr<nsIChannel> channel = aLoadingContext->OwnerDoc()->GetChannel();
      if (channel) {
        mOriginAttributes.SyncAttributesWithPrivateBrowsing(
            NS_UsePrivateBrowsing(channel));
      }
    }

    // For chrome docshell, the mPrivateBrowsingId remains 0 even its
    // UsePrivateBrowsing() is true, so we only update the mPrivateBrowsingId in
    // origin attributes if the type of the docshell is content.
    MOZ_ASSERT(!docShell || !docShell->GetBrowsingContext()->IsChrome() ||
                   mOriginAttributes.mPrivateBrowsingId == 0,
               "chrome docshell shouldn't have mPrivateBrowsingId set.");
  }
}

/* Constructor takes an outer window, but no loadingNode or loadingPrincipal.
 * This constructor should only be used for TYPE_DOCUMENT loads, since they
 * have a null loadingNode and loadingPrincipal.
 */
LoadInfo::LoadInfo(nsPIDOMWindowOuter* aOuterWindow, nsIURI* aURI,
                   nsIPrincipal* aTriggeringPrincipal,
                   nsISupports* aContextForTopLevelLoad,
                   nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags)
    : mTriggeringPrincipal(aTriggeringPrincipal),
      mTriggeringRemoteType(CurrentRemoteType()),
      mSandboxedNullPrincipalID(nsID::GenerateUUID()),
      mContextForTopLevelLoad(do_GetWeakReference(aContextForTopLevelLoad)),
      mSecurityFlags(aSecurityFlags),
      mSandboxFlags(aSandboxFlags),
      mInternalContentPolicyType(nsIContentPolicy::TYPE_DOCUMENT) {
  // Top-level loads are never third-party
  // Grab the information we can out of the window.
  MOZ_ASSERT(aOuterWindow);
  MOZ_ASSERT(mTriggeringPrincipal);

  // if the load is sandboxed, we can not also inherit the principal
  if (mSandboxFlags & SANDBOXED_ORIGIN) {
    mForceInheritPrincipalDropped =
        (mSecurityFlags & nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL);
    mSecurityFlags &= ~nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
  }

  RefPtr<BrowsingContext> bc = aOuterWindow->GetBrowsingContext();
  mBrowsingContextID = bc ? bc->Id() : 0;

  // This should be removed in bug 1618557
  nsGlobalWindowInner* innerWindow =
      nsGlobalWindowInner::Cast(aOuterWindow->GetCurrentInnerWindow());
  if (innerWindow) {
    mTopLevelPrincipal = innerWindow->GetTopLevelAntiTrackingPrincipal();
  }

  // get the docshell from the outerwindow, and then get the originattributes
  nsCOMPtr<nsIDocShell> docShell = aOuterWindow->GetDocShell();
  MOZ_ASSERT(docShell);
  mOriginAttributes = nsDocShell::Cast(docShell)->GetOriginAttributes();

  // We sometimes use this constructor for security checks for outer windows
  // that aren't top level.
  if (aSecurityFlags != nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK) {
    MOZ_ASSERT(aOuterWindow->GetBrowsingContext()->IsTop());
  }

#ifdef DEBUG
  if (docShell->GetBrowsingContext()->IsChrome()) {
    MOZ_ASSERT(mOriginAttributes.mPrivateBrowsingId == 0,
               "chrome docshell shouldn't have mPrivateBrowsingId set.");
  }
#endif

  // Let's take the current cookie behavior and current cookie permission
  // for the documents' loadInfo. Note that for any other loadInfos,
  // cookieBehavior will be BEHAVIOR_REJECT for security reasons.
  bool isPrivate = mOriginAttributes.IsPrivateBrowsing();
  bool shouldResistFingerprinting =
      nsContentUtils::ShouldResistFingerprinting_dangerous(
          aURI, mOriginAttributes,
          "We are creating CookieJarSettings, so we can't have one already.",
          RFPTarget::IsAlwaysEnabledForPrecompute);
  mCookieJarSettings = CookieJarSettings::Create(
      isPrivate ? CookieJarSettings::ePrivate : CookieJarSettings::eRegular,
      shouldResistFingerprinting);
}

LoadInfo::LoadInfo(dom::CanonicalBrowsingContext* aBrowsingContext,
                   nsIURI* aURI, nsIPrincipal* aTriggeringPrincipal,
                   const nsACString& aTriggeringRemoteType,
                   const OriginAttributes& aOriginAttributes,
                   nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags)
    : mTriggeringPrincipal(aTriggeringPrincipal),
      mTriggeringRemoteType(aTriggeringRemoteType),
      mSandboxedNullPrincipalID(nsID::GenerateUUID()),
      mSecurityFlags(aSecurityFlags),
      mSandboxFlags(aSandboxFlags),
      mInternalContentPolicyType(nsIContentPolicy::TYPE_DOCUMENT) {
  // Top-level loads are never third-party
  // Grab the information we can out of the window.
  MOZ_ASSERT(aBrowsingContext);
  MOZ_ASSERT(mTriggeringPrincipal);
  MOZ_ASSERT(aSecurityFlags !=
             nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK);

  // if the load is sandboxed, we can not also inherit the principal
  if (mSandboxFlags & SANDBOXED_ORIGIN) {
    mForceInheritPrincipalDropped =
        (mSecurityFlags & nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL);
    mSecurityFlags &= ~nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
  }

  mBrowsingContextID = aBrowsingContext->Id();
  mOriginAttributes = aOriginAttributes;

#ifdef DEBUG
  if (aBrowsingContext->IsChrome()) {
    MOZ_ASSERT(mOriginAttributes.mPrivateBrowsingId == 0,
               "chrome docshell shouldn't have mPrivateBrowsingId set.");
  }
#endif

  // This code path can be taken when loading an about:blank document, which
  // means we might think that we should be exempted from resist fingerprinting.
  // If we think that, we should defer to any opener, if it is present. If the
  // opener is also exempted, then it continues to be exempted. Regardless of
  // what ShouldRFP says, we _also_ need to propagate any RandomizationKey we
  // have.
  bool shouldResistFingerprinting =
      nsContentUtils::ShouldResistFingerprinting_dangerous(
          aURI, mOriginAttributes,
          "We are creating CookieJarSettings, so we can't have one already.",
          RFPTarget::IsAlwaysEnabledForPrecompute);

  nsresult rv = NS_ERROR_NOT_AVAILABLE;
  nsTArray<uint8_t> randomKey;
  RefPtr<BrowsingContext> opener = aBrowsingContext->GetOpener();
  if (opener) {
    MOZ_ASSERT(opener->GetCurrentWindowContext());
    if (opener->GetCurrentWindowContext()) {
      shouldResistFingerprinting |=
          opener->GetCurrentWindowContext()->ShouldResistFingerprinting();
    }

    // In the parent, we need to get the CJS from the CanonicalBrowsingContext's
    // WindowGlobalParent If we're in the child, we probably have a reference to
    // the opener's document, and can get it from there.
    if (XRE_IsParentProcess()) {
      MOZ_ASSERT(opener->Canonical()->GetCurrentWindowGlobal());
      if (opener->Canonical()->GetCurrentWindowGlobal()) {
        MOZ_ASSERT(
            opener->Canonical()->GetCurrentWindowGlobal()->CookieJarSettings());
        rv = opener->Canonical()
                 ->GetCurrentWindowGlobal()
                 ->CookieJarSettings()
                 ->GetFingerprintingRandomizationKey(randomKey);
      }
    } else if (opener->GetDocument()) {
      MOZ_ASSERT(false, "Code is in child");
      rv = opener->GetDocument()
               ->CookieJarSettings()
               ->GetFingerprintingRandomizationKey(randomKey);
    }
  }

  const bool isPrivate = mOriginAttributes.IsPrivateBrowsing();

  // Let's take the current cookie behavior and current cookie permission
  // for the documents' loadInfo. Note that for any other loadInfos,
  // cookieBehavior will be BEHAVIOR_REJECT for security reasons.
  mCookieJarSettings = CookieJarSettings::Create(
      isPrivate ? CookieJarSettings::ePrivate : CookieJarSettings::eRegular,
      shouldResistFingerprinting);

  if (NS_SUCCEEDED(rv)) {
    net::CookieJarSettings::Cast(mCookieJarSettings)
        ->SetFingerprintingRandomizationKey(randomKey);
  }
}

LoadInfo::LoadInfo(dom::WindowGlobalParent* aParentWGP,
                   nsIPrincipal* aTriggeringPrincipal,
                   const nsACString& aTriggeringRemoteType,
                   nsContentPolicyType aContentPolicyType,
                   nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags)
    : mTriggeringPrincipal(aTriggeringPrincipal),
      mTriggeringRemoteType(aTriggeringRemoteType),
      mSandboxedNullPrincipalID(nsID::GenerateUUID()),
      mSecurityFlags(aSecurityFlags),
      mSandboxFlags(aSandboxFlags),
      mInternalContentPolicyType(aContentPolicyType) {
  CanonicalBrowsingContext* parentBC = aParentWGP->BrowsingContext();
  MOZ_ASSERT(parentBC);
  ComputeAncestors(parentBC, mAncestorPrincipals, mAncestorBrowsingContextIDs);

  RefPtr<WindowGlobalParent> topLevelWGP = aParentWGP->TopWindowContext();

  // if the load is sandboxed, we can not also inherit the principal
  if (mSandboxFlags & SANDBOXED_ORIGIN) {
    mForceInheritPrincipalDropped =
        (mSecurityFlags & nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL);
    mSecurityFlags &= ~nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
  }

  // Ensure that all network requests for a window client have the ClientInfo
  // properly set.
  mClientInfo = aParentWGP->GetClientInfo();
  mLoadingPrincipal = aParentWGP->DocumentPrincipal();
  ComputeIsThirdPartyContext(aParentWGP);

  mBrowsingContextID = parentBC->Id();

  // Special treatment for resources injected by add-ons.
  if (aTriggeringPrincipal &&
      StaticPrefs::privacy_antitracking_isolateContentScriptResources() &&
      nsContentUtils::IsExpandedPrincipal(aTriggeringPrincipal)) {
    bool shouldResistFingerprinting =
        nsContentUtils::ShouldResistFingerprinting_dangerous(
            mLoadingPrincipal,
            "CookieJarSettings can't exist yet, we're creating it",
            RFPTarget::IsAlwaysEnabledForPrecompute);
    mCookieJarSettings = CookieJarSettings::Create(
        nsICookieService::BEHAVIOR_REJECT,
        StoragePrincipalHelper::PartitionKeyForExpandedPrincipal(
            aTriggeringPrincipal),
        OriginAttributes::IsFirstPartyEnabled(), false,
        shouldResistFingerprinting);
  }

  if (!mCookieJarSettings) {
    // Let's inherit the cookie behavior and permission from the embedder
    // document.
    mCookieJarSettings = aParentWGP->CookieJarSettings();
    if (topLevelWGP->BrowsingContext()->IsTop()) {
      if (mCookieJarSettings) {
        bool stopAtOurLevel = mCookieJarSettings->GetCookieBehavior() ==
                              nsICookieService::BEHAVIOR_REJECT_TRACKER;
        if (!stopAtOurLevel ||
            topLevelWGP->OuterWindowId() != aParentWGP->OuterWindowId()) {
          mTopLevelPrincipal = topLevelWGP->DocumentPrincipal();
        }
      }
    }
  }

  if (!mTopLevelPrincipal && parentBC->IsTop()) {
    // If this is the first level iframe, embedder WindowGlobalParent's document
    // principal is our top-level principal.
    mTopLevelPrincipal = aParentWGP->DocumentPrincipal();
  }

  mInnerWindowID = aParentWGP->InnerWindowId();
  mDocumentHasUserInteracted = aParentWGP->DocumentHasUserInteracted();

  // if the document forces all mixed content to be blocked, then we
  // store that bit for all requests on the loadinfo.
  mBlockAllMixedContent = aParentWGP->GetDocumentBlockAllMixedContent();

  if (mTopLevelPrincipal && BasePrincipal::Cast(mTriggeringPrincipal)
                                ->OverridesCSP(mTopLevelPrincipal)) {
    // if the load is triggered by an addon which potentially overrides the
    // CSP of the document, then do not force insecure requests to be
    // upgraded.
    mUpgradeInsecureRequests = false;
  } else {
    // if the document forces all requests to be upgraded from http to https,
    // then we should do that for all requests. If it only forces preloads to
    // be upgraded then we should enforce upgrade insecure requests only for
    // preloads.
    mUpgradeInsecureRequests = aParentWGP->GetDocumentUpgradeInsecureRequests();
  }
  mOriginAttributes = mLoadingPrincipal->OriginAttributesRef();

  // We need to do this after inheriting the document's origin attributes
  // above, in case the loading principal ends up being the system principal.
  if (parentBC->IsContent()) {
    mOriginAttributes.SyncAttributesWithPrivateBrowsing(
        parentBC->UsePrivateBrowsing());
  }

  // Inherit HTTPS-Only Mode flags from embedder document.
  mHttpsOnlyStatus |= nsHTTPSOnlyUtils::GetStatusForSubresourceLoad(
      aParentWGP->HttpsOnlyStatus());

  // For chrome BC, the mPrivateBrowsingId remains 0 even its
  // UsePrivateBrowsing() is true, so we only update the mPrivateBrowsingId in
  // origin attributes if the type of the BC is content.
  if (parentBC->IsChrome()) {
    MOZ_ASSERT(mOriginAttributes.mPrivateBrowsingId == 0,
               "chrome docshell shouldn't have mPrivateBrowsingId set.");
  }

  RefPtr<WindowContext> ctx = WindowContext::GetById(mInnerWindowID);
  if (ctx) {
    mLoadingEmbedderPolicy = ctx->GetEmbedderPolicy();

    if (Document* document = ctx->GetDocument()) {
      mIsOriginTrialCoepCredentiallessEnabledForTopLevel =
          document->Trials().IsEnabled(OriginTrial::CoepCredentialless);
    }
  }
}

// Used for TYPE_FRAME or TYPE_IFRAME load.
LoadInfo::LoadInfo(dom::CanonicalBrowsingContext* aBrowsingContext,
                   nsIPrincipal* aTriggeringPrincipal,
                   const nsACString& aTriggeringRemoteType,
                   nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags)
    : LoadInfo(aBrowsingContext->GetParentWindowContext(), aTriggeringPrincipal,
               aTriggeringRemoteType,
               InternalContentPolicyTypeForFrame(aBrowsingContext),
               aSecurityFlags, aSandboxFlags) {
  mFrameBrowsingContextID = aBrowsingContext->Id();
}

LoadInfo::LoadInfo(const LoadInfo& rhs)
    : mLoadingPrincipal(rhs.mLoadingPrincipal),
      mTriggeringPrincipal(rhs.mTriggeringPrincipal),
      mPrincipalToInherit(rhs.mPrincipalToInherit),
      mTopLevelPrincipal(rhs.mTopLevelPrincipal),
      mResultPrincipalURI(rhs.mResultPrincipalURI),
      mChannelCreationOriginalURI(rhs.mChannelCreationOriginalURI),
      mCookieJarSettings(rhs.mCookieJarSettings),
      mCspToInherit(rhs.mCspToInherit),
      mContainerFeaturePolicyInfo(rhs.mContainerFeaturePolicyInfo),
      mTriggeringRemoteType(rhs.mTriggeringRemoteType),
      mSandboxedNullPrincipalID(rhs.mSandboxedNullPrincipalID),
      mClientInfo(rhs.mClientInfo),
      // mReservedClientSource must be handled specially during redirect
      // mReservedClientInfo must be handled specially during redirect
      // mInitialClientInfo must be handled specially during redirect
      mController(rhs.mController),
      mPerformanceStorage(rhs.mPerformanceStorage),
      mLoadingContext(rhs.mLoadingContext),
      mContextForTopLevelLoad(rhs.mContextForTopLevelLoad),
      mSecurityFlags(rhs.mSecurityFlags),
      mSandboxFlags(rhs.mSandboxFlags),
      mTriggeringSandboxFlags(rhs.mTriggeringSandboxFlags),
      mTriggeringWindowId(rhs.mTriggeringWindowId),
      mTriggeringStorageAccess(rhs.mTriggeringStorageAccess),
      mInternalContentPolicyType(rhs.mInternalContentPolicyType),
      mTainting(rhs.mTainting),
      mBlockAllMixedContent(rhs.mBlockAllMixedContent),
      mUpgradeInsecureRequests(rhs.mUpgradeInsecureRequests),
      mBrowserUpgradeInsecureRequests(rhs.mBrowserUpgradeInsecureRequests),
      mBrowserDidUpgradeInsecureRequests(
          rhs.mBrowserDidUpgradeInsecureRequests),
      mBrowserWouldUpgradeInsecureRequests(
          rhs.mBrowserWouldUpgradeInsecureRequests),
      mForceAllowDataURI(rhs.mForceAllowDataURI),
      mAllowInsecureRedirectToDataURI(rhs.mAllowInsecureRedirectToDataURI),
      mSkipContentPolicyCheckForWebRequest(
          rhs.mSkipContentPolicyCheckForWebRequest),
      mOriginalFrameSrcLoad(rhs.mOriginalFrameSrcLoad),
      mForceInheritPrincipalDropped(rhs.mForceInheritPrincipalDropped),
      mInnerWindowID(rhs.mInnerWindowID),
      mBrowsingContextID(rhs.mBrowsingContextID),
      mWorkerAssociatedBrowsingContextID(
          rhs.mWorkerAssociatedBrowsingContextID),
      mFrameBrowsingContextID(rhs.mFrameBrowsingContextID),
      mInitialSecurityCheckDone(rhs.mInitialSecurityCheckDone),
      mIsThirdPartyContext(rhs.mIsThirdPartyContext),
      mIsThirdPartyContextToTopWindow(rhs.mIsThirdPartyContextToTopWindow),
      mIsOn3PCBExceptionList(rhs.mIsOn3PCBExceptionList),
      mIsFormSubmission(rhs.mIsFormSubmission),
      mIsGETRequest(rhs.mIsGETRequest),
      mSendCSPViolationEvents(rhs.mSendCSPViolationEvents),
      mOriginAttributes(rhs.mOriginAttributes),
      mRedirectChainIncludingInternalRedirects(
          rhs.mRedirectChainIncludingInternalRedirects.Clone()),
      mRedirectChain(rhs.mRedirectChain.Clone()),
      mAncestorPrincipals(rhs.mAncestorPrincipals.Clone()),
      mAncestorBrowsingContextIDs(rhs.mAncestorBrowsingContextIDs.Clone()),
      mCorsUnsafeHeaders(rhs.mCorsUnsafeHeaders.Clone()),
      mRequestBlockingReason(rhs.mRequestBlockingReason),
      mForcePreflight(rhs.mForcePreflight),
      mIsPreflight(rhs.mIsPreflight),
      mLoadTriggeredFromExternal(rhs.mLoadTriggeredFromExternal),
      mDocumentHasUserInteracted(rhs.mDocumentHasUserInteracted),
      mAllowListFutureDocumentsCreatedFromThisRedirectChain(
          rhs.mAllowListFutureDocumentsCreatedFromThisRedirectChain),
      mNeedForCheckingAntiTrackingHeuristic(
          rhs.mNeedForCheckingAntiTrackingHeuristic),
      mCspNonce(rhs.mCspNonce),
      mIntegrityMetadata(rhs.mIntegrityMetadata),
      mSkipContentSniffing(rhs.mSkipContentSniffing),
      mHttpsOnlyStatus(rhs.mHttpsOnlyStatus),
      mHstsStatus(rhs.mHstsStatus),
      mHasValidUserGestureActivation(rhs.mHasValidUserGestureActivation),
      mTextDirectiveUserActivation(rhs.mTextDirectiveUserActivation),
      mAllowDeprecatedSystemRequests(rhs.mAllowDeprecatedSystemRequests),
      mIsInDevToolsContext(rhs.mIsInDevToolsContext),
      mParserCreatedScript(rhs.mParserCreatedScript),
      mStoragePermission(rhs.mStoragePermission),
      mOverriddenFingerprintingSettings(rhs.mOverriddenFingerprintingSettings),
#ifdef DEBUG
      mOverriddenFingerprintingSettingsIsSet(
          rhs.mOverriddenFingerprintingSettingsIsSet),
#endif
      mIsMetaRefresh(rhs.mIsMetaRefresh),
      mIsFromProcessingFrameAttributes(rhs.mIsFromProcessingFrameAttributes),
      mIsMediaRequest(rhs.mIsMediaRequest),
      mIsMediaInitialRequest(rhs.mIsMediaInitialRequest),
      mIsFromObjectOrEmbed(rhs.mIsFromObjectOrEmbed),
      mLoadingEmbedderPolicy(rhs.mLoadingEmbedderPolicy),
      mIsOriginTrialCoepCredentiallessEnabledForTopLevel(
          rhs.mIsOriginTrialCoepCredentiallessEnabledForTopLevel),
      mUnstrippedURI(rhs.mUnstrippedURI),
      mInterceptionInfo(rhs.mInterceptionInfo),
      mHasInjectedCookieForCookieBannerHandling(
          rhs.mHasInjectedCookieForCookieBannerHandling),
      mSchemelessInput(rhs.mSchemelessInput),
      mHttpsUpgradeTelemetry(rhs.mHttpsUpgradeTelemetry),
      mIsNewWindowTarget(rhs.mIsNewWindowTarget) {
}

LoadInfo::LoadInfo(
    nsIPrincipal* aLoadingPrincipal, nsIPrincipal* aTriggeringPrincipal,
    nsIPrincipal* aPrincipalToInherit, nsIPrincipal* aTopLevelPrincipal,
    nsIURI* aResultPrincipalURI, nsICookieJarSettings* aCookieJarSettings,
    nsIContentSecurityPolicy* aCspToInherit,
    const nsACString& aTriggeringRemoteType,
    const nsID& aSandboxedNullPrincipalID, const Maybe<ClientInfo>& aClientInfo,
    const Maybe<ClientInfo>& aReservedClientInfo,
    const Maybe<ClientInfo>& aInitialClientInfo,
    const Maybe<ServiceWorkerDescriptor>& aController,
    nsSecurityFlags aSecurityFlags, uint32_t aSandboxFlags,
    uint32_t aTriggeringSandboxFlags, uint64_t aTriggeringWindowId,
    bool aTriggeringStorageAccess, nsContentPolicyType aContentPolicyType,
    LoadTainting aTainting, bool aBlockAllMixedContent,
    bool aUpgradeInsecureRequests, bool aBrowserUpgradeInsecureRequests,
    bool aBrowserDidUpgradeInsecureRequests,
    bool aBrowserWouldUpgradeInsecureRequests, bool aForceAllowDataURI,
    bool aAllowInsecureRedirectToDataURI,
    bool aSkipContentPolicyCheckForWebRequest, bool aOriginalFrameSrcLoad,
    bool aForceInheritPrincipalDropped, uint64_t aInnerWindowID,
    uint64_t aBrowsingContextID, uint64_t aFrameBrowsingContextID,
    bool aInitialSecurityCheckDone, bool aIsThirdPartyContext,
    const Maybe<bool>& aIsThirdPartyContextToTopWindow,
    bool aIsOn3PCBExceptionList, bool aIsFormSubmission, bool aIsGETRequest,
    bool aSendCSPViolationEvents, const OriginAttributes& aOriginAttributes,
    RedirectHistoryArray&& aRedirectChainIncludingInternalRedirects,
    RedirectHistoryArray&& aRedirectChain,
    nsTArray<nsCOMPtr<nsIPrincipal>>&& aAncestorPrincipals,
    const nsTArray<uint64_t>& aAncestorBrowsingContextIDs,
    const nsTArray<nsCString>& aCorsUnsafeHeaders, bool aForcePreflight,
    bool aIsPreflight, bool aLoadTriggeredFromExternal,
    bool aServiceWorkerTaintingSynthesized, bool aDocumentHasUserInteracted,
    bool aAllowListFutureDocumentsCreatedFromThisRedirectChain,
    bool aNeedForCheckingAntiTrackingHeuristic, const nsAString& aCspNonce,
    const nsAString& aIntegrityMetadata, bool aSkipContentSniffing,
    uint32_t aHttpsOnlyStatus, bool aHstsStatus,
    bool aHasValidUserGestureActivation, bool aTextDirectiveUserActivation,
    bool aIsSameDocumentNavigation, bool aAllowDeprecatedSystemRequests,
    bool aIsInDevToolsContext, bool aParserCreatedScript,
    nsILoadInfo::StoragePermissionState aStoragePermission,
    const Maybe<RFPTargetSet>& aOverriddenFingerprintingSettings,
    bool aIsMetaRefresh, uint32_t aRequestBlockingReason,
    nsINode* aLoadingContext,
    nsILoadInfo::CrossOriginEmbedderPolicy aLoadingEmbedderPolicy,
    bool aIsOriginTrialCoepCredentiallessEnabledForTopLevel,
    nsIURI* aUnstrippedURI, nsIInterceptionInfo* aInterceptionInfo,
    bool aHasInjectedCookieForCookieBannerHandling,
    nsILoadInfo::SchemelessInputType aSchemelessInput,
    nsILoadInfo::HTTPSUpgradeTelemetryType aHttpsUpgradeTelemetry,
    bool aIsNewWindowTarget,
    dom::UserNavigationInvolvement aUserNavigationInvolvement)
    : mLoadingPrincipal(aLoadingPrincipal),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mPrincipalToInherit(aPrincipalToInherit),
      mTopLevelPrincipal(aTopLevelPrincipal),
      mResultPrincipalURI(aResultPrincipalURI),
      mCookieJarSettings(aCookieJarSettings),
      mCspToInherit(aCspToInherit),
      mTriggeringRemoteType(aTriggeringRemoteType),
      mSandboxedNullPrincipalID(aSandboxedNullPrincipalID),
      mClientInfo(aClientInfo),
      mReservedClientInfo(aReservedClientInfo),
      mInitialClientInfo(aInitialClientInfo),
      mController(aController),
      mLoadingContext(do_GetWeakReference(aLoadingContext)),
      mSecurityFlags(aSecurityFlags),
      mSandboxFlags(aSandboxFlags),
      mTriggeringSandboxFlags(aTriggeringSandboxFlags),
      mTriggeringWindowId(aTriggeringWindowId),
      mTriggeringStorageAccess(aTriggeringStorageAccess),
      mInternalContentPolicyType(aContentPolicyType),
      mTainting(aTainting),
      mBlockAllMixedContent(aBlockAllMixedContent),
      mUpgradeInsecureRequests(aUpgradeInsecureRequests),
      mBrowserUpgradeInsecureRequests(aBrowserUpgradeInsecureRequests),
      mBrowserDidUpgradeInsecureRequests(aBrowserDidUpgradeInsecureRequests),
      mBrowserWouldUpgradeInsecureRequests(
          aBrowserWouldUpgradeInsecureRequests),
      mForceAllowDataURI(aForceAllowDataURI),
      mAllowInsecureRedirectToDataURI(aAllowInsecureRedirectToDataURI),
      mSkipContentPolicyCheckForWebRequest(
          aSkipContentPolicyCheckForWebRequest),
      mOriginalFrameSrcLoad(aOriginalFrameSrcLoad),
      mForceInheritPrincipalDropped(aForceInheritPrincipalDropped),
      mInnerWindowID(aInnerWindowID),
      mBrowsingContextID(aBrowsingContextID),
      mFrameBrowsingContextID(aFrameBrowsingContextID),
      mInitialSecurityCheckDone(aInitialSecurityCheckDone),
      mIsThirdPartyContext(aIsThirdPartyContext),
      mIsThirdPartyContextToTopWindow(aIsThirdPartyContextToTopWindow),
      mIsOn3PCBExceptionList(aIsOn3PCBExceptionList),
      mIsFormSubmission(aIsFormSubmission),
      mIsGETRequest(aIsGETRequest),
      mSendCSPViolationEvents(aSendCSPViolationEvents),
      mOriginAttributes(aOriginAttributes),
      mRedirectChainIncludingInternalRedirects(
          std::move(aRedirectChainIncludingInternalRedirects)),
      mRedirectChain(std::move(aRedirectChain)),
      mAncestorPrincipals(std::move(aAncestorPrincipals)),
      mAncestorBrowsingContextIDs(aAncestorBrowsingContextIDs.Clone()),
      mCorsUnsafeHeaders(aCorsUnsafeHeaders.Clone()),
      mRequestBlockingReason(aRequestBlockingReason),
      mForcePreflight(aForcePreflight),
      mIsPreflight(aIsPreflight),
      mLoadTriggeredFromExternal(aLoadTriggeredFromExternal),
      mServiceWorkerTaintingSynthesized(aServiceWorkerTaintingSynthesized),
      mDocumentHasUserInteracted(aDocumentHasUserInteracted),
      mAllowListFutureDocumentsCreatedFromThisRedirectChain(
          aAllowListFutureDocumentsCreatedFromThisRedirectChain),
      mNeedForCheckingAntiTrackingHeuristic(
          aNeedForCheckingAntiTrackingHeuristic),
      mCspNonce(aCspNonce),
      mIntegrityMetadata(aIntegrityMetadata),
      mSkipContentSniffing(aSkipContentSniffing),
      mHttpsOnlyStatus(aHttpsOnlyStatus),
      mHstsStatus(aHstsStatus),
      mHasValidUserGestureActivation(aHasValidUserGestureActivation),
      mTextDirectiveUserActivation(aTextDirectiveUserActivation),
      mIsSameDocumentNavigation(aIsSameDocumentNavigation),
      mAllowDeprecatedSystemRequests(aAllowDeprecatedSystemRequests),
      mIsInDevToolsContext(aIsInDevToolsContext),
      mParserCreatedScript(aParserCreatedScript),
      mStoragePermission(aStoragePermission),
      mOverriddenFingerprintingSettings(aOverriddenFingerprintingSettings),
      mIsMetaRefresh(aIsMetaRefresh),
      mLoadingEmbedderPolicy(aLoadingEmbedderPolicy),
      mIsOriginTrialCoepCredentiallessEnabledForTopLevel(
          aIsOriginTrialCoepCredentiallessEnabledForTopLevel),
      mUnstrippedURI(aUnstrippedURI),
      mInterceptionInfo(aInterceptionInfo),
      mHasInjectedCookieForCookieBannerHandling(
          aHasInjectedCookieForCookieBannerHandling),
      mSchemelessInput(aSchemelessInput),
      mHttpsUpgradeTelemetry(aHttpsUpgradeTelemetry),
      mUserNavigationInvolvement(aUserNavigationInvolvement),
      mIsNewWindowTarget(aIsNewWindowTarget) {
  // Only top level TYPE_DOCUMENT loads can have a null loadingPrincipal
  MOZ_ASSERT(mLoadingPrincipal ||
             aContentPolicyType == nsIContentPolicy::TYPE_DOCUMENT);
  MOZ_ASSERT(mTriggeringPrincipal);
}

// static
void LoadInfo::ComputeAncestors(
    CanonicalBrowsingContext* aBC,
    nsTArray<nsCOMPtr<nsIPrincipal>>& aAncestorPrincipals,
    nsTArray<uint64_t>& aBrowsingContextIDs) {
  MOZ_ASSERT(aAncestorPrincipals.IsEmpty());
  MOZ_ASSERT(aBrowsingContextIDs.IsEmpty());
  CanonicalBrowsingContext* ancestorBC = aBC;
  // Iterate over ancestor WindowGlobalParents, collecting principals and outer
  // window IDs.
  while (WindowGlobalParent* ancestorWGP =
             ancestorBC->GetParentWindowContext()) {
    ancestorBC = ancestorWGP->BrowsingContext();

    nsCOMPtr<nsIPrincipal> parentPrincipal = ancestorWGP->DocumentPrincipal();
    MOZ_ASSERT(parentPrincipal, "Ancestor principal is null");
    aAncestorPrincipals.AppendElement(parentPrincipal.forget());
    aBrowsingContextIDs.AppendElement(ancestorBC->Id());
  }
}
void LoadInfo::ComputeIsThirdPartyContext(nsPIDOMWindowOuter* aOuterWindow) {
  ExtContentPolicyType type =
      nsContentUtils::InternalContentPolicyTypeToExternal(
          mInternalContentPolicyType);
  if (type == ExtContentPolicy::TYPE_DOCUMENT) {
    // Top-level loads are never third-party.
    mIsThirdPartyContext = false;
    return;
  }

  nsCOMPtr<mozIThirdPartyUtil> util(do_GetService(THIRDPARTYUTIL_CONTRACTID));
  if (NS_WARN_IF(!util)) {
    return;
  }

  util->IsThirdPartyWindow(aOuterWindow, nullptr, &mIsThirdPartyContext);
}

void LoadInfo::ComputeIsThirdPartyContext(dom::WindowGlobalParent* aGlobal) {
  if (nsILoadInfo::GetExternalContentPolicyType() ==
      ExtContentPolicy::TYPE_DOCUMENT) {
    // Top-level loads are never third-party.
    mIsThirdPartyContext = false;
    return;
  }

  ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
  if (!thirdPartyUtil) {
    return;
  }
  thirdPartyUtil->IsThirdPartyGlobal(aGlobal, &mIsThirdPartyContext);
}

NS_IMPL_ISUPPORTS(LoadInfo, nsILoadInfo)

LoadInfo::~LoadInfo() { MOZ_RELEASE_ASSERT(NS_IsMainThread()); }

already_AddRefed<nsILoadInfo> LoadInfo::Clone() const {
  RefPtr<LoadInfo> copy(new LoadInfo(*this));
  return copy.forget();
}

already_AddRefed<nsILoadInfo> LoadInfo::CloneWithNewSecFlags(
    nsSecurityFlags aSecurityFlags) const {
  RefPtr<LoadInfo> copy(new LoadInfo(*this));
  copy->mSecurityFlags = aSecurityFlags;
  return copy.forget();
}

already_AddRefed<nsILoadInfo> LoadInfo::CloneForNewRequest() const {
  RefPtr<LoadInfo> copy(new LoadInfo(*this));
  copy->mInitialSecurityCheckDone = false;
  copy->mRedirectChainIncludingInternalRedirects.Clear();
  copy->mRedirectChain.Clear();
  copy->mResultPrincipalURI = nullptr;
  return copy.forget();
}

NS_IMETHODIMP
LoadInfo::GetLoadingPrincipal(nsIPrincipal** aLoadingPrincipal) {
  *aLoadingPrincipal = do_AddRef(mLoadingPrincipal).take();
  return NS_OK;
}

nsIPrincipal* LoadInfo::VirtualGetLoadingPrincipal() {
  return mLoadingPrincipal;
}

NS_IMETHODIMP
LoadInfo::GetTriggeringPrincipal(nsIPrincipal** aTriggeringPrincipal) {
  *aTriggeringPrincipal = do_AddRef(mTriggeringPrincipal).take();
  return NS_OK;
}

nsIPrincipal* LoadInfo::TriggeringPrincipal() { return mTriggeringPrincipal; }

NS_IMETHODIMP
LoadInfo::GetPrincipalToInherit(nsIPrincipal** aPrincipalToInherit) {
  *aPrincipalToInherit = do_AddRef(mPrincipalToInherit).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetPrincipalToInherit(nsIPrincipal* aPrincipalToInherit) {
  MOZ_ASSERT(aPrincipalToInherit, "must be a valid principal to inherit");
  mPrincipalToInherit = aPrincipalToInherit;
  return NS_OK;
}

nsIPrincipal* LoadInfo::PrincipalToInherit() { return mPrincipalToInherit; }

nsIPrincipal* LoadInfo::FindPrincipalToInherit(nsIChannel* aChannel) {
  if (mPrincipalToInherit) {
    return mPrincipalToInherit;
  }

  nsCOMPtr<nsIURI> uri = mResultPrincipalURI;
  if (!uri) {
    Unused << aChannel->GetOriginalURI(getter_AddRefs(uri));
  }

  auto* prin = BasePrincipal::Cast(mTriggeringPrincipal);
  return prin->PrincipalToInherit(uri);
}

const nsID& LoadInfo::GetSandboxedNullPrincipalID() {
  MOZ_ASSERT(!mSandboxedNullPrincipalID.Equals(nsID{}),
             "mSandboxedNullPrincipalID wasn't initialized?");
  return mSandboxedNullPrincipalID;
}

void LoadInfo::ResetSandboxedNullPrincipalID() {
  mSandboxedNullPrincipalID = nsID::GenerateUUID();
}

nsIPrincipal* LoadInfo::GetTopLevelPrincipal() { return mTopLevelPrincipal; }

NS_IMETHODIMP
LoadInfo::GetTriggeringRemoteType(nsACString& aTriggeringRemoteType) {
  aTriggeringRemoteType = mTriggeringRemoteType;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetTriggeringRemoteType(const nsACString& aTriggeringRemoteType) {
  mTriggeringRemoteType = aTriggeringRemoteType;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetLoadingDocument(Document** aResult) {
  if (nsCOMPtr<nsINode> node = do_QueryReferent(mLoadingContext)) {
    RefPtr<Document> context = node->OwnerDoc();
    context.forget(aResult);
  }
  return NS_OK;
}
NS_IMETHODIMP
LoadInfo::GetUserNavigationInvolvement(uint8_t* aUserNavigationInvolvement) {
  *aUserNavigationInvolvement = uint8_t(mUserNavigationInvolvement);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetUserNavigationInvolvement(uint8_t aUserNavigationInvolvement) {
  mUserNavigationInvolvement =
      dom::UserNavigationInvolvement(aUserNavigationInvolvement);
  return NS_OK;
}

nsINode* LoadInfo::LoadingNode() {
  nsCOMPtr<nsINode> node = do_QueryReferent(mLoadingContext);
  return node;
}

already_AddRefed<nsISupports> LoadInfo::ContextForTopLevelLoad() {
  // Most likely you want to query LoadingNode() instead of
  // ContextForTopLevelLoad() if this assertion fires.
  MOZ_ASSERT(mInternalContentPolicyType == nsIContentPolicy::TYPE_DOCUMENT,
             "should only query this context for top level document loads");
  nsCOMPtr<nsISupports> context = do_QueryReferent(mContextForTopLevelLoad);
  return context.forget();
}

already_AddRefed<nsISupports> LoadInfo::GetLoadingContext() {
  nsCOMPtr<nsISupports> context;
  if (mInternalContentPolicyType == nsIContentPolicy::TYPE_DOCUMENT) {
    context = ContextForTopLevelLoad();
  } else {
    context = LoadingNode();
  }
  return context.forget();
}

NS_IMETHODIMP
LoadInfo::GetLoadingContextXPCOM(nsISupports** aResult) {
  nsCOMPtr<nsISupports> context = GetLoadingContext();
  context.forget(aResult);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSecurityFlags(nsSecurityFlags* aResult) {
  *aResult = mSecurityFlags;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSandboxFlags(uint32_t* aResult) {
  *aResult = mSandboxFlags;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetTriggeringSandboxFlags(uint32_t* aResult) {
  *aResult = mTriggeringSandboxFlags;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetTriggeringSandboxFlags(uint32_t aFlags) {
  mTriggeringSandboxFlags = aFlags;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetTriggeringWindowId(uint64_t* aResult) {
  *aResult = mTriggeringWindowId;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetTriggeringWindowId(uint64_t aFlags) {
  mTriggeringWindowId = aFlags;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetTriggeringStorageAccess(bool* aResult) {
  *aResult = mTriggeringStorageAccess;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetTriggeringStorageAccess(bool aFlags) {
  mTriggeringStorageAccess = aFlags;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSecurityMode(uint32_t* aFlags) {
  *aFlags = (mSecurityFlags &
             (nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT |
              nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED |
              nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT |
              nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL |
              nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT));
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsInThirdPartyContext(bool* aIsInThirdPartyContext) {
  *aIsInThirdPartyContext = mIsThirdPartyContext;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsInThirdPartyContext(bool aIsInThirdPartyContext) {
  mIsThirdPartyContext = aIsInThirdPartyContext;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsThirdPartyContextToTopWindow(
    bool* aIsThirdPartyContextToTopWindow) {
  *aIsThirdPartyContextToTopWindow =
      mIsThirdPartyContextToTopWindow.valueOr(true);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsThirdPartyContextToTopWindow(
    bool aIsThirdPartyContextToTopWindow) {
  mIsThirdPartyContextToTopWindow = Some(aIsThirdPartyContextToTopWindow);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsOn3PCBExceptionList(bool* aIsOn3PCBExceptionList) {
  *aIsOn3PCBExceptionList = mIsOn3PCBExceptionList;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsOn3PCBExceptionList(bool aIsOn3PCBExceptionList) {
  mIsOn3PCBExceptionList = aIsOn3PCBExceptionList;
  return NS_OK;
}

static const uint32_t sCookiePolicyMask =
    nsILoadInfo::SEC_COOKIES_DEFAULT | nsILoadInfo::SEC_COOKIES_INCLUDE |
    nsILoadInfo::SEC_COOKIES_SAME_ORIGIN | nsILoadInfo::SEC_COOKIES_OMIT;

NS_IMETHODIMP
LoadInfo::GetCookiePolicy(uint32_t* aResult) {
  uint32_t policy = mSecurityFlags & sCookiePolicyMask;
  if (policy == nsILoadInfo::SEC_COOKIES_DEFAULT) {
    policy = (mSecurityFlags & SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT)
                 ? nsILoadInfo::SEC_COOKIES_SAME_ORIGIN
                 : nsILoadInfo::SEC_COOKIES_INCLUDE;
  }

  *aResult = policy;
  return NS_OK;
}

namespace {

already_AddRefed<nsICookieJarSettings> CreateCookieJarSettings(
    nsIPrincipal* aTriggeringPrincipal, nsContentPolicyType aContentPolicyType,
    bool aIsPrivate, bool aShouldResistFingerprinting) {
  // Special treatment for resources injected by add-ons.
  if (aTriggeringPrincipal &&
      StaticPrefs::privacy_antitracking_isolateContentScriptResources() &&
      nsContentUtils::IsExpandedPrincipal(aTriggeringPrincipal)) {
    return CookieJarSettings::Create(
        nsICookieService::BEHAVIOR_REJECT,
        StoragePrincipalHelper::PartitionKeyForExpandedPrincipal(
            aTriggeringPrincipal),
        OriginAttributes::IsFirstPartyEnabled(), false,
        aShouldResistFingerprinting);
  }

  if (StaticPrefs::network_cookieJarSettings_unblocked_for_testing()) {
    return aIsPrivate ? CookieJarSettings::Create(CookieJarSettings::ePrivate,
                                                  aShouldResistFingerprinting)
                      : CookieJarSettings::Create(CookieJarSettings::eRegular,
                                                  aShouldResistFingerprinting);
  }

  // These contentPolictTypes require a real CookieJarSettings because favicon
  // and save-as requests must send cookies. Anything else should not
  // send/receive cookies.
  if (aContentPolicyType == nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON ||
      aContentPolicyType == nsIContentPolicy::TYPE_SAVEAS_DOWNLOAD) {
    return aIsPrivate ? CookieJarSettings::Create(CookieJarSettings::ePrivate,
                                                  aShouldResistFingerprinting)
                      : CookieJarSettings::Create(CookieJarSettings::eRegular,
                                                  aShouldResistFingerprinting);
  }

  return CookieJarSettings::GetBlockingAll(aShouldResistFingerprinting);
}

}  // namespace

NS_IMETHODIMP
LoadInfo::GetCookieJarSettings(nsICookieJarSettings** aCookieJarSettings) {
  if (!mCookieJarSettings) {
    bool isPrivate = mOriginAttributes.IsPrivateBrowsing();
    nsCOMPtr<nsIPrincipal> loadingPrincipal;
    Unused << this->GetLoadingPrincipal(getter_AddRefs(loadingPrincipal));
    bool shouldResistFingerprinting =
        nsContentUtils::ShouldResistFingerprinting_dangerous(
            loadingPrincipal,
            "CookieJarSettings can't exist yet, we're creating it",
            RFPTarget::IsAlwaysEnabledForPrecompute);
    mCookieJarSettings = CreateCookieJarSettings(
        mTriggeringPrincipal, mInternalContentPolicyType, isPrivate,
        shouldResistFingerprinting);
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings = mCookieJarSettings;
  cookieJarSettings.forget(aCookieJarSettings);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetCookieJarSettings(nsICookieJarSettings* aCookieJarSettings) {
  MOZ_ASSERT(aCookieJarSettings);
  // We allow the overwrite of CookieJarSettings.
  mCookieJarSettings = aCookieJarSettings;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetStoragePermission(
    nsILoadInfo::StoragePermissionState* aStoragePermission) {
  *aStoragePermission = mStoragePermission;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetStoragePermission(
    nsILoadInfo::StoragePermissionState aStoragePermission) {
  mStoragePermission = aStoragePermission;
  return NS_OK;
}

const Maybe<RFPTargetSet>& LoadInfo::GetOverriddenFingerprintingSettings() {
#ifdef DEBUG
  RefPtr<BrowsingContext> browsingContext;
  GetTargetBrowsingContext(getter_AddRefs(browsingContext));

  // Exclude this check if the target browsing context is for the parent
  // process.
  MOZ_ASSERT_IF(XRE_IsParentProcess() && browsingContext &&
                    !browsingContext->IsInProcess(),
                mOverriddenFingerprintingSettingsIsSet);
#endif
  return mOverriddenFingerprintingSettings;
}

void LoadInfo::SetOverriddenFingerprintingSettings(RFPTargetSet aTargets) {
  mOverriddenFingerprintingSettings.reset();
  mOverriddenFingerprintingSettings.emplace(aTargets);
}

NS_IMETHODIMP
LoadInfo::GetIsMetaRefresh(bool* aIsMetaRefresh) {
  *aIsMetaRefresh = mIsMetaRefresh;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsMetaRefresh(bool aIsMetaRefresh) {
  mIsMetaRefresh = aIsMetaRefresh;
  return NS_OK;
}

void LoadInfo::SetIncludeCookiesSecFlag() {
  MOZ_ASSERT((mSecurityFlags & sCookiePolicyMask) ==
             nsILoadInfo::SEC_COOKIES_DEFAULT);
  mSecurityFlags =
      (mSecurityFlags & ~sCookiePolicyMask) | nsILoadInfo::SEC_COOKIES_INCLUDE;
}

NS_IMETHODIMP
LoadInfo::GetForceInheritPrincipal(bool* aInheritPrincipal) {
  *aInheritPrincipal =
      (mSecurityFlags & nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetForceInheritPrincipalOverruleOwner(bool* aInheritPrincipal) {
  *aInheritPrincipal =
      (mSecurityFlags &
       nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL_OVERRULE_OWNER);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetLoadingSandboxed(bool* aLoadingSandboxed) {
  *aLoadingSandboxed = (mSandboxFlags & SANDBOXED_ORIGIN);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetAboutBlankInherits(bool* aResult) {
  *aResult = (mSecurityFlags & nsILoadInfo::SEC_ABOUT_BLANK_INHERITS);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetAllowChrome(bool* aResult) {
  *aResult = (mSecurityFlags & nsILoadInfo::SEC_ALLOW_CHROME);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetDisallowScript(bool* aResult) {
  *aResult = (mSecurityFlags & nsILoadInfo::SEC_DISALLOW_SCRIPT);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetDontFollowRedirects(bool* aResult) {
  *aResult = (mSecurityFlags & nsILoadInfo::SEC_DONT_FOLLOW_REDIRECTS);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetLoadErrorPage(bool* aResult) {
  *aResult = (mSecurityFlags & nsILoadInfo::SEC_LOAD_ERROR_PAGE);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsFormSubmission(bool* aResult) {
  *aResult = mIsFormSubmission;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsFormSubmission(bool aValue) {
  mIsFormSubmission = aValue;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsGETRequest(bool* aResult) {
  *aResult = mIsGETRequest;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsGETRequest(bool aValue) {
  mIsGETRequest = aValue;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSendCSPViolationEvents(bool* aResult) {
  *aResult = mSendCSPViolationEvents;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetSendCSPViolationEvents(bool aValue) {
  mSendCSPViolationEvents = aValue;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetExternalContentPolicyType(nsContentPolicyType* aResult) {
  // We have to use nsContentPolicyType because ExtContentPolicyType is not
  // visible from xpidl.
  *aResult = static_cast<nsContentPolicyType>(
      nsContentUtils::InternalContentPolicyTypeToExternal(
          mInternalContentPolicyType));
  return NS_OK;
}

nsContentPolicyType LoadInfo::InternalContentPolicyType() {
  return mInternalContentPolicyType;
}

NS_IMETHODIMP
LoadInfo::GetBlockAllMixedContent(bool* aResult) {
  *aResult = mBlockAllMixedContent;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetUpgradeInsecureRequests(bool* aResult) {
  *aResult = mUpgradeInsecureRequests;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetBrowserUpgradeInsecureRequests(bool* aResult) {
  *aResult = mBrowserUpgradeInsecureRequests;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetBrowserDidUpgradeInsecureRequests(bool* aResult) {
  *aResult = mBrowserDidUpgradeInsecureRequests;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetBrowserWouldUpgradeInsecureRequests(bool* aResult) {
  *aResult = mBrowserWouldUpgradeInsecureRequests;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetForceAllowDataURI(bool aForceAllowDataURI) {
  MOZ_ASSERT(!mForceAllowDataURI ||
                 mInternalContentPolicyType == nsIContentPolicy::TYPE_DOCUMENT,
             "can only allow data URI navigation for TYPE_DOCUMENT");
  mForceAllowDataURI = aForceAllowDataURI;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetForceAllowDataURI(bool* aForceAllowDataURI) {
  *aForceAllowDataURI = mForceAllowDataURI;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetAllowInsecureRedirectToDataURI(
    bool aAllowInsecureRedirectToDataURI) {
  mAllowInsecureRedirectToDataURI = aAllowInsecureRedirectToDataURI;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetAllowInsecureRedirectToDataURI(
    bool* aAllowInsecureRedirectToDataURI) {
  *aAllowInsecureRedirectToDataURI = mAllowInsecureRedirectToDataURI;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetSkipContentPolicyCheckForWebRequest(bool aSkip) {
  mSkipContentPolicyCheckForWebRequest = aSkip;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSkipContentPolicyCheckForWebRequest(bool* aSkip) {
  *aSkip = mSkipContentPolicyCheckForWebRequest;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetOriginalFrameSrcLoad(bool aOriginalFrameSrcLoad) {
  mOriginalFrameSrcLoad = aOriginalFrameSrcLoad;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetOriginalFrameSrcLoad(bool* aOriginalFrameSrcLoad) {
  *aOriginalFrameSrcLoad = mOriginalFrameSrcLoad;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetForceInheritPrincipalDropped(bool* aResult) {
  *aResult = mForceInheritPrincipalDropped;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetInnerWindowID(uint64_t* aResult) {
  *aResult = mInnerWindowID;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetBrowsingContextID(uint64_t* aResult) {
  *aResult = mBrowsingContextID;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetWorkerAssociatedBrowsingContextID(uint64_t* aResult) {
  *aResult = mWorkerAssociatedBrowsingContextID;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetWorkerAssociatedBrowsingContextID(uint64_t aID) {
  mWorkerAssociatedBrowsingContextID = aID;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetFrameBrowsingContextID(uint64_t* aResult) {
  *aResult = mFrameBrowsingContextID;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetTargetBrowsingContextID(uint64_t* aResult) {
  return (nsILoadInfo::GetExternalContentPolicyType() ==
          ExtContentPolicy::TYPE_SUBDOCUMENT)
             ? GetFrameBrowsingContextID(aResult)
             : GetBrowsingContextID(aResult);
}

NS_IMETHODIMP
LoadInfo::GetBrowsingContext(dom::BrowsingContext** aResult) {
  *aResult = BrowsingContext::Get(mBrowsingContextID).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetWorkerAssociatedBrowsingContext(dom::BrowsingContext** aResult) {
  *aResult = BrowsingContext::Get(mWorkerAssociatedBrowsingContextID).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetFrameBrowsingContext(dom::BrowsingContext** aResult) {
  *aResult = BrowsingContext::Get(mFrameBrowsingContextID).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetTargetBrowsingContext(dom::BrowsingContext** aResult) {
  uint64_t targetBrowsingContextID = 0;
  MOZ_ALWAYS_SUCCEEDS(GetTargetBrowsingContextID(&targetBrowsingContextID));
  *aResult = BrowsingContext::Get(targetBrowsingContextID).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandle<JS::Value> aOriginAttributes) {
  if (NS_WARN_IF(!ToJSValue(aCx, mOriginAttributes, aOriginAttributes))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::ResetPrincipalToInheritToNullPrincipal() {
  // take the originAttributes from the LoadInfo and create
  // a new NullPrincipal using those origin attributes.
  nsCOMPtr<nsIPrincipal> newNullPrincipal =
      NullPrincipal::Create(mOriginAttributes);

  mPrincipalToInherit = newNullPrincipal;

  // setting SEC_FORCE_INHERIT_PRINCIPAL_OVERRULE_OWNER will overrule
  // any non null owner set on the channel and will return the principal
  // form the loadinfo instead.
  mSecurityFlags |= SEC_FORCE_INHERIT_PRINCIPAL_OVERRULE_OWNER;

  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetScriptableOriginAttributes(
    JSContext* aCx, JS::Handle<JS::Value> aOriginAttributes) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  mOriginAttributes = attrs;
  return NS_OK;
}

nsresult LoadInfo::GetOriginAttributes(
    mozilla::OriginAttributes* aOriginAttributes) {
  NS_ENSURE_ARG(aOriginAttributes);
  *aOriginAttributes = mOriginAttributes;
  return NS_OK;
}

nsresult LoadInfo::SetOriginAttributes(
    const mozilla::OriginAttributes& aOriginAttributes) {
  mOriginAttributes = aOriginAttributes;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetInitialSecurityCheckDone(bool aInitialSecurityCheckDone) {
  // Indicates whether the channel was ever evaluated by the
  // ContentSecurityManager. Once set to true, this flag must
  // remain true throughout the lifetime of the channel.
  // Setting it to anything else than true will be discarded.
  MOZ_ASSERT(aInitialSecurityCheckDone,
             "aInitialSecurityCheckDone must be true");
  mInitialSecurityCheckDone =
      mInitialSecurityCheckDone || aInitialSecurityCheckDone;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetInitialSecurityCheckDone(bool* aResult) {
  *aResult = mInitialSecurityCheckDone;
  return NS_OK;
}

// To prevent unintenional credential and information leaks in content
// processes we can use this function to truncate a Principal's URI as much as
// possible.
already_AddRefed<nsIPrincipal> CreateTruncatedPrincipal(
    nsIPrincipal* aPrincipal) {
  nsCOMPtr<nsIPrincipal> truncatedPrincipal;
  // System Principal URIs don't need to be truncated as they don't contain any
  // sensitive browsing history information.
  if (aPrincipal->IsSystemPrincipal()) {
    truncatedPrincipal = aPrincipal;
    return truncatedPrincipal.forget();
  }

  // Content Principal URIs are the main location of the information we need to
  // truncate.
  if (aPrincipal->GetIsContentPrincipal()) {
    // Certain URIs (chrome, resource, about, jar) don't need to be truncated
    // as they should be free of any sensitive user browsing history.
    if (aPrincipal->SchemeIs("chrome") || aPrincipal->SchemeIs("resource") ||
        aPrincipal->SchemeIs("about") || aPrincipal->SchemeIs("jar")) {
      truncatedPrincipal = aPrincipal;
      return truncatedPrincipal.forget();
    }

    // Different parts of the URI are preserved due to being vital to the
    // browser's operation.
    // Scheme for differentiating between different types of URIs and how to
    // truncate them and later on utilize them.
    // Host and Port to retain the redirect chain's core functionality.
    // Path would ideally be removed but needs to be retained to ensure that
    // http/https redirect loops can be detected.
    // The entirety of the Query String, Reference Fragment, and User Info
    // subcomponents must be stripped to avoid leaking Oauth tokens, user
    // identifiers, and similar bits of information that these subcomponents may
    // contain.
    nsAutoCString scheme;
    nsAutoCString separator("://");
    nsAutoCString hostPort;
    nsAutoCString path;
    nsAutoCString uriString("");
    if (aPrincipal->SchemeIs("view-source")) {
      // The path portion of the view-source URI will be the URI whose source is
      // being viewed, so we create a new URI object with a truncated form of
      // the path and append the view-source scheme to the front again.
      nsAutoCString viewSourcePath;
      aPrincipal->GetFilePath(viewSourcePath);

      nsCOMPtr<nsIURI> nestedURI;
      nsresult rv = NS_NewURI(getter_AddRefs(nestedURI), viewSourcePath);

      if (NS_FAILED(rv)) {
        // Since the path here should be an already validated URI this should
        // never happen.
        NS_WARNING(viewSourcePath.get());
        MOZ_ASSERT(false,
                   "Failed to create truncated form of URI with NS_NewURI.");
        truncatedPrincipal = aPrincipal;
        return truncatedPrincipal.forget();
      }

      nestedURI->GetScheme(scheme);
      nestedURI->GetHostPort(hostPort);
      nestedURI->GetFilePath(path);
      uriString += "view-source:";
    } else {
      aPrincipal->GetScheme(scheme);
      aPrincipal->GetHostPort(hostPort);
      aPrincipal->GetFilePath(path);
    }
    uriString += scheme + separator + hostPort + path;

    nsCOMPtr<nsIURI> truncatedURI;
    nsresult rv = NS_NewURI(getter_AddRefs(truncatedURI), uriString);
    if (NS_FAILED(rv)) {
      NS_WARNING(uriString.get());
      MOZ_ASSERT(false,
                 "Failed to create truncated form of URI with NS_NewURI.");
      truncatedPrincipal = aPrincipal;
      return truncatedPrincipal.forget();
    }

    return BasePrincipal::CreateContentPrincipal(
        truncatedURI, aPrincipal->OriginAttributesRef());
  }

  // Null Principal Precursor URIs can also contain information that needs to
  // be truncated.
  if (aPrincipal->GetIsNullPrincipal()) {
    nsCOMPtr<nsIPrincipal> precursorPrincipal =
        aPrincipal->GetPrecursorPrincipal();
    // If there is no precursor then nothing needs to be truncated.
    if (!precursorPrincipal) {
      truncatedPrincipal = aPrincipal;
      return truncatedPrincipal.forget();
    }

    // Otherwise we return a new Null Principal with the original's Origin
    // Attributes and a truncated version of the original's precursor URI.
    nsCOMPtr<nsIPrincipal> truncatedPrecursor =
        CreateTruncatedPrincipal(precursorPrincipal);
    return NullPrincipal::CreateWithInheritedAttributes(truncatedPrecursor);
  }

  // Expanded Principals shouldn't contain sensitive information but their
  // allowlists might so we truncate that information here.
  if (aPrincipal->GetIsExpandedPrincipal()) {
    nsTArray<nsCOMPtr<nsIPrincipal>> truncatedAllowList;

    for (const auto& allowedPrincipal : BasePrincipal::Cast(aPrincipal)
                                            ->As<ExpandedPrincipal>()
                                            ->AllowList()) {
      nsCOMPtr<nsIPrincipal> truncatedPrincipal =
          CreateTruncatedPrincipal(allowedPrincipal);

      truncatedAllowList.AppendElement(truncatedPrincipal);
    }

    return ExpandedPrincipal::Create(truncatedAllowList,
                                     aPrincipal->OriginAttributesRef());
  }

  // If we hit this assertion we need to update this function to add the
  // Principals and URIs seen as new corner cases to handle.
  MOZ_ASSERT(false, "Unhandled Principal or URI type encountered.");

  truncatedPrincipal = aPrincipal;
  return truncatedPrincipal.forget();
}

NS_IMETHODIMP
LoadInfo::AppendRedirectHistoryEntry(nsIChannel* aChannel,
                                     bool aIsInternalRedirect) {
  NS_ENSURE_ARG(aChannel);
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIPrincipal> uriPrincipal;
  nsIScriptSecurityManager* sm = nsContentUtils::GetSecurityManager();
  sm->GetChannelURIPrincipal(aChannel, getter_AddRefs(uriPrincipal));

  nsCOMPtr<nsIURI> referrer;
  nsCString remoteAddress;

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aChannel));
  if (httpChannel) {
    nsCOMPtr<nsIReferrerInfo> referrerInfo;
    Unused << httpChannel->GetReferrerInfo(getter_AddRefs(referrerInfo));
    if (referrerInfo) {
      referrer = referrerInfo->GetComputedReferrer();
    }

    nsCOMPtr<nsIHttpChannelInternal> intChannel(do_QueryInterface(aChannel));
    if (intChannel) {
      Unused << intChannel->GetRemoteAddress(remoteAddress);
    }
  }

  nsCOMPtr<nsIPrincipal> truncatedPrincipal =
      CreateTruncatedPrincipal(uriPrincipal);

  nsCOMPtr<nsIRedirectHistoryEntry> entry =
      new nsRedirectHistoryEntry(truncatedPrincipal, referrer, remoteAddress);

  mRedirectChainIncludingInternalRedirects.AppendElement(entry);
  if (!aIsInternalRedirect) {
    mRedirectChain.AppendElement(entry);
  }
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetRedirects(JSContext* aCx, JS::MutableHandle<JS::Value> aRedirects,
                       const RedirectHistoryArray& aArray) {
  JS::Rooted<JSObject*> redirects(aCx,
                                  JS::NewArrayObject(aCx, aArray.Length()));
  NS_ENSURE_TRUE(redirects, NS_ERROR_OUT_OF_MEMORY);

  JS::Rooted<JSObject*> global(aCx, JS::CurrentGlobalOrNull(aCx));
  NS_ENSURE_TRUE(global, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIXPConnect> xpc = nsIXPConnect::XPConnect();

  for (size_t idx = 0; idx < aArray.Length(); idx++) {
    JS::Rooted<JSObject*> jsobj(aCx);
    nsresult rv =
        xpc->WrapNative(aCx, global, aArray[idx],
                        NS_GET_IID(nsIRedirectHistoryEntry), jsobj.address());
    NS_ENSURE_SUCCESS(rv, rv);
    NS_ENSURE_STATE(jsobj);

    bool rc = JS_DefineElement(aCx, redirects, idx, jsobj, JSPROP_ENUMERATE);
    NS_ENSURE_TRUE(rc, NS_ERROR_UNEXPECTED);
  }

  aRedirects.setObject(*redirects);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetRedirectChainIncludingInternalRedirects(
    JSContext* aCx, JS::MutableHandle<JS::Value> aChain) {
  return GetRedirects(aCx, aChain, mRedirectChainIncludingInternalRedirects);
}

const RedirectHistoryArray&
LoadInfo::RedirectChainIncludingInternalRedirects() {
  return mRedirectChainIncludingInternalRedirects;
}

NS_IMETHODIMP
LoadInfo::GetRedirectChain(JSContext* aCx,
                           JS::MutableHandle<JS::Value> aChain) {
  return GetRedirects(aCx, aChain, mRedirectChain);
}

const RedirectHistoryArray& LoadInfo::RedirectChain() { return mRedirectChain; }

const nsTArray<nsCOMPtr<nsIPrincipal>>& LoadInfo::AncestorPrincipals() {
  return mAncestorPrincipals;
}

const nsTArray<uint64_t>& LoadInfo::AncestorBrowsingContextIDs() {
  return mAncestorBrowsingContextIDs;
}

void LoadInfo::SetCorsPreflightInfo(const nsTArray<nsCString>& aHeaders,
                                    bool aForcePreflight) {
  MOZ_ASSERT(GetSecurityMode() ==
             nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT);
  MOZ_ASSERT(!mInitialSecurityCheckDone);
  mCorsUnsafeHeaders = aHeaders.Clone();
  mForcePreflight = aForcePreflight;
}

const nsTArray<nsCString>& LoadInfo::CorsUnsafeHeaders() {
  return mCorsUnsafeHeaders;
}

NS_IMETHODIMP
LoadInfo::GetForcePreflight(bool* aForcePreflight) {
  *aForcePreflight = mForcePreflight;
  return NS_OK;
}

void LoadInfo::SetIsPreflight() {
  MOZ_ASSERT(GetSecurityMode() ==
             nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT);
  MOZ_ASSERT(!mInitialSecurityCheckDone);
  mIsPreflight = true;
}

void LoadInfo::SetUpgradeInsecureRequests(bool aValue) {
  mUpgradeInsecureRequests = aValue;
}

void LoadInfo::SetBrowserUpgradeInsecureRequests() {
  mBrowserUpgradeInsecureRequests = true;
}

NS_IMETHODIMP
LoadInfo::SetBrowserDidUpgradeInsecureRequests(
    bool aBrowserDidUpgradeInsecureRequests) {
  mBrowserDidUpgradeInsecureRequests = aBrowserDidUpgradeInsecureRequests;
  return NS_OK;
}

void LoadInfo::SetBrowserWouldUpgradeInsecureRequests() {
  mBrowserWouldUpgradeInsecureRequests = true;
}

NS_IMETHODIMP
LoadInfo::GetIsPreflight(bool* aIsPreflight) {
  *aIsPreflight = mIsPreflight;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetLoadTriggeredFromExternal(bool aLoadTriggeredFromExternal) {
  MOZ_ASSERT(!aLoadTriggeredFromExternal ||
                 mInternalContentPolicyType == nsIContentPolicy::TYPE_DOCUMENT,
             "can only set load triggered from external for TYPE_DOCUMENT");
  mLoadTriggeredFromExternal = aLoadTriggeredFromExternal;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetLoadTriggeredFromExternal(bool* aLoadTriggeredFromExternal) {
  *aLoadTriggeredFromExternal = mLoadTriggeredFromExternal;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetServiceWorkerTaintingSynthesized(
    bool* aServiceWorkerTaintingSynthesized) {
  MOZ_ASSERT(aServiceWorkerTaintingSynthesized);
  *aServiceWorkerTaintingSynthesized = mServiceWorkerTaintingSynthesized;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetTainting(uint32_t* aTaintingOut) {
  MOZ_ASSERT(aTaintingOut);
  *aTaintingOut = static_cast<uint32_t>(mTainting);
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::MaybeIncreaseTainting(uint32_t aTainting) {
  NS_ENSURE_ARG(aTainting <= TAINTING_OPAQUE);

  // Skip if the tainting has been set by the service worker.
  if (mServiceWorkerTaintingSynthesized) {
    return NS_OK;
  }

  LoadTainting tainting = static_cast<LoadTainting>(aTainting);
  if (tainting > mTainting) {
    mTainting = tainting;
  }
  return NS_OK;
}

void LoadInfo::SynthesizeServiceWorkerTainting(LoadTainting aTainting) {
  MOZ_DIAGNOSTIC_ASSERT(aTainting <= LoadTainting::Opaque);
  mTainting = aTainting;

  // Flag to prevent the tainting from being increased.
  mServiceWorkerTaintingSynthesized = true;
}

NS_IMETHODIMP
LoadInfo::GetDocumentHasUserInteracted(bool* aDocumentHasUserInteracted) {
  MOZ_ASSERT(aDocumentHasUserInteracted);
  *aDocumentHasUserInteracted = mDocumentHasUserInteracted;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetDocumentHasUserInteracted(bool aDocumentHasUserInteracted) {
  mDocumentHasUserInteracted = aDocumentHasUserInteracted;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetAllowListFutureDocumentsCreatedFromThisRedirectChain(
    bool* aValue) {
  MOZ_ASSERT(aValue);
  *aValue = mAllowListFutureDocumentsCreatedFromThisRedirectChain;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetAllowListFutureDocumentsCreatedFromThisRedirectChain(bool aValue) {
  mAllowListFutureDocumentsCreatedFromThisRedirectChain = aValue;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetNeedForCheckingAntiTrackingHeuristic(bool* aValue) {
  MOZ_ASSERT(aValue);
  *aValue = mNeedForCheckingAntiTrackingHeuristic;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetNeedForCheckingAntiTrackingHeuristic(bool aValue) {
  mNeedForCheckingAntiTrackingHeuristic = aValue;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetCspNonce(nsAString& aCspNonce) {
  aCspNonce = mCspNonce;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetCspNonce(const nsAString& aCspNonce) {
  MOZ_ASSERT(!mInitialSecurityCheckDone,
             "setting the nonce is only allowed before any sec checks");
  mCspNonce = aCspNonce;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIntegrityMetadata(nsAString& aIntegrityMetadata) {
  aIntegrityMetadata = mIntegrityMetadata;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIntegrityMetadata(const nsAString& aIntegrityMetadata) {
  MOZ_ASSERT(!mInitialSecurityCheckDone,
             "setting the nonce is only allowed before any sec checks");
  mIntegrityMetadata = aIntegrityMetadata;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSkipContentSniffing(bool* aSkipContentSniffing) {
  *aSkipContentSniffing = mSkipContentSniffing;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetSkipContentSniffing(bool aSkipContentSniffing) {
  mSkipContentSniffing = aSkipContentSniffing;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetHttpsOnlyStatus(uint32_t* aHttpsOnlyStatus) {
  *aHttpsOnlyStatus = mHttpsOnlyStatus;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetHttpsOnlyStatus(uint32_t aHttpsOnlyStatus) {
  mHttpsOnlyStatus = aHttpsOnlyStatus;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetHstsStatus(bool* aHstsStatus) {
  *aHstsStatus = mHstsStatus;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetHstsStatus(bool aHstsStatus) {
  mHstsStatus = aHstsStatus;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetHasValidUserGestureActivation(
    bool* aHasValidUserGestureActivation) {
  *aHasValidUserGestureActivation = mHasValidUserGestureActivation;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetHasValidUserGestureActivation(
    bool aHasValidUserGestureActivation) {
  mHasValidUserGestureActivation = aHasValidUserGestureActivation;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetTextDirectiveUserActivation(bool* aTextDirectiveUserActivation) {
  *aTextDirectiveUserActivation = mTextDirectiveUserActivation;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetTextDirectiveUserActivation(bool aTextDirectiveUserActivation) {
  mTextDirectiveUserActivation = aTextDirectiveUserActivation;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsSameDocumentNavigation(bool* aIsSameDocumentNavigation) {
  *aIsSameDocumentNavigation = mIsSameDocumentNavigation;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsSameDocumentNavigation(bool aIsSameDocumentNavigation) {
  mIsSameDocumentNavigation = aIsSameDocumentNavigation;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetAllowDeprecatedSystemRequests(
    bool* aAllowDeprecatedSystemRequests) {
  *aAllowDeprecatedSystemRequests = mAllowDeprecatedSystemRequests;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetAllowDeprecatedSystemRequests(
    bool aAllowDeprecatedSystemRequests) {
  mAllowDeprecatedSystemRequests = aAllowDeprecatedSystemRequests;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsUserTriggeredSave(bool* aIsUserTriggeredSave) {
  *aIsUserTriggeredSave =
      mIsUserTriggeredSave ||
      mInternalContentPolicyType == nsIContentPolicy::TYPE_SAVEAS_DOWNLOAD;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsUserTriggeredSave(bool aIsUserTriggeredSave) {
  mIsUserTriggeredSave = aIsUserTriggeredSave;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsInDevToolsContext(bool* aIsInDevToolsContext) {
  *aIsInDevToolsContext = mIsInDevToolsContext;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsInDevToolsContext(bool aIsInDevToolsContext) {
  mIsInDevToolsContext = aIsInDevToolsContext;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetParserCreatedScript(bool* aParserCreatedScript) {
  *aParserCreatedScript = mParserCreatedScript;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetParserCreatedScript(bool aParserCreatedScript) {
  mParserCreatedScript = aParserCreatedScript;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsTopLevelLoad(bool* aResult) {
  RefPtr<dom::BrowsingContext> bc;
  GetTargetBrowsingContext(getter_AddRefs(bc));
  *aResult = !bc || bc->IsTop();
  return NS_OK;
}

void LoadInfo::SetIsFromProcessingFrameAttributes() {
  mIsFromProcessingFrameAttributes = true;
}

NS_IMETHODIMP
LoadInfo::GetIsFromProcessingFrameAttributes(
    bool* aIsFromProcessingFrameAttributes) {
  MOZ_ASSERT(aIsFromProcessingFrameAttributes);
  *aIsFromProcessingFrameAttributes = mIsFromProcessingFrameAttributes;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsMediaRequest(bool aIsMediaRequest) {
  mIsMediaRequest = aIsMediaRequest;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsMediaRequest(bool* aIsMediaRequest) {
  MOZ_ASSERT(aIsMediaRequest);
  *aIsMediaRequest = mIsMediaRequest;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsMediaInitialRequest(bool aIsMediaInitialRequest) {
  mIsMediaInitialRequest = aIsMediaInitialRequest;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsMediaInitialRequest(bool* aIsMediaInitialRequest) {
  MOZ_ASSERT(aIsMediaInitialRequest);
  *aIsMediaInitialRequest = mIsMediaInitialRequest;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsFromObjectOrEmbed(bool aIsFromObjectOrEmbed) {
  mIsFromObjectOrEmbed = aIsFromObjectOrEmbed;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsFromObjectOrEmbed(bool* aIsFromObjectOrEmbed) {
  MOZ_ASSERT(aIsFromObjectOrEmbed);
  *aIsFromObjectOrEmbed = mIsFromObjectOrEmbed;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetResultPrincipalURI(nsIURI** aURI) {
  *aURI = do_AddRef(mResultPrincipalURI).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetResultPrincipalURI(nsIURI* aURI) {
  mResultPrincipalURI = aURI;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetChannelCreationOriginalURI(nsIURI** aURI) {
  *aURI = do_AddRef(mChannelCreationOriginalURI).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetChannelCreationOriginalURI(nsIURI* aURI) {
  mChannelCreationOriginalURI = aURI;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetRequestBlockingReason(uint32_t aReason) {
  mRequestBlockingReason = aReason;
  return NS_OK;
}
NS_IMETHODIMP
LoadInfo::GetRequestBlockingReason(uint32_t* aReason) {
  *aReason = mRequestBlockingReason;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetUnstrippedURI(nsIURI** aURI) {
  *aURI = do_AddRef(mUnstrippedURI).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetUnstrippedURI(nsIURI* aURI) {
  mUnstrippedURI = aURI;
  return NS_OK;
}

void LoadInfo::SetClientInfo(const ClientInfo& aClientInfo) {
  mClientInfo.emplace(aClientInfo);
}

const Maybe<ClientInfo>& LoadInfo::GetClientInfo() { return mClientInfo; }

void LoadInfo::GiveReservedClientSource(
    UniquePtr<ClientSource>&& aClientSource) {
  MOZ_DIAGNOSTIC_ASSERT(aClientSource);
  mReservedClientSource = std::move(aClientSource);
  SetReservedClientInfo(mReservedClientSource->Info());
}

UniquePtr<ClientSource> LoadInfo::TakeReservedClientSource() {
  if (mReservedClientSource) {
    // If the reserved ClientInfo was set due to a ClientSource being present,
    // then clear that info object when the ClientSource is taken.
    mReservedClientInfo.reset();
  }
  return std::move(mReservedClientSource);
}

void LoadInfo::SetReservedClientInfo(const ClientInfo& aClientInfo) {
  MOZ_DIAGNOSTIC_ASSERT(mInitialClientInfo.isNothing());
  // Treat assignments of the same value as a no-op.  The emplace below
  // will normally assert when overwriting an existing value.
  if (mReservedClientInfo.isSome()) {
    if (mReservedClientInfo.ref() == aClientInfo) {
      return;
    }
    MOZ_DIAGNOSTIC_CRASH("mReservedClientInfo already set");
    mReservedClientInfo.reset();
  }
  mReservedClientInfo.emplace(aClientInfo);
}

void LoadInfo::OverrideReservedClientInfoInParent(
    const ClientInfo& aClientInfo) {
  // This should only be called to handle redirects in the parent process.
  MOZ_ASSERT(XRE_GetProcessType() == GeckoProcessType_Default);

  mInitialClientInfo.reset();
  mReservedClientInfo.reset();
  mReservedClientInfo.emplace(aClientInfo);
}

const Maybe<ClientInfo>& LoadInfo::GetReservedClientInfo() {
  return mReservedClientInfo;
}

void LoadInfo::SetInitialClientInfo(const ClientInfo& aClientInfo) {
  MOZ_DIAGNOSTIC_ASSERT(!mReservedClientSource);
  MOZ_DIAGNOSTIC_ASSERT(mReservedClientInfo.isNothing());
  // Treat assignments of the same value as a no-op.  The emplace below
  // will normally assert when overwriting an existing value.
  if (mInitialClientInfo.isSome() && mInitialClientInfo.ref() == aClientInfo) {
    return;
  }
  mInitialClientInfo.emplace(aClientInfo);
}

const Maybe<ClientInfo>& LoadInfo::GetInitialClientInfo() {
  return mInitialClientInfo;
}

void LoadInfo::SetController(const ServiceWorkerDescriptor& aServiceWorker) {
  mController.emplace(aServiceWorker);
}

void LoadInfo::ClearController() { mController.reset(); }

const Maybe<ServiceWorkerDescriptor>& LoadInfo::GetController() {
  return mController;
}

void LoadInfo::SetPerformanceStorage(PerformanceStorage* aPerformanceStorage) {
  mPerformanceStorage = aPerformanceStorage;
}

PerformanceStorage* LoadInfo::GetPerformanceStorage() {
  if (mPerformanceStorage) {
    return mPerformanceStorage;
  }

  auto* innerWindow = nsGlobalWindowInner::GetInnerWindowWithId(mInnerWindowID);
  if (!innerWindow) {
    return nullptr;
  }

  if (!TriggeringPrincipal()->Equals(innerWindow->GetPrincipal())) {
    return nullptr;
  }

  if (nsILoadInfo::GetExternalContentPolicyType() ==
          ExtContentPolicy::TYPE_SUBDOCUMENT &&
      !GetIsFromProcessingFrameAttributes()) {
    // We only report loads caused by processing the attributes of the
    // browsing context container.
    return nullptr;
  }

  mozilla::dom::Performance* performance = innerWindow->GetPerformance();
  if (!performance) {
    return nullptr;
  }

  return performance->AsPerformanceStorage();
}

NS_IMETHODIMP
LoadInfo::GetCspEventListener(nsICSPEventListener** aCSPEventListener) {
  *aCSPEventListener = do_AddRef(mCSPEventListener).take();
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetCspEventListener(nsICSPEventListener* aCSPEventListener) {
  mCSPEventListener = aCSPEventListener;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetInternalContentPolicyType(nsContentPolicyType* aResult) {
  *aResult = mInternalContentPolicyType;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetFetchDestination(nsACString& aDestination) {
  aDestination.Assign(
      GetEnumString(InternalRequest::MapContentPolicyTypeToRequestDestination(
          mInternalContentPolicyType)));
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetLoadingEmbedderPolicy(
    nsILoadInfo::CrossOriginEmbedderPolicy* aOutPolicy) {
  *aOutPolicy = mLoadingEmbedderPolicy;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetLoadingEmbedderPolicy(
    nsILoadInfo::CrossOriginEmbedderPolicy aPolicy) {
  mLoadingEmbedderPolicy = aPolicy;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsOriginTrialCoepCredentiallessEnabledForTopLevel(
    bool* aIsOriginTrialCoepCredentiallessEnabledForTopLevel) {
  *aIsOriginTrialCoepCredentiallessEnabledForTopLevel =
      mIsOriginTrialCoepCredentiallessEnabledForTopLevel;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsOriginTrialCoepCredentiallessEnabledForTopLevel(
    bool aIsOriginTrialCoepCredentiallessEnabledForTopLevel) {
  mIsOriginTrialCoepCredentiallessEnabledForTopLevel =
      aIsOriginTrialCoepCredentiallessEnabledForTopLevel;
  return NS_OK;
}

already_AddRefed<nsIContentSecurityPolicy> LoadInfo::GetCsp() {
  // Before querying the CSP from the client we have to check if the
  // triggeringPrincipal originates from an addon and potentially
  // overrides the CSP stored within the client.
  if (mLoadingPrincipal && BasePrincipal::Cast(mTriggeringPrincipal)
                               ->OverridesCSP(mLoadingPrincipal)) {
    nsCOMPtr<nsIExpandedPrincipal> ep = do_QueryInterface(mTriggeringPrincipal);
    nsCOMPtr<nsIContentSecurityPolicy> addonCSP;
    if (ep) {
      addonCSP = ep->GetCsp();
    }
    return addonCSP.forget();
  }

  if (mClientInfo.isNothing()) {
    return nullptr;
  }

  nsCOMPtr<nsINode> node = do_QueryReferent(mLoadingContext);
  RefPtr<Document> doc = node ? node->OwnerDoc() : nullptr;

  // If the client is of type window, then we return the cached CSP
  // stored on the document instead of having to deserialize the CSP
  // from the ClientInfo.
  if (doc && mClientInfo->Type() == ClientType::Window) {
    nsCOMPtr<nsIContentSecurityPolicy> docCSP = doc->GetCsp();
    return docCSP.forget();
  }

  Maybe<mozilla::ipc::CSPInfo> cspInfo = mClientInfo->GetCspInfo();
  if (cspInfo.isNothing()) {
    return nullptr;
  }
  nsCOMPtr<nsIContentSecurityPolicy> clientCSP =
      CSPInfoToCSP(cspInfo.ref(), doc);
  return clientCSP.forget();
}

already_AddRefed<nsIContentSecurityPolicy> LoadInfo::GetPreloadCsp() {
  if (mClientInfo.isNothing()) {
    return nullptr;
  }

  nsCOMPtr<nsINode> node = do_QueryReferent(mLoadingContext);
  RefPtr<Document> doc = node ? node->OwnerDoc() : nullptr;

  // If the client is of type window, then we return the cached CSP
  // stored on the document instead of having to deserialize the CSP
  // from the ClientInfo.
  if (doc && mClientInfo->Type() == ClientType::Window) {
    nsCOMPtr<nsIContentSecurityPolicy> preloadCsp = doc->GetPreloadCsp();
    return preloadCsp.forget();
  }

  Maybe<mozilla::ipc::CSPInfo> cspInfo = mClientInfo->GetPreloadCspInfo();
  if (cspInfo.isNothing()) {
    return nullptr;
  }
  nsCOMPtr<nsIContentSecurityPolicy> preloadCSP =
      CSPInfoToCSP(cspInfo.ref(), doc);
  return preloadCSP.forget();
}

already_AddRefed<nsIContentSecurityPolicy> LoadInfo::GetCspToInherit() {
  nsCOMPtr<nsIContentSecurityPolicy> cspToInherit = mCspToInherit;
  return cspToInherit.forget();
}

Maybe<FeaturePolicyInfo> LoadInfo::GetContainerFeaturePolicyInfo() {
  return mContainerFeaturePolicyInfo;
}

void LoadInfo::SetContainerFeaturePolicyInfo(
    const FeaturePolicyInfo& aContainerFeaturePolicyInfo) {
  mContainerFeaturePolicyInfo = Some(aContainerFeaturePolicyInfo);
}

nsIInterceptionInfo* LoadInfo::InterceptionInfo() { return mInterceptionInfo; }

void LoadInfo::SetInterceptionInfo(nsIInterceptionInfo* aInfo) {
  mInterceptionInfo = aInfo;
}

NS_IMETHODIMP
LoadInfo::GetHasInjectedCookieForCookieBannerHandling(
    bool* aHasInjectedCookieForCookieBannerHandling) {
  *aHasInjectedCookieForCookieBannerHandling =
      mHasInjectedCookieForCookieBannerHandling;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetHasInjectedCookieForCookieBannerHandling(
    bool aHasInjectedCookieForCookieBannerHandling) {
  mHasInjectedCookieForCookieBannerHandling =
      aHasInjectedCookieForCookieBannerHandling;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSchemelessInput(
    nsILoadInfo::SchemelessInputType* aSchemelessInput) {
  *aSchemelessInput = mSchemelessInput;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetSchemelessInput(
    nsILoadInfo::SchemelessInputType aSchemelessInput) {
  mSchemelessInput = aSchemelessInput;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetHttpsUpgradeTelemetry(
    nsILoadInfo::HTTPSUpgradeTelemetryType* aOutHttpsUpgradeTelemetry) {
  *aOutHttpsUpgradeTelemetry = mHttpsUpgradeTelemetry;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetHttpsUpgradeTelemetry(
    nsILoadInfo::HTTPSUpgradeTelemetryType aHttpsUpgradeTelemetry) {
  mHttpsUpgradeTelemetry = aHttpsUpgradeTelemetry;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetIsNewWindowTarget(bool* aIsNewWindowTarget) {
  *aIsNewWindowTarget = mIsNewWindowTarget;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetIsNewWindowTarget(bool aIsNewWindowTarget) {
  mIsNewWindowTarget = aIsNewWindowTarget;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::GetSkipHTTPSUpgrade(bool* aSkipHTTPSUpgrade) {
  *aSkipHTTPSUpgrade = mSkipHTTPSUpgrade;
  return NS_OK;
}

NS_IMETHODIMP
LoadInfo::SetSkipHTTPSUpgrade(bool aSkipHTTPSUpgrade) {
  mSkipHTTPSUpgrade = aSkipHTTPSUpgrade;
  return NS_OK;
}

}  // namespace mozilla::net
