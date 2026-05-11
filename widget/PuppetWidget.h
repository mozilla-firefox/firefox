/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This "puppet widget" isn't really a platform widget.  It's intended
 * to be used in widgetless rendering contexts, such as sandboxed
 * content processes.  If any "real" widgetry is needed, the request
 * is forwarded to and/or data received from elsewhere.
 */

#ifndef mozilla_widget_PuppetWidget_h_
#define mozilla_widget_PuppetWidget_h_

#include "mozilla/gfx/2D.h"
#include "mozilla/RefPtr.h"
#include "nsIWidget.h"
#include "nsCOMArray.h"
#include "nsThreadUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/ContentCache.h"
#include "mozilla/EventForwards.h"
#include "mozilla/TextEventDispatcherListener.h"
#include "mozilla/layers/MemoryPressureObserver.h"

class nsRefreshDriver;

namespace mozilla {
enum class NativeKeyBindingsType : uint8_t;

namespace dom {
class BrowserChild;
}  // namespace dom

namespace layers {
class WebRenderLayerManager;
}  // namespace layers

namespace widget {

struct AutoCacheNativeKeyCommands;

class PuppetWidget final : public nsIWidget,
                           public TextEventDispatcherListener,
                           public layers::MemoryPressureListener {
  typedef mozilla::CSSRect CSSRect;
  typedef mozilla::dom::BrowserChild BrowserChild;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::layers::WebRenderLayerManager WebRenderLayerManager;

  // Avoiding to make compiler confused between mozilla::widget and nsIWidget.
  typedef mozilla::widget::TextEventDispatcher TextEventDispatcher;
  typedef mozilla::widget::TextEventDispatcherListener
      TextEventDispatcherListener;

  typedef nsIWidget Base;

  // The width and height of the "widget" are clamped to this.
 public:
  explicit PuppetWidget(BrowserChild* aBrowserChild);

 protected:
  virtual ~PuppetWidget();

 public:
  NS_DECL_ISUPPORTS_INHERITED

  // PuppetWidget creation is infallible, hence InfallibleCreate(), which
  // Create() calls.
  using nsIWidget::Create;  // for Create signature not overridden here
  nsresult Create(nsIWidget* aParent, const LayoutDeviceIntRect&,
                  const widget::InitData&) override;
  void InfallibleCreate(nsIWidget* aParent, const LayoutDeviceIntRect&,
                        const widget::InitData&);

  void InitIMEState();

  void InitSupportsUnadjustedMovement(bool aSupportsUnadjustedMovement) {
    mSupportsUnadjustedMovement = aSupportsUnadjustedMovement;
  }

  void Destroy() override;

  void Show(bool aState) override;

  bool IsVisible() const override { return mVisible; }

  // Widget position is controlled by the parent process via BrowserChild.
  void Move(const DesktopPoint&) override {}
  void Resize(const DesktopSize&, bool aRepaint) override;
  void Resize(const DesktopRect& aRect, bool aRepaint) override {
    auto targetRect = gfx::RoundedToInt(aRect * GetDesktopToDeviceScale());
    if (mBounds.TopLeft() != targetRect.TopLeft()) {
      NotifyWindowMoved(targetRect.TopLeft());
    }
    mBounds.MoveTo(targetRect.TopLeft());
    return Resize(aRect.Size(), aRepaint);
  }

  // XXX/cjones: copying gtk behavior here; unclear what disabling a
  // widget is supposed to entail
  void Enable(bool aState) override { mEnabled = aState; }
  bool IsEnabled() const override { return mEnabled; }

  nsSizeMode SizeMode() override { return mSizeMode; }
  void SetSizeMode(nsSizeMode aMode) override { mSizeMode = aMode; }

  void SetFocus(Raise, mozilla::dom::CallerType aCallerType) override;

  void Invalidate(const LayoutDeviceIntRect& aRect) override;

  // PuppetWidgets don't have native data, as they're purely nonnative.
  void* GetNativeData(uint32_t aDataType) override { return nullptr; }

  // PuppetWidgets don't have any concept of titles.
  nsresult SetTitle(const nsAString& aTitle) override {
    return NS_ERROR_UNEXPECTED;
  }

  mozilla::LayoutDeviceToLayoutDeviceMatrix4x4 WidgetToTopLevelWidgetTransform()
      override;

  LayoutDeviceIntPoint WidgetToScreenOffset() override;

  LayoutDeviceIntPoint TopLevelWidgetToScreenOffset() override {
    return GetWindowPosition();
  }

  int32_t RoundsWidgetCoordinatesTo() override { return mRounding; }

  void InitEvent(WidgetGUIEvent& aEvent,
                 LayoutDeviceIntPoint* aPoint = nullptr);

  nsEventStatus DispatchEvent(WidgetGUIEvent* aEvent) override;
  ContentAndAPZEventStatus DispatchInputEvent(
      WidgetInputEvent* aEvent) override;
  void SetConfirmedTargetAPZC(
      uint64_t aInputBlockId,
      const nsTArray<ScrollableLayerGuid>& aTargets) const override;
  void UpdateZoomConstraints(
      const uint32_t& aPresShellId, const ScrollableLayerGuid::ViewID& aViewId,
      const mozilla::Maybe<ZoomConstraints>& aConstraints) override;
  bool AsyncPanZoomEnabled() const override;

  MOZ_CAN_RUN_SCRIPT bool GetEditCommands(
      NativeKeyBindingsType aType, const mozilla::WidgetKeyboardEvent& aEvent,
      nsTArray<mozilla::CommandInt>& aCommands) override;

  friend struct AutoCacheNativeKeyCommands;

  //
  // nsIWidget methods we override
  //

  // Documents loaded in child processes are always subdocuments of
  // other docs in an ancestor process.  To ensure that the
  // backgrounds of those documents are painted like those of
  // same-process subdocuments, we force the widget here to be
  // transparent, which in turn will cause layout to use a transparent
  // backstop background color.
  TransparencyMode GetTransparencyMode() override {
    return TransparencyMode::Transparent;
  }

  WindowRenderer* GetWindowRenderer() override;

  // This is used for creating remote layer managers and for re-creating
  // them after a compositor reset. The lambda aInitializeFunc is used to
  // perform any caller-required initialization for the newly created layer
  // manager; in the event of a failure, return false and it will destroy the
  // new layer manager without changing the state of the widget.
  bool CreateRemoteLayerManager(
      const std::function<bool(WebRenderLayerManager*)>& aInitializeFunc);

  void SetInputContext(const InputContext& aContext,
                       const InputContextAction& aAction) override;
  InputContext GetInputContext() override;
  NativeIMEContext GetNativeIMEContext() override;
  TextEventDispatcherListener* GetNativeTextEventDispatcherListener() override {
    return mNativeTextEventDispatcherListener
               ? mNativeTextEventDispatcherListener.get()
               : this;
  }
  void SetNativeTextEventDispatcherListener(
      TextEventDispatcherListener* aListener) {
    mNativeTextEventDispatcherListener = aListener;
  }

  void SetCursor(const Cursor&) override;

  float GetDPI() override { return mDPI; }
  double GetDefaultScaleInternal() override { return mDefaultScale; }

  bool NeedsPaint() override;

  // Paint the widget immediately if any paints are queued up.
  void PaintNowIfNeeded();

  BrowserChild* GetOwningBrowserChild() override { return mBrowserChild; }
  LayersId GetLayersId() const override;

  void UpdateBackingScaleCache(float aDpi, int32_t aRounding, double aScale,
                               double aDesktopToDeviceScale) {
    mDPI = aDpi;
    mRounding = aRounding;
    mDefaultScale = aScale;
    mDesktopToDeviceScale = aDesktopToDeviceScale;
  }

  mozilla::DesktopToLayoutDeviceScale GetDesktopToDeviceScale() const override {
    return mozilla::DesktopToLayoutDeviceScale(mDesktopToDeviceScale);
  }

  // safe area insets support
  LayoutDeviceIntMargin GetSafeAreaInsets() const override;
  void UpdateSafeAreaInsets(const LayoutDeviceIntMargin& aSafeAreaInsets);

  // Get the offset to the chrome of the window that this tab belongs to.
  //
  // NOTE: In OOP iframes this value is zero. You should use
  // WidgetToTopLevelWidgetTransform instead which is already including the
  // chrome offset.
  LayoutDeviceIntPoint GetChromeOffset();

  // Get the screen position of the application window.
  LayoutDeviceIntPoint GetWindowPosition();

  LayoutDeviceIntRect GetBounds() override;
  LayoutDeviceIntRect GetScreenBounds() override;

  nsresult SynthesizeNativeKeyEvent(
      int32_t aNativeKeyboardLayout, int32_t aNativeKeyCode,
      uint32_t aModifierFlags, const nsAString& aCharacters,
      const nsAString& aUnmodifiedCharacters,
      nsISynthesizedEventCallback* aCallback) override;
  nsresult SynthesizeNativeMouseEvent(
      LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
      MouseButton aButton, nsIWidget::Modifiers aModifierFlags,
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
  nsresult SynthesizeNativeTouchPadPinch(TouchpadGesturePhase aEventPhase,
                                         float aScale,
                                         LayoutDeviceIntPoint aPoint,
                                         int32_t aModifierFlags) override;
  nsresult SynthesizeNativeTouchTap(
      LayoutDeviceIntPoint aPoint, bool aLongTap,
      nsISynthesizedEventCallback* aCallback) override;
  uint32_t GetMaxTouchPoints() const override;
  nsresult SynthesizeNativePenInput(
      uint32_t aPointerId, TouchPointerState aPointerState,
      LayoutDeviceIntPoint aPoint, double aPressure, uint32_t aRotation,
      int32_t aTiltX, int32_t aTiltY, int32_t aButton,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeTouchpadDoubleTap(LayoutDeviceIntPoint aPoint,
                                             uint32_t aModifierFlags) override;

  nsresult SynthesizeNativeTouchpadPan(
      TouchpadGesturePhase aEventPhase, LayoutDeviceIntPoint aPoint,
      double aDeltaX, double aDeltaY, int32_t aModifierFlags,
      nsISynthesizedEventCallback* aCallback) override;

  void LockNativePointer(NativePointerLockMode aNativePointerLockMode) override;
  void UnlockNativePointer() override;
  void SetNativePointerLockMode(
      NativePointerLockMode aNativePointerLockMode) override;
  bool SupportsUnadjustedMovement() override {
    return mSupportsUnadjustedMovement;
  }

  void StartAsyncScrollbarDrag(const AsyncDragMetrics& aDragMetrics) override;

  void ZoomToRect(const uint32_t& aPresShellId,
                  const ScrollableLayerGuid::ViewID& aViewId,
                  const CSSRect& aRect, const uint32_t& aFlags) override;

  bool HasPendingInputEvent() override;

  void LookUpDictionary(const nsAString& aText,
                        const nsTArray<mozilla::FontRange>& aFontRangeArray,
                        const bool aIsVertical,
                        const LayoutDeviceIntPoint& aPoint) override;

  nsresult SetSystemFont(const nsCString& aFontName) override;
  nsresult GetSystemFont(nsCString& aFontName) override;

  // TextEventDispatcherListener
  using nsIWidget::NotifyIME;
  NS_IMETHOD NotifyIME(TextEventDispatcher* aTextEventDispatcher,
                       const IMENotification& aNotification) override;
  NS_IMETHOD_(IMENotificationRequests) GetIMENotificationRequests() override;
  NS_IMETHOD_(void)
  OnRemovedFrom(TextEventDispatcher* aTextEventDispatcher) override;
  NS_IMETHOD_(void)
  WillDispatchKeyboardEvent(TextEventDispatcher* aTextEventDispatcher,
                            WidgetKeyboardEvent& aKeyboardEvent,
                            uint32_t aIndexOfKeypress, void* aData) override;

  void OnMemoryPressure(layers::MemoryPressureReason aWhy) override;

  void PerformHapticFeedback(mozilla::HapticFeedbackType aType) override;

 private:
  void Paint();

  nsresult RequestIMEToCommitComposition(bool aCancel);
  nsresult NotifyIMEOfFocusChange(const IMENotification& aIMENotification);
  nsresult NotifyIMEOfSelectionChange(const IMENotification& aIMENotification);
  nsresult NotifyIMEOfCompositionUpdate(
      const IMENotification& aIMENotification);
  nsresult NotifyIMEOfTextChange(const IMENotification& aIMENotification);
  nsresult NotifyIMEOfMouseButtonEvent(const IMENotification& aIMENotification);
  nsresult NotifyIMEOfPositionChange(const IMENotification& aIMENotification);

  bool CacheEditorRect();
  bool CacheCompositionRects(uint32_t& aStartOffset,
                             nsTArray<LayoutDeviceIntRect>& aRectArray,
                             uint32_t& aTargetCauseOffset);
  bool GetCaretRect(LayoutDeviceIntRect& aCaretRect, uint32_t aCaretOffset);
  uint32_t GetCaretOffset();

  // When this widget caches input context and currently managed by
  // IMEStateManager, the cache is valid.
  bool HaveValidInputContextCache() const;

  class WidgetPaintTask : public Runnable {
   public:
    NS_DECL_NSIRUNNABLE
    explicit WidgetPaintTask(PuppetWidget* widget)
        : Runnable("PuppetWidget::WidgetPaintTask"), mWidget(widget) {}
    void Revoke() { mWidget = nullptr; }

   private:
    PuppetWidget* mWidget;
  };

  nsRefreshDriver* GetTopLevelRefreshDriver() const;

  // BrowserChild normally holds a strong reference to this PuppetWidget
  // or its root ancestor, but each PuppetWidget also needs a
  // reference back to BrowserChild (e.g. to delegate nsIWidget IME calls
  // to chrome) So we hold a weak reference to BrowserChild here.  Since
  // it's possible for BrowserChild to outlive the PuppetWidget, we clear
  // this weak reference in Destroy()
  BrowserChild* mBrowserChild;
  nsRevocableEventPtr<WidgetPaintTask> mWidgetPaintTask;
  RefPtr<layers::MemoryPressureObserver> mMemoryPressureObserver;
  // IME
  IMENotificationRequests mIMENotificationRequestsOfParent;
  InputContext mInputContext;
  // mNativeIMEContext is initialized when this dispatches every composition
  // event both from parent process's widget and TextEventDispatcher in same
  // process.  If it hasn't been started composition yet, this isn't necessary
  // for XP code since there is no TextComposition instance which is caused by
  // the PuppetWidget instance.
  NativeIMEContext mNativeIMEContext;
  ContentCacheInChild mContentCache;

  // The DPI of the parent widget containing this widget.
  float mDPI = GetFallbackDPI();
  int32_t mRounding = 1;
  double mDefaultScale = GetFallbackDefaultScale().scale;
  double mDesktopToDeviceScale = 1.0;

  LayoutDeviceIntMargin mSafeAreaInsets;
  RefPtr<TextEventDispatcherListener> mNativeTextEventDispatcherListener;

 protected:
  bool mEnabled;
  bool mVisible;

 private:
  nsSizeMode mSizeMode;

  LayoutDeviceIntRect mBounds;

  bool mNeedIMEStateInit;
  // When remote process requests to commit/cancel a composition, the
  // composition may have already been committed in the main process.  In such
  // case, this will receive remaining composition events for the old
  // composition even after requesting to commit/cancel the old composition
  // but the TextComposition for the old composition has already been
  // destroyed. So, until this meets new eCompositionStart, following
  // composition events should be ignored if this is set to true.
  bool mIgnoreCompositionEvents;
  bool mSupportsUnadjustedMovement = false;
};

}  // namespace widget
}  // namespace mozilla

#endif  // mozilla_widget_PuppetWidget_h_
