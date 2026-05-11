/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DocumentLoadListener.h"

#include "imgLoader.h"
#include "NeckoCommon.h"
#include "nsLoadGroup.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DynamicFpiNavigationHeuristic.h"
#include "mozilla/Components.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ResultVariant.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ChildProcessChannelListener.h"
#include "mozilla/dom/ClientChannelHelper.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/ContentProcessManager.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/HttpChannelParent.h"
#include "mozilla/net/RedirectChannelRegistrar.h"
#include "nsContentSecurityUtils.h"
#include "nsContentSecurityManager.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsDocShellLoadTypes.h"
#include "nsDOMNavigationTiming.h"
#include "nsDSURIContentListener.h"
#include "nsObjectLoadingContent.h"
#include "nsOpenWindowInfo.h"
#include "nsExternalHelperAppService.h"
#include "nsHttpChannel.h"
#include "nsIBrowser.h"
#include "nsIClassifiedChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsINetworkInterceptController.h"
#include "nsIStreamConverterService.h"
#include "nsIViewSourceChannel.h"
#include "nsImportModule.h"
#include "nsIXULRuntime.h"
#include "nsMimeTypes.h"
#include "nsQueryObject.h"
#include "nsRedirectHistoryEntry.h"
#include "nsSandboxFlags.h"
#include "nsScriptSecurityManager.h"
#include "nsSHistory.h"
#include "nsStringStream.h"
#include "nsURILoader.h"
#include "nsWebNavigationInfo.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/RemoteWebProgressRequest.h"
#include "mozilla/net/UrlClassifierFeatureFactory.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/intl/Localization.h"
#include "nsDocLoader.h"  // for FormatStatusMessage

#ifdef ANDROID
#  include "mozilla/widget/nsWindow.h"
#endif /* ANDROID */

mozilla::LazyLogModule gDocumentChannelLog("DocumentChannel");
#define LOG(fmt) MOZ_LOG(gDocumentChannelLog, mozilla::LogLevel::Verbose, fmt)

extern mozilla::LazyLogModule gSHIPBFCacheLog;

// Bug 136580: Limit to the number of nested content frames that can have the
//             same URL. This is to stop content that is recursively loading
//             itself.  Note that "#foo" on the end of URL doesn't affect
//             whether it's considered identical, but "?foo" or ";foo" are
//             considered and compared.
// Limit this to 2, like chromium does.
static constexpr int kMaxSameURLContentFrames = 2;

using namespace mozilla::dom;

