/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/CanonicalBrowsingContext.h"

#include "ContentAnalysis.h"
#include "ErrorList.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Components.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/EventForwards.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/BrowsingContextBinding.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/Navigation.h"
#include "mozilla/dom/PBrowserParent.h"
#include "mozilla/dom/PBackgroundSessionStorageCache.h"
#include "mozilla/dom/PWindowGlobalParent.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/ContentProcessManager.h"
#include "mozilla/dom/MediaController.h"
#include "mozilla/dom/MediaControlService.h"
#include "mozilla/dom/ContentPlaybackController.h"
#include "mozilla/dom/SessionStorageManager.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#ifdef NS_PRINTING
#  include "mozilla/layout/RemotePrintJobParent.h"
#endif
#include "mozilla/net/DocumentLoadListener.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_docshell.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/glean/DomMetrics.h"
#include "nsILayoutHistoryState.h"
#include "nsIPrintSettings.h"
#include "nsIPrintSettingsService.h"
#include "nsISupports.h"
#include "nsIWebNavigation.h"
#include "nsDocShell.h"
#include "nsFrameLoader.h"
#include "nsFrameLoaderOwner.h"
#include "nsGlobalWindowOuter.h"
#include "nsIContentAnalysis.h"
#include "nsIWebBrowserChrome.h"
#include "nsIXULRuntime.h"
#include "nsNetUtil.h"
#include "nsSHistory.h"
#include "nsSecureBrowserUI.h"
#include "nsQueryObject.h"
#include "nsBrowserStatusFilter.h"
#include "nsIBrowser.h"
#include "nsTHashSet.h"
#include "nsISessionStoreFunctions.h"
#include "nsIXPConnect.h"
#include "nsImportModule.h"
#include "UnitTransforms.h"

using namespace mozilla::ipc;

extern mozilla::LazyLogModule gAutoplayPermissionLog;
extern mozilla::LazyLogModule gSHLog;
extern mozilla::LazyLogModule gSHIPBFCacheLog;

#define AUTOPLAY_LOG(msg, ...) \
  MOZ_LOG(gAutoplayPermissionLog, LogLevel::Debug, (msg, ##__VA_ARGS__))

static mozilla::LazyLogModule sPBContext("PBContext");

// Global count of canonical browsing contexts with the private attribute set
static uint32_t gNumberOfPrivateContexts = 0;

// Current parent process epoch for parent initiated navigations
static uint64_t gParentInitiatedNavigationEpoch = 0;

static void IncreasePrivateCount() {
  gNumberOfPrivateContexts++;
  MOZ_LOG(sPBContext, mozilla::LogLevel::Debug,
          ("%s: Private browsing context count %d -> %d", __func__,
           gNumberOfPrivateContexts - 1, gNumberOfPrivateContexts));
  if (gNumberOfPrivateContexts > 1) {
    return;
  }

  static bool sHasSeenPrivateContext = false;
  if (!sHasSeenPrivateContext) {
    sHasSeenPrivateContext = true;
    mozilla::glean::dom_parentprocess::private_window_used.Set(true);
  }
}

static void DecreasePrivateCount() {
  MOZ_ASSERT(gNumberOfPrivateContexts > 0);
  gNumberOfPrivateContexts--;

  MOZ_LOG(sPBContext, mozilla::LogLevel::Debug,
          ("%s: Private browsing context count %d -> %d", __func__,
           gNumberOfPrivateContexts + 1, gNumberOfPrivateContexts));
  if (!gNumberOfPrivateContexts &&
      !mozilla::StaticPrefs::browser_privatebrowsing_autostart()) {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      MOZ_LOG(sPBContext, mozilla::LogLevel::Debug,
              ("%s: last-pb-context-exited fired", __func__));
      observerService->NotifyObservers(nullptr, "last-pb-context-exited",
                                       nullptr);
    }
  }
}

namespace mozilla::dom {

extern mozilla::LazyLogModule gUserInteractionPRLog;

#define USER_ACTIVATION_LOG(msg, ...) \
  MOZ_LOG(gUserInteractionPRLog, LogLevel::Debug, (msg, ##__VA_ARGS__))

CanonicalBrowsingContext::CanonicalBrowsingContext(WindowContext* aParentWindow,
                                                   BrowsingContextGroup* aGroup,
                                                   uint64_t aBrowsingContextId,
                                                   uint64_t aOwnerProcessId,
                                                   uint64_t aEmbedderProcessId,
                                                   BrowsingContext::Type aType,
                                                   FieldValues&& aInit)
    : BrowsingContext(aParentWindow, aGroup, aBrowsingContextId, aType,
                      std::move(aInit)),
      mProcessId(aOwnerProcessId),
      mEmbedderProcessId(aEmbedderProcessId),
      mPermanentKey(JS::NullValue()) {
  // You are only ever allowed to create CanonicalBrowsingContexts in the
  // parent process.
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());

  // The initial URI in a BrowsingContext is always "about:blank".
  MOZ_ALWAYS_SUCCEEDS(
      NS_NewURI(getter_AddRefs(mCurrentRemoteURI), "about:blank"));

  mozilla::HoldJSObjects(this);
}

CanonicalBrowsingContext::~CanonicalBrowsingContext() {
  mPermanentKey.setNull();

  mozilla::DropJSObjects(this);

  if (mSessionHistory) {
    mSessionHistory->SetBrowsingContext(nullptr);
  }
}

/* static */
already_AddRefed<CanonicalBrowsingContext> CanonicalBrowsingContext::Get(
    uint64_t aId) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  return BrowsingContext::Get(aId).downcast<CanonicalBrowsingContext>();
}

/* static */
CanonicalBrowsingContext* CanonicalBrowsingContext::Cast(
    BrowsingContext* aContext) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  return static_cast<CanonicalBrowsingContext*>(aContext);
}

/* static */
const CanonicalBrowsingContext* CanonicalBrowsingContext::Cast(
    const BrowsingContext* aContext) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  return static_cast<const CanonicalBrowsingContext*>(aContext);
}

already_AddRefed<CanonicalBrowsingContext> CanonicalBrowsingContext::Cast(
    already_AddRefed<BrowsingContext>&& aContext) {
  MOZ_RELEASE_ASSERT(XRE_IsParentProcess());
  return aContext.downcast<CanonicalBrowsingContext>();
}

ContentParent* CanonicalBrowsingContext::GetContentParent() const {
  if (mProcessId == 0) {
    return nullptr;
  }

  ContentProcessManager* cpm = ContentProcessManager::GetSingleton();
  if (!cpm) {
    return nullptr;
  }
  return cpm->GetContentProcessById(ContentParentId(mProcessId));
}

void CanonicalBrowsingContext::GetCurrentRemoteType(nsACString& aRemoteType,
                                                    ErrorResult& aRv) const {
  // If we're in the parent process, dump out the void string.
  if (mProcessId == 0) {
    aRemoteType = NOT_REMOTE_TYPE;
    return;
  }

  ContentParent* cp = GetContentParent();
  if (!cp) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return;
  }

  aRemoteType = cp->GetRemoteType();
}

void CanonicalBrowsingContext::SetOwnerProcessId(uint64_t aProcessId) {
  MOZ_LOG(GetLog(), LogLevel::Debug,
          ("SetOwnerProcessId for 0x%08" PRIx64 " (0x%08" PRIx64
           " -> 0x%08" PRIx64 ")",
           Id(), mProcessId, aProcessId));

  mProcessId = aProcessId;
}

nsISecureBrowserUI* CanonicalBrowsingContext::GetSecureBrowserUI() {
  if (!IsTop()) {
    return nullptr;
  }
  if (!mSecureBrowserUI) {
    mSecureBrowserUI = new nsSecureBrowserUI(this);
  }
  return mSecureBrowserUI;
}

namespace {
// The DocShellProgressBridge is attached to a root content docshell loaded in
// the parent process. Notifications are paired up with the docshell which they
// came from, so that they can be fired to the correct
// BrowsingContextWebProgress and bubble through this tree separately.
//
// Notifications are filtered by a nsBrowserStatusFilter before being received
// by the DocShellProgressBridge.
class DocShellProgressBridge : public nsIWebProgressListener {
 public:
  NS_DECL_ISUPPORTS
  // NOTE: This relies in the expansion of `NS_FORWARD_SAFE` and all listener
  // methods accepting an `aWebProgress` argument. If this changes in the
  // future, this may need to be written manually.
  NS_FORWARD_SAFE_NSIWEBPROGRESSLISTENER(GetTargetContext(aWebProgress))

  explicit DocShellProgressBridge(uint64_t aTopContextId)
      : mTopContextId(aTopContextId) {}

 private:
  virtual ~DocShellProgressBridge() = default;

  nsIWebProgressListener* GetTargetContext(nsIWebProgress* aWebProgress) {
    RefPtr<CanonicalBrowsingContext> context;
    if (nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(aWebProgress)) {
      context = docShell->GetBrowsingContext()->Canonical();
    } else {
      context = CanonicalBrowsingContext::Get(mTopContextId);
    }
    return context && !context->IsDiscarded() ? context->GetWebProgress()
                                              : nullptr;
  }

  uint64_t mTopContextId = 0;
};

NS_IMPL_ISUPPORTS(DocShellProgressBridge, nsIWebProgressListener)
}  // namespace

void CanonicalBrowsingContext::MaybeAddAsProgressListener(
    nsIWebProgress* aWebProgress) {
  // Only add as a listener if the created docshell is a toplevel content
  // docshell. We'll get notifications for all of our subframes through a single
  // listener.
  if (!IsTopContent()) {
    return;
  }

  if (!mDocShellProgressBridge) {
    mDocShellProgressBridge = new DocShellProgressBridge(Id());
    mStatusFilter = new nsBrowserStatusFilter();
    mStatusFilter->AddProgressListener(mDocShellProgressBridge,
                                       nsIWebProgress::NOTIFY_ALL);
  }

  aWebProgress->AddProgressListener(mStatusFilter, nsIWebProgress::NOTIFY_ALL);
}

void CanonicalBrowsingContext::ReplacedBy(
    CanonicalBrowsingContext* aNewContext,
    const NavigationIsolationOptions& aRemotenessOptions) {
  MOZ_ASSERT(!aNewContext->mWebProgress);
  MOZ_ASSERT(!aNewContext->mSessionHistory);
  MOZ_ASSERT(IsTop() && aNewContext->IsTop());

  mIsReplaced = true;
  aNewContext->mIsReplaced = false;

  if (mStatusFilter) {
    mStatusFilter->RemoveProgressListener(mDocShellProgressBridge);
    mStatusFilter = nullptr;
  }

  mWebProgress->ContextReplaced(aNewContext);
  aNewContext->mWebProgress = std::move(mWebProgress);

  // Use the Transaction for the fields which need to be updated whether or not
  // the new context has been attached before.
  // SetWithoutSyncing can be used if context hasn't been attached.
  Transaction txn;
  txn.SetBrowserId(GetBrowserId());
  txn.SetIsAppTab(GetIsAppTab());
  txn.SetHasSiblings(GetHasSiblings());
  txn.SetTopLevelCreatedByWebContent(GetTopLevelCreatedByWebContent());
  txn.SetHistoryID(GetHistoryID());
  txn.SetExplicitActive(GetExplicitActive());
  txn.SetEmbedderColorSchemes(GetEmbedderColorSchemes());
  txn.SetHasRestoreData(GetHasRestoreData());
  txn.SetShouldDelayMediaFromStart(GetShouldDelayMediaFromStart());
  txn.SetForceOffline(GetForceOffline());
  txn.SetTopInnerSizeForRFP(GetTopInnerSizeForRFP());

  // Propagate some settings on BrowsingContext replacement so they're not lost
  // on bfcached navigations. These are important for GeckoView (see bug
  // 1781936).
  txn.SetAllowJavascript(GetAllowJavascript());
  txn.SetForceEnableTrackingProtection(GetForceEnableTrackingProtection());
  txn.SetUserAgentOverride(GetUserAgentOverride());
  txn.SetSuspendMediaWhenInactive(GetSuspendMediaWhenInactive());
  txn.SetDisplayMode(GetDisplayMode());
  txn.SetForceDesktopViewport(GetForceDesktopViewport());
  txn.SetIsUnderHiddenEmbedderElement(GetIsUnderHiddenEmbedderElement());

  // When using site-specific zoom, we let the frontend manage the zoom level
  // of BFCache'd contexts. Overriding those zoom levels can cause weirdness
  // like bug 1846141. We always copy to new contexts to avoid bug 1914149.
  if (!aNewContext->EverAttached() ||
      !StaticPrefs::browser_zoom_siteSpecific()) {
    txn.SetFullZoom(GetFullZoom());
    txn.SetTextZoom(GetTextZoom());
  }

  // Propagate the default load flags so that the TRR mode flags are forwarded
  // to the new browsing context. See bug 1828643.
  txn.SetDefaultLoadFlags(GetDefaultLoadFlags());

  // As this is a different BrowsingContext, set InitialSandboxFlags to the
  // current flags in the new context so that they also apply to any initial
  // about:blank documents created in it.
  txn.SetSandboxFlags(GetSandboxFlags());
  txn.SetInitialSandboxFlags(GetSandboxFlags());
  txn.SetTargetTopLevelLinkClicksToBlankInternal(
      TargetTopLevelLinkClicksToBlank());
  if (aNewContext->EverAttached()) {
    MOZ_ALWAYS_SUCCEEDS(txn.Commit(aNewContext));
  } else {
    txn.CommitWithoutSyncing(aNewContext);
  }

  aNewContext->mRestoreState = mRestoreState.forget();
  MOZ_ALWAYS_SUCCEEDS(SetHasRestoreData(false));

  // XXXBFCache name handling is still a bit broken in Fission in general,
  // at least in case name should be cleared.
  if (aRemotenessOptions.mTryUseBFCache) {
    MOZ_ASSERT(!aNewContext->EverAttached());
    aNewContext->mFields.SetWithoutSyncing<IDX_Name>(GetName());
    // We don't copy over HasLoadedNonInitialDocument here, we'll actually end
    // up loading a new initial document at this point, before the real load.
    // The real load will then end up setting HasLoadedNonInitialDocument to
    // true.
  }

  if (mSessionHistory) {
    mSessionHistory->SetBrowsingContext(aNewContext);
    // At this point we will be creating a new ChildSHistory in the child.
    // That means that the child's epoch will be reset, so it makes sense to
    // reset the epoch in the parent too.
    mSessionHistory->SetEpoch(0, Nothing());
    mSessionHistory.swap(aNewContext->mSessionHistory);
    RefPtr<ChildSHistory> childSHistory = ForgetChildSHistory();
    aNewContext->SetChildSHistory(childSHistory);
  }

  BackgroundSessionStorageManager::PropagateManager(Id(), aNewContext->Id());

  // Transfer the ownership of the priority active status from the old context
  // to the new context.
  aNewContext->mPriorityActive = mPriorityActive;
  mPriorityActive = false;

  MOZ_ASSERT(aNewContext->mLoadingEntries.IsEmpty());
  mLoadingEntries.SwapElements(aNewContext->mLoadingEntries);
  MOZ_ASSERT(!aNewContext->mActiveEntry);
  mActiveEntry.swap(aNewContext->mActiveEntry);

  aNewContext->mPermanentKey = mPermanentKey;
  mPermanentKey.setNull();
}

void CanonicalBrowsingContext::UpdateSecurityState() {
  if (mSecureBrowserUI) {
    mSecureBrowserUI->RecomputeSecurityFlags();
  }
}

void CanonicalBrowsingContext::GetWindowGlobals(
    nsTArray<RefPtr<WindowGlobalParent>>& aWindows) {
  aWindows.SetCapacity(GetWindowContexts().Length());
  for (auto& window : GetWindowContexts()) {
    aWindows.AppendElement(static_cast<WindowGlobalParent*>(window.get()));
  }
}

WindowGlobalParent* CanonicalBrowsingContext::GetCurrentWindowGlobal() const {
  return static_cast<WindowGlobalParent*>(GetCurrentWindowContext());
}

WindowGlobalParent* CanonicalBrowsingContext::GetParentWindowContext() {
  return static_cast<WindowGlobalParent*>(
      BrowsingContext::GetParentWindowContext());
}

