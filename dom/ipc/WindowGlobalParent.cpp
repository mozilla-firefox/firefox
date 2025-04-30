/* -*- Mode: C++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 8 -*- */
/* vim: set sw=2 ts=8 et tw=80 ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WindowGlobalParent.h"

#include <algorithm>

#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BounceTrackingStorageObserver.h"
#include "mozilla/BounceTrackingProtection.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ContentBlockingAllowList.h"
#include "mozilla/dom/InProcessParent.h"
#include "mozilla/dom/BrowserBridgeParent.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/BrowserHost.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/IdentityCredential.h"
#include "mozilla/dom/MediaController.h"
#include "mozilla/dom/NavigatorLogin.h"
#include "mozilla/dom/WebAuthnTransactionParent.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/ChromeUtils.h"
#include "mozilla/dom/UseCounterMetrics.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"
#include "mozilla/glean/DomMediaMetrics.h"
#include "mozilla/glean/DomUseCounterMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/glean/GeckoviewMetrics.h"
#include "mozilla/glean/CaptchadetectionMetrics.h"
#include "mozilla/Components.h"
#include "mozilla/IdentityCredentialRequestManager.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ServoCSSParser.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/DomSecurityMetrics.h"
#include "mozilla/Variant.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "MMPrinter.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsError.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsICookieManager.h"
#include "nsICookieService.h"
#include "nsQueryObject.h"
#include "nsNetUtil.h"
#include "nsSandboxFlags.h"
#include "nsSerializationHelper.h"
#include "nsIBrowser.h"
#include "nsIEffectiveTLDService.h"
#include "nsIHttpsOnlyModePermission.h"
#include "nsIPromptCollection.h"
#include "nsITimer.h"
#include "nsITransportSecurityInfo.h"
#include "nsISharePicker.h"
#include "nsIURIMutator.h"
#include "nsIWebProgressListener.h"
#include "nsScriptSecurityManager.h"
#include "nsIOService.h"

#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMExceptionBinding.h"

#include "mozilla/dom/JSActorService.h"
#include "mozilla/dom/JSWindowActorBinding.h"
#include "mozilla/dom/JSWindowActorParent.h"

#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/PCookieServiceParent.h"
#include "mozilla/net/CookieServiceParent.h"

#include "nsISessionStoreFunctions.h"
#include "nsIXPConnect.h"
#include "nsImportModule.h"
#include "nsIXULRuntime.h"

#include "mozilla/dom/PBackgroundSessionStorageCache.h"

using namespace mozilla::ipc;
using namespace mozilla::dom::ipc;

extern mozilla::LazyLogModule gSHIPBFCacheLog;
extern mozilla::LazyLogModule gUseCountersLog;

namespace mozilla::dom {

WindowGlobalParent::WindowGlobalParent(
    CanonicalBrowsingContext* aBrowsingContext, uint64_t aInnerWindowId,
    uint64_t aOuterWindowId, FieldValues&& aInit)
    : WindowContext(aBrowsingContext, aInnerWindowId, aOuterWindowId,
                    std::move(aInit)),
      mSandboxFlags(0),
      mDocumentHasLoaded(false),
      mDocumentHasUserInteracted(false),
      mBlockAllMixedContent(false),
      mUpgradeInsecureRequests(false) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(), "Parent process only");
}

already_AddRefed<WindowGlobalParent> WindowGlobalParent::CreateDisconnected(
    const WindowGlobalInit& aInit) {
  RefPtr<CanonicalBrowsingContext> browsingContext =
      CanonicalBrowsingContext::Get(aInit.context().mBrowsingContextId);
  if (NS_WARN_IF(!browsingContext)) {
    return nullptr;
  }

  RefPtr<WindowGlobalParent> wgp =
      GetByInnerWindowId(aInit.context().mInnerWindowId);
  MOZ_RELEASE_ASSERT(!wgp, "Creating duplicate WindowGlobalParent");

  FieldValues fields(aInit.context().mFields);
  wgp =
      new WindowGlobalParent(browsingContext, aInit.context().mInnerWindowId,
                             aInit.context().mOuterWindowId, std::move(fields));
  wgp->mDocumentPrincipal = aInit.principal();
  wgp->mDocumentURI = aInit.documentURI();
  wgp->mIsInitialDocument = Some(aInit.isInitialDocument());
  wgp->mBlockAllMixedContent = aInit.blockAllMixedContent();
  wgp->mUpgradeInsecureRequests = aInit.upgradeInsecureRequests();
  wgp->mSandboxFlags = aInit.sandboxFlags();
  wgp->mHttpsOnlyStatus = aInit.httpsOnlyStatus();
  wgp->mSecurityInfo = aInit.securityInfo();
  net::CookieJarSettings::Deserialize(aInit.cookieJarSettings(),
                                      getter_AddRefs(wgp->mCookieJarSettings));
  MOZ_RELEASE_ASSERT(wgp->mDocumentPrincipal, "Must have a valid principal");

  nsresult rv = wgp->SetDocumentStoragePrincipal(aInit.storagePrincipal());
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv),
                     "Must succeed in setting storage principal");

  return wgp.forget();
}

void WindowGlobalParent::Init() {
  MOZ_ASSERT(Manager(), "Should have a manager!");

  // Invoke our base class' `Init` method. This will register us in
  // `gWindowContexts`.
  WindowContext::Init();

  // Determine which content process the window global is coming from.
  dom::ContentParentId processId(0);
  ContentParent* cp = nullptr;
  if (!IsInProcess()) {
    cp = static_cast<ContentParent*>(Manager()->Manager());
    processId = cp->ChildID();

    // Ensure the content process has permissions for this principal.
    cp->TransmitPermissionsForPrincipal(mDocumentPrincipal);
  }

  MOZ_DIAGNOSTIC_ASSERT(
      !BrowsingContext()->GetParent() ||
          BrowsingContext()->GetEmbedderInnerWindowId(),
      "When creating a non-root WindowGlobalParent, the WindowGlobalParent "
      "for our embedder should've already been created.");

  // Ensure we have a document URI
  if (!mDocumentURI) {
    NS_NewURI(getter_AddRefs(mDocumentURI), "about:blank");
  }

  // NOTE: `cp` may be nullptr, but that's OK, we need to tell every other
  // process in our group in that case.
  IPCInitializer ipcinit = GetIPCInitializer();
  Group()->EachOtherParent(cp, [&](ContentParent* otherContent) {
    Unused << otherContent->SendCreateWindowContext(ipcinit);
  });

  if (!BrowsingContext()->IsDiscarded()) {
    MOZ_ALWAYS_SUCCEEDS(
        BrowsingContext()->SetCurrentInnerWindowId(InnerWindowId()));
  }

  if (BrowsingContext()->IsTopContent()) {
    // For top level sandboxed documents we need to create a new principal
    // from URI + OriginAttributes, since the document principal will be a
    // NullPrincipal. See Bug 1654546.
    if (mSandboxFlags & SANDBOXED_ORIGIN) {
      ContentBlockingAllowList::RecomputePrincipal(
          mDocumentURI, mDocumentPrincipal->OriginAttributesRef(),
          getter_AddRefs(mDocContentBlockingAllowListPrincipal));
    } else {
      ContentBlockingAllowList::ComputePrincipal(
          mDocumentPrincipal,
          getter_AddRefs(mDocContentBlockingAllowListPrincipal));
    }
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(ToSupports(this), "window-global-created", nullptr);
  }

  if (!BrowsingContext()->IsDiscarded() && ShouldTrackSiteOriginTelemetry()) {
    mOriginCounter.emplace();
    mOriginCounter->UpdateSiteOriginsFrom(this, /* aIncrease = */ true);
  }
}

void WindowGlobalParent::OriginCounter::UpdateSiteOriginsFrom(
    WindowGlobalParent* aParent, bool aIncrease) {
  MOZ_RELEASE_ASSERT(aParent);

  if (aParent->DocumentPrincipal()->GetIsContentPrincipal()) {
    nsAutoCString origin;
    aParent->DocumentPrincipal()->GetSiteOrigin(origin);

    if (aIncrease) {
      int32_t& count = mOriginMap.LookupOrInsert(origin);
      count += 1;
      mMaxOrigins = std::max(mMaxOrigins, mOriginMap.Count());
    } else if (auto entry = mOriginMap.Lookup(origin)) {
      entry.Data() -= 1;

      if (entry.Data() == 0) {
        entry.Remove();
      }
    }
  }
}

