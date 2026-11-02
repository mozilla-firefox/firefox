/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EarlyHintPreloader.h"

#include "EarlyHintRegistrar.h"
#include "EarlyHintsService.h"
#include "ErrorList.h"
#include "HttpChannelParent.h"
#include "MainThreadUtils.h"
#include "NeckoCommon.h"
#include "ParentChannelListener.h"
#include "gfxPlatform.h"
#include "mozilla/CORSMode.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/net/EarlyHintRegistrar.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsAttrValue.h"
#include "nsCOMPtr.h"
#include "nsContentPolicyUtils.h"
#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsHttpChannel.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIChannel.h"
#include "nsICookieJarSettings.h"
#include "nsIHttpChannel.h"
#include "nsIInputStream.h"
#include "nsILoadContext.h"
#include "nsILoadInfo.h"
#include "nsIParentChannel.h"
#include "nsIReferrerInfo.h"
#include "nsITimer.h"
#include "nsIURI.h"
#include "nsInterfaceRequestorAgg.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"

//
// To enable logging (see mozilla/Logging.h for full details):
//
//    set MOZ_LOG=EarlyHint:5
//    set MOZ_LOG_FILE=earlyhint.log
//
// this enables LogLevel::Debug level information and places all output in
// the file earlyhint.log
//
static mozilla::LazyLogModule gEarlyHintLog("EarlyHint");

#undef LOG
#define LOG(args) MOZ_LOG(gEarlyHintLog, mozilla::LogLevel::Debug, args)

#undef LOG_ENABLED
#define LOG_ENABLED() MOZ_LOG_TEST(gEarlyHintLog, mozilla::LogLevel::Debug)