WindowGlobalParent* CanonicalBrowsingContext::GetTopWindowContext() {
  return static_cast<WindowGlobalParent*>(
      BrowsingContext::GetTopWindowContext());
}

already_AddRefed<nsIWidget>
CanonicalBrowsingContext::GetParentProcessWidgetContaining() {
  // If our document is loaded in-process, such as chrome documents, get the
  // widget directly from our outer window. Otherwise, try to get the widget
  // from the toplevel content's browser's element.
  nsCOMPtr<nsIWidget> widget;
  if (nsGlobalWindowOuter* window = nsGlobalWindowOuter::Cast(GetDOMWindow())) {
    widget = window->GetNearestWidget();
  } else if (Element* topEmbedder = Top()->GetEmbedderElement()) {
    widget = nsContentUtils::WidgetForContent(topEmbedder);
    if (!widget) {
      widget = nsContentUtils::WidgetForDocument(topEmbedder->OwnerDoc());
    }
  }

  if (widget) {
    widget = widget->GetTopLevelWidget();
  }

  return widget.forget();
}

already_AddRefed<nsIBrowserDOMWindow>
CanonicalBrowsingContext::GetBrowserDOMWindow() {
  RefPtr<CanonicalBrowsingContext> chromeTop = TopCrossChromeBoundary();
  nsGlobalWindowOuter* topWin;
  if ((topWin = nsGlobalWindowOuter::Cast(chromeTop->GetDOMWindow())) &&
      topWin->IsChromeWindow()) {
    return do_AddRef(topWin->GetBrowserDOMWindow());
  }
  return nullptr;
}

already_AddRefed<WindowGlobalParent>
CanonicalBrowsingContext::GetEmbedderWindowGlobal() const {
  uint64_t windowId = GetEmbedderInnerWindowId();
  if (windowId == 0) {
    return nullptr;
  }

  return WindowGlobalParent::GetByInnerWindowId(windowId);
}

CanonicalBrowsingContext*
CanonicalBrowsingContext::GetParentCrossChromeBoundary() {
  if (GetParent()) {
    return Cast(GetParent());
  }
  if (auto* embedder = GetEmbedderElement()) {
    return Cast(embedder->OwnerDoc()->GetBrowsingContext());
  }
  return nullptr;
}

CanonicalBrowsingContext* CanonicalBrowsingContext::TopCrossChromeBoundary() {
  CanonicalBrowsingContext* bc = this;
  while (auto* parent = bc->GetParentCrossChromeBoundary()) {
    bc = parent;
  }
  return bc;
}

Nullable<WindowProxyHolder> CanonicalBrowsingContext::GetTopChromeWindow() {
  RefPtr<CanonicalBrowsingContext> bc = TopCrossChromeBoundary();
  if (bc->IsChrome()) {
    return WindowProxyHolder(bc.forget());
  }
  return nullptr;
}

nsISHistory* CanonicalBrowsingContext::GetSessionHistory() {
  if (!IsTop()) {
    return Cast(Top())->GetSessionHistory();
  }

  // Check GetChildSessionHistory() to make sure that this BrowsingContext has
  // session history enabled.
  if (!mSessionHistory && GetChildSessionHistory()) {
    mSessionHistory = new nsSHistory(this);
  }

  return mSessionHistory;
}

SessionHistoryEntry* CanonicalBrowsingContext::GetActiveSessionHistoryEntry() {
  return mActiveEntry;
}

void CanonicalBrowsingContext::SetActiveSessionHistoryEntry(
    SessionHistoryEntry* aEntry) {
  mActiveEntry = aEntry;
}

bool CanonicalBrowsingContext::HasHistoryEntry(nsISHEntry* aEntry) {
  // XXX Should we check also loading entries?
  return aEntry && mActiveEntry == aEntry;
}

void CanonicalBrowsingContext::SwapHistoryEntries(nsISHEntry* aOldEntry,
                                                  nsISHEntry* aNewEntry) {
  // XXX Should we check also loading entries?
  if (mActiveEntry == aOldEntry) {
    nsCOMPtr<SessionHistoryEntry> newEntry = do_QueryInterface(aNewEntry);
    mActiveEntry = newEntry.forget();
  }
}

void CanonicalBrowsingContext::AddLoadingSessionHistoryEntry(
    uint64_t aLoadId, SessionHistoryEntry* aEntry) {
  Unused << SetHistoryID(aEntry->DocshellID());
  mLoadingEntries.AppendElement(LoadingSessionHistoryEntry{aLoadId, aEntry});
}

void CanonicalBrowsingContext::GetLoadingSessionHistoryInfoFromParent(
    Maybe<LoadingSessionHistoryInfo>& aLoadingInfo) {
  nsISHistory* shistory = GetSessionHistory();
  if (!shistory || !GetParent()) {
    return;
  }

  SessionHistoryEntry* parentSHE =
      GetParent()->Canonical()->GetActiveSessionHistoryEntry();
  if (parentSHE) {
    int32_t index = -1;
    for (BrowsingContext* sibling : GetParent()->Children()) {
      ++index;
      if (sibling == this) {
        nsCOMPtr<nsISHEntry> shEntry;
        parentSHE->GetChildSHEntryIfHasNoDynamicallyAddedChild(
            index, getter_AddRefs(shEntry));
        nsCOMPtr<SessionHistoryEntry> she = do_QueryInterface(shEntry);
        if (she) {
          aLoadingInfo.emplace(she);
          mLoadingEntries.AppendElement(LoadingSessionHistoryEntry{
              aLoadingInfo.value().mLoadId, she.get()});
          Unused << SetHistoryID(she->DocshellID());
        }
        break;
      }
    }
  }
}

UniquePtr<LoadingSessionHistoryInfo>
CanonicalBrowsingContext::CreateLoadingSessionHistoryEntryForLoad(
    nsDocShellLoadState* aLoadState, SessionHistoryEntry* existingEntry,
    nsIChannel* aChannel) {
  RefPtr<SessionHistoryEntry> entry;
  const LoadingSessionHistoryInfo* existingLoadingInfo =
      aLoadState->GetLoadingSessionHistoryInfo();
  MOZ_ASSERT_IF(!existingLoadingInfo, !existingEntry);
  if (existingLoadingInfo) {
    if (existingEntry) {
      entry = existingEntry;
    } else {
      MOZ_ASSERT(!existingLoadingInfo->mLoadIsFromSessionHistory);

      SessionHistoryEntry::LoadingEntry* loadingEntry =
          SessionHistoryEntry::GetByLoadId(existingLoadingInfo->mLoadId);
      MOZ_LOG(gSHLog, LogLevel::Verbose,
              ("SHEntry::GetByLoadId(%" PRIu64 ") -> %p",
               existingLoadingInfo->mLoadId, entry.get()));
      if (!loadingEntry) {
        return nullptr;
      }
      entry = loadingEntry->mEntry;
    }

    // If the entry was updated, update also the LoadingSessionHistoryInfo.
    UniquePtr<LoadingSessionHistoryInfo> lshi =
        MakeUnique<LoadingSessionHistoryInfo>(entry, existingLoadingInfo);
    aLoadState->SetLoadingSessionHistoryInfo(std::move(lshi));
    existingLoadingInfo = aLoadState->GetLoadingSessionHistoryInfo();
    Unused << SetHistoryEntryCount(entry->BCHistoryLength());
  } else if (aLoadState->LoadType() == LOAD_REFRESH &&
             !ShouldAddEntryForRefresh(aLoadState->URI(),
                                       aLoadState->PostDataStream()) &&
             mActiveEntry) {
    entry = mActiveEntry;
  } else {
    entry = new SessionHistoryEntry(aLoadState, aChannel);
    if (IsTop()) {
      // Only top level pages care about Get/SetPersist.
      entry->SetPersist(
          nsDocShell::ShouldAddToSessionHistory(aLoadState->URI(), aChannel));
    } else if (mActiveEntry || !mLoadingEntries.IsEmpty()) {
      entry->SetIsSubFrame(true);
    }
    entry->SetDocshellID(GetHistoryID());
    entry->SetIsDynamicallyAdded(CreatedDynamically());
    entry->SetForInitialLoad(true);
  }
  MOZ_DIAGNOSTIC_ASSERT(entry);

  UniquePtr<LoadingSessionHistoryInfo> loadingInfo;
  if (existingLoadingInfo) {
    loadingInfo = MakeUnique<LoadingSessionHistoryInfo>(*existingLoadingInfo);
  } else {
    loadingInfo = MakeUnique<LoadingSessionHistoryInfo>(entry);
    mLoadingEntries.AppendElement(
        LoadingSessionHistoryEntry{loadingInfo->mLoadId, entry});
  }

  MOZ_ASSERT(SessionHistoryEntry::GetByLoadId(loadingInfo->mLoadId)->mEntry ==
             entry);

  return loadingInfo;
}

UniquePtr<LoadingSessionHistoryInfo>
CanonicalBrowsingContext::ReplaceLoadingSessionHistoryEntryForLoad(
    LoadingSessionHistoryInfo* aInfo, nsIChannel* aNewChannel) {
  MOZ_ASSERT(aInfo);
  MOZ_ASSERT(aNewChannel);

  SessionHistoryInfo newInfo = SessionHistoryInfo(
      aNewChannel, aInfo->mInfo.LoadType(),
      aInfo->mInfo.GetPartitionedPrincipalToInherit(), aInfo->mInfo.GetCsp());

  for (size_t i = 0; i < mLoadingEntries.Length(); ++i) {
    if (mLoadingEntries[i].mLoadId == aInfo->mLoadId) {
      RefPtr<SessionHistoryEntry> loadingEntry = mLoadingEntries[i].mEntry;
      loadingEntry->SetInfo(&newInfo);

      if (IsTop()) {
        // Only top level pages care about Get/SetPersist.
        nsCOMPtr<nsIURI> uri;
        aNewChannel->GetURI(getter_AddRefs(uri));
        loadingEntry->SetPersist(
            nsDocShell::ShouldAddToSessionHistory(uri, aNewChannel));
      } else {
        loadingEntry->SetIsSubFrame(aInfo->mInfo.IsSubFrame());
      }
      loadingEntry->SetDocshellID(GetHistoryID());
      loadingEntry->SetIsDynamicallyAdded(CreatedDynamically());
      return MakeUnique<LoadingSessionHistoryInfo>(loadingEntry, aInfo);
    }
  }
  return nullptr;
}

mozilla::Span<const SessionHistoryInfo>
CanonicalBrowsingContext::GetContiguousSessionHistoryInfos(
    SessionHistoryInfo& aInfo) {
  MOZ_ASSERT(Navigation::IsAPIEnabled());

  nsISHistory* history = GetSessionHistory();
  if (!history) {
    return {};
  }

  mActiveContiguousEntries.ClearAndRetainStorage();
  nsSHistory::WalkContiguousEntriesInOrder(mActiveEntry, [&](auto* aEntry) {
    if (nsCOMPtr<SessionHistoryEntry> entry = do_QueryObject(aEntry)) {
      mActiveContiguousEntries.AppendElement(entry->Info());
    }
  });

  return mActiveContiguousEntries;
}

using PrintPromise = CanonicalBrowsingContext::PrintPromise;
#ifdef NS_PRINTING
// Clients must call StaticCloneForPrintingCreated or
// NoStaticCloneForPrintingWillBeCreated before the underlying promise can
// resolve.
class PrintListenerAdapter final : public nsIWebProgressListener {
 public:
  explicit PrintListenerAdapter(PrintPromise::Private* aPromise)
      : mPromise(aPromise) {}

  NS_DECL_ISUPPORTS

  // NS_DECL_NSIWEBPROGRESSLISTENER
  NS_IMETHOD OnStateChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                           uint32_t aStateFlags, nsresult aStatus) override {
    MOZ_ASSERT(NS_IsMainThread());
    if (aStateFlags & nsIWebProgressListener::STATE_STOP &&
        aStateFlags & nsIWebProgressListener::STATE_IS_DOCUMENT && mPromise) {
      mPrintJobFinished = true;
      if (mHaveSetBrowsingContext) {
        mPromise->Resolve(mClonedStaticBrowsingContext, __func__);
        mPromise = nullptr;
      }
    }
    return NS_OK;
  }
  NS_IMETHOD OnStatusChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                            nsresult aStatus,
                            const char16_t* aMessage) override {
    if (aStatus != NS_OK && mPromise) {
      mPromise->Reject(aStatus, __func__);
      mPromise = nullptr;
    }
    return NS_OK;
  }
  NS_IMETHOD OnProgressChange(nsIWebProgress* aWebProgress,
                              nsIRequest* aRequest, int32_t aCurSelfProgress,
                              int32_t aMaxSelfProgress,
                              int32_t aCurTotalProgress,
                              int32_t aMaxTotalProgress) override {
    return NS_OK;
  }
  NS_IMETHOD OnLocationChange(nsIWebProgress* aWebProgress,
                              nsIRequest* aRequest, nsIURI* aLocation,
                              uint32_t aFlags) override {
    return NS_OK;
  }
  NS_IMETHOD OnSecurityChange(nsIWebProgress* aWebProgress,
                              nsIRequest* aRequest, uint32_t aState) override {
    return NS_OK;
  }
  NS_IMETHOD OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                    nsIRequest* aRequest,
                                    uint32_t aEvent) override {
    return NS_OK;
  }

  void StaticCloneForPrintingCreated(
      MaybeDiscardedBrowsingContext&& aClonedStaticBrowsingContext) {
    MOZ_ASSERT(NS_IsMainThread());
    mClonedStaticBrowsingContext = std::move(aClonedStaticBrowsingContext);
    mHaveSetBrowsingContext = true;
    if (mPrintJobFinished && mPromise) {
      mPromise->Resolve(mClonedStaticBrowsingContext, __func__);
      mPromise = nullptr;
    }
  }

  void NoStaticCloneForPrintingWillBeCreated() {
    StaticCloneForPrintingCreated(nullptr);
  }

 private:
  ~PrintListenerAdapter() = default;

  RefPtr<PrintPromise::Private> mPromise;
  MaybeDiscardedBrowsingContext mClonedStaticBrowsingContext = nullptr;
  bool mHaveSetBrowsingContext = false;
  bool mPrintJobFinished = false;
};

NS_IMPL_ISUPPORTS(PrintListenerAdapter, nsIWebProgressListener)
#endif

already_AddRefed<Promise> CanonicalBrowsingContext::PrintJS(
    nsIPrintSettings* aPrintSettings, ErrorResult& aRv) {
  RefPtr<Promise> promise = Promise::Create(GetIncumbentGlobal(), aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return promise.forget();
  }

  Print(aPrintSettings)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [promise](MaybeDiscardedBrowsingContext) {
            promise->MaybeResolveWithUndefined();
          },
          [promise](nsresult aResult) { promise->MaybeReject(aResult); });
  return promise.forget();
}

RefPtr<PrintPromise> CanonicalBrowsingContext::Print(
    nsIPrintSettings* aPrintSettings) {
#ifndef NS_PRINTING
  return PrintPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE, __func__);
#else
// Content analysis is not supported on non-Windows platforms.
#  if defined(XP_WIN)
  bool needContentAnalysis = false;
  nsCOMPtr<nsIContentAnalysis> contentAnalysis =
      mozilla::components::nsIContentAnalysis::Service();
  Unused << NS_WARN_IF(!contentAnalysis);
  if (contentAnalysis) {
    nsresult rv = contentAnalysis->GetIsActive(&needContentAnalysis);
    Unused << NS_WARN_IF(NS_FAILED(rv));
  }
  if (needContentAnalysis) {
    auto done = MakeRefPtr<PrintPromise::Private>(__func__);
    contentanalysis::ContentAnalysis::PrintToPDFToDetermineIfPrintAllowed(
        this, aPrintSettings)
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [done, aPrintSettings = RefPtr{aPrintSettings},
             self = RefPtr{this}](
                contentanalysis::ContentAnalysis::PrintAllowedResult aResponse)
                MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA mutable {
                  if (aResponse.mAllowed) {
                    self->PrintWithNoContentAnalysis(
                            aPrintSettings, false,
                            aResponse.mCachedStaticDocumentBrowsingContext)
                        ->ChainTo(done.forget(), __func__);
                  } else {
                    // Since we are not doing the second print in this case,
                    // release the clone that is no longer needed.
                    self->ReleaseClonedPrint(
                        aResponse.mCachedStaticDocumentBrowsingContext);
                    done->Reject(NS_ERROR_CONTENT_BLOCKED, __func__);
                  }
                },
            [done, self = RefPtr{this}](
                contentanalysis::ContentAnalysis::PrintAllowedError
                    aErrorResponse) MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
              // Since we are not doing the second print in this case, release
              // the clone that is no longer needed.
              self->ReleaseClonedPrint(
                  aErrorResponse.mCachedStaticDocumentBrowsingContext);
              done->Reject(aErrorResponse.mError, __func__);
            });
    return done;
  }
