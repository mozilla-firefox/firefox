/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for the XML and HTML content sinks, which construct a
 * DOM based on information from the parser.
 */

#include "nsContentSink.h"

#include <stdint.h>

#include "HTMLLinkElement.h"
#include "MediaList.h"
#include "mozilla/Components.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellWidgetListener.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_content.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/css/Loader.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/HTMLDNSPrefetch.h"
#include "mozilla/dom/Link.h"
#include "mozilla/dom/LinkStyle.h"
#include "mozilla/dom/ModuleLoader.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/SRILogHelper.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/dom/SpeculationRules.h"
#include "mozilla/net/HttpBaseChannel.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsAtom.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsGenericHTMLElement.h"
#include "nsGkAtoms.h"
#include "nsGlobalWindowInner.h"
#include "nsIAppShell.h"
#include "nsIContent.h"
#include "nsIContentPolicy.h"
#include "nsICookieService.h"
#include "nsIDocShell.h"
#include "nsIHttpChannel.h"
#include "nsILoadContext.h"
#include "nsIMIMEHeaderParam.h"
#include "nsIObserverService.h"
#include "nsIPrefetchService.h"
#include "nsIProtocolHandler.h"
#include "nsIURI.h"
#include "nsIWebNavigation.h"
#include "nsIWidget.h"
#include "nsLiteralString.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsNodeInfoManager.h"
#include "nsParserConstants.h"
#include "nsPresContext.h"
#include "nsSandboxFlags.h"
#include "nsString.h"
#include "nsStringFwd.h"
#include "nsWidgetsCID.h"
using namespace mozilla;
using namespace mozilla::css;
using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsContentSink)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsContentSink)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsContentSink)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(nsContentSink)
  NS_INTERFACE_MAP_ENTRY(nsICSSLoaderObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsICSSLoaderObserver)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION_CLASS(nsContentSink)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsContentSink)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParser)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocShell)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mNodeInfoManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mScriptLoader)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsContentSink)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParser)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocShell)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mNodeInfoManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScriptLoader)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

nsContentSink::nsContentSink()
    : mLayoutStarted(0),
      mDynamicLowerValue(0),
      mDeferredLayoutStart(0),
      mRunsToCompletion(0),
      mIsBlockingOnload(false),
      mDeflectedCount(0),
      mHasPendingEvent(false),
      mCurrentParseEndTime(0),
      mBeginLoadTime(0),
      mLastSampledUserEventTime(0),
      mPendingSheetCount(0) {
  NS_ASSERTION(!mLayoutStarted, "What?");
  NS_ASSERTION(!mDynamicLowerValue, "What?");
  NS_ASSERTION(mLastSampledUserEventTime == 0, "What?");
  NS_ASSERTION(mDeflectedCount == 0, "What?");
  NS_ASSERTION(!mDeferredLayoutStart, "What?");
}

nsContentSink::~nsContentSink() = default;