namespace mozilla::net {

namespace {
// This id uniquely identifies each early hint preloader in the
// EarlyHintRegistrar. Must only be accessed from main thread.
static uint64_t gEarlyHintPreloaderId{0};
}  // namespace

//=============================================================================
// OngoingEarlyHints
//=============================================================================

void OngoingEarlyHints::CancelAll(const nsACString& aReason) {
  for (auto& preloader : mPreloaders) {
    preloader->CancelChannel(NS_ERROR_ABORT, aReason, /* aDeleteEntry */ true);
  }
  mPreloaders.Clear();
  mStartedPreloads.Clear();
}

bool OngoingEarlyHints::Contains(const PreloadHashKey& aKey) {
  return mStartedPreloads.Contains(aKey);
}

bool OngoingEarlyHints::Add(const PreloadHashKey& aKey,
                            RefPtr<EarlyHintPreloader> aPreloader) {
  if (!mStartedPreloads.Contains(aKey)) {
    mStartedPreloads.Insert(aKey);
    mPreloaders.AppendElement(aPreloader);
    return true;
  }
  return false;
}

void OngoingEarlyHints::RegisterLinksAndGetConnectArgs(
    dom::ContentParentId aCpId, nsTArray<EarlyHintConnectArgs>& aOutLinks) {
  // register all channels before returning
  for (auto& preload : mPreloaders) {
    EarlyHintConnectArgs args;
    if (preload->Register(aCpId, args)) {
      aOutLinks.AppendElement(std::move(args));
    }
  }
}

//=============================================================================
// EarlyHintPreloader
//=============================================================================

EarlyHintPreloader::EarlyHintPreloader() {
  AssertIsOnMainThread();
  mConnectArgs.earlyHintPreloaderId() = ++gEarlyHintPreloaderId;
};

EarlyHintPreloader::~EarlyHintPreloader() {
  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
}

/* static */
Maybe<PreloadHashKey> EarlyHintPreloader::GenerateHashKey(
    ASDestination aAs, nsIURI* aURI, nsIPrincipal* aPrincipal,
    CORSMode aCorsMode, bool aIsModulepreload) {
  if (aIsModulepreload) {
    return Some(PreloadHashKey::CreateAsScript(
        aURI, aCorsMode, JS::loader::ScriptKind::eModule));
  }
  if (aAs == ASDestination::DESTINATION_FONT && aCorsMode != CORS_NONE) {
    return Some(PreloadHashKey::CreateAsFont(aURI, aCorsMode));
  }
  if (aAs == ASDestination::DESTINATION_IMAGE) {
    return Some(PreloadHashKey::CreateAsImage(aURI, aPrincipal, aCorsMode));
  }
  if (aAs == ASDestination::DESTINATION_SCRIPT) {
    return Some(PreloadHashKey::CreateAsScript(
        aURI, aCorsMode, JS::loader::ScriptKind::eClassic));
  }
  if (aAs == ASDestination::DESTINATION_STYLE) {
    return Some(PreloadHashKey::CreateAsStyle(aURI, aPrincipal, aCorsMode));
  }
  if (aAs == ASDestination::DESTINATION_FETCH && aCorsMode != CORS_NONE) {
    return Some(PreloadHashKey::CreateAsFetch(aURI, aCorsMode));
  }
  return Nothing();
}

/* static */
nsSecurityFlags EarlyHintPreloader::ComputeSecurityFlags(CORSMode aCORSMode,
                                                         ASDestination aAs) {
  if (aAs == ASDestination::DESTINATION_FONT) {
    return nsContentSecurityManager::ComputeSecurityFlags(
        CORSMode::CORS_NONE,
        nsContentSecurityManager::CORSSecurityMapping::REQUIRE_CORS_CHECKS);
  }
  if (aAs == ASDestination::DESTINATION_IMAGE) {
    return nsContentSecurityManager::ComputeSecurityFlags(
               aCORSMode, nsContentSecurityManager::CORSSecurityMapping::
                              CORS_NONE_MAPS_TO_INHERITED_CONTEXT) |
           nsILoadInfo::SEC_ALLOW_CHROME;
  }
  if (aAs == ASDestination::DESTINATION_SCRIPT) {
    return nsContentSecurityManager::ComputeSecurityFlags(
               aCORSMode, nsContentSecurityManager::CORSSecurityMapping::
                              CORS_NONE_MAPS_TO_DISABLED_CORS_CHECKS) |
           nsILoadInfo::SEC_ALLOW_CHROME;
  }
  if (aAs == ASDestination::DESTINATION_STYLE) {
    return nsContentSecurityManager::ComputeSecurityFlags(
               aCORSMode, nsContentSecurityManager::CORSSecurityMapping::
                              CORS_NONE_MAPS_TO_INHERITED_CONTEXT) |
           nsILoadInfo::SEC_ALLOW_CHROME;
    ;
  }
  if (aAs == ASDestination::DESTINATION_FETCH) {
    return nsContentSecurityManager::ComputeSecurityFlags(
        aCORSMode, nsContentSecurityManager::CORSSecurityMapping::
                       CORS_NONE_MAPS_TO_DISABLED_CORS_CHECKS);
  }
  MOZ_ASSERT(false, "Unexpected ASDestination");
  return nsContentSecurityManager::ComputeSecurityFlags(
      CORSMode::CORS_NONE,
      nsContentSecurityManager::CORSSecurityMapping::REQUIRE_CORS_CHECKS);
}

// static
void EarlyHintPreloader::MaybeCreateAndInsertPreload(
    OngoingEarlyHints* aOngoingEarlyHints, const LinkHeader& aLinkHeader,
    nsIURI* aBaseURI, nsIPrincipal* aPrincipal,
    nsICookieJarSettings* aCookieJarSettings,
    const nsACString& aResponseReferrerPolicy, const nsACString& aCSPHeader,
    const nsACString& aConnectionAllowlistHeader, uint64_t aBrowsingContextID,
    dom::CanonicalBrowsingContext* aLoadingBrowsingContext,
    bool aIsModulepreload) {
  nsAttrValue as;
  ParseAsValue(aLinkHeader.mAs, as);

  ASDestination destination = static_cast<ASDestination>(as.GetEnumValue());

  if (!StaticPrefs::network_early_hints_enabled()) {
    return;
  }

  if (destination == ASDestination::DESTINATION_INVALID && !aIsModulepreload) {
    // return early when it's definitly not an asset type we preload
    // would be caught later as well, e.g. when creating the PreloadHashKey
    return;
  }

  if (destination == ASDestination::DESTINATION_FONT &&
      !gfxPlatform::GetPlatform()->DownloadableFontsEnabled()) {
    return;
  }

  // The image the document picks from imagesrcset can't be determined here,
  // so it may differ from href.
  if (destination == ASDestination::DESTINATION_IMAGE &&
      !aLinkHeader.mSrcset.IsEmpty()) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  NS_ENSURE_SUCCESS_VOID(
      NS_NewURI(getter_AddRefs(uri), aLinkHeader.mHref, nullptr, aBaseURI));
  // The link relation may apply to a different resource, specified
  // in the anchor parameter. For the link relations supported so far,
  // we simply abort if the link applies to a resource different to the
  // one we've loaded
  if (!nsContentUtils::LinkContextIsURI(aLinkHeader.mAnchor, uri)) {
    return;
  }

  // only preload secure context urls
  if (!nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(uri)) {
    return;
  }

  CORSMode corsMode = dom::Element::StringToCORSMode(aLinkHeader.mCrossOrigin);

  Maybe<PreloadHashKey> hashKey =
      GenerateHashKey(destination, uri, aPrincipal, corsMode, aIsModulepreload);
  if (!hashKey) {
    return;
  }

  if (aOngoingEarlyHints->Contains(*hashKey)) {
    return;
  }

  // The document fetches compression dictionaries as fetch preloads.
  nsContentPolicyType contentPolicyType =
      aLinkHeader.mRel.LowerCaseEqualsLiteral("compression-dictionary")
          ? nsContentPolicyType::TYPE_INTERNAL_FETCH_PRELOAD
      : aIsModulepreload ? (IsScriptLikeOrInvalid(aLinkHeader.mAs)
                                ? nsContentPolicyType::TYPE_SCRIPT
                                : nsContentPolicyType::TYPE_INVALID)
                         : AsValueToContentPolicy(as);

  if (contentPolicyType == nsContentPolicyType::TYPE_INVALID) {
    return;
  }

  dom::ReferrerPolicy linkReferrerPolicy =
      dom::ReferrerInfo::ReferrerPolicyAttributeFromString(
          aLinkHeader.mReferrerPolicy);

  dom::ReferrerPolicy responseReferrerPolicy =
      dom::ReferrerInfo::ReferrerPolicyAttributeFromString(
          NS_ConvertUTF8toUTF16(aResponseReferrerPolicy));

  // The early hint may have two referrer policies, one from the response header
  // and one from the link element.
  //
  // For example, in this server response:
  //   HTTP/1.1 103 Early Hints
  //   Referrer-Policy : origin
  //   Link: </style.css>; rel=preload; as=style referrerpolicy=no-referrer
  //
  //   The link header referrer policy, if present, will take precedence over
  //   the response referrer policy
  dom::ReferrerPolicy finalReferrerPolicy = responseReferrerPolicy;
  if (linkReferrerPolicy != dom::ReferrerPolicy::_empty) {
    finalReferrerPolicy = linkReferrerPolicy;
  }
  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      new dom::ReferrerInfo(aBaseURI, finalReferrerPolicy);

  RefPtr<EarlyHintPreloader> earlyHintPreloader = new EarlyHintPreloader();

  earlyHintPreloader->mLoadContext = aLoadingBrowsingContext;

  // Security flags for modulepreload's request mode are computed here directly
  // until full support for worker destinations can be added.
  //
  // Implements "To fetch a single module script,"
  // Step 9. If destination is "worker", "sharedworker", or "serviceworker",
  //         and the top-level module fetch flag is set, then set request's
  //         mode to "same-origin".
  nsSecurityFlags securityFlags =
      aIsModulepreload
          ? ((aLinkHeader.mAs.LowerCaseEqualsASCII("worker") ||
              aLinkHeader.mAs.LowerCaseEqualsASCII("sharedworker") ||
              aLinkHeader.mAs.LowerCaseEqualsASCII("serviceworker"))
                 ? nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_IS_BLOCKED
                 : nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT) |
                (corsMode == CORS_USE_CREDENTIALS
                     ? nsILoadInfo::SEC_COOKIES_INCLUDE
                     : nsILoadInfo::SEC_COOKIES_SAME_ORIGIN) |
                nsILoadInfo::SEC_ALLOW_CHROME
          : EarlyHintPreloader::ComputeSecurityFlags(corsMode, destination);

  // Verify that the resource should be loaded.
  // This isn't the ideal way to test the resource against the CSP.
  // The problem comes from the fact that at the stage of Early Hint
  // processing we have not yet created a document where we would normally store
  // the CSP.

  // First we will create a load info.
  Result<nsCOMPtr<nsILoadInfo>, nsresult> maybeLoadInfo = LoadInfo::Create(
      aPrincipal,  // loading principal
      aPrincipal,  // triggering principal
      nullptr /* aLoadingContext node */,
      nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK, contentPolicyType);
  if (NS_WARN_IF(maybeLoadInfo.isErr())) {
    return;
  }
  nsCOMPtr<nsILoadInfo> secCheckLoadInfo = maybeLoadInfo.unwrap();

  if (!aCSPHeader.IsEmpty() || !aConnectionAllowlistHeader.IsEmpty()) {
    auto policyContainerOrErr = PolicyContainer::CreateForNonDocument(
        aPrincipal, aBaseURI,
        {.mContentSecurityPolicy = nsCString(aCSPHeader),
         .mConnectionAllowlist = nsCString(aConnectionAllowlistHeader)});
    if (NS_WARN_IF(policyContainerOrErr.isErr())) {
      return;
    }
    RefPtr<PolicyContainer> policyContainer = policyContainerOrErr.unwrap();

    // We create a temporary ClientInfo. This is required on the loadInfo as
    // that is how the CSP is queried. More specificially, as a hack to be able
    // to call NS_CheckContentLoadPolicy on nsILoadInfo which exclusively
    // accesses the CSP from the ClientInfo, we create a synthetic ClientInfo to
    // hold the CSP we are creating. This is not a safe thing to do in any other
    // circumstance because ClientInfos are always describing a ClientSource
    // that corresponds to a global or potential global, so creating an info
    // without a source is unsound. For the purposes of doing things before a
    // global exists, fetch has the concept of a
    // https://fetch.spec.whatwg.org/#concept-request-reserved-client and
    // nsILoadInfo explicity has methods around GiveReservedClientSource which
    // are primarily used by ClientChannelHelper. If you are trying to do real
    // CSP stuff and the ClientInfo is not there yet, please enhance the logic
    // around ClientChannelHelper.

    mozilla::ipc::PrincipalInfo principalInfo;
    nsresult rv = PrincipalToPrincipalInfo(aPrincipal, &principalInfo);
    NS_ENSURE_SUCCESS_VOID(rv);
    dom::ClientInfo clientInfo(nsID::GenerateUUID(), Nothing(),
                               dom::ClientType::Window, principalInfo,
                               TimeStamp::Now(), ""_ns, dom::FrameType::None);

    ipc::PolicyContainerArgs policyContainerArgs;
    PolicyContainer::ToArgs(policyContainer, policyContainerArgs);
    clientInfo.SetPolicyContainerArgs(policyContainerArgs);

    // This ClientInfo is then set on the new loadInfo.
    // It can now be used to test the resource against the policy
    secCheckLoadInfo->SetClientInfo(clientInfo);
  }

  dom::RequestMode requestMode =
      nsContentSecurityManager::SecurityModeToRequestMode(
          nsContentSecurityManager::ComputeSecurityMode(securityFlags));
  secCheckLoadInfo->SetRequestMode(Some(requestMode));

  int16_t shouldLoad = nsIContentPolicy::ACCEPT;
  nsresult rv = NS_CheckContentLoadPolicy(uri, secCheckLoadInfo, &shouldLoad,
                                          nsContentUtils::GetContentPolicy());

  if (NS_FAILED(rv) || NS_CP_REJECTED(shouldLoad)) {
    return;
  }

  NS_ENSURE_SUCCESS_VOID(earlyHintPreloader->OpenChannel(
      uri, aPrincipal, securityFlags, contentPolicyType, referrerInfo,
      aCookieJarSettings, aBrowsingContextID));

  earlyHintPreloader->SetLinkHeader(aLinkHeader);

  DebugOnly<bool> result =
      aOngoingEarlyHints->Add(*hashKey, earlyHintPreloader);
  MOZ_ASSERT(result);
}

nsresult EarlyHintPreloader::OpenChannel(
    nsIURI* aURI, nsIPrincipal* aPrincipal, nsSecurityFlags aSecurityFlags,
    nsContentPolicyType aContentPolicyType, nsIReferrerInfo* aReferrerInfo,
    nsICookieJarSettings* aCookieJarSettings, uint64_t aBrowsingContextID) {
  MOZ_ASSERT(aContentPolicyType == nsContentPolicyType::TYPE_IMAGE ||
             aContentPolicyType ==
                 nsContentPolicyType::TYPE_INTERNAL_FETCH_PRELOAD ||
             aContentPolicyType == nsContentPolicyType::TYPE_SCRIPT ||
             aContentPolicyType == nsContentPolicyType::TYPE_STYLESHEET ||
             aContentPolicyType == nsContentPolicyType::TYPE_FONT);

  nsresult rv =
      NS_NewChannel(getter_AddRefs(mChannel), aURI, aPrincipal, aSecurityFlags,
                    aContentPolicyType, aCookieJarSettings,
                    /* aPerformanceStorage */ nullptr,
                    /* aLoadGroup */ nullptr,
                    /* aCallbacks */ this, nsIRequest::LOAD_NORMAL);

  NS_ENSURE_SUCCESS(rv, rv);

  mURI = aURI;
  mLoadInfo = mChannel->LoadInfo();

  RefPtr<nsHttpChannel> httpChannelObject = do_QueryObject(mChannel);
  if (!httpChannelObject) {
    mChannel = nullptr;
    return NS_ERROR_ABORT;
  }

  // configure HTTP specific stuff
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel);
  if (!httpChannel) {
    mChannel = nullptr;
    return NS_ERROR_ABORT;
  }
  DebugOnly<nsresult> success = httpChannel->SetReferrerInfo(aReferrerInfo);
  MOZ_ASSERT(NS_SUCCEEDED(success));
  success = httpChannel->SetRequestHeader("X-Moz"_ns, "early hint"_ns, false);
  MOZ_ASSERT(NS_SUCCEEDED(success));