#  endif
  return PrintWithNoContentAnalysis(aPrintSettings, false, nullptr);
#endif
}

void CanonicalBrowsingContext::ReleaseClonedPrint(
    const MaybeDiscardedBrowsingContext& aClonedStaticBrowsingContext) {
#ifdef NS_PRINTING
  auto* browserParent = GetBrowserParent();
  if (NS_WARN_IF(!browserParent)) {
    return;
  }
  Unused << browserParent->SendDestroyPrintClone(aClonedStaticBrowsingContext);
#endif
}

RefPtr<PrintPromise> CanonicalBrowsingContext::PrintWithNoContentAnalysis(
    nsIPrintSettings* aPrintSettings, bool aForceStaticDocument,
    const MaybeDiscardedBrowsingContext& aCachedStaticDocument) {
#ifndef NS_PRINTING
  return PrintPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE, __func__);
#else
  auto promise = MakeRefPtr<PrintPromise::Private>(__func__);
  auto listener = MakeRefPtr<PrintListenerAdapter>(promise);
  if (IsInProcess()) {
    RefPtr<nsGlobalWindowOuter> outerWindow =
        nsGlobalWindowOuter::Cast(GetDOMWindow());
    if (NS_WARN_IF(!outerWindow)) {
      promise->Reject(NS_ERROR_FAILURE, __func__);
      return promise;
    }

    ErrorResult rv;
    listener->NoStaticCloneForPrintingWillBeCreated();
    outerWindow->Print(aPrintSettings,
                       /* aRemotePrintJob = */ nullptr, listener,
                       /* aDocShellToCloneInto = */ nullptr,
                       nsGlobalWindowOuter::IsPreview::No,
                       nsGlobalWindowOuter::IsForWindowDotPrint::No,
                       /* aPrintPreviewCallback = */ nullptr,
                       /* aCachedBrowsingContext = */ nullptr, rv);
    if (rv.Failed()) {
      promise->Reject(rv.StealNSResult(), __func__);
    }
    return promise;
  }

  auto* browserParent = GetBrowserParent();
  if (NS_WARN_IF(!browserParent)) {
    promise->Reject(NS_ERROR_FAILURE, __func__);
    return promise;
  }

  nsCOMPtr<nsIPrintSettingsService> printSettingsSvc =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (NS_WARN_IF(!printSettingsSvc)) {
    promise->Reject(NS_ERROR_FAILURE, __func__);
    return promise;
  }

  nsresult rv;
  nsCOMPtr<nsIPrintSettings> printSettings = aPrintSettings;
  if (!printSettings) {
    rv =
        printSettingsSvc->CreateNewPrintSettings(getter_AddRefs(printSettings));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      promise->Reject(rv, __func__);
      return promise;
    }
  }

  embedding::PrintData printData;
  rv = printSettingsSvc->SerializeToPrintData(printSettings, &printData);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    promise->Reject(rv, __func__);
    return promise;
  }

  layout::RemotePrintJobParent* remotePrintJob =
      new layout::RemotePrintJobParent(printSettings);
  printData.remotePrintJob() =
      browserParent->Manager()->SendPRemotePrintJobConstructor(remotePrintJob);

  remotePrintJob->RegisterListener(listener);

  if (!aCachedStaticDocument.IsNullOrDiscarded()) {
    // There is no cloned static browsing context that
    // SendPrintClonedPage() will return, so indicate this
    // so listener can resolve its promise.
    listener->NoStaticCloneForPrintingWillBeCreated();
    if (NS_WARN_IF(!browserParent->SendPrintClonedPage(
            this, printData, aCachedStaticDocument))) {
      promise->Reject(NS_ERROR_FAILURE, __func__);
    }
  } else {
    RefPtr<PBrowserParent::PrintPromise> printPromise =
        browserParent->SendPrint(this, printData, aForceStaticDocument);
    printPromise->Then(
        GetMainThreadSerialEventTarget(), __func__,
        [listener](MaybeDiscardedBrowsingContext cachedStaticDocument) {
          // promise will get resolved by the listener
          listener->StaticCloneForPrintingCreated(
              std::move(cachedStaticDocument));
        },
        [promise](ResponseRejectReason reason) {
          NS_WARNING("SendPrint() failed");
          promise->Reject(NS_ERROR_FAILURE, __func__);
        });
  }
  return promise.forget();
#endif
}

void CanonicalBrowsingContext::CallOnTopDescendants(
    const FunctionRef<CallState(CanonicalBrowsingContext*)>& aCallback,
    TopDescendantKind aKind) {
  // Calling with All on something other than a chrome root is unlikely to be
  // what you want, so lacking a use-case for it, we assert against it for now.
  MOZ_ASSERT_IF(aKind == TopDescendantKind::All,
                IsChrome() && !GetParentCrossChromeBoundary());
  // Similarly, calling with {NonNested,All} on a non-top bc is unlikely to be
  // what you want.
  MOZ_ASSERT_IF(aKind != TopDescendantKind::ChildrenOnly, IsTop());

  if (!IsInProcess()) {
    // We rely on top levels having to be embedded in the parent process, so
    // we can only have top level descendants if embedded here...
    return;
  }

  const auto* ourTop = Top();

  AutoTArray<RefPtr<BrowsingContextGroup>, 32> groups;
  BrowsingContextGroup::GetAllGroups(groups);
  for (auto& browsingContextGroup : groups) {
    for (auto& topLevel : browsingContextGroup->Toplevels()) {
      if (topLevel == ourTop) {
        // A nested toplevel can't be a descendant of our same toplevel.
        continue;
      }

      // Walk up the CanonicalBrowsingContext tree, looking for a match.
      const bool topLevelIsRelevant = [&] {
        auto* current = topLevel->Canonical();
        while (auto* parent = current->GetParentCrossChromeBoundary()) {
          if (parent == this) {
            return true;
          }
          // If we've reached aKind's stop condition, break out early.
          if (aKind == TopDescendantKind::ChildrenOnly ||
              (aKind == TopDescendantKind::NonNested && parent->IsTop())) {
            return false;
          }
          current = parent;
        }
        return false;
      }();

      if (!topLevelIsRelevant) {
        continue;
      }

      if (aCallback(topLevel->Canonical()) == CallState::Stop) {
        return;
      }
    }
  }
}

void CanonicalBrowsingContext::SessionHistoryCommit(
    uint64_t aLoadId, const nsID& aChangeID, uint32_t aLoadType, bool aPersist,
    bool aCloneEntryChildren, bool aChannelExpired, uint32_t aCacheKey) {
  MOZ_LOG(gSHLog, LogLevel::Verbose,
          ("CanonicalBrowsingContext::SessionHistoryCommit %p %" PRIu64, this,
           aLoadId));
  MOZ_ASSERT(aLoadId != UINT64_MAX,
             "Must not send special about:blank loadinfo to parent.");
  for (size_t i = 0; i < mLoadingEntries.Length(); ++i) {
    if (mLoadingEntries[i].mLoadId == aLoadId) {
      nsSHistory* shistory = static_cast<nsSHistory*>(GetSessionHistory());
      if (!shistory) {
        SessionHistoryEntry::RemoveLoadId(aLoadId);
        mLoadingEntries.RemoveElementAt(i);
        return;
      }

      RefPtr<SessionHistoryEntry> newActiveEntry = mLoadingEntries[i].mEntry;
      if (aCacheKey != 0) {
        newActiveEntry->SetCacheKey(aCacheKey);
      }

      if (aChannelExpired) {
        newActiveEntry->SharedInfo()->mExpired = true;
      }

      bool loadFromSessionHistory = !newActiveEntry->ForInitialLoad();
      newActiveEntry->SetForInitialLoad(false);
      SessionHistoryEntry::RemoveLoadId(aLoadId);
      mLoadingEntries.RemoveElementAt(i);

      int32_t indexOfHistoryLoad = -1;
      if (loadFromSessionHistory) {
        nsCOMPtr<nsISHEntry> root = nsSHistory::GetRootSHEntry(newActiveEntry);
        indexOfHistoryLoad = shistory->GetIndexOfEntry(root);
        if (indexOfHistoryLoad < 0) {
          // Entry has been removed from the session history.
          return;
        }
      }

      CallerWillNotifyHistoryIndexAndLengthChanges caller(shistory);

      // If there is a name in the new entry, clear the name of all contiguous
      // entries. This is for https://html.spec.whatwg.org/#history-traversal
      // Step 4.4.2.
      nsAutoString nameOfNewEntry;
      newActiveEntry->GetName(nameOfNewEntry);
      if (!nameOfNewEntry.IsEmpty()) {
        nsSHistory::WalkContiguousEntries(
            newActiveEntry,
            [](nsISHEntry* aEntry) { aEntry->SetName(EmptyString()); });
      }

      bool addEntry = ShouldUpdateSessionHistory(aLoadType);
      if (IsTop()) {
        if (mActiveEntry && !mActiveEntry->GetFrameLoader()) {
          bool sharesDocument = true;
          mActiveEntry->SharesDocumentWith(newActiveEntry, &sharesDocument);
          if (!sharesDocument) {
            // If the old page won't be in the bfcache,
            // clear the dynamic entries.
            RemoveDynEntriesFromActiveSessionHistoryEntry();
          }
        }

        if (LOAD_TYPE_HAS_FLAGS(aLoadType,
                                nsIWebNavigation::LOAD_FLAGS_REPLACE_HISTORY)) {
          // Replace the current entry with the new entry.
          int32_t index = shistory->GetIndexForReplace();

          // If we're trying to replace an inexistant shistory entry then we
          // should append instead.
          addEntry = index < 0;
          if (!addEntry) {
            shistory->ReplaceEntry(index, newActiveEntry);
          }
          mActiveEntry = newActiveEntry;
        } else if (LOAD_TYPE_HAS_FLAGS(
                       aLoadType, nsIWebNavigation::LOAD_FLAGS_IS_REFRESH) &&
                   !ShouldAddEntryForRefresh(newActiveEntry) && mActiveEntry) {
          addEntry = false;
          mActiveEntry->ReplaceWith(*newActiveEntry);
        } else {
          mActiveEntry = newActiveEntry;
        }

        if (loadFromSessionHistory) {
          // XXX Synchronize browsing context tree and session history tree?
          shistory->InternalSetRequestedIndex(indexOfHistoryLoad);
          shistory->UpdateIndex();

          if (IsTop()) {
            mActiveEntry->SetWireframe(Nothing());
          }
        } else if (addEntry) {
          shistory->AddEntry(mActiveEntry, aPersist);
          shistory->InternalSetRequestedIndex(-1);
        }
      } else {
        // FIXME The old implementations adds it to the parent's mLSHE if there
        //       is one, need to figure out if that makes sense here (peterv
        //       doesn't think it would).
        if (loadFromSessionHistory) {
          if (mActiveEntry) {
            // mActiveEntry is null if we're loading iframes from session
            // history while also parent page is loading from session history.
            // In that case there isn't anything to sync.
            mActiveEntry->SyncTreesForSubframeNavigation(newActiveEntry, Top(),
                                                         this);
          }
          mActiveEntry = newActiveEntry;

          shistory->InternalSetRequestedIndex(indexOfHistoryLoad);
          // FIXME UpdateIndex() here may update index too early (but even the
          //       old implementation seems to have similar issues).
          shistory->UpdateIndex();
        } else if (addEntry) {
          if (mActiveEntry) {
            if (LOAD_TYPE_HAS_FLAGS(
                    aLoadType, nsIWebNavigation::LOAD_FLAGS_REPLACE_HISTORY) ||
                (LOAD_TYPE_HAS_FLAGS(aLoadType,
                                     nsIWebNavigation::LOAD_FLAGS_IS_REFRESH) &&
                 !ShouldAddEntryForRefresh(newActiveEntry))) {
              // FIXME We need to make sure that when we create the info we
              //       make a copy of the shared state.
              mActiveEntry->ReplaceWith(*newActiveEntry);
            } else {
              // AddChildSHEntryHelper does update the index of the session
              // history!
              shistory->AddChildSHEntryHelper(mActiveEntry, newActiveEntry,
                                              Top(), aCloneEntryChildren);
              mActiveEntry = newActiveEntry;
            }
          } else {
            SessionHistoryEntry* parentEntry = GetParent()->mActiveEntry;
            // XXX What should happen if parent doesn't have mActiveEntry?
            //     Or can that even happen ever?
            if (parentEntry) {
              mActiveEntry = newActiveEntry;
              // FIXME Using IsInProcess for aUseRemoteSubframes isn't quite
              //       right, but aUseRemoteSubframes should be going away.
              parentEntry->AddChild(
                  mActiveEntry,
                  CreatedDynamically() ? -1 : GetParent()->IndexOf(this),
                  IsInProcess());
            }
          }
          shistory->InternalSetRequestedIndex(-1);
        }
      }

      ResetSHEntryHasUserInteractionCache();

      HistoryCommitIndexAndLength(aChangeID, caller);

      shistory->LogHistory();

      return;
    }
    // XXX Should the loading entries before [i] be removed?
  }
  // FIXME Should we throw an error if we don't find an entry for
  // aSessionHistoryEntryId?
}

already_AddRefed<nsDocShellLoadState> CanonicalBrowsingContext::CreateLoadInfo(
    SessionHistoryEntry* aEntry) {
  const SessionHistoryInfo& info = aEntry->Info();
  RefPtr<nsDocShellLoadState> loadState(new nsDocShellLoadState(info.GetURI()));
  info.FillLoadInfo(*loadState);
  UniquePtr<LoadingSessionHistoryInfo> loadingInfo;
  loadingInfo = MakeUnique<LoadingSessionHistoryInfo>(aEntry);
  mLoadingEntries.AppendElement(
      LoadingSessionHistoryEntry{loadingInfo->mLoadId, aEntry});
  loadState->SetLoadingSessionHistoryInfo(std::move(loadingInfo));

  return loadState.forget();
}

void CanonicalBrowsingContext::NotifyOnHistoryReload(
    bool aForceReload, bool& aCanReload,
    Maybe<NotNull<RefPtr<nsDocShellLoadState>>>& aLoadState,
    Maybe<bool>& aReloadActiveEntry) {
  MOZ_DIAGNOSTIC_ASSERT(!aLoadState);

  aCanReload = true;
  nsISHistory* shistory = GetSessionHistory();
  NS_ENSURE_TRUE_VOID(shistory);

  shistory->NotifyOnHistoryReload(&aCanReload);
  if (!aCanReload) {
    return;
  }

  if (mActiveEntry) {
    aLoadState.emplace(WrapMovingNotNull(RefPtr{CreateLoadInfo(mActiveEntry)}));
    aReloadActiveEntry.emplace(true);
    if (aForceReload) {
      shistory->RemoveFrameEntries(mActiveEntry);
    }
  } else if (!mLoadingEntries.IsEmpty()) {
    const LoadingSessionHistoryEntry& loadingEntry =
        mLoadingEntries.LastElement();
    uint64_t loadId = loadingEntry.mLoadId;
    aLoadState.emplace(
        WrapMovingNotNull(RefPtr{CreateLoadInfo(loadingEntry.mEntry)}));
    aReloadActiveEntry.emplace(false);
    if (aForceReload) {
      SessionHistoryEntry::LoadingEntry* entry =
          SessionHistoryEntry::GetByLoadId(loadId);
      if (entry) {
        shistory->RemoveFrameEntries(entry->mEntry);
      }
    }
  }

  if (aLoadState) {
    // Use 0 as the offset, since aLoadState will be be used for reload.
    aLoadState.ref()->SetLoadIsFromSessionHistory(0,
                                                  aReloadActiveEntry.value());
  }
  // If we don't have an active entry and we don't have a loading entry then
  // the nsDocShell will create a load state based on its document.
}