nsresult nsContentSink::Init(Document* aDoc, nsIURI* aURI,
                             nsISupports* aContainer, nsIChannel* aChannel) {
  MOZ_ASSERT(aDoc, "null ptr");
  MOZ_ASSERT(aURI, "null ptr");

  if (!aDoc || !aURI) {
    return NS_ERROR_NULL_POINTER;
  }

  mDocument = aDoc;

  mDocumentURI = aURI;
  mDocShell = do_QueryInterface(aContainer);
  mScriptLoader = mDocument->GetScriptLoader();

  if (!mRunsToCompletion) {
    if (mDocShell) {
      uint32_t loadType = 0;
      mDocShell->GetLoadType(&loadType);
      mDocument->SetChangeScrollPosWhenScrollingToRef(
          (loadType & nsIDocShell::LOAD_CMD_HISTORY) == 0);
    }

    ProcessHTTPHeaders(aChannel);
  }

  mNodeInfoManager = aDoc->NodeInfoManager();

  if (StaticPrefs::content_sink_enable_perf_mode() != 0) {
    mDynamicLowerValue = StaticPrefs::content_sink_enable_perf_mode() == 1;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsContentSink::StyleSheetLoaded(StyleSheet* aSheet, bool aWasDeferred,
                                nsresult aStatus) {
  MOZ_ASSERT(!mRunsToCompletion, "How come a fragment parser observed sheets?");
  if (aWasDeferred) {
    return NS_OK;
  }
  MOZ_ASSERT(mPendingSheetCount > 0, "How'd that happen?");
  --mPendingSheetCount;

  const bool loadedAllSheets = !mPendingSheetCount;
  if (loadedAllSheets && mDeferredLayoutStart) {
    // We might not have really started layout, since this sheet was still
    // loading.  Do it now.  Probably doesn't matter whether we do this
    // before or after we unblock scripts, but before feels saner.  Note
    // that if mDeferredLayoutStart is true, that means any subclass
    // StartLayout() stuff that needs to happen has already happened, so
    // we don't need to worry about it.
    // While the document is still render-blocked this bails out, and
    // Document::StartLayoutAfterUnblocking starts layout later.
    StartLayout(false);

    // Go ahead and try to scroll to our ref if we have one
    ScrollToRef();
  }

  if (mScriptLoader) {
    mScriptLoader->RemoveParserBlockingScriptExecutionBlocker();

    if (loadedAllSheets &&
        mDocument->GetReadyStateEnum() >= Document::READYSTATE_INTERACTIVE) {
      const RefPtr<ScriptLoader> scriptLoader = mScriptLoader;
      scriptLoader->DeferCheckpointReached();
    }
  }

  return NS_OK;
}

nsresult nsContentSink::ProcessHTTPHeaders(nsIChannel* aChannel) {
  nsCOMPtr<nsIHttpChannel> httpchannel(do_QueryInterface(aChannel));

  if (!httpchannel) {
    return NS_OK;
  }

  bool gotEarlyHints = false;
  if (nsCOMPtr<mozilla::net::HttpBaseChannel> baseChannel =
          do_QueryInterface(aChannel)) {
    nsTArray<mozilla::net::EarlyHintConnectArgs> earlyHints =
        baseChannel->TakeEarlyHints();
    gotEarlyHints = !earlyHints.IsEmpty();
    mDocument->SetEarlyHints(std::move(earlyHints));
  }

  // Note that the only headers we care about are the "link" header, since we
  // have all the infrastructure for kicking off stylesheet loads, and the
  // "Speculation-Rules" header for top-level documents.

  nsAutoCString linkHeader;

  nsresult rv = httpchannel->GetResponseHeader("link"_ns, linkHeader);
  bool gotLinkHeader = NS_SUCCEEDED(rv) && !linkHeader.IsEmpty();
  if (gotLinkHeader) {
    mDocument->SetHeaderData(nsGkAtoms::link,
                             NS_ConvertASCIItoUTF16(linkHeader));
  }
  if (gotLinkHeader || gotEarlyHints) {
    NS_ASSERTION(!mProcessLinkHeaderEvent.get(),
                 "Already dispatched an event?");

    mProcessLinkHeaderEvent =
        NewNonOwningRunnableMethod("nsContentSink::DoProcessLinkHeader", this,
                                   &nsContentSink::DoProcessLinkHeader);
    rv = NS_DispatchToCurrentThread(mProcessLinkHeaderEvent.get());
    if (NS_FAILED(rv)) {
      mProcessLinkHeaderEvent.Forget();
    }
  }

  // https://html.spec.whatwg.org/#initialise-the-document-object
  // Step 20. If navigationParams's navigable is a top-level traversable, then
  // process the `Speculation-Rules` header given document and
  // navigationParams's response.
  if (StaticPrefs::dom_speculation_rules_enabled() && mDocShell &&
      mDocShell->GetBrowsingContext()->IsTop()) {
    nsAutoCString speculationRulesHeader;
    rv = httpchannel->GetResponseHeader("Speculation-Rules"_ns,
                                        speculationRulesHeader);
    if (NS_SUCCEEDED(rv) && !speculationRulesHeader.IsEmpty()) {
      mDocument->SpeculationRules().ProcessSpeculationRulesHeader(
          speculationRulesHeader);
    }
  }

  return NS_OK;
}

void nsContentSink::DoProcessLinkHeader() {
  for (const auto& earlyHint : mDocument->GetEarlyHints()) {
    ProcessLinkFromHeader(earlyHint.link(), earlyHint.earlyHintPreloaderId());
  }

  nsAutoString value;

  // Getting the header data and parsing the link header together roughly
  // implement <https://httpwg.org/specs/rfc8288.html#parse-set>.
  mDocument->GetHeaderData(nsGkAtoms::link, value);
  auto linkHeaders = net::ParseLinkHeader(value);

  for (const auto& linkHeader : linkHeaders) {
    ProcessLinkFromHeader(linkHeader, 0);
  }
}

nsresult nsContentSink::ProcessLinkFromHeader(const net::LinkHeader& aHeader,
                                              uint64_t aEarlyHintPreloaderId) {
  uint32_t linkTypes = LinkStyle::ParseLinkTypes(aHeader.mRel);

  // The link relation may apply to a different resource, specified
  // in the anchor parameter. For the link relations supported so far,
  // we simply abort if the link applies to a resource different to the
  // one we've loaded
  if (!nsContentUtils::LinkContextIsURI(aHeader.mAnchor,
                                        mDocument->GetDocumentURI())) {
    return NS_OK;
  }

  if (nsContentUtils::PrefetchPreloadEnabled(mDocShell)) {
    // prefetch href if relation is "next" or "prefetch"
    if ((linkTypes & LinkStyle::eNEXT) || (linkTypes & LinkStyle::ePREFETCH)) {
      PrefetchHref(aHeader.mHref, aHeader.mAs, aHeader.mType, aHeader.mMedia);
    }

    if (!aHeader.mHref.IsEmpty() && (linkTypes & LinkStyle::eDNS_PREFETCH)) {
      PrefetchDNS(aHeader.mHref);
    }

    if (!aHeader.mHref.IsEmpty() && (linkTypes & LinkStyle::ePRECONNECT)) {
      Preconnect(aHeader.mHref, aHeader.mCrossOrigin);
    }

    if (linkTypes & LinkStyle::ePRELOAD) {
      PreloadHref(aHeader.mHref, aHeader.mAs, aHeader.mRel, aHeader.mType,
                  aHeader.mMedia, aHeader.mNonce, aHeader.mIntegrity,
                  aHeader.mSrcset, aHeader.mSizes, aHeader.mCrossOrigin,
                  aHeader.mReferrerPolicy, aEarlyHintPreloaderId,
                  aHeader.mFetchPriority);
    }

    if (linkTypes & LinkStyle::eCOMPRESSION_DICTIONARY) {
      PreloadHref(aHeader.mHref, u"fetch"_ns, aHeader.mRel, aHeader.mType,
                  aHeader.mMedia, aHeader.mNonce, aHeader.mIntegrity,
                  aHeader.mSrcset, aHeader.mSizes, aHeader.mCrossOrigin,
                  aHeader.mReferrerPolicy, aEarlyHintPreloaderId,
                  aHeader.mFetchPriority);
    }

    if ((linkTypes & LinkStyle::eMODULE_PRELOAD) &&
        mDocument->GetScriptLoader() &&
        mDocument->GetScriptLoader()->GetModuleLoader()) {
      PreloadModule(aHeader.mHref, aHeader.mAs, aHeader.mMedia, aHeader.mNonce,
                    aHeader.mIntegrity, aHeader.mCrossOrigin,
                    aHeader.mReferrerPolicy, aEarlyHintPreloaderId,
                    aHeader.mFetchPriority);
    }
  }

  // is it a stylesheet link?
  if (!(linkTypes & LinkStyle::eSTYLESHEET)) {
    return NS_OK;
  }

  bool isAlternate = linkTypes & LinkStyle::eALTERNATE;
  return ProcessStyleLinkFromHeader(
      aHeader.mHref, isAlternate, aHeader.mTitle, aHeader.mIntegrity,
      aHeader.mType, aHeader.mMedia, aHeader.mCrossOrigin,
      aHeader.mReferrerPolicy, aHeader.mFetchPriority);
}

nsresult nsContentSink::ProcessStyleLinkFromHeader(
    const nsAString& aHref, bool aAlternate, const nsAString& aTitle,
    const nsAString& aIntegrity, const nsAString& aType,
    const nsAString& aMedia, const nsAString& aCrossOrigin,
    const nsAString& aReferrerPolicy, const nsAString& aFetchPriority) {
  if (aAlternate && aTitle.IsEmpty()) {
    // alternates must have title return without error, for now
    return NS_OK;
  }

  nsAutoString mimeType;
  nsAutoString params;
  nsContentUtils::SplitMimeType(aType, mimeType, params);

  // see bug 18817
  if (!mimeType.IsEmpty() && !mimeType.LowerCaseEqualsLiteral("text/css")) {
    // Unknown stylesheet language
    return NS_OK;
  }

  nsCOMPtr<nsIURI> url;
  nsresult rv = NS_NewURI(getter_AddRefs(url), aHref, nullptr,
                          mDocument->GetDocBaseURI());

  if (NS_FAILED(rv)) {
    // The URI is bad, move along, don't propagate the error (for now)
    return NS_OK;
  }

  // Link header is working like a <link> node, so referrerPolicy attr should
  // have higher priority than referrer policy from document.
  ReferrerPolicy policy =
      ReferrerInfo::ReferrerPolicyAttributeFromString(aReferrerPolicy);
  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      ReferrerInfo::CreateFromDocumentAndPolicyOverride(mDocument, policy);

  const FetchPriority fetchPriority =
      nsGenericHTMLElement::ToFetchPriority(aFetchPriority);

  Loader::SheetInfo info{
      *mDocument,
      nullptr,
      url.forget(),
      nullptr,
      referrerInfo.forget(),
      dom::Element::StringToCORSMode(aCrossOrigin),
      aTitle,
      aMedia,
      aIntegrity,
      /* nonce = */ u""_ns,
      aAlternate ? Loader::HasAlternateRel::Yes : Loader::HasAlternateRel::No,
      Loader::IsInline::No,
      Loader::IsExplicitlyEnabled::No,
      fetchPriority,
  };

  auto loadResultOrErr = mDocument->EnsureCSSLoader().LoadStyleLink(
      info, mRunsToCompletion ? nullptr : this);
  if (loadResultOrErr.isErr()) {
    return loadResultOrErr.unwrapErr();
  }

  if (loadResultOrErr.inspect().ShouldBlock() && !mRunsToCompletion) {
    ++mPendingSheetCount;
    if (mScriptLoader) {
      mScriptLoader->AddParserBlockingScriptExecutionBlocker();
    }
  }

  return NS_OK;
}

void nsContentSink::PrefetchHref(const nsAString& aHref, const nsAString& aAs,
                                 const nsAString& aType,
                                 const nsAString& aMedia) {
  nsCOMPtr<nsIPrefetchService> prefetchService(components::Prefetch::Service());
  if (prefetchService) {
    // construct URI using document charset
    auto encoding = mDocument->GetDocumentCharacterSet();
    nsCOMPtr<nsIURI> uri;
    NS_NewURI(getter_AddRefs(uri), aHref, encoding, mDocument->GetDocBaseURI());
    if (uri) {
      auto referrerInfo = MakeRefPtr<ReferrerInfo>(*mDocument);
      referrerInfo = referrerInfo->CloneWithNewOriginalReferrer(mDocumentURI);

      prefetchService->PrefetchURI(uri, referrerInfo, mDocument, true);
    }
  }
}

void nsContentSink::PreloadHref(
    const nsAString& aHref, const nsAString& aAs, const nsAString& aRel,
    const nsAString& aType, const nsAString& aMedia, const nsAString& aNonce,
    const nsAString& aIntegrity, const nsAString& aSrcset,
    const nsAString& aSizes, const nsAString& aCORS,
    const nsAString& aReferrerPolicy, uint64_t aEarlyHintPreloaderId,
    const nsAString& aFetchPriority) {
  auto encoding = mDocument->GetDocumentCharacterSet();
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aHref, encoding, mDocument->GetDocBaseURI());
  if (!uri) {
    if (!aAs.LowerCaseEqualsASCII("image") || aSrcset.IsEmpty()) {
      // URL parsing failed.
      return;
    }
  }

  nsAttrValue asAttr;
  mozilla::net::ParseAsValue(aAs, asAttr);

  nsAutoString mimeType;
  nsAutoString notUsed;
  nsContentUtils::SplitMimeType(aType, mimeType, notUsed);

  auto policyType = mozilla::net::AsValueToContentPolicy(asAttr);
  if (policyType == nsIContentPolicy::TYPE_INVALID ||
      !mozilla::net::CheckPreloadAttrs(asAttr, mimeType, aMedia, mDocument)) {
    // Ignore preload wrong or empty attributes.
    mozilla::net::WarnIgnoredPreload(*mDocument, uri, aSrcset);
    return;
  }

  mDocument->Preloads().PreloadLinkHeader(
      uri, aHref, policyType, aAs, aRel, aType, aNonce, aIntegrity, aSrcset,
      aSizes, aCORS, aReferrerPolicy, aEarlyHintPreloaderId, aFetchPriority);
}

void nsContentSink::PreloadModule(
    const nsAString& aHref, const nsAString& aAs, const nsAString& aMedia,
    const nsAString& aNonce, const nsAString& aIntegrity,
    const nsAString& aCORS, const nsAString& aReferrerPolicy,
    uint64_t aEarlyHintPreloaderId, const nsAString& aFetchPriority) {
  dom::ScriptLoader* scriptLoader = mDocument->GetScriptLoader();
  if (!scriptLoader) {
    return;
  }
  ModuleLoader* moduleLoader = scriptLoader->GetModuleLoader();

  if (!StaticPrefs::network_modulepreload() &&
      !StaticPrefs::dom_multiple_import_maps_enabled()) {
    // Keep behavior from https://phabricator.services.mozilla.com/D149371,
    // prior to main implementation of modulepreload
    moduleLoader->DisallowImportMaps();
    return;
  }

  RefPtr<mozilla::dom::MediaList> mediaList =
      mozilla::dom::MediaList::Create(NS_ConvertUTF16toUTF8(aMedia));
  if (!mediaList->Matches(*mDocument)) {
    return;
  }

  if (aHref.IsEmpty()) {
    return;
  }

  if (!net::IsScriptLikeOrInvalid(aAs)) {
    return;
  }

  auto encoding = mDocument->GetDocumentCharacterSet();
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aHref, encoding, mDocument->GetDocBaseURI());
  if (!uri) {
    return;
  }

  if (!StaticPrefs::dom_multiple_import_maps_enabled()) {
    moduleLoader->DisallowImportMaps();
  }

  mDocument->Preloads().PreloadLinkHeader(
      uri, aHref, nsIContentPolicy::TYPE_SCRIPT, u"script"_ns,
      u"modulepreload"_ns, u"module"_ns, aNonce, aIntegrity, u""_ns, u""_ns,
      aCORS, aReferrerPolicy, aEarlyHintPreloaderId, aFetchPriority);
}

