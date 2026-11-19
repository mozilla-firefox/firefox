/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/*
 * A base class implementing nsIObjectLoadingContent for use by
 * various content nodes that want to provide plugin/document/image
 * loading functionality (eg <embed>, <object>, etc).
 */

// Interface headers
#include "imgLoader.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Document.h"
#include "nsError.h"
#include "nsIAppShell.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIClassOfService.h"
#include "nsIConsoleService.h"
#include "nsIDocShell.h"
#include "nsIExternalProtocolHandler.h"
#include "nsIHttpChannel.h"
#include "nsINestedURI.h"
#include "nsIPermissionManager.h"
#include "nsIScriptChannel.h"
#include "nsIScriptError.h"
#include "nsIURILoader.h"
#include "nsScriptSecurityManager.h"
#include "nsSubDocumentFrame.h"

// Util headers
#include "mozilla/Logging.h"
#include "mozilla/Preferences.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsDocShellLoadState.h"
#include "nsGkAtoms.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsStyleUtil.h"
#include "nsThreadUtils.h"

// Concrete classes
#include "ReferrerInfo.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/Components.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/HTMLEmbedElement.h"
#include "mozilla/dom/HTMLObjectElement.h"
#include "mozilla/dom/HTMLObjectElementBinding.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "mozilla/net/DocumentChannel.h"
#include "mozilla/widget/IMEData.h"
#include "nsChannelClassifier.h"
#include "nsFocusManager.h"
#include "nsFrameLoader.h"
#include "nsIEffectiveTLDService.h"
#include "nsObjectLoadingContent.h"
#include "nsWidgetsCID.h"

#ifdef XP_WIN
// Thanks so much, Microsoft! :(
#  ifdef CreateEvent
#    undef CreateEvent
#  endif
#endif  // XP_WIN

static const char kPrefYoutubeRewrite[] = "plugins.rewrite_youtube_embeds";

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::net;

static LogModule* GetObjectLog() {
  static LazyLogModule sLog("objlc");
  return sLog;
}

#define LOG(args) MOZ_LOG(GetObjectLog(), mozilla::LogLevel::Debug, args)
#define LOG_ENABLED() MOZ_LOG_TEST(GetObjectLog(), mozilla::LogLevel::Debug)

static bool IsFlashMIME(const nsACString& aMIMEType) {
  return aMIMEType.LowerCaseEqualsASCII("application/x-shockwave-flash") ||
         aMIMEType.LowerCaseEqualsASCII("application/futuresplash") ||
         aMIMEType.LowerCaseEqualsASCII("application/x-shockwave-flash-test");
}

static bool IsPluginMIME(const nsACString& aMIMEType) {
  return IsFlashMIME(aMIMEType) ||
         aMIMEType.LowerCaseEqualsASCII("application/x-test");
}

///
/// Runnables and helper classes
///

// Sets a object's mIsLoading bit to false when destroyed
class AutoSetLoadingToFalse {
 public:
  explicit AutoSetLoadingToFalse(nsObjectLoadingContent* aContent)
      : mContent(aContent) {}
  ~AutoSetLoadingToFalse() { mContent->mIsLoading = false; }

 private:
  nsObjectLoadingContent* mContent;
};

///
/// Helper functions
///

bool nsObjectLoadingContent::IsSuccessfulRequest(nsIRequest* aRequest,
                                                 nsresult* aStatus) {
  nsresult rv = aRequest->GetStatus(aStatus);
  if (NS_FAILED(rv) || NS_FAILED(*aStatus)) {
    return false;
  }

  // This may still be an error page or somesuch
  nsCOMPtr<nsIHttpChannel> httpChan(do_QueryInterface(aRequest));
  if (httpChan) {
    bool success;
    rv = httpChan->GetRequestSucceeded(&success);
    if (NS_FAILED(rv) || !success) {
      return false;
    }
  }

  // Otherwise, the request is successful
  return true;
}

static bool CanHandleURI(nsIURI* aURI) {
  nsAutoCString scheme;
  if (NS_FAILED(aURI->GetScheme(scheme))) {
    return false;
  }

  nsCOMPtr<nsIIOService> ios = mozilla::components::IO::Service();
  if (!ios) {
    return false;
  }

  nsCOMPtr<nsIProtocolHandler> handler;
  ios->GetProtocolHandler(scheme.get(), getter_AddRefs(handler));
  if (!handler) {
    return false;
  }

  nsCOMPtr<nsIExternalProtocolHandler> extHandler = do_QueryInterface(handler);
  // We can handle this URI if its protocol handler is not the external one
  return extHandler == nullptr;
}

// Helper for tedious URI equality syntax when one or both arguments may be
// null and URIEquals(null, null) should be true
static bool inline URIEquals(nsIURI* a, nsIURI* b) {
  bool equal;
  return (!a && !b) || (a && b && NS_SUCCEEDED(a->Equals(b, &equal)) && equal);
}

///
/// Member Functions
///

// Helper to spawn the frameloader.
void nsObjectLoadingContent::SetupFrameLoader() {
  mFrameLoader = nsFrameLoader::Create(AsElement(), mNetworkCreated);
  MOZ_ASSERT(mFrameLoader, "nsFrameLoader::Create failed");
}

// Helper to spawn the frameloader and return a pointer to its docshell.
already_AddRefed<nsIDocShell> nsObjectLoadingContent::SetupDocShell(
    nsIURI* aRecursionCheckURI) {
  SetupFrameLoader();
  if (!mFrameLoader) {
    return nullptr;
  }

  nsCOMPtr<nsIDocShell> docShell;

  if (aRecursionCheckURI) {
    nsresult rv = mFrameLoader->CheckForRecursiveLoad(aRecursionCheckURI);
    if (NS_SUCCEEDED(rv)) {
      IgnoredErrorResult result;
      docShell = mFrameLoader->GetDocShell(result);
      if (result.Failed()) {
        MOZ_ASSERT_UNREACHABLE("Could not get DocShell from mFrameLoader?");
      }
    } else {
      LOG(("OBJLC [%p]: Aborting recursive load", this));
    }
  }

  if (!docShell) {
    RefPtr<nsFrameLoader> loader = std::move(mFrameLoader);
    loader->Destroy();
    return nullptr;
  }

  return docShell.forget();
}

void nsObjectLoadingContent::UnbindFromTree() {
  // Reset state and clear pending events
  /// XXX(johns): The implementation for GenericFrame notes that ideally we
  ///             would keep the docshell around, but trash the frameloader
  UnloadObject();
}

nsObjectLoadingContent::nsObjectLoadingContent()
    : mType(ObjectType::Loading),
      mChannelLoaded(false),
      mNetworkCreated(true),
      mIsStopping(false),
      mIsLoading(false),
      mScriptRequested(false),
      mRewrittenYoutubeEmbed(false) {}

nsObjectLoadingContent::~nsObjectLoadingContent() {
  // Should have been unbound from the tree at this point, and
  // CheckPluginStopEvent keeps us alive
  if (mFrameLoader) {
    MOZ_ASSERT_UNREACHABLE(
        "Should not be tearing down frame loaders at this point");
    mFrameLoader->Destroy();
  }
}