void CanonicalBrowsingContext::SetActiveSessionHistoryEntry(
    const Maybe<nsPoint>& aPreviousScrollPos, SessionHistoryInfo* aInfo,
    uint32_t aLoadType, uint32_t aUpdatedCacheKey, const nsID& aChangeID) {
  nsISHistory* shistory = GetSessionHistory();
  if (!shistory) {
    return;
  }
  CallerWillNotifyHistoryIndexAndLengthChanges caller(shistory);

  RefPtr<SessionHistoryEntry> oldActiveEntry = mActiveEntry;
  if (aPreviousScrollPos.isSome() && oldActiveEntry) {
    oldActiveEntry->SetScrollPosition(aPreviousScrollPos.ref().x,
                                      aPreviousScrollPos.ref().y);
  }
  mActiveEntry = new SessionHistoryEntry(aInfo);
  mActiveEntry->SetDocshellID(GetHistoryID());
  mActiveEntry->AdoptBFCacheEntry(oldActiveEntry);
  if (aUpdatedCacheKey != 0) {
    mActiveEntry->SharedInfo()->mCacheKey = aUpdatedCacheKey;
  }

  if (IsTop()) {
    Maybe<int32_t> previousEntryIndex, loadedEntryIndex;
    shistory->AddToRootSessionHistory(
        true, oldActiveEntry, this, mActiveEntry, aLoadType,
        nsDocShell::ShouldAddToSessionHistory(aInfo->GetURI(), nullptr),
        &previousEntryIndex, &loadedEntryIndex);
  } else {
    if (oldActiveEntry) {
      shistory->AddChildSHEntryHelper(oldActiveEntry, mActiveEntry, Top(),
                                      true);
    } else if (GetParent() && GetParent()->mActiveEntry) {
      GetParent()->mActiveEntry->AddChild(
          mActiveEntry, CreatedDynamically() ? -1 : GetParent()->IndexOf(this),
          UseRemoteSubframes());
    }
  }

  ResetSHEntryHasUserInteractionCache();

  shistory->InternalSetRequestedIndex(-1);

  // FIXME Need to do the equivalent of EvictDocumentViewersOrReplaceEntry.
  HistoryCommitIndexAndLength(aChangeID, caller);

  static_cast<nsSHistory*>(shistory)->LogHistory();
}

void CanonicalBrowsingContext::ReplaceActiveSessionHistoryEntry(
    SessionHistoryInfo* aInfo) {
  if (!mActiveEntry) {
    return;
  }

  // aInfo comes from the entry stored in the current document's docshell, whose
  // interaction state does not get updated. So we instead propagate state from
  // the previous canonical entry. See bug 1917369.
  const bool hasUserInteraction = mActiveEntry->GetHasUserInteraction();
  mActiveEntry->SetInfo(aInfo);
  mActiveEntry->SetHasUserInteraction(hasUserInteraction);
  // Notify children of the update
  nsSHistory* shistory = static_cast<nsSHistory*>(GetSessionHistory());
  if (shistory) {
    shistory->NotifyOnHistoryReplaceEntry();
  }

  ResetSHEntryHasUserInteractionCache();

  if (IsTop()) {
    mActiveEntry->SetWireframe(Nothing());
  }

  // FIXME Need to do the equivalent of EvictDocumentViewersOrReplaceEntry.
}

void CanonicalBrowsingContext::RemoveDynEntriesFromActiveSessionHistoryEntry() {
  nsISHistory* shistory = GetSessionHistory();
  // In theory shistory can be null here if the method is called right after
  // CanonicalBrowsingContext::ReplacedBy call.
  NS_ENSURE_TRUE_VOID(shistory);
  nsCOMPtr<nsISHEntry> root = nsSHistory::GetRootSHEntry(mActiveEntry);
  shistory->RemoveDynEntries(shistory->GetIndexOfEntry(root), mActiveEntry);
}

void CanonicalBrowsingContext::RemoveFromSessionHistory(const nsID& aChangeID) {
  nsSHistory* shistory = static_cast<nsSHistory*>(GetSessionHistory());
  if (shistory) {
    CallerWillNotifyHistoryIndexAndLengthChanges caller(shistory);
    nsCOMPtr<nsISHEntry> root = nsSHistory::GetRootSHEntry(mActiveEntry);
    bool didRemove;
    AutoTArray<nsID, 16> ids({GetHistoryID()});
    shistory->RemoveEntries(ids, shistory->GetIndexOfEntry(root), &didRemove);
    if (didRemove) {
      RefPtr<BrowsingContext> rootBC = shistory->GetBrowsingContext();
      if (rootBC) {
        if (!rootBC->IsInProcess()) {
          if (ContentParent* cp = rootBC->Canonical()->GetContentParent()) {
            Unused << cp->SendDispatchLocationChangeEvent(rootBC);
          }
        } else if (rootBC->GetDocShell()) {
          rootBC->GetDocShell()->DispatchLocationChangeEvent();
        }
      }
    }
    HistoryCommitIndexAndLength(aChangeID, caller);
  }
}

Maybe<int32_t> CanonicalBrowsingContext::HistoryGo(
    int32_t aOffset, uint64_t aHistoryEpoch, bool aRequireUserInteraction,
    bool aUserActivation, Maybe<ContentParentId> aContentId) {
  if (aRequireUserInteraction && aOffset != -1 && aOffset != 1) {
    NS_ERROR(
        "aRequireUserInteraction may only be used with an offset of -1 or 1");
    return Nothing();
  }

  nsSHistory* shistory = static_cast<nsSHistory*>(GetSessionHistory());
  if (!shistory) {
    return Nothing();
  }

  CheckedInt<int32_t> index = shistory->GetRequestedIndex() >= 0
                                  ? shistory->GetRequestedIndex()
                                  : shistory->Index();
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("HistoryGo(%d->%d) epoch %" PRIu64 "/id %" PRIu64, aOffset,
           (index + aOffset).value(), aHistoryEpoch,
           (uint64_t)(aContentId.isSome() ? aContentId.value() : 0)));

  while (true) {
    index += aOffset;
    if (!index.isValid()) {
      MOZ_LOG(gSHLog, LogLevel::Debug, ("Invalid index"));
      return Nothing();
    }

    // Check for user interaction if desired, except for the first and last
    // history entries. We compare with >= to account for the case where
    // aOffset >= length.
    if (!StaticPrefs::browser_navigation_requireUserInteraction() ||
        !aRequireUserInteraction || index.value() >= shistory->Length() - 1 ||
        index.value() <= 0) {
      break;
    }
    if (shistory->HasUserInteractionAtIndex(index.value())) {
      break;
    }
  }

  // Implement aborting additional history navigations from within the same
  // event spin of the content process.

  uint64_t epoch;
  bool sameEpoch = false;
  Maybe<ContentParentId> id;
  shistory->GetEpoch(epoch, id);

  if (aContentId == id && epoch >= aHistoryEpoch) {
    sameEpoch = true;
    MOZ_LOG(gSHLog, LogLevel::Debug, ("Same epoch/id"));
  }
  // Don't update the epoch until we know if the target index is valid

  // GoToIndex checks that index is >= 0 and < length.
  nsTArray<nsSHistory::LoadEntryResult> loadResults;
  nsresult rv = shistory->GotoIndex(index.value(), loadResults, sameEpoch,
                                    aOffset == 0, aUserActivation);
  if (NS_FAILED(rv)) {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("Dropping HistoryGo - bad index or same epoch (not in same doc)"));
    return Nothing();
  }
  if (epoch < aHistoryEpoch || aContentId != id) {
    MOZ_LOG(gSHLog, LogLevel::Debug, ("Set epoch"));
    shistory->SetEpoch(aHistoryEpoch, aContentId);
  }
  int32_t requestedIndex = shistory->GetRequestedIndex();
  nsSHistory::LoadURIs(loadResults);
  return Some(requestedIndex);
}

JSObject* CanonicalBrowsingContext::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return CanonicalBrowsingContext_Binding::Wrap(aCx, this, aGivenProto);
}

void CanonicalBrowsingContext::DispatchWheelZoomChange(bool aIncrease) {
  Element* element = Top()->GetEmbedderElement();
  if (!element) {
    return;
  }

  auto event = aIncrease ? u"DoZoomEnlargeBy10"_ns : u"DoZoomReduceBy10"_ns;
  auto dispatcher = MakeRefPtr<AsyncEventDispatcher>(
      element, event, CanBubble::eYes, ChromeOnlyDispatch::eYes);
  dispatcher->PostDOMEvent();
}

void CanonicalBrowsingContext::CanonicalDiscard() {
  if (mTabMediaController) {
    mTabMediaController->Shutdown();
    mTabMediaController = nullptr;
  }

  if (mCurrentLoad) {
    mCurrentLoad->Cancel(NS_BINDING_ABORTED,
                         "CanonicalBrowsingContext::CanonicalDiscard"_ns);
  }

  if (mWebProgress) {
    RefPtr<BrowsingContextWebProgress> progress = mWebProgress;
    progress->ContextDiscarded();
  }

  if (IsTop()) {
    BackgroundSessionStorageManager::RemoveManager(Id());
  }

  CancelSessionStoreUpdate();

  if (UsePrivateBrowsing() && EverAttached() && IsContent()) {
    DecreasePrivateCount();
  }
}

void CanonicalBrowsingContext::CanonicalAttach() {
  if (UsePrivateBrowsing() && IsContent()) {
    IncreasePrivateCount();
  }
}

void CanonicalBrowsingContext::AddPendingDiscard() {
  MOZ_ASSERT(!mFullyDiscarded);
  mPendingDiscards++;
}

void CanonicalBrowsingContext::RemovePendingDiscard() {
  mPendingDiscards--;
  if (!mPendingDiscards) {
    mFullyDiscarded = true;
    auto listeners = std::move(mFullyDiscardedListeners);
    for (const auto& listener : listeners) {
      listener(Id());
    }
  }
}

void CanonicalBrowsingContext::AddFinalDiscardListener(
    std::function<void(uint64_t)>&& aListener) {
  if (mFullyDiscarded) {
    aListener(Id());
    return;
  }
  mFullyDiscardedListeners.AppendElement(std::move(aListener));
}

void CanonicalBrowsingContext::SetForceAppWindowActive(bool aForceActive,
                                                       ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(IsChrome());
  MOZ_DIAGNOSTIC_ASSERT(IsTop());
  if (!IsChrome() || !IsTop()) {
    return aRv.ThrowNotAllowedError(
        "You shouldn't need to force this BrowsingContext to be active, use "
        ".isActive instead");
  }
  if (mForceAppWindowActive == aForceActive) {
    return;
  }
  mForceAppWindowActive = aForceActive;
  RecomputeAppWindowVisibility();
}

void CanonicalBrowsingContext::RecomputeAppWindowVisibility() {
  MOZ_RELEASE_ASSERT(IsChrome());
  MOZ_RELEASE_ASSERT(IsTop());

  const bool wasAlreadyActive = IsActive();

  nsCOMPtr<nsIWidget> widget;
  if (auto* docShell = GetDocShell()) {
    nsDocShell::Cast(docShell)->GetMainWidget(getter_AddRefs(widget));
  }

  Unused << NS_WARN_IF(!widget);
  const bool isNowActive =
      ForceAppWindowActive() || (widget && !widget->IsFullyOccluded() &&
                                 widget->SizeMode() != nsSizeMode_Minimized);

  if (isNowActive == wasAlreadyActive) {
    return;
  }

  SetIsActiveInternal(isNowActive, IgnoreErrors());
  if (widget) {
    // Pause if we are not active, resume if we are active.
    widget->PauseOrResumeCompositor(!isNowActive);
  }
}

void CanonicalBrowsingContext::AdjustPrivateBrowsingCount(
    bool aPrivateBrowsing) {
  if (IsDiscarded() || !EverAttached() || IsChrome()) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(aPrivateBrowsing == UsePrivateBrowsing());
  if (aPrivateBrowsing) {
    IncreasePrivateCount();
  } else {
    DecreasePrivateCount();
  }
}

void CanonicalBrowsingContext::NotifyStartDelayedAutoplayMedia() {
  WindowContext* windowContext = GetCurrentWindowContext();
  if (!windowContext) {
    return;
  }

  // As this function would only be called when user click the play icon on the
  // tab bar. That's clear user intent to play, so gesture activate the window
  // context so that the block-autoplay logic allows the media to autoplay.
  windowContext->NotifyUserGestureActivation();
  AUTOPLAY_LOG("NotifyStartDelayedAutoplayMedia for chrome bc 0x%08" PRIx64,
               Id());
  StartDelayedAutoplayMediaComponents();
  // Notfiy all content browsing contexts which are related with the canonical
  // browsing content tree to start delayed autoplay media.

  Group()->EachParent([&](ContentParent* aParent) {
    Unused << aParent->SendStartDelayedAutoplayMediaComponents(this);
  });
}

void CanonicalBrowsingContext::NotifyMediaMutedChanged(bool aMuted,
                                                       ErrorResult& aRv) {
  MOZ_ASSERT(!GetParent(),
             "Notify media mute change on non top-level context!");
  SetMuted(aMuted, aRv);
}

uint32_t CanonicalBrowsingContext::CountSiteOrigins(
    GlobalObject& aGlobal,
    const Sequence<OwningNonNull<BrowsingContext>>& aRoots) {
  nsTHashSet<nsCString> uniqueSiteOrigins;

  for (const auto& root : aRoots) {
    root->PreOrderWalk([&](BrowsingContext* aContext) {
      WindowGlobalParent* windowGlobalParent =
          aContext->Canonical()->GetCurrentWindowGlobal();
      if (windowGlobalParent) {
        nsIPrincipal* documentPrincipal =
            windowGlobalParent->DocumentPrincipal();

        bool isContentPrincipal = documentPrincipal->GetIsContentPrincipal();
        if (isContentPrincipal) {
          nsCString siteOrigin;
          documentPrincipal->GetSiteOrigin(siteOrigin);
          uniqueSiteOrigins.Insert(siteOrigin);
        }
      }
    });
  }

  return uniqueSiteOrigins.Count();
}

/* static */
bool CanonicalBrowsingContext::IsPrivateBrowsingActive() {
  return gNumberOfPrivateContexts > 0;
}

void CanonicalBrowsingContext::UpdateMediaControlAction(
    const MediaControlAction& aAction) {
  if (IsDiscarded()) {
    return;
  }
  ContentMediaControlKeyHandler::HandleMediaControlAction(this, aAction);
  Group()->EachParent([&](ContentParent* aParent) {
    Unused << aParent->SendUpdateMediaControlAction(this, aAction);
  });
}

void CanonicalBrowsingContext::LoadURI(nsIURI* aURI,
                                       const LoadURIOptions& aOptions,
                                       ErrorResult& aError) {
  RefPtr<nsDocShellLoadState> loadState;
  nsresult rv = nsDocShellLoadState::CreateFromLoadURIOptions(
      this, aURI, aOptions, getter_AddRefs(loadState));
  MOZ_ASSERT(rv != NS_ERROR_MALFORMED_URI);

  if (NS_FAILED(rv)) {
    aError.Throw(rv);
    return;
  }

  LoadURI(loadState, true);
}

void CanonicalBrowsingContext::FixupAndLoadURIString(
    const nsAString& aURI, const LoadURIOptions& aOptions,
    ErrorResult& aError) {
  RefPtr<nsDocShellLoadState> loadState;
  nsresult rv = nsDocShellLoadState::CreateFromLoadURIOptions(
      this, aURI, aOptions, getter_AddRefs(loadState));

  if (rv == NS_ERROR_MALFORMED_URI) {
    DisplayLoadError(aURI);
    return;
  }

  if (NS_FAILED(rv)) {
    aError.Throw(rv);
    return;
  }

  LoadURI(loadState, true);
}

