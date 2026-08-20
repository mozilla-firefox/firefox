/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsIAppShellService.h"
#include "nsNetUtil.h"
#include "nsIObserverService.h"
#include "nsIObserver.h"
#include "nsIXULRuntime.h"

#include "nsIWindowMediator.h"
#include "nsPIWindowWatcher.h"
#include "nsPIDOMWindow.h"
#include "AppWindow.h"
#include "nsOpenWindowInfo.h"

#include "mozilla/widget/InitData.h"
#include "nsWidgetsCID.h"
#include "nsIWidget.h"

#include "nsAppDirectoryServiceDefs.h"
#include "nsAppShellService.h"
#include "nsContentUtils.h"
#include "nsDirectoryServiceUtils.h"
#include "nsThreadUtils.h"
#include "nsILoadContext.h"
#include "nsIWebNavigation.h"
#include "nsIWindowlessBrowser.h"

#include "mozilla/NullPrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StartupTimeline.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/Try.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"

#include "nsEmbedCID.h"
#include "nsIWebBrowser.h"
#include "nsIDocShell.h"

#include "nsWebBrowser.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"

using namespace mozilla;
using mozilla::dom::BrowsingContext;
using mozilla::intl::LocaleService;

class nsIAppShell;

nsAppShellService::nsAppShellService()
    : mXPCOMWillShutDown(false),
      mXPCOMShuttingDown(false),
      mModalWindowCount(0),
      mScreenId(0) {
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();

  if (obs) {
    obs->AddObserver(this, "xpcom-will-shutdown", false);
    obs->AddObserver(this, "xpcom-shutdown", false);
  }
}

nsAppShellService::~nsAppShellService() = default;

/*
 * Implement the nsISupports methods...
 */
NS_IMPL_ISUPPORTS(nsAppShellService, nsIAppShellService, nsIObserver)

NS_IMETHODIMP
nsAppShellService::SetScreenId(uint32_t aScreenId) {
  mScreenId = aScreenId;
  return NS_OK;
}

NS_IMETHODIMP
nsAppShellService::CreateHiddenWindow() {
#if defined(XP_MACOSX)
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  if (mXPCOMShuttingDown) {
    return NS_ERROR_FAILURE;
  }

  if (mHiddenWindow) {
    return NS_OK;
  }

  nsCOMPtr<nsIFile> profileDir;
  NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                         getter_AddRefs(profileDir));
  if (!profileDir) {
    // This is too early on startup to create the hidden window
    return NS_ERROR_FAILURE;
  }

  nsresult rv;
  int32_t initialHeight = 100, initialWidth = 100;

  uint32_t chromeMask = 0;
  nsAutoCString prefVal;
  rv = Preferences::GetCString("browser.hiddenWindowChromeURL", prefVal);
  if (NS_FAILED(rv)) {
    return NS_OK;
  }
  const char* hiddenWindowURL = prefVal.get();

  nsCOMPtr<nsIURI> url;
  rv = NS_NewURI(getter_AddRefs(url), hiddenWindowURL);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<AppWindow> newWindow;
  rv = JustCreateTopWindow(nullptr, url, chromeMask, initialWidth,
                           initialHeight, true, getter_AddRefs(newWindow));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDocShell> docShell;
  newWindow->GetDocShell(getter_AddRefs(docShell));
  if (docShell) {
    (void)docShell->GetBrowsingContext()->SetExplicitActive(
        dom::ExplicitActiveStatus::Inactive);
  }

  mHiddenWindow.swap(newWindow);
#endif

  return NS_OK;
}

NS_IMETHODIMP
nsAppShellService::DestroyHiddenWindow() {
  if (mHiddenWindow) {
    mHiddenWindow->Destroy();

    mHiddenWindow = nullptr;
  }

  return NS_OK;
}

/*
 * Create a new top level window and display the given URL within it...
 */
