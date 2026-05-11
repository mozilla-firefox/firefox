/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_BrowserChild_h
#define mozilla_dom_BrowserChild_h

#include "PuppetWidget.h"
#include "mozilla/Attributes.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/EventDispatcher.h"
#include "mozilla/EventForwards.h"
#include "mozilla/dom/CoalescedMouseData.h"
#include "mozilla/dom/CoalescedTouchData.h"
#include "mozilla/dom/CoalescedWheelData.h"
#include "mozilla/dom/ContentFrameMessageManager.h"
#include "mozilla/dom/MessageManagerCallback.h"
#include "mozilla/dom/PBrowserChild.h"
#include "mozilla/dom/TabContext.h"
#include "mozilla/dom/VsyncMainChild.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/layers/APZCCallbackHelper.h"
#include "mozilla/layers/CompositorOptions.h"
#include "mozilla/layers/CompositorTypes.h"
#include "mozilla/layers/GeckoContentControllerTypes.h"
#include "nsCOMPtr.h"
#include "nsDeque.h"
#include "nsIBrowserChild.h"
#include "nsIDocShell.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIRemoteTab.h"
#include "nsITooltipListener.h"
#include "nsIWebBrowserChrome.h"
#include "nsIWebNavigation.h"
#include "nsIWebProgressListener.h"
#include "nsIWindowProvider.h"
#include "nsWeakReference.h"

class nsBrowserStatusFilter;
class nsIDOMWindow;
class nsIHttpChannel;
class nsIRequest;
class nsISerialEventTarget;
class nsIWebProgress;
class nsPIDOMWindowInner;
class nsWebBrowser;
class nsDocShellLoadState;
class nsIOpenWindowInfo;

template <typename T>
class nsTHashtable;
template <typename T>
class nsPtrHashKey;

namespace mozilla {
enum class NativeKeyBindingsType : uint8_t;

class AbstractThread;
class PresShell;

namespace layers {
class APZChild;
class APZEventState;
class AsyncDragMetrics;
class IAPZCTreeManager;
class ImageCompositeNotification;
class PCompositorBridgeChild;
}  // namespace layers

namespace widget {
struct AutoCacheNativeKeyCommands;
}  // namespace widget

namespace dom {

class BrowserChild;
class BrowsingContext;
class TabGroup;
class CoalescedMouseData;
class CoalescedWheelData;
class SessionStoreChild;
class RequestData;
class WebProgressData;

#define DOM_BROWSERCHILD_IID \
  {0x58a5775d, 0xba05, 0x45bf, {0xbd, 0xb8, 0xd7, 0x61, 0xf9, 0x01, 0x01, 0x31}}

class BrowserChildMessageManager : public ContentFrameMessageManager,
                                   public nsIMessageSender,
                                   public nsSupportsWeakReference {
 public:
  explicit BrowserChildMessageManager(BrowserChild* aBrowserChild);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(BrowserChildMessageManager,
                                           DOMEventTargetHelper)

  void MarkForCC();

  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  Nullable<WindowProxyHolder> GetContent(ErrorResult& aError) override;
  already_AddRefed<nsIDocShell> GetDocShell(ErrorResult& aError) override;
  already_AddRefed<nsIEventTarget> GetTabEventTarget() override;

  NS_FORWARD_SAFE_NSIMESSAGESENDER(mMessageManager)

  void GetEventTargetParent(EventChainPreVisitor& aVisitor) override {
    aVisitor.mForceContentDispatch = true;
  }

  // Dispatch a runnable related to the global.
  nsresult Dispatch(already_AddRefed<nsIRunnable>&& aRunnable) const;

  RefPtr<BrowserChild> mBrowserChild;

 protected:
  ~BrowserChildMessageManager();
};

/**
 * BrowserChild implements the child actor part of the PBrowser protocol. See
 * PBrowser for more information.
 */
class BrowserChild final : public nsMessageManagerScriptExecutor,
                           public ipc::MessageManagerCallback,
                           public PBrowserChild,
                           public nsIWebBrowserChrome,
                           public nsIInterfaceRequestor,
                           public nsIWindowProvider,
                           public nsSupportsWeakReference,
                           public nsIBrowserChild,
                           public nsIObserver,
                           public nsIWebProgressListener,
                           public TabContext,
                           public nsITooltipListener,
                           public mozilla::ipc::IShmemAllocator {
  using PuppetWidget = mozilla::widget::PuppetWidget;
  using CoalescedMouseData = mozilla::dom::CoalescedMouseData;
  using CoalescedWheelData = mozilla::dom::CoalescedWheelData;
  using APZEventState = mozilla::layers::APZEventState;
  using TouchBehaviorFlags = mozilla::layers::TouchBehaviorFlags;

  friend class PBrowserChild;

 public:
  /**
   * Find BrowserChild of aTabId in the same content process of the
   * caller.
   */
  static already_AddRefed<BrowserChild> FindBrowserChild(const TabId& aTabId);

  // Return a list of all active BrowserChildren.
  static nsTArray<RefPtr<BrowserChild>> GetAll();

 public:
  /**
   * Create a new BrowserChild object.
   */
  BrowserChild(ContentChild* aManager, const TabId& aTabId,
               const TabContext& aContext,
               dom::BrowsingContext* aBrowsingContext, uint32_t aChromeFlags,
               bool aIsTopLevel);

  MOZ_CAN_RUN_SCRIPT nsresult Init(mozIDOMWindowProxy* aParent,
                                   WindowGlobalChild* aInitialWindowChild,
                                   nsIOpenWindowInfo* aOpenWindowInfo);

  /** Return a BrowserChild with the given attributes. */
  static already_AddRefed<BrowserChild> Create(
      ContentChild* aManager, const TabId& aTabId, const TabContext& aContext,
      BrowsingContext* aBrowsingContext, uint32_t aChromeFlags,
      bool aIsTopLevel);

  // Let managees query if it is safe to send messages.
  bool IsDestroyed() const { return mDestroyed; }

  TabId GetTabId() const {
    MOZ_ASSERT(mUniqueId != 0);
    return mUniqueId;
  }

  NS_INLINE_DECL_STATIC_IID(DOM_BROWSERCHILD_IID)
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_NSIWEBBROWSERCHROME
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSIWINDOWPROVIDER
  NS_DECL_NSIBROWSERCHILD
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIWEBPROGRESSLISTENER
  NS_DECL_NSITOOLTIPLISTENER

  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_AMBIGUOUS(BrowserChild,
                                                         nsIBrowserChild)

  FORWARD_SHMEM_ALLOCATOR_TO(PBrowserChild)

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) {
    return mBrowserChildMessageManager->WrapObject(aCx, aGivenProto);
  }