void WindowGlobalParent::OriginCounter::Accumulate() {
  mozilla::glean::geckoview::per_document_site_origins.AccumulateSingleSample(
      mMaxOrigins);

  mMaxOrigins = 0;
  mOriginMap.Clear();
}

/* static */
already_AddRefed<WindowGlobalParent> WindowGlobalParent::GetByInnerWindowId(
    uint64_t aInnerWindowId) {
  if (!XRE_IsParentProcess()) {
    return nullptr;
  }

  return WindowContext::GetById(aInnerWindowId).downcast<WindowGlobalParent>();
}

already_AddRefed<WindowGlobalChild> WindowGlobalParent::GetChildActor() {
  if (!CanSend()) {
    return nullptr;
  }
  IProtocol* otherSide = InProcessParent::ChildActorFor(this);
  return do_AddRef(static_cast<WindowGlobalChild*>(otherSide));
}

BrowserParent* WindowGlobalParent::GetBrowserParent() {
  if (IsInProcess() || !CanSend()) {
    return nullptr;
  }
  return static_cast<BrowserParent*>(Manager());
}

ContentParent* WindowGlobalParent::GetContentParent() {
  if (IsInProcess() || !CanSend()) {
    return nullptr;
  }
  return static_cast<ContentParent*>(Manager()->Manager());
}

already_AddRefed<nsFrameLoader> WindowGlobalParent::GetRootFrameLoader() {
  dom::BrowsingContext* top = BrowsingContext()->Top();

  RefPtr<nsFrameLoaderOwner> frameLoaderOwner =
      do_QueryObject(top->GetEmbedderElement());
  if (frameLoaderOwner) {
    return frameLoaderOwner->GetFrameLoader();
  }
  return nullptr;
}

uint64_t WindowGlobalParent::ContentParentId() {
  RefPtr<BrowserParent> browserParent = GetBrowserParent();
  return browserParent ? browserParent->Manager()->ChildID() : 0;
}

int32_t WindowGlobalParent::OsPid() {
  RefPtr<BrowserParent> browserParent = GetBrowserParent();
  return browserParent ? browserParent->Manager()->Pid() : -1;
}

// A WindowGlobalPaernt is the root in its process if it has no parent, or its
// embedder is in a different process.
bool WindowGlobalParent::IsProcessRoot() {
  if (!BrowsingContext()->GetParent()) {
    return true;
  }

  RefPtr<WindowGlobalParent> embedder =
      BrowsingContext()->GetEmbedderWindowGlobal();
  if (NS_WARN_IF(!embedder)) {
    return false;
  }

  return ContentParentId() != embedder->ContentParentId();
}

uint32_t WindowGlobalParent::ContentBlockingEvents() {
  return GetContentBlockingLog()->GetContentBlockingEventsInLog();
}

