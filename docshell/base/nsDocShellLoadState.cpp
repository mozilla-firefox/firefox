/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDocShellLoadState.h"
#include "nsIDocShell.h"
#include "nsDocShell.h"
#include "nsILoadInfo.h"
#include "nsIProtocolHandler.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURIFixup.h"
#include "nsIWebNavigation.h"
#include "nsIChannel.h"
#include "nsIURLQueryStringStripper.h"
#include "nsIXULRuntime.h"
#include "nsAboutProtocolUtils.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "ReferrerInfo.h"
#include "xpcpublic.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/LoadURIOptionsBinding.h"
#include "mozilla/dom/Navigation.h"
#include "mozilla/dom/NavigationUtils.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/net/DocumentLoadListener.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/glean/AntitrackingMetrics.h"

#include "mozilla/OriginAttributes.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/StaticPtr.h"

#include "mozilla/dom/PContent.h"

using namespace mozilla;
using namespace mozilla::dom;

// Global reference to the URI fixup service.
static mozilla::StaticRefPtr<nsIURIFixup> sURIFixup;

static bool ContentTriggeredURILoadIsAllowed(
    nsIURI* aURI, const nsACString& aEffectiveRemoteType) {
  MOZ_ASSERT(aEffectiveRemoteType != NOT_REMOTE_TYPE);
  MOZ_ASSERT(!aURI->SchemeIs("javascript"), "Should have been blocked already");

  // view-source: URIs are not linkable from web content, but the "View Page
  // Source" context menu has the content process itself load them,
  // so decide based on the inner URI instead.
  if (aURI->SchemeIs("view-source")) {
    nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(aURI);
    MOZ_ASSERT(nestedURI);

    nsCOMPtr<nsIURI> innerURI;
    return NS_SUCCEEDED(nestedURI->GetInnerURI(getter_AddRefs(innerURI))) &&
           ContentTriggeredURILoadIsAllowed(innerURI, aEffectiveRemoteType);
  }

  // A null principal is the least privileged principal there is, so any URI it
  // is allowed to link to may be loaded from any content process.
  nsCOMPtr<nsIPrincipal> genericNullPrincipal = NullPrincipal::Create({});

  nsCOMPtr<nsIScriptSecurityManager> secMan =
      do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID);

  if (NS_SUCCEEDED(secMan->CheckLoadURIWithPrincipal(
          genericNullPrincipal, aURI,
          nsIScriptSecurityManager::DISALLOW_SCRIPT |
              nsIScriptSecurityManager::DONT_REPORT_ERRORS,
          0))) {
    return true;
  }

  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(aURI, {});
  if (principal->GetIsNullPrincipal()) {
    // Only allow null principals from URIs that have the
    // URI_LOADABLE_BY_SUBSUMERS (i.e. blob:) flag. Other null principals likely
    // correspond to internal, unsafe-to-load in content, resources.
    bool loadableBySubsumers = false;
    if (NS_FAILED(NS_URIChainHasFlags(
            aURI, nsIProtocolHandler::URI_LOADABLE_BY_SUBSUMERS,
            &loadableBySubsumers))) {
      return false;
    }
    return loadableBySubsumers;
  }

  // Automation-Only: Allow loading of chrome://reftest/* URLs.
  if (aURI->SchemeIs("chrome") && xpc::IsInAutomation()) {
    nsAutoCString host;
    if (NS_SUCCEEDED(aURI->GetHost(host)) && host.EqualsLiteral("reftest")) {
      return true;
    }
  }

  return ValidatePrincipalCouldPotentiallyBeLoadedBy(
      principal, aEffectiveRemoteType,
      {ValidatePrincipalOptions::AllowNotLoadedOrigin});
}

nsDocShellLoadState::nsDocShellLoadState(nsIURI* aURI)
    : nsDocShellLoadState(aURI, nsContentUtils::GenerateLoadIdentifier()) {}

nsDocShellLoadState::nsDocShellLoadState(
    const DocShellLoadStateInit& aLoadState, mozilla::ipc::IProtocol* aActor,
    bool* aReadSuccess)
    : mNotifiedBeforeUnloadListeners(false),
      mLoadIdentifier(aLoadState.LoadIdentifier()) {
  // If we return early, we failed to read in the data.
  *aReadSuccess = false;
  if (!aLoadState.URI()) {
    MOZ_ASSERT_UNREACHABLE("Cannot create a LoadState with a null URI!");
    return;
  }

  mResultPrincipalURI = aLoadState.ResultPrincipalURI();
  mResultPrincipalURIIsSome = aLoadState.ResultPrincipalURIIsSome();
  mKeepResultPrincipalURIIfSet = aLoadState.KeepResultPrincipalURIIfSet();
  mLoadReplace = aLoadState.LoadReplace();
  mInheritPrincipal = aLoadState.InheritPrincipal();
  mPrincipalIsExplicit = aLoadState.PrincipalIsExplicit();
  mNotifiedBeforeUnloadListeners = aLoadState.NotifiedBeforeUnloadListeners();
  mForceAllowDataURI = aLoadState.ForceAllowDataURI();
  mIsExemptFromHTTPSFirstMode = aLoadState.IsExemptFromHTTPSFirstMode();
  mOriginalFrameSrc = aLoadState.OriginalFrameSrc();
  mShouldCheckForRecursion = aLoadState.ShouldCheckForRecursion();
  mIsFormSubmission = aLoadState.IsFormSubmission();
  mNeedsCompletelyLoadedDocument = aLoadState.NeedsCompletelyLoadedDocument();
  mHistoryBehavior = aLoadState.HistoryBehavior();
  mLoadType = aLoadState.LoadType();
  mTarget = aLoadState.Target();
  mTargetBrowsingContext = aLoadState.TargetBrowsingContext();
  mLoadFlags = aLoadState.LoadFlags();
  mInternalLoadFlags = aLoadState.InternalLoadFlags();
  mFirstParty = aLoadState.FirstParty();
  mHasValidUserGestureActivation = aLoadState.HasValidUserGestureActivation();
  mTextDirectiveUserActivation = aLoadState.TextDirectiveUserActivation();
  mAllowFocusMove = aLoadState.AllowFocusMove();
  mTypeHint = aLoadState.TypeHint();
  mFileName = aLoadState.FileName();
  mIsFromProcessingFrameAttributes =
      aLoadState.IsFromProcessingFrameAttributes();
  mReferrerInfo = aLoadState.ReferrerInfo();
  mURI = aLoadState.URI();
  mOriginalURI = aLoadState.OriginalURI();
  mSourceBrowsingContext = aLoadState.SourceBrowsingContext();
  mBaseURI = aLoadState.BaseURI();
  mTriggeringPrincipal = aLoadState.TriggeringPrincipal();
  mPrincipalToInherit = aLoadState.PrincipalToInherit();
  mPartitionedPrincipalToInherit = aLoadState.PartitionedPrincipalToInherit();
  mTriggeringSandboxFlags = aLoadState.TriggeringSandboxFlags();
  mTriggeringWindowId = aLoadState.TriggeringWindowId();
  mTriggeringStorageAccess = aLoadState.TriggeringStorageAccess();
  mTriggeringClassificationFlags = aLoadState.TriggeringClassificationFlags();
  mTriggeringRemoteType = aLoadState.TriggeringRemoteType();
  mSchemelessInput = aLoadState.SchemelessInput();
  mForceMediaDocument = aLoadState.forceMediaDocument();
  mHttpsUpgradeTelemetry = aLoadState.HttpsUpgradeTelemetry();
  mPolicyContainer = aLoadState.PolicyContainer();
  mOriginalURIString = aLoadState.OriginalURIString();
  mCancelContentJSEpoch = aLoadState.CancelContentJSEpoch();
  mPostDataStream = aLoadState.PostDataStream();
  mHeadersStream = aLoadState.HeadersStream();
  mSrcdocData = aLoadState.SrcdocData();
  mHasSpeculativeListener = aLoadState.HasSpeculativeListener();
  mIsMetaRefresh = aLoadState.IsMetaRefresh();
  if (aLoadState.loadingSessionHistoryInfo().isSome()) {
    mLoadingSessionHistoryInfo = MakeUnique<LoadingSessionHistoryInfo>(
        aLoadState.loadingSessionHistoryInfo().ref());
  }
  mUnstrippedURI = aLoadState.UnstrippedURI();
  mRemoteTypeOverride = aLoadState.RemoteTypeOverride();
  mIsCaptivePortalTab = aLoadState.IsCaptivePortalTab();
  mIsInitialAboutBlankHandlingProhibited =
      aLoadState.IsInitialAboutBlankHandlingProhibited();

  mNavigationAPIState = aLoadState.NavigationAPIState();

  // We know this was created remotely, as we just received it over IPC.
  mWasCreatedRemotely = true;

  // If we're in the parent process, potentially validate against a LoadState
  // which we sent to the source content process.
  if (XRE_IsParentProcess()) {
    ContentParent* cp = ActorDynCast<ContentParent>(aActor->ToplevelProtocol());
    if (!cp) {
      aActor->FatalError("nsDocShellLoadState must be received over PContent");
      return;
    }

    // If this load was sent down to the content process as a navigation
    // request, ensure it still matches the one we sent down.
    if (RefPtr<nsDocShellLoadState> originalState =
            cp->TakePendingLoadStateForId(mLoadIdentifier)) {
      if (const char* mismatch = ValidateWithOriginalState(originalState)) {
        aActor->FatalError(
            nsPrintfCString(
                "nsDocShellLoadState %s changed while in content process",
                mismatch)
                .get());
        return;
      }

      // The load state matches, steal parent-process-only fields which were on
      // the original state.
      mSpeculativeListener = originalState->TakeSpeculativeListener();
      MOZ_ASSERT(mHasSpeculativeListener == !!mSpeculativeListener);
    } else if (mTriggeringRemoteType != cp->GetRemoteType()) {
      // If we don't have a previous load to compare to, the content process
      // must be the triggering process.
      aActor->FatalError(
          "nsDocShellLoadState with invalid triggering remote type");
      return;
    }

    if (mTriggeringRemoteType != NOT_REMOTE_TYPE) {
      if (mURI->SchemeIs("javascript")) {
        aActor->FatalError("Illegal cross-process javascript: load attempt");
        return;
      }

      if (mRemoteTypeOverride.isSome()) {
        aActor->FatalError("RemoteTypeOverride can only be set by parent");
        return;
      }
    }

    const nsCString& effectiveRemoteType = GetEffectiveTriggeringRemoteType();
    if (effectiveRemoteType != NOT_REMOTE_TYPE &&
        !ContentTriggeredURILoadIsAllowed(mURI, effectiveRemoteType)) {
      nsAutoCString aboutModuleOrScheme;
      if (mURI->SchemeIs("about")) {
        (void)NS_GetAboutModuleName(mURI, aboutModuleOrScheme);
        aboutModuleOrScheme.InsertLiteral("about:", 0);
      } else {
        mURI->GetScheme(aboutModuleOrScheme);
        aboutModuleOrScheme.AppendLiteral(":");
      }
      nsCString remotePrefix(RemoteTypePrefix(effectiveRemoteType));
      aActor->FatalError(
          nsPrintfCString("Illegal load attempt of %s URL from %s",
                          aboutModuleOrScheme.get(), remotePrefix.get())
              .get());
      return;
    }

    // NOTE: Eventually this should probably be called on a LoadedOriginSet, but
    // we don't track this on the load state yet.
    if (!ValidatePrincipalCouldPotentiallyBeLoadedBy(
            mTriggeringPrincipal, effectiveRemoteType,
            {ValidatePrincipalOptions::AllowExpanded,
             ValidatePrincipalOptions::AlwaysAllowSystem,
             ValidatePrincipalOptions::AllowNotLoadedOrigin})) {
      aActor->FatalError(
          "nsDocShellLoadState with invalid triggering principal");
      return;
    }
    if (!ValidatePrincipalCouldPotentiallyBeLoadedBy(
            mPrincipalToInherit, effectiveRemoteType,
            {ValidatePrincipalOptions::AllowNullPtr,
             ValidatePrincipalOptions::AllowNotLoadedOrigin})) {
      aActor->FatalError("nsDocShellLoadState with invalid principalToInherit");
      return;
    }
  }

  if (!mSrcdocData.IsVoid() && !mURI->SchemeIs("view-source") &&
      !NS_IsAboutSrcdoc(mURI)) {
    aActor->FatalError("nsDocShellLoadState with invalid srcdoc state");
    return;
  }

  // We successfully read in the data - return a success value.
  *aReadSuccess = true;
}

