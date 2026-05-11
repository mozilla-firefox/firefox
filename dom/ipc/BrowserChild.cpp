/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BrowserChild.h"

#ifdef ACCESSIBILITY
#  include "mozilla/a11y/DocAccessibleChild.h"
#  include "nsAccessibilityService.h"
#endif
#include <utility>

#include "BrowserParent.h"
#include "ContentChild.h"
#include "EventStateManager.h"
#include "MMPrinter.h"
#include "PuppetWidget.h"
#include "StructuredCloneData.h"
#include "UnitTransforms.h"
#include "Units.h"
#include "VRManagerChild.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/EventForwards.h"
#include "mozilla/EventListenerManager.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/IMEStateManager.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/MediaFeatureChange.h"
#include "mozilla/MiscEvents.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/NativeKeyBindingsType.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/PointerLockManager.h"
#include "mozilla/PresShell.h"
#include "mozilla/ProcessHangMonitor.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEvents.h"
#include "mozilla/ToString.h"
#include "mozilla/dom/AutoPrintEventDispatcher.h"
#include "mozilla/dom/BrowserBridgeChild.h"
#include "mozilla/dom/DataTransfer.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/ImageDocument.h"
#include "mozilla/dom/JSWindowActorChild.h"
#include "mozilla/dom/LoadURIOptionsBinding.h"
#include "mozilla/dom/MessageManagerBinding.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/PBrowser.h"
#include "mozilla/dom/PaymentRequestChild.h"
#include "mozilla/dom/PointerEventHandler.h"
#include "mozilla/dom/SessionStoreChild.h"
#include "mozilla/dom/SessionStoreUtils.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/ViewTransition.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/dom/WindowProxyHolder.h"
#include "mozilla/gfx/CrossProcessPaint.h"
#include "mozilla/gfx/Matrix.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/APZCTreeManagerChild.h"
#include "mozilla/layers/APZChild.h"
#include "mozilla/layers/APZEventState.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/ContentProcessController.h"
#include "mozilla/layers/DoubleTapToZoom.h"
#include "mozilla/layers/IAPZCTreeManager.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/InputAPZContext.h"
#include "mozilla/layers/TouchActionHelper.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/widget/ScreenManager.h"
#include "mozilla/widget/WidgetLogging.h"
#include "nsCommandParams.h"
#include "nsContentPermissionHelper.h"
#include "nsContentUtils.h"
#include "nsDeviceContext.h"
#include "nsDocShell.h"
#include "nsDocShellLoadState.h"
#include "nsDragServiceProxy.h"
#include "nsExceptionHandler.h"
#include "nsFilePickerProxy.h"
#include "nsFocusManager.h"
#include "nsGlobalWindowOuter.h"
#include "nsIBaseWindow.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIClassifiedChannel.h"
#include "nsIDocShell.h"
#include "nsIFrame.h"
#include "nsILoadContext.h"
#include "nsIOpenWindowInfo.h"
#include "nsISHEntry.h"
#include "nsISHistory.h"
#include "nsIScreenManager.h"
#include "nsIScriptError.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsIWeakReferenceUtils.h"
#include "nsIWebBrowser.h"
#include "nsIWebProgress.h"
#include "nsIXULRuntime.h"
#include "nsLayoutUtils.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPIWindowRoot.h"
#include "nsPrintfCString.h"
#include "nsRefreshDriver.h"
#include "nsThreadManager.h"
#include "nsThreadUtils.h"
#include "nsVariant.h"
#include "nsWebBrowser.h"
#include "nsWindowWatcher.h"

#ifdef MOZ_WAYLAND
#  include "nsAppRunner.h"
#endif

#ifdef NS_PRINTING
#  include "mozilla/layout/RemotePrintJobChild.h"
#  include "nsIPrintSettings.h"
#  include "nsIPrintSettingsService.h"
#  include "nsIWebBrowserPrint.h"
#endif

static mozilla::LazyLogModule sApzChildLog("apz.child");

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::dom::ipc;
using namespace mozilla::ipc;
using namespace mozilla::layers;
using namespace mozilla::layout;
using namespace mozilla::widget;
using mozilla::layers::GeckoContentController;

static const char BEFORE_FIRST_PAINT[] = "before-first-paint";

static uint32_t sConsecutiveTouchMoveCount = 0;

using BrowserChildMap = nsTHashMap<nsUint64HashKey, BrowserChild*>;
static BrowserChildMap* sBrowserChildren;
StaticMutex sBrowserChildrenMutex;

namespace {

class SynthesizedEventChildCallback final : public nsISynthesizedEventCallback {
  NS_DECL_ISUPPORTS

 public:
  SynthesizedEventChildCallback(BrowserChild* aBrowserChild,
                                const uint64_t& aCallbackId)
      : mBrowserChild(aBrowserChild), mCallbackId(aCallbackId) {
    MOZ_ASSERT(mBrowserChild);
    MOZ_ASSERT(mCallbackId > 0, "Invalid callback ID");
  }

  NS_IMETHOD OnCompleteDispatch() override {
    MOZ_ASSERT(mCallbackId > 0, "Invalid callback ID");

    if (!mBrowserChild) {
      // We already sent the notification, or we don't actually need to
      // send any notification at all.
      MOZ_ASSERT_UNREACHABLE("OnCompleteDispatch called multiple times");
      return NS_OK;
    }

    if (mBrowserChild->IsDestroyed()) {
      // If this happens it's probably a bug in the test that's triggering this.
      NS_WARNING(
          "BrowserChild was unexpectedly destroyed during event "
          "synthesization response!");
    } else if (!mBrowserChild->SendSynthesizedEventResponse(mCallbackId)) {
      NS_WARNING("Unable to send event synthesization response!");
    }
    // Null out browserChild to indicate we already sent the response
    mBrowserChild = nullptr;
    return NS_OK;
  }

 private:
  virtual ~SynthesizedEventChildCallback() = default;

  RefPtr<BrowserChild> mBrowserChild;
  uint64_t mCallbackId;
};

NS_IMPL_ISUPPORTS(SynthesizedEventChildCallback, nsISynthesizedEventCallback)

template <class T>
class MOZ_RAII AutoSynthesizedEventResponder final {
 public:
  AutoSynthesizedEventResponder(BrowserChild* aBrowserChild, const T& aEvent) {
    if (aEvent.mCallbackId.isSome()) {
      mCallback = MakeAndAddRef<SynthesizedEventChildCallback>(
          aBrowserChild, aEvent.mCallbackId.ref());
    }
  }

  ~AutoSynthesizedEventResponder() {
    if (mCallback) {
      mCallback->OnCompleteDispatch();
    }
  }

 private:
  nsCOMPtr<nsISynthesizedEventCallback> mCallback;
};

}  // namespace

already_AddRefed<Document> BrowserChild::GetTopLevelDocument() const {
  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation());
  nsCOMPtr<Document> doc = docShell ? docShell->GetExtantDocument() : nullptr;
  return doc.forget();
}

PresShell* BrowserChild::GetTopLevelPresShell() const {
  if (RefPtr<Document> doc = GetTopLevelDocument()) {
    return doc->GetPresShell();
  }
  return nullptr;
}

bool BrowserChild::UpdateFrame(const RepaintRequest& aRequest) {
  MOZ_ASSERT(aRequest.GetScrollId() != ScrollableLayerGuid::NULL_SCROLL_ID);

  if (aRequest.IsRootContent()) {
    if (PresShell* presShell = GetTopLevelPresShell()) {
      // Guard against stale updates (updates meant for a pres shell which
      // has since been torn down and destroyed).
      if (aRequest.GetPresShellId() == presShell->GetPresShellId()) {
        APZCCallbackHelper::UpdateRootFrame(aRequest);
        return true;
      }
    }
  } else {
    // aRequest.mIsRoot is false, so we are trying to update a subframe.
    // This requires special handling.
    APZCCallbackHelper::UpdateSubFrame(aRequest);
    return true;
  }
  return true;
}

class BrowserChild::DelayedDeleteRunnable final : public Runnable,
                                                  public nsIRunnablePriority {
  RefPtr<BrowserChild> mBrowserChild;

  // In order to try that this runnable runs after everything that could
  // possibly touch this tab, we send it through the event queue twice.
  bool mReadyToDelete = false;

 public:
  explicit DelayedDeleteRunnable(BrowserChild* aBrowserChild)
      : Runnable("BrowserChild::DelayedDeleteRunnable"),
        mBrowserChild(aBrowserChild) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aBrowserChild);
  }

  NS_DECL_ISUPPORTS_INHERITED

 private:
  ~DelayedDeleteRunnable() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(!mBrowserChild);
  }

  NS_IMETHOD GetPriority(uint32_t* aPriority) override {
    *aPriority = nsIRunnablePriority::PRIORITY_NORMAL;
    return NS_OK;
  }

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mBrowserChild);

    if (!mReadyToDelete) {
      // This time run this runnable at input priority.
      mReadyToDelete = true;
      MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(this));
      return NS_OK;
    }

    // Check in case ActorDestroy was called after RecvDestroy message.
    if (mBrowserChild->IPCOpen()) {
      (void)PBrowserChild::Send__delete__(mBrowserChild);
    }

    mBrowserChild = nullptr;
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS_INHERITED(BrowserChild::DelayedDeleteRunnable, Runnable,
                            nsIRunnablePriority)

namespace {
std::map<TabId, RefPtr<BrowserChild>>& NestedBrowserChildMap() {
  MOZ_ASSERT(NS_IsMainThread());
  static std::map<TabId, RefPtr<BrowserChild>> sNestedBrowserChildMap;
  return sNestedBrowserChildMap;
}
}  // namespace

already_AddRefed<BrowserChild> BrowserChild::FindBrowserChild(
    const TabId& aTabId) {
  auto iter = NestedBrowserChildMap().find(aTabId);
  if (iter == NestedBrowserChildMap().end()) {
    return nullptr;
  }
  RefPtr<BrowserChild> browserChild = iter->second;
  return browserChild.forget();
}

/*static*/
already_AddRefed<BrowserChild> BrowserChild::Create(
    ContentChild* aManager, const TabId& aTabId, const TabContext& aContext,
    BrowsingContext* aBrowsingContext, uint32_t aChromeFlags,
    bool aIsTopLevel) {
  RefPtr<BrowserChild> iframe = new BrowserChild(
      aManager, aTabId, aContext, aBrowsingContext, aChromeFlags, aIsTopLevel);
  return iframe.forget();
}

BrowserChild::BrowserChild(ContentChild* aManager, const TabId& aTabId,
                           const TabContext& aContext,
                           BrowsingContext* aBrowsingContext,
                           uint32_t aChromeFlags, bool aIsTopLevel)
    : TabContext(aContext),
      mBrowserChildMessageManager(nullptr),
      mManager(aManager),
      mBrowsingContext(aBrowsingContext),
      mChromeFlags(aChromeFlags),
      mMaxTouchPoints(0),
      mLayersId{0},
      mEffectsInfo{EffectsInfo::FullyHidden()},
      mDynamicToolbarMaxHeight(0),
      mKeyboardHeight(0),
      mUniqueId(aTabId),
      mDidFakeShow(false),
      mTriedBrowserInit(false),
      mHasValidInnerSize(false),
      mDestroyed(false),
      mInAndroidPipMode(false),
      mIsTopLevel(aIsTopLevel),
      mIsTransparent(false),
      mIPCOpen(false),
      mDidSetRealShowInfo(false),
      mDidLoadURLInit(false),
      mSkipKeyPress(false),
      mShouldSendWebProgressEventsToParent(false),
      mRenderLayers(true),
      mIsPreservingLayers(false),
#if defined(XP_WIN) && defined(ACCESSIBILITY)
      mNativeWindowHandle(0),
#endif
      mCancelContentJSEpoch(0) {
  mozilla::HoldJSObjects(this);

  // preloaded BrowserChild should not be added to child map
  if (mUniqueId) {
    MOZ_ASSERT(NestedBrowserChildMap().find(mUniqueId) ==
               NestedBrowserChildMap().end());
    NestedBrowserChildMap()[mUniqueId] = this;
  }
  mCoalesceMouseMoveEvents = StaticPrefs::dom_events_coalesce_mousemove();
  if (mCoalesceMouseMoveEvents) {
    mCoalescedMouseEventFlusher = new CoalescedMouseMoveFlusher(this);
  }

  if (StaticPrefs::dom_events_coalesce_touchmove()) {
    mCoalescedTouchMoveEventFlusher = new CoalescedTouchMoveFlusher(this);
  }
}

const CompositorOptions& BrowserChild::GetCompositorOptions() const {
  // If you're calling this before mCompositorOptions is set, well.. don't.
  MOZ_ASSERT(mCompositorOptions);
  return mCompositorOptions.ref();
}

bool BrowserChild::AsyncPanZoomEnabled() const {
  // This might get called by the TouchEvent::PrefEnabled code before we have
  // mCompositorOptions populated (bug 1370089). In that case we just assume
  // APZ is enabled because we're in a content process (because BrowserChild)
  // and APZ is probably going to be enabled here since e10s is enabled.
  return mCompositorOptions ? mCompositorOptions->UseAPZ() : true;
}

NS_IMETHODIMP
BrowserChild::Observe(nsISupports* aSubject, const char* aTopic,
                      const char16_t* aData) {
  if (!strcmp(aTopic, BEFORE_FIRST_PAINT)) {
    if (AsyncPanZoomEnabled()) {
      nsCOMPtr<Document> subject(do_QueryInterface(aSubject));
      nsCOMPtr<Document> doc(GetTopLevelDocument());

      if (subject == doc) {
        RefPtr<PresShell> presShell = doc->GetPresShell();
        if (presShell) {
          presShell->SetIsFirstPaint(true);
        }

        APZCCallbackHelper::InitializeRootDisplayport(presShell);
      }
    }
  }

  return NS_OK;
}

void BrowserChild::ContentReceivedInputBlock(uint64_t aInputBlockId,
                                             bool aPreventDefault) const {
  if (mApzcTreeManager) {
    mApzcTreeManager->ContentReceivedInputBlock(aInputBlockId, aPreventDefault);
  }
}

void BrowserChild::SetTargetAPZC(
    uint64_t aInputBlockId,
    const nsTArray<ScrollableLayerGuid>& aTargets) const {
  if (mApzcTreeManager) {
    mApzcTreeManager->SetTargetAPZC(aInputBlockId, aTargets);
  }
}

bool BrowserChild::DoUpdateZoomConstraints(
    const uint32_t& aPresShellId, const ViewID& aViewId,
    const Maybe<ZoomConstraints>& aConstraints) {
  if (!mApzcTreeManager || mDestroyed) {
    return false;
  }

  ScrollableLayerGuid guid =
      ScrollableLayerGuid(mLayersId, aPresShellId, aViewId);

  mApzcTreeManager->UpdateZoomConstraints(guid, aConstraints);
  return true;
}

nsresult BrowserChild::Init(mozIDOMWindowProxy* aParent,
                            WindowGlobalChild* aInitialWindowChild,
                            nsIOpenWindowInfo* aOpenWindowInfo) {
  MOZ_ASSERT(aOpenWindowInfo, "Must have openwindowinfo");
  MOZ_ASSERT(aInitialWindowChild, "Must have window child");
  MOZ_ASSERT(aInitialWindowChild->BrowsingContext() == mBrowsingContext);
  MOZ_ASSERT(aInitialWindowChild->DocumentPrincipal() ==
             aOpenWindowInfo->PrincipalToInheritForAboutBlank());

  nsCOMPtr<nsIWidget> widget = nsIWidget::CreatePuppetWidget(this);
  mPuppetWidget = static_cast<PuppetWidget*>(widget.get());
  if (!mPuppetWidget) {
    NS_ERROR("couldn't create fake widget");
    return NS_ERROR_FAILURE;
  }
  mPuppetWidget->InfallibleCreate(nullptr, LayoutDeviceIntRect(),
                                  widget::InitData());

  MOZ_TRY(nsWebBrowser::Create(this, mPuppetWidget, mBrowsingContext,
                               aInitialWindowChild, aOpenWindowInfo,
                               getter_AddRefs(mWebBrowser)));
  if (!mWebBrowser) {
    // At least the JS recursion depth check can cause an early return
    // here. dom/base/crashtests/1419902.html
    return NS_ERROR_FAILURE;
  }
  nsIWebBrowser* webBrowser = mWebBrowser;

  mWebNav = do_QueryInterface(webBrowser);
  NS_ASSERTION(mWebNav, "nsWebBrowser doesn't implement nsIWebNavigation?");

  // IPC uses a WebBrowser object for which DNS prefetching is turned off
  // by default. But here we really want it, so enable it explicitly
  mWebBrowser->SetAllowDNSPrefetch(true);

  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation());
  MOZ_ASSERT(docShell);

#ifdef DEBUG
  nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(WebNavigation());
  MOZ_ASSERT(loadContext);
  MOZ_ASSERT(loadContext->UseRemoteTabs() ==
             !!(mChromeFlags & nsIWebBrowserChrome::CHROME_REMOTE_WINDOW));
  MOZ_ASSERT(loadContext->UseRemoteSubframes() ==
             !!(mChromeFlags & nsIWebBrowserChrome::CHROME_FISSION_WINDOW));
#endif  // defined(DEBUG)

  // Few lines before, baseWindow->Create() will end up creating a new
  // window root in nsGlobalWindowOuter::SetDocShell.
  // Then this chrome event handler, will be inherited to inner windows.
  // We want to also set it to the docshell so that inner windows
  // and any code that has access to the docshell
  // can all listen to the same chrome event handler.
  // XXX: ideally, we would set a chrome event handler earlier,
  // and all windows, even the root one, will use the docshell one.
  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());
  NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);
  nsCOMPtr<EventTarget> chromeHandler = window->GetChromeEventHandler();
  docShell->SetChromeEventHandler(chromeHandler);

  // Window scrollbar flags only affect top level remote frames, not fission
  // frames.
  if (mIsTopLevel) {
    nsContentUtils::SetScrollbarsVisibility(
        docShell, !!(mChromeFlags & nsIWebBrowserChrome::CHROME_SCROLLBARS));
  }

  nsWeakPtr weakPtrThis = do_GetWeakReference(
      static_cast<nsIBrowserChild*>(this));  // for capture by the lambda
  ContentReceivedInputBlockCallback callback(
      [weakPtrThis](uint64_t aInputBlockId, bool aPreventDefault) {
        if (nsCOMPtr<nsIBrowserChild> browserChild =
                do_QueryReferent(weakPtrThis)) {
          static_cast<BrowserChild*>(browserChild.get())
              ->ContentReceivedInputBlock(aInputBlockId, aPreventDefault);
        }
      });
  mAPZEventState = new APZEventState(mPuppetWidget, std::move(callback));

  mIPCOpen = true;

  if (SessionStorePlatformCollection()) {
    mSessionStoreChild = SessionStoreChild::GetOrCreate(mBrowsingContext);
  }

  // We've all set up, make sure our visibility state is consistent. This is
  // important for OOP iframes, which start off as hidden.
  UpdateVisibility();

  return NS_OK;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(BrowserChild)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(BrowserChild)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBrowserChildMessageManager)
  tmp->nsMessageManagerScriptExecutor::Unlink();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWebBrowser)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWebNav)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBrowsingContext)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSessionStoreChild)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mContentTransformPromise)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(BrowserChild)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBrowserChildMessageManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWebBrowser)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWebNav)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBrowsingContext)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSessionStoreChild)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mContentTransformPromise)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(BrowserChild)
  tmp->nsMessageManagerScriptExecutor::Trace(aCallbacks, aClosure);
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BrowserChild)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(BrowserChild)
  NS_INTERFACE_MAP_ENTRY(nsIWebBrowserChrome)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(nsIWindowProvider)
  NS_INTERFACE_MAP_ENTRY(nsIBrowserChild)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsITooltipListener)
  NS_INTERFACE_MAP_ENTRY(nsIWebProgressListener)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIBrowserChild)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(BrowserChild)
NS_IMPL_CYCLE_COLLECTING_RELEASE(BrowserChild)

NS_IMETHODIMP
BrowserChild::GetChromeFlags(uint32_t* aChromeFlags) {
  *aChromeFlags = mChromeFlags;
  return NS_OK;
}