  mParentListener = new ParentChannelListener(this, nullptr);

  PriorizeAsPreload();

  rv = mChannel->AsyncOpen(mParentListener);
  if (NS_FAILED(rv)) {
    mParentListener = nullptr;
    return rv;
  }

  SetState(ePreloaderOpened);

  // Setting the BrowsingContextID here to let Early Hint requests show up in
  // devtools. Normally that would automatically happen if we would pass the
  // nsILoadGroup in ns_NewChannel above, but the nsILoadGroup is inaccessible
  // here in the ParentProcess. The nsILoadGroup only exists in ContentProcess
  // as part of the document and nsDocShell. It is also not yet determined which
  // ContentProcess this load belongs to.
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  static_cast<LoadInfo*>(loadInfo.get())
      ->UpdateBrowsingContextID(aBrowsingContextID);

  return NS_OK;
}

void EarlyHintPreloader::PriorizeAsPreload() {
  nsLoadFlags loadFlags = nsIRequest::LOAD_NORMAL;
  (void)mChannel->GetLoadFlags(&loadFlags);
  (void)mChannel->SetLoadFlags(loadFlags | nsIRequest::LOAD_BACKGROUND);

  if (nsCOMPtr<nsIClassOfService> cos = do_QueryInterface(mChannel)) {
    (void)cos->AddClassFlags(nsIClassOfService::Unblocked);
  }
}

