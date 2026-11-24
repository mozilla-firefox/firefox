/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
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
#include "nsIClassOfService.h"
#include "nsIConsoleService.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIDocShell.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/dom/BindContext.h"
#include "mozilla/dom/Document.h"
#include "nsIExternalProtocolHandler.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIOService.h"
#include "nsIPermissionManager.h"
#include "nsPluginHost.h"
#include "nsIHttpChannel.h"
#include "nsINestedURI.h"
#include "nsScriptSecurityManager.h"
#include "nsIURILoader.h"
#include "nsIURL.h"
#include "nsIScriptChannel.h"
#include "nsIBlocklistService.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIAppShell.h"
#include "nsIXULRuntime.h"
#include "nsIScriptError.h"
#include "nsSubDocumentFrame.h"

#include "nsError.h"

// Util headers
#include "prenv.h"
#include "mozilla/Logging.h"

#include "nsCURILoader.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsDocShellCID.h"
#include "nsDocShellLoadState.h"
#include "nsGkAtoms.h"
#include "nsThreadUtils.h"
#include "nsNetUtil.h"
#include "nsMimeTypes.h"
#include "nsStyleUtil.h"
#include "nsUnicharUtils.h"
#include "mozilla/Preferences.h"
#include "nsSandboxFlags.h"
#include "nsQueryObject.h"

// Concrete classes
#include "nsFrameLoader.h"

#include "nsObjectLoadingContent.h"
#include "mozAutoDocUpdate.h"
#include "nsWrapperCacheInlines.h"
#include "nsDOMJSUtils.h"
#include "js/Object.h"  // JS::GetClass

#include "nsWidgetsCID.h"
#include "nsContentCID.h"
#include "mozilla/BasicEvents.h"
#include "mozilla/Components.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/PluginCrashedEvent.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/widget/IMEData.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/HTMLObjectElementBinding.h"
#include "mozilla/dom/HTMLEmbedElement.h"
#include "mozilla/dom/HTMLObjectElement.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/net/DocumentChannel.h"
#include "mozilla/net/UrlClassifierFeatureFactory.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_security.h"
#include "nsChannelClassifier.h"
#include "nsFocusManager.h"
#include "ReferrerInfo.h"
#include "nsIEffectiveTLDService.h"

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
  return nsPluginHost::GetSpecialType(aMIMEType) ==
         nsPluginHost::eSpecialType_Flash;
}

static bool IsPluginType(nsObjectLoadingContent::ObjectType type) {
  return type == nsObjectLoadingContent::eType_Fallback ||
         type == nsObjectLoadingContent::eType_FakePlugin;
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
void nsObjectLoadingContent::SetupFrameLoader(int32_t aJSPluginId) {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "must be a content");

  mFrameLoader =
      nsFrameLoader::Create(thisContent->AsElement(), mNetworkCreated);
  MOZ_ASSERT(mFrameLoader, "nsFrameLoader::Create failed");
}

// Helper to spawn the frameloader and return a pointer to its docshell.
already_AddRefed<nsIDocShell> nsObjectLoadingContent::SetupDocShell(
    nsIURI* aRecursionCheckURI) {
  SetupFrameLoader(nsFakePluginTag::NOT_JSPLUGIN);
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

void nsObjectLoadingContent::UnbindFromTree(bool aNullParent) {
  nsImageLoadingContent::UnbindFromTree(aNullParent);

  if (mType != eType_Image) {
    // nsImageLoadingContent handles the image case.
    // Reset state and clear pending events
    /// XXX(johns): The implementation for GenericFrame notes that ideally we
    ///             would keep the docshell around, but trash the frameloader
    UnloadObject();
  }
}

nsObjectLoadingContent::nsObjectLoadingContent()
    : mType(eType_Loading),
      mRunID(0),
      mHasRunID(false),
      mChannelLoaded(false),
      mNetworkCreated(true),
      mContentBlockingEnabled(false),
      mSkipFakePlugins(false),
      mIsStopping(false),
      mIsLoading(false),
      mScriptRequested(false),
      mRewrittenYoutubeEmbed(false),
      mLoadingSyntheticDocument(false) {}

nsObjectLoadingContent::~nsObjectLoadingContent() {
  // Should have been unbound from the tree at this point, and
  // CheckPluginStopEvent keeps us alive
  if (mFrameLoader) {
    MOZ_ASSERT_UNREACHABLE(
        "Should not be tearing down frame loaders at this point");
    mFrameLoader->Destroy();
  }

  nsImageLoadingContent::Destroy();
}

void nsObjectLoadingContent::GetPluginAttributes(
    nsTArray<MozPluginParameter>& aAttributes) {
  aAttributes = mCachedAttributes.Clone();
}

void nsObjectLoadingContent::GetPluginParameters(
    nsTArray<MozPluginParameter>& aParameters) {
  aParameters = mCachedParameters.Clone();
}

void nsObjectLoadingContent::GetNestedParams(
    nsTArray<MozPluginParameter>& aParameters) {
  nsCOMPtr<Element> ourElement =
      do_QueryInterface(static_cast<nsIObjectLoadingContent*>(this));

  nsCOMPtr<nsIHTMLCollection> allParams;
  constexpr auto xhtml_ns = u"http://www.w3.org/1999/xhtml"_ns;
  ErrorResult rv;
  allParams = ourElement->GetElementsByTagNameNS(xhtml_ns, u"param"_ns, rv);
  if (rv.Failed()) {
    return;
  }
  MOZ_ASSERT(allParams);

  uint32_t numAllParams = allParams->Length();
  for (uint32_t i = 0; i < numAllParams; i++) {
    RefPtr<Element> element = allParams->Item(i);

    nsAutoString name;
    element->GetAttr(nsGkAtoms::name, name);

    if (name.IsEmpty()) continue;

    nsCOMPtr<nsIContent> parent = element->GetParent();
    RefPtr<HTMLObjectElement> objectElement;
    while (!objectElement && parent) {
      objectElement = HTMLObjectElement::FromNode(parent);
      parent = parent->GetParent();
    }

    if (objectElement) {
      parent = objectElement;
    } else {
      continue;
    }

    if (parent == ourElement) {
      MozPluginParameter param;
      element->GetAttr(nsGkAtoms::name, param.mName);
      element->GetAttr(nsGkAtoms::value, param.mValue);

      param.mName.Trim(" \n\r\t\b", true, true, false);
      param.mValue.Trim(" \n\r\t\b", true, true, false);

      aParameters.AppendElement(param);
    }
  }
}

nsresult nsObjectLoadingContent::BuildParametersArray() {
  if (mCachedAttributes.Length() || mCachedParameters.Length()) {
    MOZ_ASSERT(false, "Parameters array should be empty.");
    return NS_OK;
  }

  nsCOMPtr<Element> element =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));

  for (uint32_t i = 0; i != element->GetAttrCount(); i += 1) {
    MozPluginParameter param;
    const nsAttrName* attrName = element->GetAttrNameAt(i);
    nsAtom* atom = attrName->LocalName();
    element->GetAttr(attrName->NamespaceID(), atom, param.mValue);
    atom->ToString(param.mName);
    mCachedAttributes.AppendElement(param);
  }

  nsAutoCString wmodeOverride;
  Preferences::GetCString("plugins.force.wmode", wmodeOverride);

  for (uint32_t i = 0; i < mCachedAttributes.Length(); i++) {
    if (!wmodeOverride.IsEmpty() &&
        mCachedAttributes[i].mName.EqualsIgnoreCase("wmode")) {
      CopyASCIItoUTF16(wmodeOverride, mCachedAttributes[i].mValue);
      wmodeOverride.Truncate();
    }
  }

  if (!wmodeOverride.IsEmpty()) {
    MozPluginParameter param;
    param.mName = u"wmode"_ns;
    CopyASCIItoUTF16(wmodeOverride, param.mValue);
    mCachedAttributes.AppendElement(param);
  }

  // Some plugins were never written to understand the "data" attribute of the
  // OBJECT tag. Real and WMP will not play unless they find a "src" attribute,
  // see bug 152334. Nav 4.x would simply replace the "data" with "src". Because
  // some plugins correctly look for "data", lets instead copy the "data"
  // attribute and add another entry to the bottom of the array if there isn't
  // already a "src" specified.
  if (element->IsHTMLElement(nsGkAtoms::object) &&
      !element->HasAttr(kNameSpaceID_None, nsGkAtoms::src)) {
    MozPluginParameter param;
    element->GetAttr(kNameSpaceID_None, nsGkAtoms::data, param.mValue);
    if (!param.mValue.IsEmpty()) {
      param.mName = u"SRC"_ns;
      mCachedAttributes.AppendElement(param);
    }
  }

  GetNestedParams(mCachedParameters);

  return NS_OK;
}