void nsContentSink::PrefetchDNS(const nsAString& aHref) {
  if (!HTMLDNSPrefetch::IsAllowed(mDocument)) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aHref, mDocument->GetDocumentCharacterSet(),
            mDocument->GetDocBaseURI());
  if (!uri) {
    return;
  }

  bool isLocalResource = false;
  nsresult rv = NS_URIChainHasFlags(
      uri, nsIProtocolHandler::URI_IS_LOCAL_RESOURCE, &isLocalResource);
  if (NS_FAILED(rv) || isLocalResource) {
    return;
  }

  nsAutoCString hostname;
  uri->GetAsciiHost(hostname);

  if (!hostname.IsEmpty()) {
    OriginAttributes oa;
    StoragePrincipalHelper::GetOriginAttributesForNetworkState(mDocument, oa);

    HTMLDNSPrefetch::Prefetch(hostname, oa,
                              mDocument->GetChannel()->GetTRRMode(),
                              HTMLDNSPrefetch::Priority::Low);
  }
}

void nsContentSink::Preconnect(const nsAString& aHref,
                               const nsAString& aCrossOrigin) {
  // construct URI using document charset
  auto encoding = mDocument->GetDocumentCharacterSet();
  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), aHref, encoding, mDocument->GetDocBaseURI());

  if (uri && mDocument) {
    mDocument->MaybePreconnect(uri,
                               dom::Element::StringToCORSMode(aCrossOrigin));
  }
}