void CanonicalBrowsingContext::GoBack(
    const Optional<int32_t>& aCancelContentJSEpoch,
    bool aRequireUserInteraction, bool aUserActivation) {
  if (IsDiscarded()) {
    return;
  }

  // Stop any known network loads if necessary.
  if (mCurrentLoad) {
    mCurrentLoad->Cancel(NS_BINDING_CANCELLED_OLD_LOAD, ""_ns);
  }

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(GetDocShell())) {
    if (aCancelContentJSEpoch.WasPassed()) {
      docShell->SetCancelContentJSEpoch(aCancelContentJSEpoch.Value());
    }
    docShell->GoBack(aRequireUserInteraction, aUserActivation);
  } else if (ContentParent* cp = GetContentParent()) {
    Maybe<int32_t> cancelContentJSEpoch;
    if (aCancelContentJSEpoch.WasPassed()) {
      cancelContentJSEpoch = Some(aCancelContentJSEpoch.Value());
    }
    Unused << cp->SendGoBack(this, cancelContentJSEpoch,
                             aRequireUserInteraction, aUserActivation);
  }
}
void CanonicalBrowsingContext::GoForward(
    const Optional<int32_t>& aCancelContentJSEpoch,
    bool aRequireUserInteraction, bool aUserActivation) {
  if (IsDiscarded()) {
    return;
  }

  // Stop any known network loads if necessary.
  if (mCurrentLoad) {
    mCurrentLoad->Cancel(NS_BINDING_CANCELLED_OLD_LOAD, ""_ns);
  }

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(GetDocShell())) {
    if (aCancelContentJSEpoch.WasPassed()) {
      docShell->SetCancelContentJSEpoch(aCancelContentJSEpoch.Value());
    }
    docShell->GoForward(aRequireUserInteraction, aUserActivation);
  } else if (ContentParent* cp = GetContentParent()) {
    Maybe<int32_t> cancelContentJSEpoch;
    if (aCancelContentJSEpoch.WasPassed()) {
      cancelContentJSEpoch.emplace(aCancelContentJSEpoch.Value());
    }
    Unused << cp->SendGoForward(this, cancelContentJSEpoch,
                                aRequireUserInteraction, aUserActivation);
  }
}
void CanonicalBrowsingContext::GoToIndex(
    int32_t aIndex, const Optional<int32_t>& aCancelContentJSEpoch,
    bool aUserActivation) {
  if (IsDiscarded()) {
    return;
  }

  // Stop any known network loads if necessary.
  if (mCurrentLoad) {
    mCurrentLoad->Cancel(NS_BINDING_CANCELLED_OLD_LOAD, ""_ns);
  }

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(GetDocShell())) {
    if (aCancelContentJSEpoch.WasPassed()) {
      docShell->SetCancelContentJSEpoch(aCancelContentJSEpoch.Value());
    }
    docShell->GotoIndex(aIndex, aUserActivation);
  } else if (ContentParent* cp = GetContentParent()) {
    Maybe<int32_t> cancelContentJSEpoch;
    if (aCancelContentJSEpoch.WasPassed()) {
      cancelContentJSEpoch.emplace(aCancelContentJSEpoch.Value());
    }
    Unused << cp->SendGoToIndex(this, aIndex, cancelContentJSEpoch,
                                aUserActivation);
  }
}

void CanonicalBrowsingContext::Reload(uint32_t aReloadFlags) {
  if (IsDiscarded()) {
    return;
  }

  // Stop any known network loads if necessary.
  if (mCurrentLoad) {
    mCurrentLoad->Cancel(NS_BINDING_CANCELLED_OLD_LOAD, ""_ns);
  }

  if (RefPtr<nsDocShell> docShell = nsDocShell::Cast(GetDocShell())) {
    docShell->Reload(aReloadFlags);
  } else if (ContentParent* cp = GetContentParent()) {
    Unused << cp->SendReload(this, aReloadFlags);
  }
}

void CanonicalBrowsingContext::Stop(uint32_t aStopFlags) {
  if (IsDiscarded()) {
    return;
  }

  // Stop any known network loads if necessary.
  if (mCurrentLoad && (aStopFlags & nsIWebNavigation::STOP_NETWORK)) {
    mCurrentLoad->Cancel(NS_BINDING_ABORTED,
                         "CanonicalBrowsingContext::Stop"_ns);
  }

  // Ask the docshell to stop to handle loads that haven't
  // yet reached here, as well as non-network activity.
  if (auto* docShell = nsDocShell::Cast(GetDocShell())) {
    docShell->Stop(aStopFlags);
  } else if (ContentParent* cp = GetContentParent()) {
    Unused << cp->SendStopLoad(this, aStopFlags);
  }
}

void CanonicalBrowsingContext::PendingRemotenessChange::ProcessLaunched() {
  if (!mPromise) {
    return;
  }

  if (mContentParentKeepAlive) {
    // If our new content process is still unloading from a previous process
    // switch, wait for that unload to complete before continuing.
    auto found = mTarget->FindUnloadingHost(mContentParentKeepAlive->ChildID());
    if (found != mTarget->mUnloadingHosts.end()) {
      found->mCallbacks.AppendElement(
          [self = RefPtr{this}]()
              MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA { self->ProcessReady(); });
      return;
    }
  }

  ProcessReady();
}

void CanonicalBrowsingContext::PendingRemotenessChange::ProcessReady() {
  if (!mPromise) {
    return;
  }

  MOZ_ASSERT(!mProcessReady);
  mProcessReady = true;
  MaybeFinish();
}

void CanonicalBrowsingContext::PendingRemotenessChange::MaybeFinish() {
  if (!mPromise) {
    return;
  }

  if (!mProcessReady || mWaitingForPrepareToChange) {
    return;
  }

  // If this BrowsingContext is embedded within the parent process, perform the
  // process switch directly.
  nsresult rv = mTarget->IsTopContent() ? FinishTopContent() : FinishSubframe();
  if (NS_FAILED(rv)) {
    NS_WARNING("Error finishing PendingRemotenessChange!");
    Cancel(rv);
  } else {
    Clear();
  }
}