// nsIRequestObserver
NS_IMETHODIMP
nsObjectLoadingContent::OnStartRequest(nsIRequest* aRequest) {
  AUTO_PROFILER_LABEL("nsObjectLoadingContent::OnStartRequest", NETWORK);

  LOG(("OBJLC [%p]: Channel OnStartRequest", this));

  if (aRequest != mChannel || !aRequest) {
    // happens when a new load starts before the previous one got here
    return NS_BINDING_ABORTED;
  }

  nsCOMPtr<nsIChannel> chan(do_QueryInterface(aRequest));
  NS_ASSERTION(chan, "Why is our request not a channel?");

  nsresult status = NS_OK;
  bool success = IsSuccessfulRequest(aRequest, &status);

  // If we have already switched to type document, we're doing a
  // process-switching DocumentChannel load. We should be able to pass down the
  // load to our inner listener, but should also make sure to update our local
  // state.
  if (mType == ObjectType::Document) {
    if (!mFinalListener) {
      MOZ_ASSERT_UNREACHABLE(
          "Already is Document, but don't have final listener yet?");
      return NS_BINDING_ABORTED;
    }

    // If the load looks successful, fix up some of our local state before
    // forwarding the request to the final URI loader.
    //
    // Forward load errors down to the document loader, so we don't tear down
    // the nsDocShell ourselves.
    if (success) {
      LOG(("OBJLC [%p]: OnStartRequest: DocumentChannel request succeeded\n",
           this));
      nsCString channelType;
      MOZ_ALWAYS_SUCCEEDS(mChannel->GetContentType(channelType));

      if (GetTypeOfContent(channelType) != ObjectType::Document) {
        MOZ_CRASH("DocumentChannel request with non-document MIME");
      }
      mContentType = channelType;

      MOZ_ALWAYS_SUCCEEDS(
          NS_GetFinalChannelURI(mChannel, getter_AddRefs(mURI)));
    }

    nsCOMPtr<nsIStreamListener> listener = mFinalListener;
    return listener->OnStartRequest(aRequest);
  }

  // Otherwise we should be state loading, and call LoadObject with the channel
  if (mType != ObjectType::Loading) {
    MOZ_ASSERT_UNREACHABLE("Should be type loading at this point");
    return NS_BINDING_ABORTED;
  }
  NS_ASSERTION(!mChannelLoaded, "mChannelLoaded set already?");
  NS_ASSERTION(!mFinalListener, "mFinalListener exists already?");

  mChannelLoaded = true;

  if (status == NS_ERROR_BLOCKED_URI) {
    nsCOMPtr<nsIConsoleService> console(
        do_GetService("@mozilla.org/consoleservice;1"));
    if (console) {
      nsCOMPtr<nsIURI> uri;
      chan->GetURI(getter_AddRefs(uri));
      nsString message =
          u"Blocking "_ns +
          NS_ConvertASCIItoUTF16(uri->GetSpecOrDefault().get()) +
          nsLiteralString(
              u" since it was found on an internal Firefox blocklist.");
      console->LogStringMessage(message.get());
    }
    return NS_ERROR_FAILURE;
  }

  if (ChannelClassifierUtils::IsClassifierBlockingErrorCode(status)) {
    return NS_ERROR_FAILURE;
  }

  if (!success) {
    LOG(("OBJLC [%p]: OnStartRequest: Request failed\n", this));
    // If the request fails, we still call LoadObject() to handle fallback
    // content and notifying of failure. (mChannelLoaded && !mChannel) indicates
    // the bad state.
    mChannel = nullptr;
    LoadObject(true, false);
    return NS_ERROR_FAILURE;
  }

  return LoadObject(true, false, aRequest);
}

NS_IMETHODIMP
nsObjectLoadingContent::OnStopRequest(nsIRequest* aRequest,
                                      nsresult aStatusCode) {
  AUTO_PROFILER_LABEL("nsObjectLoadingContent::OnStopRequest", NETWORK);

  // Handle object not loading error because source was a tracking URL (or
  // fingerprinting, cryptomining, etc.).
  // We make a note of this object node by including it in a dedicated
  // array of blocked tracking nodes under its parent document.
  if (ChannelClassifierUtils::IsClassifierBlockingErrorCode(aStatusCode)) {
    nsCOMPtr<nsIContent> thisNode =
        do_QueryInterface(static_cast<nsIObjectLoadingContent*>(this));
    if (thisNode && thisNode->IsInComposedDoc()) {
      thisNode->GetComposedDoc()->AddBlockedNodeByClassifier(thisNode);
    }
  }

  if (aRequest != mChannel) {
    return NS_BINDING_ABORTED;
  }

  mChannel = nullptr;

  if (mFinalListener) {
    // This may re-enter in the case of plugin listeners
    nsCOMPtr<nsIStreamListener> listenerGrip(mFinalListener);
    mFinalListener = nullptr;
    listenerGrip->OnStopRequest(aRequest, aStatusCode);
  }

  // Return value doesn't matter
  return NS_OK;
}

// nsIStreamListener
NS_IMETHODIMP
nsObjectLoadingContent::OnDataAvailable(nsIRequest* aRequest,
                                        nsIInputStream* aInputStream,
                                        uint64_t aOffset, uint32_t aCount) {
  if (aRequest != mChannel) {
    return NS_BINDING_ABORTED;
  }

  if (mFinalListener) {
    // This may re-enter in the case of plugin listeners
    nsCOMPtr<nsIStreamListener> listenerGrip(mFinalListener);
    return listenerGrip->OnDataAvailable(aRequest, aInputStream, aOffset,
                                         aCount);
  }

  // We shouldn't have a connected channel with no final listener
  MOZ_ASSERT_UNREACHABLE(
      "Got data for channel with no connected final "
      "listener");
  mChannel = nullptr;

  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsObjectLoadingContent::GetActualType(nsACString& aType) {
  aType = mContentType;
  return NS_OK;
}

NS_IMETHODIMP
nsObjectLoadingContent::GetDisplayedType(uint32_t* aType) {
  *aType = DisplayedType();
  return NS_OK;
}

// nsIInterfaceRequestor
// We use a shim class to implement this so that JS consumers still
// see an interface requestor even though WebIDL bindings don't expose
// that stuff.
class ObjectInterfaceRequestorShim final : public nsIInterfaceRequestor,
                                           public nsIChannelEventSink,
                                           public nsIStreamListener {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(ObjectInterfaceRequestorShim,
                                           nsIInterfaceRequestor)
  NS_DECL_NSIINTERFACEREQUESTOR
  // RefPtr<nsObjectLoadingContent> fails due to ambiguous AddRef/Release,
  // hence the ugly static cast :(
  NS_FORWARD_NSICHANNELEVENTSINK(
      static_cast<nsObjectLoadingContent*>(mContent.get())->)
  NS_FORWARD_NSISTREAMLISTENER(
      static_cast<nsObjectLoadingContent*>(mContent.get())->)
  NS_FORWARD_NSIREQUESTOBSERVER(
      static_cast<nsObjectLoadingContent*>(mContent.get())->)

  explicit ObjectInterfaceRequestorShim(nsIObjectLoadingContent* aContent)
      : mContent(aContent) {}

 protected:
  ~ObjectInterfaceRequestorShim() = default;
  nsCOMPtr<nsIObjectLoadingContent> mContent;
};

NS_IMPL_CYCLE_COLLECTION(ObjectInterfaceRequestorShim, mContent)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ObjectInterfaceRequestorShim)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIChannelEventSink)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIInterfaceRequestor)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(ObjectInterfaceRequestorShim)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ObjectInterfaceRequestorShim)

NS_IMETHODIMP
ObjectInterfaceRequestorShim::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    nsIChannelEventSink* sink = this;
    *aResult = sink;
    NS_ADDREF(sink);
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIObjectLoadingContent))) {
    nsIObjectLoadingContent* olc = mContent;
    *aResult = olc;
    NS_ADDREF(olc);
    return NS_OK;
  }
  return NS_NOINTERFACE;
}

// nsIChannelEventSink
NS_IMETHODIMP
nsObjectLoadingContent::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* cb) {
  // If we're already busy with a new load, or have no load at all,
  // cancel the redirect.
  if (!mChannel || aOldChannel != mChannel) {
    return NS_BINDING_ABORTED;
  }

  mChannel = aNewChannel;

  if (mFinalListener) {
    nsCOMPtr<nsIChannelEventSink> sink(do_QueryInterface(mFinalListener));
    MOZ_RELEASE_ASSERT(sink, "mFinalListener isn't nsIChannelEventSink?");
    if (mType != ObjectType::Document) {
      MOZ_ASSERT_UNREACHABLE(
          "Not a DocumentChannel load, but we're getting a "
          "AsyncOnChannelRedirect with a mFinalListener?");
      return NS_BINDING_ABORTED;
    }

    return sink->AsyncOnChannelRedirect(aOldChannel, aNewChannel, aFlags, cb);
  }

  cb->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

void nsObjectLoadingContent::MaybeRewriteYoutubeEmbed(nsIURI* aURI,
                                                      nsIURI* aBaseURI,
                                                      nsIURI** aRewrittenURI) {
  nsCOMPtr<nsIEffectiveTLDService> tldService =
      do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
  // If we can't analyze the URL, just pass on through.
  if (!tldService) {
    NS_WARNING("Could not get TLD service!");
    return;
  }

  nsAutoCString currentBaseDomain;
  bool ok = NS_SUCCEEDED(tldService->GetBaseDomain(aURI, 0, currentBaseDomain));
  if (!ok) {
    // Data URIs (commonly used for things like svg embeds) won't parse
    // correctly, so just fail silently here.
    return;
  }

  // See if URL is referencing youtube
  if (!currentBaseDomain.EqualsLiteral("youtube.com") &&
      !currentBaseDomain.EqualsLiteral("youtube-nocookie.com")) {
    return;
  }

  // We should only rewrite URLs with paths starting with "/v/", as we shouldn't
  // touch object nodes with "/embed/" urls that already do that right thing.
  nsAutoCString path;
  aURI->GetPathQueryRef(path);
  if (!StringBeginsWith(path, "/v/"_ns)) {
    return;
  }

  // See if requester is planning on using the JS API.
  nsAutoCString prePath;
  nsresult rv = aURI->GetPrePath(prePath);
  if (NS_FAILED(rv)) {
    return;
  }

  // Some YouTube urls have parameters in path components, e.g.
  // http://youtube.com/embed/7LcUOEP7Brc&start=35. These URLs work with flash,
  // but break iframe/object embedding. If this situation occurs with rewritten
  // URLs, convert the parameters to query in order to make the video load
  // correctly as an iframe. In either case, warn about it in the
  // developer console.
  int32_t ampIndex = path.FindChar('&', 0);
  bool replaceQuery = false;
  if (ampIndex != -1) {
    int32_t qmIndex = path.FindChar('?', 0);
    if (qmIndex == -1 || qmIndex > ampIndex) {
      replaceQuery = true;
    }
  }

  Document* doc = AsElement()->OwnerDoc();
  // If we've made it this far, we've got a rewritable embed. Log it in
  // telemetry.
  doc->SetUseCounter(eUseCounter_custom_YouTubeFlashEmbed);

  // If we're pref'd off, return after telemetry has been logged.
  if (!Preferences::GetBool(kPrefYoutubeRewrite)) {
    return;
  }

  NS_ConvertUTF8toUTF16 utf16OldURI(prePath);
  AppendUTF8toUTF16(path, utf16OldURI);
  // If we need to convert the URL, it means an ampersand comes first.
  // Use the index we found earlier.
  if (replaceQuery) {
    // Replace question marks with ampersands.
    path.ReplaceChar('?', '&');
    // Replace the first ampersand with a question mark.
    path.SetCharAt('?', ampIndex);
  }
  // Switch out video access url formats, which should possibly allow HTML5
  // video loading.
  path.ReplaceSubstring("/v/"_ns, "/embed/"_ns);
  NS_ConvertUTF8toUTF16 utf16URI(prePath);
  AppendUTF8toUTF16(path, utf16URI);
  rv = nsContentUtils::NewURIWithDocumentCharset(aRewrittenURI, utf16URI, doc,
                                                 aBaseURI);
  if (NS_FAILED(rv)) {
    return;
  }
  AutoTArray<nsString, 2> params = {std::move(utf16OldURI),
                                    std::move(utf16URI)};
  const char* msgName;
  // If there's no query to rewrite, just notify in the developer console
  // that we're changing the embed.
  if (!replaceQuery) {
    msgName = "RewriteYouTubeEmbed";
  } else {
    msgName = "RewriteYouTubeEmbedPathParams";
  }
  nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "Plugins"_ns,
                                  doc, PropertiesFile::DOM_PROPERTIES, msgName,
                                  params);
}