void nsContentSink::ScrollToRef() {
  RefPtr<Document> document = mDocument;
  document->ScrollToRef();
}

void nsContentSink::StartLayout(bool aIgnorePendingSheets) {
  if (mLayoutStarted) {
    // Nothing to do here
    return;
  }

  mDeferredLayoutStart = true;

  if (!aIgnorePendingSheets &&
      (WaitForPendingSheets() || mDocument->HasPendingInitialTranslation() ||
       mDocument->IsRenderBlocked())) {
    // Bail out; we'll start layout when the sheets and l10n load, or when
    // render-blocking elements finish loading
    return;
  }

  AUTO_PROFILER_LABEL_DYNAMIC_NSCSTRING_RELEVANT_FOR_JS(
      "Layout", LAYOUT, mDocumentURI->GetSpecOrDefault());

  mDeferredLayoutStart = false;

  if (aIgnorePendingSheets) {
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "Layout"_ns, mDocument,
        PropertiesFile::LAYOUT_PROPERTIES, "ForcedLayoutStart");
  }

  mLayoutStarted = true;

  mDocument->SetMayStartLayout(true);
  RefPtr<PresShell> presShell = mDocument->GetPresShell();
  // Make sure we don't call Initialize() for a shell that has
  // already called it. This can happen when the layout frame for
  // an iframe is constructed *between* the Embed() call for the
  // docshell in the iframe, and the content sink's call to OpenBody().
  // (Bug 153815)
  if (presShell && !presShell->DidInitialize()) {
    nsresult rv = presShell->Initialize();
    if (NS_FAILED(rv)) {
      return;
    }
  }

  // If the document we are loading has a reference or it is a
  // frameset document, disable the scroll bars on the views.

  mDocument->SetScrollToRef(mDocument->GetDocumentURI());
}