// Logic for finishing a toplevel process change embedded within the parent
// process. Due to frontend integration the logic differs substantially from
// subframe process switches, and is handled separately.
nsresult CanonicalBrowsingContext::PendingRemotenessChange::FinishTopContent() {
  MOZ_DIAGNOSTIC_ASSERT(mTarget->IsTop(),
                        "We shouldn't be trying to change the remoteness of "
                        "non-remote iframes");

  // Abort if our ContentParent died while process switching.
  if (mContentParentKeepAlive &&
      NS_WARN_IF(mContentParentKeepAlive->IsShuttingDown())) {
    return NS_ERROR_FAILURE;
  }

  // While process switching, we need to check if any of our ancestors are
  // discarded or no longer current, in which case the process switch needs to
  // be aborted.
  RefPtr<CanonicalBrowsingContext> target(mTarget);
  if (target->IsDiscarded() || !target->AncestorsAreCurrent()) {
    return NS_ERROR_FAILURE;
  }

  Element* browserElement = target->GetEmbedderElement();
  if (!browserElement) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIBrowser> browser = browserElement->AsBrowser();
  if (!browser) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsFrameLoaderOwner> frameLoaderOwner = do_QueryObject(browserElement);
  MOZ_RELEASE_ASSERT(frameLoaderOwner,
                     "embedder browser must be nsFrameLoaderOwner");

  // If we're process switching a browsing context in private browsing
  // mode we might decrease the private browsing count to '0', which
  // would make us fire "last-pb-context-exited" and drop the private
  // session. To prevent that we artificially increment the number of
  // private browsing contexts with '1' until the process switch is done.
  bool usePrivateBrowsing = mTarget->UsePrivateBrowsing();
  if (usePrivateBrowsing) {
    IncreasePrivateCount();
  }

  auto restorePrivateCount = MakeScopeExit([usePrivateBrowsing]() {
    if (usePrivateBrowsing) {
      DecreasePrivateCount();
    }
  });

  // Tell frontend code that this browser element is about to change process.
  nsresult rv = browser->BeforeChangeRemoteness();
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Some frontend code checks the value of the `remote` attribute on the
  // browser to determine if it is remote, so update the value.
  browserElement->SetAttr(kNameSpaceID_None, nsGkAtoms::remote,
                          mContentParentKeepAlive ? u"true"_ns : u"false"_ns,
                          /* notify */ true);

  // The process has been created, hand off to nsFrameLoaderOwner to finish
  // the process switch.
  ErrorResult error;
  RefPtr keepAlive = mContentParentKeepAlive.get();
  RefPtr specificGroup = mSpecificGroup;
  frameLoaderOwner->ChangeRemotenessToProcess(keepAlive, mOptions,
                                              specificGroup, error);
  if (error.Failed()) {
    return error.StealNSResult();
  }

  // Tell frontend the load is done.
  bool loadResumed = false;
  rv = browser->FinishChangeRemoteness(mPendingSwitchId, &loadResumed);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // We did it! The process switch is complete.
  RefPtr<nsFrameLoader> frameLoader = frameLoaderOwner->GetFrameLoader();
  RefPtr<BrowserParent> newBrowser = frameLoader->GetBrowserParent();
  if (!newBrowser) {
    if (mContentParentKeepAlive) {
      // Failed to create the BrowserParent somehow! Abort the process switch
      // attempt.
      return NS_ERROR_UNEXPECTED;
    }

    if (!loadResumed) {
      RefPtr<nsDocShell> newDocShell = frameLoader->GetDocShell(error);
      if (error.Failed()) {
        return error.StealNSResult();
      }

      rv = newDocShell->ResumeRedirectedLoad(mPendingSwitchId,
                                             /* aHistoryIndex */ -1);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  } else if (!loadResumed) {
    newBrowser->ResumeLoad(mPendingSwitchId);
  }

  mPromise->Resolve(
      std::pair{newBrowser,
                RefPtr{frameLoader->GetBrowsingContext()->Canonical()}},
      __func__);
  return NS_OK;
}

nsresult CanonicalBrowsingContext::PendingRemotenessChange::FinishSubframe() {
  MOZ_DIAGNOSTIC_ASSERT(!mOptions.mReplaceBrowsingContext,
                        "Cannot replace BC for subframe");
  MOZ_DIAGNOSTIC_ASSERT(!mTarget->IsTop());

  // While process switching, we need to check if any of our ancestors are
  // discarded or no longer current, in which case the process switch needs to
  // be aborted.
  RefPtr<CanonicalBrowsingContext> target(mTarget);
  if (target->IsDiscarded() || !target->AncestorsAreCurrent()) {
    return NS_ERROR_FAILURE;
  }

  if (NS_WARN_IF(!mContentParentKeepAlive)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<WindowGlobalParent> embedderWindow = target->GetParentWindowContext();
  if (NS_WARN_IF(!embedderWindow) || NS_WARN_IF(!embedderWindow->CanSend())) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<BrowserParent> embedderBrowser = embedderWindow->GetBrowserParent();
  if (NS_WARN_IF(!embedderBrowser)) {
    return NS_ERROR_FAILURE;
  }

  // If we're creating a new remote browser, and the host process is already
  // dead, abort the process switch.
  if (mContentParentKeepAlive != embedderBrowser->Manager() &&
      NS_WARN_IF(mContentParentKeepAlive->IsShuttingDown())) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<BrowserParent> oldBrowser = target->GetBrowserParent();
  target->SetCurrentBrowserParent(nullptr);

  // If we were in a remote frame, trigger unloading of the remote window. The
  // previous BrowserParent is registered in `mUnloadingHosts` and will only be
  // cleared when the BrowserParent is fully destroyed.
  bool wasRemote = oldBrowser && oldBrowser->GetBrowsingContext() == target;
  if (wasRemote) {
    MOZ_DIAGNOSTIC_ASSERT(oldBrowser != embedderBrowser);
    MOZ_DIAGNOSTIC_ASSERT(oldBrowser->IsDestroyed() ||
                          oldBrowser->GetBrowserBridgeParent());

    // `oldBrowser` will clear the `UnloadingHost` status once the actor has
    // been destroyed.
    if (oldBrowser->CanSend()) {
      target->StartUnloadingHost(oldBrowser->Manager()->ChildID());
      Unused << oldBrowser->SendWillChangeProcess();
      oldBrowser->Destroy();
    }
  }

  // Update which process is considered the current owner
  target->SetOwnerProcessId(mContentParentKeepAlive->ChildID());

  // If we're switching from remote to local, we don't need to create a
  // BrowserBridge, and can instead perform the switch directly.
  if (mContentParentKeepAlive == embedderBrowser->Manager()) {
    MOZ_DIAGNOSTIC_ASSERT(
        mPendingSwitchId,
        "We always have a PendingSwitchId, except for print-preview loads, "
        "which will never perform a process-switch to being in-process with "
        "their embedder");
    MOZ_DIAGNOSTIC_ASSERT(wasRemote,
                          "Attempt to process-switch from local to local?");

    target->SetCurrentBrowserParent(embedderBrowser);
    Unused << embedderWindow->SendMakeFrameLocal(target, mPendingSwitchId);
    mPromise->Resolve(std::pair{embedderBrowser, target}, __func__);
    return NS_OK;
  }

  // The BrowsingContext will be remote, either as an already-remote frame
  // changing processes, or as a local frame becoming remote. Construct a new
  // BrowserBridgeParent to host the remote content.
  target->SetCurrentBrowserParent(nullptr);

  MOZ_DIAGNOSTIC_ASSERT(target->UseRemoteTabs() && target->UseRemoteSubframes(),
                        "Not supported without fission");
  uint32_t chromeFlags = nsIWebBrowserChrome::CHROME_REMOTE_WINDOW |
                         nsIWebBrowserChrome::CHROME_FISSION_WINDOW;
  if (target->UsePrivateBrowsing()) {
    chromeFlags |= nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW;
  }

  nsCOMPtr<nsIPrincipal> initialPrincipal =
      NullPrincipal::Create(target->OriginAttributesRef());
  WindowGlobalInit windowInit =
      WindowGlobalActor::AboutBlankInitializer(target, initialPrincipal);

  // Create and initialize our new BrowserBridgeParent.
  TabId tabId(nsContentUtils::GenerateTabId());
  RefPtr<BrowserBridgeParent> bridge = new BrowserBridgeParent();
  nsresult rv =
      bridge->InitWithProcess(embedderBrowser, mContentParentKeepAlive.get(),
                              windowInit, chromeFlags, tabId);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // If we've already destroyed our previous document, make a best-effort
    // attempt to recover from this failure and show the crashed tab UI. We only
    // do this in the previously-remote case, as previously in-process frames
    // will have their navigation cancelled, and will remain visible.
    if (wasRemote) {
      target->ShowSubframeCrashedUI(oldBrowser->GetBrowserBridgeParent());
    }
    return rv;
  }

  // Tell the embedder process a remoteness change is in-process. When this is
  // acknowledged, reset the in-flight ID if it used to be an in-process load.
  RefPtr<BrowserParent> newBrowser = bridge->GetBrowserParent();
  {
    // If we weren't remote, mark our embedder window browser as unloading until
    // our embedder process has acked our MakeFrameRemote message.
    Maybe<uint64_t> clearChildID;
    if (!wasRemote) {
      clearChildID = Some(embedderBrowser->Manager()->ChildID());
      target->StartUnloadingHost(*clearChildID);
    }
    auto callback = [target, clearChildID](auto&&) {
      if (clearChildID) {
        target->ClearUnloadingHost(*clearChildID);
      }
    };

    ManagedEndpoint<PBrowserBridgeChild> endpoint =
        embedderBrowser->OpenPBrowserBridgeEndpoint(bridge);
    MOZ_DIAGNOSTIC_ASSERT(endpoint.IsValid());
    embedderWindow->SendMakeFrameRemote(target, std::move(endpoint), tabId,
                                        newBrowser->GetLayersId(), callback,
                                        callback);
  }

  // Resume the pending load in our new process.
  if (mPendingSwitchId) {
    newBrowser->ResumeLoad(mPendingSwitchId);
  }

  // We did it! The process switch is complete.
  mPromise->Resolve(std::pair{newBrowser, target}, __func__);
  return NS_OK;
}

void CanonicalBrowsingContext::PendingRemotenessChange::Cancel(nsresult aRv) {
  if (!mPromise) {
    return;
  }

  mPromise->Reject(aRv, __func__);
  Clear();
}

void CanonicalBrowsingContext::PendingRemotenessChange::Clear() {
  // Make sure we don't die while we're doing cleanup.
  RefPtr<PendingRemotenessChange> kungFuDeathGrip(this);
  if (mTarget) {
    MOZ_DIAGNOSTIC_ASSERT(mTarget->mPendingRemotenessChange == this);
    mTarget->mPendingRemotenessChange = nullptr;
  }

  // When this PendingRemotenessChange was created, it was given a
  // `mContentParentKeepAlive`.
  mContentParentKeepAlive = nullptr;

  // If we were given a specific group, stop keeping that group alive manually.
  if (mSpecificGroup) {
    mSpecificGroup->RemoveKeepAlive();
    mSpecificGroup = nullptr;
  }

  mPromise = nullptr;
  mTarget = nullptr;
}

CanonicalBrowsingContext::PendingRemotenessChange::PendingRemotenessChange(
    CanonicalBrowsingContext* aTarget, RemotenessPromise::Private* aPromise,
    uint64_t aPendingSwitchId, const NavigationIsolationOptions& aOptions)
    : mTarget(aTarget),
      mPromise(aPromise),
      mPendingSwitchId(aPendingSwitchId),
      mOptions(aOptions) {}

CanonicalBrowsingContext::PendingRemotenessChange::~PendingRemotenessChange() {
  MOZ_ASSERT(
      !mPromise && !mTarget && !mContentParentKeepAlive && !mSpecificGroup,
      "should've already been Cancel() or Complete()-ed");
}

BrowserParent* CanonicalBrowsingContext::GetBrowserParent() const {
  return mCurrentBrowserParent;
}

void CanonicalBrowsingContext::SetCurrentBrowserParent(
    BrowserParent* aBrowserParent) {
  MOZ_DIAGNOSTIC_ASSERT(!mCurrentBrowserParent || !aBrowserParent,
                        "BrowsingContext already has a current BrowserParent!");
  MOZ_DIAGNOSTIC_ASSERT_IF(aBrowserParent, aBrowserParent->CanSend());
  MOZ_DIAGNOSTIC_ASSERT_IF(aBrowserParent,
                           aBrowserParent->Manager()->ChildID() == mProcessId);

  // BrowserParent must either be directly for this BrowsingContext, or the
  // manager out our embedder WindowGlobal.
  MOZ_DIAGNOSTIC_ASSERT_IF(
      aBrowserParent && aBrowserParent->GetBrowsingContext() != this,
      GetParentWindowContext() &&
          GetParentWindowContext()->Manager() == aBrowserParent);

  if (aBrowserParent && IsTopContent() && !ManuallyManagesActiveness()) {
    aBrowserParent->SetRenderLayers(IsActive());
  }

  mCurrentBrowserParent = aBrowserParent;
}

bool CanonicalBrowsingContext::ManuallyManagesActiveness() const {
  auto* el = GetEmbedderElement();
  return el && el->IsXULElement() && el->HasAttr(nsGkAtoms::manualactiveness);
}

RefPtr<CanonicalBrowsingContext::RemotenessPromise>
CanonicalBrowsingContext::ChangeRemoteness(
    const NavigationIsolationOptions& aOptions, uint64_t aPendingSwitchId) {
  MOZ_DIAGNOSTIC_ASSERT(IsContent(),
                        "cannot change the process of chrome contexts");
  MOZ_DIAGNOSTIC_ASSERT(
      IsTop() == IsEmbeddedInProcess(0),
      "toplevel content must be embedded in the parent process");
  MOZ_DIAGNOSTIC_ASSERT(!aOptions.mReplaceBrowsingContext || IsTop(),
                        "Cannot replace BrowsingContext for subframes");
  MOZ_DIAGNOSTIC_ASSERT(
      aOptions.mSpecificGroupId == 0 || aOptions.mReplaceBrowsingContext,
      "Cannot specify group ID unless replacing BC");
  MOZ_DIAGNOSTIC_ASSERT(aPendingSwitchId || !IsTop(),
                        "Should always have aPendingSwitchId for top-level "
                        "frames");

  if (!AncestorsAreCurrent()) {
    NS_WARNING("An ancestor context is no longer current");
    return RemotenessPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  // Ensure our embedder hasn't been destroyed or asked to shutdown already.
  RefPtr<WindowGlobalParent> embedderWindowGlobal = GetEmbedderWindowGlobal();
  if (!embedderWindowGlobal) {
    NS_WARNING("Non-embedded BrowsingContext");
    return RemotenessPromise::CreateAndReject(NS_ERROR_UNEXPECTED, __func__);
  }

  if (!embedderWindowGlobal->CanSend()) {
    NS_WARNING("Embedder already been destroyed.");
    return RemotenessPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE, __func__);
  }

  RefPtr<BrowserParent> embedderBrowser =
      embedderWindowGlobal->GetBrowserParent();
  if (embedderBrowser && embedderBrowser->Manager()->IsShuttingDown()) {
    NS_WARNING("Embedder already asked to shutdown.");
    return RemotenessPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE, __func__);
  }

  if (aOptions.mRemoteType.IsEmpty() && (!IsTop() || !GetEmbedderElement())) {
    NS_WARNING("Cannot load non-remote subframes");
    return RemotenessPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  // Cancel ongoing remoteness changes.
  if (mPendingRemotenessChange) {
    mPendingRemotenessChange->Cancel(NS_ERROR_ABORT);
    MOZ_DIAGNOSTIC_ASSERT(!mPendingRemotenessChange, "Should have cleared");
  }

  auto promise = MakeRefPtr<RemotenessPromise::Private>(__func__);
  promise->UseDirectTaskDispatch(__func__);

  RefPtr<PendingRemotenessChange> change =
      new PendingRemotenessChange(this, promise, aPendingSwitchId, aOptions);
  mPendingRemotenessChange = change;

  // If we're replacing BrowsingContext, determine which BrowsingContextGroup
  // we'll switch into, taking into account load options.
  if (aOptions.mReplaceBrowsingContext) {
    change->mSpecificGroup =
        aOptions.mSpecificGroupId
            ? BrowsingContextGroup::GetOrCreate(aOptions.mSpecificGroupId)
            : BrowsingContextGroup::Create(aOptions.mShouldCrossOriginIsolate);
    change->mSpecificGroup->AddKeepAlive();
  }

  // Call `prepareToChangeRemoteness` in parallel with starting a new process
  // for <browser> loads.
  if (IsTop() && GetEmbedderElement()) {
    nsCOMPtr<nsIBrowser> browser = GetEmbedderElement()->AsBrowser();
    if (!browser) {
      change->Cancel(NS_ERROR_FAILURE);
      return promise.forget();
    }

    RefPtr<Promise> blocker;
    nsresult rv = browser->PrepareToChangeRemoteness(getter_AddRefs(blocker));
    if (NS_FAILED(rv)) {
      change->Cancel(rv);
      return promise.forget();
    }

    // Mark prepareToChange as unresolved, and wait for it to become resolved.
    if (blocker && blocker->State() != Promise::PromiseState::Resolved) {
      change->mWaitingForPrepareToChange = true;
      blocker->AddCallbacksWithCycleCollectedArgs(
          [change](JSContext*, JS::Handle<JS::Value>, ErrorResult&)
              MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                change->mWaitingForPrepareToChange = false;
                change->MaybeFinish();
              },
          [change](JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
            change->Cancel(
                Promise::TryExtractNSResultFromRejectionValue(aValue));
          });
    }
  }

  // Switching a subframe to be local within it's embedding process.
  if (embedderBrowser &&
      aOptions.mRemoteType == embedderBrowser->Manager()->GetRemoteType()) {
    MOZ_DIAGNOSTIC_ASSERT(
        aPendingSwitchId,
        "We always have a PendingSwitchId, except for print-preview loads, "
        "which will never perform a process-switch to being in-process with "
        "their embedder");
    MOZ_DIAGNOSTIC_ASSERT(!aOptions.mReplaceBrowsingContext);
    MOZ_DIAGNOSTIC_ASSERT(!aOptions.mRemoteType.IsEmpty());
    MOZ_DIAGNOSTIC_ASSERT(!change->mWaitingForPrepareToChange);
    MOZ_DIAGNOSTIC_ASSERT(!change->mSpecificGroup);

    // Switching to local, so we don't need to create a new process, and will
    // instead use our embedder process.
    change->mContentParentKeepAlive =
        embedderBrowser->Manager()->AddKeepAlive(BrowserId());
    change->ProcessLaunched();
    return promise.forget();
  }

  // Switching to the parent process.
  if (aOptions.mRemoteType.IsEmpty()) {
    change->ProcessLaunched();
    return promise.forget();
  }

  // If we're aiming to end up in a new process of the same type as our old
  // process, and then putting our previous document in the BFCache, try to stay
  // in the same process to avoid creating new processes unnecessarily.
  RefPtr<ContentParent> existingProcess = GetContentParent();
  if (existingProcess && !existingProcess->IsShuttingDown() &&
      aOptions.mReplaceBrowsingContext &&
      aOptions.mRemoteType == existingProcess->GetRemoteType()) {
    change->mContentParentKeepAlive =
        existingProcess->AddKeepAlive(BrowserId());
    change->ProcessLaunched();
    return promise.forget();
  }

  // Try to predict which BrowsingContextGroup will be used for the final load
  // in this BrowsingContext. This has to be accurate if switching into an
  // existing group, as it will control what pool of processes will be used
  // for process selection.
  //
  // It's _technically_ OK to provide a group here if we're actually going to
  // switch into a brand new group, though it's sub-optimal, as it can
  // restrict the set of processes we're using.
  BrowsingContextGroup* finalGroup =
      aOptions.mReplaceBrowsingContext ? change->mSpecificGroup.get() : Group();

  bool preferUsed =
      StaticPrefs::browser_tabs_remote_subframesPreferUsed() && !IsTop();

  change->mContentParentKeepAlive =
      ContentParent::GetNewOrUsedLaunchingBrowserProcess(
          /* aRemoteType = */ aOptions.mRemoteType,
          /* aGroup = */ finalGroup,
          /* aPriority = */ hal::PROCESS_PRIORITY_FOREGROUND,
          /* aPreferUsed = */ preferUsed,
          /* aBrowserId */ BrowserId());
  if (!change->mContentParentKeepAlive) {
    change->Cancel(NS_ERROR_FAILURE);
    return promise.forget();
  }

  if (change->mContentParentKeepAlive->IsLaunching()) {
    change->mContentParentKeepAlive
        ->WaitForLaunchAsync(/* aPriority */ hal::PROCESS_PRIORITY_FOREGROUND,
                             /* aBrowserId */ BrowserId())
        ->Then(
            GetMainThreadSerialEventTarget(), __func__,
            [change](UniqueContentParentKeepAlive)
                MOZ_CAN_RUN_SCRIPT_BOUNDARY_LAMBDA {
                  change->ProcessLaunched();
                },
            [change]() { change->Cancel(NS_ERROR_FAILURE); });
  } else {
    change->ProcessLaunched();
  }
  return promise.forget();
}

void CanonicalBrowsingContext::MaybeSetPermanentKey(Element* aEmbedder) {
  MOZ_DIAGNOSTIC_ASSERT(IsTop());

  if (aEmbedder) {
    if (nsCOMPtr<nsIBrowser> browser = aEmbedder->AsBrowser()) {
      JS::Rooted<JS::Value> key(RootingCx());
      if (NS_SUCCEEDED(browser->GetPermanentKey(&key)) && key.isObject()) {
        mPermanentKey = key;
      }
    }
  }
}

MediaController* CanonicalBrowsingContext::GetMediaController() {
  // We would only create one media controller per tab, so accessing the
  // controller via the top-level browsing context.
  if (GetParent()) {
    return Cast(Top())->GetMediaController();
  }

  MOZ_ASSERT(!GetParent(),
             "Must access the controller from the top-level browsing context!");
  // Only content browsing context can create media controller, we won't create
  // controller for chrome document, such as the browser UI.
  if (!mTabMediaController && !IsDiscarded() && IsContent()) {
    mTabMediaController = new MediaController(Id());
  }
  return mTabMediaController;
}

bool CanonicalBrowsingContext::HasCreatedMediaController() const {
  return !!mTabMediaController;
}

bool CanonicalBrowsingContext::SupportsLoadingInParent(
    nsDocShellLoadState* aLoadState, uint64_t* aOuterWindowId) {
  // We currently don't support initiating loads in the parent when they are
  // watched by devtools. This is because devtools tracks loads using content
  // process notifications, which happens after the load is initiated in this
  // case. Devtools clears all prior requests when it detects a new navigation,
  // so it drops the main document load that happened here.
  if (WatchedByDevTools()) {
    return false;
  }

  // Session-history-in-parent implementation relies currently on getting a
  // round trip through a child process.
  if (aLoadState->LoadIsFromSessionHistory()) {
    return false;
  }

  // DocumentChannel currently only supports connecting channels into the
  // content process, so we can only support schemes that will always be loaded
  // there for now. Restrict to just http(s) for simplicity.
  if (!net::SchemeIsHttpOrHttps(aLoadState->URI())) {
    return false;
  }

  if (WindowGlobalParent* global = GetCurrentWindowGlobal()) {
    nsCOMPtr<nsIURI> currentURI = global->GetDocumentURI();
    if (currentURI) {
      nsCOMPtr<nsIURI> uri = aLoadState->URI();
      bool newURIHasRef = false;
      uri->GetHasRef(&newURIHasRef);
      bool equalsExceptRef = false;
      uri->EqualsExceptRef(currentURI, &equalsExceptRef);

      if (equalsExceptRef && newURIHasRef) {
        // This navigation is same-doc WRT the current one, we should pass it
        // down to the docshell to be handled.
        return false;
      }
    }

    // If unloading the current document will cause a beforeunload listener to
    // run, then we need to start the load in that process after we fire the
    // event.
    if (PreOrderWalkFlag([&](BrowsingContext* aBC) {
          WindowContext* wc = aBC->GetCurrentWindowContext();
          if (wc && wc->HasBeforeUnload()) {
            // We can stop as soon as we know at least one beforeunload listener
            // exists.
            return WalkFlag::Stop;
          }
          return WalkFlag::Next;
        }) == WalkFlag::Stop) {
      return false;
    }

    *aOuterWindowId = global->OuterWindowId();
  }
  return true;
}

bool CanonicalBrowsingContext::LoadInParent(nsDocShellLoadState* aLoadState,
                                            bool aSetNavigating) {
  // We currently only support starting loads directly from the
  // CanonicalBrowsingContext for top-level BCs.
  // We currently only support starting loads directly from the
  // CanonicalBrowsingContext for top-level BCs.
  if (!IsTopContent() || !GetContentParent() ||
      !StaticPrefs::browser_tabs_documentchannel_parent_controlled()) {
    return false;
  }

  uint64_t outerWindowId = 0;
  if (!SupportsLoadingInParent(aLoadState, &outerWindowId)) {
    return false;
  }

  MOZ_ASSERT(!aLoadState->URI()->SchemeIs("javascript"));

  MOZ_ALWAYS_SUCCEEDS(
      SetParentInitiatedNavigationEpoch(++gParentInitiatedNavigationEpoch));
  // Note: If successful, this will recurse into StartDocumentLoad and
  // set mCurrentLoad to the DocumentLoadListener instance created.
  // Ideally in the future we will only start loads from here, and we can
  // just set this directly instead.
  return net::DocumentLoadListener::LoadInParent(this, aLoadState,
                                                 aSetNavigating);
}

bool CanonicalBrowsingContext::AttemptSpeculativeLoadInParent(
    nsDocShellLoadState* aLoadState) {
  // We currently only support starting loads directly from the
  // CanonicalBrowsingContext for top-level BCs.
  // We currently only support starting loads directly from the
  // CanonicalBrowsingContext for top-level BCs.
  if (!IsTopContent() || !GetContentParent() ||
      (StaticPrefs::browser_tabs_documentchannel_parent_controlled())) {
    return false;
  }

  uint64_t outerWindowId = 0;
  if (!SupportsLoadingInParent(aLoadState, &outerWindowId)) {
    return false;
  }

  // If we successfully open the DocumentChannel, then it'll register
  // itself using aLoadIdentifier and be kept alive until it completes
  // loading.
  return net::DocumentLoadListener::SpeculativeLoadInParent(this, aLoadState);
}