bool nsObjectLoadingContent::CheckLoadPolicy(int16_t* aContentPolicy) {
  if (!aContentPolicy || !mURI) {
    MOZ_ASSERT_UNREACHABLE("Doing it wrong");
    return false;
  }

  Element* el = AsElement();
  Document* doc = el->OwnerDoc();

  nsContentPolicyType contentPolicyType = GetContentPolicyType();

  Result<RefPtr<LoadInfo>, nsresult> maybeLoadInfo =
      LoadInfo::Create(doc->NodePrincipal(),  // loading principal
                       doc->NodePrincipal(),  // triggering principal
                       el, nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
                       contentPolicyType);
  if (NS_WARN_IF(maybeLoadInfo.isErr())) {
    return false;
  }
  RefPtr<LoadInfo> secCheckLoadInfo = maybeLoadInfo.unwrap();

  *aContentPolicy = nsIContentPolicy::ACCEPT;
  nsresult rv =
      NS_CheckContentLoadPolicy(mURI, secCheckLoadInfo, aContentPolicy,
                                nsContentUtils::GetContentPolicy());
  NS_ENSURE_SUCCESS(rv, false);
  if (NS_CP_REJECTED(*aContentPolicy)) {
    LOG(("OBJLC [%p]: Content policy denied load of %s", this,
         mURI->GetSpecOrDefault().get()));
    return false;
  }

  return true;
}

bool nsObjectLoadingContent::CheckProcessPolicy(int16_t* aContentPolicy) {
  if (!aContentPolicy) {
    MOZ_ASSERT_UNREACHABLE("Null out variable");
    return false;
  }

  Element* el = AsElement();
  Document* doc = el->OwnerDoc();

  nsContentPolicyType objectType;
  switch (mType) {
    case ObjectType::Document:
      objectType = nsIContentPolicy::TYPE_DOCUMENT;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Calling checkProcessPolicy with an unexpected type");
      return false;
  }

  Result<RefPtr<LoadInfo>, nsresult> maybeLoadInfo = LoadInfo::Create(
      doc->NodePrincipal(),  // loading principal
      doc->NodePrincipal(),  // triggering principal
      el, nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK, objectType);
  if (NS_WARN_IF(maybeLoadInfo.isErr())) {
    return false;
  }
  RefPtr<LoadInfo> secCheckLoadInfo = maybeLoadInfo.unwrap();

  *aContentPolicy = nsIContentPolicy::ACCEPT;
  nsresult rv = NS_CheckContentProcessPolicy(
      mURI ? mURI : mBaseURI, secCheckLoadInfo, aContentPolicy,
      nsContentUtils::GetContentPolicy());
  NS_ENSURE_SUCCESS(rv, false);

  if (NS_CP_REJECTED(*aContentPolicy)) {
    LOG(("OBJLC [%p]: CheckContentProcessPolicy rejected load", this));
    return false;
  }

  return true;
}

bool nsObjectLoadingContent::IsSyntheticImageDocument() const {
  if (mType != ObjectType::Document || !mFrameLoader) {
    return false;
  }

  BrowsingContext* browsingContext = mFrameLoader->GetExtantBrowsingContext();
  return browsingContext && browsingContext->GetIsSyntheticDocumentContainer();
}