nsDocShellLoadState::nsDocShellLoadState(const nsDocShellLoadState& aOther)
    : mReferrerInfo(aOther.mReferrerInfo),
      mURI(aOther.mURI),
      mOriginalURI(aOther.mOriginalURI),
      mResultPrincipalURI(aOther.mResultPrincipalURI),
      mResultPrincipalURIIsSome(aOther.mResultPrincipalURIIsSome),
      mTriggeringPrincipal(aOther.mTriggeringPrincipal),
      mTriggeringSandboxFlags(aOther.mTriggeringSandboxFlags),
      mTriggeringWindowId(aOther.mTriggeringWindowId),
      mTriggeringStorageAccess(aOther.mTriggeringStorageAccess),
      mTriggeringClassificationFlags(aOther.mTriggeringClassificationFlags),
      mPolicyContainer(aOther.mPolicyContainer),
      mKeepResultPrincipalURIIfSet(aOther.mKeepResultPrincipalURIIfSet),
      mLoadReplace(aOther.mLoadReplace),
      mInheritPrincipal(aOther.mInheritPrincipal),
      mPrincipalIsExplicit(aOther.mPrincipalIsExplicit),
      mNotifiedBeforeUnloadListeners(aOther.mNotifiedBeforeUnloadListeners),
      mPrincipalToInherit(aOther.mPrincipalToInherit),
      mPartitionedPrincipalToInherit(aOther.mPartitionedPrincipalToInherit),
      mForceAllowDataURI(aOther.mForceAllowDataURI),
      mIsExemptFromHTTPSFirstMode(aOther.mIsExemptFromHTTPSFirstMode),
      mHttpsFirstDowngradeData(aOther.GetHttpsFirstDowngradeData()),
      mOriginalFrameSrc(aOther.mOriginalFrameSrc),
      mShouldCheckForRecursion(aOther.mShouldCheckForRecursion),
      mIsFormSubmission(aOther.mIsFormSubmission),
      mNeedsCompletelyLoadedDocument(aOther.mNeedsCompletelyLoadedDocument),
      mHistoryBehavior(aOther.mHistoryBehavior),
      mLoadType(aOther.mLoadType),
      mSHEntry(aOther.mSHEntry),
      mTarget(aOther.mTarget),
      mTargetBrowsingContext(aOther.mTargetBrowsingContext),
      mPostDataStream(aOther.mPostDataStream),
      mHeadersStream(aOther.mHeadersStream),
      mSrcdocData(aOther.mSrcdocData),
      mSourceBrowsingContext(aOther.mSourceBrowsingContext),
      mBaseURI(aOther.mBaseURI),
      mLoadFlags(aOther.mLoadFlags),
      mInternalLoadFlags(aOther.mInternalLoadFlags),
      mFirstParty(aOther.mFirstParty),
      mHasValidUserGestureActivation(aOther.mHasValidUserGestureActivation),
      mTextDirectiveUserActivation(aOther.mTextDirectiveUserActivation),
      mAllowFocusMove(aOther.mAllowFocusMove),
      mTypeHint(aOther.mTypeHint),
      mFileName(aOther.mFileName),
      mIsFromProcessingFrameAttributes(aOther.mIsFromProcessingFrameAttributes),
      mPendingRedirectedChannel(aOther.mPendingRedirectedChannel),
      mOriginalURIString(aOther.mOriginalURIString),
      mCancelContentJSEpoch(aOther.mCancelContentJSEpoch),
      mLoadIdentifier(aOther.mLoadIdentifier),
      mIsMetaRefresh(aOther.mIsMetaRefresh),
      mWasCreatedRemotely(aOther.mWasCreatedRemotely),
      mUnstrippedURI(aOther.mUnstrippedURI),
      mRemoteTypeOverride(aOther.mRemoteTypeOverride),
      mTriggeringRemoteType(aOther.mTriggeringRemoteType),
      mSchemelessInput(aOther.mSchemelessInput),
      mForceMediaDocument(aOther.mForceMediaDocument),
      mHttpsUpgradeTelemetry(aOther.mHttpsUpgradeTelemetry),
      mNavigationAPIState(aOther.mNavigationAPIState),
      mIsInitialAboutBlankHandlingProhibited(
          aOther.mIsInitialAboutBlankHandlingProhibited) {
  MOZ_DIAGNOSTIC_ASSERT(
      XRE_IsParentProcess(),
      "Cloning a nsDocShellLoadState with the same load identifier is only "
      "allowed in the parent process, as it could break triggering remote type "
      "tracking in content.");
  MOZ_DIAGNOSTIC_ASSERT(
      !aOther.mHasSpeculativeListener && !aOther.mSpeculativeListener,
      "Cannot copy a load state with a speculative listener");
  if (aOther.mLoadingSessionHistoryInfo) {
    mLoadingSessionHistoryInfo = MakeUnique<LoadingSessionHistoryInfo>(
        *aOther.mLoadingSessionHistoryInfo);
  }
}

