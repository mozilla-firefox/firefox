/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Base class for all our document implementations.
 */

#include "mozilla/dom/Document.h"
#include "mozilla/dom/DocumentInlines.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <type_traits>
#include "Attr.h"
#include "ErrorList.h"
#include "ExpandedPrincipal.h"
#include "MainThreadUtils.h"
#include "MobileViewportManager.h"
#include "NSSErrorsService.h"
#include "NodeUbiReporting.h"
#include "PLDHashTable.h"
#include "StorageAccessPermissionRequest.h"
#include "ThirdPartyUtil.h"
#include "domstubs.h"
#include "gfxPlatform.h"
#include "imgIContainer.h"
#include "imgLoader.h"
#include "imgRequestProxy.h"
#include "js/Value.h"
#include "jsapi.h"
#include "mozAutoDocUpdate.h"
#include "mozIDOMWindow.h"
#include "mozIThirdPartyUtil.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/ArrayIterator.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Base64.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/BounceTrackingProtection.h"
#include "mozilla/CSSEnabledState.h"
#include "mozilla/ContentBlockingAllowList.h"
#include "mozilla/ContentBlockingNotifier.h"
#include "mozilla/ContentBlockingUserInteraction.h"
#include "mozilla/ContentPrincipal.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/AttributeStyles.h"
#include "mozilla/DocumentStyleRootIterator.h"
#include "mozilla/EditorBase.h"
#include "mozilla/EditorCommands.h"
#include "mozilla/Encoding.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/EventQueue.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/FullscreenChange.h"
#include "mozilla/GlobalStyleSheetCache.h"
#include "mozilla/MappedDeclarationsBuilder.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/IdentifierMapEntry.h"
#include "mozilla/InputTaskManager.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/InternalMutationEvent.h"
#include "mozilla/Likely.h"
#include "mozilla/Logging.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MacroForEach.h"
#include "mozilla/Maybe.h"
#include "mozilla/MediaFeatureChange.h"
#include "mozilla/MediaManager.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/NeverDestroyed.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/PendingFullscreenEvent.h"
#include "mozilla/PermissionDelegateHandler.h"
#include "mozilla/PermissionManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/PreloadHashKey.h"
#include "mozilla/PresShell.h"
#include "mozilla/PresShellForwards.h"
#include "mozilla/PresShellInlines.h"
#include "mozilla/PseudoStyleType.h"
#include "mozilla/RefCountType.h"
#include "mozilla/RelativeTo.h"
#include "mozilla/RestyleManager.h"
#include "mozilla/ReverseIterator.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScrollTimelineAnimationTracker.h"
#include "mozilla/SMILAnimationController.h"
#include "mozilla/SMILTimeContainer.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/Components.h"
#include "mozilla/SVGUtils.h"
#include "mozilla/ServoStyleConsts.h"
#include "mozilla/ServoTypes.h"
#include "mozilla/SizeOfState.h"
#include "mozilla/Span.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticAnalysisFunctions.h"
#include "mozilla/StaticPrefs_apz.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_docshell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StaticPrefs_full_screen_api.h"
#include "mozilla/StaticPrefs_layout.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_page_load.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StaticPrefs_widget.h"
#include "mozilla/StaticPresData.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/TelemetryScalarEnums.h"
#include "mozilla/TextControlElement.h"
#include "mozilla/TextEditor.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/URLDecorationStripper.h"
#include "mozilla/URLExtraData.h"
#include "mozilla/Unused.h"
#include "mozilla/css/ImageLoader.h"
#include "mozilla/css/Loader.h"
#include "mozilla/css/Rule.h"
#include "mozilla/css/SheetParsingMode.h"
#include "mozilla/dom/AnonymousContent.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/CanvasRenderingContextHelper.h"
#include "mozilla/dom/CDATASection.h"
#include "mozilla/dom/CSPDictionariesBinding.h"
#include "mozilla/dom/ChromeObserver.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ClientState.h"
#include "mozilla/dom/CloseWatcherManager.h"
#include "mozilla/dom/Comment.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/CSSBinding.h"
#include "mozilla/dom/CSSCustomPropertyRegisteredEvent.h"
#include "mozilla/dom/DOMImplementation.h"
#include "mozilla/dom/DOMIntersectionObserver.h"
#include "mozilla/dom/DOMStringList.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/DocumentBinding.h"
#include "mozilla/dom/DocumentFragment.h"
#include "mozilla/dom/DocumentL10n.h"
#include "mozilla/dom/DocumentTimeline.h"
#include "mozilla/dom/DocumentType.h"
#include "mozilla/dom/Sanitizer.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/ErrorEvent.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/EventListenerBinding.h"
#include "mozilla/dom/FailedCertSecurityInfoBinding.h"
#include "mozilla/dom/FeaturePolicy.h"
#include "mozilla/dom/FeaturePolicyUtils.h"
#include "mozilla/dom/FontFaceSet.h"
#include "mozilla/dom/FragmentDirective.h"
#include "mozilla/dom/NavigationBinding.h"
#include "mozilla/dom/fragmentdirectives_ffi_generated.h"
#include "mozilla/dom/FromParser.h"
#include "mozilla/dom/HighlightRegistry.h"
#include "mozilla/dom/HTMLAllCollection.h"
#include "mozilla/dom/HTMLBodyElement.h"
#include "mozilla/dom/HTMLCollectionBinding.h"
#include "mozilla/dom/HTMLDialogElement.h"
#include "mozilla/dom/HTMLEmbedElement.h"
#include "mozilla/dom/HTMLFormElement.h"
#include "mozilla/dom/HTMLIFrameElement.h"
#include "mozilla/dom/HTMLImageElement.h"
#include "mozilla/dom/HTMLInputElement.h"
#include "mozilla/dom/HTMLLinkElement.h"
#include "mozilla/dom/HTMLMediaElement.h"
#include "mozilla/dom/HTMLMetaElement.h"
#include "mozilla/dom/HTMLObjectElement.h"
#include "mozilla/dom/HTMLSharedElement.h"
#include "mozilla/dom/HTMLTextAreaElement.h"
#include "mozilla/dom/ImageTracker.h"
#include "mozilla/dom/InspectorUtils.h"
#include "mozilla/dom/InteractiveWidget.h"
#include "mozilla/dom/Link.h"
#include "mozilla/dom/MediaQueryList.h"
#include "mozilla/dom/MediaSource.h"
#include "mozilla/dom/MutationObservers.h"
#include "mozilla/dom/NameSpaceConstants.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/NetErrorInfoBinding.h"
#include "mozilla/dom/NodeInfo.h"
#include "mozilla/dom/NodeIterator.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/dom/PContentChild.h"
#include "mozilla/dom/PWindowGlobalChild.h"
#include "mozilla/dom/PageLoadEventUtils.h"
#include "mozilla/dom/PageTransitionEvent.h"
#include "mozilla/dom/PageTransitionEventBinding.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/PostMessageEvent.h"
#include "mozilla/dom/ProcessingInstruction.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseNativeHandler.h"
#include "mozilla/dom/RemoteBrowser.h"
#include "mozilla/dom/ResizeObserver.h"
#include "mozilla/dom/RustTypes.h"
#include "mozilla/dom/SVGElement.h"
#include "mozilla/dom/SVGDocument.h"
#include "mozilla/dom/SVGSVGElement.h"
#include "mozilla/dom/SVGUseElement.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/Selection.h"
#include "mozilla/dom/ServiceWorkerContainer.h"
#include "mozilla/dom/ServiceWorkerDescriptor.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/dom/ShadowIncludingTreeIterator.h"
#include "mozilla/dom/ShadowRoot.h"
#include "mozilla/dom/StyleSheetApplicableStateChangeEvent.h"
#include "mozilla/dom/StyleSheetApplicableStateChangeEventBinding.h"
#include "mozilla/dom/StyleSheetList.h"
#include "mozilla/dom/StyleSheetRemovedEvent.h"
#include "mozilla/dom/StyleSheetRemovedEventBinding.h"
#include "mozilla/dom/TimeoutManager.h"
#include "mozilla/dom/ToggleEvent.h"
#include "mozilla/dom/Touch.h"
#include "mozilla/dom/TouchEvent.h"
#include "mozilla/dom/TreeOrderedArrayInlines.h"
#include "mozilla/dom/TreeWalker.h"
#include "mozilla/dom/TrustedHTML.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/dom/URL.h"
#include "mozilla/dom/UseCounterMetrics.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/dom/WakeLockJS.h"
#include "mozilla/dom/WakeLockSentinel.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowProxyHolder.h"
#include "mozilla/dom/WorkerDocumentListener.h"
#include "mozilla/dom/XPathEvaluator.h"
#include "mozilla/dom/XPathExpression.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/fallible.h"
#include "mozilla/gfx/BaseCoord.h"
#include "mozilla/gfx/BaseSize.h"
#include "mozilla/gfx/Coord.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/gfx/ScaleFactor.h"
#include "mozilla/glean/DomMetrics.h"
#include "mozilla/glean/DomUseCounterMetrics.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/ipc/IdleSchedulerChild.h"
#include "mozilla/ipc/MessageChannel.h"
#include "mozilla/net/ChannelEventQueue.h"
#include "mozilla/net/Cookie.h"
#include "mozilla/net/CookieCommons.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/CookieParser.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/net/RequestContextService.h"
#include "nsAboutProtocolUtils.h"
#include "nsAttrValue.h"
#include "nsAttrValueInlines.h"
#include "nsBaseHashtable.h"
#include "nsBidiUtils.h"
#include "nsCRT.h"
#include "nsCSSPropertyID.h"
#include "nsCSSProps.h"
#include "nsCSSPseudoElements.h"
#include "nsCSSRendering.h"
#include "nsCanvasFrame.h"
#include "nsCaseTreatment.h"
#include "nsCharsetSource.h"
#include "nsCommandManager.h"
#include "nsCommandParams.h"
#include "nsComponentManagerUtils.h"
#include "nsContentCreatorFunctions.h"
#include "nsContentList.h"
#include "nsContentPermissionHelper.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsCoord.h"
#include "nsCycleCollectionNoteChild.h"
#include "nsCycleCollectionTraversalCallback.h"
#include "nsDOMAttributeMap.h"
#include "nsDOMCaretPosition.h"
#include "nsDOMNavigationTiming.h"
#include "nsDOMString.h"
#include "nsDeviceContext.h"
#include "nsDocShell.h"
#include "nsDocShellLoadTypes.h"
#include "nsError.h"
#include "nsEscape.h"
#include "nsFocusManager.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsGenericHTMLElement.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "nsHTMLDocument.h"
#include "nsHtml5Module.h"
#include "nsHtml5Parser.h"
#include "nsHtml5TreeOpExecutor.h"
#include "nsIAsyncShutdown.h"
#include "nsIAuthPrompt.h"
#include "nsIAuthPrompt2.h"
#include "nsIBFCacheEntry.h"
#include "nsIBaseWindow.h"
#include "nsIBrowserChild.h"
#include "nsIBrowserUsage.h"
#include "nsICSSLoaderObserver.h"
#include "nsICategoryManager.h"
#include "nsICertOverrideService.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIContentPolicy.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIContentSink.h"
#include "nsICookieJarSettings.h"
#include "nsICookieService.h"
#include "nsIDOMXULCommandDispatcher.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocumentActivity.h"
#include "nsIDocumentEncoder.h"
#include "nsIDocumentLoader.h"
#include "nsIDocumentLoaderFactory.h"
#include "nsIDocumentObserver.h"
#include "nsIDNSService.h"
#include "nsIEditingSession.h"
#include "nsIEditor.h"
#include "nsIEffectiveTLDService.h"
#include "nsIFile.h"
#include "nsIFileChannel.h"
#include "nsIFrame.h"
#include "nsIGlobalObject.h"
#include "nsIHTMLCollection.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIIOService.h"
#include "nsIImageLoadingContent.h"
#include "nsIInlineSpellChecker.h"
#include "nsIInputStreamChannel.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILayoutHistoryState.h"
#include "nsIMultiPartChannel.h"
#include "nsIMutationObserver.h"
#include "nsINSSErrorsService.h"
#include "nsINamed.h"
#include "nsINodeList.h"
#include "nsIObjectLoadingContent.h"
#include "nsIObserverService.h"
#include "nsIPermission.h"
#include "nsIPrompt.h"
#include "nsIPropertyBag2.h"
#include "nsIPublicKeyPinningService.h"
#include "nsIReferrerInfo.h"
#include "nsIRefreshURI.h"
#include "nsIRequest.h"
#include "nsIRequestContext.h"
#include "nsIRunnable.h"
#include "nsISHEntry.h"
#include "nsIScriptElement.h"
#include "nsIScriptError.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptSecurityManager.h"
#include "nsISecurityConsoleMessage.h"
#include "nsISelectionController.h"
#include "nsISerialEventTarget.h"
#include "nsISimpleEnumerator.h"
#include "nsISiteSecurityService.h"
#include "nsISocketProvider.h"
#include "nsISpeculativeConnect.h"
#include "nsIStructuredCloneContainer.h"
#include "nsIThread.h"
#include "nsITimedChannel.h"
#include "nsITimer.h"
#include "nsITransportSecurityInfo.h"
#include "nsIURIMutator.h"
#include "nsIVariant.h"
#include "nsIWeakReference.h"
#include "nsIWebNavigation.h"
#include "nsIWidget.h"
#include "nsIX509Cert.h"
#include "nsIX509CertValidity.h"
#include "nsIXMLContentSink.h"
#include "nsIHTMLContentSink.h"
#include "nsIXULRuntime.h"
#include "nsImageLoadingContent.h"
#include "nsImportModule.h"
#include "nsLanguageAtomService.h"
#include "nsLayoutUtils.h"
#include "nsMimeTypes.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsNodeInfoManager.h"
#include "nsObjectLoadingContent.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPIWindowRoot.h"
#include "nsPoint.h"
#include "nsPointerHashKeys.h"
#include "nsPresContext.h"
#include "nsQueryFrame.h"
#include "nsQueryObject.h"
#include "nsRange.h"
#include "nsRect.h"
#include "nsRefreshDriver.h"
#include "nsSandboxFlags.h"
#include "nsSerializationHelper.h"
#include "nsServiceManagerUtils.h"
#include "nsStringFlags.h"
#include "nsStyleUtil.h"
#include "nsStringIterator.h"
#include "nsStyleSheetService.h"
#include "nsStyleStruct.h"
#include "nsTextControlFrame.h"
#include "nsSubDocumentFrame.h"
#include "nsTextNode.h"
#include "nsURLHelper.h"
#include "nsUnicharUtils.h"
#include "nsWrapperCache.h"
#include "nsWrapperCacheInlines.h"
#include "nsXPCOMCID.h"
#include "nsXULAppAPI.h"
#include "prthread.h"
#include "prtime.h"
#include "prtypes.h"
#include "xpcpublic.h"

// XXX Must be included after mozilla/Encoding.h
#include "encoding_rs.h"

#include "mozilla/dom/XULBroadcastManager.h"
#include "mozilla/dom/XULPersist.h"
#include "nsIAppWindow.h"
#include "nsXULPrototypeDocument.h"
#include "nsXULCommandDispatcher.h"
#include "nsXULPopupManager.h"
#include "nsIDocShellTreeOwner.h"

#define XML_DECLARATION_BITS_DECLARATION_EXISTS (1 << 0)
#define XML_DECLARATION_BITS_ENCODING_EXISTS (1 << 1)
#define XML_DECLARATION_BITS_STANDALONE_EXISTS (1 << 2)
#define XML_DECLARATION_BITS_STANDALONE_YES (1 << 3)

#define NS_MAX_DOCUMENT_WRITE_DEPTH 20

mozilla::LazyLogModule gPageCacheLog("PageCache");
mozilla::LazyLogModule gSHIPBFCacheLog("SHIPBFCache");
mozilla::LazyLogModule gTimeoutDeferralLog("TimeoutDefer");
mozilla::LazyLogModule gUseCountersLog("UseCounters");

namespace mozilla {

using namespace net;

namespace dom {

class Document::HeaderData {
 public:
  HeaderData(nsAtom* aField, const nsAString& aData)
      : mField(aField), mData(aData) {}

  ~HeaderData() {
    // Delete iteratively to avoid blowing up the stack, though it shouldn't
    // happen in practice.
    UniquePtr<HeaderData> next = std::move(mNext);
    while (next) {
      next = std::move(next->mNext);
    }
  }

  RefPtr<nsAtom> mField;
  nsString mData;
  UniquePtr<HeaderData> mNext;
};

AutoTArray<Document*, 8>* Document::sLoadingForegroundTopLevelContentDocument =
    nullptr;

static LinkedList<Document>& AllDocumentsList() {
  static NeverDestroyed<LinkedList<Document>> sAllDocuments;
  return *sAllDocuments;
}

static LazyLogModule gDocumentLeakPRLog("DocumentLeak");
static LazyLogModule gCspPRLog("CSP");
LazyLogModule gUserInteractionPRLog("UserInteraction");

static nsresult GetHttpChannelHelper(nsIChannel* aChannel,
                                     nsIHttpChannel** aHttpChannel) {
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
  if (httpChannel) {
    httpChannel.forget(aHttpChannel);
    return NS_OK;
  }

  nsCOMPtr<nsIMultiPartChannel> multipart = do_QueryInterface(aChannel);
  if (!multipart) {
    *aHttpChannel = nullptr;
    return NS_OK;
  }

  nsCOMPtr<nsIChannel> baseChannel;
  nsresult rv = multipart->GetBaseChannel(getter_AddRefs(baseChannel));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  httpChannel = do_QueryInterface(baseChannel);
  httpChannel.forget(aHttpChannel);

  return NS_OK;
}

}  // namespace dom

#define NAME_NOT_VALID ((nsSimpleContentList*)1)

IdentifierMapEntry::IdentifierMapEntry(
    const IdentifierMapEntry::DependentAtomOrString* aKey)
    : mKey(aKey ? *aKey : nullptr) {}

void IdentifierMapEntry::Traverse(
    nsCycleCollectionTraversalCallback* aCallback) {
  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                     "mIdentifierMap mNameContentList");
  aCallback->NoteXPCOMChild(static_cast<nsINodeList*>(mNameContentList));

  NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                     "mIdentifierMap mDocumentNameContentList");
  aCallback->NoteXPCOMChild(
      static_cast<nsINodeList*>(mDocumentNameContentList));

  if (mImageElement) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mIdentifierMap mImageElement element");
    nsIContent* imageElement = mImageElement;
    aCallback->NoteXPCOMChild(imageElement);
  }
}

bool IdentifierMapEntry::IsEmpty() {
  return mIdContentList->IsEmpty() && !mNameContentList &&
         !mDocumentNameContentList && !mChangeCallbacks && !mImageElement;
}

bool IdentifierMapEntry::HasNameElement() const {
  return mNameContentList && mNameContentList->Length() != 0;
}

void IdentifierMapEntry::AddContentChangeCallback(
    Document::IDTargetObserver aCallback, void* aData, bool aForImage) {
  if (!mChangeCallbacks) {
    mChangeCallbacks = MakeUnique<nsTHashtable<ChangeCallbackEntry>>();
  }

  ChangeCallback cc = {aCallback, aData, aForImage};
  mChangeCallbacks->PutEntry(cc);
}

void IdentifierMapEntry::RemoveContentChangeCallback(
    Document::IDTargetObserver aCallback, void* aData, bool aForImage) {
  if (!mChangeCallbacks) return;
  ChangeCallback cc = {aCallback, aData, aForImage};
  mChangeCallbacks->RemoveEntry(cc);
  if (mChangeCallbacks->Count() == 0) {
    mChangeCallbacks = nullptr;
  }
}

void IdentifierMapEntry::FireChangeCallbacks(Element* aOldElement,
                                             Element* aNewElement,
                                             bool aImageOnly) {
  if (!mChangeCallbacks) return;

  for (auto iter = mChangeCallbacks->Iter(); !iter.Done(); iter.Next()) {
    IdentifierMapEntry::ChangeCallbackEntry* entry = iter.Get();
    // Don't fire image changes for non-image observers, and don't fire element
    // changes for image observers when an image override is active.
    if (entry->mKey.mForImage ? (mImageElement && !aImageOnly) : aImageOnly) {
      continue;
    }

    if (!entry->mKey.mCallback(aOldElement, aNewElement, entry->mKey.mData)) {
      iter.Remove();
    }
  }
}

void IdentifierMapEntry::AddIdElement(Element* aElement) {
  MOZ_ASSERT(aElement, "Must have element");
  MOZ_ASSERT(!mIdContentList->Contains(nullptr), "Why is null in our list?");

  size_t index = mIdContentList.Insert(*aElement);
  if (index == 0) {
    Element* oldElement = mIdContentList->SafeElementAt(1);
    FireChangeCallbacks(oldElement, aElement);
  }
}

void IdentifierMapEntry::RemoveIdElement(Element* aElement) {
  MOZ_ASSERT(aElement, "Missing element");

  // This should only be called while the document is in an update.
  // Assertions near the call to this method guarantee this.

  // This could fire in OOM situations
  // Only assert this in HTML documents for now as XUL does all sorts of weird
  // crap.
  NS_ASSERTION(!aElement->OwnerDoc()->IsHTMLDocument() ||
                   mIdContentList->Contains(aElement),
               "Removing id entry that doesn't exist");

  // XXXbz should this ever Compact() I guess when all the content is gone
  // we'll just get cleaned up in the natural order of things...
  Element* currentElement = mIdContentList->SafeElementAt(0);
  mIdContentList.RemoveElement(*aElement);
  if (currentElement == aElement) {
    FireChangeCallbacks(currentElement, mIdContentList->SafeElementAt(0));
  }
}

void IdentifierMapEntry::SetImageElement(Element* aElement) {
  Element* oldElement = GetImageIdElement();
  mImageElement = aElement;
  Element* newElement = GetImageIdElement();
  if (oldElement != newElement) {
    FireChangeCallbacks(oldElement, newElement, true);
  }
}

void IdentifierMapEntry::ClearAndNotify() {
  Element* currentElement = mIdContentList->SafeElementAt(0);
  mIdContentList.Clear();
  if (currentElement) {
    FireChangeCallbacks(currentElement, nullptr);
  }
  mNameContentList = nullptr;
  mDocumentNameContentList = nullptr;
  if (mImageElement) {
    SetImageElement(nullptr);
  }
  mChangeCallbacks = nullptr;
}

namespace dom {

class SimpleHTMLCollection final : public nsSimpleContentList,
                                   public nsIHTMLCollection {
 public:
  explicit SimpleHTMLCollection(nsINode* aRoot) : nsSimpleContentList(aRoot) {}

  NS_DECL_ISUPPORTS_INHERITED

  virtual nsINode* GetParentObject() override {
    return nsSimpleContentList::GetParentObject();
  }
  virtual uint32_t Length() override { return nsSimpleContentList::Length(); }
  virtual Element* GetElementAt(uint32_t aIndex) override {
    return mElements.SafeElementAt(aIndex)->AsElement();
  }

  virtual Element* GetFirstNamedElement(const nsAString& aName,
                                        bool& aFound) override {
    aFound = false;
    RefPtr<nsAtom> name = NS_Atomize(aName);
    for (uint32_t i = 0; i < mElements.Length(); i++) {
      MOZ_DIAGNOSTIC_ASSERT(mElements[i]);
      Element* element = mElements[i]->AsElement();
      if (element->GetID() == name ||
          (element->HasName() &&
           element->GetParsedAttr(nsGkAtoms::name)->GetAtomValue() == name)) {
        aFound = true;
        return element;
      }
    }
    return nullptr;
  }

  virtual void GetSupportedNames(nsTArray<nsString>& aNames) override {
    AutoTArray<nsAtom*, 8> atoms;
    for (uint32_t i = 0; i < mElements.Length(); i++) {
      MOZ_DIAGNOSTIC_ASSERT(mElements[i]);
      Element* element = mElements[i]->AsElement();

      nsAtom* id = element->GetID();
      MOZ_ASSERT(id != nsGkAtoms::_empty);
      if (id && !atoms.Contains(id)) {
        atoms.AppendElement(id);
      }

      if (element->HasName()) {
        nsAtom* name = element->GetParsedAttr(nsGkAtoms::name)->GetAtomValue();
        MOZ_ASSERT(name && name != nsGkAtoms::_empty);
        if (name && !atoms.Contains(name)) {
          atoms.AppendElement(name);
        }
      }
    }

    nsString* names = aNames.AppendElements(atoms.Length());
    for (uint32_t i = 0; i < atoms.Length(); i++) {
      atoms[i]->ToString(names[i]);
    }
  }

  virtual JSObject* GetWrapperPreserveColorInternal() override {
    return nsWrapperCache::GetWrapperPreserveColor();
  }
  virtual void PreserveWrapperInternal(
      nsISupports* aScriptObjectHolder) override {
    nsWrapperCache::PreserveWrapper(aScriptObjectHolder);
  }
  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override {
    return HTMLCollection_Binding::Wrap(aCx, this, aGivenProto);
  }

  using nsBaseContentList::Item;

 private:
  virtual ~SimpleHTMLCollection() = default;
};

NS_IMPL_ISUPPORTS_INHERITED(SimpleHTMLCollection, nsSimpleContentList,
                            nsIHTMLCollection)

}  // namespace dom

void IdentifierMapEntry::AddNameElement(nsINode* aNode, Element* aElement) {
  if (!mNameContentList) {
    mNameContentList = new dom::SimpleHTMLCollection(aNode);
  }

  mNameContentList->AppendElement(aElement);
}

void IdentifierMapEntry::RemoveNameElement(Element* aElement) {
  if (mNameContentList) {
    mNameContentList->RemoveElement(aElement);
  }
}

void IdentifierMapEntry::AddDocumentNameElement(
    Document* aDocument, nsGenericHTMLElement* aElement) {
  if (!mDocumentNameContentList) {
    mDocumentNameContentList = new dom::SimpleHTMLCollection(aDocument);
  }

  mDocumentNameContentList->AppendElement(aElement);
}

void IdentifierMapEntry::RemoveDocumentNameElement(
    nsGenericHTMLElement* aElement) {
  if (mDocumentNameContentList) {
    mDocumentNameContentList->RemoveElement(aElement);
  }
}

bool IdentifierMapEntry::HasDocumentNameElement() const {
  return mDocumentNameContentList && mDocumentNameContentList->Length() != 0;
}

bool IdentifierMapEntry::HasIdElementExposedAsHTMLDocumentProperty() const {
  Element* idElement = GetIdElement();
  return idElement &&
         nsGenericHTMLElement::ShouldExposeIdAsHTMLDocumentProperty(idElement);
}

size_t IdentifierMapEntry::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return mKey.mString.SizeOfExcludingThisIfUnshared(aMallocSizeOf);
}

// Helper structs for the content->subdoc map

class SubDocMapEntry : public PLDHashEntryHdr {
 public:
  // Both of these are strong references
  dom::Element* mKey;  // must be first, to look like PLDHashEntryStub
  dom::Document* mSubDocument;
};

class OnloadBlocker final : public nsIRequest {
 public:
  OnloadBlocker() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUEST

 private:
  ~OnloadBlocker() = default;
};

NS_IMPL_ISUPPORTS(OnloadBlocker, nsIRequest)

NS_IMETHODIMP
OnloadBlocker::GetName(nsACString& aResult) {
  aResult.AssignLiteral("about:document-onload-blocker");
  return NS_OK;
}

NS_IMETHODIMP
OnloadBlocker::IsPending(bool* _retval) {
  *_retval = true;
  return NS_OK;
}

NS_IMETHODIMP
OnloadBlocker::GetStatus(nsresult* status) {
  *status = NS_OK;
  return NS_OK;
}

NS_IMETHODIMP OnloadBlocker::SetCanceledReason(const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP OnloadBlocker::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP OnloadBlocker::CancelWithReason(nsresult aStatus,
                                              const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}
NS_IMETHODIMP
OnloadBlocker::Cancel(nsresult status) { return NS_OK; }
NS_IMETHODIMP
OnloadBlocker::Suspend(void) { return NS_OK; }
NS_IMETHODIMP
OnloadBlocker::Resume(void) { return NS_OK; }

NS_IMETHODIMP
OnloadBlocker::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  *aLoadGroup = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
OnloadBlocker::SetLoadGroup(nsILoadGroup* aLoadGroup) { return NS_OK; }

NS_IMETHODIMP
OnloadBlocker::GetLoadFlags(nsLoadFlags* aLoadFlags) {
  *aLoadFlags = nsIRequest::LOAD_NORMAL;
  return NS_OK;
}

NS_IMETHODIMP
OnloadBlocker::GetTRRMode(nsIRequest::TRRMode* aTRRMode) {
  return GetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
OnloadBlocker::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  return SetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
OnloadBlocker::SetLoadFlags(nsLoadFlags aLoadFlags) { return NS_OK; }

// ==================================================================

namespace dom {

ExternalResourceMap::ExternalResourceMap() : mHaveShutDown(false) {}

Document* ExternalResourceMap::RequestResource(
    nsIURI* aURI, nsIReferrerInfo* aReferrerInfo, nsINode* aRequestingNode,
    Document* aDisplayDocument, ExternalResourceLoad** aPendingLoad) {
  // If we ever start allowing non-same-origin loads here, we might need to do
  // something interesting with aRequestingPrincipal even for the hashtable
  // gets.
  MOZ_ASSERT(aURI, "Must have a URI");
  MOZ_ASSERT(aRequestingNode, "Must have a node");
  MOZ_ASSERT(aReferrerInfo, "Must have a referrerInfo");
  *aPendingLoad = nullptr;
  if (mHaveShutDown) {
    return nullptr;
  }

  // First, make sure we strip the ref from aURI.
  nsCOMPtr<nsIURI> clone;
  nsresult rv = NS_GetURIWithoutRef(aURI, getter_AddRefs(clone));
  if (NS_FAILED(rv) || !clone) {
    return nullptr;
  }

  ExternalResource* resource;
  mMap.Get(clone, &resource);
  if (resource) {
    return resource->mDocument;
  }

  bool loadStartSucceeded =
      mPendingLoads.WithEntryHandle(clone, [&](auto&& loadEntry) {
        if (!loadEntry) {
          loadEntry.Insert(MakeRefPtr<PendingLoad>(aDisplayDocument));

          if (NS_FAILED(loadEntry.Data()->StartLoad(clone, aReferrerInfo,
                                                    aRequestingNode))) {
            return false;
          }
        }

        RefPtr<PendingLoad> load(loadEntry.Data());
        load.forget(aPendingLoad);
        return true;
      });
  if (!loadStartSucceeded) {
    // Make sure we don't thrash things by trying this load again, since
    // chances are it failed for good reasons (security check, etc).
    // This must be done outside the WithEntryHandle functor, as it accesses
    // mPendingLoads.
    AddExternalResource(clone, nullptr, nullptr, aDisplayDocument);
  }

  return nullptr;
}

void ExternalResourceMap::EnumerateResources(SubDocEnumFunc aCallback) const {
  nsTArray<RefPtr<Document>> docs(mMap.Count());
  for (const auto& entry : mMap.Values()) {
    if (Document* doc = entry->mDocument) {
      docs.AppendElement(doc);
    }
  }
  for (auto& doc : docs) {
    if (aCallback(*doc) == CallState::Stop) {
      return;
    }
  }
}

void ExternalResourceMap::CollectDescendantDocuments(
    nsTArray<RefPtr<Document>>& aDocs, SubDocTestFunc aCallback) const {
  for (const auto& entry : mMap.Values()) {
    if (Document* doc = entry->mDocument) {
      if (aCallback(doc)) {
        aDocs.AppendElement(doc);
      }
      doc->CollectDescendantDocuments(aDocs, Document::IncludeSubResources::Yes,
                                      aCallback);
    }
  }
}

void ExternalResourceMap::Traverse(
    nsCycleCollectionTraversalCallback* aCallback) const {
  // mPendingLoads will get cleared out as the requests complete, so
  // no need to worry about those here.
  for (const auto& entry : mMap) {
    ExternalResourceMap::ExternalResource* resource = entry.GetWeak();

    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mExternalResourceMap.mMap entry"
                                       "->mDocument");
    aCallback->NoteXPCOMChild(ToSupports(resource->mDocument));

    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mExternalResourceMap.mMap entry"
                                       "->mViewer");
    aCallback->NoteXPCOMChild(resource->mViewer);

    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*aCallback,
                                       "mExternalResourceMap.mMap entry"
                                       "->mLoadGroup");
    aCallback->NoteXPCOMChild(resource->mLoadGroup);
  }
}

void ExternalResourceMap::HideViewers() {
  for (const auto& entry : mMap) {
    nsCOMPtr<nsIDocumentViewer> viewer = entry.GetData()->mViewer;
    if (viewer) {
      viewer->Hide();
    }
  }
}

void ExternalResourceMap::ShowViewers() {
  for (const auto& entry : mMap) {
    nsCOMPtr<nsIDocumentViewer> viewer = entry.GetData()->mViewer;
    if (viewer) {
      viewer->Show();
    }
  }
}

void TransferShowingState(Document* aFromDoc, Document* aToDoc) {
  MOZ_ASSERT(aFromDoc && aToDoc, "transferring showing state from/to null doc");

  if (aFromDoc->IsShowing()) {
    aToDoc->OnPageShow(true, nullptr);
  }
}

nsresult ExternalResourceMap::AddExternalResource(nsIURI* aURI,
                                                  nsIDocumentViewer* aViewer,
                                                  nsILoadGroup* aLoadGroup,
                                                  Document* aDisplayDocument) {
  MOZ_ASSERT(aURI, "Unexpected call");
  MOZ_ASSERT((aViewer && aLoadGroup) || (!aViewer && !aLoadGroup),
             "Must have both or neither");

  RefPtr<PendingLoad> load;
  mPendingLoads.Remove(aURI, getter_AddRefs(load));

  nsresult rv = NS_OK;

  nsCOMPtr<Document> doc;
  if (aViewer) {
    doc = aViewer->GetDocument();
    NS_ASSERTION(doc, "Must have a document");

    doc->SetDisplayDocument(aDisplayDocument);

    // Make sure that hiding our viewer will tear down its presentation.
    aViewer->SetSticky(false);

    rv = aViewer->Init(nullptr, LayoutDeviceIntRect(), nullptr);
    if (NS_SUCCEEDED(rv)) {
      rv = aViewer->Open(nullptr, nullptr);
    }

    if (NS_FAILED(rv)) {
      doc = nullptr;
      aViewer = nullptr;
      aLoadGroup = nullptr;
    }
  }

  ExternalResource* newResource =
      mMap.InsertOrUpdate(aURI, MakeUnique<ExternalResource>()).get();

  newResource->mDocument = doc;
  newResource->mViewer = aViewer;
  newResource->mLoadGroup = aLoadGroup;
  if (doc) {
    if (nsPresContext* pc = doc->GetPresContext()) {
      pc->RecomputeBrowsingContextDependentData();
    }
    TransferShowingState(aDisplayDocument, doc);
  }

  const nsTArray<nsCOMPtr<nsIObserver>>& obs = load->Observers();
  for (uint32_t i = 0; i < obs.Length(); ++i) {
    obs[i]->Observe(ToSupports(doc), "external-resource-document-created",
                    nullptr);
  }

  return rv;
}

NS_IMPL_ISUPPORTS(ExternalResourceMap::PendingLoad, nsIStreamListener,
                  nsIRequestObserver)

NS_IMETHODIMP
ExternalResourceMap::PendingLoad::OnStartRequest(nsIRequest* aRequest) {
  ExternalResourceMap& map = mDisplayDocument->ExternalResourceMap();
  if (map.HaveShutDown()) {
    return NS_BINDING_ABORTED;
  }

  nsCOMPtr<nsIDocumentViewer> viewer;
  nsCOMPtr<nsILoadGroup> loadGroup;
  nsresult rv =
      SetupViewer(aRequest, getter_AddRefs(viewer), getter_AddRefs(loadGroup));

  // Make sure to do this no matter what
  nsresult rv2 =
      map.AddExternalResource(mURI, viewer, loadGroup, mDisplayDocument);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (NS_FAILED(rv2)) {
    mTargetListener = nullptr;
    return rv2;
  }

  return mTargetListener->OnStartRequest(aRequest);
}

nsresult ExternalResourceMap::PendingLoad::SetupViewer(
    nsIRequest* aRequest, nsIDocumentViewer** aViewer,
    nsILoadGroup** aLoadGroup) {
  MOZ_ASSERT(!mTargetListener, "Unexpected call to OnStartRequest");
  *aViewer = nullptr;
  *aLoadGroup = nullptr;

  nsCOMPtr<nsIChannel> chan(do_QueryInterface(aRequest));
  NS_ENSURE_TRUE(chan, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aRequest));
  if (httpChannel) {
    bool requestSucceeded;
    if (NS_FAILED(httpChannel->GetRequestSucceeded(&requestSucceeded)) ||
        !requestSucceeded) {
      // Bail out on this load, since it looks like we have an HTTP error page
      return NS_BINDING_ABORTED;
    }
  }

  nsAutoCString type;
  chan->GetContentType(type);

  nsCOMPtr<nsILoadGroup> loadGroup;
  chan->GetLoadGroup(getter_AddRefs(loadGroup));

  // Give this document its own loadgroup
  nsCOMPtr<nsILoadGroup> newLoadGroup =
      do_CreateInstance(NS_LOADGROUP_CONTRACTID);
  NS_ENSURE_TRUE(newLoadGroup, NS_ERROR_OUT_OF_MEMORY);
  newLoadGroup->SetLoadGroup(loadGroup);

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  loadGroup->GetNotificationCallbacks(getter_AddRefs(callbacks));

  nsCOMPtr<nsIInterfaceRequestor> newCallbacks =
      new LoadgroupCallbacks(callbacks);
  newLoadGroup->SetNotificationCallbacks(newCallbacks);

  nsCOMPtr<nsIDocumentLoaderFactory> docLoaderFactory =
      nsContentUtils::FindInternalDocumentViewer(type);
  NS_ENSURE_TRUE(docLoaderFactory, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIDocumentViewer> viewer;
  nsCOMPtr<nsIStreamListener> listener;
  nsresult rv = docLoaderFactory->CreateInstance(
      "external-resource", chan, newLoadGroup, type, nullptr, nullptr,
      getter_AddRefs(listener), getter_AddRefs(viewer));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(viewer, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIParser> parser = do_QueryInterface(listener);
  if (!parser) {
    /// We don't want to deal with the various fake documents yet
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // We can't handle HTML and other weird things here yet.
  nsIContentSink* sink = parser->GetContentSink();
  nsCOMPtr<nsIXMLContentSink> xmlSink = do_QueryInterface(sink);
  if (!xmlSink) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  listener.swap(mTargetListener);
  viewer.forget(aViewer);
  newLoadGroup.forget(aLoadGroup);
  return NS_OK;
}

NS_IMETHODIMP
ExternalResourceMap::PendingLoad::OnDataAvailable(nsIRequest* aRequest,
                                                  nsIInputStream* aStream,
                                                  uint64_t aOffset,
                                                  uint32_t aCount) {
  // mTargetListener might be null if SetupViewer or AddExternalResource failed.
  NS_ENSURE_TRUE(mTargetListener, NS_ERROR_FAILURE);
  if (mDisplayDocument->ExternalResourceMap().HaveShutDown()) {
    return NS_BINDING_ABORTED;
  }
  return mTargetListener->OnDataAvailable(aRequest, aStream, aOffset, aCount);
}

NS_IMETHODIMP
ExternalResourceMap::PendingLoad::OnStopRequest(nsIRequest* aRequest,
                                                nsresult aStatus) {
  // mTargetListener might be null if SetupViewer or AddExternalResource failed
  if (mTargetListener) {
    nsCOMPtr<nsIStreamListener> listener;
    mTargetListener.swap(listener);
    return listener->OnStopRequest(aRequest, aStatus);
  }

  return NS_OK;
}

nsresult ExternalResourceMap::PendingLoad::StartLoad(
    nsIURI* aURI, nsIReferrerInfo* aReferrerInfo, nsINode* aRequestingNode) {
  MOZ_ASSERT(aURI, "Must have a URI");
  MOZ_ASSERT(aRequestingNode, "Must have a node");
  MOZ_ASSERT(aReferrerInfo, "Must have a referrerInfo");

  nsCOMPtr<nsILoadGroup> loadGroup =
      aRequestingNode->OwnerDoc()->GetDocumentLoadGroup();

  nsresult rv = NS_OK;
  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), aURI, aRequestingNode,
                     nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT,
                     nsIContentPolicy::TYPE_INTERNAL_EXTERNAL_RESOURCE,
                     nullptr,  // aPerformanceStorage
                     loadGroup);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
  if (httpChannel) {
    rv = httpChannel->SetReferrerInfo(aReferrerInfo);
    Unused << NS_WARN_IF(NS_FAILED(rv));
  }

  mURI = aURI;

  return channel->AsyncOpen(this);
}

NS_IMPL_ISUPPORTS(ExternalResourceMap::LoadgroupCallbacks,
                  nsIInterfaceRequestor)

#define IMPL_SHIM(_i) \
  NS_IMPL_ISUPPORTS(ExternalResourceMap::LoadgroupCallbacks::_i##Shim, _i)

IMPL_SHIM(nsILoadContext)
IMPL_SHIM(nsIProgressEventSink)
IMPL_SHIM(nsIChannelEventSink)

#undef IMPL_SHIM

#define IID_IS(_i) aIID.Equals(NS_GET_IID(_i))

#define TRY_SHIM(_i)                                 \
  PR_BEGIN_MACRO                                     \
  if (IID_IS(_i)) {                                  \
    nsCOMPtr<_i> real = do_GetInterface(mCallbacks); \
    if (!real) {                                     \
      return NS_NOINTERFACE;                         \
    }                                                \
    nsCOMPtr<_i> shim = new _i##Shim(this, real);    \
    shim.forget(aSink);                              \
    return NS_OK;                                    \
  }                                                  \
  PR_END_MACRO

NS_IMETHODIMP
ExternalResourceMap::LoadgroupCallbacks::GetInterface(const nsIID& aIID,
                                                      void** aSink) {
  if (mCallbacks && (IID_IS(nsIPrompt) || IID_IS(nsIAuthPrompt) ||
                     IID_IS(nsIAuthPrompt2) || IID_IS(nsIBrowserChild))) {
    return mCallbacks->GetInterface(aIID, aSink);
  }

  *aSink = nullptr;

  TRY_SHIM(nsILoadContext);
  TRY_SHIM(nsIProgressEventSink);
  TRY_SHIM(nsIChannelEventSink);

  return NS_NOINTERFACE;
}

#undef TRY_SHIM
#undef IID_IS

ExternalResourceMap::ExternalResource::~ExternalResource() {
  if (mViewer) {
    mViewer->Close(nullptr);
    mViewer->Destroy();
  }
}

// ==================================================================
// =
// ==================================================================

// If we ever have an nsIDocumentObserver notification for stylesheet title
// changes we should update the list from that instead of overriding
// EnsureFresh.
class DOMStyleSheetSetList final : public DOMStringList {
 public:
  explicit DOMStyleSheetSetList(Document* aDocument);

  void Disconnect() { mDocument = nullptr; }

  virtual void EnsureFresh() override;

 protected:
  Document* mDocument;  // Our document; weak ref.  It'll let us know if it
                        // dies.
};

DOMStyleSheetSetList::DOMStyleSheetSetList(Document* aDocument)
    : mDocument(aDocument) {
  NS_ASSERTION(mDocument, "Must have document!");
}

void DOMStyleSheetSetList::EnsureFresh() {
  MOZ_ASSERT(NS_IsMainThread());

  mNames.Clear();

  if (!mDocument) {
    return;  // Spec says "no exceptions", and we have no style sets if we have
             // no document, for sure
  }

  size_t count = mDocument->SheetCount();
  nsAutoString title;
  for (size_t index = 0; index < count; index++) {
    StyleSheet* sheet = mDocument->SheetAt(index);
    NS_ASSERTION(sheet, "Null sheet in sheet list!");
    sheet->GetTitle(title);
    if (!title.IsEmpty() && !mNames.Contains(title) && !Add(title)) {
      return;
    }
  }
}

Document::PendingFrameStaticClone::~PendingFrameStaticClone() = default;

// ==================================================================
// =
// ==================================================================

Document::InternalCommandDataHashtable*
    Document::sInternalCommandDataHashtable = nullptr;

// static
void Document::Shutdown() {
  if (sInternalCommandDataHashtable) {
    sInternalCommandDataHashtable->Clear();
    delete sInternalCommandDataHashtable;
    sInternalCommandDataHashtable = nullptr;
  }
}

Document::Document(const char* aContentType)
    : nsINode(nullptr),
      DocumentOrShadowRoot(this),
      mCharacterSet(WINDOWS_1252_ENCODING),
      mCharacterSetSource(0),
      mParentDocument(nullptr),
      mCachedRootElement(nullptr),
      mNodeInfoManager(nullptr),
#ifdef DEBUG
      mStyledLinksCleared(false),
#endif
      mCachedStateObjectValid(false),
      mBlockAllMixedContent(false),
      mBlockAllMixedContentPreloads(false),
      mUpgradeInsecureRequests(false),
      mUpgradeInsecurePreloads(false),
      mDevToolsWatchingDOMMutations(false),
      mRenderingSuppressedForViewTransitions(false),
      mBidiEnabled(false),
      mMayNeedFontPrefsUpdate(true),
      mIsInitialDocumentInWindow(false),
      mIsEverInitialDocumentInWindow(false),
      mIgnoreDocGroupMismatches(false),
      mLoadedAsData(false),
      mAddedToMemoryReportingAsDataDocument(false),
      mMayStartLayout(true),
      mHaveFiredTitleChange(false),
      mIsShowing(false),
      mVisible(true),
      mRemovedFromDocShell(false),
      // mAllowDNSPrefetch starts true, so that we can always reliably && it
      // with various values that might disable it.  Since we never prefetch
      // unless we get a window, and in that case the docshell value will get
      // &&-ed in, this is safe.
      mAllowDNSPrefetch(true),
      mIsStaticDocument(false),
      mCreatingStaticClone(false),
      mHasPrintCallbacks(false),
      mInUnlinkOrDeletion(false),
      mHasHadScriptHandlingObject(false),
      mIsBeingUsedAsImage(false),
      mChromeRulesEnabled(false),
      mInChromeDocShell(false),
      mIsSyntheticDocument(false),
      mHasLinksToUpdateRunnable(false),
      mFlushingPendingLinkUpdates(false),
      mMayHaveDOMMutationObservers(false),
      mMayHaveAnimationObservers(false),
      mHasCSPDeliveredThroughHeader(false),
      mBFCacheDisallowed(false),
      mHasHadDefaultView(false),
      mStyleSheetChangeEventsEnabled(false),
      mDevToolsAnonymousAndShadowEventsEnabled(false),
      mIsSrcdocDocument(false),
      mHasDisplayDocument(false),
      mFontFaceSetDirty(true),
      mDidFireDOMContentLoaded(true),
      mIsTopLevelContentDocument(false),
      mIsContentDocument(false),
      mDidCallBeginLoad(false),
      mEncodingMenuDisabled(false),
      mLinksEnabled(true),
      mIsSVGGlyphsDocument(false),
      mInDestructor(false),
      mIsGoingAway(false),
      mStyleSetFilled(false),
      mQuirkSheetAdded(false),
      mMayHaveTitleElement(false),
      mDOMLoadingSet(false),
      mDOMInteractiveSet(false),
      mDOMCompleteSet(false),
      mAutoFocusFired(false),
      mScrolledToRefAlready(false),
      mChangeScrollPosWhenScrollingToRef(false),
      mDelayFrameLoaderInitialization(false),
      mSynchronousDOMContentLoaded(false),
      mMaybeServiceWorkerControlled(false),
      mAllowZoom(false),
      mValidScaleFloat(false),
      mValidMinScale(false),
      mValidMaxScale(false),
      mWidthStrEmpty(false),
      mParserAborted(false),
      mReportedDocumentUseCounters(false),
      mHasReportedShadowDOMUsage(false),
      mLoadEventFiring(false),
      mSkipLoadEventAfterClose(false),
      mDisableCookieAccess(false),
      mDisableDocWrite(false),
      mTooDeepWriteRecursion(false),
      mPendingMaybeEditingStateChanged(false),
      mHasBeenEditable(false),
      mIsRunningExecCommandByContent(false),
      mIsRunningExecCommandByChromeOrAddon(false),
      mSetCompleteAfterDOMContentLoaded(false),
      mDidHitCompleteSheetCache(false),
      mUseCountersInitialized(false),
      mShouldReportUseCounters(false),
      mShouldSendPageUseCounters(false),
      mUserHasInteracted(false),
      mHasUserInteractionTimerScheduled(false),
      mShouldResistFingerprinting(false),
      mIsInPrivateBrowsing(false),
      mCloningForSVGUse(false),
      mAllowDeclarativeShadowRoots(false),
      mSuspendDOMNotifications(false),
      mForceLoadAtTop(false),
      mFireMutationEvents(true),
      mHasPolicyWithRequireTrustedTypesForDirective(false),
      mClipboardCopyTriggered(false),
      mXMLDeclarationBits(0),
      mOnloadBlockCount(0),
      mWriteLevel(0),
      mContentEditableCount(0),
      mEditingState(EditingState::eOff),
      mCompatMode(eCompatibility_FullStandards),
      mReadyState(ReadyState::READYSTATE_UNINITIALIZED),
      mAncestorIsLoading(false),
      mVisibilityState(dom::VisibilityState::Hidden),
      mType(eUnknown),
      mDefaultElementType(0),
      mAllowXULXBL(eTriUnset),
      mSkipDTDSecurityChecks(false),
      mBidiOptions(IBMBIDI_DEFAULT_BIDI_OPTIONS),
      mSandboxFlags(0),
      mPartID(0),
      mMarkedCCGeneration(0),
      mPresShell(nullptr),
      mSubtreeModifiedDepth(0),
      mPreloadPictureDepth(0),
      mEventsSuppressed(0),
      mIgnoreDestructiveWritesCounter(0),
      mStaticCloneCount(0),
      mWindow(nullptr),
      mBFCacheEntry(nullptr),
      mInSyncOperationCount(0),
      mBlockDOMContentLoaded(0),
      mUpdateNestLevel(0),
      mHttpsOnlyStatus(nsILoadInfo::HTTPS_ONLY_UNINITIALIZED),
      mViewportType(Unknown),
      mViewportFit(ViewportFitType::Auto),
      mInteractiveWidgetMode(
          InteractiveWidgetUtils::DefaultInteractiveWidgetMode()),
      mHeaderData(nullptr),
      mServoRestyleRootDirtyBits(0),
      mThrowOnDynamicMarkupInsertionCounter(0),
      mIgnoreOpensDuringUnloadCounter(0),
      mSavedResolution(1.0f),
      mGeneration(0),
      mCachedTabSizeGeneration(0),
      mNextFormNumber(0),
      mNextControlNumber(0),
      mPreloadService(this),
      mShouldNotifyFetchSuccess(false),
      mShouldNotifyFormOrPasswordRemoved(false) {
  MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug, ("DOCUMENT %p created", this));

  SetIsInDocument();
  SetIsConnected(true);

  // Create these unconditionally, they will be used to warn about the `zoom`
  // property, even if use counters are disabled.
  mStyleUseCounters.reset(Servo_UseCounters_Create());

  SetContentType(nsDependentCString(aContentType));

  // Start out mLastStyleSheetSet as null, per spec
  SetDOMStringToNull(mLastStyleSheetSet);

  // void state used to differentiate an empty source from an unselected source
  mPreloadPictureFoundSource.SetIsVoid(true);

  RecomputeLanguageFromCharset();

  mPreloadReferrerInfo = new dom::ReferrerInfo(nullptr);
  mReferrerInfo = new dom::ReferrerInfo(nullptr);
}

#ifndef ANDROID
// unused by GeckoView
static bool IsAboutErrorPage(nsGlobalWindowInner* aWin, const char* aSpec) {
  if (NS_WARN_IF(!aWin)) {
    return false;
  }

  nsIURI* uri = aWin->GetDocumentURI();
  if (NS_WARN_IF(!uri)) {
    return false;
  }
  // getSpec is an expensive operation, hence we first check the scheme
  // to see if the caller is actually an about: page.
  if (!uri->SchemeIs("about")) {
    return false;
  }

  nsAutoCString aboutSpec;
  nsresult rv = NS_GetAboutModuleName(uri, aboutSpec);
  NS_ENSURE_SUCCESS(rv, false);

  return aboutSpec.EqualsASCII(aSpec);
}
#endif

bool Document::CallerIsTrustedAboutNetError(JSContext* aCx, JSObject* aObject) {
  nsGlobalWindowInner* win = xpc::WindowOrNull(aObject);
#ifdef ANDROID
  // GeckoView uses data URLs for error pages, so for now just check for any
  // error page
  return win && win->GetDocument() && win->GetDocument()->IsErrorPage();
#else
  return win && IsAboutErrorPage(win, "neterror");
#endif
}

bool Document::CallerIsTrustedAboutHttpsOnlyError(JSContext* aCx,
                                                  JSObject* aObject) {
  nsGlobalWindowInner* win = xpc::WindowOrNull(aObject);
#ifdef ANDROID
  // GeckoView uses data URLs for error pages, so for now just check for any
  // error page
  return win && win->GetDocument() && win->GetDocument()->IsErrorPage();
#else
  return win && IsAboutErrorPage(win, "httpsonlyerror");
#endif
}

already_AddRefed<mozilla::dom::Promise> Document::AddCertException(
    bool aIsTemporary, ErrorResult& aError) {
  RefPtr<Promise> promise = Promise::Create(GetScopeObject(), aError,
                                            Promise::ePropagateUserInteraction);
  if (aError.Failed()) {
    return nullptr;
  }

  nsresult rv = NS_OK;
  if (NS_WARN_IF(!mFailedChannel)) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  nsCOMPtr<nsIURI> failedChannelURI;
  NS_GetFinalChannelURI(mFailedChannel, getter_AddRefs(failedChannelURI));
  if (!failedChannelURI) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  nsCOMPtr<nsIURI> innerURI = NS_GetInnermostURI(failedChannelURI);
  if (!innerURI) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  nsAutoCString host;
  innerURI->GetAsciiHost(host);
  int32_t port;
  innerURI->GetPort(&port);

  nsCOMPtr<nsITransportSecurityInfo> tsi;
  rv = mFailedChannel->GetSecurityInfo(getter_AddRefs(tsi));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->MaybeReject(rv);
    return promise.forget();
  }
  if (NS_WARN_IF(!tsi)) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  nsCOMPtr<nsIX509Cert> cert;
  rv = tsi->GetServerCert(getter_AddRefs(cert));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->MaybeReject(rv);
    return promise.forget();
  }
  if (NS_WARN_IF(!cert)) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    return promise.forget();
  }

  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    MOZ_ASSERT(cc);
    OriginAttributes const& attrs = NodePrincipal()->OriginAttributesRef();
    cc->SendAddCertException(cert, host, port, attrs, aIsTemporary)
        ->Then(GetCurrentSerialEventTarget(), __func__,
               [promise](const mozilla::MozPromise<
                         nsresult, mozilla::ipc::ResponseRejectReason,
                         true>::ResolveOrRejectValue& aValue) {
                 if (aValue.IsResolve()) {
                   promise->MaybeResolve(aValue.ResolveValue());
                 } else {
                   promise->MaybeRejectWithUndefined();
                 }
               });
    return promise.forget();
  }

  if (XRE_IsParentProcess()) {
    nsCOMPtr<nsICertOverrideService> overrideService =
        do_GetService(NS_CERTOVERRIDE_CONTRACTID);
    if (!overrideService) {
      promise->MaybeReject(NS_ERROR_FAILURE);
      return promise.forget();
    }

    OriginAttributes const& attrs = NodePrincipal()->OriginAttributesRef();
    rv = overrideService->RememberValidityOverride(host, port, attrs, cert,
                                                   aIsTemporary);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      promise->MaybeReject(rv);
      return promise.forget();
    }

    promise->MaybeResolveWithUndefined();
    return promise.forget();
  }

  promise->MaybeReject(NS_ERROR_FAILURE);
  return promise.forget();
}

void Document::ReloadWithHttpsOnlyException() {
  if (WindowGlobalChild* wgc = GetWindowGlobalChild()) {
    wgc->SendReloadWithHttpsOnlyException();
  }
}

// Given an nsresult that is assumed to be synthesized by PSM and describes a
// certificate or TLS error, attempts to convert it into a string
// representation of the underlying NSS error.
// `aErrorCodeString` will be an empty string if `aResult` is not an error from
// PSM or it does not represent a valid NSS error.
void GetErrorCodeStringFromNSResult(nsresult aResult,
                                    nsAString& aErrorCodeString) {
  aErrorCodeString.Truncate();

  if (NS_ERROR_GET_MODULE(aResult) != NS_ERROR_MODULE_SECURITY ||
      NS_ERROR_GET_SEVERITY(aResult) != NS_ERROR_SEVERITY_ERROR) {
    return;
  }

  PRErrorCode errorCode = -1 * NS_ERROR_GET_CODE(aResult);
  if (!mozilla::psm::IsNSSErrorCode(errorCode)) {
    return;
  }

  const char* errorCodeString = PR_ErrorToName(errorCode);
  if (!errorCodeString) {
    return;
  }

  aErrorCodeString.AssignASCII(errorCodeString);
}

void Document::GetNetErrorInfo(NetErrorInfo& aInfo, ErrorResult& aRv) {
  nsresult rv = NS_OK;
  if (NS_WARN_IF(!mFailedChannel)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(mFailedChannel));

  // We don't throw even if httpChannel is null, we just keep responseStatus and
  // responseStatusText empty
  if (httpChannel) {
    uint32_t responseStatus;
    nsAutoCString responseStatusText;
    rv = httpChannel->GetResponseStatus(&responseStatus);
    if (NS_SUCCEEDED(rv)) {
      aInfo.mResponseStatus = responseStatus;
    }

    rv = httpChannel->GetResponseStatusText(responseStatusText);
    if (NS_FAILED(rv) || responseStatusText.IsEmpty()) {
      net_GetDefaultStatusTextForCode(responseStatus, responseStatusText);
    }
    aInfo.mResponseStatusText.AssignASCII(responseStatusText);
  }

  nsCOMPtr<nsITransportSecurityInfo> tsi;
  rv = mFailedChannel->GetSecurityInfo(getter_AddRefs(tsi));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  nsresult channelStatus;
  rv = mFailedChannel->GetStatus(&channelStatus);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  aInfo.mChannelStatus = static_cast<uint32_t>(channelStatus);

  // If nsITransportSecurityInfo is not set, simply keep the remaining fields
  // empty (to make responseStatus and responseStatusText accessible).
  if (!tsi) {
    return;
  }

  // TransportSecurityInfo::GetErrorCodeString always returns NS_OK
  (void)tsi->GetErrorCodeString(aInfo.mErrorCodeString);
  if (aInfo.mErrorCodeString.IsEmpty()) {
    GetErrorCodeStringFromNSResult(channelStatus, aInfo.mErrorCodeString);
  }
}

bool Document::CallerIsTrustedAboutCertError(JSContext* aCx,
                                             JSObject* aObject) {
  nsGlobalWindowInner* win = xpc::WindowOrNull(aObject);
#ifdef ANDROID
  // GeckoView uses data URLs for error pages, so for now just check for any
  // error page
  return win && win->GetDocument() && win->GetDocument()->IsErrorPage();
#else
  return win && IsAboutErrorPage(win, "certerror");
#endif
}

bool Document::CallerIsSystemPrincipalOrWebCompatAddon(JSContext* aCx,
                                                       JSObject* aObject) {
  RefPtr<BasePrincipal> principal =
      BasePrincipal::Cast(nsContentUtils::SubjectPrincipal(aCx));

  if (!principal) {
    return false;
  }

  // We allow the privileged APIs to be called from system principal.
  if (principal->IsSystemPrincipal()) {
    return true;
  }

  // We only allow calling privileged APIs from the webcompat extension.
  if (auto* policy = principal->ContentScriptAddonPolicy()) {
    nsAutoString addonID;
    policy->GetId(addonID);

    return addonID.EqualsLiteral("webcompat@mozilla.org");
  }

  return false;
}

bool Document::IsErrorPage() const {
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel ? mChannel->LoadInfo() : nullptr;
  return loadInfo && loadInfo->GetLoadErrorPage();
}

void Document::GetFailedCertSecurityInfo(FailedCertSecurityInfo& aInfo,
                                         ErrorResult& aRv) {
  nsresult rv = NS_OK;
  if (NS_WARN_IF(!mFailedChannel)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  nsCOMPtr<nsITransportSecurityInfo> tsi;
  rv = mFailedChannel->GetSecurityInfo(getter_AddRefs(tsi));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  if (NS_WARN_IF(!tsi)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  nsresult channelStatus;
  rv = mFailedChannel->GetStatus(&channelStatus);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  aInfo.mChannelStatus = static_cast<uint32_t>(channelStatus);

  // TransportSecurityInfo::GetErrorCodeString always returns NS_OK
  (void)tsi->GetErrorCodeString(aInfo.mErrorCodeString);
  if (aInfo.mErrorCodeString.IsEmpty()) {
    GetErrorCodeStringFromNSResult(channelStatus, aInfo.mErrorCodeString);
  }

  nsITransportSecurityInfo::OverridableErrorCategory errorCategory;
  rv = tsi->GetOverridableErrorCategory(&errorCategory);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  switch (errorCategory) {
    case nsITransportSecurityInfo::OverridableErrorCategory::ERROR_TRUST:
      aInfo.mOverridableErrorCategory =
          dom::OverridableErrorCategory::Trust_error;
      break;
    case nsITransportSecurityInfo::OverridableErrorCategory::ERROR_DOMAIN:
      aInfo.mOverridableErrorCategory =
          dom::OverridableErrorCategory::Domain_mismatch;
      break;
    case nsITransportSecurityInfo::OverridableErrorCategory::ERROR_TIME:
      aInfo.mOverridableErrorCategory =
          dom::OverridableErrorCategory::Expired_or_not_yet_valid;
      break;
    default:
      aInfo.mOverridableErrorCategory = dom::OverridableErrorCategory::Unset;
      break;
  }

  nsCOMPtr<nsIX509Cert> cert;
  nsCOMPtr<nsIX509CertValidity> validity;
  rv = tsi->GetServerCert(getter_AddRefs(cert));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  if (NS_WARN_IF(!cert)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  rv = cert->GetValidity(getter_AddRefs(validity));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  if (NS_WARN_IF(!validity)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  PRTime validityResult;
  rv = validity->GetNotBefore(&validityResult);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  aInfo.mValidNotBefore = DOMTimeStamp(validityResult / PR_USEC_PER_MSEC);

  rv = validity->GetNotAfter(&validityResult);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  aInfo.mValidNotAfter = DOMTimeStamp(validityResult / PR_USEC_PER_MSEC);

  nsAutoString issuerCommonName;
  nsAutoString certChainPEMString;
  Sequence<nsString>& certChainStrings = aInfo.mCertChainStrings.Construct();
  int64_t maxValidity = std::numeric_limits<int64_t>::max();
  int64_t minValidity = 0;
  PRTime notBefore, notAfter;
  nsTArray<RefPtr<nsIX509Cert>> failedCertArray;
  rv = tsi->GetFailedCertChain(failedCertArray);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  if (NS_WARN_IF(failedCertArray.IsEmpty())) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  for (const auto& certificate : failedCertArray) {
    rv = certificate->GetIssuerCommonName(issuerCommonName);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return;
    }

    rv = certificate->GetValidity(getter_AddRefs(validity));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return;
    }
    if (NS_WARN_IF(!validity)) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }

    rv = validity->GetNotBefore(&notBefore);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return;
    }

    rv = validity->GetNotAfter(&notAfter);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return;
    }

    notBefore = std::max(minValidity, notBefore);
    notAfter = std::min(maxValidity, notAfter);
    nsTArray<uint8_t> certArray;
    rv = certificate->GetRawDER(certArray);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return;
    }

    nsAutoString der64;
    rv = Base64Encode(reinterpret_cast<const char*>(certArray.Elements()),
                      certArray.Length(), der64);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return;
    }
    if (!certChainStrings.AppendElement(der64, fallible)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
  }

  aInfo.mIssuerCommonName.Assign(issuerCommonName);
  aInfo.mCertValidityRangeNotAfter = DOMTimeStamp(notAfter / PR_USEC_PER_MSEC);
  aInfo.mCertValidityRangeNotBefore =
      DOMTimeStamp(notBefore / PR_USEC_PER_MSEC);

  int32_t errorCode;
  rv = tsi->GetErrorCode(&errorCode);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  aInfo.mErrorIsOverridable = mozilla::psm::ErrorIsOverridable(errorCode);

  nsCOMPtr<nsINSSErrorsService> nsserr =
      do_GetService("@mozilla.org/nss_errors_service;1");
  if (NS_WARN_IF(!nsserr)) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }
  nsresult res;
  rv = nsserr->GetXPCOMFromNSSError(errorCode, &res);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }
  rv = nsserr->GetErrorMessage(res, aInfo.mErrorMessage);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return;
  }

  OriginAttributes attrs;
  StoragePrincipalHelper::GetRegularPrincipalOriginAttributes(this, attrs);
  nsCOMPtr<nsIURI> aURI;
  mFailedChannel->GetURI(getter_AddRefs(aURI));
  if (XRE_IsContentProcess()) {
    ContentChild* cc = ContentChild::GetSingleton();
    MOZ_ASSERT(cc);
    cc->SendIsSecureURI(aURI, attrs, &aInfo.mHasHSTS);
  } else {
    nsCOMPtr<nsISiteSecurityService> sss =
        do_GetService(NS_SSSERVICE_CONTRACTID);
    if (NS_WARN_IF(!sss)) {
      return;
    }
    Unused << NS_WARN_IF(
        NS_FAILED(sss->IsSecureURI(aURI, attrs, &aInfo.mHasHSTS)));
  }
  nsCOMPtr<nsIPublicKeyPinningService> pkps =
      do_GetService(NS_PKPSERVICE_CONTRACTID);
  if (NS_WARN_IF(!pkps)) {
    return;
  }
  Unused << NS_WARN_IF(NS_FAILED(pkps->HostHasPins(aURI, &aInfo.mHasHPKP)));
}

bool Document::IsAboutPage() const {
  return NodePrincipal()->SchemeIs("about");
}

void Document::ConstructUbiNode(void* storage) {
  JS::ubi::Concrete<Document>::construct(storage, this);
}

void Document::LoadEventFired() {
  // Object used to collect some telemetry data so we don't need to query for it
  // twice.
  glean::perf::PageLoadExtra pageLoadEventData;

  // Accumulate timing data located in each document's realm and report to
  // telemetry.
  AccumulateJSTelemetry(pageLoadEventData);

  // Collect page load timings
  AccumulatePageLoadTelemetry(pageLoadEventData);

  // Record page load event
  RecordPageLoadEventTelemetry(pageLoadEventData);

  // Release the JS bytecode cache from its wait on the load event, and
  // potentially dispatch the encoding of the bytecode.
  if (ScriptLoader()) {
    ScriptLoader()->LoadEventFired();
  }
}

void Document::RecordPageLoadEventTelemetry(
    glean::perf::PageLoadExtra& aEventTelemetryData) {
  // If the page load time is empty, then the content wasn't something we want
  // to report (i.e. not a top level document).
  if (!aEventTelemetryData.loadTime) {
    return;
  }
  MOZ_ASSERT(IsTopLevelContentDocument());

  nsPIDOMWindowOuter* window = GetWindow();
  if (!window) {
    return;
  }

  nsIDocShell* docshell = window->GetDocShell();
  if (!docshell) {
    return;
  }

  nsAutoCString loadTypeStr;
  switch (docshell->GetLoadType()) {
    case LOAD_NORMAL:
    case LOAD_NORMAL_REPLACE:
    case LOAD_NORMAL_BYPASS_CACHE:
    case LOAD_NORMAL_BYPASS_PROXY:
    case LOAD_NORMAL_BYPASS_PROXY_AND_CACHE:
      loadTypeStr.Append("NORMAL");
      break;
    case LOAD_HISTORY:
      loadTypeStr.Append("HISTORY");
      break;
    case LOAD_RELOAD_NORMAL:
    case LOAD_RELOAD_BYPASS_CACHE:
    case LOAD_RELOAD_BYPASS_PROXY:
    case LOAD_RELOAD_BYPASS_PROXY_AND_CACHE:
    case LOAD_REFRESH:
    case LOAD_REFRESH_REPLACE:
    case LOAD_RELOAD_CHARSET_CHANGE:
    case LOAD_RELOAD_CHARSET_CHANGE_BYPASS_PROXY_AND_CACHE:
    case LOAD_RELOAD_CHARSET_CHANGE_BYPASS_CACHE:
      loadTypeStr.Append("RELOAD");
      break;
    case LOAD_LINK:
      loadTypeStr.Append("LINK");
      break;
    case LOAD_STOP_CONTENT:
    case LOAD_STOP_CONTENT_AND_REPLACE:
      loadTypeStr.Append("STOP");
      break;
    case LOAD_ERROR_PAGE:
      loadTypeStr.Append("ERROR");
      break;
    default:
      loadTypeStr.Append("OTHER");
      break;
  }

  nsCOMPtr<nsIEffectiveTLDService> tldService =
      mozilla::components::EffectiveTLD::Service();
  if (tldService && mReferrerInfo &&
      (docshell->GetLoadType() & nsIDocShell::LOAD_CMD_NORMAL)) {
    nsAutoCString currentBaseDomain, referrerBaseDomain;
    nsCOMPtr<nsIURI> referrerURI = mReferrerInfo->GetComputedReferrer();
    if (referrerURI) {
      auto result = NS_SUCCEEDED(
          tldService->GetBaseDomain(referrerURI, 0, referrerBaseDomain));
      if (result) {
        bool sameOrigin = false;
        NodePrincipal()->IsSameOrigin(referrerURI, &sameOrigin);
        aEventTelemetryData.sameOriginNav = mozilla::Some(sameOrigin);
      }
    }
  }

  aEventTelemetryData.loadType = mozilla::Some(loadTypeStr);

  // Sending a glean ping must be done on the parent process.
  if (ContentChild* cc = ContentChild::GetSingleton()) {
    cc->SendRecordPageLoadEvent(aEventTelemetryData);
  }
}

#ifndef ANDROID
static void AccumulateHttp3FcpGleanPref(const nsCString& http3Key,
                                        const TimeDuration& duration) {
  if (http3Key == "http3"_ns) {
    glean::performance_pageload::http3_fcp_http3.AccumulateRawDuration(
        duration);
  } else if (http3Key == "supports_http3"_ns) {
    glean::performance_pageload::http3_fcp_supports_http3.AccumulateRawDuration(
        duration);
  } else {
    MOZ_ASSERT_UNREACHABLE("Unknown value for http3Key");
  }
}

static void AccumulatePriorityFcpGleanPref(
    const nsCString& http3WithPriorityKey, const TimeDuration& duration) {
  if (http3WithPriorityKey == "with_priority"_ns) {
    glean::performance_pageload::h3p_fcp_with_priority.AccumulateRawDuration(
        duration);
  } else if (http3WithPriorityKey == "without_priority"_ns) {
    glean::performance_pageload::http3_fcp_without_priority
        .AccumulateRawDuration(duration);
  } else {
    MOZ_ASSERT_UNREACHABLE("Unknown value for http3WithPriorityKey");
  }
}
#endif

void Document::AccumulatePageLoadTelemetry(
    glean::perf::PageLoadExtra& aEventTelemetryDataOut) {
  // Interested only in top level documents for real websites that are in the
  // foreground.
  if (!ShouldIncludeInTelemetry() || !IsTopLevelContentDocument() ||
      !GetNavigationTiming() ||
      !GetNavigationTiming()->DocShellHasBeenActiveSinceNavigationStart()) {
    return;
  }

  if (!GetChannel()) {
    return;
  }

  nsCOMPtr<nsITimedChannel> timedChannel(do_QueryInterface(GetChannel()));
  if (!timedChannel) {
    return;
  }

  // Default duration is 0, use this to check for bogus negative values.
  const TimeDuration zeroDuration;

  TimeStamp responseStart;
  timedChannel->GetResponseStart(&responseStart);

  TimeStamp redirectStart, redirectEnd;
  timedChannel->GetRedirectStart(&redirectStart);
  timedChannel->GetRedirectEnd(&redirectEnd);

  uint8_t redirectCount;
  timedChannel->GetRedirectCount(&redirectCount);
  if (redirectCount) {
    aEventTelemetryDataOut.redirectCount =
        mozilla::Some(static_cast<uint32_t>(redirectCount));
  }

  if (!redirectStart.IsNull() && !redirectEnd.IsNull()) {
    TimeDuration redirectTime = redirectEnd - redirectStart;
    if (redirectTime > zeroDuration) {
      aEventTelemetryDataOut.redirectTime =
          mozilla::Some(static_cast<uint32_t>(redirectTime.ToMilliseconds()));
    }
  }

  TimeStamp dnsLookupStart, dnsLookupEnd;
  timedChannel->GetDomainLookupStart(&dnsLookupStart);
  timedChannel->GetDomainLookupEnd(&dnsLookupEnd);

  if (!dnsLookupStart.IsNull() && !dnsLookupEnd.IsNull()) {
    TimeDuration dnsLookupTime = dnsLookupEnd - dnsLookupStart;
    if (dnsLookupTime > zeroDuration) {
      aEventTelemetryDataOut.dnsLookupTime =
          mozilla::Some(static_cast<uint32_t>(dnsLookupTime.ToMilliseconds()));
    }
  }

  TimeStamp navigationStart =
      GetNavigationTiming()->GetNavigationStartTimeStamp();

  if (!responseStart || !navigationStart) {
    return;
  }

  nsAutoCString dnsKey("Native");
  nsAutoCString http3Key;
  nsAutoCString http3WithPriorityKey;
  nsAutoCString earlyHintKey;
  nsCOMPtr<nsIHttpChannelInternal> httpChannel =
      do_QueryInterface(GetChannel());
  if (httpChannel) {
    bool resolvedByTRR = false;
    Unused << httpChannel->GetIsResolvedByTRR(&resolvedByTRR);
    if (resolvedByTRR) {
      if (nsCOMPtr<nsIDNSService> dns =
              do_GetService(NS_DNSSERVICE_CONTRACTID)) {
        dns->GetTRRDomainKey(dnsKey);
      } else {
        // Failed to get the DNS service.
        dnsKey = "(fail)"_ns;
      }
      aEventTelemetryDataOut.trrDomain = mozilla::Some(dnsKey);
    }

    uint32_t major;
    uint32_t minor;
    if (NS_SUCCEEDED(httpChannel->GetResponseVersion(&major, &minor))) {
      if (major == 3) {
        http3Key = "http3"_ns;
        nsCOMPtr<nsIHttpChannel> httpChannel2 = do_QueryInterface(GetChannel());
        nsCString header;
        if (httpChannel2 &&
            NS_SUCCEEDED(
                httpChannel2->GetResponseHeader("priority"_ns, header)) &&
            !header.IsEmpty()) {
          http3WithPriorityKey = "with_priority"_ns;
        } else {
          http3WithPriorityKey = "without_priority"_ns;
        }
      } else if (major == 2) {
        bool supportHttp3 = false;
        if (NS_FAILED(httpChannel->GetSupportsHTTP3(&supportHttp3))) {
          supportHttp3 = false;
        }
        if (supportHttp3) {
          http3Key = "supports_http3"_ns;
        }
      }

      aEventTelemetryDataOut.httpVer = mozilla::Some(major);
    }

    uint32_t earlyHintType = 0;
    Unused << httpChannel->GetEarlyHintLinkType(&earlyHintType);
    if (earlyHintType & LinkStyle::ePRECONNECT) {
      earlyHintKey.Append("preconnect_"_ns);
    }
    if (earlyHintType & LinkStyle::ePRELOAD) {
      earlyHintKey.Append("preload_"_ns);
      earlyHintKey.Append(mPreloadService.GetEarlyHintUsed() ? "1"_ns : "0"_ns);
    }
  }

  TimeStamp asyncOpen;
  timedChannel->GetAsyncOpen(&asyncOpen);
  if (asyncOpen) {
    glean::perf::dns_first_byte.Get(dnsKey).AccumulateRawDuration(
        responseStart - asyncOpen);
  }

  // First Contentful Composite
  if (TimeStamp firstContentfulComposite =
          GetNavigationTiming()->GetFirstContentfulCompositeTimeStamp()) {
    glean::performance_pageload::fcp.AccumulateRawDuration(
        firstContentfulComposite - navigationStart);

    if (!http3Key.IsEmpty()) {
      glean::perf::http3_first_contentful_paint.Get(http3Key)
          .AccumulateRawDuration(firstContentfulComposite - navigationStart);
#ifndef ANDROID
      AccumulateHttp3FcpGleanPref(http3Key,
                                  firstContentfulComposite - navigationStart);
#endif
    }

    if (!http3WithPriorityKey.IsEmpty()) {
      glean::perf::h3p_first_contentful_paint.Get(http3WithPriorityKey)
          .AccumulateRawDuration(firstContentfulComposite - navigationStart);
#ifndef ANDROID
      AccumulatePriorityFcpGleanPref(
          http3WithPriorityKey, firstContentfulComposite - navigationStart);
#endif
    }

    glean::perf::dns_first_contentful_paint.Get(dnsKey).AccumulateRawDuration(
        firstContentfulComposite - navigationStart);

    glean::performance_pageload::fcp_responsestart.AccumulateRawDuration(
        firstContentfulComposite - responseStart);

    TimeDuration fcpTime = firstContentfulComposite - navigationStart;
    if (fcpTime > zeroDuration) {
      aEventTelemetryDataOut.fcpTime =
          mozilla::Some(static_cast<uint32_t>(fcpTime.ToMilliseconds()));
    }
  }

  // Report the most up to date LCP time. For our histogram we actually report
  // this on page unload.
  if (TimeStamp lcpTime =
          GetNavigationTiming()->GetLargestContentfulRenderTimeStamp()) {
    aEventTelemetryDataOut.lcpTime = mozilla::Some(
        static_cast<uint32_t>((lcpTime - navigationStart).ToMilliseconds()));
  }

  // Load event
  if (TimeStamp loadEventStart =
          GetNavigationTiming()->GetLoadEventStartTimeStamp()) {
    glean::performance_pageload::load_time.AccumulateRawDuration(
        loadEventStart - navigationStart);
    if (!http3Key.IsEmpty()) {
      glean::perf::http3_page_load_time.Get(http3Key).AccumulateRawDuration(
          loadEventStart - navigationStart);
    }

    if (!http3WithPriorityKey.IsEmpty()) {
      glean::perf::h3p_page_load_time.Get(http3WithPriorityKey)
          .AccumulateRawDuration(loadEventStart - navigationStart);
    }

    glean::performance_pageload::load_time_responsestart.AccumulateRawDuration(
        loadEventStart - responseStart);

    TimeDuration responseTime = responseStart - navigationStart;
    if (responseTime > zeroDuration) {
      aEventTelemetryDataOut.responseTime =
          mozilla::Some(static_cast<uint32_t>(responseTime.ToMilliseconds()));
    }

    TimeDuration loadTime = loadEventStart - navigationStart;
    if (loadTime > zeroDuration) {
      aEventTelemetryDataOut.loadTime =
          mozilla::Some(static_cast<uint32_t>(loadTime.ToMilliseconds()));
    }

    TimeStamp requestStart;
    timedChannel->GetRequestStart(&requestStart);
    if (requestStart) {
      TimeDuration timeToRequestStart = requestStart - navigationStart;
      if (timeToRequestStart > zeroDuration) {
        aEventTelemetryDataOut.timeToRequestStart = mozilla::Some(
            static_cast<uint32_t>(timeToRequestStart.ToMilliseconds()));
      }
    }

    TimeStamp secureConnectStart;
    TimeStamp connectEnd;
    timedChannel->GetSecureConnectionStart(&secureConnectStart);
    timedChannel->GetConnectEnd(&connectEnd);
    if (secureConnectStart && connectEnd) {
      TimeDuration tlsHandshakeTime = connectEnd - secureConnectStart;
      if (tlsHandshakeTime > zeroDuration) {
        aEventTelemetryDataOut.tlsHandshakeTime = mozilla::Some(
            static_cast<uint32_t>(tlsHandshakeTime.ToMilliseconds()));
      }
    }
  }

#ifdef ACCESSIBILITY
  if (GetAccService() != nullptr) {
    SetPageloadEventFeature(pageload_event::FeatureBits::USING_A11Y);
  }
#endif

  aEventTelemetryDataOut.features = mozilla::Some(mPageloadEventFeatures);
}

void Document::AccumulateJSTelemetry(
    glean::perf::PageLoadExtra& aEventTelemetryDataOut) {
  if (!IsTopLevelContentDocument() || !ShouldIncludeInTelemetry()) {
    return;
  }

  if (!GetScopeObject() || !GetScopeObject()->GetGlobalJSObject()) {
    return;
  }

  AutoJSContext cx;
  JSObject* globalObject = GetScopeObject()->GetGlobalJSObject();
  JSAutoRealm ar(cx, globalObject);
  JS::JSTimers timers = JS::GetJSTimers(cx);

  if (!timers.executionTime.IsZero()) {
    glean::javascript_pageload::execution_time.AccumulateRawDuration(
        timers.executionTime);
    aEventTelemetryDataOut.jsExecTime = mozilla::Some(
        static_cast<uint32_t>(timers.executionTime.ToMilliseconds()));
  }

  if (!timers.delazificationTime.IsZero()) {
    glean::javascript_pageload::delazification_time.AccumulateRawDuration(
        timers.delazificationTime);
  }

  if (!timers.xdrEncodingTime.IsZero()) {
    glean::javascript_pageload::xdr_encode_time.AccumulateRawDuration(
        timers.xdrEncodingTime);
  }

  if (!timers.baselineCompileTime.IsZero()) {
    glean::javascript_pageload::baseline_compile_time.AccumulateRawDuration(
        timers.baselineCompileTime);
  }

  if (!timers.gcTime.IsZero()) {
    glean::javascript_pageload::gc_time.AccumulateRawDuration(timers.gcTime);
  }

  if (!timers.protectTime.IsZero()) {
    glean::javascript_pageload::protect_time.AccumulateRawDuration(
        timers.protectTime);
    // GLAM EXPERIMENT
    // This metric is temporary, disabled by default, and will be enabled only
    // for the purpose of experimenting with client-side sampling of data for
    // GLAM use. See Bug 1947604 for more information.
    glean::glam_experiment::protect_time.AccumulateRawDuration(
        timers.protectTime);
    // END GLAM EXPERIMENT
  }
}

Document::~Document() {
  MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug, ("DOCUMENT %p destroyed", this));
  MOZ_ASSERT(!IsTopLevelContentDocument() || !IsResourceDoc(),
             "Can't be top-level and a resource doc at the same time");

  NS_ASSERTION(!mIsShowing, "Destroying a currently-showing document");

  if (IsTopLevelContentDocument()) {
    RemoveToplevelLoadingDocument(this);
  }

  mInDestructor = true;
  mInUnlinkOrDeletion = true;

  mozilla::DropJSObjects(this);

  // Clear mObservers to keep it in sync with the mutationobserver list
  mObservers.Clear();

  mIntersectionObservers.Clear();

  if (mStyleSheetSetList) {
    mStyleSheetSetList->Disconnect();
  }

  if (mAnimationController) {
    mAnimationController->Disconnect();
  }

  MOZ_ASSERT(mTimelines.isEmpty());

  mParentDocument = nullptr;

  // Kill the subdocument map, doing this will release its strong
  // references, if any.
  mSubDocuments = nullptr;

  nsAutoScriptBlocker scriptBlocker;

  // Destroy link map now so we don't waste time removing
  // links one by one
  DestroyElementMaps();

  // Invalidate cached array of child nodes
  InvalidateChildNodes();

  // We should not have child nodes when destructor is called,
  // since child nodes keep their owner document alive.
  MOZ_ASSERT(!HasChildren());

  mCachedRootElement = nullptr;

  for (auto& sheets : mAdditionalSheets) {
    UnlinkStyleSheets(sheets);
  }

  if (mAttributeStyles) {
    mAttributeStyles->SetOwningDocument(nullptr);
  }

  if (mListenerManager) {
    mListenerManager->Disconnect();
    UnsetFlags(NODE_HAS_LISTENERMANAGER);
  }

  if (mScriptLoader) {
    mScriptLoader->DropDocumentReference();
  }

  if (mCSSLoader) {
    // Could be null here if Init() failed or if we have been unlinked.
    mCSSLoader->DropDocumentReference();
  }

  if (mStyleImageLoader) {
    mStyleImageLoader->DropDocumentReference();
  }

  if (mXULBroadcastManager) {
    mXULBroadcastManager->DropDocumentReference();
  }

  if (mXULPersist) {
    mXULPersist->DropDocumentReference();
  }

  if (mPermissionDelegateHandler) {
    mPermissionDelegateHandler->DropDocumentReference();
  }

  mHeaderData = nullptr;

  mPendingTitleChangeEvent.Revoke();

  MOZ_ASSERT(mDOMMediaQueryLists.isEmpty(),
             "must not have media query lists left");

  if (mNodeInfoManager) {
    mNodeInfoManager->DropDocumentReference();
  }

  if (mDocGroup) {
    MOZ_ASSERT(mDocGroup->GetBrowsingContextGroup());
    mDocGroup->GetBrowsingContextGroup()->RemoveDocument(this, mDocGroup);
  }

  UnlinkOriginalDocumentIfStatic();

  UnregisterFromMemoryReportingForDataDocument();

  if (isInList()) {
    MOZ_ASSERT(AllDocumentsList().contains(this));
    remove();
  }
}

void Document::DropStyleSet() { mStyleSet = nullptr; }

NS_INTERFACE_TABLE_HEAD(Document)
  NS_WRAPPERCACHE_INTERFACE_TABLE_ENTRY
  NS_INTERFACE_TABLE_BEGIN
    NS_INTERFACE_TABLE_ENTRY_AMBIGUOUS(Document, nsISupports, nsINode)
    NS_INTERFACE_TABLE_ENTRY(Document, nsINode)
    NS_INTERFACE_TABLE_ENTRY(Document, Document)
    NS_INTERFACE_TABLE_ENTRY(Document, nsIScriptObjectPrincipal)
    NS_INTERFACE_TABLE_ENTRY(Document, EventTarget)
    NS_INTERFACE_TABLE_ENTRY(Document, nsISupportsWeakReference)
  NS_INTERFACE_TABLE_END
  NS_INTERFACE_TABLE_TO_MAP_SEGUE_CYCLE_COLLECTION(Document)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(Document)
NS_IMPL_CYCLE_COLLECTING_RELEASE_WITH_LAST_RELEASE(Document, LastRelease())

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_BEGIN(Document)
  if (Element::CanSkip(tmp, aRemovingAllowed)) {
    EventListenerManager* elm = tmp->GetExistingListenerManager();
    if (elm) {
      elm->MarkForCC();
    }
    return true;
  }
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_BEGIN(Document)
  return Element::CanSkipInCC(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_IN_CC_END

NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_BEGIN(Document)
  return Element::CanSkipThis(tmp);
NS_IMPL_CYCLE_COLLECTION_CAN_SKIP_THIS_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INTERNAL(Document)
  if (MOZ_UNLIKELY(cb.WantDebugInfo())) {
    char name[512];
    nsAutoCString loadedAsData;
    if (tmp->IsLoadedAsData()) {
      loadedAsData.AssignLiteral("data");
    } else {
      loadedAsData.AssignLiteral("normal");
    }
    uint32_t nsid = tmp->GetDefaultNamespaceID();
    nsAutoCString uri;
    if (tmp->mDocumentURI) uri = tmp->mDocumentURI->GetSpecOrDefault();
    static const char* kNSURIs[] = {"([none])", "(xmlns)", "(xml)",
                                    "(xhtml)",  "(XLink)", "(XSLT)",
                                    "(MathML)", "(RDF)",   "(XUL)"};
    if (nsid < std::size(kNSURIs)) {
      SprintfLiteral(name, "Document %s %s %s", loadedAsData.get(),
                     kNSURIs[nsid], uri.get());
    } else {
      SprintfLiteral(name, "Document %s %s", loadedAsData.get(), uri.get());
    }
    cb.DescribeRefCountedNode(tmp->mRefCnt.get(), name);
  } else {
    NS_IMPL_CYCLE_COLLECTION_DESCRIBE(Document, tmp->mRefCnt.get())
  }

  if (!nsINode::Traverse(tmp, cb)) {
    return NS_SUCCESS_INTERRUPTED_TRAVERSE;
  }

  tmp->mExternalResourceMap.Traverse(&cb);

  // Traverse all Document pointer members.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSecurityInfo)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDisplayDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFontFaceSet)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mReadyForIdle)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentL10n)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFragmentDirective)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHighlightRegistry)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingFullscreenEvents)

  // Traverse all Document nsCOMPtrs.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParser)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScriptGlobalObject)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mListenerManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStyleSheetSetList)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScriptLoader)

  DocumentOrShadowRoot::Traverse(tmp, cb);

  if (tmp->mRadioGroupContainer) {
    RadioGroupContainer::Traverse(tmp->mRadioGroupContainer.get(), cb);
  }

  for (auto& sheets : tmp->mAdditionalSheets) {
    tmp->TraverseStyleSheets(sheets, "mAdditionalSheets[<origin>][i]", cb);
  }

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOnloadBlocker)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLazyLoadObserver)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mElementsObservedForLastRememberedSize)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDOMImplementation)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mImageMaps)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOrientationPendingPromise)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mOriginalDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCachedEncoder)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocumentTimeline)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScrollTimelineAnimationTracker)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTemplateContentsOwner)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mChildrenCollection)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mImages);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mEmbeds);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mLinks);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mForms);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mScripts);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mApplets);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAnchors);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAnonymousContents)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCommandDispatcher)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFeaturePolicy)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPermissionDelegateHandler)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSuppressedEventListener)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPrototypeDocument)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMidasCommandManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAll)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mActiveViewTransition)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mViewTransitionUpdateCallbacks)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mDocGroup)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFrameRequestManager)

  // Traverse all our nsCOMArrays.
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPreloadingImages)

  // Traverse animation components
  if (tmp->mAnimationController) {
    tmp->mAnimationController->Traverse(&cb);
  }

  if (tmp->mSubDocuments) {
    for (auto iter = tmp->mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
      auto entry = static_cast<SubDocMapEntry*>(iter.Get());

      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mSubDocuments entry->mKey");
      cb.NoteXPCOMChild(entry->mKey);
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb,
                                         "mSubDocuments entry->mSubDocument");
      cb.NoteXPCOMChild(ToSupports(entry->mSubDocument));
    }
  }

  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mCSSLoader)

  // We own only the items in mDOMMediaQueryLists that have listeners;
  // this reference is managed by their AddListener and RemoveListener
  // methods.
  for (MediaQueryList* mql = tmp->mDOMMediaQueryLists.getFirst(); mql;
       mql = static_cast<LinkedListElement<MediaQueryList>*>(mql)->getNext()) {
    if (mql->HasListeners() &&
        NS_SUCCEEDED(mql->CheckCurrentGlobalCorrectness())) {
      NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mDOMMediaQueryLists item");
      cb.NoteXPCOMChild(static_cast<EventTarget*>(mql));
    }
  }

  // XXX: This should be not needed once bug 1569185 lands.
  for (const auto& entry : tmp->mL10nProtoElements) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(cb, "mL10nProtoElements key");
    cb.NoteXPCOMChild(entry.GetKey());
    CycleCollectionNoteChild(cb, entry.GetWeak(), "mL10nProtoElements value");
  }

  for (size_t i = 0; i < tmp->mPendingFrameStaticClones.Length(); ++i) {
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingFrameStaticClones[i].mElement);
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(
        mPendingFrameStaticClones[i].mStaticCloneOf);
  }

  for (auto& tableEntry : tmp->mActiveLocks) {
    ImplCycleCollectionTraverse(cb, *tableEntry.GetModifiableData(),
                                "mActiveLocks entry", 0);
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_CLASS(Document)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(Document)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mCachedStateObject)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(Document)
  tmp->mInUnlinkOrDeletion = true;

  tmp->SetStateObject(nullptr);

  // Clear out our external resources
  tmp->mExternalResourceMap.Shutdown();

  nsAutoScriptBlocker scriptBlocker;

  nsINode::Unlink(tmp);

  while (tmp->HasChildren()) {
    // Hold a strong ref to the node when we remove it, because we may be
    // the last reference to it.
    // If this code changes, change the corresponding code in Document's
    // unlink impl and ContentUnbinder::UnbindSubtree.
    nsCOMPtr<nsIContent> child = tmp->GetLastChild();
    tmp->DisconnectChild(child);
    child->UnbindFromTree();
  }

  tmp->UnlinkOriginalDocumentIfStatic();

  tmp->mCachedRootElement = nullptr;  // Avoid a dangling pointer

  tmp->SetScriptGlobalObject(nullptr);

  for (auto& sheets : tmp->mAdditionalSheets) {
    tmp->UnlinkStyleSheets(sheets);
  }

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSecurityInfo)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDisplayDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLazyLoadObserver)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mElementsObservedForLastRememberedSize);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFontFaceSet)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReadyForIdle)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentL10n)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFragmentDirective)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mHighlightRegistry)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingFullscreenEvents)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mParser)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOnloadBlocker)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDOMImplementation)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mImageMaps)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOrientationPendingPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mOriginalDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCachedEncoder)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mDocumentTimeline)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mScrollTimelineAnimationTracker)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTemplateContentsOwner)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mChildrenCollection)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mImages);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mEmbeds);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mLinks);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mForms);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mScripts);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mApplets);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAnchors);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAnonymousContents)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mCommandDispatcher)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFeaturePolicy)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPermissionDelegateHandler)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSuppressedEventListener)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPrototypeDocument)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMidasCommandManager)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAll)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mActiveViewTransition)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mViewTransitionUpdateCallbacks)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mReferrerInfo)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPreloadReferrerInfo)

  if (tmp->mDocGroup && tmp->mDocGroup->GetBrowsingContextGroup()) {
    tmp->mDocGroup->GetBrowsingContextGroup()->RemoveDocument(tmp,
                                                              tmp->mDocGroup);
  }
  tmp->mDocGroup = nullptr;

  if (tmp->IsTopLevelContentDocument()) {
    RemoveToplevelLoadingDocument(tmp);
  }

  tmp->mParentDocument = nullptr;

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPreloadingImages)

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mIntersectionObservers)

  if (tmp->mListenerManager) {
    tmp->mListenerManager->Disconnect();
    tmp->UnsetFlags(NODE_HAS_LISTENERMANAGER);
    tmp->mListenerManager = nullptr;
  }

  if (tmp->mStyleSheetSetList) {
    tmp->mStyleSheetSetList->Disconnect();
    tmp->mStyleSheetSetList = nullptr;
  }

  tmp->mSubDocuments = nullptr;

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFrameRequestManager)

  DocumentOrShadowRoot::Unlink(tmp);

  tmp->mRadioGroupContainer = nullptr;

  // Document has a pretty complex destructor, so we're going to
  // assume that *most* cycles you actually want to break somewhere
  // else, and not unlink an awful lot here.

  tmp->mExpandoAndGeneration.OwnerUnlinked();

  if (tmp->mAnimationController) {
    tmp->mAnimationController->Unlink();
  }

  tmp->mPendingTitleChangeEvent.Revoke();

  if (tmp->mCSSLoader) {
    tmp->mCSSLoader->DropDocumentReference();
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mCSSLoader)
  }

  if (tmp->mScriptLoader) {
    tmp->mScriptLoader->DropDocumentReference();
    NS_IMPL_CYCLE_COLLECTION_UNLINK(mScriptLoader)
  }

  // We own only the items in mDOMMediaQueryLists that have listeners;
  // this reference is managed by their AddListener and RemoveListener
  // methods.
  for (MediaQueryList* mql = tmp->mDOMMediaQueryLists.getFirst(); mql;) {
    MediaQueryList* next =
        static_cast<LinkedListElement<MediaQueryList>*>(mql)->getNext();
    mql->Disconnect();
    mql = next;
  }

  tmp->mPendingFrameStaticClones.Clear();

  tmp->mActiveLocks.Clear();

  if (tmp->isInList()) {
    MOZ_ASSERT(AllDocumentsList().contains(tmp));
    tmp->remove();
  }

  tmp->mInUnlinkOrDeletion = false;

  tmp->UnregisterFromMemoryReportingForDataDocument();

  NS_IMPL_CYCLE_COLLECTION_UNLINK(mL10nProtoElements)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_PTR
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

nsresult Document::Init(nsIPrincipal* aPrincipal,
                        nsIPrincipal* aPartitionedPrincipal) {
  if (mCSSLoader || mStyleImageLoader || mNodeInfoManager || mScriptLoader) {
    return NS_ERROR_ALREADY_INITIALIZED;
  }

  // Force initialization.
  mOnloadBlocker = new OnloadBlocker();
  mStyleImageLoader = new css::ImageLoader(this);

  mNodeInfoManager = new nsNodeInfoManager(this, aPrincipal);

  // mNodeInfo keeps NodeInfoManager alive!
  mNodeInfo = mNodeInfoManager->GetDocumentNodeInfo();
  NS_ENSURE_TRUE(mNodeInfo, NS_ERROR_OUT_OF_MEMORY);
  MOZ_ASSERT(mNodeInfo->NodeType() == DOCUMENT_NODE,
             "Bad NodeType in aNodeInfo");

  NS_ASSERTION(OwnerDoc() == this, "Our nodeinfo is busted!");

  mCSSLoader = new css::Loader(this);
  // Assume we're not quirky, until we know otherwise
  mCSSLoader->SetCompatibilityMode(eCompatibility_FullStandards);

  // If after creation the owner js global is not set for a document
  // we use the default compartment for this document, instead of creating
  // wrapper in some random compartment when the document is exposed to js
  // via some events.
  nsCOMPtr<nsIGlobalObject> global =
      xpc::NativeGlobal(xpc::PrivilegedJunkScope());
  NS_ENSURE_TRUE(global, NS_ERROR_FAILURE);
  mScopeObject = do_GetWeakReference(global);
  MOZ_ASSERT(mScopeObject);

  mScriptLoader = new dom::ScriptLoader(this);

  // we need to create a policy here so getting the policy within
  // ::Policy() can *always* return a non null policy
  mFeaturePolicy = new dom::FeaturePolicy(this);
  mFeaturePolicy->SetDefaultOrigin(NodePrincipal());

  if (aPrincipal) {
    SetPrincipals(aPrincipal, aPartitionedPrincipal);
  } else {
    RecomputeResistFingerprinting();
  }

  AllDocumentsList().insertBack(this);

  return NS_OK;
}

void Document::RemoveAllProperties() { PropertyTable().RemoveAllProperties(); }

void Document::RemoveAllPropertiesFor(nsINode* aNode) {
  PropertyTable().RemoveAllPropertiesFor(aNode);
}

void Document::Reset(nsIChannel* aChannel, nsILoadGroup* aLoadGroup) {
  nsCOMPtr<nsIURI> uri;
  nsCOMPtr<nsIPrincipal> principal;
  nsCOMPtr<nsIPrincipal> partitionedPrincipal;
  if (aChannel) {
    mIsInPrivateBrowsing = NS_UsePrivateBrowsing(aChannel);

    // Note: this code is duplicated in PrototypeDocumentContentSink::Init and
    // nsScriptSecurityManager::GetChannelResultPrincipals.
    // Note: this should match the uri used for the OnNewURI call in
    //       nsDocShell::CreateDocumentViewer.
    NS_GetFinalChannelURI(aChannel, getter_AddRefs(uri));

    nsIScriptSecurityManager* securityManager =
        nsContentUtils::GetSecurityManager();
    if (securityManager) {
      securityManager->GetChannelResultPrincipals(
          aChannel, getter_AddRefs(principal),
          getter_AddRefs(partitionedPrincipal));
    }
  }

  bool equal = principal->Equals(partitionedPrincipal);

  principal = MaybeDowngradePrincipal(principal);
  if (equal) {
    partitionedPrincipal = principal;
  } else {
    partitionedPrincipal = MaybeDowngradePrincipal(partitionedPrincipal);
  }

  ResetToURI(uri, aLoadGroup, principal, partitionedPrincipal);

  // Note that, since mTiming does not change during a reset, the
  // navigationStart time remains unchanged and therefore any future new
  // timeline will have the same global clock time as the old one.
  mDocumentTimeline = nullptr;

  if (nsCOMPtr<nsIPropertyBag2> bag = do_QueryInterface(aChannel)) {
    if (nsCOMPtr<nsIURI> baseURI = do_GetProperty(bag, u"baseURI"_ns)) {
      mDocumentBaseURI = baseURI.forget();
      mChromeXHRDocBaseURI = nullptr;
    }
  }

  mChannel = aChannel;
  RecomputeResistFingerprinting();
  MaybeRecomputePartitionKey();
}

void Document::DisconnectNodeTree() {
  // Delete references to sub-documents and kill the subdocument map,
  // if any. This is not strictly needed, but makes the node tree
  // teardown a bit faster.
  mSubDocuments = nullptr;

  bool oldVal = mInUnlinkOrDeletion;
  mInUnlinkOrDeletion = true;
  {  // Scope for update
    MOZ_AUTO_DOC_UPDATE(this, true);

    // Destroy link map now so we don't waste time removing
    // links one by one
    DestroyElementMaps();

    // Invalidate cached array of child nodes
    InvalidateChildNodes();

    while (nsCOMPtr<nsIContent> content = GetLastChild()) {
      nsMutationGuard::DidMutate();
      MutationObservers::NotifyContentWillBeRemoved(this, content, nullptr);
      DisconnectChild(content);
      if (content == mCachedRootElement) {
        // Immediately clear mCachedRootElement, now that it's been removed
        // from mChildren, so that GetRootElement() will stop returning this
        // now-stale value.
        mCachedRootElement = nullptr;
      }
      content->UnbindFromTree();
    }
    MOZ_ASSERT(!mCachedRootElement,
               "After removing all children, there should be no root elem");
  }
  mInUnlinkOrDeletion = oldVal;
}

void Document::ResetToURI(nsIURI* aURI, nsILoadGroup* aLoadGroup,
                          nsIPrincipal* aPrincipal,
                          nsIPrincipal* aPartitionedPrincipal) {
  MOZ_ASSERT(aURI, "Null URI passed to ResetToURI");
  MOZ_ASSERT(!!aPrincipal == !!aPartitionedPrincipal);

  MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug,
          ("DOCUMENT %p ResetToURI %s", this, aURI->GetSpecOrDefault().get()));

  mSecurityInfo = nullptr;

  nsCOMPtr<nsILoadGroup> group = do_QueryReferent(mDocumentLoadGroup);
  if (!aLoadGroup || group != aLoadGroup) {
    mDocumentLoadGroup = nullptr;
  }

  DisconnectNodeTree();

  // Reset our stylesheets
  ResetStylesheetsToURI(aURI);

  // Release the listener manager
  if (mListenerManager) {
    mListenerManager->Disconnect();
    mListenerManager = nullptr;
  }

  // Release the stylesheets list.
  mDOMStyleSheets = nullptr;

  // Release our principal after tearing down the document, rather than before.
  // This ensures that, during teardown, the document and the dying window
  // (which already nulled out its document pointer and cached the principal)
  // have matching principals.
  SetPrincipals(nullptr, nullptr);

  // Clear the original URI so SetDocumentURI sets it.
  mOriginalURI = nullptr;

  SetDocumentURI(aURI);
  mChromeXHRDocURI = nullptr;
  // If mDocumentBaseURI is null, Document::GetBaseURI() returns
  // mDocumentURI.
  mDocumentBaseURI = nullptr;
  mChromeXHRDocBaseURI = nullptr;

  if (aLoadGroup) {
    nsCOMPtr<nsIInterfaceRequestor> callbacks;
    aLoadGroup->GetNotificationCallbacks(getter_AddRefs(callbacks));
    if (callbacks) {
      nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(callbacks);
      if (loadContext) {
        // This is asserting that if we previously set mIsInPrivateBrowsing
        // to true from the channel in Document::Reset, that the loadContext
        // also believes it to be true.
        MOZ_ASSERT(!mIsInPrivateBrowsing ||
                   mIsInPrivateBrowsing == loadContext->UsePrivateBrowsing());
        mIsInPrivateBrowsing = loadContext->UsePrivateBrowsing();
      }
    }

    mDocumentLoadGroup = do_GetWeakReference(aLoadGroup);
    // there was an assertion here that aLoadGroup was not null.  This
    // is no longer valid: nsDocShell::SetDocument does not create a
    // load group, and it works just fine

    // XXXbz what does "just fine" mean exactly?  And given that there
    // is no nsDocShell::SetDocument, what is this talking about?

    if (IsContentDocument()) {
      // Inform the associated request context about this load start so
      // any of its internal load progress flags gets reset.
      nsCOMPtr<nsIRequestContextService> rcsvc =
          net::RequestContextService::GetOrCreate();
      if (rcsvc) {
        nsCOMPtr<nsIRequestContext> rc;
        rcsvc->GetRequestContextFromLoadGroup(aLoadGroup, getter_AddRefs(rc));
        if (rc) {
          rc->BeginLoad();
        }
      }
    }
  }

  mLastModified.Truncate();
  // XXXbz I guess we're assuming that the caller will either pass in
  // a channel with a useful type or call SetContentType?
  SetContentType(""_ns);
  mContentLanguage = nullptr;
  mBaseTarget.Truncate();

  mXMLDeclarationBits = 0;

  // Now get our new principal
  if (aPrincipal) {
    SetPrincipals(aPrincipal, aPartitionedPrincipal);
  } else {
    nsIScriptSecurityManager* securityManager =
        nsContentUtils::GetSecurityManager();
    if (securityManager) {
      nsCOMPtr<nsILoadContext> loadContext(mDocumentContainer);

      if (!loadContext && aLoadGroup) {
        nsCOMPtr<nsIInterfaceRequestor> cbs;
        aLoadGroup->GetNotificationCallbacks(getter_AddRefs(cbs));
        loadContext = do_GetInterface(cbs);
      }

      MOZ_ASSERT(loadContext,
                 "must have a load context or pass in an explicit principal");

      nsCOMPtr<nsIPrincipal> principal;
      nsresult rv = securityManager->GetLoadContextContentPrincipal(
          mDocumentURI, loadContext, getter_AddRefs(principal));
      if (NS_SUCCEEDED(rv)) {
        SetPrincipals(principal, principal);
      }
    }
  }

  if (mFontFaceSet) {
    mFontFaceSet->RefreshStandardFontLoadPrincipal();
  }

  // Refresh the principal on the realm.
  if (nsPIDOMWindowInner* win = GetInnerWindow()) {
    nsGlobalWindowInner::Cast(win)->RefreshRealmPrincipal();
  }
}

already_AddRefed<nsIPrincipal> Document::MaybeDowngradePrincipal(
    nsIPrincipal* aPrincipal) {
  if (!aPrincipal) {
    return nullptr;
  }

  // We can't load a document with an expanded principal. If we're given one,
  // automatically downgrade it to the last principal it subsumes (which is the
  // extension principal, in the case of extension content scripts).
  auto* basePrin = BasePrincipal::Cast(aPrincipal);
  if (basePrin->Is<ExpandedPrincipal>()) {
    MOZ_DIAGNOSTIC_CRASH(
        "Should never try to create a document with "
        "an expanded principal");

    auto* expanded = basePrin->As<ExpandedPrincipal>();
    return do_AddRef(expanded->AllowList().LastElement());
  }

  if (aPrincipal->IsSystemPrincipal() && mDocumentContainer) {
    // We basically want the parent document here, but because this is very
    // early in the load, GetInProcessParentDocument() returns null, so we use
    // the docshell hierarchy to get this information instead.
    if (RefPtr<BrowsingContext> parent =
            mDocumentContainer->GetBrowsingContext()->GetParent()) {
      auto* parentWin = nsGlobalWindowOuter::Cast(parent->GetDOMWindow());
      if (!parentWin || !parentWin->GetPrincipal()->IsSystemPrincipal()) {
        nsCOMPtr<nsIPrincipal> nullPrincipal =
            NullPrincipal::CreateWithoutOriginAttributes();
        return nullPrincipal.forget();
      }
    }
  }
  nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  return principal.forget();
}

size_t Document::FindDocStyleSheetInsertionPoint(const StyleSheet& aSheet) {
  ServoStyleSet& styleSet = EnsureStyleSet();

  // lowest index first
  const size_t newDocIndex = StyleOrderIndexOfSheet(aSheet);
  MOZ_ASSERT(newDocIndex != mStyleSheets.NoIndex);

  size_t index = styleSet.SheetCount(StyleOrigin::Author);
  while (index--) {
    auto* sheet = styleSet.SheetAt(StyleOrigin::Author, index);
    MOZ_ASSERT(sheet);
    if (!sheet->GetAssociatedDocumentOrShadowRoot()) {
      // If the sheet is not owned by the document it should be an author sheet
      // registered at nsStyleSheetService, or an additional sheet. In that case
      // the doc sheet should end up before it.
      // FIXME(emilio): Additional stylesheets inconsistently end up with
      // associated document, depending on which code-path adds them. Fix this.
      MOZ_ASSERT(
          nsStyleSheetService::GetInstance()->AuthorStyleSheets()->Contains(
              sheet) ||
          mAdditionalSheets[eAuthorSheet].Contains(sheet));
      continue;
    }
    size_t sheetDocIndex = StyleOrderIndexOfSheet(*sheet);
    if (MOZ_UNLIKELY(sheetDocIndex == mStyleSheets.NoIndex)) {
      MOZ_ASSERT_UNREACHABLE("Which stylesheet can hit this?");
      continue;
    }
    MOZ_ASSERT(sheetDocIndex != newDocIndex);
    if (sheetDocIndex < newDocIndex) {
      // We found a document-owned sheet. All of them go together, so if the
      // current sheet goes before ours, we're at the right index already.
      return index + 1;
    }
    // Otherwise keep looking. Unfortunately we can't do something clever like:
    //
    // return index - sheetDocIndex + newDocIndex;
    //
    // Or so, because we need to deal with disabled / non-applicable sheets
    // which are not in the styleset, even though they're in the document.
  }
  // We found no sheet that goes before us, so we're index 0.
  return 0;
}

void Document::ResetStylesheetsToURI(nsIURI* aURI) {
  MOZ_ASSERT(aURI);

  ClearAdoptedStyleSheets();
  ServoStyleSet& styleSet = EnsureStyleSet();

  auto ClearSheetList = [&](nsTArray<RefPtr<StyleSheet>>& aSheetList) {
    for (auto& sheet : Reversed(aSheetList)) {
      sheet->ClearAssociatedDocumentOrShadowRoot();
      if (mStyleSetFilled) {
        styleSet.RemoveStyleSheet(*sheet);
      }
    }
    aSheetList.Clear();
  };
  ClearSheetList(mStyleSheets);
  for (auto& sheets : mAdditionalSheets) {
    ClearSheetList(sheets);
  }
  if (mStyleSetFilled) {
    if (auto* ss = nsStyleSheetService::GetInstance()) {
      for (auto& sheet : Reversed(*ss->AuthorStyleSheets())) {
        MOZ_ASSERT(!sheet->GetAssociatedDocumentOrShadowRoot());
        if (sheet->IsApplicable()) {
          styleSet.RemoveStyleSheet(*sheet);
        }
      }
    }
  }

  // Now reset our inline style and attribute sheets.
  if (mAttributeStyles) {
    mAttributeStyles->Reset();
    mAttributeStyles->SetOwningDocument(this);
  } else {
    mAttributeStyles = new AttributeStyles(this);
  }

  if (mStyleSetFilled) {
    FillStyleSetDocumentSheets();

    if (styleSet.StyleSheetsHaveChanged()) {
      ApplicableStylesChanged();
    }
  }
}

static void AppendSheetsToStyleSet(
    ServoStyleSet* aStyleSet, const nsTArray<RefPtr<StyleSheet>>& aSheets) {
  for (StyleSheet* sheet : Reversed(aSheets)) {
    aStyleSet->AppendStyleSheet(*sheet);
  }
}

void Document::FillStyleSetUserAndUASheets() {
  // Make sure this does the same thing as PresShell::Add{User,Agent}Sheet wrt
  // ordering.

  // The document will fill in the document sheets when we create the presshell
  auto* cache = GlobalStyleSheetCache::Singleton();

  nsStyleSheetService* sheetService = nsStyleSheetService::GetInstance();
  MOZ_ASSERT(sheetService,
             "should never be creating a StyleSet after the style sheet "
             "service has gone");

  ServoStyleSet& styleSet = EnsureStyleSet();
  for (StyleSheet* sheet : *sheetService->UserStyleSheets()) {
    styleSet.AppendStyleSheet(*sheet);
  }

  StyleSheet* sheet = IsInChromeDocShell() ? cache->GetUserChromeSheet()
                                           : cache->GetUserContentSheet();
  if (sheet) {
    styleSet.AppendStyleSheet(*sheet);
  }

  styleSet.AppendStyleSheet(*cache->UASheet());

  if (MOZ_LIKELY(NodeInfoManager()->MathMLEnabled())) {
    styleSet.AppendStyleSheet(*cache->MathMLSheet());
  }

  if (MOZ_LIKELY(NodeInfoManager()->SVGEnabled())) {
    styleSet.AppendStyleSheet(*cache->SVGSheet());
  }

  styleSet.AppendStyleSheet(*cache->HTMLSheet());

  if (nsLayoutUtils::ShouldUseNoFramesSheet(this)) {
    styleSet.AppendStyleSheet(*cache->NoFramesSheet());
  }

  styleSet.AppendStyleSheet(*cache->CounterStylesSheet());

  // Only load the full XUL sheet if we'll need it.
  if (LoadsFullXULStyleSheetUpFront()) {
    styleSet.AppendStyleSheet(*cache->XULSheet());
  }

  styleSet.AppendStyleSheet(*cache->FormsSheet());
  styleSet.AppendStyleSheet(*cache->ScrollbarsSheet());

  for (StyleSheet* sheet : *sheetService->AgentStyleSheets()) {
    styleSet.AppendStyleSheet(*sheet);
  }

  MOZ_ASSERT(!mQuirkSheetAdded);
  if (NeedsQuirksSheet()) {
    styleSet.AppendStyleSheet(*cache->QuirkSheet());
    mQuirkSheetAdded = true;
  }
}

void Document::FillStyleSet() {
  MOZ_ASSERT(!mStyleSetFilled);
  FillStyleSetUserAndUASheets();
  FillStyleSetDocumentSheets();
  mStyleSetFilled = true;
}

void Document::FillStyleSetDocumentSheets() {
  ServoStyleSet& styleSet = EnsureStyleSet();
  MOZ_ASSERT(styleSet.SheetCount(StyleOrigin::Author) == 0,
             "Style set already has document sheets?");

  // Sheets are added in reverse order to avoid worst-case time complexity when
  // looking up the index of a sheet.
  //
  // Note that usually appending is faster (rebuilds less stuff in the
  // styleset), but in this case it doesn't matter since we're filling the
  // styleset from scratch anyway.
  for (StyleSheet* sheet : Reversed(mStyleSheets)) {
    if (sheet->IsApplicable()) {
      styleSet.AddDocStyleSheet(*sheet);
    }
  }

  EnumerateUniqueAdoptedStyleSheetsBackToFront([&](StyleSheet& aSheet) {
    if (aSheet.IsApplicable()) {
      styleSet.AddDocStyleSheet(aSheet);
    }
  });

  nsStyleSheetService* sheetService = nsStyleSheetService::GetInstance();
  for (StyleSheet* sheet : *sheetService->AuthorStyleSheets()) {
    styleSet.AppendStyleSheet(*sheet);
  }

  AppendSheetsToStyleSet(&styleSet, mAdditionalSheets[eAgentSheet]);
  AppendSheetsToStyleSet(&styleSet, mAdditionalSheets[eUserSheet]);
  AppendSheetsToStyleSet(&styleSet, mAdditionalSheets[eAuthorSheet]);
}

void Document::CompatibilityModeChanged() {
  MOZ_ASSERT(IsHTMLOrXHTML());
  CSSLoader()->SetCompatibilityMode(mCompatMode);

  if (mStyleSet) {
    mStyleSet->CompatibilityModeChanged();
  }
  if (!mStyleSetFilled) {
    MOZ_ASSERT(!mQuirkSheetAdded);
    return;
  }

  MOZ_ASSERT(mStyleSet);
  if (PresShell* presShell = GetPresShell()) {
    // Selectors may have become case-sensitive / case-insensitive, the stylist
    // has already performed the relevant invalidation.
    presShell->EnsureStyleFlush();
  }
  if (mQuirkSheetAdded == NeedsQuirksSheet()) {
    return;
  }
  auto* cache = GlobalStyleSheetCache::Singleton();
  StyleSheet* sheet = cache->QuirkSheet();
  if (mQuirkSheetAdded) {
    mStyleSet->RemoveStyleSheet(*sheet);
  } else {
    mStyleSet->AppendStyleSheet(*sheet);
  }
  mQuirkSheetAdded = !mQuirkSheetAdded;
  ApplicableStylesChanged();
}

void Document::SetCompatibilityMode(nsCompatibility aMode) {
  NS_ASSERTION(IsHTMLDocument() || aMode == eCompatibility_FullStandards,
               "Bad compat mode for XHTML document!");

  if (mCompatMode == aMode) {
    return;
  }
  mCompatMode = aMode;
  CompatibilityModeChanged();
  // Trigger recomputation of the nsViewportInfo the next time it's queried.
  mViewportType = Unknown;
}

static void WarnIfSandboxIneffective(nsIDocShell* aDocShell,
                                     uint32_t aSandboxFlags,
                                     nsIChannel* aChannel) {
  // If the document permits allow-top-navigation and
  // allow-top-navigation-by-user-activation this will permit all top
  // navigation.
  if (aSandboxFlags != SANDBOXED_NONE &&
      !(aSandboxFlags & SANDBOXED_TOPLEVEL_NAVIGATION) &&
      !(aSandboxFlags & SANDBOXED_TOPLEVEL_NAVIGATION_USER_ACTIVATION)) {
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "Iframe Sandbox"_ns,
        aDocShell->GetDocument(), nsContentUtils::eSECURITY_PROPERTIES,
        "BothAllowTopNavigationAndUserActivationPresent");
  }
  // If the document is sandboxed (via the HTML5 iframe sandbox
  // attribute) and both the allow-scripts and allow-same-origin
  // keywords are supplied, the sandboxed document can call into its
  // parent document and remove its sandboxing entirely - we print a
  // warning to the web console in this case.
  if (aSandboxFlags & SANDBOXED_NAVIGATION &&
      !(aSandboxFlags & SANDBOXED_SCRIPTS) &&
      !(aSandboxFlags & SANDBOXED_ORIGIN)) {
    RefPtr<BrowsingContext> bc = aDocShell->GetBrowsingContext();
    MOZ_ASSERT(bc->IsInProcess());

    RefPtr<BrowsingContext> parentBC = bc->GetParent();
    if (!parentBC || !parentBC->IsInProcess()) {
      // If parent document is not in process, then by construction it
      // cannot be same origin.
      return;
    }

    // Don't warn if our parent is not the top-level document.
    if (!parentBC->IsTopContent()) {
      return;
    }

    nsCOMPtr<nsIDocShell> parentDocShell = parentBC->GetDocShell();
    MOZ_ASSERT(parentDocShell);

    nsCOMPtr<nsIChannel> parentChannel;
    parentDocShell->GetCurrentDocumentChannel(getter_AddRefs(parentChannel));
    if (!parentChannel) {
      return;
    }
    nsresult rv = nsContentUtils::CheckSameOrigin(aChannel, parentChannel);
    if (NS_FAILED(rv)) {
      return;
    }

    nsCOMPtr<Document> parentDocument = parentDocShell->GetDocument();
    nsCOMPtr<nsIURI> iframeUri;
    parentChannel->GetURI(getter_AddRefs(iframeUri));
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "Iframe Sandbox"_ns, parentDocument,
        nsContentUtils::eSECURITY_PROPERTIES,
        "BothAllowScriptsAndSameOriginPresent", nsTArray<nsString>(),
        SourceLocation(iframeUri.get()));
  }
}

bool Document::IsSynthesized() {
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel ? mChannel->LoadInfo() : nullptr;
  return loadInfo && loadInfo->GetServiceWorkerTaintingSynthesized();
}

// static
bool Document::IsCallerChromeOrAddon(JSContext* aCx, JSObject* aObject) {
  nsIPrincipal* principal = nsContentUtils::SubjectPrincipal(aCx);
  return principal && (principal->IsSystemPrincipal() ||
                       principal->GetIsAddonOrExpandedAddonPrincipal());
}

static void CheckIsBadPolicy(nsILoadInfo::CrossOriginOpenerPolicy aPolicy,
                             BrowsingContext* aContext, nsIChannel* aChannel) {
#if defined(EARLY_BETA_OR_EARLIER)
  auto requireCORP =
      nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP;

  if (aContext->GetOpenerPolicy() == aPolicy ||
      (aContext->GetOpenerPolicy() != requireCORP && aPolicy != requireCORP)) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  bool hasURI = NS_SUCCEEDED(aChannel->GetOriginalURI(getter_AddRefs(uri)));

  bool isViewSource = hasURI && uri->SchemeIs("view-source");

  nsCString contentType;
  nsCOMPtr<nsIPropertyBag2> bag = do_QueryInterface(aChannel);
  bool isPDFJS = bag &&
                 NS_SUCCEEDED(bag->GetPropertyAsACString(u"contentType"_ns,
                                                         contentType)) &&
                 contentType.EqualsLiteral(APPLICATION_PDF);

  MOZ_DIAGNOSTIC_ASSERT(!isViewSource,
                        "Bug 1834864: Assert due to view-source.");
  MOZ_DIAGNOSTIC_ASSERT(!isPDFJS, "Bug 1834864: Assert due to  pdfjs.");
  MOZ_DIAGNOSTIC_ASSERT(aPolicy == requireCORP,
                        "Assert due to clearing REQUIRE_CORP.");
  MOZ_DIAGNOSTIC_ASSERT(aContext->GetOpenerPolicy() == requireCORP,
                        "Assert due to setting REQUIRE_CORP.");
#endif  // defined(EARLY_BETA_OR_EARLIER)
}

nsresult Document::StartDocumentLoad(const char* aCommand, nsIChannel* aChannel,
                                     nsILoadGroup* aLoadGroup,
                                     nsISupports* aContainer,
                                     nsIStreamListener** aDocListener,
                                     bool aReset) {
  if (MOZ_LOG_TEST(gDocumentLeakPRLog, LogLevel::Debug)) {
    nsCOMPtr<nsIURI> uri;
    aChannel->GetURI(getter_AddRefs(uri));
    MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug,
            ("DOCUMENT %p StartDocumentLoad %s", this,
             uri ? uri->GetSpecOrDefault().get() : ""));
  }

  MOZ_ASSERT(GetReadyStateEnum() == Document::READYSTATE_UNINITIALIZED,
             "Bad readyState");
  SetReadyStateInternal(READYSTATE_LOADING);

  if (nsCRT::strcmp(kLoadAsData, aCommand) == 0) {
    mLoadedAsData = true;
    SetLoadedAsData(true, /* aConsiderForMemoryReporting */ true);
    // We need to disable script & style loading in this case.
    // We leave them disabled even in EndLoad(), and let anyone
    // who puts the document on display to worry about enabling.

    // Do not load/process scripts when loading as data
    ScriptLoader()->SetEnabled(false);

    // styles
    CSSLoader()->SetEnabled(
        false);  // Do not load/process styles when loading as data
  } else if (nsCRT::strcmp("external-resource", aCommand) == 0) {
    // Allow CSS, but not scripts
    ScriptLoader()->SetEnabled(false);
  }

  mMayStartLayout = false;
  MOZ_ASSERT(!mReadyForIdle,
             "We should never hit DOMContentLoaded before this point");

  if (aReset) {
    Reset(aChannel, aLoadGroup);
  }

  nsAutoCString contentType;
  nsCOMPtr<nsIPropertyBag2> bag = do_QueryInterface(aChannel);
  if ((bag && NS_SUCCEEDED(bag->GetPropertyAsACString(u"contentType"_ns,
                                                      contentType))) ||
      NS_SUCCEEDED(aChannel->GetContentType(contentType))) {
    // XXX this is only necessary for viewsource:
    nsACString::const_iterator start, end, semicolon;
    contentType.BeginReading(start);
    contentType.EndReading(end);
    semicolon = start;
    FindCharInReadable(';', semicolon, end);
    SetContentType(Substring(start, semicolon));
  }

  RetrieveRelevantHeaders(aChannel);

  mChannel = aChannel;
  RecomputeResistFingerprinting();
  nsCOMPtr<nsIInputStreamChannel> inStrmChan = do_QueryInterface(mChannel);
  if (inStrmChan) {
    bool isSrcdocChannel;
    inStrmChan->GetIsSrcdocChannel(&isSrcdocChannel);
    if (isSrcdocChannel) {
      mIsSrcdocDocument = true;
    }
  }

  if (mChannel) {
    nsLoadFlags loadFlags;
    mChannel->GetLoadFlags(&loadFlags);
    bool isDocument = false;
    mChannel->GetIsDocument(&isDocument);
    if (loadFlags & nsIRequest::LOAD_DOCUMENT_NEEDS_COOKIE && isDocument &&
        IsSynthesized() && XRE_IsContentProcess()) {
      ContentChild::UpdateCookieStatus(mChannel);
    }

    // Store the security info for future use.
    mChannel->GetSecurityInfo(getter_AddRefs(mSecurityInfo));
  }

  // If this document is being loaded by a docshell, copy its sandbox flags
  // to the document, and store the fullscreen enabled flag. These are
  // immutable after being set here.
  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(aContainer);

  // If this is an error page, don't inherit sandbox flags
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  if (docShell && !loadInfo->GetLoadErrorPage()) {
    mSandboxFlags = loadInfo->GetSandboxFlags();
    WarnIfSandboxIneffective(docShell, mSandboxFlags, GetChannel());
  }

  // Set the opener policy for the top level content document.
  nsCOMPtr<nsIHttpChannelInternal> httpChan = do_QueryInterface(mChannel);
  nsILoadInfo::CrossOriginOpenerPolicy policy =
      nsILoadInfo::OPENER_POLICY_UNSAFE_NONE;
  if (IsTopLevelContentDocument() && httpChan &&
      NS_SUCCEEDED(httpChan->GetCrossOriginOpenerPolicy(&policy)) && docShell &&
      docShell->GetBrowsingContext()) {
    CheckIsBadPolicy(policy, docShell->GetBrowsingContext(), aChannel);

    // Setting the opener policy on a discarded context has no effect.
    Unused << docShell->GetBrowsingContext()->SetOpenerPolicy(policy);
  }

  // The CSP directives upgrade-insecure-requests as well as
  // block-all-mixed-content not only apply to the toplevel document,
  // but also to nested documents. The loadInfo of a subdocument
  // load already holds the correct flag, so let's just set it here
  // on the document. Please note that we set the appropriate preload
  // bits just for the sake of completeness here, because the preloader
  // does not reach into subdocuments.
  mUpgradeInsecureRequests = loadInfo->GetUpgradeInsecureRequests();
  mUpgradeInsecurePreloads = mUpgradeInsecureRequests;
  mBlockAllMixedContent = loadInfo->GetBlockAllMixedContent();
  mBlockAllMixedContentPreloads = mBlockAllMixedContent;

  // HTTPS-Only Mode flags
  // The HTTPS_ONLY_EXEMPT flag of the HTTPS-Only state gets propagated to all
  // sub-resources and sub-documents.
  mHttpsOnlyStatus = loadInfo->GetHttpsOnlyStatus();

  nsresult rv = InitReferrerInfo(aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = InitCOEP(aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  // HACK: Calling EnsureIPCPoliciesRead() here will parse the CSP using the
  // context's current mSelfURI (which is still the previous mSelfURI),
  // bypassing some internal bugs with 'self' and iframe inheritance.
  // Not calling it here results in the mSelfURI being the current mSelfURI and
  // not the previous which breaks said inheritance.
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1793560#ch-8
  nsCOMPtr<nsIContentSecurityPolicy> cspToInherit = loadInfo->GetCspToInherit();
  if (cspToInherit) {
    cspToInherit->EnsureIPCPoliciesRead();
  }

  rv = InitCSP(aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = InitDocPolicy(aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  // Initialize FeaturePolicy
  rv = InitFeaturePolicy(aChannel);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = loadInfo->GetCookieJarSettings(getter_AddRefs(mCookieJarSettings));
  NS_ENSURE_SUCCESS(rv, rv);

  MaybeRecomputePartitionKey();

  // Generally XFO and CSP frame-ancestors is handled within
  // DocumentLoadListener. However, the DocumentLoadListener can not handle
  // object and embed. Until then we have to enforce it here (See Bug 1646899).
  nsContentPolicyType internalContentType =
      loadInfo->InternalContentPolicyType();
  if (internalContentType == nsIContentPolicy::TYPE_INTERNAL_OBJECT ||
      internalContentType == nsIContentPolicy::TYPE_INTERNAL_EMBED) {
    nsContentSecurityUtils::PerformCSPFrameAncestorAndXFOCheck(aChannel);

    nsresult status;
    aChannel->GetStatus(&status);
    if (status == NS_ERROR_XFO_VIOLATION) {
      // stop!  ERROR page!
      // But before we have to reset the principal of the document
      // because the onload() event fires before the error page
      // is displayed and we do not want the enclosing document
      // to access the contentDocument.
      RefPtr<NullPrincipal> nullPrincipal =
          NullPrincipal::CreateWithInheritedAttributes(NodePrincipal());
      // Before calling SetPrincipals() we should ensure that mFontFaceSet
      // and also GetInnerWindow() is still null at this point, before
      // we can fix Bug 1614735: Evaluate calls to SetPrincipal
      // within Document.cpp
      MOZ_ASSERT(!mFontFaceSet && !GetInnerWindow());
      SetPrincipals(nullPrincipal, nullPrincipal);
    }
  }

  return NS_OK;
}

void Document::SetLoadedAsData(bool aLoadedAsData,
                               bool aConsiderForMemoryReporting) {
  mLoadedAsData = aLoadedAsData;
  if (aConsiderForMemoryReporting) {
    nsIGlobalObject* global = GetScopeObject();
    if (global) {
      if (nsPIDOMWindowInner* window = global->GetAsInnerWindow()) {
        nsGlobalWindowInner::Cast(window)
            ->RegisterDataDocumentForMemoryReporting(this);
      }
    }
  }
}

nsIContentSecurityPolicy* Document::GetCsp() const { return mCSP; }

void Document::SetCsp(nsIContentSecurityPolicy* aCSP) {
  mCSP = aCSP;
  mHasPolicyWithRequireTrustedTypesForDirective =
      aCSP && aCSP->GetRequireTrustedTypesForDirectiveState() !=
                  RequireTrustedTypesForDirectiveState::NONE;
}

nsIContentSecurityPolicy* Document::GetPreloadCsp() const {
  return mPreloadCSP;
}

void Document::SetPreloadCsp(nsIContentSecurityPolicy* aPreloadCSP) {
  mPreloadCSP = aPreloadCSP;
}

void Document::GetCspJSON(nsString& aJSON) {
  aJSON.Truncate();

  if (!mCSP) {
    dom::CSPPolicies jsonPolicies;
    jsonPolicies.ToJSON(aJSON);
    return;
  }
  mCSP->ToJSON(aJSON);
}

void Document::SendToConsole(nsCOMArray<nsISecurityConsoleMessage>& aMessages) {
  for (uint32_t i = 0; i < aMessages.Length(); ++i) {
    nsAutoString messageTag;
    aMessages[i]->GetTag(messageTag);

    nsAutoString category;
    aMessages[i]->GetCategory(category);

    nsContentUtils::ReportToConsole(nsIScriptError::warningFlag,
                                    NS_ConvertUTF16toUTF8(category), this,
                                    nsContentUtils::eSECURITY_PROPERTIES,
                                    NS_ConvertUTF16toUTF8(messageTag).get());
  }
}

void Document::ApplySettingsFromCSP(bool aSpeculative) {
  nsresult rv = NS_OK;
  if (!aSpeculative) {
    // 1) apply settings from regular CSP
    if (mCSP) {
      // Set up 'block-all-mixed-content' if not already inherited
      // from the parent context or set by any other CSP.
      if (!mBlockAllMixedContent) {
        bool block = false;
        rv = mCSP->GetBlockAllMixedContent(&block);
        NS_ENSURE_SUCCESS_VOID(rv);
        mBlockAllMixedContent = block;
      }
      if (!mBlockAllMixedContentPreloads) {
        mBlockAllMixedContentPreloads = mBlockAllMixedContent;
      }

      // Set up 'upgrade-insecure-requests' if not already inherited
      // from the parent context or set by any other CSP.
      if (!mUpgradeInsecureRequests) {
        bool upgrade = false;
        rv = mCSP->GetUpgradeInsecureRequests(&upgrade);
        NS_ENSURE_SUCCESS_VOID(rv);
        mUpgradeInsecureRequests = upgrade;
      }
      if (!mUpgradeInsecurePreloads) {
        mUpgradeInsecurePreloads = mUpgradeInsecureRequests;
      }
      // Update csp settings in the parent process
      if (auto* wgc = GetWindowGlobalChild()) {
        wgc->SendUpdateDocumentCspSettings(mBlockAllMixedContent,
                                           mUpgradeInsecureRequests);
      }
    }
    return;
  }

  // 2) apply settings from speculative csp
  if (mPreloadCSP) {
    if (!mBlockAllMixedContentPreloads) {
      bool block = false;
      rv = mPreloadCSP->GetBlockAllMixedContent(&block);
      NS_ENSURE_SUCCESS_VOID(rv);
      mBlockAllMixedContent = block;
    }
    if (!mUpgradeInsecurePreloads) {
      bool upgrade = false;
      rv = mPreloadCSP->GetUpgradeInsecureRequests(&upgrade);
      NS_ENSURE_SUCCESS_VOID(rv);
      mUpgradeInsecurePreloads = upgrade;
    }
  }
}

nsresult Document::InitCSP(nsIChannel* aChannel) {
  MOZ_ASSERT(!mScriptGlobalObject,
             "CSP must be initialized before mScriptGlobalObject is set!");

  // If this is a data document - no need to set CSP.
  if (mLoadedAsData) {
    return NS_OK;
  }

  // If this is an image, no need to set a CSP. Otherwise SVG images
  // served with a CSP might block internally applied inline styles.
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  if (loadInfo->GetExternalContentPolicyType() ==
          ExtContentPolicy::TYPE_IMAGE ||
      loadInfo->GetExternalContentPolicyType() ==
          ExtContentPolicy::TYPE_IMAGESET) {
    return NS_OK;
  }

  MOZ_ASSERT(!mCSP, "where did mCSP get set if not here?");

  // If there is a CSP that needs to be inherited from whatever
  // global is considered the client of the document fetch then
  // we query it here from the loadinfo in case the newly created
  // document needs to inherit the CSP. See:
  // https://w3c.github.io/webappsec-csp/#initialize-document-csp
  bool inheritedCSP = CSP_ShouldResponseInheritCSP(aChannel);
  if (inheritedCSP) {
    mCSP = loadInfo->GetCspToInherit();
  }

  // If there is no CSP to inherit, then we create a new CSP here so
  // that history entries always have the right reference in case a
  // Meta CSP gets dynamically added after the history entry has
  // already been created.
  if (!mCSP) {
    mCSP = new nsCSPContext();
    mHasPolicyWithRequireTrustedTypesForDirective = false;
  } else {
    mHasPolicyWithRequireTrustedTypesForDirective =
        mCSP->GetRequireTrustedTypesForDirectiveState() !=
        RequireTrustedTypesForDirectiveState::NONE;
  }

  // Always overwrite the requesting context of the CSP so that any new
  // 'self' keyword added to an inherited CSP translates correctly.
  nsresult rv = mCSP->SetRequestContextWithDocument(this);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsAutoCString tCspHeaderValue, tCspROHeaderValue;

  nsCOMPtr<nsIHttpChannel> httpChannel;
  rv = GetHttpChannelHelper(aChannel, getter_AddRefs(httpChannel));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (httpChannel) {
    Unused << httpChannel->GetResponseHeader("content-security-policy"_ns,
                                             tCspHeaderValue);

    Unused << httpChannel->GetResponseHeader(
        "content-security-policy-report-only"_ns, tCspROHeaderValue);
  }
  NS_ConvertASCIItoUTF16 cspHeaderValue(tCspHeaderValue);
  NS_ConvertASCIItoUTF16 cspROHeaderValue(tCspROHeaderValue);

  // Check if this is a document from a WebExtension.
  nsCOMPtr<nsIPrincipal> principal = NodePrincipal();
  auto addonPolicy = BasePrincipal::Cast(principal)->AddonPolicy();

  // If there's no CSP to apply, go ahead and return early
  if (!inheritedCSP && !addonPolicy && cspHeaderValue.IsEmpty() &&
      cspROHeaderValue.IsEmpty()) {
    if (MOZ_LOG_TEST(gCspPRLog, LogLevel::Debug)) {
      nsCOMPtr<nsIURI> chanURI;
      aChannel->GetURI(getter_AddRefs(chanURI));
      nsAutoCString aspec;
      chanURI->GetAsciiSpec(aspec);
      MOZ_LOG(gCspPRLog, LogLevel::Debug,
              ("no CSP for document, %s", aspec.get()));
    }

    return NS_OK;
  }

  MOZ_LOG(gCspPRLog, LogLevel::Debug,
          ("Document is an add-on or CSP header specified %p", this));

  // ----- if the doc is an addon, apply its CSP.
  if (addonPolicy) {
    mCSP->AppendPolicy(addonPolicy->BaseCSP(), false, false);

    mCSP->AppendPolicy(addonPolicy->ExtensionPageCSP(), false, false);
    // Bug 1548468: Move CSP off ExpandedPrincipal
    // Currently the LoadInfo holds the source of truth for every resource load
    // because LoadInfo::GetCsp() queries the CSP from an ExpandedPrincipal
    // (and not from the Client) if the load was triggered by an extension.
    auto* basePrin = BasePrincipal::Cast(principal);
    if (basePrin->Is<ExpandedPrincipal>()) {
      basePrin->As<ExpandedPrincipal>()->SetCsp(mCSP);
    }
  }

  // ----- if there's a full-strength CSP header, apply it.
  if (!cspHeaderValue.IsEmpty()) {
    mHasCSPDeliveredThroughHeader = true;
    rv = CSP_AppendCSPFromHeader(mCSP, cspHeaderValue, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // ----- if there's a report-only CSP header, apply it.
  if (!cspROHeaderValue.IsEmpty()) {
    rv = CSP_AppendCSPFromHeader(mCSP, cspROHeaderValue, true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // ----- Enforce sandbox policy if supplied in CSP header
  // The document may already have some sandbox flags set (e.g. if the document
  // is an iframe with the sandbox attribute set). If we have a CSP sandbox
  // directive, intersect the CSP sandbox flags with the existing flags. This
  // corresponds to the _least_ permissive policy.
  uint32_t cspSandboxFlags = SANDBOXED_NONE;
  rv = mCSP->GetCSPSandboxFlags(&cspSandboxFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  // Probably the iframe sandbox attribute already caused the creation of a
  // new NullPrincipal. Only create a new NullPrincipal if CSP requires so
  // and no one has been created yet.
  bool needNewNullPrincipal = (cspSandboxFlags & SANDBOXED_ORIGIN) &&
                              !(mSandboxFlags & SANDBOXED_ORIGIN);

  mSandboxFlags |= cspSandboxFlags;

  if (needNewNullPrincipal) {
    principal = NullPrincipal::CreateWithInheritedAttributes(principal);
    // Skip setting the content blocking allowlist principal to NullPrincipal.
    // The principal is only used to enable/disable trackingprotection via
    // permission and can be shared with the top level sandboxed site.
    // See Bug 1654546.
    SetPrincipals(principal, principal);
  }

  ApplySettingsFromCSP(false);
  return NS_OK;
}

static FeaturePolicy* GetFeaturePolicyFromElement(Element* aElement) {
  if (auto* iframe = HTMLIFrameElement::FromNodeOrNull(aElement)) {
    return iframe->FeaturePolicy();
  }

  if (!HTMLObjectElement::FromNodeOrNull(aElement) &&
      !HTMLEmbedElement::FromNodeOrNull(aElement)) {
    return nullptr;
  }

  return aElement->OwnerDoc()->FeaturePolicy();
}

nsresult Document::InitDocPolicy(nsIChannel* aChannel) {
  // We only use document policy to implement the text fragments spec, so leave
  // everything at the default value if it isn't enabled. This includes the
  // behavior for element fragments.
  if (!StaticPrefs::dom_text_fragments_enabled()) {
    return NS_OK;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel;
  nsresult rv = GetHttpChannelHelper(aChannel, getter_AddRefs(httpChannel));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsAutoCString docPolicyString;
  if (httpChannel) {
    Unused << httpChannel->GetResponseHeader("Document-Policy"_ns,
                                             docPolicyString);
  }

  if (docPolicyString.IsEmpty()) {
    return NS_OK;
  }

  mForceLoadAtTop = NS_GetForceLoadAtTopFromHeader(docPolicyString);

  return NS_OK;
}

void Document::InitFeaturePolicy(
    const Variant<Nothing, FeaturePolicyInfo, Element*>&
        aContainerFeaturePolicy) {
  MOZ_ASSERT(mFeaturePolicy, "we should have FeaturePolicy created");

  mFeaturePolicy->ResetDeclaredPolicy();

  mFeaturePolicy->SetDefaultOrigin(NodePrincipal());

  RefPtr<dom::FeaturePolicy> featurePolicy = mFeaturePolicy;
  aContainerFeaturePolicy.match(
      [](const Nothing&) {},
      [featurePolicy](const FeaturePolicyInfo& aContainerFeaturePolicy) {
        // Let's inherit the policy from the possibly cross-origin container.
        featurePolicy->InheritPolicy(aContainerFeaturePolicy);
        featurePolicy->SetSrcOrigin(aContainerFeaturePolicy.mSrcOrigin);
      },
      [featurePolicy](Element* aContainer) {
        // Let's inherit the policy from the parent container element if it
        // exists.
        if (RefPtr<dom::FeaturePolicy> containerFeaturePolicy =
                GetFeaturePolicyFromElement(aContainer)) {
          featurePolicy->InheritPolicy(containerFeaturePolicy);
          featurePolicy->SetSrcOrigin(containerFeaturePolicy->GetSrcOrigin());
        }
      });
}

Element* GetEmbedderElementFrom(BrowsingContext* aBrowsingContext) {
  if (!aBrowsingContext) {
    return nullptr;
  }
  if (!aBrowsingContext->IsContentSubframe()) {
    return nullptr;
  }

  return aBrowsingContext->GetEmbedderElement();
}

nsresult Document::InitFeaturePolicy(nsIChannel* aChannel) {
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  if (Element* embedderElement = GetEmbedderElementFrom(GetBrowsingContext())) {
    InitFeaturePolicy(AsVariant(embedderElement));
  } else if (Maybe<FeaturePolicyInfo> featurePolicyContainer =
                 loadInfo->GetContainerFeaturePolicyInfo()) {
    InitFeaturePolicy(AsVariant(*featurePolicyContainer));
  } else {
    InitFeaturePolicy(AsVariant(Nothing{}));
  }

  // We don't want to parse the http Feature-Policy header if this pref is off.
  if (!StaticPrefs::dom_security_featurePolicy_header_enabled()) {
    return NS_OK;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel;
  nsresult rv = GetHttpChannelHelper(aChannel, getter_AddRefs(httpChannel));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!httpChannel) {
    return NS_OK;
  }

  // query the policy from the header
  nsAutoCString value;
  rv = httpChannel->GetResponseHeader("Feature-Policy"_ns, value);
  if (NS_SUCCEEDED(rv)) {
    mFeaturePolicy->SetDeclaredPolicy(this, NS_ConvertUTF8toUTF16(value),
                                      NodePrincipal(), nullptr);
  }

  return NS_OK;
}

void Document::EnsureNotEnteringAndExitFullscreen() {
  Document::ClearPendingFullscreenRequests(this);
  if (GetFullscreenElement()) {
    Document::AsyncExitFullscreen(this);
  }
}

void Document::SetReferrerInfo(nsIReferrerInfo* aReferrerInfo) {
  mReferrerInfo = aReferrerInfo;
  mCachedReferrerInfoForInternalCSSAndSVGResources = nullptr;
  mCachedURLData = nullptr;
}

nsresult Document::InitReferrerInfo(nsIChannel* aChannel) {
  MOZ_ASSERT(mReferrerInfo);
  MOZ_ASSERT(mPreloadReferrerInfo);

  if (ReferrerInfo::ShouldResponseInheritReferrerInfo(aChannel)) {
    // The channel is loading `about:srcdoc`. Srcdoc loads should respond with
    // their parent's ReferrerInfo when asked for their ReferrerInfo, unless
    // they have an opaque origin.
    // https://w3c.github.io/webappsec-referrer-policy/#determine-requests-referrer
    if (BrowsingContext* bc = GetBrowsingContext()) {
      // At this point the document is not fully created and mParentDocument has
      // not been set yet,
      Document* parentDoc = bc->GetEmbedderElement()
                                ? bc->GetEmbedderElement()->OwnerDoc()
                                : nullptr;
      if (parentDoc) {
        SetReferrerInfo(parentDoc->GetReferrerInfo());
        mPreloadReferrerInfo = mReferrerInfo;
        return NS_OK;
      }

      MOZ_ASSERT(bc->IsInProcess() || NodePrincipal()->GetIsNullPrincipal(),
                 "srcdoc without null principal as toplevel!");
    }
  }

  nsCOMPtr<nsIHttpChannel> httpChannel;
  nsresult rv = GetHttpChannelHelper(aChannel, getter_AddRefs(httpChannel));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!httpChannel) {
    return NS_OK;
  }

  if (nsCOMPtr<nsIReferrerInfo> referrerInfo = httpChannel->GetReferrerInfo()) {
    SetReferrerInfo(referrerInfo);
  }

  // Override policy if we get one from Referrerr-Policy header
  mozilla::dom::ReferrerPolicy policy =
      nsContentUtils::GetReferrerPolicyFromChannel(aChannel);
  nsCOMPtr<nsIReferrerInfo> clone =
      static_cast<dom::ReferrerInfo*>(mReferrerInfo.get())
          ->CloneWithNewPolicy(policy);
  SetReferrerInfo(clone);
  mPreloadReferrerInfo = mReferrerInfo;
  return NS_OK;
}

nsresult Document::InitCOEP(nsIChannel* aChannel) {
  nsCOMPtr<nsIHttpChannel> httpChannel;
  nsresult rv = GetHttpChannelHelper(aChannel, getter_AddRefs(httpChannel));
  if (NS_FAILED(rv)) {
    return NS_OK;
  }

  nsCOMPtr<nsIHttpChannelInternal> intChannel = do_QueryInterface(httpChannel);

  if (!intChannel) {
    return NS_OK;
  }

  nsILoadInfo::CrossOriginEmbedderPolicy policy =
      nsILoadInfo::EMBEDDER_POLICY_NULL;
  if (NS_SUCCEEDED(intChannel->GetResponseEmbedderPolicy(
          mTrials.IsEnabled(OriginTrial::CoepCredentialless), &policy))) {
    mEmbedderPolicy = Some(policy);
  }

  return NS_OK;
}

void Document::StopDocumentLoad() {
  if (mParser) {
    mParserAborted = true;
    mParser->Terminate();
  }
}

void Document::SetDocumentURI(nsIURI* aURI) {
  nsCOMPtr<nsIURI> oldBase = GetDocBaseURI();
  mDocumentURI = aURI;
  // This loosely implements §3.4.1 of Text Fragments
  // https://wicg.github.io/scroll-to-text-fragment/#invoking-text-directives
  // Unlike specified in the spec, the fragment directive is not stripped from
  // the URL in the session history entry. Instead it is removed when the URL is
  // set in the `Document`. Also, instead of storing the `uninvokedDirective` in
  // `Document` as mentioned in the spec, the extracted directives are moved to
  // the `FragmentDirective` object which deals with finding the ranges to
  // highlight in `ScrollToRef()`.
  // XXX(:jjaschke): This is only a temporary solution.
  // https://bugzil.la/1881429 is filed for revisiting this.
  nsTArray<TextDirective> textDirectives;
  FragmentDirective::ParseAndRemoveFragmentDirectiveFromFragment(
      mDocumentURI, &textDirectives);
  if (!textDirectives.IsEmpty()) {
    SetUseCounter(eUseCounter_custom_TextDirectivePages);
  }
  FragmentDirective()->SetTextDirectives(std::move(textDirectives));

  nsIURI* newBase = GetDocBaseURI();

  mChromeRulesEnabled = URLExtraData::ChromeRulesEnabled(aURI);

  bool equalBases = false;
  // Changing just the ref of a URI does not change how relative URIs would
  // resolve wrt to it, so we can treat the bases as equal as long as they're
  // equal ignoring the ref.
  if (oldBase && newBase) {
    oldBase->EqualsExceptRef(newBase, &equalBases);
  } else {
    equalBases = !oldBase && !newBase;
  }

  // If this is the first time we're setting the document's URI, set the
  // document's original URI.
  if (!mOriginalURI) mOriginalURI = mDocumentURI;

  // If changing the document's URI changed the base URI of the document, we
  // need to refresh the hrefs of all the links on the page.
  if (!equalBases) {
    mCachedURLData = nullptr;
    RefreshLinkHrefs();
  }

  // Recalculate our base domain
  mBaseDomain.Truncate();
  ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
  if (thirdPartyUtil) {
    Unused << thirdPartyUtil->GetBaseDomain(mDocumentURI, mBaseDomain);
  }

  // Tell our WindowGlobalParent that the document's URI has been changed.
  if (WindowGlobalChild* wgc = GetWindowGlobalChild()) {
    wgc->SetDocumentURI(mDocumentURI);
  }
}

static void GetFormattedTimeString(PRTime aTime, bool aUniversal,
                                   nsAString& aFormattedTimeString) {
  PRExplodedTime prtime;
  PR_ExplodeTime(aTime, aUniversal ? PR_GMTParameters : PR_LocalTimeParameters,
                 &prtime);
  // "MM/DD/YYYY hh:mm:ss"
  char formatedTime[24];
  if (SprintfLiteral(formatedTime, "%02d/%02d/%04d %02d:%02d:%02d",
                     prtime.tm_month + 1, prtime.tm_mday, int(prtime.tm_year),
                     prtime.tm_hour, prtime.tm_min, prtime.tm_sec)) {
    CopyASCIItoUTF16(nsDependentCString(formatedTime), aFormattedTimeString);
  } else {
    // If we for whatever reason failed to find the last modified time
    // (or even the current time), fall back to what NS4.x returned.
    aFormattedTimeString.AssignLiteral(u"01/01/1970 00:00:00");
  }
}

void Document::GetLastModified(nsAString& aLastModified) const {
  if (!mLastModified.IsEmpty()) {
    aLastModified.Assign(mLastModified);
  } else {
    GetFormattedTimeString(PR_Now(),
                           ShouldResistFingerprinting(RFPTarget::JSDateTimeUTC),
                           aLastModified);
  }
}

static void IncrementExpandoGeneration(Document& aDoc) {
  ++aDoc.mExpandoAndGeneration.generation;
}

void Document::AddToNameTable(Element* aElement, nsAtom* aName) {
  MOZ_ASSERT(nsGenericHTMLElement::ShouldExposeNameAsWindowProperty(aElement),
             "Only put elements that need to be exposed as window['name'] in "
             "the named table.");

  IdentifierMapEntry* entry = mIdentifierMap.PutEntry(aName);

  // Null for out-of-memory
  if (entry) {
    if (!entry->HasNameElement() &&
        !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
      IncrementExpandoGeneration(*this);
    }
    entry->AddNameElement(this, aElement);
  }
}

void Document::RemoveFromNameTable(Element* aElement, nsAtom* aName) {
  // Speed up document teardown
  if (mIdentifierMap.Count() == 0) return;

  IdentifierMapEntry* entry = mIdentifierMap.GetEntry(aName);
  if (!entry)  // Could be false if the element was anonymous, hence never added
    return;

  entry->RemoveNameElement(aElement);
  if (!entry->HasNameElement() &&
      !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
    IncrementExpandoGeneration(*this);
  }
}

void Document::AddToDocumentNameTable(nsGenericHTMLElement* aElement,
                                      nsAtom* aName) {
  MOZ_ASSERT(
      nsGenericHTMLElement::ShouldExposeIdAsHTMLDocumentProperty(aElement) ||
          nsGenericHTMLElement::ShouldExposeNameAsHTMLDocumentProperty(
              aElement),
      "Only put elements that need to be exposed as document['name'] in "
      "the document named table.");

  if (IdentifierMapEntry* entry = mIdentifierMap.PutEntry(aName)) {
    entry->AddDocumentNameElement(this, aElement);
  }
}

void Document::RemoveFromDocumentNameTable(nsGenericHTMLElement* aElement,
                                           nsAtom* aName) {
  if (mIdentifierMap.Count() == 0) {
    return;
  }

  if (IdentifierMapEntry* entry = mIdentifierMap.GetEntry(aName)) {
    entry->RemoveDocumentNameElement(aElement);
    nsBaseContentList* list = entry->GetDocumentNameContentList();
    if (!list || list->Length() == 0) {
      IncrementExpandoGeneration(*this);
    }
  }
}

void Document::AddToIdTable(Element* aElement, nsAtom* aId) {
  IdentifierMapEntry* entry = mIdentifierMap.PutEntry(aId);

  if (entry) { /* True except on OOM */
    if (nsGenericHTMLElement::ShouldExposeIdAsHTMLDocumentProperty(aElement) &&
        !entry->HasNameElement() &&
        !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
      IncrementExpandoGeneration(*this);
    }
    entry->AddIdElement(aElement);
  }
}

void Document::RemoveFromIdTable(Element* aElement, nsAtom* aId) {
  NS_ASSERTION(aId, "huhwhatnow?");

  // Speed up document teardown
  if (mIdentifierMap.Count() == 0) {
    return;
  }

  IdentifierMapEntry* entry = mIdentifierMap.GetEntry(aId);
  if (!entry)  // Can be null for XML elements with changing ids.
    return;

  entry->RemoveIdElement(aElement);
  if (nsGenericHTMLElement::ShouldExposeIdAsHTMLDocumentProperty(aElement) &&
      !entry->HasNameElement() &&
      !entry->HasIdElementExposedAsHTMLDocumentProperty()) {
    IncrementExpandoGeneration(*this);
  }
  if (entry->IsEmpty()) {
    mIdentifierMap.RemoveEntry(entry);
  }
}

void Document::UpdateReferrerInfoFromMeta(const nsAString& aMetaReferrer,
                                          bool aPreload) {
  ReferrerPolicyEnum policy =
      ReferrerInfo::ReferrerPolicyFromMetaString(aMetaReferrer);
  // The empty string "" corresponds to no referrer policy, causing a fallback
  // to a referrer policy defined elsewhere.
  if (policy == ReferrerPolicy::_empty) {
    return;
  }

  MOZ_ASSERT(mReferrerInfo);
  MOZ_ASSERT(mPreloadReferrerInfo);

  if (aPreload) {
    mPreloadReferrerInfo =
        static_cast<mozilla::dom::ReferrerInfo*>((mPreloadReferrerInfo).get())
            ->CloneWithNewPolicy(policy);
  } else {
    nsCOMPtr<nsIReferrerInfo> clone =
        static_cast<mozilla::dom::ReferrerInfo*>((mReferrerInfo).get())
            ->CloneWithNewPolicy(policy);
    SetReferrerInfo(clone);
  }
}

void Document::SetPrincipals(nsIPrincipal* aNewPrincipal,
                             nsIPrincipal* aNewPartitionedPrincipal) {
  MOZ_ASSERT(!!aNewPrincipal == !!aNewPartitionedPrincipal);
  if (aNewPrincipal && mAllowDNSPrefetch &&
      StaticPrefs::network_dns_disablePrefetchFromHTTPS()) {
    if (aNewPrincipal->SchemeIs("https")) {
      mAllowDNSPrefetch = false;
    }
  }

  mScriptLoader->DeregisterFromCache();
  mCSSLoader->DeregisterFromSheetCache();

  mNodeInfoManager->SetDocumentPrincipal(aNewPrincipal);
  mPartitionedPrincipal = aNewPartitionedPrincipal;

  mCachedURLData = nullptr;

  mCSSLoader->RegisterInSheetCache();
  mScriptLoader->RegisterToCache();

  RecomputeResistFingerprinting();

#ifdef DEBUG
  // Validate that the docgroup is set correctly.
  //
  // If we're setting the principal to null, we don't want to perform the check,
  // as the document is entering an intermediate state where it does not have a
  // principal. It will be given another real principal shortly which we will
  // check. It's not unsafe to have a document which has a null principal in the
  // same docgroup as another document, so this should not be a problem.
  if (aNewPrincipal) {
    AssertDocGroupMatchesKey();
  }
#endif
}

#ifdef DEBUG
void Document::AssertDocGroupMatchesKey() const {
  // Sanity check that we have an up-to-date and accurate docgroup
  // We only check if the principal when we can get the browsing context, as
  // documents without a BrowsingContext do not need to have a matching
  // principal to their DocGroup.

  // Note that we can be invoked during cycle collection, so we need to handle
  // the browsingcontext being partially unlinked - normally you shouldn't
  // null-check `Group()` as it shouldn't return nullptr.
  if (!GetBrowsingContext() || !GetBrowsingContext()->Group()) {
    return;
  }

  if (mDocGroup && mDocGroup->GetBrowsingContextGroup()) {
    MOZ_ASSERT(mDocGroup->GetBrowsingContextGroup() ==
               GetBrowsingContext()->Group());
    mDocGroup->AssertMatches(this);
  }
}
#endif

nsresult Document::Dispatch(already_AddRefed<nsIRunnable>&& aRunnable) const {
  return SchedulerGroup::Dispatch(std::move(aRunnable));
}

void Document::NoteScriptTrackingStatus(const nsACString& aURL,
                                        bool aIsTracking) {
  if (aIsTracking) {
    mTrackingScripts.Insert(aURL);
  }
  // Ideally, whether a given script is tracking or not should be consistent,
  // but there is a race so that it is not, when loading real sites in debug
  // builds. See bug 1925286.
  // MOZ_ASSERT_IF(!aIsTracking, !mTrackingScripts.Contains(aURL));
}

bool Document::IsScriptTracking(JSContext* aCx) const {
  JS::AutoFilename filename;
  if (!JS::DescribeScriptedCaller(&filename, aCx)) {
    return false;
  }
  return mTrackingScripts.Contains(nsDependentCString(filename.get()));
}

void Document::GetContentType(nsAString& aContentType) {
  CopyUTF8toUTF16(GetContentTypeInternal(), aContentType);
}

void Document::SetContentType(const nsACString& aContentType) {
  if (!IsHTMLOrXHTML() && mDefaultElementType == kNameSpaceID_None &&
      aContentType.EqualsLiteral("application/xhtml+xml")) {
    mDefaultElementType = kNameSpaceID_XHTML;
  }

  mCachedEncoder = nullptr;
  mContentType = aContentType;
}

bool Document::HasPendingInitialTranslation() {
  return mDocumentL10n && mDocumentL10n->GetState() != DocumentL10nState::Ready;
}

bool Document::HasPendingL10nMutations() const {
  return mDocumentL10n && mDocumentL10n->HasPendingMutations();
}

bool Document::DocumentSupportsL10n(JSContext* aCx, JSObject* aObject) {
  JS::Rooted<JSObject*> object(aCx, aObject);
  nsCOMPtr<nsIPrincipal> callerPrincipal =
      nsContentUtils::SubjectPrincipal(aCx);
  nsGlobalWindowInner* win = xpc::WindowOrNull(object);
  bool allowed = false;
  callerPrincipal->IsL10nAllowed(win ? win->GetDocumentURI() : nullptr,
                                 &allowed);
  return allowed;
}

void Document::LocalizationLinkAdded(Element* aLinkElement) {
  if (!AllowsL10n()) {
    return;
  }

  nsAutoString href;
  aLinkElement->GetAttr(nsGkAtoms::href, href);

  if (!mDocumentL10n) {
    Element* elem = GetDocumentElement();
    MOZ_DIAGNOSTIC_ASSERT(elem);

    bool isSync = elem->HasAttr(nsGkAtoms::datal10nsync);
    mDocumentL10n = DocumentL10n::Create(this, isSync);
    if (NS_WARN_IF(!mDocumentL10n)) {
      return;
    }
  }

  mDocumentL10n->AddResourceId(NS_ConvertUTF16toUTF8(href));

  if (mReadyState >= READYSTATE_INTERACTIVE) {
    nsContentUtils::AddScriptRunner(NewRunnableMethod(
        "DocumentL10n::TriggerInitialTranslation()", mDocumentL10n,
        &DocumentL10n::TriggerInitialTranslation));
  } else {
    if (!mDocumentL10n->mBlockingLayout) {
      // Our initial translation is going to block layout start.  Make sure
      // we don't fire the load event until after that stops happening and
      // layout has a chance to start.
      BlockOnload();
      mDocumentL10n->mBlockingLayout = true;
    }
  }
}

void Document::LocalizationLinkRemoved(Element* aLinkElement) {
  if (!AllowsL10n()) {
    return;
  }

  if (mDocumentL10n) {
    nsAutoString href;
    aLinkElement->GetAttr(nsGkAtoms::href, href);
    uint32_t remaining =
        mDocumentL10n->RemoveResourceId(NS_ConvertUTF16toUTF8(href));
    if (remaining == 0) {
      if (mDocumentL10n->mBlockingLayout) {
        mDocumentL10n->mBlockingLayout = false;
        UnblockOnload(/* aFireSync = */ false);
      }
      mDocumentL10n = nullptr;
    }
  }
}

/**
 * This method should be called once the end of the l10n
 * resource container has been parsed.
 *
 * In XUL this is the end of the first </linkset>,
 * In XHTML/HTML this is the end of </head>.
 *
 * This milestone is used to allow for batch
 * localization context I/O and building done
 * once when all resources in the document have been
 * collected.
 */
void Document::OnL10nResourceContainerParsed() {
  // XXX: This is a scaffolding for where we might inject prefetch
  // in bug 1717241.
}

void Document::OnParsingCompleted() {
  // Let's call it again, in case the resource
  // container has not been closed, and only
  // now we're closing the document.
  OnL10nResourceContainerParsed();

  if (mDocumentL10n) {
    RefPtr<DocumentL10n> l10n = mDocumentL10n;
    l10n->TriggerInitialTranslation();
  }
}

void Document::InitialTranslationCompleted(bool aL10nCached) {
  if (mDocumentL10n && mDocumentL10n->mBlockingLayout) {
    // This means we blocked the load event in LocalizationLinkAdded.  It's
    // important that the load blocker removal here be async, because our caller
    // will notify the content sink after us, and we want the content sync's
    // work to happen before the load event fires.
    mDocumentL10n->mBlockingLayout = false;
    UnblockOnload(/* aFireSync = */ false);
  }

  mL10nProtoElements.Clear();

  nsXULPrototypeDocument* proto = GetPrototype();
  if (proto) {
    proto->SetIsL10nCached(aL10nCached);
  }
}

bool Document::AllowsL10n() const {
  if (IsStaticDocument()) {
    // We don't allow l10n on static documents, because the nodes are already
    // cloned translated, and static docs don't get parsed so we never
    // TriggerInitialTranslation, etc, so a load blocker would keep hanging
    // forever.
    return false;
  }
  bool allowed = false;
  NodePrincipal()->IsL10nAllowed(GetDocumentURI(), &allowed);
  return allowed;
}

DocumentTimeline* Document::Timeline() {
  if (!mDocumentTimeline) {
    mDocumentTimeline = new DocumentTimeline(this, TimeDuration(0));
  }

  return mDocumentTimeline;
}

SVGSVGElement* Document::GetSVGRootElement() const {
  Element* root = GetRootElement();
  if (!root || !root->IsSVGElement(nsGkAtoms::svg)) {
    return nullptr;
  }
  return static_cast<SVGSVGElement*>(root);
}

/* Return true if the document is in the focused top-level window, and is an
 * ancestor of the focused DOMWindow. */
bool Document::HasFocus(ErrorResult& rv) const {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    rv.Throw(NS_ERROR_NOT_AVAILABLE);
    return false;
  }

  BrowsingContext* bc = GetBrowsingContext();
  if (!bc) {
    return false;
  }

  if (!fm->IsInActiveWindow(bc)) {
    return false;
  }

  return fm->IsSameOrAncestor(bc, fm->GetFocusedBrowsingContext());
}

bool Document::ThisDocumentHasFocus() const {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  return fm && fm->GetFocusedWindow() &&
         fm->GetFocusedWindow()->GetExtantDoc() == this;
}

void Document::GetDesignMode(nsAString& aDesignMode) {
  if (IsInDesignMode()) {
    aDesignMode.AssignLiteral("on");
  } else {
    aDesignMode.AssignLiteral("off");
  }
}

void Document::SetDesignMode(const nsAString& aDesignMode,
                             nsIPrincipal& aSubjectPrincipal, ErrorResult& rv) {
  SetDesignMode(aDesignMode, Some(&aSubjectPrincipal), rv);
}

static void NotifyEditableStateChange(Document& aDoc) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  nsMutationGuard g;
#endif
  for (nsIContent* node = aDoc.GetNextNode(&aDoc); node;
       node = node->GetNextNode(&aDoc)) {
    if (auto* element = Element::FromNode(node)) {
      element->UpdateEditableState(true);
    }
  }
  MOZ_DIAGNOSTIC_ASSERT(!g.Mutated(0));
}

void Document::SetDocumentEditableFlag(bool aEditable) {
  if (HasFlag(NODE_IS_EDITABLE) == aEditable) {
    return;
  }
  SetEditableFlag(aEditable);
  // Changing the NODE_IS_EDITABLE flags on document changes the intrinsic
  // state of all descendant elements of it. Update that now.
  NotifyEditableStateChange(*this);
}

void Document::SetDesignMode(const nsAString& aDesignMode,
                             const Maybe<nsIPrincipal*>& aSubjectPrincipal,
                             ErrorResult& rv) {
  if (aSubjectPrincipal.isSome() &&
      !aSubjectPrincipal.value()->Subsumes(NodePrincipal())) {
    rv.Throw(NS_ERROR_DOM_PROP_ACCESS_DENIED);
    return;
  }
  const bool editableMode = IsInDesignMode();
  if (aDesignMode.LowerCaseEqualsASCII(editableMode ? "off" : "on")) {
    SetDocumentEditableFlag(!editableMode);
    rv = EditingStateChanged();
  }
}

nsCommandManager* Document::GetMidasCommandManager() {
  // check if we have it cached
  if (mMidasCommandManager) {
    return mMidasCommandManager;
  }

  nsPIDOMWindowOuter* window = GetWindow();
  if (!window) {
    return nullptr;
  }

  nsIDocShell* docshell = window->GetDocShell();
  if (!docshell) {
    return nullptr;
  }

  mMidasCommandManager = docshell->GetCommandManager();
  return mMidasCommandManager;
}

// static
void Document::EnsureInitializeInternalCommandDataHashtable() {
  if (sInternalCommandDataHashtable) {
    return;
  }
  using CommandOnTextEditor = InternalCommandData::CommandOnTextEditor;
  sInternalCommandDataHashtable = new InternalCommandDataHashtable();
  // clang-format off
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"bold"_ns,
      InternalCommandData(
          "cmd_bold",
          Command::FormatBold,
          ExecCommandParam::Ignore,
          StyleUpdatingCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"italic"_ns,
      InternalCommandData(
          "cmd_italic",
          Command::FormatItalic,
          ExecCommandParam::Ignore,
          StyleUpdatingCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"underline"_ns,
      InternalCommandData(
          "cmd_underline",
          Command::FormatUnderline,
          ExecCommandParam::Ignore,
          StyleUpdatingCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"strikethrough"_ns,
      InternalCommandData(
          "cmd_strikethrough",
          Command::FormatStrikeThrough,
          ExecCommandParam::Ignore,
          StyleUpdatingCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"subscript"_ns,
      InternalCommandData(
          "cmd_subscript",
          Command::FormatSubscript,
          ExecCommandParam::Ignore,
          StyleUpdatingCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"superscript"_ns,
      InternalCommandData(
          "cmd_superscript",
          Command::FormatSuperscript,
          ExecCommandParam::Ignore,
          StyleUpdatingCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"cut"_ns,
      InternalCommandData(
          "cmd_cut",
          Command::Cut,
          ExecCommandParam::Ignore,
          CutCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"copy"_ns,
      InternalCommandData(
          "cmd_copy",
          Command::Copy,
          ExecCommandParam::Ignore,
          CopyCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"paste"_ns,
      InternalCommandData(
          "cmd_paste",
          Command::Paste,
          ExecCommandParam::Ignore,
          PasteCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"delete"_ns,
      InternalCommandData(
          "cmd_deleteCharBackward",
          Command::DeleteCharBackward,
          ExecCommandParam::Ignore,
          DeleteCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"forwarddelete"_ns,
      InternalCommandData(
          "cmd_deleteCharForward",
          Command::DeleteCharForward,
          ExecCommandParam::Ignore,
          DeleteCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"selectall"_ns,
      InternalCommandData(
          "cmd_selectAll",
          Command::SelectAll,
          ExecCommandParam::Ignore,
          SelectAllCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"undo"_ns,
      InternalCommandData(
          "cmd_undo",
          Command::HistoryUndo,
          ExecCommandParam::Ignore,
          UndoCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"redo"_ns,
      InternalCommandData(
          "cmd_redo",
          Command::HistoryRedo,
          ExecCommandParam::Ignore,
          RedoCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"indent"_ns,
      InternalCommandData("cmd_indent",
          Command::FormatIndent,
          ExecCommandParam::Ignore,
          IndentCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"outdent"_ns,
      InternalCommandData(
          "cmd_outdent",
          Command::FormatOutdent,
          ExecCommandParam::Ignore,
          OutdentCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"backcolor"_ns,
      InternalCommandData(
          "cmd_highlight",
          Command::FormatBackColor,
          ExecCommandParam::String,
          HighlightColorStateCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"hilitecolor"_ns,
      InternalCommandData(
          "cmd_highlight",
          Command::FormatBackColor,
          ExecCommandParam::String,
          HighlightColorStateCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"forecolor"_ns,
      InternalCommandData(
          "cmd_fontColor",
          Command::FormatFontColor,
          ExecCommandParam::String,
          FontColorStateCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"fontname"_ns,
      InternalCommandData(
          "cmd_fontFace",
          Command::FormatFontName,
          ExecCommandParam::String,
          FontFaceStateCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"fontsize"_ns,
      InternalCommandData(
          "cmd_fontSize",
          Command::FormatFontSize,
          ExecCommandParam::String,
          FontSizeStateCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"inserthorizontalrule"_ns,
      InternalCommandData(
          "cmd_insertHR",
          Command::InsertHorizontalRule,
          ExecCommandParam::Ignore,
          InsertTagCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"createlink"_ns,
      InternalCommandData(
          "cmd_insertLinkNoUI",
          Command::InsertLink,
          ExecCommandParam::String,
          InsertTagCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"insertimage"_ns,
      InternalCommandData(
          "cmd_insertImageNoUI",
          Command::InsertImage,
          ExecCommandParam::String,
          InsertTagCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"inserthtml"_ns,
      InternalCommandData(
          "cmd_insertHTML",
          Command::InsertHTML,
          ExecCommandParam::String,
          InsertHTMLCommand::GetInstance,
          // TODO: Chromium inserts text content of the document fragment
          //       created from the param.
          //       https://source.chromium.org/chromium/chromium/src/+/master:third_party/blink/renderer/core/editing/commands/insert_commands.cc;l=105;drc=a4708b724062f17824815b896c3aaa43825128f8
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"inserttext"_ns,
      InternalCommandData(
          "cmd_insertText",
          Command::InsertText,
          ExecCommandParam::String,
          InsertPlaintextCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"justifyleft"_ns,
      InternalCommandData(
          "cmd_align",
          Command::FormatJustifyLeft,
          ExecCommandParam::Ignore,  // Will be set to "left"
          AlignCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"justifyright"_ns,
      InternalCommandData(
          "cmd_align",
          Command::FormatJustifyRight,
          ExecCommandParam::Ignore,  // Will be set to "right"
          AlignCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"justifycenter"_ns,
      InternalCommandData(
          "cmd_align",
          Command::FormatJustifyCenter,
          ExecCommandParam::Ignore,  // Will be set to "center"
          AlignCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"justifyfull"_ns,
      InternalCommandData(
          "cmd_align",
          Command::FormatJustifyFull,
          ExecCommandParam::Ignore,  // Will be set to "justify"
          AlignCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"removeformat"_ns,
      InternalCommandData(
          "cmd_removeStyles",
          Command::FormatRemove,
          ExecCommandParam::Ignore,
          RemoveStylesCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"unlink"_ns,
      InternalCommandData(
          "cmd_removeLinks",
          Command::FormatRemoveLink,
          ExecCommandParam::Ignore,
          StyleUpdatingCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"insertorderedlist"_ns,
      InternalCommandData(
          "cmd_ol",
          Command::InsertOrderedList,
          ExecCommandParam::Ignore,
          ListCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"insertunorderedlist"_ns,
      InternalCommandData(
          "cmd_ul",
          Command::InsertUnorderedList,
          ExecCommandParam::Ignore,
          ListCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"insertparagraph"_ns,
      InternalCommandData(
          "cmd_insertParagraph",
          Command::InsertParagraph,
          ExecCommandParam::Ignore,
          InsertParagraphCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"insertlinebreak"_ns,
      InternalCommandData(
          "cmd_insertLineBreak",
          Command::InsertLineBreak,
          ExecCommandParam::Ignore,
          InsertLineBreakCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"formatblock"_ns,
      InternalCommandData(
          "cmd_formatBlock",
          Command::FormatBlock,
          ExecCommandParam::String,
          FormatBlockStateCommand::GetInstance,
          CommandOnTextEditor::Disabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"styleWithCSS"_ns,
      InternalCommandData(
          "cmd_setDocumentUseCSS",
          Command::SetDocumentUseCSS,
          ExecCommandParam::Boolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"usecss"_ns,  // Legacy command
      InternalCommandData(
          "cmd_setDocumentUseCSS",
          Command::SetDocumentUseCSS,
          ExecCommandParam::InvertedBoolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"contentReadOnly"_ns,
      InternalCommandData(
          "cmd_setDocumentReadOnly",
          Command::SetDocumentReadOnly,
          ExecCommandParam::Boolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::Enabled));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"insertBrOnReturn"_ns,
      InternalCommandData(
          "cmd_insertBrOnReturn",
          Command::SetDocumentInsertBROnEnterKeyPress,
          ExecCommandParam::Boolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"defaultParagraphSeparator"_ns,
      InternalCommandData(
          "cmd_defaultParagraphSeparator",
          Command::SetDocumentDefaultParagraphSeparator,
          ExecCommandParam::String,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"enableObjectResizing"_ns,
      InternalCommandData(
          "cmd_enableObjectResizing",
          Command::ToggleObjectResizers,
          ExecCommandParam::Boolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"enableInlineTableEditing"_ns,
      InternalCommandData(
          "cmd_enableInlineTableEditing",
          Command::ToggleInlineTableEditor,
          ExecCommandParam::Boolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"enableAbsolutePositionEditing"_ns,
      InternalCommandData(
          "cmd_enableAbsolutePositionEditing",
          Command::ToggleAbsolutePositionEditor,
          ExecCommandParam::Boolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"enableCompatibleJoinSplitDirection"_ns,
      InternalCommandData("cmd_enableCompatibleJoinSplitNodeDirection",
          Command::EnableCompatibleJoinSplitNodeDirection,
          ExecCommandParam::Boolean,
          SetDocumentStateCommand::GetInstance,
          CommandOnTextEditor::FallThrough));
#if 0
  // with empty string
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"justifynone"_ns,
      InternalCommandData(
          "cmd_align",
          Command::Undefined,
          ExecCommandParam::Ignore,
          nullptr,
          CommandOnTextEditor::Disabled));  // Not implemented yet.
  // REQUIRED SPECIAL REVIEW special review
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"saveas"_ns,
      InternalCommandData(
          "cmd_saveAs",
          Command::Undefined,
          ExecCommandParam::Boolean,
          nullptr,
          CommandOnTextEditor::FallThrough));  // Not implemented yet.
  // REQUIRED SPECIAL REVIEW special review
  sInternalCommandDataHashtable->InsertOrUpdate(
      u"print"_ns,
      InternalCommandData(
          "cmd_print",
          Command::Undefined,
          ExecCommandParam::Boolean,
          nullptr,
          CommandOnTextEditor::FallThrough));  // Not implemented yet.
#endif  // #if 0
  // clang-format on
}

Document::InternalCommandData Document::ConvertToInternalCommand(
    const nsAString& aHTMLCommandName,
    const TrustedHTMLOrString* aValue /* = nullptr */,
    ErrorResult* aRv /* = nullptr */,
    nsAString* aAdjustedValue /* = nullptr */) {
  MOZ_ASSERT(!aAdjustedValue || aAdjustedValue->IsEmpty());
  EnsureInitializeInternalCommandDataHashtable();
  InternalCommandData commandData;
  if (!sInternalCommandDataHashtable->Get(aHTMLCommandName, &commandData)) {
    return InternalCommandData();
  }
  // Ignore if the command is disabled by a corresponding pref due to Gecko
  // specific.
  switch (commandData.mCommand) {
    case Command::SetDocumentReadOnly:
      if (!StaticPrefs::dom_document_edit_command_contentReadOnly_enabled() &&
          aHTMLCommandName.LowerCaseEqualsLiteral("contentreadonly")) {
        return InternalCommandData();
      }
      break;
    case Command::SetDocumentInsertBROnEnterKeyPress:
      MOZ_DIAGNOSTIC_ASSERT(
          aHTMLCommandName.LowerCaseEqualsLiteral("insertbronreturn"));
      if (!StaticPrefs::dom_document_edit_command_insertBrOnReturn_enabled()) {
        return InternalCommandData();
      }
      break;
    default:
      break;
  }
  if (!aAdjustedValue) {
    // No further work to do
    return commandData;
  }
  MOZ_ASSERT(aValue);
  MOZ_ASSERT(aRv);
  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString = nullptr;
  if (commandData.mCommand == Command::InsertHTML) {
    constexpr nsLiteralString sink = u"Document execCommand"_ns;
    compliantString = TrustedTypeUtils::GetTrustedTypesCompliantString(
        *aValue, sink, kTrustedTypesOnlySinkGroup, *this, compliantStringHolder,
        *aRv);
    if (aRv->Failed()) {
      return InternalCommandData();
    }
  } else {
    compliantString = aValue->IsString() ? &aValue->GetAsString()
                                         : &aValue->GetAsTrustedHTML().mData;
  }

  switch (commandData.mExecCommandParam) {
    case ExecCommandParam::Ignore:
      // Just have to copy it, no checking
      switch (commandData.mCommand) {
        case Command::FormatJustifyLeft:
          aAdjustedValue->AssignLiteral("left");
          break;
        case Command::FormatJustifyRight:
          aAdjustedValue->AssignLiteral("right");
          break;
        case Command::FormatJustifyCenter:
          aAdjustedValue->AssignLiteral("center");
          break;
        case Command::FormatJustifyFull:
          aAdjustedValue->AssignLiteral("justify");
          break;
        default:
          MOZ_ASSERT(EditorCommand::GetParamType(commandData.mCommand) ==
                     EditorCommandParamType::None);
          break;
      }
      return commandData;

    case ExecCommandParam::Boolean:
      MOZ_ASSERT(!!(EditorCommand::GetParamType(commandData.mCommand) &
                    EditorCommandParamType::Bool));
      // If this is a boolean value and it's not explicitly false (e.g. no
      // value).  We default to "true" (see bug 301490).
      if (!compliantString->LowerCaseEqualsLiteral("false")) {
        aAdjustedValue->AssignLiteral("true");
      } else {
        aAdjustedValue->AssignLiteral("false");
      }
      return commandData;

    case ExecCommandParam::InvertedBoolean:
      MOZ_ASSERT(!!(EditorCommand::GetParamType(commandData.mCommand) &
                    EditorCommandParamType::Bool));
      // For old backwards commands we invert the check.
      if (compliantString->LowerCaseEqualsLiteral("false")) {
        aAdjustedValue->AssignLiteral("true");
      } else {
        aAdjustedValue->AssignLiteral("false");
      }
      return commandData;

    case ExecCommandParam::String:
      MOZ_ASSERT(!!(
          EditorCommand::GetParamType(commandData.mCommand) &
          (EditorCommandParamType::String | EditorCommandParamType::CString)));
      switch (commandData.mCommand) {
        case Command::FormatBlock: {
          const char16_t* start = compliantString->BeginReading();
          const char16_t* end = compliantString->EndReading();
          if (start != end && *start == '<' && *(end - 1) == '>') {
            ++start;
            --end;
          }
          // XXX Should we reorder this array with actual usage?
          static const nsStaticAtom* kFormattableBlockTags[] = {
              // clang-format off
            nsGkAtoms::address,
            nsGkAtoms::article,
            nsGkAtoms::aside,
            nsGkAtoms::blockquote,
            nsGkAtoms::dd,
            nsGkAtoms::div,
            nsGkAtoms::dl,
            nsGkAtoms::dt,
            nsGkAtoms::footer,
            nsGkAtoms::h1,
            nsGkAtoms::h2,
            nsGkAtoms::h3,
            nsGkAtoms::h4,
            nsGkAtoms::h5,
            nsGkAtoms::h6,
            nsGkAtoms::header,
            nsGkAtoms::hgroup,
            nsGkAtoms::main,
            nsGkAtoms::nav,
            nsGkAtoms::p,
            nsGkAtoms::pre,
            nsGkAtoms::section,
              // clang-format on
          };
          nsAutoString value(nsDependentSubstring(start, end));
          ToLowerCase(value);
          const nsStaticAtom* valueAtom = NS_GetStaticAtom(value);
          for (const nsStaticAtom* kTag : kFormattableBlockTags) {
            if (valueAtom == kTag) {
              kTag->ToString(*aAdjustedValue);
              return commandData;
            }
          }
          return InternalCommandData();
        }
        case Command::FormatFontSize: {
          // Per editing spec as of April 23, 2012, we need to reject the value
          // if it's not a valid floating-point number surrounded by optional
          // whitespace.  Otherwise, we parse it as a legacy font size.  For
          // now, we just parse as a legacy font size regardless (matching
          // WebKit) -- bug 747879.
          int32_t size = nsContentUtils::ParseLegacyFontSize(*compliantString);
          if (!size) {
            return InternalCommandData();
          }
          MOZ_ASSERT(aAdjustedValue->IsEmpty());
          aAdjustedValue->AppendInt(size);
          return commandData;
        }
        case Command::InsertImage:
        case Command::InsertLink:
          if (compliantString->IsEmpty()) {
            // Invalid value, return false
            return InternalCommandData();
          }
          aAdjustedValue->Assign(*compliantString);
          return commandData;
        case Command::SetDocumentDefaultParagraphSeparator:
          if (!compliantString->LowerCaseEqualsLiteral("div") &&
              !compliantString->LowerCaseEqualsLiteral("p") &&
              !compliantString->LowerCaseEqualsLiteral("br")) {
            // Invalid value
            return InternalCommandData();
          }
          aAdjustedValue->Assign(*compliantString);
          return commandData;
        default:
          aAdjustedValue->Assign(*compliantString);
          return commandData;
      }

    default:
      MOZ_ASSERT_UNREACHABLE("New ExecCommandParam value hasn't been handled");
      return InternalCommandData();
  }
}

Document::AutoEditorCommandTarget::AutoEditorCommandTarget(
    Document& aDocument, const InternalCommandData& aCommandData)
    : mCommandData(aCommandData) {
  // We'll retrieve an editor with current DOM tree and layout information.
  // However, JS may have already hidden or remove exposed root content of
  // the editor.  Therefore, we need the latest layout information here.
  aDocument.FlushPendingNotifications(FlushType::Layout);
  if (!aDocument.GetPresShell() || aDocument.GetPresShell()->IsDestroying()) {
    mDoNothing = true;
    return;
  }

  if (nsPresContext* presContext = aDocument.GetPresContext()) {
    // Consider context of command handling which is automatically resolved
    // by order of controllers in `nsCommandManager::GetControllerForCommand()`.
    // The order is:
    //   1. TextEditor if there is an active element and it has TextEditor like
    //      <input type="text"> or <textarea>.
    //   2. HTMLEditor for the document, if there is.
    //   3. Retarget to the DocShell or nsCommandManager as what we've done.
    if (aCommandData.IsCutOrCopyCommand()) {
      // Note that we used to use DocShell to handle `cut` and `copy` command
      // for dispatching corresponding events for making possible web apps to
      // implement their own editor without editable elements but supports
      // standard shortcut keys, etc.  In this case, we prefer to use active
      // element's editor to keep same behavior.
      mActiveEditor = nsContentUtils::GetActiveEditor(presContext);
    } else {
      mActiveEditor = nsContentUtils::GetActiveEditor(presContext);
      mHTMLEditor = nsContentUtils::GetHTMLEditor(presContext);
      if (!mActiveEditor) {
        mActiveEditor = mHTMLEditor;
      }
    }
  }

  // Then, retrieve editor command class instance which should handle it
  // and can handle it now.
  if (!mActiveEditor) {
    // If the command is available without editor, we should redirect the
    // command to focused descendant with DocShell.
    if (aCommandData.IsAvailableOnlyWhenEditable()) {
      mDoNothing = true;
      return;
    }
    return;
  }

  // Otherwise, we should use EditorCommand instance (which is singleton
  // instance) when it's enabled.
  mEditorCommand = aCommandData.mGetEditorCommandFunc
                       ? aCommandData.mGetEditorCommandFunc()
                       : nullptr;
  if (!mEditorCommand) {
    mDoNothing = true;
    mActiveEditor = nullptr;
    mHTMLEditor = nullptr;
    return;
  }

  if (IsCommandEnabled()) {
    return;
  }

  // If the EditorCommand instance is disabled, we should do nothing if
  // the command requires an editor.
  if (aCommandData.IsAvailableOnlyWhenEditable()) {
    // Do nothing if editor specific commands is disabled (bug 760052).
    mDoNothing = true;
    return;
  }

  // Otherwise, we should redirect it to focused descendant with DocShell.
  mEditorCommand = nullptr;
  mActiveEditor = nullptr;
  mHTMLEditor = nullptr;
}

EditorBase* Document::AutoEditorCommandTarget::GetTargetEditor() const {
  using CommandOnTextEditor = InternalCommandData::CommandOnTextEditor;
  switch (mCommandData.mCommandOnTextEditor) {
    case CommandOnTextEditor::Enabled:
      return mActiveEditor;
    case CommandOnTextEditor::Disabled:
      return mActiveEditor && mActiveEditor->IsTextEditor()
                 ? nullptr
                 : mActiveEditor.get();
    case CommandOnTextEditor::FallThrough:
      return mHTMLEditor;
  }
  return nullptr;
}

bool Document::AutoEditorCommandTarget::IsEditable(Document* aDocument) const {
  if (RefPtr<Document> doc = aDocument->GetInProcessParentDocument()) {
    // Make sure frames are up to date, since that can affect whether
    // we're editable.
    doc->FlushPendingNotifications(FlushType::Frames);
  }
  EditorBase* targetEditor = GetTargetEditor();
  if (targetEditor && targetEditor->IsTextEditor()) {
    // FYI: When `disabled` attribute is set, `TextEditor` treats it as
    //      "readonly" too.
    return !targetEditor->IsReadonly();
  }
  return aDocument->IsEditingOn();
}

bool Document::AutoEditorCommandTarget::IsCommandEnabled() const {
  EditorBase* targetEditor = GetTargetEditor();
  if (!targetEditor) {
    return false;
  }
  MOZ_ASSERT(targetEditor == mActiveEditor || targetEditor == mHTMLEditor);
  return MOZ_KnownLive(mEditorCommand)
      ->IsCommandEnabled(mCommandData.mCommand, MOZ_KnownLive(targetEditor));
}

nsresult Document::AutoEditorCommandTarget::DoCommand(
    nsIPrincipal* aPrincipal) const {
  MOZ_ASSERT(!DoNothing());
  MOZ_ASSERT(mEditorCommand);
  EditorBase* targetEditor = GetTargetEditor();
  if (!targetEditor) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }
  MOZ_ASSERT(targetEditor == mActiveEditor || targetEditor == mHTMLEditor);
  return MOZ_KnownLive(mEditorCommand)
      ->DoCommand(mCommandData.mCommand, MOZ_KnownLive(*targetEditor),
                  aPrincipal);
}

template <typename ParamType>
nsresult Document::AutoEditorCommandTarget::DoCommandParam(
    const ParamType& aParam, nsIPrincipal* aPrincipal) const {
  MOZ_ASSERT(!DoNothing());
  MOZ_ASSERT(mEditorCommand);
  EditorBase* targetEditor = GetTargetEditor();
  if (!targetEditor) {
    return NS_SUCCESS_DOM_NO_OPERATION;
  }
  MOZ_ASSERT(targetEditor == mActiveEditor || targetEditor == mHTMLEditor);
  return MOZ_KnownLive(mEditorCommand)
      ->DoCommandParam(mCommandData.mCommand, aParam,
                       MOZ_KnownLive(*targetEditor), aPrincipal);
}

nsresult Document::AutoEditorCommandTarget::GetCommandStateParams(
    nsCommandParams& aParams) const {
  MOZ_ASSERT(mEditorCommand);
  EditorBase* targetEditor = GetTargetEditor();
  if (!targetEditor) {
    return NS_OK;
  }
  MOZ_ASSERT(targetEditor == mActiveEditor || targetEditor == mHTMLEditor);
  return MOZ_KnownLive(mEditorCommand)
      ->GetCommandStateParams(mCommandData.mCommand, MOZ_KnownLive(aParams),
                              MOZ_KnownLive(targetEditor), nullptr);
}

Document::AutoRunningExecCommandMarker::AutoRunningExecCommandMarker(
    Document& aDocument, nsIPrincipal* aPrincipal)
    : mDocument(aDocument),
      mTreatAsUserInput(EditorBase::TreatAsUserInput(aPrincipal)),
      mHasBeenRunningByContent(aDocument.mIsRunningExecCommandByContent),
      mHasBeenRunningByChromeOrAddon(
          aDocument.mIsRunningExecCommandByChromeOrAddon) {
  if (mTreatAsUserInput) {
    aDocument.mIsRunningExecCommandByChromeOrAddon = true;
  } else {
    aDocument.mIsRunningExecCommandByContent = true;
  }
}

bool Document::ExecCommand(const nsAString& aHTMLCommandName, bool aShowUI,
                           const TrustedHTMLOrString& aValue,
                           nsIPrincipal& aSubjectPrincipal, ErrorResult& aRv) {
  // Only allow on HTML documents.
  if (!IsHTMLOrXHTML()) {
    aRv.ThrowInvalidStateError(
        "execCommand is only supported on HTML documents");
    return false;
  }
  // Otherwise, don't throw exception for compatibility with Chrome.

  // if they are requesting UI from us, let's fail since we have no UI
  if (aShowUI) {
    return false;
  }

  //  for optional parameters see dom/src/base/nsHistory.cpp: HistoryImpl::Go()
  //  this might add some ugly JS dependencies?

  nsAutoString adjustedValue;
  InternalCommandData commandData =
      ConvertToInternalCommand(aHTMLCommandName, &aValue, &aRv, &adjustedValue);
  switch (commandData.mCommand) {
    case Command::DoNothing:
      return false;
    case Command::SetDocumentReadOnly:
      SetUseCounter(eUseCounter_custom_DocumentExecCommandContentReadOnly);
      break;
    case Command::EnableCompatibleJoinSplitNodeDirection:
      // We didn't allow to enable the legacy behavior once we've enabled the
      // new behavior by default.  For keeping the behavior at supporting both
      // mode, we should keep returning `false` if the web app to enable the
      // legacy mode.  Additionally, we don't support the legacy direction
      // anymore.  Therefore, we can return `false` here even if the caller is
      // an addon or chrome script.
      if (!adjustedValue.EqualsLiteral("true")) {
        return false;
      }
      break;
    default:
      break;
  }

  AutoRunningExecCommandMarker markRunningExecCommand(*this,
                                                      &aSubjectPrincipal);

  // If we're running an execCommand, we should just return false.
  // https://github.com/w3c/editing/issues/200#issuecomment-575241816
  if (!markRunningExecCommand.IsSafeToRun()) {
    return false;
  }

  // Do security check first.
  if (commandData.IsCutOrCopyCommand()) {
    if (!nsContentUtils::IsCutCopyAllowed(this, aSubjectPrincipal)) {
      // We have rejected the event due to it not being performed in an
      // input-driven context therefore, we report the error to the console.
      nsContentUtils::ReportToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                      this, nsContentUtils::eDOM_PROPERTIES,
                                      "ExecCommandCutCopyDeniedNotInputDriven");
      return false;
    }
  } else if (commandData.IsPasteCommand()) {
    if (!nsContentUtils::PrincipalHasPermission(aSubjectPrincipal,
                                                nsGkAtoms::clipboardRead)) {
      return false;
    }
  }

  // Next, consider context of command handling which is automatically resolved
  // by order of controllers in `nsCommandManager::GetControllerForCommand()`.
  AutoEditorCommandTarget editCommandTarget(*this, commandData);
  if (commandData.IsAvailableOnlyWhenEditable()) {
    if (!editCommandTarget.IsEditable(this)) {
      return false;
    }
    // If currently the editor cannot dispatch `input` events, it means that the
    // editor value is being set and that caused unexpected composition events.
    // In this case, the value will be updated to the setting value soon and
    // Chromium does not dispatch any events during the sequence but we dispatch
    // `compositionupdate` and `compositionend` events to conform to the UI
    // Events spec.  Therefore, this execCommand must be called accidentally.
    EditorBase* targetEditor = editCommandTarget.GetTargetEditor();
    if (targetEditor && targetEditor->IsSuppressingDispatchingInputEvent()) {
      return false;
    }
  }

  if (editCommandTarget.DoNothing()) {
    return false;
  }

  // If we cannot use EditorCommand instance directly, we need to handle the
  // command with traditional path (i.e., with DocShell or nsCommandManager).
  if (!editCommandTarget.IsEditor()) {
    MOZ_ASSERT(!commandData.IsAvailableOnlyWhenEditable());

    // Special case clipboard write commands like Command::Cut and
    // Command::Copy.  For such commands, we need the behaviour from
    // nsWindowRoot::GetControllers() which is to look at the focused element,
    // and defer to a focused textbox's controller.  The code past taken by
    // other commands in ExecCommand() always uses the window directly, rather
    // than deferring to the textbox, which is desireable for most editor
    // commands, but not these commands (as those should allow copying out of
    // embedded editors). This behaviour is invoked if we call DoCommand()
    // directly on the docShell.
    // XXX This means that we allow web app to pick up selected content in
    //     descendant document and write it into the clipboard when a
    //     descendant document has focus.  However, Chromium does not allow
    //     this and this seems that it's not good behavior from point of view
    //     of security.  We should treat this issue in another bug.
    if (commandData.IsCutOrCopyCommand()) {
      nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
      if (!docShell) {
        return false;
      }
      nsresult rv = docShell->DoCommand(commandData.mXULCommandName);
      if (rv == NS_SUCCESS_DOM_NO_OPERATION) {
        return false;
      }
      return NS_SUCCEEDED(rv);
    }

    // Otherwise (currently, only clipboard read commands like Command::Paste),
    // we don't need to redirect the command to focused subdocument.
    // Therefore, we should handle it with nsCommandManager as used to be.
    // It may dispatch only preceding event of editing on non-editable element
    // to make web apps possible to handle standard shortcut key, etc in
    // their own editor.
    RefPtr<nsCommandManager> commandManager = GetMidasCommandManager();
    if (!commandManager) {
      return false;
    }

    nsCOMPtr<nsPIDOMWindowOuter> window = GetWindow();
    if (!window) {
      return false;
    }

    // Return false for disabled commands (bug 760052)
    if (!commandManager->IsCommandEnabled(
            nsDependentCString(commandData.mXULCommandName), window)) {
      return false;
    }

    MOZ_ASSERT(commandData.IsPasteCommand() ||
               commandData.mCommand == Command::SelectAll);
    nsresult rv =
        commandManager->DoCommand(commandData.mXULCommandName, nullptr, window);
    return NS_SUCCEEDED(rv) && rv != NS_SUCCESS_DOM_NO_OPERATION;
  }

  // Now, our target is fixed to the editor.  So, we can use EditorCommand
  // in EditorCommandTarget directly.

  EditorCommandParamType paramType =
      EditorCommand::GetParamType(commandData.mCommand);

  // If we don't have meaningful parameter or the EditorCommand does not
  // require additional parameter, we can use `DoCommand()`.
  if (adjustedValue.IsEmpty() || paramType == EditorCommandParamType::None) {
    MOZ_ASSERT(!(paramType & EditorCommandParamType::Bool));
    nsresult rv = editCommandTarget.DoCommand(&aSubjectPrincipal);
    return NS_SUCCEEDED(rv) && rv != NS_SUCCESS_DOM_NO_OPERATION;
  }

  // If the EditorCommand requires `bool` parameter, `adjustedValue` must be
  // "true" or "false" here.  So, we can use `DoCommandParam()` which takes
  // a `bool` value.
  if (!!(paramType & EditorCommandParamType::Bool)) {
    MOZ_ASSERT(adjustedValue.EqualsLiteral("true") ||
               adjustedValue.EqualsLiteral("false"));
    nsresult rv = editCommandTarget.DoCommandParam(
        Some(adjustedValue.EqualsLiteral("true")), &aSubjectPrincipal);
    return NS_SUCCEEDED(rv) && rv != NS_SUCCESS_DOM_NO_OPERATION;
  }

  // Now, the EditorCommand requires `nsAString` or `nsACString` parameter
  // in this case.  However, `paramType` may contain both `String` and
  // `CString` but in such case, we should use `DoCommandParam()` which
  // takes `nsAString`.  So, we should check whether `paramType` contains
  // `String` or not first.
  if (!!(paramType & EditorCommandParamType::String)) {
    MOZ_ASSERT(!adjustedValue.IsVoid());
    nsresult rv =
        editCommandTarget.DoCommandParam(adjustedValue, &aSubjectPrincipal);
    return NS_SUCCEEDED(rv) && rv != NS_SUCCESS_DOM_NO_OPERATION;
  }

  // Finally, `paramType` should have `CString`.  We should use
  // `DoCommandParam()` which takes `nsACString`.
  if (!!(paramType & EditorCommandParamType::CString)) {
    NS_ConvertUTF16toUTF8 utf8Value(adjustedValue);
    MOZ_ASSERT(!utf8Value.IsVoid());
    nsresult rv =
        editCommandTarget.DoCommandParam(utf8Value, &aSubjectPrincipal);
    return NS_SUCCEEDED(rv) && rv != NS_SUCCESS_DOM_NO_OPERATION;
  }

  MOZ_ASSERT_UNREACHABLE(
      "Not yet implemented to handle new EditorCommandParamType");
  return false;
}

bool Document::QueryCommandEnabled(const nsAString& aHTMLCommandName,
                                   nsIPrincipal& aSubjectPrincipal,
                                   ErrorResult& aRv) {
  // Only allow on HTML documents.
  if (!IsHTMLOrXHTML()) {
    aRv.ThrowInvalidStateError(
        "queryCommandEnabled is only supported on HTML documents");
    return false;
  }
  // Otherwise, don't throw exception for compatibility with Chrome.

  InternalCommandData commandData = ConvertToInternalCommand(aHTMLCommandName);
  switch (commandData.mCommand) {
    case Command::DoNothing:
      return false;
    case Command::SetDocumentReadOnly:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandSupportedOrEnabledContentReadOnly);
      break;
    case Command::SetDocumentInsertBROnEnterKeyPress:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandSupportedOrEnabledInsertBrOnReturn);
      break;
    default:
      break;
  }

  // cut & copy are always allowed
  if (commandData.IsCutOrCopyCommand()) {
    return nsContentUtils::IsCutCopyAllowed(this, aSubjectPrincipal);
  }

  // Report false for restricted commands
  if (commandData.IsPasteCommand() && !aSubjectPrincipal.IsSystemPrincipal()) {
    return false;
  }

  AutoEditorCommandTarget editCommandTarget(*this, commandData);
  if (commandData.IsAvailableOnlyWhenEditable() &&
      !editCommandTarget.IsEditable(this)) {
    return false;
  }

  if (editCommandTarget.IsEditor()) {
    return editCommandTarget.IsCommandEnabled();
  }

  // get command manager and dispatch command to our window if it's acceptable
  RefPtr<nsCommandManager> commandManager = GetMidasCommandManager();
  if (!commandManager) {
    return false;
  }

  nsPIDOMWindowOuter* window = GetWindow();
  if (!window) {
    return false;
  }

  return commandManager->IsCommandEnabled(
      nsDependentCString(commandData.mXULCommandName), window);
}

bool Document::QueryCommandIndeterm(const nsAString& aHTMLCommandName,
                                    ErrorResult& aRv) {
  // Only allow on HTML documents.
  if (!IsHTMLOrXHTML()) {
    aRv.ThrowInvalidStateError(
        "queryCommandIndeterm is only supported on HTML documents");
    return false;
  }
  // Otherwise, don't throw exception for compatibility with Chrome.

  InternalCommandData commandData = ConvertToInternalCommand(aHTMLCommandName);
  if (commandData.mCommand == Command::DoNothing) {
    return false;
  }

  AutoEditorCommandTarget editCommandTarget(*this, commandData);
  if (commandData.IsAvailableOnlyWhenEditable() &&
      !editCommandTarget.IsEditable(this)) {
    return false;
  }
  RefPtr<nsCommandParams> params = new nsCommandParams();
  if (editCommandTarget.IsEditor()) {
    if (NS_FAILED(editCommandTarget.GetCommandStateParams(*params))) {
      return false;
    }
  } else {
    // get command manager and dispatch command to our window if it's acceptable
    RefPtr<nsCommandManager> commandManager = GetMidasCommandManager();
    if (!commandManager) {
      return false;
    }

    nsPIDOMWindowOuter* window = GetWindow();
    if (!window) {
      return false;
    }

    if (NS_FAILED(commandManager->GetCommandState(commandData.mXULCommandName,
                                                  window, params))) {
      return false;
    }
  }

  // If command does not have a state_mixed value, this call fails and sets
  // retval to false.  This is fine -- we want to return false in that case
  // anyway (bug 738385), so we just don't throw regardless.
  return params->GetBool("state_mixed");
}

bool Document::QueryCommandState(const nsAString& aHTMLCommandName,
                                 ErrorResult& aRv) {
  // Only allow on HTML documents.
  if (!IsHTMLOrXHTML()) {
    aRv.ThrowInvalidStateError(
        "queryCommandState is only supported on HTML documents");
    return false;
  }
  // Otherwise, don't throw exception for compatibility with Chrome.

  InternalCommandData commandData = ConvertToInternalCommand(aHTMLCommandName);
  switch (commandData.mCommand) {
    case Command::DoNothing:
      return false;
    case Command::SetDocumentReadOnly:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandStateOrValueContentReadOnly);
      break;
    case Command::SetDocumentInsertBROnEnterKeyPress:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandStateOrValueInsertBrOnReturn);
      break;
    default:
      break;
  }

  if (aHTMLCommandName.LowerCaseEqualsLiteral("usecss")) {
    // Per spec, state is supported for styleWithCSS but not useCSS, so we just
    // return false always.
    return false;
  }

  AutoEditorCommandTarget editCommandTarget(*this, commandData);
  if (commandData.IsAvailableOnlyWhenEditable() &&
      !editCommandTarget.IsEditable(this)) {
    return false;
  }
  RefPtr<nsCommandParams> params = new nsCommandParams();
  if (editCommandTarget.IsEditor()) {
    if (NS_FAILED(editCommandTarget.GetCommandStateParams(*params))) {
      return false;
    }
  } else {
    // get command manager and dispatch command to our window if it's acceptable
    RefPtr<nsCommandManager> commandManager = GetMidasCommandManager();
    if (!commandManager) {
      return false;
    }

    nsPIDOMWindowOuter* window = GetWindow();
    if (!window) {
      return false;
    }

    if (NS_FAILED(commandManager->GetCommandState(commandData.mXULCommandName,
                                                  window, params))) {
      return false;
    }
  }

  // handle alignment as a special case (possibly other commands too?)
  // Alignment is special because the external api is individual
  // commands but internally we use cmd_align with different
  // parameters.  When getting the state of this command, we need to
  // return the boolean for this particular alignment rather than the
  // string of 'which alignment is this?'
  switch (commandData.mCommand) {
    case Command::FormatJustifyLeft: {
      nsAutoCString currentValue;
      nsresult rv = params->GetCString("state_attribute", currentValue);
      if (NS_FAILED(rv)) {
        return false;
      }
      return currentValue.EqualsLiteral("left");
    }
    case Command::FormatJustifyRight: {
      nsAutoCString currentValue;
      nsresult rv = params->GetCString("state_attribute", currentValue);
      if (NS_FAILED(rv)) {
        return false;
      }
      return currentValue.EqualsLiteral("right");
    }
    case Command::FormatJustifyCenter: {
      nsAutoCString currentValue;
      nsresult rv = params->GetCString("state_attribute", currentValue);
      if (NS_FAILED(rv)) {
        return false;
      }
      return currentValue.EqualsLiteral("center");
    }
    case Command::FormatJustifyFull: {
      nsAutoCString currentValue;
      nsresult rv = params->GetCString("state_attribute", currentValue);
      if (NS_FAILED(rv)) {
        return false;
      }
      return currentValue.EqualsLiteral("justify");
    }
    default:
      break;
  }

  // If command does not have a state_all value, this call fails and sets
  // retval to false.  This is fine -- we want to return false in that case
  // anyway (bug 738385), so we just succeed and return false regardless.
  return params->GetBool("state_all");
}

bool Document::QueryCommandSupported(const nsAString& aHTMLCommandName,
                                     CallerType aCallerType, ErrorResult& aRv) {
  // Only allow on HTML documents.
  if (!IsHTMLOrXHTML()) {
    aRv.ThrowInvalidStateError(
        "queryCommandSupported is only supported on HTML documents");
    return false;
  }
  // Otherwise, don't throw exception for compatibility with Chrome.

  InternalCommandData commandData = ConvertToInternalCommand(aHTMLCommandName);
  switch (commandData.mCommand) {
    case Command::DoNothing:
      return false;
    case Command::SetDocumentReadOnly:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandSupportedOrEnabledContentReadOnly);
      break;
    case Command::SetDocumentInsertBROnEnterKeyPress:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandSupportedOrEnabledInsertBrOnReturn);
      break;
    default:
      break;
  }

  // Gecko technically supports all the clipboard commands including
  // cut/copy/paste, but non-privileged content will be unable to call
  // paste, and depending on the pref "dom.allow_cut_copy", cut and copy
  // may also be disallowed to be called from non-privileged content.
  // For that reason, we report the support status of corresponding
  // command accordingly.
  if (aCallerType != CallerType::System) {
    if (commandData.IsPasteCommand()) {
      return false;
    }
    if (commandData.IsCutOrCopyCommand() &&
        !StaticPrefs::dom_allow_cut_copy()) {
      // XXXbz should we worry about correctly reporting "true" in the
      // "restricted, but we're an addon with clipboardWrite permissions" case?
      // See also nsContentUtils::IsCutCopyAllowed.
      return false;
    }
  }

  // aHTMLCommandName is supported if it can be converted to a Midas command
  return true;
}

void Document::QueryCommandValue(const nsAString& aHTMLCommandName,
                                 nsAString& aValue, ErrorResult& aRv) {
  aValue.Truncate();

  // Only allow on HTML documents.
  if (!IsHTMLOrXHTML()) {
    aRv.ThrowInvalidStateError(
        "queryCommandValue is only supported on HTML documents");
    return;
  }
  // Otherwise, don't throw exception for compatibility with Chrome.

  InternalCommandData commandData = ConvertToInternalCommand(aHTMLCommandName);
  switch (commandData.mCommand) {
    case Command::DoNothing:
      // Return empty string
      return;
    case Command::SetDocumentReadOnly:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandStateOrValueContentReadOnly);
      break;
    case Command::SetDocumentInsertBROnEnterKeyPress:
      SetUseCounter(
          eUseCounter_custom_DocumentQueryCommandStateOrValueInsertBrOnReturn);
      break;
    default:
      break;
  }

  AutoEditorCommandTarget editCommandTarget(*this, commandData);
  if (commandData.IsAvailableOnlyWhenEditable() &&
      !editCommandTarget.IsEditable(this)) {
    return;
  }
  RefPtr<nsCommandParams> params = new nsCommandParams();
  if (editCommandTarget.IsEditor()) {
    if (NS_FAILED(params->SetCString("state_attribute", ""_ns))) {
      return;
    }

    if (NS_FAILED(editCommandTarget.GetCommandStateParams(*params))) {
      return;
    }
  } else {
    // get command manager and dispatch command to our window if it's acceptable
    RefPtr<nsCommandManager> commandManager = GetMidasCommandManager();
    if (!commandManager) {
      return;
    }

    nsCOMPtr<nsPIDOMWindowOuter> window = GetWindow();
    if (!window) {
      return;
    }

    if (NS_FAILED(params->SetCString("state_attribute", ""_ns))) {
      return;
    }

    if (NS_FAILED(commandManager->GetCommandState(commandData.mXULCommandName,
                                                  window, params))) {
      return;
    }
  }

  // If command does not have a state_attribute value, this call fails, and
  // aValue will wind up being the empty string.  This is fine -- we want to
  // return "" in that case anyway (bug 738385), so we just return NS_OK
  // regardless.
  nsAutoCString result;
  params->GetCString("state_attribute", result);
  CopyUTF8toUTF16(result, aValue);
}

void Document::MaybeEditingStateChanged() {
  if (!mPendingMaybeEditingStateChanged && mMayStartLayout &&
      mUpdateNestLevel == 0 && (mContentEditableCount > 0) != IsEditingOn()) {
    if (nsContentUtils::IsSafeToRunScript()) {
      EditingStateChanged();
    } else if (!mInDestructor) {
      nsContentUtils::AddScriptRunner(
          NewRunnableMethod("Document::MaybeEditingStateChanged", this,
                            &Document::MaybeEditingStateChanged));
    }
  }
}

void Document::NotifyFetchOrXHRSuccess() {
  if (mShouldNotifyFetchSuccess) {
    nsContentUtils::DispatchEventOnlyToChrome(
        this, this, u"DOMDocFetchSuccess"_ns, CanBubble::eNo, Cancelable::eNo,
        /* DefaultAction */ nullptr);
  }
}

void Document::SetNotifyFetchSuccess(bool aShouldNotify) {
  mShouldNotifyFetchSuccess = aShouldNotify;
}

void Document::SetNotifyFormOrPasswordRemoved(bool aShouldNotify) {
  mShouldNotifyFormOrPasswordRemoved = aShouldNotify;
}

void Document::TearingDownEditor() {
  if (IsEditingOn()) {
    mEditingState = EditingState::eTearingDown;
  }
}

nsresult Document::TurnEditingOff() {
  NS_ASSERTION(mEditingState != EditingState::eOff, "Editing is already off.");

  nsPIDOMWindowOuter* window = GetWindow();
  if (!window) {
    return NS_ERROR_FAILURE;
  }

  nsIDocShell* docshell = window->GetDocShell();
  if (!docshell) {
    return NS_ERROR_FAILURE;
  }

  bool isBeingDestroyed = false;
  docshell->IsBeingDestroyed(&isBeingDestroyed);
  if (isBeingDestroyed) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIEditingSession> editSession;
  nsresult rv = docshell->GetEditingSession(getter_AddRefs(editSession));
  NS_ENSURE_SUCCESS(rv, rv);

  // turn editing off
  rv = editSession->TearDownEditorOnWindow(window);
  NS_ENSURE_SUCCESS(rv, rv);

  mEditingState = EditingState::eOff;

  // Editor resets selection since it is being destroyed.  But if focus is
  // still into editable control, we have to initialize selection again.
  if (RefPtr<TextControlElement> textControlElement =
          TextControlElement::FromNodeOrNull(
              nsFocusManager::GetFocusedElementStatic())) {
    if (RefPtr<TextEditor> textEditor = textControlElement->GetTextEditor()) {
      textEditor->ReinitializeSelection(*textControlElement);
    }
  }

  return NS_OK;
}

static bool HasPresShell(nsPIDOMWindowOuter* aWindow) {
  nsIDocShell* docShell = aWindow->GetDocShell();
  if (!docShell) {
    return false;
  }
  return docShell->GetPresShell() != nullptr;
}

HTMLEditor* Document::GetHTMLEditor() const {
  nsPIDOMWindowOuter* window = GetWindow();
  if (!window) {
    return nullptr;
  }

  nsIDocShell* docshell = window->GetDocShell();
  if (!docshell) {
    return nullptr;
  }

  return docshell->GetHTMLEditor();
}

nsresult Document::EditingStateChanged() {
  if (mRemovedFromDocShell) {
    return NS_OK;
  }

  if (mEditingState == EditingState::eSettingUp ||
      mEditingState == EditingState::eTearingDown) {
    // XXX We shouldn't recurse
    return NS_OK;
  }

  const bool designMode = IsInDesignMode();
  const EditingState newState =
      designMode ? EditingState::eDesignMode
                 : (mContentEditableCount > 0 ? EditingState::eContentEditable
                                              : EditingState::eOff);
  if (mEditingState == newState) {
    // No changes in editing mode.
    return NS_OK;
  }

  const bool thisDocumentHasFocus = ThisDocumentHasFocus();
  if (newState == EditingState::eOff) {
    // Editing is being turned off.
    nsAutoScriptBlocker scriptBlocker;
    RefPtr<HTMLEditor> htmlEditor = GetHTMLEditor();
    nsresult rv = TurnEditingOff();
    // If this document has focus and the editing state of this document
    // becomes "off", it means that HTMLEditor won't handle any inputs nor
    // modify the DOM tree.  However, HTMLEditor may not receive `blur`
    // event for this state change since this may occur without focus change.
    // Therefore, let's notify HTMLEditor of this editing state change.
    // Note that even if focusedElement is an editable text control element,
    // it becomes not editable from HTMLEditor point of view since text
    // control elements are manged by TextEditor.
    RefPtr<Element> focusedElement = nsFocusManager::GetFocusedElementStatic();
    DebugOnly<nsresult> rvIgnored =
        HTMLEditor::FocusedElementOrDocumentBecomesNotEditable(
            htmlEditor, *this, focusedElement);
    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rvIgnored),
        "HTMLEditor::FocusedElementOrDocumentBecomesNotEditable() failed, but "
        "ignored");
    return rv;
  }

  const EditingState oldState = mEditingState;
  MOZ_ASSERT(newState == EditingState::eDesignMode ||
             newState == EditingState::eContentEditable);
  MOZ_ASSERT_IF(newState == EditingState::eDesignMode,
                oldState == EditingState::eContentEditable ||
                    oldState == EditingState::eOff);
  MOZ_ASSERT_IF(
      newState == EditingState::eContentEditable,
      oldState == EditingState::eDesignMode || oldState == EditingState::eOff);

  // Flush out style changes on our _parent_ document, if any, so that
  // our check for a presshell won't get stale information.
  if (mParentDocument) {
    mParentDocument->FlushPendingNotifications(FlushType::Style);
  }

  // get editing session, make sure this is a strong reference so the
  // window can't get deleted during the rest of this call.
  const nsCOMPtr<nsPIDOMWindowOuter> window = GetWindow();
  if (!window) {
    return NS_ERROR_FAILURE;
  }

  nsIDocShell* docshell = window->GetDocShell();
  if (!docshell) {
    return NS_ERROR_FAILURE;
  }

  // FlushPendingNotifications might destroy our docshell.
  bool isBeingDestroyed = false;
  docshell->IsBeingDestroyed(&isBeingDestroyed);
  if (isBeingDestroyed) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIEditingSession> editSession;
  nsresult rv = docshell->GetEditingSession(getter_AddRefs(editSession));
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<HTMLEditor> htmlEditor = editSession->GetHTMLEditorForWindow(window);
  if (htmlEditor) {
    // We might already have an editor if it was set up for mail, let's see
    // if this is actually the case.
    uint32_t flags = 0;
    htmlEditor->GetFlags(&flags);
    if (flags & nsIEditor::eEditorMailMask) {
      // We already have a mail editor, then we should not attempt to create
      // another one.
      return NS_OK;
    }
  }

  if (!HasPresShell(window)) {
    // We should not make the window editable or setup its editor.
    // It's probably style=display:none.
    return NS_OK;
  }

  bool makeWindowEditable = mEditingState == EditingState::eOff;
  bool spellRecheckAll = false;
  bool putOffToRemoveScriptBlockerUntilModifyingEditingState = false;
  htmlEditor = nullptr;

  {
    nsAutoEditingState push(this, EditingState::eSettingUp);

    RefPtr<PresShell> presShell = GetPresShell();
    NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);

    // If we're entering the design mode from non-editable state, put the
    // selection at the beginning of the document for compatibility reasons.
    bool collapseSelectionAtBeginningOfDocument =
        designMode && oldState == EditingState::eOff;
    // However, mEditingState may be eOff even if there is some
    // `contenteditable` area and selection has been initialized for it because
    // mEditingState for `contenteditable` may have been scheduled to modify
    // when safe.  In such case, we should not reinitialize selection.
    if (collapseSelectionAtBeginningOfDocument && mContentEditableCount) {
      Selection* selection =
          presShell->GetSelection(nsISelectionController::SELECTION_NORMAL);
      NS_WARNING_ASSERTION(selection, "Why don't we have Selection?");
      if (selection && selection->RangeCount()) {
        // Perhaps, we don't need to check whether the selection is in
        // an editing host or not because all contents will be editable
        // in designMode. (And we don't want to make this code so complicated
        // because of legacy API.)
        collapseSelectionAtBeginningOfDocument = false;
      }
    }

    MOZ_ASSERT(mStyleSetFilled);

    if (designMode) {
      // designMode is being turned on (overrides contentEditable).
      spellRecheckAll = oldState == EditingState::eContentEditable;
    }

    // Adjust focused element with new style but blur event shouldn't be fired
    // until mEditingState is modified with newState.
    nsAutoScriptBlocker scriptBlocker;
    if (designMode) {
      nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
      nsIContent* focusedContent = nsFocusManager::GetFocusedDescendant(
          window, nsFocusManager::eOnlyCurrentWindow,
          getter_AddRefs(focusedWindow));
      if (focusedContent) {
        nsIFrame* focusedFrame = focusedContent->GetPrimaryFrame();
        bool clearFocus = focusedFrame
                              ? !focusedFrame->IsFocusable()
                              : !focusedContent->IsFocusableWithoutStyle();
        if (clearFocus) {
          if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
            fm->ClearFocus(window);
            // If we need to dispatch blur event, we should put off after
            // modifying mEditingState since blur event handler may change
            // designMode state again.
            putOffToRemoveScriptBlockerUntilModifyingEditingState = true;
          }
        }
      }
    }

    if (makeWindowEditable) {
      // Editing is being turned on (through designMode or contentEditable)
      // Turn on editor.
      // XXX This can cause flushing which can change the editing state, so make
      //     sure to avoid recursing.
      rv = editSession->MakeWindowEditable(window, "html", false, false, true);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // XXX Need to call TearDownEditorOnWindow for all failures.
    htmlEditor = docshell->GetHTMLEditor();
    if (!htmlEditor) {
      // Return NS_OK even though we've failed to create an editor here.  This
      // is so that the setter of designMode on non-HTML documents does not
      // fail.
      // This is OK to do because in nsEditingSession::SetupEditorOnWindow() we
      // would detect that we can't support the mimetype if appropriate and
      // would fall onto the eEditorErrorCantEditMimeType path.
      return NS_OK;
    }

    if (collapseSelectionAtBeginningOfDocument) {
      htmlEditor->BeginningOfDocument();
    }

    if (putOffToRemoveScriptBlockerUntilModifyingEditingState) {
      nsContentUtils::AddScriptBlocker();
    }
  }

  mEditingState = newState;
  if (putOffToRemoveScriptBlockerUntilModifyingEditingState) {
    nsContentUtils::RemoveScriptBlocker();
    // If mEditingState is overwritten by another call and already disabled
    // the editing, we shouldn't keep making window editable.
    if (mEditingState == EditingState::eOff) {
      return NS_OK;
    }
  }

  if (makeWindowEditable) {
    // TODO: We should do this earlier in this method.
    //       Previously, we called `ExecCommand` with `insertBrOnReturn` command
    //       whose argument is false here.  Then, if it returns error, we
    //       stopped making it editable.  However, after bug 1697078 fixed,
    //       `ExecCommand` returns error only when the document is not XHTML's
    //       nor HTML's.  Therefore, we use same error handling for now.
    if (MOZ_UNLIKELY(NS_WARN_IF(!IsHTMLOrXHTML()))) {
      // Editor setup failed. Editing is not on after all.
      // XXX Should we reset the editable flag on nodes?
      editSession->TearDownEditorOnWindow(window);
      mEditingState = EditingState::eOff;
      return NS_ERROR_DOM_INVALID_STATE_ERR;
    }
    // Set the editor to not insert <br> elements on return when in <p> elements
    // by default.
    htmlEditor->SetReturnInParagraphCreatesNewParagraph(true);
  }

  // Resync the editor's spellcheck state.
  if (spellRecheckAll) {
    nsCOMPtr<nsISelectionController> selectionController =
        htmlEditor->GetSelectionController();
    if (NS_WARN_IF(!selectionController)) {
      return NS_ERROR_FAILURE;
    }

    RefPtr<Selection> spellCheckSelection = selectionController->GetSelection(
        nsISelectionController::SELECTION_SPELLCHECK);
    if (spellCheckSelection) {
      spellCheckSelection->RemoveAllRanges(IgnoreErrors());
    }
  }
  htmlEditor->SyncRealTimeSpell();

  MaybeDispatchCheckKeyPressEventModelEvent();

  // If this document keeps having focus, the HTMLEditor may not receive `focus`
  // event for this editing state change since this may occur without a focus
  // change.  Therefore, let's notify HTMLEditor of this editing state change.
  if (thisDocumentHasFocus && ThisDocumentHasFocus()) {
    RefPtr<Element> focusedElement = nsFocusManager::GetFocusedElementStatic();
    MOZ_ASSERT_IF(focusedElement, focusedElement->GetComposedDoc() == this);
    if ((focusedElement && focusedElement->IsEditable() &&
         (!focusedElement->IsTextControlElement() ||
          !TextControlElement::FromNode(focusedElement)
               ->IsSingleLineTextControlOrTextArea())) ||
        (!focusedElement && IsInDesignMode())) {
      DebugOnly<nsresult> rvIgnored =
          htmlEditor->FocusedElementOrDocumentBecomesEditable(*this,
                                                              focusedElement);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::FocusedElementOrDocumentBecomesEditable() failed, but "
          "ignored");
    } else if (htmlEditor->HasFocus()) {
      DebugOnly<nsresult> rvIgnored =
          HTMLEditor::FocusedElementOrDocumentBecomesNotEditable(
              htmlEditor, *this, focusedElement);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::FocusedElementOrDocumentBecomesNotEditable() failed, "
          "but ignored");
    }
  }

  return NS_OK;
}

// Helper class, used below in ChangeContentEditableCount().
class DeferredContentEditableCountChangeEvent : public Runnable {
 public:
  DeferredContentEditableCountChangeEvent(Document* aDoc, Element* aElement)
      : mozilla::Runnable("DeferredContentEditableCountChangeEvent"),
        mDoc(aDoc),
        mElement(aElement) {}

  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    if (mElement && mElement->OwnerDoc() == mDoc) {
      RefPtr<Document> doc = std::move(mDoc);
      RefPtr<Element> element = std::move(mElement);
      doc->DeferredContentEditableCountChange(element);
    }
    return NS_OK;
  }

 private:
  RefPtr<Document> mDoc;
  RefPtr<Element> mElement;
};

void Document::ChangeContentEditableCount(Element* aElement, int32_t aChange) {
  NS_ASSERTION(int32_t(mContentEditableCount) + aChange >= 0,
               "Trying to decrement too much.");

  mContentEditableCount += aChange;

  if (aElement) {
    nsContentUtils::AddScriptRunner(
        new DeferredContentEditableCountChangeEvent(this, aElement));
  }
}

void Document::DeferredContentEditableCountChange(Element* aElement) {
  const bool elementHasFocus =
      aElement && nsFocusManager::GetFocusedElementStatic() == aElement;
  if (elementHasFocus) {
    MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
    // When contenteditable of aElement is changed and HTMLEditor works with it
    // or needs to start working with it, HTMLEditor may not receive `focus`
    // event nor `blur` event because this may occur without a focus change.
    // Therefore, we need to notify HTMLEditor of this contenteditable attribute
    // change.
    RefPtr<HTMLEditor> htmlEditor = GetHTMLEditor();
    if (aElement->HasFlag(NODE_IS_EDITABLE)) {
      if (htmlEditor) {
        DebugOnly<nsresult> rvIgnored =
            htmlEditor->FocusedElementOrDocumentBecomesEditable(*this,
                                                                aElement);
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "HTMLEditor::FocusedElementOrDocumentBecomesEditable() failed, but "
            "ignored");
      }
    } else {
      DebugOnly<nsresult> rvIgnored =
          HTMLEditor::FocusedElementOrDocumentBecomesNotEditable(
              htmlEditor, *this, aElement);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::FocusedElementOrDocumentBecomesNotEditable() failed, "
          "but ignored");
    }
  }

  if (mParser ||
      (mUpdateNestLevel > 0 && (mContentEditableCount > 0) != IsEditingOn())) {
    return;
  }

  EditingState oldState = mEditingState;

  nsresult rv = EditingStateChanged();
  NS_ENSURE_SUCCESS_VOID(rv);

  if (oldState == mEditingState &&
      mEditingState == EditingState::eContentEditable) {
    // We just changed the contentEditable state of a node, we need to reset
    // the spellchecking state of that node.
    if (aElement) {
      if (RefPtr<HTMLEditor> htmlEditor = GetHTMLEditor()) {
        nsCOMPtr<nsIInlineSpellChecker> spellChecker;
        DebugOnly<nsresult> rvIgnored = htmlEditor->GetInlineSpellChecker(
            false, getter_AddRefs(spellChecker));
        NS_WARNING_ASSERTION(
            NS_SUCCEEDED(rvIgnored),
            "EditorBase::GetInlineSpellChecker() failed, but ignored");

        if (spellChecker &&
            aElement->InclusiveDescendantMayNeedSpellchecking(htmlEditor)) {
          RefPtr<nsRange> range = nsRange::Create(aElement);
          IgnoredErrorResult res;
          range->SelectNodeContents(*aElement, res);
          if (res.Failed()) {
            // The node might be detached from the document at this point,
            // which would cause this call to fail.  In this case, we can
            // safely ignore the contenteditable count change.
            return;
          }

          rv = spellChecker->SpellCheckRange(range);
          NS_ENSURE_SUCCESS_VOID(rv);
        }
      }
    }
  }

  // aElement causes creating new HTMLEditor and the element had and keep
  // having focus, the HTMLEditor won't receive `focus` event.  Therefore, we
  // need to notify HTMLEditor of it becomes editable.
  if (elementHasFocus && aElement->HasFlag(NODE_IS_EDITABLE) &&
      nsFocusManager::GetFocusedElementStatic() == aElement) {
    if (RefPtr<HTMLEditor> htmlEditor = GetHTMLEditor()) {
      DebugOnly<nsresult> rvIgnored =
          htmlEditor->FocusedElementOrDocumentBecomesEditable(*this, aElement);
      NS_WARNING_ASSERTION(
          NS_SUCCEEDED(rvIgnored),
          "HTMLEditor::FocusedElementOrDocumentBecomesEditable() failed, but "
          "ignored");
    }
  }
}

void Document::MaybeDispatchCheckKeyPressEventModelEvent() {
  // Currently, we need to check only when we're becoming editable for
  // contenteditable.
  if (mEditingState != EditingState::eContentEditable) {
    return;
  }

  if (mHasBeenEditable) {
    return;
  }
  mHasBeenEditable = true;

  // Dispatch "CheckKeyPressEventModel" event.  That is handled only by
  // KeyPressEventModelCheckerChild.  Then, it calls SetKeyPressEventModel()
  // with proper keypress event for the active web app.
  WidgetEvent checkEvent(true, eUnidentifiedEvent);
  checkEvent.mSpecifiedEventType = nsGkAtoms::onCheckKeyPressEventModel;
  checkEvent.mFlags.mCancelable = false;
  checkEvent.mFlags.mBubbles = false;
  checkEvent.mFlags.mOnlySystemGroupDispatch = true;
  // Post the event rather than dispatching it synchronously because we need
  // a call of SetKeyPressEventModel() before first key input.  Therefore, we
  // can avoid paying unnecessary runtime cost for most web apps.
  (new AsyncEventDispatcher(this, checkEvent))->PostDOMEvent();
}

void Document::SetKeyPressEventModel(uint16_t aKeyPressEventModel) {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return;
  }
  presShell->SetKeyPressEventModel(aKeyPressEventModel);
}

TimeStamp Document::LastFocusTime() const { return mLastFocusTime; }

void Document::SetLastFocusTime(const TimeStamp& aFocusTime) {
  MOZ_DIAGNOSTIC_ASSERT(!aFocusTime.IsNull());
  MOZ_DIAGNOSTIC_ASSERT(mLastFocusTime.IsNull() ||
                        aFocusTime >= mLastFocusTime);
  mLastFocusTime = aFocusTime;
}

void Document::GetReferrer(nsACString& aReferrer) const {
  aReferrer.Truncate();
  if (!mReferrerInfo) {
    return;
  }

  nsCOMPtr<nsIURI> referrer = mReferrerInfo->GetComputedReferrer();
  if (!referrer) {
    return;
  }

  URLDecorationStripper::StripTrackingIdentifiers(referrer, aReferrer);
}

void Document::GetCookie(nsAString& aCookie, ErrorResult& aRv) {
  aCookie.Truncate();  // clear current cookie in case service fails;
                       // no cookie isn't an error condition.

  nsCOMPtr<nsIPrincipal> cookiePrincipal;
  nsCOMPtr<nsIPrincipal> cookiePartitionedPrincipal;

  CookieCommons::SecurityChecksResult checkResult =
      CookieCommons::CheckGlobalAndRetrieveCookiePrincipals(
          this, getter_AddRefs(cookiePrincipal),
          getter_AddRefs(cookiePartitionedPrincipal));
  switch (checkResult) {
    case CookieCommons::SecurityChecksResult::eSandboxedError:
      aRv.ThrowSecurityError(
          "Forbidden in a sandboxed document without the 'allow-same-origin' "
          "flag.");
      return;

    case CookieCommons::SecurityChecksResult::eSecurityError:
      [[fallthrough]];

    case CookieCommons::SecurityChecksResult::eDoNotContinue:
      return;

    case CookieCommons::SecurityChecksResult::eContinue:
      break;
  }

  bool thirdParty = true;
  nsPIDOMWindowInner* innerWindow = GetInnerWindow();
  // in gtests we don't have a window, let's consider those requests as 3rd
  // party.
  if (innerWindow) {
    ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();

    if (thirdPartyUtil) {
      Unused << thirdPartyUtil->IsThirdPartyWindow(
          innerWindow->GetOuterWindow(), nullptr, &thirdParty);
    }
  }

  nsTArray<nsCOMPtr<nsIPrincipal>> principals;

  MOZ_ASSERT(cookiePrincipal);
  principals.AppendElement(cookiePrincipal);

  if (cookiePartitionedPrincipal) {
    principals.AppendElement(cookiePartitionedPrincipal);
  }

  nsTArray<RefPtr<Cookie>> cookieList;
  bool stale = false;
  int64_t currentTimeInUsec = PR_Now();
  int64_t currentTime = currentTimeInUsec / PR_USEC_PER_SEC;

  // not having a cookie service isn't an error
  nsCOMPtr<nsICookieService> service =
      do_GetService(NS_COOKIESERVICE_CONTRACTID);
  if (!service) {
    return;
  }

  nsCOMPtr<nsILoadInfo> loadInfo =
      GetChannel() ? GetChannel()->LoadInfo() : nullptr;
  bool on3pcbException = loadInfo && loadInfo->GetIsOn3PCBExceptionList();

  for (auto& principal : principals) {
    nsAutoCString baseDomain;
    nsresult rv = CookieCommons::GetBaseDomain(principal, baseDomain);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    nsAutoCString hostFromURI;
    rv = nsContentUtils::GetHostOrIPv6WithBrackets(principal, hostFromURI);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    nsAutoCString pathFromURI;
    rv = principal->GetFilePath(pathFromURI);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    nsTArray<RefPtr<Cookie>> cookies;
    service->GetCookiesFromHost(baseDomain, principal->OriginAttributesRef(),
                                cookies);
    if (cookies.IsEmpty()) {
      continue;
    }

    // check if the nsIPrincipal is using an https secure protocol.
    // if it isn't, then we can't send a secure cookie over the connection.
    bool potentiallyTrustworthy =
        principal->GetIsOriginPotentiallyTrustworthy();

    // iterate the cookies!
    for (Cookie* cookie : cookies) {
      // check the host, since the base domain lookup is conservative.
      if (!CookieCommons::DomainMatches(cookie, hostFromURI)) {
        continue;
      }

      // if the cookie is httpOnly and it's not going directly to the HTTP
      // connection, don't send it
      if (cookie->IsHttpOnly()) {
        continue;
      }

      if (thirdParty &&
          !CookieCommons::ShouldIncludeCrossSiteCookie(
              cookie, CookieJarSettings()->GetPartitionForeign(),
              IsInPrivateBrowsing(), UsingStorageAccess(), on3pcbException)) {
        continue;
      }

      // if the cookie is secure and the host scheme isn't, we can't send it
      if (cookie->IsSecure() && !potentiallyTrustworthy) {
        continue;
      }

      // if the nsIURI path doesn't match the cookie path, don't send it back
      if (!CookieCommons::PathMatches(cookie, pathFromURI)) {
        continue;
      }

      // check if the cookie has expired
      if (cookie->Expiry() <= currentTime) {
        continue;
      }

      // all checks passed - add to list and check if lastAccessed stamp needs
      // updating
      cookieList.AppendElement(cookie);
      if (cookie->IsStale()) {
        stale = true;
      }
    }
  }

  if (cookieList.IsEmpty()) {
    return;
  }

  // update lastAccessed timestamps. we only do this if the timestamp is stale
  // by a certain amount, to avoid thrashing the db during pageload.
  if (stale) {
    service->StaleCookies(cookieList, currentTimeInUsec);
  }

  // return cookies in order of path length; longest to shortest.
  // this is required per RFC2109.  if cookies match in length,
  // then sort by creation time (see bug 236772).
  cookieList.Sort(CompareCookiesForSending());

  nsAutoCString cookieString;
  CookieCommons::ComposeCookieString(cookieList, cookieString);

  // CopyUTF8toUTF16 doesn't handle error
  // because it assumes that the input is valid.
  UTF_8_ENCODING->DecodeWithoutBOMHandling(cookieString, aCookie);
}

void Document::SetCookie(const nsAString& aCookieString, ErrorResult& aRv) {
  nsCOMPtr<nsIPrincipal> cookiePrincipal;

  CookieCommons::SecurityChecksResult checkResult =
      CookieCommons::CheckGlobalAndRetrieveCookiePrincipals(
          this, getter_AddRefs(cookiePrincipal), nullptr);
  switch (checkResult) {
    case CookieCommons::SecurityChecksResult::eSandboxedError:
      aRv.ThrowSecurityError(
          "Forbidden in a sandboxed document without the 'allow-same-origin' "
          "flag.");
      return;

    case CookieCommons::SecurityChecksResult::eSecurityError:
      [[fallthrough]];

    case CookieCommons::SecurityChecksResult::eDoNotContinue:
      return;

    case CookieCommons::SecurityChecksResult::eContinue:
      break;
  }

  if (!mDocumentURI) {
    return;
  }

  // not having a cookie service isn't an error
  nsCOMPtr<nsICookieService> service =
      do_GetService(NS_COOKIESERVICE_CONTRACTID);
  if (!service) {
    return;
  }

  NS_ConvertUTF16toUTF8 cookieString(aCookieString);

  nsCOMPtr<nsIURI> documentURI;
  nsAutoCString baseDomain;
  OriginAttributes attrs;

  int64_t currentTimeInUsec = PR_Now();

  auto* basePrincipal = BasePrincipal::Cast(NodePrincipal());
  basePrincipal->GetURI(getter_AddRefs(documentURI));
  if (NS_WARN_IF(!documentURI)) {
    // Document's principal is not a content or null (may be system), so
    // can't set cookies
    return;
  }

  // Console report takes care of the correct reporting at the exit of this
  // method.
  RefPtr<ConsoleReportCollector> crc = new ConsoleReportCollector();
  auto scopeExit = MakeScopeExit([&] { crc->FlushConsoleReports(this); });

  CookieParser cookieParser(crc, documentURI);

  ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
  if (!thirdPartyUtil) {
    return;
  }

  nsCOMPtr<nsIEffectiveTLDService> tldService =
      mozilla::components::EffectiveTLD::Service();
  if (!tldService) {
    return;
  }

  RefPtr<Cookie> cookie = CookieCommons::CreateCookieFromDocument(
      cookieParser, this, cookieString, currentTimeInUsec, tldService,
      thirdPartyUtil, baseDomain, attrs);
  if (!cookie) {
    return;
  }

  bool thirdParty = true;
  nsPIDOMWindowInner* innerWindow = GetInnerWindow();
  // in gtests we don't have a window, let's consider those requests as 3rd
  // party.
  if (innerWindow) {
    Unused << thirdPartyUtil->IsThirdPartyWindow(innerWindow->GetOuterWindow(),
                                                 nullptr, &thirdParty);
  }

  nsCOMPtr<nsILoadInfo> loadInfo =
      GetChannel() ? GetChannel()->LoadInfo() : nullptr;
  bool on3pcbException = loadInfo && loadInfo->GetIsOn3PCBExceptionList();

  if (thirdParty &&
      !CookieCommons::ShouldIncludeCrossSiteCookie(
          cookie, CookieJarSettings()->GetPartitionForeign(),
          IsInPrivateBrowsing(), UsingStorageAccess(), on3pcbException)) {
    return;
  }

  // add the cookie to the list. AddCookieFromDocument() takes care of logging.
  service->AddCookieFromDocument(cookieParser, baseDomain, attrs, *cookie,
                                 currentTimeInUsec, documentURI, thirdParty,
                                 this);

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (observerService) {
    observerService->NotifyObservers(ToSupports(this), "document-set-cookie",
                                     nsString(aCookieString).get());
  }
}

ReferrerPolicy Document::GetReferrerPolicy() const {
  return mReferrerInfo ? mReferrerInfo->ReferrerPolicy()
                       : ReferrerPolicy::_empty;
}

void Document::GetAlinkColor(nsAString& aAlinkColor) {
  aAlinkColor.Truncate();

  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->GetALink(aAlinkColor);
  }
}

void Document::SetAlinkColor(const nsAString& aAlinkColor) {
  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->SetALink(aAlinkColor);
  }
}

void Document::GetLinkColor(nsAString& aLinkColor) {
  aLinkColor.Truncate();

  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->GetLink(aLinkColor);
  }
}

void Document::SetLinkColor(const nsAString& aLinkColor) {
  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->SetLink(aLinkColor);
  }
}

void Document::GetVlinkColor(nsAString& aVlinkColor) {
  aVlinkColor.Truncate();

  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->GetVLink(aVlinkColor);
  }
}

void Document::SetVlinkColor(const nsAString& aVlinkColor) {
  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->SetVLink(aVlinkColor);
  }
}

void Document::GetBgColor(nsAString& aBgColor) {
  aBgColor.Truncate();

  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->GetBgColor(aBgColor);
  }
}

void Document::SetBgColor(const nsAString& aBgColor) {
  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->SetBgColor(aBgColor);
  }
}

void Document::GetFgColor(nsAString& aFgColor) {
  aFgColor.Truncate();

  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->GetText(aFgColor);
  }
}

void Document::SetFgColor(const nsAString& aFgColor) {
  HTMLBodyElement* body = GetBodyElement();
  if (body) {
    body->SetText(aFgColor);
  }
}

void Document::CaptureEvents() {
  WarnOnceAbout(DeprecatedOperations::eUseOfCaptureEvents);
}

void Document::ReleaseEvents() {
  WarnOnceAbout(DeprecatedOperations::eUseOfReleaseEvents);
}

HTMLAllCollection* Document::All() {
  if (!mAll) {
    mAll = new HTMLAllCollection(this);
  }
  return mAll;
}

nsresult Document::GetSrcdocData(nsAString& aSrcdocData) {
  if (mIsSrcdocDocument) {
    nsCOMPtr<nsIInputStreamChannel> inStrmChan = do_QueryInterface(mChannel);
    if (inStrmChan) {
      return inStrmChan->GetSrcdocData(aSrcdocData);
    }
  }
  aSrcdocData = VoidString();
  return NS_OK;
}

Nullable<WindowProxyHolder> Document::GetDefaultView() const {
  nsPIDOMWindowOuter* win = GetWindow();
  if (!win) {
    return nullptr;
  }
  return WindowProxyHolder(win->GetBrowsingContext());
}

nsIContent* Document::GetUnretargetedFocusedContent(
    IncludeChromeOnly aIncludeChromeOnly) const {
  nsCOMPtr<nsPIDOMWindowOuter> window = GetWindow();
  if (!window) {
    return nullptr;
  }
  nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
  nsIContent* focusedContent = nsFocusManager::GetFocusedDescendant(
      window, nsFocusManager::eOnlyCurrentWindow,
      getter_AddRefs(focusedWindow));
  if (!focusedContent) {
    return nullptr;
  }
  // be safe and make sure the element is from this document
  if (focusedContent->OwnerDoc() != this) {
    return nullptr;
  }
  if (focusedContent->ChromeOnlyAccess() &&
      aIncludeChromeOnly == IncludeChromeOnly::No) {
    return focusedContent->FindFirstNonChromeOnlyAccessContent();
  }
  return focusedContent;
}

Element* Document::GetActiveElement() {
  // Get the focused element.
  Element* focusedElement = GetRetargetedFocusedElement();
  if (focusedElement) {
    return focusedElement;
  }

  // No focused element anywhere in this document.  Try to get the BODY.
  if (IsHTMLOrXHTML()) {
    Element* bodyElement = AsHTMLDocument()->GetBody();
    if (bodyElement) {
      return bodyElement;
    }
    // Special case to handle the transition to XHTML from XUL documents
    // where there currently isn't a body element, but we need to match the
    // XUL behavior. This should be removed when bug 1540278 is resolved.
    if (nsContentUtils::IsChromeDoc(this)) {
      Element* docElement = GetDocumentElement();
      if (docElement && docElement->IsXULElement()) {
        return docElement;
      }
    }
    // Because of IE compatibility, return null when html document doesn't have
    // a body.
    return nullptr;
  }

  // If we couldn't get a BODY, return the root element.
  return GetDocumentElement();
}

Element* Document::GetCurrentScript() {
  nsCOMPtr<Element> el(do_QueryInterface(ScriptLoader()->GetCurrentScript()));
  return el;
}

void Document::ReleaseCapture() const {
  // only release the capture if the caller can access it. This prevents a
  // page from stopping a scrollbar grab for example.
  nsCOMPtr<nsINode> node = PresShell::GetCapturingContent();
  if (node && nsContentUtils::CanCallerAccess(node)) {
    PresShell::ReleaseCapturingContent();
  }
}

nsIURI* Document::GetBaseURI(bool aTryUseXHRDocBaseURI) const {
  if (aTryUseXHRDocBaseURI && mChromeXHRDocBaseURI) {
    return mChromeXHRDocBaseURI;
  }

  return GetDocBaseURI();
}

void Document::SetBaseURI(nsIURI* aURI) {
  if (!aURI && !mDocumentBaseURI) {
    return;
  }

  // Don't do anything if the URI wasn't actually changed.
  if (aURI && mDocumentBaseURI) {
    bool equalBases = false;
    mDocumentBaseURI->Equals(aURI, &equalBases);
    if (equalBases) {
      return;
    }
  }

  mDocumentBaseURI = aURI;
  mCachedURLData = nullptr;
  RefreshLinkHrefs();
}

Result<OwningNonNull<nsIURI>, nsresult> Document::ResolveWithBaseURI(
    const nsAString& aURI) {
  RefPtr<nsIURI> resolvedURI;
  MOZ_TRY(
      NS_NewURI(getter_AddRefs(resolvedURI), aURI, nullptr, GetDocBaseURI()));
  return OwningNonNull<nsIURI>(std::move(resolvedURI));
}

nsIReferrerInfo* Document::ReferrerInfoForInternalCSSAndSVGResources() {
  if (!mCachedReferrerInfoForInternalCSSAndSVGResources) {
    mCachedReferrerInfoForInternalCSSAndSVGResources =
        ReferrerInfo::CreateForInternalCSSAndSVGResources(this);
  }
  return mCachedReferrerInfoForInternalCSSAndSVGResources;
}

URLExtraData* Document::DefaultStyleAttrURLData() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mCachedURLData) {
    mCachedURLData = new URLExtraData(
        GetDocBaseURI(), ReferrerInfoForInternalCSSAndSVGResources(),
        NodePrincipal());
  }
  return mCachedURLData;
}

void Document::SetDocumentCharacterSet(NotNull<const Encoding*> aEncoding) {
  if (mCharacterSet != aEncoding) {
    mCharacterSet = aEncoding;
    mEncodingMenuDisabled = aEncoding == UTF_8_ENCODING;
    RecomputeLanguageFromCharset();

    if (nsPresContext* context = GetPresContext()) {
      context->DocumentCharSetChanged(aEncoding);
    }
  }
}

void Document::GetSandboxFlagsAsString(nsAString& aFlags) {
  nsContentUtils::SandboxFlagsToString(mSandboxFlags, aFlags);
}

void Document::GetHeaderData(nsAtom* aHeaderField, nsAString& aData) const {
  aData.Truncate();
  const HeaderData* data = mHeaderData.get();
  while (data) {
    if (data->mField == aHeaderField) {
      aData = data->mData;
      break;
    }
    data = data->mNext.get();
  }
}

void Document::SetHeaderData(nsAtom* aHeaderField, const nsAString& aData) {
  if (!aHeaderField) {
    NS_ERROR("null headerField");
    return;
  }

  if (!mHeaderData) {
    if (!aData.IsEmpty()) {  // don't bother storing empty string
      mHeaderData = MakeUnique<HeaderData>(aHeaderField, aData);
    }
  } else {
    HeaderData* data = mHeaderData.get();
    UniquePtr<HeaderData>* lastPtr = &mHeaderData;
    bool found = false;
    do {  // look for existing and replace
      if (data->mField == aHeaderField) {
        if (!aData.IsEmpty()) {
          data->mData.Assign(aData);
        } else {  // don't store empty string
          // Note that data->mNext is moved to a temporary before the old value
          // of *lastPtr is deleted.
          *lastPtr = std::move(data->mNext);
        }
        found = true;

        break;
      }
      lastPtr = &data->mNext;
      data = lastPtr->get();
    } while (data);

    if (!aData.IsEmpty() && !found) {
      // didn't find, append
      *lastPtr = MakeUnique<HeaderData>(aHeaderField, aData);
    }
  }

  if (aHeaderField == nsGkAtoms::headerContentLanguage) {
    if (aData.IsEmpty()) {
      mContentLanguage = nullptr;
    } else {
      mContentLanguage = NS_AtomizeMainThread(aData);
    }
    mMayNeedFontPrefsUpdate = true;
    if (auto* presContext = GetPresContext()) {
      presContext->ContentLanguageChanged();
    }
  }

  if (aHeaderField == nsGkAtoms::origin_trial) {
    mTrials.UpdateFromToken(aData, NodePrincipal());
    if (mTrials.IsEnabled(OriginTrial::CoepCredentialless)) {
      InitCOEP(mChannel);

      // If we still don't have a WindowContext, WindowContext::OnNewDocument
      // will take care of this.
      if (WindowContext* ctx = GetWindowContext()) {
        if (mEmbedderPolicy) {
          Unused << ctx->SetEmbedderPolicy(mEmbedderPolicy.value());
        }
      }
    }
  }

  if (aHeaderField == nsGkAtoms::headerDefaultStyle) {
    SetPreferredStyleSheetSet(aData);
  }

  if (aHeaderField == nsGkAtoms::refresh && !IsStaticDocument()) {
    // We get into this code before we have a script global yet, so get to our
    // container via mDocumentContainer.
    if (mDocumentContainer) {
      // Note: using mDocumentURI instead of mBaseURI here, for consistency
      // (used to just use the current URI of our webnavigation, but that
      // should really be the same thing).  Note that this code can run
      // before the current URI of the webnavigation has been updated, so we
      // can't assert equality here.
      mDocumentContainer->SetupRefreshURIFromHeader(this, aData);
    }
  }

  if (aHeaderField == nsGkAtoms::headerDNSPrefetchControl &&
      mAllowDNSPrefetch) {
    // Chromium treats any value other than 'on' (case insensitive) as 'off'.
    mAllowDNSPrefetch = aData.IsEmpty() || aData.LowerCaseEqualsLiteral("on");
  }

  if (aHeaderField == nsGkAtoms::handheldFriendly) {
    mViewportType = Unknown;
  }
}

void Document::SetEarlyHints(
    nsTArray<net::EarlyHintConnectArgs>&& aEarlyHints) {
  mEarlyHints = std::move(aEarlyHints);
}

void Document::TryChannelCharset(nsIChannel* aChannel, int32_t& aCharsetSource,
                                 NotNull<const Encoding*>& aEncoding,
                                 nsHtml5TreeOpExecutor* aExecutor) {
  if (aChannel) {
    nsAutoCString charsetVal;
    nsresult rv = aChannel->GetContentCharset(charsetVal);
    if (NS_SUCCEEDED(rv)) {
      const Encoding* preferred = Encoding::ForLabel(charsetVal);
      if (preferred) {
        if (aExecutor && preferred == REPLACEMENT_ENCODING) {
          aExecutor->ComplainAboutBogusProtocolCharset(this, false);
        }
        aEncoding = WrapNotNull(preferred);
        aCharsetSource = kCharsetFromChannel;
        return;
      } else if (aExecutor && !charsetVal.IsEmpty()) {
        aExecutor->ComplainAboutBogusProtocolCharset(this, true);
      }
    }
  }
}

static inline void AssertNoStaleServoDataIn(nsINode& aSubtreeRoot) {
#ifdef DEBUG
  for (nsINode* node : ShadowIncludingTreeIterator(aSubtreeRoot)) {
    const Element* element = Element::FromNode(node);
    if (!element) {
      continue;
    }
    MOZ_ASSERT(!element->HasServoData());
  }
#endif
}

already_AddRefed<PresShell> Document::CreatePresShell(
    nsPresContext* aContext, nsViewManager* aViewManager) {
  MOZ_DIAGNOSTIC_ASSERT(!mPresShell, "We have a presshell already!");

  NS_ENSURE_FALSE(GetBFCacheEntry(), nullptr);

  AssertNoStaleServoDataIn(*this);

  RefPtr<PresShell> presShell = new PresShell(this);
  // Note: we don't hold a ref to the shell (it holds a ref to us)
  mPresShell = presShell;

  if (!mStyleSetFilled) {
    FillStyleSet();
  }

  presShell->Init(aContext, aViewManager);
  if (RefPtr<class HighlightRegistry> highlightRegistry = mHighlightRegistry) {
    highlightRegistry->AddHighlightSelectionsToFrameSelection();
  }
  // Gaining a shell causes changes in how media queries are evaluated, so
  // invalidate that.
  aContext->MediaFeatureValuesChanged(
      {MediaFeatureChange::kAllChanges},
      MediaFeatureChangePropagation::JustThisDocument);

  // Make sure to never paint if we belong to an invisible DocShell.
  nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
  if (docShell && docShell->IsInvisible()) {
    presShell->SetNeverPainting(true);
  }

  MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug,
          ("DOCUMENT %p with PressShell %p and DocShell %p", this,
           presShell.get(), docShell.get()));

  mExternalResourceMap.ShowViewers();

  if (mDocumentL10n) {
    // In case we already accumulated mutations,
    // we'll trigger the refresh driver now.
    mDocumentL10n->OnCreatePresShell();
  }

  // Now that we have a shell, we might have @font-face rules (the presence of a
  // shell may change which rules apply to us). We don't need to do anything
  // like EnsureStyleFlush or such, there's nothing to update yet and when stuff
  // is ready to update we'll flush the font set.
  MarkUserFontSetDirty();

  // Take the author style disabled state from the top browsing cvontext.
  // (PageStyleChild.sys.mjs ensures this is up to date.)
  if (BrowsingContext* bc = GetBrowsingContext()) {
    presShell->SetAuthorStyleDisabled(bc->Top()->AuthorStyleDisabledDefault());
  }

  return presShell.forget();
}

// This roughly matches https://html.spec.whatwg.org/#update-the-rendering
// step 3:
//
//     Remove from docs any Document object doc for which any of the
//     following are true.
//
// If this function changes make sure to call MaybeScheduleRendering at the
// right places.
bool Document::IsRenderingSuppressed() const {
  // TODO(emilio): Per spec we should suppress when doc's visibility state is
  // "hidden", but tests rely on throttling at least (otherwise webdriver tests
  // that test minimized windows and so on time out). Maybe we can do it only in
  // content or something along those lines?
  // if (Hidden()) {
  //   return true;
  // }

  // doc's rendering is suppressed for view transitions
  if (mRenderingSuppressedForViewTransitions) {
    return true;
  }
  // The user agent believes that updating the rendering of doc's node navigable
  // would have no visible effect.
  if (!IsEventHandlingEnabled() && !IsBeingUsedAsImage() && !mDisplayDocument) {
    return true;
  }
  if (!mPresShell || !mPresShell->DidInitialize()) {
    return true;
  }
  return false;
}

void Document::MaybeScheduleRenderingPhases(RenderingPhases aPhases) {
  if (IsRenderingSuppressed()) {
    return;
  }
  MOZ_ASSERT(mPresShell);
  nsRefreshDriver* rd = mPresShell->GetPresContext()->RefreshDriver();
  rd->ScheduleRenderingPhases(aPhases);
}

void Document::TakeVideoFrameRequestCallbacks(
    nsTArray<RefPtr<HTMLVideoElement>>& aVideoCallbacks) {
  MOZ_ASSERT(aVideoCallbacks.IsEmpty());
  mFrameRequestManager.Take(aVideoCallbacks);
}

void Document::TakeFrameRequestCallbacks(nsTArray<FrameRequest>& aCallbacks) {
  MOZ_ASSERT(aCallbacks.IsEmpty());
  mFrameRequestManager.Take(aCallbacks);
}

bool Document::ShouldThrottleFrameRequests() const {
  if (mStaticCloneCount > 0) {
    // Even if we're not visible, a static clone may be, so run at full speed.
    return false;
  }

  if (Hidden() && !StaticPrefs::layout_testing_top_level_always_active()) {
    // We're not visible (probably in a background tab or the bf cache).
    return true;
  }

  if (!mPresShell) {
    // Can't do anything smarter. We don't run frame requests in documents
    // without a pres shell anyways.
    return false;
  }

  if (!mPresShell->IsActive()) {
    // The pres shell is not active (we're an invisible OOP iframe or such), so
    // throttle.
    return true;
  }

  if (mPresShell->IsPaintingSuppressed()) {
    // Historically we have throttled frame requests until we've painted at
    // least once, so keep doing that.
    return true;
  }

  if (mPresShell->IsUnderHiddenEmbedderElement()) {
    // For display: none and visibility: hidden we always throttle, for
    // consistency with OOP iframes.
    return true;
  }

  Element* el = GetEmbedderElement();
  if (!el) {
    // If we're not in-process, our refresh driver is throttled separately (via
    // PresShell::SetIsActive, so not much more we can do here.
    return false;
  }

  if (!StaticPrefs::layout_throttle_in_process_iframes()) {
    return false;
  }

  // Note that because we have to scroll this document into view at least once
  // to un-throttle it, we will drop one requestAnimationFrame frame when a
  // document that previously wasn't visible scrolls into view. This is
  // acceptable / unlikely to be human-perceivable, though we could improve on
  // it if needed by adding an intersection margin or something of that sort.
  auto margin = DOMIntersectionObserver::LazyLoadingRootMargin();
  const IntersectionInput input = DOMIntersectionObserver::ComputeInput(
      *el->OwnerDoc(), /* aRoot = */ nullptr, &margin);
  const IntersectionOutput output = DOMIntersectionObserver::Intersect(
      input, *el, DOMIntersectionObserver::BoxToUse::Content);
  return !output.Intersects();
}

void Document::DeletePresShell() {
  mExternalResourceMap.HideViewers();
  mPendingFullscreenEvents.Clear();

  // When our shell goes away, request that all our images be immediately
  // discarded, so we don't carry around decoded image data for a document we
  // no longer intend to paint.
  ImageTracker()->RequestDiscardAll();

  // Now that we no longer have a shell, we need to forget about any FontFace
  // objects for @font-face rules that came from the style set. There's no need
  // to call EnsureStyleFlush either, the shell is going away anyway, so there's
  // no point on it.
  mFontFaceSetDirty = true;

  if (IsEditingOn()) {
    TurnEditingOff();
  }

  mPresShell = nullptr;

  ClearStaleServoData();
  AssertNoStaleServoDataIn(*this);

  mStyleSet->ShellDetachedFromDocument();
  mStyleSetFilled = false;
  mQuirkSheetAdded = false;
}

void Document::DisallowBFCaching(uint32_t aStatus) {
  NS_ASSERTION(!mBFCacheEntry, "We're already in the bfcache!");
  if (!mBFCacheDisallowed) {
    if (WindowGlobalChild* wgc = GetWindowGlobalChild()) {
      wgc->SendUpdateBFCacheStatus(aStatus, 0);
    }
  }
  mBFCacheDisallowed = true;
}

void Document::SetBFCacheEntry(nsIBFCacheEntry* aEntry) {
  MOZ_ASSERT(IsBFCachingAllowed() || !aEntry, "You should have checked!");

  if (mPresShell) {
    if (!aEntry && mBFCacheEntry) {
      mPresShell->StartObservingRefreshDriver();
    }
  }
  mBFCacheEntry = aEntry;
}

bool Document::RemoveFromBFCacheSync() {
  bool removed = false;
  if (nsCOMPtr<nsIBFCacheEntry> entry = GetBFCacheEntry()) {
    entry->RemoveFromBFCacheSync();
    removed = true;
  } else if (!IsCurrentActiveDocument()) {
    // In the old bfcache implementation while the new page is loading, but
    // before nsIDocumentViewer.show() has been called, the previous page
    // doesn't yet have nsIBFCacheEntry. However, the previous page isn't the
    // current active document anymore.
    DisallowBFCaching();
    removed = true;
  }

  if (mozilla::SessionHistoryInParent() && XRE_IsContentProcess()) {
    if (BrowsingContext* bc = GetBrowsingContext()) {
      if (bc->IsInBFCache()) {
        ContentChild* cc = ContentChild::GetSingleton();
        // IPC is asynchronous but the caller is supposed to check the return
        // value. The reason for 'Sync' in the method name is that the old
        // implementation may run scripts. There is Async variant in
        // the old session history implementation for the cases where
        // synchronous operation isn't safe.
        cc->SendRemoveFromBFCache(bc->Top());
        removed = true;
      }
    }
  }
  return removed;
}

static void SubDocClearEntry(PLDHashTable* table, PLDHashEntryHdr* entry) {
  SubDocMapEntry* e = static_cast<SubDocMapEntry*>(entry);

  NS_RELEASE(e->mKey);
  if (e->mSubDocument) {
    e->mSubDocument->SetParentDocument(nullptr);
    NS_RELEASE(e->mSubDocument);
  }
}

static void SubDocInitEntry(PLDHashEntryHdr* entry, const void* key) {
  SubDocMapEntry* e =
      const_cast<SubDocMapEntry*>(static_cast<const SubDocMapEntry*>(entry));

  e->mKey = const_cast<Element*>(static_cast<const Element*>(key));
  NS_ADDREF(e->mKey);

  e->mSubDocument = nullptr;
}

nsresult Document::SetSubDocumentFor(Element* aElement, Document* aSubDoc) {
  NS_ENSURE_TRUE(aElement, NS_ERROR_UNEXPECTED);

  if (!aSubDoc) {
    // aSubDoc is nullptr, remove the mapping

    if (mSubDocuments) {
      mSubDocuments->Remove(aElement);
    }
  } else {
    if (!mSubDocuments) {
      // Create a new hashtable

      static const PLDHashTableOps hash_table_ops = {
          PLDHashTable::HashVoidPtrKeyStub, PLDHashTable::MatchEntryStub,
          PLDHashTable::MoveEntryStub, SubDocClearEntry, SubDocInitEntry};

      mSubDocuments =
          MakeUnique<PLDHashTable>(&hash_table_ops, sizeof(SubDocMapEntry));
    }

    // Add a mapping to the hash table
    auto entry =
        static_cast<SubDocMapEntry*>(mSubDocuments->Add(aElement, fallible));

    if (!entry) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    if (entry->mSubDocument) {
      entry->mSubDocument->SetParentDocument(nullptr);

      // Release the old sub document
      NS_RELEASE(entry->mSubDocument);
    }

    entry->mSubDocument = aSubDoc;
    NS_ADDREF(entry->mSubDocument);

    aSubDoc->SetParentDocument(this);
  }

  return NS_OK;
}

Document* Document::GetSubDocumentFor(nsIContent* aContent) const {
  if (mSubDocuments && aContent->IsElement()) {
    auto entry = static_cast<SubDocMapEntry*>(
        mSubDocuments->Search(aContent->AsElement()));

    if (entry) {
      return entry->mSubDocument;
    }
  }

  return nullptr;
}

Element* Document::GetEmbedderElement() const {
  // We check if we're the active document in our BrowsingContext
  // by comparing against its document, rather than checking if the
  // WindowContext is cached, since mWindow may be null when we're
  // called (such as in nsPresContext::Init).
  if (BrowsingContext* bc = GetBrowsingContext()) {
    return bc->GetExtantDocument() == this ? bc->GetEmbedderElement() : nullptr;
  }

  return nullptr;
}

Element* Document::GetRootElement() const {
  return (mCachedRootElement && mCachedRootElement->GetParentNode() == this)
             ? mCachedRootElement
             : GetRootElementInternal();
}

Element* Document::GetUnfocusedKeyEventTarget() { return GetRootElement(); }

Element* Document::GetRootElementInternal() const {
  // We invoke GetRootElement() immediately before the servo traversal, so we
  // should always have a cache hit from Servo.
  MOZ_ASSERT(NS_IsMainThread());

  // Loop backwards because any non-elements, such as doctypes and PIs
  // are likely to appear before the root element.
  for (nsIContent* child = GetLastChild(); child;
       child = child->GetPreviousSibling()) {
    if (Element* element = Element::FromNode(child)) {
      const_cast<Document*>(this)->mCachedRootElement = element;
      return element;
    }
  }

  const_cast<Document*>(this)->mCachedRootElement = nullptr;
  return nullptr;
}

void Document::InsertChildBefore(nsIContent* aKid, nsIContent* aBeforeThis,
                                 bool aNotify, ErrorResult& aRv) {
  if (aKid->IsElement() && GetRootElement()) {
    NS_WARNING("Inserting root element when we already have one");
    aRv.ThrowHierarchyRequestError("There is already a root element.");
    return;
  }

  nsINode::InsertChildBefore(aKid, aBeforeThis, aNotify, aRv);
}

void Document::RemoveChildNode(nsIContent* aKid, bool aNotify,
                               const BatchRemovalState* aState) {
  Maybe<mozAutoDocUpdate> updateBatch;
  const bool removingRoot = aKid->IsElement();
  if (removingRoot) {
    updateBatch.emplace(this, aNotify);
    // Destroy the link map up front before we mess with the child list.
    DestroyElementMaps();

    // Notify early so that we can clear the cached element after notifying,
    // without having to slow down nsINode::RemoveChildNode.
    if (aNotify) {
      MutationObservers::NotifyContentWillBeRemoved(this, aKid, aState);
      aNotify = false;
    }

    // Preemptively clear mCachedRootElement, since we are about to remove it
    // from our child list, and we don't want to return this maybe-obsolete
    // value from any GetRootElement() calls that happen inside of
    // RemoveChildNode().
    // (NOTE: for this to be useful, RemoveChildNode() must NOT trigger any
    // GetRootElement() calls until after it's removed the child from mChildren.
    // Any call before that point would restore this soon-to-be-obsolete cached
    // answer, and our clearing here would be fruitless.)
    mCachedRootElement = nullptr;
  }

  nsINode::RemoveChildNode(aKid, aNotify);
  MOZ_ASSERT(mCachedRootElement != aKid,
             "Stale pointer in mCachedRootElement, after we tried to clear it "
             "(maybe somebody called GetRootElement() too early?)");
}

void Document::AddStyleSheetToStyleSets(StyleSheet& aSheet) {
  if (mStyleSetFilled) {
    EnsureStyleSet().AddDocStyleSheet(aSheet);
    ApplicableStylesChanged();
  }
}

void Document::RecordShadowStyleChange(ShadowRoot& aShadowRoot) {
  EnsureStyleSet().RecordShadowStyleChange(aShadowRoot);
  ApplicableStylesChanged(/* aKnownInShadowTree= */ true);
}

void Document::ApplicableStylesChanged(bool aKnownInShadowTree) {
  // TODO(emilio): if we decide to resolve style in display: none iframes, then
  // we need to always track style changes and remove the mStyleSetFilled.
  if (!mStyleSetFilled) {
    return;
  }
  if (!aKnownInShadowTree) {
    MarkUserFontSetDirty();
  }
  PresShell* ps = GetPresShell();
  if (!ps) {
    return;
  }

  ps->EnsureStyleFlush();
  nsPresContext* pc = ps->GetPresContext();
  if (!pc) {
    return;
  }

  if (!aKnownInShadowTree) {
    pc->MarkCounterStylesDirty();
    pc->MarkFontFeatureValuesDirty();
    pc->MarkFontPaletteValuesDirty();
  }
  pc->RestyleManager()->NextRestyleIsForCSSRuleChanges();
}

void Document::RemoveStyleSheetFromStyleSets(StyleSheet& aSheet) {
  if (mStyleSetFilled) {
    mStyleSet->RemoveStyleSheet(aSheet);
    ApplicableStylesChanged();
  }
}

void Document::InsertSheetAt(size_t aIndex, StyleSheet& aSheet) {
  DocumentOrShadowRoot::InsertSheetAt(aIndex, aSheet);

  if (aSheet.IsApplicable()) {
    AddStyleSheetToStyleSets(aSheet);
  }
}

void Document::StyleSheetApplicableStateChanged(StyleSheet& aSheet) {
  if (!aSheet.IsDirectlyAssociatedTo(*this)) {
    return;
  }
  if (aSheet.IsApplicable()) {
    AddStyleSheetToStyleSets(aSheet);
  } else {
    RemoveStyleSheetFromStyleSets(aSheet);
  }
}

void Document::PostStyleSheetApplicableStateChangeEvent(StyleSheet& aSheet) {
  if (!StyleSheetChangeEventsEnabled()) {
    return;
  }

  StyleSheetApplicableStateChangeEventInit init;
  init.mBubbles = true;
  init.mCancelable = true;
  init.mStylesheet = &aSheet;
  init.mApplicable = aSheet.IsApplicable();

  RefPtr<StyleSheetApplicableStateChangeEvent> event =
      StyleSheetApplicableStateChangeEvent::Constructor(
          this, u"StyleSheetApplicableStateChanged"_ns, init);
  event->SetTrusted(true);
  event->SetTarget(this);
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(this, event.forget(), ChromeOnlyDispatch::eYes);
  asyncDispatcher->PostDOMEvent();
}

void Document::PostStyleSheetRemovedEvent(StyleSheet& aSheet) {
  if (!StyleSheetChangeEventsEnabled()) {
    return;
  }

  StyleSheetRemovedEventInit init;
  init.mBubbles = true;
  init.mCancelable = false;
  init.mStylesheet = &aSheet;

  RefPtr<StyleSheetRemovedEvent> event =
      StyleSheetRemovedEvent::Constructor(this, u"StyleSheetRemoved"_ns, init);
  event->SetTrusted(true);
  event->SetTarget(this);
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(this, event.forget(), ChromeOnlyDispatch::eYes);
  asyncDispatcher->PostDOMEvent();
}

void Document::PostCustomPropertyRegistered(
    const PropertyDefinition& aDefinition) {
  if (!StyleSheetChangeEventsEnabled()) {
    return;
  }

  CSSCustomPropertyRegisteredEventInit init;
  init.mBubbles = true;
  init.mCancelable = false;

  InspectorCSSPropertyDefinition property;

  property.mName.Append(aDefinition.mName);
  property.mSyntax.Append(aDefinition.mSyntax);
  property.mInherits = aDefinition.mInherits;
  if (aDefinition.mInitialValue.WasPassed()) {
    property.mInitialValue.Append(aDefinition.mInitialValue.Value());
  } else {
    property.mInitialValue.SetIsVoid(true);
  }
  property.mFromJS = true;
  init.mPropertyDefinition = property;

  RefPtr<CSSCustomPropertyRegisteredEvent> event =
      CSSCustomPropertyRegisteredEvent::Constructor(
          this, u"csscustompropertyregistered"_ns, init);
  event->SetTrusted(true);
  event->SetTarget(this);
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(this, event.forget(), ChromeOnlyDispatch::eYes);
  asyncDispatcher->PostDOMEvent();
}

static int32_t FindSheet(const nsTArray<RefPtr<StyleSheet>>& aSheets,
                         nsIURI* aSheetURI) {
  for (int32_t i = aSheets.Length() - 1; i >= 0; i--) {
    bool bEqual;
    nsIURI* uri = aSheets[i]->GetSheetURI();

    if (uri && NS_SUCCEEDED(uri->Equals(aSheetURI, &bEqual)) && bEqual)
      return i;
  }

  return -1;
}

nsresult Document::LoadAdditionalStyleSheet(additionalSheetType aType,
                                            nsIURI* aSheetURI) {
  MOZ_ASSERT(aSheetURI, "null arg");

  // Checking if we have loaded this one already.
  if (FindSheet(mAdditionalSheets[aType], aSheetURI) >= 0)
    return NS_ERROR_INVALID_ARG;

  // Loading the sheet sync.
  RefPtr<css::Loader> loader = new css::Loader(GetDocGroup());

  css::SheetParsingMode parsingMode;
  switch (aType) {
    case Document::eAgentSheet:
      parsingMode = css::eAgentSheetFeatures;
      break;

    case Document::eUserSheet:
      parsingMode = css::eUserSheetFeatures;
      break;

    case Document::eAuthorSheet:
      parsingMode = css::eAuthorSheetFeatures;
      break;

    default:
      MOZ_CRASH("impossible value for aType");
  }

  auto result = loader->LoadSheetSync(aSheetURI, parsingMode,
                                      css::Loader::UseSystemPrincipal::Yes);
  if (result.isErr()) {
    return result.unwrapErr();
  }

  RefPtr<StyleSheet> sheet = result.unwrap();

  MOZ_ASSERT(sheet->IsApplicable());

  return AddAdditionalStyleSheet(aType, sheet);
}

nsresult Document::AddAdditionalStyleSheet(additionalSheetType aType,
                                           StyleSheet* aSheet) {
  if (mAdditionalSheets[aType].Contains(aSheet)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!aSheet->IsApplicable()) {
    return NS_ERROR_INVALID_ARG;
  }

  if (NS_WARN_IF(aSheet->GetAssociatedDocumentOrShadowRoot())) {
    return NS_ERROR_INVALID_ARG;
  }

  mAdditionalSheets[aType].AppendElement(aSheet);

  if (mStyleSetFilled) {
    EnsureStyleSet().AppendStyleSheet(*aSheet);
    ApplicableStylesChanged();
  }
  return NS_OK;
}

void Document::RemoveAdditionalStyleSheet(additionalSheetType aType,
                                          nsIURI* aSheetURI) {
  MOZ_ASSERT(aSheetURI);

  nsTArray<RefPtr<StyleSheet>>& sheets = mAdditionalSheets[aType];

  int32_t i = FindSheet(mAdditionalSheets[aType], aSheetURI);
  if (i >= 0) {
    RefPtr<StyleSheet> sheetRef = std::move(sheets[i]);
    sheets.RemoveElementAt(i);

    if (!mIsGoingAway) {
      MOZ_ASSERT(sheetRef->IsApplicable());
      if (mStyleSetFilled) {
        EnsureStyleSet().RemoveStyleSheet(*sheetRef);
        ApplicableStylesChanged();
      }
    }
    sheetRef->ClearAssociatedDocumentOrShadowRoot();
  }
}

nsIGlobalObject* Document::GetScopeObject() const {
  nsCOMPtr<nsIGlobalObject> scope(do_QueryReferent(mScopeObject));
  return scope;
}

DocGroup* Document::GetDocGroupOrCreate() {
  if (!mDocGroup && GetBrowsingContext()) {
    BrowsingContextGroup* group = GetBrowsingContext()->Group();
    MOZ_ASSERT(group);

    mDocGroup = group->AddDocument(this);
  }
  return mDocGroup;
}

void Document::SetScopeObject(nsIGlobalObject* aGlobal) {
  mScopeObject = do_GetWeakReference(aGlobal);
  if (aGlobal) {
    mHasHadScriptHandlingObject = true;

    nsPIDOMWindowInner* window = aGlobal->GetAsInnerWindow();
    if (!window) {
      return;
    }

    // Attempt to join a DocGroup based on our global and container now that our
    // principal is locked in (due to being added to a window).
    DocGroup* docGroup = GetDocGroupOrCreate();
    if (!docGroup) {
      // If we failed to join a DocGroup, inherit from our parent window.
      //
      // NOTE: It is possible for Document to be cross-origin to window while
      // loading a cross-origin data document over XHR with CORS. In that case,
      // the DocGroup can have a key which does not match NodePrincipal().
      MOZ_ASSERT(!mDocumentContainer,
                 "Must have DocGroup if loaded in a DocShell");
      mDocGroup = window->GetDocGroup();
      mDocGroup->AddDocument(this);
    }

#ifdef DEBUG
    AssertDocGroupMatchesKey();
#endif
    MOZ_ASSERT_IF(
        mNodeInfoManager->GetArenaAllocator(),
        mNodeInfoManager->GetArenaAllocator() == mDocGroup->ArenaAllocator());

    // Update data document's mMutationEventsEnabled early on so that we can
    // avoid extra IsURIInPrefList calls.
    if (mLoadedAsData && window->GetExtantDoc() &&
        window->GetExtantDoc() != this &&
        window->GetExtantDoc()->NodePrincipal() == NodePrincipal() &&
        mMutationEventsEnabled.isNothing()) {
      mMutationEventsEnabled.emplace(
          window->GetExtantDoc()->MutationEventsEnabled());
    }
  }
}

bool Document::ContainsEMEContent() {
  nsPIDOMWindowInner* win = GetInnerWindow();
  // Note this case is different from checking just media elements in that
  // it covers when we've created MediaKeys but not associated them with a
  // media element.
  return win && win->HasActiveMediaKeysInstance();
}

bool Document::ContainsMSEContent() {
  bool containsMSE = false;
  EnumerateActivityObservers([&containsMSE](nsISupports* aSupports) {
    nsCOMPtr<nsIContent> content(do_QueryInterface(aSupports));
    if (auto* mediaElem = HTMLMediaElement::FromNodeOrNull(content)) {
      RefPtr<MediaSource> ms = mediaElem->GetMozMediaSourceObject();
      if (ms) {
        containsMSE = true;
      }
    }
  });
  return containsMSE;
}

static void NotifyActivityChangedCallback(nsISupports* aSupports) {
  nsCOMPtr<nsIContent> content(do_QueryInterface(aSupports));
  if (auto* mediaElem = HTMLMediaElement::FromNodeOrNull(content)) {
    mediaElem->NotifyOwnerDocumentActivityChanged();
  }
  nsCOMPtr<nsIDocumentActivity> objectDocumentActivity(
      do_QueryInterface(aSupports));
  if (objectDocumentActivity) {
    objectDocumentActivity->NotifyOwnerDocumentActivityChanged();
  } else {
    nsCOMPtr<nsIImageLoadingContent> imageLoadingContent(
        do_QueryInterface(aSupports));
    if (imageLoadingContent) {
      auto* ilc =
          static_cast<nsImageLoadingContent*>(imageLoadingContent.get());
      ilc->NotifyOwnerDocumentActivityChanged();
    }
  }
}

void Document::NotifyActivityChanged() {
  EnumerateActivityObservers(NotifyActivityChangedCallback);
  // https://w3c.github.io/screen-wake-lock/#handling-document-loss-of-full-activity
  if (!IsActive()) {
    UnlockAllWakeLocks(WakeLockType::Screen);
  }
}

void Document::SetContainer(nsDocShell* aContainer) {
  if (aContainer) {
    mDocumentContainer = aContainer;
  } else {
    mDocumentContainer = WeakPtr<nsDocShell>();
  }

  mInChromeDocShell =
      aContainer && aContainer->GetBrowsingContext()->IsChrome();

  NotifyActivityChanged();

  // IsTopLevelWindowInactive depends on the docshell, so
  // update the cached value now that it's available.
  UpdateDocumentStates(DocumentState::WINDOW_INACTIVE, false);
  if (!aContainer) {
    return;
  }

  BrowsingContext* context = aContainer->GetBrowsingContext();
  MOZ_ASSERT_IF(context && mDocGroup,
                context->Group() == mDocGroup->GetBrowsingContextGroup());
  if (context && context->IsContent()) {
    SetIsTopLevelContentDocument(context->IsTopContent());
    SetIsContentDocument(true);
  } else {
    SetIsTopLevelContentDocument(false);
    SetIsContentDocument(false);
  }
}

nsISupports* Document::GetContainer() const {
  return static_cast<nsIDocShell*>(mDocumentContainer);
}

void Document::SetScriptGlobalObject(
    nsIScriptGlobalObject* aScriptGlobalObject) {
  MOZ_ASSERT(aScriptGlobalObject || !mAnimationController ||
                 mAnimationController->IsPausedByType(
                     SMILTimeContainer::PAUSE_PAGEHIDE |
                     SMILTimeContainer::PAUSE_BEGIN),
             "Clearing window pointer while animations are unpaused");

  if (mScriptGlobalObject && !aScriptGlobalObject) {
    // We're detaching from the window.  We need to grab a pointer to
    // our layout history state now.
    mLayoutHistoryState = GetLayoutHistoryState();

    // Also make sure to remove our onload blocker now if we haven't done it yet
    if (mOnloadBlockCount != 0) {
      nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
      if (loadGroup) {
        loadGroup->RemoveRequest(mOnloadBlocker, nullptr, NS_OK);
      }
    }

    if (GetController().isSome()) {
      if (imgLoader* loader = nsContentUtils::GetImgLoaderForDocument(this)) {
        loader->ClearCacheForControlledDocument(this);
      }

      // We may become controlled again if this document comes back out
      // of bfcache.  Clear our state to allow that to happen.  Only
      // clear this flag if we are actually controlled, though, so pages
      // that were force reloaded don't become controlled when they
      // come out of bfcache.
      mMaybeServiceWorkerControlled = false;
    }

    if (GetWindowContext()) {
      // The document is about to lose its window, so this is a good time to
      // send our page use counters, while we still have access to our
      // WindowContext.
      //
      // (We also do this in nsGlobalWindowInner::FreeInnerObjects(), which
      // catches some cases of documents losing their window that don't
      // get in here.)
      SendPageUseCounters();
    }
  }

  // BlockOnload() might be called before mScriptGlobalObject is set.
  // We may need to add the blocker once mScriptGlobalObject is set.
  bool needOnloadBlocker = !mScriptGlobalObject && aScriptGlobalObject;

  mScriptGlobalObject = aScriptGlobalObject;

  if (needOnloadBlocker) {
    EnsureOnloadBlocker();
  }

  // FIXME(emilio): is this really needed?
  MaybeScheduleFrameRequestCallbacks();

  if (aScriptGlobalObject) {
    // Go back to using the docshell for the layout history state
    mLayoutHistoryState = nullptr;
    SetScopeObject(aScriptGlobalObject);
    mHasHadDefaultView = true;

    if (mAllowDNSPrefetch) {
      nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
      if (docShell) {
#ifdef DEBUG
        nsCOMPtr<nsIWebNavigation> webNav =
            do_GetInterface(aScriptGlobalObject);
        NS_ASSERTION(SameCOMIdentity(webNav, docShell),
                     "Unexpected container or script global?");
#endif
        bool allowDNSPrefetch;
        docShell->GetAllowDNSPrefetch(&allowDNSPrefetch);
        mAllowDNSPrefetch = allowDNSPrefetch;
      }
    }

    // If we are set in a window that is already focused we should remember this
    // as the time the document gained focus.
    if (HasFocus(IgnoreErrors())) {
      SetLastFocusTime(TimeStamp::Now());
    }
  }

  // Remember the pointer to our window (or lack there of), to avoid
  // having to QI every time it's asked for.
  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(mScriptGlobalObject);
  mWindow = window;

  if (mReadyState != READYSTATE_COMPLETE) {
    if (auto* wgc = GetWindowGlobalChild()) {
      // This gets unset on OnPageShow.
      wgc->BlockBFCacheFor(BFCacheStatus::PAGE_LOADING);
    }
  }

  // Now that we know what our window is, we can flush the CSP errors to the
  // Web Console. We are flushing all messages that occurred and were stored in
  // the queue prior to this point.
  if (mCSP) {
    static_cast<nsCSPContext*>(mCSP.get())->flushConsoleMessages();
  }

  nsCOMPtr<nsIHttpChannelInternal> internalChannel =
      do_QueryInterface(GetChannel());
  if (internalChannel) {
    nsCOMArray<nsISecurityConsoleMessage> messages;
    DebugOnly<nsresult> rv = internalChannel->TakeAllSecurityMessages(messages);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    SendToConsole(messages);
  }

  // Set our visibility state, but do not fire the event.  This is correct
  // because either we're coming out of bfcache (in which case IsVisible() will
  // still test false at this point and no state change will happen) or we're
  // doing the initial document load and don't want to fire the event for this
  // change.
  //
  // When the visibility is changed, notify it to observers.
  // Some observers need the notification, for example HTMLMediaElement uses
  // it to update internal media resource allocation.
  // When video is loaded via VideoDocument, HTMLMediaElement and MediaDecoder
  // creation are already done before Document::SetScriptGlobalObject() call.
  // MediaDecoder decides whether starting decoding is decided based on
  // document's visibility. When the MediaDecoder is created,
  // Document::SetScriptGlobalObject() is not yet called and document is
  // hidden state. Therefore the MediaDecoder decides that decoding is
  // not yet necessary. But soon after Document::SetScriptGlobalObject()
  // call, the document becomes not hidden. At the time, MediaDecoder needs
  // to know it and needs to start updating decoding.
  UpdateVisibilityState(DispatchVisibilityChange::No);

  // The global in the template contents owner document should be the same.
  if (mTemplateContentsOwner && mTemplateContentsOwner != this) {
    mTemplateContentsOwner->SetScriptGlobalObject(aScriptGlobalObject);
  }

  // Tell the script loader about the new global object.
  if (mScriptLoader && !IsTemplateContentsOwner()) {
    mScriptLoader->SetGlobalObject(mScriptGlobalObject);
  }

  if (!mMaybeServiceWorkerControlled && mDocumentContainer &&
      mScriptGlobalObject && GetChannel()) {
    // If we are shift-reloaded, don't associate with a ServiceWorker.
    if (mDocumentContainer->IsForceReloading()) {
      NS_WARNING("Page was shift reloaded, skipping ServiceWorker control");
      return;
    }

    mMaybeServiceWorkerControlled = true;
  }
}

nsIScriptGlobalObject* Document::GetScriptHandlingObjectInternal() const {
  MOZ_ASSERT(!mScriptGlobalObject,
             "Do not call this when mScriptGlobalObject is set!");
  if (mHasHadDefaultView) {
    return nullptr;
  }

  nsCOMPtr<nsIScriptGlobalObject> scriptHandlingObject =
      do_QueryReferent(mScopeObject);
  nsCOMPtr<nsPIDOMWindowInner> win = do_QueryInterface(scriptHandlingObject);
  if (win) {
    nsPIDOMWindowOuter* outer = win->GetOuterWindow();
    if (!outer || outer->GetCurrentInnerWindow() != win) {
      NS_WARNING("Wrong inner/outer window combination!");
      return nullptr;
    }
  }
  return scriptHandlingObject;
}
void Document::SetScriptHandlingObject(nsIScriptGlobalObject* aScriptObject) {
  NS_ASSERTION(!mScriptGlobalObject || mScriptGlobalObject == aScriptObject,
               "Wrong script object!");
  if (aScriptObject) {
    SetScopeObject(aScriptObject);
    mHasHadDefaultView = false;
  }
}

nsPIDOMWindowOuter* Document::GetWindowInternal() const {
  MOZ_ASSERT(!mWindow, "This should not be called when mWindow is not null!");
  // Let's use mScriptGlobalObject. Even if the document is already removed from
  // the docshell, the outer window might be still obtainable from the it.
  nsCOMPtr<nsPIDOMWindowOuter> win;
  if (mRemovedFromDocShell) {
    // The docshell returns the outer window we are done.
    nsCOMPtr<nsIDocShell> kungFuDeathGrip(mDocumentContainer);
    if (kungFuDeathGrip) {
      win = kungFuDeathGrip->GetWindow();
    }
  } else {
    if (nsCOMPtr<nsPIDOMWindowInner> inner =
            do_QueryInterface(mScriptGlobalObject)) {
      // mScriptGlobalObject is always the inner window, let's get the outer.
      win = inner->GetOuterWindow();
    }
  }

  return win;
}

bool Document::InternalAllowXULXBL() {
  if (nsContentUtils::AllowXULXBLForPrincipal(NodePrincipal())) {
    mAllowXULXBL = eTriTrue;
    return true;
  }

  mAllowXULXBL = eTriFalse;
  return false;
}

// Note: We don't hold a reference to the document observer; we assume
// that it has a live reference to the document.
void Document::AddObserver(nsIDocumentObserver* aObserver) {
  NS_ASSERTION(mObservers.IndexOf(aObserver) == nsTArray<int>::NoIndex,
               "Observer already in the list");
  mObservers.AppendElement(aObserver);
  AddMutationObserver(aObserver);
}

bool Document::RemoveObserver(nsIDocumentObserver* aObserver) {
  // If we're in the process of destroying the document (and we're
  // informing the observers of the destruction), don't remove the
  // observers from the list. This is not a big deal, since we
  // don't hold a live reference to the observers.
  if (!mInDestructor) {
    RemoveMutationObserver(aObserver);
    return mObservers.RemoveElement(aObserver);
  }

  return mObservers.Contains(aObserver);
}

void Document::BeginUpdate() {
  ++mUpdateNestLevel;
  nsContentUtils::AddScriptBlocker();
  NS_DOCUMENT_NOTIFY_OBSERVERS(BeginUpdate, (this));
}

void Document::EndUpdate() {
  const bool reset = !mPendingMaybeEditingStateChanged;
  mPendingMaybeEditingStateChanged = true;

  NS_DOCUMENT_NOTIFY_OBSERVERS(EndUpdate, (this));

  --mUpdateNestLevel;

  nsContentUtils::RemoveScriptBlocker();

  if (mXULBroadcastManager) {
    mXULBroadcastManager->MaybeBroadcast();
  }

  if (reset) {
    mPendingMaybeEditingStateChanged = false;
  }
  MaybeEditingStateChanged();
}

void Document::BeginLoad() {
  if (IsEditingOn()) {
    // Reset() blows away all event listeners in the document, and our
    // editor relies heavily on those. Midas is turned on, to make it
    // work, re-initialize it to give it a chance to add its event
    // listeners again.

    TurnEditingOff();
    EditingStateChanged();
  }

  MOZ_ASSERT(!mDidCallBeginLoad);
  mDidCallBeginLoad = true;

  // Block onload here to prevent having to deal with blocking and
  // unblocking it while we know the document is loading.
  BlockOnload();
  mDidFireDOMContentLoaded = false;
  BlockDOMContentLoaded();

  if (mScriptLoader) {
    mScriptLoader->BeginDeferringScripts();
  }

  NS_DOCUMENT_NOTIFY_OBSERVERS(BeginLoad, (this));
}

void Document::MozSetImageElement(const nsAString& aImageElementId,
                                  Element* aElement) {
  if (aImageElementId.IsEmpty()) return;

  // Hold a script blocker while calling SetImageElement since that can call
  // out to id-observers
  nsAutoScriptBlocker scriptBlocker;

  IdentifierMapEntry* entry = mIdentifierMap.PutEntry(aImageElementId);
  if (entry) {
    entry->SetImageElement(aElement);
    if (entry->IsEmpty()) {
      mIdentifierMap.RemoveEntry(entry);
    }
  }
}

void Document::DispatchContentLoadedEvents() {
  // If you add early returns from this method, make sure you're
  // calling UnblockOnload properly.

  // Unpin references to preloaded images
  mPreloadingImages.Clear();

  // DOM manipulation after content loaded should not care if the element
  // came from the preloader.
  mPreloadedPreconnects.Clear();

  if (mTiming) {
    mTiming->NotifyDOMContentLoadedStart(Document::GetDocumentURI());
  }

  // Dispatch observer notification to notify observers document is interactive.
  nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
  if (os) {
    nsIPrincipal* principal = NodePrincipal();
    os->NotifyObservers(ToSupports(this),
                        principal->IsSystemPrincipal()
                            ? "chrome-document-interactive"
                            : "content-document-interactive",
                        nullptr);
  }

  // Fire a DOM event notifying listeners that this document has been
  // loaded (excluding images and other loads initiated by this
  // document).
  nsContentUtils::DispatchTrustedEvent(this, this, u"DOMContentLoaded"_ns,
                                       CanBubble::eYes, Cancelable::eNo);

  if (auto* const window = GetInnerWindow()) {
    const RefPtr<ServiceWorkerContainer> serviceWorker =
        window->Navigator()->ServiceWorker();

    // This could cause queued messages from a service worker to get
    // dispatched on serviceWorker.
    serviceWorker->StartMessages();
  }

  if (MayStartLayout()) {
    MaybeResolveReadyForIdle();
  }

  if (mTiming) {
    mTiming->NotifyDOMContentLoadedEnd(Document::GetDocumentURI());
  }

  // If this document is a [i]frame, fire a DOMFrameContentLoaded
  // event on all parent documents notifying that the HTML (excluding
  // other external files such as images and stylesheets) in a frame
  // has finished loading.

  // target_frame is the [i]frame element that will be used as the
  // target for the event. It's the [i]frame whose content is done
  // loading.
  nsCOMPtr<Element> target_frame = GetEmbedderElement();

  if (target_frame && target_frame->IsInComposedDoc()) {
    nsCOMPtr<Document> parent = target_frame->OwnerDoc();
    while (parent) {
      RefPtr<Event> event;
      if (parent) {
        IgnoredErrorResult ignored;
        event = parent->CreateEvent(u"Events"_ns, CallerType::System, ignored);
      }

      if (event) {
        event->InitEvent(u"DOMFrameContentLoaded"_ns, true, true);

        event->SetTarget(target_frame);
        event->SetTrusted(true);

        // To dispatch this event we must manually call
        // EventDispatcher::Dispatch() on the ancestor document since the
        // target is not in the same document, so the event would never reach
        // the ancestor document if we used the normal event
        // dispatching code.

        WidgetEvent* innerEvent = event->WidgetEventPtr();
        if (innerEvent) {
          nsEventStatus status = nsEventStatus_eIgnore;

          if (RefPtr<nsPresContext> context = parent->GetPresContext()) {
            EventDispatcher::Dispatch(parent, context, innerEvent, event,
                                      &status);
          }
        }
      }

      parent = parent->GetInProcessParentDocument();
    }
  }

  nsPIDOMWindowInner* inner = GetInnerWindow();
  if (inner) {
    inner->NoteDOMContentLoaded();
  }

  // TODO
  if (mMaybeServiceWorkerControlled) {
    using mozilla::dom::ServiceWorkerManager;
    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    if (swm) {
      Maybe<ClientInfo> clientInfo = GetClientInfo();
      if (clientInfo.isSome()) {
        swm->MaybeCheckNavigationUpdate(clientInfo.ref());
      }
    }
  }

  if (mSetCompleteAfterDOMContentLoaded) {
    SetReadyStateInternal(ReadyState::READYSTATE_COMPLETE);
    mSetCompleteAfterDOMContentLoaded = false;
  }

  UnblockOnload(true);
}

void Document::EndLoad() {
  bool turnOnEditing =
      mParser && (IsInDesignMode() || mContentEditableCount > 0);

#if defined(DEBUG)
  // only assert if nothing stopped the load on purpose
  if (!mParserAborted) {
    nsContentSecurityUtils::AssertAboutPageHasCSP(this);
    nsContentSecurityUtils::AssertChromePageHasCSP(this);
  }
#endif

  // EndLoad may have been called without a matching call to BeginLoad, in the
  // case of a failed parse (for example, due to timeout). In such a case, we
  // still want to execute part of this code to do appropriate cleanup, but we
  // gate part of it because it is intended to match 1-for-1 with calls to
  // BeginLoad. We have an explicit flag bit for this purpose, since it's
  // complicated and error prone to derive this condition from other related
  // flags that can be manipulated outside of a BeginLoad/EndLoad pair.

  // Part 1: Code that always executes to cleanup end of parsing, whether
  // that parsing was successful or not.

  // Drop the ref to our parser, if any, but keep hold of the sink so that we
  // can flush it from FlushPendingNotifications as needed.  We might have to
  // do that to get a StartLayout() to happen.
  if (mParser) {
    mWeakSink = do_GetWeakReference(mParser->GetContentSink());
    mParser = nullptr;
  }

  // Update the attributes on the PerformanceNavigationTiming before notifying
  // the onload observers.
  if (nsPIDOMWindowInner* window = GetInnerWindow()) {
    if (RefPtr<Performance> performance = window->GetPerformance()) {
      performance->UpdateNavigationTimingEntry();
    }
  }

  NS_DOCUMENT_NOTIFY_OBSERVERS(EndLoad, (this));

  // Part 2: Code that only executes when this EndLoad matches a BeginLoad.

  if (!mDidCallBeginLoad) {
    return;
  }
  mDidCallBeginLoad = false;

  UnblockDOMContentLoaded();

  if (turnOnEditing) {
    EditingStateChanged();
  }

  if (!GetWindow()) {
    // This is a document that's not in a window.  For example, this could be an
    // XMLHttpRequest responseXML document, or a document created via DOMParser
    // or DOMImplementation.  We don't reach this code normally for such
    // documents (which is not obviously correct), but can reach it via
    // document.open()/document.close().
    //
    // Such documents don't fire load events, but per spec should set their
    // readyState to "complete" when parsing and all loading of subresources is
    // done.  Parsing is done now, and documents not in a window don't load
    // subresources, so just go ahead and mark ourselves as complete.
    SetReadyStateInternal(Document::READYSTATE_COMPLETE,
                          /* updateTimingInformation = */ false);

    // Reset mSkipLoadEventAfterClose just in case.
    mSkipLoadEventAfterClose = false;
  }
}

void Document::UnblockDOMContentLoaded() {
  MOZ_ASSERT(mBlockDOMContentLoaded);
  if (--mBlockDOMContentLoaded != 0 || mDidFireDOMContentLoaded) {
    return;
  }

  MOZ_LOG(gDocumentLeakPRLog, LogLevel::Debug,
          ("DOCUMENT %p UnblockDOMContentLoaded", this));

  mDidFireDOMContentLoaded = true;

  MOZ_ASSERT(mReadyState == READYSTATE_INTERACTIVE);
  if (!mSynchronousDOMContentLoaded) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIRunnable> ev =
        NewRunnableMethod("Document::DispatchContentLoadedEvents", this,
                          &Document::DispatchContentLoadedEvents);
    Dispatch(ev.forget());
  } else {
    DispatchContentLoadedEvents();
  }
}

void Document::ElementStateChanged(Element* aElement, ElementState aStateMask) {
  MOZ_ASSERT(!nsContentUtils::IsSafeToRunScript(),
             "Someone forgot a scriptblocker");
  NS_DOCUMENT_NOTIFY_OBSERVERS(ElementStateChanged,
                               (this, aElement, aStateMask));
}

void Document::RuleChanged(StyleSheet& aSheet, css::Rule*,
                           const StyleRuleChange&) {
  if (aSheet.IsApplicable()) {
    ApplicableStylesChanged();
  }
}

void Document::RuleAdded(StyleSheet& aSheet, css::Rule& aRule) {
  if (aRule.IsIncompleteImportRule()) {
    return;
  }

  if (aSheet.IsApplicable()) {
    ApplicableStylesChanged();
  }
}

void Document::ImportRuleLoaded(StyleSheet& aSheet) {
  if (aSheet.IsApplicable()) {
    ApplicableStylesChanged();
  }
}

void Document::RuleRemoved(StyleSheet& aSheet, css::Rule& aRule) {
  if (aSheet.IsApplicable()) {
    ApplicableStylesChanged();
  }
}

static Element* GetCustomContentContainer(PresShell* aPresShell) {
  if (!aPresShell || !aPresShell->GetCanvasFrame()) {
    return nullptr;
  }

  return aPresShell->GetCanvasFrame()->GetCustomContentContainer();
}

already_AddRefed<AnonymousContent> Document::InsertAnonymousContent(
    bool aForce, ErrorResult& aRv) {
  RefPtr<PresShell> shell = GetPresShell();
  if (aForce && !GetCustomContentContainer(shell)) {
    FlushPendingNotifications(FlushType::Layout);
    shell = GetPresShell();
  }

  nsAutoScriptBlocker scriptBlocker;

  RefPtr<AnonymousContent> anonContent = AnonymousContent::Create(*this);
  if (!anonContent) {
    aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  mAnonymousContents.AppendElement(anonContent);

  if (RefPtr<Element> container = GetCustomContentContainer(shell)) {
    // If the container is empty and we have other anon content we should be
    // about to show all the other anonymous content nodes.
    if (container->HasChildren() || mAnonymousContents.Length() == 1) {
      container->AppendChildTo(anonContent->Host(), true, IgnoreErrors());
      if (auto* canvasFrame = shell->GetCanvasFrame()) {
        canvasFrame->ShowCustomContentContainer();
      }
    }
  }

  return anonContent.forget();
}

static void RemoveAnonContentFromCanvas(AnonymousContent& aAnonContent,
                                        PresShell* aPresShell) {
  RefPtr<Element> container = GetCustomContentContainer(aPresShell);
  if (!container) {
    return;
  }
  container->RemoveChild(*aAnonContent.Host(), IgnoreErrors());
}

void Document::RemoveAnonymousContent(AnonymousContent& aContent) {
  nsAutoScriptBlocker scriptBlocker;

  auto index = mAnonymousContents.IndexOf(&aContent);
  if (index == mAnonymousContents.NoIndex) {
    return;
  }

  mAnonymousContents.RemoveElementAt(index);
  RemoveAnonContentFromCanvas(aContent, GetPresShell());

  if (mAnonymousContents.IsEmpty() &&
      GetCustomContentContainer(GetPresShell())) {
    GetPresShell()->GetCanvasFrame()->HideCustomContentContainer();
  }
}

Element* Document::GetAnonRootIfInAnonymousContentContainer(
    nsINode* aNode) const {
  if (!aNode->IsInNativeAnonymousSubtree()) {
    return nullptr;
  }

  PresShell* presShell = GetPresShell();
  if (!presShell || !presShell->GetCanvasFrame()) {
    return nullptr;
  }

  nsAutoScriptBlocker scriptBlocker;
  nsCOMPtr<Element> customContainer =
      presShell->GetCanvasFrame()->GetCustomContentContainer();
  if (!customContainer) {
    return nullptr;
  }

  // An arbitrary number of elements can be inserted as children of the custom
  // container frame.  We want the one that was added that contains aNode, so
  // we need to keep track of the last child separately using |child| here.
  nsINode* child = aNode;
  nsINode* parent = aNode->GetParentNode();
  while (parent && parent->IsInNativeAnonymousSubtree()) {
    if (parent == customContainer) {
      return Element::FromNode(child);
    }
    child = parent;
    parent = child->GetParentNode();
  }
  return nullptr;
}

Maybe<ClientInfo> Document::GetClientInfo() const {
  if (const Document* orig = GetOriginalDocument()) {
    if (Maybe<ClientInfo> info = orig->GetClientInfo()) {
      return info;
    }
  }

  if (nsPIDOMWindowInner* inner = GetInnerWindow()) {
    return inner->GetClientInfo();
  }

  return Maybe<ClientInfo>();
}

Maybe<ClientState> Document::GetClientState() const {
  if (const Document* orig = GetOriginalDocument()) {
    if (Maybe<ClientState> state = orig->GetClientState()) {
      return state;
    }
  }

  if (nsPIDOMWindowInner* inner = GetInnerWindow()) {
    return inner->GetClientState();
  }

  return Maybe<ClientState>();
}

Maybe<ServiceWorkerDescriptor> Document::GetController() const {
  if (const Document* orig = GetOriginalDocument()) {
    if (Maybe<ServiceWorkerDescriptor> controller = orig->GetController()) {
      return controller;
    }
  }

  if (nsPIDOMWindowInner* inner = GetInnerWindow()) {
    return inner->GetController();
  }

  return Maybe<ServiceWorkerDescriptor>();
}

//
// Document interface
//
DocumentType* Document::GetDoctype() const {
  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->NodeType() == DOCUMENT_TYPE_NODE) {
      return static_cast<DocumentType*>(child);
    }
  }
  return nullptr;
}

DOMImplementation* Document::GetImplementation(ErrorResult& rv) {
  if (!mDOMImplementation) {
    nsCOMPtr<nsIURI> uri;
    NS_NewURI(getter_AddRefs(uri), "about:blank");
    if (!uri) {
      rv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return nullptr;
    }
    bool hasHadScriptObject = true;
    nsIScriptGlobalObject* scriptObject =
        GetScriptHandlingObject(hasHadScriptObject);
    if (!scriptObject && hasHadScriptObject) {
      rv.Throw(NS_ERROR_UNEXPECTED);
      return nullptr;
    }
    mDOMImplementation = new DOMImplementation(
        this, scriptObject ? scriptObject : GetScopeObject(), uri, uri);
  }

  return mDOMImplementation;
}

bool IsLowercaseASCII(const nsAString& aValue) {
  int32_t len = aValue.Length();
  for (int32_t i = 0; i < len; ++i) {
    char16_t c = aValue[i];
    if (!(0x0061 <= (c) && ((c) <= 0x007a))) {
      return false;
    }
  }
  return true;
}

already_AddRefed<Element> Document::CreateElement(
    const nsAString& aTagName, const ElementCreationOptionsOrString& aOptions,
    ErrorResult& rv) {
  rv = nsContentUtils::CheckQName(aTagName, false);
  if (rv.Failed()) {
    return nullptr;
  }

  bool needsLowercase = IsHTMLDocument() && !IsLowercaseASCII(aTagName);
  nsAutoString lcTagName;
  if (needsLowercase) {
    nsContentUtils::ASCIIToLower(aTagName, lcTagName);
  }

  const nsString* is = nullptr;
  PseudoStyleType pseudoType = PseudoStyleType::NotPseudo;
  if (aOptions.IsElementCreationOptions()) {
    const ElementCreationOptions& options =
        aOptions.GetAsElementCreationOptions();

    if (options.mIs.WasPassed()) {
      is = &options.mIs.Value();
    }

    // Check 'pseudo' and throw an exception if it's not one allowed
    // with CSS_PSEUDO_ELEMENT_IS_JS_CREATED_NAC.
    if (options.mPseudo.WasPassed()) {
      Maybe<PseudoStyleRequest> request =
          nsCSSPseudoElements::ParsePseudoElement(options.mPseudo.Value());
      if (!request || request->IsNotPseudo() ||
          !nsCSSPseudoElements::PseudoElementIsJSCreatedNAC(request->mType)) {
        rv.ThrowNotSupportedError("Invalid pseudo-element");
        return nullptr;
      }
      pseudoType = request->mType;
    }
  }

  RefPtr<Element> elem = CreateElem(needsLowercase ? lcTagName : aTagName,
                                    nullptr, mDefaultElementType, is);

  if (pseudoType != PseudoStyleType::NotPseudo) {
    elem->SetPseudoElementType(pseudoType);
  }

  return elem.forget();
}

already_AddRefed<Element> Document::CreateElementNS(
    const nsAString& aNamespaceURI, const nsAString& aQualifiedName,
    const ElementCreationOptionsOrString& aOptions, ErrorResult& rv) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  rv = nsContentUtils::GetNodeInfoFromQName(aNamespaceURI, aQualifiedName,
                                            mNodeInfoManager, ELEMENT_NODE,
                                            getter_AddRefs(nodeInfo));
  if (rv.Failed()) {
    return nullptr;
  }

  const nsString* is = nullptr;
  if (aOptions.IsElementCreationOptions()) {
    const ElementCreationOptions& options =
        aOptions.GetAsElementCreationOptions();
    if (options.mIs.WasPassed()) {
      is = &options.mIs.Value();
    }
  }

  nsCOMPtr<Element> element;
  rv = NS_NewElement(getter_AddRefs(element), nodeInfo.forget(),
                     NOT_FROM_PARSER, is);
  if (rv.Failed()) {
    return nullptr;
  }

  return element.forget();
}

already_AddRefed<Element> Document::CreateXULElement(
    const nsAString& aTagName, const ElementCreationOptionsOrString& aOptions,
    ErrorResult& aRv) {
  aRv = nsContentUtils::CheckQName(aTagName, false);
  if (aRv.Failed()) {
    return nullptr;
  }

  const nsString* is = nullptr;
  if (aOptions.IsElementCreationOptions()) {
    const ElementCreationOptions& options =
        aOptions.GetAsElementCreationOptions();
    if (options.mIs.WasPassed()) {
      is = &options.mIs.Value();
    }
  }

  RefPtr<Element> elem = CreateElem(aTagName, nullptr, kNameSpaceID_XUL, is);
  if (!elem) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }
  return elem.forget();
}

already_AddRefed<nsTextNode> Document::CreateEmptyTextNode() const {
  RefPtr<nsTextNode> text = new (mNodeInfoManager) nsTextNode(mNodeInfoManager);
  return text.forget();
}

already_AddRefed<nsTextNode> Document::CreateTextNode(
    const nsAString& aData) const {
  RefPtr<nsTextNode> text = new (mNodeInfoManager) nsTextNode(mNodeInfoManager);
  // Don't notify; this node is still being created.
  text->SetText(aData, false);
  return text.forget();
}

already_AddRefed<DocumentFragment> Document::CreateDocumentFragment() const {
  RefPtr<DocumentFragment> frag =
      new (mNodeInfoManager) DocumentFragment(mNodeInfoManager);
  return frag.forget();
}

// Unfortunately, bareword "Comment" is ambiguous with some Mac system headers.
already_AddRefed<dom::Comment> Document::CreateComment(
    const nsAString& aData) const {
  RefPtr<dom::Comment> comment =
      new (mNodeInfoManager) dom::Comment(mNodeInfoManager);

  // Don't notify; this node is still being created.
  comment->SetText(aData, false);
  return comment.forget();
}

already_AddRefed<CDATASection> Document::CreateCDATASection(
    const nsAString& aData, ErrorResult& rv) {
  if (IsHTMLDocument()) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }

  if (FindInReadable(u"]]>"_ns, aData)) {
    rv.Throw(NS_ERROR_DOM_INVALID_CHARACTER_ERR);
    return nullptr;
  }

  RefPtr<CDATASection> cdata =
      new (mNodeInfoManager) CDATASection(mNodeInfoManager);

  // Don't notify; this node is still being created.
  cdata->SetText(aData, false);

  return cdata.forget();
}

already_AddRefed<ProcessingInstruction> Document::CreateProcessingInstruction(
    const nsAString& aTarget, const nsAString& aData, ErrorResult& rv) const {
  nsresult res = nsContentUtils::CheckQName(aTarget, false);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  if (FindInReadable(u"?>"_ns, aData)) {
    rv.Throw(NS_ERROR_DOM_INVALID_CHARACTER_ERR);
    return nullptr;
  }

  RefPtr<ProcessingInstruction> pi =
      NS_NewXMLProcessingInstruction(mNodeInfoManager, aTarget, aData);

  return pi.forget();
}

already_AddRefed<Attr> Document::CreateAttribute(const nsAString& aName,
                                                 ErrorResult& rv) {
  if (!mNodeInfoManager) {
    rv.Throw(NS_ERROR_NOT_INITIALIZED);
    return nullptr;
  }

  nsresult res = nsContentUtils::CheckQName(aName, false);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  nsAutoString name;
  if (IsHTMLDocument()) {
    nsContentUtils::ASCIIToLower(aName, name);
  } else {
    name = aName;
  }

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  res = mNodeInfoManager->GetNodeInfo(name, nullptr, kNameSpaceID_None,
                                      ATTRIBUTE_NODE, getter_AddRefs(nodeInfo));
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  RefPtr<Attr> attribute =
      new (mNodeInfoManager) Attr(nullptr, nodeInfo.forget(), u""_ns);
  return attribute.forget();
}

already_AddRefed<Attr> Document::CreateAttributeNS(
    const nsAString& aNamespaceURI, const nsAString& aQualifiedName,
    ErrorResult& rv) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  rv = nsContentUtils::GetNodeInfoFromQName(aNamespaceURI, aQualifiedName,
                                            mNodeInfoManager, ATTRIBUTE_NODE,
                                            getter_AddRefs(nodeInfo));
  if (rv.Failed()) {
    return nullptr;
  }

  RefPtr<Attr> attribute =
      new (mNodeInfoManager) Attr(nullptr, nodeInfo.forget(), u""_ns);
  return attribute.forget();
}

void Document::ScheduleForPresAttrEvaluation(Element* aElement) {
  MOZ_ASSERT(aElement->IsInComposedDoc());
  DebugOnly<bool> inserted = mLazyPresElements.EnsureInserted(aElement);
  MOZ_ASSERT(inserted);
  if (aElement->HasServoData()) {
    // TODO(emilio): RESTYLE_SELF is too strong, there should be no need to
    // re-selector-match, but right now this is needed to pick up the new mapped
    // attributes. We need something like RESTYLE_STYLE_ATTRIBUTE but for mapped
    // attributes.
    nsLayoutUtils::PostRestyleEvent(aElement, RestyleHint::RESTYLE_SELF,
                                    nsChangeHint(0));
  } else if (auto* presContext = GetPresContext()) {
    presContext->RestyleManager()->IncrementUndisplayedRestyleGeneration();
  }
}

void Document::UnscheduleForPresAttrEvaluation(Element* aElement) {
  mLazyPresElements.Remove(aElement);
}

void Document::DoResolveScheduledPresAttrs() {
  MOZ_ASSERT(!mLazyPresElements.IsEmpty());
  for (Element* el : mLazyPresElements) {
    MOZ_ASSERT(el->IsInComposedDoc(),
               "Un-schedule when removing from the document");
    MOZ_ASSERT(el->IsPendingMappedAttributeEvaluation());
    if (auto* svg = SVGElement::FromNode(el)) {
      // SVG does its own (very similar) thing, for now at least.
      svg->UpdateMappedDeclarationBlock();
    } else {
      MappedDeclarationsBuilder builder(*el, *this,
                                        el->GetMappedAttributeStyle());
      auto function = el->GetAttributeMappingFunction();
      function(builder);
      el->SetMappedDeclarationBlock(builder.TakeDeclarationBlock());
    }
    MOZ_ASSERT(!el->IsPendingMappedAttributeEvaluation());
  }
  mLazyPresElements.Clear();
}

already_AddRefed<nsSimpleContentList> Document::BlockedNodesByClassifier()
    const {
  RefPtr<nsSimpleContentList> list = new nsSimpleContentList(nullptr);

  for (const nsWeakPtr& weakNode : mBlockedNodesByClassifier) {
    if (nsCOMPtr<nsIContent> node = do_QueryReferent(weakNode)) {
      // Consider only nodes to which we have managed to get strong references.
      // Coping with nullptrs since it's expected for nodes to disappear when
      // nobody else is referring to them.
      list->AppendElement(node);
    }
  }

  return list.forget();
}

void Document::GetSelectedStyleSheetSet(nsAString& aSheetSet) {
  aSheetSet.Truncate();

  // Look through our sheets, find the selected set title
  size_t count = SheetCount();
  nsAutoString title;
  for (size_t index = 0; index < count; index++) {
    StyleSheet* sheet = SheetAt(index);
    NS_ASSERTION(sheet, "Null sheet in sheet list!");

    if (sheet->Disabled()) {
      // Disabled sheets don't affect the currently selected set
      continue;
    }

    sheet->GetTitle(title);

    if (aSheetSet.IsEmpty()) {
      aSheetSet = title;
    } else if (!title.IsEmpty() && !aSheetSet.Equals(title)) {
      // Sheets from multiple sets enabled; return null string, per spec.
      SetDOMStringToNull(aSheetSet);
      return;
    }
  }
}

void Document::SetSelectedStyleSheetSet(const nsAString& aSheetSet) {
  if (DOMStringIsNull(aSheetSet)) {
    return;
  }

  // Must update mLastStyleSheetSet before doing anything else with stylesheets
  // or CSSLoaders.
  mLastStyleSheetSet = aSheetSet;
  EnableStyleSheetsForSetInternal(aSheetSet, true);
}

void Document::SetPreferredStyleSheetSet(const nsAString& aSheetSet) {
  mPreferredStyleSheetSet = aSheetSet;
  // Only mess with our stylesheets if we don't have a lastStyleSheetSet, per
  // spec.
  if (DOMStringIsNull(mLastStyleSheetSet)) {
    // Calling EnableStyleSheetsForSetInternal, not SetSelectedStyleSheetSet,
    // per spec.  The idea here is that we're changing our preferred set and
    // that shouldn't change the value of lastStyleSheetSet.  Also, we're
    // using the Internal version so we can update the CSSLoader and not have
    // to worry about null strings.
    EnableStyleSheetsForSetInternal(aSheetSet, true);
  }
}

DOMStringList* Document::StyleSheetSets() {
  if (!mStyleSheetSetList) {
    mStyleSheetSetList = new DOMStyleSheetSetList(this);
  }
  return mStyleSheetSetList;
}

void Document::EnableStyleSheetsForSet(const nsAString& aSheetSet) {
  // Per spec, passing in null is a no-op.
  if (!DOMStringIsNull(aSheetSet)) {
    // Note: must make sure to not change the CSSLoader's preferred sheet --
    // that value should be equal to either our lastStyleSheetSet (if that's
    // non-null) or to our preferredStyleSheetSet.  And this method doesn't
    // change either of those.
    EnableStyleSheetsForSetInternal(aSheetSet, false);
  }
}

void Document::EnableStyleSheetsForSetInternal(const nsAString& aSheetSet,
                                               bool aUpdateCSSLoader) {
  size_t count = SheetCount();
  nsAutoString title;
  for (size_t index = 0; index < count; index++) {
    StyleSheet* sheet = SheetAt(index);
    NS_ASSERTION(sheet, "Null sheet in sheet list!");

    sheet->GetTitle(title);
    if (!title.IsEmpty()) {
      sheet->SetEnabled(title.Equals(aSheetSet));
    }
  }
  if (aUpdateCSSLoader) {
    CSSLoader()->DocumentStyleSheetSetChanged();
  }
  if (EnsureStyleSet().StyleSheetsHaveChanged()) {
    ApplicableStylesChanged();
  }
}

void Document::GetCharacterSet(nsAString& aCharacterSet) const {
  nsAutoCString charset;
  GetDocumentCharacterSet()->Name(charset);
  CopyASCIItoUTF16(charset, aCharacterSet);
}

already_AddRefed<nsINode> Document::ImportNode(nsINode& aNode, bool aDeep,
                                               ErrorResult& rv) const {
  nsINode* imported = &aNode;

  switch (imported->NodeType()) {
    case DOCUMENT_NODE: {
      break;
    }
    case DOCUMENT_FRAGMENT_NODE:
    case ATTRIBUTE_NODE:
    case ELEMENT_NODE:
    case PROCESSING_INSTRUCTION_NODE:
    case TEXT_NODE:
    case CDATA_SECTION_NODE:
    case COMMENT_NODE:
    case DOCUMENT_TYPE_NODE: {
      return imported->Clone(aDeep, mNodeInfoManager, rv);
    }
    default: {
      NS_WARNING("Don't know how to clone this nodetype for importNode.");
    }
  }

  rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return nullptr;
}

already_AddRefed<nsRange> Document::CreateRange(ErrorResult& rv) {
  return nsRange::Create(this, 0, this, 0, rv);
}

already_AddRefed<NodeIterator> Document::CreateNodeIterator(
    nsINode& aRoot, uint32_t aWhatToShow, NodeFilter* aFilter,
    ErrorResult& rv) const {
  RefPtr<NodeIterator> iterator =
      new NodeIterator(&aRoot, aWhatToShow, aFilter);
  return iterator.forget();
}

already_AddRefed<TreeWalker> Document::CreateTreeWalker(nsINode& aRoot,
                                                        uint32_t aWhatToShow,
                                                        NodeFilter* aFilter,
                                                        ErrorResult& rv) const {
  RefPtr<TreeWalker> walker = new TreeWalker(&aRoot, aWhatToShow, aFilter);
  return walker.forget();
}

already_AddRefed<Location> Document::GetLocation() const {
  nsCOMPtr<nsPIDOMWindowInner> w = do_QueryInterface(mScriptGlobalObject);

  if (!w) {
    return nullptr;
  }

  return do_AddRef(w->Location());
}

already_AddRefed<nsIURI> Document::GetDomainURI() {
  nsIPrincipal* principal = NodePrincipal();

  nsCOMPtr<nsIURI> uri;
  principal->GetDomain(getter_AddRefs(uri));
  if (uri) {
    return uri.forget();
  }
  auto* basePrin = BasePrincipal::Cast(principal);
  basePrin->GetURI(getter_AddRefs(uri));
  return uri.forget();
}

void Document::GetDomain(nsAString& aDomain) {
  nsCOMPtr<nsIURI> uri = GetDomainURI();

  if (!uri) {
    aDomain.Truncate();
    return;
  }

  nsAutoCString hostName;
  nsresult rv = nsContentUtils::GetHostOrIPv6WithBrackets(uri, hostName);
  if (NS_SUCCEEDED(rv)) {
    CopyUTF8toUTF16(hostName, aDomain);
  } else {
    // If we can't get the host from the URI (e.g. about:, javascript:,
    // etc), just return an empty string.
    aDomain.Truncate();
  }
}

void Document::SetDomain(const nsAString& aDomain, ErrorResult& rv) {
  if (!GetBrowsingContext()) {
    // If our browsing context is null; disallow setting domain
    rv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (mSandboxFlags & SANDBOXED_DOMAIN) {
    // We're sandboxed; disallow setting domain
    rv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (!FeaturePolicyUtils::IsFeatureAllowed(this, u"document-domain"_ns)) {
    rv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (aDomain.IsEmpty()) {
    rv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  nsCOMPtr<nsIURI> uri = GetDomainURI();
  if (!uri) {
    rv.Throw(NS_ERROR_FAILURE);
    return;
  }

  // Check new domain - must be a superdomain of the current host
  // For example, a page from foo.bar.com may set domain to bar.com,
  // but not to ar.com, baz.com, or fi.foo.bar.com.

  nsCOMPtr<nsIURI> newURI = RegistrableDomainSuffixOfInternal(aDomain, uri);
  if (!newURI) {
    // Error: illegal domain
    rv.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return;
  }

  if (!GetDocGroup() || GetDocGroup()->IsOriginKeyed()) {
    WarnOnceAbout(Document::eDocumentSetDomainIgnored);
    return;
  }

  MOZ_ALWAYS_SUCCEEDS(NodePrincipal()->SetDomain(newURI));
  MOZ_ALWAYS_SUCCEEDS(PartitionedPrincipal()->SetDomain(newURI));
  if (WindowGlobalChild* wgc = GetWindowGlobalChild()) {
    wgc->SendSetDocumentDomain(WrapNotNull(newURI));
  }
}

already_AddRefed<nsIURI> Document::CreateInheritingURIForHost(
    const nsACString& aHostString) {
  if (aHostString.IsEmpty()) {
    return nullptr;
  }

  // Create new URI
  nsCOMPtr<nsIURI> uri = GetDomainURI();
  if (!uri) {
    return nullptr;
  }

  nsresult rv;
  rv = NS_MutateURI(uri)
           .SetUserPass(""_ns)
           .SetPort(-1)  // we want to reset the port number if needed.
           .SetHostPort(aHostString)
           .Finalize(uri);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  return uri.forget();
}

already_AddRefed<nsIURI> Document::RegistrableDomainSuffixOfInternal(
    const nsAString& aNewDomain, nsIURI* aOrigHost) {
  if (NS_WARN_IF(!aOrigHost)) {
    return nullptr;
  }

  nsCOMPtr<nsIURI> newURI =
      CreateInheritingURIForHost(NS_ConvertUTF16toUTF8(aNewDomain));
  if (!newURI) {
    // Error: failed to parse input domain
    return nullptr;
  }

  if (!IsValidDomain(aOrigHost, newURI)) {
    // Error: illegal domain
    return nullptr;
  }

  nsAutoCString domain;
  if (NS_FAILED(newURI->GetAsciiHost(domain))) {
    return nullptr;
  }

  return CreateInheritingURIForHost(domain);
}

/* static */
bool Document::IsValidDomain(nsIURI* aOrigHost, nsIURI* aNewURI) {
  // Check new domain - must be a superdomain of the current host
  // For example, a page from foo.bar.com may set domain to bar.com,
  // but not to ar.com, baz.com, or fi.foo.bar.com.
  nsAutoCString current;
  nsAutoCString domain;
  if (NS_FAILED(aOrigHost->GetAsciiHost(current))) {
    current.Truncate();
  }
  if (NS_FAILED(aNewURI->GetAsciiHost(domain))) {
    domain.Truncate();
  }

  bool ok = current.Equals(domain);
  if (current.Length() > domain.Length() && StringEndsWith(current, domain) &&
      current.CharAt(current.Length() - domain.Length() - 1) == '.') {
    // We're golden if the new domain is the current page's base domain or a
    // subdomain of it.
    nsCOMPtr<nsIEffectiveTLDService> tldService =
        mozilla::components::EffectiveTLD::Service();
    if (!tldService) {
      return false;
    }

    nsAutoCString currentBaseDomain;
    ok = NS_SUCCEEDED(
        tldService->GetBaseDomain(aOrigHost, 0, currentBaseDomain));
    NS_ASSERTION(StringEndsWith(domain, currentBaseDomain) ==
                     (domain.Length() >= currentBaseDomain.Length()),
                 "uh-oh!  slight optimization wasn't valid somehow!");
    ok = ok && domain.Length() >= currentBaseDomain.Length();
  }

  return ok;
}

Element* Document::GetHtmlElement() const {
  Element* rootElement = GetRootElement();
  if (rootElement && rootElement->IsHTMLElement(nsGkAtoms::html)) {
    return rootElement;
  }
  return nullptr;
}

Element* Document::GetHtmlChildElement(
    nsAtom* aTag, const nsIContent* aContentToIgnore) const {
  Element* html = GetHtmlElement();
  if (!html) {
    return nullptr;
  }

  // Look for the element with aTag inside html. This needs to run
  // forwards to find the first such element.
  for (nsIContent* child = html->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsHTMLElement(aTag) && MOZ_LIKELY(child != aContentToIgnore)) {
      return child->AsElement();
    }
  }
  return nullptr;
}

nsGenericHTMLElement* Document::GetBody() const {
  Element* html = GetHtmlElement();
  if (!html) {
    return nullptr;
  }

  for (nsIContent* child = html->GetFirstChild(); child;
       child = child->GetNextSibling()) {
    if (child->IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::frameset)) {
      return static_cast<nsGenericHTMLElement*>(child);
    }
  }

  return nullptr;
}

void Document::SetBody(nsGenericHTMLElement* newBody, ErrorResult& rv) {
  nsCOMPtr<Element> root = GetRootElement();

  // The body element must be either a body tag or a frameset tag. And we must
  // have a root element to be able to add kids to it.
  if (!newBody ||
      !newBody->IsAnyOfHTMLElements(nsGkAtoms::body, nsGkAtoms::frameset)) {
    rv.ThrowHierarchyRequestError(
        "The new body must be either a body tag or frameset tag.");
    return;
  }

  if (!root) {
    rv.ThrowHierarchyRequestError("No root element.");
    return;
  }

  // Use DOM methods so that we pass through the appropriate security checks.
  nsCOMPtr<Element> currentBody = GetBody();
  if (currentBody) {
    root->ReplaceChild(*newBody, *currentBody, rv);
  } else {
    root->AppendChild(*newBody, rv);
  }
}

HTMLSharedElement* Document::GetHead() const {
  return static_cast<HTMLSharedElement*>(GetHeadElement());
}

Element* Document::GetTitleElement() {
  // mMayHaveTitleElement will have been set to true if any HTML or SVG
  // <title> element has been bound to this document. So if it's false,
  // we know there is nothing to do here. This avoids us having to search
  // the whole DOM if someone calls document.title on a large document
  // without a title.
  if (!mMayHaveTitleElement) {
    return nullptr;
  }

  Element* root = GetRootElement();
  if (root && root->IsSVGElement(nsGkAtoms::svg)) {
    // In SVG, the document's title must be a child
    for (nsIContent* child = root->GetFirstChild(); child;
         child = child->GetNextSibling()) {
      if (child->IsSVGElement(nsGkAtoms::title)) {
        return child->AsElement();
      }
    }
    return nullptr;
  }

  // We check the HTML namespace even for non-HTML documents, except SVG.  This
  // matches the spec and the behavior of all tested browsers.
  for (nsINode* node = GetFirstChild(); node; node = node->GetNextNode(this)) {
    if (node->IsHTMLElement(nsGkAtoms::title)) {
      return node->AsElement();
    }
  }
  return nullptr;
}

void Document::GetTitle(nsAString& aTitle) {
  aTitle.Truncate();

  Element* rootElement = GetRootElement();
  if (!rootElement) {
    return;
  }

  if (rootElement->IsXULElement()) {
    rootElement->GetAttr(nsGkAtoms::title, aTitle);
  } else if (Element* title = GetTitleElement()) {
    nsContentUtils::GetNodeTextContent(title, false, aTitle);
  } else {
    return;
  }

  aTitle.CompressWhitespace();
}

void Document::SetTitle(const nsAString& aTitle, ErrorResult& aRv) {
  Element* rootElement = GetRootElement();
  if (!rootElement) {
    return;
  }

  if (rootElement->IsXULElement()) {
    aRv =
        rootElement->SetAttr(kNameSpaceID_None, nsGkAtoms::title, aTitle, true);
    return;
  }

  Maybe<mozAutoDocUpdate> updateBatch;
  nsCOMPtr<Element> title = GetTitleElement();
  if (rootElement->IsSVGElement(nsGkAtoms::svg)) {
    if (!title) {
      // Batch updates so that mutation events don't change "the title
      // element" under us
      updateBatch.emplace(this, true);
      RefPtr<mozilla::dom::NodeInfo> titleInfo = mNodeInfoManager->GetNodeInfo(
          nsGkAtoms::title, nullptr, kNameSpaceID_SVG, ELEMENT_NODE);
      NS_NewSVGElement(getter_AddRefs(title), titleInfo.forget(),
                       NOT_FROM_PARSER);
      if (!title) {
        return;
      }
      rootElement->InsertChildBefore(title, rootElement->GetFirstChild(), true,
                                     IgnoreErrors());
    }
  } else if (rootElement->IsHTMLElement()) {
    if (!title) {
      // Batch updates so that mutation events don't change "the title
      // element" under us
      updateBatch.emplace(this, true);
      Element* head = GetHeadElement();
      if (!head) {
        return;
      }

      RefPtr<mozilla::dom::NodeInfo> titleInfo;
      titleInfo = mNodeInfoManager->GetNodeInfo(
          nsGkAtoms::title, nullptr, kNameSpaceID_XHTML, ELEMENT_NODE);
      title = NS_NewHTMLTitleElement(titleInfo.forget());
      if (!title) {
        return;
      }

      head->AppendChildTo(title, true, IgnoreErrors());
    }
  } else {
    return;
  }

  aRv = nsContentUtils::SetNodeTextContent(title, aTitle, false);
}

class Document::TitleChangeEvent final : public Runnable {
 public:
  explicit TitleChangeEvent(Document* aDoc)
      : Runnable("Document::TitleChangeEvent"),
        mDoc(aDoc),
        mBlockOnload(aDoc->IsInChromeDocShell()) {
    if (mBlockOnload) {
      mDoc->BlockOnload();
    }
  }

  NS_IMETHOD Run() final {
    if (!mDoc) {
      return NS_OK;
    }
    const RefPtr<Document> doc = mDoc;
    const bool blockOnload = mBlockOnload;
    mDoc = nullptr;
    doc->DoNotifyPossibleTitleChange();
    if (blockOnload) {
      doc->UnblockOnload(/* aFireSync = */ true);
    }
    return NS_OK;
  }

  void Revoke() {
    if (mDoc) {
      if (mBlockOnload) {
        mDoc->UnblockOnload(/* aFireSync = */ false);
      }
      mDoc = nullptr;
    }
  }

 private:
  // Weak, caller is responsible for calling Revoke() when needed.
  Document* mDoc;
  // title changes should block the load event on chrome docshells, so that the
  // window title is consistently set by the time the top window is displayed.
  // Otherwise, some window manager integrations don't work properly,
  // see bug 1874766.
  const bool mBlockOnload = false;
};

void Document::NotifyPossibleTitleChange(bool aBoundTitleElement) {
  NS_ASSERTION(!mInUnlinkOrDeletion || !aBoundTitleElement,
               "Setting a title while unlinking or destroying the element?");
  if (mInUnlinkOrDeletion) {
    return;
  }

  if (aBoundTitleElement) {
    mMayHaveTitleElement = true;
  }

  if (mPendingTitleChangeEvent.IsPending()) {
    return;
  }

  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  RefPtr<TitleChangeEvent> event = new TitleChangeEvent(this);
  if (NS_WARN_IF(NS_FAILED(Dispatch(do_AddRef(event))))) {
    event->Revoke();
    return;
  }
  mPendingTitleChangeEvent = std::move(event);
}

void Document::DoNotifyPossibleTitleChange() {
  if (!mPendingTitleChangeEvent.IsPending()) {
    return;
  }
  // Make sure the pending runnable method is cleared.
  mPendingTitleChangeEvent.Revoke();
  mHaveFiredTitleChange = true;

  nsAutoString title;
  GetTitle(title);

  if (RefPtr<PresShell> presShell = GetPresShell()) {
    nsCOMPtr<nsISupports> container =
        presShell->GetPresContext()->GetContainerWeak();
    if (container) {
      if (nsCOMPtr<nsIBaseWindow> docShellWin = do_QueryInterface(container)) {
        docShellWin->SetTitle(title);
      }
    }
  }

  if (WindowGlobalChild* child = GetWindowGlobalChild()) {
    child->SendUpdateDocumentTitle(title);
  }

  // Fire a DOM event for the title change.
  nsContentUtils::DispatchChromeEvent(this, this, u"DOMTitleChanged"_ns,
                                      CanBubble::eYes, Cancelable::eYes);

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(ToSupports(this), "document-title-changed", nullptr);
  }
}

already_AddRefed<MediaQueryList> Document::MatchMedia(
    const nsACString& aMediaQueryList, CallerType aCallerType) {
  RefPtr<MediaQueryList> result =
      new MediaQueryList(this, aMediaQueryList, aCallerType);

  mDOMMediaQueryLists.insertBack(result);

  return result.forget();
}

void Document::SetMayStartLayout(bool aMayStartLayout) {
  mMayStartLayout = aMayStartLayout;
  if (MayStartLayout()) {
    // Before starting layout, check whether we're a toplevel chrome
    // window.  If we are, setup some state so that we don't have to restyle
    // the whole tree after StartLayout.
    if (nsCOMPtr<nsIAppWindow> win = GetAppWindowIfToplevelChrome()) {
      // We're the chrome document!
      win->BeforeStartLayout();
    }
    ReadyState state = GetReadyStateEnum();
    if (state >= READYSTATE_INTERACTIVE) {
      // DOMContentLoaded has fired already.
      MaybeResolveReadyForIdle();
    }
  }

  MaybeEditingStateChanged();
}

nsresult Document::InitializeFrameLoader(nsFrameLoader* aLoader) {
  mInitializableFrameLoaders.RemoveElement(aLoader);
  // Don't even try to initialize.
  if (mInDestructor) {
    NS_WARNING(
        "Trying to initialize a frame loader while"
        "document is being deleted");
    return NS_ERROR_FAILURE;
  }

  mInitializableFrameLoaders.AppendElement(aLoader);
  if (!mFrameLoaderRunner) {
    mFrameLoaderRunner =
        NewRunnableMethod("Document::MaybeInitializeFinalizeFrameLoaders", this,
                          &Document::MaybeInitializeFinalizeFrameLoaders);
    NS_ENSURE_TRUE(mFrameLoaderRunner, NS_ERROR_OUT_OF_MEMORY);
    nsContentUtils::AddScriptRunner(mFrameLoaderRunner);
  }
  return NS_OK;
}

nsresult Document::FinalizeFrameLoader(nsFrameLoader* aLoader,
                                       nsIRunnable* aFinalizer) {
  mInitializableFrameLoaders.RemoveElement(aLoader);
  if (mInDestructor) {
    return NS_ERROR_FAILURE;
  }

  LogRunnable::LogDispatch(aFinalizer);
  mFrameLoaderFinalizers.AppendElement(aFinalizer);
  if (!mFrameLoaderRunner) {
    mFrameLoaderRunner =
        NewRunnableMethod("Document::MaybeInitializeFinalizeFrameLoaders", this,
                          &Document::MaybeInitializeFinalizeFrameLoaders);
    NS_ENSURE_TRUE(mFrameLoaderRunner, NS_ERROR_OUT_OF_MEMORY);
    nsContentUtils::AddScriptRunner(mFrameLoaderRunner);
  }
  return NS_OK;
}

void Document::MaybeInitializeFinalizeFrameLoaders() {
  if (mDelayFrameLoaderInitialization) {
    // This method will be recalled when !mDelayFrameLoaderInitialization.
    mFrameLoaderRunner = nullptr;
    return;
  }

  // We're not in an update, but it is not safe to run scripts, so
  // postpone frameloader initialization and finalization.
  if (!nsContentUtils::IsSafeToRunScript()) {
    if (!mInDestructor && !mFrameLoaderRunner &&
        (mInitializableFrameLoaders.Length() ||
         mFrameLoaderFinalizers.Length())) {
      mFrameLoaderRunner = NewRunnableMethod(
          "Document::MaybeInitializeFinalizeFrameLoaders", this,
          &Document::MaybeInitializeFinalizeFrameLoaders);
      nsContentUtils::AddScriptRunner(mFrameLoaderRunner);
    }
    return;
  }
  mFrameLoaderRunner = nullptr;

  // Don't use a temporary array for mInitializableFrameLoaders, because
  // loading a frame may cause some other frameloader to be removed from the
  // array. But be careful to keep the loader alive when starting the load!
  while (mInitializableFrameLoaders.Length()) {
    RefPtr<nsFrameLoader> loader = mInitializableFrameLoaders[0];
    mInitializableFrameLoaders.RemoveElementAt(0);
    NS_ASSERTION(loader, "null frameloader in the array?");
    loader->ReallyStartLoading();
  }

  uint32_t length = mFrameLoaderFinalizers.Length();
  if (length > 0) {
    nsTArray<nsCOMPtr<nsIRunnable>> finalizers =
        std::move(mFrameLoaderFinalizers);
    for (uint32_t i = 0; i < length; ++i) {
      LogRunnable::Run run(finalizers[i]);
      finalizers[i]->Run();
    }
  }
}

void Document::TryCancelFrameLoaderInitialization(nsIDocShell* aShell) {
  uint32_t length = mInitializableFrameLoaders.Length();
  for (uint32_t i = 0; i < length; ++i) {
    if (mInitializableFrameLoaders[i]->GetExistingDocShell() == aShell) {
      mInitializableFrameLoaders.RemoveElementAt(i);
      return;
    }
  }
}

void Document::SetPrototypeDocument(nsXULPrototypeDocument* aPrototype) {
  mPrototypeDocument = aPrototype;
  mSynchronousDOMContentLoaded = true;
}

nsIPermissionDelegateHandler* Document::PermDelegateHandler() {
  return GetPermissionDelegateHandler();
}

Document* Document::RequestExternalResource(
    nsIURI* aURI, nsIReferrerInfo* aReferrerInfo, nsINode* aRequestingNode,
    ExternalResourceLoad** aPendingLoad) {
  MOZ_ASSERT(aURI, "Must have a URI");
  MOZ_ASSERT(aRequestingNode, "Must have a node");
  MOZ_ASSERT(aReferrerInfo, "Must have a referrerInfo");
  if (mDisplayDocument) {
    return mDisplayDocument->RequestExternalResource(
        aURI, aReferrerInfo, aRequestingNode, aPendingLoad);
  }

  return mExternalResourceMap.RequestResource(
      aURI, aReferrerInfo, aRequestingNode, this, aPendingLoad);
}

void Document::EnumerateExternalResources(SubDocEnumFunc aCallback) const {
  mExternalResourceMap.EnumerateResources(aCallback);
}

SMILAnimationController* Document::GetAnimationController() {
  // We create the animation controller lazily because most documents won't want
  // one and only SVG documents and the like will call this
  if (mAnimationController) return mAnimationController;
  // Refuse to create an Animation Controller for data documents.
  if (mLoadedAsData) return nullptr;

  mAnimationController = new SMILAnimationController(this);

  // If there's a presContext then check the animation mode and pause if
  // necessary.
  nsPresContext* context = GetPresContext();
  if (mAnimationController && context &&
      context->ImageAnimationMode() == imgIContainer::kDontAnimMode) {
    mAnimationController->Pause(SMILTimeContainer::PAUSE_USERPREF);
  }

  // If we're hidden (or being hidden), notify the newly-created animation
  // controller. (Skip this check for SVG-as-an-image documents, though,
  // because they don't get OnPageShow / OnPageHide calls).
  if (!mIsShowing && !mIsBeingUsedAsImage) {
    mAnimationController->OnPageHide();
  }

  return mAnimationController;
}

ScrollTimelineAnimationTracker*
Document::GetOrCreateScrollTimelineAnimationTracker() {
  if (!mScrollTimelineAnimationTracker) {
    mScrollTimelineAnimationTracker = new ScrollTimelineAnimationTracker(this);
  }

  return mScrollTimelineAnimationTracker;
}

/**
 * Retrieve the "direction" property of the document.
 *
 * @lina 01/09/2001
 */
void Document::GetDir(nsAString& aDirection) const {
  aDirection.Truncate();
  Element* rootElement = GetHtmlElement();
  if (rootElement) {
    static_cast<nsGenericHTMLElement*>(rootElement)->GetDir(aDirection);
  }
}

/**
 * Set the "direction" property of the document.
 *
 * @lina 01/09/2001
 */
void Document::SetDir(const nsAString& aDirection) {
  Element* rootElement = GetHtmlElement();
  if (rootElement) {
    rootElement->SetAttr(kNameSpaceID_None, nsGkAtoms::dir, aDirection, true);
  }
}

nsIHTMLCollection* Document::Images() {
  if (!mImages) {
    mImages = new nsContentList(this, kNameSpaceID_XHTML, nsGkAtoms::img,
                                nsGkAtoms::img);
  }
  return mImages;
}

nsIHTMLCollection* Document::Embeds() {
  if (!mEmbeds) {
    mEmbeds = new nsContentList(this, kNameSpaceID_XHTML, nsGkAtoms::embed,
                                nsGkAtoms::embed);
  }
  return mEmbeds;
}

static bool MatchLinks(Element* aElement, int32_t aNamespaceID, nsAtom* aAtom,
                       void* aData) {
  return aElement->IsAnyOfHTMLElements(nsGkAtoms::a, nsGkAtoms::area) &&
         aElement->HasAttr(nsGkAtoms::href);
}

nsIHTMLCollection* Document::Links() {
  if (!mLinks) {
    mLinks = new nsContentList(this, MatchLinks, nullptr, nullptr);
  }
  return mLinks;
}

nsIHTMLCollection* Document::Forms() {
  if (!mForms) {
    // Please keep this in sync with nsHTMLDocument::GetFormsAndFormControls.
    mForms = new nsContentList(this, kNameSpaceID_XHTML, nsGkAtoms::form,
                               nsGkAtoms::form);
  }

  return mForms;
}

nsIHTMLCollection* Document::Scripts() {
  if (!mScripts) {
    mScripts = new nsContentList(this, kNameSpaceID_XHTML, nsGkAtoms::script,
                                 nsGkAtoms::script);
  }
  return mScripts;
}

nsIHTMLCollection* Document::Applets() {
  if (!mApplets) {
    mApplets = new nsEmptyContentList(this);
  }
  return mApplets;
}

static bool MatchAnchors(Element* aElement, int32_t aNamespaceID, nsAtom* aAtom,
                         void* aData) {
  return aElement->IsHTMLElement(nsGkAtoms::a) &&
         aElement->HasAttr(nsGkAtoms::name);
}

nsIHTMLCollection* Document::Anchors() {
  if (!mAnchors) {
    mAnchors = new nsContentList(this, MatchAnchors, nullptr, nullptr);
  }
  return mAnchors;
}

mozilla::dom::Nullable<mozilla::dom::WindowProxyHolder> Document::Open(
    const nsACString& aURL, const nsAString& aName, const nsAString& aFeatures,
    ErrorResult& rv) {
  MOZ_ASSERT(nsContentUtils::CanCallerAccess(this),
             "XOW should have caught this!");

  nsCOMPtr<nsPIDOMWindowInner> window = GetInnerWindow();
  if (!window) {
    rv.Throw(NS_ERROR_DOM_INVALID_ACCESS_ERR);
    return nullptr;
  }
  nsCOMPtr<nsPIDOMWindowOuter> outer =
      nsPIDOMWindowOuter::GetFromCurrentInner(window);
  if (!outer) {
    rv.Throw(NS_ERROR_NOT_INITIALIZED);
    return nullptr;
  }
  RefPtr<nsGlobalWindowOuter> win = nsGlobalWindowOuter::Cast(outer);
  RefPtr<BrowsingContext> newBC;
  rv = win->OpenJS(aURL, aName, aFeatures, getter_AddRefs(newBC));
  if (!newBC) {
    return nullptr;
  }
  return WindowProxyHolder(std::move(newBC));
}

Document* Document::Open(const Optional<nsAString>& /* unused */,
                         const Optional<nsAString>& /* unused */,
                         ErrorResult& aError) {
  // Implements
  // <https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#document-open-steps>

  MOZ_ASSERT(nsContentUtils::CanCallerAccess(this),
             "XOW should have caught this!");

  // Step 1 -- throw if we're an XML document.
  if (!IsHTMLDocument() || mDisableDocWrite) {
    aError.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  // Step 2 -- throw if dynamic markup insertion should throw.
  if (ShouldThrowOnDynamicMarkupInsertion()) {
    aError.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return nullptr;
  }

  // Step 3 -- get the entry document, so we can use it for security checks.
  nsCOMPtr<Document> callerDoc = GetEntryDocument();
  if (!callerDoc) {
    if (nsIGlobalObject* callerGlobal = GetEntryGlobal()) {
      if (callerGlobal->IsXPCSandbox()) {
        if (nsIPrincipal* principal = callerGlobal->PrincipalOrNull()) {
          if (principal->Equals(NodePrincipal())) {
            // In case we're being called from some JS sandbox scope,
            // pretend that the caller is the document itself.
            callerDoc = this;
          }
        }
      }
    }

    if (!callerDoc) {
      // If we're called from C++ or in some other way without an originating
      // document we can't do a document.open w/o changing the principal of the
      // document to something like about:blank (as that's the only sane thing
      // to do when we don't know the origin of this call), and since we can't
      // change the principals of a document for security reasons we'll have to
      // refuse to go ahead with this call.

      aError.Throw(NS_ERROR_DOM_SECURITY_ERR);
      return nullptr;
    }
  }

  // Step 4 -- make sure we're same-origin (not just same origin-domain) with
  // the entry document.
  if (!callerDoc->NodePrincipal()->Equals(NodePrincipal())) {
    aError.Throw(NS_ERROR_DOM_SECURITY_ERR);
    return nullptr;
  }

  // Step 5 -- if we have an active parser with a nonzero script nesting level,
  // just no-op.
  if ((mParser && mParser->HasNonzeroScriptNestingLevel()) || mParserAborted) {
    return this;
  }

  // Step 6 -- check for open() during unload.  Per spec, this is just a check
  // of the ignore-opens-during-unload counter, but our unload event code
  // doesn't affect that counter yet (unlike pagehide and beforeunload, which
  // do), so we check for unload directly.
  if (ShouldIgnoreOpens()) {
    return this;
  }

  RefPtr<nsDocShell> shell(mDocumentContainer);
  if (shell) {
    bool inUnload;
    shell->GetIsInUnload(&inUnload);
    if (inUnload) {
      return this;
    }
  }

  // At this point we know this is a valid-enough document.open() call
  // and not a no-op.  Increment our use counter.
  SetUseCounter(eUseCounter_custom_DocumentOpen);

  // XXX The spec has changed. There is a new step 7 and step 8 has changed
  // a bit.

  // Step 8 -- stop existing navigation of our browsing context (and all other
  // loads it's doing) if we're the active document of our browsing context.
  // Note that we do not want to stop anything if there is no existing
  // navigation.
  if (shell && IsCurrentActiveDocument() &&
      shell->GetIsAttemptingToNavigate()) {
    shell->Stop(nsIWebNavigation::STOP_NETWORK);

    // The Stop call may have cancelled the onload blocker request or
    // prevented it from getting added, so we need to make sure it gets added
    // to the document again otherwise the document could have a non-zero
    // onload block count without the onload blocker request being in the
    // loadgroup.
    EnsureOnloadBlocker();
  }

  // Step 9 -- clear event listeners out of our DOM tree
  for (nsINode* node : ShadowIncludingTreeIterator(*this)) {
    if (EventListenerManager* elm = node->GetExistingListenerManager()) {
      elm->RemoveAllListeners();
    }
  }

  // Step 10 -- clear event listeners from our window, if we have one.
  //
  // Note that we explicitly want the inner window, and only if we're its
  // document.  We want to do this (per spec) even when we're not the "active
  // document", so we can't go through GetWindow(), because it might forward to
  // the wrong inner.
  if (nsPIDOMWindowInner* win = GetInnerWindow()) {
    if (win->GetExtantDoc() == this) {
      if (EventListenerManager* elm =
              nsGlobalWindowInner::Cast(win)->GetExistingListenerManager()) {
        elm->RemoveAllListeners();
      }
    }
  }

  // If we have a parser that has a zero script nesting level, we need to
  // properly terminate it.  We do that after we've removed all the event
  // listeners (so termination won't trigger event listeners if it does
  // something to the DOM), but before we remove all elements from the document
  // (so if termination does modify the DOM in some way we will just blow it
  // away immediately.  See the similar code in WriteCommon that handles the
  // !IsInsertionPointDefined() case and should stay in sync with this code.
  if (mParser) {
    MOZ_ASSERT(!mParser->HasNonzeroScriptNestingLevel(),
               "Why didn't we take the early return?");
    // Make sure we don't re-enter.
    IgnoreOpensDuringUnload ignoreOpenGuard(this);
    mParser->Terminate();
    MOZ_RELEASE_ASSERT(!mParser, "mParser should have been null'd out");
  }

  // Steps 11, 12, 13, 14 --
  // remove all our DOM kids without firing any mutation events.
  {
    bool oldFlag = FireMutationEvents();
    SetFireMutationEvents(false);

    // We want to ignore any recursive calls to Open() that happen while
    // disconnecting the node tree.  The spec doesn't say to do this, but the
    // spec also doesn't envision unload events on subframes firing while we do
    // this, while all browsers fire them in practice.  See
    // <https://github.com/whatwg/html/issues/4611>.
    IgnoreOpensDuringUnload ignoreOpenGuard(this);
    DisconnectNodeTree();
    SetFireMutationEvents(oldFlag);
  }

  // Step 15 -- if we're the current document in our docshell, do the
  // equivalent of pushState() with the new URL we should have.
  if (shell && IsCurrentActiveDocument()) {
    nsCOMPtr<nsIURI> newURI = callerDoc->GetDocumentURI();
    if (callerDoc != this) {
      nsCOMPtr<nsIURI> noFragmentURI;
      nsresult rv = NS_GetURIWithoutRef(newURI, getter_AddRefs(noFragmentURI));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        aError.Throw(rv);
        return nullptr;
      }
      newURI = std::move(noFragmentURI);
    }

    // UpdateURLAndHistory might do various member-setting, so make sure we're
    // holding strong refs to all the refcounted args on the stack.  We can
    // assume that our caller is holding on to "this" already.
    nsCOMPtr<nsIURI> currentURI = GetDocumentURI();
    bool equalURIs;
    nsresult rv = currentURI->Equals(newURI, &equalURIs);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aError.Throw(rv);
      return nullptr;
    }
    nsCOMPtr<nsIStructuredCloneContainer> stateContainer(mStateObjectContainer);
    rv = shell->UpdateURLAndHistory(this, newURI, stateContainer,
                                    NavigationHistoryBehavior::Replace,
                                    currentURI, equalURIs);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aError.Throw(rv);
      return nullptr;
    }

    // And use the security info of the caller document as well, since
    // it's the thing providing our data.
    mSecurityInfo = callerDoc->GetSecurityInfo();

    // Step 16
    // See <https://github.com/whatwg/html/issues/4299>.  Since our
    // URL may be changing away from about:blank here, we really want to unset
    // this flag no matter what, since only about:blank can be an initial
    // document.
    SetIsInitialDocument(false);

    // And let our docloader know that it will need to track our load event.
    nsDocShell::Cast(shell)->SetDocumentOpenedButNotLoaded();
  }

  // Per spec nothing happens with our URI in other cases, though note
  // <https://github.com/whatwg/html/issues/4286>.

  // Note that we don't need to do anything here with base URIs per spec.
  // That said, this might be assuming that we implement
  // https://html.spec.whatwg.org/multipage/urls-and-fetching.html#fallback-base-url
  // correctly, which we don't right now for the about:blank case.

  // Step 17, but note <https://github.com/whatwg/html/issues/4292>.
  mSkipLoadEventAfterClose = mLoadEventFiring;

  // Preliminary to steps 18-21.  Set our ready state to uninitialized before
  // we do anything else, so we can then proceed to later ready state levels.
  SetReadyStateInternal(READYSTATE_UNINITIALIZED,
                        /* updateTimingInformation = */ false);
  // Reset a flag that affects readyState behavior.
  mSetCompleteAfterDOMContentLoaded = false;

  // Step 18 -- set our compat mode to standards.
  SetCompatibilityMode(eCompatibility_FullStandards);

  // Step 19 -- create a new parser associated with document.  This also does
  // step 20 implicitly.
  mParserAborted = false;
  RefPtr<nsHtml5Parser> parser = nsHtml5Module::NewHtml5Parser();
  mParser = parser;
  parser->Initialize(this, GetDocumentURI(), ToSupports(shell), nullptr);
  nsresult rv = parser->StartExecutor();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aError.Throw(rv);
    return nullptr;
  }

  // Clear out our form control state, because the state of controls
  // in the pre-open() document should not affect the state of
  // controls that are now going to be written.
  mLayoutHistoryState = nullptr;

  if (shell) {
    // Prepare the docshell and the document viewer for the impending
    // out-of-band document.write()
    shell->PrepareForNewContentModel();

    nsCOMPtr<nsIDocumentViewer> viewer;
    shell->GetDocViewer(getter_AddRefs(viewer));
    if (viewer) {
      viewer->LoadStart(this);
    }
  }

  // Step 21.
  SetReadyStateInternal(Document::READYSTATE_LOADING,
                        /* updateTimingInformation = */ false);

  // Step 22.
  return this;
}

void Document::Close(ErrorResult& rv) {
  if (!IsHTMLDocument()) {
    // No calling document.close() on XHTML!

    rv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (ShouldThrowOnDynamicMarkupInsertion()) {
    rv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (!mParser || !mParser->IsScriptCreated()) {
    return;
  }

  ++mWriteLevel;
  rv = (static_cast<nsHtml5Parser*>(mParser.get()))
           ->Parse(u""_ns, nullptr, true);
  --mWriteLevel;
}

void Document::WriteCommon(const Sequence<OwningTrustedHTMLOrString>& aText,
                           bool aNewlineTerminate, mozilla::ErrorResult& rv) {
  bool isTrusted = true;
  auto getAsString =
      [&isTrusted](const OwningTrustedHTMLOrString& aTrustedHTMLOrString) {
        if (aTrustedHTMLOrString.IsString()) {
          isTrusted = false;
          return &aTrustedHTMLOrString.GetAsString();
        }
        return &aTrustedHTMLOrString.GetAsTrustedHTML()->mData;
      };

  // Fast path the common case
  if (aText.Length() == 1) {
    WriteCommon(*getAsString(aText[0]), aNewlineTerminate,
                aText[0].IsTrustedHTML(), rv);
  } else {
    // XXXbz it would be nice if we could pass all the strings to the parser
    // without having to do all this copying and then ask it to start
    // parsing....
    nsString text;
    for (size_t i = 0; i < aText.Length(); ++i) {
      text.Append(*getAsString(aText[i]));
    }
    WriteCommon(text, aNewlineTerminate, isTrusted, rv);
  }
}

void Document::WriteCommon(const nsAString& aText, bool aNewlineTerminate,
                           bool aIsTrusted, ErrorResult& aRv) {
#ifdef DEBUG
  {
    // Assert that we do not use or accidentally introduce doc.write()
    // in system privileged context or in any of our about: pages.
    nsCOMPtr<nsIPrincipal> principal = NodePrincipal();
    bool isAboutOrPrivContext = principal->IsSystemPrincipal();
    if (!isAboutOrPrivContext) {
      if (principal->SchemeIs("about")) {
        // about:blank inherits the security contetext and this assertion
        // is only meant for actual about: pages.
        nsAutoCString host;
        principal->GetHost(host);
        isAboutOrPrivContext = !host.EqualsLiteral("blank");
      }
    }
    // Some automated tests use an empty string to kick off some parsing
    // mechansims, but they do not do any harm since they use an empty string.
    MOZ_ASSERT(!isAboutOrPrivContext || aText.IsEmpty(),
               "do not use doc.write in privileged context!");
  }
#endif

  mTooDeepWriteRecursion =
      (mWriteLevel > NS_MAX_DOCUMENT_WRITE_DEPTH || mTooDeepWriteRecursion);
  if (NS_WARN_IF(mTooDeepWriteRecursion)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString = &aText;
  if (!aIsTrusted) {
    constexpr nsLiteralString sinkWrite = u"Document write"_ns;
    constexpr nsLiteralString sinkWriteLn = u"Document writeln"_ns;
    compliantString =
        TrustedTypeUtils::GetTrustedTypesCompliantStringForTrustedHTML(
            aText, aNewlineTerminate ? sinkWriteLn : sinkWrite,
            kTrustedTypesOnlySinkGroup, *this, compliantStringHolder, aRv);
    if (aRv.Failed()) {
      return;
    }
  }

  if (!IsHTMLDocument() || mDisableDocWrite) {
    // No calling document.write*() on XHTML!

    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (ShouldThrowOnDynamicMarkupInsertion()) {
    aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  if (mParserAborted) {
    // Hixie says aborting the parser doesn't undefine the insertion point.
    // However, since we null out mParser in that case, we track the
    // theoretically defined insertion point using mParserAborted.
    return;
  }

  // Implement Step 4.1 of:
  // https://html.spec.whatwg.org/multipage/dynamic-markup-insertion.html#document-write-steps
  if (ShouldIgnoreOpens()) {
    return;
  }

  void* key = GenerateParserKey();
  if (mParser && !mParser->IsInsertionPointDefined()) {
    if (mIgnoreDestructiveWritesCounter) {
      // Instead of implying a call to document.open(), ignore the call.
      nsContentUtils::ReportToConsole(
          nsIScriptError::warningFlag, "DOM Events"_ns, this,
          nsContentUtils::eDOM_PROPERTIES, "DocumentWriteIgnored");
      return;
    }
    // The spec doesn't tell us to ignore opens from here, but we need to
    // ensure opens are ignored here.  See similar code in Open() that handles
    // the case of an existing parser which is not currently running script and
    // should stay in sync with this code.
    IgnoreOpensDuringUnload ignoreOpenGuard(this);
    mParser->Terminate();
    MOZ_RELEASE_ASSERT(!mParser, "mParser should have been null'd out");
  }

  if (!mParser) {
    if (mIgnoreDestructiveWritesCounter) {
      // Instead of implying a call to document.open(), ignore the call.
      nsContentUtils::ReportToConsole(
          nsIScriptError::warningFlag, "DOM Events"_ns, this,
          nsContentUtils::eDOM_PROPERTIES, "DocumentWriteIgnored");
      return;
    }

    Open({}, {}, aRv);

    // If Open() fails, or if it didn't create a parser (as it won't
    // if the user chose to not discard the current document through
    // onbeforeunload), don't write anything.
    if (aRv.Failed() || !mParser) {
      return;
    }
  }

  static constexpr auto new_line = u"\n"_ns;

  ++mWriteLevel;

  // This could be done with less code, but for performance reasons it
  // makes sense to have the code for two separate Parse() calls here
  // since the concatenation of strings costs more than we like. And
  // why pay that price when we don't need to?
  if (aNewlineTerminate) {
    aRv = (static_cast<nsHtml5Parser*>(mParser.get()))
              ->Parse(*compliantString + new_line, key, false);
  } else {
    aRv = (static_cast<nsHtml5Parser*>(mParser.get()))
              ->Parse(*compliantString, key, false);
  };

  --mWriteLevel;

  mTooDeepWriteRecursion = (mWriteLevel != 0 && mTooDeepWriteRecursion);
}

void Document::Write(const Sequence<OwningTrustedHTMLOrString>& aText,
                     ErrorResult& rv) {
  WriteCommon(aText, false, rv);
}

void Document::Writeln(const Sequence<OwningTrustedHTMLOrString>& aText,
                       ErrorResult& rv) {
  WriteCommon(aText, true, rv);
}

void* Document::GenerateParserKey(void) {
  if (!mScriptLoader) {
    // If we don't have a script loader, then the parser probably isn't parsing
    // anything anyway, so just return null.
    return nullptr;
  }

  // The script loader provides us with the currently executing script element,
  // which is guaranteed to be unique per script.
  nsIScriptElement* script = mScriptLoader->GetCurrentParserInsertedScript();
  if (script && mParser && mParser->IsScriptCreated()) {
    nsCOMPtr<nsIParser> creatorParser = script->GetCreatorParser();
    if (creatorParser != mParser) {
      // Make scripts that aren't inserted by the active parser of this document
      // participate in the context of the script that document.open()ed
      // this document.
      return nullptr;
    }
  }
  return script;
}

/* static */
bool Document::MatchNameAttribute(Element* aElement, int32_t aNamespaceID,
                                  nsAtom* aAtom, void* aData) {
  MOZ_ASSERT(aElement, "Must have element to work with!");

  if (!aElement->HasName()) {
    return false;
  }

  nsString* elementName = static_cast<nsString*>(aData);
  return aElement->GetNameSpaceID() == kNameSpaceID_XHTML &&
         aElement->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name, *elementName,
                               eCaseMatters);
}

/* static */
void* Document::UseExistingNameString(nsINode* aRootNode,
                                      const nsString* aName) {
  return const_cast<nsString*>(aName);
}

nsresult Document::GetDocumentURI(nsString& aDocumentURI) const {
  if (mDocumentURI) {
    nsAutoCString uri;
    nsresult rv = mDocumentURI->GetSpec(uri);
    NS_ENSURE_SUCCESS(rv, rv);

    CopyUTF8toUTF16(uri, aDocumentURI);
  } else {
    aDocumentURI.Truncate();
  }

  return NS_OK;
}

// Alias of above
nsresult Document::GetURL(nsString& aURL) const { return GetDocumentURI(aURL); }

void Document::GetDocumentURIFromJS(nsString& aDocumentURI,
                                    CallerType aCallerType,
                                    ErrorResult& aRv) const {
  if (!mChromeXHRDocURI || aCallerType != CallerType::System) {
    aRv = GetDocumentURI(aDocumentURI);
    return;
  }

  nsAutoCString uri;
  nsresult res = mChromeXHRDocURI->GetSpec(uri);
  if (NS_FAILED(res)) {
    aRv.Throw(res);
    return;
  }
  CopyUTF8toUTF16(uri, aDocumentURI);
}

nsIURI* Document::GetDocumentURIObject() const {
  if (!mChromeXHRDocURI) {
    return GetDocumentURI();
  }

  return mChromeXHRDocURI;
}

void Document::GetCompatMode(nsString& aCompatMode) const {
  NS_ASSERTION(mCompatMode == eCompatibility_NavQuirks ||
                   mCompatMode == eCompatibility_AlmostStandards ||
                   mCompatMode == eCompatibility_FullStandards,
               "mCompatMode is neither quirks nor strict for this document");

  if (mCompatMode == eCompatibility_NavQuirks) {
    aCompatMode.AssignLiteral("BackCompat");
  } else {
    aCompatMode.AssignLiteral("CSS1Compat");
  }
}

}  // namespace dom
}  // namespace mozilla

void nsDOMAttributeMap::BlastSubtreeToPieces(nsINode* aNode) {
  if (Element* element = Element::FromNode(aNode)) {
    if (const nsDOMAttributeMap* map = element->GetAttributeMap()) {
      while (true) {
        RefPtr<Attr> attr;
        {
          // Use an iterator to get an arbitrary attribute from the
          // cache. The iterator must be destroyed before any other
          // operations on mAttributeCache, to avoid hash table
          // assertions.
          auto iter = map->mAttributeCache.ConstIter();
          if (iter.Done()) {
            break;
          }
          attr = iter.UserData();
        }

        BlastSubtreeToPieces(attr);

        mozilla::DebugOnly<nsresult> rv =
            element->UnsetAttr(attr->NodeInfo()->NamespaceID(),
                               attr->NodeInfo()->NameAtom(), false);

        // XXX Should we abort here?
        NS_ASSERTION(NS_SUCCEEDED(rv), "Uh-oh, UnsetAttr shouldn't fail!");
      }
    }

    if (mozilla::dom::ShadowRoot* shadow = element->GetShadowRoot()) {
      BlastSubtreeToPieces(shadow);
      element->UnattachShadow();
    }
  }

  while (aNode->HasChildren()) {
    nsIContent* node = aNode->GetFirstChild();
    BlastSubtreeToPieces(node);
    aNode->RemoveChildNode(node, false);
  }
}

namespace mozilla::dom {

nsINode* Document::AdoptNode(nsINode& aAdoptedNode, ErrorResult& rv,
                             bool aAcceptShadowRoot) {
  OwningNonNull<nsINode> adoptedNode = aAdoptedNode;
  if (adoptedNode->IsShadowRoot() && !aAcceptShadowRoot) {
    rv.ThrowHierarchyRequestError("The adopted node is a shadow root.");
    return nullptr;
  }

  // Scope firing mutation events so that we don't carry any state that
  // might be stale
  {
    if (nsCOMPtr<nsINode> parent = adoptedNode->GetParentNode()) {
      nsContentUtils::MaybeFireNodeRemoved(adoptedNode, parent);
    }
  }

  nsAutoScriptBlocker scriptBlocker;

  switch (adoptedNode->NodeType()) {
    case ATTRIBUTE_NODE: {
      // Remove from ownerElement.
      OwningNonNull<Attr> adoptedAttr = static_cast<Attr&>(*adoptedNode);

      nsCOMPtr<Element> ownerElement = adoptedAttr->GetOwnerElement();
      if (rv.Failed()) {
        return nullptr;
      }

      if (ownerElement) {
        OwningNonNull<Attr> newAttr =
            ownerElement->RemoveAttributeNode(*adoptedAttr, rv);
        if (rv.Failed()) {
          return nullptr;
        }
      }

      break;
    }
    case DOCUMENT_FRAGMENT_NODE:
    case ELEMENT_NODE:
    case PROCESSING_INSTRUCTION_NODE:
    case TEXT_NODE:
    case CDATA_SECTION_NODE:
    case COMMENT_NODE:
    case DOCUMENT_TYPE_NODE: {
      // Don't allow adopting a node's anonymous subtree out from under it.
      if (adoptedNode->IsRootOfNativeAnonymousSubtree()) {
        rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
        return nullptr;
      }

      // We don't want to adopt an element into its own contentDocument or into
      // a descendant contentDocument, so we check if the frameElement of this
      // document or any of its parents is the adopted node or one of its
      // descendants.
      RefPtr<BrowsingContext> bc = GetBrowsingContext();
      while (bc) {
        nsCOMPtr<nsINode> node = bc->GetEmbedderElement();
        if (node && node->IsInclusiveDescendantOf(adoptedNode)) {
          rv.ThrowHierarchyRequestError(
              "Trying to adopt a node into its own contentDocument or a "
              "descendant contentDocument.");
          return nullptr;
        }

        if (XRE_IsParentProcess()) {
          bc = bc->Canonical()->GetParentCrossChromeBoundary();
        } else {
          bc = bc->GetParent();
        }
      }

      // Remove from parent.
      nsCOMPtr<nsINode> parent = adoptedNode->GetParentNode();
      if (parent) {
        parent->RemoveChildNode(adoptedNode->AsContent(), true);
      } else {
        MOZ_ASSERT(!adoptedNode->IsInUncomposedDoc());
      }

      break;
    }
    case DOCUMENT_NODE: {
      rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return nullptr;
    }
    default: {
      NS_WARNING("Don't know how to adopt this nodetype for adoptNode.");

      rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return nullptr;
    }
  }

  nsCOMPtr<Document> oldDocument = adoptedNode->OwnerDoc();
  bool sameDocument = oldDocument == this;

  AutoJSContext cx;
  JS::Rooted<JSObject*> newScope(cx, nullptr);
  if (!sameDocument) {
    newScope = GetWrapper();
    if (!newScope && GetScopeObject() && GetScopeObject()->HasJSGlobal()) {
      // Make sure cx is in a semi-sane compartment before we call WrapNative.
      // It's kind of irrelevant, given that we're passing aAllowWrapping =
      // false, and documents should always insist on being wrapped in an
      // canonical scope. But we try to pass something sane anyway.
      JSObject* globalObject = GetScopeObject()->GetGlobalJSObject();
      JSAutoRealm ar(cx, globalObject);
      JS::Rooted<JS::Value> v(cx);
      rv = nsContentUtils::WrapNative(cx, ToSupports(this), this, &v,
                                      /* aAllowWrapping = */ false);
      if (rv.Failed()) return nullptr;
      newScope = &v.toObject();
    }
  }

  adoptedNode->Adopt(sameDocument ? nullptr : mNodeInfoManager, newScope, rv);
  if (rv.Failed()) {
    // Disconnect all nodes from their parents, since some have the old document
    // as their ownerDocument and some have this as their ownerDocument.
    nsDOMAttributeMap::BlastSubtreeToPieces(adoptedNode);
    return nullptr;
  }

  MOZ_ASSERT(adoptedNode->OwnerDoc() == this,
             "Should still be in the document we just got adopted into");

  return adoptedNode;
}

bool Document::UseWidthDeviceWidthFallbackViewport() const { return false; }

static Maybe<LayoutDeviceToScreenScale> ParseScaleString(
    const nsString& aScaleString) {
  // https://drafts.csswg.org/css-device-adapt/#min-scale-max-scale
  if (aScaleString.EqualsLiteral("device-width") ||
      aScaleString.EqualsLiteral("device-height")) {
    return Some(LayoutDeviceToScreenScale(10.0f));
  } else if (aScaleString.EqualsLiteral("yes")) {
    return Some(LayoutDeviceToScreenScale(1.0f));
  } else if (aScaleString.EqualsLiteral("no")) {
    return Some(LayoutDeviceToScreenScale(ViewportMinScale()));
  } else if (aScaleString.IsEmpty()) {
    return Nothing();
  }

  nsresult scaleErrorCode;
  float scale = aScaleString.ToFloatAllowTrailingChars(&scaleErrorCode);
  if (NS_FAILED(scaleErrorCode)) {
    return Some(LayoutDeviceToScreenScale(ViewportMinScale()));
  }

  if (scale < 0) {
    return Nothing();
  }
  return Some(std::clamp(LayoutDeviceToScreenScale(scale), ViewportMinScale(),
                         ViewportMaxScale()));
}

void Document::ParseScalesInViewportMetaData(
    const ViewportMetaData& aViewportMetaData) {
  Maybe<LayoutDeviceToScreenScale> scale;

  scale = ParseScaleString(aViewportMetaData.mInitialScale);
  mScaleFloat = scale.valueOr(LayoutDeviceToScreenScale(0.0f));
  mValidScaleFloat = scale.isSome();

  scale = ParseScaleString(aViewportMetaData.mMaximumScale);
  // Chrome uses '5' for the fallback value of maximum-scale, we might
  // consider matching it in future.
  // https://cs.chromium.org/chromium/src/third_party/blink/renderer/core/html/html_meta_element.cc?l=452&rcl=65ca4278b42d269ca738fc93ef7ae04a032afeb0
  mScaleMaxFloat = scale.valueOr(ViewportMaxScale());
  mValidMaxScale = scale.isSome();

  scale = ParseScaleString(aViewportMetaData.mMinimumScale);
  mScaleMinFloat = scale.valueOr(ViewportMinScale());
  mValidMinScale = scale.isSome();

  // Resolve min-zoom and max-zoom values.
  // https://drafts.csswg.org/css-device-adapt/#constraining-min-max-zoom
  if (mValidMaxScale && mValidMinScale) {
    mScaleMaxFloat = std::max(mScaleMinFloat, mScaleMaxFloat);
  }
}

void Document::ParseWidthAndHeightInMetaViewport(const nsAString& aWidthString,
                                                 const nsAString& aHeightString,
                                                 bool aHasValidScale) {
  // The width and height properties
  // https://drafts.csswg.org/css-device-adapt/#width-and-height-properties
  //
  // The width and height viewport <META> properties are translated into width
  // and height descriptors, setting the min-width/min-height value to
  // extend-to-zoom and the max-width/max-height value to the length from the
  // viewport <META> property as follows:
  //
  // 1. Non-negative number values are translated to pixel lengths, clamped to
  //    the range: [1px, 10000px]
  // 2. Negative number values are dropped
  // 3. device-width and device-height translate to 100vw and 100vh respectively
  // 4. Other keywords and unknown values are also dropped
  mMinWidth = nsViewportInfo::kAuto;
  mMaxWidth = nsViewportInfo::kAuto;
  if (!aWidthString.IsEmpty()) {
    mMinWidth = nsViewportInfo::kExtendToZoom;
    if (aWidthString.EqualsLiteral("device-width")) {
      mMaxWidth = nsViewportInfo::kDeviceSize;
    } else {
      nsresult widthErrorCode;
      mMaxWidth = aWidthString.ToInteger(&widthErrorCode);
      if (NS_FAILED(widthErrorCode)) {
        mMaxWidth = nsViewportInfo::kAuto;
      } else if (mMaxWidth >= 0.0f) {
        mMaxWidth = std::clamp(mMaxWidth, CSSCoord(1.0f), CSSCoord(10000.0f));
      } else {
        mMaxWidth = nsViewportInfo::kAuto;
      }
    }
  } else if (aHasValidScale) {
    if (aHeightString.IsEmpty()) {
      mMinWidth = nsViewportInfo::kExtendToZoom;
      mMaxWidth = nsViewportInfo::kExtendToZoom;
    }
  } else if (aHeightString.IsEmpty() && UseWidthDeviceWidthFallbackViewport()) {
    mMinWidth = nsViewportInfo::kExtendToZoom;
    mMaxWidth = nsViewportInfo::kDeviceSize;
  }

  mMinHeight = nsViewportInfo::kAuto;
  mMaxHeight = nsViewportInfo::kAuto;
  if (!aHeightString.IsEmpty()) {
    mMinHeight = nsViewportInfo::kExtendToZoom;
    if (aHeightString.EqualsLiteral("device-height")) {
      mMaxHeight = nsViewportInfo::kDeviceSize;
    } else {
      nsresult heightErrorCode;
      mMaxHeight = aHeightString.ToInteger(&heightErrorCode);
      if (NS_FAILED(heightErrorCode)) {
        mMaxHeight = nsViewportInfo::kAuto;
      } else if (mMaxHeight >= 0.0f) {
        mMaxHeight = std::clamp(mMaxHeight, CSSCoord(1.0f), CSSCoord(10000.0f));
      } else {
        mMaxHeight = nsViewportInfo::kAuto;
      }
    }
  }
}

nsViewportInfo Document::GetViewportInfo(const ScreenIntSize& aDisplaySize) {
  MOZ_ASSERT(mPresShell);

  // Compute the CSS-to-LayoutDevice pixel scale as the product of the
  // widget scale and the full zoom.
  nsPresContext* context = mPresShell->GetPresContext();
  // When querying the full zoom, get it from the device context rather than
  // directly from the pres context, because the device context's value can
  // include an adjustment necessary to keep the number of app units per device
  // pixel an integer, and we want the adjusted value.
  float fullZoom = context ? context->DeviceContext()->GetFullZoom() : 1.0;
  fullZoom = (fullZoom == 0.0) ? 1.0 : fullZoom;
  CSSToLayoutDeviceScale layoutDeviceScale =
      context ? context->CSSToDevPixelScale() : CSSToLayoutDeviceScale(1);

  CSSToScreenScale defaultScale =
      layoutDeviceScale * LayoutDeviceToScreenScale(1.0);

  auto* bc = GetBrowsingContext();
  const bool inRDM = bc && bc->InRDMPane();
  const bool ignoreMetaTag = [&] {
    if (!nsLayoutUtils::ShouldHandleMetaViewport(this)) {
      return true;
    }
    if (Fullscreen()) {
      // We ignore viewport meta tag etc when in fullscreen, see bug 1696717.
      return true;
    }
    if (inRDM && bc->ForceDesktopViewport()) {
      // We ignore meta viewport when devtools tells us to force desktop
      // viewport on RDM.
      return true;
    }
    return false;
  }();

  if (ignoreMetaTag) {
    return nsViewportInfo(aDisplaySize, defaultScale,
                          nsLayoutUtils::AllowZoomingForDocument(this)
                              ? nsViewportInfo::ZoomFlag::AllowZoom
                              : nsViewportInfo::ZoomFlag::DisallowZoom,
                          StaticPrefs::apz_allow_zooming_out()
                              ? nsViewportInfo::ZoomBehaviour::Mobile
                              : nsViewportInfo::ZoomBehaviour::Desktop);
  }

  // Special behaviour for desktop mode, provided we are not on an about: page.
  if (bc && bc->ForceDesktopViewport() && !IsAboutPage()) {
    CSSCoord viewportWidth =
        StaticPrefs::browser_viewport_desktopWidth() / fullZoom;
    // Do not use a desktop viewport size less wide than the display.
    CSSCoord displayWidth = (aDisplaySize / defaultScale).width;
    MOZ_LOG(MobileViewportManager::gLog, LogLevel::Debug,
            ("Desktop-mode viewport size: choosing the larger of display width "
             "(%f) and desktop width (%f)",
             displayWidth.value, viewportWidth.value));
    viewportWidth = nsViewportInfo::Max(displayWidth, viewportWidth);
    CSSToScreenScale scaleToFit(aDisplaySize.width / viewportWidth);
    float aspectRatio = (float)aDisplaySize.height / aDisplaySize.width;
    CSSSize viewportSize(viewportWidth, viewportWidth * aspectRatio);
    ScreenIntSize fakeDesktopSize = RoundedToInt(viewportSize * scaleToFit);
    return nsViewportInfo(fakeDesktopSize, scaleToFit,
                          nsViewportInfo::ZoomFlag::AllowZoom,
                          nsViewportInfo::ZoomBehaviour::Mobile,
                          nsViewportInfo::AutoScaleFlag::AutoScale);
  }

  // In cases where the width of the CSS viewport is less than or equal to the
  // width of the display (i.e. width <= device-width) then we disable
  // double-tap-to-zoom behaviour. See bug 941995 for details.

  switch (mViewportType) {
    case DisplayWidthHeight:
      return nsViewportInfo(aDisplaySize, defaultScale,
                            nsViewportInfo::ZoomFlag::AllowZoom,
                            nsViewportInfo::ZoomBehaviour::Mobile);
    case Unknown: {
      // We might early exit if the viewport is empty. Even if we don't,
      // at the end of this case we'll note that it was empty. Later, when
      // we're using the cached values, this will trigger alternate code paths.
      if (!mLastModifiedViewportMetaData) {
        // If the docType specifies that we are on a site optimized for mobile,
        // then we want to return specially crafted defaults for the viewport
        // info.
        if (RefPtr<DocumentType> docType = GetDoctype()) {
          nsAutoString docId;
          docType->GetPublicId(docId);
          if ((docId.Find(u"WAP") != -1) || (docId.Find(u"Mobile") != -1) ||
              (docId.Find(u"WML") != -1)) {
            // We're making an assumption that the docType can't change here
            mViewportType = DisplayWidthHeight;
            return nsViewportInfo(aDisplaySize, defaultScale,
                                  nsViewportInfo::ZoomFlag::AllowZoom,
                                  nsViewportInfo::ZoomBehaviour::Mobile);
          }
        }

        nsAutoString handheldFriendly;
        GetHeaderData(nsGkAtoms::handheldFriendly, handheldFriendly);
        if (handheldFriendly.EqualsLiteral("true")) {
          mViewportType = DisplayWidthHeight;
          return nsViewportInfo(aDisplaySize, defaultScale,
                                nsViewportInfo::ZoomFlag::AllowZoom,
                                nsViewportInfo::ZoomBehaviour::Mobile);
        }
      }

      ViewportMetaData metaData = GetViewportMetaData();

      // Parse initial-scale, minimum-scale and maximum-scale.
      ParseScalesInViewportMetaData(metaData);

      // Parse width and height properties
      // This function sets m{Min,Max}{Width,Height}.
      ParseWidthAndHeightInMetaViewport(metaData.mWidth, metaData.mHeight,
                                        mValidScaleFloat);

      mAllowZoom = true;
      if ((metaData.mUserScalable.EqualsLiteral("0")) ||
          (metaData.mUserScalable.EqualsLiteral("no")) ||
          (metaData.mUserScalable.EqualsLiteral("false"))) {
        mAllowZoom = false;
      }

      // Resolve viewport-fit value.
      // https://drafts.csswg.org/css-round-display/#viewport-fit-descriptor
      mViewportFit = ViewportFitType::Auto;
      if (!metaData.mViewportFit.IsEmpty()) {
        if (metaData.mViewportFit.EqualsLiteral("contain")) {
          mViewportFit = ViewportFitType::Contain;
        } else if (metaData.mViewportFit.EqualsLiteral("cover")) {
          mViewportFit = ViewportFitType::Cover;
        }
      }

      mWidthStrEmpty = metaData.mWidth.IsEmpty();

      mViewportType = Specified;
      [[fallthrough]];
    }
    case Specified:
    default:
      LayoutDeviceToScreenScale effectiveMinScale = mScaleMinFloat;
      LayoutDeviceToScreenScale effectiveMaxScale = mScaleMaxFloat;
      bool effectiveValidMaxScale = mValidMaxScale;

      nsViewportInfo::ZoomFlag effectiveZoomFlag =
          mAllowZoom ? nsViewportInfo::ZoomFlag::AllowZoom
                     : nsViewportInfo::ZoomFlag::DisallowZoom;
      if (StaticPrefs::browser_ui_zoom_force_user_scalable()) {
        // If the pref to force user-scalable is enabled, we ignore the values
        // from the meta-viewport tag for these properties and just assume they
        // allow the page to be scalable. Note in particular that this code is
        // in the "Specified" branch of the enclosing switch statement, so that
        // calls to GetViewportInfo always use the latest value of the
        // browser_ui_zoom_force_user_scalable pref. Other codepaths that
        // return nsViewportInfo instances are all consistent with
        // browser_ui_zoom_force_user_scalable() already.
        effectiveMinScale = ViewportMinScale();
        effectiveMaxScale = ViewportMaxScale();
        effectiveValidMaxScale = true;
        effectiveZoomFlag = nsViewportInfo::ZoomFlag::AllowZoom;
      }

      // Returns extend-zoom value which is MIN(mScaleFloat, mScaleMaxFloat).
      auto ComputeExtendZoom = [&]() -> float {
        if (mValidScaleFloat && effectiveValidMaxScale) {
          return std::min(mScaleFloat.scale, effectiveMaxScale.scale);
        }
        if (mValidScaleFloat) {
          return mScaleFloat.scale;
        }
        if (effectiveValidMaxScale) {
          return effectiveMaxScale.scale;
        }
        return nsViewportInfo::kAuto;
      };

      // Resolving 'extend-to-zoom'
      // https://drafts.csswg.org/css-device-adapt/#resolve-extend-to-zoom
      float extendZoom = ComputeExtendZoom();

      CSSCoord minWidth = mMinWidth;
      CSSCoord maxWidth = mMaxWidth;
      CSSCoord minHeight = mMinHeight;
      CSSCoord maxHeight = mMaxHeight;

      // aDisplaySize is in screen pixels; convert them to CSS pixels for the
      // viewport size. We need to use this scaled size for any clamping of
      // width or height.
      CSSSize displaySize = ScreenSize(aDisplaySize) / defaultScale;

      // Our min and max width and height values are mostly as specified by
      // the viewport declaration, but we make an exception for max width.
      // Max width, if auto, and if there's no initial-scale, will be set
      // to a default size. This is to support legacy site design with no
      // viewport declaration, and to do that using the same scheme as
      // Chrome does, in order to maintain web compatibility. Since the
      // default size has a complicated calculation, we fixup the maxWidth
      // value after setting it, above.
      if (maxWidth == nsViewportInfo::kAuto && !mValidScaleFloat) {
        maxWidth = StaticPrefs::browser_viewport_desktopWidth();
        // Divide by fullZoom to stretch CSS pixel size of viewport in order
        // to keep device pixel size unchanged after full zoom applied.
        // See bug 974242.
        maxWidth /= fullZoom;

        // The fallback behaviour of using browser.viewport.desktopWidth
        // was designed with small screens in mind, where we are _expanding_ the
        // viewport width to this value. On wider displays (e.g. most tablets),
        // we would actually be _shrinking_ the viewport width to this value,
        // which we want to avoid because it constrains the viewport width
        // (and often results in a larger initial scale) unnecessarily.
        MOZ_LOG(MobileViewportManager::gLog, LogLevel::Debug,
                ("Fallback viewport size: choosing the larger of display width "
                 "(%f) and desktop width (%f)",
                 displaySize.width, maxWidth.value));
        maxWidth = nsViewportInfo::Max(displaySize.width, maxWidth);

        // We set minWidth to ExtendToZoom, which will cause our later width
        // calculation to expand to maxWidth, if scale restrictions allow it.
        minWidth = nsViewportInfo::kExtendToZoom;
      }

      // Resolve device-width and device-height first.
      if (maxWidth == nsViewportInfo::kDeviceSize) {
        maxWidth = displaySize.width;
      }
      if (maxHeight == nsViewportInfo::kDeviceSize) {
        maxHeight = displaySize.height;
      }
      if (extendZoom == nsViewportInfo::kAuto) {
        if (maxWidth == nsViewportInfo::kExtendToZoom) {
          maxWidth = nsViewportInfo::kAuto;
        }
        if (maxHeight == nsViewportInfo::kExtendToZoom) {
          maxHeight = nsViewportInfo::kAuto;
        }
        if (minWidth == nsViewportInfo::kExtendToZoom) {
          minWidth = maxWidth;
        }
        if (minHeight == nsViewportInfo::kExtendToZoom) {
          minHeight = maxHeight;
        }
      } else {
        CSSSize extendSize = displaySize / extendZoom;
        if (maxWidth == nsViewportInfo::kExtendToZoom) {
          maxWidth = extendSize.width;
        }
        if (maxHeight == nsViewportInfo::kExtendToZoom) {
          maxHeight = extendSize.height;
        }
        if (minWidth == nsViewportInfo::kExtendToZoom) {
          minWidth = nsViewportInfo::Max(extendSize.width, maxWidth);
        }
        if (minHeight == nsViewportInfo::kExtendToZoom) {
          minHeight = nsViewportInfo::Max(extendSize.height, maxHeight);
        }
      }

      // Resolve initial width and height from min/max descriptors
      // https://drafts.csswg.org/css-device-adapt/#resolve-initial-width-height
      CSSCoord width = nsViewportInfo::kAuto;
      if (minWidth != nsViewportInfo::kAuto ||
          maxWidth != nsViewportInfo::kAuto) {
        width = nsViewportInfo::Max(
            minWidth, nsViewportInfo::Min(maxWidth, displaySize.width));
      }
      CSSCoord height = nsViewportInfo::kAuto;
      if (minHeight != nsViewportInfo::kAuto ||
          maxHeight != nsViewportInfo::kAuto) {
        height = nsViewportInfo::Max(
            minHeight, nsViewportInfo::Min(maxHeight, displaySize.height));
      }

      // Resolve width value
      // https://drafts.csswg.org/css-device-adapt/#resolve-width
      if (width == nsViewportInfo::kAuto) {
        if (height == nsViewportInfo::kAuto || aDisplaySize.height == 0) {
          width = displaySize.width;
        } else {
          width = height * aDisplaySize.width / aDisplaySize.height;
        }
      }

      // Resolve height value
      // https://drafts.csswg.org/css-device-adapt/#resolve-height
      if (height == nsViewportInfo::kAuto) {
        if (aDisplaySize.width == 0) {
          height = displaySize.height;
        } else {
          height = width * aDisplaySize.height / aDisplaySize.width;
        }
      }
      MOZ_ASSERT(width != nsViewportInfo::kAuto &&
                 height != nsViewportInfo::kAuto);

      CSSSize size(width, height);

      CSSToScreenScale scaleFloat = mScaleFloat * layoutDeviceScale;
      CSSToScreenScale scaleMinFloat = effectiveMinScale * layoutDeviceScale;
      CSSToScreenScale scaleMaxFloat = effectiveMaxScale * layoutDeviceScale;

      nsViewportInfo::AutoSizeFlag sizeFlag =
          nsViewportInfo::AutoSizeFlag::FixedSize;
      if (mMaxWidth == nsViewportInfo::kDeviceSize ||
          (mWidthStrEmpty && (mMaxHeight == nsViewportInfo::kDeviceSize ||
                              mScaleFloat.scale == 1.0f)) ||
          (!mWidthStrEmpty && mMaxWidth == nsViewportInfo::kAuto &&
           mMaxHeight < 0)) {
        sizeFlag = nsViewportInfo::AutoSizeFlag::AutoSize;
      }

      // FIXME: Resolving width and height should be done above 'Resolve width
      // value' and 'Resolve height value'.
      if (sizeFlag == nsViewportInfo::AutoSizeFlag::AutoSize) {
        size = displaySize;
      }

      // The purpose of clamping the viewport width to a minimum size is to
      // prevent page authors from setting it to a ridiculously small value.
      // If the page is actually being rendered in a very small area (as might
      // happen in e.g. Android 8's picture-in-picture mode), we don't want to
      // prevent the viewport from taking on that size.
      CSSSize effectiveMinSize = Min(CSSSize(kViewportMinSize), displaySize);

      size.width = std::clamp(size.width, effectiveMinSize.width,
                              float(kViewportMaxSize.width));

      // Also recalculate the default zoom, if it wasn't specified in the
      // metadata, and the width is specified.
      if (!mValidScaleFloat && !mWidthStrEmpty) {
        CSSToScreenScale bestFitScale(float(aDisplaySize.width) / size.width);
        scaleFloat = (scaleFloat > bestFitScale) ? scaleFloat : bestFitScale;
      }

      size.height = std::clamp(size.height, effectiveMinSize.height,
                               float(kViewportMaxSize.height));

      // In cases of user-scalable=no, if we have a positive scale, clamp it to
      // min and max, and then use the clamped value for the scale, the min, and
      // the max. If we don't have a positive scale, assert that we are setting
      // the auto scale flag.
      if (effectiveZoomFlag == nsViewportInfo::ZoomFlag::DisallowZoom &&
          scaleFloat > CSSToScreenScale(0.0f)) {
        scaleFloat = scaleMinFloat = scaleMaxFloat =
            std::clamp(scaleFloat, scaleMinFloat, scaleMaxFloat);
      }
      MOZ_ASSERT(
          scaleFloat > CSSToScreenScale(0.0f) || !mValidScaleFloat,
          "If we don't have a positive scale, we should be using auto scale.");

      // We need to perform a conversion, but only if the initial or maximum
      // scale were set explicitly by the user.
      if (mValidScaleFloat && scaleFloat >= scaleMinFloat &&
          scaleFloat <= scaleMaxFloat) {
        CSSSize displaySize = ScreenSize(aDisplaySize) / scaleFloat;
        size.width = std::max(size.width, displaySize.width);
        size.height = std::max(size.height, displaySize.height);
      } else if (effectiveValidMaxScale) {
        CSSSize displaySize = ScreenSize(aDisplaySize) / scaleMaxFloat;
        size.width = std::max(size.width, displaySize.width);
        size.height = std::max(size.height, displaySize.height);
      }

      return nsViewportInfo(
          scaleFloat, scaleMinFloat, scaleMaxFloat, size, sizeFlag,
          mValidScaleFloat ? nsViewportInfo::AutoScaleFlag::FixedScale
                           : nsViewportInfo::AutoScaleFlag::AutoScale,
          effectiveZoomFlag, mViewportFit);
  }
}

ViewportMetaData Document::GetViewportMetaData() const {
  return mLastModifiedViewportMetaData ? *mLastModifiedViewportMetaData
                                       : ViewportMetaData();
}

static InteractiveWidget ParseInteractiveWidget(
    const ViewportMetaData& aViewportMetaData) {
  if (aViewportMetaData.mInteractiveWidgetMode.IsEmpty()) {
    return InteractiveWidgetUtils::DefaultInteractiveWidgetMode();
  }

  if (aViewportMetaData.mInteractiveWidgetMode.EqualsIgnoreCase(
          "resizes-visual")) {
    return InteractiveWidget::ResizesVisual;
  }
  if (aViewportMetaData.mInteractiveWidgetMode.EqualsIgnoreCase(
          "resizes-content")) {
    return InteractiveWidget::ResizesContent;
  }
  if (aViewportMetaData.mInteractiveWidgetMode.EqualsIgnoreCase(
          "overlays-content")) {
    return InteractiveWidget::OverlaysContent;
  }
  return InteractiveWidgetUtils::DefaultInteractiveWidgetMode();
}

void Document::SetMetaViewportData(UniquePtr<ViewportMetaData> aData) {
  mLastModifiedViewportMetaData = std::move(aData);
  // Trigger recomputation of the nsViewportInfo the next time it's queried.
  mViewportType = Unknown;

  // Parse interactive-widget here anyway. Normally we parse any data in the
  // meta viewport tag in GetViewportInfo(), but GetViewportInfo() depends on
  // the document state (e.g. display size, fullscreen, desktop-mode etc.)
  // whereas interactive-widget is independent from the document state, it's
  // necessary whatever the document state is.
  dom::InteractiveWidget interactiveWidget =
      ParseInteractiveWidget(*mLastModifiedViewportMetaData);
  if (mInteractiveWidgetMode != interactiveWidget) {
    mInteractiveWidgetMode = interactiveWidget;
  }

  AsyncEventDispatcher::RunDOMEventWhenSafe(
      *this, u"DOMMetaViewportFitChanged"_ns, CanBubble::eYes,
      ChromeOnlyDispatch::eYes);
}

EventListenerManager* Document::GetOrCreateListenerManager() {
  if (!mListenerManager) {
    mListenerManager =
        new EventListenerManager(static_cast<EventTarget*>(this));
    SetFlags(NODE_HAS_LISTENERMANAGER);
  }

  return mListenerManager;
}

EventListenerManager* Document::GetExistingListenerManager() const {
  return mListenerManager;
}

void Document::GetEventTargetParent(EventChainPreVisitor& aVisitor) {
  aVisitor.mCanHandle = true;
  // FIXME! This is a hack to make middle mouse paste working also in Editor.
  // Bug 329119
  aVisitor.mForceContentDispatch = true;

  // Load events must not propagate to |window| object, see bug 335251.
  if (aVisitor.mEvent->mMessage != eLoad) {
    nsGlobalWindowOuter* window = nsGlobalWindowOuter::Cast(GetWindow());
    aVisitor.SetParentTarget(
        window ? window->GetTargetForEventTargetChain() : nullptr, false);
  }
}

already_AddRefed<Event> Document::CreateEvent(const nsAString& aEventType,
                                              CallerType aCallerType,
                                              ErrorResult& rv) const {
  nsPresContext* presContext = GetPresContext();

  // Create event even without presContext.
  RefPtr<Event> ev =
      EventDispatcher::CreateEvent(const_cast<Document*>(this), presContext,
                                   nullptr, aEventType, aCallerType);
  if (!ev) {
    rv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }
  WidgetEvent* e = ev->WidgetEventPtr();
  e->mFlags.mBubbles = false;
  e->mFlags.mCancelable = false;
  return ev.forget();
}

void Document::FlushPendingNotifications(FlushType aType) {
  mozilla::ChangesToFlush flush(aType, aType >= FlushType::Style,
                                aType >= FlushType::Layout);
  FlushPendingNotifications(flush);
}

void Document::FlushPendingNotifications(mozilla::ChangesToFlush aFlush) {
  FlushType flushType = aFlush.mFlushType;

  RefPtr<Document> documentOnStack = this;

  // We need to flush the sink for non-HTML documents (because the XML
  // parser still does insertion with deferred notifications).  We
  // also need to flush the sink if this is a layout-related flush, to
  // make sure that layout is started as needed.  But we can skip that
  // part if we have no presshell or if it's already done an initial
  // reflow.
  if ((!IsHTMLDocument() || (flushType > FlushType::ContentAndNotify &&
                             mPresShell && !mPresShell->DidInitialize())) &&
      (mParser || mWeakSink)) {
    nsCOMPtr<nsIContentSink> sink;
    if (mParser) {
      sink = mParser->GetContentSink();
    } else {
      sink = do_QueryReferent(mWeakSink);
      if (!sink) {
        mWeakSink = nullptr;
      }
    }
    // Determine if it is safe to flush the sink notifications
    // by determining if it safe to flush all the presshells.
    if (sink && (flushType == FlushType::Content || IsSafeToFlush())) {
      sink->FlushPendingNotifications(flushType);
    }
  }

  // Should we be flushing pending binding constructors in here?

  if (flushType <= FlushType::ContentAndNotify) {
    // Nothing to do here
    return;
  }

  // If we have a parent we must flush the parent too to ensure that our
  // container is reflowed if its size was changed.
  //
  // We do it only if the subdocument and the parent can observe each other
  // synchronously (that is, if we're not cross-origin), to avoid work that is
  // not observable, and if the parent document has finished loading all its
  // render-blocking stylesheets and may start laying out the document, to avoid
  // unnecessary flashes of unstyled content on the parent document. Note that
  // this last bit means that size-dependent media queries in this document may
  // produce incorrect results temporarily.
  //
  // But if it's not safe to flush ourselves, then don't flush the parent, since
  // that can cause things like resizes of our frame's widget, which we can't
  // handle while flushing is unsafe.
  if (StyleOrLayoutObservablyDependsOnParentDocumentLayout() &&
      mParentDocument->MayStartLayout() && IsSafeToFlush()) {
    ChangesToFlush parentFlush = aFlush;
    if (flushType >= FlushType::Style) {
      // Since media queries mean that a size change of our container can affect
      // style, we need to promote a style flush on ourself to a layout flush on
      // our parent, since we need our container to be the correct size to
      // determine the correct style.
      parentFlush.mFlushType = std::max(FlushType::Layout, flushType);
    }
    mParentDocument->FlushPendingNotifications(parentFlush);
  }

  if (RefPtr<PresShell> presShell = GetPresShell()) {
    presShell->FlushPendingNotifications(aFlush);
  }
}

void Document::FlushExternalResources(FlushType aType) {
  NS_ASSERTION(
      aType >= FlushType::Style,
      "should only need to flush for style or higher in external resources");
  if (GetDisplayDocument()) {
    return;
  }

  EnumerateExternalResources([aType](Document& aDoc) {
    aDoc.FlushPendingNotifications(aType);
    return CallState::Continue;
  });
}

void Document::SetXMLDeclaration(const char16_t* aVersion,
                                 const char16_t* aEncoding,
                                 const int32_t aStandalone) {
  if (!aVersion || *aVersion == '\0') {
    mXMLDeclarationBits = 0;
    return;
  }

  mXMLDeclarationBits = XML_DECLARATION_BITS_DECLARATION_EXISTS;

  if (aEncoding && *aEncoding != '\0') {
    mXMLDeclarationBits |= XML_DECLARATION_BITS_ENCODING_EXISTS;
  }

  if (aStandalone == 1) {
    mXMLDeclarationBits |= XML_DECLARATION_BITS_STANDALONE_EXISTS |
                           XML_DECLARATION_BITS_STANDALONE_YES;
  } else if (aStandalone == 0) {
    mXMLDeclarationBits |= XML_DECLARATION_BITS_STANDALONE_EXISTS;
  }
}

void Document::GetXMLDeclaration(nsAString& aVersion, nsAString& aEncoding,
                                 nsAString& aStandalone) {
  aVersion.Truncate();
  aEncoding.Truncate();
  aStandalone.Truncate();

  if (!(mXMLDeclarationBits & XML_DECLARATION_BITS_DECLARATION_EXISTS)) {
    return;
  }

  // always until we start supporting 1.1 etc.
  aVersion.AssignLiteral("1.0");

  if (mXMLDeclarationBits & XML_DECLARATION_BITS_ENCODING_EXISTS) {
    // This is what we have stored, not necessarily what was written
    // in the original
    GetCharacterSet(aEncoding);
  }

  if (mXMLDeclarationBits & XML_DECLARATION_BITS_STANDALONE_EXISTS) {
    if (mXMLDeclarationBits & XML_DECLARATION_BITS_STANDALONE_YES) {
      aStandalone.AssignLiteral("yes");
    } else {
      aStandalone.AssignLiteral("no");
    }
  }
}

void Document::AddColorSchemeMeta(HTMLMetaElement& aMeta) {
  mColorSchemeMetaTags.Insert(aMeta);
  RecomputeColorScheme();
}

void Document::RemoveColorSchemeMeta(HTMLMetaElement& aMeta) {
  mColorSchemeMetaTags.RemoveElement(aMeta);
  RecomputeColorScheme();
}

void Document::RecomputeColorScheme() {
  auto oldColorScheme = mColorSchemeBits;
  mColorSchemeBits = 0;
  const nsTArray<HTMLMetaElement*>& elements = mColorSchemeMetaTags;
  for (const HTMLMetaElement* el : elements) {
    nsAutoString content;
    if (!el->GetAttr(nsGkAtoms::content, content)) {
      continue;
    }

    NS_ConvertUTF16toUTF8 contentU8(content);
    if (Servo_ColorScheme_Parse(&contentU8, &mColorSchemeBits)) {
      break;
    }
  }

  if (mColorSchemeBits == oldColorScheme) {
    return;
  }

  if (nsPresContext* pc = GetPresContext()) {
    // This affects system colors, which are inherited, so we need to recascade.
    pc->RebuildAllStyleData(nsChangeHint(0), RestyleHint::RecascadeSubtree());
  }
}

bool Document::IsScriptEnabled() const {
  // If this document is sandboxed without 'allow-scripts'
  // script is not enabled
  if (HasScriptsBlockedBySandbox()) {
    return false;
  }

  nsCOMPtr<nsIScriptGlobalObject> globalObject =
      do_QueryInterface(GetInnerWindow());
  if (!globalObject || !globalObject->HasJSGlobal()) {
    return false;
  }

  return xpc::Scriptability::Get(globalObject->GetGlobalJSObjectPreserveColor())
      .Allowed();
}

void Document::RetrieveRelevantHeaders(nsIChannel* aChannel) {
  PRTime modDate = 0;
  nsresult rv;

  nsCOMPtr<nsIHttpChannel> httpChannel;
  rv = GetHttpChannelHelper(aChannel, getter_AddRefs(httpChannel));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  if (httpChannel) {
    nsAutoCString tmp;
    rv = httpChannel->GetResponseHeader("last-modified"_ns, tmp);

    if (NS_SUCCEEDED(rv)) {
      PRTime time;
      PRStatus st = PR_ParseTimeString(tmp.get(), true, &time);
      if (st == PR_SUCCESS) {
        modDate = time;
      }
    }

    static const char* const headers[] = {
        "default-style", "content-style-type", "content-language",
        "content-disposition", "refresh", "x-dns-prefetch-control",
        "x-frame-options", "origin-trial",
        // add more http headers if you need
        // XXXbz don't add content-location support without reading bug
        // 238654 and its dependencies/dups first.
        0};

    nsAutoCString headerVal;
    const char* const* name = headers;
    while (*name) {
      rv = httpChannel->GetResponseHeader(nsDependentCString(*name), headerVal);
      if (NS_SUCCEEDED(rv) && !headerVal.IsEmpty()) {
        RefPtr<nsAtom> key = NS_Atomize(*name);
        SetHeaderData(key, NS_ConvertASCIItoUTF16(headerVal));
      }
      ++name;
    }
  } else {
    nsCOMPtr<nsIFileChannel> fileChannel = do_QueryInterface(aChannel);
    if (fileChannel) {
      nsCOMPtr<nsIFile> file;
      fileChannel->GetFile(getter_AddRefs(file));
      if (file) {
        PRTime msecs;
        rv = file->GetLastModifiedTime(&msecs);

        if (NS_SUCCEEDED(rv)) {
          modDate = msecs * int64_t(PR_USEC_PER_MSEC);
        }
      }
    } else {
      nsAutoCString contentDisp;
      rv = aChannel->GetContentDispositionHeader(contentDisp);
      if (NS_SUCCEEDED(rv)) {
        SetHeaderData(nsGkAtoms::headerContentDisposition,
                      NS_ConvertASCIItoUTF16(contentDisp));
      }
    }
  }

  mLastModified.Truncate();
  if (modDate != 0) {
    GetFormattedTimeString(modDate,
                           ShouldResistFingerprinting(RFPTarget::JSDateTimeUTC),
                           mLastModified);
  }
}

void Document::ProcessMETATag(HTMLMetaElement* aMetaElement) {
  // set any HTTP-EQUIV data into document's header data as well as url
  nsAutoString header;
  aMetaElement->GetAttr(nsGkAtoms::httpEquiv, header);
  if (!header.IsEmpty()) {
    // Ignore META REFRESH when document is sandboxed from automatic features.
    nsContentUtils::ASCIIToLower(header);
    if (nsGkAtoms::refresh->Equals(header) &&
        (GetSandboxFlags() & SANDBOXED_AUTOMATIC_FEATURES)) {
      return;
    }

    nsAutoString result;
    aMetaElement->GetAttr(nsGkAtoms::content, result);
    if (!result.IsEmpty()) {
      RefPtr<nsAtom> fieldAtom(NS_Atomize(header));
      SetHeaderData(fieldAtom, result);
    }
  }

  if (aMetaElement->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name,
                                nsGkAtoms::handheldFriendly, eIgnoreCase)) {
    nsAutoString result;
    aMetaElement->GetAttr(nsGkAtoms::content, result);
    if (!result.IsEmpty()) {
      nsContentUtils::ASCIIToLower(result);
      SetHeaderData(nsGkAtoms::handheldFriendly, result);
    }
  }
}

already_AddRefed<Element> Document::CreateElem(const nsAString& aName,
                                               nsAtom* aPrefix,
                                               int32_t aNamespaceID,
                                               const nsAString* aIs) {
#ifdef DEBUG
  nsAutoString qName;
  if (aPrefix) {
    aPrefix->ToString(qName);
    qName.Append(':');
  }
  qName.Append(aName);

  // Note: "a:b:c" is a valid name in non-namespaces XML, and
  // Document::CreateElement can call us with such a name and no prefix,
  // which would cause an error if we just used true here.
  bool nsAware = aPrefix != nullptr || aNamespaceID != GetDefaultNamespaceID();
  NS_ASSERTION(NS_SUCCEEDED(nsContentUtils::CheckQName(qName, nsAware)),
               "Don't pass invalid prefixes to Document::CreateElem, "
               "check caller.");
#endif

  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  mNodeInfoManager->GetNodeInfo(aName, aPrefix, aNamespaceID, ELEMENT_NODE,
                                getter_AddRefs(nodeInfo));
  NS_ENSURE_TRUE(nodeInfo, nullptr);

  nsCOMPtr<Element> element;
  nsresult rv = NS_NewElement(getter_AddRefs(element), nodeInfo.forget(),
                              NOT_FROM_PARSER, aIs);
  return NS_SUCCEEDED(rv) ? element.forget() : nullptr;
}

bool Document::IsSafeToFlush() const {
  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return true;
  }
  return presShell->IsSafeToFlush();
}

void Document::Sanitize() {
  // Sanitize the document by resetting all (current and former) password fields
  // and any form fields with autocomplete=off to their default values.  We do
  // this now, instead of when the presentation is restored, to offer some
  // protection in case there is ever an exploit that allows a cached document
  // to be accessed from a different document.

  // First locate all input elements, regardless of whether they are
  // in a form, and reset the password and autocomplete=off elements.

  RefPtr<nsContentList> nodes = GetElementsByTagName(u"input"_ns);

  nsAutoString value;

  uint32_t length = nodes->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    NS_ASSERTION(nodes->Item(i), "null item in node list!");

    RefPtr<HTMLInputElement> input =
        HTMLInputElement::FromNodeOrNull(nodes->Item(i));
    if (!input) continue;

    input->GetAttr(nsGkAtoms::autocomplete, value);
    if (value.LowerCaseEqualsLiteral("off") || input->HasBeenTypePassword()) {
      input->Reset();
    }
  }

  // Now locate all _form_ elements that have autocomplete=off and reset them
  nodes = GetElementsByTagName(u"form"_ns);

  length = nodes->Length(true);
  for (uint32_t i = 0; i < length; ++i) {
    // Reset() may change the list dynamically.
    RefPtr<HTMLFormElement> form =
        HTMLFormElement::FromNodeOrNull(nodes->Item(i));
    if (!form) continue;

    form->GetAttr(nsGkAtoms::autocomplete, value);
    if (value.LowerCaseEqualsLiteral("off")) form->Reset();
  }
}

void Document::EnumerateSubDocuments(SubDocEnumFunc aCallback) {
  if (!mSubDocuments) {
    return;
  }

  // PLDHashTable::Iterator can't handle modifications while iterating so we
  // copy all entries to an array first before calling any callbacks.
  AutoTArray<RefPtr<Document>, 8> subdocs;
  for (auto iter = mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
    const auto* entry = static_cast<SubDocMapEntry*>(iter.Get());
    if (Document* subdoc = entry->mSubDocument) {
      subdocs.AppendElement(subdoc);
    }
  }
  for (auto& subdoc : subdocs) {
    if (aCallback(*subdoc) == CallState::Stop) {
      break;
    }
  }
}

void Document::CollectDescendantDocuments(
    nsTArray<RefPtr<Document>>& aDescendants,
    IncludeSubResources aIncludeSubresources, SubDocTestFunc aCallback) const {
  if (mSubDocuments) {
    for (auto iter = mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
      const auto* entry = static_cast<SubDocMapEntry*>(iter.Get());
      if (Document* subdoc = entry->mSubDocument) {
        if (aCallback(subdoc)) {
          aDescendants.AppendElement(subdoc);
        }
        subdoc->CollectDescendantDocuments(aDescendants, aIncludeSubresources,
                                           aCallback);
      }
    }
  }

  if (aIncludeSubresources == IncludeSubResources::Yes) {
    mExternalResourceMap.CollectDescendantDocuments(aDescendants, aCallback);
  }
}

bool Document::CanSavePresentation(nsIRequest* aNewRequest,
                                   uint32_t& aBFCacheCombo,
                                   bool aIncludeSubdocuments,
                                   bool aAllowUnloadListeners) {
  bool ret = true;

  if (!IsBFCachingAllowed()) {
    aBFCacheCombo |= BFCacheStatus::NOT_ALLOWED;
    ret = false;
  }

  nsAutoCString uri;
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gPageCacheLog, LogLevel::Verbose))) {
    if (mDocumentURI) {
      mDocumentURI->GetSpec(uri);
    }
  }

  if (EventHandlingSuppressed()) {
    MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
            ("Save of %s blocked on event handling suppression", uri.get()));
    aBFCacheCombo |= BFCacheStatus::EVENT_HANDLING_SUPPRESSED;
    ret = false;
  }

  // Do not allow suspended windows to be placed in the
  // bfcache.  This method is also used to verify a document
  // coming out of the bfcache is ok to restore, though.  So
  // we only want to block suspend windows that aren't also
  // frozen.
  auto* win = nsGlobalWindowInner::Cast(GetInnerWindow());
  if (win && win->IsSuspended() && !win->IsFrozen()) {
    MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
            ("Save of %s blocked on suspended Window", uri.get()));
    aBFCacheCombo |= BFCacheStatus::SUSPENDED;
    ret = false;
  }

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aNewRequest);
  bool thirdParty = false;
  // Currently some other mobile browsers seem to bfcache only cross-domain
  // pages, but bfcache those also when there are unload event listeners, so
  // this is trying to match that behavior as much as possible.
  bool allowUnloadListeners =
      aAllowUnloadListeners &&
      StaticPrefs::docshell_shistory_bfcache_allow_unload_listeners() &&
      (!channel || (NS_SUCCEEDED(NodePrincipal()->IsThirdPartyChannel(
                        channel, &thirdParty)) &&
                    thirdParty));

  // Check our event listener manager for unload/beforeunload listeners.
  nsCOMPtr<EventTarget> piTarget = do_QueryInterface(mScriptGlobalObject);
  if (!allowUnloadListeners && piTarget) {
    EventListenerManager* manager = piTarget->GetExistingListenerManager();
    if (manager) {
      if (manager->HasUnloadListeners()) {
        MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
                ("Save of %s blocked due to unload handlers", uri.get()));
        aBFCacheCombo |= BFCacheStatus::UNLOAD_LISTENER;
        ret = false;
      }
      if (manager->HasBeforeUnloadListeners()) {
        if (!mozilla::SessionHistoryInParent() ||
            !StaticPrefs::
                docshell_shistory_bfcache_ship_allow_beforeunload_listeners()) {
          MOZ_LOG(
              gPageCacheLog, mozilla::LogLevel::Verbose,
              ("Save of %s blocked due to beforeUnload handlers", uri.get()));
          aBFCacheCombo |= BFCacheStatus::BEFOREUNLOAD_LISTENER;
          ret = false;
        }
      }
    }
  }

  // Check if we have pending network requests
  nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
  if (loadGroup) {
    nsCOMPtr<nsISimpleEnumerator> requests;
    loadGroup->GetRequests(getter_AddRefs(requests));

    bool hasMore = false;

    // We want to bail out if we have any requests other than aNewRequest (or
    // in the case when aNewRequest is a part of a multipart response the base
    // channel the multipart response is coming in on).
    nsCOMPtr<nsIChannel> baseChannel;
    nsCOMPtr<nsIMultiPartChannel> part(do_QueryInterface(aNewRequest));
    if (part) {
      part->GetBaseChannel(getter_AddRefs(baseChannel));
    }

    while (NS_SUCCEEDED(requests->HasMoreElements(&hasMore)) && hasMore) {
      nsCOMPtr<nsISupports> elem;
      requests->GetNext(getter_AddRefs(elem));

      nsCOMPtr<nsIRequest> request = do_QueryInterface(elem);
      if (request && request != aNewRequest && request != baseChannel) {
        // Favicon loads don't need to block caching.
        nsCOMPtr<nsIChannel> channel = do_QueryInterface(request);
        if (channel) {
          nsCOMPtr<nsILoadInfo> li = channel->LoadInfo();
          if (li->InternalContentPolicyType() ==
              nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON) {
            continue;
          }
        }

        if (MOZ_UNLIKELY(MOZ_LOG_TEST(gPageCacheLog, LogLevel::Verbose))) {
          nsAutoCString requestName;
          request->GetName(requestName);
          MOZ_LOG(gPageCacheLog, LogLevel::Verbose,
                  ("Save of %s blocked because document has request %s",
                   uri.get(), requestName.get()));
        }
        aBFCacheCombo |= BFCacheStatus::REQUEST;
        ret = false;
      }
    }
  }

  // Check if we have active GetUserMedia use
  if (MediaManager::Exists() && win &&
      MediaManager::Get()->IsWindowStillActive(win->WindowID())) {
    MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
            ("Save of %s blocked due to GetUserMedia", uri.get()));
    aBFCacheCombo |= BFCacheStatus::ACTIVE_GET_USER_MEDIA;
    ret = false;
  }

#ifdef MOZ_WEBRTC
  // Check if we have active PeerConnections
  if (win && win->HasActivePeerConnections()) {
    MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
            ("Save of %s blocked due to PeerConnection", uri.get()));
    aBFCacheCombo |= BFCacheStatus::ACTIVE_PEER_CONNECTION;
    ret = false;
  }
#endif  // MOZ_WEBRTC

  // Don't save presentations for documents containing EME content, so that
  // CDMs reliably shutdown upon user navigation.
  if (ContainsEMEContent()) {
    aBFCacheCombo |= BFCacheStatus::CONTAINS_EME_CONTENT;
    ret = false;
  }

  // Don't save presentations for documents containing MSE content, to
  // reduce memory usage.
  if (ContainsMSEContent()) {
    MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
            ("Save of %s blocked due to MSE use", uri.get()));
    aBFCacheCombo |= BFCacheStatus::CONTAINS_MSE_CONTENT;
    ret = false;
  }

  if (aIncludeSubdocuments && mSubDocuments) {
    for (auto iter = mSubDocuments->Iter(); !iter.Done(); iter.Next()) {
      auto entry = static_cast<SubDocMapEntry*>(iter.Get());
      Document* subdoc = entry->mSubDocument;

      uint32_t subDocBFCacheCombo = 0;
      // The aIgnoreRequest we were passed is only for us, so don't pass it on.
      bool canCache =
          subdoc ? subdoc->CanSavePresentation(nullptr, subDocBFCacheCombo,
                                               true, allowUnloadListeners)
                 : false;
      if (!canCache) {
        MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
                ("Save of %s blocked due to subdocument blocked", uri.get()));
        aBFCacheCombo |= subDocBFCacheCombo;
        ret = false;
      }
    }
  }

  if (!mozilla::BFCacheInParent()) {
    // BFCache is currently not compatible with remote subframes (bug 1609324)
    if (RefPtr<BrowsingContext> browsingContext = GetBrowsingContext()) {
      for (auto& child : browsingContext->Children()) {
        if (!child->IsInProcess()) {
          aBFCacheCombo |= BFCacheStatus::CONTAINS_REMOTE_SUBFRAMES;
          ret = false;
          break;
        }
      }
    }
  }

  if (win) {
    auto* globalWindow = nsGlobalWindowInner::Cast(win);
#ifdef MOZ_WEBSPEECH
    if (globalWindow->HasActiveSpeechSynthesis()) {
      MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
              ("Save of %s blocked due to Speech use", uri.get()));
      aBFCacheCombo |= BFCacheStatus::HAS_ACTIVE_SPEECH_SYNTHESIS;
      ret = false;
    }
#endif
    if (globalWindow->HasUsedVR()) {
      MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
              ("Save of %s blocked due to having used VR", uri.get()));
      aBFCacheCombo |= BFCacheStatus::HAS_USED_VR;
      ret = false;
    }

    if (win->HasActiveLocks()) {
      MOZ_LOG(
          gPageCacheLog, mozilla::LogLevel::Verbose,
          ("Save of %s blocked due to having active lock requests", uri.get()));
      aBFCacheCombo |= BFCacheStatus::ACTIVE_LOCK;
      ret = false;
    }

    if (win->HasActiveWebTransports()) {
      MOZ_LOG(gPageCacheLog, mozilla::LogLevel::Verbose,
              ("Save of %s blocked due to WebTransport", uri.get()));
      aBFCacheCombo |= BFCacheStatus::ACTIVE_WEBTRANSPORT;
      ret = false;
    }
  }

  return ret;
}

void Document::Destroy() {
  // The DocumentViewer wants to release the document now.  So, tell our content
  // to drop any references to the document so that it can be destroyed.
  if (mIsGoingAway) {
    return;
  }

  if (RefPtr transition = mActiveViewTransition) {
    transition->SkipTransition(SkipTransitionReason::DocumentHidden);
  }

  ReportDocumentUseCounters();
  ReportLCP();
  SetDevToolsWatchingDOMMutations(false);

  mIsGoingAway = true;

  ScriptLoader()->Destroy();
  SetScriptGlobalObject(nullptr);
  RemovedFromDocShell();

  bool oldVal = mInUnlinkOrDeletion;
  mInUnlinkOrDeletion = true;

#ifdef DEBUG
  uint32_t oldChildCount = GetChildCount();
#endif

  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    child->DestroyContent();
    MOZ_ASSERT(child->GetParentNode() == this);
  }
  MOZ_ASSERT(oldChildCount == GetChildCount());
  MOZ_ASSERT(!mSubDocuments || mSubDocuments->EntryCount() == 0);

  mInUnlinkOrDeletion = oldVal;

  mLayoutHistoryState = nullptr;

  if (mOriginalDocument) {
    mOriginalDocument->mLatestStaticClone = nullptr;
  }

  if (IsStaticDocument()) {
    RemoveProperty(nsGkAtoms::printisfocuseddoc);
    RemoveProperty(nsGkAtoms::printselectionranges);
  }

  // Shut down our external resource map.  We might not need this for
  // leak-fixing if we fix nsDocumentViewer to do cycle-collection, but
  // tearing down all those frame trees right now is the right thing to do.
  mExternalResourceMap.Shutdown();

  // Manually break cycles via promise's global object pointer.
  mReadyForIdle = nullptr;
  mOrientationPendingPromise = nullptr;

  // To break cycles.
  mPreloadService.ClearAllPreloads();

  if (mDocumentL10n) {
    mDocumentL10n->Destroy();
  }

  if (!mPresShell) {
    DropStyleSet();
  }
}

void Document::RemovedFromDocShell() {
  mEditingState = EditingState::eOff;

  if (mRemovedFromDocShell) return;

  mRemovedFromDocShell = true;
  NotifyActivityChanged();

  for (nsIContent* child = GetFirstChild(); child;
       child = child->GetNextSibling()) {
    child->SaveSubtreeState();
  }

  nsIDocShell* docShell = GetDocShell();
  if (docShell) {
    docShell->SynchronizeLayoutHistoryState();
  }
}

already_AddRefed<nsILayoutHistoryState> Document::GetLayoutHistoryState()
    const {
  nsCOMPtr<nsILayoutHistoryState> state;
  if (!mScriptGlobalObject) {
    state = mLayoutHistoryState;
  } else {
    nsCOMPtr<nsIDocShell> docShell(mDocumentContainer);
    if (docShell) {
      docShell->GetLayoutHistoryState(getter_AddRefs(state));
    }
  }

  return state.forget();
}

void Document::EnsureOnloadBlocker() {
  // If mScriptGlobalObject is null, we shouldn't be messing with the loadgroup
  // -- it's not ours.
  if (mOnloadBlockCount != 0 && mScriptGlobalObject) {
    nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup();
    if (loadGroup) {
      // Check first to see if mOnloadBlocker is in the loadgroup.
      nsCOMPtr<nsISimpleEnumerator> requests;
      loadGroup->GetRequests(getter_AddRefs(requests));

      bool hasMore = false;
      while (NS_SUCCEEDED(requests->HasMoreElements(&hasMore)) && hasMore) {
        nsCOMPtr<nsISupports> elem;
        requests->GetNext(getter_AddRefs(elem));
        nsCOMPtr<nsIRequest> request = do_QueryInterface(elem);
        if (request && request == mOnloadBlocker) {
          return;
        }
      }

      // Not in the loadgroup, so add it.
      loadGroup->AddRequest(mOnloadBlocker, nullptr);
    }
  }
}

void Document::BlockOnload() {
  if (mDisplayDocument) {
    mDisplayDocument->BlockOnload();
    return;
  }

  // If mScriptGlobalObject is null, we shouldn't be messing with the loadgroup
  // -- it's not ours.
  // If we're already complete there's no need to mess with the loadgroup
  // either, we're not blocking the load event after all.
  if (mOnloadBlockCount == 0 && mScriptGlobalObject &&
      mReadyState != ReadyState::READYSTATE_COMPLETE) {
    if (nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup()) {
      loadGroup->AddRequest(mOnloadBlocker, nullptr);
    }
  }
  ++mOnloadBlockCount;
}

void Document::UnblockOnload(bool aFireSync) {
  if (mDisplayDocument) {
    mDisplayDocument->UnblockOnload(aFireSync);
    return;
  }

  --mOnloadBlockCount;

  if (mOnloadBlockCount != 0) {
    return;
  }
  if (mScriptGlobalObject) {
    // Only manipulate the loadgroup in this case, because if
    // mScriptGlobalObject is null, it's not ours.
    if (aFireSync) {
      // Increment mOnloadBlockCount, since DoUnblockOnload will decrement it
      ++mOnloadBlockCount;
      DoUnblockOnload();
    } else {
      PostUnblockOnloadEvent();
    }
  } else if (mIsBeingUsedAsImage) {
    // To correctly unblock onload for a document that contains an SVG
    // image, we need to know when all of the SVG document's resources are
    // done loading, in a way comparable to |window.onload|. We fire this
    // event to indicate that the SVG should be considered fully loaded.
    // Because scripting is disabled on SVG-as-image documents, this event
    // is not accessible to content authors. (See bug 837315.)
    RefPtr<AsyncEventDispatcher> asyncDispatcher =
        new AsyncEventDispatcher(this, u"MozSVGAsImageDocumentLoad"_ns,
                                 CanBubble::eNo, ChromeOnlyDispatch::eNo);
    asyncDispatcher->PostDOMEvent();
  }
}

class nsUnblockOnloadEvent : public Runnable {
 public:
  explicit nsUnblockOnloadEvent(Document* aDoc)
      : mozilla::Runnable("nsUnblockOnloadEvent"), mDoc(aDoc) {}
  NS_IMETHOD Run() override {
    mDoc->DoUnblockOnload();
    return NS_OK;
  }

 private:
  RefPtr<Document> mDoc;
};

void Document::PostUnblockOnloadEvent() {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIRunnable> evt = new nsUnblockOnloadEvent(this);
  nsresult rv = Dispatch(evt.forget());
  if (NS_SUCCEEDED(rv)) {
    // Stabilize block count so we don't post more events while this one is up
    ++mOnloadBlockCount;
  } else {
    NS_WARNING("failed to dispatch nsUnblockOnloadEvent");
  }
}

void Document::DoUnblockOnload() {
  MOZ_ASSERT(!mDisplayDocument, "Shouldn't get here for resource document");
  MOZ_ASSERT(mOnloadBlockCount != 0,
             "Shouldn't have a count of zero here, since we stabilized in "
             "PostUnblockOnloadEvent");

  --mOnloadBlockCount;

  if (mOnloadBlockCount != 0) {
    // We blocked again after the last unblock.  Nothing to do here.  We'll
    // post a new event when we unblock again.
    return;
  }

  // If mScriptGlobalObject is null, we shouldn't be messing with the loadgroup
  // -- it's not ours.
  if (mScriptGlobalObject) {
    if (nsCOMPtr<nsILoadGroup> loadGroup = GetDocumentLoadGroup()) {
      loadGroup->RemoveRequest(mOnloadBlocker, nullptr, NS_OK);
    }
  }
}

nsIContent* Document::GetContentInThisDocument(nsIFrame* aFrame) const {
  for (nsIFrame* f = aFrame; f;
       f = nsLayoutUtils::GetParentOrPlaceholderForCrossDoc(f)) {
    nsIContent* content = f->GetContent();
    if (!content) {
      continue;
    }

    if (content->OwnerDoc() == this) {
      return content;
    }
    // We must be in a subdocument so jump directly to the root frame.
    // GetParentOrPlaceholderForCrossDoc gets called immediately to jump up to
    // the containing document.
    f = f->PresContext()->GetPresShell()->GetRootFrame();
  }

  return nullptr;
}

void Document::DispatchPageTransition(EventTarget* aDispatchTarget,
                                      const nsAString& aType, bool aInFrameSwap,
                                      bool aPersisted, bool aOnlySystemGroup) {
  if (!aDispatchTarget) {
    return;
  }

  PageTransitionEventInit init;
  init.mBubbles = true;
  init.mCancelable = true;
  init.mPersisted = aPersisted;
  init.mInFrameSwap = aInFrameSwap;

  RefPtr<PageTransitionEvent> event =
      PageTransitionEvent::Constructor(this, aType, init);

  event->SetTrusted(true);
  event->SetTarget(this);
  if (aOnlySystemGroup) {
    event->WidgetEventPtr()->mFlags.mOnlySystemGroupDispatchInContent = true;
  }
  EventDispatcher::DispatchDOMEvent(aDispatchTarget, nullptr, event, nullptr,
                                    nullptr);
}

void Document::OnPageShow(bool aPersisted, EventTarget* aDispatchStartTarget,
                          bool aOnlySystemGroup) {
  if (MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Debug)) {
    nsCString uri;
    if (GetDocumentURI()) {
      uri = GetDocumentURI()->GetSpecOrDefault();
    }
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
            ("Document::OnPageShow [%s] persisted=%i", uri.get(), aPersisted));
  }

  const bool inFrameLoaderSwap = !!aDispatchStartTarget;
  MOZ_DIAGNOSTIC_ASSERT(
      inFrameLoaderSwap ==
      (mDocumentContainer && mDocumentContainer->InFrameSwap()));

  Element* root = GetRootElement();
  if (aPersisted && root) {
    // Send out notifications that our <link> elements are attached.
    RefPtr<nsContentList> links =
        NS_GetContentList(root, kNameSpaceID_XHTML, u"link"_ns);

    uint32_t linkCount = links->Length(true);
    for (uint32_t i = 0; i < linkCount; ++i) {
      static_cast<HTMLLinkElement*>(links->Item(i, false))->LinkAdded();
    }
  }

  // See Document
  if (!inFrameLoaderSwap) {
    if (aPersisted) {
      ImageTracker()->SetAnimatingState(true);
    }

    // Set mIsShowing before firing events, in case those event handlers
    // move us around.
    mIsShowing = true;
    mVisible = true;

    UpdateVisibilityState();
  }

  NotifyActivityChanged();

  EnumerateExternalResources([aPersisted](Document& aExternalResource) {
    aExternalResource.OnPageShow(aPersisted, nullptr);
    return CallState::Continue;
  });

  if (mAnimationController) {
    mAnimationController->OnPageShow();
  }

  if (!mIsBeingUsedAsImage) {
    // Dispatch observer notification to notify observers page is shown.
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      nsIPrincipal* principal = NodePrincipal();
      os->NotifyObservers(ToSupports(this),
                          principal->IsSystemPrincipal() ? "chrome-page-shown"
                                                         : "content-page-shown",
                          nullptr);
    }

    nsCOMPtr<EventTarget> target = aDispatchStartTarget;
    if (!target) {
      target = do_QueryInterface(GetWindow());
    }
    DispatchPageTransition(target, u"pageshow"_ns, inFrameLoaderSwap,
                           aPersisted, aOnlySystemGroup);
  }

  if (auto* wgc = GetWindowGlobalChild()) {
    wgc->UnblockBFCacheFor(BFCacheStatus::PAGE_LOADING);
  }
}

static void DispatchFullscreenChange(Document& aDocument, nsINode* aTarget) {
  aDocument.AddPendingFullscreenEvent(
      MakeUnique<PendingFullscreenEvent>(FullscreenEventType::Change, aTarget));
}

void Document::OnPageHide(bool aPersisted, EventTarget* aDispatchStartTarget,
                          bool aOnlySystemGroup) {
  if (MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Debug)) {
    nsCString uri;
    if (GetDocumentURI()) {
      uri = GetDocumentURI()->GetSpecOrDefault();
    }
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
            ("Document::OnPageHide %s persisted=%i", uri.get(), aPersisted));
  }

  const bool inFrameLoaderSwap = !!aDispatchStartTarget;
  MOZ_DIAGNOSTIC_ASSERT(
      inFrameLoaderSwap ==
      (mDocumentContainer && mDocumentContainer->InFrameSwap()));

  if (mAnimationController) {
    mAnimationController->OnPageHide();
  }

  if (!inFrameLoaderSwap) {
    if (aPersisted) {
      // We do not stop the animations (bug 1024343) when the page is refreshing
      // while being dragged out.
      ImageTracker()->SetAnimatingState(false);
    }

    // Set mIsShowing before firing events, in case those event handlers
    // move us around.
    mIsShowing = false;
    mVisible = false;
  }

  PointerLockManager::Unlock("Document::OnPageHide", this);

  if (!mIsBeingUsedAsImage) {
    // Dispatch observer notification to notify observers page is hidden.
    nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
    if (os) {
      nsIPrincipal* principal = NodePrincipal();
      os->NotifyObservers(ToSupports(this),
                          principal->IsSystemPrincipal()
                              ? "chrome-page-hidden"
                              : "content-page-hidden",
                          nullptr);
    }

    // Now send out a PageHide event.
    nsCOMPtr<EventTarget> target = aDispatchStartTarget;
    if (!target) {
      target = do_QueryInterface(GetWindow());
    }
    {
      PageUnloadingEventTimeStamp timeStamp(this);
      DispatchPageTransition(target, u"pagehide"_ns, inFrameLoaderSwap,
                             aPersisted, aOnlySystemGroup);
    }
  }

  if (!inFrameLoaderSwap) {
    UpdateVisibilityState();
  }

  EnumerateExternalResources([aPersisted](Document& aExternalResource) {
    aExternalResource.OnPageHide(aPersisted, nullptr);
    return CallState::Continue;
  });
  NotifyActivityChanged();

  ClearPendingFullscreenRequests(this);
  if (Fullscreen()) {
    // If this document was fullscreen, we should exit fullscreen in this
    // doctree branch. This ensures that if the user navigates while in
    // fullscreen mode we don't leave its still visible ancestor documents
    // in fullscreen mode. So exit fullscreen in the document's fullscreen
    // root document, as this will exit fullscreen in all the root's
    // descendant documents. Note that documents are removed from the
    // doctree by the time OnPageHide() is called, so we must store a
    // reference to the root (in Document::mFullscreenRoot) since we can't
    // just traverse the doctree to get the root.
    Document::ExitFullscreenInDocTree(this);

    // Since the document is removed from the doctree before OnPageHide() is
    // called, ExitFullscreen() can't traverse from the root down to *this*
    // document, so we must manually call CleanupFullscreenState() below too.
    // Note that CleanupFullscreenState() clears Document::mFullscreenRoot,
    // so we *must* call it after ExitFullscreen(), not before.
    // OnPageHide() is called in every hidden (i.e. descendant) document,
    // so calling CleanupFullscreenState() here will ensure all hidden
    // documents have their fullscreen state reset.
    CleanupFullscreenState();

    // The fullscreenchange event is to be queued in the refresh driver,
    // however a hidden page wouldn't trigger that again, so it makes no
    // sense to dispatch such event here.
  }
}

void Document::WillDispatchMutationEvent(nsINode* aTarget) {
  NS_ASSERTION(
      mSubtreeModifiedDepth != 0 || mSubtreeModifiedTargets.Count() == 0,
      "mSubtreeModifiedTargets not cleared after dispatching?");
  ++mSubtreeModifiedDepth;
  if (aTarget) {
    // MayDispatchMutationEvent is often called just before this method,
    // so it has already appended the node to mSubtreeModifiedTargets.
    int32_t count = mSubtreeModifiedTargets.Count();
    if (!count || mSubtreeModifiedTargets[count - 1] != aTarget) {
      mSubtreeModifiedTargets.AppendObject(aTarget);
    }
  }
}

void Document::MutationEventDispatched(nsINode* aTarget) {
  if (--mSubtreeModifiedDepth) {
    return;
  }

  int32_t count = mSubtreeModifiedTargets.Count();
  if (!count) {
    return;
  }

  nsPIDOMWindowInner* window = GetInnerWindow();
  if (window &&
      !window->HasMutationListeners(NS_EVENT_BITS_MUTATION_SUBTREEMODIFIED)) {
    mSubtreeModifiedTargets.Clear();
    return;
  }

  nsCOMArray<nsINode> realTargets;
  for (nsINode* possibleTarget : mSubtreeModifiedTargets) {
    if (possibleTarget->ChromeOnlyAccess()) {
      continue;
    }

    nsINode* commonAncestor = nullptr;
    int32_t realTargetCount = realTargets.Count();
    for (int32_t j = 0; j < realTargetCount; ++j) {
      commonAncestor = nsContentUtils::GetClosestCommonInclusiveAncestor(
          possibleTarget, realTargets[j]);
      if (commonAncestor) {
        realTargets.ReplaceObjectAt(commonAncestor, j);
        break;
      }
    }
    if (!commonAncestor) {
      realTargets.AppendObject(possibleTarget);
    }
  }

  mSubtreeModifiedTargets.Clear();

  for (const nsCOMPtr<nsINode>& target : realTargets) {
    InternalMutationEvent mutation(true, eLegacySubtreeModified);
    // MOZ_KnownLive due to bug 1620312
    AsyncEventDispatcher::RunDOMEventWhenSafe(MOZ_KnownLive(*target), mutation);
  }
}

void Document::DestroyElementMaps() {
#ifdef DEBUG
  mStyledLinksCleared = true;
#endif
  mStyledLinks.Clear();
  // Notify ID change listeners before clearing the identifier map.
  for (auto iter = mIdentifierMap.Iter(); !iter.Done(); iter.Next()) {
    iter.Get()->ClearAndNotify();
  }
  mIdentifierMap.Clear();
  mComposedShadowRoots.Clear();
  mResponsiveContent.Clear();
  IncrementExpandoGeneration(*this);
}

void Document::RefreshLinkHrefs() {
  // Get a list of all links we know about.  We will reset them, which will
  // remove them from the document, so we need a copy of what is in the
  // hashtable.
  const nsTArray<Link*> linksToNotify = ToArray(mStyledLinks);

  // Reset all of our styled links.
  nsAutoScriptBlocker scriptBlocker;
  for (Link* link : linksToNotify) {
    link->ResetLinkState(true);
  }
}

nsresult Document::CloneDocHelper(Document* clone) const {
  clone->mIsStaticDocument = mCreatingStaticClone;

  // Init document
  nsresult rv = clone->Init(NodePrincipal(), mPartitionedPrincipal);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mCreatingStaticClone) {
    if (mOriginalDocument) {
      clone->mOriginalDocument = mOriginalDocument;
    } else {
      clone->mOriginalDocument = const_cast<Document*>(this);
    }
    clone->mOriginalDocument->mLatestStaticClone = clone;
    clone->mOriginalDocument->mStaticCloneCount++;

    nsCOMPtr<nsILoadGroup> loadGroup;

    // |mDocumentContainer| is the container of the document that is being
    // created and not the original container. See CreateStaticClone function().
    nsCOMPtr<nsIDocumentLoader> docLoader(mDocumentContainer);
    if (docLoader) {
      docLoader->GetLoadGroup(getter_AddRefs(loadGroup));
    }
    nsCOMPtr<nsIChannel> channel = GetChannel();
    nsCOMPtr<nsIURI> uri;
    if (channel) {
      NS_GetFinalChannelURI(channel, getter_AddRefs(uri));
    } else {
      uri = Document::GetDocumentURI();
    }
    clone->mChannel = channel;
    clone->mShouldResistFingerprinting = mShouldResistFingerprinting;
    if (uri) {
      clone->ResetToURI(uri, loadGroup, NodePrincipal(), mPartitionedPrincipal);
    }

    clone->mIsSrcdocDocument = mIsSrcdocDocument;
    clone->SetContainer(mDocumentContainer);

    // Setup the navigation time. This will be needed by any animations in the
    // document, even if they are only paused.
    MOZ_ASSERT(!clone->GetNavigationTiming(),
               "Navigation time was already set?");
    if (mTiming) {
      RefPtr<nsDOMNavigationTiming> timing =
          mTiming->CloneNavigationTime(nsDocShell::Cast(clone->GetDocShell()));
      clone->SetNavigationTiming(timing);
    }
    clone->SetCsp(mCSP);
  }

  // Now ensure that our clone has the same URI, base URI, and principal as us.
  // We do this after the mCreatingStaticClone block above, because that block
  // can set the base URI to an incorrect value in cases when base URI
  // information came from the channel.  So we override explicitly, and do it
  // for all these properties, in case ResetToURI messes with any of the rest of
  // them.
  clone->SetDocumentURI(Document::GetDocumentURI());
  clone->SetChromeXHRDocURI(mChromeXHRDocURI);
  clone->mActiveStoragePrincipal = mActiveStoragePrincipal;
  clone->mActiveCookiePrincipal = mActiveCookiePrincipal;
  // NOTE(emilio): Intentionally setting this to the GetDocBaseURI rather than
  // just mDocumentBaseURI, so that srcdoc iframes get the right base URI even
  // when printed standalone via window.print() (where there won't be a parent
  // document to grab the URI from).
  clone->mDocumentBaseURI = GetDocBaseURI();
  clone->SetChromeXHRDocBaseURI(mChromeXHRDocBaseURI);
  clone->mReferrerInfo =
      static_cast<dom::ReferrerInfo*>(mReferrerInfo.get())->Clone();
  clone->mPreloadReferrerInfo = clone->mReferrerInfo;

  bool hasHadScriptObject = true;
  nsIScriptGlobalObject* scriptObject =
      GetScriptHandlingObject(hasHadScriptObject);
  NS_ENSURE_STATE(scriptObject || !hasHadScriptObject);
  if (mCreatingStaticClone) {
    // If we're doing a static clone (print, print preview), then we're going to
    // be setting a scope object after the clone. It's better to set it only
    // once, so we don't do that here. However, we do want to act as if there is
    // a script handling object. So we set mHasHadScriptHandlingObject.
    clone->mHasHadScriptHandlingObject = true;
  } else if (scriptObject) {
    clone->SetScriptHandlingObject(scriptObject);
  } else {
    clone->SetScopeObject(GetScopeObject());
  }
  // Make the clone a data document
  clone->SetLoadedAsData(
      true,
      /* aConsiderForMemoryReporting */ !mCreatingStaticClone);

  // Misc state

  // State from Document
  clone->mCharacterSet = mCharacterSet;
  clone->mCharacterSetSource = mCharacterSetSource;
  clone->SetCompatibilityMode(mCompatMode);
  clone->mBidiOptions = mBidiOptions;
  clone->mContentLanguage = mContentLanguage;
  clone->SetContentType(GetContentTypeInternal());
  clone->mSecurityInfo = mSecurityInfo;

  // State from Document
  clone->mType = mType;
  clone->mXMLDeclarationBits = mXMLDeclarationBits;
  clone->mBaseTarget = mBaseTarget;

  return NS_OK;
}

void Document::NotifyLoading(bool aNewParentIsLoading,
                             const ReadyState& aCurrentState,
                             ReadyState aNewState) {
  // Mirror the top-level loading state down to all subdocuments
  bool was_loading = mAncestorIsLoading ||
                     aCurrentState == READYSTATE_LOADING ||
                     aCurrentState == READYSTATE_INTERACTIVE;
  bool is_loading = aNewParentIsLoading || aNewState == READYSTATE_LOADING ||
                    aNewState == READYSTATE_INTERACTIVE;  // new value for state
  bool set_load_state = was_loading != is_loading;

  MOZ_LOG(
      gTimeoutDeferralLog, mozilla::LogLevel::Debug,
      ("NotifyLoading for doc %p: currentAncestor: %d, newParent: %d, "
       "currentState %d newState: %d, was_loading: %d, is_loading: %d, "
       "set_load_state: %d",
       (void*)this, mAncestorIsLoading, aNewParentIsLoading, (int)aCurrentState,
       (int)aNewState, was_loading, is_loading, set_load_state));

  mAncestorIsLoading = aNewParentIsLoading;
  if (set_load_state && StaticPrefs::dom_timeout_defer_during_load() &&
      !NodePrincipal()->IsURIInPrefList(
          "dom.timeout.defer_during_load.force-disable")) {
    // Tell our innerwindow (and thus TimeoutManager)
    nsPIDOMWindowInner* inner = GetInnerWindow();
    if (inner) {
      inner->SetActiveLoadingState(is_loading);
    }
    BrowsingContext* context = GetBrowsingContext();
    if (context) {
      // Don't use PreOrderWalk to mirror this down; go down one level as a
      // time so we can set mAncestorIsLoading and take into account the
      // readystates of the subdocument.  In the child process it will call
      // NotifyLoading() to notify the innerwindow/TimeoutManager, and then
      // iterate it's children
      for (auto& child : context->Children()) {
        MOZ_LOG(gTimeoutDeferralLog, mozilla::LogLevel::Debug,
                ("bc: %p SetAncestorLoading(%d)", (void*)child, is_loading));
        // Setting ancestor loading on a discarded browsing context has no
        // effect.
        Unused << child->SetAncestorLoading(is_loading);
      }
    }
  }
}

void Document::SetReadyStateInternal(ReadyState aReadyState,
                                     bool aUpdateTimingInformation) {
  if (aReadyState == READYSTATE_UNINITIALIZED) {
    // Transition back to uninitialized happens only to keep assertions happy
    // right before readyState transitions to something else. Make this
    // transition undetectable by Web content.
    mReadyState = aReadyState;
    return;
  }

  if (IsTopLevelContentDocument()) {
    if (aReadyState == READYSTATE_LOADING) {
      AddToplevelLoadingDocument(this);
    } else if (aReadyState == READYSTATE_COMPLETE) {
      RemoveToplevelLoadingDocument(this);
    }
  }

  if (aUpdateTimingInformation && READYSTATE_LOADING == aReadyState) {
    SetLoadingOrRestoredFromBFCacheTimeStampToNow();
  }
  NotifyLoading(mAncestorIsLoading, mReadyState, aReadyState);
  mReadyState = aReadyState;
  if (aUpdateTimingInformation && mTiming) {
    switch (aReadyState) {
      case READYSTATE_LOADING:
        mTiming->NotifyDOMLoading(GetDocumentURI());
        break;
      case READYSTATE_INTERACTIVE:
        mTiming->NotifyDOMInteractive(GetDocumentURI());
        break;
      case READYSTATE_COMPLETE:
        mTiming->NotifyDOMComplete(GetDocumentURI());
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected ReadyState value");
        break;
    }
  }
  // At the time of loading start, we don't have timing object, record time.

  if (READYSTATE_INTERACTIVE == aReadyState &&
      NodePrincipal()->IsSystemPrincipal()) {
    if (!mXULPersist && XRE_IsParentProcess()) {
      mXULPersist = new XULPersist(this);
      mXULPersist->Init();
    }
    if (!mChromeObserver) {
      mChromeObserver = new ChromeObserver(this);
      mChromeObserver->Init();
    }
  }

  if (aUpdateTimingInformation) {
    RecordNavigationTiming(aReadyState);
  }

  AsyncEventDispatcher::RunDOMEventWhenSafe(
      *this, u"readystatechange"_ns, CanBubble::eNo, ChromeOnlyDispatch::eNo);
}

void Document::GetReadyState(nsAString& aReadyState) const {
  switch (mReadyState) {
    case READYSTATE_LOADING:
      aReadyState.AssignLiteral(u"loading");
      break;
    case READYSTATE_INTERACTIVE:
      aReadyState.AssignLiteral(u"interactive");
      break;
    case READYSTATE_COMPLETE:
      aReadyState.AssignLiteral(u"complete");
      break;
    default:
      aReadyState.AssignLiteral(u"uninitialized");
  }
}

void Document::SuppressEventHandling(uint32_t aIncrease) {
  mEventsSuppressed += aIncrease;
  if (mEventsSuppressed == aIncrease) {
    if (WindowGlobalChild* wgc = GetWindowGlobalChild()) {
      wgc->BlockBFCacheFor(BFCacheStatus::EVENT_HANDLING_SUPPRESSED);
    }
  }
  for (uint32_t i = 0; i < aIncrease; ++i) {
    ScriptLoader()->AddExecuteBlocker();
  }

  EnumerateSubDocuments([aIncrease](Document& aSubDoc) {
    aSubDoc.SuppressEventHandling(aIncrease);
    return CallState::Continue;
  });
}

void Document::NotifyAbortedLoad() {
  // If we still have outstanding work blocking DOMContentLoaded,
  // then don't try to change the readystate now, but wait until
  // they finish and then do so.
  if (mBlockDOMContentLoaded > 0 && !mDidFireDOMContentLoaded) {
    mSetCompleteAfterDOMContentLoaded = true;
    return;
  }

  // Otherwise we're fully done at this point, so set the
  // readystate to complete.
  if (GetReadyStateEnum() == Document::READYSTATE_INTERACTIVE) {
    SetReadyStateInternal(Document::READYSTATE_COMPLETE);
  }
}

MOZ_CAN_RUN_SCRIPT static void FireOrClearDelayedEvents(
    nsTArray<nsCOMPtr<Document>>&& aDocuments, bool aFireEvents) {
  RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager();
  if (MOZ_UNLIKELY(!fm)) {
    return;
  }

  nsTArray<nsCOMPtr<Document>> documents = std::move(aDocuments);
  for (uint32_t i = 0; i < documents.Length(); ++i) {
    nsCOMPtr<Document> document = std::move(documents[i]);
    // NB: Don't bother trying to fire delayed events on documents that were
    // closed before this event ran.
    if (!document->EventHandlingSuppressed()) {
      fm->FireDelayedEvents(document);
      RefPtr<PresShell> presShell = document->GetPresShell();
      if (presShell) {
        // Only fire events for active documents.
        bool fire = aFireEvents && document->GetInnerWindow() &&
                    document->GetInnerWindow()->IsCurrentInnerWindow();
        presShell->FireOrClearDelayedEvents(fire);
      }
      document->FireOrClearPostMessageEvents(aFireEvents);
    }
  }
}

void Document::PreloadPictureClosed() {
  MOZ_ASSERT(mPreloadPictureDepth > 0);
  mPreloadPictureDepth--;
  if (mPreloadPictureDepth == 0) {
    mPreloadPictureFoundSource.SetIsVoid(true);
  }
}

void Document::PreloadPictureImageSource(const nsAString& aSrcsetAttr,
                                         const nsAString& aSizesAttr,
                                         const nsAString& aTypeAttr,
                                         const nsAString& aMediaAttr) {
  // Nested pictures are not valid syntax, so while we'll eventually load them,
  // it's not worth tracking sources mixed between nesting levels to preload
  // them effectively.
  if (mPreloadPictureDepth == 1 && mPreloadPictureFoundSource.IsVoid()) {
    // <picture> selects the first matching source, so if this returns a URI we
    // needn't consider new sources until a new <picture> is encountered.
    bool found = HTMLImageElement::SelectSourceForTagWithAttrs(
        this, true, VoidString(), aSrcsetAttr, aSizesAttr, aTypeAttr,
        aMediaAttr, mPreloadPictureFoundSource);
    if (found && mPreloadPictureFoundSource.IsVoid()) {
      // Found an empty source, which counts
      mPreloadPictureFoundSource.SetIsVoid(false);
    }
  }
}

already_AddRefed<nsIURI> Document::ResolvePreloadImage(
    nsIURI* aBaseURI, const nsAString& aSrcAttr, const nsAString& aSrcsetAttr,
    const nsAString& aSizesAttr, bool* aIsImgSet) {
  nsString sourceURL;
  bool isImgSet;
  if (mPreloadPictureDepth == 1 && !mPreloadPictureFoundSource.IsVoid()) {
    // We're in a <picture> element and found a URI from a source previous to
    // this image, use it.
    sourceURL = mPreloadPictureFoundSource;
    isImgSet = true;
  } else {
    // Otherwise try to use this <img> as a source
    HTMLImageElement::SelectSourceForTagWithAttrs(
        this, false, aSrcAttr, aSrcsetAttr, aSizesAttr, VoidString(),
        VoidString(), sourceURL);
    isImgSet = !aSrcsetAttr.IsEmpty();
  }

  // Empty sources are not loaded by <img> (i.e. not resolved to the baseURI)
  if (sourceURL.IsEmpty()) {
    return nullptr;
  }

  // Construct into URI using passed baseURI (the parser may know of base URI
  // changes that have not reached us)
  nsresult rv;
  nsCOMPtr<nsIURI> uri;
  rv = nsContentUtils::NewURIWithDocumentCharset(getter_AddRefs(uri), sourceURL,
                                                 this, aBaseURI);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  *aIsImgSet = isImgSet;

  // We don't clear mPreloadPictureFoundSource because subsequent <img> tags in
  // this this <picture> share the same <sources> (though this is not valid per
  // spec)
  return uri.forget();
}

void Document::PreLoadImage(nsIURI* aUri, const nsAString& aCrossOriginAttr,
                            ReferrerPolicyEnum aReferrerPolicy, bool aIsImgSet,
                            bool aLinkPreload, uint64_t aEarlyHintPreloaderId,
                            const nsAString& aFetchPriority) {
  nsLoadFlags loadFlags = nsIRequest::LOAD_NORMAL |
                          nsContentUtils::CORSModeToLoadImageFlags(
                              Element::StringToCORSMode(aCrossOriginAttr));

  nsContentPolicyType policyType =
      aIsImgSet ? nsIContentPolicy::TYPE_IMAGESET
                : nsIContentPolicy::TYPE_INTERNAL_IMAGE_PRELOAD;

  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      ReferrerInfo::CreateFromDocumentAndPolicyOverride(this, aReferrerPolicy);

  RefPtr<imgRequestProxy> request;

  nsLiteralString initiator = aEarlyHintPreloaderId
                                  ? u"early-hints"_ns
                                  : (aLinkPreload ? u"link"_ns : u"img"_ns);

  nsresult rv = nsContentUtils::LoadImage(
      aUri, static_cast<nsINode*>(this), this, NodePrincipal(), 0, referrerInfo,
      nullptr /* no observer */, loadFlags, initiator, getter_AddRefs(request),
      policyType, false /* urgent */, aLinkPreload, aEarlyHintPreloaderId,
      nsGenericHTMLElement::ToFetchPriority(aFetchPriority));

  // Pin image-reference to avoid evicting it from the img-cache before
  // the "real" load occurs. Unpinned in DispatchContentLoadedEvents and
  // unlink
  if (!aLinkPreload && NS_SUCCEEDED(rv)) {
    mPreloadingImages.InsertOrUpdate(aUri, std::move(request));
  }
}

void Document::MaybePreLoadImage(nsIURI* aUri,
                                 const nsAString& aCrossOriginAttr,
                                 ReferrerPolicyEnum aReferrerPolicy,
                                 bool aIsImgSet, bool aLinkPreload,
                                 const nsAString& aFetchPriority) {
  const CORSMode corsMode = dom::Element::StringToCORSMode(aCrossOriginAttr);
  if (aLinkPreload) {
    // Check if the image was already preloaded in this document to avoid
    // duplicate preloading.
    PreloadHashKey key =
        PreloadHashKey::CreateAsImage(aUri, NodePrincipal(), corsMode);
    if (!mPreloadService.PreloadExists(key)) {
      PreLoadImage(aUri, aCrossOriginAttr, aReferrerPolicy, aIsImgSet,
                   aLinkPreload, 0, aFetchPriority);
    }
    return;
  }

  // Early exit if the img is already present in the img-cache
  // which indicates that the "real" load has already started and
  // that we shouldn't preload it.
  if (nsContentUtils::IsImageAvailable(aUri, NodePrincipal(), corsMode, this)) {
    return;
  }

  // Image not in cache - trigger preload
  PreLoadImage(aUri, aCrossOriginAttr, aReferrerPolicy, aIsImgSet, aLinkPreload,
               0, aFetchPriority);
}

void Document::MaybePreconnect(nsIURI* aOrigURI, mozilla::CORSMode aCORSMode) {
  if (!StaticPrefs::network_preconnect()) {
    return;
  }

  NS_MutateURI mutator(aOrigURI);
  if (NS_FAILED(mutator.GetStatus())) {
    return;
  }

  // The URI created here is used in 2 contexts. One is nsISpeculativeConnect
  // which ignores the path and uses only the origin. The other is for the
  // document mPreloadedPreconnects de-duplication hash. Anonymous vs
  // non-Anonymous preconnects create different connections on the wire and
  // therefore should not be considred duplicates of each other and we
  // normalize the path before putting it in the hash to accomplish that.

  if (aCORSMode == CORS_ANONYMOUS) {
    mutator.SetPathQueryRef("/anonymous"_ns);
  } else {
    mutator.SetPathQueryRef("/"_ns);
  }

  nsCOMPtr<nsIURI> uri;
  nsresult rv = mutator.Finalize(uri);
  if (NS_FAILED(rv)) {
    return;
  }

  const bool existingEntryFound =
      mPreloadedPreconnects.WithEntryHandle(uri, [](auto&& entry) {
        if (entry) {
          return true;
        }
        entry.Insert(true);
        return false;
      });
  if (existingEntryFound) {
    return;
  }

  nsCOMPtr<nsISpeculativeConnect> speculator =
      mozilla::components::IO::Service();
  if (!speculator) {
    return;
  }

  OriginAttributes oa;
  StoragePrincipalHelper::GetOriginAttributesForNetworkState(this, oa);
  speculator->SpeculativeConnectWithOriginAttributesNative(
      uri, std::move(oa), nullptr, aCORSMode == CORS_ANONYMOUS);
}

void Document::ForgetImagePreload(nsIURI* aURI) {
  // Checking count is faster than hashing the URI in the common
  // case of empty table.
  if (mPreloadingImages.Count() != 0) {
    nsCOMPtr<imgIRequest> req;
    mPreloadingImages.Remove(aURI, getter_AddRefs(req));
    if (req) {
      // Make sure to cancel the request so imagelib knows it's gone.
      req->CancelAndForgetObserver(NS_BINDING_ABORTED);
    }
  }
}

void Document::UpdateDocumentStates(DocumentState aMaybeChangedStates,
                                    bool aNotify) {
  const DocumentState oldStates = mState;
  if (aMaybeChangedStates.HasAtLeastOneOfStates(
          DocumentState::ALL_LOCALEDIR_BITS)) {
    mState &= ~DocumentState::ALL_LOCALEDIR_BITS;
    if (IsDocumentRightToLeft()) {
      mState |= DocumentState::RTL_LOCALE;
    } else {
      mState |= DocumentState::LTR_LOCALE;
    }
  }

  if (aMaybeChangedStates.HasState(DocumentState::WINDOW_INACTIVE)) {
    BrowsingContext* bc = GetBrowsingContext();
    if (!bc || !bc->GetIsActiveBrowserWindow()) {
      mState |= DocumentState::WINDOW_INACTIVE;
    } else {
      mState &= ~DocumentState::WINDOW_INACTIVE;
    }
  }

  const DocumentState changedStates = oldStates ^ mState;
  if (aNotify && !changedStates.IsEmpty()) {
    if (PresShell* ps = GetObservingPresShell()) {
      ps->DocumentStatesChanged(changedStates);
    }
  }
}

namespace {

/**
 * Stub for LoadSheet(), since all we want is to get the sheet into
 * the CSSLoader's style cache
 */
class StubCSSLoaderObserver final : public nsICSSLoaderObserver {
  ~StubCSSLoaderObserver() = default;

 public:
  NS_IMETHOD
  StyleSheetLoaded(StyleSheet*, bool, nsresult) override { return NS_OK; }
  NS_DECL_ISUPPORTS
};
NS_IMPL_ISUPPORTS(StubCSSLoaderObserver, nsICSSLoaderObserver)

}  // namespace

SheetPreloadStatus Document::PreloadStyle(
    nsIURI* uri, const Encoding* aEncoding, const nsAString& aCrossOriginAttr,
    const enum ReferrerPolicy aReferrerPolicy, const nsAString& aNonce,
    const nsAString& aIntegrity, css::StylePreloadKind aKind,
    uint64_t aEarlyHintPreloaderId, const nsAString& aFetchPriority) {
  MOZ_ASSERT(aKind != css::StylePreloadKind::None);

  // The CSSLoader will retain this object after we return.
  nsCOMPtr<nsICSSLoaderObserver> obs = new StubCSSLoaderObserver();

  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      ReferrerInfo::CreateFromDocumentAndPolicyOverride(this, aReferrerPolicy);

  // Charset names are always ASCII.
  auto result = CSSLoader()->LoadSheet(
      uri, aKind, aEncoding, referrerInfo, obs, aEarlyHintPreloaderId,
      Element::StringToCORSMode(aCrossOriginAttr), aNonce, aIntegrity,
      nsGenericHTMLElement::ToFetchPriority(aFetchPriority));
  if (result.isErr()) {
    return SheetPreloadStatus::Errored;
  }
  RefPtr<StyleSheet> sheet = result.unwrap();
  if (sheet->IsComplete()) {
    return SheetPreloadStatus::AlreadyComplete;
  }
  return SheetPreloadStatus::InProgress;
}

RefPtr<StyleSheet> Document::LoadChromeSheetSync(nsIURI* uri) {
  return CSSLoader()
      ->LoadSheetSync(uri, css::eAuthorSheetFeatures)
      .unwrapOr(nullptr);
}

void Document::ResetDocumentDirection() {
  if (!nsContentUtils::IsChromeDoc(this)) {
    return;
  }
  UpdateDocumentStates(DocumentState::ALL_LOCALEDIR_BITS, true);
}

bool Document::IsDocumentRightToLeft() {
  if (!nsContentUtils::IsChromeDoc(this)) {
    return false;
  }
  // setting the localedir attribute on the root element forces a
  // specific direction for the document.
  Element* element = GetRootElement();
  if (element) {
    static Element::AttrValuesArray strings[] = {nsGkAtoms::ltr, nsGkAtoms::rtl,
                                                 nullptr};
    switch (element->FindAttrValueIn(kNameSpaceID_None, nsGkAtoms::localedir,
                                     strings, eCaseMatters)) {
      case 0:
        return false;
      case 1:
        return true;
      default:
        break;  // otherwise, not a valid value, so fall through
    }
  }

  if (!mDocumentURI->SchemeIs("chrome") && !mDocumentURI->SchemeIs("about") &&
      !mDocumentURI->SchemeIs("resource")) {
    return false;
  }

  return intl::LocaleService::GetInstance()->IsAppLocaleRTL();
}

class nsDelayedEventDispatcher : public Runnable {
 public:
  explicit nsDelayedEventDispatcher(nsTArray<nsCOMPtr<Document>>&& aDocuments)
      : mozilla::Runnable("nsDelayedEventDispatcher"),
        mDocuments(std::move(aDocuments)) {}
  virtual ~nsDelayedEventDispatcher() = default;

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.  See
  // bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHOD Run() override {
    FireOrClearDelayedEvents(std::move(mDocuments), true);
    return NS_OK;
  }

 private:
  nsTArray<nsCOMPtr<Document>> mDocuments;
};

static void GetAndUnsuppressSubDocuments(
    Document& aDocument, nsTArray<nsCOMPtr<Document>>& aDocuments) {
  if (aDocument.EventHandlingSuppressed() > 0) {
    aDocument.DecreaseEventSuppression();
    aDocument.ScriptLoader()->RemoveExecuteBlocker();
  }
  aDocuments.AppendElement(&aDocument);
  aDocument.EnumerateSubDocuments([&aDocuments](Document& aSubDoc) {
    GetAndUnsuppressSubDocuments(aSubDoc, aDocuments);
    return CallState::Continue;
  });
}

void Document::UnsuppressEventHandlingAndFireEvents(bool aFireEvents) {
  nsTArray<nsCOMPtr<Document>> documents;
  GetAndUnsuppressSubDocuments(*this, documents);

  for (nsCOMPtr<Document>& doc : documents) {
    if (!doc->EventHandlingSuppressed()) {
      if (WindowGlobalChild* wgc = doc->GetWindowGlobalChild()) {
        wgc->UnblockBFCacheFor(BFCacheStatus::EVENT_HANDLING_SUPPRESSED);
      }

      MOZ_ASSERT(NS_IsMainThread());
      nsTArray<RefPtr<net::ChannelEventQueue>> queues =
          std::move(doc->mSuspendedQueues);
      for (net::ChannelEventQueue* queue : queues) {
        queue->Resume();
      }
    }
  }

  if (aFireEvents) {
    MOZ_RELEASE_ASSERT(NS_IsMainThread());
    nsCOMPtr<nsIRunnable> ded =
        new nsDelayedEventDispatcher(std::move(documents));
    Dispatch(ded.forget());
  } else {
    FireOrClearDelayedEvents(std::move(documents), false);
  }
}

bool Document::AreClipboardCommandsUnconditionallyEnabled() const {
  return IsHTMLOrXHTML() && !nsContentUtils::IsChromeDoc(this);
}

void Document::AddSuspendedChannelEventQueue(net::ChannelEventQueue* aQueue) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(EventHandlingSuppressed());
  mSuspendedQueues.AppendElement(aQueue);
}

bool Document::SuspendPostMessageEvent(PostMessageEvent* aEvent) {
  MOZ_ASSERT(NS_IsMainThread());

  if (EventHandlingSuppressed() || !mSuspendedPostMessageEvents.IsEmpty()) {
    mSuspendedPostMessageEvents.AppendElement(aEvent);
    return true;
  }
  return false;
}

void Document::FireOrClearPostMessageEvents(bool aFireEvents) {
  nsTArray<RefPtr<PostMessageEvent>> events =
      std::move(mSuspendedPostMessageEvents);

  if (aFireEvents) {
    for (PostMessageEvent* event : events) {
      event->Run();
    }
  }
}

void Document::SetSuppressedEventListener(EventListener* aListener) {
  mSuppressedEventListener = aListener;
  EnumerateSubDocuments([&](Document& aDocument) {
    aDocument.SetSuppressedEventListener(aListener);
    return CallState::Continue;
  });
}

bool Document::IsActive() const {
  return mDocumentContainer && !mRemovedFromDocShell && GetBrowsingContext() &&
         !GetBrowsingContext()->IsInBFCache();
}

bool Document::HasBeenScrolled() const {
  nsGlobalWindowInner* window = nsGlobalWindowInner::Cast(GetInnerWindow());
  if (!window) {
    return false;
  }
  if (ScrollContainerFrame* frame = window->GetScrollContainerFrame()) {
    return frame->HasBeenScrolled();
  }

  return false;
}

bool Document::CanRewriteURL(nsIURI* aTargetURL) const {
  if (nsContentUtils::URIIsLocalFile(aTargetURL)) {
    // It's a file:// URI
    nsCOMPtr<nsIPrincipal> principal = NodePrincipal();
    return NS_SUCCEEDED(principal->CheckMayLoadWithReporting(aTargetURL, false,
                                                             InnerWindowID()));
  }

  nsCOMPtr<nsIScriptSecurityManager> secMan =
      do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID);
  if (!secMan) {
    return false;
  }

  // It's very important that we check that aTargetURL is of the same
  // origin as mDocumentURI, not docBaseURI, because a page can
  // set docBaseURI arbitrarily to any domain.
  bool isPrivateWin =
      NodePrincipal()->OriginAttributesRef().IsPrivateBrowsing();
  if (NS_FAILED(secMan->CheckSameOriginURI(mDocumentURI, aTargetURL, true,
                                           isPrivateWin))) {
    return false;
  }

  // In addition to checking that the security manager says that
  // the new URI has the same origin as our current URI, we also
  // check that the two URIs have the same userpass. (The
  // security manager says that |http://foo.com| and
  // |http://me@foo.com| have the same origin.)  mDocumentURI
  // won't contain the password part of the userpass, so this
  // means that it's never valid to specify a password in a
  // pushState or replaceState URI.
  nsAutoCString currentUserPass, newUserPass;
  (void)mDocumentURI->GetUserPass(currentUserPass);
  (void)aTargetURL->GetUserPass(newUserPass);

  return currentUserPass.Equals(newUserPass);
}

nsISupports* Document::GetCurrentContentSink() {
  return mParser ? mParser->GetContentSink() : nullptr;
}

Document* Document::GetTemplateContentsOwner() {
  if (!mTemplateContentsOwner) {
    bool hasHadScriptObject = true;
    nsIScriptGlobalObject* scriptObject =
        GetScriptHandlingObject(hasHadScriptObject);

    nsCOMPtr<Document> document;
    nsresult rv = NS_NewDOMDocument(
        getter_AddRefs(document),
        u""_ns,   // aNamespaceURI
        u""_ns,   // aQualifiedName
        nullptr,  // aDoctype
        Document::GetDocumentURI(), Document::GetDocBaseURI(), NodePrincipal(),
        true,          // aLoadedAsData
        scriptObject,  // aEventObject
        IsHTMLDocument() ? DocumentFlavor::HTML : DocumentFlavor::XML);
    NS_ENSURE_SUCCESS(rv, nullptr);

    mTemplateContentsOwner = document;
    NS_ENSURE_TRUE(mTemplateContentsOwner, nullptr);

    if (!scriptObject) {
      mTemplateContentsOwner->SetScopeObject(GetScopeObject());
    }

    mTemplateContentsOwner->mHasHadScriptHandlingObject = hasHadScriptObject;

    // Set |mTemplateContentsOwner| as the template contents owner of itself so
    // that it is the template contents owner of nested template elements.
    mTemplateContentsOwner->mTemplateContentsOwner = mTemplateContentsOwner;
  }

  MOZ_ASSERT(mTemplateContentsOwner->IsTemplateContentsOwner());
  return mTemplateContentsOwner;
}

// https://html.spec.whatwg.org/#the-autofocus-attribute
void Document::ElementWithAutoFocusInserted(Element* aAutoFocusCandidate) {
  BrowsingContext* bc = GetBrowsingContext();
  if (!bc) {
    return;
  }

  // If target is not fully active, then return.
  if (!IsCurrentActiveDocument()) {
    return;
  }

  // If target's active sandboxing flag set has the sandboxed automatic features
  // browsing context flag, then return.
  if (GetSandboxFlags() & SANDBOXED_AUTOMATIC_FEATURES) {
    return;
  }

  // For each ancestorBC of target's browsing context's ancestor browsing
  // contexts: if ancestorBC's active document's origin is not same origin with
  // target's origin, then return.
  while (bc) {
    BrowsingContext* parent = bc->GetParent();
    if (!parent) {
      break;
    }
    // AncestorBC is not the same site
    if (!parent->IsInProcess()) {
      return;
    }

    Document* currentDocument = bc->GetDocument();
    if (!currentDocument) {
      return;
    }

    Document* parentDocument = parent->GetDocument();
    if (!parentDocument) {
      return;
    }

    // Not same origin
    if (!currentDocument->NodePrincipal()->Equals(
            parentDocument->NodePrincipal())) {
      return;
    }

    bc = parent;
  }
  MOZ_ASSERT(bc->IsTop());

  Document* topDocument = bc->GetDocument();
  MOZ_ASSERT(topDocument);
  topDocument->AppendAutoFocusCandidateToTopDocument(aAutoFocusCandidate);
}

void Document::ScheduleFlushAutoFocusCandidates() {
  MOZ_ASSERT(HasAutoFocusCandidates());
  MaybeScheduleRenderingPhases({RenderingPhase::FlushAutoFocusCandidates});
}

void Document::AppendAutoFocusCandidateToTopDocument(
    Element* aAutoFocusCandidate) {
  MOZ_ASSERT(GetBrowsingContext()->IsTop());
  if (mAutoFocusFired) {
    return;
  }

  const bool hadCandidates = HasAutoFocusCandidates();
  nsWeakPtr element = do_GetWeakReference(aAutoFocusCandidate);
  mAutoFocusCandidates.RemoveElement(element);
  mAutoFocusCandidates.AppendElement(element);
  if (!hadCandidates) {
    ScheduleFlushAutoFocusCandidates();
  }
}

void Document::SetAutoFocusFired() {
  mAutoFocusCandidates.Clear();
  mAutoFocusFired = true;
}

// https://html.spec.whatwg.org/#flush-autofocus-candidates
void Document::FlushAutoFocusCandidates() {
  MOZ_ASSERT(GetBrowsingContext()->IsTop());
  if (mAutoFocusFired) {
    return;
  }

  if (!mPresShell) {
    return;
  }

  MOZ_ASSERT(HasAutoFocusCandidates());
  MOZ_ASSERT(mPresShell->DidInitialize());

  nsCOMPtr<nsPIDOMWindowOuter> topWindow = GetWindow();
  // We should be the top document
  if (!topWindow) {
    return;
  }

#ifdef DEBUG
  {
    // Trying to find the top window (equivalent to window.top).
    nsCOMPtr<nsPIDOMWindowOuter> top = topWindow->GetInProcessTop();
    MOZ_ASSERT(topWindow == top);
  }
#endif

  // Don't steal the focus from the user
  if (topWindow->GetFocusedElement()) {
    SetAutoFocusFired();
    return;
  }

  MOZ_ASSERT(mDocumentURI);
  nsAutoCString ref;
  // GetRef never fails
  nsresult rv = mDocumentURI->GetRef(ref);
  if (NS_SUCCEEDED(rv) &&
      nsContentUtils::GetTargetElement(this, NS_ConvertUTF8toUTF16(ref))) {
    SetAutoFocusFired();
    return;
  }

  nsTObserverArray<nsWeakPtr>::ForwardIterator iter(mAutoFocusCandidates);
  while (iter.HasMore()) {
    nsCOMPtr<Element> autoFocusElement = do_QueryReferent(iter.GetNext());
    if (!autoFocusElement) {
      continue;
    }
    RefPtr<Document> autoFocusElementDoc = autoFocusElement->OwnerDoc();
    // Get the latest info about the frame and allow scripts
    // to run which might affect the focusability of this element.
    autoFocusElementDoc->FlushPendingNotifications(FlushType::Frames);

    // Above layout flush may cause the PresShell to disappear.
    if (!mPresShell) {
      return;
    }

    // Re-get the element because the ownerDoc() might have changed
    autoFocusElementDoc = autoFocusElement->OwnerDoc();
    BrowsingContext* bc = autoFocusElementDoc->GetBrowsingContext();
    if (!bc) {
      continue;
    }

    // If doc is not fully active, then remove element from candidates, and
    // continue.
    if (!autoFocusElementDoc->IsCurrentActiveDocument()) {
      iter.Remove();
      continue;
    }

    nsCOMPtr<nsIContentSink> sink =
        do_QueryInterface(autoFocusElementDoc->GetCurrentContentSink());
    if (sink) {
      nsHtml5TreeOpExecutor* executor =
          static_cast<nsHtml5TreeOpExecutor*>(sink->AsExecutor());
      if (executor) {
        // This is a HTML5 document
        MOZ_ASSERT(autoFocusElementDoc->IsHTMLDocument());
        // If doc's script-blocking style sheet counter is greater than 0, th
        // return.
        if (executor->WaitForPendingSheets()) {
          // In this case, element is the currently-best candidate, but doc is
          // not ready for autofocusing. We'll try again next time flush
          // autofocus candidates is called.
          ScheduleFlushAutoFocusCandidates();
          return;
        }
      }
    }

    // The autofocus element could be moved to a different
    // top level BC.
    if (bc->Top()->GetDocument() != this) {
      continue;
    }

    iter.Remove();

    // Let inclusiveAncestorDocuments be a list consisting of doc, plus the
    // active documents of each of doc's browsing context's ancestor browsing
    // contexts.
    // If any Document in inclusiveAncestorDocuments has non-null target
    // element, then continue.
    bool shouldFocus = true;
    while (bc) {
      Document* doc = bc->GetDocument();
      if (!doc) {
        shouldFocus = false;
        break;
      }

      nsIURI* uri = doc->GetDocumentURI();
      if (!uri) {
        shouldFocus = false;
        break;
      }

      nsAutoCString ref;
      nsresult rv = uri->GetRef(ref);
      // If there is an element in the document tree that has an ID equal to
      // fragment
      if (NS_SUCCEEDED(rv) &&
          nsContentUtils::GetTargetElement(doc, NS_ConvertUTF8toUTF16(ref))) {
        shouldFocus = false;
        break;
      }
      bc = bc->GetParent();
    }

    if (!shouldFocus) {
      continue;
    }

    MOZ_ASSERT(topWindow);
    if (TryAutoFocusCandidate(*autoFocusElement)) {
      // We've successfully autofocused an element, don't
      // need to try to focus the rest.
      SetAutoFocusFired();
      break;
    }
  }

  if (HasAutoFocusCandidates()) {
    ScheduleFlushAutoFocusCandidates();
  }
}

bool Document::TryAutoFocusCandidate(Element& aElement) {
  const FocusOptions options;
  if (RefPtr<Element> target = nsFocusManager::GetTheFocusableArea(
          &aElement, nsFocusManager::ProgrammaticFocusFlags(options))) {
    target->Focus(options, CallerType::NonSystem, IgnoreErrors());
    return true;
  }

  return false;
}

void Document::SetScrollToRef(nsIURI* aDocumentURI) {
  if (!aDocumentURI) {
    return;
  }

  nsAutoCString ref;

  // Since all URI's that pass through here aren't URL's we can't
  // rely on the nsIURI implementation for providing a way for
  // finding the 'ref' part of the URI, we'll haveto revert to
  // string routines for finding the data past '#'

  nsresult rv = aDocumentURI->GetSpec(ref);
  if (NS_FAILED(rv)) {
    Unused << aDocumentURI->GetRef(mScrollToRef);
    return;
  }

  nsReadingIterator<char> start, end;

  ref.BeginReading(start);
  ref.EndReading(end);

  if (FindCharInReadable('#', start, end)) {
    ++start;  // Skip over the '#'

    mScrollToRef = Substring(start, end);
  }
}

// https://html.spec.whatwg.org/#scrolling-to-a-fragment
void Document::ScrollToRef() {
  RefPtr<PresShell> presShell = GetPresShell();
  if (!presShell) {
    return;
  }

  // https://wicg.github.io/scroll-to-text-fragment/#invoking-text-directives
  // Monkeypatching HTML § 7.4.6.3 Scrolling to a fragment:
  // 1. Let text directives be the document's pending text directives.
  const RefPtr fragmentDirective = FragmentDirective();
  const nsTArray<RefPtr<nsRange>> textDirectives =
      fragmentDirective->FindTextFragmentsInDocument();
  // 2. If ranges is non-empty, then:
  // 2.1 Let firstRange be the first item of ranges
  const RefPtr<nsRange> textDirectiveToScroll =
      !textDirectives.IsEmpty() ? textDirectives[0] : nullptr;
  // 2.2 Visually indicate each range in ranges in an implementation-defined
  // way. The indication must not be observable from author script. See § 3.7
  // Indicating The Text Match.
  fragmentDirective->HighlightTextDirectives(textDirectives);

  // In a subsequent call to `ScrollToRef()` during page load, `textDirectives`
  // would only contain text directives that were not found in the previous
  // runs. If an earlier call during the same page load already found a text
  // directive to scroll to, only highlighting of the text directives needs to
  // be done.
  // This is indicated by `mScrolledToRefAlready`.
  if (mScrolledToRefAlready) {
    presShell->ScrollToAnchor();
    return;
  }
  // 2. If fragment is the empty string and no text directives have been
  // scrolled to, then return the special value top of the document.
  if (!textDirectiveToScroll && mScrollToRef.IsEmpty()) {
    return;
  }

  // TODO(mccr8): This will incorrectly block scrolling from a same-document
  // navigation triggered before the document is full loaded. See bug 1898630.
  if (ForceLoadAtTop()) {
    return;
  }

  // 3. Let potentialIndicatedElement be the result of finding a potential
  // indicated element given document and fragment.
  NS_ConvertUTF8toUTF16 ref(mScrollToRef);
  // This also covers 2.3 of the Monkeypatch for text fragments mentioned above:
  // 2.3 Set firstRange as document's indicated part, return.

  const bool scrollToTextDirective =
      textDirectiveToScroll
          ? fragmentDirective->IsTextDirectiveAllowedToBeScrolledTo()
          : mChangeScrollPosWhenScrollingToRef;

  auto rv =
      presShell->GoToAnchor(ref, textDirectiveToScroll, scrollToTextDirective);

  // 4. If potentialIndicatedElement is not null, then return
  // potentialIndicatedElement.
  if (NS_SUCCEEDED(rv)) {
    mScrolledToRefAlready = true;
    return;
  }

  // 5. Let fragmentBytes be the result of percent-decoding fragment.
  nsAutoCString fragmentBytes;
  const bool unescaped =
      NS_UnescapeURL(mScrollToRef.Data(), mScrollToRef.Length(),
                     /* aFlags = */ 0, fragmentBytes);

  if (!unescaped || fragmentBytes.IsEmpty()) {
    // Another attempt is only necessary if characters were unescaped.
    return;
  }

  // 6. Let decodedFragment be the result of running UTF-8 decode without BOM on
  // fragmentBytes.
  nsAutoString decodedFragment;
  rv = UTF_8_ENCODING->DecodeWithoutBOMHandling(fragmentBytes, decodedFragment);
  NS_ENSURE_SUCCESS_VOID(rv);

  // 7. Set potentialIndicatedElement to the result of finding a potential
  // indicated element given document and decodedFragment.
  rv = presShell->GoToAnchor(decodedFragment, nullptr,
                             mChangeScrollPosWhenScrollingToRef);
  if (NS_SUCCEEDED(rv)) {
    mScrolledToRefAlready = true;
  }
}

void Document::RegisterActivityObserver(nsISupports* aSupports) {
  if (!mActivityObservers) {
    mActivityObservers = MakeUnique<nsTHashSet<nsISupports*>>();
  }
  mActivityObservers->Insert(aSupports);
}

bool Document::UnregisterActivityObserver(nsISupports* aSupports) {
  if (!mActivityObservers) {
    return false;
  }
  return mActivityObservers->EnsureRemoved(aSupports);
}

void Document::EnumerateActivityObservers(
    ActivityObserverEnumerator aEnumerator) {
  if (!mActivityObservers) {
    return;
  }

  const auto keyArray =
      ToTArray<nsTArray<nsCOMPtr<nsISupports>>>(*mActivityObservers);
  for (auto& observer : keyArray) {
    aEnumerator(observer.get());
  }
}

void Document::RegisterPendingLinkUpdate(Link* aLink) {
  if (aLink->HasPendingLinkUpdate()) {
    return;
  }

  aLink->SetHasPendingLinkUpdate();

  if (!mHasLinksToUpdateRunnable && !mFlushingPendingLinkUpdates) {
    nsCOMPtr<nsIRunnable> event =
        NewRunnableMethod("Document::FlushPendingLinkUpdates", this,
                          &Document::FlushPendingLinkUpdates);
    // Do this work in a second in the worst case.
    nsresult rv = NS_DispatchToCurrentThreadQueue(event.forget(), 1000,
                                                  EventQueuePriority::Idle);
    if (NS_FAILED(rv)) {
      // If during shutdown posting a runnable doesn't succeed, we probably
      // don't need to update link states.
      return;
    }
    mHasLinksToUpdateRunnable = true;
  }

  mLinksToUpdate.InfallibleAppend(aLink);
}

void Document::FlushPendingLinkUpdates() {
  MOZ_DIAGNOSTIC_ASSERT(!mFlushingPendingLinkUpdates);
  MOZ_ASSERT(mHasLinksToUpdateRunnable);
  mHasLinksToUpdateRunnable = false;

  auto restore = MakeScopeExit([&] { mFlushingPendingLinkUpdates = false; });
  mFlushingPendingLinkUpdates = true;

  while (!mLinksToUpdate.IsEmpty()) {
    LinksToUpdateList links(std::move(mLinksToUpdate));
    for (auto iter = links.Iter(); !iter.Done(); iter.Next()) {
      Link* link = iter.Get();
      Element* element = link->GetElement();
      if (element->OwnerDoc() == this) {
        link->ClearHasPendingLinkUpdate();
        if (element->IsInComposedDoc()) {
          link->TriggerLinkUpdate(/* aNotify = */ true);
        }
      }
    }
  }
}

/**
 * Retrieves the node in a static-clone document that corresponds to aOrigNode,
 * which is a node in the original document from which aStaticClone was cloned.
 */
static nsINode* GetCorrespondingNodeInDocument(const nsINode* aOrigNode,
                                               Document& aStaticClone) {
  MOZ_ASSERT(aOrigNode);

  // Selections in anonymous subtrees aren't supported.
  if (NS_WARN_IF(aOrigNode->IsInNativeAnonymousSubtree())) {
    return nullptr;
  }

  // If the node is disconnected, this is a bug in the selection code, but it
  // can happen with shadow DOM so handle it.
  if (NS_WARN_IF(!aOrigNode->IsInComposedDoc())) {
    return nullptr;
  }

  AutoTArray<Maybe<uint32_t>, 32> indexArray;
  const nsINode* current = aOrigNode;
  while (const nsINode* parent = current->GetParentNode()) {
    Maybe<uint32_t> index = parent->ComputeIndexOf(current);
    NS_ENSURE_TRUE(index.isSome(), nullptr);
    indexArray.AppendElement(std::move(index));
    current = parent;
  }
  MOZ_ASSERT(current->IsDocument() || current->IsShadowRoot());
  nsINode* correspondingNode = [&]() -> nsINode* {
    if (current->IsDocument()) {
      return &aStaticClone;
    }
    const auto* shadow = ShadowRoot::FromNode(*current);
    if (!shadow) {
      return nullptr;
    }
    nsINode* correspondingHost =
        GetCorrespondingNodeInDocument(shadow->Host(), aStaticClone);
    if (NS_WARN_IF(!correspondingHost || !correspondingHost->IsElement())) {
      return nullptr;
    }
    return correspondingHost->AsElement()->GetShadowRoot();
  }();

  if (NS_WARN_IF(!correspondingNode)) {
    return nullptr;
  }
  for (const Maybe<uint32_t>& index : Reversed(indexArray)) {
    correspondingNode = correspondingNode->GetChildAt_Deprecated(*index);
    NS_ENSURE_TRUE(correspondingNode, nullptr);
  }
  return correspondingNode;
}

/**
 * Caches the selection ranges from the source document onto the static clone in
 * case the "Print Selection Only" functionality is invoked.
 *
 * Note that we cannot use the selection obtained from GetOriginalDocument()
 * since that selection may have mutated after the print was invoked.
 *
 * Note also that because nsRange objects point into a specific document's
 * nodes, we cannot reuse an array of nsRange objects across multiple static
 * clone documents. For that reason we cache a new array of ranges on each
 * static clone that we create.
 *
 * TODO(emilio): This can be simplified once we don't re-clone from static
 * documents.
 *
 * @param aSourceDoc the document from which we are caching selection ranges
 * @param aStaticClone the document that will hold the cache
 * @return true if a selection range was cached
 */
static void CachePrintSelectionRanges(const Document& aSourceDoc,
                                      Document& aStaticClone) {
  MOZ_ASSERT(aStaticClone.IsStaticDocument());
  MOZ_ASSERT(!aStaticClone.GetProperty(nsGkAtoms::printisfocuseddoc));
  MOZ_ASSERT(!aStaticClone.GetProperty(nsGkAtoms::printselectionranges));

  bool sourceDocIsStatic = aSourceDoc.IsStaticDocument();

  // When the user opts to "Print Selection Only", the print code prefers any
  // selection in the static clone corresponding to the focused frame. If this
  // is that static clone, flag it for the printing code:
  const bool isFocusedDoc = [&] {
    if (sourceDocIsStatic) {
      return bool(aSourceDoc.GetProperty(nsGkAtoms::printisfocuseddoc));
    }
    nsPIDOMWindowOuter* window = aSourceDoc.GetWindow();
    if (!window) {
      return false;
    }
    nsCOMPtr<nsPIDOMWindowOuter> rootWindow = window->GetPrivateRoot();
    if (!rootWindow) {
      return false;
    }
    nsCOMPtr<nsPIDOMWindowOuter> focusedWindow;
    nsFocusManager::GetFocusedDescendant(rootWindow,
                                         nsFocusManager::eIncludeAllDescendants,
                                         getter_AddRefs(focusedWindow));
    return focusedWindow && focusedWindow->GetExtantDoc() == &aSourceDoc;
  }();
  if (isFocusedDoc) {
    aStaticClone.SetProperty(nsGkAtoms::printisfocuseddoc,
                             reinterpret_cast<void*>(true));
  }

  const Selection* origSelection = nullptr;
  const nsTArray<RefPtr<nsRange>>* origRanges = nullptr;

  if (sourceDocIsStatic) {
    origRanges = static_cast<nsTArray<RefPtr<nsRange>>*>(
        aSourceDoc.GetProperty(nsGkAtoms::printselectionranges));
  } else if (PresShell* shell = aSourceDoc.GetPresShell()) {
    origSelection = shell->GetCurrentSelection(SelectionType::eNormal);
  }

  if (!origSelection && !origRanges) {
    return;
  }

  const uint32_t rangeCount =
      sourceDocIsStatic ? origRanges->Length() : origSelection->RangeCount();
  auto printRanges = MakeUnique<nsTArray<RefPtr<nsRange>>>(rangeCount);

  for (const uint32_t i : IntegerRange(rangeCount)) {
    MOZ_ASSERT_IF(!sourceDocIsStatic,
                  origSelection->RangeCount() == rangeCount);
    const nsRange* range = sourceDocIsStatic ? origRanges->ElementAt(i).get()
                                             : origSelection->GetRangeAt(i);
    MOZ_ASSERT(range);
    nsINode* startContainer = range->GetMayCrossShadowBoundaryStartContainer();
    nsINode* endContainer = range->GetMayCrossShadowBoundaryEndContainer();

    if (!startContainer || !endContainer) {
      continue;
    }

    nsINode* startNode =
        GetCorrespondingNodeInDocument(startContainer, aStaticClone);
    nsINode* endNode =
        GetCorrespondingNodeInDocument(endContainer, aStaticClone);

    if (NS_WARN_IF(!startNode || !endNode)) {
      continue;
    }

    RefPtr<nsRange> clonedRange = nsRange::Create(
        startNode, range->MayCrossShadowBoundaryStartOffset(), endNode,
        range->MayCrossShadowBoundaryEndOffset(), IgnoreErrors());
    if (clonedRange &&
        !clonedRange->AreNormalRangeAndCrossShadowBoundaryRangeCollapsed()) {
      printRanges->AppendElement(std::move(clonedRange));
    }
  }

  if (printRanges->IsEmpty()) {
    return;
  }

  aStaticClone.SetProperty(nsGkAtoms::printselectionranges,
                           printRanges.release(),
                           nsINode::DeleteProperty<nsTArray<RefPtr<nsRange>>>);
}

already_AddRefed<Document> Document::CreateStaticClone(
    nsIDocShell* aCloneContainer, nsIDocumentViewer* aViewer,
    nsIPrintSettings* aPrintSettings, bool* aOutHasInProcessPrintCallbacks) {
  MOZ_ASSERT(!mCreatingStaticClone);
  MOZ_ASSERT(!GetProperty(nsGkAtoms::adoptedsheetclones));
  MOZ_DIAGNOSTIC_ASSERT(aViewer);

  mCreatingStaticClone = true;
  SetProperty(nsGkAtoms::adoptedsheetclones, new AdoptedStyleSheetCloneCache(),
              nsINode::DeleteProperty<AdoptedStyleSheetCloneCache>);

  auto raii = MakeScopeExit([&] {
    RemoveProperty(nsGkAtoms::adoptedsheetclones);
    mCreatingStaticClone = false;
  });

  // Make document use different container during cloning.
  //
  // FIXME(emilio): Why is this needed?
  RefPtr<nsDocShell> originalShell = mDocumentContainer.get();
  SetContainer(nsDocShell::Cast(aCloneContainer));
  IgnoredErrorResult rv;
  nsCOMPtr<nsINode> clonedNode = CloneNode(true, rv);
  SetContainer(originalShell);
  if (rv.Failed()) {
    return nullptr;
  }

  nsCOMPtr<Document> clonedDoc = do_QueryInterface(clonedNode);
  if (!clonedDoc) {
    return nullptr;
  }

  size_t sheetsCount = SheetCount();
  for (size_t i = 0; i < sheetsCount; ++i) {
    RefPtr<StyleSheet> sheet = SheetAt(i);
    if (sheet) {
      if (sheet->IsApplicable()) {
        RefPtr<StyleSheet> clonedSheet = sheet->Clone(nullptr, clonedDoc);
        NS_WARNING_ASSERTION(clonedSheet, "Cloning a stylesheet didn't work!");
        if (clonedSheet) {
          clonedDoc->AddStyleSheet(clonedSheet);
        }
      }
    }
  }
  clonedDoc->CloneAdoptedSheetsFrom(*this);

  for (int t = 0; t < AdditionalSheetTypeCount; ++t) {
    auto& sheets = mAdditionalSheets[additionalSheetType(t)];
    for (StyleSheet* sheet : sheets) {
      if (sheet->IsApplicable()) {
        clonedDoc->AddAdditionalStyleSheet(additionalSheetType(t), sheet);
      }
    }
  }

  // Font faces created with the JS API will not be reflected in the
  // stylesheets and need to be copied over to the cloned document.
  if (const FontFaceSet* set = GetFonts()) {
    set->CopyNonRuleFacesTo(clonedDoc->Fonts());
  }

  clonedDoc->mReferrerInfo =
      static_cast<dom::ReferrerInfo*>(mReferrerInfo.get())->Clone();
  clonedDoc->mPreloadReferrerInfo = clonedDoc->mReferrerInfo;
  CachePrintSelectionRanges(*this, *clonedDoc);

  // We're done with the clone, embed ourselves into the document viewer and
  // clone our children. The order here is pretty important, because our
  // document our document needs to have an owner global before we can create
  // the frame loaders for subdocuments.
  aViewer->SetDocument(clonedDoc);

  *aOutHasInProcessPrintCallbacks |= clonedDoc->HasPrintCallbacks();

  auto pendingClones = std::move(clonedDoc->mPendingFrameStaticClones);
  for (const auto& clone : pendingClones) {
    RefPtr<Element> element = do_QueryObject(clone.mElement);
    RefPtr<nsFrameLoader> frameLoader =
        nsFrameLoader::Create(element, /* aNetworkCreated */ false);

    if (NS_WARN_IF(!frameLoader)) {
      continue;
    }

    clone.mElement->SetFrameLoader(frameLoader);

    nsresult rv = frameLoader->FinishStaticClone(
        clone.mStaticCloneOf, aPrintSettings, aOutHasInProcessPrintCallbacks);
    Unused << NS_WARN_IF(NS_FAILED(rv));
  }

  return clonedDoc.forget();
}

void Document::UnlinkOriginalDocumentIfStatic() {
  if (IsStaticDocument() && mOriginalDocument) {
    MOZ_ASSERT(mOriginalDocument->mStaticCloneCount > 0);
    mOriginalDocument->mStaticCloneCount--;
    mOriginalDocument = nullptr;
  }
  MOZ_ASSERT(!mOriginalDocument);
}

nsresult Document::ScheduleFrameRequestCallback(FrameRequestCallback& aCallback,
                                                uint32_t* aHandle) {
  const bool wasEmpty = mFrameRequestManager.IsEmpty();
  MOZ_TRY(mFrameRequestManager.Schedule(aCallback, aHandle));
  if (wasEmpty) {
    MaybeScheduleFrameRequestCallbacks();
  }
  return NS_OK;
}

void Document::CancelFrameRequestCallback(uint32_t aHandle) {
  mFrameRequestManager.Cancel(aHandle);
}

bool Document::IsCanceledFrameRequestCallback(uint32_t aHandle) const {
  return mFrameRequestManager.IsCanceled(aHandle);
}

void Document::ScheduleVideoFrameCallbacks(HTMLVideoElement* aElement) {
  const bool wasEmpty = mFrameRequestManager.IsEmpty();
  mFrameRequestManager.Schedule(aElement);
  if (wasEmpty) {
    MaybeScheduleFrameRequestCallbacks();
  }
}

void Document::CancelVideoFrameCallbacks(HTMLVideoElement* aElement) {
  mFrameRequestManager.Cancel(aElement);
}

nsresult Document::GetStateObject(JS::MutableHandle<JS::Value> aState) {
  // Get the document's current state object. This is the object backing both
  // history.state and popStateEvent.state.
  //
  // mStateObjectContainer may be null; this just means that there's no
  // current state object.

  if (!mCachedStateObjectValid) {
    if (mStateObjectContainer) {
      AutoJSAPI jsapi;
      // Init with null is "OK" in the sense that it will just fail.
      if (!jsapi.Init(GetScopeObject())) {
        return NS_ERROR_UNEXPECTED;
      }
      JS::Rooted<JS::Value> value(jsapi.cx());
      nsresult rv =
          mStateObjectContainer->DeserializeToJsval(jsapi.cx(), &value);
      NS_ENSURE_SUCCESS(rv, rv);

      mCachedStateObject = value;
      if (!value.isNullOrUndefined()) {
        mozilla::HoldJSObjects(this);
      }
    } else {
      mCachedStateObject = JS::NullValue();
    }
    mCachedStateObjectValid = true;
  }

  aState.set(mCachedStateObject);
  return NS_OK;
}

void Document::SetNavigationTiming(nsDOMNavigationTiming* aTiming) {
  mTiming = aTiming;
  if (!mLoadingOrRestoredFromBFCacheTimeStamp.IsNull() && mTiming) {
    mTiming->SetDOMLoadingTimeStamp(GetDocumentURI(),
                                    mLoadingOrRestoredFromBFCacheTimeStamp);
  }

  // If there's already the DocumentTimeline instance, tell it since the
  // DocumentTimeline is based on both the navigation start time stamp and the
  // refresh driver timestamp.
  if (mDocumentTimeline) {
    mDocumentTimeline->UpdateLastRefreshDriverTime();
  }
}

nsContentList* Document::ImageMapList() {
  if (!mImageMaps) {
    mImageMaps = new nsContentList(this, kNameSpaceID_XHTML, nsGkAtoms::map,
                                   nsGkAtoms::map);
  }

  return mImageMaps;
}

#define DEPRECATED_OPERATION(_op) #_op "Warning",
static const char* kDeprecationWarnings[] = {
#include "nsDeprecatedOperationList.h"
    nullptr};
#undef DEPRECATED_OPERATION

#define DOCUMENT_WARNING(_op) #_op "Warning",
static const char* kDocumentWarnings[] = {
#include "nsDocumentWarningList.h"
    nullptr};
#undef DOCUMENT_WARNING

static UseCounter OperationToUseCounter(DeprecatedOperations aOperation) {
  switch (aOperation) {
#define DEPRECATED_OPERATION(_op)    \
  case DeprecatedOperations::e##_op: \
    return eUseCounter_##_op;
#include "nsDeprecatedOperationList.h"
#undef DEPRECATED_OPERATION
    default:
      MOZ_CRASH();
  }
}

bool Document::HasWarnedAbout(DeprecatedOperations aOperation) const {
  return mDeprecationWarnedAbout[static_cast<size_t>(aOperation)];
}

void Document::WarnOnceAbout(
    DeprecatedOperations aOperation, bool asError /* = false */,
    const nsTArray<nsString>& aParams /* = empty array */) const {
  MOZ_ASSERT(NS_IsMainThread());
  if (HasWarnedAbout(aOperation)) {
    return;
  }
  mDeprecationWarnedAbout[static_cast<size_t>(aOperation)] = true;
  // Don't count deprecated operations for about pages since those pages
  // are almost in our control, and we always need to remove uses there
  // before we remove the operation itself anyway.
  if (!IsAboutPage()) {
    const_cast<Document*>(this)->SetUseCounter(
        OperationToUseCounter(aOperation));
  }
  uint32_t flags =
      asError ? nsIScriptError::errorFlag : nsIScriptError::warningFlag;
  nsContentUtils::ReportToConsole(
      flags, "DOM Core"_ns, this, nsContentUtils::eDOM_PROPERTIES,
      kDeprecationWarnings[static_cast<size_t>(aOperation)], aParams);
}

bool Document::HasWarnedAbout(DocumentWarnings aWarning) const {
  return mDocWarningWarnedAbout[aWarning];
}

void Document::WarnOnceAbout(
    DocumentWarnings aWarning, bool asError /* = false */,
    const nsTArray<nsString>& aParams /* = empty array */) const {
  MOZ_ASSERT(NS_IsMainThread());
  if (HasWarnedAbout(aWarning)) {
    return;
  }
  mDocWarningWarnedAbout[aWarning] = true;
  uint32_t flags =
      asError ? nsIScriptError::errorFlag : nsIScriptError::warningFlag;
  nsContentUtils::ReportToConsole(flags, "DOM Core"_ns, this,
                                  nsContentUtils::eDOM_PROPERTIES,
                                  kDocumentWarnings[aWarning], aParams);
}

mozilla::dom::ImageTracker* Document::ImageTracker() {
  if (!mImageTracker) {
    mImageTracker = new mozilla::dom::ImageTracker;
  }
  return mImageTracker;
}

void Document::ScheduleSVGUseElementShadowTreeUpdate(
    SVGUseElement& aUseElement) {
  MOZ_ASSERT(aUseElement.IsInComposedDoc());

  if (MOZ_UNLIKELY(mIsStaticDocument)) {
    // Printing doesn't deal well with dynamic DOM mutations.
    return;
  }

  mSVGUseElementsNeedingShadowTreeUpdate.Insert(&aUseElement);

  if (PresShell* presShell = GetPresShell()) {
    presShell->EnsureStyleFlush();
  }
}

void Document::DoUpdateSVGUseElementShadowTrees() {
  MOZ_ASSERT(!mSVGUseElementsNeedingShadowTreeUpdate.IsEmpty());

  MOZ_ASSERT(!mCloningForSVGUse);
  nsAutoScriptBlockerSuppressNodeRemoved blocker;
  mCloningForSVGUse = true;

  do {
    const auto useElementsToUpdate = ToTArray<nsTArray<RefPtr<SVGUseElement>>>(
        mSVGUseElementsNeedingShadowTreeUpdate);
    mSVGUseElementsNeedingShadowTreeUpdate.Clear();

    for (const auto& useElement : useElementsToUpdate) {
      if (MOZ_UNLIKELY(!useElement->IsInComposedDoc())) {
        // The element was in another <use> shadow tree which we processed
        // already and also needed an update, and is removed from the document
        // now, so nothing to do here.
        MOZ_ASSERT(useElementsToUpdate.Length() > 1);
        continue;
      }
      useElement->UpdateShadowTree();
    }
  } while (!mSVGUseElementsNeedingShadowTreeUpdate.IsEmpty());

  mCloningForSVGUse = false;
}

void Document::NotifyMediaFeatureValuesChanged() {
  for (RefPtr<HTMLImageElement> imageElement : mResponsiveContent) {
    imageElement->MediaFeatureValuesChanged();
  }
}

already_AddRefed<Touch> Document::CreateTouch(
    nsGlobalWindowInner* aView, EventTarget* aTarget, int32_t aIdentifier,
    int32_t aPageX, int32_t aPageY, int32_t aScreenX, int32_t aScreenY,
    int32_t aClientX, int32_t aClientY, int32_t aRadiusX, int32_t aRadiusY,
    float aRotationAngle, float aForce) {
  RefPtr<Touch> touch =
      new Touch(aTarget, aIdentifier, aPageX, aPageY, aScreenX, aScreenY,
                aClientX, aClientY, aRadiusX, aRadiusY, aRotationAngle, aForce);
  return touch.forget();
}

already_AddRefed<TouchList> Document::CreateTouchList() {
  RefPtr<TouchList> retval = new TouchList(ToSupports(this));
  return retval.forget();
}

already_AddRefed<TouchList> Document::CreateTouchList(
    Touch& aTouch, const Sequence<OwningNonNull<Touch>>& aTouches) {
  RefPtr<TouchList> retval = new TouchList(ToSupports(this));
  retval->Append(&aTouch);
  for (uint32_t i = 0; i < aTouches.Length(); ++i) {
    retval->Append(aTouches[i].get());
  }
  return retval.forget();
}

already_AddRefed<TouchList> Document::CreateTouchList(
    const Sequence<OwningNonNull<Touch>>& aTouches) {
  RefPtr<TouchList> retval = new TouchList(ToSupports(this));
  for (uint32_t i = 0; i < aTouches.Length(); ++i) {
    retval->Append(aTouches[i].get());
  }
  return retval.forget();
}

// https://drafts.csswg.org/cssom-view/Overview#dom-document-caretpositionfrompoint
already_AddRefed<nsDOMCaretPosition> Document::CaretPositionFromPoint(
    float aX, float aY, const CaretPositionFromPointOptions& aOptions) {
  using FrameForPointOption = nsLayoutUtils::FrameForPointOption;

  nscoord x = nsPresContext::CSSPixelsToAppUnits(aX);
  nscoord y = nsPresContext::CSSPixelsToAppUnits(aY);
  nsPoint pt(x, y);

  FlushPendingNotifications(FlushType::Layout);

  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return nullptr;
  }

  nsIFrame* rootFrame = presShell->GetRootFrame();

  // XUL docs, unlike HTML, have no frame tree until everything's done loading
  if (!rootFrame) {
    return nullptr;
  }

  nsIFrame* ptFrame = nsLayoutUtils::GetFrameForPoint(
      RelativeTo{rootFrame}, pt,
      {{FrameForPointOption::IgnorePaintSuppression,
        FrameForPointOption::IgnoreCrossDoc}});
  if (!ptFrame) {
    return nullptr;
  }

  // We require frame-relative coordinates for GetContentOffsetsFromPoint.
  nsPoint adjustedPoint = pt;
  if (nsLayoutUtils::TransformPoint(RelativeTo{rootFrame}, RelativeTo{ptFrame},
                                    adjustedPoint) !=
      nsLayoutUtils::TRANSFORM_SUCCEEDED) {
    return nullptr;
  }

  nsIFrame::ContentOffsets offsets =
      ptFrame->GetContentOffsetsFromPoint(adjustedPoint);

  nsCOMPtr<nsINode> node = offsets.content;
  uint32_t offset = offsets.offset;
  nsCOMPtr<nsINode> anonNode = node;
  bool nodeIsAnonymous = node && node->IsInNativeAnonymousSubtree();
  if (nodeIsAnonymous) {
    node = ptFrame->GetContent();
    nsINode* nonChrome =
        node->AsContent()->FindFirstNonChromeOnlyAccessContent();
    HTMLTextAreaElement* textArea = HTMLTextAreaElement::FromNode(nonChrome);
    nsTextControlFrame* textFrame =
        do_QueryFrame(nonChrome->AsContent()->GetPrimaryFrame());
    if (!textFrame) {
      return nullptr;
    }

    // If the anonymous content node has a child, then we need to make sure
    // that we get the appropriate child, as otherwise the offset may not be
    // correct when we construct a range for it.
    nsCOMPtr<nsINode> firstChild = anonNode->GetFirstChild();
    if (firstChild) {
      anonNode = firstChild;
    }

    if (textArea) {
      offset = nsContentUtils::GetAdjustedOffsetInTextControl(ptFrame, offset);
    }

    node = nonChrome;
  }

  bool offsetAndNodeNeedsAdjustment = false;

  if (StaticPrefs::
          dom_shadowdom_new_caretPositionFromPoint_behavior_enabled()) {
    while (node->IsInShadowTree() &&
           !aOptions.mShadowRoots.Contains(node->GetContainingShadow())) {
      node = node->GetContainingShadowHost();
      offsetAndNodeNeedsAdjustment = true;
    }
  }

  if (offsetAndNodeNeedsAdjustment) {
    const Maybe<uint32_t> maybeIndex = node->ComputeIndexInParentContent();
    if (MOZ_UNLIKELY(maybeIndex.isNothing())) {
      // Unlikely to happen, but still return nullptr to avoid leaking
      // information about the shadow tree.
      return nullptr;
    }
    // 5.3.1: Set startOffset to index of startNode’s root's host.
    offset = maybeIndex.value();
    // 5.3.2: Set startNode to startNode’s root's host's parent.
    node = node->GetParentNode();
  }

  RefPtr<nsDOMCaretPosition> aCaretPos = new nsDOMCaretPosition(node, offset);
  if (nodeIsAnonymous) {
    aCaretPos->SetAnonymousContentNode(anonNode);
  }
  return aCaretPos.forget();
}

bool Document::IsPotentiallyScrollable(HTMLBodyElement* aBody) {
  // We rely on correct frame information here, so need to flush frames.
  FlushPendingNotifications(FlushType::Frames);

  // An element that is the HTML body element is potentially scrollable if all
  // of the following conditions are true:

  // The element has an associated CSS layout box.
  nsIFrame* bodyFrame = nsLayoutUtils::GetStyleFrame(aBody);
  if (!bodyFrame) {
    return false;
  }

  // The element's parent element's computed value of the overflow-x and
  // overflow-y properties are visible.
  MOZ_ASSERT(aBody->GetParent() == aBody->OwnerDoc()->GetRootElement());
  nsIFrame* parentFrame = nsLayoutUtils::GetStyleFrame(aBody->GetParent());
  if (parentFrame &&
      parentFrame->StyleDisplay()->OverflowIsVisibleInBothAxis()) {
    return false;
  }

  // The element's computed value of the overflow-x or overflow-y properties is
  // not visible.
  return !bodyFrame->StyleDisplay()->OverflowIsVisibleInBothAxis();
}

Element* Document::GetScrollingElement() {
  // Keep this in sync with IsScrollingElement.
  if (GetCompatibilityMode() == eCompatibility_NavQuirks) {
    RefPtr<HTMLBodyElement> body = GetBodyElement();
    if (body && !IsPotentiallyScrollable(body)) {
      return body;
    }

    return nullptr;
  }

  return GetRootElement();
}

bool Document::IsScrollingElement(Element* aElement) {
  // Keep this in sync with GetScrollingElement.
  MOZ_ASSERT(aElement);

  if (GetCompatibilityMode() != eCompatibility_NavQuirks) {
    return aElement == GetRootElement();
  }

  // In the common case when aElement != body, avoid refcounting.
  HTMLBodyElement* body = GetBodyElement();
  if (aElement != body) {
    return false;
  }

  // Now we know body is non-null, since aElement is not null.  It's the
  // scrolling element for the document if it itself is not potentially
  // scrollable.
  RefPtr<HTMLBodyElement> strongBody(body);
  return !IsPotentiallyScrollable(strongBody);
}

class UnblockParsingPromiseHandler final : public PromiseNativeHandler {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(UnblockParsingPromiseHandler)

  explicit UnblockParsingPromiseHandler(Document* aDocument, Promise* aPromise,
                                        const BlockParsingOptions& aOptions)
      : mPromise(aPromise) {
    nsCOMPtr<nsIParser> parser = aDocument->CreatorParserOrNull();
    if (parser &&
        (aOptions.mBlockScriptCreated || !parser->IsScriptCreated())) {
      parser->BlockParser();
      mParser = do_GetWeakReference(parser);
      mDocument = aDocument;
      mDocument->BlockOnload();
      mDocument->BlockDOMContentLoaded();
    }
  }

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MaybeUnblockParser();

    mPromise->MaybeResolve(aValue);
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MaybeUnblockParser();

    mPromise->MaybeReject(aValue);
  }

 protected:
  virtual ~UnblockParsingPromiseHandler() {
    // If we're being cleaned up by the cycle collector, our mDocument reference
    // may have been unlinked while our mParser weak reference is still alive.
    if (mDocument) {
      MaybeUnblockParser();
    }
  }

 private:
  void MaybeUnblockParser() {
    nsCOMPtr<nsIParser> parser = do_QueryReferent(mParser);
    if (parser) {
      MOZ_DIAGNOSTIC_ASSERT(mDocument);
      nsCOMPtr<nsIParser> docParser = mDocument->CreatorParserOrNull();
      if (parser == docParser) {
        parser->UnblockParser();
        parser->ContinueInterruptedParsingAsync();
      }
    }
    if (mDocument) {
      // We blocked DOMContentLoaded and load events on this document.  Unblock
      // them.  Note that we want to do that no matter what's going on with the
      // parser state for this document.  Maybe someone caused it to stop being
      // parsed, so CreatorParserOrNull() is returning null, but we still want
      // to unblock these.
      mDocument->UnblockDOMContentLoaded();
      mDocument->UnblockOnload(false);
    }
    mParser = nullptr;
    mDocument = nullptr;
  }

  nsWeakPtr mParser;
  RefPtr<Promise> mPromise;
  RefPtr<Document> mDocument;
};

NS_IMPL_CYCLE_COLLECTION(UnblockParsingPromiseHandler, mDocument, mPromise)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(UnblockParsingPromiseHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(UnblockParsingPromiseHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(UnblockParsingPromiseHandler)

already_AddRefed<Promise> Document::BlockParsing(
    Promise& aPromise, const BlockParsingOptions& aOptions, ErrorResult& aRv) {
  RefPtr<Promise> resultPromise =
      Promise::Create(aPromise.GetParentObject(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  RefPtr<PromiseNativeHandler> promiseHandler =
      new UnblockParsingPromiseHandler(this, resultPromise, aOptions);
  aPromise.AppendNativeHandler(promiseHandler);

  return resultPromise.forget();
}

already_AddRefed<nsIURI> Document::GetMozDocumentURIIfNotForErrorPages() {
  if (mFailedChannel) {
    nsCOMPtr<nsIURI> failedURI;
    if (NS_SUCCEEDED(mFailedChannel->GetURI(getter_AddRefs(failedURI)))) {
      return failedURI.forget();
    }
  }

  nsCOMPtr<nsIURI> uri = GetDocumentURIObject();
  if (!uri) {
    return nullptr;
  }

  return uri.forget();
}

Promise* Document::GetDocumentReadyForIdle(ErrorResult& aRv) {
  if (mIsGoingAway) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }

  if (!mReadyForIdle) {
    nsIGlobalObject* global = GetScopeObject();
    if (!global) {
      aRv.Throw(NS_ERROR_NOT_AVAILABLE);
      return nullptr;
    }

    mReadyForIdle = Promise::Create(global, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
  }

  return mReadyForIdle;
}

void Document::MaybeResolveReadyForIdle() {
  IgnoredErrorResult rv;
  Promise* readyPromise = GetDocumentReadyForIdle(rv);
  if (readyPromise) {
    readyPromise->MaybeResolveWithUndefined();
  }
}

mozilla::dom::FeaturePolicy* Document::FeaturePolicy() const {
  // The policy is created when the document is initialized. We _must_ have a
  // policy here even if the featurePolicy pref is off. If this assertion fails,
  // it means that ::FeaturePolicy() is called before ::StartDocumentLoad().
  MOZ_ASSERT(mFeaturePolicy);
  return mFeaturePolicy;
}

nsIDOMXULCommandDispatcher* Document::GetCommandDispatcher() {
  // Only chrome documents are allowed to use command dispatcher.
  if (!nsContentUtils::IsChromeDoc(this)) {
    return nullptr;
  }
  if (!mCommandDispatcher) {
    // Create our command dispatcher and hook it up.
    mCommandDispatcher = new nsXULCommandDispatcher(this);
  }
  return mCommandDispatcher;
}

void Document::InitializeXULBroadcastManager() {
  if (mXULBroadcastManager) {
    return;
  }
  mXULBroadcastManager = new XULBroadcastManager(this);
}

namespace {

class DevToolsMutationObserver final : public nsStubMutationObserver {
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMUTATIONOBSERVER_ATTRIBUTECHANGED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTAPPENDED
  NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED

  // We handle this in nsContentUtils::MaybeFireNodeRemoved, since devtools
  // relies on the event firing _before_ the removal happens.
  // NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

  // NOTE(emilio, bug 1694627): DevTools doesn't seem to deal with character
  // data changes right now (maybe intentionally?).
  // NS_DECL_NSIMUTATIONOBSERVER_CHARACTERDATACHANGED

  DevToolsMutationObserver() = default;

 private:
  void FireEvent(nsINode* aTarget, const nsAString& aType);

  ~DevToolsMutationObserver() = default;
};

NS_IMPL_ISUPPORTS(DevToolsMutationObserver, nsIMutationObserver)

void DevToolsMutationObserver::FireEvent(nsINode* aTarget,
                                         const nsAString& aType) {
  AsyncEventDispatcher::RunDOMEventWhenSafe(*aTarget, aType, CanBubble::eNo,
                                            ChromeOnlyDispatch::eYes,
                                            Composed::eYes);
}

void DevToolsMutationObserver::AttributeChanged(Element* aElement,
                                                int32_t aNamespaceID,
                                                nsAtom* aAttribute,
                                                int32_t aModType,
                                                const nsAttrValue* aOldValue) {
  FireEvent(aElement, u"devtoolsattrmodified"_ns);
}

void DevToolsMutationObserver::ContentAppended(nsIContent* aFirstNewContent) {
  for (nsIContent* c = aFirstNewContent; c; c = c->GetNextSibling()) {
    ContentInserted(c);
  }
}

void DevToolsMutationObserver::ContentInserted(nsIContent* aChild) {
  FireEvent(aChild, u"devtoolschildinserted"_ns);
}

static StaticRefPtr<DevToolsMutationObserver> sDevToolsMutationObserver;

}  // namespace

void Document::SetDevToolsWatchingDOMMutations(bool aValue) {
  if (mDevToolsWatchingDOMMutations == aValue || mIsGoingAway) {
    return;
  }
  mDevToolsWatchingDOMMutations = aValue;
  if (aValue) {
    if (MOZ_UNLIKELY(!sDevToolsMutationObserver)) {
      sDevToolsMutationObserver = new DevToolsMutationObserver();
      ClearOnShutdown(&sDevToolsMutationObserver);
    }
    AddMutationObserver(sDevToolsMutationObserver);
  } else if (sDevToolsMutationObserver) {
    RemoveMutationObserver(sDevToolsMutationObserver);
  }
}

void EvaluateMediaQueryLists(nsTArray<RefPtr<MediaQueryList>>& aListsToNotify,
                             Document& aDocument) {
  if (nsPresContext* pc = aDocument.GetPresContext()) {
    pc->FlushPendingMediaFeatureValuesChanged();
  }

  for (MediaQueryList* mql : aDocument.MediaQueryLists()) {
    if (mql->EvaluateOnRenderingUpdate()) {
      aListsToNotify.AppendElement(mql);
    }
  }
}

void Document::EvaluateMediaQueriesAndReportChanges() {
  AutoTArray<RefPtr<MediaQueryList>, 32> mqls;
  EvaluateMediaQueryLists(mqls, *this);
  for (auto& mql : mqls) {
    mql->FireChangeEvent();
  }
}

nsIHTMLCollection* Document::Children() {
  if (!mChildrenCollection) {
    mChildrenCollection =
        new nsContentList(this, kNameSpaceID_Wildcard, nsGkAtoms::_asterisk,
                          nsGkAtoms::_asterisk, false);
  }

  return mChildrenCollection;
}

uint32_t Document::ChildElementCount() { return Children()->Length(); }

// Singleton class to manage the list of fullscreen documents which are the
// root of a branch which contains fullscreen documents. We maintain this list
// so that we can easily exit all windows from fullscreen when the user
// presses the escape key.
class FullscreenRoots {
 public:
  // Adds the root of given document to the manager. Calling this method
  // with a document whose root is already contained has no effect.
  static void Add(Document* aDoc);

  // Iterates over every root in the root list, and calls aFunction, passing
  // each root once to aFunction. It is safe to call Add() and Remove() while
  // iterating over the list (i.e. in aFunction). Documents that are removed
  // from the manager during traversal are not traversed, and documents that
  // are added to the manager during traversal are also not traversed.
  static void ForEach(void (*aFunction)(Document* aDoc));

  // Removes the root of a specific document from the manager.
  static void Remove(Document* aDoc);

  // Returns true if all roots added to the list have been removed.
  static bool IsEmpty();

 private:
  MOZ_COUNTED_DEFAULT_CTOR(FullscreenRoots)
  MOZ_COUNTED_DTOR(FullscreenRoots)

  using RootsArray = nsTArray<WeakPtr<Document>>;

  // Returns true if aRoot is in the list of fullscreen roots.
  static bool Contains(Document* aRoot);

  // Singleton instance of the FullscreenRoots. This is instantiated when a
  // root is added, and it is deleted when the last root is removed.
  static FullscreenRoots* sInstance;

  // List of weak pointers to roots.
  RootsArray mRoots;
};

FullscreenRoots* FullscreenRoots::sInstance = nullptr;

/* static */
void FullscreenRoots::ForEach(void (*aFunction)(Document* aDoc)) {
  if (!sInstance) {
    return;
  }
  // Create a copy of the roots array, and iterate over the copy. This is so
  // that if an element is removed from mRoots we don't mess up our iteration.
  RootsArray roots(sInstance->mRoots.Clone());
  // Call aFunction on all entries.
  for (uint32_t i = 0; i < roots.Length(); i++) {
    nsCOMPtr<Document> root(roots[i]);
    // Check that the root isn't in the manager. This is so that new additions
    // while we were running don't get traversed.
    if (root && FullscreenRoots::Contains(root)) {
      aFunction(root);
    }
  }
}

/* static */
bool FullscreenRoots::Contains(Document* aRoot) {
  return sInstance && sInstance->mRoots.Contains(aRoot);
}

/* static */
void FullscreenRoots::Add(Document* aDoc) {
  nsCOMPtr<Document> root =
      nsContentUtils::GetInProcessSubtreeRootDocument(aDoc);
  if (!FullscreenRoots::Contains(root)) {
    if (!sInstance) {
      sInstance = new FullscreenRoots();
    }
    sInstance->mRoots.AppendElement(root);
  }
}

/* static */
void FullscreenRoots::Remove(Document* aDoc) {
  nsCOMPtr<Document> root =
      nsContentUtils::GetInProcessSubtreeRootDocument(aDoc);
  if (!sInstance || !sInstance->mRoots.RemoveElement(root)) {
    NS_ERROR("Should only try to remove roots which are still added!");
    return;
  }
  if (sInstance->mRoots.IsEmpty()) {
    delete sInstance;
    sInstance = nullptr;
  }
}

/* static */
bool FullscreenRoots::IsEmpty() { return !sInstance; }

// Any fullscreen change waiting for the widget to finish transition
// is queued here. This is declared static instead of a member of
// Document because in the majority of time, there would be at most
// one document requesting or exiting fullscreen. We shouldn't waste
// the space to hold for it in every document.
class PendingFullscreenChangeList {
 public:
  PendingFullscreenChangeList() = delete;

  template <typename T>
  static void Add(UniquePtr<T> aChange) {
    sList.insertBack(aChange.release());
  }

  static const FullscreenChange* GetLast() { return sList.getLast(); }

  enum IteratorOption {
    // When we are committing fullscreen changes or preparing for
    // that, we generally want to iterate all requests in the same
    // window with eDocumentsWithSameRoot option.
    eDocumentsWithSameRoot,
    // If we are removing a document from the tree, we would only
    // want to remove the requests from the given document and its
    // descendants. For that case, use eInclusiveDescendants.
    eInclusiveDescendants
  };

  template <typename T>
  class Iterator {
   public:
    explicit Iterator(Document* aDoc, IteratorOption aOption)
        : mCurrent(PendingFullscreenChangeList::sList.getFirst()) {
      if (mCurrent) {
        if (aDoc->GetBrowsingContext()) {
          mRootBCForIteration = aDoc->GetBrowsingContext();
          if (aOption == eDocumentsWithSameRoot) {
            BrowsingContext* bc =
                GetParentIgnoreChromeBoundary(mRootBCForIteration);
            while (bc) {
              mRootBCForIteration = bc;
              bc = GetParentIgnoreChromeBoundary(mRootBCForIteration);
            }
          }
        }
        SkipToNextMatch();
      }
    }

    UniquePtr<T> TakeAndNext() {
      auto thisChange = TakeAndNextInternal();
      SkipToNextMatch();
      return thisChange;
    }
    bool AtEnd() const { return mCurrent == nullptr; }

   private:
    static BrowsingContext* GetParentIgnoreChromeBoundary(
        BrowsingContext* aBC) {
      // Chrome BrowsingContexts are only available in the parent process, so if
      // we're in a content process, we only worry about the context tree.
      if (XRE_IsParentProcess()) {
        return aBC->Canonical()->GetParentCrossChromeBoundary();
      }
      return aBC->GetParent();
    }

    UniquePtr<T> TakeAndNextInternal() {
      FullscreenChange* thisChange = mCurrent;
      MOZ_ASSERT(thisChange->Type() == T::kType);
      mCurrent = mCurrent->removeAndGetNext();
      return WrapUnique(static_cast<T*>(thisChange));
    }
    void SkipToNextMatch() {
      while (mCurrent) {
        if (mCurrent->Type() == T::kType) {
          BrowsingContext* bc = mCurrent->Document()->GetBrowsingContext();
          if (!bc) {
            // Always automatically drop fullscreen changes which are
            // from a document detached from the doc shell.
            UniquePtr<T> change = TakeAndNextInternal();
            change->MayRejectPromise("Document is not active");
            continue;
          }
          while (bc && bc != mRootBCForIteration) {
            bc = GetParentIgnoreChromeBoundary(bc);
          }
          if (bc) {
            break;
          }
        }
        // The current one either don't have matched type, or isn't
        // inside the given subtree, so skip this item.
        mCurrent = mCurrent->getNext();
      }
    }

    FullscreenChange* mCurrent;
    RefPtr<BrowsingContext> mRootBCForIteration;
  };

 private:
  static LinkedList<FullscreenChange> sList;
};

/* static */
MOZ_RUNINIT LinkedList<FullscreenChange> PendingFullscreenChangeList::sList;

size_t Document::CountFullscreenElements() const {
  size_t count = 0;
  for (const nsWeakPtr& ptr : mTopLayer) {
    if (nsCOMPtr<Element> elem = do_QueryReferent(ptr)) {
      if (elem->State().HasState(ElementState::FULLSCREEN)) {
        count++;
      }
    }
  }
  return count;
}

// https://github.com/whatwg/html/issues/9143
// We need to consider the precedence between active modal dialog, topmost auto
// popover and fullscreen element once it's specified.
void Document::HandleEscKey() {
  for (const nsWeakPtr& weakPtr : Reversed(mTopLayer)) {
    nsCOMPtr<Element> element(do_QueryReferent(weakPtr));
    if (RefPtr popoverHTMLEl = nsGenericHTMLElement::FromNodeOrNull(element)) {
      if (element->IsAutoPopover() && element->IsPopoverOpen()) {
        popoverHTMLEl->HidePopover(IgnoreErrors());
        break;
      }
    }
    if (auto* dialog = HTMLDialogElement::FromNodeOrNull(element)) {
      dialog->QueueCancelDialog();
      break;
    }
  }
}

MOZ_CAN_RUN_SCRIPT void Document::ProcessCloseRequest() {
  if (RefPtr win = GetInnerWindow()) {
    if (win->IsFullyActive()) {
      RefPtr manager = win->EnsureCloseWatcherManager();
      manager->ProcessCloseRequest();
    }
  }
}

already_AddRefed<Promise> Document::ExitFullscreen(ErrorResult& aRv) {
  UniquePtr<FullscreenExit> exit = FullscreenExit::Create(this, aRv);
  RefPtr<Promise> promise = exit->GetPromise();
  RestorePreviousFullscreenState(std::move(exit));
  return promise.forget();
}

static void AskWindowToExitFullscreen(Document* aDoc) {
  if (XRE_GetProcessType() == GeckoProcessType_Content) {
    nsContentUtils::DispatchEventOnlyToChrome(
        aDoc, aDoc, u"MozDOMFullscreen:Exit"_ns, CanBubble::eYes,
        Cancelable::eNo, /* DefaultAction */ nullptr);
  } else {
    if (nsPIDOMWindowOuter* win = aDoc->GetWindow()) {
      win->SetFullscreenInternal(FullscreenReason::ForFullscreenAPI, false);
    }
  }
}

class nsCallExitFullscreen : public Runnable {
 public:
  explicit nsCallExitFullscreen(Document* aDoc)
      : mozilla::Runnable("nsCallExitFullscreen"), mDoc(aDoc) {}

  NS_IMETHOD Run() final {
    if (!mDoc) {
      FullscreenRoots::ForEach(&AskWindowToExitFullscreen);
    } else {
      AskWindowToExitFullscreen(mDoc);
    }
    return NS_OK;
  }

 private:
  nsCOMPtr<Document> mDoc;
};

/* static */
void Document::AsyncExitFullscreen(Document* aDoc) {
  MOZ_RELEASE_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIRunnable> exit = new nsCallExitFullscreen(aDoc);
  NS_DispatchToCurrentThread(exit.forget());
}

static uint32_t CountFullscreenSubDocuments(Document& aDoc) {
  uint32_t count = 0;
  // FIXME(emilio): Should this be recursive and dig into our nested subdocs?
  aDoc.EnumerateSubDocuments([&count](Document& aSubDoc) {
    if (aSubDoc.Fullscreen()) {
      count++;
    }
    return CallState::Continue;
  });
  return count;
}

bool Document::IsFullscreenLeaf() {
  // A fullscreen leaf document is fullscreen, and has no fullscreen
  // subdocuments.
  //
  // FIXME(emilio): This doesn't seem to account for fission iframes, is that
  // ok?
  return Fullscreen() && CountFullscreenSubDocuments(*this) == 0;
}

static Document* GetFullscreenLeaf(Document& aDoc) {
  if (aDoc.IsFullscreenLeaf()) {
    return &aDoc;
  }
  if (!aDoc.Fullscreen()) {
    return nullptr;
  }
  Document* leaf = nullptr;
  aDoc.EnumerateSubDocuments([&leaf](Document& aSubDoc) {
    leaf = GetFullscreenLeaf(aSubDoc);
    return leaf ? CallState::Stop : CallState::Continue;
  });
  return leaf;
}

static Document* GetFullscreenLeaf(Document* aDoc) {
  if (Document* leaf = GetFullscreenLeaf(*aDoc)) {
    return leaf;
  }
  // Otherwise we could be either in a non-fullscreen doc tree, or we're
  // below the fullscreen doc. Start the search from the root.
  Document* root = nsContentUtils::GetInProcessSubtreeRootDocument(aDoc);
  return GetFullscreenLeaf(*root);
}

static CallState ResetFullscreen(Document& aDocument) {
  if (Element* fsElement = aDocument.GetUnretargetedFullscreenElement()) {
    NS_ASSERTION(CountFullscreenSubDocuments(aDocument) <= 1,
                 "Should have at most 1 fullscreen subdocument.");
    aDocument.CleanupFullscreenState();
    NS_ASSERTION(!aDocument.Fullscreen(), "Should reset fullscreen");
    DispatchFullscreenChange(aDocument, fsElement);
    aDocument.EnumerateSubDocuments(ResetFullscreen);
  }
  return CallState::Continue;
}

// Since Document::ExitFullscreenInDocTree() could be called from
// Element::UnbindFromTree() where it is not safe to synchronously run
// script. This runnable is the script part of that function.
class ExitFullscreenScriptRunnable : public Runnable {
 public:
  explicit ExitFullscreenScriptRunnable(Document* aRoot, Document* aLeaf)
      : mozilla::Runnable("ExitFullscreenScriptRunnable"),
        mRoot(aRoot),
        mLeaf(aLeaf) {}

  NS_IMETHOD Run() override {
    // Dispatch MozDOMFullscreen:Exited to the original fullscreen leaf
    // document since we want this event to follow the same path that
    // MozDOMFullscreen:Entered was dispatched.
    nsContentUtils::DispatchEventOnlyToChrome(
        mLeaf, mLeaf, u"MozDOMFullscreen:Exited"_ns, CanBubble::eYes,
        Cancelable::eNo, /* DefaultAction */ nullptr);
    // Ensure the window exits fullscreen, as long as we don't have
    // pending fullscreen requests.
    if (nsPIDOMWindowOuter* win = mRoot->GetWindow()) {
      if (!mRoot->HasPendingFullscreenRequests()) {
        win->SetFullscreenInternal(FullscreenReason::ForForceExitFullscreen,
                                   false);
      }
    }
    return NS_OK;
  }

 private:
  nsCOMPtr<Document> mRoot;
  nsCOMPtr<Document> mLeaf;
};

/* static */
void Document::ExitFullscreenInDocTree(Document* aMaybeNotARootDoc) {
  MOZ_ASSERT(aMaybeNotARootDoc);

  // Unlock the pointer
  PointerLockManager::Unlock("Document::ExitFullscreenInDocTree");

  // Resolve all promises which waiting for exit fullscreen.
  PendingFullscreenChangeList::Iterator<FullscreenExit> iter(
      aMaybeNotARootDoc, PendingFullscreenChangeList::eDocumentsWithSameRoot);
  while (!iter.AtEnd()) {
    UniquePtr<FullscreenExit> exit = iter.TakeAndNext();
    exit->MayResolvePromise();
  }

  nsCOMPtr<Document> root = aMaybeNotARootDoc->GetFullscreenRoot();
  if (!root || !root->Fullscreen()) {
    // If a document was detached before exiting from fullscreen, it is
    // possible that the root had left fullscreen state. In this case,
    // we would not get anything from the ResetFullscreen() call. Root's
    // not being a fullscreen doc also means the widget should have
    // exited fullscreen state. It means even if we do not return here,
    // we would actually do nothing below except crashing ourselves via
    // dispatching the "MozDOMFullscreen:Exited" event to an nonexistent
    // document.
    return;
  }

  // Record the fullscreen leaf document for MozDOMFullscreen:Exited.
  // See ExitFullscreenScriptRunnable::Run for details. We have to
  // record it here because we don't have such information after we
  // reset the fullscreen state below.
  Document* fullscreenLeaf = GetFullscreenLeaf(root);

  // Walk the tree of fullscreen documents, and reset their fullscreen state.
  ResetFullscreen(*root);

  NS_ASSERTION(!root->Fullscreen(),
               "Fullscreen root should no longer be a fullscreen doc...");

  // Move the top-level window out of fullscreen mode.
  FullscreenRoots::Remove(root);

  nsContentUtils::AddScriptRunner(
      new ExitFullscreenScriptRunnable(root, fullscreenLeaf));
}

static void DispatchFullscreenNewOriginEvent(Document* aDoc) {
  RefPtr<AsyncEventDispatcher> asyncDispatcher =
      new AsyncEventDispatcher(aDoc, u"MozDOMFullscreen:NewOrigin"_ns,
                               CanBubble::eYes, ChromeOnlyDispatch::eYes);
  asyncDispatcher->PostDOMEvent();
}

void Document::RestorePreviousFullscreenState(UniquePtr<FullscreenExit> aExit) {
  NS_ASSERTION(!Fullscreen() || !FullscreenRoots::IsEmpty(),
               "Should have at least 1 fullscreen root when fullscreen!");

  if (!GetWindow()) {
    aExit->MayRejectPromise("No active window");
    return;
  }
  if (!Fullscreen() || FullscreenRoots::IsEmpty()) {
    aExit->MayRejectPromise("Not in fullscreen mode");
    return;
  }

  nsCOMPtr<Document> fullScreenDoc = GetFullscreenLeaf(this);
  AutoTArray<Element*, 8> exitElements;

  Document* doc = fullScreenDoc;
  // Collect all subdocuments.
  for (; doc != this; doc = doc->GetInProcessParentDocument()) {
    Element* fsElement = doc->GetUnretargetedFullscreenElement();
    MOZ_ASSERT(fsElement,
               "Parent document of "
               "a fullscreen document without fullscreen element?");
    exitElements.AppendElement(fsElement);
  }
  MOZ_ASSERT(doc == this, "Must have reached this doc");
  // Collect all ancestor documents which we are going to change.
  for (; doc; doc = doc->GetInProcessParentDocument()) {
    Element* fsElement = doc->GetUnretargetedFullscreenElement();
    MOZ_ASSERT(fsElement,
               "Ancestor of fullscreen document must also be in fullscreen");
    if (doc != this) {
      if (auto* iframe = HTMLIFrameElement::FromNode(fsElement)) {
        if (iframe->FullscreenFlag()) {
          // If this is an iframe, and it explicitly requested
          // fullscreen, don't rollback it automatically.
          break;
        }
      }
    }
    exitElements.AppendElement(fsElement);
    if (doc->CountFullscreenElements() > 1) {
      break;
    }
  }

  Document* lastDoc = exitElements.LastElement()->OwnerDoc();
  size_t fullscreenCount = lastDoc->CountFullscreenElements();
  if (!lastDoc->GetInProcessParentDocument() && fullscreenCount == 1) {
    // If we are fully exiting fullscreen, don't touch anything here,
    // just wait for the window to get out from fullscreen first.
    PendingFullscreenChangeList::Add(std::move(aExit));
    AskWindowToExitFullscreen(this);
    return;
  }

  // If fullscreen mode is updated the pointer should be unlocked
  PointerLockManager::Unlock("Document::RestorePreviousFullscreenState");
  // All documents listed in the array except the last one are going to
  // completely exit from the fullscreen state.
  for (auto i : IntegerRange(exitElements.Length() - 1)) {
    exitElements[i]->OwnerDoc()->CleanupFullscreenState();
  }
  // The last document will either rollback one fullscreen element, or
  // completely exit from the fullscreen state as well.
  Document* newFullscreenDoc;
  if (fullscreenCount > 1) {
    DebugOnly<bool> removedFullscreenElement = lastDoc->PopFullscreenElement();
    MOZ_ASSERT(removedFullscreenElement);
    newFullscreenDoc = lastDoc;
  } else {
    lastDoc->CleanupFullscreenState();
    newFullscreenDoc = lastDoc->GetInProcessParentDocument();
  }
  // Dispatch the fullscreenchange event to all document listed. Note
  // that the loop order is reversed so that events are dispatched in
  // the tree order as indicated in the spec.
  for (Element* e : Reversed(exitElements)) {
    DispatchFullscreenChange(*e->OwnerDoc(), e);
  }
  aExit->MayResolvePromise();

  MOZ_ASSERT(newFullscreenDoc,
             "If we were going to exit from fullscreen on "
             "all documents in this doctree, we should've asked the window to "
             "exit first instead of reaching here.");
  if (fullScreenDoc != newFullscreenDoc &&
      !nsContentUtils::HaveEqualPrincipals(fullScreenDoc, newFullscreenDoc)) {
    // We've popped so enough off the stack that we've rolled back to
    // a fullscreen element in a parent document. If this document is
    // cross origin, dispatch an event to chrome so it knows to show
    // the warning UI.
    DispatchFullscreenNewOriginEvent(newFullscreenDoc);
  }
}

static void UpdateViewportScrollbarOverrideForFullscreen(Document* aDoc) {
  if (nsPresContext* presContext = aDoc->GetPresContext()) {
    presContext->UpdateViewportScrollStylesOverride();
  }
}

static void NotifyFullScreenChangedForMediaElement(Element& aElement) {
  // When a media element enters the fullscreen, we would like to notify that
  // to the media controller in order to update its status.
  if (auto* mediaElem = HTMLMediaElement::FromNode(aElement)) {
    mediaElem->NotifyFullScreenChanged();
  }
}

void Document::CleanupFullscreenState() {
  while (PopFullscreenElement(UpdateViewport::No)) {
    // Remove the next one if appropriate
  }

  UpdateViewportScrollbarOverrideForFullscreen(this);
  mFullscreenRoot = nullptr;

  // Restore the zoom level that was in place prior to entering fullscreen.
  if (PresShell* presShell = GetPresShell()) {
    if (presShell->GetMobileViewportManager()) {
      presShell->SetResolutionAndScaleTo(
          mSavedResolution, ResolutionChangeOrigin::MainThreadRestore);
    }
  }
}

bool Document::PopFullscreenElement(UpdateViewport aUpdateViewport) {
  Element* removedElement = TopLayerPop([](Element* element) -> bool {
    return element->State().HasState(ElementState::FULLSCREEN);
  });

  if (!removedElement) {
    return false;
  }

  MOZ_ASSERT(removedElement->State().HasState(ElementState::FULLSCREEN));
  removedElement->RemoveStates(ElementState::FULLSCREEN | ElementState::MODAL);
  NotifyFullScreenChangedForMediaElement(*removedElement);
  // Reset iframe fullscreen flag.
  if (auto* iframe = HTMLIFrameElement::FromNode(removedElement)) {
    iframe->SetFullscreenFlag(false);
  }
  if (aUpdateViewport == UpdateViewport::Yes) {
    UpdateViewportScrollbarOverrideForFullscreen(this);
  }
  return true;
}

void Document::SetFullscreenElement(Element& aElement) {
  ElementState statesToAdd = ElementState::FULLSCREEN;
  if (!IsInChromeDocShell()) {
    // Don't make the document modal in chrome documents, since we don't want
    // the browser UI like the context menu / etc to be inert.
    statesToAdd |= ElementState::MODAL;
  }
  aElement.AddStates(statesToAdd);
  TopLayerPush(aElement);
  NotifyFullScreenChangedForMediaElement(aElement);
  UpdateViewportScrollbarOverrideForFullscreen(this);
}

void Document::TopLayerPush(Element& aElement) {
  const bool modal = aElement.State().HasState(ElementState::MODAL);

  TopLayerPop(aElement);
  if (nsIFrame* f = aElement.GetPrimaryFrame()) {
    f->MarkNeedsDisplayItemRebuild();
  }

  mTopLayer.AppendElement(do_GetWeakReference(&aElement));
  NS_ASSERTION(GetTopLayerTop() == &aElement, "Should match");

  if (modal) {
    aElement.AddStates(ElementState::TOPMOST_MODAL);

    bool foundExistingModalElement = false;
    for (const nsWeakPtr& weakPtr : Reversed(mTopLayer)) {
      nsCOMPtr<Element> element(do_QueryReferent(weakPtr));
      if (element && element != &aElement &&
          element->State().HasState(ElementState::TOPMOST_MODAL)) {
        element->RemoveStates(ElementState::TOPMOST_MODAL);
        foundExistingModalElement = true;
        break;
      }
    }

    if (!foundExistingModalElement) {
      Element* root = GetRootElement();
      MOZ_RELEASE_ASSERT(root, "top layer element outside of document?");
      if (&aElement != root) {
        // Add inert to the root element so that the inertness is applied to the
        // entire document.
        root->AddStates(ElementState::INERT);
      }
    }
  }
}

void Document::AddModalDialog(HTMLDialogElement& aDialogElement) {
  aDialogElement.AddStates(ElementState::MODAL);
  TopLayerPush(aDialogElement);
}

void Document::RemoveModalDialog(HTMLDialogElement& aDialogElement) {
  DebugOnly<Element*> removedElement = TopLayerPop(aDialogElement);
  MOZ_ASSERT(removedElement == &aDialogElement);
  aDialogElement.RemoveStates(ElementState::MODAL);
}

Element* Document::TopLayerPop(FunctionRef<bool(Element*)> aPredicate) {
  if (mTopLayer.IsEmpty()) {
    return nullptr;
  }

  // Remove the topmost element that qualifies aPredicate; This
  // is required is because the top layer contains not only
  // fullscreen elements, but also dialog elements.
  Element* removedElement = nullptr;
  for (auto i : Reversed(IntegerRange(mTopLayer.Length()))) {
    nsCOMPtr<Element> element(do_QueryReferent(mTopLayer[i]));
    if (element && aPredicate(element)) {
      removedElement = element;
      if (nsIFrame* f = element->GetPrimaryFrame()) {
        f->MarkNeedsDisplayItemRebuild();
      }
      mTopLayer.RemoveElementAt(i);
      break;
    }
  }

  // Pop from the stack null elements (references to elements which have
  // been GC'd since they were added to the stack) and elements which are
  // no longer in this document.
  //
  // FIXME(emilio): If this loop does something, it'd violate the assertions
  // from GetTopLayerTop()... What gives?
  while (!mTopLayer.IsEmpty()) {
    Element* element = GetTopLayerTop();
    if (!element || element->GetComposedDoc() != this) {
      if (element) {
        if (nsIFrame* f = element->GetPrimaryFrame()) {
          f->MarkNeedsDisplayItemRebuild();
        }
      }

      mTopLayer.RemoveLastElement();
    } else {
      // The top element of the stack is now an in-doc element. Return here.
      break;
    }
  }

  if (!removedElement) {
    return nullptr;
  }

  const bool modal = removedElement->State().HasState(ElementState::MODAL);

  if (modal) {
    removedElement->RemoveStates(ElementState::TOPMOST_MODAL);
    bool foundExistingModalElement = false;
    for (const nsWeakPtr& weakPtr : Reversed(mTopLayer)) {
      nsCOMPtr<Element> element(do_QueryReferent(weakPtr));
      if (element && element->State().HasState(ElementState::MODAL)) {
        element->AddStates(ElementState::TOPMOST_MODAL);
        foundExistingModalElement = true;
        break;
      }
    }
    // No more modal elements, make the document not inert anymore.
    if (!foundExistingModalElement) {
      Element* root = GetRootElement();
      if (root && !root->GetBoolAttr(nsGkAtoms::inert)) {
        root->RemoveStates(ElementState::INERT);
      }
    }
  }

  return removedElement;
}

Element* Document::TopLayerPop(Element& aElement) {
  auto predictFunc = [&aElement](Element* element) {
    return element == &aElement;
  };
  return TopLayerPop(predictFunc);
}

void Document::GetWireframe(bool aIncludeNodes,
                            Nullable<Wireframe>& aWireframe) {
  FlushPendingNotifications(FlushType::Layout);
  GetWireframeWithoutFlushing(aIncludeNodes, aWireframe);
}

void Document::GetWireframeWithoutFlushing(bool aIncludeNodes,
                                           Nullable<Wireframe>& aWireframe) {
  using FrameForPointOptions = nsLayoutUtils::FrameForPointOptions;
  using FrameForPointOption = nsLayoutUtils::FrameForPointOption;

  PresShell* shell = GetPresShell();
  if (!shell) {
    return;
  }

  nsPresContext* pc = shell->GetPresContext();
  if (!pc) {
    return;
  }

  nsIFrame* rootFrame = shell->GetRootFrame();
  if (!rootFrame) {
    return;
  }

  auto& wireframe = aWireframe.SetValue();
  wireframe.mCanvasBackground =
      shell->ComputeCanvasBackground().mViewport.mColor;

  FrameForPointOptions options;
  options.mBits += FrameForPointOption::IgnoreCrossDoc;
  options.mBits += FrameForPointOption::IgnorePaintSuppression;
  options.mBits += FrameForPointOption::OnlyVisible;

  AutoTArray<nsIFrame*, 32> frames;
  const RelativeTo relativeTo{rootFrame, mozilla::ViewportType::Layout};
  nsLayoutUtils::GetFramesForArea(relativeTo, pc->GetVisibleArea(), frames,
                                  options);

  // TODO(emilio): We could rewrite hit testing to return nsDisplayItem*s or
  // something perhaps, but seems hard / like it'd involve at least some extra
  // copying around, since they don't outlive GetFramesForArea.
  auto& rects = wireframe.mRects.Construct();
  if (!rects.SetCapacity(frames.Length(), fallible)) {
    return;
  }
  for (nsIFrame* frame : Reversed(frames)) {
    auto [rectColor,
          rectType] = [&]() -> std::tuple<nscolor, WireframeRectType> {
      if (frame->IsTextFrame()) {
        return {frame->StyleText()->mWebkitTextFillColor.CalcColor(frame),
                WireframeRectType::Text};
      }
      if (frame->IsImageFrame() || frame->IsSVGOuterSVGFrame()) {
        return {0, WireframeRectType::Image};
      }
      if (frame->IsThemed()) {
        return {0, WireframeRectType::Background};
      }
      bool drawImage = false;
      bool drawColor = false;
      if (const auto* bgStyle = nsCSSRendering::FindBackground(frame)) {
        const nscolor color = nsCSSRendering::DetermineBackgroundColor(
            pc, bgStyle, frame, drawImage, drawColor);
        if (drawImage &&
            !bgStyle->StyleBackground()->mImage.BottomLayer().mImage.IsNone()) {
          return {color, WireframeRectType::Image};
        }
        if (drawColor && !frame->IsCanvasFrame()) {
          // Canvas frame background already accounted for in mCanvasBackground.
          return {color, WireframeRectType::Background};
        }
      }
      return {0, WireframeRectType::Unknown};
    }();

    if (rectType == WireframeRectType::Unknown) {
      continue;
    }

    const auto r =
        CSSRect::FromAppUnits(nsLayoutUtils::TransformFrameRectToAncestor(
            frame, frame->GetRectRelativeToSelf(), relativeTo));
    if ((uint32_t)r.Area() <
        StaticPrefs::browser_history_wireframeAreaThreshold()) {
      continue;
    }

    // Can't really fail because SetCapacity succeeded.
    auto& taggedRect = *rects.AppendElement(fallible);

    if (aIncludeNodes) {
      if (nsIContent* c = frame->GetContent()) {
        taggedRect.mNode.Construct(c);
      }
    }
    taggedRect.mX = r.x;
    taggedRect.mY = r.y;
    taggedRect.mWidth = r.width;
    taggedRect.mHeight = r.height;
    taggedRect.mColor = rectColor;
    taggedRect.mType.Construct(rectType);
  }
}

Element* Document::GetTopLayerTop() {
  if (mTopLayer.IsEmpty()) {
    return nullptr;
  }
  uint32_t last = mTopLayer.Length() - 1;
  nsCOMPtr<Element> element(do_QueryReferent(mTopLayer[last]));
  NS_ASSERTION(element, "Should have a top layer element!");
  NS_ASSERTION(element->IsInComposedDoc(),
               "Top layer element should be in doc");
  NS_ASSERTION(element->OwnerDoc() == this,
               "Top layer element should be in this doc");
  return element;
}

Element* Document::GetUnretargetedFullscreenElement() const {
  for (const nsWeakPtr& weakPtr : Reversed(mTopLayer)) {
    nsCOMPtr<Element> element(do_QueryReferent(weakPtr));
    // Per spec, the fullscreen element is the topmost element in the document’s
    // top layer whose fullscreen flag is set, if any, and null otherwise.
    if (element && element->State().HasState(ElementState::FULLSCREEN)) {
      return element;
    }
  }
  return nullptr;
}

nsTArray<Element*> Document::GetTopLayer() const {
  nsTArray<Element*> elements;
  for (const nsWeakPtr& ptr : mTopLayer) {
    if (nsCOMPtr<Element> elem = do_QueryReferent(ptr)) {
      elements.AppendElement(elem);
    }
  }
  return elements;
}

bool Document::TopLayerContains(Element& aElement) const {
  if (mTopLayer.IsEmpty()) {
    return false;
  }
  nsWeakPtr weakElement = do_GetWeakReference(&aElement);
  return mTopLayer.Contains(weakElement);
}

void Document::HideAllPopoversUntil(nsINode& aEndpoint,
                                    bool aFocusPreviousElement,
                                    bool aFireEvents) {
  auto closeAllOpenPopovers = [&aFocusPreviousElement, &aFireEvents,
                               this]() MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION {
    while (RefPtr<Element> topmost = GetTopmostAutoPopover()) {
      HidePopover(*topmost, aFocusPreviousElement, aFireEvents, IgnoreErrors());
    }
  };

  if (aEndpoint.IsElement() && !aEndpoint.AsElement()->IsPopoverOpen()) {
    return;
  }

  if (&aEndpoint == this) {
    closeAllOpenPopovers();
    return;
  }

  // https://github.com/whatwg/html/pull/9198
  auto needRepeatingHide = [&]() {
    auto autoList = AutoPopoverList();
    return autoList.Contains(&aEndpoint) &&
           &aEndpoint != autoList.LastElement();
  };

  MOZ_ASSERT((&aEndpoint)->IsElement() &&
             (&aEndpoint)->AsElement()->IsAutoPopover());
  bool repeatingHide = false;
  bool fireEvents = aFireEvents;
  do {
    RefPtr<const Element> lastToHide = nullptr;
    bool foundEndpoint = false;
    for (const Element* popover : AutoPopoverList()) {
      if (popover == &aEndpoint) {
        foundEndpoint = true;
      } else if (foundEndpoint) {
        lastToHide = popover;
        break;
      }
    }

    if (!foundEndpoint) {
      closeAllOpenPopovers();
      return;
    }

    while (lastToHide && lastToHide->IsPopoverOpen()) {
      RefPtr<Element> topmost = GetTopmostAutoPopover();
      if (!topmost) {
        break;
      }
      HidePopover(*topmost, aFocusPreviousElement, fireEvents, IgnoreErrors());
    }

    repeatingHide = needRepeatingHide();
    if (repeatingHide) {
      fireEvents = false;
    }
  } while (repeatingHide);
}

void Document::HidePopover(Element& aPopover, bool aFocusPreviousElement,
                           bool aFireEvents, ErrorResult& aRv) {
  RefPtr<nsGenericHTMLElement> popoverHTMLEl =
      nsGenericHTMLElement::FromNode(aPopover);
  NS_ASSERTION(popoverHTMLEl, "Not a HTML element");

  if (!popoverHTMLEl->CheckPopoverValidity(PopoverVisibilityState::Showing,
                                           nullptr, aRv)) {
    return;
  }

  bool wasShowingOrHiding =
      popoverHTMLEl->GetPopoverData()->IsShowingOrHiding();
  popoverHTMLEl->GetPopoverData()->SetIsShowingOrHiding(true);
  const bool fireEvents = aFireEvents && !wasShowingOrHiding;
  auto cleanupHidingFlag = MakeScopeExit([&]() {
    if (auto* popoverData = popoverHTMLEl->GetPopoverData()) {
      popoverData->SetIsShowingOrHiding(wasShowingOrHiding);
      if (auto* closeWatcher = popoverData->GetCloseWatcher()) {
        closeWatcher->Destroy();
      }
    }
  });

  if (popoverHTMLEl->IsAutoPopover()) {
    HideAllPopoversUntil(*popoverHTMLEl, aFocusPreviousElement, fireEvents);
    if (!popoverHTMLEl->CheckPopoverValidity(PopoverVisibilityState::Showing,
                                             nullptr, aRv)) {
      return;
    }
    // TODO: we can't always guarantee:
    // The last item in document's auto popover list is popoverHTMLEl.
    // See, https://github.com/whatwg/html/issues/9197
    // If popoverHTMLEl is not on top, hide popovers again without firing
    // events.
    if (NS_WARN_IF(GetTopmostAutoPopover() != popoverHTMLEl)) {
      HideAllPopoversUntil(*popoverHTMLEl, aFocusPreviousElement, false);
      if (!popoverHTMLEl->CheckPopoverValidity(PopoverVisibilityState::Showing,
                                               nullptr, aRv)) {
        return;
      }
      MOZ_ASSERT(GetTopmostAutoPopover() == popoverHTMLEl,
                 "popoverHTMLEl should be on top of auto popover list");
    }
  }

  auto* data = popoverHTMLEl->GetPopoverData();
  MOZ_ASSERT(data, "Should have popover data");
  data->SetInvoker(nullptr);

  // Fire beforetoggle event and re-check popover validity.
  if (fireEvents) {
    // Intentionally ignore the return value here as only on open event for
    // beforetoggle the cancelable attribute is initialized to true.
    popoverHTMLEl->FireToggleEvent(u"open"_ns, u"closed"_ns,
                                   u"beforetoggle"_ns);

    // https://html.spec.whatwg.org/multipage/popover.html#hide-popover-algorithm
    // step 10.2.
    // Hide all popovers when beforetoggle shows a popover.
    if (popoverHTMLEl->IsAutoPopover() &&
        GetTopmostAutoPopover() != popoverHTMLEl &&
        popoverHTMLEl->PopoverOpen()) {
      HideAllPopoversUntil(*popoverHTMLEl, aFocusPreviousElement, false);
    }

    if (!popoverHTMLEl->CheckPopoverValidity(PopoverVisibilityState::Showing,
                                             nullptr, aRv)) {
      return;
    }
  }

  RemovePopoverFromTopLayer(aPopover);

  popoverHTMLEl->PopoverPseudoStateUpdate(false, true);
  popoverHTMLEl->GetPopoverData()->SetPopoverVisibilityState(
      PopoverVisibilityState::Hidden);

  // Queue popover toggle event task.
  if (fireEvents) {
    popoverHTMLEl->QueuePopoverEventTask(PopoverVisibilityState::Showing);
  }

  if (aFocusPreviousElement) {
    popoverHTMLEl->FocusPreviousElementAfterHidingPopover();
  } else {
    popoverHTMLEl->ForgetPreviouslyFocusedElementAfterHidingPopover();
  }
}

nsTArray<Element*> Document::AutoPopoverList() const {
  nsTArray<Element*> elements;
  for (const nsWeakPtr& ptr : mTopLayer) {
    if (nsCOMPtr<Element> element = do_QueryReferent(ptr)) {
      if (element && element->IsAutoPopover() && element->IsPopoverOpen()) {
        elements.AppendElement(element);
      }
    }
  }
  return elements;
}

Element* Document::GetTopmostAutoPopover() const {
  for (const nsWeakPtr& weakPtr : Reversed(mTopLayer)) {
    nsCOMPtr<Element> element(do_QueryReferent(weakPtr));
    if (element && element->IsAutoPopover() && element->IsPopoverOpen()) {
      return element;
    }
  }
  return nullptr;
}

void Document::AddToAutoPopoverList(Element& aElement) {
  MOZ_ASSERT(aElement.IsAutoPopover());
  TopLayerPush(aElement);
}

void Document::RemoveFromAutoPopoverList(Element& aElement) {
  MOZ_ASSERT(aElement.IsAutoPopover());
  TopLayerPop(aElement);
}

void Document::AddPopoverToTopLayer(Element& aElement) {
  MOZ_ASSERT(aElement.GetPopoverData());
  TopLayerPush(aElement);
}

void Document::RemovePopoverFromTopLayer(Element& aElement) {
  MOZ_ASSERT(aElement.GetPopoverData());
  TopLayerPop(aElement);
}

// Returns true if aDoc browsing context is focused.
bool IsInFocusedTab(Document* aDoc) {
  BrowsingContext* bc = aDoc->GetBrowsingContext();
  if (!bc) {
    return false;
  }

  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    return false;
  }

  if (XRE_IsParentProcess()) {
    // Keep dom/tests/mochitest/chrome/test_MozDomFullscreen_event.xhtml happy
    // by retaining the old code path for the parent process.
    nsIDocShell* docshell = aDoc->GetDocShell();
    if (!docshell) {
      return false;
    }
    nsCOMPtr<nsIDocShellTreeItem> rootItem;
    docshell->GetInProcessRootTreeItem(getter_AddRefs(rootItem));
    if (!rootItem) {
      return false;
    }
    nsCOMPtr<nsPIDOMWindowOuter> rootWin = rootItem->GetWindow();
    if (!rootWin) {
      return false;
    }

    nsCOMPtr<nsPIDOMWindowOuter> activeWindow;
    activeWindow = fm->GetActiveWindow();
    if (!activeWindow) {
      return false;
    }

    return activeWindow == rootWin;
  }

  return fm->GetActiveBrowsingContext() == bc->Top();
}

// Returns true if aDoc browsing context is focused and is also active.
bool IsInActiveTab(Document* aDoc) {
  if (!IsInFocusedTab(aDoc)) {
    return false;
  }

  BrowsingContext* bc = aDoc->GetBrowsingContext();
  MOZ_ASSERT(bc, "With no BrowsingContext, we should have failed earlier.");
  return bc->IsActive();
}

void Document::RemoteFrameFullscreenChanged(Element* aFrameElement) {
  // Ensure the frame element is the fullscreen element in this document.
  // If the frame element is already the fullscreen element in this document,
  // this has no effect.
  auto request = FullscreenRequest::CreateForRemote(aFrameElement);
  RequestFullscreen(std::move(request), XRE_IsContentProcess());
}

void Document::RemoteFrameFullscreenReverted() {
  UniquePtr<FullscreenExit> exit = FullscreenExit::CreateForRemote(this);
  RestorePreviousFullscreenState(std::move(exit));
}

static bool HasFullscreenSubDocument(Document& aDoc) {
  uint32_t count = CountFullscreenSubDocuments(aDoc);
  NS_ASSERTION(count <= 1,
               "Fullscreen docs should have at most 1 fullscreen child!");
  return count >= 1;
}

// Returns nullptr if a request for Fullscreen API is currently enabled
// in the given document. Returns a static string indicates the reason
// why it is not enabled otherwise.
const char* Document::GetFullscreenError(CallerType aCallerType) {
  if (!StaticPrefs::full_screen_api_enabled()) {
    return "FullscreenDeniedDisabled";
  }

  if (aCallerType == CallerType::System) {
    // Chrome code can always use the fullscreen API, provided it's not
    // explicitly disabled.
    return nullptr;
  }

  if (!IsVisible()) {
    return "FullscreenDeniedHidden";
  }

  if (!FeaturePolicyUtils::IsFeatureAllowed(this, u"fullscreen"_ns)) {
    return "FullscreenDeniedFeaturePolicy";
  }

  // Ensure that all containing elements are <iframe> and have allowfullscreen
  // attribute set.
  BrowsingContext* bc = GetBrowsingContext();
  if (!bc || !bc->FullscreenAllowed()) {
    return "FullscreenDeniedContainerNotAllowed";
  }

  return nullptr;
}

bool Document::FullscreenElementReadyCheck(FullscreenRequest& aRequest) {
  Element* elem = aRequest.Element();
  // Strictly speaking, this isn't part of the fullscreen element ready
  // check in the spec, but per steps in the spec, when an element which
  // is already the fullscreen element requests fullscreen, nothing
  // should change and no event should be dispatched, but we still need
  // to resolve the returned promise.
  Element* fullscreenElement = GetUnretargetedFullscreenElement();
  if (elem == fullscreenElement) {
    aRequest.MayResolvePromise();
    return false;
  }
  if (!elem->IsInComposedDoc()) {
    aRequest.Reject("FullscreenDeniedNotInDocument");
    return false;
  }
  if (elem->IsPopoverOpen()) {
    aRequest.Reject("FullscreenDeniedPopoverOpen");
    return false;
  }
  if (elem->OwnerDoc() != this) {
    aRequest.Reject("FullscreenDeniedMovedDocument");
    return false;
  }
  if (!GetWindow()) {
    aRequest.Reject("FullscreenDeniedLostWindow");
    return false;
  }
  if (const char* msg = GetFullscreenError(aRequest.mCallerType)) {
    aRequest.Reject(msg);
    return false;
  }
  if (HasFullscreenSubDocument(*this)) {
    aRequest.Reject("FullscreenDeniedSubDocFullScreen");
    return false;
  }
  if (elem->IsHTMLElement(nsGkAtoms::dialog)) {
    aRequest.Reject("FullscreenDeniedHTMLDialog");
    return false;
  }
  if (!nsContentUtils::IsChromeDoc(this) && !IsInFocusedTab(this)) {
    aRequest.Reject("FullscreenDeniedNotFocusedTab");
    return false;
  }
  return true;
}

static nsCOMPtr<nsPIDOMWindowOuter> GetRootWindow(Document* aDoc) {
  MOZ_ASSERT(XRE_IsParentProcess());
  nsIDocShell* docShell = aDoc->GetDocShell();
  if (!docShell) {
    return nullptr;
  }
  nsCOMPtr<nsIDocShellTreeItem> rootItem;
  docShell->GetInProcessRootTreeItem(getter_AddRefs(rootItem));
  return rootItem ? rootItem->GetWindow() : nullptr;
}

static bool ShouldApplyFullscreenDirectly(Document* aDoc,
                                          nsPIDOMWindowOuter* aRootWin) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // If we are in the chrome process, and the window has not been in
  // fullscreen, we certainly need to make that fullscreen first.
  if (!aRootWin->GetFullScreen()) {
    return false;
  }
  // The iterator not being at end indicates there is still some
  // pending fullscreen request relates to this document. We have to
  // push the request to the pending queue so requests are handled
  // in the correct order.
  PendingFullscreenChangeList::Iterator<FullscreenRequest> iter(
      aDoc, PendingFullscreenChangeList::eDocumentsWithSameRoot);
  if (!iter.AtEnd()) {
    return false;
  }

  // Same thing for exits. If we have any pending, we have to push
  // to the pending queue.
  PendingFullscreenChangeList::Iterator<FullscreenExit> iterExit(
      aDoc, PendingFullscreenChangeList::eDocumentsWithSameRoot);
  if (!iterExit.AtEnd()) {
    return false;
  }

  // We have to apply the fullscreen state directly in this case,
  // because nsGlobalWindow::SetFullscreenInternal() will do nothing
  // if it is already in fullscreen. If we do not apply the state but
  // instead add it to the queue and wait for the window as normal,
  // we would get stuck.
  return true;
}

static bool CheckFullscreenAllowedElementType(const Element* elem) {
  // Per spec only HTML, <svg>, and <math> should be allowed, but
  // we also need to allow XUL elements right now.
  return elem->IsHTMLElement() || elem->IsXULElement() ||
         elem->IsSVGElement(nsGkAtoms::svg) ||
         elem->IsMathMLElement(nsGkAtoms::math);
}

void Document::RequestFullscreen(UniquePtr<FullscreenRequest> aRequest,
                                 bool aApplyFullscreenDirectly) {
  if (XRE_IsContentProcess()) {
    RequestFullscreenInContentProcess(std::move(aRequest),
                                      aApplyFullscreenDirectly);
  } else {
    RequestFullscreenInParentProcess(std::move(aRequest),
                                     aApplyFullscreenDirectly);
  }
}

void Document::RequestFullscreenInContentProcess(
    UniquePtr<FullscreenRequest> aRequest, bool aApplyFullscreenDirectly) {
  MOZ_ASSERT(XRE_IsContentProcess());

  // If we are in the content process, we can apply the fullscreen
  // state directly only if we have been in DOM fullscreen, because
  // otherwise we always need to notify the chrome.
  if (aApplyFullscreenDirectly ||
      nsContentUtils::GetInProcessSubtreeRootDocument(this)->Fullscreen()) {
    ApplyFullscreen(std::move(aRequest));
    return;
  }

  if (!CheckFullscreenAllowedElementType(aRequest->Element())) {
    aRequest->Reject("FullscreenDeniedNotHTMLSVGOrMathML");
    return;
  }

  // We don't need to check element ready before this point, because
  // if we called ApplyFullscreen, it would check that for us.
  if (!FullscreenElementReadyCheck(*aRequest)) {
    return;
  }

  PendingFullscreenChangeList::Add(std::move(aRequest));
  // If we are not the top level process, dispatch an event to make
  // our parent process go fullscreen first.
  Dispatch(NS_NewRunnableFunction(
      "Document::RequestFullscreenInContentProcess", [self = RefPtr{this}] {
        if (!self->HasPendingFullscreenRequests()) {
          return;
        }
        nsContentUtils::DispatchEventOnlyToChrome(
            self, self, u"MozDOMFullscreen:Request"_ns, CanBubble::eYes,
            Cancelable::eNo, /* DefaultAction */ nullptr);
      }));
}

void Document::RequestFullscreenInParentProcess(
    UniquePtr<FullscreenRequest> aRequest, bool aApplyFullscreenDirectly) {
  MOZ_ASSERT(XRE_IsParentProcess());
  nsCOMPtr<nsPIDOMWindowOuter> rootWin = GetRootWindow(this);
  if (!rootWin) {
    aRequest->MayRejectPromise("No active window");
    return;
  }

  if (aApplyFullscreenDirectly ||
      ShouldApplyFullscreenDirectly(this, rootWin)) {
    ApplyFullscreen(std::move(aRequest));
    return;
  }

  if (!CheckFullscreenAllowedElementType(aRequest->Element())) {
    aRequest->Reject("FullscreenDeniedNotHTMLSVGOrMathML");
    return;
  }

  // See if we're waiting on an exit. If so, just make this one pending.
  PendingFullscreenChangeList::Iterator<FullscreenExit> iter(
      this, PendingFullscreenChangeList::eDocumentsWithSameRoot);
  if (!iter.AtEnd()) {
    PendingFullscreenChangeList::Add(std::move(aRequest));
    rootWin->SetFullscreenInternal(FullscreenReason::ForFullscreenAPI, true);
    return;
  }

  // We don't need to check element ready before this point, because
  // if we called ApplyFullscreen, it would check that for us.
  if (!FullscreenElementReadyCheck(*aRequest)) {
    return;
  }

  PendingFullscreenChangeList::Add(std::move(aRequest));
  // Make the window fullscreen.
  rootWin->SetFullscreenInternal(FullscreenReason::ForFullscreenAPI, true);
}

/* static */
bool Document::HandlePendingFullscreenRequests(Document* aDoc) {
  bool handled = false;
  PendingFullscreenChangeList::Iterator<FullscreenRequest> iter(
      aDoc, PendingFullscreenChangeList::eDocumentsWithSameRoot);
  while (!iter.AtEnd()) {
    UniquePtr<FullscreenRequest> request = iter.TakeAndNext();
    Document* doc = request->Document();
    if (doc->ApplyFullscreen(std::move(request))) {
      handled = true;
    }
  }
  return handled;
}

/* static */
void Document::ClearPendingFullscreenRequests(Document* aDoc) {
  PendingFullscreenChangeList::Iterator<FullscreenRequest> iter(
      aDoc, PendingFullscreenChangeList::eInclusiveDescendants);
  while (!iter.AtEnd()) {
    UniquePtr<FullscreenRequest> request = iter.TakeAndNext();
    request->MayRejectPromise("Fullscreen request aborted");
  }
}

void Document::AddPendingFullscreenEvent(
    UniquePtr<PendingFullscreenEvent> aPendingEvent) {
  const bool wasEmpty = mPendingFullscreenEvents.IsEmpty();
  mPendingFullscreenEvents.AppendElement(std::move(aPendingEvent));
  if (wasEmpty) {
    MaybeScheduleRenderingPhases({RenderingPhase::FullscreenSteps});
  }
}

// https://fullscreen.spec.whatwg.org/#run-the-fullscreen-steps
void Document::RunFullscreenSteps() {
  auto events = std::move(mPendingFullscreenEvents);
  for (auto& event : events) {
    event->Dispatch(this);
  }
}

bool Document::HasPendingFullscreenRequests() {
  PendingFullscreenChangeList::Iterator<FullscreenRequest> iter(
      this, PendingFullscreenChangeList::eDocumentsWithSameRoot);
  return !iter.AtEnd();
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY
bool Document::ApplyFullscreen(UniquePtr<FullscreenRequest> aRequest) {
  if (!FullscreenElementReadyCheck(*aRequest)) {
    return false;
  }

  Element* elem = aRequest->Element();

  RefPtr<nsINode> hideUntil = elem->GetTopmostPopoverAncestor(nullptr, false);
  if (!hideUntil) {
    hideUntil = OwnerDoc();
  }

  RefPtr<Document> doc = aRequest->Document();
  doc->HideAllPopoversUntil(*hideUntil, false, true);

  // Stash a reference to any existing fullscreen doc, we'll use this later
  // to detect if the origin which is fullscreen has changed.
  nsCOMPtr<Document> previousFullscreenDoc = GetFullscreenLeaf(this);

  // Stores a list of documents which we must dispatch "fullscreenchange"
  // too. We're required by the spec to dispatch the events in root-to-leaf
  // order, but we traverse the doctree in a leaf-to-root order, so we save
  // references to the documents we must dispatch to so that we get the order
  // as specified.
  AutoTArray<Document*, 8> changed;

  // Remember the root document, so that if a fullscreen document is hidden
  // we can reset fullscreen state in the remaining visible fullscreen
  // documents.
  Document* fullScreenRootDoc =
      nsContentUtils::GetInProcessSubtreeRootDocument(this);

  // If a document is already in fullscreen, then unlock the mouse pointer
  // before setting a new document to fullscreen
  PointerLockManager::Unlock("Document::ApplyFullscreen");

  // Set the fullscreen element. This sets the fullscreen style on the
  // element, and the fullscreen-ancestor styles on ancestors of the element
  // in this document.
  SetFullscreenElement(*elem);
  // Set the iframe fullscreen flag.
  if (auto* iframe = HTMLIFrameElement::FromNode(elem)) {
    iframe->SetFullscreenFlag(true);
  }
  changed.AppendElement(this);

  // Propagate up the document hierarchy, setting the fullscreen element as
  // the element's container in ancestor documents. This also sets the
  // appropriate css styles as well. Note we don't propagate down the
  // document hierarchy, the fullscreen element (or its container) is not
  // visible there. Stop when we reach the root document.
  Document* child = this;
  while (true) {
    child->SetFullscreenRoot(fullScreenRootDoc);

    // When entering fullscreen, reset the RCD's resolution to the intrinsic
    // resolution, otherwise the fullscreen content could be sized larger than
    // the screen (since fullscreen is implemented using position:fixed and
    // fixed elements are sized to the layout viewport).
    // This also ensures that things like video controls aren't zoomed in
    // when in fullscreen mode.
    if (PresShell* presShell = child->GetPresShell()) {
      if (RefPtr<MobileViewportManager> manager =
              presShell->GetMobileViewportManager()) {
        // Save the previous resolution so it can be restored.
        child->mSavedResolution = presShell->GetResolution();
        presShell->SetResolutionAndScaleTo(
            manager->ComputeIntrinsicResolution(),
            ResolutionChangeOrigin::MainThreadRestore);
      }
    }

    NS_ASSERTION(child->GetFullscreenRoot() == fullScreenRootDoc,
                 "Fullscreen root should be set!");
    if (child == fullScreenRootDoc) {
      break;
    }

    Element* element = child->GetEmbedderElement();
    if (!element) {
      // We've reached the root.No more changes need to be made
      // to the top layer stacks of documents further up the tree.
      break;
    }

    Document* parent = child->GetInProcessParentDocument();
    parent->SetFullscreenElement(*element);
    changed.AppendElement(parent);
    child = parent;
  }

  FullscreenRoots::Add(this);

  // If it is the first entry of the fullscreen, trigger an event so
  // that the UI can response to this change, e.g. hide chrome, or
  // notifying parent process to enter fullscreen. Note that chrome
  // code may also want to listen to MozDOMFullscreen:NewOrigin event
  // to pop up warning UI.
  if (!previousFullscreenDoc) {
    nsContentUtils::DispatchEventOnlyToChrome(
        this, elem, u"MozDOMFullscreen:Entered"_ns, CanBubble::eYes,
        Cancelable::eNo, /* DefaultAction */ nullptr);
  }

  // The origin which is fullscreen gets changed. Trigger an event so
  // that the chrome knows to pop up a warning UI. Note that
  // previousFullscreenDoc == nullptr upon first entry, we show the warning UI
  // directly as soon as chrome document goes into fullscreen state. Also note
  // that, in a multi-process browser, the code in content process is
  // responsible for sending message with the origin to its parent, and the
  // parent shouldn't rely on this event itself.
  if (aRequest->mShouldNotifyNewOrigin && previousFullscreenDoc &&
      !nsContentUtils::HaveEqualPrincipals(previousFullscreenDoc, this)) {
    DispatchFullscreenNewOriginEvent(this);
  }

  // Dispatch "fullscreenchange" events. Note that the loop order is
  // reversed so that events are dispatched in the tree order as
  // indicated in the spec.
  for (Document* d : Reversed(changed)) {
    DispatchFullscreenChange(*d, d->GetUnretargetedFullscreenElement());
  }
  aRequest->MayResolvePromise();
  return true;
}

void Document::ClearOrientationPendingPromise() {
  mOrientationPendingPromise = nullptr;
}

bool Document::SetOrientationPendingPromise(Promise* aPromise) {
  if (mIsGoingAway) {
    return false;
  }

  mOrientationPendingPromise = aPromise;
  return true;
}

void Document::MaybeSkipTransitionAfterVisibilityChange() {
  if (Hidden() && mActiveViewTransition) {
    mActiveViewTransition->SkipTransition(SkipTransitionReason::DocumentHidden);
  }
}

// https://drafts.csswg.org/css-view-transitions-1/#schedule-the-update-callback
void Document::ScheduleViewTransitionUpdateCallback(ViewTransition* aVt) {
  MOZ_ASSERT(aVt);
  const bool hasTasks = !mViewTransitionUpdateCallbacks.IsEmpty();

  // 1. Append transition to transition’s relevant settings object’s update
  // callback queue.
  mViewTransitionUpdateCallbacks.AppendElement(aVt);

  // 2. Queue a global task on the DOM manipulation task source, given
  // transition’s relevant global object, to flush the update callback queue.
  // Note: |hasTasks| means the list was not empty, so there must be a global
  // task there already.
  if (!hasTasks) {
    Dispatch(NewRunnableMethod(
        "Document::FlushViewTransitionUpdateCallbackQueue", this,
        &Document::FlushViewTransitionUpdateCallbackQueue));
  }
}

// https://drafts.csswg.org/css-view-transitions-1/#flush-the-update-callback-queue
void Document::FlushViewTransitionUpdateCallbackQueue() {
  // 1. For each transition in document’s update callback queue, call the update
  // callback given transition.
  // Note: we move mViewTransitionUpdateCallbacks into a temporary array to make
  // sure no one updates the array when iterating.
  auto callbacks = std::move(mViewTransitionUpdateCallbacks);
  MOZ_ASSERT(mViewTransitionUpdateCallbacks.IsEmpty());
  for (RefPtr<ViewTransition>& vt : callbacks) {
    MOZ_KnownLive(vt)->CallUpdateCallback(IgnoreErrors());
  }

  // 2. Set document’s update callback queue to an empty list.
  // mViewTransitionUpdateCallbacks is empty after the 1st step.
}

// https://html.spec.whatwg.org/#update-the-visibility-state
void Document::UpdateVisibilityState(DispatchVisibilityChange aDispatchEvent) {
  const dom::VisibilityState visibilityState = ComputeVisibilityState();
  if (mVisibilityState == visibilityState) {
    // 1. If document's visibility state equals visibilityState, then return.
    return;
  }
  // 2. Set document's visibility state to visibilityState.
  mVisibilityState = visibilityState;
  if (aDispatchEvent == DispatchVisibilityChange::Yes) {
    // 3. Fire an event named visibilitychange at document, with its bubbles
    // attribute initialized to true.
    nsContentUtils::DispatchTrustedEvent(this, this, u"visibilitychange"_ns,
                                         CanBubble::eYes, Cancelable::eNo);
  }
  // TODO 4. Run the screen orientation change steps with document.

  // 5. Run the view transition page visibility change steps with document.
  // https://drafts.csswg.org/css-view-transitions/#view-transition-page-visibility-change-steps
  const bool visible = !Hidden();
  if (mActiveViewTransition && !visible) {
    // The mActiveViewTransition check is an optimization: We don't allow
    // creating transitions while the doc is hidden.
    Dispatch(
        NewRunnableMethod("MaybeSkipTransitionAfterVisibilityChange", this,
                          &Document::MaybeSkipTransitionAfterVisibilityChange));
  }

  NotifyActivityChanged();
  if (visible) {
    MaybeScheduleRendering();
    MaybeActiveMediaComponents();
  }

  for (auto* listener : mWorkerListeners) {
    listener->OnVisible(visible);
  }

  // https://w3c.github.io/screen-wake-lock/#handling-document-loss-of-visibility
  if (!visible) {
    UnlockAllWakeLocks(WakeLockType::Screen);
  }
}

void Document::AddWorkerDocumentListener(WorkerDocumentListener* aListener) {
  mWorkerListeners.Insert(aListener);
  aListener->OnVisible(!Hidden());
}

void Document::RemoveWorkerDocumentListener(WorkerDocumentListener* aListener) {
  mWorkerListeners.Remove(aListener);
}

VisibilityState Document::ComputeVisibilityState() const {
  // We have to check a few pieces of information here:
  // 1)  Are we in bfcache (!IsVisible())?  If so, nothing else matters.
  // 2)  Do we have an outer window?  If not, we're hidden.  Note that we don't
  //     want to use GetWindow here because it does weird groveling for windows
  //     in some cases.
  // 3)  Is our outer window background?  If so, we're hidden.
  // Otherwise, we're visible.
  if (!IsVisible() || !mWindow || !mWindow->GetOuterWindow() ||
      mWindow->GetOuterWindow()->IsBackground()) {
    return dom::VisibilityState::Hidden;
  }

  return dom::VisibilityState::Visible;
}

void Document::PostVisibilityUpdateEvent() {
  nsCOMPtr<nsIRunnable> event = NewRunnableMethod<DispatchVisibilityChange>(
      "Document::UpdateVisibilityState", this, &Document::UpdateVisibilityState,
      DispatchVisibilityChange::Yes);
  Dispatch(event.forget());
}

void Document::MaybeActiveMediaComponents() {
  auto* window = GetWindow();
  if (!window || !window->ShouldDelayMediaFromStart()) {
    return;
  }
  window->ActivateMediaComponents();
}

void Document::DocAddSizeOfExcludingThis(nsWindowSizes& aWindowSizes) const {
  nsINode::AddSizeOfExcludingThis(aWindowSizes,
                                  &aWindowSizes.mDOMSizes.mDOMOtherSize);

  for (nsIContent* kid = GetFirstChild(); kid; kid = kid->GetNextSibling()) {
    AddSizeOfNodeTree(*kid, aWindowSizes);
  }

  // IMPORTANT: for our ComputedValues measurements, we want to measure
  // ComputedValues accessible from DOM elements before ComputedValues not
  // accessible from DOM elements (i.e. accessible only from the frame tree).
  //
  // Therefore, the measurement of the Document superclass must happen after
  // the measurement of DOM nodes (above), because Document contains the
  // PresShell, which contains the frame tree.
  if (mPresShell) {
    mPresShell->AddSizeOfIncludingThis(aWindowSizes);
  }

  if (mStyleSet) {
    mStyleSet->AddSizeOfIncludingThis(aWindowSizes);
  }

  aWindowSizes.mPropertyTablesSize +=
      mPropertyTable.SizeOfExcludingThis(aWindowSizes.mState.mMallocSizeOf);

  if (EventListenerManager* elm = GetExistingListenerManager()) {
    aWindowSizes.mDOMEventListenersCount += elm->ListenerCount();
  }

  if (mNodeInfoManager) {
    mNodeInfoManager->AddSizeOfIncludingThis(aWindowSizes);
  }

  aWindowSizes.mDOMSizes.mDOMMediaQueryLists +=
      mDOMMediaQueryLists.sizeOfExcludingThis(
          aWindowSizes.mState.mMallocSizeOf);

  for (const MediaQueryList* mql : mDOMMediaQueryLists) {
    aWindowSizes.mDOMSizes.mDOMMediaQueryLists +=
        mql->SizeOfExcludingThis(aWindowSizes.mState.mMallocSizeOf);
  }

  DocumentOrShadowRoot::AddSizeOfExcludingThis(aWindowSizes);

  for (auto& sheetArray : mAdditionalSheets) {
    AddSizeOfOwnedSheetArrayExcludingThis(aWindowSizes, sheetArray);
  }
  // Lumping in the loader with the style-sheets size is not ideal,
  // but most of the things in there are in fact stylesheets, so it
  // doesn't seem worthwhile to separate it out.
  // This can be null if we've already been unlinked.
  if (mCSSLoader) {
    aWindowSizes.mLayoutStyleSheetsSize +=
        mCSSLoader->SizeOfIncludingThis(aWindowSizes.mState.mMallocSizeOf);
  }

  aWindowSizes.mDOMSizes.mDOMResizeObserverControllerSize +=
      mResizeObservers.ShallowSizeOfExcludingThis(
          aWindowSizes.mState.mMallocSizeOf);

  if (mAttributeStyles) {
    aWindowSizes.mDOMSizes.mDOMOtherSize +=
        mAttributeStyles->DOMSizeOfIncludingThis(
            aWindowSizes.mState.mMallocSizeOf);
  }

  if (mRadioGroupContainer) {
    aWindowSizes.mDOMSizes.mDOMOtherSize +=
        mRadioGroupContainer->SizeOfIncludingThis(
            aWindowSizes.mState.mMallocSizeOf);
  }

  aWindowSizes.mDOMSizes.mDOMOtherSize +=
      mStyledLinks.ShallowSizeOfExcludingThis(
          aWindowSizes.mState.mMallocSizeOf);

  // Measurement of the following members may be added later if DMD finds it
  // is worthwhile:
  // - mMidasCommandManager
  // - many!
}

void Document::DocAddSizeOfIncludingThis(nsWindowSizes& aWindowSizes) const {
  aWindowSizes.mDOMSizes.mDOMOtherSize +=
      aWindowSizes.mState.mMallocSizeOf(this);
  DocAddSizeOfExcludingThis(aWindowSizes);
}

void Document::AddSizeOfExcludingThis(nsWindowSizes& aSizes,
                                      size_t* aNodeSize) const {
  // This AddSizeOfExcludingThis() overrides the one from nsINode.  But
  // nsDocuments can only appear at the top of the DOM tree, and we use the
  // specialized DocAddSizeOfExcludingThis() in that case.  So this should never
  // be called.
  MOZ_CRASH();
}

/* static */
void Document::AddSizeOfNodeTree(nsINode& aNode, nsWindowSizes& aWindowSizes) {
  size_t nodeSize = 0;
  aNode.AddSizeOfIncludingThis(aWindowSizes, &nodeSize);

  // This is where we transfer the nodeSize obtained from
  // nsINode::AddSizeOfIncludingThis() to a value in nsWindowSizes.
  switch (aNode.NodeType()) {
    case nsINode::ELEMENT_NODE:
      aWindowSizes.mDOMSizes.mDOMElementNodesSize += nodeSize;
      break;
    case nsINode::TEXT_NODE:
      aWindowSizes.mDOMSizes.mDOMTextNodesSize += nodeSize;
      break;
    case nsINode::CDATA_SECTION_NODE:
      aWindowSizes.mDOMSizes.mDOMCDATANodesSize += nodeSize;
      break;
    case nsINode::COMMENT_NODE:
      aWindowSizes.mDOMSizes.mDOMCommentNodesSize += nodeSize;
      break;
    default:
      aWindowSizes.mDOMSizes.mDOMOtherSize += nodeSize;
      break;
  }

  if (EventListenerManager* elm = aNode.GetExistingListenerManager()) {
    aWindowSizes.mDOMEventListenersCount += elm->ListenerCount();
  }

  if (aNode.IsContent()) {
    nsTArray<nsIContent*> anonKids;
    nsContentUtils::AppendNativeAnonymousChildren(aNode.AsContent(), anonKids,
                                                  nsIContent::eAllChildren);
    for (nsIContent* anonKid : anonKids) {
      AddSizeOfNodeTree(*anonKid, aWindowSizes);
    }

    if (auto* element = Element::FromNode(aNode)) {
      if (ShadowRoot* shadow = element->GetShadowRoot()) {
        AddSizeOfNodeTree(*shadow, aWindowSizes);
      }
    }
  }

  // NOTE(emilio): If you feel smart and want to change this function to use
  // GetNextNode(), think twice, since you'd need to handle <xbl:content> in a
  // sane way, and kids of <content> won't point to the parent, so we'd never
  // find the root node where we should stop at.
  for (nsIContent* kid = aNode.GetFirstChild(); kid;
       kid = kid->GetNextSibling()) {
    AddSizeOfNodeTree(*kid, aWindowSizes);
  }
}

already_AddRefed<Document> Document::Constructor(const GlobalObject& aGlobal,
                                                 ErrorResult& rv) {
  nsCOMPtr<nsIScriptGlobalObject> global =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!global) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsCOMPtr<nsIScriptObjectPrincipal> prin =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!prin) {
    rv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  nsCOMPtr<nsIURI> uri;
  NS_NewURI(getter_AddRefs(uri), "about:blank");
  if (!uri) {
    rv.Throw(NS_ERROR_OUT_OF_MEMORY);
    return nullptr;
  }

  nsCOMPtr<Document> doc;
  nsresult res = NS_NewDOMDocument(getter_AddRefs(doc), VoidString(), u""_ns,
                                   nullptr, uri, uri, prin->GetPrincipal(),
                                   true, global, DocumentFlavor::Plain);
  if (NS_FAILED(res)) {
    rv.Throw(res);
    return nullptr;
  }

  doc->SetReadyStateInternal(Document::READYSTATE_COMPLETE);

  return doc.forget();
}

UniquePtr<XPathExpression> Document::CreateExpression(
    const nsAString& aExpression, XPathNSResolver* aResolver, ErrorResult& rv) {
  return XPathEvaluator()->CreateExpression(aExpression, aResolver, rv);
}

nsINode* Document::CreateNSResolver(nsINode& aNodeResolver) {
  return XPathEvaluator()->CreateNSResolver(aNodeResolver);
}

already_AddRefed<XPathResult> Document::Evaluate(
    JSContext* aCx, const nsAString& aExpression, nsINode& aContextNode,
    XPathNSResolver* aResolver, uint16_t aType, JS::Handle<JSObject*> aResult,
    ErrorResult& rv) {
  return XPathEvaluator()->Evaluate(aCx, aExpression, aContextNode, aResolver,
                                    aType, aResult, rv);
}

already_AddRefed<nsIAppWindow> Document::GetAppWindowIfToplevelChrome() const {
  nsCOMPtr<nsIDocShellTreeItem> item = GetDocShell();
  if (!item) {
    return nullptr;
  }
  nsCOMPtr<nsIDocShellTreeOwner> owner;
  item->GetTreeOwner(getter_AddRefs(owner));
  nsCOMPtr<nsIAppWindow> appWin = do_GetInterface(owner);
  if (!appWin) {
    return nullptr;
  }
  nsCOMPtr<nsIDocShell> appWinShell;
  appWin->GetDocShell(getter_AddRefs(appWinShell));
  if (!SameCOMIdentity(appWinShell, item)) {
    return nullptr;
  }
  return appWin.forget();
}

WindowContext* Document::GetTopLevelWindowContext() const {
  WindowContext* windowContext = GetWindowContext();
  return windowContext ? windowContext->TopWindowContext() : nullptr;
}

Document* Document::GetTopLevelContentDocumentIfSameProcess() {
  Document* parent;

  if (!mLoadedAsData) {
    parent = this;
  } else {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(GetScopeObject());
    if (!window) {
      return nullptr;
    }

    parent = window->GetExtantDoc();
    if (!parent) {
      return nullptr;
    }
  }

  do {
    if (parent->IsTopLevelContentDocument()) {
      break;
    }

    // If we ever have a non-content parent before we hit a toplevel content
    // parent, then we're never going to find one.  Just bail.
    if (!parent->IsContentDocument()) {
      return nullptr;
    }

    parent = parent->GetInProcessParentDocument();
  } while (parent);

  return parent;
}

const Document* Document::GetTopLevelContentDocumentIfSameProcess() const {
  return const_cast<Document*>(this)->GetTopLevelContentDocumentIfSameProcess();
}

void Document::PropagateImageUseCounters(Document* aReferencingDocument) {
  MOZ_ASSERT(IsBeingUsedAsImage());
  MOZ_ASSERT(aReferencingDocument);

  if (!aReferencingDocument->mShouldReportUseCounters) {
    // No need to propagate use counters to a document that itself won't report
    // use counters.
    return;
  }

  MOZ_LOG(gUseCountersLog, LogLevel::Debug,
          ("PropagateImageUseCounters from %s to %s",
           nsContentUtils::TruncatedURLForDisplay(mDocumentURI).get(),
           nsContentUtils::TruncatedURLForDisplay(
               aReferencingDocument->mDocumentURI)
               .get()));

  if (aReferencingDocument->IsBeingUsedAsImage()) {
    NS_WARNING(
        "Page use counters from nested image documents may not "
        "propagate to the top-level document (bug 1657805)");
  }

  SetCssUseCounterBits();
  aReferencingDocument->mChildDocumentUseCounters |= mUseCounters;
  aReferencingDocument->mChildDocumentUseCounters |= mChildDocumentUseCounters;
}

bool Document::HasScriptsBlockedBySandbox() const {
  return mSandboxFlags & SANDBOXED_SCRIPTS;
}

void Document::SetCssUseCounterBits() {
  if (StaticPrefs::layout_css_use_counters_enabled()) {
    for (size_t i = 0; i < eCSSProperty_COUNT_with_aliases; ++i) {
      auto id = nsCSSPropertyID(i);
      if (Servo_IsPropertyIdRecordedInUseCounter(mStyleUseCounters.get(), id)) {
        SetUseCounter(nsCSSProps::UseCounterFor(id));
      }
    }
  }

  if (StaticPrefs::layout_css_use_counters_unimplemented_enabled()) {
    for (size_t i = 0; i < size_t(CountedUnknownProperty::Count); ++i) {
      auto id = CountedUnknownProperty(i);
      if (Servo_IsUnknownPropertyRecordedInUseCounter(mStyleUseCounters.get(),
                                                      id)) {
        SetUseCounter(UseCounter(eUseCounter_FirstCountedUnknownProperty + i));
      }
    }
  }
}

void Document::InitUseCounters() {
  // We can be called more than once, e.g. when session history navigation shows
  // us a second time.
  if (mUseCountersInitialized) {
    return;
  }
  mUseCountersInitialized = true;

  if (!ShouldIncludeInTelemetry()) {
    return;
  }

  // Now we know for sure that we should report use counters from this document.
  mShouldReportUseCounters = true;

  WindowContext* top = GetWindowContextForPageUseCounters();
  if (!top) {
    // This is the case for SVG image documents.  They are not displayed in a
    // window, but we still do want to record document use counters for them.
    //
    // Page use counter propagation is handled in PropagateImageUseCounters,
    // so there is no need to use the cross-process machinery to send them.
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            ("InitUseCounters for a non-displayed document [%s]",
             nsContentUtils::TruncatedURLForDisplay(mDocumentURI).get()));
    return;
  }

  RefPtr<WindowGlobalChild> wgc = GetWindowGlobalChild();
  if (!wgc) {
    return;
  }

  MOZ_LOG(gUseCountersLog, LogLevel::Debug,
          ("InitUseCounters for a displayed document: %" PRIu64 " -> %" PRIu64
           " [from %s]",
           wgc->InnerWindowId(), top->Id(),
           nsContentUtils::TruncatedURLForDisplay(mDocumentURI).get()));

  // Inform the parent process that we will send it page use counters later on.
  wgc->SendExpectPageUseCounters(top);
  mShouldSendPageUseCounters = true;
}

// We keep separate counts for individual documents and top-level
// pages to more accurately track how many web pages might break if
// certain features were removed.  Consider the case of a single
// HTML document with several SVG images and/or iframes with
// sub-documents of their own.  If we maintained a single set of use
// counters and all the sub-documents use a particular feature, then
// telemetry would indicate that we would be breaking N documents if
// that feature were removed.  Whereas with a document/top-level
// page split, we can see that N documents would be affected, but
// only a single web page would be affected.
//
// The difference between the values of these two counts and the
// related use counters below tell us how many pages did *not* use
// the feature in question.  For instance, if we see that a given
// session has destroyed 30 content documents, but a particular use
// counter shows only a count of 5, we can infer that the use
// counter was *not* used in 25 of those 30 documents.
//
// We do things this way, rather than accumulating a boolean flag
// for each use counter, to avoid sending data for features
// that don't get widely used.  Doing things in this fashion means
// smaller telemetry payloads and faster processing on the server
// side.
void Document::ReportDocumentUseCounters() {
  if (!mShouldReportUseCounters || mReportedDocumentUseCounters) {
    return;
  }

  mReportedDocumentUseCounters = true;

  // Note that a document is being destroyed.  See the comment above for how
  // use counter data are interpreted relative to this measurement.
  glean::use_counter::content_documents_destroyed.Add();

  // Ask all of our resource documents to report their own document use
  // counters.
  EnumerateExternalResources([](Document& aDoc) {
    aDoc.ReportDocumentUseCounters();
    return CallState::Continue;
  });

  // Copy StyleUseCounters into our document use counters.
  SetCssUseCounterBits();

  Maybe<nsCString> urlForLogging;
  const bool dumpCounters = StaticPrefs::dom_use_counters_dump_document();
  if (dumpCounters) {
    urlForLogging.emplace(
        nsContentUtils::TruncatedURLForDisplay(GetDocumentURI()));
  }

  // Report our per-document use counters.
  for (int32_t c = 0; c < eUseCounter_Count; ++c) {
    auto uc = static_cast<UseCounter>(c);
    if (!mUseCounters[uc]) {
      continue;
    }

    const char* metricName = IncrementUseCounter(uc, /* aIsPage = */ false);
    if (dumpCounters) {
      printf_stderr("USE_COUNTER_DOCUMENT: %s - %s\n", metricName,
                    urlForLogging->get());
    }
  }
}

void Document::ReportLCP() {
  const nsDOMNavigationTiming* timing = GetNavigationTiming();

  if (!ShouldIncludeInTelemetry() || !IsTopLevelContentDocument() || !timing ||
      !timing->DocShellHasBeenActiveSinceNavigationStart()) {
    return;
  }

  TimeStamp lcpTime = timing->GetLargestContentfulRenderTimeStamp();

  if (!lcpTime) {
    return;
  }

  mozilla::glean::perf::largest_contentful_paint.AccumulateRawDuration(
      lcpTime - timing->GetNavigationStartTimeStamp());
  // GLAM EXPERIMENT
  // This metric is temporary, disabled by default, and will be enabled only
  // for the purpose of experimenting with client-side sampling of data for
  // GLAM use. See Bug 1947604 for more information.
  mozilla::glean::glam_experiment::largest_contentful_paint
      .AccumulateRawDuration(lcpTime - timing->GetNavigationStartTimeStamp());

  if (!GetChannel()) {
    return;
  }

  nsCOMPtr<nsITimedChannel> timedChannel(do_QueryInterface(GetChannel()));
  if (!timedChannel) {
    return;
  }

  TimeStamp responseStart;
  timedChannel->GetResponseStart(&responseStart);

  if (!responseStart) {
    return;
  }

  mozilla::glean::perf::largest_contentful_paint_from_response_start
      .AccumulateRawDuration(lcpTime - responseStart);

  if (profiler_thread_is_being_profiled_for_markers()) {
    MarkerInnerWindowId innerWindowID =
        MarkerInnerWindowIdFromDocShell(GetDocShell());
    GetNavigationTiming()->MaybeAddLCPProfilerMarker(innerWindowID);
  }
}

void Document::SendPageUseCounters() {
  if (!mShouldReportUseCounters || !mShouldSendPageUseCounters) {
    return;
  }

  // Ask all of our resource documents to send their own document use
  // counters to the parent process to be counted as page use counters.
  EnumerateExternalResources([](Document& aDoc) {
    aDoc.SendPageUseCounters();
    return CallState::Continue;
  });

  // Send our use counters to the parent process to accumulate them towards the
  // page use counters for the top-level document.
  //
  // We take our own document use counters (those in mUseCounters) and any child
  // document use counters (those in mChildDocumentUseCounters) that have been
  // explicitly propagated up to us, which includes resource documents, static
  // clones, and SVG images.
  RefPtr<WindowGlobalChild> wgc = GetWindowGlobalChild();
  if (!wgc) {
    MOZ_ASSERT_UNREACHABLE(
        "SendPageUseCounters should be called while we still have access "
        "to our WindowContext");
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > too late to send page use counters"));
    return;
  }

  MOZ_LOG(gUseCountersLog, LogLevel::Debug,
          ("Sending page use counters: from WindowContext %" PRIu64 " [%s]",
           wgc->WindowContext()->Id(),
           nsContentUtils::TruncatedURLForDisplay(GetDocumentURI()).get()));

  // Copy StyleUseCounters into our document use counters.
  SetCssUseCounterBits();

  UseCounters counters = mUseCounters | mChildDocumentUseCounters;
  wgc->SendAccumulatePageUseCounters(counters);
}

void Document::MaybeRecomputePartitionKey() {
  // We only need to recompute the partition key for the top-level content
  // document.
  if (!IsTopLevelContentDocument()) {
    return;
  }

  // Bail out early if there is no cookieJarSettings for this document. For
  // example, a chrome document.
  if (!mCookieJarSettings) {
    return;
  }

  // Check whether the partition key matches the document's node principal. They
  // can be different if the document is sandboxed. In this case, the node
  // principal is a null principal. But the partitionKey of the
  // cookieJarSettings was derived from the channel URI of the top-level
  // loading, which isn't a null principal. Therefore, we need to recompute
  // the partition Key from the document's node principal to reflect the actual
  // partition Key.
  nsAutoCString originNoSuffix;
  nsresult rv = NodePrincipal()->GetOriginNoSuffix(originNoSuffix);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIURI> originURI;
  rv = NS_NewURI(getter_AddRefs(originURI), originNoSuffix);
  NS_ENSURE_SUCCESS_VOID(rv);

  // Bail out early if we don't have a principal URI.
  if (!originURI) {
    return;
  }

  OriginAttributes attrs;
  attrs.SetPartitionKey(originURI, false);

  // We don't need to set the partition key if the cooieJarSettings'
  // partitionKey matches the document's node principal.
  if (attrs.mPartitionKey.Equals(
          net::CookieJarSettings::Cast(mCookieJarSettings)
              ->GetPartitionKey())) {
    return;
  }

  // Set the partition key to the document's node principal. So we will use the
  // right partition key afterward.
  mozilla::net::CookieJarSettings::Cast(mCookieJarSettings)
      ->SetPartitionKey(originURI, false);
}

bool Document::RecomputeResistFingerprinting(bool aForceRefreshRTPCallerType) {
  mOverriddenFingerprintingSettings.reset();
  const bool previous = mShouldResistFingerprinting;

  RefPtr<BrowsingContext> opener =
      GetBrowsingContext() ? GetBrowsingContext()->GetOpener() : nullptr;
  // If we have a parent or opener document, defer to it only when we have a
  // null principal (e.g. a sandboxed iframe or a data: uri) or when the
  // document's principal matches.  This means we will defer about:blank,
  // about:srcdoc, blob and same-origin iframes/popups to the parent/opener,
  // but not cross-origin ones.  Cross-origin iframes/popups may inherit a
  // CookieJarSettings.mShouldRFP = false bit however, which will be respected.
  auto shouldInheritFrom = [this](Document* aDoc) {
    return aDoc && (this->NodePrincipal()->Equals(aDoc->NodePrincipal()) ||
                    this->NodePrincipal()->GetIsNullPrincipal());
  };

  if (shouldInheritFrom(mParentDocument)) {
    MOZ_LOG(
        nsContentUtils::ResistFingerprintingLog(), LogLevel::Debug,
        ("Inside RecomputeResistFingerprinting with URI %s and deferring "
         "to parent document %s",
         GetDocumentURI() ? GetDocumentURI()->GetSpecOrDefault().get() : "null",
         mParentDocument->GetDocumentURI()->GetSpecOrDefault().get()));
    mShouldResistFingerprinting = mParentDocument->ShouldResistFingerprinting(
        RFPTarget::IsAlwaysEnabledForPrecompute);
    mOverriddenFingerprintingSettings =
        mParentDocument->mOverriddenFingerprintingSettings;
  } else if (opener && shouldInheritFrom(opener->GetDocument())) {
    MOZ_LOG(
        nsContentUtils::ResistFingerprintingLog(), LogLevel::Debug,
        ("Inside RecomputeResistFingerprinting with URI %s and deferring to "
         "opener document %s",
         GetDocumentURI() ? GetDocumentURI()->GetSpecOrDefault().get() : "null",
         opener->GetDocument()->GetDocumentURI()->GetSpecOrDefault().get()));
    mShouldResistFingerprinting =
        opener->GetDocument()->ShouldResistFingerprinting(
            RFPTarget::IsAlwaysEnabledForPrecompute);
    mOverriddenFingerprintingSettings =
        opener->GetDocument()->mOverriddenFingerprintingSettings;
  } else if (nsContentUtils::IsChromeDoc(this)) {
    MOZ_LOG(nsContentUtils::ResistFingerprintingLog(), LogLevel::Debug,
            ("Inside RecomputeResistFingerprinting with a ChromeDoc"));

    mShouldResistFingerprinting = false;
    mOverriddenFingerprintingSettings.reset();
  } else if (mChannel) {
    MOZ_LOG(nsContentUtils::ResistFingerprintingLog(), LogLevel::Debug,
            ("Inside RecomputeResistFingerprinting with URI %s",
             GetDocumentURI() ? GetDocumentURI()->GetSpecOrDefault().get()
                              : "null"));
    mShouldResistFingerprinting = nsContentUtils::ShouldResistFingerprinting(
        mChannel, RFPTarget::IsAlwaysEnabledForPrecompute);

    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
    mOverriddenFingerprintingSettings =
        loadInfo->GetOverriddenFingerprintingSettings();
  } else {
    MOZ_LOG(nsContentUtils::ResistFingerprintingLog(), LogLevel::Debug,
            ("Inside RecomputeResistFingerprinting fallback case."));
    // We still preserve the behavior in the fallback case. But, it means there
    // might be some cases we haven't considered yet and we need to investigate
    // them.
    mShouldResistFingerprinting = nsContentUtils::ShouldResistFingerprinting(
        mChannel, RFPTarget::IsAlwaysEnabledForPrecompute);
    mOverriddenFingerprintingSettings.reset();
  }

  MOZ_LOG(nsContentUtils::ResistFingerprintingLog(), LogLevel::Debug,
          ("Finished RecomputeResistFingerprinting with result %x",
           mShouldResistFingerprinting));

  bool changed = previous != mShouldResistFingerprinting;
  if (changed || aForceRefreshRTPCallerType) {
    if (auto win = nsGlobalWindowInner::Cast(GetInnerWindow())) {
      win->RefreshReduceTimerPrecisionCallerType();
    }
  }
  return changed;
}

bool Document::ShouldResistFingerprinting(RFPTarget aTarget) const {
  return mShouldResistFingerprinting &&
         nsRFPService::IsRFPEnabledFor(this->IsInPrivateBrowsing(), aTarget,
                                       mOverriddenFingerprintingSettings);
}

void Document::RecordCanvasUsage(CanvasUsage& aUsage) {
  // Limit the number of recent canvas extraction uses that are tracked.
  const size_t kTrackedCanvasLimit = 8;
  // Timeout between different canvas extractions.
  const uint64_t kTimeoutUsec = 3000 * 1000;

  uint64_t now = PR_Now();
  if ((mCanvasUsage.Length() > kTrackedCanvasLimit) ||
      ((now - mLastCanvasUsage) > kTimeoutUsec)) {
    mCanvasUsage.ClearAndRetainStorage();
  }

  mCanvasUsage.AppendElement(aUsage);
  mLastCanvasUsage = now;

  nsCString originNoSuffix;
  if (NS_FAILED(NodePrincipal()->GetOriginNoSuffix(originNoSuffix))) {
    return;
  }

  nsRFPService::MaybeReportCanvasFingerprinter(mCanvasUsage, GetChannel(),
                                               originNoSuffix);
}

void Document::RecordFontFingerprinting() {
  nsCString originNoSuffix;
  if (NS_FAILED(NodePrincipal()->GetOriginNoSuffix(originNoSuffix))) {
    return;
  }

  nsRFPService::MaybeReportFontFingerprinter(GetChannel(), originNoSuffix);
}

bool Document::IsInPrivateBrowsing() const { return mIsInPrivateBrowsing; }

WindowContext* Document::GetWindowContextForPageUseCounters() const {
  if (mDisplayDocument) {
    // If we are a resource document, then go through it to find the
    // top-level document.
    return mDisplayDocument->GetWindowContextForPageUseCounters();
  }

  if (mOriginalDocument) {
    // For static clones (print preview documents), contribute page use counters
    // towards the original document.
    return mOriginalDocument->GetWindowContextForPageUseCounters();
  }

  WindowContext* wc = GetTopLevelWindowContext();
  if (!wc || !wc->GetBrowsingContext()->IsContent()) {
    return nullptr;
  }

  return wc;
}

void Document::UpdateIntersections(TimeStamp aNowTime) {
  if (!mIntersectionObservers.IsEmpty()) {
    DOMHighResTimeStamp time = 0;
    if (nsPIDOMWindowInner* win = GetInnerWindow()) {
      if (Performance* perf = win->GetPerformance()) {
        time = perf->TimeStampToDOMHighResForRendering(aNowTime);
      }
    }
    for (DOMIntersectionObserver* observer : mIntersectionObservers) {
      observer->Update(*this, time);
    }
    Dispatch(NewRunnableMethod("Document::NotifyIntersectionObservers", this,
                               &Document::NotifyIntersectionObservers));
  }
}

static void UpdateEffectsOnBrowsingContext(BrowsingContext* aBc,
                                           const IntersectionInput& aInput,
                                           bool aIncludeInactive) {
  Element* el = aBc->GetEmbedderElement();
  if (!el) {
    return;
  }
  auto* rb = RemoteBrowser::GetFrom(el);
  if (!rb) {
    return;
  }
  const bool isInactiveTop = aBc->IsTop() && !aBc->IsActive();
  nsSubDocumentFrame* subDocFrame = do_QueryFrame(el->GetPrimaryFrame());
  rb->UpdateEffects([&] {
    if (isInactiveTop) {
      // We don't use the visible rect of top browsers, so if they're inactive
      // don't waste time computing it. Note that for OOP iframes, it might seem
      // a bit wasteful to bother computing this in background tabs, but:
      //  * We need it so that child frames know if they're visible or not, in
      //    case they're preserving layers for example.
      //  * If we're hidden, our refresh driver is throttled and we don't run
      //    this code very often anyways.
      return EffectsInfo::FullyHidden();
    }
    const IntersectionOutput output = DOMIntersectionObserver::Intersect(
        aInput, *el, DOMIntersectionObserver::BoxToUse::Content);
    if (!output.Intersects()) {
      // XXX do we want to pass the scale and such down even if out of the
      // viewport?
      return EffectsInfo::FullyHidden();
    }
    MOZ_ASSERT(el->GetPrimaryFrame(), "How do we intersect without a frame?");
    if (MOZ_UNLIKELY(NS_WARN_IF(!subDocFrame))) {
      // <frame> not inside a <frameset> might not create a subdoc frame,
      // for example.
      return EffectsInfo::FullyHidden();
    }
    Maybe<nsRect> visibleRect = subDocFrame->GetVisibleRect();
    // If we're paginated, we the display list rect might not be reasonable,
    // because it is the one from the last display item painted. We assume the
    // frame is fully visible, lacking something better.
    if (subDocFrame->PresContext()->IsPaginated()) {
      visibleRect = Some(subDocFrame->GetDestRect());
    }
    if (!visibleRect) {
      // If we have no visible rect (e.g., because we are zero-sized) we
      // still want to provide the intersection rect in order to get the
      // right throttling behavior.
      visibleRect.emplace(*output.mIntersectionRect -
                          output.mTargetRect.TopLeft());
    }
    gfx::MatrixScales rasterScale = subDocFrame->GetRasterScale();
    ParentLayerToScreenScale2D transformToAncestorScale =
        ParentLayerToParentLayerScale(
            subDocFrame->PresShell()->GetCumulativeResolution()) *
        nsLayoutUtils::GetTransformToAncestorScaleCrossProcessForFrameMetrics(
            subDocFrame);
    return EffectsInfo::VisibleWithinRect(visibleRect, rasterScale,
                                          transformToAncestorScale);
  }());
  if (subDocFrame && (!isInactiveTop || aIncludeInactive)) {
    if (nsFrameLoader* fl = subDocFrame->FrameLoader()) {
      fl->UpdatePositionAndSize(subDocFrame);
    }
  }
}

void Document::UpdateRemoteFrameEffects(bool aIncludeInactive) {
  auto margin = DOMIntersectionObserver::LazyLoadingRootMargin();
  const IntersectionInput input = DOMIntersectionObserver::ComputeInput(
      *this, /* aRoot = */ nullptr, &margin);
  if (auto* wc = GetWindowContext()) {
    for (const RefPtr<BrowsingContext>& child : wc->Children()) {
      UpdateEffectsOnBrowsingContext(child, input, aIncludeInactive);
    }
  }
  if (XRE_IsParentProcess()) {
    if (auto* bc = GetBrowsingContext(); bc && bc->IsTop()) {
      bc->Canonical()->CallOnTopDescendants(
          [&](CanonicalBrowsingContext* aDescendant) {
            UpdateEffectsOnBrowsingContext(aDescendant, input,
                                           aIncludeInactive);
            return CallState::Continue;
          },
          CanonicalBrowsingContext::TopDescendantKind::NonNested);
    }
  }
  EnumerateSubDocuments([aIncludeInactive](Document& aDoc) {
    aDoc.UpdateRemoteFrameEffects(aIncludeInactive);
    return CallState::Continue;
  });
}

void Document::SynchronouslyUpdateRemoteBrowserDimensions(
    bool aIncludeInactive) {
  FlushPendingNotifications(FlushType::Layout);
  UpdateRemoteFrameEffects(aIncludeInactive);
}

void Document::NotifyIntersectionObservers() {
  const auto observers = ToTArray<nsTArray<RefPtr<DOMIntersectionObserver>>>(
      mIntersectionObservers);
  for (const auto& observer : observers) {
    // MOZ_KnownLive because the 'observers' array guarantees to keep it
    // alive.
    MOZ_KnownLive(observer)->Notify();
  }
}

DOMIntersectionObserver& Document::EnsureLazyLoadObserver() {
  if (!mLazyLoadObserver) {
    mLazyLoadObserver = DOMIntersectionObserver::CreateLazyLoadObserver(*this);
  }
  return *mLazyLoadObserver;
}

void Document::ObserveForLastRememberedSize(Element& aElement) {
  if (NS_WARN_IF(!IsActive())) {
    return;
  }
  mElementsObservedForLastRememberedSize.Insert(&aElement);
}

void Document::UnobserveForLastRememberedSize(Element& aElement) {
  mElementsObservedForLastRememberedSize.Remove(&aElement);
}

void Document::UpdateLastRememberedSizes() {
  auto shouldRemoveElement = [&](auto* element) {
    if (element->GetComposedDoc() != this) {
      element->RemoveLastRememberedBSize();
      element->RemoveLastRememberedISize();
      return true;
    }
    return !element->GetPrimaryFrame();
  };

  for (auto it = mElementsObservedForLastRememberedSize.begin(),
            end = mElementsObservedForLastRememberedSize.end();
       it != end; ++it) {
    if (shouldRemoveElement(*it)) {
      mElementsObservedForLastRememberedSize.Remove(it);
      continue;
    }
    auto* element = *it;
    MOZ_ASSERT(element->GetComposedDoc() == this);
    nsIFrame* frame = element->GetPrimaryFrame();
    MOZ_ASSERT(frame);

    // As for ResizeObserver, skip nodes hidden `content-visibility`.
    if (frame->IsHiddenByContentVisibilityOnAnyAncestor()) {
      continue;
    }

    MOZ_ASSERT(!frame->IsLineParticipant() || frame->IsReplaced(),
               "Should have unobserved non-replaced inline.");
    MOZ_ASSERT(!frame->HidesContent(),
               "Should have unobserved element skipping its contents.");
    const nsStylePosition* stylePos = frame->StylePosition();
    const WritingMode wm = frame->GetWritingMode();
    bool canUpdateBSize = stylePos->ContainIntrinsicBSize(wm).HasAuto();
    bool canUpdateISize = stylePos->ContainIntrinsicISize(wm).HasAuto();
    MOZ_ASSERT(canUpdateBSize || !element->HasLastRememberedBSize(),
               "Should have removed the last remembered block size.");
    MOZ_ASSERT(canUpdateISize || !element->HasLastRememberedISize(),
               "Should have removed the last remembered inline size.");
    MOZ_ASSERT(canUpdateBSize || canUpdateISize,
               "Should have unobserved if we can't update any size.");

    AutoTArray<LogicalPixelSize, 1> contentSizeList =
        ResizeObserver::CalculateBoxSize(element,
                                         ResizeObserverBoxOptions::Content_box,
                                         /* aForceFragmentHandling */ true);
    MOZ_ASSERT(!contentSizeList.IsEmpty());

    if (canUpdateBSize) {
      float bSize = 0;
      for (const auto& current : contentSizeList) {
        bSize += current.BSize();
      }
      element->SetLastRememberedBSize(bSize);
    }
    if (canUpdateISize) {
      float iSize = 0;
      for (const auto& current : contentSizeList) {
        iSize = std::max(iSize, current.ISize());
      }
      element->SetLastRememberedISize(iSize);
    }
  }
}

void Document::NotifyLayerManagerRecreated() {
  NotifyActivityChanged();
  EnumerateSubDocuments([](Document& aSubDoc) {
    aSubDoc.NotifyLayerManagerRecreated();
    return CallState::Continue;
  });
}

XPathEvaluator* Document::XPathEvaluator() {
  if (!mXPathEvaluator) {
    mXPathEvaluator.reset(new dom::XPathEvaluator(this));
  }
  return mXPathEvaluator.get();
}

already_AddRefed<nsIDocumentEncoder> Document::GetCachedEncoder() {
  return mCachedEncoder.forget();
}

void Document::SetCachedEncoder(already_AddRefed<nsIDocumentEncoder> aEncoder) {
  mCachedEncoder = aEncoder;
}

nsILoadContext* Document::GetLoadContext() const { return mDocumentContainer; }

nsIDocShell* Document::GetDocShell() const { return mDocumentContainer; }

void Document::SetStateObject(nsIStructuredCloneContainer* scContainer) {
  mStateObjectContainer = scContainer;
  mCachedStateObject = JS::UndefinedValue();
  mCachedStateObjectValid = false;
}

already_AddRefed<Element> Document::CreateHTMLElement(nsAtom* aTag) {
  RefPtr<mozilla::dom::NodeInfo> nodeInfo;
  nodeInfo = mNodeInfoManager->GetNodeInfo(aTag, nullptr, kNameSpaceID_XHTML,
                                           ELEMENT_NODE);
  MOZ_ASSERT(nodeInfo, "GetNodeInfo should never fail");

  nsCOMPtr<Element> element;
  DebugOnly<nsresult> rv =
      NS_NewHTMLElement(getter_AddRefs(element), nodeInfo.forget(),
                        mozilla::dom::NOT_FROM_PARSER);

  MOZ_ASSERT(NS_SUCCEEDED(rv), "NS_NewHTMLElement should never fail");
  return element.forget();
}

void AutoWalkBrowsingContextGroup::SuppressBrowsingContext(
    BrowsingContext* aContext) {
  aContext->PreOrderWalk([&](BrowsingContext* aBC) {
    if (nsCOMPtr<nsPIDOMWindowOuter> win = aBC->GetDOMWindow()) {
      if (RefPtr<Document> doc = win->GetExtantDoc()) {
        SuppressDocument(doc);
        mDocuments.AppendElement(doc);
      }
    }
  });
}

void AutoWalkBrowsingContextGroup::SuppressBrowsingContextGroup(
    BrowsingContextGroup* aGroup) {
  for (const auto& bc : aGroup->Toplevels()) {
    SuppressBrowsingContext(bc);
  }
}

nsAutoSyncOperation::nsAutoSyncOperation(Document* aDoc,
                                         SyncOperationBehavior aSyncBehavior)
    : mSyncBehavior(aSyncBehavior) {
  mMicroTaskLevel = 0;
  if (CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get()) {
    mMicroTaskLevel = ccjs->MicroTaskLevel();
    ccjs->SetMicroTaskLevel(0);
    ccjs->EnterSyncOperation();
  }
  if (aDoc) {
    mBrowsingContext = aDoc->GetBrowsingContext();
    if (InputTaskManager::CanSuspendInputEvent()) {
      if (auto* bcg = aDoc->GetDocGroup()->GetBrowsingContextGroup()) {
        SuppressBrowsingContextGroup(bcg);
      }
    } else if (mBrowsingContext) {
      SuppressBrowsingContext(mBrowsingContext->Top());
    }
    if (mBrowsingContext &&
        mSyncBehavior == SyncOperationBehavior::eSuspendInput &&
        InputTaskManager::CanSuspendInputEvent()) {
      mBrowsingContext->Group()->IncInputEventSuspensionLevel();
    }
  }
}

void nsAutoSyncOperation::SuppressDocument(Document* aDoc) {
  if (RefPtr<nsGlobalWindowInner> win =
          nsGlobalWindowInner::Cast(aDoc->GetInnerWindow())) {
    win->GetTimeoutManager()->BeginSyncOperation();
  }
  aDoc->SetIsInSyncOperation(true);
}

void nsAutoSyncOperation::UnsuppressDocument(Document* aDoc) {
  if (RefPtr<nsGlobalWindowInner> win =
          nsGlobalWindowInner::Cast(aDoc->GetInnerWindow())) {
    win->GetTimeoutManager()->EndSyncOperation();
  }
  aDoc->SetIsInSyncOperation(false);
}

nsAutoSyncOperation::~nsAutoSyncOperation() {
  UnsuppressDocuments();
  CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
  if (ccjs) {
    ccjs->SetMicroTaskLevel(mMicroTaskLevel);
    ccjs->LeaveSyncOperation();
  }
  if (mBrowsingContext &&
      mSyncBehavior == SyncOperationBehavior::eSuspendInput &&
      InputTaskManager::CanSuspendInputEvent()) {
    mBrowsingContext->Group()->DecInputEventSuspensionLevel();
  }
}

void Document::SetIsInSyncOperation(bool aSync) {
  if (CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get()) {
    ccjs->UpdateMicroTaskSuppressionGeneration();
  }

  if (aSync) {
    ++mInSyncOperationCount;
  } else {
    --mInSyncOperationCount;
  }
}

gfxUserFontSet* Document::GetUserFontSet() {
  if (!mFontFaceSet) {
    return nullptr;
  }

  return mFontFaceSet->GetImpl();
}

void Document::FlushUserFontSet() {
  if (!mFontFaceSetDirty) {
    return;
  }

  mFontFaceSetDirty = false;

  if (gfxPlatform::GetPlatform()->DownloadableFontsEnabled()) {
    nsTArray<nsFontFaceRuleContainer> rules;
    RefPtr<PresShell> presShell = GetPresShell();
    if (presShell) {
      MOZ_ASSERT(mStyleSetFilled);
      EnsureStyleSet().AppendFontFaceRules(rules);
    }

    if (!mFontFaceSet && !rules.IsEmpty()) {
      mFontFaceSet = FontFaceSet::CreateForDocument(this);
    }

    bool changed = false;
    if (mFontFaceSet) {
      changed = mFontFaceSet->UpdateRules(rules);
    }

    // We need to enqueue a style change reflow (for later) to
    // reflect that we're modifying @font-face rules.  (However,
    // without a reflow, nothing will happen to start any downloads
    // that are needed.)
    if (changed && presShell) {
      if (nsPresContext* presContext = presShell->GetPresContext()) {
        presContext->UserFontSetUpdated();
      }
    }
  }
}

void Document::MarkUserFontSetDirty() {
  if (mFontFaceSetDirty) {
    return;
  }
  mFontFaceSetDirty = true;
  if (PresShell* presShell = GetPresShell()) {
    presShell->EnsureStyleFlush();
  }
}

FontFaceSet* Document::Fonts() {
  if (!mFontFaceSet) {
    mFontFaceSet = FontFaceSet::CreateForDocument(this);
    FlushUserFontSet();
  }
  return mFontFaceSet;
}

void Document::ReportHasScrollLinkedEffect(const TimeStamp& aTimeStamp) {
  MOZ_ASSERT(!aTimeStamp.IsNull());

  if (!mLastScrollLinkedEffectDetectionTime.IsNull() &&
      mLastScrollLinkedEffectDetectionTime >= aTimeStamp) {
    return;
  }

  if (mLastScrollLinkedEffectDetectionTime.IsNull()) {
    // Report to console just once.
    nsContentUtils::ReportToConsole(
        nsIScriptError::warningFlag, "Async Pan/Zoom"_ns, this,
        nsContentUtils::eLAYOUT_PROPERTIES, "ScrollLinkedEffectFound3");
  }

  mLastScrollLinkedEffectDetectionTime = aTimeStamp;
}

bool Document::HasScrollLinkedEffect() const {
  if (nsPresContext* pc = GetPresContext()) {
    return mLastScrollLinkedEffectDetectionTime ==
           pc->RefreshDriver()->MostRecentRefresh();
  }

  return false;
}

void Document::SetSHEntryHasUserInteraction(bool aHasInteraction) {
  if (RefPtr<WindowContext> topWc = GetTopLevelWindowContext()) {
    // Setting has user interction on a discarded browsing context has
    // no effect.
    Unused << topWc->SetSHEntryHasUserInteraction(aHasInteraction);
  }
}

bool Document::GetSHEntryHasUserInteraction() {
  if (RefPtr<WindowContext> topWc = GetTopLevelWindowContext()) {
    return topWc->GetSHEntryHasUserInteraction();
  }
  return false;
}

void Document::SetUserHasInteracted() {
  MOZ_LOG(gUserInteractionPRLog, LogLevel::Debug,
          ("Document %p has been interacted by user.", this));

  // We maybe need to update the user-interaction permission.
  MaybeStoreUserInteractionAsPermission();

  // For purposes of reducing irrelevant session history entries on
  // the back button, we annotate entries with whether they had user
  // interaction. This is gated on its own flag on the WindowContext
  // (instead of mUserHasInteracted) to account for the fact that multiple
  // top-level SH entries can be associated with the same document.
  // Thus, whenever we create a new SH entry for this document,
  // this flag is reset.
  if (!GetSHEntryHasUserInteraction()) {
    nsIDocShell* docShell = GetDocShell();
    if (docShell) {
      nsCOMPtr<nsISHEntry> currentEntry;
      bool oshe;
      nsresult rv =
          docShell->GetCurrentSHEntry(getter_AddRefs(currentEntry), &oshe);
      if (!NS_WARN_IF(NS_FAILED(rv)) && currentEntry) {
        currentEntry->SetHasUserInteraction(true);
      }
    }
    SetSHEntryHasUserInteraction(true);
  }

  if (mUserHasInteracted) {
    return;
  }

  mUserHasInteracted = true;

  if (mChannel) {
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
    loadInfo->SetDocumentHasUserInteracted(true);
  }
  // Tell the parent process about user interaction
  if (auto* wgc = GetWindowGlobalChild()) {
    wgc->SendUpdateDocumentHasUserInteracted(true);
  }

  MaybeAllowStorageForOpenerAfterUserInteraction();
}

BrowsingContext* Document::GetBrowsingContext() const {
  return mDocumentContainer ? mDocumentContainer->GetBrowsingContext()
                            : nullptr;
}

void Document::NotifyUserGestureActivation(
    UserActivation::Modifiers
        aModifiers /* = UserActivation::Modifiers::None() */) {
  // https://html.spec.whatwg.org/multipage/interaction.html#activation-notification
  // 1. "Assert: document is fully active."
  RefPtr<BrowsingContext> currentBC = GetBrowsingContext();
  if (!currentBC) {
    return;
  }

  RefPtr<WindowContext> currentWC = GetWindowContext();
  if (!currentWC) {
    return;
  }

  // 2. "Let windows be « document's relevant global object"
  // Instead of assembling a list, we just call notify for wanted windows as we
  // find them
  currentWC->NotifyUserGestureActivation(aModifiers);

  // 3. "...windows with the active window of each of document's ancestor
  // navigables."
  for (WindowContext* wc = currentWC; wc; wc = wc->GetParentWindowContext()) {
    wc->NotifyUserGestureActivation(aModifiers);
  }

  // 4. "windows with the active window of each of document's descendant
  // navigables, filtered to include only those navigables whose active
  // document's origin is same origin with document's origin"
  currentBC->PreOrderWalk([&](BrowsingContext* bc) {
    WindowContext* wc = bc->GetCurrentWindowContext();
    if (!wc) {
      return;
    }

    // Check same-origin as current document
    WindowGlobalChild* wgc = wc->GetWindowGlobalChild();
    if (!wgc || !wgc->IsSameOriginWith(currentWC)) {
      return;
    }

    wc->NotifyUserGestureActivation(aModifiers);
  });

  // If there has been a user activation, mark the current session history
  // entry as having been interacted with.
  SetSHEntryHasUserInteraction(true);
}

bool Document::HasBeenUserGestureActivated() {
  RefPtr<WindowContext> wc = GetWindowContext();
  return wc && wc->HasBeenUserGestureActivated();
}

bool Document::ConsumeTextDirectiveUserActivation() {
  if (!mChannel) {
    return false;
  }
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  if (!loadInfo) {
    return false;
  }
  const bool textDirectiveUserActivation =
      loadInfo->GetTextDirectiveUserActivation();
  loadInfo->SetTextDirectiveUserActivation(false);
  return textDirectiveUserActivation;
}

DOMHighResTimeStamp Document::LastUserGestureTimeStamp() {
  if (RefPtr<WindowContext> wc = GetWindowContext()) {
    if (nsGlobalWindowInner* innerWindow = wc->GetInnerWindow()) {
      if (Performance* perf = innerWindow->GetPerformance()) {
        return perf->GetDOMTiming()->TimeStampToDOMHighRes(
            wc->GetUserGestureStart());
      }
    }
  }

  NS_WARNING(
      "Unable to calculate DOMHighResTimeStamp for LastUserGestureTimeStamp");
  return 0;
}

void Document::ClearUserGestureActivation() {
  if (RefPtr<BrowsingContext> bc = GetBrowsingContext()) {
    bc = bc->Top();
    bc->PreOrderWalk([&](BrowsingContext* aBC) {
      if (WindowContext* windowContext = aBC->GetCurrentWindowContext()) {
        windowContext->NotifyResetUserGestureActivation();
      }
    });
  }
}

bool Document::HasValidTransientUserGestureActivation() const {
  RefPtr<WindowContext> wc = GetWindowContext();
  return wc && wc->HasValidTransientUserGestureActivation();
}

bool Document::ConsumeTransientUserGestureActivation() {
  RefPtr<WindowContext> wc = GetWindowContext();
  return wc && wc->ConsumeTransientUserGestureActivation();
}

bool Document::GetTransientUserGestureActivationModifiers(
    UserActivation::Modifiers* aModifiers) {
  RefPtr<WindowContext> wc = GetWindowContext();
  return wc && wc->GetTransientUserGestureActivationModifiers(aModifiers);
}

void Document::SetDocTreeHadMedia() {
  RefPtr<WindowContext> topWc = GetTopLevelWindowContext();
  if (topWc && !topWc->IsDiscarded() && !topWc->GetDocTreeHadMedia()) {
    MOZ_ALWAYS_SUCCEEDS(topWc->SetDocTreeHadMedia(true));
  }
}

void Document::MaybeAllowStorageForOpenerAfterUserInteraction() {
  if (!CookieJarSettings()->GetRejectThirdPartyContexts()) {
    return;
  }

  // This will probably change for project fission, but currently this document
  // and the opener are on the same process. In the future, we should make this
  // part async.
  nsPIDOMWindowInner* inner = GetInnerWindow();
  if (NS_WARN_IF(!inner)) {
    return;
  }

  // Don't trigger navigation heuristic for first-party trackers if the pref
  // says so.
  if (StaticPrefs::
          privacy_restrict3rdpartystorage_heuristic_exclude_third_party_trackers() &&
      nsContentUtils::IsFirstPartyTrackingResourceWindow(inner)) {
    return;
  }

  auto* outer = nsGlobalWindowOuter::Cast(inner->GetOuterWindow());
  if (NS_WARN_IF(!outer)) {
    return;
  }

  RefPtr<BrowsingContext> openerBC = outer->GetOpenerBrowsingContext();
  if (!openerBC) {
    // No opener.
    return;
  }

  // We want to ensure the following check works for both fission mode and
  // non-fission mode:
  // "If the opener is not a 3rd party and if this window is not a 3rd party
  // with respect to the opener, we should not continue."
  //
  // In non-fission mode, the opener and the opened window are in the same
  // process, we can use AntiTrackingUtils::IsThirdPartyWindow to do the check.
  // In fission mode, if this window is not a 3rd party with respect to the
  // opener, they must be in the same process, so we can still use
  // IsThirdPartyWindow(openerInner) to continue to check if the opener is a 3rd
  // party.
  if (openerBC->IsInProcess()) {
    nsCOMPtr<nsPIDOMWindowOuter> outerOpener = openerBC->GetDOMWindow();
    if (NS_WARN_IF(!outerOpener)) {
      return;
    }

    nsCOMPtr<nsPIDOMWindowInner> openerInner =
        outerOpener->GetCurrentInnerWindow();
    if (NS_WARN_IF(!openerInner)) {
      return;
    }

    RefPtr<Document> openerDocument = openerInner->GetExtantDoc();
    if (NS_WARN_IF(!openerDocument)) {
      return;
    }

    nsCOMPtr<nsIURI> openerURI = openerDocument->GetDocumentURI();
    if (NS_WARN_IF(!openerURI)) {
      return;
    }

    // If the opener is not a 3rd party and if this window is not
    // a 3rd party with respect to the opener, we should not continue.
    if (!AntiTrackingUtils::IsThirdPartyWindow(inner, openerURI) &&
        !AntiTrackingUtils::IsThirdPartyWindow(openerInner, nullptr)) {
      return;
    }
  }

  // We don't care when the asynchronous work finishes here.
  // Without e10s or fission enabled this is run in the parent process.
  if (XRE_IsParentProcess()) {
    Unused << StorageAccessAPIHelper::AllowAccessForOnParentProcess(
        NodePrincipal(), openerBC,
        ContentBlockingNotifier::eOpenerAfterUserInteraction);
  } else {
    Unused << StorageAccessAPIHelper::AllowAccessForOnChildProcess(
        NodePrincipal(), openerBC,
        ContentBlockingNotifier::eOpenerAfterUserInteraction);
  }
}

namespace {

// Documents can stay alive for days. We don't want to update the permission
// value at any user-interaction, and, using a timer triggered any X seconds
// should be good enough. 'X' is taken from
// privacy.userInteraction.document.interval pref.
//  We also want to store the user-interaction before shutting down, and, for
//  this reason, this class implements nsIAsyncShutdownBlocker interface.
class UserInteractionTimer final : public Runnable,
                                   public nsITimerCallback,
                                   public nsIAsyncShutdownBlocker {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  explicit UserInteractionTimer(Document* aDocument)
      : Runnable("UserInteractionTimer"),
        mPrincipal(aDocument->NodePrincipal()),
        mDocument(aDocument) {
    static int32_t userInteractionTimerId = 0;
    // Blocker names must be unique. Let's create it now because when needed,
    // the document could be already gone.
    mBlockerName.AppendPrintf("UserInteractionTimer %d for document %p",
                              ++userInteractionTimerId, aDocument);

    // For ContentBlockingUserInteraction we care about user-interaction stored
    // only for top-level documents and documents with access to the Storage
    // Access API
    if (aDocument->IsTopLevelContentDocument()) {
      mShouldRecordContentBlockingUserInteraction = true;
    } else {
      bool hasSA;
      nsresult rv = aDocument->HasStorageAccessSync(hasSA);
      mShouldRecordContentBlockingUserInteraction = NS_SUCCEEDED(rv) && hasSA;
    }
  }

  // Runnable interface

  NS_IMETHOD
  Run() override {
    uint32_t interval =
        StaticPrefs::privacy_userInteraction_document_interval();
    if (!interval) {
      return NS_OK;
    }

    RefPtr<UserInteractionTimer> self = this;
    auto raii =
        MakeScopeExit([self] { self->CancelTimerAndStoreUserInteraction(); });

    nsresult rv = NS_NewTimerWithCallback(
        getter_AddRefs(mTimer), this, interval * 1000, nsITimer::TYPE_ONE_SHOT);
    NS_ENSURE_SUCCESS(rv, NS_OK);

    nsCOMPtr<nsIAsyncShutdownClient> phase = GetShutdownPhase();
    NS_ENSURE_TRUE(!!phase, NS_OK);

    rv = phase->AddBlocker(this, NS_LITERAL_STRING_FROM_CSTRING(__FILE__),
                           __LINE__, u"UserInteractionTimer shutdown"_ns);
    NS_ENSURE_SUCCESS(rv, NS_OK);

    raii.release();
    return NS_OK;
  }

  // nsITimerCallback interface

  NS_IMETHOD
  Notify(nsITimer* aTimer) override {
    StoreUserInteraction();
    return NS_OK;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  using nsINamed::GetName;
#endif

  // nsIAsyncShutdownBlocker interface

  NS_IMETHOD
  GetName(nsAString& aName) override {
    aName = mBlockerName;
    return NS_OK;
  }

  NS_IMETHOD
  BlockShutdown(nsIAsyncShutdownClient* aClient) override {
    CancelTimerAndStoreUserInteraction();
    return NS_OK;
  }

  NS_IMETHOD
  GetState(nsIPropertyBag**) override { return NS_OK; }

 private:
  ~UserInteractionTimer() = default;

  void StoreUserInteraction() {
    // Remove the shutting down blocker
    nsCOMPtr<nsIAsyncShutdownClient> phase = GetShutdownPhase();
    if (phase) {
      phase->RemoveBlocker(this);
    }

    // If the document is not gone, let's reset its timer flag.
    nsCOMPtr<Document> document(mDocument);
    if (document) {
      if (mShouldRecordContentBlockingUserInteraction) {
        ContentBlockingUserInteraction::Observe(mPrincipal);
      }
      Unused << BounceTrackingProtection::RecordUserActivation(
          mDocument->GetWindowContext());
      document->ResetUserInteractionTimer();
    }
  }

  void CancelTimerAndStoreUserInteraction() {
    if (mTimer) {
      mTimer->Cancel();
      mTimer = nullptr;
    }

    StoreUserInteraction();
  }

  static already_AddRefed<nsIAsyncShutdownClient> GetShutdownPhase() {
    nsCOMPtr<nsIAsyncShutdownService> svc = services::GetAsyncShutdownService();
    NS_ENSURE_TRUE(!!svc, nullptr);

    nsCOMPtr<nsIAsyncShutdownClient> phase;
    nsresult rv = svc->GetXpcomWillShutdown(getter_AddRefs(phase));
    NS_ENSURE_SUCCESS(rv, nullptr);

    return phase.forget();
  }

  nsCOMPtr<nsIPrincipal> mPrincipal;
  WeakPtr<Document> mDocument;
  bool mShouldRecordContentBlockingUserInteraction = false;

  nsCOMPtr<nsITimer> mTimer;

  nsString mBlockerName;
};

NS_IMPL_ISUPPORTS_INHERITED(UserInteractionTimer, Runnable, nsITimerCallback,
                            nsIAsyncShutdownBlocker)

}  // namespace

void Document::MaybeStoreUserInteractionAsPermission() {
  if (!mUserHasInteracted) {
    // First interaction, let's store this info now.
    Unused << BounceTrackingProtection::RecordUserActivation(
        GetWindowContext());

    // For ContentBlockingUserInteraction we care about user-interaction stored
    // only for top-level documents and documents with access to the Storage
    // Access API
    if (!IsTopLevelContentDocument()) {
      bool hasSA;
      nsresult rv = HasStorageAccessSync(hasSA);
      if (NS_FAILED(rv) || !hasSA) {
        return;
      }
    }
    ContentBlockingUserInteraction::Observe(NodePrincipal());
    return;
  }

  if (mHasUserInteractionTimerScheduled) {
    return;
  }

  nsCOMPtr<nsIRunnable> task = new UserInteractionTimer(this);
  nsresult rv = NS_DispatchToCurrentThreadQueue(task.forget(), 2500,
                                                EventQueuePriority::Idle);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  // This value will be reset by the timer.
  mHasUserInteractionTimerScheduled = true;
}

void Document::ResetUserInteractionTimer() {
  mHasUserInteractionTimerScheduled = false;
}

bool Document::IsExtensionPage() const {
  return BasePrincipal::Cast(NodePrincipal())->AddonPolicy();
}

PermissionDelegateHandler* Document::GetPermissionDelegateHandler() {
  if (!mPermissionDelegateHandler) {
    mPermissionDelegateHandler = MakeAndAddRef<PermissionDelegateHandler>(this);
  }

  if (!mPermissionDelegateHandler->Initialize()) {
    mPermissionDelegateHandler = nullptr;
  }

  return mPermissionDelegateHandler;
}

void Document::ScheduleResizeObserversNotification() {
  MaybeScheduleRenderingPhases({RenderingPhase::Layout});
}

static void FlushLayoutForWholeBrowsingContextTree(Document& aDoc,
                                                   const ChangesToFlush& aCtf) {
  BrowsingContext* bc = aDoc.GetBrowsingContext();
  if (bc && bc->GetExtantDocument() == &aDoc) {
    RefPtr<BrowsingContext> top = bc->Top();
    top->PreOrderWalk([aCtf](BrowsingContext* aCur) {
      if (Document* doc = aCur->GetExtantDocument()) {
        doc->FlushPendingNotifications(aCtf);
      }
    });
  } else {
    // If there is no browsing context, or we're not the current document of the
    // browsing context, then we just flush this document itself.
    aDoc.FlushPendingNotifications(aCtf);
  }
}

// https://html.spec.whatwg.org/#update-the-rendering steps 16 and 17
void Document::DetermineProximityToViewportAndNotifyResizeObservers() {
  RefPtr ps = GetPresShell();
  if (!ps) {
    return;
  }

  // Try to do an interruptible reflow if it wouldn't be observable by the page
  // in any obvious way.
  const bool interruptible = !ps->HasContentVisibilityAutoFrames() &&
                             !HasResizeObservers() &&
                             !HasElementsWithLastRememberedSize();
  ps->ResetWasLastReflowInterrupted();

  // https://github.com/whatwg/html/issues/11210 for this not being in the spec.
  ps->UpdateRelevancyOfContentVisibilityAutoFrames();

  // 1. Let resizeObserverDepth be 0.
  uint32_t resizeObserverDepth = 0;
  bool initialResetOfScrolledIntoViewFlagsDone = false;
  const ChangesToFlush ctf(
      interruptible ? FlushType::InterruptibleLayout : FlushType::Layout,
      /* aFlushAnimations = */ false, /* aUpdateRelevancy = */ false);

  // 2. While true:
  while (true) {
    // 2.1. Recalculate styles and update layout for doc.
    if (interruptible) {
      ps->FlushPendingNotifications(ctf);
    } else {
      // The ResizeObserver callbacks functions may do changes in its
      // sub-documents or ancestors, so flushing layout for the whole browsing
      // context tree makes sure we don't miss anyone.
      //
      // TODO(emilio): It seems Document::FlushPendingNotifications should be
      // able to take care of ancestors... Can we do this less often?
      FlushLayoutForWholeBrowsingContextTree(*this, ctf);
    }

    // Last remembered sizes are recorded "at the time that ResizeObserver
    // events are determined and delivered".
    // https://drafts.csswg.org/css-sizing-4/#last-remembered
    //
    // We do it right after layout to make sure sizes are up-to-date. If we do
    // it after determining the proximities to viewport of
    // 'content-visibility: auto' nodes, and if one of such node ever becomes
    // relevant to the user, then we would be incorrectly recording the size
    // of its rendering when it was skipping its content.
    //
    // https://github.com/whatwg/html/issues/11210 for the timing of this.
    UpdateLastRememberedSizes();

    // 2.2. Let hadInitialVisibleContentVisibilityDetermination be false.
    //      (this is part of "result").
    // 2.3. For each element element with 'auto' used value of
    //      'content-visibility' [...] Determine proximity to the viewport for
    //      element.
    auto result = ps->DetermineProximityToViewport();
    if (result.mHadInitialDetermination) {
      // 2.4. If hadInitialVisibleContentVisibilityDetermination is true, then
      //      continue.
      continue;
    }
    if (result.mAnyScrollIntoViewFlag) {
      // Not defined in the spec: It's possible that some elements with
      // content-visibility: auto were forced to be visible in order to
      // perform scrollIntoView() so clear their flags now and restart the
      // loop. See https://github.com/w3c/csswg-drafts/issues/9337
      ps->ClearTemporarilyVisibleForScrolledIntoViewDescendantFlags();
      ps->ScheduleContentRelevancyUpdate(ContentRelevancyReason::Visible);
      if (!initialResetOfScrolledIntoViewFlagsDone) {
        initialResetOfScrolledIntoViewFlagsDone = true;
        continue;
      }
    }

    // 2.5. Gather active resize observations at depth resizeObserverDepth for
    // doc.
    GatherAllActiveResizeObservations(resizeObserverDepth);
    // 2.6. If doc has active resize observations: [..] steps below
    if (!HasAnyActiveResizeObservations()) {
      // 2.7. Otherwise, break.
      break;
    }
    // 2.6.1. Set resizeObserverDepth to the result of broadcasting active
    // resize observations given doc.
    DebugOnly<uint32_t> oldResizeObserverDepth = resizeObserverDepth;
    resizeObserverDepth = BroadcastAllActiveResizeObservations();
    NS_ASSERTION(oldResizeObserverDepth < resizeObserverDepth,
                 "resizeObserverDepth should be getting strictly deeper");
    // 2.6.2. Continue.
  }

  if (HasAnySkippedResizeObservations()) {
    if (nsCOMPtr<nsPIDOMWindowInner> window = GetInnerWindow()) {
      // Per spec, we deliver an error if the document has any skipped
      // observations. Also, we re-register via ScheduleNotification().
      RootedDictionary<ErrorEventInit> init(RootingCx());
      init.mMessage.AssignLiteral(
          "ResizeObserver loop completed with undelivered notifications.");
      init.mBubbles = false;
      init.mCancelable = false;

      nsEventStatus status = nsEventStatus_eIgnore;
      nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(window);
      MOZ_ASSERT(sgo);
      if (NS_WARN_IF(sgo->HandleScriptError(init, &status))) {
        status = nsEventStatus_eIgnore;
      }
    } else {
      // We don't fire error events at any global for non-window JS on the main
      // thread.
    }

    // We need to deliver pending notifications in next cycle.
    ScheduleResizeObserversNotification();
  }

  // Step 17: For each doc of docs, if the focused area of doc is not a
  // focusable area, then run the focusing steps for doc's viewport, and set
  // doc's relevant global object's navigation API's focus changed during
  // ongoing navigation to false.
  //
  // We do it here rather than a separate walk over the docs for convenience,
  // and because I don't think there's a strong reason for it to be a separate
  // walk altogether, see https://github.com/whatwg/html/issues/11211
  const bool fixedUpFocus = ps->FixUpFocus();
  if (fixedUpFocus) {
    FlushPendingNotifications(ctf);
  }

  if (NS_WARN_IF(ps->NeedStyleFlush()) || NS_WARN_IF(ps->NeedLayoutFlush()) ||
      NS_WARN_IF(fixedUpFocus && ps->NeedsFocusFixUp())) {
    ps->EnsureLayoutFlush();
  }

  // Inform the FontFaceSet that we ticked, so that it can resolve its ready
  // promise if it needs to. See PresShell::MightHavePendingFontLoads.
  ps->NotifyFontFaceSetOnRefresh();
}

void Document::GatherAllActiveResizeObservations(uint32_t aDepth) {
  for (ResizeObserver* observer : mResizeObservers) {
    observer->GatherActiveObservations(aDepth);
  }
}

uint32_t Document::BroadcastAllActiveResizeObservations() {
  uint32_t shallowestTargetDepth = std::numeric_limits<uint32_t>::max();

  // Copy the observers as this invokes the callbacks and could register and
  // unregister observers at will.
  const auto observers =
      ToTArray<nsTArray<RefPtr<ResizeObserver>>>(mResizeObservers);
  for (const auto& observer : observers) {
    // MOZ_KnownLive because 'observers' is guaranteed to keep it
    // alive.
    //
    // This can go away once
    // https://bugzilla.mozilla.org/show_bug.cgi?id=1620312 is fixed.
    uint32_t targetDepth =
        MOZ_KnownLive(observer)->BroadcastActiveObservations();
    if (targetDepth < shallowestTargetDepth) {
      shallowestTargetDepth = targetDepth;
    }
  }

  return shallowestTargetDepth;
}

bool Document::HasAnySkippedResizeObservations() const {
  for (const auto& observer : mResizeObservers) {
    if (observer->HasSkippedObservations()) {
      return true;
    }
  }
  return false;
}

bool Document::HasAnyActiveResizeObservations() const {
  for (const auto& observer : mResizeObservers) {
    if (observer->HasActiveObservations()) {
      return true;
    }
  }
  return false;
}

void Document::ClearStaleServoData() {
  DocumentStyleRootIterator iter(this);
  while (Element* root = iter.GetNextStyleRoot()) {
    RestyleManager::ClearServoDataFromSubtree(root);
  }
}

// https://drafts.csswg.org/css-view-transitions-1/#dom-document-startviewtransition
already_AddRefed<ViewTransition> Document::StartViewTransition(
    const Optional<OwningNonNull<ViewTransitionUpdateCallback>>& aCallback) {
  // Steps 1-3
  RefPtr transition = new ViewTransition(
      *this, aCallback.WasPassed() ? &aCallback.Value() : nullptr);
  if (Hidden()) {
    // Step 4:
    //
    // If document's visibility state is "hidden", then skip transition with an
    // "InvalidStateError" DOMException, and return transition.
    transition->SkipTransition(SkipTransitionReason::DocumentHidden);
    return transition.forget();
  }
  if (mActiveViewTransition) {
    // Step 5:
    // If document's active view transition is not null, then skip that view
    // transition with an "AbortError" DOMException in this's relevant Realm.
    mActiveViewTransition->SkipTransition(
        SkipTransitionReason::ClobberedActiveTransition);
  }
  // Step 6: Set document's active view transition to transition.
  mActiveViewTransition = transition;

  EnsureViewTransitionOperationsHappen();

  // Step 7: return transition
  return transition.forget();
}

void Document::ClearActiveViewTransition() { mActiveViewTransition = nullptr; }

void Document::SetRenderingSuppressedForViewTransitions(bool aValue) {
  if (mRenderingSuppressedForViewTransitions == aValue) {
    return;
  }
  mRenderingSuppressedForViewTransitions = aValue;
  if (aValue) {
    return;
  }
  // We might have missed virtually all rendering steps, ensure we run them.
  MaybeScheduleRendering();
}

void Document::PerformPendingViewTransitionOperations() {
  if (mActiveViewTransition && !RenderingSuppressedForViewTransitions()) {
    RefPtr activeVT = mActiveViewTransition;
    activeVT->PerformPendingOperations();
  }
  EnumerateSubDocuments([](Document& aDoc) MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION {
    // Note: EnumerateSubDocuments keeps aDoc alive in a local array, so it's
    // fine to use MOZ_KnownLive here.
    MOZ_KnownLive(aDoc).PerformPendingViewTransitionOperations();
    return CallState::Continue;
  });
}

void Document::EnsureViewTransitionOperationsHappen() {
  MaybeScheduleRenderingPhases({RenderingPhase::ViewTransitionOperations});
}

Selection* Document::GetSelection(ErrorResult& aRv) {
  nsCOMPtr<nsPIDOMWindowInner> window = GetInnerWindow();
  if (!window) {
    return nullptr;
  }

  if (!window->IsCurrentInnerWindow()) {
    return nullptr;
  }

  return nsGlobalWindowInner::Cast(window)->GetSelection(aRv);
}

void Document::MakeBrowsingContextNonSynthetic() {
  if (BrowsingContext* bc = GetBrowsingContext()) {
    if (bc->GetIsSyntheticDocumentContainer()) {
      Unused << bc->SetIsSyntheticDocumentContainer(false);
    }
  }
}

nsresult Document::HasStorageAccessSync(bool& aHasStorageAccess) {
  // Step 1: check if cookie permissions are available or denied to this
  // document's principal
  nsCOMPtr<nsPIDOMWindowInner> inner = GetInnerWindow();
  if (!inner) {
    aHasStorageAccess = false;
    return NS_OK;
  }
  Maybe<bool> resultBecauseCookiesApproved =
      StorageAccessAPIHelper::CheckCookiesPermittedDecidesStorageAccessAPI(
          CookieJarSettings(), NodePrincipal());
  if (resultBecauseCookiesApproved.isSome()) {
    if (resultBecauseCookiesApproved.value()) {
      aHasStorageAccess = true;
      return NS_OK;
    } else {
      aHasStorageAccess = false;
      return NS_OK;
    }
  }

  // Step 2: Check if the browser settings determine whether or not this
  // document has access to its unpartitioned cookies.
  bool isThirdPartyDocument = AntiTrackingUtils::IsThirdPartyDocument(this);
  bool isOnThirdPartySkipList = false;
  if (mChannel) {
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
    isOnThirdPartySkipList = loadInfo->GetStoragePermission() ==
                             nsILoadInfo::StoragePermissionAllowListed;
  }
  bool isThirdPartyTracker =
      nsContentUtils::IsThirdPartyTrackingResourceWindow(inner);
  Maybe<bool> resultBecauseBrowserSettings =
      StorageAccessAPIHelper::CheckBrowserSettingsDecidesStorageAccessAPI(
          CookieJarSettings(), isThirdPartyDocument, isOnThirdPartySkipList,
          isThirdPartyTracker);
  if (resultBecauseBrowserSettings.isSome()) {
    if (resultBecauseBrowserSettings.value()) {
      aHasStorageAccess = true;
      return NS_OK;
    } else {
      aHasStorageAccess = false;
      return NS_OK;
    }
  }

  // Step 3: Check if the location of this call (embedded, top level, same-site)
  // determines if cookies are permitted or not.
  Maybe<bool> resultBecauseCallContext =
      StorageAccessAPIHelper::CheckCallingContextDecidesStorageAccessAPI(this,
                                                                         false);
  if (resultBecauseCallContext.isSome()) {
    if (resultBecauseCallContext.value()) {
      aHasStorageAccess = true;
      return NS_OK;
    } else {
      aHasStorageAccess = false;
      return NS_OK;
    }
  }

  // Step 4: Check if the permissions for this document determine if if has
  // access or is denied cookies.
  Maybe<bool> resultBecausePreviousPermission =
      StorageAccessAPIHelper::CheckExistingPermissionDecidesStorageAccessAPI(
          this, false);
  if (resultBecausePreviousPermission.isSome()) {
    if (resultBecausePreviousPermission.value()) {
      aHasStorageAccess = true;
      return NS_OK;
    } else {
      aHasStorageAccess = false;
      return NS_OK;
    }
  }
  // If you get here, we default to not giving you permission.
  aHasStorageAccess = false;
  return NS_OK;
}

already_AddRefed<mozilla::dom::Promise> Document::HasStorageAccess(
    mozilla::ErrorResult& aRv) {
  nsIGlobalObject* global = GetScopeObject();
  if (!global) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }

  RefPtr<Promise> promise =
      Promise::Create(global, aRv, Promise::ePropagateUserInteraction);
  if (aRv.Failed()) {
    return nullptr;
  }

  if (!IsCurrentActiveDocument()) {
    promise->MaybeRejectWithInvalidStateError(
        "hasStorageAccess requires an active document");
    return promise.forget();
  }

  bool hasStorageAccess;
  nsresult rv = HasStorageAccessSync(hasStorageAccess);
  if (NS_FAILED(rv)) {
    promise->MaybeRejectWithUndefined();
  } else {
    promise->MaybeResolve(hasStorageAccess);
  }

  return promise.forget();
}

RefPtr<Document::GetContentBlockingEventsPromise>
Document::GetContentBlockingEvents() {
  RefPtr<WindowGlobalChild> wgc = GetWindowGlobalChild();
  if (!wgc) {
    return nullptr;
  }

  return wgc->SendGetContentBlockingEvents()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [](const WindowGlobalChild::GetContentBlockingEventsPromise::
             ResolveOrRejectValue& aValue) {
        if (aValue.IsResolve()) {
          return Document::GetContentBlockingEventsPromise::CreateAndResolve(
              aValue.ResolveValue(), __func__);
        }

        return Document::GetContentBlockingEventsPromise::CreateAndReject(
            false, __func__);
      });
}

StorageAccessAPIHelper::PerformPermissionGrant
Document::CreatePermissionGrantPromise(
    nsPIDOMWindowInner* aInnerWindow, nsIPrincipal* aPrincipal,
    bool aHasUserInteraction, bool aRequireUserInteraction,
    const Maybe<nsCString>& aTopLevelBaseDomain, bool aFrameOnly) {
  MOZ_ASSERT(aInnerWindow);
  MOZ_ASSERT(aPrincipal);
  RefPtr<Document> self(this);
  RefPtr<nsPIDOMWindowInner> inner(aInnerWindow);
  RefPtr<nsIPrincipal> principal(aPrincipal);

  return [inner, self, principal, aHasUserInteraction, aRequireUserInteraction,
          aTopLevelBaseDomain, aFrameOnly]() {
    RefPtr<StorageAccessAPIHelper::StorageAccessPermissionGrantPromise::Private>
        p = new StorageAccessAPIHelper::StorageAccessPermissionGrantPromise::
            Private(__func__);

    // Before we prompt, see if we are same-site
    if (aFrameOnly) {
      nsIChannel* channel = self->GetChannel();
      if (channel) {
        nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
        if (!loadInfo->GetIsThirdPartyContextToTopWindow()) {
          p->Resolve(StorageAccessAPIHelper::eAllow, __func__);
          return p;
        }
      }
    }

    RefPtr<PWindowGlobalChild::GetStorageAccessPermissionPromise> promise;
    // Test the permission
    MOZ_ASSERT(XRE_IsContentProcess());

    WindowGlobalChild* wgc = inner->GetWindowGlobalChild();
    MOZ_ASSERT(wgc);

    promise = wgc->SendGetStorageAccessPermission(true);
    MOZ_ASSERT(promise);
    promise->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self, p, inner, principal, aHasUserInteraction,
         aRequireUserInteraction, aTopLevelBaseDomain,
         aFrameOnly](uint32_t aAction) {
          if (aAction == nsIPermissionManager::ALLOW_ACTION) {
            p->Resolve(StorageAccessAPIHelper::eAllow, __func__);
            return;
          }
          if (aAction == nsIPermissionManager::DENY_ACTION) {
            p->Reject(false, __func__);
            return;
          }

          // We require user activation before conducting a permission request
          // See
          // https://privacycg.github.io/storage-access/#dom-document-requeststorageaccess
          // where we "If has transient activation is false: ..." immediately
          // before we "Let permissionState be the result of requesting
          // permission to use "storage-access"" from in parallel.
          if (!aHasUserInteraction && aRequireUserInteraction) {
            // Report an error to the console for this case
            nsContentUtils::ReportToConsole(
                nsIScriptError::errorFlag,
                nsLiteralCString("requestStorageAccess"), self,
                nsContentUtils::eDOM_PROPERTIES,
                "RequestStorageAccessUserGesture");
            p->Reject(false, __func__);
            return;
          }

          // Create the user prompt
          RefPtr<StorageAccessPermissionRequest> sapr =
              StorageAccessPermissionRequest::Create(
                  inner, principal, aTopLevelBaseDomain, aFrameOnly,
                  // Allow
                  [p] {
                    glean::dom::storage_access_api_ui
                        .EnumGet(glean::dom::StorageAccessApiUiLabel::eAllow)
                        .Add();
                    p->Resolve(StorageAccessAPIHelper::eAllow, __func__);
                  },
                  // Block
                  [p] {
                    glean::dom::storage_access_api_ui
                        .EnumGet(glean::dom::StorageAccessApiUiLabel::eDeny)
                        .Add();
                    p->Reject(false, __func__);
                  });

          using PromptResult = ContentPermissionRequestBase::PromptResult;
          PromptResult pr = sapr->CheckPromptPrefs();

          if (pr == PromptResult::Pending) {
            // We're about to show a prompt, record the request attempt
            glean::dom::storage_access_api_ui
                .EnumGet(glean::dom::StorageAccessApiUiLabel::eRequest)
                .Add();
          }

          bool isThirdPartyTracker =
              nsContentUtils::IsThirdPartyTrackingResourceWindow(inner);

          // Try to auto-grant the storage access so the user doesn't see the
          // prompt.
          self->AutomaticStorageAccessPermissionCanBeGranted(
                  aHasUserInteraction, isThirdPartyTracker)
              ->Then(
                  GetCurrentSerialEventTarget(), __func__,
                  // If the autogrant check didn't fail, call this function
                  [p, pr, sapr,
                   inner](const Document::
                              AutomaticStorageAccessPermissionGrantPromise::
                                  ResolveOrRejectValue& aValue) -> void {
                    // Make a copy because we can't modified copy-captured
                    // lambda variables.
                    PromptResult pr2 = pr;

                    // If the user didn't already click "allow" and we can
                    // autogrant, do that!
                    bool storageAccessCanBeGrantedAutomatically =
                        aValue.IsResolve() && aValue.ResolveValue();
                    bool autoGrant = false;
                    if (pr2 == PromptResult::Pending &&
                        storageAccessCanBeGrantedAutomatically) {
                      pr2 = PromptResult::Granted;
                      autoGrant = true;

                      glean::dom::storage_access_api_ui
                          .EnumGet(glean::dom::StorageAccessApiUiLabel::
                                       eAllowautomatically)
                          .Add();
                    }

                    // If we can complete the permission request, do so.
                    if (pr2 != PromptResult::Pending) {
                      MOZ_ASSERT_IF(pr2 != PromptResult::Granted,
                                    pr2 == PromptResult::Denied);
                      if (pr2 == PromptResult::Granted) {
                        StorageAccessAPIHelper::StorageAccessPromptChoices
                            choice = StorageAccessAPIHelper::eAllow;
                        if (autoGrant) {
                          choice = StorageAccessAPIHelper::eAllowAutoGrant;
                        }
                        if (!autoGrant) {
                          p->Resolve(choice, __func__);
                        } else {
                          // We capture sapr here to prevent it from destructing
                          // before the callbacks complete.
                          sapr->MaybeDelayAutomaticGrants()->Then(
                              GetCurrentSerialEventTarget(), __func__,
                              [p, sapr, choice] {
                                p->Resolve(choice, __func__);
                              },
                              [p, sapr] { p->Reject(false, __func__); });
                        }
                        return;
                      }
                      p->Reject(false, __func__);
                      return;
                    }

                    // If we get here, the auto-decision failed and we need to
                    // wait for the user prompt to complete.
                    sapr->RequestDelayedTask(
                        GetMainThreadSerialEventTarget(),
                        ContentPermissionRequestBase::DelayedTaskType::Request);
                  });
        },
        [p](mozilla::ipc::ResponseRejectReason aError) {
          p->Reject(false, __func__);
          return p;
        });

    return p;
  };
}

already_AddRefed<mozilla::dom::Promise> Document::RequestStorageAccess(
    mozilla::ErrorResult& aRv) {
  nsIGlobalObject* global = GetScopeObject();
  if (!global) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  if (!IsCurrentActiveDocument()) {
    promise->MaybeRejectWithInvalidStateError(
        "requestStorageAccess requires an active document");
    return promise.forget();
  }

  // Get a pointer to the inner window- We need this for convenience sake
  RefPtr<nsPIDOMWindowInner> inner = GetInnerWindow();
  if (!inner) {
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }

  // Step 1: Check if the principal calling this has a permission that lets
  // them use cookies or forbids them from using cookies.
  // This is outside of the spec of the StorageAccess API, but makes the return
  // values to have proper semantics.
  Maybe<bool> resultBecauseCookiesApproved =
      StorageAccessAPIHelper::CheckCookiesPermittedDecidesStorageAccessAPI(
          CookieJarSettings(), NodePrincipal());
  if (resultBecauseCookiesApproved.isSome()) {
    if (resultBecauseCookiesApproved.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    } else {
      ConsumeTransientUserGestureActivation();
      promise->MaybeRejectWithNotAllowedError(
          "requestStorageAccess not allowed"_ns);
      return promise.forget();
    }
  }

  // Step 2: Check if the browser settings always allow or deny cookies.
  // We should always return a resolved promise if the cookieBehavior is ACCEPT.
  // This is outside of the spec of the StorageAccess API, but makes the return
  // values to have proper semantics.
  bool isThirdPartyDocument = AntiTrackingUtils::IsThirdPartyDocument(this);
  bool isOnThirdPartySkipList = false;
  if (mChannel) {
    nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
    isOnThirdPartySkipList = loadInfo->GetStoragePermission() ==
                             nsILoadInfo::StoragePermissionAllowListed;
  }
  bool isThirdPartyTracker =
      nsContentUtils::IsThirdPartyTrackingResourceWindow(inner);
  Maybe<bool> resultBecauseBrowserSettings =
      StorageAccessAPIHelper::CheckBrowserSettingsDecidesStorageAccessAPI(
          CookieJarSettings(), isThirdPartyDocument, isOnThirdPartySkipList,
          isThirdPartyTracker);
  if (resultBecauseBrowserSettings.isSome()) {
    if (resultBecauseBrowserSettings.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    } else {
      ConsumeTransientUserGestureActivation();
      promise->MaybeRejectWithNotAllowedError(
          "requestStorageAccess not allowed"_ns);
      return promise.forget();
    }
  }

  // Step 3: Check if the Document calling requestStorageAccess has anything to
  // gain from storage access. It should be embedded, non-null, etc.
  Maybe<bool> resultBecauseCallContext =
      StorageAccessAPIHelper::CheckCallingContextDecidesStorageAccessAPI(this,
                                                                         true);
  if (resultBecauseCallContext.isSome()) {
    if (resultBecauseCallContext.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    } else {
      ConsumeTransientUserGestureActivation();
      promise->MaybeRejectWithNotAllowedError(
          "requestStorageAccess not allowed"_ns);
      return promise.forget();
    }
  }

  // Step 4: Check if we already allowed or denied storage access for this
  // document's storage key.
  Maybe<bool> resultBecausePreviousPermission =
      StorageAccessAPIHelper::CheckExistingPermissionDecidesStorageAccessAPI(
          this, true);
  if (resultBecausePreviousPermission.isSome()) {
    if (resultBecausePreviousPermission.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    } else {
      ConsumeTransientUserGestureActivation();
      promise->MaybeRejectWithNotAllowedError(
          "requestStorageAccess not allowed"_ns);
      return promise.forget();
    }
  }

  // Get pointers to some objects that will be used in the async portion
  RefPtr<BrowsingContext> bc = GetBrowsingContext();
  RefPtr<nsGlobalWindowOuter> outer =
      nsGlobalWindowOuter::Cast(inner->GetOuterWindow());
  if (!outer) {
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }
  RefPtr<Document> self(this);

  // Step 5. Start an async call to request storage access. This will either
  // perform an automatic decision or notify the user, then perform some follow
  // on work changing state to reflect the result of the API. If it resolves,
  // the request was granted. If it rejects it was denied.
  StorageAccessAPIHelper::RequestStorageAccessAsyncHelper(
      this, inner, bc, NodePrincipal(),
      self->HasValidTransientUserGestureActivation(), true, true,
      ContentBlockingNotifier::eStorageAccessAPI, true)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [inner] { return inner->SaveStorageAccessPermissionGranted(); },
          [] {
            return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise] { promise->MaybeResolveWithUndefined(); },
          [promise, self] {
            self->ConsumeTransientUserGestureActivation();
            promise->MaybeRejectWithNotAllowedError(
                "requestStorageAccess not allowed"_ns);
          });

  return promise.forget();
}

already_AddRefed<mozilla::dom::Promise> Document::RequestStorageAccessForOrigin(
    const nsAString& aThirdPartyOrigin, const bool aRequireUserActivation,
    mozilla::ErrorResult& aRv) {
  nsIGlobalObject* global = GetScopeObject();
  if (!global) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Step 0: Check that we have user activation before proceeding to prevent
  // rapid calls to the API to leak information.
  if (aRequireUserActivation && !HasValidTransientUserGestureActivation()) {
    // Report an error to the console for this case
    nsContentUtils::ReportToConsole(nsIScriptError::errorFlag,
                                    nsLiteralCString("requestStorageAccess"),
                                    this, nsContentUtils::eDOM_PROPERTIES,
                                    "RequestStorageAccessUserGesture");
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }

  // Step 1: Check if the provided URI is different-site to this Document
  nsCOMPtr<nsIURI> thirdPartyURI;
  nsresult rv = NS_NewURI(getter_AddRefs(thirdPartyURI), aThirdPartyOrigin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return nullptr;
  }
  bool isThirdPartyDocument;
  rv = NodePrincipal()->IsThirdPartyURI(thirdPartyURI, &isThirdPartyDocument);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return nullptr;
  }
  Maybe<bool> resultBecauseBrowserSettings =
      StorageAccessAPIHelper::CheckBrowserSettingsDecidesStorageAccessAPI(
          CookieJarSettings(), isThirdPartyDocument, false, true);
  if (resultBecauseBrowserSettings.isSome()) {
    if (resultBecauseBrowserSettings.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }

  // Step 2: Check that this Document is same-site to the top, and check that
  // we have user activation if we require it.
  Maybe<bool> resultBecauseCallContext = StorageAccessAPIHelper::
      CheckSameSiteCallingContextDecidesStorageAccessAPI(
          this, aRequireUserActivation);
  if (resultBecauseCallContext.isSome()) {
    if (resultBecauseCallContext.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }

  // Step 3: Get some useful variables that can be captured by the lambda for
  // the asynchronous portion
  RefPtr<BrowsingContext> bc = GetBrowsingContext();
  nsCOMPtr<nsPIDOMWindowInner> inner = GetInnerWindow();
  if (!inner) {
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }
  RefPtr<nsGlobalWindowOuter> outer =
      nsGlobalWindowOuter::Cast(inner->GetOuterWindow());
  if (!outer) {
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }
  nsCOMPtr<nsIPrincipal> principal = BasePrincipal::CreateContentPrincipal(
      thirdPartyURI, NodePrincipal()->OriginAttributesRef());
  if (!principal) {
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithNotAllowedError(
        "requestStorageAccess not allowed"_ns);
    return promise.forget();
  }

  RefPtr<Document> self(this);
  bool hasUserActivation = HasValidTransientUserGestureActivation();

  // Consume user activation before entering the async part of this method.
  // This prevents usage of other transient activation-gated APIs.
  ConsumeTransientUserGestureActivation();

  ContentChild* cc = ContentChild::GetSingleton();
  if (!cc) {
    // TODO(bug 1778561): Make this work in non-content processes.
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Step 4a: Start the async part of this function. Check the cookie
  // permission, but this can't be done in this process. We needs the cookie
  // permission of the URL as if it were embedded on this page, so we need to
  // make this check in the ContentParent.
  StorageAccessAPIHelper::
      AsyncCheckCookiesPermittedDecidesStorageAccessAPIOnChildProcess(
          GetBrowsingContext(), principal)
          ->Then(
              GetCurrentSerialEventTarget(), __func__,
              [inner, thirdPartyURI, bc, principal, hasUserActivation,
               aRequireUserActivation, self,
               promise](Maybe<bool> cookieResult) {
                // Handle the result of the cookie permission check that took
                // place in the ContentParent.
                if (cookieResult.isSome()) {
                  if (cookieResult.value()) {
                    return MozPromise<int, bool, true>::CreateAndResolve(
                        true, __func__);
                  }
                  return MozPromise<int, bool, true>::CreateAndReject(false,
                                                                      __func__);
                }

                // Step 4b: Check for the existing storage access permission
                nsAutoCString type;
                bool ok = AntiTrackingUtils::CreateStoragePermissionKey(
                    principal, type);
                if (!ok) {
                  return MozPromise<int, bool, true>::CreateAndReject(false,
                                                                      __func__);
                }
                if (AntiTrackingUtils::CheckStoragePermission(
                        self->NodePrincipal(), type,
                        self->IsInPrivateBrowsing(), nullptr, 0)) {
                  return MozPromise<int, bool, true>::CreateAndResolve(
                      true, __func__);
                }

                // Step 4c: Try to request storage access, either automatically
                // or with a user-prompt. This is the part that is async in the
                // typical requestStorageAccess function.
                return StorageAccessAPIHelper::RequestStorageAccessAsyncHelper(
                    self, inner, bc, principal, hasUserActivation,
                    aRequireUserActivation, false,
                    ContentBlockingNotifier::
                        ePrivilegeStorageAccessForOriginAPI,
                    true);
              },
              // If the IPC rejects, we should reject our promise here which
              // will cause a rejection of the promise we already returned
              [promise]() {
                return MozPromise<int, bool, true>::CreateAndReject(false,
                                                                    __func__);
              })
          ->Then(
              GetCurrentSerialEventTarget(), __func__,
              // If the previous handlers resolved, we should reinstate user
              // activation and resolve the promise we returned in Step 5.
              [self, inner, promise] {
                inner->SaveStorageAccessPermissionGranted();
                self->NotifyUserGestureActivation();
                promise->MaybeResolveWithUndefined();
              },
              // If the previous handler rejected, we should reject the promise
              // returned by this function.
              [promise] {
                promise->MaybeRejectWithNotAllowedError(
                    "requestStorageAccess not allowed"_ns);
              });

  // Step 5: While the async stuff is happening, we should return the promise so
  // our caller can continue executing.
  return promise.forget();
}

already_AddRefed<Promise> Document::RequestStorageAccessUnderSite(
    const nsAString& aSerializedSite, ErrorResult& aRv) {
  nsIGlobalObject* global = GetScopeObject();
  if (!global) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Check that we have user activation before proceeding to prevent
  // rapid calls to the API to leak information.
  if (!ConsumeTransientUserGestureActivation()) {
    // Report an error to the console for this case
    nsContentUtils::ReportToConsole(
        nsIScriptError::errorFlag, "requestStorageAccess"_ns, this,
        nsContentUtils::eDOM_PROPERTIES, "RequestStorageAccessUserGesture");
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Check if the provided URI is different-site to this Document
  nsCOMPtr<nsIURI> siteURI;
  nsresult rv = NS_NewURI(getter_AddRefs(siteURI), aSerializedSite);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }
  bool isCrossSiteArgument;
  rv = NodePrincipal()->IsThirdPartyURI(siteURI, &isCrossSiteArgument);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return nullptr;
  }
  if (!isCrossSiteArgument) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Check if this party has broad cookie permissions.
  Maybe<bool> resultBecauseCookiesApproved =
      StorageAccessAPIHelper::CheckCookiesPermittedDecidesStorageAccessAPI(
          CookieJarSettings(), NodePrincipal());
  if (resultBecauseCookiesApproved.isSome()) {
    if (resultBecauseCookiesApproved.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Check if browser settings preclude this document getting storage
  // access under the provided site
  Maybe<bool> resultBecauseBrowserSettings =
      StorageAccessAPIHelper::CheckBrowserSettingsDecidesStorageAccessAPI(
          CookieJarSettings(), true, false, true);
  if (resultBecauseBrowserSettings.isSome()) {
    if (resultBecauseBrowserSettings.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Check that this Document is same-site to the top
  Maybe<bool> resultBecauseCallContext = StorageAccessAPIHelper::
      CheckSameSiteCallingContextDecidesStorageAccessAPI(this, false);
  if (resultBecauseCallContext.isSome()) {
    if (resultBecauseCallContext.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  nsCOMPtr<nsIPrincipal> principal(NodePrincipal());

  // Test if the permission this is requesting is already set
  nsCOMPtr<nsIPrincipal> argumentPrincipal =
      BasePrincipal::CreateContentPrincipal(
          siteURI, NodePrincipal()->OriginAttributesRef());
  if (!argumentPrincipal) {
    ConsumeTransientUserGestureActivation();
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }
  nsCString originNoSuffix;
  rv = NodePrincipal()->GetOriginNoSuffix(originNoSuffix);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  ContentChild* cc = ContentChild::GetSingleton();
  MOZ_ASSERT(cc);
  RefPtr<Document> self(this);
  cc->SendTestStorageAccessPermission(argumentPrincipal, originNoSuffix)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise, siteURI,
           self](const ContentChild::TestStorageAccessPermissionPromise::
                     ResolveValueType& aResult) {
            if (aResult) {
              return StorageAccessAPIHelper::
                  StorageAccessPermissionGrantPromise::CreateAndResolve(
                      StorageAccessAPIHelper::eAllow, __func__);
            }
            // Get a grant for the storage access permission that will be set
            // when this is completed in the embedding context
            nsCString serializedSite;
            nsCOMPtr<nsIEffectiveTLDService> etld =
                mozilla::components::EffectiveTLD::Service();
            if (!etld) {
              return StorageAccessAPIHelper::
                  StorageAccessPermissionGrantPromise::CreateAndReject(
                      false, __func__);
            }
            nsresult rv = etld->GetSite(siteURI, serializedSite);
            if (NS_FAILED(rv)) {
              return StorageAccessAPIHelper::
                  StorageAccessPermissionGrantPromise::CreateAndReject(
                      false, __func__);
            }
            return self->CreatePermissionGrantPromise(
                self->GetInnerWindow(), self->NodePrincipal(), true, true,
                Some(serializedSite), false)();
          },
          [](const ContentChild::TestStorageAccessPermissionPromise::
                 RejectValueType& aResult) {
            return StorageAccessAPIHelper::StorageAccessPermissionGrantPromise::
                CreateAndReject(false, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise, principal, siteURI](int result) {
            ContentChild* cc = ContentChild::GetSingleton();
            if (!cc) {
              // TODO(bug 1778561): Make this work in non-content processes.
              promise->MaybeRejectWithUndefined();
              return;
            }
            // Set a permission in the parent process that this document wants
            // storage access under the argument's site, resolving our returned
            // promise on success
            cc->SendSetAllowStorageAccessRequestFlag(principal, siteURI)
                ->Then(
                    GetCurrentSerialEventTarget(), __func__,
                    [promise](bool success) {
                      if (success) {
                        promise->MaybeResolveWithUndefined();
                      } else {
                        promise->MaybeRejectWithUndefined();
                      }
                    },
                    [promise](mozilla::ipc::ResponseRejectReason reason) {
                      promise->MaybeRejectWithUndefined();
                    });
          },
          [promise](bool result) { promise->MaybeRejectWithUndefined(); });

  // Return the promise that is resolved in the async handler above
  return promise.forget();
}

already_AddRefed<Promise> Document::CompleteStorageAccessRequestFromSite(
    const nsAString& aSerializedOrigin, ErrorResult& aRv) {
  nsIGlobalObject* global = GetScopeObject();
  if (!global) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  // Check that the provided URI is different-site to this Document
  nsCOMPtr<nsIURI> argumentURI;
  nsresult rv = NS_NewURI(getter_AddRefs(argumentURI), aSerializedOrigin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }
  bool isCrossSiteArgument;
  rv = NodePrincipal()->IsThirdPartyURI(argumentURI, &isCrossSiteArgument);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aRv.Throw(rv);
    return nullptr;
  }
  if (!isCrossSiteArgument) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Check if browser settings preclude this document getting storage
  // access under the provided site
  Maybe<bool> resultBecauseBrowserSettings =
      StorageAccessAPIHelper::CheckBrowserSettingsDecidesStorageAccessAPI(
          CookieJarSettings(), true, false, true);
  if (resultBecauseBrowserSettings.isSome()) {
    if (resultBecauseBrowserSettings.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Check that this Document is same-site to the top
  Maybe<bool> resultBecauseCallContext = StorageAccessAPIHelper::
      CheckSameSiteCallingContextDecidesStorageAccessAPI(this, false);
  if (resultBecauseCallContext.isSome()) {
    if (resultBecauseCallContext.value()) {
      promise->MaybeResolveWithUndefined();
      return promise.forget();
    }
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Create principal of the embedded site requesting storage access
  nsCOMPtr<nsIPrincipal> principal = BasePrincipal::CreateContentPrincipal(
      argumentURI, NodePrincipal()->OriginAttributesRef());
  if (!principal) {
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }

  // Get versions of these objects that we can use in lambdas for callbacks
  RefPtr<Document> self(this);
  RefPtr<BrowsingContext> bc = GetBrowsingContext();
  nsCOMPtr<nsPIDOMWindowInner> inner = GetInnerWindow();

  // Test that the permission was set by a call to RequestStorageAccessUnderSite
  // from a top level document that is same-site with the argument
  ContentChild* cc = ContentChild::GetSingleton();
  if (!cc) {
    // TODO(bug 1778561): Make this work in non-content processes.
    promise->MaybeRejectWithUndefined();
    return promise.forget();
  }
  cc->SendTestAllowStorageAccessRequestFlag(NodePrincipal(), argumentURI)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [inner, bc, self, principal](bool success) {
            if (success) {
              // If that resolved with true, check that we don't already have a
              // permission that gives cookie access.
              return StorageAccessAPIHelper::
                  AsyncCheckCookiesPermittedDecidesStorageAccessAPIOnChildProcess(
                      bc, principal);
            }
            return MozPromise<Maybe<bool>, nsresult, true>::CreateAndReject(
                NS_ERROR_FAILURE, __func__);
          },
          [](mozilla::ipc::ResponseRejectReason reason) {
            return MozPromise<Maybe<bool>, nsresult, true>::CreateAndReject(
                NS_ERROR_FAILURE, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [inner, bc, principal, self, promise](Maybe<bool> cookieResult) {
            // Handle the result of the cookie permission check that took place
            // in the ContentParent.
            if (cookieResult.isSome()) {
              if (cookieResult.value()) {
                return StorageAccessAPIHelper::
                    StorageAccessPermissionGrantPromise::CreateAndResolve(
                        StorageAccessAPIHelper::eAllowAutoGrant, __func__);
              }
              return StorageAccessAPIHelper::
                  StorageAccessPermissionGrantPromise::CreateAndReject(
                      false, __func__);
            }

            // Check for the existing storage access permission
            nsAutoCString type;
            bool ok =
                AntiTrackingUtils::CreateStoragePermissionKey(principal, type);
            if (!ok) {
              return StorageAccessAPIHelper::
                  StorageAccessPermissionGrantPromise::CreateAndReject(
                      false, __func__);
            }
            if (AntiTrackingUtils::CheckStoragePermission(
                    self->NodePrincipal(), type, self->IsInPrivateBrowsing(),
                    nullptr, 0)) {
              return StorageAccessAPIHelper::
                  StorageAccessPermissionGrantPromise::CreateAndResolve(
                      StorageAccessAPIHelper::eAllowAutoGrant, __func__);
            }

            // Try to request storage access, ignoring the final checks.
            // We ignore the final checks because this is where the "grant"
            // either by prompt doorhanger or autogrant takes place. We already
            // gathered an equivalent grant in requestStorageAccessUnderSite.
            return StorageAccessAPIHelper::RequestStorageAccessAsyncHelper(
                self, inner, bc, principal, true, true, false,
                ContentBlockingNotifier::eStorageAccessAPI, false);
          },
          // If the IPC rejects, we should reject our promise here which will
          // cause a rejection of the promise we already returned
          [promise]() {
            return MozPromise<int, bool, true>::CreateAndReject(false,
                                                                __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          // If the previous handlers resolved, we should reinstate user
          // activation and resolve the promise we returned in Step 5.
          [self, inner, promise] {
            inner->SaveStorageAccessPermissionGranted();
            promise->MaybeResolveWithUndefined();
          },
          // If the previous handler rejected, we should reject the promise
          // returned by this function.
          [promise] { promise->MaybeRejectWithUndefined(); });

  return promise.forget();
}

nsTHashSet<RefPtr<WakeLockSentinel>>& Document::ActiveWakeLocks(
    WakeLockType aType) {
  return mActiveLocks.LookupOrInsert(aType);
}

class UnlockAllWakeLockRunnable final : public Runnable {
 public:
  UnlockAllWakeLockRunnable(WakeLockType aType, Document* aDoc)
      : Runnable("UnlockAllWakeLocks"), mType(aType), mDoc(aDoc) {}

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.  See
  // bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Run() override {
    // Move, as ReleaseWakeLock will try to remove from and possibly allow
    // scripts via onrelease to add to document.[[ActiveLocks]]["screen"]
    nsCOMPtr<Document> doc = mDoc;
    nsTHashSet<RefPtr<WakeLockSentinel>> locks =
        std::move(doc->ActiveWakeLocks(mType));
    for (const auto& lock : locks) {
      // ReleaseWakeLock runs script, which could release other locks
      if (!lock->Released()) {
        ReleaseWakeLock(doc, MOZ_KnownLive(lock), mType);
      }
    }
    return NS_OK;
  }

 protected:
  ~UnlockAllWakeLockRunnable() = default;

 private:
  WakeLockType mType;
  nsCOMPtr<Document> mDoc;
};

void Document::UnlockAllWakeLocks(WakeLockType aType) {
  // Perform unlock in a runnable to prevent UnlockAll being MOZ_CAN_RUN_SCRIPT
  if (!ActiveWakeLocks(aType).IsEmpty()) {
    RefPtr<UnlockAllWakeLockRunnable> runnable =
        MakeRefPtr<UnlockAllWakeLockRunnable>(aType, this);
    nsresult rv = NS_DispatchToMainThread(runnable);
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
    Unused << rv;
  }
}

RefPtr<Document::AutomaticStorageAccessPermissionGrantPromise>
Document::AutomaticStorageAccessPermissionCanBeGranted(
    bool hasUserActivation, bool isThirdPartyTracker) {
  // requestStorageAccessForOrigin may not require user activation. If we don't
  // have user activation at this point we should always show the prompt.
  if (!hasUserActivation ||
      !StaticPrefs::privacy_antitracking_enableWebcompat()) {
    return AutomaticStorageAccessPermissionGrantPromise::CreateAndResolve(
        false, __func__);
  }

  if (isThirdPartyTracker &&
      StaticPrefs::
          dom_storage_access_auto_grants_exclude_third_party_trackers()) {
    return AutomaticStorageAccessPermissionGrantPromise::CreateAndResolve(
        false, __func__);
  }

  if (XRE_IsContentProcess()) {
    // In the content process, we need to ask the parent process to compute
    // this.  The reason is that nsIPermissionManager::GetAllWithTypePrefix()
    // isn't accessible in the content process.
    ContentChild* cc = ContentChild::GetSingleton();
    MOZ_ASSERT(cc);

    return cc->SendAutomaticStorageAccessPermissionCanBeGranted(NodePrincipal())
        ->Then(GetCurrentSerialEventTarget(), __func__,
               [](const ContentChild::
                      AutomaticStorageAccessPermissionCanBeGrantedPromise::
                          ResolveOrRejectValue& aValue) {
                 if (aValue.IsResolve()) {
                   return AutomaticStorageAccessPermissionGrantPromise::
                       CreateAndResolve(aValue.ResolveValue(), __func__);
                 }

                 return AutomaticStorageAccessPermissionGrantPromise::
                     CreateAndReject(false, __func__);
               });
  }

  if (XRE_IsParentProcess()) {
    // In the parent process, we can directly compute this.
    return AutomaticStorageAccessPermissionGrantPromise::CreateAndResolve(
        AutomaticStorageAccessPermissionCanBeGranted(NodePrincipal()),
        __func__);
  }

  return AutomaticStorageAccessPermissionGrantPromise::CreateAndReject(
      false, __func__);
}

bool Document::AutomaticStorageAccessPermissionCanBeGranted(
    nsIPrincipal* aPrincipal) {
  if (!StaticPrefs::dom_storage_access_auto_grants()) {
    return false;
  }

  if (!ContentBlockingUserInteraction::Exists(aPrincipal)) {
    return false;
  }

  nsCOMPtr<nsIBrowserUsage> bu = do_ImportESModule(
      "resource:///modules/BrowserUsageTelemetry.sys.mjs", fallible);
  if (NS_WARN_IF(!bu)) {
    return false;
  }

  uint32_t uniqueDomainsVisitedInPast24Hours = 0;
  nsresult rv = bu->GetUniqueDomainsVisitedInPast24Hours(
      &uniqueDomainsVisitedInPast24Hours);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  Maybe<size_t> maybeOriginsThirdPartyHasAccessTo =
      AntiTrackingUtils::CountSitesAllowStorageAccess(aPrincipal);
  if (maybeOriginsThirdPartyHasAccessTo.isNothing()) {
    return false;
  }
  size_t originsThirdPartyHasAccessTo =
      maybeOriginsThirdPartyHasAccessTo.value();

  // one percent of the number of top-levels origins visited in the current
  // session (but not to exceed 24 hours), or the value of the
  // dom.storage_access.max_concurrent_auto_grants preference, whichever is
  // higher.
  size_t maxConcurrentAutomaticGrants = std::max(
      std::max(int(std::floor(uniqueDomainsVisitedInPast24Hours / 100)),
               StaticPrefs::dom_storage_access_max_concurrent_auto_grants()),
      0);

  return originsThirdPartyHasAccessTo < maxConcurrentAutomaticGrants;
}

void Document::RecordNavigationTiming(ReadyState aReadyState) {
  if (!XRE_IsContentProcess()) {
    return;
  }
  if (!IsTopLevelContentDocument()) {
    return;
  }
  // If we dont have the timing yet (mostly because the doc is still loading),
  // get it from docshell.
  RefPtr<nsDOMNavigationTiming> timing = mTiming;
  if (!timing) {
    if (!mDocumentContainer) {
      return;
    }
    timing = mDocumentContainer->GetNavigationTiming();
    if (!timing) {
      return;
    }
  }
  TimeStamp startTime = timing->GetNavigationStartTimeStamp();
  switch (aReadyState) {
    case READYSTATE_LOADING:
      if (!mDOMLoadingSet) {
        glean::performance_time::to_dom_loading.AccumulateRawDuration(
            TimeStamp::Now() - startTime);
        mDOMLoadingSet = true;
      }
      break;
    case READYSTATE_INTERACTIVE:
      if (!mDOMInteractiveSet) {
        glean::performance_time::dom_interactive.AccumulateRawDuration(
            TimeStamp::Now() - startTime);
        mDOMInteractiveSet = true;
      }
      break;
    case READYSTATE_COMPLETE:
      if (!mDOMCompleteSet) {
        glean::performance_time::dom_complete.AccumulateRawDuration(
            TimeStamp::Now() - startTime);
        mDOMCompleteSet = true;
      }
      break;
    default:
      NS_WARNING("Unexpected ReadyState value");
      break;
  }
}

void Document::ReportShadowDOMUsage() {
  nsPIDOMWindowInner* inner = GetInnerWindow();
  if (NS_WARN_IF(!inner)) {
    return;
  }

  WindowContext* wc = inner->GetWindowContext();
  if (NS_WARN_IF(!wc || wc->IsDiscarded())) {
    return;
  }

  WindowContext* topWc = wc->TopWindowContext();
  if (topWc->GetHasReportedShadowDOMUsage()) {
    return;
  }

  MOZ_ALWAYS_SUCCEEDS(topWc->SetHasReportedShadowDOMUsage(true));
}

// static
bool Document::StorageAccessSandboxed(uint32_t aSandboxFlags) {
  return StaticPrefs::dom_storage_access_enabled() &&
         (aSandboxFlags & SANDBOXED_STORAGE_ACCESS) != 0;
}

bool Document::StorageAccessSandboxed() const {
  return Document::StorageAccessSandboxed(GetSandboxFlags());
}

bool Document::GetCachedSizes(nsTabSizes* aSizes) {
  if (mCachedTabSizeGeneration == 0 ||
      GetGeneration() != mCachedTabSizeGeneration) {
    return false;
  }
  aSizes->mDom += mCachedTabSizes.mDom;
  aSizes->mStyle += mCachedTabSizes.mStyle;
  aSizes->mOther += mCachedTabSizes.mOther;
  return true;
}

void Document::SetCachedSizes(nsTabSizes* aSizes) {
  mCachedTabSizes.mDom = aSizes->mDom;
  mCachedTabSizes.mStyle = aSizes->mStyle;
  mCachedTabSizes.mOther = aSizes->mOther;
  mCachedTabSizeGeneration = GetGeneration();
}

nsAtom* Document::GetContentLanguageAsAtomForStyle() const {
  // Content-Language may be a comma-separated list of language codes,
  // in which case the HTML5 spec says to treat it as unknown
  if (mContentLanguage &&
      !nsDependentAtomString(mContentLanguage).Contains(char16_t(','))) {
    return GetContentLanguage();
  }

  return nullptr;
}

nsAtom* Document::GetLanguageForStyle() const {
  if (nsAtom* lang = GetContentLanguageAsAtomForStyle()) {
    return lang;
  }
  return mLanguageFromCharset.get();
}

void Document::GetContentLanguageForBindings(DOMString& aString) const {
  aString.SetKnownLiveAtom(mContentLanguage, DOMString::eTreatNullAsEmpty);
}

const LangGroupFontPrefs* Document::GetFontPrefsForLang(
    nsAtom* aLanguage, bool* aNeedsToCache) const {
  nsAtom* lang = aLanguage ? aLanguage : mLanguageFromCharset.get();
  return StaticPresData::Get()->GetFontPrefsForLang(lang, aNeedsToCache);
}

void Document::DoCacheAllKnownLangPrefs() {
  MOZ_ASSERT(mMayNeedFontPrefsUpdate);
  RefPtr<nsAtom> lang = GetLanguageForStyle();
  StaticPresData* data = StaticPresData::Get();
  data->GetFontPrefsForLang(lang ? lang.get() : mLanguageFromCharset.get());
  data->GetFontPrefsForLang(nsGkAtoms::x_math);
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1362599#c12
  data->GetFontPrefsForLang(nsGkAtoms::Unicode);
  for (const auto& key : mLanguagesUsed) {
    data->GetFontPrefsForLang(key);
  }
  mMayNeedFontPrefsUpdate = false;
}

void Document::RecomputeLanguageFromCharset() {
  RefPtr<nsAtom> language;
  // Optimize the default character sets.
  if (mCharacterSet == WINDOWS_1252_ENCODING) {
    language = nsGkAtoms::x_western;
  } else {
    nsLanguageAtomService* service = nsLanguageAtomService::GetService();
    if (mCharacterSet == UTF_8_ENCODING) {
      language = nsGkAtoms::Unicode;
    } else {
      language = service->LookupCharSet(mCharacterSet);
    }

    if (language == nsGkAtoms::Unicode) {
      language = service->GetLocaleLanguage();
    }
  }

  if (language == mLanguageFromCharset) {
    return;
  }

  mMayNeedFontPrefsUpdate = true;
  mLanguageFromCharset = std::move(language);
}

nsICookieJarSettings* Document::CookieJarSettings() {
  // If we are here, this is probably a javascript: URL document. In any case,
  // we must have a nsCookieJarSettings. Let's create it.
  if (!mCookieJarSettings) {
    Document* inProcessParent = GetInProcessParentDocument();

    auto shouldInheritFrom = [this](Document* aDoc) {
      return aDoc && (this->NodePrincipal()->Equals(aDoc->NodePrincipal()) ||
                      this->NodePrincipal()->GetIsNullPrincipal());
    };
    RefPtr<BrowsingContext> opener =
        GetBrowsingContext() ? GetBrowsingContext()->GetOpener() : nullptr;

    if (inProcessParent) {
      mCookieJarSettings = net::CookieJarSettings::Create(
          inProcessParent->CookieJarSettings()->GetCookieBehavior(),
          mozilla::net::CookieJarSettings::Cast(
              inProcessParent->CookieJarSettings())
              ->GetPartitionKey(),
          inProcessParent->CookieJarSettings()->GetIsFirstPartyIsolated(),
          inProcessParent->CookieJarSettings()
              ->GetIsOnContentBlockingAllowList(),
          inProcessParent->CookieJarSettings()
              ->GetShouldResistFingerprinting());

      // Inherit the fingerprinting random key from the parent.
      nsTArray<uint8_t> randomKey;
      nsresult rv = inProcessParent->CookieJarSettings()
                        ->GetFingerprintingRandomizationKey(randomKey);

      if (NS_SUCCEEDED(rv)) {
        net::CookieJarSettings::Cast(mCookieJarSettings)
            ->SetFingerprintingRandomizationKey(randomKey);
      }

      // Inerit the top level windowContext id from the parent.
      net::CookieJarSettings::Cast(mCookieJarSettings)
          ->SetTopLevelWindowContextId(
              net::CookieJarSettings::Cast(inProcessParent->CookieJarSettings())
                  ->GetTopLevelWindowContextId());
    } else if (opener && shouldInheritFrom(opener->GetDocument())) {
      mCookieJarSettings = net::CookieJarSettings::Create(NodePrincipal());

      nsTArray<uint8_t> randomKey;
      nsresult rv = opener->GetDocument()
                        ->CookieJarSettings()
                        ->GetFingerprintingRandomizationKey(randomKey);

      if (NS_SUCCEEDED(rv)) {
        net::CookieJarSettings::Cast(mCookieJarSettings)
            ->SetFingerprintingRandomizationKey(randomKey);
      }
    } else {
      mCookieJarSettings = net::CookieJarSettings::Create(NodePrincipal());

      if (IsTopLevelContentDocument()) {
        net::CookieJarSettings::Cast(mCookieJarSettings)
            ->SetTopLevelWindowContextId(InnerWindowID());
      }
    }

    if (auto* wgc = GetWindowGlobalChild()) {
      net::CookieJarSettingsArgs csArgs;

      net::CookieJarSettings::Cast(mCookieJarSettings)->Serialize(csArgs);
      // Update cookie settings in the parent process
      if (!wgc->SendUpdateCookieJarSettings(csArgs)) {
        NS_WARNING(
            "Failed to update document's cookie jar settings on the "
            "WindowGlobalParent");
      }
    }
  }

  return mCookieJarSettings;
}

bool Document::UsingStorageAccess() {
  if (WindowContext* wc = GetWindowContext()) {
    return wc->GetUsingStorageAccess();
  }

  // If we don't yet have a window context, we have to use the decision
  // from the Document's Channel's LoadInfo directly.
  if (!mChannel) {
    return false;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  return loadInfo->GetStoragePermission() != nsILoadInfo::NoStoragePermission;
}

bool Document::IsOn3PCBExceptionList() const {
  if (!mChannel) {
    return false;
  }
  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();

  return loadInfo->GetIsOn3PCBExceptionList();
}

bool Document::HasStorageAccessPermissionGrantedByAllowList() {
  // We only care about if the document gets the storage permission via the
  // allow list here. So we don't check the storage access cache in the inner
  // window.

  if (!mChannel) {
    return false;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = mChannel->LoadInfo();
  return loadInfo->GetStoragePermission() ==
         nsILoadInfo::StoragePermissionAllowListed;
}

bool Document::InAndroidPipMode() const {
  auto* bc = BrowserChild::GetFrom(GetDocShell());
  return bc && bc->InAndroidPipMode();
}

nsIPrincipal* Document::EffectiveStoragePrincipal() const {
  if (!StaticPrefs::
          privacy_partition_always_partition_third_party_non_cookie_storage()) {
    return EffectiveCookiePrincipal();
  }

  nsPIDOMWindowInner* inner = GetInnerWindow();
  if (!inner) {
    return NodePrincipal();
  }

  // Return our cached storage principal if one exists.
  if (mActiveStoragePrincipal) {
    return mActiveStoragePrincipal;
  }

  // Calling StorageAllowedForDocument will notify the ContentBlockLog. This
  // loads TrackingDBService.sys.mjs, making us potentially
  // fail // browser/base/content/test/performance/browser_startup.js. To avoid
  // that, we short-circuit the check here by allowing storage access to system
  // and addon principles, avoiding the test-failure.
  nsIPrincipal* principal = NodePrincipal();
  if (principal && (principal->IsSystemPrincipal() ||
                    principal->GetIsAddonOrExpandedAddonPrincipal())) {
    return mActiveStoragePrincipal = NodePrincipal();
  }

  auto cookieJarSettings = const_cast<Document*>(this)->CookieJarSettings();
  if (cookieJarSettings->GetIsOnContentBlockingAllowList()) {
    return mActiveStoragePrincipal = NodePrincipal();
  }

  // We use the lower-level ContentBlocking API here to ensure this
  // check doesn't send notifications.
  uint32_t rejectedReason = 0;
  if (ShouldAllowAccessFor(inner, GetDocumentURI(), false, &rejectedReason)) {
    return mActiveStoragePrincipal = NodePrincipal();
  }

  // Let's use the storage principal only if we need to partition the cookie
  // jar. When the permission is granted, access will be different and the
  // normal principal will be used.
  if (ShouldPartitionStorage(rejectedReason) &&
      !StoragePartitioningEnabled(
          rejectedReason, const_cast<Document*>(this)->CookieJarSettings())) {
    return mActiveStoragePrincipal = NodePrincipal();
  }

  Unused << NS_WARN_IF(NS_FAILED(StoragePrincipalHelper::GetPrincipal(
      nsGlobalWindowInner::Cast(inner),
      StoragePrincipalHelper::eForeignPartitionedPrincipal,
      getter_AddRefs(mActiveStoragePrincipal))));
  return mActiveStoragePrincipal;
}

nsIPrincipal* Document::EffectiveCookiePrincipal() const {
  nsPIDOMWindowInner* inner = GetInnerWindow();
  if (!inner) {
    return NodePrincipal();
  }

  // Return our cached storage principal if one exists.
  //
  // Handle special case where privacy_partition_always_partition_third_party
  // _non_cookie_storage is disabled and the loading document has
  // StorageAccess. The pref will lead to WindowGlobalChild::OnNewDocument
  // setting the documents StoragePrincipal on the parent to the documents
  // EffectiveCookiePrincipal. Since this happens before the WindowContext,
  // including possible StorageAccess, is set the PartitonedPrincipal will be
  // selected and cached. Since no change of permission occured it won't be
  // updated later. Avoid this by not using a cached PartitionedPrincipal if
  // the pref is disabled, this should rarely happen since the pref defaults to
  // true. See Bug 1899570.
  if (mActiveCookiePrincipal &&
      (StaticPrefs::
           privacy_partition_always_partition_third_party_non_cookie_storage() ||
       mActiveCookiePrincipal != mPartitionedPrincipal)) {
    return mActiveCookiePrincipal;
  }

  // We use the lower-level ContentBlocking API here to ensure this
  // check doesn't send notifications.
  uint32_t rejectedReason = 0;
  if (ShouldAllowAccessFor(inner, GetDocumentURI(), true, &rejectedReason)) {
    return mActiveCookiePrincipal = NodePrincipal();
  }

  // Let's use the storage principal only if we need to partition the cookie
  // jar. When the permission is granted, access will be different and the
  // normal principal will be used.
  if (ShouldPartitionStorage(rejectedReason) &&
      !StoragePartitioningEnabled(
          rejectedReason, const_cast<Document*>(this)->CookieJarSettings())) {
    return mActiveCookiePrincipal = NodePrincipal();
  }

  return mActiveCookiePrincipal = mPartitionedPrincipal;
}

nsIPrincipal* Document::GetPrincipalForPrefBasedHacks() const {
  // If the document is sandboxed document or data: document, we should
  // get URI of the parent document.
  for (const Document* document = this;
       document && document->IsContentDocument();
       document = document->GetInProcessParentDocument()) {
    // The document URI may be about:blank even if it comes from actual web
    // site.  Therefore, we need to check the URI of its principal.
    nsIPrincipal* principal = document->NodePrincipal();
    if (principal->GetIsNullPrincipal()) {
      continue;
    }
    return principal;
  }
  return nullptr;
}

void Document::SetIsInitialDocument(bool aIsInitialDocument) {
  mIsInitialDocumentInWindow = aIsInitialDocument;

  if (aIsInitialDocument && !mIsEverInitialDocumentInWindow) {
    mIsEverInitialDocumentInWindow = aIsInitialDocument;
  }

  // Asynchronously tell the parent process that we are, or are no longer, the
  // initial document. This happens async.
  if (auto* wgc = GetWindowGlobalChild()) {
    wgc->SendSetIsInitialDocument(aIsInitialDocument);
  }
}

// static
void Document::AddToplevelLoadingDocument(Document* aDoc) {
  MOZ_ASSERT(aDoc && aDoc->IsTopLevelContentDocument());
  // Currently we're interested in foreground documents only, so bail out early.
  if (aDoc->IsInBackgroundWindow() || !XRE_IsContentProcess()) {
    return;
  }

  if (!sLoadingForegroundTopLevelContentDocument) {
    sLoadingForegroundTopLevelContentDocument = new AutoTArray<Document*, 8>();
    mozilla::ipc::IdleSchedulerChild* idleScheduler =
        mozilla::ipc::IdleSchedulerChild::GetMainThreadIdleScheduler();
    if (idleScheduler) {
      idleScheduler->SendRunningPrioritizedOperation();
    }
  }
  if (!sLoadingForegroundTopLevelContentDocument->Contains(aDoc)) {
    sLoadingForegroundTopLevelContentDocument->AppendElement(aDoc);
  }
}

// static
void Document::RemoveToplevelLoadingDocument(Document* aDoc) {
  MOZ_ASSERT(aDoc && aDoc->IsTopLevelContentDocument());
  if (sLoadingForegroundTopLevelContentDocument) {
    sLoadingForegroundTopLevelContentDocument->RemoveElement(aDoc);
    if (sLoadingForegroundTopLevelContentDocument->IsEmpty()) {
      delete sLoadingForegroundTopLevelContentDocument;
      sLoadingForegroundTopLevelContentDocument = nullptr;

      mozilla::ipc::IdleSchedulerChild* idleScheduler =
          mozilla::ipc::IdleSchedulerChild::GetMainThreadIdleScheduler();
      if (idleScheduler) {
        idleScheduler->SendPrioritizedOperationDone();
      }
    }
  }
}

ColorScheme Document::DefaultColorScheme() const {
  return LookAndFeel::ColorSchemeForStyle(*this, {GetColorSchemeBits()});
}

ColorScheme Document::PreferredColorScheme(IgnoreRFP aIgnoreRFP) const {
  if (ShouldResistFingerprinting(RFPTarget::CSSPrefersColorScheme) &&
      aIgnoreRFP == IgnoreRFP::No) {
    return ColorScheme::Light;
  }

  if (nsPresContext* pc = GetPresContext()) {
    if (auto scheme = pc->GetOverriddenOrEmbedderColorScheme()) {
      return *scheme;
    }
  }

  return PreferenceSheet::PrefsFor(*this).mColorScheme;
}

bool Document::HasRecentlyStartedForegroundLoads() {
  if (!sLoadingForegroundTopLevelContentDocument) {
    return false;
  }

  for (size_t i = 0; i < sLoadingForegroundTopLevelContentDocument->Length();
       ++i) {
    Document* doc = sLoadingForegroundTopLevelContentDocument->ElementAt(i);
    // A page loaded in foreground could be in background now.
    if (!doc->IsInBackgroundWindow()) {
      nsPIDOMWindowInner* win = doc->GetInnerWindow();
      if (win) {
        Performance* perf = win->GetPerformance();
        if (perf &&
            perf->Now() < StaticPrefs::page_load_deprioritization_period()) {
          return true;
        }
      }
    }
  }

  // Didn't find any loading foreground documents, just clear the array.
  delete sLoadingForegroundTopLevelContentDocument;
  sLoadingForegroundTopLevelContentDocument = nullptr;

  mozilla::ipc::IdleSchedulerChild* idleScheduler =
      mozilla::ipc::IdleSchedulerChild::GetMainThreadIdleScheduler();
  if (idleScheduler) {
    idleScheduler->SendPrioritizedOperationDone();
  }
  return false;
}

void Document::AddPendingFrameStaticClone(nsFrameLoaderOwner* aElement,
                                          nsFrameLoader* aStaticCloneOf) {
  PendingFrameStaticClone* clone = mPendingFrameStaticClones.AppendElement();
  clone->mElement = aElement;
  clone->mStaticCloneOf = aStaticCloneOf;
}

bool Document::ShouldAvoidNativeTheme() const {
  return !IsInChromeDocShell() || XRE_IsContentProcess();
}

bool Document::UseRegularPrincipal() const {
  return EffectiveStoragePrincipal() == NodePrincipal();
}

bool Document::HasThirdPartyChannel() {
  nsCOMPtr<nsIChannel> channel = GetChannel();
  if (channel) {
    // We assume that the channel is a third-party by default.
    bool thirdParty = true;

    nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil =
        components::ThirdPartyUtil::Service();
    if (!thirdPartyUtil) {
      return thirdParty;
    }

    // Check that if the channel is a third-party to its parent.
    nsresult rv =
        thirdPartyUtil->IsThirdPartyChannel(channel, nullptr, &thirdParty);
    if (NS_FAILED(rv)) {
      // Assume third-party in case of failure
      thirdParty = true;
    }

    return thirdParty;
  }

  if (mParentDocument) {
    return mParentDocument->HasThirdPartyChannel();
  }

  return false;
}

bool Document::IsLikelyContentInaccessibleTopLevelAboutBlank() const {
  if (!mDocumentURI || !NS_IsAboutBlank(mDocumentURI)) {
    return false;
  }
  // FIXME(emilio): This is not quite edge-case free. See bug 1860098.
  //
  // For stuff in frames, that makes our per-document telemetry probes not
  // really reliable but doesn't affect the correctness of our page probes, so
  // it's not too terrible.
  BrowsingContext* bc = GetBrowsingContext();
  return bc && bc->IsTop() && !bc->GetTopLevelCreatedByWebContent();
}

bool Document::ShouldIncludeInTelemetry() const {
  if (!IsContentDocument() && !IsResourceDoc()) {
    return false;
  }

  if (IsLikelyContentInaccessibleTopLevelAboutBlank()) {
    return false;
  }

  nsIPrincipal* prin = NodePrincipal();
  // TODO(emilio): Should this use GetIsContentPrincipal() +
  // GetPrecursorPrincipal() instead (accounting for add-ons separately)?
  return !(prin->GetIsAddonOrExpandedAddonPrincipal() ||
           prin->IsSystemPrincipal() || prin->SchemeIs("about") ||
           prin->SchemeIs("chrome") || prin->SchemeIs("resource"));
}

void Document::GetConnectedShadowRoots(
    nsTArray<RefPtr<ShadowRoot>>& aOut) const {
  AppendToArray(aOut, mComposedShadowRoots);
}

void Document::AddMediaElementWithMSE() {
  if (mMediaElementWithMSECount++ == 0) {
    if (WindowGlobalChild* wgc = GetWindowGlobalChild()) {
      wgc->BlockBFCacheFor(BFCacheStatus::CONTAINS_MSE_CONTENT);
    }
  }
}

void Document::RemoveMediaElementWithMSE() {
  MOZ_ASSERT(mMediaElementWithMSECount > 0);
  if (--mMediaElementWithMSECount == 0) {
    if (WindowGlobalChild* wgc = GetWindowGlobalChild()) {
      wgc->UnblockBFCacheFor(BFCacheStatus::CONTAINS_MSE_CONTENT);
    }
  }
}

void Document::UnregisterFromMemoryReportingForDataDocument() {
  if (!mAddedToMemoryReportingAsDataDocument) {
    return;
  }
  mAddedToMemoryReportingAsDataDocument = false;
  nsIGlobalObject* global = GetScopeObject();
  if (global) {
    if (nsPIDOMWindowInner* win = global->GetAsInnerWindow()) {
      nsGlobalWindowInner::Cast(win)->UnregisterDataDocumentForMemoryReporting(
          this);
    }
  }
}
void Document::OOPChildLoadStarted(BrowserBridgeChild* aChild) {
  MOZ_DIAGNOSTIC_ASSERT(!mOOPChildrenLoading.Contains(aChild));
  mOOPChildrenLoading.AppendElement(aChild);
  if (mOOPChildrenLoading.Length() == 1) {
    // Let's block unload so that we're blocked from going into the BFCache
    // until the child has actually notified us that it has done loading.
    BlockOnload();
  }
}

void Document::OOPChildLoadDone(BrowserBridgeChild* aChild) {
  // aChild will not be in the list if nsDocLoader::Stop() was called, since
  // that clears mOOPChildrenLoading.  It also dispatches the 'load' event,
  // so we don't need to call DocLoaderIsEmpty in that case.
  if (mOOPChildrenLoading.RemoveElement(aChild)) {
    if (mOOPChildrenLoading.IsEmpty()) {
      UnblockOnload(false);
    }
    RefPtr<nsDocLoader> docLoader(mDocumentContainer);
    if (docLoader) {
      docLoader->OOPChildrenLoadingIsEmpty();
    }
  }
}

void Document::ClearOOPChildrenLoading() {
  nsTArray<const BrowserBridgeChild*> oopChildrenLoading;
  mOOPChildrenLoading.SwapElements(oopChildrenLoading);
  if (!oopChildrenLoading.IsEmpty()) {
    UnblockOnload(false);
  }
}

bool Document::MayHaveDOMActivateListeners() const {
  if (nsPIDOMWindowInner* inner = GetInnerWindow()) {
    return inner->HasDOMActivateEventListeners();
  }

  // If we can't get information from the window object, default to true.
  return true;
}

HighlightRegistry& Document::HighlightRegistry() {
  if (!mHighlightRegistry) {
    mHighlightRegistry = MakeRefPtr<class HighlightRegistry>(this);
  }
  return *mHighlightRegistry;
}

FragmentDirective* Document::FragmentDirective() {
  if (!mFragmentDirective) {
    mFragmentDirective = MakeRefPtr<class FragmentDirective>(this);
  }
  return mFragmentDirective;
}

RadioGroupContainer& Document::OwnedRadioGroupContainer() {
  if (!mRadioGroupContainer) {
    mRadioGroupContainer = MakeUnique<RadioGroupContainer>();
  }
  return *mRadioGroupContainer;
}

void Document::UpdateHiddenByContentVisibilityForAnimations() {
  for (AnimationTimeline* timeline : Timelines()) {
    timeline->UpdateHiddenByContentVisibility();
  }
}

void Document::SetAllowDeclarativeShadowRoots(
    bool aAllowDeclarativeShadowRoots) {
  mAllowDeclarativeShadowRoots = aAllowDeclarativeShadowRoots;
}

bool Document::AllowsDeclarativeShadowRoots() const {
  return mAllowDeclarativeShadowRoots;
}

static already_AddRefed<Document> CreateHTMLDocument(GlobalObject& aGlobal,
                                                     bool aLoadedAsData,
                                                     ErrorResult& aError) {
  nsCOMPtr<nsIURI> uri;
  aError = NS_NewURI(getter_AddRefs(uri), "about:blank");
  if (aError.Failed()) {
    return nullptr;
  }

  nsCOMPtr<Document> doc;
  aError =
      NS_NewHTMLDocument(getter_AddRefs(doc), aGlobal.GetSubjectPrincipal(),
                         aGlobal.GetSubjectPrincipal(), aLoadedAsData);
  if (aError.Failed()) {
    return nullptr;
  }

  doc->SetAllowDeclarativeShadowRoots(true);
  doc->SetDocumentURI(uri);

  nsCOMPtr<nsIScriptGlobalObject> scriptHandlingObject =
      do_QueryInterface(aGlobal.GetAsSupports());
  doc->SetScriptHandlingObject(scriptHandlingObject);
  doc->SetDocumentCharacterSet(UTF_8_ENCODING);

  return doc.forget();
}

/* static */
already_AddRefed<Document> Document::ParseHTMLUnsafe(
    GlobalObject& aGlobal, const TrustedHTMLOrString& aHTML,
    ErrorResult& aError) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  constexpr nsLiteralString sink = u"Document parseHTMLUnsafe"_ns;
  Maybe<nsAutoString> compliantStringHolder;
  const nsAString* compliantString =
      TrustedTypeUtils::GetTrustedTypesCompliantString(
          aHTML, sink, kTrustedTypesOnlySinkGroup, *global,
          compliantStringHolder, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  RefPtr<Document> doc = CreateHTMLDocument(aGlobal, false, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  aError = nsContentUtils::ParseDocumentHTML(*compliantString, doc, false);
  if (aError.Failed()) {
    return nullptr;
  }

  return doc.forget();
}

// https://wicg.github.io/sanitizer-api/#document-parsehtml
/* static */
already_AddRefed<Document> Document::ParseHTML(GlobalObject& aGlobal,
                                               const nsAString& aHTML,
                                               const SetHTMLOptions& aOptions,
                                               ErrorResult& aError) {
  // Step 1. Let document be a new Document, whose content type is "text/html".
  // Step 2. Set document’s allow declarative shadow roots to true.
  RefPtr<Document> doc = CreateHTMLDocument(aGlobal, true, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  // Step 3. Parse HTML from a string given document and html.
  // TODO(bug 1960845): Investigate the behavior around <noscript> with
  // parseHTML
  aError = nsContentUtils::ParseDocumentHTML(
      aHTML, doc, /* aScriptingEnabledForNoscriptParsing */ true);
  if (aError.Failed()) {
    return nullptr;
  }

  // Step 4. Let sanitizer be the result of calling get a sanitizer instance
  // from options with options and true.
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<Sanitizer> sanitizer =
      Sanitizer::GetInstance(global, aOptions.mSanitizer, true, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  // Step 5. Call sanitize on document’s root node with sanitizer and true.
  nsCOMPtr<nsINode> root = doc->GetRootElement();
  MOZ_DIAGNOSTIC_ASSERT(root, "HTML parser should have create the <html> root");
  sanitizer->Sanitize(root, /* aSafe */ true, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  // Step 6. Return document.
  return doc.forget();
}

bool Document::MutationEventsEnabled() {
  if (StaticPrefs::dom_mutation_events_enabled()) {
    return true;
  }
  if (mMutationEventsEnabled.isNothing()) {
    mMutationEventsEnabled.emplace(
        NodePrincipal()->IsURIInPrefList("dom.mutation_events.forceEnable"));
  }
  return mMutationEventsEnabled.value();
}

void Document::GetAllInProcessDocuments(
    nsTArray<RefPtr<Document>>& aAllDocuments) {
  for (Document* doc : AllDocumentsList()) {
    aAllDocuments.AppendElement(doc);
  }
}

}  // namespace mozilla::dom
