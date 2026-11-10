/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Local Includes
#include "nsWebBrowser.h"

// Hack: nsIOpenWindowInfo depends on this without being able to include it
#include "mozilla/Assertions.h"
#include "mozilla/dom/BrowserParent.h"

// Helper Classes
#include "nsGfxCIID.h"
#include "nsWidgetsCID.h"

#include "gfxUtils.h"
#include "mozilla/gfx/2D.h"

// Interfaces Needed
#include "gfxContext.h"
#include "nsReadableUtils.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIWebBrowserChrome.h"
#include "nsPIDOMWindow.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsIURI.h"
#include "nsFocusManager.h"
#include "nsILoadContext.h"
#include "nsComponentManagerUtils.h"
#include "nsDocShell.h"
#include "nsServiceManagerUtils.h"
#include "WindowRenderer.h"
#include "nsOpenWindowInfo.h"

#include "mozilla/dom/Element.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/LoadURIOptionsBinding.h"
#include "mozilla/dom/WindowGlobalChild.h"

// for painting the background window
#include "mozilla/LookAndFeel.h"
#include "mozilla/ServoStyleConsts.h"

// Printing Includes
#include "nsIWebBrowserPrint.h"
#include "nsIDocumentViewer.h"

// PSM2 includes
#include "nsISecureBrowserUI.h"
#include "nsXULAppAPI.h"

#include "nsContentUtils.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_ui.h"
#include "nsITooltipListener.h"
#include "nsITooltipTextProvider.h"
#include "nsCTooltipTextProvider.h"
#include "nsIDOMEventListener.h"
#include "nsINode.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/MouseEvent.h"
#include "nsPIWindowRoot.h"
#include "nsPIWindowWatcher.h"
#include "nsIRemoteTab.h"
#include "nsIBrowserChild.h"
#include "nsIDroppedLinkHandler.h"
#include "nsXULTooltipListener.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/Try.h"
#include "mozilla/dom/DragEvent.h"
#include "mozilla/dom/Event.h"
#include "mozilla/PresShell.h"
#include "mozilla/TextEvents.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::layers;

// A helper routine that navigates the tricky path from a |nsWebBrowser| to
// a |EventTarget| via the window root and chrome event handler.
static nsresult GetDOMEventTarget(nsWebBrowser* aInBrowser,
                                  EventTarget** aTarget) {
  if (!aInBrowser) {
    return NS_ERROR_INVALID_POINTER;
  }

  nsPIDOMWindowOuter* outerWindow = aInBrowser->GetWindow();
  if (!outerWindow) {
    return NS_ERROR_FAILURE;
  }

  nsPIDOMWindowOuter* rootWindow = outerWindow->GetPrivateRoot();
  NS_ENSURE_TRUE(rootWindow, NS_ERROR_FAILURE);
  nsCOMPtr<EventTarget> target = rootWindow->GetChromeEventHandler();
  NS_ENSURE_TRUE(target, NS_ERROR_FAILURE);
  target.forget(aTarget);

  return NS_OK;
}

// The class that listens to the chrome events and tells the embedding chrome to
// show tooltips, as appropriate. Handles registering itself with the DOM with
// AddChromeListeners() and removing itself with RemoveChromeListeners().
class ChromeTooltipListener final : public nsIDOMEventListener {
 protected:
  virtual ~ChromeTooltipListener();

 public:
  NS_DECL_ISUPPORTS

  ChromeTooltipListener(nsWebBrowser* aInBrowser,
                        nsIWebBrowserChrome* aInChrome);

  NS_DECL_NSIDOMEVENTLISTENER
  NS_IMETHOD MouseMove(mozilla::dom::Event* aMouseEvent);

  // Add/remove the relevant listeners, based on what interfaces the embedding
  // chrome implements.
  NS_IMETHOD AddChromeListeners();
  NS_IMETHOD RemoveChromeListeners();

  NS_IMETHOD HideTooltip();

  bool WebProgressShowedTooltip(nsIWebProgress* aWebProgress);

 private:
  // pixel tolerance for mousemove event
  static constexpr CSSIntCoord kTooltipMouseMoveTolerance = 7;

  NS_IMETHOD AddTooltipListener();
  NS_IMETHOD RemoveTooltipListener();

  NS_IMETHOD ShowTooltip(int32_t aInXCoords, int32_t aInYCoords,
                         const nsAString& aInTipText,
                         const nsAString& aDirText);
  nsITooltipTextProvider* GetTooltipTextProvider();

  nsWebBrowser* mWebBrowser;
  nsCOMPtr<mozilla::dom::EventTarget> mEventTarget;
  nsCOMPtr<nsITooltipTextProvider> mTooltipTextProvider;

  // This must be a strong ref in order to make sure we can hide the tooltip if
  // the window goes away while we're displaying one. If we don't hold a strong
  // ref, the chrome might have been disposed of before we get a chance to tell
  // it, and no one would ever tell us of that fact.
  nsCOMPtr<nsIWebBrowserChrome> mWebBrowserChrome;

  bool mTooltipListenerInstalled;

  nsCOMPtr<nsITimer> mTooltipTimer;
  static void sTooltipCallback(nsITimer* aTimer, void* aListener);

  // Mouse coordinates for last mousemove event we saw
  CSSIntPoint mMouseClientPoint;

  // Mouse coordinates for tooltip event
  LayoutDeviceIntPoint mMouseScreenPoint;

  bool mShowingTooltip;

  bool mTooltipShownOnce;

  // The string of text that we last displayed.
  nsString mLastShownTooltipText;

  nsWeakPtr mLastDocshell;

  // The node hovered over that fired the timer. This may turn into the node
  // that triggered the tooltip, but only if the timer ever gets around to
  // firing. This is a strong reference, because the tooltip content can be
  // destroyed while we're waiting for the tooltip to pop up, and we need to
  // detect that. It's set only when the tooltip timer is created and launched.
  // The timer must either fire or be cancelled (or possibly released?), and we
  // release this reference in each of those cases. So we don't leak.
  nsCOMPtr<nsINode> mPossibleTooltipNode;
};

nsWebBrowser::nsWebBrowser(int aItemType)
    : mContentType(aItemType),
      mWillChangeProcess(false),
      mParentWidget(nullptr),
      mPrimaryContentShell(nullptr),
      mWebBrowserChrome(nullptr),
      mOwnerWin(nullptr),
      mOwnerRequestor(nullptr) {}

nsWebBrowser::~nsWebBrowser() { InternalDestroy(); }