nsDocShellLoadState::nsDocShellLoadState(nsIURI* aURI, uint64_t aLoadIdentifier)
    : mURI(aURI),
      mResultPrincipalURIIsSome(false),
      mTriggeringSandboxFlags(0),
      mTriggeringWindowId(0),
      mTriggeringStorageAccess(false),
      mTriggeringClassificationFlags({0, 0}),
      mKeepResultPrincipalURIIfSet(false),
      mLoadReplace(false),
      mInheritPrincipal(false),
      mPrincipalIsExplicit(false),
      mNotifiedBeforeUnloadListeners(false),
      mForceAllowDataURI(false),
      mIsExemptFromHTTPSFirstMode(false),
      mOriginalFrameSrc(false),
      mShouldCheckForRecursion(false),
      mIsFormSubmission(false),
      mNeedsCompletelyLoadedDocument(false),
      mHistoryBehavior(Nothing()),
      mLoadType(LOAD_NORMAL),
      mSrcdocData(VoidString()),
      mLoadFlags(0),
      mInternalLoadFlags(0),
      mFirstParty(false),
      mHasValidUserGestureActivation(false),
      mAllowFocusMove(false),
      mTypeHint(VoidCString()),
      mFileName(VoidString()),
      mIsFromProcessingFrameAttributes(false),
      mLoadIdentifier(aLoadIdentifier),
      mIsMetaRefresh(false),
      mWasCreatedRemotely(false),
      mTriggeringRemoteType(XRE_IsContentProcess()
                                ? ContentChild::GetSingleton()->GetRemoteType()
                                : NOT_REMOTE_TYPE),
      mSchemelessInput(nsILoadInfo::SchemelessInputTypeUnset),
      mIsInitialAboutBlankHandlingProhibited(false) {
  MOZ_ASSERT(aURI, "Cannot create a LoadState with a null URI!");

  // For https telemetry we set a flag indicating whether the load is https.
  // There are some corner cases, e.g. view-source and also about: pages.
  // about: pages, when hitting the network, always redirect to https.
  // Since we record https telemetry only within nsHTTPSChannel, it's fine
  // to set the flag here.
  nsCOMPtr<nsIURI> innerURI = NS_GetInnermostURI(aURI);
  if (innerURI->SchemeIs("https") || innerURI->SchemeIs("about")) {
    mHttpsUpgradeTelemetry = nsILoadInfo::ALREADY_HTTPS;
  } else {
    mHttpsUpgradeTelemetry = nsILoadInfo::NO_UPGRADE;
  }
}

nsDocShellLoadState::~nsDocShellLoadState() {
  if (mWasCreatedRemotely && XRE_IsContentProcess() &&
      ContentChild::GetSingleton()->CanSend()) {
    ContentChild::GetSingleton()->SendCleanupPendingLoadState(mLoadIdentifier);
  }
  if (mSpeculativeListener) {
    mSpeculativeListener->CleanupParentLoadAttempt();
  }
}

nsresult nsDocShellLoadState::CreateFromPendingChannel(
    nsIChannel* aPendingChannel, uint64_t aLoadIdentifier,
    uint64_t aRegistrarId, nsDocShellLoadState** aResult) {
  // Create the nsDocShellLoadState object with default state pulled from the
  // passed-in channel.
  nsCOMPtr<nsIURI> uri;
  nsresult rv = aPendingChannel->GetURI(getter_AddRefs(uri));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(uri, aLoadIdentifier);
  loadState->mPendingRedirectedChannel = aPendingChannel;
  loadState->mChannelRegistrarId = aRegistrarId;

  // Pull relevant state from the channel, and store it on the
  // nsDocShellLoadState.
  nsCOMPtr<nsIURI> originalUri;
  rv = aPendingChannel->GetOriginalURI(getter_AddRefs(originalUri));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  loadState->SetOriginalURI(originalUri);

  nsCOMPtr<nsILoadInfo> loadInfo = aPendingChannel->LoadInfo();
  loadState->SetTriggeringPrincipal(loadInfo->TriggeringPrincipal());

  // Return the newly created loadState.
  loadState.forget(aResult);
  return NS_OK;
}

static uint32_t WebNavigationFlagsToFixupFlags(nsIURI* aURI,
                                               const nsACString& aURIString,
                                               uint32_t aNavigationFlags) {
  if (aURI) {
    aNavigationFlags &= ~nsIWebNavigation::LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP;
  }
  uint32_t fixupFlags = nsIURIFixup::FIXUP_FLAG_NONE;
  if (aNavigationFlags & nsIWebNavigation::LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP) {
    fixupFlags |= nsIURIFixup::FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP;
  }
  if (aNavigationFlags & nsIWebNavigation::LOAD_FLAGS_FIXUP_SCHEME_TYPOS) {
    fixupFlags |= nsIURIFixup::FIXUP_FLAG_FIX_SCHEME_TYPOS;
  }
  return fixupFlags;
};

nsresult nsDocShellLoadState::CreateFromLoadURIOptions(
    BrowsingContext* aBrowsingContext, const nsAString& aURI,
    const LoadURIOptions& aLoadURIOptions, nsDocShellLoadState** aResult) {
  uint32_t loadFlags = aLoadURIOptions.mLoadFlags;

  NS_ASSERTION(
      (loadFlags & nsDocShell::INTERNAL_LOAD_FLAGS_LOADURI_SETUP_FLAGS) == 0,
      "Unexpected flags");

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_OK;

  NS_ConvertUTF16toUTF8 uriString(aURI);
  // Cleanup the empty spaces that might be on each end.
  uriString.Trim(" ");
  // Eliminate embedded newlines, which single-line text fields now allow:
  uriString.StripCRLF();
  NS_ENSURE_TRUE(!uriString.IsEmpty(), NS_ERROR_FAILURE);

  // Just create a URI and see what happens...
  rv = NS_NewURI(getter_AddRefs(uri), uriString);
  bool fixup = true;
  if (NS_SUCCEEDED(rv) && uri &&
      (uri->SchemeIs("about") || uri->SchemeIs("chrome"))) {
    // Avoid third party fixup as a performance optimization.
    loadFlags &= ~nsIWebNavigation::LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP;
    fixup = false;
  } else if (!sURIFixup && !XRE_IsContentProcess()) {
    nsCOMPtr<nsIURIFixup> uriFixup = components::URIFixup::Service();
    if (uriFixup) {
      sURIFixup = uriFixup;
      ClearOnShutdown(&sURIFixup);
    } else {
      fixup = false;
    }
  }

  nsAutoString searchProviderId, keyword;
  RefPtr<nsIInputStream> fixupStream;
  if (fixup) {
    uint32_t fixupFlags =
        WebNavigationFlagsToFixupFlags(uri, uriString, loadFlags);

    // If we don't allow keyword lookups for this URL string, make sure to
    // update loadFlags to indicate this as well.
    if (!(fixupFlags & nsIURIFixup::FIXUP_FLAG_ALLOW_KEYWORD_LOOKUP)) {
      loadFlags &= ~nsIWebNavigation::LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP;
    }
    // Ensure URIFixup will use the right search engine in Private Browsing.
    if (aBrowsingContext->UsePrivateBrowsing()) {
      fixupFlags |= nsIURIFixup::FIXUP_FLAG_PRIVATE_CONTEXT;
    }

    if (!XRE_IsContentProcess()) {
      nsCOMPtr<nsIURIFixupInfo> fixupInfo;
      sURIFixup->GetFixupURIInfo(uriString, fixupFlags,
                                 getter_AddRefs(fixupInfo));
      if (fixupInfo) {
        // We could fix the uri, clear NS_ERROR_MALFORMED_URI.
        rv = NS_OK;
        fixupInfo->GetPreferredURI(getter_AddRefs(uri));
        fixupInfo->SetConsumer(aBrowsingContext);
        fixupInfo->GetKeywordProviderId(searchProviderId);
        fixupInfo->GetKeywordAsSent(keyword);
        // GetFixupURIInfo only returns a post data stream if it succeeded
        // and changed the URI, in which case we should override the
        // passed-in post data by passing this as an override arg to
        // our internal method.
        fixupInfo->GetPostData(getter_AddRefs(fixupStream));

        if (fixupInfo &&
            loadFlags & nsIWebNavigation::LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP) {
          nsCOMPtr<nsIObserverService> serv = services::GetObserverService();
          if (serv) {
            serv->NotifyObservers(fixupInfo, "keyword-uri-fixup",
                                  PromiseFlatString(aURI).get());
          }
        }
        nsDocShell::MaybeNotifyKeywordSearchLoading(searchProviderId, keyword);
      }
    }
  }

  if (rv == NS_ERROR_MALFORMED_URI) {
    MOZ_ASSERT(!uri);
    return rv;
  }

  if (NS_FAILED(rv) || !uri) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsDocShellLoadState> loadState;
  rv = CreateFromLoadURIOptions(
      aBrowsingContext, uri, aLoadURIOptions, loadFlags,
      fixupStream ? fixupStream : aLoadURIOptions.mPostData,
      getter_AddRefs(loadState));
  NS_ENSURE_SUCCESS(rv, rv);
  loadState->SetOriginalURIString(uriString);
  loadState.forget(aResult);
  return NS_OK;
}

nsresult nsDocShellLoadState::CreateFromLoadURIOptions(
    BrowsingContext* aBrowsingContext, nsIURI* aURI,
    const LoadURIOptions& aLoadURIOptions, nsDocShellLoadState** aResult) {
  return CreateFromLoadURIOptions(aBrowsingContext, aURI, aLoadURIOptions,
                                  aLoadURIOptions.mLoadFlags,
                                  aLoadURIOptions.mPostData, aResult);
}

