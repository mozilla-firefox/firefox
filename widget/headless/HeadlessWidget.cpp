/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "HeadlessWidget.h"

#include "BasicEvents.h"
#include "ErrorList.h"
#include "HeadlessCompositorWidget.h"
#include "HeadlessKeyBindings.h"
#include "InputData.h"
#include "MouseEvents.h"
#include "UnitTransforms.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Maybe.h"
#include "mozilla/NativeKeyBindingsType.h"
#include "mozilla/Preferences.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TextEvents.h"
#include "mozilla/WritingModes.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/widget/HeadlessWidgetTypes.h"
#include "mozilla/widget/PlatformWidgetTypes.h"
#include "mozilla/widget/Screen.h"
#include "nsIScreen.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::layers;

using mozilla::LogLevel;

#ifdef MOZ_LOGGING

#  include "mozilla/Logging.h"
static mozilla::LazyLogModule sWidgetLog("Widget");
static mozilla::LazyLogModule sWidgetFocusLog("WidgetFocus");
#  define LOG(args) MOZ_LOG(sWidgetLog, mozilla::LogLevel::Debug, args)
#  define LOGFOCUS(args) \
    MOZ_LOG(sWidgetFocusLog, mozilla::LogLevel::Debug, args)

#else

#  define LOG(args)
#  define LOGFOCUS(args)

#endif /* MOZ_LOGGING */

/*static*/
already_AddRefed<nsIWidget> nsIWidget::CreateHeadlessWidget() {
  nsCOMPtr<nsIWidget> widget = new mozilla::widget::HeadlessWidget();
  return widget.forget();
}