namespace mozilla {
namespace net {

static StaticRefPtr<mozilla::intl::Localization> sL10n;

static ContentParentId GetContentProcessId(ContentParent* aContentParent) {
  return aContentParent ? aContentParent->ChildID() : ContentParentId{0};
}

static void SetNeedToAddURIVisit(nsIChannel* aChannel,
                                 bool aNeedToAddURIVisit) {
  nsCOMPtr<nsIWritablePropertyBag2> props(do_QueryInterface(aChannel));
  if (!props) {
    return;
  }

  props->SetPropertyAsBool(u"docshell.needToAddURIVisit"_ns,
                           aNeedToAddURIVisit);
}

static auto SecurityFlagsForLoadInfo(nsDocShellLoadState* aLoadState)
    -> nsSecurityFlags {
  // TODO: Block copied from nsDocShell::DoURILoad, refactor out somewhere
  nsSecurityFlags securityFlags =
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL;

  if (aLoadState->LoadType() == LOAD_ERROR_PAGE) {
    securityFlags |= nsILoadInfo::SEC_LOAD_ERROR_PAGE;
  }

  if (aLoadState->PrincipalToInherit()) {
    nsIURI* uri = aLoadState->URI();
    bool isSrcdoc = aLoadState->HasInternalLoadFlags(
        nsDocShell::INTERNAL_LOAD_FLAGS_IS_SRCDOC);
    bool inheritAttrs = nsContentUtils::ChannelShouldInheritPrincipal(
        aLoadState->PrincipalToInherit(), uri,
        true,  // aInheritForAboutBlank
        isSrcdoc);

    if (inheritAttrs && !uri->SchemeIs("data")) {
      securityFlags |= nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
    }
  }

  return securityFlags;
}

// Construct a LoadInfo object to use when creating the internal channel for a
// Document/SubDocument load.
static auto CreateDocumentLoadInfo(CanonicalBrowsingContext* aBrowsingContext,
                                   nsDocShellLoadState* aLoadState)
    -> already_AddRefed<LoadInfo> {
  uint32_t sandboxFlags = aBrowsingContext->GetSandboxFlags();
  RefPtr<LoadInfo> loadInfo;

  auto securityFlags = SecurityFlagsForLoadInfo(aLoadState);

  if (aBrowsingContext->GetParent()) {
    loadInfo = LoadInfo::CreateForFrame(
        aBrowsingContext, aLoadState->TriggeringPrincipal(),
        aLoadState->GetEffectiveTriggeringRemoteType(), securityFlags,
        sandboxFlags);
  } else {
    OriginAttributes attrs;
    aBrowsingContext->GetOriginAttributes(attrs);
    loadInfo = LoadInfo::CreateForDocument(
        aBrowsingContext, aLoadState->URI(), aLoadState->TriggeringPrincipal(),
        aLoadState->GetEffectiveTriggeringRemoteType(), attrs, securityFlags,
        sandboxFlags);
  }

  if (aLoadState->IsExemptFromHTTPSFirstMode() &&
      nsHTTPSOnlyUtils::GetUpgradeMode(loadInfo) ==
          nsHTTPSOnlyUtils::HTTPS_FIRST_MODE) {
    uint32_t httpsOnlyStatus = loadInfo->GetHttpsOnlyStatus();
    httpsOnlyStatus |= nsILoadInfo::HTTPS_ONLY_EXEMPT;
    loadInfo->SetHttpsOnlyStatus(httpsOnlyStatus);
  }

  loadInfo->SetSchemelessInput(aLoadState->GetSchemelessInput());
  loadInfo->SetHttpsUpgradeTelemetry(aLoadState->GetHttpsUpgradeTelemetry());

  loadInfo->SetTriggeringSandboxFlags(aLoadState->TriggeringSandboxFlags());
  loadInfo->SetTriggeringWindowId(aLoadState->TriggeringWindowId());
  loadInfo->SetTriggeringStorageAccess(aLoadState->TriggeringStorageAccess());
  ClassificationFlags classificationFlags =
      aLoadState->TriggeringClassificationFlags();
  loadInfo->SetTriggeringFirstPartyClassificationFlags(
      classificationFlags.firstPartyFlags);
  loadInfo->SetTriggeringThirdPartyClassificationFlags(
      classificationFlags.thirdPartyFlags);
  loadInfo->SetHasValidUserGestureActivation(
      aLoadState->HasValidUserGestureActivation());
  // External loads (e.g. links opened from other apps) are always
  // user-initiated, so text fragment directives are allowed to scroll.
  // XXX: This is inconsistent to the other code path that sets user activation
  // based on the EXTERNAL load flag (nsDocShell::DoURILoad), which also sets
  // the "normal" user activation if the flag is present. We should figure out
  // if we should do this here as well.
  loadInfo->SetTextDirectiveUserActivation(
      aLoadState->GetTextDirectiveUserActivation() ||
      aLoadState->HasLoadFlags(nsIWebNavigation::LOAD_FLAGS_FROM_EXTERNAL));
  loadInfo->SetIsMetaRefresh(aLoadState->IsMetaRefresh());

  return loadInfo.forget();
}

// Construct a LoadInfo object to use when creating the internal channel for an
// Object/Embed load.
static auto CreateObjectLoadInfo(nsDocShellLoadState* aLoadState,
                                 uint64_t aInnerWindowId,
                                 nsContentPolicyType aContentPolicyType,
                                 uint32_t aSandboxFlags)
    -> already_AddRefed<LoadInfo> {
  RefPtr<WindowGlobalParent> wgp =
      WindowGlobalParent::GetByInnerWindowId(aInnerWindowId);
  MOZ_RELEASE_ASSERT(wgp);

  auto securityFlags = SecurityFlagsForLoadInfo(aLoadState);

  RefPtr<LoadInfo> loadInfo = LoadInfo::CreateForNonDocument(
      wgp, wgp->DocumentPrincipal(), aContentPolicyType, securityFlags,
      aSandboxFlags);

  loadInfo->SetHasValidUserGestureActivation(
      aLoadState->HasValidUserGestureActivation());
  loadInfo->SetTextDirectiveUserActivation(
      aLoadState->GetTextDirectiveUserActivation());
  loadInfo->SetTriggeringSandboxFlags(aLoadState->TriggeringSandboxFlags());
  loadInfo->SetTriggeringWindowId(aLoadState->TriggeringWindowId());
  loadInfo->SetTriggeringStorageAccess(aLoadState->TriggeringStorageAccess());
  net::ClassificationFlags classificationFlags =
      aLoadState->TriggeringClassificationFlags();
  loadInfo->SetTriggeringFirstPartyClassificationFlags(
      classificationFlags.firstPartyFlags);
  loadInfo->SetTriggeringThirdPartyClassificationFlags(
      classificationFlags.thirdPartyFlags);
  loadInfo->SetIsMetaRefresh(aLoadState->IsMetaRefresh());

  return loadInfo.forget();
}

/**
 * An extension to nsDocumentOpenInfo that we run in the parent process, so
 * that we can make the decision to retarget to content handlers or the external
 * helper app, before we make process switching decisions.
 *
 * This modifies the behaviour of nsDocumentOpenInfo so that it can do
 * retargeting, but doesn't do stream conversion (but confirms that we will be
 * able to do so later).
 *
 * We still run nsDocumentOpenInfo in the content process, but disable
 * retargeting, so that it can only apply stream conversion, and then send data
 * to the docshell.
 */
class ParentProcessDocumentOpenInfo final : public nsDocumentOpenInfo,
                                            public nsIMultiPartChannelListener {
 public:
  ParentProcessDocumentOpenInfo(ParentChannelListener* aListener,
                                uint32_t aFlags,
                                mozilla::dom::BrowsingContext* aBrowsingContext,
                                const nsACString& aTypeHint,
                                bool aIsDocumentLoad)
      : nsDocumentOpenInfo(aFlags, false),
        mBrowsingContext(aBrowsingContext),
        mListener(aListener),
        mTypeHint(aTypeHint),
        mIsDocumentLoad(aIsDocumentLoad) {
    LOG(("ParentProcessDocumentOpenInfo ctor [this=%p]", this));
  }

  NS_DECL_ISUPPORTS_INHERITED

  // The default content listener is always a docshell (potentially with an
  // interstitial nsObjectLoadingContent), so this manually implements the same
  // checks, and if it succeeds, uses the parent channel listener so that we
  // forward onto DocumentLoadListener.
  bool TryDefaultContentListener(nsIChannel* aChannel,
                                 const nsCString& aContentType) {
    uint32_t canHandle = nsWebNavigationInfo::IsTypeSupported(aContentType);
    if (canHandle != nsIWebNavigationInfo::UNSUPPORTED) {
      m_targetStreamListener = mListener;
      nsLoadFlags loadFlags = 0;
      aChannel->GetLoadFlags(&loadFlags);
      aChannel->SetLoadFlags(loadFlags | nsIChannel::LOAD_TARGETED);
      return true;
    }
    return false;
  }

  bool TryDefaultContentListener(nsIChannel* aChannel) override {
    return TryDefaultContentListener(aChannel, mContentType);
  }

  // Generally we only support stream converters that can tell
  // use exactly what type they'll output. If we find one, then
  // we just target to our default listener directly (without
  // conversion), and the content process nsDocumentOpenInfo will
  // run and do the actual conversion.
  nsresult TryStreamConversion(nsIChannel* aChannel) override {
    // The one exception is nsUnknownDecoder, which works in the parent
    // (and we need to know what the content type is before we can
    // decide if it will be handled in the parent), so we run that here.
    if (mContentType.LowerCaseEqualsASCII(UNKNOWN_CONTENT_TYPE) ||
        mContentType.IsEmpty()) {
      return nsDocumentOpenInfo::TryStreamConversion(aChannel);
    }

    nsresult rv;
    nsCOMPtr<nsIStreamConverterService> streamConvService;
    nsAutoCString str;
    streamConvService = mozilla::components::StreamConverter::Service(&rv);
    rv = streamConvService->ConvertedType(mContentType, aChannel, str);
    NS_ENSURE_SUCCESS(rv, rv);

    // We only support passing data to the default content listener
    // (docshell), and we don't supported chaining converters.
    if (TryDefaultContentListener(aChannel, str)) {
      mContentType = str;
      return NS_OK;
    }
    // This is the same result as nsStreamConverterService uses when it
    // can't find a converter
    return NS_ERROR_FAILURE;
  }

  nsresult TryExternalHelperApp(nsIExternalHelperAppService* aHelperAppService,
                                nsIChannel* aChannel) override {
    RefPtr<nsIStreamListener> listener;
    nsresult rv = aHelperAppService->CreateListener(
        mContentType, aChannel, mBrowsingContext, false, nullptr,
        getter_AddRefs(listener));
    if (NS_SUCCEEDED(rv)) {
      m_targetStreamListener = listener;
    }
    return rv;
  }

  nsDocumentOpenInfo* Clone() override {
    mCloned = true;
    return new ParentProcessDocumentOpenInfo(
        mListener, mFlags, mBrowsingContext, mTypeHint, mIsDocumentLoad);
  }

  nsresult OnDocumentStartRequest(nsIRequest* request) {
    LOG(("ParentProcessDocumentOpenInfo OnDocumentStartRequest [this=%p]",
         this));

    return nsDocumentOpenInfo::OnStartRequest(request);
  }

  nsresult OnObjectStartRequest(nsIRequest* request) {
    LOG(("ParentProcessDocumentOpenInfo OnObjectStartRequest [this=%p]", this));

    // Respect the specified image MIME type if loading binary content type into
    // an object/embed element.
    if (nsCOMPtr<nsIChannel> channel = do_QueryInterface(request)) {
      nsAutoCString channelType;
      channel->GetContentType(channelType);
      if (!mTypeHint.IsEmpty() &&
          imgLoader::SupportImageWithMimeType(mTypeHint) &&
          (channelType.EqualsASCII(APPLICATION_GUESS_FROM_EXT) ||
           channelType.EqualsASCII(APPLICATION_OCTET_STREAM) ||
           channelType.EqualsASCII(BINARY_OCTET_STREAM))) {
        channel->SetContentType(mTypeHint);
      }
    }

    // If the load is considered to have failed, we're going to display fallback
    // content in the nsDocShellLoadingContent. Cancel the channel to reflect
    // this.
    nsresult status = NS_OK;
    if (!nsObjectLoadingContent::IsSuccessfulRequest(request, &status)) {
      LOG(("OnObjectStartRequest for unsuccessful request [this=%p, status=%s]",
           this, GetStaticErrorName(status)));
      return NS_ERROR_WONT_HANDLE_CONTENT;
    }

    // All successful object loads will be treated as document loads, so run
    // through nsDocumentOpenInfo. This will check the MIME type to ensure it is
    // supported, and attempt stream conversions where applicable.
    //
    // If the dom.navigation.object_embed.allow_retargeting pref is enabled,
    // this may lead to the resource being downloaded.
    return OnDocumentStartRequest(request);
  }

  NS_IMETHOD OnStartRequest(nsIRequest* request) override {
    LOG(("ParentProcessDocumentOpenInfo OnStartRequest [this=%p]", this));

    nsresult rv = mIsDocumentLoad ? OnDocumentStartRequest(request)
                                  : OnObjectStartRequest(request);

    // If we didn't find a content handler, and we don't have a listener, then
    // just forward to our default listener. This happens when the channel is in
    // an error state, and we want to just forward that on to be handled in the
    // content process, or when DONT_RETARGET is set.
    if (!mUsedContentHandler && !m_targetStreamListener) {
      m_targetStreamListener = mListener;
      if (NS_FAILED(rv)) {
        LOG(("nsDocumentOpenInfo OnStartRequest Failed [this=%p, rv=%s]", this,
             GetStaticErrorName(rv)));
        request->CancelWithReason(
            rv, "nsDocumentOpenInfo::OnStartRequest failed"_ns);
      }
      return m_targetStreamListener->OnStartRequest(request);
    }
    if (m_targetStreamListener != mListener) {
      LOG(
          ("ParentProcessDocumentOpenInfo targeted to non-default listener "
           "[this=%p]",
           this));
      // If this is the only part, then we can immediately tell our listener
      // that it won't be getting any content and disconnect it. For multipart
      // channels we have to wait until we've handled all parts before we know.
      // This does mean that the content process can still Cancel() a multipart
      // response while the response is being handled externally, but this
      // matches the single-process behaviour.
      // If we got cloned, then we don't need to do this, as only the last link
      // needs to do it.
      // Multi-part channels are guaranteed to call OnAfterLastPart, which we
      // forward to the listeners, so it will handle disconnection at that
      // point.
      nsCOMPtr<nsIMultiPartChannel> multiPartChannel =
          do_QueryInterface(request);
      if (!multiPartChannel && !mCloned) {
        DisconnectChildListeners(NS_FAILED(rv) ? rv : NS_BINDING_RETARGETED,
                                 rv);
      }
    }

    return rv;
  }

  NS_IMETHOD OnAfterLastPart(nsresult aStatus) override {
    mListener->OnAfterLastPart(aStatus);
    return NS_OK;
  }

 private:
  virtual ~ParentProcessDocumentOpenInfo() {
    LOG(("ParentProcessDocumentOpenInfo dtor [this=%p]", this));
  }

  void DisconnectChildListeners(nsresult aStatus, nsresult aLoadGroupStatus) {
    // Tell the DocumentLoadListener to notify the content process that it's
    // been entirely retargeted, and to stop waiting.
    // Clear mListener's pointer to the DocumentLoadListener to break the
    // reference cycle.
    RefPtr<DocumentLoadListener> doc = do_GetInterface(ToSupports(mListener));
    MOZ_ASSERT(doc);
    doc->DisconnectListeners(aStatus, aLoadGroupStatus);
    mListener->SetListenerAfterRedirect(nullptr);
  }

  RefPtr<mozilla::dom::BrowsingContext> mBrowsingContext;
  RefPtr<ParentChannelListener> mListener;
  nsCString mTypeHint;
  const bool mIsDocumentLoad;

  /**
   * Set to true if we got cloned to create a chained listener.
   */
  bool mCloned = false;
};

NS_IMPL_ADDREF_INHERITED(ParentProcessDocumentOpenInfo, nsDocumentOpenInfo)
NS_IMPL_RELEASE_INHERITED(ParentProcessDocumentOpenInfo, nsDocumentOpenInfo)

NS_INTERFACE_MAP_BEGIN(ParentProcessDocumentOpenInfo)
  NS_INTERFACE_MAP_ENTRY(nsIMultiPartChannelListener)
NS_INTERFACE_MAP_END_INHERITING(nsDocumentOpenInfo)

NS_IMPL_ADDREF(DocumentLoadListener)
NS_IMPL_RELEASE(DocumentLoadListener)

NS_INTERFACE_MAP_BEGIN(DocumentLoadListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIParentChannel)
  NS_INTERFACE_MAP_ENTRY(nsIAsyncVerifyRedirectReadyCallback)
  NS_INTERFACE_MAP_ENTRY(nsIChannelEventSink)
  NS_INTERFACE_MAP_ENTRY(nsIMultiPartChannelListener)
  NS_INTERFACE_MAP_ENTRY(nsIProgressEventSink)
  NS_INTERFACE_MAP_ENTRY(nsIEarlyHintObserver)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(DocumentLoadListener)
NS_INTERFACE_MAP_END

DocumentLoadListener::DocumentLoadListener(
    CanonicalBrowsingContext* aLoadingBrowsingContext, bool aIsDocumentLoad)
    : mIsDocumentLoad(aIsDocumentLoad) {
  LOG(("DocumentLoadListener ctor [this=%p]", this));
  mParentChannelListener =
      new ParentChannelListener(this, aLoadingBrowsingContext);
}

DocumentLoadListener::~DocumentLoadListener() {
  LOG(("DocumentLoadListener dtor [this=%p]", this));
}

void DocumentLoadListener::AddURIVisit(nsIChannel* aChannel,
                                       uint32_t aLoadFlags) {
  if (mLoadStateLoadType == LOAD_ERROR_PAGE ||
      mLoadStateLoadType == LOAD_BYPASS_HISTORY) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  NS_GetFinalChannelURI(aChannel, getter_AddRefs(uri));

  nsCOMPtr<nsIURI> previousURI;
  uint32_t previousFlags = 0;
  if (mLoadStateLoadType & nsIDocShell::LOAD_CMD_RELOAD) {
    previousURI = uri;
  } else {
    nsDocShell::ExtractLastVisit(aChannel, getter_AddRefs(previousURI),
                                 &previousFlags);
  }

  // Get the HTTP response code, if available.
  uint32_t responseStatus = 0;
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
  if (httpChannel) {
    (void)httpChannel->GetResponseStatus(&responseStatus);
  }

  RefPtr<CanonicalBrowsingContext> browsingContext =
      GetDocumentBrowsingContext();
  nsCOMPtr<nsIWidget> widget =
      browsingContext->GetParentProcessWidgetContaining();

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  // Check if the URI has a http scheme and if either HSTS is enabled for this
  // host, or if we were upgraded by HTTPS-Only/First. If this is the case, we
  // want to mark this URI specially, as it will be followed shortly by an
  // almost identical https history entry. That way the global history
  // implementation can handle the visit appropriately (e.g. by marking it as
  // `hidden`, so only the https url appears in default history views).
  bool wasUpgraded =
      uri->SchemeIs("http") &&
      (loadInfo->GetHstsStatus() ||
       (loadInfo->GetHttpsOnlyStatus() &
        (nsILoadInfo::HTTPS_ONLY_UPGRADED_HTTPS_FIRST |
         nsILoadInfo::HTTPS_ONLY_UPGRADED_LISTENER_NOT_REGISTERED |
         nsILoadInfo::HTTPS_ONLY_UPGRADED_LISTENER_REGISTERED)));

  nsDocShell::InternalAddURIVisit(uri, previousURI, previousFlags,
                                  responseStatus, browsingContext, widget,
                                  mLoadStateLoadType, wasUpgraded);
}

CanonicalBrowsingContext* DocumentLoadListener::GetLoadingBrowsingContext()
    const {
  return mParentChannelListener ? mParentChannelListener->GetBrowsingContext()
                                : nullptr;
}

CanonicalBrowsingContext* DocumentLoadListener::GetDocumentBrowsingContext()
    const {
  return mIsDocumentLoad ? GetLoadingBrowsingContext() : nullptr;
}

CanonicalBrowsingContext* DocumentLoadListener::GetTopBrowsingContext() const {
  auto* loadingContext = GetLoadingBrowsingContext();
  return loadingContext ? loadingContext->Top() : nullptr;
}

WindowGlobalParent* DocumentLoadListener::GetParentWindowContext() const {
  return mParentWindowContext;
}

bool CheckRecursiveLoad(CanonicalBrowsingContext* aLoadingContext,
                        nsDocShellLoadState* aLoadState, bool aIsDocumentLoad) {
  if (!aLoadState->ShouldCheckForRecursion()) {
    return true;
  }

  // Bug 136580: Check for recursive frame loading excluding about:srcdoc URIs.
  // srcdoc URIs require their contents to be specified inline, so it isn't
  // possible for undesirable recursion to occur without the aid of a
  // non-srcdoc URI,  which this method will block normally.
  // Besides, URI is not enough to guarantee uniqueness of srcdoc documents.
  nsAutoCString buffer;
  if (aLoadState->URI()->SchemeIs("about")) {
    nsresult rv = aLoadState->URI()->GetPathQueryRef(buffer);
    if (NS_SUCCEEDED(rv) && buffer.EqualsLiteral("srcdoc")) {
      // Duplicates allowed up to depth limits
      return true;
    }
  }

  RefPtr<WindowGlobalParent> parent;
  if (!aIsDocumentLoad) {  // object load
    parent = aLoadingContext->GetCurrentWindowGlobal();
  } else {
    parent = aLoadingContext->GetParentWindowContext();
  }

  int matchCount = 0;
  CanonicalBrowsingContext* ancestorBC;
  for (WindowGlobalParent* ancestorWGP = parent; ancestorWGP;
       ancestorWGP = ancestorBC->GetParentWindowContext()) {
    ancestorBC = ancestorWGP->BrowsingContext();
    MOZ_ASSERT(ancestorBC);
    if (nsCOMPtr<nsIURI> parentURI = ancestorWGP->GetDocumentURI()) {
      bool equal;
      nsresult rv = aLoadState->URI()->EqualsExceptRef(parentURI, &equal);
      NS_ENSURE_SUCCESS(rv, false);

      if (equal) {
        matchCount++;
        if (matchCount >= kMaxSameURLContentFrames) {
          NS_WARNING(
              "Too many nested content frames/objects have the same url "
              "(recursion?) "
              "so giving up");
          return false;
        }
      }
    }
  }
  return true;
}

// Check that the load state, potentially received from a child process, appears
// to be performing a load of the specified LoadingSessionHistoryInfo.
// Returns a Result<…> containing the SessionHistoryEntry found for the
// LoadingSessionHistoryInfo as success value if the validation succeeded, or a
// static (telemetry-safe) string naming what did not match as a failure value
// if the validation failed.
static Result<SessionHistoryEntry*, const char*> ValidateHistoryLoad(
    CanonicalBrowsingContext* aLoadingContext,
    nsDocShellLoadState* aLoadState) {
  MOZ_ASSERT(aLoadState->LoadIsFromSessionHistory());

  if (!aLoadState->GetLoadingSessionHistoryInfo()) {
    return Err("Missing LoadingSessionHistoryInfo");
  }

  SessionHistoryEntry::LoadingEntry* loading = SessionHistoryEntry::GetByLoadId(
      aLoadState->GetLoadingSessionHistoryInfo()->mLoadId);
  if (!loading) {
    return Err("Missing SessionHistoryEntry");
  }

  SessionHistoryInfo* snapshot = loading->mInfoSnapshotForValidation.get();
  // History loads do not inherit principal.
  if (aLoadState->HasInternalLoadFlags(
          nsDocShell::INTERNAL_LOAD_FLAGS_INHERIT_PRINCIPAL)) {
    return Err("LOAD_FLAGS_INHERIT_PRINCIPAL");
  }

  auto uriEq = [](nsIURI* a, nsIURI* b) -> bool {
    bool eq = false;
    return a == b || (a && b && NS_SUCCEEDED(a->Equals(b, &eq)) && eq);
  };
  auto principalEq = [](nsIPrincipal* a, nsIPrincipal* b) -> bool {
    return a == b || (a && b && a->Equals(b));
  };

  // XXX: Needing to do all of this validation manually is kinda gross.
  if (!uriEq(snapshot->GetURI(), aLoadState->URI())) {
    return Err("URI");
  }
  if (!uriEq(snapshot->GetOriginalURI(), aLoadState->OriginalURI())) {
    return Err("OriginalURI");
  }
  if (!aLoadState->ResultPrincipalURIIsSome() ||
      !uriEq(snapshot->GetResultPrincipalURI(),
             aLoadState->ResultPrincipalURI())) {
    return Err("ResultPrincipalURI");
  }
  if (!uriEq(snapshot->GetUnstrippedURI(), aLoadState->GetUnstrippedURI())) {
    return Err("UnstrippedURI");
  }
  if (!principalEq(snapshot->GetTriggeringPrincipal(),
                   aLoadState->TriggeringPrincipal())) {
    return Err("TriggeringPrincipal");
  }
  if (!principalEq(snapshot->GetPrincipalToInherit(),
                   aLoadState->PrincipalToInherit())) {
    return Err("PrincipalToInherit");
  }
  if (!principalEq(snapshot->GetPartitionedPrincipalToInherit(),
                   aLoadState->PartitionedPrincipalToInherit())) {
    return Err("PartitionedPrincipalToInherit");
  }

  // Everything matches!
  return loading->mEntry;
}

auto DocumentLoadListener::Open(nsDocShellLoadState* aLoadState,
                                LoadInfo* aLoadInfo, nsLoadFlags aLoadFlags,
                                uint32_t aCacheKey,
                                const Maybe<uint64_t>& aChannelId,
                                const TimeStamp& aAsyncOpenTime,
                                nsDOMNavigationTiming* aTiming,
                                Maybe<ClientInfo>&& aInfo, bool aUrgentStart,
                                dom::ContentParent* aContentParent,
                                nsresult* aRv) -> RefPtr<OpenPromise> {
  auto* loadingContext = GetLoadingBrowsingContext();

  // Snapshot the referrer policy to be used when running the "create internal
  // ancestor origins list".
  aLoadInfo->SetFrameReferrerPolicySnapshot(
      loadingContext->GetEmbedderFrameReferrerPolicy());

  MOZ_DIAGNOSTIC_ASSERT_IF(loadingContext->GetParent(),
                           loadingContext->GetParentWindowContext());

  OriginAttributes attrs;
  loadingContext->GetOriginAttributes(attrs);

  aLoadInfo->SetContinerFeaturePolicy(
      loadingContext->GetContainerFeaturePolicy());

  mLoadIdentifier = aLoadState->GetLoadIdentifier();
  // See description of  mFileName in nsDocShellLoadState.h
  mIsDownload = !aLoadState->FileName().IsVoid();
  mIsLoadingJSURI = aLoadState->URI()->SchemeIs("javascript");
  mHTTPSFirstDowngradeData = aLoadState->GetHttpsFirstDowngradeData().forget();

  // Check for infinite recursive object or iframe loads
  if (!CheckRecursiveLoad(loadingContext, aLoadState, mIsDocumentLoad)) {
    *aRv = NS_ERROR_RECURSIVE_DOCUMENT_LOAD;
    mParentChannelListener = nullptr;
    return nullptr;
  }

  auto* documentContext = GetDocumentBrowsingContext();

  // If we are using SHIP and this load is from session history, validate that
  // the load matches our local copy of the loading history entry.
  //
  // NOTE: Keep this check in-sync with the check in
  // `nsDocShellLoadState::GetEffectiveTriggeringRemoteType()`!
  RefPtr<SessionHistoryEntry> existingEntry;
  if (aLoadState->LoadIsFromSessionHistory() &&
      aLoadState->LoadType() != LOAD_ERROR_PAGE) {
    Result<SessionHistoryEntry*, const char*> result =
        ValidateHistoryLoad(loadingContext, aLoadState);
    if (result.isErr()) {
      const char* mismatch = result.unwrapErr();
      LOG(
          ("DocumentLoadListener::Open with invalid loading history entry "
           "[this=%p, mismatch=%s]",
           this, mismatch));
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
      MOZ_CRASH_UNSAFE_PRINTF(
          "DocumentLoadListener::Open for invalid history entry due to "
          "mismatch of '%s'",
          mismatch);
#endif
      *aRv = NS_ERROR_DOM_SECURITY_ERR;
      mParentChannelListener = nullptr;
      return nullptr;
    }

    existingEntry = result.unwrap();
    if (!existingEntry->IsInSessionHistory() &&
        !documentContext->HasLoadingHistoryEntry(existingEntry)) {
      SessionHistoryEntry::RemoveLoadId(
          aLoadState->GetLoadingSessionHistoryInfo()->mLoadId);
      LOG(
          ("DocumentLoadListener::Open with disconnected history entry "
           "[this=%p]",
           this));

      *aRv = NS_BINDING_ABORTED;
      mParentChannelListener = nullptr;
      mChannel = nullptr;
      return nullptr;
    }
  }

  if (aLoadState->GetRemoteTypeOverride()) {
    if (!mIsDocumentLoad || !NS_IsAboutBlank(aLoadState->URI()) ||
        !loadingContext->IsTopContent()) {
      LOG(
          ("DocumentLoadListener::Open with invalid remoteTypeOverride "
           "[this=%p]",
           this));
      *aRv = NS_ERROR_DOM_SECURITY_ERR;
      mParentChannelListener = nullptr;
      return nullptr;
    }

    mRemoteTypeOverride = aLoadState->GetRemoteTypeOverride();
  }

  if (NS_WARN_IF(!loadingContext->IsOwnedByProcess(
          GetContentProcessId(aContentParent)))) {
    LOG(
        ("DocumentLoadListener::Open called from non-current content process "
         "[this=%p, current=%" PRIu64 ", caller=%" PRIu64 "]",
         this, loadingContext->OwnerProcessId(),
         uint64_t(GetContentProcessId(aContentParent))));
    *aRv = NS_BINDING_ABORTED;
    mParentChannelListener = nullptr;
    return nullptr;
  }

  if (mIsDocumentLoad && loadingContext->IsContent() &&
      NS_WARN_IF(loadingContext->IsReplaced())) {
    LOG(
        ("DocumentLoadListener::Open called from replaced BrowsingContext "
         "[this=%p, browserid=%" PRIx64 ", bcid=%" PRIx64 "]",
         this, loadingContext->BrowserId(), loadingContext->Id()));
    *aRv = NS_BINDING_ABORTED;
    mParentChannelListener = nullptr;
    return nullptr;
  }

  if (!nsDocShell::CreateAndConfigureRealChannelForLoadState(
          loadingContext, aLoadState, aLoadInfo, mParentChannelListener,
          nullptr, attrs, aLoadFlags, aCacheKey, *aRv,
          getter_AddRefs(mChannel))) {
    LOG(("DocumentLoadListener::Open failed to create channel [this=%p]",
         this));
    mParentChannelListener = nullptr;
    return nullptr;
  }

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel)) {
    // We need to set the partitioned principal to the load state so that we can
    // propagate it to the loadingSessionHistoryInfo. To do this, we need to
    // get the finalized cookieJarSettings for the channel because it is used by
    // StoragePrincipalHelper to mint the partitioned principal.
    //
    // ClientChannelHelper below also needs us to have finalized the principal
    // for the channel because it will request that StoragePrincipalHelper mint
    // a principal that needs to match the same principal that a later call to
    // StoragePrincipalHelper will mint when determining the right origin to
    // look up the ServiceWorker.
    //
    // Because nsHttpChannel::AsyncOpen calls UpdateAntiTrackingInfoForChannel
    // which potentially flips the third party bit/flag on the partition key on
    // the cookie jar which impacts the principal that will be minted, it is
    // essential that UpdateAntiTrackingInfoForChannel is called before
    // AddClientChannelHelperInParent below.
    //
    // Because the call to UpdateAntiTrackingInfoForChannel is largely
    // idempotent, we currently just make the call ourselves right now.  The one
    // caveat is that the RFPRandomKey may be spuriously regenerated for
    // top-level documents.
    AntiTrackingUtils::UpdateAntiTrackingInfoForChannel(httpChannel);

    nsCOMPtr<nsIPrincipal> partitionedPrincipal;

    (void)StoragePrincipalHelper::GetPrincipal(
        httpChannel, StoragePrincipalHelper::ePartitionedPrincipal,
        getter_AddRefs(partitionedPrincipal));

    aLoadState->SetPartitionedPrincipalToInherit(partitionedPrincipal);
  }