nsresult nsDocShellLoadState::CreateFromLoadURIOptions(
    BrowsingContext* aBrowsingContext, nsIURI* aURI,
    const LoadURIOptions& aLoadURIOptions, uint32_t aLoadFlagsOverride,
    nsIInputStream* aPostDataOverride, nsDocShellLoadState** aResult) {
  nsresult rv = NS_OK;
  uint32_t loadFlags = aLoadFlagsOverride;
  RefPtr<nsIInputStream> postData = aPostDataOverride;
  uint64_t available;
  if (postData) {
    rv = postData->Available(&available);
    NS_ENSURE_SUCCESS(rv, rv);
    if (available == 0) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  if (aLoadURIOptions.mHeaders) {
    rv = aLoadURIOptions.mHeaders->Available(&available);
    NS_ENSURE_SUCCESS(rv, rv);
    if (available == 0) {
      return NS_ERROR_INVALID_ARG;
    }
  }

  bool forceAllowDataURI =
      loadFlags & nsIWebNavigation::LOAD_FLAGS_FORCE_ALLOW_DATA_URI;

  // Don't pass certain flags that aren't needed and end up confusing
  // ConvertLoadTypeToDocShellInfoLoadType.  We do need to ensure that they are
  // passed to LoadURI though, since it uses them.
  uint32_t extraFlags = (loadFlags & EXTRA_LOAD_FLAGS);
  loadFlags &= ~EXTRA_LOAD_FLAGS;

  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(aURI);
  loadState->SetReferrerInfo(aLoadURIOptions.mReferrerInfo);

  loadState->SetLoadType(MAKE_LOAD_TYPE(LOAD_NORMAL, loadFlags));

  loadState->SetLoadFlags(extraFlags);
  loadState->SetFirstParty(true);
  loadState->SetHasValidUserGestureActivation(
      aLoadURIOptions.mHasValidUserGestureActivation);
  loadState->SetTextDirectiveUserActivation(
      aLoadURIOptions.mTextDirectiveUserActivation);
  loadState->SetTriggeringSandboxFlags(aLoadURIOptions.mTriggeringSandboxFlags);
  loadState->SetTriggeringWindowId(aLoadURIOptions.mTriggeringWindowId);
  loadState->SetTriggeringStorageAccess(
      aLoadURIOptions.mTriggeringStorageAccess);
  // The load is assumed to be first-party, so the triggering classification
  // should be both zero.
  loadState->SetTriggeringClassificationFlags({0, 0});
  loadState->SetPostDataStream(postData);
  loadState->SetHeadersStream(aLoadURIOptions.mHeaders);
  loadState->SetBaseURI(aLoadURIOptions.mBaseURI);
  loadState->SetTriggeringPrincipal(aLoadURIOptions.mTriggeringPrincipal);
  loadState->SetPolicyContainer(aLoadURIOptions.mPolicyContainer);
  loadState->SetForceAllowDataURI(forceAllowDataURI);
  if (aLoadURIOptions.mCancelContentJSEpoch) {
    loadState->SetCancelContentJSEpoch(aLoadURIOptions.mCancelContentJSEpoch);
  }

  if (aLoadURIOptions.mTriggeringRemoteType.WasPassed()) {
    if (XRE_IsParentProcess()) {
      loadState->SetTriggeringRemoteType(
          aLoadURIOptions.mTriggeringRemoteType.Value());
    } else if (ContentChild::GetSingleton()->GetRemoteType() !=
               aLoadURIOptions.mTriggeringRemoteType.Value()) {
      NS_WARNING("Invalid TriggeringRemoteType from LoadURIOptions in content");
      return NS_ERROR_INVALID_ARG;
    }
  }

  if (aLoadURIOptions.mRemoteTypeOverride.WasPassed()) {
    loadState->SetRemoteTypeOverride(
        aLoadURIOptions.mRemoteTypeOverride.Value());
  }

  loadState->SetSchemelessInput(static_cast<nsILoadInfo::SchemelessInputType>(
      aLoadURIOptions.mSchemelessInput));

  loadState->SetForceMediaDocument(aLoadURIOptions.mForceMediaDocument);
  loadState->SetAppLinkLaunchType(aLoadURIOptions.mAppLinkLaunchType);
  loadState->SetIsCaptivePortalTab(aLoadURIOptions.mIsCaptivePortalTab);

  loadState.forget(aResult);
  return NS_OK;
}

nsIReferrerInfo* nsDocShellLoadState::GetReferrerInfo() const {
  return mReferrerInfo;
}

void nsDocShellLoadState::SetReferrerInfo(nsIReferrerInfo* aReferrerInfo) {
  mReferrerInfo = aReferrerInfo;
}

nsIURI* nsDocShellLoadState::URI() const { return mURI; }

void nsDocShellLoadState::SetURI(nsIURI* aURI) { mURI = aURI; }

nsIURI* nsDocShellLoadState::OriginalURI() const { return mOriginalURI; }

void nsDocShellLoadState::SetOriginalURI(nsIURI* aOriginalURI) {
  mOriginalURI = aOriginalURI;
}

nsIURI* nsDocShellLoadState::ResultPrincipalURI() const {
  return mResultPrincipalURI;
}

void nsDocShellLoadState::SetResultPrincipalURI(nsIURI* aResultPrincipalURI) {
  mResultPrincipalURI = aResultPrincipalURI;
}

bool nsDocShellLoadState::ResultPrincipalURIIsSome() const {
  return mResultPrincipalURIIsSome;
}

void nsDocShellLoadState::SetResultPrincipalURIIsSome(bool aIsSome) {
  mResultPrincipalURIIsSome = aIsSome;
}

bool nsDocShellLoadState::KeepResultPrincipalURIIfSet() const {
  return mKeepResultPrincipalURIIfSet;
}

void nsDocShellLoadState::SetKeepResultPrincipalURIIfSet(bool aKeep) {
  mKeepResultPrincipalURIIfSet = aKeep;
}

bool nsDocShellLoadState::LoadReplace() const { return mLoadReplace; }

void nsDocShellLoadState::SetLoadReplace(bool aLoadReplace) {
  mLoadReplace = aLoadReplace;
}

nsIPrincipal* nsDocShellLoadState::TriggeringPrincipal() const {
  return mTriggeringPrincipal;
}

void nsDocShellLoadState::SetTriggeringPrincipal(
    nsIPrincipal* aTriggeringPrincipal) {
  mTriggeringPrincipal = aTriggeringPrincipal;
}

nsIPrincipal* nsDocShellLoadState::PrincipalToInherit() const {
  return mPrincipalToInherit;
}

void nsDocShellLoadState::SetPrincipalToInherit(
    nsIPrincipal* aPrincipalToInherit) {
  mPrincipalToInherit = aPrincipalToInherit;
}

nsIPrincipal* nsDocShellLoadState::PartitionedPrincipalToInherit() const {
  return mPartitionedPrincipalToInherit;
}

void nsDocShellLoadState::SetPartitionedPrincipalToInherit(
    nsIPrincipal* aPartitionedPrincipalToInherit) {
  mPartitionedPrincipalToInherit = aPartitionedPrincipalToInherit;
}

void nsDocShellLoadState::SetPolicyContainer(
    nsIPolicyContainer* aPolicyContainer) {
  mPolicyContainer = aPolicyContainer;
}

nsIPolicyContainer* nsDocShellLoadState::PolicyContainer() const {
  return mPolicyContainer;
}

void nsDocShellLoadState::SetTriggeringSandboxFlags(uint32_t flags) {
  mTriggeringSandboxFlags = flags;
}

uint32_t nsDocShellLoadState::TriggeringSandboxFlags() const {
  return mTriggeringSandboxFlags;
}

void nsDocShellLoadState::SetTriggeringWindowId(uint64_t aTriggeringWindowId) {
  mTriggeringWindowId = aTriggeringWindowId;
}

uint64_t nsDocShellLoadState::TriggeringWindowId() const {
  return mTriggeringWindowId;
}

void nsDocShellLoadState::SetTriggeringStorageAccess(
    bool aTriggeringStorageAccess) {
  mTriggeringStorageAccess = aTriggeringStorageAccess;
}

bool nsDocShellLoadState::TriggeringStorageAccess() const {
  return mTriggeringStorageAccess;
}

mozilla::net::ClassificationFlags
nsDocShellLoadState::TriggeringClassificationFlags() const {
  return mTriggeringClassificationFlags;
}

void nsDocShellLoadState::SetTriggeringClassificationFlags(
    mozilla::net::ClassificationFlags aFlags) {
  mTriggeringClassificationFlags = aFlags;
}

bool nsDocShellLoadState::InheritPrincipal() const { return mInheritPrincipal; }

void nsDocShellLoadState::SetInheritPrincipal(bool aInheritPrincipal) {
  mInheritPrincipal = aInheritPrincipal;
}

bool nsDocShellLoadState::PrincipalIsExplicit() const {
  return mPrincipalIsExplicit;
}

void nsDocShellLoadState::SetPrincipalIsExplicit(bool aPrincipalIsExplicit) {
  mPrincipalIsExplicit = aPrincipalIsExplicit;
}

bool nsDocShellLoadState::NotifiedBeforeUnloadListeners() const {
  return mNotifiedBeforeUnloadListeners;
}

void nsDocShellLoadState::SetNotifiedBeforeUnloadListeners(
    bool aNotifiedBeforeUnloadListeners) {
  mNotifiedBeforeUnloadListeners = aNotifiedBeforeUnloadListeners;
}

bool nsDocShellLoadState::ForceAllowDataURI() const {
  return mForceAllowDataURI;
}

void nsDocShellLoadState::SetForceAllowDataURI(bool aForceAllowDataURI) {
  mForceAllowDataURI = aForceAllowDataURI;
}

bool nsDocShellLoadState::IsExemptFromHTTPSFirstMode() const {
  return mIsExemptFromHTTPSFirstMode;
}

void nsDocShellLoadState::SetIsExemptFromHTTPSFirstMode(
    bool aIsExemptFromHTTPSFirstMode) {
  mIsExemptFromHTTPSFirstMode = aIsExemptFromHTTPSFirstMode;
}

RefPtr<HTTPSFirstDowngradeData>
nsDocShellLoadState::GetHttpsFirstDowngradeData() const {
  return mHttpsFirstDowngradeData;
}

void nsDocShellLoadState::SetHttpsFirstDowngradeData(
    RefPtr<HTTPSFirstDowngradeData> const& aHttpsFirstTelemetryData) {
  mHttpsFirstDowngradeData = aHttpsFirstTelemetryData;
}

bool nsDocShellLoadState::OriginalFrameSrc() const { return mOriginalFrameSrc; }

void nsDocShellLoadState::SetOriginalFrameSrc(bool aOriginalFrameSrc) {
  mOriginalFrameSrc = aOriginalFrameSrc;
}

bool nsDocShellLoadState::ShouldCheckForRecursion() const {
  return mShouldCheckForRecursion;
}

void nsDocShellLoadState::SetShouldCheckForRecursion(
    bool aShouldCheckForRecursion) {
  mShouldCheckForRecursion = aShouldCheckForRecursion;
}

bool nsDocShellLoadState::IsFormSubmission() const { return mIsFormSubmission; }

void nsDocShellLoadState::SetIsFormSubmission(bool aIsFormSubmission) {
  mIsFormSubmission = aIsFormSubmission;
}

bool nsDocShellLoadState::NeedsCompletelyLoadedDocument() const {
  return mNeedsCompletelyLoadedDocument;
}

void nsDocShellLoadState::SetNeedsCompletelyLoadedDocument(
    bool aNeedsCompletelyLoadedDocument) {
  mNeedsCompletelyLoadedDocument = aNeedsCompletelyLoadedDocument;
}

Maybe<mozilla::dom::NavigationHistoryBehavior>
nsDocShellLoadState::HistoryBehavior() const {
  return mHistoryBehavior;
}

void nsDocShellLoadState::SetHistoryBehavior(
    mozilla::dom::NavigationHistoryBehavior aHistoryBehavior) {
  mHistoryBehavior = Some(aHistoryBehavior);
}

void nsDocShellLoadState::ResetHistoryBehavior() {
  mHistoryBehavior = Nothing();
}

uint32_t nsDocShellLoadState::LoadType() const { return mLoadType; }

void nsDocShellLoadState::SetLoadType(uint32_t aLoadType) {
  mLoadType = aLoadType;
}

mozilla::dom::UserNavigationInvolvement
nsDocShellLoadState::UserNavigationInvolvement() const {
  return mUserNavigationInvolvement;
}

void nsDocShellLoadState::SetUserNavigationInvolvement(
    mozilla::dom::UserNavigationInvolvement aUserNavigationInvolvement) {
  mUserNavigationInvolvement = aUserNavigationInvolvement;
}

SessionHistoryEntry* nsDocShellLoadState::SHEntry() const { return mSHEntry; }

void nsDocShellLoadState::SetSHEntry(SessionHistoryEntry* aSHEntry) {
  mSHEntry = aSHEntry;
  if (aSHEntry) {
    mLoadingSessionHistoryInfo =
        MakeUnique<LoadingSessionHistoryInfo>(aSHEntry);
    mLoadingSessionHistoryInfo->mTriggeringNavigationType =
        NavigationUtils::NavigationTypeFromLoadType(LoadType());
    MOZ_ASSERT(mLoadingSessionHistoryInfo->mTriggeringNavigationType);
  } else {
    mLoadingSessionHistoryInfo = nullptr;
  }
}

void nsDocShellLoadState::SetPreviousEntryForActivation(nsISHEntry* aSHEntry) {
  MOZ_DIAGNOSTIC_ASSERT(mSHEntry);
  nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(aSHEntry);
  if (mLoadingSessionHistoryInfo) {
    mLoadingSessionHistoryInfo->mPreviousEntry =
        Some(PreviousSessionHistoryInfo(she->Info()));
  }
}

void nsDocShellLoadState::SetPreviousEntryForActivation(
    const Maybe<PreviousSessionHistoryInfo>& aInfo) {
  if (mLoadingSessionHistoryInfo) {
    mLoadingSessionHistoryInfo->mPreviousEntry = aInfo;
  }
}

void nsDocShellLoadState::SetLoadingSessionHistoryInfo(
    const mozilla::dom::LoadingSessionHistoryInfo& aLoadingInfo) {
  SetLoadingSessionHistoryInfo(
      MakeUnique<mozilla::dom::LoadingSessionHistoryInfo>(aLoadingInfo));
}

void nsDocShellLoadState::SetLoadingSessionHistoryInfo(
    mozilla::UniquePtr<mozilla::dom::LoadingSessionHistoryInfo> aLoadingInfo) {
  mLoadingSessionHistoryInfo = std::move(aLoadingInfo);
}

const mozilla::dom::LoadingSessionHistoryInfo*
nsDocShellLoadState::GetLoadingSessionHistoryInfo() const {
  return mLoadingSessionHistoryInfo.get();
}

void nsDocShellLoadState::SetLoadIsFromSessionHistory(
    int32_t aOffset, bool aLoadingCurrentEntry) {
  if (mLoadingSessionHistoryInfo) {
    mLoadingSessionHistoryInfo->mLoadIsFromSessionHistory = true;
    mLoadingSessionHistoryInfo->mOffset = aOffset;
    mLoadingSessionHistoryInfo->mLoadingCurrentEntry = aLoadingCurrentEntry;
  }
}

void nsDocShellLoadState::ClearLoadIsFromSessionHistory() {
  if (mLoadingSessionHistoryInfo) {
    mLoadingSessionHistoryInfo->mLoadIsFromSessionHistory = false;
  }
  mSHEntry = nullptr;
}

bool nsDocShellLoadState::LoadIsFromSessionHistory() const {
  return mLoadingSessionHistoryInfo
             ? mLoadingSessionHistoryInfo->mLoadIsFromSessionHistory
             : !!mSHEntry;
}

void nsDocShellLoadState::MaybeStripTrackerQueryStrings(
    BrowsingContext* aContext) {
  MOZ_ASSERT(aContext);

  // Return early if the triggering principal doesn't exist. This could happen
  // when loading a URL by using a browsing context in the Browser Toolbox.
  if (!TriggeringPrincipal()) {
    return;
  }

  // We don't need to strip for sub frames because the query string has been
  // stripped in the top-level content. Also, we don't apply stripping if it
  // is triggered by addons.
  //
  // Note that we don't need to do the stripping if the load state will have a
  // speculative listener in the parent process. This means that this has been
  // loaded speculatively in the parent process before and the stripping was
  // happening by then.
  if (mHasSpeculativeListener || !aContext->IsTopContent() ||
      BasePrincipal::Cast(TriggeringPrincipal())->AddonPolicy()) {
    return;
  }

  // We don't strip the URI if it's the same-site navigation. Note that we will
  // consider the system principal triggered load as third-party in case the
  // user copies and pastes a URL which has tracking query parameters or an
  // loading from external applications, such as clicking a link in an email
  // client.
  bool isThirdPartyURI = false;
  if (!TriggeringPrincipal()->IsSystemPrincipal() &&
      (NS_FAILED(
           TriggeringPrincipal()->IsThirdPartyURI(URI(), &isThirdPartyURI)) ||
       !isThirdPartyURI)) {
    return;
  }

  glean::contentblocking::query_stripping_count
      .EnumGet(glean::contentblocking::QueryStrippingCountLabel::eNavigation)
      .Add();

  nsCOMPtr<nsIURI> strippedURI;

  nsresult rv;
  nsCOMPtr<nsIURLQueryStringStripper> queryStripper =
      components::URLQueryStringStripper::Service(&rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  uint32_t numStripped;

  queryStripper->Strip(URI(), aContext->UsePrivateBrowsing(),
                       getter_AddRefs(strippedURI), &numStripped);
  if (numStripped) {
    if (!mUnstrippedURI) {
      mUnstrippedURI = URI();
    }
    SetURI(strippedURI);

    glean::contentblocking::query_stripping_count
        .EnumGet(glean::contentblocking::QueryStrippingCountLabel::
                     eStripfornavigation)
        .Add();
    glean::contentblocking::query_stripping_param_count.AccumulateSingleSample(
        numStripped);
  }

#ifdef DEBUG
  // Make sure that unstripped URI is the same as URI() but only the query
  // string could be different.
  if (mUnstrippedURI) {
    nsCOMPtr<nsIURI> uri;
    (void)queryStripper->Strip(mUnstrippedURI, aContext->UsePrivateBrowsing(),
                               getter_AddRefs(uri), &numStripped);
    bool equals = false;
    (void)URI()->Equals(uri, &equals);
    MOZ_ASSERT(equals);
  }
#endif
}

const nsString& nsDocShellLoadState::Target() const { return mTarget; }

void nsDocShellLoadState::SetTarget(const nsAString& aTarget) {
  mTarget = aTarget;
}

nsIInputStream* nsDocShellLoadState::PostDataStream() const {
  return mPostDataStream;
}

void nsDocShellLoadState::SetPostDataStream(nsIInputStream* aStream) {
  mPostDataStream = aStream;
}

nsIInputStream* nsDocShellLoadState::HeadersStream() const {
  return mHeadersStream;
}

void nsDocShellLoadState::SetHeadersStream(nsIInputStream* aHeadersStream) {
  mHeadersStream = aHeadersStream;
}

const nsString& nsDocShellLoadState::SrcdocData() const { return mSrcdocData; }

void nsDocShellLoadState::SetSrcdocData(const nsAString& aSrcdocData) {
  mSrcdocData = aSrcdocData;
}

void nsDocShellLoadState::SetSourceBrowsingContext(
    BrowsingContext* aSourceBrowsingContext) {
  mSourceBrowsingContext = aSourceBrowsingContext;
}

void nsDocShellLoadState::SetTargetBrowsingContext(
    BrowsingContext* aTargetBrowsingContext) {
  mTargetBrowsingContext = aTargetBrowsingContext;
}

nsIURI* nsDocShellLoadState::BaseURI() const { return mBaseURI; }

void nsDocShellLoadState::SetBaseURI(nsIURI* aBaseURI) { mBaseURI = aBaseURI; }

void nsDocShellLoadState::GetMaybeResultPrincipalURI(
    mozilla::Maybe<nsCOMPtr<nsIURI>>& aRPURI) const {
  bool isSome = ResultPrincipalURIIsSome();
  aRPURI.reset();

  if (!isSome) {
    return;
  }

  nsCOMPtr<nsIURI> uri = ResultPrincipalURI();
  aRPURI.emplace(std::move(uri));
}

void nsDocShellLoadState::SetMaybeResultPrincipalURI(
    mozilla::Maybe<nsCOMPtr<nsIURI>> const& aRPURI) {
  SetResultPrincipalURI(aRPURI.refOr(nullptr));
  SetResultPrincipalURIIsSome(aRPURI.isSome());
}

uint32_t nsDocShellLoadState::LoadFlags() const { return mLoadFlags; }

void nsDocShellLoadState::SetLoadFlags(uint32_t aLoadFlags) {
  mLoadFlags = aLoadFlags;
}

void nsDocShellLoadState::SetLoadFlag(uint32_t aFlag) { mLoadFlags |= aFlag; }

void nsDocShellLoadState::UnsetLoadFlag(uint32_t aFlag) {
  mLoadFlags &= ~aFlag;
}

bool nsDocShellLoadState::HasLoadFlags(uint32_t aFlags) {
  return (mLoadFlags & aFlags) == aFlags;
}

uint32_t nsDocShellLoadState::InternalLoadFlags() const {
  return mInternalLoadFlags;
}

void nsDocShellLoadState::SetInternalLoadFlags(uint32_t aLoadFlags) {
  mInternalLoadFlags = aLoadFlags;
}

void nsDocShellLoadState::SetInternalLoadFlag(uint32_t aFlag) {
  mInternalLoadFlags |= aFlag;
}

void nsDocShellLoadState::UnsetInternalLoadFlag(uint32_t aFlag) {
  mInternalLoadFlags &= ~aFlag;
}

bool nsDocShellLoadState::HasInternalLoadFlags(uint32_t aFlags) {
  return (mInternalLoadFlags & aFlags) == aFlags;
}

bool nsDocShellLoadState::FirstParty() const { return mFirstParty; }

void nsDocShellLoadState::SetFirstParty(bool aFirstParty) {
  mFirstParty = aFirstParty;
}

bool nsDocShellLoadState::HasValidUserGestureActivation() const {
  return mHasValidUserGestureActivation;
}

void nsDocShellLoadState::SetHasValidUserGestureActivation(
    bool aHasValidUserGestureActivation) {
  mHasValidUserGestureActivation = aHasValidUserGestureActivation;
}

void nsDocShellLoadState::SetTextDirectiveUserActivation(
    bool aTextDirectiveUserActivation) {
  mTextDirectiveUserActivation = aTextDirectiveUserActivation;
}

bool nsDocShellLoadState::GetTextDirectiveUserActivation() {
  return mTextDirectiveUserActivation;
}

const nsCString& nsDocShellLoadState::TypeHint() const { return mTypeHint; }

void nsDocShellLoadState::SetTypeHint(const nsCString& aTypeHint) {
  mTypeHint = aTypeHint;
}

const nsString& nsDocShellLoadState::FileName() const { return mFileName; }

void nsDocShellLoadState::SetFileName(const nsAString& aFileName) {
  MOZ_DIAGNOSTIC_ASSERT(aFileName.FindChar(char16_t(0)) == kNotFound,
                        "The filename should never contain null characters");
  mFileName = aFileName;
}

void nsDocShellLoadState::SetSpeculativeListener(
    mozilla::net::DocumentLoadListener* aListener) {
  MOZ_ASSERT(XRE_IsParentProcess(), "parent-process only field");
  mSpeculativeListener = aListener;
  mHasSpeculativeListener = mSpeculativeListener != nullptr;
}

already_AddRefed<mozilla::net::DocumentLoadListener>
nsDocShellLoadState::TakeSpeculativeListener() {
  MOZ_ASSERT(XRE_IsParentProcess(), "parent-process only field");
  mHasSpeculativeListener = false;
  return mSpeculativeListener.forget();
}

void nsDocShellLoadState::SetRemoteTypeOverride(
    const nsCString& aRemoteTypeOverride) {
  MOZ_DIAGNOSTIC_ASSERT(
      NS_IsAboutBlank(mURI),
      "Should only have aRemoteTypeOverride for about:blank URIs");
  mRemoteTypeOverride = mozilla::Some(aRemoteTypeOverride);
}

const nsCString& nsDocShellLoadState::GetEffectiveTriggeringRemoteType() const {
  // Consider non-errorpage loads from session history as being triggred by the
  // parent process, as we'll validate them against the history entry.
  //
  // NOTE: Keep this check in-sync with the session-history validation check in
  // `DocumentLoadListener::Open`!
  if (LoadIsFromSessionHistory() && LoadType() != LOAD_ERROR_PAGE) {
    return NOT_REMOTE_TYPE;
  }
  return mTriggeringRemoteType;
}

void nsDocShellLoadState::SetTriggeringRemoteType(
    const nsACString& aTriggeringRemoteType) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(), "only settable in parent");
  mTriggeringRemoteType = aTriggeringRemoteType;
}

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
void nsDocShellLoadState::AssertProcessCouldTriggerLoadIfSystem() {
  // Early check to see if we're trying to start a file URI load with a system
  // principal within a web content process.
  // If this assertion fails, the load will fail later during
  // nsContentSecurityManager checks, however this assertion should happen
  // closer to whichever caller is triggering the system-principal load.
  if (TriggeringPrincipal()->IsSystemPrincipal() &&
      mozilla::dom::IsWebRemoteType(GetEffectiveTriggeringRemoteType())) {
    bool localFile = false;
    if (NS_SUCCEEDED(NS_URIChainHasFlags(
            URI(), nsIProtocolHandler::URI_IS_LOCAL_FILE, &localFile)) &&
        localFile) {
      NS_WARNING(nsPrintfCString("Unexpected system load of file URI (%s) from "
                                 "web content process",
                                 URI()->GetSpecOrDefault().get())
                     .get());
      MOZ_CRASH("Unexpected system load of file URI from web content process");
    }
  }
}
#endif