  // Get the Document for the top-level window in this tab.
  already_AddRefed<Document> GetTopLevelDocument() const;

  // Get the pres-shell of the document for the top-level window in this tab.
  PresShell* GetTopLevelPresShell() const;

  BrowserChildMessageManager* GetMessageManager() {
    return mBrowserChildMessageManager;
  }

  bool IsTopLevel() const { return mIsTopLevel; }

  bool ShouldSendWebProgressEventsToParent() const {
    return mShouldSendWebProgressEventsToParent;
  }

  /**
   * MessageManagerCallback methods that we override.
   */
  virtual bool DoSendBlockingMessage(
      const nsAString& aMessage, NotNull<ipc::StructuredCloneData*> aData,
      nsTArray<NotNull<RefPtr<ipc::StructuredCloneData>>>* aRetVal) override;

  virtual nsresult DoSendAsyncMessage(
      const nsAString& aMessage,
      NotNull<ipc::StructuredCloneData*> aData) override;

  bool DoUpdateZoomConstraints(const uint32_t& aPresShellId,
                               const ViewID& aViewId,
                               const Maybe<ZoomConstraints>& aConstraints);

  mozilla::ipc::IPCResult RecvLoadURL(nsDocShellLoadState* aLoadState,
                                      const ParentShowInfo& aInfo);

  mozilla::ipc::IPCResult RecvCreateAboutBlankDocumentViewer(
      nsIPrincipal* aPrincipal, nsIPrincipal* aPartitionedPrincipal);

  mozilla::ipc::IPCResult RecvResumeLoad(const uint64_t& aPendingSwitchID,
                                         const ParentShowInfo&);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult
  RecvCloneDocumentTreeIntoSelf(
      const MaybeDiscarded<BrowsingContext>& aSourceBC,
      const embedding::PrintData& aPrintData,
      CloneDocumentTreeIntoSelfResolver&& aResolve);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvUpdateRemotePrintSettings(
      const embedding::PrintData& aPrintData);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvShow(const ParentShowInfo&, const OwnerShowInfo&);

  mozilla::ipc::IPCResult RecvInitRendering(
      const TextureFactoryIdentifier& aTextureFactoryIdentifier,
      const layers::LayersId& aLayersId,
      const mozilla::layers::CompositorOptions& aCompositorOptions,
      const bool& aLayersConnected);

  mozilla::ipc::IPCResult RecvCompositorOptionsChanged(
      const mozilla::layers::CompositorOptions& aNewOptions);

  mozilla::ipc::IPCResult RecvUpdateDimensions(
      const mozilla::dom::DimensionInfo& aDimensionInfo);
  mozilla::ipc::IPCResult RecvSizeModeChanged(const nsSizeMode& aSizeMode);

  mozilla::ipc::IPCResult RecvChildToParentMatrix(
      const mozilla::Maybe<mozilla::gfx::Matrix4x4>& aMatrix,
      const mozilla::ScreenRect& aTopLevelViewportVisibleRectInBrowserCoords);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvDynamicToolbarMaxHeightChanged(
      const mozilla::ScreenIntCoord& aHeight);

