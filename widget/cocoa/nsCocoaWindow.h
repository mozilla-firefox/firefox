/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsCocoaWindow_h_
#define nsCocoaWindow_h_

#undef DARWIN

#import <Cocoa/Cocoa.h>

#include "mozilla/RefPtr.h"
#include "mozilla/layers/NativeLayerRootRemoteMacChild.h"
#include "mozilla/layers/NativeLayerRootRemoteMacParent.h"
#include "nsIWidget.h"
#include "nsCocoaUtils.h"
#include "nsTouchBar.h"
#include "ViewRegion.h"
#include "mozView.h"
#include <dlfcn.h>
#include <queue>

class nsCocoaWindow;
class nsChildView;
class nsMenuBarX;
@class ChildView;

namespace mozilla {
enum class NativeKeyBindingsType : uint8_t;
class VibrancyManager;
namespace widget {
class PlatformCompositorWidgetDelegate;
class TextInputHandler;
}  // namespace widget
}  // namespace mozilla

// NSWindow subclass that is the base class for all of our own window classes.
// Among other things, this class handles the storage of those settings that
// need to be persisted across window destruction and reconstruction, i.e. when
// switching to and from fullscreen mode.
// We don't save shadow, transparency mode or background color because it's not
// worth the hassle - Gecko will reset them anyway as soon as the window is
// resized.
@interface BaseWindow : NSWindow {
  // Data Storage
  NSMutableDictionary* mState;
  BOOL mDrawsIntoWindowFrame;

  // Invalidation disabling
  BOOL mDisabledNeedsDisplay;

  NSTrackingArea* mTrackingArea;
  NSView* mViewWithTrackingArea;

  NSRect mDirtyRect;

  BOOL mBeingShown;
  BOOL mIsAnimationSuppressed;

  nsTouchBar* mTouchBar;
}

- (void)importState:(NSDictionary*)aState;
- (NSMutableDictionary*)exportState;
- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState;
- (BOOL)drawsContentsIntoWindowFrame;
- (NSRect)childViewFrameRectForCurrentBounds;
- (void)updateChildViewFrameRect;

// These two methods are like contentRectForFrameRect and
// frameRectForContentRect, but they deal with the rect of the window's "main
// ChildView" instead of the rect of the window's content view. The two are
// sometimes sized differently: The window's content view always covers the
// entire window, whereas the ChildView only covers the full window when
// drawsContentsIntoWindowFrame is YES. When drawsContentsIntoWindowFrame is NO,
// there's a titlebar-sized gap above the ChildView within the content view.
- (NSRect)childViewRectForFrameRect:(NSRect)aFrameRect;
- (NSRect)frameRectForChildViewRect:(NSRect)aChildViewRect;

- (void)mouseEntered:(NSEvent*)aEvent;
- (void)mouseExited:(NSEvent*)aEvent;
- (void)mouseMoved:(NSEvent*)aEvent;
- (NSView*)trackingAreaView;
- (void)createTrackingArea;
- (void)removeTrackingArea;

- (void)setIsBeingShown:(BOOL)aValue;
- (BOOL)isBeingShown;
- (BOOL)isVisibleOrBeingShown;

- (void)setIsAnimationSuppressed:(BOOL)aValue;
- (BOOL)isAnimationSuppressed;

// Returns an autoreleased NSArray containing the NSViews that we consider the
// "contents" of this window. All views in the returned array are subviews of
// this window's content view. However, the array may not include all of the
// content view's subviews; concretely, the ToolbarWindow implementation will
// exclude its MOZTitlebarView from the array that is returned here.
// In the vast majority of cases, the array will only have a single element:
// this window's mainChildView.
- (NSArray<NSView*>*)contentViewContents;

- (ChildView*)mainChildView;

- (void)disableSetNeedsDisplay;
- (void)enableSetNeedsDisplay;

- (NSRect)getAndResetNativeDirtyRect;

- (void)setEffectViewWrapperForStyle:(mozilla::WindowShadow)aStyle;
@property(nonatomic) mozilla::WindowShadow shadowStyle;

- (void)updateTitlebarTransparency;
- (void)setTitlebarSeparatorStyle:(NSTitlebarSeparatorStyle)aStyle
    API_AVAILABLE(macos(11.0));

- (void)releaseJSObjects;

@end