nsresult nsDocShellLoadState::SetupInheritingPrincipal(
    BrowsingContext::Type aType,
    const mozilla::OriginAttributes& aOriginAttributes) {
  // We need a principalToInherit.
  //
  // If principalIsExplicit is not set there are 4 possibilities:
  // (1) If the system principal or an expanded principal was passed
  //     in and we're a typeContent docshell, inherit the principal
  //     from the current document instead.
  // (2) In all other cases when the principal passed in is not null,
  //     use that principal.
  // (3) If the caller has allowed inheriting from the current document,
  //     or if we're being called from system code (eg chrome JS or pure
  //     C++) then inheritPrincipal should be true and InternalLoad will get
  //     a principal from the current document. If none of these things are
  //     true, then
  // (4) we don't pass a principal into the channel, and a principal will be
  //     created later from the channel's internal data.
  //
  // If principalIsExplicit *is* set, there are 4 possibilities
  // (1) If the system principal or an expanded principal was passed in
  //     and we're a typeContent docshell, return an error.
  // (2) In all other cases when the principal passed in is not null,
  //     use that principal.
  // (3) If the caller has allowed inheriting from the current document,
  //     then inheritPrincipal should be true and InternalLoad will get
  //     a principal from the current document. If none of these things are
  //     true, then
  // (4) we dont' pass a principal into the channel, and a principal will be
  //     created later from the channel's internal data.
  mPrincipalToInherit = mTriggeringPrincipal;
  if (mPrincipalToInherit && aType != BrowsingContext::Type::Chrome) {
    if (mPrincipalToInherit->IsSystemPrincipal()) {
      if (mPrincipalIsExplicit) {
        return NS_ERROR_DOM_SECURITY_ERR;
      }
      mPrincipalToInherit = nullptr;
      mInheritPrincipal = true;
    } else if (nsContentUtils::IsExpandedPrincipal(mPrincipalToInherit)) {
      if (mPrincipalIsExplicit) {
        return NS_ERROR_DOM_SECURITY_ERR;
      }
      // Don't inherit from the current page.  Just do the safe thing
      // and pretend that we were loaded by a nullprincipal.
      //
      // We didn't inherit OriginAttributes here as ExpandedPrincipal doesn't
      // have origin attributes.
      mPrincipalToInherit = NullPrincipal::Create(aOriginAttributes);
      mInheritPrincipal = false;
    }
  }

  if (!mPrincipalToInherit && !mInheritPrincipal && !mPrincipalIsExplicit) {
    // See if there's system or chrome JS code running
    mInheritPrincipal = nsContentUtils::LegacyIsCallerChromeOrNativeCode();
  }

  if (mLoadFlags & nsIWebNavigation::LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL) {
    mInheritPrincipal = false;
    // Create a new null principal URI based on our precursor principal.
    nsCOMPtr<nsIURI> nullPrincipalURI =
        NullPrincipal::CreateURI(mPrincipalToInherit);
    // If mFirstParty is true and the pref 'privacy.firstparty.isolate' is
    // enabled, we will set firstPartyDomain on the origin attributes.
    OriginAttributes attrs(aOriginAttributes);
    if (mFirstParty) {
      attrs.SetFirstPartyDomain(true, nullPrincipalURI);
    }
    mPrincipalToInherit = NullPrincipal::Create(attrs, nullPrincipalURI);
  }

  return NS_OK;
}