void nsObjectLoadingContent::NotifyOwnerDocumentActivityChanged() {
  // XXX(johns): We cannot touch plugins or run arbitrary script from this call,
  //             as Document is in a non-reentrant state.

  nsImageLoadingContent::NotifyOwnerDocumentActivityChanged();
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
  if (mType == eType_Document) {
    if (!mFinalListener) {
      MOZ_ASSERT_UNREACHABLE(
          "Already are eType_Document, but don't have final listener yet?");
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

      if (GetTypeOfContent(channelType, mSkipFakePlugins) != eType_Document) {
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
  if (mType != eType_Loading) {
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
    mContentBlockingEnabled = true;
    return NS_ERROR_FAILURE;
  }

  if (UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(status)) {
    mContentBlockingEnabled = true;
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
  if (UrlClassifierFeatureFactory::IsClassifierBlockingErrorCode(aStatusCode)) {
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

void nsObjectLoadingContent::PresetOpenerWindow(
    const Nullable<WindowProxyHolder>& aOpenerWindow, ErrorResult& aRv) {
  aRv.Throw(NS_ERROR_FAILURE);
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

NS_IMETHODIMP
nsObjectLoadingContent::GetContentTypeForMIMEType(const nsACString& aMIMEType,
                                                  uint32_t* aType) {
  *aType = GetTypeOfContent(PromiseFlatCString(aMIMEType), false);
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
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
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
    if (mType != eType_Document) {
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

// <public>
ElementState nsObjectLoadingContent::ObjectState() const {
  switch (mType) {
    case eType_Loading:
      return ElementState::LOADING;
    case eType_Image:
      return ImageState();
    case eType_FakePlugin:
    case eType_Document: {
      // These are OK. If documents start to load successfully, they display
      // something, and are thus not broken in this sense. The same goes for
      // plugins.
      ElementState states = ElementState();
      if (mLoadingSyntheticDocument) {
        states |= ElementState::LOADING;
        states |= ImageState();
      }
      return states;
    }
    case eType_Fallback:
      // This may end up handled as TYPE_NULL or as a "special" type, as
      // chosen by the layout.use-plugin-fallback pref.
      return ElementState();
    case eType_Null:
      return ElementState::BROKEN;
  }
  MOZ_ASSERT_UNREACHABLE("unknown type?");
  return ElementState::LOADING;
}

void nsObjectLoadingContent::MaybeRewriteYoutubeEmbed(nsIURI* aURI,
                                                      nsIURI* aBaseURI,
                                                      nsIURI** aRewrittenURI) {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "Must be an instance of content");

  // We're only interested in switching out embed and object tags
  if (!thisContent->NodeInfo()->Equals(nsGkAtoms::embed) &&
      !thisContent->NodeInfo()->Equals(nsGkAtoms::object)) {
    return;
  }

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
  nsAutoCString uri;
  nsresult rv = aURI->GetSpec(uri);
  if (NS_FAILED(rv)) {
    return;
  }

  // Some YouTube urls have parameters in path components, e.g.
  // http://youtube.com/embed/7LcUOEP7Brc&start=35. These URLs work with flash,
  // but break iframe/object embedding. If this situation occurs with rewritten
  // URLs, convert the parameters to query in order to make the video load
  // correctly as an iframe. In either case, warn about it in the
  // developer console.
  int32_t ampIndex = uri.FindChar('&', 0);
  bool replaceQuery = false;
  if (ampIndex != -1) {
    int32_t qmIndex = uri.FindChar('?', 0);
    if (qmIndex == -1 || qmIndex > ampIndex) {
      replaceQuery = true;
    }
  }

  // If we're pref'd off, return after telemetry has been logged.
  if (!Preferences::GetBool(kPrefYoutubeRewrite)) {
    return;
  }

  nsAutoString utf16OldURI = NS_ConvertUTF8toUTF16(uri);
  // If we need to convert the URL, it means an ampersand comes first.
  // Use the index we found earlier.
  if (replaceQuery) {
    // Replace question marks with ampersands.
    uri.ReplaceChar('?', '&');
    // Replace the first ampersand with a question mark.
    uri.SetCharAt('?', ampIndex);
  }
  // Switch out video access url formats, which should possibly allow HTML5
  // video loading.
  uri.ReplaceSubstring("/v/"_ns, "/embed/"_ns);
  nsAutoString utf16URI = NS_ConvertUTF8toUTF16(uri);
  rv = nsContentUtils::NewURIWithDocumentCharset(
      aRewrittenURI, utf16URI, thisContent->OwnerDoc(), aBaseURI);
  if (NS_FAILED(rv)) {
    return;
  }
  AutoTArray<nsString, 2> params = {utf16OldURI, utf16URI};
  const char* msgName;
  // If there's no query to rewrite, just notify in the developer console
  // that we're changing the embed.
  if (!replaceQuery) {
    msgName = "RewriteYouTubeEmbed";
  } else {
    msgName = "RewriteYouTubeEmbedPathParams";
  }
  nsContentUtils::ReportToConsole(
      nsIScriptError::warningFlag, "Plugins"_ns, thisContent->OwnerDoc(),
      nsContentUtils::eDOM_PROPERTIES, msgName, params);
}

bool nsObjectLoadingContent::CheckLoadPolicy(int16_t* aContentPolicy) {
  if (!aContentPolicy || !mURI) {
    MOZ_ASSERT_UNREACHABLE("Doing it wrong");
    return false;
  }

  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "Must be an instance of content");

  Document* doc = thisContent->OwnerDoc();

  nsContentPolicyType contentPolicyType = GetContentPolicyType();

  nsCOMPtr<nsILoadInfo> secCheckLoadInfo = new LoadInfo(
      doc->NodePrincipal(),  // loading principal
      doc->NodePrincipal(),  // triggering principal
      thisContent, nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
      contentPolicyType);

  *aContentPolicy = nsIContentPolicy::ACCEPT;
  nsresult rv = NS_CheckContentLoadPolicy(mURI, secCheckLoadInfo, mContentType,
                                          aContentPolicy,
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

  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "Must be an instance of content");

  Document* doc = thisContent->OwnerDoc();

  nsContentPolicyType objectType;
  switch (mType) {
    case eType_Image:
      objectType = nsIContentPolicy::TYPE_INTERNAL_IMAGE;
      break;
    case eType_Document:
      objectType = nsIContentPolicy::TYPE_DOCUMENT;
      break;
    case eType_Fallback:
    case eType_FakePlugin:
      objectType = GetContentPolicyType();
      break;
    default:
      MOZ_ASSERT_UNREACHABLE(
          "Calling checkProcessPolicy with an unloadable "
          "type");
      return false;
  }

  nsCOMPtr<nsILoadInfo> secCheckLoadInfo = new LoadInfo(
      doc->NodePrincipal(),  // loading principal
      doc->NodePrincipal(),  // triggering principal
      thisContent, nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
      objectType);

  *aContentPolicy = nsIContentPolicy::ACCEPT;
  nsresult rv = NS_CheckContentProcessPolicy(
      mURI ? mURI : mBaseURI, secCheckLoadInfo, mContentType, aContentPolicy,
      nsContentUtils::GetContentPolicy());
  NS_ENSURE_SUCCESS(rv, false);

  if (NS_CP_REJECTED(*aContentPolicy)) {
    LOG(("OBJLC [%p]: CheckContentProcessPolicy rejected load", this));
    return false;
  }

  return true;
}

nsObjectLoadingContent::ParameterUpdateFlags
nsObjectLoadingContent::UpdateObjectParameters() {
  nsCOMPtr<Element> thisElement =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  MOZ_ASSERT(thisElement, "Must be an Element");

  uint32_t caps = GetCapabilities();
  LOG(("OBJLC [%p]: Updating object parameters", this));

  nsresult rv;
  nsAutoCString newMime;
  nsAutoString typeAttr;
  nsCOMPtr<nsIURI> newURI;
  nsCOMPtr<nsIURI> newBaseURI;
  ObjectType newType;
  // Set if this state can't be used to load anything, forces eType_Null
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
  nsObjectLoadingContent::ParameterUpdateFlags retval = eParamNoChange;

  ///
  /// Initial MIME Type
  ///

  if (caps & eFallbackIfClassIDPresent) {
    nsAutoString classIDAttr;
    thisElement->GetAttr(kNameSpaceID_None, nsGkAtoms::classid, classIDAttr);
    // We don't support class ID plugin references, so we should always treat
    // having class Ids as attributes as invalid, and fallback accordingly.
    if (!classIDAttr.IsEmpty()) {
      newMime.Truncate();
      stateInvalid = true;
    }
  }

  ///
  /// Codebase
  ///

  nsAutoString codebaseStr;
  nsIURI* docBaseURI = thisElement->GetBaseURI();
  thisElement->GetAttr(kNameSpaceID_None, nsGkAtoms::codebase, codebaseStr);

  if (!codebaseStr.IsEmpty()) {
    rv = nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(newBaseURI), codebaseStr, thisElement->OwnerDoc(),
        docBaseURI);
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

  nsAutoString rawTypeAttr;
  thisElement->GetAttr(kNameSpaceID_None, nsGkAtoms::type, rawTypeAttr);
  if (!rawTypeAttr.IsEmpty()) {
    typeAttr = rawTypeAttr;
    nsAutoString params;
    nsAutoString mime;
    nsContentUtils::SplitMimeType(rawTypeAttr, mime, params);
    CopyUTF16toUTF8(mime, newMime);
  }

  ///
  /// URI
  ///

  nsAutoString uriStr;
  // Different elements keep this in various locations
  if (thisElement->NodeInfo()->Equals(nsGkAtoms::object)) {
    thisElement->GetAttr(kNameSpaceID_None, nsGkAtoms::data, uriStr);
  } else if (thisElement->NodeInfo()->Equals(nsGkAtoms::embed)) {
    thisElement->GetAttr(kNameSpaceID_None, nsGkAtoms::src, uriStr);
  } else {
    MOZ_ASSERT_UNREACHABLE("Unrecognized plugin-loading tag");
  }

  mRewrittenYoutubeEmbed = false;
  // Note that the baseURI changing could affect the newURI, even if uriStr did
  // not change.
  if (!uriStr.IsEmpty()) {
    rv = nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(newURI), uriStr, thisElement->OwnerDoc(), newBaseURI);
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
  bool newChannel = useChannel && mType == eType_Loading;

  RefPtr<DocumentChannel> documentChannel = do_QueryObject(mChannel);
  if (newChannel && documentChannel) {
    // If we've got a DocumentChannel which is marked as loaded using
    // `mChannelLoaded`, we are currently in the middle of a
    // `UpgradeLoadToDocument`.
    //
    // As we don't have the real mime-type from the channel, handle this by
    // using `newMime`.
    newMime = TEXT_HTML;

    MOZ_DIAGNOSTIC_ASSERT(
        GetTypeOfContent(newMime, mSkipFakePlugins) == eType_Document,
        "How is text/html not eType_Document?");
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

    ObjectType typeHint = newMime.IsEmpty()
                              ? eType_Null
                              : GetTypeOfContent(newMime, mSkipFakePlugins);

    //
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
    if (IsPluginType(typeHint)) {
      LOG(("OBJLC [%p]: Using plugin type hint in favor of any channel type",
           this));
      overrideChannelType = true;
    } else if (binaryChannelType && typeHint != eType_Null) {
      if (typeHint == eType_Document) {
        if (StaticPrefs::
                browser_opaqueResponseBlocking_syntheticBrowsingContext_AtStartup() &&
            imgLoader::SupportImageWithMimeType(newMime)) {
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

  ObjectType newMime_Type = GetTypeOfContent(newMime, mSkipFakePlugins);

  if (stateInvalid) {
    newType = eType_Null;
    LOG(("OBJLC [%p]: NewType #0: %s - %u", this, newMime.get(), newType));
    newMime.Truncate();
  } else if (newChannel) {
    // If newChannel is set above, we considered it in setting newMime
    newType = newMime_Type;
    LOG(("OBJLC [%p]: NewType #1: %s - %u", this, newMime.get(), newType));
    LOG(("OBJLC [%p]: Using channel type", this));
  } else if (((caps & eAllowPluginSkipChannel) || !newURI) &&
             IsPluginType(newMime_Type)) {
    newType = newMime_Type;
    LOG(("OBJLC [%p]: NewType #2: %s - %u", this, newMime.get(), newType));
    LOG(("OBJLC [%p]: Plugin type with no URI, skipping channel load", this));
  } else if (newURI &&
             (mOriginalContentType.IsEmpty() || newMime_Type != eType_Null)) {
    // We could potentially load this if we opened a channel on mURI, indicate
    // this by leaving type as loading.
    //
    // If a MIME type was requested in the tag, but we have decided to set load
    // type to null, ignore (otherwise we'll default to document type loading).
    newType = eType_Loading;
    LOG(("OBJLC [%p]: NewType #3: %u", this, newType));
  } else {
    // Unloadable - no URI, and no plugin/MIME type. Non-plugin types (images,
    // documents) always load with a channel.
    newType = eType_Null;
    LOG(("OBJLC [%p]: NewType #4: %u", this, newType));
  }

  mLoadingSyntheticDocument =
      newType == eType_Document &&
      StaticPrefs::
          browser_opaqueResponseBlocking_syntheticBrowsingContext_AtStartup() &&
      imgLoader::SupportImageWithMimeType(newMime);

  ///
  /// Handle existing channels
  ///

  if (useChannel && newType == eType_Loading) {
    // We decided to use a channel, and also that the previous channel is still
    // usable, so re-use the existing values.
    newType = mType;
    LOG(("OBJLC [%p]: NewType #5: %u", this, newType));
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
    LOG(("OBJLC [%p]: Type changed from %u -> %u", this, mType, newType));
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
  if (mType != eType_Loading && mContentType != newMime) {
    retval = (ParameterUpdateFlags)(retval | eParamStateChanged);
    retval = (ParameterUpdateFlags)(retval | eParamContentTypeChanged);
    LOG(("OBJLC [%p]: Object effective mime type changed (%s -> %s)", this,
         mContentType.get(), newMime.get()));
    mContentType = newMime;
  }

  // If we decided to keep using info from an old channel, but also that state
  // changed, we need to invalidate it.
  if (useChannel && !newChannel && (retval & eParamStateChanged)) {
    mType = eType_Loading;
    retval = (ParameterUpdateFlags)(retval | eParamChannelChanged);
  }

  return retval;
}

// Used by PluginDocument to kick off our initial load from the already-opened
// channel.
NS_IMETHODIMP
nsObjectLoadingContent::InitializeFromChannel(nsIRequest* aChannel) {
  LOG(("OBJLC [%p] InitializeFromChannel: %p", this, aChannel));
  if (mType != eType_Loading || mChannel) {
    // We could technically call UnloadObject() here, if consumers have a valid
    // reason for wanting to call this on an already-loaded tag.
    MOZ_ASSERT_UNREACHABLE("Should not have begun loading at this point");
    return NS_ERROR_UNEXPECTED;
  }

  // Because we didn't open this channel from an initial LoadObject, we'll
  // update our parameters now, so the OnStartRequest->LoadObject doesn't
  // believe our src/type suddenly changed.
  UpdateObjectParameters();
  // But we always want to load from a channel, in this case.
  mType = eType_Loading;
  mChannel = do_QueryInterface(aChannel);
  NS_ASSERTION(mChannel, "passed a request that is not a channel");

  // OnStartRequest will now see we have a channel in the loading state, and
  // call into LoadObject. There's a possibility LoadObject will decide not to
  // load anything from a channel - it will call CloseChannel() in that case.
  return NS_OK;
}

// Only OnStartRequest should be passing the channel parameter
nsresult nsObjectLoadingContent::LoadObject(bool aNotify, bool aForceLoad) {
  return LoadObject(aNotify, aForceLoad, nullptr);
}

nsresult nsObjectLoadingContent::LoadObject(bool aNotify, bool aForceLoad,
                                            nsIRequest* aLoadingChannel) {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "must be a content");
  Document* doc = thisContent->OwnerDoc();
  nsresult rv = NS_OK;

  // Per bug 1318303, if the parent document is not active, load the alternative
  // and return.
  if (!doc->IsCurrentActiveDocument()) {
    // Since this can be triggered on change of attributes, make sure we've
    // unloaded whatever is loaded first.
    UnloadObject();
    ObjectType oldType = mType;
    mType = eType_Fallback;
    ConfigureFallback();
    NotifyStateChanged(oldType, ObjectState(), true, false);
    return NS_OK;
  }

  // XXX(johns): In these cases, we refuse to touch our content and just
  //   remain unloaded, as per legacy behavior. It would make more sense to
  //   load fallback content initially and refuse to ever change state again.
  if (doc->IsBeingUsedAsImage()) {
    return NS_OK;
  }

  if (doc->IsLoadedAsData() && !doc->IsStaticDocument()) {
    return NS_OK;
  }
  if (doc->IsStaticDocument()) {
    // We only allow image loads in static documents, but we need to let the
    // eType_Loading state go through too while we do so.
    if (mType != eType_Image && mType != eType_Loading) {
      return NS_OK;
    }
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
  ElementState oldState = ObjectState();
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
  } else if (mType == eType_Null && mChannel) {
    // If we opened a channel but then failed to find a loadable state, throw it
    // away. mChannelLoaded will indicate that we tried to load a channel at one
    // point so we wont recurse
    CloseChannel();
  } else if (mType == eType_Loading && mChannel) {
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

  if (mType != eType_Null && mType != eType_Fallback) {
    bool allowLoad = true;
    int16_t contentPolicy = nsIContentPolicy::ACCEPT;
    // If mChannelLoaded is set we presumably already passed load policy
    // If mType == eType_Loading then we call OpenChannel() which internally
    // creates a new channel and calls asyncOpen() on that channel which
    // then enforces content policy checks.
    if (allowLoad && mURI && !mChannelLoaded && mType != eType_Loading) {
      allowLoad = CheckLoadPolicy(&contentPolicy);
    }
    // If we're loading a type now, check ProcessPolicy. Note that we may check
    // both now in the case of plugins whose type is determined before opening a
    // channel.
    if (allowLoad && mType != eType_Loading) {
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
      mType = eType_Null;
    }
  }

  // Don't allow view-source scheme.
  // view-source is the only scheme to which this applies at the moment due to
  // potential timing attacks to read data from cross-origin documents. If this
  // widens we should add a protocol flag for whether the scheme is only allowed
  // in top and use something like nsNetUtil::NS_URIChainHasFlags.
  if (mType != eType_Null) {
    nsCOMPtr<nsIURI> tempURI = mURI;
    nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(tempURI);
    while (nestedURI) {
      // view-source should always be an nsINestedURI, loop and check the
      // scheme on this and all inner URIs that are also nested URIs.
      if (tempURI->SchemeIs("view-source")) {
        LOG(("OBJLC [%p]: Blocking as effective URI has view-source scheme",
             this));
        mType = eType_Null;
        break;
      }

      nestedURI->GetInnerURI(getter_AddRefs(tempURI));
      nestedURI = do_QueryInterface(tempURI);
    }
  }

  // Items resolved as Image/Document are not candidates for content blocking,
  // as well as invalid plugins (they will not have the mContentType set).
  if ((mType == eType_Null || IsPluginType(mType)) && ShouldBlockContent()) {
    LOG(("OBJLC [%p]: Enable content blocking", this));
    mType = eType_Loading;
  }

  // Sanity check: We shouldn't have any loaded resources, pending events, or
  // a final listener at this point
  if (mFrameLoader || mFinalListener) {
    MOZ_ASSERT_UNREACHABLE("Trying to load new plugin with existing content");
    return NS_OK;
  }

  // More sanity-checking:
  // If mChannel is set, mChannelLoaded should be set, and vice-versa
  if (mType != eType_Null && !!mChannel != mChannelLoaded) {
    MOZ_ASSERT_UNREACHABLE("Trying to load with bad channel state");
    return NS_OK;
  }

  ///
  /// Attempt to load new type
  ///

  // Cache the current attributes and parameters.
  if (mType == eType_Null) {
    rv = BuildParametersArray();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // We don't set mFinalListener until OnStartRequest has been called, to
  // prevent re-entry ugliness with CloseChannel()
  nsCOMPtr<nsIStreamListener> finalListener;
  switch (mType) {
    case eType_Image:
      if (!mChannel) {
        // We have a LoadImage() call, but UpdateObjectParameters requires a
        // channel for images, so this is not a valid state.
        MOZ_ASSERT_UNREACHABLE("Attempting to load image without a channel?");
        rv = NS_ERROR_UNEXPECTED;
        break;
      }
      rv = LoadImageWithChannel(mChannel, getter_AddRefs(finalListener));
      // finalListener will receive OnStartRequest below
      break;
    case eType_Document: {
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

      rv = uriLoader->OpenChannel(mChannel, nsIURILoader::DONT_RETARGET, req,
                                  getter_AddRefs(finalListener));
      // finalListener will receive OnStartRequest either below, or if
      // `mChannel` is a `DocumentChannel`, it will be received after
      // RedirectToRealChannel.
    } break;
    case eType_Loading:
      // If our type remains Loading, we need a channel to proceed
      rv = OpenChannel();
      if (NS_FAILED(rv)) {
        LOG(("OBJLC [%p]: OpenChannel returned failure (%" PRIu32 ")", this,
             static_cast<uint32_t>(rv)));
      }
      break;
    case eType_Null:
    case eType_Fallback:
      // Handled below, silence compiler warnings
      break;
    case eType_FakePlugin:
      // We're now in the process of removing FakePlugin. See bug 1529133.
      MOZ_CRASH(
          "Shouldn't reach here! This means there's a fakeplugin trying to be "
          "loaded.");
  }

  //
  // Loaded, handle notifications and fallback
  //
  if (NS_FAILED(rv)) {
    // If we failed in the loading hunk above, switch to null (empty) region
    LOG(("OBJLC [%p]: Loading failed, switching to null", this));
    mType = eType_Null;
  }

  // If we didn't load anything, handle switching to fallback state
  if (mType == eType_Fallback || mType == eType_Null) {
    LOG(("OBJLC [%p]: Switching to fallback state", this));
    MOZ_ASSERT(!mFrameLoader, "switched to fallback but also loaded something");

    MaybeFireErrorEvent();

    if (mChannel) {
      // If we were loading with a channel but then failed over, throw it away
      CloseChannel();
    }

    // Don't try to initialize plugins or final listener below
    finalListener = nullptr;

    ConfigureFallback();
  }

  // Notify of our final state
  NotifyStateChanged(oldType, oldState, aNotify, false);
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
    NS_ASSERTION(mType != eType_Null && mType != eType_Loading,
                 "We should not have a final listener with a non-loaded type");
    mFinalListener = finalListener;

    // If we're a DocumentChannel load, hold off on firing the `OnStartRequest`
    // callback, as we haven't received it yet from our caller.
    RefPtr<DocumentChannel> documentChannel = do_QueryObject(mChannel);
    if (documentChannel) {
      MOZ_ASSERT(
          mType == eType_Document,
          "We have a DocumentChannel here but aren't loading a document?");
    } else {
      rv = finalListener->OnStartRequest(mChannel);
    }
  }

  if ((NS_FAILED(rv) && rv != NS_ERROR_PARSED_DATA_CACHED) && mIsLoading) {
    // Since we've already notified of our transition, we can just Unload and
    // call ConfigureFallback (which will notify again)
    oldType = mType;
    mType = eType_Fallback;
    UnloadObject(false);
    NS_ENSURE_TRUE(mIsLoading, NS_OK);
    CloseChannel();
    ConfigureFallback();
    NotifyStateChanged(oldType, ObjectState(), true, false);
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
  return NS_IsAboutBlank(aURI) && aInheritPrincipal &&
         (!mFrameLoader || !mFrameLoader->GetExistingDocShell() ||
          mFrameLoader->GetExistingDocShell()
              ->IsAboutBlankLoadOntoInitialAboutBlank(aURI, aInheritPrincipal,
                                                      aPrincipalToInherit));
}

nsresult nsObjectLoadingContent::OpenChannel() {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "must be a content");
  Document* doc = thisContent->OwnerDoc();
  NS_ASSERTION(doc, "No owner document?");

  nsresult rv;
  mChannel = nullptr;

  // E.g. mms://
  if (!mURI || !CanHandleURI(mURI)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsILoadGroup> group = doc->GetDocumentLoadGroup();
  nsCOMPtr<nsIChannel> chan;
  RefPtr<ObjectInterfaceRequestorShim> shim =
      new ObjectInterfaceRequestorShim(this);

  bool inheritAttrs = nsContentUtils::ChannelShouldInheritPrincipal(
      thisContent->NodePrincipal(),  // aLoadState->PrincipalToInherit()
      mURI,                          // aLoadState->URI()
      true,                          // aInheritForAboutBlank
      false);                        // aForceInherit

  bool inheritPrincipal = inheritAttrs && !SchemeIsData(mURI);

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

  // For object loads we store the CSP that potentially needs to
  // be inherited, e.g. in case we are loading an opaque origin
  // like a data: URI. The actual inheritance check happens within
  // Document::InitCSP(). Please create an actual copy of the CSP
  // (do not share the same reference) otherwise a Meta CSP of an
  // opaque origin will incorrectly be propagated to the embedding
  // document.
  RefPtr<nsCSPContext> cspToInherit;
  if (nsCOMPtr<nsIContentSecurityPolicy> csp = doc->GetCsp()) {
    cspToInherit = new nsCSPContext();
    cspToInherit->InitFromOther(static_cast<nsCSPContext*>(csp.get()));
  }

  // --- Create LoadInfo
  RefPtr<LoadInfo> loadInfo = new LoadInfo(
      /*aLoadingPrincipal = aLoadingContext->NodePrincipal() */ nullptr,
      /*aTriggeringPrincipal = aLoadingPrincipal */ nullptr,
      /*aLoadingContext = */ thisContent,
      /*aSecurityFlags = */ securityFlags,
      /*aContentPolicyType = */ contentPolicyType,
      /*aLoadingClientInfo = */ Nothing(),
      /*aController = */ Nothing(),
      /*aSandboxFlags = */ sandboxFlags);

  if (inheritAttrs) {
    loadInfo->SetPrincipalToInherit(thisContent->NodePrincipal());
  }

  if (cspToInherit) {
    loadInfo->SetCSPToInherit(cspToInherit);
  }

  if (DocumentChannel::CanUseDocumentChannel(mURI) &&
      !IsAboutBlankLoadOntoInitialAboutBlank(mURI, inheritPrincipal,
                                             thisContent->NodePrincipal())) {
    // --- Create LoadState
    RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(mURI);
    loadState->SetPrincipalToInherit(thisContent->NodePrincipal());
    loadState->SetTriggeringPrincipal(loadInfo->TriggeringPrincipal());
    if (cspToInherit) {
      loadState->SetCsp(cspToInherit);
    }
    loadState->SetTriggeringSandboxFlags(sandboxFlags);

    // TODO(djg): This was httpChan->SetReferrerInfoWithoutClone(referrerInfo);
    // Is the ...WithoutClone(...) important?
    auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);
    loadState->SetReferrerInfo(referrerInfo);

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

    if (inheritAttrs) {
      nsCOMPtr<nsILoadInfo> loadinfo = chan->LoadInfo();
      loadinfo->SetPrincipalToInherit(thisContent->NodePrincipal());
    }

    // For object loads we store the CSP that potentially needs to
    // be inherited, e.g. in case we are loading an opaque origin
    // like a data: URI. The actual inheritance check happens within
    // Document::InitCSP(). Please create an actual copy of the CSP
    // (do not share the same reference) otherwise a Meta CSP of an
    // opaque origin will incorrectly be propagated to the embedding
    // document.
    if (cspToInherit) {
      nsCOMPtr<nsILoadInfo> loadinfo = chan->LoadInfo();
      static_cast<LoadInfo*>(loadinfo.get())->SetCSPToInherit(cspToInherit);
    }
  };

  // Referrer
  nsCOMPtr<nsIHttpChannel> httpChan(do_QueryInterface(chan));
  if (httpChan) {
    auto referrerInfo = MakeRefPtr<ReferrerInfo>(*doc);

    rv = httpChan->SetReferrerInfoWithoutClone(referrerInfo);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    // Set the initiator type
    nsCOMPtr<nsITimedChannel> timedChannel(do_QueryInterface(httpChan));
    if (timedChannel) {
      timedChannel->SetInitiatorType(thisContent->LocalName());
    }

    nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(httpChan));
    if (cos && UserActivation::IsHandlingUserInput()) {
      cos->AddClassFlags(nsIClassOfService::UrgentStart);
    }
  }

  nsCOMPtr<nsIScriptChannel> scriptChannel = do_QueryInterface(chan);
  if (scriptChannel) {
    // Allow execution against our context if the principals match
    scriptChannel->SetExecutionPolicy(nsIScriptChannel::EXECUTE_NORMAL);
  }

  // AsyncOpen can fail if a file does not exist.
  rv = chan->AsyncOpen(shim);
  NS_ENSURE_SUCCESS(rv, rv);
  LOG(("OBJLC [%p]: Channel opened", this));
  mChannel = chan;
  return NS_OK;
}

uint32_t nsObjectLoadingContent::GetCapabilities() const {
  return eSupportImages | eSupportPlugins | eSupportDocuments;
}

void nsObjectLoadingContent::Destroy() {
  // Reset state so that if the element is re-appended to tree again (e.g.
  // adopting to another document), it will reload resource again.
  UnloadObject();

  nsImageLoadingContent::Destroy();
}

/* static */
void nsObjectLoadingContent::Traverse(nsObjectLoadingContent* tmp,
                                      nsCycleCollectionTraversalCallback& cb) {
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFrameLoader);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFeaturePolicy);
}

/* static */
void nsObjectLoadingContent::Unlink(nsObjectLoadingContent* tmp) {
  if (tmp->mFrameLoader) {
    tmp->mFrameLoader->Destroy();
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFrameLoader);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFeaturePolicy);
}

void nsObjectLoadingContent::UnloadObject(bool aResetState) {
  // Don't notify in CancelImageRequests until we transition to a new loaded
  // state, but not if we've loaded the image in a synthetic browsing context.
  CancelImageRequests(false);
  if (mFrameLoader) {
    RefPtr<nsFrameLoader> loader = std::move(mFrameLoader);
    loader->Destroy();
  }

  if (aResetState) {
    CloseChannel();
    mChannelLoaded = false;
    mType = eType_Loading;
    mURI = mOriginalURI = mBaseURI = nullptr;
    mContentType.Truncate();
    mOriginalContentType.Truncate();
  }

  mScriptRequested = false;

  mIsStopping = false;

  mCachedAttributes.Clear();
  mCachedParameters.Clear();

  mSubdocumentIntrinsicSize.reset();
  mSubdocumentIntrinsicRatio.reset();
}

void nsObjectLoadingContent::NotifyStateChanged(ObjectType aOldType,
                                                ElementState aOldState,
                                                bool aNotify,
                                                bool aForceRestyle) {
  LOG(("OBJLC [%p]: NotifyStateChanged: (%u, %" PRIx64 ") -> (%u, %" PRIx64 ")"
       " (notify %i)",
       this, aOldType, aOldState.GetInternalValue(), mType,
       ObjectState().GetInternalValue(), aNotify));

  nsCOMPtr<dom::Element> thisEl = AsContent()->AsElement();
  MOZ_ASSERT(thisEl, "must be an element");

  // XXX(johns): A good bit of the code below replicates UpdateState(true)

  // Unfortunately, we do some state changes without notifying
  // (e.g. in Fallback when canceling image requests), so we have to
  // manually notify object state changes.
  thisEl->UpdateState(aForceRestyle);

  if (!aNotify) {
    // We're done here
    return;
  }

  Document* doc = thisEl->GetComposedDoc();
  if (!doc) {
    return;  // Nothing to do
  }

  const ElementState newState = ObjectState();
  if (newState == aOldState && mType == aOldType) {
    return;  // Also done.
  }

  RefPtr<PresShell> presShell = doc->GetPresShell();
  // If there is no PresShell or it hasn't been initialized there isn't much to
  // do.
  if (!presShell || !presShell->DidInitialize()) {
    return;
  }

  if (presShell && (aOldType != mType)) {
    presShell->PostRecreateFramesFor(thisEl);
  }
}

nsObjectLoadingContent::ObjectType nsObjectLoadingContent::GetTypeOfContent(
    const nsCString& aMIMEType, bool aNoFakePlugin) {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "must be a content");

  // Images, documents and (fake) plugins are always supported.
  MOZ_ASSERT(GetCapabilities() &
             (eSupportImages | eSupportDocuments | eSupportPlugins));

  LOG(
      ("OBJLC [%p]: calling HtmlObjectContentTypeForMIMEType: aMIMEType: %s - "
       "thisContent: %p\n",
       this, aMIMEType.get(), thisContent.get()));
  auto ret =
      static_cast<ObjectType>(nsContentUtils::HtmlObjectContentTypeForMIMEType(
          aMIMEType, aNoFakePlugin));
  LOG(("OBJLC [%p]: called HtmlObjectContentTypeForMIMEType\n", this));
  return ret;
}

void nsObjectLoadingContent::CreateStaticClone(
    nsObjectLoadingContent* aDest) const {
  aDest->mType = mType;

  if (mFrameLoader) {
    nsCOMPtr<nsIContent> content =
        do_QueryInterface(static_cast<nsIImageLoadingContent*>(aDest));
    Document* doc = content->OwnerDoc();
    if (doc->IsStaticDocument()) {
      doc->AddPendingFrameStaticClone(aDest, mFrameLoader);
    }
  }
}

NS_IMETHODIMP
nsObjectLoadingContent::GetSrcURI(nsIURI** aURI) {
  NS_IF_ADDREF(*aURI = GetSrcURI());
  return NS_OK;
}

void nsObjectLoadingContent::ConfigureFallback() {
  MOZ_ASSERT(!mFrameLoader && !mChannel,
             "ConfigureFallback called with loaded content");

  // We only fallback in special cases where we are already of fallback
  // type (e.g. removed Flash plugin use) or where something went wrong
  // (e.g. unknown MIME type).
  MOZ_ASSERT(mType == eType_Fallback || mType == eType_Null);

  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  NS_ASSERTION(thisContent, "must be a content");

  // There are two types of fallback:
  // 1. HTML fallbacks are children of the <object> or <embed> DOM element.
  // 2. The special transparent region fallback replacing Flash use.
  // If our type is eType_Fallback (e.g. Flash use) then we use #1 if
  // available, otherwise we use #2.
  // If our type is eType_Null (e.g. unknown MIME type) then we use
  // #1, otherwise the element has no size.
  bool hasHtmlFallback = false;
  if (thisContent->IsHTMLElement(nsGkAtoms::object)) {
    // Do a depth-first traverse of node tree with the current element as root,
    // looking for non-<param> elements.  If we find some then we have an HTML
    // fallback for this element
    AutoTArray<RefPtr<nsIContent>, 4> targets;
    for (nsIContent* child = thisContent->GetFirstChild(); child;) {
      hasHtmlFallback =
          hasHtmlFallback || (!child->IsHTMLElement(nsGkAtoms::param) &&
                              nsStyleUtil::IsSignificantChild(child, false));

      // <object> and <embed> elements in the fallback need to StartObjectLoad.
      // Their children should be ignored since they are part of those
      // element's fallback.
      if (child->IsAnyOfHTMLElements(nsGkAtoms::embed, nsGkAtoms::object)) {
        targets.AppendElement(child);
        // Skip the children
        child = child->GetNextNonChildNode(thisContent);
      } else {
        child = child->GetNextNode(thisContent);
      }
    }

    for (RefPtr<nsIContent>& target : targets) {
      if (!target->IsInclusiveDescendantOf(thisContent)) {
        continue;
      }
      if (auto* embed = HTMLEmbedElement::FromNode(target)) {
        embed->StartObjectLoad(true, true);
      } else if (auto* object = HTMLObjectElement::FromNode(target)) {
        object->StartObjectLoad(true, true);
      }
    }
  }

  // If we find an HTML fallback then we always switch type to null.
  if (hasHtmlFallback) {
    mType = eType_Null;
  }
}

NS_IMETHODIMP
nsObjectLoadingContent::Reload(bool aClearActivation) {
  if (aClearActivation) {
    mSkipFakePlugins = false;
  }

  return LoadObject(true, true);
}

NS_IMETHODIMP
nsObjectLoadingContent::SkipFakePlugins() {
  if (!nsContentUtils::IsCallerChrome()) return NS_ERROR_NOT_AVAILABLE;

  mSkipFakePlugins = true;

  // If we're showing a fake plugin now, reload
  if (mType == eType_FakePlugin) {
    return LoadObject(true, true);
  }

  return NS_OK;
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
  if (mType != eType_Loading) {
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
  // sure that that browsing context gets the correct container feature policy.
  // This is needed for `DocumentLoadListener::MaybeTriggerProcessSwitch` to be
  // able to start loading the document with the correct container feature
  // policy in the load info.
  RefreshFeaturePolicy();

  bc.forget(aBrowsingContext);
  return NS_OK;
}

uint32_t nsObjectLoadingContent::GetRunID(SystemCallerGuarantee,
                                          ErrorResult& aRv) {
  if (!mHasRunID) {
    // The plugin instance must not have a run ID, so we must
    // be running the plugin in-process.
    aRv.Throw(NS_ERROR_NOT_IMPLEMENTED);
    return 0;
  }
  return mRunID;
}

bool nsObjectLoadingContent::ShouldBlockContent() {
  if (mContentBlockingEnabled && mURI && IsFlashMIME(mContentType) &&
      StaticPrefs::browser_safebrowsing_blockedURIs_enabled()) {
    return true;
  }

  return false;
}

Document* nsObjectLoadingContent::GetContentDocument(
    nsIPrincipal& aSubjectPrincipal) {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));

  if (!thisContent->IsInComposedDoc()) {
    return nullptr;
  }

  Document* sub_doc = thisContent->OwnerDoc()->GetSubDocumentFor(thisContent);
  if (!sub_doc) {
    return nullptr;
  }

  // Return null for cross-origin contentDocument.
  if (!aSubjectPrincipal.SubsumesConsideringDomain(sub_doc->NodePrincipal())) {
    return nullptr;
  }

  return sub_doc;
}

bool nsObjectLoadingContent::DoResolve(
    JSContext* aCx, JS::Handle<JSObject*> aObject, JS::Handle<jsid> aId,
    JS::MutableHandle<mozilla::Maybe<JS::PropertyDescriptor>> aDesc) {
  return true;
}

/* static */
bool nsObjectLoadingContent::MayResolve(jsid aId) {
  // We can resolve anything, really.
  return true;
}

void nsObjectLoadingContent::GetOwnPropertyNames(
    JSContext* aCx, JS::MutableHandleVector<jsid> /* unused */,
    bool /* unused */, ErrorResult& aRv) {}

void nsObjectLoadingContent::MaybeFireErrorEvent() {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));
  // Queue a task to fire an error event if we're an <object> element.  The
  // queueing is important, since then we don't have to worry about reentry.
  if (thisContent->IsHTMLElement(nsGkAtoms::object)) {
    RefPtr<AsyncEventDispatcher> loadBlockingAsyncDispatcher =
        new LoadBlockingAsyncEventDispatcher(
            thisContent, u"error"_ns, CanBubble::eNo, ChromeOnlyDispatch::eNo);
    loadBlockingAsyncDispatcher->PostDOMEvent();
  }
}

bool nsObjectLoadingContent::BlockEmbedOrObjectContentLoading() {
  nsCOMPtr<nsIContent> thisContent =
      do_QueryInterface(static_cast<nsIImageLoadingContent*>(this));

  // Traverse up the node tree to see if we have any ancestors that may block us
  // from loading
  for (nsIContent* parent = thisContent->GetParent(); parent;
       parent = parent->GetParent()) {
    if (parent->IsAnyOfHTMLElements(nsGkAtoms::video, nsGkAtoms::audio)) {
      return true;
    }
    // If we have an ancestor that is an object with a source, it'll have an
    // associated displayed type. If that type is not null, don't load content
    // for the embed.
    if (HTMLObjectElement* object = HTMLObjectElement::FromNode(parent)) {
      uint32_t type = object->DisplayedType();
      if (type != eType_Null) {
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

  if (nsSubDocumentFrame* sdf = do_QueryFrame(AsContent()->GetPrimaryFrame())) {
    sdf->SubdocumentIntrinsicSizeOrRatioChanged();
  }
}

void nsObjectLoadingContent::SubdocumentImageLoadComplete(nsresult aResult) {
  ElementState oldState = ObjectState();
  ObjectType oldType = mType;
  mLoadingSyntheticDocument = false;

  if (NS_FAILED(aResult)) {
    UnloadObject();
    mType = eType_Fallback;
    ConfigureFallback();
    NotifyStateChanged(oldType, oldState, true, false);
    return;
  }

  // (mChannelLoaded && mChannel) indicates this is a good state, not any sort
  // of failures.
  MOZ_DIAGNOSTIC_ASSERT_IF(mChannelLoaded && mChannel, mType == eType_Document);

  NotifyStateChanged(oldType, oldState, true, true);
}

void nsObjectLoadingContent::MaybeStoreCrossOriginFeaturePolicy() {
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

  Element* el = AsContent()->AsElement();

  if (!el->IsInComposedDoc()) {
    return;
  }

  if (ContentChild* cc = ContentChild::GetSingleton()) {
    Unused << cc->SendSetContainerFeaturePolicy(
        browsingContext, Some(mFeaturePolicy->ToFeaturePolicyInfo()));
  }
}

/* static */ already_AddRefed<nsIPrincipal>
nsObjectLoadingContent::GetFeaturePolicyDefaultOrigin(nsINode* aNode) {
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

void nsObjectLoadingContent::RefreshFeaturePolicy() {
  if (mType != ObjectType::eType_Document) {
    return;
  }

  if (!mFeaturePolicy) {
    mFeaturePolicy = MakeAndAddRef<FeaturePolicy>(AsContent()->AsElement());
  }

  // The origin can change if 'src' or 'data' attributes change.
  nsCOMPtr<nsIPrincipal> origin =
      GetFeaturePolicyDefaultOrigin(AsContent()->AsElement());
  MOZ_ASSERT(origin);
  mFeaturePolicy->SetDefaultOrigin(origin);

  mFeaturePolicy->InheritPolicy(
      AsContent()->AsElement()->OwnerDoc()->FeaturePolicy());
  MaybeStoreCrossOriginFeaturePolicy();
}