  if (documentContext && aLoadState->LoadType() != LOAD_ERROR_PAGE) {
    // It's hard to know at this point whether session history will be enabled
    // in the browsing context, so we always create an entry for a load here.
    mLoadingSessionHistoryInfo =
        documentContext->CreateLoadingSessionHistoryEntryForLoad(
            aLoadState, existingEntry, mChannel);
    MOZ_ASSERT(mLoadingSessionHistoryInfo);
  }

  nsCOMPtr<nsIURI> uriBeingLoaded;
  (void)NS_WARN_IF(NS_FAILED(mChannel->GetURI(getter_AddRefs(uriBeingLoaded))));

  RefPtr<HttpBaseChannel> httpBaseChannel = do_QueryObject(mChannel, aRv);
  if (uriBeingLoaded && httpBaseChannel) {
    nsCOMPtr<nsIURI> topWindowURI;
    if (mIsDocumentLoad && loadingContext->IsTop()) {
      // If this is for the top level loading, the top window URI should be the
      // URI which we are loading.
      topWindowURI = uriBeingLoaded;
    } else if (RefPtr<WindowGlobalParent> topWindow = AntiTrackingUtils::
                   GetTopWindowExcludingExtensionAccessibleContentFrames(
                       loadingContext, uriBeingLoaded)) {
      nsCOMPtr<nsIPrincipal> topWindowPrincipal =
          topWindow->DocumentPrincipal();
      if (topWindowPrincipal && !topWindowPrincipal->GetIsNullPrincipal()) {
        auto* basePrin = BasePrincipal::Cast(topWindowPrincipal);
        basePrin->GetURI(getter_AddRefs(topWindowURI));
      }
    }
    httpBaseChannel->SetTopWindowURI(topWindowURI);
  }

  nsCOMPtr<nsIIdentChannel> identChannel = do_QueryInterface(mChannel);
  if (identChannel && aChannelId) {
    (void)identChannel->SetChannelId(*aChannelId);
  }
  mDocumentChannelId = aChannelId;

  RefPtr<nsHttpChannel> httpChannelImpl = do_QueryObject(mChannel);
  if (httpChannelImpl) {
    httpChannelImpl->SetWarningReporter(this);

    if (mIsDocumentLoad && loadingContext->IsTop()) {
      httpChannelImpl->SetEarlyHintObserver(this);
    }
  }

  nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(mChannel);
  if (timedChannel) {
    timedChannel->SetAsyncOpen(aAsyncOpenTime);
  }

  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel)) {
    (void)httpChannel->SetRequestContextID(
        loadingContext->GetRequestContextId());

    nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(httpChannel));
    if (cos && aUrgentStart) {
      cos->AddClassFlags(nsIClassOfService::UrgentStart);
    }
  }

  // Setup a ClientChannelHelper to watch for redirects, and copy
  // across any serviceworker related data between channels as needed.
  AddClientChannelHelperInParent(mChannel, std::move(aInfo));

  if (documentContext && !documentContext->StartDocumentLoad(this)) {
    LOG(("DocumentLoadListener::Open failed StartDocumentLoad [this=%p]",
         this));
    *aRv = NS_BINDING_ABORTED;
    mParentChannelListener = nullptr;
    mChannel = nullptr;
    return nullptr;
  }

  // Recalculate the openFlags, matching the logic in use in Content process.
  MOZ_ASSERT(!aLoadState->GetPendingRedirectedChannel());
  uint32_t openFlags = nsDocShell::ComputeURILoaderFlags(
      loadingContext, aLoadState->LoadType(), mIsDocumentLoad);

  RefPtr<ParentProcessDocumentOpenInfo> openInfo =
      new ParentProcessDocumentOpenInfo(mParentChannelListener, openFlags,
                                        loadingContext, aLoadState->TypeHint(),
                                        mIsDocumentLoad);
  openInfo->Prepare();

#ifdef ANDROID
  RefPtr<MozPromise<bool, bool, false>> promise;
  if (documentContext && aLoadState->LoadType() != LOAD_ERROR_PAGE &&
      !(aLoadState->HasInternalLoadFlags(
          nsDocShell::INTERNAL_LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE)) &&
      !(aLoadState->LoadType() & LOAD_HISTORY) &&
      !nsExternalHelperAppService::ExternalProtocolIsBlockedBySandbox(
          documentContext, aLoadState->HasValidUserGestureActivation())) {
    nsCOMPtr<nsIWidget> widget =
        documentContext->GetParentProcessWidgetContaining();
    RefPtr<nsWindow> window = nsWindow::From(widget);

    if (window) {
      promise = window->OnLoadRequest(
          aLoadState->URI(), nsIBrowserDOMWindow::OPEN_CURRENTWINDOW,
          aLoadState->InternalLoadFlags(), aLoadState->TriggeringPrincipal(),
          aLoadState->HasValidUserGestureActivation(),
          documentContext->IsTopContent());
    }
  }

  if (promise) {
    RefPtr<DocumentLoadListener> self = this;
    promise->Then(
        GetCurrentSerialEventTarget(), __func__,
        [=](const MozPromise<bool, bool, false>::ResolveOrRejectValue& aValue) {
          if (aValue.IsResolve()) {
            bool handled = aValue.ResolveValue();
            if (handled) {
              self->DisconnectListeners(NS_ERROR_ABORT, NS_ERROR_ABORT);
              self->mParentChannelListener = nullptr;
            } else {
              nsresult rv = self->mChannel->AsyncOpen(openInfo);
              if (NS_FAILED(rv)) {
                self->DisconnectListeners(rv, rv);
                self->mParentChannelListener = nullptr;
              }
            }
          }
        });
  } else
#endif /* ANDROID */
  {
    *aRv = mChannel->AsyncOpen(openInfo);
    if (NS_FAILED(*aRv)) {
      LOG(("DocumentLoadListener::Open failed AsyncOpen [this=%p rv=%" PRIx32
           "]",
           this, static_cast<uint32_t>(*aRv)));
      if (documentContext) {
        documentContext->EndDocumentLoad(false);
      }
      mParentChannelListener = nullptr;
      return nullptr;
    }
  }

  // HTTPS-Only Mode fights potential timeouts caused by upgrades. Instantly
  // after opening the document channel we have to kick off countermeasures.
  nsHTTPSOnlyUtils::PotentiallyFireHttpRequestToShortenTimout(this);

  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  loadInfo->SetChannelCreationOriginalURI(aLoadState->URI());

  mContentParent = aContentParent;
  mLoadStateExternalLoadFlags = aLoadState->LoadFlags();
  mLoadStateInternalLoadFlags = aLoadState->InternalLoadFlags();
  mLoadStateLoadType = aLoadState->LoadType();
  mTiming = aTiming;
  mSrcdocData = aLoadState->SrcdocData();
  mBaseURI = aLoadState->BaseURI();
  mOriginalUriString = aLoadState->GetOriginalURIString();
  if (documentContext) {
    mParentWindowContext = documentContext->GetParentWindowContext();
  } else {
    mParentWindowContext =
        WindowGlobalParent::GetByInnerWindowId(aLoadInfo->GetInnerWindowID());
    MOZ_RELEASE_ASSERT(mParentWindowContext->GetBrowsingContext() ==
                           GetLoadingBrowsingContext(),
                       "mismatched parent window context?");
  }

  // For content-initiated loads, this flag is set in nsDocShell::LoadURI.
  // For parent-initiated loads, we have to set it here.
  // Below comment is copied from nsDocShell::LoadURI -
  // If we have a system triggering principal, we can assume that this load was
  // triggered by some UI in the browser chrome, such as the URL bar or
  // bookmark bar. This should count as a user interaction for the current sh
  // entry, so that the user may navigate back to the current entry, from the
  // entry that is going to be added as part of this load.
  if (!mSupportsRedirectToRealChannel && aLoadState->TriggeringPrincipal() &&
      aLoadState->TriggeringPrincipal()->IsSystemPrincipal()) {
    WindowContext* topWc = loadingContext->GetTopWindowContext();
    if (topWc && !topWc->IsDiscarded()) {
      MOZ_ALWAYS_SUCCEEDS(topWc->SetSHEntryHasUserInteraction(true));
    }
  }

  *aRv = NS_OK;
  mOpenPromise = new OpenPromise::Private(__func__);
  // We make the promise use direct task dispatch in order to reduce the number
  // of event loops iterations.
  mOpenPromise->UseDirectTaskDispatch(__func__);
  return mOpenPromise;
}

auto DocumentLoadListener::OpenDocument(
    nsDocShellLoadState* aLoadState, nsLoadFlags aLoadFlags, uint32_t aCacheKey,
    const Maybe<uint64_t>& aChannelId, const TimeStamp& aAsyncOpenTime,
    nsDOMNavigationTiming* aTiming, Maybe<dom::ClientInfo>&& aInfo,
    bool aUriModified, Maybe<bool> aIsEmbeddingBlockedError,
    dom::ContentParent* aContentParent, nsresult* aRv) -> RefPtr<OpenPromise> {
  LOG(("DocumentLoadListener [%p] OpenDocument [uri=%s]", this,
       aLoadState->URI()->GetSpecOrDefault().get()));

  MOZ_ASSERT(mIsDocumentLoad);

  RefPtr<CanonicalBrowsingContext> browsingContext =
      GetDocumentBrowsingContext();

  // As a security check, check that aLoadFlags matches what we expect. We
  // expect them to be the same we compute on the parent, except for the load
  // group flags.
  {
    const nsLoadFlags parentLoadFlags = aLoadState->CalculateChannelLoadFlags(
        browsingContext, aUriModified, std::move(aIsEmbeddingBlockedError));
    const nsLoadFlags differing = parentLoadFlags ^ aLoadFlags;
    if (differing & ~nsLoadGroup::kInheritedLoadFlags) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
      MOZ_CRASH_UNSAFE_PRINTF(
          "DocumentLoadListener::OpenDocument: Unexpected load flags: "
          "%x vs. %x (differing %x vs. %x)",
          parentLoadFlags, aLoadFlags, differing & parentLoadFlags,
          differing & aLoadFlags);
#endif
      *aRv = NS_ERROR_UNEXPECTED;
      return nullptr;
    }
  }

  // If this is a top-level load, then rebuild the LoadInfo from scratch,
  // since the goal is to be able to initiate loads in the parent, where the
  // content process won't have provided us with an existing one.
  RefPtr<LoadInfo> loadInfo =
      CreateDocumentLoadInfo(browsingContext, aLoadState);

  // Keep track of navigation for the Bounce Tracking Protection.
  if (browsingContext->IsTopContent()) {
    RefPtr<BounceTrackingState> bounceTrackingState =
        browsingContext->GetBounceTrackingState();

    // Not every browsing context has a BounceTrackingState. It's also null when
    // the feature is disabled.
    if (bounceTrackingState) {
      nsCOMPtr<nsIPrincipal> triggeringPrincipal;
      nsresult rv =
          loadInfo->GetTriggeringPrincipal(getter_AddRefs(triggeringPrincipal));

      if (!NS_WARN_IF(NS_FAILED(rv))) {
        DebugOnly<nsresult> rv = bounceTrackingState->OnStartNavigation(
            triggeringPrincipal, loadInfo->GetHasValidUserGestureActivation());
        NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                             "BounceTrackingState::OnStartNavigation failed");
      }
    }
  }

  return Open(aLoadState, loadInfo, aLoadFlags, aCacheKey, aChannelId,
              aAsyncOpenTime, aTiming, std::move(aInfo), false, aContentParent,
              aRv);
}

auto DocumentLoadListener::OpenObject(
    nsDocShellLoadState* aLoadState, uint32_t aCacheKey,
    const Maybe<uint64_t>& aChannelId, const TimeStamp& aAsyncOpenTime,
    nsDOMNavigationTiming* aTiming, Maybe<dom::ClientInfo>&& aInfo,
    uint64_t aInnerWindowId, nsLoadFlags aLoadFlags,
    nsContentPolicyType aContentPolicyType, bool aUrgentStart,
    dom::ContentParent* aContentParent,
    ObjectUpgradeHandler* aObjectUpgradeHandler, nsresult* aRv)
    -> RefPtr<OpenPromise> {
  LOG(("DocumentLoadListener [%p] OpenObject [uri=%s]", this,
       aLoadState->URI()->GetSpecOrDefault().get()));

  MOZ_ASSERT(!mIsDocumentLoad);

  auto sandboxFlags = aLoadState->TriggeringSandboxFlags();

  RefPtr<LoadInfo> loadInfo = CreateObjectLoadInfo(
      aLoadState, aInnerWindowId, aContentPolicyType, sandboxFlags);

  mObjectUpgradeHandler = aObjectUpgradeHandler;

  return Open(aLoadState, loadInfo, aLoadFlags, aCacheKey, aChannelId,
              aAsyncOpenTime, aTiming, std::move(aInfo), aUrgentStart,
              aContentParent, aRv);
}

auto DocumentLoadListener::OpenInParent(nsDocShellLoadState* aLoadState,
                                        bool aSupportsRedirectToRealChannel)
    -> RefPtr<OpenPromise> {
  MOZ_ASSERT(mIsDocumentLoad);

  // We currently only support passing nullptr for aLoadInfo for
  // top level browsing contexts.
  auto* browsingContext = GetDocumentBrowsingContext();
  if (!browsingContext->IsTopContent() ||
      !browsingContext->GetContentParent()) {
    LOG(("DocumentLoadListener::OpenInParent failed because of subdoc"));
    return nullptr;
  }

  // Clone because this mutates the load flags in the load state, which
  // breaks nsDocShells expectations of being able to do it.
  RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(*aLoadState);
  loadState->CalculateLoadURIFlags();

  RefPtr<nsDOMNavigationTiming> timing = new nsDOMNavigationTiming(nullptr);
  timing->NotifyNavigationStart(
      browsingContext->IsActive()
          ? nsDOMNavigationTiming::DocShellState::eActive
          : nsDOMNavigationTiming::DocShellState::eInactive);

  const mozilla::dom::LoadingSessionHistoryInfo* loadingInfo =
      loadState->GetLoadingSessionHistoryInfo();

  uint32_t cacheKey = 0;
  auto loadType = aLoadState->LoadType();
  if (loadType == LOAD_HISTORY || loadType == LOAD_RELOAD_NORMAL ||
      loadType == LOAD_RELOAD_CHARSET_CHANGE ||
      loadType == LOAD_RELOAD_CHARSET_CHANGE_BYPASS_CACHE ||
      loadType == LOAD_RELOAD_CHARSET_CHANGE_BYPASS_PROXY_AND_CACHE) {
    if (loadingInfo) {
      cacheKey = loadingInfo->mInfo.GetCacheKey();
    }
  }

  // Loads start in the content process might have exposed a channel id to
  // observers, so we need to preserve the value in the parent. That can't have
  // happened here, so Nothing() is fine.
  Maybe<uint64_t> channelId = Nothing();

  // Initial client info is only relevant for subdocument loads, which we're
  // not supporting yet.
  Maybe<dom::ClientInfo> initialClientInfo;

  mSupportsRedirectToRealChannel = aSupportsRedirectToRealChannel;

  // This is a top-level load, so rebuild the LoadInfo from scratch,
  // since in the parent the
  // content process won't have provided us with an existing one.
  RefPtr<LoadInfo> loadInfo =
      CreateDocumentLoadInfo(browsingContext, aLoadState);

  nsLoadFlags loadFlags = loadState->CalculateChannelLoadFlags(
      browsingContext, loadingInfo && loadingInfo->mInfo.GetURIWasModified(),
      Nothing());

  nsresult rv;
  return Open(loadState, loadInfo, loadFlags, cacheKey, channelId,
              TimeStamp::Now(), timing, std::move(initialClientInfo), false,
              browsingContext->GetContentParent(), &rv);
}

base::ProcessId DocumentLoadListener::OtherPid() const {
  return mContentParent ? mContentParent->OtherPid() : base::ProcessId{0};
}

void DocumentLoadListener::FireStateChange(uint32_t aStateFlags,
                                           nsresult aStatus) {
  nsCOMPtr<nsIChannel> request = GetChannel();

  RefPtr<BrowsingContextWebProgress> webProgress =
      GetLoadingBrowsingContext()->GetWebProgress();

  if (webProgress) {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("DocumentLoadListener::FireStateChange", [=]() {
          webProgress->OnStateChange(webProgress, request, aStateFlags,
                                     aStatus);
        }));
  }
}

static void SetNavigating(CanonicalBrowsingContext* aBrowsingContext,
                          bool aNavigating) {
  nsCOMPtr<nsIBrowser> browser;
  if (RefPtr<Element> currentElement = aBrowsingContext->GetEmbedderElement()) {
    browser = currentElement->AsBrowser();
  }

  if (!browser) {
    return;
  }

  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "DocumentLoadListener::SetNavigating",
      [browser, aNavigating]() { browser->SetIsNavigating(aNavigating); }));
}