nsObjectLoadingContent::ParameterUpdateFlags
nsObjectLoadingContent::UpdateObjectParameters() {
  Element* el = AsElement();

  uint32_t caps = GetCapabilities();
  LOG(("OBJLC [%p]: Updating object parameters", this));

  nsresult rv;
  nsAutoCString newMime;
  nsCOMPtr<nsIURI> newURI;
  nsCOMPtr<nsIURI> newBaseURI;
  ObjectType newType;
  // Set if this state can't be used to load anything, forces
  // ObjectType::Fallback
  bool stateInvalid = false;
  // Indicates what parameters changed.
  // eParamChannelChanged - means parameters that affect channel opening
  //                        decisions changed
  // eParamStateChanged -   means anything that affects what content we load
  //                        changed, even if the channel we'd open remains the
  //                        same.
  //
  // State changes outside of the channel parameters only matter if we've
  // already opened a channel or tried to instantiate content, whereas channel
  // parameter changes require re-opening the channel even if we haven't gotten
  // that far.
  ParameterUpdateFlags retval = eParamNoChange;

  ///
  /// Initial MIME Type
  ///

  if (caps & eFallbackIfClassIDPresent &&
      el->HasNonEmptyAttr(nsGkAtoms::classid)) {
    // We don't support class ID plugin references, so we should always treat
    // having class Ids as attributes as invalid, and fallback accordingly.
    stateInvalid = true;
  }

  ///
  /// Codebase
  ///

  nsIURI* docBaseURI = el->GetBaseURI();

  nsAutoString codebaseStr;
  el->GetAttr(nsGkAtoms::codebase, codebaseStr);
  if (StaticPrefs::dom_object_embed_codebase_enabled() &&
      !codebaseStr.IsEmpty()) {
    rv = nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(newBaseURI), codebaseStr, el->OwnerDoc(), docBaseURI);
    if (NS_FAILED(rv)) {
      // Malformed URI
      LOG(
          ("OBJLC [%p]: Could not parse plugin's codebase as a URI, "
           "will use document baseURI instead",
           this));
    }
  }

  // If we failed to build a valid URI, use the document's base URI
  if (!newBaseURI) {
    newBaseURI = docBaseURI;
  }

  ///
  /// URI
  ///

  nsAutoString uriStr;
  // Different elements keep this in various locations
  if (el->NodeInfo()->Equals(nsGkAtoms::object)) {
    el->GetAttr(nsGkAtoms::data, uriStr);
  } else if (el->NodeInfo()->Equals(nsGkAtoms::embed)) {
    el->GetAttr(nsGkAtoms::src, uriStr);
  } else {
    MOZ_ASSERT_UNREACHABLE("Unrecognized plugin-loading tag");
  }

  mRewrittenYoutubeEmbed = false;

  // Note that the baseURI changing could affect the newURI, even if uriStr did
  // not change.
  if (!uriStr.IsEmpty()) {
    rv = nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(newURI), uriStr, el->OwnerDoc(), newBaseURI);
    nsCOMPtr<nsIURI> rewrittenURI;
    MaybeRewriteYoutubeEmbed(newURI, newBaseURI, getter_AddRefs(rewrittenURI));
    if (rewrittenURI) {
      newURI = rewrittenURI;
      mRewrittenYoutubeEmbed = true;
      newMime = "text/html"_ns;
    }

    if (NS_FAILED(rv)) {
      stateInvalid = true;
    }
  }

  ///
  /// type
  ///
  nsAutoString rawTypeAttr;
  el->GetAttr(nsGkAtoms::type, rawTypeAttr);
  // YouTube embeds might be using type="application/x-shockwave-flash"
  // which needs to be allowed, but must not override the text/html MIME set
  // above.
  if (!mRewrittenYoutubeEmbed && !rawTypeAttr.IsEmpty()) {
    nsAutoString params;
    nsAutoString mime;
    nsContentUtils::SplitMimeType(rawTypeAttr, mime, params);

    if (!StaticPrefs::dom_object_embed_type_hint_enabled()) {
      NS_ConvertUTF16toUTF8 mimeUTF8(mime);
      if (imgLoader::SupportImageWithMimeType(mimeUTF8)) {
        // Normally the type attribute should not be used as a hint, but for
        // images it does seem to happen in Chrome and Safari. Images generally
        // don't lead to code execution and we don't use
        // AcceptedMimeTypes::IMAGES_AND_DOCUMENTS above.
        newMime = mimeUTF8;
      } else if (GetTypeOfContent(mimeUTF8) != ObjectType::Document) {
        LOG(
            ("OBJLC [%p]: MIME '%s' from type attribute is not supported, "
             "forcing fallback.",
             this, mimeUTF8.get()));
        stateInvalid = true;
      }

      // Don't use the type attribute as a Content-Type hint in other cases.
    } else {
      CopyUTF16toUTF8(mime, newMime);
    }
  }

  ///
  /// Check if the original (pre-channel) content-type or URI changed, and
  /// record mOriginal{ContentType,URI}
  ///

  if ((mOriginalContentType != newMime) || !URIEquals(mOriginalURI, newURI)) {
    // These parameters changing requires re-opening the channel, so don't
    // consider the currently-open channel below
    // XXX(johns): Changing the mime type might change our decision on whether
    //             or not we load a channel, so we count changes to it as a
    //             channel parameter change for the sake of simplicity.
    retval = (ParameterUpdateFlags)(retval | eParamChannelChanged);
    LOG(("OBJLC [%p]: Channel parameters changed", this));
  }
  mOriginalContentType = newMime;
  mOriginalURI = newURI;

  ///
  /// If we have a channel, see if its MIME type should take precendence and
  /// check the final (redirected) URL
  ///

  // If we have a loaded channel and channel parameters did not change, use it
  // to determine what we would load.
  bool useChannel = mChannelLoaded && !(retval & eParamChannelChanged);
  // If we have a channel and are type loading, as opposed to having an existing
  // channel for a previous load.
  bool newChannel = useChannel && mType == ObjectType::Loading;

  RefPtr<DocumentChannel> documentChannel = do_QueryObject(mChannel);
  if (newChannel && documentChannel) {
    // If we've got a DocumentChannel which is marked as loaded using
    // `mChannelLoaded`, we are currently in the middle of a
    // `UpgradeLoadToDocument`.
    //
    // As we don't have the real mime-type from the channel, handle this by
    // using `newMime`.
    newMime = TEXT_HTML;

    MOZ_DIAGNOSTIC_ASSERT(GetTypeOfContent(newMime) == ObjectType::Document,
                          "How is text/html not ObjectType::Document?");
  } else if (newChannel && mChannel) {
    nsCString channelType;
    rv = mChannel->GetContentType(channelType);
    if (NS_FAILED(rv)) {
      MOZ_ASSERT_UNREACHABLE("GetContentType failed");
      stateInvalid = true;
      channelType.Truncate();
    }

    LOG(("OBJLC [%p]: Channel has a content type of %s", this,
         channelType.get()));

    bool binaryChannelType = false;
    if (channelType.EqualsASCII(APPLICATION_GUESS_FROM_EXT)) {
      channelType = APPLICATION_OCTET_STREAM;
      mChannel->SetContentType(channelType);
      binaryChannelType = true;
    } else if (channelType.EqualsASCII(APPLICATION_OCTET_STREAM) ||
               channelType.EqualsASCII(BINARY_OCTET_STREAM)) {
      binaryChannelType = true;
    }

    // Channel can change our URI through redirection
    rv = NS_GetFinalChannelURI(mChannel, getter_AddRefs(newURI));
    if (NS_FAILED(rv)) {
      MOZ_ASSERT_UNREACHABLE("NS_GetFinalChannelURI failure");
      stateInvalid = true;
    }

    ObjectType typeHint =
        newMime.IsEmpty() ? ObjectType::Fallback : GetTypeOfContent(newMime);

    // In order of preference:
    //
    // 1) Use our type hint if it matches a plugin
    // 2) If we have eAllowPluginSkipChannel, use the uri file extension if
    //    it matches a plugin
    // 3) If the channel returns a binary stream type:
    //    3a) If we have a type non-null non-document type hint, use that
    //    3b) If the uri file extension matches a plugin type, use that
    // 4) Use the channel type

    bool overrideChannelType = false;
    if (IsPluginMIME(newMime)) {
      LOG(("OBJLC [%p]: Using plugin type hint in favor of any channel type",
           this));
      overrideChannelType = true;
    } else if (binaryChannelType && typeHint != ObjectType::Fallback) {
      if (typeHint == ObjectType::Document) {
        if (imgLoader::SupportImageWithMimeType(newMime)) {
          LOG(
              ("OBJLC [%p]: Using type hint in favor of binary channel type "
               "(Image Document)",
               this));
          overrideChannelType = true;
        }
      } else {
        LOG(
            ("OBJLC [%p]: Using type hint in favor of binary channel type "
             "(Non-Image Document)",
             this));
        overrideChannelType = true;
      }
    }

    if (overrideChannelType) {
      // Set the type we'll use for dispatch on the channel.  Otherwise we could
      // end up trying to dispatch to a nsFrameLoader, which will complain that
      // it couldn't find a way to handle application/octet-stream
      nsAutoCString parsedMime, dummy;
      NS_ParseResponseContentType(newMime, parsedMime, dummy);
      if (!parsedMime.IsEmpty()) {
        mChannel->SetContentType(parsedMime);
      }
    } else {
      newMime = channelType;
    }
  } else if (newChannel) {
    LOG(("OBJLC [%p]: We failed to open a channel, marking invalid", this));
    stateInvalid = true;
  }

  ///
  /// Determine final type
  ///
  // In order of preference:
  //  1) If we have attempted channel load, or set stateInvalid above, the type
  //     is always null (fallback)
  //  2) If we have a loaded channel, we grabbed its mimeType above, use that
  //     type.
  //  3) If we have a plugin type and no URI, use that type.
  //  4) If we have a plugin type and eAllowPluginSkipChannel, use that type.
  //  5) if we have a URI, set type to loading to indicate we'd need a channel
  //     to proceed.
  //  6) Otherwise, type null to indicate unloadable content (fallback)
  //

  ObjectType newMime_Type = GetTypeOfContent(newMime);

  if (stateInvalid) {
    newType = ObjectType::Fallback;
    LOG(("OBJLC [%p]: NewType #0: %s - %u", this, newMime.get(),
         uint32_t(newType)));
    newMime.Truncate();
  } else if (newChannel) {
    // If newChannel is set above, we considered it in setting newMime
    newType = newMime_Type;
    LOG(("OBJLC [%p]: NewType #1: %s - %u", this, newMime.get(),
         uint32_t(newType)));
    LOG(("OBJLC [%p]: Using channel type", this));
  } else if (((caps & eAllowPluginSkipChannel) || !newURI) &&
             IsPluginMIME(newMime)) {
    newType = newMime_Type;
    LOG(("OBJLC [%p]: NewType #2: %s - %u", this, newMime.get(),
         uint32_t(newType)));
    LOG(("OBJLC [%p]: Plugin type with no URI, skipping channel load", this));
  } else if (newURI && (mOriginalContentType.IsEmpty() ||
                        newMime_Type != ObjectType::Fallback)) {
    // We could potentially load this if we opened a channel on mURI, indicate
    // this by leaving type as loading.
    //
    // If a MIME type was requested in the tag, but we have decided to set load
    // type to null, ignore (otherwise we'll default to document type loading).
    newType = ObjectType::Loading;
    LOG(("OBJLC [%p]: NewType #3: %u", this, uint32_t(newType)));
  } else {
    // Unloadable - no URI, and no plugin/MIME type. Non-plugin types (images,
    // documents) always load with a channel.
    newType = ObjectType::Fallback;
    LOG(("OBJLC [%p]: NewType #4: %u", this, uint32_t(newType)));
  }

  ///
  /// Handle existing channels
  ///

  if (useChannel && newType == ObjectType::Loading) {
    // We decided to use a channel, and also that the previous channel is still
    // usable, so re-use the existing values.
    newType = mType;
    LOG(("OBJLC [%p]: NewType #5: %u", this, uint32_t(newType)));
    newMime = mContentType;
    newURI = mURI;
  } else if (useChannel && !newChannel) {
    // We have an existing channel, but did not decide to use one.
    retval = (ParameterUpdateFlags)(retval | eParamChannelChanged);
    useChannel = false;
  }

  ///
  /// Update changed values
  ///

  if (newType != mType) {
    retval = (ParameterUpdateFlags)(retval | eParamStateChanged);
    LOG(("OBJLC [%p]: Type changed from %u -> %u", this, uint32_t(mType),
         uint32_t(newType)));
    mType = newType;
  }

  if (!URIEquals(mBaseURI, newBaseURI)) {
    LOG(("OBJLC [%p]: Object effective baseURI changed", this));
    mBaseURI = newBaseURI;
  }

  if (!URIEquals(newURI, mURI)) {
    retval = (ParameterUpdateFlags)(retval | eParamStateChanged);
    LOG(("OBJLC [%p]: Object effective URI changed", this));
    mURI = newURI;
  }

  // We don't update content type when loading, as the type is not final and we
  // don't want to superfluously change between mOriginalContentType ->
  // mContentType when doing |obj.data = obj.data| with a channel and differing
  // type.
  if (mType != ObjectType::Loading && mContentType != newMime) {
    retval = (ParameterUpdateFlags)(retval | eParamStateChanged);
    retval = (ParameterUpdateFlags)(retval | eParamContentTypeChanged);
    LOG(("OBJLC [%p]: Object effective mime type changed (%s -> %s)", this,
         mContentType.get(), newMime.get()));
    mContentType = newMime;
  }

  // If we decided to keep using info from an old channel, but also that state
  // changed, we need to invalidate it.
  if (useChannel && !newChannel && (retval & eParamStateChanged)) {
    mType = ObjectType::Loading;
    retval = (ParameterUpdateFlags)(retval | eParamChannelChanged);
  }

  return retval;
}