namespace mozilla {
namespace widget {

StaticAutoPtr<nsTArray<HeadlessWidget*>> HeadlessWidget::sActiveWindows;

already_AddRefed<HeadlessWidget> HeadlessWidget::GetActiveWindow() {
  if (!sActiveWindows) {
    return nullptr;
  }
  auto length = sActiveWindows->Length();
  if (length == 0) {
    return nullptr;
  }
  RefPtr<HeadlessWidget> widget = sActiveWindows->ElementAt(length - 1);
  return widget.forget();
}

HeadlessWidget::HeadlessWidget()
    : mEnabled(true),
      mVisible(false),
      mDestroyed(false),
      mAlwaysOnTop(false),
      mCompositorWidgetDelegate(nullptr),
      mSizeMode(nsSizeMode_Normal),
      mLastSizeMode(nsSizeMode_Normal),
      mEffectiveSizeMode(nsSizeMode_Normal),
      mRestoreBounds(0, 0, 0, 0) {
  mWidgetType = WidgetType::Headless;
  if (!sActiveWindows) {
    sActiveWindows = new nsTArray<HeadlessWidget*>();
    ClearOnShutdown(&sActiveWindows);
  }
}

HeadlessWidget::~HeadlessWidget() {
  LOG(("HeadlessWidget::~HeadlessWidget() [%p]\n", (void*)this));

  Destroy();
}

void HeadlessWidget::Destroy() {
  if (mDestroyed) {
    return;
  }
  LOG(("HeadlessWidget::Destroy [%p]\n", (void*)this));
  mDestroyed = true;

  if (sActiveWindows) {
    int32_t index = sActiveWindows->IndexOf(this);
    if (index != -1) {
      RefPtr<HeadlessWidget> activeWindow = GetActiveWindow();
      sActiveWindows->RemoveElementAt(index);
      // If this is the currently active widget and there's a previously active
      // widget, activate the previous widget.
      RefPtr<HeadlessWidget> previousActiveWindow = GetActiveWindow();
      if (this == activeWindow && previousActiveWindow &&
          previousActiveWindow->mWidgetListener) {
        previousActiveWindow->mWidgetListener->WindowActivated();
      }
    }
  }

  nsIWidget::OnDestroy();

  nsIWidget::Destroy();
}

nsresult HeadlessWidget::Create(nsIWidget* aParent,
                                const LayoutDeviceIntRect& aRect,
                                const widget::InitData& aInitData) {
  BaseCreate(aParent, aInitData);
  mBounds = aRect;
  mRestoreBounds = aRect;
  mAlwaysOnTop = aInitData.mAlwaysOnTop;
  return NS_OK;
}

void HeadlessWidget::GetCompositorWidgetInitData(
    mozilla::widget::CompositorWidgetInitData* aInitData) {
  *aInitData =
      mozilla::widget::HeadlessCompositorWidgetInitData(GetClientSize());
}

void HeadlessWidget::RaiseWindow() {
  MOZ_ASSERT(
      mWindowType == WindowType::TopLevel || mWindowType == WindowType::Dialog,
      "Raising a non-toplevel window.");

  // Do nothing if this is the currently active window.
  RefPtr<HeadlessWidget> activeWindow = GetActiveWindow();
  if (activeWindow == this) {
    return;
  }

  // Deactivate the last active window.
  if (activeWindow && activeWindow->mWidgetListener) {
    activeWindow->mWidgetListener->WindowDeactivated();
  }

  // Remove this window if it's already tracked.
  int32_t index = sActiveWindows->IndexOf(this);
  if (index != -1) {
    sActiveWindows->RemoveElementAt(index);
  }

  // Activate this window.
  sActiveWindows->AppendElement(this);
  if (mWidgetListener) mWidgetListener->WindowActivated();
}

void HeadlessWidget::Show(bool aState) {
  mVisible = aState;

  LOG(("HeadlessWidget::Show [%p] state %d\n", (void*)this, aState));

  // Top-level window and dialogs are activated/raised when shown.
  // NB: alwaysontop windows are generally used for peripheral indicators.
  //     So we don't focus them by default unless they are a Document
  //     Picture-in-Picture.
  if (aState && (!mAlwaysOnTop || mPiPType == PiPType::DocumentPiP) &&
      (mWindowType == WindowType::TopLevel ||
       mWindowType == WindowType::Dialog)) {
    RaiseWindow();
  }

  ApplySizeModeSideEffects();
}

bool HeadlessWidget::IsVisible() const { return mVisible; }

void HeadlessWidget::SetFocus(Raise aRaise,
                              mozilla::dom::CallerType aCallerType) {
  LOGFOCUS(("  SetFocus %d [%p]\n", aRaise == Raise::Yes, (void*)this));

  // This means we request activation of our toplevel window.
  if (aRaise == Raise::Yes) {
    HeadlessWidget* topLevel = (HeadlessWidget*)GetTopLevelWidget();

    // The toplevel only becomes active if it's currently visible; otherwise, it
    // will be activated anyway when it's shown.
    if (topLevel->IsVisible()) {
      topLevel->RaiseWindow();
    }
  }
}

void HeadlessWidget::Enable(bool aState) { mEnabled = aState; }

bool HeadlessWidget::IsEnabled() const { return mEnabled; }

void HeadlessWidget::Move(const DesktopPoint& aPoint) {
  LOG(("HeadlessWidget::Move [%p] %f %f\n", this, aPoint.x.value,
       aPoint.y.value));

  auto topLeft =
      LayoutDeviceIntPoint::Round(aPoint * GetDesktopToDeviceScale());
  if (mWindowType == WindowType::TopLevel ||
      mWindowType == WindowType::Dialog) {
    SetSizeMode(nsSizeMode_Normal);
  }
  MoveInternal(topLeft.x, topLeft.y);
}

void HeadlessWidget::MoveInternal(int32_t aX, int32_t aY) {
  // Since a popup window's x/y coordinates are in relation to
  // the parent, the parent might have moved so we always move a
  // popup window.
  if (mBounds.IsEqualXY(aX, aY) && mWindowType != WindowType::Popup) {
    return;
  }

  mBounds.MoveTo(aX, aY);
  NotifyWindowMoved(mBounds.TopLeft());
}

LayoutDeviceIntPoint HeadlessWidget::WidgetToScreenOffset() {
  if (mWindowType == WindowType::Popup) {
    return mBounds.TopLeft();
  }
  return GetTopLevelWidget()->GetBounds().TopLeft();
}

WindowRenderer* HeadlessWidget::GetWindowRenderer() {
  return nsIWidget::GetWindowRenderer();
}

void HeadlessWidget::SetCompositorWidgetDelegate(
    CompositorWidgetDelegate* aDelegate) {
  mCompositorWidgetDelegate = aDelegate;
}

void HeadlessWidget::Resize(const DesktopSize& aSize, bool aRepaint) {
  int32_t width = NSToIntRound(aSize.width);
  int32_t height = NSToIntRound(aSize.height);
  ResizeInternal(width, height, aRepaint);
}

void HeadlessWidget::ResizeInternal(int32_t aWidth, int32_t aHeight,
                                    bool aRepaint) {
  ConstrainSize(&aWidth, &aHeight);
  mBounds.SizeTo(LayoutDeviceIntSize(aWidth, aHeight));

  if (mCompositorWidgetDelegate) {
    mCompositorWidgetDelegate->NotifyClientSizeChanged(mBounds.Size());
  }
  if (mWidgetListener) {
    mWidgetListener->WindowResized(this, mBounds.Size());
  }
  if (mAttachedWidgetListener) {
    mAttachedWidgetListener->WindowResized(this, mBounds.Size());
  }
}

void HeadlessWidget::Resize(const DesktopRect& aRect, bool aRepaint) {
  auto tl =
      LayoutDeviceIntPoint::Round(aRect.TopLeft() * GetDesktopToDeviceScale());
  // Intentionally to avoid SetSizeMode() call in Move().
  MoveInternal(tl.x, tl.y);
  Resize(aRect.Size(), aRepaint);
}

void HeadlessWidget::SetSizeMode(nsSizeMode aMode) {
  LOG(("HeadlessWidget::SetSizeMode [%p] %d\n", (void*)this, aMode));

  if (aMode == mSizeMode) {
    return;
  }

  if (aMode == nsSizeMode_Normal && mSizeMode == nsSizeMode_Fullscreen) {
    MakeFullScreen(false);
    return;
  }

  mSizeMode = aMode;

  // Normally in real widget backends a window event would be triggered that
  // would cause the window manager to handle resizing the window. In headless
  // the window must manually be resized.
  ApplySizeModeSideEffects();
}

void HeadlessWidget::ApplySizeModeSideEffects() {
  if (!mVisible || mEffectiveSizeMode == mSizeMode) {
    return;
  }

  if (mEffectiveSizeMode == nsSizeMode_Normal) {
    // Store the last normal size bounds so it can be restored when entering
    // normal mode again.
    mRestoreBounds = mBounds;
  }

  switch (mSizeMode) {
    case nsSizeMode_Normal: {
      MoveInternal(mRestoreBounds.X(), mRestoreBounds.Y());
      ResizeInternal(mRestoreBounds.Width(), mRestoreBounds.Height(), false);
      break;
    }
    case nsSizeMode_Minimized:
      break;
    case nsSizeMode_Maximized: {
      nsCOMPtr<nsIScreen> screen = GetWidgetScreen();
      if (screen) {
        int32_t left, top, width, height;
        if (NS_SUCCEEDED(
                screen->GetRectDisplayPix(&left, &top, &width, &height))) {
          MoveInternal(0, 0);
          ResizeInternal(width, height, true);
        }
      }
      break;
    }
    case nsSizeMode_Fullscreen:
      // This will take care of resizing the window.
      nsIWidget::InfallibleMakeFullScreen(true);
      break;
    default:
      break;
  }

  mEffectiveSizeMode = mSizeMode;
  if (mWidgetListener) {
    mWidgetListener->SizeModeChanged(mSizeMode);
  }
}

nsresult HeadlessWidget::MakeFullScreen(bool aFullScreen) {
  // Directly update the size mode here so a later call SetSizeMode does
  // nothing.
  if (aFullScreen) {
    if (mSizeMode != nsSizeMode_Fullscreen) {
      mLastSizeMode = mSizeMode;
    }
    mSizeMode = nsSizeMode_Fullscreen;
  } else {
    mSizeMode = mLastSizeMode;
  }

  // Notify the listener first so size mode change events are triggered before
  // resize events.
  if (mWidgetListener) {
    mWidgetListener->SizeModeChanged(mSizeMode);
  }

  // Real widget backends don't seem to follow a common approach for
  // when and how many resize events are triggered during fullscreen
  // transitions. InfallibleMakeFullScreen will trigger a resize, but it
  // will be ignored if still transitioning to fullscreen, so it must be
  // triggered on the next tick.
  RefPtr<HeadlessWidget> self(this);
  NS_DispatchToCurrentThread(NS_NewRunnableFunction(
      "HeadlessWidget::MakeFullScreen", [self, aFullScreen]() -> void {
        self->InfallibleMakeFullScreen(aFullScreen);
      }));

  return NS_OK;
}

nsresult HeadlessWidget::AttachNativeKeyEvent(WidgetKeyboardEvent& aEvent) {
  HeadlessKeyBindings& bindings = HeadlessKeyBindings::GetInstance();
  return bindings.AttachNativeKeyEvent(aEvent);
}

bool HeadlessWidget::GetEditCommands(NativeKeyBindingsType aType,
                                     const WidgetKeyboardEvent& aEvent,
                                     nsTArray<CommandInt>& aCommands) {
  // Validate the arguments.
  if (NS_WARN_IF(!nsIWidget::GetEditCommands(aType, aEvent, aCommands))) {
    return false;
  }

  Maybe<WritingMode> writingMode;
  if (aEvent.NeedsToRemapNavigationKey()) {
    if (RefPtr<TextEventDispatcher> dispatcher = GetTextEventDispatcher()) {
      writingMode = dispatcher->MaybeQueryWritingModeAtSelection();
    }
  }

  HeadlessKeyBindings& bindings = HeadlessKeyBindings::GetInstance();
  bindings.GetEditCommands(aType, aEvent, writingMode, aCommands);
  return true;
}

nsresult HeadlessWidget::SynthesizeNativeMouseEvent(
    LayoutDeviceIntPoint aPoint, NativeMouseMessage aNativeMessage,
    MouseButton aButton, nsIWidget::NativeModifiers aModifierFlags,
    nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);
  EventMessage msg;
  switch (aNativeMessage) {
    case NativeMouseMessage::Move:
      msg = eMouseMove;
      break;
    case NativeMouseMessage::ButtonDown:
      msg = eMouseDown;
      break;
    case NativeMouseMessage::ButtonUp:
      msg = eMouseUp;
      break;
    case NativeMouseMessage::EnterWindow:
    case NativeMouseMessage::LeaveWindow:
      MOZ_ASSERT_UNREACHABLE("Unsupported synthesized mouse event");
      return NS_ERROR_UNEXPECTED;
  }
  WidgetMouseEvent event(true, msg, this, WidgetMouseEvent::eReal);
  event.mRefPoint = aPoint - WidgetToScreenOffset();
  if (msg == eMouseDown || msg == eMouseUp) {
    event.mButton = aButton;
  }
  if (msg == eMouseDown) {
    event.mClickCount = 1;
  }
  event.AssignEventTime(WidgetEventTime());
  DispatchInputEvent(&event);
  return NS_OK;
}

nsresult HeadlessWidget::SynthesizeNativeMouseScrollEvent(
    mozilla::LayoutDeviceIntPoint aPoint, uint32_t aNativeMessage,
    double aDeltaX, double aDeltaY, double aDeltaZ,
    nsIWidget::NativeModifiers aModifierFlags, uint32_t aAdditionalFlags,
    nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);
  // The various platforms seem to handle scrolling deltas differently,
  // but the following seems to emulate it well enough.
  WidgetWheelEvent event(true, eWheel, this);
  event.mDeltaMode = MOZ_HEADLESS_SCROLL_DELTA_MODE;
  event.mIsNoLineOrPageDelta = true;
  event.mDeltaX = -aDeltaX * MOZ_HEADLESS_SCROLL_MULTIPLIER;
  event.mDeltaY = -aDeltaY * MOZ_HEADLESS_SCROLL_MULTIPLIER;
  event.mDeltaZ = -aDeltaZ * MOZ_HEADLESS_SCROLL_MULTIPLIER;
  event.mRefPoint = aPoint - WidgetToScreenOffset();
  event.AssignEventTime(WidgetEventTime());
  DispatchInputEvent(&event);
  return NS_OK;
}