/* static */ bool DocumentLoadListener::LoadInParent(
    CanonicalBrowsingContext* aBrowsingContext, nsDocShellLoadState* aLoadState,
    bool aSetNavigating) {
  SetNavigating(aBrowsingContext, aSetNavigating);

  RefPtr<DocumentLoadListener> load =
      new DocumentLoadListener(aBrowsingContext, true);
  RefPtr<DocumentLoadListener::OpenPromise> promise = load->OpenInParent(
      aLoadState, /* aSupportsRedirectToRealChannel */ false);
  if (!promise) {
    SetNavigating(aBrowsingContext, false);
    return false;
  }

  // We passed false for aSupportsRedirectToRealChannel, so we should always
  // take the process switching path, and reject this promise.
  promise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [load](DocumentLoadListener::OpenPromise::ResolveOrRejectValue&& aValue) {
        MOZ_ASSERT(aValue.IsReject());
        DocumentLoadListener::OpenPromiseFailedType& rejectValue =
            aValue.RejectValue();
        if (!rejectValue.mContinueNavigating) {
          // If we're not switching the load to a new process, then it is
          // finished (and failed), and we should fire a state change to notify
          // observers. Normally the docshell would fire this, and it would get
          // filtered out by BrowserParent if needed.
          load->FireStateChange(nsIWebProgressListener::STATE_STOP |
                                    nsIWebProgressListener::STATE_IS_WINDOW |
                                    nsIWebProgressListener::STATE_IS_NETWORK,
                                rejectValue.mStatus);
        }
      });

  load->FireStateChange(nsIWebProgressListener::STATE_START |
                            nsIWebProgressListener::STATE_IS_DOCUMENT |
                            nsIWebProgressListener::STATE_IS_REQUEST |
                            nsIWebProgressListener::STATE_IS_WINDOW |
                            nsIWebProgressListener::STATE_IS_NETWORK,
                        NS_OK);
  SetNavigating(aBrowsingContext, false);
  return true;
}

/* static */
bool DocumentLoadListener::SpeculativeLoadInParent(
    dom::CanonicalBrowsingContext* aBrowsingContext,
    nsDocShellLoadState* aLoadState) {
  LOG(("DocumentLoadListener::OpenFromParent"));

  RefPtr<DocumentLoadListener> listener =
      new DocumentLoadListener(aBrowsingContext, true);

  auto promise = listener->OpenInParent(aLoadState, true);
  if (promise) {
    // Create an entry in the redirect channel registrar to
    // allocate an identifier for this load.
    nsCOMPtr<nsIRedirectChannelRegistrar> registrar =
        RedirectChannelRegistrar::GetOrCreate();
    if (!registrar) {
      // Shutdown is in progress.
      return false;
    }
    uint64_t loadIdentifier = aLoadState->GetLoadIdentifier();
    DebugOnly<nsresult> rv =
        registrar->RegisterChannel(nullptr, loadIdentifier);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    // Register listener (as an nsIParentChannel) under our new identifier.
    rv = registrar->LinkChannels(loadIdentifier, listener, nullptr);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
  return !!promise;
}

void DocumentLoadListener::CleanupParentLoadAttempt(uint64_t aLoadIdent) {
  nsCOMPtr<nsIRedirectChannelRegistrar> registrar =
      RedirectChannelRegistrar::GetOrCreate();
  if (!registrar) {
    // Shutdown is in progress.
    return;
  }

  nsCOMPtr<nsIParentChannel> parentChannel;
  registrar->GetParentChannel(aLoadIdent, getter_AddRefs(parentChannel));
  RefPtr<DocumentLoadListener> loadListener = do_QueryObject(parentChannel);

  if (loadListener) {
    // If the load listener is still registered, then we must have failed
    // to connect DocumentChannel into it. Better cancel it!
    loadListener->NotifyDocumentChannelFailed();
  }

  registrar->DeregisterChannels(aLoadIdent);
}

auto DocumentLoadListener::ClaimParentLoad(DocumentLoadListener** aListener,
                                           uint64_t aLoadIdent,
                                           Maybe<uint64_t> aChannelId)
    -> RefPtr<OpenPromise> {
  nsCOMPtr<nsIRedirectChannelRegistrar> registrar =
      RedirectChannelRegistrar::GetOrCreate();
  if (!registrar) {
    // Shutdown is in progress.
    *aListener = nullptr;
    return nullptr;
  }

  nsCOMPtr<nsIParentChannel> parentChannel;
  registrar->GetParentChannel(aLoadIdent, getter_AddRefs(parentChannel));
  RefPtr<DocumentLoadListener> loadListener = do_QueryObject(parentChannel);
  registrar->DeregisterChannels(aLoadIdent);

  if (!loadListener) {
    // The parent went away unexpectedly.
    *aListener = nullptr;
    return nullptr;
  }

  loadListener->mDocumentChannelId = aChannelId;

  MOZ_DIAGNOSTIC_ASSERT(loadListener->mOpenPromise);
  loadListener.forget(aListener);

  return (*aListener)->mOpenPromise;
}

void DocumentLoadListener::NotifyDocumentChannelFailed() {
  LOG(("DocumentLoadListener NotifyDocumentChannelFailed [this=%p]", this));
  // There's been no calls to ClaimParentLoad, and so no listeners have been
  // attached to mOpenPromise yet. As such we can run Then() on it.
  mOpenPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [](DocumentLoadListener::OpenPromiseSucceededType&& aResolveValue) {
        aResolveValue.mPromise->Resolve(NS_BINDING_ABORTED, __func__);
      },
      []() {});

  Cancel(NS_BINDING_ABORTED,
         "DocumentLoadListener::NotifyDocumentChannelFailed"_ns);
}

void DocumentLoadListener::Disconnect(bool aContinueNavigating) {
  LOG(("DocumentLoadListener Disconnect [this=%p, aContinueNavigating=%d]",
       this, aContinueNavigating));
  // The nsHttpChannel may have a reference to this parent, release it
  // to avoid circular references.
  RefPtr<nsHttpChannel> httpChannelImpl = do_QueryObject(mChannel);
  if (httpChannelImpl) {
    httpChannelImpl->SetWarningReporter(nullptr);
    httpChannelImpl->SetEarlyHintObserver(nullptr);
  }

  // Don't cancel ongoing early hints when continuing to load the web page.
  // Early hints are loaded earlier in the code and shouldn't get cancelled
  // here. See also: Bug 1765652
  if (!aContinueNavigating) {
    mEarlyHintsService.Cancel("DocumentLoadListener::Disconnect"_ns);
  }

  if (auto* ctx = GetDocumentBrowsingContext()) {
    ctx->EndDocumentLoad(aContinueNavigating);
  }
}

void DocumentLoadListener::Cancel(const nsresult& aStatusCode,
                                  const nsACString& aReason) {
  LOG(
      ("DocumentLoadListener Cancel [this=%p, "
       "aStatusCode=%" PRIx32 " ]",
       this, static_cast<uint32_t>(aStatusCode)));
  if (mOpenPromiseResolved) {
    return;
  }
  if (mChannel) {
    mChannel->CancelWithReason(aStatusCode, aReason);
  }

  DisconnectListeners(aStatusCode, aStatusCode);
}

void DocumentLoadListener::DisconnectListeners(nsresult aStatus,
                                               nsresult aLoadGroupStatus,
                                               bool aContinueNavigating) {
  LOG(
      ("DocumentLoadListener DisconnectListener [this=%p, "
       "aStatus=%" PRIx32 ", aLoadGroupStatus=%" PRIx32
       ", aContinueNavigating=%d]",
       this, static_cast<uint32_t>(aStatus),
       static_cast<uint32_t>(aLoadGroupStatus), aContinueNavigating));

  RejectOpenPromise(aStatus, aLoadGroupStatus, aContinueNavigating, __func__);

  Disconnect(aContinueNavigating);

  // Clear any pending stream filter requests. If we're going to be sending a
  // response to the content process due to a navigation, our caller will have
  // already stashed the array to be passed to `TriggerRedirectToRealChannel`,
  // so it's safe for us to clear here.
  // TODO: If we retargeted the stream to a non-default handler (e.g. to trigger
  // a download), we currently never attach a stream filter. Should we attach a
  // stream filter in those situations as well?
  mStreamFilterRequests.Clear();
}

void DocumentLoadListener::RedirectToRealChannelFinished(nsresult aRv) {
  LOG(
      ("DocumentLoadListener RedirectToRealChannelFinished [this=%p, "
       "aRv=%" PRIx32 " ]",
       this, static_cast<uint32_t>(aRv)));
  if (NS_FAILED(aRv)) {
    FinishReplacementChannelSetup(aRv);
    return;
  }

  // Wait for background channel ready on target channel
  nsCOMPtr<nsIRedirectChannelRegistrar> redirectReg =
      RedirectChannelRegistrar::GetOrCreate();
  if (!redirectReg) {
    // Shutdown is in progress.
    FinishReplacementChannelSetup(NS_ERROR_ABORT);
    return;
  }

  nsCOMPtr<nsIParentChannel> redirectParentChannel;
  redirectReg->GetParentChannel(mRedirectChannelId,
                                getter_AddRefs(redirectParentChannel));
  if (!redirectParentChannel) {
    FinishReplacementChannelSetup(NS_ERROR_FAILURE);
    return;
  }

  nsCOMPtr<nsIParentRedirectingChannel> redirectingParent =
      do_QueryInterface(redirectParentChannel);
  if (!redirectingParent) {
    // Continue verification procedure if redirecting to non-Http protocol
    FinishReplacementChannelSetup(NS_OK);
    return;
  }

  // Ask redirected channel if verification can proceed.
  // ReadyToVerify will be invoked when redirected channel is ready.
  redirectingParent->ContinueVerification(this);
}

NS_IMETHODIMP
DocumentLoadListener::ReadyToVerify(nsresult aResultCode) {
  FinishReplacementChannelSetup(aResultCode);
  return NS_OK;
}

void DocumentLoadListener::FinishReplacementChannelSetup(nsresult aResult) {
  LOG(
      ("DocumentLoadListener FinishReplacementChannelSetup [this=%p, "
       "aResult=%x]",
       this, int(aResult)));

  auto endDocumentLoad = MakeScopeExit([&]() {
    if (auto* ctx = GetDocumentBrowsingContext()) {
      ctx->EndDocumentLoad(false);
    }
  });
  mStreamFilterRequests.Clear();

  nsCOMPtr<nsIRedirectChannelRegistrar> registrar =
      RedirectChannelRegistrar::GetOrCreate();
  if (!registrar) {
    // Shutdown is in progress.
    return;
  }

  nsCOMPtr<nsIParentChannel> redirectChannel;
  nsresult rv = registrar->GetParentChannel(mRedirectChannelId,
                                            getter_AddRefs(redirectChannel));
  if (NS_FAILED(rv) || !redirectChannel) {
    aResult = NS_ERROR_FAILURE;
  }

  // Release all previously registered channels, they are no longer needed to
  // be kept in the registrar from this moment.
  registrar->DeregisterChannels(mRedirectChannelId);
  mRedirectChannelId = 0;
  if (NS_FAILED(aResult)) {
    if (redirectChannel) {
      redirectChannel->Delete();
    }
    mChannel->Cancel(aResult);
    mChannel->Resume();
    return;
  }

  MOZ_ASSERT(
      !SameCOMIdentity(redirectChannel, static_cast<nsIParentChannel*>(this)));

  redirectChannel->SetParentListener(mParentChannelListener);

  ApplyPendingFunctions(redirectChannel);

  if (!ResumeSuspendedChannel(redirectChannel)) {
    nsCOMPtr<nsILoadGroup> loadGroup;
    mChannel->GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup) {
      // We added ourselves to the load group, but attempting
      // to resume has notified us that the channel is already
      // finished. Better remove ourselves from the loadgroup
      // again. The only time the channel will be in a loadgroup
      // is if we're connected to the parent process.
      nsresult status = NS_OK;
      mChannel->GetStatus(&status);
      loadGroup->RemoveRequest(mChannel, nullptr, status);
    }
  }
}

void DocumentLoadListener::ApplyPendingFunctions(
    nsIParentChannel* aChannel) const {
  // We stored the values from all nsIParentChannel functions called since we
  // couldn't handle them. Copy them across to the real channel since it
  // should know what to do.

  nsCOMPtr<nsIParentChannel> parentChannel = aChannel;
  for (const auto& variant : mIParentChannelFunctions) {
    variant.match(
        [parentChannel](const ClassifierMatchedInfoParams& aParams) {
          parentChannel->SetClassifierMatchedInfo(
              aParams.mList, aParams.mProvider, aParams.mFullHash);
        },
        [parentChannel](const ClassifierMatchedTrackingInfoParams& aParams) {
          parentChannel->SetClassifierMatchedTrackingInfo(aParams.mLists,
                                                          aParams.mFullHashes);
        },
        [parentChannel](const ClassificationFlagsParams& aParams) {
          parentChannel->NotifyClassificationFlags(aParams.mClassificationFlags,
                                                   aParams.mIsThirdParty);
        });
  }

  RefPtr<HttpChannelSecurityWarningReporter> reporter;
  if (RefPtr<HttpChannelParent> httpParent = do_QueryObject(aChannel)) {
    reporter = httpParent;
  } else if (RefPtr<nsHttpChannel> httpChannel = do_QueryObject(aChannel)) {
    reporter = httpChannel->GetWarningReporter();
  }
  if (reporter) {
    for (const auto& variant : mSecurityWarningFunctions) {
      variant.match(
          [reporter](const ReportSecurityMessageParams& aParams) {
            (void)reporter->ReportSecurityMessage(aParams.mMessageTag,
                                                  aParams.mMessageCategory);
          },
          [reporter](const LogBlockedCORSRequestParams& aParams) {
            (void)reporter->LogBlockedCORSRequest(
                aParams.mMessage, aParams.mCategory, aParams.mIsWarning);
          },
          [reporter](const LogMimeTypeMismatchParams& aParams) {
            (void)reporter->LogMimeTypeMismatch(aParams.mMessageName,
                                                aParams.mWarning, aParams.mURL,
                                                aParams.mContentType);
          });
    }
  }
}

bool DocumentLoadListener::ResumeSuspendedChannel(
    nsIStreamListener* aListener) {
  LOG(("DocumentLoadListener ResumeSuspendedChannel [this=%p]", this));
  RefPtr<nsHttpChannel> httpChannel = do_QueryObject(mChannel);
  if (httpChannel) {
    httpChannel->SetApplyConversion(mOldApplyConversion);
  }

  if (!mIsFinished) {
    mParentChannelListener->SetListenerAfterRedirect(aListener);
  }

  // If we failed to suspend the channel, then we might have received
  // some messages while the redirected was being handled.
  // Manually send them on now.
  nsTArray<StreamListenerFunction> streamListenerFunctions =
      std::move(mStreamListenerFunctions);
  if (!aListener) {
    streamListenerFunctions.Clear();
  }

  ForwardStreamListenerFunctions(std::move(streamListenerFunctions), aListener);

  // We don't expect to get new stream listener functions added
  // via re-entrancy. If this ever happens, we should understand
  // exactly why before allowing it.
  NS_ASSERTION(mStreamListenerFunctions.IsEmpty(),
               "Should not have added new stream listener function!");

  mChannel->Resume();

  // Our caller will invoke `EndDocumentLoad` for us.

  return !mIsFinished;
}

void DocumentLoadListener::CancelEarlyHintPreloads() {
  mEarlyHintsService.Cancel("DocumentLoadListener::CancelEarlyHintPreloads"_ns);
}

void DocumentLoadListener::RegisterEarlyHintLinksAndGetConnectArgs(
    dom::ContentParentId aCpId, nsTArray<EarlyHintConnectArgs>& aOutLinks) {
  mEarlyHintsService.RegisterLinksAndGetConnectArgs(aCpId, aOutLinks);
}