nsresult nsContentSink::DidProcessATokenImpl() {
  if (mRunsToCompletion || !mParser) {
    return NS_OK;
  }

  // Get the current user event time
  PresShell* presShell = mDocument->GetPresShell();
  if (!presShell) {
    // If there's no pres shell in the document, return early since
    // we're not laying anything out here.
    return NS_OK;
  }

  // Increase before comparing to gEventProbeRate
  ++mDeflectedCount;

  // Check if there's a pending event
  if (StaticPrefs::content_sink_pending_event_mode() != 0 &&
      !mHasPendingEvent &&
      (mDeflectedCount % StaticPrefs::content_sink_event_probe_rate()) == 0) {
    nsIWidget* widget = presShell->GetRootWidget();
    mHasPendingEvent = widget && widget->HasPendingInputEvent();
  }

  if (mHasPendingEvent && StaticPrefs::content_sink_pending_event_mode() == 2) {
    return NS_ERROR_HTMLPARSER_INTERRUPTED;
  }

  // Have we processed enough tokens to check time?
  if (!mHasPendingEvent &&
      mDeflectedCount <
          uint32_t(mDynamicLowerValue
                       ? StaticPrefs::content_sink_interactive_deflect_count()
                       : StaticPrefs::content_sink_perf_deflect_count())) {
    return NS_OK;
  }

  mDeflectedCount = 0;

  // Check if it's time to return to the main event loop
  if (PR_IntervalToMicroseconds(PR_IntervalNow()) > mCurrentParseEndTime) {
    return NS_ERROR_HTMLPARSER_INTERRUPTED;
  }

  return NS_OK;
}