nsresult HeadlessWidget::SynthesizeNativeTouchPoint(
    uint32_t aPointerId, TouchPointerState aPointerState,
    LayoutDeviceIntPoint aPoint, double aPointerPressure,
    uint32_t aPointerOrientation, nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  MOZ_ASSERT(NS_IsMainThread());
  if (aPointerState == TOUCH_HOVER) {
    return NS_ERROR_UNEXPECTED;
  }

  if (!mSynthesizedTouchInput) {
    mSynthesizedTouchInput = std::make_unique<MultiTouchInput>();
  }

  LayoutDeviceIntPoint pointInWindow = aPoint - WidgetToScreenOffset();
  MultiTouchInput inputToDispatch = UpdateSynthesizedTouchState(
      mSynthesizedTouchInput.get(), TimeStamp::Now(), aPointerId, aPointerState,
      pointInWindow, aPointerPressure, aPointerOrientation);
  DispatchTouchInput(inputToDispatch);
  return NS_OK;
}

nsresult HeadlessWidget::SynthesizeNativeTouchpadPinch(
    TouchpadGesturePhase aEventPhase, float aScale, LayoutDeviceIntPoint aPoint,
    int32_t aModifierFlags) {
  MOZ_ASSERT(NS_IsMainThread());

  PinchGestureInput::PinchGestureType pinchGestureType =
      PinchGestureInput::PINCHGESTURE_SCALE;
  ScreenCoord CurrentSpan;
  ScreenCoord PreviousSpan;
  switch (aEventPhase) {
    case PHASE_BEGIN:
      pinchGestureType = PinchGestureInput::PINCHGESTURE_START;
      CurrentSpan = aScale;
      PreviousSpan = 0.999;
      break;

    case PHASE_UPDATE:
      pinchGestureType = PinchGestureInput::PINCHGESTURE_SCALE;
      if (aScale == mLastPinchSpan) {
        return NS_ERROR_INVALID_ARG;
      }
      CurrentSpan = aScale;
      PreviousSpan = mLastPinchSpan;
      break;

    case PHASE_END:
      pinchGestureType = PinchGestureInput::PINCHGESTURE_END;
      CurrentSpan = aScale;
      PreviousSpan = mLastPinchSpan;
      break;

    default:
      return NS_ERROR_INVALID_ARG;
  }

  ScreenPoint touchpadPoint = ViewAs<ScreenPixel>(
      aPoint - WidgetToScreenOffset(),
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);
  // The headless widget does not support modifiers.
  // Do not pass `aModifierFlags` because it contains native modifier values.
  PinchGestureInput inputToDispatch(
      pinchGestureType, PinchGestureInput::TRACKPAD, TimeStamp::Now(),
      ExternalPoint(0, 0), touchpadPoint,
      100.0 * ((aEventPhase == PHASE_END) ? ScreenCoord(1.f) : CurrentSpan),
      100.0 * ((aEventPhase == PHASE_END) ? ScreenCoord(1.f) : PreviousSpan),
      0);

  if (!inputToDispatch.SetLineOrPageDeltaY(this)) {
    return NS_ERROR_INVALID_ARG;
  }

  mLastPinchSpan = aScale;
  DispatchPinchGestureInput(inputToDispatch);
  return NS_OK;
}