// Only OnStartRequest should be passing the channel parameter
nsresult nsObjectLoadingContent::LoadObject(bool aNotify, bool aForceLoad) {
  return LoadObject(aNotify, aForceLoad, nullptr);
}

nsresult nsObjectLoadingContent::LoadObject(bool aNotify, bool aForceLoad,
                                            nsIRequest* aLoadingChannel) {
  Element* el = AsElement();
  Document* doc = el->OwnerDoc();
  nsresult rv = NS_OK;

  // Per bug 1318303, if the parent document is not active, load the alternative
  // and return.
  if (!doc->IsCurrentActiveDocument()) {
    // Since this can be triggered on change of attributes, make sure we've
    // unloaded whatever is loaded first.
    UnloadObject();
    ObjectType oldType = mType;
    mType = ObjectType::Fallback;
    TriggerInnerFallbackLoads();
    NotifyStateChanged(oldType, true);
    return NS_OK;
  }

  // XXX(johns): In these cases, we refuse to touch our content and just
  //   remain unloaded, as per legacy behavior. It would make more sense to
  //   load fallback content initially and refuse to ever change state again.
  if (doc->IsBeingUsedAsImage()) {
    return NS_OK;
  }

  if (doc->IsLoadedAsData() || doc->IsStaticDocument()) {
    return NS_OK;
  }

  LOG(("OBJLC [%p]: LoadObject called, notify %u, forceload %u, channel %p",
       this, aNotify, aForceLoad, aLoadingChannel));

  // We can't re-use an already open channel, but aForceLoad may make us try
  // to load a plugin without any changes in channel state.
  if (aForceLoad && mChannelLoaded) {
    CloseChannel();
    mChannelLoaded = false;
  }

  // Save these for NotifyStateChanged();
  ObjectType oldType = mType;

  ParameterUpdateFlags stateChange = UpdateObjectParameters();

  if (!stateChange && !aForceLoad) {
    return NS_OK;
  }

  ///
  /// State has changed, unload existing content and attempt to load new type
  ///
  LOG(("OBJLC [%p]: LoadObject - plugin state changed (%u)", this,
       stateChange));

  // We synchronously start/stop plugin instances below, which may spin the
  // event loop. Re-entering into the load is fine, but at that point the
  // original load call needs to abort when unwinding
  // NOTE this is located *after* the state change check, a subsequent load
  //      with no subsequently changed state will be a no-op.
  if (mIsLoading) {
    LOG(("OBJLC [%p]: Re-entering into LoadObject", this));
  }
  mIsLoading = true;
  AutoSetLoadingToFalse reentryCheck(this);

  // Unload existing content, keeping in mind stopping plugins might spin the
  // event loop. Note that we check for still-open channels below
  UnloadObject(false);  // Don't reset state
  if (!mIsLoading) {
    // The event loop must've spun and re-entered into LoadObject, which
    // finished the load
    LOG(("OBJLC [%p]: Re-entered into LoadObject, aborting outer load", this));
    return NS_OK;
  }

  // Determine what's going on with our channel.
  if (stateChange & eParamChannelChanged) {
    // If the channel params changed, throw away the channel, but unset
    // mChannelLoaded so we'll still try to open a new one for this load if
    // necessary
    CloseChannel();
    mChannelLoaded = false;
  } else if (mType == ObjectType::Fallback && mChannel) {
    // If we opened a channel but then failed to find a loadable state, throw it
    // away. mChannelLoaded will indicate that we tried to load a channel at one
    // point so we wont recurse
    CloseChannel();
  } else if (mType == ObjectType::Loading && mChannel) {
    // We're still waiting on a channel load, already opened one, and
    // channel parameters didn't change
    return NS_OK;
  } else if (mChannelLoaded && mChannel != aLoadingChannel) {
    // The only time we should have a loaded channel with a changed state is
    // when the channel has just opened -- in which case this call should
    // have originated from OnStartRequest
    MOZ_ASSERT_UNREACHABLE(
        "Loading with a channel, but state doesn't make sense");
    return NS_OK;
  }

  //
  // Security checks
  //

  if (mType != ObjectType::Fallback) {
    bool allowLoad = true;
    int16_t contentPolicy = nsIContentPolicy::ACCEPT;
    // If mChannelLoaded is set we presumably already passed load policy
    // If mType == ObjectType::Loading then we call OpenChannel() which
    // internally creates a new channel and calls asyncOpen() on that channel
    // which then enforces content policy checks.
    if (allowLoad && mURI && !mChannelLoaded && mType != ObjectType::Loading) {
      allowLoad = CheckLoadPolicy(&contentPolicy);
    }
    // If we're loading a type now, check ProcessPolicy. Note that we may check
    // both now in the case of plugins whose type is determined before opening a
    // channel.
    if (allowLoad && mType != ObjectType::Loading) {
      allowLoad = CheckProcessPolicy(&contentPolicy);
    }

    // Content policy implementations can mutate the DOM, check for re-entry
    if (!mIsLoading) {
      LOG(("OBJLC [%p]: We re-entered in content policy, leaving original load",
           this));
      return NS_OK;
    }

    // Load denied, switch to null
    if (!allowLoad) {
      LOG(("OBJLC [%p]: Load denied by policy", this));
      mType = ObjectType::Fallback;
    }
  }

  // https://html.spec.whatwg.org/multipage/iframe-embed-object.html#the-object-element
  // requires that `embed` and `object` go through `Fetch` with mode=navigate,
  // see 1.3.5. This will in https://fetch.spec.whatwg.org/#fetching plumb us
  // through to https://fetch.spec.whatwg.org/#concept-main-fetch where in step
  // 12 a switch is performed. Since `object` and `embed` have mode=navigate the
  // result of https://fetch.spec.whatwg.org/#concept-scheme-fetch will decide
  // if main fetch proceeds. We short-circuit that scheme-fetch here, inspecting
  // if the scheme of `mURI` is one that would return a network error. The
  // following schemes are allowed through in scheme fetch:
  // "about", "blob", "data", "file", "http", "https".
  //
  // Some accessibility tests use our internal "chrome" scheme.
  if (mType != ObjectType::Fallback && mURI) {
    ObjectType type = ObjectType::Fallback;
    for (const auto& candidate :
         {"about", "blob", "chrome", "data", "file", "http", "https"}) {
      if (mURI->SchemeIs(candidate)) {
        type = mType;
        break;
      }
    }
    mType = type;
  }

  // Sanity check: We shouldn't have any loaded resources, pending events, or
  // a final listener at this point
  if (mFrameLoader || mFinalListener) {
    MOZ_ASSERT_UNREACHABLE("Trying to load new plugin with existing content");
    return NS_OK;
  }

  // More sanity-checking:
  // If mChannel is set, mChannelLoaded should be set, and vice-versa
  if (mType != ObjectType::Fallback && !!mChannel != mChannelLoaded) {
    MOZ_ASSERT_UNREACHABLE("Trying to load with bad channel state");
    return NS_OK;
  }

  ///
  /// Attempt to load new type
  ///

  // We don't set mFinalListener until OnStartRequest has been called, to
  // prevent re-entry ugliness with CloseChannel()
  nsCOMPtr<nsIStreamListener> finalListener;
  switch (mType) {
    case ObjectType::Document: {
      if (!mChannel) {
        // We could mFrameLoader->LoadURI(mURI), but UpdateObjectParameters
        // requires documents have a channel, so this is not a valid state.
        MOZ_ASSERT_UNREACHABLE(
            "Attempting to load a document without a "
            "channel");
        rv = NS_ERROR_FAILURE;
        break;
      }

      nsCOMPtr<nsIDocShell> docShell = SetupDocShell(mURI);
      if (!docShell) {
        rv = NS_ERROR_FAILURE;
        break;
      }

      // We're loading a document, so we have to set LOAD_DOCUMENT_URI
      // (especially important for firing onload)
      nsLoadFlags flags = 0;
      mChannel->GetLoadFlags(&flags);
      flags |= nsIChannel::LOAD_DOCUMENT_URI;
      mChannel->SetLoadFlags(flags);

      nsCOMPtr<nsIInterfaceRequestor> req(do_QueryInterface(docShell));
      NS_ASSERTION(req, "Docshell must be an ifreq");

      nsCOMPtr<nsIURILoader> uriLoader(components::URILoader::Service());
      if (NS_WARN_IF(!uriLoader)) {
        MOZ_ASSERT_UNREACHABLE("Failed to get uriLoader service");
        RefPtr<nsFrameLoader> loader = std::move(mFrameLoader);
        loader->Destroy();
        break;
      }

      uint32_t uriLoaderFlags = nsDocShell::ComputeURILoaderFlags(
          docShell->GetBrowsingContext(), LOAD_NORMAL,
          /* aIsDocumentLoad */ false);

      rv = uriLoader->OpenChannel(mChannel, uriLoaderFlags, req,
                                  getter_AddRefs(finalListener));
      // finalListener will receive OnStartRequest either below, or if
      // `mChannel` is a `DocumentChannel`, it will be received after
      // RedirectToRealChannel.
    } break;
    case ObjectType::Loading:
      // If our type remains Loading, we need a channel to proceed
      rv = OpenChannel();
      if (NS_FAILED(rv)) {
        LOG(("OBJLC [%p]: OpenChannel returned failure (%" PRIu32 ")", this,
             static_cast<uint32_t>(rv)));
      }
      break;
    case ObjectType::Fallback:
      // Handled below, silence compiler warnings
      break;
  }

  //
  // Loaded, handle notifications and fallback
  //
  if (NS_FAILED(rv)) {
    // If we failed in the loading hunk above, switch to null (empty) region
    LOG(("OBJLC [%p]: Loading failed, switching to fallback", this));
    mType = ObjectType::Fallback;
  }

  if (mType == ObjectType::Fallback) {
    LOG(("OBJLC [%p]: Switching to fallback state", this));
    MOZ_ASSERT(!mFrameLoader, "switched to fallback but also loaded something");

    MaybeFireErrorEvent();

    if (mChannel) {
      // If we were loading with a channel but then failed over, throw it away
      CloseChannel();
    }

    // Don't try to initialize plugins or final listener below
    finalListener = nullptr;

    TriggerInnerFallbackLoads();
  }

  // Notify of our final state
  NotifyStateChanged(oldType, aNotify);
  NS_ENSURE_TRUE(mIsLoading, NS_OK);

  //
  // Spawning plugins and dispatching to the final listener may re-enter, so are
  // delayed until after we fire a notification, to prevent missing
  // notifications or firing them out of order.
  //
  // Note that we ensured that we entered into LoadObject() from
  // ::OnStartRequest above when loading with a channel.
  //

  rv = NS_OK;
  if (finalListener) {
    NS_ASSERTION(mType != ObjectType::Fallback && mType != ObjectType::Loading,
                 "We should not have a final listener with a non-loaded type");
    mFinalListener = finalListener;

    // If we're a DocumentChannel load, hold off on firing the `OnStartRequest`
    // callback, as we haven't received it yet from our caller.
    RefPtr<DocumentChannel> documentChannel = do_QueryObject(mChannel);
    if (documentChannel) {
      MOZ_ASSERT(
          mType == ObjectType::Document,
          "We have a DocumentChannel here but aren't loading a document?");
    } else {
      rv = finalListener->OnStartRequest(mChannel);
    }
  }

  if ((NS_FAILED(rv) && rv != NS_ERROR_PARSED_DATA_CACHED) && mIsLoading) {
    // Since we've already notified of our transition, we can just Unload and
    // call ConfigureFallback (which will notify again)
    oldType = mType;
    mType = ObjectType::Fallback;
    UnloadObject(false);
    NS_ENSURE_TRUE(mIsLoading, NS_OK);
    CloseChannel();
    TriggerInnerFallbackLoads();
    NotifyStateChanged(oldType, true);
  }

  return NS_OK;
}