void nsContentSink::DidBuildModelImpl(bool aTerminated) {
  MOZ_ASSERT(aTerminated || (mParser && mParser->IsParserClosed()) ||
                 mDocument->GetReadyStateEnum() == Document::READYSTATE_LOADING,
             "Bad readyState");
  mDocument->SetReadyStateInternal(Document::READYSTATE_INTERACTIVE);

  if (const RefPtr<ScriptLoader> scriptLoader = mScriptLoader) {
    scriptLoader->ParsingComplete(aTerminated);
    if (!mPendingSheetCount) {
      scriptLoader->DeferCheckpointReached();
    }
  }

  if (!mDocument->HaveFiredDOMTitleChange()) {
    mDocument->NotifyPossibleTitleChange(false);
  }
}

void nsContentSink::DropParserAndPerfHint(void) {
  if (!mParser) {
    // Make sure we don't unblock unload too many times
    return;
  }

  // Ref. Bug 49115
  // Do this hack to make sure that the parser
  // doesn't get destroyed, accidently, before
  // the circularity, between sink & parser, is
  // actually broken.
  // Drop our reference to the parser to get rid of a circular
  // reference.
  RefPtr<nsParserBase> kungFuDeathGrip = std::move(mParser);
  (void)kungFuDeathGrip;

  // Call UnblockOnload only if mRunsToComletion is false and if
  // we have already started loading because it's possible that this function
  // is called (i.e. the parser is terminated) before we start loading due to
  // destroying the window inside unload event callbacks for the previous
  // document.
  if (!mRunsToCompletion && mIsBlockingOnload) {
    mDocument->UnblockOnload(true);
    mIsBlockingOnload = false;
  }
}