@interface NSWindow (Undocumented)
- (NSDictionary*)shadowParameters;

// Present in the same form on macOS since at least macOS 10.5.
- (NSRect)contentRectForFrameRect:(NSRect)windowFrame
                        styleMask:(NSUInteger)windowStyle;
- (NSRect)frameRectForContentRect:(NSRect)windowContentRect
                        styleMask:(NSUInteger)windowStyle;

// Present since at least macOS 10.5.  The OS calls this method on NSWindow
// (and its subclasses) to find out which NSFrameView subclass to instantiate
// to create its "frame view".
+ (Class)frameViewClassForStyleMask:(NSUInteger)styleMask;

@end

@interface PopupWindow : BaseWindow <NSPopoverDelegate> {
 @private
  BOOL mIsContextMenu;

  // NSPopover support for native appearance
  NSPopover* mPopover;
  NSViewController* mPopoverViewController;
  BOOL mUsePopover;
}

- (id)initWithContentRect:(NSRect)contentRect
                styleMask:(NSUInteger)styleMask
                  backing:(NSBackingStoreType)bufferingType
                    defer:(BOOL)deferCreation;
- (BOOL)isContextMenu;
- (void)setIsContextMenu:(BOOL)flag;
- (BOOL)canBecomeMainWindow;

// NSPopover support
- (void)setAllowPopover;
- (BOOL)usePopover;
- (void)showPopoverRelativeToRect:(NSRect)positioningRect
                           ofView:(NSView*)positioningView
                    preferredEdge:(NSRectEdge)preferredEdge
                     hiddenAnchor:(BOOL)hiddenAnchor;
- (void)closePopover;
- (void)updatePopoverContent;

@end

@interface BorderlessWindow : BaseWindow {
}

- (BOOL)canBecomeKeyWindow;
- (BOOL)canBecomeMainWindow;

@end

@interface WindowDelegate : NSObject <NSWindowDelegate> {
  nsCocoaWindow* mGeckoWindow;  // [WEAK] (we are owned by the window)
  // Used to avoid duplication when we send NS_ACTIVATE and
  // NS_DEACTIVATE to Gecko for toplevel widgets.  Starts out
  // false.
  bool mToplevelActiveState;
  BOOL mHasEverBeenZoomed;
}
+ (void)paintMenubarForWindow:(NSWindow*)aWindow;
- (id)initWithGeckoWindow:(nsCocoaWindow*)geckoWind;
- (void)windowDidResize:(NSNotification*)aNotification;
- (nsCocoaWindow*)geckoWidget;
- (bool)toplevelActiveState;
- (void)sendToplevelActivateEvents;
- (void)sendToplevelDeactivateEvents;
@end

@interface FullscreenTitlebarTracker : NSTitlebarAccessoryViewController
- (FullscreenTitlebarTracker*)init;
@end

// NSWindow subclass for handling windows with toolbars.
@interface ToolbarWindow : BaseWindow {
  // mFullscreenTitlebarTracker attaches an invisible rectangle to the system
  // title bar. This allows us to detect when the title bar is showing in
  // fullscreen.
  FullscreenTitlebarTracker* mFullscreenTitlebarTracker;

  CGFloat mMenuBarHeight;
  NSRect mWindowButtonsRect;
}
- (void)setDrawsContentsIntoWindowFrame:(BOOL)aState;
- (void)placeWindowButtons:(NSRect)aRect;
- (NSRect)windowButtonsRect;
- (void)windowMainStateChanged;
@end

class nsCocoaWindow final : public nsIWidget {
 private:
  friend class nsChildView;
  typedef nsIWidget Inherited;

 public:
  nsCocoaWindow();

  // Check if this window should use NSPopover for popup/menu display
  bool ShouldUseNSPopover() const;
  bool ShouldShowAsNSPopover() const;

  [[nodiscard]] nsresult Create(nsIWidget* aParent, const DesktopIntRect& aRect,
                                const InitData&) override;

  [[nodiscard]] nsresult Create(nsIWidget* aParent,
                                const LayoutDeviceIntRect& aRect,
                                const InitData&) override;

  void Destroy() override;

  void Show(bool aState) override;
  bool NeedsRecreateToReshow() override;