bool CanonicalBrowsingContext::StartDocumentLoad(
    net::DocumentLoadListener* aLoad) {
  // If we're controlling loads from the parent, then starting a new load means
  // that we need to cancel any existing ones.
  if (StaticPrefs::browser_tabs_documentchannel_parent_controlled() &&
      mCurrentLoad) {
    // Make sure we are not loading a javascript URI.
    MOZ_ASSERT(!aLoad->IsLoadingJSURI());

    // If we want to do a download, don't cancel the current navigation.
    if (!aLoad->IsDownload()) {
      mCurrentLoad->Cancel(NS_BINDING_CANCELLED_OLD_LOAD, ""_ns);
    }
  }
  mCurrentLoad = aLoad;

  if (NS_FAILED(SetCurrentLoadIdentifier(Some(aLoad->GetLoadIdentifier())))) {
    mCurrentLoad = nullptr;
    return false;
  }

  return true;
}

void CanonicalBrowsingContext::EndDocumentLoad(bool aContinueNavigating) {
  mCurrentLoad = nullptr;

  if (!aContinueNavigating) {
    // Resetting the current load identifier on a discarded context
    // has no effect when a document load has finished.
    Unused << SetCurrentLoadIdentifier(Nothing());
  }
}

already_AddRefed<nsIURI> CanonicalBrowsingContext::GetCurrentURI() const {
  nsCOMPtr<nsIURI> currentURI;
  if (nsIDocShell* docShell = GetDocShell()) {
    MOZ_ALWAYS_SUCCEEDS(
        nsDocShell::Cast(docShell)->GetCurrentURI(getter_AddRefs(currentURI)));
  } else {
    currentURI = mCurrentRemoteURI;
  }
  return currentURI.forget();
}

void CanonicalBrowsingContext::SetCurrentRemoteURI(nsIURI* aCurrentRemoteURI) {
  MOZ_ASSERT(!GetDocShell());
  mCurrentRemoteURI = aCurrentRemoteURI;
}

void CanonicalBrowsingContext::ResetSHEntryHasUserInteractionCache() {
  WindowContext* topWc = GetTopWindowContext();
  if (topWc && !topWc->IsDiscarded()) {
    MOZ_ALWAYS_SUCCEEDS(topWc->SetSHEntryHasUserInteraction(false));
  }
}

void CanonicalBrowsingContext::HistoryCommitIndexAndLength() {
  nsID changeID = {};
  CallerWillNotifyHistoryIndexAndLengthChanges caller(nullptr);
  HistoryCommitIndexAndLength(changeID, caller);
}
void CanonicalBrowsingContext::HistoryCommitIndexAndLength(
    const nsID& aChangeID,
    const CallerWillNotifyHistoryIndexAndLengthChanges& aProofOfCaller) {
  if (!IsTop()) {
    Cast(Top())->HistoryCommitIndexAndLength(aChangeID, aProofOfCaller);
    return;
  }

  nsISHistory* shistory = GetSessionHistory();
  if (!shistory) {
    return;
  }
  int32_t index = 0;
  shistory->GetIndex(&index);
  int32_t length = shistory->GetCount();

  GetChildSessionHistory()->SetIndexAndLength(index, length, aChangeID);

  shistory->EvictOutOfRangeDocumentViewers(index);

  Group()->EachParent([&](ContentParent* aParent) {
    Unused << aParent->SendHistoryCommitIndexAndLength(this, index, length,
                                                       aChangeID);
  });
}

void CanonicalBrowsingContext::SynchronizeLayoutHistoryState() {
  if (mActiveEntry) {
    if (IsInProcess()) {
      nsIDocShell* docShell = GetDocShell();
      if (docShell) {
        docShell->PersistLayoutHistoryState();

        nsCOMPtr<nsILayoutHistoryState> state;
        docShell->GetLayoutHistoryState(getter_AddRefs(state));
        if (state) {
          mActiveEntry->SetLayoutHistoryState(state);
        }
      }
    } else if (ContentParent* cp = GetContentParent()) {
      cp->SendGetLayoutHistoryState(this)->Then(
          GetCurrentSerialEventTarget(), __func__,
          [activeEntry = mActiveEntry](
              const std::tuple<RefPtr<nsILayoutHistoryState>, Maybe<Wireframe>>&
                  aResult) {
            if (std::get<0>(aResult)) {
              activeEntry->SetLayoutHistoryState(std::get<0>(aResult));
            }
            if (std::get<1>(aResult)) {
              activeEntry->SetWireframe(std::get<1>(aResult));
            }
          },
          []() {});
    }
  }
}

void CanonicalBrowsingContext::ResetScalingZoom() {
  // This currently only ever gets called in the parent process, and we
  // pass the message on to the WindowGlobalChild for the rootmost browsing
  // context.
  if (WindowGlobalParent* topWindow = GetTopWindowContext()) {
    Unused << topWindow->SendResetScalingZoom();
  }
}

void CanonicalBrowsingContext::SetRestoreData(SessionStoreRestoreData* aData,
                                              ErrorResult& aError) {
  MOZ_DIAGNOSTIC_ASSERT(aData);

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(GetParentObject());
  RefPtr<Promise> promise = Promise::Create(global, aError);
  if (aError.Failed()) {
    return;
  }

  if (NS_WARN_IF(NS_FAILED(SetHasRestoreData(true)))) {
    aError.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  mRestoreState = new RestoreState();
  mRestoreState->mData = aData;
  mRestoreState->mPromise = promise;
}

already_AddRefed<Promise> CanonicalBrowsingContext::GetRestorePromise() {
  if (mRestoreState) {
    return do_AddRef(mRestoreState->mPromise);
  }
  return nullptr;
}

void CanonicalBrowsingContext::ClearRestoreState() {
  if (IsDiscarded()) {
    return;
  }

  if (!mRestoreState) {
    MOZ_DIAGNOSTIC_ASSERT(!GetHasRestoreData());
    return;
  }
  if (mRestoreState->mPromise) {
    mRestoreState->mPromise->MaybeRejectWithUndefined();
  }
  mRestoreState = nullptr;

  MOZ_ALWAYS_SUCCEEDS(SetHasRestoreData(false));
}

void CanonicalBrowsingContext::RequestRestoreTabContent(
    WindowGlobalParent* aWindow) {
  MOZ_DIAGNOSTIC_ASSERT(IsTop());

  if (IsDiscarded() || !mRestoreState || !mRestoreState->mData) {
    return;
  }

  CanonicalBrowsingContext* context = aWindow->GetBrowsingContext();
  MOZ_DIAGNOSTIC_ASSERT(!context->IsDiscarded());

  RefPtr<SessionStoreRestoreData> data =
      mRestoreState->mData->FindDataForChild(context);

  if (context->IsTop()) {
    MOZ_DIAGNOSTIC_ASSERT(context == this);

    // We need to wait until the appropriate load event has fired before we
    // can "complete" the restore process, so if we're holding an empty data
    // object, just resolve the promise immediately.
    if (mRestoreState->mData->IsEmpty()) {
      MOZ_DIAGNOSTIC_ASSERT(!data || data->IsEmpty());
      mRestoreState->Resolve();
      ClearRestoreState();
      return;
    }

    // Since we're following load event order, we'll only arrive here for a
    // toplevel context after we've already sent down data for all child frames,
    // so it's safe to clear this reference now. The completion callback below
    // relies on the mData field being null to determine if all requests have
    // been sent out.
    mRestoreState->ClearData();
    MOZ_ALWAYS_SUCCEEDS(SetHasRestoreData(false));
  }

  if (data && !data->IsEmpty()) {
    auto onTabRestoreComplete = [self = RefPtr{this},
                                 state = RefPtr{mRestoreState}](auto) {
      state->mResolves++;
      if (!state->mData && state->mRequests == state->mResolves) {
        state->Resolve();
        if (state == self->mRestoreState) {
          self->ClearRestoreState();
        }
      }
    };

    mRestoreState->mRequests++;

    if (data->CanRestoreInto(aWindow->GetDocumentURI())) {
      if (!aWindow->IsInProcess()) {
        aWindow->SendRestoreTabContent(WrapNotNull(data.get()),
                                       onTabRestoreComplete,
                                       onTabRestoreComplete);
        return;
      }
      data->RestoreInto(context);
    }

    // This must be called both when we're doing an in-process restore, and when
    // we didn't do a restore at all due to a URL mismatch.
    onTabRestoreComplete(true);
  }
}

void CanonicalBrowsingContext::RestoreState::Resolve() {
  MOZ_DIAGNOSTIC_ASSERT(mPromise);
  mPromise->MaybeResolveWithUndefined();
  mPromise = nullptr;
}

nsresult CanonicalBrowsingContext::WriteSessionStorageToSessionStore(
    const nsTArray<SSCacheCopy>& aSesssionStorage, uint32_t aEpoch) {
  nsCOMPtr<nsISessionStoreFunctions> sessionStoreFuncs =
      do_GetService("@mozilla.org/toolkit/sessionstore-functions;1");
  if (!sessionStoreFuncs) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIXPConnectWrappedJS> wrapped =
      do_QueryInterface(sessionStoreFuncs);
  AutoJSAPI jsapi;
  if (!jsapi.Init(wrapped->GetJSObjectGlobal())) {
    return NS_ERROR_FAILURE;
  }

  JS::Rooted<JS::Value> key(jsapi.cx(), Top()->PermanentKey());

  Record<nsCString, Record<nsString, nsString>> storage;
  JS::Rooted<JS::Value> update(jsapi.cx());

  if (!aSesssionStorage.IsEmpty()) {
    SessionStoreUtils::ConstructSessionStorageValues(this, aSesssionStorage,
                                                     storage);
    if (!ToJSValue(jsapi.cx(), storage, &update)) {
      return NS_ERROR_FAILURE;
    }
  } else {
    update.setNull();
  }

  return sessionStoreFuncs->UpdateSessionStoreForStorage(
      Top()->GetEmbedderElement(), this, key, aEpoch, update);
}

void CanonicalBrowsingContext::UpdateSessionStoreSessionStorage(
    const std::function<void()>& aDone) {
  using DataPromise = BackgroundSessionStorageManager::DataPromise;
  BackgroundSessionStorageManager::GetData(
      this, StaticPrefs::browser_sessionstore_dom_storage_limit(),
      /* aClearSessionStoreTimer = */ true)
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, aDone, epoch = GetSessionStoreEpoch()](
                 const DataPromise::ResolveOrRejectValue& valueList) {
               if (valueList.IsResolve()) {
                 self->WriteSessionStorageToSessionStore(
                     valueList.ResolveValue(), epoch);
               }
               aDone();
             });
}

/* static */
void CanonicalBrowsingContext::UpdateSessionStoreForStorage(
    uint64_t aBrowsingContextId) {
  RefPtr<CanonicalBrowsingContext> browsingContext = Get(aBrowsingContextId);

  if (!browsingContext) {
    return;
  }

  browsingContext->UpdateSessionStoreSessionStorage([]() {});
}

void CanonicalBrowsingContext::MaybeScheduleSessionStoreUpdate() {
  if (!SessionStorePlatformCollection()) {
    return;
  }

  if (!IsTop()) {
    Top()->MaybeScheduleSessionStoreUpdate();
    return;
  }

  if (IsInBFCache()) {
    return;
  }

  if (mSessionStoreSessionStorageUpdateTimer) {
    return;
  }

  if (!StaticPrefs::browser_sessionstore_debug_no_auto_updates()) {
    auto result = NS_NewTimerWithFuncCallback(
        [](nsITimer*, void* aClosure) {
          auto* context = static_cast<CanonicalBrowsingContext*>(aClosure);
          context->UpdateSessionStoreSessionStorage([]() {});
        },
        this, StaticPrefs::browser_sessionstore_interval(),
        nsITimer::TYPE_ONE_SHOT,
        "CanonicalBrowsingContext::MaybeScheduleSessionStoreUpdate");

    if (result.isErr()) {
      return;
    }

    mSessionStoreSessionStorageUpdateTimer = result.unwrap();
  }
}

void CanonicalBrowsingContext::CancelSessionStoreUpdate() {
  if (mSessionStoreSessionStorageUpdateTimer) {
    mSessionStoreSessionStorageUpdateTimer->Cancel();
    mSessionStoreSessionStorageUpdateTimer = nullptr;
  }
}

void CanonicalBrowsingContext::SetContainerFeaturePolicy(
    Maybe<FeaturePolicyInfo>&& aContainerFeaturePolicyInfo) {
  mContainerFeaturePolicyInfo = std::move(aContainerFeaturePolicyInfo);
}

already_AddRefed<CanonicalBrowsingContext>
CanonicalBrowsingContext::GetCrossGroupOpener() const {
  return Get(mCrossGroupOpenerId);
}

void CanonicalBrowsingContext::SetCrossGroupOpenerId(uint64_t aOpenerId) {
  MOZ_DIAGNOSTIC_ASSERT(IsTopContent());
  MOZ_DIAGNOSTIC_ASSERT(mCrossGroupOpenerId == 0,
                        "Can only set CrossGroupOpenerId once");
  mCrossGroupOpenerId = aOpenerId;
}

void CanonicalBrowsingContext::SetCrossGroupOpener(
    CanonicalBrowsingContext* aCrossGroupOpener, ErrorResult& aRv) {
  if (!IsTopContent()) {
    aRv.ThrowNotAllowedError(
        "Can only set crossGroupOpener on toplevel content");
    return;
  }
  if (mCrossGroupOpenerId != 0) {
    aRv.ThrowNotAllowedError("Can only set crossGroupOpener once");
    return;
  }
  if (!aCrossGroupOpener) {
    aRv.ThrowNotAllowedError("Can't set crossGroupOpener to null");
    return;
  }

  SetCrossGroupOpenerId(aCrossGroupOpener->Id());
}

auto CanonicalBrowsingContext::FindUnloadingHost(uint64_t aChildID)
    -> nsTArray<UnloadingHost>::iterator {
  return std::find_if(
      mUnloadingHosts.begin(), mUnloadingHosts.end(),
      [&](const auto& host) { return host.mChildID == aChildID; });
}

void CanonicalBrowsingContext::ClearUnloadingHost(uint64_t aChildID) {
  // Notify any callbacks which were waiting for the host to finish unloading
  // that it has.
  auto found = FindUnloadingHost(aChildID);
  if (found != mUnloadingHosts.end()) {
    auto callbacks = std::move(found->mCallbacks);
    mUnloadingHosts.RemoveElementAt(found);
    for (const auto& callback : callbacks) {
      callback();
    }
  }
}

void CanonicalBrowsingContext::StartUnloadingHost(uint64_t aChildID) {
  MOZ_DIAGNOSTIC_ASSERT(FindUnloadingHost(aChildID) == mUnloadingHosts.end());
  mUnloadingHosts.AppendElement(UnloadingHost{aChildID, {}});
}

void CanonicalBrowsingContext::BrowserParentDestroyed(
    BrowserParent* aBrowserParent, bool aAbnormalShutdown) {
  ClearUnloadingHost(aBrowserParent->Manager()->ChildID());

  // Handling specific to when the current BrowserParent has been destroyed.
  if (mCurrentBrowserParent == aBrowserParent) {
    mCurrentBrowserParent = nullptr;

    // If this BrowserParent is for a subframe, attempt to recover from a
    // subframe crash by rendering the subframe crashed page in the embedding
    // content.
    if (aAbnormalShutdown) {
      ShowSubframeCrashedUI(aBrowserParent->GetBrowserBridgeParent());
    }
  }
}