void DocumentLoadListener::SerializeRedirectData(
    RedirectToRealChannelArgs& aArgs, bool aIsCrossProcess,
    uint32_t aRedirectFlags, uint32_t aLoadFlags,
    nsTArray<EarlyHintConnectArgs>&& aEarlyHints,
    uint32_t aEarlyHintLinkType) const {
  aArgs.uri() = GetChannelCreationURI();
  aArgs.loadIdentifier() = mLoadIdentifier;
  aArgs.earlyHints() = std::move(aEarlyHints);
  aArgs.earlyHintLinkType() = aEarlyHintLinkType;

  // I previously used HttpBaseChannel::CloneLoadInfoForRedirect, but that
  // clears the principal to inherit, which fails tests (probably because this
  // 'redirect' is usually just an implementation detail). It's also http
  // only, and mChannel can be anything that we redirected to.
  nsCOMPtr<nsILoadInfo> channelLoadInfo = mChannel->LoadInfo();
  nsCOMPtr<nsIPrincipal> principalToInherit;
  channelLoadInfo->GetPrincipalToInherit(getter_AddRefs(principalToInherit));

  const RefPtr<nsHttpChannel> baseChannel = do_QueryObject(mChannel);

  nsCOMPtr<nsILoadContext> loadContext;
  NS_QueryNotificationCallbacks(mChannel, loadContext);
  nsCOMPtr<nsILoadInfo> redirectLoadInfo;

  // Only use CloneLoadInfoForRedirect if we have a load context,
  // since it internally tries to pull OriginAttributes from the
  // the load context and asserts if they don't match the load info.
  // We can end up without a load context if the channel has been aborted
  // and the callbacks have been cleared.
  if (baseChannel && loadContext) {
    redirectLoadInfo = baseChannel->CloneLoadInfoForRedirect(
        aArgs.uri(), nsIChannelEventSink::REDIRECT_INTERNAL);
    redirectLoadInfo->SetResultPrincipalURI(aArgs.uri());

    // The clone process clears this, and then we fail tests..
    // docshell/test/mochitest/test_forceinheritprincipal_overrule_owner.html
    if (principalToInherit) {
      redirectLoadInfo->SetPrincipalToInherit(principalToInherit);
    }
  } else {
    redirectLoadInfo =
        static_cast<mozilla::net::LoadInfo*>(channelLoadInfo.get())->Clone();

    redirectLoadInfo->AppendRedirectHistoryEntry(mChannel, true);
  }

  const Maybe<ClientInfo>& reservedClientInfo =
      channelLoadInfo->GetReservedClientInfo();
  if (reservedClientInfo) {
    redirectLoadInfo->SetReservedClientInfo(*reservedClientInfo);
  }

  aArgs.registrarId() = mRedirectChannelId;

#ifdef DEBUG
  // We only set the granularFingerprintingProtection field when opening http
  // channels. So, we mark the field as set here if the channel is not a
  // nsHTTPChannel to pass the assertion check for getting this field in below
  // LoadInfoToLoadInfoArgs() call.
  if (!baseChannel) {
    static_cast<mozilla::net::LoadInfo*>(redirectLoadInfo.get())
        ->MarkOverriddenFingerprintingSettingsAsSet();
  }
#endif

  MOZ_ALWAYS_SUCCEEDS(
      ipc::LoadInfoToLoadInfoArgs(redirectLoadInfo, &aArgs.loadInfo()));

  if (StaticPrefs::dom_location_ancestorOrigins_enabled()) {
    MOZ_ASSERT(XRE_IsParentProcess());
    if (RefPtr bc = redirectLoadInfo->GetFrameBrowsingContext()) {
      nsCOMPtr<nsIPrincipal> resultPrincipal;
      // If this fails, we get an empty location.ancestorOrigins list
      if (NS_SUCCEEDED(
              nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
                  mChannel, getter_AddRefs(resultPrincipal)))) {
        const auto referrerPolicy =
            static_cast<LoadInfo*>(channelLoadInfo.get())
                ->GetFrameReferrerPolicySnapshot();
        bc->Canonical()->CreateRedactedAncestorOriginsList(resultPrincipal,
                                                           referrerPolicy);
      }

      // convert principals to IPC data
      constexpr auto prepareInfo =
          [](nsIPrincipal* aPrincipal) -> Maybe<ipc::PrincipalInfo> {
        if (aPrincipal == nullptr) {
          return Nothing();
        }
        ipc::PrincipalInfo data;
        return NS_SUCCEEDED(PrincipalToPrincipalInfo(aPrincipal, &data))
                   ? Some(std::move(data))
                   : Nothing();
      };

      // The ancestorOrigins list the document should ultimately have, that we
      // send down with load args.
      auto& ancestorOrigins = aArgs.loadInfo().ancestorOrigins();
      for (const auto& ancestorPrincipal :
           bc->Canonical()->GetPossiblyRedactedAncestorOriginsList()) {
        ancestorOrigins.AppendElement(prepareInfo(ancestorPrincipal));
      }
    }
  }

  mChannel->GetOriginalURI(getter_AddRefs(aArgs.originalURI()));

  // mChannel can be a nsHttpChannel as well as InterceptedHttpChannel so we
  // can't use baseChannel here.
  if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel)) {
    MOZ_ALWAYS_SUCCEEDS(httpChannel->GetChannelId(&aArgs.channelId()));

    // propagated the channel's referrerInfo back to child if the redirection
    // is caused by ServiceWorker interception.
    if (nsCOMPtr<nsIInterceptedChannel> interceptedChannel =
            do_QueryInterface(mChannel)) {
      nsCOMPtr<nsIReferrerInfo> referrerInfo;
      MOZ_ALWAYS_SUCCEEDS(
          httpChannel->GetReferrerInfo(getter_AddRefs(referrerInfo)));
      if (referrerInfo) {
        aArgs.referrerInfo() = referrerInfo;
      }
    }
  }

  aArgs.redirectMode() = nsIHttpChannelInternal::REDIRECT_MODE_FOLLOW;
  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal =
      do_QueryInterface(mChannel);
  if (httpChannelInternal) {
    MOZ_ALWAYS_SUCCEEDS(
        httpChannelInternal->GetRedirectMode(&aArgs.redirectMode()));
  }

  if (baseChannel) {
    aArgs.init() =
        Some(baseChannel
                 ->CloneReplacementChannelConfig(
                     true, aRedirectFlags,
                     HttpBaseChannel::ReplacementReason::DocumentChannel)
                 .Serialize());
  }

  uint32_t contentDispositionTemp;
  nsresult rv = mChannel->GetContentDisposition(&contentDispositionTemp);
  if (NS_SUCCEEDED(rv)) {
    aArgs.contentDisposition() = Some(contentDispositionTemp);
  }

  nsString contentDispositionFilenameTemp;
  rv = mChannel->GetContentDispositionFilename(contentDispositionFilenameTemp);
  if (NS_SUCCEEDED(rv)) {
    aArgs.contentDispositionFilename() = Some(contentDispositionFilenameTemp);
  }

  SetNeedToAddURIVisit(mChannel, false);

  aArgs.newLoadFlags() = aLoadFlags;
  aArgs.redirectFlags() = aRedirectFlags;
  aArgs.properties() = do_QueryObject(mChannel);
  aArgs.srcdocData() = mSrcdocData;
  aArgs.baseUri() = mBaseURI;
  aArgs.loadStateExternalLoadFlags() = mLoadStateExternalLoadFlags;
  aArgs.loadStateInternalLoadFlags() = mLoadStateInternalLoadFlags;
  aArgs.loadStateLoadType() = mLoadStateLoadType;
  aArgs.originalUriString() = mOriginalUriString;
  if (mLoadingSessionHistoryInfo) {
    aArgs.loadingSessionHistoryInfo().emplace(*mLoadingSessionHistoryInfo);
  }
}

static bool IsFirstLoadInWindow(nsIChannel* aChannel) {
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  return loadInfo->GetIsNewWindowTarget();
}

// Get where the document loaded by this nsIChannel should be rendered. This
// will be `OPEN_CURRENTWINDOW` unless we're loading an attachment which would
// normally open in an external program, but we're instead choosing to render
// internally.
static int32_t GetWhereToOpen(nsIChannel* aChannel, bool aIsDocumentLoad) {
  // Ignore content disposition for loads from an object or embed element.
  if (!aIsDocumentLoad) {
    return nsIBrowserDOMWindow::OPEN_CURRENTWINDOW;
  }

  // Always continue in the same window if we're not loading an attachment.
  uint32_t disposition = nsIChannel::DISPOSITION_INLINE;
  if (NS_FAILED(aChannel->GetContentDisposition(&disposition)) ||
      disposition != nsIChannel::DISPOSITION_ATTACHMENT) {
    return nsIBrowserDOMWindow::OPEN_CURRENTWINDOW;
  }

  // If the channel is for a new window target, continue in the same window.
  if (IsFirstLoadInWindow(aChannel)) {
    return nsIBrowserDOMWindow::OPEN_CURRENTWINDOW;
  }

  // Respect the user's preferences with browser.link.open_newwindow
  // FIXME: There should probably be a helper for this, as the logic is
  // duplicated in a few places.
  int32_t where = Preferences::GetInt("browser.link.open_newwindow",
                                      nsIBrowserDOMWindow::OPEN_NEWTAB);
  if (where == nsIBrowserDOMWindow::OPEN_CURRENTWINDOW ||
      where == nsIBrowserDOMWindow::OPEN_NEWWINDOW ||
      where == nsIBrowserDOMWindow::OPEN_NEWTAB) {
    return where;
  }
  // NOTE: nsIBrowserDOMWindow::OPEN_NEWTAB_BACKGROUND and
  //       nsIBrowserDOMWindow::OPEN_NEWTAB_FOREGROUND are not allowed as pref
  //       values.
  return nsIBrowserDOMWindow::OPEN_NEWTAB;
}

static bool ContextCanProcessSwitch(CanonicalBrowsingContext* aBrowsingContext,
                                    WindowGlobalParent* aParentWindow,
                                    bool aSwitchToNewTab) {
  if (NS_WARN_IF(!aBrowsingContext)) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Process Switch Abort: no browsing context"));
    return false;
  }
  if (!aBrowsingContext->IsContent()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Process Switch Abort: non-content browsing context"));
    return false;
  }

  // If we're switching into a new tab, we can skip the remaining checks, as
  // we're not actually changing the process of aBrowsingContext, so whether or
  // not it is allowed to process switch isn't relevant.
  if (aSwitchToNewTab) {
    return true;
  }

  if (aParentWindow) {
    // If remote subframes are disabled, subframes never process switch.
    if (!aBrowsingContext->UseRemoteSubframes()) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
              ("Process Switch Abort: remote subframes disabled"));
      return false;
    }

    // Otherwise, subframes can always process-switch unless they are directly
    // embedded within a parent-process document.
    if (aParentWindow->IsInProcess()) {
      MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
              ("Process Switch Abort: Subframe with in-process parent"));
      return false;
    }
    return true;
  }

  // Check if the "maychangeremoteness" attribute is present on the embedding
  // element. Assume the context can process switch if the embedder element is
  // unknown, as it's safer to fail to switch in that scenario.
  Element* browserElement = aBrowsingContext->Top()->GetEmbedderElement();
  if (browserElement &&
      !browserElement->HasAttribute(u"maychangeremoteness"_ns)) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Process Switch Abort: toplevel switch disabled by <browser>"));
    return false;
  }

  // In some tests, we use `createWindowlessBrowser(false)` to create a
  // windowless content browser. As process switching relies on the `<browser>`
  // element to perform a remote abstraction, we cannot perform process
  // switching on the root element of this browser.
  if (!browserElement && aBrowsingContext->Windowless()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Process Switch Abort: switch disabled by windowless browser"));
    return false;
  }

  return true;
}

static RefPtr<dom::BrowsingContextCallbackReceivedPromise> SwitchToNewTab(
    CanonicalBrowsingContext* aLoadingBrowsingContext, int32_t aWhere) {
  MOZ_ASSERT(aWhere == nsIBrowserDOMWindow::OPEN_NEWTAB ||
                 aWhere == nsIBrowserDOMWindow::OPEN_NEWTAB_BACKGROUND ||
                 aWhere == nsIBrowserDOMWindow::OPEN_NEWTAB_FOREGROUND ||
                 aWhere == nsIBrowserDOMWindow::OPEN_NEWWINDOW,
             "Unsupported open location");

  auto promise =
      MakeRefPtr<dom::BrowsingContextCallbackReceivedPromise::Private>(
          __func__);

  // Get the nsIBrowserDOMWindow for the given BrowsingContext's tab.
  nsCOMPtr<nsIBrowserDOMWindow> browserDOMWindow =
      aLoadingBrowsingContext->GetBrowserDOMWindow();
  if (NS_WARN_IF(!browserDOMWindow)) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Process Switch Abort: Unable to get nsIBrowserDOMWindow"));
    promise->Reject(NS_ERROR_FAILURE, __func__);
    return promise;
  }

  // Open a new content tab by calling into frontend. We don't need to worry
  // about the triggering principal or CSP, as createContentWindow doesn't
  // actually start loading anything, but use a null principal anyway in case
  // something changes.
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      NullPrincipal::Create(aLoadingBrowsingContext->OriginAttributesRef());

  RefPtr<nsOpenWindowInfo> openInfo = new nsOpenWindowInfo();
  openInfo->mBrowsingContextReadyCallback =
      new nsBrowsingContextReadyCallback(promise);
  openInfo->mParent = aLoadingBrowsingContext;
  openInfo->mForceNoOpener = true;
  openInfo->mIsRemote = true;
  openInfo->mPrincipalToInheritForAboutBlank = triggeringPrincipal;

  // Do the actual work to open a new tab or window async.
  nsresult rv = NS_DispatchToMainThread(NS_NewRunnableFunction(
      "DocumentLoadListener::SwitchToNewTab",
      [browserDOMWindow, openInfo, aWhere, triggeringPrincipal, promise] {
        RefPtr<BrowsingContext> bc;
        nsresult rv = browserDOMWindow->CreateContentWindow(
            /* uri */ nullptr, openInfo, aWhere,
            nsIBrowserDOMWindow::OPEN_NO_REFERRER, triggeringPrincipal,
            /* policyContainer */ nullptr, getter_AddRefs(bc));
        if (NS_WARN_IF(NS_FAILED(rv))) {
          MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
                  ("Process Switch Abort: CreateContentWindow threw"));
          promise->Reject(rv, __func__);
        }
        if (bc) {
          promise->Resolve(bc, __func__);
        }
      }));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->Reject(NS_ERROR_UNEXPECTED, __func__);
  }
  return promise;
}

bool DocumentLoadListener::MaybeTriggerProcessSwitch(
    bool* aWillSwitchToRemote) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_DIAGNOSTIC_ASSERT(mChannel);
  MOZ_DIAGNOSTIC_ASSERT(mParentChannelListener);
  MOZ_DIAGNOSTIC_ASSERT(aWillSwitchToRemote);

  MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
          ("DocumentLoadListener MaybeTriggerProcessSwitch [this=%p, uri=%s, "
           "browserid=%" PRIx64 "]",
           this, GetChannelCreationURI()->GetSpecOrDefault().get(),
           GetLoadingBrowsingContext()->Top()->BrowserId()));

  // Check if we should handle this load in a different tab or window.
  int32_t where = GetWhereToOpen(mChannel, mIsDocumentLoad);
  bool switchToNewTab = where != nsIBrowserDOMWindow::OPEN_CURRENTWINDOW;

  // Get the loading BrowsingContext. This may not be the context which will be
  // switching processes when switching to a new tab, and in the case of an
  // <object> or <embed> element, as we don't create the final context until
  // after process selection.
  //
  // - /!\ WARNING /!\ -
  // Don't use `browsingContext->IsTop()` in this method! It will behave
  // incorrectly for non-document loads such as `<object>` or `<embed>`.
  // Instead, check whether or not `parentWindow` is null.
  RefPtr<CanonicalBrowsingContext> browsingContext =
      GetLoadingBrowsingContext();
  // If switching to a new tab, the final BC isn't a frame.
  RefPtr<WindowGlobalParent> parentWindow =
      switchToNewTab ? nullptr : GetParentWindowContext();
  if (!ContextCanProcessSwitch(browsingContext, parentWindow, switchToNewTab)) {
    return false;
  }

  if (!browsingContext->IsOwnedByProcess(GetContentProcessId(mContentParent))) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Error,
            ("Process Switch Abort: context no longer owned by creator"));
    Cancel(NS_BINDING_ABORTED,
           "Process Switch Abort: context no longer owned by creator"_ns);
    return false;
  }

  if (browsingContext->IsReplaced()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Process Switch Abort: replaced browsing context"));
    Cancel(NS_BINDING_ABORTED,
           "Process Switch Abort: replaced browsing context"_ns);
    return false;
  }

  nsAutoCString currentRemoteType(NOT_REMOTE_TYPE);
  if (mContentParent) {
    currentRemoteType = mContentParent->GetRemoteType();
  }

  auto optionsResult = IsolationOptionsForNavigation(
      browsingContext->Top(), switchToNewTab ? nullptr : parentWindow.get(),
      GetChannelCreationURI(), mChannel, currentRemoteType,
      HasCrossOriginOpenerPolicyMismatch(), switchToNewTab, mLoadStateLoadType,
      mDocumentChannelId, mRemoteTypeOverride);
  if (optionsResult.isErr()) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Error,
            ("Process Switch Abort: CheckIsolationForNavigation Failed with %s",
             GetStaticErrorName(optionsResult.inspectErr())));
    Cancel(optionsResult.unwrapErr(),
           "Process Switch Abort: CheckIsolationForNavigation Failed"_ns);
    return false;
  }

  NavigationIsolationOptions options = optionsResult.unwrap();

  if (options.mTryUseBFCache) {
    MOZ_ASSERT(!parentWindow, "Can only BFCache toplevel windows");
    MOZ_ASSERT(!switchToNewTab, "Can't BFCache for a tab switch");
    bool sameOrigin = false;
    if (auto* wgp = browsingContext->GetCurrentWindowGlobal()) {
      nsCOMPtr<nsIPrincipal> resultPrincipal;
      MOZ_ALWAYS_SUCCEEDS(
          nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
              mChannel, getter_AddRefs(resultPrincipal)));
      sameOrigin =
          wgp->DocumentPrincipal()->EqualsConsideringDomain(resultPrincipal);
    }

    // We only reset the window name for content.
    mLoadingSessionHistoryInfo->mForceMaybeResetName.emplace(
        StaticPrefs::privacy_window_name_update_enabled() &&
        browsingContext->IsContent() && !sameOrigin);
  }

  MOZ_LOG(
      gProcessIsolationLog, LogLevel::Verbose,
      ("CheckIsolationForNavigation -> current:(%s) remoteType:(%s) replace:%d "
       "group:%" PRIx64 " bfcache:%d shentry:%p newTab:%d",
       currentRemoteType.get(), options.mRemoteType.get(),
       options.mReplaceBrowsingContext, options.mSpecificGroupId,
       options.mTryUseBFCache, options.mActiveSessionHistoryEntry.get(),
       switchToNewTab));

  // Check if a process switch is needed.
  if (currentRemoteType == options.mRemoteType &&
      !options.mReplaceBrowsingContext && !switchToNewTab) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Info,
            ("Process Switch Abort: type (%s) is compatible",
             options.mRemoteType.get()));
    return false;
  }

  if (NS_WARN_IF(parentWindow && options.mRemoteType.IsEmpty())) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Error,
            ("Process Switch Abort: non-remote target process for subframe"));
    return false;
  }

  *aWillSwitchToRemote = !options.mRemoteType.IsEmpty();

  // If we've decided to re-target this load into a new tab or window (see
  // `GetWhereToOpen`), do so before performing a process switch. This will
  // require creating the new <browser> to load in, which may be performed
  // async.
  if (switchToNewTab) {
    SwitchToNewTab(browsingContext, where)
        ->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [self = RefPtr{this},
             options](const RefPtr<BrowsingContext>& aBrowsingContext)
                MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA mutable {
                  if (aBrowsingContext->IsDiscarded()) {
                    MOZ_LOG(gProcessIsolationLog, LogLevel::Error,
                            ("Process Switch: Got invalid new-tab "
                             "BrowsingContext"));
                    self->RedirectToRealChannelFinished(NS_ERROR_FAILURE);
                    return;
                  }

                  MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
                          ("Process Switch: Redirected load to new tab"));
                  self->TriggerProcessSwitch(
                      MOZ_KnownLive(aBrowsingContext->Canonical()), options,
                      /* aIsNewTab */ true);
                },
            [self = RefPtr{this}](const CopyableErrorResult&) {
              MOZ_LOG(gProcessIsolationLog, LogLevel::Error,
                      ("Process Switch: SwitchToNewTab failed"));
              self->RedirectToRealChannelFinished(NS_ERROR_FAILURE);
            });
    return true;
  }

  // If we're doing a document load, we can immediately perform a process
  // switch.
  if (mIsDocumentLoad) {
    TriggerProcessSwitch(browsingContext, options);
    return true;
  }

  // We're not doing a document load, which means we must be performing an
  // object load. We need a BrowsingContext to perform the switch in, so will
  // trigger an upgrade.
  if (!mObjectUpgradeHandler) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Warning,
            ("Process Switch Abort: no object upgrade handler"));
    return false;
  }

  mObjectUpgradeHandler->UpgradeObjectLoad()->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self = RefPtr{this}, options,
       parentWindow](const RefPtr<CanonicalBrowsingContext>& aBrowsingContext)
          MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA mutable {
            if (aBrowsingContext->IsDiscarded() ||
                parentWindow != aBrowsingContext->GetParentWindowContext()) {
              MOZ_LOG(
                  gProcessIsolationLog, LogLevel::Error,
                  ("Process Switch: Got invalid BrowsingContext from object "
                   "upgrade!"));
              self->RedirectToRealChannelFinished(NS_ERROR_FAILURE);
              return;
            }

            // At this point the element has stored the container feature policy
            // in the new browsing context, but we need to make sure that we
            // copy it over to the load info.
            nsCOMPtr<nsILoadInfo> loadInfo = self->mChannel->LoadInfo();
            if (aBrowsingContext->GetContainerFeaturePolicy()) {
              loadInfo->SetContainerFeaturePolicyInfo(
                  *aBrowsingContext->GetContainerFeaturePolicy());
            }

            MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
                    ("Process Switch: Upgraded Object to Document Load"));
            self->TriggerProcessSwitch(aBrowsingContext, options);
          },
      [self = RefPtr{this}](nsresult aStatusCode) {
        MOZ_ASSERT(NS_FAILED(aStatusCode), "Status should be error");
        self->RedirectToRealChannelFinished(aStatusCode);
      });
  return true;
}

