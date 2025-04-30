/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WIDGET_WINDOWS_NSWINDOW_H_
#define WIDGET_WINDOWS_NSWINDOW_H_

/*
 * nsWindow - Native window management and event handling.
 */

#include "mozilla/RefPtr.h"
#include "nsBaseWidget.h"
#include "CompositorWidget.h"
#include "mozilla/EventForwards.h"
#include "nsClassHashtable.h"
#include <windows.h>
#include "touchinjection_sdk80.h"
#include "nsdefs.h"
#include "nsUserIdleService.h"
#include "nsToolkit.h"
#include "nsString.h"
#include "nsTArray.h"
#include "gfxWindowsPlatform.h"
#include "gfxWindowsSurface.h"
#include "nsWindowDbg.h"
#include "cairo.h"
#include "nsRegion.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/Maybe.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "mozilla/dom/MouseEventBinding.h"
#include "mozilla/DataMutex.h"
#include "mozilla/UniquePtr.h"
#include "nsMargin.h"
#include "nsRegionFwd.h"

#include "nsWinGesture.h"
#include "WinPointerEvents.h"
#include "WinUtils.h"
#include "WindowHook.h"
#include "TaskbarWindowPreview.h"

#ifdef ACCESSIBILITY
#  include "oleacc.h"
#  include "mozilla/a11y/LocalAccessible.h"
#endif

#include "nsIUserIdleServiceInternal.h"

#include "IMMHandler.h"
#include "CheckInvariantWrapper.h"

/**
 * Forward class definitions
 */

class nsNativeDragTarget;
class nsIRollupListener;
class imgIContainer;

namespace mozilla {
class WidgetMouseEvent;
namespace widget {
class NativeKey;
class InProcessWinCompositorWidget;
struct MSGResult;
class DirectManipulationOwner;
}  // namespace widget
}  // namespace mozilla

/**
 * Forward Windows-internal definitions of otherwise incomplete ones provided by
 * the SDK.
 */