NS_IMETHODIMP
nsAppShellService::CreateTopLevelWindow(nsIAppWindow* aParent, nsIURI* aUrl,
                                        uint32_t aChromeMask,
                                        int32_t aInitialWidth,
                                        int32_t aInitialHeight,
                                        nsIAppWindow** aResult) {
  nsresult rv;

  StartupTimeline::RecordOnce(StartupTimeline::CREATE_TOP_LEVEL_WINDOW);

  RefPtr<AppWindow> newWindow;
  rv = JustCreateTopWindow(aParent, aUrl, aChromeMask, aInitialWidth,
                           aInitialHeight, false, getter_AddRefs(newWindow));
  newWindow.forget(aResult);

  if (NS_SUCCEEDED(rv)) {
    // the addref resulting from this is the owning addref for this window
    RegisterTopLevelWindow(*aResult);
  }

  return rv;
}

/*
 * This class provides a stub implementation of nsIWebBrowserChrome, as needed
 * by nsAppShellService::CreateWindowlessBrowser
 */
class WebBrowserChrome2Stub final : public nsIWebBrowserChrome,
                                    public nsIInterfaceRequestor,
                                    public nsSupportsWeakReference {
 protected:
  nsCOMPtr<nsIWebBrowser> mBrowser;
  virtual ~WebBrowserChrome2Stub() = default;

 public:
  void SetBrowser(nsIWebBrowser* aBrowser) { mBrowser = aBrowser; }

  NS_DECL_ISUPPORTS
  NS_DECL_NSIWEBBROWSERCHROME
  NS_DECL_NSIINTERFACEREQUESTOR
};

NS_INTERFACE_MAP_BEGIN(WebBrowserChrome2Stub)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIWebBrowserChrome)
  NS_INTERFACE_MAP_ENTRY(nsIWebBrowserChrome)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(WebBrowserChrome2Stub)
NS_IMPL_RELEASE(WebBrowserChrome2Stub)