void DocumentLoadListener::TriggerProcessSwitch(
    CanonicalBrowsingContext* aContext,
    const NavigationIsolationOptions& aOptions, bool aIsNewTab) {
  MOZ_DIAGNOSTIC_ASSERT(aIsNewTab || aContext->IsOwnedByProcess(
                                         GetContentProcessId(mContentParent)),
                        "not owned by creator process anymore?");
  if (MOZ_LOG_TEST(gProcessIsolationLog, LogLevel::Info)) {
    nsCString currentRemoteType = "INVALID"_ns;
    aContext->GetCurrentRemoteType(currentRemoteType, IgnoreErrors());

    MOZ_LOG(gProcessIsolationLog, LogLevel::Info,
            ("Process Switch: Changing Remoteness from '%s' to '%s'",
             currentRemoteType.get(), aOptions.mRemoteType.get()));
  }

  // Stash our stream filter requests to pass to TriggerRedirectToRealChannel,
  // as the call to `DisconnectListeners` will clear our list.
  nsTArray<StreamFilterRequest> streamFilterRequests =
      std::move(mStreamFilterRequests);

  // We're now committing to a process switch, so we can disconnect from
  // the listeners in the old process.
  // As the navigation is continuing, we don't actually want to cancel the
  // request in the old process unless we're redirecting the load into a new
  // tab.
  DisconnectListeners(NS_BINDING_ABORTED, NS_BINDING_ABORTED, !aIsNewTab);

  MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
          ("Process Switch: Calling ChangeRemoteness"));
  aContext->ChangeRemoteness(aOptions, mLoadIdentifier)
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self = RefPtr{this}, requests = std::move(streamFilterRequests)](
              const std::pair<RefPtr<BrowserParent>,
                              RefPtr<CanonicalBrowsingContext>>&
                  aResult) mutable {
            MOZ_ASSERT(self->mChannel,
                       "Something went wrong, channel got cancelled");
            const auto& [browserParent, browsingContext] = aResult;
            self->TriggerRedirectToRealChannel(
                browsingContext,
                Some(browserParent ? browserParent->Manager() : nullptr),
                std::move(requests));
          },
          [self = RefPtr{this}](nsresult aStatusCode) {
            MOZ_ASSERT(NS_FAILED(aStatusCode), "Status should be error");
            self->RedirectToRealChannelFinished(aStatusCode);
          });
}

RefPtr<PDocumentChannelParent::RedirectToRealChannelPromise>
DocumentLoadListener::RedirectToParentProcess(uint32_t aRedirectFlags,
                                              uint32_t aLoadFlags) {
  // This is largely the same as ContentChild::RecvCrossProcessRedirect,
  // except without needing to deserialize or create an nsIChildChannel.

  RefPtr<nsDocShellLoadState> loadState;
  nsDocShellLoadState::CreateFromPendingChannel(
      mChannel, mLoadIdentifier, mRedirectChannelId, getter_AddRefs(loadState));

  loadState->SetLoadFlags(mLoadStateExternalLoadFlags);
  loadState->SetInternalLoadFlags(mLoadStateInternalLoadFlags);
  loadState->SetLoadType(mLoadStateLoadType);
  if (mLoadingSessionHistoryInfo) {
    loadState->SetLoadingSessionHistoryInfo(*mLoadingSessionHistoryInfo);
  }

  // This is poorly named now.
  RefPtr<ChildProcessChannelListener> processListener =
      ChildProcessChannelListener::GetSingleton();

  auto promise =
      MakeRefPtr<PDocumentChannelParent::RedirectToRealChannelPromise::Private>(
          __func__);
  promise->UseDirectTaskDispatch(__func__);
  auto resolve = [promise](nsresult aResult) {
    promise->Resolve(aResult, __func__);
  };

  nsTArray<ipc::Endpoint<extensions::PStreamFilterParent>> endpoints;
  processListener->OnChannelReady(loadState, mLoadIdentifier,
                                  std::move(endpoints), mTiming,
                                  std::move(resolve));

  return promise;
}

RefPtr<PDocumentChannelParent::RedirectToRealChannelPromise>
DocumentLoadListener::RedirectToRealChannel(
    uint32_t aRedirectFlags, uint32_t aLoadFlags,
    const Maybe<ContentParent*>& aDestinationProcess,
    nsTArray<ParentEndpoint>&& aStreamFilterEndpoints) {
  LOG(
      ("DocumentLoadListener RedirectToRealChannel [this=%p] "
       "aRedirectFlags=%" PRIx32 ", aLoadFlags=%" PRIx32,
       this, aRedirectFlags, aLoadFlags));

  if (mIsDocumentLoad) {
    // TODO(djg): Add the last URI visit to history if success. Is there a
    // better place to handle this? Need access to the updated aLoadFlags.
    nsresult status = NS_OK;
    mChannel->GetStatus(&status);
    bool updateGHistory =
        nsDocShell::ShouldUpdateGlobalHistory(mLoadStateLoadType);
    if (NS_SUCCEEDED(status) && updateGHistory &&
        !net::ChannelIsPost(mChannel)) {
      AddURIVisit(mChannel, aLoadFlags);
    }
  }

  // Register the new channel and obtain id for it
  nsCOMPtr<nsIRedirectChannelRegistrar> registrar =
      RedirectChannelRegistrar::GetOrCreate();
  if (!registrar) {
    // Shutdown is in progress.
    return PDocumentChannelParent::RedirectToRealChannelPromise::
        CreateAndReject(ipc::ResponseRejectReason::SendError, __func__);
  }
  nsCOMPtr<nsIChannel> chan = mChannel;
  if (nsCOMPtr<nsIViewSourceChannel> vsc = do_QueryInterface(chan)) {
    chan = vsc->GetInnerChannel();
  }
  mRedirectChannelId = nsContentUtils::GenerateLoadIdentifier();
  MOZ_ALWAYS_SUCCEEDS(registrar->RegisterChannel(chan, mRedirectChannelId));

  if (aDestinationProcess) {
    RefPtr<ContentParent> cp = *aDestinationProcess;
    if (!cp) {
      MOZ_ASSERT(aStreamFilterEndpoints.IsEmpty());
      return RedirectToParentProcess(aRedirectFlags, aLoadFlags);
    }

    if (!cp->CanSend()) {
      return PDocumentChannelParent::RedirectToRealChannelPromise::
          CreateAndReject(ipc::ResponseRejectReason::SendError, __func__);
    }

    nsTArray<EarlyHintConnectArgs> ehArgs;
    mEarlyHintsService.RegisterLinksAndGetConnectArgs(cp->ChildID(), ehArgs);

    RedirectToRealChannelArgs args;
    SerializeRedirectData(args, /* aIsCrossProcess */ true, aRedirectFlags,
                          aLoadFlags, std::move(ehArgs),
                          mEarlyHintsService.LinkType());
    if (mTiming) {
      mTiming->Anonymize(args.uri());
      args.timing() = std::move(mTiming);
    }

    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    cp->TransmitBlobDataIfBlobURL(args.uri(), loadInfo->GetOriginAttributes());

    if (CanonicalBrowsingContext* bc = GetDocumentBrowsingContext()) {
      if (bc->IsTop() && bc->IsActive()) {
        nsContentUtils::RequestGeckoTaskBurst();
      }
    }

    return cp->SendCrossProcessRedirect(args,
                                        std::move(aStreamFilterEndpoints));
  }

  if (mOpenPromiseResolved) {
    LOG(
        ("DocumentLoadListener RedirectToRealChannel [this=%p] "
         "promise already resolved. Aborting.",
         this));
    // The promise has already been resolved or aborted, so we have no way to
    // return a promise again to the listener which would cancel the operation.
    // Reject the promise immediately.
    return PDocumentChannelParent::RedirectToRealChannelPromise::
        CreateAndResolve(NS_BINDING_ABORTED, __func__);
  }

  // This promise will be passed on the promise listener which will
  // resolve this promise for us.
  auto promise =
      MakeRefPtr<PDocumentChannelParent::RedirectToRealChannelPromise::Private>(
          __func__);

  mOpenPromise->Resolve(
      OpenPromiseSucceededType({std::move(aStreamFilterEndpoints),
                                aRedirectFlags, aLoadFlags,
                                mEarlyHintsService.LinkType(), promise}),
      __func__);

  // There is no way we could come back here if the promise had been resolved
  // previously. But for clarity and to avoid all doubt, we set this boolean to
  // true.
  mOpenPromiseResolved = true;

  return promise;
}

void DocumentLoadListener::TriggerRedirectToRealChannel(
    CanonicalBrowsingContext* aDestinationBrowsingContext,
    const Maybe<ContentParent*>& aDestinationProcess,
    nsTArray<StreamFilterRequest> aStreamFilterRequests) {
  LOG(
      ("DocumentLoadListener::TriggerRedirectToRealChannel [this=%p] "
       "aDestinationBrowsingContext=%" PRIx64 " aDestinationProcess=%" PRId64,
       this, aDestinationBrowsingContext->Id(),
       aDestinationProcess.valueOr(nullptr)
           ? int64_t((*aDestinationProcess)->ChildID())
           : int64_t(-1)));
  MOZ_ASSERT(aDestinationBrowsingContext);

  // This initiates replacing the current DocumentChannel with a
  // protocol specific 'real' channel, maybe in a different process than
  // the current DocumentChannelChild, if aDestinationProces is set.
  // It registers the current mChannel with the registrar to get an ID
  // so that the remote end can setup a new IPDL channel and lookup
  // the same underlying channel.
  // We expect this process to finish with FinishReplacementChannelSetup
  // (for both in-process and process switch cases), where we cleanup
  // the registrar and copy across any needed state to the replacing
  // IPDL parent object.

  // If we've already called `DisconnectListeners`, and have no destination
  // process we're switching to, abort early to skip the following checks.
  if (mOpenPromiseResolved && !aDestinationProcess) {
    LOG(
        ("DocumentLoadListener::TriggerRedirectToRealChannel [this=%p] "
         "Listeners already disconnected for non-switching redirect. Aborting.",
         this));
    RedirectToRealChannelFinished(NS_BINDING_ABORTED);
    return;
  }

  RefPtr<ContentParent> contentParent =
      aDestinationProcess.valueOr(mContentParent);

  nsTArray<ParentEndpoint> parentEndpoints(aStreamFilterRequests.Length());
  if (!aStreamFilterRequests.IsEmpty()) {
    base::ProcessId pid =
        contentParent ? contentParent->OtherPid() : base::ProcessId{0};

    for (StreamFilterRequest& request : aStreamFilterRequests) {
      if (!pid) {
        request.mPromise->Reject(false, __func__);
        request.mPromise = nullptr;
        continue;
      }
      ParentEndpoint parent;
      nsresult rv = extensions::PStreamFilter::CreateEndpoints(
          &parent, &request.mChildEndpoint);

      if (NS_FAILED(rv)) {
        request.mPromise->Reject(false, __func__);
        request.mPromise = nullptr;
      } else {
        parentEndpoints.AppendElement(std::move(parent));
      }
    }
  }

  // Check if the load is for a "silent" error (i.e. no document or error page
  // will load). This is the case for all failed object/embed loads, and some
  // failed document loads.
  // We never process switch for these silent loads (as that will destroy the
  // existing document, which is being navigated away from)
  nsresult status = NS_OK;
  mChannel->GetStatus(&status);
  bool silentErrorLoad = !DocShellWillDisplayContent(status);

  // Get the unsandboxed result principal for our channel. This is required both
  // to validate that the response which will be loaded is being sent to the
  // appropriate process, as well as to apply origin keying.
  nsCOMPtr<nsIPrincipal> unsandboxedPrincipal;
  nsresult rv = nsScriptSecurityManager::GetScriptSecurityManager()
                    ->GetChannelResultPrincipalIfNotSandboxed(
                        mChannel, getter_AddRefs(unsandboxedPrincipal));
  if (NS_FAILED(rv)) {
    LOG(
        ("DocumentLoadListener::TriggerRedirectToRealChannel [this=%p] "
         "GetChannelResultPrincipalIfNotSandboxed failed",
         this));
    RedirectToRealChannelFinished(NS_ERROR_FAILURE);
    return;
  }

  // Checks related to loads which will complete in a content process.
  //
  // These checks are skipped for silent error loads, as those never process
  // switch, and should not result in a document being loaded in the content
  // process.
  if (contentParent && !silentErrorLoad) {
    nsCOMPtr<nsIURI> docURI;
    MOZ_ALWAYS_SUCCEEDS(
        NS_GetFinalChannelURI(mChannel, getter_AddRefs(docURI)));

    // Validate that the target process, if specified, would be allowed to load
    // this principal, and fail the navigation if it would not.
    // NOTE: Keep this in sync with the similar check in
    // BrowserParent::RecvNewWindowGlobal.
    EnumSet<ValidatePrincipalOptions> validationOptions = {};
    // FIXME(bug 1699385): Remove allowSystem for blobs
    // FIXME(bug 1698087): chrome://devtools/**/webextension-fallback.html
    // Automation-Only: chrome://reftest/** + blank subframes
    if (docURI->SchemeIs("blob") || docURI->SchemeIs("chrome") ||
        (xpc::IsInAutomation() && NS_IsAboutBlank(docURI) &&
         GetParentWindowContext() &&
         GetParentWindowContext()->Manager()->Manager() == contentParent &&
         GetParentWindowContext()->DocumentPrincipal()->IsSystemPrincipal())) {
      validationOptions += ValidatePrincipalOptions::AllowSystem;
    }
    if (!contentParent->ValidatePrincipal(unsandboxedPrincipal,
                                          validationOptions)) {
      ContentParent::LogAndAssertFailedPrincipalValidationInfo(
          unsandboxedPrincipal, "TriggerRedirectToRealChannel");
      RedirectToRealChannelFinished(NS_ERROR_FAILURE);
      return;
    }

    // Give ContentParent a chance to transmit information such as permissions
    // into the content process.
    rv = contentParent->AboutToLoadDocumentForChild(mChannel);
    if (NS_FAILED(rv)) {
      LOG(
          ("DocumentLoadListener::TriggerRedirectToRealChannel [this=%p] "
           "AboutToLoadDocumentForChild failed",
           this));
      RedirectToRealChannelFinished(rv);
      return;
    }
  }

  // Ensure that the BrowsingContextGroup which will finish this load has the
  // UseOriginAgentCluster flag set to a value. We'll try to base it on
  // `mChannel` if it has the appropriate header.
  //
  // This needs to be set such that we can perform correct DocGroup keying in
  // the content process.
  //
  // In effect, this is performing the side-effect component of "obtain a
  // similar-origin window agent", leading to the historical agent cluster key
  // map being populated in the BrowsingContextGroup.
  //
  // https://html.spec.whatwg.org/#obtain-similar-origin-window-agent
  if (aDestinationBrowsingContext->Group()
          ->UsesOriginAgentCluster(unsandboxedPrincipal)
          .isNothing()) {
    // UseOriginAgentCluster requires a secure context, so never origin key
    // unless we're a potentially-trustworthy origin.
    //
    // We don't handle this within BrowsingContextGroup, as the set of URIs
    // which are considered "potentially trustworthy" can change at runtime, so
    // we want to cache the decision at the time we make it.
    bool isSecureContext =
        unsandboxedPrincipal->GetIsOriginPotentiallyTrustworthy();
    bool hasOriginAgentCluster =
        StaticPrefs::dom_origin_agent_cluster_default() && isSecureContext;
    if (nsCOMPtr<nsIHttpChannelInternal> httpChannel =
            do_QueryInterface(mChannel);
        httpChannel && isSecureContext &&
        StaticPrefs::dom_origin_agent_cluster_enabled()) {
      bool headerValue = false;
      if (NS_SUCCEEDED(
              httpChannel->GetOriginAgentClusterHeader(&headerValue))) {
        hasOriginAgentCluster = headerValue;
      }
    }
    aDestinationBrowsingContext->Group()->SetUseOriginAgentClusterFromNetwork(
        unsandboxedPrincipal, hasOriginAgentCluster);
  }

  // If we didn't have any redirects, then we pass the REDIRECT_INTERNAL flag
  // for this channel switch so that it isn't recorded in session history etc.
  // If there were redirect(s), then we want this switch to be recorded as a
  // real one, since we have a new URI.
  uint32_t redirectFlags = 0;
  if (!mHaveVisibleRedirect) {
    redirectFlags = nsIChannelEventSink::REDIRECT_INTERNAL;
  }

  uint32_t newLoadFlags = nsIRequest::LOAD_NORMAL;
  MOZ_ALWAYS_SUCCEEDS(mChannel->GetLoadFlags(&newLoadFlags));
  // We're pulling our flags from the inner channel, which may not have this
  // flag set on it. This is the case when loading a 'view-source' channel.
  if (mIsDocumentLoad || aDestinationProcess) {
    newLoadFlags |= nsIChannel::LOAD_DOCUMENT_URI;
  }
  if (!aDestinationProcess) {
    newLoadFlags |= nsIChannel::LOAD_REPLACE;
  }

  // INHIBIT_PERSISTENT_CACHING is clearing during http redirects (from
  // both parent and content process channel instances), but only ever
  // re-added to the parent-side nsHttpChannel.
  // To match that behaviour, we want to explicitly avoid copying this flag
  // back to our newly created content side channel, otherwise it can
  // affect sub-resources loads in the same load group.
  nsCOMPtr<nsIURI> uri;
  mChannel->GetURI(getter_AddRefs(uri));
  if (uri && uri->SchemeIs("https")) {
    newLoadFlags &= ~nsIRequest::INHIBIT_PERSISTENT_CACHING;
  }

  RefPtr<DocumentLoadListener> self = this;
  RedirectToRealChannel(redirectFlags, newLoadFlags, aDestinationProcess,
                        std::move(parentEndpoints))
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self, requests = std::move(aStreamFilterRequests)](
              const nsresult& aResponse) mutable {
            for (StreamFilterRequest& request : requests) {
              if (request.mPromise) {
                request.mPromise->Resolve(std::move(request.mChildEndpoint),
                                          __func__);
                request.mPromise = nullptr;
              }
            }
            self->RedirectToRealChannelFinished(aResponse);
          },
          [self](const mozilla::ipc::ResponseRejectReason) {
            self->RedirectToRealChannelFinished(NS_ERROR_FAILURE);
          });
}