// This call can re-enter when dealing with plugin listeners
nsresult nsObjectLoadingContent::CloseChannel() {
  if (mChannel) {
    LOG(("OBJLC [%p]: Closing channel\n", this));
    // Null the values before potentially-reentering, and ensure they survive
    // the call
    nsCOMPtr<nsIChannel> channelGrip(mChannel);
    nsCOMPtr<nsIStreamListener> listenerGrip(mFinalListener);
    mChannel = nullptr;
    mFinalListener = nullptr;
    channelGrip->CancelWithReason(NS_BINDING_ABORTED,
                                  "nsObjectLoadingContent::CloseChannel"_ns);
    if (listenerGrip) {
      // mFinalListener is only set by LoadObject after OnStartRequest, or
      // by OnStartRequest in the case of late-opened plugin streams
      if (nsContentUtils::IsSafeToRunScript()) {
        listenerGrip->OnStopRequest(channelGrip, NS_BINDING_ABORTED);
      } else {
        nsContentUtils::AddScriptRunner(NS_NewRunnableFunction(
            "nsObjectLoadingContent::CloseChannel",
            [channelGrip, listenerGrip]() {
              listenerGrip->OnStopRequest(channelGrip, NS_BINDING_ABORTED);
            }));
      }
    }
  }
  return NS_OK;
}

bool nsObjectLoadingContent::IsAboutBlankLoadOntoInitialAboutBlank(
    nsIURI* aURI, bool aInheritPrincipal, nsIPrincipal* aPrincipalToInherit) {
  if (!NS_IsAboutBlankAllowQueryAndFragment(aURI) || !aInheritPrincipal) {
    return false;
  }

  if (!mFrameLoader || !mFrameLoader->GetExistingDocShell()) {
    return false;
  }

  RefPtr<nsDocShellLoadState> dummyLoadState = new nsDocShellLoadState(mURI);
  return mFrameLoader->GetExistingDocShell()->ShouldDoInitialAboutBlankSyncLoad(
      aURI, dummyLoadState, aPrincipalToInherit);
}

