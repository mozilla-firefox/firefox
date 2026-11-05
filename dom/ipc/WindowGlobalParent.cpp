/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WindowGlobalParent.h"

#include <algorithm>

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessibleParent.h"
#  include "mozilla/a11y/Platform.h"
#  include "nsAccessibilityService.h"
#  if defined(XP_WIN)
#    include "mozilla/a11y/nsWinUtils.h"
#  endif
#endif
#include "MMPrinter.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/BounceTrackingProtection.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ContentBlockingAllowList.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/IdentityCredentialRequestManager.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ServoCSSParser.h"
#include "mozilla/ServoStyleSet.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/BrowserBridgeParent.h"
#include "mozilla/dom/BrowserHost.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ChromeUtils.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ClientValidation.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMExceptionBinding.h"
#include "mozilla/dom/DigitalCredential.h"
#include "mozilla/dom/DigitalCredentialParent.h"
#include "mozilla/dom/ElementBinding.h"
#include "mozilla/dom/IdentityCredential.h"
#include "mozilla/dom/InProcessParent.h"
#include "mozilla/dom/JSActorService.h"
#include "mozilla/dom/JSWindowActorBinding.h"
#include "mozilla/dom/JSWindowActorParent.h"
#include "mozilla/dom/MediaController.h"
#include "mozilla/dom/Navigation.h"
#include "mozilla/dom/NavigatorLogin.h"
#include "mozilla/dom/PBackgroundSessionStorageCache.h"
#include "mozilla/dom/ParentProcessChannelHandle.h"
#include "mozilla/dom/PrefetchLog.h"
#include "mozilla/dom/PrefetchMatchWaiter.h"
#include "mozilla/dom/PrefetchRecordParent.h"
#include "mozilla/dom/SerialManagerParent.h"
#include "mozilla/dom/UseCounterMetrics.h"
#include "mozilla/dom/WebAuthnTransactionParent.h"
#include "mozilla/dom/WebIdentityParent.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/dom/ipc/StructuredCloneData.h"
#include "mozilla/glean/CaptchadetectionMetrics.h"
#include "mozilla/glean/DomMediaMetrics.h"
#include "mozilla/glean/DomSecurityMetrics.h"
#include "mozilla/glean/DomUseCounterMetrics.h"
#include "mozilla/glean/GeckoviewMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/net/CookieCommons.h"
#include "mozilla/net/CookieServiceParent.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/PCookieServiceParent.h"
#include "nsContentUtils.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsError.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsIBrowser.h"
#include "nsICertOverrideService.h"
#include "nsICookieManager.h"
#include "nsICookieService.h"
#include "nsIEffectiveTLDService.h"
#include "nsIHttpsOnlyModePermission.h"
#include "nsIOService.h"
#include "nsIPromptCollection.h"
#include "nsISessionStoreFunctions.h"
#include "nsISharePicker.h"
#include "nsISiteIntegrityService.h"
#include "nsITimer.h"
#include "nsIURIMutator.h"
#include "nsIWebProgressListener.h"
#include "nsIX509Cert.h"
#include "nsIXPConnect.h"
#include "nsIXULRuntime.h"
#include "nsImportModule.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsSandboxFlags.h"
#include "nsScriptSecurityManager.h"
#include "nsSerializationHelper.h"

using namespace mozilla::ipc;
using namespace mozilla::dom::ipc;

extern mozilla::LazyLogModule gSHIPBFCacheLog;
extern mozilla::LazyLogModule gUseCountersLog;

namespace mozilla::dom {

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

WindowGlobalParent::WindowGlobalParent(
    CanonicalBrowsingContext* aBrowsingContext, uint64_t aInnerWindowId,
    uint64_t aOuterWindowId, FieldValues&& aInit)
    : WindowContext(aBrowsingContext, aInnerWindowId, aOuterWindowId,
                    std::move(aInit)),
      mIsUncommittedInitialDocument(false),
      mSandboxFlags(0),
      mDocumentHasLoaded(false),
      mDocumentHasUserInteracted(false),
      mBlockAllMixedContent(false),
      mUpgradeInsecureRequests(false),
      mPartitionStoragePrincipal(false) {
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess(), "Parent process only");
}

// An error page's own load has no session history load, so it is attributed to
// the load which failed, if that got far enough to have a handle.
static uint64_t HistoryLoadIdFromHandles(
    ParentProcessChannelHandle* aDocumentHandle,
    ParentProcessChannelHandle* aFailedHandle) {
  if (uint64_t id = aDocumentHandle ? aDocumentHandle->HistoryLoadId() : 0) {
    return id;
  }
  return aFailedHandle ? aFailedHandle->HistoryLoadId() : 0;
}

already_AddRefed<WindowGlobalParent> WindowGlobalParent::CreateDisconnected(
    const WindowGlobalInit& aInit, ContentParent* aForProcess) {
  RefPtr<CanonicalBrowsingContext> browsingContext =
      CanonicalBrowsingContext::Get(aInit.context().mBrowsingContextId);
  if (NS_WARN_IF(!browsingContext)) {
    return nullptr;
  }

  MOZ_RELEASE_ASSERT(!aInit.staticCloneOf().IsDiscarded());

  RefPtr<WindowGlobalParent> wgp =
      GetByInnerWindowId(aInit.context().mInnerWindowId);
  MOZ_RELEASE_ASSERT(!wgp, "Creating duplicate WindowGlobalParent");

  MOZ_RELEASE_ASSERT(VerifyPartitionedPrincipalMatchesDocumentPrincipal(
                         aInit.principal(), aInit.partitionedPrincipal()),
                     "Invalid partitioned principal from content");

  FieldValues fields(aInit.context().mFields);
  wgp =
      new WindowGlobalParent(browsingContext, aInit.context().mInnerWindowId,
                             aInit.context().mOuterWindowId, std::move(fields));
  wgp->mDocumentPrincipal = aInit.principal();
  wgp->mDocumentPartitionedPrincipal = aInit.partitionedPrincipal();
  wgp->mDocumentURI = aInit.documentURI();
  if (aInit.isVideoDocument() && wgp->mDocumentURI) {
    wgp->RecordSubsequentNoCorsRequestState(wgp->mDocumentURI);
  }
  wgp->mStaticCloneOf = aInit.staticCloneOf().get_canonical();
  wgp->mIsInitialDocument = Some(aInit.isInitialDocument());
  wgp->mIsUncommittedInitialDocument = aInit.isUncommittedInitialDocument();
  wgp->mBlockAllMixedContent = aInit.blockAllMixedContent();
  wgp->mUpgradeInsecureRequests = aInit.upgradeInsecureRequests();
  wgp->mPartitionStoragePrincipal = aInit.partitionStoragePrincipal();
  wgp->mSandboxFlags = aInit.sandboxFlags();
  wgp->mHttpsOnlyStatus = aInit.httpsOnlyStatus();
  net::CookieJarSettings::Deserialize(aInit.cookieJarSettings(),
                                      getter_AddRefs(wgp->mCookieJarSettings));
  MOZ_RELEASE_ASSERT(
      !aForProcess || !wgp->mStaticCloneOf ||
          wgp->mStaticCloneOf->GetContentParent() == aForProcess,
      "Cannot static clone from a document in a different process!");

  if (aInit.documentChannelHandle()) {
    auto result = aInit.documentChannelHandle()->GetChannel(
        browsingContext, wgp->mStaticCloneOf);
    if (result.isOk()) {
      wgp->mDocumentChannel = result.unwrap();
    } else {
      MOZ_CRASH_UNSAFE_PRINTF("Invalid documentChannelHandle: %s",
                              result.unwrapErr().get());
    }
  }

  if (aInit.failedChannelHandle()) {
    auto result = aInit.failedChannelHandle()->GetChannel(browsingContext,
                                                          wgp->mStaticCloneOf);
    if (result.isOk()) {
      wgp->mFailedChannel = result.unwrap();
    } else {
      MOZ_CRASH_UNSAFE_PRINTF("Invalid failedChannelHandle: %s",
                              result.unwrapErr().get());
    }
  }

  wgp->mDocumentHistoryLoadId = HistoryLoadIdFromHandles(
      aInit.documentChannelHandle(), aInit.failedChannelHandle());

  return wgp.forget();
}