  mozilla::ipc::IPCResult RecvDynamicToolbarOffsetChanged(
      const mozilla::ScreenIntCoord& aOffset);

  mozilla::ipc::IPCResult RecvKeyboardHeightChanged(
      const mozilla::ScreenIntCoord& aHeight);

  mozilla::ipc::IPCResult RecvAndroidPipModeChanged(bool aPipMode);

  mozilla::ipc::IPCResult RecvActivate(uint64_t aActionId);

  mozilla::ipc::IPCResult RecvDeactivate(uint64_t aActionId);

  mozilla::ipc::IPCResult RecvRealMouseMoveEvent(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
  mozilla::ipc::IPCResult RecvNormalPriorityRealMouseMoveEvent(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
  mozilla::ipc::IPCResult RecvRealMouseMoveEventNoCompress(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
  mozilla::ipc::IPCResult RecvNormalPriorityRealMouseMoveEventNoCompress(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);

  mozilla::ipc::IPCResult RecvSynthMouseMoveEvent(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
  mozilla::ipc::IPCResult RecvNormalPrioritySynthMouseMoveEvent(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);

  mozilla::ipc::IPCResult RecvRealMouseButtonEvent(
      const mozilla::WidgetMouseEvent& aMouseOrPointerEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
  mozilla::ipc::IPCResult RecvNormalPriorityRealMouseButtonEvent(
      const mozilla::WidgetMouseEvent& aMouseOrPointerEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);

  mozilla::ipc::IPCResult RecvRealPointerButtonEvent(
      const mozilla::WidgetPointerEvent& aPointerEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
  mozilla::ipc::IPCResult RecvNormalPriorityRealPointerButtonEvent(
      const mozilla::WidgetPointerEvent& aPointerEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);

  mozilla::ipc::IPCResult RecvRealMouseEnterExitWidgetEvent(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
  mozilla::ipc::IPCResult RecvNormalPriorityRealMouseEnterExitWidgetEvent(
      const mozilla::WidgetMouseEvent& aMouseEvent,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvRealDragEvent(
      const WidgetDragEvent& aEvent, const uint32_t& aDragAction,
      const uint32_t& aDropEffect, nsIPrincipal* aPrincipal,
      nsIPolicyContainer* aPolicyContainer);

  mozilla::ipc::IPCResult RecvRealKeyEvent(
      const mozilla::WidgetKeyboardEvent& aEvent, const nsID& aUUID);

  mozilla::ipc::IPCResult RecvNormalPriorityRealKeyEvent(
      const mozilla::WidgetKeyboardEvent& aEvent, const nsID& aUUID);

  mozilla::ipc::IPCResult RecvMouseWheelEvent(
      const mozilla::WidgetWheelEvent& aEvent, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId);

  mozilla::ipc::IPCResult RecvNormalPriorityMouseWheelEvent(
      const mozilla::WidgetWheelEvent& aEvent, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId);

  mozilla::ipc::IPCResult RecvRealTouchEvent(const WidgetTouchEvent& aEvent,
                                             const ScrollableLayerGuid& aGuid,
                                             const uint64_t& aInputBlockId,
                                             const nsEventStatus& aApzResponse);

  mozilla::ipc::IPCResult RecvNormalPriorityRealTouchEvent(
      const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse);

  mozilla::ipc::IPCResult RecvRealTouchMoveEvent(
      const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse);

  mozilla::ipc::IPCResult RecvNormalPriorityRealTouchMoveEvent(
      const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse);

  mozilla::ipc::IPCResult RecvRealTouchMoveEvent2(
      const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse) {
    return RecvRealTouchMoveEvent(aEvent, aGuid, aInputBlockId, aApzResponse);
  }

  mozilla::ipc::IPCResult RecvNormalPriorityRealTouchMoveEvent2(
      const WidgetTouchEvent& aEvent, const ScrollableLayerGuid& aGuid,
      const uint64_t& aInputBlockId, const nsEventStatus& aApzResponse) {
    return RecvNormalPriorityRealTouchMoveEvent(aEvent, aGuid, aInputBlockId,
                                                aApzResponse);
  }

  mozilla::ipc::IPCResult RecvUpdateSHistory();

  mozilla::ipc::IPCResult RecvSynthesizedEventResponse(
      const uint64_t& aCallbackId);

  mozilla::ipc::IPCResult RecvCompositionEvent(
      const mozilla::WidgetCompositionEvent& aEvent);

  mozilla::ipc::IPCResult RecvNormalPriorityCompositionEvent(
      const mozilla::WidgetCompositionEvent& aEvent);

  mozilla::ipc::IPCResult RecvSelectionEvent(
      const mozilla::WidgetSelectionEvent& aEvent);

  mozilla::ipc::IPCResult RecvNormalPrioritySelectionEvent(
      const mozilla::WidgetSelectionEvent& aEvent);

  mozilla::ipc::IPCResult RecvSimpleContentCommandEvent(
      const mozilla::EventMessage& aMessage);

  mozilla::ipc::IPCResult RecvNormalPrioritySimpleContentCommandEvent(
      const mozilla::EventMessage& aMessage);

  mozilla::ipc::IPCResult RecvInsertText(const nsAString& aStringToInsert);

  mozilla::ipc::IPCResult RecvUpdateRemoteStyle(
      const StyleImageRendering& aImageRendering);

  mozilla::ipc::IPCResult RecvNormalPriorityInsertText(
      const nsAString& aStringToInsert);

  mozilla::ipc::IPCResult RecvReplaceText(const nsString& aReplaceSrcString,
                                          const nsString& aStringToInsert,
                                          uint32_t aOffset,
                                          bool aPreventSetSelection);

  mozilla::ipc::IPCResult RecvNormalPriorityReplaceText(
      const nsString& aReplaceSrcString, const nsString& aStringToInsert,
      uint32_t aOffset, bool aPreventSetSelection);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvPasteTransferable(
      const IPCTransferable& aTransferable);

  mozilla::ipc::IPCResult RecvLoadRemoteScript(const nsAString& aURL,
                                               const bool& aRunInGlobalScope);

  mozilla::ipc::IPCResult RecvAsyncMessage(const nsAString& aMessage,
                                           NotNull<StructuredCloneData*> aData);
  mozilla::ipc::IPCResult RecvSwappedWithOtherRemoteLoader(
      const IPCTabContext& aContext);

  mozilla::ipc::IPCResult RecvSafeAreaInsetsChanged(
      const mozilla::LayoutDeviceIntMargin& aSafeAreaInsets);

  mozilla::ipc::IPCResult RecvInitSupportsUnadjustedMovement(
      const bool& aSupportsUnadjustedMovement);

#ifdef ACCESSIBILITY
  PDocAccessibleChild* AllocPDocAccessibleChild(
      PDocAccessibleChild*, const uint64_t&,
      const MaybeDiscardedBrowsingContext&);
  bool DeallocPDocAccessibleChild(PDocAccessibleChild*);
#endif

  RefPtr<VsyncMainChild> GetVsyncChild();

  nsIWebNavigation* WebNavigation() const { return mWebNav; }

  PuppetWidget* WebWidget() { return mPuppetWidget; }

  bool IsTransparent() const { return mIsTransparent; }

  const EffectsInfo& GetEffectsInfo() const { return mEffectsInfo; }

  void SetBackgroundColor(const nscolor& aColor);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY virtual mozilla::ipc::IPCResult RecvUpdateEffects(
      const EffectsInfo& aEffects);

  void RequestEditCommands(NativeKeyBindingsType aType,
                           const WidgetKeyboardEvent& aEvent,
                           nsTArray<CommandInt>& aCommands);

  bool IsVisible();
  bool IsPreservingLayers() const { return mIsPreservingLayers; }

  /**
   * Signal to this BrowserChild that it should be made visible:
   * activated widget, retained layer tree, etc.  (Respectively,
   * made not visible.)
   */
  void UpdateVisibility();
  void MakeVisible();
  void MakeHidden();
  void PresShellActivenessMaybeChanged();

  ContentChild* Manager() const { return mManager; }

  static inline BrowserChild* GetFrom(nsIDocShell* aDocShell) {
    if (!aDocShell) {
      return nullptr;
    }

    nsCOMPtr<nsIBrowserChild> tc = aDocShell->GetBrowserChild();
    return static_cast<BrowserChild*>(tc.get());
  }

  static inline BrowserChild* GetFrom(mozIDOMWindow* aWindow) {
    nsCOMPtr<nsIWebNavigation> webNav = do_GetInterface(aWindow);
    nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(webNav);
    return GetFrom(docShell);
  }

  static inline BrowserChild* GetFrom(mozIDOMWindowProxy* aWindow) {
    nsCOMPtr<nsIWebNavigation> webNav = do_GetInterface(aWindow);
    nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(webNav);
    return GetFrom(docShell);
  }

  static BrowserChild* GetFrom(PresShell* aPresShell);
  static BrowserChild* GetFrom(layers::LayersId aLayersId);

  layers::LayersId GetLayersId() { return mLayersId; }
  Maybe<bool> IsLayersConnected() { return mLayersConnected; }

  void DidComposite(mozilla::layers::TransactionId aTransactionId,
                    const TimeStamp& aCompositeStart,
                    const TimeStamp& aCompositeEnd);

  void ClearCachedResources();
  void SchedulePaint();
  void ReinitRendering();
  void ReinitRenderingForDeviceReset();

  void NotifyJankedAnimations(const nsTArray<uint64_t>& aJankedAnimations);

  static inline BrowserChild* GetFrom(nsIDOMWindow* aWindow) {
    nsCOMPtr<nsIWebNavigation> webNav = do_GetInterface(aWindow);
    nsCOMPtr<nsIDocShell> docShell = do_QueryInterface(webNav);
    return GetFrom(docShell);
  }

  mozilla::ipc::IPCResult RecvUIResolutionChanged(
      const float& aDpi, const int32_t& aRounding, const double& aScale,
      const double& aDesktopToDeviceScale);

  mozilla::ipc::IPCResult RecvTransparencyChanged(const bool& aIsTransparent);

  mozilla::ipc::IPCResult RecvHandleAccessKey(const WidgetKeyboardEvent& aEvent,
                                              nsTArray<uint32_t>&& aCharCodes);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvPrintPreview(const PrintData& aPrintData,
                                           const MaybeDiscardedBrowsingContext&,
                                           PrintPreviewResolver&& aCallback);

  mozilla::ipc::IPCResult RecvExitPrintPreview();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult RecvPrint(
      const MaybeDiscardedBrowsingContext&, const PrintData&, bool,
      PrintResolver&&);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY mozilla::ipc::IPCResult RecvPrintClonedPage(
      const MaybeDiscardedBrowsingContext&, const PrintData&,
      const MaybeDiscardedBrowsingContext&);

  mozilla::ipc::IPCResult RecvDestroyPrintClone(
      const MaybeDiscardedBrowsingContext&);

  mozilla::ipc::IPCResult RecvUpdateNativeWindowHandle(
      const uintptr_t& aNewHandle);

  mozilla::ipc::IPCResult RecvWillChangeProcess();

  PPaymentRequestChild* AllocPPaymentRequestChild();

  bool DeallocPPaymentRequestChild(PPaymentRequestChild* aActor);

  LayoutDeviceIntPoint GetClientOffset() const { return mClientOffset; }
  LayoutDeviceIntPoint GetChromeOffset() const { return mChromeOffset; };
  ScreenIntCoord GetDynamicToolbarMaxHeight() const {
    return mDynamicToolbarMaxHeight;
  };
  mozilla::ScreenIntCoord GetKeyboardHeight() const { return mKeyboardHeight; }

  bool InAndroidPipMode() const { return mInAndroidPipMode; }

  bool IPCOpen() const { return mIPCOpen; }

  const mozilla::layers::CompositorOptions& GetCompositorOptions() const;
  bool AsyncPanZoomEnabled() const;

  LayoutDeviceIntSize GetInnerSize();
  CSSSize GetUnscaledInnerSize() { return mUnscaledInnerSize; }

  Maybe<nsRect> GetVisibleRect() const;

  // Call RecvShow(nsIntSize(0, 0)) and block future calls to RecvShow().
  void DoFakeShow(const ParentShowInfo&);

  void ContentReceivedInputBlock(uint64_t aInputBlockId,
                                 bool aPreventDefault) const;
  void SetTargetAPZC(
      uint64_t aInputBlockId,
      const nsTArray<layers::ScrollableLayerGuid>& aTargets) const;
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvHandleTap(
      const layers::GeckoContentController_TapType& aType,
      const LayoutDevicePoint& aPoint, const Modifiers& aModifiers,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvNormalPriorityHandleTap(
      const layers::GeckoContentController_TapType& aType,
      const LayoutDevicePoint& aPoint, const Modifiers& aModifiers,
      const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId,
      const Maybe<DoubleTapToZoomMetrics>& aDoubleTapToZoomMetrics);

  bool UpdateFrame(const layers::RepaintRequest& aRequest);
  void NotifyAPZStateChange(
      const ViewID& aViewId,
      const layers::GeckoContentController_APZStateChange& aChange,
      const int& aArg, Maybe<uint64_t> aInputBlockId);
  void StartScrollbarDrag(const layers::AsyncDragMetrics& aDragMetrics);
  void ZoomToRect(const uint32_t& aPresShellId,
                  const ScrollableLayerGuid::ViewID& aViewId,
                  const CSSRect& aRect, const uint32_t& aFlags);

  // Request that the docshell be marked as active.
  void PaintWhileInterruptingJS();

  void UnloadLayersWhileInterruptingJS();

  nsresult CanCancelContentJS(nsIRemoteTab::NavigationType aNavigationType,
                              int32_t aNavigationIndex, nsIURI* aNavigationURI,
                              int32_t aEpoch, bool* aCanCancel);

#if defined(XP_WIN) && defined(ACCESSIBILITY)
  uintptr_t GetNativeWindowHandle() const { return mNativeWindowHandle; }
#endif

  BrowsingContext* GetBrowsingContext() const { return mBrowsingContext; }

  // The transform from the coordinate space of this BrowserChild to the
  // coordinate space of the native window its BrowserParent is in.
  mozilla::LayoutDeviceToLayoutDeviceMatrix4x4
  GetChildToParentConversionMatrix() const;

  // Returns the portion of the visible rect of this remote document in the
  // top browser window coordinate system.  This is the result of being
  // clipped by all ancestor viewports.
  Maybe<ScreenRect> GetTopLevelViewportVisibleRectInBrowserCoords() const;

  // Similar to above GetTopLevelViewportVisibleRectInBrowserCoords(), but
  // in this out-of-process document's coordinate system.
  Maybe<LayoutDeviceRect> GetTopLevelViewportVisibleRectInSelfCoords() const;

  // Prepare to dispatch all coalesced mousemove events. We'll move all data
  // in mCoalescedMouseData to a nsDeque; then we start processing them. We
  // can't fetch the coalesced event one by one and dispatch it because we
  // may reentry the event loop and access to the same hashtable. It's
  // called when dispatching some mouse events other than mousemove.
  void FlushAllCoalescedMouseData();

  void ProcessPendingCoalescedMouseDataAndDispatchEvents();

  void ProcessPendingCoalescedTouchData();

  /**
   * Dispatch an eMouseRawUpdate event for dispatching ePointerRawUpdate event
   * into the DOM immediately when aPendingEvent will be dispatched later.
   * This does nothing if there is no window which has at least one
   * `pointerrawupdate` event listener.
   */
  void HandleMouseRawUpdateEvent(const WidgetMouseEvent& aPendingMouseEvent,
                                 const ScrollableLayerGuid& aGuid,
                                 const uint64_t& aInputBlockId);

  void HandleRealMouseButtonEvent(const WidgetMouseEvent& aMouseOrPointerEvent,
                                  const ScrollableLayerGuid& aGuid,
                                  const uint64_t& aInputBlockId);

  /**
   * Dispatch an eTouchRawUpdate event for dispatching ePointerRawUpdate event
   * into the DOM immediately when aPendingEvent will be dispatched later.
   * This does nothing if there is no window which has at least one
   * `pointerrawupdate` event listener.
   */
  void HandleTouchRawUpdateEvent(const WidgetTouchEvent& aPendingTouchEvent,
                                 const ScrollableLayerGuid& aGuid,
                                 const uint64_t& aInputBlockId,
                                 const nsEventStatus& aApzResponse);

  void SetCancelContentJSEpoch(int32_t aEpoch) {
    mCancelContentJSEpoch = aEpoch;
  }

  void UpdateSessionStore();

  mozilla::dom::SessionStoreChild* GetSessionStoreChild() {
    return mSessionStoreChild;
  }

#ifdef XP_WIN
  // Check if the window this BrowserChild is associated with supports
  // protected media (EME) or not.
  // Returns a promise the will resolve true if the window supports
  // protected media or false if it does not. The promise will be rejected
  // with an ResponseRejectReason if the IPC needed to do the check fails.
  // Callers should treat the reject case as if the window does not support
  // protected media to ensure robust handling.
  RefPtr<IsWindowSupportingProtectedMediaPromise>
  DoesWindowSupportProtectedMedia();
#endif

  // Notify the content blocking event in the parent process. This sends an
  // IPC message to the BrowserParent in the parent. The BrowserParent will
  // find the top-level WindowGlobalParent and notify the event from it.
  void NotifyContentBlockingEvent(
      uint32_t aEvent, nsIChannel* aChannel, bool aBlocked,
      const nsACString& aTrackingOrigin,
      const nsTArray<nsCString>& aTrackingFullHashes,
      const Maybe<
          ContentBlockingNotifier::StorageAccessPermissionGrantedReason>&
          aReason,
      const Maybe<CanvasFingerprintingEvent>& aCanvasFingerprintingEvent);

  already_AddRefed<nsIDragSession> GetDragSession();
  void SetDragSession(nsIDragSession* aSession);

  mozilla::ipc::IPCResult RecvInvokeChildDragSession(
      const MaybeDiscarded<WindowContext>& aSourceWindowContext,
      const MaybeDiscarded<WindowContext>& aSourceTopWindowContext,
      nsIPrincipal* aPrincipal, nsTArray<IPCTransferableData>&& aTransferables,
      const uint32_t& aAction);

  mozilla::ipc::IPCResult RecvUpdateDragSession(
      nsIPrincipal* aPrincipal, nsTArray<IPCTransferableData>&& aTransferables,
      EventMessage aEventMessage);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvEndDragSession(
      const bool& aDoneDrag, const bool& aUserCancelled,
      const mozilla::LayoutDeviceIntPoint& aEndDragPoint,
      const uint32_t& aKeyModifiers, const uint32_t& aDropEffect);

  mozilla::ipc::IPCResult RecvStoreDropTargetAndDelayEndDragSession(
      const LayoutDeviceIntPoint& aPt, uint32_t aDropEffect,
      uint32_t aDragAction, nsIPrincipal* aPrincipal,
      nsIPolicyContainer* aPolicyContainer);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvDispatchToDropTargetAndResumeEndDragSession(
      bool aShouldDrop, nsTHashSet<nsString>&& aAllowedFilesPaths);

  void OnPointerRawUpdateEventListenerAdded(const nsPIDOMWindowInner* aWindow);
  void OnPointerRawUpdateEventListenerRemoved(
      const nsPIDOMWindowInner* aWindow);
  [[nodiscard]] bool HasPointerRawUpdateEventListeners() const {
    return !!mPointerRawUpdateWindowCount;
  }

 protected:
  virtual ~BrowserChild();

  mozilla::ipc::IPCResult RecvDestroy();

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  mozilla::ipc::IPCResult RecvRenderLayers(const bool& aEnabled);

  mozilla::ipc::IPCResult RecvPreserveLayers(bool);

  mozilla::ipc::IPCResult RecvNavigateByKey(const bool& aForward,
                                            const bool& aForDocumentNavigation);

  mozilla::ipc::IPCResult RecvSuppressDisplayport(const bool& aEnabled);

  mozilla::ipc::IPCResult RecvScrollbarPreferenceChanged(ScrollbarPreference);

  mozilla::ipc::IPCResult RecvStopIMEStateManagement();

  mozilla::ipc::IPCResult RecvAllowScriptsToClose();

  mozilla::ipc::IPCResult RecvReleaseAllPointerCapture();

  mozilla::ipc::IPCResult RecvReleasePointerLock();

#if defined(ACCESSIBILITY) && defined(MOZ_ENABLE_SKIA_PDF)
  mozilla::ipc::IPCResult RecvRequestDocAccessibleForPrint();
#endif

 private:
  void HandleDoubleTap(const CSSPoint& aPoint, const Modifiers& aModifiers,
                       const ScrollableLayerGuid& aGuid,
                       const DoubleTapToZoomMetrics& aMetrics);

  void ActorDestroy(ActorDestroyReason why) override;

  bool InitBrowserChildMessageManager();

  void InitRenderingState(
      const TextureFactoryIdentifier& aTextureFactoryIdentifier,
      const layers::LayersId& aLayersId,
      const mozilla::layers::CompositorOptions& aCompositorOptions);
  void InitAPZState();

  void DestroyWindow();

  void ApplyParentShowInfo(const ParentShowInfo&);

  bool HasValidInnerSize();

  LayoutDeviceIntRect GetOuterRect();

  void SetUnscaledInnerSize(const CSSSize& aSize) {
    mUnscaledInnerSize = aSize;
  }

  bool SkipRepeatedKeyEvent(const WidgetKeyboardEvent& aEvent);

  void UpdateRepeatedKeyEventEndTime(const WidgetKeyboardEvent& aEvent);

  void DispatchCoalescedWheelEvent();

  /**
   * Dispatch aEvent on aEvent.mWidget.
   */
  nsEventStatus DispatchWidgetEventViaAPZ(WidgetGUIEvent& aEvent);

  void DispatchWheelEvent(const WidgetWheelEvent& aEvent,
                          const ScrollableLayerGuid& aGuid,
                          const uint64_t& aInputBlockId);

  void InternalSetDocShellIsActive(bool aIsActive);

  MOZ_CAN_RUN_SCRIPT
  mozilla::ipc::IPCResult CommonPrint(
      const MaybeDiscardedBrowsingContext& aBc, const PrintData& aPrintData,
      RefPtr<BrowsingContext>* aCachedBrowsingContext);

  bool CreateRemoteLayerManager();

  nsresult PrepareRequestData(nsIRequest* aRequest, RequestData& aRequestData);
  nsresult PrepareProgressListenerData(nsIWebProgress* aWebProgress,
                                       nsIRequest* aRequest,
                                       WebProgressData& aWebProgressData,
                                       RequestData& aRequestData);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult UpdateRemotePrintSettings(const embedding::PrintData& aPrintData);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  nsresult CloneDocumentTreeIntoSelf(
      const MaybeDiscarded<BrowsingContext>& aSourceBC,
      const embedding::PrintData& aPrintData);

  already_AddRefed<DataTransfer> ConvertToDataTransfer(
      nsIPrincipal* aPrincipal, nsTArray<IPCTransferableData>&& aTransferables,
      EventMessage aMessage);

  class DelayedDeleteRunnable;

  RefPtr<BrowserChildMessageManager> mBrowserChildMessageManager;
  TextureFactoryIdentifier mTextureFactoryIdentifier;
  RefPtr<nsWebBrowser> mWebBrowser;
  nsCOMPtr<nsIWebNavigation> mWebNav;
  RefPtr<PuppetWidget> mPuppetWidget;
  nsCOMPtr<nsIURI> mLastURI;
  RefPtr<ContentChild> mManager;
  RefPtr<BrowsingContext> mBrowsingContext;
  RefPtr<nsIDragSession> mDragSession;

  // Tracks the keydown event currently being dispatched.
  // This allows us to detect whether the event loop is being spun during
  // keydown dispatch, which can cause key event IPC messages to be handled
  // out of order.
  Maybe<CodeNameIndex> mCurrentBeingDispatchedKeyDownCode;
  // Tracks the most recent keydown event that was consumed.
  Maybe<CodeNameIndex> mPreviousConsumedKeyDownCode;

  uint32_t mChromeFlags;
  uint32_t mMaxTouchPoints;
  // The number of windows which may have ePointerRawUpdate event listener.
  uint32_t mPointerRawUpdateWindowCount = 0;
  layers::LayersId mLayersId;
  CSSRect mUnscaledOuterRect;
  Maybe<bool> mLayersConnected;
  Maybe<bool> mLayersConnectRequested;
  EffectsInfo mEffectsInfo;

  RefPtr<APZEventState> mAPZEventState;

  // Position of client area relative to the outer window
  LayoutDeviceIntPoint mClientOffset;
  // Position of tab, relative to parent widget (typically the window)
  // NOTE: This value is valuable only for the top level browser.
  LayoutDeviceIntPoint mChromeOffset;
  ScreenIntCoord mDynamicToolbarMaxHeight;
  // The software keyboard height.
  ScreenIntCoord mKeyboardHeight;
  TabId mUniqueId;

  // Position of a delayed drop event.
  LayoutDeviceIntPoint mDelayedDropPoint;

  bool mDidFakeShow : 1;
  bool mTriedBrowserInit : 1;
  bool mHasValidInnerSize : 1;
  bool mDestroyed : 1;
  // Whether we're in Android's PiP mode.
  bool mInAndroidPipMode : 1;

  // Whether or not this browser is the child part of the top level PBrowser
  // actor in a remote browser.
  bool mIsTopLevel : 1;

  bool mIsTransparent : 1;
  bool mIPCOpen : 1;

  bool mDidSetRealShowInfo : 1;
  bool mDidLoadURLInit : 1;

  bool mSkipKeyPress : 1;

  bool mCoalesceMouseMoveEvents : 1;

  bool mShouldSendWebProgressEventsToParent : 1;

  // Whether we are rendering to the compositor or not.
  bool mRenderLayers : 1;

  // Whether we're artificially preserving layers.
  bool mIsPreservingLayers : 1;

  // Holds the compositor options for the compositor rendering this tab,
  // once we find out which compositor that is.
  Maybe<mozilla::layers::CompositorOptions> mCompositorOptions;

  friend class ContentChild;

  CSSSize mUnscaledInnerSize;

  // Store the end time of the handling of the last repeated
  // keydown/keypress event so that in case event handling takes time, some
  // repeated events can be skipped to not flood child process.
  mozilla::TimeStamp mRepeatedKeyEventTime;

  // Similar to mRepeatedKeyEventTime, store the end time (from parent
  // process) of handling the last repeated wheel event so that in case
  // event handling takes time, some repeated events can be skipped to not
  // flood child process.
  mozilla::TimeStamp mLastWheelProcessedTimeFromParent;
  mozilla::TimeDuration mLastWheelProcessingDuration;

  // Hash table to track coalesced mousemove events for different pointers.
  nsClassHashtable<nsUint32HashKey, CoalescedMouseData> mCoalescedMouseData;

  nsDeque<CoalescedMouseData> mToBeDispatchedMouseData;

  CoalescedWheelData mCoalescedWheelData;
  CoalescedTouchData mCoalescedTouchData;

  RefPtr<CoalescedMouseMoveFlusher> mCoalescedMouseEventFlusher;
  RefPtr<CoalescedTouchMoveFlusher> mCoalescedTouchMoveEventFlusher;

  RefPtr<layers::IAPZCTreeManager> mApzcTreeManager;
  RefPtr<SessionStoreChild> mSessionStoreChild;

#if defined(XP_WIN) && defined(ACCESSIBILITY)
  // The handle associated with the native window that contains this tab
  uintptr_t mNativeWindowHandle;
#endif  // defined(XP_WIN)

  int32_t mCancelContentJSEpoch;

  Maybe<LayoutDeviceToLayoutDeviceMatrix4x4> mChildToParentConversionMatrix;
  // When mChildToParentConversionMatrix is Nothing() this value is invalid.
  ScreenRect mTopLevelViewportVisibleRectInBrowserCoords;

#ifdef XP_WIN
  // Should only be accessed on main thread.
  Maybe<bool> mWindowSupportsProtectedMedia;
#endif

  // If set, resolve when we receive ChildToParentMatrix.
  RefPtr<dom::Promise> mContentTransformPromise;

  DISALLOW_EVIL_CONSTRUCTORS(BrowserChild);
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_BrowserChild_h
