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

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;

nsWebBrowser::nsWebBrowser(int aItemType)
    : mContentType(aItemType),
      mWillChangeProcess(false),
      mParentWidget(nullptr) {}

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

  // nsWebBrowser::SetContainer also calls nsWebBrowser::EnsureDocShellTreeOwner
  MOZ_TRY(browser->SetContainerWindow(aContainerWindow));
  MOZ_TRY(browser->SetParentWidget(aParentWidget));

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

  // HACK ALERT - this registration registers the nsDocShellTreeOwner as a
  // nsIWebBrowserListener so it can setup its MouseListener in one of the
  // progress callbacks. If we can register the MouseListener another way, this
  // registration can go away, and nsDocShellTreeOwner can stop implementing
  // nsIWebProgressListener.
  RefPtr<nsDocShellTreeOwner> docShellTreeOwner = browser->mDocShellTreeOwner;
  (void)docShell->AddProgressListener(docShellTreeOwner,
                                      nsIWebProgress::NOTIFY_ALL);

  docShell->SetTreeOwner(docShellTreeOwner);

  // If the webbrowser is a content docshell item then we won't hear any
  // events from subframes. To solve that we install our own chrome event
  // handler that always gets called (even for subframes) for any bubbling
  // event.
  MOZ_TRY(docShell->InitWindow(docShellParentWidget, 0, 0, 0, 0,
                               aOpenWindowInfo, aInitialWindowChild));

  docShellTreeOwner->AddToWatcher();  // evil twin of Remove in SetDocShell(0)
  docShellTreeOwner->AddChromeListeners();

  browser.forget(aWebBrowser);
  return NS_OK;
}

void nsWebBrowser::InternalDestroy() {
  SetDocShell(nullptr);

  if (mDocShellTreeOwner) {
    mDocShellTreeOwner->WebBrowser(nullptr);
    mDocShellTreeOwner = nullptr;
  }
}

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsWebBrowser)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsWebBrowser)

NS_IMPL_CYCLE_COLLECTION_WEAK(nsWebBrowser, mDocShell)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsWebBrowser)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWebBrowser)
  NS_INTERFACE_MAP_ENTRY(nsIWebBrowser)
  NS_INTERFACE_MAP_ENTRY(nsIWebNavigation)
  NS_INTERFACE_MAP_ENTRY(nsIBaseWindow)
  NS_INTERFACE_MAP_ENTRY(nsIDocShellTreeItem)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
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
    return mDocShell->GetInterface(aIID, aSink);
  }

  return NS_NOINTERFACE;
}