NS_IMETHODIMP
BrowserChild::SetChromeFlags(uint32_t aChromeFlags) {
  NS_WARNING("trying to SetChromeFlags from content process?");

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
BrowserChild::RemoteDropLinks(
    const nsTArray<RefPtr<nsIDroppedLinkItem>>& aLinks) {
  nsTArray<nsString> linksArray;
  nsresult rv = NS_OK;
  for (nsIDroppedLinkItem* link : aLinks) {
    nsString tmp;
    rv = link->GetUrl(tmp);
    if (NS_FAILED(rv)) {
      return rv;
    }
    linksArray.AppendElement(tmp);

    rv = link->GetName(tmp);
    if (NS_FAILED(rv)) {
      return rv;
    }
    linksArray.AppendElement(tmp);

    rv = link->GetType(tmp);
    if (NS_FAILED(rv)) {
      return rv;
    }
    linksArray.AppendElement(tmp);
  }
  bool sent = SendDropLinks(linksArray);

  return sent ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP
BrowserChild::ShowAsModal() {
  NS_WARNING("BrowserChild::ShowAsModal not supported in BrowserChild");

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
BrowserChild::IsWindowModal(bool* aRetVal) {
  *aRetVal = false;
  return NS_OK;
}

NS_IMETHODIMP
BrowserChild::SetLinkStatus(const nsAString& aStatusText) {
  // We can only send the status after the ipc machinery is set up
  if (IPCOpen()) {
    SendSetLinkStatus(aStatusText);
  }
  return NS_OK;
}

NS_IMETHODIMP
BrowserChild::SetDimensions(DimensionRequest&& aRequest) {
  // The parent is in charge of the dimension changes. If JS code wants to
  // change the dimensions (moveTo, screenX, etc.) we send a message to the
  // parent about the new requested dimension, the parent does the resize/move
  // then send a message to the child to update itself. For APIs like screenX
  // this function is called with only the changed values.  In a series of calls
  // like window.screenX = 10; window.screenY = 10; for the second call, since
  // screenX is not yet updated we might accidentally reset back screenX to it's
  // old value. To avoid this, if a parameter did not change, we want the parent
  // to handle the unchanged values.

  double scale = mPuppetWidget ? mPuppetWidget->GetDefaultScale().scale : 1.0;
  SendSetDimensions(aRequest, scale);
  return NS_OK;
}

NS_IMETHODIMP
BrowserChild::GetDimensions(DimensionKind aDimensionKind, int32_t* aX,
                            int32_t* aY, int32_t* aCx, int32_t* aCy) {
  LayoutDeviceIntRect rect = GetOuterRect();
  if (aDimensionKind == DimensionKind::Inner) {
    if (aX || aY) {
      return NS_ERROR_NOT_IMPLEMENTED;
    }
    rect.SizeTo(GetInnerSize());
  }
  if (aX) {
    *aX = rect.x;
  }
  if (aY) {
    *aY = rect.y;
  }
  if (aCx) {
    *aCx = rect.width;
  }
  if (aCy) {
    *aCy = rect.height;
  }
  return NS_OK;
}

NS_IMETHODIMP
BrowserChild::Blur() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
BrowserChild::GetInterface(const nsIID& aIID, void** aSink) {
  // XXXbz should we restrict the set of interfaces we hand out here?
  // See bug 537429
  return QueryInterface(aIID, aSink);
}

NS_IMETHODIMP
BrowserChild::ProvideWindow(nsIOpenWindowInfo* aOpenWindowInfo,
                            uint32_t aChromeFlags, bool aCalledFromJS,
                            nsIURI* aURI, const nsAString& aName,
                            const nsACString& aFeatures,
                            const UserActivation::Modifiers& aModifiers,
                            bool aForceNoOpener, bool aForceNoReferrer,
                            bool aIsPopupRequested,
                            nsDocShellLoadState* aLoadState, bool* aWindowIsNew,
                            BrowsingContext** aReturn) {
  *aReturn = nullptr;

  RefPtr<BrowsingContext> parent = aOpenWindowInfo->GetParent();

  int32_t openLocation = nsWindowWatcher::GetWindowOpenLocation(
      parent->GetDOMWindow(), aChromeFlags, aModifiers, aCalledFromJS,
      aOpenWindowInfo->GetIsForPrinting());

  // If it turns out we're opening in the current browser, just hand over the
  // current browser's docshell.
  if (openLocation == nsIBrowserDOMWindow::OPEN_CURRENTWINDOW) {
    nsCOMPtr<nsIWebBrowser> browser = do_GetInterface(WebNavigation());
    *aWindowIsNew = false;

    nsCOMPtr<mozIDOMWindowProxy> win;
    MOZ_TRY(browser->GetContentDOMWindow(getter_AddRefs(win)));

    RefPtr<BrowsingContext> bc(
        nsPIDOMWindowOuter::From(win)->GetBrowsingContext());
    bc.forget(aReturn);
    return NS_OK;
  }

  // Note that ProvideWindowCommon may return NS_ERROR_ABORT if the
  // open window call was canceled.  It's important that we pass this error
  // code back to our caller.
  ContentChild* cc = ContentChild::GetSingleton();
  return cc->ProvideWindowCommon(
      WrapNotNull(this), aOpenWindowInfo, aChromeFlags, aCalledFromJS, aURI,
      aName, aFeatures, aModifiers, aForceNoOpener, aForceNoReferrer,
      aIsPopupRequested, aLoadState, aWindowIsNew, aReturn);
}

void BrowserChild::DestroyWindow() {
  mBrowsingContext = nullptr;

  if (mCoalescedMouseEventFlusher) {
    mCoalescedMouseEventFlusher->RemoveObserver();
    mCoalescedMouseEventFlusher = nullptr;
  }

  if (mCoalescedTouchMoveEventFlusher) {
    mCoalescedTouchMoveEventFlusher->RemoveObserver();
    mCoalescedTouchMoveEventFlusher = nullptr;
  }

  if (mSessionStoreChild) {
    mSessionStoreChild->Stop();
    mSessionStoreChild = nullptr;
  }

  // In case we don't have chance to process all entries, clean all data in
  // the queue.
  while (mToBeDispatchedMouseData.GetSize() > 0) {
    UniquePtr<CoalescedMouseData> data(
        static_cast<CoalescedMouseData*>(mToBeDispatchedMouseData.PopFront()));
    data.reset();
  }

  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(WebNavigation());
  if (baseWindow) baseWindow->Destroy();

  if (mPuppetWidget) {
    mPuppetWidget->Destroy();
  }

  mLayersConnected = Nothing();

  if (mLayersId.IsValid()) {
    StaticMutexAutoLock lock(sBrowserChildrenMutex);

    MOZ_ASSERT(sBrowserChildren);
    sBrowserChildren->Remove(uint64_t(mLayersId));
    if (!sBrowserChildren->Count()) {
      delete sBrowserChildren;
      sBrowserChildren = nullptr;
    }
    mLayersId = layers::LayersId{0};
  }

  if (mAPZEventState) {
    mAPZEventState->Destroy();
    mAPZEventState = nullptr;
  }
}

void BrowserChild::ActorDestroy(ActorDestroyReason why) {
  mIPCOpen = false;

  DestroyWindow();

  if (mBrowserChildMessageManager) {
    // We should have a message manager if the global is alive, but it
    // seems sometimes we don't.  Assert in aurora/nightly, but don't
    // crash in release builds.
    MOZ_DIAGNOSTIC_ASSERT(mBrowserChildMessageManager->GetMessageManager());
    if (mBrowserChildMessageManager->GetMessageManager()) {
      // The messageManager relays messages via the BrowserChild which
      // no longer exists.
      mBrowserChildMessageManager->DisconnectMessageManager();
    }
  }

  if (GetTabId() != 0) {
    NestedBrowserChildMap().erase(GetTabId());
  }
}

BrowserChild::~BrowserChild() {
  mAnonymousGlobalScopes.Clear();

  DestroyWindow();

  nsCOMPtr<nsIWebBrowser> webBrowser = do_QueryInterface(WebNavigation());
  if (webBrowser) {
    webBrowser->SetContainerWindow(nullptr);
  }

  mozilla::DropJSObjects(this);
}

mozilla::ipc::IPCResult BrowserChild::RecvWillChangeProcess() {
  if (mWebBrowser) {
    mWebBrowser->SetWillChangeProcess();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvLoadURL(
    nsDocShellLoadState* aLoadState, const ParentShowInfo& aInfo) {
  if (!mDidLoadURLInit) {
    mDidLoadURLInit = true;
    if (!InitBrowserChildMessageManager()) {
      return IPC_FAIL_NO_REASON(this);
    }

    ApplyParentShowInfo(aInfo);
  }
  nsAutoCString spec;
  aLoadState->URI()->GetSpec(spec);

  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation());
  if (!docShell) {
    NS_WARNING("WebNavigation does not have a docshell");
    return IPC_OK();
  }
  docShell->LoadURI(aLoadState, true);

  CrashReporter::RecordAnnotationNSCString(CrashReporter::Annotation::URL,
                                           spec);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvCreateAboutBlankDocumentViewer(
    nsIPrincipal* aPrincipal, nsIPrincipal* aPartitionedPrincipal) {
  if (aPrincipal->GetIsExpandedPrincipal() ||
      aPartitionedPrincipal->GetIsExpandedPrincipal()) {
    return IPC_FAIL(this, "Cannot create document with an expanded principal");
  }
  if (aPrincipal->IsSystemPrincipal() ||
      aPartitionedPrincipal->IsSystemPrincipal()) {
    MOZ_ASSERT_UNREACHABLE(
        "Cannot use CreateAboutBlankDocumentViewer to create system principal "
        "document in content");
    return IPC_OK();
  }

  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation());
  if (!docShell) {
    MOZ_ASSERT_UNREACHABLE("WebNavigation does not have a docshell");
    return IPC_OK();
  }

  nsCOMPtr<nsIURI> currentURI;
  MOZ_ALWAYS_SUCCEEDS(
      WebNavigation()->GetCurrentURI(getter_AddRefs(currentURI)));
  if (!currentURI || !NS_IsAboutBlank(currentURI)) {
    NS_WARNING("Can't create a DocumentViewer unless on about:blank");
    return IPC_OK();
  }

  docShell->CreateAboutBlankDocumentViewer(aPrincipal, aPartitionedPrincipal,
                                           nullptr);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvResumeLoad(
    const uint64_t& aPendingSwitchID, const ParentShowInfo& aInfo) {
  if (!mDidLoadURLInit) {
    mDidLoadURLInit = true;
    if (!InitBrowserChildMessageManager()) {
      return IPC_FAIL_NO_REASON(this);
    }

    ApplyParentShowInfo(aInfo);
  }

  nsresult rv = WebNavigation()->ResumeRedirectedLoad(aPendingSwitchID);
  if (NS_FAILED(rv)) {
    NS_WARNING("WebNavigation()->ResumeRedirectedLoad failed");
  }

  return IPC_OK();
}

nsresult BrowserChild::CloneDocumentTreeIntoSelf(
    const MaybeDiscarded<BrowsingContext>& aSourceBC,
    const embedding::PrintData& aPrintData) {
#ifdef NS_PRINTING
  if (NS_WARN_IF(aSourceBC.IsNullOrDiscarded())) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<Document> sourceDocument = aSourceBC.get()->GetDocument();
  if (NS_WARN_IF(!sourceDocument)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDocShell> ourDocShell = do_GetInterface(WebNavigation());
  if (NS_WARN_IF(!ourDocShell)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDocumentViewer> viewer;
  ourDocShell->GetDocViewer(getter_AddRefs(viewer));
  if (NS_WARN_IF(!viewer)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrintSettingsService> printSettingsSvc =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (NS_WARN_IF(!printSettingsSvc)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrintSettings> printSettings;
  nsresult rv =
      printSettingsSvc->CreateNewPrintSettings(getter_AddRefs(printSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  printSettingsSvc->DeserializeToPrintSettings(aPrintData, printSettings);

  RefPtr<Document> clone;
  {
    AutoPrintEventDispatcher dispatcher(*sourceDocument);
    nsAutoScriptBlocker scriptBlocker;
    bool hasInProcessCallbacks = false;
    clone = sourceDocument->CreateStaticClone(
        ourDocShell, viewer, printSettings, &hasInProcessCallbacks);
    if (NS_WARN_IF(!clone)) {
      return NS_ERROR_FAILURE;
    }
  }

  rv = UpdateRemotePrintSettings(aPrintData);
  if (NS_FAILED(rv)) {
    return rv;
  }

#endif
  return NS_OK;
}

mozilla::ipc::IPCResult BrowserChild::RecvCloneDocumentTreeIntoSelf(
    const MaybeDiscarded<BrowsingContext>& aSourceBC,
    const embedding::PrintData& aPrintData,
    CloneDocumentTreeIntoSelfResolver&& aResolve) {
  nsresult rv = NS_OK;

#ifdef NS_PRINTING
  rv = CloneDocumentTreeIntoSelf(aSourceBC, aPrintData);
#endif

  aResolve(NS_SUCCEEDED(rv));
  return IPC_OK();
}

nsresult BrowserChild::UpdateRemotePrintSettings(
    const embedding::PrintData& aPrintData) {
#ifdef NS_PRINTING
  nsCOMPtr<nsIDocShell> ourDocShell = do_GetInterface(WebNavigation());
  if (NS_WARN_IF(!ourDocShell)) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<Document> doc = ourDocShell->GetExtantDocument();
  if (NS_WARN_IF(!doc) || NS_WARN_IF(!doc->IsStaticDocument())) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<BrowsingContext> bc = ourDocShell->GetBrowsingContext();
  if (NS_WARN_IF(!bc)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrintSettingsService> printSettingsSvc =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (NS_WARN_IF(!printSettingsSvc)) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrintSettings> printSettings;
  nsresult rv =
      printSettingsSvc->CreateNewPrintSettings(getter_AddRefs(printSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  printSettingsSvc->DeserializeToPrintSettings(aPrintData, printSettings);

  bc->PreOrderWalk([&](BrowsingContext* aBc) {
    if (nsCOMPtr<nsIDocShell> inProcess = aBc->GetDocShell()) {
      nsCOMPtr<nsIDocumentViewer> viewer;
      inProcess->GetDocViewer(getter_AddRefs(viewer));
      if (NS_WARN_IF(!viewer)) {
        return BrowsingContext::WalkFlag::Skip;
      }
      // The CanRunScript analysis is not smart enough to see across
      // the std::function PreOrderWalk uses, so we cheat a bit here, but it is
      // fine because PreOrderWalk does deal with arbitrary script changing the
      // BC tree, and our code above is simple enough and keeps strong refs to
      // everything.
      ([&]() MOZ_CAN_RUN_SCRIPT_BOUNDARY {
        RefPtr<RemotePrintJobChild> printJob =
            static_cast<RemotePrintJobChild*>(
                aPrintData.remotePrintJob().AsChild());
        viewer->SetPrintSettingsForSubdocument(printSettings, printJob);
      }());
    } else if (RefPtr<BrowserBridgeChild> remoteChild =
                   BrowserBridgeChild::GetFrom(aBc->GetEmbedderElement())) {
      (void)remoteChild->SendUpdateRemotePrintSettings(aPrintData);
      return BrowsingContext::WalkFlag::Skip;
    }
    return BrowsingContext::WalkFlag::Next;
  });
#endif

  return NS_OK;
}

mozilla::ipc::IPCResult BrowserChild::RecvUpdateRemotePrintSettings(
    const embedding::PrintData& aPrintData) {
#ifdef NS_PRINTING
  UpdateRemotePrintSettings(aPrintData);
#endif

  return IPC_OK();
}

void BrowserChild::DoFakeShow(const ParentShowInfo& aParentShowInfo) {
  OwnerShowInfo ownerInfo{LayoutDeviceIntSize(), ScrollbarPreference::Auto,
                          nsSizeMode_Normal};
  RecvShow(aParentShowInfo, ownerInfo);
  mDidFakeShow = true;
}

void BrowserChild::ApplyParentShowInfo(const ParentShowInfo& aInfo) {
  // Even if we already set real show info, the dpi / rounding & scale may still
  // be invalid (if BrowserParent wasn't able to get widget it would just send
  // 0). So better to always set up-to-date values here.
  if (aInfo.dpi() > 0) {
    mPuppetWidget->UpdateBackingScaleCache(aInfo.dpi(), aInfo.widgetRounding(),
                                           aInfo.defaultScale(),
                                           aInfo.desktopToDeviceScale());
  }

  if (mDidSetRealShowInfo) {
    return;
  }

  if (!aInfo.fakeShowInfo()) {
    // Once we've got one ShowInfo from parent, no need to update the values
    // anymore.
    mDidSetRealShowInfo = true;
  }

  mIsTransparent = aInfo.isTransparent();
}

mozilla::ipc::IPCResult BrowserChild::RecvShow(
    const ParentShowInfo& aParentInfo, const OwnerShowInfo& aOwnerInfo) {
  bool res = true;

  mPuppetWidget->SetSizeMode(aOwnerInfo.sizeMode());
  if (!mDidFakeShow) {
    nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(WebNavigation());
    if (!baseWindow) {
      NS_ERROR("WebNavigation() doesn't QI to nsIBaseWindow");
      return IPC_FAIL_NO_REASON(this);
    }

    baseWindow->SetVisibility(true);
    res = InitBrowserChildMessageManager();
  }

  ApplyParentShowInfo(aParentInfo);

  if (!mIsTopLevel) {
    RecvScrollbarPreferenceChanged(aOwnerInfo.scrollbarPreference());
  }

  if (!res) {
    return IPC_FAIL_NO_REASON(this);
  }

  UpdateVisibility();

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvInitRendering(
    const TextureFactoryIdentifier& aTextureFactoryIdentifier,
    const layers::LayersId& aLayersId,
    const CompositorOptions& aCompositorOptions, const bool& aLayersConnected) {
  mLayersConnected = Some(aLayersConnected);
  mLayersConnectRequested = Some(aLayersConnected);
  InitRenderingState(aTextureFactoryIdentifier, aLayersId, aCompositorOptions);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvScrollbarPreferenceChanged(
    ScrollbarPreference aPreference) {
  MOZ_ASSERT(!mIsTopLevel,
             "Scrollbar visibility should be derived from chrome flags for "
             "top-level windows");
  if (nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation())) {
    nsDocShell::Cast(docShell)->SetScrollbarPreference(aPreference);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvCompositorOptionsChanged(
    const CompositorOptions& aNewOptions) {
  MOZ_ASSERT(mCompositorOptions);

  // The only compositor option we currently support changing is APZ
  // enablement. Even that is only partially supported for now:
  //   * Going from APZ to non-APZ is fine - we just flip the stored flag.
  //     Note that we keep the actors (mApzcTreeManager, and the APZChild
  //     created in InitAPZState()) around (read on for why).
  //   * Going from non-APZ to APZ is only supported if we were using
  //     APZ initially (at InitRendering() time) and we are transitioning
  //     back. In this case, we just reuse the actors which we kept around.
  //     Fully supporting a non-APZ to APZ transition (i.e. even in cases
  //     where we initialized as non-APZ) would require setting up the actors
  //     here. (In that case, we would also have the options of destroying
  //     the actors in the APZ --> non-APZ case, and always re-creating them
  //     during a non-APZ --> APZ transition).
  mCompositorOptions->SetUseAPZ(aNewOptions.UseAPZ());
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvUpdateDimensions(
    const DimensionInfo& aDimensionInfo) {
  if (mLayersConnected.isNothing()) {
    return IPC_OK();
  }

  mUnscaledOuterRect = aDimensionInfo.rect();
  mClientOffset = aDimensionInfo.clientOffset();
  mChromeOffset = aDimensionInfo.chromeOffset();
  MOZ_ASSERT_IF(!IsTopLevel(), mChromeOffset == LayoutDeviceIntPoint());

  SetUnscaledInnerSize(aDimensionInfo.size());
  if (!mHasValidInnerSize && aDimensionInfo.size().width != 0 &&
      aDimensionInfo.size().height != 0) {
    mHasValidInnerSize = true;
  }

  const LayoutDeviceIntSize innerSize = GetInnerSize();
  // Make sure to set the size on the document viewer first.  The
  // MobileViewportManager needs the content viewer size to be updated before
  // the reflow, otherwise it gets a stale size when it computes a new CSS
  // viewport.
  nsCOMPtr<nsIBaseWindow> baseWin = do_QueryInterface(WebNavigation());
  baseWin->SetPositionAndSize(0, 0, innerSize.width, innerSize.height,
                              nsIBaseWindow::eRepaint);

  const LayoutDeviceIntRect widgetRect(
      GetOuterRect().TopLeft() + mClientOffset + mChromeOffset, innerSize);
  mPuppetWidget->Resize(widgetRect / mPuppetWidget->GetDesktopToDeviceScale(),
                        true);

  RecvSafeAreaInsetsChanged(mPuppetWidget->GetSafeAreaInsets());

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvSizeModeChanged(
    const nsSizeMode& aSizeMode) {
  mPuppetWidget->SetSizeMode(aSizeMode);
  if (!mPuppetWidget->IsVisible()) {
    return IPC_OK();
  }
  nsCOMPtr<Document> document(GetTopLevelDocument());
  if (!document) {
    return IPC_OK();
  }
  nsPresContext* presContext = document->GetPresContext();
  if (presContext) {
    presContext->SizeModeChanged(aSizeMode);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvChildToParentMatrix(
    const Maybe<gfx::Matrix4x4>& aMatrix,
    const ScreenRect& aTopLevelViewportVisibleRectInBrowserCoords) {
  mChildToParentConversionMatrix =
      LayoutDeviceToLayoutDeviceMatrix4x4::FromUnknownMatrix(aMatrix);
  mTopLevelViewportVisibleRectInBrowserCoords =
      aTopLevelViewportVisibleRectInBrowserCoords;

  if (mContentTransformPromise) {
    mContentTransformPromise->MaybeResolveWithUndefined();
    mContentTransformPromise = nullptr;
  }

  // Trigger an intersection observation update since ancestor viewports
  // changed.
  if (RefPtr<Document> toplevelDoc = GetTopLevelDocument()) {
    if (nsPresContext* pc = toplevelDoc->GetPresContext()) {
      pc->RefreshDriver()->EnsureIntersectionObservationsUpdateHappens();
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvUpdateRemoteStyle(
    const StyleImageRendering& aImageRendering) {
  BrowsingContext* context = GetBrowsingContext();
  if (!context) {
    return IPC_OK();
  }

  Document* document = context->GetDocument();
  if (!document) {
    return IPC_OK();
  }

  if (document->IsImageDocument()) {
    document->AsImageDocument()->UpdateRemoteStyle(aImageRendering);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvDynamicToolbarMaxHeightChanged(
    const ScreenIntCoord& aHeight) {
  mDynamicToolbarMaxHeight = aHeight;

  RefPtr<Document> document = GetTopLevelDocument();
  if (!document) {
    return IPC_OK();
  }

  if (RefPtr<nsPresContext> presContext = document->GetPresContext()) {
    presContext->SetDynamicToolbarMaxHeight(aHeight);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvDynamicToolbarOffsetChanged(
    const ScreenIntCoord& aOffset) {
  RefPtr<Document> document = GetTopLevelDocument();
  if (!document) {
    return IPC_OK();
  }

  if (nsPresContext* presContext = document->GetPresContext()) {
    presContext->UpdateDynamicToolbarOffset(aOffset);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvKeyboardHeightChanged(
    const ScreenIntCoord& aHeight) {
#if defined(MOZ_WIDGET_ANDROID)
  mKeyboardHeight = aHeight;

  RefPtr<Document> document = GetTopLevelDocument();
  if (!document) {
    return IPC_OK();
  }

  if (nsPresContext* presContext = document->GetPresContext()) {
    presContext->UpdateKeyboardHeight(aHeight);
  }
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvAndroidPipModeChanged(bool aPipMode) {
  if (mInAndroidPipMode == aPipMode) {
    return IPC_OK();
  }
  mInAndroidPipMode = aPipMode;
  if (RefPtr<Document> document = GetTopLevelDocument()) {
    if (nsPresContext* presContext = document->GetPresContext()) {
      presContext->MediaFeatureValuesChanged(
          {MediaFeatureChangeReason::DisplayModeChange},
          MediaFeatureChangePropagation::JustThisDocument);
    }
    nsContentUtils::DispatchEventOnlyToChrome(
        document, document,
        aPipMode ? u"MozAndroidPipModeEntered"_ns
                 : u"MozAndroidPipModeExited"_ns,
        CanBubble::eYes, Cancelable::eNo, /* DefaultAction */ nullptr);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvSuppressDisplayport(
    const bool& aEnabled) {
  if (RefPtr<PresShell> presShell = GetTopLevelPresShell()) {
    presShell->SuppressDisplayport(aEnabled);
  }
  return IPC_OK();
}

void BrowserChild::HandleDoubleTap(const CSSPoint& aPoint,
                                   const Modifiers& aModifiers,
                                   const ScrollableLayerGuid& aGuid,
                                   const DoubleTapToZoomMetrics& aMetrics) {
  MOZ_LOG(
      sApzChildLog, LogLevel::Debug,
      ("Handling double tap at %s with %p %p\n", ToString(aPoint).c_str(),
       mBrowserChildMessageManager ? mBrowserChildMessageManager->GetWrapper()
                                   : nullptr,
       mBrowserChildMessageManager.get()));

  if (!mBrowserChildMessageManager) {
    return;
  }

  // Note: there is nothing to do with the modifiers here, as we are not
  // synthesizing any sort of mouse event.
  RefPtr<Document> document = GetTopLevelDocument();
  ZoomTarget zoomTarget = CalculateRectToZoomTo(document, aPoint, aMetrics);
  // The double-tap can be dispatched by any scroll frame (so |aGuid| could be
  // the guid of any scroll frame), but the zoom-to-rect operation must be
  // performed by the root content scroll frame, so query its identifiers
  // for the SendZoomToRect() call rather than using the ones from |aGuid|.
  uint32_t presShellId;
  ViewID viewId;
  if (APZCCallbackHelper::GetOrCreateScrollIdentifiers(
          document->GetDocumentElement(), &presShellId, &viewId) &&
      mApzcTreeManager) {
    ScrollableLayerGuid guid(mLayersId, presShellId, viewId);

    mApzcTreeManager->ZoomToRect(guid, zoomTarget,
                                 ZoomToRectBehavior::DEFAULT_BEHAVIOR);
  }
}

mozilla::ipc::IPCResult BrowserChild::RecvHandleTap(
    const GeckoContentController::TapType& aType,
    const LayoutDevicePoint& aPoint, const Modifiers& aModifiers,
    const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId,
    const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) {
  // IPDL doesn't hold a strong reference to protocols as they're not required
  // to be refcounted. This function can run script, which may trigger a nested
  // event loop, which may release this, so we hold a strong reference here.
  RefPtr<BrowserChild> kungFuDeathGrip(this);
  RefPtr<PresShell> presShell = GetTopLevelPresShell();
  if (!presShell || !presShell->GetPresContext() || !mAPZEventState) {
    return IPC_OK();
  }
  CSSToLayoutDeviceScale scale(
      presShell->GetPresContext()->CSSToDevPixelScale());
  CSSPoint point = aPoint / scale;

  // Stash the guid in InputAPZContext so that when the visual-to-layout
  // transform is applied to the event's coordinates, we use the right transform
  // based on the scroll frame being targeted.
  // The other values don't really matter.
  InputAPZContext context(aGuid, aInputBlockId, nsEventStatus_eSentinel);

  switch (aType) {
    case GeckoContentController::TapType::eSingleTap:
      if (mBrowserChildMessageManager) {
        RefPtr<APZEventState> eventState(mAPZEventState);
        eventState->ProcessSingleTap(point, scale, aModifiers, 1,
                                     aInputBlockId);
      }
      break;
    case GeckoContentController::TapType::eDoubleTap:
      HandleDoubleTap(point, aModifiers, aGuid, *aDoubleTapToZoomMetrics);
      break;
    case GeckoContentController::TapType::eSecondTap:
      if (mBrowserChildMessageManager) {
        RefPtr<APZEventState> eventState(mAPZEventState);
        eventState->ProcessSingleTap(point, scale, aModifiers, 2,
                                     aInputBlockId);
      }
      break;
    case GeckoContentController::TapType::eLongTap:
      if (mBrowserChildMessageManager) {
        RefPtr<APZEventState> eventState(mAPZEventState);
        eventState->ProcessLongTap(presShell, point, scale, aModifiers,
                                   aInputBlockId);
      }
      break;
    case GeckoContentController::TapType::eLongTapUp:
      if (mBrowserChildMessageManager) {
        RefPtr<APZEventState> eventState(mAPZEventState);
        eventState->ProcessLongTapUp(presShell, point, scale, aModifiers);
      }
      break;
  }

  // mAPZEventState may not dispatch the compatibility mouse events.  Therefore,
  // we should release the pointer capturing element at the last ePointerUp
  // here.
  PointerEventHandler::ReleasePointerCapturingElementAtLastPointerUp();

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityHandleTap(
    const GeckoContentController::TapType& aType,
    const LayoutDevicePoint& aPoint, const Modifiers& aModifiers,
    const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId,
    const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics) {
  // IPDL doesn't hold a strong reference to protocols as they're not required
  // to be refcounted. This function can run script, which may trigger a nested
  // event loop, which may release this, so we hold a strong reference here.
  RefPtr<BrowserChild> kungFuDeathGrip(this);
  return RecvHandleTap(aType, aPoint, aModifiers, aGuid, aInputBlockId,
                       aDoubleTapToZoomMetrics);
}

void BrowserChild::NotifyAPZStateChange(
    const ViewID& aViewId,
    const layers::GeckoContentController::APZStateChange& aChange,
    const int& aArg, Maybe<uint64_t> aInputBlockId) {
  if (mAPZEventState) {
    mAPZEventState->ProcessAPZStateChange(aViewId, aChange, aArg,
                                          aInputBlockId);
  }
  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (aChange ==
      layers::GeckoContentController::APZStateChange::eTransformEnd) {
    // This is used by tests to determine when the APZ is done doing whatever
    // it's doing. XXX generify this as needed when writing additional tests.
    observerService->NotifyObservers(nullptr, "APZ:TransformEnd", nullptr);
    observerService->NotifyObservers(nullptr, "PanZoom:StateChange",
                                     u"NOTHING");
  } else if (aChange ==
             layers::GeckoContentController::APZStateChange::eTransformBegin) {
    observerService->NotifyObservers(nullptr, "PanZoom:StateChange",
                                     u"PANNING");
  }
}

void BrowserChild::StartScrollbarDrag(
    const layers::AsyncDragMetrics& aDragMetrics) {
  ScrollableLayerGuid guid(mLayersId, aDragMetrics.mPresShellId,
                           aDragMetrics.mViewId);

  if (mApzcTreeManager) {
    mApzcTreeManager->StartScrollbarDrag(guid, aDragMetrics);
  }
}

void BrowserChild::ZoomToRect(const uint32_t& aPresShellId,
                              const ScrollableLayerGuid::ViewID& aViewId,
                              const CSSRect& aRect, const uint32_t& aFlags) {
  ScrollableLayerGuid guid(mLayersId, aPresShellId, aViewId);

  if (mApzcTreeManager) {
    mApzcTreeManager->ZoomToRect(guid, ZoomTarget{aRect}, aFlags);
  }
}

mozilla::ipc::IPCResult BrowserChild::RecvActivate(uint64_t aActionId) {
  MOZ_ASSERT(mWebBrowser);
  mWebBrowser->FocusActivate(aActionId);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvDeactivate(uint64_t aActionId) {
  MOZ_ASSERT(mWebBrowser);
  mWebBrowser->FocusDeactivate(aActionId);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvStopIMEStateManagement() {
  IMEStateManager::StopIMEStateManagement();
  return IPC_OK();
}

void BrowserChild::ProcessPendingCoalescedTouchData() {
  MOZ_ASSERT(StaticPrefs::dom_events_coalesce_touchmove());

  if (mCoalescedTouchData.IsEmpty()) {
    return;
  }

  if (mCoalescedTouchMoveEventFlusher) {
    mCoalescedTouchMoveEventFlusher->RemoveObserver();
  }

  UniquePtr<WidgetTouchEvent> touchMoveEvent =
      mCoalescedTouchData.TakeCoalescedEvent();
  (void)RecvRealTouchEvent(*touchMoveEvent,
                           mCoalescedTouchData.GetScrollableLayerGuid(),
                           mCoalescedTouchData.GetInputBlockId(),
                           mCoalescedTouchData.GetApzResponse());
}

void BrowserChild::ProcessPendingCoalescedMouseDataAndDispatchEvents() {
  if (!mCoalesceMouseMoveEvents || !mCoalescedMouseEventFlusher) {
    // We don't enable mouse coalescing or we are destroying BrowserChild.
    return;
  }

  // We may reentry the event loop and push more data to
  // mToBeDispatchedMouseData while dispatching an event.

  // We may have some pending coalesced data while dispatch an event and reentry
  // the event loop. In that case we don't have chance to consume the remaining
  // pending data until we get new mouse events. Get some helps from
  // mCoalescedMouseEventFlusher to trigger it.
  mCoalescedMouseEventFlusher->StartObserver();

  while (mToBeDispatchedMouseData.GetSize() > 0) {
    UniquePtr<CoalescedMouseData> data(
        static_cast<CoalescedMouseData*>(mToBeDispatchedMouseData.PopFront()));

    if (const UniquePtr<WidgetMouseEvent> mouseOrPointerEvent =
            data->TakeCoalescedEvent()) {
      MOZ_ASSERT_IF(mouseOrPointerEvent->AsPointerEvent(),
                    IsPointerEventMessage(mouseOrPointerEvent->mMessage));
      MOZ_ASSERT_IF(!mouseOrPointerEvent->AsPointerEvent(),
                    !IsPointerEventMessage(mouseOrPointerEvent->mMessage));
      // When the real mouse event receivers put the received event into the
      // queue, they should dispatch eMouseRawUpdate event immediately (if and
      // only if it's required).  Therefore, unless the event is the last one
      // of the queue, the pending events should've been marked as "Do not
      // convert to "pointerrawupdate".
      MOZ_ASSERT_IF(mToBeDispatchedMouseData.GetSize() > 0,
                    !mouseOrPointerEvent->convertToPointerRawUpdate);
      // Dispatch the pending events. Using HandleRealMouseButtonEvent
      // to bypass the coalesce handling in RecvRealMouseMoveEvent. Can't use
      // RecvRealMouseButtonEvent because we may also put some mouse events
      // other than mousemove.
      HandleRealMouseButtonEvent(*mouseOrPointerEvent,
                                 data->GetScrollableLayerGuid(),
                                 data->GetInputBlockId());
    }
  }
  // mCoalescedMouseEventFlusher may be destroyed when reentrying the event
  // loop.
  if (mCoalescedMouseEventFlusher) {
    mCoalescedMouseEventFlusher->RemoveObserver();
  }
}

LayoutDeviceToLayoutDeviceMatrix4x4
BrowserChild::GetChildToParentConversionMatrix() const {
  if (mChildToParentConversionMatrix) {
    return *mChildToParentConversionMatrix;
  }
  LayoutDevicePoint offset(GetChromeOffset());
  return LayoutDeviceToLayoutDeviceMatrix4x4::Translation(offset);
}

Maybe<ScreenRect> BrowserChild::GetTopLevelViewportVisibleRectInBrowserCoords()
    const {
  if (!mChildToParentConversionMatrix) {
    return Nothing();
  }
  return Some(mTopLevelViewportVisibleRectInBrowserCoords);
}

void BrowserChild::FlushAllCoalescedMouseData() {
  MOZ_ASSERT(mCoalesceMouseMoveEvents);

  // Move all entries from mCoalescedMouseData to mToBeDispatchedMouseData.
  for (const auto& data : mCoalescedMouseData.Values()) {
    if (!data || data->IsEmpty()) {
      continue;
    }
    UniquePtr<CoalescedMouseData> dispatchData =
        MakeUnique<CoalescedMouseData>();

    dispatchData->RetrieveDataFrom(*data);
    mToBeDispatchedMouseData.Push(dispatchData.release());
  }
  mCoalescedMouseData.Clear();
}

mozilla::ipc::IPCResult BrowserChild::RecvRealMouseMoveEvent(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  MOZ_ASSERT(!aMouseEvent.AsPointerEvent());
  MOZ_ASSERT(!aMouseEvent.AsDragEvent());
  if (mCoalesceMouseMoveEvents && mCoalescedMouseEventFlusher) {
    CoalescedMouseData* data =
        mCoalescedMouseData.GetOrInsertNew(aMouseEvent.pointerId);
    MOZ_ASSERT(data);
    if (data->CanCoalesce(aMouseEvent, aGuid, aInputBlockId,
                          mCoalescedMouseEventFlusher->GetRefreshDriver())) {
      // Callback doesn't support if the event is coalesced.
      MOZ_ASSERT_IF(!data->IsEmpty(), aMouseEvent.mCallbackId.isNothing());

      // We don't need to dispatch aMouseEvent immediately.  However, we need to
      // dispatch eMouseRawUpdate immediately if there is a `pointerrawupdate`
      // event listener.  Therefore, the cloned event in the queue shouldn't
      // cause eMouseRawUpdate later when it'll be dispatched.
      WidgetMouseEvent pendingMouseMoveEvent(aMouseEvent);
      // We don't want to dispatch aEvent immediately, so the cloned event
      // should track the callback id. And the callback id of cloned event will
      // be moved again if there is no coalesced data yet when coalescing.
      pendingMouseMoveEvent.mCallbackId = aMouseEvent.mCallbackId;
      pendingMouseMoveEvent.convertToPointerRawUpdate = false;
      data->Coalesce(pendingMouseMoveEvent, aGuid, aInputBlockId);
      mCoalescedMouseEventFlusher->StartObserver();
      HandleMouseRawUpdateEvent(pendingMouseMoveEvent, aGuid, aInputBlockId);
      return IPC_OK();
    }

    // Can't coalesce current mousemove event. Put the coalesced mousemove data
    // with the same pointer id to mToBeDispatchedMouseData, coalesce the
    // current one, and process all pending data in mToBeDispatchedMouseData.
    UniquePtr<CoalescedMouseData> dispatchData =
        MakeUnique<CoalescedMouseData>();

    dispatchData->RetrieveDataFrom(*data);
    mToBeDispatchedMouseData.Push(dispatchData.release());

    // Put new data to replace the old one in the hash table.
    CoalescedMouseData* newData =
        mCoalescedMouseData
            .InsertOrUpdate(aMouseEvent.pointerId,
                            MakeUnique<CoalescedMouseData>())
            .get();
    // We don't want to dispatch aEvent immediately.  However, we need to
    // dispatch eMouseRawUpdate immediately if there is a `pointerrawupdate`
    // event listener.  Therefore, the cloned event in the queue shouldn't
    // cause eMouseRawUpdate later when it'll be dispatched.
    WidgetMouseEvent pendingMouseMoveEvent(aMouseEvent);
    // The cloned event should track the cllback id. And the callback id of
    // cloned event will be moved again if there is no coalesced data yet when
    // coalescing.
    pendingMouseMoveEvent.mCallbackId = std::move(aMouseEvent.mCallbackId);
    pendingMouseMoveEvent.convertToPointerRawUpdate = false;
    newData->Coalesce(pendingMouseMoveEvent, aGuid, aInputBlockId);

    // Dispatch all pending mouse events which does NOT include aEvent.
    ProcessPendingCoalescedMouseDataAndDispatchEvents();

    mCoalescedMouseEventFlusher->StartObserver();
    // Finally, dispatch eMouseRawUpdate for aEvent right now.
    HandleMouseRawUpdateEvent(pendingMouseMoveEvent, aGuid, aInputBlockId);
    return IPC_OK();
  }

  if (!RecvRealMouseButtonEvent(aMouseEvent, aGuid, aInputBlockId)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

void BrowserChild::HandleMouseRawUpdateEvent(
    const WidgetMouseEvent& aPendingMouseEvent,
    const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId) {
  MOZ_ASSERT(!aPendingMouseEvent.AsPointerEvent());
  MOZ_ASSERT(!aPendingMouseEvent.AsDragEvent());
  // If there is no window containing pointerrawupdate event listeners or the
  // event is a synthesized mousemove, we don't need to dispatch eMouseRawUpdate
  // event.
  if (!mPointerRawUpdateWindowCount || aPendingMouseEvent.IsSynthesized()) {
    return;
  }
  WidgetMouseEvent mouseRawUpdateEvent(aPendingMouseEvent);
  mouseRawUpdateEvent.mMessage = eMouseRawUpdate;
  // PointerEvent.button should always be -1 if the source event is eMouseMove.
  // PointerEventHandler cannot distinguish whether it's caused by
  // eMouseDown/eMouseUp or eMouseMove.  Therefore, we need to set -1
  // (eNotPressed) here.
  mouseRawUpdateEvent.mButton = MouseButton::eNotPressed;
  mouseRawUpdateEvent.mCoalescedWidgetEvents = nullptr;
  mouseRawUpdateEvent.convertToPointer = true;
  // Nobody checks `convertToPointerRawUpdate` of eMouseRawUpdate event.
  // However, the name indicates that it would cause ePointerRawUpdate.
  // For avoiding to make the developers who watch the value with the debugger
  // confused, here sets it to `true`.
  mouseRawUpdateEvent.convertToPointerRawUpdate = true;
  HandleRealMouseButtonEvent(mouseRawUpdateEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult BrowserChild::RecvRealMouseMoveEventNoCompress(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvRealMouseMoveEvent(aMouseEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityRealMouseMoveEvent(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvRealMouseMoveEvent(aMouseEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult
BrowserChild::RecvNormalPriorityRealMouseMoveEventNoCompress(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvRealMouseMoveEvent(aMouseEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult BrowserChild::RecvSynthMouseMoveEvent(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  if (!RecvRealMouseButtonEvent(aMouseEvent, aGuid, aInputBlockId)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPrioritySynthMouseMoveEvent(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvSynthMouseMoveEvent(aMouseEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult BrowserChild::RecvRealMouseButtonEvent(
    const WidgetMouseEvent& aMouseOrPointerEvent,
    const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId) {
  MOZ_ASSERT(!aMouseOrPointerEvent.AsDragEvent());
  if (mCoalesceMouseMoveEvents && mCoalescedMouseEventFlusher &&
      aMouseOrPointerEvent.mMessage != eMouseMove) {
    // When receiving a mouse event other than mousemove, we have to dispatch
    // all coalesced events before it. However, we can't dispatch all pending
    // coalesced events directly because we may reentry the event loop while
    // dispatching. To make sure we won't dispatch disorder events, we move all
    // coalesced mousemove events and current event to a deque to dispatch them.
    // When reentrying the event loop and dispatching more events, we put new
    // events in the end of the nsQueue and dispatch events from the beginning.
    FlushAllCoalescedMouseData();

    UniquePtr<CoalescedMouseData> dispatchData =
        MakeUnique<CoalescedMouseData>();

    // We'll dispatch aEvent immediately via
    // ProcessPendingCoalescedMouseDataAndDispatchEvents().
    // Therefore, PresShell should convert it to eMouseRawUpdate when it starts
    // handling aEvent if and only if there is a `pointerrawupdate` event
    // listener.  Therefore, let's assert the allowing flag to convert it to
    // eMouseRawUpdate here.
    MOZ_ASSERT(aMouseOrPointerEvent.convertToPointerRawUpdate);
    dispatchData->Coalesce(aMouseOrPointerEvent, aGuid, aInputBlockId);

    mToBeDispatchedMouseData.Push(dispatchData.release());
    ProcessPendingCoalescedMouseDataAndDispatchEvents();
    return IPC_OK();
  }
  HandleRealMouseButtonEvent(aMouseOrPointerEvent, aGuid, aInputBlockId);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvRealPointerButtonEvent(
    const WidgetPointerEvent& aPointerEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvRealMouseButtonEvent(aPointerEvent, aGuid, aInputBlockId);
}

void BrowserChild::HandleRealMouseButtonEvent(
    const WidgetMouseEvent& aMouseOrPointerEvent,
    const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId) {
  MOZ_ASSERT(!aMouseOrPointerEvent.AsDragEvent());

  AutoSynthesizedEventResponder<WidgetMouseEvent> responder(
      this, aMouseOrPointerEvent);

  Maybe<WidgetPointerEvent> pointerEvent;
  Maybe<WidgetMouseEvent> mouseEvent;
  if (aMouseOrPointerEvent.mClass == ePointerEventClass) {
    pointerEvent.emplace(
        WidgetPointerEvent::MakeCopyFromMouseEvent(aMouseOrPointerEvent));
  } else {
    MOZ_DIAGNOSTIC_ASSERT(!aMouseOrPointerEvent.AsPointerEvent());
    MOZ_DIAGNOSTIC_ASSERT(!aMouseOrPointerEvent.AsDragEvent());
    mouseEvent.emplace(aMouseOrPointerEvent);
  }
  WidgetMouseEvent& localEvent =
      pointerEvent.isSome() ? pointerEvent.ref() : mouseEvent.ref();
  localEvent.mWidget = mPuppetWidget;

  // We need one InputAPZContext here to propagate |aGuid| to places in
  // SendSetTargetAPZCNotification() which apply the visual-to-layout transform,
  // and another below to propagate the |postLayerization| flag (whose value
  // we don't know until SendSetTargetAPZCNotification() returns) into
  // the event dispatch code.
  InputAPZContext context1(aGuid, aInputBlockId, nsEventStatus_eSentinel);

  // Mouse events like eMouseEnterIntoWidget, that are created in the parent
  // process EventStateManager code, have an input block id which they get from
  // the InputAPZContext in the parent process stack. However, they did not
  // actually go through the APZ code and so their mHandledByAPZ flag is false.
  // Since thos events didn't go through APZ, we don't need to send
  // notifications for them.
  RefPtr<DisplayportSetListener> postLayerization;
  if (aInputBlockId && localEvent.mFlags.mHandledByAPZ) {
    nsCOMPtr<Document> document(GetTopLevelDocument());
    postLayerization = APZCCallbackHelper::SendSetTargetAPZCNotification(
        mPuppetWidget, document, localEvent, aGuid.mLayersId, aInputBlockId);
  }

  InputAPZContext context2(aGuid, aInputBlockId, nsEventStatus_eSentinel,
                           postLayerization != nullptr);

  DispatchWidgetEventViaAPZ(localEvent);

  if (aInputBlockId && localEvent.mFlags.mHandledByAPZ && mAPZEventState) {
    mAPZEventState->ProcessMouseEvent(localEvent, aInputBlockId);
  }

  // Do this after the DispatchWidgetEventViaAPZ call above, so that if the
  // mouse event triggered a post-refresh AsyncDragMetrics message to be sent
  // to APZ (from scrollbar dragging in nsSliderFrame), then that will reach
  // APZ before the SetTargetAPZC message. This ensures the drag input block
  // gets the drag metrics before handling the input events.
  if (postLayerization) {
    postLayerization->Register();
  }
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityRealMouseButtonEvent(
    const WidgetMouseEvent& aMouseOrPointerEvent,
    const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId) {
  return RecvRealMouseButtonEvent(aMouseOrPointerEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityRealPointerButtonEvent(
    const WidgetPointerEvent& aPointerEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvNormalPriorityRealMouseButtonEvent(aPointerEvent, aGuid,
                                                aInputBlockId);
}

mozilla::ipc::IPCResult BrowserChild::RecvRealMouseEnterExitWidgetEvent(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvRealMouseButtonEvent(aMouseEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult
BrowserChild::RecvNormalPriorityRealMouseEnterExitWidgetEvent(
    const WidgetMouseEvent& aMouseEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvRealMouseButtonEvent(aMouseEvent, aGuid, aInputBlockId);
}

nsEventStatus BrowserChild::DispatchWidgetEventViaAPZ(WidgetGUIEvent& aEvent) {
  aEvent.ResetWaitingReplyFromRemoteProcessState();
  return APZCCallbackHelper::DispatchWidgetEvent(aEvent);
}

void BrowserChild::DispatchCoalescedWheelEvent() {
  UniquePtr<WidgetWheelEvent> wheelEvent =
      mCoalescedWheelData.TakeCoalescedEvent();
  MOZ_ASSERT(wheelEvent);
  DispatchWheelEvent(*wheelEvent, mCoalescedWheelData.GetScrollableLayerGuid(),
                     mCoalescedWheelData.GetInputBlockId());
}

void BrowserChild::DispatchWheelEvent(const WidgetWheelEvent& aEvent,
                                      const ScrollableLayerGuid& aGuid,
                                      const uint64_t& aInputBlockId) {
  WidgetWheelEvent localEvent(aEvent);
  if (aInputBlockId && aEvent.mFlags.mHandledByAPZ) {
    nsCOMPtr<Document> document(GetTopLevelDocument());
    RefPtr<DisplayportSetListener> postLayerization =
        APZCCallbackHelper::SendSetTargetAPZCNotification(
            mPuppetWidget, document, aEvent, aGuid.mLayersId, aInputBlockId);
    if (postLayerization) {
      postLayerization->Register();
    }
  }

  localEvent.mWidget = mPuppetWidget;

  // Stash the guid in InputAPZContext so that when the visual-to-layout
  // transform is applied to the event's coordinates, we use the right transform
  // based on the scroll frame being targeted.
  // The other values don't really matter.
  InputAPZContext context(aGuid, aInputBlockId, nsEventStatus_eSentinel);

  DispatchWidgetEventViaAPZ(localEvent);

  if (localEvent.mCanTriggerSwipe) {
    SendRespondStartSwipeEvent(aInputBlockId, localEvent.TriggersSwipe());
  }

  if (aInputBlockId && aEvent.mFlags.mHandledByAPZ && mAPZEventState) {
    mAPZEventState->ProcessWheelEvent(localEvent, aInputBlockId);
  }
}

mozilla::ipc::IPCResult BrowserChild::RecvMouseWheelEvent(
    const WidgetWheelEvent& aEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  AutoSynthesizedEventResponder<WidgetWheelEvent> responder(this, aEvent);

  bool isNextWheelEvent = false;
  // We only coalesce the current event when
  // 1. It's eWheel (we don't coalesce eOperationStart and eWheelOperationEnd)
  // 2. It has same attributes as the coalesced wheel event which is not yet
  //    fired.
  if (aEvent.mMessage == eWheel) {
    GetIPCChannel()->PeekMessages(
        [&isNextWheelEvent](const IPC::Message& aMsg) -> bool {
          if (aMsg.type() == mozilla::dom::PBrowser::Msg_MouseWheelEvent__ID) {
            isNextWheelEvent = true;
          }
          return false;  // Stop peeking.
        });

    if (!mCoalescedWheelData.IsEmpty() &&
        !mCoalescedWheelData.CanCoalesce(aEvent, aGuid, aInputBlockId)) {
      DispatchCoalescedWheelEvent();
      MOZ_ASSERT(mCoalescedWheelData.IsEmpty());
    }
    mCoalescedWheelData.Coalesce(aEvent, aGuid, aInputBlockId);

    MOZ_ASSERT(!mCoalescedWheelData.IsEmpty());
    // If the next event isn't a wheel event, make sure we dispatch.
    if (!isNextWheelEvent) {
      DispatchCoalescedWheelEvent();
    }
  } else {
    DispatchWheelEvent(aEvent, aGuid, aInputBlockId);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityMouseWheelEvent(
    const WidgetWheelEvent& aEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId) {
  return RecvMouseWheelEvent(aEvent, aGuid, aInputBlockId);
}

mozilla::ipc::IPCResult BrowserChild::RecvRealTouchEvent(
    const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse) {
  MOZ_LOG(sApzChildLog, LogLevel::Debug,
          ("Receiving touch event of type %d\n", aEvent.mMessage));

  AutoSynthesizedEventResponder<WidgetTouchEvent> responder(this, aEvent);

  if (StaticPrefs::dom_events_coalesce_touchmove()) {
    if (aEvent.mMessage == eTouchEnd || aEvent.mMessage == eTouchStart) {
      ProcessPendingCoalescedTouchData();
    }

    if (aEvent.mMessage != eTouchMove && aEvent.mMessage != eTouchRawUpdate) {
      sConsecutiveTouchMoveCount = 0;
    }
  }

  WidgetTouchEvent localEvent(aEvent);
  localEvent.mWidget = mPuppetWidget;

  // Stash the guid in InputAPZContext so that when the visual-to-layout
  // transform is applied to the event's coordinates, we use the right transform
  // based on the scroll frame being targeted.
  // The other values don't really matter.
  InputAPZContext context(aGuid, aInputBlockId, aApzResponse);

  nsTArray<TouchBehaviorFlags> allowedTouchBehaviors;
  if (localEvent.mMessage == eTouchStart && AsyncPanZoomEnabled()) {
    nsCOMPtr<Document> document = GetTopLevelDocument();
    allowedTouchBehaviors = TouchActionHelper::GetAllowedTouchBehavior(
        mPuppetWidget, document, localEvent);
    if (!allowedTouchBehaviors.IsEmpty() && mApzcTreeManager) {
      mApzcTreeManager->SetAllowedTouchBehavior(aInputBlockId,
                                                allowedTouchBehaviors);
    }
    RefPtr<DisplayportSetListener> postLayerization =
        APZCCallbackHelper::SendSetTargetAPZCNotification(
            mPuppetWidget, document, localEvent, aGuid.mLayersId,
            aInputBlockId);
    if (postLayerization) {
      postLayerization->Register();
    }
  }

  // Dispatch event to content (potentially a long-running operation)
  nsEventStatus status = DispatchWidgetEventViaAPZ(localEvent);

  if (!AsyncPanZoomEnabled()) {
    // We shouldn't have any e10s platforms that have touch events enabled
    // without APZ.
    MOZ_ASSERT(false);
    return IPC_OK();
  }

  if (mAPZEventState) {
    mAPZEventState->ProcessTouchEvent(localEvent, aGuid, aInputBlockId,
                                      aApzResponse, status,
                                      std::move(allowedTouchBehaviors));
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityRealTouchEvent(
    const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse) {
  return RecvRealTouchEvent(aEvent, aGuid, aInputBlockId, aApzResponse);
}

mozilla::ipc::IPCResult BrowserChild::RecvRealTouchMoveEvent(
    const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse) {
  if (StaticPrefs::dom_events_coalesce_touchmove()) {
    ++sConsecutiveTouchMoveCount;
    if (mCoalescedTouchMoveEventFlusher) {
      MOZ_ASSERT(aEvent.mMessage == eTouchMove);
      // NOTE: While dispatching eTouchMove or eTouchRawUpdate,
      // sConsecutiveTouchMoveCount may be changed by the event loop spun,
      // e.g., an event listener uses sync XHR or calling window.alert().
      const auto PostponeDispatchingTouchMove = [&]() {
        return sConsecutiveTouchMoveCount > 1;
      };
      // Tests do not want to coalesces the consecutive touch events.
      // XXX: Maybe we should add another flag to WidgetEvent::mFlags to allow
      // move events to be coalesced, so that the content process can ignore
      // unexpected events synthesized by the OS and avoid intermittent
      // failures.
      if (aEvent.mFlags.mIsSynthesizedForTests) {
        ProcessPendingCoalescedTouchData();
        if (!RecvRealTouchEvent(aEvent, aGuid, aInputBlockId, aApzResponse)) {
          return IPC_FAIL_NO_REASON(this);
        }
        return IPC_OK();
      }
      if (mCoalescedTouchData.IsEmpty() ||
          mCoalescedTouchData.CanCoalesce(aEvent, aGuid, aInputBlockId,
                                          aApzResponse)) {
        if (PostponeDispatchingTouchMove()) {
          WidgetTouchEvent pendingTouchMoveEvent(
              aEvent, WidgetTouchEvent::CloneTouches::Yes);
          // We don't dispatch aEvent immediately here.  However, we need to
          // dispatch eTouchRawUpdate immediately if and only if there is a
          // `pointerrawupdate` event listener.  Therefore, the cloned event in
          // the queue and it shouldn't cause eTouchRawUpdate again.
          pendingTouchMoveEvent.SetConvertToPointerRawUpdate(false);
          mCoalescedTouchData.Coalesce(pendingTouchMoveEvent, aGuid,
                                       aInputBlockId, aApzResponse);
          MOZ_ASSERT(PostponeDispatchingTouchMove());
          mCoalescedTouchMoveEventFlusher->StartObserver();
          // Let's notify the web app of `pointerrawupdate` immediately if and
          // only if they listen to it.
          HandleTouchRawUpdateEvent(pendingTouchMoveEvent, aGuid, aInputBlockId,
                                    aApzResponse);
          return IPC_OK();
        }

        // We'll dispatch aEvent via ProcessPendingCoalescedTouchData() below.
        // Therefore, the touches should cause eTouchRawUpdate event.
        MOZ_ASSERT(aEvent.CanConvertToPointerRawUpdate());
        mCoalescedTouchData.Coalesce(aEvent, aGuid, aInputBlockId,
                                     aApzResponse);
        MOZ_ASSERT(!PostponeDispatchingTouchMove());
      } else {
        UniquePtr<WidgetTouchEvent> touchMoveEvent =
            mCoalescedTouchData.TakeCoalescedEvent();
        MOZ_ASSERT(touchMoveEvent->mMessage == eTouchMove);

        // Before dispatching touchMoveEvent, we need to put aEvent into the
        // queue for keeping the event order even if an event listener spins the
        // event loop and we'll receive another touch event.  So, aEvent may be
        // dispatched while we're dispatching touchMoveEvent. Therefore, we need
        // to make it convertible to eTouchRawUpdate.
        MOZ_ASSERT(aEvent.CanConvertToPointerRawUpdate());
        mCoalescedTouchData.Coalesce(aEvent, aGuid, aInputBlockId,
                                     aApzResponse);
        MOZ_ASSERT(!PostponeDispatchingTouchMove());

        // touchMoveEvent was stored by mCoalescedTouchData before receiving
        // aEvent.  Therefore, the receiver should've already dispatched
        // eTouchRawUpdate for dispatching `pointerrawupdate` and let web apps
        // know the update immediately (with sacrificing the performance).
        // Therefore, we don't need to dispatch eTouchRawUpdate here before
        // dispatching the touchMoveEvent.
        MOZ_ASSERT(!touchMoveEvent->CanConvertToPointerRawUpdate());
        const uint32_t generation = mCoalescedTouchData.Generation();
        if (!RecvRealTouchEvent(*touchMoveEvent,
                                mCoalescedTouchData.GetScrollableLayerGuid(),
                                mCoalescedTouchData.GetInputBlockId(),
                                mCoalescedTouchData.GetApzResponse())) {
          return IPC_FAIL_NO_REASON(this);
        }
        // RecvRealTouchEvent() may have caused spinning the event loop and
        // changed sConsecutiveTouchMoveCount.  So, we need to check it now.
        if (PostponeDispatchingTouchMove()) {
          mCoalescedTouchMoveEventFlusher->StartObserver();
          if (generation == mCoalescedTouchData.Generation()) {
            // Let's notify the web app of `pointerrawupdate` immediately if and
            // only if they listen to it.  Additionally, we don't want to notify
            // eTouchRawUpdate when ProcessPendingCoalescedTouchData() is called
            // later.
            mCoalescedTouchData.NotifyTouchRawUpdateOfHandled(aEvent);
            HandleTouchRawUpdateEvent(aEvent, aGuid, aInputBlockId,
                                      aApzResponse);
          }
          return IPC_OK();
        }
      }
      // Flush the pending coalesced touch in order to avoid the first
      // touchmove be overridden by the second one, this contains aEvent.
      ProcessPendingCoalescedTouchData();
      return IPC_OK();
    }
  }

  if (!RecvRealTouchEvent(aEvent, aGuid, aInputBlockId, aApzResponse)) {
    return IPC_FAIL_NO_REASON(this);
  }
  return IPC_OK();
}

void BrowserChild::HandleTouchRawUpdateEvent(
    const WidgetTouchEvent& aPendingTouchEvent,
    const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId,
    const nsEventStatus& aApzResponse) {
  if (!mPointerRawUpdateWindowCount) {
    return;  // There is no window containing pointerrawupdate event listeners
  }

  WidgetTouchEvent touchRawUpdateEvent(aPendingTouchEvent,
                                       WidgetTouchEvent::CloneTouches::Yes);
  touchRawUpdateEvent.mMessage = eTouchRawUpdate;
  for (Touch* const touch : touchRawUpdateEvent.mTouches) {
    touch->mMessage = eTouchRawUpdate;
    touch->mCoalescedWidgetEvents = nullptr;
    touch->convertToPointer = true;
    // Although nobody checks `convertToPointerRawUpdate` of eTouchRawUpdate.
    // However, the name indicates it would cause ePointerRawUpdate or not, so,
    // for avoiding to make developers confused when they watch the value with
    // the debugger, we should set this to `true`.
    touch->convertToPointerRawUpdate = true;
  }
  RecvRealTouchEvent(touchRawUpdateEvent, aGuid, aInputBlockId, aApzResponse);
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityRealTouchMoveEvent(
    const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
    const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse) {
  return RecvRealTouchMoveEvent(aEvent, aGuid, aInputBlockId, aApzResponse);
}

mozilla::ipc::IPCResult BrowserChild::RecvRealDragEvent(
    const WidgetDragEvent& aEvent, const uint32_t& aDragAction,
    const uint32_t& aDropEffect, nsIPrincipal* aPrincipal,
    nsIPolicyContainer* aPolicyContainer) {
  WidgetDragEvent localEvent(aEvent);
  localEvent.mWidget = mPuppetWidget;

  nsCOMPtr<nsIDragSession> dragSession = GetDragSession();
  DRAGSERVICE_LOGD(
      "[%p] %s | aEvent.mMessage: %s | aDragAction: %u | aDropEffect: %u | "
      "widgetRelativePt: (%d,%d) | dragSession: %p",
      this, __FUNCTION__,
      NS_ConvertUTF16toUTF8(dom::Event::GetEventName(aEvent.mMessage)).get(),
      aDragAction, aDropEffect, static_cast<int>(localEvent.mRefPoint.x),
      static_cast<int>(localEvent.mRefPoint.y), dragSession.get());
  if (dragSession) {
    dragSession->SetDragAction(aDragAction);
    dragSession->SetTriggeringPrincipal(aPrincipal);
    dragSession->SetPolicyContainer(aPolicyContainer);
    RefPtr<DataTransfer> initialDataTransfer = dragSession->GetDataTransfer();
    if (initialDataTransfer) {
      initialDataTransfer->SetDropEffectInt(aDropEffect);
    }
  }

  if (aEvent.mMessage == eDrop) {
    bool canDrop = true;
    if (!dragSession || NS_FAILED(dragSession->GetCanDrop(&canDrop)) ||
        !canDrop) {
      DRAGSERVICE_LOGD("[%p] %s | changed drop to dragexit", this,
                       __FUNCTION__);
      localEvent.mMessage = eDragExit;
    }
  } else if (aEvent.mMessage == eDragOver) {
    if (dragSession) {
      // This will dispatch 'drag' event at the source if the
      // drag transaction started in this process.
      dragSession->FireDragEventAtSource(eDrag, aEvent.mModifiers);
    }
  }

  DispatchWidgetEventViaAPZ(localEvent);
  return IPC_OK();
}

already_AddRefed<DataTransfer> BrowserChild::ConvertToDataTransfer(
    nsIPrincipal* aPrincipal, nsTArray<IPCTransferableData>&& aTransferables,
    EventMessage aMessage) {
  // The extension process should grant access to a protected DataTransfer if
  // the principal permits it (and dom.events.datatransfer.protected.enabled is
  // false).  Otherwise, protected DataTransfer access should only be given to
  // the system.
  if (!aPrincipal || Manager()->GetRemoteType() != EXTENSION_REMOTE_TYPE) {
    aPrincipal = nsContentUtils::GetSystemPrincipal();
  }

  // Check if we are receiving any file objects. If we are we will want
  // to hide any of the other objects coming in from content.
  bool hasFiles = false;
  for (uint32_t i = 0; i < aTransferables.Length() && !hasFiles; ++i) {
    auto& items = aTransferables[i].items();
    for (uint32_t j = 0; j < items.Length() && !hasFiles; ++j) {
      if (items[j].data().type() ==
          IPCTransferableDataType::TIPCTransferableDataBlob) {
        hasFiles = true;
      }
    }
  }
  // Add the entries from the IPC to the new DataTransfer
  RefPtr<DataTransfer> dataTransfer =
      new DataTransfer(nullptr, aMessage, false, Nothing());
  for (uint32_t i = 0; i < aTransferables.Length(); ++i) {
    auto& items = aTransferables[i].items();
    for (uint32_t j = 0; j < items.Length(); ++j) {
      const IPCTransferableDataItem& item = items[j];
      RefPtr<nsVariantCC> variant = new nsVariantCC();
      nsresult rv =
          nsContentUtils::IPCTransferableDataItemToVariant(item, variant);
      if (NS_FAILED(rv)) {
        continue;
      }

      // We should hide this data from content if we have a file, and we
      // aren't a file.
      bool hidden =
          hasFiles && item.data().type() !=
                          IPCTransferableDataType::TIPCTransferableDataBlob;
      dataTransfer->SetDataWithPrincipalFromOtherProcess(
          NS_ConvertUTF8toUTF16(item.flavor()), variant, i, aPrincipal, hidden);
    }
  }
  return dataTransfer.forget();
}

mozilla::ipc::IPCResult BrowserChild::RecvInvokeChildDragSession(
    const MaybeDiscarded<WindowContext>& aSourceWindowContext,
    const MaybeDiscarded<WindowContext>& aSourceTopWindowContext,
    nsIPrincipal* aPrincipal, nsTArray<IPCTransferableData>&& aTransferables,
    const uint32_t& aAction) {
  if (nsCOMPtr<nsIDragService> dragService =
          do_GetService("@mozilla.org/widget/dragservice;1")) {
    nsIWidget* widget = WebWidget();
    dragService->StartDragSession(widget);
    if (RefPtr<nsIDragSession> session = GetDragSession()) {
      session->SetSourceWindowContext(aSourceWindowContext.GetMaybeDiscarded());
      session->SetSourceTopWindowContext(
          aSourceTopWindowContext.GetMaybeDiscarded());
      session->SetDragAction(aAction);
      RefPtr<DataTransfer> dataTransfer = ConvertToDataTransfer(
          aPrincipal, std::move(aTransferables), eDragStart);
      session->SetDataTransfer(dataTransfer);
      DRAGSERVICE_LOGD("[%p] %s | Successfully started dragSession: %p", this,
                       __FUNCTION__, session.get());
    } else {
      DRAGSERVICE_LOGE("[%p] %s | Failed to start dragSession", this,
                       __FUNCTION__);
    }
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvUpdateDragSession(
    nsIPrincipal* aPrincipal, nsTArray<IPCTransferableData>&& aTransferables,
    EventMessage aEventMessage) {
  if (RefPtr<nsIDragSession> session = GetDragSession()) {
    nsCOMPtr<DataTransfer> dataTransfer = ConvertToDataTransfer(
        aPrincipal, std::move(aTransferables), aEventMessage);
    session->SetDataTransfer(dataTransfer);
    DRAGSERVICE_LOGD(
        "[%p] %s | session: %p | aEventMessage: %s | Updated dragSession "
        "dataTransfer",
        this, __FUNCTION__, session.get(),
        NS_ConvertUTF16toUTF8(dom::Event::GetEventName(aEventMessage)).get());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvEndDragSession(
    const bool& aDoneDrag, const bool& aUserCancelled,
    const LayoutDeviceIntPoint& aDragEndPoint, const uint32_t& aKeyModifiers,
    const uint32_t& aDropEffect) {
  RefPtr<nsIDragSession> dragSession = GetDragSession();
  if (dragSession) {
    DRAGSERVICE_LOGD(
        "[%p] %s | dragSession: %p | aDoneDrag: %s | aUserCancelled: %s | "
        "aDragEndPoint: (%d, %d) | aKeyModifiers: %u | aDropEffect: %u",
        this, __FUNCTION__, dragSession.get(), TrueOrFalse(aDoneDrag),
        TrueOrFalse(aUserCancelled), static_cast<int>(aDragEndPoint.x),
        static_cast<int>(aDragEndPoint.y), aKeyModifiers, aDropEffect);

    if (aUserCancelled) {
      dragSession->UserCancelled();
    }

    RefPtr<DataTransfer> dataTransfer = dragSession->GetDataTransfer();
    if (dataTransfer) {
      dataTransfer->SetDropEffectInt(aDropEffect);
    }
    dragSession->SetDragEndPoint(aDragEndPoint.x, aDragEndPoint.y);
    dragSession->EndDragSession(aDoneDrag, aKeyModifiers);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvStoreDropTargetAndDelayEndDragSession(
    const LayoutDeviceIntPoint& aPt, uint32_t aDropEffect, uint32_t aDragAction,
    nsIPrincipal* aPrincipal, nsIPolicyContainer* aPolicyContainer) {
  // cf. RecvRealDragEvent
  nsCOMPtr<nsIDragSession> dragSession = GetDragSession();
  MOZ_ASSERT(dragSession);
  DRAGSERVICE_LOGD(
      "[%p] %s | dragSession: %p aPt: (%d, %d) | aDropEffect: %u | "
      "aDragAction: %u",
      this, __FUNCTION__, dragSession.get(), static_cast<int>(aPt.x),
      static_cast<int>(aPt.y), aDropEffect, aDragAction);
  dragSession->SetDragAction(aDragAction);
  dragSession->SetTriggeringPrincipal(aPrincipal);
  dragSession->SetPolicyContainer(aPolicyContainer);
  RefPtr<DataTransfer> initialDataTransfer = dragSession->GetDataTransfer();
  if (initialDataTransfer) {
    initialDataTransfer->SetDropEffectInt(aDropEffect);
  }

  bool canDrop = true;
  if (!dragSession || NS_FAILED(dragSession->GetCanDrop(&canDrop)) ||
      !canDrop) {
    // Don't record the target or delay EDS calls.
    return IPC_OK();
  }

  auto parentToChildTf = GetChildToParentConversionMatrix().MaybeInverse();
  NS_ENSURE_TRUE(parentToChildTf, IPC_OK());
  LayoutDevicePoint floatPt(aPt);
  LayoutDevicePoint floatTf = parentToChildTf->TransformPoint(floatPt);
  WidgetQueryContentEvent queryEvent(true, eQueryDropTargetHittest,
                                     mPuppetWidget);
  queryEvent.mRefPoint = RoundedToInt(floatTf);
  DispatchWidgetEventViaAPZ(queryEvent);
  if (queryEvent.mReply && queryEvent.mReply->mDropElement) {
    mDelayedDropPoint = queryEvent.mRefPoint;
    dragSession->StoreDropTargetAndDelayEndDragSession(
        queryEvent.mReply->mDropElement, queryEvent.mReply->mDropFrame);
  } else {
    MOZ_ASSERT(false, "Didn't get reply from eQueryDropTargetHittest event!");
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult
BrowserChild::RecvDispatchToDropTargetAndResumeEndDragSession(
    bool aShouldDrop, nsTHashSet<nsString>&& aAllowedFilesPaths) {
  DRAGSERVICE_LOGD("[%p] %s | aShouldDrop: %s", this, __FUNCTION__,
                   TrueOrFalse(aShouldDrop));
  nsCOMPtr<nsIDragSession> dragSession = GetDragSession();
  MOZ_ASSERT(dragSession);
  RefPtr<nsIWidget> widget = mPuppetWidget;
  nsTHashSet<nsString> allowedPaths =
      aShouldDrop ? std::move(aAllowedFilesPaths) : nsTHashSet<nsString>();
  dragSession->DispatchToDropTargetAndResumeEndDragSession(
      widget, mDelayedDropPoint, aShouldDrop, allowedPaths);
  mDelayedDropPoint = {};
  return IPC_OK();
}

void BrowserChild::RequestEditCommands(NativeKeyBindingsType aType,
                                       const WidgetKeyboardEvent& aEvent,
                                       nsTArray<CommandInt>& aCommands) {
  MOZ_ASSERT(aCommands.IsEmpty());

  if (NS_WARN_IF(aEvent.IsEditCommandsInitialized(aType))) {
    aCommands = aEvent.EditCommandsConstRef(aType).Clone();
    return;
  }

  switch (aType) {
    case NativeKeyBindingsType::SingleLineEditor:
    case NativeKeyBindingsType::MultiLineEditor:
    case NativeKeyBindingsType::RichTextEditor:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Invalid native key bindings type");
  }

  // Don't send aEvent to the parent process directly because it'll be marked
  // as posted to remote process.
  WidgetKeyboardEvent localEvent(aEvent);
  SendRequestNativeKeyBindings(aType, localEvent, &aCommands);
}

mozilla::ipc::IPCResult BrowserChild::RecvSynthesizedEventResponse(
    const uint64_t& aCallbackId) {
  NS_ENSURE_TRUE(xpc::IsInAutomation(), IPC_FAIL(this, "Unexpected event"));
  mozilla::widget::AutoSynthesizedEventCallbackNotifier::NotifySavedCallback(
      aCallbackId);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvUpdateSHistory() {
  if (mSessionStoreChild) {
    mSessionStoreChild->UpdateSHistoryChanges();
  }
  return IPC_OK();
}

// In case handling repeated keys takes much time, we skip firing new ones.
bool BrowserChild::SkipRepeatedKeyEvent(const WidgetKeyboardEvent& aEvent) {
  if (mRepeatedKeyEventTime.IsNull() || !aEvent.CanSkipInRemoteProcess() ||
      (aEvent.mMessage != eKeyDown && aEvent.mMessage != eKeyPress)) {
    mRepeatedKeyEventTime = TimeStamp();
    mSkipKeyPress = false;
    return false;
  }

  if ((aEvent.mMessage == eKeyDown &&
       (mRepeatedKeyEventTime > aEvent.mTimeStamp)) ||
      (mSkipKeyPress && (aEvent.mMessage == eKeyPress))) {
    // If we skip a keydown event, also the following keypress events should be
    // skipped.
    mSkipKeyPress |= aEvent.mMessage == eKeyDown;
    return true;
  }

  if (aEvent.mMessage == eKeyDown) {
    // If keydown wasn't skipped, nor should the possible following keypress.
    mRepeatedKeyEventTime = TimeStamp();
    mSkipKeyPress = false;
  }
  return false;
}

void BrowserChild::UpdateRepeatedKeyEventEndTime(
    const WidgetKeyboardEvent& aEvent) {
  if (aEvent.mIsRepeat &&
      (aEvent.mMessage == eKeyDown || aEvent.mMessage == eKeyPress)) {
    mRepeatedKeyEventTime = TimeStamp::Now();
  }
}

mozilla::ipc::IPCResult BrowserChild::RecvRealKeyEvent(
    const WidgetKeyboardEvent& aEvent, const nsID& aUUID) {
  MOZ_ASSERT_IF(aEvent.mMessage == eKeyPress,
                aEvent.AreAllEditCommandsInitialized());

  // If content code called preventDefault() on a keydown event, then we don't
  // want to process any following keypress events which is caused by the
  // preceding keydown (i.e., default action of the preceding keydown).
  // In other words, if the keypress is not a default action of the preceding
  // keydown, we should not stop dispatching keypress event even if the
  // immediate preceding keydown was consumed.
  const bool isPrecedingKeyDownEventConsumed =
      aEvent.mMessage == eKeyPress && mPreviousConsumedKeyDownCode.isSome() &&
      mPreviousConsumedKeyDownCode.value() == aEvent.mCodeNameIndex;

  WidgetKeyboardEvent localEvent(aEvent);
  localEvent.mWidget = mPuppetWidget;
  localEvent.mUniqueId = aEvent.mUniqueId;

  if (!SkipRepeatedKeyEvent(aEvent) && !isPrecedingKeyDownEventConsumed) {
    const bool isOtherKeyDownBeingDispatched =
        mCurrentBeingDispatchedKeyDownCode.isSome();
    if (aEvent.mMessage == eKeyDown && !isOtherKeyDownBeingDispatched) {
      mCurrentBeingDispatchedKeyDownCode.emplace(aEvent.mCodeNameIndex);
    }
    nsEventStatus status = DispatchWidgetEventViaAPZ(localEvent);
    // If another keydown event is still being dispatched, the event loop is
    // likely being spun. If the following key event is not suppressed or
    // delayed, it could cause mPreviousConsumedKeyDownCode is not updated
    // correctly.
    NS_WARNING_ASSERTION(!isOtherKeyDownBeingDispatched ||
                             localEvent.mFlags.mIsSuppressedOrDelayed,
                         "keypress event isn't suppressed or delayed while "
                         "event loop is being spun");

    // Update the end time of the possible repeated event so that we can skip
    // some incoming events in case event handling took long time.
    UpdateRepeatedKeyEventEndTime(localEvent);

    if (aEvent.mMessage == eKeyDown) {
      // If eKeyDown is consumed, we should stop dispatching the following
      // eKeyPress events since the events are default action of eKeyDown.
      // FIXME:  We should synthesize eKeyPress in this process (bug 1181501).
      if (status == nsEventStatus_eConsumeNoDefault) {
        MOZ_ASSERT_IF(!aEvent.mFlags.mIsSynthesizedForTests,
                      aEvent.mCodeNameIndex != CODE_NAME_INDEX_USE_STRING);
        MOZ_ASSERT(!localEvent.mFlags.mIsSuppressedOrDelayed);
        MOZ_ASSERT(!isOtherKeyDownBeingDispatched);

        // In most of cases, this keydown event should be still tracked, we need
        // to remember whether it was consumed for the following keypress event.
        // If this is not the case, it likely means the event loop is being spun
        // while dispatching the keydown event, and the subsequent keypress
        // event is suppressed or delayed. In such case, PresShell will check
        // whether this keydown event was consumed and handle the delayed
        // keypress event accordingly.
        if (MOZ_LIKELY(mCurrentBeingDispatchedKeyDownCode)) {
          MOZ_ASSERT(mCurrentBeingDispatchedKeyDownCode.value() ==
                     aEvent.mCodeNameIndex);
          // If mPreviousConsumedKeyDownCode is not Nothing, 2 or more keys may
          // be pressed at same time and their eKeyDown are consumed.  However,
          // we forget the previous eKeyDown event result here and that might
          // cause dispatching eKeyPress events caused by the previous eKeyDown
          // in theory.  However, this should not occur because eKeyPress should
          // be fired before another eKeyDown, although it's depend on how the
          // native keyboard event handler is implemented.
          mPreviousConsumedKeyDownCode = Some(aEvent.mCodeNameIndex);
        }
      }
      // If eKeyDown is not consumed but we know preceding eKeyDown is consumed,
      // we need to forget it since we should not stop dispatching following
      // eKeyPress events which are default action of current eKeyDown.
      else if (mPreviousConsumedKeyDownCode.isSome() &&
               aEvent.mCodeNameIndex == mPreviousConsumedKeyDownCode.value()) {
        mPreviousConsumedKeyDownCode.reset();
      }

      // `mPreviousConsumedKeyDownCode` has been updated, so we no longer need
      // to track the keydown event.
      if (!isOtherKeyDownBeingDispatched &&
          mCurrentBeingDispatchedKeyDownCode) {
        MOZ_ASSERT(mCurrentBeingDispatchedKeyDownCode.value() ==
                   aEvent.mCodeNameIndex);
        mCurrentBeingDispatchedKeyDownCode.reset();
      }
    }
    // eKeyPress is a default action of eKeyDown.  Therefore, eKeyPress is fired
    // between eKeyDown and eKeyUp.  So, received an eKeyUp for eKeyDown which
    // was consumed means that following eKeyPress events should be dispatched.
    // Therefore, we need to forget the fact that the preceding eKeyDown was
    // consumed right now.
    // NOTE: On Windows, eKeyPress may be fired without preceding eKeyDown if
    // IME or utility app sends WM_CHAR message.  So, if we don't forget it,
    // we'd consume unrelated eKeyPress events.
    else if (aEvent.mMessage == eKeyUp &&
             mPreviousConsumedKeyDownCode.isSome() &&
             aEvent.mCodeNameIndex == mPreviousConsumedKeyDownCode.value()) {
      mPreviousConsumedKeyDownCode.reset();
    }
    // If the preceding keydown event is still being dispatched and a keypress
    // is received for the same key, it likely means the event loop is being
    // spun. In that case, the keypress event should be suppressed or delayed,
    // so we can stop tracking the current dispatched keydown event and let
    // PresShell track whether the keydown event was consumed and
    // PresShell::FireOrClearDelayedEvents() will handle the keypress event
    // correctly.
    else if (aEvent.mMessage == eKeyPress &&
             mCurrentBeingDispatchedKeyDownCode &&
             mCurrentBeingDispatchedKeyDownCode.value() ==
                 aEvent.mCodeNameIndex) {
      MOZ_DIAGNOSTIC_ASSERT(isOtherKeyDownBeingDispatched);
      NS_WARNING_ASSERTION(localEvent.mFlags.mIsSuppressedOrDelayed,
                           "keypress event isn't suppressed or delayed while "
                           "event loop is being spun");
      mCurrentBeingDispatchedKeyDownCode.reset();
    }

    if (localEvent.mFlags.mIsSuppressedOrDelayed) {
      localEvent.PreventDefault();
    }

    // If the event's default isn't prevented but the status is no default,
    // That means that the event was consumed by EventStateManager or something
    // which is not a usual event handler.  In such case, prevent its default
    // as a default handler.  For example, when an eKeyPress event matches
    // with a content accesskey, and it's executed, preventDefault() of the
    // event won't be called but the status is set to "no default".  Then,
    // the event shouldn't be handled by nsMenuBarListener in the main process.
    if (!localEvent.DefaultPrevented() &&
        status == nsEventStatus_eConsumeNoDefault) {
      localEvent.PreventDefault();
    }

    MOZ_DIAGNOSTIC_ASSERT(!localEvent.PropagationStopped());
  }
  // The keyboard event which we ignore should not be handled in the main
  // process for shortcut key handling.  For notifying if we skipped it, we can
  // use "stop propagation" flag here because it must be cleared by
  // `EventTargetChainItem` if we've dispatched it.
  else {
    localEvent.StopPropagation();
  }

  // If we don't need to send a rely for the given keyboard event, we do nothing
  // anymore here.
  if (!aEvent.WantReplyFromContentProcess()) {
    return IPC_OK();
  }

  // This is an ugly hack, mNoRemoteProcessDispatch is set to true when the
  // event's PreventDefault() or StopScrollProcessForwarding() is called.
  // And then, it'll be checked by ParamTraits<mozilla::WidgetEvent>::Write()
  // whether the event is being sent to remote process unexpectedly.
  // However, unfortunately, it cannot check the destination.  Therefore,
  // we need to clear the flag explicitly here because ParamTraits should
  // keep checking the flag for avoiding regression.
  localEvent.mFlags.mNoRemoteProcessDispatch = false;
  // The parent process won't use the native key bindings of the reply event
  // anymore. To save the IPC cost, let's clear the edit commands before sending
  // the event back to the parent process.
  localEvent.PreventNativeKeyBindings();
  SendReplyKeyEvent(localEvent, aUUID);

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityRealKeyEvent(
    const WidgetKeyboardEvent& aEvent, const nsID& aUUID) {
  return RecvRealKeyEvent(aEvent, aUUID);
}

mozilla::ipc::IPCResult BrowserChild::RecvCompositionEvent(
    const WidgetCompositionEvent& aEvent) {
  WidgetCompositionEvent localEvent(aEvent);
  localEvent.mWidget = mPuppetWidget;
  DispatchWidgetEventViaAPZ(localEvent);
  (void)SendOnEventNeedingAckHandled(aEvent.mMessage,
                                     localEvent.mCompositionId);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityCompositionEvent(
    const WidgetCompositionEvent& aEvent) {
  return RecvCompositionEvent(aEvent);
}

mozilla::ipc::IPCResult BrowserChild::RecvSelectionEvent(
    const WidgetSelectionEvent& aEvent) {
  WidgetSelectionEvent localEvent(aEvent);
  localEvent.mWidget = mPuppetWidget;
  DispatchWidgetEventViaAPZ(localEvent);
  (void)SendOnEventNeedingAckHandled(aEvent.mMessage, 0u);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPrioritySelectionEvent(
    const WidgetSelectionEvent& aEvent) {
  return RecvSelectionEvent(aEvent);
}

mozilla::ipc::IPCResult BrowserChild::RecvSimpleContentCommandEvent(
    const EventMessage& aMessage) {
  WidgetContentCommandEvent localEvent(true, aMessage, mPuppetWidget);
  DispatchWidgetEventViaAPZ(localEvent);
  (void)SendOnEventNeedingAckHandled(aMessage, 0u);
  return IPC_OK();
}

mozilla::ipc::IPCResult
BrowserChild::RecvNormalPrioritySimpleContentCommandEvent(
    const EventMessage& aMessage) {
  return RecvSimpleContentCommandEvent(aMessage);
}

mozilla::ipc::IPCResult BrowserChild::RecvInsertText(
    const nsAString& aStringToInsert) {
  // Use normal event path to reach focused document.
  WidgetContentCommandEvent localEvent(true, eContentCommandInsertText,
                                       mPuppetWidget);
  localEvent.mString = Some(nsString(aStringToInsert));
  DispatchWidgetEventViaAPZ(localEvent);
  (void)SendOnEventNeedingAckHandled(eContentCommandInsertText, 0u);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityInsertText(
    const nsAString& aStringToInsert) {
  return RecvInsertText(aStringToInsert);
}

mozilla::ipc::IPCResult BrowserChild::RecvReplaceText(
    const nsString& aReplaceSrcString, const nsString& aStringToInsert,
    uint32_t aOffset, bool aPreventSetSelection) {
  // Use normal event path to reach focused document.
  WidgetContentCommandEvent localEvent(true, eContentCommandReplaceText,
                                       mPuppetWidget);
  localEvent.mString = Some(aStringToInsert);
  localEvent.mSelection.mReplaceSrcString = aReplaceSrcString;
  localEvent.mSelection.mOffset = aOffset;
  localEvent.mSelection.mPreventSetSelection = aPreventSetSelection;
  DispatchWidgetEventViaAPZ(localEvent);
  (void)SendOnEventNeedingAckHandled(eContentCommandReplaceText, 0u);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNormalPriorityReplaceText(
    const nsString& aReplaceSrcString, const nsString& aStringToInsert,
    uint32_t aOffset, bool aPreventSetSelection) {
  return RecvReplaceText(aReplaceSrcString, aStringToInsert, aOffset,
                         aPreventSetSelection);
}

mozilla::ipc::IPCResult BrowserChild::RecvPasteTransferable(
    const IPCTransferable& aTransferable) {
  nsresult rv;
  nsCOMPtr<nsITransferable> trans =
      do_CreateInstance("@mozilla.org/widget/transferable;1", &rv);
  NS_ENSURE_SUCCESS(rv, IPC_OK());
  trans->Init(nullptr);

  rv = nsContentUtils::IPCTransferableToTransferable(
      aTransferable, true /* aAddDataFlavor */, trans,
      false /* aFilterUnknownFlavors */);
  NS_ENSURE_SUCCESS(rv, IPC_OK());

  nsCOMPtr<nsIDocShell> ourDocShell = do_GetInterface(WebNavigation());
  if (NS_WARN_IF(!ourDocShell)) {
    return IPC_OK();
  }

  RefPtr<nsCommandParams> params = new nsCommandParams();
  rv = params->SetISupports("transferable", trans);
  NS_ENSURE_SUCCESS(rv, IPC_OK());

  ourDocShell->DoCommandWithParams("cmd_pasteTransferable", params);
  return IPC_OK();
}

#ifdef ACCESSIBILITY
a11y::PDocAccessibleChild* BrowserChild::AllocPDocAccessibleChild(
    PDocAccessibleChild*, const uint64_t&,
    const MaybeDiscardedBrowsingContext&) {
  MOZ_ASSERT(false, "should never call this!");
  return nullptr;
}

bool BrowserChild::DeallocPDocAccessibleChild(
    a11y::PDocAccessibleChild* aChild) {
  delete static_cast<mozilla::a11y::DocAccessibleChild*>(aChild);
  return true;
}
#endif

RefPtr<VsyncMainChild> BrowserChild::GetVsyncChild() {
  // Initializing VsyncMainChild here turns on per-BrowserChild Vsync for a
  // given platform. Note: this only makes sense if nsWindow returns a
  // window-specific VsyncSource.
#if defined(MOZ_WAYLAND)
  if (IsWaylandEnabled()) {
    if (auto* actor = static_cast<VsyncMainChild*>(
            LoneManagedOrNullAsserts(ManagedPVsyncChild()))) {
      return actor;
    }
    auto actor = MakeRefPtr<VsyncMainChild>();
    if (!SendPVsyncConstructor(actor)) {
      return nullptr;
    }
    return actor;
  }
#endif
  return nullptr;
}

mozilla::ipc::IPCResult BrowserChild::RecvLoadRemoteScript(
    const nsAString& aURL, const bool& aRunInGlobalScope) {
  if (!InitBrowserChildMessageManager())
    // This can happen if we're half-destroyed.  It's not a fatal
    // error.
    return IPC_OK();

  JS::Rooted<JSObject*> mm(RootingCx(),
                           mBrowserChildMessageManager->GetOrCreateWrapper());
  if (!mm) {
    // This can happen if we're half-destroyed.  It's not a fatal error.
    return IPC_OK();
  }

  LoadScriptInternal(mm, aURL, !aRunInGlobalScope);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvAsyncMessage(
    const nsAString& aMessage, NotNull<StructuredCloneData*> aData) {
  AUTO_PROFILER_LABEL_DYNAMIC_LOSSY_NSSTRING("BrowserChild::RecvAsyncMessage",
                                             OTHER, aMessage);
  MMPrinter::Print("BrowserChild::RecvAsyncMessage", aMessage, aData);

  if (!mBrowserChildMessageManager) {
    return IPC_OK();
  }

  RefPtr<nsFrameMessageManager> mm =
      mBrowserChildMessageManager->GetMessageManager();

  // We should have a message manager if the global is alive, but it
  // seems sometimes we don't.  Assert in aurora/nightly, but don't
  // crash in release builds.
  MOZ_DIAGNOSTIC_ASSERT(mm);
  if (!mm) {
    return IPC_OK();
  }

  JS::Rooted<JSObject*> kungFuDeathGrip(
      dom::RootingCx(), mBrowserChildMessageManager->GetWrapper());
  mm->ReceiveMessage(static_cast<EventTarget*>(mBrowserChildMessageManager),
                     nullptr, aMessage, false, aData, nullptr);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvSwappedWithOtherRemoteLoader(
    const IPCTabContext& aContext) {
  nsCOMPtr<nsIDocShell> ourDocShell = do_GetInterface(WebNavigation());
  if (NS_WARN_IF(!ourDocShell)) {
    return IPC_OK();
  }

  nsCOMPtr<nsPIDOMWindowOuter> ourWindow = ourDocShell->GetWindow();
  if (NS_WARN_IF(!ourWindow)) {
    return IPC_OK();
  }

  RefPtr<nsDocShell> docShell = static_cast<nsDocShell*>(ourDocShell.get());

  nsCOMPtr<EventTarget> ourEventTarget = nsGlobalWindowOuter::Cast(ourWindow);

  docShell->SetInFrameSwap(true);

  nsContentUtils::FirePageShowEventForFrameLoaderSwap(
      ourDocShell, ourEventTarget, false, true);
  nsContentUtils::FirePageHideEventForFrameLoaderSwap(ourDocShell,
                                                      ourEventTarget, true);

  // Owner content type may have changed, so store the possibly updated context
  // and notify others.
  MaybeInvalidTabContext maybeContext(aContext);
  if (!maybeContext.IsValid()) {
    NS_ERROR(nsPrintfCString("Received an invalid TabContext from "
                             "the parent process. (%s)",
                             maybeContext.GetInvalidReason())
                 .get());
    MOZ_CRASH("Invalid TabContext received from the parent process.");
  }

  if (!UpdateTabContextAfterSwap(maybeContext.GetTabContext())) {
    MOZ_CRASH("Update to TabContext after swap was denied.");
  }

  // Ignore previous value of mTriedBrowserInit since owner content has changed.
  mTriedBrowserInit = true;

  nsContentUtils::FirePageShowEventForFrameLoaderSwap(
      ourDocShell, ourEventTarget, true, true);

  docShell->SetInFrameSwap(false);

  // This is needed to get visibility state right in cases when we swapped a
  // visible tab (foreground in visible window) with a non-visible tab.
  if (RefPtr<Document> doc = docShell->GetDocument()) {
    doc->UpdateVisibilityState();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvHandleAccessKey(
    const WidgetKeyboardEvent& aEvent, nsTArray<uint32_t>&& aCharCodes) {
  nsCOMPtr<Document> document(GetTopLevelDocument());
  RefPtr<nsPresContext> pc = document->GetPresContext();
  if (pc) {
    if (!pc->EventStateManager()->HandleAccessKey(
            &(const_cast<WidgetKeyboardEvent&>(aEvent)), pc, aCharCodes)) {
      // If no accesskey was found, inform the parent so that accesskeys on
      // menus can be handled.
      WidgetKeyboardEvent localEvent(aEvent);
      localEvent.mWidget = mPuppetWidget;
      SendAccessKeyNotHandled(localEvent);
    }
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvPrintPreview(
    const PrintData& aPrintData, const MaybeDiscardedBrowsingContext& aSourceBC,
    PrintPreviewResolver&& aCallback) {
#ifdef NS_PRINTING
  // If we didn't succeed in passing off ownership of aCallback, then something
  // went wrong.
  auto sendCallbackError = MakeScopeExit([&] {
    if (aCallback) {
      // signal error
      aCallback(PrintPreviewResultInfo(0, 0, false, false, false, {}, {}, {}));
    }
  });

  if (NS_WARN_IF(aSourceBC.IsDiscarded())) {
    return IPC_OK();
  }

  RefPtr<nsGlobalWindowOuter> sourceWindow;
  if (!aSourceBC.IsNull()) {
    sourceWindow = nsGlobalWindowOuter::Cast(aSourceBC.get()->GetDOMWindow());
    if (NS_WARN_IF(!sourceWindow)) {
      return IPC_OK();
    }
  } else {
    nsCOMPtr<nsPIDOMWindowOuter> ourWindow = do_GetInterface(WebNavigation());
    if (NS_WARN_IF(!ourWindow)) {
      return IPC_OK();
    }
    sourceWindow = nsGlobalWindowOuter::Cast(ourWindow);
  }

  RefPtr<nsIPrintSettings> printSettings;
  nsCOMPtr<nsIPrintSettingsService> printSettingsSvc =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (NS_WARN_IF(!printSettingsSvc)) {
    return IPC_OK();
  }
  printSettingsSvc->CreateNewPrintSettings(getter_AddRefs(printSettings));
  if (NS_WARN_IF(!printSettings)) {
    return IPC_OK();
  }
  printSettingsSvc->DeserializeToPrintSettings(aPrintData, printSettings);

  nsCOMPtr<nsIDocShell> docShellToCloneInto;
  if (!aSourceBC.IsNull()) {
    docShellToCloneInto = do_GetInterface(WebNavigation());
    if (NS_WARN_IF(!docShellToCloneInto)) {
      return IPC_OK();
    }
  }

  sourceWindow->Print(printSettings,
                      /* aRemotePrintJob = */ nullptr,
                      /* aListener = */ nullptr, docShellToCloneInto,
                      nsGlobalWindowOuter::IsPreview::Yes,
                      nsGlobalWindowOuter::IsForWindowDotPrint::No,
                      std::move(aCallback), nullptr, IgnoreErrors());
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvExitPrintPreview() {
#ifdef NS_PRINTING
  nsCOMPtr<nsIWebBrowserPrint> webBrowserPrint =
      do_GetInterface(ToSupports(WebNavigation()));
  if (NS_WARN_IF(!webBrowserPrint)) {
    return IPC_OK();
  }
  webBrowserPrint->ExitPrintPreview();
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::CommonPrint(
    const MaybeDiscardedBrowsingContext& aBc, const PrintData& aPrintData,
    RefPtr<BrowsingContext>* aCachedBrowsingContext) {
#ifdef NS_PRINTING
  if (NS_WARN_IF(aBc.IsNullOrDiscarded())) {
    return IPC_OK();
  }
  RefPtr<nsGlobalWindowOuter> outerWindow =
      nsGlobalWindowOuter::Cast(aBc.get()->GetDOMWindow());
  if (NS_WARN_IF(!outerWindow)) {
    return IPC_OK();
  }

  nsCOMPtr<nsIPrintSettingsService> printSettingsSvc =
      do_GetService("@mozilla.org/gfx/printsettings-service;1");
  if (NS_WARN_IF(!printSettingsSvc)) {
    return IPC_OK();
  }

  nsCOMPtr<nsIPrintSettings> printSettings;
  nsresult rv =
      printSettingsSvc->CreateNewPrintSettings(getter_AddRefs(printSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return IPC_OK();
  }

  printSettingsSvc->DeserializeToPrintSettings(aPrintData, printSettings);
  {
    IgnoredErrorResult rv;
    RefPtr printJob = static_cast<RemotePrintJobChild*>(
        aPrintData.remotePrintJob().AsChild());
    outerWindow->Print(
        printSettings, printJob,
        /* aListener = */ nullptr,
        /* aWindowToCloneInto = */ nullptr, nsGlobalWindowOuter::IsPreview::No,
        nsGlobalWindowOuter::IsForWindowDotPrint::No,
        /* aPrintPreviewCallback = */ nullptr, aCachedBrowsingContext, rv);
    if (NS_WARN_IF(rv.Failed())) {
      return IPC_OK();
    }
  }
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvPrint(
    const MaybeDiscardedBrowsingContext& aBc, const PrintData& aPrintData,
    bool aReturnStaticClone, PrintResolver&& aResolve) {
#ifdef NS_PRINTING
  RefPtr<BrowsingContext> browsingContext;
  auto result = CommonPrint(aBc, aPrintData,
                            aReturnStaticClone ? &browsingContext : nullptr);
  aResolve(browsingContext);
  return result;
#else
  aResolve(nullptr);
  return IPC_OK();
#endif
}

mozilla::ipc::IPCResult BrowserChild::RecvPrintClonedPage(
    const MaybeDiscardedBrowsingContext& aBc, const PrintData& aPrintData,
    const MaybeDiscardedBrowsingContext& aClonedBc) {
#ifdef NS_PRINTING
  if (aClonedBc.IsNullOrDiscarded()) {
    return IPC_OK();
  }
  RefPtr<BrowsingContext> clonedBc = aClonedBc.get();
  return CommonPrint(aBc, aPrintData, &clonedBc);
#else
  return IPC_OK();
#endif
}

mozilla::ipc::IPCResult BrowserChild::RecvDestroyPrintClone(
    const MaybeDiscardedBrowsingContext& aCachedPage) {
#ifdef NS_PRINTING
  if (aCachedPage) {
    RefPtr<nsPIDOMWindowOuter> window = aCachedPage->GetDOMWindow();
    if (NS_WARN_IF(!window)) {
      return IPC_OK();
    }
    window->Close();
  }
#endif
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvUpdateNativeWindowHandle(
    const uintptr_t& aNewHandle) {
#if defined(XP_WIN) && defined(ACCESSIBILITY)
  mNativeWindowHandle = aNewHandle;
  return IPC_OK();
#else
  return IPC_FAIL_NO_REASON(this);
#endif
}

mozilla::ipc::IPCResult BrowserChild::RecvDestroy() {
  MOZ_ASSERT(!mDestroyed);
  mDestroyed = true;

  nsTArray<PContentPermissionRequestChild*> childArray =
      nsContentPermissionUtils::GetContentPermissionRequestChildById(
          GetTabId());

  // Need to close undeleted ContentPermissionRequestChilds before tab is
  // closed.
  for (auto& permissionRequestChild : childArray) {
    auto* child = static_cast<RemotePermissionRequest*>(permissionRequestChild);
    child->Destroy();
  }

  if (mBrowserChildMessageManager) {
    // Message handlers are called from the event loop, so it better be safe to
    // run script.
    MOZ_ASSERT(nsContentUtils::IsSafeToRunScript());
    mBrowserChildMessageManager->DispatchTrustedEvent(u"unload"_ns);
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  observerService->RemoveObserver(this, BEFORE_FIRST_PAINT);

  // XXX what other code in ~BrowserChild() should we be running here?
  DestroyWindow();

  // Bounce through the event loop once to allow any delayed teardown runnables
  // that were just generated to have a chance to run.
  nsCOMPtr<nsIRunnable> deleteRunnable = new DelayedDeleteRunnable(this);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToCurrentThread(deleteRunnable));

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvRenderLayers(const bool& aEnabled) {
  auto clearPaintWhileInterruptingJS = MakeScopeExit([&] {
    // We might force a paint, or we might already have painted and this is a
    // no-op. In either case, once we exit this scope, we need to alert the
    // ProcessHangMonitor that we've finished responding to what might have
    // been a request to force paint. This is so that the BackgroundHangMonitor
    // for force painting can be made to wait again.
    if (aEnabled) {
      ProcessHangMonitor::ClearPaintWhileInterruptingJS();
    }
  });

  if (aEnabled) {
    ProcessHangMonitor::MaybeStartPaintWhileInterruptingJS();
  }

  mRenderLayers = aEnabled;
  const bool wasVisible = IsVisible();

  UpdateVisibility();

  // If we just became visible, try to trigger a paint as soon as possible.
  const bool becameVisible = !wasVisible && IsVisible();
  if (!becameVisible) {
    return IPC_OK();
  }

  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation());
  if (!docShell) {
    return IPC_OK();
  }

  // We don't use BrowserChildBase::GetPresShell() here because that would
  // create a content viewer if one doesn't exist yet. Creating a content
  // viewer can cause JS to run, which we want to avoid.
  // nsIDocShell::GetPresShell returns null if no content viewer exists yet.
  RefPtr<PresShell> presShell = docShell->GetPresShell();
  if (!presShell) {
    return IPC_OK();
  }

  if (nsIFrame* root = presShell->GetRootFrame()) {
    root->SchedulePaint();
  }

  // If we need to repaint, let's do that right away. No sense waiting until
  // we get back to the event loop again. We suppress the display port so
  // that we only paint what's visible. This ensures that the tab we're
  // switching to paints as quickly as possible.
  presShell->SuppressDisplayport(true);
  if (nsContentUtils::IsSafeToRunScript()) {
    WebWidget()->PaintNowIfNeeded();
  } else {
    // NOTE: We want to call in even without a root frame (we might paint the
    // canvas background in that case).
    presShell->PaintAndRequestComposite(presShell->GetRootFrame(),
                                        mPuppetWidget->GetWindowRenderer(),
                                        PaintFlags::None);
  }
  presShell->SuppressDisplayport(false);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvNavigateByKey(
    const bool& aForward, const bool& aForDocumentNavigation) {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    return IPC_OK();
  }

  RefPtr<Element> result;
  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());

  // Move to the first or last document.
  {
    uint32_t type =
        aForward
            ? (aForDocumentNavigation
                   ? static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_FIRSTDOC)
                   : static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_FIRST))
            : (aForDocumentNavigation
                   ? static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_LASTDOC)
                   : static_cast<uint32_t>(nsIFocusManager::MOVEFOCUS_LAST));
    uint32_t flags = nsIFocusManager::FLAG_BYKEY;
    if (aForward || aForDocumentNavigation) {
      flags |= nsIFocusManager::FLAG_NOSCROLL;
    }
    fm->MoveFocus(window, nullptr, type, flags, getter_AddRefs(result));
  }

  // No valid root element was found, so move to the first focusable element.
  if (!result && aForward && !aForDocumentNavigation) {
    fm->MoveFocus(window, nullptr, nsIFocusManager::MOVEFOCUS_FIRST,
                  nsIFocusManager::FLAG_BYKEY, getter_AddRefs(result));
  }

  SendRequestFocus(false, CallerType::System);
  return IPC_OK();
}

bool BrowserChild::InitBrowserChildMessageManager() {
  mShouldSendWebProgressEventsToParent = true;

  if (!mBrowserChildMessageManager) {
    nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());
    NS_ENSURE_TRUE(window, false);
    nsCOMPtr<EventTarget> chromeHandler = window->GetChromeEventHandler();
    NS_ENSURE_TRUE(chromeHandler, false);

    RefPtr<BrowserChildMessageManager> scope = mBrowserChildMessageManager =
        new BrowserChildMessageManager(this);

    MOZ_ALWAYS_TRUE(nsMessageManagerScriptExecutor::Init());

    nsCOMPtr<nsPIWindowRoot> root = do_QueryInterface(chromeHandler);
    if (NS_WARN_IF(!root)) {
      mBrowserChildMessageManager = nullptr;
      return false;
    }
    root->SetParentTarget(scope);
  }

  if (!mTriedBrowserInit) {
    mTriedBrowserInit = true;
  }

  return true;
}

void BrowserChild::InitRenderingState(
    const TextureFactoryIdentifier& aTextureFactoryIdentifier,
    const layers::LayersId& aLayersId,
    const CompositorOptions& aCompositorOptions) {
  mPuppetWidget->InitIMEState();

  MOZ_ASSERT(aLayersId.IsValid());
  mTextureFactoryIdentifier = aTextureFactoryIdentifier;

  // Pushing layers transactions directly to a separate
  // compositor context.
  if (!CompositorBridgeChild::Get()) {
    mLayersConnected = Some(false);
    NS_WARNING("failed to get CompositorBridgeChild instance");
    return;
  }

  mCompositorOptions = Some(aCompositorOptions);

  if (aLayersId.IsValid()) {
    StaticMutexAutoLock lock(sBrowserChildrenMutex);

    if (!sBrowserChildren) {
      sBrowserChildren = new BrowserChildMap;
    }
    MOZ_ASSERT(!sBrowserChildren->Contains(uint64_t(aLayersId)));
    sBrowserChildren->InsertOrUpdate(uint64_t(aLayersId), this);
    mLayersId = aLayersId;
  }

  // Depending on timing, we might paint too early and fall back to basic
  // layers. CreateRemoteLayerManager will destroy us if we manage to get a
  // remote layer manager though, so that's fine.
  MOZ_ASSERT(!mPuppetWidget->HasWindowRenderer() ||
             mPuppetWidget->GetWindowRenderer()->GetBackendType() ==
                 layers::LayersBackend::LAYERS_NONE);
  bool success = false;
  if (mLayersConnected == Some(true)) {
    success = CreateRemoteLayerManager();
  }

  if (success) {
    MOZ_ASSERT(mLayersConnected == Some(true));
    // Succeeded to create "remote" layer manager
    ImageBridgeChild::IdentifyCompositorTextureHost(mTextureFactoryIdentifier);
    gfx::VRManagerChild::IdentifyTextureHost(mTextureFactoryIdentifier);
    InitAPZState();
  } else {
    mLayersConnected = Some(false);
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  if (observerService) {
    observerService->AddObserver(this, BEFORE_FIRST_PAINT, false);
  }
}

bool BrowserChild::CreateRemoteLayerManager() {
  return mPuppetWidget->CreateRemoteLayerManager(
      [&](WebRenderLayerManager* aLayerManager) -> bool {
        nsCString error;
        return aLayerManager->Initialize(&mTextureFactoryIdentifier, error);
      });
}

void BrowserChild::InitAPZState() {
  if (!mCompositorOptions->UseAPZ()) {
    return;
  }
  auto* cbc = CompositorBridgeChild::Get();

  // Initialize the ApzcTreeManager. This takes multiple casts because of ugly
  // multiple inheritance.
  PAPZCTreeManagerChild* baseProtocol =
      cbc->SendPAPZCTreeManagerConstructor(mLayersId);
  if (!baseProtocol) {
    MOZ_ASSERT(false,
               "Allocating a TreeManager should not fail with APZ enabled");
    return;
  }
  APZCTreeManagerChild* derivedProtocol =
      static_cast<APZCTreeManagerChild*>(baseProtocol);

  mApzcTreeManager = RefPtr<IAPZCTreeManager>(derivedProtocol);

  // Initialize the GeckoContentController for this tab. We don't hold a
  // reference because we don't need it. The ContentProcessController will hold
  // a reference to the tab, and will be destroyed by the compositor or ipdl
  // during destruction.
  RefPtr<GeckoContentController> contentController =
      new ContentProcessController(this);
  APZChild* apzChild = new APZChild(contentController);
  cbc->SendPAPZConstructor(apzChild, mLayersId);
}

IPCResult BrowserChild::RecvUpdateEffects(const EffectsInfo& aEffects) {
  bool needInvalidate = false;
  if (mEffectsInfo.IsVisible() && aEffects.IsVisible() &&
      mEffectsInfo != aEffects) {
    // If we are staying visible and either the visrect or scale changed we need
    // to invalidate
    needInvalidate = true;
  }

  mEffectsInfo = aEffects;
  UpdateVisibility();

  if (needInvalidate) {
    if (nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation())) {
      // We don't use BrowserChildBase::GetPresShell() here because that would
      // create a content viewer if one doesn't exist yet. Creating a content
      // viewer can cause JS to run, which we want to avoid.
      // nsIDocShell::GetPresShell returns null if no content viewer exists yet.
      if (RefPtr<PresShell> presShell = docShell->GetPresShell()) {
        if (nsIFrame* root = presShell->GetRootFrame()) {
          root->InvalidateFrame();
        }
      }
    }
  }

  return IPC_OK();
}

bool BrowserChild::IsVisible() {
  return mPuppetWidget && mPuppetWidget->IsVisible();
}

void BrowserChild::UpdateVisibility() {
  const bool shouldBeVisible = [&] {
    // If we're known to be visibility: hidden / display: none, just return
    // false here, we're pretty sure we don't want to be considered visible
    // here.
    if (mBrowsingContext && mBrowsingContext->IsUnderHiddenEmbedderElement()) {
      return false;
    }
    // If we're explicitly told not to render layers, we're also invisible.
    if (!mRenderLayers) {
      return false;
    }
    if (!mIsTopLevel) {
      // For OOP iframes, include viewport visibility.
      if (!mEffectsInfo.IsVisible()) {
        return false;
      }
      // Also include activeness, unless we're artificially preserving layers.
      // An alternative to this would be to propagate mRenderLayers from the
      // parent, perhaps, so that it applies to the whole tree...
      if (!mIsPreservingLayers && mBrowsingContext &&
          !mBrowsingContext->IsActive()) {
        return false;
      }
    }
    return true;
  }();

  const bool isVisible = IsVisible();
  if (shouldBeVisible == isVisible) {
    return;
  }
  if (shouldBeVisible) {
    MakeVisible();
  } else {
    MakeHidden();
  }
}

void BrowserChild::MakeVisible() {
  if (IsVisible()) {
    return;
  }

  if (mPuppetWidget) {
    mPuppetWidget->Show(true);
  }

  PresShellActivenessMaybeChanged();
}

void BrowserChild::MakeHidden() {
  if (!IsVisible()) {
    return;
  }

  // Due to the nested event loop in ContentChild::ProvideWindowCommon,
  // it's possible to be told to become hidden before we're finished
  // setting up a layer manager. We should skip clearing cached layers
  // in that case, since doing so might accidentally put is into
  // BasicLayers mode.
  if (mPuppetWidget) {
    if (mPuppetWidget->HasWindowRenderer()) {
      ClearCachedResources();
    }
    mPuppetWidget->Show(false);
  }

  PresShellActivenessMaybeChanged();
}

IPCResult BrowserChild::RecvPreserveLayers(bool aPreserve) {
  mIsPreservingLayers = aPreserve;

  UpdateVisibility();
  PresShellActivenessMaybeChanged();

  return IPC_OK();
}

void BrowserChild::PresShellActivenessMaybeChanged() {
  // We don't use BrowserChildBase::GetPresShell() here because that would
  // create a content viewer if one doesn't exist yet. Creating a content
  // viewer can cause JS to run, which we want to avoid.
  // nsIDocShell::GetPresShell returns null if no content viewer exists yet.
  //
  // When this method is called we don't want to go through the browsing context
  // because we don't want to change the visibility state of the document, which
  // has side effects like firing events to content, unblocking media playback,
  // unthrottling timeouts... PresShell activeness has a lot less side effects.
  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation());
  if (!docShell) {
    return;
  }
  RefPtr<PresShell> presShell = docShell->GetPresShell();
  if (!presShell) {
    return;
  }
  presShell->ActivenessMaybeChanged();
}

NS_IMETHODIMP
BrowserChild::GetMessageManager(ContentFrameMessageManager** aResult) {
  RefPtr<ContentFrameMessageManager> mm(mBrowserChildMessageManager);
  mm.forget(aResult);
  return *aResult ? NS_OK : NS_ERROR_FAILURE;
}

void BrowserChild::SendRequestFocus(bool aCanFocus, CallerType aCallerType) {
  nsFocusManager* fm = nsFocusManager::GetFocusManager();
  if (!fm) {
    return;
  }

  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());
  if (!window) {
    return;
  }

  BrowsingContext* focusedBC = fm->GetFocusedBrowsingContext();
  if (focusedBC == window->GetBrowsingContext()) {
    // BrowsingContext has the focus already, do not request again.
    return;
  }

  PBrowserChild::SendRequestFocus(aCanFocus, aCallerType);
}

NS_IMETHODIMP
BrowserChild::GetTabId(uint64_t* aId) {
  *aId = GetTabId();
  return NS_OK;
}

NS_IMETHODIMP
BrowserChild::GetChromeOuterWindowID(uint64_t* aId) {
  *aId = ChromeOuterWindowID();
  return NS_OK;
}

bool BrowserChild::DoSendBlockingMessage(
    const nsAString& aMessage, NotNull<StructuredCloneData*> aData,
    nsTArray<NotNull<RefPtr<StructuredCloneData>>>* aRetVal) {
  return SendSyncMessage(PromiseFlatString(aMessage), aData, aRetVal);
}

nsresult BrowserChild::DoSendAsyncMessage(const nsAString& aMessage,
                                          NotNull<StructuredCloneData*> aData) {
  if (!SendAsyncMessage(PromiseFlatString(aMessage), aData)) {
    return NS_ERROR_UNEXPECTED;
  }
  return NS_OK;
}

/* static */
nsTArray<RefPtr<BrowserChild>> BrowserChild::GetAll() {
  StaticMutexAutoLock lock(sBrowserChildrenMutex);

  if (!sBrowserChildren) {
    return {};
  }

  return ToTArray<nsTArray<RefPtr<BrowserChild>>>(sBrowserChildren->Values());
}

BrowserChild* BrowserChild::GetFrom(PresShell* aPresShell) {
  Document* doc = aPresShell->GetDocument();
  if (!doc) {
    return nullptr;
  }
  nsCOMPtr<nsIDocShell> docShell(doc->GetDocShell());
  return GetFrom(docShell);
}

BrowserChild* BrowserChild::GetFrom(layers::LayersId aLayersId) {
  StaticMutexAutoLock lock(sBrowserChildrenMutex);
  if (!sBrowserChildren) {
    return nullptr;
  }
  return sBrowserChildren->Get(uint64_t(aLayersId));
}

void BrowserChild::DidComposite(mozilla::layers::TransactionId aTransactionId,
                                const TimeStamp& aCompositeStart,
                                const TimeStamp& aCompositeEnd) {
  MOZ_ASSERT(mPuppetWidget);
  RefPtr<WebRenderLayerManager> lm =
      mPuppetWidget->GetWindowRenderer()->AsWebRender();
  MOZ_ASSERT(lm);

  if (lm) {
    lm->DidComposite(aTransactionId, aCompositeStart, aCompositeEnd);
  }
}

void BrowserChild::ClearCachedResources() {
  MOZ_ASSERT(mPuppetWidget);
  RefPtr<WebRenderLayerManager> lm =
      mPuppetWidget->GetWindowRenderer()->AsWebRender();
  if (lm) {
    lm->ClearCachedResources();
  }

  if (nsCOMPtr<Document> document = GetTopLevelDocument()) {
    nsPresContext* presContext = document->GetPresContext();
    if (presContext) {
      presContext->NotifyPaintStatusReset();
    }
  }
}

void BrowserChild::SchedulePaint() {
  nsCOMPtr<nsIDocShell> docShell = do_GetInterface(WebNavigation());
  if (!docShell) {
    return;
  }

  // We don't use BrowserChildBase::GetPresShell() here because that would
  // create a content viewer if one doesn't exist yet. Creating a content viewer
  // can cause JS to run, which we want to avoid. nsIDocShell::GetPresShell
  // returns null if no content viewer exists yet.
  if (RefPtr<PresShell> presShell = docShell->GetPresShell()) {
    if (nsIFrame* root = presShell->GetRootFrame()) {
      root->SchedulePaint();
    }
  }
}

void SkipViewTransitionsAfterRenderingReset(Document& aDocument) {
  if (RefPtr<ViewTransition> transition = aDocument.GetActiveViewTransition()) {
    transition->SkipTransition(SkipTransitionReason::ResetRendering);
  }

  aDocument.EnumerateSubDocuments([&](Document& aSubDoc) {
    SkipViewTransitionsAfterRenderingReset(aSubDoc);
    return CallState::Continue;
  });
}

void BrowserChild::ReinitRendering() {
  MOZ_ASSERT(mLayersId.IsValid());

  if (RefPtr<Document> doc = GetTopLevelDocument()) {
    SkipViewTransitionsAfterRenderingReset(*doc);
  }

  // In some cases, like when we create a windowless browser,
  // RemoteLayerTreeOwner/BrowserChild is not connected to a compositor.
  if (mLayersConnectRequested.isNothing() ||
      mLayersConnectRequested == Some(false)) {
    return;
  }

  bool success = false;
  // Before we establish a new PLayerTransaction, we must connect our layer tree
  // id, CompositorBridge, and the widget compositor all together again.
  // Normally this happens in BrowserParent before BrowserChild is given
  // rendering information.
  //
  // In this case, we will send a sync message to our BrowserParent, which in
  // turn will send a sync message to the Compositor of the widget owning this
  // tab. This guarantees the correct association is in place before our
  // PLayerTransaction constructor message arrives on the cross-process
  // compositor bridge.
  Maybe<CompositorOptions> options;
  SendEnsureLayersConnected(&options);
  if (options) {
    mCompositorOptions = options;
    if (CompositorBridgeChild::Get()) {
      success = CreateRemoteLayerManager();
    }
  }

  if (!success) {
    NS_WARNING("failed to recreate layer manager");
    return;
  }

  mLayersConnected = Some(true);
  ImageBridgeChild::IdentifyCompositorTextureHost(mTextureFactoryIdentifier);
  gfx::VRManagerChild::IdentifyTextureHost(mTextureFactoryIdentifier);

  InitAPZState();
  if (nsCOMPtr<Document> doc = GetTopLevelDocument()) {
    doc->NotifyLayerManagerRecreated();
  }

  if (mRenderLayers) {
    SchedulePaint();
  }
}

void BrowserChild::ReinitRenderingForDeviceReset() {
  RefPtr<WebRenderLayerManager> lm =
      mPuppetWidget->GetWindowRenderer()->AsWebRender();
  if (lm) {
    lm->DoDestroy(/* aIsSync */ true);
  }

  // Proceed with destroying and recreating the layer manager.
  ReinitRendering();
}

NS_IMETHODIMP
BrowserChild::OnShowTooltip(int32_t aXCoords, int32_t aYCoords,
                            const nsAString& aTipText,
                            const nsAString& aTipDir) {
  nsString str(aTipText);
  nsString dir(aTipDir);
  SendShowTooltip(aXCoords, aYCoords, str, dir);
  return NS_OK;
}

NS_IMETHODIMP
BrowserChild::OnHideTooltip() {
  SendHideTooltip();
  return NS_OK;
}

void BrowserChild::NotifyJankedAnimations(
    const nsTArray<uint64_t>& aJankedAnimations) {
  MOZ_ASSERT(mPuppetWidget);
  RefPtr<WebRenderLayerManager> lm =
      mPuppetWidget->GetWindowRenderer()->AsWebRender();
  if (lm) {
    lm->UpdatePartialPrerenderedAnimations(aJankedAnimations);
  }
}

mozilla::ipc::IPCResult BrowserChild::RecvUIResolutionChanged(
    const float& aDpi, const int32_t& aRounding, const double& aScale,
    const double& aDesktopToDeviceScale) {
  const LayoutDeviceIntSize oldInnerSize = GetInnerSize();
  if (aDpi > 0) {
    mPuppetWidget->UpdateBackingScaleCache(aDpi, aRounding, aScale,
                                           aDesktopToDeviceScale);
  }

  const LayoutDeviceIntSize innerSize = GetInnerSize();
  if (mHasValidInnerSize && oldInnerSize != innerSize) {
    // See RecvUpdateDimensions for the order of these operations.
    nsCOMPtr<nsIBaseWindow> baseWin = do_QueryInterface(WebNavigation());
    baseWin->SetPositionAndSize(0, 0, innerSize.width, innerSize.height,
                                nsIBaseWindow::eRepaint);

    const LayoutDeviceIntRect widgetRect(
        GetOuterRect().TopLeft() + mClientOffset + mChromeOffset, innerSize);
    mPuppetWidget->Resize(widgetRect / mPuppetWidget->GetDesktopToDeviceScale(),
                          true);
  }

  nsCOMPtr<Document> document(GetTopLevelDocument());
  RefPtr<nsPresContext> presContext =
      document ? document->GetPresContext() : nullptr;
  if (presContext) {
    presContext->UIResolutionChangedSync();
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvTransparencyChanged(
    const bool& aIsTransparent) {
  mIsTransparent = aIsTransparent;
  SchedulePaint();
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvSafeAreaInsetsChanged(
    const mozilla::LayoutDeviceIntMargin& aSafeAreaInsets) {
  mPuppetWidget->UpdateSafeAreaInsets(aSafeAreaInsets);

  LayoutDeviceIntMargin currentSafeAreaInsets;
  // aSafeAreaInsets is for current screen. But we have to calculate safe insets
  // for content window.
  LayoutDeviceIntRect outerRect = GetOuterRect();
  RefPtr<Screen> screen = widget::ScreenManager::GetSingleton().ScreenForRect(
      RoundedToInt(outerRect / mPuppetWidget->GetDesktopToDeviceScale()));
  if (screen) {
    LayoutDeviceIntRect windowRect = outerRect + mClientOffset + mChromeOffset;
    currentSafeAreaInsets = nsContentUtils::GetWindowSafeAreaInsets(
        screen, aSafeAreaInsets, windowRect);
  }

  if (nsCOMPtr<Document> document = GetTopLevelDocument()) {
    if (nsPresContext* presContext = document->GetPresContext()) {
      presContext->SetSafeAreaInsets(currentSafeAreaInsets);
    }
  }

  // https://github.com/w3c/csswg-drafts/issues/4670
  // Actually we don't set this value on sub document. This behaviour is
  // same as Blink that safe area insets isn't set on sub document.

  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvInitSupportsUnadjustedMovement(
    const bool& aSupportsUnadjustedMovement) {
  mPuppetWidget->InitSupportsUnadjustedMovement(aSupportsUnadjustedMovement);
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvAllowScriptsToClose() {
  nsCOMPtr<nsPIDOMWindowOuter> window = do_GetInterface(WebNavigation());
  if (window) {
    nsGlobalWindowOuter::Cast(window)->AllowScriptsToClose();
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvReleaseAllPointerCapture() {
  PointerEventHandler::ReleaseAllPointerCapture();
  return IPC_OK();
}

mozilla::ipc::IPCResult BrowserChild::RecvReleasePointerLock() {
  PointerLockManager::Unlock("BrowserChild::RecvReleasePointerLock");
  return IPC_OK();
}

PPaymentRequestChild* BrowserChild::AllocPPaymentRequestChild() {
  MOZ_CRASH(
      "We should never be manually allocating PPaymentRequestChild actors");
  return nullptr;
}

bool BrowserChild::DeallocPPaymentRequestChild(PPaymentRequestChild* actor) {
  delete actor;
  return true;
}

LayoutDeviceIntSize BrowserChild::GetInnerSize() {
  return RoundedToInt(mUnscaledInnerSize * mPuppetWidget->GetDefaultScale());
};

Maybe<nsRect> BrowserChild::GetVisibleRect() const {
  if (mIsTopLevel) {
    // We are conservative about visible rects for top-level browsers to avoid
    // artifacts when resizing
    return Nothing();
  }
  return mEffectsInfo.mVisibleRect;
}

Maybe<LayoutDeviceRect>
BrowserChild::GetTopLevelViewportVisibleRectInSelfCoords() const {
  if (mIsTopLevel) {
    return Nothing();
  }

  if (!mChildToParentConversionMatrix) {
    // We have no way to tell this remote document visible rect right now.
    return Nothing();
  }

  Maybe<LayoutDeviceToLayoutDeviceMatrix4x4> inverse =
      mChildToParentConversionMatrix->MaybeInverse();
  if (!inverse) {
    return Nothing();
  }

  // Convert the remote document visible rect to the coordinate system of the
  // iframe document.
  Maybe<LayoutDeviceRect> rect = UntransformBy(
      *inverse,
      ViewAs<LayoutDevicePixel>(
          mTopLevelViewportVisibleRectInBrowserCoords,
          PixelCastJustification::ContentProcessIsLayerInUiProcess),
      LayoutDeviceRect::MaxIntRect());
  if (!rect) {
    return Nothing();
  }

  return rect;
}

LayoutDeviceIntRect BrowserChild::GetOuterRect() {
  return RoundedToInt(mUnscaledOuterRect * mPuppetWidget->GetDefaultScale());
}

void BrowserChild::PaintWhileInterruptingJS() {
  if (!IPCOpen() || !mPuppetWidget || !mPuppetWidget->HasWindowRenderer()) {
    // Don't bother doing anything now. Better to wait until we receive the
    // message on the PContent channel.
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(nsContentUtils::IsSafeToRunScript());
  nsAutoScriptBlocker scriptBlocker;
  RecvRenderLayers(/* aEnabled = */ true);
}

void BrowserChild::UnloadLayersWhileInterruptingJS() {
  if (!IPCOpen() || !mPuppetWidget || !mPuppetWidget->HasWindowRenderer()) {
    // Don't bother doing anything now. Better to wait until we receive the
    // message on the PContent channel.
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(nsContentUtils::IsSafeToRunScript());
  nsAutoScriptBlocker scriptBlocker;
  RecvRenderLayers(/* aEnabled = */ false);
}

nsresult BrowserChild::CanCancelContentJS(
    nsIRemoteTab::NavigationType aNavigationType, int32_t aNavigationIndex,
    nsIURI* aNavigationURI, int32_t aEpoch, bool* aCanCancel) {
  *aCanCancel = false;

  if (aEpoch <= mCancelContentJSEpoch) {
    // The next page loaded before we got here, so we shouldn't try to cancel
    // the content JS.
    return NS_OK;
  }

  *aCanCancel = true;
  return NS_OK;
}

NS_IMETHODIMP BrowserChild::OnStateChange(nsIWebProgress* aWebProgress,
                                          nsIRequest* aRequest,
                                          uint32_t aStateFlags,
                                          nsresult aStatus) {
  if (!IPCOpen() || mDestroyed || !mShouldSendWebProgressEventsToParent) {
    return NS_OK;
  }

  // We shouldn't need to notify the parent of redirect state changes, since
  // with DocumentChannel that only happens when we switch to the real channel,
  // and that's an implementation detail that we can hide.
  if (aStateFlags & nsIWebProgressListener::STATE_IS_REDIRECTED_DOCUMENT) {
    return NS_OK;
  }

  // Our OnStateChange call must have provided the nsIDocShell which the source
  // comes from. We'll use this to locate the corresponding BrowsingContext in
  // the parent process.
  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(aWebProgress);
  if (!docShell) {
    MOZ_ASSERT_UNREACHABLE("aWebProgress is null or not a nsIDocShell?");
    return NS_ERROR_UNEXPECTED;
  }

  WebProgressData webProgressData;
  Maybe<WebProgressStateChangeData> stateChangeData;
  RequestData requestData;

  MOZ_TRY(PrepareProgressListenerData(aWebProgress, aRequest, webProgressData,
                                      requestData));

  RefPtr<BrowsingContext> browsingContext = docShell->GetBrowsingContext();
  if (browsingContext->IsTopContent()) {
    stateChangeData.emplace();

    stateChangeData->isNavigating() = docShell->GetIsNavigating();
    stateChangeData->mayEnableCharacterEncodingMenu() =
        docShell->GetMayEnableCharacterEncodingMenu();

    RefPtr<Document> document = browsingContext->GetExtantDocument();
    if (document && aStateFlags & nsIWebProgressListener::STATE_STOP) {
      document->GetContentType(stateChangeData->contentType());
      document->GetCharacterSet(stateChangeData->charset());
      stateChangeData->documentURI() = document->GetDocumentURIObject();
    } else {
      stateChangeData->contentType().SetIsVoid(true);
      stateChangeData->charset().SetIsVoid(true);
    }
  }

  (void)SendOnStateChange(webProgressData, requestData, aStateFlags, aStatus,
                          stateChangeData);

  return NS_OK;
}

NS_IMETHODIMP BrowserChild::OnProgressChange(nsIWebProgress* aWebProgress,
                                             nsIRequest* aRequest,
                                             int32_t aCurSelfProgress,
                                             int32_t aMaxSelfProgress,
                                             int32_t aCurTotalProgress,
                                             int32_t aMaxTotalProgress) {
  if (!IPCOpen() || mDestroyed || !mShouldSendWebProgressEventsToParent) {
    return NS_OK;
  }

  // FIXME: We currently ignore ProgressChange events from out-of-process
  // subframes both here and in BrowserParent. We may want to change this
  // behaviour in the future.
  if (!GetBrowsingContext()->IsTopContent()) {
    return NS_OK;
  }

  // NOTE: ProgressChange notifications delivered here are filtered by
  // nsBrowserStatusFilter, which passes meaningless values for all other
  // arguments, so they are ignored here.
  (void)SendOnProgressChange(aCurTotalProgress, aMaxTotalProgress);

  return NS_OK;
}

NS_IMETHODIMP BrowserChild::OnLocationChange(nsIWebProgress* aWebProgress,
                                             nsIRequest* aRequest,
                                             nsIURI* aLocation,
                                             uint32_t aFlags) {
  if (!IPCOpen() || mDestroyed || !mShouldSendWebProgressEventsToParent) {
    return NS_OK;
  }

  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(aWebProgress);
  if (!docShell) {
    MOZ_ASSERT_UNREACHABLE("aWebProgress is null or not a nsIDocShell?");
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<BrowsingContext> browsingContext = docShell->GetBrowsingContext();
  RefPtr<Document> document = browsingContext->GetExtantDocument();
  if (!document) {
    return NS_OK;
  }

  WebProgressData webProgressData;
  RequestData requestData;

  MOZ_TRY(PrepareProgressListenerData(aWebProgress, aRequest, webProgressData,
                                      requestData));

  Maybe<WebProgressLocationChangeData> locationChangeData;

  bool canGoBack = false;
  bool canGoBackIgnoringUserInteraction = false;
  bool canGoForward = false;

  if (browsingContext->IsTopContent()) {
    MOZ_ASSERT(
        browsingContext == GetBrowsingContext(),
        "Toplevel content BrowsingContext which isn't GetBrowsingContext()?");

    locationChangeData.emplace();

    document->GetContentType(locationChangeData->contentType());
    locationChangeData->isNavigating() = docShell->GetIsNavigating();
    locationChangeData->documentURI() = document->GetDocumentURIObject();
    document->GetTitle(locationChangeData->title());
    document->GetCharacterSet(locationChangeData->charset());

    locationChangeData->mayEnableCharacterEncodingMenu() =
        docShell->GetMayEnableCharacterEncodingMenu();

    locationChangeData->contentPrincipal() = document->NodePrincipal();
    locationChangeData->contentPartitionedPrincipal() =
        document->PartitionedPrincipal();
    locationChangeData->policyContainer() = document->GetPolicyContainer();
    locationChangeData->referrerInfo() = document->ReferrerInfo();
    locationChangeData->isSyntheticDocument() = document->IsSyntheticDocument();

    if (nsCOMPtr<nsILoadGroup> loadGroup = document->GetDocumentLoadGroup()) {
      uint64_t requestContextID = 0;
      MOZ_TRY(loadGroup->GetRequestContextID(&requestContextID));
      locationChangeData->requestContextID() = Some(requestContextID);
    }

#ifdef MOZ_CRASHREPORTER
    if (CrashReporter::GetEnabled()) {
      nsCOMPtr<nsIURI> annotationURI;

      nsresult rv =
          NS_MutateURI(aLocation).SetUserPass(""_ns).Finalize(annotationURI);

      if (NS_FAILED(rv)) {
        // Ignore failures on about: URIs.
        annotationURI = aLocation;
      }

      CrashReporter::RecordAnnotationNSCString(
          CrashReporter::Annotation::URL, annotationURI->GetSpecOrDefault());
    }
#endif
  }

  (void)SendOnLocationChange(webProgressData, requestData, aLocation, aFlags,
                             canGoBack, canGoBackIgnoringUserInteraction,
                             canGoForward, locationChangeData);

  return NS_OK;
}

NS_IMETHODIMP BrowserChild::OnStatusChange(nsIWebProgress* aWebProgress,
                                           nsIRequest* aRequest,
                                           nsresult aStatus,
                                           const char16_t* aMessage) {
  if (!IPCOpen() || mDestroyed || !mShouldSendWebProgressEventsToParent) {
    return NS_OK;
  }

  // NOTE: StatusChange notifications delivered here are filtered by
  // nsBrowserStatusFilter, which passes meaningless values for all other
  // arguments, so they are ignored here.
  (void)SendOnStatusChange(nsDependentString(aMessage));

  return NS_OK;
}

NS_IMETHODIMP BrowserChild::OnSecurityChange(nsIWebProgress* aWebProgress,
                                             nsIRequest* aRequest,
                                             uint32_t aState) {
  // Security changes are now handled entirely in the parent process
  // so we don't need to worry about forwarding them (and we shouldn't
  // be receiving any to forward).
  return NS_OK;
}

NS_IMETHODIMP BrowserChild::OnContentBlockingEvent(nsIWebProgress* aWebProgress,
                                                   nsIRequest* aRequest,
                                                   uint32_t aEvent) {
  // The OnContentBlockingEvent only happenes in the parent process. It should
  // not be seen in the content process.
  MOZ_DIAGNOSTIC_ASSERT(
      false, "OnContentBlockingEvent should not be seen in content process.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP BrowserChild::NotifyNavigationFinished() {
  (void)SendNavigationFinished();
  return NS_OK;
}

nsresult BrowserChild::PrepareRequestData(nsIRequest* aRequest,
                                          RequestData& aRequestData) {
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  if (!channel) {
    aRequestData.requestURI() = nullptr;
    return NS_OK;
  }

  nsresult rv = channel->GetURI(getter_AddRefs(aRequestData.requestURI()));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = channel->GetOriginalURI(
      getter_AddRefs(aRequestData.originalRequestURI()));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = channel->GetCanceledReason(aRequestData.canceledReason());
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIClassifiedChannel> classifiedChannel = do_QueryInterface(channel);
  if (classifiedChannel) {
    rv = classifiedChannel->GetMatchedList(aRequestData.matchedList());
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult BrowserChild::PrepareProgressListenerData(
    nsIWebProgress* aWebProgress, nsIRequest* aRequest,
    WebProgressData& aWebProgressData, RequestData& aRequestData) {
  nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(aWebProgress);
  if (!docShell) {
    MOZ_ASSERT_UNREACHABLE("aWebProgress is null or not a nsIDocShell?");
    return NS_ERROR_UNEXPECTED;
  }

  aWebProgressData.browsingContext() = docShell->GetBrowsingContext();
  nsresult rv = aWebProgress->GetLoadType(&aWebProgressData.loadType());
  NS_ENSURE_SUCCESS(rv, rv);

  return PrepareRequestData(aRequest, aRequestData);
}

void BrowserChild::UpdateSessionStore() {
  if (mSessionStoreChild) {
    mSessionStoreChild->UpdateSessionStore();
  }
}

#ifdef XP_WIN
RefPtr<PBrowserChild::IsWindowSupportingProtectedMediaPromise>
BrowserChild::DoesWindowSupportProtectedMedia() {
  MOZ_ASSERT(
      NS_IsMainThread(),
      "Protected media support check should be done on main thread only.");
  if (mWindowSupportsProtectedMedia) {
    // If we've already checked and have a cached result, resolve with that.
    return IsWindowSupportingProtectedMediaPromise::CreateAndResolve(
        mWindowSupportsProtectedMedia.value(), __func__);
  }
  RefPtr<BrowserChild> self = this;
  // We chain off the promise rather than passing it directly so we can cache
  // the result and use that for future calls.
  return SendIsWindowSupportingProtectedMedia(ChromeOuterWindowID())
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self](bool isSupported) {
            // If a result was cached while this check was inflight, ensure the
            // results match.
            MOZ_ASSERT_IF(
                self->mWindowSupportsProtectedMedia,
                self->mWindowSupportsProtectedMedia.value() == isSupported);
            // Cache the response as it will not change during the lifetime
            // of this object.
            self->mWindowSupportsProtectedMedia = Some(isSupported);
            return IsWindowSupportingProtectedMediaPromise::CreateAndResolve(
                self->mWindowSupportsProtectedMedia.value(), __func__);
          },
          [](ResponseRejectReason reason) {
            return IsWindowSupportingProtectedMediaPromise::CreateAndReject(
                reason, __func__);
          });
}
#endif

void BrowserChild::NotifyContentBlockingEvent(
    uint32_t aEvent, nsIChannel* aChannel, bool aBlocked,
    const nsACString& aTrackingOrigin,
    const nsTArray<nsCString>& aTrackingFullHashes,
    const Maybe<
        mozilla::ContentBlockingNotifier::StorageAccessPermissionGrantedReason>&
        aReason,
    const Maybe<CanvasFingerprintingEvent>& aCanvasFingerprintingEvent) {
  if (!IPCOpen()) {
    return;
  }

  RequestData requestData;
  if (NS_SUCCEEDED(PrepareRequestData(aChannel, requestData))) {
    (void)SendNotifyContentBlockingEvent(
        aEvent, requestData, aBlocked, PromiseFlatCString(aTrackingOrigin),
        aTrackingFullHashes, aReason, aCanvasFingerprintingEvent);
  }
}

NS_IMETHODIMP
BrowserChild::ContentTransformsReceived(JSContext* aCx,
                                        dom::Promise** aPromise) {
  auto* globalObject = xpc::CurrentNativeGlobal(aCx);
  ErrorResult rv;
  if (mChildToParentConversionMatrix) {
    // Already received content transforms
    RefPtr<Promise> promise =
        Promise::CreateResolvedWithUndefined(globalObject, rv);
    promise.forget(aPromise);
    return rv.StealNSResult();
  }

  if (!mContentTransformPromise) {
    mContentTransformPromise = Promise::Create(globalObject, rv);
  }

  MOZ_ASSERT(globalObject == mContentTransformPromise->GetGlobalObject());
  NS_IF_ADDREF(*aPromise = mContentTransformPromise);
  return rv.StealNSResult();
}

already_AddRefed<nsIDragSession> BrowserChild::GetDragSession() {
  return RefPtr(mDragSession).forget();
}

void BrowserChild::SetDragSession(nsIDragSession* aSession) {
  mDragSession = aSession;
}

LazyLogModule gPointerRawUpdateEventListenersLog(
    "PointerRawUpdateEventListeners");

void BrowserChild::OnPointerRawUpdateEventListenerAdded(
    const nsPIDOMWindowInner* aWindow) {
  mPointerRawUpdateWindowCount++;
  MOZ_LOG(gPointerRawUpdateEventListenersLog, LogLevel::Info,
          ("Added for %p (total: %u)", aWindow, mPointerRawUpdateWindowCount));
}

void BrowserChild::OnPointerRawUpdateEventListenerRemoved(
    const nsPIDOMWindowInner* aWindow) {
  MOZ_ASSERT(mPointerRawUpdateWindowCount);
  if (MOZ_LIKELY(mPointerRawUpdateWindowCount)) {
    mPointerRawUpdateWindowCount--;
  }
  MOZ_LOG(gPointerRawUpdateEventListenersLog, LogLevel::Info,
          ("Removed for %p (remaining: %u)", aWindow,
           mPointerRawUpdateWindowCount));
}

#if defined(ACCESSIBILITY) && defined(MOZ_ENABLE_SKIA_PDF)
mozilla::ipc::IPCResult BrowserChild::RecvRequestDocAccessibleForPrint() {
  if (RefPtr<Document> doc = GetTopLevelDocument()) {
    if (nsAccessibilityService* serv = GetAccService()) {
      serv->NotifyOfPrintDocument(doc);
    }
  }
  return IPC_OK();
}
#endif

BrowserChildMessageManager::BrowserChildMessageManager(
    BrowserChild* aBrowserChild)
    : ContentFrameMessageManager(new nsFrameMessageManager(aBrowserChild)),
      mBrowserChild(aBrowserChild) {}

BrowserChildMessageManager::~BrowserChildMessageManager() = default;

NS_IMPL_CYCLE_COLLECTION_CLASS(BrowserChildMessageManager)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(BrowserChildMessageManager,
                                                DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mMessageManager);
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mBrowserChild);
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(BrowserChildMessageManager,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mMessageManager)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mBrowserChild)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(BrowserChildMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsIMessageSender)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(ContentFrameMessageManager)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(BrowserChildMessageManager, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(BrowserChildMessageManager, DOMEventTargetHelper)

JSObject* BrowserChildMessageManager::WrapObject(
    JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
  return ContentFrameMessageManager_Binding::Wrap(aCx, this, aGivenProto);
}

void BrowserChildMessageManager::MarkForCC() {
  if (mBrowserChild) {
    mBrowserChild->MarkScopesForCC();
  }
  EventListenerManager* elm = GetExistingListenerManager();
  if (elm) {
    elm->MarkForCC();
  }
  MessageManagerGlobal::MarkForCC();
}

Nullable<WindowProxyHolder> BrowserChildMessageManager::GetContent(
    ErrorResult& aError) {
  nsCOMPtr<nsIDocShell> docShell = GetDocShell(aError);
  if (!docShell) {
    return nullptr;
  }
  return WindowProxyHolder(docShell->GetBrowsingContext());
}

already_AddRefed<nsIDocShell> BrowserChildMessageManager::GetDocShell(
    ErrorResult& aError) {
  if (!mBrowserChild) {
    aError.Throw(NS_ERROR_NULL_POINTER);
    return nullptr;
  }
  nsCOMPtr<nsIDocShell> window =
      do_GetInterface(mBrowserChild->WebNavigation());
  return window.forget();
}

already_AddRefed<nsIEventTarget>
BrowserChildMessageManager::GetTabEventTarget() {
  return do_AddRef(GetMainThreadSerialEventTarget());
}

nsresult BrowserChildMessageManager::Dispatch(
    already_AddRefed<nsIRunnable>&& aRunnable) const {
  return SchedulerGroup::Dispatch(std::move(aRunnable));
}
