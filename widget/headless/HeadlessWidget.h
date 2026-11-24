/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HEADLESSWIDGET_H
#define HEADLESSWIDGET_H

#include "CompositorWidget.h"
#include "mozilla/dom/WheelEventBinding.h"
#include "mozilla/widget/InProcessCompositorWidget.h"
#include "nsIWidget.h"

// The various synthesized event values are hardcoded to avoid pulling
// in the platform specific widget code.
#if defined(MOZ_WIDGET_GTK)
#  define MOZ_HEADLESS_SCROLL_MULTIPLIER 3
#  define MOZ_HEADLESS_SCROLL_DELTA_MODE \
    mozilla::dom::WheelEvent_Binding::DOM_DELTA_LINE
#elif defined(XP_WIN)
#  define MOZ_HEADLESS_SCROLL_MULTIPLIER \
    .025  // default scroll lines (3) / WHEEL_DELTA (120)
#  define MOZ_HEADLESS_SCROLL_DELTA_MODE \
    mozilla::dom::WheelEvent_Binding::DOM_DELTA_LINE
#elif defined(XP_MACOSX)
#  define MOZ_HEADLESS_SCROLL_MULTIPLIER 1
#  define MOZ_HEADLESS_SCROLL_DELTA_MODE \
    mozilla::dom::WheelEvent_Binding::DOM_DELTA_PIXEL
#elif defined(ANDROID)
#  define MOZ_HEADLESS_SCROLL_MULTIPLIER 1
#  define MOZ_HEADLESS_SCROLL_DELTA_MODE \
    mozilla::dom::WheelEvent_Binding::DOM_DELTA_LINE
#else
#  define MOZ_HEADLESS_SCROLL_MULTIPLIER -1
#  define MOZ_HEADLESS_SCROLL_DELTA_MODE -1
#endif

namespace mozilla {
enum class NativeKeyBindingsType : uint8_t;
namespace widget {

class HeadlessWidget final : public nsIWidget {
 public:
  HeadlessWidget();

  NS_INLINE_DECL_REFCOUNTING_INHERITED(HeadlessWidget, nsIWidget)

  void* GetNativeData(uint32_t aDataType) override {
    // Headless widgets have no native data.
    return nullptr;
  }

  nsresult Create(nsIWidget* aParent, const LayoutDeviceIntRect& aRect,
                  const widget::InitData&) override;
  using nsIWidget::Create;  // for Create signature not overridden here

  void GetCompositorWidgetInitData(
      mozilla::widget::CompositorWidgetInitData* aInitData) override;

  void Destroy() override;
  void Show(bool aState) override;
  bool IsVisible() const override;
  void Move(const DesktopPoint&) override;
  void Resize(const DesktopSize&, bool aRepaint) override;
  void Resize(const DesktopRect&, bool aRepaint) override;
  nsSizeMode SizeMode() override { return mSizeMode; }
  void SetSizeMode(nsSizeMode aMode) override;
  nsresult MakeFullScreen(bool aFullScreen) override;
  void Enable(bool aState) override;
  bool IsEnabled() const override;
  void SetFocus(Raise, mozilla::dom::CallerType aCallerType) override;
  LayoutDeviceIntRect GetBounds() override { return mBounds; }
  void Invalidate(const LayoutDeviceIntRect& aRect) override {
    // TODO: see if we need to do anything here.
  }
  nsresult SetTitle(const nsAString& title) override {
    // Headless widgets have no title, so just ignore it.
    return NS_OK;
  }
  LayoutDeviceIntPoint WidgetToScreenOffset() override;
  void SetInputContext(const InputContext& aContext,
                       const InputContextAction& aAction) override {
    mInputContext = aContext;
  }
  InputContext GetInputContext() override { return mInputContext; }

  WindowRenderer* GetWindowRenderer() override;

  void SetCompositorWidgetDelegate(CompositorWidgetDelegate* delegate) override;

  [[nodiscard]] nsresult AttachNativeKeyEvent(
      WidgetKeyboardEvent& aEvent) override;
  MOZ_CAN_RUN_SCRIPT bool GetEditCommands(
      NativeKeyBindingsType aType, const WidgetKeyboardEvent& aEvent,
      nsTArray<CommandInt>& aCommands) override;

  nsresult SynthesizeNativeMouseEvent(
      LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
      mozilla::MouseButton aButton, nsIWidget::NativeModifiers aModifierFlags,
      nsISynthesizedEventCallback* aCallback) override;
  nsresult SynthesizeNativeMouseMove(
      LayoutDeviceIntPoint aPoint,
      nsISynthesizedEventCallback* aCallback) override {
    return SynthesizeNativeMouseEvent(
        aPoint, NativeMouseMessage::Move, mozilla::MouseButton::eNotPressed,
        nsIWidget::NativeModifiers::NO_MODIFIERS, aCallback);
  };

  nsresult SynthesizeNativeMouseScrollEvent(
      LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage, double aDeltaX,
      double aDeltaY, double aDeltaZ, nsIWidget::NativeModifiers aModifierFlags,
      uint32_t aAdditionalFlags,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeTouchPoint(
      uint32_t aPointerId, TouchPointerState aPointerState,
      LayoutDeviceIntPoint aPoint, double aPointerPressure,
      uint32_t aPointerOrientation,
      nsISynthesizedEventCallback* aCallback) override;

  nsresult SynthesizeNativeTouchpadPinch(TouchpadGesturePhase aEventPhase,
                                         float aScale,
                                         LayoutDeviceIntPoint aPoint,
                                         int32_t aModifierFlags) override;

  nsresult SynthesizeNativeTouchpadPan(
      TouchpadGesturePhase aEventPhase, LayoutDeviceIntPoint aPoint,
      double aDeltaX, double aDeltaY, int32_t aModifierFlags,
      nsISynthesizedEventCallback* aCallback) override;

 private:
  ~HeadlessWidget();
  bool mEnabled;
  bool mVisible;
  bool mDestroyed;
  bool mAlwaysOnTop;
  CompositorWidgetDelegate* mCompositorWidgetDelegate;
  nsSizeMode mSizeMode;
  // The size mode before entering fullscreen mode.
  nsSizeMode mLastSizeMode;
  // The last size mode set while the window was visible.
  nsSizeMode mEffectiveSizeMode;
  mozilla::ScreenCoord mLastPinchSpan;
  InputContext mInputContext;
  mozilla::UniquePtr<mozilla::MultiTouchInput> mSynthesizedTouchInput;
  // In headless there is no window manager to track window bounds
  // across size mode changes, so we must track it to emulate.
  LayoutDeviceIntRect mRestoreBounds;
  LayoutDeviceIntRect mBounds;
  void ApplySizeModeSideEffects();
  // Move while maintaining size mode.
  void MoveInternal(int32_t aX, int32_t aY);
  // Resize while maintaining size mode.
  void ResizeInternal(int32_t aWidth, int32_t aHeight, bool aRepaint);
  // Similarly, we must track the active window ourselves in order
  // to dispatch (de)activation events properly.
  void RaiseWindow();
  // The top level widgets are tracked for window ordering. They are
  // stored in order of activation where the last element is always the
  // currently active widget.
  static StaticAutoPtr<nsTArray<HeadlessWidget*>> sActiveWindows;
  // Get the most recently activated widget or null if there are none.
  static already_AddRefed<HeadlessWidget> GetActiveWindow();
};

}  // namespace widget
}  // namespace mozilla

#endif