//*****************************************************************************
// nsWebBrowser::nsIWebBrowser
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::GetContainerWindow(nsIWebBrowserChrome** aTopWindow) {
  NS_ENSURE_ARG_POINTER(aTopWindow);

  nsCOMPtr<nsIWebBrowserChrome> top;
  if (mDocShellTreeOwner) {
    top = mDocShellTreeOwner->GetWebBrowserChrome();
  }

  top.forget(aTopWindow);

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::SetContainerWindow(nsIWebBrowserChrome* aTopWindow) {
  EnsureDocShellTreeOwner();
  return mDocShellTreeOwner->SetWebBrowserChrome(aTopWindow);
}

NS_IMETHODIMP
nsWebBrowser::GetContentDOMWindow(mozIDOMWindowProxy** aResult) {
  if (!mDocShell) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsPIDOMWindowOuter> retval = mDocShell->GetWindow();
  retval.forget(aResult);
  return *aResult ? NS_OK : NS_ERROR_FAILURE;
}

void nsWebBrowser::SetOriginAttributes(const OriginAttributes& aAttrs) {
  mOriginAttributes = aAttrs;
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
  *aTreeOwner = do_AddRef(mDocShellTreeOwner).take();
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
// nsWebBrowser::nsIBaseWindow
//*****************************************************************************

NS_IMETHODIMP
nsWebBrowser::Destroy() {
  InternalDestroy();

  return NS_OK;
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
  // XXX jfkthame
  // It's not clear to me whether this will be fully correct across
  // potential multi-screen, mixed-DPI configurations for all platforms;
  // we might need to add code paths that make it possible to pass the
  // desktop-pix parameters all the way through to the native widget,
  // to avoid the risk of device-pixel coords mapping to the wrong
  // display on OS X with mixed retina/non-retina screens.
  double scale = 1.0;
  GetDevicePixelsPerDesktopPixel(&scale);
  return SetPosition(NSToIntRound(aX * scale), NSToIntRound(aY * scale));
}

NS_IMETHODIMP
nsWebBrowser::SetPosition(int32_t aX, int32_t aY) {
  int32_t cx = 0;
  int32_t cy = 0;

  GetSize(&cx, &cy);

  return SetPositionAndSize(aX, aY, cx, cy, 0);
}

NS_IMETHODIMP
nsWebBrowser::GetPosition(int32_t* aX, int32_t* aY) {
  return GetPositionAndSize(aX, aY, nullptr, nullptr);
}

NS_IMETHODIMP
nsWebBrowser::SetSize(int32_t aCX, int32_t aCY, bool aRepaint) {
  int32_t x = 0;
  int32_t y = 0;

  GetPosition(&x, &y);

  return SetPositionAndSize(x, y, aCX, aCY,
                            aRepaint ? nsIBaseWindow::eRepaint : 0);
}

NS_IMETHODIMP
nsWebBrowser::GetSize(int32_t* aCX, int32_t* aCY) {
  return GetPositionAndSize(nullptr, nullptr, aCX, aCY);
}

NS_IMETHODIMP
nsWebBrowser::SetPositionAndSize(int32_t aX, int32_t aY, int32_t aCX,
                                 int32_t aCY, uint32_t aFlags) {
  int32_t doc_x = aX;
  int32_t doc_y = aY;

  // We also need to resize our widget then.
  // Now reposition/ resize the doc
  NS_ENSURE_SUCCESS(
      mDocShell->SetPositionAndSize(doc_x, doc_y, aCX, aCY, aFlags),
      NS_ERROR_FAILURE);

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetPositionAndSize(int32_t* aX, int32_t* aY, int32_t* aCX,
                                 int32_t* aCY) {
  // Can directly return this as it is the
  // same interface, thus same returns.
  return mDocShell->GetPositionAndSize(aX, aY, aCX, aCY);
}

NS_IMETHODIMP
nsWebBrowser::SetDimensions(DimensionRequest&& aRequest) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowser::GetDimensions(DimensionKind aDimensionKind, int32_t* aX,
                            int32_t* aY, int32_t* aCX, int32_t* aCY) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowser::GetParentWidget(nsIWidget** aParentWidget) {
  NS_ENSURE_ARG_POINTER(aParentWidget);

  *aParentWidget = mParentWidget;

  NS_IF_ADDREF(*aParentWidget);

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::SetParentWidget(nsIWidget* aParentWidget) {
  NS_ENSURE_STATE(!mDocShell);
  mParentWidget = aParentWidget;
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetNativeHandle(nsAString& aNativeHandle) {
  // the nativeHandle should be accessed from nsIAppWindow
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWebBrowser::GetVisibility(bool* aVisibility) {
  NS_ENSURE_ARG_POINTER(aVisibility);

  if (mDocShell) {
    NS_ENSURE_SUCCESS(mDocShell->GetVisibility(aVisibility), NS_ERROR_FAILURE);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::SetVisibility(bool aVisibility) {
  if (mDocShell) {
    NS_ENSURE_SUCCESS(mDocShell->SetVisibility(aVisibility), NS_ERROR_FAILURE);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetEnabled(bool* aEnabled) { return NS_ERROR_FAILURE; }

NS_IMETHODIMP
nsWebBrowser::SetEnabled(bool aEnabled) { return NS_ERROR_FAILURE; }

NS_IMETHODIMP
nsWebBrowser::GetMainWidget(nsIWidget** aMainWidget) {
  NS_ENSURE_ARG_POINTER(aMainWidget);
  NS_IF_ADDREF(*aMainWidget = mParentWidget);
  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::GetTitle(nsAString& aTitle) {
  NS_ENSURE_STATE(mDocShell);

  NS_ENSURE_SUCCESS(mDocShell->GetTitle(aTitle), NS_ERROR_FAILURE);

  return NS_OK;
}

NS_IMETHODIMP
nsWebBrowser::SetTitle(const nsAString& aTitle) {
  NS_ENSURE_STATE(mDocShell);

  NS_ENSURE_SUCCESS(mDocShell->SetTitle(aTitle), NS_ERROR_FAILURE);

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
    if (mDocShellTreeOwner) {
      mDocShellTreeOwner->RemoveFromWatcher();  // evil twin of Add in Create()
    }
    if (mDocShell) {
      mDocShell->Destroy();
    }
    if (!mWillChangeProcess && mDocShell) {
      mDocShell->GetBrowsingContext()->Detach(/* aFromIPC */ true);
    }

    mDocShell = nullptr;
  }
}

void nsWebBrowser::EnsureDocShellTreeOwner() {
  if (mDocShellTreeOwner) {
    return;
  }

  mDocShellTreeOwner = new nsDocShellTreeOwner();
  mDocShellTreeOwner->WebBrowser(this);
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