nsresult nsDocShellLoadState::SetupTriggeringPrincipal(
    const mozilla::OriginAttributes& aOriginAttributes) {
  // If the triggeringPrincipal is not set, we first try to create a principal
  // from the referrer, since the referrer URI reflects the web origin that
  // triggered the load. If there is no referrer URI, we fall back to using the
  // SystemPrincipal. It's safe to assume that no provided triggeringPrincipal
  // and no referrer simulate a load that was triggered by the system. It's
  // important to note that this block of code needs to appear *after* the block
  // where we munge the principalToInherit, because otherwise we would never
  // enter code blocks checking if the principalToInherit is null and we will
  // end up with a wrong inheritPrincipal flag.
  if (!mTriggeringPrincipal) {
    if (mReferrerInfo) {
      nsCOMPtr<nsIURI> referrer = mReferrerInfo->GetOriginalReferrer();
      mTriggeringPrincipal =
          BasePrincipal::CreateContentPrincipal(referrer, aOriginAttributes);

      if (!mTriggeringPrincipal) {
        return NS_ERROR_FAILURE;
      }
    } else {
      mTriggeringPrincipal = nsContentUtils::GetSystemPrincipal();
    }
  }
  return NS_OK;
}

void nsDocShellLoadState::CalculateLoadURIFlags() {
  if (mInheritPrincipal) {
    MOZ_ASSERT(
        !mPrincipalToInherit || !mPrincipalToInherit->IsSystemPrincipal(),
        "Should not inherit SystemPrincipal");
    mInternalLoadFlags |= nsDocShell::INTERNAL_LOAD_FLAGS_INHERIT_PRINCIPAL;
  }

  if (mReferrerInfo && !mReferrerInfo->GetSendReferrer()) {
    mInternalLoadFlags |= nsDocShell::INTERNAL_LOAD_FLAGS_DONT_SEND_REFERRER;
  }
  if (mLoadFlags & nsIWebNavigation::LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP) {
    mInternalLoadFlags |=
        nsDocShell::INTERNAL_LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP;
  }

  if (mLoadFlags & nsIWebNavigation::LOAD_FLAGS_FIRST_LOAD) {
    mInternalLoadFlags |= nsDocShell::INTERNAL_LOAD_FLAGS_FIRST_LOAD;
  }

  if (mLoadFlags & nsIWebNavigation::LOAD_FLAGS_BYPASS_CLASSIFIER) {
    mInternalLoadFlags |= nsDocShell::INTERNAL_LOAD_FLAGS_BYPASS_CLASSIFIER;
  }

  if (mLoadFlags & nsIWebNavigation::LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE) {
    mInternalLoadFlags |=
        nsDocShell::INTERNAL_LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE;
  }

  if (!mSrcdocData.IsVoid()) {
    mInternalLoadFlags |= nsDocShell::INTERNAL_LOAD_FLAGS_IS_SRCDOC;
  }

  if (mForceAllowDataURI) {
    mInternalLoadFlags |= nsDocShell::INTERNAL_LOAD_FLAGS_FORCE_ALLOW_DATA_URI;
  }

  if (mOriginalFrameSrc) {
    mInternalLoadFlags |= nsDocShell::INTERNAL_LOAD_FLAGS_ORIGINAL_FRAME_SRC;
  }
}