nsresult HeadlessWidget::SynthesizeNativeTouchpadPan(
    TouchpadGesturePhase aEventPhase, LayoutDeviceIntPoint aPoint,
    double aDeltaX, double aDeltaY, int32_t aModifierFlags,
    nsISynthesizedEventCallback* aCallback) {
  AutoSynthesizedEventCallbackNotifier notifier(aCallback);

  MOZ_ASSERT(NS_IsMainThread());

  PanGestureInput::PanGestureType eventType = PanGestureInput::PANGESTURE_PAN;
  switch (aEventPhase) {
    case PHASE_BEGIN:
      eventType = PanGestureInput::PANGESTURE_START;
      break;
    case PHASE_UPDATE:
      eventType = PanGestureInput::PANGESTURE_PAN;
      break;
    case PHASE_END:
      eventType = PanGestureInput::PANGESTURE_END;
      break;
    default:
      return NS_ERROR_INVALID_ARG;
  }

  ScreenPoint touchpadPoint = ViewAs<ScreenPixel>(
      aPoint - WidgetToScreenOffset(),
      PixelCastJustification::LayoutDeviceIsScreenForUntransformedEvent);
  PanGestureInput input(eventType, TimeStamp::Now(), touchpadPoint,
                        ScreenPoint(float(aDeltaX), float(aDeltaY)),
                        // Same as SynthesizeNativeTouchpadPinch case we ignore
                        // aModifierFlags.
                        0);

  input.mSimulateMomentum =
      Preferences::GetBool("apz.test.headless.simulate_momentum");

  DispatchPanGestureInput(input);

  return NS_OK;
}

}  // namespace widget
}  // namespace mozilla