void EarlyHintPreloader::SetLinkHeader(const LinkHeader& aLinkHeader) {
  mConnectArgs.link() = aLinkHeader;
}

bool EarlyHintPreloader::IsFromContentParent(dom::ContentParentId aCpId) const {
  return aCpId == mCpId;
}

bool EarlyHintPreloader::MatchesRequest(nsIURI* aURI,
                                        nsILoadInfo* aLoadInfo) const {
  if (!mURI || !mLoadInfo || !aURI || !aLoadInfo) {
    return false;
  }

  bool equals = false;
  if (NS_FAILED(mURI->EqualsExceptRef(aURI, &equals))) {
    return false;
  }

  if (!equals) {
    return false;
  }

  if (mLoadInfo->GetExternalContentPolicyType() !=
      aLoadInfo->GetExternalContentPolicyType()) {
    return false;
  }

  if (nsContentSecurityManager::SecurityModeToRequestMode(
          mLoadInfo->GetSecurityMode()) !=
      nsContentSecurityManager::SecurityModeToRequestMode(
          aLoadInfo->GetSecurityMode())) {
    return false;
  }

  if (mLoadInfo->GetCookiePolicy() != aLoadInfo->GetCookiePolicy()) {
    return false;
  }

  nsIPrincipal* loadingPrincipal = aLoadInfo->GetLoadingPrincipal();
  if (!loadingPrincipal) {
    return false;
  }

  if (!mLoadInfo->GetLoadingPrincipal()->Equals(loadingPrincipal)) {
    return false;
  }

  if (!mLoadInfo->TriggeringPrincipal()->Equals(
          aLoadInfo->TriggeringPrincipal())) {
    return false;
  }

  if (mLoadInfo->GetOriginAttributes() != aLoadInfo->GetOriginAttributes()) {
    return false;
  }

  nsCOMPtr<nsICookieJarSettings> cjs;
  if (NS_FAILED(mLoadInfo->GetCookieJarSettings(getter_AddRefs(cjs)))) {
    return false;
  }

  nsCOMPtr<nsICookieJarSettings> otherCjs;
  if (NS_FAILED(aLoadInfo->GetCookieJarSettings(getter_AddRefs(otherCjs)))) {
    return false;
  }

  nsAutoString partitionKey;
  nsAutoString otherPartitionKey;
  cjs->GetPartitionKey(partitionKey);
  otherCjs->GetPartitionKey(otherPartitionKey);
  return partitionKey == otherPartitionKey;
}