void DocumentLoadListener::MaybeReportBlockedByURLClassifier(nsresult aStatus) {
  auto* browsingContext = GetDocumentBrowsingContext();
  if (!browsingContext || browsingContext->IsTop() ||
      !StaticPrefs::privacy_trackingprotection_testing_report_blocked_node()) {
    return;
  }

  if (!UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(aStatus)) {
    return;
  }

  RefPtr<WindowGlobalParent> parent = browsingContext->GetParentWindowContext();
  if (parent) {
    (void)parent->SendAddBlockedFrameNodeByClassifier(browsingContext);
  }
}

bool DocumentLoadListener::DocShellWillDisplayContent(nsresult aStatus) {
  if (NS_SUCCEEDED(aStatus)) {
    return true;
  }

  // Always return errored loads to the <object> or <embed> element's process,
  // as load errors will not be rendered as documents.
  if (!mIsDocumentLoad) {
    return false;
  }

  // nsDocShell attempts urifixup on some failure types,
  // but also of those also display an error page if we don't
  // succeed with fixup, so we don't need to check for it
  // here.

  auto* loadingContext = GetLoadingBrowsingContext();

  nsresult rv = nsDocShell::FilterStatusForErrorPage(
      aStatus, mChannel, mLoadStateLoadType, loadingContext->IsTop(),
      loadingContext->GetUseErrorPages(), nullptr);

  // If filtering returned a failure code, then an error page will
  // be display for that code, so return true;
  return NS_FAILED(rv);
}

bool DocumentLoadListener::MaybeHandleLoadErrorWithURIFixup(nsresult aStatus) {
  RefPtr<CanonicalBrowsingContext> bc = GetDocumentBrowsingContext();
  if (!bc) {
    return false;
  }

  nsCOMPtr<nsIInputStream> newPostData;
  nsILoadInfo::SchemelessInputType schemelessInput =
      nsILoadInfo::SchemelessInputTypeUnset;
  nsCOMPtr<nsIURI> newURI = nsDocShell::AttemptURIFixup(
      mChannel, aStatus, mOriginalUriString, mLoadStateLoadType, bc->IsTop(),
      mLoadStateInternalLoadFlags &
          nsDocShell::INTERNAL_LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP,
      bc->UsePrivateBrowsing(), true, getter_AddRefs(newPostData),
      &schemelessInput);

  // Since aStatus will be NS_OK for 4xx and 5xx error codes we
  // have to check each request which was upgraded by https-first.
  // If an error (including 4xx and 5xx) occured, then let's check if
  // we can downgrade the scheme to HTTP again.
  bool isHTTPSFirstFixup = false;
  if (!newURI) {
    newURI =
        nsHTTPSOnlyUtils::PotentiallyDowngradeHttpsFirstRequest(this, aStatus);
    isHTTPSFirstFixup = true;
  }

  if (!newURI) {
    return false;
  }

  // If we got a new URI, then we should initiate a load with that.
  // Notify the listeners that this load is complete (with a code that
  // won't trigger an error page), and then start the new one.
  DisconnectListeners(NS_BINDING_ABORTED, NS_BINDING_ABORTED);

  RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(newURI);
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();

  nsCOMPtr<nsIPolicyContainer> policyContainerToInherit =
      loadInfo->GetPolicyContainerToInherit();
  loadState->SetPolicyContainer(policyContainerToInherit);

  nsCOMPtr<nsIPrincipal> triggeringPrincipal = loadInfo->TriggeringPrincipal();
  loadState->SetTriggeringPrincipal(triggeringPrincipal);

  loadState->SetPostDataStream(newPostData);

  // Record whether the protocol was added through a fixup.
  loadState->SetSchemelessInput(schemelessInput);

  if (isHTTPSFirstFixup) {
    nsHTTPSOnlyUtils::UpdateLoadStateAfterHTTPSFirstDowngrade(this, loadState);
  }

  // Ensure to set referrer information in the fallback channel equally to the
  // not-upgraded original referrer info.
  //
  // A simply copy of the referrer info from the upgraded one leads to problems.
  // For example:
  // 1. https://some-site.com redirects to http://other-site.com with referrer
  // policy
  //   "no-referrer-when-downgrade".
  // 2. https-first upgrades the redirection, so redirects to
  // https://other-site.com,
  //    according to referrer policy the referrer will be send (https-> https)
  // 3. Assume other-site.com is not supporting https, https-first performs
  // fall-
  //    back.
  // If the referrer info from the upgraded channel gets copied into the
  // http fallback channel, the referrer info would contain the referrer
  // (https://some-site.com). That would violate the policy
  // "no-referrer-when-downgrade". A recreation of the original referrer info
  // would ensure us that the referrer is set according to the referrer policy.
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel);
  if (httpChannel) {
    nsCOMPtr<nsIReferrerInfo> referrerInfo = httpChannel->GetReferrerInfo();
    if (referrerInfo) {
      ReferrerPolicy referrerPolicy = referrerInfo->ReferrerPolicy();
      nsCOMPtr<nsIURI> originalReferrer = referrerInfo->GetOriginalReferrer();
      if (originalReferrer) {
        // Create new ReferrerInfo with the original referrer and the referrer
        // policy.
        nsCOMPtr<nsIReferrerInfo> newReferrerInfo =
            new ReferrerInfo(originalReferrer, referrerPolicy);
        loadState->SetReferrerInfo(newReferrerInfo);
      }
    }
  }

  bc->LoadURI(loadState, false);
  return true;
}

NS_IMETHODIMP
DocumentLoadListener::OnStartRequest(nsIRequest* aRequest) {
  return DoOnStartRequest(aRequest);
}