nsresult nsObjectLoadingContent::OpenChannel() {
  Element* el = AsElement();
  Document* doc = el->OwnerDoc();
  NS_ASSERTION(doc, "No owner document?");

  nsresult rv;
  mChannel = nullptr;

  // E.g. mms://
  if (!mURI || !CanHandleURI(mURI)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // The channel's own security check happens in the parent process, which for
  // a document load is only reached after the nsDocShellLoadState has already
  // crossed IPC. Check here as well so that a load which content is not
  // allowed to trigger never gets that far.
  rv = nsContentUtils::GetSecurityManager()->CheckLoadURIWithPrincipal(
      el->NodePrincipal(), mURI, nsIScriptSecurityManager::STANDARD,
      doc->InnerWindowID());
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsILoadGroup> group = doc->GetDocumentLoadGroup();
  nsCOMPtr<nsIChannel> chan;
  RefPtr<ObjectInterfaceRequestorShim> shim =
      new ObjectInterfaceRequestorShim(this);

  bool inheritAttrs = nsContentUtils::ChannelShouldInheritPrincipal(
      el->NodePrincipal(),  // aLoadState->PrincipalToInherit()
      mURI,                 // aLoadState->URI()
      true,                 // aInheritForAboutBlank
      false);               // aForceInherit

  bool inheritPrincipal = inheritAttrs && !mURI->SchemeIs("data");

  nsSecurityFlags securityFlags =
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL;
  if (inheritPrincipal) {
    securityFlags |= nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
  }

  nsContentPolicyType contentPolicyType = GetContentPolicyType();
  // The setting of LOAD_BYPASS_SERVICE_WORKER here is now an optimization.
  // ServiceWorkerInterceptController::ShouldPrepareForIntercept does a more
  // expensive check of BrowsingContext ancestors to look for object/embed.
  nsLoadFlags loadFlags = nsIChannel::LOAD_CALL_CONTENT_SNIFFERS |
                          nsIChannel::LOAD_BYPASS_SERVICE_WORKER |
                          nsIRequest::LOAD_HTML_OBJECT_DATA;
  uint32_t sandboxFlags = doc->GetSandboxFlags();

  // For object loads we store the policyContainer that potentially needs to
  // be inherited, e.g. in case we are loading an opaque origin
  // like a data: URI. The actual inheritance check happens within
  // Document::InitPolicyContainer(). Please create an actual copy of the
  // policyContainer (do not share the same reference) otherwise modifications
  // done (such as the meta CSP of the new doc) in an opaque origin will
  // incorrectly be propagated to the embedding document.
  RefPtr<PolicyContainer> policyContainerToInherit;
  if (nsCOMPtr<nsIPolicyContainer> policyContainer =
          doc->GetPolicyContainer()) {
    policyContainerToInherit = new PolicyContainer();
    policyContainerToInherit->InitFromOther(
        PolicyContainer::Cast(policyContainer.get()));
  }

  // --- Create LoadInfo
  RefPtr<LoadInfo> loadInfo = MOZ_TRY(LoadInfo::Create(
      /*aLoadingPrincipal = aLoadingContext->NodePrincipal() */ nullptr,
      /*aTriggeringPrincipal = aLoadingPrincipal */ nullptr,
      /*aLoadingContext = */ el,
      /*aSecurityFlags = */ securityFlags,
      /*aContentPolicyType = */ contentPolicyType,
      /*aLoadingClientInfo = */ Nothing(),
      /*aController = */ Nothing(),
      /*aSandboxFlags = */ sandboxFlags));

  if (inheritAttrs) {
    loadInfo->SetPrincipalToInherit(el->NodePrincipal());
  }

  // For object loads we store the policyContainer that potentially needs to
  // be inherited, e.g. in case we are loading an opaque origin
  // like a data: URI. The actual inheritance check happens within
  // Document::InitPolicyContainer(). Please create an actual copy of the
  // policyContainer (do not share the same reference) otherwise modifications
  // done (such as the meta CSP of the new doc) in an opaque origin will
  // incorrectly be propagated to the embedding document.
  if (policyContainerToInherit) {
    loadInfo->SetPolicyContainerToInherit(policyContainerToInherit);
  }

  if (DocumentChannel::CanUseDocumentChannel(mURI) &&
      !IsAboutBlankLoadOntoInitialAboutBlank(mURI, inheritPrincipal,
                                             el->NodePrincipal())) {
    // --- Create LoadState
    RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(mURI);
    loadState->SetPrincipalToInherit(el->NodePrincipal());
    loadState->SetTriggeringPrincipal(loadInfo->TriggeringPrincipal());
    if (policyContainerToInherit) {
      loadState->SetPolicyContainer(policyContainerToInherit);
    }
    loadState->SetTriggeringSandboxFlags(sandboxFlags);

    // TODO(djg): This was httpChan->SetReferrerInfoWithoutClone(referrerInfo);
    // Is the ...WithoutClone(...) important?
    auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);
    loadState->SetReferrerInfo(referrerInfo);

    loadState->SetShouldCheckForRecursion(true);

    // When loading using DocumentChannel, ensure that the MIME type hint is
    // propagated to DocumentLoadListener. Object elements can override MIME
    // handling in some scenarios.
    if (!mOriginalContentType.IsEmpty()) {
      nsAutoCString parsedMime, dummy;
      NS_ParseResponseContentType(mOriginalContentType, parsedMime, dummy);
      if (!parsedMime.IsEmpty()) {
        loadState->SetTypeHint(parsedMime);
      }
    }

    chan =
        DocumentChannel::CreateForObject(loadState, loadInfo, loadFlags, shim);
    MOZ_ASSERT(chan);
    // NS_NewChannel sets the group on the channel.  CreateDocumentChannel does
    // not.
    chan->SetLoadGroup(group);
  } else {
    rv = NS_NewChannelInternal(getter_AddRefs(chan),  // outChannel
                               mURI,                  // aUri
                               loadInfo,              // aLoadInfo
                               nullptr,               // aPerformanceStorage
                               group,                 // aLoadGroup
                               shim,                  // aCallbacks
                               loadFlags,             // aLoadFlags
                               nullptr);              // aIoService
    NS_ENSURE_SUCCESS(rv, rv);
  };

  // Referrer
  if (nsCOMPtr<nsIHttpChannel> httpChan = do_QueryInterface(chan)) {
    auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);

    rv = httpChan->SetReferrerInfoWithoutClone(referrerInfo);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    // Set the initiator type
    if (nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(httpChan)) {
      timedChannel->SetInitiatorType(el->LocalName());
    }

    nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(httpChan));
    if (cos && UserActivation::IsHandlingUserInput()) {
      cos->AddClassFlags(nsIClassOfService::UrgentStart);
    }
  }

  // AsyncOpen can fail if a file does not exist.
  rv = chan->AsyncOpen(shim);
  NS_ENSURE_SUCCESS(rv, rv);
  LOG(("OBJLC [%p]: Channel opened", this));
  mChannel = std::move(chan);
  return NS_OK;
}

uint32_t nsObjectLoadingContent::GetCapabilities() const {
  return eSupportImages | eSupportDocuments;
}

void nsObjectLoadingContent::Destroy() {
  // Reset state so that if the element is re-appended to tree again (e.g.
  // adopting to another document), it will reload resource again.
  UnloadObject();
}

/* static */
void nsObjectLoadingContent::Traverse(nsObjectLoadingContent* tmp,
                                      nsCycleCollectionTraversalCallback& cb) {
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFrameLoader);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPermissionsPolicy);
}

/* static */
void nsObjectLoadingContent::Unlink(nsObjectLoadingContent* tmp) {
  if (tmp->mFrameLoader) {
    tmp->mFrameLoader->Destroy();
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFrameLoader);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPermissionsPolicy);
}

void nsObjectLoadingContent::UnloadObject(bool aResetState) {
  if (mFrameLoader) {
    RefPtr<nsFrameLoader> loader = std::move(mFrameLoader);
    loader->Destroy();
  }

  if (aResetState) {
    CloseChannel();
    mChannelLoaded = false;
    mType = ObjectType::Loading;
    mURI = mOriginalURI = mBaseURI = nullptr;
    mContentType.Truncate();
    mOriginalContentType.Truncate();
  }

  mScriptRequested = false;

  mIsStopping = false;

  mSubdocumentIntrinsicSize.reset();
  mSubdocumentIntrinsicRatio.reset();
}

void nsObjectLoadingContent::NotifyStateChanged(ObjectType aOldType,
                                                bool aNotify) {
  LOG(("OBJLC [%p]: NotifyStateChanged: (%u) -> (%u) (notify %i)", this,
       uint32_t(aOldType), uint32_t(mType), aNotify));

  dom::Element* thisEl = AsElement();
  // Non-images are always not broken.
  // XXX: I assume we could just remove this completely?
  thisEl->RemoveStates(ElementState::BROKEN, aNotify);

  if (mType == aOldType) {
    return;
  }

  Document* doc = thisEl->GetComposedDoc();
  if (!doc) {
    return;  // Nothing to do
  }

  PresShell* presShell = doc->GetPresShell();
  // If there is no PresShell or it hasn't been initialized there isn't much to
  // do.
  if (!presShell || !presShell->DidInitialize()) {
    return;
  }
  presShell->PostRecreateFramesFor(thisEl);
}

nsObjectLoadingContent::ObjectType nsObjectLoadingContent::GetTypeOfContent(
    const nsCString& aMIMEType) {
  Element* el = AsElement();
  NS_ASSERTION(el, "must be a content");

  Document* doc = el->OwnerDoc();

  // Images and documents are always supported.
  MOZ_ASSERT((GetCapabilities() & (eSupportImages | eSupportDocuments)) ==
             (eSupportImages | eSupportDocuments));

  LOG(
      ("OBJLC [%p]: calling HtmlObjectContentTypeForMIMEType: aMIMEType: %s - "
       "el: %p\n",
       this, aMIMEType.get(), el));
  auto ret =
      static_cast<ObjectType>(nsContentUtils::HtmlObjectContentTypeForMIMEType(
          aMIMEType, doc->GetSandboxFlags()));
  LOG(("OBJLC [%p]: called HtmlObjectContentTypeForMIMEType\n", this));
  return ret;
}

void nsObjectLoadingContent::CreateStaticClone(
    nsObjectLoadingContent* aDest) const {
  MOZ_ASSERT(aDest->AsElement()->OwnerDoc()->IsStaticDocument());
  aDest->mType = mType;

  if (mFrameLoader) {
    aDest->AsElement()->OwnerDoc()->AddPendingFrameStaticClone(aDest,
                                                               mFrameLoader);
  }
}

NS_IMETHODIMP
nsObjectLoadingContent::GetSrcURI(nsIURI** aURI) {
  NS_IF_ADDREF(*aURI = GetSrcURI());
  return NS_OK;
}