bool EarlyHintPreloader::Register(dom::ContentParentId aCpId,
                                  EarlyHintConnectArgs& aOut) {
  mCpId = aCpId;

  // Set minimum delay of 1ms to always start the timer after the function call
  // completed.
  nsresult rv = NS_NewTimerWithCallback(
      getter_AddRefs(mTimer), this,
      std::max(StaticPrefs::network_early_hints_parent_connect_timeout(),
               (uint32_t)1),
      nsITimer::TYPE_ONE_SHOT);
  if (NS_FAILED(rv)) {
    MOZ_ASSERT(!mTimer);
    CancelChannel(NS_ERROR_ABORT, "new-timer-failed"_ns,
                  /* aDeleteEntry */ false);
    return false;
  }

  // Create an entry in the redirect channel registrar
  RefPtr<EarlyHintRegistrar> registrar = EarlyHintRegistrar::GetOrCreate();
  registrar->RegisterEarlyHint(mConnectArgs.earlyHintPreloaderId(), this);

  aOut = mConnectArgs;
  return true;
}

nsresult EarlyHintPreloader::CancelChannel(nsresult aStatus,
                                           const nsACString& aReason,
                                           bool aDeleteEntry) {
  LOG(("EarlyHintPreloader::CancelChannel [this=%p]\n", this));

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }
  if (aDeleteEntry) {
    RefPtr<EarlyHintRegistrar> registrar = EarlyHintRegistrar::GetOrCreate();
    registrar->DeleteEntry(mCpId, mConnectArgs.earlyHintPreloaderId());
  }
  // clear redirect channel in case this channel is cleared between the call of
  // EarlyHintPreloader::AsyncOnChannelRedirect and
  // EarlyHintPreloader::OnRedirectResult
  mRedirectChannel = nullptr;
  if (mChannel) {
    if (mSuspended) {
      mChannel->Resume();
    }
    mChannel->CancelWithReason(aStatus, aReason);
    // Clearing mChannel is safe, because this EarlyHintPreloader is not in the
    // EarlyHintRegistrar after this function call and we won't call
    // SetHttpChannelFromEarlyHintPreloader nor OnStartRequest on mParent.
    mChannel = nullptr;
    SetState(ePreloaderCancelled);
  }
  return NS_OK;
}