void WindowGlobalParent::InitFromContentProcess(const FieldValues& aRequested,
                                                ContentParent* aSource) {
  MOZ_ASSERT(GetContentParent() == aSource);
  MOZ_DIAGNOSTIC_ASSERT(!BrowsingContext()->GetWindowContexts().Contains(this),
                        "must reconcile before Init registers this context");

  Transaction correction;
  Transaction::ReconcileInitialFields(this, FieldValues(aRequested), aSource,
                                      correction);
  Init();
  correction.SendCorrection(this, aSource);
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
  }

  // Ensure we have a document URI
  if (!mDocumentURI) {
    NS_NewURI(getter_AddRefs(mDocumentURI), "about:blank");
  }

  // NOTE: `cp` may be nullptr, but that's OK, we need to tell every other
  // process in our group in that case.
  IPCInitializer ipcinit = GetIPCInitializer();
  Group()->EachOtherParent(cp, [&](ContentParent* otherContent) {
    (void)otherContent->SendCreateWindowContext(ipcinit);
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

  // A fresh document on a reused BC id must not inherit the previous
  // document's AudioSession override. BFCache restore reuses the WGP so
  // Init() does not run on restore — only new document loads trigger this
  // path.
  if (auto* top = BrowsingContext()->Top();
      top && top->HasCreatedMediaController()) {
    top->GetMediaController()->ClearAudioSessionFor(BrowsingContext()->Id());
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

/* static */
WindowGlobalParent* WindowGlobalParent::Cast(WindowContext* aContext) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  return static_cast<WindowGlobalParent*>(aContext);
}

already_AddRefed<WindowGlobalChild> WindowGlobalParent::GetChildActor() {
  if (!CanSend()) {
    return nullptr;
  }
  IProtocol* otherSide = InProcessParent::ChildActorFor(this);
  return do_AddRef(static_cast<WindowGlobalChild*>(otherSide));
}

BrowserParent* WindowGlobalParent::GetBrowserParent() const {
  return IsInProcess() ? nullptr : static_cast<BrowserParent*>(Manager());
}

ContentParent* WindowGlobalParent::GetContentParent() const {
  BrowserParent* browserParent = GetBrowserParent();
  return browserParent ? browserParent->Manager() : nullptr;
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
  ContentParent* contentParent = GetContentParent();
  return contentParent ? contentParent->ChildID() : 0;
}

int32_t WindowGlobalParent::OsPid() {
  ContentParent* contentParent = GetContentParent();
  return contentParent ? contentParent->Pid() : -1;
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

  if (targetBC->Group() != BrowsingContext()->Group()) {
    return IPC_FAIL(this, "Illegal cross-group BrowsingContext load");
  }

  if (!nsContentUtils::CanNavigate(BrowsingContext(), targetBC.get(),
                                   DocumentPrincipal(), true)) {
    return IPC_FAIL(this,
                    "Illegal cross-process load attempt (!CanNavigate())");
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

  if (targetBC->Group() != BrowsingContext()->Group()) {
    return IPC_FAIL(this, "Illegal cross-group BrowsingContext load");
  }

  if (!nsContentUtils::CanNavigate(BrowsingContext(), targetBC.get(),
                                   DocumentPrincipal(), true)) {
    return IPC_FAIL(this,
                    "Illegal cross-process load attempt (!CanNavigate())");
  }

  // FIXME: We should really initiate the load in the parent before bouncing
  // back down to the child.

  targetBC->InternalLoad(aLoadState);
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvUpdateDocumentURI(NotNull<nsIURI*> aURI) {
  // XXX(nika): Assert that the URI change was one which makes sense (either
  // about:blank -> a real URI, or a legal push/popstate URI change):
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
    if (nsCOMPtr<nsIPrincipal> precursor =
            mDocumentPrincipal->GetPrecursorPrincipal()) {
      principalURI = precursor->GetURI();
    }
  }

  if (nsScriptSecurityManager::IsHttpOrHttpsAndCrossOrigin(principalURI,
                                                           aURI)) {
    return IPC_FAIL(this,
                    "Setting DocumentURI with a different Origin than "
                    "principal URI");
  }

  mDocumentURI = std::move(aURI);
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvUpdateDocumentPrincipal(
    nsIPrincipal* aNewDocumentPrincipal,
    nsIPrincipal* aNewDocumentPartitionedPrincipal) {
  if (!mDocumentPrincipal->Equals(aNewDocumentPrincipal)) {
    return IPC_FAIL(this,
                    "Trying to reuse WindowGlobalParent but the principal of "
                    "the new document does not match the old one");
  }
  // NOTE: Unfortunately, we cannot check ->Equals for our old & new partitioned
  // principals, as the partition key for an initial about:blank document may
  // not match the partition key of the load which replaces it.
  if (!VerifyPartitionedPrincipalMatchesDocumentPrincipal(
          aNewDocumentPrincipal, aNewDocumentPartitionedPrincipal)) {
    return IPC_FAIL(
        this, "Invalid PartitionedPrincipal when re-using WindowGlobalParent");
  }

  mDocumentPrincipal = aNewDocumentPrincipal;
  if (mDocumentPrincipal->Equals(aNewDocumentPartitionedPrincipal)) {
    // Keep only one copy of the principal around if we don't have a partition
    // key.
    mDocumentPartitionedPrincipal = mDocumentPrincipal;
  } else {
    mDocumentPartitionedPrincipal = aNewDocumentPartitionedPrincipal;
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvUpdatePrincipalPartitioning(
    bool aPartitionStoragePrincipal) {
  mPartitionStoragePrincipal = aPartitionStoragePrincipal;
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

already_AddRefed<Promise> WindowGlobalParent::RequestDocumentLanguageMetadata(
    const DocumentLanguageMetadataRequestOptions& aOptions, ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  RefPtr<Promise> domPromise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_ASSERT(aOptions.mTextSampleMinCodeUnits <=
             aOptions.mTextSampleTargetCodeUnits);

  if (!IsCurrentGlobal()) {
    domPromise->MaybeResolve(JS::NullHandleValue);
    return domPromise.forget();
  }

  auto ipcPromise = SendRequestDocumentLanguageMetadata(
      aOptions.mTextSampleMinCodeUnits, aOptions.mTextSampleTargetCodeUnits);
  ipcPromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [domPromise,
       self = RefPtr{this}](const Maybe<DocumentLanguageMetadata>& aMetadata) {
        if (!self->IsCurrentGlobal() || aMetadata.isNothing()) {
          domPromise->MaybeResolve(JS::NullHandleValue);
          return;
        }

        domPromise->MaybeResolve(*aMetadata);
      },
      [domPromise](ResponseRejectReason&&) {
        domPromise->MaybeResolve(JS::NullHandleValue);
      });

  return domPromise.forget();
}

IPCResult WindowGlobalParent::RecvUpdateDocumentHasLoaded(
    bool aDocumentHasLoaded) {
  mDocumentHasLoaded = aDocumentHasLoaded;
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvSetMayHaveViewTransitionOptIn(bool aMayHave) {
  mMayHaveViewTransitionOptIn = aMayHave;
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
  if (!ClientIsValidPrincipalInfo(
          aIPCClientInfo.principalInfo(),
          GetContentParent() ? GetContentParent()->LoadedOrigins() : nullptr)) {
    return IPC_FAIL(this, "SetClientInfo principal not valid for remote type");
  }
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
#ifdef ACCESSIBILITY
      // Destroy the accessibility actor (if any) before we start tearing down
      // this instance so that accessibility can still access information such
      // as the owner element. For example, this allows us to gracefully fire
      // accessibility events notifying of the destruction.
      if (auto* docAcc = a11y::DocAccessibleParent::GetFrom(this)) {
#  if defined(ANDROID)
        MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
#  endif
        docAcc->Destroy();
      }
#endif
      (void)Send__delete__(this);
    }
  }
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvRawMessage(const JSActorMessageMeta& aMeta,
                                             JSIPCValue&& aData,
                                             StructuredCloneData* aStack) {
  ReceiveRawMessage(aMeta, std::move(aData), aStack);
  return IPC_OK();
}

const RemoteType& WindowGlobalParent::GetRemoteType() const {
  if (ContentParent* contentParent = GetContentParent()) {
    return contentParent->GetRemoteType();
  }

  return RemoteType::NotRemote();
}

void WindowGlobalParent::GetRemoteType(nsACString& aRemoteType) const {
  aRemoteType = GetRemoteType().Stringify();
}

void WindowGlobalParent::NotifyContentBlockingEvent(
    uint32_t aEvent, nsIRequest* aRequest, bool aBlocked,
    const nsACString& aTrackingOrigin,
    const nsTArray<nsCString>& aTrackingFullHashes,
    const Maybe<ContentBlockingNotifier::StorageAccessPermissionGrantedReason>&
        aReason,
    const Maybe<CanvasFingerprintingEvent>& aCanvasFingerprintingEvent) {
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
      aCanvasFingerprintingEvent);

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
  if (BrowsingContext() && BrowsingContext()->IsInBFCache()) {
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

mozilla::ipc::IPCResult WindowGlobalParent::RecvUpdateChannels(
    ParentProcessChannelHandle* aDocumentHandle,
    ParentProcessChannelHandle* aFailedHandle) {
  nsCOMPtr<nsIChannel> documentChannel;
  if (aDocumentHandle) {
    auto result = aDocumentHandle->GetChannel(BrowsingContext());
    if (result.isOk()) {
      documentChannel = result.unwrap();
    } else {
      MOZ_CRASH_UNSAFE_PRINTF("Invalid aDocumentHandle: %s",
                              result.unwrapErr().get());
    }
  }

  nsCOMPtr<nsIChannel> failedChannel;
  if (aFailedHandle) {
    auto result = aFailedHandle->GetChannel(BrowsingContext());
    if (result.isOk()) {
      failedChannel = result.unwrap();
    } else {
      MOZ_CRASH_UNSAFE_PRINTF("Invalid aFailedHandle: %s",
                              result.unwrapErr().get());
    }
  }

  // NOTE: In the case where XSLT has performed a transformation on our
  // document, it is possible for this method to be called multiple times. In
  // that case, the channel should be identical to the channel which was
  // previously loaded. Once XSLT is removed, we should be able to remove that
  // part of this check.
  if ((mDocumentChannel || mFailedChannel) &&
      (mDocumentChannel != documentChannel ||
       mFailedChannel != failedChannel)) {
    return IPC_FAIL(this,
                    "Conflicting attempts to set ParentProcessChannelHandle on "
                    "WindowGlobalParent");
  }

  mDocumentChannel = documentChannel;
  mFailedChannel = failedChannel;
  mDocumentHistoryLoadId =
      HistoryLoadIdFromHandles(aDocumentHandle, aFailedHandle);

  return IPC_OK();
}

already_AddRefed<nsIChannel> WindowGlobalParent::GetDocumentChannel() {
  if (mDocumentChannel) {
    return do_AddRef(mDocumentChannel);
  }
  if (Document* doc = GetExtantDoc()) {
    return do_AddRef(doc->GetChannel());
  }
  return nullptr;
}

already_AddRefed<nsIChannel> WindowGlobalParent::GetFailedChannel() {
  if (mFailedChannel) {
    return do_AddRef(mFailedChannel);
  }
  if (Document* doc = GetExtantDoc()) {
    return do_AddRef(doc->GetFailedChannel());
  }
  return nullptr;
}

dom::NoCorsMediaRequestState WindowGlobalParent::NoCorsMediaRequestState(
    nsIURI* aURI) {
  nsCString uri;
  return (NS_SUCCEEDED(aURI->GetSpecIgnoringRef(uri)) &&
          EnsureKnownAllowedSubsequentRequests()
              ->mNoCorsMediaRequestURIs.Contains(uri))
             ? dom::NoCorsMediaRequestState::Subsequent
             : dom::NoCorsMediaRequestState::Initial;
}

StaticAutoPtr<WindowGlobalParent::AllKnownAllowedSubsequentRequests>
    WindowGlobalParent::sAllKnownSubsequentRequests;

/* static */ WindowGlobalParent::AllKnownAllowedSubsequentRequests&
WindowGlobalParent::GetAllKnownAllowedSubsequentRequests() {
  if (!sAllKnownSubsequentRequests) {
    sAllKnownSubsequentRequests =
        new nsTHashMap<PrincipalHashKey,
                       WeakPtr<KnownAllowedSubsequentRequests>>;
  }

  return *sAllKnownSubsequentRequests;
}

WindowGlobalParent::KnownAllowedSubsequentRequests::
    ~KnownAllowedSubsequentRequests() {
  if (!sAllKnownSubsequentRequests) {
    return;
  }

  auto& allKnownRequests = GetAllKnownAllowedSubsequentRequests();
  allKnownRequests.Remove(mPrincipal);
}

WindowGlobalParent::KnownAllowedSubsequentRequests*
WindowGlobalParent::EnsureKnownAllowedSubsequentRequests() {
  if (!mKnownAllowedSubsequentRequests) {
    auto& allKnownRequests = GetAllKnownAllowedSubsequentRequests();
    RefPtr<KnownAllowedSubsequentRequests> knownAllowedSubsequentRequests;
    mKnownAllowedSubsequentRequests = allKnownRequests.LookupOrInsertWith(
        mDocumentPrincipal, [knownAllowedSubsequentRequests,
                             principal = mDocumentPrincipal]() mutable {
          knownAllowedSubsequentRequests =
              MakeAndAddRef<KnownAllowedSubsequentRequests>();
          knownAllowedSubsequentRequests->mPrincipal = principal;
          return knownAllowedSubsequentRequests;
        });
  }

  return mKnownAllowedSubsequentRequests;
}

void WindowGlobalParent::RecordSubsequentNoCorsRequestState(nsIURI* aURI) {
  nsCString uri;
  if (NS_FAILED(aURI->GetSpecIgnoringRef(uri)) || uri.IsEmpty()) {
    return;
  }

  EnsureKnownAllowedSubsequentRequests()->mNoCorsMediaRequestURIs.PutEntry(uri);
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
  CheckPermitUnloadRequest(
      WindowGlobalParent* aWGP, bool aHasInProcessBlocker,
      nsIDocumentViewer::PermitUnloadAction aAction,
      std::function<void(nsIDocumentViewer::PermitUnloadResult)>&& aResolver)
      : mResolver(std::move(aResolver)),
        mWGP(aWGP),
        mAction(aAction),
        mFoundBlocker(aHasInProcessBlocker) {}

  // Special case for when we also want to dispatch a "navigate" event to the
  // top level window's navigation object. The target session history entry that
  // we dispatch is what we use when we #fire-a-traverse-navigate-event. This
  // will _only_ run `DispatchBeforeUnloadToSubtree` for the content process of
  // the top level window. See further comments below in `Run` and in
  // `DispatchBeforeUnloadToSubtree`.
  void RunTraversable(nsDocShellLoadState* aDocShellLoadState) {
    MOZ_DIAGNOSTIC_ASSERT(mWGP->BrowsingContext()->IsTop());
    Run(nullptr, 0, aDocShellLoadState);
  }

  // The complementing special case for `RunTraversable`, which is short hand
  // for running `DispatchBeforeUnloadToSubtree` for all content processes
  // except for the content process of the top level window. See further
  // comments below in `Run` and in `DispatchBeforeUnloadToSubtree`.
  void RunChildNavigables() {
    MOZ_DIAGNOSTIC_ASSERT(mWGP->BrowsingContext()->IsTop());
    Run(mWGP->BrowsingContext()->GetContentParent(), 0);
  }

  void Run(ContentParent* aIgnoreProcess = nullptr, uint32_t aTimeout = 0,
           nsDocShellLoadState* aDocShellLoadState = nullptr) {
    MOZ_ASSERT(mState == State::UNINITIALIZED);
    mState = State::WAITING;

    RefPtr<CheckPermitUnloadRequest> self(this);

    AutoTArray<ContentParent*, 8> seen;
    if (aIgnoreProcess) {
      seen.AppendElement(aIgnoreProcess);
    }

    BrowsingContext* bc = mWGP->GetBrowsingContext();
    auto resolve = [self](nsIDocumentViewer::PermitUnloadResult aResult) {
      self->mFoundBlocker =
          aResult == nsIDocumentViewer::eCanceledByBeforeUnload;
      self->mReason = aResult;
      self->ResolveRequest();
    };
    auto reject = [self](auto) { self->ResolveRequest(); };
    // If `aInfo` is passed, only dispatch to the content process of the top
    // level window.
    if (aDocShellLoadState) {
      MOZ_DIAGNOSTIC_ASSERT(Navigation::IsAPIEnabled());
      ContentParent* cp = mWGP->GetContentParent();
      mPendingRequests++;
      // Here eDontPromptAndUnload means that we ignore beforeunload handlers,
      // but we still need to handle the traversable navigate handler.
      mozilla::NotNull<RefPtr<nsDocShellLoadState>> loadState =
          WrapNotNull(aDocShellLoadState);
      if (mAction ==
          nsIDocumentViewer::PermitUnloadAction::eDontPromptAndUnload) {
        cp->SendDispatchNavigateToTraversable(bc, loadState, resolve, reject);
      } else {
        cp->SendDispatchBeforeUnloadToSubtree(bc, Some(loadState), resolve,
                                              reject);
      }
    } else {
      bc->PreOrderWalk([&](dom::BrowsingContext* aBC) {
        if (WindowGlobalParent* wgp =
                aBC->Canonical()->GetCurrentWindowGlobal()) {
          ContentParent* cp = wgp->GetContentParent();
          // `seen` contains processes that we've already dispatched
          // "beforeunload" to.
          if (wgp->NeedsBeforeUnload() && !seen.ContainsSorted(cp)) {
            seen.InsertElementSorted(cp);
            mPendingRequests++;

            if (cp) {
              cp->SendDispatchBeforeUnloadToSubtree(bc, Nothing(), resolve,
                                                    reject);
            } else {
              NS_DispatchToMainThread(NS_NewRunnableFunction(
                  "DispatchBeforeUnloadToSubtree",
                  [bc = RefPtr{bc}, resolve]() {
                    ContentChild::DispatchBeforeUnloadToSubtree(bc, Nothing(),
                                                                resolve);
                  }));
            }
          }
        }
      });
    }

    if (mPendingRequests && aTimeout) {
      (void)NS_NewTimerWithCallback(getter_AddRefs(mTimer), this, aTimeout,
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
      SendReply();
      return;
    }

    auto action = mAction;
    if (StaticPrefs::dom_disable_beforeunload()) {
      action = nsIDocumentViewer::eDontPromptAndUnload;
    }
    if (action != nsIDocumentViewer::ePrompt) {
      if (action == nsIDocumentViewer::eDontPromptAndUnload) {
        mReason = nsIDocumentViewer::eContinue;
      } else {
        mReason = nsIDocumentViewer::eCanceledByBeforeUnload;
      }
      SendReply();
      return;
    }

    // Handle any failure in prompting by aborting the navigation. See comment
    // in nsDocumentViewer::PermitUnload for reasoning.
    auto cleanup = MakeScopeExit([&]() {
      mReason = nsIDocumentViewer::eCanceledByBeforeUnload;
      SendReply();
    });

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

  void SendReply() {
    MOZ_ASSERT(mState != State::REPLIED);
    mResolver(mReason);
    mState = State::REPLIED;
  }

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MOZ_ASSERT(mState == State::PROMPTING);
    if (!JS::ToBoolean(aValue)) {
      mReason = nsIDocumentViewer::eCanceledByBeforeUnload;
    } else {
      mReason = nsIDocumentViewer::eContinue;
    }

    SendReply();
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    MOZ_ASSERT(mState == State::PROMPTING);

    mReason = nsIDocumentViewer::eCanceledByBeforeUnload;
    SendReply();
  }

  NS_DECL_ISUPPORTS

 private:
  ~CheckPermitUnloadRequest() {
    // We may get here without having sent a reply if the promise we're waiting
    // on is destroyed without being resolved or rejected.
    if (mState != State::REPLIED) {
      mReason = nsIDocumentViewer::eCanceledByBeforeUnload;
      SendReply();
    }
  }

  enum class State : uint8_t {
    UNINITIALIZED,
    WAITING,
    TIMED_OUT,
    PROMPTING,
    REPLIED,
  };

  std::function<void(nsIDocumentViewer::PermitUnloadResult)> mResolver;

  RefPtr<WindowGlobalParent> mWGP;
  nsCOMPtr<nsITimer> mTimer;

  uint32_t mPendingRequests = 0;

  nsIDocumentViewer::PermitUnloadAction mAction;

  State mState = State::UNINITIALIZED;

  bool mFoundBlocker = false;

  nsIDocumentViewer::PermitUnloadResult mReason = nsIDocumentViewer::eContinue;
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
      this, aHasInProcessBlocker, aAction,
      [resolver = std::move(aResolver)](
          nsIDocumentViewer::PermitUnloadResult aResult) {
        resolver(aResult == nsIDocumentViewer::eContinue);
      });
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
      [promise](nsIDocumentViewer::PermitUnloadResult aResult) {
        promise->MaybeResolve(aResult == nsIDocumentViewer::eContinue);
      });
  request->Run(/* aIgnoreProcess */ nullptr, aTimeout);

  return promise.forget();
}

void WindowGlobalParent::PermitUnload(
    std::function<void(nsIDocumentViewer::PermitUnloadResult)>&& aResolver) {
  RefPtr<CheckPermitUnloadRequest> request =
      MakeRefPtr<CheckPermitUnloadRequest>(
          this, /* aHasInProcessBlocker */ false,
          nsIDocumentViewer::PermitUnloadAction::ePrompt, std::move(aResolver));
  request->Run();
}

// https://html.spec.whatwg.org/#checking-if-unloading-is-canceled
// Implements the traversable-specific portion (step 4): fires beforeunload on
// the traversable's active document (if needed) and fires the traverse
// `navigate` event, which may intercept the load via `aDocShellLoadState`.
void WindowGlobalParent::CheckIfUnloadingIsCanceledForTraversable(
    nsDocShellLoadState* aDocShellLoadState,
    nsIDocumentViewer::PermitUnloadAction aAction,
    std::function<void(nsIDocumentViewer::PermitUnloadResult)>&& aResolver) {
  MOZ_DIAGNOSTIC_ASSERT(BrowsingContext()->IsTop());
  RefPtr<CheckPermitUnloadRequest> request =
      MakeRefPtr<CheckPermitUnloadRequest>(this,
                                           /* aHasInProcessBlocker */ false,
                                           aAction, std::move(aResolver));
  request->RunTraversable(aDocShellLoadState);
}

void WindowGlobalParent::PermitUnloadChildNavigables(
    nsIDocumentViewer::PermitUnloadAction aAction,
    std::function<void(nsIDocumentViewer::PermitUnloadResult)>&& aResolver) {
  RefPtr<CheckPermitUnloadRequest> request =
      MakeRefPtr<CheckPermitUnloadRequest>(this,
                                           /* aHasInProcessBlocker */ false,
                                           aAction, std::move(aResolver));
  request->RunChildNavigables();
}

already_AddRefed<mozilla::dom::Promise> WindowGlobalParent::DrawSnapshot(
    const DOMRect* aRect, double aScale, const nsACString& aBackgroundColor,
    const DrawSnapshotOptions& aOptions, mozilla::ErrorResult& aRv) {
  nsIGlobalObject* global = GetParentObject();
  RefPtr<Promise> promise = Promise::Create(global, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  nscolor color;
  if (NS_WARN_IF(
          !ServoCSSParser::ComputeColor(nullptr, aBackgroundColor, &color))) {
    aRv = NS_ERROR_FAILURE;
    return nullptr;
  }

  gfx::CrossProcessPaintFlags flags =
      gfx::CrossProcessPaintFlags::UseHighQualityScaling;
  if (!aRect || aOptions.mDrawView) {
    // If no explicit Rect was passed, we want the currently visible viewport.
    flags |= gfx::CrossProcessPaintFlags::DrawView;
  } else if (aOptions.mResetScrollPosition) {
    flags |= gfx::CrossProcessPaintFlags::ResetScrollPosition;
  }

  gfx::CrossProcessPaint::Start(this, aRect, (float)aScale, color, flags,
                                promise);
  return promise.forget();
}

void WindowGlobalParent::DrawSnapshotInternal(
    gfx::CrossProcessPaint* aPaint, const Maybe<IntRect>& aRect, float aScale,
    nscolor aBackgroundColor, gfx::CrossProcessPaintFlags aFlags) {
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
  RefPtr<WindowGlobalParent> page = Cast(aTop.GetMaybeDiscarded());
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
    page->mPageUseCounters = std::make_unique<PageUseCounters>();
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
WindowGlobalParent::PageUseCounterResult
WindowGlobalParent::FinishAccumulatingPageUseCounters() {
  MOZ_LOG(gUseCountersLog, LogLevel::Debug,
          ("Stop expecting page use counters: -> WindowContext %" PRIu64,
           InnerWindowId()));

  if (!mPageUseCounters) {
    MOZ_ASSERT_UNREACHABLE("Not expecting page use counter data");
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > not expecting page use counter data"));
    return WindowGlobalParent::PageUseCounterResult();
  }

  MOZ_ASSERT(mPageUseCounters->mWaiting > 0);
  --mPageUseCounters->mWaiting;

  if (mPageUseCounters->mWaiting > 0) {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > now waiting on %d", mPageUseCounters->mWaiting));
    return PageUseCounterResultBits::WAITING;
  }

  PageUseCounterResult result;
  if (mPageUseCounters->mReceivedAny) {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > reporting [%s]",
             nsContentUtils::TruncatedURLForDisplay(mDocumentURI).get()));

    result += PageUseCounterResultBits::DATA_RECEIVED;
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
    for (const UseCounter uc : MakeEnumeratedRange(eUseCounter_Count)) {
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
      result += PageUseCounterResultBits::EMPTY_DATA;
    }
  } else {
    MOZ_LOG(gUseCountersLog, LogLevel::Debug,
            (" > no page use counter data was received"));
  }

  mSentPageUseCounters = true;
  mPageUseCounters = nullptr;
  return result;
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
  ADD_BFCACHESTATUS_TO_STRING(ABOUT_PAGE);
  ADD_BFCACHESTATUS_TO_STRING(RESTORING);
  ADD_BFCACHESTATUS_TO_STRING(BEFOREUNLOAD_LISTENER);
  ADD_BFCACHESTATUS_TO_STRING(ACTIVE_LOCK);
  ADD_BFCACHESTATUS_TO_STRING(ACTIVE_WEBTRANSPORT);
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
    (void)top->SetHasActivePeerConnections(newValue.value() > 0);
  }

  return IPC_OK();
}

void WindowGlobalParent::UpdateFullscreenKeyboardLockStatus(
    FullscreenKeyboardLock aStatus) {
  auto* bc = GetBrowsingContext();
  if (auto* topChromeBc = bc ? bc->TopCrossChromeBoundary() : nullptr;
      topChromeBc != bc) {
    if (auto* doc = topChromeBc->GetExtantDocument()) {
      doc->SetFullscreenKeyboardLockStatus(aStatus);
    }
  }
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

  // Might need to do a permissions policy check here, like we currently do in
  // the child process?

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

mozilla::ipc::IPCResult WindowGlobalParent::RecvSetSiteIntegrityProtected(
    NotNull<nsIURI*> aSourceURI, uint64_t aMaxAge) {
  nsCOMPtr<nsISiteIntegrityService> service =
      do_GetService("@mozilla.org/security/integrity;1");
  if (!service) {
    return IPC_OK();
  }

  OriginAttributes originAttributes =
      DocumentPrincipal()->OriginAttributesRef();
  StoragePrincipalHelper::UpdateOriginAttributesForNetworkState(
      aSourceURI, originAttributes);

  (void)service->SetProtected(aSourceURI, originAttributes, aMaxAge);

  return IPC_OK();
}

nsresult WindowGlobalParent::DoAddCertException(bool aTemporary) {
  nsCOMPtr<nsIChannel> failedChannel(GetFailedChannel());
  NS_ENSURE_TRUE(failedChannel, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIURI> failedChannelURI;
  nsresult rv =
      NS_GetFinalChannelURI(failedChannel, getter_AddRefs(failedChannelURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> innerURI(NS_GetInnermostURI(failedChannelURI));
  NS_ENSURE_TRUE(innerURI, NS_ERROR_DOM_INVALID_STATE_ERR);

  nsAutoCString host;
  rv = innerURI->GetAsciiHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  int32_t port;
  rv = innerURI->GetPort(&port);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsITransportSecurityInfo> failedSecurityInfo;
  rv = failedChannel->GetSecurityInfo(getter_AddRefs(failedSecurityInfo));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(failedSecurityInfo, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsIX509Cert> cert;
  rv = failedSecurityInfo->GetServerCert(getter_AddRefs(cert));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(cert, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsICertOverrideService> overrideService(
      do_GetService(NS_CERTOVERRIDE_CONTRACTID));
  NS_ENSURE_TRUE(overrideService, NS_ERROR_FAILURE);

  return overrideService->RememberValidityOverride(
      host, port, mDocumentPrincipal->OriginAttributesRef(), cert, aTemporary);
}

IPCResult WindowGlobalParent::RecvAddCertException(
    bool aTemporary, AddCertExceptionResolver&& aResolver) {
  aResolver(DoAddCertException(aTemporary));
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
  (void)NS_MutateURI(innerURI).SetScheme("http"_ns).Finalize(
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
    rv = identity::CanSilentlyCollect(topPrincipal, principal, &canCollect);
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

  PageUseCounterResult pageUseCounterResult;
  if (mPageUseCountersWindow) {
    pageUseCounterResult =
        mPageUseCountersWindow->FinishAccumulatingPageUseCounters();
    mPageUseCountersWindow = nullptr;
  }

  if (GetBrowsingContext()->IsTopContent() &&
      !mDocumentPrincipal->SchemeIs("about")) {
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

  // Report content blocking log when destroyed. In addition to the regular
  // content-blocking log flush (shared with the flush-on-query path via
  // MaybeReportContentBlockingLog), the teardown path also emits the
  // canvas/font/email fingerprinting Glean metrics that are only meaningful
  // at end-of-page.
  MaybeReportContentBlockingLog();
  if (!IsInProcess()) {
    RefPtr<BrowserParent> browserParent = GetBrowserParent();
    if (browserParent) {
      nsCOMPtr<nsILoadContext> loadContext = browserParent->GetLoadContext();
      if (loadContext && !loadContext->UsePrivateBrowsing() &&
          BrowsingContext()->IsTopContent() && mDocumentURI &&
          net::SchemeIsHttpOrHttps(mDocumentURI)) {
        GetContentBlockingLog()->ReportCanvasFingerprintingLog(
            DocumentPrincipal());
        GetContentBlockingLog()->ReportFontFingerprintingLog(
            DocumentPrincipal());
        GetContentBlockingLog()->ReportEmailTrackingLog(DocumentPrincipal());
      }
    }
  }

  // Note that our WindowContext has become discarded.
  WindowContext::Discard();

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

void WindowGlobalParent::MaybeReportContentBlockingLog() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  // No blocking is recorded for in-process (non-remote) documents — see
  // NotifyContentBlockingEvent.
  if (IsInProcess()) {
    return;
  }
  RefPtr<BrowserParent> browserParent = static_cast<BrowserParent*>(Manager());
  if (!browserParent) {
    return;
  }
  nsCOMPtr<nsILoadContext> loadContext = browserParent->GetLoadContext();
  if (!loadContext || loadContext->UsePrivateBrowsing()) {
    return;
  }
  if (!BrowsingContext()->IsTopContent()) {
    return;
  }

  GetContentBlockingLog()->ReportLog();
}

/* static */
void WindowGlobalParent::FlushAllContentBlockingLogs() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  nsTArray<RefPtr<BrowsingContextGroup>> groups;
  BrowsingContextGroup::GetAllGroups(groups);

  for (const auto& group : groups) {
    for (const auto& bc : group->Toplevels()) {
      if (!bc) {
        continue;
      }
      RefPtr<CanonicalBrowsingContext> canonical = bc->Canonical();
      if (!canonical) {
        continue;
      }
      RefPtr<WindowGlobalParent> wgp = canonical->GetCurrentWindowGlobal();
      if (!wgp) {
        continue;
      }
      wgp->MaybeReportContentBlockingLog();
    }
  }
}

JSObject* WindowGlobalParent::WrapObject(JSContext* aCx,
                                         JS::Handle<JSObject*> aGivenProto) {
  return WindowGlobalParent_Binding::Wrap(aCx, this, aGivenProto);
}

nsIGlobalObject* WindowGlobalParent::GetParentObject() {
  return xpc::NativeGlobal(xpc::PrivilegedJunkScope());
}

nsIDOMProcessParent* WindowGlobalParent::GetDomProcess() {
  if (ContentParent* contentParent = GetContentParent()) {
    return contentParent;
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

  if (!GetRemoteType().IsWeb()) {
    return false;
  }

  return DocumentPrincipal()->GetIsContentPrincipal();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvAddSecurityState(
    uint32_t aStateFlags) {
  if ((aStateFlags & kValidSecurityStateFlags) != aStateFlags) {
    return IPC_FAIL(this, "Invalid security state flags");
  }

  AddSecurityState(aStateFlags);
  return IPC_OK();
}

void WindowGlobalParent::ApplySecurityStateToTop(uint32_t aStateFlags) {
  MOZ_ASSERT(TopWindowContext() == this);
  MOZ_ASSERT((aStateFlags & kValidSecurityStateFlags) == aStateFlags,
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
    nsIURI* aHost, bool aIsThirdParty, const nsTArray<CookieStruct>& aCookies) {
  // Only content principals should be setting cookies via this path.
  // Reject non-content principals (system, null, expanded) to prevent a
  // compromised content process from bypassing the baseDomain check below.
  nsIPrincipal* documentPrincipal = DocumentPrincipal();
  if (!documentPrincipal || !documentPrincipal->GetIsContentPrincipal()) {
    return IPC_FAIL(this,
                    "SetCookies requires a content principal on the window");
  }

  // file:// principals have no meaningful baseDomain for cookie validation,
  // skip the domain check.
  if (!documentPrincipal->SchemeIs("file")) {
    nsAutoCString principalBaseDomain;
    if (NS_FAILED(net::CookieCommons::GetBaseDomain(documentPrincipal,
                                                    principalBaseDomain)) ||
        !principalBaseDomain.Equals(aBaseDomain)) {
      return IPC_FAIL(
          this, "SetCookies baseDomain does not match document principal");
    }
  }

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

  if (!aHost) {
    return IPC_FAIL(this, "aHost must not be null");
  }

  // Authorize the write if the process has already loaded this cookie key, or
  // could legitimately load this principal. The latter covers documents with no
  // channel registration (e.g. file://), which never populate the key set.
  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(aHost, aOriginAttributes);
  if (!cs->ContentProcessHasCookie(aBaseDomain, aOriginAttributes) &&
      !contentParent->ValidatePrincipal(principal)) {
    return IPC_FAIL(this,
                    "Content process not authorized for this cookie domain");
  }

  return cs->SetCookies(aBaseDomain, aOriginAttributes, aHost, aIsThirdParty,
                        aCookies, GetBrowsingContext());
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

IPCResult WindowGlobalParent::RecvRecordUserInteractionForPermissions() {
  WindowGlobalParent* top = TopWindowContext();
  if (!top) {
    return IPC_OK();
  }
  nsIPrincipal* principal = top->DocumentPrincipal();
  if (!principal) {
    return IPC_OK();
  }

  nsCOMPtr<nsIPermissionManager> permMgr =
      do_GetService(NS_PERMISSIONMANAGER_CONTRACTID);
  if (permMgr) {
    (void)permMgr->UpdateLastInteractionForPrincipal(principal);
  }
  return IPC_OK();
}

IPCResult WindowGlobalParent::RecvNotifyAudioSessionTypeOverride(
    const dom::AudioSessionType& aType) {
  // The MediaController lives on the top-level BC, but the override is
  // keyed by the sender's BC id (which may be an iframe). The setter must
  // be honoured even before any controllable media has started, so call
  // GetMediaController() which creates the controller on demand.
  if (auto* top = BrowsingContext()->Top()) {
    if (RefPtr<MediaController> controller = top->GetMediaController()) {
      controller->SetAudioSessionTypeOverride(BrowsingContext()->Id(), aType);
    }
  }
  return IPC_OK();
}

already_AddRefed<PSerialManagerParent>
WindowGlobalParent::AllocPSerialManagerParent() {
  return MakeAndAddRef<SerialManagerParent>();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvPSerialManagerConstructor(
    PSerialManagerParent* aActor) {
  auto* manager = static_cast<SerialManagerParent*>(aActor);
  manager->Init(BrowsingContext()->GetBrowserId());
  return IPC_OK();
}

already_AddRefed<PWebAuthnTransactionParent>
WindowGlobalParent::AllocPWebAuthnTransactionParent() {
  return MakeAndAddRef<WebAuthnTransactionParent>();
}

already_AddRefed<PWebIdentityParent>
WindowGlobalParent::AllocPWebIdentityParent() {
  return MakeAndAddRef<WebIdentityParent>();
}

already_AddRefed<PDigitalCredentialParent>
WindowGlobalParent::AllocPDigitalCredentialParent() {
  return MakeAndAddRef<DigitalCredentialParent>();
}

#ifdef ACCESSIBILITY
already_AddRefed<a11y::PDocAccessibleParent>
WindowGlobalParent::AllocPDocAccessibleParent(const uint64_t&, const bool&) {
  return a11y::DocAccessibleParent::New();
}

mozilla::ipc::IPCResult WindowGlobalParent::RecvPDocAccessibleConstructor(
    a11y::PDocAccessibleParent* aDoc, const uint64_t& aParentID,
    const bool& aIsPrintDoc) {
#  if defined(ANDROID)
  MonitorAutoLock mal(nsAccessibilityService::GetAndroidMonitor());
#  endif
  if (ManagedPDocAccessibleParent().Count() > 1) {
    return IPC_FAIL(
        this, "Attempt to construct second PDocAccessible for a PWindowGlobal");
  }
  auto doc = static_cast<a11y::DocAccessibleParent*>(aDoc);
  doc->SetIsPrintDoc(aIsPrintDoc);
  auto allow = doc->ShouldAllowConstruction();
  if (allow == a11y::DocAccessibleParent::AllowConstruction::Disallow) {
    return IPC_FAIL(
        this,
        "Attempt to construct PDocAccessible when accessibility not in use");
  }
  if (allow == a11y::DocAccessibleParent::AllowConstruction::AllowButIgnore) {
    doc->MarkAsShutdown();
    return IPC_OK();
  }

  if (GetBrowsingContext()->IsDiscarded() || !IsCurrentGlobal()) {
    // This document is about to die, so ignore it. This is particularly
    // important on Android because we must never have more than one active top
    // level DocAccessible at the same time there.
    doc->MarkAsShutdown();
    return IPC_OK();
  }

  RefPtr<WindowGlobalParent> embedderWgp =
      GetBrowsingContext()->GetEmbedderWindowGlobal();
  if (NS_WARN_IF(!IsTop() && (!embedderWgp || embedderWgp->IsDiscarded()))) {
    // This is an iframe, but it doesn't have a valid embedder WindowGlobal.
    // This can happen if the parent BrowsingContext navigated somewhere else
    // while the embedded document was loading. This isn't an error, but it does
    // mean this embedded document is about to die and its content process just
    // hasn't caught up yet. Just ignore this document.
    doc->MarkAsShutdown();
    return IPC_OK();
  }

  if (!IsProcessRoot()) {
    // Iframe document rendered in the same process as its embedder.
    // A document should never directly be the parent of another document.
    // There should always be an outer doc accessible child of the outer
    // document containing the child.
    MOZ_ASSERT(aParentID);
    if (!aParentID) {
      return IPC_FAIL(this, "No parent specified for same-process iframe");
    }

    MOZ_ASSERT(embedderWgp);
    auto* parentDoc = a11y::DocAccessibleParent::GetFrom(
        embedderWgp, /* aAllowShutdown */ true);
    if (!parentDoc) {
      return IPC_FAIL(this,
                      "Same-process embedder's PDocAccessible doesn't exist");
    }
    if (parentDoc->IsShutdown()) {
      // This can happen if parentDoc is an OOP iframe, but its embedder has
      // been destroyed. (DocAccessibleParent::Destroy destroys any child
      // documents.) The OOP iframe (and anything it embeds) will die soon
      // anyway, so mark this document as shutdown and ignore it.
      doc->MarkAsShutdown();
      return IPC_OK();
    }

    mozilla::ipc::IPCResult added = parentDoc->AddChildDoc(doc, aParentID);
    if (!added) {
      return added;
    }

#  ifdef XP_WIN
    if (a11y::nsWinUtils::IsWindowEmulationStarted()) {
      doc->SetEmulatedWindowHandle(parentDoc->GetEmulatedWindowHandle());
    }
#  endif

    return IPC_OK();
  }

  // This document is at the top level in its content process. That means it
  // makes no sense to get an id for an accessible that is its parent.
  MOZ_ASSERT(!aParentID);
  if (aParentID) {
    return IPC_FAIL(
        this, "Doc at top level of its process shouldn't have a remote parent");
  }
  // Sometimes, we can get a new top level DocAccessibleParent before the
  // previous WindowGlobalParent (and its own top level DocAccessibleParent)
  // gets destroyed. The previous one will die pretty shortly anyway, so just
  // destroy its DocAccessibleParent now. We do this because some platforms
  // (e.g. Android) require that there is only ever a single platform wrapper
  // for a top level document at a time.
  for (dom::WindowContext* wc : GetBrowsingContext()->GetWindowContexts()) {
    if (wc == this) {
      continue;
    }
    WindowGlobalParent* otherWgp = wc->Canonical();
    if (auto* otherDoc = a11y::DocAccessibleParent::GetFrom(otherWgp)) {
      MOZ_ASSERT(otherDoc->IsTopLevelInContentProcess());
      otherDoc->Destroy();
    }
  }

  if (BrowserBridgeParent* bridge =
          GetBrowserParent()->GetBrowserBridgeParent()) {
    // Iframe document rendered in a different process to its embedder.
    doc->SetTopLevelInContentProcess();
    if (!doc->IsPrintDoc()) {
      a11y::ProxyCreated(doc);
    }
    // It's possible the embedder accessible hasn't been set yet; e.g.
    // a hidden iframe. In that case, embedderDoc will be null and this will
    // be handled when the embedder is set.
    if (a11y::DocAccessibleParent* embedderDoc =
            bridge->GetEmbedderAccessibleDoc()) {
      mozilla::ipc::IPCResult added = embedderDoc->AddChildDoc(bridge);
      if (!added) {
        return added;
      }
    }
    return IPC_OK();
  }

  MOZ_ASSERT(IsTop());
  doc->SetTopLevel();
  a11y::DocManager::RemoteDocAdded(doc);
#  ifdef XP_WIN
  if (!aIsPrintDoc) {
    doc->MaybeInitWindowEmulation();
  }
#  endif
  return IPC_OK();
}
#endif  // ACCESSIBILITY

already_AddRefed<PPrefetchRecordParent>
WindowGlobalParent::AllocPPrefetchRecordParent(
    const SpeculativePrefetchArgs& aArgs) {
  RefPtr<PrefetchRecordParent> actor = MakeRefPtr<PrefetchRecordParent>();
  actor->Init(this, aArgs);
  return actor.forget();
}

RefPtr<PrefetchMatchPromise> WindowGlobalParent::WaitForMatchingPrefetchRecord(
    nsIURI* aURI) {
  // Implements the waiting half of "wait for a matching prefetch record";
  // the caller has already checked for a completed match (steps 5.1.1-5.1.2).
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#wait-for-a-matching-prefetch-record

  // Steps 5.1.3-5.1.5: build potentialRecords (ongoing records that could
  // still satisfy aURI) and, "if potentialRecords is empty, return null"
  // without ever registering a waiter.
  if (!HasPotentialPrefetchMatch(aURI)) {
    LOG_SPECRULES(
        ("WindowGlobalParent::WaitForMatchingPrefetchRecord: "
         "this=%p no potential records",
         this));
    return nullptr;
  }

  // Otherwise "wait until the state of any element of ... prefetch records
  // changes" and re-run the loop: PrefetchMatchWaiter re-checks for a match
  // (or gives up) each time NotifyPrefetchStateChanged fires, until the
  // dom.speculation_rules.wait_timeout_ms timeout.
  RefPtr<PrefetchMatchWaiter> waiter = PrefetchMatchWaiter::Create(
      this, aURI,
      TimeDuration::FromMilliseconds(
          StaticPrefs::dom_speculation_rules_wait_timeout_ms()));
  mPrefetchWaiters.AppendElement(waiter);
  LOG_SPECRULES(
      ("WindowGlobalParent::WaitForMatchingPrefetchRecord: "
       "this=%p registered waiter=%p",
       this, waiter.get()));
  return waiter->Promise();
}

bool WindowGlobalParent::HasPotentialPrefetchMatch(nsIURI* aURI) {
  // Implements step 5.1.4 of "wait for a matching prefetch record": true if
  // some ongoing record could still, once it completes, be a matching
  // prefetch record for aURI.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#wait-for-a-matching-prefetch-record
  nsTArray<PPrefetchRecordParent*> managed;
  ManagedPPrefetchRecordParent(managed);
  for (auto* p : managed) {
    auto* rec = static_cast<PrefetchRecordParent*>(p);
    if (rec->State() == PrefetchState::Ongoing &&
        rec->IsExpectedToMatch(aURI)) {
      return true;
    }
  }
  return false;
}

bool WindowGlobalParent::HasMatchingPrefetchCandidate(nsIURI* aURI) {
  nsTArray<PPrefetchRecordParent*> managed;
  ManagedPPrefetchRecordParent(managed);
  for (auto* p : managed) {
    auto* rec = static_cast<PrefetchRecordParent*>(p);
    // Network outcomes exist only in treatment and must not change eligibility.
    if (rec->IsEligibleForPrefetch() && rec->MatchesCandidateURL(aURI)) {
      return true;
    }
  }
  return false;
}

void WindowGlobalParent::RemoveWaiter(PrefetchMatchWaiter* aWaiter) {
  mPrefetchWaiters.RemoveElement(aWaiter);
  LOG_SPECRULES(
      ("WindowGlobalParent::RemoveWaiter: this=%p waiter=%p "
       "remaining=%zu",
       this, aWaiter, mPrefetchWaiters.Length()));
}

void WindowGlobalParent::NotifyPrefetchStateChanged(
    PrefetchRecordParent* aRec) {
  // Drives "wait for a matching prefetch record" waiters when a record's state
  // changes.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#wait-for-a-matching-prefetch-record
  for (RefPtr<PrefetchMatchWaiter>& waiter : mPrefetchWaiters.Clone()) {
    waiter->OnRecordStateChanged(aRec);
  }
}

// Cross-site privacy gate: whether conflicting credentials exist for an
// exchange.
// Spec:
// https://wicg.github.io/nav-speculation/prefetch.html#conflicting-credentials-exist
static bool ConflictingCredentialsExist(const ExchangeRecord& aExchange,
                                        const nsString& aSourcePartitionKey) {
  OriginAttributes attrs;
  if (aExchange.mRequestURI) {
    attrs.SetPartitionKey(aExchange.mRequestURI, false);
  }
  // Same-site: no conflict possible.
  if (attrs.mPartitionKey == aSourcePartitionKey) {
    return false;
  }
  // M1: same_origin_only gate prevents cross-site hops (see the cross-site
  // redirect check in PrefetchRecordParent::AsyncOnChannelRedirect).
  // M2: implement cookie presence check via nsICookieManager.
  return false;
}

PrefetchRecordParent* WindowGlobalParent::FindMatchingPrefetchRecord(
    nsIURI* aURI) {
  // Implements "find a matching complete prefetch record".
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#find-a-matching-complete-prefetch-record
  // Steps 1-2: "Let exactRecord be null. Let inexactRecord be null."
  nsTArray<PPrefetchRecordParent*> managed;
  ManagedPPrefetchRecordParent(managed);

  PrefetchRecordParent* exact = nullptr;
  PrefetchRecordParent* inexact = nullptr;

  // Step 3: "For each record of sourceSnapshotParams's prefetch records:"
  for (auto* p : managed) {
    auto* rec = static_cast<PrefetchRecordParent*>(p);
    // Step 3a: "If record's state is not "completed", then continue."
    if (rec->State() != PrefetchState::Completed) {
      continue;
    }

    bool urlEquals = false;
    if (rec->URL()) {
      rec->URL()->Equals(aURI, &urlEquals);
    }
    // Step 3b: "If record's URL is equal to url: Set exactRecord to record.
    // Break."
    if (urlEquals) {
      exact = rec;
      break;
    }

    // Step 3c: "If inexactRecord is null and record matches a URL given url:
    // Set inexactRecord to record."
    if (!inexact && rec->MatchesURL(aURI)) {
      inexact = rec;
    }
  }

  // Step 4: "Let recordToUse be exactRecord if exactRecord is not null,
  // otherwise inexactRecord."
  PrefetchRecordParent* toUse = exact ? exact : inexact;
  // Step 6: "Return null." (reached when recordToUse is null, so step 5's
  // "If recordToUse is not null" guard doesn't apply.)
  if (!toUse) {
    return nullptr;  // Step 6: no match
  }

  // Step 5b: "If recordToUse's expiry time is less than currentTime: Trigger
  // a prefetch status updated event ... "failure" status. Return null."
  if (toUse->ExpiryTime() < TimeStamp::Now()) {
    LOG_SPECRULES(
        ("WindowGlobalParent::FindMatchingPrefetchRecord: "
         "this=%p rec=%p expired",
         this, toUse));
    toUse->FirePrefetchStatusUpdated(false);
    return nullptr;
  }

  // Step 5c: "For each exchangeRecord of recordToUse's redirect chain: If
  // conflicting credentials exist ...: Trigger a prefetch status updated
  // event ... "failure" status. Return null."
  for (const auto& xr : toUse->RedirectChain()) {
    if (ConflictingCredentialsExist(xr, toUse->SourcePartitionKey())) {
      LOG_SPECRULES(
          ("WindowGlobalParent::FindMatchingPrefetchRecord: "
           "this=%p rec=%p credential conflict",
           this, toUse));
      toUse->FirePrefetchStatusUpdated(false);
      return nullptr;
    }
  }

  // Step 5d: "Return recordToUse."
  LOG_SPECRULES(
      ("WindowGlobalParent::FindMatchingPrefetchRecord: this=%p found rec=%p",
       this, toUse));
  return toUse;
}

void WindowGlobalParent::DedupePrefetchRecords(
    PrefetchRecordParent* aJustCompleted) {
  // Implements "complete a prefetch record" step 4: remove all other completed
  // records with the same URL.
  // Spec:
  // https://wicg.github.io/nav-speculation/prefetch.html#prefetch-record-complete
  // Note: parent cannot send __delete__ (parent: async __delete__() is
  // child-initiated). Old records are marked Canceled so navigation matching
  // skips them; DOM GC handles actor cleanup.
  nsTArray<PPrefetchRecordParent*> managed;
  ManagedPPrefetchRecordParent(managed);
  for (auto* p : managed) {
    auto* rec = static_cast<PrefetchRecordParent*>(p);
    if (rec == aJustCompleted) {
      continue;
    }
    if (rec->State() != PrefetchState::Completed) {
      continue;
    }
    bool urlEquals = false;
    if (rec->URL() && aJustCompleted->URL()) {
      rec->URL()->Equals(aJustCompleted->URL(), &urlEquals);
    }
    if (urlEquals) {
      LOG_SPECRULES(
          ("WindowGlobalParent::DedupePrefetchRecords: this=%p evicting "
           "older rec=%p",
           this, rec));
      rec->MarkCanceled();
    }
  }
}

NS_IMPL_CYCLE_COLLECTION_CLASS(WindowGlobalParent)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(WindowGlobalParent,
                                                WindowContext)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPageUseCountersWindow)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mStaticCloneOf)
  tmp->UnlinkManager();
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(WindowGlobalParent,
                                                  WindowContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPageUseCountersWindow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mStaticCloneOf)
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