void WindowGlobalParent::GetContentBlockingLog(nsAString& aLog) {
  NS_ConvertUTF8toUTF16 log(GetContentBlockingLog()->Stringify());
  aLog.Assign(std::move(log));
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvLoadURI(
    const MaybeDiscarded<dom::BrowsingContext>& aTargetBC,
    nsDocShellLoadState* aLoadState, bool aSetNavigating) {
  if (aTargetBC.IsNullOrDiscarded()) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ParentIPC: Trying to send a message with dead or detached context"));
    return IPC_OK();
  }

  if (aLoadState->URI()->SchemeIs("javascript")) {
    return IPC_FAIL(this, "Illegal cross-process javascript: load attempt");
  }

  RefPtr<CanonicalBrowsingContext> targetBC = aTargetBC.get_canonical();

  // FIXME: For cross-process loads, we should double check CanAccess() for the
  // source browsing context in the parent process.

  if (targetBC->Group() != BrowsingContext()->Group()) {
    return IPC_FAIL(this, "Illegal cross-group BrowsingContext load");
  }

  // FIXME: We should really initiate the load in the parent before bouncing
  // back down to the child.

  targetBC->LoadURI(aLoadState, aSetNavigating);
  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvInternalLoad(
    nsDocShellLoadState* aLoadState) {
  if (!aLoadState->Target().IsEmpty() ||
      aLoadState->TargetBrowsingContext().IsNull()) {
    return IPC_FAIL(this, "must already be retargeted");
  }
  if (aLoadState->TargetBrowsingContext().IsDiscarded()) {
    MOZ_LOG(
        BrowsingContext::GetLog(), LogLevel::Debug,
        ("ParentIPC: Trying to send a message with dead or detached context"));
    return IPC_OK();
  }

  if (aLoadState->URI()->SchemeIs("javascript")) {
    return IPC_FAIL(this, "Illegal cross-process javascript: load attempt");
  }

  RefPtr<CanonicalBrowsingContext> targetBC =
      aLoadState->TargetBrowsingContext().get_canonical();

  // FIXME: For cross-process loads, we should double check CanAccess() for the
  // source browsing context in the parent process.

  if (targetBC->Group() != BrowsingContext()->Group()) {
    return IPC_FAIL(this, "Illegal cross-group BrowsingContext load");
  }

  // FIXME: We should really initiate the load in the parent before bouncing
  // back down to the child.

  targetBC->InternalLoad(aLoadState);
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvUpdateDocumentURI(NotNull<nsIURI*> aURI) {
  // XXX(nika): Assert that the URI change was one which makes sense (either
  // about:blank -> a real URI, or a legal push/popstate URI change):
  if (StaticPrefs::dom_security_setdocumenturi()) {
    nsAutoCString scheme;
    if (NS_FAILED(aURI->GetScheme(scheme))) {
      return IPC_FAIL(this, "Setting DocumentURI without scheme.");
    }

    nsCOMPtr<nsIIOService> ios = do_GetIOService();
    if (!ios) {
      return IPC_FAIL(this, "Cannot get IOService");
    }
    nsCOMPtr<nsIProtocolHandler> handler;
    ios->GetProtocolHandler(scheme.get(), getter_AddRefs(handler));
    if (!handler) {
      return IPC_FAIL(this, "Setting DocumentURI with unknown protocol.");
    }

    nsCOMPtr<nsIURI> principalURI = mDocumentPrincipal->GetURI();
    if (mDocumentPrincipal->GetIsNullPrincipal()) {
      nsCOMPtr<nsIPrincipal> precursor =
          mDocumentPrincipal->GetPrecursorPrincipal();
      if (precursor) {
        principalURI = precursor->GetURI();
      }
    }

    if (nsScriptSecurityManager::IsHttpOrHttpsAndCrossOrigin(principalURI,
                                                             aURI)) {
      return IPC_FAIL(this,
                      "Setting DocumentURI with a different Origin than "
                      "principal URI");
    }
  }

  mDocumentURI = aURI;
  return IPC_OK();
}

nsresult WindowGlobalParent::SetDocumentStoragePrincipal(
    nsIPrincipal* aNewDocumentStoragePrincipal) {
  if (mDocumentPrincipal->Equals(aNewDocumentStoragePrincipal)) {
    mDocumentStoragePrincipal = mDocumentPrincipal;
    return NS_OK;
  }

  // Compare originNoSuffix to ensure it's equal.
  nsCString noSuffix;
  nsresult rv = mDocumentPrincipal->GetOriginNoSuffix(noSuffix);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCString storageNoSuffix;
  rv = aNewDocumentStoragePrincipal->GetOriginNoSuffix(storageNoSuffix);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (noSuffix != storageNoSuffix) {
    return NS_ERROR_FAILURE;
  }

  if (!mDocumentPrincipal->OriginAttributesRef().EqualsIgnoringPartitionKey(
          aNewDocumentStoragePrincipal->OriginAttributesRef())) {
    return NS_ERROR_FAILURE;
  }

  mDocumentStoragePrincipal = aNewDocumentStoragePrincipal;
  return NS_OK;
}

IPCResult WindowGlobalParent::RecvUpdateDocumentPrincipal(
    nsIPrincipal* aNewDocumentPrincipal,
    nsIPrincipal* aNewDocumentStoragePrincipal) {
  if (!mDocumentPrincipal->Equals(aNewDocumentPrincipal)) {
    return IPC_FAIL(this,
                    "Trying to reuse WindowGlobalParent but the principal of "
                    "the new document does not match the old one");
  }
  mDocumentPrincipal = aNewDocumentPrincipal;

  if (NS_FAILED(SetDocumentStoragePrincipal(aNewDocumentStoragePrincipal))) {
    return IPC_FAIL(this,
                    "Trying to reuse WindowGlobalParent but the principal of "
                    "the new document does not match the storage principal");
  }

  return IPC_OK();
}
mozilla::ipc::IPCResult WindowGlobalParent::RecvUpdateDocumentTitle(
    const nsString& aTitle) {
  if (mDocumentTitle.isSome() && mDocumentTitle.value() == aTitle) {
    return IPC_OK();
  }

  mDocumentTitle = Some(aTitle);

  // Send a pagetitlechanged event only for changes to the title
  // for top-level frames.
  if (!BrowsingContext()->IsTop()) {
    return IPC_OK();
  }

  // Notify media controller in order to update its default metadata.
  if (BrowsingContext()->HasCreatedMediaController()) {
    BrowsingContext()->GetMediaController()->NotifyPageTitleChanged();
  }

  Element* frameElement = BrowsingContext()->GetEmbedderElement();
  if (!frameElement) {
    return IPC_OK();
  }

  AsyncEventDispatcher::RunDOMEventWhenSafe(
      *frameElement, u"pagetitlechanged"_ns, CanBubble::eYes,
      ChromeOnlyDispatch::eYes);

  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvUpdateHttpsOnlyStatus(
    uint32_t aHttpsOnlyStatus) {
  mHttpsOnlyStatus = aHttpsOnlyStatus;
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvUpdateDocumentHasLoaded(
    bool aDocumentHasLoaded) {
  mDocumentHasLoaded = aDocumentHasLoaded;
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvUpdateDocumentHasUserInteracted(
    bool aDocumentHasUserInteracted) {
  mDocumentHasUserInteracted = aDocumentHasUserInteracted;
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvUpdateSandboxFlags(uint32_t aSandboxFlags) {
  mSandboxFlags = aSandboxFlags;
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvUpdateDocumentCspSettings(
    bool aBlockAllMixedContent, bool aUpgradeInsecureRequests) {
  mBlockAllMixedContent = aBlockAllMixedContent;
  mUpgradeInsecureRequests = aUpgradeInsecureRequests;
  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvSetClientInfo(
    const IPCClientInfo& aIPCClientInfo) {
  mClientInfo = Some(ClientInfo(aIPCClientInfo));
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvDestroy() {
  // Make a copy so that we can avoid potential iterator invalidation when
  // calling the user-provided Destroy() methods.
  JSActorWillDestroy();

  if (CanSend()) {
    RefPtr<BrowserParent> browserParent = GetBrowserParent();
    if (!browserParent || !browserParent->IsDestroyed()) {
      Unused << Send__delete__(this);
    }
  }
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvRawMessage(
    const JSActorMessageMeta& aMeta, const Maybe<ClonedMessageData>& aData,
    const Maybe<ClonedMessageData>& aStack) {
  Maybe<StructuredCloneData> data;
  if (aData) {
    data.emplace();
    data->BorrowFromClonedMessageData(*aData);
  }
  Maybe<StructuredCloneData> stack;
  if (aStack) {
    stack.emplace();
    stack->BorrowFromClonedMessageData(*aStack);
  }
  MMPrinter::Print("WindowGlobalParent::RecvRawMessage", aMeta.actorName(),
                   aMeta.messageName(), aData);
  ReceiveRawMessage(aMeta, std::move(data), std::move(stack));
  return IPC_OK();
}

const nsACString& WindowGlobalParent::GetRemoteType() {
  if (RefPtr<BrowserParent> browserParent = GetBrowserParent()) {
    return browserParent->Manager()->GetRemoteType();
  }

  return NOT_REMOTE_TYPE;
}

void WindowGlobalParent::NotifyContentBlockingEvent(
    uint32_t aEvent, nsIRequest* aRequest, bool aBlocked,
    const nsACString& aTrackingOrigin,
    const nsTArray<nsCString>& aTrackingFullHashes,
    const Maybe<ContentBlockingNotifier::StorageAccessPermissionGrantedReason>&
        aReason,
    const Maybe<ContentBlockingNotifier::CanvasFingerprinter>&
        aCanvasFingerprinter,
    const Maybe<bool> aCanvasFingerprinterKnownText) {
  MOZ_ASSERT(NS_IsMainThread());
  DebugOnly<bool> isCookiesBlocked =
      aEvent == nsIWebProgressListener::STATE_COOKIES_BLOCKED_TRACKER ||
      aEvent == nsIWebProgressListener::STATE_COOKIES_BLOCKED_SOCIALTRACKER;
  MOZ_ASSERT_IF(aBlocked, aReason.isNothing());
  MOZ_ASSERT_IF(!isCookiesBlocked, aReason.isNothing());
  MOZ_ASSERT_IF(isCookiesBlocked && !aBlocked, aReason.isSome());
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());
  // TODO: temporarily remove this until we find the root case of Bug 1609144
  // MOZ_DIAGNOSTIC_ASSERT_IF(XRE_IsE10sParentProcess(), !IsInProcess());

  // Return early if this WindowGlobalParent is in process.
  if (IsInProcess()) {
    return;
  }

  Maybe<uint32_t> event = GetContentBlockingLog()->RecordLogParent(
      aTrackingOrigin, aEvent, aBlocked, aReason, aTrackingFullHashes,
      aCanvasFingerprinter, aCanvasFingerprinterKnownText);

  // Notify the OnContentBlockingEvent if necessary.
  if (event) {
    if (auto* webProgress = GetBrowsingContext()->GetWebProgress()) {
      webProgress->OnContentBlockingEvent(webProgress, aRequest, event.value());
    }
  }
}

already_AddRefed<JSWindowActorParent> WindowGlobalParent::GetActor(
    JSContext* aCx, const nsACString& aName, ErrorResult& aRv) {
  return JSActorManager::GetActor(aCx, aName, aRv)
      .downcast<JSWindowActorParent>();
}

already_AddRefed<JSWindowActorParent> WindowGlobalParent::GetExistingActor(
    const nsACString& aName) {
  return JSActorManager::GetExistingActor(aName)
      .downcast<JSWindowActorParent>();
}

already_AddRefed<JSActor> WindowGlobalParent::InitJSActor(
    JS::Handle<JSObject*> aMaybeActor, const nsACString& aName,
    ErrorResult& aRv) {
  RefPtr<JSWindowActorParent> actor;
  if (aMaybeActor.get()) {
    aRv = UNWRAP_OBJECT(JSWindowActorParent, aMaybeActor.get(), actor);
    if (aRv.Failed()) {
      return nullptr;
    }
  } else {
    actor = new JSWindowActorParent();
  }

  MOZ_RELEASE_ASSERT(!actor->GetManager(),
                     "mManager was already initialized once!");
  actor->Init(aName, this);
  return actor.forget();
}

bool WindowGlobalParent::IsCurrentGlobal() {
  if (mozilla::SessionHistoryInParent() && BrowsingContext() &&
      BrowsingContext()->IsInBFCache()) {
    return false;
  }

  return CanSend() && BrowsingContext()->GetCurrentWindowGlobal() == this;
}

bool WindowGlobalParent::IsActiveInTab() {
  if (!CanSend()) {
    return false;
  }

  CanonicalBrowsingContext* bc = BrowsingContext();
  if (!bc || bc->GetCurrentWindowGlobal() != this) {
    return false;
  }

  // We check the top BC so we don't need to worry about getting a stale value.
  // That may not be necessary.
  MOZ_ASSERT(bc->Top()->IsInBFCache() == bc->IsInBFCache(),
             "BFCache bit out of sync?");
  return bc->AncestorsAreCurrent() && !bc->Top()->IsInBFCache();
}

namespace {

class ShareHandler final : public PromiseNativeHandler {
 public:
  explicit ShareHandler(
      mozilla::dom::WindowGlobalParent::ShareResolver&& aResolver)
      : mResolver(std::move(aResolver)) {}

  NS_DECL_ISUPPORTS

 public:
  virtual void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override {
    mResolver(NS_OK);
  }

  virtual void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                                ErrorResult& aRv) override {
    if (NS_WARN_IF(!aValue.isObject())) {
      mResolver(NS_ERROR_FAILURE);
      return;
    }

    // nsresult is stored as Exception internally in Promise
    JS::Rooted<JSObject*> obj(aCx, &aValue.toObject());
    RefPtr<DOMException> unwrapped;
    nsresult rv = UNWRAP_OBJECT(DOMException, &obj, unwrapped);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      mResolver(NS_ERROR_FAILURE);
      return;
    }

    mResolver(unwrapped->GetResult());
  }

 private:
  ~ShareHandler() = default;

  mozilla::dom::WindowGlobalParent::ShareResolver mResolver;
};

NS_IMPL_ISUPPORTS0(ShareHandler)

}  // namespace

mozilla::ipc::IPCResult WindowGlobalParent::RecvGetContentBlockingEvents(
    WindowGlobalParent::GetContentBlockingEventsResolver&& aResolver) {
  uint32_t events = GetContentBlockingLog()->GetContentBlockingEventsInLog();
  aResolver(events);

  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvUpdateCookieJarSettings(
    const CookieJarSettingsArgs& aCookieJarSettingsArgs) {
  net::CookieJarSettings::Deserialize(aCookieJarSettingsArgs,
                                      getter_AddRefs(mCookieJarSettings));
  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvUpdateDocumentSecurityInfo(
    nsITransportSecurityInfo* aSecurityInfo) {
  mSecurityInfo = aSecurityInfo;
  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvShare(
    IPCWebShareData&& aData, WindowGlobalParent::ShareResolver&& aResolver) {
  // Widget Layer handoff...
  nsCOMPtr<nsISharePicker> sharePicker =
      do_GetService("@mozilla.org/sharepicker;1");
  if (!sharePicker) {
    aResolver(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return IPC_OK();
  }

  // Initialize the ShareWidget
  RefPtr<BrowserParent> parent = GetBrowserParent();
  nsCOMPtr<mozIDOMWindowProxy> openerWindow;
  if (parent) {
    openerWindow = parent->GetParentWindowOuter();
    if (!openerWindow) {
      aResolver(NS_ERROR_FAILURE);
      return IPC_OK();
    }
  }
  sharePicker->Init(openerWindow);

  // And finally share the data...
  RefPtr<Promise> promise;
  nsresult rv = sharePicker->Share(aData.title(), aData.text(), aData.url(),
                                   getter_AddRefs(promise));
  if (NS_FAILED(rv)) {
    aResolver(rv);
    return IPC_OK();
  }

  // Handler finally awaits response...
  RefPtr<ShareHandler> handler = new ShareHandler(std::move(aResolver));
  promise->AppendNativeHandler(handler);

  return IPC_OK();
}

namespace {

class CheckPermitUnloadRequest final : public PromiseNativeHandler,
                                       public nsITimerCallback {
 public:
  CheckPermitUnloadRequest(WindowGlobalParent* aWGP, bool aHasInProcessBlocker,
                           nsIDocumentViewer::PermitUnloadAction aAction,
                           std::function<void(bool)>&& aResolver)
      : mResolver(std::move(aResolver)),
        mWGP(aWGP),
        mAction(aAction),
        mFoundBlocker(aHasInProcessBlocker) {}

  void Run(ContentParent* aIgnoreProcess = nullptr, uint32_t aTimeout = 0) {
    MOZ_ASSERT(mState == State::UNINITIALIZED);
    mState = State::WAITING;

    RefPtr<CheckPermitUnloadRequest> self(this);

    AutoTArray<ContentParent*, 8> seen;
    if (aIgnoreProcess) {
      seen.AppendElement(aIgnoreProcess);
    }

    BrowsingContext* bc = mWGP->GetBrowsingContext();
    bc->PreOrderWalk([&](dom::BrowsingContext* aBC) {
      if (WindowGlobalParent* wgp =
              aBC->Canonical()->GetCurrentWindowGlobal()) {
        ContentParent* cp = wgp->GetContentParent();
        if (wgp->HasBeforeUnload() && !seen.ContainsSorted(cp)) {
          seen.InsertElementSorted(cp);
          mPendingRequests++;
          auto resolve = [self](bool blockNavigation) {
            if (blockNavigation) {
              self->mFoundBlocker = true;
            }
            self->ResolveRequest();
          };
          if (cp) {
            cp->SendDispatchBeforeUnloadToSubtree(
                bc, resolve, [self](auto) { self->ResolveRequest(); });
          } else {
            ContentChild::DispatchBeforeUnloadToSubtree(bc, resolve);
          }
        }
      }
    });

    if (mPendingRequests && aTimeout) {
      Unused << NS_NewTimerWithCallback(getter_AddRefs(mTimer), this, aTimeout,
                                        nsITimer::TYPE_ONE_SHOT);
    }

    CheckDoneWaiting();
  }

  void ResolveRequest() {
    mPendingRequests--;
    CheckDoneWaiting();
  }

  NS_IMETHODIMP Notify(nsITimer* aTimer) override {
    MOZ_ASSERT(aTimer == mTimer);
    if (mState == State::WAITING) {
      mState = State::TIMED_OUT;
      CheckDoneWaiting();
    }
    return NS_OK;
  }

  void CheckDoneWaiting() {
    // If we've found a blocker, we prompt immediately without waiting for
    // further responses. The user's response applies to the entire navigation
    // attempt, regardless of how many "beforeunload" listeners we call.
    if (mState != State::TIMED_OUT &&
        (mState != State::WAITING || (mPendingRequests && !mFoundBlocker))) {
      return;
    }

    mState = State::PROMPTING;

    // Clearing our reference to the timer will automatically cancel it if it's
    // still running.
    mTimer = nullptr;

    if (!mFoundBlocker) {
      SendReply(true);
      return;
    }

    auto action = mAction;
    if (StaticPrefs::dom_disable_beforeunload()) {
      action = nsIDocumentViewer::eDontPromptAndUnload;
    }
    if (action != nsIDocumentViewer::ePrompt) {
      SendReply(action == nsIDocumentViewer::eDontPromptAndUnload);
      return;
    }

    // Handle any failure in prompting by aborting the navigation. See comment
    // in nsDocumentViewer::PermitUnload for reasoning.
    auto cleanup = MakeScopeExit([&]() { SendReply(false); });

    if (nsCOMPtr<nsIPromptCollection> prompt =
            do_GetService("@mozilla.org/embedcomp/prompt-collection;1")) {
      RefPtr<Promise> promise;
      prompt->AsyncBeforeUnloadCheck(mWGP->GetBrowsingContext(),
                                     getter_AddRefs(promise));

      if (!promise) {
        return;
      }

      promise->AppendNativeHandler(this);
      cleanup.release();
    }
  }

  void SendReply(bool aAllow) {
    MOZ_ASSERT(mState != State::REPLIED);
    mResolver(aAllow);
    mState = State::REPLIED;
  }

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MOZ_ASSERT(mState == State::PROMPTING);

    SendReply(JS::ToBoolean(aValue));
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MOZ_ASSERT(mState == State::PROMPTING);

    SendReply(false);
  }

  NS_DECL_ISUPPORTS

 private:
  ~CheckPermitUnloadRequest() {
    // We may get here without having sent a reply if the promise we're waiting
    // on is destroyed without being resolved or rejected.
    if (mState != State::REPLIED) {
      SendReply(false);
    }
  }

  enum class State : uint8_t {
    UNINITIALIZED,
    WAITING,
    TIMED_OUT,
    PROMPTING,
    REPLIED,
  };

  std::function<void(bool)> mResolver;

  RefPtr<WindowGlobalParent> mWGP;
  nsCOMPtr<nsITimer> mTimer;

  uint32_t mPendingRequests = 0;

  nsIDocumentViewer::PermitUnloadAction mAction;

  State mState = State::UNINITIALIZED;

  bool mFoundBlocker = false;
};

NS_IMPL_ISUPPORTS(CheckPermitUnloadRequest, nsITimerCallback)

}  // namespace

mozilla::ipc::IPCResult WindowGlobalParent::RecvCheckPermitUnload(
    bool aHasInProcessBlocker, XPCOMPermitUnloadAction aAction,
    CheckPermitUnloadResolver&& aResolver) {
  if (!IsCurrentGlobal()) {
    aResolver(false);
    return IPC_OK();
  }

  auto request = MakeRefPtr<CheckPermitUnloadRequest>(
      this, aHasInProcessBlocker, aAction, std::move(aResolver));
  request->Run(/* aIgnoreProcess */ GetContentParent());

  return IPC_OK();
}

already_AddRefed<Promise> WindowGlobalParent::PermitUnload(
    PermitUnloadAction aAction, uint32_t aTimeout, mozilla::ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  auto request = MakeRefPtr<CheckPermitUnloadRequest>(
      this, /* aHasInProcessBlocker */ false,
      nsIDocumentViewer::PermitUnloadAction(aAction),
      [promise](bool aAllow) { promise->MaybeResolve(aAllow); });
  request->Run(/* aIgnoreProcess */ nullptr, aTimeout);

  return promise.forget();
}

void WindowGlobalParent::PermitUnload(std::function<void(bool)>&& aResolver) {
  RefPtr<CheckPermitUnloadRequest> request = new CheckPermitUnloadRequest(
      this, /* aHasInProcessBlocker */ false,
      nsIDocumentViewer::PermitUnloadAction::ePrompt, std::move(aResolver));
  request->Run();
}

already_AddRefed<mozilla::dom::Promise> WindowGlobalParent::DrawSnapshot(
    const DOMRect* aRect, double aScale, const nsACString& aBackgroundColor,
    bool aResetScrollPosition, mozilla::ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  nscolor color;
  if (NS_WARN_IF(!ServoCSSParser::ComputeColor(nullptr, NS_RGB(0, 0, 0),
                                               aBackgroundColor, &color,
                                               nullptr, nullptr))) {
    aRv = NS_ERROR_FAILURE;
    return nullptr;
  }

  gfx::CrossProcessPaintFlags flags =
      gfx::CrossProcessPaintFlags::UseHighQualityScaling;
  if (!aRect) {
    // If no explicit Rect was passed, we want the currently visible viewport.
    flags |= gfx::CrossProcessPaintFlags::DrawView;
  } else if (aResetScrollPosition) {
    flags |= gfx::CrossProcessPaintFlags::ResetScrollPosition;
  }

  if (!gfx::CrossProcessPaint::Start(this, aRect, (float)aScale, color, flags,
                                     promise)) {
    aRv = NS_ERROR_FAILURE;
    return nullptr;
  }
  return promise.forget();
}

void WindowGlobalParent::DrawSnapshotInternal(gfx::CrossProcessPaint* aPaint,
                                              const Maybe<IntRect>& aRect,
                                              float aScale,
                                              nscolor aBackgroundColor,
                                              uint32_t aFlags) {
  auto promise = SendDrawSnapshot(aRect, aScale, aBackgroundColor, aFlags);

  RefPtr<gfx::CrossProcessPaint> paint(aPaint);
  RefPtr<WindowGlobalParent> wgp(this);
  promise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [paint, wgp](PaintFragment&& aFragment) {
        paint->ReceiveFragment(wgp, std::move(aFragment));
      },
      [paint, wgp](ResponseRejectReason&& aReason) {
        paint->LostFragment(wgp);
      });
}

/**
 * Accumulated page use counter data for a given top-level content document.
 */
struct PageUseCounters {
  // The number of page use counter data messages we are still waiting for.
  uint32_t mWaiting = 0;

  // Whether we have received any page use counter data.
  bool mReceivedAny = false;

  // The accumulated page use counters.
  UseCounters mUseCounters;
};

mozilla::ipc::IPCResult WindowGlobalParent::RecvExpectPageUseCounters(
    const MaybeDiscarded<WindowContext>& aTop) {
  if (aTop.IsNull()) {
    return IPC_FAIL(this, "aTop must not be null");
  }

  MOZ_LOG(gUseCountersLog, LogLevel::Debug,
          ("Expect page use counters: WindowContext %" PRIu64 " -> %" PRIu64,
           InnerWindowId(), aTop.ContextId()));

  // We've been called to indicate that the document in our window intends
  // to send use counter data to accumulate towards the top-level document's
  // page use counters.  This causes us to wait for this window to go away
  // (in WindowGlobalParent::ActorDestroy) before reporting the page use
  // counters via Telemetry.
  RefPtr<WindowGlobalParent> page =
      static_cast<WindowGlobalParent*>(aTop.GetMaybeDiscarded());
  if (!page || page->mSentPageUseCounters) {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > too late, won't report page use counters for this straggler"));
    return IPC_OK();
  }

  if (mPageUseCountersWindow) {
    if (mPageUseCountersWindow != page) {
      return IPC_FAIL(this,
                      "ExpectPageUseCounters called on the same "
                      "WindowContext with a different aTop value");
    }

    // We can get called with the same aTop value more than once, e.g. for
    // initial about:blank documents and then subsequent "real" documents loaded
    // into the same window.  We must note each source window only once.
    return IPC_OK();
  }

  // Note that the top-level document must wait for one more window's use
  // counters before reporting via Telemetry.
  mPageUseCountersWindow = page;
  if (!page->mPageUseCounters) {
    page->mPageUseCounters = MakeUnique<PageUseCounters>();
  }
  ++page->mPageUseCounters->mWaiting;

  MOZ_LOG(
      gUseCountersLog, LogLevel::Debug,
      (" > top-level now waiting on %d\n", page->mPageUseCounters->mWaiting));

  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvAccumulatePageUseCounters(
    const UseCounters& aUseCounters) {
  // We've been called to accumulate use counter data into the page use counters
  // for the document in mPageUseCountersWindow.

  MOZ_LOG(
      gUseCountersLog, LogLevel::Debug,
      ("Accumulate page use counters: WindowContext %" PRIu64 " -> %" PRIu64,
       InnerWindowId(),
       mPageUseCountersWindow ? mPageUseCountersWindow->InnerWindowId() : 0));

  if (!mPageUseCountersWindow || mPageUseCountersWindow->mSentPageUseCounters) {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > too late, won't report page use counters for this straggler"));
    return IPC_OK();
  }

  MOZ_ASSERT(mPageUseCountersWindow->mPageUseCounters);
  MOZ_ASSERT(mPageUseCountersWindow->mPageUseCounters->mWaiting > 0);

  mPageUseCountersWindow->mPageUseCounters->mUseCounters |= aUseCounters;
  mPageUseCountersWindow->mPageUseCounters->mReceivedAny = true;
  return IPC_OK();
}

// This is called on the top-level WindowGlobal, i.e. the one that is
// accumulating the page use counters, not the (potentially descendant) window
// that has finished providing use counter data.
void WindowGlobalParent::FinishAccumulatingPageUseCounters() {
  MOZ_LOG(gUseCountersLog, LogLevel::Debug,
          ("Stop expecting page use counters: -> WindowContext %" PRIu64,
           InnerWindowId()));

  if (!mPageUseCounters) {
    MOZ_ASSERT_UNREACHABLE("Not expecting page use counter data");
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > not expecting page use counter data"));
    return;
  }

  MOZ_ASSERT(mPageUseCounters->mWaiting > 0);
  --mPageUseCounters->mWaiting;

  if (mPageUseCounters->mWaiting > 0) {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > now waiting on %d", mPageUseCounters->mWaiting));
    return;
  }

  if (mPageUseCounters->mReceivedAny) {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > reporting [%s]",
             nsContentUtils::TruncatedURLForDisplay(mDocumentURI).get()));

    Maybe<nsCString> urlForLogging;
    const bool dumpCounters = StaticPrefs::dom_use_counters_dump_page();
    if (dumpCounters) {
      urlForLogging.emplace(
          nsContentUtils::TruncatedURLForDisplay(mDocumentURI));
    }

    glean::use_counter::top_level_content_documents_destroyed.Add();
    if (CanonicalBrowsingContext* bc = BrowsingContext()) {
      if (bc->UsePrivateBrowsing()) {
        glean::captcha_detection::pages_visited_pbm.Add();
      } else {
        glean::captcha_detection::pages_visited.Add();
      }
    }

    bool any = false;
    for (int32_t c = 0; c < eUseCounter_Count; ++c) {
      auto uc = static_cast<UseCounter>(c);
      if (!mPageUseCounters->mUseCounters[uc]) {
        continue;
      }
      any = true;
      const char* metricName = IncrementUseCounter(uc, /* aIsPage = */ true);
      if (dumpCounters) {
        printf_stderr("USE_COUNTER_PAGE: %s - %s\n", metricName,
                      urlForLogging->get());
      }
    }

    if (!any) {
      MOZ_LOG(gUseCountersLog, LogLevel::Debug,
              (" > page use counter data was received, but was empty"));
    }
  } else {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > no page use counter data was received"));
  }

  mSentPageUseCounters = true;
  mPageUseCounters = nullptr;
}

Element* WindowGlobalParent::GetRootOwnerElement() {
  WindowGlobalParent* top = TopWindowContext();
  if (!top) {
    return nullptr;
  }

  if (IsInProcess()) {
    return top->BrowsingContext()->GetEmbedderElement();
  }

  if (BrowserParent* parent = top->GetBrowserParent()) {
    return parent->GetOwnerElement();
  }

  return nullptr;
}

void WindowGlobalParent::NotifySessionStoreUpdatesComplete(Element* aEmbedder) {
  if (!aEmbedder) {
    aEmbedder = GetRootOwnerElement();
  }
  if (aEmbedder) {
    if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
      obs->NotifyWhenScriptSafe(ToSupports(aEmbedder),
                                "browser-shutdown-tabstate-updated", nullptr);
    }
  }
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvRequestRestoreTabContent() {
  CanonicalBrowsingContext* bc = BrowsingContext();
  if (bc && bc->AncestorsAreCurrent()) {
    bc->Top()->RequestRestoreTabContent(this);
  }
  return IPC_OK();
}

nsCString BFCacheStatusToString(uint32_t aFlags) {
  if (aFlags == 0) {
    return "0"_ns;
  }

  nsCString flags;
#define ADD_BFCACHESTATUS_TO_STRING(_flag) \
  if (aFlags & BFCacheStatus::_flag) {     \
    if (!flags.IsEmpty()) {                \
      flags.Append('|');                   \
    }                                      \
    flags.AppendLiteral(#_flag);           \
    aFlags &= ~BFCacheStatus::_flag;       \
  }

  ADD_BFCACHESTATUS_TO_STRING(NOT_ALLOWED);
  ADD_BFCACHESTATUS_TO_STRING(EVENT_HANDLING_SUPPRESSED);
  ADD_BFCACHESTATUS_TO_STRING(SUSPENDED);
  ADD_BFCACHESTATUS_TO_STRING(UNLOAD_LISTENER);
  ADD_BFCACHESTATUS_TO_STRING(REQUEST);
  ADD_BFCACHESTATUS_TO_STRING(ACTIVE_GET_USER_MEDIA);
  ADD_BFCACHESTATUS_TO_STRING(ACTIVE_PEER_CONNECTION);
  ADD_BFCACHESTATUS_TO_STRING(CONTAINS_EME_CONTENT);
  ADD_BFCACHESTATUS_TO_STRING(CONTAINS_MSE_CONTENT);
  ADD_BFCACHESTATUS_TO_STRING(HAS_ACTIVE_SPEECH_SYNTHESIS);
  ADD_BFCACHESTATUS_TO_STRING(HAS_USED_VR);
  ADD_BFCACHESTATUS_TO_STRING(CONTAINS_REMOTE_SUBFRAMES);
  ADD_BFCACHESTATUS_TO_STRING(NOT_ONLY_TOPLEVEL_IN_BCG);
  ADD_BFCACHESTATUS_TO_STRING(BEFOREUNLOAD_LISTENER);
  ADD_BFCACHESTATUS_TO_STRING(ACTIVE_LOCK);
  ADD_BFCACHESTATUS_TO_STRING(PAGE_LOADING);

#undef ADD_BFCACHESTATUS_TO_STRING

  MOZ_ASSERT(aFlags == 0,
             "Missing stringification for enum value in BFCacheStatus.");
  return flags;
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvUpdateBFCacheStatus(
    const uint32_t& aOnFlags, const uint32_t& aOffFlags) {
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Debug))) {
    nsAutoCString uri("[no uri]");
    if (mDocumentURI) {
      uri = mDocumentURI->GetSpecOrDefault();
    }
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
            ("Setting BFCache flags for %s +(%s) -(%s)", uri.get(),
             BFCacheStatusToString(aOnFlags).get(),
             BFCacheStatusToString(aOffFlags).get()));
  }
  mBFCacheStatus |= aOnFlags;
  mBFCacheStatus &= ~aOffFlags;
  return IPC_OK();
}

mozilla::ipc::IPCResult
WindowGlobalParent::RecvUpdateActivePeerConnectionStatus(bool aIsAdded) {
  if (aIsAdded) {
    RecvUpdateBFCacheStatus(BFCacheStatus::ACTIVE_PEER_CONNECTION, 0);
  } else {
    RecvUpdateBFCacheStatus(0, BFCacheStatus::ACTIVE_PEER_CONNECTION);
  }

  if (WindowGlobalParent* top = TopWindowContext()) {
    CheckedUint32 newValue(top->mNumOfProcessesWithActivePeerConnections);
    if (aIsAdded) {
      ++newValue;
    } else {
      --newValue;
    }
    if (!newValue.isValid()) {
      return IPC_FAIL(this,
                      "mNumOfProcessesWithActivePeerConnections overflowed");
    }

    top->mNumOfProcessesWithActivePeerConnections = newValue.value();
    Unused << top->SetHasActivePeerConnections(newValue.value() > 0);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvSetSingleChannelId(
    const Maybe<uint64_t>& aSingleChannelId) {
  mSingleChannelId = aSingleChannelId;
  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvSetDocumentDomain(
    NotNull<nsIURI*> aDomain) {
  if (mSandboxFlags & SANDBOXED_DOMAIN) {
    // We're sandboxed; disallow setting domain
    return IPC_FAIL(this, "Sandbox disallows domain setting.");
  }

  // Might need to do a featurepolicy check here, like we currently do in the
  // child process?

  nsCOMPtr<nsIURI> uri;
  mDocumentPrincipal->GetDomain(getter_AddRefs(uri));
  if (!uri) {
    uri = mDocumentPrincipal->GetURI();
    if (!uri) {
      return IPC_OK();
    }
  }

  if (!Document::IsValidDomain(uri, aDomain)) {
    // Error: illegal domain
    return IPC_FAIL(
        this, "Setting domain that's not a suffix of existing domain value.");
  }

  if (Group()->IsPotentiallyCrossOriginIsolated()) {
    return IPC_FAIL(this, "Setting domain in a cross-origin isolated BC.");
  }

  mDocumentPrincipal->SetDomain(aDomain);
  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvReloadWithHttpsOnlyException() {
  nsresult rv;
  nsCOMPtr<nsIURI> currentURI = BrowsingContext()->Top()->GetCurrentURI();

  if (!currentURI) {
    return IPC_FAIL(this, "HTTPS-only mode: Failed to get current URI");
  }

  bool isViewSource = currentURI->SchemeIs("view-source");

  nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(currentURI);
  nsCOMPtr<nsIURI> innerURI;
  if (isViewSource) {
    nestedURI->GetInnerURI(getter_AddRefs(innerURI));
  } else {
    innerURI = currentURI;
  }

  if (!net::SchemeIsHttpOrHttps(innerURI)) {
    return IPC_FAIL(this, "HTTPS-only mode: Illegal state");
  }

  // We replace the scheme with http, because the user wants to unbreak the
  // whole page.
  nsCOMPtr<nsIURI> newURI;
  Unused << NS_MutateURI(innerURI).SetScheme("http"_ns).Finalize(
      getter_AddRefs(newURI));

  OriginAttributes originAttributes =
      TopWindowContext()->DocumentPrincipal()->OriginAttributesRef();

  originAttributes.SetFirstPartyDomain(true, newURI);

  nsCOMPtr<nsIPermissionManager> permMgr =
      components::PermissionManager::Service();
  if (!permMgr) {
    return IPC_FAIL(
        this, "HTTPS-only mode: Failed to get Permission Manager service");
  }

  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(newURI, originAttributes);

  rv = permMgr->AddFromPrincipal(
      principal, "https-only-load-insecure"_ns,
      nsIHttpsOnlyModePermission::LOAD_INSECURE_ALLOW_SESSION,
      nsIPermissionManager::EXPIRE_SESSION, 0);

  if (NS_FAILED(rv)) {
    return IPC_FAIL(
        this, "HTTPS-only mode: Failed to add permission to the principal");
  }

  nsCOMPtr<nsIURI> insecureURI = newURI;
  if (isViewSource) {
    nsAutoCString spec;
    MOZ_ALWAYS_SUCCEEDS(newURI->GetSpec(spec));
    if (NS_FAILED(
            NS_NewURI(getter_AddRefs(insecureURI), "view-source:"_ns + spec))) {
      return IPC_FAIL(
          this, "HTTPS-only mode: Failed to re-construct view-source URI");
    }
  }

  RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(insecureURI);
  loadState->SetTriggeringPrincipal(nsContentUtils::GetSystemPrincipal());
  loadState->SetLoadType(LOAD_NORMAL_REPLACE);
  loadState->SetHttpsUpgradeTelemetry(
      nsILoadInfo::HTTPS_ONLY_UPGRADE_DOWNGRADE);

  RefPtr<CanonicalBrowsingContext> topBC = BrowsingContext()->Top();
  topBC->LoadURI(loadState, /* setNavigating */ true);

  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvGetIdentityCredential(
    const IdentityCredentialRequestOptions& aOptions,
    const CredentialMediationRequirement& aMediationRequirement,
    const GetIdentityCredentialResolver& aResolver) {
  IdentityCredential::GetCredentialInMainProcess(
      DocumentPrincipal(), this->BrowsingContext(), aOptions,
      aMediationRequirement)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const IPCIdentityCredential& aResult) {
            return aResolver({Some(aResult), NS_OK});
          },
          [aResolver](nsresult aErr) {
            aResolver({Maybe<IPCIdentityCredential>(Nothing()), aErr});
          });
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvStoreIdentityCredential(
    const IPCIdentityCredential& aCredential,
    const StoreIdentityCredentialResolver& aResolver) {
  IdentityCredential::StoreInMainProcess(DocumentPrincipal(), aCredential)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const bool& aResult) { aResolver(NS_OK); },
          [aResolver](nsresult aErr) { aResolver(aErr); });
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvDisconnectIdentityCredential(
    const IdentityCredentialDisconnectOptions& aOptions,
    const DisconnectIdentityCredentialResolver& aResolver) {
  IdentityCredential::DisconnectInMainProcess(DocumentPrincipal(), aOptions)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const bool& aResult) { aResolver(NS_OK); },
          [aResolver](nsresult aErr) { aResolver(aErr); });
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvPreventSilentAccess(
    const PreventSilentAccessResolver& aResolver) {
  nsIPrincipal* principal = DocumentPrincipal();
  if (principal) {
    nsCOMPtr<nsIPermissionManager> permissionManager =
        components::PermissionManager::Service();
    if (permissionManager) {
      permissionManager->RemoveFromPrincipal(
          principal, "credential-allow-silent-access"_ns);
      aResolver(NS_OK);
      return IPC_OK();
    }
  }

  aResolver(NS_ERROR_NOT_AVAILABLE);
  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvSetLoginStatus(
    LoginStatus aStatus, const SetLoginStatusResolver& aResolver) {
  nsIPrincipal* principal = DocumentPrincipal();
  if (!principal) {
    aResolver(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return IPC_OK();
  }
  nsresult rv = NavigatorLogin::SetLoginStatus(principal, aStatus);
  aResolver(rv);
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvGetStorageAccessPermission(
    bool aIncludeIdentityCredential,
    GetStorageAccessPermissionResolver&& aResolve) {
  WindowGlobalParent* top = TopWindowContext();
  if (!top) {
    return IPC_FAIL_NO_REASON(this);
  }
  nsIPrincipal* topPrincipal = top->DocumentPrincipal();
  nsIPrincipal* principal = DocumentPrincipal();
  uint32_t result;
  nsresult rv = AntiTrackingUtils::TestStoragePermissionInParent(
      topPrincipal, principal, &result);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aResolve(nsIPermissionManager::UNKNOWN_ACTION);
    return IPC_OK();
  }
  if (result == nsIPermissionManager::ALLOW_ACTION) {
    aResolve(nsIPermissionManager::ALLOW_ACTION);
    return IPC_OK();
  }

  if (aIncludeIdentityCredential) {
    bool canCollect;
    rv = IdentityCredential::CanSilentlyCollect(topPrincipal, principal,
                                                &canCollect);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aResolve(nsIPermissionManager::UNKNOWN_ACTION);
      return IPC_OK();
    }
    if (canCollect) {
      aResolve(nsIPermissionManager::ALLOW_ACTION);
      return IPC_OK();
    }
  }

  aResolve(result);
  return IPC_OK();
}

void WindowGlobalParent::ActorDestroy(ActorDestroyReason aWhy) {
  if (GetBrowsingContext()->IsTopContent()) {
    glean::orb::did_ever_block_response
        .EnumGet(mShouldReportHasBlockedOpaqueResponse
                     ? glean::orb::DidEverBlockResponseLabel::eTrue
                     : glean::orb::DidEverBlockResponseLabel::eFalse)
        .Add();
  }

  if (mPageUseCountersWindow) {
    mPageUseCountersWindow->FinishAccumulatingPageUseCounters();
    mPageUseCountersWindow = nullptr;
  }

  if (GetBrowsingContext()->IsTopContent() &&
      !mDocumentPrincipal->SchemeIs("about")) {
    // Record the page load
    uint32_t pageLoaded = 1;
    glean::mixed_content::unblock_counter.AccumulateSingleSample(pageLoaded);

    // Record the mixed content status of the docshell in Telemetry
    enum {
      NO_MIXED_CONTENT = 0,  // There is no Mixed Content on the page
      MIXED_DISPLAY_CONTENT =
          1,  // The page attempted to load Mixed Display Content
      MIXED_ACTIVE_CONTENT =
          2,  // The page attempted to load Mixed Active Content
      MIXED_DISPLAY_AND_ACTIVE_CONTENT = 3  // The page attempted to load Mixed
                                            // Display & Mixed Active Content
    };

    bool hasMixedDisplay =
        mSecurityState &
        (nsIWebProgressListener::STATE_LOADED_MIXED_DISPLAY_CONTENT |
         nsIWebProgressListener::STATE_BLOCKED_MIXED_DISPLAY_CONTENT);
    bool hasMixedActive =
        mSecurityState &
        (nsIWebProgressListener::STATE_LOADED_MIXED_ACTIVE_CONTENT |
         nsIWebProgressListener::STATE_BLOCKED_MIXED_ACTIVE_CONTENT);

    uint32_t mixedContentLevel = NO_MIXED_CONTENT;
    if (hasMixedDisplay && hasMixedActive) {
      mixedContentLevel = MIXED_DISPLAY_AND_ACTIVE_CONTENT;
    } else if (hasMixedActive) {
      mixedContentLevel = MIXED_ACTIVE_CONTENT;
    } else if (hasMixedDisplay) {
      mixedContentLevel = MIXED_DISPLAY_CONTENT;
    }
    glean::mixed_content::page_load.AccumulateSingleSample(mixedContentLevel);

    if (GetDocTreeHadMedia()) {
      glean::media::element_in_page_count.Add(1);
    }
  }

  ContentParent* cp = nullptr;
  if (!IsInProcess()) {
    cp = static_cast<ContentParent*>(Manager()->Manager());
  }

  Group()->EachOtherParent(cp, [&](ContentParent* otherContent) {
    // Keep the WindowContext and our BrowsingContextGroup alive until other
    // processes have acknowledged it has been discarded.
    Group()->AddKeepAlive();
    auto callback = [self = RefPtr{this}](auto) {
      self->Group()->RemoveKeepAlive();
    };
    otherContent->SendDiscardWindowContext(InnerWindowId(), callback, callback);
  });

  // Note that our WindowContext has become discarded.
  WindowContext::Discard();

  // Report content blocking log when destroyed.
  // There shouldn't have any content blocking log when a document is loaded in
  // the parent process(See NotifyContentBlockingEvent), so we could skip
  // reporting log when it is in-process.
  if (!IsInProcess()) {
    RefPtr<BrowserParent> browserParent =
        static_cast<BrowserParent*>(Manager());
    if (browserParent) {
      nsCOMPtr<nsILoadContext> loadContext = browserParent->GetLoadContext();
      if (loadContext && !loadContext->UsePrivateBrowsing() &&
          BrowsingContext()->IsTopContent()) {
        GetContentBlockingLog()->ReportLog();

        if (mDocumentURI && net::SchemeIsHttpOrHttps(mDocumentURI)) {
          GetContentBlockingLog()->ReportCanvasFingerprintingLog(
              DocumentPrincipal());
          GetContentBlockingLog()->ReportFontFingerprintingLog(
              DocumentPrincipal());
          GetContentBlockingLog()->ReportEmailTrackingLog(DocumentPrincipal());
        }
      }
    }
  }

  // Destroy our JSWindowActors, and reject any pending queries.
  JSActorDidDestroy();

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(ToSupports(this), "window-global-destroyed", nullptr);
  }

  if (mOriginCounter) {
    mOriginCounter->Accumulate();
  }
}

WindowGlobalParent::~WindowGlobalParent() = default;

JSObject* WindowGlobalParent::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return WindowGlobalParent_Binding::Wrap(aCx, this, aGivenProto);
}

nsIGlobalObject* WindowGlobalParent::GetParentObject() {
  return xpc::NativeGlobal(xpc::PrivilegedJunkScope());
}

nsIDOMProcessParent* WindowGlobalParent::GetDomProcess() {
  if (RefPtr<BrowserParent> browserParent = GetBrowserParent()) {
    return browserParent->Manager();
  }
  return InProcessParent::Singleton();
}

void WindowGlobalParent::DidBecomeCurrentWindowGlobal(bool aCurrent) {
  WindowGlobalParent* top = BrowsingContext()->GetTopWindowContext();
  if (top && top->mOriginCounter) {
    top->mOriginCounter->UpdateSiteOriginsFrom(this,
                                               /* aIncrease = */ aCurrent);
  }

  if (!aCurrent && Fullscreen()) {
    ExitTopChromeDocumentFullscreen();
  }
}

bool WindowGlobalParent::ShouldTrackSiteOriginTelemetry() {
  CanonicalBrowsingContext* bc = BrowsingContext();

  if (!bc->IsTopContent()) {
    return false;
  }

  RefPtr<BrowserParent> browserParent = GetBrowserParent();
  if (!browserParent ||
      !IsWebRemoteType(browserParent->Manager()->GetRemoteType())) {
    return false;
  }

  return DocumentPrincipal()->GetIsContentPrincipal();
}

void WindowGlobalParent::AddSecurityState(uint32_t aStateFlags) {
  MOZ_ASSERT(TopWindowContext() == this);
  MOZ_ASSERT((aStateFlags &
              (nsIWebProgressListener::STATE_LOADED_MIXED_DISPLAY_CONTENT |
               nsIWebProgressListener::STATE_LOADED_MIXED_ACTIVE_CONTENT |
               nsIWebProgressListener::STATE_BLOCKED_MIXED_DISPLAY_CONTENT |
               nsIWebProgressListener::STATE_BLOCKED_MIXED_ACTIVE_CONTENT |
               nsIWebProgressListener::STATE_HTTPS_ONLY_MODE_UPGRADED |
               nsIWebProgressListener::STATE_HTTPS_ONLY_MODE_UPGRADE_FAILED |
               nsIWebProgressListener::STATE_HTTPS_ONLY_MODE_UPGRADED_FIRST)) ==
                 aStateFlags,
             "Invalid flags specified!");

  if ((mSecurityState & aStateFlags) == aStateFlags) {
    return;
  }

  mSecurityState |= aStateFlags;

  if (GetBrowsingContext()->GetCurrentWindowGlobal() == this) {
    GetBrowsingContext()->UpdateSecurityState();
  }
}

bool WindowGlobalParent::HasActivePeerConnections() {
  MOZ_ASSERT(TopWindowContext() == this,
             "mNumOfProcessesWithActivePeerConnections is set only "
             "in the top window context");
  return mNumOfProcessesWithActivePeerConnections > 0;
}

void WindowGlobalParent::ExitTopChromeDocumentFullscreen() {
  RefPtr<CanonicalBrowsingContext> chromeTop =
      BrowsingContext()->TopCrossChromeBoundary();
  if (Document* chromeDoc = chromeTop->GetDocument()) {
    Document::ClearPendingFullscreenRequests(chromeDoc);
    if (chromeDoc->Fullscreen()) {
      // This only clears the DOM fullscreen, will not exit from browser UI
      // fullscreen mode.
      Document::AsyncExitFullscreen(chromeDoc);
    }
  }
}

void WindowGlobalParent::SetShouldReportHasBlockedOpaqueResponse(
    nsContentPolicyType aContentPolicy) {
  // It's always okay to block TYPE_BEACON, TYPE_PING and TYPE_CSP_REPORT in
  // the parent process because content processes can do nothing to their
  // responses. Hence excluding them from the telemetry as blocking
  // them have no webcompat concerns.
  if (aContentPolicy != nsIContentPolicy::TYPE_BEACON &&
      aContentPolicy != nsIContentPolicy::TYPE_PING &&
      aContentPolicy != nsIContentPolicy::TYPE_CSP_REPORT) {
    if (IsTop()) {
      mShouldReportHasBlockedOpaqueResponse = true;
    }
  }
}

IPCResult WindowGlobalParent::RecvSetCookies(
    const nsCString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    nsIURI* aHost, bool aFromHttp, bool aIsThirdParty,
    const nsTArray<CookieStruct>& aCookies) {
  // Get CookieServiceParent via
  // ContentParent->NeckoParent->CookieServiceParent.
  ContentParent* contentParent = GetContentParent();
  NS_ENSURE_TRUE(contentParent, IPC_OK());

  net::PNeckoParent* neckoParent =
      LoneManagedOrNullAsserts(contentParent->ManagedPNeckoParent());
  NS_ENSURE_TRUE(neckoParent, IPC_OK());
  net::PCookieServiceParent* csParent =
      LoneManagedOrNullAsserts(neckoParent->ManagedPCookieServiceParent());
  NS_ENSURE_TRUE(csParent, IPC_OK());
  auto* cs = static_cast<net::CookieServiceParent*>(csParent);

  return cs->SetCookies(aBaseDomain, aOriginAttributes, aHost, aFromHttp,
                        aIsThirdParty, aCookies, GetBrowsingContext());
}

IPCResult WindowGlobalParent::RecvOnInitialStorageAccess() {
  DebugOnly<nsresult> rv =
      BounceTrackingStorageObserver::OnInitialStorageAccess(this);
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to notify storage access");
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvRecordUserActivationForBTP() {
  WindowGlobalParent* top = TopWindowContext();
  if (!top) {
    return IPC_OK();
  }
  nsIPrincipal* principal = top->DocumentPrincipal();
  if (!principal) {
    return IPC_OK();
  }

  DebugOnly<nsresult> rv = BounceTrackingProtection::RecordUserActivation(
      principal, Some(PR_Now()), top->BrowsingContext());
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                       "Failed to record BTP user activation.");

  return IPC_OK();
}

already_AddRefed<PWebAuthnTransactionParent>
WindowGlobalParent::AllocPWebAuthnTransactionParent() {
  return MakeAndAddRef<WebAuthnTransactionParent>();
}

NS_IMPL_CYCLE_COLLECTION_CLASS(WindowGlobalParent)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(WindowGlobalParent,
                                                WindowContext)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPageUseCountersWindow)
  tmp->UnlinkManager();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(WindowGlobalParent,
                                                  WindowContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPageUseCountersWindow)
  if (!tmp->IsInProcess()) {
    CycleCollectionNoteChild(cb, static_cast<BrowserParent*>(tmp->Manager()),
                             "Manager()");
  }
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(WindowGlobalParent,
                                               WindowContext)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WindowGlobalParent)
NS_INTERFACE_MAP_END_INHERITING(WindowContext)

NS_IMPL_ADDREF_INHERITED(WindowGlobalParent, WindowContext)
NS_IMPL_RELEASE_INHERITED(WindowGlobalParent, WindowContext)

}  // namespace mozilla::dom