nsLoadFlags nsDocShellLoadState::CalculateChannelLoadFlags(
    BrowsingContext* aBrowsingContext, bool aUriModified,
    Maybe<bool> aIsEmbeddingBlockedError) {
  MOZ_ASSERT(aBrowsingContext);

  nsLoadFlags loadFlags = aBrowsingContext->GetDefaultLoadFlags();

  if (FirstParty()) {
    // tag first party URL loads
    loadFlags |= nsIChannel::LOAD_INITIAL_DOCUMENT_URI;
  }

  const uint32_t loadType = LoadType();

  // These values aren't available for loads initiated in the Parent process.
  MOZ_ASSERT_IF(loadType == LOAD_ERROR_PAGE, aIsEmbeddingBlockedError.isSome());

  if (loadType == LOAD_ERROR_PAGE) {
    // Error pages are LOAD_BACKGROUND, unless it's an
    // XFO / frame-ancestors error for which we want an error page to load
    // but additionally want the onload() event to fire.
    if (!*aIsEmbeddingBlockedError) {
      loadFlags |= nsIChannel::LOAD_BACKGROUND;
    }
  }

  // Mark the channel as being a document URI and allow content sniffing...
  loadFlags |=
      nsIChannel::LOAD_DOCUMENT_URI | nsIChannel::LOAD_CALL_CONTENT_SNIFFERS;

  if (nsDocShell::SandboxFlagsImplyCookies(
          aBrowsingContext->GetSandboxFlags())) {
    loadFlags |= nsIRequest::LOAD_DOCUMENT_NEEDS_COOKIE;
  }

  // Load attributes depend on load type...
  switch (loadType) {
    case LOAD_HISTORY: {
      // Only send VALIDATE_NEVER if mLSHE's URI was never changed via
      // push/replaceState (bug 669671).
      if (!aUriModified) {
        loadFlags |= nsIRequest::VALIDATE_NEVER;
      }
      break;
    }

    case LOAD_RELOAD_CHARSET_CHANGE_BYPASS_PROXY_AND_CACHE:
    case LOAD_RELOAD_CHARSET_CHANGE_BYPASS_CACHE:
      loadFlags |=
          nsIRequest::LOAD_BYPASS_CACHE | nsIRequest::LOAD_FRESH_CONNECTION;
      [[fallthrough]];

    case LOAD_REFRESH:
      loadFlags |= nsIRequest::VALIDATE_ALWAYS;
      break;

    case LOAD_NORMAL_BYPASS_CACHE:
    case LOAD_NORMAL_BYPASS_PROXY:
    case LOAD_NORMAL_BYPASS_PROXY_AND_CACHE:
    case LOAD_RELOAD_BYPASS_CACHE:
    case LOAD_RELOAD_BYPASS_PROXY:
    case LOAD_RELOAD_BYPASS_PROXY_AND_CACHE:
    case LOAD_REPLACE_BYPASS_CACHE:
      loadFlags |=
          nsIRequest::LOAD_BYPASS_CACHE | nsIRequest::LOAD_FRESH_CONNECTION;
      break;

    case LOAD_RELOAD_NORMAL:
      if (!StaticPrefs::
              browser_soft_reload_only_force_validate_top_level_document()) {
        loadFlags |= nsIRequest::VALIDATE_ALWAYS;
        break;
      }
      [[fallthrough]];
    case LOAD_NORMAL:
    case LOAD_LINK:
      // Set cache checking flags
      switch (StaticPrefs::browser_cache_check_doc_frequency()) {
        case 0:
          loadFlags |= nsIRequest::VALIDATE_ONCE_PER_SESSION;
          break;
        case 1:
          loadFlags |= nsIRequest::VALIDATE_ALWAYS;
          break;
        case 2:
          loadFlags |= nsIRequest::VALIDATE_NEVER;
          break;
      }
      break;
  }

  if (HasInternalLoadFlags(nsDocShell::INTERNAL_LOAD_FLAGS_BYPASS_CLASSIFIER)) {
    loadFlags |= nsIChannel::LOAD_BYPASS_URL_CLASSIFIER;
  }

  // If the user pressed shift-reload, then do not allow ServiceWorker
  // interception to occur. See step 12.1 of the SW HandleFetch algorithm.
  if (IsForceReloadType(loadType)) {
    loadFlags |= nsIChannel::LOAD_BYPASS_SERVICE_WORKER;
  } else if (aBrowsingContext->IsTopContent()) {
    // For the top-level site the uri to load determines whether service workers
    // are blocked by policy.
    if (dom::IsServiceWorkersDisabledByPolicy(mURI)) {
      loadFlags |= nsIChannel::LOAD_BYPASS_SERVICE_WORKER;
    }
  } else if (aBrowsingContext->Top()->ServiceWorkersDisabledByPolicy()) {
    // Otherwise use the state of the top-level site to determine whether
    // service workers are blocked.
    loadFlags |= nsIChannel::LOAD_BYPASS_SERVICE_WORKER;
  }

  return loadFlags;
}