void EarlyHintPreloader::OnParentReady(nsIParentChannel* aParent) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aParent);
  LOG(("EarlyHintPreloader::OnParentReady [this=%p]\n", this));

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(mChannel, "earlyhints-connectback", nullptr);
  }

  mParent = aParent;

  if (mTimer) {
    mTimer->Cancel();
    mTimer = nullptr;
  }

  RefPtr<EarlyHintRegistrar> registrar = EarlyHintRegistrar::GetOrCreate();
  registrar->DeleteEntry(mCpId, mConnectArgs.earlyHintPreloaderId());

  if (mOnStartRequestCalled) {
    SetParentChannel();
    InvokeStreamListenerFunctions();
  }
}

void EarlyHintPreloader::SetParentChannel() {
  RefPtr<HttpBaseChannel> channel = do_QueryObject(mChannel);
  RefPtr<HttpChannelParent> parent = do_QueryObject(mParent);
  parent->SetHttpChannelFromEarlyHintPreloader(channel);
}

// Adapted from
// https://searchfox.org/mozilla-central/rev/b4150d1c6fae0c51c522df2d2c939cf5ad331d4c/netwerk/ipc/DocumentLoadListener.cpp#1311
void EarlyHintPreloader::InvokeStreamListenerFunctions() {
  AssertIsOnMainThread();
  RefPtr<EarlyHintPreloader> self(this);

  LOG((
      "EarlyHintPreloader::InvokeStreamListenerFunctions [this=%p parent=%p]\n",
      this, mParent.get()));

  // If we failed to suspend the channel, then we might have received
  // some messages while the redirected was being handled.
  // Manually send them on now.
  if (!mIsFinished) {
    // This is safe to do, because OnStartRequest/OnStopRequest/OnDataAvailable
    // are all called on the main thread. They can't be called until we worked
    // through all functions in the streamListnerFunctions array.
    mParentListener->SetListenerAfterRedirect(mParent);
  }
  nsTArray<StreamListenerFunction> streamListenerFunctions =
      std::move(mStreamListenerFunctions);

  ForwardStreamListenerFunctions(std::move(streamListenerFunctions), mParent);

  // We don't expect to get new stream listener functions added
  // via re-entrancy. If this ever happens, we should understand
  // exactly why before allowing it.
  NS_ASSERTION(mStreamListenerFunctions.IsEmpty(),
               "Should not have added new stream listener function!");

  if (mChannel && mSuspended) {
    mChannel->Resume();
  }
  mChannel = nullptr;
  mParent = nullptr;
  mParentListener = nullptr;

  SetState(ePreloaderUsed);
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(EarlyHintPreloader, nsIRequestObserver, nsIStreamListener,
                  nsIChannelEventSink, nsIInterfaceRequestor,
                  nsIRedirectResultListener, nsIMultiPartChannelListener,
                  nsINamed, nsITimerCallback);

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsIStreamListener
//-----------------------------------------------------------------------------

// Implementation copied and adapted from DocumentLoadListener::OnStartRequest
// https://searchfox.org/mozilla-central/rev/380fc5571b039fd453b45bbb64ed13146fe9b066/netwerk/ipc/DocumentLoadListener.cpp#2317-2508
NS_IMETHODIMP
EarlyHintPreloader::OnStartRequest(nsIRequest* aRequest) {
  LOG(("EarlyHintPreloader::OnStartRequest [this=%p]\n", this));
  AssertIsOnMainThread();

  mOnStartRequestCalled = true;

  nsCOMPtr<nsIMultiPartChannel> multiPartChannel = do_QueryInterface(aRequest);
  if (multiPartChannel) {
    multiPartChannel->GetBaseChannel(getter_AddRefs(mChannel));
  } else {
    mChannel = do_QueryInterface(aRequest);
  }
  MOZ_DIAGNOSTIC_ASSERT(mChannel);

  nsresult status = NS_OK;
  (void)aRequest->GetStatus(&status);

  if (nsCOMPtr<nsIParentChannel> parent = mParent) {
    SetParentChannel();
    parent->OnStartRequest(aRequest);
    InvokeStreamListenerFunctions();
  } else {
    // Don't suspend the chanel when the channel got cancelled with
    // CancelChannel, because then OnStopRequest wouldn't get called and we
    // wouldn't clean up the channel.
    if (NS_SUCCEEDED(status)) {
      mChannel->Suspend();
      mSuspended = true;
    }
    mStreamListenerFunctions.AppendElement(
        AsVariant(OnStartRequestParams{aRequest}));
  }

  // return error after adding the OnStartRequest forward. The OnStartRequest
  // failure has to be forwarded to listener, because they called AsyncOpen on
  // this channel
  return status;
}

// Implementation copied from DocumentLoadListener::OnStopRequest
// https://searchfox.org/mozilla-central/rev/380fc5571b039fd453b45bbb64ed13146fe9b066/netwerk/ipc/DocumentLoadListener.cpp#2510-2528
NS_IMETHODIMP
EarlyHintPreloader::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  AssertIsOnMainThread();
  LOG(("EarlyHintPreloader::OnStopRequest [this=%p]\n", this));
  mStreamListenerFunctions.AppendElement(
      AsVariant(OnStopRequestParams{aRequest, aStatusCode}));

  // If we're not a multi-part channel, then we're finished and we don't
  // expect any further events. If we are, then this might be called again,
  // so wait for OnAfterLastPart instead.
  nsCOMPtr<nsIMultiPartChannel> multiPartChannel = do_QueryInterface(aRequest);
  if (!multiPartChannel) {
    mIsFinished = true;
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsIStreamListener
//-----------------------------------------------------------------------------

// Implementation copied from DocumentLoadListener::OnDataAvailable
// https://searchfox.org/mozilla-central/rev/380fc5571b039fd453b45bbb64ed13146fe9b066/netwerk/ipc/DocumentLoadListener.cpp#2530-2549
NS_IMETHODIMP
EarlyHintPreloader::OnDataAvailable(nsIRequest* aRequest,
                                    nsIInputStream* aInputStream,
                                    uint64_t aOffset, uint32_t aCount) {
  AssertIsOnMainThread();
  LOG(("EarlyHintPreloader::OnDataAvailable [this=%p]\n", this));
  // This isn't supposed to happen, since we suspended the channel, but
  // sometimes Suspend just doesn't work. This can happen when we're routing
  // through nsUnknownDecoder to sniff the content type, and it doesn't handle
  // being suspended. Let's just store the data and manually forward it to our
  // redirected channel when it's ready.
  nsCString data;
  nsresult rv = NS_ReadInputStreamToString(aInputStream, data, aCount);
  NS_ENSURE_SUCCESS(rv, rv);

  mStreamListenerFunctions.AppendElement(AsVariant(
      OnDataAvailableParams{aRequest, std::move(data), aOffset, aCount}));

  return NS_OK;
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsIMultiPartChannelListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EarlyHintPreloader::OnAfterLastPart(nsresult aStatus) {
  LOG(("EarlyHintPreloader::OnAfterLastPart [this=%p]", this));
  mStreamListenerFunctions.AppendElement(
      AsVariant(OnAfterLastPartParams{aStatus}));
  mIsFinished = true;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsIChannelEventSink
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EarlyHintPreloader::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* callback) {
  LOG(("EarlyHintPreloader::AsyncOnChannelRedirect [this=%p]", this));
  nsCOMPtr<nsIURI> newURI;
  nsresult rv = NS_GetFinalChannelURI(aNewChannel, getter_AddRefs(newURI));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aNewChannel->GetURI(getter_AddRefs(newURI));
  if (NS_FAILED(rv)) {
    callback->OnRedirectVerifyCallback(rv);
    return NS_OK;
  }

  // HTTP request headers are not automatically forwarded to the new channel.
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aNewChannel);
  NS_ENSURE_STATE(httpChannel);

  rv = httpChannel->SetRequestHeader("X-Moz"_ns, "early hint"_ns, false);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // Assign to mChannel after we get notification about success of the
  // redirect in OnRedirectResult.
  mRedirectChannel = aNewChannel;

  callback->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsIRedirectResultListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EarlyHintPreloader::OnRedirectResult(nsresult aStatus) {
  LOG(("EarlyHintPreloader::OnRedirectResult [this=%p] aProceeding=0x%" PRIx32,
       this, static_cast<uint32_t>(aStatus)));
  if (NS_SUCCEEDED(aStatus) && mRedirectChannel) {
    mChannel = mRedirectChannel;
  }

  mRedirectChannel = nullptr;

  return NS_OK;
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsINamed
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EarlyHintPreloader::GetName(nsACString& aName) {
  aName.AssignLiteral("EarlyHintPreloader");
  return NS_OK;
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsITimerCallback
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EarlyHintPreloader::Notify(nsITimer* timer) {
  // Death grip, because we will most likely remove the last reference when
  // deleting us from the EarlyHintRegistrar
  RefPtr<EarlyHintPreloader> deathGrip(this);

  RefPtr<EarlyHintRegistrar> registrar = EarlyHintRegistrar::GetOrCreate();
  registrar->DeleteEntry(mCpId, mConnectArgs.earlyHintPreloaderId());

  mTimer = nullptr;
  mRedirectChannel = nullptr;
  if (mChannel) {
    if (mSuspended) {
      mChannel->Resume();
    }
    mChannel->CancelWithReason(NS_ERROR_ABORT, "parent-connect-timeout"_ns);
#ifndef ANDROID
    glean::netwerk::parent_connect_timeout.Add(1);
#endif
    mChannel = nullptr;
  }
  SetState(ePreloaderTimeout);

  return NS_OK;
}

//-----------------------------------------------------------------------------
// EarlyHintPreloader::nsIInterfaceRequestor
//-----------------------------------------------------------------------------

NS_IMETHODIMP
EarlyHintPreloader::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    NS_ADDREF_THIS();
    *aResult = static_cast<nsIChannelEventSink*>(this);
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsIRedirectResultListener))) {
    NS_ADDREF_THIS();
    *aResult = static_cast<nsIRedirectResultListener*>(this);
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsILoadContext)) && mLoadContext != nullptr) {
    nsCOMPtr<nsILoadContext> loadContext = mLoadContext;
    loadContext.forget(aResult);
    return NS_OK;
  }

  return NS_ERROR_NO_INTERFACE;
}
}  // namespace mozilla::net