bool nsContentSink::IsScriptExecutingImpl() {
  return mScriptLoader && mScriptLoader->GetCurrentScript();
}

void nsContentSink::ContinueParsingDocumentAfterCurrentScriptImpl() {
  if (mScriptLoader) {
    mScriptLoader->ContinueParsingDocumentAfterCurrentScript();
  }
}

nsresult nsContentSink::WillParseImpl(void) {
  if (mRunsToCompletion || !mDocument) {
    return NS_OK;
  }

  PresShell* presShell = mDocument->GetPresShell();
  if (!presShell) {
    return NS_OK;
  }

  uint32_t currentTime = PR_IntervalToMicroseconds(PR_IntervalNow());

  if (StaticPrefs::content_sink_enable_perf_mode() == 0) {
    uint32_t lastEventTime = PresShellWidgetListener::GetLastUserEventTime();
    bool newDynLower = mDocument->IsInBackgroundWindow() ||
                       ((currentTime - mBeginLoadTime) >
                            StaticPrefs::content_sink_initial_perf_time() &&
                        (currentTime - lastEventTime) <
                            StaticPrefs::content_sink_interactive_time());

    if (mDynamicLowerValue != newDynLower) {
      mDynamicLowerValue = newDynLower;
    }
  }

  mDeflectedCount = 0;
  mHasPendingEvent = false;

  mCurrentParseEndTime =
      currentTime + (mDynamicLowerValue
                         ? StaticPrefs::content_sink_interactive_parse_time()
                         : StaticPrefs::content_sink_perf_parse_time());

  return NS_OK;
}

void nsContentSink::WillBuildModelImpl() {
  if (!mRunsToCompletion) {
    mDocument->BlockOnload();
    mIsBlockingOnload = true;

    mBeginLoadTime = PR_IntervalToMicroseconds(PR_IntervalNow());
  }

  mDocument->ResetScrolledToRefAlready();

  if (mProcessLinkHeaderEvent.get()) {
    mProcessLinkHeaderEvent.Revoke();

    DoProcessLinkHeader();
  }
}

/* static */
void nsContentSink::NotifyDocElementCreated(Document* aDoc) {
  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  // This process just created a document. Ensure it's been marked as untrusted.
  mozilla::dom::ContentChild::MaybeBecomeUntrusted();

  // Data documents with a content principal (DOMParser, XHR responseXML,
  // createDocument) are never displayed, cannot run scripts and have no
  // window. None of the observers of these notifications act on them, so
  // skip the notification overhead. Only CustomElementListener with
  // SystemPrincipals actually make use of this.
  if (aDoc->IsLoadedAsData() && !aDoc->NodePrincipal()->IsSystemPrincipal()) {
    return;
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  MOZ_ASSERT(observerService);

  auto* win = nsGlobalWindowInner::Cast(aDoc->GetInnerWindow());
  bool fireInitialInsertion = !win || !win->DidFireDocElemInserted();
  if (win) {
    win->SetDidFireDocElemInserted();
  }
  if (fireInitialInsertion) {
    observerService->NotifyObservers(ToSupports(aDoc),
                                     "initial-document-element-inserted", u"");
  }
  observerService->NotifyObservers(ToSupports(aDoc),
                                   "document-element-inserted", u"");

  nsContentUtils::DispatchChromeEvent(aDoc, aDoc, u"DOMDocElementInserted"_ns,
                                      CanBubble::eYes, Cancelable::eNo);
}