nsresult DocumentLoadListener::DoOnStartRequest(nsIRequest* aRequest) {
  LOG(("DocumentLoadListener OnStartRequest [this=%p]", this));

  nsCOMPtr<nsIMultiPartChannel> multiPartChannel = do_QueryInterface(aRequest);
  if (multiPartChannel) {
    multiPartChannel->GetBaseChannel(getter_AddRefs(mChannel));
  } else {
    mChannel = do_QueryInterface(aRequest);
  }
  MOZ_DIAGNOSTIC_ASSERT(mChannel);

  if (mHaveVisibleRedirect && GetDocumentBrowsingContext() &&
      mLoadingSessionHistoryInfo) {
    mLoadingSessionHistoryInfo =
        GetDocumentBrowsingContext()->ReplaceLoadingSessionHistoryEntryForLoad(
            mLoadingSessionHistoryInfo.get(), mChannel);
  }

  RefPtr<nsHttpChannel> httpChannel = do_QueryObject(mChannel);

  // Enforce CSP frame-ancestors and x-frame-options checks which
  // might cancel the channel.
  nsContentSecurityUtils::PerformCSPFrameAncestorAndXFOCheck(mChannel);

  // HTTPS-Only Mode tries to upgrade connections to https. Once loading
  // is in progress we set that flag so that timeout counter measures
  // do not kick in.
  if (httpChannel) {
    nsCOMPtr<nsILoadInfo> loadInfo = httpChannel->LoadInfo();
    if (nsHTTPSOnlyUtils::GetUpgradeMode(loadInfo) ==
        nsHTTPSOnlyUtils::HTTPS_ONLY_MODE) {
      uint32_t httpsOnlyStatus = loadInfo->GetHttpsOnlyStatus();
      httpsOnlyStatus |= nsILoadInfo::HTTPS_ONLY_TOP_LEVEL_LOAD_IN_PROGRESS;
      loadInfo->SetHttpsOnlyStatus(httpsOnlyStatus);
    }

    if (mLoadingSessionHistoryInfo &&
        nsDocShell::ShouldDiscardLayoutState(httpChannel)) {
      mLoadingSessionHistoryInfo->mInfo.SetSaveLayoutStateFlag(false);
    }
  }

  auto* loadingContext = GetLoadingBrowsingContext();
  if (!loadingContext || loadingContext->IsDiscarded()) {
    Cancel(NS_ERROR_UNEXPECTED, "No valid LoadingBrowsingContext."_ns);
    return NS_ERROR_UNEXPECTED;
  }

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    Cancel(NS_ERROR_ILLEGAL_DURING_SHUTDOWN,
           "Aborting OnStartRequest after shutdown started."_ns);
    return NS_OK;
  }

  // Block top-level data URI navigations if triggered by the web. Logging is
  // performed in AllowTopLevelNavigationToDataURI.
  if (!nsContentSecurityManager::AllowTopLevelNavigationToDataURI(mChannel)) {
    mChannel->Cancel(NS_ERROR_DOM_BAD_URI);
    if (loadingContext) {
      RefPtr<MaybeCloseWindowHelper> maybeCloseWindowHelper =
          new MaybeCloseWindowHelper(loadingContext);
      // If a new window was opened specifically for this request, close it
      // after blocking the navigation.
      maybeCloseWindowHelper->SetShouldCloseWindow(
          IsFirstLoadInWindow(mChannel));
      (void)maybeCloseWindowHelper->MaybeCloseWindow();
    }
    DisconnectListeners(NS_ERROR_DOM_BAD_URI, NS_ERROR_DOM_BAD_URI);
    return NS_OK;
  }

  // Generally we want to switch to a real channel even if the request failed,
  // since the listener might want to access protocol-specific data (like http
  // response headers) in its error handling.
  // An exception to this is when nsExtProtocolChannel handled the request and
  // returned NS_ERROR_NO_CONTENT, since creating a real one in the content
  // process will attempt to handle the URI a second time.
  nsresult status = NS_OK;
  aRequest->GetStatus(&status);
  if (status == NS_ERROR_NO_CONTENT) {
    DisconnectListeners(status, status);
    return NS_OK;
  }

  // PerformCSPFrameAncestorAndXFOCheck may cancel a moz-extension request that
  // needs to be handled here. Without this, the resource would be loaded and
  // not blocked when the real channel is created in the content process.
  if (status == NS_ERROR_CSP_FRAME_ANCESTOR_VIOLATION && !httpChannel) {
    DisconnectListeners(status, status);
    return NS_OK;
  }

  // If this was a failed load and we want to try fixing the uri, then
  // this will initiate a new load (and disconnect this one), and we don't
  // need to do anything else.
  if (MaybeHandleLoadErrorWithURIFixup(status)) {
    return NS_OK;
  }

  // If this is a successful load with a successful status code, we can possibly
  // submit HTTPS-First telemetry.
  if (NS_SUCCEEDED(status) && httpChannel) {
    uint32_t responseStatus = 0;
    if (NS_SUCCEEDED(httpChannel->GetResponseStatus(&responseStatus)) &&
        responseStatus < 400) {
      nsHTTPSOnlyUtils::SubmitHTTPSFirstTelemetry(
          mChannel->LoadInfo(), mHTTPSFirstDowngradeData.forget());
    }
  }

  mStreamListenerFunctions.AppendElement(StreamListenerFunction{
      VariantIndex<0>{}, OnStartRequestParams{aRequest}});

  if (mOpenPromiseResolved || mInitiatedRedirectToRealChannel) {
    // I we have already resolved the promise, there's no point to continue
    // attempting a process switch or redirecting to the real channel.
    // We can also have multiple calls to OnStartRequest when dealing with
    // multi-part content, but only want to redirect once.
    return NS_OK;
  }

  // Keep track of server responses resulting in a document for the Bounce
  // Tracking Protection.
  if (mIsDocumentLoad && GetParentWindowContext() == nullptr &&
      loadingContext->IsTopContent()) {
    RefPtr<BounceTrackingState> bounceTrackingState =
        loadingContext->GetBounceTrackingState();

    // Not every browsing context has a BounceTrackingState. It's also null when
    // the feature is disabled.
    if (bounceTrackingState) {
      // Don't warn when OnDocumentStartRequest fails until bug 1894936 is
      // fixed, because it fails frequently because of that.
      (void)bounceTrackingState->OnDocumentStartRequest(mChannel);

      DynamicFpiNavigationHeuristic::MaybeGrantStorageAccess(loadingContext,
                                                             mChannel);
    }
  }

  mChannel->Suspend();

  mInitiatedRedirectToRealChannel = true;

  MaybeReportBlockedByURLClassifier(status);

  // If the channel has failed, and the docshell isn't going to display an
  // error page for that failure, then don't allow process switching, since
  // we just want to keep our existing document.
  bool silentErrorLoad = !DocShellWillDisplayContent(status);
  if (silentErrorLoad) {
    MOZ_LOG(gProcessIsolationLog, LogLevel::Verbose,
            ("Skipping process switch, as DocShell will not display content "
             "(status: %s) %s",
             GetStaticErrorName(status),
             GetChannelCreationURI()->GetSpecOrDefault().get()));

    // If this load would not lead to the content docShell displaying any
    // content, cancel it here to ensure that we don't spuriously succeed when
    // finishing the load in the content process. We don't do this for HTTP
    // channels, which may have extra information (e.g. navigation timing) which
    // would be relevant to the content process.
    if (!httpChannel) {
      // Resume the channel that was suspended above so that the pump
      // can call OnStopRequest, which breaks the reference cycle between
      // DocumentLoadListener and ParentChannelListener.
      mChannel->Resume();
      DisconnectListeners(status, status);
      return NS_OK;
    }
  }

  // Determine if a new process needs to be spawned. If it does, this will
  // trigger a cross process switch, and we should hold off on redirecting to
  // the real channel.
  bool willBeRemote = false;
  if (silentErrorLoad || !MaybeTriggerProcessSwitch(&willBeRemote)) {
    // We're not going to be doing a process switch, so redirect to the real
    // channel within our current process.
    nsTArray<StreamFilterRequest> streamFilterRequests =
        std::move(mStreamFilterRequests);
    if (!mSupportsRedirectToRealChannel) {
      RefPtr<BrowserParent> browserParent = loadingContext->GetBrowserParent();
      if (browserParent->Manager() != mContentParent) {
        LOG(
            ("DocumentLoadListener::RedirectToRealChannel failed because "
             "browsingContext no longer owned by creator"));
        Cancel(NS_BINDING_ABORTED,
               "DocumentLoadListener::RedirectToRealChannel failed because "
               "browsingContext no longer owned by creator"_ns);
        return NS_OK;
      }
      MOZ_DIAGNOSTIC_ASSERT(
          browserParent->GetBrowsingContext() == loadingContext,
          "make sure the load is going to the right place");

      // If the existing process is right for this load, but the bridge doesn't
      // support redirects, then we need to do it manually, by faking a process
      // switch.
      DisconnectListeners(NS_BINDING_ABORTED, NS_BINDING_ABORTED,
                          /* aContinueNavigating */ true);

      // Notify the docshell that it should load using the newly connected
      // channel
      browserParent->ResumeLoad(mLoadIdentifier);

      // Use the current process ID to run the 'process switch' path and connect
      // the channel into the current process.
      TriggerRedirectToRealChannel(loadingContext, Some(mContentParent),
                                   std::move(streamFilterRequests));
    } else {
      TriggerRedirectToRealChannel(loadingContext, Nothing(),
                                   std::move(streamFilterRequests));
    }

    // If we're not switching, then check if we're currently remote.
    if (mContentParent) {
      willBeRemote = true;
    }
  }

  if (httpChannel) {
    mEarlyHintsService.Reset();
  } else {
    mEarlyHintsService.Cancel(
        "DocumentLoadListener::OnStartRequest: no httpChannel"_ns);
  }

  // If we're going to be delivering this channel to a remote content
  // process, then we want to install any required content conversions
  // in the content process.
  // The caller of this OnStartRequest will install a conversion
  // helper after we return if we haven't disabled conversion. Normally
  // HttpChannelParent::OnStartRequest would disable conversion, but we're
  // defering calling that until later. Manually disable it now to prevent the
  // converter from being installed (since we want the child to do it), and
  // also save the value so that when we do call
  // HttpChannelParent::OnStartRequest, we can have the value as it originally
  // was.
  if (httpChannel) {
    (void)httpChannel->GetApplyConversion(&mOldApplyConversion);
    if (willBeRemote) {
      httpChannel->SetApplyConversion(false);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::OnStopRequest(nsIRequest* aRequest,
                                    nsresult aStatusCode) {
  LOG(("DocumentLoadListener OnStopRequest [this=%p]", this));
  mStreamListenerFunctions.AppendElement(StreamListenerFunction{
      VariantIndex<2>{}, OnStopRequestParams{aRequest, aStatusCode}});

  // If we're not a multi-part channel, then we're finished and we don't
  // expect any further events. If we are, then this might be called again,
  // so wait for OnAfterLastPart instead.
  nsCOMPtr<nsIMultiPartChannel> multiPartChannel = do_QueryInterface(aRequest);
  if (!multiPartChannel) {
    mIsFinished = true;
  }

  mStreamFilterRequests.Clear();

  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::OnDataAvailable(nsIRequest* aRequest,
                                      nsIInputStream* aInputStream,
                                      uint64_t aOffset, uint32_t aCount) {
  LOG(("DocumentLoadListener OnDataAvailable [this=%p]", this));
  // This isn't supposed to happen, since we suspended the channel, but
  // sometimes Suspend just doesn't work. This can happen when we're routing
  // through nsUnknownDecoder to sniff the content type, and it doesn't handle
  // being suspended. Let's just store the data and manually forward it to our
  // redirected channel when it's ready.
  nsCString data;
  nsresult rv = NS_ReadInputStreamToString(aInputStream, data, aCount);
  NS_ENSURE_SUCCESS(rv, rv);

  mStreamListenerFunctions.AppendElement(StreamListenerFunction{
      VariantIndex<1>{},
      OnDataAvailableParams{aRequest, std::move(data), aOffset, aCount}});

  return NS_OK;
}

//-----------------------------------------------------------------------------
// DoucmentLoadListener::nsIMultiPartChannelListener
//-----------------------------------------------------------------------------

NS_IMETHODIMP
DocumentLoadListener::OnAfterLastPart(nsresult aStatus) {
  LOG(("DocumentLoadListener OnAfterLastPart [this=%p]", this));
  if (!mInitiatedRedirectToRealChannel) {
    // if we get here, and we haven't initiated a redirect to a real
    // channel, then it means we never got OnStartRequest (maybe a problem?)
    // and we retargeted everything.
    LOG(("DocumentLoadListener Disconnecting child"));
    DisconnectListeners(NS_BINDING_RETARGETED, NS_OK);
    return NS_OK;
  }
  mStreamListenerFunctions.AppendElement(StreamListenerFunction{
      VariantIndex<3>{}, OnAfterLastPartParams{aStatus}});
  mIsFinished = true;
  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::GetInterface(const nsIID& aIID, void** result) {
  RefPtr<CanonicalBrowsingContext> browsingContext =
      GetLoadingBrowsingContext();
  if (aIID.Equals(NS_GET_IID(nsILoadContext)) && browsingContext) {
    browsingContext.forget(result);
    return NS_OK;
  }

  return QueryInterface(aIID, result);
}

////////////////////////////////////////////////////////////////////////////////
// nsIParentChannel
////////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP
DocumentLoadListener::SetParentListener(
    mozilla::net::ParentChannelListener* listener) {
  // We don't need this (do we?)
  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::SetClassifierMatchedInfo(const nsACString& aList,
                                               const nsACString& aProvider,
                                               const nsACString& aFullHash) {
  ClassifierMatchedInfoParams params;
  params.mList = aList;
  params.mProvider = aProvider;
  params.mFullHash = aFullHash;

  mIParentChannelFunctions.AppendElement(
      IParentChannelFunction{VariantIndex<0>{}, std::move(params)});
  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::SetClassifierMatchedTrackingInfo(
    const nsACString& aLists, const nsACString& aFullHash) {
  ClassifierMatchedTrackingInfoParams params;
  params.mLists = aLists;
  params.mFullHashes = aFullHash;

  mIParentChannelFunctions.AppendElement(
      IParentChannelFunction{VariantIndex<1>{}, std::move(params)});
  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::NotifyClassificationFlags(uint32_t aClassificationFlags,
                                                bool aIsThirdParty) {
  mIParentChannelFunctions.AppendElement(IParentChannelFunction{
      VariantIndex<2>{},
      ClassificationFlagsParams{aClassificationFlags, aIsThirdParty}});
  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::Delete() {
  MOZ_ASSERT_UNREACHABLE("This method is unused");
  return NS_OK;
}

NS_IMETHODIMP
DocumentLoadListener::GetRemoteType(nsACString& aRemoteType) {
  // FIXME: The remote type here should be pulled from the remote process used
  // to create this DLL, not from the current `browsingContext`.
  RefPtr<CanonicalBrowsingContext> browsingContext =
      GetDocumentBrowsingContext();
  if (!browsingContext) {
    return NS_ERROR_UNEXPECTED;
  }

  ErrorResult error;
  browsingContext->GetCurrentRemoteType(aRemoteType, error);
  if (error.Failed()) {
    aRemoteType = NOT_REMOTE_TYPE;
  }
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsIChannelEventSink
////////////////////////////////////////////////////////////////////////////////

NS_IMETHODIMP
DocumentLoadListener::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* aCallback) {
  LOG(("DocumentLoadListener::AsyncOnChannelRedirect [this=%p flags=%" PRIu32
       "]",
       this, aFlags));
  // We generally don't want to notify the content process about redirects,
  // so just update our channel and tell the callback that we're good to go.
  mChannel = aNewChannel;

  // We need the original URI of the current channel to use to open the real
  // channel in the content process. Unfortunately we overwrite the original
  // uri of the new channel with the original pre-redirect URI, so grab
  // a copy of it now and save it on the loadInfo corresponding to the
  // new channel.
  nsCOMPtr<nsILoadInfo> loadInfoFromChannel = mChannel->LoadInfo();
  MOZ_ASSERT(loadInfoFromChannel);
  nsCOMPtr<nsIURI> uri;
  mChannel->GetOriginalURI(getter_AddRefs(uri));
  loadInfoFromChannel->SetChannelCreationOriginalURI(uri);

  // Since we're redirecting away from aOldChannel, we should check if it
  // had a COOP mismatch, since we want the final result for this to
  // include the state of all channels we redirected through.
  nsCOMPtr<nsIHttpChannelInternal> httpChannel = do_QueryInterface(aOldChannel);
  if (httpChannel) {
    bool isCOOPMismatch = false;
    (void)NS_WARN_IF(NS_FAILED(
        httpChannel->HasCrossOriginOpenerPolicyMismatch(&isCOOPMismatch)));
    mHasCrossOriginOpenerPolicyMismatch |= isCOOPMismatch;
  }

  // If HTTPS-Only mode is enabled, we need to check whether the exception-flag
  // needs to be removed or set, by asking the PermissionManager.
  nsHTTPSOnlyUtils::TestSitePermissionAndPotentiallyAddExemption(mChannel);

  // We don't need to confirm internal redirects or record any
  // history for them, so just immediately verify and return.
  if (aFlags & nsIChannelEventSink::REDIRECT_INTERNAL) {
    LOG(
        ("DocumentLoadListener AsyncOnChannelRedirect [this=%p] "
         "flags=REDIRECT_INTERNAL",
         this));
    aCallback->OnRedirectVerifyCallback(NS_OK);
    return NS_OK;
  }

  // Cancel cross origin redirects as described by whatwg:
  // > Note: [The early hint reponse] is discarded if it is succeeded by a
  // > cross-origin redirect.
  // https://html.spec.whatwg.org/multipage/semantics.html#early-hints
  nsCOMPtr<nsIURI> oldURI;
  aOldChannel->GetURI(getter_AddRefs(oldURI));
  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  nsresult rv = ssm->CheckSameOriginURI(oldURI, uri, false, false);
  if (NS_FAILED(rv)) {
    mEarlyHintsService.Cancel(
        "DocumentLoadListener::AsyncOnChannelRedirect: cors redirect"_ns);
  }

  if (GetDocumentBrowsingContext()) {
    if (!net::ChannelIsPost(aOldChannel)) {
      AddURIVisit(aOldChannel, 0);
      nsDocShell::SaveLastVisit(aNewChannel, oldURI, aFlags);
    }
  }
  mHaveVisibleRedirect |= true;

  LOG(
      ("DocumentLoadListener AsyncOnChannelRedirect [this=%p] "
       "mHaveVisibleRedirect=%c",
       this, mHaveVisibleRedirect ? 'T' : 'F'));

  // Clear out our nsIParentChannel functions, since a normal parent
  // channel would actually redirect and not have those values on the new one.
  // We expect the URI classifier to run on the redirected channel with
  // the new URI and set these again.
  mIParentChannelFunctions.Clear();

  // If we had a remote type override, ensure it's been cleared after a
  // redirect, as it can't apply anymore.
  mRemoteTypeOverride.reset();

#ifdef ANDROID
  nsCOMPtr<nsIURI> uriBeingLoaded =
      AntiTrackingUtils::MaybeGetDocumentURIBeingLoaded(mChannel);

  RefPtr<MozPromise<bool, bool, false>> promise;
  RefPtr<CanonicalBrowsingContext> bc =
      mParentChannelListener->GetBrowsingContext();
  nsCOMPtr<nsIWidget> widget =
      bc ? bc->GetParentProcessWidgetContaining() : nullptr;
  RefPtr<nsWindow> window = nsWindow::From(widget);

  if (window && !nsExternalHelperAppService::ExternalProtocolIsBlockedBySandbox(
                    bc, false)) {
    promise = window->OnLoadRequest(uriBeingLoaded,
                                    nsIBrowserDOMWindow::OPEN_CURRENTWINDOW,
                                    nsIWebNavigation::LOAD_FLAGS_IS_REDIRECT,
                                    nullptr, false, bc->IsTopContent());
  }

  if (promise) {
    RefPtr<nsIAsyncVerifyRedirectCallback> cb = aCallback;
    promise->Then(
        GetCurrentSerialEventTarget(), __func__,
        [=](const MozPromise<bool, bool, false>::ResolveOrRejectValue& aValue) {
          if (aValue.IsResolve()) {
            bool handled = aValue.ResolveValue();
            if (handled) {
              cb->OnRedirectVerifyCallback(NS_ERROR_ABORT);
            } else {
              cb->OnRedirectVerifyCallback(NS_OK);
            }
          }
        });
  } else
#endif /* ANDROID */
  {
    aCallback->OnRedirectVerifyCallback(NS_OK);
  }
  return NS_OK;
}

nsIURI* DocumentLoadListener::GetChannelCreationURI() const {
  nsCOMPtr<nsILoadInfo> channelLoadInfo = mChannel->LoadInfo();

  nsCOMPtr<nsIURI> uri;
  channelLoadInfo->GetChannelCreationOriginalURI(getter_AddRefs(uri));
  if (uri) {
    // See channelCreationOriginalURI for more info. We use this instead of the
    // originalURI of the channel to help us avoid the situation when we use
    // the URI of a redirect that has failed to happen.
    return uri;
  }

  // Otherwise, get the original URI from the channel.
  mChannel->GetOriginalURI(getter_AddRefs(uri));
  return uri;
}

// This method returns the cached result of running the Cross-Origin-Opener
// policy compare algorithm by calling ComputeCrossOriginOpenerPolicyMismatch
bool DocumentLoadListener::HasCrossOriginOpenerPolicyMismatch() const {
  // If we found a COOP mismatch on an earlier channel and then
  // redirected away from that, we should use that result.
  if (mHasCrossOriginOpenerPolicyMismatch) {
    return true;
  }

  nsCOMPtr<nsIHttpChannelInternal> httpChannel = do_QueryInterface(mChannel);
  if (!httpChannel) {
    // Not an nsIHttpChannelInternal assume it's okay to switch.
    return false;
  }

  bool isCOOPMismatch = false;
  (void)NS_WARN_IF(NS_FAILED(
      httpChannel->HasCrossOriginOpenerPolicyMismatch(&isCOOPMismatch)));
  return isCOOPMismatch;
}

auto DocumentLoadListener::AttachStreamFilter()
    -> RefPtr<ChildEndpointPromise> {
  LOG(("DocumentLoadListener AttachStreamFilter [this=%p]", this));

  StreamFilterRequest* request = mStreamFilterRequests.AppendElement();
  request->mPromise = new ChildEndpointPromise::Private(__func__);
  return request->mPromise;
}

NS_IMETHODIMP DocumentLoadListener::OnProgress(nsIRequest* aRequest,
                                               int64_t aProgress,
                                               int64_t aProgressMax) {
  return NS_OK;
}

NS_IMETHODIMP DocumentLoadListener::OnStatus(nsIRequest* aRequest,
                                             nsresult aStatus,
                                             const char16_t* aStatusArg) {
  nsCOMPtr<nsIChannel> channel = mChannel;

  RefPtr<BrowsingContextWebProgress> webProgress =
      GetLoadingBrowsingContext()->GetWebProgress();

  nsAutoString host;
  host.Append(aStatusArg);

  nsAutoString message;
  nsresult rv = nsDocLoader::FormatStatusMessage(aStatus, host, message, sL10n);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (webProgress) {
    NS_DispatchToMainThread(
        NS_NewRunnableFunction("DocumentLoadListener::OnStatus", [=]() {
          webProgress->OnStatusChange(webProgress, channel, aStatus,
                                      message.get());
        }));
  }
  return NS_OK;
}

NS_IMETHODIMP DocumentLoadListener::EarlyHint(const nsACString& aLinkHeader,
                                              const nsACString& aReferrerPolicy,
                                              const nsACString& aCSPHeader) {
  LOG(("DocumentLoadListener::EarlyHint.\n"));
  RefPtr<DocumentLoadListener> kungFuDeathGrip(this);
  mEarlyHintsService.EarlyHint(aLinkHeader, GetChannelCreationURI(), mChannel,
                               aReferrerPolicy, aCSPHeader,
                               GetLoadingBrowsingContext());
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla

#undef LOG