const char* nsDocShellLoadState::ValidateWithOriginalState(
    nsDocShellLoadState* aOriginalState) {
  MOZ_ASSERT(mLoadIdentifier == aOriginalState->mLoadIdentifier);

  // Check that `aOriginalState` is sufficiently similar to this state that
  // they're performing the same load.
  auto uriEq = [](nsIURI* a, nsIURI* b) -> bool {
    bool eq = false;
    return a == b || (a && b && NS_SUCCEEDED(a->Equals(b, &eq)) && eq);
  };
  if (!uriEq(mURI, aOriginalState->mURI)) {
    return "URI";
  }
  if (!uriEq(mUnstrippedURI, aOriginalState->mUnstrippedURI)) {
    return "UnstrippedURI";
  }
  if (!uriEq(mOriginalURI, aOriginalState->mOriginalURI)) {
    return "OriginalURI";
  }
  if (!uriEq(mResultPrincipalURI, aOriginalState->mResultPrincipalURI)) {
    return "mResultPrincipalURI";
  }
  if (!uriEq(mBaseURI, aOriginalState->mBaseURI)) {
    return "BaseURI";
  }

  if (mSrcdocData != aOriginalState->mSrcdocData) {
    return "SrcdocData";
  }

  if (!mTriggeringPrincipal->Equals(aOriginalState->mTriggeringPrincipal)) {
    return "TriggeringPrincipal";
  }
  if (mTriggeringSandboxFlags != aOriginalState->mTriggeringSandboxFlags) {
    return "TriggeringSandboxFlags";
  }
  if (mTriggeringRemoteType != aOriginalState->mTriggeringRemoteType) {
    return "TriggeringRemoteType";
  }

  if (mOriginalURIString != aOriginalState->mOriginalURIString) {
    return "OriginalURIString";
  }

  if (mRemoteTypeOverride != aOriginalState->mRemoteTypeOverride) {
    return "RemoteTypeOverride";
  }

  if (mSourceBrowsingContext.ContextId() !=
      aOriginalState->mSourceBrowsingContext.ContextId()) {
    return "SourceBrowsingContext";
  }

  if (mHasSpeculativeListener != aOriginalState->mHasSpeculativeListener) {
    return "HasSpeculativeListener";
  }

  // FIXME: Consider calculating less information in the target process so
  // that we can validate more properties more easily.
  // FIXME: Identify what other flags will not change when sent through a
  // content process.

  return nullptr;
}

DocShellLoadStateInit nsDocShellLoadState::Serialize(
    mozilla::ipc::IProtocol* aActor) {
  MOZ_ASSERT(aActor);
  DocShellLoadStateInit loadState;
  loadState.ResultPrincipalURI() = mResultPrincipalURI;
  loadState.ResultPrincipalURIIsSome() = mResultPrincipalURIIsSome;
  loadState.KeepResultPrincipalURIIfSet() = mKeepResultPrincipalURIIfSet;
  loadState.LoadReplace() = mLoadReplace;
  loadState.InheritPrincipal() = mInheritPrincipal;
  loadState.PrincipalIsExplicit() = mPrincipalIsExplicit;
  loadState.NotifiedBeforeUnloadListeners() = mNotifiedBeforeUnloadListeners;
  loadState.ForceAllowDataURI() = mForceAllowDataURI;
  loadState.IsExemptFromHTTPSFirstMode() = mIsExemptFromHTTPSFirstMode;
  loadState.OriginalFrameSrc() = mOriginalFrameSrc;
  loadState.ShouldCheckForRecursion() = mShouldCheckForRecursion;
  loadState.IsFormSubmission() = mIsFormSubmission;
  loadState.NeedsCompletelyLoadedDocument() = mNeedsCompletelyLoadedDocument;
  loadState.HistoryBehavior() = mHistoryBehavior;
  loadState.LoadType() = mLoadType;
  loadState.userNavigationInvolvement() = mUserNavigationInvolvement;
  loadState.Target() = mTarget;
  loadState.TargetBrowsingContext() = mTargetBrowsingContext;
  loadState.LoadFlags() = mLoadFlags;
  loadState.InternalLoadFlags() = mInternalLoadFlags;
  loadState.FirstParty() = mFirstParty;
  loadState.HasValidUserGestureActivation() = mHasValidUserGestureActivation;
  loadState.TextDirectiveUserActivation() = mTextDirectiveUserActivation;
  loadState.AllowFocusMove() = mAllowFocusMove;
  loadState.TypeHint() = mTypeHint;
  loadState.FileName() = mFileName;
  loadState.IsFromProcessingFrameAttributes() =
      mIsFromProcessingFrameAttributes;
  loadState.URI() = mURI;
  loadState.OriginalURI() = mOriginalURI;
  loadState.SourceBrowsingContext() = mSourceBrowsingContext;
  loadState.BaseURI() = mBaseURI;
  loadState.TriggeringPrincipal() = mTriggeringPrincipal;
  loadState.PrincipalToInherit() = mPrincipalToInherit;
  loadState.PartitionedPrincipalToInherit() = mPartitionedPrincipalToInherit;
  loadState.TriggeringSandboxFlags() = mTriggeringSandboxFlags;
  loadState.TriggeringWindowId() = mTriggeringWindowId;
  loadState.TriggeringStorageAccess() = mTriggeringStorageAccess;
  loadState.TriggeringClassificationFlags() = mTriggeringClassificationFlags;
  loadState.TriggeringRemoteType() = mTriggeringRemoteType;
  loadState.SchemelessInput() = mSchemelessInput;
  loadState.HttpsUpgradeTelemetry() = mHttpsUpgradeTelemetry;
  loadState.PolicyContainer() = mPolicyContainer;
  loadState.OriginalURIString() = mOriginalURIString;
  loadState.CancelContentJSEpoch() = mCancelContentJSEpoch;
  loadState.ReferrerInfo() = mReferrerInfo;
  loadState.PostDataStream() = mPostDataStream;
  loadState.HeadersStream() = mHeadersStream;
  loadState.SrcdocData() = mSrcdocData;
  loadState.ResultPrincipalURI() = mResultPrincipalURI;
  loadState.LoadIdentifier() = mLoadIdentifier;
  loadState.HasSpeculativeListener() = mHasSpeculativeListener;
  loadState.IsMetaRefresh() = mIsMetaRefresh;
  loadState.forceMediaDocument() = mForceMediaDocument;
  if (mLoadingSessionHistoryInfo) {
    loadState.loadingSessionHistoryInfo().emplace(*mLoadingSessionHistoryInfo);
  }
  loadState.UnstrippedURI() = mUnstrippedURI;
  loadState.RemoteTypeOverride() = mRemoteTypeOverride;
  loadState.IsCaptivePortalTab() = mIsCaptivePortalTab;
  loadState.IsInitialAboutBlankHandlingProhibited() =
      mIsInitialAboutBlankHandlingProhibited;
  loadState.NavigationAPIState() = mNavigationAPIState;

  if (XRE_IsParentProcess()) {
    ContentParent* cp = ActorDynCast<ContentParent>(aActor->ToplevelProtocol());
    MOZ_RELEASE_ASSERT(cp, "nsDocShellLoadState must be sent over PContent");
    cp->StorePendingLoadState(this);
  }

  return loadState;
}

nsIURI* nsDocShellLoadState::GetUnstrippedURI() const { return mUnstrippedURI; }

void nsDocShellLoadState::SetUnstrippedURI(nsIURI* aUnstrippedURI) {
  mUnstrippedURI = aUnstrippedURI;
}

void nsDocShellLoadState::SetSourceElement(mozilla::dom::Element* aElement) {
  mSourceElement = do_GetWeakReference(aElement);
}
already_AddRefed<Element> nsDocShellLoadState::GetSourceElement() const {
  nsCOMPtr<Element> element = do_QueryReferent(mSourceElement);
  return element.forget();
}

nsIStructuredCloneContainer* nsDocShellLoadState::GetNavigationAPIState()
    const {
  return mNavigationAPIState;
}

void nsDocShellLoadState::SetNavigationAPIState(
    nsIStructuredCloneContainer* aNavigationAPIState) {
  mNavigationAPIState =
      static_cast<nsStructuredCloneContainer*>(aNavigationAPIState);
}

NavigationAPIMethodTracker* nsDocShellLoadState::GetNavigationAPIMethodTracker()
    const {
  return mNavigationAPIMethodTracker;
}

void nsDocShellLoadState::SetNavigationAPIMethodTracker(
    NavigationAPIMethodTracker* aTracker) {
  mNavigationAPIMethodTracker = aTracker;
}

NavigationType nsDocShellLoadState::GetNavigationType() const {
  return NavigationUtils::NavigationTypeFromLoadType(LoadType())
      .valueOr(NavigationType::Push);
}

mozilla::dom::FormData* nsDocShellLoadState::GetFormDataEntryList() {
  return mFormDataEntryList;
}

void nsDocShellLoadState::SetFormDataEntryList(
    mozilla::dom::FormData* aFormDataEntryList) {
  mFormDataEntryList = aFormDataEntryList;
}

uint32_t nsDocShellLoadState::GetAppLinkLaunchType() const {
  return mAppLinkLaunchType;
}

void nsDocShellLoadState::SetAppLinkLaunchType(uint32_t aAppLinkLaunchType) {
  mAppLinkLaunchType = aAppLinkLaunchType;
}

bool nsDocShellLoadState::GetIsCaptivePortalTab() const {
  return mIsCaptivePortalTab;
}

void nsDocShellLoadState::SetIsCaptivePortalTab(bool aIsCaptivePortalTab) {
  mIsCaptivePortalTab = aIsCaptivePortalTab;
}
