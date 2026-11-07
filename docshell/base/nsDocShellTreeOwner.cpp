/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Local Includes
#include "nsDocShellTreeOwner.h"
#include "nsWebBrowser.h"

// Helper Classes
#include "nsContentUtils.h"
#include "nsSize.h"
#include "mozilla/ReflowInput.h"
#include "mozilla/ScopeExit.h"
#include "nsComponentManagerUtils.h"
#include "nsString.h"
#include "nsAtom.h"
#include "nsReadableUtils.h"
#include "nsUnicharUtils.h"
#include "mozilla/StaticPrefs_ui.h"

// Interfaces needed to be included
#include "nsPresContext.h"
#include "nsITooltipListener.h"
#include "nsINode.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Link.h"
#include "mozilla/dom/MouseEvent.h"
#include "mozilla/dom/SVGTitleElement.h"
#include "nsIFormControl.h"
#include "nsIWebNavigation.h"
#include "nsPIDOMWindow.h"
#include "nsPIWindowRoot.h"
#include "nsIWindowWatcher.h"
#include "nsPIWindowWatcher.h"
#include "nsIPrompt.h"
#include "nsIRemoteTab.h"
#include "nsIBrowserChild.h"
#include "nsRect.h"
#include "nsIContent.h"
#include "nsServiceManagerUtils.h"
#include "nsXULTooltipListener.h"
#include "nsIConstraintValidation.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/Try.h"
#include "mozilla/dom/DragEvent.h"
#include "mozilla/dom/Event.h"     // for Event
#include "mozilla/dom/File.h"      // for input type=file
#include "mozilla/dom/FileList.h"  // for input type=file
#include "mozilla/dom/LoadURIOptionsBinding.h"
#include "mozilla/PresShell.h"
#include "mozilla/TextEvents.h"

using namespace mozilla;
using namespace mozilla::dom;

// A helper routine that navigates the tricky path from a |nsWebBrowser| to
// a |EventTarget| via the window root and chrome event handler.
static nsresult GetDOMEventTarget(nsWebBrowser* aInBrowser,
                                  EventTarget** aTarget) {
  if (!aInBrowser) {
    return NS_ERROR_INVALID_POINTER;
  }

  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  aInBrowser->GetContentDOMWindow(getter_AddRefs(domWindow));
  if (!domWindow) {
    return NS_ERROR_FAILURE;
  }

  auto* outerWindow = nsPIDOMWindowOuter::From(domWindow);
  nsPIDOMWindowOuter* rootWindow = outerWindow->GetPrivateRoot();
  NS_ENSURE_TRUE(rootWindow, NS_ERROR_FAILURE);
  nsCOMPtr<EventTarget> target = rootWindow->GetChromeEventHandler();
  NS_ENSURE_TRUE(target, NS_ERROR_FAILURE);
  target.forget(aTarget);

  return NS_OK;
}

nsDocShellTreeOwner::nsDocShellTreeOwner()
    : mWebBrowser(nullptr),
      mPrimaryContentShell(nullptr),
      mWebBrowserChrome(nullptr),
      mOwnerWin(nullptr),
      mOwnerRequestor(nullptr) {}

nsDocShellTreeOwner::~nsDocShellTreeOwner() { RemoveChromeListeners(); }

NS_IMPL_ADDREF(nsDocShellTreeOwner)
NS_IMPL_RELEASE(nsDocShellTreeOwner)

NS_INTERFACE_MAP_BEGIN(nsDocShellTreeOwner)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIDocShellTreeOwner)
  NS_INTERFACE_MAP_ENTRY(nsIDocShellTreeOwner)
  NS_INTERFACE_MAP_ENTRY(nsIBaseWindow)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIWebProgressListener)
  NS_INTERFACE_MAP_ENTRY(nsIDOMEventListener)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

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

//*****************************************************************************
// nsDocShellTreeOwner::nsIInterfaceRequestor
//*****************************************************************************

NS_IMETHODIMP
nsDocShellTreeOwner::GetInterface(const nsIID& aIID, void** aSink) {
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

  nsCOMPtr<nsIInterfaceRequestor> req = GetOwnerRequestor();
  if (req) {
    return req->GetInterface(aIID, aSink);
  }

  return NS_NOINTERFACE;
}

//*****************************************************************************
// nsDocShellTreeOwner::nsIDocShellTreeOwner
//*****************************************************************************