nsIWidget* nsWebBrowser::EnsureWidget() {
  MOZ_DIAGNOSTIC_ASSERT(mParentWidget);
  return mParentWidget;
}

/* static */
nsresult nsWebBrowser::Create(nsIWebBrowserChrome* aContainerWindow,
                              nsIWidget* aParentWidget,
                              dom::BrowsingContext* aBrowsingContext,
                              dom::WindowGlobalChild* aInitialWindowChild,
                              nsIOpenWindowInfo* aOpenWindowInfo,
                              nsWebBrowser** aWebBrowser) {
  MOZ_ASSERT(aOpenWindowInfo, "Must have openwindowinfo");
  MOZ_ASSERT_IF(aInitialWindowChild,
                aInitialWindowChild->BrowsingContext() == aBrowsingContext);
  MOZ_ASSERT_IF(aInitialWindowChild,
                aInitialWindowChild->DocumentPrincipal() ==
                    aOpenWindowInfo->PrincipalToInheritForAboutBlank());

  RefPtr<nsWebBrowser> browser = new nsWebBrowser(
      aBrowsingContext->IsContent() ? typeContentWrapper : typeChromeWrapper);

  browser->SetWebBrowserChrome(aContainerWindow);
  browser->mParentWidget = aParentWidget;

  nsCOMPtr<nsIWidget> docShellParentWidget = browser->EnsureWidget();
  if (NS_WARN_IF(!docShellParentWidget)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  uint64_t outerWindowId =
      aInitialWindowChild ? aInitialWindowChild->OuterWindowId() : 0;

  RefPtr<nsDocShell> docShell =
      nsDocShell::Create(aBrowsingContext, outerWindowId);
  if (NS_WARN_IF(!docShell)) {
    return NS_ERROR_FAILURE;
  }
  browser->SetDocShell(docShell);
  MOZ_ASSERT(browser->mDocShell == docShell);

  // HACK ALERT - this registration registers the nsWebBrowser as a
  // nsIWebBrowserListener so it can setup its MouseListener in one of the
  // progress callbacks. If we can register the MouseListener another way, this
  // registration can go away, and nsWebBrowser can stop implementing
  // nsIWebProgressListener.
  (void)docShell->AddProgressListener(browser, nsIWebProgress::NOTIFY_ALL);

  docShell->SetTreeOwner(browser);

  // If the webbrowser is a content docshell item then we won't hear any
  // events from subframes. To solve that we install our own chrome event
  // handler that always gets called (even for subframes) for any bubbling
  // event.
  MOZ_TRY(docShell->InitWindow(docShellParentWidget, 0, 0, 0, 0,
                               aOpenWindowInfo, aInitialWindowChild));

  browser->AddToWatcher();  // evil twin of Remove in SetDocShell(0)
  browser->AddChromeListeners();

  browser.forget(aWebBrowser);
  return NS_OK;
}

void nsWebBrowser::InternalDestroy() {
  RemoveChromeListeners();
  SetDocShell(nullptr);
  mPrompter = nullptr;
  mAuthPrompter = nullptr;
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsWebBrowser)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsWebBrowser)

NS_IMPL_CYCLE_COLLECTION_WEAK(nsWebBrowser, mDocShell)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsWebBrowser)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWebNavigation)
  NS_INTERFACE_MAP_ENTRY(nsIWebNavigation)
  NS_INTERFACE_MAP_ENTRY(nsIDocShellTreeItem)
  NS_INTERFACE_MAP_ENTRY(nsIDocShellTreeOwner)
  NS_INTERFACE_MAP_ENTRY(nsIBaseWindow)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIWebProgressListener)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

///*****************************************************************************
// nsWebBrowser::nsIInterfaceRequestor
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::GetInterface(const nsIID& aIID, void** aSink) {
  NS_ENSURE_ARG_POINTER(aSink);

  if (NS_SUCCEEDED(QueryInterface(aIID, aSink))) {
    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsIPrompt))) {
    nsCOMPtr<nsIPrompt> prompt;
    EnsurePrompter();
    prompt = mPrompter;
    if (prompt) {
      prompt.forget(aSink);
      return NS_OK;
    }
    return NS_NOINTERFACE;
  }

  if (aIID.Equals(NS_GET_IID(nsIAuthPrompt))) {
    nsCOMPtr<nsIAuthPrompt> prompt;
    EnsureAuthPrompter();
    prompt = mAuthPrompter;
    if (prompt) {
      prompt.forget(aSink);
      return NS_OK;
    }
    return NS_NOINTERFACE;
  }

  if (mDocShell) {
    if (aIID.Equals(NS_GET_IID(nsIWebBrowserPrint))) {
      nsCOMPtr<nsIDocumentViewer> viewer;
      mDocShell->GetDocViewer(getter_AddRefs(viewer));
      if (!viewer) {
        return NS_NOINTERFACE;
      }

      nsCOMPtr<nsIWebBrowserPrint> webBrowserPrint(do_QueryInterface(viewer));
      nsIWebBrowserPrint* print = (nsIWebBrowserPrint*)webBrowserPrint.get();
      NS_ASSERTION(print, "This MUST support this interface!");
      NS_ADDREF(print);
      *aSink = print;
      return NS_OK;
    }
    if (NS_SUCCEEDED(mDocShell->GetInterface(aIID, aSink))) {
      return NS_OK;
    }
  }

  if (nsCOMPtr<nsIInterfaceRequestor> req = GetOwnerRequestor()) {
    return req->GetInterface(aIID, aSink);
  }

  return NS_NOINTERFACE;
}