void nsObjectLoadingContent::TriggerInnerFallbackLoads() {
  MOZ_ASSERT(!mFrameLoader && !mChannel,
             "ConfigureFallback called with loaded content");
  MOZ_ASSERT(mType == ObjectType::Fallback);

  Element* el = AsElement();
  if (!el->IsHTMLElement(nsGkAtoms::object)) {
    return;
  }
  // Do a depth-first traverse of node tree with the current element as root,
  // looking for non-<param> elements.  If we find some then we have an HTML
  // fallback for this element
  AutoTArray<RefPtr<nsIContent>, 4> targets;
  for (nsIContent* child = el->GetFirstChild(); child;) {
    // <object> and <embed> elements in the fallback need to StartObjectLoad.
    // Their children should be ignored since they are part of those element's
    // fallback.
    if (child->IsAnyOfHTMLElements(nsGkAtoms::embed, nsGkAtoms::object)) {
      targets.AppendElement(child);
      child = child->GetNextNonChildNode(el);
    } else {
      child = child->GetNextNode(el);
    }
  }

  for (RefPtr<nsIContent>& target : targets) {
    if (!target->IsInclusiveDescendantOf(el)) {
      continue;
    }
    if (auto* embed = HTMLEmbedElement::FromNode(target)) {
      embed->StartObjectLoad(true, true);
    } else if (auto* object = HTMLObjectElement::FromNode(target)) {
      object->StartObjectLoad(true, true);
    }
  }
}

NS_IMETHODIMP
nsObjectLoadingContent::UpgradeLoadToDocument(
    nsIChannel* aRequest, BrowsingContext** aBrowsingContext) {
  AUTO_PROFILER_LABEL("nsObjectLoadingContent::UpgradeLoadToDocument", NETWORK);

  LOG(("OBJLC [%p]: UpgradeLoadToDocument", this));

  if (aRequest != mChannel || !aRequest) {
    // happens when a new load starts before the previous one got here.
    return NS_BINDING_ABORTED;
  }

  // We should be state loading.
  if (mType != ObjectType::Loading) {
    MOZ_ASSERT_UNREACHABLE("Should be type loading at this point");
    return NS_BINDING_ABORTED;
  }
  MOZ_ASSERT(!mChannelLoaded, "mChannelLoaded set already?");
  MOZ_ASSERT(!mFinalListener, "mFinalListener exists already?");

  mChannelLoaded = true;

  // We don't need to check for errors here, unlike in `OnStartRequest`, as
  // `UpgradeLoadToDocument` is only called when the load is going to become a
  // process-switching load. As we never process switch for failed object loads,
  // we know our channel status is successful.

  // Call `LoadObject` to trigger our nsObjectLoadingContext to switch into the
  // specified new state.
  nsresult rv = LoadObject(true, false, aRequest);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  RefPtr<BrowsingContext> bc = GetBrowsingContext();
  if (!bc) {
    return NS_ERROR_FAILURE;
  }

  // At this point we know that we have a browsing context, so it's time to make
  // sure that that browsing context gets the correct container permissions
  // policy. This is needed for
  // `DocumentLoadListener::MaybeTriggerProcessSwitch` to be able to start
  // loading the document with the correct container permissions policy in the
  // load info.
  RefreshPermissionsPolicy();

  bc.forget(aBrowsingContext);
  return NS_OK;
}

Document* nsObjectLoadingContent::GetContentDocument(
    nsIPrincipal& aSubjectPrincipal) {
  Element* el = AsElement();
  if (!el->IsInComposedDoc()) {
    return nullptr;
  }

  Document* sub_doc = el->OwnerDoc()->GetSubDocumentFor(el);
  if (!sub_doc) {
    return nullptr;
  }

  // Return null for cross-origin contentDocument.
  if (!aSubjectPrincipal.SubsumesConsideringDomain(sub_doc->NodePrincipal())) {
    return nullptr;
  }

  return sub_doc;
}

void nsObjectLoadingContent::MaybeFireErrorEvent() {
  Element* el = AsElement();
  // Queue a task to fire an error event if we're an <object> element.  The
  // queueing is important, since then we don't have to worry about reentry.
  if (el->IsHTMLElement(nsGkAtoms::object)) {
    RefPtr<AsyncEventDispatcher> loadBlockingAsyncDispatcher =
        new LoadBlockingAsyncEventDispatcher(el, u"error"_ns, CanBubble::eNo,
                                             ChromeOnlyDispatch::eNo);
    loadBlockingAsyncDispatcher->PostDOMEvent();
  }
}

bool nsObjectLoadingContent::BlockEmbedOrObjectContentLoading() {
  Element* el = AsElement();

  // Traverse up the node tree to see if we have any ancestors that may block us
  // from loading
  for (nsIContent* parent = el->GetParent(); parent;
       parent = parent->GetParent()) {
    if (parent->IsAnyOfHTMLElements(nsGkAtoms::video, nsGkAtoms::audio)) {
      return true;
    }
    // If we have an ancestor that is an object with a source, it'll have an
    // associated displayed type. If that type is not null, don't load content
    // for the embed.
    if (auto* object = HTMLObjectElement::FromNode(parent)) {
      if (object->Type() != ObjectType::Fallback) {
        return true;
      }
    }
  }
  return false;
}

void nsObjectLoadingContent::SubdocumentIntrinsicSizeOrRatioChanged(
    const Maybe<IntrinsicSize>& aIntrinsicSize,
    const Maybe<AspectRatio>& aIntrinsicRatio) {
  if (aIntrinsicSize == mSubdocumentIntrinsicSize &&
      aIntrinsicRatio == mSubdocumentIntrinsicRatio) {
    return;
  }

  mSubdocumentIntrinsicSize = aIntrinsicSize;
  mSubdocumentIntrinsicRatio = aIntrinsicRatio;

  if (nsSubDocumentFrame* sdf = do_QueryFrame(AsElement()->GetPrimaryFrame())) {
    sdf->SubdocumentIntrinsicSizeOrRatioChanged();
  }
}

void nsObjectLoadingContent::SubdocumentImageLoadComplete(nsresult aResult) {
  ObjectType oldType = mType;
  if (NS_FAILED(aResult)) {
    UnloadObject();
    mType = ObjectType::Fallback;
    TriggerInnerFallbackLoads();
    NotifyStateChanged(oldType, true);
    return;
  }

  // (mChannelLoaded && mChannel) indicates this is a good state, not any sort
  // of failures.
  MOZ_DIAGNOSTIC_ASSERT_IF(mChannelLoaded && mChannel,
                           mType == ObjectType::Document);
  NotifyStateChanged(oldType, true);
}

void nsObjectLoadingContent::MaybeStoreCrossOriginPermissionsPolicy() {
  MOZ_DIAGNOSTIC_ASSERT(mFrameLoader);
  if (!mFrameLoader) {
    return;
  }

  // If the browsingContext is not ready (because docshell is dead), don't try
  // to create one.
  if (!mFrameLoader->IsRemoteFrame() && !mFrameLoader->GetExistingDocShell()) {
    return;
  }

  RefPtr<BrowsingContext> browsingContext = mFrameLoader->GetBrowsingContext();

  if (!browsingContext || !browsingContext->IsContentSubframe()) {
    return;
  }

  auto* el = nsGenericHTMLElement::FromNode(AsElement());
  if (!el->IsInComposedDoc()) {
    return;
  }

  if (ContentChild* cc = ContentChild::GetSingleton()) {
    (void)cc->SendSetContainerPermissionsPolicy(
        browsingContext, Some(mPermissionsPolicy->ToPermissionsPolicyInfo()));
  }
}

/* static */ already_AddRefed<nsIPrincipal>
nsObjectLoadingContent::GetPermissionsPolicyDefaultOrigin(nsINode* aNode) {
  auto* el = nsGenericHTMLElement::FromNode(aNode);
  nsCOMPtr<nsIURI> nodeURI;
  // Different elements keep this in various locations
  if (el->NodeInfo()->Equals(nsGkAtoms::object)) {
    el->GetURIAttr(nsGkAtoms::data, nullptr, getter_AddRefs(nodeURI));
  } else if (el->NodeInfo()->Equals(nsGkAtoms::embed)) {
    el->GetURIAttr(nsGkAtoms::src, nullptr, getter_AddRefs(nodeURI));
  }

  nsCOMPtr<nsIPrincipal> principal;
  if (nodeURI) {
    principal = BasePrincipal::CreateContentPrincipal(
        nodeURI,
        BasePrincipal::Cast(el->NodePrincipal())->OriginAttributesRef());
  } else {
    principal = el->NodePrincipal();
  }

  return principal.forget();
}

void nsObjectLoadingContent::RefreshPermissionsPolicy() {
  if (mType != ObjectType::Document) {
    return;
  }

  if (!mPermissionsPolicy) {
    mPermissionsPolicy = MakeAndAddRef<PermissionsPolicy>(AsElement());
  }

  // The origin can change if 'src' or 'data' attributes change.
  nsCOMPtr<nsIPrincipal> origin =
      GetPermissionsPolicyDefaultOrigin(AsElement());
  MOZ_ASSERT(origin);
  mPermissionsPolicy->SetDefaultOrigin(origin);

  mPermissionsPolicy->InheritPolicy(
      AsElement()->OwnerDoc()->PermissionsPolicy());
  MaybeStoreCrossOriginPermissionsPolicy();
}