void nsDocShellTreeOwner::EnsurePrompter() {
  if (mPrompter) {
    return;
  }

  nsCOMPtr<nsIWindowWatcher> wwatch(do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  if (wwatch && mWebBrowser) {
    nsCOMPtr<mozIDOMWindowProxy> domWindow;
    mWebBrowser->GetContentDOMWindow(getter_AddRefs(domWindow));
    if (domWindow) {
      wwatch->GetNewPrompter(domWindow, getter_AddRefs(mPrompter));
    }
  }
}

void nsDocShellTreeOwner::EnsureAuthPrompter() {
  if (mAuthPrompter) {
    return;
  }

  nsCOMPtr<nsIWindowWatcher> wwatch(do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  if (wwatch && mWebBrowser) {
    nsCOMPtr<mozIDOMWindowProxy> domWindow;
    mWebBrowser->GetContentDOMWindow(getter_AddRefs(domWindow));
    if (domWindow) {
      wwatch->GetNewAuthPrompter(domWindow, getter_AddRefs(mAuthPrompter));
    }
  }
}

void nsDocShellTreeOwner::AddToWatcher() {
  if (mWebBrowser) {
    nsCOMPtr<mozIDOMWindowProxy> domWindow;
    mWebBrowser->GetContentDOMWindow(getter_AddRefs(domWindow));
    if (domWindow) {
      nsCOMPtr<nsPIWindowWatcher> wwatch(
          do_GetService(NS_WINDOWWATCHER_CONTRACTID));
      if (wwatch) {
        nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
        if (webBrowserChrome) {
          wwatch->AddWindow(domWindow, webBrowserChrome);
        }
      }
    }
  }
}

void nsDocShellTreeOwner::RemoveFromWatcher() {
  if (mWebBrowser) {
    nsCOMPtr<mozIDOMWindowProxy> domWindow;
    mWebBrowser->GetContentDOMWindow(getter_AddRefs(domWindow));
    if (domWindow) {
      nsCOMPtr<nsPIWindowWatcher> wwatch(
          do_GetService(NS_WINDOWWATCHER_CONTRACTID));
      if (wwatch) {
        wwatch->RemoveWindow(domWindow);
      }
    }
  }
}

void nsDocShellTreeOwner::EnsureContentTreeOwner() {
  if (mContentTreeOwner) {
    return;
  }

  mContentTreeOwner = new nsDocShellTreeOwner();
  nsCOMPtr<nsIWebBrowserChrome> browserChrome = GetWebBrowserChrome();
  if (browserChrome) {
    mContentTreeOwner->SetWebBrowserChrome(browserChrome);
  }

  if (mWebBrowser) {
    mContentTreeOwner->WebBrowser(mWebBrowser);
  }
}

NS_IMETHODIMP
nsDocShellTreeOwner::ContentShellAdded(nsIDocShellTreeItem* aContentShell,
                                       bool aPrimary) {
  EnsureContentTreeOwner();
  aContentShell->SetTreeOwner(mContentTreeOwner);

  if (aPrimary) {
    mPrimaryContentShell = aContentShell;
    mPrimaryRemoteTab = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::ContentShellRemoved(nsIDocShellTreeItem* aContentShell) {
  if (mPrimaryContentShell == aContentShell) {
    mPrimaryContentShell = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetPrimaryContentShell(nsIDocShellTreeItem** aShell) {
  NS_ENSURE_ARG_POINTER(aShell);

  nsCOMPtr<nsIDocShellTreeItem> shell;
  if (!mPrimaryRemoteTab) {
    shell =
        mPrimaryContentShell ? mPrimaryContentShell : mWebBrowser->mDocShell;
  }
  shell.forget(aShell);

  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::RemoteTabAdded(nsIRemoteTab* aTab, bool aPrimary) {
  if (aPrimary) {
    mPrimaryRemoteTab = aTab;
    mPrimaryContentShell = nullptr;
  } else if (mPrimaryRemoteTab == aTab) {
    mPrimaryRemoteTab = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::RemoteTabRemoved(nsIRemoteTab* aTab) {
  if (aTab == mPrimaryRemoteTab) {
    mPrimaryRemoteTab = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetPrimaryRemoteTab(nsIRemoteTab** aTab) {
  nsCOMPtr<nsIRemoteTab> tab = mPrimaryRemoteTab;
  tab.forget(aTab);
  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetPrimaryContentBrowsingContext(
    mozilla::dom::BrowsingContext** aBc) {
  if (mPrimaryRemoteTab) {
    return mPrimaryRemoteTab->GetBrowsingContext(aBc);
  }
  if (mPrimaryContentShell) {
    return mPrimaryContentShell->GetBrowsingContextXPCOM(aBc);
  }
  if (mWebBrowser->mDocShell) {
    return mWebBrowser->mDocShell->GetBrowsingContextXPCOM(aBc);
  }
  *aBc = nullptr;
  return NS_OK;
}

//*****************************************************************************
// nsDocShellTreeOwner::nsIBaseWindow
//*****************************************************************************

NS_IMETHODIMP
nsDocShellTreeOwner::Destroy() {
  nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
  if (webBrowserChrome) {
    // XXX: this is weird, but we used to call a method here
    // (webBrowserChrome->DestroyBrowserWindow()) whose implementations all
    // failed like this, so...
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  return NS_ERROR_NULL_POINTER;
}

double nsDocShellTreeOwner::GetWidgetCSSToDeviceScale() {
  return mWebBrowser ? mWebBrowser->GetWidgetCSSToDeviceScale() : 1.0;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetDevicePixelsPerDesktopPixel(double* aScale) {
  if (mWebBrowser) {
    return mWebBrowser->GetDevicePixelsPerDesktopPixel(aScale);
  }

  *aScale = 1.0;
  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetPositionDesktopPix(int32_t aX, int32_t aY) {
  if (mWebBrowser) {
    nsresult rv = mWebBrowser->SetPositionDesktopPix(aX, aY);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  double scale = 1.0;
  GetDevicePixelsPerDesktopPixel(&scale);
  return SetPosition(NSToIntRound(aX * scale), NSToIntRound(aY * scale));
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetPosition(int32_t aX, int32_t aY) {
  return SetDimensions(
      {DimensionKind::Outer, Some(aX), Some(aY), Nothing(), Nothing()});
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetPosition(int32_t* aX, int32_t* aY) {
  return GetDimensions(DimensionKind::Outer, aX, aY, nullptr, nullptr);
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetSize(int32_t aCX, int32_t aCY, bool /*aRepaint*/) {
  return SetDimensions(
      {DimensionKind::Outer, Nothing(), Nothing(), Some(aCX), Some(aCY)});
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetSize(int32_t* aCX, int32_t* aCY) {
  return GetDimensions(DimensionKind::Outer, nullptr, nullptr, aCX, aCY);
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetPositionAndSize(int32_t aX, int32_t aY, int32_t aCX,
                                        int32_t aCY, uint32_t /*aFlags*/) {
  return SetDimensions(
      {DimensionKind::Outer, Some(aX), Some(aY), Some(aCX), Some(aCY)});
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetPositionAndSize(int32_t* aX, int32_t* aY, int32_t* aCX,
                                        int32_t* aCY) {
  return GetDimensions(DimensionKind::Outer, aX, aY, aCX, aCY);
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetDimensions(DimensionRequest&& aRequest) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->SetDimensions(std::move(aRequest));
  }

  nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
  NS_ENSURE_STATE(webBrowserChrome);
  return webBrowserChrome->SetDimensions(std::move(aRequest));
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetDimensions(DimensionKind aDimensionKind, int32_t* aX,
                                   int32_t* aY, int32_t* aCX, int32_t* aCY) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->GetDimensions(aDimensionKind, aX, aY, aCX, aCY);
  }

  nsCOMPtr<nsIWebBrowserChrome> webBrowserChrome = GetWebBrowserChrome();
  NS_ENSURE_STATE(webBrowserChrome);
  return webBrowserChrome->GetDimensions(aDimensionKind, aX, aY, aCX, aCY);
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetParentWidget(nsIWidget** /*aParentWidget*/) {
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetParentWidget(nsIWidget* /*aParentWidget*/) {
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetNativeHandle(nsAString& /*aNativeHandle*/) {
  // the nativeHandle should be accessed from nsIAppWindow
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetVisibility(bool* aVisibility) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->GetVisibility(aVisibility);
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetVisibility(bool aVisibility) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->SetVisibility(aVisibility);
  }
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetEnabled(bool* aEnabled) {
  NS_ENSURE_ARG_POINTER(aEnabled);
  *aEnabled = true;
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetEnabled(bool /*aEnabled*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetMainWidget(nsIWidget** /*aMainWidget*/) {
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsDocShellTreeOwner::GetTitle(nsAString& aTitle) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->GetTitle(aTitle);
  }
  return NS_ERROR_NULL_POINTER;
}

NS_IMETHODIMP
nsDocShellTreeOwner::SetTitle(const nsAString& aTitle) {
  nsCOMPtr<nsIBaseWindow> ownerWin = GetOwnerWin();
  if (ownerWin) {
    return ownerWin->SetTitle(aTitle);
  }
  return NS_ERROR_NULL_POINTER;
}

//*****************************************************************************
// nsDocShellTreeOwner::nsIWebProgressListener
//*****************************************************************************

NS_IMETHODIMP
nsDocShellTreeOwner::OnProgressChange(nsIWebProgress* /*aProgress*/,
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
nsDocShellTreeOwner::OnStateChange(nsIWebProgress* /*aProgress*/,
                                   nsIRequest* /*aRequest*/,
                                   uint32_t /*aProgressStateFlags*/,
                                   nsresult /*aStatus*/) {
  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::OnLocationChange(nsIWebProgress* aWebProgress,
                                      nsIRequest* /*aRequest*/,
                                      nsIURI* /*aURI*/, uint32_t aFlags) {
  if (mChromeTooltipListener && aWebProgress &&
      !(aFlags & nsIWebProgressListener::LOCATION_CHANGE_SAME_DOCUMENT) &&
      mChromeTooltipListener->WebProgressShowedTooltip(aWebProgress)) {
    mChromeTooltipListener->HideTooltip();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::OnStatusChange(nsIWebProgress* /*aWebProgress*/,
                                    nsIRequest* /*aRequest*/,
                                    nsresult /*aStatus*/,
                                    const char16_t* /*aMessage*/) {
  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::OnSecurityChange(nsIWebProgress* /*aWebProgress*/,
                                      nsIRequest* /*aRequest*/,
                                      uint32_t /*aState*/) {
  return NS_OK;
}

NS_IMETHODIMP
nsDocShellTreeOwner::OnContentBlockingEvent(nsIWebProgress* /*aWebProgress*/,
                                            nsIRequest* /*aRequest*/,
                                            uint32_t /*aEvent*/) {
  return NS_OK;
}

//*****************************************************************************
// nsDocShellTreeOwner: Accessors
//*****************************************************************************

void nsDocShellTreeOwner::WebBrowser(nsWebBrowser* aWebBrowser) {
  if (!aWebBrowser) {
    RemoveChromeListeners();
  }
  if (aWebBrowser != mWebBrowser) {
    mPrompter = nullptr;
    mAuthPrompter = nullptr;
  }

  mWebBrowser = aWebBrowser;

  if (mContentTreeOwner) {
    mContentTreeOwner->WebBrowser(aWebBrowser);
    if (!aWebBrowser) {
      mContentTreeOwner = nullptr;
    }
  }
}

nsWebBrowser* nsDocShellTreeOwner::WebBrowser() { return mWebBrowser; }

NS_IMETHODIMP
nsDocShellTreeOwner::SetWebBrowserChrome(
    nsIWebBrowserChrome* aWebBrowserChrome) {
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

  if (mContentTreeOwner) {
    mContentTreeOwner->SetWebBrowserChrome(aWebBrowserChrome);
  }

  return NS_OK;
}

// Hook up things to the chrome like context menus and tooltips, if the chrome
// has implemented the right interfaces.
NS_IMETHODIMP
nsDocShellTreeOwner::AddChromeListeners() {
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
          MakeRefPtr<ChromeTooltipListener>(mWebBrowser, webBrowserChrome);
      rv = mChromeTooltipListener->AddChromeListeners();
    }
  }

  nsCOMPtr<EventTarget> target;
  GetDOMEventTarget(mWebBrowser, getter_AddRefs(target));

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

NS_IMETHODIMP
nsDocShellTreeOwner::RemoveChromeListeners() {
  if (mChromeTooltipListener) {
    mChromeTooltipListener->RemoveChromeListeners();
    mChromeTooltipListener = nullptr;
  }

  nsCOMPtr<EventTarget> piTarget;
  GetDOMEventTarget(mWebBrowser, getter_AddRefs(piTarget));
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
nsDocShellTreeOwner::HandleEvent(Event* aEvent) {
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
    nsCOMPtr<nsIWebNavigation> webnav =
        static_cast<nsIWebNavigation*>(mWebBrowser);

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
                         "nsDocShellTreeOwner::HandleEvent: Need a valid "
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

already_AddRefed<nsIWebBrowserChrome>
nsDocShellTreeOwner::GetWebBrowserChrome() {
  nsCOMPtr<nsIWebBrowserChrome> chrome;
  if (mWebBrowserChromeWeak) {
    chrome = do_QueryReferent(mWebBrowserChromeWeak);
  } else if (mWebBrowserChrome) {
    chrome = mWebBrowserChrome;
  }
  return chrome.forget();
}

already_AddRefed<nsIBaseWindow> nsDocShellTreeOwner::GetOwnerWin() {
  nsCOMPtr<nsIBaseWindow> win;
  if (mWebBrowserChromeWeak) {
    win = do_QueryReferent(mWebBrowserChromeWeak);
  } else if (mOwnerWin) {
    win = mOwnerWin;
  }
  return win.forget();
}

already_AddRefed<nsIInterfaceRequestor>
nsDocShellTreeOwner::GetOwnerRequestor() {
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

  nsCOMPtr<nsIDocShell> docShell =
      do_GetInterface(static_cast<nsIWebBrowser*>(self->mWebBrowser));
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
