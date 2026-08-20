/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsDocShell.h"

#include <algorithm>
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/dom/HTMLFormElement.h"

#ifdef XP_WIN
#  include <process.h>
#  define getpid _getpid
#else
#  include <unistd.h>  // for getpid()
#endif

#include "nsDeviceContext.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Casting.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Components.h"
#include "mozilla/Encoding.h"
#include "mozilla/EventStateManager.h"
#include "mozilla/HTMLEditor.h"
#include "mozilla/InputTaskManager.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Logging.h"
#include "mozilla/MediaFeatureChange.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/ScrollContainerFrame.h"
#include "mozilla/ScrollTypes.h"
#include "mozilla/SimpleEnumerator.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_docshell.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StaticPrefs_ui.h"
#include "mozilla/StaticPrefs_fission.h"
#include "mozilla/StartupTimeline.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/Telemetry.h"

#include "mozilla/WidgetUtils.h"

#include "mozilla/dom/AutoEntryScript.h"
#include "mozilla/dom/ChildProcessChannelListener.h"
#include "mozilla/dom/ClientChannelHelper.h"
#include "mozilla/dom/ClientHandle.h"
#include "mozilla/dom/ClientInfo.h"
#include "mozilla/dom/ClientManager.h"
#include "mozilla/dom/ClientSource.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/ContentFrameMessageManager.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/FragmentDirective.h"
#include "mozilla/dom/HTMLAnchorElement.h"
#include "mozilla/dom/HTMLIFrameElement.h"
#include "mozilla/dom/Navigation.h"
#include "mozilla/dom/NavigationBinding.h"
#include "mozilla/dom/NavigationHistoryEntry.h"
#include "mozilla/dom/NavigationUtils.h"
#include "mozilla/dom/PerformanceNavigation.h"
#include "mozilla/dom/PermissionMessageUtils.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/PopupBlocker.h"
#include "mozilla/dom/ScreenOrientation.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/ServiceWorkerInterceptController.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/dom/SessionHistoryEntry.h"
#include "mozilla/dom/SessionStorageManager.h"
#include "mozilla/dom/SessionStoreChangeListener.h"
#include "mozilla/dom/SessionStoreChild.h"
#include "mozilla/dom/SessionStoreUtils.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/TrustedTypesConstants.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/ToJSValue.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/ChildSHistory.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/dom/LoadURIOptionsBinding.h"
#include "mozilla/dom/JSWindowActorChild.h"
#include "mozilla/dom/DocumentBinding.h"
#include "mozilla/glean/DocshellMetrics.h"
#include "mozilla/ipc/ProtocolUtils.h"
#include "mozilla/net/DocumentChannel.h"
#include "mozilla/net/DocumentChannelChild.h"
#include "mozilla/net/ParentChannelWrapper.h"
#include "mozilla/net/UrlClassifierFeatureFactory.h"
#include "ReferrerInfo.h"

#include "nsIAuthPrompt.h"
#include "nsIAuthPrompt2.h"
#include "nsICachingChannel.h"
#include "nsICaptivePortalService.h"
#include "nsIChannel.h"
#include "nsIChannelEventSink.h"
#include "nsIClassifiedChannel.h"
#include "nsIClassOfService.h"
#include "nsIConsoleReportCollector.h"
#include "nsIContent.h"
#include "nsIContentInlines.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIController.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeOwner.h"
#include "nsIDocumentViewer.h"
#include "mozilla/dom/Document.h"
#include "nsHTMLDocument.h"
#include "nsIDocumentLoaderFactory.h"
#include "nsIDOMWindow.h"
#include "nsIEditingSession.h"
#include "nsIEffectiveTLDService.h"
#include "nsExternalHelperAppService.h"
#include "nsIExternalProtocolService.h"
#include "nsIFormPOSTActionChannel.h"
#include "nsIFrame.h"
#include "nsIGlobalObject.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIIDNService.h"
#include "nsIInputStreamChannel.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILayoutHistoryState.h"
#include "nsILoadInfo.h"
#include "nsILoadURIDelegate.h"
#include "nsIMultiPartChannel.h"
#include "nsINestedURI.h"
#include "nsINode.h"
#include "nsINSSErrorsService.h"
#include "nsIObserverService.h"
#include "nsIOService.h"
#include "nsIPrincipal.h"
#include "nsIPrivacyTransitionObserver.h"
#include "nsIPrompt.h"
#include "nsIPromptCollection.h"
#include "nsIPromptFactory.h"
#include "nsIPublicKeyPinningService.h"
#include "nsIReflowObserver.h"
#include "nsIScriptChannel.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsIScriptSecurityManager.h"
#include "nsScriptSecurityManager.h"
#include "nsIScrollObserver.h"
#include "nsISupportsPrimitives.h"
#include "nsISecureBrowserUI.h"
#include "nsISeekableStream.h"
#include "nsISelectionDisplay.h"
#include "nsISiteSecurityService.h"
#include "nsISocketProvider.h"
#include "nsIStringBundle.h"
#include "nsIStructuredCloneContainer.h"
#include "nsIBrowserChild.h"
#include "nsITextToSubURI.h"
#include "nsITimedChannel.h"
#include "nsITimer.h"
#include "nsITransportSecurityInfo.h"
#include "nsIUploadChannel.h"
#include "nsIURIFixup.h"
#include "nsIURIMutator.h"
#include "nsIURILoader.h"
#include "nsIViewSourceChannel.h"
#include "nsIWebBrowserChrome.h"
#include "nsIWebBrowserFind.h"
#include "nsIWebProgress.h"
#include "nsIWidget.h"
#include "nsIWindowWatcher.h"
#include "nsIWritablePropertyBag2.h"
#include "nsIX509Cert.h"
#include "nsIXULRuntime.h"

#include "nsCommandManager.h"
#include "nsPIDOMWindow.h"
#include "nsPIWindowRoot.h"

#include "IHistory.h"
#include "IUrlClassifierUITelemetry.h"

#include "nsAboutProtocolUtils.h"
#include "nsArray.h"
#include "nsArrayUtils.h"
#include "nsBrowserStatusFilter.h"
#include "nsCExternalHandlerService.h"
#include "nsContentDLF.h"
#include "nsContentPolicyUtils.h"  // NS_CheckContentLoadPolicy(...)
#include "nsContentSecurityManager.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsCURILoader.h"
#include "nsDocElementCreatedNotificationRunner.h"
#include "nsDocShellCID.h"
#include "nsDocShellEditorData.h"
#include "nsDocShellEnumerator.h"
#include "nsDocShellLoadState.h"
#include "nsDocShellLoadTypes.h"
#include "nsDOMCID.h"
#include "nsDOMNavigationTiming.h"
#include "nsDSURIContentListener.h"
#include "nsEditingSession.h"
#include "nsError.h"
#include "nsEscape.h"
#include "nsFocusManager.h"
#include "nsGlobalWindowInner.h"
#include "nsGlobalWindowOuter.h"
#include "nsJSEnvironment.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsObjectLoadingContent.h"
#include "nsPIDOMWindowInlines.h"
#include "nsPingListener.h"
#include "nsPoint.h"
#include "nsQueryObject.h"
#include "nsQueryActor.h"
#include "nsRect.h"
#include "nsRefreshTimer.h"
#include "nsSandboxFlags.h"
#include "nsSHistory.h"
#include "nsStructuredCloneContainer.h"
#include "nsSubDocumentFrame.h"
#include "nsURILoader.h"
#include "nsURLHelper.h"
#include "nsViewSourceHandler.h"
#include "nsWebBrowserFind.h"
#include "nsWhitespaceTokenizer.h"
#include "nsWidgetsCID.h"
#include "nsXULAppAPI.h"

#include "CertVerifier.h"
#include "ThirdPartyUtil.h"
#include "GeckoProfiler.h"
#include "mozilla/NullPrincipal.h"
#include "Navigator.h"
#include "prenv.h"
#include "mozilla/ipc/URIUtils.h"
#include "sslerr.h"
#include "mozpkix/pkix.h"
#include "NSSErrorsService.h"

#include "nsDocShellTelemetryUtils.h"

#include "nsIOpenWindowInfo.h"

#ifdef MOZ_PLACES
#  include "mozilla/places/nsFaviconService.h"
#  include "mozIPlacesPendingOperation.h"
#endif

#ifdef NS_PRINTING
#  include "nsIDocumentViewerPrint.h"
#  include "nsIWebBrowserPrint.h"
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::net;

using mozilla::ipc::Endpoint;

// Threshold value in ms for META refresh based redirects
#define REFRESH_REDIRECT_TIMER 15000

static mozilla::LazyLogModule gCharsetMenuLog("CharsetMenu");
static mozilla::LazyLogModule gDocShellLog("nsDocShell");

#define LOGCHARSETMENU(args) \
  MOZ_LOG(gCharsetMenuLog, mozilla::LogLevel::Debug, args)

#ifdef DEBUG
unsigned long nsDocShell::gNumberOfDocShells = 0;
static uint64_t gDocshellIDCounter = 0;

static mozilla::LazyLogModule gDocShellAndDOMWindowLeakLogging(
    "DocShellAndDOMWindowLeak");
#endif
static mozilla::LazyLogModule gDocShellLeakLog("nsDocShellLeak");
extern mozilla::LazyLogModule gPageCacheLog;
extern mozilla::LazyLogModule gNavigationAPILog;
mozilla::LazyLogModule gSHLog("SessionHistory");
extern mozilla::LazyLogModule gSHIPBFCacheLog;

const char kAppstringsBundleURL[] =
    "chrome://global/locale/appstrings.properties";

static bool IsTopLevelDoc(BrowsingContext* aBrowsingContext,
                          nsILoadInfo* aLoadInfo) {
  MOZ_ASSERT(aBrowsingContext);
  MOZ_ASSERT(aLoadInfo);

  if (aLoadInfo->GetExternalContentPolicyType() !=
      ExtContentPolicy::TYPE_DOCUMENT) {
    return false;
  }

  return aBrowsingContext->IsTopContent();
}

// True if loading for top level document loading in active tab.
static bool IsUrgentStart(BrowsingContext* aBrowsingContext,
                          nsILoadInfo* aLoadInfo, uint32_t aLoadType) {
  MOZ_ASSERT(aBrowsingContext);
  MOZ_ASSERT(aLoadInfo);

  if (!IsTopLevelDoc(aBrowsingContext, aLoadInfo)) {
    return false;
  }

  if (aLoadType &
      (nsIDocShell::LOAD_CMD_NORMAL | nsIDocShell::LOAD_CMD_HISTORY)) {
    return true;
  }

  return aBrowsingContext->IsActive();
}

nsDocShell::nsDocShell(BrowsingContext* aBrowsingContext,
                       uint64_t aContentWindowID)
    : nsDocLoader(true),
      mContentWindowID(aContentWindowID),
      mBrowsingContext(aBrowsingContext),
      mParentCharset(nullptr),
      mTreeOwner(nullptr),
      mScrollbarPref(ScrollbarPreference::Auto),
      mCharsetReloadState(eCharsetReloadInit),
      mParentCharsetSource(0),
      mFrameMargins(-1, -1),
      mItemType(aBrowsingContext->IsContent() ? typeContent : typeChrome),
      mPreviousEntryIndex(-1),
      mLoadedEntryIndex(-1),
      mBusyFlags(BUSY_FLAGS_NONE),
      mAppType(nsIDocShell::APP_TYPE_UNKNOWN),
      mLoadType(0),
      mFailedLoadType(0),
      mChannelToDisconnectOnPageHide(0),
      mCreatingDocument(false),
#ifdef DEBUG
      mInEnsureScriptEnv(false),
#endif
      mInitialized(false),
      mAllowSubframes(true),
      mAllowMetaRedirects(true),
      mAllowImages(true),
      mAllowMedia(true),
      mAllowDNSPrefetch(true),
      mAllowWindowControl(true),
      mCSSErrorReportingEnabled(false),
      mAllowAuth(mItemType == typeContent),
      mAllowKeywordFixup(false),
      mDisableMetaRefreshWhenInactive(false),
      mWindowDraggingAllowed(false),
      mInFrameSwap(false),
      mFiredUnloadEvent(false),
      mEODForCurrentDocument(false),
      mURIResultedInDocument(false),
      mIsBeingDestroyed(false),
      mIsExecutingOnLoadHandler(false),
      mInvisible(false),
      mHasLoadedNonBlankURI(false),
      mHasStartedLoadingOtherThanInitialBlankURI(false),
      mBlankTiming(false),
      mTitleValidForCurrentURI(false),
      mWillChangeProcess(false),
      mIsNavigating(false),
      mForcedAutodetection(false),
      mCheckingSessionHistory(false),
      mNeedToReportActiveAfterLoadingBecomesActive(false) {
  // If no outer window ID was provided, generate a new one.
  if (aContentWindowID == 0) {
    mContentWindowID = nsContentUtils::GenerateWindowId();
  }

  MOZ_LOG(gDocShellLeakLog, LogLevel::Debug, ("DOCSHELL %p created\n", this));

#ifdef DEBUG
  mDocShellID = gDocshellIDCounter++;
  // We're counting the number of |nsDocShells| to help find leaks
  ++gNumberOfDocShells;
  MOZ_LOG(gDocShellAndDOMWindowLeakLogging, LogLevel::Info,
          ("++DOCSHELL %p == %ld [pid = %d] [id = %" PRIu64 "]\n", (void*)this,
           gNumberOfDocShells, getpid(), mDocShellID));
#endif
}

void nsDocShell::DestroyDocumentViewer() {
  if (!mDocumentViewer) {
    return;
  }
  mDocumentViewer->Close();
  mDocumentViewer->Destroy();
  mDocumentViewer = nullptr;
}

nsDocShell::~nsDocShell() {
  // Avoid notifying observers while we're in the dtor.
  mIsBeingDestroyed = true;

  Destroy();

  DestroyDocumentViewer();

  MOZ_LOG(gDocShellLeakLog, LogLevel::Debug, ("DOCSHELL %p destroyed\n", this));

#ifdef DEBUG
  if (MOZ_LOG_TEST(gDocShellAndDOMWindowLeakLogging, LogLevel::Info)) {
    nsAutoCString url;
    if (mLastOpenedURI) {
      url = mLastOpenedURI->GetSpecOrDefault();

      // Data URLs can be very long, so truncate to avoid flooding the log.
      const uint32_t maxURLLength = 1000;
      if (url.Length() > maxURLLength) {
        url.Truncate(maxURLLength);
      }
    }

    // We're counting the number of |nsDocShells| to help find leaks
    --gNumberOfDocShells;
    MOZ_LOG(
        gDocShellAndDOMWindowLeakLogging, LogLevel::Info,
        ("--DOCSHELL %p == %ld [pid = %d] [id = %" PRIu64 "] [url = %s]\n",
         (void*)this, gNumberOfDocShells, getpid(), mDocShellID, url.get()));
  }
#endif
}

nsresult nsDocShell::InitWindow(nsIWidget* aParentWidget, int32_t aX,
                                int32_t aY, int32_t aWidth, int32_t aHeight,
                                nsIOpenWindowInfo* aOpenWindowInfo,
                                mozilla::dom::WindowGlobalChild* aWindowActor) {
  SetParentWidget(aParentWidget);
  SetPositionAndSize(aX, aY, aWidth, aHeight, 0);
  return Initialize(aOpenWindowInfo, aWindowActor);
}

nsresult nsDocShell::Initialize(nsIOpenWindowInfo* aOpenWindowInfo,
                                mozilla::dom::WindowGlobalChild* aWindowActor) {
  if (mInitialized) {
    // We've already been initialized.
    MOZ_ASSERT(!aOpenWindowInfo,
               "Tried to reinitialize with override principal");
    MOZ_ASSERT(!aWindowActor, "Tried to reinitialize with a window actor");
    return NS_OK;
  }

  MOZ_ASSERT(aOpenWindowInfo,
             "Must have openwindowinfo if not already initialized.");

  NS_ASSERTION(mItemType == typeContent || mItemType == typeChrome,
               "Unexpected item type in docshell");

  NS_ENSURE_TRUE(Preferences::GetRootBranch(), NS_ERROR_NOT_INITIALIZED);
  mInitialized = true;

  mDisableMetaRefreshWhenInactive =
      Preferences::GetBool("browser.meta_refresh_when_inactive.disabled",
                           mDisableMetaRefreshWhenInactive);

  nsresult rv = CreateInitialDocumentViewer(aOpenWindowInfo, aWindowActor);

  if (nsCOMPtr<nsIObserverService> serv = services::GetObserverService()) {
    const char* msg = mItemType == typeContent ? NS_WEBNAVIGATION_CREATE
                                               : NS_CHROME_WEBNAVIGATION_CREATE;
    serv->NotifyWhenScriptSafe(GetAsSupports(this), msg, nullptr);
  }

  return rv;
}

/* static */
already_AddRefed<nsDocShell> nsDocShell::Create(
    BrowsingContext* aBrowsingContext, uint64_t aContentWindowID) {
  MOZ_ASSERT(aBrowsingContext, "DocShell without a BrowsingContext!");

  nsresult rv;
  RefPtr<nsDocShell> ds = new nsDocShell(aBrowsingContext, aContentWindowID);

  // Initialize the underlying nsDocLoader.
  rv = ds->nsDocLoader::InitWithBrowsingContext(aBrowsingContext);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  // Create our ContentListener
  ds->mContentListener = new nsDSURIContentListener(ds);

  // We enable if we're in the parent process in order to support non-e10s
  // configurations.
  // Note: This check is duplicated in SharedWorkerInterfaceRequestor's
  // constructor.
  if (XRE_IsParentProcess()) {
    ds->mInterceptController = MakeRefPtr<ServiceWorkerInterceptController>();
  }

  // We want to hold a strong ref to the loadgroup, so it better hold a weak
  // ref to us...  use an InterfaceRequestorProxy to do this.
  RefPtr proxy = MakeRefPtr<InterfaceRequestorProxy>(ds);
  ds->mLoadGroup->SetNotificationCallbacks(proxy);

  // XXX(nika): We have our BrowsingContext, so we might be able to skip this.
  // It could be nice to directly set up our DocLoader tree?
  rv = nsDocLoader::AddDocLoaderAsChildOfRoot(ds);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  uint32_t notifyMask =
      nsIWebProgress::NOTIFY_STATE_ALL | nsIWebProgress::NOTIFY_LOCATION |
      nsIWebProgress::NOTIFY_SECURITY | nsIWebProgress::NOTIFY_STATUS;

  // NOTE: Only listen for NOTIFY_PROGRESS on toplevel BrowsingContexts, as
  // listeners in the browser UI only cares about total progress on the toplevel
  // context. Aggregation of the total progress is currently handled within
  // `nsDocLoader`, and does not take out-of-process iframes into account.
  if (aBrowsingContext->IsTop()) {
    notifyMask |= nsIWebProgress::NOTIFY_PROGRESS;
  }

  // Add |ds| as a progress listener to itself.  A little weird, but simpler
  // than reproducing all the listener-notification logic in overrides of the
  // various methods via which nsDocLoader can be notified.   Note that this
  // holds an nsWeakPtr to |ds|, so it's ok.
  rv = ds->AddProgressListener(ds, notifyMask);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  // If our BrowsingContext has private browsing enabled, update the number of
  // private browsing docshells.
  if (aBrowsingContext->UsePrivateBrowsing()) {
    ds->NotifyPrivateBrowsingChanged();
  }

  // If our parent window is present in this process, set up our parent now.
  RefPtr<WindowContext> parentWC = aBrowsingContext->GetParentWindowContext();
  if (parentWC && parentWC->IsInProcess()) {
    // If we don't have a parent element anymore, we can't finish this load!
    // How'd we get here?
    RefPtr<Element> parentElement = aBrowsingContext->GetEmbedderElement();
    if (!parentElement) {
      MOZ_ASSERT_UNREACHABLE("nsDocShell::Create() - !parentElement");
      return nullptr;
    }

    // We have an in-process parent window, but don't have a parent nsDocShell?
    // How'd we get here!
    nsCOMPtr<nsIDocShell> parentShell =
        parentElement->OwnerDoc()->GetDocShell();
    if (!parentShell) {
      MOZ_ASSERT_UNREACHABLE("nsDocShell::Create() - !parentShell");
      return nullptr;
    }
    parentShell->AddChild(ds);
  }

  // Make |ds| the primary DocShell for the given context.
  aBrowsingContext->SetDocShell(ds);

  // Set |ds| default load flags on load group.
  ds->SetLoadGroupDefaultLoadFlags(aBrowsingContext->GetDefaultLoadFlags());

  return ds.forget();
}

void nsDocShell::DestroyChildren() {
  for (auto* child : mChildList.ForwardRange()) {
    nsCOMPtr<nsIDocShellTreeItem> shell = do_QueryObject(child);
    NS_ASSERTION(shell, "docshell has null child");

    if (shell) {
      shell->SetTreeOwner(nullptr);
    }
  }

  nsDocLoader::DestroyChildren();
}

NS_IMPL_CYCLE_COLLECTION_WEAK_PTR_INHERITED(nsDocShell, nsDocLoader,
                                            mScriptGlobal, mInitialClientSource,
                                            mBrowsingContext,
                                            mChromeEventHandler,
                                            mBCWebProgressStatusFilter)

NS_IMPL_ADDREF_INHERITED(nsDocShell, nsDocLoader)
NS_IMPL_RELEASE_INHERITED(nsDocShell, nsDocLoader)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsDocShell)
  NS_INTERFACE_MAP_ENTRY(nsIDocShell)
  NS_INTERFACE_MAP_ENTRY(nsIDocShellTreeItem)
  NS_INTERFACE_MAP_ENTRY(nsIWebNavigation)
  NS_INTERFACE_MAP_ENTRY(nsIBaseWindow)
  NS_INTERFACE_MAP_ENTRY(nsIRefreshURI)
  NS_INTERFACE_MAP_ENTRY(nsIWebProgressListener)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIWebPageDescriptor)
  NS_INTERFACE_MAP_ENTRY(nsIAuthPromptProvider)
  NS_INTERFACE_MAP_ENTRY(nsILoadContext)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsINetworkInterceptController,
                                     mInterceptController)
NS_INTERFACE_MAP_END_INHERITING(nsDocLoader)

NS_IMETHODIMP
nsDocShell::GetInterface(const nsIID& aIID, void** aSink) {
  MOZ_ASSERT(aSink, "null out param");

  *aSink = nullptr;

  if (aIID.Equals(NS_GET_IID(nsICommandManager))) {
    NS_ENSURE_SUCCESS(EnsureCommandHandler(), NS_ERROR_FAILURE);
    *aSink = static_cast<nsICommandManager*>(mCommandManager.get());
  } else if (aIID.Equals(NS_GET_IID(nsIURIContentListener))) {
    *aSink = mContentListener;
  } else if ((aIID.Equals(NS_GET_IID(nsIScriptGlobalObject)) ||
              aIID.Equals(NS_GET_IID(nsIGlobalObject)) ||
              aIID.Equals(NS_GET_IID(nsPIDOMWindowOuter)) ||
              aIID.Equals(NS_GET_IID(mozIDOMWindowProxy)) ||
              aIID.Equals(NS_GET_IID(nsIDOMWindow))) &&
             NS_SUCCEEDED(EnsureScriptEnvironment())) {
    return mScriptGlobal->QueryInterface(aIID, aSink);
  } else if (aIID.Equals(NS_GET_IID(Document)) && VerifyDocumentViewer()) {
    RefPtr<Document> doc = mDocumentViewer->GetDocument();
    doc.forget(aSink);
    return *aSink ? NS_OK : NS_NOINTERFACE;
  } else if (aIID.Equals(NS_GET_IID(nsIPrompt)) &&
             NS_SUCCEEDED(EnsureScriptEnvironment())) {
    nsresult rv;
    nsCOMPtr<nsIWindowWatcher> wwatch =
        do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    // Get the an auth prompter for our window so that the parenting
    // of the dialogs works as it should when using tabs.
    nsIPrompt* prompt;
    rv = wwatch->GetNewPrompter(mScriptGlobal, &prompt);
    NS_ENSURE_SUCCESS(rv, rv);

    *aSink = prompt;
    return NS_OK;
  } else if (aIID.Equals(NS_GET_IID(nsIAuthPrompt)) ||
             aIID.Equals(NS_GET_IID(nsIAuthPrompt2))) {
    return NS_SUCCEEDED(GetAuthPrompt(PROMPT_NORMAL, aIID, aSink))
               ? NS_OK
               : NS_NOINTERFACE;
  } else if (aIID.Equals(NS_GET_IID(nsISHistory))) {
    // This is deprecated, you should instead directly get
    // ChildSHistory from the browsing context.
    MOZ_DIAGNOSTIC_ASSERT(
        false, "Do not try to get a nsISHistory interface from nsIDocShell");
    return NS_NOINTERFACE;
  } else if (aIID.Equals(NS_GET_IID(nsIWebBrowserFind))) {
    nsresult rv = EnsureFind();
    if (NS_FAILED(rv)) {
      return rv;
    }

    *aSink = mFind;
    NS_ADDREF((nsISupports*)*aSink);
    return NS_OK;
  } else if (aIID.Equals(NS_GET_IID(nsISelectionDisplay))) {
    if (PresShell* presShell = GetPresShell()) {
      return presShell->QueryInterface(aIID, aSink);
    }
  } else if (aIID.Equals(NS_GET_IID(nsIDocShellTreeOwner))) {
    nsCOMPtr<nsIDocShellTreeOwner> treeOwner;
    nsresult rv = GetTreeOwner(getter_AddRefs(treeOwner));
    if (NS_SUCCEEDED(rv) && treeOwner) {
      return treeOwner->QueryInterface(aIID, aSink);
    }
  } else if (aIID.Equals(NS_GET_IID(nsIBrowserChild))) {
    *aSink = GetBrowserChild().take();
    return *aSink ? NS_OK : NS_ERROR_FAILURE;
  } else {
    return nsDocLoader::GetInterface(aIID, aSink);
  }

  NS_IF_ADDREF(((nsISupports*)*aSink));
  return *aSink ? NS_OK : NS_NOINTERFACE;
}

NS_IMETHODIMP
nsDocShell::SetCancelContentJSEpoch(int32_t aEpoch) {
  // Note: this gets called fairly early (before a pageload actually starts).
  // We could probably defer this even longer.
  nsCOMPtr<nsIBrowserChild> browserChild = GetBrowserChild();
  static_cast<BrowserChild*>(browserChild.get())
      ->SetCancelContentJSEpoch(aEpoch);
  return NS_OK;
}

nsresult nsDocShell::CheckDisallowedJavascriptLoad(
    nsDocShellLoadState* aLoadState) {
  if (!aLoadState->URI()->SchemeIs("javascript")) {
    return NS_OK;
  }

  if (nsCOMPtr<nsIPrincipal> targetPrincipal =
          GetInheritedPrincipal(/* aConsiderCurrentDocument */ true)) {
    if (!aLoadState->TriggeringPrincipal()->Subsumes(targetPrincipal)) {
      return NS_ERROR_DOM_BAD_CROSS_ORIGIN_URI;
    }
    return NS_OK;
  }
  return NS_ERROR_DOM_BAD_CROSS_ORIGIN_URI;
}

NS_IMETHODIMP
nsDocShell::LoadURI(nsDocShellLoadState* aLoadState, bool aSetNavigating) {
  return LoadURI(aLoadState, aSetNavigating, false);
}

nsresult nsDocShell::LoadURI(nsDocShellLoadState* aLoadState,
                             bool aSetNavigating,
                             bool aContinueHandlingSubframeHistory) {
  MOZ_ASSERT(aLoadState, "Must have a valid load state!");
  // NOTE: This comparison between what appears to be internal/external load
  // flags is intentional, as it's ensuring that the caller isn't using any of
  // the flags reserved for implementations by the `nsIWebNavigation` interface.
  // In the future, this check may be dropped.
  MOZ_ASSERT(
      (aLoadState->LoadFlags() & INTERNAL_LOAD_FLAGS_LOADURI_SETUP_FLAGS) == 0,
      "Should not have these flags set");
  MOZ_ASSERT(aLoadState->TargetBrowsingContext().IsNull(),
             "Targeting doesn't occur until InternalLoad");

  if (!aLoadState->TriggeringPrincipal()) {
    MOZ_ASSERT(false, "LoadURI must have a triggering principal");
    return NS_ERROR_FAILURE;
  }

  MOZ_TRY(CheckDisallowedJavascriptLoad(aLoadState));

  bool oldIsNavigating = mIsNavigating;
  auto cleanupIsNavigating =
      MakeScopeExit([&]() { mIsNavigating = oldIsNavigating; });
  if (aSetNavigating) {
    mIsNavigating = true;
  }

  PopupBlocker::PopupControlState popupState = PopupBlocker::openOverridden;
  if (aLoadState->HasLoadFlags(LOAD_FLAGS_ALLOW_POPUPS)) {
    popupState = PopupBlocker::openAllowed;
    // If we allow popups as part of the navigation, ensure we fake a user
    // interaction, so that popups can, in fact, be allowed to open.
    if (WindowContext* wc = mBrowsingContext->GetCurrentWindowContext()) {
      wc->NotifyUserGestureActivation();
    }
  }

  AutoPopupStatePusher statePusher(popupState);

  if (aLoadState->GetCancelContentJSEpoch().isSome()) {
    SetCancelContentJSEpoch(*aLoadState->GetCancelContentJSEpoch());
  }

  // Note: we allow loads to get through here even if mFiredUnloadEvent is
  // true; that case will get handled in LoadInternal or LoadHistoryEntry,
  // so we pass false as the second parameter to IsNavigationAllowed.
  // However, we don't allow the page to change location *in the middle of*
  // firing beforeunload, so we do need to check if *beforeunload* is currently
  // firing, so we call IsNavigationAllowed rather than just IsPrintingOrPP.
  if (!IsNavigationAllowed(true, false)) {
    return NS_OK;  // JS may not handle returning of an error code
  }

  nsLoadFlags defaultLoadFlags = mBrowsingContext->GetDefaultLoadFlags();
  if (aLoadState->HasLoadFlags(LOAD_FLAGS_FORCE_TRR)) {
    defaultLoadFlags |= nsIRequest::LOAD_TRR_ONLY_MODE;
  } else if (aLoadState->HasLoadFlags(LOAD_FLAGS_DISABLE_TRR)) {
    defaultLoadFlags |= nsIRequest::LOAD_TRR_DISABLED_MODE;
  }

  MOZ_ALWAYS_SUCCEEDS(mBrowsingContext->SetDefaultLoadFlags(defaultLoadFlags));

  if (!StartupTimeline::HasRecord(StartupTimeline::FIRST_LOAD_URI) &&
      mItemType == typeContent && !NS_IsAboutBlank(aLoadState->URI())) {
    StartupTimeline::RecordOnce(StartupTimeline::FIRST_LOAD_URI);
  }

  // LoadType used to be set to a default value here, if no LoadInfo/LoadState
  // object was passed in. That functionality has been removed as of bug
  // 1492648. LoadType should now be set up by the caller at the time they
  // create their nsDocShellLoadState object to pass into LoadURI.

  MOZ_LOG(
      gDocShellLeakLog, LogLevel::Debug,
      ("nsDocShell[%p]: loading %s with flags 0x%08x", this,
       aLoadState->URI()->GetSpecOrDefault().get(), aLoadState->LoadFlags()));

  if ((!aLoadState->LoadIsFromSessionHistory() &&
       !LOAD_TYPE_HAS_FLAGS(aLoadState->LoadType(),
                            LOAD_FLAGS_REPLACE_HISTORY)) ||
      aContinueHandlingSubframeHistory) {
    // This is possibly a subframe, so handle it accordingly.
    //
    // If history exists, it will be loaded into the aLoadState object, and the
    // LoadType will be changed.
    if (MaybeHandleSubframeHistory(aLoadState,
                                   aContinueHandlingSubframeHistory)) {
      // MaybeHandleSubframeHistory returns true if we need to continue loading
      // asynchronously.
      return NS_OK;
    }
  }

  if (aLoadState->LoadIsFromSessionHistory()) {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("nsDocShell[%p]: loading from session history", this));

    // FIXME Null check aLoadState->GetLoadingSessionHistoryInfo()?
    return LoadHistoryEntry(*aLoadState->GetLoadingSessionHistoryInfo(),
                            aLoadState->LoadType(),
                            aLoadState->HasValidUserGestureActivation(),
                            aLoadState->NotifiedBeforeUnloadListeners(),
                            aLoadState->IsResumingInterceptedNavigation());
  }

  // On history navigation via Back/Forward buttons, don't execute
  // automatic JavaScript redirection such as |location.href = ...| or
  // |window.open()|
  //
  // LOAD_NORMAL:        window.open(...) etc.
  // LOAD_STOP_CONTENT:  location.href = ..., location.assign(...)
  if ((aLoadState->LoadType() == LOAD_NORMAL ||
       aLoadState->LoadType() == LOAD_STOP_CONTENT) &&
      ShouldBlockLoadingForBackButton()) {
    return NS_OK;
  }

  BrowsingContext::Type bcType = mBrowsingContext->GetType();

  // Set up the inheriting principal in LoadState.
  nsresult rv = aLoadState->SetupInheritingPrincipal(
      bcType, mBrowsingContext->OriginAttributesRef());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadState->SetupTriggeringPrincipal(
      mBrowsingContext->OriginAttributesRef());
  NS_ENSURE_SUCCESS(rv, rv);

  aLoadState->CalculateLoadURIFlags();

  MOZ_ASSERT(aLoadState->TypeHint().IsVoid(),
             "Typehint should be null when calling InternalLoad from LoadURI");
  MOZ_ASSERT(aLoadState->FileName().IsVoid(),
             "FileName should be null when calling InternalLoad from LoadURI");
  MOZ_ASSERT(!aLoadState->LoadIsFromSessionHistory(),
             "Shouldn't be loading from an entry when calling InternalLoad "
             "from LoadURI");

  // If we have a system triggering principal, we can assume that this load was
  // triggered by some UI in the browser chrome, such as the URL bar or
  // bookmark bar. This should count as a user interaction for the current sh
  // entry, so that the user may navigate back to the current entry, from the
  // entry that is going to be added as part of this load.
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      aLoadState->TriggeringPrincipal();
  if (triggeringPrincipal && triggeringPrincipal->IsSystemPrincipal()) {
    WindowContext* topWc = mBrowsingContext->GetTopWindowContext();
    if (topWc && !topWc->IsDiscarded()) {
      MOZ_ALWAYS_SUCCEEDS(topWc->SetSHEntryHasUserInteraction(true));
    }
  }

  rv = InternalLoad(aLoadState);
  NS_ENSURE_SUCCESS(rv, rv);

  if (aLoadState->GetOriginalURIString().isSome()) {
    // Save URI string in case it's needed later when
    // sending to search engine service in EndPageLoad()
    mOriginalUriString = *aLoadState->GetOriginalURIString();
  }

  return NS_OK;
}

bool nsDocShell::IsLoadingFromSessionHistory() {
  return mActiveEntryIsLoadingFromSessionHistory;
}

// StopDetector is modeled similarly to OnloadBlocker; it is a rather
// dummy nsIRequest implementation which can be added to an nsILoadGroup to
// detect Cancel calls.
class StopDetector final : public nsIRequest {
 public:
  StopDetector() = default;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUEST

  bool Canceled() { return mCanceled; }

 private:
  ~StopDetector() = default;

  bool mCanceled = false;
};

NS_IMPL_ISUPPORTS(StopDetector, nsIRequest)

NS_IMETHODIMP
StopDetector::GetName(nsACString& aResult) {
  aResult.AssignLiteral("about:stop-detector");
  return NS_OK;
}

NS_IMETHODIMP
StopDetector::IsPending(bool* aRetVal) {
  *aRetVal = true;
  return NS_OK;
}

NS_IMETHODIMP
StopDetector::GetStatus(nsresult* aStatus) {
  *aStatus = NS_OK;
  return NS_OK;
}

NS_IMETHODIMP StopDetector::SetCanceledReason(const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP StopDetector::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP StopDetector::CancelWithReason(nsresult aStatus,
                                             const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP
StopDetector::Cancel(nsresult aStatus) {
  mCanceled = true;
  return NS_OK;
}

NS_IMETHODIMP
StopDetector::Suspend(void) { return NS_OK; }
NS_IMETHODIMP
StopDetector::Resume(void) { return NS_OK; }

NS_IMETHODIMP
StopDetector::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  *aLoadGroup = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
StopDetector::SetLoadGroup(nsILoadGroup* aLoadGroup) { return NS_OK; }

NS_IMETHODIMP
StopDetector::GetLoadFlags(nsLoadFlags* aLoadFlags) {
  *aLoadFlags = nsIRequest::LOAD_NORMAL;
  return NS_OK;
}

NS_IMETHODIMP
StopDetector::GetTRRMode(nsIRequest::TRRMode* aTRRMode) {
  return GetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
StopDetector::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  return SetTRRModeImpl(aTRRMode);
}

NS_IMETHODIMP
StopDetector::SetLoadFlags(nsLoadFlags aLoadFlags) { return NS_OK; }

bool nsDocShell::MaybeHandleSubframeHistory(
    nsDocShellLoadState* aLoadState, bool aContinueHandlingSubframeHistory) {
  // First, verify if this is a subframe.
  // Note, it is ok to rely on docshell here and not browsing context since when
  // an iframe is created, it has first in-process docshell.
  nsCOMPtr<nsIDocShellTreeItem> parentAsItem;
  GetInProcessSameTypeParent(getter_AddRefs(parentAsItem));
  nsCOMPtr<nsIDocShell> parentDS(do_QueryInterface(parentAsItem));

  if (!parentDS || parentDS == static_cast<nsIDocShell*>(this)) {
    if (mBrowsingContext && mBrowsingContext->IsTop() &&
        !aLoadState->HistoryBehavior()) {
      // This is the root docshell. If we got here while
      // executing an onLoad Handler,this load will not go
      // into session history.
      // XXX Why is this code in a method which deals with iframes!
      if (aLoadState->IsFormSubmission()) {
#ifdef DEBUG
        if (!mEODForCurrentDocument) {
          const MaybeDiscarded<BrowsingContext>& targetBC =
              aLoadState->TargetBrowsingContext();
          MOZ_ASSERT_IF(GetBrowsingContext() == targetBC.get(),
                        aLoadState->LoadType() == LOAD_NORMAL_REPLACE);
        }
#endif
      } else {
        bool inOnLoadHandler = false;
        GetIsExecutingOnLoadHandler(&inOnLoadHandler);
        if (inOnLoadHandler) {
          aLoadState->SetLoadType(LOAD_NORMAL_REPLACE);
        }
      }
    }
    return false;
  }

  /* OK. It is a subframe. Checkout the parent's loadtype. If the parent was
   * loaded through a history mechanism, then get the SH entry for the child
   * from the parent. This is done to restore frameset navigation while going
   * back/forward. If the parent was loaded through any other loadType, set the
   * child's loadType too accordingly, so that session history does not get
   * confused.
   */

  // Get the parent's load type
  uint32_t parentLoadType;
  parentDS->GetLoadType(&parentLoadType);

  if (!aContinueHandlingSubframeHistory) {
    if (nsDocShell::Cast(parentDS.get())->IsLoadingFromSessionHistory() &&
        !GetCreatedDynamically()) {
      if (XRE_IsContentProcess()) {
        dom::ContentChild* contentChild = dom::ContentChild::GetSingleton();
        nsCOMPtr<nsILoadGroup> loadGroup;
        GetLoadGroup(getter_AddRefs(loadGroup));
        if (contentChild && loadGroup && !GetIsAttemptingToNavigate()) {
          RefPtr<Document> parentDoc = parentDS->GetDocument();
          parentDoc->BlockOnload();
          RefPtr<BrowsingContext> browsingContext = mBrowsingContext;
          Maybe<uint64_t> currentLoadIdentifier =
              mBrowsingContext->GetCurrentLoadIdentifier();
          RefPtr<nsDocShellLoadState> loadState = aLoadState;
          bool isNavigating = mIsNavigating;
          RefPtr stopDetector = MakeRefPtr<StopDetector>();
          loadGroup->AddRequest(stopDetector, nullptr);
          // Need to set mCheckingSessionHistory so that
          // GetIsAttemptingToNavigate() returns true.
          mCheckingSessionHistory = true;

          auto resolve =
              [currentLoadIdentifier, browsingContext, parentDoc, loadState,
               isNavigating, loadGroup, stopDetector](
                  mozilla::Maybe<LoadingSessionHistoryInfo>&& aResult) {
                RefPtr<nsDocShell> docShell =
                    static_cast<nsDocShell*>(browsingContext->GetDocShell());
                auto unblockParent = MakeScopeExit(
                    [loadGroup, stopDetector, parentDoc, docShell]() {
                      if (docShell) {
                        docShell->mCheckingSessionHistory = false;
                      }
                      loadGroup->RemoveRequest(stopDetector, nullptr, NS_OK);
                      parentDoc->UnblockOnload(false);
                    });

                if (!docShell || !docShell->mCheckingSessionHistory) {
                  return;
                }

                if (stopDetector->Canceled()) {
                  return;
                }
                if (currentLoadIdentifier ==
                        browsingContext->GetCurrentLoadIdentifier() &&
                    aResult.isSome()) {
                  loadState->SetLoadingSessionHistoryInfo(aResult.value());
                  // This is an initial subframe load from the session
                  // history, index doesn't need to be updated.
                  loadState->SetLoadIsFromSessionHistory(0, false);
                }

                // We got the results back from the parent process, call
                // LoadURI again with the possibly updated data.
                docShell->LoadURI(loadState, isNavigating, true);
              };
          auto reject = [loadGroup, stopDetector, browsingContext,
                         parentDoc](mozilla::ipc::ResponseRejectReason) {
            RefPtr<nsDocShell> docShell =
                static_cast<nsDocShell*>(browsingContext->GetDocShell());
            if (docShell) {
              docShell->mCheckingSessionHistory = false;
            }
            // In practise reject shouldn't be called ever.
            loadGroup->RemoveRequest(stopDetector, nullptr, NS_OK);
            parentDoc->UnblockOnload(false);
          };
          contentChild->SendGetLoadingSessionHistoryInfoFromParent(
              mBrowsingContext, std::move(resolve), std::move(reject));
          return true;
        }
      } else {
        Maybe<LoadingSessionHistoryInfo> info;
        mBrowsingContext->Canonical()->GetLoadingSessionHistoryInfoFromParent(
            info);
        if (info.isSome()) {
          aLoadState->SetLoadingSessionHistoryInfo(info.value());
          // This is an initial subframe load from the session
          // history, index doesn't need to be updated.
          aLoadState->SetLoadIsFromSessionHistory(0, false);
        }
      }
    }
  }

  // Make some decisions on the child frame's loadType based on the
  // parent's loadType, if the subframe hasn't loaded anything into it.
  //
  // In some cases privileged scripts may try to get the DOMWindow
  // reference of this docshell before the loading starts, causing the
  // initial about:blank content viewer being created and mCurrentURI being
  // set. To handle this case we check if mCurrentURI is about:blank.

  if (mCurrentURI &&
      (!NS_IsAboutBlank(mCurrentURI) || mLoadingEntry || mActiveEntry) &&
      !aLoadState->HistoryBehavior()) {
    // This is a pre-existing subframe. If
    // 1. The load of this frame was not originally initiated by session
    //    history directly (i.e. (!shEntry) condition succeeded, but it can
    //    still be a history load on parent which causes this frame being
    //    loaded), which we checked with the above assert, and
    // 2. mCurrentURI is not null, nor the initial about:blank,
    // it is possible that a parent's onLoadHandler or even self's
    // onLoadHandler is loading a new page in this child. Check parent's and
    // self's busy flag and if it is set, we don't want this onLoadHandler
    // load to get in to session history.
    BusyFlags parentBusy = parentDS->GetBusyFlags();
    BusyFlags selfBusy = GetBusyFlags();

    if (parentBusy & BUSY_FLAGS_BUSY || selfBusy & BUSY_FLAGS_BUSY) {
      aLoadState->SetLoadType(LOAD_NORMAL_REPLACE);
      aLoadState->ClearLoadIsFromSessionHistory();
    }
    return false;
  }

  // This is a newly created frame. Check for exception cases first.
  // By default the subframe will inherit the parent's loadType.
  if (aLoadState->LoadIsFromSessionHistory() &&
      (parentLoadType == LOAD_NORMAL || parentLoadType == LOAD_LINK)) {
    // The parent was loaded normally. In this case, this *brand new*
    // child really shouldn't have a SHEntry. If it does, it could be
    // because the parent is replacing an existing frame with a new frame,
    // in the onLoadHandler. We don't want this url to get into session
    // history. Clear off shEntry, and set load type to
    // LOAD_BYPASS_HISTORY.
    bool inOnLoadHandler = false;
    parentDS->GetIsExecutingOnLoadHandler(&inOnLoadHandler);
    if (inOnLoadHandler) {
      aLoadState->SetLoadType(LOAD_NORMAL_REPLACE);
      aLoadState->ClearLoadIsFromSessionHistory();
    }
  } else if (parentLoadType == LOAD_REFRESH) {
    // Clear shEntry. For refresh loads, we have to load
    // what comes through the pipe, not what's in history.
    aLoadState->ClearLoadIsFromSessionHistory();
  } else if ((parentLoadType == LOAD_BYPASS_HISTORY) ||
             (aLoadState->LoadIsFromSessionHistory() &&
              ((parentLoadType & LOAD_CMD_HISTORY) ||
               (parentLoadType == LOAD_RELOAD_NORMAL) ||
               (parentLoadType == LOAD_RELOAD_CHARSET_CHANGE) ||
               (parentLoadType == LOAD_RELOAD_CHARSET_CHANGE_BYPASS_CACHE) ||
               (parentLoadType ==
                LOAD_RELOAD_CHARSET_CHANGE_BYPASS_PROXY_AND_CACHE)))) {
    // If the parent url, bypassed history or was loaded from
    // history, pass on the parent's loadType to the new child
    // frame too, so that the child frame will also
    // avoid getting into history.
    aLoadState->SetLoadType(parentLoadType);
  } else if (parentLoadType == LOAD_ERROR_PAGE) {
    // If the parent document is an error page, we don't
    // want to update global/session history. However,
    // this child frame is not an error page.
    aLoadState->SetLoadType(LOAD_BYPASS_HISTORY);
  } else if ((parentLoadType == LOAD_RELOAD_BYPASS_CACHE) ||
             (parentLoadType == LOAD_RELOAD_BYPASS_PROXY) ||
             (parentLoadType == LOAD_RELOAD_BYPASS_PROXY_AND_CACHE)) {
    // the new frame should inherit the parent's load type so that it also
    // bypasses the cache and/or proxy
    aLoadState->SetLoadType(parentLoadType);
  }

  return false;
}

/*
 * Reset state to a new content model within the current document and the
 * document viewer. Called by the document before initiating an out of band
 * document.write().
 */
NS_IMETHODIMP
nsDocShell::PrepareForNewContentModel() {
  // Clear out our form control state, because the state of controls
  // in the pre-open() document should not affect the state of
  // controls that are now going to be written.
  SetLayoutHistoryState(nullptr);
  mEODForCurrentDocument = false;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::FirePageHideNotification() {
  FirePageHideNotificationInternal(false);
  return NS_OK;
}

void nsDocShell::FirePageHideNotificationInternal(
    bool aSkipCheckingDynEntries) {
  {
    nsAutoMicroTask mt;
    SetOngoingNavigation(Nothing());
  }

  if (mDocumentViewer && !mFiredUnloadEvent) {
    // Keep an explicit reference since calling PageHide could release
    // mDocumentViewer
    nsCOMPtr<nsIDocumentViewer> viewer(mDocumentViewer);
    mFiredUnloadEvent = true;

    if (mTiming) {
      mTiming->NotifyUnloadEventStart();
    }

    viewer->PageHide(true);

    if (mTiming) {
      mTiming->NotifyUnloadEventEnd();
    }

    AutoTArray<nsCOMPtr<nsIDocShell>, 8> kids;
    uint32_t n = mChildList.Length();
    kids.SetCapacity(n);
    for (uint32_t i = 0; i < n; i++) {
      kids.AppendElement(do_QueryInterface(ChildAt(i)));
    }

    n = kids.Length();
    for (uint32_t i = 0; i < n; ++i) {
      RefPtr<nsDocShell> child = static_cast<nsDocShell*>(kids[i].get());
      if (child) {
        // Skip checking dynamic subframe entries in our children.
        child->FirePageHideNotificationInternal(true);
      }
    }

    // If the document is unloading, remove all dynamic subframe entries.
    if (!aSkipCheckingDynEntries) {
      RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
      if (rootSH) {
        MOZ_LOG(
            gSHLog, LogLevel::Debug,
            ("nsDocShell %p unloading, remove dynamic subframe entries", this));
        if (mActiveEntry) {
          mBrowsingContext->RemoveDynEntriesFromActiveSessionHistoryEntry();
        }
        MOZ_LOG(gSHLog, LogLevel::Debug,
                ("nsDocShell %p unloading, no active entries", this));
      }
    }

    // Now make sure our editor, if any, is detached before we go
    // any farther.
    DetachEditorFromWindow();
  }
}

void nsDocShell::ThawFreezeNonRecursive(bool aThaw) {
  MOZ_ASSERT(mozilla::BFCacheInParent());

  if (!mScriptGlobal) {
    return;
  }

  if (RefPtr<nsGlobalWindowInner> inner =
          nsGlobalWindowInner::Cast(mScriptGlobal->GetCurrentInnerWindow())) {
    if (aThaw) {
      inner->Thaw(false);
    } else {
      inner->Freeze(false);
    }
  }
}

void nsDocShell::FirePageHideShowNonRecursive(bool aShow) {
  MOZ_ASSERT(mozilla::BFCacheInParent());

  if (!mDocumentViewer) {
    return;
  }

  // Emulate what non-SHIP BFCache does too. In pageshow case
  // add and remove a request and before that call SetCurrentURI to get
  // the location change notification.
  // For pagehide, set mFiredUnloadEvent to true, so that unload doesn't fire.
  nsCOMPtr<nsIDocumentViewer> viewer(mDocumentViewer);
  if (aShow) {
    viewer->SetIsHidden(false);
    mRefreshURIList = std::move(mBFCachedRefreshURIList);
    RefreshURIFromQueue();
    mFiredUnloadEvent = false;
    RefPtr<Document> doc = viewer->GetDocument();
    if (doc) {
      doc->NotifyActivityChanged();
      nsCOMPtr<nsPIDOMWindowInner> inner =
          mScriptGlobal ? mScriptGlobal->GetCurrentInnerWindow() : nullptr;
      if (mBrowsingContext->IsTop()) {
        doc->NotifyPossibleTitleChange(false);
        doc->SetLoadingOrRestoredFromBFCacheTimeStampToNow();
        if (inner) {
          // Now that we have found the inner window of the page restored
          // from the history, we have to make sure that
          // performance.navigation.type is 2.
          // Traditionally this type change has been done to the top level page
          // only.
          Performance* performance = inner->GetPerformance();
          if (performance) {
            performance->GetDOMTiming()->NotifyRestoreStart();
          }
        }
      }

      nsCOMPtr<nsIChannel> channel = doc->GetChannel();
      if (channel) {
        SetLoadType(LOAD_HISTORY);
        mEODForCurrentDocument = false;
        mIsRestoringDocument = true;
        mLoadGroup->AddRequest(channel, nullptr);
        nsCOMPtr<nsIURI> uri;
        if (doc->FragmentDirective()) {
          // If we have fragment directives, then we've mutated the document
          // uri. Set the current URI from session history instead.
          uri = mActiveEntry ? mActiveEntry->GetURI() : nullptr;
        }
        if (!uri) {
          uri = doc->GetDocumentURI();
        }
        SetCurrentURI(uri, channel,
                      /* aFireOnLocationChange */ true,
                      /* aIsInitialAboutBlank */ false,
                      /* aLocationFlags */ 0);
        mLoadGroup->RemoveRequest(channel, nullptr, NS_OK);
        mIsRestoringDocument = false;
      }
      RefPtr<PresShell> presShell = GetPresShell();
      if (presShell) {
        presShell->Thaw(false);
      }

      if (inner) {
        inner->FireDelayedDOMEvents(false);
      }
    }
  } else if (!mFiredUnloadEvent) {
    // XXXBFCache check again that the page can enter bfcache.
    // XXXBFCache should mTiming->NotifyUnloadEventStart()/End() be called here?

    if (mRefreshURIList) {
      RefreshURIToQueue();
      mBFCachedRefreshURIList = std::move(mRefreshURIList);
    } else {
      // If Stop was called, the list was moved to mSavedRefreshURIList after
      // calling SuspendRefreshURIs, which calls RefreshURIToQueue.
      mBFCachedRefreshURIList = std::move(mSavedRefreshURIList);
    }

    mFiredUnloadEvent = true;
    viewer->PageHide(false);

    RefPtr<PresShell> presShell = GetPresShell();
    if (presShell) {
      presShell->Freeze(false);
    }
  }
}

nsresult nsDocShell::Dispatch(already_AddRefed<nsIRunnable> aRunnable) {
  nsCOMPtr<nsIRunnable> runnable(aRunnable);
  if (NS_WARN_IF(!GetWindow())) {
    // Window should only be unavailable after destroyed.
    MOZ_ASSERT(mIsBeingDestroyed);
    return NS_ERROR_FAILURE;
  }
  return SchedulerGroup::Dispatch(runnable.forget());
}

NS_IMETHODIMP
nsDocShell::DispatchLocationChangeEvent() {
  return Dispatch(NewRunnableMethod("nsDocShell::FireDummyOnLocationChange",
                                    this,
                                    &nsDocShell::FireDummyOnLocationChange));
}

NS_IMETHODIMP
nsDocShell::StartDelayedAutoplayMediaComponents() {
  RefPtr<nsPIDOMWindowOuter> outerWindow = GetWindow();
  if (outerWindow) {
    outerWindow->ActivateMediaComponents();
  }
  return NS_OK;
}

bool nsDocShell::MaybeInitTiming() {
  if (mTiming && !mBlankTiming) {
    return false;
  }

  bool canBeReset = false;

  if (mScriptGlobal && mBlankTiming) {
    nsPIDOMWindowInner* innerWin = mScriptGlobal->GetCurrentInnerWindow();
    if (innerWin && innerWin->GetPerformance()) {
      mTiming = innerWin->GetPerformance()->GetDOMTiming();
      mBlankTiming = false;
    }
  }

  if (!mTiming) {
    mTiming = new nsDOMNavigationTiming(this);
    canBeReset = true;
  }

  mTiming->NotifyNavigationStart(
      mBrowsingContext->IsActive()
          ? nsDOMNavigationTiming::DocShellState::eActive
          : nsDOMNavigationTiming::DocShellState::eInactive);

  return canBeReset;
}

void nsDocShell::MaybeResetInitTiming(bool aReset) {
  if (aReset) {
    mTiming = nullptr;
  }
}

nsDOMNavigationTiming* nsDocShell::GetNavigationTiming() const {
  return mTiming;
}

nsPresContext* nsDocShell::GetEldestPresContext() {
  nsIDocumentViewer* viewer = mDocumentViewer;
  while (viewer) {
    nsIDocumentViewer* prevViewer = viewer->GetPreviousViewer();
    if (!prevViewer) {
      return viewer->GetPresContext();
    }
    viewer = prevViewer;
  }

  return nullptr;
}

nsPresContext* nsDocShell::GetPresContext() {
  if (!mDocumentViewer) {
    return nullptr;
  }

  return mDocumentViewer->GetPresContext();
}

PresShell* nsDocShell::GetPresShell() {
  nsPresContext* presContext = GetPresContext();
  return presContext ? presContext->GetPresShell() : nullptr;
}

PresShell* nsDocShell::GetEldestPresShell() {
  nsPresContext* presContext = GetEldestPresContext();

  if (presContext) {
    return presContext->GetPresShell();
  }

  return nullptr;
}

NS_IMETHODIMP
nsDocShell::GetDocViewer(nsIDocumentViewer** aDocumentViewer) {
  NS_ENSURE_ARG_POINTER(aDocumentViewer);

  *aDocumentViewer = mDocumentViewer;
  NS_IF_ADDREF(*aDocumentViewer);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetOuterWindowID(uint64_t* aWindowID) {
  *aWindowID = mContentWindowID;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetChromeEventHandler(EventTarget* aChromeEventHandler) {
  mChromeEventHandler = aChromeEventHandler;

  if (mScriptGlobal) {
    mScriptGlobal->SetChromeEventHandler(mChromeEventHandler);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetChromeEventHandler(EventTarget** aChromeEventHandler) {
  NS_ENSURE_ARG_POINTER(aChromeEventHandler);
  RefPtr<EventTarget> handler = mChromeEventHandler;
  handler.forget(aChromeEventHandler);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetCurrentURIForSessionStore(nsIURI* aURI) {
  // Note that securityUI will set STATE_IS_INSECURE, even if
  // the scheme of |aURI| is "https".
  SetCurrentURI(aURI, nullptr,
                /* aFireOnLocationChange */
                true,
                /* aIsInitialAboutBlank */
                false,
                /* aLocationFlags */
                nsIWebProgressListener::LOCATION_CHANGE_SESSION_STORE);
  return NS_OK;
}

bool nsDocShell::SetCurrentURI(nsIURI* aURI, nsIRequest* aRequest,
                               bool aFireOnLocationChange,
                               bool aIsInitialAboutBlank,
                               uint32_t aLocationFlags) {
  MOZ_ASSERT(!mIsBeingDestroyed);

  MOZ_LOG(gDocShellLeakLog, LogLevel::Debug,
          ("DOCSHELL %p SetCurrentURI %s\n", this,
           aURI ? aURI->GetSpecOrDefault().get() : ""));

  // We don't want to send a location change when we're displaying an error
  // page, and we don't want to change our idea of "current URI" either
  if (mLoadType == LOAD_ERROR_PAGE) {
    return false;
  }

  bool uriIsEqual = false;
  if (!mCurrentURI || !aURI ||
      NS_FAILED(mCurrentURI->Equals(aURI, &uriIsEqual)) || !uriIsEqual) {
    mTitleValidForCurrentURI = false;
  }

  SetCurrentURIInternal(aURI);

#ifdef DEBUG
  mLastOpenedURI = aURI;
#endif

  if (!NS_IsAboutBlankAllowQueryAndFragment(mCurrentURI)) {
    mHasLoadedNonBlankURI = true;
  }

  // Don't fire onLocationChange when creating a the initial about:blank
  // document, as this can happen when it's not safe for us to run script.
  // Note that if this initial about:blank isn't immediately navigated
  // away from, the onLocationChange will be fired as part of committing
  // to keeping the initial about:blank as the actual first initial
  // navigation destination.
  if (aIsInitialAboutBlank) {
    MOZ_ASSERT(!mHasLoadedNonBlankURI && !aRequest && aLocationFlags == 0);
    return false;
  }

  MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());

  if (aFireOnLocationChange) {
    FireOnLocationChange(this, aRequest, aURI, aLocationFlags);
  }
  return !aFireOnLocationChange;
}

void nsDocShell::SetCurrentURIInternal(nsIURI* aURI) {
  mCurrentURI = aURI;
  if (mBrowsingContext) {
    mBrowsingContext->ClearCachedValuesOfLocations();
  }
}

NS_IMETHODIMP
nsDocShell::GetCharset(nsACString& aCharset) {
  aCharset.Truncate();

  PresShell* presShell = GetPresShell();
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);
  Document* doc = presShell->GetDocument();
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);
  doc->GetDocumentCharacterSet()->Name(aCharset);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::ForceEncodingDetection() {
  nsCOMPtr<nsIDocumentViewer> viewer;
  GetDocViewer(getter_AddRefs(viewer));
  if (!viewer) {
    return NS_OK;
  }

  Document* doc = viewer->GetDocument();
  if (!doc || doc->WillIgnoreCharsetOverride()) {
    return NS_OK;
  }

  mForcedAutodetection = true;

  nsIURI* uri = doc->GetOriginalURI();
  bool isFileURL = uri && uri->SchemeIs("file");

  int32_t charsetSource = doc->GetDocumentCharacterSetSource();
  auto encoding = doc->GetDocumentCharacterSet();
  // AsHTMLDocument is valid, because we called
  // WillIgnoreCharsetOverride() above.
  if (doc->AsHTMLDocument()->IsPlainText()) {
    switch (charsetSource) {
      case kCharsetFromInitialAutoDetectionASCII:
        // Deliberately no final version
        LOGCHARSETMENU(("TEXT:UnlabeledAscii"));
        break;
      case kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Generic:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Generic:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8GenericInitialWasASCII:
      case kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Content:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Content:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8ContentInitialWasASCII:
        LOGCHARSETMENU(("TEXT:UnlabeledNonUtf8"));
        break;
      case kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLDInitialWasASCII:
        LOGCHARSETMENU(("TEXT:UnlabeledNonUtf8TLD"));
        break;
      case kCharsetFromInitialAutoDetectionWouldHaveBeenUTF8:
      case kCharsetFromFinalAutoDetectionWouldHaveBeenUTF8InitialWasASCII:
        LOGCHARSETMENU(("TEXT:UnlabeledUtf8"));
        break;
      case kCharsetFromChannel:
        if (encoding == UTF_8_ENCODING) {
          LOGCHARSETMENU(("TEXT:ChannelUtf8"));
        } else {
          LOGCHARSETMENU(("TEXT:ChannelNonUtf8"));
        }
        break;
      default:
        LOGCHARSETMENU(("TEXT:Bug"));
        break;
    }
  } else {
    switch (charsetSource) {
      case kCharsetFromInitialAutoDetectionASCII:
        // Deliberately no final version
        LOGCHARSETMENU(("HTML:UnlabeledAscii"));
        break;
      case kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Generic:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Generic:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8GenericInitialWasASCII:
      case kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8Content:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8Content:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8ContentInitialWasASCII:
        LOGCHARSETMENU(("HTML:UnlabeledNonUtf8"));
        break;
      case kCharsetFromInitialAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLD:
      case kCharsetFromFinalAutoDetectionWouldNotHaveBeenUTF8DependedOnTLDInitialWasASCII:
        LOGCHARSETMENU(("HTML:UnlabeledNonUtf8TLD"));
        break;
      case kCharsetFromInitialAutoDetectionWouldHaveBeenUTF8:
      case kCharsetFromFinalAutoDetectionWouldHaveBeenUTF8InitialWasASCII:
        LOGCHARSETMENU(("HTML:UnlabeledUtf8"));
        break;
      case kCharsetFromChannel:
        if (encoding == UTF_8_ENCODING) {
          LOGCHARSETMENU(("HTML:ChannelUtf8"));
        } else {
          LOGCHARSETMENU(("HTML:ChannelNonUtf8"));
        }
        break;
      case kCharsetFromXmlDeclaration:
      case kCharsetFromMetaTag:
        if (isFileURL) {
          LOGCHARSETMENU(("HTML:LocalLabeled"));
        } else if (encoding == UTF_8_ENCODING) {
          LOGCHARSETMENU(("HTML:MetaUtf8"));
        } else {
          LOGCHARSETMENU(("HTML:MetaNonUtf8"));
        }
        break;
      default:
        LOGCHARSETMENU(("HTML:Bug"));
        break;
    }
  }
  return NS_OK;
}

void nsDocShell::SetParentCharset(const Encoding*& aCharset,
                                  int32_t aCharsetSource,
                                  nsIPrincipal* aPrincipal) {
  mParentCharset = aCharset;
  mParentCharsetSource = aCharsetSource;
  mParentCharsetPrincipal = aPrincipal;
}

void nsDocShell::GetParentCharset(const Encoding*& aCharset,
                                  int32_t* aCharsetSource,
                                  nsIPrincipal** aPrincipal) {
  aCharset = mParentCharset;
  *aCharsetSource = mParentCharsetSource;
  NS_IF_ADDREF(*aPrincipal = mParentCharsetPrincipal);
}

NS_IMETHODIMP
nsDocShell::GetHasTrackingContentBlocked(Promise** aPromise) {
  MOZ_ASSERT(aPromise);

  ErrorResult rv;
  RefPtr<Document> doc(GetDocument());
  RefPtr<Promise> retPromise = Promise::Create(doc->GetRelevantGlobal(), rv);
  if (NS_WARN_IF(rv.Failed())) {
    return rv.StealNSResult();
  }

  // Retrieve the document's content blocking events from the parent process.
  RefPtr<Document::GetContentBlockingEventsPromise> promise =
      doc->GetContentBlockingEvents();
  if (promise) {
    promise->Then(
        GetCurrentSerialEventTarget(), __func__,
        [retPromise](const Document::GetContentBlockingEventsPromise::
                         ResolveOrRejectValue& aValue) {
          if (aValue.IsResolve()) {
            bool has = aValue.ResolveValue() &
                       nsIWebProgressListener::STATE_BLOCKED_TRACKING_CONTENT;
            retPromise->MaybeResolve(has);
          } else {
            retPromise->MaybeResolve(false);
          }
        });
  } else {
    retPromise->MaybeResolve(false);
  }

  retPromise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetCssErrorReportingEnabled(bool* aEnabled) {
  MOZ_ASSERT(aEnabled);
  *aEnabled = mCSSErrorReportingEnabled;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetCssErrorReportingEnabled(bool aEnabled) {
  mCSSErrorReportingEnabled = aEnabled;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetUsePrivateBrowsing(bool* aUsePrivateBrowsing) {
  NS_ENSURE_ARG_POINTER(aUsePrivateBrowsing);
  return mBrowsingContext->GetUsePrivateBrowsing(aUsePrivateBrowsing);
}

void nsDocShell::NotifyPrivateBrowsingChanged() {
  MOZ_ASSERT(!mIsBeingDestroyed);

  nsTObserverArray<nsWeakPtr>::ForwardIterator iter(mPrivacyObservers);
  while (iter.HasMore()) {
    nsWeakPtr ref = iter.GetNext();
    nsCOMPtr<nsIPrivacyTransitionObserver> obs = do_QueryReferent(ref);
    if (!obs) {
      iter.Remove();
    } else {
      obs->PrivateModeChanged(UsePrivateBrowsing());
    }
  }
}

NS_IMETHODIMP
nsDocShell::SetUsePrivateBrowsing(bool aUsePrivateBrowsing) {
  return mBrowsingContext->SetUsePrivateBrowsing(aUsePrivateBrowsing);
}

NS_IMETHODIMP
nsDocShell::SetPrivateBrowsing(bool aUsePrivateBrowsing) {
  return mBrowsingContext->SetPrivateBrowsing(aUsePrivateBrowsing);
}

NS_IMETHODIMP
nsDocShell::GetHasLoadedNonBlankURI(bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);

  *aResult = mHasLoadedNonBlankURI;
  return NS_OK;
}

bool nsDocShell::HasStartedLoadingOtherThanInitialBlankURI() {
  return mHasStartedLoadingOtherThanInitialBlankURI;
}

NS_IMETHODIMP
nsDocShell::GetUseRemoteTabs(bool* aUseRemoteTabs) {
  NS_ENSURE_ARG_POINTER(aUseRemoteTabs);
  return mBrowsingContext->GetUseRemoteTabs(aUseRemoteTabs);
}

NS_IMETHODIMP
nsDocShell::SetRemoteTabs(bool aUseRemoteTabs) {
  return mBrowsingContext->SetRemoteTabs(aUseRemoteTabs);
}

NS_IMETHODIMP
nsDocShell::GetUseRemoteSubframes(bool* aUseRemoteSubframes) {
  NS_ENSURE_ARG_POINTER(aUseRemoteSubframes);
  return mBrowsingContext->GetUseRemoteSubframes(aUseRemoteSubframes);
}

NS_IMETHODIMP
nsDocShell::SetRemoteSubframes(bool aUseRemoteSubframes) {
  return mBrowsingContext->SetRemoteSubframes(aUseRemoteSubframes);
}

NS_IMETHODIMP
nsDocShell::AddWeakPrivacyTransitionObserver(
    nsIPrivacyTransitionObserver* aObserver) {
  nsWeakPtr weakObs = do_GetWeakReference(aObserver);
  if (!weakObs) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  mPrivacyObservers.AppendElement(weakObs);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::AddWeakReflowObserver(nsIReflowObserver* aObserver) {
  nsWeakPtr weakObs = do_GetWeakReference(aObserver);
  if (!weakObs) {
    return NS_ERROR_FAILURE;
  }
  mReflowObservers.AppendElement(weakObs);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::RemoveWeakReflowObserver(nsIReflowObserver* aObserver) {
  nsWeakPtr obs = do_GetWeakReference(aObserver);
  return mReflowObservers.RemoveElement(obs) ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDocShell::NotifyReflowObservers(bool aInterruptible,
                                  DOMHighResTimeStamp aStart,
                                  DOMHighResTimeStamp aEnd) {
  nsTObserverArray<nsWeakPtr>::ForwardIterator iter(mReflowObservers);
  while (iter.HasMore()) {
    nsWeakPtr ref = iter.GetNext();
    nsCOMPtr<nsIReflowObserver> obs = do_QueryReferent(ref);
    if (!obs) {
      iter.Remove();
    } else if (aInterruptible) {
      obs->ReflowInterruptible(aStart, aEnd);
    } else {
      obs->Reflow(aStart, aEnd);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowMetaRedirects(bool* aReturn) {
  NS_ENSURE_ARG_POINTER(aReturn);

  *aReturn = mAllowMetaRedirects;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowMetaRedirects(bool aValue) {
  mAllowMetaRedirects = aValue;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowSubframes(bool* aAllowSubframes) {
  NS_ENSURE_ARG_POINTER(aAllowSubframes);

  *aAllowSubframes = mAllowSubframes;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowSubframes(bool aAllowSubframes) {
  mAllowSubframes = aAllowSubframes;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowImages(bool* aAllowImages) {
  NS_ENSURE_ARG_POINTER(aAllowImages);

  *aAllowImages = mAllowImages;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowImages(bool aAllowImages) {
  mAllowImages = aAllowImages;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowMedia(bool* aAllowMedia) {
  *aAllowMedia = mAllowMedia;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowMedia(bool aAllowMedia) {
  mAllowMedia = aAllowMedia;

  // Mute or unmute audio contexts attached to the inner window.
  if (mScriptGlobal) {
    if (nsPIDOMWindowInner* innerWin = mScriptGlobal->GetCurrentInnerWindow()) {
      if (aAllowMedia) {
        innerWin->UnmuteAudioContexts();
      } else {
        innerWin->MuteAudioContexts();
      }
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowDNSPrefetch(bool* aAllowDNSPrefetch) {
  *aAllowDNSPrefetch = mAllowDNSPrefetch;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowDNSPrefetch(bool aAllowDNSPrefetch) {
  mAllowDNSPrefetch = aAllowDNSPrefetch;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowWindowControl(bool* aAllowWindowControl) {
  *aAllowWindowControl = mAllowWindowControl;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowWindowControl(bool aAllowWindowControl) {
  mAllowWindowControl = aAllowWindowControl;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowContentRetargeting(bool* aAllowContentRetargeting) {
  *aAllowContentRetargeting = mBrowsingContext->GetAllowContentRetargeting();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowContentRetargeting(bool aAllowContentRetargeting) {
  BrowsingContext::Transaction txn;
  txn.SetAllowContentRetargeting(aAllowContentRetargeting);
  txn.SetAllowContentRetargetingOnChildren(aAllowContentRetargeting);
  return txn.Commit(mBrowsingContext);
}

NS_IMETHODIMP
nsDocShell::GetAllowContentRetargetingOnChildren(
    bool* aAllowContentRetargetingOnChildren) {
  *aAllowContentRetargetingOnChildren =
      mBrowsingContext->GetAllowContentRetargetingOnChildren();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowContentRetargetingOnChildren(
    bool aAllowContentRetargetingOnChildren) {
  return mBrowsingContext->SetAllowContentRetargetingOnChildren(
      aAllowContentRetargetingOnChildren);
}

NS_IMETHODIMP
nsDocShell::GetMayEnableCharacterEncodingMenu(
    bool* aMayEnableCharacterEncodingMenu) {
  *aMayEnableCharacterEncodingMenu = false;
  if (!mDocumentViewer) {
    return NS_OK;
  }
  Document* doc = mDocumentViewer->GetDocument();
  if (!doc) {
    return NS_OK;
  }
  if (doc->WillIgnoreCharsetOverride()) {
    return NS_OK;
  }

  *aMayEnableCharacterEncodingMenu = true;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllDocShellsInSubtree(int32_t aItemType,
                                     DocShellEnumeratorDirection aDirection,
                                     nsTArray<RefPtr<nsIDocShell>>& aResult) {
  aResult.Clear();

  nsDocShellEnumerator docShellEnum(
      (aDirection == ENUMERATE_FORWARDS)
          ? nsDocShellEnumerator::EnumerationDirection::Forwards
          : nsDocShellEnumerator::EnumerationDirection::Backwards,
      aItemType, *this);

  nsresult rv = docShellEnum.BuildDocShellArray(aResult);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAppType(AppType* aAppType) {
  *aAppType = mAppType;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAppType(AppType aAppType) {
  mAppType = aAppType;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetAllowAuth(bool* aAllowAuth) {
  *aAllowAuth = mAllowAuth;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetAllowAuth(bool aAllowAuth) {
  mAllowAuth = aAllowAuth;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetZoom(float* aZoom) {
  NS_ENSURE_ARG_POINTER(aZoom);
  *aZoom = 1.0f;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetZoom(float aZoom) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
nsDocShell::GetBusyFlags(BusyFlags* aBusyFlags) {
  NS_ENSURE_ARG_POINTER(aBusyFlags);

  *aBusyFlags = mBusyFlags;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetLoadURIDelegate(nsILoadURIDelegate** aLoadURIDelegate) {
  nsCOMPtr<nsILoadURIDelegate> delegate = GetLoadURIDelegate();
  delegate.forget(aLoadURIDelegate);
  return NS_OK;
}

already_AddRefed<nsILoadURIDelegate> nsDocShell::GetLoadURIDelegate() {
  if (nsCOMPtr<nsILoadURIDelegate> result =
          do_QueryActor("LoadURIDelegate", GetDocument())) {
    return result.forget();
  }

  return nullptr;
}

NS_IMETHODIMP
nsDocShell::GetUseErrorPages(bool* aUseErrorPages) {
  *aUseErrorPages = mBrowsingContext->GetUseErrorPages();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetUseErrorPages(bool aUseErrorPages) {
  return mBrowsingContext->SetUseErrorPages(aUseErrorPages);
}

NS_IMETHODIMP
nsDocShell::GetPreviousEntryIndex(int32_t* aPreviousEntryIndex) {
  *aPreviousEntryIndex = mPreviousEntryIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetLoadedEntryIndex(int32_t* aLoadedEntryIndex) {
  *aLoadedEntryIndex = mLoadedEntryIndex;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::HistoryPurged(int32_t aNumEntries) {
  // These indices are used for fastback cache eviction, to determine
  // which session history entries are candidates for content viewer
  // eviction.  We need to adjust by the number of entries that we
  // just purged from history, so that we look at the right session history
  // entries during eviction.
  mPreviousEntryIndex = std::max(-1, mPreviousEntryIndex - aNumEntries);
  mLoadedEntryIndex = std::max(0, mLoadedEntryIndex - aNumEntries);

  for (auto* child : mChildList.ForwardRange()) {
    nsCOMPtr<nsIDocShell> shell = do_QueryObject(child);
    if (shell) {
      shell->HistoryPurged(aNumEntries);
    }
  }

  return NS_OK;
}

nsresult nsDocShell::HistoryEntryRemoved(int32_t aIndex) {
  // These indices are used for fastback cache eviction, to determine
  // which session history entries are candidates for content viewer
  // eviction.  We need to adjust by the number of entries that we
  // just purged from history, so that we look at the right session history
  // entries during eviction.
  if (aIndex == mPreviousEntryIndex) {
    mPreviousEntryIndex = -1;
  } else if (aIndex < mPreviousEntryIndex) {
    --mPreviousEntryIndex;
  }
  if (mLoadedEntryIndex == aIndex) {
    mLoadedEntryIndex = 0;
  } else if (aIndex < mLoadedEntryIndex) {
    --mLoadedEntryIndex;
  }

  for (auto* child : mChildList.ForwardRange()) {
    nsCOMPtr<nsIDocShell> shell = do_QueryObject(child);
    if (shell) {
      static_cast<nsDocShell*>(shell.get())->HistoryEntryRemoved(aIndex);
    }
  }

  return NS_OK;
}

nsresult nsDocShell::Now(DOMHighResTimeStamp* aWhen) {
  *aWhen = (TimeStamp::Now() - TimeStamp::ProcessCreation()).ToMilliseconds();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetWindowDraggingAllowed(bool aValue) {
  RefPtr<nsDocShell> parent;
  if (!aValue && mItemType == typeChrome &&
      !(parent = GetInProcessParentDocshell())) {
    // Window dragging is always allowed for top level
    // chrome docshells.
    return NS_ERROR_FAILURE;
  }
  mWindowDraggingAllowed = aValue;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetWindowDraggingAllowed(bool* aValue) {
  // window dragging regions in CSS (-moz-window-drag:drag)
  // can be slow. Default behavior is to only allow it for
  // chrome top level windows.
  RefPtr<nsDocShell> parent;
  if (mItemType == typeChrome && !(parent = GetInProcessParentDocshell())) {
    // Top level chrome window
    *aValue = true;
  } else {
    *aValue = mWindowDraggingAllowed;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetCurrentDocumentChannel(nsIChannel** aResult) {
  NS_IF_ADDREF(*aResult = GetCurrentDocChannel());
  return NS_OK;
}

nsIChannel* nsDocShell::GetCurrentDocChannel() {
  if (mDocumentViewer) {
    Document* doc = mDocumentViewer->GetDocument();
    if (doc) {
      return doc->GetChannel();
    }
  }
  return nullptr;
}

NS_IMETHODIMP
nsDocShell::AddWeakScrollObserver(nsIScrollObserver* aObserver) {
  nsWeakPtr weakObs = do_GetWeakReference(aObserver);
  if (!weakObs) {
    return NS_ERROR_FAILURE;
  }
  mScrollObservers.AppendElement(weakObs);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::RemoveWeakScrollObserver(nsIScrollObserver* aObserver) {
  nsWeakPtr obs = do_GetWeakReference(aObserver);
  return mScrollObservers.RemoveElement(obs) ? NS_OK : NS_ERROR_FAILURE;
}

void nsDocShell::NotifyAsyncPanZoomStarted() {
  nsTObserverArray<nsWeakPtr>::ForwardIterator iter(mScrollObservers);
  while (iter.HasMore()) {
    nsWeakPtr ref = iter.GetNext();
    nsCOMPtr<nsIScrollObserver> obs = do_QueryReferent(ref);
    if (obs) {
      obs->AsyncPanZoomStarted();
    } else {
      iter.Remove();
    }
  }
}

void nsDocShell::NotifyAsyncPanZoomStopped() {
  nsTObserverArray<nsWeakPtr>::ForwardIterator iter(mScrollObservers);
  while (iter.HasMore()) {
    nsWeakPtr ref = iter.GetNext();
    nsCOMPtr<nsIScrollObserver> obs = do_QueryReferent(ref);
    if (obs) {
      obs->AsyncPanZoomStopped();
    } else {
      iter.Remove();
    }
  }
}

NS_IMETHODIMP
nsDocShell::NotifyScrollObservers() {
  nsTObserverArray<nsWeakPtr>::ForwardIterator iter(mScrollObservers);
  while (iter.HasMore()) {
    nsWeakPtr ref = iter.GetNext();
    nsCOMPtr<nsIScrollObserver> obs = do_QueryReferent(ref);
    if (obs) {
      obs->ScrollPositionChanged();
    } else {
      iter.Remove();
    }
  }
  return NS_OK;
}

//*****************************************************************************
// nsDocShell::nsIDocShellTreeItem
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::GetName(nsAString& aName) {
  aName = mBrowsingContext->Name();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetName(const nsAString& aName) {
  return mBrowsingContext->SetName(aName);
}

NS_IMETHODIMP
nsDocShell::NameEquals(const nsAString& aName, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = mBrowsingContext->NameEquals(aName);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetCustomUserAgent(nsAString& aCustomUserAgent) {
  mBrowsingContext->GetCustomUserAgent(aCustomUserAgent);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetCustomUserAgent(const nsAString& aCustomUserAgent) {
  if (mWillChangeProcess) {
    NS_WARNING("SetCustomUserAgent: Process is changing. Ignoring set");
    return NS_ERROR_FAILURE;
  }

  return mBrowsingContext->SetCustomUserAgent(aCustomUserAgent);
}

NS_IMETHODIMP
nsDocShell::ClearCachedPlatform() {
  nsCOMPtr<nsPIDOMWindowInner> win =
      mScriptGlobal ? mScriptGlobal->GetCurrentInnerWindow() : nullptr;
  if (win) {
    Navigator* navigator = win->Navigator();
    if (navigator) {
      navigator->ClearPlatformCache();
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::ClearCachedUserAgent() {
  nsCOMPtr<nsPIDOMWindowInner> win =
      mScriptGlobal ? mScriptGlobal->GetCurrentInnerWindow() : nullptr;
  if (win) {
    Navigator* navigator = win->Navigator();
    if (navigator) {
      navigator->ClearUserAgentCache();
    }
  }

  return NS_OK;
}

/* virtual */
int32_t nsDocShell::ItemType() { return mItemType; }

NS_IMETHODIMP
nsDocShell::GetItemType(int32_t* aItemType) {
  NS_ENSURE_ARG_POINTER(aItemType);

  MOZ_DIAGNOSTIC_ASSERT(
      (mBrowsingContext->IsContent() ? typeContent : typeChrome) == mItemType);
  *aItemType = mItemType;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetInProcessParent(nsIDocShellTreeItem** aParent) {
  if (!mParent) {
    *aParent = nullptr;
  } else {
    CallQueryInterface(mParent, aParent);
  }
  // Note that in the case when the parent is not an nsIDocShellTreeItem we
  // don't want to throw; we just want to return null.
  return NS_OK;
}

// With Fission, related nsDocShell objects may exist in a different process. In
// that case, this method will return `nullptr`, despite a parent nsDocShell
// object existing.
//
// Prefer using `BrowsingContext::Parent()`, which will succeed even if the
// parent entry is not in the current process, and handle the case where the
// parent nsDocShell is inaccessible.
already_AddRefed<nsDocShell> nsDocShell::GetInProcessParentDocshell() {
  nsCOMPtr<nsIDocShell> docshell = do_QueryInterface(GetAsSupports(mParent));
  return docshell.forget().downcast<nsDocShell>();
}

void nsDocShell::MaybeCreateInitialClientSource(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(!mIsBeingDestroyed);

  // If there is an existing document then there is no need to create
  // a client for a future initial about:blank document.
  if (mScriptGlobal && mScriptGlobal->GetCurrentInnerWindow() &&
      mScriptGlobal->GetCurrentInnerWindow()->GetExtantDoc()) {
    MOZ_DIAGNOSTIC_ASSERT(
        mScriptGlobal->GetCurrentInnerWindow()->GetClientInfo().isSome());
    MOZ_DIAGNOSTIC_ASSERT(!mInitialClientSource);
    return;
  }

  // Don't recreate the initial client source.  We call this multiple times
  // when DoChannelLoad() is called before CreateAboutBlankDocumentViewer.
  if (mInitialClientSource) {
    return;
  }

  // Don't pre-allocate the client when we are sandboxed.  The inherited
  // principal does not take sandboxing into account.
  // TODO: Refactor sandboxing principal code out so we can use it here.
  if (!aPrincipal && mBrowsingContext->GetSandboxFlags()) {
    return;
  }

  // We cannot get inherited foreign partitioned principal here. Instead, we
  // directly check which principal we want to inherit for the service worker.
  nsIPrincipal* principal =
      aPrincipal
          ? aPrincipal
          : GetInheritedPrincipal(
                false, StoragePrincipalHelper::
                           ShouldUsePartitionPrincipalForServiceWorker(this));

  // Sometimes there is no principal available when we are called from
  // CreateAboutBlankDocumentViewer.  For example, sometimes the principal
  // is only extracted from the load context after the document is created
  // in Document::ResetToURI().  Ideally we would do something similar
  // here, but for now lets just avoid the issue by not preallocating the
  // client.
  if (!principal) {
    return;
  }

  nsCOMPtr<nsPIDOMWindowOuter> win = GetWindow();
  if (!win) {
    return;
  }

  mInitialClientSource = ClientManager::CreateSource(
      ClientType::Window, GetMainThreadSerialEventTarget(), principal);
  MOZ_DIAGNOSTIC_ASSERT(mInitialClientSource);

  // Mark the initial client as execution ready, but owned by the docshell.
  // If the client is actually used this will cause ClientSource to force
  // the creation of the initial about:blank by calling
  // nsDocShell::GetDocument().
  mInitialClientSource->DocShellExecutionReady(this);

  // Next, check to see if the parent is controlled.
  MaybeInheritController(mInitialClientSource.get(), principal);
}

void VerifyClientPrincipalInfosMatch(
    const mozilla::ipc::PrincipalInfo& aLeft,
    const mozilla::ipc::PrincipalInfo& aRight) {
  // Inheriting a controller when the principals don't match would cause a
  // crash. Let's do the checks earlier to crash here already instead of
  // ClientSource::SetController. And assert each condition separately. See bug
  // 1880012.
  MOZ_RELEASE_ASSERT(aLeft.type() == aRight.type());

  switch (aLeft.type()) {
    case mozilla::ipc::PrincipalInfo::TContentPrincipalInfo: {
      const mozilla::ipc::ContentPrincipalInfo& leftContent =
          aLeft.get_ContentPrincipalInfo();
      const mozilla::ipc::ContentPrincipalInfo& rightContent =
          aRight.get_ContentPrincipalInfo();
      {
        // The most likely mismatch is the foreign bit in the partition key.
        // See bug 2006265 and 2013379.
        nsAutoString scheme;
        nsAutoString baseDomain;
        int32_t port;
        bool leftForeignBit;
        bool rightForeignBit;
        OriginAttributes::ParsePartitionKey(leftContent.attrs().mPartitionKey,
                                            scheme, baseDomain, port,
                                            leftForeignBit);
        OriginAttributes::ParsePartitionKey(rightContent.attrs().mPartitionKey,
                                            scheme, baseDomain, port,
                                            rightForeignBit);
        MOZ_RELEASE_ASSERT(leftForeignBit == rightForeignBit);
      }
      MOZ_RELEASE_ASSERT(leftContent.attrs() == rightContent.attrs());
      MOZ_RELEASE_ASSERT(leftContent.originNoSuffix() ==
                         rightContent.originNoSuffix());
      return;
    }
    case mozilla::ipc::PrincipalInfo::TNullPrincipalInfo: {
      // null principal never matches
      MOZ_RELEASE_ASSERT(false, "Clients have null principals");
      return;
    }
    default: {
      break;
    }
  }
}

void nsDocShell::MaybeInheritController(
    mozilla::dom::ClientSource* aClientSource, nsIPrincipal* aPrincipal) {
  nsCOMPtr<nsIDocShell> parent = GetInProcessParentDocshell();
  nsPIDOMWindowOuter* parentOuter = parent ? parent->GetWindow() : nullptr;
  nsPIDOMWindowInner* parentInner =
      parentOuter ? parentOuter->GetCurrentInnerWindow() : nullptr;
  if (!parentInner) {
    return;
  }

  nsCOMPtr<nsIURI> uri;
  MOZ_ALWAYS_SUCCEEDS(NS_NewURI(getter_AddRefs(uri), "about:blank"_ns));

  // We're done if there is no parent controller or if this docshell
  // is not permitted to control for some reason.
  Maybe<ServiceWorkerDescriptor> controller(parentInner->GetController());
  if (controller.isNothing() ||
      !ServiceWorkerAllowedToControlWindow(aPrincipal, uri)) {
    return;
  }

  VerifyClientPrincipalInfosMatch(aClientSource->Info().PrincipalInfo(),
                                  controller->PrincipalInfo());
  aClientSource->InheritController(controller.ref());
}

Maybe<ClientInfo> nsDocShell::GetInitialClientInfo() const {
  if (mInitialClientSource) {
    Maybe<ClientInfo> result;
    result.emplace(mInitialClientSource->Info());
    return result;
  }

  nsPIDOMWindowInner* innerWindow =
      mScriptGlobal ? mScriptGlobal->GetCurrentInnerWindow() : nullptr;
  Document* doc = innerWindow ? innerWindow->GetExtantDoc() : nullptr;

  if (!doc || !doc->IsUncommittedInitialDocument()) {
    // We won't reuse the inner window so let the channel determine one
    return Maybe<ClientInfo>();
  }

  return innerWindow->GetClientInfo();
}

nsresult nsDocShell::SetDocLoaderParent(nsDocLoader* aParent) {
  bool wasFrame = IsSubframe();

  nsresult rv = nsDocLoader::SetDocLoaderParent(aParent);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsPriority> priorityGroup = do_QueryInterface(mLoadGroup);
  if (wasFrame != IsSubframe() && priorityGroup) {
    priorityGroup->AdjustPriority(wasFrame ? -1 : 1);
  }

  // Curse ambiguous nsISupports inheritance!
  nsISupports* parent = GetAsSupports(aParent);

  // If parent is another docshell, we inherit all their flags for
  // allowing plugins, scripting etc.
  bool value;
  nsCOMPtr<nsIDocShell> parentAsDocShell(do_QueryInterface(parent));

  if (parentAsDocShell) {
    if (mAllowMetaRedirects &&
        NS_SUCCEEDED(parentAsDocShell->GetAllowMetaRedirects(&value))) {
      SetAllowMetaRedirects(value);
    }
    if (mAllowSubframes &&
        NS_SUCCEEDED(parentAsDocShell->GetAllowSubframes(&value))) {
      SetAllowSubframes(value);
    }
    if (mAllowImages &&
        NS_SUCCEEDED(parentAsDocShell->GetAllowImages(&value))) {
      SetAllowImages(value);
    }
    SetAllowMedia(parentAsDocShell->GetAllowMedia() && mAllowMedia);
    if (mAllowWindowControl &&
        NS_SUCCEEDED(parentAsDocShell->GetAllowWindowControl(&value))) {
      SetAllowWindowControl(value);
    }
    if (NS_FAILED(parentAsDocShell->GetAllowDNSPrefetch(&value))) {
      value = false;
    }
    SetAllowDNSPrefetch(mAllowDNSPrefetch && value);
  }

  nsCOMPtr<nsIURIContentListener> parentURIListener(do_GetInterface(parent));
  if (parentURIListener) {
    mContentListener->SetParentContentListener(parentURIListener);
  }

  return NS_OK;
}

void nsDocShell::MaybeRestoreWindowName() {
  if (!StaticPrefs::privacy_window_name_update_enabled()) {
    return;
  }

  // We only restore window.name for the top-level content.
  if (!mBrowsingContext->IsTopContent()) {
    return;
  }

  nsAutoString name;

  // Following implements https://html.spec.whatwg.org/#history-traversal:
  // Step 4.4. Check if the loading entry has a name.

  if (mLoadingEntry) {
    name = mLoadingEntry->mInfo.GetName();
  }

  if (name.IsEmpty()) {
    return;
  }

  // Step 4.4.1. Set the name to the browsing context.
  (void)mBrowsingContext->SetName(name);

  // Step 4.4.2. Clear the name of all entries that are contiguous and
  // same-origin with the loading entry.
  if (mLoadingEntry) {
    // Clear the name of the session entry in the child side. For parent side,
    // the clearing will be done when we commit the history to the parent.
    mLoadingEntry->mInfo.SetName(EmptyString());
  }
}

void nsDocShell::StoreWindowNameToSHEntries() {
  MOZ_ASSERT(mBrowsingContext->IsTopContent());

  nsAutoString name;
  mBrowsingContext->GetName(name);

  if (XRE_IsParentProcess()) {
    SessionHistoryEntry* entry =
        mBrowsingContext->Canonical()->GetActiveSessionHistoryEntry();
    if (entry) {
      nsSHistory::WalkContiguousEntries(
          entry, [&](SessionHistoryEntry* aEntry) { aEntry->SetName(name); });
    }
  } else {
    // Ask parent process to store the name in entries.
    (void)ContentChild::GetSingleton()
        ->SendSessionHistoryEntryStoreWindowNameInContiguousEntries(
            mBrowsingContext, name);
  }
}

NS_IMETHODIMP
nsDocShell::GetInProcessSameTypeParent(nsIDocShellTreeItem** aParent) {
  if (BrowsingContext* parentBC = mBrowsingContext->GetParent()) {
    *aParent = do_AddRef(parentBC->GetDocShell()).take();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetInProcessRootTreeItem(nsIDocShellTreeItem** aRootTreeItem) {
  NS_ENSURE_ARG_POINTER(aRootTreeItem);

  RefPtr<nsDocShell> root = this;
  RefPtr<nsDocShell> parent = root->GetInProcessParentDocshell();
  while (parent) {
    root = parent;
    parent = root->GetInProcessParentDocshell();
  }

  root.forget(aRootTreeItem);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetInProcessSameTypeRootTreeItem(
    nsIDocShellTreeItem** aRootTreeItem) {
  NS_ENSURE_ARG_POINTER(aRootTreeItem);
  *aRootTreeItem = static_cast<nsIDocShellTreeItem*>(this);

  nsCOMPtr<nsIDocShellTreeItem> parent;
  NS_ENSURE_SUCCESS(GetInProcessSameTypeParent(getter_AddRefs(parent)),
                    NS_ERROR_FAILURE);
  while (parent) {
    *aRootTreeItem = parent;
    NS_ENSURE_SUCCESS(
        (*aRootTreeItem)->GetInProcessSameTypeParent(getter_AddRefs(parent)),
        NS_ERROR_FAILURE);
  }
  NS_ADDREF(*aRootTreeItem);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetTreeOwner(nsIDocShellTreeOwner** aTreeOwner) {
  NS_ENSURE_ARG_POINTER(aTreeOwner);

  *aTreeOwner = mTreeOwner;
  NS_IF_ADDREF(*aTreeOwner);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetTreeOwner(nsIDocShellTreeOwner* aTreeOwner) {
  if (mIsBeingDestroyed && aTreeOwner) {
    return NS_ERROR_FAILURE;
  }

  // Don't automatically set the progress based on the tree owner for frames
  if (!IsSubframe()) {
    nsCOMPtr<nsIWebProgress> webProgress =
        do_QueryInterface(GetAsSupports(this));

    if (webProgress) {
      nsCOMPtr<nsIWebProgressListener> oldListener =
          do_QueryInterface(mTreeOwner);
      nsCOMPtr<nsIWebProgressListener> newListener =
          do_QueryInterface(aTreeOwner);

      if (oldListener) {
        webProgress->RemoveProgressListener(oldListener);
      }

      if (newListener) {
        webProgress->AddProgressListener(newListener,
                                         nsIWebProgress::NOTIFY_ALL);
      }
    }
  }

  mTreeOwner = aTreeOwner;  // Weak reference per API

  for (auto* childDocLoader : mChildList.ForwardRange()) {
    nsCOMPtr<nsIDocShellTreeItem> child = do_QueryObject(childDocLoader);
    NS_ENSURE_TRUE(child, NS_ERROR_FAILURE);

    if (child->ItemType() == mItemType) {
      child->SetTreeOwner(aTreeOwner);
    }
  }

  // If we're in the content process and have had a TreeOwner set on us, extract
  // our BrowserChild actor. If we've already had our BrowserChild set, assert
  // that it hasn't changed.
  if (mTreeOwner && XRE_IsContentProcess()) {
    nsCOMPtr<nsIBrowserChild> newBrowserChild = do_GetInterface(mTreeOwner);
    MOZ_ASSERT(newBrowserChild,
               "No BrowserChild actor for tree owner in Content!");

    if (mBrowserChild) {
      nsCOMPtr<nsIBrowserChild> oldBrowserChild =
          do_QueryReferent(mBrowserChild);
      MOZ_RELEASE_ASSERT(
          oldBrowserChild == newBrowserChild,
          "Cannot change BrowserChild during nsDocShell lifetime!");
    } else {
      mBrowserChild = do_GetWeakReference(newBrowserChild);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetHistoryID(nsID& aID) {
  aID = mBrowsingContext->GetHistoryID();
  return NS_OK;
}

const nsID& nsDocShell::HistoryID() { return mBrowsingContext->GetHistoryID(); }

NS_IMETHODIMP
nsDocShell::GetIsInUnload(bool* aIsInUnload) {
  *aIsInUnload = mFiredUnloadEvent;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetInProcessChildCount(int32_t* aChildCount) {
  NS_ENSURE_ARG_POINTER(aChildCount);
  *aChildCount = mChildList.Length();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::AddChild(nsIDocShellTreeItem* aChild) {
  NS_ENSURE_ARG_POINTER(aChild);

  RefPtr<nsDocLoader> childAsDocLoader = GetAsDocLoader(aChild);
  NS_ENSURE_TRUE(childAsDocLoader, NS_ERROR_UNEXPECTED);

  // Make sure we're not creating a loop in the docshell tree
  nsDocLoader* ancestor = this;
  do {
    if (childAsDocLoader == ancestor) {
      return NS_ERROR_ILLEGAL_VALUE;
    }
    ancestor = ancestor->GetParent();
  } while (ancestor);

  // Make sure to remove the child from its current parent.
  nsDocLoader* childsParent = childAsDocLoader->GetParent();
  if (childsParent) {
    nsresult rv = childsParent->RemoveChildLoader(childAsDocLoader);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Make sure to clear the treeowner in case this child is a different type
  // from us.
  aChild->SetTreeOwner(nullptr);

  nsresult res = AddChildLoader(childAsDocLoader);
  NS_ENSURE_SUCCESS(res, res);
  NS_ASSERTION(!mChildList.IsEmpty(),
               "child list must not be empty after a successful add");

  /* Set the child's global history if the parent has one */
  if (mBrowsingContext->GetUseGlobalHistory()) {
    // childDocShell->SetUseGlobalHistory(true);
    // this should be set through BC inherit
    MOZ_ASSERT(aChild->GetBrowsingContext()->GetUseGlobalHistory());
  }

  if (aChild->ItemType() != mItemType) {
    return NS_OK;
  }

  aChild->SetTreeOwner(mTreeOwner);

  nsCOMPtr<nsIDocShell> childAsDocShell(do_QueryInterface(aChild));
  if (!childAsDocShell) {
    return NS_OK;
  }

  // charset, style-disabling, and zoom will be inherited in SetupNewViewer()

  // Now take this document's charset and set the child's parentCharset field
  // to it. We'll later use that field, in the loading process, for the
  // charset choosing algorithm.
  // If we fail, at any point, we just return NS_OK.
  // This code has some performance impact. But this will be reduced when
  // the current charset will finally be stored as an Atom, avoiding the
  // alias resolution extra look-up.

  // we are NOT going to propagate the charset is this Chrome's docshell
  if (mItemType == nsIDocShellTreeItem::typeChrome) {
    return NS_OK;
  }

  // get the parent's current charset
  if (!mDocumentViewer) {
    return NS_OK;
  }
  Document* doc = mDocumentViewer->GetDocument();
  if (!doc) {
    return NS_OK;
  }

  const Encoding* parentCS = doc->GetDocumentCharacterSet();
  int32_t charsetSource = doc->GetDocumentCharacterSetSource();
  // set the child's parentCharset
  childAsDocShell->SetParentCharset(parentCS, charsetSource,
                                    doc->NodePrincipal());

  // printf("### 1 >>> Adding child. Parent CS = %s. ItemType = %d.\n",
  //        NS_LossyConvertUTF16toASCII(parentCS).get(), mItemType);

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::RemoveChild(nsIDocShellTreeItem* aChild) {
  NS_ENSURE_ARG_POINTER(aChild);

  RefPtr<nsDocLoader> childAsDocLoader = GetAsDocLoader(aChild);
  NS_ENSURE_TRUE(childAsDocLoader, NS_ERROR_UNEXPECTED);

  nsresult rv = RemoveChildLoader(childAsDocLoader);
  NS_ENSURE_SUCCESS(rv, rv);

  aChild->SetTreeOwner(nullptr);

  return nsDocLoader::AddDocLoaderAsChildOfRoot(childAsDocLoader);
}

NS_IMETHODIMP
nsDocShell::GetInProcessChildAt(int32_t aIndex, nsIDocShellTreeItem** aChild) {
  NS_ENSURE_ARG_POINTER(aChild);

  RefPtr<nsDocShell> child = GetInProcessChildAt(aIndex);
  NS_ENSURE_TRUE(child, NS_ERROR_UNEXPECTED);

  child.forget(aChild);

  return NS_OK;
}

nsDocShell* nsDocShell::GetInProcessChildAt(int32_t aIndex) {
#ifdef DEBUG
  if (aIndex < 0) {
    NS_WARNING("Negative index passed to GetChildAt");
  } else if (static_cast<uint32_t>(aIndex) >= mChildList.Length()) {
    NS_WARNING("Too large an index passed to GetChildAt");
  }
#endif

  nsIDocumentLoader* child = ChildAt(aIndex);

  // child may be nullptr here.
  return static_cast<nsDocShell*>(child);
}

NS_IMETHODIMP nsDocShell::SynchronizeLayoutHistoryState() {
  if (mActiveEntry && mActiveEntry->GetLayoutHistoryState() &&
      mBrowsingContext) {
    if (XRE_IsContentProcess()) {
      dom::ContentChild* contentChild = dom::ContentChild::GetSingleton();
      if (contentChild) {
        contentChild->SendSynchronizeLayoutHistoryState(
            mBrowsingContext, mActiveEntry->GetLayoutHistoryState());
      }
    } else {
      SessionHistoryEntry* entry =
          mBrowsingContext->Canonical()->GetActiveSessionHistoryEntry();
      if (entry) {
        entry->SetLayoutHistoryState(mActiveEntry->GetLayoutHistoryState());
      }
    }
    if (mLoadingEntry &&
        mLoadingEntry->mInfo.SharedId() == mActiveEntry->SharedId()) {
      mLoadingEntry->mInfo.SetLayoutHistoryState(
          mActiveEntry->GetLayoutHistoryState());
    }
  }

  return NS_OK;
}

void nsDocShell::SetLoadGroupDefaultLoadFlags(nsLoadFlags aLoadFlags) {
  if (mLoadGroup) {
    mLoadGroup->SetDefaultLoadFlags(aLoadFlags);
  } else {
    NS_WARNING(
        "nsDocShell::SetLoadGroupDefaultLoadFlags has no loadGroup to "
        "propagate the mode to");
  }
}

nsIScriptGlobalObject* nsDocShell::GetScriptGlobalObject() {
  NS_ENSURE_SUCCESS(EnsureScriptEnvironment(), nullptr);
  return mScriptGlobal;
}

Document* nsDocShell::GetDocument() {
  NS_ENSURE_TRUE(VerifyDocumentViewer(), nullptr);
  return mDocumentViewer->GetDocument();
}

Document* nsDocShell::GetExtantDocument() {
  return mDocumentViewer ? mDocumentViewer->GetDocument() : nullptr;
}

nsPIDOMWindowOuter* nsDocShell::GetWindow() {
  if (NS_FAILED(EnsureScriptEnvironment())) {
    return nullptr;
  }
  return mScriptGlobal;
}

NS_IMETHODIMP
nsDocShell::GetDomWindow(mozIDOMWindowProxy** aWindow) {
  NS_ENSURE_ARG_POINTER(aWindow);

  nsresult rv = EnsureScriptEnvironment();
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsGlobalWindowOuter> window = mScriptGlobal;
  window.forget(aWindow);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetMessageManager(ContentFrameMessageManager** aMessageManager) {
  RefPtr<ContentFrameMessageManager> mm;
  if (RefPtr<BrowserChild> browserChild = BrowserChild::GetFrom(this)) {
    mm = browserChild->GetMessageManager();
  } else if (nsPIDOMWindowOuter* win = GetWindow()) {
    mm = win->GetMessageManager();
  }
  mm.forget(aMessageManager);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetIsNavigating(bool* aOut) {
  *aOut = mIsNavigating;
  return NS_OK;
}

//-------------------------------------
//-- Helper Method for Print discovery
//-------------------------------------
bool nsDocShell::NavigationBlockedByPrinting(bool aDisplayErrorDialog) {
  if (!mBrowsingContext->Top()->GetIsPrinting()) {
    return false;
  }
  if (aDisplayErrorDialog) {
    DisplayLoadError(NS_ERROR_DOCUMENT_IS_PRINTMODE, nullptr, nullptr, nullptr);
  }
  return true;
}

bool nsDocShell::IsNavigationAllowed(bool aDisplayPrintErrorDialog,
                                     bool aCheckIfUnloadFired) {
  bool isAllowed = !NavigationBlockedByPrinting(aDisplayPrintErrorDialog) &&
                   (!aCheckIfUnloadFired || !mFiredUnloadEvent);
  if (!isAllowed) {
    return false;
  }
  if (!mDocumentViewer) {
    return true;
  }
  bool firingBeforeUnload;
  mDocumentViewer->GetBeforeUnloadFiring(&firingBeforeUnload);
  return !firingBeforeUnload;
}

//*****************************************************************************
// nsDocShell::nsIWebNavigation
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::GetCanGoBack(bool* aCanGoBack) {
  *aCanGoBack = false;
  if (!IsNavigationAllowed(false)) {
    return NS_OK;  // JS may not handle returning of an error code
  }
  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  if (rootSH) {
    *aCanGoBack = rootSH->CanGo(
        -1, StaticPrefs::browser_navigation_requireUserInteraction());
    MOZ_LOG(gSHLog, LogLevel::Verbose,
            ("nsDocShell %p CanGoBack()->%d", this, *aCanGoBack));

    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDocShell::GetCanGoBackIgnoringUserInteraction(bool* aCanGoBack) {
  *aCanGoBack = false;
  if (!IsNavigationAllowed(false)) {
    return NS_OK;  // JS may not handle returning of an error code
  }
  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  if (rootSH) {
    *aCanGoBack = rootSH->CanGo(-1, false);
    MOZ_LOG(gSHLog, LogLevel::Verbose,
            ("nsDocShell %p CanGoBackIgnoringUserInteraction()->%d", this,
             *aCanGoBack));

    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDocShell::GetCanGoForward(bool* aCanGoForward) {
  *aCanGoForward = false;
  if (!IsNavigationAllowed(false)) {
    return NS_OK;  // JS may not handle returning of an error code
  }
  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  if (rootSH) {
    *aCanGoForward = rootSH->CanGo(
        1, StaticPrefs::browser_navigation_requireUserInteraction());
    MOZ_LOG(gSHLog, LogLevel::Verbose,
            ("nsDocShell %p CanGoForward()->%d", this, *aCanGoForward));
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDocShell::GoBack(bool aRequireUserInteraction, bool aUserActivation) {
  if (!IsNavigationAllowed()) {
    return NS_OK;  // JS may not handle returning of an error code
  }

  auto cleanupIsNavigating = MakeScopeExit([&]() { mIsNavigating = false; });
  mIsNavigating = true;

  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  NS_ENSURE_TRUE(rootSH, NS_ERROR_FAILURE);
  ErrorResult rv;
  rootSH->Go(-1, aRequireUserInteraction, aUserActivation, rv);
  return rv.StealNSResult();
}

NS_IMETHODIMP
nsDocShell::GoForward(bool aRequireUserInteraction, bool aUserActivation) {
  if (!IsNavigationAllowed()) {
    return NS_OK;  // JS may not handle returning of an error code
  }

  auto cleanupIsNavigating = MakeScopeExit([&]() { mIsNavigating = false; });
  mIsNavigating = true;

  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  NS_ENSURE_TRUE(rootSH, NS_ERROR_FAILURE);
  ErrorResult rv;
  rootSH->Go(1, aRequireUserInteraction, aUserActivation, rv);
  return rv.StealNSResult();
}

// XXX(nika): We may want to stop exposing this API in the child process? Going
// to a specific index from multiple different processes could definitely race.
NS_IMETHODIMP
nsDocShell::GotoIndex(int32_t aIndex, bool aUserActivation) {
  if (!IsNavigationAllowed()) {
    return NS_OK;  // JS may not handle returning of an error code
  }

  auto cleanupIsNavigating = MakeScopeExit([&]() { mIsNavigating = false; });
  mIsNavigating = true;

  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  NS_ENSURE_TRUE(rootSH, NS_ERROR_FAILURE);

  ErrorResult rv;
  rootSH->GotoIndex(aIndex, aIndex - rootSH->Index(), false, aUserActivation,
                    rv);
  return rv.StealNSResult();
}

nsresult nsDocShell::LoadURI(nsIURI* aURI,
                             const LoadURIOptions& aLoadURIOptions) {
  if (!IsNavigationAllowed()) {
    return NS_OK;  // JS may not handle returning of an error code
  }
  RefPtr<nsDocShellLoadState> loadState;
  nsresult rv = nsDocShellLoadState::CreateFromLoadURIOptions(
      mBrowsingContext, aURI, aLoadURIOptions, getter_AddRefs(loadState));
  MOZ_ASSERT(rv != NS_ERROR_MALFORMED_URI);
  if (NS_FAILED(rv) || !loadState) {
    return NS_ERROR_FAILURE;
  }

  // Set the captive portal tab flag on the browsing context if requested
  if (loadState->GetIsCaptivePortalTab()) {
    (void)mBrowsingContext->SetIsCaptivePortalTab(true);
  }

  return LoadURI(loadState, true);
}

NS_IMETHODIMP
nsDocShell::LoadURIFromScript(nsIURI* aURI,
                              JS::Handle<JS::Value> aLoadURIOptions,
                              JSContext* aCx) {
  // generate dictionary for aLoadURIOptions and forward call
  LoadURIOptions loadURIOptions;
  if (!loadURIOptions.Init(aCx, aLoadURIOptions)) {
    return NS_ERROR_INVALID_ARG;
  }
  return LoadURI(aURI, loadURIOptions);
}

nsresult nsDocShell::FixupAndLoadURIString(
    const nsAString& aURIString, const LoadURIOptions& aLoadURIOptions) {
  if (!IsNavigationAllowed()) {
    return NS_OK;  // JS may not handle returning of an error code
  }

  RefPtr<nsDocShellLoadState> loadState;
  nsresult rv = nsDocShellLoadState::CreateFromLoadURIOptions(
      mBrowsingContext, aURIString, aLoadURIOptions, getter_AddRefs(loadState));

  uint32_t loadFlags = aLoadURIOptions.mLoadFlags;
  if (NS_ERROR_MALFORMED_URI == rv) {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("Creating an active entry on nsDocShell %p to %s (because "
             "we're showing an error page)",
             this, NS_ConvertUTF16toUTF8(aURIString).get()));

    // We need to store a session history entry. We don't have a valid URI, so
    // we use about:blank instead.
    nsCOMPtr<nsIURI> uri;
    MOZ_ALWAYS_SUCCEEDS(NS_NewURI(getter_AddRefs(uri), "about:blank"_ns));
    nsCOMPtr<nsIPrincipal> triggeringPrincipal;
    if (aLoadURIOptions.mTriggeringPrincipal) {
      triggeringPrincipal = aLoadURIOptions.mTriggeringPrincipal;
    } else {
      triggeringPrincipal = nsContentUtils::GetSystemPrincipal();
    }
    UniquePtr<SessionHistoryInfo> previousActiveEntry(mActiveEntry.release());
    mActiveEntry = MakeUnique<SessionHistoryInfo>(
        uri, triggeringPrincipal, nullptr, nullptr, nullptr,
        nsLiteralCString("text/html"));
    mBrowsingContext->SetActiveSessionHistoryEntry(
        Nothing(), mActiveEntry.get(), previousActiveEntry.get(),
        MAKE_LOAD_TYPE(LOAD_NORMAL, loadFlags),
        /* aUpdatedCacheKey = */ 0);
    if (DisplayLoadError(rv, nullptr, PromiseFlatString(aURIString).get(),
                         nullptr) &&
        (loadFlags & LOAD_FLAGS_ERROR_LOAD_CHANGES_RV) != 0) {
      return NS_ERROR_LOAD_SHOWED_ERRORPAGE;
    }
  }

  if (NS_FAILED(rv) || !loadState) {
    return NS_ERROR_FAILURE;
  }

  // Set the captive portal tab flag on the browsing context if requested
  if (loadState->GetIsCaptivePortalTab()) {
    (void)mBrowsingContext->SetIsCaptivePortalTab(true);
  }

  return LoadURI(loadState, true);
}

NS_IMETHODIMP
nsDocShell::FixupAndLoadURIStringFromScript(
    const nsAString& aURIString, JS::Handle<JS::Value> aLoadURIOptions,
    JSContext* aCx) {
  // generate dictionary for aLoadURIOptions and forward call
  LoadURIOptions loadURIOptions;
  if (!loadURIOptions.Init(aCx, aLoadURIOptions)) {
    return NS_ERROR_INVALID_ARG;
  }
  return FixupAndLoadURIString(aURIString, loadURIOptions);
}

void nsDocShell::UnblockEmbedderLoadEventForFailure(bool aFireFrameErrorEvent) {
  // If we're not in a content frame, or are at a BrowsingContext tree boundary,
  // such as the content-chrome boundary, don't fire the error event.
  if (mBrowsingContext->IsTopContent() || mBrowsingContext->IsChrome()) {
    return;
  }

  // If embedder is same-process, then unblocking the load event is already
  // handled by nsDocLoader. Fire the error event on our embedder element if
  // requested.
  //
  // XXX: Bug 1440212 is looking into potentially changing this behaviour to act
  // more like the remote case when in-process.
  RefPtr<Element> element = mBrowsingContext->GetEmbedderElement();
  if (element) {
    if (aFireFrameErrorEvent) {
      if (RefPtr<nsFrameLoaderOwner> flo = do_QueryObject(element)) {
        if (RefPtr<nsFrameLoader> fl = flo->GetFrameLoader()) {
          fl->FireErrorEvent();
        }
      }
    }
    return;
  }

  // If we have a cross-process parent document, we must notify it that we no
  // longer block its load event.  This is necessary for OOP sub-documents
  // because error documents do not result in a call to
  // SendMaybeFireEmbedderLoadEvents via any of the normal call paths.
  // (Obviously, we must do this before any of the returns below.)
  RefPtr<BrowserChild> browserChild = BrowserChild::GetFrom(this);
  if (browserChild &&
      !mBrowsingContext->GetParentWindowContext()->IsInProcess()) {
    (void)browserChild->SendMaybeFireEmbedderLoadEvents(
        aFireFrameErrorEvent ? EmbedderElementEventType::ErrorEvent
                             : EmbedderElementEventType::NoEvent);
  }
}

NS_IMETHODIMP
nsDocShell::DisplayLoadError(nsresult aError, nsIURI* aURI,
                             const char16_t* aURL, nsIChannel* aFailedChannel,
                             bool* aDisplayedErrorPage) {
  MOZ_LOG(gDocShellLeakLog, LogLevel::Debug,
          ("DOCSHELL %p DisplayLoadError %s\n", this,
           aURI ? aURI->GetSpecOrDefault().get() : ""));

  *aDisplayedErrorPage = false;
  // Get prompt and string bundle services
  nsCOMPtr<nsIPrompt> prompter;
  nsCOMPtr<nsIStringBundle> stringBundle;
  GetPromptAndStringBundle(getter_AddRefs(prompter),
                           getter_AddRefs(stringBundle));

  NS_ENSURE_TRUE(stringBundle, NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(prompter, NS_ERROR_FAILURE);

  const char* error = nullptr;
  // The key used to select the appropriate error message from the properties
  // file.
  const char* errorDescriptionID = nullptr;
  AutoTArray<nsString, 3> formatStrs;
  bool addHostPort = false;
  bool isBadStsCertError = false;
  nsresult rv = NS_OK;
  nsAutoString messageStr;
  nsAutoCString cssClass;
  nsAutoCString errorPage;

  errorPage.AssignLiteral("neterror");

  // Turn the error code into a human readable error message.
  if (NS_ERROR_UNKNOWN_PROTOCOL == aError) {
    NS_ENSURE_ARG_POINTER(aURI);

    // Extract the schemes into a comma delimited list.
    nsAutoCString scheme;
    aURI->GetScheme(scheme);
    CopyASCIItoUTF16(scheme, *formatStrs.AppendElement());
    nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(aURI);
    while (nestedURI) {
      nsCOMPtr<nsIURI> tempURI;
      nsresult rv2;
      rv2 = nestedURI->GetInnerURI(getter_AddRefs(tempURI));
      if (NS_SUCCEEDED(rv2) && tempURI) {
        tempURI->GetScheme(scheme);
        formatStrs[0].AppendLiteral(", ");
        AppendASCIItoUTF16(scheme, formatStrs[0]);
      }
      nestedURI = do_QueryInterface(tempURI);
    }
    error = "unknownProtocolFound";
  } else if (NS_ERROR_NET_EMPTY_RESPONSE == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    error = "httpErrorPage";
  } else if (NS_ERROR_NET_ERROR_RESPONSE == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    error = "serverError";
  } else if (NS_ERROR_FILE_NOT_FOUND == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    error = "fileNotFound";
  } else if (NS_ERROR_FILE_ACCESS_DENIED == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    error = "fileAccessDenied";
  } else if (NS_ERROR_UNKNOWN_HOST == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    // Get the host
    nsAutoCString host;
    nsCOMPtr<nsIURI> innermostURI = NS_GetInnermostURI(aURI);
    innermostURI->GetHost(host);
    CopyUTF8toUTF16(host, *formatStrs.AppendElement());
    errorDescriptionID = "dnsNotFound2";
    error = "dnsNotFound";
  } else if (NS_ERROR_CONNECTION_REFUSED == aError ||
             NS_ERROR_PROXY_BAD_GATEWAY == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    addHostPort = true;
    error = "connectionFailure";
  } else if (NS_ERROR_NET_INTERRUPT == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    addHostPort = true;
    error = "netInterrupt";
  } else if (NS_ERROR_NET_TIMEOUT == aError ||
             NS_ERROR_PROXY_GATEWAY_TIMEOUT == aError ||
             NS_ERROR_NET_TIMEOUT_EXTERNAL == aError) {
    NS_ENSURE_ARG_POINTER(aURI);
    // Get the host
    nsAutoCString host;
    aURI->GetHost(host);
    CopyUTF8toUTF16(host, *formatStrs.AppendElement());
    error = "netTimeout";
  } else if (NS_ERROR_CSP_FRAME_ANCESTOR_VIOLATION == aError ||
             NS_ERROR_CSP_FORM_ACTION_VIOLATION == aError) {
    // CSP error
    cssClass.AssignLiteral("neterror");
    error = "cspBlocked";
  } else if (NS_ERROR_XFO_VIOLATION == aError) {
    // XFO error
    cssClass.AssignLiteral("neterror");
    error = "xfoBlocked";
  } else if (NS_ERROR_GET_MODULE(aError) == NS_ERROR_MODULE_SECURITY) {
    nsCOMPtr<nsINSSErrorsService> nsserr =
        do_GetService(NS_NSS_ERRORS_SERVICE_CONTRACTID);

    uint32_t errorClass;
    if (!nsserr || NS_FAILED(nsserr->GetErrorClass(aError, &errorClass))) {
      errorClass = nsINSSErrorsService::ERROR_CLASS_SSL_PROTOCOL;
    }

    nsCOMPtr<nsITransportSecurityInfo> tsi;
    if (aFailedChannel) {
      aFailedChannel->GetSecurityInfo(getter_AddRefs(tsi));
    }
    if (tsi) {
      uint32_t securityState;
      tsi->GetSecurityState(&securityState);
      if (securityState & nsIWebProgressListener::STATE_USES_SSL_3) {
        error = "sslv3Used";
        addHostPort = true;
      } else if (securityState &
                 nsIWebProgressListener::STATE_USES_WEAK_CRYPTO) {
        error = "weakCryptoUsed";
        addHostPort = true;
      }
    } else {
      // No channel, let's obtain the generic error message
      if (nsserr) {
        nsserr->GetErrorMessage(aError, messageStr);
      }
    }
    // We don't have a message string here anymore but DisplayLoadError
    // requires a non-empty messageStr.
    messageStr.Truncate();
    messageStr.AssignLiteral(u" ");
    if (errorClass == nsINSSErrorsService::ERROR_CLASS_BAD_CERT) {
      error = "nssBadCert";

      // If this is an HTTP Strict Transport Security host or a pinned host
      // and the certificate is bad, don't allow overrides (RFC 6797 section
      // 12.1).
      bool isStsHost = false;
      bool isPinnedHost = false;
      OriginAttributes attrsForHSTS;
      if (aFailedChannel) {
        StoragePrincipalHelper::GetOriginAttributesForHSTS(aFailedChannel,
                                                           attrsForHSTS);
      } else {
        attrsForHSTS = GetOriginAttributes();
      }

      if (XRE_IsParentProcess()) {
        nsCOMPtr<nsISiteSecurityService> sss =
            do_GetService(NS_SSSERVICE_CONTRACTID, &rv);
        NS_ENSURE_SUCCESS(rv, rv);
        rv = sss->IsSecureURI(aURI, attrsForHSTS, &isStsHost);
        NS_ENSURE_SUCCESS(rv, rv);
      } else {
        mozilla::dom::ContentChild* cc =
            mozilla::dom::ContentChild::GetSingleton();
        cc->SendIsSecureURI(aURI, attrsForHSTS, &isStsHost);
      }
      nsCOMPtr<nsIPublicKeyPinningService> pkps =
          do_GetService(NS_PKPSERVICE_CONTRACTID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = pkps->HostHasPins(aURI, &isPinnedHost);

      if (Preferences::GetBool("browser.xul.error_pages.expert_bad_cert",
                               false)) {
        cssClass.AssignLiteral("expertBadCert");
      }

      // HSTS/pinning takes precedence over the expert bad cert pref. We
      // never want to show the "Add Exception" button for these sites.
      // In the future we should differentiate between an HSTS host and a
      // pinned host and display a more informative message to the user.
      if (isStsHost || isPinnedHost) {
        isBadStsCertError = true;
        cssClass.AssignLiteral("badStsCert");
      }

      errorPage.Assign("certerror");
    } else {
      error = "nssFailure2";
    }
  } else if (NS_ERROR_PHISHING_URI == aError ||
             NS_ERROR_MALWARE_URI == aError ||
             NS_ERROR_UNWANTED_URI == aError ||
             NS_ERROR_HARMFULADDON_URI == aError ||
             NS_ERROR_HARMFUL_URI == aError) {
    nsAutoCString host;
    aURI->GetHost(host);
    CopyUTF8toUTF16(host, *formatStrs.AppendElement());

    // Malware and phishing detectors may want to use an alternate error
    // page, but if the pref's not set, we'll fall back on the standard page
    nsAutoCString alternateErrorPage;
    nsresult rv = Preferences::GetCString("urlclassifier.alternate_error_page",
                                          alternateErrorPage);
    if (NS_SUCCEEDED(rv)) {
      errorPage.Assign(alternateErrorPage);
    }

    if (NS_ERROR_PHISHING_URI == aError) {
      error = "deceptiveBlocked";
    } else if (NS_ERROR_MALWARE_URI == aError) {
      error = "malwareBlocked";
    } else if (NS_ERROR_UNWANTED_URI == aError) {
      error = "unwantedBlocked";
    } else if (NS_ERROR_HARMFUL_URI == aError) {
      error = "harmfulBlocked";
    } else if (NS_ERROR_HARMFULADDON_URI == aError) {
      error = "addonBlocked";
    }

    cssClass.AssignLiteral("blacklist");
  } else if (NS_ERROR_CONTENT_CRASHED == aError) {
    errorPage.AssignLiteral("tabcrashed");
    error = "tabcrashed";

    RefPtr<EventTarget> handler = mChromeEventHandler;
    if (handler) {
      nsCOMPtr<Element> element = do_QueryInterface(handler);
      element->GetAttribute(u"crashedPageTitle"_ns, messageStr);
    }

    // DisplayLoadError requires a non-empty messageStr to proceed and call
    // LoadErrorPage. If the page doesn't have a title, we will use a blank
    // space which will be trimmed and thus treated as empty by the front-end.
    if (messageStr.IsEmpty()) {
      messageStr.AssignLiteral(u" ");
    }
  } else if (NS_ERROR_FRAME_CRASHED == aError) {
    errorPage.AssignLiteral("framecrashed");
    error = "framecrashed";
    messageStr.AssignLiteral(u" ");
  } else if (NS_ERROR_BUILDID_MISMATCH == aError) {
    errorPage.AssignLiteral("restartrequired");
    error = "restartrequired";

    // DisplayLoadError requires a non-empty messageStr to proceed and call
    // LoadErrorPage. If the page doesn't have a title, we will use a blank
    // space which will be trimmed and thus treated as empty by the front-end.
    if (messageStr.IsEmpty()) {
      messageStr.AssignLiteral(u" ");
    }
  } else if (aError == NS_ERROR_RESTRICTED_CONTENT) {
    errorPage.AssignLiteral("restricted");
    error = "restrictedcontent";
    if (messageStr.IsEmpty()) {
      messageStr.AssignLiteral(u" ");
    }
  } else {
    // Errors requiring simple formatting
    switch (aError) {
      case NS_ERROR_MALFORMED_URI:
        // URI is malformed
        error = "malformedURI";
        errorDescriptionID = "malformedURI2";
        break;
      case NS_ERROR_REDIRECT_LOOP:
        // Doc failed to load because the server generated too many redirects
        error = "redirectLoop";
        break;
      case NS_ERROR_UNKNOWN_SOCKET_TYPE:
        // Doc failed to load because the client is misconfigured
        error = "clientSocketMisconfiguration";
        break;
      case NS_ERROR_NET_RESET:
      case NS_ERROR_OS_LOCAL_NETWORK_ACCESS_DENIED:
        // Doc failed to load because the server kept reseting the connection
        // before we could read any data from it
        // NS_ERROR_OS_LOCAL_NETWORK_ACCESS_DENIED is temporarily grouped
        // with netReset; it will get its own dedicated error page in
        // bug 2060188.
        error = "netReset";
        break;
      case NS_ERROR_DOCUMENT_NOT_CACHED:
        // Doc failed to load because the cache does not contain a copy of
        // the document.
        error = "notCached";
        break;
      case NS_ERROR_OFFLINE:
        // Doc failed to load because we are offline.
        error = "netOffline";
        break;
      case NS_ERROR_DOCUMENT_IS_PRINTMODE:
        // Doc navigation attempted while Printing or Print Preview
        error = "isprinting";
        break;
      case NS_ERROR_PORT_ACCESS_NOT_ALLOWED:
        // Port blocked for security reasons
        addHostPort = true;
        error = "deniedPortAccess";
        break;
      case NS_ERROR_UNKNOWN_PROXY_HOST:
        // Proxy hostname could not be resolved.
        error = "proxyResolveFailure";
        break;
      case NS_ERROR_PROXY_CONNECTION_REFUSED:
      case NS_ERROR_PROXY_FORBIDDEN:
      case NS_ERROR_PROXY_NOT_IMPLEMENTED:
      case NS_ERROR_PROXY_AUTHENTICATION_FAILED:
      case NS_ERROR_PROXY_TOO_MANY_REQUESTS:
        // Proxy connection was refused.
        error = "proxyConnectFailure";
        break;
      case NS_ERROR_INVALID_CONTENT_ENCODING:
        // Bad Content Encoding.
        error = "contentEncodingError";
        break;
      case NS_ERROR_UNSAFE_CONTENT_TYPE:
        // Channel refused to load from an unrecognized content type.
        error = "unsafeContentType";
        break;
      case NS_ERROR_CORRUPTED_CONTENT:
        // Broken Content Detected. e.g. Content-MD5 check failure.
        error = "corruptedContentErrorv2";
        break;
      case NS_ERROR_INTERCEPTION_FAILED:
        // ServiceWorker intercepted request, but something went wrong.
        error = "corruptedContentErrorv2";
        break;
      case NS_ERROR_NET_INADEQUATE_SECURITY:
        // Server negotiated bad TLS for HTTP/2.
        error = "inadequateSecurityError";
        addHostPort = true;
        break;
      case NS_ERROR_BLOCKED_BY_POLICY:
        // Page blocked by policy
        error = "blockedByPolicy";
        break;
      case NS_ERROR_DOM_COOP_FAILED:
        error = "blockedByCOOP";
        errorDescriptionID = "blockedByCORP";
        break;
      case NS_ERROR_DOM_COEP_FAILED:
        error = "blockedByCOEP";
        errorDescriptionID = "blockedByCORP";
        break;
      case NS_ERROR_DOM_INVALID_HEADER_VALUE:
        error = "invalidHeaderValue";
        break;
      case NS_ERROR_NET_HTTP2_SENT_GOAWAY:
      case NS_ERROR_NET_HTTP3_PROTOCOL_ERROR:
        // HTTP/2 or HTTP/3 stack detected a protocol error
        error = "networkProtocolError";
        break;
      case NS_ERROR_BASIC_HTTP_AUTH_DISABLED:
        error = "basicHttpAuthDisabled";
        break;
      default:
        break;
    }
  }

  nsresult delegateErrorCode = aError;
  // If the HTTPS-Only Mode upgraded this request and the upgrade might have
  // caused this error, we replace the error-page with about:httpsonlyerror
  if (nsHTTPSOnlyUtils::CouldBeHttpsOnlyError(aFailedChannel, aError)) {
    errorPage.AssignLiteral("httpsonlyerror");
    delegateErrorCode = NS_ERROR_HTTPS_ONLY;
  } else if (isBadStsCertError) {
    delegateErrorCode = NS_ERROR_BAD_HSTS_CERT;
  }

  if (nsCOMPtr<nsILoadURIDelegate> loadURIDelegate = GetLoadURIDelegate()) {
    nsCOMPtr<nsIURI> errorPageURI;
    rv = loadURIDelegate->HandleLoadError(
        aURI, delegateErrorCode, NS_ERROR_GET_MODULE(delegateErrorCode),
        getter_AddRefs(errorPageURI));
    // If the docshell is going away there's no point in showing an error page.
    if (NS_FAILED(rv) || mIsBeingDestroyed) {
      *aDisplayedErrorPage = false;
      return NS_OK;
    }

    if (errorPageURI) {
      *aDisplayedErrorPage =
          NS_SUCCEEDED(LoadErrorPage(errorPageURI, aURI, aFailedChannel));
      return NS_OK;
    }
  }

  // Test if the error should be displayed
  if (!error) {
    return NS_OK;
  }

  if (!errorDescriptionID) {
    errorDescriptionID = error;
  }

  glean::page::load_error
      .Get(IsSubframe() ? "frame"_ns : "top"_ns,
           mozilla::dom::LoadErrorToTelemetryLabel(aError))
      .Add();

  // Test if the error needs to be formatted
  if (!messageStr.IsEmpty()) {
    // already obtained message
  } else {
    if (addHostPort) {
      // Build up the host:port string.
      nsAutoCString hostport;
      if (aURI) {
        aURI->GetHostPort(hostport);
      } else {
        hostport.Assign('?');
      }
      CopyUTF8toUTF16(hostport, *formatStrs.AppendElement());
    }

    nsAutoCString spec;
    rv = NS_ERROR_NOT_AVAILABLE;
    auto& nextFormatStr = *formatStrs.AppendElement();
    if (aURI) {
      // displaying "file://" is aesthetically unpleasing and could even be
      // confusing to the user
      if (aURI->SchemeIs("file")) {
        aURI->GetPathQueryRef(spec);
      } else {
        aURI->GetSpec(spec);
      }

      nsCOMPtr<nsITextToSubURI> textToSubURI(
          do_GetService(NS_ITEXTTOSUBURI_CONTRACTID, &rv));
      if (NS_SUCCEEDED(rv)) {
        rv = textToSubURI->UnEscapeURIForUI(spec, nextFormatStr);
      }
    } else {
      spec.Assign('?');
    }
    if (NS_FAILED(rv)) {
      CopyUTF8toUTF16(spec, nextFormatStr);
    }
    rv = NS_OK;

    nsAutoString str;
    rv =
        stringBundle->FormatStringFromName(errorDescriptionID, formatStrs, str);
    NS_ENSURE_SUCCESS(rv, rv);
    messageStr.Assign(str);
  }

  // Display the error as a page or an alert prompt
  NS_ENSURE_FALSE(messageStr.IsEmpty(), NS_ERROR_FAILURE);

  if ((NS_ERROR_NET_INTERRUPT == aError || NS_ERROR_NET_RESET == aError) &&
      aURI->SchemeIs("https")) {
    // Maybe TLS intolerant. Treat this as an SSL error.
    error = "nssFailure2";
  }

  if (mBrowsingContext->GetUseErrorPages()) {
    // Display an error page
    nsresult loadedPage =
        LoadErrorPage(aURI, aURL, errorPage.get(), error, messageStr.get(),
                      cssClass.get(), aFailedChannel);
    *aDisplayedErrorPage = NS_SUCCEEDED(loadedPage);
  } else {
    // The prompter reqires that our private window has a document (or it
    // asserts). Satisfy that assertion now since GetDoc will force
    // creation of one if it hasn't already been created.
    if (mScriptGlobal) {
      (void)mScriptGlobal->GetDoc();
    }

    // Display a message box
    prompter->Alert(nullptr, messageStr.get());
  }

  return NS_OK;
}

#define PREF_SAFEBROWSING_ALLOWOVERRIDE "browser.safebrowsing.allowOverride"

nsresult nsDocShell::LoadErrorPage(nsIURI* aURI, const char16_t* aURL,
                                   const char* aErrorPage,
                                   const char* aErrorType,
                                   const char16_t* aDescription,
                                   const char* aCSSClass,
                                   nsIChannel* aFailedChannel) {
  if (mIsBeingDestroyed) {
    return NS_ERROR_NOT_AVAILABLE;
  }

#if defined(DEBUG)
  if (MOZ_LOG_TEST(gDocShellLog, LogLevel::Debug)) {
    nsAutoCString chanName;
    if (aFailedChannel) {
      aFailedChannel->GetName(chanName);
    } else {
      chanName.AssignLiteral("<no channel>");
    }

    MOZ_LOG(gDocShellLog, LogLevel::Debug,
            ("nsDocShell[%p]::LoadErrorPage(\"%s\", \"%s\", {...}, [%s])\n",
             this, aURI ? aURI->GetSpecOrDefault().get() : "",
             NS_ConvertUTF16toUTF8(aURL).get(), chanName.get()));
  }
#endif

  nsAutoCString url;
  if (aURI) {
    nsresult rv = aURI->GetSpec(url);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (aURL) {
    CopyUTF16toUTF8(MakeStringSpan(aURL), url);
  } else {
    return NS_ERROR_INVALID_POINTER;
  }

  // Create a URL to pass all the error information through to the page.

#undef SAFE_ESCAPE
#define SAFE_ESCAPE(output, input, params)             \
  if (NS_WARN_IF(!NS_Escape(input, output, params))) { \
    return NS_ERROR_OUT_OF_MEMORY;                     \
  }

  nsCString escapedUrl, escapedError, escapedDescription, escapedCSSClass;
  SAFE_ESCAPE(escapedUrl, url, url_Path);
  SAFE_ESCAPE(escapedError, nsDependentCString(aErrorType), url_Path);
  SAFE_ESCAPE(escapedDescription, NS_ConvertUTF16toUTF8(aDescription),
              url_Path);
  if (aCSSClass) {
    nsCString cssClass(aCSSClass);
    SAFE_ESCAPE(escapedCSSClass, cssClass, url_Path);
  }
  nsCString errorPageUrl("about:");
  errorPageUrl.AppendASCII(aErrorPage);
  errorPageUrl.AppendLiteral("?e=");

  errorPageUrl.AppendASCII(escapedError.get());
  errorPageUrl.AppendLiteral("&u=");
  errorPageUrl.AppendASCII(escapedUrl.get());
  if ((strcmp(aErrorPage, "blocked") == 0) &&
      Preferences::GetBool(PREF_SAFEBROWSING_ALLOWOVERRIDE, true)) {
    errorPageUrl.AppendLiteral("&o=1");
  }
  if (!escapedCSSClass.IsEmpty()) {
    errorPageUrl.AppendLiteral("&s=");
    errorPageUrl.AppendASCII(escapedCSSClass.get());
  }
  errorPageUrl.AppendLiteral("&c=UTF-8");

  nsCOMPtr<nsICaptivePortalService> cps = do_GetService(NS_CAPTIVEPORTAL_CID);
  int32_t cpsState;
  if (cps && NS_SUCCEEDED(cps->GetState(&cpsState))) {
    if (cpsState == nsICaptivePortalService::LOCKED_PORTAL) {
      errorPageUrl.AppendLiteral("&captive=true");
    }
    if (strcmp(aErrorPage, "neterror") == 0) {
      static const char* const kCaptivePortalStateNames[] = {
          "unknown", "not_captive", "unlocked_portal", "locked_portal"};
      if (cpsState >= 0 &&
          size_t(cpsState) < std::size(kCaptivePortalStateNames)) {
        errorPageUrl.AppendLiteral("&captivePortalState=");
        errorPageUrl.AppendASCII(kCaptivePortalStateNames[cpsState]);
      }
    }
  }

  errorPageUrl.AppendLiteral("&d=");
  errorPageUrl.AppendASCII(escapedDescription.get());

  nsCOMPtr<nsIWritablePropertyBag2> props(do_QueryInterface(aFailedChannel));
  if (props) {
    nsAutoCString addonName;
    props->GetPropertyAsACString(u"blockedExtension"_ns, addonName);

    nsCString escapedAddonName;
    SAFE_ESCAPE(escapedAddonName, addonName, url_Path);

    errorPageUrl.AppendLiteral("&a=");
    errorPageUrl.AppendASCII(escapedAddonName.get());
  }

  nsCOMPtr<nsIURI> errorPageURI;
  nsresult rv = NS_NewURI(getter_AddRefs(errorPageURI), errorPageUrl);
  NS_ENSURE_SUCCESS(rv, rv);

  return LoadErrorPage(errorPageURI, aURI, aFailedChannel);
}

nsresult nsDocShell::LoadErrorPage(nsIURI* aErrorURI, nsIURI* aFailedURI,
                                   nsIChannel* aFailedChannel) {
  mFailedChannel = aFailedChannel;
  mFailedURI = aFailedURI;
  mFailedLoadType = mLoadType;

  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(aErrorURI);
  loadState->SetTriggeringPrincipal(nsContentUtils::GetSystemPrincipal());
  if (mBrowsingContext) {
    loadState->SetTriggeringSandboxFlags(mBrowsingContext->GetSandboxFlags());
    loadState->SetTriggeringWindowId(
        mBrowsingContext->GetCurrentInnerWindowId());
    nsPIDOMWindowInner* innerWin = mScriptGlobal->GetCurrentInnerWindow();
    if (innerWin) {
      loadState->SetTriggeringStorageAccess(innerWin->UsingStorageAccess());
    }
  }
  loadState->SetLoadType(LOAD_ERROR_PAGE);
  loadState->SetFirstParty(true);
  loadState->SetSourceBrowsingContext(mBrowsingContext);
  if (mLoadingEntry) {
    // We keep the loading entry for the load that failed here. If the user
    // reloads we want to try to reload the original load, not the error page.
    loadState->SetLoadingSessionHistoryInfo(
        MakeUnique<LoadingSessionHistoryInfo>(*mLoadingEntry));
  }

  // Prevent initial about:blank handling, as it's likely irrelevant and
  // keeps us from needing to change GeckoView / NavigationDelegateTest.
  // It also makes the load more consistent with non-about-blank cases.
  loadState->ProhibitInitialAboutBlankHandling();

  return InternalLoad(loadState);
}

MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
nsDocShell::Reload(uint32_t aReloadFlags) {
  return ReloadNavigable(Nothing(), aReloadFlags, nullptr,
                         UserNavigationInvolvement::BrowserUI);
}

// https://html.spec.whatwg.org/#reload
// To reload a navigable navigable given an optional serialized state-or-null
// navigationAPIState (default null) and an optional user navigation
// involvement userInvolvement (default "none"):
nsresult nsDocShell::ReloadNavigable(
    mozilla::Maybe<NotNull<JSContext*>> aCx, uint32_t aReloadFlags,
    nsIStructuredCloneContainer* aNavigationAPIState,
    UserNavigationInvolvement aUserInvolvement,
    NavigationAPIMethodTracker* aNavigationAPIMethodTracker) {
  AUTO_PROFILER_MARKER_UNTYPED(
      "nsDocShell:ReloadNavigable", DOM,
      MarkerOptions(MarkerStack::Capture(),
                    MarkerInnerWindowIdFromDocShell(this)));

  if (!IsNavigationAllowed()) {
    return NS_OK;  // JS may not handle returning of an error code
  }

  NS_ASSERTION(((aReloadFlags & INTERNAL_LOAD_FLAGS_LOADURI_SETUP_FLAGS) == 0),
               "Reload command not updated to use load flags!");
  NS_ASSERTION((aReloadFlags & EXTRA_LOAD_FLAGS) == 0,
               "Don't pass these flags to Reload");

  uint32_t loadType = MAKE_LOAD_TYPE(LOAD_RELOAD_NORMAL, aReloadFlags);
  NS_ENSURE_TRUE(IsValidLoadType(loadType), NS_ERROR_INVALID_ARG);
  NS_ENSURE_TRUE(
      aUserInvolvement == UserNavigationInvolvement::BrowserUI || aCx,
      NS_ERROR_INVALID_ARG);

  RefPtr<nsDocShell> docShell(this);

  // 1. If userInvolvement is not "browser UI", then:
  if (aUserInvolvement != UserNavigationInvolvement::BrowserUI) {
    // 1.1 Let navigation be navigable's active window's navigation API.
    nsPIDOMWindowOuter* windowOuter = GetWindow();
    MOZ_DIAGNOSTIC_ASSERT(windowOuter);
    nsPIDOMWindowInner* windowInner = windowOuter->GetCurrentInnerWindow();
    MOZ_DIAGNOSTIC_ASSERT(windowInner);
    RefPtr navigation = windowInner->Navigation();

    // 1.2 Let destinationNavigationAPIState be navigable's active session
    //     history entry's navigation API state.
    // 1.3 If navigationAPIState is not null, then set
    //     destinationNavigationAPIState to navigationAPIState.
    RefPtr<nsIStructuredCloneContainer> destinationNavigationAPIState =
        aNavigationAPIState;
    if (!destinationNavigationAPIState) {
      destinationNavigationAPIState =
          mActiveEntry ? mActiveEntry->GetNavigationAPIState() : nullptr;
    }

    // 1.4 Let continue be the result of firing a push/replace/reload navigate
    //     event at navigation with navigationType set to "reload",
    //     isSameDocument set to false, userInvolvement set to userInvolvement,
    //     destinationURL set to navigable's active session history entry's URL,
    //     and navigationAPIState set to destinationNavigationAPIState.
    // 1.5 If continue is false, then return.
    RefPtr destinationURL = mActiveEntry ? mActiveEntry->GetURI() : nullptr;
    if (navigation &&
        !navigation->FirePushReplaceReloadNavigateEvent(
            *aCx, NavigationType::Reload, destinationURL,
            /* aIsSameDocument */ false, Some(aUserInvolvement),
            /* aSourceElement*/ nullptr, /* aFormDataEntryList */ nullptr,
            destinationNavigationAPIState,
            /* aClassiCHistoryAPIState */ nullptr,
            aNavigationAPIMethodTracker)) {
      return NS_OK;
    }
  }

  // The following steps are implemented by the remainder of ReloadNavigable.

  // Send notifications to the HistoryListener if any, about the impending
  // reload
  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  MOZ_LOG(gSHLog, LogLevel::Debug, ("nsDocShell %p Reload", this));
  bool forceReload = IsForceReloadType(loadType);
  if (!XRE_IsParentProcess()) {
    ++mPendingReloadCount;
    nsCOMPtr<nsIDocumentViewer> viewer(mDocumentViewer);
    NS_ENSURE_STATE(viewer);

    bool okToUnload = true;
    MOZ_TRY(viewer->PermitUnload(&okToUnload));
    if (mIsBeingDestroyed) {
      // unload handler destroyed this docshell.
      return NS_ERROR_NOT_AVAILABLE;
    }
    if (!okToUnload) {
      return NS_OK;
    }

    RefPtr<Document> doc(GetDocument());
    RefPtr<BrowsingContext> browsingContext(mBrowsingContext);
    nsCOMPtr<nsIURI> currentURI(mCurrentURI);
    nsCOMPtr<nsIReferrerInfo> referrerInfo(mReferrerInfo);
    RefPtr stopDetector = MakeRefPtr<StopDetector>();
    nsCOMPtr<nsILoadGroup> loadGroup;
    GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup) {
      // loadGroup may be null in theory. In that case stopDetector just
      // doesn't do anything.
      loadGroup->AddRequest(stopDetector, nullptr);
    }

    ContentChild::GetSingleton()->SendNotifyOnHistoryReload(
        mBrowsingContext, forceReload,
        [docShell, doc, loadType, browsingContext, currentURI, referrerInfo,
         loadGroup, stopDetector](
            std::tuple<bool, Maybe<NotNull<RefPtr<nsDocShellLoadState>>>,
                       Maybe<bool>>&& aResult) {
          auto scopeExit = MakeScopeExit([loadGroup, stopDetector]() {
            if (loadGroup) {
              loadGroup->RemoveRequest(stopDetector, nullptr, NS_OK);
            }
          });

          // Decrease mPendingReloadCount before any other early returns!
          if (--(docShell->mPendingReloadCount) > 0) {
            return;
          }

          if (stopDetector->Canceled()) {
            return;
          }
          bool canReload;
          Maybe<NotNull<RefPtr<nsDocShellLoadState>>> loadState;
          Maybe<bool> reloadingActiveEntry;

          std::tie(canReload, loadState, reloadingActiveEntry) = aResult;

          if (!canReload) {
            return;
          }

          if (loadState.isSome()) {
            MOZ_LOG(
                gSHLog, LogLevel::Debug,
                ("nsDocShell %p Reload - LoadHistoryEntry", docShell.get()));
            loadState.ref()->SetNotifiedBeforeUnloadListeners(true);
            docShell->LoadHistoryEntry(loadState.ref(), loadType,
                                       reloadingActiveEntry.ref());
          } else {
            MOZ_LOG(gSHLog, LogLevel::Debug,
                    ("nsDocShell %p ReloadDocument", docShell.get()));
            ReloadDocument(docShell, doc, loadType, browsingContext, currentURI,
                           referrerInfo,
                           /* aNotifiedBeforeUnloadListeners */ true);
          }
        },
        [](mozilla::ipc::ResponseRejectReason) {});
  } else {
    // Parent process
    bool canReload = false;
    Maybe<NotNull<RefPtr<nsDocShellLoadState>>> loadState;
    Maybe<bool> reloadingActiveEntry;
    if (!mBrowsingContext->IsDiscarded()) {
      mBrowsingContext->Canonical()->NotifyOnHistoryReload(
          forceReload, canReload, loadState, reloadingActiveEntry);
    }
    if (canReload) {
      if (loadState.isSome()) {
        MOZ_LOG(gSHLog, LogLevel::Debug,
                ("nsDocShell %p Reload - LoadHistoryEntry", this));
        LoadHistoryEntry(loadState.ref(), loadType, reloadingActiveEntry.ref());
      } else {
        MOZ_LOG(gSHLog, LogLevel::Debug,
                ("nsDocShell %p ReloadDocument", this));
        RefPtr<Document> doc = GetDocument();
        RefPtr<BrowsingContext> bc = mBrowsingContext;
        nsCOMPtr<nsIURI> currentURI = mCurrentURI;
        nsCOMPtr<nsIReferrerInfo> referrerInfo = mReferrerInfo;
        ReloadDocument(this, doc, loadType, bc, currentURI, referrerInfo);
      }
    }
  }
  return NS_OK;
}

void nsDocShell::DisplayRestrictedContentError() {
  bool didDisplayLoadError = false;
  RefPtr<mozilla::dom::Document> doc = GetDocument();
  if (!doc) {
    return;
  }
  doc->TerminateParserAndDisableScripts();
  DisplayLoadError(NS_ERROR_RESTRICTED_CONTENT, doc->GetDocumentURI(), nullptr,
                   nullptr, &didDisplayLoadError);
}

/* static */
nsresult nsDocShell::ReloadDocument(nsDocShell* aDocShell, Document* aDocument,
                                    uint32_t aLoadType,
                                    BrowsingContext* aBrowsingContext,
                                    nsIURI* aCurrentURI,
                                    nsIReferrerInfo* aReferrerInfo,
                                    bool aNotifiedBeforeUnloadListeners) {
  if (!aDocument) {
    return NS_OK;
  }

  // Do not inherit owner from document
  uint32_t flags = INTERNAL_LOAD_FLAGS_NONE;
  nsAutoString srcdoc;
  nsIURI* baseURI = nullptr;
  nsCOMPtr<nsIURI> originalURI;
  nsCOMPtr<nsIURI> resultPrincipalURI;
  bool loadReplace = false;

  nsIPrincipal* triggeringPrincipal = aDocument->NodePrincipal();
  nsCOMPtr<nsIPolicyContainer> policyContainer =
      aDocument->GetPolicyContainer();
  uint32_t triggeringSandboxFlags = aDocument->GetSandboxFlags();
  uint64_t triggeringWindowId = aDocument->InnerWindowID();
  bool triggeringStorageAccess = aDocument->UsingStorageAccess();
  net::ClassificationFlags triggeringClassificationFlags =
      aDocument->GetScriptTrackingFlags();

  nsAutoString contentTypeHint;
  aDocument->GetContentType(contentTypeHint);

  if (aDocument->IsSrcdocDocument()) {
    aDocument->GetSrcdocData(srcdoc);
    flags |= INTERNAL_LOAD_FLAGS_IS_SRCDOC;
    baseURI = aDocument->GetBaseURI();
  } else {
    srcdoc = VoidString();
  }
  nsCOMPtr<nsIChannel> chan = aDocument->GetChannel();
  if (chan) {
    uint32_t loadFlags;
    chan->GetLoadFlags(&loadFlags);
    loadReplace = loadFlags & nsIChannel::LOAD_REPLACE;
    nsCOMPtr<nsIHttpChannel> httpChan(do_QueryInterface(chan));
    if (httpChan) {
      httpChan->GetOriginalURI(getter_AddRefs(originalURI));
    }

    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    loadInfo->GetResultPrincipalURI(getter_AddRefs(resultPrincipalURI));
  }

  if (!triggeringPrincipal) {
    MOZ_ASSERT(false, "Reload needs a valid triggeringPrincipal");
    return NS_ERROR_FAILURE;
  }

  // Stack variables to ensure changes to the member variables don't affect to
  // the call.
  nsCOMPtr<nsIURI> currentURI = aCurrentURI;

  // Reload always rewrites result principal URI.
  Maybe<nsCOMPtr<nsIURI>> emplacedResultPrincipalURI;
  emplacedResultPrincipalURI.emplace(std::move(resultPrincipalURI));

  RefPtr<WindowContext> context = aBrowsingContext->GetCurrentWindowContext();
  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(currentURI);
  loadState->SetReferrerInfo(aReferrerInfo);
  loadState->SetOriginalURI(originalURI);
  loadState->SetMaybeResultPrincipalURI(emplacedResultPrincipalURI);
  loadState->SetLoadReplace(loadReplace);
  loadState->SetTriggeringPrincipal(triggeringPrincipal);
  loadState->SetTriggeringSandboxFlags(triggeringSandboxFlags);
  loadState->SetTriggeringWindowId(triggeringWindowId);
  loadState->SetTriggeringStorageAccess(triggeringStorageAccess);
  loadState->SetTriggeringClassificationFlags(triggeringClassificationFlags);
  loadState->SetPrincipalToInherit(triggeringPrincipal);
  loadState->SetPolicyContainer(policyContainer);
  loadState->SetInternalLoadFlags(flags);
  loadState->SetTypeHint(NS_ConvertUTF16toUTF8(contentTypeHint));
  loadState->SetLoadType(aLoadType);
  loadState->SetFirstParty(true);
  loadState->SetSrcdocData(srcdoc);
  loadState->SetSourceBrowsingContext(aBrowsingContext);
  loadState->SetBaseURI(baseURI);
  loadState->SetHasValidUserGestureActivation(
      context && context->HasValidTransientUserGestureActivation());

  loadState->SetTextDirectiveUserActivation(
      aDocument->ConsumeTextDirectiveUserActivation() ||
      loadState->HasValidUserGestureActivation());

  loadState->SetNotifiedBeforeUnloadListeners(aNotifiedBeforeUnloadListeners);
  return aDocShell->InternalLoad(loadState);
}

// TODO: Convert this to MOZ_CAN_RUN_SCRIPT (bug 1415230)
MOZ_CAN_RUN_SCRIPT_BOUNDARY NS_IMETHODIMP
nsDocShell::Stop(uint32_t aStopFlags) {
  return StopInternal(aStopFlags, UnsetOngoingNavigation::Yes);
}

nsresult nsDocShell::StopInternal(
    uint32_t aStopFlags, UnsetOngoingNavigation aUnsetOngoingNavigation) {
  RefPtr kungFuDeathGrip = this;
  if (RefPtr<Document> doc = GetExtantDocument();
      aUnsetOngoingNavigation == UnsetOngoingNavigation::Yes && doc &&
      !doc->ShouldIgnoreOpens() &&
      mOngoingNavigation == Some(OngoingNavigation::NavigationID)) {
    SetOngoingNavigation(Nothing());
  }

  if (mLoadType == LOAD_ERROR_PAGE) {
    mActiveEntryIsLoadingFromSessionHistory = false;

    mFailedChannel = nullptr;
    mFailedURI = nullptr;
  }

  if (nsIWebNavigation::STOP_CONTENT & aStopFlags) {
    // Stop the document loading and animations
    if (mDocumentViewer) {
      nsCOMPtr<nsIDocumentViewer> viewer = mDocumentViewer;
      viewer->Stop();
    }
  } else if (nsIWebNavigation::STOP_NETWORK & aStopFlags) {
    // Stop the document loading only
    if (mDocumentViewer) {
      RefPtr<Document> doc = mDocumentViewer->GetDocument();
      if (doc) {
        doc->StopDocumentLoad();
      }
    }
  }

  if (nsIWebNavigation::STOP_NETWORK & aStopFlags) {
    // Suspend any timers that were set for this loader.  We'll clear
    // them out for good in CreateDocumentViewer.
    if (mRefreshURIList) {
      SuspendRefreshURIs();
      mSavedRefreshURIList.swap(mRefreshURIList);
      mRefreshURIList = nullptr;
    }

    // XXXbz We could also pass |this| to nsIURILoader::Stop.  That will
    // just call Stop() on us as an nsIDocumentLoader... We need fewer
    // redundant apis!
    if (aUnsetOngoingNavigation == UnsetOngoingNavigation::No && mLoadGroup) {
      // Tag load group cancellation as navigation-caused so that XHR can
      // suppress abort events (bug 1505389). SetCanceledReason is
      // first-write-wins, so nsDocLoader::Stop's reason won't overwrite.

      // XXX Consider using a flag on LoadGroup instead of CanceledReason
      mLoadGroup->SetCanceledReason("navigation"_ns);
    }
    // Cancel <form> planned navigations.
    // Not currently in the spec, but it follows other browsers' behaviour:
    // https://github.com/whatwg/html/issues/12609
    CancelPlannedFormNavigation();
    Stop();

    // Clear out mChannelToDisconnectOnPageHide. This page won't go in the
    // BFCache now, and the Stop above will have removed the DocumentChannel
    // from the loadgroup.
    mChannelToDisconnectOnPageHide = 0;
  }

  for (auto* child : mChildList.ForwardRange()) {
    nsCOMPtr<nsIWebNavigation> shellAsNav(do_QueryObject(child));
    if (shellAsNav) {
      shellAsNav->Stop(aStopFlags);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetDocument(Document** aDocument) {
  NS_ENSURE_ARG_POINTER(aDocument);
  NS_ENSURE_TRUE(VerifyDocumentViewer(), NS_ERROR_FAILURE);

  RefPtr<Document> doc = mDocumentViewer->GetDocument();
  if (!doc) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  doc.forget(aDocument);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetCurrentURI(nsIURI** aURI) {
  NS_ENSURE_ARG_POINTER(aURI);

  nsCOMPtr<nsIURI> uri = mCurrentURI;
  uri.forget(aURI);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetSessionHistoryXPCOM(nsISupports** aSessionHistory) {
  NS_ENSURE_ARG_POINTER(aSessionHistory);
  RefPtr<ChildSHistory> shistory = GetSessionHistory();
  shistory.forget(aSessionHistory);
  return NS_OK;
}

//*****************************************************************************
// nsDocShell::nsIWebPageDescriptor
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::LoadPageAsViewSource(nsIDocShell* aOtherDocShell,
                                 const nsAString& aURI) {
  if (!aOtherDocShell) {
    return NS_ERROR_INVALID_POINTER;
  }
  auto* otherDocShell = nsDocShell::Cast(aOtherDocShell);

  Document* otherDoc = otherDocShell->GetDocument();
  NS_ENSURE_TRUE(otherDoc, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIURI> newURI;
  nsresult rv = NS_NewURI(getter_AddRefs(newURI), aURI);
  if (NS_FAILED(rv)) {
    return rv;
  }

  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(newURI);
  if (!otherDocShell->FillLoadStateFromCurrentEntry(*loadState)) {
    return NS_ERROR_INVALID_POINTER;
  }
  uint32_t cacheKey = otherDocShell->GetCacheKeyFromCurrentEntry().valueOr(0);

  // We're doing a load of the page, via an API that
  // is only exposed to system code.  The triggering principal for this load
  // should be the system principal.
  loadState->SetTriggeringPrincipal(nsContentUtils::GetSystemPrincipal());
  RefPtr<nsIPrincipal> nullPrincipal =
      NullPrincipal::CreateWithInheritedAttributes(otherDoc->NodePrincipal());
  loadState->SetPrincipalToInherit(nullPrincipal);
  loadState->SetPartitionedPrincipalToInherit(nullPrincipal);
  loadState->SetOriginalURI(nullptr);
  loadState->SetResultPrincipalURI(nullptr);

  // Initial about:blank handling is probably irrelevant, but newURI shouldn't
  // anyway be about:blank. Otherwise we should prohibit initial about blank
  // handling.
  MOZ_ASSERT(!NS_IsAboutBlankAllowQueryAndFragment(newURI),
             "We only expect view-source:// URIs");

  return InternalLoad(loadState, Some(cacheKey));
}

already_AddRefed<nsIInputStream> nsDocShell::GetPostDataFromCurrentEntry()
    const {
  nsCOMPtr<nsIInputStream> postData;
  if (mActiveEntry) {
    postData = mActiveEntry->GetPostData();
  } else if (mLoadingEntry) {
    postData = mLoadingEntry->mInfo.GetPostData();
  }

  return postData.forget();
}

Maybe<uint32_t> nsDocShell::GetCacheKeyFromCurrentEntry() const {
  if (mActiveEntry) {
    return Some(mActiveEntry->GetCacheKey());
  }

  if (mLoadingEntry) {
    return Some(mLoadingEntry->mInfo.GetCacheKey());
  }

  return Nothing();
}

bool nsDocShell::FillLoadStateFromCurrentEntry(
    nsDocShellLoadState& aLoadState) {
  if (mLoadingEntry) {
    mLoadingEntry->mInfo.FillLoadInfo(aLoadState);
    return true;
  }
  if (mActiveEntry) {
    mActiveEntry->FillLoadInfo(aLoadState);
    return true;
  }
  return false;
}

//*****************************************************************************
// nsDocShell::nsIBaseWindow
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::Destroy() {
  // XXX: We allow this function to be called just once.  If you are going to
  // reset new variables in this function, please make sure the variables will
  // never be re-initialized.  Adding assertions to check |mIsBeingDestroyed|
  // in the setter functions for the variables would be enough.
  if (mIsBeingDestroyed) {
    return NS_ERROR_DOCSHELL_DYING;
  }

  NS_ASSERTION(mItemType == typeContent || mItemType == typeChrome,
               "Unexpected item type in docshell");

  nsCOMPtr<nsIObserverService> serv = services::GetObserverService();
  if (serv) {
    const char* msg = mItemType == typeContent
                          ? NS_WEBNAVIGATION_DESTROY
                          : NS_CHROME_WEBNAVIGATION_DESTROY;
    serv->NotifyObservers(GetAsSupports(this), msg, nullptr);
  }

  mIsBeingDestroyed = true;

  // Brak the cycle with the initial client, if present.
  mInitialClientSource.reset();

  // Make sure to blow away our mLoadingURI just in case.  No loads
  // from inside this pagehide.
  mLoadingURI = nullptr;

  // Fire unload event before we blow anything away.
  (void)FirePageHideNotification();

  // Note: mContentListener can be null if Init() failed and we're being
  // called from the destructor.
  if (mContentListener) {
    mContentListener->DropDocShellReference();
    mContentListener->SetParentContentListener(nullptr);
    // Note that we do NOT set mContentListener to null here; that
    // way if someone tries to do a load in us after this point
    // the nsDSURIContentListener will block it.  All of which
    // means that we should do this before calling Stop(), of
    // course.
  }

  if (BrowsingContext* browsingContext = GetBrowsingContext();
      browsingContext && !browsingContext->IsTop()) {
    InformNavigationAPIAboutChildNavigableDestruction();
  }

  // Stop any URLs that are currently being loaded...
  Stop(nsIWebNavigation::STOP_ALL);

  mEditorData = nullptr;

  // Save the state of the current document, before destroying the window.
  // This is needed to capture the state of a frameset when the new document
  // causes the frameset to be destroyed...
  PersistLayoutHistoryState();

  // Remove this docshell from its parent's child list
  nsCOMPtr<nsIDocShellTreeItem> docShellParentAsItem =
      do_QueryInterface(GetAsSupports(mParent));
  if (docShellParentAsItem) {
    docShellParentAsItem->RemoveChild(this);
  }

  DestroyDocumentViewer();

  nsDocLoader::Destroy();

  mParentWidget = nullptr;
  SetCurrentURIInternal(nullptr);

  if (mScriptGlobal) {
    mScriptGlobal->DetachFromDocShell(!mWillChangeProcess);
    mScriptGlobal = nullptr;
  }

  if (mWillChangeProcess && !mBrowsingContext->IsDiscarded()) {
    mBrowsingContext->PrepareForProcessChange();
  }

  SetTreeOwner(nullptr);

  mBrowserChild = nullptr;

  mChromeEventHandler = nullptr;

  mBCWebProgressStatusFilter = nullptr;

  // Cancel any timers that were set for this docshell; this is needed
  // to break the cycle between us and the timers.
  CancelRefreshURITimers();

  return NS_OK;
}

double nsDocShell::GetWidgetCSSToDeviceScale() {
  if (mParentWidget) {
    return mParentWidget->GetDefaultScale().scale;
  }
  if (nsCOMPtr<nsIBaseWindow> ownerWindow = do_QueryInterface(mTreeOwner)) {
    return ownerWindow->GetWidgetCSSToDeviceScale();
  }
  return 1.0;
}

NS_IMETHODIMP
nsDocShell::GetDevicePixelsPerDesktopPixel(double* aScale) {
  if (mParentWidget) {
    *aScale = mParentWidget->GetDesktopToDeviceScale().scale;
    return NS_OK;
  }

  nsCOMPtr<nsIBaseWindow> ownerWindow(do_QueryInterface(mTreeOwner));
  if (ownerWindow) {
    return ownerWindow->GetDevicePixelsPerDesktopPixel(aScale);
  }

  *aScale = 1.0;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetPosition(int32_t aX, int32_t aY) {
  mBounds.MoveTo(aX, aY);

  if (mDocumentViewer) {
    NS_ENSURE_SUCCESS(mDocumentViewer->Move(aX, aY), NS_ERROR_FAILURE);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetPositionDesktopPix(int32_t aX, int32_t aY) {
  nsCOMPtr<nsIBaseWindow> ownerWindow(do_QueryInterface(mTreeOwner));
  if (ownerWindow) {
    return ownerWindow->SetPositionDesktopPix(aX, aY);
  }

  double scale = 1.0;
  GetDevicePixelsPerDesktopPixel(&scale);
  return SetPosition(NSToIntRound(aX * scale), NSToIntRound(aY * scale));
}

NS_IMETHODIMP
nsDocShell::GetPosition(int32_t* aX, int32_t* aY) {
  return GetPositionAndSize(aX, aY, nullptr, nullptr);
}

NS_IMETHODIMP
nsDocShell::SetSize(int32_t aWidth, int32_t aHeight, bool aRepaint) {
  int32_t x = 0, y = 0;
  GetPosition(&x, &y);
  return SetPositionAndSize(x, y, aWidth, aHeight,
                            aRepaint ? nsIBaseWindow::eRepaint : 0);
}

NS_IMETHODIMP
nsDocShell::GetSize(int32_t* aWidth, int32_t* aHeight) {
  return GetPositionAndSize(nullptr, nullptr, aWidth, aHeight);
}

NS_IMETHODIMP
nsDocShell::SetPositionAndSize(int32_t aX, int32_t aY, int32_t aWidth,
                               int32_t aHeight, uint32_t aFlags) {
  mBounds.SetRect(aX, aY, aWidth, aHeight);

  // Hold strong ref, since SetBounds can make us null out mDocumentViewer
  nsCOMPtr<nsIDocumentViewer> viewer = mDocumentViewer;
  if (viewer) {
    uint32_t cvflags = (aFlags & nsIBaseWindow::eDelayResize)
                           ? nsIDocumentViewer::eDelayResize
                           : 0;
    // XXX Border figured in here or is that handled elsewhere?
    nsresult rv = viewer->SetBoundsWithFlags(mBounds, cvflags);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_FAILURE);
  }

  if (nsCOMPtr<nsIObserverService> obs = services::GetObserverService()) {
    obs->NotifyObservers(GetAsSupports(this), "docshell-position-size-changed",
                         nullptr);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetPositionAndSize(int32_t* aX, int32_t* aY, int32_t* aWidth,
                               int32_t* aHeight) {
  if (mParentWidget) {
    // ensure size is up-to-date if window has changed resolution
    LayoutDeviceIntRect r = mParentWidget->GetClientBounds();
    SetPositionAndSize(mBounds.X(), mBounds.Y(), r.Width(), r.Height(), 0);
  }

  // We should really consider just getting this information from
  // our window instead of duplicating the storage and code...
  if (aWidth || aHeight) {
    // Caller wants to know our size; make sure to give them up to
    // date information.
    RefPtr<Document> doc(do_GetInterface(GetAsSupports(mParent)));
    if (doc) {
      doc->FlushPendingNotifications(FlushType::Layout);
    }
  }

  DoGetPositionAndSize(aX, aY, aWidth, aHeight);
  return NS_OK;
}

void nsDocShell::DoGetPositionAndSize(int32_t* aX, int32_t* aY, int32_t* aWidth,
                                      int32_t* aHeight) {
  if (aX) {
    *aX = mBounds.X();
  }
  if (aY) {
    *aY = mBounds.Y();
  }
  if (aWidth) {
    *aWidth = mBounds.Width();
  }
  if (aHeight) {
    *aHeight = mBounds.Height();
  }
}

NS_IMETHODIMP
nsDocShell::SetDimensions(DimensionRequest&& aRequest) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShell::GetDimensions(DimensionKind aDimensionKind, int32_t* aX,
                          int32_t* aY, int32_t* aCX, int32_t* aCY) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShell::GetParentWidget(nsIWidget** aParentWidget) {
  NS_ENSURE_ARG_POINTER(aParentWidget);

  *aParentWidget = mParentWidget;
  NS_IF_ADDREF(*aParentWidget);

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetParentWidget(nsIWidget* aParentWidget) {
  MOZ_ASSERT(!mIsBeingDestroyed);
  mParentWidget = aParentWidget;

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetNativeHandle(nsAString& aNativeHandle) {
  // the nativeHandle should be accessed from nsIAppWindow
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShell::GetVisibility(bool* aVisibility) {
  NS_ENSURE_ARG_POINTER(aVisibility);

  *aVisibility = false;

  if (!mDocumentViewer) {
    return NS_OK;
  }

  PresShell* presShell = GetPresShell();
  if (!presShell || presShell->IsUnderHiddenEmbedderElement()) {
    // NOTE(emilio): IsUnderHiddenEmbedderElement() accounts for ancestors as
    // well.
    return NS_OK;
  }

  nsCOMPtr<nsIBaseWindow> treeOwnerAsWin(do_QueryInterface(mTreeOwner));
  if (!treeOwnerAsWin) {
    *aVisibility = true;
    return NS_OK;
  }

  // Check with the tree owner as well to give embedders a chance to
  // expose visibility as well.
  nsresult rv = treeOwnerAsWin->GetVisibility(aVisibility);
  if (rv == NS_ERROR_NOT_IMPLEMENTED) {
    // The tree owner had no opinion on our visibility.
    *aVisibility = true;
    return NS_OK;
  }
  return rv;
}

void nsDocShell::ActivenessMaybeChanged() {
  const bool isActive = mBrowsingContext->IsActive();
  if (RefPtr<PresShell> presShell = GetPresShell()) {
    presShell->ActivenessMaybeChanged();
  }

  // Tell the window about it
  if (mScriptGlobal) {
    mScriptGlobal->SetIsBackground(!isActive);
    if (RefPtr<Document> doc = mScriptGlobal->GetExtantDoc()) {
      // Update orientation when the top-level browsing context becomes active.
      if (isActive && mBrowsingContext->IsTop() &&
          !mBrowsingContext->Windowless()) {
        // We only care about the top-level browsing context.
        auto orientation = mBrowsingContext->GetOrientationLock();
        ScreenOrientation::UpdateActiveOrientationLock(orientation);
      }

      doc->PostVisibilityUpdateEvent();
    }
  }

  // Tell the nsDOMNavigationTiming about it
  RefPtr<nsDOMNavigationTiming> timing = mTiming;
  if (!timing && mDocumentViewer) {
    if (Document* doc = mDocumentViewer->GetDocument()) {
      timing = doc->GetNavigationTiming();
    }
  }
  if (timing) {
    timing->NotifyDocShellStateChanged(
        isActive ? nsDOMNavigationTiming::DocShellState::eActive
                 : nsDOMNavigationTiming::DocShellState::eInactive);
  }

  // Restart or stop meta refresh timers if necessary
  if (mDisableMetaRefreshWhenInactive) {
    if (isActive) {
      ResumeRefreshURIs();
    } else {
      SuspendRefreshURIs();
    }
  }

  if (InputTaskManager::CanSuspendInputEvent()) {
    mBrowsingContext->Group()->UpdateInputTaskManagerIfNeeded(isActive);
  }
}

NS_IMETHODIMP
nsDocShell::SetDefaultLoadFlags(uint32_t aDefaultLoadFlags) {
  if (!mWillChangeProcess) {
    // Intentionally ignoring handling discarded browsing contexts.
    (void)mBrowsingContext->SetDefaultLoadFlags(aDefaultLoadFlags);
  } else {
    // Bug 1623565: DevTools tries to clean up defaultLoadFlags on
    // shutdown. Sorry DevTools, your DocShell is in another process.
    NS_WARNING("nsDocShell::SetDefaultLoadFlags called on Zombie DocShell");
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetDefaultLoadFlags(uint32_t* aDefaultLoadFlags) {
  *aDefaultLoadFlags = mBrowsingContext->GetDefaultLoadFlags();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetFailedChannel(nsIChannel** aFailedChannel) {
  NS_ENSURE_ARG_POINTER(aFailedChannel);
  Document* doc = GetDocument();
  if (!doc) {
    *aFailedChannel = nullptr;
    return NS_OK;
  }
  NS_IF_ADDREF(*aFailedChannel = doc->GetFailedChannel());
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetVisibility(bool aVisibility) {
  // Show()/Hide() may change mDocumentViewer.
  nsCOMPtr<nsIDocumentViewer> viewer = mDocumentViewer;
  if (!viewer) {
    return NS_OK;
  }
  if (aVisibility) {
    viewer->Show();
  } else {
    viewer->Hide();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetEnabled(bool* aEnabled) {
  NS_ENSURE_ARG_POINTER(aEnabled);
  *aEnabled = true;
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShell::SetEnabled(bool aEnabled) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
nsDocShell::GetMainWidget(nsIWidget** aMainWidget) {
  // We don't create our own widget, so simply return the parent one.
  return GetParentWidget(aMainWidget);
}

NS_IMETHODIMP
nsDocShell::GetTitle(nsAString& aTitle) {
  aTitle = mTitle;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetTitle(const nsAString& aTitle) {
  // Avoid unnecessary updates of the title if the URI and the title haven't
  // changed.
  if (mTitleValidForCurrentURI && mTitle == aTitle) {
    return NS_OK;
  }

  // Store local title
  mTitle = aTitle;
  mTitleValidForCurrentURI = true;

  // When title is set on the top object it should then be passed to the
  // tree owner.
  if (mBrowsingContext->IsTop()) {
    nsCOMPtr<nsIBaseWindow> treeOwnerAsWin(do_QueryInterface(mTreeOwner));
    if (treeOwnerAsWin) {
      treeOwnerAsWin->SetTitle(aTitle);
    }
  }

  if (mCurrentURI && mLoadType != LOAD_ERROR_PAGE) {
    UpdateGlobalHistoryTitle(mCurrentURI);
  }

  // Update SessionHistory with the document's title.
  if (mLoadType != LOAD_BYPASS_HISTORY && mLoadType != LOAD_ERROR_PAGE) {
    SetTitleOnHistoryEntry(true);
  }

  return NS_OK;
}

void nsDocShell::SetTitleOnHistoryEntry(bool aUpdateEntryInSessionHistory) {
  if (mActiveEntry && mBrowsingContext) {
    mActiveEntry->SetTitle(mTitle);
    if (aUpdateEntryInSessionHistory) {
      if (XRE_IsParentProcess()) {
        SessionHistoryEntry* entry =
            mBrowsingContext->Canonical()->GetActiveSessionHistoryEntry();
        if (entry) {
          entry->SetTitle(mTitle);
        }
      } else {
        (void)ContentChild::GetSingleton()->SendSessionHistoryEntryTitle(
            mBrowsingContext, mTitle);
      }
    }
  }
}

nsPoint nsDocShell::GetCurScrollPos() {
  nsPoint scrollPos;
  if (ScrollContainerFrame* sf = GetRootScrollContainerFrame()) {
    scrollPos = sf->GetVisualViewportOffset();
  }
  return scrollPos;
}

nsresult nsDocShell::SetCurScrollPosEx(int32_t aCurHorizontalPos,
                                       int32_t aCurVerticalPos) {
  ScrollContainerFrame* sf = GetRootScrollContainerFrame();
  NS_ENSURE_TRUE(sf, NS_ERROR_FAILURE);

  ScrollMode scrollMode = sf->ScrollModeForScrollBehavior();

  nsPoint targetPos(aCurHorizontalPos, aCurVerticalPos);
  sf->ScrollTo(targetPos, scrollMode);

  // Set the visual viewport offset as well.

  RefPtr<PresShell> presShell = GetPresShell();
  NS_ENSURE_TRUE(presShell, NS_ERROR_FAILURE);

  nsPresContext* presContext = presShell->GetPresContext();
  NS_ENSURE_TRUE(presContext, NS_ERROR_FAILURE);

  // Only the root content document can have a distinct visual viewport offset.
  if (!presContext->IsRootContentDocumentCrossProcess()) {
    return NS_OK;
  }

  // Not on a platform with a distinct visual viewport - don't bother setting
  // the visual viewport offset.
  if (!presShell->IsVisualViewportSizeSet()) {
    return NS_OK;
  }

  presShell->ScrollToVisual(
      targetPos, layers::ScrollOffsetUpdateType::MainThread, scrollMode);

  return NS_OK;
}

void nsDocShell::RestoreScrollPositionFromTargetSessionHistoryInfo(
    SessionHistoryInfo* aTarget) {
  nscoord bx = 0;
  nscoord by = 0;
  if (aTarget) {
    aTarget->GetScrollPosition(&bx, &by);
  }
  SetCurScrollPosEx(bx, by);
}

void nsDocShell::SetScrollbarPreference(mozilla::ScrollbarPreference aPref) {
  if (mScrollbarPref == aPref) {
    return;
  }
  mScrollbarPref = aPref;
  auto* ps = GetPresShell();
  if (!ps) {
    return;
  }
  nsIFrame* rootScrollContainerFrame = ps->GetRootScrollContainerFrame();
  if (!rootScrollContainerFrame) {
    return;
  }
  ps->FrameNeedsReflow(rootScrollContainerFrame,
                       IntrinsicDirty::FrameAncestorsAndDescendants,
                       NS_FRAME_IS_DIRTY);
}

//*****************************************************************************
// nsDocShell::nsIRefreshURI
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::RefreshURI(nsIURI* aURI, nsIPrincipal* aPrincipal,
                       uint32_t aDelay) {
  MOZ_ASSERT(!mIsBeingDestroyed);

  NS_ENSURE_ARG(aURI);

  /* Check if Meta refresh/redirects are permitted. Some
   * embedded applications may not want to do this.
   * Must do this before sending out NOTIFY_REFRESH events
   * because listeners may have side effects (e.g. displaying a
   * button to manually trigger the refresh later).
   */
  bool allowRedirects = true;
  GetAllowMetaRedirects(&allowRedirects);
  if (!allowRedirects) {
    return NS_OK;
  }

  // If any web progress listeners are listening for NOTIFY_REFRESH events,
  // give them a chance to block this refresh.
  bool sameURI;
  nsresult rv = aURI->Equals(mCurrentURI, &sameURI);
  if (NS_FAILED(rv)) {
    sameURI = false;
  }
  if (!RefreshAttempted(this, aURI, aDelay, sameURI)) {
    return NS_OK;
  }

  nsCOMPtr<nsITimerCallback> refreshTimer =
      MakeRefPtr<nsRefreshTimer>(this, aURI, aPrincipal, aDelay);

  BusyFlags busyFlags = GetBusyFlags();

  if (!mRefreshURIList) {
    mRefreshURIList = nsArray::Create();
  }

  if (busyFlags & BUSY_FLAGS_BUSY ||
      (!mBrowsingContext->IsActive() && mDisableMetaRefreshWhenInactive)) {
    // We don't  want to create the timer right now. Instead queue up the
    // request and trigger the timer in EndPageLoad() or whenever we become
    // active.
    mRefreshURIList->AppendElement(refreshTimer);
  } else {
    // There is no page loading going on right now.  Create the
    // timer and fire it right away.
    nsCOMPtr<nsPIDOMWindowOuter> win = GetWindow();
    NS_ENSURE_TRUE(win, NS_ERROR_FAILURE);

    nsCOMPtr<nsITimer> timer = MOZ_TRY(
        NS_NewTimerWithCallback(refreshTimer, aDelay, nsITimer::TYPE_ONE_SHOT));

    mRefreshURIList->AppendElement(timer);  // owning timer ref
  }
  return NS_OK;
}

nsresult nsDocShell::ForceRefreshURIFromTimer(nsIURI* aURI,
                                              nsIPrincipal* aPrincipal,
                                              uint32_t aDelay,
                                              nsITimer* aTimer) {
  MOZ_ASSERT(aTimer, "Must have a timer here");

  // Remove aTimer from mRefreshURIList if needed
  if (mRefreshURIList) {
    uint32_t n = 0;
    mRefreshURIList->GetLength(&n);

    for (uint32_t i = 0; i < n; ++i) {
      nsCOMPtr<nsITimer> timer = do_QueryElementAt(mRefreshURIList, i);
      if (timer == aTimer) {
        mRefreshURIList->RemoveElementAt(i);
        break;
      }
    }
  }

  return ForceRefreshURI(aURI, aPrincipal, aDelay);
}

NS_IMETHODIMP
nsDocShell::ForceRefreshURI(nsIURI* aURI, nsIPrincipal* aPrincipal,
                            uint32_t aDelay) {
  NS_ENSURE_ARG(aURI);

  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(aURI);
  loadState->SetOriginalURI(mCurrentURI);
  loadState->SetResultPrincipalURI(aURI);
  loadState->SetResultPrincipalURIIsSome(true);
  loadState->SetKeepResultPrincipalURIIfSet(true);
  loadState->SetIsMetaRefresh(true);

  RefPtr<Document> doc = GetDocument();
  NS_ENSURE_STATE(doc);

  // Set the triggering pricipal to aPrincipal if available, or current
  // document's principal otherwise.
  nsCOMPtr<nsIPrincipal> principal = aPrincipal;
  if (!principal) {
    principal = doc->NodePrincipal();
  }
  loadState->SetTriggeringPrincipal(principal);
  loadState->SetPolicyContainer(doc->GetPolicyContainer());
  loadState->SetHasValidUserGestureActivation(
      doc->HasValidTransientUserGestureActivation());

  loadState->SetTextDirectiveUserActivation(
      doc->ConsumeTextDirectiveUserActivation() ||
      loadState->HasValidUserGestureActivation());
  loadState->SetTriggeringSandboxFlags(doc->GetSandboxFlags());
  loadState->SetTriggeringWindowId(doc->InnerWindowID());
  loadState->SetTriggeringStorageAccess(doc->UsingStorageAccess());
  loadState->SetTriggeringClassificationFlags(doc->GetScriptTrackingFlags());

  loadState->SetPrincipalIsExplicit(true);

  /* Check if this META refresh causes a redirection
   * to another site.
   */
  bool equalUri = false;
  nsresult rv = aURI->Equals(mCurrentURI, &equalUri);

  if (NS_SUCCEEDED(rv) && !equalUri && aDelay <= REFRESH_REDIRECT_TIMER) {
    /* It is a META refresh based redirection within the threshold time
     * we have in mind (15000 ms as defined by REFRESH_REDIRECT_TIMER).
     * Pass a REPLACE flag to LoadURI().
     */
    loadState->SetLoadType(LOAD_REFRESH_REPLACE);
  } else {
    loadState->SetLoadType(LOAD_REFRESH);
  }

  const bool sendReferrer = StaticPrefs::network_http_referer_sendFromRefresh();
  /* The document's referrer policy is needed instead of mReferrerInfo's
   * referrer policy.
   */
  const RefPtr referrerInfo = MakeRefPtr<ReferrerInfo>(*doc, sendReferrer);
  /* We mimic HTTP, which passes the original referrer. See step 3 of
   * <https://html.spec.whatwg.org/multipage/browsing-the-web.html#create-navigation-params-by-fetching>.
   */
  loadState->SetReferrerInfo(referrerInfo);

  loadState->SetLoadFlags(
      nsIWebNavigation::LOAD_FLAGS_DISALLOW_INHERIT_PRINCIPAL);
  loadState->SetFirstParty(true);

  /*
   * LoadURI(...) will cancel all refresh timers... This causes the
   * Timer and its refreshData instance to be released...
   */
  LoadURI(loadState, false);

  return NS_OK;
}

static const char16_t* SkipASCIIWhitespace(const char16_t* aStart,
                                           const char16_t* aEnd) {
  const char16_t* iter = aStart;
  while (iter != aEnd && mozilla::IsAsciiWhitespace(*iter)) {
    ++iter;
  }
  return iter;
}

static std::tuple<const char16_t*, const char16_t*> ExtractURLString(
    const char16_t* aPosition, const char16_t* aEnd) {
  MOZ_ASSERT(aPosition != aEnd);

  // 1. Let urlString be the substring of input from the code point at
  //    position to the end of the string.
  const char16_t* urlStart = aPosition;
  const char16_t* urlEnd = aEnd;

  // 2. If the code point in input pointed to by position is U+0055 (U) or
  //    U+0075 (u), then advance position to the next code point.
  //    Otherwise, jump to the step labeled skip quotes.
  if (*aPosition == 'U' || *aPosition == 'u') {
    ++aPosition;

    // 3. If the code point in input pointed to by position is U+0052 (R) or
    //    U+0072 (r), then advance position to the next code point.
    //    Otherwise, jump to the step labeled parse.
    if (aPosition == aEnd || (*aPosition != 'R' && *aPosition != 'r')) {
      return std::make_tuple(urlStart, urlEnd);
    }

    ++aPosition;

    // 4. If the code point in input pointed to by position is U+004C (L) or
    //    U+006C (l), then advance position to the next code point.
    //    Otherwise, jump to the step labeled parse.
    if (aPosition == aEnd || (*aPosition != 'L' && *aPosition != 'l')) {
      return std::make_tuple(urlStart, urlEnd);
    }

    ++aPosition;

    // 5. Skip ASCII whitespace within input given position.
    aPosition = SkipASCIIWhitespace(aPosition, aEnd);

    // 6. If the code point in input pointed to by position is U+003D (=),
    //    then advance position to the next code point. Otherwise, jump to
    //    the step labeled parse.
    if (aPosition == aEnd || *aPosition != '=') {
      return std::make_tuple(urlStart, urlEnd);
    }

    ++aPosition;

    // 7. Skip ASCII whitespace within input given position.
    aPosition = SkipASCIIWhitespace(aPosition, aEnd);
  }

  // 8. Skip quotes: If the code point in input pointed to by position is
  //    U+0027 (') or U+0022 ("), then let quote be that code point, and
  //    advance position to the next code point. Otherwise, let quote be
  //    the empty string.
  Maybe<char> quote;
  if (aPosition != aEnd && (*aPosition == '\'' || *aPosition == '"')) {
    quote.emplace(*aPosition);
    ++aPosition;
  }

  // 9. Set urlString to the substring of input from the code point at
  //    position to the end of the string.
  urlStart = aPosition;
  urlEnd = aEnd;

  // 10. If quote is not the empty string, and there is a code point in
  //     urlString equal to quote, then truncate urlString at that code
  //     point, so that it and all subsequent code points are removed.
  const char16_t* quotePos;
  if (quote.isSome() &&
      (quotePos = nsCharTraits<char16_t>::find(
           urlStart, std::distance(urlStart, aEnd), quote.value()))) {
    urlEnd = quotePos;
  }

  return std::make_tuple(urlStart, urlEnd);
}

void nsDocShell::SetupRefreshURIFromHeader(Document* aDocument,
                                           const nsAString& aHeader) {
  if (mIsBeingDestroyed) {
    return;
  }

  const char16_t* position = aHeader.BeginReading();
  const char16_t* end = aHeader.EndReading();

  // See https://html.spec.whatwg.org/#shared-declarative-refresh-steps.

  // 3. Skip ASCII whitespace
  position = SkipASCIIWhitespace(position, end);

  // 4. Let time be 0.
  CheckedInt<uint32_t> milliSeconds;

  // 5. Collect a sequence of code points that are ASCII digits
  const char16_t* digitsStart = position;
  while (position != end && mozilla::IsAsciiDigit(*position)) {
    ++position;
  }

  if (position == digitsStart) {
    // 6. If timeString is the empty string, then:
    //    1. If the code point in input pointed to by position is not U+002E
    //       (.), then return.
    if (position == end || *position != '.') {
      return;
    }
  } else {
    // 7. Otherwise, set time to the result of parsing timeString using the
    //    rules for parsing non-negative integers.
    nsContentUtils::ParseHTMLIntegerResultFlags result;
    uint32_t seconds =
        nsContentUtils::ParseHTMLInteger(digitsStart, position, &result);
    MOZ_ASSERT(!(result & nsContentUtils::eParseHTMLInteger_Negative));
    if (result & nsContentUtils::eParseHTMLInteger_Error) {
      // The spec assumes no errors here (since we only pass ASCII digits in),
      // but we can still overflow, so this block should deal with that (and
      // only that).
      MOZ_ASSERT(
          !(result & ~(nsContentUtils::eParseHTMLInteger_DidNotConsumeAllInput |
                       nsContentUtils::eParseHTMLInteger_Error |
                       nsContentUtils::eParseHTMLInteger_ErrorOverflow)));
      return;
    }
    MOZ_ASSERT(
        !(result & nsContentUtils::eParseHTMLInteger_DidNotConsumeAllInput));

    milliSeconds = seconds;
    milliSeconds *= 1000;
    if (!milliSeconds.isValid()) {
      return;
    }
  }

  // 8. Collect a sequence of code points that are ASCII digits and U+002E FULL
  //    STOP characters (.) from input given position. Ignore any collected
  //    characters.
  while (position != end &&
         (mozilla::IsAsciiDigit(*position) || *position == '.')) {
    ++position;
  }

  // 9. Let urlRecord be document's URL.
  nsCOMPtr<nsIURI> urlRecord(aDocument->GetDocumentURI());

  // 10. If position is not past the end of input
  if (position != end) {
    // 1. If the code point in input pointed to by position is not U+003B (;),
    //    U+002C (,), or ASCII whitespace, then return.
    if (*position != ';' && *position != ',' &&
        !mozilla::IsAsciiWhitespace(*position)) {
      return;
    }

    // 2. Skip ASCII whitespace within input given position.
    position = SkipASCIIWhitespace(position, end);

    // 3. If the code point in input pointed to by position is U+003B (;) or
    //    U+002C (,), then advance position to the next code point.
    if (position != end && (*position == ';' || *position == ',')) {
      ++position;

      // 4. Skip ASCII whitespace within input given position.
      position = SkipASCIIWhitespace(position, end);
    }

    // 11. If position is not past the end of input, then:
    if (position != end) {
      const char16_t* urlStart;
      const char16_t* urlEnd;

      // 1-10. See ExtractURLString.
      std::tie(urlStart, urlEnd) = ExtractURLString(position, end);

      // 11. Parse: Parse urlString relative to document. If that fails, return.
      //     Otherwise, set urlRecord to the resulting URL record.
      nsresult rv =
          NS_NewURI(getter_AddRefs(urlRecord),
                    Substring(urlStart, std::distance(urlStart, urlEnd)),
                    /* charset = */ nullptr, aDocument->GetDocBaseURI());
      NS_ENSURE_SUCCESS_VOID(rv);
    }
  }

  nsIPrincipal* principal = aDocument->NodePrincipal();
  nsCOMPtr<nsIScriptSecurityManager> securityManager =
      nsContentUtils::GetSecurityManager();
  nsresult rv = securityManager->CheckLoadURIWithPrincipal(
      principal, urlRecord,
      nsIScriptSecurityManager::LOAD_IS_AUTOMATIC_DOCUMENT_REPLACEMENT,
      aDocument->InnerWindowID());
  NS_ENSURE_SUCCESS_VOID(rv);

  bool isjs = true;
  rv = NS_URIChainHasFlags(
      urlRecord, nsIProtocolHandler::URI_OPENING_EXECUTES_SCRIPT, &isjs);
  NS_ENSURE_SUCCESS_VOID(rv);

  if (isjs) {
    return;
  }

  RefreshURI(urlRecord, principal, milliSeconds.value());
}

static void DoCancelRefreshURITimers(nsIMutableArray* aTimerList) {
  if (!aTimerList) {
    return;
  }

  uint32_t n = 0;
  aTimerList->GetLength(&n);

  while (n) {
    nsCOMPtr<nsITimer> timer(do_QueryElementAt(aTimerList, --n));

    aTimerList->RemoveElementAt(n);  // bye bye owning timer ref

    if (timer) {
      timer->Cancel();
    }
  }
}

NS_IMETHODIMP
nsDocShell::CancelRefreshURITimers() {
  DoCancelRefreshURITimers(mRefreshURIList);
  DoCancelRefreshURITimers(mSavedRefreshURIList);
  DoCancelRefreshURITimers(mBFCachedRefreshURIList);
  mRefreshURIList = nullptr;
  mSavedRefreshURIList = nullptr;
  mBFCachedRefreshURIList = nullptr;

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetRefreshPending(bool* aResult) {
  if (!mRefreshURIList) {
    *aResult = false;
    return NS_OK;
  }

  uint32_t count;
  nsresult rv = mRefreshURIList->GetLength(&count);
  if (NS_SUCCEEDED(rv)) {
    *aResult = (count != 0);
  }
  return rv;
}

void nsDocShell::RefreshURIToQueue() {
  if (mRefreshURIList) {
    uint32_t n = 0;
    mRefreshURIList->GetLength(&n);

    for (uint32_t i = 0; i < n; ++i) {
      nsCOMPtr<nsITimer> timer = do_QueryElementAt(mRefreshURIList, i);
      if (!timer) {
        continue;  // this must be a nsRefreshURI already
      }

      // Replace this timer object with a nsRefreshTimer object.
      nsCOMPtr<nsITimerCallback> callback;
      timer->GetCallback(getter_AddRefs(callback));

      timer->Cancel();

      mRefreshURIList->ReplaceElementAt(callback, i);
    }
  }
}

NS_IMETHODIMP
nsDocShell::SuspendRefreshURIs() {
  RefreshURIToQueue();

  // Suspend refresh URIs for our child shells as well.
  for (auto* child : mChildList.ForwardRange()) {
    nsCOMPtr<nsIDocShell> shell = do_QueryObject(child);
    if (shell) {
      shell->SuspendRefreshURIs();
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::ResumeRefreshURIs() {
  RefreshURIFromQueue();

  // Resume refresh URIs for our child shells as well.
  for (auto* child : mChildList.ForwardRange()) {
    nsCOMPtr<nsIDocShell> shell = do_QueryObject(child);
    if (shell) {
      shell->ResumeRefreshURIs();
    }
  }

  return NS_OK;
}

nsresult nsDocShell::RefreshURIFromQueue() {
  if (!mRefreshURIList) {
    return NS_OK;
  }
  uint32_t n = 0;
  mRefreshURIList->GetLength(&n);

  while (n) {
    nsCOMPtr<nsITimerCallback> refreshInfo =
        do_QueryElementAt(mRefreshURIList, --n);

    if (refreshInfo) {
      // This is the nsRefreshTimer object, waiting to be
      // setup in a timer object and fired.
      // Create the timer and  trigger it.
      uint32_t delay = static_cast<nsRefreshTimer*>(
                           static_cast<nsITimerCallback*>(refreshInfo))
                           ->GetDelay();
      nsCOMPtr<nsPIDOMWindowOuter> win = GetWindow();
      if (win) {
        nsCOMPtr<nsITimer> timer;
        NS_NewTimerWithCallback(getter_AddRefs(timer), refreshInfo, delay,
                                nsITimer::TYPE_ONE_SHOT);

        if (timer) {
          // Replace the nsRefreshTimer element in the queue with
          // its corresponding timer object, so that in case another
          // load comes through before the timer can go off, the timer will
          // get cancelled in CancelRefreshURITimer()
          mRefreshURIList->ReplaceElementAt(timer, n);
        }
      }
    }
  }

  return NS_OK;
}

static bool IsFollowupPartOfMultipart(nsIRequest* aRequest) {
  nsCOMPtr<nsIMultiPartChannel> multiPartChannel = do_QueryInterface(aRequest);
  bool firstPart = false;
  return multiPartChannel &&
         NS_SUCCEEDED(multiPartChannel->GetIsFirstPart(&firstPart)) &&
         !firstPart;
}

nsresult nsDocShell::Embed(nsIDocumentViewer* aDocumentViewer,
                           WindowGlobalChild* aWindowActor,
                           bool aIsTransientAboutBlank, nsIRequest* aRequest,
                           nsIURI* aPreviousURI) {
  // Save the LayoutHistoryState of the previous document, before
  // setting up new document
  PersistLayoutHistoryState();

  nsresult rv = SetupNewViewer(aDocumentViewer, aWindowActor);
  NS_ENSURE_SUCCESS(rv, rv);

  // XXX What if SetupNewViewer fails?
  if (mLoadingEntry) {
    // Set history.state
    SetDocCurrentStateObj(mLoadingEntry ? &mLoadingEntry->mInfo : nullptr);
  }

  if (!aIsTransientAboutBlank && !IsFollowupPartOfMultipart(aRequest)) {
    bool expired = false;
    uint32_t cacheKey = 0;
    nsCOMPtr<nsICacheInfoChannel> cacheChannel = do_QueryInterface(aRequest);
    if (cacheChannel) {
      // Check if the page has expired from cache
      uint32_t expTime = 0;
      cacheChannel->GetCacheTokenExpirationTime(&expTime);
      uint32_t now = PRTimeToSeconds(PR_Now());
      if (expTime <= now) {
        expired = true;
      }

      // The checks for updating cache key are similar to the old session
      // history in OnNewURI. Try to update the cache key if
      //  - we should update session history and aren't doing a session
      //    history load.
      //  - we're doing a forced reload.
      if (((!mLoadingEntry || !mLoadingEntry->mLoadIsFromSessionHistory) &&
           mBrowsingContext->ShouldUpdateSessionHistory(mLoadType)) ||
          IsForceReloadType(mLoadType)) {
        cacheChannel->GetCacheKey(&cacheKey);
      }
    }

    MOZ_LOG(gSHLog, LogLevel::Debug, ("document %p Embed", this));
    MoveLoadingToActiveEntry(expired, cacheKey, aPreviousURI);
  }

  bool updateHistory = true;

  // Determine if this type of load should update history
  switch (mLoadType) {
    case LOAD_NORMAL_REPLACE:
    case LOAD_REFRESH_REPLACE:
    case LOAD_STOP_CONTENT_AND_REPLACE:
    case LOAD_RELOAD_BYPASS_CACHE:
    case LOAD_RELOAD_BYPASS_PROXY:
    case LOAD_RELOAD_BYPASS_PROXY_AND_CACHE:
    case LOAD_REPLACE_BYPASS_CACHE:
      updateHistory = false;
      break;
    default:
      break;
  }

  if (!updateHistory) {
    SetLayoutHistoryState(nullptr);
  }

  return NS_OK;
}

//*****************************************************************************
// nsDocShell::nsIWebProgressListener
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::OnProgressChange(nsIWebProgress* aProgress, nsIRequest* aRequest,
                             int32_t aCurSelfProgress, int32_t aMaxSelfProgress,
                             int32_t aCurTotalProgress,
                             int32_t aMaxTotalProgress) {
  // Listeners in the parent process only care about aCurTotalProgress and
  // aMaxTotalProgress, which is internally managed by nsDocLoader. Because of
  // this, we don't send progress notifications except when they are recorded by
  // the toplevel context, and only report them on the toplevel context in the
  // parent process.
  //
  // FIXME: We should track progress for out-of-process iframes and manage total
  // progress in the parent process for more accurate notifications.
  MOZ_ASSERT(
      mBrowsingContext->IsTop(),
      "notification excluded in AddProgressListener(...) for non-toplevel BCs");

  if (nsCOMPtr<nsIWebProgressListener> listener = BCWebProgressListener()) {
    listener->OnProgressChange(aProgress, aRequest, aCurSelfProgress,
                               aMaxSelfProgress, aCurTotalProgress,
                               aMaxTotalProgress);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::OnStateChange(nsIWebProgress* aProgress, nsIRequest* aRequest,
                          uint32_t aStateFlags, nsresult aStatus) {
  // If we're receiving a notification on ourselves which has at least one of
  // the state change flags in kStateChangeFlagFilter, also notify WebProgress
  // on BrowsingContextWebProgress, potentially over IPC.
  //
  // NOTE: We don't notify for bubbled notifications (aProgress != this), as
  // BrowsingContextWebProgress independently handles event bubbling in the
  // parent process.
  //
  // NOTE: We don't filter notifications when registering our listener, as
  // `STATE_IS_REDIRECTED_DOCUMENT` cannot be filtered for at registration time.
  static constexpr uint32_t kStateChangeFlagFilter =
      STATE_IS_NETWORK | STATE_IS_DOCUMENT | STATE_IS_WINDOW |
      STATE_IS_REDIRECTED_DOCUMENT;
  if (aProgress == this && (aStateFlags & kStateChangeFlagFilter) != 0) {
    if (nsCOMPtr<nsIWebProgressListener> listener = BCWebProgressListener()) {
      listener->OnStateChange(aProgress, aRequest, aStateFlags, aStatus);
    }
  }

  if ((~aStateFlags & (STATE_START | STATE_IS_NETWORK)) == 0) {
    // Save timing statistics.
    nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
    nsCOMPtr<nsIURI> uri;
    channel->GetURI(getter_AddRefs(uri));
    nsAutoCString aURI;
    uri->GetAsciiSpec(aURI);

    if (this == aProgress) {
      (void)MaybeInitTiming();
      mTiming->NotifyFetchStart(uri,
                                ConvertLoadTypeToNavigationType(mLoadType));
      // If we are starting a DocumentChannel, we need to pass the timing
      // statistics so that should a process switch occur, the starting type can
      // be passed to the new DocShell running in the other content process.
      if (RefPtr<DocumentChannel> docChannel = do_QueryObject(aRequest)) {
        docChannel->SetNavigationTiming(mTiming);
      }
    }

    // Page has begun to load
    mBusyFlags = (BusyFlags)(BUSY_FLAGS_BUSY | BUSY_FLAGS_BEFORE_PAGE_LOAD);

    if ((aStateFlags & STATE_RESTORING) == 0) {
      if (SessionStorePlatformCollection()) {
        if (IsForceReloadType(mLoadType)) {
          if (WindowContext* windowContext =
                  mBrowsingContext->GetCurrentWindowContext()) {
            SessionStoreChild::From(windowContext->GetWindowGlobalChild())
                ->ResetSessionStore(mBrowsingContext,
                                    mBrowsingContext->GetSessionStoreEpoch());
          }
        }
      }
    }
  } else if ((~aStateFlags & (STATE_TRANSFERRING | STATE_IS_DOCUMENT)) == 0) {
    // Page is loading
    mBusyFlags = (BusyFlags)(BUSY_FLAGS_BUSY | BUSY_FLAGS_PAGE_LOADING);
  } else if ((aStateFlags & STATE_STOP) && (aStateFlags & STATE_IS_NETWORK)) {
    // Page has finished loading
    mBusyFlags = BUSY_FLAGS_NONE;
  }

  if ((~aStateFlags & (STATE_IS_DOCUMENT | STATE_STOP)) == 0) {
    nsCOMPtr<nsIWebProgress> webProgress =
        do_QueryInterface(GetAsSupports(this));
    // Is the document stop notification for this document?
    if (aProgress == webProgress.get()) {
      nsCOMPtr<nsIChannel> channel(do_QueryInterface(aRequest));
      EndPageLoad(aProgress, channel, aStatus);
    }
  }
  // note that redirect state changes will go through here as well, but it
  // is better to handle those in OnRedirectStateChange where more
  // information is available.
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::OnLocationChange(nsIWebProgress* aProgress, nsIRequest* aRequest,
                             nsIURI* aURI, uint32_t aFlags) {
  // If we're receiving a notification on ourselves, also notify WebProgress on
  // BrowsingContextWebProgress, potentially over IPC.
  //
  // NOTE: We don't notify for bubbled notifications (aProgress != this), as
  // BrowsingContextWebProgress independently handles event bubbling in the
  // parent process.
  //
  // NOTE: Tests depend on this happening before UpdateSecurityState.
  if (aProgress == this) {
    if (nsCOMPtr<nsIWebProgressListener> listener = BCWebProgressListener()) {
      listener->OnLocationChange(aProgress, aRequest, aURI, aFlags);
    }
  }

  // Since we've now changed Documents, notify the BrowsingContext that we've
  // changed. Ideally we'd just let the BrowsingContext do this when it
  // changes the current window global, but that happens before this and we
  // have a lot of tests that depend on the specific ordering of messages.
  bool isTopLevel = false;
  if (XRE_IsParentProcess() &&
      !(aFlags & nsIWebProgressListener::LOCATION_CHANGE_SAME_DOCUMENT) &&
      NS_SUCCEEDED(aProgress->GetIsTopLevel(&isTopLevel)) && isTopLevel) {
    GetBrowsingContext()->Canonical()->UpdateSecurityState();
  }
  return NS_OK;
}

void nsDocShell::OnRedirectStateChange(nsIChannel* aOldChannel,
                                       nsIChannel* aNewChannel,
                                       uint32_t aRedirectFlags,
                                       uint32_t aStateFlags) {
  NS_ASSERTION(aStateFlags & STATE_REDIRECTING,
               "Calling OnRedirectStateChange when there is no redirect");

  if (!(aStateFlags & STATE_IS_DOCUMENT)) {
    return;  // not a toplevel document
  }

  nsCOMPtr<nsIURI> oldURI, newURI;
  aOldChannel->GetURI(getter_AddRefs(oldURI));
  aNewChannel->GetURI(getter_AddRefs(newURI));
  if (!oldURI || !newURI) {
    return;
  }

  // DocumentChannel adds redirect chain to global history in the parent
  // process. The redirect chain can't be queried from the content process, so
  // there's no need to update global history here.
  RefPtr<DocumentChannel> docChannel = do_QueryObject(aOldChannel);
  if (!docChannel) {
    // Below a URI visit is saved (see AddURIVisit method doc).
    // The visit chain looks something like:
    //   ...
    //   Site N - 1
    //                =>  Site N
    //   (redirect to =>) Site N + 1 (we are here!)

    // Get N - 1 and transition type
    nsCOMPtr<nsIURI> previousURI;
    uint32_t previousFlags = 0;
    ExtractLastVisit(aOldChannel, getter_AddRefs(previousURI), &previousFlags);

    if (aRedirectFlags & nsIChannelEventSink::REDIRECT_INTERNAL ||
        net::ChannelIsPost(aOldChannel)) {
      // 1. Internal redirects are ignored because they are specific to the
      //    channel implementation.
      // 2. POSTs are not saved by global history.
      //
      // Regardless, we need to propagate the previous visit to the new
      // channel.
      SaveLastVisit(aNewChannel, previousURI, previousFlags);
    } else {
      // Get the HTTP response code, if available.
      uint32_t responseStatus = 0;
      nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aOldChannel);
      if (httpChannel) {
        (void)httpChannel->GetResponseStatus(&responseStatus);
      }

      // Add visit N -1 => N
      AddURIVisit(oldURI, previousURI, previousFlags, responseStatus);

      // Since N + 1 could be the final destination, we will not save N => N + 1
      // here.  OnNewURI will do that, so we will cache it.
      SaveLastVisit(aNewChannel, oldURI, aRedirectFlags);
    }
  }

  if (!(aRedirectFlags & nsIChannelEventSink::REDIRECT_INTERNAL) &&
      mLoadType & (LOAD_CMD_RELOAD | LOAD_CMD_HISTORY)) {
    mLoadType = LOAD_NORMAL_REPLACE;
  }
}

NS_IMETHODIMP
nsDocShell::OnStatusChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                           nsresult aStatus, const char16_t* aMessage) {
  // If we're receiving a notification on ourselves, also notify WebProgress on
  // BrowsingContextWebProgress, potentially over IPC.
  //
  // NOTE: We don't notify for bubbled notifications (aWebProgress != this), as
  // BrowsingContextWebProgress independently handles event bubbling in the
  // parent process.
  if (aWebProgress == this) {
    if (nsCOMPtr<nsIWebProgressListener> listener = BCWebProgressListener()) {
      listener->OnStatusChange(aWebProgress, aRequest, aStatus, aMessage);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::OnSecurityChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                             uint32_t aState) {
  // If we're receiving a notification on ourselves, also notify WebProgress on
  // BrowsingContextWebProgress, potentially over IPC.
  //
  // NOTE: We don't notify for bubbled notifications (aWebProgress != this), as
  // BrowsingContextWebProgress independently handles event bubbling in the
  // parent process.
  if (aWebProgress == this) {
    if (nsCOMPtr<nsIWebProgressListener> listener = BCWebProgressListener()) {
      listener->OnSecurityChange(aWebProgress, aRequest, aState);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                   nsIRequest* aRequest, uint32_t aEvent) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

already_AddRefed<nsIWebProgressListener> nsDocShell::BCWebProgressListener() {
  // If this BrowsingContext has been replaced, we should discard any
  // notifications which would otherwise be delivered in-process.
  if (XRE_IsParentProcess() && mBrowsingContext->Canonical()->IsReplaced()) {
    return nullptr;
  }

  // Create a nsBrowserStatusFilter to perform some throttling of
  // OnProgressChange and OnStatusChange notifications which are delivered to
  // our BCWebProgress listener. This reduces the amount of IPC traffic.
  if (!mBCWebProgressStatusFilter && !mIsBeingDestroyed) {
    nsCOMPtr<nsIWebProgressListener> innerListener;
    if (XRE_IsParentProcess()) {
      innerListener = mBrowsingContext->Canonical()->GetWebProgress();
    } else {
      innerListener = do_QueryReferent(mBrowserChild);
    }
    if (innerListener) {
      // NOTE: We need to disable filtering of StateChange events here, as
      // listeners on BrowsingContextWebProgress may depend on state change
      // notifications are otherwise filtered.
      // NOTE: Unlike other nsIWebProgress types, nsBrowserStatusFilter holds a
      // strong cycle-collected reference to the inner listener.
      mBCWebProgressStatusFilter = MakeRefPtr<nsBrowserStatusFilter>(
          /* aDisableStateChangeFilters */ true);
      mBCWebProgressStatusFilter->AddProgressListener(
          innerListener, nsIWebProgress::NOTIFY_ALL);
    }
  }

  return do_AddRef(mBCWebProgressStatusFilter);
}

already_AddRefed<nsIURIFixupInfo> nsDocShell::KeywordToURI(
    const nsACString& aKeyword, bool aIsPrivateContext) {
  nsCOMPtr<nsIURIFixupInfo> info;
  if (!XRE_IsContentProcess()) {
    nsCOMPtr<nsIURIFixup> uriFixup = components::URIFixup::Service();
    if (uriFixup) {
      uriFixup->KeywordToURI(aKeyword, aIsPrivateContext, getter_AddRefs(info));
    }
  }
  return info.forget();
}

/* static */
already_AddRefed<nsIURI> nsDocShell::MaybeFixBadCertDomainErrorURI(
    nsIChannel* aChannel, nsIURI* aUrl) {
  if (!aChannel) {
    return nullptr;
  }

  nsresult rv = NS_OK;
  nsAutoCString host;
  rv = aUrl->GetAsciiHost(host);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  // Return if fixup enable pref is turned off.
  if (!mozilla::StaticPrefs::security_bad_cert_domain_error_url_fix_enabled()) {
    return nullptr;
  }

  // Return if scheme is not HTTPS.
  if (!aUrl->SchemeIs("https")) {
    return nullptr;
  }

  nsCOMPtr<nsILoadInfo> info = aChannel->LoadInfo();
  if (!info) {
    return nullptr;
  }

  // Skip doing the fixup if our channel was redirected, because we
  // shouldn't be guessing things about the post-redirect URI.
  if (!info->RedirectChain().IsEmpty()) {
    return nullptr;
  }

  int32_t port = 0;
  rv = aUrl->GetPort(&port);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  // Don't fix up hosts with ports.
  if (port != -1) {
    return nullptr;
  }

  // Don't fix up localhost url.
  if (host == "localhost") {
    return nullptr;
  }

  // Don't fix up hostnames with IP address.
  if (net_IsValidIPv4Addr(host) || net_IsValidIPv6Addr(host)) {
    return nullptr;
  }

  nsAutoCString userPass;
  rv = aUrl->GetUserPass(userPass);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  // Security - URLs with user / password info should NOT be modified.
  if (!userPass.IsEmpty()) {
    return nullptr;
  }

  nsCOMPtr<nsITransportSecurityInfo> tsi;
  rv = aChannel->GetSecurityInfo(getter_AddRefs(tsi));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  if (NS_WARN_IF(!tsi)) {
    return nullptr;
  }

  nsCOMPtr<nsIX509Cert> cert;
  rv = tsi->GetServerCert(getter_AddRefs(cert));
  if (NS_WARN_IF(NS_FAILED(rv) || !cert)) {
    return nullptr;
  }

  nsTArray<uint8_t> certBytes;
  rv = cert->GetRawDER(certBytes);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  mozilla::pkix::Input serverCertInput;
  mozilla::pkix::Result result =
      serverCertInput.Init(certBytes.Elements(), certBytes.Length());
  if (result != mozilla::pkix::Success) {
    return nullptr;
  }

  constexpr auto wwwPrefix = "www."_ns;
  nsAutoCString newHost;
  if (StringBeginsWith(host, wwwPrefix)) {
    // Try www.example.com -> example.com
    newHost.Assign(Substring(host, wwwPrefix.Length()));
  } else {
    // Try example.com -> www.example.com
    newHost.Assign(wwwPrefix);
    newHost.Append(host);
  }

  mozilla::pkix::Input newHostInput;
  result = newHostInput.Init(
      BitwiseCast<const uint8_t*, const char*>(newHost.BeginReading()),
      newHost.Length());
  if (result != mozilla::pkix::Success) {
    return nullptr;
  }

  // Because certificate verification returned Result::ERROR_BAD_CERT_DOMAIN /
  // SSL_ERROR_BAD_CERT_DOMAIN, a chain was built and we know whether or not
  // the root was a built-in.
  bool rootIsBuiltIn;
  if (NS_FAILED(tsi->GetIsBuiltCertChainRootBuiltInRoot(&rootIsBuiltIn))) {
    return nullptr;
  }
  mozilla::psm::SkipInvalidSANsForNonBuiltInRootsPolicy nameMatchingPolicy(
      rootIsBuiltIn);

  // Check if the certificate is valid for the new hostname.
  result = mozilla::pkix::CheckCertHostname(serverCertInput, newHostInput,
                                            nameMatchingPolicy);
  if (result != mozilla::pkix::Success) {
    return nullptr;
  }

  nsCOMPtr<nsIURI> newURI;
  (void)NS_MutateURI(aUrl).SetHost(newHost).Finalize(getter_AddRefs(newURI));

  return newURI.forget();
}

/* static */
already_AddRefed<nsIURI> nsDocShell::AttemptURIFixup(
    nsIChannel* aChannel, nsresult aStatus,
    const mozilla::Maybe<nsCString>& aOriginalURIString, uint32_t aLoadType,
    bool aIsTopFrame, bool aAllowKeywordFixup, bool aUsePrivateBrowsing,
    bool aNotifyKeywordSearchLoading, nsIInputStream** aNewPostData,
    nsILoadInfo::SchemelessInputType* outSchemelessInput) {
  if (aStatus != NS_ERROR_UNKNOWN_HOST && aStatus != NS_ERROR_NET_RESET &&
      aStatus != NS_ERROR_CONNECTION_REFUSED &&
      aStatus !=
          mozilla::psm::GetXPCOMFromNSSError(SSL_ERROR_BAD_CERT_DOMAIN)) {
    return nullptr;
  }

  if (!(aLoadType == LOAD_NORMAL && aIsTopFrame) && !aAllowKeywordFixup) {
    return nullptr;
  }

  nsCOMPtr<nsIURI> url;
  nsresult rv = aChannel->GetURI(getter_AddRefs(url));
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  //
  // Try and make an alternative URI from the old one
  //
  nsCOMPtr<nsIURI> newURI;
  nsCOMPtr<nsIInputStream> newPostData;

  nsAutoCString oldSpec;
  url->GetSpec(oldSpec);

  //
  // First try keyword fixup
  //
  nsAutoString keywordProviderId, keywordAsSent;
  if (aStatus == NS_ERROR_UNKNOWN_HOST && aAllowKeywordFixup) {
    // we should only perform a keyword search under the following
    // conditions:
    // (0) Pref keyword.enabled is true
    // (1) the url scheme is http (or https)
    // (2) the url does not have a protocol scheme
    // If we don't enforce such a policy, then we end up doing
    // keyword searchs on urls we don't intend like imap, file,
    // mailbox, etc. This could lead to a security problem where we
    // send data to the keyword server that we shouldn't be.
    // Someone needs to clean up keywords in general so we can
    // determine on a per url basis if we want keywords
    // enabled...this is just a bandaid...
    if (Preferences::GetBool("keyword.enabled", false) &&
        net::SchemeIsHttpOrHttps(url)) {
      bool attemptFixup = false;
      nsAutoCString host;
      (void)url->GetHost(host);
      if (host.FindChar('.') == kNotFound) {
        attemptFixup = true;
      } else {
        // For domains with dots, we check the public suffix validity.
        nsCOMPtr<nsIEffectiveTLDService> tldService =
            do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
        if (tldService) {
          nsAutoCString suffix;
          attemptFixup =
              NS_SUCCEEDED(tldService->GetKnownPublicSuffix(url, suffix)) &&
              suffix.IsEmpty();
        }
      }
      if (attemptFixup) {
        nsCOMPtr<nsIURIFixupInfo> info;
        // only send non-qualified hosts to the keyword server
        if (aOriginalURIString && !aOriginalURIString->IsEmpty()) {
          info = KeywordToURI(*aOriginalURIString, aUsePrivateBrowsing);
        } else {
          //
          // If this string was passed through nsStandardURL by
          // chance, then it may have been converted from UTF-8 to
          // Punycode, which would result in a completely bogus keyword
          // query.  Here we try to recover the original Unicode
          // value, but this is not 100% correct since the value may
          // have been normalized per the IDN normalization rules.
          //
          // Since we don't have access to the exact original string
          // that was entered by the user, this will just have to do.
          nsAutoCString utf8Host;
          mozilla_net_recover_keyword_from_punycode(&host, &utf8Host);
          info = KeywordToURI(utf8Host, aUsePrivateBrowsing);
        }
        if (info) {
          info->GetPreferredURI(getter_AddRefs(newURI));
          info->GetSchemelessInput(outSchemelessInput);
          if (newURI) {
            info->GetKeywordAsSent(keywordAsSent);
            info->GetKeywordProviderId(keywordProviderId);
            info->GetPostData(getter_AddRefs(newPostData));
          }
        }
      }
    }
  }

  //
  // Now try change the address, e.g. turn http://foo into
  // http://www.foo.com, and if that doesn't work try https with
  // https://foo and https://www.foo.com.
  //
  if (aStatus == NS_ERROR_UNKNOWN_HOST || aStatus == NS_ERROR_NET_RESET) {
    // Skip fixup for anything except a normal document load
    // operation on the topframe.
    bool doCreateAlternate = aLoadType == LOAD_NORMAL && aIsTopFrame;

    if (doCreateAlternate) {
      nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
      nsIPrincipal* principal = loadInfo->TriggeringPrincipal();
      // Only do this if our channel was loaded directly by the user from the
      // URL bar or similar (system principal) and not redirected, because we
      // shouldn't be guessing things about links from other sites, or a
      // post-redirect URI.
      doCreateAlternate = principal && principal->IsSystemPrincipal() &&
                          loadInfo->RedirectChain().IsEmpty();
    }
    // Test if keyword lookup produced a new URI or not
    if (doCreateAlternate && newURI) {
      bool sameURI = false;
      url->Equals(newURI, &sameURI);
      if (!sameURI) {
        // Keyword lookup made a new URI so no need to try
        // an alternate one.
        doCreateAlternate = false;
      }
    }
    if (doCreateAlternate) {
      newURI = nullptr;
      newPostData = nullptr;
      keywordProviderId.Truncate();
      keywordAsSent.Truncate();
      nsCOMPtr<nsIURIFixup> uriFixup = components::URIFixup::Service();
      if (uriFixup) {
        nsCOMPtr<nsIURIFixupInfo> fixupInfo;
        uriFixup->GetFixupURIInfo(oldSpec, nsIURIFixup::FIXUP_FLAG_NONE,
                                  getter_AddRefs(fixupInfo));
        if (fixupInfo) {
          fixupInfo->GetPreferredURI(getter_AddRefs(newURI));
        }
      }
    }
  } else if (aStatus == NS_ERROR_CONNECTION_REFUSED &&
             Preferences::GetBool("browser.fixup.fallback-to-https", false)) {
    // Try HTTPS, since http didn't work
    if (url->SchemeIs("http")) {
      int32_t port = 0;
      url->GetPort(&port);

      // Fall back to HTTPS only if port is default
      if (port == -1) {
        newURI = nullptr;
        newPostData = nullptr;
        (void)NS_MutateURI(url)
            .SetScheme("https"_ns)
            .Finalize(getter_AddRefs(newURI));
      }
    }
  }

  // If we have a SSL_ERROR_BAD_CERT_DOMAIN error, try adding or removing
  // "www." to/from the beginning of the domain name to see if we can avoid
  // showing the cert error page. For example, https://example.com ->
  // https://www.example.com or https://www.example.com -> https://example.com.
  if (aStatus ==
      mozilla::psm::GetXPCOMFromNSSError(SSL_ERROR_BAD_CERT_DOMAIN)) {
    newPostData = nullptr;
    newURI = MaybeFixBadCertDomainErrorURI(aChannel, url);
  }

  // Did we make a new URI that is different to the old one? If so
  // load it.
  //
  if (newURI) {
    // Make sure the new URI is different from the old one,
    // otherwise there's little point trying to load it again.
    bool sameURI = false;
    url->Equals(newURI, &sameURI);
    if (!sameURI) {
      if (aNewPostData) {
        newPostData.forget(aNewPostData);
      }
      if (aNotifyKeywordSearchLoading) {
        // This notification is meant for Firefox Health Report so it
        // can increment counts from the search engine
        MaybeNotifyKeywordSearchLoading(keywordProviderId, keywordAsSent);
      }
      return newURI.forget();
    }
  }

  return nullptr;
}

nsresult nsDocShell::FilterStatusForErrorPage(
    nsresult aStatus, nsIChannel* aChannel, uint32_t aLoadType,
    bool aIsTopFrame, bool aUseErrorPages,
    bool* aSkippedUnknownProtocolNavigation) {
  // Errors to be shown only on top-level frames
  if ((aStatus == NS_ERROR_UNKNOWN_HOST ||
       aStatus == NS_ERROR_CONNECTION_REFUSED ||
       aStatus == NS_ERROR_UNKNOWN_PROXY_HOST ||
       aStatus == NS_ERROR_PROXY_CONNECTION_REFUSED ||
       aStatus == NS_ERROR_PROXY_FORBIDDEN ||
       aStatus == NS_ERROR_PROXY_NOT_IMPLEMENTED ||
       aStatus == NS_ERROR_PROXY_AUTHENTICATION_FAILED ||
       aStatus == NS_ERROR_PROXY_TOO_MANY_REQUESTS ||
       aStatus == NS_ERROR_MALFORMED_URI ||
       aStatus == NS_ERROR_HARMFULADDON_URI ||
       aStatus == NS_ERROR_BLOCKED_BY_POLICY ||
       aStatus == NS_ERROR_DOM_COOP_FAILED ||
       aStatus == NS_ERROR_DOM_COEP_FAILED ||
       aStatus == NS_ERROR_DOM_INVALID_HEADER_VALUE) &&
      (aIsTopFrame || aUseErrorPages)) {
    return aStatus;
  }

  if (aStatus == NS_ERROR_NET_TIMEOUT ||
      aStatus == NS_ERROR_NET_TIMEOUT_EXTERNAL ||
      aStatus == NS_ERROR_OS_LOCAL_NETWORK_ACCESS_DENIED ||
      aStatus == NS_ERROR_NET_EMPTY_RESPONSE ||
      aStatus == NS_ERROR_NET_ERROR_RESPONSE ||
      aStatus == NS_ERROR_PROXY_GATEWAY_TIMEOUT ||
      aStatus == NS_ERROR_REDIRECT_LOOP ||
      aStatus == NS_ERROR_UNKNOWN_SOCKET_TYPE ||
      aStatus == NS_ERROR_NET_INTERRUPT || aStatus == NS_ERROR_NET_RESET ||
      aStatus == NS_ERROR_PROXY_BAD_GATEWAY || aStatus == NS_ERROR_OFFLINE ||
      aStatus == NS_ERROR_MALWARE_URI || aStatus == NS_ERROR_PHISHING_URI ||
      aStatus == NS_ERROR_UNWANTED_URI || aStatus == NS_ERROR_HARMFUL_URI ||
      aStatus == NS_ERROR_UNSAFE_CONTENT_TYPE ||
      aStatus == NS_ERROR_INTERCEPTION_FAILED ||
      aStatus == NS_ERROR_NET_INADEQUATE_SECURITY ||
      aStatus == NS_ERROR_NET_HTTP2_SENT_GOAWAY ||
      aStatus == NS_ERROR_NET_HTTP3_PROTOCOL_ERROR ||
      aStatus == NS_ERROR_BASIC_HTTP_AUTH_DISABLED ||
      aStatus == NS_ERROR_DOM_BAD_URI || aStatus == NS_ERROR_FILE_NOT_FOUND ||
      aStatus == NS_ERROR_FILE_ACCESS_DENIED ||
      aStatus == NS_ERROR_CORRUPTED_CONTENT ||
      aStatus == NS_ERROR_INVALID_CONTENT_ENCODING ||
      NS_ERROR_GET_MODULE(aStatus) == NS_ERROR_MODULE_SECURITY) {
    // Errors to be shown for any frame
    return aStatus;
  }

  if (aStatus == NS_ERROR_UNKNOWN_PROTOCOL) {
    // For unknown protocols we only display an error if the load is triggered
    // by the browser itself. Showing the error for page-triggered navigations
    // causes annoying behavior for users when a page tries to open an external
    // app which has not been installed, see bug 1528305. A missing WebExtension
    // protocol handlers will however always load the error page, as it is not
    // expected to be opened externally, see bug 1921426.
    nsCOMPtr<nsILoadInfo> info = aChannel->LoadInfo();
    if (!info->TriggeringPrincipal()->IsSystemPrincipal() &&
        !BasePrincipal::Cast(info->TriggeringPrincipal())->AddonPolicy()) {
      if (aSkippedUnknownProtocolNavigation) {
        *aSkippedUnknownProtocolNavigation = true;
      }
      return NS_OK;
    }
    return aStatus;
  }

  if (aStatus == NS_ERROR_DOCUMENT_NOT_CACHED) {
    // Non-caching channels will simply return NS_ERROR_OFFLINE.
    // Caching channels would have to look at their flags to work
    // out which error to return. Or we can fix up the error here.
    if (!(aLoadType & LOAD_CMD_HISTORY)) {
      return NS_ERROR_OFFLINE;
    }
    return aStatus;
  }

  return NS_OK;
}

nsresult nsDocShell::EndPageLoad(nsIWebProgress* aProgress,
                                 nsIChannel* aChannel, nsresult aStatus) {
  MOZ_LOG(gDocShellLeakLog, LogLevel::Debug,
          ("DOCSHELL %p EndPageLoad status: %" PRIx32 "\n", this,
           static_cast<uint32_t>(aStatus)));
  if (!aChannel) {
    return NS_ERROR_NULL_POINTER;
  }

  // Make sure to discard the initial client if we never created the initial
  // about:blank document.  Do this before possibly returning from the method
  // due to an error.
  mInitialClientSource.reset();

  nsCOMPtr<nsIConsoleReportCollector> reporter = do_QueryInterface(aChannel);
  if (reporter) {
    nsCOMPtr<nsILoadGroup> loadGroup;
    aChannel->GetLoadGroup(getter_AddRefs(loadGroup));
    if (loadGroup) {
      reporter->FlushConsoleReports(loadGroup);
    } else {
      reporter->FlushConsoleReports(GetDocument());
    }
  }

  nsCOMPtr<nsIURI> url;
  nsresult rv = aChannel->GetURI(getter_AddRefs(url));
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsITimedChannel> timingChannel = do_QueryInterface(aChannel);
  if (timingChannel) {
    TimeStamp channelCreationTime;
    rv = timingChannel->GetChannelCreation(&channelCreationTime);
    if (NS_SUCCEEDED(rv) && !channelCreationTime.IsNull()) {
      glean::performance_page::total_content_page_load.AccumulateRawDuration(
          TimeStamp::Now() - channelCreationTime);
    }
  }

  // Timing is picked up by the window, we don't need it anymore
  mTiming = nullptr;

  // clean up reload state for meta charset
  if (eCharsetReloadRequested == mCharsetReloadState) {
    mCharsetReloadState = eCharsetReloadStopOrigional;
  } else {
    mCharsetReloadState = eCharsetReloadInit;
  }

  //
  // one of many safeguards that prevent death and destruction if
  // someone is so very very rude as to bring this window down
  // during this load handler.
  //
  nsCOMPtr<nsIDocShell> kungFuDeathGrip(this);

  // Notify the DocumentViewer that the Document has finished loading.  This
  // will cause any OnLoad(...) and PopState(...) handlers to fire.
  if (!mEODForCurrentDocument && mDocumentViewer) {
    mIsExecutingOnLoadHandler = true;
    nsCOMPtr<nsIDocumentViewer> viewer = mDocumentViewer;
    viewer->LoadComplete(aStatus);
    mIsExecutingOnLoadHandler = false;

    mEODForCurrentDocument = true;
  }
  /* Check if the httpChannel has any cache-control related response headers,
   * like no-store, no-cache. If so, update SHEntry so that
   * when a user goes back/forward to this page, we appropriately do
   * form value restoration or load from server.
   */
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aChannel));
  if (!httpChannel) {
    // HttpChannel could be hiding underneath a Multipart channel.
    GetHttpChannel(aChannel, getter_AddRefs(httpChannel));
  }

  mActiveEntryIsLoadingFromSessionHistory = false;

  // if there's a refresh header in the channel, this method
  // will set it up for us.
  if (mBrowsingContext->IsActive() || !mDisableMetaRefreshWhenInactive)
    RefreshURIFromQueue();

  // Test whether this is the top frame or a subframe
  bool isTopFrame = mBrowsingContext->IsTop();

  bool hadErrorStatus = false;
  // If status code indicates an error it means that DocumentChannel already
  // tried to fixup the uri and failed. Throw an error dialog box here.
  if (NS_FAILED(aStatus)) {
    // If we got CONTENT_BLOCKED from EndPageLoad, then we need to fire
    // the error event to our embedder, since tests are relying on this.
    // The error event is usually fired by the caller of InternalLoad, but
    // this particular error can happen asynchronously.
    // Bug 1629201 is filed for having much clearer decision making around
    // which cases need error events.
    bool fireFrameErrorEvent = (aStatus == NS_ERROR_CONTENT_BLOCKED_SHOW_ALT ||
                                aStatus == NS_ERROR_CONTENT_BLOCKED);
    UnblockEmbedderLoadEventForFailure(fireFrameErrorEvent);

    bool skippedUnknownProtocolNavigation = false;
    aStatus = FilterStatusForErrorPage(aStatus, aChannel, mLoadType, isTopFrame,
                                       mBrowsingContext->GetUseErrorPages(),
                                       &skippedUnknownProtocolNavigation);
    hadErrorStatus = true;
    if (NS_FAILED(aStatus)) {
      if (!mIsBeingDestroyed) {
        DisplayLoadError(aStatus, url, nullptr, aChannel);
      }
    } else if (skippedUnknownProtocolNavigation) {
      nsAutoCString sanitized;
      nsTArray<nsString> params;
      if (NS_SUCCEEDED(NS_GetSanitizedURIStringFromURI(url, sanitized))) {
        params.AppendElement(NS_ConvertUTF8toUTF16(sanitized));
      } else {
        params.AppendElement(u"(unknown uri)"_ns);
      }
      nsContentUtils::ReportToConsole(
          nsIScriptError::warningFlag, "DOM"_ns, GetExtantDocument(),
          PropertiesFile::DOM_PROPERTIES, "UnknownProtocolNavigationPrevented",
          params);
    }
  }

  if (hadErrorStatus) {
    // Don't send session store updates if the reason EndPageLoad was called is
    // because we are process switching. Sometimes the update takes too long and
    // incorrectly overrides session store data from the following load.
    return NS_OK;
  }
  if (SessionStorePlatformCollection()) {
    if (WindowContext* windowContext =
            mBrowsingContext->GetCurrentWindowContext()) {
      using Change = SessionStoreChangeListener::Change;

      // We've finished loading the page and now we want to collect all the
      // session store state that the page is initialized with.
      SessionStoreChangeListener::CollectSessionStoreData(
          windowContext,
          EnumSet<Change>(Change::Input, Change::Scroll, Change::SessionHistory,
                          Change::WireFrame));
    }
  }

  return NS_OK;
}

//*****************************************************************************
// nsDocShell: Content Viewer Management
//*****************************************************************************

bool nsDocShell::VerifyDocumentViewer() {
  if (mDocumentViewer) {
    return true;
  }
  if (mIsBeingDestroyed) {
    return false;
  }
  if (!mInitialized) {
    // The viewer should be created during docshell initialization. If something
    // wants a viewer or document, it has to initialize the docshell first.
    MOZ_ASSERT_UNREACHABLE(
        "The docshell should be initialized to get a viewer.");
  } else {
    NS_WARNING("No document viewer, docshell failed to initialize.");
  }
  return false;
}

nsresult nsDocShell::CreateInitialDocumentViewer(
    nsIOpenWindowInfo* aOpenWindowInfo,
    mozilla::dom::WindowGlobalChild* aWindowActor) {
  if (mIsBeingDestroyed) {
    return NS_ERROR_FAILURE;
  }
  MOZ_DIAGNOSTIC_ASSERT(!mDocumentViewer);
  MOZ_ASSERT(aOpenWindowInfo, "Why don't we have openwindowinfo?");

  nsCOMPtr<nsIPrincipal> principal =
      aOpenWindowInfo->PrincipalToInheritForAboutBlank();
  nsCOMPtr<nsIPrincipal> partitionedPrincipal =
      aOpenWindowInfo->PartitionedPrincipalToInheritForAboutBlank();

  // Previously, CreateDocumentViewerForActor would've used the actor's
  // principal.
  MOZ_ASSERT_IF(aWindowActor, aWindowActor->DocumentPrincipal() == principal);
  MOZ_ASSERT_IF(aWindowActor,
                aWindowActor->DocumentPrincipal() == partitionedPrincipal);

  nsCOMPtr<nsIPolicyContainer> policyContainer =
      aOpenWindowInfo->PolicyContainerToInheritForAboutBlank();
  nsCOMPtr<nsIURI> base = aOpenWindowInfo->BaseUriToInheritForAboutBlank();
  MOZ_TRY(CreateAboutBlankDocumentViewer(
      principal, partitionedPrincipal, policyContainer, base,
      /* aIsInitialDocument */ true,
      aOpenWindowInfo->CoepToInheritForAboutBlank(),
      /* aTryToSaveOldPresentation */ true,
      /* aCheckPermitUnload */ false, aWindowActor));

  NS_ENSURE_STATE(mDocumentViewer);

  RefPtr<Document> doc(GetDocument());
  MOZ_ASSERT(doc,
             "Should have doc if CreateAboutBlankDocumentViewer succeeded!");
  MOZ_ASSERT(doc->IsInitialDocument(), "Document should be initial document");

  // Documents created using CreateInitialDocumentViewer may be transient
  // placeholders created by framescripts before content has a
  // chance to load. In some cases, window.open(..., "noopener")
  // will create such a document and then synchronously tear it
  // down, firing a "pagehide" event. Doing so violates our
  // assertions about DocGroups. It's easier to silence the
  // assertion here than to avoid creating the extra document.
  doc->IgnoreDocGroupMismatches();

  return NS_OK;
}

// Location.ancestorOrigins for about:blank, need special case handling.
// Particularly in the case for initial about:blank, which does not go through
// normal code that happen with navigations.
static void CreateAboutBlankAncestorOriginsForNonTopLevel(Document* aDoc) {
  BrowsingContext* bc = aDoc->GetBrowsingContext();
  MOZ_ASSERT(bc && !bc->IsDiscarded() && bc->GetEmbedderElement());
  // We're not even going to attempt to deal with location.ancestorOrigins stuff
  // in the parent process.
  if (!XRE_IsContentProcess()) {
    return;
  }

  const auto* frame = bc->GetEmbedderElement();
  const auto referrerPolicy = frame->GetReferrerPolicyAsEnum();
  // Inform the parent process that it needs to create an internal ancestor
  // origins list for this browsing context `bc`
  (void)ContentChild::GetSingleton()->SendUpdateAncestorOriginsList(bc);

  const bool masked = referrerPolicy == ReferrerPolicy::No_referrer;
  BrowsingContext* parent = bc->GetParent();
  MOZ_DIAGNOSTIC_ASSERT(parent && parent->IsInProcess() &&
                        parent->GetExtantDocument());

  nsTArray<nsCOMPtr<nsIPrincipal>> ancestorPrincipals;
  constexpr auto getPrincipal =
      [](const BrowsingContext* ctx) -> nsIPrincipal* {
    if (!ctx) {
      return nullptr;
    }
    auto* doc = ctx->GetExtantDocument();
    return doc ? doc->GetPrincipal() : nullptr;
  };
  BrowsingContext* ancestorContextToCopyAncestorListFrom = parent;

  // about:blank is different from normal docs.
  // We only care about in-process, same-origin ancestors.
  // Therefore run the algorithm all the way up to the last same-origin doc
  // add that origin to the list, and then append that origin's ancestor origins
  // list
  if (masked) {
    ancestorPrincipals.AppendElement(nullptr);
    // 16.1.1. If ancestorOrigin is same origin with parentDoc's origin, then
    // append a new opaque origin to output.
    auto* parentDocPrincipal = getPrincipal(parent);
    for (auto* ancestor = parent->GetParent(); ancestor;
         ancestor = ancestor->GetParent()) {
      auto* principal = getPrincipal(ancestor);
      if (principal && principal->Equals(parentDocPrincipal)) {
        // same principal, same process, adding nullptr for
        // parentContextToCopyAncestorListFrom
        ancestorContextToCopyAncestorListFrom = ancestor;
        ancestorPrincipals.AppendElement(nullptr);
      } else {
        // 16.1.2 Otherwise, append ancestorOrigin to output and set masked to
        // false.
        // Note: But in the case of about:blank, ancestorOrigin can
        // potentially live in another process. So we stop right before it, and
        // just copy `ancestorContextToCopyAncestorListFrom` list, since the
        // masking steps should finish here.
        break;
      }
    }
  } else {
    ancestorPrincipals.AppendElement(getPrincipal(parent));
  }

  nsTArray<nsString> list = ProduceAncestorOriginsList(ancestorPrincipals);
  Document* ancestorDoc =
      ancestorContextToCopyAncestorListFrom->GetExtantDocument();
  MOZ_DIAGNOSTIC_ASSERT(ancestorDoc);
  list.AppendElements(ancestorDoc->GetAncestorOriginsList());
  aDoc->SetAncestorOriginsList(std::move(list));
}

nsresult nsDocShell::CreateAboutBlankDocumentViewer(
    nsIPrincipal* aPrincipal, nsIPrincipal* aPartitionedPrincipal,
    nsIPolicyContainer* aPolicyContainer, nsIURI* aBaseURI,
    bool aIsInitialDocument,
    const Maybe<nsILoadInfo::CrossOriginEmbedderPolicy>& aCOEP,
    bool aTryToSaveOldPresentation, bool aCheckPermitUnload,
    WindowGlobalChild* aActor) {
  RefPtr<Document> blankDoc;
  nsCOMPtr<nsIDocumentViewer> viewer;
  nsresult rv = NS_ERROR_FAILURE;

  PROFILER_MARKER_UNTYPED("CreateAboutBlankDocumentViewer", DOM,
                          MarkerStack::Capture());

  MOZ_ASSERT_IF(aActor, aActor->DocumentPrincipal() == aPrincipal);

  MOZ_DIAGNOSTIC_ASSERT(mInitialized, "Must initialize before viewer creation");

  /* mCreatingDocument should never be true at this point. However, it's
     a theoretical possibility. We want to know about it and make it stop,
     and this sounds like a job for an assertion. */
  NS_ASSERTION(!mCreatingDocument,
               "infinite(?) loop creating document averted");
  if (mCreatingDocument) {
    return NS_ERROR_FAILURE;
  }

  if (!mBrowsingContext->AncestorsAreCurrent() ||
      mBrowsingContext->IsInBFCache()) {
    mBrowsingContext->RemoveRootFromBFCacheSync();
    return NS_ERROR_NOT_AVAILABLE;
  }

  // mDocumentViewer->PermitUnload may release |this| docshell.
  nsCOMPtr<nsIDocShell> kungFuDeathGrip(this);

  // Ensure that UsesOriginAgentCluster has been initialized for this
  // BrowsingContextGroup/principal pair before creating the document.
  if (aPrincipal) {
    mBrowsingContext->Group()->EnsureUsesOriginAgentClusterInitialized(
        aPrincipal);
  }

  AutoRestore<bool> creatingDocument(mCreatingDocument);
  mCreatingDocument = true;

  if (aPrincipal && !aPrincipal->IsSystemPrincipal() &&
      mItemType != typeChrome) {
    if (GetIsTopLevelContentDocShell()) {
      // Bug 1948216 tracks having a FPD for top-level initial about:blank
      MOZ_ASSERT(aPrincipal->OriginAttributesRef().EqualsIgnoringFPD(
          mBrowsingContext->OriginAttributesRef()));
    } else {
      MOZ_ASSERT(aPrincipal->OriginAttributesRef() ==
                 mBrowsingContext->OriginAttributesRef());
    }
  }

  // Make sure timing is created.  But first record whether we had it
  // already, so we don't clobber the timing for an in-progress load.
  bool hadTiming = mTiming;
  bool toBeReset = MaybeInitTiming();
  if (mDocumentViewer) {
    if (aCheckPermitUnload) {
      // We've got a content viewer already. Make sure the user
      // permits us to discard the current document and replace it
      // with about:blank. And also ensure we fire the unload events
      // in the current document.

      // Unload gets fired first for
      // document loaded from the session history.
      mTiming->NotifyBeforeUnload();

      bool okToUnload;
      rv = mDocumentViewer->PermitUnload(&okToUnload);
      if (mIsBeingDestroyed) {
        // unload handler destroyed this docshell.
        return NS_ERROR_NOT_AVAILABLE;
      }
      if (NS_SUCCEEDED(rv) && !okToUnload) {
        // The user chose not to unload the page, interrupt the load.
        MaybeResetInitTiming(toBeReset);
        return NS_ERROR_FAILURE;
      }
      if (mTiming) {
        mTiming->NotifyUnloadAccepted(mCurrentURI);
      }
    }

    // Make sure to blow away our mLoadingURI just in case.  No loads
    // from inside this pagehide.
    mLoadingURI = nullptr;

    // Stop any in-progress loading, so that we don't accidentally trigger any
    // PageShow notifications from Embed() interrupting our loading below.
    Stop();

    // Notify the current document that it is about to be unloaded!!
    //
    // It is important to fire the unload() notification *before* any state
    // is changed within the DocShell - otherwise, javascript will get the
    // wrong information :-(
    //
    (void)FirePageHideNotification();
    // pagehide notification might destroy this docshell.
    if (mIsBeingDestroyed) {
      return NS_ERROR_DOCSHELL_DYING;
    }
  }

  // Now make sure we don't think we're in the middle of firing unload after
  // this point.  This will make us fire unload when the about:blank document
  // unloads... but that's ok, more or less.  Would be nice if it fired load
  // too, of course.
  mFiredUnloadEvent = false;

  nsCOMPtr<nsIDocumentLoaderFactory> docFactory =
      nsContentUtils::FindInternalDocumentViewer("text/html"_ns);

  if (docFactory) {
    nsCOMPtr<nsIPrincipal> principal, partitionedPrincipal;
    const uint32_t sandboxFlags =
        mBrowsingContext->GetHasLoadedNonInitialDocument()
            ? mBrowsingContext->GetSandboxFlags()
            : mBrowsingContext->GetInitialSandboxFlags();
    // If we're sandboxed, then create a new null principal. We skip
    // this if we're being created from WindowGlobalChild, since in
    // that case we already have a null principal if required.
    // We can't compare againt the BrowsingContext sandbox flag, since
    // the value was taken when the load initiated and may have since
    // changed.
    if ((sandboxFlags & SANDBOXED_ORIGIN) && !aActor) {
      if (aPrincipal) {
        principal = NullPrincipal::CreateWithInheritedAttributes(aPrincipal);
      } else {
        principal = NullPrincipal::Create(GetOriginAttributes());
      }
      partitionedPrincipal = principal;
    } else {
      principal = aPrincipal;
      partitionedPrincipal = aPartitionedPrincipal;
    }

    // We cannot get the foreign partitioned principal for the initial
    // about:blank page. So, we change to check if we need to use the
    // partitioned principal for the service worker here.
    MaybeCreateInitialClientSource(
        StoragePrincipalHelper::ShouldUsePartitionPrincipalForServiceWorker(
            this)
            ? partitionedPrincipal
            : principal);

    // generate (about:blank) document to load
    blankDoc = nsContentDLF::CreateBlankDocument(mLoadGroup, principal,
                                                 partitionedPrincipal, this);
    if (blankDoc) {
      // Hack: manually set the policyContainer for the new document
      // Please create an actual copy of the policyContainer (do not share the
      // same reference) otherwise modifying the new container (such as
      // appending a new policy to CSP) within the new document will be
      // incorrectly propagated to the opening doc.
      if (aPolicyContainer) {
        RefPtr policyContainerToInherit = MakeRefPtr<PolicyContainer>();
        policyContainerToInherit->InitFromOther(
            PolicyContainer::Cast(aPolicyContainer));
        blankDoc->SetPolicyContainer(policyContainerToInherit);
        if (!PolicyContainer::GetCSP(policyContainerToInherit)) {
          RefPtr newCsp = MakeRefPtr<nsCSPContext>();
          policyContainerToInherit->SetCSP(newCsp);
          nsresult rv = newCsp->SetRequestContextWithDocument(blankDoc);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return rv;
          }
        }
      }

      blankDoc->SetInitialStatus(
          aIsInitialDocument ? Document::InitialStatus::IsInitialUncommitted
                             : Document::InitialStatus::NeverInitial);

      blankDoc->SetEmbedderPolicy(aCOEP);

      // Hack: set the base URI manually, since this document never
      // got Reset() with a channel.
      blankDoc->SetBaseURI(aBaseURI);

      // Copy our sandbox flags to the document. These are immutable
      // after being set here.
      blankDoc->SetSandboxFlags(sandboxFlags);

      // We inherit the classification flags from the parent document if the
      // principal matches.
      nsCOMPtr<nsIDocShellTreeItem> parentItem;
      GetInProcessSameTypeParent(getter_AddRefs(parentItem));
      if (parentItem) {
        RefPtr<Document> parentDocument = parentItem->GetDocument();
        if (parentDocument && principal &&
            principal->Equals(parentDocument->NodePrincipal())) {
          blankDoc->SetClassificationFlags(
              parentDocument->GetClassificationFlags());
        }
      }

      // create a content viewer for us and the new document
      docFactory->CreateInstanceForDocument(
          NS_ISUPPORTS_CAST(nsIDocShell*, this), blankDoc, "view",
          getter_AddRefs(viewer));

      // hook 'em up
      if (viewer) {
        viewer->SetContainer(this);
        if (mLoadingEntry && mBrowsingContext->IsTop()) {
          mLoadingEntry->mInfo.SetTransient();
        }
        rv = Embed(viewer, aActor, true, nullptr, mCurrentURI);
        NS_ENSURE_SUCCESS(rv, rv);

        SetCurrentURI(blankDoc->GetDocumentURI(), nullptr,
                      /* aFireLocationChange */ true,
                      /* aIsInitialAboutBlank */ aIsInitialDocument,
                      /* aLocationFlags */ 0);
        rv = mIsBeingDestroyed ? NS_ERROR_NOT_AVAILABLE : NS_OK;
      }

      if (Element* embedderElement = blankDoc->GetEmbedderElement()) {
        blankDoc->InitFeaturePolicy(AsVariant(embedderElement));
      } else {
        blankDoc->InitFeaturePolicy(AsVariant(Nothing{}));
      }

      // Perform redacted location.ancestorOrigins algorithm for about:blank
      if (BrowsingContext* bc = GetBrowsingContext();
          bc && bc->GetEmbedderElement()) {
        CreateAboutBlankAncestorOriginsForNonTopLevel(blankDoc);
      }
    }
  }

  // Clear out our mTiming like we would in EndPageLoad, if we didn't
  // have one before entering this function.
  if (!hadTiming) {
    mTiming = nullptr;
    mBlankTiming = true;
  }

  return rv;
}

NS_IMETHODIMP
nsDocShell::CreateAboutBlankDocumentViewer(
    nsIPrincipal* aPrincipal, nsIPrincipal* aPartitionedPrincipal,
    nsIPolicyContainer* aPolicyContainer) {
  return CreateAboutBlankDocumentViewer(aPrincipal, aPartitionedPrincipal,
                                        aPolicyContainer, nullptr,
                                        /* aIsInitialDocument */ false);
}

/* static */
void nsDocShell::ReportBFCacheComboTelemetry(uint32_t aCombo) {
  // There are 11 possible reasons to make a request fails to use BFCache
  // (see BFCacheStatus in dom/base/Document.h), and we'd like to record
  // the common combinations for reasons which make requests fail to use
  // BFCache. These combinations are generated based on some local browsings,
  // we need to adjust them when necessary.
  enum BFCacheStatusCombo : uint32_t {
    BFCACHE_SUCCESS,
    NOT_ONLY_TOPLEVEL = mozilla::dom::BFCacheStatus::NOT_ONLY_TOPLEVEL_IN_BCG,
    // If both unload and beforeunload listeners are presented, it'll be
    // recorded as unload
    UNLOAD = mozilla::dom::BFCacheStatus::UNLOAD_LISTENER,
    UNLOAD_REQUEST = mozilla::dom::BFCacheStatus::UNLOAD_LISTENER |
                     mozilla::dom::BFCacheStatus::REQUEST,
    REQUEST = mozilla::dom::BFCacheStatus::REQUEST,
    UNLOAD_REQUEST_PEER = mozilla::dom::BFCacheStatus::UNLOAD_LISTENER |
                          mozilla::dom::BFCacheStatus::REQUEST |
                          mozilla::dom::BFCacheStatus::ACTIVE_PEER_CONNECTION,
    UNLOAD_REQUEST_PEER_MSE =
        mozilla::dom::BFCacheStatus::UNLOAD_LISTENER |
        mozilla::dom::BFCacheStatus::REQUEST |
        mozilla::dom::BFCacheStatus::ACTIVE_PEER_CONNECTION |
        mozilla::dom::BFCacheStatus::CONTAINS_MSE_CONTENT,
    UNLOAD_REQUEST_MSE = mozilla::dom::BFCacheStatus::UNLOAD_LISTENER |
                         mozilla::dom::BFCacheStatus::REQUEST |
                         mozilla::dom::BFCacheStatus::CONTAINS_MSE_CONTENT,
    SUSPENDED_UNLOAD_REQUEST_PEER =
        mozilla::dom::BFCacheStatus::SUSPENDED |
        mozilla::dom::BFCacheStatus::UNLOAD_LISTENER |
        mozilla::dom::BFCacheStatus::REQUEST |
        mozilla::dom::BFCacheStatus::ACTIVE_PEER_CONNECTION,
    REMOTE_SUBFRAMES = mozilla::dom::BFCacheStatus::CONTAINS_REMOTE_SUBFRAMES,
    BEFOREUNLOAD = mozilla::dom::BFCacheStatus::BEFOREUNLOAD_LISTENER,
  };

  // Beforeunload is recorded as a blocker only if it is the only one to block
  // bfcache.
  if (aCombo != mozilla::dom::BFCacheStatus::BEFOREUNLOAD_LISTENER) {
    aCombo &= ~mozilla::dom::BFCacheStatus::BEFOREUNLOAD_LISTENER;
  }
  switch (aCombo) {
    case BFCACHE_SUCCESS:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eBfcacheSuccess)
          .Add();
      break;
    case NOT_ONLY_TOPLEVEL:
      if (StaticPrefs::docshell_shistory_bfcache_require_no_opener()) {
        glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eOther).Add();
        break;
      }
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eBfcacheSuccess)
          .Add();
      glean::bfcache::combo
          .EnumGet(glean::bfcache::ComboLabel::eSuccessNotToplevel)
          .Add();
      break;
    case UNLOAD:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eUnload).Add();
      break;
    case BEFOREUNLOAD:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eBeforeunload)
          .Add();
      break;
    case UNLOAD_REQUEST:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eUnloadReq)
          .Add();
      break;
    case REQUEST:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eReq).Add();
      break;
    case UNLOAD_REQUEST_PEER:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eUnloadReqPeer)
          .Add();
      break;
    case UNLOAD_REQUEST_PEER_MSE:
      glean::bfcache::combo
          .EnumGet(glean::bfcache::ComboLabel::eUnloadReqPeerMse)
          .Add();
      break;
    case UNLOAD_REQUEST_MSE:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eUnloadReqMse)
          .Add();
      break;
    case SUSPENDED_UNLOAD_REQUEST_PEER:
      glean::bfcache::combo
          .EnumGet(glean::bfcache::ComboLabel::eSpdUnloadReqPeer)
          .Add();
      break;
    case REMOTE_SUBFRAMES:
      glean::bfcache::combo
          .EnumGet(glean::bfcache::ComboLabel::eRemoteSubframes)
          .Add();
      break;
    default:
      glean::bfcache::combo.EnumGet(glean::bfcache::ComboLabel::eOther).Add();
      break;
  }
};

void nsDocShell::DetachEditorFromWindow() {
  if (!mEditorData || mEditorData->WaitingForLoad()) {
    // If there's nothing to detach, or if the editor data is actually set
    // up for the _new_ page that's coming in, don't detach.
    return;
  }

  nsresult res = mEditorData->DetachFromWindow();
  NS_ASSERTION(NS_SUCCEEDED(res), "Failed to detach editor");

  if (NS_SUCCEEDED(res)) {
    mEditorData = nullptr;
  }

#ifdef DEBUG
  {
    bool isEditable;
    GetEditable(&isEditable);
    NS_ASSERTION(!isEditable,
                 "Window is still editable after detaching editor.");
  }
#endif  // DEBUG
}

NS_IMETHODIMP
nsDocShell::GetRestoringDocument(bool* aRestoring) {
  *aRestoring = mIsRestoringDocument;
  return NS_OK;
}

bool nsDocShell::SandboxFlagsImplyCookies(const uint32_t& aSandboxFlags) {
  return (aSandboxFlags & (SANDBOXED_ORIGIN | SANDBOXED_SCRIPTS)) == 0;
}

nsresult nsDocShell::CreateDocumentViewer(const nsACString& aContentType,
                                          nsIRequest* aRequest,
                                          nsIStreamListener** aContentHandler) {
  *aContentHandler = nullptr;

  if (!mTreeOwner || mIsBeingDestroyed) {
    // If we don't have a tree owner, then we're in the process of being
    // destroyed. Rather than continue trying to load something, just give up.
    return NS_ERROR_DOCSHELL_DYING;
  }

  if (!mBrowsingContext->AncestorsAreCurrent() ||
      mBrowsingContext->IsInBFCache()) {
    mBrowsingContext->RemoveRootFromBFCacheSync();
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Can we check the content type of the current content viewer
  // and reuse it without destroying it and re-creating it?

  NS_ASSERTION(mLoadGroup, "Someone ignored return from Init()?");

  // Instantiate the content viewer object
  nsCOMPtr<nsIDocumentViewer> viewer;
  nsresult rv = NewDocumentViewerObj(aContentType, aRequest, mLoadGroup,
                                     aContentHandler, getter_AddRefs(viewer));

  if (NS_FAILED(rv)) {
    return rv;
  }

  // Notify the current document that it is about to be unloaded!!
  //
  // It is important to fire the unload() notification *before* any state
  // is changed within the DocShell - otherwise, javascript will get the
  // wrong information :-(
  //

  NS_ASSERTION(!mLoadingURI, "Re-entering unload?");

  nsCOMPtr<nsIChannel> aOpenedChannel = do_QueryInterface(aRequest);
  if (aOpenedChannel) {
    aOpenedChannel->GetURI(getter_AddRefs(mLoadingURI));
  }

  // Grab the current URI, we need to pass it to Embed, and OnNewURI will reset
  // it before we do call Embed.
  nsCOMPtr<nsIURI> previousURI = mCurrentURI;

  FirePageHideNotification();
  if (mIsBeingDestroyed) {
    // Force to stop the newly created orphaned viewer.
    viewer->Stop();
    return NS_ERROR_DOCSHELL_DYING;
  }
  mLoadingURI = nullptr;

  // Set mFiredUnloadEvent = false so that the unload handler for the
  // *new* document will fire.
  mFiredUnloadEvent = false;

  // we've created a new document so go ahead and call
  // OnNewURI(), but don't fire OnLocationChange()
  // notifications before we've called Embed(). See bug 284993.
  mURIResultedInDocument = true;
  bool errorOnLocationChangeNeeded = false;
  nsCOMPtr<nsIChannel> failedChannel = mFailedChannel;
  nsCOMPtr<nsIURI> failedURI;

  // https://html.spec.whatwg.org/#finalize-a-cross-document-navigation
  // 9. If entryToReplace is null, then: ...
  //    Otherwise: ...
  //      4. If historyEntry's document state's origin is same origin with
  //         entryToReplace's document state's origin, then set
  //         historyEntry's navigation API key to entryToReplace's
  //         navigation API key.
  bool isReplace =
      mActiveEntry && mLoadingEntry &&
      mLoadingEntry->mTriggeringNavigationType
          .map([](auto type) { return type == NavigationType::Replace; })
          .valueOr(false);
  if (isReplace) {
    nsCOMPtr<nsIURI> uri = mActiveEntry->GetURIOrInheritedForAboutBlank();
    nsCOMPtr<nsIURI> targetURI =
        mLoadingEntry->mInfo.GetURIOrInheritedForAboutBlank();
    bool sameOrigin =
        NS_SUCCEEDED(nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
            targetURI, uri, false, false));
    if (sameOrigin) {
      mLoadingEntry->mInfo.NavigationKey() = mActiveEntry->NavigationKey();
    }
  }

  if (mLoadType == LOAD_ERROR_PAGE) {
    // We need to set the SH entry and our current URI here and not
    // at the moment we load the page. We want the same behavior
    // of Stop() as for a normal page load. See bug 514232 for details.

    // Revert mLoadType to load type to state the page load failed,
    // following function calls need it.
    mLoadType = mFailedLoadType;

    Document* doc = viewer->GetDocument();
    if (doc) {
      doc->SetFailedChannel(failedChannel);
    }

    nsCOMPtr<nsIPrincipal> triggeringPrincipal;
    if (failedChannel) {
      // Make sure we have a URI to set currentURI.
      NS_GetFinalChannelURI(failedChannel, getter_AddRefs(failedURI));
    } else {
      // if there is no failed channel we have to explicitly provide
      // a triggeringPrincipal for the history entry.
      triggeringPrincipal = nsContentUtils::GetSystemPrincipal();
    }

    if (!failedURI) {
      failedURI = mFailedURI;
    }
    if (!failedURI) {
      // We need a URI object to store a session history entry, so make up a URI
      NS_NewURI(getter_AddRefs(failedURI), "about:blank");
    }

    // When we don't have failedURI, something wrong will happen. See
    // bug 291876.
    MOZ_ASSERT(failedURI, "We don't have a URI for history APIs.");

    mFailedChannel = nullptr;
    mFailedURI = nullptr;

    // Create an shistory entry for the old load.
    if (failedURI) {
      errorOnLocationChangeNeeded =
          OnNewURI(failedURI, failedChannel, triggeringPrincipal, nullptr,
                   nullptr, nullptr, false, false);
    }

    mLoadType = LOAD_ERROR_PAGE;
  }

  nsCOMPtr<nsIURI> finalURI;
  // If this a redirect, use the final url (uri)
  // else use the original url
  //
  // Note that this should match what documents do (see Document::Reset).
  NS_GetFinalChannelURI(aOpenedChannel, getter_AddRefs(finalURI));

  bool onLocationChangeNeeded = false;
  if (finalURI) {
    // Pass false for aCloneSHChildren, since we're loading a new page here.
    onLocationChangeNeeded = OnNewURI(finalURI, aOpenedChannel, nullptr,
                                      nullptr, nullptr, nullptr, true, false);
  }

  // We inherit the classification flags from the parent document if the
  // document is about:blank and the principal matches.
  nsCOMPtr<nsIDocShellTreeItem> parentItem;
  GetInProcessSameTypeParent(getter_AddRefs(parentItem));
  if (parentItem && finalURI && NS_IsAboutBlank(finalURI)) {
    RefPtr<Document> doc = viewer->GetDocument();
    RefPtr<Document> parentDocument = parentItem->GetDocument();
    if (parentDocument && doc &&
        doc->NodePrincipal()->Equals(parentDocument->NodePrincipal())) {
      doc->SetClassificationFlags(parentDocument->GetClassificationFlags());
    }
  }

  // let's try resetting the load group if we need to...
  nsCOMPtr<nsILoadGroup> currentLoadGroup;
  NS_ENSURE_SUCCESS(
      aOpenedChannel->GetLoadGroup(getter_AddRefs(currentLoadGroup)),
      NS_ERROR_FAILURE);

  if (currentLoadGroup != mLoadGroup) {
    nsLoadFlags loadFlags = 0;

    // Cancel any URIs that are currently loading...
    // XXX: Need to do this eventually      Stop();
    //
    // Retarget the document to this loadgroup...
    //
    /* First attach the channel to the right loadgroup
     * and then remove from the old loadgroup. This
     * puts the notifications in the right order and
     * we don't null-out mLSHE in OnStateChange() for
     * all redirected urls
     */
    aOpenedChannel->SetLoadGroup(mLoadGroup);

    // Mark the channel as being a document URI...
    aOpenedChannel->GetLoadFlags(&loadFlags);
    loadFlags |= nsIChannel::LOAD_DOCUMENT_URI;
    nsCOMPtr<nsILoadInfo> loadInfo = aOpenedChannel->LoadInfo();
    if (SandboxFlagsImplyCookies(loadInfo->GetSandboxFlags())) {
      loadFlags |= nsIRequest::LOAD_DOCUMENT_NEEDS_COOKIE;
    }

    aOpenedChannel->SetLoadFlags(loadFlags);

    mLoadGroup->AddRequest(aRequest, nullptr);
    if (currentLoadGroup) {
      currentLoadGroup->RemoveRequest(aRequest, nullptr, NS_BINDING_RETARGETED);
    }

    // Update the notification callbacks, so that progress and
    // status information are sent to the right docshell...
    aOpenedChannel->SetNotificationCallbacks(this);
  }

  NS_ENSURE_SUCCESS(Embed(viewer, nullptr, false, aOpenedChannel, previousURI),
                    NS_ERROR_FAILURE);

  if (!mBrowsingContext->GetHasLoadedNonInitialDocument()) {
    MOZ_ALWAYS_SUCCEEDS(mBrowsingContext->SetHasLoadedNonInitialDocument(true));
  }

  mSavedRefreshURIList = nullptr;
  mEODForCurrentDocument = false;

  // if this document is part of a multipart document,
  // the ID can be used to distinguish it from the other parts.
  nsCOMPtr<nsIMultiPartChannel> multiPartChannel(do_QueryInterface(aRequest));
  if (multiPartChannel) {
    if (PresShell* presShell = GetPresShell()) {
      if (Document* doc = presShell->GetDocument()) {
        uint32_t partID;
        multiPartChannel->GetPartID(&partID);
        doc->SetPartID(partID);
      }
    }
  }

  if (errorOnLocationChangeNeeded) {
    FireOnLocationChange(this, failedChannel, failedURI,
                         LOCATION_CHANGE_ERROR_PAGE);
  } else if (onLocationChangeNeeded) {
    uint32_t locationFlags =
        (mLoadType & LOAD_CMD_RELOAD) ? uint32_t(LOCATION_CHANGE_RELOAD) : 0;
    FireOnLocationChange(this, aRequest, mCurrentURI, locationFlags);
  }

  return NS_OK;
}

nsresult nsDocShell::NewDocumentViewerObj(const nsACString& aContentType,
                                          nsIRequest* aRequest,
                                          nsILoadGroup* aLoadGroup,
                                          nsIStreamListener** aContentHandler,
                                          nsIDocumentViewer** aViewer) {
  nsCOMPtr<nsIChannel> aOpenedChannel = do_QueryInterface(aRequest);

  nsCOMPtr<nsIDocumentLoaderFactory> docLoaderFactory =
      nsContentUtils::FindInternalDocumentViewer(aContentType);
  if (!docLoaderFactory) {
    return NS_ERROR_FAILURE;
  }

  // Now create an instance of the content viewer nsLayoutDLF makes the
  // determination if it should be a "view-source" instead of "view"
  nsresult rv = docLoaderFactory->CreateInstance(
      "view", aOpenedChannel, aLoadGroup, aContentType, this, nullptr,
      aContentHandler, aViewer);
  NS_ENSURE_SUCCESS(rv, rv);

  (*aViewer)->SetContainer(this);
  return NS_OK;
}

nsresult nsDocShell::SetupNewViewer(nsIDocumentViewer* aNewViewer,
                                    WindowGlobalChild* aWindowActor) {
  MOZ_ASSERT(!mIsBeingDestroyed);

  //
  // Copy content viewer state from previous or parent content viewer.
  //
  // The following logic is mirrored in nsHTMLDocument::StartDocumentLoad!
  //
  // Do NOT to maintain a reference to the old content viewer outside
  // of this "copying" block, or it will not be destroyed until the end of
  // this routine and all <SCRIPT>s and event handlers fail! (bug 20315)
  //
  // In this block of code, if we get an error result, we return it
  // but if we get a null pointer, that's perfectly legal for parent
  // and parentDocumentViewer.
  //

  int32_t x = 0;
  int32_t y = 0;
  int32_t cx = 0;
  int32_t cy = 0;

  // This will get the size from the current content viewer or from the
  // Init settings
  DoGetPositionAndSize(&x, &y, &cx, &cy);

  nsCOMPtr<nsIDocShellTreeItem> parentAsItem;
  NS_ENSURE_SUCCESS(GetInProcessSameTypeParent(getter_AddRefs(parentAsItem)),
                    NS_ERROR_FAILURE);
  nsCOMPtr<nsIDocShell> parent(do_QueryInterface(parentAsItem));

  const Encoding* reloadEncoding = nullptr;
  int32_t reloadEncodingSource = kCharsetUninitialized;
  // |newMUDV| also serves as a flag to set the data from the above vars
  nsCOMPtr<nsIDocumentViewer> newViewer;

  if (mDocumentViewer || parent) {
    nsCOMPtr<nsIDocumentViewer> oldViewer;
    if (mDocumentViewer) {
      // Get any interesting state from old content viewer
      // XXX: it would be far better to just reuse the document viewer ,
      //      since we know we're just displaying the same document as before
      oldViewer = mDocumentViewer;
    } else {
      // No old content viewer, so get state from parent's content viewer
      parent->GetDocViewer(getter_AddRefs(oldViewer));
    }

    if (oldViewer) {
      newViewer = aNewViewer;
      if (newViewer) {
        reloadEncoding =
            oldViewer->GetReloadEncodingAndSource(&reloadEncodingSource);
      }
    }
  }

  SingleCanvasBackground canvasBg = {};
  bool isUnderHiddenEmbedderElement = false;
  // Ensure that the content viewer is destroyed *after* the GC - bug 71515
  nsCOMPtr<nsIDocumentViewer> viewer = mDocumentViewer;
  if (viewer) {
    // Stop any activity that may be happening in the old document before
    // releasing it...
    viewer->Stop();

    // Try to extract the canvas background color from the old
    // presentation shell, so we can use it for the next document.
    if (PresShell* presShell = viewer->GetPresShell()) {
      canvasBg = presShell->GetViewportCanvasBackground();
      isUnderHiddenEmbedderElement = presShell->IsUnderHiddenEmbedderElement();
    }

    viewer->Close();
    aNewViewer->SetPreviousViewer(viewer);
  }

  mDocumentViewer = nullptr;

  // Now that we're about to switch documents, forget all of our children.
  // Note that we cached them as needed up in CaptureState above.
  DestroyChildren();

  mDocumentViewer = aNewViewer;

  nsCOMPtr<nsIWidget> widget = GetMainWidget();
  LayoutDeviceIntRect bounds(x, y, cx, cy);

  mDocumentViewer->SetNavigationTiming(mTiming);

  nsresult rv = mDocumentViewer->Init(widget, bounds, aWindowActor);
  if (NS_FAILED(rv)) {
    nsCOMPtr<nsIDocumentViewer> viewer = mDocumentViewer;
    viewer->Close();
    viewer->Destroy();
    mDocumentViewer = nullptr;
    SetCurrentURIInternal(nullptr);
    NS_WARNING("DocumentViewer Initialization failed");
    return rv;
  }

  // If we have old state to copy, set the old state onto the new content
  // viewer
  if (newViewer) {
    newViewer->SetReloadEncodingAndSource(reloadEncoding, reloadEncodingSource);
  }

  NS_ENSURE_TRUE(mDocumentViewer, NS_ERROR_FAILURE);

  // Stuff the bgcolor from the old pres shell into the new
  // pres shell. This improves page load continuity.
  if (RefPtr<PresShell> presShell = mDocumentViewer->GetPresShell()) {
    presShell->SetViewportCanvasBackground(canvasBg);
    presShell->ActivenessMaybeChanged();
    if (isUnderHiddenEmbedderElement) {
      presShell->SetIsUnderHiddenEmbedderElement(isUnderHiddenEmbedderElement);
    }
  }

  // XXX: It looks like the LayoutState gets restored again in Embed()
  //      right after the call to SetupNewViewer(...)

  // We don't show the mDocumentViewer yet, since we want to draw the old page
  // until we have enough of the new page to show.  Just return with the new
  // viewer still set to hidden.

  return NS_OK;
}

void nsDocShell::SetDocCurrentStateObj(SessionHistoryInfo* aInfo) {
  NS_ENSURE_TRUE_VOID(mDocumentViewer);

  RefPtr<Document> document = GetDocument();
  NS_ENSURE_TRUE_VOID(document);

  nsCOMPtr<nsIStructuredCloneContainer> scContainer;
  // If aInfo is null, just set the document's state object to null.
  if (aInfo) {
    scContainer = aInfo->GetStateData();
  }
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("nsDocShell %p SetCurrentDocState %p", this, scContainer.get()));

  // It's OK for scContainer too be null here; that just means there's no
  // state data associated with this history entry.
  document->SetStateObject(scContainer);
}

nsresult nsDocShell::CheckLoadingPermissions() {
  // This method checks whether the caller may load content into
  // this docshell. Even though we've done our best to hide windows
  // from code that doesn't have the right to access them, it's
  // still possible for an evil site to open a window and access
  // frames in the new window through window.frames[] (which is
  // allAccess for historic reasons), so we still need to do this
  // check on load.
  nsresult rv = NS_OK;

  if (!IsSubframe()) {
    // We're not a frame. Permit all loads.
    return rv;
  }

  // Note - The check for a current JSContext here isn't necessarily sensical.
  // It's just designed to preserve the old semantics during a mass-conversion
  // patch.
  if (!nsContentUtils::GetCurrentJSContext()) {
    return NS_OK;
  }

  // Check if the caller is from the same origin as this docshell,
  // or any of its ancestors.
  nsIPrincipal* subjectPrincipal = nsContentUtils::SubjectPrincipal();
  for (RefPtr<BrowsingContext> bc = mBrowsingContext; bc;
       bc = bc->GetParent()) {
    // If the BrowsingContext is not in process, then it
    // is true by construction that its principal will not
    // subsume the current docshell principal.
    if (!bc->IsInProcess()) {
      continue;
    }

    nsCOMPtr<nsIScriptGlobalObject> sgo =
        bc->GetDocShell()->GetScriptGlobalObject();
    nsCOMPtr<nsIScriptObjectPrincipal> sop(do_QueryInterface(sgo));

    nsIPrincipal* p;
    if (!sop || !(p = sop->GetPrincipal())) {
      return NS_ERROR_UNEXPECTED;
    }

    // file: URIs are considered the same domain for the purpose of frame
    // navigation by clicking a targeted link, regardless of script
    // accessibility (bug 1934807).
    if (subjectPrincipal->Subsumes(p) ||
        (subjectPrincipal->SchemeIs("file") && p->SchemeIs("file"))) {
      // Same origin, permit load
      return NS_OK;
    }
  }

  return NS_ERROR_DOM_PROP_ACCESS_DENIED;
}

//*****************************************************************************
// nsDocShell: Site Loading
//*****************************************************************************

void nsDocShell::CopyFavicon(nsIURI* aOldURI, nsIURI* aNewURI,
                             bool aInPrivateBrowsing) {
  if (XRE_IsContentProcess()) {
    dom::ContentChild* contentChild = dom::ContentChild::GetSingleton();
    if (contentChild) {
      contentChild->SendCopyFavicon(aOldURI, aNewURI, aInPrivateBrowsing);
    }
    return;
  }

#ifdef MOZ_PLACES
  auto* faviconService = nsFaviconService::GetFaviconService();
  if (faviconService) {
    faviconService->AsyncTryCopyFavicons(
        aOldURI, aNewURI,
        aInPrivateBrowsing ? nsIFaviconService::FAVICON_LOAD_PRIVATE
                           : nsIFaviconService::FAVICON_LOAD_NON_PRIVATE);
  }
#endif
}

class InternalLoadEvent : public Runnable {
 public:
  InternalLoadEvent(nsDocShell* aDocShell, nsDocShellLoadState* aLoadState)
      : mozilla::Runnable("InternalLoadEvent"),
        mDocShell(aDocShell),
        mLoadState(aLoadState) {
    // For events, both target and filename should be the version of "null" they
    // expect. By the time the event is fired, both window targeting and file
    // downloading have been handled, so we should never have an internal load
    // event that retargets or had a download.
    mLoadState->SetTarget(u""_ns);
    mLoadState->SetFileName(VoidString());
  }

  NS_IMETHOD
  Run() override {
#ifndef ANDROID
    MOZ_ASSERT(mLoadState->TriggeringPrincipal(),
               "InternalLoadEvent: Should always have a principal here");
#endif
    return mDocShell->InternalLoad(mLoadState);
  }

 private:
  RefPtr<nsDocShell> mDocShell;
  RefPtr<nsDocShellLoadState> mLoadState;
};

/**
 * Returns true if we started an asynchronous load (i.e., from the network), but
 * the document we're loading there hasn't yet become this docshell's active
 * document.
 *
 * When JustStartedNetworkLoad is true, you should be careful about modifying
 * mLoadType and mLSHE.  These are both set when the asynchronous load first
 * starts, and the load expects that, when it eventually runs InternalLoad,
 * mLoadType and mLSHE will have their original values.
 */
bool nsDocShell::JustStartedNetworkLoad() {
  return mDocumentRequest && mDocumentRequest != GetCurrentDocChannel();
}

// The contentType will be INTERNAL_(I)FRAME if this docshell is for a
// non-toplevel browsing context in spec terms. (frame, iframe, <object>,
// <embed>, etc)
//
// This return value will be used when we call NS_CheckContentLoadPolicy, and
// later when we call DoURILoad.
nsContentPolicyType nsDocShell::DetermineContentType() {
  if (!IsSubframe()) {
    return nsIContentPolicy::TYPE_DOCUMENT;
  }

  const auto& maybeEmbedderElementType =
      GetBrowsingContext()->GetEmbedderElementType();
  if (!maybeEmbedderElementType) {
    // If the EmbedderElementType hasn't been set yet, just assume we're
    // an iframe since that's more common.
    return nsIContentPolicy::TYPE_INTERNAL_IFRAME;
  }

  return maybeEmbedderElementType->EqualsLiteral("iframe")
             ? nsIContentPolicy::TYPE_INTERNAL_IFRAME
             : nsIContentPolicy::TYPE_INTERNAL_FRAME;
}

bool nsDocShell::NoopenerForceEnabled() {
  // If current's top-level browsing context's active document's
  // cross-origin-opener-policy is "same-origin" or "same-origin + COEP" then
  // if currentDoc's origin is not same origin with currentDoc's top-level
  // origin, noopener is force enabled, and name is cleared to "_blank".
  auto topPolicy = mBrowsingContext->Top()->GetOpenerPolicy();
  return (topPolicy == nsILoadInfo::OPENER_POLICY_SAME_ORIGIN ||
          topPolicy ==
              nsILoadInfo::
                  OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP) &&
         !mBrowsingContext->SameOriginWithTop();
}

nsresult nsDocShell::ComputeNamedTargetBrowsingContext(
    nsDocShellLoadState* aLoadState) {
  if (aLoadState->HasComputedNamedTargetBrowsingContext() ||
      aLoadState->Target().IsEmpty()) {
    return NS_OK;
  }
  // Only _self, _parent, and _top are supported in noopener case.  But we
  // have to be careful to not apply that to the noreferrer case.  See bug
  // 1358469.
  bool allowNamedTarget =
      !aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_NO_OPENER) ||
      aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_DONT_SEND_REFERRER);
  if (allowNamedTarget ||
      aLoadState->Target().LowerCaseEqualsLiteral("_self") ||
      aLoadState->Target().LowerCaseEqualsLiteral("_parent") ||
      aLoadState->Target().LowerCaseEqualsLiteral("_top")) {
    Document* document = GetDocument();
    NS_ENSURE_TRUE(document, NS_ERROR_FAILURE);
    WindowGlobalChild* wgc = document->GetWindowGlobalChild();
    NS_ENSURE_TRUE(wgc, NS_ERROR_FAILURE);
    aLoadState->SetTargetBrowsingContext(wgc->FindBrowsingContextWithName(
        aLoadState->Target(), /* aUseEntryGlobalForAccessCheck */ false));
  }
  aLoadState->SetHasComputedNamedTargetBrowsingContext(true);
  return NS_OK;
}

nsresult nsDocShell::PerformRetargeting(nsDocShellLoadState* aLoadState) {
  MOZ_ASSERT(aLoadState, "need a load state!");
  MOZ_ASSERT(!aLoadState->Target().IsEmpty(), "should have a target here!");

  nsresult rv = NS_OK;

  rv = ComputeNamedTargetBrowsingContext(aLoadState);
  NS_ENSURE_SUCCESS(rv, rv);
  const MaybeDiscarded<BrowsingContext>& targetBCMaybeDiscarded =
      aLoadState->TargetBrowsingContext();
  if (targetBCMaybeDiscarded.IsDiscarded()) {
    // e.g. target frame navigated to cross-origin site before async navigation
    // happened
    return NS_BINDING_ABORTED;
  }
  RefPtr<BrowsingContext> targetContext =
      targetBCMaybeDiscarded.GetMaybeDiscarded();

  if (!targetContext) {
    // If the targetContext doesn't exist, then this is a new docShell and we
    // should consider this a TYPE_DOCUMENT load
    //
    // For example, when target="_blank"

    // If there's no targetContext, that means we are about to create a new
    // window. Perform a content policy check before creating the window. Please
    // note for all other docshell loads content policy checks are performed
    // within the contentSecurityManager when the channel is about to be
    // openend.
    nsISupports* requestingContext = nullptr;
    if (XRE_IsContentProcess()) {
      // In e10s the child process doesn't have access to the element that
      // contains the browsing context (because that element is in the chrome
      // process). So we just pass mScriptGlobal.
      requestingContext = ToSupports(mScriptGlobal);
    } else {
      // This is for loading non-e10s tabs and toplevel windows of various
      // sorts.
      // For the toplevel window cases, requestingElement will be null.
      nsCOMPtr<Element> requestingElement =
          mScriptGlobal->GetFrameElementInternal();
      requestingContext = requestingElement;
    }

    // Ideally we should use the same loadinfo as within DoURILoad which
    // should match this one when both are applicable.
    RefPtr secCheckLoadInfo = MakeRefPtr<LoadInfo>(
        mScriptGlobal, aLoadState->URI(), aLoadState->TriggeringPrincipal(),
        requestingContext, nsILoadInfo::SEC_ONLY_FOR_EXPLICIT_CONTENTSEC_CHECK,
        0);

    // Since Content Policy checks are performed within docShell as well as
    // the ContentSecurityManager we need a reliable way to let certain
    // nsIContentPolicy consumers ignore duplicate calls.
    secCheckLoadInfo->SetSkipContentPolicyCheckForWebRequest(true);

    int16_t shouldLoad = nsIContentPolicy::ACCEPT;
    rv = NS_CheckContentLoadPolicy(aLoadState->URI(), secCheckLoadInfo,
                                   &shouldLoad);

    if (NS_FAILED(rv) || NS_CP_REJECTED(shouldLoad)) {
      if (NS_SUCCEEDED(rv)) {
        if (shouldLoad == nsIContentPolicy::REJECT_TYPE) {
          return NS_ERROR_CONTENT_BLOCKED_SHOW_ALT;
        }
        if (shouldLoad == nsIContentPolicy::REJECT_POLICY) {
          return NS_ERROR_BLOCKED_BY_POLICY;
        }
      }

      return NS_ERROR_CONTENT_BLOCKED;
    }
  }

  //
  // Resolve the window target before going any further...
  // If the load has been targeted to another DocShell, then transfer the
  // load to it...
  //

  // We've already done our owner-inheriting.  Mask out that bit, so we
  // don't try inheriting an owner from the target window if we came up
  // with a null owner above.
  aLoadState->UnsetInternalLoadFlag(INTERNAL_LOAD_FLAGS_INHERIT_PRINCIPAL);

  if (!targetContext) {
    // If the docshell's document is sandboxed, only open a new window
    // if the document's SANDBOXED_AUXILLARY_NAVIGATION flag is not set.
    // (i.e. if allow-popups is specified)
    NS_ENSURE_TRUE(mDocumentViewer, NS_ERROR_FAILURE);
    Document* doc = mDocumentViewer->GetDocument();

    const bool isDocumentAuxSandboxed =
        doc && (doc->GetSandboxFlags() & SANDBOXED_AUXILIARY_NAVIGATION);

    if (isDocumentAuxSandboxed) {
      return NS_ERROR_DOM_INVALID_ACCESS_ERR;
    }

    nsCOMPtr<nsPIDOMWindowOuter> win = GetWindow();
    NS_ENSURE_TRUE(win, NS_ERROR_NOT_AVAILABLE);

    RefPtr<BrowsingContext> newBC;
    nsAutoCString spec;
    aLoadState->URI()->GetSpec(spec);

    // If we are a noopener load, we just hand the whole thing over to our
    // window.
    if (aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_NO_OPENER) ||
        NoopenerForceEnabled()) {
      // Various asserts that we know to hold because NO_OPENER loads can only
      // happen for links.
      MOZ_ASSERT(!aLoadState->LoadReplace());
      MOZ_ASSERT(aLoadState->PrincipalToInherit() ==
                 aLoadState->TriggeringPrincipal());
      MOZ_ASSERT(!(aLoadState->InternalLoadFlags() &
                   ~(INTERNAL_LOAD_FLAGS_NO_OPENER |
                     INTERNAL_LOAD_FLAGS_DONT_SEND_REFERRER)),
                 "Only INTERNAL_LOAD_FLAGS_NO_OPENER and "
                 "INTERNAL_LOAD_FLAGS_DONT_SEND_REFERRER can be set");
      MOZ_ASSERT_IF(aLoadState->PostDataStream(),
                    aLoadState->IsFormSubmission());
      MOZ_ASSERT(!aLoadState->HeadersStream());
      // If OnLinkClickSync was invoked inside the onload handler, the load
      // type would be set to LOAD_NORMAL_REPLACE; otherwise it should be
      // LOAD_LINK.
      MOZ_ASSERT(aLoadState->LoadType() == LOAD_LINK ||
                 aLoadState->LoadType() == LOAD_NORMAL_REPLACE);
      MOZ_ASSERT(!aLoadState->LoadIsFromSessionHistory());
      MOZ_ASSERT(aLoadState->FirstParty());  // Windowwatcher will assume this.

      RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(aLoadState->URI());

      // Set up our loadinfo so it will do the load as much like we would have
      // as possible.
      loadState->SetReferrerInfo(aLoadState->GetReferrerInfo());
      loadState->SetOriginalURI(aLoadState->OriginalURI());

      Maybe<nsCOMPtr<nsIURI>> resultPrincipalURI;
      aLoadState->GetMaybeResultPrincipalURI(resultPrincipalURI);

      loadState->SetMaybeResultPrincipalURI(resultPrincipalURI);
      loadState->SetKeepResultPrincipalURIIfSet(
          aLoadState->KeepResultPrincipalURIIfSet());
      // LoadReplace will always be false due to asserts above, skip setting
      // it.
      loadState->SetTriggeringPrincipal(aLoadState->TriggeringPrincipal());
      loadState->SetTriggeringSandboxFlags(
          aLoadState->TriggeringSandboxFlags());
      loadState->SetTriggeringWindowId(aLoadState->TriggeringWindowId());
      loadState->SetTriggeringStorageAccess(
          aLoadState->TriggeringStorageAccess());
      loadState->SetTriggeringClassificationFlags(
          aLoadState->TriggeringClassificationFlags());
      loadState->SetPolicyContainer(aLoadState->PolicyContainer());
      loadState->SetInheritPrincipal(aLoadState->HasInternalLoadFlags(
          INTERNAL_LOAD_FLAGS_INHERIT_PRINCIPAL));
      // Explicit principal because we do not want any guesses as to what the
      // principal to inherit is: it should be aTriggeringPrincipal.
      loadState->SetPrincipalIsExplicit(true);
      loadState->SetLoadType(aLoadState->LoadType());
      loadState->SetForceAllowDataURI(aLoadState->HasInternalLoadFlags(
          INTERNAL_LOAD_FLAGS_FORCE_ALLOW_DATA_URI));

      loadState->SetHasValidUserGestureActivation(
          aLoadState->HasValidUserGestureActivation());

      loadState->SetTextDirectiveUserActivation(
          aLoadState->GetTextDirectiveUserActivation());

      // Propagate POST data to the new load.
      loadState->SetPostDataStream(aLoadState->PostDataStream());
      loadState->SetIsFormSubmission(aLoadState->IsFormSubmission());

      loadState->SetNavigationAPIState(aLoadState->GetNavigationAPIState());

      rv = win->Open(spec,
                     aLoadState->Target(),  // window name
                     u""_ns,                // Features
                     loadState,
                     true,  // aForceNoOpener
                     getter_AddRefs(newBC));
      MOZ_ASSERT(!newBC);
      return rv;
    }

    rv = win->OpenNoNavigate(spec,
                             aLoadState->Target(),  // window name
                             u""_ns,                // Features
                             getter_AddRefs(newBC));

    // In some cases the Open call doesn't actually result in a new
    // window being opened.  We can detect these cases by examining the
    // document in |newBC|, if any.
    nsCOMPtr<nsPIDOMWindowOuter> piNewWin =
        newBC ? newBC->GetDOMWindow() : nullptr;
    if (piNewWin) {
      RefPtr<Document> newDoc = piNewWin->GetExtantDoc();
      if (!newDoc || newDoc->IsInitialDocument()) {
        aLoadState->SetInternalLoadFlag(INTERNAL_LOAD_FLAGS_FIRST_LOAD);
      }
    }

    if (newBC) {
      targetContext = newBC;
    }
  }
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(targetContext, rv);

  // If our target BrowsingContext is still pending initialization, ignore the
  // navigation request targeting it.
  if (NS_WARN_IF(targetContext->GetPendingInitialization())) {
    return NS_OK;
  }

  aLoadState->SetTargetBrowsingContext(targetContext);
  if (aLoadState->IsFormSubmission()) {
    aLoadState->SetLoadType(
        GetLoadTypeForFormSubmission(targetContext, aLoadState));
  }

  //
  // Transfer the load to the target BrowsingContext... Clear the window target
  // name to the empty string to prevent recursive retargeting!
  //
  // No window target
  aLoadState->SetTarget(u""_ns);
  // No forced download
  aLoadState->SetFileName(VoidString());
  return targetContext->InternalLoad(aLoadState);
}

static nsAutoCString RefMaybeNull(nsIURI* aURI) {
  nsAutoCString result;
  if (NS_FAILED(aURI->GetRef(result))) {
    result.SetIsVoid(true);
  }
  return result;
}

uint32_t nsDocShell::GetSameDocumentNavigationFlags(nsIURI* aNewURI) {
  uint32_t flags = LOCATION_CHANGE_SAME_DOCUMENT;

  bool equal = false;
  if (mCurrentURI &&
      NS_SUCCEEDED(mCurrentURI->EqualsExceptRef(aNewURI, &equal)) && equal &&
      RefMaybeNull(mCurrentURI) != RefMaybeNull(aNewURI)) {
    flags |= LOCATION_CHANGE_HASHCHANGE;
  }

  return flags;
}

struct SameDocumentNavigationState {
  nsAutoCString mCurrentHash;
  nsAutoCString mNewHash;
  nsTArray<TextDirective> mTextDirectives;
  bool mCurrentURIHasRef = false;
  bool mNewURIHasRef = false;
  bool mSameExceptHashes = false;
  bool mSecureUpgradeURI = false;
  bool mHistoryNavBetweenSameDoc = false;
  bool mIdentical = false;
};

bool nsDocShell::IsSameDocumentNavigation(nsDocShellLoadState* aLoadState,
                                          SameDocumentNavigationState& aState) {
  MOZ_ASSERT(aLoadState);
  if (!(aLoadState->LoadType() == LOAD_NORMAL ||
        aLoadState->LoadType() == LOAD_STOP_CONTENT ||
        LOAD_TYPE_HAS_FLAGS(aLoadState->LoadType(),
                            LOAD_FLAGS_REPLACE_HISTORY) ||
        aLoadState->LoadType() == LOAD_HISTORY ||
        aLoadState->LoadType() == LOAD_LINK)) {
    return false;
  }

  if (GetExtantDocument() &&
      GetExtantDocument()->IsUncommittedInitialDocument()) {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("nsDocShell::IsSameDocumentNavigation %p false, document is "
             "uncommitted initial",
             this));
    return false;
  }

  nsCOMPtr<nsIURI> currentURI = mCurrentURI;

  nsresult rvURINew = aLoadState->URI()->GetRef(aState.mNewHash);
  if (NS_SUCCEEDED(rvURINew)) {
    rvURINew = aLoadState->URI()->GetHasRef(&aState.mNewURIHasRef);
  }

  // A Fragment Directive must be removed from the new hash in order to allow
  // fallback element id scroll.
  FragmentDirective::ParseAndRemoveFragmentDirectiveFromFragmentString(
      aState.mNewHash, &aState.mTextDirectives, aLoadState->URI());

  if (currentURI && NS_SUCCEEDED(rvURINew)) {
    nsresult rvURIOld = currentURI->GetRef(aState.mCurrentHash);
    if (NS_SUCCEEDED(rvURIOld)) {
      rvURIOld = currentURI->GetHasRef(&aState.mCurrentURIHasRef);
    }
    if (NS_SUCCEEDED(rvURIOld)) {
      if (NS_FAILED(currentURI->EqualsExceptRef(aLoadState->URI(),
                                                &aState.mSameExceptHashes))) {
        aState.mSameExceptHashes = false;
      }
    }
  }

  if (!aState.mSameExceptHashes && currentURI && NS_SUCCEEDED(rvURINew)) {
    // Maybe aLoadState->URI() came from the exposable form of currentURI?
    nsCOMPtr<nsIURI> currentExposableURI =
        nsIOService::CreateExposableURI(currentURI);
    nsresult rvURIOld = currentExposableURI->GetRef(aState.mCurrentHash);
    if (NS_SUCCEEDED(rvURIOld)) {
      rvURIOld = currentExposableURI->GetHasRef(&aState.mCurrentURIHasRef);
    }
    if (NS_SUCCEEDED(rvURIOld)) {
      if (NS_FAILED(currentExposableURI->EqualsExceptRef(
              aLoadState->URI(), &aState.mSameExceptHashes))) {
        aState.mSameExceptHashes = false;
      }
      // HTTPS-Only Mode upgrades schemes from http to https in Necko, hence we
      // have to perform a special check here to avoid an actual navigation. If
      // HTTPS-Only Mode is enabled and the two URIs are same-origin (modulo the
      // fact that the new URI is currently http), then set mSameExceptHashes to
      // true and only perform a fragment navigation.
      if (!aState.mSameExceptHashes) {
        if (nsCOMPtr<nsIChannel> docChannel = GetCurrentDocChannel()) {
          nsCOMPtr<nsILoadInfo> docLoadInfo = docChannel->LoadInfo();
          nsHTTPSOnlyUtils::UpgradeMode upgradeMode =
              nsHTTPSOnlyUtils::GetUpgradeMode(docLoadInfo);
          if (!docLoadInfo->GetLoadErrorPage() &&
              (upgradeMode == nsHTTPSOnlyUtils::HTTPS_ONLY_MODE ||
               upgradeMode == nsHTTPSOnlyUtils::HTTPS_FIRST_MODE) &&
              nsHTTPSOnlyUtils::IsHttpDowngrade(currentExposableURI,
                                                aLoadState->URI())) {
            uint32_t status = docLoadInfo->GetHttpsOnlyStatus();
            if ((status &
                 (nsILoadInfo::HTTPS_ONLY_UPGRADED_LISTENER_REGISTERED |
                  nsILoadInfo::HTTPS_ONLY_UPGRADED_HTTPS_FIRST)) &&
                !(status & nsILoadInfo::HTTPS_ONLY_EXEMPT)) {
              // At this point the requested URI is for sure a fragment
              // navigation via HTTP and HTTPS-Only mode or HTTPS-First is
              // enabled. Also it is not interfering the upgrade order of
              // https://searchfox.org/firefox-main/source/netwerk/base/nsNetUtil.cpp#2948-2953.
              // Since we are on an HTTPS site the fragment
              // navigation should also be an HTTPS.
              // For that reason we should upgrade the URI to HTTPS.
              aState.mSecureUpgradeURI = true;
              aState.mSameExceptHashes = true;
            }
          }
        }
      }
    }
  }

  if (mActiveEntry && aLoadState->LoadIsFromSessionHistory()) {
    aState.mHistoryNavBetweenSameDoc = mActiveEntry->SharesDocumentWith(
        aLoadState->GetLoadingSessionHistoryInfo()->mInfo);
  }
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("nsDocShell::IsSameDocumentNavigation %p NavBetweenSameDoc=%d", this,
           aState.mHistoryNavBetweenSameDoc));

  // Two URIs are identical if they're same except hashes, they both have
  // hashes, and their hashes are the same.
  aState.mIdentical = aState.mSameExceptHashes &&
                      (aState.mNewURIHasRef == aState.mCurrentURIHasRef) &&
                      aState.mCurrentHash.Equals(aState.mNewHash);

  // A same document navigation happens when we navigate between two SHEntries
  // for the same document. We do a same document navigation under two
  // circumstances. Either
  //
  //  a) we're navigating between two different SHEntries which share a
  //     document, or
  //
  //  b) we're navigating to a new shentry whose URI differs from the
  //     current URI only in its hash, the new hash is non-empty, and
  //     we're not doing a POST.
  //
  // The restriction that the SHEntries in (a) must be different ensures
  // that history.go(0) and the like trigger full refreshes, rather than
  // same document navigations.
  if (aState.mHistoryNavBetweenSameDoc &&
      !aLoadState->GetLoadingSessionHistoryInfo()->mLoadingCurrentEntry) {
    return true;
  }

  MOZ_LOG(
      gSHLog, LogLevel::Debug,
      ("nsDocShell::IsSameDocumentNavigation %p !LoadIsFromSessionHistory=%s "
       "!PostDataStream: %s mSameExceptHashes: %s mNewURIHasRef: %s",
       this, !aLoadState->LoadIsFromSessionHistory() ? "true" : "false",
       !aLoadState->PostDataStream() ? "true" : "false",
       aState.mSameExceptHashes ? "true" : "false",
       aState.mNewURIHasRef ? "true" : "false"));
  return !aLoadState->LoadIsFromSessionHistory() &&
         !aLoadState->PostDataStream() && aState.mSameExceptHashes &&
         aState.mNewURIHasRef;
}

static bool IsSamePrincipalForDocumentURI(nsIPrincipal* aCurrentPrincipal,
                                          nsIURI* aCurrentURI,
                                          nsIURI* aNewURI) {
  nsCOMPtr<nsIURI> principalURI = aCurrentPrincipal->GetURI();
  if (aCurrentPrincipal->GetIsNullPrincipal()) {
    if (nsCOMPtr<nsIPrincipal> precursor =
            aCurrentPrincipal->GetPrecursorPrincipal()) {
      principalURI = precursor->GetURI();
    }
  }

  return !nsScriptSecurityManager::IsHttpOrHttpsAndCrossOrigin(principalURI,
                                                               aNewURI) &&
         !nsScriptSecurityManager::IsHttpOrHttpsAndCrossOrigin(principalURI,
                                                               aCurrentURI) &&
         !nsScriptSecurityManager::IsHttpOrHttpsAndCrossOrigin(aCurrentURI,
                                                               aNewURI);
}

nsresult nsDocShell::HandleSameDocumentNavigation(
    nsDocShellLoadState* aLoadState, SameDocumentNavigationState& aState,
    bool& aSameDocument) {
  aSameDocument = true;
#ifdef DEBUG
  SameDocumentNavigationState state;
  MOZ_ASSERT(IsSameDocumentNavigation(aLoadState, state));
#endif

  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("nsDocShell::HandleSameDocumentNavigation %p %s -> %s", this,
           mCurrentURI->GetSpecOrDefault().get(),
           aLoadState->URI()->GetSpecOrDefault().get()));

  RefPtr<Document> doc = GetDocument();
  NS_ENSURE_TRUE(doc, NS_ERROR_FAILURE);

  nsCOMPtr<nsIURI> currentURI = mCurrentURI;

  // We need to upgrade the new URI from http: to https:
  nsCOMPtr<nsIURI> newURI = aLoadState->URI();
  if (aState.mSecureUpgradeURI) {
    MOZ_TRY(NS_GetSecureUpgradedURI(aLoadState->URI(), getter_AddRefs(newURI)));
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("Upgraded URI to %s", newURI->GetSpecOrDefault().get()));
  }

  // check if documentPrincipal, mCurrentURI, and aLoadState->URI() are same
  // origin skip handling otherwise
  if (!IsSamePrincipalForDocumentURI(doc->NodePrincipal(), mCurrentURI,
                                     newURI)) {
    aSameDocument = false;
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("nsDocShell[%p]: possible violation of the same origin policy "
             "during same document navigation",
             this));
    return NS_OK;
  }

  // https://html.spec.whatwg.org/#navigate-fragid
  // Step 2
  RefPtr<nsIStructuredCloneContainer> destinationNavigationAPIState =
      mActiveEntry ? mActiveEntry->GetNavigationAPIState() : nullptr;
  // Step 3
  if (auto* navigationAPIState = aLoadState->GetNavigationAPIState()) {
    destinationNavigationAPIState = navigationAPIState;
  }

  if (nsCOMPtr<nsPIDOMWindowInner> window = doc->GetInnerWindow();
      window && !aState.mHistoryNavBetweenSameDoc) {
    // https://html.spec.whatwg.org/#navigate-fragid
    // Step 1
    if (RefPtr<Navigation> navigation = window->Navigation()) {
      AutoJSAPI jsapi;
      if (jsapi.Init(window)) {
        RefPtr<Element> sourceElement = aLoadState->GetSourceElement();
        // Step 4
        RefPtr apiMethodTracker = aLoadState->GetNavigationAPIMethodTracker();
        bool shouldContinue = navigation->FirePushReplaceReloadNavigateEvent(
            jsapi.cx(), aLoadState->GetNavigationType(), newURI,
            /* aIsSameDocument */ true,
            Some(aLoadState->UserNavigationInvolvement()), sourceElement,
            /* aFormDataEntryList */ nullptr,
            /* aNavigationAPIState */ destinationNavigationAPIState,
            /* aClassicHistoryAPIState */ nullptr, apiMethodTracker);

        // Step 5
        if (!shouldContinue) {
          return NS_OK;
        }
      }
    }
  }

  doc->DoNotifyPossibleTitleChange();

  // Store the pending uninvoked directives if it is a same document
  // navigation. We need to set it here, in case the navigation happens before
  // the document has actually finished loading.
  doc->FragmentDirective()->SetTextDirectives(
      std::move(aState.mTextDirectives));

#ifdef DEBUG
  if (aState.mSameExceptHashes) {
    bool sameExceptHashes = false;
    currentURI->EqualsExceptRef(newURI, &sameExceptHashes);
    MOZ_ASSERT(sameExceptHashes);
  }
#endif
  const nsCOMPtr<nsILoadInfo> loadInfo =
      doc->GetChannel() ? doc->GetChannel()->LoadInfo() : nullptr;
  if (loadInfo) {
    loadInfo->SetIsSameDocumentNavigation(true);
  }
  // Save the position of the scrollers.
  nsPoint scrollPos = GetCurScrollPos();

  // Reset mLoadType to its original value once we exit this block, because this
  // same document navigation might have started after a normal, network load,
  // and we don't want to clobber its load type. See bug 737307.
  Maybe<AutoRestore<uint32_t>> loadTypeResetter;
  if (StaticPrefs::
          docshell_shistory_sameDocumentNavigationOverridesLoadType() &&
      !doc->NodePrincipal()->IsURIInPrefList(
          "docshell.shistory.sameDocumentNavigationOverridesLoadType."
          "forceDisable")) {
    loadTypeResetter.emplace(mLoadType);
  }
  if (JustStartedNetworkLoad() && !loadTypeResetter.isSome()) {
    loadTypeResetter.emplace(mLoadType);
  }

  // If a non-same-document-navigation (i.e., a network load) is pending, make
  // this a replacement load, so that we don't add a SHEntry here and the
  // network load goes into the SHEntry it expects to.
  if (JustStartedNetworkLoad() && (aLoadState->LoadType() & LOAD_CMD_NORMAL)) {
    mLoadType = LOAD_NORMAL_REPLACE;
  } else {
    mLoadType = aLoadState->LoadType();
  }

  mURIResultedInDocument = true;

  // we need to assign aLoadState->SHEntry() to mLoadingEntry right here, so
  // that on History loads, SetCurrentURI() called from OnNewURI() will send
  // proper onLocationChange() notifications to the browser to update
  // back/forward buttons.
  UniquePtr<mozilla::dom::LoadingSessionHistoryInfo> oldLoadingEntry;
  mLoadingEntry.swap(oldLoadingEntry);
  if (aLoadState->GetLoadingSessionHistoryInfo()) {
    mLoadingEntry = MakeUnique<LoadingSessionHistoryInfo>(
        *aLoadState->GetLoadingSessionHistoryInfo());
    mNeedToReportActiveAfterLoadingBecomesActive = false;
  }

  // Set the doc's URI according to the new history entry's URI.
  doc->SetDocumentURI(newURI);

  /* This is a anchor traversal within the same page.
   * call OnNewURI() so that, this traversal will be
   * recorded in session and global history.
   */
  nsCOMPtr<nsIPrincipal> newURITriggeringPrincipal, newURIPrincipalToInherit,
      newURIPartitionedPrincipalToInherit;
  nsCOMPtr<nsIPolicyContainer> newPolicyContainer;
  if (mActiveEntry) {
    newURITriggeringPrincipal = mActiveEntry->GetTriggeringPrincipal();
    newURIPrincipalToInherit = mActiveEntry->GetPrincipalToInherit();
    newURIPartitionedPrincipalToInherit =
        mActiveEntry->GetPartitionedPrincipalToInherit();
    newPolicyContainer = mActiveEntry->GetPolicyContainer();
  } else {
    newURITriggeringPrincipal = aLoadState->TriggeringPrincipal();
    newURIPrincipalToInherit = doc->NodePrincipal();
    newURIPartitionedPrincipalToInherit = doc->PartitionedPrincipal();
    newPolicyContainer = doc->GetPolicyContainer();
  }

  uint32_t locationChangeFlags = GetSameDocumentNavigationFlags(newURI);

  // Pass true for aCloneSHChildren, since we're not
  // changing documents here, so all of our subframes are
  // still relevant to the new session history entry.
  //
  // It also makes OnNewURI(...) set LOCATION_CHANGE_SAME_DOCUMENT
  // flag on firing onLocationChange(...).
  // Anyway, aCloneSHChildren param is simply reflecting
  // doSameDocumentNavigation in this scope.
  //
  // Note: we'll actually fire onLocationChange later, in order to preserve
  // ordering of HistoryCommit() in the parent vs onLocationChange (bug
  // 1668126)
  bool locationChangeNeeded = OnNewURI(
      newURI, nullptr, newURITriggeringPrincipal, newURIPrincipalToInherit,
      newURIPartitionedPrincipalToInherit, newPolicyContainer, true, true);

  nsCOMPtr<nsIInputStream> postData;
  nsCOMPtr<nsIReferrerInfo> referrerInfo;
  uint32_t cacheKey = 0;

  bool scrollRestorationIsManual = false;
  if (mActiveEntry) {
    mActiveEntry->SetScrollPosition(scrollPos.x, scrollPos.y);
    if (mBrowsingContext) {
      CollectWireframe();
      if (XRE_IsParentProcess()) {
        SessionHistoryEntry* entry =
            mBrowsingContext->Canonical()->GetActiveSessionHistoryEntry();
        if (entry) {
          entry->SetScrollPosition(scrollPos.x, scrollPos.y);
        }
      } else {
        (void)ContentChild::GetSingleton()
            ->SendSessionHistoryEntryScrollPosition(mBrowsingContext,
                                                    scrollPos.x, scrollPos.y);
      }
    }
  }
  if (mLoadingEntry && !mLoadingEntry->mLoadIsFromSessionHistory) {
    // If we're not doing a history load, scroll restoration
    // should be inherited from the previous session history entry.
    // XXX This needs most probably tweaks once fragment navigation is
    // fixed to work with session-history-in-parent.
    SetScrollRestorationIsManualOnHistoryEntry(scrollRestorationIsManual);
  }

  // If we're doing a history load, use its scroll restoration state.
  if (aLoadState->LoadIsFromSessionHistory()) {
    scrollRestorationIsManual = aLoadState->GetLoadingSessionHistoryInfo()
                                    ->mInfo.GetScrollRestorationIsManual();
  }

  /* Make a new mActiveEntry. This will either be a new entry created
   * by OnNewURI() for normal loads or aLoadState->SHEntry() for history
   * loads.
   */
  if (aLoadState->LoadIsFromSessionHistory()) {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("Moving the loading entry to the active entry on nsDocShell %p to "
             "%s",
             this, mLoadingEntry->mInfo.GetURI()->GetSpecOrDefault().get()));

    nsCOMPtr<nsILayoutHistoryState> currentLayoutHistoryState;
    if (mActiveEntry) {
      currentLayoutHistoryState = mActiveEntry->GetLayoutHistoryState();
    }

    UniquePtr<SessionHistoryInfo> previousActiveEntry(mActiveEntry.release());
    mActiveEntry = MakeUnique<SessionHistoryInfo>(mLoadingEntry->mInfo);
    if (currentLayoutHistoryState) {
      // Restore the existing nsILayoutHistoryState object, since it is
      // possibly being used by the layout. When doing a new load, the
      // shared state is copied from the existing active entry, so this
      // special case is needed only with the history loads.
      mActiveEntry->SetLayoutHistoryState(currentLayoutHistoryState);
    }

    if (cacheKey != 0) {
      mActiveEntry->SetCacheKey(cacheKey);
    }

    // We're passing in mCurrentURI, which could be null. SessionHistoryCommit
    // does require a non-null uri if this is for a refresh load of the same
    // URI, but in that case mCurrentURI won't be null here.
    mBrowsingContext->SessionHistoryCommit(
        *mLoadingEntry, mLoadType, mCurrentURI, previousActiveEntry.get(), true,
        /* No expiration update on the same document loads*/
        false, cacheKey);
    // FIXME Need to set postdata.

    // Set the title for the SH entry for this target url so that
    // SH menus in go/back/forward buttons won't be empty for this.
    // Note, when session history lives in the parent process, this does not
    // update the title there.
    SetTitleOnHistoryEntry(false);
  } else {
    Maybe<bool> scrollRestorationIsManual;
    if (mActiveEntry) {
      scrollRestorationIsManual.emplace(
          mActiveEntry->GetScrollRestorationIsManual());

      // Get the postdata, page ident and referrer info from the current page,
      // if the new load is being done via normal means.  Note that "normal
      // means" can be checked for just by checking for LOAD_CMD_NORMAL, given
      // the loadType and allowScroll check above -- it filters out some
      // LOAD_CMD_NORMAL cases that we wouldn't want here.
      if (aLoadState->LoadType() & LOAD_CMD_NORMAL) {
        postData = mActiveEntry->GetPostData();
        cacheKey = mActiveEntry->GetCacheKey();
        referrerInfo = mActiveEntry->GetReferrerInfo();
      }
    }

    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("Creating an active entry on nsDocShell %p to %s", this,
             newURI->GetSpecOrDefault().get()));
    UniquePtr<SessionHistoryInfo> previousActiveEntry(mActiveEntry.release());
    if (previousActiveEntry) {
      mActiveEntry =
          MakeUnique<SessionHistoryInfo>(*previousActiveEntry, newURI);
    } else {
      mActiveEntry = MakeUnique<SessionHistoryInfo>(
          newURI, newURITriggeringPrincipal, newURIPrincipalToInherit,
          newURIPartitionedPrincipalToInherit, newPolicyContainer,
          mContentTypeHint);
    }

    // Save the postData obtained from the previous page in to the session
    // history entry created for the anchor page, so that any history load of
    // the anchor page will restore the appropriate postData.
    if (postData) {
      mActiveEntry->SetPostData(postData);
    }

    // Make sure we won't just repost without hitting the
    // cache first
    if (cacheKey != 0) {
      mActiveEntry->SetCacheKey(cacheKey);
    }

    // As the document has not changed, the referrer info hasn't changed too,
    // so we can just copy it over.
    if (referrerInfo) {
      mActiveEntry->SetReferrerInfo(referrerInfo);
    }

    // Set the title for the SH entry for this target url so that
    // SH menus in go/back/forward buttons won't be empty for this.
    mActiveEntry->SetTitle(mTitle);

    if (scrollRestorationIsManual.isSome()) {
      mActiveEntry->SetScrollRestorationIsManual(
          scrollRestorationIsManual.value());
    }

    if (destinationNavigationAPIState) {
      mActiveEntry->SetNavigationAPIState(destinationNavigationAPIState);
    }

    if (LOAD_TYPE_HAS_FLAGS(mLoadType, LOAD_FLAGS_REPLACE_HISTORY)) {
      if (previousActiveEntry) {
        mActiveEntry->NavigationKey() = previousActiveEntry->NavigationKey();
      }
      mBrowsingContext->ReplaceActiveSessionHistoryEntry(mActiveEntry.get());
    } else {
      mBrowsingContext->IncrementHistoryEntryCountForBrowsingContext();
      // FIXME We should probably just compute mChildOffset in the parent
      //       instead of passing it over IPC here.
      mBrowsingContext->SetActiveSessionHistoryEntry(
          Some(scrollPos), mActiveEntry.get(), previousActiveEntry.get(),
          mLoadType, cacheKey);
      // FIXME Do we need to update mPreviousEntryIndex and mLoadedEntryIndex?
    }
  }

  if (locationChangeNeeded) {
    FireOnLocationChange(this, nullptr, newURI, locationChangeFlags);
  }

  /* Restore the original LSHE if we were loading something
   * while same document navigation was initiated.
   */
  mLoadingEntry.swap(oldLoadingEntry);

  /* Set the title for the Global History entry for this anchor url.
   */
  UpdateGlobalHistoryTitle(newURI);

  SetDocCurrentStateObj(mActiveEntry.get());

  // Inform the favicon service that the favicon for oldURI also
  // applies to newURI.
  CopyFavicon(currentURI, newURI, UsePrivateBrowsing());

  RefPtr<nsGlobalWindowOuter> scriptGlobal = mScriptGlobal;
  nsCOMPtr<nsPIDOMWindowInner> win =
      scriptGlobal ? scriptGlobal->GetCurrentInnerWindow() : nullptr;

  // https://html.spec.whatwg.org/#scroll-to-fragid
  // 14. Update document for history step application given navigable's
  //     active document, historyEntry, true, scriptHistoryIndex,
  //     scriptHistoryLength, and historyHandling.
  if (RefPtr navigation = win ? win->Navigation() : nullptr) {
    MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
            ("nsDocShell %p triggering a navigation event from "
             "HandleSameDocumentNavigation",
             this));
    // https://html.spec.whatwg.org/#update-document-for-history-step-application
    // 6.4.2. Update the navigation API entries for a same-document
    //        navigation given navigation, entry, and navigationType.
    navigation->UpdateEntriesForSameDocumentNavigation(
        mActiveEntry.get(),
        NavigationUtils::NavigationTypeFromLoadType(mLoadType).valueOr(
            NavigationType::Push));
  }

  // The check for uninvoked directives must come before ScrollToAnchor() is
  // called.
  const bool hasTextDirectives =
      doc->FragmentDirective()->HasUninvokedDirectives();

  // https://html.spec.whatwg.org/#scroll-to-fragid
  // 15. Scroll to the fragment given navigable's active document.

  // ScrollToAnchor doesn't necessarily cause us to scroll the window;
  // the function decides whether a scroll is appropriate based on the
  // arguments it receives.  But even if we don't end up scrolling,
  // ScrollToAnchor performs other important tasks, such as informing
  // the presShell that we have a new hash.  See bug 680257.
  nsresult rv = ScrollToAnchor(aState.mCurrentURIHasRef, aState.mNewURIHasRef,
                               aState.mNewHash, aLoadState->LoadType());
  NS_ENSURE_SUCCESS(rv, rv);

  /* restore previous position of scroller(s), if we're moving
   * back in history (bug 59774)
   */
  nscoord bx = 0;
  nscoord by = 0;
  bool needsScrollPosUpdate = false;
  if (mActiveEntry &&
      (aLoadState->LoadType() == LOAD_HISTORY ||
       aLoadState->LoadType() == LOAD_RELOAD_NORMAL) &&
      !scrollRestorationIsManual) {
    needsScrollPosUpdate = true;
    mActiveEntry->GetScrollPosition(&bx, &by);
  }

  // Dispatch the popstate and hashchange events, as appropriate.
  //
  // The event dispatch below can cause us to re-enter script and
  // destroy the docshell, nulling out mScriptGlobal. Hold a stack
  // reference to avoid null derefs. See bug 914521.
  if (win) {
    // Fire a hashchange event URIs differ, and only in their hashes.
    // If the fragment contains a directive, compare hasRef.
    bool doHashchange = aState.mSameExceptHashes &&
                        (!aState.mCurrentHash.Equals(aState.mNewHash) ||
                         (hasTextDirectives &&
                          aState.mCurrentURIHasRef != aState.mNewURIHasRef));

    // We enqueue the hashchange event first before dispatching popstate,
    // differing from the spec.
    // See: https://github.com/whatwg/html/issues/12190
    if (doHashchange) {
      // Note that currentURI hasn't changed because it's on the
      // stack, so we can just use it directly as the old URI.
      win->DispatchAsyncHashchange(currentURI, newURI);
    }

    if (aState.mHistoryNavBetweenSameDoc || doHashchange) {
      win->DispatchSyncPopState();
    }

    if (needsScrollPosUpdate && win->HasActiveDocument()) {
      SetCurScrollPosEx(bx, by);
    }
  }

  return NS_OK;
}

static bool NavigationShouldTakeFocus(nsDocShell* aDocShell,
                                      nsDocShellLoadState* aLoadState) {
  if (!aLoadState->AllowFocusMove()) {
    return false;
  }
  if (!aLoadState->HasValidUserGestureActivation()) {
    return false;
  }
  const auto& sourceBC = aLoadState->SourceBrowsingContext();
  if (!sourceBC || !sourceBC->IsActive()) {
    // If the navigation didn't come from a foreground tab, then we don't steal
    // focus.
    return false;
  }
  auto* bc = aDocShell->GetBrowsingContext();
  if (sourceBC.get() == bc) {
    // If it comes from the same tab / frame, don't steal focus either.
    return false;
  }
  auto* fm = nsFocusManager::GetFocusManager();
  if (fm && bc->IsActive() && fm->IsInActiveWindow(bc)) {
    // If we're already on the foreground tab of the foreground window, then we
    // don't need to do this. This helps to e.g. not steal focus from the
    // browser chrome unnecessarily.
    return false;
  }
  if (auto* doc = aDocShell->GetExtantDocument()) {
    if (doc->IsInitialDocument()) {
      // If we're the initial load for the browsing context, the browser
      // chrome determines what to focus. This is important because the
      // browser chrome may want to e.g focus the url-bar
      return false;
    }
  }
  // Take loadDivertedInBackground into account so the behavior would be the
  // same as how the tab first opened.
  return !Preferences::GetBool("browser.tabs.loadDivertedInBackground", false);
}

uint32_t nsDocShell::GetLoadTypeForFormSubmission(
    BrowsingContext* aTargetBC, nsDocShellLoadState* aLoadState) {
  MOZ_ASSERT(aLoadState->IsFormSubmission());

  // https://html.spec.whatwg.org/#form-submission-algorithm
  //  22. Let historyHandling be "push".
  //  23. If form document equals targetNavigable's active document, and
  //      form document has not yet completely loaded, then set
  //      historyHandling to "replace".
  return GetBrowsingContext() == aTargetBC && !mEODForCurrentDocument
             ? LOAD_NORMAL_REPLACE
             : LOAD_LINK;
}

static void MaybeConvertToReplaceLoad(nsDocShellLoadState* aLoadState,
                                      Document* aExtantDocument,
                                      bool aIdenticalURI,
                                      bool aHasActiveEntry) {
  // MaybeConvertToReplaceLoad implements steps 12 and 13 of #navigate, but
  // since we're not yet using historyBehavior for all types of loads and
  // configurations, we need to sometimes bail and revert to the old way of
  // handling push to replace load conversion. The different cases we can't
  // handle are:
  //
  // * When we don't have an active document
  // * When a document doesn't yet have a session history entry
  // * When we don't use historyBehavior
  if (!aExtantDocument || !aHasActiveEntry || !aLoadState->HistoryBehavior()) {
    aLoadState->ResetHistoryBehavior();
    return;
  }

  bool convertToReplaceLoad = aLoadState->NeedsCompletelyLoadedDocument() &&
                              !aExtantDocument->IsCompletelyLoaded();
  if (const auto& historyBehavior = aLoadState->HistoryBehavior();
      !convertToReplaceLoad && historyBehavior &&
      *historyBehavior == NavigationHistoryBehavior::Auto) {
    convertToReplaceLoad = aIdenticalURI;
    if (convertToReplaceLoad && aExtantDocument->GetPrincipal()) {
      aExtantDocument->GetPrincipal()->Equals(aLoadState->TriggeringPrincipal(),
                                              &convertToReplaceLoad);
    }
  } else if (aLoadState->HistoryBehavior() ==
             Some(NavigationHistoryBehavior::Replace)) {
    // This load is supposed to be a replace, regardless of completely loaded.
    // This can happen for form submissions since the planned form
    // navigation might be queued before the document is completely loaded
    // and then actually run after it is completely loaded:
    // https://html.spec.whatwg.org/#concept-form-submit
    convertToReplaceLoad = true;
  }

  convertToReplaceLoad =
      convertToReplaceLoad || nsContentUtils::NavigationMustBeAReplace(
                                  *aLoadState->URI(), *aExtantDocument);

  if (convertToReplaceLoad) {
    MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug,
                "Convert to replace when navigating from {} to {}, {}",
                *aExtantDocument->GetDocumentURI(), *aLoadState->URI(),
                (aLoadState->NeedsCompletelyLoadedDocument() &&
                 !nsContentUtils::NavigationMustBeAReplace(*aLoadState->URI(),
                                                           *aExtantDocument))
                    ? "needs completely loaded document"
                    : "navigation must be a replace");
    // There is no replace variant for LOAD_LINK, so we convert it to
    // LOAD_NORMAL_REPLACE just like in nsDocShell::OnNewURI.
    if (aLoadState->LoadType() == LOAD_LINK) {
      aLoadState->SetLoadType(LOAD_NORMAL_REPLACE);
    } else {
      aLoadState->SetLoadType(
          MaybeAddLoadFlags(aLoadState->LoadType(),
                            nsIWebNavigation::LOAD_FLAGS_REPLACE_HISTORY));
    }
    aLoadState->SetHistoryBehavior(NavigationHistoryBehavior::Replace);
  } else {
    aLoadState->SetHistoryBehavior(NavigationHistoryBehavior::Push);
  }
}

// InternalLoad performs several of the steps from
// https://html.spec.whatwg.org/#navigate.
nsresult nsDocShell::InternalLoad(nsDocShellLoadState* aLoadState,
                                  Maybe<uint32_t> aCacheKey) {
  MOZ_ASSERT(aLoadState, "need a load state!");
  MOZ_ASSERT(aLoadState->TriggeringPrincipal(),
             "need a valid TriggeringPrincipal");

  if (!aLoadState->TriggeringPrincipal()) {
    MOZ_ASSERT(false, "InternalLoad needs a valid triggeringPrincipal");
    return NS_ERROR_FAILURE;
  }
  if (NS_WARN_IF(mBrowsingContext->GetPendingInitialization())) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  const bool shouldTakeFocus = NavigationShouldTakeFocus(this, aLoadState);

  mOriginalUriString.Truncate();

  MOZ_LOG(gDocShellLeakLog, LogLevel::Debug,
          ("DOCSHELL %p InternalLoad %s\n", this,
           aLoadState->URI()->GetSpecOrDefault().get()));

  NS_ENSURE_TRUE(IsValidLoadType(aLoadState->LoadType()), NS_ERROR_INVALID_ARG);

  // Cancel loads coming from Docshells that are being destroyed.
  if (mIsBeingDestroyed) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsresult rv = EnsureScriptEnvironment();
  if (NS_FAILED(rv)) {
    return rv;
  }

  // If we have a target to move to, do that now.
  if (!aLoadState->Target().IsEmpty()) {
    return PerformRetargeting(aLoadState);
  }

  // This is the non-retargeting load path, we've already set the right loadtype
  // for form submissions in nsDocShell::OnLinkClickSync.
  if (aLoadState->TargetBrowsingContext().IsNull()) {
    aLoadState->SetTargetBrowsingContext(GetBrowsingContext());
  }

  MOZ_DIAGNOSTIC_ASSERT(
      aLoadState->TargetBrowsingContext() == GetBrowsingContext(),
      "Load must be targeting this BrowsingContext");

  MOZ_TRY(CheckDisallowedJavascriptLoad(aLoadState));

  // If we don't have a target, we're loading into ourselves, and our load
  // delegate may want to intercept that load.
  SameDocumentNavigationState sameDocumentNavigationState;
  bool sameDocument =
      IsSameDocumentNavigation(aLoadState, sameDocumentNavigationState) &&
      !aLoadState->GetPendingRedirectedChannel();

  if (mLoadType != LOAD_ERROR_PAGE &&
      !aLoadState->HasLoadFlags(LOAD_FLAGS_FROM_EXTERNAL)) {
    MaybeConvertToReplaceLoad(aLoadState, GetExtantDocument(),
                              sameDocumentNavigationState.mIdentical,
                              !!mActiveEntry);
  }

  // Note: We do this check both here and in BrowsingContext::
  // LoadURI/InternalLoad, since document-specific sandbox flags are only
  // available in the process triggering the load, and we don't want the target
  // process to have to trust the triggering process to do the appropriate
  // checks for the BrowsingContext's sandbox flags.
  MOZ_TRY(mBrowsingContext->CheckSandboxFlags(aLoadState));
  MOZ_TRY(mBrowsingContext->CheckFramebusting(aLoadState));

  NS_ENSURE_STATE(!HasUnloadedParent());

  rv = CheckLoadingPermissions();
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (mFiredUnloadEvent) {
    if (IsOKToLoadURI(aLoadState->URI())) {
      MOZ_ASSERT(aLoadState->Target().IsEmpty(),
                 "Shouldn't have a window target here!");

      // If this is a replace load, make whatever load triggered
      // the unload event also a replace load, so we don't
      // create extra history entries.
      if (LOAD_TYPE_HAS_FLAGS(aLoadState->LoadType(),
                              LOAD_FLAGS_REPLACE_HISTORY)) {
        mLoadType = LOAD_NORMAL_REPLACE;
      }

      // Do this asynchronously
      RefPtr ev = MakeRefPtr<InternalLoadEvent>(this, aLoadState);
      return Dispatch(ev.forget());
    }

    // Just ignore this load attempt
    return NS_OK;
  }

  // If we are loading a URI that should inherit a security context (basically
  // javascript: at this point), and the caller has said that principal
  // inheritance is allowed, there are a few possible cases:
  //
  // 1) We are provided with the principal to inherit. In that case, we just use
  //    it.
  //
  // 2) The load is coming from some other application. In this case we don't
  //    want to inherit from whatever document we have loaded now, since the
  //    load is unrelated to it.
  //
  // 3) It's a load from our application, but does not provide an explicit
  //    principal to inherit. In that case, we want to inherit the principal of
  //    our current document, or of our parent document (if any) if we don't
  //    have a current document.
  {
    bool inherits;

    if (!aLoadState->HasLoadFlags(LOAD_FLAGS_FROM_EXTERNAL) &&
        !aLoadState->PrincipalToInherit() &&
        (aLoadState->HasInternalLoadFlags(
            INTERNAL_LOAD_FLAGS_INHERIT_PRINCIPAL)) &&
        NS_SUCCEEDED(nsContentUtils::URIInheritsSecurityContext(
            aLoadState->URI(), &inherits)) &&
        inherits) {
      aLoadState->SetPrincipalToInherit(GetInheritedPrincipal(true));
    }
    // If principalToInherit is still null (e.g. if some of the conditions of
    // were not satisfied), then no inheritance of any sort will happen: the
    // load will just get a principal based on the URI being loaded.
  }

  // If this docshell is owned by a frameloader, make sure to cancel
  // possible frameloader initialization before loading a new page.
  nsCOMPtr<nsIDocShellTreeItem> parent = GetInProcessParentDocshell();
  if (parent) {
    RefPtr<Document> doc = parent->GetDocument();
    if (doc) {
      doc->TryCancelFrameLoaderInitialization(this);
    }
  }

  // Before going any further vet loads initiated by external programs.
  if (aLoadState->HasLoadFlags(LOAD_FLAGS_FROM_EXTERNAL)) {
    MOZ_DIAGNOSTIC_ASSERT(aLoadState->LoadType() == LOAD_NORMAL);

    // Disallow external chrome: loads targetted at content windows
    if (aLoadState->URI()->SchemeIs("chrome")) {
      NS_WARNING("blocked external chrome: url -- use '--chrome' option");
      return NS_ERROR_FAILURE;
    }

    // clear the decks to prevent context bleed-through (bug 298255)
    rv = CreateAboutBlankDocumentViewer(nullptr, nullptr, nullptr, nullptr,
                                        /* aIsInitialDocument */ false);
    if (NS_FAILED(rv)) {
      return NS_ERROR_FAILURE;
    }

    if (Document* doc = GetDocument()) {
      doc->DisallowBFCaching();
    }
  }

  mAllowKeywordFixup = aLoadState->HasInternalLoadFlags(
      INTERNAL_LOAD_FLAGS_ALLOW_THIRD_PARTY_FIXUP);
  mURIResultedInDocument = false;  // reset the clock...

  // If container is an iframe element and will lazy load element steps given
  // container returns true, then stop intersection-observing a lazy loading
  // element container and set container's lazy load resumption steps to null.
  if (IsSubframe()) {
    if (auto* iframe = HTMLIFrameElement::FromNodeOrNull(
            mBrowsingContext->GetEmbedderElement())) {
      // Per spec, reload doesn't cancel lazy loading iframes.
      if (!(aLoadState->LoadType() & LOAD_RELOAD_NORMAL)) {
        iframe->StopLazyLoading(HTMLIFrameElement::TriggerLoad::No);
      }
    }
  }

  // See if this is actually a load between two history entries for the same
  // document. If the process fails, or if we successfully navigate within the
  // same document, return.
  if (sameDocument) {
    nsresult rv = HandleSameDocumentNavigation(
        aLoadState, sameDocumentNavigationState, sameDocument);
    NS_ENSURE_SUCCESS(rv, rv);
    if (shouldTakeFocus) {
      mBrowsingContext->Focus(CallerType::System, IgnoreErrors());
    }
    if (sameDocument) {
      if (aLoadState->LoadIsFromSessionHistory() &&
          (mLoadType & LOAD_CMD_HISTORY) &&
          !aLoadState->IsResumingInterceptedNavigation()) {
        // We must ensure to not abort intercepted history navigations. See
        // https://github.com/whatwg/html/issues/12407
        SetOngoingNavigation(Nothing());
      }
      return rv;
    }
  }

  // mDocumentViewer->PermitUnload can destroy |this| docShell, which
  // causes the next call of CanSavePresentation to crash.
  // Hold onto |this| until we return, to prevent a crash from happening.
  // (bug#331040)
  nsCOMPtr<nsIDocShell> kungFuDeathGrip(this);

  // Don't init timing for javascript:, since it generally doesn't
  // actually start a load or anything.  If it does, we'll init
  // timing then, from OnStateChange.

  // XXXbz mTiming should know what channel it's for, so we don't
  // need this hackery.
  const bool isJavaScript = aLoadState->URI()->SchemeIs("javascript");
  const bool isExternalProtocol =
      nsContentUtils::IsExternalProtocol(aLoadState->URI());
  const bool isDownload = !aLoadState->FileName().IsVoid();
  const bool toBeReset = !isJavaScript && MaybeInitTiming();

  // FIXME(emilio): Should this be done by javascript: uris? What about external
  // protocols?
  if (mTiming && !isDownload) {
    mTiming->NotifyBeforeUnload();
  }

  // The following steps are from https://html.spec.whatwg.org/#navigate
  // Step 19, and here we actually also perform step 2 from
  // #navigate-to-a-javascript:-url (step 20) where the ongoing navigation is
  // set to null.
  SetOngoingNavigation(isJavaScript ? Nothing()
                                    : Some(OngoingNavigation::NavigationID));

  // Step 21
  if (RefPtr<Document> document = GetDocument();
      !aLoadState->LoadIsFromSessionHistory() && document &&
      aLoadState->UserNavigationInvolvement() !=
          UserNavigationInvolvement::BrowserUI &&
      !document->IsInitialDocument() &&
      !NS_IsAboutBlankAllowQueryAndFragment(document->GetDocumentURI()) &&
      NS_IsFetchScheme(aLoadState->URI()) &&
      document->NodePrincipal()->EqualsConsideringDomain(
          aLoadState->TriggeringPrincipal())) {
    if (nsCOMPtr<nsPIDOMWindowInner> window = document->GetInnerWindow()) {
      // Step 21.1
      if (RefPtr<Navigation> navigation = window->Navigation()) {
        AutoJSAPI jsapi;
        if (jsapi.Init(window)) {
          RefPtr<Element> sourceElement = aLoadState->GetSourceElement();

          // Step 21.2
          RefPtr<FormData> formData = aLoadState->GetFormDataEntryList();

          // Step 21.3
          RefPtr<nsIStructuredCloneContainer> navigationAPIStateForFiring =
              aLoadState->GetNavigationAPIState();

          nsCOMPtr<nsIURI> destinationURL = aLoadState->URI();
          // Step 21.4
          RefPtr apiMethodTracker = aLoadState->GetNavigationAPIMethodTracker();
          bool shouldContinue = navigation->FirePushReplaceReloadNavigateEvent(
              jsapi.cx(), aLoadState->GetNavigationType(), destinationURL,
              /* aIsSameDocument */ false,
              Some(aLoadState->UserNavigationInvolvement()), sourceElement,
              formData, navigationAPIStateForFiring,
              /* aClassicHistoryAPIState */ nullptr, apiMethodTracker);

          // Step 21.5
          if (!shouldContinue) {
            return NS_OK;
          }
        }
      }
    }
  }

  // Check if the page doesn't want to be unloaded. The javascript:
  // protocol handler deals with this for javascript: URLs.
  // NOTE(emilio): As of this writing, other browsers fire beforeunload for
  // external protocols, so keep doing that even though they don't return data
  // and thus we won't really unload this...
  if (!isJavaScript && !isDownload &&
      !aLoadState->NotifiedBeforeUnloadListeners() && mDocumentViewer) {
    // Check if request is exempted from HTTPSOnlyMode and if https-first is
    // enabled, if so it means:
    //    * https-first failed to upgrade request to https
    //    * we already asked for permission to unload and the user accepted
    //      otherwise we wouldn't be here.
    const bool isPrivateWin = GetOriginAttributes().IsPrivateBrowsing();
    const uint32_t loadType = aLoadState->LoadType();

    // Check if request is a reload.
    const bool isHistoryOrReload =
        loadType == LOAD_RELOAD_NORMAL ||
        loadType == LOAD_RELOAD_BYPASS_CACHE ||
        loadType == LOAD_RELOAD_BYPASS_PROXY ||
        loadType == LOAD_RELOAD_BYPASS_PROXY_AND_CACHE ||
        loadType == LOAD_HISTORY;

    // If it isn't a reload, the request already failed to be upgraded and
    // https-first is enabled then don't ask the user again for permission to
    // unload and just unload.
    bool okToUnload;
    if (!isHistoryOrReload && aLoadState->IsExemptFromHTTPSFirstMode() &&
        nsHTTPSOnlyUtils::GetUpgradeMode(isPrivateWin) ==
            nsHTTPSOnlyUtils::HTTPS_FIRST_MODE) {
      rv = mDocumentViewer->PermitUnload(
          nsIDocumentViewer::PermitUnloadAction::eDontPromptAndUnload,
          &okToUnload);
    } else {
      rv = mDocumentViewer->PermitUnload(&okToUnload);
      if (mIsBeingDestroyed) {
        // unload handler destroyed this docshell.
        return NS_ERROR_NOT_AVAILABLE;
      }
    }

    if (NS_SUCCEEDED(rv) && !okToUnload) {
      // The user chose not to unload the page, interrupt the
      // load.
      MaybeResetInitTiming(toBeReset);
      return NS_OK;
    }
  }

  if (mTiming && !isDownload) {
    mTiming->NotifyUnloadAccepted(mCurrentURI);
  }

  // In e10s, in the parent process, we refuse to load anything other than
  // "safe" resources that we ship or trust enough to give "special" URLs.
  // Similar check will be performed by the ParentProcessDocumentChannel if in
  // use.
  if (XRE_IsE10sParentProcess() &&
      !DocumentChannel::CanUseDocumentChannel(aLoadState->URI()) &&
      !CanLoadInParentProcess(aLoadState->URI())) {
    return NS_ERROR_FAILURE;
  }

  // Whenever a top-level browsing context is navigated, the user agent MUST
  // lock the orientation of the document to the document's default
  // orientation. We don't explicitly check for a top-level browsing context
  // here because orientation is only set on top-level browsing contexts.
  if (mBrowsingContext->GetOrientationLock() != hal::ScreenOrientation::None) {
    MOZ_ASSERT(mBrowsingContext->IsTop());
    MOZ_ALWAYS_SUCCEEDS(
        mBrowsingContext->SetOrientationLock(hal::ScreenOrientation::None));
    if (mBrowsingContext->IsActive()) {
      ScreenOrientation::UpdateActiveOrientationLock(
          hal::ScreenOrientation::None);
    }
  }

  // Do a check so that we know if there are ongoing requests
  // before calling Stop() below.
  Document* document = GetDocument();
  uint32_t flags = 0;
  if (document && !document->CanSavePresentation(nullptr, flags, true)) {
    // This forces some flags into the WindowGlobalParent's mBFCacheStatus,
    // which we'll then use in CanonicalBrowsingContext::AllowedInBFCache,
    // and in particular we'll store BFCacheStatus::REQUEST if needed.
    // Also, we want to report all the flags to the parent process here (and
    // not just BFCacheStatus::NOT_ALLOWED), so that it can update the
    // telemetry data correctly.
    document->DisallowBFCaching(flags);
  }

  if (aLoadState->LoadIsFromSessionHistory() &&
      (mLoadType & LOAD_CMD_HISTORY)) {
    SetOngoingNavigation(Nothing());
  }

  // Don't stop current network activity for javascript: URL's since they might
  // not result in any data, and thus nothing should be stopped in those cases.
  // In the case where they do result in data, the javascript: URL channel takes
  // care of stopping current network activity. Similarly, downloads don't
  // unload this document...
  if (!isJavaScript && !isDownload && !isExternalProtocol) {
    // Stop any current network activity.
    // Also stop content if this is a zombie doc. otherwise
    // the onload will be delayed by other loads initiated in the
    // background by the first document that
    // didn't fully load before the next load was initiated.
    // If not a zombie, don't stop content until data
    // starts arriving from the new URI...
    if ((mDocumentViewer && mDocumentViewer->GetPreviousViewer()) ||
        LOAD_TYPE_HAS_FLAGS(aLoadState->LoadType(), LOAD_FLAGS_STOP_CONTENT)) {
      rv = StopInternal(nsIWebNavigation::STOP_ALL, UnsetOngoingNavigation::No);
    } else {
      rv = StopInternal(nsIWebNavigation::STOP_NETWORK,
                        UnsetOngoingNavigation::No);
    }

    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  mLoadType = aLoadState->LoadType();

  if (aLoadState->LoadIsFromSessionHistory() &&
      (mLoadType & LOAD_CMD_HISTORY)) {
    if (RefPtr window = GetActiveWindow()) {
      if (RefPtr navigation = window->Navigation()) {
        if (const LoadingSessionHistoryInfo* loadingInfo =
                GetLoadingSessionHistoryInfo()) {
          navigation->CreateNavigationActivationFrom(
              loadingInfo->mPreviousEntry,
              NavigationUtils::NavigationTypeFromLoadType(mLoadType));
        }
      }
    }

    // https://html.spec.whatwg.org/#history-traversal:
    // To traverse the history
    // "If entry has a different Document object than the current entry, then
    // run the following substeps: Remove any tasks queued by the history
    // traversal task source..."
    // Same document object case was handled already above with
    // HandleSameDocumentNavigation call.
    RefPtr<ChildSHistory> shistory = GetRootSessionHistory();
    if (shistory) {
      shistory->RemovePendingHistoryNavigations();
    }
  }

  bool isTopLevelDoc = mBrowsingContext->IsTopContent();

  OriginAttributes attrs = GetOriginAttributes();
  attrs.SetFirstPartyDomain(isTopLevelDoc, aLoadState->URI());

  nsCOMPtr<nsIRequest> req;
  rv = DoURILoad(aLoadState, aCacheKey, getter_AddRefs(req));

  if (NS_SUCCEEDED(rv)) {
    if (shouldTakeFocus) {
      mBrowsingContext->Focus(CallerType::System, IgnoreErrors());
    }
  }

  if (NS_FAILED(rv)) {
    nsCOMPtr<nsIChannel> chan(do_QueryInterface(req));
    UnblockEmbedderLoadEventForFailure();

    // The spec says no exception should be raised for pre-navigation check
    // failures.
    if (NS_ERROR_DOM_SECURITY_ERR == rv) {
      return NS_OK;
    }

    nsCOMPtr<nsIURI> uri = aLoadState->URI();
    if (DisplayLoadError(rv, uri, nullptr, chan) &&
        // FIXME: At this point code was using internal load flags, but checking
        // non-internal load flags?
        aLoadState->HasLoadFlags(LOAD_FLAGS_ERROR_LOAD_CHANGES_RV)) {
      return NS_ERROR_LOAD_SHOWED_ERRORPAGE;
    }

    // We won't report any error if this is an unknown protocol error. The
    // reason behind this is that it will allow enumeration of external
    // protocols if we report an error for each unknown protocol.
    if (NS_ERROR_UNKNOWN_PROTOCOL == rv) {
      return NS_OK;
    }
  }

  return rv;
}

/* static */
bool nsDocShell::CanLoadInParentProcess(nsIURI* aURI) {
  nsCOMPtr<nsIURI> uri = aURI;
  // In e10s, in the parent process, we refuse to load anything other than
  // "safe" resources that we ship or trust enough to give "special" URLs.
  bool canLoadInParent = false;
  if (NS_SUCCEEDED(NS_URIChainHasFlags(
          uri, nsIProtocolHandler::URI_IS_UI_RESOURCE, &canLoadInParent)) &&
      canLoadInParent) {
    // We allow UI resources.
    return true;
  }
  // For about: and extension-based URIs, which don't get
  // URI_IS_UI_RESOURCE, first remove layers of view-source:, if present.
  while (uri && uri->SchemeIs("view-source")) {
    nsCOMPtr<nsINestedURI> nested = do_QueryInterface(uri);
    if (nested) {
      nested->GetInnerURI(getter_AddRefs(uri));
    } else {
      break;
    }
  }
  // Allow about: URIs, execept about:srcdoc, which can include arbitrary code.
  // And allow moz-extension ones if we're running extension content in the
  // parent process.
  if (!uri || (uri->SchemeIs("about") && !NS_IsAboutSrcdoc(uri)) ||
      (!StaticPrefs::extensions_webextensions_remote() &&
       uri->SchemeIs("moz-extension"))) {
    return true;
  }
#ifdef MOZ_THUNDERBIRD
  if (uri->SchemeIs("imap") || uri->SchemeIs("mailbox") ||
      uri->SchemeIs("news") || uri->SchemeIs("nntp") ||
      uri->SchemeIs("snews") || uri->SchemeIs("x-moz-ews") ||
      uri->SchemeIs("x-moz-graph")) {
    return true;
  }
#endif
  nsAutoCString scheme;
  uri->GetScheme(scheme);
  // Allow ext+foo URIs (extension-registered custom protocols). See
  // https://developer.mozilla.org/en-US/docs/Mozilla/Add-ons/WebExtensions/manifest.json/protocol_handlers
  if (StringBeginsWith(scheme, "ext+"_ns) &&
      !StaticPrefs::extensions_webextensions_remote()) {
    return true;
  }
  // Final exception for some legacy automated tests:
  if (xpc::IsInAutomation() &&
      StaticPrefs::security_allow_unsafe_parent_loads()) {
    return true;
  }
  return false;
}

nsIPrincipal* nsDocShell::GetInheritedPrincipal(
    bool aConsiderCurrentDocument, bool aConsiderPartitionedPrincipal) {
  RefPtr<Document> document;
  bool inheritedFromCurrent = false;

  if (aConsiderCurrentDocument && mDocumentViewer) {
    document = mDocumentViewer->GetDocument();
    inheritedFromCurrent = true;
  }

  if (!document) {
    nsCOMPtr<nsIDocShellTreeItem> parentItem;
    GetInProcessSameTypeParent(getter_AddRefs(parentItem));
    if (parentItem) {
      document = parentItem->GetDocument();
    }
  }

  if (!document) {
    if (!aConsiderCurrentDocument) {
      return nullptr;
    }

    // Make sure we end up with _something_ as the principal no matter
    // what.If this fails, we'll just get a null docViewer and bail.
    if (!VerifyDocumentViewer()) {
      return nullptr;
    }
    document = mDocumentViewer->GetDocument();
  }

  //-- Get the document's principal
  if (document) {
    nsIPrincipal* docPrincipal = aConsiderPartitionedPrincipal
                                     ? document->PartitionedPrincipal()
                                     : document->NodePrincipal();

    // Don't allow loads in typeContent docShells to inherit the system
    // principal from existing documents.
    if (inheritedFromCurrent && mItemType == typeContent &&
        docPrincipal->IsSystemPrincipal()) {
      return nullptr;
    }

    return docPrincipal;
  }

  return nullptr;
}

/* static */ nsresult nsDocShell::CreateRealChannelForDocument(
    nsIChannel** aChannel, nsIURI* aURI, nsILoadInfo* aLoadInfo,
    nsIInterfaceRequestor* aCallbacks, nsLoadFlags aLoadFlags,
    const nsAString& aSrcdoc, nsIURI* aBaseURI) {
  nsCOMPtr<nsIChannel> channel;
  if (aSrcdoc.IsVoid()) {
    MOZ_TRY(NS_NewChannelInternal(getter_AddRefs(channel), aURI, aLoadInfo,
                                  nullptr,  // PerformanceStorage
                                  nullptr,  // loadGroup
                                  aCallbacks, aLoadFlags));

    if (aBaseURI) {
      nsCOMPtr<nsIViewSourceChannel> vsc = do_QueryInterface(channel);
      if (vsc) {
        MOZ_ALWAYS_SUCCEEDS(vsc->SetBaseURI(aBaseURI));
      }
    }
  } else if (aURI->SchemeIs("view-source")) {
    // Instantiate view source handler protocol, if it doesn't exist already.
    nsCOMPtr<nsIIOService> io(do_GetIOService());
    MOZ_ASSERT(io);
    nsCOMPtr<nsIProtocolHandler> handler;
    nsresult rv =
        io->GetProtocolHandler("view-source", getter_AddRefs(handler));
    if (NS_FAILED(rv)) {
      return rv;
    }

    nsViewSourceHandler* vsh = nsViewSourceHandler::GetInstance();
    if (!vsh) {
      return NS_ERROR_FAILURE;
    }

    MOZ_TRY(vsh->NewSrcdocChannel(aURI, aBaseURI, aSrcdoc, aLoadInfo,
                                  getter_AddRefs(channel)));
  } else {
    MOZ_RELEASE_ASSERT(NS_IsAboutSrcdoc(aURI));

    MOZ_TRY(NS_NewInputStreamChannelInternal(getter_AddRefs(channel), aURI,
                                             aSrcdoc, "text/html"_ns, aLoadInfo,
                                             true));
    nsCOMPtr<nsIInputStreamChannel> isc = do_QueryInterface(channel);
    MOZ_ASSERT(isc);
    isc->SetBaseURI(aBaseURI);
  }

  if (aLoadFlags != nsIRequest::LOAD_NORMAL) {
    nsresult rv = channel->SetLoadFlags(aLoadFlags);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  channel.forget(aChannel);
  return NS_OK;
}

/* static */ bool nsDocShell::CreateAndConfigureRealChannelForLoadState(
    BrowsingContext* aBrowsingContext, nsDocShellLoadState* aLoadState,
    LoadInfo* aLoadInfo, nsIInterfaceRequestor* aCallbacks,
    nsDocShell* aDocShell, const OriginAttributes& aOriginAttributes,
    nsLoadFlags aLoadFlags, uint32_t aCacheKey, nsresult& aRv,
    nsIChannel** aChannel) {
  MOZ_ASSERT(aLoadInfo);

  nsString srcdoc = VoidString();
  bool isSrcdoc =
      aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_IS_SRCDOC);
  if (isSrcdoc) {
    srcdoc = aLoadState->SrcdocData();
  }

  aLoadInfo->SetTriggeringRemoteType(
      aLoadState->GetEffectiveTriggeringRemoteType());

  if (aLoadState->PrincipalToInherit()) {
    aLoadInfo->SetPrincipalToInherit(aLoadState->PrincipalToInherit());
  }
  aLoadInfo->SetLoadTriggeredFromExternal(
      aLoadState->HasLoadFlags(LOAD_FLAGS_FROM_EXTERNAL));
  aLoadInfo->SetForceAllowDataURI(aLoadState->HasInternalLoadFlags(
      INTERNAL_LOAD_FLAGS_FORCE_ALLOW_DATA_URI));
  aLoadInfo->SetOriginalFrameSrcLoad(
      aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_ORIGINAL_FRAME_SRC));
  aLoadInfo->SetIsNewWindowTarget(
      aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_FIRST_LOAD));
  aLoadInfo->SetForceMediaDocument(aLoadState->GetForceMediaDocument());

  bool inheritAttrs = false;
  if (aLoadState->PrincipalToInherit()) {
    inheritAttrs = nsContentUtils::ChannelShouldInheritPrincipal(
        aLoadState->PrincipalToInherit(), aLoadState->URI(),
        true,  // aInheritForAboutBlank
        isSrcdoc);
  }

  // Strip the target query parameters before creating the channel.
  aLoadState->MaybeStripTrackerQueryStrings(aBrowsingContext);

  OriginAttributes attrs;

  // Inherit origin attributes from PrincipalToInherit if inheritAttrs is
  // true. Otherwise we just use the origin attributes from docshell.
  if (inheritAttrs) {
    MOZ_ASSERT(aLoadState->PrincipalToInherit(),
               "We should have PrincipalToInherit here.");
    attrs = aLoadState->PrincipalToInherit()->OriginAttributesRef();
    // If firstPartyIsolation is not enabled, then PrincipalToInherit should
    // have the same origin attributes with docshell.
    MOZ_ASSERT_IF(!OriginAttributes::IsFirstPartyEnabled(),
                  attrs == aOriginAttributes);
  } else {
    attrs = aOriginAttributes;
    attrs.SetFirstPartyDomain(IsTopLevelDoc(aBrowsingContext, aLoadInfo),
                              aLoadState->URI());
  }

  aRv = aLoadInfo->SetOriginAttributes(attrs);
  if (NS_WARN_IF(NS_FAILED(aRv))) {
    return false;
  }

  if (aLoadState->GetIsFromProcessingFrameAttributes()) {
    aLoadInfo->SetIsFromProcessingFrameAttributes();
  }

  // Propagate the IsFormSubmission flag to the loadInfo.
  if (aLoadState->IsFormSubmission()) {
    aLoadInfo->SetIsFormSubmission(true);
  }

  aLoadInfo->SetUnstrippedURI(aLoadState->GetUnstrippedURI());

  nsCOMPtr<nsIChannel> channel;
  aRv = CreateRealChannelForDocument(getter_AddRefs(channel), aLoadState->URI(),
                                     aLoadInfo, aCallbacks, aLoadFlags, srcdoc,
                                     aLoadState->BaseURI());
  NS_ENSURE_SUCCESS(aRv, false);

  if (!channel) {
    return false;
  }

  // If the HTTPS-Only mode is enabled, every insecure request gets upgraded to
  // HTTPS by default. This behavior can be disabled through the loadinfo flag
  // HTTPS_ONLY_EXEMPT.
  nsHTTPSOnlyUtils::TestSitePermissionAndPotentiallyAddExemption(channel);

  // hack
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(channel));
  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal(
      do_QueryInterface(channel));
  nsCOMPtr<nsIURI> referrer;
  nsIReferrerInfo* referrerInfo = aLoadState->GetReferrerInfo();
  if (referrerInfo) {
    referrerInfo->GetOriginalReferrer(getter_AddRefs(referrer));
  }
  if (httpChannelInternal) {
    if (aLoadState->FirstParty()) {
      aRv = httpChannelInternal->SetDocumentURI(aLoadState->URI());
      MOZ_ASSERT(NS_SUCCEEDED(aRv));
    } else {
      aRv = httpChannelInternal->SetDocumentURI(referrer);
      MOZ_ASSERT(NS_SUCCEEDED(aRv));
    }
    aRv = httpChannelInternal->SetRedirectMode(
        nsIHttpChannelInternal::REDIRECT_MODE_MANUAL);
    MOZ_ASSERT(NS_SUCCEEDED(aRv));
  }

  if (httpChannel) {
    if (aLoadState->HeadersStream()) {
      aRv = AddHeadersToChannel(aLoadState->HeadersStream(), httpChannel);
    }
    // Set the referrer explicitly
    // Referrer is currenly only set for link clicks here.
    if (referrerInfo) {
      aRv = httpChannel->SetReferrerInfo(referrerInfo);
      MOZ_ASSERT(NS_SUCCEEDED(aRv));
    }

    // Mark the http channel as UrgentStart for top level document loading in
    // active tab.
    if (IsUrgentStart(aBrowsingContext, aLoadInfo, aLoadState->LoadType())) {
      nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(channel));
      if (cos) {
        cos->AddClassFlags(nsIClassOfService::UrgentStart);
        if (StaticPrefs::dom_document_priority_incremental()) {
          cos->SetIncremental(true);
        }
      }
    }
  }

  channel->SetOriginalURI(aLoadState->OriginalURI() ? aLoadState->OriginalURI()
                                                    : aLoadState->URI());

  const nsACString& typeHint = aLoadState->TypeHint();
  if (!typeHint.IsVoid()) {
    channel->SetContentType(typeHint);
  }

  const nsAString& fileName = aLoadState->FileName();
  if (!fileName.IsVoid()) {
    aRv = channel->SetContentDisposition(nsIChannel::DISPOSITION_ATTACHMENT);
    NS_ENSURE_SUCCESS(aRv, false);
    if (!fileName.IsEmpty()) {
      aRv = channel->SetContentDispositionFilename(fileName);
      NS_ENSURE_SUCCESS(aRv, false);
    }
  }

  if (nsCOMPtr<nsIWritablePropertyBag2> props = do_QueryInterface(channel)) {
    nsCOMPtr<nsIURI> referrer;
    nsIReferrerInfo* referrerInfo = aLoadState->GetReferrerInfo();
    if (referrerInfo) {
      referrerInfo->GetOriginalReferrer(getter_AddRefs(referrer));
    }
    // save true referrer for those who need it (e.g. xpinstall whitelisting)
    // Currently only http and ftp channels support this.
    props->SetPropertyAsInterface(u"docshell.internalReferrer"_ns, referrer);
  }

  nsCOMPtr<nsICacheInfoChannel> cacheChannel(do_QueryInterface(channel));
  auto loadType = aLoadState->LoadType();

  if (loadType == LOAD_RELOAD_NORMAL &&
      StaticPrefs::
          browser_soft_reload_only_force_validate_top_level_document()) {
    nsCOMPtr<nsICacheInfoChannel> cachingChannel = do_QueryInterface(channel);
    if (cachingChannel) {
      cachingChannel->SetForceValidateCacheContent(true);
    }
  }

  // figure out if we need to set the post data stream on the channel...
  if (aLoadState->PostDataStream()) {
    if (nsCOMPtr<nsIFormPOSTActionChannel> postChannel =
            do_QueryInterface(channel)) {
      // XXX it's a bit of a hack to rewind the postdata stream here but
      // it has to be done in case the post data is being reused multiple
      // times.
      nsCOMPtr<nsISeekableStream> postDataSeekable =
          do_QueryInterface(aLoadState->PostDataStream());
      if (postDataSeekable) {
        aRv = postDataSeekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
        NS_ENSURE_SUCCESS(aRv, false);
      }

      // we really need to have a content type associated with this stream!!
      postChannel->SetUploadStream(aLoadState->PostDataStream(), ""_ns, -1);

      // Ownership of the stream has transferred to the channel, clear our
      // reference.
      aLoadState->SetPostDataStream(nullptr);
    }

    /* If there is a valid postdata *and* it is a History Load,
     * set up the cache key on the channel, to retrieve the
     * data *only* from the cache. If it is a normal reload, the
     * cache is free to go to the server for updated postdata.
     */
    if (cacheChannel && aCacheKey != 0) {
      if (loadType == LOAD_HISTORY || loadType == LOAD_RELOAD_CHARSET_CHANGE) {
        cacheChannel->SetCacheKey(aCacheKey);
        uint32_t loadFlags;
        if (NS_SUCCEEDED(channel->GetLoadFlags(&loadFlags))) {
          channel->SetLoadFlags(loadFlags |
                                nsICachingChannel::LOAD_ONLY_FROM_CACHE);
        }
      } else if (loadType == LOAD_RELOAD_NORMAL) {
        cacheChannel->SetCacheKey(aCacheKey);
      }
    }
  } else {
    /* If there is no postdata, set the cache key on the channel, and
     * do not set the LOAD_ONLY_FROM_CACHE flag, so that the channel
     * will be free to get it from net if it is not found in cache.
     * New cache may use it creatively on CGI pages with GET
     * method and even on those that say "no-cache"
     */
    if (loadType == LOAD_HISTORY || loadType == LOAD_RELOAD_NORMAL ||
        loadType == LOAD_RELOAD_CHARSET_CHANGE ||
        loadType == LOAD_RELOAD_CHARSET_CHANGE_BYPASS_CACHE ||
        loadType == LOAD_RELOAD_CHARSET_CHANGE_BYPASS_PROXY_AND_CACHE) {
      if (cacheChannel && aCacheKey != 0) {
        cacheChannel->SetCacheKey(aCacheKey);
      }
    }
  }

  if (nsCOMPtr<nsIScriptChannel> scriptChannel = do_QueryInterface(channel)) {
    // Allow execution against our context if the principals match
    scriptChannel->SetExecutionPolicy(nsIScriptChannel::EXECUTE_NORMAL);
  }

  if (nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(channel)) {
    nsString initiatorType;
    switch (aLoadInfo->InternalContentPolicyType()) {
      case nsIContentPolicy::TYPE_INTERNAL_EMBED:
        initiatorType = u"embed"_ns;
        break;
      case nsIContentPolicy::TYPE_INTERNAL_OBJECT:
        initiatorType = u"object"_ns;
        break;
      default: {
        const auto& embedderElementType =
            aBrowsingContext->GetEmbedderElementType();
        if (embedderElementType) {
          initiatorType = *embedderElementType;
        }
        break;
      }
    }

    if (!initiatorType.IsEmpty()) {
      timedChannel->SetInitiatorType(initiatorType);
    }
  }

  nsCOMPtr<nsIURI> rpURI;
  aLoadInfo->GetResultPrincipalURI(getter_AddRefs(rpURI));
  Maybe<nsCOMPtr<nsIURI>> originalResultPrincipalURI;
  aLoadState->GetMaybeResultPrincipalURI(originalResultPrincipalURI);
  if (originalResultPrincipalURI &&
      (!aLoadState->KeepResultPrincipalURIIfSet() || !rpURI)) {
    // Unconditionally override, we want the replay to be equal to what has
    // been captured.
    aLoadInfo->SetResultPrincipalURI(originalResultPrincipalURI.ref());
  }

  if (aLoadState->OriginalURI() && aLoadState->LoadReplace()) {
    // The LOAD_REPLACE flag and its handling here will be removed as part
    // of bug 1319110.  For now preserve its restoration here to not break
    // any code expecting it being set specially on redirected channels.
    // If the flag has originally been set to change result of
    // NS_GetFinalChannelURI it won't have any effect and also won't cause
    // any harm.
    uint32_t loadFlags;
    aRv = channel->GetLoadFlags(&loadFlags);
    NS_ENSURE_SUCCESS(aRv, false);
    channel->SetLoadFlags(loadFlags | nsIChannel::LOAD_REPLACE);
  }

  nsCOMPtr<nsIPolicyContainer> policyContainer = aLoadState->PolicyContainer();
  if (nsCOMPtr<nsIContentSecurityPolicy> csp =
          PolicyContainer::GetCSP(policyContainer)) {
    // Navigational requests that are same origin need to be upgraded in case
    // upgrade-insecure-requests is present. Please note that for document
    // navigations that bit is re-computed in case we encounter a server
    // side redirect so the navigation is not same-origin anymore.
    bool upgradeInsecureRequests = false;
    csp->GetUpgradeInsecureRequests(&upgradeInsecureRequests);
    if (upgradeInsecureRequests) {
      // only upgrade if the navigation is same origin
      nsCOMPtr<nsIPrincipal> resultPrincipal;
      aRv = nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
          channel, getter_AddRefs(resultPrincipal));
      NS_ENSURE_SUCCESS(aRv, false);
      if (nsContentSecurityUtils::IsConsideredSameOriginForUIR(
              aLoadState->TriggeringPrincipal(), resultPrincipal)) {
        aLoadInfo->SetUpgradeInsecureRequests(true);
      }
    }
  }

  if (policyContainer) {
    // For document loads we store the policyContainer that potentially needs to
    // be inherited by the new document, e.g. in case we are loading
    // an opaque origin like a data: URI. The actual inheritance
    // check happens within Document::InitPolicyContainer().
    // Please create an actual copy of the policyContainer (do not share the
    // same reference) otherwise modifications done (such as the meta CSP of the
    // new doc) in an opaque origin will incorrectly be propagated to the
    // embedding document.
    RefPtr policyContainerToInherit = MakeRefPtr<PolicyContainer>();
    policyContainerToInherit->InitFromOther(
        PolicyContainer::Cast(policyContainer));
    aLoadInfo->SetPolicyContainerToInherit(policyContainerToInherit);
  }

  channel.forget(aChannel);
  return true;
}

bool nsDocShell::ShouldDoInitialAboutBlankSyncLoad(
    nsIURI* aURI, nsDocShellLoadState* aLoadState,
    nsIPrincipal* aPrincipalToInherit) {
  MOZ_ASSERT(mDocumentViewer);

  if (!NS_IsAboutBlankAllowQueryAndFragment(aURI)) {
    return false;
  }

  if (aLoadState->IsInitialAboutBlankHandlingProhibited()) {
    return false;
  }

  if (mHasStartedLoadingOtherThanInitialBlankURI || !mDocumentViewer ||
      !mDocumentViewer->GetDocument() ||
      !mDocumentViewer->GetDocument()->IsUncommittedInitialDocument()) {
    return false;
  }

  if (!aPrincipalToInherit) {
    MOZ_ASSERT(
        mDocumentViewer->GetDocument()->NodePrincipal()->GetIsNullPrincipal(),
        "Load looks like first load but does not want principal inheritance.");
  } else {
    if (XRE_IsContentProcess()) {
      RefPtr<LoadedOriginSet> loadedOrigins = CurrentLoadedOriginSet();
      if (!loadedOrigins->ValidatePrincipal(aPrincipalToInherit)) {
        // We can't directly load this principal, so we need the normal load
        // path to do a process switch.
        return false;
      }
    }

    // If a page opens about:blank, it will have a content principal.
    // If it is then restored after a restart, we might not have initialized
    // UsesOAC for it. If this is the case, do a normal load (bug 2004165).
    // XXX bug 2005205 tracks removing this workaround.
    if (aLoadState->LoadIsFromSessionHistory() &&
        !mBrowsingContext->Group()
             ->UsesOriginAgentCluster(aPrincipalToInherit)
             .isSome()) {
      return false;
    }
  }

  return true;
}

void nsDocShell::UnsuppressPaintingIfNoNavigationAwayFromAboutBlank(
    mozilla::PresShell* aPresShell) {
  if (mHasStartedLoadingOtherThanInitialBlankURI || !mDocumentViewer) {
    return;
  }
  Document* doc = mDocumentViewer->GetDocument();
  if (!doc || !doc->IsInitialDocument()) {
    return;
  }
  if (mDocumentViewer->GetPresShell() != aPresShell) {
    return;
  }
  // Our surroundings appear to remain in the same state
  // as before posting the runnable.
  aPresShell->UnsuppressPainting();
  // The content viewer's mPresShell could have been removed now, see bug
  // 378682/421432
  if ((aPresShell = mDocumentViewer->GetPresShell())) {
    aPresShell->LoadComplete();
  }
}

nsresult nsDocShell::PerformTrustedTypesPreNavigationCheck(
    nsDocShellLoadState* aLoadState, nsGlobalWindowInner* aWindow) const {
  MOZ_ASSERT(aWindow);
  RefPtr<nsIContentSecurityPolicy> csp =
      PolicyContainer::GetCSP(aWindow->GetPolicyContainer());
  if (csp->GetRequireTrustedTypesForDirectiveState() ==
      RequireTrustedTypesForDirectiveState::NONE) {
    return NS_OK;
  }

  // Exempt web extension content scripts from trusted types policies defined by
  // the page in which they are running.
  if (auto principal = BasePrincipal::Cast(aLoadState->TriggeringPrincipal())) {
    if (principal->ContentScriptAddonPolicyCore()) {
      return NS_OK;
    }
  }

  // If disposion is enforce for require-trusted-types-for, then we return
  // errors in order to block navigation. If it's report-only, errors are
  // ignored and the URL is unchanged.
  bool shouldBlockOnError = csp->GetRequireTrustedTypesForDirectiveState() ==
                            RequireTrustedTypesForDirectiveState::ENFORCE;

  // 2. Let urlString be the result of running the URL serializer on
  // request’s url.
  nsAutoCString urlString;
  aLoadState->URI()->GetSpec(urlString);

  // 3. Let encodedScriptSource be the result of removing the leading
  // "javascript:" from urlString.
  constexpr auto javascriptScheme = "javascript:"_ns;
  const nsDependentCSubstring encodedScriptSource =
      Substring(urlString, javascriptScheme.Length());

  // 4. Let convertedScriptSource be the result of executing Process value
  // with a default policy algorithm
  Maybe<nsAutoString> compliantStringHolder;
  NS_ConvertUTF8toUTF16 encodedScriptSourceUTF16(encodedScriptSource);
  constexpr nsLiteralString sink = u"Location href"_ns;
  auto reportPreNavigationCheckViolations = [&csp, &sink,
                                             &encodedScriptSourceUTF16] {
    // Report violation the same way as for "Should sink type mismatch
    // violation be blocked by Content Security Policy", since that's what
    // other browsers do. See
    // https://github.com/w3c/trusted-types/issues/584.
    auto location = JSCallingLocation::Get();
    TrustedTypeUtils::ReportSinkTypeMismatchViolations(
        csp, nullptr /* aCSPEventListener */, location.FileName(),
        location.mLine, location.mColumn, sink, kTrustedTypesOnlySinkGroup,
        encodedScriptSourceUTF16);
  };
  ErrorResult error;
  auto convertedScriptSource =
      TrustedTypeUtils::GetConvertedScriptSourceForPreNavigationCheck(
          *aWindow, encodedScriptSourceUTF16, sink, compliantStringHolder,
          error);
  error.WouldReportJSException();
  if (error.Failed()) {
    reportPreNavigationCheckViolations();
    if (shouldBlockOnError) {
      RETURN_NSRESULT_ON_FAILURE(error);
    }
    error.SuppressException();
    return NS_OK;
  }

  // 5. Set urlString to be the result of prepending "javascript:" to
  // stringified convertedScriptSource.
  urlString = javascriptScheme + NS_ConvertUTF16toUTF8(*convertedScriptSource);

  // 6. Let newURL be the result of running the URL parser on urlString.
  nsCOMPtr<nsIURI> newURL;
  nsresult rv = NS_NewURI(getter_AddRefs(newURL), urlString);
  if (NS_FAILED(rv)) {
    reportPreNavigationCheckViolations();
    return shouldBlockOnError ? rv : NS_OK;
  }

  // 7. Set request’s url to newURL.
  aLoadState->SetURI(newURL);
  return NS_OK;
}

nsresult nsDocShell::DoURILoad(nsDocShellLoadState* aLoadState,
                               Maybe<uint32_t> aCacheKey,
                               nsIRequest** aRequest) {
  // Double-check that we're still around to load this URI.
  if (mIsBeingDestroyed) {
    // Return NS_OK despite not doing anything to avoid throwing exceptions
    // from nsLocation::SetHref if the unload handler of the existing page
    // tears us down.
    return NS_OK;
  }

  MOZ_DIAGNOSTIC_ASSERT(mInitialized, "Need to initialize before load");
  NS_ENSURE_TRUE(VerifyDocumentViewer(), NS_ERROR_FAILURE);

  nsCOMPtr<nsIURILoader> uriLoader = components::URILoader::Service();
  if (NS_WARN_IF(!uriLoader)) {
    return NS_ERROR_UNEXPECTED;
  }

  // Persist and sync layout history state before we load a new uri, as this
  // might be our last chance to do so, in the content process.
  PersistLayoutHistoryState();
  SynchronizeLayoutHistoryState();

  nsresult rv;
  nsContentPolicyType contentPolicyType = DetermineContentType();

  auto getSourceWindowContext = [this, &aLoadState] {
    const MaybeDiscardedBrowsingContext& sourceBC =
        aLoadState->SourceBrowsingContext();
    if (!sourceBC.IsNullOrDiscarded()) {
      if (WindowContext* wc = sourceBC.get()->GetCurrentWindowContext()) {
        return wc;
      }
    }
    return mBrowsingContext->GetParentWindowContext();
  };

  if (StaticPrefs::dom_security_trusted_types_enabled() &&
      aLoadState->URI()->SchemeIs("javascript")) {
    if (WindowContext* sourceWindowContext = getSourceWindowContext()) {
      RefPtr<nsGlobalWindowInner> window =
          sourceWindowContext->GetInnerWindow();
      rv = PerformTrustedTypesPreNavigationCheck(aLoadState, window);
      // Default policy might destroy the whole docshell, so check that again.
      if (mIsBeingDestroyed) {
        return NS_OK;
      }
      if (NS_FAILED(rv)) {
        return NS_ERROR_DOM_SECURITY_ERR;
      }
    }
  }

  if (IsSubframe()) {
    MOZ_ASSERT(contentPolicyType == nsIContentPolicy::TYPE_INTERNAL_IFRAME ||
                   contentPolicyType == nsIContentPolicy::TYPE_INTERNAL_FRAME,
               "DoURILoad thinks this is a frame and InternalLoad does not");
    if (StaticPrefs::dom_block_external_protocol_in_iframes()) {
      // Only allow URLs able to return data in iframes.
      if (nsContentUtils::IsExternalProtocol(aLoadState->URI())) {
        // The context to check user-interaction with for the purposes of
        // popup-blocking.
        //
        // We generally want to check the context that initiated the navigation.
        WindowContext* sourceWindowContext = getSourceWindowContext();
        MOZ_ASSERT(sourceWindowContext);

        // FIXME: We can't check user-interaction against an OOP window. This is
        // the next best thing we can really do. The load state keeps whether
        // the navigation had a user interaction in process
        // (aLoadState->HasValidUserGestureActivation()), but we can't really
        // consume it, which we want to prevent popup-spamming from the same
        // click event.
        WindowContext* context =
            sourceWindowContext->IsInProcess()
                ? sourceWindowContext
                : mBrowsingContext->GetCurrentWindowContext();
        const bool popupBlocked = [&] {
          const bool active = mBrowsingContext->IsActive();

          // For same-origin-with-top windows, we grant a single free popup
          // without user activation, see bug 1680721.
          //
          // We consume the flag now even if there's no user activation.
          const bool hasFreePass = [&] {
            if (!active ||
                !(context->IsInProcess() && context->SameOriginWithTop())) {
              return false;
            }
            nsGlobalWindowInner* win =
                context->TopWindowContext()->GetInnerWindow();
            return win && win->TryOpenExternalProtocolIframe();
          }();

          if (context->IsInProcess() &&
              context->ConsumeTransientUserGestureActivation()) {
            // If the user has interacted with the page, consume it.
            return false;
          }

          // TODO(emilio): Can we remove this check? It seems like what prompted
          // this code (bug 1514547) should be covered by transient user
          // activation, see bug 1514547.
          if (active &&
              PopupBlocker::ConsumeTimerTokenForExternalProtocolIframe()) {
            return false;
          }

          if (sourceWindowContext->CanShowPopup()) {
            return false;
          }

          if (hasFreePass) {
            return false;
          }

          return true;
        }();

        // No error must be returned when iframes are blocked.
        if (popupBlocked) {
          nsAutoString message;
          nsresult rv = nsContentUtils::GetLocalizedString(
              PropertiesFile::DOM_PROPERTIES,
              "ExternalProtocolFrameBlockedNoUserActivation", message);
          if (NS_SUCCEEDED(rv)) {
            nsContentUtils::ReportToConsoleByWindowID(
                message, nsIScriptError::warningFlag, "DOM"_ns,
                context->InnerWindowId());
          }
          return NS_OK;
        }
      }
    }

    // Only allow view-source scheme in top-level docshells. view-source is
    // the only scheme to which this applies at the moment due to potential
    // timing attacks to read data from cross-origin iframes. If this widens
    // we should add a protocol flag for whether the scheme is allowed in
    // frames and use something like nsNetUtil::NS_URIChainHasFlags.
    nsCOMPtr<nsIURI> tempURI = aLoadState->URI();
    nsCOMPtr<nsINestedURI> nestedURI = do_QueryInterface(tempURI);
    while (nestedURI) {
      // view-source should always be an nsINestedURI, loop and check the
      // scheme on this and all inner URIs that are also nested URIs.
      if (tempURI->SchemeIs("view-source")) {
        return NS_ERROR_UNKNOWN_PROTOCOL;
      }
      nestedURI->GetInnerURI(getter_AddRefs(tempURI));
      nestedURI = do_QueryInterface(tempURI);
    }
  } else {
    MOZ_ASSERT(contentPolicyType == nsIContentPolicy::TYPE_DOCUMENT,
               "DoURILoad thinks this is a document and InternalLoad does not");
  }

  // We want to inherit aLoadState->PrincipalToInherit() when:
  // 1. ChannelShouldInheritPrincipal returns true.
  // 2. aLoadState->URI() is not data: URI, or data: URI is not
  //    configured as unique opaque origin.
  bool inheritPrincipal = false;

  nsCOMPtr<nsIURI> uri = aLoadState->URI();
  if (aLoadState->PrincipalToInherit()) {
    bool isSrcdoc =
        aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_IS_SRCDOC);
    bool inheritAttrs = nsContentUtils::ChannelShouldInheritPrincipal(
        aLoadState->PrincipalToInherit(), uri,
        true,  // aInheritForAboutBlank
        isSrcdoc);

    inheritPrincipal = inheritAttrs && !uri->SchemeIs("data");
  }

  MOZ_ASSERT_IF(NS_IsAboutBlankAllowQueryAndFragment(uri) &&
                    aLoadState->PrincipalToInherit(),
                inheritPrincipal);
  // See https://bugzilla.mozilla.org/show_bug.cgi?id=1736570
  const bool doInitialSyncLoad = ShouldDoInitialAboutBlankSyncLoad(
      uri, aLoadState, aLoadState->PrincipalToInherit());

  if (!doInitialSyncLoad && mBrowsingContext->IsContent()) {
    // https://wicg.github.io/document-picture-in-picture/#close-on-navigate
    // Two exceptions.
    // - The initial navigation is not per-spec and must complete.
    // - We also set IsDocumentPiP on chrome but the spec doesn't apply to it.
    if (Document* doc = GetExtantDocument()) {
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "Close PIP window on navigate", [doc = RefPtr(doc)]() {
            doc->CloseAnyAssociatedDocumentPiPWindows();
          }));
    }
    if (mBrowsingContext->GetIsDocumentPiP()) {
      return NS_OK;
    }
  }

  // FIXME We still have a ton of codepaths that don't pass through
  //       DocumentLoadListener, so probably need to create session history info
  //       in more places.
  if (aLoadState->GetLoadingSessionHistoryInfo()) {
    SetLoadingSessionHistoryInfo(*aLoadState->GetLoadingSessionHistoryInfo());
  } else if (doInitialSyncLoad) {
    // Materialize LoadingSessionHistoryInfo here, because DocumentChannel
    // loads have it, and later history behavior depends on it existing.
    UniquePtr<SessionHistoryInfo> entry = MakeUnique<SessionHistoryInfo>(
        uri, aLoadState->TriggeringPrincipal(),
        aLoadState->PrincipalToInherit(),
        aLoadState->PartitionedPrincipalToInherit(),
        aLoadState->PolicyContainer(), mContentTypeHint);
    entry->SetTransient();
    mozilla::dom::LoadingSessionHistoryInfo info(*entry);
    if (Navigation::IsAPIEnabled()) {
      info.mContiguousEntries.AppendElement(*entry);
    }
    SetLoadingSessionHistoryInfo(info, true);
  }

  // open a channel for the url

  // If we have a pending channel, use the channel we've already created here.
  // We don't need to set up load flags for our channel, as it has already been
  // created.

  if (nsCOMPtr<nsIChannel> channel =
          aLoadState->GetPendingRedirectedChannel()) {
    // If we have a request outparameter, shove our channel into it.
    if (aRequest) {
      nsCOMPtr<nsIRequest> outRequest = channel;
      outRequest.forget(aRequest);
    }

    mHasStartedLoadingOtherThanInitialBlankURI = true;
    return OpenRedirectedChannel(aLoadState);
  }

  // There are two cases we care about:
  // * Top-level load: In this case, loadingNode is null, but loadingWindow
  //   is our mScriptGlobal. We pass null for loadingPrincipal in this case.
  // * Subframe load: loadingWindow is null, but loadingNode is the frame
  //   element for the load. loadingPrincipal is the NodePrincipal of the
  //   frame element.
  nsCOMPtr<nsINode> loadingNode;
  nsCOMPtr<nsPIDOMWindowOuter> loadingWindow;
  nsCOMPtr<nsIPrincipal> loadingPrincipal;
  nsCOMPtr<nsISupports> topLevelLoadingContext;

  if (contentPolicyType == nsIContentPolicy::TYPE_DOCUMENT) {
    loadingNode = nullptr;
    loadingPrincipal = nullptr;
    loadingWindow = mScriptGlobal;
    if (XRE_IsContentProcess()) {
      // In e10s the child process doesn't have access to the element that
      // contains the browsing context (because that element is in the chrome
      // process).
      nsCOMPtr<nsIBrowserChild> browserChild = GetBrowserChild();
      topLevelLoadingContext = ToSupports(browserChild);
    } else {
      // This is for loading non-e10s tabs and toplevel windows of various
      // sorts.
      // For the toplevel window cases, requestingElement will be null.
      nsCOMPtr<Element> requestingElement =
          loadingWindow->GetFrameElementInternal();
      topLevelLoadingContext = requestingElement;
    }
  } else {
    loadingWindow = nullptr;
    loadingNode = mScriptGlobal->GetFrameElementInternal();
    if (loadingNode) {
      // If we have a loading node, then use that as our loadingPrincipal.
      loadingPrincipal = loadingNode->NodePrincipal();
#ifdef DEBUG
      // Get the docshell type for requestingElement.
      RefPtr<Document> requestingDoc = loadingNode->OwnerDoc();
      nsCOMPtr<nsIDocShell> elementDocShell = requestingDoc->GetDocShell();
      // requestingElement docshell type = current docshell type.
      MOZ_ASSERT(
          mItemType == elementDocShell->ItemType(),
          "subframes should have the same docshell type as their parent");
#endif
    } else {
      if (mIsBeingDestroyed) {
        // If this isn't a top-level load and mScriptGlobal's frame element is
        // null, then the element got removed from the DOM while we were trying
        // to load this resource. This docshell is scheduled for destruction
        // already, so bail out here.
        return NS_OK;
      }
      // If we are not being destroyed and we do not have access to the loading
      // node, then we are a remote subframe. Set the loading principal
      // to be a null principal and then set it correctly in the parent.
      loadingPrincipal = NullPrincipal::Create(GetOriginAttributes(), nullptr);
    }
  }

  if (!aLoadState->TriggeringPrincipal()) {
    MOZ_ASSERT(false, "DoURILoad needs a valid triggeringPrincipal");
    return NS_ERROR_FAILURE;
  }

  uint32_t sandboxFlags = mBrowsingContext->GetSandboxFlags();
  nsSecurityFlags securityFlags =
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL;

  if (mLoadType == LOAD_ERROR_PAGE) {
    securityFlags |= nsILoadInfo::SEC_LOAD_ERROR_PAGE;
  }

  if (inheritPrincipal) {
    securityFlags |= nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL;
  }

  // Must never have a parent for TYPE_DOCUMENT loads
  MOZ_ASSERT_IF(contentPolicyType == nsIContentPolicy::TYPE_DOCUMENT,
                !mBrowsingContext->GetParent());
  // Subdocuments must have a parent
  MOZ_ASSERT_IF(contentPolicyType == nsIContentPolicy::TYPE_SUBDOCUMENT,
                mBrowsingContext->GetParent());
  mBrowsingContext->SetTriggeringAndInheritPrincipals(
      aLoadState->TriggeringPrincipal(), aLoadState->PrincipalToInherit(),
      aLoadState->GetLoadIdentifier());
  RefPtr<LoadInfo> loadInfo;
  if (contentPolicyType == nsIContentPolicy::TYPE_DOCUMENT) {
    loadInfo = MakeRefPtr<LoadInfo>(
        loadingWindow, uri, aLoadState->TriggeringPrincipal(),
        topLevelLoadingContext, securityFlags, sandboxFlags);
  } else {
    loadInfo = MOZ_TRY(LoadInfo::Create(
        loadingPrincipal, aLoadState->TriggeringPrincipal(), loadingNode,
        securityFlags, contentPolicyType, Maybe<mozilla::dom::ClientInfo>(),
        Maybe<mozilla::dom::ServiceWorkerDescriptor>(), sandboxFlags));
  }
  RefPtr<WindowContext> context = mBrowsingContext->GetCurrentWindowContext();

  if (doInitialSyncLoad) {
    // Stay on the eagerly created document and adjust it to match what we would
    // be loading.
    return CompleteInitialAboutBlankLoad(aLoadState, loadInfo);
  }
  mHasStartedLoadingOtherThanInitialBlankURI = true;

  if (mLoadType != LOAD_ERROR_PAGE && context && context->IsInProcess()) {
    if (context->HasValidTransientUserGestureActivation()) {
      aLoadState->SetHasValidUserGestureActivation(true);
      aLoadState->SetTextDirectiveUserActivation(true);
    }
    if (!aLoadState->TriggeringWindowId()) {
      aLoadState->SetTriggeringWindowId(context->Id());
    }
    if (!aLoadState->TriggeringStorageAccess()) {
      Document* contextDoc = context->GetExtantDoc();
      if (contextDoc) {
        aLoadState->SetTriggeringStorageAccess(
            contextDoc->UsingStorageAccess());
      }
    }
  }

  // in case this docshell load was triggered by a valid transient user gesture,
  // or also the load originates from external, then we pass that information on
  // to the loadinfo, which allows e.g. setting Sec-Fetch-User request headers.
  if (aLoadState->HasValidUserGestureActivation() ||
      aLoadState->HasLoadFlags(LOAD_FLAGS_FROM_EXTERNAL)) {
    loadInfo->SetHasValidUserGestureActivation(true);
    aLoadState->SetTextDirectiveUserActivation(true);
  }

  loadInfo->SetTextDirectiveUserActivation(
      aLoadState->GetTextDirectiveUserActivation());

  loadInfo->SetTriggeringWindowId(aLoadState->TriggeringWindowId());
  loadInfo->SetTriggeringStorageAccess(aLoadState->TriggeringStorageAccess());
  loadInfo->SetTriggeringSandboxFlags(aLoadState->TriggeringSandboxFlags());
  net::ClassificationFlags flags = aLoadState->TriggeringClassificationFlags();
  loadInfo->SetTriggeringFirstPartyClassificationFlags(flags.firstPartyFlags);
  loadInfo->SetTriggeringThirdPartyClassificationFlags(flags.thirdPartyFlags);
  loadInfo->SetIsMetaRefresh(aLoadState->IsMetaRefresh());

  uint32_t cacheKey = 0;
  if (aCacheKey) {
    cacheKey = *aCacheKey;
  } else {
    if (mLoadingEntry) {
      cacheKey = mLoadingEntry->mInfo.GetCacheKey();
    } else if (mActiveEntry) {  // for reload cases
      cacheKey = mActiveEntry->GetCacheKey();
    }
  }

  bool uriModified;
  if (mLoadingEntry) {
    uriModified = mLoadingEntry->mInfo.GetURIWasModified();
  } else {
    uriModified = false;
  }

  bool isEmbeddingBlockedError = false;
  if (mFailedChannel) {
    nsresult status;
    mFailedChannel->GetStatus(&status);
    isEmbeddingBlockedError = status == NS_ERROR_XFO_VIOLATION ||
                              status == NS_ERROR_CSP_FRAME_ANCESTOR_VIOLATION;
  }

  nsLoadFlags loadFlags = aLoadState->CalculateChannelLoadFlags(
      mBrowsingContext, uriModified, Some(isEmbeddingBlockedError));

  nsCOMPtr<nsIChannel> channel;
  if (DocumentChannel::CanUseDocumentChannel(uri)) {
    channel = DocumentChannel::CreateForDocument(
        aLoadState, loadInfo, loadFlags, this, cacheKey, uriModified,
        isEmbeddingBlockedError);
    MOZ_ASSERT(channel);

    // Disable keyword fixup when using DocumentChannel, since
    // DocumentLoadListener will handle this for us (in the parent process).
    mAllowKeywordFixup = false;
  } else if (!CreateAndConfigureRealChannelForLoadState(
                 mBrowsingContext, aLoadState, loadInfo, this, this,
                 GetOriginAttributes(), loadFlags, cacheKey, rv,
                 getter_AddRefs(channel))) {
    return rv;
  }

  // Make sure to give the caller a channel if we managed to create one
  // This is important for correct error page/session history interaction
  if (aRequest) {
    NS_ADDREF(*aRequest = channel);
  }

  const nsACString& typeHint = aLoadState->TypeHint();
  if (!typeHint.IsVoid()) {
    mContentTypeHint = typeHint;
  } else {
    mContentTypeHint.Truncate();
  }

  // Load attributes depend on load type...
  if (mLoadType == LOAD_RELOAD_CHARSET_CHANGE) {
    // Use SetAllowStaleCacheContent (not LOAD_FROM_CACHE flag) since we
    // only want to force cache load for this channel, not the whole
    // loadGroup.
    nsCOMPtr<nsICacheInfoChannel> cachingChannel = do_QueryInterface(channel);
    if (cachingChannel) {
      cachingChannel->SetAllowStaleCacheContent(true);
    }
  }

  uint32_t openFlags =
      nsDocShell::ComputeURILoaderFlags(mBrowsingContext, mLoadType);
  return OpenInitializedChannel(channel, uriLoader, openFlags);
}

nsresult nsDocShell::CompleteInitialAboutBlankLoad(
    nsDocShellLoadState* aLoadState, nsILoadInfo* aLoadInfo) {
  nsresult rv;
  // Match the DocumentChannel case where the default for third-partiness
  // differs from the default in LoadInfo construction here.
  BrowsingContext* top = mBrowsingContext->Top();
  if (top == mBrowsingContext) {
    aLoadInfo->SetIsThirdPartyContextToTopWindow(false);
  } else {
    bool thirdParty = true;
    if (Document* topDoc = top->GetExtantDocument();
        topDoc && aLoadState->PrincipalToInherit()) {
      (void)topDoc->GetPrincipal()->IsThirdPartyPrincipal(
          aLoadState->PrincipalToInherit(), &thirdParty);
    }
    aLoadInfo->SetIsThirdPartyContextToTopWindow(thirdParty);
  }

  if (!mDocumentViewer) {
    MOZ_ASSERT(false, "How did the viewer go away?");
    return NS_ERROR_FAILURE;
  }
  RefPtr<Document> doc = mDocumentViewer->GetDocument();
  MOZ_LOG(gDocShellLog, LogLevel::Debug,
          ("nsDocShell[%p]::DoURILoad sync about:blank onto initial "
           "about:blank. Document[%p]\n",
           this, doc.get()));
  if (!doc) {
    MOZ_ASSERT(false, "How did the document go away?");
    return NS_ERROR_FAILURE;
  }

  // Get the load event fired for the initial about:blank without starting
  // a real load from a channel. We still need a channel object even though
  // we don't care about reading from the channel.
  nsCOMPtr<nsIChannel> aboutBlankChannel;
  rv = NS_NewChannelInternal(getter_AddRefs(aboutBlankChannel),
                             aLoadState->URI(), aLoadInfo, nullptr, mLoadGroup,
                             nullptr, nsIChannel::LOAD_DOCUMENT_URI);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!aboutBlankChannel) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrincipal> expectedPrincipal = aLoadState->PrincipalToInherit();
  nsCOMPtr<nsIPrincipal> expectedPartitionedPrincipal = expectedPrincipal;
  // If we don't have a content principal, also use it as partitioned principal
  if (expectedPrincipal && expectedPrincipal->GetIsContentPrincipal()) {
    // The partitioned content principal is always isolated.
    StoragePrincipalHelper::Create(
        aboutBlankChannel, expectedPrincipal, /* aForceIsolation */ true,
        getter_AddRefs(expectedPartitionedPrincipal));
  }

  const bool principalMismatch =
      expectedPrincipal && !expectedPrincipal->Equals(doc->GetPrincipal());
  MOZ_ASSERT_IF(!expectedPrincipal, doc->GetPrincipal()->GetIsNullPrincipal());

  // The channel would sandbox aLoadState->PrincipalToInherit(). Even if
  // the document already has a null principal, we don't know if it's the right
  // sandboxed one. So be safe and clobber.
  const uint32_t sandboxFlags =
      mBrowsingContext->GetHasLoadedNonInitialDocument()
          ? mBrowsingContext->GetSandboxFlags()
          : mBrowsingContext->GetInitialSandboxFlags();
  const bool shouldBeSandboxed = sandboxFlags & SANDBOXED_ORIGIN;
  MOZ_ASSERT_IF(shouldBeSandboxed, expectedPrincipal);

  // Clobber document before completing the synchronous load if it doesn't have
  // the right principal (bug 1979032)
  if (principalMismatch || shouldBeSandboxed) {
    // This will sandbox the principals as needed
    nsCOMPtr<nsIPolicyContainer> policyContainer =
        aLoadState->PolicyContainer();
    nsCOMPtr<nsIURI> base = doc->GetDocBaseURI();
    rv = CreateAboutBlankDocumentViewer(
        expectedPrincipal, expectedPartitionedPrincipal, policyContainer, base,
        /* aIsInitialDocument */ true);
    NS_ENSURE_SUCCESS(rv, rv);

    doc = mDocumentViewer->GetDocument();
    MOZ_ASSERT(doc);
    MOZ_LOG(gDocShellLog, LogLevel::Warning,
            ("nsDocShell[%p] sync about:blank principals don't match, create "
             "new document. Document[%p] \n",
             this, doc.get()));
  }

  MOZ_ASSERT(doc->IsInitialDocument(),
             "How come the doc is no longer the initial one?");

  MOZ_ASSERT(doc->GetReadyStateEnum() == Document::READYSTATE_COMPLETE);
  MOZ_ASSERT(!mIsLoadingDocument);

  if (nsIContentSecurityPolicy* csp =
          PolicyContainer::GetCSP(doc->GetPolicyContainer())) {
    // We do this here rather than when inheriting the CSP in
    // CreateAboutBlankDocumentViewer so that client source and parsed policies
    // use the parent's URI as self (bug 2021482, 2035423).
    MOZ_TRY(csp->SetRequestContextWithDocument(doc));
  }
  doc->ApplyCspFromLoadInfo(aLoadInfo);
  doc->ApplySettingsFromCSP(false);
  doc->RecomputeResistFingerprinting();

  rv = doc->GetWindowContext()->SetIsOriginalFrameSource(
      aLoadState->HasInternalLoadFlags(INTERNAL_LOAD_FLAGS_ORIGINAL_FRAME_SRC));
  NS_ENSURE_SUCCESS(rv, rv);

  nsPIDOMWindowInner* innerWindow = doc->GetInnerWindow();
  if (innerWindow) {
    mozilla::dom::ClientSource* clientSource =
        nsGlobalWindowInner::Cast(innerWindow)->GetClientSource();
    // See if we don't have a controller but the parent has gained a
    // controller.
    if (clientSource && clientSource->GetController().isNothing()) {
      MaybeInheritController(
          clientSource,
          StoragePrincipalHelper::ShouldUsePartitionPrincipalForServiceWorker(
              this)
              ? doc->PartitionedPrincipal()
              : doc->GetPrincipal());
    }
  }

  MOZ_ASSERT(!mIsLoadingDocument);
  MOZ_ASSERT(!mDocumentRequest);

  // Call OnStartRequest so that nsDocLoader sets mIsLoadingDocument and fire
  // state start
  OnStartRequest(aboutBlankChannel);

  MOZ_ASSERT(mIsLoadingDocument);
  MOZ_ASSERT(mDocumentRequest == aboutBlankChannel);
  MOZ_ASSERT(!doc->InitialAboutBlankLoadCompleting());

  doc->BeginInitialAboutBlankLoadCompleting(aboutBlankChannel);
  auto resetLoadCompleting =
      MakeScopeExit([&] { doc->EndInitialAboutBlankLoadCompleting(); });

  mCurrentURI = aLoadState->URI();
  doc->SetDocumentURI(aLoadState->URI());

  // Normal documents fire the location change at content viewer creation.
  // The initial about:blank does not do that at content viewer creation,
  // so that the UI isn't bothered about the initial about:blank if there's
  // immediate navigation away. However, now that the initial about:blank is
  // going to remain in this docshell, we need to let to the UI know about it
  // (at least in the top-level case).
  FireOnLocationChange(this, aboutBlankChannel, aLoadState->URI(), 0);

  MoveLoadingToActiveEntry(false, 0, nullptr);

  doc->BeginLoad();

  nsContentUtils::AddScriptRunner(
      MakeAndAddRef<nsDocElementCreatedNotificationRunner>(doc));
  // When scripts are not blocked (are they ever blocked here?), the runnable
  // runs immediately, so let's check if this docshell got destroyed or the
  // document got swapped. Unclear if this ever happens; this is a defensive
  // check.
  if (mIsBeingDestroyed || !mDocumentViewer ||
      doc != mDocumentViewer->GetDocument()) {
    return NS_OK;
  }

  // Initialize the presShell here in the window.open() case.
  RefPtr<PresShell> presShell = doc->GetPresShell();
  if (presShell && !presShell->DidInitialize()) {
    rv = presShell->Initialize();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  doc->SetScrollToRef(doc->GetDocumentURI());

  OnStopRequest(aboutBlankChannel, NS_OK);

  // Mechanisms in Document will force a load from EndLoad()
  // even if there are still blockers.
  doc->EndLoad();
  // Can't assert any postcondition, because the load event
  // handler may have started loading something new in this
  // docshell.

  return NS_OK;
}

static nsresult AppendSegmentToString(nsIInputStream* aIn, void* aClosure,
                                      const char* aFromRawSegment,
                                      uint32_t aToOffset, uint32_t aCount,
                                      uint32_t* aWriteCount) {
  // aFromSegment now contains aCount bytes of data.

  nsAutoCString* buf = static_cast<nsAutoCString*>(aClosure);
  buf->Append(aFromRawSegment, aCount);

  // Indicate that we have consumed all of aFromSegment
  *aWriteCount = aCount;
  return NS_OK;
}

/* static */ nsresult nsDocShell::AddHeadersToChannel(
    nsIInputStream* aHeadersData, nsIChannel* aGenericChannel) {
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aGenericChannel);
  NS_ENSURE_STATE(httpChannel);

  uint32_t numRead;
  nsAutoCString headersString;
  nsresult rv = aHeadersData->ReadSegments(
      AppendSegmentToString, &headersString, UINT32_MAX, &numRead);
  NS_ENSURE_SUCCESS(rv, rv);

  // used during the manipulation of the String from the InputStream
  nsAutoCString headerName;
  nsAutoCString headerValue;
  int32_t crlf;
  int32_t colon;

  //
  // Iterate over the headersString: for each "\r\n" delimited chunk,
  // add the value as a header to the nsIHttpChannel
  //

  static const char kWhitespace[] = "\b\t\r\n ";
  while (true) {
    crlf = headersString.Find("\r\n");
    if (crlf == kNotFound) {
      return NS_OK;
    }

    const nsACString& oneHeader = StringHead(headersString, crlf);

    colon = oneHeader.FindChar(':');
    if (colon == kNotFound) {
      return NS_ERROR_UNEXPECTED;
    }

    headerName = StringHead(oneHeader, colon);
    headerValue = Substring(oneHeader, colon + 1);

    headerName.Trim(kWhitespace);
    headerValue.Trim(kWhitespace);

    headersString.Cut(0, crlf + 2);

    //
    // FINALLY: we can set the header!
    //

    rv = httpChannel->SetRequestHeader(headerName, headerValue, true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  MOZ_ASSERT_UNREACHABLE("oops");
  return NS_ERROR_UNEXPECTED;
}

/* static */ uint32_t nsDocShell::ComputeURILoaderFlags(
    BrowsingContext* aBrowsingContext, uint32_t aLoadType,
    bool aIsDocumentLoad) {
  MOZ_ASSERT(aBrowsingContext);

  uint32_t openFlags = 0;
  if (aLoadType == LOAD_LINK) {
    openFlags |= nsIURILoader::IS_CONTENT_PREFERRED;
  }
  if (!aBrowsingContext->GetAllowContentRetargeting()) {
    openFlags |= nsIURILoader::DONT_RETARGET;
  }

  if (!aIsDocumentLoad) {
    openFlags |= nsIURILoader::IS_OBJECT_EMBED;

    // Unless the pref is set, object/embed loads always specify DONT_RETARGET.
    // See bug 1868001 for details.
    if (!StaticPrefs::dom_navigation_object_embed_allow_retargeting()) {
      openFlags |= nsIURILoader::DONT_RETARGET;
    }
  }

  return openFlags;
}

nsresult nsDocShell::OpenInitializedChannel(nsIChannel* aChannel,
                                            nsIURILoader* aURILoader,
                                            uint32_t aOpenFlags) {
  nsresult rv = NS_OK;

  // If anything fails here, make sure to clear our initial ClientSource.
  auto cleanupInitialClient =
      MakeScopeExit([&] { mInitialClientSource.reset(); });

  nsCOMPtr<nsPIDOMWindowOuter> win = GetWindow();
  NS_ENSURE_TRUE(win, NS_ERROR_FAILURE);

  MaybeCreateInitialClientSource();

  // Let the client channel helper know if we are using DocumentChannel,
  // since redirects get handled in the parent process in that case.
  RefPtr<net::DocumentChannel> docChannel = do_QueryObject(aChannel);
  if (docChannel && XRE_IsContentProcess()) {
    // Tell the content process nsDocumentOpenInfo to not try to do
    // any sort of targeting.
    aOpenFlags |= nsIURILoader::DONT_RETARGET;
  }

  // Since we are loading a document we need to make sure the proper reserved
  // and initial client data is stored on the nsILoadInfo.  The
  // ClientChannelHelper does this and ensures that it is propagated properly
  // on redirects.  We pass no reserved client here so that the helper will
  // create the reserved ClientSource if necessary.
  Maybe<ClientInfo> noReservedClient;
  if (docChannel) {
    // When using DocumentChannel, all redirect handling is done in the parent,
    // so we just need the child variant to watch for the internal redirect
    // to the final channel.
    rv = AddClientChannelHelperInChild(aChannel,
                                       GetMainThreadSerialEventTarget());
    docChannel->SetInitialClientInfo(GetInitialClientInfo());
  } else {
    rv = AddClientChannelHelper(aChannel, std::move(noReservedClient),
                                GetInitialClientInfo(),
                                GetMainThreadSerialEventTarget());
  }
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aURILoader->OpenURI(aChannel, aOpenFlags, this);
  NS_ENSURE_SUCCESS(rv, rv);

  // We're about to load a new page and it may take time before necko
  // gives back any data, so main thread might have a chance to process a
  // collector slice
  nsJSContext::MaybeRunNextCollectorSlice(this, JS::GCReason::DOCSHELL);

  // Success.  Keep the initial ClientSource if it exists.
  cleanupInitialClient.release();

  return NS_OK;
}

nsresult nsDocShell::OpenRedirectedChannel(nsDocShellLoadState* aLoadState) {
  nsCOMPtr<nsIChannel> channel = aLoadState->GetPendingRedirectedChannel();
  MOZ_ASSERT(channel);

  // If anything fails here, make sure to clear our initial ClientSource.
  auto cleanupInitialClient =
      MakeScopeExit([&] { mInitialClientSource.reset(); });

  nsCOMPtr<nsPIDOMWindowOuter> win = GetWindow();
  NS_ENSURE_TRUE(win, NS_ERROR_FAILURE);

  MaybeCreateInitialClientSource();

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();

  LoadInfo* li = static_cast<LoadInfo*>(loadInfo.get());
  if (loadInfo->GetExternalContentPolicyType() ==
      ExtContentPolicy::TYPE_DOCUMENT) {
    li->UpdateBrowsingContextID(mBrowsingContext->Id());
  } else if (loadInfo->GetExternalContentPolicyType() ==
             ExtContentPolicy::TYPE_SUBDOCUMENT) {
    li->UpdateFrameBrowsingContextID(mBrowsingContext->Id());
  }

  // If we did a process switch, then we should have an existing allocated
  // ClientInfo, so we just need to allocate a corresponding ClientSource.
  CreateReservedSourceIfNeeded(channel, GetMainThreadSerialEventTarget());

  uint32_t documentOpenInfoFlags = nsIURILoader::DONT_RETARGET;
  if (loadInfo->GetExternalContentPolicyType() ==
      ExtContentPolicy::TYPE_OBJECT) {
    documentOpenInfoFlags |= nsIURILoader::IS_OBJECT_EMBED;
  }

  RefPtr loader =
      MakeRefPtr<nsDocumentOpenInfo>(this, documentOpenInfoFlags, nullptr);
  channel->SetLoadGroup(mLoadGroup);

  MOZ_ALWAYS_SUCCEEDS(loader->Prepare());

  nsresult rv = NS_OK;
  if (XRE_IsParentProcess()) {
    // If we're in the parent, the we don't have an nsIChildChannel, just
    // the original channel, which is already open in this process.

    // DocumentLoadListener expects to get an nsIParentChannel, so
    // we create a wrapper around the channel and nsIStreamListener
    // that forwards functionality as needed, and then we register
    // it under the provided identifier.
    RefPtr wrapper = MakeRefPtr<ParentChannelWrapper>(channel, loader);
    // We're in the parent process, so the redirect is owned by the parent
    // process (ContentParentId 0).
    wrapper->Register(aLoadState->GetPendingRedirectChannelRegistrarId(), 0);

    mLoadGroup->AddRequest(channel, nullptr);
  } else if (nsCOMPtr<nsIChildChannel> childChannel =
                 do_QueryInterface(channel)) {
    // Our channel was redirected from another process, so doesn't need to
    // be opened again. However, it does need its listener hooked up
    // correctly.
    rv = childChannel->CompleteRedirectSetup(loader);
  } else {
    // It's possible for the redirected channel to not implement
    // nsIChildChannel and be entirely local (like srcdoc). In that case we
    // can just open the local instance and it will work.
    rv = channel->AsyncOpen(loader);
  }
  if (rv == NS_ERROR_NO_CONTENT) {
    return NS_OK;
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // Success.  Keep the initial ClientSource if it exists.
  cleanupInitialClient.release();
  return NS_OK;
}

// https://html.spec.whatwg.org/#scrolling-to-a-fragment
nsresult nsDocShell::ScrollToAnchor(bool aCurHasRef, bool aNewHasRef,
                                    nsACString& aNewHash, uint32_t aLoadType) {
  if (!mCurrentURI) {
    return NS_OK;
  }

  RefPtr<PresShell> presShell = GetPresShell();
  if (!presShell) {
    // If we failed to get the shell, or if there is no shell,
    // nothing left to do here.
    return NS_OK;
  }

  ScrollContainerFrame* rootScroll = presShell->GetRootScrollContainerFrame();
  if (rootScroll) {
    rootScroll->ClearDidHistoryRestore();
  }

  // If it's a load from history, we don't have any anchor jumping to do.
  // Scrollbar position will be restored by the caller based on positions stored
  // in session history.
  bool scroll = aLoadType != LOAD_HISTORY && aLoadType != LOAD_RELOAD_NORMAL;
  // If the load contains text directives, try to apply them. This may fail if
  // the load is a same-document load that was initiated before the document was
  // fully loaded and the target is not yet included in the DOM tree.
  // For this case, the `uninvokedTextDirectives` are not cleared, so that
  // `Document::ScrollToRef()` can re-apply the text directive.
  // `Document::ScrollToRef()` is (presumably) the second "async" call mentioned
  // in sec. 7.4.2.3.3 in the HTML spec, "Fragment navigations":
  // https://html.spec.whatwg.org/#scroll-to-fragid:~:text=This%20algorithm%20will%20be%20called%20twice

  const RefPtr fragmentDirective = GetDocument()->FragmentDirective();
  const nsTArray<RefPtr<nsRange>> textDirectiveRanges =
      fragmentDirective->FindTextFragmentsInDocument();
  fragmentDirective->HighlightTextDirectives(textDirectiveRanges);
  const bool scrollToTextDirective =
      !textDirectiveRanges.IsEmpty() &&
      fragmentDirective->IsTextDirectiveAllowedToBeScrolledTo();
  const RefPtr<nsRange> textDirectiveToScroll =
      scrollToTextDirective ? textDirectiveRanges[0] : nullptr;

  // If we have no new anchor, we do not want to scroll, unless there is a
  // current anchor and we are doing a history load.  So return if we have no
  // new anchor, and there is no current anchor or the load is not a history
  // load.
  if ((!aCurHasRef || aLoadType != LOAD_HISTORY) && !aNewHasRef &&
      !scrollToTextDirective) {
    return NS_OK;
  }

  // Both the new and current URIs refer to the same page. We can now
  // browse to the hash stored in the new URI.

  if (aNewHash.IsEmpty() && !scrollToTextDirective) {
    // 2. If fragment is the empty string, then return the special value top of
    // the document.
    //
    // Tell the shell it's at an anchor without scrolling.
    presShell->GoToAnchor(u""_ns, nullptr, false);

    if (scroll) {
      // Scroll to the top of the page. Ignore the return value; failure to
      // scroll here (e.g. if there is no root scrollframe) is not grounds for
      // canceling the load!
      SetCurScrollPosEx(0, 0);
    }

    return NS_OK;
  }

  // 3. Let potentialIndicatedElement be the result of finding a potential
  // indicated element given document and fragment.
  NS_ConvertUTF8toUTF16 uStr(aNewHash);

  MOZ_ASSERT(!uStr.IsEmpty() || scrollToTextDirective);

  auto rv = presShell->GoToAnchor(uStr, textDirectiveToScroll, scroll,
                                  ScrollFlags::ScrollSmoothAuto);

  // 4. If potentialIndicatedElement is not null, then return
  // potentialIndicatedElement.
  if (NS_SUCCEEDED(rv)) {
    return NS_OK;
  }

  // 5. Let fragmentBytes be the result of percent-decoding fragment.
  nsAutoCString fragmentBytes;
  const bool unescaped = NS_UnescapeURL(aNewHash.Data(), aNewHash.Length(),
                                        /* aFlags = */ 0, fragmentBytes);

  if (!unescaped) {
    // Another attempt is only necessary if characters were unescaped.
    return NS_OK;
  }

  if (fragmentBytes.IsEmpty()) {
    // When aNewHash contains "%00", the unescaped string may be empty, and
    // GoToAnchor asserts if we ask it to scroll to an empty ref.
    presShell->GoToAnchor(u""_ns, nullptr, false);
    return NS_OK;
  }

  // 6. Let decodedFragment be the result of running UTF-8 decode without BOM on
  // fragmentBytes.
  nsAutoString decodedFragment;
  rv = UTF_8_ENCODING->DecodeWithoutBOMHandling(fragmentBytes, decodedFragment);
  NS_ENSURE_SUCCESS(rv, rv);

  // 7. Set potentialIndicatedElement to the result of finding a potential
  // indicated element given document and decodedFragment.
  //
  // Ignore the return value of GoToAnchor, since it will return an error if
  // there is no such anchor in the document, which is actually a success
  // condition for us (we want to update the session history with the new URI no
  // matter whether we actually scrolled somewhere).
  presShell->GoToAnchor(decodedFragment, nullptr, scroll,
                        ScrollFlags::ScrollSmoothAuto);

  return NS_OK;
}

bool nsDocShell::OnNewURI(nsIURI* aURI, nsIChannel* aChannel,
                          nsIPrincipal* aTriggeringPrincipal,
                          nsIPrincipal* aPrincipalToInherit,
                          nsIPrincipal* aPartitionedPrincipalToInherit,
                          nsIPolicyContainer* aPolicyContainer,
                          bool aAddToGlobalHistory, bool aCloneSHChildren) {
  MOZ_ASSERT(aURI, "uri is null");
  MOZ_ASSERT(!aChannel || !aTriggeringPrincipal, "Shouldn't have both set");

  MOZ_ASSERT(!aPrincipalToInherit ||
             (aPrincipalToInherit && aTriggeringPrincipal));

#if defined(DEBUG)
  if (MOZ_LOG_TEST(gDocShellLog, LogLevel::Debug)) {
    nsAutoCString chanName;
    if (aChannel) {
      aChannel->GetName(chanName);
    } else {
      chanName.AssignLiteral("<no channel>");
    }

    MOZ_LOG(gDocShellLog, LogLevel::Debug,
            ("nsDocShell[%p]::OnNewURI(\"%s\", [%s], 0x%x)\n", this,
             aURI->GetSpecOrDefault().get(), chanName.get(), mLoadType));
  }
#endif

  bool equalUri = false;

  // Get the post data and the HTTP response code from the channel.
  uint32_t responseStatus = 0;
  nsCOMPtr<nsIInputStream> inputStream;
  if (aChannel) {
    nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aChannel));

    // Check if the HTTPChannel is hiding under a multiPartChannel
    if (!httpChannel) {
      GetHttpChannel(aChannel, getter_AddRefs(httpChannel));
    }

    if (httpChannel) {
      nsCOMPtr<nsIUploadChannel> uploadChannel(do_QueryInterface(httpChannel));
      if (uploadChannel) {
        uploadChannel->GetUploadStream(getter_AddRefs(inputStream));
      }
    }
  }

  // Determine if this type of load should update history.
  bool updateGHistory = ShouldUpdateGlobalHistory(mLoadType);

  // We don't update session history on reload unless we're loading
  // an iframe in shift-reload case.
  [[maybe_unused]]
  bool updateSHistory = mBrowsingContext->ShouldUpdateSessionHistory(mLoadType);

  // Create SH Entry (mLSHE) only if there is a SessionHistory object in the
  // root browsing context.
  // FIXME If session history in the parent is enabled then we only do this if
  //       the session history object is in process, otherwise we can't really
  //       use the mLSHE anyway. Once session history is only stored in the
  //       parent then this code will probably be removed anyway.
  RefPtr<ChildSHistory> rootSH = GetRootSessionHistory();
  if (!rootSH) {
    updateSHistory = false;
    updateGHistory = false;  // XXX Why global history too?
  }

  // Check if the url to be loaded is the same as the one already loaded.
  if (mCurrentURI) {
    aURI->Equals(mCurrentURI, &equalUri);
  }

#ifdef DEBUG
  bool shAvailable = (rootSH != nullptr);

  // XXX This log message is almost useless because |updateSHistory|
  //     and |updateGHistory| are not correct at this point.

  MOZ_LOG(gDocShellLog, LogLevel::Debug,
          ("  shAvailable=%i updateSHistory=%i updateGHistory=%i"
           " equalURI=%i\n",
           shAvailable, updateSHistory, updateGHistory, equalUri));
#endif

  /* If the url to be loaded is the same as the one already there,
   * and the original loadType is LOAD_NORMAL, LOAD_LINK, or
   * LOAD_STOP_CONTENT, set loadType to LOAD_NORMAL_REPLACE so that
   * AddToSessionHistory() won't mess with the current SHEntry and
   * if this page has any frame children, it also will be handled
   * properly. see bug 83684
   *
   * NB: If mOSHE is null but we have a current URI, then it probably
   * means that we must be at the transient about:blank content viewer;
   * we should let the normal load continue, since there's nothing to
   * replace. Sometimes this happens after a session restore (eg process
   * switch) and mCurrentURI is not about:blank; we assume we can let the load
   * continue (Bug 1301399).
   *
   * XXX Hopefully changing the loadType at this time will not hurt
   *  anywhere. The other way to take care of sequentially repeating
   *  frameset pages is to add new methods to nsIDocShellTreeItem.
   * Hopefully I don't have to do that.
   */
  if (equalUri && mActiveEntry &&
      (mLoadType == LOAD_NORMAL || mLoadType == LOAD_LINK ||
       mLoadType == LOAD_STOP_CONTENT) &&
      !inputStream) {
    mLoadType = LOAD_NORMAL_REPLACE;
  }

  /* If the user pressed shift-reload, cache will create a new cache key
   * for the page. Save the new cacheKey in Session History.
   * see bug 90098
   */
  if (aChannel && IsForceReloadType(mLoadType)) {
    MOZ_ASSERT(!updateSHistory || IsSubframe(),
               "We shouldn't be updating session history for forced"
               " reloads unless we're in a newly created iframe!");

    nsCOMPtr<nsICacheInfoChannel> cacheChannel(do_QueryInterface(aChannel));
    uint32_t cacheKey = 0;
    // Get the Cache Key and store it in SH.
    if (cacheChannel) {
      cacheChannel->GetCacheKey(&cacheKey);
    }
    // If we already have a loading history entry, store the new cache key
    // in it.  Otherwise, since we're doing a reload and won't be updating
    // our history entry, store the cache key in our current history entry.
    SetCacheKeyOnHistoryEntry(cacheKey);
  }

  if (ShouldAddURIVisit(aChannel) && updateGHistory && aAddToGlobalHistory) {
    nsCOMPtr<nsIURI> previousURI;
    uint32_t previousFlags = 0;

    if (mLoadType & LOAD_CMD_RELOAD) {
      // On a reload request, we don't set redirecting flags.
      previousURI = aURI;
    } else {
      ExtractLastVisit(aChannel, getter_AddRefs(previousURI), &previousFlags);
    }

    AddURIVisit(aURI, previousURI, previousFlags, responseStatus,
                net::ChannelIsPost(aChannel));
  }

  // aCloneSHChildren exactly means "we are not loading a new document".
  uint32_t locationFlags =
      aCloneSHChildren ? uint32_t(LOCATION_CHANGE_SAME_DOCUMENT) : 0;

  bool onLocationChangeNeeded =
      SetCurrentURI(aURI, aChannel, false,
                    /* aIsInitialAboutBlank */ false, locationFlags);
  // Make sure to store the referrer from the channel, if any
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aChannel));
  if (httpChannel) {
    mReferrerInfo = httpChannel->GetReferrerInfo();
  }
  return onLocationChangeNeeded;
}

Maybe<Wireframe> nsDocShell::GetWireframe() {
  const bool collectWireFrame =
      StaticPrefs::browser_history_collectWireframes() &&
      mBrowsingContext->IsTopContent() && mActiveEntry;

  if (!collectWireFrame) {
    return Nothing();
  }

  RefPtr<Document> doc = mDocumentViewer->GetDocument();
  Nullable<Wireframe> wireframe;
  doc->GetWireframeWithoutFlushing(false, wireframe);
  if (wireframe.IsNull()) {
    return Nothing();
  }
  return Some(wireframe.Value());
}

bool nsDocShell::CollectWireframe() {
  Maybe<Wireframe> wireframe = GetWireframe();
  if (wireframe.isNothing()) {
    return false;
  }

  if (XRE_IsParentProcess()) {
    SessionHistoryEntry* entry =
        mBrowsingContext->Canonical()->GetActiveSessionHistoryEntry();
    if (entry) {
      entry->SetWireframe(wireframe);
    }
  } else {
    (void)ContentChild::GetSingleton()->SendSessionHistoryEntryWireframe(
        mBrowsingContext, wireframe.ref());
  }

  return true;
}

//*****************************************************************************
// nsDocShell: Session History
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::AddState(JS::Handle<JS::Value> aData, const nsAString& aTitle,
                     const nsAString& aURL, bool aReplace, JSContext* aCx) {
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("nsDocShell[%p]: AddState(..., %s, %s, %d)", this,
           NS_ConvertUTF16toUTF8(aTitle).get(),
           NS_ConvertUTF16toUTF8(aURL).get(), aReplace));
  // Implements History.pushState and History.replaceState

  // Here's what we do, roughly in the order specified by HTML5.  The specific
  // steps we are executing are at
  // <https://html.spec.whatwg.org/multipage/history.html#dom-history-pushstate>,
  // <https://html.spec.whatwg.org/#shared-history-push/replace-state-steps>,
  // and
  // <https://html.spec.whatwg.org/multipage/history.html#url-and-history-update-steps>.
  // This function basically implements #dom-history-pushstate and
  // UpdateURLAndHistory implements #url-and-history-update-steps.
  //
  // A. Serialize aData using structured clone.  This is #dom-history-pushstate
  //    step 5.
  // B. If the third argument is present, #dom-history-pushstate step 7.
  //     7.1. Resolve the url, relative to our document.
  //     7.2. If (a) fails, raise a SECURITY_ERR
  //     7.4. Compare the resulting absolute URL to the document's address.  If
  //          any part of the URLs difer other than the <path>, <query>, and
  //          <fragment> components, raise a SECURITY_ERR and abort.
  // C. If !aReplace, #url-and-history-update-steps steps 2.1-2.3:
  //     Remove from the session history all entries after the current entry,
  //     as we would after a regular navigation, and save the current
  //     entry's scroll position (bug 590573).
  // D. #url-and-history-update-steps step 2.4 or step 3.  As apropriate,
  //    either add a state object entry to the session history after the
  //    current entry with the following properties, or modify the current
  //    session history entry to set
  //      a. cloned data as the state object,
  //      b. if the third argument was present, the absolute URL found in
  //         step 2
  //    Also clear the new history entry's POST data (see bug 580069).
  // E. If aReplace is false (i.e. we're doing a pushState instead of a
  //    replaceState), notify bfcache that we've navigated to a new page.
  // F. If the third argument is present, set the document's current address
  //    to the absolute URL found in step B.  This is
  //    #url-and-history-update-steps step 4.
  //
  // It's important that this function not run arbitrary scripts after step A
  // and before completing step E.  For example, if a script called
  // history.back() before we completed step E, bfcache might destroy an
  // active content viewer.  Since EvictOutOfRangeDocumentViewers at the end of
  // step E might run script, we can't just put a script blocker around the
  // critical section.
  //
  // Note that we completely ignore the aTitle parameter.

  nsresult rv;

  RefPtr<Document> document = GetDocument();
  NS_ENSURE_TRUE(document, NS_ERROR_FAILURE);

  // Don't clobber the load type of an existing network load.
  Maybe<AutoRestore<uint32_t>> loadTypeResetter;
  if (StaticPrefs::
          docshell_shistory_sameDocumentNavigationOverridesLoadType() &&
      !document->NodePrincipal()->IsURIInPrefList(
          "docshell.shistory.sameDocumentNavigationOverridesLoadType."
          "forceDisable")) {
    loadTypeResetter.emplace(mLoadType);
  }

  // pushState effectively becomes replaceState when we've started a network
  // load but haven't adopted its document yet.  This mirrors what we do with
  // changes to the hash at this stage of the game.
  if (JustStartedNetworkLoad()) {
    if (!loadTypeResetter.isSome()) {
      loadTypeResetter.emplace(mLoadType);
    }
    aReplace = true;
  }

  // Step A: Serialize aData using structured clone.
  // https://html.spec.whatwg.org/multipage/history.html#dom-history-pushstate
  // step 5.
  nsCOMPtr<nsIStructuredCloneContainer> scContainer;

  // scContainer->Init might cause arbitrary JS to run, and this code might
  // navigate the page we're on, potentially to a different origin! (bug
  // 634834)  To protect against this, we abort if our principal changes due
  // to the InitFromJSVal() call.
  {
    RefPtr<Document> origDocument = GetDocument();
    if (!origDocument) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }
    nsCOMPtr<nsIPrincipal> origPrincipal = origDocument->NodePrincipal();

    scContainer = new nsStructuredCloneContainer();
    rv = scContainer->InitFromJSVal(aData, aCx);
    NS_ENSURE_SUCCESS(rv, rv);

    RefPtr<Document> newDocument = GetDocument();
    if (!newDocument) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }
    nsCOMPtr<nsIPrincipal> newPrincipal = newDocument->NodePrincipal();

    bool principalsEqual = false;
    origPrincipal->Equals(newPrincipal, &principalsEqual);
    NS_ENSURE_TRUE(principalsEqual, NS_ERROR_DOM_SECURITY_ERR);
  }

  // Check that the state object isn't too long.
  int32_t maxStateObjSize = StaticPrefs::browser_history_maxStateObjectSize();
  if (maxStateObjSize < 0) {
    maxStateObjSize = 0;
  }

  uint64_t scSize;
  rv = scContainer->GetSerializedNBytes(&scSize);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ENSURE_TRUE(scSize <= (uint32_t)maxStateObjSize, NS_ERROR_ILLEGAL_VALUE);

  // Step B: Resolve aURL.
  // https://html.spec.whatwg.org/multipage/history.html#dom-history-pushstate
  // step 7.
  bool equalURIs = true;
  nsCOMPtr<nsIURI> currentURI;
  if (mCurrentURI) {
    currentURI = nsIOService::CreateExposableURI(mCurrentURI);
  } else {
    currentURI = mCurrentURI;
  }
  nsCOMPtr<nsIURI> newURI;
  if (aURL.Length() == 0) {
    newURI = currentURI;
  } else {
    // 7.1: Resolve aURL relative to mURI

    nsIURI* docBaseURI = document->GetDocBaseURI();
    if (!docBaseURI) {
      return NS_ERROR_FAILURE;
    }

    nsAutoCString spec;
    docBaseURI->GetSpec(spec);

    rv = NS_NewURI(getter_AddRefs(newURI), aURL,
                   document->GetDocumentCharacterSet(), docBaseURI);

    // 7.2: If 2a fails, raise a SECURITY_ERR
    if (NS_FAILED(rv)) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }

    if (!document->CanRewriteURL(newURI)) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }

    if (currentURI) {
      currentURI->Equals(newURI, &equalURIs);
    } else {
      equalURIs = false;
    }

  }  // end of same-origin check

  // https://html.spec.whatwg.org/#shared-history-push/replace-state-steps
  // Step 8
  if (nsCOMPtr<nsPIDOMWindowInner> window = document->GetInnerWindow()) {
    if (RefPtr<Navigation> navigation = window->Navigation()) {
      bool shouldContinue = navigation->FirePushReplaceReloadNavigateEvent(
          aCx, aReplace ? NavigationType::Replace : NavigationType::Push,
          newURI,
          /* aIsSameDocument */ true,
          /* aUserInvolvement */ Nothing(),
          /* aSourceElement */ nullptr, /* aFormDataEntryList */ nullptr,
          /* aNavigationAPIState */ nullptr, scContainer);

      // Step 9
      if (!shouldContinue) {
        return NS_OK;
      }
    }
  }

  // Step 10
  // Run #url-and-history-update-steps
  rv = UpdateURLAndHistory(document, newURI, scContainer,
                           aReplace ? NavigationHistoryBehavior::Replace
                                    : NavigationHistoryBehavior::Push,
                           currentURI, equalURIs);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult nsDocShell::UpdateURLAndHistory(
    Document* aDocument, nsIURI* aNewURI, nsIStructuredCloneContainer* aData,
    NavigationHistoryBehavior aHistoryHandling, nsIURI* aCurrentURI,
    bool aEqualURIs, bool aFiredNavigateEvent) {
  MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug, "UpdateURLAndHistory {}",
              aHistoryHandling);

  // Implements
  // https://html.spec.whatwg.org/multipage/history.html#url-and-history-update-steps
  MOZ_ASSERT(aHistoryHandling != NavigationHistoryBehavior::Auto);
  bool isReplace = aHistoryHandling == NavigationHistoryBehavior::Replace;

  // If we have a pending title change, handle it before creating a new entry.
  aDocument->DoNotifyPossibleTitleChange();

  // Step 2, if aReplace is false: Create a new entry in the session
  // history. This will erase all SHEntries after the new entry and make this
  // entry the current one.
  NS_ENSURE_TRUE(mActiveEntry || isReplace, NS_ERROR_FAILURE);

  // If this push/replaceState changed the document's current URI and the new
  // URI differs from the old URI in more than the hash, or if the old
  // SHEntry's URI was modified in this way by a push/replaceState call
  // set URIWasModified to true for the current SHEntry (bug 669671).
  bool sameExceptHashes = true;
  aNewURI->EqualsExceptRef(aCurrentURI, &sameExceptHashes);
  bool uriWasModified;
  if (sameExceptHashes) {
    uriWasModified = mActiveEntry && mActiveEntry->GetURIWasModified();
  } else {
    uriWasModified = true;
  }

  mLoadType = LOAD_PUSHSTATE;

  RefPtr<SessionHistoryEntry> newSHEntry;
  if (!isReplace) {
    // Step 2.

    // Step 2.2, "Remove any tasks queued by the history traversal task
    // source that are associated with any Document objects in the
    // top-level browsing context's document family."  This is very hard in
    // SessionHistoryInParent since we can't synchronously access the
    // pending navigations that are already sent to the parent. We can
    // abort any AsyncGo navigations that are waiting to be sent.  If we
    // send a message to the parent, it would be processed after any
    // navigations previously sent.  So long as we consider the "history
    // traversal task source" to be the list in this process we match the
    // spec.  If we move the entire list to the parent, we can handle the
    // aborting of loads there, but we don't have a way to synchronously
    // remove entries as we do here for non-SHIP.
    RefPtr<ChildSHistory> shistory = GetRootSessionHistory();
    if (shistory) {
      shistory->RemovePendingHistoryNavigations();
    }

    nsPoint scrollPos = GetCurScrollPos();

    bool scrollRestorationIsManual;
    // FIXME Need to save the current scroll position on mActiveEntry.
    scrollRestorationIsManual = mActiveEntry->GetScrollRestorationIsManual();

    nsCOMPtr<nsIPolicyContainer> policyContainer =
        aDocument->GetPolicyContainer();

    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("nsDocShell %p UpdateActiveEntry (not replacing)", this));

    nsString title(mActiveEntry->GetTitle());
    nsCOMPtr<nsIReferrerInfo> referrerInfo = mActiveEntry->GetReferrerInfo();

    UpdateActiveEntry(false,
                      /* aPreviousScrollPos = */ Some(scrollPos), aNewURI,
                      /* aOriginalURI = */ nullptr,
                      /* aReferrerInfo = */ referrerInfo,
                      /* aTriggeringPrincipal = */ aDocument->NodePrincipal(),
                      policyContainer, title, scrollRestorationIsManual, aData,
                      uriWasModified);
  } else {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("nsDocShell %p UpdateActiveEntry (replacing) mActiveEntry %p",
             this, mActiveEntry.get()));
    // Setting the resultPrincipalURI to nullptr is fine here: it will cause
    // NS_GetFinalChannelURI to use the originalURI as the URI, which is aNewURI
    // in our case.  We could also set it to aNewURI, with the same result.
    // We don't use aTitle here, see bug 544535.
    nsString title;
    nsCOMPtr<nsIReferrerInfo> referrerInfo;
    if (mActiveEntry) {
      title = mActiveEntry->GetTitle();
      referrerInfo = mActiveEntry->GetReferrerInfo();
    } else {
      referrerInfo = nullptr;
    }
    UpdateActiveEntry(
        true, /* aPreviousScrollPos = */ Nothing(), aNewURI, aNewURI,
        /* aReferrerInfo = */ referrerInfo, aDocument->NodePrincipal(),
        aDocument->GetPolicyContainer(), title,
        mActiveEntry && mActiveEntry->GetScrollRestorationIsManual(), aData,
        uriWasModified);
  }

  // Step 4: If the document's URI changed, update document's URI and update
  // global history.
  //
  // We need to call FireOnLocationChange so that the browser's address bar
  // gets updated and the back button is enabled, but we only need to
  // explicitly call FireOnLocationChange if we're not calling SetCurrentURI,
  // since SetCurrentURI will call FireOnLocationChange for us.
  //
  // Both SetCurrentURI(...) and FireDummyOnLocationChange() pass
  // nullptr for aRequest param to FireOnLocationChange(...). Such an update
  // notification is allowed only when we know docshell is not loading a new
  // document and it requires LOCATION_CHANGE_SAME_DOCUMENT flag. Otherwise,
  // FireOnLocationChange(...) breaks security UI.
  //
  // If the docshell is shutting down, don't update the document URI, as we
  // can't load into a docshell that is being destroyed.
  if (!aEqualURIs && !mIsBeingDestroyed) {
    aDocument->SetDocumentURI(aNewURI);
    SetCurrentURI(aNewURI, nullptr, /* aFireLocationChange */ true,
                  /* aIsInitialAboutBlank */ false,
                  GetSameDocumentNavigationFlags(aNewURI));

    AddURIVisit(aNewURI, aCurrentURI, 0);

    // AddURIVisit doesn't set the title for the new URI in global history,
    // so do that here.
    UpdateGlobalHistoryTitle(aNewURI);

    // Inform the favicon service that our old favicon applies to this new
    // URI.
    CopyFavicon(aCurrentURI, aNewURI, UsePrivateBrowsing());
  } else {
    FireDummyOnLocationChange();
  }
  aDocument->SetStateObject(aData);

  if (RefPtr navigation = aDocument->GetInnerWindow()->Navigation()) {
    MOZ_LOG(gNavigationAPILog, LogLevel::Debug,
            ("nsDocShell %p triggering a navigation event for a same-document "
             "navigation from UpdateURLAndHistory -> isReplace: %s",
             this, isReplace ? "true" : "false"));
    // Step 11: Update the navigation API entries for a same-document
    // navigation given document's relevant global object's navigation API,
    // newEntry, and historyHandling.
    navigation->UpdateEntriesForSameDocumentNavigation(
        mActiveEntry.get(),
        isReplace ? NavigationType::Replace : NavigationType::Push,
        aFiredNavigateEvent);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetCurrentScrollRestorationIsManual(bool* aIsManual) {
  *aIsManual = mActiveEntry && mActiveEntry->GetScrollRestorationIsManual();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetCurrentScrollRestorationIsManual(bool aIsManual) {
  SetScrollRestorationIsManualOnHistoryEntry(aIsManual);

  return NS_OK;
}

void nsDocShell::SetScrollRestorationIsManualOnHistoryEntry(bool aIsManual) {
  if (mActiveEntry && mBrowsingContext) {
    mActiveEntry->SetScrollRestorationIsManual(aIsManual);
    if (XRE_IsParentProcess()) {
      SessionHistoryEntry* entry =
          mBrowsingContext->Canonical()->GetActiveSessionHistoryEntry();
      if (entry) {
        entry->SetScrollRestorationIsManual(aIsManual);
      }
    } else {
      (void)ContentChild::GetSingleton()
          ->SendSessionHistoryEntryScrollRestorationIsManual(mBrowsingContext,
                                                             aIsManual);
    }
  }
}

void nsDocShell::SetCacheKeyOnHistoryEntry(uint32_t aCacheKey) {
  if (mActiveEntry && mBrowsingContext) {
    mActiveEntry->SetCacheKey(aCacheKey);
    if (XRE_IsParentProcess()) {
      SessionHistoryEntry* entry =
          mBrowsingContext->Canonical()->GetActiveSessionHistoryEntry();
      if (entry) {
        entry->SetCacheKey(aCacheKey);
      }
    } else {
      (void)ContentChild::GetSingleton()->SendSessionHistoryEntryCacheKey(
          mBrowsingContext, aCacheKey);
    }
  }
}

void nsDocShell::UpdateActiveEntry(
    bool aReplace, const Maybe<nsPoint>& aPreviousScrollPos, nsIURI* aURI,
    nsIURI* aOriginalURI, nsIReferrerInfo* aReferrerInfo,
    nsIPrincipal* aTriggeringPrincipal, nsIPolicyContainer* aPolicyContainer,
    const nsAString& aTitle, bool aScrollRestorationIsManual,
    nsIStructuredCloneContainer* aData, bool aURIWasModified) {
  MOZ_ASSERT(aURI, "uri is null");
  MOZ_ASSERT(mLoadType == LOAD_PUSHSTATE,
             "This code only deals with pushState");
  MOZ_ASSERT_IF(aPreviousScrollPos.isSome(), !aReplace);

  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("Creating an active entry on nsDocShell %p to %s", this,
           aURI->GetSpecOrDefault().get()));

  // Even if we're replacing an existing entry we create new a
  // SessionHistoryInfo. In the parent process we'll keep the existing
  // SessionHistoryEntry, but just replace its SessionHistoryInfo, that way the
  // entry keeps identity but its data is replaced.
  bool replace = aReplace && mActiveEntry;

  if (!replace) {
    CollectWireframe();
  }

  UniquePtr<SessionHistoryInfo> previousActiveEntry(mActiveEntry.release());
  if (previousActiveEntry) {
    // Link this entry to the previous active entry.
    mActiveEntry = MakeUnique<SessionHistoryInfo>(*previousActiveEntry, aURI);
  } else {
    Document* doc = GetDocument();
    MOZ_ASSERT(doc);
    mActiveEntry = MakeUnique<SessionHistoryInfo>(
        aURI, aTriggeringPrincipal, doc->NodePrincipal(), nullptr,
        aPolicyContainer, mContentTypeHint);
  }
  mActiveEntry->SetOriginalURI(aOriginalURI);
  mActiveEntry->SetUnstrippedURI(nullptr);
  mActiveEntry->SetReferrerInfo(aReferrerInfo);
  mActiveEntry->SetTitle(aTitle);
  mActiveEntry->SetStateData(static_cast<nsStructuredCloneContainer*>(aData));
  mActiveEntry->SetURIWasModified(aURIWasModified);
  mActiveEntry->SetScrollRestorationIsManual(aScrollRestorationIsManual);

  if (replace) {
    mActiveEntry->NavigationKey() = previousActiveEntry->NavigationKey();
    mBrowsingContext->ReplaceActiveSessionHistoryEntry(mActiveEntry.get());
  } else {
    mBrowsingContext->IncrementHistoryEntryCountForBrowsingContext();
    // FIXME We should probably just compute mChildOffset in the parent
    //       instead of passing it over IPC here.
    mBrowsingContext->SetActiveSessionHistoryEntry(
        aPreviousScrollPos, mActiveEntry.get(), previousActiveEntry.get(),
        mLoadType,
        /* aCacheKey = */ 0);
    // FIXME Do we need to update mPreviousEntryIndex and mLoadedEntryIndex?
  }
}

nsresult nsDocShell::LoadHistoryEntry(const LoadingSessionHistoryInfo& aEntry,
                                      uint32_t aLoadType, bool aUserActivation,
                                      bool aNotifiedBeforeUnloadListeners,
                                      bool aIsResumingInterceptedNavigation) {
  RefPtr<nsDocShellLoadState> loadState = aEntry.CreateLoadInfo();
  loadState->SetHasValidUserGestureActivation(
      loadState->HasValidUserGestureActivation() || aUserActivation);

  loadState->SetTextDirectiveUserActivation(
      loadState->GetTextDirectiveUserActivation() || aUserActivation);

  loadState->SetNotifiedBeforeUnloadListeners(aNotifiedBeforeUnloadListeners);

  loadState->SetIsResumingInterceptedNavigation(
      aIsResumingInterceptedNavigation);

  return LoadHistoryEntry(loadState, aLoadType, aEntry.mLoadingCurrentEntry);
}

void nsDocShell::MaybeFireTraverseHistory(nsDocShellLoadState* aLoadState) {
  if (!Navigation::IsAPIEnabled()) {
    return;
  }

  BrowsingContext* browsingContext = GetBrowsingContext();
  if (!browsingContext || browsingContext->IsTop()) {
    return;
  }

  if (!mActiveEntry || !aLoadState->GetLoadingSessionHistoryInfo() ||
      aLoadState->IsResumingInterceptedNavigation()) {
    return;
  }
  if (mActiveEntry->NavigationKey() ==
      aLoadState->GetLoadingSessionHistoryInfo()->mInfo.NavigationKey()) {
    return;
  }

  nsCOMPtr activeURI = mActiveEntry->GetURIOrInheritedForAboutBlank();
  nsCOMPtr<nsIURI> loadingURI = aLoadState->GetLoadingSessionHistoryInfo()
                                    ->mInfo.GetURIOrInheritedForAboutBlank();
  if (NS_FAILED(nsContentUtils::GetSecurityManager()->CheckSameOriginURI(
          activeURI, loadingURI,
          /*reportError=*/false,
          /*fromPrivateWindow=*/false))) {
    return;
  }

  if (RefPtr window = GetActiveWindow()) {
    if (RefPtr navigation = window->Navigation()) {
      if (AutoJSAPI jsapi; jsapi.Init(window)) {
        // This should send the correct user involvment. See bug 1903552.
        navigation->FireTraverseNavigateEvent(jsapi.cx(), aLoadState,
                                              Nothing());
      }
    }
  }
}

nsIDocumentViewer::PermitUnloadResult
nsDocShell::MaybeFireTraversableTraverseHistory(
    nsDocShellLoadState* aLoadState,
    Maybe<UserNavigationInvolvement> aUserInvolvement) {
  MOZ_DIAGNOSTIC_ASSERT(GetBrowsingContext());
  MOZ_DIAGNOSTIC_ASSERT(GetBrowsingContext()->IsTop());

  SetOngoingNavigation(Some(OngoingNavigation::Traversal));

  nsIDocumentViewer::PermitUnloadResult finalStatus =
      nsIDocumentViewer::eContinue;
  if (RefPtr<nsPIDOMWindowInner> activeWindow = GetActiveWindow()) {
    if (RefPtr navigation = activeWindow->Navigation()) {
      if (AutoJSAPI jsapi; jsapi.Init(activeWindow)) {
        bool shouldContinue = navigation->FireTraverseNavigateEvent(
            jsapi.cx(), aLoadState, aUserInvolvement);

        if (!shouldContinue) {
          finalStatus = nsIDocumentViewer::eCanceledByNavigate;
        }
      }
    }
  }

  return finalStatus;
}

nsresult nsDocShell::LoadHistoryEntry(nsDocShellLoadState* aLoadState,
                                      uint32_t aLoadType,
                                      bool aLoadingCurrentEntry) {
  if (!IsNavigationAllowed()) {
    return NS_OK;
  }

  // https://html.spec.whatwg.org/#navigate-to-a-javascript:-url
  // Step 13: historyEntry should store entryToReplace's URL.
  if (aLoadState->URI()->SchemeIs("javascript")) {
    MOZ_ASSERT_UNREACHABLE("javascript: URIs should not enter session history");
    return NS_ERROR_FAILURE;
  }

  // We are setting load type afterwards so we don't have to
  // send it in an IPC message
  aLoadState->SetLoadType(aLoadType);

  SetOngoingNavigation(Some(OngoingNavigation::Traversal));

  /* If there is a valid postdata *and* the user pressed
   * reload or shift-reload, take user's permission before we
   * repost the data to the server.
   */
  if ((aLoadType & LOAD_CMD_RELOAD) && aLoadState->PostDataStream()) {
    bool repost;
    nsresult rv = ConfirmRepost(&repost);
    if (NS_FAILED(rv)) {
      return rv;
    }

    // If the user pressed cancel in the dialog, return.  We're done here.
    if (!repost) {
      return NS_BINDING_ABORTED;
    }
  }

  // If there is no valid triggeringPrincipal, we deny the load
  MOZ_ASSERT(aLoadState->TriggeringPrincipal(),
             "need a valid triggeringPrincipal to load from history");
  if (!aLoadState->TriggeringPrincipal()) {
    return NS_ERROR_FAILURE;
  }

  MaybeFireTraverseHistory(aLoadState);

  return InternalLoad(aLoadState);  // No nsIRequest
}

NS_IMETHODIMP
nsDocShell::PersistLayoutHistoryState() {
  nsresult rv = NS_OK;

  if (mActiveEntry) {
    bool scrollRestorationIsManual =
        mActiveEntry->GetScrollRestorationIsManual();
    nsCOMPtr<nsILayoutHistoryState> layoutState;
    if (RefPtr<PresShell> presShell = GetPresShell()) {
      rv = presShell->CaptureHistoryState(getter_AddRefs(layoutState));
    } else if (scrollRestorationIsManual) {
      // Even if we don't have layout anymore, we may want to reset the
      // current scroll state in layout history.
      GetLayoutHistoryState(getter_AddRefs(layoutState));
    }

    if (scrollRestorationIsManual && layoutState) {
      layoutState->ResetScrollState();
    }
  }

  return rv;
}

already_AddRefed<ChildSHistory> nsDocShell::GetRootSessionHistory() {
  RefPtr<ChildSHistory> childSHistory =
      mBrowsingContext->Top()->GetChildSessionHistory();
  return childSHistory.forget();
}

nsresult nsDocShell::GetHttpChannel(nsIChannel* aChannel,
                                    nsIHttpChannel** aReturn) {
  NS_ENSURE_ARG_POINTER(aReturn);
  if (!aChannel) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIMultiPartChannel> multiPartChannel(do_QueryInterface(aChannel));
  if (multiPartChannel) {
    nsCOMPtr<nsIChannel> baseChannel;
    multiPartChannel->GetBaseChannel(getter_AddRefs(baseChannel));
    nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(baseChannel));
    *aReturn = httpChannel;
    NS_IF_ADDREF(*aReturn);
  }
  return NS_OK;
}

bool nsDocShell::ShouldDiscardLayoutState(nsIHttpChannel* aChannel) {
  // By default layout State will be saved.
  if (!aChannel) {
    return false;
  }

  // figure out if SH should be saving layout state
  bool noStore = false;
  (void)aChannel->IsNoStoreResponse(&noStore);
  return noStore;
}

NS_IMETHODIMP
nsDocShell::GetEditor(nsIEditor** aEditor) {
  NS_ENSURE_ARG_POINTER(aEditor);
  RefPtr<HTMLEditor> htmlEditor = GetHTMLEditorInternal();
  htmlEditor.forget(aEditor);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetEditor(nsIEditor* aEditor) {
  HTMLEditor* htmlEditor = aEditor ? aEditor->GetAsHTMLEditor() : nullptr;
  // If TextEditor comes, throw an error.
  if (aEditor && !htmlEditor) {
    return NS_ERROR_INVALID_ARG;
  }
  return SetHTMLEditorInternal(htmlEditor);
}

HTMLEditor* nsDocShell::GetHTMLEditorInternal() {
  return mEditorData ? mEditorData->GetHTMLEditor() : nullptr;
}

nsresult nsDocShell::SetHTMLEditorInternal(HTMLEditor* aHTMLEditor) {
  if (!aHTMLEditor && !mEditorData) {
    return NS_OK;
  }

  nsresult rv = EnsureEditorData();
  if (NS_FAILED(rv)) {
    return rv;
  }

  return mEditorData->SetHTMLEditor(aHTMLEditor);
}

NS_IMETHODIMP
nsDocShell::GetEditable(bool* aEditable) {
  NS_ENSURE_ARG_POINTER(aEditable);
  *aEditable = mEditorData && mEditorData->GetEditable();
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetHasEditingSession(bool* aHasEditingSession) {
  NS_ENSURE_ARG_POINTER(aHasEditingSession);

  if (mEditorData) {
    *aHasEditingSession = !!mEditorData->GetEditingSession();
  } else {
    *aHasEditingSession = false;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::MakeEditable(bool aInWaitForUriLoad) {
  nsresult rv = EnsureEditorData();
  if (NS_FAILED(rv)) {
    return rv;
  }

  return mEditorData->MakeEditable(aInWaitForUriLoad);
}

/* static */ bool nsDocShell::ShouldAddURIVisit(nsIChannel* aChannel) {
  bool needToAddURIVisit = true;
  nsCOMPtr<nsIPropertyBag2> props(do_QueryInterface(aChannel));
  if (props) {
    (void)props->GetPropertyAsBool(u"docshell.needToAddURIVisit"_ns,
                                   &needToAddURIVisit);
  }

  return needToAddURIVisit;
}

/* static */ void nsDocShell::ExtractLastVisit(
    nsIChannel* aChannel, nsIURI** aURI, uint32_t* aChannelRedirectFlags) {
  nsCOMPtr<nsIPropertyBag2> props(do_QueryInterface(aChannel));
  if (!props) {
    return;
  }

  nsresult rv;
  nsCOMPtr<nsIURI> uri(do_GetProperty(props, u"docshell.previousURI"_ns, &rv));
  if (NS_SUCCEEDED(rv)) {
    uri.forget(aURI);

    rv = props->GetPropertyAsUint32(u"docshell.previousFlags"_ns,
                                    aChannelRedirectFlags);

    NS_WARNING_ASSERTION(
        NS_SUCCEEDED(rv),
        "Could not fetch previous flags, URI will be treated like referrer");

  } else {
    // There is no last visit for this channel, so this must be the first
    // link.  Link the visit to the referrer of this request, if any.
    // Treat referrer as null if there is an error getting it.
    NS_GetReferrerFromChannel(aChannel, aURI);
  }
}

void nsDocShell::SaveLastVisit(nsIChannel* aChannel, nsIURI* aURI,
                               uint32_t aChannelRedirectFlags) {
  nsCOMPtr<nsIWritablePropertyBag2> props(do_QueryInterface(aChannel));
  if (!props || !aURI) {
    return;
  }

  props->SetPropertyAsInterface(u"docshell.previousURI"_ns, aURI);
  props->SetPropertyAsUint32(u"docshell.previousFlags"_ns,
                             aChannelRedirectFlags);
}

/* static */ void nsDocShell::InternalAddURIVisit(
    nsIURI* aURI, nsIURI* aPreviousURI, uint32_t aChannelRedirectFlags,
    uint32_t aResponseStatus, BrowsingContext* aBrowsingContext,
    nsIWidget* aWidget, uint32_t aLoadType, bool aWasUpgraded, bool aIsPost) {
  MOZ_ASSERT(aURI, "Visited URI is null!");
  MOZ_ASSERT(aLoadType != LOAD_ERROR_PAGE && aLoadType != LOAD_BYPASS_HISTORY,
             "Do not add error or bypass pages to global history");

  bool usePrivateBrowsing = false;
  aBrowsingContext->GetUsePrivateBrowsing(&usePrivateBrowsing);

  // Only content-type docshells save URI visits.  Also don't do
  // anything here if we're not supposed to use global history.
  if (!aBrowsingContext->IsContent() ||
      !aBrowsingContext->GetUseGlobalHistory() || usePrivateBrowsing) {
    return;
  }

  nsCOMPtr<IHistory> history = components::History::Service();

  if (history) {
    uint32_t visitURIFlags = 0;

    if (aBrowsingContext->IsTop()) {
      visitURIFlags |= IHistory::TOP_LEVEL;
    }

    if (aChannelRedirectFlags & nsIChannelEventSink::REDIRECT_TEMPORARY) {
      visitURIFlags |= IHistory::REDIRECT_TEMPORARY;
    } else if (aChannelRedirectFlags &
               nsIChannelEventSink::REDIRECT_PERMANENT) {
      visitURIFlags |= IHistory::REDIRECT_PERMANENT;
    } else {
      MOZ_ASSERT(!aChannelRedirectFlags,
                 "One of REDIRECT_TEMPORARY or REDIRECT_PERMANENT must be set "
                 "if any flags in aChannelRedirectFlags is set.");
    }

    if (aResponseStatus >= 300 && aResponseStatus < 400) {
      visitURIFlags |= IHistory::REDIRECT_SOURCE;
      if (aResponseStatus == 301 || aResponseStatus == 308) {
        visitURIFlags |= IHistory::REDIRECT_SOURCE_PERMANENT;
      }
    }
    // Errors 400-501 and 505 are considered unrecoverable, in the sense a
    // simple retry attempt by the user is unlikely to solve them.
    // 408 is special cased, since may actually indicate a temporary
    // connection problem.
    else if (aResponseStatus != 408 &&
             ((aResponseStatus >= 400 && aResponseStatus <= 501) ||
              aResponseStatus == 505)) {
      visitURIFlags |= IHistory::UNRECOVERABLE_ERROR;
    }

    if (aWasUpgraded) {
      visitURIFlags |=
          IHistory::REDIRECT_SOURCE | IHistory::REDIRECT_SOURCE_UPGRADED;
    }

    if (aIsPost) {
      visitURIFlags |= IHistory::SOURCE_IS_POST_RESPONSE;
    }

    (void)history->VisitURI(aWidget, aURI, aPreviousURI, visitURIFlags,
                            aBrowsingContext->BrowserId());
  }
}

void nsDocShell::AddURIVisit(nsIURI* aURI, nsIURI* aPreviousURI,
                             uint32_t aChannelRedirectFlags,
                             uint32_t aResponseStatus, bool aIsPost) {
  nsPIDOMWindowOuter* outer = GetWindow();
  nsCOMPtr<nsIWidget> widget = widget::WidgetUtils::DOMWindowToWidget(outer);

  InternalAddURIVisit(aURI, aPreviousURI, aChannelRedirectFlags,
                      aResponseStatus, mBrowsingContext, widget, mLoadType,
                      false, aIsPost);
}

//*****************************************************************************
// nsDocShell: Helper Routines
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::SetLoadType(uint32_t aLoadType) {
  mLoadType = aLoadType;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetLoadType(uint32_t* aLoadType) {
  *aLoadType = mLoadType;
  return NS_OK;
}

nsresult nsDocShell::ConfirmRepost(bool* aRepost) {
  if (StaticPrefs::dom_confirm_repost_testing_always_accept()) {
    *aRepost = true;
    return NS_OK;
  }

  nsCOMPtr<nsIPromptCollection> prompter =
      do_GetService("@mozilla.org/embedcomp/prompt-collection;1");
  if (!prompter) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return prompter->ConfirmRepost(mBrowsingContext, aRepost);
}

nsresult nsDocShell::GetPromptAndStringBundle(nsIPrompt** aPrompt,
                                              nsIStringBundle** aStringBundle) {
  NS_ENSURE_SUCCESS(GetInterface(NS_GET_IID(nsIPrompt), (void**)aPrompt),
                    NS_ERROR_FAILURE);

  nsCOMPtr<nsIStringBundleService> stringBundleService =
      mozilla::components::StringBundle::Service();
  NS_ENSURE_TRUE(stringBundleService, NS_ERROR_FAILURE);

  NS_ENSURE_SUCCESS(
      stringBundleService->CreateBundle(kAppstringsBundleURL, aStringBundle),
      NS_ERROR_FAILURE);

  return NS_OK;
}

ScrollContainerFrame* nsDocShell::GetRootScrollContainerFrame() {
  PresShell* presShell = GetPresShell();
  NS_ENSURE_TRUE(presShell, nullptr);

  return presShell->GetRootScrollContainerFrame();
}

nsresult nsDocShell::EnsureScriptEnvironment() {
  if (mScriptGlobal) {
    return NS_OK;
  }

  if (mIsBeingDestroyed) {
    return NS_ERROR_NOT_AVAILABLE;
  }

#ifdef DEBUG
  NS_ASSERTION(!mInEnsureScriptEnv,
               "Infinite loop! Calling EnsureScriptEnvironment() from "
               "within EnsureScriptEnvironment()!");

  // Yeah, this isn't re-entrant safe, but that's ok since if we
  // re-enter this method, we'll infinitely loop...
  AutoRestore<bool> boolSetter(mInEnsureScriptEnv);
  mInEnsureScriptEnv = true;
#endif

  nsCOMPtr<nsIWebBrowserChrome> browserChrome(do_GetInterface(mTreeOwner));
  NS_ENSURE_TRUE(browserChrome, NS_ERROR_NOT_AVAILABLE);

  uint32_t chromeFlags;
  browserChrome->GetChromeFlags(&chromeFlags);

  // If our window is modal and we're not opened as chrome, make
  // this window a modal content window.
  mScriptGlobal = nsGlobalWindowOuter::Create(this, mItemType == typeChrome);
  MOZ_ASSERT(mScriptGlobal);

  // Ensure the script object is set up to run script.
  return mScriptGlobal->EnsureScriptEnvironment();
}

nsresult nsDocShell::EnsureEditorData() {
  MOZ_ASSERT(!mIsBeingDestroyed);

  bool openDocHasDetachedEditor = false;
  if (!mEditorData && !mIsBeingDestroyed && !openDocHasDetachedEditor) {
    // We shouldn't recreate the editor data if it already exists, or
    // we're shutting down, or we already have a detached editor data
    // stored in the session history. We should only have one editordata
    // per docshell.
    mEditorData = MakeUnique<nsDocShellEditorData>(this);
  }

  return mEditorData ? NS_OK : NS_ERROR_NOT_AVAILABLE;
}

nsresult nsDocShell::EnsureFind() {
  if (!mFind) {
    mFind = MakeRefPtr<nsWebBrowserFind>();
  }

  // we promise that the nsIWebBrowserFind that we return has been set
  // up to point to the focused, or content window, so we have to
  // set that up each time.

  nsIScriptGlobalObject* scriptGO = GetScriptGlobalObject();
  NS_ENSURE_TRUE(scriptGO, NS_ERROR_UNEXPECTED);

  // default to our window
  nsCOMPtr<nsPIDOMWindowOuter> ourWindow = do_QueryInterface(scriptGO);
  nsCOMPtr<nsPIDOMWindowOuter> windowToSearch;
  nsFocusManager::GetFocusedDescendant(ourWindow,
                                       nsFocusManager::eIncludeAllDescendants,
                                       getter_AddRefs(windowToSearch));

  nsCOMPtr<nsIWebBrowserFindInFrames> findInFrames = do_QueryInterface(mFind);
  if (!findInFrames) {
    return NS_ERROR_NO_INTERFACE;
  }

  nsresult rv = findInFrames->SetRootSearchFrame(ourWindow);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = findInFrames->SetCurrentSearchFrame(windowToSearch);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::IsBeingDestroyed(bool* aDoomed) {
  NS_ENSURE_ARG(aDoomed);
  *aDoomed = mIsBeingDestroyed;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetIsExecutingOnLoadHandler(bool* aResult) {
  NS_ENSURE_ARG(aResult);
  *aResult = mIsExecutingOnLoadHandler;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetLayoutHistoryState(nsILayoutHistoryState** aLayoutHistoryState) {
  nsCOMPtr<nsILayoutHistoryState> state;
  if (mActiveEntry) {
    state = mActiveEntry->GetLayoutHistoryState();
  }
  state.forget(aLayoutHistoryState);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetLayoutHistoryState(nsILayoutHistoryState* aLayoutHistoryState) {
  if (mActiveEntry) {
    mActiveEntry->SetLayoutHistoryState(aLayoutHistoryState);
  }
  return NS_OK;
}

nsDocShell::InterfaceRequestorProxy::InterfaceRequestorProxy(
    nsIInterfaceRequestor* aRequestor) {
  if (aRequestor) {
    mWeakPtr = do_GetWeakReference(aRequestor);
  }
}

nsDocShell::InterfaceRequestorProxy::~InterfaceRequestorProxy() {
  mWeakPtr = nullptr;
}

NS_IMPL_ISUPPORTS(nsDocShell::InterfaceRequestorProxy, nsIInterfaceRequestor)

NS_IMETHODIMP
nsDocShell::InterfaceRequestorProxy::GetInterface(const nsIID& aIID,
                                                  void** aSink) {
  NS_ENSURE_ARG_POINTER(aSink);
  nsCOMPtr<nsIInterfaceRequestor> ifReq = do_QueryReferent(mWeakPtr);
  if (ifReq) {
    return ifReq->GetInterface(aIID, aSink);
  }
  *aSink = nullptr;
  return NS_NOINTERFACE;
}

//*****************************************************************************
// nsDocShell::nsIAuthPromptProvider
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::GetAuthPrompt(uint32_t aPromptReason, const nsIID& aIID,
                          void** aResult) {
  // a priority prompt request will override a false mAllowAuth setting
  bool priorityPrompt = (aPromptReason == PROMPT_PROXY);

  if (!mAllowAuth && !priorityPrompt) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // we're either allowing auth, or it's a proxy request
  nsresult rv;
  nsCOMPtr<nsIPromptFactory> wwatch =
      do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = EnsureScriptEnvironment();
  NS_ENSURE_SUCCESS(rv, rv);

  // Get the an auth prompter for our window so that the parenting
  // of the dialogs works as it should when using tabs.

  return wwatch->GetPrompt(mScriptGlobal, aIID,
                           reinterpret_cast<void**>(aResult));
}

//*****************************************************************************
// nsDocShell::nsILoadContext
//*****************************************************************************

NS_IMETHODIMP
nsDocShell::GetAssociatedWindow(mozIDOMWindowProxy** aWindow) {
  CallGetInterface(this, aWindow);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetTopWindow(mozIDOMWindowProxy** aWindow) {
  return mBrowsingContext->GetTopWindow(aWindow);
}

NS_IMETHODIMP
nsDocShell::GetTopFrameElement(Element** aElement) {
  return mBrowsingContext->GetTopFrameElement(aElement);
}

NS_IMETHODIMP
nsDocShell::GetUseTrackingProtection(bool* aUseTrackingProtection) {
  return mBrowsingContext->GetUseTrackingProtection(aUseTrackingProtection);
}

NS_IMETHODIMP
nsDocShell::SetUseTrackingProtection(bool aUseTrackingProtection) {
  return mBrowsingContext->SetUseTrackingProtection(aUseTrackingProtection);
}

NS_IMETHODIMP
nsDocShell::GetIsContent(bool* aIsContent) {
  *aIsContent = (mItemType == typeContent);
  return NS_OK;
}

bool nsDocShell::IsOKToLoadURI(nsIURI* aURI) {
  MOZ_ASSERT(aURI, "Must have a URI!");

  if (!mFiredUnloadEvent) {
    return true;
  }

  if (!mLoadingURI) {
    return false;
  }

  bool isPrivateWin = false;
  Document* doc = GetDocument();
  if (doc) {
    isPrivateWin =
        doc->NodePrincipal()->OriginAttributesRef().IsPrivateBrowsing();
  }

  nsCOMPtr<nsIScriptSecurityManager> secMan =
      do_GetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID);
  return secMan && NS_SUCCEEDED(secMan->CheckSameOriginURI(
                       aURI, mLoadingURI, false, isPrivateWin));
}

//
// Routines for selection and clipboard
//
nsresult nsDocShell::GetControllerForCommand(const char* aCommand,
                                             nsIController** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  NS_ENSURE_TRUE(mScriptGlobal, NS_ERROR_FAILURE);

  nsCOMPtr<nsPIWindowRoot> root = mScriptGlobal->GetTopWindowRoot();
  NS_ENSURE_TRUE(root, NS_ERROR_FAILURE);

  return root->GetControllerForCommand(aCommand, false /* for any window */,
                                       aResult);
}

NS_IMETHODIMP
nsDocShell::IsCommandEnabled(const char* aCommand, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = false;

  nsresult rv = NS_ERROR_FAILURE;

  nsCOMPtr<nsIController> controller;
  rv = GetControllerForCommand(aCommand, getter_AddRefs(controller));
  if (controller) {
    rv = controller->IsCommandEnabled(aCommand, aResult);
  }

  return rv;
}

NS_IMETHODIMP
nsDocShell::DoCommand(const char* aCommand) {
  nsresult rv = NS_ERROR_FAILURE;

  nsCOMPtr<nsIController> controller;
  rv = GetControllerForCommand(aCommand, getter_AddRefs(controller));
  if (controller) {
    rv = controller->DoCommand(aCommand);
  }

  return rv;
}

NS_IMETHODIMP
nsDocShell::DoCommandWithParams(const char* aCommand,
                                nsICommandParams* aParams) {
  nsCOMPtr<nsIController> controller;
  nsresult rv = GetControllerForCommand(aCommand, getter_AddRefs(controller));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsICommandController> commandController =
      do_QueryInterface(controller, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return commandController->DoCommandWithParams(aCommand, aParams);
}

nsresult nsDocShell::EnsureCommandHandler() {
  if (!mCommandManager) {
    if (nsCOMPtr<nsPIDOMWindowOuter> domWindow = GetWindow()) {
      mCommandManager = MakeRefPtr<nsCommandManager>(domWindow);
    }
  }
  return mCommandManager ? NS_OK : NS_ERROR_FAILURE;
}

// link handling

class OnLinkClickEvent : public CancelableRunnable, public SupportsWeakPtr {
 public:
  OnLinkClickEvent(nsDocShell* aHandler, nsIContent* aContent,
                   nsDocShellLoadState* aLoadState, bool aNoOpenerImplied,
                   nsIPrincipal* aTriggeringPrincipal);

  NS_IMETHOD Run() override {
    if (mCancelled) {
      return NS_OK;
    }

    // We need to set up an AutoJSAPI here for the following reason: When we
    // do OnLinkClickSync we'll eventually end up in
    // nsGlobalWindow::OpenInternal which only does popup blocking if
    // !LegacyIsCallerChromeOrNativeCode(). So we need to fake things so that
    // we don't look like native code as far as LegacyIsCallerNativeCode() is
    // concerned. (Bug 1930445)
    AutoJSAPI jsapi;
    if (jsapi.Init(mContent->OwnerDoc()->GetScopeObject())) {
      // For form submissions, the source element is set to the submitter,
      // and we don't want to overwrite it with the <form> element here.
      if (!mLoadState->HasSourceElement()) {
        mLoadState->SetSourceElement(mContent->AsElement());
      }
      mHandler->OnLinkClickSync(mContent, mLoadState, mNoOpenerImplied,
                                mTriggeringPrincipal);
    }
    return NS_OK;
  }

  nsresult Cancel() override final {
    mCancelled = true;
    // Don't clear mLoadState, etc. here since this can be called
    // from the OnLinkClickSync() above, and we might be holding
    // the last references to them.
    return NS_OK;
  }

 private:
  RefPtr<nsDocShell> mHandler;
  nsCOMPtr<nsIContent> mContent;
  RefPtr<nsDocShellLoadState> mLoadState;
  nsCOMPtr<nsIPrincipal> mTriggeringPrincipal;
  bool mNoOpenerImplied;
  bool mCancelled = false;
};

OnLinkClickEvent::OnLinkClickEvent(nsDocShell* aHandler, nsIContent* aContent,
                                   nsDocShellLoadState* aLoadState,
                                   bool aNoOpenerImplied,
                                   nsIPrincipal* aTriggeringPrincipal)
    : mozilla::CancelableRunnable("OnLinkClickEvent"),
      mHandler(aHandler),
      mContent(aContent),
      mLoadState(aLoadState),
      mTriggeringPrincipal(aTriggeringPrincipal),
      mNoOpenerImplied(aNoOpenerImplied) {}

Result<RefPtr<OnLinkClickEvent>, nsresult> nsDocShell::OnLinkClickWithLoadState(
    nsIContent* aContent, nsDocShellLoadState* aLoadState,
    bool aNoOpenerImplied, nsIPrincipal* aTriggeringPrincipal) {
  if (StaticPrefs::dom_forms_submit_async_navigation()) {
    // Determine BC to target synchronously, for consistency with other browsers
    // (Guarding this behind dom.forms.submit_async_navigation since this change
    //  is necessary for async form navigation, and it may cause regressions.)
    ComputeNamedTargetBrowsingContext(aLoadState);
    if (aContent->IsHTMLElement(nsGkAtoms::form)) {
      // https://html.spec.whatwg.org/#form-submission-algorithm
      // 25. If form document equals targetNavigable's active document, and form
      //     document has not yet completely loaded, then set historyHandling to
      //     "replace".
      const MaybeDiscarded<BrowsingContext>& bc =
          aLoadState->TargetBrowsingContext();
      Document* formDocument = aContent->OwnerDoc();
      if ((bc.IsNull() || bc == formDocument->GetBrowsingContext()) &&
          !formDocument->IsCompletelyLoaded()) {
        aLoadState->SetHistoryBehavior(NavigationHistoryBehavior::Replace);
      }
    }
  }
  RefPtr ev = MakeRefPtr<OnLinkClickEvent>(
      this, aContent, aLoadState, aNoOpenerImplied, aTriggeringPrincipal);
  RefPtr<nsIRunnable> runnable = ev;
  nsresult rv = Dispatch(runnable.forget());
  NS_ENSURE_SUCCESS(rv, Err(rv));
  return ev;
}

nsresult nsDocShell::OnFormSubmit(HTMLFormElement* aForm,
                                  nsDocShellLoadState* aLoadState) {
  // Need to do this here, since it's ordinarily done in OnLinkClick.
  if (ShouldBlockLoadingForBackButton()) {
    return NS_OK;
  }
  if (!StaticPrefs::dom_forms_submit_async_navigation()) {
    return OnLinkClickSync(aForm, aLoadState, false, aForm->NodePrincipal());
  }

  auto result = OnLinkClickWithLoadState(aForm, aLoadState, false,
                                         aForm->NodePrincipal());
  if (result.isErr()) {
    return result.unwrapErr();
  }
  nsDocShell* targetDocShell = this;
  if (!aLoadState->Target().IsEmpty()) {
    const MaybeDiscarded<BrowsingContext>& targetBC =
        aLoadState->TargetBrowsingContext();
    targetDocShell =
        targetBC.IsNullOrDiscarded()
            ? nullptr
            : static_cast<nsDocShell*>(targetBC.get()->GetDocShell());
  }
  if (targetDocShell) {
    targetDocShell->CancelPlannedFormNavigation();
    // Chrome stops pending javascript: navigations here.
    // https://github.com/whatwg/html/issues/12607
    targetDocShell->StopPendingJavascriptURLNavigations();
    targetDocShell->mPlannedFormNavigation = result.unwrap().get();
  }
  return NS_OK;
}

nsresult nsDocShell::CancelPlannedFormNavigation() {
  if (mPlannedFormNavigation) {
    mPlannedFormNavigation->Cancel();
    mPlannedFormNavigation = nullptr;
  }
  return NS_OK;
}

nsresult nsDocShell::OnLinkClick(
    nsIContent* aContent, nsIURI* aURI, const nsAString& aTargetSpec,
    const nsAString& aFileName, nsIInputStream* aPostDataStream,
    nsIInputStream* aHeadersDataStream, bool aIsUserTriggered,
    UserNavigationInvolvement aUserInvolvement,
    nsIPrincipal* aTriggeringPrincipal, nsIPolicyContainer* aPolicyContainer) {
#ifndef ANDROID
  MOZ_ASSERT(aTriggeringPrincipal, "Need a valid triggeringPrincipal");
#endif
  NS_ASSERTION(NS_IsMainThread(), "wrong thread");

  if (!IsNavigationAllowed() || !IsOKToLoadURI(aURI)) {
    return NS_OK;
  }

  // On history navigation through Back/Forward buttons, don't execute
  // automatic JavaScript redirection such as |anchorElement.click()| or
  // |formElement.submit()|.
  //
  // XXX |formElement.submit()| bypasses this checkpoint because it calls
  //     nsDocShell::OnLinkClickSync(...) instead.
  if (ShouldBlockLoadingForBackButton()) {
    return NS_OK;
  }

  if (aContent->IsEditable()) {
    return NS_OK;
  }

  RefPtr<Document> ownerDoc = aContent->OwnerDoc();
  if (nsContentUtils::IsExternalProtocol(aURI)) {
    ownerDoc->EnsureNotEnteringAndExitFullscreen();
  }

  bool noOpenerImplied = false;
  nsAutoString target(aTargetSpec);
  if (aFileName.IsVoid() &&
      ShouldOpenInBlankTarget(aTargetSpec, aURI, aContent, aIsUserTriggered)) {
    target = u"_blank";
    if (!aTargetSpec.Equals(target)) {
      noOpenerImplied = true;
    }
  }

  // https://html.spec.whatwg.org/#downloading-hyperlinks
  // Step 6, step 6.1, step 6.2
  // aFileName not being void implies a download attribute, since we've already
  // checked if the attribute is present in `nsContentUtils::TriggerLinkClick`
  // and made it void otherwise.
  if (!aFileName.IsVoid() &&
      aUserInvolvement != UserNavigationInvolvement::BrowserUI) {
    if (nsCOMPtr<nsPIDOMWindowInner> window = ownerDoc->GetInnerWindow()) {
      if (RefPtr<Navigation> navigation = window->Navigation()) {
        AutoJSAPI jsapi;
        if (jsapi.Init(window)) {
          RefPtr element = aContent->AsElement();
          // Step 6.4
          bool shouldContinue = navigation->FireDownloadRequestNavigateEvent(
              jsapi.cx(), aURI, aUserInvolvement, element, aFileName);

          // Step 6.5
          if (!shouldContinue) {
            return NS_OK;
          }
        }
      }
    }
  }

  RefPtr loadState = MakeRefPtr<nsDocShellLoadState>(aURI);
  loadState->SetTarget(target);
  loadState->SetFileName(aFileName);
  loadState->SetPostDataStream(aPostDataStream);
  loadState->SetHeadersStream(aHeadersDataStream);
  loadState->SetFirstParty(true);
  loadState->SetTriggeringPrincipal(
      aTriggeringPrincipal ? aTriggeringPrincipal : aContent->NodePrincipal());
  loadState->SetPrincipalToInherit(aContent->NodePrincipal());
  loadState->SetPolicyContainer(
      aPolicyContainer ? aPolicyContainer : aContent->GetPolicyContainer());
  loadState->SetAllowFocusMove(UserActivation::IsHandlingUserInput());

  const bool hasValidUserGestureActivation =
      ownerDoc->HasValidTransientUserGestureActivation();
  loadState->SetHasValidUserGestureActivation(hasValidUserGestureActivation);

  // For protocols that would launch without a prompt (e.g. mailto), consume the
  // transient user gesture activation so a single gesture can't chain multiple
  // launches. Do this here, at the same point the activation value is captured
  // on the load state, rather than later in OnLinkClickSync: that runs
  // asynchronously, and consuming there would be inconsistent with the scripted
  // navigation path (see BrowsingContext::Navigate). The pre-consume value is
  // already recorded on the load state above. See bug 299116.
  if (nsAutoCString scheme; NS_SUCCEEDED(aURI->GetScheme(scheme))) {
    nsExternalHelperAppService::MaybeConsumeUserActivationForExternalScheme(
        ownerDoc->GetWindowContext(), loadState->TriggeringPrincipal(), scheme);
  }

  loadState->SetTextDirectiveUserActivation(
      ownerDoc->ConsumeTextDirectiveUserActivation() ||
      hasValidUserGestureActivation);
  loadState->SetUserNavigationInvolvement(aUserInvolvement);
  loadState->SetTriggeringClassificationFlags(
      ownerDoc->GetScriptTrackingFlags());
  loadState->SetHistoryBehavior(NavigationHistoryBehavior::Auto);

  auto result = OnLinkClickWithLoadState(aContent, loadState, noOpenerImplied,
                                         aTriggeringPrincipal);
  return result.isErr() ? result.unwrapErr() : NS_OK;
}

bool nsDocShell::ShouldOpenInBlankTarget(const nsAString& aOriginalTarget,
                                         nsIURI* aLinkURI, nsIContent* aContent,
                                         bool aIsUserTriggered) {
  if (aLinkURI->SchemeIs("javascript")) {
    return false;
  }

  // External links from within app tabs should always open in new tabs
  // instead of replacing the app tab's page (Bug 575561)
  // nsIURI.host can throw for non-nsStandardURL nsIURIs. If we fail to
  // get either host, just return false to use the original target.
  nsAutoCString linkHost;
  if (NS_FAILED(aLinkURI->GetHost(linkHost))) {
    return false;
  }

  // The targetTopLevelLinkClicksToBlank property on BrowsingContext allows
  // privileged code to change the default targeting behaviour. In particular,
  // if a user-initiated link click for the (or targetting the) top-level frame
  // is detected, we default the target to "_blank" to give it a new
  // top-level BrowsingContext.
  if (mBrowsingContext->TargetTopLevelLinkClicksToBlank() && aIsUserTriggered &&
      ((aOriginalTarget.IsEmpty() && mBrowsingContext->IsTop()) ||
       aOriginalTarget == u"_top"_ns)) {
    return true;
  }

  // Don't modify non-default targets.
  if (!aOriginalTarget.IsEmpty()) {
    return false;
  }

  // Only check targets that are in extension panels or app tabs.
  // (isAppTab will be false for app tab subframes).
  nsString mmGroup = mBrowsingContext->Top()->GetMessageManagerGroup();
  if (!mmGroup.EqualsLiteral("webext-browsers") &&
      !mBrowsingContext->IsAppTab()) {
    return false;
  }

  nsCOMPtr<nsIURI> docURI = aContent->OwnerDoc()->GetDocumentURIObject();
  if (!docURI) {
    return false;
  }

  nsAutoCString docHost;
  if (NS_FAILED(docURI->GetHost(docHost))) {
    return false;
  }

  if (linkHost.Equals(docHost)) {
    return false;
  }

  // Special case: ignore "www" prefix if it is part of host string
  return linkHost.Length() < docHost.Length()
             ? !docHost.Equals("www."_ns + linkHost)
             : !linkHost.Equals("www."_ns + docHost);
}

static bool ElementCanHaveNoopener(nsIContent* aContent) {
  // Make sure we are dealing with either an <A>, <AREA>, or <FORM> element in
  // the HTML, XHTML, or SVG namespace.
  return aContent->IsAnyOfHTMLElements(nsGkAtoms::a, nsGkAtoms::area,
                                       nsGkAtoms::form) ||
         aContent->IsSVGElement(nsGkAtoms::a);
}

nsresult nsDocShell::OnLinkClickSync(nsIContent* aContent,
                                     nsDocShellLoadState* aLoadState,
                                     bool aNoOpenerImplied,
                                     nsIPrincipal* aTriggeringPrincipal) {
  if (!IsNavigationAllowed() || !IsOKToLoadURI(aLoadState->URI())) {
    return NS_OK;
  }

  if (aContent->IsEditable()) {
    return NS_OK;
  }

  // if the triggeringPrincipal is not passed explicitly, then we
  // fall back to using doc->NodePrincipal() as the triggeringPrincipal.
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      aTriggeringPrincipal ? aTriggeringPrincipal : aContent->NodePrincipal();

  {
    // defer to an external protocol handler if necessary...
    nsCOMPtr<nsIExternalProtocolService> extProtService =
        do_GetService(NS_EXTERNALPROTOCOLSERVICE_CONTRACTID);
    if (extProtService) {
      nsAutoCString scheme;
      aLoadState->URI()->GetScheme(scheme);
      if (!scheme.IsEmpty()) {
        // if the URL scheme does not correspond to an exposed protocol, then
        // we need to hand this link click over to the external protocol
        // handler.
        bool isExposed;
        nsresult rv =
            extProtService->IsExposedProtocol(scheme.get(), &isExposed);
        if (NS_SUCCEEDED(rv) && !isExposed) {
          // Use the activation captured on the load state at click/submit time
          // (OnLinkClick, HTMLFormElement::SubmitSubmission), rather than
          // re-reading from the document here: this runs asynchronously, by
          // which point the transient activation may have expired. Consumption
          // of the activation (for gated schemes like mailto) also happens at
          // that capture point. See bug 299116.
          bool hasValidUserGestureActivation =
              aLoadState->HasValidUserGestureActivation();
          return extProtService->LoadURI(aLoadState->URI(), triggeringPrincipal,
                                         nullptr, mBrowsingContext,
                                         /* aTriggeredExternally */
                                         false, hasValidUserGestureActivation,
                                         /* aNewWindowTarget */ false);
        }
      }
    }
  }
  uint32_t triggeringSandboxFlags = 0;
  uint64_t triggeringWindowId = 0;
  bool triggeringStorageAccess = false;
  if (mBrowsingContext) {
    triggeringSandboxFlags = aContent->OwnerDoc()->GetSandboxFlags();
    triggeringWindowId = aContent->OwnerDoc()->InnerWindowID();
    triggeringStorageAccess = aContent->OwnerDoc()->UsingStorageAccess();
  }

  uint32_t flags = INTERNAL_LOAD_FLAGS_NONE;
  bool elementCanHaveNoopener = ElementCanHaveNoopener(aContent);
  bool triggeringPrincipalIsSystemPrincipal =
      aLoadState->TriggeringPrincipal()->IsSystemPrincipal();
  if (elementCanHaveNoopener) {
    MOZ_ASSERT(aContent->IsHTMLElement() || aContent->IsSVGElement());
    nsAutoString relString;
    aContent->AsElement()->GetAttr(nsGkAtoms::rel, relString);
    nsWhitespaceTokenizerTemplate<nsContentUtils::IsHTMLWhitespace> tok(
        relString);

    bool targetBlank = aLoadState->Target().LowerCaseEqualsLiteral("_blank");
    bool explicitOpenerSet = false;

    // The opener behaviour follows a hierarchy, such that if a higher
    // priority behaviour is specified, it always takes priority. That
    // priority is currently: norefrerer > noopener > opener > default

    while (tok.hasMoreTokens()) {
      const nsAString& token = tok.nextToken();
      if (token.LowerCaseEqualsLiteral("noreferrer")) {
        flags |= INTERNAL_LOAD_FLAGS_DONT_SEND_REFERRER |
                 INTERNAL_LOAD_FLAGS_NO_OPENER;
        // noreferrer cannot be overwritten by a 'rel=opener'.
        explicitOpenerSet = true;
        break;
      }

      if (token.LowerCaseEqualsLiteral("noopener")) {
        flags |= INTERNAL_LOAD_FLAGS_NO_OPENER;
        explicitOpenerSet = true;
      }

      if (targetBlank && StaticPrefs::dom_targetBlankNoOpener_enabled() &&
          token.LowerCaseEqualsLiteral("opener") && !explicitOpenerSet) {
        explicitOpenerSet = true;
      }
    }

    if (targetBlank && StaticPrefs::dom_targetBlankNoOpener_enabled() &&
        !explicitOpenerSet && !triggeringPrincipalIsSystemPrincipal) {
      flags |= INTERNAL_LOAD_FLAGS_NO_OPENER;
    }

    if (aNoOpenerImplied) {
      flags |= INTERNAL_LOAD_FLAGS_NO_OPENER;
    }
  }

  // Get the owner document of the link that was clicked, this will be
  // the document that the link is in, or the last document that the
  // link was in. From that document, we'll get the URI to use as the
  // referrer, since the current URI in this docshell may be a
  // new document that we're in the process of loading.
  RefPtr<Document> referrerDoc = aContent->OwnerDoc();

  // Now check that the referrerDoc's inner window is the current inner
  // window for mScriptGlobal.  If it's not, then we don't want to
  // follow this link.
  nsPIDOMWindowInner* referrerInner = referrerDoc->GetInnerWindow();
  if (!mScriptGlobal || !referrerInner ||
      mScriptGlobal->GetCurrentInnerWindow() != referrerInner) {
    // We're no longer the current inner window
    return NS_OK;
  }

  // referrer could be null here in some odd cases, but that's ok,
  // we'll just load the link w/o sending a referrer in those cases.

  uint32_t loadType = LOAD_LINK;
  if (aLoadState->IsFormSubmission()) {
    if (aLoadState->Target().IsEmpty()) {
      // We set the right load type here for form submissions with an empty
      // target. Form submission with a non-empty target are handled in
      // nsDocShell::PerformRetargeting after we've selected the correct target
      // BC.
      loadType = GetLoadTypeForFormSubmission(GetBrowsingContext(), aLoadState);
    }
  } else {
    // Link click can be triggered inside an onload handler, and we don't want
    // to add history entry in this case.
    bool inOnLoadHandler = false;
    GetIsExecutingOnLoadHandler(&inOnLoadHandler);
    if (inOnLoadHandler) {
      loadType = LOAD_NORMAL_REPLACE;
    }
  }

  RefPtr referrerInfo = elementCanHaveNoopener
                            ? MakeRefPtr<ReferrerInfo>(*aContent->AsElement())
                            : MakeRefPtr<ReferrerInfo>(*referrerDoc);

  aLoadState->SetTriggeringSandboxFlags(triggeringSandboxFlags);
  aLoadState->SetTriggeringWindowId(triggeringWindowId);
  aLoadState->SetTriggeringStorageAccess(triggeringStorageAccess);
  aLoadState->SetReferrerInfo(referrerInfo);
  aLoadState->SetInternalLoadFlags(flags);
  aLoadState->SetLoadType(loadType);
  aLoadState->SetSourceBrowsingContext(mBrowsingContext);

  nsresult rv = InternalLoad(aLoadState);

  if (NS_SUCCEEDED(rv)) {
    nsPingListener::DispatchPings(this, aContent, aLoadState->URI(),
                                  referrerInfo);
  }

  return rv;
}

nsresult nsDocShell::OnOverLink(nsIContent* aContent, nsIURI* aURI,
                                const nsAString& aTargetSpec) {
  if (aContent->IsEditable()) {
    return NS_OK;
  }

  nsresult rv = NS_ERROR_FAILURE;

  nsCOMPtr<nsIWebBrowserChrome> browserChrome = do_GetInterface(mTreeOwner);
  if (!browserChrome) {
    return rv;
  }

  nsCOMPtr<nsIURI> exposableURI = nsIOService::CreateExposableURI(aURI);
  nsAutoCString spec;
  rv = exposableURI->GetDisplaySpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ConvertUTF8toUTF16 uStr(spec);

  // The speculative connect used to go through the predictor, but we don't
  // need all that just to initiate a speculative connect.
  if ((StaticPrefs::network_predictor_enable_hover_on_ssl() &&
       mCurrentURI->SchemeIs("https")) ||
      mCurrentURI->SchemeIs("http")) {
    if (nsCOMPtr<nsISpeculativeConnect> specService =
            mozilla::components::IO::Service()) {
      // This would be a navigation, so if this is cross origin the speculative
      // connection needs to have the origin of the URL not the current page.
      nsCOMPtr<nsIPrincipal> principal = BasePrincipal::CreateContentPrincipal(
          aURI, aContent->NodePrincipal()->OriginAttributesRef());

      specService->SpeculativeConnect(aURI, principal, this, false);
    }
  }

  rv = browserChrome->SetLinkStatus(uStr);
  return rv;
}

nsresult nsDocShell::OnLeaveLink() {
  nsCOMPtr<nsIWebBrowserChrome> browserChrome(do_GetInterface(mTreeOwner));
  nsresult rv = NS_ERROR_FAILURE;

  if (browserChrome) {
    rv = browserChrome->SetLinkStatus(u""_ns);
  }
  return rv;
}

bool nsDocShell::ShouldBlockLoadingForBackButton() {
  if (!(mLoadType & LOAD_CMD_HISTORY) ||
      UserActivation::IsHandlingUserInput() ||
      !Preferences::GetBool("accessibility.blockjsredirection")) {
    return false;
  }

  bool canGoForward = false;
  GetCanGoForward(&canGoForward);
  return canGoForward;
}

//----------------------------------------------------------------------
// Web Shell Services API

// This functions is only called when a new charset is detected in loading a
// document.
nsresult nsDocShell::CharsetChangeReloadDocument(
    mozilla::NotNull<const mozilla::Encoding*> aEncoding, int32_t aSource) {
  // XXX hack. keep the aCharset and aSource wait to pick it up
  nsCOMPtr<nsIDocumentViewer> viewer;
  NS_ENSURE_SUCCESS(GetDocViewer(getter_AddRefs(viewer)), NS_ERROR_FAILURE);
  if (viewer) {
    int32_t source;
    (void)viewer->GetReloadEncodingAndSource(&source);
    if (aSource > source) {
      viewer->SetReloadEncodingAndSource(aEncoding, aSource);
      if (eCharsetReloadRequested != mCharsetReloadState) {
        mCharsetReloadState = eCharsetReloadRequested;
        switch (mLoadType) {
          case LOAD_RELOAD_BYPASS_PROXY_AND_CACHE:
            return Reload(LOAD_FLAGS_CHARSET_CHANGE | LOAD_FLAGS_BYPASS_CACHE |
                          LOAD_FLAGS_BYPASS_PROXY);
          case LOAD_RELOAD_BYPASS_CACHE:
            return Reload(LOAD_FLAGS_CHARSET_CHANGE | LOAD_FLAGS_BYPASS_CACHE);
          default:
            return Reload(LOAD_FLAGS_CHARSET_CHANGE);
        }
      }
    }
  }
  // return failure if this request is not accepted due to mCharsetReloadState
  return NS_ERROR_DOCSHELL_REQUEST_REJECTED;
}

nsresult nsDocShell::CharsetChangeStopDocumentLoad() {
  if (eCharsetReloadRequested != mCharsetReloadState) {
    Stop(nsIWebNavigation::STOP_ALL);
    return NS_OK;
  }
  // return failer if this request is not accepted due to mCharsetReloadState
  return NS_ERROR_DOCSHELL_REQUEST_REJECTED;
}

NS_IMETHODIMP nsDocShell::ExitPrintPreview() {
#ifdef NS_PRINTING
  nsCOMPtr<nsIWebBrowserPrint> viewer = do_QueryInterface(mDocumentViewer);
  MOZ_TRY(viewer->ExitPrintPreview());
#endif
  return NS_OK;
}

/* [infallible] */
NS_IMETHODIMP nsDocShell::GetIsTopLevelContentDocShell(
    bool* aIsTopLevelContentDocShell) {
  *aIsTopLevelContentDocShell = false;

  if (mItemType == typeContent) {
    *aIsTopLevelContentDocShell = mBrowsingContext->IsTopContent();
  }

  return NS_OK;
}

// Implements nsILoadContext.originAttributes
NS_IMETHODIMP
nsDocShell::GetScriptableOriginAttributes(JSContext* aCx,
                                          JS::MutableHandle<JS::Value> aVal) {
  return mBrowsingContext->GetScriptableOriginAttributes(aCx, aVal);
}

// Implements nsIDocShell.GetOriginAttributes()
NS_IMETHODIMP
nsDocShell::GetOriginAttributes(JSContext* aCx,
                                JS::MutableHandle<JS::Value> aVal) {
  return mBrowsingContext->GetScriptableOriginAttributes(aCx, aVal);
}

bool nsDocShell::ServiceWorkerAllowedToControlWindow(nsIPrincipal* aPrincipal,
                                                     nsIURI* aURI) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aURI);

  if (UsePrivateBrowsing() || mBrowsingContext->GetSandboxFlags()) {
    return false;
  }

  nsCOMPtr<nsIDocShellTreeItem> parent;
  GetInProcessSameTypeParent(getter_AddRefs(parent));
  nsPIDOMWindowOuter* parentOuter = parent ? parent->GetWindow() : nullptr;
  nsPIDOMWindowInner* parentInner =
      parentOuter ? parentOuter->GetCurrentInnerWindow() : nullptr;

  StorageAccess storage =
      StorageAllowedForNewWindow(aPrincipal, aURI, parentInner);

  // A service worker is allowed to control the window if partition is enabled.
  if (parentInner) {
    RefPtr<Document> doc = parentInner->GetExtantDoc();

    if (doc && StoragePartitioningEnabled(storage, doc->CookieJarSettings())) {
      return true;
    }
  }

  return storage == StorageAccess::eAllow;
}

nsresult nsDocShell::SetOriginAttributes(const OriginAttributes& aAttrs) {
  MOZ_ASSERT(!mIsBeingDestroyed);
  return mBrowsingContext->SetOriginAttributes(aAttrs);
}

NS_IMETHODIMP
nsDocShell::ResumeRedirectedLoad(uint64_t aIdentifier) {
  RefPtr<nsDocShell> self = this;
  RefPtr<ChildProcessChannelListener> cpcl =
      ChildProcessChannelListener::GetSingleton();

  // Call into InternalLoad with the pending channel when it is received.
  cpcl->RegisterCallback(
      aIdentifier, [self](nsDocShellLoadState* aLoadState,
                          nsTArray<Endpoint<extensions::PStreamFilterParent>>&&
                              aStreamFilterEndpoints,
                          nsDOMNavigationTiming* aTiming) {
        MOZ_ASSERT(aLoadState->GetPendingRedirectedChannel());
        if (NS_WARN_IF(self->mIsBeingDestroyed)) {
          aLoadState->GetPendingRedirectedChannel()->CancelWithReason(
              NS_BINDING_ABORTED, "nsDocShell::mIsBeingDestroyed"_ns);
          return NS_BINDING_ABORTED;
        }

        self->mLoadType = aLoadState->LoadType();
        nsCOMPtr<nsIURI> previousURI;
        uint32_t previousFlags = 0;
        ExtractLastVisit(aLoadState->GetPendingRedirectedChannel(),
                         getter_AddRefs(previousURI), &previousFlags);
        self->SaveLastVisit(aLoadState->GetPendingRedirectedChannel(),
                            previousURI, previousFlags);

        if (aTiming) {
          self->mTiming = new nsDOMNavigationTiming(self, aTiming);
          self->mBlankTiming = false;
        }

        // Prohibit initial about:blank handling e.g. for when a cross-process
        // iframe loads about:blank and becomes same-process. Conceptually, the
        // browsing context isn't new despite the docshell being newly created.
        aLoadState->ProhibitInitialAboutBlankHandling();

        self->InternalLoad(aLoadState);

        if (aLoadState->GetOriginalURIString().isSome()) {
          // Save URI string in case it's needed later when
          // sending to search engine service in EndPageLoad()
          self->mOriginalUriString = *aLoadState->GetOriginalURIString();
        }

        for (auto& endpoint : aStreamFilterEndpoints) {
          extensions::StreamFilterParent::Attach(
              aLoadState->GetPendingRedirectedChannel(), std::move(endpoint));
        }

        // If the channel isn't pending, then it means that InternalLoad
        // never connected it, and we shouldn't try to continue. This
        // can happen even if InternalLoad returned NS_OK.
        bool pending = false;
        aLoadState->GetPendingRedirectedChannel()->IsPending(&pending);
        NS_ASSERTION(pending, "We should have connected the pending channel!");
        if (!pending) {
          return NS_BINDING_ABORTED;
        }
        return NS_OK;
      });
  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::SetOriginAttributes(JS::Handle<JS::Value> aOriginAttributes,
                                JSContext* aCx) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  return SetOriginAttributes(attrs);
}

NS_IMETHODIMP
nsDocShell::GetAsyncPanZoomEnabled(bool* aOut) {
  if (PresShell* presShell = GetPresShell()) {
    *aOut = presShell->AsyncPanZoomEnabled();
    return NS_OK;
  }

  // If we don't have a presShell, fall back to the default platform value of
  // whether or not APZ is enabled.
  *aOut = gfxPlatform::AsyncPanZoomEnabled();
  return NS_OK;
}

bool nsDocShell::HasUnloadedParent() {
  for (WindowContext* wc = GetBrowsingContext()->GetParentWindowContext(); wc;
       wc = wc->GetParentWindowContext()) {
    if (!wc->IsCurrent() || wc->IsDiscarded() ||
        wc->GetBrowsingContext()->IsDiscarded()) {
      // If a parent is OOP and the parent WindowContext is no
      // longer current, we can assume the parent was unloaded.
      return true;
    }

    if (wc->GetBrowsingContext()->IsInProcess() &&
        (!wc->GetBrowsingContext()->GetDocShell() ||
         wc->GetBrowsingContext()->GetDocShell()->GetIsInUnload())) {
      return true;
    }
  }
  return false;
}

/* static */
bool nsDocShell::ShouldUpdateGlobalHistory(uint32_t aLoadType) {
  return !(aLoadType == LOAD_BYPASS_HISTORY || aLoadType == LOAD_ERROR_PAGE ||
           aLoadType & LOAD_CMD_HISTORY);
}

void nsDocShell::UpdateGlobalHistoryTitle(nsIURI* aURI) {
  if (!mBrowsingContext->GetUseGlobalHistory() || UsePrivateBrowsing()) {
    return;
  }

  // Global history is interested into sub-frame visits only for link-coloring
  // purposes, thus title updates are skipped for those.
  //
  // Moreover, some iframe documents (such as the ones created via
  // document.open()) inherit the document uri of the caller, which would cause
  // us to override a previously set page title with one from the subframe.
  if (IsSubframe()) {
    return;
  }

  if (nsCOMPtr<IHistory> history = components::History::Service()) {
    history->SetURITitle(aURI, mTitle);
  }
}

bool nsDocShell::IsInvisible() { return mInvisible; }

void nsDocShell::SetInvisible(bool aInvisible) { mInvisible = aInvisible; }

/* static */
void nsDocShell::MaybeNotifyKeywordSearchLoading(const nsString& aProviderId,
                                                 const nsString& aKeyword) {
  if (aProviderId.IsEmpty()) {
    return;
  }
  nsresult rv;
  nsCOMPtr<nsISupportsString> isupportsString =
      do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS_VOID(rv);

  rv = isupportsString->SetData(aProviderId);
  NS_ENSURE_SUCCESS_VOID(rv);

  nsCOMPtr<nsIObserverService> obsSvc = services::GetObserverService();
  if (obsSvc) {
    // Note that "keyword-search" refers to a search via the url
    // bar, not a bookmarks keyword search.
    obsSvc->NotifyObservers(isupportsString, "keyword-search", aKeyword.get());
  }
}

NS_IMETHODIMP
nsDocShell::ShouldPrepareForIntercept(nsIURI* aURI, nsIChannel* aChannel,
                                      bool* aShouldIntercept) {
  return mInterceptController->ShouldPrepareForIntercept(aURI, aChannel,
                                                         aShouldIntercept);
}

NS_IMETHODIMP
nsDocShell::ChannelIntercepted(nsIInterceptedChannel* aChannel) {
  return mInterceptController->ChannelIntercepted(aChannel);
}

bool nsDocShell::InFrameSwap() {
  RefPtr<nsDocShell> shell = this;
  do {
    if (shell->mInFrameSwap) {
      return true;
    }
    shell = shell->GetInProcessParentDocshell();
  } while (shell);
  return false;
}

UniquePtr<ClientSource> nsDocShell::TakeInitialClientSource() {
  return std::move(mInitialClientSource);
}

NS_IMETHODIMP
nsDocShell::GetEditingSession(nsIEditingSession** aEditSession) {
  if (!NS_SUCCEEDED(EnsureEditorData())) {
    return NS_ERROR_FAILURE;
  }

  *aEditSession = do_AddRef(mEditorData->GetEditingSession()).take();
  return *aEditSession ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsDocShell::GetScriptableBrowserChild(nsIBrowserChild** aBrowserChild) {
  *aBrowserChild = GetBrowserChild().take();
  return *aBrowserChild ? NS_OK : NS_ERROR_FAILURE;
}

already_AddRefed<nsIBrowserChild> nsDocShell::GetBrowserChild() {
  nsCOMPtr<nsIBrowserChild> tc = do_QueryReferent(mBrowserChild);
  return tc.forget();
}

nsCommandManager* nsDocShell::GetCommandManager() {
  NS_ENSURE_SUCCESS(EnsureCommandHandler(), nullptr);
  return mCommandManager;
}

NS_IMETHODIMP_(void)
nsDocShell::GetOriginAttributes(mozilla::OriginAttributes& aAttrs) {
  mBrowsingContext->GetOriginAttributes(aAttrs);
}

HTMLEditor* nsIDocShell::GetHTMLEditor() {
  nsDocShell* docShell = static_cast<nsDocShell*>(this);
  return docShell->GetHTMLEditorInternal();
}

nsresult nsIDocShell::SetHTMLEditor(HTMLEditor* aHTMLEditor) {
  nsDocShell* docShell = static_cast<nsDocShell*>(this);
  return docShell->SetHTMLEditorInternal(aHTMLEditor);
}

#define MATRIX_LENGTH 20

NS_IMETHODIMP
nsDocShell::SetColorMatrix(const nsTArray<float>& aMatrix) {
  if (aMatrix.Length() == MATRIX_LENGTH) {
    mColorMatrix = MakeUnique<gfx::Matrix5x4>();
    static_assert(
        MATRIX_LENGTH * sizeof(float) == sizeof(mColorMatrix->components),
        "Size mismatch for our memcpy");
    memcpy(mColorMatrix->components, aMatrix.Elements(),
           sizeof(mColorMatrix->components));
  } else if (aMatrix.Length() == 0) {
    mColorMatrix.reset();
  } else {
    return NS_ERROR_INVALID_ARG;
  }

  PresShell* presShell = GetPresShell();
  if (!presShell) {
    return NS_ERROR_FAILURE;
  }

  nsIFrame* frame = presShell->GetRootFrame();
  if (!frame) {
    return NS_ERROR_FAILURE;
  }

  frame->SchedulePaint();

  return NS_OK;
}

NS_IMETHODIMP
nsDocShell::GetColorMatrix(nsTArray<float>& aMatrix) {
  if (mColorMatrix) {
    aMatrix.SetLength(MATRIX_LENGTH);
    static_assert(
        MATRIX_LENGTH * sizeof(float) == sizeof(mColorMatrix->components),
        "Size mismatch for our memcpy");
    memcpy(aMatrix.Elements(), mColorMatrix->components,
           MATRIX_LENGTH * sizeof(float));
  }

  return NS_OK;
}

#undef MATRIX_LENGTH

NS_IMETHODIMP
nsDocShell::GetIsForceReloading(bool* aForceReload) {
  *aForceReload = IsForceReloading();
  return NS_OK;
}

bool nsDocShell::IsForceReloading() { return IsForceReloadType(mLoadType); }

NS_IMETHODIMP
nsDocShell::GetBrowsingContextXPCOM(BrowsingContext** aBrowsingContext) {
  *aBrowsingContext = do_AddRef(mBrowsingContext).take();
  return NS_OK;
}

BrowsingContext* nsDocShell::GetBrowsingContext() { return mBrowsingContext; }

bool nsDocShell::GetIsAttemptingToNavigate() {
  // XXXbz the document.open spec says to abort even if there's just a
  // queued navigation task, sort of.  It's not clear whether browsers
  // actually do that, and we didn't use to do it, so for now let's
  // not do that.
  // https://github.com/whatwg/html/issues/3447 tracks the spec side of this.
  if (mDocumentRequest) {
    // There's definitely a navigation in progress.
    return true;
  }

  // javascript: channels have slightly weird behavior: they're LOAD_BACKGROUND
  // until the script runs, which means they're not sending loadgroup
  // notifications and hence not getting set as mDocumentRequest.  Look through
  // our loadgroup for document-level javascript: loads.
  if (!mLoadGroup) {
    return false;
  }

  nsCOMPtr<nsISimpleEnumerator> requests;
  mLoadGroup->GetRequests(getter_AddRefs(requests));
  bool hasMore = false;
  while (NS_SUCCEEDED(requests->HasMoreElements(&hasMore)) && hasMore) {
    nsCOMPtr<nsISupports> elem;
    requests->GetNext(getter_AddRefs(elem));
    nsCOMPtr<nsIScriptChannel> scriptChannel(do_QueryInterface(elem));
    if (!scriptChannel) {
      continue;
    }

    if (scriptChannel->GetIsDocumentLoad()) {
      // This is a javascript: load that might lead to a new document,
      // hence a navigation.
      return true;
    }
  }

  if (mOngoingNavigation == Some(OngoingNavigation::NavigationID)) {
    return true;
  }

  return mCheckingSessionHistory;
}

mozilla::dom::SessionHistoryInfo* nsDocShell::GetActiveSessionHistoryInfo()
    const {
  return mActiveEntry.get();
}

void nsDocShell::SetLoadingSessionHistoryInfo(
    const mozilla::dom::LoadingSessionHistoryInfo& aLoadingInfo,
    bool aNeedToReportActiveAfterLoadingBecomesActive) {
  // FIXME Would like to assert this, but can't yet.
  // MOZ_ASSERT(!mLoadingEntry);
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("Setting the loading entry on nsDocShell %p to %s", this,
           aLoadingInfo.mInfo.GetURI()->GetSpecOrDefault().get()));
  mLoadingEntry = MakeUnique<LoadingSessionHistoryInfo>(aLoadingInfo);
  mNeedToReportActiveAfterLoadingBecomesActive =
      aNeedToReportActiveAfterLoadingBecomesActive;
}

void nsDocShell::MoveLoadingToActiveEntry(bool aExpired, uint32_t aCacheKey,
                                          nsIURI* aPreviousURI) {
  MOZ_LOG(gSHLog, LogLevel::Debug,
          ("nsDocShell %p MoveLoadingToActiveEntry", this));

  UniquePtr<SessionHistoryInfo> previousActiveEntry(mActiveEntry.release());
  mozilla::UniquePtr<mozilla::dom::LoadingSessionHistoryInfo> loadingEntry;
  mActiveEntryIsLoadingFromSessionHistory =
      mLoadingEntry && mLoadingEntry->mLoadIsFromSessionHistory;
  if (mLoadingEntry) {
    MOZ_LOG(gSHLog, LogLevel::Debug,
            ("Moving the loading entry to the active entry on nsDocShell %p "
             "to %s",
             this, mLoadingEntry->mInfo.GetURI()->GetSpecOrDefault().get()));
    mActiveEntry = MakeUnique<SessionHistoryInfo>(mLoadingEntry->mInfo);
    mLoadingEntry.swap(loadingEntry);
    if (!mActiveEntryIsLoadingFromSessionHistory) {
      if (mNeedToReportActiveAfterLoadingBecomesActive) {
        // Needed to pass various history length WPTs.
        mBrowsingContext->SetActiveSessionHistoryEntry(
            mozilla::Nothing(), mActiveEntry.get(), previousActiveEntry.get(),
            mLoadType,
            /* aUpdatedCacheKey = */ 0, false);
      }
      if (!(previousActiveEntry && previousActiveEntry->IsTransient())) {
        mBrowsingContext->IncrementHistoryEntryCountForBrowsingContext();
      }
    }
  }
  mNeedToReportActiveAfterLoadingBecomesActive = false;

  if (mActiveEntry) {
    if (aCacheKey != 0) {
      mActiveEntry->SetCacheKey(aCacheKey);
    }

    MOZ_ASSERT(loadingEntry);
    uint32_t loadType =
        mLoadType == LOAD_ERROR_PAGE ? mFailedLoadType : mLoadType;

    if (loadingEntry->mLoadId != UINT64_MAX) {
      // We're passing in mCurrentURI, which could be null. SessionHistoryCommit
      // does require a non-null uri if this is for a refresh load of the same
      // URI, but in that case mCurrentURI won't be null here.
      mBrowsingContext->SessionHistoryCommit(
          *loadingEntry, loadType, aPreviousURI, previousActiveEntry.get(),
          false, aExpired, aCacheKey);
    }

    // Only update navigation if the new entry will be persisted (i.e., is not
    // an about: page).
    if (!loadingEntry->mInfo.IsTransient() && GetWindow() &&
        GetWindow()->GetCurrentInnerWindow()) {
      if (RefPtr navigation =
              GetWindow()->GetCurrentInnerWindow()->Navigation()) {
        navigation->InitializeHistoryEntries(loadingEntry->mContiguousEntries,
                                             mActiveEntry.get());

        MOZ_LOG_FMT(gNavigationAPILog, LogLevel::Debug,
                    "Before creating NavigationActivation, "
                    "triggeringEntry={}, triggeringType={}",
                    fmt::ptr(loadingEntry->mPreviousEntry
                                 .map([](auto& entry) { return &entry; })
                                 .valueOr(nullptr)),
                    loadingEntry->mTriggeringNavigationType
                        .map([](NavigationType type) {
                          return fmt::format("{}", type);
                        })
                        .valueOr("none"));
        navigation->CreateNavigationActivationFrom(
            loadingEntry->mPreviousEntry,
            loadingEntry->mTriggeringNavigationType);
      }
    }
  }
}

static bool IsFaviconLoad(nsIRequest* aRequest) {
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (!channel) {
    return false;
  }

  nsCOMPtr<nsILoadInfo> li = channel->LoadInfo();
  return li && li->InternalContentPolicyType() ==
                   nsIContentPolicy::TYPE_INTERNAL_IMAGE_FAVICON;
}

void nsDocShell::RecordSingleChannelId(bool aStartRequest,
                                       nsIRequest* aRequest) {
  // Ignore favicon loads, they don't need to block caching.
  if (IsFaviconLoad(aRequest)) {
    return;
  }

  MOZ_ASSERT_IF(!aStartRequest, mRequestForBlockingFromBFCacheCount > 0);

  mRequestForBlockingFromBFCacheCount += aStartRequest ? 1 : -1;

  if (mBrowsingContext->GetCurrentWindowContext()) {
    // We have three states: no request, one request with an id and
    // eiher one request without an id or multiple requests. Nothing() is no
    // request, Some(non-zero) is one request with an id and Some(0) is one
    // request without an id or multiple requests.
    Maybe<uint64_t> singleChannelId;
    if (mRequestForBlockingFromBFCacheCount > 1) {
      singleChannelId = Some(0);
    } else if (mRequestForBlockingFromBFCacheCount == 1) {
      nsCOMPtr<nsIIdentChannel> identChannel;
      if (aStartRequest) {
        identChannel = do_QueryInterface(aRequest);
      } else {
        // aChannel is the channel that's being removed, but we need to check if
        // the remaining channel in the loadgroup has an id.
        nsCOMPtr<nsISimpleEnumerator> requests;
        mLoadGroup->GetRequests(getter_AddRefs(requests));
        for (const auto& request : SimpleEnumerator<nsIRequest>(requests)) {
          if (!IsFaviconLoad(request) &&
              !!(identChannel = do_QueryInterface(request))) {
            break;
          }
        }
      }

      if (identChannel) {
        singleChannelId = Some(identChannel->ChannelId());
      } else {
        singleChannelId = Some(0);
      }
    } else {
      MOZ_ASSERT(mRequestForBlockingFromBFCacheCount == 0);
      singleChannelId = Nothing();
    }

    if (MOZ_UNLIKELY(MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Verbose))) {
      nsAutoCString uri("[no uri]");
      if (mCurrentURI) {
        uri = mCurrentURI->GetSpecOrDefault();
      }
      if (singleChannelId.isNothing()) {
        MOZ_LOG(gSHIPBFCacheLog, LogLevel::Verbose,
                ("Loadgroup for %s doesn't have any requests relevant for "
                 "blocking BFCache",
                 uri.get()));
      } else if (singleChannelId.value() == 0) {
        MOZ_LOG(gSHIPBFCacheLog, LogLevel::Verbose,
                ("Loadgroup for %s has multiple requests relevant for blocking "
                 "BFCache",
                 uri.get()));
      } else {
        MOZ_LOG(gSHIPBFCacheLog, LogLevel::Verbose,
                ("Loadgroup for %s has one request with id %" PRIu64
                 " relevant for blocking BFCache",
                 uri.get(), singleChannelId.value()));
      }
    }

    if (mSingleChannelId != singleChannelId) {
      mSingleChannelId = singleChannelId;
      WindowGlobalChild* wgc =
          mBrowsingContext->GetCurrentWindowContext()->GetWindowGlobalChild();
      if (wgc) {
        wgc->SendSetSingleChannelId(singleChannelId);
      }
    }
  }
}

NS_IMETHODIMP
nsDocShell::OnStartRequest(nsIRequest* aRequest) {
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Verbose))) {
    nsAutoCString uri("[no uri]");
    if (mCurrentURI) {
      uri = mCurrentURI->GetSpecOrDefault();
    }
    nsAutoCString name;
    aRequest->GetName(name);
    MOZ_LOG(gSHIPBFCacheLog, LogLevel::Verbose,
            ("Adding request %s to loadgroup for %s", name.get(), uri.get()));
  }
  RecordSingleChannelId(true, aRequest);
  return nsDocLoader::OnStartRequest(aRequest);
}

NS_IMETHODIMP
nsDocShell::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gSHIPBFCacheLog, LogLevel::Verbose))) {
    nsAutoCString uri("[no uri]");
    if (mCurrentURI) {
      uri = mCurrentURI->GetSpecOrDefault();
    }
    nsAutoCString name;
    aRequest->GetName(name);
    MOZ_LOG(
        gSHIPBFCacheLog, LogLevel::Verbose,
        ("Removing request %s from loadgroup for %s", name.get(), uri.get()));
  }
  RecordSingleChannelId(false, aRequest);
  return nsDocLoader::OnStopRequest(aRequest, aStatusCode);
}

void nsDocShell::MaybeDisconnectChildListenersOnPageHide() {
  MOZ_RELEASE_ASSERT(XRE_IsContentProcess());

  if (mChannelToDisconnectOnPageHide != 0 && mLoadGroup) {
    nsCOMPtr<nsISimpleEnumerator> requests;
    mLoadGroup->GetRequests(getter_AddRefs(requests));
    for (const auto& request : SimpleEnumerator<nsIRequest>(requests)) {
      RefPtr<DocumentChannel> channel = do_QueryObject(request);
      if (channel && channel->ChannelId() == mChannelToDisconnectOnPageHide) {
        static_cast<DocumentChannelChild*>(channel.get())
            ->DisconnectChildListeners(NS_BINDING_ABORTED, NS_BINDING_ABORTED);
      }
    }
    mChannelToDisconnectOnPageHide = 0;
  }
}

bool nsDocShell::IsSameDocumentAsActiveEntry(
    const mozilla::dom::SessionHistoryInfo& aSHInfo) {
  return mActiveEntry ? mActiveEntry->SharesDocumentWith(aSHInfo) : false;
}

// https://html.spec.whatwg.org/#nav-window
nsPIDOMWindowInner* nsDocShell::GetActiveWindow() {
  nsPIDOMWindowOuter* outer = GetWindow();
  return outer ? outer->GetCurrentInnerWindow() : nullptr;
}

// https://html.spec.whatwg.org/#inform-the-navigation-api-about-aborting-navigation
void nsDocShell::InformNavigationAPIAboutAbortingNavigation() {
  // Step 1
  // We really have no idea what this means.

  // No ongoing navigations if we don't have a window.
  RefPtr<nsPIDOMWindowInner> window = GetActiveWindow();
  if (!window) {
    return;
  }

  // Step 2
  RefPtr<Navigation> navigation = window->Navigation();
  if (!navigation) {
    return;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(navigation->GetRelevantGlobal())) {
    return;
  }

  // Steps 3 & 4
  // See https://github.com/whatwg/html/issues/11579
  navigation->InnerInformAboutAbortingNavigation(jsapi.cx());
}

// https://html.spec.whatwg.org/#inform-the-navigation-api-about-child-navigable-destruction
void nsDocShell::InformNavigationAPIAboutChildNavigableDestruction() {
  // Step 1
  InformNavigationAPIAboutAbortingNavigation();

  // No ongoing navigations if we don't have a window.
  RefPtr<nsPIDOMWindowInner> window = GetActiveWindow();
  if (!window) {
    return;
  }

  // Step 2
  RefPtr<Navigation> navigation = window->Navigation();
  if (!navigation) {
    return;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(navigation->GetRelevantGlobal())) {
    return;
  }

  navigation->InformAboutChildNavigableDestruction(jsapi.cx());
}

// https://html.spec.whatwg.org/#set-the-ongoing-navigation
void nsDocShell::SetOngoingNavigation(
    const Maybe<OngoingNavigation>& aOngoingNavigation) {
  // We currently only use #set-the-ongoing-navigation to call,
  // #inform-the-navigation-api-about-aborting-navigation, but really it should
  // be used for more. The spec keeps a piece of state on the navigable:
  // https://html.spec.whatwg.org/#ongoing-navigation. Spec uses it for several
  // things, for example right here in #set-the-ongoing-navigation to make sure
  // that we don't call #inform-the-navigation-api-about-aborting-navigation if
  // we're setting it to the same value. We currently only care about aborting
  // the currently firing navigate event. Also, in reality, this is very much
  // related to nsDocShell::GetIsAttemptingToNavigate() which is what we
  // currently use to determine if we need to stop an ongoing navigation in
  // Document::Open, whereas the spec checks if the ongoing navigation is a
  // NavigationID.

  // Step 1, with the exception that we assume setting the ongoing navigation to
  // an id always means a fresh id.
  if (aOngoingNavigation == mOngoingNavigation &&
      aOngoingNavigation != Some(OngoingNavigation::NavigationID)) {
    return;
  }

  // Step 2
  InformNavigationAPIAboutAbortingNavigation();

  // Step 3
  mOngoingNavigation = aOngoingNavigation;
}

void nsDocShell::StopPendingJavascriptURLNavigations() {
  nsCOMPtr<nsISimpleEnumerator> requests;
  mLoadGroup->GetRequests(getter_AddRefs(requests));
  bool hasMore;
  while (NS_SUCCEEDED(requests->HasMoreElements(&hasMore)) && hasMore) {
    nsCOMPtr<nsISupports> elem;
    requests->GetNext(getter_AddRefs(elem));
    nsCOMPtr<nsIScriptChannel> script = do_QueryInterface(elem);
    if (script) {
      nsCOMPtr<nsIRequest> request = do_QueryInterface(elem);
      MOZ_ASSERT(request);
      mLoadGroup->CancelRequest(
          request, "nsDocShell::StopPendingJavascriptNavigations"_ns,
          NS_BINDING_ABORTED);
    }
  }
}