//*****************************************************************************
// nsWebBrowser::nsIDocShellTreeItem
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::GetName(nsAString& aName) {
  if (mDocShell) {
    mDocShell->GetName(aName);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::SetName(const nsAString& aName) {
  if (mDocShell) {
    return mDocShell->SetName(aName);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::NameEquals(const nsAString& aName, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  if (mDocShell) {
    return mDocShell->NameEquals(aName, aResult);
  }

  return NS_OK;
}

/* virtual */
int32_t nsWebBrowser::ItemType() { return mContentType; }

NS_IMETHODIMP
nsWebBrowser::GetItemType(int32_t* aItemType) {
  NS_ENSURE_ARG_POINTER(aItemType);

  *aItemType = ItemType();
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetInProcessParent(nsIDocShellTreeItem** aParent) {
  *aParent = nullptr;
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetInProcessSameTypeParent(nsIDocShellTreeItem** aParent) {
  *aParent = nullptr;

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetInProcessRootTreeItem(nsIDocShellTreeItem** aRootTreeItem) {
  NS_ENSURE_ARG_POINTER(aRootTreeItem);
  *aRootTreeItem = static_cast<nsIDocShellTreeItem*>(this);

  nsCOMPtr<nsIDocShellTreeItem> parent;
  NS_ENSURE_SUCCESS(GetInProcessParent(getter_AddRefs(parent)),
                    NS_ERROR_FAILURE);
  while (parent) {
    *aRootTreeItem = parent;
    NS_ENSURE_SUCCESS(
        (*aRootTreeItem)->GetInProcessParent(getter_AddRefs(parent)),
        NS_ERROR_FAILURE);
  }
  NS_ADDREF(*aRootTreeItem);
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetInProcessSameTypeRootTreeItem(
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

dom::Document* nsWebBrowser::GetDocument() {
  return mDocShell ? mDocShell->GetDocument() : nullptr;
}

nsPIDOMWindowOuter* nsWebBrowser::GetWindow() {
  return mDocShell ? mDocShell->GetWindow() : nullptr;
}

NS_IMETHODIMP
nsWebBrowser::GetBrowsingContextXPCOM(dom::BrowsingContext** aBrowsingContext) {
  NS_ENSURE_STATE(mDocShell);
  return mDocShell->GetBrowsingContextXPCOM(aBrowsingContext);
}

dom::BrowsingContext* nsWebBrowser::GetBrowsingContext() {
  return mDocShell->GetBrowsingContext();
}

NS_IMETHODIMP
nsWebBrowser::GetDomWindow(mozIDOMWindowProxy** aWindow) {
  if (!mDocShell) return NS_ERROR_NOT_INITIALIZED;
  return mDocShell->GetDomWindow(aWindow);
}

NS_IMETHODIMP
nsWebBrowser::GetTreeOwner(nsIDocShellTreeOwner** aTreeOwner) {
  NS_ENSURE_ARG_POINTER(aTreeOwner);
  *aTreeOwner = do_AddRef(this).take();
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::SetTreeOwner(nsIDocShellTreeOwner* aTreeOwner) {
  MOZ_ASSERT_UNREACHABLE("nsWebBrowser is never a child docshell");
  return NS_ERROR_NOT_IMPLEMENTED;
}

//*****************************************************************************
// nsWebBrowser::nsIDocShellTreeItem
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::GetInProcessChildCount(int32_t* aChildCount) {
  NS_ENSURE_ARG_POINTER(aChildCount);
  *aChildCount = 0;
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::AddChild(nsIDocShellTreeItem* aChild) {
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsWebBrowser::RemoveChild(nsIDocShellTreeItem* aChild) {
  return NS_ERROR_UNEXPECTED;
}

NS_IMETHODIMP
nsWebBrowser::GetInProcessChildAt(int32_t aIndex,
                                  nsIDocShellTreeItem** aChild) {
  return NS_ERROR_UNEXPECTED;
}

//*****************************************************************************
// nsWebBrowser::nsIWebNavigation
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::GetCanGoBack(bool* aCanGoBack) {
  NS_ENSURE_STATE(mDocShell);

  return mDocShell->GetCanGoBack(aCanGoBack);
}

NS_IMETHODIMP
nsWebBrowser::GetCanGoBackIgnoringUserInteraction(bool* aCanGoBack) {
  NS_ENSURE_STATE(mDocShell);

  return mDocShell->GetCanGoBackIgnoringUserInteraction(aCanGoBack);
}

NS_IMETHODIMP
nsWebBrowser::GetCanGoForward(bool* aCanGoForward) {
  NS_ENSURE_STATE(mDocShell);

  return mDocShell->GetCanGoForward(aCanGoForward);
}

NS_IMETHODIMP
nsWebBrowser::GoBack(bool aRequireUserInteraction, bool aUserActivation) {
  NS_ENSURE_STATE(mDocShell);

  RefPtr<nsDocShell> docShell = mDocShell;
  return docShell->GoBack(aRequireUserInteraction, aUserActivation);
}

NS_IMETHODIMP
nsWebBrowser::GoForward(bool aRequireUserInteraction, bool aUserActivation) {
  NS_ENSURE_STATE(mDocShell);

  RefPtr<nsDocShell> docShell = mDocShell;
  return docShell->GoForward(aRequireUserInteraction, aUserActivation);
}

nsresult nsWebBrowser::LoadURI(nsIURI* aURI,
                               const dom::LoadURIOptions& aLoadURIOptions) {
#ifndef ANDROID
  MOZ_ASSERT(aLoadURIOptions.mTriggeringPrincipal,
             "nsWebBrowser::LoadURI - Need a valid triggeringPrincipal");
#endif
  NS_ENSURE_STATE(mDocShell);

  RefPtr<nsDocShell> docShell = mDocShell;
  return docShell->LoadURI(aURI, aLoadURIOptions);
}

NS_IMETHODIMP
nsWebBrowser::LoadURIFromScript(nsIURI* aURI,
                                JS::Handle<JS::Value> aLoadURIOptions,
                                JSContext* aCx) {
  // generate dictionary for loadURIOptions and forward call
  dom::LoadURIOptions loadURIOptions;
  if (!loadURIOptions.Init(aCx, aLoadURIOptions)) {
    return NS_ERROR_INVALID_ARG;
  }
  return LoadURI(aURI, loadURIOptions);
}

nsresult nsWebBrowser::FixupAndLoadURIString(
    const nsAString& aURI, const dom::LoadURIOptions& aLoadURIOptions) {
#ifndef ANDROID
  MOZ_ASSERT(
      aLoadURIOptions.mTriggeringPrincipal,
      "nsWebBrowser::FixupAndLoadURIString - Need a valid triggeringPrincipal");
#endif
  NS_ENSURE_STATE(mDocShell);

  RefPtr<nsDocShell> docShell = mDocShell;
  return docShell->FixupAndLoadURIString(aURI, aLoadURIOptions);
}

NS_IMETHODIMP
nsWebBrowser::FixupAndLoadURIStringFromScript(
    const nsAString& aURI, JS::Handle<JS::Value> aLoadURIOptions,
    JSContext* aCx) {
  // generate dictionary for loadURIOptions and forward call
  dom::LoadURIOptions loadURIOptions;
  if (!loadURIOptions.Init(aCx, aLoadURIOptions)) {
    return NS_ERROR_INVALID_ARG;
  }
  return FixupAndLoadURIString(aURI, loadURIOptions);
}

NS_IMETHODIMP
nsWebBrowser::ResumeRedirectedLoad(uint64_t aIdentifier) {
  NS_ENSURE_STATE(mDocShell);

  return mDocShell->ResumeRedirectedLoad(aIdentifier);
}

NS_IMETHODIMP
nsWebBrowser::Reload(uint32_t aReloadFlags) {
  NS_ENSURE_STATE(mDocShell);

  RefPtr<nsDocShell> docShell = mDocShell;
  return docShell->Reload(aReloadFlags);
}

NS_IMETHODIMP
nsWebBrowser::GotoIndex(int32_t aIndex, bool aUserActivation) {
  NS_ENSURE_STATE(mDocShell);

  RefPtr<nsDocShell> docShell = mDocShell;
  return docShell->GotoIndex(aIndex, aUserActivation);
}

NS_IMETHODIMP
nsWebBrowser::Stop(uint32_t aStopFlags) {
  NS_ENSURE_STATE(mDocShell);

  return mDocShell->Stop(aStopFlags);
}

NS_IMETHODIMP
nsWebBrowser::GetCurrentURI(nsIURI** aURI) {
  NS_ENSURE_STATE(mDocShell);

  return mDocShell->GetCurrentURI(aURI);
}

// XXX(nika): Consider making the mozilla::dom::ChildSHistory version the
// canonical one?
NS_IMETHODIMP
nsWebBrowser::GetSessionHistoryXPCOM(nsISupports** aSessionHistory) {
  NS_ENSURE_ARG_POINTER(aSessionHistory);
  *aSessionHistory = nullptr;
  if (mDocShell) {
    return mDocShell->GetSessionHistoryXPCOM(aSessionHistory);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetDocument(dom::Document** aDocument) {
  NS_ENSURE_STATE(mDocShell);

  return mDocShell->GetDocument(aDocument);
}

void nsWebBrowser::SetAllowDNSPrefetch(bool aAllowPrefetch) {
  MOZ_ASSERT(mDocShell);
  mDocShell->SetAllowDNSPrefetch(aAllowPrefetch);
}

//*****************************************************************************
// nsWebBrowser::nsIDocShellTreeOwner
//*****************************************************************************

void nsWebBrowser::EnsurePrompter() {
  if (mPrompter) {
    return;
  }

  nsCOMPtr<nsIWindowWatcher> wwatch(do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  if (wwatch) {
    nsCOMPtr<nsPIDOMWindowOuter> domWindow = GetWindow();
    if (domWindow) {
      wwatch->GetNewPrompter(domWindow, getter_AddRefs(mPrompter));
    }
  }
}

void nsWebBrowser::EnsureAuthPrompter() {
  if (mAuthPrompter) {
    return;
  }

  nsCOMPtr<nsIWindowWatcher> wwatch(do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  if (wwatch) {
    nsCOMPtr<nsPIDOMWindowOuter> domWindow = GetWindow();
    if (domWindow) {
      wwatch->GetNewAuthPrompter(domWindow, getter_AddRefs(mAuthPrompter));
    }
  }
}

void nsWebBrowser::AddToWatcher() {
  nsCOMPtr<nsPIDOMWindowOuter> domWindow = GetWindow();
  if (!domWindow) {
    return;
  }
  nsCOMPtr<nsPIWindowWatcher> wwatch(
      do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  if (!wwatch) {
    return;
  }
  if (nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome()) {
    wwatch->AddWindow(domWindow, webBrowserChrome);
  }
}

void nsWebBrowser::RemoveFromWatcher() {
  nsCOMPtr<nsPIDOMWindowOuter> domWindow = GetWindow();
  if (!domWindow) {
    return;
  }
  if (nsCOMPtr<nsPIWindowWatcher> wwatch =
          do_GetService(NS_WINDOWWATCHER_CONTRACTID)) {
    wwatch->RemoveWindow(domWindow);
  }
}

NS_IMETHODIMP
nsWebBrowser::ContentShellAdded(nsIDocShellTreeItem* aContentShell,
                                bool aPrimary) {
  aContentShell->SetTreeOwner(this);

  if (aPrimary) {
    mPrimaryContentShell = aContentShell;
    mPrimaryRemoteTab = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::ContentShellRemoved(nsIDocShellTreeItem* aContentShell) {
  if (mPrimaryContentShell == aContentShell) {
    mPrimaryContentShell = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetPrimaryContentShell(nsIDocShellTreeItem** aShell) {
  NS_ENSURE_ARG_POINTER(aShell);

  nsCOMPtr<nsIDocShellTreeItem> shell;
  if (!mPrimaryRemoteTab) {
    shell = mPrimaryContentShell ? mPrimaryContentShell : mDocShell;
  }
  shell.forget(aShell);

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::RemoteTabAdded(nsIRemoteTab* aTab, bool aPrimary) {
  if (aPrimary) {
    mPrimaryRemoteTab = aTab;
    mPrimaryContentShell = nullptr;
  } else if (mPrimaryRemoteTab == aTab) {
    mPrimaryRemoteTab = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::RemoteTabRemoved(nsIRemoteTab* aTab) {
  if (aTab == mPrimaryRemoteTab) {
    mPrimaryRemoteTab = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetPrimaryRemoteTab(nsIRemoteTab** aTab) {
  nsCOMPtr<nsIRemoteTab> tab = mPrimaryRemoteTab;
  tab.forget(aTab);
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetPrimaryContentBrowsingContext(
    mozilla::dom::BrowsingContext** aBc) {
  if (mPrimaryRemoteTab) {
    return mPrimaryRemoteTab->GetBrowsingContext(aBc);
  }
  if (mPrimaryContentShell) {
    return mPrimaryContentShell->GetBrowsingContextXPCOM(aBc);
  }
  if (mDocShell) {
    return mDocShell->GetBrowsingContextXPCOM(aBc);
  }
  *aBc = nullptr;
  return NS_OK;
}

//*****************************************************************************
// nsWebBrowser::nsIBaseWindow
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::Destroy() {
  nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
  if (webBrowserChrome) {
    // XXX: this is weird, but we used to call a method here
    // (webBrowserChrome->DestroyBrowserWindow()) whose implementations all
    // failed like this, so...
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  return NS_ERROR_NULL_POINTER;
}

double nsWebBrowser::GetWidgetCSSToDeviceScale() {
  return mParentWidget ? mParentWidget->GetDefaultScale().scale : 1.0;
}

NS_IMETHODIMP
nsWebBrowser::GetDevicePixelsPerDesktopPixel(double* aScale) {
  *aScale =
      mParentWidget ? mParentWidget->GetDesktopToDeviceScale().scale : 1.0;
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::SetPositionDesktopPix(int32_t aX, int32_t aY) {
  double scale = 1.0;
  GetDevicePixelsPerDesktopPixel(&scale);
  return SetPosition(NSToIntRound(aX * scale), NSToIntRound(aY * scale));
}

NS_IMETHODIMP
nsWebBrowser::SetPosition(int32_t aX, int32_t aY) {
  return SetDimensions({Some(aX), Some(aY), Nothing(), Nothing()});
}

NS_IMETHODIMP
nsWebBrowser::GetPosition(int32_t* aX, int32_t* aY) {
  return GetPositionAndSize(aX, aY, nullptr, nullptr);
}

NS_IMETHODIMP
nsWebBrowser::SetSize(int32_t aCX, int32_t aCY, bool /*aRepaint*/) {
  return SetDimensions({Nothing(), Nothing(), Some(aCX), Some(aCY)});
}

NS_IMETHODIMP
nsWebBrowser::GetSize(int32_t* aCX, int32_t* aCY) {
  return GetPositionAndSize(nullptr, nullptr, aCX, aCY);
}

NS_IMETHODIMP
nsWebBrowser::SetPositionAndSize(int32_t aX, int32_t aY, int32_t aCX,
                                 int32_t aCY, uint32_t /*aFlags*/) {
  return SetDimensions({Some(aX), Some(aY), Some(aCX), Some(aCY)});
}

NS_IMETHODIMP
nsWebBrowser::GetPositionAndSize(int32_t* aX, int32_t* aY, int32_t* aCX,
                                 int32_t* aCY) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->GetPositionAndSize(aX, aY, aCX, aCY);
  }

  nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
  NS_ENSURE_STATE(webBrowserChrome);
  return webBrowserChrome->GetDimensions(aX, aY, aCX, aCY);
}

NS_IMETHODIMP
nsWebBrowser::SetDimensions(DimensionRequest&& aRequest) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->SetDimensions(std::move(aRequest));
  }

  nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
  NS_ENSURE_STATE(webBrowserChrome);
  return webBrowserChrome->SetDimensions(std::move(aRequest));
}

NS_IMETHODIMP
nsWebBrowser::GetParentWidget(nsIWidget** /*aParentWidget*/) {
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsWebBrowser::SetParentWidget(nsIWidget* /*aParentWidget*/) {
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsWebBrowser::GetNativeHandle(nsAString& /*aNativeHandle*/) {
  // the nativeHandle should be accessed from nsIAppWindow
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowser::GetVisibility(bool* aVisibility) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->GetVisibility(aVisibility);
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowser::SetVisibility(bool aVisibility) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->SetVisibility(aVisibility);
  }
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsWebBrowser::GetEnabled(bool* aEnabled) {
  NS_ENSURE_ARG_POINTER(aEnabled);
  *aEnabled = true;
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowser::SetEnabled(bool /*aEnabled*/) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
nsWebBrowser::GetMainWidget(nsIWidget** /*aMainWidget*/) {
  return NS_ERROR_NULL_POINTER;
}

//*****************************************************************************
// nsWebBrowser::nsIWebProgressListener
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::OnProgressChange(nsIWebProgress* /*aProgress*/,
                               nsIRequest* /*aRequest*/,
                               int32_t /*aCurSelfProgress*/,
                               int32_t /*aMaxSelfProgress*/,
                               int32_t /*aCurTotalProgress*/,
                               int32_t /*aMaxTotalProgress*/) {
  // In the absence of DOM document creation event, this method is the
  // most convenient place to install the mouse listener on the
  // DOM document.
  return AddChromeListeners();
}

NS_IMETHODIMP
nsWebBrowser::OnStateChange(nsIWebProgress* /*aProgress*/,
                            nsIRequest* /*aRequest*/,
                            uint32_t /*aProgressStateFlags*/,
                            nsresult /*aStatus*/) {
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::OnLocationChange(nsIWebProgress* aWebProgress,
                               nsIRequest* /*aRequest*/, nsIURI* /*aURI*/,
                               uint32_t aFlags) {
  if (mChromeTooltipListener && aWebProgress &&
      !(aFlags & nsIWebProgressListener::LOCATION_CHANGE_SAME_DOCUMENT) &&
      mChromeTooltipListener->WebProgressShowedTooltip(aWebProgress)) {
    mChromeTooltipListener->HideTooltip();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::OnStatusChange(nsIWebProgress* /*aWebProgress*/,
                             nsIRequest* /*aRequest*/, nsresult /*aStatus*/,
                             const char16_t* /*aMessage*/) {
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::OnSecurityChange(nsIWebProgress* /*aWebProgress*/,
                               nsIRequest* /*aRequest*/, uint32_t /*aState*/) {
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::OnContentBlockingEvent(nsIWebProgress* /*aWebProgress*/,
                                     nsIRequest* /*aRequest*/,
                                     uint32_t /*aEvent*/) {
  return NS_OK;
}

//*****************************************************************************
// nsWebBrowser: Listener Helpers
//*****************************************************************************

void nsWebBrowser::SetDocShell(nsDocShell* aDocShell) {
  // We need to keep the docshell alive while we perform the changes, but we
  // don't need to call any methods on it.
  nsCOMPtr<nsIDocShell> kungFuDeathGrip(mDocShell);
  (void)kungFuDeathGrip;

  if (aDocShell) {
    MOZ_ASSERT(!mDocShell, "Should not overwrite an existing value!");

    mDocShell = aDocShell;

    // By default, do not allow DNS prefetch, so we don't break our frozen
    // API.  Embeddors who decide to enable it should do so manually.
    mDocShell->SetAllowDNSPrefetch(false);
  } else {
    RemoveFromWatcher();  // evil twin of Add in Create()
    if (mDocShell) {
      mDocShell->Destroy();
    }
    if (!mWillChangeProcess && mDocShell) {
      mDocShell->GetBrowsingContext()->Detach(/* aFromIPC */ true);
    }

    mDocShell = nullptr;
  }
}

void nsWebBrowser::FocusActivate(uint64_t aActionId) {
  if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
    if (nsCOMPtr<nsPIDOMWindowOuter> window = GetWindow()) {
      fm->WindowRaised(window, aActionId);
    }
  }
}

void nsWebBrowser::FocusDeactivate(uint64_t aActionId) {
  if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
    if (nsCOMPtr<nsPIDOMWindowOuter> window = GetWindow()) {
      fm->WindowLowered(window, aActionId);
    }
  }
}

void nsWebBrowser::SetWillChangeProcess() {
  mWillChangeProcess = true;
  if (mDocShell) {
    nsDocShell::Cast(mDocShell)->SetWillChangeProcess();
  }
}

void nsWebBrowser::SetWebBrowserChrome(nsIWebBrowserChrome* aWebBrowserChrome) {
  if (!aWebBrowserChrome) {
    mWebBrowserChrome = nullptr;
    mOwnerWin = nullptr;
    mOwnerRequestor = nullptr;
    mWebBrowserChromeWeak = nullptr;
  } else {
    nsCOMPtr<nsISupportsWeakReference> supportsweak =
        do_QueryInterface(aWebBrowserChrome);
    if (supportsweak) {
      supportsweak->GetWeakReference(getter_AddRefs(mWebBrowserChromeWeak));
    } else {
      nsCOMPtr<nsIBaseWindow> ownerWin(do_QueryInterface(aWebBrowserChrome));
      nsCOMPtr<nsIInterfaceRequestor> requestor(
          do_QueryInterface(aWebBrowserChrome));

      // it's ok for ownerWin or requestor to be null.
      mWebBrowserChrome = aWebBrowserChrome;
      mOwnerWin = ownerWin;
      mOwnerRequestor = requestor;
    }
  }
}

// Hook up things to the chrome like context menus and tooltips, if the chrome
// has implemented the right interfaces.
nsresult nsWebBrowser::AddChromeListeners() {
  nsresult rv = NS_OK;

  nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
  if (!webBrowserChrome) {
    return NS_ERROR_FAILURE;
  }

  // install tooltips
  if (!mChromeTooltipListener) {
    nsCOMPtr<nsITooltipListener> tooltipListener(
        do_QueryInterface(webBrowserChrome));
    if (tooltipListener) {
      mChromeTooltipListener =
          MakeRefPtr<ChromeTooltipListener>(this, webBrowserChrome);
      rv = mChromeTooltipListener->AddChromeListeners();
    }
  }

  nsCOMPtr<EventTarget> target;
  GetDOMEventTarget(this, getter_AddRefs(target));

  // register dragover and drop event listeners with the listener manager
  MOZ_ASSERT(target, "how does this happen? (see bug 1659758)");
  if (target) {
    if (EventListenerManager* elmP = target->GetOrCreateListenerManager()) {
      elmP->AddEventListenerByType(this, u"dragover"_ns,
                                   TrustedEventsAtSystemGroupBubble());
      elmP->AddEventListenerByType(this, u"drop"_ns,
                                   TrustedEventsAtSystemGroupBubble());
    }
  }

  return rv;
}

nsresult nsWebBrowser::RemoveChromeListeners() {
  if (mChromeTooltipListener) {
    mChromeTooltipListener->RemoveChromeListeners();
    mChromeTooltipListener = nullptr;
  }

  nsCOMPtr<EventTarget> piTarget;
  GetDOMEventTarget(this, getter_AddRefs(piTarget));
  if (!piTarget) {
    return NS_OK;
  }

  EventListenerManager* elmP = piTarget->GetOrCreateListenerManager();
  if (elmP) {
    elmP->RemoveEventListenerByType(this, u"dragover"_ns,
                                    TrustedEventsAtSystemGroupBubble());
    elmP->RemoveEventListenerByType(this, u"drop"_ns,
                                    TrustedEventsAtSystemGroupBubble());
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::HandleEvent(Event* aEvent) {
  DragEvent* dragEvent = aEvent ? aEvent->AsDragEvent() : nullptr;
  if (NS_WARN_IF(!dragEvent)) {
    return NS_ERROR_INVALID_ARG;
  }

  if (dragEvent->DefaultPrevented()) {
    return NS_OK;
  }

  nsCOMPtr<nsIDroppedLinkHandler> handler =
      do_GetService("@mozilla.org/content/dropped-link-handler;1");
  if (!handler) {
    return NS_OK;
  }

  nsAutoString eventType;
  aEvent->GetType(eventType);
  if (eventType.EqualsLiteral("dragover")) {
    bool canDropLink = false;
    handler->CanDropLink(dragEvent, false, &canDropLink);
    if (canDropLink) {
      aEvent->PreventDefault();
    }
  } else if (eventType.EqualsLiteral("drop")) {
    nsCOMPtr<nsIWebNavigation> webnav = this;

    // The page might have cancelled the dragover event itself, so check to
    // make sure that the link can be dropped first.
    bool canDropLink = false;
    handler->CanDropLink(dragEvent, false, &canDropLink);
    if (!canDropLink) {
      return NS_OK;
    }

    nsTArray<RefPtr<nsIDroppedLinkItem>> links;
    if (webnav && NS_SUCCEEDED(handler->DropLinks(dragEvent, true, links))) {
      if (links.Length() >= 1) {
        nsCOMPtr<nsIPrincipal> triggeringPrincipal;
        handler->GetTriggeringPrincipal(dragEvent,
                                        getter_AddRefs(triggeringPrincipal));
        if (triggeringPrincipal) {
          nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome =
              GetWebBrowserChrome();
          if (webBrowserChrome) {
            nsCOMPtr<nsIBrowserChild> browserChild =
                do_QueryInterface(webBrowserChrome);
            if (browserChild) {
              nsresult rv = browserChild->RemoteDropLinks(links);
              return rv;
            }
          }
          nsAutoString url;
          if (NS_SUCCEEDED(links[0]->GetUrl(url))) {
            if (!url.IsEmpty()) {
#ifndef ANDROID
              MOZ_ASSERT(triggeringPrincipal,
                         "nsWebBrowser::HandleEvent: Need a valid "
                         "triggeringPrincipal");
#endif
              LoadURIOptions loadURIOptions;
              loadURIOptions.mTriggeringPrincipal = triggeringPrincipal;
              nsCOMPtr<nsIPolicyContainer> policyContainer;
              handler->GetPolicyContainer(dragEvent,
                                          getter_AddRefs(policyContainer));
              loadURIOptions.mPolicyContainer = policyContainer;
              webnav->FixupAndLoadURIString(url, loadURIOptions);
            }
          }
        }
      }
    } else {
      aEvent->StopPropagation();
      aEvent->PreventDefault();
    }
  }

  return NS_OK;
}

already_AddRefed<nsIWebBrowserChrome> nsWebBrowser::GetWebBrowserChrome() {
  nsCOMPtr<nsIWebBrowserChrome> chrome;
  if (mWebBrowserChromeWeak) {
    chrome = do_QueryReferent(mWebBrowserChromeWeak);
  } else if (mWebBrowserChrome) {
    chrome = mWebBrowserChrome;
  }
  return chrome.forget();
}

already_AddRefed<nsIBaseWindow> nsWebBrowser::GetOwnerWin() {
  nsCOMPtr<nsIBaseWindow> win;
  if (mWebBrowserChromeWeak) {
    win = do_QueryReferent(mWebBrowserChromeWeak);
  } else if (mOwnerWin) {
    win = mOwnerWin;
  }
  return win.forget();
}

already_AddRefed<nsIInterfaceRequestor> nsWebBrowser::GetOwnerRequestor() {
  nsCOMPtr<nsIInterfaceRequestor> req;
  if (mWebBrowserChromeWeak) {
    req = do_QueryReferent(mWebBrowserChromeWeak);
  } else if (mOwnerRequestor) {
    req = mOwnerRequestor;
  }
  return req.forget();
}

NS_IMPL_ISUPPORTS(ChromeTooltipListener, nsIDOMEventListener)

ChromeTooltipListener::ChromeTooltipListener(nsWebBrowser* aInBrowser,
                                             nsIWebBrowserChrome* aInChrome)
    : mWebBrowser(aInBrowser),
      mWebBrowserChrome(aInChrome),
      mTooltipListenerInstalled(false),
      mShowingTooltip(false),
      mTooltipShownOnce(false) {}

ChromeTooltipListener::~ChromeTooltipListener() {
  if (mTooltipTimer) {
    mTooltipTimer->Cancel();
  }
}

nsITooltipTextProvider* ChromeTooltipListener::GetTooltipTextProvider() {
  if (!mTooltipTextProvider) {
    mTooltipTextProvider = do_GetService(NS_TOOLTIPTEXTPROVIDER_CONTRACTID);
  }

  if (!mTooltipTextProvider) {
    mTooltipTextProvider =
        do_GetService(NS_DEFAULTTOOLTIPTEXTPROVIDER_CONTRACTID);
  }

  return mTooltipTextProvider;
}

// Hook up things to the chrome like context menus and tooltips, if the chrome
// has implemented the right interfaces.
NS_IMETHODIMP
ChromeTooltipListener::AddChromeListeners() {
  if (!mEventTarget) {
    GetDOMEventTarget(mWebBrowser, getter_AddRefs(mEventTarget));
  }

  // Register the appropriate events for tooltips, but only if
  // the embedding chrome cares.
  nsresult rv = NS_OK;
  nsCOMPtr<nsITooltipListener> tooltipListener(
      do_QueryInterface(mWebBrowserChrome));
  if (tooltipListener && !mTooltipListenerInstalled) {
    rv = AddTooltipListener();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  return rv;
}

// Subscribe to the events that will allow us to track tooltips. We need "mouse"
// for mouseExit, "mouse motion" for mouseMove, and "key" for keyDown. As we
// add the listeners, keep track of how many succeed so we can clean up
// correctly in Release().
NS_IMETHODIMP
ChromeTooltipListener::AddTooltipListener() {
  if (mEventTarget) {
    MOZ_TRY(mEventTarget->AddSystemEventListener(u"keydown"_ns, this, false,
                                                 false));
    MOZ_TRY(mEventTarget->AddSystemEventListener(u"mousedown"_ns, this, false,
                                                 false));
    MOZ_TRY(mEventTarget->AddSystemEventListener(u"mouseout"_ns, this, false,
                                                 false));
    MOZ_TRY(mEventTarget->AddSystemEventListener(u"mousemove"_ns, this, false,
                                                 false));

    mTooltipListenerInstalled = true;
  }

  return NS_OK;
}

// Unsubscribe from the various things we've hooked up to the window root.
NS_IMETHODIMP
ChromeTooltipListener::RemoveChromeListeners() {
  HideTooltip();

  if (mTooltipListenerInstalled) {
    RemoveTooltipListener();
  }

  mEventTarget = nullptr;

  // it really doesn't matter if these fail...
  return NS_OK;
}

// Unsubscribe from all the various tooltip events that we were listening to.
NS_IMETHODIMP
ChromeTooltipListener::RemoveTooltipListener() {
  if (mEventTarget) {
    mEventTarget->RemoveSystemEventListener(u"keydown"_ns, this, false);
    mEventTarget->RemoveSystemEventListener(u"mousedown"_ns, this, false);
    mEventTarget->RemoveSystemEventListener(u"mouseout"_ns, this, false);
    mEventTarget->RemoveSystemEventListener(u"mousemove"_ns, this, false);
    mTooltipListenerInstalled = false;
  }

  return NS_OK;
}

NS_IMETHODIMP
ChromeTooltipListener::HandleEvent(Event* aEvent) {
  nsAutoString eventType;
  aEvent->GetType(eventType);

  if (eventType.EqualsLiteral("mousedown")) {
    return HideTooltip();
  } else if (eventType.EqualsLiteral("keydown")) {
    WidgetKeyboardEvent* keyEvent = aEvent->WidgetEventPtr()->AsKeyboardEvent();
    if (nsXULTooltipListener::KeyEventHidesTooltip(*keyEvent)) {
      return HideTooltip();
    }
    return NS_OK;
  } else if (eventType.EqualsLiteral("mouseout")) {
    // Reset flag so that tooltip will display on the next MouseMove
    mTooltipShownOnce = false;
    return HideTooltip();
  } else if (eventType.EqualsLiteral("mousemove")) {
    return MouseMove(aEvent);
  }

  NS_ERROR("Unexpected event type");
  return NS_OK;
}

// If we're a tooltip, fire off a timer to see if a tooltip should be shown. If
// the timer fires, we cache the node in |mPossibleTooltipNode|.
nsresult ChromeTooltipListener::MouseMove(Event* aMouseEvent) {
  if (!nsXULTooltipListener::ShowTooltips()) {
    return NS_OK;
  }

  MouseEvent* mouseEvent = aMouseEvent->AsMouseEvent();
  if (!mouseEvent) {
    return NS_OK;
  }

  // stash the coordinates of the event so that we can still get back to it from
  // within the timer callback. On win32, we'll get a MouseMove event even when
  // a popup goes away -- even when the mouse doesn't change position! To get
  // around this, we make sure the mouse has really moved before proceeding.
  const CSSIntPoint newMouseClientPoint =
      RoundedToInt(mouseEvent->ClientPoint());
  if (mMouseClientPoint == newMouseClientPoint) {
    return NS_OK;
  }

  // Filter out minor mouse movements.
  if (mShowingTooltip &&
      (abs(mMouseClientPoint.x - newMouseClientPoint.x) <=
       kTooltipMouseMoveTolerance) &&
      (abs(mMouseClientPoint.y - newMouseClientPoint.y) <=
       kTooltipMouseMoveTolerance)) {
    return NS_OK;
  }

  mMouseClientPoint = newMouseClientPoint;
  mMouseScreenPoint = mouseEvent->ScreenPointLayoutDevicePix();

  if (mTooltipTimer) {
    mTooltipTimer->Cancel();
    mTooltipTimer = nullptr;
  }

  if (!mShowingTooltip) {
    if (nsCOMPtr<EventTarget> eventTarget = aMouseEvent->GetOriginalTarget()) {
      mPossibleTooltipNode = nsINode::FromEventTarget(eventTarget);
    }

    if (mPossibleTooltipNode) {
      nsresult rv = NS_NewTimerWithFuncCallback(
          getter_AddRefs(mTooltipTimer), sTooltipCallback, this,
          StaticPrefs::ui_tooltip_delay_ms(), nsITimer::TYPE_ONE_SHOT,
          "ChromeTooltipListener::MouseMove"_ns,
          GetMainThreadSerialEventTarget());
      if (NS_FAILED(rv)) {
        mPossibleTooltipNode = nullptr;
        NS_WARNING("Could not create a timer for tooltip tracking");
      }
    }
  } else {
    mTooltipShownOnce = true;
    return HideTooltip();
  }

  return NS_OK;
}

// Tell the registered chrome that they should show the tooltip.
NS_IMETHODIMP
ChromeTooltipListener::ShowTooltip(int32_t aInXCoords, int32_t aInYCoords,
                                   const nsAString& aInTipText,
                                   const nsAString& aTipDir) {
  nsresult rv = NS_OK;

  // do the work to call the client
  nsCOMPtr<nsITooltipListener> tooltipListener(
      do_QueryInterface(mWebBrowserChrome));
  if (tooltipListener) {
    rv = tooltipListener->OnShowTooltip(aInXCoords, aInYCoords, aInTipText,
                                        aTipDir);
    if (NS_SUCCEEDED(rv)) {
      mShowingTooltip = true;
    }
  }

  return rv;
}

// Tell the registered chrome that they should rollup the tooltip
// NOTE: This routine is safe to call even if the popup is already closed.
NS_IMETHODIMP
ChromeTooltipListener::HideTooltip() {
  nsresult rv = NS_OK;

  // shut down the relevant timers
  if (mTooltipTimer) {
    mTooltipTimer->Cancel();
    mTooltipTimer = nullptr;
    // release tooltip target
    mPossibleTooltipNode = nullptr;
    mLastDocshell = nullptr;
  }

  // if we're showing the tip, tell the chrome to hide it
  if (mShowingTooltip) {
    nsCOMPtr<nsITooltipListener> tooltipListener(
        do_QueryInterface(mWebBrowserChrome));
    if (tooltipListener) {
      rv = tooltipListener->OnHideTooltip();
      if (NS_SUCCEEDED(rv)) {
        mShowingTooltip = false;
      }
    }
  }

  return rv;
}

bool ChromeTooltipListener::WebProgressShowedTooltip(
    nsIWebProgress* aWebProgress) {
  nsCOMPtr<nsIDocShell> docshell = do_QueryInterface(aWebProgress);
  nsCOMPtr<nsIDocShell> lastUsed = do_QueryReferent(mLastDocshell);
  while (lastUsed) {
    if (lastUsed == docshell) {
      return true;
    }
    // We can't use the docshell hierarchy here, because when the parent
    // docshell is navigated, the child docshell is disconnected (ie its
    // references to the parent are nulled out) despite it still being
    // alive here. So we use the document hierarchy instead:
    Document* document = lastUsed->GetDocument();
    if (document) {
      document = document->GetInProcessParentDocument();
    }
    if (!document) {
      break;
    }
    lastUsed = document->GetDocShell();
  }
  return false;
}

// A timer callback, fired when the mouse has hovered inside of a frame for the
// appropriate amount of time. Getting to this point means that we should show
// the tooltip, but only after we determine there is an appropriate TITLE
// element.
//
// This relies on certain things being cached into the |aChromeTooltipListener|
// object passed to us by the timer:
//   -- the x/y coordinates of the mouse      (mMouseClientY, mMouseClientX)
//   -- the dom node the user hovered over    (mPossibleTooltipNode)
void ChromeTooltipListener::sTooltipCallback(nsITimer* /*aTimer*/,
                                             void* aChromeTooltipListener) {
  RefPtr<ChromeTooltipListener> self =
      static_cast<ChromeTooltipListener*>(aChromeTooltipListener);
  if (!self || !self->mPossibleTooltipNode) {
    return;
  }
  // release tooltip target once done, no matter what we do here.
  auto cleanup = MakeScopeExit([&] { self->mPossibleTooltipNode = nullptr; });
  if (!self->mPossibleTooltipNode->IsInComposedDoc()) {
    return;
  }
  // Check that the document or its ancestors haven't been replaced.
  {
    Document* doc = self->mPossibleTooltipNode->OwnerDoc();
    while (doc) {
      if (!doc->IsCurrentActiveDocument()) {
        return;
      }
      doc = doc->GetInProcessParentDocument();
    }
  }

  RefPtr<nsDocShell> docShell = self->mWebBrowser->GetDocShell();
  if (!docShell || !docShell->GetBrowsingContext()->IsActive()) {
    return;
  }

  // if there is text associated with the node, show the tip and fire
  // off a timer to auto-hide it.
  nsITooltipTextProvider* tooltipProvider = self->GetTooltipTextProvider();
  if (!tooltipProvider) {
    return;
  }
  nsString tooltipText;
  nsString directionText;
  bool textFound = false;
  tooltipProvider->GetNodeText(self->mPossibleTooltipNode,
                               getter_Copies(tooltipText),
                               getter_Copies(directionText), &textFound);

  if (self->mPossibleTooltipNode && textFound &&
      (!self->mTooltipShownOnce ||
       tooltipText != self->mLastShownTooltipText)) {
    // ShowTooltip expects screen-relative position.
    self->ShowTooltip(self->mMouseScreenPoint.x, self->mMouseScreenPoint.y,
                      tooltipText, directionText);
    self->mLastShownTooltipText = std::move(tooltipText);
    self->mLastDocshell = do_GetWeakReference(
        self->mPossibleTooltipNode->OwnerDoc()->GetDocShell());
  }
}