const CLSID CLSID_ImmersiveShell = {
    0xC2F03A33,
    0x21F5,
    0x47FA,
    {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};

/**
 * Native WIN32 window wrapper.
 */

namespace mozilla::widget {

// Data manipulation: styles + ex-styles, and bitmasking operations thereupon.
struct WindowStyles {
  LONG_PTR style = 0;
  LONG_PTR ex = 0;

  static WindowStyles FromHWND(HWND);

  constexpr bool operator==(WindowStyles const& that) const {
    return style == that.style && ex == that.ex;
  }
  constexpr bool operator!=(WindowStyles const& that) const {
    return !(*this == that);
  }
  constexpr WindowStyles operator|(WindowStyles const& that) const {
    return WindowStyles{.style = style | that.style, .ex = ex | that.ex};
  }
  constexpr WindowStyles operator&(WindowStyles const& that) const {
    return WindowStyles{.style = style & that.style, .ex = ex & that.ex};
  }
  constexpr WindowStyles operator~() const {
    return WindowStyles{.style = ~style, .ex = ~ex};
  }

  explicit constexpr operator bool() const { return style || ex; }

  // Compute a style-set which matches `zero` where the bits of `this` are 0
  // and `one` where the bits of `this` are 1.
  constexpr WindowStyles merge(WindowStyles zero, WindowStyles one) const {
    WindowStyles const& mask = *this;
    return (~mask & zero) | (mask & one);
  }

  // The dual of `merge`, above: returns a pair [zero, one] satisfying
  // `a.merge(a.split(b)...) == b`. (Or its equivalent in valid C++.)
  constexpr std::tuple<WindowStyles, WindowStyles> split(
      WindowStyles data) const {
    WindowStyles const& mask = *this;
    return {~mask & data, mask & data};
  }
};

void SetWindowStyles(HWND, const WindowStyles&);

}  // namespace mozilla::widget

class nsWindow final : public nsBaseWidget,
                       public mozilla::SupportsThreadSafeWeakPtr<nsWindow> {
 public:
  using Styles = mozilla::widget::WindowStyles;
  using WindowHook = mozilla::widget::WindowHook;
  using IMEContext = mozilla::widget::IMEContext;
  using WidgetEventTime = mozilla::WidgetEventTime;

  NS_INLINE_DECL_REFCOUNTING_INHERITED(nsWindow, nsBaseWidget)
  MOZ_DECLARE_REFCOUNTED_TYPENAME(nsWindow)

  nsWindow();

  void SendAnAPZEvent(mozilla::InputData& aEvent);

  /*
   * Init a standard gecko event for this widget.
   * @param aEvent the event to initialize.
   * @param aPoint message position in physical coordinates.
   */
  void InitEvent(mozilla::WidgetGUIEvent& aEvent,
                 LayoutDeviceIntPoint* aPoint = nullptr);

  /*
   * Returns WidgetEventTime instance which is initialized with current message
   * time.
   */
  WidgetEventTime CurrentMessageWidgetEventTime() const;

  /*
   * Dispatch a gecko keyboard event for this widget. This
   * is called by KeyboardLayout to dispatch gecko events.
   * Returns true if it's consumed.  Otherwise, false.
   */
  bool DispatchKeyboardEvent(mozilla::WidgetKeyboardEvent* aEvent);

  /*
   * Dispatch a gecko wheel event for this widget. This
   * is called by ScrollHandler to dispatch gecko events.
   * Returns true if it's consumed.  Otherwise, false.
   */
  bool DispatchWheelEvent(mozilla::WidgetWheelEvent* aEvent);

  /*
   * Dispatch a gecko content command event for this widget. This
   * is called by ScrollHandler to dispatch gecko events.
   * Returns true if it's consumed.  Otherwise, false.
   */
  bool DispatchContentCommandEvent(mozilla::WidgetContentCommandEvent* aEvent);

  /*
   * Return the parent window, if it exists.
   */
  nsWindow* GetParentWindowBase(bool aIncludeOwner);

  // nsIWidget interface
  using nsBaseWidget::Create;  // for Create signature not overridden here
  [[nodiscard]] nsresult Create(nsIWidget* aParent,
                                const LayoutDeviceIntRect& aRect,
                                InitData* aInitData = nullptr) override;
  void Destroy() override;
  float GetDPI() override;
  double GetDefaultScaleInternal() override;
  void DidClearParent(nsIWidget* aOldParent) override;
  int32_t LogToPhys(double aValue);
  mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScale() override {
    if (mozilla::widget::WinUtils::IsPerMonitorDPIAware()) {
      return mozilla::DesktopToLayoutDeviceScale(1.0);
    } else {
      return mozilla::DesktopToLayoutDeviceScale(GetDefaultScaleInternal());
    }
  }

  void Show(bool aState) override;
  bool IsVisible() const override;
  void ConstrainPosition(DesktopIntPoint&) override;
  void SetSizeConstraints(const SizeConstraints& aConstraints) override;
  void LockAspectRatio(bool aShouldLock) override;
  const SizeConstraints GetSizeConstraints() override;
  void SetInputRegion(const InputRegion&) override;
  void Move(double aX, double aY) override;
  void Resize(double aWidth, double aHeight, bool aRepaint) override;
  void Resize(double aX, double aY, double aWidth, double aHeight,
              bool aRepaint) override;
  void SetSizeMode(nsSizeMode aMode) override;
  nsSizeMode SizeMode() override;
  void GetWorkspaceID(nsAString& workspaceID) override;
  void MoveToWorkspace(const nsAString& workspaceID) override;
  void SuppressAnimation(bool aSuppress) override;
  void Enable(bool aState) override;
  bool IsEnabled() const override;
  void SetFocus(Raise, mozilla::dom::CallerType aCallerType) override;
  LayoutDeviceIntRect GetBounds() override;
  LayoutDeviceIntRect GetScreenBounds() override;
  [[nodiscard]] nsresult GetRestoredBounds(LayoutDeviceIntRect& aRect) override;
  LayoutDeviceIntRect GetClientBounds() override;
  LayoutDeviceIntPoint GetClientOffset() override;
  LayoutDeviceIntSize GetSize() const;
  void SetCursor(const Cursor&) override;
  bool PrepareForFullscreenTransition(nsISupports** aData) override;
  void PerformFullscreenTransition(FullscreenTransitionStage aStage,
                                   uint16_t aDuration, nsISupports* aData,
                                   nsIRunnable* aCallback) override;
  void CleanupFullscreenTransition() override;
  nsresult MakeFullScreen(bool aFullScreen) override;
  void HideWindowChrome(bool aShouldHide) override;
  void Invalidate(bool aEraseBackground = false, bool aUpdateNCArea = false,
                  bool aIncludeChildren = false);
  void Invalidate(const LayoutDeviceIntRect& aRect) override;
  void* GetNativeData(uint32_t aDataType) override;
  void FreeNativeData(void* data, uint32_t aDataType) override;
  nsresult SetTitle(const nsAString& aTitle) override;
  void SetIcon(const nsAString& aIconSpec) override;
  LayoutDeviceIntPoint WidgetToScreenOffset() override;
  LayoutDeviceIntMargin NormalSizeModeClientToWindowMargin() override;
  nsresult DispatchEvent(mozilla::WidgetGUIEvent* aEvent,
                         nsEventStatus& aStatus) override;
  void EnableDragDrop(bool aEnable) override;
  void CaptureMouse(bool aCapture);
  void CaptureRollupEvents(bool aDoCapture) override;
  [[nodiscard]] nsresult GetAttention(int32_t aCycleCount) override;
  bool HasPendingInputEvent() override;
  WindowRenderer* GetWindowRenderer() override;
  void SetCompositorWidgetDelegate(CompositorWidgetDelegate* delegate) override;
  [[nodiscard]] nsresult OnDefaultButtonLoaded(
      const LayoutDeviceIntRect& aButtonRect) override;
  nsresult SynthesizeNativeKeyEvent(int32_t aNativeKeyboardLayout,
                                    int32_t aNativeKeyCode,
                                    uint32_t aModifierFlags,
                                    const nsAString& aCharacters,
                                    const nsAString& aUnmodifiedCharacters,
                                    nsIObserver* aObserver) override;
  nsresult SynthesizeNativeMouseEvent(LayoutDeviceIntPoint aPoint,
                                      NativeMouseMessage aNativeMessage,
                                      mozilla::MouseButton aButton,
                                      nsIWidget::Modifiers aModifierFlags,
                                      nsIObserver* aObserver) override;

  nsresult SynthesizeNativeMouseMove(LayoutDeviceIntPoint aPoint,
                                     nsIObserver* aObserver) override {
    return SynthesizeNativeMouseEvent(
        aPoint, NativeMouseMessage::Move, mozilla::MouseButton::eNotPressed,
        nsIWidget::Modifiers::NO_MODIFIERS, aObserver);
  }

  nsresult SynthesizeNativeMouseScrollEvent(
      LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage, double aDeltaX,
      double aDeltaY, double aDeltaZ, uint32_t aModifierFlags,
      uint32_t aAdditionalFlags, nsIObserver* aObserver) override;

  nsresult SynthesizeNativeTouchpadPan(TouchpadGesturePhase aEventPhase,
                                       LayoutDeviceIntPoint aPoint,
                                       double aDeltaX, double aDeltaY,
                                       int32_t aModifierFlagsn,
                                       nsIObserver* aObserver) override;

  void SetInputContext(const InputContext& aContext,
                       const InputContextAction& aAction) override;
  InputContext GetInputContext() override;
  TextEventDispatcherListener* GetNativeTextEventDispatcherListener() override;
  void SetTransparencyMode(TransparencyMode aMode) override;
  TransparencyMode GetTransparencyMode() override { return mTransparencyMode; }
  void SetCustomTitlebar(bool) override;
  void SetResizeMargin(mozilla::LayoutDeviceIntCoord aResizeMargin) override;
  void UpdateWindowDraggingRegion(
      const LayoutDeviceIntRegion& aRegion) override;

  uint32_t GetMaxTouchPoints() const override;
  void SetIsEarlyBlankWindow(bool) override;

  /**
   * Event helpers
   */
  enum class IsNonclient : bool { No = false, Yes = true };
  bool DispatchMouseEvent(mozilla::EventMessage aEventMessage, WPARAM wParam,
                          LPARAM lParam, bool aIsContextMenuKey,
                          int16_t aButton, uint16_t aInputSource,
                          WinPointerInfo* aPointerInfo = nullptr,
                          IsNonclient aIgnoreAPZ = IsNonclient::No);
  void DispatchPendingEvents();
  void DispatchCustomEvent(const nsString& eventName);

#ifdef ACCESSIBILITY
  /**
   * Return an accessible associated with the window.
   */
  mozilla::a11y::LocalAccessible* GetAccessible();
#endif  // ACCESSIBILITY

  /**
   * Window utilities
   */
  nsWindow* GetTopLevelWindow(bool aStopOnDialogOrPopup);
  WNDPROC GetPrevWindowProc() { return mPrevWndProc.valueOr(nullptr); }
  WindowHook& GetWindowHook() { return mWindowHook; }
  nsWindow* GetParentWindow(bool aIncludeOwner);

  bool WidgetTypeSupportsAcceleration() override;

  void ForcePresent();
  bool TouchEventShouldStartDrag(mozilla::EventMessage aEventMessage,
                                 LayoutDeviceIntPoint aEventPoint);

  void SetSmallIcon(HICON aIcon);
  void SetBigIcon(HICON aIcon);
  void SetSmallIconNoData();
  void SetBigIconNoData();

  void UpdateMicaBackdrop(bool aForce = false);

  static void SetIsRestoringSession(const bool aIsRestoringSession) {
    sIsRestoringSession = aIsRestoringSession;
  }

  bool IsRTL() const { return mIsRTL; }

  /**
   * AssociateDefaultIMC() associates or disassociates the default IMC for
   * the window.
   *
   * @param aAssociate    TRUE, associates the default IMC with the window.
   *                      Otherwise, disassociates the default IMC from the
   *                      window.
   * @return              TRUE if this method associated the default IMC with
   *                      disassociated window or disassociated the default IMC
   *                      from associated window.
   *                      Otherwise, i.e., if this method did nothing actually,
   *                      FALSE.
   */
  bool AssociateDefaultIMC(bool aAssociate);

  bool HasTaskbarIconBeenCreated() { return mHasTaskbarIconBeenCreated; }
  // Called when either the nsWindow or an nsITaskbarTabPreview receives the
  // noticiation that this window has its icon placed on the taskbar.
  void SetHasTaskbarIconBeenCreated(bool created = true) {
    mHasTaskbarIconBeenCreated = created;
  }

  // Getter/setter for the nsITaskbarWindowPreview for this nsWindow
  already_AddRefed<nsITaskbarWindowPreview> GetTaskbarPreview() {
    nsCOMPtr<nsITaskbarWindowPreview> preview(
        do_QueryReferent(mTaskbarPreview));
    return preview.forget();
  }
  void SetTaskbarPreview(nsITaskbarWindowPreview* preview) {
    mTaskbarPreview = do_GetWeakReference(preview);
  }

  // Open file picker tracking
  void PickerOpen();
  void PickerClosed();

  bool DestroyCalled() { return mDestroyCalled; }

  bool IsPopup();
  bool ShouldUseOffMainThreadCompositing() override;

  const IMEContext& DefaultIMC() const { return mDefaultIMC; }

  void GetCompositorWidgetInitData(
      mozilla::widget::CompositorWidgetInitData* aInitData) override;
  bool IsTouchWindow() const { return mTouchWindow; }
  bool SynchronouslyRepaintOnResize() override;
  void MaybeDispatchInitialFocusEvent() override;

  void LocalesChanged() override;

  void NotifyOcclusionState(mozilla::widget::OcclusionState aState) override;
  void MaybeEnableWindowOcclusion(bool aEnable);

  /*
   * Return the HWND or null for this widget.
   */
  HWND GetWindowHandle() {
    return static_cast<HWND>(GetNativeData(NS_NATIVE_WINDOW));
  }

  /*
   * Touch input injection apis
   */
  nsresult SynthesizeNativeTouchPoint(uint32_t aPointerId,
                                      TouchPointerState aPointerState,
                                      LayoutDeviceIntPoint aPoint,
                                      double aPointerPressure,
                                      uint32_t aPointerOrientation,
                                      nsIObserver* aObserver) override;
  nsresult ClearNativeTouchSequence(nsIObserver* aObserver) override;

  nsresult SynthesizeNativePenInput(uint32_t aPointerId,
                                    TouchPointerState aPointerState,
                                    LayoutDeviceIntPoint aPoint,
                                    double aPressure, uint32_t aRotation,
                                    int32_t aTiltX, int32_t aTiltY,
                                    int32_t aButton,
                                    nsIObserver* aObserver) override;

  /*
   * WM_APPCOMMAND common handler.
   * Sends events via NativeKey::HandleAppCommandMessage().
   */
  bool HandleAppCommandMsg(const MSG& aAppCommandMsg, LRESULT* aRetValue);

  const InputContext& InputContextRef() const { return mInputContext; }

 private:
  using TimeStamp = mozilla::TimeStamp;
  using TimeDuration = mozilla::TimeDuration;
  using TaskbarWindowPreview = mozilla::widget::TaskbarWindowPreview;
  using NativeKey = mozilla::widget::NativeKey;
  using MSGResult = mozilla::widget::MSGResult;
  using PlatformCompositorWidgetDelegate =
      mozilla::widget::PlatformCompositorWidgetDelegate;

  struct Desktop {
    // Cached GUID of the virtual desktop this window should be on.
    // This value may be stale.
    nsString mID;
    bool mUpdateIsQueued = false;
  };

  class PointerInfo {
   public:
    enum class PointerType : uint8_t {
      TOUCH,
      PEN,
    };

    PointerInfo(int32_t aPointerId, LayoutDeviceIntPoint& aPoint,
                PointerType aType)
        : mPointerId(aPointerId), mPosition(aPoint), mType(aType) {}

    int32_t mPointerId;
    LayoutDeviceIntPoint mPosition;
    PointerType mType;
  };

  class FrameState {
   public:
    explicit FrameState(nsWindow* aWindow);

    void ConsumePreXULSkeletonState(bool aWasMaximized);

    // Whether we should call ShowWindow with the relevant size mode if needed.
    // We want to avoid that when Windows is already performing the change for
    // us (via the SWP_FRAMECHANGED messages).
    enum class DoShowWindow : bool { No, Yes };

    void EnsureSizeMode(nsSizeMode, DoShowWindow = DoShowWindow::Yes);
    void EnsureFullscreenMode(bool, DoShowWindow = DoShowWindow::Yes);
    void OnFrameChanging();
    void OnFrameChanged();

    nsSizeMode GetSizeMode() const;

    void CheckInvariant() const;

   private:
    void SetSizeModeInternal(nsSizeMode, DoShowWindow);

    nsSizeMode mSizeMode = nsSizeMode_Normal;
    // The old size mode before going into fullscreen mode. This should never
    // be nsSizeMode_Fullscreen.
    nsSizeMode mPreFullscreenSizeMode = nsSizeMode_Normal;
    // Whether we're in fullscreen. We need to keep this state out of band,
    // rather than just using mSizeMode, because a window can be minimized
    // while fullscreen, and we don't store the fullscreen state anywhere else.
    bool mFullscreenMode = false;
    nsWindow* mWindow;
  };

  // Manager for taskbar-hiding. No persistent state.
  class TaskbarConcealer;

  // A magic number to identify the FAKETRACKPOINTSCROLLABLE window created
  // when the trackpoint hack is enabled.
  enum { eFakeTrackPointScrollableID = 0x46545053 };

  // Used for displayport suppression during window resize
  enum ResizeState { NOT_RESIZING, IN_SIZEMOVE, RESIZING, MOVING };

  ~nsWindow() override;

  void WindowUsesOMTC() override;
  void RegisterTouchWindow() override;

  /**
   * Callbacks
   */
  static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam,
                                     LPARAM lParam);
  static LRESULT CALLBACK WindowProcInternal(HWND hWnd, UINT msg, WPARAM wParam,
                                             LPARAM lParam);

  static BOOL CALLBACK DispatchStarvedPaints(HWND aTopWindow, LPARAM aMsg);
  static BOOL CALLBACK RegisterTouchForDescendants(HWND aTopWindow,
                                                   LPARAM aMsg);
  static BOOL CALLBACK UnregisterTouchForDescendants(HWND aTopWindow,
                                                     LPARAM aMsg);
  static LRESULT CALLBACK MozSpecialMsgFilter(int code, WPARAM wParam,
                                              LPARAM lParam);
  static LRESULT CALLBACK MozSpecialWndProc(int code, WPARAM wParam,
                                            LPARAM lParam);
  static LRESULT CALLBACK MozSpecialMouseProc(int code, WPARAM wParam,
                                              LPARAM lParam);
  static VOID CALLBACK HookTimerForPopups(HWND hwnd, UINT uMsg, UINT idEvent,
                                          DWORD dwTime);

  /**
   * Window utilities
   */
  LPARAM lParamToScreen(LPARAM lParam);
  LPARAM lParamToClient(LPARAM lParam);

  WPARAM wParamFromGlobalMouseState();

  bool AssociateWithNativeWindow();
  void DissociateFromNativeWindow();
  bool CanTakeFocus();
  bool UpdateNonClientMargins(bool aReflowWindow = true);
  void UpdateDarkModeToolbar();
  void ResetLayout();
  HWND GetOwnerWnd() const { return ::GetWindow(mWnd, GW_OWNER); }
  bool IsOwnerForegroundWindow() const {
    HWND owner = GetOwnerWnd();
    return owner && owner == ::GetForegroundWindow();
  }
  bool IsForegroundWindow() const { return mWnd == ::GetForegroundWindow(); }
  bool IsPopup() const { return mWindowType == WindowType::Popup; }
  bool IsCloaked() const { return mIsCloaked; }

  /**
   * Event processing helpers
   */
  HWND GetTopLevelForFocus(HWND aCurWnd);
  void DispatchFocusToTopLevelWindow(bool aIsActivate);
  bool DispatchStandardEvent(mozilla::EventMessage aMsg);
  void RelayMouseEvent(UINT aMsg, WPARAM wParam, LPARAM lParam);
  bool ProcessMessage(UINT msg, WPARAM& wParam, LPARAM& lParam,
                      LRESULT* aRetValue);
  // We wrap this in ProcessMessage so we can log the return value
  bool ProcessMessageInternal(UINT msg, WPARAM& wParam, LPARAM& lParam,
                              LRESULT* aRetValue);
  bool ExternalHandlerProcessMessage(UINT aMessage, WPARAM& aWParam,
                                     LPARAM& aLParam, MSGResult& aResult);
  LRESULT ProcessCharMessage(const MSG& aMsg, bool* aEventDispatched);
  LRESULT ProcessKeyUpMessage(const MSG& aMsg, bool* aEventDispatched);
  LRESULT ProcessKeyDownMessage(const MSG& aMsg, bool* aEventDispatched);
  static bool EventIsInsideWindow(
      nsWindow* aWindow,
      mozilla::Maybe<POINT> aEventPoint = mozilla::Nothing());
  static void PostSleepWakeNotification(const bool aIsSleepMode);
  int32_t ClientMarginHitTestPoint(int32_t aX, int32_t aY);
  void SetWindowButtonRect(WindowButtonType aButtonType,
                           const LayoutDeviceIntRect& aClientRect) override {
    mWindowBtnRect[aButtonType] = aClientRect;
  }
  TimeStamp GetMessageTimeStamp(LONG aEventTime) const;
  static void UpdateFirstEventTime(DWORD aEventTime);
  void FinishLiveResizing(ResizeState aNewState);
  mozilla::Maybe<mozilla::PanGestureInput> ConvertTouchToPanGesture(
      const mozilla::MultiTouchInput& aTouchInput, PTOUCHINPUT aOriginalEvent);
  void DispatchTouchOrPanGestureInput(mozilla::MultiTouchInput& aTouchInput,
                                      PTOUCHINPUT aOSEvent);

  /**
   * Event handlers
   */
  void OnDestroy() override;
  bool OnResize(const LayoutDeviceIntSize& aSize);
  void OnSizeModeChange();
  bool OnGesture(WPARAM wParam, LPARAM lParam);
  bool OnTouch(WPARAM wParam, LPARAM lParam);
  bool OnHotKey(WPARAM wParam, LPARAM lParam);
  bool OnPaint();
  void OnWindowPosChanging(WINDOWPOS* info);
  void OnWindowPosChanged(WINDOWPOS* wp);
  void OnSysColorChanged();
  void OnDPIChanged(int32_t x, int32_t y, int32_t width, int32_t height);
  bool OnPointerEvents(UINT msg, WPARAM wParam, LPARAM lParam);
  bool OnPenPointerEvents(uint32_t aPointerId, UINT aMsg, WPARAM aWParam,
                          LPARAM aLParam);
  bool OnTouchPointerEvents(uint32_t aPointerId, UINT aMsg, WPARAM aWParam,
                            LPARAM aLParam);

  /**
   * Function that registers when the user has been active (used for detecting
   * when the user is idle).
   */
  void UserActivity();

  DWORD WindowStyle();
  DWORD WindowExStyle();

  /**
   * Popup hooks
   */
  static void ScheduleHookTimer(HWND aWnd, UINT aMsgId);
  static void RegisterSpecialDropdownHooks();
  static void UnregisterSpecialDropdownHooks();
  static bool GetPopupsToRollup(
      nsIRollupListener* aRollupListener, uint32_t* aPopupsToRollup,
      mozilla::Maybe<POINT> aEventPoint = mozilla::Nothing());
  static bool DealWithPopups(HWND inWnd, UINT inMsg, WPARAM inWParam,
                             LPARAM inLParam, LRESULT* outResult);
  bool IsSimulatedClientArea(int32_t clientX, int32_t clientY);
  bool IsWindowButton(int32_t hitTestResult);

  void UpdateOpaqueRegion(const LayoutDeviceIntRegion&) override;
  void UpdateOpaqueRegionInternal();
  LayoutDeviceIntRegion GetOpaqueRegionForTesting() const override {
    return mOpaqueRegion;
  }
  // Gets the translucent region, in client coordinates.
  LayoutDeviceIntRegion GetTranslucentRegion();
  void MaybeInvalidateTranslucentRegion();

  void SetColorScheme(const mozilla::Maybe<mozilla::ColorScheme>&) override;
  void SetMicaBackdrop(bool) override;

  bool DispatchTouchEventFromWMPointer(UINT msg, LPARAM aLParam,
                                       const WinPointerInfo& aPointerInfo,
                                       mozilla::MouseButton aButton);

  static bool IsAsyncResponseEvent(UINT aMsg, LRESULT& aResult);
  void IPCWindowProcHandler(UINT& msg, WPARAM& wParam, LPARAM& lParam);

  /**
   * Misc.
   */
  void StopFlashing();
  static HWND WindowAtMouse();
  static bool IsTopLevelMouseExit(HWND aWnd);
  LayoutDeviceIntRegion GetRegionToPaint(const PAINTSTRUCT& ps, HDC aDC) const;
  nsIWidgetListener* GetPaintListener();

  void CreateCompositor() override;
  void DestroyCompositor() override;
  void RequestFxrOutput() override;

  void RecreateDirectManipulationIfNeeded();
  void ResizeDirectManipulationViewport();
  void DestroyDirectManipulation();

  bool NeedsToTrackWindowOcclusionState();

  void AsyncUpdateWorkspaceID(Desktop& aDesktop);

  // See bug 603793
  static bool HasBogusPopupsDropShadowOnMultiMonitor();

  static void InitMouseWheelScrollData();

  void ChangedDPI();

  static bool InitTouchInjection();

  bool InjectTouchPoint(uint32_t aId, LayoutDeviceIntPoint& aPoint,
                        POINTER_FLAGS aFlags, uint32_t aPressure = 1024,
                        uint32_t aOrientation = 90);

  void OnFullscreenChanged(nsSizeMode aOldSizeMode, bool aFullScreen);
  void TryDwmResizeHack();

  static void OnCloakEvent(HWND aWnd, bool aCloaked);
  void OnCloakChanged(bool aCloaked);

#ifdef DEBUG
  nsresult SetHiDPIMode(bool aHiDPI) override;
  nsresult RestoreHiDPIMode() override;
#endif

  // Get the orientation of the hidden taskbar, on the screen that this window
  // is on, or Nothing if taskbar isn't hidden.
  mozilla::Maybe<UINT> GetHiddenTaskbarEdge();

  static bool sTouchInjectInitialized;
  static InjectTouchInputPtr sInjectTouchFuncPtr;
  static uint32_t sInstanceCount;
  static nsWindow* sCurrentWindow;
  static bool sIsOleInitialized;
  static Cursor sCurrentCursor;
  static bool sJustGotDeactivate;
  static bool sJustGotActivate;
  static bool sIsInMouseCapture;
  static bool sIsRestoringSession;

  // Message postponement hack. See the definition-site of
  // WndProcUrgentInvocation::sDepth for details.
  struct MOZ_STACK_CLASS WndProcUrgentInvocation {
    struct Marker {
      Marker() { ++sDepth; }
      ~Marker() { --sDepth; }
    };
    inline static bool IsActive() { return sDepth > 0; }
    static size_t sDepth;
  };

  // Hook Data Members for Dropdowns. sProcessHook Tells the
  // hook methods whether they should be processing the hook
  // messages.
  static HHOOK sMsgFilterHook;
  static HHOOK sCallProcHook;
  static HHOOK sCallMouseHook;
  static bool sProcessHook;
  static UINT sRollupMsgId;
  static HWND sRollupMsgWnd;
  static UINT sHookTimerId;

  // Used to prevent dispatching mouse events that do not originate from user
  // input.
  static POINT sLastMouseMovePoint;

  nsClassHashtable<nsUint32HashKey, PointerInfo> mActivePointers;

  // This is used by SynthesizeNativeTouchPoint to maintain state between
  // multiple synthesized points, in the case where we can't call InjectTouch
  // directly.
  mozilla::UniquePtr<mozilla::MultiTouchInput> mSynthesizedTouchInput;

  InputContext mInputContext;

  nsIntSize mLastSize = nsIntSize(0, 0);
  nsIntPoint mLastPoint;
  HWND mWnd = nullptr;
  HWND mTransitionWnd = nullptr;
  mozilla::Maybe<WNDPROC> mPrevWndProc;
  IMEContext mDefaultIMC;
  HDEVNOTIFY mDeviceNotifyHandle = nullptr;
  bool mInDtor = false;
  bool mIsVisible = false;
  bool mIsCloaked = false;
  bool mTouchWindow = false;
  bool mDisplayPanFeedback = false;
  bool mHideChrome = false;
  bool mIsRTL = false;
  bool mMousePresent = false;
  bool mSimulatedClientArea = false;
  bool mDestroyCalled = false;
  bool mOpeningAnimationSuppressed = false;
  bool mAlwaysOnTop = false;
  bool mIsEarlyBlankWindow = false;
  bool mIsShowingPreXULSkeletonUI = false;
  bool mResizable = false;
  bool mHasBeenShown = false;
  // Whether we're an alert window. Alert windows don't have taskbar icons and
  // don't steal focus from other windows when opened. They're also expected to
  // be of type WindowType::Dialog.
  bool mIsAlert = false;
  bool mIsPerformingDwmFlushHack = false;
  bool mDraggingWindowWithMouse = false;
  mozilla::Maybe<Styles> mOldStyles;
  nsNativeDragTarget* mNativeDragTarget = nullptr;
  HKL mLastKeyboardLayout = 0;
  mozilla::CheckInvariantWrapper<FrameState> mFrameState;
  WindowHook mWindowHook;
  uint32_t mPickerDisplayCount = 0;
  HICON mIconSmall = nullptr;
  HICON mIconBig = nullptr;
  HWND mLastKillFocusWindow = nullptr;
  PlatformCompositorWidgetDelegate* mCompositorWidgetDelegate = nullptr;

  LayoutDeviceIntMargin NonClientSizeMargin() const {
    return NonClientSizeMargin(mCustomNonClientMetrics.mOffset);
  }
  LayoutDeviceIntMargin NonClientSizeMargin(
      const LayoutDeviceIntMargin& aNonClientOffset) const;
  LayoutDeviceIntMargin NormalWindowNonClientOffset() const;

  struct CustomNonClientMetrics {
    // Width of the left and right portions of the resize region
    mozilla::LayoutDeviceIntCoord mHorResizeMargin;
    // Height of the top and bottom portions of the resize region
    mozilla::LayoutDeviceIntCoord mVertResizeMargin;
    // Height of the caption plus border
    mozilla::LayoutDeviceIntCoord mCaptionHeight;
    // Pre-calculated outward offset applied to the default frame
    LayoutDeviceIntMargin mOffset;

    LayoutDeviceIntMargin ResizeMargins() const {
      return {mVertResizeMargin, mHorResizeMargin, mVertResizeMargin,
              mHorResizeMargin};
    }

    LayoutDeviceIntMargin DefaultMargins() const {
      auto margins = ResizeMargins();
      margins.top += mCaptionHeight;
      return margins;
    }
  } mCustomNonClientMetrics;

  // Indicates the custom titlebar is enabled.
  bool mCustomNonClient = false;
  // Whether we want to draw to the titlebar once the chrome shows. (Always
  // Nothing if mHideChrome is false.)
  mozilla::Maybe<bool> mCustomTitlebarOnceChromeShows;
  // Custom extra resize margin width.
  mozilla::LayoutDeviceIntCoord mCustomResizeMargin{0};

  // not yet set, will be calculated on first use
  double mDefaultScale = -1.0;

  // not yet set, will be calculated on first use
  float mAspectRatio = 0.0;

  nsCOMPtr<nsIUserIdleServiceInternal> mIdleService;

  // Draggable titlebar region maintained by UpdateWindowDraggingRegion
  LayoutDeviceIntRegion mDraggableRegion;
  // Opaque region maintained by UpdateOpaqueRegion (relative to the client
  // area).
  LayoutDeviceIntRegion mOpaqueRegion;

  // Graphics
  LayoutDeviceIntRect mLastPaintBounds;
  // The region of the window we know is cleared to transparent already,
  // in client coords.
  LayoutDeviceIntRegion mClearedRegion;

  ResizeState mResizeState = NOT_RESIZING;

  // Transparency
  TransparencyMode mTransparencyMode = TransparencyMode::Opaque;

  // Win7 Gesture processing and management
  nsWinGesture mGesture;

  // Weak ref to the nsITaskbarWindowPreview associated with this window
  nsWeakPtr mTaskbarPreview = nullptr;

  // The input region that determines whether mouse events should be ignored
  // and pass through to the window below. This is currently only used for
  // popups.
  InputRegion mInputRegion;

  // True if the taskbar (possibly through the tab preview) tells us that the
  // icon has been created on the taskbar.
  bool mHasTaskbarIconBeenCreated = false;

  // Whether we're in the process of sending a WM_SETTEXT ourselves
  bool mSendingSetText = false;

  // Whether we're a PIP window.
  bool mPIPWindow : 1;

  // Whether we are asked to render a mica backdrop.
  bool mMicaBackdrop : 1;

  int32_t mCachedHitTestResult = 0;

  // The point in time at which the last paint completed. We use this to avoid
  //  painting too rapidly in response to frequent input events.
  TimeStamp mLastPaintEndTime;

  // Caching for hit test results (in client coordinates)
  LayoutDeviceIntPoint mCachedHitTestPoint;
  TimeStamp mCachedHitTestTime;

  RefPtr<mozilla::widget::InProcessWinCompositorWidget> mBasicLayersSurface;

  double mSizeConstraintsScale;  // scale in effect when setting constraints

  // Will be calculated when layer manager is created.
  int32_t mMaxTextureSize = -1;

  // Pointer events processing and management
  WinPointerEvents mPointerEvents;

  ScreenPoint mLastPanGestureFocus;

  // When true, used to indicate an async call to RequestFxrOutput to the GPU
  // process after the Compositor is created
  bool mRequestFxrOutputPending = false;

  // A stack based class used in DispatchMouseEvent() to tell whether we should
  // NOT open context menu when we receives WM_CONTEXTMENU after the
  // DispatchMouseEvent calls.
  // This class now works only in the case where a mouse up event happened in
  // the overscroll gutter.
  class MOZ_STACK_CLASS ContextMenuPreventer final {
   public:
    explicit ContextMenuPreventer(nsWindow* aWindow)
        : mWindow(aWindow), mNeedsToPreventContextMenu(false) {};
    ~ContextMenuPreventer() {
      mWindow->mNeedsToPreventContextMenu = mNeedsToPreventContextMenu;
    }
    void Update(const mozilla::WidgetMouseEvent& aEvent,
                const nsIWidget::ContentAndAPZEventStatus& aEventStatus);

   private:
    nsWindow* mWindow;
    bool mNeedsToPreventContextMenu = false;
  };
  friend class ContextMenuPreventer;
  bool mNeedsToPreventContextMenu = false;

  mozilla::UniquePtr<mozilla::widget::DirectManipulationOwner> mDmOwner;

  // Client rect for minimize, maximize and close buttons.
  mozilla::EnumeratedArray<WindowButtonType, LayoutDeviceIntRect,
                           size_t(WindowButtonType::Count)>
      mWindowBtnRect;

  mozilla::DataMutex<Desktop> mDesktopId;

  friend class nsWindowGfx;

  static constexpr int kHiddenTaskbarSize = 2;
};

#endif  // WIDGET_WINDOWS_NSWINDOW_H_
