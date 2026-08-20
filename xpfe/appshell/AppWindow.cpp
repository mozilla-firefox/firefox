/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ErrorList.h"
#include "mozilla/MathAlgorithms.h"

// Local includes
#include "AppWindow.h"
#include <algorithm>

// Helper classes
#include "nsPrintfCString.h"
#include "nsString.h"
#include "nsWidgetsCID.h"
#include "nsThreadUtils.h"
#include "nsNetCID.h"
#include "nsQueryObject.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/Sprintf.h"
#include "mozilla/Try.h"

// Interfaces needed to be included
#include "nsGlobalWindowOuter.h"
#include "nsIAppShell.h"
#include "nsIAppShellService.h"
#include "nsIDocumentViewer.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "nsPIDOMWindow.h"
#include "nsScreen.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIIOService.h"
#include "nsIObserverService.h"
#include "nsIOpenWindowInfo.h"
#include "nsIWindowMediator.h"
#include "nsIScreenManager.h"
#include "nsIScreen.h"
#include "nsIWindowWatcher.h"
#include "nsIURI.h"
#include "nsAppShellCID.h"
#include "nsReadableUtils.h"
#include "nsStyleConsts.h"
#include "nsPresContext.h"
#include "nsContentUtils.h"
#include "nsXULTooltipListener.h"
#include "nsXULPopupManager.h"
#include "nsFocusManager.h"
#include "mozilla/dom/ContentList.h"
#include "nsIDOMWindowUtils.h"
#include "nsServiceManagerUtils.h"

#include "prenv.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/Preferences.h"
#include "mozilla/PresShell.h"
#include "mozilla/Services.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/dom/BarProps.h"
#include "mozilla/dom/DOMRect.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/BrowserHost.h"
#include "mozilla/dom/BrowserParent.h"
#include "mozilla/dom/LoadURIOptionsBinding.h"
#include "mozilla/intl/LocaleService.h"
#include "mozilla/EventDispatcher.h"

#ifdef XP_WIN
#  include "mozilla/PreXULSkeletonUI.h"
#  include "mozilla/WindowsVersion.h"
#  include "nsIWindowsUIUtils.h"
#endif

#include "mozilla/dom/DocumentL10n.h"

#if defined(XP_MACOSX) || defined(MOZ_WIDGET_GTK)
#  include "mozilla/widget/NativeMenuSupport.h"
#  define USE_NATIVE_MENUS
#endif

#define SIZEMODE_NORMAL u"normal"_ns
#define SIZEMODE_MAXIMIZED u"maximized"_ns
#define SIZEMODE_MINIMIZED u"minimized"_ns
#define SIZEMODE_FULLSCREEN u"fullscreen"_ns

#define SIZE_PERSISTENCE_TIMEOUT 500  // msec

//*****************************************************************************
//***    AppWindow: Object Management
//*****************************************************************************

namespace mozilla {

using dom::AutoNoJSAPI;
using dom::BrowserHost;
using dom::BrowsingContext;
using dom::Document;
using dom::DocumentL10n;
using dom::Element;
using dom::EventTarget;
using dom::LoadURIOptions;
using dom::Promise;

AppWindow::AppWindow(uint32_t aChromeFlags)
    : mChromeTreeOwner(nullptr),
      mContentTreeOwner(nullptr),
      mPrimaryContentTreeOwner(nullptr),
      mModalStatus(NS_OK),
      mFullscreenChangeState(FullscreenChangeState::NotChanging),
      mContinueModalLoop(false),
      mDebuting(false),
      mChromeLoaded(false),
      mSizingShellFromXUL(false),
      mShowAfterLoad(false),
      mIntrinsicallySized(false),
      mCenterAfterLoad(false),
      mIsHiddenWindow(false),
      mLockedUntilChromeLoad(false),
      mIgnoreXULSize(false),
      mIgnoreXULPosition(false),
      mIgnoreXULSizeMode(false),
      mDestroying(false),
      mRegistered(false),
      mDominantClientSize(false),
      mChromeFlags(aChromeFlags),
      mWidgetListenerDelegate(this) {}

AppWindow::~AppWindow() {
  if (mSPTimer) {
    mSPTimer->Cancel();
    mSPTimer = nullptr;
  }
  Destroy();
}

//*****************************************************************************
// AppWindow::nsISupports
//*****************************************************************************

NS_IMPL_ADDREF(AppWindow)
NS_IMPL_RELEASE(AppWindow)

NS_INTERFACE_MAP_BEGIN(AppWindow)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIAppWindow)
  NS_INTERFACE_MAP_ENTRY(nsIAppWindow)
  NS_INTERFACE_MAP_ENTRY(nsIBaseWindow)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIWebProgressListener)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(AppWindow)
NS_INTERFACE_MAP_END