void CanonicalBrowsingContext::ShowSubframeCrashedUI(
    BrowserBridgeParent* aBridge) {
  if (!aBridge || IsDiscarded() || !aBridge->CanSend()) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(!aBridge->GetBrowsingContext() ||
                        aBridge->GetBrowsingContext() == this);

  // There is no longer a current inner window within this
  // BrowsingContext, update the `CurrentInnerWindowId` field to reflect
  // this.
  MOZ_ALWAYS_SUCCEEDS(SetCurrentInnerWindowId(0));

  // The owning process will now be the embedder to render the subframe
  // crashed page, switch ownership back over.
  SetOwnerProcessId(aBridge->Manager()->Manager()->ChildID());
  SetCurrentBrowserParent(aBridge->Manager());

  Unused << aBridge->SendSubFrameCrashed();
}

static void LogBFCacheBlockingForDoc(BrowsingContext* aBrowsingContext,
                                     uint32_t aBFCacheCombo, bool aIsSubDoc) {
  if (aIsSubDoc) {
    nsAutoCString uri("[no uri]");
    nsCOMPtr<nsIURI> currentURI =
        aBrowsingContext->Canonical()->GetCurrentURI();
    if (currentURI) {
      uri = currentURI->GetSpecOrDefault();
    }
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
            (" ** Blocked for document %s", uri.get()));
  }
  if (aBFCacheCombo & BFCacheStatus::EVENT_HANDLING_SUPPRESSED) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
            (" * event handling suppression"));
  }
  if (aBFCacheCombo & BFCacheStatus::SUSPENDED) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * suspended Window"));
  }
  if (aBFCacheCombo & BFCacheStatus::UNLOAD_LISTENER) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * unload listener"));
  }
  if (aBFCacheCombo & BFCacheStatus::REQUEST) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * requests in the loadgroup"));
  }
  if (aBFCacheCombo & BFCacheStatus::ACTIVE_GET_USER_MEDIA) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * GetUserMedia"));
  }
  if (aBFCacheCombo & BFCacheStatus::ACTIVE_PEER_CONNECTION) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * PeerConnection"));
  }
  if (aBFCacheCombo & BFCacheStatus::CONTAINS_EME_CONTENT) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * EME content"));
  }
  if (aBFCacheCombo & BFCacheStatus::CONTAINS_MSE_CONTENT) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * MSE use"));
  }
  if (aBFCacheCombo & BFCacheStatus::HAS_ACTIVE_SPEECH_SYNTHESIS) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * Speech use"));
  }
  if (aBFCacheCombo & BFCacheStatus::HAS_USED_VR) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * used VR"));
  }
  if (aBFCacheCombo & BFCacheStatus::BEFOREUNLOAD_LISTENER) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * beforeunload listener"));
  }
  if (aBFCacheCombo & BFCacheStatus::ACTIVE_LOCK) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * has active Web Locks"));
  }
  if (aBFCacheCombo & BFCacheStatus::PAGE_LOADING) {
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * has page loading"));
  }
}

bool CanonicalBrowsingContext::AllowedInBFCache(
    const Maybe<uint64_t>& aChannelId, nsIURI* aNewURI) {
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Debug))) {
    nsAutoCString uri("[no uri]");
    nsCOMPtr<nsIURI> currentURI = GetCurrentURI();
    if (currentURI) {
      uri = currentURI->GetSpecOrDefault();
    }
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, ("Checking %s", uri.get()));
  }

  if (IsInProcess()) {
    return false;
  }

  uint32_t bfcacheCombo = 0;
  if (mRestoreState) {
    bfcacheCombo |= BFCacheStatus::RESTORING;
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * during session restore"));
  }

  if (Group()->Toplevels().Length() > 1) {
    bfcacheCombo |= BFCacheStatus::NOT_ONLY_TOPLEVEL_IN_BCG;
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
            (" * auxiliary BrowsingContexts"));
  }

  // There are not a lot of about:* pages that are allowed to load in
  // subframes, so it's OK to allow those few about:* pages enter BFCache.
  MOZ_ASSERT(IsTop(), "Trying to put a non top level BC into BFCache");

  WindowGlobalParent* wgp = GetCurrentWindowGlobal();
  if (wgp && wgp->GetDocumentURI()) {
    nsCOMPtr<nsIURI> currentURI = wgp->GetDocumentURI();
    // Exempt about:* pages from bfcache, with the exception of about:blank
    if (currentURI->SchemeIs("about") &&
        !NS_IsAboutBlankAllowQueryAndFragment(currentURI)) {
      bfcacheCombo |= BFCacheStatus::ABOUT_PAGE;
      MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug, (" * about:* page"));
    }

    if (aNewURI) {
      bool equalUri = false;
      aNewURI->Equals(currentURI, &equalUri);
      if (equalUri) {
        // When loading the same uri, disable bfcache so that
        // nsDocShell::OnNewURI transforms the load to LOAD_NORMAL_REPLACE.
        return false;
      }
    }
  }

  // For telemetry we're collecting all the flags for all the BCs hanging
  // from this top-level BC.
  PreOrderWalk([&](BrowsingContext* aBrowsingContext) {
    WindowGlobalParent* wgp =
        aBrowsingContext->Canonical()->GetCurrentWindowGlobal();
    uint32_t subDocBFCacheCombo = wgp ? wgp->GetBFCacheStatus() : 0;
    if (wgp) {
      const Maybe<uint64_t>& singleChannelId = wgp->GetSingleChannelId();
      if (singleChannelId.isSome()) {
        if (singleChannelId.value() == 0 || aChannelId.isNothing() ||
            singleChannelId.value() != aChannelId.value()) {
          subDocBFCacheCombo |= BFCacheStatus::REQUEST;
        }
      }
    }

    if (MOZ_UNLIKELY(MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Debug))) {
      LogBFCacheBlockingForDoc(aBrowsingContext, subDocBFCacheCombo,
                               aBrowsingContext != this);
    }

    bfcacheCombo |= subDocBFCacheCombo;
  });

  nsDocShell::ReportBFCacheComboTelemetry(bfcacheCombo);
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Debug))) {
    nsAutoCString uri("[no uri]");
    nsCOMPtr<nsIURI> currentURI = GetCurrentURI();
    if (currentURI) {
      uri = currentURI->GetSpecOrDefault();
    }
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Debug,
            (" +> %s %s be blocked from going into the BFCache", uri.get(),
             bfcacheCombo == 0 ? "shouldn't" : "should"));
  }

  if (StaticPrefs::docshell_shistory_bfcache_allow_unload_listeners()) {
    bfcacheCombo &= ~BFCacheStatus::UNLOAD_LISTENER;
  }

  return bfcacheCombo == 0;
}

void CanonicalBrowsingContext::SetIsActive(bool aIsActive, ErrorResult& aRv) {
#ifdef DEBUG
  if (MOZ_UNLIKELY(!ManuallyManagesActiveness())) {
    xpc_DumpJSStack(true, true, false);
    MOZ_ASSERT_UNREACHABLE(
        "Trying to manually manage activeness of a browsing context that isn't "
        "manually managed (see manualactiveness attribute)");
  }
#endif
  SetIsActiveInternal(aIsActive, aRv);
}

void CanonicalBrowsingContext::SetTouchEventsOverride(
    dom::TouchEventsOverride aOverride, ErrorResult& aRv) {
  SetTouchEventsOverrideInternal(aOverride, aRv);
}

void CanonicalBrowsingContext::SetTargetTopLevelLinkClicksToBlank(
    bool aTargetTopLevelLinkClicksToBlank, ErrorResult& aRv) {
  SetTargetTopLevelLinkClicksToBlankInternal(aTargetTopLevelLinkClicksToBlank,
                                             aRv);
}

void CanonicalBrowsingContext::AddPageAwakeRequest() {
  MOZ_ASSERT(IsTop());
  auto count = GetPageAwakeRequestCount();
  MOZ_ASSERT(count < UINT32_MAX);
  Unused << SetPageAwakeRequestCount(++count);
}

void CanonicalBrowsingContext::RemovePageAwakeRequest() {
  MOZ_ASSERT(IsTop());
  auto count = GetPageAwakeRequestCount();
  MOZ_ASSERT(count > 0);
  Unused << SetPageAwakeRequestCount(--count);
}

void CanonicalBrowsingContext::CloneDocumentTreeInto(
    CanonicalBrowsingContext* aSource, const nsACString& aRemoteType,
    embedding::PrintData&& aPrintData) {
  NavigationIsolationOptions options;
  options.mRemoteType = aRemoteType;

  mClonePromise =
      ChangeRemoteness(options, /* aPendingSwitchId = */ 0)
          ->Then(
              GetMainThreadSerialEventTarget(), __func__,
              [source = MaybeDiscardedBrowsingContext{aSource},
               data = std::move(aPrintData)](
                  const std::pair<RefPtr<BrowserParent>,
                                  RefPtr<CanonicalBrowsingContext>>& aResult)
                  -> RefPtr<GenericNonExclusivePromise> {
                const auto& [browserParent, browsingContext] = aResult;

                RefPtr<BrowserBridgeParent> bridge =
                    browserParent->GetBrowserBridgeParent();
                return browserParent
                    ->SendCloneDocumentTreeIntoSelf(source, data)
                    ->Then(
                        GetMainThreadSerialEventTarget(), __func__,
                        [bridge](
                            BrowserParent::CloneDocumentTreeIntoSelfPromise::
                                ResolveOrRejectValue&& aValue) {
                          // We're cloning a remote iframe, so we created a
                          // BrowserBridge which makes us register an OOP load
                          // (see Document::OOPChildLoadStarted), even though
                          // this isn't a real load. We call
                          // SendMaybeFireEmbedderLoadEvents here so that we do
                          // register the end of the load (see
                          // Document::OOPChildLoadDone).
                          if (bridge) {
                            Unused << bridge->SendMaybeFireEmbedderLoadEvents(
                                EmbedderElementEventType::NoEvent);
                          }
                          if (aValue.IsResolve() && aValue.ResolveValue()) {
                            return GenericNonExclusivePromise::CreateAndResolve(
                                true, __func__);
                          }
                          return GenericNonExclusivePromise::CreateAndReject(
                              NS_ERROR_FAILURE, __func__);
                        });
              },
              [](nsresult aRv) -> RefPtr<GenericNonExclusivePromise> {
                NS_WARNING(
                    nsPrintfCString("Remote clone failed: %x\n", unsigned(aRv))
                        .get());
                return GenericNonExclusivePromise::CreateAndReject(
                    NS_ERROR_FAILURE, __func__);
              });

  mClonePromise->Then(
      GetMainThreadSerialEventTarget(), __func__,
      [self = RefPtr{this}]() { self->mClonePromise = nullptr; });
}

bool CanonicalBrowsingContext::StartApzAutoscroll(float aAnchorX,
                                                  float aAnchorY,
                                                  nsViewID aScrollId,
                                                  uint32_t aPresShellId) {
  nsCOMPtr<nsIWidget> widget;
  mozilla::layers::LayersId layersId{0};

  if (IsInProcess()) {
    nsCOMPtr<nsPIDOMWindowOuter> outer = GetDOMWindow();
    if (!outer) {
      return false;
    }

    widget = widget::WidgetUtils::DOMWindowToWidget(outer);
    if (widget) {
      layersId = widget->GetRootLayerTreeId();
    }
  } else {
    RefPtr<BrowserParent> parent = GetBrowserParent();
    if (!parent) {
      return false;
    }

    widget = parent->GetWidget();
    layersId = parent->GetLayersId();
  }

  if (!widget || !widget->AsyncPanZoomEnabled()) {
    return false;
  }

  // The anchor coordinates that are passed in are relative to the origin of the
  // screen, but we are sending them to APZ which only knows about coordinates
  // relative to the widget, so convert them accordingly.
  const LayoutDeviceIntPoint anchor =
      RoundedToInt(LayoutDevicePoint(aAnchorX, aAnchorY)) -
      widget->WidgetToScreenOffset();

  mozilla::layers::ScrollableLayerGuid guid(layersId, aPresShellId, aScrollId);

  return widget->StartAsyncAutoscroll(
      ViewAs<ScreenPixel>(
          anchor, PixelCastJustification::LayoutDeviceIsScreenForBounds),
      guid);
}

void CanonicalBrowsingContext::StopApzAutoscroll(nsViewID aScrollId,
                                                 uint32_t aPresShellId) {
  nsCOMPtr<nsIWidget> widget;
  mozilla::layers::LayersId layersId{0};

  if (IsInProcess()) {
    nsCOMPtr<nsPIDOMWindowOuter> outer = GetDOMWindow();
    if (!outer) {
      return;
    }

    widget = widget::WidgetUtils::DOMWindowToWidget(outer);
    if (widget) {
      layersId = widget->GetRootLayerTreeId();
    }
  } else {
    RefPtr<BrowserParent> parent = GetBrowserParent();
    if (!parent) {
      return;
    }

    widget = parent->GetWidget();
    layersId = parent->GetLayersId();
  }

  if (!widget || !widget->AsyncPanZoomEnabled()) {
    return;
  }

  mozilla::layers::ScrollableLayerGuid guid(layersId, aPresShellId, aScrollId);
  widget->StopAsyncAutoscroll(guid);
}

already_AddRefed<nsISHEntry>
CanonicalBrowsingContext::GetMostRecentLoadingSessionHistoryEntry() {
  if (mLoadingEntries.IsEmpty()) {
    return nullptr;
  }

  RefPtr<SessionHistoryEntry> entry = mLoadingEntries.LastElement().mEntry;
  return entry.forget();
}

already_AddRefed<BounceTrackingState>
CanonicalBrowsingContext::GetBounceTrackingState() {
  if (!mWebProgress) {
    return nullptr;
  }
  return mWebProgress->GetBounceTrackingState();
}

bool CanonicalBrowsingContext::CanOpenModalPicker() {
  if (!mozilla::StaticPrefs::browser_disable_pickers_background_tabs()) {
    return true;
  }

  // Alway allows to open picker from chrome.
  if (IsChrome()) {
    return true;
  }

  if (!IsActive()) {
    return false;
  }

  mozilla::dom::Element* topFrameElement = GetTopFrameElement();
  if (!mozilla::StaticPrefs::
          browser_disable_pickers_in_hidden_extension_pages() &&
      Windowless()) {
    WindowGlobalParent* wgp = GetCurrentWindowGlobal();
    if (wgp && BasePrincipal::Cast(wgp->DocumentPrincipal())->AddonPolicy()) {
      // This may be a HiddenExtensionPage, e.g. an extension background page.
      return true;
    }
  }

  RefPtr<Document> chromeDoc = TopCrossChromeBoundary()->GetExtantDocument();
  if (!chromeDoc || !chromeDoc->HasFocus(mozilla::IgnoreErrors())) {
    return false;
  }

  // Only allow web content to open a picker when it has focus. For example, if
  // the focus is on the URL bar, web content cannot open a picker, even if it
  // is the foreground tab.
  // topFrameElement may be a <browser> embedded in another <browser>. In that
  // case, verify that the full chain of <browser> elements has focus.
  while (topFrameElement) {
    RefPtr<Document> doc = topFrameElement->OwnerDoc();
    if (doc->GetActiveElement() != topFrameElement) {
      return false;
    }
    topFrameElement = doc->GetBrowsingContext()->GetTopFrameElement();
    // Eventually topFrameElement == nullptr, implying that we have reached the
    // top browser window (and chromeDoc == doc).
  }
  return true;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(CanonicalBrowsingContext)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(CanonicalBrowsingContext,
                                                BrowsingContext)
  tmp->mPermanentKey.setNull();
  if (tmp->mSessionHistory) {
    tmp->mSessionHistory->SetBrowsingContext(nullptr);
  }
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSessionHistory, mCurrentBrowserParent,
                                  mWebProgress,
                                  mSessionStoreSessionStorageUpdateTimer)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(CanonicalBrowsingContext,
                                                  BrowsingContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSessionHistory, mCurrentBrowserParent,
                                    mWebProgress,
                                    mSessionStoreSessionStorageUpdateTimer)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(CanonicalBrowsingContext,
                                               BrowsingContext)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mPermanentKey)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_ADDREF_INHERITED(CanonicalBrowsingContext, BrowsingContext)
NS_IMPL_RELEASE_INHERITED(CanonicalBrowsingContext, BrowsingContext)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(CanonicalBrowsingContext)
NS_INTERFACE_MAP_END_INHERITING(BrowsingContext)

}  // namespace mozilla::dom