  void Enable(bool aState) override;
  bool IsEnabled() const override;
  void SetModal(bool aState) override;
  bool IsRunningAppModal() override;
  bool IsVisible() const override;
  void SetFocus(Raise, mozilla::dom::CallerType aCallerType) override;
  LayoutDeviceIntPoint WidgetToScreenOffset() override;
  LayoutDeviceIntPoint GetClientOffset() override;
  LayoutDeviceIntMargin NormalSizeModeClientToWindowMargin() override;
  bool ShowsResizeIndicator(LayoutDeviceIntRect* aResizerRect) override {
    return false;
  }

  void* GetNativeData(uint32_t aDataType) override;

  void ConstrainPosition(DesktopIntPoint&) override;
  void SetSizeConstraints(const SizeConstraints& aConstraints) override;
  void Move(const DesktopPoint&) override;
  nsSizeMode SizeMode() override { return mSizeMode; }
  void SetSizeMode(nsSizeMode aMode) override;
  void GetWorkspaceID(nsAString& workspaceID) override;
  void MoveToWorkspace(const nsAString& workspaceID) override;
  void SuppressAnimation(bool aSuppress) override;
  void HideWindowChrome(bool aShouldHide) override;
  NSView<mozView>* GetEditorView();

  nsresult SynthesizeNativeKeyEvent(
      int32_t aNativeKeyboardLayout, int32_t aNativeKeyCode,
      uint32_t aModifierFlags, const nsAString& aCharacters,
      const nsAString& aUnmodifiedCharacters,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeMouseEvent(
      LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
      mozilla::MouseButton aButton, nsIWidget::Modifiers aModifierFlags,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeMouseMove(
      LayoutDeviceIntPoint aPoint,
      nsISynthesizedEventCallback* aCallback) override;
  nsresult SynthesizeNativeMouseScrollEvent(
      LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage, double aDeltaX,
      double aDeltaY, double aDeltaZ, uint32_t aModifierFlags,
      uint32_t aAdditionalFlags,
      nsISynthesizedEventCallback* aCallback) override;
  nsresult SynthesizeNativeTouchPoint(
      uint32_t aPointerId, TouchPointerState aPointerState,
      LayoutDeviceIntPoint aPoint, double aPointerPressure,
      uint32_t aPointerOrientation,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeTouchpadDoubleTap(LayoutDeviceIntPoint aPoint,
                                             uint32_t aModifierFlags) override;

  static bool DoHasPendingInputEvent();
  static uint32_t GetCurrentInputEventCount();
  static void UpdateCurrentInputEventCount();

  // unit conversion convenience functions
  int32_t CocoaPointsToDevPixels(CGFloat aPts) const {
    return nsCocoaUtils::CocoaPointsToDevPixels(aPts, BackingScaleFactor());
  }
  LayoutDeviceIntPoint CocoaPointsToDevPixels(const NSPoint& aPt) const {
    return nsCocoaUtils::CocoaPointsToDevPixels(aPt, BackingScaleFactor());
  }
  LayoutDeviceIntPoint CocoaPointsToDevPixelsRoundDown(
      const NSPoint& aPt) const {
    return nsCocoaUtils::CocoaPointsToDevPixelsRoundDown(aPt,
                                                         BackingScaleFactor());
  }
  LayoutDeviceIntRect CocoaPointsToDevPixels(const NSRect& aRect) const {
    return nsCocoaUtils::CocoaPointsToDevPixels(aRect, BackingScaleFactor());
  }
  CGFloat DevPixelsToCocoaPoints(int32_t aPixels) const {
    return nsCocoaUtils::DevPixelsToCocoaPoints(aPixels, BackingScaleFactor());
  }
  NSRect DevPixelsToCocoaPoints(const LayoutDeviceIntRect& aRect) const {
    return nsCocoaUtils::DevPixelsToCocoaPoints(aRect, BackingScaleFactor());
  }

  // Called by nsCocoaWindow when the window's fullscreen state changes.
  void UpdateFullscreen(bool aFullscreen);

  void DispatchAPZWheelInputEvent(mozilla::InputData& aEvent);
  nsEventStatus DispatchAPZInputEvent(mozilla::InputData& aEvent);

  void DispatchDoubleTapGesture(mozilla::TimeStamp aEventTimeStamp,
                                LayoutDeviceIntPoint aScreenPosition,
                                mozilla::Modifiers aModifiers);

  // Called when the main thread enters a phase during which visual changes
  // are imminent and any layer updates on the compositor thread would interfere
  // with visual atomicity.
  // "Async" CATransactions are CATransactions which happen on a thread that's
  // not the main thread.
  void SuspendAsyncCATransactions();

  // Called when we know that the current main thread paint will be completed
  // once the main thread goes back to the event loop.
  void MaybeScheduleUnsuspendAsyncCATransactions();

  // Called from the runnable dispatched by
  // MaybeScheduleUnsuspendAsyncCATransactions(). At this point we know that the
  // main thread is done handling the visual change (such as a window resize)
  // and we can start modifying CALayers from the compositor thread again.
  void UnsuspendAsyncCATransactions();

#ifdef DEBUG
  // test only.
  nsresult SetHiDPIMode(bool aHiDPI) override;
  nsresult RestoreHiDPIMode() override;
#endif

  void UpdateVibrancy(const nsTArray<ThemeGeometry>& aThemeGeometries);
  mozilla::VibrancyManager& EnsureVibrancyManager();

  void TearDownView();

  bool PrepareForFullscreenTransition(nsISupports** aData) override;
  void PerformFullscreenTransition(FullscreenTransitionStage aStage,
                                   uint16_t aDuration, nsISupports* aData,
                                   nsIRunnable* aCallback) override;
  void CleanupFullscreenTransition() override;
  nsresult MakeFullScreen(bool aFullScreen) final;
  nsresult MakeFullScreenWithNativeTransition(bool aFullScreen) final;
  NSAnimation* FullscreenTransitionAnimation() const {
    return mFullscreenTransitionAnimation;
  }
  void ReleaseFullscreenTransitionAnimation() {
    MOZ_ASSERT(mFullscreenTransitionAnimation,
               "Should only be called when there is animation");
    [mFullscreenTransitionAnimation release];
    mFullscreenTransitionAnimation = nil;
  }

  void Resize(const DesktopSize&, bool aRepaint) override;
  void Resize(const DesktopRect&, bool aRepaint) override;
  NSRect GetClientCocoaRect();
  LayoutDeviceIntRect GetClientBounds() override;
  LayoutDeviceIntRect GetScreenBounds() override;
  LayoutDeviceIntRect GetBounds() override { return mBounds; }
  [[nodiscard]] nsresult GetRestoredBounds(LayoutDeviceIntRect& aRect) override;
  void ReportMoveEvent();
  void ReportSizeEvent();
  bool WidgetTypeSupportsAcceleration() override { return true; }
  bool ShouldUseOffMainThreadCompositing() override;
  void SetCursor(const Cursor&) override;

  void UpdateBoundsFromView();
  void EnsureContentLayerForMainThreadPainting();

  CGFloat BackingScaleFactor() const;
  void BackingScaleFactorChanged();
  double GetDefaultScaleInternal() override;
  int32_t RoundsWidgetCoordinatesTo() override;

  // Mac specific methods
  void PaintWindow();
  void PaintWindowInDrawTarget(mozilla::gfx::DrawTarget* aDT,
                               const LayoutDeviceIntRegion& aRegion,
                               const mozilla::gfx::IntSize& aSurfaceSize);

  void PaintWindowInContentLayer();
  void HandleMainThreadCATransaction();

#ifdef ACCESSIBILITY
  already_AddRefed<mozilla::a11y::LocalAccessible> GetWindowAccessible();
#endif

  bool WidgetPaintsBackground() override { return true; }

  void CreateCompositor(int aWidth, int aHeight) override;
  void DestroyCompositor() override;
  void NotifyCompositorSessionLost(
      mozilla::layers::CompositorSession* aSession) override;
  void SetCompositorWidgetDelegate(
      mozilla::widget::CompositorWidgetDelegate*) override;

  void GetCompositorWidgetInitData(
      mozilla::widget::CompositorWidgetInitData* aInitData) override;
  mozilla::layers::CompositorBridgeChild* GetCompositorBridgeChild() const;

  bool PreRender(mozilla::widget::WidgetRenderingContext* aContext) override;
  void PostRender(mozilla::widget::WidgetRenderingContext* aContext) override;
  mozilla::layers::NativeLayerRoot* GetNativeLayerRoot() override;

  void UpdateWindowDraggingRegion(
      const LayoutDeviceIntRegion& aRegion) override;
  LayoutDeviceIntRegion GetNonDraggableRegion() {
    return mNonDraggableRegion.Region();
  }

  void LookUpDictionary(const nsAString& aText,
                        const nsTArray<mozilla::FontRange>& aFontRangeArray,
                        const bool aIsVertical,
                        const LayoutDeviceIntPoint& aPoint) override;

  mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScale() const final {
    return mozilla::DesktopToLayoutDeviceScale(BackingScaleFactor());
  }

  nsresult SetTitle(const nsAString& aTitle) override;

  void Invalidate(const LayoutDeviceIntRect& aRect) override;
  nsEventStatus DispatchEvent(mozilla::WidgetGUIEvent* aEvent) override;
  void CaptureRollupEvents(bool aDoCapture) override;
  [[nodiscard]] nsresult GetAttention(int32_t aCycleCount) override;
  bool HasPendingInputEvent() override;
  bool SendEventToNativeMenuSystem(NSEvent* aEvent);
  void PostHandleKeyEvent(mozilla::WidgetKeyboardEvent* aEvent) override;
  nsresult ActivateNativeMenuItemAt(const nsAString& indexString) override;
  nsresult ForceUpdateNativeMenuAt(const nsAString& indexString) override;
  [[nodiscard]] nsresult GetSelectionAsPlaintext(nsAString& aResult) override;
  TextEventDispatcherListener* GetNativeTextEventDispatcherListener() override;
  [[nodiscard]] nsresult AttachNativeKeyEvent(
      mozilla::WidgetKeyboardEvent& aEvent) override;

  TransparencyMode GetTransparencyMode() override;
  void SetTransparencyMode(TransparencyMode aMode) override;
  void SetWindowShadowStyle(mozilla::WindowShadow aStyle) override;
  void SetWindowOpacity(float aOpacity) override;
  void SetWindowTransform(const mozilla::gfx::Matrix& aTransform) override;
  void SetInputRegion(const InputRegion&) override;
  void SetColorScheme(const mozilla::Maybe<mozilla::ColorScheme>&) override;
  void SetShowsToolbarButton(bool aShow) override;
  bool GetSupportsNativeFullscreen();
  void SetSupportsNativeFullscreen(bool aShow) override;
  void SetWindowAnimationType(WindowAnimationType aType) override;
  void SetHideTitlebarSeparator(bool) override;
  bool IsMacTitlebarDirectionRTL() override;
  void SetCustomTitlebar(bool) override;
  void UpdateThemeGeometries(
      const nsTArray<ThemeGeometry>& aThemeGeometries) override;
  void LockAspectRatio(bool aShouldLock) override;

  void DispatchSizeModeEvent();
  void DispatchOcclusionEvent();

  // be notified that a some form of drag event needs to go into Gecko
  bool DragEvent(unsigned int aMessage, mozilla::gfx::Point aMouseGlobal,
                 UInt16 aKeyModifiers);

  bool HasModalDescendants() const { return mNumModalDescendants > 0; }
  bool IsModal() const { return mModal; }

  NSWindow* GetCocoaWindow() { return [[mWindow retain] autorelease]; }

  void SetMenuBar(RefPtr<nsMenuBarX>&& aMenuBar);
  nsMenuBarX* GetMenuBar();

  void SetInputContext(const InputContext& aContext,
                       const InputContextAction& aAction) override;
  InputContext GetInputContext() override;
  MOZ_CAN_RUN_SCRIPT bool GetEditCommands(
      mozilla::NativeKeyBindingsType aType,
      const mozilla::WidgetKeyboardEvent& aEvent,
      nsTArray<mozilla::CommandInt>& aCommands) override;

  void SetPopupWindowLevel();

  bool InFullScreenMode() const { return mInFullScreenMode; }

  // Class method versions of NSWindow/Delegate callbacks which need to
  // access object state.
  void CocoaWindowWillEnterFullscreen(bool aFullscreen);
  void CocoaWindowDidEnterFullscreen(bool aFullscreen);
  void CocoaWindowDidResize();
  void CocoaSendToplevelActivateEvents();
  void CocoaSendToplevelDeactivateEvents();

  enum class TransitionType {
    Windowed,
    Fullscreen,
    EmulatedFullscreen,
    Miniaturize,
    Deminiaturize,
    Zoom,
  };
  void FinishCurrentTransitionIfMatching(const TransitionType& aTransition);
  bool IsInTransition() { return mTransitionCurrent.isSome(); }

  // Called when something has happened that might cause us to update our
  // fullscreen state. Returns true if we updated state. We'll call this
  // on window resize, and we'll call it when we enter or exit fullscreen,
  // since fullscreen to-and-from zoomed windows won't necessarily trigger
  // a resize.
  bool HandleUpdateFullscreenOnResize();

  void LockNativePointer(NativePointerLockMode aNativePointerLockMode) override;
  void UnlockNativePointer() override;
  void SetNativePointerLockMode(
      NativePointerLockMode aNativePointerLockMode) override;
  bool SupportsUnadjustedMovement() override;

  static const mozilla::Maybe<NativePointerLockMode>&
  GetNativePointerLockedMode();
  static LayoutDeviceIntPoint GetNativeLockedPoint();

 protected:
  virtual ~nsCocoaWindow();

  nsresult CreateNativeWindow(const NSRect& aRect, BorderStyle aBorderStyle,
                              bool aRectIsFrameRect, bool aIsPrivateBrowsing);
  void DestroyNativeWindow();
  void UpdateBounds();
  int32_t GetWorkspaceID();
  void MoveVisibleWindowToWorkspace(int32_t workspaceID);
  CGFloat ComputeBackingScaleFactor() const;

  void DoResize(double aX, double aY, double aWidth, double aHeight,
                bool aRepaint, bool aConstrainToCurrentScreen);

  void UpdateFullscreenState(bool aFullScreen, bool aNativeMode);
  nsresult DoMakeFullScreen(bool aFullScreen, bool aUseSystemTransition);

  BaseWindow* mWindow;  // our cocoa window [STRONG]
  // window upon closing it, held through our destructor. This is useful
  // to ensure that macOS run loops which reference the window will still
  // have something to point to even if they don't use proper retain and
  // release patterns.
  WindowDelegate* mDelegate =
      nullptr;  // our delegate for processing window msgs [STRONG]
  RefPtr<nsMenuBarX> mMenuBar;
  ChildView* mChildView = nullptr;  // Cocoa content view, [STRONG]
#ifdef ACCESSIBILITY
  // weak ref to this childview's associated mozAccessible for speed reasons
  // (we get queried for it *a lot* but don't want to own it)
  nsWeakPtr mAccessible;
#endif
  // Held while the compositor (or WR renderer) thread is compositing.
  // Protects from tearing down the view during compositing and from presenting
  // half-composited layers to the screen.
  mozilla::Mutex mCompositingLock MOZ_UNANNOTATED{"ChildViewCompositing"};

  mozilla::ViewRegion mNonDraggableRegion;

  // Cached value of [mChildView backingScaleFactor], to avoid sending two obj-c
  // messages (respondsToSelector, backingScaleFactor) every time we need to
  // use it.
  // ** We'll need to reinitialize this if the backing resolution changes. **
  mutable CGFloat mBackingScaleFactor;

  RefPtr<mozilla::layers::NativeLayerRootCA> mNativeLayerRoot;
  RefPtr<mozilla::layers::NativeLayerRootRemoteMacParent>
      mNativeLayerRootRemoteMacParent;

  // In BasicLayers mode, this is the CoreAnimation layer that contains the
  // rendering from Gecko. It is a sublayer of mNativeLayerRoot's underlying
  // wrapper layer.
  // Lazily created by EnsureContentLayerForMainThreadPainting().
  RefPtr<mozilla::layers::NativeLayerCA> mContentLayer;
  RefPtr<mozilla::layers::SurfacePoolHandle> mPoolHandle;

  // In BasicLayers mode, this is the invalid region of mContentLayer.
  LayoutDeviceIntRegion mContentLayerInvalidRegion;

  mozilla::UniquePtr<mozilla::VibrancyManager> mVibrancyManager;

  RefPtr<mozilla::CancelableRunnable> mUnsuspendAsyncCATransactionsRunnable;

  // if this is a toplevel window, and there is any ongoing fullscreen
  // transition, it is the animation object.
  NSAnimation* mFullscreenTransitionAnimation;
  mozilla::WindowShadow mShadowStyle;
  CGFloat mAspectRatio;

  WindowAnimationType mAnimationType;

  bool mWindowMadeHere;  // true if we created the window, false for embedding
  nsSizeMode mSizeMode;
  bool mInFullScreenMode;
  // Whether we are currently using native fullscreen. It could be false because
  // we are in the emulated fullscreen where we do not use the native
  // fullscreen.
  bool mInNativeFullScreenMode;

  mozilla::Maybe<TransitionType> mTransitionCurrent;
  std::queue<TransitionType> mTransitionsPending;

  static uint32_t sLastInputEventCount;

  // This is used by SynthesizeNativeTouchPoint to maintain state between
  // multiple synthesized points
  mozilla::UniquePtr<mozilla::MultiTouchInput> mSynthesizedTouchInput;

  // A runnable we might assign to run ProcessTransitions at a later event loop.
  // Cancelable so we can cancel it in CancelAllTransitions(), if needed.
  RefPtr<mozilla::CancelableRunnable> mProcessTransitionsPending;

  // Sometimes we add a transition that wasn't requested by a caller. We do this
  // to manage transitions between states that otherwise would be rejected by
  // Cocoa. When we do this, it's useful to know when we are handling an added
  // transition because we don't want to send size mode events when they
  // execute.
  bool mIsTransitionCurrentAdded = false;

  // Whether we are treating the next resize as the start of a fullscreen
  // transition. If we are, which direction are we going: Fullscreen or
  // Windowed.
  mozilla::Maybe<TransitionType> mUpdateFullscreenOnResize;

  void QueueTransition(const TransitionType& aTransition);
  void ProcessTransitions();

  // Call this to stop all transition processing, which is useful during
  // window closing and shutdown.
  void CancelAllTransitions();

  bool mInProcessTransitions = false;

  // While running an emulated fullscreen transition, we want to suppress
  // sending size mode events due to window resizing. We fix it up at the end
  // when the transition is complete.
  bool mSuppressSizeModeEvents = false;

  // Ignore occlusion events caused by displaying the temporary fullscreen
  // window during the fullscreen transition animation because only focused
  // contexts are permitted to enter DOM fullscreen.
  int mIgnoreOcclusionCount;

  // Set to true when a native fullscreen transition is initiated -- either to
  // or from fullscreen -- and set to false when it is complete. During this
  // period, we presume the window is visible, which prevents us from sending
  // unnecessary OcclusionStateChanged events.
  bool mHasStartedNativeFullscreen;

  bool mModal = false;
  bool mIsAnimationSuppressed = false;

  bool mInReportMoveEvent = false;  // true if in a call to ReportMoveEvent().
  bool mInResize = false;           // true if in a call to DoResize().
  bool mWindowTransformIsIdentity = true;
  bool mAlwaysOnTop = false;
  bool mAspectRatioLocked = false;
  bool mIsAlert = false;  // True if this is an non-native alert window.
  bool mWasShown = false;

  int32_t mNumModalDescendants = 0;

  // The workspaceID to move to once the window becomes visible. A value of 0
  // is a no-op.
  int32_t mDeferredWorkspaceID = 0;

  RefPtr<mozilla::widget::TextInputHandler> mTextInputHandler;
  InputContext mInputContext;
  NSWindowAnimationBehavior mWindowAnimationBehavior;

  LayoutDeviceIntRect mBounds;

  // The window bounds saved just before the window last left nsSizeMode_Normal
  // (for fullscreen, maximized, or minimized). Used by GetRestoredBounds() so
  // that we can persist the pre-transition position/size while the window is
  // not in normal mode. Nothing() until the first transition out of normal
  // mode occurs.
  mozilla::Maybe<LayoutDeviceIntRect> mRestoredBounds;

  mozilla::widget::PlatformCompositorWidgetDelegate* mCompositorWidgetDelegate =
      nullptr;

 private:
  // This is class state for tracking which nsCocoaWindow, if any, is in the
  // middle of a native fullscreen transition.
  static nsCocoaWindow* sWindowInNativeTransition;

  // This function returns true if the caller has been able to claim the sole
  // permission to start a native transition. It must be followed by a call
  // to EndOurNativeTransition() when the native transition is complete.
  bool CanStartNativeTransition();
  void EndOurNativeTransition();

  // This is class state for tracking native pointer lock state.
  static mozilla::Maybe<NativePointerLockMode> sNativePointerLockMode;
  static LayoutDeviceIntPoint sNativeLockedPoint;
};

#endif  // nsCocoaWindow_h_