NS_IMETHODIMP
WebBrowserChrome2Stub::GetChromeFlags(uint32_t* aChromeFlags) {
  *aChromeFlags = 0;
  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome2Stub::ShowAsModal() {
  MOZ_ASSERT_UNREACHABLE("WebBrowserChrome2Stub::ShowAsModal is not supported");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WebBrowserChrome2Stub::IsWindowModal(bool* aResult) {
  *aResult = false;
  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome2Stub::SetLinkStatus(const nsAString& aStatusText) {
  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome2Stub::GetInterface(const nsIID& aIID, void** aSink) {
  return QueryInterface(aIID, aSink);
}

NS_IMETHODIMP
WebBrowserChrome2Stub::GetDimensions(DimensionKind aDimensionKind, int32_t* aX,
                                     int32_t* aY, int32_t* aCX, int32_t* aCY) {
  if (aX) {
    *aX = 0;
  }
  if (aY) {
    *aY = 0;
  }
  if (aCX) {
    *aCX = 0;
  }
  if (aCY) {
    *aCY = 0;
  }
  return NS_OK;
}

NS_IMETHODIMP
WebBrowserChrome2Stub::SetDimensions(DimensionRequest&& aRequest) {
  nsCOMPtr<nsIBaseWindow> window(do_QueryInterface(mBrowser));
  NS_ENSURE_STATE(window);
  // Inner and outer dimensions are equal.
  aRequest.mDimensionKind = DimensionKind::Outer;
  MOZ_TRY(aRequest.SupplementFrom(window));
  return aRequest.ApplyOuterTo(window);
}

NS_IMETHODIMP
WebBrowserChrome2Stub::Blur() { return NS_ERROR_NOT_IMPLEMENTED; }

class BrowserDestroyer final : public Runnable {
 public:
  BrowserDestroyer(nsIWebBrowser* aBrowser, nsISupports* aContainer)
      : mozilla::Runnable("BrowserDestroyer"),
        mBrowser(aBrowser),
        mContainer(aContainer) {}

  static nsresult Destroy(nsIWebBrowser* aBrowser) {
    nsCOMPtr<nsIBaseWindow> window(do_QueryInterface(aBrowser));
    return window->Destroy();
  }

  NS_IMETHOD
  Run() override {
    // Explicitly destroy the browser, in case this isn't the last reference.
    return Destroy(mBrowser);
  }

 protected:
  virtual ~BrowserDestroyer() = default;

 private:
  nsCOMPtr<nsIWebBrowser> mBrowser;
  nsCOMPtr<nsISupports> mContainer;
};

// This is the "stub" we return from CreateWindowlessBrowser - it exists
// to manage the lifetimes of the nsIWebBrowser and container window.
// In particular, it keeps a strong reference to both, to prevent them from
// being collected while this object remains alive, and ensures that they
// aren't destroyed when it's not safe to run scripts.
class WindowlessBrowser final : public nsIWindowlessBrowser,
                                public nsIInterfaceRequestor {
 public:
  WindowlessBrowser(nsIWebBrowser* aBrowser, nsISupports* aContainer)
      : mBrowser(aBrowser), mContainer(aContainer), mClosed(false) {
    mWebNavigation = do_QueryInterface(aBrowser);
    mInterfaceRequestor = do_QueryInterface(aBrowser);
  }
  NS_DECL_ISUPPORTS
  NS_DECL_NSIWINDOWLESSBROWSER
  NS_FORWARD_SAFE_NSIWEBNAVIGATION(RefPtr{mWebNavigation.get()})
  NS_FORWARD_SAFE_NSIINTERFACEREQUESTOR(mInterfaceRequestor)

 private:
  ~WindowlessBrowser() {
    if (mClosed) {
      return;
    }

    NS_WARNING("Windowless browser was not closed prior to destruction");

    // The docshell destructor needs to dispatch events, and can only run
    // when it's safe to run scripts. If this was triggered by GC, it may
    // not always be safe to run scripts, in which cases we need to delay
    // destruction until it is.
    auto runnable = MakeRefPtr<BrowserDestroyer>(mBrowser, mContainer);
    nsContentUtils::AddScriptRunner(runnable.forget());
  }

  nsCOMPtr<nsIWebBrowser> mBrowser;
  nsCOMPtr<nsIWebNavigation> mWebNavigation;
  nsCOMPtr<nsIInterfaceRequestor> mInterfaceRequestor;
  // we don't use the container but just hold a reference to it.
  nsCOMPtr<nsISupports> mContainer;

  bool mClosed;
};

NS_IMPL_ISUPPORTS(WindowlessBrowser, nsIWindowlessBrowser, nsIWebNavigation,
                  nsIInterfaceRequestor)

NS_IMETHODIMP
WindowlessBrowser::Close() {
  NS_ENSURE_TRUE(!mClosed, NS_ERROR_UNEXPECTED);
  NS_ASSERTION(nsContentUtils::IsSafeToRunScript(),
               "WindowlessBrowser::Close called when not safe to run scripts");

  mClosed = true;

  mWebNavigation = nullptr;
  mInterfaceRequestor = nullptr;
  return BrowserDestroyer::Destroy(mBrowser);
}

NS_IMETHODIMP
WindowlessBrowser::GetBrowsingContext(BrowsingContext** aBrowsingContext) {
  nsCOMPtr<nsIDocShellTreeItem> docShellTreeItem = do_QueryInterface(mBrowser);
  if (!docShellTreeItem) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  return docShellTreeItem->GetBrowsingContextXPCOM(aBrowsingContext);
}

NS_IMETHODIMP
WindowlessBrowser::GetDocShell(nsIDocShell** aDocShell) {
  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(mInterfaceRequestor);
  if (!docShell) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  docShell.forget(aDocShell);
  return NS_OK;
}

NS_IMETHODIMP
nsAppShellService::CreateWindowlessBrowser(bool aIsChrome, uint32_t aChromeMask,
                                           nsIWindowlessBrowser** aResult) {
  if (aChromeMask) {
    MOZ_DIAGNOSTIC_ASSERT(aIsChrome, "Got chrome flags for non-chrome browser");
    if (aChromeMask & ~(nsIWebBrowserChrome::CHROME_REMOTE_WINDOW |
                        nsIWebBrowserChrome::CHROME_FISSION_WINDOW |
                        nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW)) {
      NS_ERROR("Received unexpected chrome flags");
      return NS_ERROR_FAILURE;
    }
  }

  /* First, we set the container window for our instance of nsWebBrowser. Since
   * we don't actually have a window, we instead set the container window to be
   * an instance of WebBrowserChrome2Stub, which provides a stub implementation
   * of nsIWebBrowserChrome.
   */
  RefPtr<WebBrowserChrome2Stub> stub = new WebBrowserChrome2Stub();

  /* A windowless web browser doesn't have an associated OS level window. To
   * accomplish this, we initialize the window associated with our instance of
   * nsWebBrowser with an instance of PuppetWidget, which provides a stub
   * implementation of nsIWidget.
   */
  nsCOMPtr<nsIWidget> widget = nsIWidget::CreatePuppetWidget(nullptr);
  if (!widget) {
    NS_ERROR("Couldn't create instance of stub widget");
    return NS_ERROR_FAILURE;
  }

  MOZ_TRY(widget->Create(nullptr, LayoutDeviceIntRect(), widget::InitData()));

  // Create a BrowsingContext for our windowless browser.
  RefPtr<BrowsingContext> browsingContext = BrowsingContext::CreateIndependent(
      aIsChrome ? BrowsingContext::Type::Chrome
                : BrowsingContext::Type::Content,
      true);

  if (aChromeMask & nsIWebBrowserChrome::CHROME_REMOTE_WINDOW) {
    browsingContext->SetRemoteTabs(true);
  }
  if (aChromeMask & nsIWebBrowserChrome::CHROME_FISSION_WINDOW) {
    browsingContext->SetRemoteSubframes(true);
  }
  if (aChromeMask & nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW) {
    browsingContext->SetPrivateBrowsing(true);
  }

  RefPtr<nsOpenWindowInfo> openWindowInfo = new nsOpenWindowInfo();
  if (aIsChrome) {
    openWindowInfo->mPrincipalToInheritForAboutBlank =
        nsContentUtils::GetSystemPrincipal();
  } else {
    openWindowInfo->mPrincipalToInheritForAboutBlank =
        NullPrincipal::CreateWithoutOriginAttributes();
  }

  /* Next, we create an instance of nsWebBrowser. Instances of this class have
   * an associated doc shell, which is what we're interested in.
   */
  RefPtr<nsWebBrowser> browser;
  MOZ_TRY(nsWebBrowser::Create(stub, widget, browsingContext,
                               nullptr /* initialWindowChild */, openWindowInfo,
                               getter_AddRefs(browser)));

  if (NS_WARN_IF(!browser)) {
    NS_ERROR("Couldn't create instance of nsWebBrowser!");
    return NS_ERROR_FAILURE;
  }

  // Make sure the container window owns the the nsWebBrowser instance.
  stub->SetBrowser(browser);

  nsISupports* isstub = NS_ISUPPORTS_CAST(nsIWebBrowserChrome*, stub);
  RefPtr<nsIWindowlessBrowser> result = new WindowlessBrowser(browser, isstub);
  nsCOMPtr<nsIDocShell> docshell = do_GetInterface(result);
  docshell->SetInvisible(true);

  result.forget(aResult);
  return NS_OK;
}

/*
 * Just do the window-making part of CreateTopLevelWindow
 */
nsresult nsAppShellService::JustCreateTopWindow(
    nsIAppWindow* aParent, nsIURI* aUrl, uint32_t aChromeMask,
    int32_t aInitialWidth, int32_t aInitialHeight, bool aIsHiddenWindow,
    AppWindow** aResult) {
  using BorderStyle = widget::BorderStyle;
  *aResult = nullptr;
  NS_ENSURE_STATE(!mXPCOMWillShutDown);

  nsCOMPtr<nsIAppWindow> parent;
  if (aChromeMask & nsIWebBrowserChrome::CHROME_DEPENDENT) parent = aParent;

  RefPtr<AppWindow> window = new AppWindow(aChromeMask);

#ifdef XP_WIN
  // If the parent is currently fullscreen, tell the child to ignore persisted
  // full screen states. This way new browser windows open on top of fullscreen
  // windows normally.
  if (nsCOMPtr<nsIBaseWindow> baseWin = do_QueryInterface(aParent)) {
    nsCOMPtr<nsIWidget> widget = baseWin->GetMainWidget();
    if (widget && widget->SizeMode() == nsSizeMode_Fullscreen) {
      window->IgnoreXULSizeMode(true);
    }
  }
#endif

  widget::InitData widgetInitData;
  if (aIsHiddenWindow) {
    widgetInitData.mWindowType = widget::WindowType::Invisible;
  } else {
    widgetInitData.mWindowType =
        aChromeMask & nsIWebBrowserChrome::CHROME_OPENAS_DIALOG
            ? widget::WindowType::Dialog
            : widget::WindowType::TopLevel;
  }

  if (aChromeMask & nsIWebBrowserChrome::CHROME_SUPPRESS_ANIMATION) {
    widgetInitData.mIsAnimationSuppressed = true;
  }

  if (aChromeMask & nsIWebBrowserChrome::CHROME_SUPPRESS_INITIAL_FULLSCREEN) {
    widgetInitData.mIsInitialFullscreenSuppressed = true;
  }

  if (aChromeMask & nsIWebBrowserChrome::CHROME_ALWAYS_ON_TOP) {
    widgetInitData.mAlwaysOnTop = true;
  }

  if (aChromeMask & nsIWebBrowserChrome::CHROME_REMOTE_WINDOW) {
    widgetInitData.mHasRemoteContent = true;
  }

#if defined(MOZ_WIDGET_GTK) || defined(XP_WIN)
  // Windows/Gtk PIP window support. It's Chrome dialog window, always on top
  // and without any bar.
  uint32_t pipMask = nsIWebBrowserChrome::CHROME_ALWAYS_ON_TOP |
                     nsIWebBrowserChrome::CHROME_OPENAS_CHROME |
                     nsIWebBrowserChrome::CHROME_WINDOW_RESIZE;
  uint32_t barMask = nsIWebBrowserChrome::CHROME_MENUBAR |
                     nsIWebBrowserChrome::CHROME_TOOLBAR |
                     nsIWebBrowserChrome::CHROME_TITLEBAR;
  if (widgetInitData.mWindowType == widget::WindowType::Dialog &&
      ((aChromeMask & pipMask) == pipMask) && !(aChromeMask & barMask)) {
    widgetInitData.mPiPType = mozilla::widget::PiPType::MediaPiP;
  }
#endif

  if (widgetInitData.mWindowType == widget::WindowType::TopLevel &&
      (aChromeMask & nsIWebBrowserChrome::CHROME_DOCUMENT_PIP)) {
    widgetInitData.mPiPType = mozilla::widget::PiPType::DocumentPiP;
  }

  // alert=yes is expected to be used along with dialogs, not other window
  // types.
  MOZ_ASSERT_IF(aChromeMask & nsIWebBrowserChrome::CHROME_ALERT,
                widgetInitData.mWindowType == widget::WindowType::Dialog);
  widgetInitData.mIsAlert =
      !!(aChromeMask & nsIWebBrowserChrome::CHROME_ALERT) &&
      widgetInitData.mWindowType == widget::WindowType::Dialog;

#if defined(XP_WIN)
  if (widgetInitData.mWindowType == widget::WindowType::TopLevel ||
      widgetInitData.mWindowType == widget::WindowType::Dialog) {
    widgetInitData.mClipChildren = true;
  }
#endif

  // note default chrome overrides other OS chrome settings, but
  // not internal chrome
  if (aChromeMask & nsIWebBrowserChrome::CHROME_DEFAULT) {
    widgetInitData.mBorderStyle = BorderStyle::Default;
  } else if ((aChromeMask & nsIWebBrowserChrome::CHROME_ALL) ==
             nsIWebBrowserChrome::CHROME_ALL) {
    widgetInitData.mBorderStyle = BorderStyle::All;
  } else {
    widgetInitData.mBorderStyle = BorderStyle::Border;
    if (aChromeMask & nsIWebBrowserChrome::CHROME_TITLEBAR) {
      widgetInitData.mBorderStyle |= BorderStyle::Title;
    }
    if (aChromeMask & nsIWebBrowserChrome::CHROME_WINDOW_RESIZE) {
      widgetInitData.mResizable = true;
      widgetInitData.mBorderStyle |= BorderStyle::ResizeH;
      // only resizable windows get the maximize button (but not dialogs)
      if (!(aChromeMask & nsIWebBrowserChrome::CHROME_OPENAS_DIALOG)) {
        widgetInitData.mBorderStyle |= BorderStyle::Maximize;
      }
    }
    // all windows (except dialogs) get minimize buttons and the system menu
    if (!(aChromeMask & nsIWebBrowserChrome::CHROME_OPENAS_DIALOG)) {
      widgetInitData.mBorderStyle |= BorderStyle::Minimize | BorderStyle::Menu;
    }
    // but anyone can explicitly ask for a minimize button
    if (aChromeMask & nsIWebBrowserChrome::CHROME_WINDOW_MINIMIZE) {
      widgetInitData.mBorderStyle |= BorderStyle::Minimize;
    }
  }

  if (aInitialWidth == nsIAppShellService::SIZE_TO_CONTENT ||
      aInitialHeight == nsIAppShellService::SIZE_TO_CONTENT) {
    aInitialWidth = 1;
    aInitialHeight = 1;
    window->SetIntrinsicallySized(true);
  }

  bool center = aChromeMask & nsIWebBrowserChrome::CHROME_CENTER_SCREEN;

  widgetInitData.mRTL = LocaleService::GetInstance()->IsAppLocaleRTL();

  // Enforce the Private Browsing autoStart pref first.
  bool isPrivateBrowsingWindow =
      StaticPrefs::browser_privatebrowsing_autostart();
  if (aChromeMask & nsIWebBrowserChrome::CHROME_PRIVATE_WINDOW) {
    // Caller requested a private window
    isPrivateBrowsingWindow = true;
  }
  widgetInitData.mIsPrivate = isPrivateBrowsingWindow;

  RefPtr<nsOpenWindowInfo> openWindowInfo = new nsOpenWindowInfo();
  // Eagerly create an about:blank content viewer with the right principal
  // here, rather than letting it happen in the upcoming call to
  // SetInitialPrincipal. This avoids creating the about:blank document and
  // then blowing it away with a second one, which can cause problems for the
  // top-level chrome window case. See bug 789773.
  // Toplevel chrome windows always have a system principal, so ensure the
  // initial window is created with that principal.
  // We need to do this even when creating a chrome window to load a content
  // window, see bug 799348 comment 13 for details about what previously
  // happened here due to it using the subject principal.
  if (nsContentUtils::IsInitialized()) {  // Sometimes this happens really
                                          // early. See bug 793370.
    MOZ_DIAGNOSTIC_ASSERT(
        nsContentUtils::LegacyIsCallerChromeOrNativeCode(),
        "Previously, this method would use the subject principal rather than "
        "hardcoding the system principal");
    openWindowInfo->mPrincipalToInheritForAboutBlank =
        nsContentUtils::GetSystemPrincipal();
  }

  nsresult rv = window->Initialize(
      parent, center ? aParent : nullptr, aInitialWidth, aInitialHeight,
      aIsHiddenWindow, widgetInitData, openWindowInfo);

  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIDOMWindowProxy> domWin = do_GetInterface(aParent);
  nsCOMPtr<nsIWebNavigation> webNav = do_GetInterface(domWin);
  nsCOMPtr<nsILoadContext> parentContext = do_QueryInterface(webNav);

  if (!isPrivateBrowsingWindow && parentContext) {
    // Ensure that we propagate any existing private browsing status
    // from the parent, even if it will not actually be used
    // as a parent value.
    isPrivateBrowsingWindow = parentContext->UsePrivateBrowsing();
  }

  RefPtr<nsDocShell> docShell = window->GetDocShell();
  NS_ENSURE_TRUE(docShell, NS_ERROR_UNEXPECTED);

  MOZ_ASSERT(docShell->GetBrowsingContext()->IsChrome());

  docShell->SetPrivateBrowsing(isPrivateBrowsingWindow);
  docShell->SetRemoteTabs(aChromeMask &
                          nsIWebBrowserChrome::CHROME_REMOTE_WINDOW);
  docShell->SetRemoteSubframes(aChromeMask &
                               nsIWebBrowserChrome::CHROME_FISSION_WINDOW);

  if ((aChromeMask & nsIWebBrowserChrome::CHROME_DOCUMENT_PIP)) {
    rv = docShell->GetBrowsingContext()->SetIsDocumentPiP(true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Begin loading the URL provided.
  if (aUrl) {
    RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(aUrl);
    loadState->SetTriggeringPrincipal(nsContentUtils::GetSystemPrincipal());
    loadState->SetFirstParty(true);
    rv = docShell->LoadURI(loadState, /* aSetNavigating */ true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  window.forget(aResult);

  if (center) rv = (*aResult)->Center(parent, parent ? false : true, false);

  return rv;
}

NS_IMETHODIMP
nsAppShellService::GetHiddenWindow(nsIAppWindow** aWindow) {
  NS_ENSURE_ARG_POINTER(aWindow);

  *aWindow = mHiddenWindow;
  NS_IF_ADDREF(*aWindow);
  return *aWindow ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsAppShellService::GetHiddenDOMWindow(mozIDOMWindowProxy** aWindow) {
  NS_ENSURE_ARG_POINTER(aWindow);

  nsresult rv;
  nsCOMPtr<nsIDocShell> docShell;
  NS_ENSURE_TRUE(mHiddenWindow, NS_ERROR_FAILURE);

  rv = mHiddenWindow->GetDocShell(getter_AddRefs(docShell));
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  nsCOMPtr<nsPIDOMWindowOuter> hiddenDOMWindow(docShell->GetWindow());
  hiddenDOMWindow.forget(aWindow);
  return *aWindow ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
nsAppShellService::GetHasHiddenWindow(bool* aHasHiddenWindow) {
  NS_ENSURE_ARG_POINTER(aHasHiddenWindow);

  *aHasHiddenWindow = !!mHiddenWindow;
  return NS_OK;
}

/*
 * Register a new top level window (created elsewhere)
 */
NS_IMETHODIMP
nsAppShellService::RegisterTopLevelWindow(nsIAppWindow* aWindow) {
  NS_ENSURE_ARG_POINTER(aWindow);

  nsCOMPtr<nsIDocShell> docShell;
  aWindow->GetDocShell(getter_AddRefs(docShell));
  NS_ENSURE_TRUE(docShell, NS_ERROR_FAILURE);

  nsCOMPtr<nsPIDOMWindowOuter> domWindow(docShell->GetWindow());
  NS_ENSURE_TRUE(domWindow, NS_ERROR_FAILURE);

  // Toplevel chrome windows always have a system principal, so ensure the
  // initial window is created with that principal.
  // We need to do this even when creating a chrome window to load a content
  // window, see bug 799348 comment 13 for details about what previously
  // happened here due to it using the subject principal.
  MOZ_DIAGNOSTIC_ASSERT(
      nsContentUtils::LegacyIsCallerChromeOrNativeCode(),
      "Previously, this method would use the subject principal rather than "
      "hardcoding the system principal");
#ifdef DEBUG
  mozilla::dom::Document* doc = domWindow->GetDoc();
  if (doc) {
    MOZ_ASSERT(doc->GetPrincipal() == nsContentUtils::GetSystemPrincipal(),
               "Wrong principal!");
  } else {
    MOZ_ASSERT(false, "How come there was no doc?");
  }
#endif

  // tell the window mediator about the new window
  nsCOMPtr<nsIWindowMediator> mediator(
      do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));
  NS_ASSERTION(mediator, "Couldn't get window mediator.");

  if (mediator) mediator->RegisterWindow(aWindow);

  // tell the window watcher about the new window
  nsCOMPtr<nsPIWindowWatcher> wwatcher(
      do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  NS_ASSERTION(wwatcher, "No windowwatcher?");
  if (wwatcher && domWindow) {
    wwatcher->AddWindow(domWindow, nullptr);
  }

  // an ongoing attempt to quit is stopped by a newly opened window
  nsCOMPtr<nsIObserverService> obssvc = services::GetObserverService();
  NS_ASSERTION(obssvc, "Couldn't get observer service.");

  if (obssvc) {
    obssvc->NotifyObservers(aWindow, "xul-window-registered", nullptr);
    AppWindow* appWindow = static_cast<AppWindow*>(aWindow);
    appWindow->WasRegistered();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAppShellService::UnregisterTopLevelWindow(nsIAppWindow* aWindow) {
  if (mXPCOMShuttingDown) {
    /* return an error code in order to:
       - avoid doing anything with other member variables while we are in
         the destructor
       - notify the caller not to release the AppShellService after
         unregistering the window
         (we don't want to be deleted twice consecutively to
         mHiddenWindow->Destroy() in our destructor)
    */
    return NS_ERROR_FAILURE;
  }

  NS_ENSURE_ARG_POINTER(aWindow);

  if (aWindow == mHiddenWindow) {
    // CreateHiddenWindow() does not register the window, so we're done.
    return NS_OK;
  }

  // tell the window mediator
  nsCOMPtr<nsIWindowMediator> mediator(
      do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));
  NS_ASSERTION(mediator, "Couldn't get window mediator. Doing xpcom shutdown?");

  if (mediator) mediator->UnregisterWindow(aWindow);

  // tell the window watcher
  nsCOMPtr<nsPIWindowWatcher> wwatcher(
      do_GetService(NS_WINDOWWATCHER_CONTRACTID));
  NS_ASSERTION(wwatcher, "Couldn't get windowwatcher, doing xpcom shutdown?");
  if (wwatcher) {
    nsCOMPtr<nsIDocShell> docShell;
    aWindow->GetDocShell(getter_AddRefs(docShell));
    if (docShell) {
      nsCOMPtr<nsPIDOMWindowOuter> domWindow(docShell->GetWindow());
      if (domWindow) wwatcher->RemoveWindow(domWindow);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsAppShellService::Observe(nsISupports* aSubject, const char* aTopic,
                           const char16_t* aData) {
  if (!strcmp(aTopic, "xpcom-will-shutdown")) {
    mXPCOMWillShutDown = true;
  } else if (!strcmp(aTopic, "xpcom-shutdown")) {
    mXPCOMShuttingDown = true;
    if (mHiddenWindow) {
      mHiddenWindow->Destroy();
    }
  } else {
    NS_ERROR("Unexpected observer topic!");
  }

  return NS_OK;
}