nsresult AppWindow::Initialize(nsIAppWindow* aParent, nsIAppWindow* aOpener,
                               int32_t aInitialWidth, int32_t aInitialHeight,
                               bool aIsHiddenWindow,
                               widget::InitData& widgetInitData,
                               nsIOpenWindowInfo* aOpenWindowInfo) {
  nsresult rv;
  nsCOMPtr<nsIWidget> parentWidget;

  mIsHiddenWindow = aIsHiddenWindow;

  DesktopIntPoint initialPos;
  nsCOMPtr<nsIBaseWindow> base(do_QueryInterface(aOpener));
  if (base) {
    LayoutDeviceIntRect rect = base->GetPositionAndSize();
    mOpenerScreenRect =
        DesktopIntRect::Round(rect / base->DevicePixelsPerDesktopPixel());
    if (!mOpenerScreenRect.IsEmpty()) {
      initialPos = mOpenerScreenRect.TopLeft();
      ConstrainToOpenerScreen(&initialPos.x.value, &initialPos.y.value);
    }
  }

  // XXX: need to get the default window size from prefs...
  // Doesn't come from prefs... will come from CSS/XUL/RDF
  DesktopIntRect deskRect(initialPos,
                          DesktopIntSize(aInitialWidth, aInitialHeight));

  // Create top level window
  if (gfxPlatform::IsHeadless()) {
    mWindow = nsIWidget::CreateHeadlessWidget();
  } else {
    mWindow = nsIWidget::CreateTopLevelWindow();
  }
  if (!mWindow) {
    return NS_ERROR_FAILURE;
  }

  /* This next bit is troublesome. We carry two different versions of a pointer
     to our parent window. One is the parent window's widget, which is passed
     to our own widget. The other is a weak reference we keep here to our
     parent AppWindow. The former is useful to the widget, and we can't
     trust its treatment of the parent reference because they're platform-
     specific. The latter is useful to this class.
       A better implementation would be one in which the parent keeps strong
     references to its children and closes them before it allows itself
     to be closed. This would mimic the behaviour of OSes that support
     top-level child windows in OSes that do not. Later.
  */
  if (nsCOMPtr<nsIBaseWindow> parent = do_QueryInterface(aParent)) {
    parentWidget = parent->GetMainWidget();
    mParentWindow = do_GetWeakReference(aParent);
  }

  mWindow->SetWidgetListener(&mWidgetListenerDelegate);
  rv = mWindow->Create(parentWidget.get(),  // Parent nsIWidget
                       deskRect,            // Widget dimensions
                       widgetInitData);     // Widget initialization data
  NS_ENSURE_SUCCESS(rv, rv);

  LayoutDeviceIntRect r = mWindow->GetClientBounds();
  // Match the default background color of content. Previously important on
  // Windows, but no longer has any effect there.
  mWindow->SetBackgroundColor(NS_RGB(255, 255, 255));

  // All Chrome BCs exist within the same BrowsingContextGroup, so we don't need
  // to pass in the opener window here. The opener is set later, if needed, by
  // nsWindowWatcher.
  RefPtr<BrowsingContext> browsingContext =
      BrowsingContext::CreateIndependent(BrowsingContext::Type::Chrome, false);

  // Create web shell
  mDocShell = nsDocShell::Create(browsingContext);
  NS_ENSURE_TRUE(mDocShell, NS_ERROR_FAILURE);

  // Make sure to set the item type on the docshell _before_ calling
  // InitWindow() so it knows what type it is.
  NS_ENSURE_SUCCESS(EnsureChromeTreeOwner(), NS_ERROR_FAILURE);

  mDocShell->SetTreeOwner(mChromeTreeOwner);

  r.MoveTo(0, 0);
  NS_ENSURE_SUCCESS(mDocShell->InitWindow(mWindow, r.X(), r.Y(), r.Width(),
                                          r.Height(), aOpenWindowInfo, nullptr),
                    NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(mDocShell->GetDocument(), NS_ERROR_FAILURE);

  // Attach a WebProgress listener.during initialization...
  mDocShell->AddProgressListener(this, nsIWebProgress::NOTIFY_STATE_NETWORK);

  mWindow->MaybeDispatchInitialFocusEvent();

  return rv;
}

//*****************************************************************************
// AppWindow::nsIIntefaceRequestor
//*****************************************************************************

NS_IMETHODIMP AppWindow::GetInterface(const nsIID& aIID, void** aSink) {
  nsresult rv;

  NS_ENSURE_ARG_POINTER(aSink);

  if (aIID.Equals(NS_GET_IID(nsIPrompt))) {
    rv = EnsurePrompter();
    if (NS_FAILED(rv)) return rv;
    return mPrompter->QueryInterface(aIID, aSink);
  }
  if (aIID.Equals(NS_GET_IID(nsIAuthPrompt))) {
    rv = EnsureAuthPrompter();
    if (NS_FAILED(rv)) return rv;
    return mAuthPrompter->QueryInterface(aIID, aSink);
  }
  if (aIID.Equals(NS_GET_IID(mozIDOMWindowProxy))) {
    return GetWindowDOMWindow(reinterpret_cast<mozIDOMWindowProxy**>(aSink));
  }
  if (aIID.Equals(NS_GET_IID(nsIDOMWindow))) {
    nsCOMPtr<mozIDOMWindowProxy> window = nullptr;
    rv = GetWindowDOMWindow(getter_AddRefs(window));
    nsCOMPtr<nsIDOMWindow> domWindow = do_QueryInterface(window);
    domWindow.forget(aSink);
    return rv;
  }
  if (aIID.Equals(NS_GET_IID(nsIWebBrowserChrome)) &&
      NS_SUCCEEDED(EnsureContentTreeOwner()) &&
      NS_SUCCEEDED(mContentTreeOwner->QueryInterface(aIID, aSink))) {
    return NS_OK;
  }

  return QueryInterface(aIID, aSink);
}

//*****************************************************************************
// AppWindow::nsIAppWindow
//*****************************************************************************

NS_IMETHODIMP AppWindow::GetDocShell(nsIDocShell** aDocShell) {
  NS_ENSURE_ARG_POINTER(aDocShell);

  *aDocShell = mDocShell;
  NS_IF_ADDREF(*aDocShell);
  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetChromeFlags(uint32_t* aChromeFlags) {
  NS_ENSURE_ARG_POINTER(aChromeFlags);
  *aChromeFlags = mChromeFlags;
  return NS_OK;
}

NS_IMETHODIMP AppWindow::SetIntrinsicallySized(bool aIntrinsicallySized) {
  mIntrinsicallySized = aIntrinsicallySized;
  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetIntrinsicallySized(bool* aIntrinsicallySized) {
  NS_ENSURE_ARG_POINTER(aIntrinsicallySized);

  *aIntrinsicallySized = mIntrinsicallySized;
  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetPrimaryContentShell(
    nsIDocShellTreeItem** aDocShellTreeItem) {
  NS_ENSURE_ARG_POINTER(aDocShellTreeItem);
  NS_IF_ADDREF(*aDocShellTreeItem = mPrimaryContentShell);
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::RemoteTabAdded(nsIRemoteTab* aTab, bool aPrimary) {
  if (aPrimary) {
    mPrimaryBrowserParent = aTab;
    mPrimaryContentShell = nullptr;
  } else if (mPrimaryBrowserParent == aTab) {
    mPrimaryBrowserParent = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
AppWindow::RemoteTabRemoved(nsIRemoteTab* aTab) {
  if (aTab == mPrimaryBrowserParent) {
    mPrimaryBrowserParent = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
AppWindow::GetPrimaryRemoteTab(nsIRemoteTab** aTab) {
  nsCOMPtr<nsIRemoteTab> tab = mPrimaryBrowserParent;
  tab.forget(aTab);
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::GetPrimaryContentBrowsingContext(
    mozilla::dom::BrowsingContext** aBc) {
  if (mPrimaryBrowserParent) {
    return mPrimaryBrowserParent->GetBrowsingContext(aBc);
  }
  if (mPrimaryContentShell) {
    return mPrimaryContentShell->GetBrowsingContextXPCOM(aBc);
  }
  *aBc = nullptr;
  return NS_OK;
}

static LayoutDeviceIntSize GetOuterToInnerSizeDifference(nsIWidget* aWindow) {
  if (!aWindow) {
    return LayoutDeviceIntSize();
  }
  return aWindow->NormalSizeModeClientToWindowSizeDifference();
}

static CSSIntSize GetOuterToInnerSizeDifferenceInCSSPixels(
    nsIWidget* aWindow, CSSToLayoutDeviceScale aScale) {
  LayoutDeviceIntSize devPixelSize = GetOuterToInnerSizeDifference(aWindow);
  return RoundedToInt(devPixelSize / aScale);
}

NS_IMETHODIMP
AppWindow::GetOuterToInnerHeightDifferenceInCSSPixels(uint32_t* aResult) {
  if (mWindow && mWindow->PersistClientBounds()) {
    *aResult = 0;
  } else {
    *aResult = GetOuterToInnerSizeDifferenceInCSSPixels(
                   mWindow, UnscaledDevicePixelsPerCSSPixel())
                   .height;
  }
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::GetOuterToInnerWidthDifferenceInCSSPixels(uint32_t* aResult) {
  if (mWindow && mWindow->PersistClientBounds()) {
    *aResult = 0;
  } else {
    *aResult = GetOuterToInnerSizeDifferenceInCSSPixels(
                   mWindow, UnscaledDevicePixelsPerCSSPixel())
                   .width;
  }
  return NS_OK;
}

nsTArray<RefPtr<mozilla::LiveResizeListener>>
AppWindow::GetLiveResizeListeners() {
  nsTArray<RefPtr<mozilla::LiveResizeListener>> listeners;
  if (mPrimaryBrowserParent) {
    BrowserHost* host = BrowserHost::GetFrom(mPrimaryBrowserParent.get());
    RefPtr<mozilla::LiveResizeListener> actor = host->GetActor();
    if (actor) {
      listeners.AppendElement(actor);
    }
  }
  return listeners;
}

NS_IMETHODIMP AppWindow::ShowModal() {
  AUTO_PROFILER_LABEL("AppWindow::ShowModal", OTHER);

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    MOZ_ASSERT_UNREACHABLE(
        "Trying to show modal window after shutdown started.");
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  // Store locally so it doesn't die on us
  nsCOMPtr<nsIWidget> window = mWindow;
  nsCOMPtr<nsIAppWindow> tempRef = this;

#ifdef USE_NATIVE_MENUS
  if (!gfxPlatform::IsHeadless()) {
    // On macOS, for modals created early in startup. (e.g.
    // ProfileManager/ProfileDowngrade) this creates a fallback menu for the
    // menu bar which only contains a "Quit" menu item. This allows the user to
    // quit the application in a regular way with cmd+Q.
    widget::NativeMenuSupport::CreateNativeMenuBar(mWindow, nullptr);
  }
#endif

  window->SetModal(true);
  mContinueModalLoop = true;
  EnableParent(false);

  {
    AutoNoJSAPI nojsapi;
    SpinEventLoopUntil("AppWindow::ShowModal"_ns, [&]() {
      if (MOZ_UNLIKELY(
              AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed))) {
        // TODO: Bug 1699041 would apply also here: Should we return an error
        // if we are bailing out from a pre-existing modal dialog for shutdown?
        ExitModalLoop(NS_OK);
      }
      return !mContinueModalLoop;
    });
  }

  mContinueModalLoop = false;
  window->SetModal(false);
  /*   Note there's no EnableParent(true) here to match the false one
     above. That's done in ExitModalLoop. It's important that the parent
     be re-enabled before this window is made invisible; to do otherwise
     causes bizarre z-ordering problems. At this point, the window is
     already invisible.
       No known current implementation of Enable would have a problem with
     re-enabling the parent twice, so we could do it again here without
     breaking any current implementation. But that's unnecessary if the
     modal loop is always exited using ExitModalLoop (the other way would be
     to change the protected member variable directly.)
  */

  return mModalStatus;
}

NS_IMETHODIMP AppWindow::RollupAllPopups() {
  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    pm->Rollup({});
  }
  return NS_OK;
}

//*****************************************************************************
// AppWindow::nsIBaseWindow
//*****************************************************************************

NS_IMETHODIMP AppWindow::Destroy() {
  nsCOMPtr<nsIAppWindow> kungFuDeathGrip(this);

  if (mDocShell) {
    mDocShell->RemoveProgressListener(this);
  }

  if (mSPTimer) {
    mSPTimer->Cancel();
    SavePersistentAttributes();
    mSPTimer = nullptr;
  }

  if (!mWindow) return NS_OK;

  // Ensure we don't reenter this code
  if (mDestroying) return NS_OK;

  mozilla::AutoRestore<bool> guard(mDestroying);
  mDestroying = true;

  nsCOMPtr<nsIAppShellService> appShell(
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
  NS_ASSERTION(appShell, "Couldn't get appShell... xpcom shutdown?");
  if (appShell) {
    appShell->UnregisterTopLevelWindow(static_cast<nsIAppWindow*>(this));
  }

  // Remove modality (if any) and hide while destroying. More than
  // a convenience, the hide prevents user interaction with the partially
  // destroyed window. This is especially necessary when the eldest window
  // in a stack of modal windows is destroyed first. It happens.
  ExitModalLoop(NS_OK);
  // XXX: Skip unmapping the window on Linux due to GLX hangs on the compositor
  // thread with NVIDIA driver 310.32. We don't need to worry about user
  // interactions with destroyed windows on X11 either.
#ifndef MOZ_WIDGET_GTK
  if (mWindow) mWindow->Show(false);
#endif

  // Raise and focus our parent explicitly on Windows, if visible. Apparently
  // Windows gets the z-order and focus wrong otherwise for nested modal
  // windows, see bug 1977581.
#ifdef XP_WIN
  if (nsCOMPtr<nsIBaseWindow> parent = do_QueryReferent(mParentWindow)) {
    if (nsCOMPtr<nsIWidget> parentWidget = parent->GetMainWidget();
        parentWidget && parentWidget->IsVisible()) {
      parentWidget->SetFocus(nsIWidget::Raise::Yes, dom::CallerType::System);
    }
  }
#endif

  RemoveTooltipSupport();

  mDOMWindow = nullptr;
  if (mDocShell) {
    RefPtr<BrowsingContext> bc(mDocShell->GetBrowsingContext());
    mDocShell->Destroy();
    bc->Detach();
    mDocShell = nullptr;  // this can cause reentrancy of this function
  }

  mPrimaryContentShell = nullptr;

  if (mContentTreeOwner) {
    mContentTreeOwner->AppWindow(nullptr);
    NS_RELEASE(mContentTreeOwner);
  }
  if (mPrimaryContentTreeOwner) {
    mPrimaryContentTreeOwner->AppWindow(nullptr);
    NS_RELEASE(mPrimaryContentTreeOwner);
  }
  if (mChromeTreeOwner) {
    mChromeTreeOwner->AppWindow(nullptr);
    NS_RELEASE(mChromeTreeOwner);
  }
  if (mWindow) {
    mWindow->SetWidgetListener(nullptr);  // nsWebShellWindow hackery
    mWindow->Destroy();
    mWindow = nullptr;
  }

  if (!mIsHiddenWindow && mRegistered) {
    /* Inform appstartup we've destroyed this window and it could
       quit now if it wanted. This must happen at least after mDocShell
       is destroyed, because onunload handlers fire then, and those being
       script, anything could happen. A new window could open, even.
       See bug 130719. */
    nsCOMPtr<nsIObserverService> obssvc = services::GetObserverService();
    NS_ASSERTION(obssvc, "Couldn't get observer service?");

    if (obssvc)
      obssvc->NotifyObservers(nullptr, "xul-window-destroyed", nullptr);
  }

  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetDevicePixelsPerDesktopPixel(double* aScale) {
  *aScale = mWindow ? mWindow->GetDesktopToDeviceScale().scale : 1.0;
  return NS_OK;
}

double AppWindow::GetWidgetCSSToDeviceScale() {
  return mWindow ? mWindow->GetDefaultScale().scale : 1.0;
}

NS_IMETHODIMP AppWindow::SetPositionDesktopPix(int32_t aX, int32_t aY) {
  return MoveResize(Some(DesktopIntPoint(aX, aY)), Nothing(), false);
}

// The parameters here are device pixels; do the best we can to convert to
// desktop px, using the window's current scale factor (if available).
NS_IMETHODIMP AppWindow::SetPosition(int32_t aX, int32_t aY) {
  // Don't reset the window's size mode here - platforms that don't want to move
  // maximized windows should reset it in their respective Move implementation.
  return MoveResize(Some(LayoutDeviceIntPoint(aX, aY)), Nothing(), false);
}

NS_IMETHODIMP AppWindow::GetPosition(int32_t* aX, int32_t* aY) {
  return GetPositionAndSize(aX, aY, nullptr, nullptr);
}

NS_IMETHODIMP AppWindow::SetSize(int32_t aCX, int32_t aCY, bool aRepaint) {
  /* any attempt to set the window's size or position overrides the window's
     zoom state. this is important when these two states are competing while
     the window is being opened. but it should probably just always be so. */
  return MoveResize(Nothing(), Some(LayoutDeviceIntSize(aCX, aCY)), aRepaint);
}

NS_IMETHODIMP AppWindow::GetSize(int32_t* aCX, int32_t* aCY) {
  return GetPositionAndSize(nullptr, nullptr, aCX, aCY);
}

NS_IMETHODIMP AppWindow::SetPositionAndSize(int32_t aX, int32_t aY, int32_t aCX,
                                            int32_t aCY, uint32_t aFlags) {
  /* any attempt to set the window's size or position overrides the window's
     zoom state. this is important when these two states are competing while
     the window is being opened. but it should probably just always be so. */
  return MoveResize(Some(LayoutDeviceIntPoint(aX, aY)),
                    Some(LayoutDeviceIntSize(aCX, aCY)),
                    !!(aFlags & nsIBaseWindow::eRepaint));
}

NS_IMETHODIMP AppWindow::GetPositionAndSize(int32_t* x, int32_t* y, int32_t* cx,
                                            int32_t* cy) {
  if (!mWindow) return NS_ERROR_FAILURE;

  LayoutDeviceIntRect rect = mWindow->GetScreenBounds();

  if (x) *x = rect.X();
  if (y) *y = rect.Y();
  if (cx) *cx = rect.Width();
  if (cy) *cy = rect.Height();

  return NS_OK;
}

NS_IMETHODIMP
AppWindow::SetDimensions(DimensionRequest&& aRequest) {
  if (aRequest.mDimensionKind == DimensionKind::Inner) {
    // For the chrome the inner size is the root shell size, and for the
    // content it's the primary content size. We lack an indicator here that
    // would allow us to distinguish between the two.
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  MOZ_TRY(aRequest.SupplementFrom(this));
  return aRequest.ApplyOuterTo(this);
}

NS_IMETHODIMP
AppWindow::GetDimensions(DimensionKind aDimensionKind, int32_t* aX, int32_t* aY,
                         int32_t* aCX, int32_t* aCY) {
  if (aDimensionKind == DimensionKind::Inner) {
    // For the chrome the inner size is the root shell size, and for the
    // content it's the primary content size. We lack an indicator here that
    // would allow us to distinguish between the two.
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  return GetPositionAndSize(aX, aY, aCX, aCY);
}

nsresult AppWindow::MoveResize(const Maybe<LayoutDeviceIntPoint>& aPosition,
                               const Maybe<LayoutDeviceIntSize>& aSize,
                               bool aRepaint) {
  NS_ENSURE_STATE(mWindow);
  DesktopToLayoutDeviceScale scale = mWindow->GetDesktopToDeviceScale();
  return MoveResize(aPosition ? Some(*aPosition / scale) : Nothing(),
                    aSize ? Some(*aSize / scale) : Nothing(), aRepaint);
}

nsresult AppWindow::MoveResize(const Maybe<DesktopPoint>& aPosition,
                               const Maybe<DesktopSize>& aSize, bool aRepaint) {
  NS_ENSURE_STATE(mWindow);
  PersistentAttributes dirtyAttributes;

  if (!aPosition && !aSize) {
    MOZ_ASSERT_UNREACHABLE("Doing nothing?");
    return NS_ERROR_UNEXPECTED;
  }

  if (aSize) {
    mWindow->SetSizeMode(nsSizeMode_Normal);
    mIntrinsicallySized = false;
    mDominantClientSize = false;
  }

  if (aPosition && aSize) {
    mWindow->Resize(DesktopRect(*aPosition, *aSize), aRepaint);
    dirtyAttributes = {PersistentAttribute::Size,
                       PersistentAttribute::Position};
  } else if (aSize) {
    mWindow->Resize(*aSize, aRepaint);
    dirtyAttributes = {PersistentAttribute::Size};
  } else if (aPosition) {
    mWindow->Move(*aPosition);
    dirtyAttributes = {PersistentAttribute::Position};
  }

  if (mSizingShellFromXUL) {
    // If we're invoked for sizing from XUL, we want to neither ignore anything
    // nor persist anything, since it's already the value in XUL.
    return NS_OK;
  }
  if (!mChromeLoaded) {
    // If we're called before the chrome is loaded someone obviously wants this
    // window at this size & in the normal size mode (since it is the only mode
    // in which setting dimensions makes sense). We don't persist this one-time
    // position/size.
    if (aPosition) {
      mIgnoreXULPosition = true;
    }
    if (aSize) {
      mIgnoreXULSize = true;
      mIgnoreXULSizeMode = true;
    }
    return NS_OK;
  }

  PersistentAttributesDirty(dirtyAttributes, Sync);
  return NS_OK;
}

nsresult AppWindow::CenterImpl(nsIAppWindow* aRelative, bool aScreen,
                               bool aAlert, bool aAllowCenteringForSizeChange) {
  DesktopIntRect rect;
  bool screenCoordinates = false, windowCoordinates = false;
  nsresult result;

  if (!mChromeLoaded) {
    // note we lose the parameters. at time of writing, this isn't a problem.
    mCenterAfterLoad = true;
    return NS_OK;
  }

  if (!aScreen && !aRelative) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIScreenManager> screenmgr =
      do_GetService("@mozilla.org/gfx/screenmanager;1", &result);
  if (NS_FAILED(result)) {
    return result;
  }

  nsCOMPtr<nsIScreen> screen;

  if (aRelative) {
    nsCOMPtr<nsIBaseWindow> base(do_QueryInterface(aRelative));
    if (base) {
      rect = RoundedToInt(base->GetPositionAndSize() /
                          base->DevicePixelsPerDesktopPixel());
      // if centering on screen, convert that to the corresponding screen
      if (aScreen) {
        screen = screenmgr->ScreenForRect(rect);
      } else {
        windowCoordinates = true;
      }
    }
  }
  if (!aRelative) {
    if (!mOpenerScreenRect.IsEmpty()) {
      screen = screenmgr->ScreenForRect(mOpenerScreenRect);
    } else {
      screenmgr->GetPrimaryScreen(getter_AddRefs(screen));
    }
  }

  if (aScreen && screen) {
    rect = screen->GetAvailRectDisplayPix();
    screenCoordinates = true;
  }

  if (!screenCoordinates && !windowCoordinates) {
    return NS_ERROR_FAILURE;
  }

  NS_ASSERTION(mWindow, "what, no window?");
  const LayoutDeviceIntSize ourDevSize = GetSize();
  const DesktopIntSize ourSize =
      RoundedToInt(ourDevSize / DevicePixelsPerDesktopPixel());
  auto newPos =
      rect.TopLeft() +
      DesktopIntPoint((rect.width - ourSize.width) / 2,
                      (rect.height - ourSize.height) / (aAlert ? 3 : 2));
  if (windowCoordinates) {
    mWindow->ConstrainPosition(newPos);
  }

  SetPositionDesktopPix(newPos.x, newPos.y);

  // If moving the window caused it to change size, re-do the centering.
  // Allow only one recursion here.
  if (GetSize() != ourDevSize && aAllowCenteringForSizeChange) {
    return CenterImpl(aRelative, aScreen, aAlert,
                      /* aAllowCenteringForSizeChange */ false);
  }
  return NS_OK;
}

NS_IMETHODIMP AppWindow::Center(nsIAppWindow* aRelative, bool aScreen,
                                bool aAlert) {
  return CenterImpl(aRelative, aScreen, aAlert,
                    /* aAllowCenteringForSizeChange */ true);
}

NS_IMETHODIMP AppWindow::GetParentWidget(nsIWidget** aParentWidget) {
  NS_ENSURE_ARG_POINTER(aParentWidget);
  NS_ENSURE_STATE(mWindow);

  NS_IF_ADDREF(*aParentWidget = mWindow->GetParent());
  return NS_OK;
}

NS_IMETHODIMP AppWindow::SetParentWidget(nsIWidget* aParentWidget) {
  // XXX First Check In
  NS_ASSERTION(false, "Not Yet Implemented");
  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetNativeHandle(nsAString& aNativeHandle) {
  if (mWindow) {
    nativeWindow nativeWindowPtr = mWindow->GetNativeData(NS_NATIVE_WINDOW);
    /* the nativeWindow pointer is converted to and exposed as a string. This
       is a more reliable way not to lose information (as opposed to JS
       |Number| for instance) */
    aNativeHandle =
        NS_ConvertASCIItoUTF16(nsPrintfCString("0x%p", nativeWindowPtr));
  }
  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetVisibility(bool* aVisibility) {
  NS_ENSURE_ARG_POINTER(aVisibility);

  // Always claim to be visible for now. See bug
  // https://bugzilla.mozilla.org/show_bug.cgi?id=306245.

  *aVisibility = true;

  return NS_OK;
}

NS_IMETHODIMP AppWindow::SetVisibility(bool aVisibility) {
  if (!mChromeLoaded) {
    mShowAfterLoad = aVisibility;
    return NS_OK;
  }

  if (mDebuting) {
    return NS_OK;
  }

  NS_ENSURE_STATE(mDocShell);

  mDebuting = true;  // (Show / Focus is recursive)

  // XXXTAB Do we really need to show docshell and the window?  Isn't
  // the window good enough?
  mDocShell->SetVisibility(aVisibility);
  // Store locally so it doesn't die on us. 'Show' can result in the window
  // being closed with AppWindow::Destroy being called. That would set
  // mWindow to null and possibly destroy the nsIWidget while its Show method
  // is on the stack. We need to keep it alive until Show finishes.
  nsCOMPtr<nsIWidget> window = mWindow;
  window->Show(aVisibility);

  // NOTE(emilio): A bit hacky, but we need to synchronously trigger resizes
  // for remote frames here if we're a sized popup (mDominantClientSize=true).
  //
  // This is because what we do to show a popup window with a specified size is
  // to wait until the chrome loads (and gets sized, and thus laid out at a
  // particular pre-size), then size the window, and call Show(), which ends up
  // here.
  //
  // After bug 1917458, that remote browser resize would happen asynchronously,
  // which means content might be able to observe the old size unexpectedly.
  if (aVisibility && mDominantClientSize) {
    if (RefPtr doc = mDocShell->GetDocument()) {
      doc->SynchronouslyUpdateRemoteBrowserDimensions();
    }
  }

  nsCOMPtr<nsIWindowMediator> windowMediator(
      do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));
  if (windowMediator)
    windowMediator->UpdateWindowTimeStamp(static_cast<nsIAppWindow*>(this));

  // notify observers so that we can hide the splash screen if possible
  nsCOMPtr<nsIObserverService> obssvc = services::GetObserverService();
  NS_ASSERTION(obssvc, "Couldn't get observer service.");
  if (obssvc) {
    obssvc->NotifyObservers(static_cast<nsIAppWindow*>(this),
                            "xul-window-visible", nullptr);
  }

  mDebuting = false;
  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetEnabled(bool* aEnabled) {
  NS_ENSURE_ARG_POINTER(aEnabled);

  if (mWindow) {
    *aEnabled = mWindow->IsEnabled();
    return NS_OK;
  }

  *aEnabled = true;  // better guess than most
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP AppWindow::SetEnabled(bool aEnable) {
  if (mWindow) {
    mWindow->Enable(aEnable);
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP AppWindow::GetMainWidget(nsIWidget** aMainWidget) {
  NS_ENSURE_ARG_POINTER(aMainWidget);
  NS_IF_ADDREF(*aMainWidget = mWindow);
  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetTitle(nsAString& aTitle) {
  aTitle = mTitle;
  return NS_OK;
}

NS_IMETHODIMP AppWindow::SetTitle(const nsAString& aTitle) {
  NS_ENSURE_STATE(mWindow);
  mTitle.Assign(aTitle);
  mTitle.StripCRLF();
  NS_ENSURE_SUCCESS(mWindow->SetTitle(mTitle), NS_ERROR_FAILURE);
  return NS_OK;
}

//*****************************************************************************
// AppWindow: Helpers
//*****************************************************************************

NS_IMETHODIMP AppWindow::EnsureChromeTreeOwner() {
  if (mChromeTreeOwner) return NS_OK;

  mChromeTreeOwner = new nsChromeTreeOwner();
  NS_ADDREF(mChromeTreeOwner);
  mChromeTreeOwner->AppWindow(this);

  return NS_OK;
}

NS_IMETHODIMP AppWindow::EnsureContentTreeOwner() {
  if (mContentTreeOwner) return NS_OK;

  mContentTreeOwner = new nsContentTreeOwner(false);
  NS_ADDREF(mContentTreeOwner);
  mContentTreeOwner->AppWindow(this);

  return NS_OK;
}

NS_IMETHODIMP AppWindow::EnsurePrimaryContentTreeOwner() {
  if (mPrimaryContentTreeOwner) return NS_OK;

  mPrimaryContentTreeOwner = new nsContentTreeOwner(true);
  NS_ADDREF(mPrimaryContentTreeOwner);
  mPrimaryContentTreeOwner->AppWindow(this);

  return NS_OK;
}

NS_IMETHODIMP AppWindow::EnsurePrompter() {
  if (mPrompter) return NS_OK;

  nsCOMPtr<mozIDOMWindowProxy> ourWindow;
  nsresult rv = GetWindowDOMWindow(getter_AddRefs(ourWindow));
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIWindowWatcher> wwatch =
        do_GetService(NS_WINDOWWATCHER_CONTRACTID);
    if (wwatch) wwatch->GetNewPrompter(ourWindow, getter_AddRefs(mPrompter));
  }
  return mPrompter ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP AppWindow::EnsureAuthPrompter() {
  if (mAuthPrompter) return NS_OK;

  nsCOMPtr<mozIDOMWindowProxy> ourWindow;
  nsresult rv = GetWindowDOMWindow(getter_AddRefs(ourWindow));
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIWindowWatcher> wwatch(
        do_GetService(NS_WINDOWWATCHER_CONTRACTID));
    if (wwatch)
      wwatch->GetNewAuthPrompter(ourWindow, getter_AddRefs(mAuthPrompter));
  }
  return mAuthPrompter ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP AppWindow::GetAvailScreenSize(int32_t* aAvailWidth,
                                            int32_t* aAvailHeight) {
  nsCOMPtr<mozIDOMWindowProxy> domWindow;
  GetWindowDOMWindow(getter_AddRefs(domWindow));
  NS_ENSURE_STATE(domWindow);

  auto* window = nsGlobalWindowOuter::Cast(domWindow);

  RefPtr<nsScreen> screen = window->GetScreen();
  NS_ENSURE_STATE(screen);

  *aAvailWidth = screen->AvailWidth();
  *aAvailHeight = screen->AvailHeight();
  return NS_OK;
}

// Rounds window size to 1000x1000, or, if there isn't enough available
// screen space, to a multiple of 200x100.
NS_IMETHODIMP AppWindow::ForceRoundedDimensions() {
  if (mIsHiddenWindow) {
    return NS_OK;
  }

  CSSToLayoutDeviceScale scale = UnscaledDevicePixelsPerCSSPixel();

  CSSIntSize availSizeCSS;
  GetAvailScreenSize(&availSizeCSS.width, &availSizeCSS.height);

  // To get correct chrome size, we have to resize the window to a proper
  // size first. So, here, we size it to its available size.
  SetSpecifiedSize(availSizeCSS.width, availSizeCSS.height);

  // Get the current window size for calculating chrome UI size.
  CSSIntSize windowSizeCSS = RoundedToInt(GetSize() / scale);

  // Get the content size for calculating chrome UI size.
  LayoutDeviceIntSize contentSizeDev;
  GetPrimaryContentSize(&contentSizeDev.width, &contentSizeDev.height);
  CSSIntSize contentSizeCSS = RoundedToInt(contentSizeDev / scale);

  // Calculate the chrome UI size.
  CSSIntSize chromeSizeCSS = windowSizeCSS - contentSizeCSS;

  CSSIntSize targetSizeCSS;
  // Here, we use the available screen dimensions as the input dimensions to
  // force the window to be rounded as the maximum available content size.
  nsContentUtils::CalcRoundedWindowSizeForResistingFingerprinting(
      chromeSizeCSS.width, chromeSizeCSS.height, availSizeCSS.width,
      availSizeCSS.height, availSizeCSS.width, availSizeCSS.height,
      false,  // aSetOuterWidth
      false,  // aSetOuterHeight
      &targetSizeCSS.width, &targetSizeCSS.height);

  LayoutDeviceIntSize targetSizeDev = RoundedToInt(targetSizeCSS * scale);

  SetPrimaryContentSize(targetSizeDev.width, targetSizeDev.height);

  return NS_OK;
}

bool AppWindow::NeedsTooltipListener() {
  nsCOMPtr<dom::Element> docShellElement = GetWindowDOMElement();
  if (!docShellElement || docShellElement->IsXULElement()) {
    // Tooltips in XUL are handled by each element.
    return false;
  }
  // All other non-XUL document types need a tooltip listener.
  return true;
}

void AppWindow::AddTooltipSupport() {
  if (!NeedsTooltipListener()) {
    return;
  }
  nsXULTooltipListener* listener = nsXULTooltipListener::GetInstance();
  if (!listener) {
    return;
  }

  nsCOMPtr<dom::Element> docShellElement = GetWindowDOMElement();
  MOZ_ASSERT(docShellElement);
  listener->AddTooltipSupport(docShellElement);
}

void AppWindow::RemoveTooltipSupport() {
  if (!NeedsTooltipListener()) {
    return;
  }
  nsXULTooltipListener* listener = nsXULTooltipListener::GetInstance();
  if (!listener) {
    return;
  }

  nsCOMPtr<dom::Element> docShellElement = GetWindowDOMElement();
  MOZ_ASSERT(docShellElement);
  listener->RemoveTooltipSupport(docShellElement);
}

static Maybe<int32_t> ReadIntAttribute(const Element& aElement,
                                       nsAtom* aPrimary,
                                       nsAtom* aSecondary = nullptr) {
  nsAutoString attrString;
  if (!aElement.GetAttr(aPrimary, attrString)) {
    if (aSecondary) {
      return ReadIntAttribute(aElement, aSecondary);
    }
    return Nothing();
  }

  nsresult res = NS_OK;
  int32_t ret = attrString.ToInteger(&res);
  return NS_SUCCEEDED(res) ? Some(ret) : Nothing();
}

// If aSpecWidth and/or aSpecHeight are > 0, we will use these CSS px sizes
// to fit to the screen when staggering windows; if they're negative,
// we use the window's current size instead.
bool AppWindow::LoadPositionFromXUL(int32_t aSpecWidth, int32_t aSpecHeight) {
  // if we're the hidden window, don't try to validate our size/position. We're
  // special.
  if (mIsHiddenWindow) {
    return false;
  }

  // If we're not in the normal sizemode, don't move the window around.
  if (mWindow->SizeMode() != nsSizeMode_Normal) {
    return false;
  }

  RefPtr<dom::Element> root = GetWindowDOMElement();
  NS_ENSURE_TRUE(root, false);

  const LayoutDeviceIntRect devRect = GetPositionAndSize();

  // Convert to global display pixels for consistent window management across
  // screens with diverse resolutions
  const DesktopIntPoint curPoint =
      RoundedToInt(devRect.TopLeft() / DevicePixelsPerDesktopPixel());

  // For size, use specified value if > 0, else current value
  CSSIntSize cssSize(aSpecWidth, aSpecHeight);
  {
    CSSIntSize currentSize =
        RoundedToInt(devRect.Size() / UnscaledDevicePixelsPerCSSPixel());
    if (aSpecHeight <= 0) {
      cssSize.height = currentSize.height;
    }
    if (aSpecWidth <= 0) {
      cssSize.width = currentSize.width;
    }
  }

  // Obtain the position information from the <xul:window> element.
  DesktopIntPoint specPoint = curPoint;
  bool gotPosition = false;

  // Also read lowercase screenx/y because the front-end sometimes sets these
  // via setAttribute on HTML documents like about:blank, and stuff gets
  // lowercased.
  //
  // TODO(emilio): We should probably rename screenX/Y to screen-x/y to
  // prevent this impedance mismatch.
  if (auto attr =
          ReadIntAttribute(*root, nsGkAtoms::screenX, nsGkAtoms::screenx)) {
    specPoint.x = *attr;
    gotPosition = true;
  }

  if (auto attr =
          ReadIntAttribute(*root, nsGkAtoms::screenY, nsGkAtoms::screeny)) {
    specPoint.y = *attr;
    gotPosition = true;
  }

  if (gotPosition) {
    // Our position will be relative to our parent, if any
    nsCOMPtr<nsIBaseWindow> parent(do_QueryReferent(mParentWindow));
    if (parent) {
      const DesktopIntPoint parentPos = RoundedToInt(
          parent->GetPosition() / parent->DevicePixelsPerDesktopPixel());
      specPoint += parentPos;
    } else {
      StaggerPosition(specPoint.x.value, specPoint.y.value, cssSize.width,
                      cssSize.height);
    }
  }
  mWindow->ConstrainPosition(specPoint);
  if (specPoint != curPoint) {
    SetPositionDesktopPix(specPoint.x, specPoint.y);
  }
  return gotPosition;
}

static Maybe<int32_t> ReadSize(const Element& aElement, nsAtom* aAttr,
                               nsAtom* aMinAttr, nsAtom* aMaxAttr) {
  Maybe<int32_t> attr = ReadIntAttribute(aElement, aAttr);
  if (!attr) {
    return Nothing();
  }

  int32_t min =
      std::max(100, ReadIntAttribute(aElement, aMinAttr).valueOr(100));
  int32_t max = ReadIntAttribute(aElement, aMaxAttr)
                    .valueOr(std::numeric_limits<int32_t>::max());

  return Some(std::clamp(*attr, min, max));
}

bool AppWindow::LoadSizeFromXUL(int32_t& aSpecWidth, int32_t& aSpecHeight) {
  bool gotSize = false;

  // if we're the hidden window, don't try to validate our size/position. We're
  // special.
  if (mIsHiddenWindow) {
    return false;
  }

  nsCOMPtr<dom::Element> windowElement = GetWindowDOMElement();
  NS_ENSURE_TRUE(windowElement, false);

  // Obtain the sizing information from the <xul:window> element.
  aSpecWidth = 100;
  aSpecHeight = 100;

  if (auto width = ReadSize(*windowElement, nsGkAtoms::width,
                            nsGkAtoms::minwidth, nsGkAtoms::maxwidth)) {
    aSpecWidth = *width;
    gotSize = true;
  }

  if (auto height = ReadSize(*windowElement, nsGkAtoms::height,
                             nsGkAtoms::minheight, nsGkAtoms::maxheight)) {
    aSpecHeight = *height;
    gotSize = true;
  }

  return gotSize;
}

void AppWindow::SetSpecifiedSize(int32_t aSpecWidth, int32_t aSpecHeight) {
  // These are in CSS pixels of the main window.
  // TODO(emilio): In my testing we usually have a pres context around, can we
  // just use it? That'd simplify the coordinate calculations.
  {
    int32_t screenWidth;
    int32_t screenHeight;

    if (NS_SUCCEEDED(GetAvailScreenSize(&screenWidth, &screenHeight))) {
      if (aSpecWidth > screenWidth) {
        aSpecWidth = screenWidth;
      }
      if (aSpecHeight > screenHeight) {
        aSpecHeight = screenHeight;
      }
    }
  }

  NS_ASSERTION(mWindow, "we expected to have a window already");

  mIntrinsicallySized = false;

  // Convert specified values to device pixels, and resize
  auto newSize = RoundedToInt(CSSIntSize(aSpecWidth, aSpecHeight) *
                              UnscaledDevicePixelsPerCSSPixel());

  // Note: Because of the asynchronous resizing on Linux we have to call
  // SetSize even when the size doesn't appear to change. A previous call that
  // has yet to complete can still change the size. We want the latest call to
  // define the final size.
  SetSize(newSize.width, newSize.height, false);
}

/* Miscellaneous persistent attributes are attributes named in the
   |persist| attribute, other than size and position. Those are special
   because it's important to load those before one of the misc
   attributes (sizemode) and they require extra processing. */
bool AppWindow::UpdateWindowStateFromMiscXULAttributes() {
  /* There are no misc attributes of interest to the hidden window.
     It's especially important not to try to validate that window's
     size or position, because some platforms (Mac OS X) need to
     make it visible and offscreen. */
  if (mIsHiddenWindow) {
    return false;
  }

  nsCOMPtr<dom::Element> windowElement = GetWindowDOMElement();
  NS_ENSURE_TRUE(windowElement, false);

  nsAutoString stateString;
  nsSizeMode sizeMode = nsSizeMode_Normal;

  // If we are told to ignore the size mode attribute, force
  // normal sizemode.
  if (mIgnoreXULSizeMode) {
    windowElement->SetAttr(nsGkAtoms::sizemode, SIZEMODE_NORMAL,
                           IgnoreErrors());
  } else {
    // Otherwise, read sizemode from DOM and, if the window is resizable,
    // set it later.
    windowElement->GetAttr(nsGkAtoms::sizemode, stateString);
    if ((stateString.Equals(SIZEMODE_MAXIMIZED) ||
         stateString.Equals(SIZEMODE_FULLSCREEN))) {
      /* Honor request to maximize only if the window is sizable.
         An unsizable, unmaximizable, yet maximized window confuses
         Windows OS and is something of a travesty, anyway. */
      if (mChromeFlags & nsIWebBrowserChrome::CHROME_WINDOW_RESIZE) {
        mIntrinsicallySized = false;

        sizeMode = stateString.Equals(SIZEMODE_MAXIMIZED)
                       ? nsSizeMode_Maximized
                       : nsSizeMode_Fullscreen;
      }
    }
  }

  if (sizeMode == nsSizeMode_Fullscreen) {
    nsCOMPtr<mozIDOMWindowProxy> ourWindow;
    GetWindowDOMWindow(getter_AddRefs(ourWindow));
    auto* piWindow = nsPIDOMWindowOuter::From(ourWindow);
    piWindow->SetFullScreen(true);
  } else {
    // For maximized windows, ignore the XUL size and position attributes,
    // as setting them would set the window back to normal sizemode.
    if (sizeMode == nsSizeMode_Maximized) {
      mIgnoreXULSize = true;
      mIgnoreXULPosition = true;
    }
    mWindow->SetSizeMode(sizeMode);
  }
  return true;
}

/* Stagger windows of the same type so they don't appear on top of each other.
   This code does have a scary double loop -- it'll keep passing through
   the entire list of open windows until it finds a non-collision. Doesn't
   seem to be a problem, but it deserves watching.
   The aRequested{X,Y} parameters here are in desktop pixels;
   the aSpec{Width,Height} parameters are CSS pixel dimensions.
*/
void AppWindow::StaggerPosition(int32_t& aRequestedX, int32_t& aRequestedY,
                                int32_t aSpecWidth, int32_t aSpecHeight) {
  // These "constants" will be converted from CSS to desktop pixels
  // for the appropriate screen, assuming we find a screen to use...
  // hence they're not actually declared const here.
  int32_t kOffset = 22;
  uint32_t kSlop = 4;

  bool keepTrying;
  int bouncedX = 0,  // bounced off vertical edge of screen
      bouncedY = 0;  // bounced off horizontal edge

  // look for any other windows of this type
  nsCOMPtr<nsIWindowMediator> wm(do_GetService(NS_WINDOWMEDIATOR_CONTRACTID));
  if (!wm) return;

  nsCOMPtr<dom::Element> windowElement = GetWindowDOMElement();
  if (!windowElement) return;

  nsCOMPtr<nsIAppWindow> ourAppWindow(this);

  nsAutoString windowType;
  windowElement->GetAttr(nsGkAtoms::windowtype, windowType);

  DesktopIntRect screenRect;
  bool gotScreen = false;

  {  // fetch screen coordinates
    nsCOMPtr<nsIScreenManager> screenMgr(
        do_GetService("@mozilla.org/gfx/screenmanager;1"));
    if (screenMgr) {
      nsCOMPtr<nsIScreen> ourScreen;
      // The coordinates here are already display pixels
      // XXX aSpecWidth and aSpecHeight are CSS pixels!
      screenMgr->ScreenForRect(aRequestedX, aRequestedY, aSpecWidth,
                               aSpecHeight, getter_AddRefs(ourScreen));
      if (ourScreen) {
        screenRect = ourScreen->GetAvailRectDisplayPix();

        // Get the screen's scaling factors and convert staggering constants
        // from CSS px to desktop pixel units
        auto scale = ourScreen->GetCSSToDesktopScale();
        kOffset = (CSSCoord(kOffset) * scale).Rounded();
        kSlop = (CSSCoord(kSlop) * scale).Rounded();
        // Convert dimensions from CSS to desktop pixels
        aSpecWidth = (CSSCoord(aSpecWidth) * scale).Rounded();
        aSpecHeight = (CSSCoord(aSpecHeight) * scale).Rounded();
        gotScreen = true;
      }
    }
  }

  // One full pass through all windows of this type, repeat until no collisions.
  do {
    keepTrying = false;
    nsCOMPtr<nsISimpleEnumerator> windowList;
    wm->GetAppWindowEnumerator(windowType.get(), getter_AddRefs(windowList));

    if (!windowList) break;

    // One full pass through all windows of this type, offset and stop on
    // collision.
    do {
      bool more;
      windowList->HasMoreElements(&more);
      if (!more) break;

      nsCOMPtr<nsISupports> supportsWindow;
      windowList->GetNext(getter_AddRefs(supportsWindow));

      nsCOMPtr<nsIAppWindow> listAppWindow(do_QueryInterface(supportsWindow));
      if (listAppWindow != ourAppWindow) {
        int32_t listX, listY;
        nsCOMPtr<nsIBaseWindow> listBaseWindow(
            do_QueryInterface(supportsWindow));
        listBaseWindow->GetPosition(&listX, &listY);
        double scale;
        if (NS_SUCCEEDED(
                listBaseWindow->GetDevicePixelsPerDesktopPixel(&scale))) {
          listX = NSToIntRound(listX / scale);
          listY = NSToIntRound(listY / scale);
        }

        if (Abs(listX - aRequestedX) <= kSlop &&
            Abs(listY - aRequestedY) <= kSlop) {
          // collision! offset and start over
          if (bouncedX & 0x1)
            aRequestedX -= kOffset;
          else
            aRequestedX += kOffset;
          aRequestedY += kOffset;

          if (gotScreen) {
            // if we're moving to the right and we need to bounce...
            if (!(bouncedX & 0x1) &&
                ((aRequestedX + aSpecWidth) > screenRect.XMost())) {
              aRequestedX = screenRect.XMost() - aSpecWidth;
              ++bouncedX;
            }

            // if we're moving to the left and we need to bounce...
            if ((bouncedX & 0x1) && aRequestedX < screenRect.X()) {
              aRequestedX = screenRect.X();
              ++bouncedX;
            }

            // if we hit the bottom then bounce to the top
            if (aRequestedY + aSpecHeight > screenRect.YMost()) {
              aRequestedY = screenRect.Y();
              ++bouncedY;
            }
          }

          /* loop around again,
             but it's time to give up once we've covered the screen.
             there's a potential infinite loop with lots of windows. */
          keepTrying = bouncedX < 2 || bouncedY == 0;
          break;
        }
      }
    } while (true);
  } while (keepTrying);
}

void AppWindow::SyncAttributesToWidget() {
  nsCOMPtr<dom::Element> windowElement = GetWindowDOMElement();
  if (!windowElement) return;

  MOZ_DIAGNOSTIC_ASSERT(mWindow, "No widget on SyncAttributesToWidget?");

  nsAutoString attr;

  // Some attributes can change the client size (e.g. customtitlebar on Windows
  // and MacOS). But we might want to keep it.
  const LayoutDeviceIntSize oldClientSize = mWindow->GetClientSize();
  // We have to check now whether we want to restore the client size, as any
  // change in size will reset its state.
  bool maintainClientSize = mDominantClientSize;

  // "hidechrome" attribute
  // FIXME(emilio): This should arguably be
  // HideWindowChrome(windowElement->GetBoolAttr(...)), but that has
  // side-effects in some platforms.
  if (windowElement->GetBoolAttr(nsGkAtoms::hidechrome)) {
    mWindow->HideWindowChrome(true);
  }
  NS_ENSURE_TRUE_VOID(mWindow);

  // "customtitlebar" attribute
  // FIXME(emilio): This should arguably be
  // SetCustomTitlebar(windowElement->GetBoolAttr(...)), but that breaks with
  // the early blank window which sets the custom titlebar via
  // nsIDOMWindowUtils...
  if (windowElement->GetBoolAttr(nsGkAtoms::customtitlebar)) {
    mWindow->SetCustomTitlebar(true);
  }

  NS_ENSURE_TRUE_VOID(mWindow);

  mWindow->SetMicaBackdrop(windowElement->GetBoolAttr(nsGkAtoms::windowsmica));
  NS_ENSURE_TRUE_VOID(mWindow);

  // "windowtype", "windowclass", "windowname" attributes
  nsAutoString windowClassAttr, windowNameAttr;
  windowElement->GetAttr(nsGkAtoms::windowtype, attr);
  windowElement->GetAttribute(u"windowclass"_ns, windowClassAttr);
  windowElement->GetAttribute(u"windowname"_ns, windowNameAttr);
  mWindow->SetWindowClass(attr, windowClassAttr, windowNameAttr);

  NS_ENSURE_TRUE_VOID(mWindow);

  // Only change blank window status once we're loaded, so that a
  // partially-loaded browser window doesn't start painting early.
  if (mChromeLoaded) {
    mWindow->SetIsEarlyBlankWindow(attr.EqualsLiteral("navigator:blank"));
    NS_ENSURE_TRUE_VOID(mWindow);
  }

  // "icon" attribute
  windowElement->GetAttribute(u"icon"_ns, attr);
  if (!attr.IsEmpty()) {
    mWindow->SetIcon(attr);
    NS_ENSURE_TRUE_VOID(mWindow);
  }

  // "hidetitlebarseparator" attribute
  mWindow->SetHideTitlebarSeparator(
      windowElement->GetBoolAttr(nsGkAtoms::hidetitlebarseparator));
  NS_ENSURE_TRUE_VOID(mWindow);

  // "macnativefullscreen" attribute. Only override the creation-time default
  // when the attribute is actually present; absence means "use the
  // window-creation default" (set in nsCocoaWindow::CreateNativeWindow), not
  // "force off". Read the attribute value as a boolean so an explicit
  // macnativefullscreen="false" correctly turns native fullscreen off, rather
  // than just turning it on the way HasAttribute would (bug 2038980).
  if (windowElement->HasAttribute(u"macnativefullscreen"_ns)) {
    nsAutoString value;
    windowElement->GetAttribute(u"macnativefullscreen"_ns, value);
    mWindow->SetSupportsNativeFullscreen(!value.EqualsLiteral("false"));
    NS_ENSURE_TRUE_VOID(mWindow);
  }

  // "macanimationtype" attribute
  windowElement->GetAttribute(u"macanimationtype"_ns, attr);
  if (attr.EqualsLiteral("document")) {
    mWindow->SetWindowAnimationType(nsIWidget::eDocumentWindowAnimation);
  }

  // Check if the client size did change and if we want to restore it.
  if (maintainClientSize && mWindow->SizeMode() == nsSizeMode_Normal &&
      oldClientSize != mWindow->GetClientSize()) {
    mWindow->ResizeClient(oldClientSize / mWindow->GetDesktopToDeviceScale(),
                          true);
    mDominantClientSize = true;
  }
}

enum class ConversionDirection {
  InnerToOuter,
  OuterToInner,
};

static void ConvertWindowSize(nsIAppWindow* aWin, const nsAtom* aAttr,
                              ConversionDirection aDirection,
                              nsAString& aInOutString) {
  MOZ_ASSERT(aWin);
  MOZ_ASSERT(aAttr == nsGkAtoms::width || aAttr == nsGkAtoms::height);

  nsresult rv;
  int32_t size = aInOutString.ToInteger(&rv);
  if (NS_FAILED(rv)) {
    return;
  }

  int32_t sizeDiff = aAttr == nsGkAtoms::width
                         ? aWin->GetOuterToInnerWidthDifferenceInCSSPixels()
                         : aWin->GetOuterToInnerHeightDifferenceInCSSPixels();

  if (!sizeDiff) {
    return;
  }

  int32_t multiplier = aDirection == ConversionDirection::InnerToOuter ? 1 : -1;

  CopyASCIItoUTF16(nsPrintfCString("%d", size + multiplier * sizeDiff),
                   aInOutString);
}

nsresult AppWindow::GetPersistentValue(const nsAtom* aAttr, nsAString& aValue) {
  if (!XRE_IsParentProcess()) {
    // The XULStore is only available in the parent process.
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<dom::Element> docShellElement = GetWindowDOMElement();
  if (!docShellElement) {
    return NS_ERROR_FAILURE;
  }

  nsAutoString windowElementId;
  docShellElement->GetId(windowElementId);
  // Elements must have an ID to be persisted.
  if (windowElementId.IsEmpty()) {
    return NS_OK;
  }

  RefPtr<dom::Document> ownerDoc = docShellElement->OwnerDoc();
  nsIURI* docURI = ownerDoc->GetDocumentURI();
  if (!docURI) {
    return NS_ERROR_FAILURE;
  }
  nsAutoCString utf8uri;
  nsresult rv = docURI->GetSpec(utf8uri);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ConvertUTF8toUTF16 uri(utf8uri);

  if (!mLocalStore) {
    mLocalStore = do_GetService("@mozilla.org/xul/xulstore;1");
    if (NS_WARN_IF(!mLocalStore)) {
      return NS_ERROR_NOT_INITIALIZED;
    }
  }

  rv = mLocalStore->GetValue(uri, windowElementId, nsDependentAtomString(aAttr),
                             aValue);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (aAttr == nsGkAtoms::width || aAttr == nsGkAtoms::height) {
    // Convert attributes from outer size to inner size for top-level
    // windows, see bug 1444525 & co.
    ConvertWindowSize(this, aAttr, ConversionDirection::OuterToInner, aValue);
  }

  return NS_OK;
}

nsresult AppWindow::GetDocXulStoreKeys(nsString& aUriSpec,
                                       nsString& aWindowElementId) {
  nsCOMPtr<dom::Element> docShellElement = GetWindowDOMElement();
  if (!docShellElement) {
    return NS_ERROR_FAILURE;
  }

  docShellElement->GetId(aWindowElementId);
  // Match the behavior of XULPersist and only persist values if the element
  // has an ID.
  if (aWindowElementId.IsEmpty()) {
    return NS_OK;
  }

  RefPtr<dom::Document> ownerDoc = docShellElement->OwnerDoc();
  nsIURI* docURI = ownerDoc->GetDocumentURI();
  if (!docURI) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString utf8uri;
  nsresult rv = docURI->GetSpec(utf8uri);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  aUriSpec = NS_ConvertUTF8toUTF16(utf8uri);

  return NS_OK;
}

nsresult AppWindow::MaybeSaveEarlyWindowPersistentValues(
    const LayoutDeviceIntRect& aRect) {
#ifdef XP_WIN
  nsAutoString uri;
  nsAutoString windowElementId;
  nsresult rv = GetDocXulStoreKeys(uri, windowElementId);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!windowElementId.EqualsLiteral("main-window") ||
      !uri.EqualsLiteral("chrome://browser/content/browser.xhtml")) {
    return NS_OK;
  }

  SkeletonUISettings settings;

  settings.screenX = aRect.X();
  settings.screenY = aRect.Y();
  settings.width = aRect.Width();
  settings.height = aRect.Height();

  settings.maximized = mWindow->SizeMode() == nsSizeMode_Maximized;
  settings.cssToDevPixelScaling = UnscaledDevicePixelsPerCSSPixel().scale;

  nsCOMPtr<dom::Element> windowElement = GetWindowDOMElement();
  Document* doc = windowElement->GetComposedDoc();
  Element* urlbarEl = doc->GetElementById(u"urlbar"_ns);

  nsCOMPtr<nsPIDOMWindowOuter> window = mDocShell->GetWindow();
  nsCOMPtr<nsIDOMWindowUtils> utils =
      nsGlobalWindowOuter::Cast(window)->WindowUtils();
  RefPtr<dom::DOMRect> urlbarRect;
  rv = utils->GetBoundsWithoutFlushing(urlbarEl, getter_AddRefs(urlbarRect));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  double urlbarX = urlbarRect->X();
  double urlbarWidth = urlbarRect->Width();

  // Hard-coding the following values and this behavior in general is rather
  // fragile, and can easily get out of sync with the actual front-end values.
  // This is not intended as a long-term solution, but only as the relatively
  // straightforward implementation of an experimental feature. If we want to
  // ship the skeleton UI to all users, we should strongly consider a more
  // robust solution than this. The vertical position of the urlbar will be
  // fixed.
  nsAutoString attributeValue;
  urlbarEl->GetAttribute(u"breakout-extend"_ns, attributeValue);
  // Scale down the urlbar if it is focused
  if (attributeValue.EqualsLiteral("true")) {
    // defined in browser.inc.css as 2px
    int urlbarBreakoutExtend = 2;
    // defined in urlbar-searchbar.inc.css as 5px
    int urlbarMarginInline = 5;

    // breakout-extend measurements are defined in urlbar-searchbar.inc.css
    urlbarX += (double)(urlbarBreakoutExtend + urlbarMarginInline);
    urlbarWidth -= (double)(2 * (urlbarBreakoutExtend + urlbarMarginInline));
  }
  CSSPixelSpan urlbar;
  urlbar.start = urlbarX;
  urlbar.end = urlbar.start + urlbarWidth;
  settings.urlbarSpan = urlbar;

  Element* navbar = doc->GetElementById(u"nav-bar"_ns);

  Element* searchbarEl = doc->GetElementById(u"searchbar"_ns);
  CSSPixelSpan searchbar;
  if (navbar->Contains(searchbarEl)) {
    RefPtr<dom::DOMRect> searchbarRect;
    rv = utils->GetBoundsWithoutFlushing(searchbarEl,
                                         getter_AddRefs(searchbarRect));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    searchbar.start = searchbarRect->X();
    searchbar.end = searchbar.start + searchbarRect->Width();
  } else {
    // There is no searchbar in the UI
    searchbar.start = 0;
    searchbar.end = 0;
  }
  settings.searchbarSpan = searchbar;

  nsAutoString bookmarksVisibility;
  Preferences::GetString("browser.toolbars.bookmarks.visibility",
                         bookmarksVisibility);
  settings.bookmarksToolbarShown =
      bookmarksVisibility.EqualsLiteral("always") ||
      bookmarksVisibility.EqualsLiteral("newtab");

  Element* menubar = doc->GetElementById(u"toolbar-menubar"_ns);
  menubar->GetAttribute(u"autohide"_ns, attributeValue);
  settings.menubarShown = attributeValue.EqualsLiteral("false");

  ErrorResult err;
  RefPtr<dom::HTMLCollection> toolbarSprings = navbar->GetElementsByTagNameNS(
      u"http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul"_ns,
      u"toolbarspring"_ns, err);
  if (err.Failed()) {
    return NS_ERROR_FAILURE;
  }
  mozilla::Vector<CSSPixelSpan> springs;
  for (size_t i = 0; i < toolbarSprings->Length(); i++) {
    RefPtr<Element> springEl = toolbarSprings->Item(i);
    RefPtr<dom::DOMRect> springRect;
    rv = utils->GetBoundsWithoutFlushing(springEl, getter_AddRefs(springRect));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    CSSPixelSpan spring;
    spring.start = springRect->X();
    spring.end = spring.start + springRect->Width();
    if (!settings.springs.append(spring)) {
      return NS_ERROR_FAILURE;
    }
  }

  settings.rtlEnabled = intl::LocaleService::GetInstance()->IsAppLocaleRTL();

  bool isInTabletMode = false;
  bool const autoTouchModePref =
      Preferences::GetBool("browser.touchmode.auto", false);
  if (autoTouchModePref) {
    nsCOMPtr<nsIWindowsUIUtils> uiUtils(
        do_GetService("@mozilla.org/windows-ui-utils;1"));
    if (!NS_WARN_IF(!uiUtils)) {
      // We switch to the touch-optimized layout in both Win10 and Win11 tablet-
      // modes, since only the input mechanism is relevant. (See bug 1819421.)
      if (IsWin11OrLater()) {
        uiUtils->GetInWin11TabletMode(&isInTabletMode);
      } else {
        uiUtils->GetInWin10TabletMode(&isInTabletMode);
      }
    }
  }

  if (isInTabletMode) {
    settings.uiDensity = SkeletonUIDensity::Touch;
  } else {
    int uiDensityPref = Preferences::GetInt("browser.uidensity", 0);
    switch (uiDensityPref) {
      case 0: {
        settings.uiDensity = SkeletonUIDensity::Default;
        break;
      }
      case 1: {
        settings.uiDensity = SkeletonUIDensity::Compact;
        break;
      }
      case 2: {
        settings.uiDensity = SkeletonUIDensity::Touch;
        break;
      }
    }
  }

  settings.verticalTabs = Preferences::GetBool("sidebar.verticalTabs", false);

  (void)PersistPreXULSkeletonUIValues(settings);
#endif

  return NS_OK;
}

nsresult AppWindow::SetPersistentValue(const nsAtom* aAttr,
                                       const nsAString& aValue) {
  if (!XRE_IsParentProcess()) {
    // The XULStore is only available in the parent process.
    return NS_ERROR_UNEXPECTED;
  }

  nsAutoString uri;
  nsAutoString windowElementId;
  nsresult rv = GetDocXulStoreKeys(uri, windowElementId);

  if (NS_FAILED(rv) || windowElementId.IsEmpty()) {
    return rv;
  }

  nsAutoString maybeConvertedValue(aValue);
  if (aAttr == nsGkAtoms::width || aAttr == nsGkAtoms::height) {
    // Make sure we store the <window> attributes as outer window size, see
    // bug 1444525 & co.
    ConvertWindowSize(this, aAttr, ConversionDirection::InnerToOuter,
                      maybeConvertedValue);
  }

  if (!mLocalStore) {
    mLocalStore = do_GetService("@mozilla.org/xul/xulstore;1");
    if (NS_WARN_IF(!mLocalStore)) {
      return NS_ERROR_NOT_INITIALIZED;
    }
  }

  return mLocalStore->SetValue(
      uri, windowElementId, nsDependentAtomString(aAttr), maybeConvertedValue);
}

void AppWindow::MaybeSavePersistentPositionAndSize(
    PersistentAttributes aAttributes, Element& aRootElement,
    const nsAString& aPersistString, bool aShouldPersist) {
  if ((aAttributes & PersistentAttributes{PersistentAttribute::Position,
                                          PersistentAttribute::Size})
          .isEmpty()) {
    return;
  }

  // get our size, position and mode to persist
  LayoutDeviceIntRect rect;
  if (NS_FAILED(mWindow->GetRestoredBounds(rect))) {
    return;
  }

  const bool isClient = mWindow->PersistClientBounds();

  // we use CSS pixels for size, but desktop pixels for position
  CSSToLayoutDeviceScale sizeScale = UnscaledDevicePixelsPerCSSPixel();
  DesktopToLayoutDeviceScale posScale = DevicePixelsPerDesktopPixel();

  // make our position relative to our parent, if any
  nsCOMPtr<nsIBaseWindow> parent(do_QueryReferent(mParentWindow));
  if (parent) {
    int32_t parentX, parentY;
    if (NS_SUCCEEDED(parent->GetPosition(&parentX, &parentY))) {
      rect.MoveBy(-parentX, -parentY);
    }
  }

  nsAutoString sizeString;
  // (only for size elements which are persisted)
  if (aAttributes.contains(PersistentAttribute::Position)) {
    if (aPersistString.Find(u"screenX") >= 0) {
      sizeString.Truncate();
      sizeString.AppendInt(NSToIntRound(rect.X() / posScale.scale));
      aRootElement.SetAttr(nsGkAtoms::screenX, sizeString, IgnoreErrors());
      if (aShouldPersist) {
        (void)SetPersistentValue(nsGkAtoms::screenX, sizeString);
      }
    }
    if (aPersistString.Find(u"screenY") >= 0) {
      sizeString.Truncate();
      sizeString.AppendInt(NSToIntRound(rect.Y() / posScale.scale));
      aRootElement.SetAttr(nsGkAtoms::screenY, sizeString, IgnoreErrors());
      if (aShouldPersist) {
        (void)SetPersistentValue(nsGkAtoms::screenY, sizeString);
      }
    }
  }

  if (aAttributes.contains(PersistentAttribute::Size)) {
    const LayoutDeviceIntRect innerRect =
        isClient ? rect : rect - GetOuterToInnerSizeDifference(mWindow);
    if (aPersistString.Find(u"width") >= 0) {
      sizeString.Truncate();
      sizeString.AppendInt(NSToIntRound(innerRect.Width() / sizeScale.scale));
      aRootElement.SetAttr(nsGkAtoms::width, sizeString, IgnoreErrors());
      if (aShouldPersist) {
        (void)SetPersistentValue(nsGkAtoms::width, sizeString);
      }
    }
    if (aPersistString.Find(u"height") >= 0) {
      sizeString.Truncate();
      sizeString.AppendInt(NSToIntRound(innerRect.Height() / sizeScale.scale));
      aRootElement.SetAttr(nsGkAtoms::height, sizeString, IgnoreErrors());
      if (aShouldPersist) {
        (void)SetPersistentValue(nsGkAtoms::height, sizeString);
      }
    }
  }

  (void)MaybeSaveEarlyWindowPersistentValues(rect);
}

void AppWindow::MaybeSavePersistentMiscAttributes(
    PersistentAttributes aAttributes, Element& aRootElement,
    const nsAString& aPersistString, bool aShouldPersist) {
  if (!aAttributes.contains(PersistentAttribute::Misc)) {
    return;
  }

  nsSizeMode sizeMode = mWindow->SizeMode();
  nsAutoString sizeString;
  if (sizeMode != nsSizeMode_Minimized) {
    if (sizeMode == nsSizeMode_Maximized) {
      sizeString.Assign(SIZEMODE_MAXIMIZED);
    } else if (sizeMode == nsSizeMode_Fullscreen) {
      sizeString.Assign(SIZEMODE_FULLSCREEN);
    } else {
      sizeString.Assign(SIZEMODE_NORMAL);
    }
    aRootElement.SetAttr(nsGkAtoms::sizemode, sizeString, IgnoreErrors());
    if (aShouldPersist && aPersistString.Find(u"sizemode") >= 0) {
      (void)SetPersistentValue(nsGkAtoms::sizemode, sizeString);
    }
  }
  aRootElement.SetBoolAttr(nsGkAtoms::tiled, mWindow->IsTiled());
}

void AppWindow::SavePersistentAttributes(
    const PersistentAttributes aAttributes) {
  // can happen when the persistence timer fires at an inopportune time
  // during window shutdown
  if (!mDocShell) {
    return;
  }

  nsCOMPtr<dom::Element> docShellElement = GetWindowDOMElement();
  if (!docShellElement) {
    return;
  }

  nsAutoString persistString;
  docShellElement->GetAttr(nsGkAtoms::persist, persistString);
  if (persistString.IsEmpty()) {  // quick check which sometimes helps
    mPersistentAttributesDirty.clear();
    return;
  }

  bool shouldPersist = mWindow->SizeMode() != nsSizeMode_Fullscreen;
  MaybeSavePersistentPositionAndSize(aAttributes, *docShellElement,
                                     persistString, shouldPersist);
  MaybeSavePersistentMiscAttributes(aAttributes, *docShellElement,
                                    persistString, shouldPersist);
  mPersistentAttributesDirty -= aAttributes;
}

NS_IMETHODIMP AppWindow::GetWindowDOMWindow(mozIDOMWindowProxy** aDOMWindow) {
  NS_ENSURE_STATE(mDocShell);

  if (!mDOMWindow) mDOMWindow = mDocShell->GetWindow();
  NS_ENSURE_TRUE(mDOMWindow, NS_ERROR_FAILURE);

  *aDOMWindow = mDOMWindow;
  NS_ADDREF(*aDOMWindow);
  return NS_OK;
}

dom::Element* AppWindow::GetWindowDOMElement() const {
  NS_ENSURE_TRUE(mDocShell, nullptr);

  nsCOMPtr<nsIDocumentViewer> viewer;
  mDocShell->GetDocViewer(getter_AddRefs(viewer));
  NS_ENSURE_TRUE(viewer, nullptr);

  const dom::Document* document = viewer->GetDocument();
  NS_ENSURE_TRUE(document, nullptr);

  return document->GetRootElement();
}

nsresult AppWindow::ContentShellAdded(nsIDocShellTreeItem* aContentShell,
                                      bool aPrimary) {
  // Set the default content tree owner
  if (aPrimary) {
    NS_ENSURE_SUCCESS(EnsurePrimaryContentTreeOwner(), NS_ERROR_FAILURE);
    aContentShell->SetTreeOwner(mPrimaryContentTreeOwner);
    mPrimaryContentShell = aContentShell;
    mPrimaryBrowserParent = nullptr;
  } else {
    NS_ENSURE_SUCCESS(EnsureContentTreeOwner(), NS_ERROR_FAILURE);
    aContentShell->SetTreeOwner(mContentTreeOwner);
    if (mPrimaryContentShell == aContentShell) mPrimaryContentShell = nullptr;
  }

  return NS_OK;
}

nsresult AppWindow::ContentShellRemoved(nsIDocShellTreeItem* aContentShell) {
  if (mPrimaryContentShell == aContentShell) {
    mPrimaryContentShell = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::GetPrimaryContentSize(int32_t* aWidth, int32_t* aHeight) {
  if (mPrimaryBrowserParent) {
    return GetPrimaryRemoteTabSize(aWidth, aHeight);
  }
  if (mPrimaryContentShell) {
    return GetPrimaryContentShellSize(aWidth, aHeight);
  }
  return NS_ERROR_UNEXPECTED;
}

nsresult AppWindow::GetPrimaryRemoteTabSize(int32_t* aWidth, int32_t* aHeight) {
  BrowserHost* host = BrowserHost::GetFrom(mPrimaryBrowserParent.get());
  // Need strong ref, since Client* can run script.
  RefPtr<dom::Element> element = host->GetOwnerElement();
  NS_ENSURE_STATE(element);

  CSSIntSize size(element->ClientWidth(), element->ClientHeight());
  LayoutDeviceIntSize sizeDev =
      RoundedToInt(size * UnscaledDevicePixelsPerCSSPixel());
  if (aWidth) {
    *aWidth = sizeDev.width;
  }
  if (aHeight) {
    *aHeight = sizeDev.height;
  }
  return NS_OK;
}

nsresult AppWindow::GetPrimaryContentShellSize(int32_t* aWidth,
                                               int32_t* aHeight) {
  NS_ENSURE_STATE(mPrimaryContentShell);

  nsCOMPtr<nsIBaseWindow> shellWindow(do_QueryInterface(mPrimaryContentShell));
  NS_ENSURE_STATE(shellWindow);

  LayoutDeviceIntSize sizeDev = shellWindow->GetSize();
  if (aWidth) {
    *aWidth = sizeDev.width;
  }
  if (aHeight) {
    *aHeight = sizeDev.height;
  }
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::SetPrimaryContentSize(int32_t aWidth, int32_t aHeight) {
  if (mPrimaryBrowserParent) {
    return SetPrimaryRemoteTabSize(aWidth, aHeight);
  }
  if (mPrimaryContentShell) {
    return SizeShellTo(mPrimaryContentShell, aWidth, aHeight);
  }
  return NS_ERROR_UNEXPECTED;
}

nsresult AppWindow::SetPrimaryRemoteTabSize(int32_t aWidth, int32_t aHeight) {
  int32_t shellWidth, shellHeight;
  GetPrimaryRemoteTabSize(&shellWidth, &shellHeight);
  SizeShellToWithLimit(aWidth, aHeight, shellWidth, shellHeight);
  return NS_OK;
}

nsresult AppWindow::GetRootShellSize(int32_t* aWidth, int32_t* aHeight) {
  NS_ENSURE_TRUE(mDocShell, NS_ERROR_FAILURE);
  return mDocShell->GetSize(aWidth, aHeight);
}

nsresult AppWindow::SetRootShellSize(int32_t aWidth, int32_t aHeight) {
  return SizeShellTo(mDocShell, aWidth, aHeight);
}

NS_IMETHODIMP AppWindow::SizeShellTo(nsIDocShellTreeItem* aShellItem,
                                     int32_t aCX, int32_t aCY) {
  MOZ_ASSERT(aShellItem == mDocShell || aShellItem == mPrimaryContentShell);
  if (aShellItem == mDocShell) {
    auto newSize =
        LayoutDeviceIntSize(aCX, aCY) + GetOuterToInnerSizeDifference(mWindow);
    SetSize(newSize.width, newSize.height, /* aRepaint = */ true);
    mDominantClientSize = true;
    return NS_OK;
  }

  // XXXTAB This is wrong, we should actually reflow based on the passed in
  // shell.  For now we are hacking and doing delta sizing.  This is bad
  // because it assumes all size we add will go to the shell which probably
  // won't happen.
  nsCOMPtr<nsIBaseWindow> shellAsWin(do_QueryInterface(aShellItem));
  NS_ENSURE_TRUE(shellAsWin, NS_ERROR_FAILURE);

  int32_t width = 0;
  int32_t height = 0;
  shellAsWin->GetSize(&width, &height);

  SizeShellToWithLimit(aCX, aCY, width, height);

  return NS_OK;
}

NS_IMETHODIMP AppWindow::ExitModalLoop(nsresult aStatus) {
  if (mContinueModalLoop) EnableParent(true);
  mContinueModalLoop = false;
  mModalStatus = aStatus;
  return NS_OK;
}

// top-level function to create a new window
NS_IMETHODIMP AppWindow::CreateNewWindow(int32_t aChromeFlags,
                                         nsIOpenWindowInfo* aOpenWindowInfo,
                                         nsIAppWindow** _retval) {
  NS_ENSURE_ARG_POINTER(_retval);

  // If a position change is pending (e.g. this window was just dragged to a
  // different display), flush it now so the new window reads our current
  // position from the XULStore rather than the stale one still queued behind
  // the SIZE_PERSISTENCE_TIMEOUT timer.
  if (mPersistentAttributesDirty.contains(PersistentAttribute::Position)) {
    PersistentAttributesDirty(PersistentAttribute::Position, Sync);
  }

  if (aChromeFlags & nsIWebBrowserChrome::CHROME_OPENAS_CHROME) {
    MOZ_RELEASE_ASSERT(
        !aOpenWindowInfo,
        "Unexpected nsOpenWindowInfo when creating a new chrome window");
    return CreateNewChromeWindow(aChromeFlags, _retval);
  }

  return CreateNewContentWindow(aChromeFlags, aOpenWindowInfo, _retval);
}

NS_IMETHODIMP AppWindow::CreateNewChromeWindow(int32_t aChromeFlags,
                                               nsIAppWindow** _retval) {
  nsCOMPtr<nsIAppShellService> appShell(
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
  NS_ENSURE_TRUE(appShell, NS_ERROR_FAILURE);

  // Just do a normal create of a window and return.
  nsCOMPtr<nsIAppWindow> newWindow;
  appShell->CreateTopLevelWindow(
      this, nullptr, aChromeFlags, nsIAppShellService::SIZE_TO_CONTENT,
      nsIAppShellService::SIZE_TO_CONTENT, getter_AddRefs(newWindow));

  NS_ENSURE_TRUE(newWindow, NS_ERROR_FAILURE);

  newWindow.forget(_retval);

  return NS_OK;
}

NS_IMETHODIMP AppWindow::CreateNewContentWindow(
    int32_t aChromeFlags, nsIOpenWindowInfo* aOpenWindowInfo,
    nsIAppWindow** _retval) {
  nsCOMPtr<nsIAppShellService> appShell(
      do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
  NS_ENSURE_TRUE(appShell, NS_ERROR_FAILURE);

  // We need to create a new top level window and then enter a nested
  // loop. Eventually the new window will be told that it has loaded,
  // at which time we know it is safe to spin out of the nested loop
  // and allow the opening code to proceed.

  nsCOMPtr<nsIURI> uri;
  nsAutoCString urlStr;
  urlStr.AssignLiteral(BROWSER_CHROME_URL_QUOTED);

  nsCOMPtr<nsIIOService> service(do_GetService(NS_IOSERVICE_CONTRACTID));
  if (service) {
    service->NewURI(urlStr, nullptr, nullptr, getter_AddRefs(uri));
  }
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);

  // We need to create a chrome window to contain the content window we're about
  // to pass back. The subject principal needs to be system while we're creating
  // it to make things work right, so force a system caller. See bug 799348
  // comment 13 for a description of what happens when we don't.
  nsCOMPtr<nsIAppWindow> newWindow;
  {
    AutoNoJSAPI nojsapi;
    appShell->CreateTopLevelWindow(this, uri, aChromeFlags, 615, 480,
                                   getter_AddRefs(newWindow));
    NS_ENSURE_TRUE(newWindow, NS_ERROR_FAILURE);
  }

  AppWindow* appWin =
      static_cast<AppWindow*>(static_cast<nsIAppWindow*>(newWindow));

  // Specify which flags should be used by browser.xhtml to create the initial
  // content browser window.
  appWin->mInitialOpenWindowInfo = aOpenWindowInfo;

  // Specify that we want the window to remain locked until the chrome has
  // loaded.
  appWin->LockUntilChromeLoad();

  {
    AutoNoJSAPI nojsapi;
    SpinEventLoopUntil("AppWindow::CreateNewContentWindow"_ns,
                       [&]() { return !appWin->IsLocked(); });
  }

  NS_ENSURE_STATE(appWin->mPrimaryContentShell ||
                  appWin->mPrimaryBrowserParent);
  MOZ_ASSERT_IF(appWin->mPrimaryContentShell,
                !aOpenWindowInfo->GetNextRemoteBrowser());

  newWindow.forget(_retval);

  return NS_OK;
}

NS_IMETHODIMP AppWindow::GetHasPrimaryContent(bool* aResult) {
  *aResult = mPrimaryBrowserParent || mPrimaryContentShell;
  return NS_OK;
}

void AppWindow::EnableParent(bool aEnable) {
  if (nsCOMPtr<nsIBaseWindow> parentWindow = do_QueryReferent(mParentWindow)) {
    if (nsCOMPtr<nsIWidget> parentWidget = parentWindow->GetMainWidget()) {
      parentWidget->Enable(aEnable);
    }
  }
}

void AppWindow::SetContentScrollbarVisibility(bool aVisible) {
  nsCOMPtr<nsPIDOMWindowOuter> contentWin(
      do_GetInterface(mPrimaryContentShell));
  if (!contentWin) {
    return;
  }

  nsContentUtils::SetScrollbarsVisibility(contentWin->GetDocShell(), aVisible);
}

void AppWindow::ApplyChromeFlags() {
  nsCOMPtr<dom::Element> root = GetWindowDOMElement();
  if (!root) {
    return;
  }

  if (mChromeLoaded) {
    // The two calls in this block don't need to happen early because they
    // don't cause a global restyle on the document.  Not only that, but the
    // scrollbar stuff needs a content area to toggle the scrollbars on anyway.
    // So just don't do these until mChromeLoaded is true.

    // Scrollbars have their own special treatment.
    SetContentScrollbarVisibility(mChromeFlags &
                                  nsIWebBrowserChrome::CHROME_SCROLLBARS);
  }

  /* the other flags are handled together. we have style rules
     in navigator.css that trigger visibility based on
     the 'chromehidden' attribute of the <window> tag. */
  nsAutoString newvalue;

  if (!(mChromeFlags & nsIWebBrowserChrome::CHROME_MENUBAR))
    newvalue.AppendLiteral("menubar ");

  if (!(mChromeFlags & nsIWebBrowserChrome::CHROME_TOOLBAR))
    newvalue.AppendLiteral("toolbar ");

  if (!(mChromeFlags & nsIWebBrowserChrome::CHROME_PERSONAL_TOOLBAR))
    newvalue.AppendLiteral("directories ");

  if (!(mChromeFlags & nsIWebBrowserChrome::CHROME_EXTRA))
    newvalue.AppendLiteral("extrachrome ");

  // Note that if we're not actually changing the value this will be a no-op,
  // so no need to compare to the old value.
  root->SetAttribute(u"chromehidden"_ns, newvalue, IgnoreErrors());

  if (mChromeFlags & nsIWebBrowserChrome::CHROME_NO_PERSISTENCE) {
    root->SetAttribute(u"persist"_ns, u""_ns, IgnoreErrors());
  }
}

NS_IMETHODIMP
AppWindow::BeforeStartLayout() {
  ApplyChromeFlags();
  // Ordering here is important, loading width/height values in
  // LoadPersistentWindowState() depends on the customtitlebar attribute (since
  // we need to translate outer to inner sizes).
  SyncAttributesToWidget();
  LoadPersistentWindowState();
  if (mWindow) {
    SizeShell();
  }
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::LockAspectRatio(bool aShouldLock) {
  mWindow->LockAspectRatio(aShouldLock);
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::NeedFastSnaphot() {
  MOZ_ASSERT(mWindow);
  if (!mWindow) {
    return NS_ERROR_FAILURE;
  }
  mWindow->SetNeedFastSnaphot();
  return NS_OK;
}

void AppWindow::LoadPersistentWindowState() {
  nsCOMPtr<dom::Element> docShellElement = GetWindowDOMElement();
  if (!docShellElement) {
    return;
  }

  // Disable state restoration, allowing the kiosk desktop environment
  // to manage state and position.
  if (StaticPrefs::browser_restoreWindowState_disabled()) {
    return;
  }

  // Check if the window wants to persist anything.
  nsAutoString persist;
  docShellElement->GetAttr(nsGkAtoms::persist, persist);
  if (persist.IsEmpty()) {
    return;
  }

  auto loadValue = [&](nsAtom* aAttr) {
    nsDependentAtomString attrString(aAttr);
    if (persist.Find(attrString) >= 0) {
      nsAutoString value;
      nsresult rv = GetPersistentValue(aAttr, value);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Failed to get persistent state.");
      if (NS_SUCCEEDED(rv) && !value.IsEmpty()) {
        docShellElement->SetAttr(aAttr, value, IgnoreErrors());
      }
    }
  };

  loadValue(nsGkAtoms::screenX);
  loadValue(nsGkAtoms::screenY);
  loadValue(nsGkAtoms::width);
  loadValue(nsGkAtoms::height);
  loadValue(nsGkAtoms::sizemode);
}

void AppWindow::IntrinsicallySizeShell(const CSSIntSize& aWindowDiff,
                                       int32_t& aSpecWidth,
                                       int32_t& aSpecHeight) {
  nsCOMPtr<nsIDocumentViewer> viewer;
  mDocShell->GetDocViewer(getter_AddRefs(viewer));
  if (!viewer) {
    return;
  }
  RefPtr<nsDocShell> docShell = mDocShell;

  CSSIntCoord maxWidth = 0;
  CSSIntCoord maxHeight = 0;
  CSSIntCoord prefWidth = 0;
  if (RefPtr element = GetWindowDOMElement()) {
    nsAutoString prefWidthAttr;
    if (element->GetAttr(nsGkAtoms::prefwidth, prefWidthAttr)) {
      // TODO: Make this more generic perhaps?
      if (prefWidthAttr.EqualsLiteral("min-width")) {
        if (auto* f = element->GetPrimaryFrame(FlushType::Frames)) {
          const auto coord = f->StylePosition()->GetMinWidth(
              AnchorPosResolutionParams::From(f));
          if (coord->ConvertsToLength()) {
            prefWidth = CSSPixel::FromAppUnitsRounded(coord->ToLength());
          }
        }
      }
    }
  }

  Maybe<CSSIntSize> size =
      viewer->GetContentSize(maxWidth, maxHeight, prefWidth);
  if (!size) {
    return;
  }
  nsPresContext* pc = viewer->GetPresContext();
  MOZ_ASSERT(pc, "Should have pres context");

  int32_t width = pc->CSSPixelsToDevPixels(size->width);
  int32_t height = pc->CSSPixelsToDevPixels(size->height);
  SizeShellTo(docShell, width, height);

  // Update specified size for the final LoadPositionFromXUL call.
  aSpecWidth = size->width + aWindowDiff.width;
  aSpecHeight = size->height + aWindowDiff.height;
}

void AppWindow::SizeShell() {
  AutoRestore<bool> sizingShellFromXUL(mSizingShellFromXUL);
  mSizingShellFromXUL = true;

  int32_t specWidth = -1, specHeight = -1;
  bool gotSize = false;

  nsAutoString windowType;
  if (nsCOMPtr<dom::Element> windowElement = GetWindowDOMElement()) {
    windowElement->GetAttr(nsGkAtoms::windowtype, windowType);
  }

  const CSSIntSize windowDiff = GetOuterToInnerSizeDifferenceInCSSPixels(
      mWindow, UnscaledDevicePixelsPerCSSPixel());

  // If we're using fingerprint resistance, we're going to resize the window
  // once we have primary content.
  if (nsContentUtils::ShouldResistFingerprinting(
          "if RFP is enabled we want to round the dimensions of the new"
          "new pop up window regardless of their origin",
          RFPTarget::RoundWindowSize) &&
      windowType.EqualsLiteral("navigator:browser")) {
    // Once we've got primary content, force dimensions.
    if (mPrimaryContentShell || mPrimaryBrowserParent) {
      ForceRoundedDimensions();
    }
    // Always avoid setting size/sizemode on this window.
    mIgnoreXULSize = true;
    mIgnoreXULSizeMode = true;
  } else if (!mIgnoreXULSize) {
    gotSize = LoadSizeFromXUL(specWidth, specHeight);
    specWidth += windowDiff.width;
    specHeight += windowDiff.height;
  }

  bool positionSet = !mIgnoreXULPosition;
  nsCOMPtr<nsIAppWindow> parentWindow(do_QueryReferent(mParentWindow));
#if defined(XP_UNIX) && !defined(XP_MACOSX)
  // don't override WM placement on unix for independent, top-level windows
  // (however, we think the benefits of intelligent dependent window placement
  // trump that override.)
  if (!parentWindow) positionSet = false;
#endif
  if (positionSet) {
    // We have to do this before sizing the window, because sizing depends
    // on the resolution of the screen we're on. But positioning needs to
    // know the size so that it can constrain to screen bounds.... as an
    // initial guess here, we'll use the specified size (if any).
    positionSet = LoadPositionFromXUL(specWidth, specHeight);
  }

  if (gotSize) {
    SetSpecifiedSize(specWidth, specHeight);
  }

  // If LoadSizeFromXUL set the size, mIntrinsicallySized will be false.
  if (mIntrinsicallySized) {
    IntrinsicallySizeShell(windowDiff, specWidth, specHeight);
  }

  // Now that we have set the window's final size, we can re-do its
  // positioning so that it is properly constrained to the screen.
  if (positionSet) {
    LoadPositionFromXUL(specWidth, specHeight);
  }

  UpdateWindowStateFromMiscXULAttributes();

  if (mChromeLoaded && mCenterAfterLoad && !positionSet &&
      mWindow->SizeMode() == nsSizeMode_Normal) {
    Center(parentWindow, !parentWindow, false);
  }
}

NS_IMETHODIMP AppWindow::GetXULBrowserWindow(
    nsIXULBrowserWindow** aXULBrowserWindow) {
  NS_IF_ADDREF(*aXULBrowserWindow = mXULBrowserWindow);
  return NS_OK;
}

NS_IMETHODIMP AppWindow::SetXULBrowserWindow(
    nsIXULBrowserWindow* aXULBrowserWindow) {
  mXULBrowserWindow = aXULBrowserWindow;
  return NS_OK;
}

// Given the dimensions of some content area held within this XUL window, and
// assuming that that content area will change its dimensions in linear
// proportion to the dimensions of this XUL window, changes the size of the XUL
// window so that the content area reaches a particular size.
void AppWindow::SizeShellToWithLimit(int32_t aDesiredWidth,
                                     int32_t aDesiredHeight,
                                     int32_t shellItemWidth,
                                     int32_t shellItemHeight) {
  int32_t widthDelta = aDesiredWidth - shellItemWidth;
  int32_t heightDelta = aDesiredHeight - shellItemHeight;

  int32_t winWidth = 0;
  int32_t winHeight = 0;

  GetSize(&winWidth, &winHeight);
  // There's no point in trying to make the window smaller than the
  // desired content area size --- that's not likely to work. This whole
  // function assumes that the outer docshell is adding some constant
  // "border" chrome to the content area.
  winWidth = std::max(winWidth + widthDelta, aDesiredWidth);
  winHeight = std::max(winHeight + heightDelta, aDesiredHeight);

  // Note: Because of the asynchronous resizing on Linux we have to call
  // SetSize even when the size doesn't appear to change. A previous call that
  // has yet to complete can still change the size. We want the latest call to
  // define the final size.
  SetSize(winWidth, winHeight, true);
  mDominantClientSize = true;
}

nsresult AppWindow::GetInitialOpenWindowInfo(
    nsIOpenWindowInfo** aOpenWindowInfo) {
  NS_ENSURE_ARG_POINTER(aOpenWindowInfo);
  *aOpenWindowInfo = do_AddRef(mInitialOpenWindowInfo).take();
  return NS_OK;
}

PresShell* AppWindow::GetPresShell() {
  if (!mDocShell) {
    return nullptr;
  }
  return mDocShell->GetPresShell();
}

void AppWindow::WindowMoved(nsIWidget*, const LayoutDeviceIntPoint&) {
  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    nsCOMPtr<nsPIDOMWindowOuter> window =
        mDocShell ? mDocShell->GetWindow() : nullptr;
    pm->AdjustPopupsOnWindowChange(window);
  }

  // Notify all tabs that the widget moved.
  if (mDocShell && mDocShell->GetWindow()) {
    nsCOMPtr<EventTarget> eventTarget =
        mDocShell->GetWindow()->GetTopWindowRoot();
    nsContentUtils::DispatchChromeEvent(
        mDocShell->GetDocument(), eventTarget, u"MozUpdateWindowPos"_ns,
        CanBubble::eNo, Cancelable::eNo, nullptr);
  }

  // Persist position, but not immediately, in case this OS is firing
  // repeated move events as the user drags the window
  PersistentAttributesDirty(PersistentAttribute::Position, Async);
}

void AppWindow::WindowResized(nsIWidget* aWidget,
                              const LayoutDeviceIntSize& aSize) {
  mDominantClientSize = false;
  if (mDocShell) {
    mDocShell->SetPositionAndSize(0, 0, aSize.width, aSize.height, 0);
  }
  // Persist size, but not immediately, in case this OS is firing
  // repeated size events as the user drags the sizing handle
  if (!IsLocked()) {
    PersistentAttributesDirty(AllPersistentAttributes(), Async);
  }
  // Check if we need to continue a fullscreen change.
  switch (mFullscreenChangeState) {
    case FullscreenChangeState::WillChange:
      mFullscreenChangeState = FullscreenChangeState::WidgetResized;
      break;
    case FullscreenChangeState::WidgetEnteredFullscreen:
      FinishFullscreenChange(true);
      break;
    case FullscreenChangeState::WidgetExitedFullscreen:
      FinishFullscreenChange(false);
      break;
    case FullscreenChangeState::WidgetResized:
    case FullscreenChangeState::NotChanging:
      break;
  }
}

bool AppWindow::RequestWindowClose(nsIWidget* aWidget) {
  // Maintain a reference to this as it is about to get destroyed.
  nsCOMPtr<nsIAppWindow> appWindow(this);

  nsCOMPtr<nsPIDOMWindowOuter> window(mDocShell ? mDocShell->GetWindow()
                                                : nullptr);
  nsCOMPtr<EventTarget> eventTarget = do_QueryInterface(window);

  RefPtr<PresShell> presShell = mDocShell->GetPresShell();
  if (!presShell) {
    mozilla::DebugOnly<bool> dying;
    MOZ_ASSERT(NS_SUCCEEDED(mDocShell->IsBeingDestroyed(&dying)) && dying,
               "No presShell, but window is not being destroyed");
  } else if (eventTarget) {
    RefPtr<nsPresContext> presContext = presShell->GetPresContext();

    nsEventStatus status = nsEventStatus_eIgnore;
    WidgetMouseEvent event(true, eClose, nullptr, WidgetMouseEvent::eReal);
    if (NS_SUCCEEDED(EventDispatcher::Dispatch(eventTarget, presContext, &event,
                                               nullptr, &status)) &&
        status == nsEventStatus_eConsumeNoDefault)
      return false;
  }

  Destroy();
  return false;
}

void AppWindow::SizeModeChanged(nsSizeMode aSizeMode) {
  const bool wasWidgetInFullscreen = mIsWidgetInFullscreen;
  // Fullscreen and minimized states are usually compatible, and the widget
  // typically returns to fullscreen after restoration. By not updating the
  // widget's fullscreen state while it is minimized, we can avoid unnecessary
  // fullscreen exits, such as those encountered in bug 1823284.
  if (aSizeMode != nsSizeMode_Minimized) {
    mIsWidgetInFullscreen = aSizeMode == nsSizeMode_Fullscreen;
  }

  const bool fullscreenChanged = wasWidgetInFullscreen != mIsWidgetInFullscreen;
  if (fullscreenChanged) {
    FullscreenWillChange(mIsWidgetInFullscreen);
  }

  RecomputeBrowsingContextVisibility();

  PersistentAttributesDirty(PersistentAttribute::Misc, Sync);
  nsCOMPtr<nsPIDOMWindowOuter> ourWindow =
      mDocShell ? mDocShell->GetWindow() : nullptr;
  if (ourWindow) {
    // Always fire a user-defined sizemodechange event on the window
    ourWindow->DispatchCustomEvent(u"sizemodechange"_ns);
  }

  if (PresShell* presShell = GetPresShell()) {
    presShell->GetPresContext()->SizeModeChanged(aSizeMode);
  }

  if (fullscreenChanged) {
    FullscreenChanged(mIsWidgetInFullscreen);
  }

  // Note the current implementation of SetSizeMode just stores
  // the new state; it doesn't actually resize. So here we store
  // the state and pass the event on to the OS. The day is coming
  // when we'll handle the event here, and the return result will
  // then need to be different.
}

void AppWindow::FullscreenWillChange(bool aInFullscreen) {
  if (mDocShell) {
    if (nsCOMPtr<nsPIDOMWindowOuter> ourWindow = mDocShell->GetWindow()) {
      ourWindow->FullscreenWillChange(aInFullscreen);
    }
  }
  MOZ_ASSERT(mFullscreenChangeState == FullscreenChangeState::NotChanging);

  CSSToLayoutDeviceScale scale = UnscaledDevicePixelsPerCSSPixel();
  CSSIntSize windowSizeCSS = RoundedToInt(GetSize() / scale);

  CSSIntSize screenSizeCSS;
  GetAvailScreenSize(&screenSizeCSS.width, &screenSizeCSS.height);

  // Check if the window is already at the expected dimensions. If it is, set
  // the fullscreen change state to WidgetResized to avoid waiting for a resize
  // event. On macOS, a fullscreen window could be slightly higher than
  // available screen size because of the OS menu bar isn't yet hidden.
  mFullscreenChangeState =
      (aInFullscreen == (windowSizeCSS.width == screenSizeCSS.width &&
                         windowSizeCSS.height >= screenSizeCSS.height))
          ? FullscreenChangeState::WidgetResized
          : FullscreenChangeState::WillChange;
}

void AppWindow::FullscreenChanged(bool aInFullscreen) {
  if (mFullscreenChangeState == FullscreenChangeState::WidgetResized) {
    FinishFullscreenChange(aInFullscreen);
  } else {
    NS_WARNING_ASSERTION(
        mFullscreenChangeState == FullscreenChangeState::WillChange,
        "Unexpected fullscreen change state");
    FullscreenChangeState newState =
        aInFullscreen ? FullscreenChangeState::WidgetEnteredFullscreen
                      : FullscreenChangeState::WidgetExitedFullscreen;
    mFullscreenChangeState = newState;
    nsCOMPtr<nsIAppWindow> kungFuDeathGrip(this);
    // Wait for resize for a small amount of time.
    // 80ms is actually picked arbitrarily. But it shouldn't be too large
    // in case the widget resize is not going to happen at all, which can
    // be the case for some Linux window managers and possibly Android.
    NS_DelayedDispatchToCurrentThread(
        NS_NewRunnableFunction(
            "AppWindow::FullscreenChanged",
            [this, kungFuDeathGrip, newState, aInFullscreen]() {
              if (mFullscreenChangeState == newState) {
                FinishFullscreenChange(aInFullscreen);
              }
            }),
        80);
  }
}

void AppWindow::FinishFullscreenChange(bool aInFullscreen) {
  mFullscreenChangeState = FullscreenChangeState::NotChanging;
  if (nsXULPopupManager* pm = nsXULPopupManager::GetInstance()) {
    pm->Rollup({});
  }
  if (mDocShell) {
    if (nsCOMPtr<nsPIDOMWindowOuter> ourWindow = mDocShell->GetWindow()) {
      ourWindow->FinishFullscreenChange(aInFullscreen);
    }
  }
}

void AppWindow::MacFullscreenMenubarOverlapChanged(
    mozilla::DesktopCoord aOverlapAmount) {
  if (mDocShell) {
    if (nsCOMPtr<nsPIDOMWindowOuter> ourWindow = mDocShell->GetWindow()) {
      ourWindow->MacFullscreenMenubarOverlapChanged(aOverlapAmount);
    }
  }
}

void AppWindow::RecomputeBrowsingContextVisibility() {
  if (!mDocShell) {
    return;
  }
  RefPtr bc = mDocShell->GetBrowsingContext();
  if (!bc) {
    return;
  }
  bc->Canonical()->RecomputeAppWindowVisibility();
}

void AppWindow::OcclusionStateChanged(bool aIsFullyOccluded) {
  if (!mDocShell) {
    return;
  }
  RecomputeBrowsingContextVisibility();
  if (RefPtr win = mDocShell->GetWindow()) {
    // And always fire a user-defined occlusionstatechange event on the window
    win->DispatchCustomEvent(u"occlusionstatechange"_ns,
                             ChromeOnlyDispatch::eYes);
  }
}

void AppWindow::WindowActivated() {
  nsCOMPtr<nsIAppWindow> appWindow(this);

  // focusing the window could cause it to close, so keep a reference to it
  if (mDocShell) {
    if (nsCOMPtr<nsPIDOMWindowOuter> window = mDocShell->GetWindow()) {
      if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
        fm->WindowRaised(window, nsFocusManager::GenerateFocusActionId());
      }
    }
  }

  if (mChromeLoaded) {
    PersistentAttributesDirty(AllPersistentAttributes(), Sync);
  }
}

void AppWindow::WindowDeactivated() {
  if (mDocShell) {
    if (nsCOMPtr<nsPIDOMWindowOuter> window = mDocShell->GetWindow()) {
      if (RefPtr<nsFocusManager> fm = nsFocusManager::GetFocusManager()) {
        if (!fm->IsTestMode()) {
          fm->WindowLowered(window, nsFocusManager::GenerateFocusActionId());
        }
      }
    }
  }
}

#ifdef USE_NATIVE_MENUS

struct LoadNativeMenusListener {
  LoadNativeMenusListener(Document* aDoc, nsIWidget* aParentWindow)
      : mDocument(aDoc), mParentWindow(aParentWindow) {}

  RefPtr<Document> mDocument;
  nsCOMPtr<nsIWidget> mParentWindow;
};

// On macOS the hidden window is created eagerly, and we want to wait for it to
// load the native menus.
static bool sWaitingForHiddenWindowToLoadNativeMenus =
#  ifdef XP_MACOSX
    true
#  else
    false
#  endif
    ;

constinit static nsTArray<LoadNativeMenusListener> sLoadNativeMenusListeners;

static void BeginLoadNativeMenus(Document* aDoc, nsIWidget* aParentWindow);

static void LoadNativeMenus(Document* aDoc, nsIWidget* aParentWindow) {
  MOZ_ASSERT(!gfxPlatform::IsHeadless());

  // Find the menubar tag (if there is more than one, we ignore all but
  // the first).
  RefPtr<Element> menubar =
      aDoc->QuerySelector("menubar:not([nonnative])"_ns, IgnoreErrors());
  widget::NativeMenuSupport::CreateNativeMenuBar(aParentWindow, menubar);

  if (sWaitingForHiddenWindowToLoadNativeMenus) {
    sWaitingForHiddenWindowToLoadNativeMenus = false;
    for (auto& listener : sLoadNativeMenusListeners) {
      BeginLoadNativeMenus(listener.mDocument, listener.mParentWindow);
    }
    sLoadNativeMenusListeners.Clear();
  }
}

class L10nReadyPromiseHandler final : public dom::PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  L10nReadyPromiseHandler(Document* aDoc, nsIWidget* aParentWindow)
      : mDocument(aDoc), mWindow(aParentWindow) {}

  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    LoadNativeMenus(mDocument, mWindow);
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    // Again, this shouldn't happen, but fallback to loading the menus as is.
    NS_WARNING(
        "L10nReadyPromiseHandler rejected - loading fallback native "
        "menu.");
    LoadNativeMenus(mDocument, mWindow);
  }

 private:
  ~L10nReadyPromiseHandler() = default;

  RefPtr<Document> mDocument;
  nsCOMPtr<nsIWidget> mWindow;
};

NS_IMPL_ISUPPORTS0(L10nReadyPromiseHandler)

static void BeginLoadNativeMenus(Document* aDoc, nsIWidget* aParentWindow) {
  if (RefPtr<DocumentL10n> l10n = aDoc->GetL10n()) {
    // Wait for l10n to be ready so the menus are localized.
    RefPtr<Promise> promise = l10n->Ready();
    MOZ_ASSERT(promise);
    RefPtr handler = new L10nReadyPromiseHandler(aDoc, aParentWindow);
    promise->AppendNativeHandler(handler);
  } else {
    // Something went wrong loading the doc and l10n wasn't created. This
    // shouldn't really happen, but if it does fallback to trying to load
    // the menus as is.
    LoadNativeMenus(aDoc, aParentWindow);
  }
}

#endif

class AppWindowTimerCallback final : public nsITimerCallback, public nsINamed {
 public:
  explicit AppWindowTimerCallback(AppWindow* aWindow) : mWindow(aWindow) {}

  NS_DECL_THREADSAFE_ISUPPORTS

  NS_IMETHOD Notify(nsITimer* aTimer) override {
    // Although this object participates in a refcount cycle (this -> mWindow
    // -> mSPTimer -> this), mSPTimer is a one-shot timer and releases this
    // after it fires.  So we don't need to release mWindow here.

    mWindow->FirePersistenceTimer();
    return NS_OK;
  }

  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("AppWindowTimerCallback");
    return NS_OK;
  }

 private:
  ~AppWindowTimerCallback() = default;

  RefPtr<AppWindow> mWindow;
};

NS_IMPL_ISUPPORTS(AppWindowTimerCallback, nsITimerCallback, nsINamed)

void AppWindow::PersistentAttributesDirty(PersistentAttributes aAttributes,
                                          PersistentAttributeUpdate aUpdate) {
  aAttributes = aAttributes & mPersistentAttributesMask;
  if (aAttributes.isEmpty()) {
    return;
  }

  mPersistentAttributesDirty += aAttributes;
  if (aUpdate == Sync) {
    // Only apply the attributes we've been requested to apply sync, not other
    // potentially dirty attributes that have been requested asynchronously.
    SavePersistentAttributes(aAttributes);
    return;
  }
  if (!mSPTimer) {
    mSPTimer = NS_NewTimer();
    if (!mSPTimer) {
      NS_WARNING("Couldn't create timer instance?");
      return;
    }
  }

  RefPtr<AppWindowTimerCallback> callback = new AppWindowTimerCallback(this);
  mSPTimer->InitWithCallback(callback, SIZE_PERSISTENCE_TIMEOUT,
                             nsITimer::TYPE_ONE_SHOT);
}

void AppWindow::FirePersistenceTimer() { SavePersistentAttributes(); }

void AppWindow::OnChromeLoaded() {
  MOZ_ASSERT(!mChromeLoaded);

  mChromeLoaded = true;
  mLockedUntilChromeLoad = false;

#ifdef USE_NATIVE_MENUS
  ///////////////////////////////
  // Find the Menubar DOM  and Load the menus, hooking them up to the loaded
  // commands
  ///////////////////////////////
  if (!gfxPlatform::IsHeadless()) {
    if (RefPtr<Document> menubarDoc = mDocShell->GetExtantDocument()) {
      nsCOMPtr<nsIAppShellService> appShellService(
          do_GetService(NS_APPSHELLSERVICE_CONTRACTID));
      bool hasHiddenWindow = false;
      if (appShellService) {
        appShellService->GetHasHiddenWindow(&hasHiddenWindow);
      }

      // Load this window's native menus immediately if any of the
      // following hold; otherwise queue them to be loaded once the
      // hidden window has loaded its own menus first.
      //
      //   (a) This window IS the hidden window. Its menus must load
      //       first so the application menu is set up before any
      //       other window's <menubar>.
      //   (b) The hidden window has already loaded its native menus
      //       (sWaitingForHiddenWindowToLoadNativeMenus is false).
      //   (c) No hidden window has been (or will be) created. This
      //       happens in flows that run before normal startup, e.g.
      //       Profile Manager / Profile Downgrade (bug 1154697).
      //       Without this check we would queue indefinitely and the
      //       early-startup window's <menubar> would render in-window.
      bool shouldLoadNativeMenus = mIsHiddenWindow ||
                                   !sWaitingForHiddenWindowToLoadNativeMenus ||
                                   !hasHiddenWindow;
      if (shouldLoadNativeMenus) {
        BeginLoadNativeMenus(menubarDoc, mWindow);
      } else {
        sLoadNativeMenusListeners.EmplaceBack(menubarDoc, mWindow);
      }
    }
  }
#endif  // USE_NATIVE_MENUS

  nsresult rv = EnsureContentTreeOwner();

  if (NS_SUCCEEDED(rv)) {
    ApplyChromeFlags();
    SyncAttributesToWidget();
    if (RefPtr ps = GetPresShell()) {
      // Sync window properties now, before showing the window.
      ps->SyncWindowPropertiesIfNeeded();
    }
    if (mWindow) {
      SizeShell();
      if (mShowAfterLoad) {
        SetVisibility(true);
      }
      AddTooltipSupport();
    }
    // At this point the window may have been closed already during Show() or
    // SyncAttributesToWidget(), so AppWindow::Destroy may already have been
    // called. Take care!
  }
  mPersistentAttributesMask += AllPersistentAttributes();
}

NS_IMETHODIMP
AppWindow::ShowInitialViewer() {
  NS_ENSURE_FALSE(mChromeLoaded, NS_ERROR_UNEXPECTED);

  MOZ_ASSERT(mDocShell->GetDocument()->IsUncommittedInitialDocument(),
             "This method is for showing the initial document, not the result "
             "of a some navigation");

  OnChromeLoaded();
  return NS_OK;
}

//----------------------------------------
// nsIWebProgessListener implementation
//----------------------------------------
NS_IMETHODIMP
AppWindow::OnProgressChange(nsIWebProgress* aProgress, nsIRequest* aRequest,
                            int32_t aCurSelfProgress, int32_t aMaxSelfProgress,
                            int32_t aCurTotalProgress,
                            int32_t aMaxTotalProgress) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::OnStateChange(nsIWebProgress* aProgress, nsIRequest* aRequest,
                         uint32_t aStateFlags, nsresult aStatus) {
  // If the notification is not about a document finishing, then just
  // ignore it...
  if (!(aStateFlags & nsIWebProgressListener::STATE_STOP) ||
      !(aStateFlags & nsIWebProgressListener::STATE_IS_NETWORK)) {
    return NS_OK;
  }

  if (mChromeLoaded) return NS_OK;

  // If this document notification is for a frame then ignore it...
  nsCOMPtr<mozIDOMWindowProxy> eventWin;
  aProgress->GetDOMWindow(getter_AddRefs(eventWin));
  auto* eventPWin = nsPIDOMWindowOuter::From(eventWin);
  if (eventPWin) {
    nsPIDOMWindowOuter* rootPWin = eventPWin->GetPrivateRoot();
    if (eventPWin != rootPWin) return NS_OK;
  }

  OnChromeLoaded();

  return NS_OK;
}

NS_IMETHODIMP
AppWindow::OnLocationChange(nsIWebProgress* aProgress, nsIRequest* aRequest,
                            nsIURI* aURI, uint32_t aFlags) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::OnStatusChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                          nsresult aStatus, const char16_t* aMessage) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::OnSecurityChange(nsIWebProgress* aWebProgress, nsIRequest* aRequest,
                            uint32_t aState) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

NS_IMETHODIMP
AppWindow::OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                  nsIRequest* aRequest, uint32_t aEvent) {
  MOZ_ASSERT_UNREACHABLE("notification excluded in AddProgressListener(...)");
  return NS_OK;
}

/**
 * ExecuteCloseHandler - Run the close handler, if any.
 * @return true iff we found a close handler to run.
 */
bool AppWindow::ExecuteCloseHandler() {
  /* If the event handler closes this window -- a likely scenario --
     things get deleted out of order without this death grip.
     (The problem may be the death grip in nsWindow::windowProc,
     which forces this window's widget to remain alive longer
     than it otherwise would.) */
  nsCOMPtr<nsIAppWindow> kungFuDeathGrip(this);

  nsCOMPtr<EventTarget> eventTarget;
  if (mDocShell) {
    eventTarget = do_QueryInterface(mDocShell->GetWindow());
  }

  if (eventTarget) {
    nsCOMPtr<nsIDocumentViewer> viewer;
    mDocShell->GetDocViewer(getter_AddRefs(viewer));
    if (viewer) {
      RefPtr<nsPresContext> presContext = viewer->GetPresContext();

      nsEventStatus status = nsEventStatus_eIgnore;
      WidgetMouseEvent event(true, eClose, nullptr, WidgetMouseEvent::eReal);

      nsresult rv = EventDispatcher::Dispatch(eventTarget, presContext, &event,
                                              nullptr, &status);
      if (NS_SUCCEEDED(rv) && status == nsEventStatus_eConsumeNoDefault)
        return true;
      // else fall through and return false
    }
  }

  return false;
}  // ExecuteCloseHandler

void AppWindow::ConstrainToOpenerScreen(int32_t* aX, int32_t* aY) {
  if (mOpenerScreenRect.IsEmpty()) {
    *aX = *aY = 0;
    return;
  }

  int32_t left, top, width, height;
  // Constrain initial positions to the same screen as opener
  nsCOMPtr<nsIScreenManager> screenmgr =
      do_GetService("@mozilla.org/gfx/screenmanager;1");
  if (screenmgr) {
    nsCOMPtr<nsIScreen> screen = screenmgr->ScreenForRect(mOpenerScreenRect);
    if (screen) {
      screen->GetAvailRectDisplayPix(&left, &top, &width, &height);
      if (*aX < left || *aX > left + width) {
        *aX = left;
      }
      if (*aY < top || *aY > top + height) {
        *aY = top;
      }
    }
  }
}

nsIAppWindow* AppWindow::WidgetListenerDelegate::GetAppWindow() {
  return mAppWindow->GetAppWindow();
}

PresShell* AppWindow::WidgetListenerDelegate::GetPresShell() {
  return mAppWindow->GetPresShell();
}

void AppWindow::WidgetListenerDelegate::WindowMoved(
    nsIWidget* aWidget, const LayoutDeviceIntPoint& aPoint, ByMoveToRect) {
  RefPtr<AppWindow> holder = mAppWindow;
  holder->WindowMoved(aWidget, aPoint);
}

void AppWindow::WidgetListenerDelegate::WindowResized(
    nsIWidget* aWidget, const LayoutDeviceIntSize& aSize) {
  RefPtr<AppWindow> holder = mAppWindow;
  holder->WindowResized(aWidget, aSize);
}

bool AppWindow::WidgetListenerDelegate::RequestWindowClose(nsIWidget* aWidget) {
  RefPtr<AppWindow> holder = mAppWindow;
  return holder->RequestWindowClose(aWidget);
}

void AppWindow::WidgetListenerDelegate::SizeModeChanged(nsSizeMode aSizeMode) {
  RefPtr<AppWindow> holder = mAppWindow;
  holder->SizeModeChanged(aSizeMode);
}

void AppWindow::WidgetListenerDelegate::MacFullscreenMenubarOverlapChanged(
    DesktopCoord aOverlapAmount) {
  RefPtr<AppWindow> holder = mAppWindow;
  return holder->MacFullscreenMenubarOverlapChanged(aOverlapAmount);
}

void AppWindow::WidgetListenerDelegate::OcclusionStateChanged(
    bool aIsFullyOccluded) {
  RefPtr<AppWindow> holder = mAppWindow;
  holder->OcclusionStateChanged(aIsFullyOccluded);
}

void AppWindow::WidgetListenerDelegate::WindowActivated() {
  RefPtr<AppWindow> holder = mAppWindow;
  holder->WindowActivated();
}

void AppWindow::WidgetListenerDelegate::WindowDeactivated() {
  RefPtr<AppWindow> holder = mAppWindow;
  holder->WindowDeactivated();
}

}  // namespace mozilla
